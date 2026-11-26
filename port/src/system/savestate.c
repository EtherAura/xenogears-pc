/*
 * Save states (savestate.h): requests, the park, the snapshot's contents.
 *
 * A state's body is a series of chunks, each a tag (a byte of length, the
 * text), a u32 size and the bytes:
 *   mem.<module>.data/.bss  every game module's sections (portModules[])
 *   mem.heap                the heap arena
 *   mem.scratchpad          the PS1 scratchpad's 1 KB at 0x1F800000
 *   mem.stack.sp, .low,     the game's stack pointer at the park, the deepest
 *   mem.stack               page the stack ever reached, and the stack from
 *                           there to its top (the frames and what lies below)
 *   mem.guard               the stack protector's value in the saved frames
 *   <part>.<variable>...    each platform file's xxxStateIo, in parts[]'s order
 * A save writes them (SAVE); a load first walks the whole body checking
 * every tag and size (VERIFY: nothing is touched, so a damaged or foreign
 * state leaves the game as it was), then writes it back (LOAD) and runs the
 * files' load hooks. The game is parked meanwhile (gamestack.h): this runs
 * on the host side, the game stack free to be rewritten.
 */

#include "engine.h"
#include "savestate.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "main/game_state.h"
#include "../card/card.h"
#include "../cd/cd.h"
#include "../cd/verify.h"
#include "../gpu/gpu.h"
#include "../gpu/gpu_internal.h"
#include "cputime.h"
#include "gamestack.h"
#include "statecodec.h"
#include "statefile.h"
#include "stateimage.h"
#include "stateportable.h"
#include "system.h"
#include "world.h"
#include "cores.h"

#ifdef VERSION_JP
#define REGION "JP"
#else
#define REGION "US"
#endif

/* gen/modules.c (memory.c has the same view). */
typedef struct {
    const char* name;
    char* dataStart;
    char* dataEnd;
    char* bssStart;
    char* bssEnd;
} PortModule;
extern PortModule portModules[];
extern const int portModuleCount;

unsigned char* portHeapStart(void);
unsigned char* portHeapEnd(void);
int portSavedModuleOnHeap(void);
int portGameMode(void);
int32_t portOverlayCd(void);
void cardImageStateIo(XgState* s);
extern u8 g_playTimeHours, g_playTimeMinutes, g_playTimeSeconds;

extern unsigned char pcScratchpad[]; /* system/memory.c: the PS1 scratchpad (include/common.h PSX_SCRATCH) */
#define SCRATCHPAD pcScratchpad
#define SCRATCHPAD_SIZE 0x400

/* ---- the chunks ----------------------------------------------------------------------- */

/* SAVE writes a body; LIST lists the chunks for a rewind capture (rewind.c);
 * VERIFY and LOAD read a body or a list of chunks. */
enum { MODE_SAVE, MODE_LIST, MODE_VERIFY, MODE_LOAD };

typedef struct {
    size_t tagOff;
    int tagLen;
    const uint8_t* data; /* the game's static memory, or NULL: a copy at dataOff */
    size_t dataOff, size;
    int late; /* device pixels still on their way (xgStateLate) */
    int direct; /* a complete late read: data is bound to its mapping after the wait */
    int aside; /* in place, in a copy the module keeps until its next snapshot (xgStateIoAside) */
} ListEntry;

struct XgState {
    int mode;
    uint8_t* out; /* SAVE: the body; LIST: tags and the copied chunks */
    size_t size, cap;
    ListEntry* list; /* LIST */
    int listCount, listCap;
    const uint8_t* in; /* VERIFY, LOAD: a body ... */
    size_t inSize, pos;
    const StateChunkRef* chunks; /* ... or a list */
    int chunkCount, chunk;
    const char* part;
    int failed;
    char why[256];
    int late; /* LIST: device reads may be queued (xgStateLate) */
};

int xgStateLoading(const XgState* s) {
    return s->mode == MODE_LOAD;
}

int xgStateSaving(const XgState* s) {
    return s->mode == MODE_SAVE || s->mode == MODE_LIST;
}

static void fail(XgState* s, const char* fmt, ...) {
    if (!s->failed) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(s->why, sizeof s->why, fmt, ap);
        va_end(ap);
        s->failed = 1;
    }
    s->pos = s->inSize;
    s->chunk = s->chunkCount;
}

static void grow(XgState* s, size_t more) {
    if (s->size + more <= s->cap) {
        return;
    }
    size_t cap = s->cap ? s->cap : (size_t)24 << 20;
    while (cap < s->size + more) {
        cap *= 2;
    }
    uint8_t* p = realloc(s->out, cap);
    if (p == NULL) {
        fprintf(stderr, "state: out of memory for %zu bytes\n", cap);
        abort();
    }
    s->out = p;
    s->cap = cap;
}

/* A chunk's tag: "<part>.<name>". */
static int tagOf(const XgState* s, const char* name, char* out) {
    int n = snprintf(out, 256, "%s.%s", s->part != NULL ? s->part : "", name);
    return n > 255 ? 255 : n;
}

/* Memory that stays where it is while the game is parked: the executable's
 * own data and BSS (modules, the heap arena, the game stack, statics). A
 * rewind capture lists such chunks in place and copies the others. */
#if defined(__linux__)
/* The image's own bounds from the linker (an executable, PIE or not, or a
 * shared library): its ELF header to its end. */
extern const char __ehdr_start[] __attribute__((visibility("hidden")));
extern char _end[] __attribute__((visibility("hidden")));
static int staticMemory(const void* p, size_t n) {
    return (const char*)p >= __ehdr_start && (const char*)p + n <= _end;
}
#elif defined(_WIN32)
/* The executable's image (gamestack.c: its base and size from the PE header). */
void portGameStackBounds(uintptr_t* low, uintptr_t* high);
static int staticMemory(const void* p, size_t n) {
    uintptr_t low, high;
    portGameStackBounds(&low, &high);
    return (uintptr_t)p >= low && (uintptr_t)p + n <= high;
}
#else
static int staticMemory(const void* p, size_t n) {
    (void)p;
    (void)n;
    return 0;
}
#endif

/* ---- late reads: a rewind capture's device pixels ------------------------------------------- */

/* A copy queued in the device (gvkLateRead) and where its pixels go: into
 * static memory the listing points at, or into the listing's copy of the
 * chunk that held dst (an offset: the listing's buffer may still move). */
typedef struct {
    ptrdiff_t at;
    int w, h, pitch;
    uint16_t* dst;
    size_t off;
    int bound; /* 0: unbound, 1: dst, 2: listing.out + off, 3: complete chunk at index off */
} LateRead;

static LateRead* lates;
static int lateCount, lateCap;
static XgState listing; /* the last list (its buffers reused) */
/* The list before's copies, reused by turns: rewind's pack worker may still
 * be comparing its late chunks while the next list is made. */
static struct {
    uint8_t* out;
    size_t cap;
} listingSpare;

int xgStateLate(XgState* s, int wide, int x, int y, int w, int h, uint16_t* dst, int pitch) {
    if (s->mode != MODE_LIST || !s->late) {
        return 0;
    }
    ptrdiff_t at = gvkLateRead(wide, (VRect){x, y, x + w - 1, y + h - 1});
    if (at < 0) {
        return 0;
    }
    if (lateCount == lateCap) {
        lateCap = lateCap ? lateCap * 2 : 64;
        lates = realloc(lates, (size_t)lateCap * sizeof(*lates));
        if (lates == NULL) {
            fprintf(stderr, "state: out of memory\n");
            abort();
        }
    }
    lates[lateCount++] = (LateRead){at, w, h, pitch, dst, 0, 0};
    return 1;
}

/* A chunk listed from p (in place, or copied to `off`) as entry `entry` of
 * the list being made: the late reads whose pixels lie in it now go there. */
static int lateBind(const void* p, size_t n, int keep, size_t off, int entry, int* complete) {
    int any = 0, full = -1;
    *complete = 0;
    for (int i = 0; i < lateCount; i++) {
        const LateRead* l = &lates[i];
        uintptr_t start = (uintptr_t)p, d = (uintptr_t)l->dst;
        size_t span = ((size_t)(l->h - 1) * (size_t)l->pitch + (size_t)l->w) * 2;
        if (!l->bound && d >= start && d - start <= n && span <= n - (d - start)) {
            any++;
            if (d == start && l->pitch == l->w && span == n) full = i;
        }
    }
    /* Exactly one contiguous read covers every byte. Its immutable mapping
     * is valid until the next list, after rewind has compared this one. */
    *complete = !keep && any == 1 && full >= 0;
    for (int i = 0; i < lateCount; i++) {
        LateRead* l = &lates[i];
        uintptr_t start = (uintptr_t)p, d = (uintptr_t)l->dst;
        size_t span = ((size_t)(l->h - 1) * (size_t)l->pitch + (size_t)l->w) * 2;
        if (!l->bound && d >= start && d - start <= n && span <= n - (d - start)) {
            l->bound = *complete ? 3 : keep ? 1 : 2;
            l->off = *complete ? (size_t)entry : off + (size_t)(d - start);
        }
    }
    return any;
}

static StateChunkRef* listed;

void stateLateFinish(void) {
    if (lateCount == 0) {
        return;
    }
    const uint16_t* px = gvkLateWait();
    for (int i = 0; px != NULL && i < lateCount; i++) {
        const LateRead* l = &lates[i];
        if (l->bound == 0) {
            continue;
        }
        if (l->bound == 3) {
            listing.list[l->off].data = (const uint8_t*)(px + l->at);
            listed[l->off].data = listing.list[l->off].data;
            continue;
        }
        uint16_t* d = l->bound == 1 ? l->dst : (uint16_t*)(void*)(listing.out + l->off);
        if (l->pitch == l->w) {
            memcpy(d, px + l->at, (size_t)l->w * l->h * 2);
            continue;
        }
        for (int y = 0; y < l->h; y++) {
            memcpy(d + (size_t)y * (size_t)l->pitch, px + l->at + (ptrdiff_t)y * l->w, (size_t)l->w * 2);
        }
    }
    lateCount = 0;
}

void xgStateIoAside(XgState* s, const char* name, void* p, size_t n) {
    xgStateIo(s, name, p, n);
    if (s->mode == MODE_LIST && s->listCount > 0 && s->list[s->listCount - 1].data != NULL) {
        s->list[s->listCount - 1].aside = 1;
    }
}

void xgStateIo(XgState* s, const char* name, void* p, size_t n) {
    char tag[256];
    int len = tagOf(s, name, tag);
    if (s->mode == MODE_LIST) {
        if (s->listCount == s->listCap) {
            s->listCap = s->listCap ? s->listCap * 2 : 512;
            s->list = realloc(s->list, (size_t)s->listCap * sizeof(*s->list));
            if (s->list == NULL) {
                fprintf(stderr, "state: out of memory\n");
                abort();
            }
        }
        ListEntry* e = &s->list[s->listCount++];
        int keep = n >= 4096 && staticMemory(p, n);
        int complete;
        size_t dataOff = s->size + (((size_t)len + 15) & ~(size_t)15);
        int owed = lateBind(p, n, keep, dataOff, s->listCount - 1, &complete);
        grow(s, (size_t)len + (keep || complete ? 0 : n) + 16);
        e->tagOff = s->size;
        e->tagLen = len;
        memcpy(s->out + s->size, tag, (size_t)len);
        s->size += ((size_t)len + 15) & ~(size_t)15;
        e->size = n;
        e->data = keep ? p : NULL;
        e->dataOff = s->size;
        e->late = owed;
        e->direct = complete;
        e->aside = 0;
        if (!keep && !complete && n > 0) {
            memcpy(s->out + s->size, p, n);
            s->size += (n + 15) & ~(size_t)15;
        }
        return;
    }
    if (s->mode == MODE_SAVE) {
        grow(s, 1 + (size_t)len + 4 + n);
        s->out[s->size++] = (uint8_t)len;
        memcpy(s->out + s->size, tag, (size_t)len);
        s->size += (size_t)len;
        for (int i = 0; i < 4; i++) {
            s->out[s->size++] = (uint8_t)(n >> (8 * i));
        }
        if (n > 0) {
            memcpy(s->out + s->size, p, n);
        }
        s->size += n;
        return;
    }
    if (s->failed) {
        return;
    }
    if (s->chunks != NULL) {
        const StateChunkRef* c = s->chunk < s->chunkCount ? &s->chunks[s->chunk] : NULL;
        if (c == NULL || c->tagLen != len || memcmp(c->tag, tag, (size_t)len) != 0) {
            fail(s, "the capture has %.*s where this build has %s", c != NULL ? c->tagLen : 3,
                 c != NULL ? c->tag : "end", tag);
            return;
        }
        if (c->size != n) {
            fail(s, "%s: %zu bytes in the capture, %zu now", tag, c->size, n);
            return;
        }
        if (s->mode == MODE_LOAD && n > 0) {
            memcpy(p, c->data, n);
        }
        s->chunk++;
        return;
    }
    if (s->pos + 1 > s->inSize || s->pos + 1 + s->in[s->pos] + 4 > s->inSize) {
        fail(s, "the state ends before %s", tag);
        return;
    }
    size_t tl = s->in[s->pos];
    const uint8_t* t = s->in + s->pos + 1;
    size_t size = (size_t)t[tl] | (size_t)t[tl + 1] << 8 | (size_t)t[tl + 2] << 16 | (size_t)t[tl + 3] << 24;
    if (tl != (size_t)len || memcmp(t, tag, tl) != 0) {
        fail(s, "the state has %.*s where this build has %s", (int)tl, (const char*)t, tag);
        return;
    }
    if (size != n || s->pos + 1 + tl + 4 + size > s->inSize) {
        fail(s, "%s: %zu bytes in the state, %zu in this build", tag, size, n);
        return;
    }
    if (s->mode == MODE_LOAD && n > 0) {
        memcpy(p, t + tl + 4, n);
    }
    s->pos += 1 + tl + 4 + size;
}

size_t xgStateLen(XgState* s, const char* name, size_t n) {
    uint64_t v = n;
    if (s->mode == MODE_SAVE || s->mode == MODE_LIST) {
        xgStateIo(s, name, &v, sizeof v);
        return n;
    }
    uint64_t saved = 0;
    int was = s->mode;
    s->mode = was == MODE_VERIFY ? MODE_LOAD : was; /* a length is read in both passes */
    xgStateIo(s, name, &saved, sizeof saved);
    s->mode = was;
    return s->failed ? 0 : (size_t)saved;
}

/* ---- the contents ----------------------------------------------------------------------- */

typedef struct {
    const char* name;
    void (*io)(XgState* s);
} Part;

static const Part parts[] = {
    {"hw", hwStateIo},           {"api", apiStateIo},         {"etc", etcStateIo},
    {"libc", libcStateIo},       {"pad", padStateIo},         {"memory", memoryStateIo},
    {"gameplay", gameplayStateIo}, {"gauntlet", gauntletStateIo}, {"gameid", gameIdStateIo}, {"arena", arenaStateIo},
    {"options", optionsStateIo}, {"gte", gteStateIo},         {"libgte", libgteStateIo},
    {"card", cardStateIo},       {"cardimage", cardImageStateIo}, {"spu", spuStateIo},
    {"spucore", spuCoreStateIo}, {"spucd", spuCdStateIo},     {"drive", driveStateIo},
    {"libcd", libcdStateIo},     {"stream", streamStateIo},   {"swap", swapStateIo},
    {"xa", xaStateIo},           {"boot", bootStateIo},       {"regional", regionalStateIo},
    {"press", pressStateIo},     {"mdec", mdecStateIo},       {"vlc", vlcStateIo},
    {"gpu", gpuStateIo},         {"vram", vramStateIo},       {"gp0", gp0StateIo},
    {"wide", wideStateIo},       {"prim", primStateIo},       {"quitat", quitAtStateIo},
    {"gametext", gametextStateIo}, {"gametextfont", gametextFontStateIo}, {"moviepack", moviePackStateIo},
    {"battlespeed", battleSpeedStateIo},
};

/* The stack protector's guard value: random per process (glibc keeps it in
 * the thread block at %fs:0x28 on x86-64 and in the global __stack_chk_guard
 * on aarch64; Android's bionic on aarch64 in the thread block's guard slot,
 * TPIDR_EL0 + 0x28). The saved frames hold the saving process's value, so a
 * load from another run puts this run's value in their place
 * (relocateGuard), or the first restored function to return would stop the
 * program. */
#if defined(__aarch64__) && defined(__linux__) && !defined(__ANDROID__)
extern uintptr_t __stack_chk_guard;
#endif
static uint64_t stackGuard(void) {
#if defined(__x86_64__) && defined(__linux__)
    uint64_t v;
    __asm__ volatile("movq %%fs:0x28, %0" : "=r"(v));
    return v;
#elif defined(__aarch64__) && defined(__ANDROID__)
    uint64_t* tls;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tls));
    return tls[5];
#elif defined(__aarch64__) && defined(__linux__)
    return (uint64_t)__stack_chk_guard;
#else
    return 0;
#endif
}

/* Every word equal to the old guard, in [from, to), becomes the new one
 * (the guard is 64 random bits: nothing else matches it). */
static void relocateGuard(unsigned char* from, unsigned char* to, uint64_t old, uint64_t now) {
    uint64_t* p = (uint64_t*)(((uintptr_t)from + 7) & ~(uintptr_t)7);
    uint64_t* end = (uint64_t*)((uintptr_t)to & ~(uintptr_t)7);
    for (; p < end; p++) {
        if (*p == old) {
            *p = now;
        }
    }
}

/* The last load's state had no stack: the game starts again at its safe point (serveLoad). */
static int stackless;

/* port/src/game/main/statetypes.c: the saved mode's loop, taken up at its safe point. */
void pcResumeGame(void);

static void memoryRegions(XgState* s) {
    char name[96];
    s->part = "mem";
    for (int i = 0; i < portModuleCount; i++) {
        PortModule* m = &portModules[i];
        snprintf(name, sizeof name, "%s.data", m->name);
        xgStateIo(s, name, m->dataStart, m->dataEnd > m->dataStart ? (size_t)(m->dataEnd - m->dataStart) : 0);
        snprintf(name, sizeof name, "%s.bss", m->name);
        xgStateIo(s, name, m->bssStart, m->bssEnd > m->bssStart ? (size_t)(m->bssEnd - m->bssStart) : 0);
    }
    xgStateIo(s, "heap", portHeapStart(), (size_t)(portHeapEnd() - portHeapStart()));
    xgStateIo(s, "scratchpad", SCRATCHPAD, SCRATCHPAD_SIZE);

    /* The stack from the deepest page its frames ever reached to the top:
     * the live frames (the switch's saved registers at the parked stack
     * pointer) and what lies below them, which an uninitialised local in a
     * later call reads (pages lower down were never written: zero). */
    unsigned char* base = gameStackBase();
    unsigned char* top = base + gameStackSize();
    unsigned char* at = (unsigned char*)(uintptr_t)xgStateLen(s, "stack.sp", (size_t)(uintptr_t)gameStackParkedSp());
    if (!xgStateSaving(s) && at == NULL) {
        /* A state without the game's frames (a canonical state decoded for this build, which
         * cannot carry them): the game starts again from the safe point it was saved at
         * (serveLoad: gameStackRestart, kernel.c pcResumeGame). */
        (void)xgStateLen(s, "stack.low", 0);
        xgStateIo(s, "stack", NULL, 0);
        (void)xgStateLen(s, "guard", 0);
        if (s->mode == MODE_LOAD && !s->failed) {
            stackless = 1;
        }
        return;
    }
    unsigned char* low = xgStateSaving(s) ? gameStackLowWater() : NULL;
    if (low != NULL && low > at) {
        low = (unsigned char*)((uintptr_t)at & ~(uintptr_t)4095);
    }
    low = (unsigned char*)(uintptr_t)xgStateLen(s, "stack.low", (size_t)(uintptr_t)low);
    if (!xgStateSaving(s) && !s->failed && (at < base || at >= top || low < base || low > at)) {
        fail(s, "the state's stack is not on this build's game stack");
    }
    if (s->failed) {
        return;
    }
    if (s->mode == MODE_LOAD) {
        unsigned char* now = gameStackLowWater();
        if (now < low) {
            memset(now, 0, (size_t)(low - now)); /* deeper than the state's frames went: never written there */
        }
        gameStackSetLowWater(low);
    }
    xgStateIo(s, "stack", low, (size_t)(top - low));
    uint64_t guard = (uint64_t)xgStateLen(s, "guard", (size_t)stackGuard());
    if (s->mode == MODE_LOAD && !s->failed) {
        gameStackSetParkedSp(at);
        uint64_t now = stackGuard();
        if (guard != now) {
            /* The game stack and Battling's (in its module's BSS). */
            relocateGuard(low, top, guard, now);
            for (int i = 0; i < portModuleCount; i++) {
                relocateGuard((unsigned char*)portModules[i].bssStart, (unsigned char*)portModules[i].bssEnd, guard, now);
                relocateGuard((unsigned char*)portModules[i].dataStart, (unsigned char*)portModules[i].dataEnd, guard,
                              now);
            }
        }
    }
}

static void walk(XgState* s) {
    memoryRegions(s);
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]) && !s->failed; i++) {
        s->part = parts[i].name;
        parts[i].io(s);
    }
    s->part = NULL;
}

/* ---- notes ------------------------------------------------------------------------------ */

static void note(const char* fmt, ...) {
    char text[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    fprintf(stderr, "state: %s\n", text);
    presentShowNote(text);
}

static int logOn(void) {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("XG_STATE_LOG");
        on = e != NULL && atoi(e) != 0;
    }
    return on;
}

/* ---- the header, the thumbnail ----------------------------------------------------- */

static const char* const modeNames[7] = {"Kernel menu", "Field", "Battle", "World map", "Battling", "Menu", "Movie"};

static const char* serialOf(int disc) {
#ifdef VERSION_JP
    return disc == 2 ? "SLPS_011.61" : "SLPS_011.60";
#else
    return disc == 2 ? "SLUS_006.69" : "SLUS_006.64";
#endif
}

static void fillHeader(StateHeader* h, size_t bodySize, const uint8_t* body) {
    memset(h, 0, sizeof(*h));
    memcpy(h->buildId, stateBuildId(), 16);
    snprintf(h->appVersion, sizeof h->appVersion, "%s", stateAppVersion());
    h->engine = (uint32_t)engineThis();
    snprintf(h->region, sizeof h->region, "%s", REGION);
    h->disc = (uint32_t)cdBootDiscNumber();
    snprintf(h->serial, sizeof h->serial, "%s", serialOf((int)h->disc));
    h->sectors = (uint32_t)cdDiscSectorCount();
    h->vblank = hwVSyncCount();
    h->savedAt = (int64_t)time(NULL);
    h->playTime = (uint32_t)g_playTimeHours * 3600u + (uint32_t)g_playTimeMinutes * 60u + g_playTimeSeconds;
    h->mode = portGameMode();
    h->place = h->mode == 1 ? (int32_t)gameState.m231A_fieldID : -1;
    const char* mode = h->mode >= 0 && h->mode < 7 ? modeNames[h->mode] : "Starting";
    if (h->place >= 0) {
        snprintf(h->label, sizeof h->label, "%s %d", mode, h->place);
    } else {
        snprintf(h->label, sizeof h->label, "%s", mode);
    }
    const char* clock = getenv("XG_CLOCK");
    h->flags = clock != NULL && strcmp(clock, "step") == 0 ? STATE_FLAG_STEPPED : 0;
    h->dumpFrames = spuOutputDumpFrames();
    h->thumbW = STATE_THUMB_W;
    h->thumbH = STATE_THUMB_H;
    h->bodySize = bodySize;
    h->bodyHash = stateHash(body, bodySize, 0);
}

static uint8_t expand5(unsigned v) {
    return (uint8_t)(v << 3 | v >> 2);
}

/* The display area of the saved VRAM as 8-bit RGB. */
static void displayRgb(const GpuDisplay* d, const uint16_t* vram, uint8_t* out) {
    for (int y = 0; y < d->h; y++) {
        const uint16_t* row = vram + (size_t)((d->y + y) & (VRAM_H - 1)) * VRAM_W;
        uint8_t* o = out + (size_t)y * d->w * 3;
        for (int x = 0; x < d->w; x++, o += 3) {
            if (!d->enabled) {
                o[0] = o[1] = o[2] = 0;
            } else if (d->rgb24) {
                for (int k = 0; k < 3; k++) {
                    int n = x * 3 + k;
                    uint16_t w = row[(d->x + (n >> 1)) & (VRAM_W - 1)];
                    o[k] = (uint8_t)(n & 1 ? w >> 8 : w);
                }
            } else {
                uint16_t c = row[(d->x + x) & (VRAM_W - 1)];
                o[0] = expand5(c & 31);
                o[1] = expand5((c >> 5) & 31);
                o[2] = expand5((c >> 10) & 31);
            }
        }
    }
}

/* The display as the TV shows it (4:3), box-filtered to the thumbnail, from
 * the VRAM the save just took. */
static uint8_t* thumbnail(void) {
    const GpuDisplay* d = gpuDisplay();
    uint8_t* thumb = calloc((size_t)STATE_THUMB_W * STATE_THUMB_H, 3);
    if (thumb == NULL || d == NULL || d->w <= 0 || d->h <= 0 || d->w > 1024 || d->h > 512) {
        return thumb;
    }
    uint8_t* rgb = malloc((size_t)d->w * d->h * 3);
    if (rgb == NULL) {
        return thumb;
    }
    displayRgb(d, vramSaved(), rgb);
    for (int ty = 0; ty < STATE_THUMB_H; ty++) {
        int y0 = ty * d->h / STATE_THUMB_H, y1 = (ty + 1) * d->h / STATE_THUMB_H;
        for (int tx = 0; tx < STATE_THUMB_W; tx++) {
            int x0 = tx * d->w / STATE_THUMB_W, x1 = (tx + 1) * d->w / STATE_THUMB_W;
            unsigned sum[3] = {0, 0, 0}, n = 0;
            for (int y = y0; y < (y1 > y0 ? y1 : y0 + 1); y++) {
                for (int x = x0; x < (x1 > x0 ? x1 : x0 + 1); x++) {
                    const uint8_t* p = rgb + ((size_t)y * d->w + x) * 3;
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    n++;
                }
            }
            uint8_t* o = thumb + ((size_t)ty * STATE_THUMB_W + tx) * 3;
            for (int k = 0; k < 3; k++) {
                o[k] = (uint8_t)(sum[k] / (n ? n : 1));
            }
        }
    }
    free(rgb);
    return thumb;
}

/* ---- writing files (a worker thread) ----------------------------------------------- */

typedef struct {
    char path[1100];
    char what[64]; /* "slot 3", "undo" */
    StateHeader header;
    uint8_t* thumb;
    uint8_t* body;
    int portable; /* taken at a safe point for a slot: with a trailer (stateportable.h), if it can be made */
    int missed;   /* a slot's, taken when no safe point came in time: for this build only */
} Job;

static SDL_AtomicInt serial; /* stateSerial */
static SDL_Thread* worker;
static Job* job;
static SDL_AtomicInt jobDone;
static char jobNote[400];

static int workerMain(void* arg) {
    Job* j = arg;
    char why[300], local[300] = "";
    StatePortable portable;
    int carried = j->portable && statePortableMake(j->body, (size_t)j->header.bodySize, &portable, local, sizeof local);
    if (j->portable && !carried) {
        fprintf(stderr, "state: %s for this version only: %s\n", j->what, local);
    }
    if (stateFileWritePortable(j->path, &j->header, j->thumb, j->body, carried ? &portable : NULL, why, sizeof why)) {
        if (j->missed) {
            snprintf(jobNote, sizeof jobNote, "State saved (%s), for this version only: it was taken during a movie, "
                     "a prompt or a card access", j->what);
        } else if (j->portable && !carried) {
            snprintf(jobNote, sizeof jobNote, "State saved (%s), for this version only: %s", j->what, local);
        } else {
            snprintf(jobNote, sizeof jobNote, "State saved (%s)", j->what);
        }
    } else {
        snprintf(jobNote, sizeof jobNote, "State not saved (%s): %s", j->what, why);
    }
    if (carried) {
        statePortableFree(&portable);
    }
    SDL_AddAtomicInt(&serial, 1);
    SDL_SetAtomicInt(&jobDone, 1);
    return 0;
}

/* The last write done: its note shown, its memory freed. */
static void finishJob(int wait) {
    if (job == NULL || (!wait && SDL_GetAtomicInt(&jobDone) == 0)) {
        return;
    }
    if (worker != NULL) {
        SDL_WaitThread(worker, NULL);
        worker = NULL;
    }
    if (job != NULL && strcmp(job->what, "undo") != 0) {
        note("%s", jobNote);
    } else {
        fprintf(stderr, "state: %s\n", jobNote);
    }
    if (job != NULL) {
        free(job->thumb);
        free(job->body);
        free(job);
        job = NULL;
    }
    SDL_SetAtomicInt(&jobDone, 0);
}

static void finishAtExit(void) {
    finishJob(1);
}

/* Writes in the background (sync: here, before going on). Takes the body. portable: a slot's at a
 * safe point (a trailer, made on the worker); missed: a slot's that waited for one in vain. */
static void writeState(const char* path, const char* what, uint8_t* body, size_t size, int sync, int portable,
                       int missed) {
    static int registered;
    finishJob(1);
    if (!registered) {
        registered = 1;
        atexit(finishAtExit);
    }
    Job* j = calloc(1, sizeof(Job));
    if (j == NULL) {
        free(body);
        return;
    }
    snprintf(j->path, sizeof j->path, "%s", path);
    snprintf(j->what, sizeof j->what, "%s", what);
    fillHeader(&j->header, size, body);
    j->thumb = thumbnail();
    j->body = body;
    j->portable = portable;
    j->missed = missed;
    fprintf(stderr, "state: saving %s at vblank %u (audio dump frame %llu)\n", path, j->header.vblank,
            (unsigned long long)j->header.dumpFrames);
    job = j;
    if (sync) {
        workerMain(j);
        finishJob(1);
        return;
    }
    worker = hostThreadCreate(workerMain, "state write", j);
    if (worker == NULL) {
        workerMain(j);
        finishJob(1);
    }
}

/* ---- capture and restore -------------------------------------------------------------- */

/* The game as it is now (parked): the body, malloc'd. */
static uint8_t* capture(size_t* size) {
    XgState s;
    memset(&s, 0, sizeof s);
    s.mode = MODE_SAVE;
    CPU_TIME_BEGIN(CPU_STATE);
    gp0StateFlush(); /* the batch ended: what the device drew can be read back */
    walk(&s);
    CPU_TIME_END(CPU_STATE);
    *size = s.size;
    return s.out;
}

static void loadedHooks(uint64_t dumpFrames) {
    precisionStateLoaded();
    mouseStateLoaded();
    mouseBattleStateLoaded();
    rumbleGameStateLoaded();
    spuOutputStateLoaded(dumpFrames);
    gpuStateLoaded();
    dumpStateLoaded();
}

/* Whether the image for `disc` can be put in (the one in the drive, or the
 * other configured one). */
static int discAvailable(int disc) {
    if (cdBootDiscNumber() == disc) {
        return 1;
    }
    const char* other = cdSwapPath(1);
    CdImageInfo info;
    char why[300];
    return other != NULL && other[0] != '\0' && cdVerifyImage(other, &info, why, sizeof why) && info.disc == disc &&
           strcmp(info.region, REGION) == 0;
}

static int putDiscIn(int disc) {
    if (cdBootDiscNumber() == disc) {
        return 1;
    }
    if (!cdSwapForReset() || cdBootDiscNumber() != disc) {
        return 0;
    }
    fprintf(stderr, "state: disc %d put in for the state\n", disc);
    return 1;
}

/* A body checked chunk by chunk against what this build reads; 0 with why. */
static int verified(const StateHeader* h, const uint8_t* body, char* why, size_t size) {
    XgState s;
    memset(&s, 0, sizeof s);
    s.mode = MODE_VERIFY;
    s.in = body;
    s.inSize = (size_t)h->bodySize;
    walk(&s);
    if (!s.failed && s.pos != s.inSize) {
        fail(&s, "the state has more than this build reads");
    }
    if (s.failed) {
        snprintf(why, size, "%s", s.why);
        return 0;
    }
    return 1;
}

enum { LOADS_NOT, LOADS_RAW, LOADS_CONVERTED };

/* Checks a state against this build and the discs: LOADS_RAW when it loads
 * as it is (with a body: verified chunk by chunk), LOADS_CONVERTED when it is
 * another build's that this one converts (stateportable.h: its body is
 * checked once converted), LOADS_NOT with why. path: the state's file (the
 * trailer of another build's). */
static int usable(const StateHeader* h, const uint8_t* body, const char* path, char* why, size_t size) {
    if (h->engine != (uint32_t)engineThis()) {
        snprintf(why, size, "saved by the %s engine: load it there (F1 > System > Engine)",
                 engineTitle((int)h->engine));
        return LOADS_NOT;
    }
    if (strcmp(h->region, REGION) != 0) {
        snprintf(why, size, "a %s state: this is the %s build", h->region, REGION);
        return LOADS_NOT;
    }
    if (!discAvailable((int)h->disc)) {
        snprintf(why, size, "it is on Disc %u: choose that disc's image in F1 > System", h->disc);
        return LOADS_NOT;
    }
    if (memcmp(h->buildId, stateBuildId(), 16) != 0) {
        return statePortableCanLoad(h, path, why, size) ? LOADS_CONVERTED : LOADS_NOT;
    }
    if (body != NULL) {
        char fit[300];
        if (!verified(h, body, fit, sizeof fit)) {
            snprintf(why, size, "it does not fit this build: %s", fit);
            return LOADS_NOT;
        }
    }
    return LOADS_RAW;
}

/* ---- the chunk lists of rewind.c (stateimage.h) -------------------------------------- */

static int listedCap;

int stateListChunks(const StateChunkRef** chunks, int* count, int late) {
    stateLateFinish(); /* the last list's, if still owed (its buffers are reused) */
    uint8_t* out = listingSpare.out;
    size_t cap = listingSpare.cap;
    listingSpare.out = listing.out;
    listingSpare.cap = listing.cap;
    ListEntry* list = listing.list;
    int listCap = listing.listCap;
    memset(&listing, 0, sizeof listing);
    listing.mode = MODE_LIST;
    listing.out = out;
    listing.cap = cap;
    listing.list = list;
    listing.listCap = listCap;
    listing.late = late;
    gp0StateFlush();
    walk(&listing);
    if (late) gvkLateSubmit(); /* the device's copies start now, not with the frame's drawing */
    if (listedCap < listing.listCount) {
        listedCap = listing.listCount;
        listed = realloc(listed, (size_t)listedCap * sizeof(*listed));
        if (listed == NULL) {
            fprintf(stderr, "state: out of memory\n");
            abort();
        }
    }
    for (int i = 0; i < listing.listCount; i++) {
        const ListEntry* e = &listing.list[i];
        listed[i].tag = (const char*)listing.out + e->tagOff;
        listed[i].tagLen = e->tagLen;
        listed[i].data = e->data != NULL ? e->data : e->direct ? NULL : listing.out + e->dataOff;
        listed[i].size = e->size;
        listed[i].late = e->late;
        /* the listing's copies and the device's mappings stay until the list after next;
         * a module's snapshot aside too (its next snapshot comes after that compare) */
        listed[i].stable = e->data == NULL || e->aside;
    }
    *chunks = listed;
    *count = listing.listCount;
    return 1;
}

int stateRestoreChunks(const StateChunkRef* chunks, int count, int disc, uint64_t dumpFrames, char* why,
                       size_t whySize) {
    XgState s;
    memset(&s, 0, sizeof s);
    s.mode = MODE_VERIFY;
    s.chunks = chunks;
    s.chunkCount = count;
    walk(&s);
    if (!s.failed && s.chunk != count) {
        fail(&s, "the capture has more than this build reads");
    }
    if (s.failed) {
        snprintf(why, whySize, "%s", s.why);
        return 0;
    }
    putDiscIn(disc);
    memset(&s, 0, sizeof s);
    s.mode = MODE_LOAD;
    s.chunks = chunks;
    s.chunkCount = count;
    walk(&s);
    if (s.failed) {
        fprintf(stderr, "state: restore failed half way (%s)\n", s.why);
        abort();
    }
    loadedHooks(dumpFrames);
    return 1;
}

static void restore(const StateHeader* h, const uint8_t* body) {
    XgState s;
    memset(&s, 0, sizeof s);
    s.mode = MODE_LOAD;
    s.in = body;
    s.inSize = (size_t)h->bodySize;
    putDiscIn((int)h->disc);
    walk(&s);
    if (s.failed) {
        /* Verified just before: cannot happen short of memory corruption. */
        fprintf(stderr, "state: load failed half way (%s)\n", s.why);
        abort();
    }
    loadedHooks(h->dumpFrames);
}

/* ---- requests ----------------------------------------------------------------------------- */

enum { REQ_NONE, REQ_SAVE, REQ_LOAD, REQ_REWIND, REQ_RESTART, REQ_RETURN };

static struct {
    int kind;
    int slot;            /* 1..10, STATE_UNDO, -1: path */
    char path[1100];
    int sync;            /* write before going on (the harness) */
    int rewind;          /* REQ_REWIND: REWIND_CAPTURE or REWIND_BACK */
    int safe;            /* REQ_SAVE: only at a safe point (stateSafePark: a canonical state's) */
    uint32_t until;      /* safe: from this vblank an ordinary save instead (a slot's; 0: wait as long as it takes) */
    int missed;          /* that came first: the save loads in this build only */
    int keep;            /* REQ_RESTART: the game kept in memory first (stateimage.h) */
    void (*entry)(void); /* REQ_RESTART: where the game starts again */
} req;

/* The game kept in memory while a Battle arena match runs (stateimage.h,
 * system/arena.c): a whole state, as a save writes it, never on disk. */
static struct {
    uint8_t* body;
    StateHeader header;
} kept;

/* How long a slot save waits for a safe point: 2 seconds. */
#define SAFE_WAIT 120

static int selected = 1;

/* system/netplay.c (weak: tests link this file without it). */
int netplayActive(void) __attribute__((weak));

int stateAvailable(const char** why) {
    if (netplayActive != NULL && netplayActive()) {
        /* Both players' games must stay the same: neither side jumps alone. */
        if (why) *why = "Save states are off during an online session";
        return 0;
    }
    if (kept.body != NULL) {
        /* The game the match came from waits in memory; a state of the match loaded later would have
         * no game to go back to, a state of before it would leave the match without it. */
        if (why) *why = "Save states are off during a Battle arena match: Leave at its end brings your game back";
        return 0;
    }
#if defined(__x86_64__) || defined(__aarch64__)
    if (!gameStackActive() && gameStackBase() == NULL) {
        if (why) *why = "Save states need the game on its own stack (not in this build)";
        return 0;
    }
    if (why) *why = NULL;
    return 1;
#else
    if (why) *why = "Save states are not available on this platform yet";
    return 0;
#endif
}

int stateBusy(void) {
    return req.kind != REQ_NONE;
}

/* ---- the Battle arena's restart and return (stateimage.h) ----------------------------------- */

int stateRestartAvailable(const char** why) {
    if (why) *why = NULL;
    if (getenv("XG_SLOWDOWN") != NULL || portOverlayCd()) {
        if (why) *why = "Not during the slowdown replay or with XG_OVERLAY_CD";
        return 0;
    }
#if defined(__x86_64__) || defined(__aarch64__)
    if (!gameStackActive() && gameStackBase() == NULL) {
        if (why) *why = "Needs the game on its own stack (not in this build)";
        return 0;
    }
    return 1;
#else
    if (why) *why = "Not available on this platform yet";
    return 0;
#endif
}

void stateRequestRestart(int keep, void (*entry)(void)) {
    req.kind = REQ_RESTART;
    req.keep = keep;
    req.entry = entry;
}

void stateRequestReturn(void) {
    if (kept.body != NULL) {
        req.kind = REQ_RETURN;
    }
}

int stateKept(void) {
    return kept.body != NULL;
}

void stateKeptDrop(void) {
    free(kept.body);
    kept.body = NULL;
}

static void serveRestart(void) {
    if (req.keep) {
        size_t size;
        free(kept.body);
        kept.body = capture(&size);
        fillHeader(&kept.header, size, kept.body);
        fprintf(stderr, "state: the game kept in memory (%zu bytes) at vblank %u\n", size, kept.header.vblank);
    }
    rewindReset(); /* the history is the timeline left behind */
    gameStackRestart(req.entry); /* the game's frames are dropped: it starts at entry when the host switches back */
}

static void serveReturn(void) {
    uint8_t* body = kept.body;
    kept.body = NULL; /* (stateAvailable while the hooks run) */
    stackless = 0;
    restore(&kept.header, body);
    free(body);
    if (stackless) {
        gameStackRestart(pcResumeGame); /* (cannot happen: the kept game has its frames) */
        stackless = 0;
    }
    rewindReset();
    fprintf(stderr, "state: the kept game is back (vblank %u)\n", kept.header.vblank);
}

static void request(int kind, int slot, const char* path) {
    const char* why;
    if (!stateAvailable(&why)) {
        note("%s", why);
        return;
    }
    req.kind = kind;
    req.slot = slot;
    req.sync = 0;
    req.safe = 0;
    req.until = 0;
    req.missed = 0;
    if (path != NULL) {
        snprintf(req.path, sizeof req.path, "%s", path);
    } else if (!stateSlotPath(slot, req.path, sizeof req.path)) {
        note("No folder for save states (XG_SETTINGS=none): set XG_STATE_DIR");
        req.kind = REQ_NONE;
    }
}

void stateRequestSave(int slot) {
    if (slot < 1 || slot > STATE_SLOTS) {
        return;
    }
    request(REQ_SAVE, slot, NULL);
    if (req.kind == REQ_SAVE && statePortableOn()) {
        /* At the next safe point, which normally comes at once, so that other builds load the slot too
         * (stateportable.h); if none comes in time (a movie, a prompt, the card's directory scan), as
         * before, for this build only (stateVBlankPoint). */
        req.safe = 1;
        req.until = hwVSyncCount() + SAFE_WAIT;
        if (req.until == 0) {
            req.until = 1;
        }
    }
}

int stateSlotsPortable(void) {
    return statePortableOn();
}

void stateRequestLoad(int slot) {
    if (slot < 0 || slot > STATE_SLOTS) {
        return;
    }
    request(REQ_LOAD, slot, NULL);
}

void stateRequestLoadFile(const char* path) {
    request(REQ_LOAD, -1, path);
}

int stateSelectedSlot(void) {
    return selected;
}

static void describe(int slot, char* out, size_t size) {
    char path[1100];
    StateHeader h;
    if (!stateSlotPath(slot, path, sizeof path) || !stateFileReadHeader(path, &h, NULL)) {
        snprintf(out, size, "Slot %d: empty", slot);
        return;
    }
    time_t t = (time_t)h.savedAt;
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char when[32];
    strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tmv);
    int other = memcmp(h.buildId, stateBuildId(), 16) != 0;
    int engine = h.engine != (uint32_t)engineThis();
    char why[300];
    const char* mark = !other ? ((h.flags & STATE_FLAG_PORTABLE) ? "" : " (this version only)")
                       : usable(&h, NULL, path, why, sizeof why) == LOADS_CONVERTED ? " (another build's: it converts)"
                                                                                     : " (another build: cannot load)";
    snprintf(out, size, "Slot %d: %s, Disc %u, %s%s%s%s", slot, when, h.disc, h.label, engine ? " (" : mark,
             engine ? engineTitle((int)h.engine) : "", engine ? " engine: cannot load)" : "");
}

void stateSelectSlot(int slot) {
    while (slot < 1) {
        slot += STATE_SLOTS;
    }
    selected = (slot - 1) % STATE_SLOTS + 1;
}

void stateNoteSlot(void) {
    char text[300];
    describe(selected, text, sizeof text);
    presentShowNote(text);
}

int stateSerial(void) {
    return SDL_GetAtomicInt(&serial);
}

int stateSlotInfo(int slot, XgStateInfo* out) {
    char path[1100];
    StateHeader h;
    memset(out, 0, sizeof(*out));
    if (!stateSlotPath(slot, path, sizeof path) || !stateFileReadHeader(path, &h, &out->thumb)) {
        return 0;
    }
    out->exists = 1;
    out->usable = usable(&h, NULL, path, out->why, sizeof out->why) != LOADS_NOT;
    out->otherBuild = memcmp(h.buildId, stateBuildId(), 16) != 0;
    out->portable = (h.flags & STATE_FLAG_PORTABLE) != 0;
    snprintf(out->appVersion, sizeof out->appVersion, "%s", h.appVersion);
    out->savedAt = h.savedAt;
    out->disc = (int)h.disc;
    out->vblank = h.vblank;
    out->playTime = h.playTime;
    snprintf(out->label, sizeof out->label, "%s", h.label);
    out->thumbW = out->thumb != NULL ? (int)h.thumbW : 0;
    out->thumbH = out->thumb != NULL ? (int)h.thumbH : 0;
    return 1;
}

void stateInfoFree(XgStateInfo* info) {
    free(info->thumb);
    info->thumb = NULL;
}

int stateDeleteSlot(int slot) {
    char path[1100];
    int ok = stateSlotPath(slot, path, sizeof path) && SDL_RemovePath(path);
    SDL_AddAtomicInt(&serial, 1);
    return ok;
}

/* ---- the harness ---------------------------------------------------------------------- */

static struct {
    int parsed;
    struct {
        uint32_t vblank;
        char path[1100];
        int done;
        int safe; /* XG_STATE_SAVE_SAFE: at the first safe point from that vblank */
    } saves[16];
    int saveCount;
    char load[1100];
    int loadDone;
    uint32_t every;
    char everyPath[1100];
    struct {
        uint32_t vblank;
        int slot;
        int done;
    } slotSaves[16]; /* XG_STATE_SLOT_SAVE: as F7 does */
    int slotSaveCount;
    uint32_t slotLoadAt; /* XG_STATE_SLOT_LOAD: as F8 does */
    int slotLoad;        /* 0: none */
    int slotLoadDone;
} harness;

static void harnessParseSaves(const char* e, int safe) {
    while (e != NULL && *e != '\0' && harness.saveCount < 16) {
        unsigned long v;
        int n = 0;
        if (sscanf(e, "%lu:%n", &v, &n) < 1 || n == 0) {
            break;
        }
        const char* path = e + n;
        const char* end = strchr(path, ',');
        size_t len = end != NULL ? (size_t)(end - path) : strlen(path);
        harness.saves[harness.saveCount].vblank = (uint32_t)v;
        harness.saves[harness.saveCount].safe = safe;
        snprintf(harness.saves[harness.saveCount].path, sizeof harness.saves[0].path, "%.*s", (int)len, path);
        harness.saveCount++;
        e = end != NULL ? end + 1 : NULL;
    }
}

static void harnessParse(void) {
    harness.parsed = 1;
    harnessParseSaves(getenv("XG_STATE_SAVE"), 0);
    harnessParseSaves(getenv("XG_STATE_SAVE_SAFE"), 1);
    const char* e = getenv("XG_STATE_LOAD");
    if (e != NULL && e[0] != '\0') {
        snprintf(harness.load, sizeof harness.load, "%s", e);
    }
    for (e = getenv("XG_STATE_SLOT_SAVE"); e != NULL && *e != '\0' && harness.slotSaveCount < 16;) {
        unsigned long v, slot;
        int n = 0;
        if (sscanf(e, "%lu:%lu%n", &v, &slot, &n) < 2 || n == 0) {
            break;
        }
        harness.slotSaves[harness.slotSaveCount].vblank = (uint32_t)v;
        harness.slotSaves[harness.slotSaveCount].slot = (int)slot;
        harness.slotSaveCount++;
        e = e[n] == ',' ? e + n + 1 : NULL;
    }
    e = getenv("XG_STATE_SLOT_LOAD");
    if (e != NULL && e[0] != '\0') {
        unsigned long v, slot;
        if (sscanf(e, "%lu:%lu", &v, &slot) == 2) {
            harness.slotLoadAt = (uint32_t)v;
            harness.slotLoad = (int)slot;
        } else if (sscanf(e, "%lu", &slot) == 1) {
            harness.slotLoad = (int)slot;
        }
    }
    e = getenv("XG_STATE_EVERY");
    if (e != NULL && e[0] != '\0') {
        unsigned long v;
        int n = 0;
        if (sscanf(e, "%lu:%n", &v, &n) >= 1 && n > 0 && v > 0) {
            harness.every = (uint32_t)v;
            snprintf(harness.everyPath, sizeof harness.everyPath, "%s", e + n);
        }
    }
}

static void harnessCheck(uint32_t vblank) {
    if (!harness.parsed) {
        harnessParse();
    }
    if (netplayActive != NULL && netplayActive()) {
        return; /* an online session: no state taken or loaded (stateAvailable) */
    }
    if (req.kind != REQ_NONE) {
        return;
    }
    if (harness.load[0] != '\0' && !harness.loadDone) {
        harness.loadDone = 1;
        request(REQ_LOAD, -1, harness.load);
        req.sync = 1;
        return;
    }
    if (harness.slotLoad > 0 && !harness.slotLoadDone && vblank >= harness.slotLoadAt) {
        harness.slotLoadDone = 1;
        stateRequestLoad(harness.slotLoad);
        return;
    }
    for (int i = 0; i < harness.slotSaveCount; i++) {
        if (!harness.slotSaves[i].done && vblank >= harness.slotSaves[i].vblank) {
            harness.slotSaves[i].done = 1;
            stateRequestSave(harness.slotSaves[i].slot);
            return;
        }
    }
    for (int i = 0; i < harness.saveCount; i++) {
        if (!harness.saves[i].done && vblank >= harness.saves[i].vblank) {
            harness.saves[i].done = 1;
            request(REQ_SAVE, -1, harness.saves[i].path);
            req.sync = 1;
            req.safe = harness.saves[i].safe;
            return;
        }
    }
    if (harness.every != 0 && vblank % harness.every == 0) {
        request(REQ_SAVE, -1, harness.everyPath);
        req.sync = 1;
    }
}

/* ---- the park and the serve ----------------------------------------------------------- */

int vramBookkeepingHeld(void) __attribute__((weak)); /* port/src/gpu/vram.c */

static int parkAllowed(void);

void safeAuditVBlank(void);       /* safeaudit.c: XG_SAFE_AUDIT */
void safeAuditSafePoint(void);

void stateVBlankPoint(void) {
    safeAuditVBlank();
    finishJob(0);
    harnessCheck(hwVSyncCount());
    if (req.kind == REQ_SAVE && req.safe) {
        if (req.until == 0 || (int32_t)(hwVSyncCount() - req.until) < 0) {
            return; /* a canonical state's: at the next safe point (stateSafePark), not inside a frame */
        }
        /* None came in time (a slot's): an ordinary save, which only this build loads. */
        fprintf(stderr, "state: no safe point in %d vblanks: slot %d saved as it is\n", SAFE_WAIT, req.slot);
        req.safe = 0;
        req.missed = 1;
    }
    if (req.kind == REQ_NONE) {
        /* Rewind (rewind.c): a capture due, or the rewind input held. */
        int r = rewindWanted(hwVSyncCount());
        if (r == 0) {
            return;
        }
        req.kind = REQ_REWIND;
        req.rewind = r;
    }
    if (parkAllowed()) {
        gameStackPark();
    }
}

/* Whether the game may park for the request now (the request may be dropped). */
static int parkAllowed(void) {
    if (hwWorldCurrent() != NULL || !gameStackActive()) {
        return 0; /* not the game's own world: later */
    }
    if (getenv("XG_SLOWDOWN") != NULL || portOverlayCd() || (netplayActive != NULL && netplayActive())) {
        if (req.kind != REQ_REWIND) {
            note("Save states are off during the slowdown replay, XG_OVERLAY_CD and online sessions");
        }
        req.kind = REQ_NONE;
        return 0;
    }
    /* Battling's picture between drawn and not yet put back (its VRAM and bookkeeping go back
     * after the wait, from host memory no state carries: between.c pcBattling60WaitDone): a state
     * taken now would hold the picture's tile generations and wide arena, and one put back now
     * would be undone by that put-back (xg-9mlq). Later, as for a card access. */
    if (cardBusy(0) || cardBusy(1) || portSavedModuleOnHeap() ||
        (vramBookkeepingHeld != NULL && vramBookkeepingHeld())) {
        if (req.kind == REQ_REWIND) {
            req.kind = REQ_NONE; /* no capture now (the next park tries again) */
        }
        return 0; /* a memory card access or file in progress, or a picture between: at its end */
    }
    return 1;
}

void stateSafePark(void) {
    safeAuditSafePoint();
    if (req.kind == REQ_SAVE && req.safe && parkAllowed()) {
        gameStackPark();
    }
}

static void serveSave(void) {
    size_t size;
    uint64_t t0 = SDL_GetTicksNS();
    uint8_t* body = capture(&size);
    char what[64];
    if (req.slot > 0) {
        snprintf(what, sizeof what, "slot %d", req.slot);
    } else {
        const char* name = strrchr(req.path, '/');
        snprintf(what, sizeof what, "%.60s", name != NULL ? name + 1 : req.path);
    }
    if (logOn()) {
        fprintf(stderr, "state: captured %zu bytes in %.2f ms\n", size, (double)(SDL_GetTicksNS() - t0) * 1e-6);
    }
    writeState(req.path, what, body, size, req.sync, req.safe && req.slot > 0, req.missed);
}

static void serveLoad(void) {
    StateHeader h;
    uint8_t* body = NULL;
    char why[400];
    uint64_t t0 = SDL_GetTicksNS();
    const char* what = req.slot == STATE_UNDO ? "the game before the last load" : req.path;

    char name[64];
    if (req.slot > 0) {
        snprintf(name, sizeof name, "slot %d", req.slot);
        what = name;
    }
    if (!stateFileRead(req.path, &h, &body, why, sizeof why)) {
        if (req.slot > 0 && strcmp(why, "there is no such file") == 0) {
            note("Slot %d is empty", req.slot);
        } else if (req.slot == STATE_UNDO && strcmp(why, "there is no such file") == 0) {
            note("No load to undo");
        } else {
            note("Cannot load %s: %s", what, why);
        }
        return;
    }
    int how = usable(&h, body, req.path, why, sizeof why);
    if (how == LOADS_NOT) {
        note("Cannot load %s: %s", what, why);
        free(body);
        return;
    }
    /* The game as it is now becomes "before the last load" (Shift+F8); for another build's state it
     * gives the converted one its chunks too (stateportable.h). */
    char undo[1100];
    int keepUndo = stateSlotPath(STATE_UNDO, undo, sizeof undo);
    uint8_t* now = NULL;
    size_t nowSize = 0;
    StateConvertResult conv;
    memset(&conv, 0, sizeof conv);
    char writer[64];
    statePortableWriter(&h, writer, sizeof writer);
    if (how == LOADS_CONVERTED) {
        finishJob(1); /* (a save's worker may be reading this build's schema) */
        now = capture(&nowSize);
        if (!statePortableConvert(&h, body, req.path, now, nowSize, &conv, why, sizeof why)) {
            note("Cannot load %s: %s", what, why);
            free(now);
            free(body);
            return;
        }
        free(body);
        body = conv.body; /* the converted state, with the writer's header: its label, disc, vblank, play time */
        conv.body = NULL;
        memcpy(h.buildId, stateBuildId(), 16);
        h.bodySize = conv.size;
        if (!verified(&h, body, why, sizeof why)) {
            note("Cannot load %s: saved by %s; converted, it does not fit this build: %s", what, writer, why);
            stateConvertResultFree(&conv);
            free(now);
            free(body);
            return;
        }
    } else if (keepUndo) {
        now = capture(&nowSize);
    }
    if (keepUndo && now != NULL) {
        writeState(undo, "undo", now, nowSize, req.sync, 0, 0);
    } else {
        free(now);
    }
    stackless = 0;
    restore(&h, body);
    free(body);
    if (stackless) {
        gameStackRestart(pcResumeGame); /* a state without the game's frames: from its safe point */
        stackless = 0;
    }
    rewindReset(); /* the history belongs to the timeline left behind */
    int64_t dropped = 0; /* values the converter left zero or could not place in this build */
    for (int32_t i = 0; i < conv.decodeWarningCount; i++) {
        dropped += conv.decodeWarnings[i].count;
    }
    stateConvertResultFree(&conv);
    if (req.slot > 0 && how == LOADS_CONVERTED && dropped > 0) {
        note("Loaded slot %d (%s), converted from %s: %lld values not carried (the log lists them)", req.slot,
             h.label, writer, (long long)dropped);
    } else if (req.slot > 0 && how == LOADS_CONVERTED) {
        note("Loaded slot %d (%s), converted from %s", req.slot, h.label, writer);
    } else if (req.slot > 0) {
        note("Loaded slot %d (%s)", req.slot, h.label);
    } else if (req.slot == STATE_UNDO) {
        note("Back to the game before the last load (Shift+F8 again: forward)");
    } else {
        fprintf(stderr, "state: loaded %s%s: vblank %u (audio dump frame %llu)\n", req.path,
                how == LOADS_CONVERTED ? " (converted)" : "", h.vblank, (unsigned long long)h.dumpFrames);
    }
    if (logOn()) {
        fprintf(stderr, "state: load took %.2f ms\n", (double)(SDL_GetTicksNS() - t0) * 1e-6);
    }
}

void stateServe(void) {
    if (req.kind == REQ_NONE) {
        return;
    }
    if (req.kind == REQ_REWIND && req.rewind == REWIND_CAPTURE) {
        /* About a millisecond, in the frame's idle time: the clock goes on
         * (paused, every capture would put the game that much behind). */
        rewindServe(REWIND_CAPTURE);
        req.kind = REQ_NONE;
        return;
    }
    hwPause(1);
    hwPausedFrame();
    if (req.kind == REQ_REWIND) {
        rewindServe(req.rewind);
    } else if (req.kind == REQ_RESTART) {
        rewindSettle();
        serveRestart();
    } else if (req.kind == REQ_RETURN) {
        rewindSettle();
        serveReturn();
    } else if (req.kind == REQ_SAVE) {
        rewindSettle(); /* a capture's late pixels first: they land where a save's snapshot goes */
        serveSave();
        const char* again = getenv("XG_STATE_RELOAD");
        if (req.sync && again != NULL && atoi(again) != 0) {
            serveLoad(); /* the harness: put back what was just saved (a load must change nothing) */
        }
    } else {
        rewindSettle();
        serveLoad();
    }
    req.kind = REQ_NONE;
    hwPausedFrame();
    hwPause(0);
}
