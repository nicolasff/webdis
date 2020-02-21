#ifndef WORKER_H
#define WORKER_H

#include <pthread.h>

struct http_client;
struct pool;

struct worker {

	/* self */
	pthread_t thread;
	struct event_base *base;

	/* connection dispatcher */
	struct server *s;
	int link[2];

	/* Redis connection pool */
	struct pool *pool;
};

struct worker *
worker_new(struct server *s);

void
worker_start(struct worker *w);

void
worker_add_client(struct worker *w, struct http_client *c);

void
worker_monitor_input(struct http_client *c);

void
worker_can_read(int fd, short event, void *p);

void
worker_process_client(struct http_client *c);

#endif
