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
	int ret;

	if(!s->ac) { /* redis is unavailable */
		evhttp_send_reply(rq, 503, "Service Unavailable", NULL);
		return;
	}

	/* check that the command can be executed */

	switch(rq->type) {
		case EVHTTP_REQ_GET:
			ret = cmd_run(s, rq, 1+uri, strlen(uri)-1);
			break;

		case EVHTTP_REQ_POST:
			ret = cmd_run(s, rq,
				(const char*)EVBUFFER_DATA(rq->input_buffer),
				EVBUFFER_LENGTH(rq->input_buffer));
			break;

		default:
			evhttp_send_reply(rq, 405, "Method Not Allowed", NULL);
			return;
	}

	if(ret < 0) {
		evhttp_send_reply(rq, 403, "Forbidden", NULL);
	}
}

int
main(int argc, char *argv[]) {

	struct server *s = calloc(1, sizeof(struct server));
	s->base = event_base_new();
	struct evhttp *http = evhttp_new(s->base);

	if(argc > 1) {
		s->cfg = conf_read(argv[1]);
	} else {
		s->cfg = conf_read("turnip.json");
	}

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

