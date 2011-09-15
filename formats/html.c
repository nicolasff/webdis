#include "json.h"
#include "common.h"
#include "cmd.h"
#include "http.h"
#include "client.h"
#include "worker.h"
#include "server.h"
#include "conf.h"

#include <string.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

static char *
html_wrap_script(struct cmd *cmd, const char *s, size_t *sz);

void
html_reply(redisAsyncContext *c, void *r, void *privdata) {

	redisReply *reply = r;
	struct cmd *cmd = privdata;
	json_t *j;
	char *jstr, *script;
	size_t script_sz;
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

	script = html_wrap_script(cmd, jstr, &script_sz);

	/* send reply */
	format_send_reply(cmd, script, script_sz, "text/html");

	/* cleanup */
	json_decref(j);
	free(jstr);
	free(script);
}

static char *
html_wrap_script(struct cmd *cmd, const char *s, size_t *out_sz) {

	char *ret, *p, *body;
	size_t sz = strlen(s), body_sz, zeroes = 4096;
	char before[] = "<script type=\"text/javascript\">", after[]="</script>";

	/* compute size */
	*out_sz = sz + sizeof(before)-1 + sizeof(after)-1 + zeroes;
	if(!cmd->started_responding) { /* add body first */
		body = cmd->w->s->cfg->html_body;
		body_sz = cmd->w->s->cfg->html_body_sz;
		*out_sz += body_sz;
	}

	/* allocate and copy */
	p = ret = malloc(*out_sz);
	if(!cmd->started_responding) { /* add body */
		memcpy(p, body, body_sz);
		p += body_sz;
	}

	/* add script tag */
	memcpy(p, before, sizeof(before)-1);
	memcpy(p + sizeof(before)-1, s, sz);
	memcpy(p + sizeof(before)-1 + sz, after, sizeof(after)-1);

	/* flush */
	memset(ret + *out_sz - zeroes, '\n', zeroes);

	return ret;
}
