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
	int needs_free;

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
	str_t header_connection;

	str_t out_content_type;
	str_t out_etag;

	/* private, used in HTTP parser */
	str_t last_header_name;
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

int
http_on_header_name(http_parser*, const char *at, size_t length);

int
http_on_header_value(http_parser*, const char *at, size_t length);

int
http_on_complete(http_parser*);

void
http_set_header(str_t *h, const char *p);

void
http_send_reply(struct http_client *c, short code, const char *msg,
		const char *body, size_t body_len);

#endif
