#include "server.h"
#include "conf.h"
#include "cmd.h"

#include <hiredis/hiredis.h>
#include <hiredis/adapters/libevent.h>
#include <jansson.h>

static void
connectCallback(const redisAsyncContext *c) {
	((void)c);
	printf("connected...\n");
}

static void
disconnectCallback(const redisAsyncContext *c, int status) {
	struct server *s = c->data;
	if (status != REDIS_OK) {
		printf("Error: %s\n", c->errstr);
	}
	printf("disconnected, schedule reconnect.\n");
	s->ac = NULL;

	dishy_connect(s);
}

static void
on_timer_reconnect(int fd, short event, void *ctx) {

	(void)fd;
	(void)event;
	struct server *s = ctx;

	if(s->ac) {
		redisLibeventCleanup(s->ac->_adapter_data);
	}

	/* TODO: free AC. */

	if(s->cfg->redis_host[0] == '/') { /* unix socket */
		s->ac = redisAsyncConnectUnix(s->cfg->redis_host);
	} else {
		s->ac = redisAsyncConnect(s->cfg->redis_host, s->cfg->redis_port);
	}

	s->ac->data = s;

	if(s->ac->err) {
		/* Let *c leak for now... */
		printf("Error: %s\n", s->ac->errstr);
	}

	redisLibeventAttach(s->ac, s->base);
	redisAsyncSetConnectCallback(s->ac, connectCallback);
	redisAsyncSetDisconnectCallback(s->ac, disconnectCallback);

	if (s->cfg->redis_auth) { /* authenticate. */
		redisAsyncCommand(s->ac, NULL, NULL, "AUTH %s", s->cfg->redis_auth);
	}
}

void
dishy_connect(struct server *s) {
	/* schedule reconnect */
	evtimer_set(&s->ev_reconnect, on_timer_reconnect, s);
	event_base_set(s->base, &s->ev_reconnect);
	s->tv_reconnect.tv_sec = 1;
	s->tv_reconnect.tv_usec = 0;
	evtimer_add(&s->ev_reconnect, &s->tv_reconnect);
}

