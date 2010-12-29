#include "json.h"
#include "cmd.h"

#include <string.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <event.h>
#include <evhttp.h>

extern int __redisPushCallback(redisCallbackList *list, redisCallback *source);

static json_t *
json_wrap_redis_reply(const struct cmd *cmd, const redisReply *r);

void
json_reply(redisAsyncContext *c, void *r, void *privdata) {

	struct evbuffer *body;
	redisReply *reply = r;
	struct cmd *cmd = privdata;
	json_t *j;
	char *jstr;
	int free_cmd = 1;

	if(cmd == NULL) {
		/* broken connection */
		return;
	}

	if (reply == NULL) {
		evhttp_send_reply(cmd->rq, 404, "Not Found", NULL);
		return;
	}

	/* encode redis reply as JSON */
	j = json_wrap_redis_reply(cmd, r);

	/* get JSON as string, possibly with JSONP wrapper */
	jstr = json_string_output(j, cmd);

	/* send reply */
	body = evbuffer_new();
	evbuffer_add(body, jstr, strlen(jstr));
	evhttp_add_header(cmd->rq->output_headers, "Content-Type", "application/json");

	if(strncasecmp(cmd->argv[0], "SUBSCRIBE", cmd->argv_len[0]) == 0) {
		redisCallback cb;
		free_cmd = 0;

		/* reinstall callback */
		cb.fn = json_reply;
		cb.privdata = privdata;
		__redisPushCallback(&c->replies, &cb);

		/* start streaming */
		if(cmd->replied == 0) {
			cmd->replied = 1;
			evhttp_send_reply_start(cmd->rq, 200, "OK");
		}
		evhttp_send_reply_chunk(cmd->rq, body);

	} else {
		evhttp_send_reply(cmd->rq, 200, "OK", body);
	}

	/* cleanup */
	evbuffer_free(body);
	json_decref(j);
	free(jstr);
	evhttp_clear_headers(&cmd->uri_params);
	if(free_cmd) {
		cmd_free(cmd);
	}
	freeReplyObject(r);
}

static json_t *
json_wrap_redis_reply(const struct cmd *cmd, const redisReply *r) {

	unsigned int i;
	json_t *jlist, *jroot = json_object(); /* that's what we return */


	/* copy verb, as jansson only takes a char* but not its length. */
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
			json_object_set_new(jroot, verb, jlist);
			break;

		case REDIS_REPLY_STRING:
			json_object_set_new(jroot, verb, json_string(r->str));
			break;

		case REDIS_REPLY_INTEGER:
			json_object_set_new(jroot, verb, json_integer(r->integer));
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
			json_object_set_new(jroot, verb, jlist);
			break;

		default:
			json_object_set_new(jroot, verb, json_null());
			break;
	}

	free(verb);
	return jroot;
}


char *
json_string_output(json_t *j, struct cmd *cmd) {
	struct evkeyval *kv;

	char *json_reply = json_dumps(j, JSON_COMPACT);

	/* check for JSONP */
	TAILQ_FOREACH(kv, &cmd->uri_params, next) {
		if(strcmp(kv->key, "jsonp") == 0) {
			size_t json_len = strlen(json_reply);
			size_t val_len = strlen(kv->value);
			size_t ret_len = val_len + 1 + json_len + 1;
			char *ret = calloc(1 + ret_len, 1);

			memcpy(ret, kv->value, val_len);
			ret[val_len]='(';
			memcpy(ret + val_len + 1, json_reply, json_len);
			ret[val_len + 1 + json_len] = ')';
			free(json_reply);

			return ret;
		}
	}


	return json_reply;
}

