#ifndef HTTP_H
#define HTTP_H

#include <event.h>
#include <arpa/inet.h>
#include "http-parser/http_parser.h"

typedef struct {
	char *s;
	size_t sz;
} str_t;

struct http_client {

	/* socket and server reference */
	int fd;
	in_addr_t addr;
	struct event ev;
	struct server *s;
	int executing;

	/* http parser */
	http_parser_settings settings;
	http_parser parser;

	/* decoded http */
	enum http_method verb;
	str_t path;
	str_t body;
	str_t header_connection;
	str_t header_if_none_match;
	str_t header_authorization;

	/* response headers */
	str_t out_content_type;
	str_t out_etag;

	str_t qs_type;
	str_t qs_jsonp;

	/* private, used in HTTP parser */
	str_t last_header_name;
};

struct http_response {
	short code;
	const char *msg;

	str_t *headers;
	int header_count;

	const char *body;
	size_t body_len;
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

int
http_on_query_string(http_parser*, const char *at, size_t length);


void
http_set_header(str_t *h, const char *p, size_t sz);

void
http_send_reply(struct http_client *c, short code, const char *msg,
		const char *body, size_t body_len);

void
http_send_error(struct http_client *c, short code, const char *msg);

/* HTTP response */
void
http_response_init(struct http_client *c, struct http_response *r, int code, const char *msg);

void
http_response_set_header(struct http_response *r, const char *k, const char *v);

void
http_response_set_body(struct http_response *r, const char *body, size_t body_len);

int
http_response_send(struct http_response *r, int fd);


#endif
