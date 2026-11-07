/*
 * The soft reset. Start+Select+L1+R1 (the game's softReboot) shuts the
 * hardware down and calls startKernel, the PS1 executable's entry point: it
 * clears main's BSS (0x800592BC-0x8006FAF0 US), resets the stack and enters
 * kernelMain. Main's data keeps what the game wrote into it, as on the PS1.
 *
 * On the PC, startKernel unwinds to main() (port/src/main.c), which calls
 * portSoftReset and then kernelMain again. The PsyQ libraries keep their
 * state in main's BSS on the PS1; here the platform layer holds it, and what
 * the game's re-initialisation expects to find cleared is cleared too:
 * LIBETC's, so ResetCallback starts afresh (callbacks, the vblank count).
 * The hardware (VRAM, SPU RAM, the drive, the clock) carries on, as it does
 * on the PS1.
 */

#include "cd/regional.h"
#include <stdio.h>

#include "system.h"

void portClearModuleBss(const char* name);
void portStartPrepare(void); /* options.c: a start the extras asked for (its disc) */

void portSoftReset(void) {
    regionalMovieEnd();
    fprintf(stderr, "xenogears: soft reset\n");
    portClearModuleBss("main");
    etcSoftReset();
    portStartPrepare();
}
