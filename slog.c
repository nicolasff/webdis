/* A slog is a simple log. Basically extracted from antirez/redis. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

void slog(const char *logfile, int level, const char *body) {
    const char *c = ".-*#";
    time_t now = time(NULL);
    FILE *fp;
    char buf[64];
    char msg[1024];

    snprintf(msg, sizeof(msg), "%s", body);

    fp = (logfile == NULL) ? stdout : fopen(logfile,"a");
    if (!fp) return;

    strftime(buf,sizeof(buf),"%d %b %H:%M:%S",localtime(&now));
    fprintf(fp,"[%d] %s %c %s\n",(int)getpid(),buf,c[level],msg);
    fprintf(stdout,"[%d] %s %c %s\n",(int)getpid(),buf,c[level],msg);
    fflush(fp);

    if (logfile) fclose(fp);
}
