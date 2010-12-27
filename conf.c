#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

#include <jansson.h>
#include "conf.h"

static struct disabled_command *
conf_disable_commands(json_t *jtab);

struct conf *
conf_read(const char *filename) {

	json_t *j;
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
		json_t *jtmp = json_object_iter_value(kv);

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
			conf->disabled = conf_disable_commands(jtmp);
		}
	}

	json_decref(j);

	return conf;
}


struct disabled_command *
conf_disable_commands(json_t *jtab) {

	struct disabled_command *root = NULL;

	void *kv;
	for(kv = json_object_iter(jtab); kv; kv = json_object_iter_next(jtab, kv)) {

		unsigned int i, cur, n;
		char *p, *ip;
		const char *s;
		in_addr_t mask_ip;
		short mask_bits = 0;

		struct disabled_command *dc;
		json_t *val = json_object_iter_value(kv);

		if(json_typeof(val) != JSON_ARRAY) {
			continue; /* TODO: report error? */
		}

		/* parse key in format "ip/mask" */
		s = json_object_iter_key(kv);
		p = strchr(s, ':');
		if(!p) {
			ip = strdup(s);
		} else {
			ip = calloc(p - s + 1, 1);
			memcpy(ip, s, p - s);
			mask_bits = atoi(p+1);
		}
		mask_ip = inet_addr(ip);


		/* count strings in the array */
		n = 0;
		for(i = 0; i < json_array_size(val); ++i) {
			json_t *jelem = json_array_get(val, i);
			if(json_typeof(jelem) == JSON_STRING) {
				n++;
			}
		}

		/* allocate block */
		dc = calloc(1, sizeof(struct disabled_command));
		dc->commands = calloc(n, sizeof(char*));
		dc->mask_ip = mask_ip;
		dc->mask_bits = mask_bits;
		dc->next = root;
		root = dc;

		/* add all disabled commands */
		for(i = 0, cur = 0; i < json_array_size(val); ++i) {
			json_t *jelem = json_array_get(val, i);
			if(json_typeof(jelem) == JSON_STRING) {
				s = json_string_value(jelem);
				size_t sz = strlen(s);

				dc->commands[cur] = calloc(1 + sz, 1);
				memcpy(dc->commands[cur], s, sz);
				cur++;
			}
		}
	}

	return root;
}

void
conf_free(struct conf *conf) {

	free(conf->redis_host);
	free(conf->http_host);

	free(conf);
}
