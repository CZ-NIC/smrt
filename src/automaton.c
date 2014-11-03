#include "automaton.h"
#include "proto_const.h"

#include <arpa/inet.h>

enum action {
	AC_ENTER,
	AC_TIMEOUT,
	AC_PACKET
};

struct action_def {
	const struct transition *(*hook)(struct extra_state *state, const void *packet, size_t packet_size);
	struct transition value;
};

struct node_def {
	struct action_def actions[3];
};

static const uint8_t ask_present_pkt[] = { CMD_GET_PARAM /* Get value */, 0x00, 0x04 /* 4 bytes of value name */, 0x00, 0x01 /* Seq */, 0x00, 0x00, 0x00, 0x0f /* The PM value (just something that is available even without the image) */ };
// TODO: This needs to be computed, the file size is variable, once we have some kind of configuration.
static const uint8_t ask_want_image[] = { CMD_OFFER_IMAGE /* Offer a file */, 0x00 /* File index (what is that?) */, 0x00, 0x1D, 0x2E, 0xC9 /* File size */, 0x00 /* File is firmware */ };

struct param_answer {
	uint8_t cmd;
	uint16_t len;
	uint16_t seq;
	uint32_t type;
	uint8_t data[];
} __attribute__((packed));

static const struct transition *check_presence_answer(struct extra_state *state, const void *packet, size_t packet_size) {
	(void)state;
	const struct param_answer *answer = packet;
	if (packet_size < sizeof *answer)
		return NULL; // Message too short
	if (answer->cmd != CMD_ANSWER_PARAM || ntohs(answer->seq) != 1 || ntohl(answer->type) != PARAM_PM)
		return NULL;
	static struct transition result = {
		.new_state = AS_ASKED_WANT_IMAGE,
		.state_change = true
	};
	return &result;
}

struct img_ack {
	uint8_t cmd;
	uint32_t status;
} __attribute__((packed));

static const struct transition *check_want_image_answer(struct extra_state *state, const void *packet, size_t packet_size) {
	(void)state;
	const struct img_ack *ack = packet;
	if (packet_size < sizeof *ack) // Too short to be the right kind of packet
		return NULL;
	if (ack->cmd != CMD_IMG_ACK) // Wrong type of packet
		return NULL;
	if (ntohl(ack->status) != IMG_PROCEED) {
		// There was an error. But it shouldn't refuse to upload an image (it may ignore the offer), try reseting it and start again once more.
		static struct transition result = {
			.new_state = AS_RESET,
			.state_change = true
		};
		return &result;
	} else {
		static struct transition result = {
			.new_state = AS_SEND_FIRMWARE,
			.state_change = true
		};
		return &result;
	}
}

static struct node_def defs[] = {
	[AS_PRESTART] = {
		.actions = {
			// Just move to initial state
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
			// Send a packet with query. If it answers, it's there. If not, it's dead.
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
			},
			[AC_PACKET] = {
				.hook = check_presence_answer
			}
		}
	},
	[AS_ASKED_WANT_IMAGE] = {
		.actions = {
			// Send a offer of firmware. If it answers, it wants one. If it doesn't, it is probably already loaded. Just confirm and continue.
			[AC_ENTER] = {
				.value = {
					.timeout = 50,
					.timeout_mult = 2,
					.retries = 2,
					.timeout_set = true,
					.packet = ask_want_image,
					.packet_size = sizeof ask_want_image,
					.packet_send = true
				}
			},
			[AC_TIMEOUT] = {
				.value = {
					.new_state = AS_ASKED_VERSION,
					.state_change = true
				}
			},
			[AC_PACKET] = {
				.hook = check_want_image_answer
			}
		}
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
	if (result && result->extra_state != extra_state)
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
