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

#ifndef SMRT_AUTOMATO_H
#define SMRT_AUTOMATO_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

enum autom_state {
	// We didn't do anything yet, just created the data structure
	AS_PRESTART,
	// We asked if the modem is there
	AS_ASKED_PRESENT,
	// We asked if there's an image loaded
	AS_ASKED_WANT_IMAGE,
	// We are sending the image now
	AS_SEND_FIRMWARE,
	// Confirm the thing is alive and if the version is OK
	AS_ASKED_VERSION,
	// Wait a little bit before feeding it with config
	AS_WAIT_BEFORE_CONFIG,
	// Send the config (the modulation mode)
	AS_SEND_CONFIG_MODE,
	// Send the config (the connection mappings)
	AS_SEND_CONFIG_CONN,
	// Wait for config to be swallowed (it answers it enables the link, but does nothing at all)
	AS_WAIT_CONFIG,
	// Enable the link
	AS_ENABLE_LINK,
	// We just started and wait for the first working state. We ask more often, mostly to make openwrt thing the link is not dead
	AS_FIRST_START,
	// Watch it is still operating
	AS_WATCH,
	// Query status and decide if everything is OK
	AS_CONFIRM_WORKING,
	// Send a reset command
	AS_RESET,
	// Not there or not responding, after fatal heart attack, whatever.
	AS_DEAD
};

struct extra_state;

struct transition {
	enum autom_state new_state;
	bool state_change;
	int timeout;
	int timeout_add;
	int timeout_mult;
	int retries;
	bool timeout_set;
	struct extra_state *extra_state;
	size_t packet_size;
	bool packet_send;
	const uint8_t *packet;
};

const struct transition *state_enter(const char *iface, enum autom_state state, struct extra_state *extra_state);
const struct transition *state_timeout(const char *iface, enum autom_state state, struct extra_state *extra_state);
const struct transition *state_packet(const char *iface, enum autom_state, struct extra_state *extra_state, const void *packet, size_t packet_size);
void extra_state_destroy(struct extra_state *state);

#endif
