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
