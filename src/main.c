#include "netstate.h"

#include <unistd.h> // TODO: Delete when sleep is removed
#include <stdlib.h>

void up(const char *ifname) {
	system("/home/vorner/start-modem");
}

int main(int argc, const char *argv[]) {
	netstate_init();
	// TODO: This should be read from configuration
	netstate_add("eth0");
	netstate_set_hooks(up, NULL);
	for (;;) {
		netstate_update();
		sleep(1);
	}
}
