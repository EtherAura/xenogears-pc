/*
 * Rewind (rewind.h): hold R (F10 when R plays a PS1 button) or Select+L2 on a
 * gamepad and the game steps back through its last seconds; let go and it
 * plays on from there, exactly as from a loaded state.
 *
 * While rewind is on, the game is captured at its park point (savestate.h:
 * its VSync wait, after a vblank) every `interval` vblanks. One full
 * image of the latest capture is kept, chunk by chunk as savestate.c lists
 * them, and behind it a ring of reverse deltas: each takes the image back one
 * capture. A delta holds, for the 64-byte blocks that changed since the
 * capture before, the old bytes XORed with the new (mostly zeros), packed
 * with the states' codec; a chunk whose size changed is kept whole, and a
 * capture whose list of chunks differs (a mode that adds or drops chunks)
 * keeps the whole previous image. The oldest deltas go when the history is
 * longer than its length or larger than its budget. The pixels only the
 * device has (VRAM drawn since its last readback, the wide target) are not
 * waited for: their copies are queued behind the frame, and the capture's
 * delta is finished at the next capture (rewindSettle), by when they are done.
 * The game's live memory is compared at the park in lanes (the game thread
 * and helper threads, at the same time); a worker thread compares the rest
 * (copies, the device's pixels) and packs each delta while the game goes
 * on; a capture that finds it still busy with the one before (a large delta
 * after a scene change) waits for the next park instead of stalling the
 * game.
 *
 * Going back: the image restored as a load restores a file (stateimage.h),
 * shown, then the next delta applied, one capture per displayed frame, the
 * clock and the sound paused. Off (the default) nothing is captured and the
 * park point is one test of a flag: the game is unchanged.
 *
 * Settings (F1 > Save states > Rewind; settings.ini):
 *   rewind           0 (default) / 1
 *   rewind_seconds   how far back it goes (default 30)
 *   rewind_interval  vblanks between captures (default 2)
 *   rewind_budget    MB for the image and the deltas (default 256)
 * Harness: XG_REWIND=1 turns it on for a run (as XG_<KEY> does any setting);
 * XG_REWIND_AT=<vblank>:<captures>[,...] goes back that many captures at the
 * first park at or after that vblank, without input or pacing;
 * XG_REWIND_HOLD=<vblank>:<frames> holds the rewind input from that vblank
 * for that many displayed frames (the interactive path, paced);
 * XG_REWIND_LOG=1 says where the deltas' bytes come from (every 300
 * captures); XG_REWIND_SHOTS=1 writes each picture shown while going back
 * (rewind_<n>.png, XG_DUMP_PRESENT's folder).
 */

#include "rewind.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../gpu/gpu.h"
#include "cputime.h"
#include "pad.h"
#include "settings.h"
#include "statecodec.h"
#include "savestate.h"
#include "stateimage.h"
#include "system.h"

int cdBootDiscNumber(void);                      /* cd/cd.h */
uint64_t spuOutputDumpFrames(void);              /* spu/output.c */
void spuOutputPause(int on);                     /* spu/output.h */
int padRewindComboHeld(void) __attribute__((weak)); /* pad.c */

#define BLOCK 64
#define PAGE 4096
#define REWIND_LANES 4     /* the compare's threads, the game thread included (rewind_lanes) */
#define REWIND_LANES_MAX 8

/* ---- settings ---------------------------------------------------------------------------- */

static struct {
    int loaded;
    int on, seconds, interval, budget, lanes;
} cfg;

static void loadSettings(void) {
    if (cfg.loaded) {
        return;
    }
    cfg.loaded = 1;
    settingsDescribe("rewind", "rewind: 1 to keep the last seconds for rewinding (hold R, or Select+L2 on a "
                               "gamepad); 0 (default) off");
    settingsDescribe("rewind_seconds", "rewind_seconds: how far back rewind goes (5..600, default 30)");
    settingsDescribe("rewind_interval", "rewind_interval: vblanks between rewind captures (1..10, default 2)");
    settingsDescribe("rewind_budget", "rewind_budget: MB of memory rewind may use (32..4096, default 256)");
    settingsDescribe("rewind_lanes", "rewind_lanes: threads comparing the game's memory at a capture, the game thread "
                                     "included (1..8, default 4; 1: the game thread alone)");
    cfg.on = settingsBool("rewind", 0);
    cfg.seconds = settingsInt("rewind_seconds", 30);
    cfg.interval = settingsInt("rewind_interval", 2);
    cfg.budget = settingsInt("rewind_budget", 256);
    cfg.lanes = settingsInt("rewind_lanes", REWIND_LANES);
    cfg.seconds = cfg.seconds < 5 ? 5 : cfg.seconds > 600 ? 600 : cfg.seconds;
    cfg.interval = cfg.interval < 1 ? 1 : cfg.interval > 10 ? 10 : cfg.interval;
    cfg.budget = cfg.budget < 32 ? 32 : cfg.budget > 4096 ? 4096 : cfg.budget;
    cfg.lanes = cfg.lanes < 1 ? 1 : cfg.lanes > REWIND_LANES_MAX ? REWIND_LANES_MAX : cfg.lanes;
}

/* ---- the image of the latest capture -------------------------------------------------- */

typedef struct {
    char* tag;
    int tagLen;
    uint8_t* data;
    size_t size, cap;
} Chunk;

typedef struct {
    uint32_t vblank;
    uint64_t dumpFrames;
    int disc;
} Meta;

static struct {
    Chunk* c;
    int count, cap;
    int valid;
    Meta meta;
    size_t bytes;
    StateChunkRef* refs; /* for restoring */
} img;

/* One step back: the delta from a capture to the one before it, and that one's meta. */
typedef struct {
    uint8_t* packed;
    size_t packedSize, rawSize;
    Meta meta;
} Step;

static struct {
    Step* s;
    int cap, head, count; /* the newest at (head + count - 1) % cap */
    size_t bytes;
} ring;

typedef struct {
    uint8_t* raw; /* a delta being built, or unpacked */
    size_t size, cap;
} Delta;

static Delta buf;

static struct {
    uint32_t lastCapture;
    int haveCapture;
    uint64_t captureNs, captures; /* the cost so far (stats) */
    uint64_t listNs, compareNs;   /* of it: the listing, the lanes' compare (the game thread waits) */
    double lastMs, worstMs;
    int active; /* going back now */
} run;

/* A capture whose device pixels (VRAM drawn since its last readback, the
 * wide target) are still on their way: they are copied behind the frame
 * (stateListChunks' late) rather than waited for, and the capture is
 * completed at the next one, or before anything uses or replaces it: the
 * pack worker compares them then, from where they landed, while the next
 * capture lists into other buffers. Its other chunks are already compared
 * (the game goes on and changes them). */
static struct {
    int active;
    int first;  /* the first capture: no step back */
    int whole;  /* the chunks changed: the step back keeps the whole image */
    Meta meta;  /* the step's: the capture before */
    const StateChunkRef* r;
    int n;
    uint64_t ns; /* its cost so far */
} pend;

static void* xrealloc(void* p, size_t n) {
    void* q = realloc(p, n ? n : 1);
    if (q == NULL) {
        fprintf(stderr, "rewind: out of memory (%zu bytes)\n", n);
        abort();
    }
    return q;
}

static int zeroPage(const uint8_t* p, size_t n) {
    uint64_t any = 0;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        memcpy(&w, p + i, 8);
        any |= w;
    }
    for (; i < n; i++) {
        any |= p[i];
    }
    return any == 0;
}

/* Bytes into a chunk's buffer, page by page: a page that is zero in both is
 * not written, so the buffer's never-written pages stay the system's shared
 * zero page (no memory, and comparing them with the game's untouched pages
 * reads one page from the cache). */
static void copySparse(uint8_t* dst, const uint8_t* src, size_t n, int dstFresh) {
    for (size_t at = 0; at < n; at += PAGE) {
        size_t len = n - at < PAGE ? n - at : PAGE;
        if (zeroPage(src + at, len)) {
            if (!dstFresh && !zeroPage(dst + at, len)) {
                memset(dst + at, 0, len);
            }
        } else {
            memcpy(dst + at, src + at, len);
        }
    }
}

/* A chunk's buffer of at least n bytes; a large one from calloc (fresh pages
 * read as zero without being memory yet). Returns whether it is fresh. */
static int chunkReserve(Chunk* c, size_t n) {
    if (c->cap >= n) {
        return 0;
    }
    uint8_t* fresh = calloc(1, n ? n : 1);
    if (fresh == NULL) {
        fprintf(stderr, "rewind: out of memory (%zu bytes)\n", n);
        abort();
    }
    if (c->size > 0) {
        copySparse(fresh, c->data, c->size, 1);
    }
    free(c->data);
    img.bytes += n - c->cap;
    c->data = fresh;
    c->cap = n;
    return c->size == 0;
}

static void chunkSet(Chunk* c, const StateChunkRef* r, int ready) {
    if (c->tag == NULL || c->tagLen != r->tagLen || memcmp(c->tag, r->tag, (size_t)r->tagLen) != 0) {
        free(c->tag);
        c->tag = xrealloc(NULL, (size_t)r->tagLen);
        memcpy(c->tag, r->tag, (size_t)r->tagLen);
        c->tagLen = r->tagLen;
    }
    int fresh = c->size == 0 && c->cap < r->size;
    chunkReserve(c, r->size);
    if (ready && r->size > 0) {
        copySparse(c->data, r->data, r->size, fresh);
    }
    c->size = r->size;
}

static void imageFree(void) {
    for (int i = 0; i < img.cap; i++) {
        free(img.c[i].tag);
        free(img.c[i].data);
    }
    free(img.c);
    free(img.refs);
    memset(&img, 0, sizeof img);
}

static void imageSetCount(int n) {
    if (n > img.cap) {
        img.c = xrealloc(img.c, (size_t)n * sizeof(Chunk));
        img.refs = xrealloc(img.refs, (size_t)n * sizeof(StateChunkRef));
        memset(img.c + img.cap, 0, (size_t)(n - img.cap) * sizeof(Chunk));
        img.cap = n;
    }
    img.count = n;
}

static void imageFrom(const StateChunkRef* r, int n) {
    imageSetCount(n);
    for (int i = 0; i < n; i++) {
        /* Deferred pixels may point directly at an in-flight device read.
         * Reserve their image now; copy only after stateLateFinish waits. */
        chunkSet(&img.c[i], &r[i], !r[i].late);
    }
    img.valid = 1;
}

/* ---- deltas ----------------------------------------------------------------------------- */

enum { REC_XOR = 1, REC_RESIZE = 2, REC_IMAGE = 3 };

static void putTo(Delta* d, const void* p, size_t n) {
    if (d->size + n > d->cap) {
        d->cap = (d->size + n) * 2 + (1u << 20);
        d->raw = xrealloc(d->raw, d->cap);
    }
    memcpy(d->raw + d->size, p, n);
    d->size += n;
}

static void put32To(Delta* d, uint32_t v) {
    putTo(d, &v, 4);
}

static void put(const void* p, size_t n) {
    putTo(&buf, p, n);
}

static void put32(uint32_t v) {
    put32To(&buf, v);
}

/* A run of changed bytes: the old ^ new into the delta, the new into the image. */
static void xorRun(Delta* d, int chunk, uint8_t* old, const uint8_t* now, size_t off, size_t len) {
    uint8_t rec = REC_XOR;
    putTo(d, &rec, 1);
    put32To(d, (uint32_t)chunk);
    put32To(d, (uint32_t)off);
    put32To(d, (uint32_t)len);
    if (d->size + len > d->cap) {
        d->cap = (d->size + len) * 2 + (1u << 20);
        d->raw = xrealloc(d->raw, d->cap);
    }
    uint8_t* x = d->raw + d->size;
    for (size_t i = 0; i < len; i++) {
        x[i] = old[off + i] ^ now[off + i];
    }
    d->size += len;
    memcpy(old + off, now + off, len);
}

/* The image's chunk against the game's, bytes [from, n) (from page aligned):
 * pages compared whole, a changed page block by block, neighbouring changed
 * blocks in one run. */
static void diffRange(Delta* d, int chunk, uint8_t* old, const uint8_t* now, size_t from, size_t n) {
    size_t runStart = 0, runEnd = 0;
    int inRun = 0;
    for (size_t page = from; page < n; page += PAGE) {
        size_t len = n - page < PAGE ? n - page : PAGE;
        if (memcmp(old + page, now + page, len) == 0) {
            if (inRun) {
                xorRun(d, chunk, old, now, runStart, runEnd - runStart);
                inRun = 0;
            }
            continue;
        }
        for (size_t at = page; at < page + len; at += BLOCK) {
            size_t bl = page + len - at < BLOCK ? page + len - at : BLOCK;
            if (memcmp(old + at, now + at, bl) != 0) {
                if (!inRun) {
                    runStart = at;
                    inRun = 1;
                }
                runEnd = at + bl;
            } else if (inRun) {
                xorRun(d, chunk, old, now, runStart, runEnd - runStart);
                inRun = 0;
            }
        }
    }
    if (inRun) {
        xorRun(d, chunk, old, now, runStart, runEnd - runStart);
    }
}

static void diffChunk(Delta* d, int chunk, uint8_t* old, const uint8_t* now, size_t n) {
    diffRange(d, chunk, old, now, 0, n);
}

static void wholeImage(void) {
    uint8_t rec = REC_IMAGE;
    put(&rec, 1);
    put32((uint32_t)img.count);
    for (int i = 0; i < img.count; i++) {
        put32((uint32_t)img.c[i].tagLen);
        put(img.c[i].tag, (size_t)img.c[i].tagLen);
        put32((uint32_t)img.c[i].size);
        put(img.c[i].data, img.c[i].size);
    }
}

static uint32_t get32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/* The image one capture back: a delta's records applied. */
static int applyBack(const uint8_t* d, size_t n) {
    size_t at = 0;
    while (at < n) {
        uint8_t rec = d[at++];
        if (rec == REC_XOR && at + 12 <= n) {
            uint32_t chunk = get32(d + at), off = get32(d + at + 4), len = get32(d + at + 8);
            at += 12;
            if ((int)chunk >= img.count || (size_t)off + len > img.c[chunk].size || at + len > n) {
                return 0;
            }
            uint8_t* p = img.c[chunk].data + off;
            for (uint32_t i = 0; i < len; i++) {
                p[i] ^= d[at + i];
            }
            at += len;
        } else if (rec == REC_RESIZE && at + 12 <= n) {
            /* The chunk's old size; when it was larger, its old tail. */
            uint32_t chunk = get32(d + at), size = get32(d + at + 4), tail = get32(d + at + 8);
            at += 12;
            if ((int)chunk >= img.count || at + tail > n || tail > size) {
                return 0;
            }
            Chunk* c = &img.c[chunk];
            chunkReserve(c, size);
            if (tail > 0) {
                memcpy(c->data + size - tail, d + at, tail);
            }
            c->size = size;
            at += tail;
        } else if (rec == REC_IMAGE && at + 4 <= n) {
            uint32_t count = get32(d + at);
            at += 4;
            imageSetCount((int)count);
            for (uint32_t i = 0; i < count; i++) {
                if (at + 4 > n) {
                    return 0;
                }
                uint32_t tl = get32(d + at);
                if (at + 4 + tl + 4 > n) {
                    return 0;
                }
                uint32_t size = get32(d + at + 4 + tl);
                if (at + 8 + tl + size > n) {
                    return 0;
                }
                StateChunkRef r = {(const char*)d + at + 4, (int)tl, d + at + 8 + tl, size, 0, 1};
                chunkSet(&img.c[i], &r, 1);
                at += 8 + tl + size;
            }
        } else {
            return 0;
        }
    }
    return 1;
}

/* ---- the ring -------------------------------------------------------------------------- */

static int maxSteps(void) {
    return cfg.seconds * 60 / cfg.interval + 1;
}

static void dropOldest(void) {
    Step* s = &ring.s[ring.head];
    ring.bytes -= s->packedSize;
    free(s->packed);
    s->packed = NULL;
    ring.head = (ring.head + 1) % ring.cap;
    ring.count--;
}

static void push(Step step, size_t imageBytes) {
    int want = maxSteps() + 1;
    if (ring.cap < want) {
        /* Grown in place: unrolled so the oldest is first. */
        Step* s = xrealloc(NULL, (size_t)want * sizeof(Step));
        for (int i = 0; i < ring.count; i++) {
            s[i] = ring.s[(ring.head + i) % ring.cap];
        }
        free(ring.s);
        ring.s = s;
        ring.cap = want;
        ring.head = 0;
    }
    while (ring.count >= maxSteps() || (ring.count > 0 && ring.count == ring.cap)) {
        dropOldest();
    }
    ring.s[(ring.head + ring.count) % ring.cap] = step;
    ring.count++;
    ring.bytes += step.packedSize;
    size_t budget = (size_t)cfg.budget << 20;
    while (ring.count > 0 && ring.bytes + imageBytes > budget) {
        dropOldest();
    }
}

/* A chunk the worker compares (the game going on): the image's chunk i
 * against r.data, which stays put until the worker is done: a late chunk
 * (device pixels, where the list left them: valid until the list after
 * next) or a stable one (the listing's copies, a module's snapshot aside:
 * the same); at, len: its records in the worker's own delta. */
typedef struct {
    int i;
    StateChunkRef r;
    size_t at, len;
} Owed;

/* The live chunks (the game's memory listed in place: most of the image,
 * mostly pages never written, never late) are compared at the park, where
 * they are consistent, in lanes: the game thread and the helper threads
 * each take a contiguous share of the pieces (a chunk, or a page-aligned
 * part of a large one; a chunk whose size changed stays whole, in the game
 * thread's lane: it resizes the image) into a delta buffer of its own, at
 * the same time; the game thread waits for them (xg-hbc.20's leftover: the
 * memcmp of the whole image on the game thread, each capture). The worker
 * then assembles the delta in the chunks' order with the stable chunks'
 * records (its own, compared after the delta before) in their places. The
 * records are the serial compare's, but a run of changed blocks across a
 * piece's cut is two records (the same bytes). XG_REWIND_LANES: 1 (the
 * game thread alone) to REWIND_LANES_MAX; default REWIND_LANES. */
#define PIECE_MAX ((size_t)1 << 20)
typedef struct {
    int i;
    int whole; /* the chunk as a whole (diffInto) */
    int pre;   /* ... its size changed: compared by the game thread before the lanes (it resizes the image), into lanes.pre */
    size_t from, to; /* else the chunk's bytes [from, to), from page aligned */
    const uint8_t* now;
    StateChunkRef r; /* whole */
    int lane;
    size_t at, len; /* its records in the lane's delta */
} Piece;
static struct {
    int count; /* lanes in use, 0 before the first compare */
    SDL_Thread* thread[REWIND_LANES_MAX];
    SDL_Semaphore *go[REWIND_LANES_MAX], *done[REWIND_LANES_MAX];
    SDL_AtomicInt stop;
    Delta d[REWIND_LANES_MAX];
    Delta pre;
    Piece* piece;
    int pieceCount, pieceCap;
    int start[REWIND_LANES_MAX + 1]; /* lane l's pieces: [start[l], start[l + 1]) */
} lanes;

/* The worker's job, one at a time, kicked by a capture after its park's
 * compare (or by packFinish): the delta before's late chunks compared and
 * packed, then this capture's stable chunks compared and its delta
 * assembled into buf. The game thread reads or changes buf, the image, the
 * lanes' deltas and the ring only while no job runs (packBusy, packFinish).
 * Budget decisions use the image size at the original push point, before
 * the next capture can grow its chunks. */
static struct {
    SDL_Thread* thread;
    SDL_Semaphore *ready, *done;
    SDL_AtomicInt stop;
    int checked;
    int active;   /* a job is kicked and not collected */
    int hasDelta; /* the job packs a delta (packing.d, step): pushed when collected */
    Delta d;
    size_t imageBytes;
    Step step;
    Owed* late; /* the delta's late chunks, compared into d first, in this order */
    int lateCount, lateCap;
    Owed* stable; /* this capture's stable chunks, compared into own */
    int stableCount, stableCap;
    Delta own;
    int assemble;   /* this capture's delta to assemble into buf */
    int chunkCount; /* its chunks */
} packing;

static void diffInto(Delta* d, int i, const StateChunkRef* r);
static void diffRange(Delta* d, int chunk, uint8_t* old, const uint8_t* now, size_t from, size_t to);
static void logDelta(const Delta* d);
static int logOn(void);

/* Lane l's pieces compared into its delta. */
static void laneRun(int l) {
    Delta* d = &lanes.d[l];
    d->size = 0;
    for (int k = lanes.start[l]; k < lanes.start[l + 1]; k++) {
        Piece* pc = &lanes.piece[k];
        if (pc->pre) continue;
        pc->lane = l;
        pc->at = d->size;
        if (pc->whole) {
            diffInto(d, pc->i, &pc->r);
        } else {
            diffRange(d, pc->i, img.c[pc->i].data, pc->now, pc->from, pc->to);
        }
        pc->len = d->size - pc->at;
    }
}

static int laneWorker(void* arg) {
    int l = (int)(intptr_t)arg;
    for (;;) {
        SDL_WaitSemaphore(lanes.go[l]);
        if (SDL_GetAtomicInt(&lanes.stop)) return 0;
        laneRun(l);
        SDL_SignalSemaphore(lanes.done[l]);
    }
}

static void lanesInit(void) {
    if (lanes.count > 0) return;
    loadSettings();
    lanes.count = cfg.lanes; /* rewind_lanes (XG_REWIND_LANES for one run) */
    if (getenv("XG_REWIND_PACK_SYNC") != NULL && strcmp(getenv("XG_REWIND_PACK_SYNC"), "1") == 0) {
        lanes.count = 1; /* the synchronous test path: no threads */
    }
    for (int l = 1; l < lanes.count; l++) {
        lanes.go[l] = SDL_CreateSemaphore(0);
        lanes.done[l] = SDL_CreateSemaphore(0);
        lanes.thread[l] = lanes.go[l] && lanes.done[l] ? SDL_CreateThread(laneWorker, "rewind lane", (void*)(intptr_t)l) : NULL;
        if (lanes.thread[l] == NULL) {
            lanes.count = l;
            break;
        }
    }
}

static void lanesShutdown(void) {
    if (lanes.count > 1) {
        SDL_SetAtomicInt(&lanes.stop, 1);
        for (int l = 1; l < lanes.count; l++) {
            SDL_SignalSemaphore(lanes.go[l]);
            SDL_WaitThread(lanes.thread[l], NULL);
            SDL_DestroySemaphore(lanes.go[l]);
            SDL_DestroySemaphore(lanes.done[l]);
        }
    }
    for (int l = 0; l < REWIND_LANES_MAX; l++) {
        free(lanes.d[l].raw);
    }
    free(lanes.pre.raw);
    free(lanes.piece);
    memset(&lanes, 0, sizeof(lanes));
}

static void pieceAdd(Piece pc) {
    if (lanes.pieceCount == lanes.pieceCap) {
        lanes.pieceCap = lanes.pieceCap ? lanes.pieceCap * 2 : 128;
        lanes.piece = xrealloc(lanes.piece, (size_t)lanes.pieceCap * sizeof(*lanes.piece));
    }
    lanes.piece[lanes.pieceCount++] = pc;
}

/* This capture's compare: the live chunks now, in the lanes; the stable
 * ones listed for the worker; the delta assembled by the worker (assemble). */
static void compareChunks(const StateChunkRef* r, int n) {
    lanesInit();
    lanes.pieceCount = 0;
    packing.stableCount = 0;
    size_t total = 0;
    for (int i = 0; i < n; i++) {
        if (r[i].late) continue;
        if (r[i].stable) {
            if (packing.stableCount == packing.stableCap) {
                packing.stableCap = packing.stableCap ? packing.stableCap * 2 : 64;
                packing.stable = xrealloc(packing.stable, (size_t)packing.stableCap * sizeof(*packing.stable));
            }
            packing.stable[packing.stableCount++] = (Owed){i, r[i], 0, 0};
            continue;
        }
        Chunk* c = &img.c[i];
        if (c->size != r[i].size || r[i].size <= PIECE_MAX) {
            pieceAdd((Piece){i, 1, c->size != r[i].size, 0, r[i].size, r[i].data, r[i], 0, 0, 0});
        } else {
            for (size_t from = 0; from < r[i].size; from += PIECE_MAX) {
                size_t to = r[i].size - from < PIECE_MAX ? r[i].size : from + PIECE_MAX;
                pieceAdd((Piece){i, 0, 0, from, to, r[i].data, r[i], 0, 0, 0});
            }
        }
        if (c->size == r[i].size) total += r[i].size;
    }
    /* the resized chunks first, here; then the lanes' shares: contiguous, by bytes */
    lanes.pre.size = 0;
    for (int k = 0; k < lanes.pieceCount; k++) {
        Piece* pc = &lanes.piece[k];
        if (!pc->pre) continue;
        pc->lane = -1;
        pc->at = lanes.pre.size;
        diffInto(&lanes.pre, pc->i, &pc->r);
        pc->len = lanes.pre.size - pc->at;
    }
    int l = 0;
    size_t sum = 0;
    lanes.start[0] = 0;
    for (int k = 0; k < lanes.pieceCount; k++) {
        const Piece* pc = &lanes.piece[k];
        if (pc->pre) continue;
        while (l + 1 < lanes.count && sum >= total * (size_t)(l + 1) / (size_t)lanes.count) {
            lanes.start[++l] = k;
        }
        sum += pc->whole ? pc->r.size : pc->to - pc->from;
    }
    for (int m = l + 1; m <= lanes.count; m++) {
        lanes.start[m] = lanes.pieceCount;
    }
    for (int m = 1; m < lanes.count; m++) {
        SDL_SignalSemaphore(lanes.go[m]);
    }
    laneRun(0);
    for (int m = 1; m < lanes.count; m++) {
        SDL_WaitSemaphore(lanes.done[m]);
    }
    packing.assemble = 1;
    packing.chunkCount = n;
}

/* The delta of this capture into buf: the lanes' pieces' records and the
 * stable chunks' (packing.own), in the chunks' order. */
static void assembleDelta(void) {
    int k = 0, m = 0;
    for (int i = 0; i < packing.chunkCount; i++) {
        while (k < lanes.pieceCount && lanes.piece[k].i == i) {
            const Piece* pc = &lanes.piece[k++];
            put((pc->pre ? lanes.pre.raw : lanes.d[pc->lane].raw) + pc->at, pc->len);
        }
        while (m < packing.stableCount && packing.stable[m].i == i) {
            const Owed* o = &packing.stable[m++];
            put(packing.own.raw + o->at, o->len);
        }
    }
}

static void packDelta(void) {
    if (packing.hasDelta) {
        for (int k = 0; k < packing.lateCount; k++) {
            diffInto(&packing.d, packing.late[k].i, &packing.late[k].r);
        }
        packing.step.rawSize = packing.d.size;
        packing.step.packed = xrealloc(NULL, statePackBound(packing.d.size));
        packing.step.packedSize = statePack(packing.d.raw, packing.d.size, packing.step.packed);
        packing.step.packed = xrealloc(packing.step.packed, packing.step.packedSize);
    }
    if (packing.assemble) {
        packing.own.size = 0;
        for (int m = 0; m < packing.stableCount; m++) {
            Owed* o = &packing.stable[m];
            o->at = packing.own.size;
            diffInto(&packing.own, o->i, &o->r);
            o->len = packing.own.size - o->at;
        }
        assembleDelta();
    }
}

static int packWorker(void* unused) {
    (void)unused;
    for (;;) {
        SDL_WaitSemaphore(packing.ready);
        if (SDL_GetAtomicInt(&packing.stop)) return 0;
        packDelta();
        SDL_SignalSemaphore(packing.done);
    }
}

static void packShutdown(void);

static void packInit(void) {
    if (packing.checked) return;
    packing.checked = 1;
    atexit(packShutdown);
    /* Test the synchronous fallback with identical code/data addresses. */
    const char* sync = getenv("XG_REWIND_PACK_SYNC");
    if (!sync || strcmp(sync, "1")) {
        packing.ready = SDL_CreateSemaphore(0);
        packing.done = SDL_CreateSemaphore(0);
        if (packing.ready && packing.done)
            packing.thread = SDL_CreateThread(packWorker, "rewind pack", NULL);
    }
}

/* The job's results taken (the worker done): the delta pushed. */
static void packCollect(void) {
    packing.active = 0;
    packing.assemble = 0;
    packing.stableCount = 0;
    packing.lateCount = 0;
    if (packing.hasDelta) {
        packing.hasDelta = 0;
        if (logOn()) {
            logDelta(&packing.d);
        }
        push(packing.step, packing.imageBytes);
        packing.step.packed = NULL;
    }
}

/* The job started: the delta prepared (packStart) and the compare's rest. */
static void packKick(void) {
    if (packing.active || (!packing.hasDelta && !packing.assemble)) return;
    packInit();
    packing.active = 1;
    if (packing.thread) SDL_SignalSemaphore(packing.ready);
    else { packDelta(); packCollect(); } /* synchronous creation-failure path */
}

static void packFinish(void) {
    packKick();
    if (!packing.active) return;
    if (packing.thread) SDL_WaitSemaphore(packing.done);
    packCollect();
}

/* Whether the worker still owns its job (packing.d, the step it fills, buf,
 * the lanes' deltas and the image's stable chunks; it never touches the
 * ring). Done: collected now, as packFinish would. */
static int packBusy(void) {
    packKick(); /* a delta prepared and not yet kicked (packStart alone) goes now */
    if (!packing.active) return 0;
    if (packing.thread && !SDL_TryWaitSemaphore(packing.done)) return 1;
    packCollect();
    return 0;
}

static void packShutdown(void) {
    packFinish();
    if (packing.thread) {
        SDL_SetAtomicInt(&packing.stop, 1);
        SDL_SignalSemaphore(packing.ready);
        SDL_WaitThread(packing.thread, NULL);
    }
    SDL_DestroySemaphore(packing.ready);
    SDL_DestroySemaphore(packing.done);
    free(packing.d.raw);
    free(packing.own.raw);
    free(packing.late);
    free(packing.stable);
    memset(&packing, 0, sizeof(packing));
    lanesShutdown();
}

void rewindShutdown(void) { packShutdown(); }

/* The delta built so far (buf) becomes the job's, to pack once its late
 * chunks are compared (packLate); kicked by packKick (a capture, after its
 * compare) or packFinish. */
static void packStart(Meta meta) {
    packInit();
    Delta spare = packing.d;
    packing.d = buf;
    packing.step = (Step){.rawSize = buf.size, .meta = meta};
    packing.imageBytes = img.bytes;
    buf = spare;
    buf.size = 0;
    packing.hasDelta = 1;
}

/* Chunk i of the image compared by the delta's packing (settlePixels
 * queues it before packStart; the worker then owns that chunk). */
static void packLate(int i, const StateChunkRef* r) {
    if (packing.lateCount == packing.lateCap) {
        packing.lateCap = packing.lateCap ? packing.lateCap * 2 : 16;
        packing.late = xrealloc(packing.late, (size_t)packing.lateCap * sizeof(*packing.late));
    }
    packing.late[packing.lateCount++] = (Owed){i, *r, 0, 0};
}

static int popNewest(Step* out) {
    if (ring.count == 0) {
        return 0;
    }
    Step* s = &ring.s[(ring.head + ring.count - 1) % ring.cap];
    *out = *s;
    s->packed = NULL;
    ring.count--;
    ring.bytes -= out->packedSize;
    return 1;
}

void rewindReset(void) {
    packFinish();
    if (pend.active) {
        pend.active = 0;
        stateLateFinish(); /* nothing left in flight into the listing's buffers */
    }
    while (ring.count > 0) {
        dropOldest();
    }
    img.valid = 0;
    run.haveCapture = 0;
}

/* ---- capturing ------------------------------------------------------------------------ */

static Meta metaNow(void) {
    Meta m = {hwVSyncCount(), spuOutputDumpFrames(), cdBootDiscNumber()};
    return m;
}

/* The image's chunk i becomes the capture's (delta records into d). */
static void diffInto(Delta* d, int i, const StateChunkRef* r) {
    Chunk* c = &img.c[i];
    if (c->size != r->size) {
        /* The common part as any other, then the old size and, when it was
         * larger, the old tail. */
        size_t common = c->size < r->size ? c->size : r->size;
        diffChunk(d, i, c->data, r->data, common);
        uint8_t rec = REC_RESIZE;
        size_t tail = c->size - common;
        putTo(d, &rec, 1);
        put32To(d, (uint32_t)i);
        put32To(d, (uint32_t)c->size);
        put32To(d, (uint32_t)tail);
        putTo(d, c->data + common, tail);
        chunkReserve(c, r->size);
        if (r->size > common) {
            memcpy(c->data + common, r->data + common, r->size - common);
        }
        c->size = r->size;
    } else {
        diffChunk(d, i, c->data, r->data, c->size);
    }
}

static void logDelta(const Delta* d) {
    /* Where the delta's bytes are, by chunk (a diagnostic). */
    static size_t perChunk[512], rawSum, runs;
    static char names[512][48];
    static int captures;
    rawSum += d->size;
    size_t at = 0;
    while (at < d->size && d->raw[at] == REC_XOR) {
        uint32_t chunk = get32(d->raw + at + 1), len = get32(d->raw + at + 9);
        if (chunk < 512) {
            perChunk[chunk] += len;
            snprintf(names[chunk], sizeof names[chunk], "%.*s", img.c[chunk].tagLen, img.c[chunk].tag);
        }
        at += 13 + len;
        runs++;
    }
    if (++captures % 300 == 0) {
        fprintf(stderr, "rewind: per capture over the last 300:");
        for (int i = 0; i < 512; i++) {
            if (perChunk[i] >= 300 * 1024) {
                fprintf(stderr, " %s %zu KB", names[i], perChunk[i] / 300 / 1024);
            }
        }
        fprintf(stderr, "; raw %zu KB in %zu runs; history %d steps, %zu MB (image %zu MB); game thread %.3f ms a capture "
                        "(worst %.3f; the listing %.3f, the compare %.3f)\n",
                rawSum / 300 / 1024, runs / 300, ring.count, (ring.bytes + img.bytes) >> 20, img.bytes >> 20,
                run.captures ? (double)run.captureNs * 1e-6 / (double)run.captures : 0.0, run.worstMs,
                run.captures ? (double)run.listNs * 1e-6 / (double)run.captures : 0.0,
                run.captures ? (double)run.compareNs * 1e-6 / (double)run.captures : 0.0);
        memset(perChunk, 0, sizeof perChunk);
        rawSum = runs = 0;
    }
}

/* XG_REWIND_LOG, read once (it is looked at in every capture). */
static int logOn(void) {
    static int on = -1;
    if (on < 0) {
        on = getenv("XG_REWIND_LOG") != NULL;
    }
    return on;
}

static void costAdd(uint64_t ns) {
    double ms = (double)ns * 1e-6;
    run.lastMs = ms;
    run.worstMs = ms > run.worstMs ? ms : run.worstMs;
    run.captureNs += ns;
}

static void settlePixels(int background) {
    packFinish();
    if (!pend.active) {
        return;
    }
    uint64_t t0 = SDL_GetTicksNS();
    CPU_TIME_BEGIN(CPU_STATE);
    pend.active = 0;
    stateLateFinish();
    for (int i = 0; i < pend.n; i++) {
        if (!pend.r[i].late) {
            continue;
        }
        if (pend.first || pend.whole) {
            chunkSet(&img.c[i], &pend.r[i], 1);
        } else {
            /* Compared on the worker (the pixels stay put until the list
             * after next); its room here, as the image's size is the
             * budget's on the game thread. */
            chunkReserve(&img.c[i], pend.r[i].size);
            packLate(i, &pend.r[i]);
        }
    }
    if (!pend.first) {
        packStart(pend.meta); /* kicked by the capture after its compare (packKick), or now */
        if (!background) packFinish();
    }
    CPU_TIME_END(CPU_STATE);
    costAdd(pend.ns + (SDL_GetTicksNS() - t0));
}

void rewindSettle(void) { settlePixels(0); }

static void capture(void) {
    /* The worker still on the job before (a large delta after a scene
     * change): no capture now; the next park tries again (rewindWanted:
     * lastCapture is unchanged), the capture before still owed its device
     * pixels, which stay valid until the next list (savestate.c
     * stateListChunks). Nothing waits on the game thread. */
    if (packBusy()) {
        if (logOn()) {
            fprintf(stderr, "rewind: vblank %u: still packing the last delta, the capture waits\n", hwVSyncCount());
        }
        return;
    }
    settlePixels(1);
    uint64_t t0 = SDL_GetTicksNS();
    CPU_TIME_BEGIN(CPU_STATE);
    const StateChunkRef* r;
    int n;
    stateListChunks(&r, &n, 1);
    run.listNs += SDL_GetTicksNS() - t0;
    Meta now = metaNow();
    pend.r = r;
    pend.n = n;
    pend.first = !img.valid;
    pend.whole = 0;
    pend.meta = img.meta;
    buf.size = 0;
    if (!img.valid) {
        packFinish();
        imageFrom(r, n);
        lanesInit();
        if (logOn()) {
            size_t live = 0, stable = 0, late = 0;
            for (int i = 0; i < n; i++) {
                *(r[i].late ? &late : r[i].stable ? &stable : &live) += r[i].size;
            }
            fprintf(stderr, "rewind: %d chunks: %zu KB live (compared at the park, %d lanes), %zu KB stable, %zu KB late\n",
                    n, live >> 10, lanes.count, stable >> 10, late >> 10);
        }
    } else {
        int same = n == img.count;
        for (int i = 0; same && i < n; i++) {
            same = r[i].tagLen == img.c[i].tagLen && memcmp(r[i].tag, img.c[i].tag, (size_t)r[i].tagLen) == 0;
        }
        if (!same) {
            if (logOn()) {
                fprintf(stderr, "rewind: the chunks changed (%d -> %d): the whole image kept\n", img.count, n);
            }
            packFinish();
            wholeImage();
            imageFrom(r, n);
            pend.whole = 1;
        } else {
            /* The live chunks compared now, in the lanes; the stable ones by the
             * worker, which assembles the delta after the delta before (packDelta). */
            uint64_t t1 = SDL_GetTicksNS();
            compareChunks(r, n);
            run.compareNs += SDL_GetTicksNS() - t1;
        }
    }
    img.meta = now;
    pend.active = 1;
    packKick();
    CPU_TIME_END(CPU_STATE);
    pend.ns = SDL_GetTicksNS() - t0;
    run.captures++;
    run.lastCapture = now.vblank;
    run.haveCapture = 1;
}

/* ---- going back ------------------------------------------------------------------------- */

static int restoreImage(void) {
    for (int i = 0; i < img.count; i++) {
        img.refs[i].tag = img.c[i].tag;
        img.refs[i].tagLen = img.c[i].tagLen;
        img.refs[i].data = img.c[i].data;
        img.refs[i].size = img.c[i].size;
        img.refs[i].late = 0;
    }
    char why[300];
    if (!stateRestoreChunks(img.refs, img.count, img.meta.disc, img.meta.dumpFrames, why, sizeof why)) {
        fprintf(stderr, "rewind: cannot restore a capture: %s\n", why);
        rewindReset();
        return 0;
    }
    return 1;
}

/* One capture back: the newest delta applied to the image. */
static int stepBack(void) {
    Step step;
    if (!popNewest(&step)) {
        return 0;
    }
    if (buf.cap < step.rawSize) {
        buf.cap = step.rawSize;
        buf.raw = xrealloc(buf.raw, buf.cap);
    }
    int ok = stateUnpack(step.packed, step.packedSize, buf.raw, step.rawSize) && applyBack(buf.raw, step.rawSize);
    free(step.packed);
    if (!ok) {
        fprintf(stderr, "rewind: a step back is damaged; the history is dropped\n");
        rewindReset();
        return 0;
    }
    img.meta = step.meta;
    return 1;
}

static SDL_Scancode rewindKey(void) {
    for (int b = 0; b < PAD_BUTTONS; b++) {
        for (int k = 0; k < PAD_KEYS; k++) {
            if (padKey(b, k) == SDL_SCANCODE_R) {
                return SDL_SCANCODE_F10; /* R plays a PS1 button */
            }
        }
    }
    return SDL_SCANCODE_R;
}

static struct {
    uint32_t at;
    int frames, read;
} hold; /* XG_REWIND_HOLD */

static int held(void) {
    if (!hold.read) {
        hold.read = 1;
        const char* e = getenv("XG_REWIND_HOLD");
        unsigned long v;
        if (e != NULL && sscanf(e, "%lu:%d", &v, &hold.frames) == 2) {
            hold.at = (uint32_t)v;
        }
    }
    if (hold.frames > 0 && (run.active || hwVSyncCount() >= hold.at)) {
        hold.frames -= run.active; /* counted in displayed frames */
        return 1;
    }
    const bool* keys = SDL_GetKeyboardState(NULL);
    if (keys != NULL && keys[rewindKey()]) {
        return 1;
    }
    return padRewindComboHeld != NULL && padRewindComboHeld();
}

/* ---- the harness --------------------------------------------------------------------- */

static struct {
    int parsed, count, next;
    uint32_t at[16];
    int steps[16];
} harness;

static void harnessParse(void) {
    harness.parsed = 1;
    const char* e = getenv("XG_REWIND_AT");
    while (e != NULL && *e != '\0' && harness.count < 16) {
        unsigned long v;
        int k, n = 0;
        if (sscanf(e, "%lu:%d%n", &v, &k, &n) < 2) {
            break;
        }
        harness.at[harness.count] = (uint32_t)v;
        harness.steps[harness.count] = k;
        harness.count++;
        e += n;
        e = *e == ',' ? e + 1 : NULL;
    }
}

/* ---- the park point -------------------------------------------------------------------- */

int rewindEnabled(void) {
    loadSettings();
    return cfg.on;
}

int rewindWanted(uint32_t vblank) {
    loadSettings();
    if (!cfg.on || !stateAvailable(NULL)) {
        return 0;
    }
    if (!harness.parsed) {
        harnessParse();
    }
    if (harness.next < harness.count && vblank >= harness.at[harness.next]) {
        return REWIND_BACK;
    }
    if (img.valid && held()) {
        return REWIND_BACK;
    }
    if (!run.haveCapture || vblank - run.lastCapture >= (uint32_t)cfg.interval || vblank < run.lastCapture) {
        return REWIND_CAPTURE;
    }
    return 0;
}

static void note(const char* text) {
    fprintf(stderr, "rewind: %s\n", text);
    presentShowNote(text);
}

void rewindServe(int what) {
    if (what == REWIND_CAPTURE) {
        capture();
        return;
    }
    rewindSettle(); /* the latest capture whole first */
    if (harness.next < harness.count && hwVSyncCount() >= harness.at[harness.next]) {
        /* The harness: that many captures back, then on. */
        int steps = harness.steps[harness.next++];
        uint32_t from = hwVSyncCount();
        for (int i = 0; i < steps && stepBack(); i++) {
        }
        if (img.valid && restoreImage()) {
            fprintf(stderr, "rewind: from vblank %u back to %u (audio dump frame %llu)\n", from, img.meta.vblank,
                    (unsigned long long)img.meta.dumpFrames);
        }
        run.lastCapture = hwVSyncCount();
        return;
    }
    if (!img.valid) {
        return;
    }
    /* Held: the latest capture, then one further back each displayed frame,
     * until let go (or the oldest, held there). */
    run.active = 1;
    spuOutputPause(1);
    uint64_t next = SDL_GetTicksNS();
    int first = 1;
    while (held()) {
        if (first || stepBack()) {
            first = 0;
            if (!restoreImage()) {
                break;
            }
            char text[80];
            snprintf(text, sizeof text, "Rewinding: %.1f s kept", ring.count * cfg.interval / 60.0);
            presentShowNote(text);
        }
        static int shots = -1;
        char shot[32];
        if (shots < 0) {
            shots = getenv("XG_REWIND_SHOTS") != NULL ? 0 : -2; /* each picture shown, as rewind_<n>.png */
        }
        if (shots >= 0) {
            snprintf(shot, sizeof shot, "rewind_%04d", shots++);
        }
        gpuPresentMenu(shots >= 0 ? shot : NULL);
        padPumpEvents();
        hwPausedFrame();
        next += 16666667ull;
        uint64_t now = SDL_GetTicksNS();
        if (next > now) {
            SDL_DelayNS(next - now);
        } else {
            next = now;
        }
    }
    spuOutputPause(0);
    run.active = 0;
    run.lastCapture = hwVSyncCount();
    if (ring.count == 0) {
        note("Rewind: the oldest moment kept");
    }
}

/* ---- the menu's view --------------------------------------------------------------------- */

void rewindStats(RewindStats* out) {
    loadSettings();
    packFinish();
    memset(out, 0, sizeof(*out));
    out->on = cfg.on;
    out->seconds = cfg.seconds;
    out->interval = cfg.interval;
    out->budget = cfg.budget;
    out->lanes = cfg.lanes;
    out->kept = ring.count * cfg.interval / 60.0;
    out->bytes = ring.bytes + img.bytes;
    out->captureMs = run.captures ? (double)run.captureNs * 1e-6 / (double)run.captures : 0;
    out->worstMs = run.worstMs;
}

void rewindSetOn(int on) {
    loadSettings();
    packFinish();
    cfg.on = on != 0;
    settingsSetInt("rewind", cfg.on);
    settingsSave();
    if (!cfg.on) {
        rewindReset();
        imageFree();
    }
}

void rewindSetSeconds(int seconds) {
    loadSettings();
    packFinish();
    cfg.seconds = seconds < 5 ? 5 : seconds > 600 ? 600 : seconds;
    settingsSetInt("rewind_seconds", cfg.seconds);
    settingsSave();
    while (ring.count > maxSteps()) {
        dropOldest();
    }
}

void rewindSetInterval(int vblanks) {
    loadSettings();
    packFinish();
    cfg.interval = vblanks < 1 ? 1 : vblanks > 10 ? 10 : vblanks;
    settingsSetInt("rewind_interval", cfg.interval);
    settingsSave();
    rewindReset(); /* the history's steps had the old spacing */
}

void rewindSetLanes(int lanes) {
    loadSettings();
    packFinish();
    cfg.lanes = lanes < 1 ? 1 : lanes > REWIND_LANES_MAX ? REWIND_LANES_MAX : lanes;
    settingsSetInt("rewind_lanes", cfg.lanes);
    settingsSave();
    lanesShutdown(); /* the next capture starts the lanes anew */
}

void rewindSetBudget(int megabytes) {
    loadSettings();
    packFinish();
    cfg.budget = megabytes < 32 ? 32 : megabytes > 4096 ? 4096 : megabytes;
    settingsSetInt("rewind_budget", cfg.budget);
    settingsSave();
    while (ring.count > 0 && ring.bytes + img.bytes > ((size_t)cfg.budget << 20)) {
        dropOldest();
    }
}
