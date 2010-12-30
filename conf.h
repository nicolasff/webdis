#ifndef CONF_H
#define CONF_H

#include <netinet/in.h>

struct acl_commands {
	unsigned int count;
	char **commands;
};

struct acl {

	/* CIDR subnet + mask */
	struct {
		int enabled;
		in_addr_t subnet;
		in_addr_t mask;
	} cidr;

	char *http_basic_auth;

	/* commands that have been enabled or disabled */
	struct acl_commands enable;
	struct acl_commands disable;

	struct acl *next;
};

struct conf {

	char *redis_host;
	short redis_port;
	char *redis_auth;

	char *http_host;
	short http_port;

	struct acl *perms;
};

struct conf *
conf_read(const char *filename);

void
conf_free(struct conf *conf);

#endif /* CONF_H */
