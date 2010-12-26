#ifndef RAW_H
#define RAW_H

#include <hiredis/hiredis.h>
#include <hiredis/async.h>

struct cmd;

void
raw_reply(redisAsyncContext *c, void *r, void *privdata);


#endif
