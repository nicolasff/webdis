/* A slog is a simple log. Basically extracted from antirez/redis. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#include "slog.h"
#include "server.h"
#include "conf.h"

void slog(const struct server *s, log_level level, const char *body) {
	const char *c = ".-*#";
	time_t now = time(NULL);
	FILE *fp;
	char buf[64];
	char msg[1024];

	if(level > s->cfg->verbosity) {
		return;
	}

	snprintf(msg, sizeof(msg), "%s", body);

	fp = (s->cfg->logfile == NULL) ? stdout : fopen(s->cfg->logfile,"a");
	if(!fp) return;

	strftime(buf,sizeof(buf),"%d %b %H:%M:%S",localtime(&now));
	fprintf(fp,"[%d] %s %c %s\n",(int)getpid(),buf,c[level],msg);
	fprintf(stdout,"[%d] %s %c %s\n",(int)getpid(),buf,c[level],msg);
	fflush(fp);

	if(s->cfg->logfile) fclose(fp);
}
