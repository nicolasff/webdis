#include <stdlib.h>
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <event.h>


/*
 * Connection object.
 */
struct cx {
	int fd;

	int *counter;	/* shared counter of the number of messages received. */
	int total;	/* total number of messages to send */

	char *http_request;

	void (*read_fun)(int,short,void*);	/* called when able to read fd */
	void (*write_fun)(int,short,void*);	/* called when able to write fd */

	/* libevent data structures */
	struct event evr, evw;
	struct event_base *base;
};

int
webdis_connect(const char *host, short port) {

	int ret;
	int fd;
	struct sockaddr_in addr;

	/* connect socket */
	fd = socket(AF_INET, SOCK_STREAM, 0);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	memset(&(addr.sin_addr), 0, sizeof(addr.sin_addr));
	addr.sin_addr.s_addr = inet_addr(host);

	ret = connect(fd, (struct sockaddr*)&addr, sizeof(struct sockaddr));
	if(ret != 0) {
		fprintf(stderr, "connect: ret=%d: %s\n", ret, strerror(errno));
		return -1;
	}

	return fd;
}

/**
 * Send request and read until the delimiter string is reached. blocking.
 */
void
reader_http_request(struct cx *c, const char* buffer, const char *limit) {

	char resp[2048];
	int pos = 0;

	int r = write(c->fd, buffer, strlen(buffer));
	(void)r;

	memset(resp, 0, sizeof(resp));
	while(1) {
		int ret = read(c->fd, resp+pos, sizeof(resp)-pos);
		if(ret <= 0) {
			return;
		}
		pos += ret;

		if(strstr(resp, limit) != NULL) {
			break;
		}
	}
}

/**
 * (re)install connection in the event loop.
 */
void
cx_install(struct cx *c) {

	if(c->read_fun) { /* attach callback for read. */
		event_set(&c->evr, c->fd, EV_READ, c->read_fun, c);
		event_base_set(c->base, &c->evr);
		event_add(&c->evr, NULL);
	}
	if(c->write_fun) { /* attach callback for write. */
		event_set(&c->evw, c->fd, EV_WRITE, c->write_fun, c);
		event_base_set(c->base, &c->evw);
		event_add(&c->evw, NULL);
	}

}

/**
 * Called when a reader has received data.
 */
void
reader_can_read(int fd, short event, void *ptr) {

	char buffer[1024];
	struct cx *c = ptr;
	const char *p;

	(void)event;

	int ret = read(fd, buffer, sizeof(buffer));
	if(ret > 0) {

		/* count messages, each message starts with '{' */
		p = buffer;
		do {
			/* look for the start of a message */
			p = memchr(p, '{', buffer + ret - p);

			if(!p) break;	/* none left. */
			p++;

			(*c->counter)++; /* increment the global message counter. */
			if(((*c->counter * 100) % c->total) == 0) { /* show progress. */
				printf("\r%d %%", 100 * *c->counter / c->total);
				fflush(stdout);
			}
			if(*c->counter > c->total) {
				/* halt event loop. */
				event_base_loopbreak(c->base);
			}
		} while(1);
	}
	
	cx_install(c);
}


/**
 * create a new reader object.
 */
void
reader_new(struct event_base *base, const char *host, short port, int total, int *counter, int chan) {

	struct cx *c = calloc(1, sizeof(struct cx));
	c->base = base;
	c->counter = counter;
	c->total = total;
	c->fd = webdis_connect(host, port);
	c->read_fun = reader_can_read;

	/* send subscription request. */
	c->http_request = malloc(100);
	sprintf(c->http_request, "GET /SUBSCRIBE/chan:%d HTTP/1.1\r\n\r\n", chan);
	reader_http_request(c, c->http_request, "{\"SUBSCRIBE\":[\"subscribe\"");

	/* add to the event loop. */
	cx_install(c);
}

/**
 * Called when a writer has received data back. read and ignore.
 */
void
writer_can_read(int fd, short event, void *ptr) {
	char buffer[1024];
	struct cx *c = ptr;
	int r;

	(void)event;

	r = read(fd, buffer, sizeof(buffer)); /* discard */
	(void)r;

	/* re-install in the event loop. */
	cx_install(c);
}

/* send request */
void
writer_can_write(int fd, short event, void *ptr) {

	struct cx *c = ptr;

	(void)fd;
	(void)event;

	reader_http_request(c, c->http_request, "{\"PUBLISH\":");

	cx_install(c);
}

void
writer_new(struct event_base *base, const char *host, short port, int chan) {

	struct cx *c = malloc(sizeof(struct cx));
	c->base = base;
	c->fd = webdis_connect(host, port);
	c->read_fun = writer_can_read;
	c->write_fun = writer_can_write;

	/* send request. */
	c->http_request = malloc(100);
	sprintf(c->http_request, "GET /PUBLISH/chan:%d/hi HTTP/1.1\r\n\r\n", chan);
	reader_http_request(c, c->http_request, "{\"PUBLISH\":");

	cx_install(c);
}


void
usage(const char* argv0, char *host_default, short port_default,
		int r_default, int w_default, int n_default, int c_default) {

	printf("Usage: %s [options]\n"
		"Options are:\n"
		"\t-h host\t\t(default = \"%s\")\n"
		"\t-p port\t\t(default = %d)\n"
		"\t-r readers\t(default = %d)\n"
		"\t-w writers\t(default = %d)\n"
		"\t-c channels\t(default = %d)\n"
		"\t-n messages\t(number of messages to read in total, default = %d)\n",
		argv0, host_default, (int)port_default,
		r_default, w_default,
		c_default, n_default);
}

static struct event_base *__base;
void on_sigint(int s) {
	(void)s;
	event_base_loopbreak(__base);
}


int
main(int argc, char *argv[]) {

	/* Create R readers and W writers, send N messages in total. */

	struct timespec t0, t1;

	struct event_base *base = event_base_new();
	int i, count = 0;

	/* getopt vars */
	int opt;
	char *colon;

	/* default values */
	short port_default = 7379;
	char *host_default = "127.0.0.1";
	int r_default = 450, w_default = 10, n_default = 100000, c_default = 1;


	/* real values */
	int r = r_default, w = w_default, chans = c_default, n = n_default;
	char *host = host_default;
	short port = port_default;

	/* getopt */
	while ((opt = getopt(argc, argv, "h:p:r:w:c:n:")) != -1) {
		switch (opt) {
			case 'h':
				colon = strchr(optarg, ':');
				if(!colon) {
					size_t sz = strlen(optarg);
					host = calloc(1 + sz, 1);
					strncpy(host, optarg, sz);
				} else {
					host = calloc(1+colon-optarg, 1);
					strncpy(host, optarg, colon-optarg);
					port = (short)atol(colon+1);
				}
				break;

			case 'p':
				port = (short)atol(optarg);
				break;

			case 'r':
				r = atoi(optarg);
				break;

			case 'w':
				w = atoi(optarg);
				break;

			case 'c':
				chans = atoi(optarg);
				break;

			case 'n':
				n = atoi(optarg);
				break;

			default:
				usage(argv[0], host_default, port_default,
						r_default, w_default,
						n_default, c_default);
				exit(EXIT_FAILURE);
		}
	}

	for(i = 0; i < r; ++i) {
		reader_new(base, host, port, n, &count, i % chans);
	}

	for(i = 0; i < w; ++i) {
		writer_new(base, host, port, i % chans);
	}

	/* install signal handler */
	__base = base;
	signal(SIGINT, on_sigint);

	/* save time now */
	clock_gettime(CLOCK_MONOTONIC, &t0);
	
	/* run test */
	event_base_dispatch(base);

	/* timing */
	clock_gettime(CLOCK_MONOTONIC, &t1);
	float mili0 = t0.tv_sec * 1000 + t0.tv_nsec / 1000000;
	float mili1 = t1.tv_sec * 1000 + t1.tv_nsec / 1000000;

	printf("\rReceived %d messages from %d writers to %d readers through %d channels in %0.2f sec: received %0.2f msg/sec\n",
		count, w, r, chans,
		(mili1-mili0)/1000.0,
		1000*count/(mili1-mili0));

	return EXIT_SUCCESS;
}

