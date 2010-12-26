#include <stdlib.h>
#include <stdio.h>
#include <sys/queue.h>
#include <evhttp.h>
#include <event.h>
#include <string.h>
#include <signal.h>

#include <hiredis/hiredis.h>
#include <hiredis/async.h>

#include "server.h"
#include "conf.h"
#include "cmd.h"


void
on_request(struct evhttp_request *rq, void *ctx) {

	const char *uri = evhttp_request_uri(rq);
	struct server *s = ctx;

	if(!s->ac) { /* redis is unavailable */
		printf("503\n");
		evhttp_send_reply(rq, 503, "Service Unavailable", NULL);
		return;
	}



	switch(rq->type) {
		case EVHTTP_REQ_GET:
			cmd_run(s, rq, 1+uri, strlen(uri)-1);
			break;

		case EVHTTP_REQ_POST:
			cmd_run(s, rq,
				(const char*)EVBUFFER_DATA(rq->input_buffer),
				EVBUFFER_LENGTH(rq->input_buffer));
			break;

		default:
			printf("405\n");
			evhttp_send_reply(rq, 405, "Method Not Allowed", NULL);
			return;
	}
}

int
main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;

	struct server *s = calloc(1, sizeof(struct server));
	s->base = event_base_new();
	struct evhttp *http = evhttp_new(s->base);
	
	s->cfg = conf_read("turnip.conf");

	/* ignore sigpipe */
#ifdef SIGPIPE
	signal(SIGPIPE, SIG_IGN);
#endif

	/* start http server */
	evhttp_bind_socket(http, s->cfg->http_host, s->cfg->http_port);
	evhttp_set_gencb(http, on_request, s);

	/* attach hiredis to libevent base */
	turnip_connect(s);

	/* loop */
	event_base_dispatch(s->base);

	return EXIT_SUCCESS;
}

