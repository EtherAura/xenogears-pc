#ifndef POLLHOST_H
#define POLLHOST_H

/*
 * POLLHOST(): the SN Systems debug-stub poll, a `break 0x400` (disassemblers
 * print it as `break 1`) that lets a PC-hosted debugger (PC-link / DTL-H)
 * service host requests, like PSY-Q's pollhost(). A single instruction C
 * cannot express; sanctioned inline asm like include/gte.h and
 * include/stack.h (user decision).
 */
/* No SN debugger on the PC. */
#define POLLHOST() ((void)0)

#endif /* POLLHOST_H */
