#include "cmd.h"
#include "server.h"
#include "conf.h"

#include "formats/json.h"
#include "formats/raw.h"

#include <stdlib.h>
#include <string.h>
#include <hiredis/hiredis.h>
#include <netinet/in.h>
#include <arpa/inet.h>

struct cmd *
cmd_new(struct evhttp_request *rq, int count) {

	struct cmd *c = calloc(1, sizeof(struct cmd));

	c->rq = rq;
	c->count = count;

	c->argv = calloc(count, sizeof(char*));
	c->argv_len = calloc(count, sizeof(size_t));

	return c;
}


void
cmd_free(struct cmd *c) {

	free(c->argv);
	free(c->argv_len);

	free(c);
}

struct pubsub_client {
	struct server *s;
	struct evhttp_request *rq;
};

void on_http_disconnect(struct evhttp_connection *evcon, void *ctx) {
	struct pubsub_client *ps = ctx;

	(void)evcon;

	if(ps->s->ac->replies.head) { /* TODO: proper free. */
		if(ps->s->ac->replies.head->privdata) {
			cmd_free(ps->s->ac->replies.head->privdata);
		}
		ps->s->ac->replies.head->privdata = NULL;
	}
	redisAsyncFree(ps->s->ac);
	free(ps);
}

int
cmd_authorized(struct conf *cfg, struct evhttp_request *rq, const char *verb, size_t verb_len) {

	char *always_off[] = {"MULTI", "EXEC", "WATCH", "DISCARD"};

	struct disabled_command *dc;
	unsigned int i;

	char *client_ip;
	u_short client_port;
	in_addr_t client_addr;

	/* some commands are always disabled, regardless of the config file. */
	for(i = 0; i < sizeof(always_off) / sizeof(always_off[0]); ++i) {
		if(strncasecmp(always_off[i], verb, verb_len) == 0) {
			return 0;
		}
	}


	/* find client's address */
	evhttp_connection_get_peer(rq->evcon, &client_ip, &client_port);
	client_addr = ntohl(inet_addr(client_ip));

	for(dc = cfg->disabled; dc; dc = dc->next) {
		/* CIDR test */

		if((client_addr & dc->mask) != (dc->subnet & dc->mask)) {
			continue;
		}

		/* matched an ip */
		for(i = 0; i < dc->count; ++i) {
			if(strncasecmp(dc->commands[i], verb, verb_len) == 0) {
				return 0;
			}
		}
	}

	return 1;
}

int
cmd_run(struct server *s, struct evhttp_request *rq,
		const char *uri, size_t uri_len) {

	char *qmark = strchr(uri, '?');
	char *slash = strchr(uri, '/');
	const char *p;
	int cmd_len;
	int param_count = 0, cur_param = 1;

	struct cmd *cmd;
	formatting_fun fun;

	/* count arguments */
	if(qmark) {
		uri_len = qmark - uri;
	}
	for(p = uri; p && p < uri + uri_len; param_count++) {
		p = strchr(p+1, '/');
	}

	cmd = cmd_new(rq, param_count);

	if(slash) {
		cmd_len = slash - uri;
	} else {
		cmd_len = uri_len;
	}

	/* parse URI parameters */
	evhttp_parse_query(uri, &cmd->uri_params);

	/* get output formatting function */
	fun = get_formatting_funtion(&cmd->uri_params);

	/* there is always a first parameter, it's the command name */
	cmd->argv[0] = uri;
	cmd->argv_len[0] = cmd_len;


	/* check that the client is able to run this command */
	if(!cmd_authorized(s->cfg, rq, cmd->argv[0], cmd->argv_len[0])) {
		return -1;
	}

	/* check if we have to split the connection */
	if(cmd_is_subscribe(cmd)) {
		struct pubsub_client *ps;
		ps = calloc(1, sizeof(struct pubsub_client));
		ps->s = s = server_copy(s);

		ps->rq = rq;

		evhttp_connection_set_closecb(rq->evcon, on_http_disconnect, ps);
	}

	if(!slash) {
		redisAsyncCommandArgv(s->ac, fun, cmd, 1, cmd->argv, cmd->argv_len);
		return 0;
	}
	p = slash + 1;
	while(p < uri + uri_len) {

		const char *arg = p;
		int arg_len;
		char *next = strchr(arg, '/');
		if(next) { /* found a slash */
			arg_len = next - arg;
			p = next + 1;
		} else { /* last argument */
			arg_len = uri + uri_len - arg;
			p = uri + uri_len;
		}

		/* record argument */
		cmd->argv[cur_param] = arg;
		cmd->argv_len[cur_param] = arg_len;

		cur_param++;
	}

	redisAsyncCommandArgv(s->ac, fun, cmd, param_count, cmd->argv, cmd->argv_len);
	return 0;
}



formatting_fun
get_formatting_funtion(struct evkeyvalq *params) {

	struct evkeyval *kv;

	/* check for JSONP */
	TAILQ_FOREACH(kv, params, next) {
		if(strcmp(kv->key, "format") == 0) {
			if(strcmp(kv->value, "raw") == 0) {
				return raw_reply;
			} else if(strcmp(kv->value, "json") == 0) {
				return json_reply;
			}
			break;
		}
	}

	return json_reply;
}

int
cmd_is_subscribe(struct cmd *cmd) {

	if(strncasecmp(cmd->argv[0], "SUBSCRIBE", cmd->argv_len[0]) == 0 ||
		strncasecmp(cmd->argv[0], "PSUBSCRIBE", cmd->argv_len[0]) == 0) {
		return 1;
	}
	return 0;
}
