/*
 * XG_QUIT_AT=<vblank>: quit cleanly (quit.c) right after that vblank, for
 * validation runs whose length is known in vblanks rather than in seconds
 * (content sweeps: the battle, movie and field sweeps against Redux and
 * Beetle).
 *
 * The end is an event on the virtual clock (hw.h), queued at each vblank's
 * time behind the vblank's own interrupt: it runs on the game thread just
 * after the requested vblank's handlers. So the picture shown at that vblank
 * is the last one dumped (XG_DUMP_FRAMES, XG_DUMP_PRESENT) and the sound
 * (XG_AUDIO_DUMP) ends at that vblank's time; with XG_CLOCK=step every run of
 * the same input stops at the same frame and sample, whatever the host's
 * speed. (A thread polling the vblank count every 5 ms, as this was, let a
 * busy host run on a vblank or two and a few thousand samples: xg-htn.34.9.1.)
 *
 * The thread stays as a fallback: should the vblank count pass the end
 * without the event having run, it asks for a quit the ordinary way, and
 * quit.c's watchdog ends a game that then reaches no safe point.
 */

#include <SDL3/SDL.h>
#include <stdlib.h>

#include "savestate.h"
#include "system.h"

#define FALLBACK_POLL_MS 50
#define FALLBACK_GRACE_POLLS 20 /* a second past the end before the fallback steps in */

static uint32_t quitAt;
static double queuedAt; /* the time the end event is queued at */

/* At a vblank's time, after that vblank's interrupt: the end, or again at the
 * next vblank. Events due at the same time run in the order they were queued
 * (hw.h), and the next vblank's interrupt is queued by this one's handler,
 * before this event runs: so the next one queued here runs right after it. */
static void endEvent(void* arg) {
    if (quitAt == 0) {
        return; /* restored by a save state from a run with XG_QUIT_AT: not this run's */
    }
    if (hwVSyncCount() >= quitAt) {
        quitNow();
    }
    double at = hwVBlankToTime((double)hwVSyncCount() + 1);
    if (at < queuedAt) {
        at = queuedAt + hwVSyncPeriod(); /* no vblank queued yet (before hwInit) */
    }
    queuedAt = at;
    hwScheduleAt(at, endEvent, arg);
}

static int fallback(void* arg) {
    int past = 0;

    (void)arg;
    for (;;) {
        SDL_Delay(FALLBACK_POLL_MS);
        if (quitRequested()) {
            return 0;
        }
        if (hwVSyncCount() > quitAt && ++past >= FALLBACK_GRACE_POLLS) {
            quitRequest();
            return 0;
        }
    }
}

__attribute__((constructor)) static void quitAtInit(void) {
    const char* s = getenv("XG_QUIT_AT");
    if (s == NULL || s[0] == '\0' || atol(s) <= 0) {
        return;
    }
    quitAt = (uint32_t)atol(s);
    /* The first vblank's time as hwInit will queue it (the 240-line NTSC
     * period from 0). Queued before it, this runs first there and queues
     * itself again behind it; at any other time it finds its place the same
     * way. */
    queuedAt = hwVSyncPeriod();
    hwScheduleAt(queuedAt, endEvent, NULL);
    SDL_Thread* t = SDL_CreateThread(fallback, "quit at", NULL);
    if (t != NULL) {
        SDL_DetachThread(t);
    }
}

/* Save states (savestate.h): whether the saving run had its end event
 * queued. The restored queue then holds it, and it serves this run too (the
 * same time, the same place among the vblank's events); a state from a run
 * without one gets one now. */
void quitAtStateIo(XgState* s) {
    int queued = quitAt != 0;
    double at = queuedAt;
    XG_STATE_VAR(s, queued);
    XG_STATE_VAR(s, at);
    if (!xgStateLoading(s) || quitAt == 0) {
        return;
    }
    if (queued) {
        queuedAt = at;
    } else {
        queuedAt = hwVBlankToTime((double)hwVSyncCount() + 1);
        hwScheduleAt(queuedAt, endEvent, NULL);
    }
}
