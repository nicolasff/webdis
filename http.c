#include "http.h"
#include "server.h"
#include "worker.h"
#include "client.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

/* HTTP Response */

void
http_response_init(struct http_response *r, int code, const char *msg) {

	/* remove any old data */
	memset(r, 0, sizeof(struct http_response));

	r->code = code;
	r->msg = msg;

	http_response_set_header(r, "Server", "Webdis");
	return;

	/* Cross-Origin Resource Sharing, CORS. */
	http_response_set_header(r, "Allow", "GET,POST,PUT,OPTIONS");
	http_response_set_header(r, "Access-Control-Allow-Origin", "*");
}


void
http_response_set_header(struct http_response *r, const char *k, const char *v) {

	int i, pos = r->header_count;
	size_t key_sz = strlen(k);
	size_t val_sz = strlen(v);

	for(i = 0; i < r->header_count; ++i) {
		if(strncmp(r->headers[i].key, k, key_sz) == 0) {
			pos = i;
			/* free old value before replacing it. */
			free(r->headers[i].key);
			free(r->headers[i].val);
			break;
		}
	}

	/* extend array */
	if(pos == r->header_count) {
		r->headers = realloc(r->headers,
				sizeof(struct http_header)*(r->header_count + 1));
		r->header_count++;
	}

	/* copy key */
	r->headers[pos].key = calloc(key_sz + 1, 1);
	memcpy(r->headers[pos].key, k, key_sz);
	r->headers[pos].key_sz = key_sz;

	/* copy val */
	r->headers[pos].val = calloc(val_sz + 1, 1);
	memcpy(r->headers[pos].val, v, val_sz);
	r->headers[pos].val_sz = val_sz;

	if(!r->chunked && !strcmp(k, "Transfer-Encoding") && !strcmp(v, "Chunked")) {
		r->chunked = 1;
	}
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
	int i, ret, keep_alive = 0;

	sz = sizeof("HTTP/1.x xxx ")-1 + strlen(r->msg) + 2;
	s = calloc(sz + 1, 1);

	ret = sprintf(s, "HTTP/1.%d %d %s\r\n", (r->http_version?1:0), r->code, r->msg);
	p = s;

	if(r->code == 200 && r->body) {
		char content_length[10];
		sprintf(content_length, "%zd", r->body_len);
		http_response_set_header(r, "Content-Length", content_length);
	} else if(!r->chunked) {
		http_response_set_header(r, "Content-Length", "0");
	}

	for(i = 0; i < r->header_count; ++i) {
		/* "Key: Value\r\n" */
		size_t header_sz = r->headers[i].key_sz + 2 + r->headers[i].val_sz + 2;
		s = realloc(s, sz + header_sz);
		p = s + sz;

		/* add key */
		memcpy(p, r->headers[i].key, r->headers[i].key_sz);
		p += r->headers[i].key_sz;

		/* add ": " */
		memcpy(p, ": ", 2);
		p += 2;

		/* add value */
		memcpy(p, r->headers[i].val, r->headers[i].val_sz);
		p += r->headers[i].val_sz;

		/* add "\r\n" */
		memcpy(p, "\r\n", 2);
		p += 2;

		sz += header_sz;

		if(strncasecmp("Connection", r->headers[i].key, r->headers[i].key_sz) == 0 &&
			strncasecmp("Keep-Alive", r->headers[i].val, r->headers[i].val_sz) == 0) {
			keep_alive = 1;
		}
	}

	/* end of headers */
	s = realloc(s, sz + 2);
	memcpy(s + sz, "\r\n", 2);
	sz += 2;

	/* append body if there is one. */
	if(r->body && r->body_len) {
		s = realloc(s, sz + r->body_len);
		memcpy(s + sz, r->body, r->body_len);
		sz += r->body_len;
	}

	/* send buffer to client */
	ret = write(fd, s, sz);

	/* cleanup buffer */
	free(s);
	if(!keep_alive && (size_t)ret == sz) {
		/* Close fd is client doesn't support Keep-Alive. */
		close(fd);
	}

	/* cleanup response object */
	for(i = 0; i < r->header_count; ++i) {
		free(r->headers[i].key);
		free(r->headers[i].val);
	}
	free(r->headers);

	return ret == (int)sz ? 0 : 1;
}

static void
http_response_set_connection_header(struct http_client *c, struct http_response *r) {
	http_response_set_keep_alive(r, c->keep_alive);
}



/* Adobe flash cross-domain request */
void
http_crossdomain(struct http_client *c) {

	struct http_response resp;
	char out[] = "<?xml version=\"1.0\"?>\n"
"<!DOCTYPE cross-domain-policy SYSTEM \"http://www.macromedia.com/xml/dtds/cross-domain-policy.dtd\">\n"
"<cross-domain-policy>\n"
  "<allow-access-from domain=\"*\" />\n"
"</cross-domain-policy>\n";

	http_response_init(&resp, 200, "OK");
	resp.http_version = c->http_version;
	http_response_set_connection_header(c, &resp);
	http_response_set_header(&resp, "Content-Type", "application/xml");
	http_response_set_body(&resp, out, sizeof(out)-1);

	http_response_write(&resp, c->fd);
	http_client_reset(c);
}

/* Simple error response */
void
http_send_error(struct http_client *c, short code, const char *msg) {

	struct http_response resp;
	http_response_init(&resp, code, msg);
	resp.http_version = c->http_version;
	http_response_set_connection_header(c, &resp);
	http_response_set_body(&resp, NULL, 0);

	http_response_write(&resp, c->fd);
	http_client_reset(c);
}

/**
 * Set Connection field, either Keep-Alive or Close.
 */
void
http_response_set_keep_alive(struct http_response *r, int enabled) {
	if(enabled) {
		http_response_set_header(r, "Connection", "Keep-Alive");
	} else {
		http_response_set_header(r, "Connection", "Close");
	}
}

/* Response to HTTP OPTIONS */
void
http_send_options(struct http_client *c) {

	struct http_response resp;
	http_response_init(&resp, 200, "OK");
	resp.http_version = c->http_version;
	http_response_set_connection_header(c, &resp);

	http_response_set_header(&resp, "Content-Type", "text/html");
	http_response_set_header(&resp, "Content-Length", "0");

	http_response_write(&resp, c->fd);
	http_client_reset(c);
}

/**
 * Write HTTP chunk.
 */
void
http_response_write_chunk(int fd, const char *p, size_t sz) {

	char buf[64];
	int ret, chunk_size;

	chunk_size = sprintf(buf, "%x\r\n", (int)sz);
	ret = write(fd, buf, chunk_size);
	ret = write(fd, p, sz);
	ret = write(fd, "\r\n", 2);
	(void)ret;
}

