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

void
on_request(struct evhttp_request *rq, void *ctx) {

	struct evbuffer *body;
	struct evkeyvalq headers;
	const char *uri = evhttp_request_uri(rq);
	evhttp_parse_query(uri, &headers);


	/* send reply */
	body = evbuffer_new();
	evbuffer_add(body, "hello world\n", 12);
	evhttp_send_reply(rq, 200, "OK", body);
	evbuffer_free(body);
}

int
main(int argc, char *argv[]) {

	struct event_base *base = event_base_new();
	struct evhttp *http = evhttp_new(base);
	
	redisAsyncContext *c = redisAsyncConnect("127.0.0.1", 6379);
	if(c->err) {
		/* Let *c leak for now... */
		printf("Error: %s\n", c->errstr);
		return 1;
	}


	/* start http server */
	evhttp_bind_socket(http, "0.0.0.0", 7379);
	evhttp_set_gencb(http, on_request, NULL);

	/* attach hiredis to libevent base */
	redisLibeventAttach(c, base);
	event_base_dispatch(base);

	return EXIT_SUCCESS;
}

