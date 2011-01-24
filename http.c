#include "http.h"
#include "server.h"
#include "slog.h"
#include "cmd.h"
#include "client.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* Adobe flash cross-domain request */
static int
http_crossdomain(struct http_client *c) {

	struct http_response resp;
	char out[] = "<?xml version=\"1.0\"?>\n"
"<!DOCTYPE cross-domain-policy SYSTEM \"http://www.macromedia.com/xml/dtds/cross-domain-policy.dtd\">\n"
"<cross-domain-policy>\n"
  "<allow-access-from domain=\"*\" />\n"
"</cross-domain-policy>\n";

	http_response_init(c, &resp, 200, "OK");
	http_response_set_header(&resp, "Content-Type", "application/xml");
	http_response_set_body(&resp, out, sizeof(out)-1);

	http_response_write(&resp, c->fd);
	http_client_reset(c);

	return 0;
}

/* reply to OPTIONS HTTP verb */
static int
http_options(struct http_client *c) {

	struct http_response resp;

	http_response_init(c, &resp, 200, "OK");

	http_response_set_header(&resp, "Content-Type", "text/html");
	http_response_set_header(&resp, "Allow", "GET,POST,OPTIONS");
	http_response_set_header(&resp, "Content-Length", "0");

	/* Cross-Origin Resource Sharing, CORS. */
	http_response_set_header(&resp, "Access-Control-Allow-Origin", "*");

	http_response_write(&resp, c->fd);
	http_client_reset(c);
	
	return 0;
}

int
http_on_complete(http_parser *p) {
	struct http_client *c = p->data;
	int ret = -1;

	c->executing = 1;
	/* check that the command can be executed */
	switch(c->verb) {
		case HTTP_GET:
			if(c->path.sz == 16 && memcmp(c->path.s, "/crossdomain.xml", 16) == 0) {
				return http_crossdomain(c);
			}
			/* slog(s, WEBDIS_DEBUG, uri); */
			ret = cmd_run(c->s, c, 1+c->path.s, c->path.sz-1, NULL, 0);
			break;

		case HTTP_POST:
			/*slog(s, WEBDIS_DEBUG, uri);*/
			ret = cmd_run(c->s, c, 1+c->body.s, c->body.sz-1, NULL, 0);
			break;

		case HTTP_PUT:
			/* slog(s, WEBDIS_DEBUG, uri); */
			ret = cmd_run(c->s, c, 1+c->path.s, c->path.sz-1,
					c->body.s, c->body.sz);
			break;

		case HTTP_OPTIONS:
			return http_options(c);

		default:
			slog(c->s, WEBDIS_DEBUG, "405");
			http_send_error(c, 405, "Method Not Allowed");
			return 0;
	}

	if(ret < 0) {
		http_send_error(c, 403, "Forbidden");
	}

	return ret;
}

/* HTTP Replies */

void
http_send_error(struct http_client *c, short code, const char *msg) {

	http_send_reply(c, code, msg, NULL, 0);
}

void
http_send_reply(struct http_client *c, short code, const char *msg,
		const char *body, size_t body_len) {

	struct http_response resp;
	const char *ct = c->output_headers.content_type.s;
	if(!ct) {
		ct = "text/html";
	}

	/* respond */
	http_response_init(c, &resp, code, msg);

	if(body_len) {
		http_response_set_header(&resp, "Content-Type", ct);
	}

	if(c->sub) {
		http_response_set_header(&resp, "Transfer-Encoding", "chunked");
	}

	if(code == 200 && c->output_headers.etag.s) {
		http_response_set_header(&resp, "ETag", c->output_headers.etag.s);
	}

	http_response_set_body(&resp, body, body_len);

	/* flush response in the socket */
	if(http_response_write(&resp, c->fd)) { /* failure */
		http_client_free(c);
	} else {
		if(c->sub) { /* don't free the client, but monitor fd. */
			http_client_serve(c);
			return;
		} else if(code == 200 && http_client_keep_alive(c)) { /* reset client */
			http_client_reset(c);
			http_client_serve(c);
		} else {
			http_client_free(c); /* error or HTTP < 1.1: close */
		}
	}
}

/* Transfer-encoding: chunked */
void
http_send_reply_start(struct http_client *c, short code, const char *msg) {

	http_send_reply(c, code, msg, NULL, 0);
}

void
http_send_reply_chunk(struct http_client *c, const char *p, size_t sz) {

	char buf[64];
	int ret;

	ret = sprintf(buf, "%x\r\n", (int)sz);
	write(c->fd, buf, ret);
	write(c->fd, p, sz);
	write(c->fd, "\r\n", 2);
}

/* send nil chunk to mark the end of a stream. */
void
http_send_reply_end(struct http_client *c) {

	http_send_reply_chunk(c, "", 0);
	http_client_free(c);
}

void
http_set_header(str_t *h, const char *p, size_t sz) {

	h->s = calloc(1, 1+sz);
	memcpy(h->s, p, sz);
	h->sz = sz;
}

/**
 * Called when a header name is read
 */
int
http_on_header_name(http_parser *p, const char *at, size_t length) {

	struct http_client *c = p->data;

	c->last_header_name.s = calloc(length+1, 1);
	memcpy(c->last_header_name.s, at, length);
	c->last_header_name.sz = length;

	return 0;
}

/**
 * Called when a header value is read
 */
int
http_on_header_value(http_parser *p, const char *at, size_t length) {

	struct http_client *c = p->data;

	if(strncmp("Connection", c->last_header_name.s, c->last_header_name.sz) == 0) {
		http_set_header(&c->input_headers.connection, at, length);
	} else if(strncmp("If-None-Match", c->last_header_name.s, c->last_header_name.sz) == 0) {
		http_set_header(&c->input_headers.if_none_match, at, length);
	} else if(strncmp("Authorization", c->last_header_name.s, c->last_header_name.sz) == 0) {
		http_set_header(&c->input_headers.authorization, at, length);
	} else if(strncmp("Expect", c->last_header_name.s, c->last_header_name.sz) == 0) {
		if(length == 12 && memcmp(at, "100-continue", length) == 0) {
			/* support HTTP file upload */
			char http100[] = "HTTP/1.1 100 Continue\r\n\r\n";
			write(c->fd, http100, sizeof(http100)-1);
		}
	}

	free(c->last_header_name.s);
	c->last_header_name.s = NULL;
	return 0;
}


/* HTTP Response */
void
http_response_init(struct http_client *c, struct http_response *r, int code, const char *msg) {

	memset(r, 0, sizeof(struct http_response));
	r->code = code;
	r->msg = msg;

	http_response_set_header(r, "Server", "Webdis");

	if(!http_client_keep_alive(c)) {
		http_response_set_header(r, "Connection", "Close");
	} else if(code == 200) {
		http_response_set_header(r, "Connection", "Keep-Alive");
	}
}

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

