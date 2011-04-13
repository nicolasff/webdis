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


struct cx {
	int fd;

	int *counter;
	int total;

	char *http_request;

	void (*read_fun)(int,short,void*);
	void (*write_fun)(int,short,void*);
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

void
cx_install(struct cx *c) {

	if(c->read_fun) {
		event_set(&c->evr, c->fd, EV_READ, c->read_fun, c);
		event_base_set(c->base, &c->evr);
		event_add(&c->evr, NULL);
	}
	if(c->write_fun) {
		event_set(&c->evw, c->fd, EV_WRITE, c->write_fun, c);
		event_base_set(c->base, &c->evw);
		event_add(&c->evw, NULL);
	}

}
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

			if(!p) break;	/* none left */
			p++;

			(*c->counter)++;
			if(((*c->counter * 100) % c->total) == 0) {
				printf("\r%d %%", 100 * *c->counter / c->total);
				fflush(stdout);
			}
			if(*c->counter > c->total) {
				event_base_loopbreak(c->base);
			}
		} while(1);
	}
	
	cx_install(c);
}


void
reader_new(struct event_base *base, int total, int *counter, int chan) {

	struct cx *c = calloc(1, sizeof(struct cx));
	c->base = base;
	c->counter = counter;
	c->total = total;
	c->fd = webdis_connect("127.0.0.1", 7379);
	c->read_fun = reader_can_read;

	/* send read request. */
	c->http_request = malloc(100);
	sprintf(c->http_request, "GET /SUBSCRIBE/chan:%d HTTP/1.1\r\n\r\n", chan);
	reader_http_request(c, c->http_request, "{\"SUBSCRIBE\":[\"subscribe\"");

	cx_install(c);
}

void
writer_can_read(int fd, short event, void *ptr) {
	char buffer[1024];
	struct cx *c = ptr;
	int r;

	(void)event;

	r = read(fd, buffer, sizeof(buffer)); /* discard */
	(void)r;

	cx_install(c);
}
void
writer_can_write(int fd, short event, void *ptr) {

	struct cx *c = ptr;

	(void)fd;
	(void)event;

	reader_http_request(c, c->http_request, "{\"PUBLISH\":");

	cx_install(c);
}

void
writer_new(struct event_base *base, int chan) {

	struct cx *c = malloc(sizeof(struct cx));
	c->base = base;
	c->fd = webdis_connect("127.0.0.1", 7379);
	c->read_fun = writer_can_read;
	c->write_fun = writer_can_write;

	/* send request. */
	c->http_request = malloc(100);
	sprintf(c->http_request, "GET /PUBLISH/chan:%d/hi HTTP/1.1\r\n\r\n", chan);
	reader_http_request(c, c->http_request, "{\"PUBLISH\":");

	cx_install(c);
}



int
main(int argc, char *argv[]) {

	/* Create R readers and W writers, send N messages in total. */

	struct timespec t0, t1;

	struct event_base *base = event_base_new();
	int r = 450, w = 10, chans = 1, n = 200000, count = 0;
	int i;

	(void)argc;
	(void)argv;

	for(i = 0; i < r; ++i) {
		reader_new(base, n, &count, i % chans);
	}

	for(i = 0; i < w; ++i) {
		writer_new(base, i % chans);
	}

	/* save time now */
	clock_gettime(CLOCK_MONOTONIC, &t0);
	
	/* run test */
	event_base_dispatch(base);

	/* timing */
	clock_gettime(CLOCK_MONOTONIC, &t1);
	float mili0 = t0.tv_sec * 1000 + t0.tv_nsec / 1000000;
	float mili1 = t1.tv_sec * 1000 + t1.tv_nsec / 1000000;

	printf("\rPushed %ld messages from %d writers to %d readers through %d channels in %0.2f sec: %0.2f msg/sec\n",
		(long)n, w, r, chans,
		(mili1-mili0)/1000.0,
		1000*n/(mili1-mili0));

	return EXIT_SUCCESS;
}

