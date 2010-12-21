#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "conf.h"

static char *
skipspaces(char *p) {

	while(isspace(*p)) p++;

	return p;
}

struct conf *
conf_read(const char *filename) {

	struct conf *conf;

	FILE *f = fopen(filename, "r");

	if(!f) {
		return NULL;
	}

	conf = calloc(1, sizeof(struct conf));
	conf->redis_port = 6379;
	conf->http_port = 7379;

	while(!feof(f)) {
		char buffer[100], *ret;
		memset(buffer, 0, sizeof(buffer));
		if(!(ret = fgets(buffer, sizeof(buffer)-1, f))) {
			break;
		}
		if(*ret == '#') { /* comments */
			continue;
		}

		if(*ret != 0) {
			ret[strlen(ret)-1] = 0; /* remove new line */
		}

		if(strncmp(ret, "redis_host", 10) == 0) {
			conf->redis_host = strdup(skipspaces(ret + 11));
		} else if(strncmp(ret, "redis_port", 10) == 0) {
			conf->redis_port = (short)atoi(skipspaces(ret + 10));
		} else if(strncmp(ret, "http_host", 10) == 0) {
			conf->http_host = strdup(skipspaces(ret + 11));
		} else if(strncmp(ret, "http_port", 9) == 0) {
			conf->http_port = (short)atoi(skipspaces(ret + 10));
		}
	}
	fclose(f);

	/* default values */
	if(!conf->redis_host) {
		conf->redis_host = strdup("127.0.0.1");
	}
	if(!conf->http_host) {
		conf->http_host = strdup("0.0.0.0");
	}

	return conf;
}

void
conf_free(struct conf *conf) {

	free(conf->redis_host);
	free(conf->http_host);

	free(conf);
}
