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
// Path where to put files describing status
extern const char *status_path;

// What is the path to status file for given interface. The result is freed by the next call to this function.
const char *interface_status_path(const char *interface);

#endif
