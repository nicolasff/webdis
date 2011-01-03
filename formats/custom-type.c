#include "custom-type.h"
#include "cmd.h"
#include "common.h"

#include <string.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

static void
custom_400(struct cmd *cmd) {
	evhttp_send_reply(cmd->rq, 400, "Bad request", NULL);
	cmd_free(cmd);
}

void
custom_type_reply(redisAsyncContext *c, void *r, void *privdata) {

	redisReply *reply = r;
	struct cmd *cmd = privdata;
	char *ct;
	(void)c;

	evhttp_clear_headers(&cmd->uri_params);

	if (reply == NULL) {
		evhttp_send_reply(cmd->rq, 404, "Not Found", NULL);
		return;
	}

	if(cmd->mime) { /* use the given content-type */
		if(reply->type != REDIS_REPLY_STRING) {
			custom_400(cmd);
			return;
		}
		format_send_reply(cmd, reply->str, reply->len, cmd->mime);
		return;
	}

	/* we expect array(string, string) */
	if(!cmd->mimeKey || reply->type != REDIS_REPLY_ARRAY || reply->elements != 2 || reply->element[0]->type != REDIS_REPLY_STRING) {
		custom_400(cmd);
		return;
	}

	/* case of MGET, we need to have a string for content-type in element[1] */
	if(reply->element[1]->type == REDIS_REPLY_STRING) {
		ct = reply->element[1]->str;
	} else {
		ct = "binary/octet-stream";
	}

	/* send reply */
	format_send_reply(cmd, reply->element[0]->str, reply->element[0]->len, ct);
	return;
}

/* This will change a GET command into MGET if a key is provided to get the response MIME-type from. */
void
custom_type_process_cmd(struct cmd *cmd) {
	/* MGET if mode is “custom” */
	if(cmd->count == 2 && cmd->argv_len[0] == 3 &&
		strncasecmp(cmd->argv[0], "GET", 3) == 0 && cmd->mimeKey) {
		
		cmd->count++;	/* space for content-type key */
		cmd->argv = realloc(cmd->argv, cmd->count * sizeof(char*));
		cmd->argv_len = realloc(cmd->argv_len, cmd->count * sizeof(size_t));

		/* replace command with MGET */
		cmd->argv[0] = "MGET";
		cmd->argv_len[0] = 4;

		/* add mime key after the key. */
		cmd->argv[2] = strdup(cmd->mimeKey);
		cmd->argv_len[2] = strlen(cmd->mimeKey);
	}
}
