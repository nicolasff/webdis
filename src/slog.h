#ifndef SLOG_H
#define SLOG_H

typedef enum {
	WEBDIS_ERROR = 0,
	WEBDIS_WARNING,
	WEBDIS_NOTICE,
	WEBDIS_INFO,
	WEBDIS_DEBUG,
	WEBDIS_TRACE
} log_level;

typedef enum {
	LOG_FSYNC_AUTO = 0,
	LOG_FSYNC_MILLIS,
	LOG_FSYNC_ALL
} log_fsync_mode;

struct server;

void
slog_reload();

void
slog_init(struct server *s);

void
slog_fsync_init(struct server *s);

int
slog_enabled(struct server *s, log_level level);

void slog(struct server *s, log_level level,
		const char *body, size_t sz);

#endif
