#include <stdlib.h>
#include <stdio.h>
#include <sys/queue.h>
#include <evhttp.h>
#include <event.h>
#include <string.h>
#include <signal.h>

#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libevent.h>
#include <jansson.h>

#include "conf.h"
#include "cmd.h"
#include "turnip.h"

struct turnip *__t = NULL;

static void
connectCallback(const redisAsyncContext *c);

static void
disconnectCallback(const redisAsyncContext *c, int status);


static void
reconnect();

static void
on_timer_reconnect(int fd, short event, void *ctx) {

	(void)fd;
	(void)event;
	(void)ctx;

	if(__t->ac) {
		redisLibeventCleanup(__t->ac->_adapter_data);
	}

	/* TODO: free AC. */

	if(__t->cfg->redis_host[0] == '/') { /* unix socket */
		__t->ac = redisAsyncConnectUnix(__t->cfg->redis_host);
	} else {
		__t->ac = redisAsyncConnect(__t->cfg->redis_host, __t->cfg->redis_port);
	}
	if(__t->ac->err) {
		/* Let *c leak for now... */
		printf("Error: %s\n", __t->ac->errstr);
	}


	redisLibeventAttach(__t->ac, __t->base);
	redisAsyncSetConnectCallback(__t->ac, connectCallback);
	redisAsyncSetDisconnectCallback(__t->ac, disconnectCallback);

}

static void
connectCallback(const redisAsyncContext *c) {
	((void)c);
	printf("connected...\n");
}

static void
disconnectCallback(const redisAsyncContext *c, int status) {
	if (status != REDIS_OK) {
		printf("Error: %s\n", c->errstr);
	}
	printf("disconnected, schedule reconnect.\n");
	__t->ac = NULL;

	reconnect();
}

static void
reconnect() {
	/* schedule reconnect */
	evtimer_set(&__t->ev_reconnect, on_timer_reconnect, NULL);
	event_base_set(__t->base, &__t->ev_reconnect);
	__t->tv_reconnect.tv_sec = 1;
	__t->tv_reconnect.tv_usec = 0;
	evtimer_add(&__t->ev_reconnect, &__t->tv_reconnect);
}

void
on_request(struct evhttp_request *rq, void *ctx) {

	const char *uri = evhttp_request_uri(rq);
	(void)ctx;

	if(!__t->ac) { /* redis is unavailable */
		evhttp_send_reply(rq, 503, "Service Unavailable", NULL);
		return;
	}

	switch(rq->type) {
		case EVHTTP_REQ_GET:
			cmd_run(__t->ac, rq, 1+uri, strlen(uri)-1);
			break;

		case EVHTTP_REQ_POST:
			cmd_run(__t->ac, rq,
				(const char*)EVBUFFER_DATA(rq->input_buffer),
				EVBUFFER_LENGTH(rq->input_buffer));
			break;

		default:
			evhttp_send_reply(rq, 405, "Method Not Allowed", NULL);
			return;
	}
}

int
main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;

	__t = calloc(1, sizeof(struct turnip));
	__t->base = event_base_new();
	struct evhttp *http = evhttp_new(__t->base);
	
	__t->cfg = conf_read("turnip.conf");

	/* ignore sigpipe */
#ifdef SIGPIPE
	signal(SIGPIPE, SIG_IGN);
#endif

	/* start http server */
	evhttp_bind_socket(http, __t->cfg->http_host, __t->cfg->http_port);
	evhttp_set_gencb(http, on_request, NULL);

	/* attach hiredis to libevent base */
	reconnect();

	/* loop */
	event_base_dispatch(__t->base);

	return EXIT_SUCCESS;
}

