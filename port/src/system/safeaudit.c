/*
 * The safe-point audit (XG_SAFE_AUDIT=<file>; docs/CANONICAL_STATES.md, safe
 * points at every wait): where the game waits for a vblank with no safe point
 * near.
 *
 * A slot state loads in another build only when it is taken at a safe point
 * (stateportable.h). A slot save waits SAFE_WAIT vblanks for one and is saved
 * for this build only when none comes (savestate.c stateVBlankPoint). The goal
 * is a safe point at every wait, so no save ever misses; this audit is the
 * inventory of the waits that still have none, and later the gate that keeps
 * the count at zero.
 *
 * At each vblank park (savestate.c stateVBlankPoint) a vblank counts as
 * covered when a safe point (savestate.c stateSafePark: pcSafePoint with a
 * real id) ran within GRACE vblanks: a mode that draws every 2nd or 3rd vblank
 * reaches its loop top less often than the vblanks come. Otherwise the game
 * thread's backtrace is taken there and counted by its frames: each distinct
 * wait is one entry, with how often it was seen and the longest stretch
 * without a safe point it was part of. A stretch longer than SAFE_WAIT is one
 * a slot save would have missed.
 *
 * The report (sorted by vblanks seen, symbols by stall.c stallNameOf) is
 * rewritten every DUMP_VBLANKS and at exit. XG_SAFE_AUDIT=- or 1: stderr.
 * Linux (glibc) only: elsewhere it says so once and does nothing.
 * Diagnostics only: it changes no state and nothing a state holds.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hw.h"

void safeAuditSafePoint(void);
void safeAuditVBlank(void);

#if defined(__linux__) && defined(__GLIBC__)
#include <execinfo.h>

void stallNameOf(void* p, char* out, size_t size); /* stall.c */

#define GRACE 6          /* vblanks: a frame's longest wait at a loop top */
#define SAFE_WAIT 120    /* savestate.c: how long a slot save waits */
#define DUMP_VBLANKS 1200
#define KEY_FRAMES 16    /* frames that tell two waits apart */
#define SKIP_FRAMES 1    /* this function's own frame */
#define ENTRIES 1024

typedef struct {
    uint64_t hash;
    void* frames[KEY_FRAMES];
    int n;
    uint32_t seen;
    uint32_t longest; /* the longest stretch without a safe point this wait was in */
    uint32_t missed;  /* stretches past SAFE_WAIT that reached it */
    char** names;     /* symbolised once, at the first report that prints it */
} Entry;

static struct {
    int state; /* 0 not read, 1 on, -1 off */
    char path[1024];
    uint32_t lastSafe, run;
    int haveSafe;
    uint64_t vblanks, covered, uncovered, stretches, longest;
    uint32_t sinceDump;
    Entry e[ENTRIES];
    int used, dropped;
} au;

static void dump(void);

static int on(void) {
    if (au.state == 0) {
        const char* p = getenv("XG_SAFE_AUDIT");
        if (p == NULL || p[0] == '\0' || strcmp(p, "0") == 0) {
            au.state = -1;
        } else {
            au.state = 1;
            snprintf(au.path, sizeof au.path, "%s", strcmp(p, "1") == 0 ? "-" : p);
            atexit(dump);
            fprintf(stderr, "safe-audit: on, report to %s\n", strcmp(au.path, "-") == 0 ? "stderr" : au.path);
        }
    }
    return au.state > 0;
}

void safeAuditSafePoint(void) {
    if (!on()) return;
    au.lastSafe = hwVSyncCount();
    au.haveSafe = 1;
}

static Entry* find(void* const* frames, int n) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < n; i++) {
        h = (h ^ (uint64_t)(uintptr_t)frames[i]) * 1099511628211ull;
    }
    for (int k = 0; k < ENTRIES; k++) {
        Entry* e = &au.e[(h + (uint64_t)k) % ENTRIES];
        if (e->n == 0) {
            if (au.used >= ENTRIES * 3 / 4) {
                au.dropped++;
                return NULL;
            }
            au.used++;
            e->hash = h;
            e->n = n;
            memcpy(e->frames, frames, (size_t)n * sizeof(void*));
            return e;
        }
        if (e->hash == h && e->n == n && memcmp(e->frames, frames, (size_t)n * sizeof(void*)) == 0) {
            return e;
        }
    }
    au.dropped++;
    return NULL;
}

void safeAuditVBlank(void) {
    if (!on()) return;
    uint32_t now = hwVSyncCount();
    au.vblanks++;
    if (++au.sinceDump >= DUMP_VBLANKS) {
        au.sinceDump = 0;
        dump();
    }
    if (au.haveSafe && now - au.lastSafe <= GRACE) {
        au.covered++;
        au.run = 0;
        return;
    }
    au.uncovered++;
    au.run = au.haveSafe ? now - au.lastSafe : au.run + 1;
    if (au.run == SAFE_WAIT + 1) au.stretches++;
    if (au.run > au.longest) au.longest = au.run;
    void* frames[KEY_FRAMES + SKIP_FRAMES];
    int n = backtrace(frames, KEY_FRAMES + SKIP_FRAMES) - SKIP_FRAMES;
    if (n <= 0) return;
    Entry* e = find(frames + SKIP_FRAMES, n);
    if (e == NULL) return;
    e->seen++;
    if (au.run > e->longest) e->longest = au.run;
    if (au.run == SAFE_WAIT + 1) e->missed++;
}

static int bySeen(const void* a, const void* b) {
    const Entry* x = *(const Entry* const*)a;
    const Entry* y = *(const Entry* const*)b;
    return x->seen < y->seen ? 1 : x->seen > y->seen ? -1 : 0;
}

static void dump(void) {
    if (au.state <= 0) return;
    int toErr = strcmp(au.path, "-") == 0;
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.new", au.path);
    FILE* f = toErr ? stderr : fopen(tmp, "w");
    if (f == NULL) return;
    fprintf(f, "safe-point audit (XG_SAFE_AUDIT): vblanks %llu, near a safe point %llu (%.1f%%), without %llu\n",
            (unsigned long long)au.vblanks, (unsigned long long)au.covered,
            au.vblanks ? 100.0 * (double)au.covered / (double)au.vblanks : 0.0, (unsigned long long)au.uncovered);
    fprintf(f, "stretches without one past %d vblanks (a slot save made there is for this build only): %llu, "
               "longest %llu vblanks\n", SAFE_WAIT, (unsigned long long)au.stretches, (unsigned long long)au.longest);
    fprintf(f, "distinct waits: %d%s\n\n", au.used, au.dropped ? " (table full: some not counted)" : "");
    Entry* order[ENTRIES];
    int m = 0;
    for (int i = 0; i < ENTRIES; i++) {
        if (au.e[i].n > 0 && au.e[i].seen > 0) order[m++] = &au.e[i];
    }
    qsort(order, (size_t)m, sizeof(order[0]), bySeen);
    for (int i = 0; i < m; i++) {
        Entry* e = order[i];
        if (e->names == NULL) {
            e->names = calloc((size_t)e->n, sizeof(char*));
            for (int k = 0; e->names != NULL && k < e->n; k++) {
                char name[256];
                stallNameOf(e->frames[k], name, sizeof name);
                e->names[k] = strdup(name);
            }
        }
        fprintf(f, "wait %d: seen %u vblanks, longest stretch %u, missed saves %u\n", i + 1, e->seen, e->longest,
                e->missed);
        for (int k = 0; k < e->n; k++) {
            fprintf(f, "    #%-2d %s\n", k, e->names != NULL && e->names[k] != NULL ? e->names[k] : "?");
        }
    }
    if (!toErr) {
        fclose(f);
        rename(tmp, au.path);
    }
}

#else

static int said;

void safeAuditSafePoint(void) {}

void safeAuditVBlank(void) {
    if (!said) {
        said = 1;
        const char* p = getenv("XG_SAFE_AUDIT");
        if (p != NULL && p[0] != '\0' && strcmp(p, "0") != 0) {
            fprintf(stderr, "safe-audit: Linux (glibc) builds only\n");
        }
    }
}

#endif
