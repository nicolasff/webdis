/* A slog is a simple log. Basically extracted from antirez/redis. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

#include "slog.h"
#include "server.h"
#include "conf.h"

void
slog(struct server *s, log_level level,
		const char *body, size_t sz) {

	const char *c = ".-*#";
	time_t now = time(NULL);
	static FILE *fp = NULL;
	char buf[64];
	char msg[124];


	static pid_t self = 0;
	if(!self) self = getpid();

	if(level > s->cfg->verbosity) return; /* too verbose */

	pthread_spin_lock(&s->log_lock);

	if(!fp) fp = (s->cfg->logfile == NULL) ? stdout : fopen(s->cfg->logfile, "a");
	if(!fp) goto end;

	/* limit message size */
	snprintf(msg, sz + 1 > sizeof(msg) ? sizeof(msg) : sz + 1, "%s", body);

	strftime(buf,sizeof(buf),"%d %b %H:%M:%S",localtime(&now));
	fprintf(fp,"[%d] %s %c %s\n", (int)self, buf, c[level], msg);
	fflush(fp);

end:
	pthread_spin_unlock(&s->log_lock);
}
