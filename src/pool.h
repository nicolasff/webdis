#ifndef POOL_H
#define POOL_H

#include <hiredis/async.h>

struct conf;
struct worker;

struct pool {

	struct worker *w;
	struct conf *cfg;

	const redisAsyncContext **ac;
	int count;
	int cur;

};


struct pool *
pool_new(struct worker *w, int count);

void
pool_free_context(redisAsyncContext *ac);

redisAsyncContext *
pool_connect(struct pool *p, int db_num, int attach);

const redisAsyncContext *
pool_get_context(struct pool *p);

#endif
