#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stdlib.h>
#include <stdint.h>

struct http_client;
struct cmd;

enum ws_read_action {
	WS_READ_FAIL,
	WS_READ_MORE,
	WS_READ_EXEC};

struct ws_msg {
	char *data;
	size_t sz;
};

int
ws_handshake_reply(struct http_client *c);

enum ws_read_action
ws_add_data(struct http_client *c);

int
ws_reply(struct cmd *cmd, const char *p, size_t sz);

#endif
