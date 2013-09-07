#include "acl.h"
#include "cmd.h"
#include "conf.h"
#include "http.h"
#include "client.h"

#include <string.h>
#include <evhttp.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int
acl_match_client(struct acl *a, struct http_client *client, in_addr_t *ip) {

	/* check HTTP Basic Auth */
	const char *auth;
	auth = client_get_header(client, "Authorization");
	if(a->http_basic_auth) {
		if(auth && strncasecmp(auth, "Basic ", 6) == 0) { /* sent auth */
			if(strcmp(auth + 6, a->http_basic_auth) != 0) { /* bad password */
				return 0;
			}
		} else { /* no auth sent, required to match this ACL */
			return 0;
		}
	}

	/* CIDR check. */
	if(a->cidr.enabled == 0) { /* none given, all match */
		return 1;
	}
	if(((*ip) & a->cidr.mask) == (a->cidr.subnet & a->cidr.mask)) {
		return 1;
	}

	return 0;
}

int
acl_allow_command(struct cmd *cmd, struct conf *cfg, struct http_client *client) {

	char *always_off[] = {"MULTI", "EXEC", "WATCH", "DISCARD", "SELECT"};

	unsigned int i;
	int authorized = 1;
	struct acl *a;

	in_addr_t client_addr;

	const char *cmd_name;
	size_t cmd_len;

	if(cmd->count == 0) {
		return 0;
	}

	cmd_name = cmd->argv[0];
	cmd_len = cmd->argv_len[0];

	/* some commands are always disabled, regardless of the config file. */
	for(i = 0; i < sizeof(always_off) / sizeof(always_off[0]); ++i) {
		if(strncasecmp(always_off[i], cmd_name, cmd_len) == 0) {
			return 0;
		}
	}

	/* find client's address */
	client_addr = ntohl(client->addr);

	/* go through permissions */
	for(a = cfg->perms; a; a = a->next) {

		if(!acl_match_client(a, client, &client_addr)) continue; /* match client */

		/* go through authorized commands */
		for(i = 0; i < a->enabled.count; ++i) {
			if(strncasecmp(a->enabled.commands[i], cmd_name, cmd_len) == 0) {
				authorized = 1;
			}
			if(strncasecmp(a->enabled.commands[i], "*", 1) == 0) {
				authorized = 1;
			}
		}

		/* go through unauthorized commands */
		for(i = 0; i < a->disabled.count; ++i) {
			if(strncasecmp(a->disabled.commands[i], cmd_name, cmd_len) == 0) {
				authorized = 0;
			}
			if(strncasecmp(a->disabled.commands[i], "*", 1) == 0) {
				authorized = 0;
			}
		}
	}

	return authorized;
}

