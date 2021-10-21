#include "pool.h"
#include "worker.h"
#include "conf.h"
#include "server.h"

#include <stdlib.h>
#include <string.h>
#include <event.h>
#include <hiredis/adapters/libevent.h>

static void
pool_schedule_reconnect(struct pool* p);

struct pool *
pool_new(struct worker *w, int count) {

	struct pool *p = calloc(1, sizeof(struct pool));

	p->count = count;
	p->ac = calloc(count, sizeof(redisAsyncContext*));

	p->w = w;
	p->cfg = w->s->cfg;

	return p;
}

void
pool_free_context(redisAsyncContext *ac) {

	if (ac)	{
		redisAsyncDisconnect(ac);
	}
}

static void
pool_on_connect(const redisAsyncContext *ac, int status) {
	struct pool *p = ac->data;
	int i = 0;

	if(!p || status == REDIS_ERR || ac->err) {
		if (p) {
			pool_schedule_reconnect(p);
		}
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

	pool_connect(p, p->cfg->database, 1);
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

	if(p == NULL) { /* no need to clean anything here. */
		return;
	}

	if (status != REDIS_OK) {
		char format[] = "Error disconnecting: %s";
		size_t msg_sz = sizeof(format) - 2 + ((ac && ac->errstr) ? strlen(ac->errstr) : 6);
		char *log_msg = calloc(msg_sz + 1, 1);
		if(log_msg) {
			snprintf(log_msg, msg_sz + 1, format, ((ac && ac->errstr) ? ac->errstr : "(null)"));
			slog(p->w->s, WEBDIS_ERROR, log_msg, msg_sz-1);
			free(log_msg);
		}
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

static void
pool_log_auth(struct server *s, log_level level, const char *format, size_t format_len, const char *str) {
	/* -2 for `%s`, 6 for "(null)", +1 for \0 */
	size_t msg_size = format_len - 2 + (str ? strlen(str) : 6) + 1;
	char *msg = calloc(1, msg_size);
	if(msg) {
		snprintf(msg, msg_size, format, str ? str : "(null)");
		slog(s, level, msg, msg_size - 1);
		free(msg);
	}
}

static void
pool_on_auth_complete(redisAsyncContext *c, void *r, void *data) {
	redisReply *reply = r;
	struct pool *p = data;
	const char err_format[] = "Authentication failed: %s";
	const char ok_format[] = "Authentication succeeded: %s";
	struct server *s = p->w->s;
	(void)c;
	if(!reply) {
		return;
	}
	pthread_mutex_lock(&s->auth_log_mutex);
	if(s->auth_logged) {
		pthread_mutex_unlock(&s->auth_log_mutex);
		return;
	}
	if(reply->type == REDIS_REPLY_ERROR) {
		pool_log_auth(s, WEBDIS_ERROR, err_format, sizeof(err_format) - 1, reply->str);
	} else if(reply->type == REDIS_REPLY_STATUS) {
		pool_log_auth(s, WEBDIS_INFO, ok_format, sizeof(ok_format) - 1, reply->str);
	}
	s->auth_logged++;
	pthread_mutex_unlock(&s->auth_log_mutex);
}

/**
 * Create new connection.
 */
redisAsyncContext *
pool_connect(struct pool *p, int db_num, int attach) {

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
		char msg[] = "Connection failed: %s";
		size_t errlen = strlen(ac->errstr);
		char *err = malloc(sizeof(msg) + errlen);
		if (err) {
			size_t sz = sprintf(err, msg, ac->errstr);
			slog(p->w->s, WEBDIS_ERROR, err, sz);
			free(err);
		}
		redisAsyncFree(ac);
		pool_schedule_reconnect(p);
		return NULL;
	}

#ifdef HAVE_SSL
/* Negotiate SSL/TLS */
	if(p->w->s->cfg->ssl.enabled) {
		if(redisInitiateSSLWithContext((redisContext*)&ac->c, p->w->s->ssl_context) != REDIS_OK) {
			/* Handle error, in c->err / c->errstr */
			slog(p->w->s, WEBDIS_ERROR, "SSL negotiation failed", 0);
			if(ac->c.err) { /* non-zero on error */
				slog(p->w->s, WEBDIS_ERROR, ac->c.errstr, 0);
			}
			pool_schedule_reconnect(p);
			return NULL;
		}
	}
#endif

	redisLibeventAttach(ac, p->w->base);
	redisAsyncSetConnectCallback(ac, pool_on_connect);
	redisAsyncSetDisconnectCallback(ac, pool_on_disconnect);

	if(p->cfg->redis_auth) { /* authenticate. */
		if(p->cfg->redis_auth->use_legacy_auth) {
			redisAsyncCommand(ac, pool_on_auth_complete, p, "AUTH %s",
				p->cfg->redis_auth->password);
		} else {
			redisAsyncCommand(ac, pool_on_auth_complete, p, "AUTH %s %s",
				p->cfg->redis_auth->username,
				p->cfg->redis_auth->password);
		}
	}
	if(db_num) { /* change database. */
		redisAsyncCommand(ac, NULL, NULL, "SELECT %d", db_num);
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

