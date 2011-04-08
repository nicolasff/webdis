#include "common.h"
#include "cmd.h"
#include "http.h"
#include "client.h"
#include "websocket.h"

#include "md5/md5.h"
#include <string.h>
#include <unistd.h>

/* TODO: replace this with a faster hash function? */
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
	struct http_response resp;

	if(cmd->is_websocket) {
		ws_reply(cmd, p, sz);
		cmd_free(cmd);
		return;
	}

	if(cmd_is_subscribe(cmd)) {
		free_cmd = 0;

		/* start streaming */
		if(cmd->started_responding == 0) {
			const char *ct = cmd->mime?cmd->mime:content_type;
			cmd->started_responding = 1;
			http_response_init(&resp, 200, "OK");
			resp.http_version = cmd->http_version;
			http_response_set_header(&resp, "Content-Type", ct);
			http_response_set_header(&resp, "Connection", "Keep-Alive");
			http_response_set_header(&resp, "Transfer-Encoding", "Chunked");
			http_response_write(&resp, cmd->fd);
		}
		http_response_write_chunk(cmd->fd, p, sz);

	} else {
		/* compute ETag */
		char *etag = etag_new(p, sz);

		/* check If-None-Match */
		if(cmd->if_none_match && strcmp(cmd->if_none_match, etag) == 0) {
			/* SAME! send 304. */
			http_response_init(&resp, 304, "Not Modified");
		} else {
			const char *ct = cmd->mime?cmd->mime:content_type;
			http_response_init(&resp, 200, "OK");
			http_response_set_header(&resp, "Content-Type", ct);
			http_response_set_header(&resp, "ETag", etag);
			http_response_set_body(&resp, p, sz);
		}
		resp.http_version = cmd->http_version;
		if(cmd->keep_alive) {
			http_response_set_header(&resp, "Connection", "Keep-Alive");
		} else {
			http_response_set_header(&resp, "Connection", "Close");
		}
		http_response_write(&resp, cmd->fd);
		if(!cmd->keep_alive) {
			close(cmd->fd);
		}
		free(etag);
	}
	
	/* cleanup */
	if(free_cmd) {
		cmd_free(cmd);
	}
}

