#include "common.h"
#include "cmd.h"

#include <evhttp.h>

void
format_send_reply(struct cmd *cmd, const char *p, size_t sz, const char *content_type) {

	struct evbuffer *body;
	int free_cmd = 1;

	/* send reply */
	body = evbuffer_new();
	evbuffer_add(body, p, sz);
	evhttp_add_header(cmd->rq->output_headers, "Content-Type", content_type);

	if(cmd_is_subscribe(cmd)) {
		free_cmd = 0;

		/* start streaming */
		if(cmd->started_responding == 0) {
			cmd->started_responding = 1;
			evhttp_send_reply_start(cmd->rq, 200, "OK");
		}
		evhttp_send_reply_chunk(cmd->rq, body);

	} else {
		evhttp_send_reply(cmd->rq, 200, "OK", body);
	}
	/* cleanup */
	evbuffer_free(body);
	if(free_cmd) {
		evhttp_clear_headers(&cmd->uri_params);
		cmd_free(cmd);
	}
}

