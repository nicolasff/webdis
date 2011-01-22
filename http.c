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
		printf("close client %d\n", c->fd);
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
	}
	/*
	printf("parse %zd bytes: [", c->sz); fflush(stdout);
	write(1, c->buffer, c->sz);
	printf("]\n");
	*/


	/* carry on */
	http_client_serve(c);
}

void
http_client_free(struct http_client *c) {

	close(c->fd);
	free(c->buffer);
	free((char*)c->out_content_type.s);
	free(c);
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
	int ret;

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
	return 0;
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
			"Server: Webdis\r\n"
			"\r\n", code, msg, ct, body_len);

		if(!out) {
			sz = ret + body_len;
			out = malloc(sz);
		} else {
			if(body && body_len) memcpy(out + ret, body, body_len);
			break;
		}
	}

	ok = write(c->fd, out, sz);
	if(!ok) {
		http_client_free(c);
	}
	free(out);
}

void
http_set_header(str_t *h, const char *p) {

	h->s = strdup(p);
	h->sz = strlen(p);
}
