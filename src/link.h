#ifndef SMRT_LINK_H
#define SMRT_LINK_H

#include <stdbool.h>

// Initialize the netlink socket and start watching for link up/down events. The FD is returned.
int netlink_init();
// There was an event on the netlink socket. Is it a link up/down event?
bool netlink_event();

#endif
