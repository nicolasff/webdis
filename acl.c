#include "acl.h"
#include "cmd.h"
#include "conf.h"

#include <string.h>
#include <evhttp.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int
acl_match_client(struct acl *a, struct evhttp_request *rq, in_addr_t *ip) {

	/* check HTTP Basic Auth */
	const char *auth;
	auth = evhttp_find_header(rq->input_headers, "Authorization");
	if(auth && a->http_basic_auth && strncasecmp(auth, "Basic ", 6) == 0) { /* sent auth */
		if(strcmp(auth + 6, a->http_basic_auth) != 0) { /* wrong */
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
acl_allow_command(struct cmd *cmd, struct conf *cfg, struct evhttp_request *rq) {

	char *always_off[] = {"MULTI", "EXEC", "WATCH", "DISCARD"};

	unsigned int i;
	int authorized = 1;
	struct acl *a;

	char *client_ip;
	u_short client_port;
	in_addr_t client_addr;

	const char *cmd_name = cmd->argv[0];
	size_t cmd_len = cmd->argv_len[0];

	/* some commands are always disabled, regardless of the config file. */
	for(i = 0; i < sizeof(always_off) / sizeof(always_off[0]); ++i) {
		if(strncasecmp(always_off[i], cmd_name, cmd_len) == 0) {
			return 0;
		}
	}

	/* find client's address */
	evhttp_connection_get_peer(rq->evcon, &client_ip, &client_port);
	client_addr = ntohl(inet_addr(client_ip));

	/* go through permissions */
	for(a = cfg->perms; a; a = a->next) {

		if(!acl_match_client(a, rq, &client_addr)) continue; /* match client */

		/* go through authorized commands */
		for(i = 0; i < a->enabled.count; ++i) {
			if(strncasecmp(a->enabled.commands[i], cmd_name, cmd_len) == 0) {
				authorized = 1;
			}
		}

		/* go through unauthorized commands */
		for(i = 0; i < a->disabled.count; ++i) {
			if(strncasecmp(a->disabled.commands[i], cmd_name, cmd_len) == 0) {
				authorized = 0;
			}
		}
	}

	return authorized;
}

