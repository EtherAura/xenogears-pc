/*
 * Small hand-written routines of main (config/handwritten.yaml): a memory
 * clear from the startup code and three stubs at 0x800379B4.
 */
#include "common.h"

void logToScreen(const char* fmt, ...);

extern s32 battlingMainTaskIndex; /* index into battling's main task table */

/* The startup code's clear loop as a routine: zeroes the words after start up
 * to and including end (end is cleared, start is not), nothing when they are
 * equal. Addresses are host addresses on the PC. */
void clearMemoryRange(uptr start, uptr end) {
    u32* p = (u32*)start;

    while (p != (u32*)end) {
        *++p = 0;
    }
}

void setBattlingMainTaskIndex(s32 index) {
    battlingMainTaskIndex = index;
}

/* A tail jump to logToScreen with the caller's arguments untouched. C cannot
 * pass "..." on, so the PC forwards the format and six argument words: every
 * call site passes at most five integers or pointers after the format, and
 * logToScreen reads only as many as its format names. */
void sprintf_screen(const char* fmt, ...) {
    __builtin_va_list ap;
    sptr a[6];
    s32 i;

    __builtin_va_start(ap, fmt);
    for (i = 0; i < 6; i++) {
        a[i] = __builtin_va_arg(ap, sptr);
    }
    __builtin_va_end(ap);
    logToScreen(fmt, a[0], a[1], a[2], a[3], a[4], a[5]);
}

/* An empty debug hook (jr $ra); the PS1 returns whatever $v0 held. */
s32 debugFontNop(void) {
    return 0;
}
