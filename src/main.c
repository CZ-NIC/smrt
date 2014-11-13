#include "netstate.h"
#include "link.h"
#include "util.h"
#include "interface.h"
#include "configuration.h"

#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>

struct epoll_tag {
	void (*hook)(struct epoll_tag *tag);
	int fd;
	const char *name;
	size_t idx;
};

struct interface_wrapper {
	char *name;
	struct interface_state *state;
	struct epoll_tag *tag;
};

static struct interface_wrapper *interfaces;
static size_t interface_count;
static uint64_t now; // Current time in milliseconds from some point in the past

static int poller = -1;

static int interface_idx(const char *ifname) {
	for (size_t i = 0; i < interface_count; i ++)
		if (strcmp(ifname, interfaces[i].name) == 0)
			return i;
	return -1;
}

static void interface_packet(struct epoll_tag *tag) {
	interface_read(interfaces[tag->idx].state, now);
}

static void up(const char *ifname) {
	assert(interface_idx(ifname) == -1); // This interface doesn't exist here
	size_t idx = interface_count;
	dbg("Creating structure for interface %s on index %zu\n", ifname, idx);
	interfaces = realloc(interfaces, (++ interface_count) * sizeof *interfaces);
	interfaces[idx] = (struct interface_wrapper) {
		.name = strdup(ifname),
		.tag = malloc(sizeof *interfaces->tag)
	};
	*interfaces[idx].tag = (struct epoll_tag) {
		.hook = interface_packet,
		.name = interfaces[idx].name,
		.idx = idx
	};
	interfaces[idx].state = interface_alloc(ifname, &interfaces[idx].tag->fd);
	struct epoll_event event = {
		.events = EPOLLIN,
		.data.ptr = interfaces[idx].tag
	};
	if (epoll_ctl(poller, EPOLL_CTL_ADD, interfaces[idx].tag->fd, &event) == -1)
		die("Couldn't add fd %d for interface %s to epoll: %s\n", interfaces[idx].tag->fd, ifname, strerror(errno));
}

static void down(const char *ifname) {
	int idx = interface_idx(ifname);
	assert(idx != -1);
	size_t last = interface_count - 1;
	dbg("Releasing interface structure %s on index %d (%zu moves to %d)\n", ifname, idx, last, idx);
	interfaces[last].tag->idx = idx;
	// This will also close the file descriptor, which will remove it from the poller
	interface_release(interfaces[idx].state);
	free(interfaces[idx].tag);
	free(interfaces[idx].name);
	interfaces = realloc(interfaces, (interface_count --) * sizeof *interfaces);
}

static void netlink_ready(struct epoll_tag *unused) {
	(void)unused;
	if (netlink_event()) {
		dbg("Netlink event\n");
		netstate_update();
	} else {
		dbg("Netlink false alarm\n");
	}
}

static void update_now(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
		die("Couldn't get time: %s\n", strerror(errno));
	now = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

// We don't care about performance. But multiple events might mean trouble like releasing something and then using it from another event.
#define MAX_EVENTS 1

int main(int argc, char *argv[]) {
	// Initialize epoll
	poller = epoll_create(42 /* Man mandates this to be positive but otherwise without meaning */);
	if (poller == -1)
		die("Couldn't create epoll: %s\n", strerror(errno));
	// Initialize link detector
	struct epoll_tag netlink_epoll = {
		.hook = netlink_ready,
		.fd = netlink_init(),
		.name = "Netlink"
	};
	struct epoll_event netlink_event = {
		.events = EPOLLIN,
		.data.ptr = &netlink_epoll
	};
	if (epoll_ctl(poller, EPOLL_CTL_ADD, netlink_epoll.fd, &netlink_event) == -1)
		die("Couldn't insert netlink FD into epoll: %s\n", strerror(errno));
	// Initialize the netstate (after the netlink, so we don't miss any event
	netstate_init();
	netstate_set_hooks(up, down);
	configure(argc, argv);
	netstate_update();

	// Run the loop. Forever.
	dbg("Init done\n");
	update_now();
	for (;;) {
		int timeout = -1;
		for (size_t i = 0; i < interface_count; i ++) {
			int it = interface_timeout(interfaces[i].state, now);
			assert(it >= -1);
			if (timeout == -1 || (it != -1 && it < timeout))
				timeout = it;
		}
		struct epoll_event events[MAX_EVENTS];
		dbg("Epoll wait with %d ms timeout\n", timeout);
		int events_read = epoll_wait(poller, events, MAX_EVENTS, timeout);
		update_now();
		dbg("Epoll tick\n");
		if (events_read == -1) {
			if (errno == EINTR)
				continue;
			die("Error waiting for epoll: %s\n", strerror(errno));
		}
		for (int i = 0; i < events_read; i ++) {
			struct epoll_tag *t = events[i].data.ptr;
			if (events[i].events & EPOLLERR)
				die("Error on file descriptor %d/%s\n", t->fd, t->name);
			if (events[i].events & EPOLLIN)
				t->hook(t);
		}
		// Timeouts
		for (size_t i = 0; i < interface_count; i ++)
			if (interface_timeout(interfaces[i].state, now) <= 0)
				interface_tick(interfaces[i].state, now);
	}
}
