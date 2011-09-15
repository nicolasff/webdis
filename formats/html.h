#ifndef HTML_H
#define HTML_H

#include <jansson.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>

struct cmd;
struct http_client;

void
html_reply(redisAsyncContext *c, void *r, void *privdata);

#endif
