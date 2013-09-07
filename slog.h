#ifndef SLOG_H
#define SLOG_H

typedef enum {
	WEBDIS_ERROR = 0,
	WEBDIS_WARNING,
	WEBDIS_NOTICE,
	WEBDIS_INFO,
	WEBDIS_DEBUG
} log_level;

struct server;

void
slog_reload();

void
slog_init(struct server *s);

void slog(struct server *s, log_level level,
		const char *body, size_t sz);

#endif
