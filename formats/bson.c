#include "bson.h"
#include "common.h"
#include "cmd.h"

#include <string.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

static bson_t *
bson_wrap_redis_reply(const struct cmd *cmd, const redisReply *r);

static char *
bson_string_output(bson_t *b, size_t *sz);

void
bson_reply(redisAsyncContext *c, void *r, void *privdata) {

	redisReply *reply = r;
	struct cmd *cmd = privdata;
	bson_t *b;
	char *bstr = NULL;
	size_t bsz;
	(void)c;

	if(cmd == NULL) {
		/* broken connection */
		return;
	}

	if (reply == NULL) {
		evhttp_send_reply(cmd->rq, 404, "Not Found", NULL);
		return;
	}

	/* encode redis reply as BSON */
	b = bson_wrap_redis_reply(cmd, reply);

	/* get BSON as string */
	bstr = bson_string_output(b, &bsz);

	/* send reply */
	format_send_reply(cmd, bstr, bsz, "application/bson");

	/* cleanup */
	free(bstr);
	bson_free(b);
}

/**
 * Parse info message and return object.
 */
static bson_t *
bson_info_reply(const char *s, size_t sz) {
	const char *p = s;

	bson_t *broot = bson_object();

	/* TODO: handle new format */

	while(p < s + sz) {
		char *key, *val, *nl, *colon;
		size_t key_len, val_len;
		/* find key */
		colon = strchr(p, ':');
		if(!colon) {
			break;
		}
		key = calloc(colon - p + 1, 1);
		key_len = colon - p;
		memcpy(key, p, key_len);
		p = colon + 1;

		/* find value */
		nl = strchr(p, '\r');
		if(!nl) {
			free(key);
			break;
		}
		val = calloc(nl - p + 1, 1);
		val_len = nl - p;
		memcpy(val, p, val_len);
		p = nl + 1;
		if(*p == '\n') p++;

		/* add to object */
		bson_object_set_new(broot, key, key_len,
				bson_string(val, val_len));
		free(key);
		free(val);
	}

	return broot;
}


static bson_t *
bson_hgetall_reply(const redisReply *r) {
	/* zip keys and values together in a bson object */
	bson_t *broot;
	unsigned int i;

	if(r->elements % 2 != 0) {
		return NULL;
	}

	broot = bson_object();
	for(i = 0; i < r->elements; i += 2) {
		redisReply *k = r->element[i], *v = r->element[i+1];

		/* keys and values need to be strings */
		if(k->type != REDIS_REPLY_STRING || v->type != REDIS_REPLY_STRING) {
			bson_free(broot);
			return NULL;
		}
		bson_object_set_new(broot, k->str, k->len, bson_string(v->str, v->len));
	}
	return broot;
}

static bson_t *
bson_wrap_redis_reply(const struct cmd *cmd, const redisReply *r) {

	unsigned int i;
	bson_t *blist, *broot = bson_object(); /* that's what we return */

	const char *verb = cmd->argv[0];
	size_t verb_sz = cmd->argv_len[0];

	switch(r->type) {
		case REDIS_REPLY_STATUS:
		case REDIS_REPLY_ERROR:
			blist = bson_array();
			bson_array_append_new(blist,
				r->type == REDIS_REPLY_ERROR ? bson_false() : bson_true());
			bson_array_append_new(blist, bson_bin(r->str, r->len));
			bson_object_set_new(broot, verb, verb_sz, blist);
			break;

		case REDIS_REPLY_STRING:
			if(strncasecmp(verb, "INFO", verb_sz) == 0) {
				bson_object_set_new(broot, verb, verb_sz,
						bson_info_reply(r->str, r->len));
			} else {
				bson_object_set_new(broot, verb, verb_sz,
						bson_bin(r->str, r->len));
			}
			break;

		case REDIS_REPLY_INTEGER:
			bson_object_set_new(broot, verb, verb_sz, bson_integer(r->integer));
			break;

		case REDIS_REPLY_ARRAY:
			if(strcasecmp(verb, "HGETALL") == 0) {
				bson_t *bobj = bson_hgetall_reply(r);
				if(bobj) {
					bson_object_set_new(broot, verb, verb_sz, bobj);
					break;
				}
			}
			blist = bson_array();
			for(i = 0; i < r->elements; ++i) {
				redisReply *e = r->element[i];
				switch(e->type) {
					case REDIS_REPLY_STRING:
						bson_array_append_new(blist,
							bson_bin(e->str, e->len));
						break;
					case REDIS_REPLY_INTEGER:
						bson_array_append_new(blist, bson_integer(e->integer));
						break;
					default:
						bson_array_append_new(blist, bson_null());
						break;
				}
			}
			bson_object_set_new(broot, verb, verb_sz, blist);
			break;

		default:
			bson_object_set_new(broot, verb, verb_sz, bson_null());
			break;
	}

	return broot;
}

static bson_t *
bson_new() {
	return calloc(sizeof(bson_t), 1);
}

void
bson_free(bson_t *b) {

	int i;
	switch(b->type) {
		case BSON_STRING:
		case BSON_BIN:
			free(b->data.bin.s);
			break;

		case BSON_OBJECT:
		case BSON_ARRAY:
			for(i = 0; i < b->data.array.count; ++i) {
				bson_free(b->data.array.elements[i]);
			}
			free(b->data.array.elements);
			break;

		default:
			break;
	}
	free(b);
}

bson_t *
bson_array() {
	bson_t *b = bson_new();
	b->type = BSON_ARRAY;
	return b;
}

bson_t *
bson_object() {
	bson_t *b = bson_new();
	b->type = BSON_OBJECT;
	return b;
}

bson_t *
bson_null() {
	bson_t *b = bson_new();
	b->type = BSON_NULL;
	return b;
}

bson_t *
bson_true() {
	bson_t *b = bson_new();
	b->type = BSON_TRUE;
	return b;
}

bson_t *
bson_false() {
	bson_t *b = bson_new();
	b->type = BSON_FALSE;
	return b;
}

bson_t *
bson_integer(long long i) {
	bson_t *b = bson_new();
	b->type = BSON_INT;
	b->data.i = i;
	return b;
}

bson_t *
bson_bin(const char *s, size_t sz) {
	bson_t *b = bson_new();
	b->type = BSON_BIN;
	b->data.bin.s = malloc(sz);
	memcpy(b->data.bin.s, s, sz);
	b->data.bin.sz = sz;
	return b;
}

bson_t *
bson_string(const char *s, size_t sz) {
	bson_t *b = bson_new();
	b->type = BSON_STRING;
	b->data.bin.s = malloc(sz);
	memcpy(b->data.bin.s, s, sz);
	b->data.bin.sz = sz;
	return b;
}

static void
bson_array_append_raw(bson_t *b, bson_t *e) {
	if(b->type != BSON_ARRAY && b->type != BSON_OBJECT) {
		return;
	}

	b->data.array.count++;
	b->data.array.elements = realloc(b->data.array.elements,
			b->data.array.count * sizeof(bson_t*));
	b->data.array.elements[b->data.array.count-1] = e;
}

void
bson_array_append_new(bson_t *b, bson_t *e) {
	char s[20];
	int sz = sprintf(s, "%d", b->data.array.count / 2);

	bson_array_append_raw(b, bson_string(s, sz));
	bson_array_append_raw(b, e);
}

void
bson_object_set_new(bson_t *b, const char *k, size_t sz, bson_t *v) {
	bson_array_append_raw(b, bson_string(k, sz));
	bson_array_append_raw(b, v);
}

static char *
bson_string_output_object(bson_t *b, size_t *out_sz) {

	int i;
	size_t sz = 0;
	char *s = NULL;

	for(i = 0; i < b->data.array.count; i += 2) {
		char type = '\x00';
		/* key is always string. */
		const char *key = b->data.array.elements[i]->data.bin.s;
		size_t key_sz = b->data.array.elements[i]->data.bin.sz;

		/* val can be anything. */
		bson_t *bval = b->data.array.elements[i+1];

		switch(bval->type) {
			case BSON_OBJECT:
				type = '\x03';
				break;

			case BSON_TRUE:
			case BSON_FALSE:
				type = '\x08';
				break;

			case BSON_INT:
				type = '\x12';
				break;

			case BSON_STRING:
				type = '\x02';
				break;

			case BSON_BIN:
				type = '\x05';
				break;

			case BSON_ARRAY:
				type = '\x04';
				break;

			case BSON_NULL:
				type = '\x0A';
				break;
		}

		if(type) {
			size_t val_sz;
			char *val = bson_string_output(bval, &val_sz);

			s = realloc(s, sz + 1 + key_sz + 1 + val_sz);
			s[sz] = type;	/* type */
			memcpy(s + sz + 1, key, key_sz); /* key */
			s[sz + 1 + key_sz] = 0;	/* end of key */
			memcpy(s + sz + 1 + key_sz + 1, val, val_sz);

			sz += 1 + key_sz + 1 + val_sz;
			free(val);
		}
	}

	*out_sz = sz;
	return s;
}

static char *
bson_string_output(bson_t *b, size_t *sz) {

	char *s = NULL, *s_obj = NULL;
	size_t sz_obj;
	*sz = 0;
	int64_t i64;
	int32_t i32;

	switch(b->type) {
		case BSON_ARRAY:
		case BSON_OBJECT:
			s_obj = bson_string_output_object(b, &sz_obj);
			*sz = sizeof(i32) + sz_obj + 1;
			s = malloc(*sz);
			i32 = *sz;
			memcpy(s, &i32, sizeof(i32));
			memcpy(s + sizeof(i32), s_obj, sz_obj);
			s[*sz-1] = 0;
			free(s_obj);
			break;

		case BSON_TRUE:
			*sz = 1;
			s = malloc(*sz);
			*s = 1;
			break;

		case BSON_FALSE:
			*sz = 1;
			s = malloc(*sz);
			*s = 0;
			break;

		case BSON_INT:
			*sz = 8;
			i64 = b->data.i;
			s = malloc(*sz);
			memcpy(s, &i64, sizeof(i64));
			break;

		case BSON_BIN:
			i32 = b->data.bin.sz;
			*sz = sizeof(i32) + 1 + i32;
			s = malloc(*sz);
			memcpy(s, &i32, sizeof(i32));
			s[sizeof(i32)] = 0;
			memcpy(s + sizeof(i32) + 1, b->data.bin.s, b->data.bin.sz);
			break;

		case BSON_STRING:
			i32 = b->data.bin.sz + 1;
			*sz = sizeof(i32) + i32;
			s = malloc(*sz);
			memcpy(s, &i32, sizeof(i32));
			memcpy(s + sizeof(i32), b->data.bin.s, b->data.bin.sz);
			s[*sz - 1] = 0;
			break;

		case BSON_NULL:
			*sz = 0;
			return NULL;
			break;
	}

	return s;

}

