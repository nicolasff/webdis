#include "http.h"
#include "server.h"

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
