#ifndef CMD_H
#define CMD_H

#include <stdlib.h>
#include <hiredis/async.h>
#include <sys/queue.h>
#include <event.h>
#include <evhttp.h>

struct evhttp_request;
struct server;
struct cmd;

typedef void (*formatting_fun)(redisAsyncContext *, void *, void *);
typedef void (*transform_fun)(struct cmd *);

struct cmd {

	int count;
	const char **argv;
	size_t *argv_len;
	struct evhttp_request *rq;

	struct evkeyvalq uri_params;

	int started_responding;

	char *mime;
	char *mimeKey;
};

struct pubsub_client {
	struct server *s;
	struct evhttp_request *rq;
};

struct cmd *
cmd_new(struct evhttp_request *rq, int count);

void
cmd_free(struct cmd *c);

int
cmd_run(struct server *s, struct evhttp_request *rq,
		const char *uri, size_t uri_len);

void
get_functions(struct cmd *cmd, formatting_fun *f_format, transform_fun *f_transform);

int
cmd_is_subscribe(struct cmd *cmd);

#endif
