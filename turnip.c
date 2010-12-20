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

#include "conf.h"

static void
cmdCallback(redisAsyncContext *c, void *r, void *privdata) {
	(void)c;
	struct evbuffer *body;
	redisReply *reply = r;
	struct evhttp_request *rq = privdata;

	if (reply == NULL) {
		evhttp_send_reply(rq, 404, "Not Found", NULL);
		return;
	}

	switch(reply->type) {
		case REDIS_REPLY_STRING:
		case REDIS_REPLY_STATUS:

			/* send reply */
			body = evbuffer_new();
			evbuffer_add(body, reply->str, strlen(reply->str));
			evhttp_send_reply(rq, 200, "OK", body);
			evbuffer_free(body);
			break;

		case REDIS_REPLY_NIL:
			evhttp_send_reply(rq, 404, "Not Found", NULL);
			break;

		default:
			evhttp_send_reply(rq, 500, "Unknown redis format", NULL);
	}
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
	printf("disconnected...\n");
}

void
run_async_command(redisAsyncContext *c, struct evhttp_request *rq, const char *uri, size_t uri_len) {

	char *slash = strchr(uri, '/');
	int cmd_len;
	int param_count = 0, cur_param = 1;

	const char **arguments;
	size_t *argument_sizes;

	const char *p;

	/* count arguments */
	for(p = uri; p && p < uri + uri_len; param_count++) {
		p = strchr(p+1, '/');
	}

	arguments = calloc(param_count, sizeof(char*));
	argument_sizes = calloc(param_count, sizeof(size_t));

	if(slash) {
		cmd_len = slash - uri;
	} else {
		cmd_len = uri_len;
	}

	/* there is always a first parameter, it's the command name */
	arguments[0] = uri;
	argument_sizes[0] = cmd_len;

	if(!slash) {
		redisAsyncCommandArgv(c, cmdCallback, rq, 1, arguments, argument_sizes);
		free(arguments);
		free(argument_sizes);
		return;
	}
	p = slash + 1;
	while(p < uri + uri_len) {

		const char *arg = p;
		int arg_len;
		char *next = strchr(arg, '/');
		if(next) { /* found a slash */
			arg_len = next - arg;
			p = next + 1;
		} else { /* last argument */
			arg_len = uri + uri_len - arg;
			p = uri + uri_len;
		}

		/* record argument */
		arguments[cur_param] = arg;
		argument_sizes[cur_param] = arg_len;

		cur_param++;
	}

	redisAsyncCommandArgv(c, cmdCallback, rq, param_count, arguments, argument_sizes);

	free(arguments);
	free(argument_sizes);
}


void
on_request(struct evhttp_request *rq, void *ctx) {

	const char *uri = evhttp_request_uri(rq);

	/* get context */
	redisAsyncContext *c = ctx;

	switch(rq->type) {
		case EVHTTP_REQ_GET:
			run_async_command(c, rq, 1+uri, strlen(uri)-1);
			break;
		case EVHTTP_REQ_POST:
			run_async_command(c, rq,
				(const char*)EVBUFFER_DATA(rq->input_buffer),
				EVBUFFER_LENGTH(rq->input_buffer));
			break;

		default:
			evhttp_send_reply(rq, 500, "Unknown redis format", NULL);
			return;
	}
}

int
main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;

	struct event_base *base = event_base_new();
	struct evhttp *http = evhttp_new(base);
	
	struct conf *cfg = conf_read("turnip.conf");

	redisAsyncContext *c = redisAsyncConnect(cfg->redis_host, cfg->redis_port);
	if(c->err) {
		/* Let *c leak for now... */
		printf("Error: %s\n", c->errstr);
		return 1;
	}

	/* start http server */
	evhttp_bind_socket(http, cfg->http_host, cfg->http_port);
	evhttp_set_gencb(http, on_request, c);

	/* attach hiredis to libevent base */
	redisLibeventAttach(c, base);
	redisAsyncSetConnectCallback(c, connectCallback);
	redisAsyncSetDisconnectCallback(c, disconnectCallback);

	/* loop */
	event_base_dispatch(base);

	return EXIT_SUCCESS;
}

