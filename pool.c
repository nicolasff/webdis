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
	/* printf("Connected to redis\n"); */

	/* add to pool */
	for(i = 0; i < p->count; ++i) {
		if(p->ac[i] == NULL) {
			p->ac[i] = ac;
			return;
		}
	}
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

	/* reconnect */
	/* FIXME: schedule reconnect */
	pool_connect(p, 1);
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

