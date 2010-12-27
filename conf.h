#ifndef CONF_H
#define CONF_H

#include <stdint.h>
#include <netinet/in.h>

struct disabled_command {

	in_addr_t mask_ip;
	short mask_bits;

	char **commands;

	struct disabled_command *next;
};

struct conf {

	char *redis_host;
	short redis_port;

	char *http_host;
	short http_port;

	struct disabled_command *disabled;
};

struct conf *
conf_read(const char *filename);

void
conf_free(struct conf *conf);

#endif /* CONF_H */
