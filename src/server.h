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
	struct {
		pid_t self;
		int fd;
		struct timeval fsync_tv;
		struct event *fsync_ev;
	} log;

	/* used to log auth message only once */
	pthread_mutex_t auth_log_mutex;
	int auth_logged;
};

struct server *
server_new(const char *cfg_file);

int
server_start(struct server *s);

#endif

