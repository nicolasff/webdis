#ifndef FORMATS_COMMON_H
#define FORMATS_COMMON_H

#include <stdlib.h>

struct http_client;

void
format_send_reply(struct http_client *client,
		const char *p, size_t sz,
		const char *content_type);

#endif
