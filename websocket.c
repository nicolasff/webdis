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
#include <endian.h>

/**
 * This code uses the WebSocket specification from RFC 6455.
 * A copy is available at http://www.rfc-editor.org/rfc/rfc6455.txt
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

	char template0[] = "HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Protocol: chat\r\n"
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

static struct ws_msg *
ws_msg_new(const char *p, size_t psz, const unsigned char *mask) {
	
	size_t i;
	struct ws_msg *m = malloc(sizeof(struct ws_msg));
	m->payload = malloc(psz);
	m->payload_sz = psz;

	memcpy(m->payload, p, psz);
	for(i = 0; i < psz && mask; ++i) {
		m->payload[i] = (unsigned char)p[i] ^ mask[i%4];
	}

	return m;
}

static void
ws_msg_free(struct ws_msg *m) {

	free(m->payload);
	free(m);
}

static enum ws_state
ws_parse_data(const char *frame, size_t sz, struct ws_msg **msg) {
	
	int has_mask;
	uint32_t len;
	const char *p;
	unsigned char mask[4];

	/* parse frame and extract contents */
	if(sz < 8) {
		return WS_READING;
	}


	if(frame[0] & 0x80) { /* FIN bit set */
		/* TODO */
	}

	has_mask = frame[1] & 0x80 ? 1:0;

	/* get payload length */
	len = frame[1] & 0x7f;	/* remove leftmost bit */
	if(len <= 125) { /* data starts right after the mask */
		p = frame + 2 + (has_mask ? 4 : 0);
		if(has_mask) memcpy(&mask, frame + 2, sizeof(mask));
	} else if(len == 126) {
		memcpy(&len, frame, sizeof(uint32_t));
		len = ntohl(len & 0x0000ffff);
		p = frame + 4 + (has_mask ? 4 : 0);
		if(has_mask) memcpy(&mask, frame + 4, sizeof(mask));
	} else if(len == 127) {
		memcpy(&len, frame + 4, sizeof(uint32_t));
		len = ntohl(len);
		p = frame + 6 + (has_mask ? 4 : 0);
		if(has_mask) memcpy(&mask, frame + 6, sizeof(mask));
	}

	/* we now have the (possibly masked) data starting in p, and its length.  */
	if(len > sz - (p - frame)) { /* not enough data */
		return WS_READING;
	}

	*msg = ws_msg_new(p, len, has_mask ? mask : NULL);
	(*msg)->total_sz = len + (p - frame);


	return WS_MSG_COMPLETE;
}


/**
 * Process some data just received on the socket.
 */
enum ws_state
ws_add_data(struct http_client *c) {

	enum ws_state state;
	struct ws_msg *frame = NULL;

	state = ws_parse_data(c->buffer, c->sz, &frame);

	if(state == WS_MSG_COMPLETE) {
		int ret = ws_execute(c, frame->payload, frame->payload_sz);
		ws_msg_free(frame);

		/* remove frame from client buffer */
		http_client_remove_data(c, frame->total_sz);

		if(ret != 0) {
			/* can't process frame. */
			return WS_ERROR;
		}
	}
	return state;
}

int
ws_reply(struct cmd *cmd, const char *p, size_t sz) {

	int ret;
	char *frame = malloc(sz + 8); /* create frame by prepending header */
	size_t frame_sz;

	/*
      The length of the "Payload data", in bytes: if 0-125, that is the
      payload length.  If 126, the following 2 bytes interpreted as a
      16-bit unsigned integer are the payload length.  If 127, the
      following 8 bytes interpreted as a 64-bit unsigned integer (the
      most significant bit MUST be 0) are the payload length.
	  */
	frame[0] = '\x81';
	if(sz <= 125) {
		frame[1] = sz;
		memcpy(frame + 2, p, sz);
		frame_sz = sz + 2;
	} else if (sz > 125 && sz <= 65536) {
		uint16_t sz16 = htons(sz);
		frame[1] = 126;
		memcpy(frame + 2, &sz16, 2);
		memcpy(frame + 4, p, sz);
		frame_sz = sz + 4;
	} else if (sz > 65536) {
		uint64_t sz64 = htobe64(sz);
		sz64 = (sz64 << 1) >> 1;	/* clear leftmost bit */
		frame[1] = 127;
		memcpy(frame + 2, &sz64, 8);
		memcpy(frame + 10, p, sz);
		frame_sz = sz + 10;
	}

	/* send WS frame */
	ret = write(cmd->fd, frame, frame_sz);
	free(frame);


	if(ret == (int)frame_sz) { /* success */
		return 0;
	}

	/* write fail */
	return -1;
}
