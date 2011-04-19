#ifndef SERVER_H
#define SERVER_H

#include <event.h>
#include <hiredis/async.h>
#include <pthread.h>

struct worker;
struct conf;

struct server {

	int fd;
	struct event ev;
	struct event_base *base;

	struct conf *cfg;

	/* worker threads */
	struct worker **w;
	int next_worker;

	/* log lock */
	pthread_spinlock_t log_lock;
};

struct server *
server_new(const char *cfg_file);

int
server_start(struct server *s);

#endif

