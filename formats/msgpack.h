#ifndef MSGPACK_H
#define MSGPACK_H

#include <jansson.h>
#include <msgpack.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

struct cmd;
struct http_client;

void
msgpack_reply(redisAsyncContext *c, void *r, void *privdata);

#endif
