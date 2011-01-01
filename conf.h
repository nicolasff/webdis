#ifndef CONF_H
#define CONF_H

#include <sys/types.h>

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

        /* Logfile */
        char *logfile;
};

struct conf *
conf_read(const char *filename);

void
conf_free(struct conf *conf);

#endif /* CONF_H */
