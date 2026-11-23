/*
 * Select as the port's chord button (selectchord.h; xg-mrj.6).
 *
 * Threads: the game thread only (pad.c's vblank). Host state: the player's
 * hands, not the game's, so never in a save state; a load starts from no
 * Select held back, as pad.c's own edges do.
 */

#include "selectchord.h"

#include <stddef.h>

/* battlespeed.c and backlog.c (weak: tests link this file without them). */
extern int speedControlHoldsSelect(void) __attribute__((weak));
extern int backlogHoldsSelect(void) __attribute__((weak));

#define PAD_L2 0x0001u
#define PAD_R2 0x0002u
#define PAD_L1 0x0004u
#define PAD_R1 0x0008u
#define PAD_SQUARE 0x0080u
#define PAD_SELECT 0x0100u
#define PAD_START 0x0800u
/* What Select makes a chord with: the shoulders (the speed controls, rewind)
 * and Square (the dialogue backlog). */
#define PARTNERS (PAD_L1 | PAD_R1 | PAD_L2 | PAD_R2 | PAD_SQUARE)
/* A Select let go on its own: the vblanks the game is handed it. The field
 * reads the pad once a frame of its own, two to four vblanks. */
#define SELECT_TAP 6

static struct {
    unsigned last; /* the live buttons at the last vblank */
    unsigned kept; /* the other buttons of a chord, kept from the game until let go */
    int pending;   /* Select is down and kept from the game */
    int chord;     /* ... and another button made it a chord */
    int tap;       /* vblanks left of a lone Select handed to the game after its release */
} hold;

static int holding(void) {
    return (speedControlHoldsSelect != NULL && speedControlHoldsSelect()) ||
           (backlogHoldsSelect != NULL && backlogHoldsSelect());
}

unsigned selectChordFilter(unsigned live) {
    unsigned pressed = live & ~hold.last;
    hold.last = live;
    if ((live & PAD_START) || !holding()) {
        hold.kept = 0;
        hold.pending = hold.chord = hold.tap = 0;
        return live;
    }
    if (pressed & PAD_SELECT) {
        hold.pending = 1;
        hold.chord = (live & PARTNERS) != 0; /* one held already: the game has seen it, keeps seeing it */
        hold.tap = 0;
    }
    if (hold.pending) {
        if (live & PAD_SELECT) {
            if (pressed & PARTNERS) {
                hold.chord = 1;
                hold.kept |= pressed & PARTNERS;
            }
        } else {
            hold.pending = 0;
            hold.tap = hold.chord ? 0 : SELECT_TAP;
        }
    }
    hold.kept &= live;
    unsigned out = live & ~hold.kept;
    if (hold.pending) {
        out &= ~PAD_SELECT;
    } else if (hold.tap > 0) {
        out |= PAD_SELECT;
        hold.tap--;
    }
    return out;
}
