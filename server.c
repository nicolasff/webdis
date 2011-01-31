#include "server.h"
#include "conf.h"
#include "cmd.h"
#include "slog.h"
#include "http.h"
#include "client.h"

#include <hiredis/hiredis.h>
#include <hiredis/adapters/libevent.h>
#include <jansson.h>

#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>

/**
 * Sets up a non-blocking socket
 */
int
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
server_new(const char *filename) {
	struct server *s = calloc(1, sizeof(struct server));

	s->cfg = conf_read(filename);
	s->base = event_base_new();

	return s;
}

void
server_free(struct server *s) {

	/* cleanup Redis async object, _before_ the 2 struct event. */
	redisAsyncFree(s->ac);

	/* event_del(&s->ev); */
	event_del(&s->ev_reconnect);
	free(s);
}

static void
connectCallback(const redisAsyncContext *c) {
	((void)c);
}

static void
disconnectCallback(const redisAsyncContext *c, int status) {
	struct server *s = c->data;
	if (status != REDIS_OK) {
		fprintf(stderr, "Error: %s\n", c->errstr);
	}
	s->ac = NULL;

	/* wait 10 msec and reconnect */
	s->tv_reconnect.tv_sec = 0;
	s->tv_reconnect.tv_usec = 100000;
	webdis_connect(s);
}

static void
on_timer_reconnect(int fd, short event, void *ctx) {

	(void)fd;
	(void)event;
	struct server *s = ctx;

	if(s->ac) {
		redisLibeventCleanup(s->ac->data);
		redisFree((redisContext*)s->ac);
	}

	if(s->cfg->redis_host[0] == '/') { /* unix socket */
		s->ac = redisAsyncConnectUnix(s->cfg->redis_host);
	} else {
		s->ac = redisAsyncConnect(s->cfg->redis_host, s->cfg->redis_port);
	}

	s->ac->data = s;

	if(s->ac->err) {
		const char err[] = "Connection failed";
		slog(s, WEBDIS_ERROR, err, sizeof(err)-1);
		fprintf(stderr, "Error: %s\n", s->ac->errstr);
	}

	redisLibeventAttach(s->ac, s->base);
	redisAsyncSetConnectCallback(s->ac, connectCallback);
	redisAsyncSetDisconnectCallback(s->ac, disconnectCallback);

	if (s->cfg->redis_auth) { /* authenticate. */
		redisAsyncCommand(s->ac, NULL, NULL, "AUTH %s", s->cfg->redis_auth);
	}
}

void
webdis_connect(struct server *s) {
	/* schedule reconnect */
	evtimer_set(&s->ev_reconnect, on_timer_reconnect, s);
	event_base_set(s->base, &s->ev_reconnect);
	evtimer_add(&s->ev_reconnect, &s->tv_reconnect);
}

struct server *
server_copy(const struct server *s) {
	struct server *ret = calloc(1, sizeof(struct server));

	*ret = *s;

	/* create a new connection */
	ret->ac = NULL;
	on_timer_reconnect(0, 0, ret);

	return ret;
}

static void
on_possible_accept(int fd, short event, void *ctx) {

	struct server *s = ctx;
	int client_fd;
	struct sockaddr_in addr;
	socklen_t addr_sz = sizeof(addr);
	(void)event;
	struct http_client *c;

	client_fd = accept(fd, (struct sockaddr*)&addr, &addr_sz);
	c = http_client_new(client_fd, s);
	c->addr = addr.sin_addr.s_addr;
	http_client_serve(c);
}

/* Taken from Redis. */
void
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

void
server_start(struct server *s) {


	if(s->cfg->daemonize) {
		server_daemonize();
	}

	/* ignore sigpipe */
#ifdef SIGPIPE
	signal(SIGPIPE, SIG_IGN);
#endif

	/* start http server */
	slog(s, WEBDIS_INFO, "Starting HTTP Server", sizeof("Starting HTTP Server")-1);

	s->fd = socket_setup(s->cfg->http_host, s->cfg->http_port);
	/* check return value. */
	if(s->fd == -1) {
		char err[] = "Failed to create socket.";
		slog(s, WEBDIS_ERROR, err, sizeof(err)-1);
		_exit(1);
	}
	event_set(&s->ev, s->fd, EV_READ | EV_PERSIST, on_possible_accept, s);
	event_base_set(s->base, &s->ev);
	event_add(&s->ev, NULL);

	/* drop privileges */
	slog(s, WEBDIS_INFO, "Dropping Privileges", sizeof("Dropping Privileges")-1);
	setuid(s->cfg->user);
	setgid(s->cfg->group);

	/* attach hiredis to libevent base */
	webdis_connect(s);

	/* loop */
	event_base_dispatch(s->base);
}
