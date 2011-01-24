#include "http.h"
#include "server.h"
#include "slog.h"
#include "cmd.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

void
http_set_header(str_t *h, const char *p, size_t sz) {

	h->s = calloc(1, 1+sz);
	memcpy(h->s, p, sz);
	h->sz = sz;
}

/* HTTP Response */

void
http_response_set_header(struct http_response *r, const char *k, const char *v) {

	int i, pos = r->header_count;
	size_t sz;
	char *s;

	sz = strlen(k) + 2 + strlen(v) + 2;
	s = calloc(sz + 1, 1);
	sprintf(s, "%s: %s\r\n", k, v);

	for(i = 0; i < r->header_count; ++i) {
		size_t klen = strlen(k);
		if(strncmp(r->headers[i].s, k, klen) == 0 && r->headers[i].s[klen] == ':') {
			pos = i;
			/* free old value before replacing it. */
			free(r->headers[i].s);
			break;
		}
	}

	/* extend array */
	if(pos == r->header_count) {
		r->headers = realloc(r->headers, sizeof(str_t)*(r->header_count + 1));
		r->header_count++;
	}
	r->headers[pos].s = s;
	r->headers[pos].sz = sz;

}

void
http_response_set_body(struct http_response *r, const char *body, size_t body_len) {

	r->body = body;
	r->body_len = body_len;
}

int
http_response_write(struct http_response *r, int fd) {

	char *s = NULL, *p;
	size_t sz = 0;
	int i, ret;

	sz = sizeof("HTTP/1.x xxx ")-1 + strlen(r->msg) + 2;
	s = calloc(sz + 1, 1);

	ret = sprintf(s, "HTTP/1.1 %d %s\r\n", r->code, r->msg);
	p = s; // + ret - 3;

	if(r->code == 200 && r->body) {
		char content_length[10];
		sprintf(content_length, "%zd", r->body_len);
		http_response_set_header(r, "Content-Length", content_length);
		http_response_set_header(r, "Content-Length", content_length);
		http_response_set_header(r, "Content-Length", content_length);
	}

	for(i = 0; i < r->header_count; ++i) {
		s = realloc(s, sz + r->headers[i].sz);
		p = s + sz;
		memcpy(p, r->headers[i].s, r->headers[i].sz);

		p += r->headers[i].sz;
		sz += r->headers[i].sz;
	}
	
	/* end of headers */
	s = realloc(s, sz + 2);
	memcpy(s + sz, "\r\n", 2);
	sz += 2;

	if(r->body && r->body_len) {
		s = realloc(s, sz + r->body_len);
		memcpy(s + sz, r->body, r->body_len);
		sz += r->body_len;
	}
	
	ret = write(fd, s, sz);
	free(s);

	/* cleanup response object */
	for(i = 0; i < r->header_count; ++i) {
		free(r->headers[i].s);
	}
	free(r->headers);

	return ret == (int)sz ? 0 : 1;
}

