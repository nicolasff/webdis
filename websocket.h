#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stdlib.h>
#include <stdint.h>

struct http_client;
struct cmd;

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
