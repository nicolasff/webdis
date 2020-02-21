#ifndef RAW_H
#define RAW_H

#include <hiredis/hiredis.h>
#include <hiredis/async.h>

struct cmd;
struct http_client;

void
raw_reply(redisAsyncContext *c, void *r, void *privdata);

struct cmd *
raw_ws_extract(struct http_client *c, const char *p, size_t sz);

#endif
