#include "http.h"
#include "server.h"
#include "slog.h"
#include "cmd.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

struct http_client *
http_client_new(int fd, struct server *s) {

	struct http_client *c = calloc(1, sizeof(struct http_client));
	c->fd = fd;
	c->s = s;

	c->settings.on_path = http_on_path;
	c->settings.on_body = http_on_body;
	c->settings.on_message_complete = http_on_complete;
	c->settings.on_header_field = http_on_header_name;
	c->settings.on_header_value = http_on_header_value;

	http_parser_init(&c->parser, HTTP_REQUEST);
	c->parser.data = c;

	return c;
}


void
http_client_read(int fd, short event, void *ctx) {

	struct http_client *c = ctx;
	char buffer[64*1024];
	int ret, nparsed;

	(void)fd;
	(void)event;

	ret = read(c->fd, buffer, sizeof(buffer));
	if(ret <= 0) { /* broken connection, bye */
		http_client_free(c);
		return;
	}

	c->buffer = realloc(c->buffer, c->sz + ret);
	memcpy(c->buffer + c->sz, buffer, ret);
	c->sz += ret;

	/* TODO: http parse. */
	nparsed = http_parser_execute(&c->parser, &c->settings, buffer, ret);
	if(c->parser.upgrade) {
		/* TODO */
	} else if(nparsed != ret) { /* invalid */
		http_client_free(c);
		return;
	}

	http_client_serve(c);
}

void
http_client_free(struct http_client *c) {

	event_del(&c->ev);
	close(c->fd);
	free(c->buffer);
	free((char*)c->out_content_type.s);
	free(c);
}

static int
http_client_keep_alive(struct http_client *c) {

	/* check disconnection */
	int disconnect = 0;

	if(c->parser.http_major == 1 && c->parser.http_minor == 0) {
		disconnect = 1; /* HTTP 1.0: disconnect by default */
	}
	if(c->header_connection.s) {
		if(strncasecmp(c->header_connection.s, "Keep-Alive", 10) == 0) {
			disconnect = 0;
		} else if(strncasecmp(c->header_connection.s, "Close", 5) == 0) {
			disconnect = 1;
		}
	}
	return disconnect ? 0 : 1;
}

void
http_client_reset(struct http_client *c) {

	if(!http_client_keep_alive(c)) {
		http_client_free(c);
		return;
	}


	free(c->path.s);
	memset(&c->path, 0, sizeof(str_t));

	free(c->body.s);
	memset(&c->body, 0, sizeof(str_t));

	free(c->header_connection.s);
	memset(&c->header_connection, 0, sizeof(str_t));

	free(c->header_if_none_match.s);
	memset(&c->header_if_none_match, 0, sizeof(str_t));

	free(c->out_content_type.s);
	memset(&c->out_content_type, 0, sizeof(str_t));
	memset(&c->out_etag, 0, sizeof(str_t));

	free(c->buffer);
	c->buffer = NULL;
	c->sz = 0;

	memset(&c->verb, 0, sizeof(c->verb));

	http_parser_init(&c->parser, HTTP_REQUEST);
}

/**
 * Add read event callback
 */
void
http_client_serve(struct http_client *c) {

	event_set(&c->ev, c->fd, EV_READ, http_client_read, c);
	event_base_set(c->s->base, &c->ev);
	event_add(&c->ev, NULL);
}

int
http_on_path(http_parser *p, const char *at, size_t length) {

	struct http_client *c = p->data;

	c->path.s = calloc(length+1, 1);
	memcpy(c->path.s, at, length);
	c->path.sz = length;

	/* save HTTP verb as well */
	c->verb = (enum http_method)p->method;

	return 0;
}

int
http_on_body(http_parser *p, const char *at, size_t length) {
	struct http_client *c = p->data;

	c->body.s = calloc(length+1, 1);
	memcpy(c->body.s, at, length);
	c->body.sz = length;

	return 0;
}

/* Adobe flash cross-domain request */
static int
http_crossdomain(struct http_client *c) {

	struct http_response resp;
	char content_length[10];
	char out[] = "<?xml version=\"1.0\"?>\n"
"<!DOCTYPE cross-domain-policy SYSTEM \"http://www.macromedia.com/xml/dtds/cross-domain-policy.dtd\">\n"
"<cross-domain-policy>\n"
  "<allow-access-from domain=\"*\" />\n"
"</cross-domain-policy>\n";

	http_response_init(&resp, 200, "OK");

	http_response_set_header(&resp, "Content-Type", "application/xml");
	sprintf(content_length, "%zd", sizeof(content_length));
	http_response_set_header(&resp, "Content-Length", content_length);
	http_response_set_body(&resp, out, sizeof(out)-1);

	http_response_send(&resp, c->fd);
	http_client_reset(c);

	return 0;
}


/* reply to OPTIONS HTTP verb */
static int
http_options(struct http_client *c) {

	struct http_response resp;

	http_response_init(&resp, 200, "OK");

	http_response_set_header(&resp, "Content-Type", "text/html");
	http_response_set_header(&resp, "Allow", "GET,POST,OPTIONS");
	http_response_set_header(&resp, "Content-Length", "0");

	/* Cross-Origin Resource Sharing, CORS. */
	http_response_set_header(&resp, "Access-Control-Allow-Origin", "*");

	http_response_send(&resp, c->fd);
	http_client_reset(c);
	
	return 0;
}

int
http_on_complete(http_parser *p) {
	struct http_client *c = p->data;
	int ret = -1;

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
			/* evhttp_send_reply(rq, 405, "Method Not Allowed", NULL); */
			return 0;
	}
	return ret;
}

void
http_send_reply(struct http_client *c, short code, const char *msg,
		const char *body, size_t body_len) {

	struct http_response resp;
	char content_length[10];
	const char *ct = c->out_content_type.s;
	if(!ct) {
		ct = "text/html";
	}

	/* respond */
	http_response_init(&resp, code, msg);

	if(!http_client_keep_alive(c)) {
		http_response_set_header(&resp, "Connection", "Close");
	} else if(code == 200) {
		http_response_set_header(&resp, "Connection", "Keep-Alive");
	}

	sprintf(content_length, "%zd", body_len);
	http_response_set_header(&resp, "Content-Length", content_length);
	if(body_len) {
		http_response_set_header(&resp, "Content-Type", ct);
	}

	if(code == 200 && c->out_etag.s) {
		http_response_set_header(&resp, "ETag", c->out_etag.s);
	}

	http_response_set_body(&resp, body, body_len);

	if(http_response_send(&resp, c->fd)) {
		http_client_free(c);
	} else {
		if(code == 200){
			http_client_reset(c);
		} else {
			http_client_free(c);
		}
	}
}

void
http_set_header(str_t *h, const char *p) {

	size_t sz = strlen(p);
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
		c->header_connection.s = calloc(length+1, 1);
		memcpy(c->header_connection.s, at, length);
		c->header_connection.sz = length;
	} else if(strncmp("If-None-Match", c->last_header_name.s, c->last_header_name.sz) == 0) {
		c->header_if_none_match.s = calloc(length+1, 1);
		memcpy(c->header_if_none_match.s, at, length);
		c->header_if_none_match.sz = length;
	}

	free(c->last_header_name.s);
	c->last_header_name.s = NULL;
	return 0;
}

/* HTTP Response */
void
http_response_init(struct http_response *r, int code, const char *msg) {

	memset(r, 0, sizeof(struct http_response));
	r->code = code;
	r->msg = msg;

	http_response_set_header(r, "Server", "Webdis");
}

void
http_response_set_header(struct http_response *r, const char *k, const char *v) {

	size_t sz;
	char *s;

	sz = strlen(k) + 2 + strlen(v) + 2;
	s = calloc(sz + 1, 1);
	sprintf(s, "%s: %s\r\n", k, v);

	r->headers = realloc(r->headers, sizeof(str_t)*(r->header_count + 1));
	r->headers[r->header_count].s = s;
	r->headers[r->header_count].sz = sz;

	r->header_count++;
}

void
http_response_set_body(struct http_response *r, const char *body, size_t body_len) {

	r->body = body;
	r->body_len = body_len;
}

int
http_response_send(struct http_response *r, int fd) {

	char *s = NULL, *p;
	size_t sz = 0;
	int i, ret;

	sz = sizeof("HTTP/1.x xxx ")-1 + strlen(r->msg) + 2;
	s = calloc(sz + 1, 1);

	ret = sprintf(s, "HTTP/1.1 %d %s\r\n", r->code, r->msg);
	p = s; // + ret - 3;

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
	/*
	printf("sz=%zd, s=["); fflush(stdout);
	write(1, s, sz);
	printf("]\n");
	*/
	
	ret = write(fd, s, sz);
	free(s);

	return ret == (int)sz ? 0 : 1;
}

