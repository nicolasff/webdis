#ifndef FORMATS_COMMON_H
#define FORMATS_COMMON_H

#include <stdlib.h>

struct cmd;

void
format_send_reply(struct cmd *cmd, const char *p, size_t sz, const char *content_type);

#endif
