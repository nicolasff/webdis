#include "custom-type.h"
#include "cmd.h"
#include "common.h"
#include "http.h"

#include <string.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

static char *
custom_array(struct cmd *cmd, const redisReply *r, size_t *sz);

void
custom_type_reply(redisAsyncContext *c, void *r, void *privdata) {

	redisReply *reply = r;
	struct cmd *cmd = privdata;
	(void)c;
	char int_buffer[50];
	char *status_buf;
	int int_len;
	struct http_response *resp;
	size_t sz;
	char *array_out;


	if (reply == NULL) { /* broken Redis link */
		format_send_error(cmd, 503, "Service Unavailable");
		return;
	}

	if(cmd->mime) { /* use the given content-type, but only for strings */
		switch(reply->type) {

			case REDIS_REPLY_NIL: /* or nil values */
				format_send_error(cmd, 404, "Not found");
				return;

			case REDIS_REPLY_STRING:
				format_send_reply(cmd, reply->str, reply->len, cmd->mime);
				return;

			case REDIS_REPLY_STATUS:
			case REDIS_REPLY_ERROR:
				status_buf = calloc(1 + reply->len, 1);
				status_buf[0] = (reply->type == REDIS_REPLY_STATUS ? '+' : '-');
				memcpy(status_buf + 1, reply->str, reply->len);
				format_send_reply(cmd, status_buf, 1 + reply->len, cmd->mime);
				free(status_buf);
				return;

			case REDIS_REPLY_INTEGER:
				int_len = sprintf(int_buffer, "%lld", reply->integer);
				format_send_reply(cmd, int_buffer, int_len, cmd->mime);
				return;
			case REDIS_REPLY_ARRAY:
				array_out = custom_array(cmd, r, &sz);
				format_send_reply(cmd, array_out, sz, cmd->mime);
				free(array_out);
				return;
		}
	}

	/* couldn't make sense of what the client wanted. */
	resp = http_response_init(cmd->w, 400, "Bad Request");
	http_response_set_header(resp, "Content-Length", "0");
	http_response_set_keep_alive(resp, cmd->keep_alive);
	http_response_write(resp, cmd->fd);

	if(!cmd_is_subscribe(cmd)) {
		cmd_free(cmd);
	}
}

static char *
custom_array(struct cmd *cmd, const redisReply *r, size_t *sz) {

	unsigned int i;
	char *ret, *p;
	size_t sep_len = 0;

	if(cmd->separator)
		sep_len = strlen(cmd->separator);

	/* compute size */
	*sz = 0;
	for(i = 0; i < r->elements; ++i) {
		redisReply *e = r->element[i];
		switch(e->type) {
			case REDIS_REPLY_STRING:
				if(sep_len && i != 0)
					*sz += sep_len;
				*sz += e->len;
				break;

		}
	}

	/* allocate */
	p = ret = malloc(*sz);

	/* copy */
	for(i = 0; i < r->elements; ++i) {
		redisReply *e = r->element[i];
		switch(e->type) {
			case REDIS_REPLY_STRING:
				if(sep_len && i != 0) {
					memcpy(p, cmd->separator, sep_len);
					p += sep_len;
				}
				memcpy(p, e->str, e->len);
				p += e->len;
				break;
		}
	}

	return ret;
}
