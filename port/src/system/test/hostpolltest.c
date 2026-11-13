/* The host's presentation budget (hw.c hostPoll) on the real clock: a
 * presentation may only run until the wait gives control back to the game.
 * A plain yield returns after any event (the next queued one bounds it); a
 * wait for the vblank (hwWaitVSync, a VSync(-1) busy wait) returns only at
 * the vblank, with an earlier timer still delivered at its deadline. Also
 * the park of Speedup's catch-up (hwWaitVSyncSince) once per vblank. */
#include "../hw.c"
static int checks;
#include "test/check.h"

/* hw.c's callees outside the clock: nothing of theirs is under test. */
int cpuTimingOn; /* (hw.c's weak data reference: PE links need a definition) */
void etcVBlank(void) {}
void padInit(void) {}
void padVBlank(void) {}
void quitInit(void) {}
void quitPoll(int pump) { (void)pump; }
void stallInit(void) {}
void invariantVBlank(void) {}
void slowdownSafePoint(void) {}
void spuSync(double now) { (void)now; }

/* The GPU's host presentation: one slot due when armed. The poll records
 * the budget it was given and, at the same moment, how far the next vblank
 * and the next queued event are. */
static int armed, polls, parks, fired;
static double given, toVBlank, toEvent;
uint64_t gpuHostDeadline(void) { return armed ? 0 : UINT64_MAX; }
void gpuHostPoll(uint64_t now, double vblank, uint64_t available) {
    (void)now; (void)vblank;
    double t = hwNow();
    polls++;
    armed = 0;
    given = (double)available * 1e-9;
    toVBlank = nextVBlank - t;
    toEvent = nextEventTime() - t;
}
void stateVBlankPoint(void) { parks++; }
static void timer(void* arg) { (void)arg; fired++; }

/* Right after a vblank: a timer 4 ms ahead, the next vblank ~16 ms. */
static void freshFrame(void) {
    hwWaitVSync();
    hwScheduleAt(hwNow() + 0.004, timer, NULL);
}

/* The poll ran with the timer still ahead (an overloaded host can deliver
 * it first: try again then). */
static int sampled(int before) {
    return polls == before + 1 && toEvent > 0.001 && toVBlank - toEvent > 0.005;
}

int main(void) {
    SDL_unsetenv_unsafe("XG_CLOCK");
    CHECK(SDL_Init(0));
    CHECK(hwInit() && !stepClock);

    int ok = 0;
    for (int attempt = 0; attempt < 20 && !ok; attempt++) {
        freshFrame();
        int before = polls;
        armed = 1;
        hwYield();
        armed = 0;
        ok = sampled(before);
        if (ok) CHECK(fabs(given - toEvent) < 0.0005); /* the next event, not the vblank */
    }
    CHECK(ok);

    ok = 0;
    for (int attempt = 0; attempt < 20 && !ok; attempt++) {
        freshFrame();
        int before = polls, park = parks;
        uint32_t count = hwVSyncCount();
        armed = 1;
        CHECK(hwWaitVSync() == count + 1 && parks == park + 1);
        armed = 0;
        ok = sampled(before);
        if (ok) CHECK(fabs(given - toVBlank) < 0.0005); /* the vblank ends this wait */
    }
    CHECK(ok);

    ok = 0;
    for (int attempt = 0; attempt < 20 && !ok; attempt++) {
        freshFrame();
        int before = polls;
        armed = 1;
        hwPresentPoll();
        armed = 0;
        ok = sampled(before);
        if (ok) CHECK(fabs(given - toVBlank) < 0.0005);
    }
    CHECK(ok);

    /* No presentation while the clock is paused or interrupts are masked. */
    int before = polls;
    hwPause(1);
    armed = 1; hwPresentPoll(); CHECK(polls == before);
    hwPause(0);
    hwMaskInterrupts();
    hwPresentPoll(); CHECK(polls == before);
    armed = 0;
    hwUnmaskInterrupts();

    /* Catch-up: a vblank that passed while the game drew ends the wait at
     * once with one park; the next call waits for a new vblank. */
    uint32_t previous = hwWaitVSync();
    hwBusy(hwVSyncPeriod() * 1.25);
    CHECK((int32_t)(hwVSyncCount() - previous) > 0);
    int park = parks;
    uint32_t now = hwVSyncCount();
    CHECK(hwWaitVSyncSince(previous) == now && parks == park + 1);
    CHECK(hwWaitVSyncSince(now) == now + 1 && parks == park + 2);

    SDL_Quit();
    printf("Host presentation budget: %d checks passed (%d timers)\n", checks, fired);
    return 0;
}
