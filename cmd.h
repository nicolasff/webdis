#ifndef CMD_H
#define CMD_H

#include <stdlib.h>
#include <hiredis/async.h>

struct evhttp_request;

struct cmd {

	int count;
	const char **argv;
	size_t *argv_len;
	struct evhttp_request *rq;
};

struct cmd *
cmd_new(struct evhttp_request *rq, int count);

void
cmd_free(struct cmd *c);

void
cmd_run(redisAsyncContext *c, struct evhttp_request *rq, const char *uri, size_t uri_len);

#endif
