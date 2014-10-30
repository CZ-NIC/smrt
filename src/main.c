#include "netstate.h"
#include "link.h"
#include "util.h"

#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <stdlib.h> // TODO: Remove when system is no longer needed

static void up(const char *ifname) {
	(void)ifname;
	system("/home/vorner/start-modem");
}

struct epoll_tag {
	void (*hook)(struct epoll_tag *tag);
	int fd;
	const char *name;
};

static void netlink_ready(struct epoll_tag *unused) {
	(void)unused;
	if (netlink_event()) {
		dbg("Netlink event\n");
		netstate_update();
	} else {
		dbg("Netlink false alarm\n");
	}
}

#define MAX_EVENTS 10

int main(int argc, const char *argv[]) {
	// Initialize epoll
	int poller = epoll_create(42 /* Man mandates this to be positive but otherwise without meaning */);
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
	netstate_set_hooks(up, NULL);
	// TODO: This should be read from configuration
	netstate_add("eth0");
	netstate_update();

	// Run the loop. Forever.
	dbg("Init done\n");
	for (;;) {
		struct epoll_event events[MAX_EVENTS];
		int events_read = epoll_wait(poller, events, MAX_EVENTS, -1);
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
	}
}
