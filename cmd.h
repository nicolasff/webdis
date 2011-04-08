#ifndef CMD_H
#define CMD_H

#include <stdlib.h>
#include <hiredis/async.h>
#include <sys/queue.h>
#include <event.h>
#include <evhttp.h>

struct evhttp_request;
struct http_client;
struct server;
struct worker;
struct cmd;

typedef void (*formatting_fun)(redisAsyncContext *, void *, void *);

struct cmd {
	int fd;

	int count;
	char **argv;
	size_t *argv_len;

	/* HTTP data */
	char *mime; /* forced output content-type */
	int mime_free;
	char *if_none_match; /* used with ETags */
	char *jsonp; /* jsonp wrapper */
	int keep_alive;

	/* various flags */
	int started_responding;
	int is_websocket;
};

struct subscription {
	struct server *s;
	struct cmd *cmd;
};

struct cmd *
cmd_new(int count);

void
cmd_free(struct cmd *c);

int
cmd_run(struct worker *w, struct http_client *client,
		const char *uri, size_t uri_len,
		const char *body, size_t body_len);

int
cmd_select_format(struct http_client *client, struct cmd *cmd,
		const char *uri, size_t uri_len, formatting_fun *f_format);

int
cmd_is_subscribe(struct cmd *cmd);

void
cmd_send(redisAsyncContext *ac, formatting_fun f_format, struct cmd *cmd);

#endif
