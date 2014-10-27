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
}

static void iflink(size_t i, bool up) {
	if (interfaces[i].up != up) {
		dbg("Link of interface %s changed to %s\n", interfaces[i].name, up ? "up" : "down");
		interfaces[i].up = up;
		// TODO: Do something about it
	}
}

void netstate_update(void) {
	for (size_t i = 0; i < interface_count; i ++) {
		if (ioctl(dummy_socket, SIOCGIFFLAGS, &interfaces[i].ifreq) == -1) {
			dbg("Link %s doesn't exist\n", interfaces[i].name);
			iflink(i, false);
		} else
			iflink(i, interfaces[i].ifreq.ifr_flags & IFF_RUNNING);
	}
}
