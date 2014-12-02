/*
 * SMRTd ‒ daemon to initialize the Small Modem for Router Turris
 * Copyright (C) 2014 CZ.NIC, z.s.p.o. <http://www.nic.cz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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

const char *image_path;
const char *fw_version;

void configure(int argc, char *argv[]) {
	int option;
	int position = -1;
	while ((option = getopt(argc, argv, "-i:c:f:v:h")) != -1) {
		switch(option) {
			case 'i':
				netstate_add(optarg);
				interfaces = realloc(interfaces, (++ interface_count) * sizeof *interfaces);
				interfaces[interface_count - 1] = (struct interface) {
					.name = optarg
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
			case 'f':
				image_path = optarg;
				break;
			case 'v':
				fw_version = optarg;
				break;
			case '?':
			case 'h':
				puts("Small Modem for Router Turris daemon\n");
				puts("-e <interface>\n");
				puts("-c <vlan> <vpi> <vci>\n");
				puts("-f <firmware_image>\n");
				puts("-v <firmware_version>\n");
				exit(1);
		}
	}
	for (size_t i = 0; i < interface_count; i ++) {
		struct interface *ifc = &interfaces[i];
		for (size_t j = 0; j < ifc->mapping_count; j++)
			if (!ifc->mappings[j].active)
				die("Inactive connection %zu vlan %d on interface %s\n", j, ifc->mappings[j].vlan, ifc->name);
	}
	if (!image_path)
		die("The firmware image not set\n");
	if (!fw_version)
		die("The firmware version not set\n");
}

const struct conn_mapping *iface_conns(const char *iface) {
	for (size_t i = 0; i < interface_count; i ++)
		if (strcmp(interfaces[i].name, iface) == 0)
			return interfaces[i].mappings;
	return NULL;
}
