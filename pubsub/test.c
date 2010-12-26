#include <event.h>
#include <stdlib.h>
#include <stdio.h>

#include <hiredis/hiredis.h>
#include <hiredis/adapters/libevent.h>
#include <hiredis/async.h>

extern int __redisPushCallback(redisCallbackList *list, redisCallback *source);

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
	printf("disconnected.\n");
}

static void
onCmd(redisAsyncContext *ac, void *r, void *privdata) {

	(void)ac;
	(void)r;
	(void)privdata;
	redisCallback *cb;

	printf("got something from Redis, reinstalling callback.\n");
	cb = calloc(1, sizeof(redisCallback));
	cb->fn = onCmd;

	printf("__redisPushCallback: %d\n", __redisPushCallback(&ac->replies, cb));
}

int
main() {

	struct event_base *base = event_base_new();
	struct redisAsyncContext *ac = redisAsyncConnect("127.0.0.1", 6379);

	redisLibeventAttach(ac, base);
	redisAsyncSetConnectCallback(ac, connectCallback);
	redisAsyncSetDisconnectCallback(ac, disconnectCallback);

	redisAsyncCommand(ac, onCmd, NULL, "SUBSCRIBE %s", "x");

	event_base_dispatch(base);

	return EXIT_SUCCESS;
}

