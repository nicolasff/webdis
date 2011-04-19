/* http://tools.ietf.org/html/draft-hixie-thewebsocketprotocol-76 */

#include <stdlib.h>
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <event.h>

struct host_info {
	char *host;
	short port;
};

/* worker_thread, with counter of remaining messages */
struct worker_thread {
	struct host_info *hi;
	struct event_base *base;

	int msg_target;
	int msg_received;
	int msg_sent;
	int byte_count;
	pthread_t thread;

	struct evbuffer *buffer;
	int got_header;

	int verbose;
	struct event ev_w;
};

void
process_message(struct worker_thread *wt, size_t sz) {

	// printf("process_message\n");
	if(wt->msg_received % 10000 == 0) {
		printf("thread %u: %8d messages left (got %9d bytes so far).\n",
			(unsigned int)wt->thread,
			wt->msg_target - wt->msg_received, wt->byte_count);
	}
	wt->byte_count += sz;

	/* decrement read count, and stop receiving when we reach zero. */
	wt->msg_received++;
	if(wt->msg_received == wt->msg_target) {
		event_base_loopexit(wt->base, NULL);
	}
}

void
websocket_write(int fd, short event, void *ptr) {
	int ret;
	struct worker_thread *wt = ptr;

	if(event != EV_WRITE) {
		return;
	}

	char message[] = "\x00[\"SET\",\"key\",\"value\"]\xff\x00[\"GET\",\"key\"]\xff";
	ret = write(fd, message, sizeof(message)-1);
	if(ret != sizeof(message)-1) {
		fprintf(stderr, "write on %d failed: %s\n", fd, strerror(errno));
		close(fd);
	}

	wt->msg_sent += 2;
	if(wt->msg_sent < wt->msg_target) {
		event_set(&wt->ev_w, fd, EV_WRITE, websocket_write, wt);
		event_base_set(wt->base, &wt->ev_w);
		ret = event_add(&wt->ev_w, NULL);
	}
}

static void
websocket_read(int fd, short event, void *ptr) {
	char packet[2048], *pos;
	int ret, success = 1;

	struct worker_thread *wt = ptr;

	if(event != EV_READ) {
		return;
	}

	/* read message */
	ret = read(fd, packet, sizeof(packet));
	pos = packet;
	if(ret > 0) {
		char *data, *last;
		int sz, msg_sz;

		if(wt->got_header == 0) { /* first response */
			char *frame_start = strstr(packet, "MH"); /* end of the handshake */
			if(frame_start == NULL) {
				return; /* not yet */
			} else { /* start monitoring possible writes */
				printf("start monitoring possible writes\n");
				evbuffer_add(wt->buffer, frame_start + 2, ret - (frame_start + 2 - packet));

				wt->got_header = 1;
				event_set(&wt->ev_w, fd, EV_WRITE,
						websocket_write, wt);
				event_base_set(wt->base, &wt->ev_w);
				ret = event_add(&wt->ev_w, NULL);
			}
		} else {
			/* we've had the header already, now bufffer data. */
			evbuffer_add(wt->buffer, packet, ret);
		}

		while(1) {
			data = (char*)EVBUFFER_DATA(wt->buffer);
			sz = EVBUFFER_LENGTH(wt->buffer);

			if(sz == 0) { /* no data */
				break;
			}
			if(*data != 0) { /* missing frame start */
				success = 0;
				break;
			}
			last = memchr(data, 0xff, sz); /* look for frame end */
			if(!last) {
				/* no end of frame in sight. */
				break;
			}
			msg_sz = last - data - 1;
			process_message(ptr, msg_sz); /* record packet */

			/* drain including frame delimiters (+2 bytes) */
			evbuffer_drain(wt->buffer, msg_sz + 2); 
		}
	} else {
		printf("ret=%d\n", ret);
		success = 0;
	}
	if(success == 0) {
		shutdown(fd, SHUT_RDWR);
		close(fd);
		event_base_loopexit(wt->base, NULL);
	}
}

void*
worker_main(void *ptr) {

	char ws_template[] = "GET /.json HTTP/1.1\r\n"
				"Host: %s:%d\r\n"
				"Connection: Upgrade\r\n"
				"Upgrade: WebSocket\r\n"
				"Origin: http://%s:%d\r\n"
				"Sec-WebSocket-Key1: 18x 6]8vM;54 *(5:  {   U1]8  z [  8\r\n"
				"Sec-WebSocket-Key2: 1_ tx7X d  <  nw  334J702) 7]o}` 0\r\n"
				"\r\n"
				"Tm[K T2u";

	struct worker_thread *wt = ptr;

	int ret;
	int fd;
	struct sockaddr_in addr;
	char *ws_handshake;
	size_t ws_handshake_sz;

	/* connect socket */
	fd = socket(AF_INET, SOCK_STREAM, 0);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(wt->hi->port);
	memset(&(addr.sin_addr), 0, sizeof(addr.sin_addr));
	addr.sin_addr.s_addr = inet_addr(wt->hi->host);

	ret = connect(fd, (struct sockaddr*)&addr, sizeof(struct sockaddr));
	if(ret != 0) {
		fprintf(stderr, "connect: ret=%d: %s\n", ret, strerror(errno));
		return NULL;
	}

	/* initialize worker thread */
	wt->base = event_base_new();
	wt->buffer = evbuffer_new();
	wt->byte_count = 0;
	wt->got_header = 0;

	/* send handshake */
	ws_handshake_sz = sizeof(ws_handshake)
		+ 2*strlen(wt->hi->host) + 500;
	ws_handshake = calloc(ws_handshake_sz, 1);
	ws_handshake_sz = (size_t)sprintf(ws_handshake, ws_template, 
			wt->hi->host, wt->hi->port,
			wt->hi->host, wt->hi->port);
	ret = write(fd, ws_handshake, ws_handshake_sz);

	struct event ev_r;
	event_set(&ev_r, fd, EV_READ | EV_PERSIST, websocket_read, wt);
	event_base_set(wt->base, &ev_r);
	event_add(&ev_r, NULL);

	/* go! */
	event_base_dispatch(wt->base);
	event_base_free(wt->base);
	free(ws_handshake);
	return NULL;
}

void
usage(const char* argv0, char *host_default, short port_default,
		int thread_count_default, int messages_default) {

	printf("Usage: %s [options]\n"
		"Options are:\n"
		"\t-h host\t\t(default = \"%s\")\n"
		"\t-p port\t\t(default = %d)\n"
		"\t-c threads\t(default = %d)\n"
		"\t-n count\t(number of messages per thread, default = %d)\n"
		"\t-v\t\t(verbose)\n",
		argv0, host_default, (int)port_default,
		thread_count_default, messages_default);
}

int
main(int argc, char *argv[]) {

	struct timespec t0, t1;

	int messages_default = 100000;
	int thread_count_default = 4;
	short port_default = 7379;
	char *host_default = "127.0.0.1";

	int msg_target = messages_default;
	int thread_count = thread_count_default;
	int i, opt;
	char *colon;
	double total = 0, total_bytes = 0;
	int verbose = 0;

	struct host_info hi = {host_default, port_default};

	struct worker_thread *workers;
	
	/* getopt */
	while ((opt = getopt(argc, argv, "h:p:c:n:v")) != -1) {
		switch (opt) {
			case 'h':
				colon = strchr(optarg, ':');
				if(!colon) {
					size_t sz = strlen(optarg);
					hi.host = calloc(1 + sz, 1);
					strncpy(hi.host, optarg, sz);
				} else {
					hi.host = calloc(1+colon-optarg, 1);
					strncpy(hi.host, optarg, colon-optarg);
					hi.port = (short)atol(colon+1);
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

			case 'v':
				verbose = 1;
				break;
			default: /* '?' */
				usage(argv[0], host_default, port_default,
						thread_count_default,
						messages_default);
				exit(EXIT_FAILURE);
		}
	}

	/* run threads */
	workers = calloc(sizeof(struct worker_thread), thread_count);

	clock_gettime(CLOCK_MONOTONIC, &t0);
	for(i = 0; i < thread_count; ++i) {
		workers[i].msg_target = msg_target;
		workers[i].hi = &hi;
		workers[i].verbose = verbose;

		pthread_create(&workers[i].thread, NULL,
				worker_main, &workers[i]);
	}

	/* wait for threads to finish */
	for(i = 0; i < thread_count; ++i) {
		pthread_join(workers[i].thread, NULL);
		total += workers[i].msg_received;
		total_bytes += workers[i].byte_count;
	}

	/* timing */
	clock_gettime(CLOCK_MONOTONIC, &t1);
	float mili0 = t0.tv_sec * 1000 + t0.tv_nsec / 1000000;
	float mili1 = t1.tv_sec * 1000 + t1.tv_nsec / 1000000;

	if(total != 0) {
		printf("Read %ld messages in %0.2f sec: %0.2f msg/sec (%d MB/sec, %d KB/sec)\n",
			(long)total, 
			(mili1-mili0)/1000.0,
			1000*total/(mili1-mili0),
			(int)(total_bytes / (1000*(mili1-mili0))),
			(int)(total_bytes / (mili1-mili0)));
		return EXIT_SUCCESS;
	} else {
		printf("No message was read.\n");
		return EXIT_FAILURE;
	}
}

