/*
 * Movie sweeps against PCSX-Redux (work/agents/PORT_M6/battles/msweep.py).
 *
 *   XG_MOVIE=<n>   the movie mode's first start plays movie n (picture and
 *                  ADPCM, dir 0x18/1 file n + 2) to its end instead of the
 *                  kernel's opening movie (type 0x81 with end frame 0x1FFF:
 *                  type 1 alone stops at frame 233), then returns to the
 *                  field as the opening does; with XG_BOOT_MODE=6, any movie
 *                  of the disc. Redux: msweep.py pokes the same.
 *   XG_SWEEP_LOG   "M <vblank> <frame>" each time the STR player hands a
 *                  frame to handleMovieFrame; Redux logs the frame counter
 *                  (movieCurrentFrame) as it changes
 */
#include "common.h"
#include "main/filesystem.h"

int portSweepEnabled(void);
void portSweepLog(const char* tag, const int* values, int count);
const char* portOption(const char* name);
extern u16 movieRequestEndFrame; /* the end frame of a type 0x80 movie (field scripts set it) */

void pcMovieStartOption(void) {
    static s32 done;
    const char* s;
    s32 n = 0;

    if (done) {
        return;
    }
    done = 1;
    s = portOption("XG_MOVIE");
    if (s == NULL) {
        return;
    }
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s++ - '0');
    }
    movieType.m0_type = 0x81;
    movieType.m1_number = n;
    movieRequestEndFrame = 0x1FFF;
}

void portMovieSweepFrame(s32 frame) {
    if (portSweepEnabled()) {
        portSweepLog("M", &frame, 1);
    }
}
