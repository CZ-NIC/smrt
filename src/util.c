/*
 * SMRTd ‒ daemon to initialize the Small Modem for Router Turris
 * Copyright (C) 2014 CZ.NIC, z.s.p.o. <http://www.nic.cz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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
