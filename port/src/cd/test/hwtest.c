/*
 * A minimal clock and interrupt queue (port/src/system/hw.h) for cdtest, which
 * runs the CD layer without the rest of the port: real time, events delivered
 * at safe points, never nested.
 */

#include "system/hw.h"

#include <SDL3/SDL.h>
#include <stdio.h>

#define MAX_PENDING 1024

typedef struct {
    double at;
    HwEvent fn;
    void* arg;
} Pending;

static Pending pending[MAX_PENDING];
static int pendingCount;
static int delivering;
static int masked;

double hwNow(void) {
    return (double)SDL_GetTicksNS() / 1e9;
}

void hwScheduleAt(double at, HwEvent fn, void* arg) {
    if (pendingCount >= MAX_PENDING) {
        fprintf(stderr, "cdtest: event queue full\n");
        return;
    }
    int i = pendingCount++;
    while (i > 0 && pending[i - 1].at > at) {
        pending[i] = pending[i - 1];
        i--;
    }
    pending[i] = (Pending){ at, fn, arg };
}

static int deliver(void) {
    if (delivering || masked) {
        return 0;
    }
    delivering = 1;
    int n = 0;
    while (pendingCount > 0 && pending[0].at <= hwNow()) {
        Pending p = pending[0];
        pendingCount--;
        for (int i = 0; i < pendingCount; i++) {
            pending[i] = pending[i + 1];
        }
        p.fn(p.arg);
        n++;
    }
    delivering = 0;
    return n;
}

void hwSafePoint(void) {
    deliver();
}

void hwYield(void) {
    if (deliver() == 0) {
        double wait = pendingCount > 0 ? pending[0].at - hwNow() : 0.001;
        if (wait > 0.001) {
            wait = 0.001;
        }
        if (wait > 0) {
            SDL_DelayNS((Uint64)(wait * 1e9));
        }
        deliver();
    }
}

void hwMaskInterrupts(void) {
    masked++;
}

void hwUnmaskInterrupts(void) {
    if (masked > 0) {
        masked--;
    }
}

void portYield(void) {
    hwYield();
}
