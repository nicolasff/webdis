#include "raw.h"
#include "common.h"
#include "http.h"
#include "client.h"
#include "cmd.h"

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

	if (reply == NULL) { /* broken Redis link */
		format_send_error(cmd, 503, "Service Unavailable");
		return;
	}

	raw_out = raw_wrap(r, &sz);

	/* send reply */
	format_send_reply(cmd, raw_out, sz, "binary/octet-stream");

	/* cleanup */
	free(raw_out);
}

/* extract Redis protocol string from WebSocket frame and fill struct cmd. */
struct cmd *
raw_ws_extract(struct http_client *c, const char *p, size_t sz) {

	struct cmd *cmd = NULL;
	void *reader = NULL;
	redisReply *reply = NULL;
	void **reply_ptr = (void**)&reply;
	unsigned int i;
	(void)c;

	/* create protocol reader */
	reader = redisReaderCreate();

	/* add data */
	redisReaderFeed(reader, (char*)p, sz);

	/* parse data into reply object */
	if(redisReaderGetReply(reader, reply_ptr) == REDIS_ERR) {
		goto end;
	}

	/* add data from reply object to cmd struct */
	if(reply->type != REDIS_REPLY_ARRAY) {
		goto end;
	}

	/* create cmd object */
	cmd = cmd_new(reply->elements);

	for(i = 0; i < reply->elements; ++i) {
		redisReply *ri = reply->element[i];

		switch(ri->type) {
			case REDIS_REPLY_STRING:
				cmd->argv_len[i] = ri->len;
				cmd->argv[i] = calloc(cmd->argv_len[i] + 1, 1);
				memcpy(cmd->argv[i], ri->str, ri->len);
				break;

			case REDIS_REPLY_INTEGER:
				cmd->argv_len[i] = integer_length(ri->integer);
				cmd->argv[i] = calloc(cmd->argv_len[i] + 1, 1);
				sprintf(cmd->argv[i], "%lld", ri->integer);
				break;

			default:
				cmd_free(cmd);
				cmd = NULL;
				goto end;
		}
	}


end:
	/* free reader */
	if(reader) redisReaderFree(reader);

	/* free reply */
	if(reply) freeReplyObject(reply);

	return cmd;
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
	p = ret = malloc(1+*sz);
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

