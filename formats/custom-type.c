#include "custom-type.h"
#include "cmd.h"
#include "common.h"
#include "http.h"

#include <string.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

void
custom_type_reply(redisAsyncContext *c, void *r, void *privdata) {

	redisReply *reply = r;
	struct cmd *cmd = privdata;
	(void)c;
	char int_buffer[50];
	int int_len;

	if(reply == NULL) {
		http_send_reply(cmd->client, 404, "Not Found", NULL, 0);
		return;
	}

	if(cmd->mime) { /* use the given content-type, but only for strings */
		switch(reply->type) {

			case REDIS_REPLY_NIL: /* or nil values */
				format_send_reply(cmd, "", 0, cmd->mime);
				return;

			case REDIS_REPLY_STRING:
				format_send_reply(cmd, reply->str, reply->len, cmd->mime);
				return;

			case REDIS_REPLY_INTEGER:
				int_len = sprintf(int_buffer, "%lld", reply->integer);
				format_send_reply(cmd, int_buffer, int_len, cmd->mime);
				return;
		}
	}

	/* couldn't make sense of what the client wanted. */
	http_send_reply(cmd->client, 400, "Bad request", NULL, 0);
	cmd_free(cmd);
}

