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

#include "interface.h"
#include "util.h"
#include "automaton.h"
#include "proto_const.h"
#include "configuration.h"

#include <alloca.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <stdio.h>

// The MAC address of the device - this seems ugly, but can't be helped
#define DEST_MAC {6, 5, 4, 3, 2, 1}
const uint8_t dest_mac[] = DEST_MAC;
// We won't receive larger packets
#define RECV_PACKET_LEN 4096

struct interface_state {
	char *ifname;
	int fd;
	enum autom_state autom_state;
	bool timeout_active;
	uint64_t timeout_dest;
	int timeout, timeout_add, timeout_mult, retries;
	void *packet;
	size_t packet_size;
	struct extra_state *extra_state;
	uint8_t mac_addr[ETH_ALEN];
	int ifindex;
};

struct interface_state *interface_alloc(const char *name, int *fd) {
	// We communicate over ethernet frames, so we need to manipulate them on rather low level.
	int sock = socket(AF_PACKET, SOCK_RAW, htons(CONTROL_PROTOCOL));
	if (sock == -1)
		die("Couldn't create AF_PACKET socket: %s\n", strerror(errno));
	// Get info about the interface (index, MAC address)
	static int fake_sock = -1;
	if (fake_sock == -1) {
		fake_sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (fake_sock == -1)
			die("Couldn't crate fake socket: %s\n", strerror(errno));
	}
	struct ifreq req;
	memset(&req, 0, sizeof req);
	strncpy(req.ifr_name, name, IFNAMSIZ);
	req.ifr_name[IFNAMSIZ - 1] = '\0'; // strncpy doesn't set the terminating '\0' if it doesn't fit
	if (ioctl(sock, SIOCGIFINDEX, &req) == -1)
		die("Couldn't get interface index for %s: %s\n", name, strerror(errno));
	dbg("Interface %s is on index %d\n", name, req.ifr_ifindex);
	int ifindex = req.ifr_ifindex;
	if (ioctl(sock, SIOCGIFHWADDR, &req) == -1)
		die("Couldn't get mac address for interface %s: %s\n", name, strerror(errno));
	struct sockaddr_ll addr = {
		.sll_family = AF_PACKET,
		.sll_protocol = htons(CONTROL_PROTOCOL),
		.sll_ifindex = ifindex
	};
	if (bind(sock, (struct sockaddr *)&addr, sizeof addr) == -1)
		die("Couldn't bind AF_PACKET socket %d to interface %s: %s\n", sock, name, strerror(errno));
	*fd = sock;
	struct interface_state *result = malloc(sizeof *result);
	*result = (struct interface_state) {
		.ifname = strdup(name),
		.fd = sock,
		.autom_state = AS_PRESTART,
		.timeout_active = true,
		.ifindex = ifindex
	};
	memcpy(result->mac_addr, req.ifr_hwaddr.sa_data, ETH_ALEN);
	return result;
}

void interface_release(struct interface_state *interface) {
	if (close(interface->fd) == -1)
		die("Couldn't close interface's communication socket %d: %s\n", interface->fd, strerror(errno));
	extra_state_destroy(interface->extra_state);
	const char *path = interface_status_path(interface->ifname);
	if (unlink(path) == -1) {
		if (errno == ENOENT)
			msg("File %s not removed as it doesn't exist\n", path);
		else
			die("Couldn't remove interface status file %s: %s\n", path, strerror(errno));
	}
	free(interface->ifname);
	free(interface->packet);
	free(interface);
}

int interface_timeout(struct interface_state *interface, uint64_t now) {
	if (interface->timeout_active) {
		if (now >= interface->timeout_dest)
			return 0; // Already timed out
		else
			return interface->timeout_dest - now;
	} else
		return -1;
}

struct packet_basic {
	struct ethhdr hdr;
	uint8_t data[];
} __attribute__((packed));

static void packet_send(struct interface_state *interface) {
	if (!interface->packet)
		return; // No packet to send
	// Assemble the packet with the header
	size_t size = interface->packet_size + sizeof(struct packet_basic);
	struct packet_basic *p = alloca(size);
	p->hdr = (struct ethhdr) {
		.h_dest = DEST_MAC,
		.h_proto = htons(CONTROL_PROTOCOL)
	};
	memcpy(p->hdr.h_source, interface->mac_addr, ETH_ALEN);
	memcpy(p->data, interface->packet, interface->packet_size);
	// Address (it only says the interface)
	struct sockaddr_ll addr = {
		.sll_family = AF_PACKET,
		.sll_ifindex = interface->ifindex,
		.sll_protocol = htons(CONTROL_PROTOCOL)
	};
	ssize_t sent = sendto(interface->fd, p, size, MSG_NOSIGNAL, (struct sockaddr *)&addr, sizeof addr);
	if (sent == -1) {
		if (errno == EINTR) {// Interrupted when sending. Retry.
			packet_send(interface);
			return;
		}
		die("Couldn't send packet of size %zu on interface %d and fd %d: %s\n", size, interface->ifindex, interface->fd, strerror(errno));
	}
	if ((size_t)sent != size)
		die("Sent only %zd bytes out of %zu on packet on interface %d and fd %d\n", sent, size, interface->ifindex, interface->fd);
}

static void transition_perform(struct interface_state *interface, uint64_t now, const struct transition *transition) {
	if (!transition) // It is allowed to perform no transition as a result of some event
		return;
	// The timeout
	if (transition->timeout_set) {
		interface->timeout = transition->timeout;
		interface->timeout_add = transition->timeout_add;
		interface->timeout_mult = transition->timeout_mult;
		interface->timeout_dest = transition->timeout + now;
		interface->retries = transition->retries;
	}
	interface->timeout_active = transition->timeout_set;
	// The packet
	free(interface->packet); // Destroy the old one
	interface->packet = NULL;
	if (transition->packet_send) {
		size_t size = interface->packet_size = transition->packet_size;
		memcpy(interface->packet = malloc(size), transition->packet, size);
		packet_send(interface);
	}
	// Extra state (just store it)
	interface->extra_state = transition->extra_state;
	// Name of state
	if (transition->status_name) {
		const char *path = interface_status_path(interface->ifname);
		FILE *f = fopen(path, "w");
		if (!f)
			die("Failed to write status to file %s: %s\n", path, strerror(errno));
		fprintf(f, "<status>%s</status>\n", transition->status_name);
		dbg("State %s\n", transition->status_name);
		if (fclose(f) == EOF)
			die("Failed to close status file %s: %s\n", path, strerror(errno));
	}
	// The state
	if (transition->state_change) {
		dbg("Changing state to %u\n", (unsigned)transition->new_state);
		interface->autom_state = transition->new_state;
		transition_perform(interface, now, state_enter(interface->ifname, interface->autom_state, interface->extra_state)); // Also enter the new state
	}
}

void interface_tick(struct interface_state *interface, uint64_t now) {
	if (interface->retries) {
		dbg("Resending packet\n");
		// We should try sending the packet again as long we have retries
		packet_send(interface);
		interface->retries --;
		// Compute a new timeout
		interface->timeout = interface->timeout * interface->timeout_mult + interface->timeout_add;
		interface->timeout_dest = now + interface->timeout;
	} else {
		dbg("Timed out\n");
		// OK, we sent all the retries. We really timed out. So enter a new state.
		transition_perform(interface, now, state_timeout(interface->ifname, interface->autom_state, interface->extra_state));
	}
}

void interface_read(struct interface_state *interface, uint64_t now) {
	uint8_t buffer[RECV_PACKET_LEN];
	ssize_t received = recv(interface->fd, buffer, RECV_PACKET_LEN, MSG_DONTWAIT | MSG_TRUNC);
	if (received == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			return; // Try again next time
		die("Error receiving packet on interface %d and fd %d: %s\n", interface->ifindex, interface->fd, strerror(errno));
	}
	if (received > RECV_PACKET_LEN)
		die("Packet of size %zd received, but I have space only for %u (interface %d, fd %d)\n", received, (unsigned)RECV_PACKET_LEN, interface->ifindex, interface->fd);
	dbg("Packet from the modem on interface %d fd %d of size %zd\n", interface->ifindex, interface->fd, received);
	struct packet_basic *p = (struct packet_basic *)buffer;
	if (memcmp(p->hdr.h_source, dest_mac, ETH_ALEN) != 0 || memcmp(p->hdr.h_dest, interface->mac_addr, ETH_ALEN) != 0 || p->hdr.h_proto != htons(CONTROL_PROTOCOL)) {
		dbg("Foreign packet received and ignored\n");
		return;
	}
	dbg("Packet on interface %d fd %d of size %zd\n", interface->ifindex, interface->fd, received);
	transition_perform(interface, now, state_packet(interface->ifname, interface->autom_state, interface->extra_state, p->data, received - sizeof p->hdr));
}
