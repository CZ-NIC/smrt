#ifndef SMRT_NETSTATE_H
#define SMRT_NETSTATE_H

// Initialize the module.
void netstate_init(void);
// Scan the interfaces and look if there's any change in the link state
void netstate_update(void);
// Add another interface to be watched (if it exists)
void netstate_add(const char *name);

#endif
