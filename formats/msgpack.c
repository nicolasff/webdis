#include "msgpack.h"
#include "common.h"
#include "cmd.h"
#include "http.h"
#include "client.h"

#include <string.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

/* msgpack-c versions >= 1.0 changed to support the v5 msgpack spec.
 * As part of doing this, the (un)pack_raw functions were replaced with
 * more explicit (un)pack_str and (un)pack_bin.  1.2.0 introduced the
 * (un)pack_v4raw functions to retain compatibility.
 */
#if defined(MSGPACK_VERSION_MAJOR) && defined(MSGPACK_VERSION_MINOR) \
	&& MSGPACK_VERSION_MAJOR > 1 \
	|| (MSGPACK_VERSION_MAJOR == 1 && MSGPACK_VERSION_MINOR >= 2)
#define msgpack_pack_raw msgpack_pack_v4raw
#define msgpack_pack_raw_body msgpack_pack_v4raw_body
#endif

struct msg_out {
	char *p;
	size_t sz;
};

static void
msgpack_wrap_redis_reply(const struct cmd *cmd, struct msg_out *, const redisReply *r);

void
msgpack_reply(redisAsyncContext *c, void *r, void *privdata) {

	redisReply *reply = r;
	struct cmd *cmd = privdata;
	struct msg_out out;
	(void)c;

	if(cmd == NULL) {
		/* broken connection */
		return;
	}

	if (reply == NULL) { /* broken Redis link */
		format_send_error(cmd, 503, "Service Unavailable");
		return;
	}

	/* prepare data structure for output */
	out.p = NULL;
	out.sz = 0;

	/* encode redis reply */
	msgpack_wrap_redis_reply(cmd, &out, r);

	/* send reply */
	format_send_reply(cmd, out.p, out.sz, "application/x-msgpack");

	/* cleanup */
	free(out.p);
}

static int
on_msgpack_write(void *data, const char *s, unsigned int sz) {

	struct msg_out *out = data;

	out->p = realloc(out->p, out->sz + sz);
	memcpy(out->p + out->sz, s, sz);
	out->sz += sz;

	return sz;
}

/**
 * Parse info message and return object.
 */
void
msg_info_reply(msgpack_packer* pk, const char *s, size_t sz) {

	const char *p = s;
	unsigned int count = 0;

	/* TODO: handle new format */

	/* count number of lines */
	while(p < s + sz) {
		p = strchr(p, '\r');
		if(!p) break;

		p++;
		count++;
	}

	/* create msgpack object */
	msgpack_pack_map(pk, count);

	p = s;
	while(p < s + sz) {
		char *key, *val, *nl, *colon;
		size_t key_sz, val_sz;

		/* find key */
		colon = strchr(p, ':');
		if(!colon) {
			break;
		}
		key_sz = colon - p;
		key = calloc(key_sz + 1, 1);
		memcpy(key, p, key_sz);
		p = colon + 1;

		/* find value */
		nl = strchr(p, '\r');
		if(!nl) {
			free(key);
			break;
		}
		val_sz = nl - p;
		val = calloc(val_sz + 1, 1);
		memcpy(val, p, val_sz);
		p = nl + 1;
		if(*p == '\n') p++;

		/* add to object */
		msgpack_pack_raw(pk, key_sz);
		msgpack_pack_raw_body(pk, key, key_sz);
		msgpack_pack_raw(pk, val_sz);
		msgpack_pack_raw_body(pk, val, val_sz);

		free(key);
		free(val);
	}
}

static void
msg_hgetall_reply(msgpack_packer* pk, const redisReply *r) {

	/* zip keys and values together in a msgpack object */

	unsigned int i;

	if(r->elements % 2 != 0) {
		return;
	}

	msgpack_pack_map(pk, r->elements / 2);
	for(i = 0; i < r->elements; i += 2) {
		redisReply *k = r->element[i], *v = r->element[i+1];

		/* keys and values need to be strings */
		if(k->type != REDIS_REPLY_STRING || v->type != REDIS_REPLY_STRING) {
			return;
		}

		/* key */
		msgpack_pack_raw(pk, k->len);
		msgpack_pack_raw_body(pk, k->str, k->len);

		/* value */
		msgpack_pack_raw(pk, v->len);
		msgpack_pack_raw_body(pk, v->str, v->len);
	}
}

static void
msgpack_wrap_redis_reply(const struct cmd *cmd, struct msg_out *out, const redisReply *r) {

	unsigned int i;
	msgpack_packer* pk = msgpack_packer_new(out, on_msgpack_write);

	/* copy verb, as jansson only takes a char* but not its length. */
	char *verb = "";
	size_t verb_sz = 0;
	if(cmd->count) {
		verb_sz = cmd->argv_len[0];
		verb = cmd->argv[0];
	}

	/* Create map object */
	msgpack_pack_map(pk, 1);

	/* The single element is the verb */
	msgpack_pack_raw(pk, verb_sz);
	msgpack_pack_raw_body(pk, verb, verb_sz);

	switch(r->type) {
		case REDIS_REPLY_STATUS:
		case REDIS_REPLY_ERROR:
			msgpack_pack_array(pk, 2);

			/* first element: book */
			if(r->type == REDIS_REPLY_ERROR)
				msgpack_pack_false(pk);
			else
				msgpack_pack_true(pk);

			/* second element: message */
			msgpack_pack_raw(pk, r->len);
			msgpack_pack_raw_body(pk, r->str, r->len);
			break;

		case REDIS_REPLY_STRING:
			if(verb_sz ==4 && strncasecmp(verb, "INFO", 4) == 0) {
				msg_info_reply(pk, r->str, r->len);
			} else {
				msgpack_pack_raw(pk, r->len);
				msgpack_pack_raw_body(pk, r->str, r->len);
			}
			break;

		case REDIS_REPLY_INTEGER:
			msgpack_pack_int(pk, r->integer);
			break;

		case REDIS_REPLY_ARRAY:
			if(verb_sz == 7 && strncasecmp(verb, "HGETALL", 7) == 0) {
				msg_hgetall_reply(pk, r);
				break;
			}

			msgpack_pack_array(pk, r->elements);

			for(i = 0; i < r->elements; ++i) {
				redisReply *e = r->element[i];
				switch(e->type) {
					case REDIS_REPLY_STRING:
						msgpack_pack_raw(pk, e->len);
						msgpack_pack_raw_body(pk, e->str, e->len);
						break;
					case REDIS_REPLY_INTEGER:
						msgpack_pack_int(pk, e->integer);
						break;
					default:
						msgpack_pack_nil(pk);
						break;
				}
			}

			break;

		default:
			msgpack_pack_nil(pk);
			break;
	}

	msgpack_packer_free(pk);
}
