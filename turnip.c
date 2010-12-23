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

static struct conf *cfg;
redisAsyncContext *__redis_context = NULL;
struct event_base *__base;

static void reconnect();

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
	printf("disconnected...\n");
	__redis_context = NULL;

	/* TODO: schedule reconnect instead to avoid opening too many sockets */
	reconnect();
}

static void
reconnect() {

	if(__redis_context) {
		redisLibeventCleanup(__redis_context->_adapter_data);
	}

	if(cfg->redis_host[0] == '/') { /* unix socket */
		__redis_context = redisAsyncConnectUnix(cfg->redis_host);
	} else {
		__redis_context = redisAsyncConnect(cfg->redis_host, cfg->redis_port);
	}
	if(__redis_context->err) {
		/* Let *c leak for now... */
		printf("Error: %s\n", __redis_context->errstr);
	}


	redisLibeventAttach(__redis_context, __base);
	redisAsyncSetConnectCallback(__redis_context, connectCallback);
	redisAsyncSetDisconnectCallback(__redis_context, disconnectCallback);
}

void
on_request(struct evhttp_request *rq, void *ctx) {

	const char *uri = evhttp_request_uri(rq);
	(void)ctx;

	if(!__redis_context) { /* redis is unavailable */
		evhttp_send_reply(rq, 503, "Service Unavailable", NULL);
		return;
	}

	switch(rq->type) {
		case EVHTTP_REQ_GET:
			cmd_run(__redis_context, rq, 1+uri, strlen(uri)-1);
			break;

		case EVHTTP_REQ_POST:
			cmd_run(__redis_context, rq,
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

	__base = event_base_new();
	struct evhttp *http = evhttp_new(__base);
	
	cfg = conf_read("turnip.conf");

	/* ignore sigpipe */
#ifdef SIGPIPE
	signal(SIGPIPE, SIG_IGN);
#endif

	/* start http server */
	evhttp_bind_socket(http, cfg->http_host, cfg->http_port);
	evhttp_set_gencb(http, on_request, NULL);

	/* attach hiredis to libevent base */
	reconnect();

	/* loop */
	event_base_dispatch(__base);

	return EXIT_SUCCESS;
}

