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
struct cmd;

typedef void (*formatting_fun)(redisAsyncContext *, void *, void *);

struct cmd {

	int count;
	const char **argv;
	size_t *argv_len;
	struct http_client *client;

	int started_responding;

	/* HTTP data */
	char *mime;
	int mime_free;

	char *if_none_match;
};

struct pubsub_client {
	struct server *s;
	struct cmd *cmd;
	struct evhttp_request *rq;
};

struct cmd *
cmd_new(struct http_client *client, int count);

void
cmd_free(struct cmd *c);

int
cmd_run(struct server *s, struct http_client *client,
		const char *uri, size_t uri_len,
		const char *body, size_t body_len);

int
cmd_select_format(struct http_client *client, struct cmd *cmd,
		const char *uri, size_t uri_len, formatting_fun *f_format);

int
cmd_is_subscribe(struct cmd *cmd);

#endif
