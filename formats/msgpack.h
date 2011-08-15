#ifndef MSGPACK_H
#define MSGPACK_H

#include <jansson.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

struct cmd;
struct http_client;

void
msgpack_reply(redisAsyncContext *c, void *r, void *privdata);

char *
msgpack_string_output(json_t *j, const char *jsonp);

struct cmd *
msgpack_ws_extract(struct http_client *c, const char *p, size_t sz);

#endif
