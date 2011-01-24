#ifndef HTTP_H
#define HTTP_H

#include <event.h>
#include <arpa/inet.h>
#include "http-parser/http_parser.h"

struct http_client; /* FIXME: this shouldn't be here. */

typedef struct {
	char *s;
	size_t sz;
} str_t;

struct http_response {
	short code;
	const char *msg;

	str_t *headers;
	int header_count;

	const char *body;
	size_t body_len;
};

void
http_set_header(str_t *h, const char *p, size_t sz);

/* Transfer-encoding: chunked */
void
http_send_reply_start(struct http_client *c, short code, const char *msg);

void
http_send_reply_chunk(struct http_client *c, const char *p, size_t sz);

void
http_send_reply_end(struct http_client *c);

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
http_response_write(struct http_response *r, int fd);

#endif
