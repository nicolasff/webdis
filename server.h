#ifndef DISHY_H
#define DISHY_H

#include <hiredis/async.h>
#include <time.h>
#include <sys/queue.h>
#include <event.h>

struct server {

	struct conf *cfg;
	struct event_base *base;
	redisAsyncContext *ac;

	struct event ev_reconnect;
	struct timeval tv_reconnect;
};

void
dishy_connect(struct server *s);

#endif

