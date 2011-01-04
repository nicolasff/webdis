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
		switch(reply->type) {

			case REDIS_REPLY_NIL:
				format_send_reply(cmd, "", 0, cmd->mime);
				return;

			case REDIS_REPLY_STRING:
				format_send_reply(cmd, reply->str, reply->len, cmd->mime);
				return;

			default:
				custom_400(cmd);
				return;
		}
	}

	/* we expect array(string, string) */
	if(reply->type != REDIS_REPLY_ARRAY || reply->elements != 2 || reply->element[0]->type != REDIS_REPLY_STRING) {
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

