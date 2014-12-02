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

#ifndef SMRT_PROTO_CONST_H
#define SMRT_PROTO_CONST_H

#define CONTROL_PROTOCOL 0x8889

#define CMD_OFFER_IMAGE 1
#define CMD_IMG_DATA 2
#define CMD_IMG_ACK 3
#define CMD_GET_PARAM 5
#define CMD_ANSWER_PARAM 6
#define CMD_SET_PARAM 7
#define CMD_PARAM_ACK 8

#define PARAM_RESET 0x00
#define PARAM_CONN 0x01
// Now there are 7 more connections
#define PARAM_MODE 0x0B
#define PARAM_PM 0x0F
#define PARAM_STATUS 0x10
#define PARAM_LINK 0x1D
#define PARAM_VERSION 0x20

#define IMG_PROCEED 0xFFFFFFFF
#define IMG_COMPLETE 0xFFFFFFF9
#define IMG_MAX_ACK 0xFFFFFF00

#define L2_ATM 1
#define TRAFFIC_EOA 0
#define ENCAP_LLC 1
#define QOS_DISABLE 6

#define STATE_OK 3

// This fits into the ethernet MTU (1500) and is number divisible by 4 (it doesn't work otherwise and freezes).
#define MAX_DATA_PAYLOAD 1488

#endif
