#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void die(const char *message, ...) {
	va_list args;
	va_start(args, message);
	vfprintf(stderr, message, args);
	va_end(args);
	exit(1);
}

void dbg(const char *message, ...) {
	va_list args;
	va_start(args, message);
	vfprintf(stderr, message, args);
	va_end(args);
}
