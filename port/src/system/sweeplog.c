/*
 * State logs for content sweeps against PCSX-Redux.
 *
 *   XG_SWEEP_LOG=<file>    game-side collectors (port/src/game/<module>/
 *                          sweep.c) write one line per logical frame:
 *                          "<tag> <vblank> <numbers...>"; the Redux side
 *                          logs the same numbers from RAM at the same code
 *                          point (work/agents/PORT_M6/fields/ for the field)
 *   XG_SWEEP_STOP=<n>      quit after the n-th frame line (any tag but "V",
 *                          the field's variable dumps; a clean exit, so a
 *                          sweep does not wait for its timeout)
 *
 * The collectors are compiled like the game C, without libc, so the text is
 * written here.
 */

#include <stdio.h>
#include <stdlib.h>

#include "system.h"

static FILE* sweepFile;
static int sweepChecked;
static long sweepLines;
static long sweepStop;

int portSweepEnabled(void);
/* XG_SWEEP_PICTURE=1 beside XG_SWEEP_LOG: the picture lines (FP: the Enhanced
 * engine's 60 Hz field oracle, game/field/sweep.c). */
int portSweepPictureEnabled(void) {
    static int checked, on;
    if (!checked) {
        const char* e = getenv("XG_SWEEP_PICTURE");
        checked = 1;
        on = e != NULL && e[0] == '1' && portSweepEnabled();
    }
    return on;
}

int portSweepEnabled(void) {
    if (!sweepChecked) {
        const char* path = getenv("XG_SWEEP_LOG");
        const char* stop = getenv("XG_SWEEP_STOP");
        sweepChecked = 1;
        if (path != NULL && path[0] != '\0') {
            sweepFile = fopen(path, "w");
            if (sweepFile == NULL) {
                fprintf(stderr, "sweep: cannot write %s\n", path);
            }
        }
        sweepStop = stop != NULL ? atol(stop) : 0;
    }
    return sweepFile != NULL;
}

void gpuTraceNote(const char* text); /* port/src/gpu/libgpu.c, XG_GPU_TRACE */

void portSweepLog(const char* tag, const int* values, int count) {
    if (!portSweepEnabled()) {
        return;
    }
    if (count > 0) {
        char note[32];
        snprintf(note, sizeof note, "%s %d", tag, values[0]); /* the frame, in the GPU trace */
        gpuTraceNote(note);
    }
    fprintf(sweepFile, "%s %u", tag, (unsigned)hwVSyncCount());
    for (int i = 0; i < count; i++) {
        fprintf(sweepFile, " %d", values[i]);
    }
    fputc('\n', sweepFile);
    if (sweepStop > 0 && !(tag[0] == 'V' && tag[1] == '\0') && ++sweepLines >= sweepStop) {
        fflush(sweepFile);
        quitRequest();
    }
}
