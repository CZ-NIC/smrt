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
	// Send the config
	AS_SEND_CONFIG,
	// Wait for config to be swallowed (it answers it enables the link, but does nothing at all)
	AS_WAIT_CONFIG,
	// Enable the link
	AS_ENABLE_LINK,
	// Watch it is still operating
	AS_WATCH,
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

const struct transition *state_enter(enum autom_state state, struct extra_state *extra_state);
const struct transition *state_timeout(enum autom_state state, struct extra_state *extra_state);
const struct transition *state_packet(enum autom_state, struct extra_state *extra_state, const void *packet, size_t packet_size);
void extra_state_destroy(struct extra_state *state);

#endif
