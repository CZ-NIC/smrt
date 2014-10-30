#include "interface.h"

#include <stdlib.h>

struct interface_state {
	int fd;
};

struct interface_state *interface_alloc(const char *name, int *fd) {
	// TODO: Implement
	return NULL;
}

void interface_release(struct interface_state *interface) {
	// TODO
	free(interface);
}

int interface_timeout(struct interface_state *interface, uint64_t now) {
	// TODO
}

void interface_tick(struct interface_state *interface, uint64_t now) {
	// TODO
}

void interface_read(struct interface_state *interface, uint64_t now) {
	// TODO
}
