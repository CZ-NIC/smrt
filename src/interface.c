#include "interface.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <sys/ioctl.h>

const uint16_t control_protocol = 0x8889;

struct interface_state {
	int fd;
};

int ifindex(const char *name) {
	static int fake_sock = -1;
	if (fake_sock == -1) {
		fake_sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (fake_sock == -1)
			die("Couldn't crate fake socket: %s\n", strerror(errno));
	}
	struct ifreq req;
	memset(&req, 0, sizeof req);
	strncpy(req.ifr_name, name, IFNAMSIZ);
	if (ioctl(fake_sock, SIOCGIFINDEX, &req) == -1)
		die("Couldn't get interface index for %s: %s\n", name, strerror(errno));
	dbg("Interface %s is on index %d\n", name, req.ifr_ifindex);
	return req.ifr_ifindex;
}

struct interface_state *interface_alloc(const char *name, int *fd) {
	// We communicate over ethernet frames, so we need to manipulate them on rather low level.
	int sock = socket(AF_PACKET, SOCK_RAW, htons(control_protocol));
	if (sock == -1)
		die("Couldn't create AF_PACKET socket: %s\n", strerror(errno));
	struct sockaddr_ll addr = {
		.sll_family = AF_PACKET,
		.sll_protocol = htons(control_protocol),
		.sll_ifindex = ifindex(name)
	};
	if (bind(sock, (struct sockaddr *)&addr, sizeof addr) == -1)
		die("Couldn't bind AF_PACKET socket %d to interface %s: %s\n", sock, name, strerror(errno));
	*fd = sock;
	struct interface_state *result = malloc(sizeof *result);
	*result = (struct interface_state) {
		.fd = sock
	};
	return result;
}

void interface_release(struct interface_state *interface) {
	if (close(interface->fd) == -1)
		die("Couldn't close interface's communication socket %d: %s\n", interface->fd, strerror(errno));
	free(interface);
}

int interface_timeout(struct interface_state *interface, uint64_t now) {
	// TODO
	return -1;
}

void interface_tick(struct interface_state *interface, uint64_t now) {
	// TODO
}

void interface_read(struct interface_state *interface, uint64_t now) {
	// TODO
}
