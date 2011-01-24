#ifndef HTTP_H
#define HTTP_H

#include <sys/types.h>

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

/* HTTP response */
void
http_response_set_header(struct http_response *r, const char *k, const char *v);

void
http_response_set_body(struct http_response *r, const char *body, size_t body_len);

int
http_response_write(struct http_response *r, int fd);

#endif
