/*
 * LIBETC: VSync, the vsync callbacks, ResetCallback and the video mode.
 *
 * The PS1 library counts vblanks in its own interrupt handler (started by the
 * first ResetCallback) and runs up to eight callbacks after counting; the
 * game's (VSyncCallback) is slot 4. VSync(n) waits until n vblanks have passed
 * since the previous VSync and at least one new one; VSync(1) is the time
 * since the previous VSync in horizontal lines; VSync(-1) the vblank count.
 */

/* No <string.h> here: with C23's const-preserving memchr macro, psyq.h's
 * host libc declarations do not compile after it. */
#include "common.h"
#include "psyq.h"
#include "system.h"
#include "savestate.h"
#include "settings.h"
#include "speedup.h"
#include "netplay.h"

void ExitCriticalSection(void);

static int started;
static void (*callbacks[8])(void);
static uint32_t countBase;     /* hwVSyncCount() when the library started counting */
static uint32_t lastVSync;     /* hwVSyncCount() when the last VSync returned */
static double lastVSyncTime;
static s32 videoMode;
/* The previous VSync call was VSync(-1) and returned this count (vsyncWork:
 * a busy wait for the vblank). The host's presentation only, never a state. */
static int pollRun;
static uint32_t pollCount;

void etcVBlank(void) {
    if (!started) {
        return;
    }
    for (int i = 0; i < 8; i++) {
        if (callbacks[i] != NULL) {
            callbacks[i]();
        }
    }
}

/* The soft reset clears main's BSS, LIBETC's state with it: the next
 * ResetCallback sets everything up again. */
void etcSoftReset(void) {
    started = 0;
}

void ResetCallback(void) {
    hwSafePoint();
    if (started) {
        return; /* only the first call sets up the interrupt handlers */
    }
    for (int i = 0; i < 8; i++) {
        callbacks[i] = NULL;
    }
    countBase = hwVSyncCount();
    lastVSync = countBase;
    lastVSyncTime = hwNow();
    started = 1;
    ExitCriticalSection();
}

void* VSyncCallbacks(s32 idx, void (*func)(void)) {
    hwSafePoint();
    void (*old)(void) = callbacks[idx & 7];
    callbacks[idx & 7] = func;
    return (void*)old;
}

void* VSyncCallback(void (*func)(void)) {
    if (func != NULL) {
        slowdownBoot(); /* the replay counts the boot from the first one (slowdown.c) */
    }
    return VSyncCallbacks(4, func);
}

s32 CheckCallback(void) {
    return hwInInterrupt();
}

extern void hwPresentPoll(void) __attribute__((weak));

static s32 vsyncWork(s32 mode, int catchUp);
static int idleChecked, idleOn; /* vsync_idle, read once */

s32 VSync(s32 mode) {
    s32 r = vsyncWork(mode, 0);
    slowdownVSyncReturn(__builtin_return_address(0)); /* the replay times the work after it */
    return r;
}

/* VSync(0) of battle's and the field's main flip (pc_speedup.h): with
 * Speedup, a vblank that already passed during rendering ends the wait.
 * Replay and netplay keep native waits (deterministic frame boundaries);
 * explicit waits in fades, loading, the field's final frame cap and slower
 * authored modes still call VSync. `site` is the game's call, as VSync's
 * return address is (the replay tells the callers apart by it). */
static void mainFlipVSync(const void* site) {
    int64_t replay;
    int catchUp = speedupEnabled() && !netplayActive() && !slowdownLogVBlank(&replay);
    vsyncWork(0, catchUp);
    slowdownVSyncReturn(site);
}

void pcBattleVSync(void) {
    mainFlipVSync(__builtin_return_address(0));
}

void pcFieldVSync(void) {
    mainFlipVSync(__builtin_return_address(0));
}

/* The Enhanced engine's pictures between two vblanks (xg-hbc.19.4): a loop
 * that waits two vblanks a frame (VSync(2): the battle's shatter, the field's
 * transition strips) registers a function that draws the picture between its
 * frames. VSync(2) calls it once the first vblank has come, inside the wait,
 * where the PS1 showed the frame before for a second vblank; it is PC-only
 * work: no time on the stepped clock or in the slowdown replay
 * (hwClockHold), and the game's timing, input and state are the Original's.
 * Not with the slowdown replay or online (their frame boundaries are the
 * log's or the peer's), nor when the frame's work already took that vblank.
 * The registration is the caller's, cleared by passing NULL.
 * XG_VSYNC_BETWEEN=0 leaves them out for one run (work/menu60's comparisons). */
const char* portOption(const char* name); /* options.c */
static void (*betweenDraw)(void);
static int betweenChecked, betweenOff; /* XG_VSYNC_BETWEEN=0: none (the oracle's comparison run) */

void pcVSyncBetween(void (*draw)(void)) {
    if (!betweenChecked) {
        const char* e = portOption("XG_VSYNC_BETWEEN");
        betweenChecked = 1;
        betweenOff = e != NULL && e[0] == '0';
    }
    betweenDraw = betweenOff ? NULL : draw;
}

static s32 vsyncWork(s32 mode, int catchUp) {
    hwSafePoint();
    slowdownVSync(mode);
    s32 lines = (s32)((hwNow() - lastVSyncTime) * hwHBlankRate()) & 0xFFFF;

    if (mode < 0) {
        /* The field's final frame cap busy-waits on VSync(-1), never in
         * hwYield: the host may present in it, but only on a call that
         * repeats the previous one's count. A single VSync(-1) reads the
         * count in the middle of a frame's work (the field's start, battle's
         * sprite timing), and the call that delivered the awaited vblank
         * returns to the game at once. VSync(1) only ever times work. */
        uint32_t count = hwVSyncCount();
        if (pollRun && count == pollCount) {
            /* A repeated poll: the game is busy-waiting for the vblank. On the
             * real clock sleep until the next event instead of spinning
             * (vsync_idle=0 restores the poll: XG_VSYNC_IDLE=0 for one run);
             * the next poll sees the delivered vblank as a spinning one would. */
            if (!idleChecked) {
                idleChecked = 1;
                idleOn = settingsBool("vsync_idle", 1);
            }
            if (!(idleOn && hwVBlankIdle()) && hwPresentPoll) hwPresentPoll();
        }
        pollRun = 1;
        pollCount = count;
        return (s32)(count - countBase);
    }
    pollRun = 0;
    if (mode == 1) {
        return lines;
    }
    if (mode > 1) {
        uint32_t target = lastVSync + (uint32_t)(mode - 1);
        int waited = 0;
        while ((int32_t)(hwVSyncCount() - target) < 0) {
            uint32_t before = hwVSyncCount();
            if (hwWaitVSync() == before) {
                break; /* inside a callback or a critical section: no vblank can arrive */
            }
            waited = 1;
        }
        if (betweenDraw != NULL && waited && mode == 2 && hwVSyncCount() == target && !netplayActive()) {
            int64_t replay;
            if (!slowdownLogVBlank(&replay)) {
                void (*draw)(void) = betweenDraw;
                betweenDraw = NULL; /* once a wait; never re-entered from its own drawing */
                hwClockHold(1);
                draw();
                hwClockHold(0);
                betweenDraw = draw;
            }
        }
    }
    if (catchUp) hwWaitVSyncSince(lastVSync);
    else hwWaitVSync();
    lastVSync = hwVSyncCount();
    lastVSyncTime = hwNow();
    hwVSyncResume();
    return lines;
}

s32 SetVideoMode(s32 mode) {
    hwSafePoint();
    s32 old = videoMode;
    videoMode = mode;
    hwSetVideoStandard(mode == 1);
    return old;
}

s32 GetVideoMode(void) {
    hwSafePoint();
    return videoMode;
}

/* ---- save states (savestate.h) ---- */

void etcStateIo(XgState* s) {
    XG_STATE_VAR(s, started);
    XG_STATE_VAR(s, callbacks);
    XG_STATE_VAR(s, countBase);
    XG_STATE_VAR(s, lastVSync);
    XG_STATE_VAR(s, lastVSyncTime);
    XG_STATE_VAR(s, videoMode);
}
