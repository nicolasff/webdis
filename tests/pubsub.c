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

struct reader {
	int fd;

	struct event ev;
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
reader_http_request(struct reader *r) {

	char buffer[] = "GET /SUBSCRIBE/chan HTTP/1.1\r\n\r\n";
	char first_msg[] = "{\"SUBSCRIBE\":[\"subscribe\",\"chan\",1]}";
	char resp[2048];
	int pos = 0;

	write(r->fd, buffer, sizeof(buffer)-1);

	memset(resp, 0, sizeof(resp));
	while(1) {
		int ret = read(r->fd, resp+pos, sizeof(resp)-pos);
		if(ret <= 0) {
			printf("fd=%d, ret=%d\n", r->fd, ret);
			return;
		}
		pos += ret;

		if(strstr(resp, first_msg) != NULL) {
			break;
		}
	}
}

void
reader_can_read(int fd, short event, void *ptr) {

	struct reader *r = ptr;
	printf("Reader can read on fd=%d\n", fd);

	// reader_install(r);
}

void
reader_install(struct reader *r) {

	event_set(&r->ev, r->fd, EV_READ, reader_can_read, r);
	event_base_set(r->base, &r->ev);
	event_add(&r->ev, NULL);

}

void
reader_new(struct event_base *base) {

	struct reader *r = malloc(sizeof(struct reader));
	r->base = base;
	r->fd = webdis_connect("127.0.0.1", 7379);

	/* send read request. */
	reader_http_request(r);

	reader_install(r);
}

void
write_can_write(int fd, short event, void *ptr) {

	printf("Can write on fd=%d\n", fd);

}


int
main(int argc, char *argv[]) {

	/* Create R readers and W writers, send N messages in total. */

	struct event_base *base = event_base_new();
	int r = 10, w = 10, n = 1000;
	int i;

	for(i = 0; i < w; ++i) {
		reader_new(base);
	}

	event_base_dispatch(base);

	return EXIT_SUCCESS;
}

