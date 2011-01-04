#ifndef CONF_H
#define CONF_H

#define WEBDIS_VERBOSE 0
#define WEBDIS_QUIET 1
#define WEBDIS_SILENT 2

#include <sys/types.h>

typedef enum {
  WARNING = 0
} log_level;

struct conf {

	/* connection to Redis */
	char *redis_host;
	short redis_port;
	char *redis_auth;

	/* HTTP server interface */
	char *http_host;
	short http_port;

	/* ACL */
	struct acl *perms;

	/* user/group */
	uid_t user;
	gid_t group;

	/* Logging */
	char *logfile;
	log_level verbosity;
};

struct conf *
conf_read(const char *filename);

void
conf_free(struct conf *conf);

#endif /* CONF_H */
