#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stdlib.h>
#include <stdint.h>

struct http_client;
struct cmd;

#define ntohl64(p) ((p)[0] +\
			(((int64_t)((p)[1])) << 8) 	+\
			(((int64_t)((p)[2])) << 16) +\
			(((int64_t)((p)[3])) << 24) +\
			(((int64_t)((p)[4])) << 32) +\
			(((int64_t)((p)[5])) << 40) +\
			(((int64_t)((p)[6])) << 48) +\
			(((int64_t)((p)[7])) << 56))

enum ws_state {
	WS_ERROR,
	WS_READING,
	WS_MSG_COMPLETE};

struct ws_msg {
	char *payload;
	size_t payload_sz;
	size_t total_sz;
};

int
ws_handshake_reply(struct http_client *c);

enum ws_state
ws_add_data(struct http_client *c);

int
ws_reply(struct cmd *cmd, const char *p, size_t sz);

#endif
