/* Address-validated projection metadata. No value-only vertex cache: a HUD
 * word equal to a projected point is not evidence that it is that vertex. */
#include "gte_pc.h"
#include "../system/savestate.h"
#include <stdint.h>
#include <string.h>

#define SLOTS 65536
#define PROBES 32
#define MAX_AGE 2 /* packets can be constructed across a vblank */
static struct {
    const void* addr;
    unsigned epoch;
    GtePrecise vertex;
} entries[SLOTS];
static unsigned epoch = 1;
static int enabled;
static unsigned pendingDraws;

static unsigned hash(const void* addr) {
    uintptr_t p = (uintptr_t)addr >> 2;
    p ^= p >> 16;
    p *= UINT64_C(0x9e3779b1);
    return (unsigned)p & (SLOTS - 1);
}

void gtePrecisionEnable(int on) {
    on = on != 0;
    if (enabled != on) {
        memset(entries, 0, sizeof(entries));
        epoch = 1;
        enabled = on;
    }
}
int gtePrecisionEnabled(void) { return enabled; }
void gtePrecisionNewFrame(void) {
    /* A slow host can deliver several vblanks while the field constructs
     * and submits one ordering table. Its pending vertices still belong
     * to that draw, regardless of elapsed display refreshes. */
    if (pendingDraws) return;
    if (++epoch == 0) {
        memset(entries, 0, sizeof(entries));
        epoch = 1;
    }
}

void gtePrecisionBeginDraw(void) {
    if (!pendingDraws) gtePrecisionNewFrame();
    pendingDraws++;
}

void gtePrecisionEndDraw(void) {
    if (pendingDraws) pendingDraws--;
}

void gtePrecisionRecord(void* addr, const GtePrecise* vertex) {
    if (!enabled) return;
    unsigned first = hash(addr);
    int freeSlot = -1;
    for (unsigned i = 0; i < PROBES; i++) {
        unsigned slot = (first + i) & (SLOTS - 1);
        if (entries[slot].addr == addr) {
            freeSlot = (int)slot;
            break;
        }
        if (freeSlot < 0 && (!entries[slot].addr || epoch - entries[slot].epoch > MAX_AGE)) freeSlot = (int)slot;
    }
    if (freeSlot >= 0) {
        entries[freeSlot].addr = addr;
        entries[freeSlot].epoch = epoch;
        entries[freeSlot].vertex = *vertex;
    }
}

int gtePrecisionLookup(const void* addr, u32 word, GtePrecise* out) {
    if (!enabled) return 0;
    unsigned first = hash(addr);
    for (unsigned i = 0; i < PROBES; i++) {
        unsigned slot = (first + i) & (SLOTS - 1);
        /* An empty slot ends the probe: gtePrecisionRecord puts an address in
         * the first empty or stale slot of its sequence and never empties one
         * (only the whole table is cleared), so an address recorded past an
         * empty slot cannot exist. */
        if (!entries[slot].addr) return 0;
        if (entries[slot].addr == addr) {
            const GtePrecise* p = &entries[slot].vertex;
            if (epoch - entries[slot].epoch <= MAX_AGE && p->valid && p->word == word) {
                *out = *p;
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

void gtePrecisionForget(const void* addr) {
    GtePrecise invalid = {0};
    gtePrecisionRecord((void*)addr, &invalid);
}

void gteCopyPrecision(void* dst, const void* src) {
    if (!enabled) return;
    u32 a, b;
    GtePrecise p = {0};
    memcpy(&a, src, 4);
    memcpy(&b, dst, 4);
    if (a == b) gtePrecisionLookup(src, a, &p);
    gtePrecisionRecord(dst, &p);
}

void gteCopySxy(void* dst, const void* src) {
    memcpy(dst, src, 4);
    gteCopyPrecision(dst, src);
}

/* Save states (system/savestate.h): the projections recorded before a load
 * belong to another timeline. */
void precisionStateLoaded(void) {
    memset(entries, 0, sizeof(entries));
    epoch = 1;
    pendingDraws = 0;
}
