#ifndef SMRT_PROTO_CONST_H
#define SMRT_PROTO_CONST_H

#define CMD_OFFER_IMAGE 1
#define CMD_IMG_DATA 2
#define CMD_IMG_ACK 3
#define CMD_GET_PARAM 5
#define CMD_ANSWER_PARAM 6
#define CMD_SET_PARAM 7
#define CMD_PARAM_ACK 8

#define PARAM_PM 0x0F
#define PARAM_LINK 0x1D
#define PARAM_VERSION 0x20

#define IMG_PROCEED 0xFFFFFFFF
#define IMG_COMPLETE 0xFFFFFFF9
#define IMG_MAX_ACK 0xFFFFFF00

// This fits into the ethernet MTU (1500) and is number divisible by 4 (it doesn't work otherwise and freezes).
#define MAX_DATA_PAYLOAD 1488

#endif