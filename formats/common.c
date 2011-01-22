#include "common.h"
#include "cmd.h"
#include "http.h"

#include "md5/md5.h"
#include <string.h>

/* TODO: replace this with a faster hash function */
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

	int free_cmd = 1;


	if(cmd_is_subscribe(cmd)) {
		free_cmd = 0;

		/* start streaming */
		if(cmd->started_responding == 0) {
			const char *ct = cmd->mime?cmd->mime:content_type;
			cmd->started_responding = 1;
			http_set_header(&cmd->client->out_content_type, ct, strlen(ct));
			/*FIXME:
			evhttp_send_reply_start(cmd->rq, 200, "OK");
			*/
		}
		/*FIXME: evhttp_send_reply_chunk(cmd->rq, body); */

	} else {
		/* compute ETag */
		char *etag = etag_new(p, sz);
		const char *if_none_match = cmd->client->header_if_none_match.s;
		/* FIXME */
#if 1
		/* check If-None-Match */
		if(if_none_match && strncmp(if_none_match, etag, cmd->client->header_if_none_match.sz) == 0) {
			/* SAME! send 304. */
			http_send_reply(cmd->client, 304, "Not Modified", NULL, 0);
		} else {
			const char *ct = cmd->mime?cmd->mime:content_type;
			http_set_header(&cmd->client->out_content_type, ct, strlen(ct));
			http_set_header(&cmd->client->out_etag, etag, strlen(etag));
			http_send_reply(cmd->client, 200, "OK", p, sz);
		}
#endif
		free(etag);
	}
	/* cleanup */
	if(free_cmd) {
		/*FIXME: evhttp_clear_headers(&cmd->uri_params); */
		cmd_free(cmd);
	}
}

