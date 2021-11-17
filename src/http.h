#ifndef HTTP_H
#define HTTP_H

#include <sys/types.h>
#include <event.h>

struct http_client;
struct worker;

/* bit flags */
typedef enum {
	HEADER_COPY_NONE = 0,	/* don't strdup key or value */
	HEADER_COPY_KEY = 1,	/* strdup key only */
	HEADER_COPY_VALUE = 2,	/* strdup value only */
	HEADER_CHECK_DUPE = 4	/* replace duplicate when adding header */
} header_copy;

struct http_header {
	char *key;
	size_t key_sz;

	char *val;
	size_t val_sz;

	header_copy copy;
};


struct http_response {

	struct event ev;

	short code;
	const char *msg;

	struct http_header *headers;
	int header_count; /* actual count in array */
	int headers_array_size; /* allocated size */

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

struct http_response *
http_response_init_with_buffer(struct worker *w, char *data, size_t data_sz, int keep_alive);

void
http_response_set_header(struct http_response *r, const char *k, const char *v, header_copy copy);

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
