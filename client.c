#include "client.h"
#include "http_parser.h"
#include "http.h"
#include "server.h"
#include "worker.h"
#include "websocket.h"
#include "cmd.h"
#include "conf.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

#define CHECK_ALLOC(c, ptr) if(!(ptr)) { c->failed_alloc = 1; return -1;}

static int
http_client_on_url(struct http_parser *p, const char *at, size_t sz) {

	struct http_client *c = p->data;

	CHECK_ALLOC(c, c->path = realloc(c->path, c->path_sz + sz + 1));
	memcpy(c->path + c->path_sz, at, sz);
	c->path_sz += sz;
	c->path[c->path_sz] = 0;

	return 0;
}

/*
 * Called when the body is parsed.
 */
static int
http_client_on_body(struct http_parser *p, const char *at, size_t sz) {

	struct http_client *c = p->data;
	return http_client_add_to_body(c, at, sz);
}

int
http_client_add_to_body(struct http_client *c, const char *at, size_t sz) {

	CHECK_ALLOC(c, c->body = realloc(c->body, c->body_sz + sz + 1));
	memcpy(c->body + c->body_sz, at, sz);
	c->body_sz += sz;
	c->body[c->body_sz] = 0;

	return 0;
}

static int
http_client_on_header_name(struct http_parser *p, const char *at, size_t sz) {

	struct http_client *c = p->data;
	size_t n = c->header_count;

	/* if we're not adding to the same header name as last time, realloc to add one field. */
	if(c->last_cb != LAST_CB_KEY) {
		n = ++c->header_count;
		CHECK_ALLOC(c, c->headers = realloc(c->headers, n * sizeof(struct http_header)));
		memset(&c->headers[n-1], 0, sizeof(struct http_header));
	}

	/* Add data to the current header name. */
	CHECK_ALLOC(c, c->headers[n-1].key = realloc(c->headers[n-1].key,
			c->headers[n-1].key_sz + sz + 1));
	memcpy(c->headers[n-1].key + c->headers[n-1].key_sz, at, sz);
	c->headers[n-1].key_sz += sz;
	c->headers[n-1].key[c->headers[n-1].key_sz] = 0;

	c->last_cb = LAST_CB_KEY;

	return 0;
}

static char *
wrap_filename(const char *val, size_t val_len) {

	char format[] = "attachment; filename=\"";
	size_t sz = sizeof(format) - 1 + val_len + 1;
	char *p = calloc(sz + 1, 1);

	memcpy(p, format, sizeof(format)-1); /* copy format */
	memcpy(p + sizeof(format)-1, val, val_len); /* copy filename */
	p[sz-1] = '"';

	return p;
}

/*
 * Split query string into key/value pairs, process some of them.
 */
static int
http_client_on_query_string(struct http_parser *parser, const char *at, size_t sz) {

	struct http_client *c = parser->data;
	const char *p = at;

	while(p < at + sz) {

		const char *key = p, *val;
		int key_len, val_len;
		char *eq = memchr(key, '=', sz - (p-at));
		if(!eq || eq > at + sz) { /* last argument */
			break;
		} else { /* found an '=' */
			char *amp;
			val = eq + 1;
			key_len = eq - key;
			p = eq + 1;

			amp = memchr(p, '&', sz - (p-at));
			if(!amp || amp > at + sz) {
				val_len = at + sz - p; /* last arg */
			} else {
				val_len = amp - val; /* cur arg */
				p = amp + 1;
			}

			if(key_len == 4 && strncmp(key, "type", 4) == 0) {
				c->type = calloc(1 + val_len, 1);
				memcpy(c->type, val, val_len);
			} else if((key_len == 5 && strncmp(key, "jsonp", 5) == 0)
				|| (key_len == 8 && strncmp(key, "callback", 8) == 0)) {
				c->jsonp = calloc(1 + val_len, 1);
				memcpy(c->jsonp, val, val_len);
			} else if(key_len == 3 && strncmp(key, "sep", 3) == 0) {
				c->separator = calloc(1 + val_len, 1);
				memcpy(c->separator, val, val_len);
			} else if(key_len == 8 && strncmp(key, "filename", 8) == 0) {
				c->filename = wrap_filename(val, val_len);
			}

			if(!amp) {
				break;
			}
		}
	}
	return 0;
}

static int
http_client_on_header_value(struct http_parser *p, const char *at, size_t sz) {

	struct http_client *c = p->data;
	size_t n = c->header_count;

	/* Add data to the current header value. */
	CHECK_ALLOC(c, c->headers[n-1].val = realloc(c->headers[n-1].val,
			c->headers[n-1].val_sz + sz + 1));
	memcpy(c->headers[n-1].val + c->headers[n-1].val_sz, at, sz);
	c->headers[n-1].val_sz += sz;
	c->headers[n-1].val[c->headers[n-1].val_sz] = 0;

	c->last_cb = LAST_CB_VAL;


	/* react to some values. */
	if(strncmp("Expect", c->headers[n-1].key, c->headers[n-1].key_sz) == 0) {
		if(sz == 12 && strncasecmp(at, "100-continue", sz) == 0) {
			/* support HTTP file upload */
			char http100[] = "HTTP/1.1 100 Continue\r\n\r\n";
			int ret = write(c->fd, http100, sizeof(http100)-1);
			(void)ret;
		}
	} else if(strncasecmp("Connection", c->headers[n-1].key, c->headers[n-1].key_sz) == 0) {
		if(sz == 10 && strncasecmp(at, "Keep-Alive", sz) == 0) {
			c->keep_alive = 1;
		}
	}

	return 0;
}

static int
http_client_on_message_complete(struct http_parser *p) {

	struct http_client *c = p->data;

	/* keep-alive detection */
	if (c->parser.flags & F_CONNECTION_CLOSE) {
		c->keep_alive = 0;
	} else if(c->parser.http_major == 1 && c->parser.http_minor == 1) { /* 1.1 */
		c->keep_alive = 1;
	}
	c->http_version = c->parser.http_minor;

	if(p->upgrade && c->w->s->cfg->websockets) { /* WebSocket, don't execute just yet */
		c->is_websocket = 1;
		return 0;
	}

	/* handle default root object */
	if(c->path_sz == 1 && *c->path == '/' && c->w->s->cfg->default_root) { /* replace */
		free(c->path);
		c->path = strdup(c->w->s->cfg->default_root);
		c->path_sz = strlen(c->path);
	}


	worker_process_client(c);
	http_client_reset(c);

	return 0;
}

struct http_client *
http_client_new(struct worker *w, int fd, in_addr_t addr) {

	struct http_client *c = calloc(1, sizeof(struct http_client));

	c->fd = fd;
	c->w = w;
	c->addr = addr;
	c->s = w->s;

	/* parser */
	http_parser_init(&c->parser, HTTP_REQUEST);
	c->parser.data = c;

	/* callbacks */
	c->settings.on_url = http_client_on_url;
	c->settings.on_query_string = http_client_on_query_string;
	c->settings.on_body = http_client_on_body;
	c->settings.on_message_complete = http_client_on_message_complete;
	c->settings.on_header_field = http_client_on_header_name;
	c->settings.on_header_value = http_client_on_header_value;

	c->last_cb = LAST_CB_NONE;

	return c;
}


void
http_client_reset(struct http_client *c) {

	int i;

	/* headers */
	for(i = 0; i < c->header_count; ++i) {
		free(c->headers[i].key);
		free(c->headers[i].val);
	}
	free(c->headers);
	c->headers = NULL;
	c->header_count = 0;

	/* other data */
	free(c->body); c->body = NULL;
	c->body_sz = 0;
	free(c->path); c->path = NULL;
	c->path_sz = 0;
	free(c->type); c->type = NULL;
	free(c->jsonp); c->jsonp = NULL;
	free(c->filename); c->filename = NULL;
	c->request_sz = 0;

	/* no last known header callback */
	c->last_cb = LAST_CB_NONE;

	/* mark as broken if client doesn't support Keep-Alive. */
	if(c->keep_alive == 0) {
		c->broken = 1;
	}
}

void
http_client_free(struct http_client *c) {

	http_client_reset(c);
	free(c->buffer);
	free(c);
}

int
http_client_read(struct http_client *c) {

	char buffer[4096];
	int ret;

	ret = read(c->fd, buffer, sizeof(buffer));
	if(ret <= 0) {
		/* broken link, free buffer and client object */

		/* disconnect pub/sub client if there is one. */
		if(c->pub_sub && c->pub_sub->ac) {
			struct cmd *cmd = c->pub_sub;

			/* disconnect from all channels */
			redisAsyncDisconnect(c->pub_sub->ac);
			if(c->pub_sub) c->pub_sub->ac = NULL;
			c->pub_sub = NULL;

			/* delete command object */
			cmd_free(cmd);
		}

		close(c->fd);

		http_client_free(c);
		return (int)CLIENT_DISCONNECTED;
	}

	/* save what we've just read */
	c->buffer = realloc(c->buffer, c->sz + ret);
	if(!c->buffer) {
		return (int)CLIENT_OOM;
	}
	memcpy(c->buffer + c->sz, buffer, ret);
	c->sz += ret;

	/* keep track of total sent */
	c->request_sz += ret;

	return ret;
}

int
http_client_remove_data(struct http_client *c, size_t sz) {

	char *buffer;
	if(c->sz < sz)
		return -1;

	/* replace buffer */
	CHECK_ALLOC(c, buffer = malloc(c->sz - sz));
	memcpy(buffer, c->buffer + sz, c->sz - sz);
	free(c->buffer);
	c->buffer = buffer;
	c->sz -= sz;

	return 0;
}

int
http_client_execute(struct http_client *c) {

	int nparsed = http_parser_execute(&c->parser, &c->settings, c->buffer, c->sz);

	if(!c->is_websocket) {
		/* removed consumed data, all has been copied already. */
		free(c->buffer);
		c->buffer = NULL;
		c->sz = 0;
	}
	return nparsed;
}

/*
 * Find header value, returns NULL if not found.
 */
const char *
client_get_header(struct http_client *c, const char *key) {

	int i;
	size_t sz = strlen(key);

	for(i = 0; i < c->header_count; ++i) {

		if(sz == c->headers[i].key_sz &&
			strncasecmp(key, c->headers[i].key, sz) == 0) {
			return c->headers[i].val;
		}

	}

	return NULL;
}

