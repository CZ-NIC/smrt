#include "link.h"
#include "util.h"

#include <errno.h>
#include <string.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

static int sock = -1;

int netlink_init(void) {
	sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
	if (sock == -1)
		die("Couldn't create netlink socket to listen for link up/down messages: %s\n", strerror(errno));
	struct sockaddr_nl addr = {
		.nl_family = AF_NETLINK,
		.nl_groups = RTMGRP_LINK
	};
	if (bind(sock, (struct sockaddr *)&addr, sizeof addr) == -1)
		die("Couldn't bind RTMGRP_LINK netlink group: %s\n", strerror(errno));
	return sock;
}

#define BUF_SIZE 8192

bool netlink_event(void) {
	// Receive message
	char msg_buf[BUF_SIZE];
	struct iovec iov = { msg_buf, sizeof msg_buf };
	struct sockaddr_nl addr;
	struct msghdr msg = {
		.msg_name = &addr,
		.msg_namelen = sizeof addr,
		.msg_iov = &iov,
		.msg_iovlen = 1
	};
	ssize_t slen = recvmsg(sock, &msg, MSG_DONTWAIT);
	if (slen == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			return false; // No data now, so no event
		if (errno == ENOBUFS)
			return false; // OK, there was not enough memory to send us message. The message may have contained something interesting, so expect it did contain and err on the safe side
		die("Error reading netlink data: %s\n", strerror(errno));
	}
	// A 0-length datagram is allowed. No idea why would anyone do that, but it's not an error and its not EOF here, so don't special-case it
	if (msg.msg_flags & MSG_TRUNC)
		return true; // We didn't read all the data from the packet. The part we lost might as well have contained the event we watch for, so we better expect there was one and err on the safe side.

	size_t len = slen; // Make sure it's unsigned, otherwise the next line complains with warning.
	for (struct nlmsghdr *nh = (struct nlmsghdr *)msg_buf; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
		if (nh->nlmsg_type == NLMSG_DONE)
			return false; // Nothing interesting so far and this one is a sentinel

		if (nh->nlmsg_type == NLMSG_NOOP)
			continue; // No idea why this is here, but ignore no-operation messages.

		if (nh->nlmsg_type == NLMSG_ERROR) {
			// There was an error, but we didn't send any netlink message, so why are we getting it?
			struct nlmsgerr *err = NLMSG_DATA(nh);
			die("Error sent over netlink: %s\n", strerror(-err->error));
		}

		if (nh->nlmsg_type == RTM_NEWLINK || nh->nlmsg_type == RTM_DELLINK) // If it's about link, it's interesting
			return true;

		// The rest that may be here is not interesting to us at all, just continue
	}

	return false; // Nothing interesting in the whole message
}
