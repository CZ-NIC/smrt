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

#include "netstate.h"
#include "util.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

// Just a socket to feed ioctl with, no real use
static int dummy_socket = -1;

struct interface {
	struct ifreq ifreq;
	const char *name;
	bool up;
};

static struct interface *interfaces;
static size_t interface_count;

static link_hook up_hook;
static link_hook down_hook;

void netstate_init(void) {
	dbg("Initializing netstate\n");
	dummy_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (dummy_socket == -1)
		die("Couldn't create dummy UDP socket: %s\n", strerror(errno));
}

void netstate_add(const char *name) {
	dbg("Watching for interface %s\n", name);
	interfaces = realloc(interfaces, (++ interface_count) * sizeof *interfaces);
	interfaces[interface_count - 1] = (struct interface) {
		.name = strdup(name)
	};
	strncpy(interfaces[interface_count - 1].ifreq.ifr_name, name, IFNAMSIZ);
	interfaces[interface_count - 1].ifreq.ifr_name[IFNAMSIZ - 1] = '\0'; // strncpy may omit the '\0' if the name doesn't fit
}

static void iflink(size_t i, bool up) {
	if (interfaces[i].up != up) {
		msg("Link of interface %s changed to %s\n", interfaces[i].name, up ? "up" : "down");
		interfaces[i].up = up;
		link_hook hook = up ? up_hook : down_hook;
		if (hook)
			hook(interfaces[i].name);
	}
}

void netstate_update(void) {
	for (size_t i = 0; i < interface_count; i ++) {
		if (ioctl(dummy_socket, SIOCGIFFLAGS, &interfaces[i].ifreq) == -1) {
			msg("Link %s doesn't exist: %s\n", interfaces[i].name, strerror(errno));
			iflink(i, false);
		} else
			iflink(i, interfaces[i].ifreq.ifr_flags & IFF_RUNNING);
	}
}

void netstate_set_hooks(link_hook up, link_hook down) {
	up_hook = up;
	down_hook = down;
}

void netstate_down(const char *name) {
	for (size_t i = 0; i < interface_count; i ++)
		if (strcmp(name, interfaces[i].name) == 0) {
			interfaces[i].up = false;
			return;
		}
}
