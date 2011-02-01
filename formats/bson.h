#ifndef BSON_H
#define BSON_H

#include <jansson.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

struct cmd;

void
bson_reply(redisAsyncContext *c, void *r, void *privdata);

typedef struct bson_t {

	enum {BSON_OBJECT, BSON_TRUE, BSON_FALSE, BSON_INT,
		BSON_BIN, BSON_STRING, BSON_ARRAY, BSON_NULL} type;

	union {
		long long i;
		struct {
			char *s;
			size_t sz;
		} bin;

		struct {
			struct bson_t **elements;
			int count;
		} array;
	} data;

} bson_t;

/* BSON encoding */

void
bson_free(bson_t *b);

bson_t *
bson_array();

bson_t *
bson_object();

bson_t *
bson_null();

bson_t *
bson_true();

bson_t *
bson_false();

bson_t *
bson_integer(long long i);

bson_t *
bson_string(const char *s, size_t sz);

bson_t *
bson_bin(const char *s, size_t sz);

void
bson_array_append_new(bson_t *b, bson_t *e);

void
bson_object_set_new(bson_t *b, const char *k, size_t sz, bson_t *v);

#endif
