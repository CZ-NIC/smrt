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

#ifndef SMRT_NETSTATE_H
#define SMRT_NETSTATE_H

// Initialize the module.
void netstate_init(void);
// Scan the interfaces and look if there's any change in the link state
void netstate_update(void);
// Add another interface to be watched (if it exists)
void netstate_add(const char *name);
// Mark the interface as down externally
void netstate_down(const char *name);

typedef void (*link_hook)(const char *ifname);
void netstate_set_hooks(link_hook up, link_hook down);

#endif
