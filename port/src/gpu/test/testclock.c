/*
 * A minimal clock for the GPU layer's test programs (port/src/system has the
 * real one): events run at the next safe point once due.
 *
 * Time stands still here, so only what is due at once ever runs (a
 * DrawSyncCallback's callback). The GPU layer's own vblanks (libgpu.c
 * selfVBlank, a period after gpuInit) never come: a test's vblanks are the
 * gpuVBlank calls it makes, the same on a fast host and a busy one. On the
 * wall clock they landed wherever the host's speed put them: they aged the
 * precision tags (xg-htn.34.2.5) and set the hi-res copy back to the settings'
 * scale in the middle of a test (xg-htn.35.83).
 */

#include <SDL3/SDL.h>
#include <string.h>

#include "system/hw.h"

typedef struct {
    double at;
    HwEvent fn;
    void* arg;
} Event;

static Event events[256];
static int eventCount;
static int delivering;

double hwNow(void) {
    return 0.0;
}

void hwScheduleAt(double at, HwEvent fn, void* arg) {
    if (eventCount < 256) {
        events[eventCount++] = (Event){at, fn, arg};
    }
}

void hwSafePoint(void) {
    if (delivering) {
        return;
    }
    delivering = 1;
    double now = hwNow();
    for (int i = 0; i < eventCount;) {
        if (events[i].at <= now) {
            Event e = events[i];
            memmove(&events[i], &events[i + 1], sizeof(Event) * (size_t)(eventCount - i - 1));
            eventCount--;
            e.fn(e.arg);
            i = 0;
        } else {
            i++;
        }
    }
    delivering = 0;
}

void hwSetDisplayTiming(int interlaced, int dotClocks) {
    (void)interlaced;
    (void)dotClocks;
}

void pcTextImageUploaded(const void* data, int bytes) {
    (void)data;
    (void)bytes;
}
