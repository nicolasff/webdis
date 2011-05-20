#include "pool.h"
#include "worker.h"
#include "conf.h"
#include "server.h"

#include <stdlib.h>
#include <event.h>
#include <hiredis/adapters/libevent.h>

struct pool *
pool_new(struct worker *w, int count) {

	struct pool *p = calloc(1, sizeof(struct pool));

	p->count = count;
	p->ac = calloc(count, sizeof(redisAsyncContext*));

	p->w = w;
	p->cfg = w->s->cfg;

	return p;
}

static void
pool_on_connect(const redisAsyncContext *ac) {
	struct pool *p = ac->data;
	int i = 0;

	if(!p || ac->err) {
		return;
	}
	/* connected to redis! */

	/* add to pool */
	for(i = 0; i < p->count; ++i) {
		if(p->ac[i] == NULL) {
			p->ac[i] = ac;
			return;
		}
	}
}

struct pool_reconnect {
	struct event ev;
	struct pool *p;

	struct timeval tv;
};

static void
pool_can_connect(int fd, short event, void *ptr) {
	struct pool_reconnect *pr = ptr;
	struct pool *p = pr->p;

	(void)fd;
	(void)event;

	free(pr);

	pool_connect(p, 1);
}
static void
pool_schedule_reconnect(struct pool *p) {

	struct pool_reconnect *pr = malloc(sizeof(struct pool_reconnect));
	pr->p = p;

	pr->tv.tv_sec = 0;
	pr->tv.tv_usec = 100*1000; /* 0.1 sec*/

	evtimer_set(&pr->ev, pool_can_connect, pr);
	event_base_set(p->w->base, &pr->ev);
	evtimer_add(&pr->ev, &pr->tv);
}



static void
pool_on_disconnect(const redisAsyncContext *ac, int status) {

	struct pool *p = ac->data;
	int i = 0;
	if (status != REDIS_OK) {
		/* fprintf(stderr, "Error: %s\n", ac->errstr); */
	}

	if(p == NULL) { /* no need to clean anything here. */
		return;
	}

	/* remove from the pool */
	for(i = 0; i < p->count; ++i) {
		if(p->ac[i] == ac) {
			p->ac[i] = NULL;
			break;
		}
	}

	/* schedule reconnect */
	pool_schedule_reconnect(p);
}

/**
 * Create new connection.
 */
redisAsyncContext *
pool_connect(struct pool *p, int attach) {

	struct redisAsyncContext *ac;
	if(p->cfg->redis_host[0] == '/') { /* unix socket */
		ac = redisAsyncConnectUnix(p->cfg->redis_host);
	} else {
		ac = redisAsyncConnect(p->cfg->redis_host, p->cfg->redis_port);
	}

	if(attach) {
		ac->data = p;
	} else {
		ac->data = NULL;
	}

	if(ac->err) {
		/*
		const char err[] = "Connection failed";
		slog(s, WEBDIS_ERROR, err, sizeof(err)-1);
		*/
		/* fprintf(stderr, "Error: %s\n", ac->errstr); */
		redisAsyncFree(ac);
		pool_schedule_reconnect(p);
		return NULL;
	}

	redisLibeventAttach(ac, p->w->base);
	redisAsyncSetConnectCallback(ac, pool_on_connect);
	redisAsyncSetDisconnectCallback(ac, pool_on_disconnect);

	if (p->cfg->redis_auth) { /* authenticate. */
		redisAsyncCommand(ac, NULL, NULL, "AUTH %s", p->cfg->redis_auth);
	}
	if (p->cfg->database) { /* change database. */
		redisAsyncCommand(ac, NULL, NULL, "SELECT %d", p->cfg->database);
	}
	return ac;
}

const redisAsyncContext *
pool_get_context(struct pool *p) {

	int orig = p->cur++;

	do {
		p->cur++;
		p->cur %= p->count;
		if(p->ac[p->cur] != NULL) {
			return p->ac[p->cur];
		}
	} while(p->cur != orig);

	return NULL;

}

