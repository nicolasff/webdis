#ifndef JSON_H
#define JSON_H

#include <jansson.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

struct cmd;
struct http_client;

void
json_reply(redisAsyncContext *c, void *r, void *privdata);

char *
json_string_output(json_t *j, const char *jsonp);

struct cmd *
json_ws_extract(struct http_client *c, const char *p, size_t sz);

#endif
