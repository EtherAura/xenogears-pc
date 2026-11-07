/*
 * The virtual PS1's clock and interrupts (hw.h).
 *
 * Interrupts are callbacks in a time-ordered queue, delivered on the game
 * thread at safe points. The vertical blank is one of them: a periodic event
 * that counts vblanks, has the GPU layer show the frame, reads the pads and
 * runs the LIBETC vsync callbacks (the game's vsync callback), in that order.
 *
 * Clock: real time by default. XG_CLOCK=step makes it a stepped clock for
 * reproducible runs: time stands still while the game computes (each safe
 * point costs a microsecond, so polling loops still advance) and jumps to the
 * next event whenever the game waits.
 *
 * Waiting in real time sleeps (SDL_DelayNS, at most MAX_SLEEP at a time), and
 * the host wakes a sleeper up late by its timer's slack. For the vblank that
 * lateness is the picture's: so the last stretch before a due vblank is spun
 * instead of slept, as long as the host's sleeps have lately overshot (twice
 * their mean overshoot and a little, SPIN_MIN to SPIN_MAX). The stepped clock
 * never sleeps and is not touched by this.
 *
 * A real-time game thread that reaches no safe point for longer than
 * MAX_BACKLOG was stopped from outside (the host slept, SIGSTOP, a debugger,
 * a blocked present): that time is taken out of the clock, as the settings
 * menu's pause is. Otherwise the CD's sectors and the SPU would catch up all
 * of it at once (the SPU computing the whole gap's sound, the CD delivering
 * every sector the gap held without a vblank between them).
 *
 * Battle speed (hwSetGameSpeed, battlespeed.c): the game's clock can run 2 or
 * 4 game seconds per audio second. The audio clock (hwAudioTime) is what the
 * sound driver's root counter (libapi.c) and the SPU's samples (spu.c) follow:
 * on the real clock it keeps real time while the game's vblanks, CD and
 * everything else come that many times as often; on the stepped clock it is
 * the game's time divided by the factor. It is the game's time itself until
 * the factor first changes, so a run that never changes it is the same as
 * before to the bit.
 */

#include <SDL3/SDL.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cputime.h"
#include "invariants.h"
#include "savestate.h"
#include "system.h"
#include "world.h"

/* port/src/gpu: presents the display area at each vblank (weak: the GPU layer
 * may not be in the build). */
extern void gpuVBlank(void) __attribute__((weak));
/* port/src/card: the BIOS card driver's vblank part (weak, like the GPU's). */
extern void cardVBlank(void) __attribute__((weak));
/* displaylock.c: the optional lock of the vblank to the display (weak: tests
 * that link this file alone have none). */
extern void displayLockVBlank(void) __attribute__((weak));
/* savestate.c: the game parks there for a save state or a rewind capture
 * (weak, like the GPU's: tests that link this file alone have none). */
extern void stateVBlankPoint(void) __attribute__((weak));
extern void gpuHostClock(int realtime) __attribute__((weak));
extern uint64_t gpuHostDeadline(void) __attribute__((weak));
extern void gpuHostPoll(uint64_t now, double vblank, uint64_t available) __attribute__((weak));
extern void gpuHostReset(void) __attribute__((weak));
/* cputime.c (weak: hostpolltest includes this file alone): the spin's bucket. */
extern int cpuTimingOn __attribute__((weak));
extern void cpuTimeBegin(int kind) __attribute__((weak));
extern void cpuTimeEnd(int kind) __attribute__((weak));
/* settings.c: the vsync_wait key (weak: tests that link this file alone have
 * no settings; they sleep). */
extern const char* settingsGet(const char* key) __attribute__((weak));
extern void settingsSet(const char* key, const char* value) __attribute__((weak));
extern int settingsSave(void) __attribute__((weak));
extern void settingsDescribe(const char* key, const char* comment) __attribute__((weak));
/* port/src/gpu: the hi-res canvas work (a trim, a growth) that stalled the
 * game thread since a host time, in seconds (weak, like the GPU's). */
extern double gpuStallSince(uint64_t ns) __attribute__((weak));
/* port/src/spu/output.c: a stall taken out of the clock (weak: tests that
 * link this file alone have no sound output). */
extern void spuOutputClockGap(void) __attribute__((weak));
/* libapi.c: the audio clock's rate changed, the sound driver's root counter
 * is queued again at its audio time (weak, like the GPU's). */
extern void apiAudioClockChanged(void) __attribute__((weak));
/* battlespeed.c: the factor for the frame this vblank starts (weak, like the GPU's). */
extern void battleSpeedVBlank(void) __attribute__((weak));

#define STEP_SAFE_POINT_COST 1e-6 /* XG_CLOCK=step: seconds per safe point */
#define MAX_SLEEP 0.005           /* hwYield never sleeps longer, so events queued from other threads are seen */
#define SPIN_MIN 0.0003           /* real time: spin at least this long before a due vblank */
#define SPIN_MAX 0.0015           /* ... and at most this long (a coarse host timer) */
#define MAX_BACKLOG 0.25          /* a periodic interrupt further behind than this skips ahead */

typedef struct {
    double at;
    uint64_t seq;
    HwEvent fn;
    void* arg;
} Pending;

static SDL_Mutex* queueLock;
static Pending* queue; /* binary min-heap on (at, seq) */
static int queueCount;
static int queueCap;
static uint64_t queueSeq;
/* The earliest event's time (the heap's top, INFINITY for none) as the bits
 * of its double, kept under the lock wherever the top can change, so the
 * spin wait (spinUntil) reads it without taking the lock on every turn: the
 * lock was the busiest call of the game thread's idle time, in the way of
 * the other threads' hwScheduleAt. */
static _Atomic uint64_t earliestBits = 0x7FF0000000000000ull; /* INFINITY */

static void noteEarliest(void) {
    double at = queueCount ? queue[0].at : INFINITY;
    uint64_t bits;
    memcpy(&bits, &at, sizeof bits);
    atomic_store_explicit(&earliestBits, bits, memory_order_release);
}

static double earliestEvent(void) {
    uint64_t bits = atomic_load_explicit(&earliestBits, memory_order_acquire);
    double at;
    memcpy(&at, &bits, sizeof at);
    return at;
}

static uint64_t startNs;    /* real time: the clock's anchor on the host's clock */
static double startAt;      /* ... the virtual time there (0 unless the rate changed) */
static double clockRate = 1; /* ... virtual seconds per real second (hwSetClockRate) */
/* Battle speed (hwSetGameSpeed): game seconds per audio second, and the audio
 * clock's anchor: at the game's time audioBaseV it read audioBaseA. */
static int gameSpeed = 1;
static double audioBaseV, audioBaseA;
static uint64_t lastBeatNs; /* real time: the game thread's last safe point (gapCheck) */
static int stepClock;
static double stepNow;
static uint64_t pausedAt; /* hwPause: the real time the settings menu opened, else 0 */

static int delivering;
static char* deliveringFrame; /* deliverDue's stack frame while delivering */
static int maskDepth;
static int inWorld; /* a world of its own is in (world.h): its queue and stepped clock */

static uint32_t vblankCount;
static double nextVBlank;
static int palVideo;
static int interlacedVideo; /* GP1(08h) bit 5 */
static int dotClocks = 10;  /* video clocks per pixel (GP1(00h) leaves the 256-pixel mode) */

/* Safe points reached (stall.c watches it from its own thread). Only the
 * game thread writes it: a relaxed load and store, no locked instruction. */
static _Atomic uint32_t heartbeat;

static void beat(void) {
    atomic_store_explicit(&heartbeat, atomic_load_explicit(&heartbeat, memory_order_relaxed) + 1,
                          memory_order_relaxed);
}

uint32_t hwHeartbeat(void) {
    return atomic_load_explicit(&heartbeat, memory_order_relaxed);
}

/* Real time, on the game thread at each safe point: a gap since the last one
 * longer than MAX_BACKLOG leaves the clock (the file's comment). */
static void gapCheck(void) {
    if (stepClock || pausedAt != 0 || inWorld || lastBeatNs == 0) {
        return;
    }
    uint64_t t = SDL_GetTicksNS();
    uint64_t gap = t - lastBeatNs;
    lastBeatNs = t;
    if (gap > (uint64_t)(MAX_BACKLOG * 1e9)) {
        double stalled = gpuStallSince ? gpuStallSince(t - gap) : 0;
        startNs += gap;
        if (gpuHostReset) gpuHostReset();
        if (spuOutputClockGap) spuOutputClockGap();
        fprintf(stderr, "hw: the game thread was stopped for %.3f s (%s): taken out of the clock\n", (double)gap * 1e-9,
                stalled * 1e9 >= (double)gap * 0.5 ? "the hi-res canvas's trim or growth" : "the host slept?");
    }
}

/* ---- clock ---- */

double hwNow(void) {
    if (stepClock) {
        return stepNow;
    }
    return startAt + (double)((pausedAt != 0 ? pausedAt : SDL_GetTicksNS()) - startNs) * 1e-9 * clockRate * gameSpeed;
}

void hwSetClockRate(double rate) {
    if (stepClock || inWorld || rate <= 0 || rate == clockRate) {
        return;
    }
    /* From now on at the new rate: the clock goes on from where it is. */
    uint64_t t = pausedAt != 0 ? pausedAt : SDL_GetTicksNS();
    startAt += (double)(t - startNs) * 1e-9 * clockRate * gameSpeed;
    startNs = t;
    clockRate = rate;
}

double hwClockRate(void) {
    return clockRate;
}

/* ---- battle speed (hw.h) ---- */

double hwAudioTime(double t) {
    return audioBaseA + (t - audioBaseV) / gameSpeed;
}

double hwAudioToGame(double a) {
    return audioBaseV + (a - audioBaseA) * gameSpeed;
}

int hwGameSpeed(void) {
    return gameSpeed;
}

int hwInWorld(void) {
    return inWorld;
}

void hwSetGameSpeed(int factor) {
    factor = factor < 1 ? 1 : factor > HW_GAME_SPEED_MAX ? HW_GAME_SPEED_MAX : factor;
    if (factor == gameSpeed || inWorld) {
        return;
    }
    double now = hwNow();
    if (!stepClock) {
        /* The real clock goes on from where it is, `factor` times real time. */
        startAt = now;
        startNs = pausedAt != 0 ? pausedAt : SDL_GetTicksNS();
    }
    audioBaseA = hwAudioTime(now);
    audioBaseV = now;
    gameSpeed = factor;
    if (apiAudioClockChanged) apiAudioClockChanged();
}

/* Real-time waits: game seconds to real ones. A factor of 1 leaves the value
 * as it was (the display lock's rate was never in these). */
static double realSeconds(double gameSeconds) {
    return gameSeconds / gameSpeed;
}

/* vsync_wait=spin: every real-time wait spins instead of sleeping. A thread
 * that sleeps most of each vblank runs at a low clock when it wakes (the
 * governor; 2-4x on the CPU buckets, xg-hbc.9), and the host's timer wakes
 * it late; spinning keeps the clock steady and delivers vblanks on time, at
 * the cost of one core always busy. Off by default (sleep). */
static int spinWait = -1;

static int spinWaitOn(void) {
    if (spinWait < 0) {
        if (settingsDescribe) {
            settingsDescribe("vsync_wait", "vsync_wait: sleep (default: the game thread sleeps until its next event; the CPU may clock down between frames) or spin (busy-waits: a steady CPU clock and vblanks on time, one core always busy)");
        }
        const char* v = settingsGet ? settingsGet("vsync_wait") : NULL;
        spinWait = v != NULL && strcmp(v, "spin") == 0;
    }
    return spinWait;
}

int hwSpinWait(void) {
    return spinWaitOn();
}

void hwSetSpinWait(int on) {
    spinWait = on != 0;
    if (settingsSet) settingsSet("vsync_wait", on ? "spin" : "sleep");
    if (settingsSave) settingsSave();
}

static double nextEventTime(void);

/* Real time: spin until the host time `end` (s on hwNow's scale), or until
 * an event due before `until` is queued by another thread. */
static void spinUntil(double end, double until) {
    int timed = &cpuTimingOn != NULL && cpuTimingOn && cpuTimeBegin != NULL;
    if (timed) cpuTimeBegin(CPU_SPIN);
    while (pausedAt == 0 && hwNow() < end && earliestEvent() >= until) {
        SDL_CPUPauseInstruction();
    }
    if (timed) cpuTimeEnd(CPU_SPIN);
}

static void sleepFor(double seconds) {
    if (seconds <= 0) {
        return;
    }
    if (stepClock) {
        stepNow += seconds;
    } else if (spinWaitOn()) {
        spinUntil(hwNow() + seconds, INFINITY);
    } else {
        SDL_DelayNS((Uint64)(realSeconds(seconds) * 1e9));
    }
}

static double nextEventTime(void);

/* The real-time sleeps' mean overshoot (s), and the stretch before a due
 * vblank that is spun rather than slept. */
static double oversleep = 0.0001;

static double spinMargin(void) {
    double m = 2.0 * oversleep + 0.0001;
    return m < SPIN_MIN ? SPIN_MIN : m > SPIN_MAX ? SPIN_MAX : m;
}

/* Real time: wait towards `until`, the next event's time (or an end before
 * it), no longer than MAX_SLEEP. Before the vblank the last spinMargin() is
 * spun, so the vblank is delivered on time rather than when the host's timer
 * wakes the thread; the spin stops early for an event another thread queues
 * before it. */
static void waitReal(double until, int host) {
    double now = hwNow();
    int beforeVBlank = until >= nextVBlank;
    if (host && !inWorld && !pausedAt && gpuHostDeadline) {
        uint64_t deadline = gpuHostDeadline();
        if (deadline != UINT64_MAX) {
            uint64_t ticks = SDL_GetTicksNS();
            double at = now + (deadline > ticks ? (double)(deadline - ticks) * 1e-9 * clockRate * gameSpeed : 0);
            if (at < until) until = at;
        }
    }
    double wait = until - now;
    if (wait <= 0) {
        return;
    }
    if (spinWaitOn()) {
        /* At most MAX_SLEEP at a time, as a sleep: the caller's loop sees
         * what the other threads queued and quits when asked. */
        spinUntil(now + (wait > MAX_SLEEP ? MAX_SLEEP : wait), until);
        gapCheck();
        return;
    }
    if (beforeVBlank) {
        double margin = spinMargin();
        if (wait <= margin) {
            spinUntil(until, until);
            gapCheck();
            return;
        }
        wait -= margin;
    }
    if (wait > MAX_SLEEP) {
        wait = MAX_SLEEP;
    }
    SDL_DelayNS((Uint64)(realSeconds(wait) * 1e9));
    double over = hwNow() - now - wait;
    if (over < MAX_BACKLOG) {
        oversleep += ((over > 0 ? over : 0) - oversleep) / 16.0;
    }
    gapCheck(); /* stopped while asleep: out of the clock before anything is delivered */
}

/* The video clock of the mode's own region, as psx-spx recommends for
 * emulation (a console only has its own region's crystal). */
static double videoClock(void) {
    return palVideo ? HW_VIDEO_HZ_PAL : HW_VIDEO_HZ_NTSC;
}

double hwHBlankRate(void) {
    return videoClock() / (palVideo ? HW_LINE_CLOCKS_PAL : HW_LINE_CLOCKS_NTSC);
}

double hwVSyncPeriod(void) {
    double lines = palVideo ? (interlacedVideo ? HW_FIELD_LINES_PAL_INTERLACED : HW_FIELD_LINES_PAL)
                            : (interlacedVideo ? HW_FIELD_LINES_NTSC_INTERLACED : HW_FIELD_LINES_NTSC);
    return lines / hwHBlankRate();
}

double hwDotClockRate(void) {
    return videoClock() / dotClocks;
}

void hwSetVideoStandard(int pal) {
    palVideo = pal != 0;
}

void hwSetDisplayTiming(int interlaced, int clocks) {
    interlacedVideo = interlaced != 0;
    if (clocks > 0) {
        dotClocks = clocks;
    }
}

/* ---- event queue ---- */

static int before(const Pending* a, const Pending* b) {
    return a->at < b->at || (a->at == b->at && a->seq < b->seq);
}

static void lockQueue(void) {
    if (queueLock == NULL) {
        queueLock = SDL_CreateMutex();
    }
    SDL_LockMutex(queueLock);
}

static void unlockQueue(void) {
    SDL_UnlockMutex(queueLock);
}

void hwScheduleAt(double at, HwEvent fn, void* arg) {
    lockQueue();
    if (queueCount == queueCap) {
        queueCap = queueCap ? queueCap * 2 : 64;
        queue = realloc(queue, (size_t)queueCap * sizeof(*queue));
        if (queue == NULL) {
            fprintf(stderr, "hwScheduleAt: out of memory\n");
            abort();
        }
    }
    int i = queueCount++;
    Pending p = {at, queueSeq++, fn, arg};
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (!before(&p, &queue[parent])) {
            break;
        }
        queue[i] = queue[parent];
        i = parent;
    }
    queue[i] = p;
    noteEarliest();
    unlockQueue();
}

/* Remove the earliest event if it is due by `now`. */
static int popDue(double now, Pending* out) {
    lockQueue();
    if (queueCount == 0 || queue[0].at > now) {
        unlockQueue();
        return 0;
    }
    *out = queue[0];
    Pending last = queue[--queueCount];
    int i = 0;
    for (;;) {
        int child = i * 2 + 1;
        if (child >= queueCount) {
            break;
        }
        if (child + 1 < queueCount && before(&queue[child + 1], &queue[child])) {
            child++;
        }
        if (!before(&queue[child], &last)) {
            break;
        }
        queue[i] = queue[child];
        i = child;
    }
    if (queueCount > 0) {
        queue[i] = last;
    }
    noteEarliest();
    unlockQueue();
    return 1;
}

static double nextEventTime(void) {
    lockQueue();
    double at = queueCount ? queue[0].at : INFINITY;
    unlockQueue();
    return at;
}

/* ---- delivery ---- */

/* Whether a callback is running. A callback can leave by longjmp (bootGame,
 * the game's restart after a fatal error): a caller whose frame is above
 * deliverDue's is no longer inside it. */
__attribute__((noinline)) static int inDelivery(void) {
    if (delivering && (char*)__builtin_frame_address(0) > deliveringFrame) {
        delivering = 0;
    }
    return delivering;
}

int hwInInterrupt(void) {
    return inDelivery();
}

static int blocked(void) {
    return inDelivery() || maskDepth > 0;
}

static void hostPoll(int vblankWait) {
    if (!stepClock && !pausedAt && !inWorld && !blocked() && gpuHostPoll && gpuHostDeadline &&
        gpuHostDeadline() != UINT64_MAX) {
        /* Presentation runs only while game code is waiting, and must end
         * where the wait gives control back to it. A plain yield returns
         * after any delivered event (the loop may wait for a CD, DMA or
         * timer interrupt): reserve the next event. A wait for the vblank
         * (hwWaitVSync, the VSync(-1) frame cap) returns only at the vblank;
         * the timer/CD events before it still run at their queued virtual
         * deadlines in deliverDue, so a short timer interval need not
         * prevent every display sample there. */
        double until = vblankWait ? nextVBlank : nextEventTime();
        double slack = (until - hwNow()) / (clockRate * gameSpeed);
        uint64_t available = !isfinite(slack) ? UINT64_MAX : slack > 0 ? (uint64_t)(slack * 1e9) : 0;
        gpuHostPoll(SDL_GetTicksNS(), hwVBlankNow(), available);
    }
}

/* Run every event due by `now`, as the PS1 runs pending interrupts: one at a
 * time, none nested. The SPU runs up to each event's time first (what the
 * callback reads, ENVX, is current) and takes its register writes at that
 * time. */
static int deliverDue(double now) {
    int n = 0;
    Pending p;

    delivering = 1;
    deliveringFrame = __builtin_frame_address(0);
    while (maskDepth == 0 && popDue(now, &p)) {
        spuSync(p.at);
        p.fn(p.arg);
        spuSync(p.at);
        n++;
    }
    delivering = 0;
    spuSync(now);
    return n;
}

/* A game busy wait for the vblank that never reaches hwYield (libetc.c's
 * VSync(-1) frame cap). */
void hwPresentPoll(void) { hostPoll(1); }

static void yieldFor(int vblankWait);

int hwVBlankIdle(void) {
    if (stepClock || inWorld || pausedAt) return 0;
    yieldFor(1); /* presents (hostPoll(1)), sleeps to the next event, delivers it */
    return 1;
}

/* hwClockHold: work the PS1 does not do (the Enhanced engine's extra
 * pictures) takes no time on the stepped clock nor in the slowdown replay,
 * so the game's timing (its sound among it) stays the Original's. Nests. */
static int clockHold;
void hwClockHold(int on) {
    clockHold += on ? 1 : (clockHold > 0 ? -1 : 0);
}

/* hwClockDefer: work the PS1 does, but later than the Enhanced engine does
 * it (the 60 Hz world map's tick ends its frame a vblank early: the picture
 * between comes after it): its safe points take their time on the stepped
 * clock only at hwClockSettle, where the Original's would have; they deliver
 * nothing until then. On the real clock nothing changes. */
static int clockDefer;
static double clockDeferred;
void hwClockDefer(int on) {
    clockDefer += on ? 1 : (clockDefer > 0 ? -1 : 0);
}
void hwClockSettle(void) {
    if (clockDeferred > 0) {
        stepNow += clockDeferred;
        clockDeferred = 0;
        if (!blocked()) {
            deliverDue(hwNow());
        }
    }
}

void hwSafePoint(void) {
    beat();
    gapCheck();
    quitPoll(0);
    if (clockHold > 0) {
        return;
    }
    if (clockDefer > 0 && stepClock && pausedAt == 0 && !inWorld) {
        clockDeferred += STEP_SAFE_POINT_COST; /* (the real clock: as usual) */
        return;
    }
    if (stepClock && pausedAt == 0 && !inWorld) {
        /* Paused (hwPause), game code the extras run in their sandbox reaches
         * safe points too: the clock stands still for it. A world's clock
         * moves only when it runs or waits (hwWorldRun, hwYield). */
        stepNow += STEP_SAFE_POINT_COST;
    }
    if (blocked()) {
        return;
    }
    deliverDue(hwNow());
    if (!inWorld) {
        slowdownSafePoint(); /* the replay may let a vblank pass here, as the PS1's CPU time did */
    }
}

/* hwYield; vblankWait: hwWaitVSync's, which returns only at the vblank
 * (the host's presentation budget, hostPoll). */
static void yieldFor(int vblankWait) {
    beat();
    gapCheck();
    quitPoll(0); /* a game loop that only yields (PORT_YIELD) must quit too */
    if (blocked()) {
        /* Nothing can be delivered here (the PS1 would hang the same way);
         * at least let time pass, and let the window be closed. */
        quitPoll(1);
        sleepFor(0.001);
        return;
    }
    double now = hwNow();
    int delivered = deliverDue(now);
    if (delivered > 0) {
        return;
    }
    hostPoll(vblankWait); /* idle only: a delivered event gives control back to game code */
    double next = nextEventTime();
    if (stepClock) {
        double wait = next - now;
        sleepFor(wait < MAX_SLEEP ? wait : MAX_SLEEP);
    } else {
        waitReal(next, 1);
    }
    delivered = deliverDue(hwNow());
    if (delivered == 0) hostPoll(vblankWait);
}

void hwYield(void) {
    yieldFor(0);
}

void hwBusy(double seconds) {
    if (seconds <= 0 || inWorld || blocked()) {
        return;
    }
    double end = hwNow() + seconds;
    for (;;) {
        beat();
        gapCheck();
        quitPoll(0);
        deliverDue(hwNow());
        double now = hwNow();
        if (now >= end || blocked()) {
            return;
        }
        double next = nextEventTime();
        double until = next < end ? next : end;
        if (stepClock) {
            stepNow = until;
        } else {
            waitReal(until, 0);
        }
    }
}

/* A wait for the vblank (LIBETC's VSync) returns this long after it on the
 * PS1: the vblank interrupt's handlers run first (the BIOS dispatch, LIBETC's
 * count and callbacks, the pad/card driver, the game's vsync callback).
 * Measured against Beetle PSX as a black box: at least 0.004 vblank (a CD
 * sector ready at x.004 is picked up by the code after the wait) and at most
 * 0.008 (the movie mode's first VSync(1) at x.008); 0.006 lines the US title
 * crawl's Cross and the movie pictures up with Beetle's (xg-htn.15.33). */
#define VSYNC_RESUME 0.006 /* vblanks */

void hwVSyncResume(void) {
    hwBusy(VSYNC_RESUME * hwVSyncPeriod());
}

void portYield(void) {
    hwYield();
}

void hwMaskInterrupts(void) {
    maskDepth++;
}

/* The settings menu: the real clock stands still and leaves out the paused
 * time; the stepped clock only moves at safe points, which a pause has none of. */
void hwPause(int on) {
    if (on && pausedAt == 0) {
        pausedAt = SDL_GetTicksNS();
        if (gpuHostReset) gpuHostReset();
    } else if (!on && pausedAt != 0) {
        startNs += SDL_GetTicksNS() - pausedAt;
        pausedAt = 0;
        if (gpuHostReset) gpuHostReset();
        if (lastBeatNs != 0) {
            lastBeatNs = SDL_GetTicksNS(); /* the pause is out of the clock already */
        }
    }
}

void hwPausedFrame(void) {
    beat();
    quitPoll(0);
}

void hwUnmaskInterrupts(void) {
    if (maskDepth > 0 && --maskDepth == 0) {
        /* What became due while masked runs now, as on the PS1. */
        hwSafePoint();
    }
}

/* ---- vertical blank ---- */

static void vblankInterrupt(void* arg) {
    double now = hwNow();

    vblankCount++;
    nextVBlank += hwVSyncPeriod();
    if (nextVBlank < now - MAX_BACKLOG * gameSpeed) {
        nextVBlank = now + hwVSyncPeriod(); /* a long stall (debugger, window drag): skip */
    }
    hwScheduleAt(nextVBlank, vblankInterrupt, arg);
    if (displayLockVBlank != NULL && !stepClock) {
        displayLockVBlank();
    }
    if (battleSpeedVBlank != NULL) {
        battleSpeedVBlank(); /* the factor of the frame this vblank starts, and whether it is shown */
    }
    if (gpuVBlank != NULL) {
        gpuVBlank();
    }
    padVBlank();
    if (cardVBlank != NULL) {
        cardVBlank();
    }
    etcVBlank();
}

uint32_t hwVSyncCount(void) {
    return vblankCount;
}

/* The vblank count with the fraction of the current frame (the next vblank
 * is due at nextVBlank), and the time a given such count is reached. */
double hwVBlankNow(void) {
    double period = hwVSyncPeriod();
    double frac = 1.0 - (nextVBlank - hwNow()) / period;
    return vblankCount + (frac < 0 ? 0 : frac > 0.999 ? 0.999 : frac);
}

double hwVBlankToTime(double vblank) {
    return nextVBlank + (vblank - (vblankCount + 1)) * hwVSyncPeriod();
}

uint32_t hwWaitVSync(void) {
    hwSafePoint();
    uint32_t start = vblankCount;
    if (inWorld) {
        /* A world has no vblank: a frame's time passes in it. */
        double until = hwNow() + hwVSyncPeriod();
        while (hwNow() < until) {
            hwYield();
        }
        return vblankCount;
    }
    if (blocked()) {
        /* The vblank cannot be delivered inside a callback or with interrupts
         * masked (the PS1's VSync times out there); wait for its time only. */
        quitPoll(1);
        sleepFor(nextVBlank - hwNow());
        gapCheck();
        return vblankCount;
    }
    while (vblankCount == start) {
        yieldFor(1);
    }
    /* The vblank's handlers have run and nothing is being delivered: the
     * place a save state is taken or put back (savestate.h). */
    if (stateVBlankPoint != NULL) {
        stateVBlankPoint();
    }
    invariantVBlank(); /* XG_INVARIANT_LOG: the state this vblank left (invariants.c) */
    return vblankCount;
}

uint32_t hwWaitVSyncSince(uint32_t previous) {
    /* Keep the original single safe point on deterministic/world clocks:
     * even one extra safe point advances a stepped-clock replay slightly. */
    if (stepClock || inWorld || blocked()) return hwWaitVSync();
    hwSafePoint();
    if ((int32_t)(vblankCount - previous) <= 0) {
        return hwWaitVSync();
    }
    /* The same park as hwWaitVSync's, later in the frame: the vblank
     * arrived while the game drew, and the safe point above has run its
     * handlers and everything else due; nothing is being delivered, the
     * game is in its VSync between two frames, and a state keeps the
     * virtual time, so a load resumes here exactly. Skipping it would leave
     * a game that always overruns its frame without states or rewind. At
     * most one park per vblank: the next call waits for a new one. */
    if (stateVBlankPoint != NULL) stateVBlankPoint();
    return vblankCount;
}

int hwInit(void) {
    const char* clock = getenv("XG_CLOCK");

    quitInit();
    stallInit();
    stepClock = clock != NULL && strcmp(clock, "step") == 0;
    if (gpuHostClock) gpuHostClock(!stepClock);
    startNs = SDL_GetTicksNS();
    lastBeatNs = startNs;
    stepNow = 0;
    lockQueue();
    unlockQueue();
    nextVBlank = hwVSyncPeriod();
    hwScheduleAt(nextVBlank, vblankInterrupt, NULL);
    padInit();
    if (stepClock) {
        fprintf(stderr, "hw: stepped clock (XG_CLOCK=step)\n");
    }
    return 1;
}

/* The game is about to run: the boot since the last safe point (the Vulkan
 * device and its pipelines, the disc, the modules' data: ~160 ms on an
 * NVIDIA driver, more on a cold start) leaves the clock. Otherwise the first
 * vblanks are due already and the game starts with a backlog it works off
 * at full speed; gapCheck only leaves out a boot longer than MAX_BACKLOG,
 * so a warm boot stalled and a cold one did not. */
void hwGameStart(void) {
    if (stepClock || pausedAt != 0 || lastBeatNs == 0) {
        return;
    }
    uint64_t t = SDL_GetTicksNS();
    startNs += t - lastBeatNs;
    lastBeatNs = t;
    if (gpuHostReset) gpuHostReset();
}

/* ---- a world of its own (world.h) ---- */

/* What a world has of this file: the event queue, a stepped clock of its
 * own, interrupt masking and delivery. */
typedef struct {
    Pending* queue;
    int queueCount;
    int queueCap;
    uint64_t queueSeq;
    int stepClock;
    double stepNow;
    uint64_t pausedAt;
    int delivering;
    char* deliveringFrame;
    int maskDepth;
    int inWorld;
    int gameSpeed;
    double audioBaseV, audioBaseA;
} HwPart;

size_t hwWorldPartSize(void) {
    return sizeof(HwPart);
}

void hwWorldPartInit(void* blob) {
    HwPart* h = blob;
    memset(h, 0, sizeof(*h));
    h->stepClock = 1; /* from 0, moved only by hwWorldRun and waits */
    h->inWorld = 1;
    h->gameSpeed = 1; /* its sound on its own clock */
}

void hwWorldPartFree(void* blob) {
    free(((HwPart*)blob)->queue);
}

void hwWorldPartSwap(void* blob) {
    HwPart* h = blob;
    HwPart mine = {queue,     queueCount, queueCap,   queueSeq,        stepClock, stepNow,
                   pausedAt,  delivering, deliveringFrame, maskDepth, inWorld,
                   gameSpeed, audioBaseV, audioBaseA};
    lockQueue();
    queue = h->queue;
    queueCount = h->queueCount;
    queueCap = h->queueCap;
    queueSeq = h->queueSeq;
    noteEarliest();
    unlockQueue();
    stepClock = h->stepClock;
    stepNow = h->stepNow;
    pausedAt = h->pausedAt;
    delivering = h->delivering;
    deliveringFrame = h->deliveringFrame;
    maskDepth = h->maskDepth;
    inWorld = h->inWorld;
    gameSpeed = h->gameSpeed;
    audioBaseV = h->audioBaseV;
    audioBaseA = h->audioBaseA;
    if (gpuHostClock) gpuHostClock(!stepClock && !inWorld);
    *h = mine;
}

/* Inside a world: its time moves on by `seconds`, each event delivered at its
 * own time (a root counter's backlog is never skipped), the SPU run between. */
void hwWorldRun(double seconds) {
    if (!inWorld || seconds <= 0) {
        return;
    }
    double end = stepNow + seconds;
    while (!blocked()) {
        double at = nextEventTime();
        if (at > end) {
            break;
        }
        if (at > stepNow) {
            stepNow = at;
        }
        deliverDue(stepNow);
    }
    if (end > stepNow) {
        stepNow = end;
    }
    if (blocked()) {
        spuSync(stepNow);
    } else {
        deliverDue(stepNow);
    }
}

/* ---- save states (savestate.h) ---- */

/* The machine's time and pending interrupts. The queue's entries are saved
 * (their functions are code and their arguments integers or pointers into
 * statics, the same in every run of a build) and loaded into the live array,
 * a min-heap as saved. Only the virtual time is kept: the stepped clock is
 * set to it, the real clock re-anchored there, so a state taken with either
 * clock loads with either. */
void hwStateIo(XgState* s) {
    double now = hwNow();
    XG_STATE_VAR(s, now);
    XG_STATE_VAR(s, clockDeferred); /* (hwClockDefer: a park in the 60 Hz world map's frame between owes the tick's) */
    XG_STATE_VAR(s, vblankCount);
    XG_STATE_VAR(s, nextVBlank);
    XG_STATE_VAR(s, palVideo);
    XG_STATE_VAR(s, interlacedVideo);
    XG_STATE_VAR(s, dotClocks);
    XG_STATE_VAR(s, maskDepth);
    XG_STATE_VAR(s, delivering);
    XG_STATE_VAR(s, deliveringFrame);
    XG_STATE_VAR(s, gameSpeed); /* (battle speed: the audio clock's anchor and rate) */
    XG_STATE_VAR(s, audioBaseV);
    XG_STATE_VAR(s, audioBaseA);
    lockQueue();
    XG_STATE_VAR(s, queueSeq);
    int count = (int)xgStateLen(s, "queueCount", (size_t)queueCount);
    if (xgStateLoading(s) && count > queueCap) {
        queueCap = count;
        queue = realloc(queue, (size_t)queueCap * sizeof(*queue));
        if (queue == NULL) {
            fprintf(stderr, "hwStateIo: out of memory\n");
            abort();
        }
    }
    xgStateIo(s, "queue", queue, (size_t)count * sizeof(*queue));
    if (xgStateLoading(s)) {
        queueCount = count;
        noteEarliest();
    }
    unlockQueue();
    if (xgStateLoading(s)) {
        if (gpuHostReset) gpuHostReset();
        if (stepClock) {
            stepNow = now;
        } else {
            uint64_t t = SDL_GetTicksNS();
            startAt = now;
            startNs = pausedAt != 0 ? pausedAt : t;
            if (lastBeatNs != 0) {
                lastBeatNs = t;
            }
        }
    }
}
