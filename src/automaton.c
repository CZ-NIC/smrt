#include "automaton.h"

enum action {
	AC_ENTER,
	AC_TIMEOUT,
	AC_PACKET
};

struct action_def {
	struct transition *(*hook)(struct extra_state *state, const void *packet, size_t packet_size);
	struct transition value;
};

struct node_def {
	struct action_def actions[3];
};

static const uint8_t ask_present_pkt[] = { 0x05 /* Get value */, 0x00, 0x04 /* 4 bytes of value name */, 0x00, 0x01 /* Seq */, 0x00, 0x00, 0x00, 0x0f /* The PM value (just something that is available even without the image) */ };

static struct node_def defs[] = {
	[AS_PRESTART] = {
		.actions = {
			[AC_ENTER] = {
				.value = {
					.timeout_set = true
				}
			},
			[AC_TIMEOUT] = {
				.value = {
					.new_state = AS_ASKED_PRESENT,
					.state_change = true
				}
			}
		}
	},
	[AS_ASKED_PRESENT] = {
		.actions = {
			[AC_ENTER] = {
				.value = {
					.timeout = 100,
					.timeout_mult = 2,
					.retries = 5,
					.timeout_set = true,
					.packet = ask_present_pkt,
					.packet_size = sizeof ask_present_pkt,
					.packet_send = true
				}
			},
			[AC_TIMEOUT] = {
				.value = {
					.new_state = AS_DEAD,
					.state_change = true
				}
			}
		}
		// TODO: Handle the packet
	},
	[AS_DEAD] = {
		.actions = {
			[AC_ENTER] = {
				.hook = NULL // Just because we can't have empty initializer braces
			}
		}
	}
};

static const struct transition *action(enum autom_state state, enum action action, struct extra_state *extra_state, const void *packet, size_t packet_size) {
	struct action_def *ad = &defs[state].actions[action];
	const struct transition *result;
	if (ad->hook)
		result = ad->hook(extra_state, packet, packet_size);
	else
		result = &ad->value;
	if (result->extra_state != extra_state)
		extra_state_destroy(extra_state);
	return result;
}

const struct transition *state_enter(enum autom_state state, struct extra_state *extra_state) {
	return action(state, AC_ENTER, extra_state, NULL, 0);
}

const struct transition *state_timeout(enum autom_state state, struct extra_state *extra_state) {
	return action(state, AC_TIMEOUT, extra_state, NULL, 0);
}

const struct transition *state_packet(enum autom_state state, struct extra_state *extra_state, const void *packet, size_t packet_size) {
	return action(state, AC_PACKET, extra_state, packet, packet_size);
}

void extra_state_destroy(struct extra_state *state) {
	// TODO
}
