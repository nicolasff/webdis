#include "cmd.h"
#include "server.h"
#include "conf.h"
#include "acl.h"
#include "client.h"

#include "formats/json.h"
#include "formats/raw.h"
#include "formats/custom-type.h"

#include <stdlib.h>
#include <string.h>
#include <hiredis/hiredis.h>
#include <ctype.h>

struct cmd *
cmd_new(struct http_client *client, int count) {

	struct cmd *c = calloc(1, sizeof(struct cmd));

	c->client = client;
	c->count = count;

	c->argv = calloc(count, sizeof(char*));
	c->argv_len = calloc(count, sizeof(size_t));

	return c;
}


void
cmd_free(struct cmd *c) {

	free(c->argv);
	free(c->argv_len);

	if(c->mime_free) free(c->mime);

	free(c);
}

/* taken from libevent */
static char *
decode_uri(const char *uri, size_t length, size_t *out_len, int always_decode_plus) {
	char c;
	size_t i, j;
	int in_query = always_decode_plus;

	char *ret = malloc(length);

	for (i = j = 0; i < length; i++) {
		c = uri[i];
		if (c == '?') {
			in_query = 1;
		} else if (c == '+' && in_query) {
			c = ' ';
		} else if (c == '%' && isxdigit((unsigned char)uri[i+1]) &&
		    isxdigit((unsigned char)uri[i+2])) {
			char tmp[] = { uri[i+1], uri[i+2], '\0' };
			c = (char)strtol(tmp, NULL, 16);
			i += 2;
		}
		ret[j++] = c;
	}
	*out_len = (size_t)j;

	return ret;
}


int
cmd_run(struct server *s, struct http_client *client,
		const char *uri, size_t uri_len,
		const char *body, size_t body_len) {

	char *qmark = memchr(uri, '?', uri_len);
	char *slash;
	const char *p;
	int cmd_len;
	int param_count = 0, cur_param = 1, i;

	struct cmd *cmd;

	formatting_fun f_format;

	/* count arguments */
	if(qmark) {
		uri_len = qmark - uri;
	}
	for(p = uri; p && p < uri + uri_len; param_count++) {
		p = memchr(p+1, '/', uri_len - (p+1-uri));
	}

	if(body && body_len) { /* PUT request */
		param_count++;
	}

	cmd = cmd_new(client, param_count);

	/* get output formatting function */
	uri_len = cmd_select_format(client, cmd, uri, uri_len, &f_format);

	/* check if we only have one command or more. */
	slash = memchr(uri, '/', uri_len);
	if(slash) {
		cmd_len = slash - uri;
	} else {
		cmd_len = uri_len;
	}

	/* there is always a first parameter, it's the command name */
	cmd->argv[0] = uri;
	cmd->argv_len[0] = cmd_len;


	/* check that the client is able to run this command */
	if(!acl_allow_command(cmd, s->cfg, client)) {
		return -1;
	}

	/* check if we have to split the connection */
	if(cmd_is_subscribe(cmd)) {

		client->sub = malloc(sizeof(struct subscription));
		client->sub->s = s = server_copy(s);
		client->sub->cmd = cmd;
	}

	/* no args (e.g. INFO command) */
	if(!slash) {
		redisAsyncCommandArgv(s->ac, f_format, cmd, 1, cmd->argv, cmd->argv_len);
		return 0;
	}
	p = slash + 1;
	while(p < uri + uri_len) {

		const char *arg = p;
		int arg_len;
		char *next = memchr(arg, '/', uri_len - (arg-uri));
		if(!next || next > uri + uri_len) { /* last argument */
			p = uri + uri_len;
			arg_len = p - arg;
		} else { /* found a slash */
			arg_len = next - arg;
			p = next + 1;
		}

		/* record argument */
		cmd->argv[cur_param] = decode_uri(arg, arg_len, &cmd->argv_len[cur_param], 1);
		cur_param++;
	}

	if(body && body_len) { /* PUT request */
		cmd->argv[cur_param] = body;
		cmd->argv_len[cur_param] = body_len;
	}

	/* push command to Redis. */
	redisAsyncCommandArgv(s->ac, f_format, cmd, cmd->count, cmd->argv, cmd->argv_len);

	for(i = 1; i < cur_param; ++i) {
		free((char*)cmd->argv[i]);
	}

	return 0;
}


/**
 * Select Content-Type and processing function.
 */
int
cmd_select_format(struct http_client *client, struct cmd *cmd,
		const char *uri, size_t uri_len, formatting_fun *f_format) {

	const char *ext;
	int ext_len = -1;
	unsigned int i;

	/* those are the available reply formats */
	struct reply_format {
		const char *s;
		size_t sz;
		formatting_fun f;
		const char *ct;
	};
	struct reply_format funs[] = {
		{.s = "json", .sz = 4, .f = json_reply, .ct = "application/json"},
		{.s = "raw", .sz = 3, .f = raw_reply, .ct = "binary/octet-stream"},

		{.s = "bin", .sz = 3, .f = custom_type_reply, .ct = "binary/octet-stream"},
		{.s = "txt", .sz = 3, .f = custom_type_reply, .ct = "text/plain"},
		{.s = "html", .sz = 4, .f = custom_type_reply, .ct = "text/html"},
		{.s = "xhtml", .sz = 5, .f = custom_type_reply, .ct = "application/xhtml+xml"},
		{.s = "xml", .sz = 3, .f = custom_type_reply, .ct = "text/xml"},

		{.s = "png", .sz = 3, .f = custom_type_reply, .ct = "image/png"},
		{.s = "jpg", .sz = 3, .f = custom_type_reply, .ct = "image/jpeg"},
		{.s = "jpeg", .sz = 4, .f = custom_type_reply, .ct = "image/jpeg"},
	};

	/* default */
	*f_format = json_reply;

	/* find extension */
	for(ext = uri + uri_len - 1; ext != uri && *ext != '/'; --ext) {
		if(*ext == '.') {
			ext++;
			ext_len = uri + uri_len - ext;
			break;
		}
	}
	if(!ext_len) return uri_len; /* nothing found */

	/* find function for the given extension */
	for(i = 0; i < sizeof(funs)/sizeof(funs[0]); ++i) {
		if(ext_len == (int)funs[i].sz && strncmp(ext, funs[i].s, ext_len) == 0) {

			if(cmd->mime_free) free(cmd->mime);
			cmd->mime = (char*)funs[i].ct;
			cmd->mime_free = 0;

			*f_format = funs[i].f;
		}
	}

	/* the user can force it with ?type=some/thing */
	if(client->query_string.type.s) {
		*f_format = custom_type_reply;
		cmd->mime = strdup(client->query_string.type.s);
		cmd->mime_free = 1;
	}

	return uri_len - ext_len - 1;
}

int
cmd_is_subscribe(struct cmd *cmd) {

	/*
	if(cmd->started_responding) {
		return 1;
	}
	*/

	if(cmd->count >= 1 && 
		(strncasecmp(cmd->argv[0], "SUBSCRIBE", cmd->argv_len[0]) == 0 ||
		strncasecmp(cmd->argv[0], "PSUBSCRIBE", cmd->argv_len[0]) == 0)) {
		return 1;
	}
	return 0;
}
