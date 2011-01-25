#include "custom-type.h"
#include "cmd.h"
#include "common.h"
#include "http.h"
#include "client.h"

#include <string.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

void
custom_type_reply(redisAsyncContext *c, void *r, void *privdata) {

	redisReply *reply = r;
	struct http_client *client = privdata;
	(void)c;
	char int_buffer[50];
	int int_len;

	if(reply == NULL) {
		return;
	}

	if(client->cmd->mime) { /* use the given content-type, but only for strings */
		switch(reply->type) {

			case REDIS_REPLY_NIL: /* or nil values */
				format_send_reply(client, "", 0, client->cmd->mime);
				return;

			case REDIS_REPLY_STRING:
				format_send_reply(client, reply->str, reply->len, client->cmd->mime);
				return;

			case REDIS_REPLY_INTEGER:
				int_len = sprintf(int_buffer, "%lld", reply->integer);
				format_send_reply(client, int_buffer, int_len, client->cmd->mime);
				return;
		}
	}

	/* couldn't make sense of what the client wanted. */
	http_send_reply(client, 400, "Bad request", NULL, 0);
	cmd_free(client->cmd);
}

