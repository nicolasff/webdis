#ifndef HTTP_H
#define HTTP_H

#include <sys/types.h>
#include <event.h>

struct http_client;
struct worker;

struct http_header {
	char *key;
	size_t key_sz;

	char *val;
	size_t val_sz;
};


struct http_response {

	struct event ev;

	short code;
	const char *msg;

	struct http_header *headers;
	int header_count;

	const char *body;
	size_t body_len;

	char *out;
	size_t out_sz;

	int chunked;
	int http_version;
	int keep_alive;
	int sent;

	struct worker *w;
};

/* HTTP response */

struct http_response *
http_response_init(struct worker *w, int code, const char *msg);

void
http_response_set_header(struct http_response *r, const char *k, const char *v);

void
http_response_set_body(struct http_response *r, const char *body, size_t body_len);

void
http_response_write(struct http_response *r, int fd);

void
http_schedule_write(int fd, struct http_response *r);

void
http_crossdomain(struct http_client *c);

void
http_send_error(struct http_client *c, short code, const char *msg);

void
http_send_options(struct http_client *c);

void
http_response_write_chunk(int fd, struct worker *w, const char *p, size_t sz);

void
http_response_set_keep_alive(struct http_response *r, int enabled);

#endif
