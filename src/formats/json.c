#include "json.h"
#include "common.h"
#include "cmd.h"
#include "http.h"
#include "client.h"

#include <string.h>
#include <strings.h>
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

	if(reply == NULL) { /* broken Redis link */
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
json_expand_array(const redisReply *r);

static json_t *
json_array_to_keyvalue_reply(const redisReply *r) {
	/* zip keys and values together in a json object */
	json_t *jroot, *jlist;
	unsigned int i;

	if(r->elements % 2 != 0) {
		return NULL;
	}

	jroot = json_object();
	for(i = 0; i < r->elements; i += 2) {
		redisReply *k = r->element[i], *v = r->element[i+1];

		/* keys need to be strings */
		if(k->type != REDIS_REPLY_STRING) {
			json_decref(jroot);
			return NULL;
		}
		switch (v->type) {
		case REDIS_REPLY_NIL:
			json_object_set_new(jroot, k->str, json_null());
			break;

		case REDIS_REPLY_STRING:
			json_object_set_new(jroot, k->str, json_string(v->str));
			break;

		case REDIS_REPLY_INTEGER:
			json_object_set_new(jroot, k->str, json_integer(v->integer));
			break;

		case REDIS_REPLY_ARRAY:
			if(!(jlist = json_expand_array(v))) {
				jlist = json_null();
			}

			json_object_set_new(jroot, k->str, jlist);
			break;

		default:
			json_decref(jroot);
			return NULL;
		}

	}
	return jroot;
}

static json_t *
json_expand_array(const redisReply *r) {

	unsigned int i;
	json_t *jlist, *sublist;
	const redisReply *e;

	jlist = json_array();
	for(i = 0; i < r->elements; ++i) {
		e = r->element[i];
		switch(e->type) {
		case REDIS_REPLY_STATUS:
		case REDIS_REPLY_STRING:
			json_array_append_new(jlist, json_string(e->str));
			break;

		case REDIS_REPLY_INTEGER:
			json_array_append_new(jlist, json_integer(e->integer));
			break;

		case REDIS_REPLY_ARRAY:
			if(!(sublist = json_expand_array(e))) {
				sublist = json_null();
			}
			json_array_append_new(jlist, sublist);
			break;

		case REDIS_REPLY_NIL:
		default:
			json_array_append_new(jlist, json_null());
			break;
		}
	}
	return jlist;
}

static json_t *
json_singlestream_list(const redisReply *r) {

	unsigned int i;
	json_t *jlist, *jmsg, *jsubmsg;
	const redisReply *id, *msg;
	const redisReply *e;

	/* reply on XRANGE / XREVRANGE / XCLAIM and one substream of XREAD / XREADGROUP */
	jlist = json_array();
	for(i = 0; i < r->elements; i++) {
		e = r->element[i];
		if(e->type != REDIS_REPLY_ARRAY || e->elements < 2) {
			continue;
		}
		id = e->element[0];
		msg = e->element[1];
		if(id->type != REDIS_REPLY_STRING || id->len < 1) {
			continue;
		}
		if(msg->type != REDIS_REPLY_ARRAY || msg->elements < 2) {
			continue;
		}
		jmsg = json_object();
		json_object_set_new(jmsg, "id", json_string(id->str));
		if(!(jsubmsg = json_array_to_keyvalue_reply(msg))) {
			jsubmsg = json_null();
		}
		json_object_set_new(jmsg, "msg", jsubmsg);
		json_array_append_new(jlist, jmsg);
	}
	return jlist;
}

static json_t *
json_xreadstream_list(const redisReply *r) {

	unsigned int i;
	json_t *jobj = NULL, *jlist;
	const redisReply *sid, *msglist;
	const redisReply *e;

	/* reply on XREAD / XREADGROUP */
	if(r->elements) {
		jobj = json_object();
	}
	for(i = 0; i < r->elements; i++) {
		e = r->element[i];
		if(e->type != REDIS_REPLY_ARRAY || e->elements < 2) {
			continue;
		}
		sid = e->element[0]; msglist = e->element[1];
		if(sid->type != REDIS_REPLY_STRING || sid->len < 1) {
			continue;
		}
		if(msglist->type != REDIS_REPLY_ARRAY) {
			continue;
		}
		if(!(jlist = json_singlestream_list(msglist))) {
			jlist = json_null();
		}
		json_object_set_new(jobj, sid->str, jlist);
	}
	return jobj;
}

static json_t *
json_xpending_list(const redisReply *r) {

	unsigned int i;
	json_t *jobj, *jlist, *jown;
	const redisReply *own, *msgs;
	const redisReply *e;

	if(r->elements >= 4 && r->element[0]->type == REDIS_REPLY_INTEGER) {
		/* reply on XPENDING <key> <consumergroup> */
		jobj = json_object();
		json_object_set_new(jobj, "msgs", json_integer(r->element[0]->integer));
		if(r->element[1]->type == REDIS_REPLY_STRING) {
			json_object_set_new(jobj, "idmin", json_string(r->element[1]->str));
		}
		if(r->element[2]->type == REDIS_REPLY_STRING) {
			json_object_set_new(jobj, "idmax", json_string(r->element[2]->str));
		}
		if(r->element[3]->type != REDIS_REPLY_ARRAY) {
			return jobj;
		}
		jown = json_object();
		for(i = 0; i < r->element[3]->elements; i++) {
			e = r->element[3]->element[i];
			if(e->type != REDIS_REPLY_ARRAY || e->elements < 2) {
				continue;
			}
			own = e->element[0];
			msgs = e->element[1];
			if(own->type != REDIS_REPLY_STRING) {
				continue;
			}
			switch(msgs->type){
				case REDIS_REPLY_STRING:
					json_object_set_new(jown, own->str, json_string(msgs->str));
					break;

				case REDIS_REPLY_INTEGER:
					json_object_set_new(jown, own->str, json_integer(msgs->integer));
					break;
			}
		}
		json_object_set_new(jobj, "msgsperconsumer", jown);

		return jobj;
	}

	/* reply on XPENDING <key> <consumergroup> <minid> <maxid> <count> ... */
	jlist = json_array();
	for(i = 0; i < r->elements; i++) {
		e = r->element[i];
		if(e->type != REDIS_REPLY_ARRAY || e->elements < 4) {
			continue;
		}
		jobj = json_object();
		if(e->element[0]->type == REDIS_REPLY_STRING) {
			json_object_set_new(jobj, "id", json_string(e->element[0]->str));
		}
		if(e->element[1]->type == REDIS_REPLY_STRING) {
			json_object_set_new(jobj, "owner", json_string(e->element[1]->str));
		}
		if(e->element[2]->type == REDIS_REPLY_INTEGER) {
			json_object_set_new(jobj, "elapsedtime", json_integer(e->element[2]->integer));
		}
		if(e->element[3]->type == REDIS_REPLY_INTEGER) {
			json_object_set_new(jobj, "deliveries", json_integer(e->element[3]->integer));
		}
		json_array_append_new(jlist, jobj);
	}

	return jlist;
}

static json_t *
json_georadius_with_list(const redisReply *r) {

	unsigned int i, j;
	json_t *jobj, *jlist = NULL, *jcoo;
	const redisReply *e;

	/* reply on GEORADIUS* ... WITHCOORD | WITHDIST | WITHHASH */
	jlist = json_array();
	for(i = 0; i < r->elements; i++) {
		e = r->element[i];
		if(e->type != REDIS_REPLY_ARRAY || e->elements < 1) {
			continue;
		}
		jobj = json_object();
		json_object_set_new(jobj, "name", json_string(e->element[0]->str));
		for(j = 1; j < e->elements; j++) {
			switch(e->element[j]->type) {
				case REDIS_REPLY_INTEGER:
					json_object_set_new(jobj, "hash", json_integer(e->element[j]->integer));
					break;

				case REDIS_REPLY_STRING:
					json_object_set_new(jobj, "dist", json_string(e->element[j]->str));
					break;

				case REDIS_REPLY_ARRAY:
					if(e->element[j]->type != REDIS_REPLY_ARRAY || e->element[j]->elements != 2) {
						continue;
					}
					if(e->element[j]->element[0]->type != REDIS_REPLY_STRING || e->element[j]->element[1]->type != REDIS_REPLY_STRING) {
						continue;
					}
					jcoo = json_array();
					json_array_append_new(jcoo, json_string(e->element[j]->element[0]->str));
					json_array_append_new(jcoo, json_string(e->element[j]->element[1]->str));
					json_object_set_new(jobj, "coords", jcoo);
					break;
			}

		}
		json_array_append_new(jlist, jobj);
	}
	return jlist;
}

static json_t *
json_wrap_redis_reply(const struct cmd *cmd, const redisReply *r) {

	json_t *jlist, *jobj, *jroot = json_object(); /* that's what we return */

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
				jobj = json_array_to_keyvalue_reply(r);
				if(jobj) {
					json_object_set_new(jroot, verb, jobj);
				}
				break;
			} else if(strcasecmp(verb, "XRANGE") == 0 || strcasecmp(verb, "XREVRANGE") == 0 ||
					(strcasecmp(verb, "XCLAIM") == 0 &&  r->elements > 0 && r->element[0]->type == REDIS_REPLY_ARRAY)) {
				jobj = json_singlestream_list(r);
				if(jobj) {
					json_object_set_new(jroot, verb, jobj);
				}
				break;
			} else if(strcasecmp(verb, "XREAD") == 0 || strcasecmp(verb, "XREADGROUP") == 0) {
				jobj = json_xreadstream_list(r);
				if(jobj) {
					json_object_set_new(jroot, verb, jobj);
				}
				break;
			} else if(strcasecmp(verb, "XPENDING") == 0) {
				jobj = json_xpending_list(r);
				if(jobj) {
					json_object_set_new(jroot, verb, jobj);
				}
				break;
			} else if(strncasecmp(verb, "GEORADIUS", 9) == 0 && r->elements > 0 && r->element[0]->type == REDIS_REPLY_ARRAY) {
				jobj = json_georadius_with_list(r);
				if(jobj) {
					json_object_set_new(jroot, verb, jobj);
				}
				break;
			}

			if(!(jlist = json_expand_array(r))) {
				jlist = json_null();
			}

			json_object_set_new(jroot, verb, jlist);
			break;

		case REDIS_REPLY_NIL:
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
		ret[jsonp_len] = '(';
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
	cmd = cmd_new(c, argc);
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
