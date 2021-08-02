#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stdlib.h>
#include <stdint.h>
#include <event.h>
#include <hiredis/async.h>

struct http_client;
struct cmd;

enum ws_state {
	WS_ERROR,
	WS_READING,
	WS_MSG_COMPLETE};

enum ws_frame_type {
	WS_TEXT_FRAME = 0,
	WS_BINARY_FRAME = 1,
	WS_CONNECTION_CLOSE = 8,
	WS_PING = 9,
	WS_PONG = 0xA,
	WS_UNKNOWN_FRAME = -1};

struct ws_msg {
	enum ws_frame_type type;
	char *payload;
	size_t payload_sz;
	size_t total_sz;
};

struct ws_client {
	struct http_client *http_client; /* parent */
	int scheduled_read; /* set if we are scheduled to read WS data */
	int scheduled_write; /* set if we are scheduled to send out WS data */
	struct evbuffer *rbuf; /* read buffer for incoming data */
	struct evbuffer *wbuf; /* write buffer for outgoing data */
	redisAsyncContext *ac; /* dedicated connection to redis */
	struct cmd *cmd; /* current command */
	/* indicates that we'll close once we've flushed all
	   buffered data and read what we planned to read */
	int close_after_events;
	int ran_subscribe; /* set if we've run a (p)subscribe command */
};

struct ws_client *
ws_client_new(struct http_client *http_client);

void
ws_client_free(struct ws_client *ws);

int
ws_handshake_reply(struct ws_client *ws);

int
ws_monitor_input(struct ws_client *ws);

enum ws_state
ws_process_read_data(struct ws_client *ws, unsigned int *out_processed);

int
ws_frame_and_send_response(struct ws_client *ws, enum ws_frame_type type, const char *p, size_t sz);

#endif
