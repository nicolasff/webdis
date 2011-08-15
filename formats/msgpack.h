#ifndef MSGPACK_H
#define MSGPACK_H

#include <msgpack.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

void
msgpack_reply(redisAsyncContext *c, void *r, void *privdata);

#endif
