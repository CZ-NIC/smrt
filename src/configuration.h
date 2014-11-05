#ifndef SMRT_CONFIGURATION_H
#define SMRT_CONFIGURATION_H

#include <stdlib.h>
#include <stdbool.h>

#define MAX_CONN_CNT 8

struct conn_mapping {
	int vlan;
	int vpi;
	int vci;
	bool active;
};

const struct conn_mapping *iface_conns(const char *iface);

// Read the configuration passed on command line and set up everything
void configure(int argc, char *argv[]);

extern const char *image_path;
extern const char *fw_version;

#endif
