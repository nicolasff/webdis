#include "md5/md5.h"
#include "websocket.h"
#include "client.h"
#include "cmd.h"
#include "worker.h"
#include "pool.h"

/* message parsers */
#include "formats/json.h"
#include "formats/raw.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/**
 * This code uses the WebSocket specification from May 23, 2010.
 * The latest copy is available at http://www.whatwg.org/specs/web-socket-protocol/
 */
static uint32_t
ws_read_key(const char *s) {
	
	uint32_t ret = 0, spaces = 0;
	const char *p;
	size_t sz;

	if(!s) {
		return 0;
	}

	sz = strlen(s);

	for(p = s; p < s+sz; ++p) {
		if(*p >= '0' && *p <= '9') {
			ret *= 10;
			ret += (*p)  - '0';
		} else if (*p == ' ') {
			spaces++;
		}
	}
	return htonl(ret / spaces);
}

static int
ws_compute_handshake(struct http_client *c, unsigned char *out) {

	char buffer[16];
	md5_state_t ctx;

	// websocket handshake
	uint32_t number_1 = ws_read_key(client_get_header(c, "Sec-WebSocket-Key1"));
	uint32_t number_2 = ws_read_key(client_get_header(c, "Sec-WebSocket-Key2"));

	if(c->body_sz < 8) { /* we need at least 8 bytes */
		return -1;
	}

	/* copy number_1, number_2, and last 8 bytes of the body. */
	memcpy(buffer, &number_1, 4);
	memcpy(buffer + 4, &number_2, 4);
	memcpy(buffer + 8, c->body + c->body_sz - 8, 8);
	
	/* hash that buffer, that creates the handshake signature. */
	md5_init(&ctx);
	md5_append(&ctx, (const md5_byte_t *)buffer, sizeof(buffer));
	md5_finish(&ctx, out);

	return 0;
}

int
ws_handshake_reply(struct http_client *c) {

	int ret;
	unsigned char md5_handshake[16];
	char *buffer = NULL, *p;
	const char *origin = NULL, *host = NULL;
	size_t origin_sz = 0, host_sz = 0, sz;

	char template0[] = "HTTP/1.1 101 Websocket Protocol Handshake\r\n"
		"Upgrade: WebSocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Origin: "; /* %s */
	char template1[] = "\r\n"
		"Sec-WebSocket-Location: ws://"; /* %s%s */
	char template2[] = "\r\n"
		"Origin: http://"; /* %s */
	char template3[] = "\r\n\r\n";

	if((origin = client_get_header(c, "Origin"))) {
		origin_sz = strlen(origin);
	}
	if((host = client_get_header(c, "Host"))) {
		host_sz = strlen(host);
	}

	/* need those headers */
	if(!origin || !origin_sz || !host || !host_sz || !c->path || !c->path_sz) {
		return -1;
	}

	if(ws_compute_handshake(c, &md5_handshake[0]) != 0) {
		/* failed to compute handshake. */
		return -1;
	}

	sz = sizeof(template0)-1 + origin_sz
		+ sizeof(template1)-1 + host_sz + c->path_sz
		+ sizeof(template2)-1 + host_sz
		+ sizeof(template3)-1 + sizeof(md5_handshake);

	p = buffer = malloc(sz);

	/* Concat all */

	/* template0 */
	memcpy(p, template0, sizeof(template0)-1); 
	p += sizeof(template0)-1;
	memcpy(p, origin, origin_sz);
	p += origin_sz;

	/* template1 */
	memcpy(p, template1, sizeof(template1)-1); 
	p += sizeof(template1)-1;
	memcpy(p, host, host_sz);
	p += host_sz;
	memcpy(p, c->path, c->path_sz);
	p += c->path_sz;

	/* template2 */
	memcpy(p, template2, sizeof(template2)-1); 
	p += sizeof(template2)-1;
	memcpy(p, host, host_sz);
	p += host_sz;

	/* template3 */
	memcpy(p, template3, sizeof(template3)-1); 
	p += sizeof(template3)-1;
	memcpy(p, &md5_handshake[0], sizeof(md5_handshake));

	ret = write(c->fd, buffer, sz);
	free(buffer);

	return 0;
}


static int
ws_execute(struct http_client *c, const char *frame, size_t frame_len) {

	struct cmd*(*fun_extract)(struct http_client *, const char *, size_t) = NULL;
	formatting_fun fun_reply = NULL;

	if((c->path_sz == 1 && strncmp(c->path, "/", 1) == 0) ||
	   strncmp(c->path, "/.json", 6) == 0) {
		fun_extract = json_ws_extract;
		fun_reply = json_reply;
	} else if(strncmp(c->path, "/.raw", 5) == 0) {
		fun_extract = raw_ws_extract;
		fun_reply = raw_reply;
	}

	if(fun_extract) {

		/* Parse websocket frame into a cmd object. */
		struct cmd *cmd = fun_extract(c, frame, frame_len);

		if(cmd) {
			/* copy client info into cmd. */
			cmd_setup(cmd, c);
			cmd->is_websocket = 1;

			/* get Redis connection from pool */
			redisAsyncContext *ac = (redisAsyncContext*)pool_get_context(c->w->pool);

			/* send it off */
			cmd_send(ac, fun_reply, cmd);

			return 0;
		}
	}

	return -1;
}

/**
 * Process some data just received on the socket.
 */
enum ws_read_action
ws_add_data(struct http_client *c) {
	const char *frame_start, *frame_end;
	char *tmp;

	while(1) {
		/* look for frame start */
		if(!c->sz || c->buffer[0] != '\x00') {
			/* can't find frame start */
			return WS_READ_FAIL;
		}

		/* look for frame end */
		int ret;
		size_t frame_len;
		frame_start = c->buffer;
		frame_end = memchr(frame_start, '\xff', c->sz);
		if(frame_end == NULL) {
			/* continue reading */
			return WS_READ_MORE;
		}

		/* parse and execute frame. */
		frame_len = frame_end - frame_start - 1;

		ret = ws_execute(c, frame_start + 1, frame_len);
		if(ret != 0) {
			/* can't process frame. */
			return WS_READ_FAIL;
		}

		/* remove frame from buffer */
		c->sz -= (2 + frame_len);
		tmp = malloc(c->sz);
		memcpy(tmp, c->buffer + 2 + frame_len, c->sz);
		free(c->buffer);
		c->buffer = tmp;
	}
}

int
ws_reply(struct cmd *cmd, const char *p, size_t sz) {

	int ret;
	char *buffer = malloc(sz + 2);

	/* create frame by prepending 0x00 and appending 0xff */
	buffer[0] = '\x00';
	memcpy(buffer + 1, p, sz);
	buffer[sz + 1] = '\xff';

	/* send WS frame */
	ret = write(cmd->fd, buffer, sz+2);
	free(buffer);

	if(ret == (int)sz + 2) {
		/* success */
		return 0;
	}

	/* write fail */
	return -1;
}
