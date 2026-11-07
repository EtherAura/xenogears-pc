/*
 * The vblank locked to the display (xg-htn.41.3; a setting, off by default).
 *
 * The game's vblank comes from the PS1's video clock (hw.c: 59.826 Hz in the
 * 240-line NTSC modes, 59.940 interlaced, 49.76/50 PAL) and the pictures are
 * presented MAILBOX, so on a display that refreshes at another rate a picture
 * is now and then shown twice or not at all (at 59.826 Hz on a 60 Hz display,
 * one repeat every 5.7 s). With display_lock=1, when the window's display
 * refreshes within display_lock_tolerance Hz of the game's vblank rate, the
 * real-time clock runs at the display's rate instead (hwSetClockRate: the
 * whole virtual PS1, CD and SPU included, that much faster or slower, at most
 * 0.4%), one vblank per refresh, and pictures are presented FIFO (vk.c), so
 * each is shown once. The sound follows: the SPU output's stream plays at the
 * clock's rate (hwClockRate), its drift correction on top (port/src/spu/output.c).
 *
 * It changes the game's speed by the difference and adds the FIFO queue's
 * latency, so it is a setting. Stepped runs (XG_CLOCK=step) never lock (hw.c
 * does not call this there).
 *
 * Settings (settings.ini; XG_<KEY> in the environment overrides):
 *   display_lock=0|1             off (default) or on
 *   display_lock_tolerance=0.05  the largest difference, in Hz, that locks
 *                                (at most 0.24: 0.4% of 60 Hz)
 * Environment:
 *   XG_DISPLAY_HZ=<hz>           the display's refresh, instead of asking SDL
 *                                (headless runs, tests)
 */

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "system.h"
#include "settings.h"

/* port/src/gpu (weak: the GPU layer may not be in the build). */
extern SDL_Window* presentWindow(void) __attribute__((weak));
extern void gvkPresentModeChanged(void) __attribute__((weak));

#define CHECK_VBLANKS 30   /* the display and the settings looked at again this often */
#define MAX_RATE_OFFSET 0.004
#define MAX_TOLERANCE 0.24

static struct {
    int described;
    int countdown;
    double period;  /* the game's vblank period when last looked at */
    int locked;
    double rate;
} lk;

/* The window's display's refresh in Hz, 0 when not known. */
static double displayHz(void) {
    const char* e = getenv("XG_DISPLAY_HZ");
    if (e != NULL && e[0] != '\0') {
        return atof(e);
    }
    if (presentWindow == NULL || presentWindow() == NULL) {
        return 0;
    }
    SDL_DisplayID id = SDL_GetDisplayForWindow(presentWindow());
    const SDL_DisplayMode* m = id != 0 ? SDL_GetCurrentDisplayMode(id) : NULL;
    if (m == NULL) {
        return 0;
    }
    if (m->refresh_rate_numerator > 0 && m->refresh_rate_denominator > 0) {
        return (double)m->refresh_rate_numerator / m->refresh_rate_denominator;
    }
    return m->refresh_rate;
}

/* Whether pictures are presented FIFO for the lock (vk.c). */
int displayLockFifo(void) {
    return lk.locked;
}

static void setLocked(int on, double rate, double hz, double vblankHz) {
    if (on != lk.locked) {
        if (on) {
            fprintf(stderr, "display lock: on, the display's %.4f Hz for the game's %.4f Hz (clock x%.5f)\n", hz,
                    vblankHz, rate);
        } else {
            fprintf(stderr, "display lock: off\n");
        }
        lk.locked = on;
        if (gvkPresentModeChanged != NULL) {
            gvkPresentModeChanged();
        }
    }
    if (rate != lk.rate) {
        lk.rate = rate;
        hwSetClockRate(rate);
    }
}

/* At each vblank (real time; hw.c). */
void displayLockVBlank(void) {
    if (!lk.described) {
        lk.described = 1;
        lk.rate = 1;
        settingsDescribe("display_lock", "display_lock: 1 ties the game's vblank to the display's refresh when they "
                                         "are within display_lock_tolerance Hz (FIFO; the game that much faster or "
                                         "slower), 0 off (default)");
        settingsDescribe("display_lock_tolerance",
                         "display_lock_tolerance: the largest difference in Hz that display_lock locks (default 0.05)");
    }
    double period = hwVSyncPeriod();
    if (--lk.countdown > 0 && period == lk.period) {
        return;
    }
    lk.countdown = CHECK_VBLANKS;
    lk.period = period;
    if (!settingsInt("display_lock", 0)) {
        setLocked(0, 1, 0, 0);
        return;
    }
    const char* t = settingsGet("display_lock_tolerance");
    double tolerance = t != NULL && t[0] != '\0' ? atof(t) : 0.05;
    tolerance = tolerance < 0 ? 0 : tolerance > MAX_TOLERANCE ? MAX_TOLERANCE : tolerance;
    double hz = displayHz(), vblankHz = 1.0 / period;
    /* Virtual seconds per real second for one vblank per refresh. */
    double rate = hz * period;
    if (hz > 0 && fabs(hz - vblankHz) <= tolerance && fabs(rate - 1) <= MAX_RATE_OFFSET) {
        setLocked(1, rate, hz, vblankHz);
    } else {
        setLocked(0, 1, hz, vblankHz);
    }
}
