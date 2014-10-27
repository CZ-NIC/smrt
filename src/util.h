#ifndef SMRT_UTIL_H
#define SMRT_UTIL_H

void die(const char *message, ...) __attribute__((noreturn)) __attribute__((format(printf, 1, 2)));
void dbg(const char *message, ...) __attribute__((format(printf, 1, 2)));

#endif
