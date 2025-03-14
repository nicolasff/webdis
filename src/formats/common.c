#include "common.h"
#include "cmd.h"
#include "http.h"
#include "client.h"
#include "websocket.h"

#include "md5/md5.h"
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>

/* TODO: replace this with a faster hash function? */
char *etag_new(const char *p, size_t sz) {

	md5_byte_t buf[16];
	char *etag = calloc(34 + 1, 1);
	int i;

	if(!etag)
		return NULL;

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
format_send_error(struct cmd *cmd, short code, const char *msg) {

	struct http_response *resp;

	if(!cmd->is_websocket && !cmd->pub_sub_client) {
		resp = http_response_init(cmd->w, code, msg);
		resp->http_version = cmd->http_version;
		http_response_set_keep_alive(resp, cmd->keep_alive);
		http_response_write(resp, cmd->fd);
	} else if(cmd->is_websocket && !cmd->http_client->ws->close_after_events) {
		ws_frame_and_send_response(cmd->http_client->ws, WS_BINARY_FRAME, msg, strlen(msg));
	}

	if (!cmd->is_websocket) { /* don't free or detach persistent cmd */
		if (cmd->pub_sub_client) { /* for pub/sub, remove command from client */
			cmd->pub_sub_client->reused_cmd = NULL;
		} else {
			cmd_free(cmd);
		}
	}
}

void
format_send_reply(struct cmd *cmd, const char *p, size_t sz, const char *content_type) {

	int free_cmd = 1;
	const char *ct = cmd->mime?cmd->mime:content_type;
	const char *ce = cmd->content_encoding;
	struct http_response *resp;

	if(cmd->is_websocket) {

		ws_frame_and_send_response(cmd->http_client->ws, WS_BINARY_FRAME, p, sz);

		/* If it's a subscribe command, there'll be more responses */
		if(!cmd_is_subscribe(cmd))
			cmd_free(cmd);
		return;
	}

	if(cmd_is_subscribe(cmd)) {
		free_cmd = 0;

		/* start streaming */
		if(cmd->started_responding == 0) {
			cmd->started_responding = 1;
			resp = http_response_init(cmd->w, 200, "OK");
			resp->http_version = cmd->http_version;
			if(cmd->filename) {
				http_response_set_header(resp, "Content-Disposition", cmd->filename, HEADER_COPY_VALUE);
			}
			http_response_set_header(resp, "Content-Type", ct, HEADER_COPY_VALUE);
			// FIXME: Do we I this for streaming?
			if(ce) {
				http_response_set_header(resp, "Content-Encoding", ce, HEADER_COPY_VALUE);
			}
			http_response_set_keep_alive(resp, 1);
			http_response_set_header(resp, "Transfer-Encoding", "chunked", HEADER_COPY_NONE);
			http_response_set_body(resp, p, sz);
			http_response_write(resp, cmd->fd);
		} else {
			/* Asynchronous chunk write. */
			http_response_write_chunk(cmd->fd, cmd->w, p, sz);
		}

	} else {
		/* compute ETag */
		char *etag = etag_new(p, sz);

		if(etag) {
			/* check If-None-Match */
			if(cmd->if_none_match && strcmp(cmd->if_none_match, etag) == 0) {
				/* SAME! send 304. */
				resp = http_response_init(cmd->w, 304, "Not Modified");
			} else {
				resp = http_response_init(cmd->w, 200, "OK");
				if(cmd->filename) {
					http_response_set_header(resp, "Content-Disposition", cmd->filename, HEADER_COPY_VALUE);
				}
				http_response_set_header(resp, "Content-Type", ct, HEADER_COPY_VALUE);
				http_response_set_header(resp, "ETag", etag, HEADER_COPY_VALUE);
				if(ce) {
					http_response_set_header(resp, "Content-Encoding", ce, HEADER_COPY_VALUE);
				}
				http_response_set_body(resp, p, sz);
			}
			resp->http_version = cmd->http_version;
			http_response_set_keep_alive(resp, cmd->keep_alive);
			http_response_write(resp, cmd->fd);
			free(etag);
		} else {
			format_send_error(cmd, 503, "Service Unavailable");
		}
	}

	/* cleanup */
	if(free_cmd) {
		cmd_free(cmd);
	}
}

int
integer_length(long long int i) {
	int sz = 0;
	int ci = llabs(i);
	while (ci > 0) {
		ci = (ci/10);
		sz += 1;
	}
	if(i == 0) { /* log 0 doesn't make sense. */
		sz = 1;
	} else if(i < 0) { /* allow for neg sign as well. */
		sz++;
	}
	return sz;
}
