#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#include <jansson.h>
#include <evhttp.h>
#include <b64/cencode.h>
#include "conf.h"
#include "acl.h"

static struct acl *
conf_parse_acls(json_t *jtab);

#define ACL_ERROR_PREFIX "Config error with 'redis_auth': "
#define ACL_ERROR_SUFFIX ". Starting with auth disabled.\n"

static struct auth *
conf_auth_legacy(char *password);
static struct auth *
conf_auth_username_password(json_t *jarray);

int
conf_str_allcaps(const char *s, const size_t sz) {
	size_t i;
	for(i = 0; i < sz; i++) {
		if(s[i] != toupper(s[i])) {
			return 0;
		}
	}
	return 1;
}

char *
conf_string_or_envvar(const char *val) {
	if(!val) {
		return strdup(""); /* safe value for atoi/aol */
	}
	size_t val_len = strlen(val);
	if(val_len >= 2 && val[0] == '$' && conf_str_allcaps(val+1, val_len-1)) {
		char *env_val = getenv(val + 1);
		if(env_val) { /* found in environment */
			return strdup(env_val);
		} else {
			fprintf(stderr, "No value found for env var %s\n", val+1);
		}
	}
	/* duplicate string coming from JSON parser */
	return strdup(val);
}

int
atoi_free(char *s) {
	int val = atoi(s);
	free(s);
	return val;
}

int
is_true_free(char *s) {
	int val = strcasecmp(s, "true") == 0 ? 1 : 0;
	free(s);
	return val;
}

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
	conf->http_max_request_size = 128*1024*1024;
	conf->http_threads = 4;
	conf->user = getuid();
	conf->group = getgid();
	conf->logfile = "webdis.log";
	conf->log_fsync.mode = LOG_FSYNC_AUTO;
	conf->verbosity = WEBDIS_NOTICE;
	conf->daemonize = 0;
	conf->pidfile = "webdis.pid";
	conf->database = 0;
	conf->pool_size_per_thread = 2;

	j = json_load_file(filename, 0, &error);
	if(!j) {
		fprintf(stderr, "Error: %s (line %d)\n", error.text, error.line);
		return conf;
	}

	for(kv = json_object_iter(j); kv; kv = json_object_iter_next(j, kv)) {
		json_t *jtmp = json_object_iter_value(kv);

		if(strcmp(json_object_iter_key(kv), "redis_host") == 0 && json_typeof(jtmp) == JSON_STRING) {
			free(conf->redis_host);
			conf->redis_host = conf_string_or_envvar(json_string_value(jtmp));
		} else if(strcmp(json_object_iter_key(kv), "redis_port") == 0 && json_typeof(jtmp) == JSON_INTEGER) {
			conf->redis_port = (int)json_integer_value(jtmp);
		} else if(strcmp(json_object_iter_key(kv), "redis_port") == 0 && json_typeof(jtmp) == JSON_STRING) {
			conf->redis_port = atoi_free(conf_string_or_envvar(json_string_value(jtmp)));
		} else if(strcmp(json_object_iter_key(kv), "redis_auth") == 0) {
			if(json_typeof(jtmp) == JSON_STRING) {
				conf->redis_auth = conf_auth_legacy(conf_string_or_envvar(json_string_value(jtmp)));
			} else if(json_typeof(jtmp) == JSON_ARRAY) {
				conf->redis_auth = conf_auth_username_password(jtmp);
			} else if(json_typeof(jtmp) != JSON_NULL) {
				fprintf(stderr, ACL_ERROR_PREFIX "expected a string or an array of two strings" ACL_ERROR_SUFFIX);
			}
		} else if(strcmp(json_object_iter_key(kv), "http_host") == 0 && json_typeof(jtmp) == JSON_STRING) {
			free(conf->http_host);
			conf->http_host = conf_string_or_envvar(json_string_value(jtmp));
		} else if(strcmp(json_object_iter_key(kv), "http_port") == 0 && json_typeof(jtmp) == JSON_INTEGER) {
			conf->http_port = (int)json_integer_value(jtmp);
		} else if(strcmp(json_object_iter_key(kv), "http_port") == 0 && json_typeof(jtmp) == JSON_STRING) {
			conf->http_port = atoi_free(conf_string_or_envvar(json_string_value(jtmp)));
		} else if(strcmp(json_object_iter_key(kv), "http_max_request_size") == 0 && json_typeof(jtmp) == JSON_INTEGER) {
			conf->http_max_request_size = (size_t)json_integer_value(jtmp);
		} else if(strcmp(json_object_iter_key(kv), "http_max_request_size") == 0 && json_typeof(jtmp) == JSON_STRING) {
			conf->http_max_request_size = (size_t) atoi_free(conf_string_or_envvar(json_string_value(jtmp)));
		} else if(strcmp(json_object_iter_key(kv), "threads") == 0 && json_typeof(jtmp) == JSON_INTEGER) {
			conf->http_threads = (int)json_integer_value(jtmp);
		} else if(strcmp(json_object_iter_key(kv), "threads") == 0 && json_typeof(jtmp) == JSON_STRING) {
			conf->http_threads = atoi_free(conf_string_or_envvar(json_string_value(jtmp)));
		} else if(strcmp(json_object_iter_key(kv), "acl") == 0 && json_typeof(jtmp) == JSON_ARRAY) {
			conf->perms = conf_parse_acls(jtmp);
		} else if(strcmp(json_object_iter_key(kv), "user") == 0 && json_typeof(jtmp) == JSON_STRING) {
			struct passwd *u;
			if((u = getpwnam(conf_string_or_envvar(json_string_value(jtmp))))) {
				conf->user = u->pw_uid;
			}
		} else if(strcmp(json_object_iter_key(kv), "group") == 0 && json_typeof(jtmp) == JSON_STRING) {
			struct group *g;
			if((g = getgrnam(conf_string_or_envvar(json_string_value(jtmp))))) {
				conf->group = g->gr_gid;
			}
		} else if(strcmp(json_object_iter_key(kv),"logfile") == 0 && json_typeof(jtmp) == JSON_STRING){
			conf->logfile = conf_string_or_envvar(json_string_value(jtmp));
		} else if(strcmp(json_object_iter_key(kv),"log_fsync") == 0) {
			if(json_typeof(jtmp) == JSON_STRING && strcmp(json_string_value(jtmp), "auto") == 0) {
				conf->log_fsync.mode = LOG_FSYNC_AUTO;
			} else if(json_typeof(jtmp) == JSON_STRING && strcmp(json_string_value(jtmp), "all") == 0) {
				conf->log_fsync.mode = LOG_FSYNC_ALL;
			} else if(json_typeof(jtmp) == JSON_INTEGER && json_integer_value(jtmp) > 0) {
				conf->log_fsync.mode = LOG_FSYNC_MILLIS;
				conf->log_fsync.period_millis = (int)json_integer_value(jtmp);
			} else if(json_typeof(jtmp) != JSON_NULL) {
				fprintf(stderr, "Unexpected value for \"log_fsync\", defaulting to \"auto\"\n");
			}
		} else if(strcmp(json_object_iter_key(kv),"verbosity") == 0 && json_typeof(jtmp) == JSON_INTEGER){
			int tmp = json_integer_value(jtmp);
			if(tmp < 0 || tmp > (int)WEBDIS_TRACE) {
				fprintf(stderr, "Invalid log verbosity: %d. Acceptable range: [%d .. %d]\n",
					tmp, WEBDIS_ERROR, WEBDIS_TRACE);
			}
			conf->verbosity = (tmp < 0 ? WEBDIS_ERROR : (tmp > WEBDIS_TRACE ? WEBDIS_TRACE : (log_level)tmp));
		} else if(strcmp(json_object_iter_key(kv), "daemonize") == 0 && json_typeof(jtmp) == JSON_TRUE) {
			conf->daemonize = 1;
		} else if(strcmp(json_object_iter_key(kv), "daemonize") == 0 && json_typeof(jtmp) == JSON_STRING) {
			conf->daemonize = is_true_free(conf_string_or_envvar(json_string_value(jtmp)));
		} else if(strcmp(json_object_iter_key(kv),"pidfile") == 0 && json_typeof(jtmp) == JSON_STRING){
			conf->pidfile = conf_string_or_envvar(json_string_value(jtmp));
		} else if(strcmp(json_object_iter_key(kv), "websockets") == 0 && json_typeof(jtmp) == JSON_TRUE) {
			conf->websockets = 1;
		} else if(strcmp(json_object_iter_key(kv), "websockets") == 0 && json_typeof(jtmp) == JSON_STRING) {
			conf->websockets = is_true_free(conf_string_or_envvar(json_string_value(jtmp)));
		} else if(strcmp(json_object_iter_key(kv), "database") == 0 && json_typeof(jtmp) == JSON_INTEGER) {
			conf->database = json_integer_value(jtmp);
		} else if(strcmp(json_object_iter_key(kv), "database") == 0 && json_typeof(jtmp) == JSON_STRING) {
			conf->database = atoi_free(conf_string_or_envvar(json_string_value(jtmp)));
		} else if(strcmp(json_object_iter_key(kv), "pool_size") == 0 && json_typeof(jtmp) == JSON_INTEGER) {
			conf->pool_size_per_thread = json_integer_value(jtmp);
		} else if(strcmp(json_object_iter_key(kv), "pool_size") == 0 && json_typeof(jtmp) == JSON_STRING) {
			conf->pool_size_per_thread = atoi_free(conf_string_or_envvar(json_string_value(jtmp)));
		} else if(strcmp(json_object_iter_key(kv), "default_root") == 0 && json_typeof(jtmp) == JSON_STRING) {
			conf->default_root = conf_string_or_envvar(json_string_value(jtmp));
		}
	}

	json_decref(j);

	return conf;
}

void
acl_read_commands(json_t *jlist, struct acl_commands *ac) {

	unsigned int i, n, cur;

	/* count strings in the array */
	for(i = 0, n = 0; i < json_array_size(jlist); ++i) {
		json_t *jelem = json_array_get(jlist, (size_t)i);
		if(json_typeof(jelem) == JSON_STRING) {
			n++;
		}
	}

	/* allocate block */
	ac->commands = calloc((size_t)n, sizeof(char*));
	ac->count = n;

	/* add all disabled commands */
	for(i = 0, cur = 0; i < json_array_size(jlist); ++i) {
		json_t *jelem = json_array_get(jlist, i);
		if(json_typeof(jelem) == JSON_STRING) {
			ac->commands[cur] = conf_string_or_envvar(json_string_value(jelem));
			cur++;
		}
	}
}

struct acl *
conf_parse_acl(json_t *j) {

	json_t *jcidr, *jbasic, *jlist;
	unsigned short mask_bits = 0;

	struct acl *a = calloc(1, sizeof(struct acl));

	/* parse CIDR */
	if((jcidr = json_object_get(j, "ip")) && json_typeof(jcidr) == JSON_STRING) {
		const char *s;
		char *p, *ip;

		s = conf_string_or_envvar(json_string_value(jcidr));
		p = strchr(s, '/');
		if(!p) {
			ip = strdup(s);
		} else {
			ip = calloc((size_t)(p - s + 1), 1);
			memcpy(ip, s, (size_t)(p - s));
			mask_bits = (unsigned short)atoi(p+1);
		}
		a->cidr.enabled = 1;
		a->cidr.mask = (mask_bits == 0 ? 0xffffffff : (0xffffffff << (32 - mask_bits)));
		a->cidr.subnet = ntohl(inet_addr(ip)) & a->cidr.mask;
		free(ip);
	}

	/* parse basic_auth */
	if((jbasic = json_object_get(j, "http_basic_auth")) && json_typeof(jbasic) == JSON_STRING) {

		/* base64 encode */
		base64_encodestate b64;
		int pos;
		char *p;
		char *plain = conf_string_or_envvar(json_string_value(jbasic));
		size_t len, plain_len = strlen(plain) + 0;
		len = (plain_len + 8) * 8 / 6;
		a->http_basic_auth = calloc(len, 1);

		base64_init_encodestate(&b64);
		pos = base64_encode_block(plain, (int)plain_len, a->http_basic_auth, &b64);
		free(plain);
		if(!pos) { /* nothing was encoded */
			fprintf(stderr, "Error: could not encode credentials as HTTP basic auth header\n");
			exit(1);
		}
		base64_encode_blockend(a->http_basic_auth + pos, &b64);

		/* end string with \0 rather than \n */
		if((p = strchr(a->http_basic_auth + pos, '\n'))) {
			*p = 0;
		}
	}

	/* parse enabled commands */
	if((jlist = json_object_get(j, "enabled")) && json_typeof(jlist) == JSON_ARRAY) {
		acl_read_commands(jlist, &a->enabled);
	}

	/* parse disabled commands */
	if((jlist = json_object_get(j, "disabled")) && json_typeof(jlist) == JSON_ARRAY) {
		acl_read_commands(jlist, &a->disabled);
	}

	return a;
}

struct acl *
conf_parse_acls(json_t *jtab) {

	struct acl *head = NULL, *tail = NULL, *tmp;

	unsigned int i;
	for(i = 0; i < json_array_size(jtab); ++i) {
		json_t *val = json_array_get(jtab, i);

		tmp = conf_parse_acl(val);
		if(head == NULL && tail == NULL) {
			head = tail = tmp;
		} else {
			tail->next = tmp;
			tail = tmp;
		}
	}

	return head;
}

void
conf_free(struct conf *conf) {

	free(conf->redis_host);
	if(conf->redis_auth) {
		free(conf->redis_auth->username);
		free(conf->redis_auth->password);
	}
	free(conf->redis_auth);

	free(conf->http_host);

	free(conf);
}

static struct auth *
conf_auth_legacy(char *password) {
	struct auth *ret = calloc(1, sizeof(struct auth));
	if(!ret) {
		free(password);
		fprintf(stderr, ACL_ERROR_PREFIX "failed to allocate memory for credentials" ACL_ERROR_SUFFIX);
		return NULL;
	}
	ret->use_legacy_auth = 1;
	ret->password = password; /* already transformed to include decoded env vars */
	return ret;
}

static struct auth *
conf_auth_username_password(json_t *jarray) {
	size_t array_size = json_array_size(jarray);
	if(array_size != 2) {
		fprintf(stderr, ACL_ERROR_PREFIX "expected two values, found %zu" ACL_ERROR_SUFFIX, array_size);
		return NULL;
	}
	json_t *jusername = json_array_get(jarray, 0);
	json_t *jpassword = json_array_get(jarray, 1);
	if(json_typeof(jusername) != JSON_STRING || json_typeof(jpassword) != JSON_STRING) {
		fprintf(stderr, ACL_ERROR_PREFIX "both values need to be strings" ACL_ERROR_SUFFIX);
		return NULL;
	}

	char *username = conf_string_or_envvar(json_string_value(jusername));
	char *password = conf_string_or_envvar(json_string_value(jpassword));
	struct auth *ret = calloc(1, sizeof(struct auth));
	if(!username || !password || !ret) {
		free(username);
		free(password);
		free(ret);
		fprintf(stderr, ACL_ERROR_PREFIX "failed to allocate memory for credentials" ACL_ERROR_SUFFIX);
		return NULL;
	}

	ret->use_legacy_auth = 0;
	ret->username = username;
	ret->password = password;
	return ret;
}
