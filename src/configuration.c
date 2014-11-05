#include "configuration.h"
#include "netstate.h"
#include "util.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct interface {
	const char *name;
	struct conn_mapping mappings[MAX_CONN_CNT];
	size_t mapping_count;
};

static struct interface *interfaces;
static size_t interface_count;

static int getnum() {
	char *end;
	long result = strtol(optarg, &end, 10);
	if (end && *end)
		die("%s is not a valid number\n", optarg);
	return result; // TODO: Check for range?
}

void configure(int argc, char *argv[]) {
	int option;
	int position = -1;
	while ((option = getopt(argc, argv, "-i:c:h")) != -1) {
		switch(option) {
			case 'i':
				netstate_add(optarg);
				interfaces = realloc(interfaces, (++ interface_count) * sizeof *interfaces);
				interfaces[interface_count - 1] = (struct interface) {
					.name = strdup(optarg)
				};
				break;
			case 'c': {
				if (!interfaces)
					die("No interfaces to assign the connection mapping to\n");
				struct interface *i = &interfaces[interface_count - 1];
				if (i->mapping_count == MAX_CONN_CNT)
					die("Too many connection mappings for interface %s\n", i->name);
				i->mappings[i->mapping_count ++].vlan = getnum();
				position = 0;
				break;
			}
			case 1: {
				if (position == -1)
					die("Unknown value %s\n", optarg);
				struct interface *i = &interfaces[interface_count - 1];
				struct conn_mapping *m = &i->mappings[i->mapping_count - 1];
				switch (position ++) {
					case 0:
						m->vpi = getnum();
						break;
					case 1:
						m->vci = getnum();
						m->active = true;
						position = -1;
						break;
				}
				break;
			}
			case '?':
			case 'h':
				puts("Small Modem for Router Turris daemon\n");
				puts("-e <interface>\n");
				puts("-c <vlan> <vpi> <vci>\n");
				exit(1);
		}
	}
	for (size_t i = 0; i < interface_count; i ++) {
		struct interface *ifc = &interfaces[i];
		for (size_t j = 0; j < ifc->mapping_count; j++)
			if (!ifc->mappings[j].active)
				die("Inactive connection %zu vlan %d on interface %s\n", j, ifc->mappings[j].vlan, ifc->name);
	}
}
