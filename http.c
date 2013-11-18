#include "http.h"
#include "server.h"
#include "worker.h"
#include "client.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

/* HTTP Response */

struct http_response *
http_response_init(struct worker *w, int code, const char *msg) {

	/* create object */
	struct http_response *r = calloc(1, sizeof(struct http_response));

	r->code = code;
	r->msg = msg;
	r->w = w;
	r->keep_alive = 0; /* default */

	http_response_set_header(r, "Server", "Webdis");

	/* Cross-Origin Resource Sharing, CORS. */
	http_response_set_header(r, "Allow", "GET,POST,PUT,OPTIONS");
	/*
	Chrome doesn't support Allow and requires 
	Access-Control-Allow-Methods
	*/
	http_response_set_header(r, "Access-Control-Allow-Methods", "GET,POST,PUT,OPTIONS");
	http_response_set_header(r, "Access-Control-Allow-Origin", "*");
	/* 
	According to 
	http://www.w3.org/TR/cors/#access-control-allow-headers-response-header
	Access-Control-Allow-Headers cannot be a wildcard and must be set
	with explicit names
	*/
	http_response_set_header(r, "Access-Control-Allow-Headers", "X-Requested-With, Content-Type, Authorization");

	return r;
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

	if(!r->chunked && !strcmp(k, "Transfer-Encoding") && !strcmp(v, "chunked")) {
		r->chunked = 1;
	}
}

void
http_response_set_body(struct http_response *r, const char *body, size_t body_len) {

	r->body = body;
	r->body_len = body_len;
}

static void
http_response_cleanup(struct http_response *r, int fd, int success) {

	int i;

	/* cleanup buffer */
	free(r->out);
	if(!r->keep_alive || !success) {
		/* Close fd is client doesn't support Keep-Alive. */
		close(fd);
	}

	/* cleanup response object */
	for(i = 0; i < r->header_count; ++i) {
		free(r->headers[i].key);
		free(r->headers[i].val);
	}
	free(r->headers);

	free(r);
}

static void
http_can_write(int fd, short event, void *p) {

	int ret;
	struct http_response *r = p;

	(void)event;
	
	ret = write(fd, r->out + r->sent, r->out_sz - r->sent);
	
	if(ret > 0)
		r->sent += ret;

	if(ret <= 0 || r->out_sz - r->sent == 0) { /* error or done */
		http_response_cleanup(r, fd, (int)r->out_sz == r->sent ? 1 : 0);
	} else { /* reschedule write */
		http_schedule_write(fd, r);
	}
}

void
http_schedule_write(int fd, struct http_response *r) {

	if(r->w) { /* async */
		event_set(&r->ev, fd, EV_WRITE, http_can_write, r);
		event_base_set(r->w->base, &r->ev);
		event_add(&r->ev, NULL);
	} else { /* blocking */
		http_can_write(fd, 0, r);
	}

}

static char *
format_chunk(const char *p, size_t sz, size_t *out_sz) {

	char *out, tmp[64];
	int chunk_size;

	/* calculate format size */
	chunk_size = sprintf(tmp, "%x\r\n", (int)sz);
	
	*out_sz = chunk_size + sz + 2;
	out = malloc(*out_sz);
	memcpy(out, tmp, chunk_size);
	memcpy(out + chunk_size, p, sz);
	memcpy(out + chunk_size + sz, "\r\n", 2);

	return out;
}

void
http_response_write(struct http_response *r, int fd) {

	char *p;
	int i, ret;

	/*r->keep_alive = 0;*/
	r->out_sz = sizeof("HTTP/1.x xxx ")-1 + strlen(r->msg) + 2;
	r->out = calloc(r->out_sz + 1, 1);

	ret = sprintf(r->out, "HTTP/1.%d %d %s\r\n", (r->http_version?1:0), r->code, r->msg);
	(void)ret;
	p = r->out;

	if(!r->chunked) {
		if(r->code == 200 && r->body) {
			char content_length[10];
			sprintf(content_length, "%zd", r->body_len);
			http_response_set_header(r, "Content-Length", content_length);
		} else {
			http_response_set_header(r, "Content-Length", "0");
		}
	}

	for(i = 0; i < r->header_count; ++i) {
		/* "Key: Value\r\n" */
		size_t header_sz = r->headers[i].key_sz + 2 + r->headers[i].val_sz + 2;
		r->out = realloc(r->out, r->out_sz + header_sz);
		p = r->out + r->out_sz;

		/* add key */
		memcpy(p, r->headers[i].key, r->headers[i].key_sz);
		p += r->headers[i].key_sz;

		/* add ": " */
		*(p++) = ':';
		*(p++) = ' ';

		/* add value */
		memcpy(p, r->headers[i].val, r->headers[i].val_sz);
		p += r->headers[i].val_sz;

		/* add "\r\n" */
		*(p++) = '\r';
		*(p++) = '\n';

		r->out_sz += header_sz;

		if(strncasecmp("Connection", r->headers[i].key, r->headers[i].key_sz) == 0 &&
			strncasecmp("Keep-Alive", r->headers[i].val, r->headers[i].val_sz) == 0) {
			r->keep_alive = 1;
		}
	}

	/* end of headers */
	r->out = realloc(r->out, r->out_sz + 2);
	memcpy(r->out + r->out_sz, "\r\n", 2);
	r->out_sz += 2;

	/* append body if there is one. */
	if(r->body && r->body_len) {

		char *tmp = (char*)r->body;
		size_t tmp_len = r->body_len;
		if(r->chunked) { /* replace body with formatted chunk */
			tmp = format_chunk(r->body, r->body_len, &tmp_len);
		}

		r->out = realloc(r->out, r->out_sz + tmp_len);
		memcpy(r->out + r->out_sz, tmp, tmp_len);
		r->out_sz += tmp_len;

		if(r->chunked) { /* need to free the chunk */
			free(tmp);
		}
	}

	/* send buffer to client */
	r->sent = 0;
	http_schedule_write(fd, r);
}

static void
http_response_set_connection_header(struct http_client *c, struct http_response *r) {
	http_response_set_keep_alive(r, c->keep_alive);
}



/* Adobe flash cross-domain request */
void
http_crossdomain(struct http_client *c) {

	struct http_response *resp = http_response_init(NULL, 200, "OK");
	char out[] = "<?xml version=\"1.0\"?>\n"
"<!DOCTYPE cross-domain-policy SYSTEM \"http://www.macromedia.com/xml/dtds/cross-domain-policy.dtd\">\n"
"<cross-domain-policy>\n"
  "<allow-access-from domain=\"*\" />\n"
"</cross-domain-policy>\n";

	resp->http_version = c->http_version;
	http_response_set_connection_header(c, resp);
	http_response_set_header(resp, "Content-Type", "application/xml");
	http_response_set_body(resp, out, sizeof(out)-1);

	http_response_write(resp, c->fd);
	http_client_reset(c);
}

/* Simple error response */
void
http_send_error(struct http_client *c, short code, const char *msg) {

	struct http_response *resp = http_response_init(NULL, code, msg);
	resp->http_version = c->http_version;
	http_response_set_connection_header(c, resp);
	http_response_set_body(resp, NULL, 0);

	http_response_write(resp, c->fd);
	http_client_reset(c);
}

/**
 * Set Connection field, either Keep-Alive or Close.
 */
void
http_response_set_keep_alive(struct http_response *r, int enabled) {
	r->keep_alive = enabled;
	if(enabled) {
		http_response_set_header(r, "Connection", "Keep-Alive");
	} else {
		http_response_set_header(r, "Connection", "Close");
	}
}

/* Response to HTTP OPTIONS */
void
http_send_options(struct http_client *c) {

	struct http_response *resp = http_response_init(NULL, 200, "OK");
	resp->http_version = c->http_version;
	http_response_set_connection_header(c, resp);

	http_response_set_header(resp, "Content-Type", "text/html");
	http_response_set_header(resp, "Content-Length", "0");

	http_response_write(resp, c->fd);
	http_client_reset(c);
}

/**
 * Write HTTP chunk.
 */
void
http_response_write_chunk(int fd, struct worker *w, const char *p, size_t sz) {

	struct http_response *r = http_response_init(w, 0, NULL);
	r->keep_alive = 1; /* chunks are always keep-alive */

	/* format packet */
	r->out = format_chunk(p, sz, &r->out_sz);

	/* send async write */
	http_schedule_write(fd, r);
}

