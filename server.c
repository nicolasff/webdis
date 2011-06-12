#include "server.h"
#include "worker.h"
#include "client.h"
#include "conf.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

/**
 * Sets up a non-blocking socket
 */
static int
socket_setup(const char *ip, short port) {

	int reuse = 1;
	struct sockaddr_in addr;
	int fd, ret;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	memset(&(addr.sin_addr), 0, sizeof(addr.sin_addr));
	addr.sin_addr.s_addr = inet_addr(ip);

	/* this sad list of tests could use a Maybe monad... */

	/* create socket */
	fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (-1 == fd) {
		/*syslog(LOG_ERR, "Socket error: %m\n");*/
		return -1;
	}

	/* reuse address if we've bound to it before. */
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
				sizeof(reuse)) < 0) {
		/* syslog(LOG_ERR, "setsockopt error: %m\n"); */
		return -1;
	}

	/* set socket as non-blocking. */
	ret = fcntl(fd, F_SETFD, O_NONBLOCK);
	if (0 != ret) {
		/* syslog(LOG_ERR, "fcntl error: %m\n"); */
		return -1;
	}

	/* bind */
	ret = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (0 != ret) {
		/* syslog(LOG_ERR, "Bind error: %m\n"); */
		return -1;
	}

	/* listen */
	ret = listen(fd, SOMAXCONN);
	if (0 != ret) {
		/* syslog(LOG_DEBUG, "Listen error: %m\n"); */
		return -1;
	}

	/* there you go, ready to accept! */
	return fd;
}

struct server *
server_new(const char *cfg_file) {
	
	int i;
	struct server *s = calloc(1, sizeof(struct server));

	s->cfg = conf_read(cfg_file);

	/* workers */
	s->w = calloc(s->cfg->http_threads, sizeof(struct worker*));
	for(i = 0; i < s->cfg->http_threads; ++i) {
		s->w[i] = worker_new(s);
	}
	return s;
}

static void
server_can_accept(int fd, short event, void *ptr) {

	struct server *s = ptr;
	struct worker *w;
	struct http_client *c;
	int client_fd;
	struct sockaddr_in addr;
	socklen_t addr_sz = sizeof(addr);
	char on = 1;
	(void)event;

	/* select worker to send the client to */
	w = s->w[s->next_worker];

	/* accept client */
	client_fd = accept(fd, (struct sockaddr*)&addr, &addr_sz);

	/* make non-blocking */
	ioctl(client_fd, (int)FIONBIO, (char *)&on);

	/* create client and send to worker. */
	if(client_fd > 0) {
		c = http_client_new(w, client_fd, addr.sin_addr.s_addr);
		worker_add_client(w, c);

		/* loop over ring of workers */
		s->next_worker = (s->next_worker + 1) % s->cfg->http_threads;
	} else { /* too many connections */
		slog(s, WEBDIS_NOTICE, "Too many connections", 0);
	}
}

/**
 * Daemonize server.
 * (taken from Redis)
 */
static void
server_daemonize(void) {
        int fd;

        if (fork() != 0) exit(0); /* parent exits */
        setsid(); /* create a new session */

        /* Every output goes to /dev/null. */
        if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
                dup2(fd, STDIN_FILENO);
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                if (fd > STDERR_FILENO) close(fd);
        }
}


int
server_start(struct server *s) {

	int i;
	if(s->cfg->daemonize) {
		server_daemonize();

		/* sometimes event mech gets lost on fork */
		if(event_reinit(s->base) != 0) {
			fprintf(stderr, "Error: event_reinit failed after fork");
		}
	}

	/* ignore sigpipe */
#ifdef SIGPIPE
	signal(SIGPIPE, SIG_IGN);
#endif

	slog_init(s);

	/* start worker threads */
	for(i = 0; i < s->cfg->http_threads; ++i) {
		worker_start(s->w[i]);
	}

	/* create socket */
	s->fd = socket_setup(s->cfg->http_host, s->cfg->http_port);
	if(s->fd < 0) {
		return -1;
	}

	/* initialize libevent */
	s->base = event_base_new();

	/* start http server */
	event_set(&s->ev, s->fd, EV_READ | EV_PERSIST, server_can_accept, s);
	event_base_set(s->base, &s->ev);
	event_add(&s->ev, NULL);

	event_base_dispatch(s->base);

	return 0;
}

