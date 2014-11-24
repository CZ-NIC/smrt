#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

void die(const char *message, ...) {
	va_list args, copy;
	va_start(args, message);
	va_copy(copy, args);
	vsyslog(LOG_MAKEPRI(LOG_DAEMON, LOG_CRIT), message, copy);
	va_end(copy);
	vfprintf(stderr, message, args);
	va_end(args);
	exit(1);
}

void msg(const char *message, ...) {
	va_list args, copy;
	va_start(args, message);
	va_copy(copy, args);
	vsyslog(LOG_MAKEPRI(LOG_DAEMON, LOG_INFO), message, copy);
	va_end(copy);
	vfprintf(stderr, message, args);
	va_end(args);
}

void dbg(const char *message, ...) {
	va_list args;
	va_start(args, message);
	vfprintf(stderr, message, args);
	va_end(args);
}
