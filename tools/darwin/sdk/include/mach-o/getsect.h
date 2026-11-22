#ifndef FAKE_GETSECT_H
#define FAKE_GETSECT_H
#include <stdint.h>
struct mach_header_64;
extern uint8_t *getsegmentdata(const struct mach_header_64 *, const char *, unsigned long *);
#endif
