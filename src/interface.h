#ifndef SMRT_INTERFACE_H
#define SMRT_INTERFACE_H

#include <stdint.h>

struct interface_state;

// Create a new interface with given name. The fd is out-parameter and it is a file descriptor to watch for new packets.
struct interface_state *interface_alloc(const char *name, int *fd);
// Destroy previosly created interface.
void interface_release(struct interface_state *interface);

// Return number of milliseconds after which the interface should „tick“. -1 is never.
int interface_timeout(struct interface_state *interface, uint64_t now);
// The interface timeout reached 0, so this gets called.
void interface_tick(struct interface_state *interface, uint64_t now);
// There's a packet on the interface.
void interface_read(struct interface_state *interface, uint64_t now);

#endif
