#ifndef JSON_H
#define JSON_H

#include <jansson.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

struct cmd;

void
json_reply(redisAsyncContext *c, void *r, void *privdata);


#endif
