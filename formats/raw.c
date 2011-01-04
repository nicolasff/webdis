#include "raw.h"
#include "cmd.h"
#include "common.h"

#include <string.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

static char *
raw_wrap(const redisReply *r, size_t *sz);

void
raw_reply(redisAsyncContext *c, void *r, void *privdata) {

	redisReply *reply = r;
	struct cmd *cmd = privdata;
	char *raw_out;
	size_t sz;
	(void)c;

	evhttp_clear_headers(&cmd->uri_params);

	if (reply == NULL) {
		evhttp_send_reply(cmd->rq, 404, "Not Found", NULL);
		return;
	}

	raw_out = raw_wrap(r, &sz);

	/* send reply */
	format_send_reply(cmd, raw_out, sz, "binary/octet-stream");

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
	*sz += 1 + integer_length(r->elements) + 1;
	for(i = 0; i < r->elements; ++i) {
		redisReply *e = r->element[i];
		switch(e->type) {
			case REDIS_REPLY_STRING:
				*sz += 1 + integer_length(e->len) + 1
					+ e->len + 1;
				break;
			case REDIS_REPLY_INTEGER:
				*sz += 1 + integer_length(integer_length(e->integer)) + 1
					+ integer_length(e->integer) + 1;
				break;

		}
	}

	/* allocate */
	p = ret = malloc(*sz);
	p += sprintf(p, "*%zd\n", r->elements);

	/* copy */
	for(i = 0; i < r->elements; ++i) {
		redisReply *e = r->element[i];
		switch(e->type) {
			case REDIS_REPLY_STRING:
				p += sprintf(p, "$%d\n", e->len);
				memcpy(p, e->str, e->len);
				p += e->len;
				*p = '\n';
				p++;
				break;
			case REDIS_REPLY_INTEGER:
				p += sprintf(p, "$%d\n%lld\n",
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
			*sz = 2 + r->len;
			ret = malloc(*sz);
			ret[0] = (r->type == REDIS_REPLY_STATUS?'+':'-');
			memcpy(ret+1, r->str, *sz-2);
			memcpy(ret+*sz - 1, "\n", 1);
			return ret;

		case REDIS_REPLY_STRING:
			*sz = 1 + integer_length(r->len) + 1 + r->len + 1;
			p = ret = malloc(*sz);
			p += sprintf(p, "$%d\n", r->len);
			memcpy(p, r->str, *sz - 1 - (p-ret));
			memcpy(ret + *sz - 1, "\n", 1);
			return ret;

		case REDIS_REPLY_INTEGER:
			*sz = 2 + integer_length(r->integer);
			ret = malloc(3+*sz);
			sprintf(ret, ":%lld\n", r->integer);
			return ret;

		case REDIS_REPLY_ARRAY:
			return raw_array(r, sz);

		default:
			*sz = 4;
			ret = malloc(*sz);
			memcpy(ret, "$-1\n", 4);
			return ret;
	}
}

