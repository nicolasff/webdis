#include "worker.h"
#include "client.h"
#include "http.h"
#include "cmd.h"
#include "pool.h"
#include "slog.h"
#include "websocket.h"
#include "conf.h"
#include "server.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <event.h>
#include <string.h>


struct worker *
worker_new(struct server *s) {

	int ret;
	struct worker *w = calloc(1, sizeof(struct worker));
	w->s = s;

	/* setup communication link */
	ret = pipe(w->link);
	(void)ret;

	/* Redis connection pool */
	w->pool = pool_new(w, s->cfg->pool_size_per_thread);

	return w;

}

void
worker_can_read(int fd, short event, void *p) {

	struct http_client *c = p;
	int ret, nparsed;

	(void)fd;
	(void)event;

	ret = http_client_read(c);
	if(ret <= 0) {
		if((client_error_t)ret == CLIENT_DISCONNECTED) {
			return;
		} else if (c->failed_alloc || (client_error_t)ret == CLIENT_OOM) {
			slog(c->w->s, WEBDIS_DEBUG, "503", 3);
			http_send_error(c, 503, "Service Unavailable");
			return;
		}
	}

	if(c->is_websocket) {
		/* Got websocket data */
		ws_add_data(c);
	} else {
		/* run parser */
		nparsed = http_client_execute(c);

		if(c->failed_alloc) {
			slog(c->w->s, WEBDIS_DEBUG, "503", 3);
			http_send_error(c, 503, "Service Unavailable");
		} else if (c->parser.flags & F_CONNECTION_CLOSE) {
			c->broken = 1;
		} else if(c->is_websocket) {
			/* we need to use the remaining (unparsed) data as the body. */
			if(nparsed < ret) {
				http_client_add_to_body(c, c->buffer + nparsed + 1, c->sz - nparsed - 1);
				ws_handshake_reply(c);
			} else {
				c->broken = 1;
			}
			free(c->buffer);
			c->buffer = NULL;
			c->sz = 0;
		} else if(nparsed != ret) {
			slog(c->w->s, WEBDIS_DEBUG, "400", 3);
			http_send_error(c, 400, "Bad Request");
		} else if(c->request_sz > c->s->cfg->http_max_request_size) {
			slog(c->w->s, WEBDIS_DEBUG, "413", 3);
			http_send_error(c, 413, "Request Entity Too Large");
		}
	}

	if(c->broken) { /* terminate client */
		http_client_free(c);
	} else {
		/* start monitoring input again */
		worker_monitor_input(c);
	}
}

/**
 * Monitor client FD for possible reads.
 */
void
worker_monitor_input(struct http_client *c) {

	event_set(&c->ev, c->fd, EV_READ, worker_can_read, c);
	event_base_set(c->w->base, &c->ev);
	event_add(&c->ev, NULL);
}

/**
 * Called when a client is sent to this worker.
 */
static void
worker_on_new_client(int pipefd, short event, void *ptr) {

	struct http_client *c;
	unsigned long addr;

	(void)event;
	(void)ptr;

	/* Get client from messaging pipe */
	int ret = read(pipefd, &addr, sizeof(addr));
	if(ret == sizeof(addr)) {
		c = (struct http_client*)addr;

		/* monitor client for input */
		worker_monitor_input(c);
	}
}

static void
worker_pool_connect(struct worker *w) {

	int i;
	/* create connections */
	for(i = 0; i < w->pool->count; ++i) {
		pool_connect(w->pool, w->s->cfg->database, 1);
	}

}

static void*
worker_main(void *p) {

	struct worker *w = p;
	struct event ev;

	/* setup libevent */
	w->base = event_base_new();

	/* monitor pipe link */
	event_set(&ev, w->link[0], EV_READ | EV_PERSIST, worker_on_new_client, w);
	event_base_set(w->base, &ev);
	event_add(&ev, NULL);

	/* connect to Redis */
	worker_pool_connect(w);

	/* loop */
	event_base_dispatch(w->base);

	return NULL;
}

void
worker_start(struct worker *w) {

	pthread_create(&w->thread, NULL, worker_main, w);
}

/**
 * Queue new client to process
 */
void
worker_add_client(struct worker *w, struct http_client *c) {

	/* write into pipe link */
	unsigned long addr = (unsigned long)c;
	int ret = write(w->link[1], &addr, sizeof(addr));
	(void)ret;
}

/**
 * Called when a client has finished reading input and can create a cmd
 */
void
worker_process_client(struct http_client *c) {

	/* check that the command can be executed */
	struct worker *w = c->w;
	cmd_response_t ret = CMD_PARAM_ERROR;
	switch(c->parser.method) {
		case HTTP_GET:
			if(c->path_sz == 16 && memcmp(c->path, "/crossdomain.xml", 16) == 0) {
				http_crossdomain(c);
				return;
			}
			slog(w->s, WEBDIS_DEBUG, c->path, c->path_sz);
			ret = cmd_run(c->w, c, 1+c->path, c->path_sz-1, NULL, 0);
			break;

		case HTTP_POST:
			slog(w->s, WEBDIS_DEBUG, c->path, c->path_sz);
			ret = cmd_run(c->w, c, c->body, c->body_sz, NULL, 0);
			break;

		case HTTP_PUT:
			slog(w->s, WEBDIS_DEBUG, c->path, c->path_sz);
			ret = cmd_run(c->w, c, 1+c->path, c->path_sz-1,
					c->body, c->body_sz);
			break;

		case HTTP_OPTIONS:
			http_send_options(c);
			return;

		default:
			slog(w->s, WEBDIS_DEBUG, "405", 3);
			http_send_error(c, 405, "Method Not Allowed");
			return;
	}

	switch(ret) {
		case CMD_ACL_FAIL:
		case CMD_PARAM_ERROR:
			slog(w->s, WEBDIS_DEBUG, "403", 3);
			http_send_error(c, 403, "Forbidden");
			break;

		case CMD_REDIS_UNAVAIL:
			slog(w->s, WEBDIS_DEBUG, "503", 3);
			http_send_error(c, 503, "Service Unavailable");
			break;
		default:
			break;
	}

}

