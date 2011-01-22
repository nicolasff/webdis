#ifndef HTTP_H
#define HTTP_H

#include <event.h>
#include "http-parser/http_parser.h"

typedef struct {
	const char *s;
	size_t sz;
} str_t;

struct http_client {

	/* socket and server reference */
	int fd;
	struct event ev;
	struct server *s;

	/* input buffer */
	char *buffer;
	size_t sz;

	/* http parser */
	http_parser_settings settings;
	http_parser parser;

	/* decoded http */
	enum http_method verb;

	str_t path;
	str_t body;
};

struct http_client *
http_client_new(int fd, struct server *s);

void
http_client_serve(struct http_client *c);

void
http_client_free(struct http_client *c);

int
http_on_path(http_parser*, const char *at, size_t length);

int
http_on_path(http_parser*, const char *at, size_t length);

int
http_on_body(http_parser*, const char *at, size_t length);

#endif
