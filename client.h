#ifndef CLIENT_H
#define CLIENT_H

#include <event.h>
#include <arpa/inet.h>
#include "http-parser/http_parser.h"
#include "http.h"

struct server;
struct cmd;

typedef enum {
	CLIENT_WAITING,
	CLIENT_EXECUTING,
	CLIENT_BROKEN} client_state;

struct http_client {

	/* socket and server reference */
	int fd;
	in_addr_t addr;
	struct event ev;
	struct server *s;
	client_state state;
	struct cmd *cmd;

	/* http parser */
	http_parser_settings settings;
	http_parser parser;

	/* decoded http */
	enum http_method verb;
	str_t path;
	str_t body;

	/* input headers from client */
	struct {
		str_t connection;
		str_t if_none_match;
		str_t authorization;
	} input_headers;

	/* response headers */
	struct input_headers {
		str_t content_type;
		str_t etag;
	} output_headers;

	/* query string */
	struct {
		str_t type;
		str_t jsonp;
	} query_string;

	/* pub/sub */
	struct subscription *sub;
	int started_responding;

	struct http_response resp;

	/* private, used in HTTP parser */
	str_t last_header_name;
};


struct http_client *
http_client_new(int fd, struct server *s);

void
http_client_serve(struct http_client *c);

void
http_client_free(struct http_client *c);

void
http_client_reset(struct http_client *c);

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

int
http_client_keep_alive(struct http_client *c);

/* responses */

void
http_send_reply(struct http_client *c, short code, const char *msg,
		const char *body, size_t body_len);

/* Transfer-encoding: chunked */
void
http_send_reply_start(struct http_client *c, short code, const char *msg);

void
http_send_reply_chunk(struct http_client *c, const char *p, size_t sz);

void
http_send_reply_end(struct http_client *c);

void
http_send_error(struct http_client *c, short code, const char *msg);

/* convenience functions */
int
http_crossdomain(struct http_client *c);

/* reply to OPTIONS HTTP verb */
int
http_options(struct http_client *c);

#endif
