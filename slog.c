#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "slog.h"
#include "server.h"
#include "conf.h"

/**
 * Initialize log writer.
 */
void
slog_init(struct server *s) {

	s->log.self = getpid();

	if(s->cfg->logfile) {

		int old_fd = s->log.fd;

		s->log.fd = open(s->cfg->logfile,
			O_WRONLY | O_APPEND | O_CREAT, S_IRUSR|S_IWUSR);

		/* close old log */
		if (old_fd != -1) {
			close(old_fd);
		}

		if (s->log.fd != -1)
			return;

		fprintf(stderr, "Could not open %s: %s\n", s->cfg->logfile,
				strerror(errno));
	}
	s->log.fd = 2; /* stderr */
}

/**
 * Write log message to disk, or stderr.
 */
void
slog(struct server *s, log_level level,
		const char *body, size_t sz) {

	const char *c = ".-*#";
	time_t now;
	char time_buf[64];
	char msg[124];
	char line[256]; /* bounds are checked. */
	int line_sz, ret;

	if(level > s->cfg->verbosity) return; /* too verbose */

	if(!s->log.fd) return;

	/* limit message size */
	sz = sz ? sz:strlen(body);
	snprintf(msg, sz + 1 > sizeof(msg) ? sizeof(msg) : sz + 1, "%s", body);

	/* get current time */
	now = time(NULL);
	strftime(time_buf, sizeof(time_buf), "%d %b %H:%M:%S", localtime(&now));

	/* generate output line. */
	line_sz = snprintf(line, sizeof(line),
		"[%d] %s %d %s\n", (int)s->log.self, time_buf, c[level], msg);

	/* write to log and flush to disk. */
	ret = write(s->log.fd, line, line_sz);
	ret = fsync(s->log.fd);

	(void)ret;
}
