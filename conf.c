#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <jansson.h>
#include "conf.h"

struct conf *
conf_read(const char *filename) {

	json_t *j, *jtmp;
	json_error_t error;
	struct conf *conf;
	void *kv;

	/* defaults */
	conf = calloc(1, sizeof(struct conf));
	conf->redis_host = strdup("127.0.0.1");
	conf->redis_port = 6379;
	conf->http_host = strdup("0.0.0.0");
	conf->http_port = 7379;

	j = json_load_file(filename, 0, &error);
	if(!j) {
		fprintf(stderr, "Error: %s (line %d)\n", error.text, error.line);
		return conf;
	}

	for(kv = json_object_iter(j); kv; kv = json_object_iter_next(j, kv)) {
		jtmp = json_object_iter_value(kv);
		if(strcmp(json_object_iter_key(kv), "redis_host") == 0 && json_typeof(jtmp) == JSON_STRING) {
			free(conf->redis_host);
			conf->redis_host = strdup(json_string_value(jtmp));
		} else if(strcmp(json_object_iter_key(kv), "redis_port") == 0 && json_typeof(jtmp) == JSON_INTEGER) {
			conf->redis_port = json_integer_value(jtmp);
		} else if(strcmp(json_object_iter_key(kv), "http_host") == 0 && json_typeof(jtmp) == JSON_STRING) {
			free(conf->http_host);
			conf->http_host = strdup(json_string_value(jtmp));
		} else if(strcmp(json_object_iter_key(kv), "http_port") == 0 && json_typeof(jtmp) == JSON_INTEGER) {
			conf->http_port = json_integer_value(jtmp);
		} else if(strcmp(json_object_iter_key(kv), "disable") == 0 && json_typeof(jtmp) == JSON_OBJECT) {
			/* TODO */
		}
	}

	json_decref(j);

	return conf;
}

void
conf_free(struct conf *conf) {

	free(conf->redis_host);
	free(conf->http_host);

	free(conf);
}
