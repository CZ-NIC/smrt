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

#include "automaton.h"
#include "proto_const.h"
#include "util.h"
#include "configuration.h"

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

enum action {
	AC_ENTER,
	AC_TIMEOUT,
	AC_PACKET
};

struct action_def {
	const struct transition *(*hook)(const char *ifname, struct extra_state *state, const void *packet, size_t packet_size);
	struct transition value;
};

static const struct transition *hook_ignore(const char *ifname, struct extra_state *state, const void *packet, size_t packet_size) {
	(void)ifname;
	(void)state;
	(void)packet;
	(void)packet_size;
	return NULL;
}

#define ACTION_IGNORE { .hook = hook_ignore }
#define ACTION_ASK_PRESENT { .value = { .new_state = AS_ASKED_PRESENT, .state_change = true } }

struct node_def {
	struct action_def actions[3];
};

struct extra_state {
	int image_fd;
	uint32_t image_offset;
	size_t conn_index;
};

static const uint8_t ask_present_pkt[] = { CMD_GET_PARAM /* Get value */, 0x00, 0x04 /* 4 bytes of value name */, 0x00, 0x01 /* Seq */, 0x00, 0x00, 0x00, PARAM_PM /* The PM value (just something that is available even without the image) */ };
static const uint8_t ask_version[] = { CMD_GET_PARAM, 0x00, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00, PARAM_VERSION };
static const uint8_t enable_link[] = { CMD_SET_PARAM, 0x00, 0x05, 0x00, 0x03, 0x00, 0x00, 0x00, PARAM_LINK, 0x01 };
static const uint8_t ask_state[] = { CMD_GET_PARAM, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, PARAM_STATUS };
static const uint8_t cmd_reset[] = { CMD_SET_PARAM, 0x00, 0x05, 0x00, 0x05, 0x00, 0x00, 0x00, PARAM_RESET, 0x01 };
// List allowed modes. This allows them all.
static const uint8_t set_mode[] = { CMD_SET_PARAM, 0x00, 0x08, 0x00, 0x06, 0x00, 0x00, 0x00, PARAM_MODE, 0x00, 0x3F, 0x00, 0xF3 };

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

static const struct transition *check_presence_answer(const char *ifname, struct extra_state *state, const void *packet, size_t packet_size) {
	(void)ifname;
	(void)state;
	const struct param_answer *answer = packet;
	if (packet_size < sizeof *answer)
		return NULL; // Message too short
	if (answer->cmd != CMD_ANSWER_PARAM || ntohs(answer->seq) != 1 || ntohl(answer->type) != PARAM_PM)
		return NULL;
	msg("Modem seems to be present\n");
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

static const struct transition *check_want_image_answer(const char *ifname, struct extra_state *state, const void *packet, size_t packet_size) {
	(void)ifname;
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
		if ((new_state->image_fd = open(image_path, O_RDONLY)) == -1)
			die("Couldn't open %s: %s\n", image_path, strerror(errno));
		new_state->image_offset = 0;
		msg("Sending firmware");
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

static const struct transition *prepare_image_offer(const char *ifname, struct extra_state *state, const void *packet, size_t packet_size) {
	(void)ifname;
	(void)state;
	(void)packet;
	(void)packet_size;
	static struct file_offer offer = {
		.cmd = CMD_OFFER_IMAGE
	};
	struct stat st;
	if (stat(image_path, &st) == -1)
		die("Couldn't stat %s: %s\n", image_path, strerror(errno));
	offer.fsize = htonl(st.st_size);
	static struct transition result = {
		.timeout = 50,
		.timeout_mult = 2,
		.retries = 2,
		.timeout_set = true,
		.status_name = "upload firmware",
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

static const struct transition *send_image_part(const char *ifname, struct extra_state *state, const void *packet, size_t packet_size) {
	(void)ifname;
	(void)packet;
	(void)packet_size;
	assert(state);
	assert(state->image_fd != -1);
	if (lseek(state->image_fd, state->image_offset, SEEK_SET) == (off_t)-1)
		die("Couldn't seek in firmware image %s to location %zd: %s\n", image_path, (ssize_t)state->image_offset, strerror(errno));
	static struct image_part part = {
		.cmd = CMD_IMG_DATA
	};
	ssize_t amount = read(state->image_fd, part.data, sizeof part.data);
	if (amount == -1)
		die("Couldn't read data from firmware image %s at location %zd: %s\n", image_path, (ssize_t)state->image_offset, strerror(errno));
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

static const struct transition *check_image_ack(const char *ifname, struct extra_state *state, const void *packet, size_t packet_size) {
	(void)ifname;
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

static const struct transition *check_version(const char *ifname, struct extra_state *state, const void *packet, size_t packet_size) {
	(void)ifname;
	(void)state;
	const struct version *version = packet;
	if (packet_size < sizeof *version)
		return NULL; // Too short a packet
	if (version->cmd != CMD_ANSWER_PARAM || ntohl(version->param) != PARAM_VERSION)
		return NULL; // Wrong packet
	if (strcmp(version->fw, fw_version) != 0) {
		// Wrong version if image, reset it and load a new one
		return &reset_transition;
	} else {
		// All is OK, proceed to setting config
		static struct transition result = {
			.new_state = AS_WAIT_BEFORE_CONFIG,
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

static const struct transition *check_ack(const char *ifname, struct extra_state *state, const void *packet, size_t packet_size, uint16_t seq, enum autom_state new_state) {
	(void)ifname;
	const struct param_ack *ack = packet;
	if (packet_size < sizeof *ack)
		return NULL;
	if (ack->cmd != CMD_PARAM_ACK || ntohs(ack->seq) != seq)
		return NULL;
	if (ack->error) {
		// It refused to turn on the link. Therefore we try restarting the whole thing.
		return &reset_transition;
	} else {
		static struct transition result = {
			.state_change = true
		};
		result.new_state = new_state;
		result.extra_state = state;
		return &result;
	}
}

static const struct transition *check_link_ack(const char *ifname, struct extra_state *state, const void *packet, size_t packet_size) {
	return check_ack(ifname, state, packet, packet_size, 3, AS_FIRST_START);
}

static const struct transition *check_mode_ack(const char *ifname, struct extra_state *state, const void *packet, size_t packet_size) {
	return check_ack(ifname, state, packet, packet_size, 6, AS_SEND_CONFIG_CONN);
}

struct state {
	uint8_t cmd;
	uint16_t len;
	uint16_t seq;
	uint32_t param;
	uint8_t annex;
	uint8_t standard;
	uint8_t state;
	uint8_t power;
	uint8_t data_path;
	uint32_t dsmax;
	uint32_t usmax;
	uint32_t dscur;
	uint32_t uscur;
	uint16_t dspower;
	uint16_t uspower;
	// There are other items, but we're not interested in them. So we may as well ignore them.
} __attribute__((packed));

static const char *const states[] = {
	"idle",
	"handshake",
	"training",
	"online",
	"no signal",
	"CRC error",
	"disabled"
};

static const char *const standards[] = {
	"T1.413",
	"G992_1",
	"G992_2",
	"G992_3",
	"G992_4",
	"G992_5",
	"G993_2"
};

static const char *const annexes[] = {
	[0] = "None",
	[1] = "A",
	[1 << 1] = "B",
	[1 << 2] = "C",
	[1 << 3] = "I",
	[1 << 4] = "J",
	[1 << 5] = "L1",
	[1 << 6] = "L2",
	[1 << 7] = "M"
};

static const struct transition *check_state(const char *ifname, struct extra_state *state, const void *packet, size_t packet_size) {
	(void)ifname;
	const struct state *st = packet;
	if (packet_size < sizeof *st)
		return NULL;
	if (st->cmd != CMD_ANSWER_PARAM || ntohs(st->seq) != 4 || ntohl(st->param) != PARAM_STATUS)
		return NULL;
	// If it is in up state, then everything is nice
	const char *path = interface_status_path(ifname);
	FILE *f = fopen(path, "w");
	if (!f)
		die("Couldn't write interface state file %s: %s\n", path, strerror(errno));
	assert(st->state < sizeof states / sizeof *states);
	fprintf(f, "<status>%s</status>\n", states[st->state]);
	if (st->standard < sizeof standards / sizeof *standards)
		fprintf(f, "<standard>%s</standard>\n", standards[st->standard]);
	if (st->annex < sizeof annexes / sizeof *annexes)
		fprintf(f, "<annex>%s</annex>\n", annexes[st->annex]);
	fprintf(f, "<power-state>%hhu</power-state>\n", st->power);
	fprintf(f, "<max-speed><down>%u</down><up>%u</up></max-speed>\n", ntohl(st->dsmax), ntohl(st->usmax));
	fprintf(f, "<cur-speed><down>%u</down><up>%u</up></cur-speed>\n", ntohl(st->dscur), ntohl(st->uscur));
	fprintf(f, "<power><down>%u</down><up>%u</up></power>\n", ntohs(st->dspower), ntohs(st->uspower));
	if (fclose(f) == EOF)
		die("Couldn't close interface state file %s: %s\n", path, strerror(errno));
	if (st->state == STATE_OK) {
		static struct transition result = {
			.new_state = AS_WATCH,
			.state_change = true
		};
		result.extra_state = state;
		msg("Modem is running\n");
		return &result;
	} else {
		dbg("In state %hhu\n", st->state);
		/*
		 * Otherwise, it's some bad state (some that shoundn't be kept forever,
		 * like training or handshaking - that should last for a short time).
		 *
		 * We ignore this packet and wait for one with a better state or time out.
		 */
		return NULL;
	}
}

struct conn_params {
	uint8_t command;
	uint16_t len;
	uint16_t seq;
	uint32_t param;
	uint8_t enable;
	uint8_t l2mode;
	uint8_t traffic_type;
	uint8_t encap_mode;
	uint8_t qos;
	uint32_t pcr;
	uint32_t scr;
	uint32_t mbs;
	uint32_t mcr;
	uint16_t vpi;
	uint16_t vci;
	uint16_t vlan;
	uint8_t vlan_flag;
} __attribute__((packed));

static const struct transition *send_conn(const char *ifname, struct extra_state *state, const void *packet, size_t packet_size) {
	(void)packet;
	(void)packet_size;
	if (!state) {
		state = malloc(sizeof *state);
		*state = (struct extra_state) {
			.image_fd = -1
		};
	}
	const struct conn_mapping *conns = iface_conns(ifname);
	assert(conns);
	static struct conn_params params;
	params = (struct conn_params) {
		.command = CMD_SET_PARAM,
		.len = htons(sizeof params - 5),
		.seq = htons(7 + state->conn_index),
		.param = htonl(PARAM_CONN + state->conn_index),
		.enable = conns[state->conn_index].active,
		.l2mode = L2_ATM,
		.traffic_type = TRAFFIC_EOA,
		.encap_mode = ENCAP_LLC,
		.qos = QOS_DISABLE,
		// TODO PCR?
		// TODO SCR?
		// TODO MBS?
		// TODO MCR?
		.vpi = htons(conns[state->conn_index].vpi),
		.vci = htons(conns[state->conn_index].vci),
		.vlan = htons(conns[state->conn_index].vlan),
		.vlan_flag = 0
	};
	static struct transition result = {
		.timeout = 500, // This operation seems to be really slow, so give it time
		.timeout_mult = 2,
		.retries = 3,
		.timeout_set = true,
		.packet = (void *)&params,
		.packet_size = sizeof params,
		.packet_send = true
	};
	result.extra_state = state;
	msg("Sending config\n");
	return &result;
}

static const struct transition *check_conn_ack(const char *ifname, struct extra_state *state, const void *packet, size_t packet_size) {
	assert(state);
	const struct transition *result = check_ack(ifname, state, packet, packet_size, 7 + state->conn_index, AS_SEND_CONFIG_CONN);
	state->conn_index ++;
	if (result && state->conn_index == MAX_CONN_CNT) {
		static struct transition next = {
			.new_state = AS_WAIT_CONFIG,
			.state_change = true
		};
		return &next;
	}
	return result;
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
			[AC_TIMEOUT] = ACTION_ASK_PRESENT,
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
					.status_name = "presence query",
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
			[AC_TIMEOUT] = ACTION_ASK_PRESENT,
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
					.status_name = "version query",
					.packet = ask_version,
					.packet_size = sizeof ask_version,
					.packet_send = true
				}
			},
			[AC_TIMEOUT] = ACTION_ASK_PRESENT,
			[AC_PACKET] = {
				.hook = check_version
			}
		}
	},
	[AS_WAIT_BEFORE_CONFIG] = {
		.actions = {
			[AC_ENTER] = {
				.value = {
					.timeout = 100,
					.timeout_set = true,
					.status_name = "config"
				}
			},
			[AC_TIMEOUT] = {
				.value = {
					.new_state = AS_SEND_CONFIG_MODE,
					.state_change = true
				}
			},
			[AC_PACKET] = ACTION_IGNORE
		}
	},
	[AS_SEND_CONFIG_MODE] = {
		.actions = {
			[AC_ENTER] = {
				// This one is empty for now. Once we have ADSL, it'll do something.
				.value = {
					.timeout = 100,
					.timeout_mult = 2,
					.retries = 4,
					.timeout_set = true,
					.packet = set_mode,
					.packet_size = sizeof set_mode,
					.packet_send = true
				}
			},
			[AC_TIMEOUT] = ACTION_ASK_PRESENT,
			[AC_PACKET] = {
				.hook = check_mode_ack
			}
		}
	},
	[AS_SEND_CONFIG_CONN] = {
		.actions = {
			[AC_ENTER] = {
				.hook = send_conn
			},
			[AC_TIMEOUT] = ACTION_ASK_PRESENT,
			[AC_PACKET] = {
				.hook = check_conn_ack
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
					.status_name = "activate",
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
					// Ask every second , up to 5 minutes
					.timeout = 1000,
					.timeout_mult = 1,
					.retries = 79,
					.timeout_set = true,
					.packet = ask_state,
					.packet_size = sizeof ask_state,
					.packet_send = true
				}
			},
			[AC_TIMEOUT] = {
				// If it doesn't work for 5 minutes, try resetting it completely
				.value = {
					.new_state = AS_RESET,
					.state_change = true
				}
			},
			[AC_PACKET] = {
				.hook = check_state
			}
		}
	},
	[AS_FIRST_START] = { // Just like AS_CONFIRM_WORKING, but asking more often and for longer time
		.actions = {
			[AC_ENTER] = {
				.value = {
					.timeout = 500,
					.timeout_mult = 1,
					.retries = 599, // Ask for whole 5 minutes
					.timeout_set = true,
					.packet = ask_state,
					.packet_size = sizeof ask_state,
					.packet_send = true
				}
			},
			[AC_TIMEOUT] = {
				.value = {
					.new_state = AS_RESET,
					.state_change = true
				}
			},
			[AC_PACKET] = {
				.hook = check_state
			}
		}
	},
	[AS_RESET] = {
		.actions = {
			/*
			 * Send two requests to reset, 20ms from each other (for the case one gets lost).
			 * Then just go to the initial state, don't expect answer from the modem whet
			 * it's being reset.
			 */
			[AC_ENTER] = {
				.value = {
					.timeout = 20,
					.timeout_add = -20,
					.retries = 1,
					.timeout_set = true,
					.status_name = "reset",
					.packet = cmd_reset,
					.packet_size = sizeof cmd_reset,
					.packet_send = true
				}
			},
			[AC_TIMEOUT] = {
				.value = {
					.new_state = AS_PRESTART,
					.state_change = true
				}
			},
			[AC_PACKET] = ACTION_IGNORE
		}
	},
	[AS_DEAD] = {
		.actions = {
			[AC_ENTER] = {
				.value = {
					.status_name = "not present"
				}
			}
		}
	}
};

static const struct transition *action(const char *ifname, enum autom_state state, enum action action, struct extra_state *extra_state, const void *packet, size_t packet_size) {
	struct action_def *ad = &defs[state].actions[action];
	const struct transition *result;
	if (ad->hook)
		result = ad->hook(ifname, extra_state, packet, packet_size);
	else
		result = &ad->value;
	if (result && result->extra_state != extra_state)
		extra_state_destroy(extra_state);
	return result;
}

const struct transition *state_enter(const char *ifname, enum autom_state state, struct extra_state *extra_state) {
	return action(ifname, state, AC_ENTER, extra_state, NULL, 0);
}

const struct transition *state_timeout(const char *ifname, enum autom_state state, struct extra_state *extra_state) {
	return action(ifname, state, AC_TIMEOUT, extra_state, NULL, 0);
}

const struct transition *state_packet(const char *ifname, enum autom_state state, struct extra_state *extra_state, const void *packet, size_t packet_size) {
	return action(ifname, state, AC_PACKET, extra_state, packet, packet_size);
}

void extra_state_destroy(struct extra_state *state) {
	if (state) {
		if (state->image_fd != -1)
			if (close(state->image_fd) == -1)
				die("Couldn't close FD %d: %s\n", state->image_fd, strerror(errno));
		free(state);
	}
}
