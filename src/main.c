#include "netstate.h"

#include <unistd.h> // TODO: Delete when sleep is removed

int main(int argc, const char *argv[]) {
	netstate_init();
	// TODO: This should be read from configuration
	netstate_add("eth0");
	for (;;) {
		netstate_update();
		sleep(1);
	}
}
