#include "common.h"
#include "cmd.h"

#include "md5/md5.h"
#include <evhttp.h>
#include <string.h>

char *etag_new(const char *p, size_t sz) {

	md5_byte_t buf[16];
	char *etag = calloc(34 + 1, 1);
	int i;

	md5_state_t pms;

	md5_init(&pms);
	md5_append(&pms, (const md5_byte_t *)p, (int)sz);
	md5_finish(&pms, buf);

	for(i = 0; i < 16; ++i) {
		sprintf(etag + 1 + 2*i, "%.2x", (unsigned char)buf[i]);
	}
	
	etag[0] = '"';
	etag[33] = '"';

	return etag;
}

void
format_send_reply(struct cmd *cmd, const char *p, size_t sz, const char *content_type) {

	struct evbuffer *body;
	int free_cmd = 1;

	/* send reply */
	body = evbuffer_new();
	evbuffer_add(body, p, sz);


	if(cmd_is_subscribe(cmd)) {
		free_cmd = 0;

		/* start streaming */
		if(cmd->started_responding == 0) {
			cmd->started_responding = 1;
			evhttp_add_header(cmd->rq->output_headers, "Content-Type",
					cmd->mime?cmd->mime:content_type);
			evhttp_send_reply_start(cmd->rq, 200, "OK");
		}
		evhttp_send_reply_chunk(cmd->rq, body);

	} else {
		/* compute ETag */
		char *etag = etag_new(p, sz);
		const char *if_none_match;

		/* check If-None-Match */
		if((if_none_match = evhttp_find_header(cmd->rq->input_headers, "If-None-Match")) 
				&& strcmp(if_none_match, etag) == 0) {

			/* SAME! send 304. */
			evhttp_send_reply(cmd->rq, 304, "Not Modified", NULL);
		} else {
			evhttp_add_header(cmd->rq->output_headers, "Content-Type",
					cmd->mime?cmd->mime:content_type);
			evhttp_add_header(cmd->rq->output_headers, "ETag", etag);
			evhttp_send_reply(cmd->rq, 200, "OK", body);
		}
		free(etag);
	}
	/* cleanup */
	evbuffer_free(body);
	if(free_cmd) {
		evhttp_clear_headers(&cmd->uri_params);
		cmd_free(cmd);
	}
}

