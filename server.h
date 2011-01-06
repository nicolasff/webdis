#ifndef SERVER_H
#define SERVER_H

#include <hiredis/async.h>
#include <time.h>
#include <sys/queue.h>
#include <event.h>

struct evhttp;

struct server {

	struct conf *cfg;
	struct event_base *base;
	redisAsyncContext *ac;
	struct evhttp *http;

	struct event ev_reconnect;
	struct timeval tv_reconnect;
};

void
webdis_connect(struct server *s);

struct server *
server_new(const char *filename);

struct server *
server_copy(const struct server *s);

void
server_start(struct server *s);

void 
webdis_log(struct server *s, int level, const char *body);

#endif

