#ifndef CMD_H
#define CMD_H

#include <stdlib.h>
#include <hiredis/async.h>
#include <sys/queue.h>
#include <event.h>
#include <evhttp.h>

struct evhttp_request;
struct http_client;
struct server;
struct worker;
struct cmd;

typedef void (*formatting_fun)(redisAsyncContext *, void *, void *);
typedef enum {CMD_SENT,
	CMD_PARAM_ERROR,
	CMD_ACL_FAIL,
	CMD_REDIS_UNAVAIL} cmd_response_t;

struct cmd {
	int fd;

	int count;
	char **argv;
	size_t *argv_len;

	/* HTTP data */
	char *mime; /* forced output content-type */
	int mime_free; /* need to free mime buffer */

	char *filename; /* content-disposition attachment */

	char *if_none_match; /* used with ETags */
	char *jsonp; /* jsonp wrapper */
	char *separator; /* list separator for raw lists */
	int keep_alive;

	/* various flags */
	int started_responding;
	int is_websocket;
	int http_version;
	int database;

	struct http_client *pub_sub_client;
	redisAsyncContext *ac;
	struct worker *w;
};

struct subscription {
	struct server *s;
	struct cmd *cmd;
};

struct cmd *
cmd_new(int count);

void
cmd_free(struct cmd *c);

cmd_response_t
cmd_run(struct worker *w, struct http_client *client,
		const char *uri, size_t uri_len,
		const char *body, size_t body_len);

int
cmd_select_format(struct http_client *client, struct cmd *cmd,
		const char *uri, size_t uri_len, formatting_fun *f_format);

int
cmd_is_subscribe(struct cmd *cmd);

void
cmd_send(struct cmd *cmd, formatting_fun f_format);

void
cmd_setup(struct cmd *cmd, struct http_client *client);

#endif
