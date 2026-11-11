/* A sustained overload reduces one step; recovery needs five seconds of
 * headroom for the next step. The conservative N-squared prediction includes
 * native work too. This can leave spare capacity, but avoids repeated probes
 * into an expensive scale. No wall clock or game state enters this policy. */
#include "autores.h"
#include <math.h>
#include <string.h>

void autoResolutionReset(AutoResolution* a, int ceiling) {
    memset(a, 0, sizeof(*a));
    a->ceiling = a->limit = ceiling;
    a->factor = 1;
}

void autoResolutionRestart(AutoResolution* a) {
    a->scale = 0;
    a->cooldown = a->count = a->over = a->headroom = 0;
    a->total = a->mean = 0;
}

void autoResolutionCeiling(AutoResolution* a, int ceiling) {
    if (ceiling == a->ceiling) return;
    if (!a->constrained || a->limit > ceiling) a->limit = ceiling;
    a->ceiling = ceiling;
    a->count = a->over = a->headroom = 0;
    a->total = 0;
}

/* Restarting sets a new-scale warm-up (60 vblanks), which also discards the
 * samples of the old speed that arrive late (about one vblank). */
void autoResolutionSpeed(AutoResolution* a, int factor) {
    factor = factor > 1 ? factor : 1;
    if (factor == a->factor) return;
    a->factor = factor;
    autoResolutionRestart(a);
}

void autoResolutionSample(AutoResolution* a, int scale, double budget, double ms, unsigned frames) {
    if (scale < 1 || scale > 12 || !isfinite(budget) || budget <= 0 ||
        !isfinite(ms) || ms < 0 || frames == 0 || frames > 120) return;
    if (a->factor > 1) budget /= a->factor; /* a video frame's budget shared by its game vblanks */
    if (scale != a->scale) {
        a->scale = scale;
        a->cooldown = 60; /* discard allocation/refill and shader warm-up */
        a->count = a->over = a->headroom = 0;
        a->total = 0;
    }
    if (a->cooldown) {
        a->cooldown = frames >= a->cooldown ? 0 : a->cooldown - frames;
        return;
    }
    a->total += ms;
    a->count += frames;
    if (a->count < 30) return;
    a->mean = a->total / a->count;
    if (a->mean > budget) {
        a->over++;
        a->headroom = 0;
    } else {
        a->over = 0;
        double ratio = (scale + 1.0) / scale;
        if (a->constrained && scale == a->limit && scale < a->ceiling &&
            a->mean * ratio * ratio < budget * 0.7)
            a->headroom += a->count;
        else a->headroom = 0;
    }
    if (a->over >= 2 && scale > 1) {
        a->limit = scale - 1;
        if (a->limit > a->ceiling) a->limit = a->ceiling;
        a->constrained = 1;
        a->over = a->headroom = 0;
    } else if (a->headroom >= 300) {
        a->limit = scale + 1;
        a->constrained = a->limit < a->ceiling;
        a->over = a->headroom = 0;
    }
    a->total = 0;
    a->count = 0;
}
