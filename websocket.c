#include "sha1/sha1.h"
#include "libb64/cencode.h"
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
 * This code uses the WebSocket specification from July, 2011.
 * The latest copy is available at http://datatracker.ietf.org/doc/draft-ietf-hybi-thewebsocketprotocol/?include_text=1
 */

static int
ws_compute_handshake(struct http_client *c, char *out, size_t *out_sz) {

	unsigned char *buffer, sha1_output[20];
	char magic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	SHA1Context ctx;
	base64_encodestate b64_ctx;
	int pos, i;

	// websocket handshake
	const char *key = client_get_header(c, "Sec-WebSocket-Key");
	size_t key_sz = key?strlen(key):0, buffer_sz = key_sz + sizeof(magic) - 1;
	buffer = calloc(buffer_sz, 1);

	// concatenate key and guid in buffer
	memcpy(buffer, key, key_sz);
	memcpy(buffer+key_sz, magic, sizeof(magic)-1);

	// compute sha-1
	SHA1Reset(&ctx);
	SHA1Input(&ctx, buffer, buffer_sz);
	SHA1Result(&ctx);
	for(i = 0; i < 5; ++i) {	// put in correct byte order before memcpy.
		ctx.Message_Digest[i] = ntohl(ctx.Message_Digest[i]);
	}
	memcpy(sha1_output, (unsigned char*)ctx.Message_Digest, 20);

	// encode `sha1_output' in base 64, into `out'.
	base64_init_encodestate(&b64_ctx);
	pos = base64_encode_block((const char*)sha1_output, 20, out, &b64_ctx);
	base64_encode_blockend(out + pos, &b64_ctx);

	// compute length, without \n
	*out_sz = strlen(out);
	if(out[*out_sz-1] == '\n')
		(*out_sz)--;

	free(buffer);

	return 0;
}

int
ws_handshake_reply(struct http_client *c) {

	int ret;
	char sha1_handshake[40];
	char *buffer = NULL, *p;
	const char *origin = NULL, *host = NULL;
	size_t origin_sz = 0, host_sz = 0, handshake_sz = 0, sz;

	char template0[] = "HTTP/1.1 101 Websocket Protocol Handshake\r\n"
		"Upgrade: WebSocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Origin: "; /* %s */
	char template1[] = "\r\n"
		"Sec-WebSocket-Location: ws://"; /* %s%s */
	char template2[] = "\r\n"
		"Origin: http://"; /* %s */
	char template3[] = "\r\n"
		"Sec-WebSocket-Accept: "; /* %s */
	char template4[] = "\r\n\r\n";

	if((origin = client_get_header(c, "Origin"))) {
		origin_sz = strlen(origin);
	} else if((origin = client_get_header(c, "Sec-WebSocket-Origin"))) {
		origin_sz = strlen(origin);
	}
	if((host = client_get_header(c, "Host"))) {
		host_sz = strlen(host);
	}

	/* need those headers */
	if(!origin || !origin_sz || !host || !host_sz || !c->path || !c->path_sz) {
		return -1;
	}

	memset(sha1_handshake, 0, sizeof(sha1_handshake));
	if(ws_compute_handshake(c, &sha1_handshake[0], &handshake_sz) != 0) {
		/* failed to compute handshake. */
		return -1;
	}

	sz = sizeof(template0)-1 + origin_sz
		+ sizeof(template1)-1 + host_sz + c->path_sz
		+ sizeof(template2)-1 + host_sz
		+ sizeof(template3)-1 + handshake_sz
		+ sizeof(template4)-1;

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
	memcpy(p, &sha1_handshake[0], handshake_sz);
	p += handshake_sz;

	/* template4 */
	memcpy(p, template4, sizeof(template4)-1);
	p += sizeof(template4)-1;

	/* send data to client */
	ret = write(c->fd, buffer, sz);
	(void)ret;
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
			cmd->ac = (redisAsyncContext*)pool_get_context(c->w->pool);

			/* send it off */
			cmd_send(cmd, fun_reply);

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
