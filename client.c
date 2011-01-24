#include "client.h"
#include "server.h"
#include "cmd.h"
#include "http.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "hiredis/async.h"

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
	c->settings.on_query_string = http_on_query_string;

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

	/* TODO: http parse. */
	nparsed = http_parser_execute(&c->parser, &c->settings, buffer, ret);
	if(c->parser.upgrade) {
		/* TODO */
	} else if(nparsed != ret) { /* invalid */
		http_client_free(c);
		return;
	}

	if(!c->executing) {
		http_client_serve(c);
	}
}

static void
http_client_cleanup(struct http_client *c) {

	if(c->sub) {
		return; /* we need to keep those. */
	}

	free(c->path.s);
	memset(&c->path, 0, sizeof(str_t));

	free(c->body.s);
	memset(&c->body, 0, sizeof(str_t));

	free(c->input_headers.connection.s);
	memset(&c->input_headers.connection, 0, sizeof(str_t));

	free(c->input_headers.if_none_match.s);
	memset(&c->input_headers.if_none_match, 0, sizeof(str_t));

	free(c->input_headers.authorization.s);
	memset(&c->input_headers.authorization, 0, sizeof(str_t));

	free(c->output_headers.content_type.s);
	memset(&c->output_headers.content_type, 0, sizeof(str_t));

	free(c->output_headers.etag.s);
	memset(&c->output_headers.etag, 0, sizeof(str_t));

	free(c->qs_type.s);
	memset(&c->qs_type, 0, sizeof(str_t));

	free(c->qs_jsonp.s);
	memset(&c->qs_jsonp, 0, sizeof(str_t));

	memset(&c->verb, 0, sizeof(c->verb));

	c->executing = 0;
}

void
http_client_free(struct http_client *c) {

	event_del(&c->ev);
	close(c->fd);

	if(c->sub) {
		/* clean up redis object */
		redisAsyncFree(c->sub->s->ac);

		/* clean up command object */
		if(c->sub->cmd) {
			cmd_free(c->sub->cmd);
		}
		free(c->sub);
		c->sub = NULL;
	}

	http_client_cleanup(c);
	free(c);
}

int
http_client_keep_alive(struct http_client *c) {

	/* check disconnection */
	int disconnect = 0;

	if(c->parser.http_major == 0) {
		disconnect = 1; /* No version given. */
	} else if(c->parser.http_major == 1 && c->parser.http_minor == 0) {
		disconnect = 1; /* HTTP 1.0: disconnect by default */
	}
	if(c->input_headers.connection.s) {
		if(strncasecmp(c->input_headers.connection.s, "Keep-Alive", 10) == 0) {
			disconnect = 0;
		} else if(strncasecmp(c->input_headers.connection.s, "Close", 5) == 0) {
			disconnect = 1;
		}
	}
	return disconnect ? 0 : 1;
}

void
http_client_reset(struct http_client *c) {

	if(!http_client_keep_alive(c) && !c->sub) {
		http_client_free(c);
		return;
	}

	http_client_cleanup(c);
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

/**** Parser callbacks ****/

/**
 * Called when the path has been found. This is before any `?query-string'.
 */
int
http_on_path(http_parser *p, const char *at, size_t length) {

	struct http_client *c = p->data;

	c->path.s = calloc(length+1, 1);
	memcpy(c->path.s, at, length);
	c->path.sz = length;

	/* save HTTP verb as well */
	c->verb = (enum http_method)p->method;

	return 0;
}

/**
 * Called when the whole body has been read.
 */
int
http_on_body(http_parser *p, const char *at, size_t length) {
	struct http_client *c = p->data;

	c->body.s = calloc(length+1, 1);
	memcpy(c->body.s, at, length);
	c->body.sz = length;

	return 0;
}

/**
 * Called when the query string has been completely read.
 */
int
http_on_query_string(http_parser *parser, const char *at, size_t length) {

	struct http_client *c = parser->data;
	const char *p = at;

	while(p < at + length) {

		const char *key = p, *val;
		int key_len, val_len;
		char *eq = memchr(key, '=', length - (p-at));
		if(!eq || eq > at + length) { /* last argument */
			break;
		} else { /* found an '=' */
			char *and;
			val = eq + 1;
			key_len = eq - key;
			p = eq + 1;

			and = memchr(p, '&', length - (p-at));
			if(!and || and > at + length) {
				val_len = at + length - p; /* last arg */
			} else {
				val_len = and - val; /* cur arg */
				p = and + 1;
			}

			if(key_len == 4 && strncmp(key, "type", 4) == 0) {
				http_set_header(&c->qs_type, val, val_len);
			} else if(key_len == 5 && strncmp(key, "jsonp", 5) == 0) {
				http_set_header(&c->qs_jsonp, val, val_len);
			}

			if(!and) {
				break;
			}
		}
	}
	return 0;
}

