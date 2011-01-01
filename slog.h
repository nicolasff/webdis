#ifndef SLOG_H
#define SLOG_H

#include "conf.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

void
slog(const char *logfile, int level, const char *body);

#endif
