#include "automaton.h"
#include "proto_const.h"
#include "util.h"

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

// TODO: Load from configuration
#define IMAGE_PATH "/home/vorner/g2_cpe-vdsl-ok.bin"
#define FW_VERSION "1.03.15"

enum action {
	AC_ENTER,
	AC_TIMEOUT,
	AC_PACKET
};

struct action_def {
	const struct transition *(*hook)(struct extra_state *state, const void *packet, size_t packet_size);
	struct transition value;
};

static const struct transition *hook_ignore(struct extra_state *state, const void *packet, size_t packet_size) {
	(void)state;
	(void)packet;
	(void)packet_size;
	return NULL;
}

#define ACTION_IGNORE { .hook = hook_ignore }

struct node_def {
	struct action_def actions[3];
};

struct extra_state {
	int image_fd;
	uint32_t image_offset;
};

static const uint8_t ask_present_pkt[] = { CMD_GET_PARAM /* Get value */, 0x00, 0x04 /* 4 bytes of value name */, 0x00, 0x01 /* Seq */, 0x00, 0x00, 0x00, PARAM_PM /* The PM value (just something that is available even without the image) */ };
static const uint8_t ask_version[] = { CMD_GET_PARAM, 0x00, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00, PARAM_VERSION };
static const uint8_t enable_link[] = { CMD_SET_PARAM, 0x00, 0x05, 0x00, 0x03, 0x00, 0x00, 0x00, PARAM_LINK, 0x01 };
static const uint8_t ask_state[] = { CMD_GET_PARAM, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, PARAM_STATUS };

static struct transition reset_transition = {
	.new_state = AS_RESET,
	.state_change = true
};

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
		return &reset_transition;
	} else {
		struct extra_state *new_state = malloc(sizeof *new_state);
		if ((new_state->image_fd = open(IMAGE_PATH, O_RDONLY)) == -1)
			die("Couldn't open %s: %s\n", IMAGE_PATH, strerror(errno));
		new_state->image_offset = 0;
		static struct transition result = {
			.new_state = AS_SEND_FIRMWARE,
			.state_change = true
		};
		result.extra_state = new_state;
		return &result;
	}
}

struct file_offer {
	uint8_t cmd;
	uint8_t findex;
	uint32_t fsize;
	uint8_t ftype;
} __attribute__((packed));

static const struct transition *prepare_image_offer(struct extra_state *state, const void *packet, size_t packet_size) {
	(void)state;
	(void)packet;
	(void)packet_size;
	static struct file_offer offer = {
		.cmd = CMD_OFFER_IMAGE
	};
	struct stat st;
	if (stat(IMAGE_PATH, &st) == -1)
		die("Couldn't stat %s: %s\n", IMAGE_PATH, strerror(errno));
	offer.fsize = htonl(st.st_size);
	static struct transition result = {
		.timeout = 50,
		.timeout_mult = 2,
		.retries = 2,
		.timeout_set = true,
		.packet = (uint8_t *)&offer,
		.packet_size = sizeof offer,
		.packet_send = true
	};
	return &result;
}

struct image_part {
	uint8_t cmd;
	uint32_t offset;
	uint32_t size;
	uint8_t data[MAX_DATA_PAYLOAD];
} __attribute__((packed));

static const struct transition *send_image_part(struct extra_state *state, const void *packet, size_t packet_size) {
	(void)packet;
	(void)packet_size;
	assert(state);
	assert(state->image_fd != -1);
	if (lseek(state->image_fd, state->image_offset, SEEK_SET) == (off_t)-1)
		die("Couldn't seek in firmware image %s to location %zd: %s\n", IMAGE_PATH, (ssize_t)state->image_offset, strerror(errno));
	static struct image_part part = {
		.cmd = CMD_IMG_DATA
	};
	ssize_t amount = read(state->image_fd, part.data, sizeof part.data);
	if (amount == -1)
		die("Couldn't read data from firmware image %s at location %zd: %s\n", IMAGE_PATH, (ssize_t)state->image_offset, strerror(errno));
	part.offset = htonl(state->image_offset);
	part.size = htonl(amount);
	static struct transition result = {
		.timeout = 50,
		.timeout_mult = 2,
		.retries = 2,
		.timeout_set = true,
		.packet = (uint8_t *)&part,
		.packet_send = true
	};
	// Don't send the empty data at the end
	result.packet_size = sizeof part + amount - MAX_DATA_PAYLOAD;
	result.extra_state = state;
	return &result;
}

static const struct transition *check_image_ack(struct extra_state *state, const void *packet, size_t packet_size) {
	const struct img_ack *ack = packet;
	if (packet_size < sizeof *ack) // Too short to be the right kind of packet
		return NULL;
	if (ack->cmd != CMD_IMG_ACK) // Wrong type of packet
		return NULL;
	uint32_t status = ntohl(ack->status);
	if (status <= IMG_MAX_ACK) {
		// Acked a packet, move to the next one
		state->image_offset = status;
		static struct transition result = {
			.new_state = AS_SEND_FIRMWARE,
			.state_change = true
		};
		result.extra_state = state;
		return &result;
	} else {
		static struct transition result = {
			.state_change = true
			// Don't set the state - it'll be automatically destroyed by action()
		};
		// If it is successful, proceed to confirming it talks and has the correct version. Otherwise, try offering the image again, maybe it'll work this time
		result.new_state = (status == IMG_COMPLETE) ? AS_ASKED_VERSION : AS_ASKED_WANT_IMAGE;
		return &result;
	}
}

struct version {
	uint8_t cmd;
	uint16_t len;
	uint16_t seq;
	uint32_t param;
	char fw[20];
	char dsp[20];
} __attribute__((packed));

static const struct transition *check_version(struct extra_state *state, const void *packet, size_t packet_size) {
	(void)state;
	const struct version *version = packet;
	if (packet_size < sizeof *version)
		return NULL; // Too short a packet
	if (version->cmd != CMD_ANSWER_PARAM || ntohl(version->param) != PARAM_VERSION)
		return NULL; // Wrong packet
	if (strcmp(version->fw, FW_VERSION) != 0) {
		// Wrong version if image, reset it and load a new one
		return &reset_transition;
	} else {
		// All is OK, proceed to setting config
		static struct transition result = {
			.new_state = AS_SEND_CONFIG,
			.state_change = true
		};
		return &result;
	}
}

struct param_ack {
	uint8_t cmd;
	uint16_t len;
	uint16_t seq;
	uint8_t error;
} __attribute__((packed));

static const struct transition *check_link_ack(struct extra_state *state, const void *packet, size_t packet_size) {
	(void)state;
	const struct param_ack *ack = packet;
	if (packet_size < sizeof *ack)
		return NULL;
	if (ack->cmd != CMD_PARAM_ACK || ntohs(ack->seq) != 3)
		return NULL;
	if (ack->error) {
		// It refused to turn on the link. Therefore we try restarting the whole thing.
		return &reset_transition;
	} else {
		static struct transition result = {
			.new_state = AS_WATCH, // It is brought up, we just want to check from time to time it is still operational
			.state_change = true
		};
		return &result;
	}
}

struct state {
	uint8_t cmd;
	uint16_t len;
	uint16_t seq;
	uint32_t param;
	uint16_t modulation;
	uint8_t state;
	// There are other items, but we're not interested in them. So we may as well ignore them.
} __attribute__((packed));

static const struct transition *check_state(struct extra_state *state, const void *packet, size_t packet_size) {
	(void)state;
	const struct state *st = packet;
	if (packet_size < sizeof *st)
		return NULL;
	if (st->cmd != CMD_ANSWER_PARAM || ntohs(st->seq) != 4 || ntohl(st->param) != PARAM_STATUS)
		return NULL;
	// If it is in up state, then everything is nice
	if (st->state == STATE_OK) {
		static struct transition result = {
			.new_state = AS_WATCH,
			.state_change = true
		};
		return &result;
	} else {
		/*
		 * Otherwise, it's some bad state (some that shoundn't be kept forever,
		 * like training or handshaking - that should last for a short time).
		 *
		 * We ignore this packet and wait for one with a better state or time out.
		 */
		return NULL;
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
			},
			[AC_PACKET] = ACTION_IGNORE
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
				.hook = prepare_image_offer
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
	[AS_SEND_FIRMWARE] = {
		.actions = {
			[AC_ENTER] = {
				.hook = send_image_part
			},
			[AC_TIMEOUT] = {
				.value = {
					// Timed out sending data. So retry from the start, asking if it is still there.
					.new_state = AS_ASKED_PRESENT,
					.state_change = true
				}
			},
			[AC_PACKET] = {
				.hook = check_image_ack
			}
		}
	},
	[AS_ASKED_VERSION] = {
		.actions = {
			[AC_ENTER] = {
				.value = {
					.timeout = 100,
					.timeout_mult = 2,
					.retries = 4,
					.timeout_set = true,
					.packet = ask_version,
					.packet_size = sizeof ask_version,
					.packet_send = true
				}
			},
			[AC_TIMEOUT] = {
				.value = { // Not answering version. Is it still there?
					.new_state = AS_ASKED_PRESENT,
					.state_change = true
				}
			},
			[AC_PACKET] = {
				.hook = check_version
			}
		}
	},
	[AS_SEND_CONFIG] = {
		.actions = {
			[AC_ENTER] = {
				// This one is empty for now. Once we have ADSL, it'll do something.
				.value = {
					.new_state = AS_WAIT_CONFIG,
					.state_change = true
				}
			}
		}
	},
	[AS_WAIT_CONFIG] = {
		.actions = {
			[AC_ENTER] = {
				.value = {
					.timeout = 100,
					.timeout_set = true
				}
			},
			[AC_TIMEOUT] = {
				.value = {
					.new_state = AS_ENABLE_LINK,
					.state_change = true
				}
			},
			[AC_PACKET] = ACTION_IGNORE
		}
	},
	[AS_ENABLE_LINK] = {
		.actions = {
			[AC_ENTER] = {
				.value = {
					.timeout = 50,
					.timeout_mult = 2,
					.retries = 2,
					.timeout_set = true,
					.packet = enable_link,
					.packet_size = sizeof enable_link,
					.packet_send = true
				}
			},
			[AC_TIMEOUT] = {
				.value = {
					// If we can't turn it on, ask it once again if it is alive by asking for its version
					.new_state = AS_ASKED_VERSION,
					.state_change = true
				}
			},
			[AC_PACKET] = {
				.hook = check_link_ack
			}
		}
	},
	[AS_WATCH] = {
		.actions = {
			[AC_ENTER] = {
				.value = {
					.timeout = 60 * 1000, // Ask for alive-status every minute
					.timeout_set = true
				}
			},
			[AC_TIMEOUT] = {
				.value = {
					.new_state = AS_CONFIRM_WORKING,
					.state_change = true
				}
			},
			[AC_PACKET] = ACTION_IGNORE
		}
	},
	[AS_CONFIRM_WORKING] = {
		.actions = {
			[AC_ENTER] = {
				.value = {
					// Ask every quarter of minute, up to 5 minutes
					.timeout = 15 * 1000,
					.timeout_mult = 1,
					.retries = 19,
					.timeout_set = true,
					.packet = ask_state,
					.packet_size = sizeof ask_state,
					.packet_send = true
				}
			},
			[AC_TIMEOUT] = {
				.value = {
					// If it doesn't work for 5 minutes, try resetting it completely
					.new_state = AS_RESET,
					.state_change = true
				}
			},
			[AC_PACKET] = {
				.hook = check_state
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
	if (state) {
		if (state->image_fd != -1)
			if (close(state->image_fd) == -1)
				die("Couldn't close FD %d: %s\n", state->image_fd, strerror(errno));
		free(state);
	}
}
