#include "json.h"
#include "common.h"
#include "cmd.h"
#include "http.h"
#include "client.h"

#include <string.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

static json_t *
json_wrap_redis_reply(const struct cmd *cmd, const redisReply *r);

void
json_reply(redisAsyncContext *c, void *r, void *privdata) {

	redisReply *reply = r;
	struct cmd *cmd = privdata;
	json_t *j;
	char *jstr;
	(void)c;

	if(cmd == NULL) {
		/* broken connection */
		return;
	}

	if (reply == NULL) { /* broken Redis link */
		format_send_error(cmd, 503, "Service Unavailable");
		return;
	}

	/* encode redis reply as JSON */
	j = json_wrap_redis_reply(cmd, r);

	/* get JSON as string, possibly with JSONP wrapper */
	jstr = json_string_output(j, cmd->jsonp);

	/* send reply */
	format_send_reply(cmd, jstr, strlen(jstr), "application/json");

	/* cleanup */
	json_decref(j);
	free(jstr);
}

/**
 * Parse info message and return object.
 */
static json_t *
json_info_reply(const char *s) {
	const char *p = s;
	size_t sz = strlen(s);

	json_t *jroot = json_object();

	/* TODO: handle new format */

	while(p < s + sz) {
		char *key, *val, *nl, *colon;

		/* find key */
		colon = strchr(p, ':');
		if(!colon) {
			break;
		}
		key = calloc(colon - p + 1, 1);
		memcpy(key, p, colon - p);
		p = colon + 1;

		/* find value */
		nl = strchr(p, '\r');
		if(!nl) {
			free(key);
			break;
		}
		val = calloc(nl - p + 1, 1);
		memcpy(val, p, nl - p);
		p = nl + 1;
		if(*p == '\n') p++;

		/* add to object */
		json_object_set_new(jroot, key, json_string(val));
		free(key);
		free(val);
	}

	return jroot;
}

static json_t *
json_hgetall_reply(const redisReply *r) {
	/* zip keys and values together in a json object */
	json_t *jroot;
	unsigned int i;

	if(r->elements % 2 != 0) {
		return NULL;
	}

	jroot = json_object();
	for(i = 0; i < r->elements; i += 2) {
		redisReply *k = r->element[i], *v = r->element[i+1];

		/* keys and values need to be strings */
		if(k->type != REDIS_REPLY_STRING || v->type != REDIS_REPLY_STRING) {
			json_decref(jroot);
			return NULL;
		}
		json_object_set_new(jroot, k->str, json_string(v->str));
	}
	return jroot;
}

static json_t *
json_wrap_redis_reply(const struct cmd *cmd, const redisReply *r) {

	unsigned int i;
	json_t *jlist, *jroot = json_object(); /* that's what we return */


	/* copy verb, as jansson only takes a char* but not its length. */
	char *verb;
	if(cmd->count) {
		verb = calloc(cmd->argv_len[0]+1, 1);
		memcpy(verb, cmd->argv[0], cmd->argv_len[0]);
	} else {
		verb = strdup("");
	}


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
			if(strcasecmp(verb, "INFO") == 0) {
				json_object_set_new(jroot, verb, json_info_reply(r->str));
			} else {
				json_object_set_new(jroot, verb, json_string(r->str));
			}
			break;

		case REDIS_REPLY_INTEGER:
			json_object_set_new(jroot, verb, json_integer(r->integer));
			break;

		case REDIS_REPLY_ARRAY:
			if(strcasecmp(verb, "HGETALL") == 0) {
				json_t *jobj = json_hgetall_reply(r);
				if(jobj) {
					json_object_set_new(jroot, verb, jobj);
					break;
				}
			}
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
json_string_output(json_t *j, const char *jsonp) {

	char *json_reply = json_dumps(j, JSON_COMPACT);

	/* check for JSONP */
	if(jsonp) {
		size_t jsonp_len = strlen(jsonp);
		size_t json_len = strlen(json_reply);
		size_t ret_len = jsonp_len + 1 + json_len + 3;
		char *ret = calloc(1 + ret_len, 1);

		memcpy(ret, jsonp, jsonp_len);
		ret[jsonp_len]='(';
		memcpy(ret + jsonp_len + 1, json_reply, json_len);
		memcpy(ret + jsonp_len + 1 + json_len, ");\n", 3);
		free(json_reply);

		return ret;
	}

	return json_reply;
}

/* extract JSON from WebSocket frame and fill struct cmd. */
struct cmd *
json_ws_extract(struct http_client *c, const char *p, size_t sz) {

	struct cmd *cmd = NULL;
	json_t *j;
	char *jsonz; /* null-terminated */

	unsigned int i, cur;
	int argc = 0;
	json_error_t jerror;

	(void)c;

	jsonz = calloc(sz + 1, 1);
	memcpy(jsonz, p, sz);
	j = json_loads(jsonz, sz, &jerror);
	free(jsonz);

	if(!j) {
		return NULL;
	}
	if(json_typeof(j) != JSON_ARRAY) {
		json_decref(j);
		return NULL; /* invalid JSON */
	}

	/* count elements */
	for(i = 0; i < json_array_size(j); ++i) {
		json_t *jelem = json_array_get(j, i);

		switch(json_typeof(jelem)) {
			case JSON_STRING:
			case JSON_INTEGER:
				argc++;
				break;

			default:
				break;
		}
	}

	if(!argc) { /* not a single item could be decoded */
		json_decref(j);
		return NULL;
	}

	/* create command and add args */
	cmd = cmd_new(argc);
	for(i = 0, cur = 0; i < json_array_size(j); ++i) {
		json_t *jelem = json_array_get(j, i);
		char *tmp;

		switch(json_typeof(jelem)) {
			case JSON_STRING:
				tmp = strdup(json_string_value(jelem));

				cmd->argv[cur] = tmp;
				cmd->argv_len[cur] = strlen(tmp);
				cur++;
				break;

			case JSON_INTEGER:
				tmp = malloc(40);
				sprintf(tmp, "%d", (int)json_integer_value(jelem));

				cmd->argv[cur] = tmp;
				cmd->argv_len[cur] = strlen(tmp);
				cur++;
				break;

			default:
				break;
		}
	}

	json_decref(j);
	return cmd;
}

