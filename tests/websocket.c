/* https://datatracker.ietf.org/doc/html/rfc6455 */

#include <stdlib.h>
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <b64/cencode.h>
#include <http-parser/http_parser.h>
#include <sha1/sha1.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <event.h>

struct host_info {
	char *host;
	short port;
};

enum worker_state {
	WS_INITIAL,
	WS_SENT_HANDSHAKE,
	WS_RECEIVED_HANDSHAKE,
	WS_SENT_FRAME,
	WS_COMPLETE
};

enum mask_config {
	MASK_NEVER,
	MASK_ALWAYS,
	MASK_ALTERNATE
};

/* worker_thread, with counter of remaining messages */
struct worker_thread {
	struct host_info *hi;
	struct event_base *base;

	int msg_target;
	int msg_received;
	int msg_sent;
	int byte_count;
	int id;
	pthread_t thread;
	enum worker_state state;
	int timeout_seconds;

	/* non-encoded websocket key */
	char ws_key[16];
	/* expected response */
	char ws_response[28];
	size_t ws_response_len;
	/* actual response */
	char *sec_websocket_accept;
	/* masking */
	enum mask_config mask_cfg;
	int mask_applied;

	/* current header */
	char *cur_hdr_key;
	size_t cur_hdr_key_len; /* not including trailing \0 */
	char *cur_hdr_val;
	size_t cur_hdr_val_len; /* not including trailing \0 */
	int hdr_last_cb_was_name; /* tells us if the last call was header name or value */

	struct evbuffer *rbuffer;
	int got_header;

	struct evbuffer *wbuffer;

	int verbose;
	int fd;
	struct event ev_r;
	struct event ev_w;

	http_parser parser;
	http_parser_settings settings;

	int (*debug)(const char *fmt, ...);
};

int debug_noop(const char *fmt, ...) {
	(void)fmt;
	return 0;
}

int debug_verbose(const char *fmt, ...) {
	int ret;
	va_list vargs;
	va_start(vargs, fmt);
	ret = vfprintf(stderr, fmt, vargs);
	va_end(vargs);
	return ret;
}

void
hex_dump(struct worker_thread *wt, char *p, size_t sz) {
	wt->debug("hex dump of %p (%ld bytes)\n", p, sz);
	for (char *cur = p; cur < p + sz; cur += 16) {
		char letters[16] = {0};
		int limit = (cur + 16) > p + sz ? (sz % 16) : 16;
		wt->debug("%08lx ", cur - p); /* address */
		for (int i = 0; i < limit; i++) {
			wt->debug("%02x ", (unsigned int)(cur[i] & 0xff));
			letters[i] = isprint(cur[i]) ? cur[i] : '.';
		}
		for (int i = limit; i < 16; i++) { /* pad on last line */
			wt->debug("   "); /* 3 spaces for "%02x " */
		}
		wt->debug(" %.*s\n", limit, letters);
	}
}

void
evbuffer_debug_dump(struct worker_thread *wt, struct evbuffer *buffer) {
	size_t sz = evbuffer_get_length(buffer);
	char *data = malloc(sz);
	if (!data) {
		fprintf(stderr, "failed to allocate %ld bytes\n", sz);
		return;
	}
	evbuffer_remove(buffer, data, sz);
	hex_dump(wt, data, sz);
	evbuffer_prepend(buffer, data, sz);
	free(data);
}

static void
wait_for_possible_read(struct worker_thread *wt);

static void
wait_for_possible_write(struct worker_thread *wt);

static void
ws_enqueue_frame(struct worker_thread *wt);

void
process_message(struct worker_thread *wt, size_t sz) {
	if (wt->msg_received && wt->msg_received % 1000 == 0) {
		printf("thread %d: %8d messages left (got %9d bytes so far).\n",
		       wt->id,
		       wt->msg_target - wt->msg_received, wt->byte_count);
	}
	wt->byte_count += sz;

	/* decrement read count, and stop receiving when we reach zero. */
	wt->msg_received++;
	if (wt->msg_received == wt->msg_target) {
		wt->debug("%s: thread %d has received all %d messages it expected\n",
			  __func__, wt->id, wt->msg_received);
		event_base_loopexit(wt->base, NULL);
	}
}

/**
 * Called when we can write to the socket.
 */
void
websocket_can_write(int fd, short event, void *ptr) {
	int ret;
	struct worker_thread *wt = ptr;
	(void) event;
	wt->debug("%s (wt=%p, fd=%d)\n", __func__, wt, fd);

	switch (wt->state) {
	case WS_INITIAL: { /* still sending initial HTTP request */
		ret = evbuffer_write(wt->wbuffer, fd);
		wt->debug("evbuffer_write returned %d\n", ret);
		wt->debug("evbuffer_get_length returned %d\n", evbuffer_get_length(wt->wbuffer));
		if (evbuffer_get_length(wt->wbuffer) != 0) { /* not all written */
			wait_for_possible_write(wt);
			return;
		}
		/* otherwise, we've sent the full request, time to read the response */
		wt->state = WS_SENT_HANDSHAKE;
		wt->debug("state=WS_SENT_HANDSHAKE\n");
		wait_for_possible_read(wt);
		return;
	}
	case WS_RECEIVED_HANDSHAKE: { /* ready to send a frame */
		wt->debug("About to send data for WS frame, %lu in buffer\n", evbuffer_get_length(wt->wbuffer));
		evbuffer_write(wt->wbuffer, fd);
		size_t write_remains = evbuffer_get_length(wt->wbuffer);
		wt->debug("Sent data for WS frame, still %lu left to write\n", write_remains);
		if (write_remains == 0) { /* ready to read response */
			wt->state = WS_SENT_FRAME;
			wt->msg_sent++;
			wait_for_possible_read(wt);
		} else { /* not finished writing */
			wait_for_possible_write(wt);
		}
		return;
	}
	default:
		break;
	}
}

static void
websocket_can_read(int fd, short event, void *ptr) {
	int ret;

	struct worker_thread *wt = ptr;
	(void) event;
	wt->debug("%s (wt=%p)\n", __func__, wt);

	/* read message */
	ret = evbuffer_read(wt->rbuffer, fd, 65536);
	wt->debug("evbuffer_read() returned %d; wt->state=%d. wt->rbuffer:\n", ret, wt->state);
	evbuffer_debug_dump(wt, wt->rbuffer);
	if (ret == 0) {
		wt->debug("We didn't read anything from the socket...\n");
		event_base_loopexit(wt->base, NULL);
		return;
	}

	while (1) {
		switch (wt->state) {
		case WS_SENT_HANDSHAKE: { /* waiting for handshake response */
			size_t avail_sz = evbuffer_get_length(wt->rbuffer);
			char *tmp = calloc(avail_sz, 1);
			wt->debug("avail_sz from rbuffer = %lu\n", avail_sz);
			evbuffer_remove(wt->rbuffer, tmp, avail_sz); /* copy into `tmp` */
			wt->debug("Giving %lu bytes to http-parser\n", avail_sz);
			int nparsed = http_parser_execute(&wt->parser, &wt->settings, tmp, avail_sz);
			wt->debug("http-parser returned %d\n", nparsed);
			free(tmp);
			/* http parser will return the offset at which the upgraded protocol begins,
			   which in our case is 1 under the total response size. */

			if (wt->state == WS_SENT_HANDSHAKE || /* haven't encountered end of response yet */
			    (wt->parser.upgrade && nparsed != (int)avail_sz -1)) {
				wt->debug("UPGRADE *and* we have some data left\n");
				continue;
			} else if (wt->state == WS_RECEIVED_HANDSHAKE) { /* we have the full response */
				evbuffer_drain(wt->rbuffer, evbuffer_get_length(wt->rbuffer));
			}
			return;
		}

		case WS_SENT_FRAME: { /* waiting for frame response */
			wt->debug("We're in WS_SENT_FRAME, just read a frame response. wt->rbuffer:\n");
			evbuffer_debug_dump(wt, wt->rbuffer);
			uint8_t flag_opcodes, payload_len;
			if (evbuffer_get_length(wt->rbuffer) < 2) { /* not enough data */
				wait_for_possible_read(wt);
				return;
			}
			evbuffer_remove(wt->rbuffer, &flag_opcodes, 1);	  /* remove flags & opcode */
			evbuffer_remove(wt->rbuffer, &payload_len, 1);	  /* remove length */
			evbuffer_drain(wt->rbuffer, (size_t)payload_len); /* remove payload itself */
			process_message(wt, payload_len);

			if (evbuffer_get_length(wt->rbuffer) == 0) { /* consumed everything */
				if (wt->msg_received < wt->msg_target) { /* let's write again */
					wt->debug("our turn to write again\n");
					wt->state = WS_RECEIVED_HANDSHAKE;
					ws_enqueue_frame(wt);
				} /* otherwise, we're done */
				return;
			} else {
				wt->debug("there's still data to consume\n");
				continue;
			}
			return;
		}

		default:
			return;
		}
	}
}


static void
wait_for_possible_read(struct worker_thread *wt) {
	wt->debug("%s (wt=%p)\n", __func__, wt);
	event_set(&wt->ev_r, wt->fd, EV_READ, websocket_can_read, wt);
	event_base_set(wt->base, &wt->ev_r);
	event_add(&wt->ev_r, NULL);
}

static void
wait_for_possible_write(struct worker_thread *wt) {
	wt->debug("%s (wt=%p)\n", __func__, wt);
	event_set(&wt->ev_r, wt->fd, EV_WRITE, websocket_can_write, wt);
	event_base_set(wt->base, &wt->ev_r);
	event_add(&wt->ev_r, NULL);
}

static int
ws_on_header_field(http_parser *p, const char *at, size_t length) {
	(void)length;
	struct worker_thread *wt = (struct worker_thread *)p->data;

	if (wt->hdr_last_cb_was_name) { /* we're appending to the name */
		wt->cur_hdr_key = realloc(wt->cur_hdr_key, wt->cur_hdr_key_len + length + 1);
		memcpy(wt->cur_hdr_key + wt->cur_hdr_key_len, at, length);
		wt->cur_hdr_key_len += length;
	} else { /* first call for this header name */
		free(wt->cur_hdr_key); /* free the previous header name if there was one */
		wt->cur_hdr_key_len = length;
		wt->cur_hdr_key = calloc(length + 1, 1);
		memcpy(wt->cur_hdr_key, at, length);
	}
	wt->debug("%s appended header name data: currently [%.*s]\n", __func__,
		  (int)wt->cur_hdr_key_len, wt->cur_hdr_key);
	// wt->cur_header_is_ws_resp = (strncasecmp(at, "Sec-WebSocket-Accept", 20) == 0) ? 1 : 0;

	wt->hdr_last_cb_was_name = 1;
	return 0;
}

static int
ws_on_header_value(http_parser *p, const char *at, size_t length) {
	struct worker_thread *wt = (struct worker_thread *)p->data;

	if (wt->hdr_last_cb_was_name == 0) { /* we're appending to the value */
		wt->cur_hdr_val = realloc(wt->cur_hdr_val, wt->cur_hdr_val_len + length + 1);
		memcpy(wt->cur_hdr_val + wt->cur_hdr_val_len, at, length);
		wt->cur_hdr_val_len += length;
	} else { /* first call for this header value */
		free(wt->cur_hdr_val); /* free the previous header value if there was one */
		wt->cur_hdr_val_len = length;
		wt->cur_hdr_val = calloc(length + 1, 1);
		memcpy(wt->cur_hdr_val, at, length);
	}
	wt->debug("%s appended header value data: currently [%.*s]\n", __func__,
		  (int)wt->cur_hdr_val_len, wt->cur_hdr_val);

	if (wt->cur_hdr_key_len == 20 && strncasecmp(wt->cur_hdr_key, "Sec-WebSocket-Accept", 20) == 0) {
		free(wt->sec_websocket_accept);
		wt->sec_websocket_accept = calloc(wt->cur_hdr_val_len + 1, 1);
		memcpy(wt->sec_websocket_accept, wt->cur_hdr_val, wt->cur_hdr_val_len);
	}

	wt->hdr_last_cb_was_name = 0;
	return 0;
}


static int
ws_on_headers_complete(http_parser *p) {
	struct worker_thread *wt = p->data;
	wt->debug("%s (wt=%p)\n", __func__, wt);
	free(wt->cur_hdr_key);
	free(wt->cur_hdr_val);

	/* make sure that we received a Sec-WebSocket-Accept header */
	if (!wt->sec_websocket_accept) {
		wt->debug("%s: no Sec-WebSocket-Accept header was returned\n", __func__);
		return 1;
	}

	/* and that it matches what we expect */
	int ret = 0;
	if (strlen(wt->sec_websocket_accept) != wt->ws_response_len
		|| memcmp(wt->ws_response, wt->sec_websocket_accept, wt->ws_response_len) != 0) {
		wt->debug("Invalid WS handshake: expected [%.*s], got [%s]\n",
			(int)wt->ws_response_len, wt->ws_response, wt->sec_websocket_accept);
		ret = 1;
	}

	free(wt->sec_websocket_accept);
	return ret;
}

static void
ws_enqueue_frame_for_command(struct worker_thread *wt, char *cmd, size_t sz) {
	int include_mask = (wt->mask_cfg == MASK_ALWAYS ||
		(wt->mask_cfg == MASK_ALTERNATE && wt->msg_sent % 2 == 0)) ? 1 : 0;

	unsigned char mask[4];
	for (int i = 0; include_mask && i < 4; i++) { /* only if mask is needed */
		mask[i] = rand() & 0xff;
	}
	uint8_t len = (uint8_t)(sz); /* (1 << 7) | length. */
	if (include_mask) {
		len |= (1 << 7); /* set masking bit ON */
	} 

	/* apply the mask to the payload */
	for (size_t i = 0; include_mask && i < sz; i++) {
		cmd[i] = (cmd[i] ^ mask[i % 4]) & 0xff;
	}
	/* 0x81 = 10000001b:
		1: FIN bit (meaning there's only one message in the frame),
		0: RSV1 bit (reserved),
		0: RSV2 bit (reserved),
		0: RSV3 bit (reserved),
		0001: text frame */
	evbuffer_add(wt->wbuffer, "\x81", 1);
	evbuffer_add(wt->wbuffer, &len, 1);
	if (include_mask) { /* only include mask in the frame if needed */
		evbuffer_add(wt->wbuffer, mask, 4);
	}
	evbuffer_add(wt->wbuffer, cmd, sz);
	wt->mask_applied += include_mask;
}

static void
ws_enqueue_frame(struct worker_thread *wt) {
	char ping_command[] = "[\"PING\"]";
	ws_enqueue_frame_for_command(wt, ping_command, sizeof(ping_command) - 1);

	wait_for_possible_write(wt);
}

static int
ws_on_message_complete(http_parser *p) {
	struct worker_thread *wt = p->data;

	wt->debug("%s (wt=%p), upgrade=%d\n", __func__, wt, p->upgrade);
	// we've received the full HTTP response now, so we're ready to send frames
	wt->state = WS_RECEIVED_HANDSHAKE;
	ws_enqueue_frame(wt); /* add frame to buffer and register interest in writing */
	return 0;
}

static void
ws_on_timeout(evutil_socket_t fd, short event, void *arg) {
	struct worker_thread *wt = arg;
	(void)fd;
	(void)event;

	fprintf(stderr, "Time has run out! (thread %d)\n", wt->id);
	event_base_loopbreak(wt->base); /* break out of event loop */
}

void*
worker_main(void *ptr) {

	char ws_template[] = "GET /.json HTTP/1.1\r\n"
			     "Host: %s:%d\r\n"
			     "Connection: Upgrade\r\n"
			     "Upgrade: WebSocket\r\n"
			     "Origin: http://%s:%d\r\n"
			     "Sec-WebSocket-Key: %s\r\n"
			     "\r\n";

	struct worker_thread *wt = ptr;

	int ret;
	int fd;
	int int_one = 1;
	struct sockaddr_in addr;
	struct timeval timeout_tv;
	struct event *timeout_ev;

	/* connect socket */
	fd = socket(AF_INET, SOCK_STREAM, 0);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(wt->hi->port);
	memset(&(addr.sin_addr), 0, sizeof(addr.sin_addr));
	addr.sin_addr.s_addr = inet_addr(wt->hi->host);

	ret = connect(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr));
	if (ret != 0) {
		fprintf(stderr, "connect: ret=%d: %s\n", ret, strerror(errno));
		return NULL;
	}
	ret = ioctl(fd, FIONBIO, &int_one);
	if (ret != 0) {
		fprintf(stderr, "ioctl: ret=%d: %s\n", ret, strerror(errno));
		return NULL;
	}

	/* initialize worker thread */
	wt->fd = fd;
	wt->base = event_base_new();
	wt->rbuffer = evbuffer_new();
	wt->wbuffer = evbuffer_new(); /* write buffer */
	wt->byte_count = 0;
	wt->got_header = 0;

	/* generate a random key */
	for (int i = 0; i < 16; i++) {
		wt->ws_key[i] = rand() & 0xff;
	}
	wt->debug("Raw WS key:\n");
	hex_dump(wt, wt->ws_key, 16);

	char encoded_key[23]; /* it shouldn't be more than 4/3 * 16 */
	base64_encodestate b64state;
	base64_init_encodestate(&b64state);
	int pos = base64_encode_block((const char *)wt->ws_key, 16, encoded_key, &b64state);
	int delta = base64_encode_blockend(encoded_key + pos, &b64state);
	/* the block ends with a '\n', which we need to remove */
	encoded_key[pos+delta-1] = '\0';
	wt->debug("Encoded WS key [%s]:\n", encoded_key);

	/* compute the expected response, to be validated when we receive it */
	char magic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	size_t expected_raw_sz = (pos+delta-1) + sizeof(magic)-1;
	char *expected_raw = calloc(expected_raw_sz + 1, 1);
	memcpy(expected_raw, encoded_key, pos+delta-1); /* add encoded key */
	memcpy(expected_raw + pos+delta-1, magic, sizeof(magic)-1); /* then constant guid */

	SHA1Context ctx;
	SHA1Reset(&ctx);
	SHA1Input(&ctx, (const unsigned char*)expected_raw, expected_raw_sz);
	SHA1Result(&ctx);
	for(int i = 0; i < (int)(20/sizeof(int)); ++i) { /* put in correct byte order */
		ctx.Message_Digest[i] = ntohl(ctx.Message_Digest[i]);
	}

	/* and then base64 encode the hash */
	base64_init_encodestate(&b64state);
	int resp_pos = base64_encode_block((const char *)ctx.Message_Digest, 20, wt->ws_response, &b64state);
	int resp_delta = base64_encode_blockend(wt->ws_response + resp_pos, &b64state);
	wt->ws_response_len = resp_pos + resp_delta - 1;
	wt->ws_response[wt->ws_response_len] = '\0'; /* again remove the '\n' */

	wt->debug("Expected response header: [%s]\n", wt->ws_response);

	/* add timeout, if set */
	if (wt->timeout_seconds > 0) {
		timeout_tv.tv_sec = wt->timeout_seconds;
		timeout_tv.tv_usec = 0;
		timeout_ev = event_new(wt->base, -1, EV_TIMEOUT, ws_on_timeout, wt);
		event_add(timeout_ev, &timeout_tv);
	}

	/* initialize HTTP parser, to parse the server response */
	memset(&wt->settings, 0, sizeof(http_parser_settings));
	wt->settings.on_header_field = ws_on_header_field;
	wt->settings.on_header_value = ws_on_header_value;
	wt->settings.on_headers_complete = ws_on_headers_complete;
	wt->settings.on_message_complete = ws_on_message_complete;
	http_parser_init(&wt->parser, HTTP_RESPONSE);
	wt->parser.data = wt;

	/* add GET request to buffer */
	evbuffer_add_printf(wt->wbuffer, ws_template, wt->hi->host, wt->hi->port,
			    wt->hi->host, wt->hi->port, encoded_key);
	wait_for_possible_write(wt); /* request callback */

	/* go! */
	event_base_dispatch(wt->base);
	wt->debug("event_base_dispatch returned\n");
	event_base_free(wt->base);
	return NULL;
}

void usage(const char *argv0, char *host_default, short port_default,
	   int thread_count_default, int messages_default) {

	printf("Usage: %s [options]\n"
	       "Options are:\n"
	       "\t[--host|-h] HOST\t(default = \"%s\")\n"
	       "\t[--port|-p] PORT\t(default = %d)\n"
	       "\t[--clients|-c] THREADS\t(default = %d)\n"
	       "\t[--messages|-n] COUNT\t(number of messages per thread, default = %d)\n"
	       "\t[--mask|-m] MASK_CFG\t(%d: always, %d: never, %d: alternate, default = always)\n"
	       "\t[--max-time|-t] SECONDS\t(max time to give to the run, default = unlimited)\n"
	       "\t[--verbose|-v]\t\t(extremely verbose output)\n",
	       argv0, host_default, (int)port_default,
	       thread_count_default, messages_default,
	       MASK_ALWAYS, MASK_NEVER, MASK_ALTERNATE);
}

int
main(int argc, char *argv[]) {

	struct timespec t0, t1;

	int messages_default = 2500;
	int thread_count_default = 4;
	short port_default = 7379;
	char *host_default = "127.0.0.1";

	int msg_target = messages_default;
	int thread_count = thread_count_default;
	int i, opt;
	char *colon;
	long total = 0, total_bytes = 0;
	int verbose = 0;
	int timeout_seconds = -1;
	enum mask_config mask_cfg = MASK_ALWAYS;

	struct host_info hi = {host_default, port_default};

	struct worker_thread *workers;

	/* getopt */
	struct option long_options[] = {
	    {"help", no_argument, NULL, '?'},
	    {"host", required_argument, NULL, 'h'},
	    {"port", required_argument, NULL, 'p'},
	    {"clients", required_argument, NULL, 'c'},
	    {"messages", required_argument, NULL, 'n'},
	    {"mask", required_argument, NULL, 'm'},
	    {"max-time", required_argument, NULL, 't'},
	    {"verbose", no_argument, NULL, 'v'},
	    {0, 0, 0, 0}};
	while ((opt = getopt_long(argc, argv, "h:p:c:n:m:t:vs", long_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			colon = strchr(optarg, ':');
			if (!colon) {
				size_t sz = strlen(optarg);
				hi.host = calloc(1 + sz, 1);
				strncpy(hi.host, optarg, sz);
			} else {
				hi.host = calloc(1 + colon - optarg, 1);
				strncpy(hi.host, optarg, colon - optarg);
				hi.port = (short)atol(colon + 1);
			}
			break;

		case 'p':
			hi.port = (short)atol(optarg);
			break;

		case 'c':
			thread_count = atoi(optarg);
			break;

		case 'n':
			msg_target = atoi(optarg);
			break;

		case 'm':
			mask_cfg = atoi(optarg);
			if (mask_cfg < MASK_NEVER || mask_cfg > MASK_ALTERNATE) {
				fprintf(stderr, "Invalid mask configuration: %d (range is [%d .. %d])\n",
					mask_cfg, MASK_NEVER, MASK_ALTERNATE);
				exit(EXIT_FAILURE);
			}
			break;

		case 't':
			timeout_seconds = atoi(optarg);
			break;

		case 'v':
			verbose = 1;
			break;

		default: /* '?' */
			usage(argv[0], host_default, port_default,
			      thread_count_default,
			      messages_default);
			exit(EXIT_SUCCESS);
		}
	}

	/* run threads */
	workers = calloc(sizeof(struct worker_thread), thread_count);

	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (i = 0; i < thread_count; ++i) {
		workers[i].id = i;
		workers[i].msg_target = msg_target;
		workers[i].hi = &hi;
		workers[i].verbose = verbose;
		workers[i].state = WS_INITIAL;
		workers[i].debug = verbose ? debug_verbose : debug_noop;
		workers[i].timeout_seconds = timeout_seconds;
		workers[i].mask_cfg = mask_cfg;
		pthread_create(&workers[i].thread, NULL,
			       worker_main, &workers[i]);
	}

	/* wait for threads to finish */
	for (i = 0; i < thread_count; ++i) {
		pthread_join(workers[i].thread, NULL);
		total += workers[i].msg_received;
		total_bytes += workers[i].byte_count;
	}

	/* timing */
	clock_gettime(CLOCK_MONOTONIC, &t1);
	float mili0 = t0.tv_sec * 1000 + t0.tv_nsec / 1000000;
	float mili1 = t1.tv_sec * 1000 + t1.tv_nsec / 1000000;

	if (total != 0) {
		int total_masked = 0;
		for (i = 0; i < thread_count; ++i) {
			total_masked += workers[i].mask_applied;
		}

		double kb_per_sec = ((double)total_bytes / (double)(mili1 - mili0)) / 1.024;
		printf("Sent+received %ld messages (%d sent masked) for a total of %ld bytes in %0.2f sec: %0.2f msg/sec (%0.2f KB/sec)\n",
		       total,
		       total_masked,
		       total_bytes,
		       (mili1 - mili0) / 1000.0,
		       1000 * ((double)total) / (mili1 - mili0),
		       kb_per_sec);
		return (total == thread_count * msg_target ? EXIT_SUCCESS : EXIT_FAILURE);
	} else {
		printf("No message was read.\n");
		return EXIT_FAILURE;
	}
}

