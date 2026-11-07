/* The invariant log, XG_INVARIANT_LOG (invariants.c): the game's state per
 * vblank as text, for comparing two runs (tools/engine_diff.py). */
#ifndef XG_INVARIANTS_H
#define XG_INVARIANTS_H

#include <stdint.h>

int invariantLogging(void);                                        /* a log file is open */
void invariantVBlank(void);                                        /* hwWaitVSync: the per-vblank line */
void invariantCardWrite(int slot, int sector, const uint8_t* frame); /* bu.c: a memory-card frame written */

#endif
