#include "cmd.h"
#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <hiredis/hiredis.h>

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

void
cmd_run(redisAsyncContext *c, struct evhttp_request *rq, const char *uri, size_t uri_len) {

	char *slash = strchr(uri, '/');
	int cmd_len;
	int param_count = 0, cur_param = 1;

	struct cmd *cmd;

	const char *p;

	/* count arguments */
	for(p = uri; p && p < uri + uri_len; param_count++) {
		p = strchr(p+1, '/');
	}

	cmd = cmd_new(rq, param_count);

	if(slash) {
		cmd_len = slash - uri;
	} else {
		cmd_len = uri_len;
	}

	/* there is always a first parameter, it's the command name */
	cmd->argv[0] = uri;
	cmd->argv_len[0] = cmd_len;

	if(!slash) {
		redisAsyncCommandArgv(c, json_reply, cmd, 1, cmd->argv, cmd->argv_len);
		return;
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

	redisAsyncCommandArgv(c, json_reply, cmd, param_count, cmd->argv, cmd->argv_len);
}

