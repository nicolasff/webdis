#ifndef CUSTOM_TYPE_H
#define CUSTOM_TYPE_H

#include <hiredis/hiredis.h>
#include <hiredis/async.h>

struct cmd;

void
custom_type_reply(redisAsyncContext *c, void *r, void *privdata);

#endif
