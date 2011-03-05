#include "raw.h"
#include "common.h"
#include "http.h"

#include <string.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

static char *
raw_wrap(const redisReply *r, size_t *sz);

void
raw_reply(redisAsyncContext *c, void *r, void *privdata) {

	redisReply *reply = r;
	struct http_client *client = privdata;
	char *raw_out;
	size_t sz;
	(void)c;

	if (reply == NULL) {
		return;
	}

	raw_out = raw_wrap(r, &sz);

	/* send reply */
	format_send_reply(client, raw_out, sz, "binary/octet-stream");

	/* cleanup */
	free(raw_out);
}

static int
integer_length(long long int i) {
	int sz = 0;
	int ci = abs(i);
	while (ci > 0) {
		ci = (ci/10);
		sz += 1;
	}
	if(i == 0) { /* log 0 doesn't make sense. */
		sz = 1;
	} else if(i < 0) { /* allow for neg sign as well. */
		sz++;
	}
	return sz;
}

static char *
raw_array(const redisReply *r, size_t *sz) {
	
	unsigned int i;
	char *ret, *p;

	/* compute size */
	*sz = 0;
	*sz += 1 + integer_length(r->elements) + 2;
	for(i = 0; i < r->elements; ++i) {
		redisReply *e = r->element[i];
		switch(e->type) {
			case REDIS_REPLY_STRING:
				*sz += 1 + integer_length(e->len) + 2
					+ e->len + 2;
				break;
			case REDIS_REPLY_INTEGER:
				*sz += 1 + integer_length(integer_length(e->integer)) + 2
					+ integer_length(e->integer) + 2;
				break;

		}
	}

	/* allocate */
	p = ret = malloc(*sz);
	p += sprintf(p, "*%zd\r\n", r->elements);

	/* copy */
	for(i = 0; i < r->elements; ++i) {
		redisReply *e = r->element[i];
		switch(e->type) {
			case REDIS_REPLY_STRING:
				p += sprintf(p, "$%d\r\n", e->len);
				memcpy(p, e->str, e->len);
				p += e->len;
				*p = '\r';
				p++;
				*p = '\n';
				p++;
				break;
			case REDIS_REPLY_INTEGER:
				p += sprintf(p, "$%d\r\n%lld\r\n",
					integer_length(e->integer), e->integer);
				break;
		}
	}

	return ret;
}

static char *
raw_wrap(const redisReply *r, size_t *sz) {

	char *ret, *p;

	switch(r->type) {
		case REDIS_REPLY_STATUS:
		case REDIS_REPLY_ERROR:
			*sz = 3 + r->len;
			ret = malloc(*sz);
			ret[0] = (r->type == REDIS_REPLY_STATUS?'+':'-');
			memcpy(ret+1, r->str, *sz-3);
			memcpy(ret+*sz - 2, "\r\n", 2);
			return ret;

		case REDIS_REPLY_STRING:
			*sz = 1 + integer_length(r->len) + 2 + r->len + 2;
			p = ret = malloc(*sz);
			p += sprintf(p, "$%d\r\n", r->len);
			memcpy(p, r->str, *sz - 2 - (p-ret));
			memcpy(ret + *sz - 2, "\r\n", 2);
			return ret;

		case REDIS_REPLY_INTEGER:
			*sz = 3 + integer_length(r->integer);
			ret = malloc(4+*sz);
			sprintf(ret, ":%lld\r\n", r->integer);
			return ret;

		case REDIS_REPLY_ARRAY:
			return raw_array(r, sz);

		default:
			*sz = 5;
			ret = malloc(*sz);
			memcpy(ret, "$-1\r\n", 5);
			return ret;
	}
}

