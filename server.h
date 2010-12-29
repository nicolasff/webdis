#ifndef SERVER_H
#define SERVER_H

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
webdis_connect(struct server *s);

struct server *
server_copy(const struct server *s);

#endif

