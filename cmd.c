#include "cmd.h"
#include "server.h"
#include "conf.h"
#include "acl.h"

#include "formats/json.h"
#include "formats/raw.h"
#include "formats/custom-type.h"

#include <stdlib.h>
#include <string.h>
#include <hiredis/hiredis.h>
#include <ctype.h>

struct cmd *
cmd_new(struct evhttp_request *rq, int count) {

	struct cmd *c = calloc(1, sizeof(struct cmd));

	c->rq = rq;
	c->count = count;

	c->argv = calloc(1+count, sizeof(char*));
	c->argv_len = calloc(1+count, sizeof(size_t));

	return c;
}


void
cmd_free(struct cmd *c) {

	free(c->argv);
	free(c->argv_len);

	free(c->mime);
	free(c->mimeKey);

	free(c);
}

/**
 * Detect disconnection of a pub/sub client. We need to clean up the command.
 */
void on_http_disconnect(struct evhttp_connection *evcon, void *ctx) {
	struct pubsub_client *ps = ctx;

	(void)evcon;

	if(ps->s->ac->replies.head) {
		struct cmd *cmd = ps->s->ac->replies.head->privdata;
		if(cmd) {
			cmd_free(cmd);
		}
		ps->s->ac->replies.head->privdata = NULL;
	}
	redisAsyncFree(ps->s->ac);
	free(ps);
}

/* taken from libevent */
static char *
decode_uri(const char *uri, size_t length, size_t *out_len, int always_decode_plus) {
	char c;
	size_t i, j;
	int in_query = always_decode_plus;

	char *ret = malloc(length);

	for (i = j = 0; i < length; i++) {
		c = uri[i];
		if (c == '?') {
			in_query = 1;
		} else if (c == '+' && in_query) {
			c = ' ';
		} else if (c == '%' && isxdigit((unsigned char)uri[i+1]) &&
		    isxdigit((unsigned char)uri[i+2])) {
			char tmp[] = { uri[i+1], uri[i+2], '\0' };
			c = (char)strtol(tmp, NULL, 16);
			i += 2;
		}
		ret[j++] = c;
	}
	*out_len = (size_t)j;

	return ret;
}


int
cmd_run(struct server *s, struct evhttp_request *rq,
		const char *uri, size_t uri_len) {

	char *qmark = strchr(uri, '?');
	char *slash = strchr(uri, '/');
	const char *p;
	int cmd_len;
	int param_count = 0, cur_param = 1, i;

	struct cmd *cmd;

	formatting_fun f_format;
	transform_fun f_transform = NULL;

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
	get_functions(cmd, &f_format, &f_transform);

	/* there is always a first parameter, it's the command name */
	cmd->argv[0] = uri;
	cmd->argv_len[0] = cmd_len;


	/* check that the client is able to run this command */
	if(!acl_allow_command(cmd, s->cfg, rq)) {
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
		redisAsyncCommandArgv(s->ac, f_format, cmd, 1, cmd->argv, cmd->argv_len);
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
		cmd->argv[cur_param] = decode_uri(arg, arg_len, &cmd->argv_len[cur_param], 1);
		cur_param++;
	}

	/* transform command if we need to. */
	if(f_transform) f_transform(cmd);

	redisAsyncCommandArgv(s->ac, f_format, cmd, cmd->count, cmd->argv, cmd->argv_len);

	for(i = 1; i < cur_param; ++i) {
		free((char*)cmd->argv[i]);
	}

	return 0;
}

void
get_functions(struct cmd *cmd, formatting_fun *f_format, transform_fun *f_transform) {

	struct evkeyval *kv;

	/* defaults */
	*f_format = json_reply;
	*f_transform = NULL;

	/* check for JSONP */
	TAILQ_FOREACH(kv, &cmd->uri_params, next) {
		if(strcmp(kv->key, "format") == 0) {
			if(strcmp(kv->value, "raw") == 0) {
				*f_format = raw_reply;
			} else if(strcmp(kv->value, "json") == 0) {
				*f_format = json_reply;
			}
			break;
		} else if(strcmp(kv->key, "typeKey") == 0) { /* MIME type in key. */
			cmd->mimeKey = strdup(kv->value);
			*f_transform = custom_type_process_cmd;
			*f_format = custom_type_reply;
		} else if(strcmp(kv->key, "type") == 0) { /* MIME type in parameter */
			cmd->mime = strdup(kv->value);
		}
	}
}

int
cmd_is_subscribe(struct cmd *cmd) {

	if(strncasecmp(cmd->argv[0], "SUBSCRIBE", cmd->argv_len[0]) == 0 ||
		strncasecmp(cmd->argv[0], "PSUBSCRIBE", cmd->argv_len[0]) == 0) {
		return 1;
	}
	return 0;
}
