#include "json.h"
#include "cmd.h"

#include <string.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <event.h>
#include <evhttp.h>

static json_t *
json_encode(const struct cmd *cmd, const redisReply *r);

void
json_reply(redisAsyncContext *c, void *r, void *privdata) {

	(void)c;
	struct evbuffer *body;
	redisReply *reply = r;
	struct cmd *cmd = privdata;
	json_t *j;
	char *json_reply;

	if (reply == NULL) {
		evhttp_send_reply(cmd->rq, 404, "Not Found", NULL);
		return;
	}

	j = json_encode(cmd, r);

	/* get JSON as string */
	json_reply = json_dumps(j, JSON_COMPACT);

	/* reply */
	body = evbuffer_new();
	evbuffer_add(body, json_reply, strlen(json_reply));
	evhttp_add_header(cmd->rq->output_headers, "Content-Type", "application/json");
	evhttp_send_reply(cmd->rq, 200, "OK", body);
	evbuffer_free(body);

	/* cleanup */
	json_decref(j);
	freeReplyObject(r);
	cmd_free(cmd);
	free(json_reply);


}

json_t *
json_encode(const struct cmd *cmd, const redisReply *r) {

	unsigned int i;
	json_t *jlist, *jroot = json_object(); /* that's what we return */


	/* copy verb */
	char *verb;
	verb = calloc(cmd->argv_len[0]+1, 1);
	memcpy(verb, cmd->argv[0], cmd->argv_len[0]);

	switch(r->type) {
		case REDIS_REPLY_STATUS:
		case REDIS_REPLY_ERROR:

			jlist = json_array();
			json_array_append_new(jlist,
				r->type == REDIS_REPLY_ERROR ? json_false() : json_true());
			json_array_append_new(jlist, json_string(r->str));
			json_object_set(jroot, verb, jlist);
			break;

		case REDIS_REPLY_STRING:
			json_object_set(jroot, verb, json_string(r->str));
			break;

		case REDIS_REPLY_INTEGER:
			json_object_set(jroot, verb, json_integer(r->integer));
			break;

		case REDIS_REPLY_ARRAY:
			jlist = json_array();
			for(i = 0; i < r->elements; ++i) {
				redisReply *e = r->element[i];
				switch(e->type) {
					case REDIS_REPLY_STRING:
						json_array_append_new(jlist, json_string(e->str));
						break;
					case REDIS_REPLY_INTEGER:
						json_array_append_new(jlist, json_integer(e->integer));
						break;
					default:
						json_array_append_new(jlist, json_null());
						break;
				}
			}
			json_object_set(jroot, verb, jlist);
			break;

		default:
			json_object_set(jroot, verb, json_null());
			break;
	}

	free(verb);
	return jroot;
}

