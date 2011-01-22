#include "http.h"
#include "server.h"
#include "slog.h"
#include "cmd.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

struct http_client *
http_client_new(int fd, struct server *s) {

	struct http_client *c = calloc(1, sizeof(struct http_client));
	c->fd = fd;
	c->s = s;

	c->settings.on_path = http_on_path;
	c->settings.on_body = http_on_body;
	c->settings.on_message_complete = http_on_complete;
	c->settings.on_header_field = http_on_header_name;
	c->settings.on_header_value = http_on_header_value;

	http_parser_init(&c->parser, HTTP_REQUEST);
	c->parser.data = c;

	return c;
}


void
http_client_read(int fd, short event, void *ctx) {

	struct http_client *c = ctx;
	char buffer[64*1024];
	int ret, nparsed;

	(void)fd;
	(void)event;

	ret = read(c->fd, buffer, sizeof(buffer));
	if(ret <= 0) { /* broken connection, bye */
		http_client_free(c);
		return;
	}

	c->buffer = realloc(c->buffer, c->sz + ret);
	memcpy(c->buffer + c->sz, buffer, ret);
	c->sz += ret;

	/* TODO: http parse. */
	nparsed = http_parser_execute(&c->parser, &c->settings, buffer, ret);
	if(c->parser.upgrade) {
		/* TODO */
	} else if(nparsed != ret) { /* invalid */
		http_client_free(c);
		return;
	}

	http_client_serve(c);
}

void
http_client_free(struct http_client *c) {

	event_del(&c->ev);
	close(c->fd);
	free(c->buffer);
	free((char*)c->out_content_type.s);
	free(c);
}

static int
http_client_keep_alive(struct http_client *c) {

	/* check disconnection */
	int disconnect = 0;

	if(c->parser.http_major == 1 && c->parser.http_minor == 0) {
		disconnect = 1; /* HTTP 1.0: disconnect by default */
	}
	if(c->header_connection.s) {
		if(strncasecmp(c->header_connection.s, "Keep-Alive", 10) == 0) {
			disconnect = 0;
		} else if(strncasecmp(c->header_connection.s, "Close", 5) == 0) {
			disconnect = 1;
		}
	}
	return disconnect ? 0 : 1;
}

void
http_client_reset(struct http_client *c) {

	if(!http_client_keep_alive(c)) {
		http_client_free(c);
		return;
	}

	memset(&c->path, 0, sizeof(str_t));
	memset(&c->body, 0, sizeof(str_t));
	memset(&c->header_connection, 0, sizeof(str_t));
	memset(&c->header_if_none_match, 0, sizeof(str_t));

	free((char*)c->out_content_type.s);
	memset(&c->out_content_type, 0, sizeof(str_t));
	memset(&c->out_etag, 0, sizeof(str_t));

	free(c->buffer);
	c->buffer = NULL;
	c->sz = 0;

	memset(&c->verb, 0, sizeof(c->verb));

	http_parser_init(&c->parser, HTTP_REQUEST);
}

/**
 * Add read event callback
 */
void
http_client_serve(struct http_client *c) {

	event_set(&c->ev, c->fd, EV_READ, http_client_read, c);
	event_base_set(c->s->base, &c->ev);
	event_add(&c->ev, NULL);
}

int
http_on_path(http_parser *p, const char *at, size_t length) {

	struct http_client *c = p->data;

	c->path.s = at;
	c->path.sz = length;

	/* save HTTP verb as well */
	c->verb = (enum http_method)p->method;

	return 0;
}

int
http_on_body(http_parser *p, const char *at, size_t length) {
	struct http_client *c = p->data;

	c->body.s = at;
	c->body.sz = length;

	return 0;
}

int
http_on_complete(http_parser *p) {
	struct http_client *c = p->data;
	int ret = -1;

	/* check that the command can be executed */
	switch(c->verb) {
		case HTTP_GET:
			/* slog(s, WEBDIS_DEBUG, uri); */
			ret = cmd_run(c->s, c, 1+c->path.s, c->path.sz-1, NULL, 0);
			break;

		case HTTP_POST:
			/*slog(s, WEBDIS_DEBUG, uri);*/
			ret = cmd_run(c->s, c, 1+c->body.s, c->body.sz-1, NULL, 0);
			break;

		case HTTP_PUT:
			/* slog(s, WEBDIS_DEBUG, uri); */
			ret = cmd_run(c->s, c, 1+c->path.s, c->path.sz-1,
					c->body.s, c->body.sz);
			break;

		case HTTP_OPTIONS:
			/* on_options(rq); */
			return 0;

		default:
			slog(c->s, WEBDIS_DEBUG, "405");
			/* evhttp_send_reply(rq, 405, "Method Not Allowed", NULL); */
			return 0;
	}
	return ret;
}

void
http_send_reply(struct http_client *c, short code, const char *msg,
		const char *body, size_t body_len) {

	char *out = NULL;
	size_t sz = 0;
	int ret = 0, ok;

	const char *ct = c->out_content_type.s;
	if(!ct) {
		ct = "text/html";
	}

	while(1) {
		ret = snprintf(out, sz, "HTTP/1.1 %d %s\r\n"
			"Content-Type: %s\r\n"
			"Content-Length: %zd\r\n"
			"ETag: %s\r\n"
			"Connection: %s\r\n"
			"Server: Webdis\r\n"
			"\r\n", code, msg, ct, body_len,
			(c->out_etag.s ? c->out_etag.s : "\"\""),
			(http_client_keep_alive(c) ? "Keep-Alive" : "Close")
			);

		if(!out) { /* first step: allocate space */
			sz = ret + body_len;
			out = malloc(sz);
		} else { /* second step: copy body and leave loop */
			if(body && body_len) memcpy(out + ret, body, body_len);
			break;
		}
	}

	ok = write(c->fd, out, sz);
	if(ok != (int)sz) {
		http_client_free(c);
	}
	free(out);

	http_client_reset(c);
}

void
http_set_header(str_t *h, const char *p) {

	h->s = strdup(p);
	h->sz = strlen(p);
}

/**
 * Called when a header name is read
 */
int
http_on_header_name(http_parser *p, const char *at, size_t length) {

	struct http_client *c = p->data;

	c->last_header_name.s = at;
	c->last_header_name.sz = length;

	return 0;
}

/**
 * Called when a header value is read
 */
int
http_on_header_value(http_parser *p, const char *at, size_t length) {

	struct http_client *c = p->data;

	if(strncmp("Connection", c->last_header_name.s, c->last_header_name.sz) == 0) {
		c->header_connection.s = at;
		c->header_connection.sz = length;
	} else if(strncmp("If-None-Match", c->last_header_name.s, c->last_header_name.sz) == 0) {
		c->header_if_none_match.s = at;
		c->header_if_none_match.sz = length;
	}
	return 0;
}
