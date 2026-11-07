#ifndef PC_OBSERVE_H
#define PC_OBSERVE_H
/* Optional observers, absent when extras are disabled. No game state writes. */
void pcObserveOp(int field, int actor, int slot, int routine, int pc, int op, int ext) __attribute__((weak));
void pcObserveWindow(const void* window, int field, int actor, int dialog) __attribute__((weak));
void pcObserveText(const void* window) __attribute__((weak));
void pcObserveLine(const void* window, const void* image, int stride, int x, int width, int line, int odd) __attribute__((weak));
#endif
