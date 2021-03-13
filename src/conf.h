#ifndef CONF_H
#define CONF_H

#include <sys/types.h>
#include "slog.h"

struct auth {
	/* 1 if only password is used, 0 for username + password */
	int use_legacy_auth;
	char *username;
	char *password;
};

struct conf {

	/* connection to Redis */
	char *redis_host;
	int redis_port;
	struct auth *redis_auth;

	/* HTTP server interface */
	char *http_host;
	int http_port;
	int http_threads;
	size_t http_max_request_size;

	/* pool size, one pool per worker thread */
	int pool_size_per_thread;

	/* daemonize process, off by default */
	int daemonize;
	char *pidfile;

	/* WebSocket support, off by default */
	int websockets;

	/* database number */
	int database;

	/* ACL */
	struct acl *perms;

	/* user/group */
	uid_t user;
	gid_t group;

	/* Logging */
	char *logfile;
	log_level verbosity;
	struct {
		log_fsync_mode mode;
		int period_millis; /* only used with LOG_FSYNC_MILLIS */
	} log_fsync;

	/* Request to serve on “/” */
	char *default_root;
};

struct conf *
conf_read(const char *filename);

void
conf_free(struct conf *conf);

#endif /* CONF_H */
