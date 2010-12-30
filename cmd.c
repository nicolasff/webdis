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

int
cmd_authorized(struct cmd *cmd, struct conf *cfg, struct evhttp_request *rq) {

	char *always_off[] = {"MULTI", "EXEC", "WATCH", "DISCARD", "SUBSCRIBE", "PSUBSCRIBE"};

	unsigned int i;
	int authorized = 1;
	struct acl *a;

	char *client_ip;
	u_short client_port;
	in_addr_t client_addr;

	const char *cmd_name = cmd->argv[0];
	size_t cmd_len = cmd->argv_len[0];

	/* some commands are always disabled, regardless of the config file. */
	for(i = 0; i < sizeof(always_off) / sizeof(always_off[0]); ++i) {
		if(strncasecmp(always_off[i], cmd_name, cmd_len) == 0) {
			return 0;
		}
	}

	/* find client's address */
	evhttp_connection_get_peer(rq->evcon, &client_ip, &client_port);
	client_addr = ntohl(inet_addr(client_ip));

	/* go through permissions */
	for(a = cfg->perms; a; a = a->next) {

		if(!acl_match(a, &client_addr)) continue; /* match client */

		/* go through authorized commands */
		for(i = 0; i < a->enabled.count; ++i) {
			if(strncasecmp(a->enabled.commands[i], cmd_name, cmd_len) == 0) {
				authorized = 1;
			}
		}

		/* go through unauthorized commands */
		for(i = 0; i < a->disabled.count; ++i) {
			if(strncasecmp(a->disabled.commands[i], cmd_name, cmd_len) == 0) {
				authorized = 0;
			}
		}
	}

	return authorized;
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
	if(!cmd_authorized(cmd, s->cfg, rq)) {
		return -1;
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
