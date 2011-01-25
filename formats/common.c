#include "common.h"
#include "cmd.h"
#include "http.h"
#include "client.h"

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
format_send_reply(struct http_client *client, const char *p, size_t sz, const char *content_type) {

	int free_cmd = 1;

	struct cmd *cmd = client->cmd;

	if(cmd_is_subscribe(cmd)) {
		free_cmd = 0;

		/* start streaming */
		if(cmd->started_responding == 0) {
			const char *ct = cmd->mime?cmd->mime:content_type;
			cmd->started_responding = 1;
			http_set_header(&client->output_headers.content_type, ct, strlen(ct));
			http_send_reply_start(client, 200, "OK");
		}
		http_send_reply_chunk(client, p, sz);

	} else {
		/* compute ETag */
		char *etag = etag_new(p, sz);
		const char *if_none_match = client->input_headers.if_none_match.s;

		/* check If-None-Match */
		if(if_none_match && strncmp(if_none_match, etag, client->input_headers.if_none_match.sz) == 0) {
			/* SAME! send 304. */
			http_send_reply(client, 304, "Not Modified", NULL, 0);
		} else {
			const char *ct = cmd->mime?cmd->mime:content_type;
			http_set_header(&client->output_headers.content_type, ct, strlen(ct));
			http_set_header(&client->output_headers.etag, etag, strlen(etag));
			http_send_reply(client, 200, "OK", p, sz);
		}

		free(etag);
	}
	/* cleanup */
	if(free_cmd) {
		cmd_free(client->cmd);
	}
}

