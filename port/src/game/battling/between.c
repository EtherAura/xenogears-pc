/*
 * Battling (the arena) at 60 Hz (xg-hbc.19.2, Enhanced engine, battle60):
 * the picture between two frames of the match.
 *
 * The match's logic and drawing are one coroutine step a frame, every two
 * vblanks (battlingMain: VSync(2)), and its drawing is not pure: the renderer
 * moves and spawns particles, fades the Gears' glow, draws from the random
 * generator and places the sounds. So the picture between is made the way
 * the transitions' is (libetc.c pcVSyncBetween: inside the wait, at its
 * first vblank, under a clock hold), but with the previous frame's render
 * run again in a sandbox: at every frame's render entry the memory the
 * module can write (its data and BSS, the game heap, the scratchpad, the
 * random seed) is copied aside with the frame's transforms (the Gears'
 * positions and turns, every skeleton node's position and rotation, the
 * camera, the HUD bars, the geometry screen). Inside the wait of frame k the
 * display shows frame k-2 and frame k-1 comes next (the frames are double
 * buffered), so the sandbox is loaded with frame k-1's pre-render memory, the
 * transforms are put halfway back towards frame k-2's, the frame's renderer
 * runs (its side effects land in the sandbox; the sound placement skipped),
 * its ordering table is drawn into the buffer on display (the one frame k
 * draws over after the wait), and the live memory is put back. The effects
 * (particles, sparks, trails) are therefore frame k-1's exactly, half a frame
 * ahead of the halfway Gears; the Gears, camera and HUD are halfway. Nothing
 * of the match's state changes: the sweep line (sweep.c), the invariants and
 * the audio are the oracle (work/battling60). The match draws over the
 * picture of two frames ago (no full clear: the ring wall's outside, the
 * HUD's surroundings) and the render uploads into VRAM (the HUD's text,
 * CLUTs) where the native frame then draws from, so every VRAM tile the
 * picture between writes is kept before the write and put back after the
 * wait, before the native frame draws: the displayed buffer on the device
 * (libgpu.c pcVramKeepDevice: its native rectangle, its hi-res copy and,
 * with a wide picture, its wide rows copied to spare images and back, no
 * readback, no wait; the CPU keep of it, a readback, when the device has no
 * room), the uploads and any draw elsewhere on the CPU at their first write
 * (vram.c vramKeepWrite/Draw), the changed tiles put back through
 * pcVramPutBack as the ripple's LoadImage: every native frame's pixels stay
 * the Original's. (The whole VRAM, a 1 MB readback and upload a frame, was
 * the first form, the displayed buffer's readback the second; xg-hbc.24,
 * .25.) The render may also write voice registers the live frame set
 * (volumes, pitches): kept and put back too (spu.c spuRegsKeep/PutBack).
 *
 * The memory records are page-wise (4 KB) differences, exact: the module's
 * data and BSS, the whole heap arena and the scratchpad are one image of
 * pages. A record at frame k's render entry compares the live memory with
 * frame k-1's record (D_k: the pages frame k-1's render and frame k's logic
 * changed) and copies into frame k-2's record, which it replaces, the pages
 * of D_k and D_(k-1): frame k's image differs from frame k-2's on no other.
 * Inside the wait the live memory (frame k's, rendered) differs from frame
 * k-1's record only on D_k and on the pages frame k's render changed (R_k:
 * compared against frame k's record): those pages go aside and take frame
 * k-1's; after the sandbox render they come back, and every other page is
 * compared with frame k's record (equal to the live memory there) and put
 * back from it where the render wrote. Three compares of the image a frame
 * instead of four copies of it (44 MB: ~3 ms of a 4.4 ms frame on the
 * stepped clock, xg-hbc.24); each compare runs in lanes, the helper threads
 * of port/src/system/workers.c and this thread at once, each lane a range
 * of whole bitmap words of pages (the result is the same page sets).
 *
 * This file is linked into the battling module, so its statics would sit in
 * the BSS the sandbox restores: everything that changes lives in host memory
 * behind one pointer, set once before the first record (every snapshot then
 * holds the same value), and the two flags beside it are set before it too.
 */
#include "common.h"
#include "psyq.h"
#include "battling/battling.h"
#include "gte_pc.h"
#include "pc_wide.h"

extern BattlingGear battlingGear1;
extern BattlingGear battlingGear2;
extern VECTOR battlingCameraPos;
extern VECTOR battlingCameraTarget;
extern VECTOR battlingViewOrigin;
extern BattlingDisplayBuffer* battlingDisplayBuffer;
extern OTag* battlingCurrentOt;
extern u8 battlingBufferIndex;
extern s16 battlingVsyncInterval;
extern s32 battlingFrameCounter;
extern u8 battlingScreenClearFlag;
s32 renderBattlingArenaScene(BattlingScene* scene);
s32 renderBattlingDemoScene(BattlingScene* scene);
unsigned char* portHeapStart(void); /* port/src/system/memory.c */
unsigned char* portHeapEnd(void);
int portModuleRange(const char* name, void** dataStart, void** dataEnd, void** bssStart, void** bssEnd);
void* portSandboxAlloc(uptr n);
u32 portRandSeed(void); /* port/src/system/libc.c */
void srand(u32 seed);
int pcGameplayBattle60(void);
void pcVSyncBetween(void (*draw)(void)); /* port/src/system/libetc.c */
s32 ReadGeomScreen(void);
void SetGeomScreen(s32 h);
void DrawOTagEnv(void* ot, DRAWENV* env);
void hwClockHold(int on); /* port/src/system/hw.c */
void spuRegsKeep(void);    /* port/src/system/spu.c */
void spuRegsPutBack(void);
void pcVramPutBack(RECT* rect, void* data); /* port/src/gpu/libgpu.c */
int pcVramKeepDevice(RECT* rect);           /* port/src/gpu/libgpu.c: the displayed buffer kept on the device */
void pcVramKeepDevicePutBack(RECT* rect);
void pcGpuQuiet(int on);                    /* port/src/gpu/libgpu.c */
void vramBookkeepingKeep(void);             /* port/src/gpu/vram.c */
void vramBookkeepingPutBack(void);
int vramKeepBegin(void); /* port/src/gpu/vram.c: the tiles the picture between writes, kept first */
void vramKeepRect(int x, int y, int w, int h);
void vramKeepEnd(void);
int vramKeepChanged(int* index, int* x, int* y, int* w, int* h, u16* out);
unsigned long long vramKeepBytes(void);
void cpuTimeBetween(int part, int on); /* port/src/system/cputime.c (XG_CPU_TIMING): 0 keep, 1 memory, 2 draw, 3 put back */
void cpuTimeBetweenBytes(int which, unsigned long long n); /* 0 memory, 1 VRAM */
int workersRun(void (*fn)(void* arg, int part, int parts), void* arg); /* port/src/system/workers.c */

#define SCRATCHPAD ((unsigned char*)(uptr)0x1F800000UL)
#define SCRATCHPAD_SIZE 0x400
#define NODES_MAX 160
#define JUMP_POSITION 0x1000 /* a Gear or node moved further in one frame: a cut, drawn where it is */
#define JUMP_ANGLE 0x400

#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << PAGE_SHIFT)
#define SEGMENTS 4
#define TILE_RUN (1024 * 16) /* a put back run: a row of tiles (vramKeepChanged) */
#define LANES_MAX 8          /* workersRun's parts at most (workers.c WORKERS_MAX) */

typedef struct {
    unsigned char* live; /* where the memory lives */
    uptr len;
    uptr off; /* its page-aligned offset in an image */
} Segment;

typedef struct {
    unsigned char* image; /* the segments' bytes, each at its offset */
    u64* diff;            /* pages where this image differs from the frame before's (all set when unknown) */
    u32 seed;
    s32 frame; /* battlingFrameCounter at the record */
} Memory;

typedef struct {
    SVECTOR position;
    SVECTOR rotation;
    VECTOR translation;
} NodePose;

typedef struct {
    s32 frame;
    BattlingScene* scene;
    int demo;
    s32 geomScreen;
    VECTOR gearPos[2];
    s32 gearRotY[2];
    s16 barHp[2];
    s16 gauge[2];
    s32 nodeCount[2];
    BattlingNode* nodes[2][NODES_MAX];
    NodePose pose[2][NODES_MAX];
    VECTOR camPos, camTarget, viewOrigin;
} Transforms;

typedef struct {
    Segment seg[SEGMENTS]; /* the heap arena, the module's data, its BSS, the scratchpad */
    int segCount;
    uptr imageLen, pages, diffWords;
    Memory records[2];  /* by frame parity: the two last frames' pre-render memory */
    Memory live;        /* the live memory's pages the sandbox takes over (diff: which) */
    u64* both;          /* a record's pages to copy */
    u32 liveSeed;
    Transforms ring[3]; /* by frame mod 3 */
    s32 recorded;       /* frames recorded in a row (0 after a gap) */
    s32 lastFrame;
    int inBetween;
    int kept; /* a VRAM keep is open (vram.c): changed tiles to put back */
    RECT keptClip; /* the displayed buffer's rectangle kept (pcVramKeepDevice) */
    u16 run[TILE_RUN];
} Sandbox;

/* A compare of the live memory with an image, in lanes (workersRun): lane
 * `part` of `parts` takes a range of whole bitmap words of pages. Mode 0:
 * the differing pages' bits set in out (zeroed first by the caller); mode
 * 1: the pages not in skip take the image's where they differ. */
typedef struct {
    const unsigned char* image;
    u64* out;
    const u64* skip;
    int mode;
    uptr bytes[LANES_MAX];
} Lanes;

static int sandboxReady, sandboxFailed;
static Sandbox* sb;

/* The next segment of the image, at a page-aligned offset: a page belongs
 * to one segment, whole or its tail. */
static void segmentAdd(void* live, uptr len) {
    Segment* g;

    if (len == 0 || sb->segCount >= SEGMENTS) return;
    g = &sb->seg[sb->segCount++];
    g->live = live;
    g->len = len;
    g->off = sb->imageLen;
    sb->imageLen += (len + PAGE_SIZE - 1) & ~(uptr)(PAGE_SIZE - 1);
}

static int sandboxInit(void) {
    void *ds, *de, *bs, *be;
    int i;

    if (sandboxReady) return 1;
    if (sandboxFailed) return 0;
    if (!portModuleRange("battling", &ds, &de, &bs, &be)) {
        sandboxFailed = 1;
        return 0;
    }
    sb = portSandboxAlloc(sizeof *sb);
    if (sb == NULL) {
        sandboxFailed = 1;
        return 0;
    }
    __builtin_memset(sb, 0, sizeof *sb);
    segmentAdd(portHeapStart(), (uptr)(portHeapEnd() - portHeapStart()));
    segmentAdd(ds, (unsigned char*)de > (unsigned char*)ds ? (uptr)((unsigned char*)de - (unsigned char*)ds) : 0);
    segmentAdd(bs, (unsigned char*)be > (unsigned char*)bs ? (uptr)((unsigned char*)be - (unsigned char*)bs) : 0);
    segmentAdd(SCRATCHPAD, SCRATCHPAD_SIZE);
    sb->pages = sb->imageLen >> PAGE_SHIFT;
    sb->diffWords = (sb->pages + 63) / 64;
    sb->both = portSandboxAlloc(sb->diffWords * sizeof(u64));
    if (sb->both == NULL) {
        sandboxFailed = 1;
        return 0;
    }
    for (i = 0; i < 3; i++) {
        Memory* m = i < 2 ? &sb->records[i] : &sb->live;
        m->image = portSandboxAlloc(sb->imageLen);
        m->diff = portSandboxAlloc(sb->diffWords * sizeof(u64));
        if (m->image == NULL || m->diff == NULL) {
            sandboxFailed = 1;
            return 0;
        }
        m->frame = -2;
    }
    sandboxReady = 1;
    return 1;
}

/* ---- the memory sandbox --------------------------------------------------- */

/* Page p's live address and length (a segment's tail page is short). */
static uptr pageAt(uptr p, unsigned char** live) {
    uptr off = p << PAGE_SHIFT;
    int i;

    for (i = 0; i < sb->segCount; i++) {
        const Segment* g = &sb->seg[i];
        if (off >= g->off && off < g->off + g->len) {
            *live = g->live + (off - g->off);
            return g->off + g->len - off < PAGE_SIZE ? g->off + g->len - off : PAGE_SIZE;
        }
    }
    *live = NULL;
    return 0;
}

static void bitsFill(u64* bits, int on) {
    __builtin_memset(bits, on ? 0xFF : 0, sb->diffWords * sizeof(u64));
}

/* The whole live memory into m; every page counts as different. */
static void imageTakeAll(Memory* m) {
    int i;

    for (i = 0; i < sb->segCount; i++) {
        const Segment* g = &sb->seg[i];
        __builtin_memcpy(m->image + g->off, g->live, g->len);
    }
    bitsFill(m->diff, 1);
    cpuTimeBetweenBytes(0, sb->imageLen);
}

static void laneRun(void* arg, int part, int parts) {
    Lanes* L = arg;
    uptr w0 = sb->diffWords * (uptr)part / (uptr)parts, w1 = sb->diffWords * (uptr)(part + 1) / (uptr)parts;
    uptr p0 = w0 << 6, p1 = w1 >= sb->diffWords ? sb->pages : w1 << 6;
    uptr p, bytes = 0;

    for (p = p0; p < p1; p++) {
        unsigned char* live;
        uptr len;
        if (L->mode == 1 && (L->skip[p >> 6] >> (p & 63) & 1)) continue;
        len = pageAt(p, &live);
        if (!len || __builtin_memcmp(live, L->image + (p << PAGE_SHIFT), len) == 0) continue;
        if (L->mode == 0) {
            L->out[p >> 6] |= (u64)1 << (p & 63);
        } else {
            __builtin_memcpy(live, L->image + (p << PAGE_SHIFT), len);
            bytes += len;
        }
    }
    if (part < LANES_MAX) L->bytes[part] = bytes;
}

/* The compare in lanes; the bytes copied (mode 1). */
static uptr lanesRun(Lanes* L) {
    int parts, i;
    uptr bytes = 0;

    __builtin_memset(L->bytes, 0, sizeof L->bytes);
    parts = workersRun(laneRun, L);
    for (i = 0; i < parts && i < LANES_MAX; i++) bytes += L->bytes[i];
    return bytes;
}

/* out: the pages where the live memory differs from m's image (bits set). */
static void diffLive(const Memory* m, u64* out) {
    Lanes L;

    bitsFill(out, 0);
    L.image = m->image;
    L.out = out;
    L.skip = NULL;
    L.mode = 0;
    lanesRun(&L);
}

/* The set pages: the live memory into m's image (in), or m's image into the live memory. */
static void pagesCopy(unsigned char* image, const u64* bits, int in) {
    uptr p, bytes = 0;

    for (p = 0; p < sb->pages; p++) {
        unsigned char* live;
        uptr len;
        if (!(bits[p >> 6] >> (p & 63) & 1)) continue;
        len = pageAt(p, &live);
        if (!len) continue;
        if (in) __builtin_memcpy(image + (p << PAGE_SHIFT), live, len);
        else __builtin_memcpy(live, image + (p << PAGE_SHIFT), len);
        bytes += len;
    }
    cpuTimeBetweenBytes(0, bytes);
}

/* At frame k's render entry: frame k's pre-render memory into the record
 * of frame k-2 (by parity), as the pages that differ from frame k-1's
 * record (D_k) and the pages that record differs from frame k-2's on
 * (D_(k-1)); the whole memory when either record is not the frame's. */
static void recordTake(s32 frame) {
    Memory* cur = &sb->records[frame & 1];
    const Memory* prev = &sb->records[(frame - 1) & 1];
    uptr w;

    cpuTimeBetween(1, 1);
    if (prev->frame == frame - 1 && cur->frame == frame - 2) {
        diffLive(prev, cur->diff);
        for (w = 0; w < sb->diffWords; w++) sb->both[w] = cur->diff[w] | prev->diff[w];
        pagesCopy(cur->image, sb->both, 1);
    } else {
        imageTakeAll(cur);
    }
    cur->seed = portRandSeed();
    cur->frame = frame;
    cpuTimeBetween(1, 0);
}

/* The live memory (frame k's, rendered) becomes frame k-1's record m: the
 * pages that differ (D_k from m's successor rk, R_k: compared against rk)
 * go aside (sb->live) and take m's. */
static void sandboxEnter(const Memory* m, const Memory* rk) {
    uptr w;

    cpuTimeBetween(1, 1);
    sb->liveSeed = portRandSeed();
    diffLive(rk, sb->live.diff);
    for (w = 0; w < sb->diffWords; w++) sb->live.diff[w] |= rk->diff[w];
    pagesCopy(sb->live.image, sb->live.diff, 1);
    pagesCopy(m->image, sb->live.diff, 0);
    srand(m->seed);
    cpuTimeBetween(1, 0);
}

/* The live memory back: the pages taken over from what went aside, every
 * other from frame k's record (the live memory's own values there) where
 * the sandbox render wrote. */
static void sandboxLeave(const Memory* rk) {
    Lanes L;

    cpuTimeBetween(1, 1);
    pagesCopy(sb->live.image, sb->live.diff, 0);
    L.image = rk->image;
    L.out = NULL;
    L.skip = sb->live.diff;
    L.mode = 1;
    cpuTimeBetweenBytes(0, lanesRun(&L));
    srand(sb->liveSeed);
    cpuTimeBetween(1, 0);
}

/* ---- the transforms ------------------------------------------------------- */

static s32 collectNodes(BattlingNode* root, BattlingNode** out) {
    BattlingNode* stack[NODES_MAX];
    s32 sp = 0;
    s32 n = 0;

    if (root == NULL) return 0;
    stack[sp++] = root;
    while (sp > 0) {
        BattlingNode* node = stack[--sp];
        while (node != NULL) {
            if (n >= NODES_MAX) return n;
            out[n++] = node;
            if (node->m94_child != NULL && sp < NODES_MAX) {
                stack[sp++] = node->m94_child;
            }
            node = node->m90_next;
        }
    }
    return n;
}

static void transformsTake(Transforms* t, BattlingScene* scene, int demo) {
    BattlingGear* gears[2];
    s32 g, i;

    gears[0] = &battlingGear1;
    gears[1] = &battlingGear2;
    t->frame = battlingFrameCounter;
    t->scene = scene;
    t->demo = demo;
    t->geomScreen = ReadGeomScreen();
    for (g = 0; g < 2; g++) {
        t->gearPos[g] = gears[g]->m0_position;
        t->gearRotY[g] = gears[g]->m54_rotY;
        t->barHp[g] = gears[g]->mB8_barHp;
        t->gauge[g] = gears[g]->mB6_gauge;
        t->nodeCount[g] = collectNodes(gears[g]->m5C_model, t->nodes[g]);
        for (i = 0; i < t->nodeCount[g]; i++) {
            BattlingNode* n = t->nodes[g][i];
            t->pose[g][i].position = n->m2C_position;
            t->pose[g][i].rotation = n->m44_rotation;
            t->pose[g][i].translation = n->m34_translation;
        }
    }
    t->camPos = battlingCameraPos;
    t->camTarget = battlingCameraTarget;
    t->viewOrigin = battlingViewOrigin;
}

static s32 mid32(s32 a, s32 b) {
    s32 d = b - a;
    if (d > JUMP_POSITION || d < -JUMP_POSITION) return b;
    return a + d / 2;
}
static s16 mid16(s16 a, s16 b) {
    return (s16)mid32(a, b);
}
static s32 midAngle(s32 a, s32 b) {
    s32 d = ((b - a + 0x800) & 0xFFF) - 0x800;
    if (d > JUMP_ANGLE || d < -JUMP_ANGLE) return b;
    return a + d / 2;
}
static void midVector(VECTOR* out, const VECTOR* a, const VECTOR* b) {
    out->vx = mid32(a->vx, b->vx);
    out->vy = mid32(a->vy, b->vy);
    out->vz = mid32(a->vz, b->vz);
}

/* The live transforms (the sandbox holds b's frame): halfway from a to b. */
static void transformsHalfway(const Transforms* a, const Transforms* b) {
    BattlingGear* gears[2];
    s32 g, i;

    gears[0] = &battlingGear1;
    gears[1] = &battlingGear2;
    for (g = 0; g < 2; g++) {
        midVector(&gears[g]->m0_position, &a->gearPos[g], &b->gearPos[g]);
        gears[g]->m54_rotY = midAngle(a->gearRotY[g], b->gearRotY[g]);
        gears[g]->mB8_barHp = (s16)(a->barHp[g] + (b->barHp[g] - a->barHp[g]) / 2);
        gears[g]->mB6_gauge = (s16)(a->gauge[g] + (b->gauge[g] - a->gauge[g]) / 2);
        if (a->nodeCount[g] != b->nodeCount[g]) continue; /* a new skeleton: b's pose */
        for (i = 0; i < b->nodeCount[g]; i++) {
            BattlingNode* n = b->nodes[g][i];
            const NodePose* pa = &a->pose[g][i];
            const NodePose* pb = &b->pose[g][i];
            if (n != a->nodes[g][i]) continue;
            n->m2C_position.vx = mid16(pa->position.vx, pb->position.vx);
            n->m2C_position.vy = mid16(pa->position.vy, pb->position.vy);
            n->m2C_position.vz = mid16(pa->position.vz, pb->position.vz);
            n->m44_rotation.vx = (s16)midAngle(pa->rotation.vx, pb->rotation.vx);
            n->m44_rotation.vy = (s16)midAngle(pa->rotation.vy, pb->rotation.vy);
            n->m44_rotation.vz = (s16)midAngle(pa->rotation.vz, pb->rotation.vz);
            midVector(&n->m34_translation, &pa->translation, &pb->translation);
        }
    }
    midVector(&battlingCameraPos, &a->camPos, &b->camPos);
    midVector(&battlingCameraTarget, &a->camTarget, &b->camTarget);
    midVector(&battlingViewOrigin, &a->viewOrigin, &b->viewOrigin);
}

/* ---- the hooks ------------------------------------------------------------ */

int pcBattling60InBetween(void) {
    return sb != NULL && sb->inBetween;
}

static int active(void) {
    return battlingVsyncInterval == 2 && pcGameplayBattle60();
}

/* At a frame's render entry (renderBattlingArenaScene, renderBattlingDemoScene). */
void pcBattling60RenderHook(BattlingScene* scene, int demo) {
    s32 frame = battlingFrameCounter;

    if (sb != NULL && sb->inBetween) return;
    if (!active() || !sandboxInit()) {
        if (sb != NULL) sb->recorded = 0;
        return;
    }
    if (sb->recorded > 0 && frame != sb->lastFrame + 1) sb->recorded = 0;
    transformsTake(&sb->ring[frame % 3], scene, demo);
    recordTake(frame);
    sb->lastFrame = frame;
    if (sb->recorded < 3) sb->recorded++;
}

/* Inside the frame's VSync(2), at its first vblank. */
static void battling60Between(void) {
    s32 k = battlingFrameCounter; /* the frame built, not drawn yet */
    const Transforms* a = &sb->ring[(k - 2) % 3];
    const Transforms* b = &sb->ring[(k - 1) % 3];
    const Memory* m = &sb->records[(k - 1) & 1];
    const Memory* rk = &sb->records[k & 1];
    DRAWENV env;
    GteState gte;

    if (sb->recorded < 3 || sb->lastFrame != k || a->frame != k - 2 || b->frame != k - 1 || m->frame != k - 1 ||
        rk->frame != k)
        return;
    if (b->scene == NULL) return;
    env = battlingDisplayBuffer->m0_drawEnv; /* the buffer on display: drawn over after the wait */
    gteSave(&gte);
    pcWideStash(1);
    cpuTimeBetween(0, 1);
    /* VRAM's bookkeeping (generations, drawn tiles, owned hints: save state
     * a rewind restore cannot replay for a picture between) first: the keep
     * judges the changed tiles against its generations */
    vramBookkeepingKeep();
    if (!vramKeepBegin()) {
        vramBookkeepingPutBack();
        cpuTimeBetween(0, 0);
        pcWideStash(0);
        gteLoad(&gte);
        return;
    }
    sb->kept = 1;
    /* the displayed buffer now: on the device (its native rectangle, its
     * hi-res copy), else on the CPU (a readback); the render's uploads (the
     * HUD's text, CLUTs, into areas the native frame then draws from) and
     * any draw elsewhere at their first write (vram.c vramKeepWrite/Draw) */
    sb->keptClip = env.clip;
    if (!pcVramKeepDevice(&sb->keptClip)) vramKeepRect(env.clip.x, env.clip.y, env.clip.w, env.clip.h);
    /* and the wide picture of the buffer the picture between draws into
     * (its margins, its shifted 2D): the VRAM put back leaves wide alone */
    pcWideFrameKeep(&env.clip);
    spuRegsKeep();
    cpuTimeBetween(0, 0);
    sandboxEnter(m, rk);
    /* The sandbox's draw buffer is frame k-1's; its layer takes its draw area
     * and offset from it (addBattlingLayerToOt: SetDrawArea/SetDrawOffset
     * from battlingDisplayBuffer->m0_drawEnv), so without this the chain
     * switches, after the backdrop, to frame k-1's finished picture (which
     * the VRAM put back then repairs) and the picture between shows only the
     * backdrop over the picture of two frames ago. The buffer on display,
     * where the picture between goes, instead. */
    battlingDisplayBuffer->m0_drawEnv.clip = env.clip;
    battlingDisplayBuffer->m0_drawEnv.ofs[0] = env.ofs[0];
    battlingDisplayBuffer->m0_drawEnv.ofs[1] = env.ofs[1];
    cpuTimeBetween(2, 1);
    transformsHalfway(a, b);
    SetGeomScreen(b->geomScreen);
    sb->inBetween = 1;
    if (b->demo) {
        renderBattlingDemoScene(b->scene);
    } else {
        renderBattlingArenaScene(b->scene);
    }
    sb->inBetween = 0;
    spuRegsPutBack();
    /* battlingMain adds the lower screen's clear tile after the render (the
     * grey under the ground, which the wide picture carries across its
     * margins): without it the margins keep the picture of two frames ago
     * where the ground's cells do not reach. */
    if (battlingScreenClearFlag & 1) {
        AddPrim(battlingCurrentOt, &battlingDisplayBuffer->mE8_clearTile);
    }
    pcGpuQuiet(1); /* no DrawSync callback, no event for this drawing */
    DrawOTagEnv(battlingCurrentOt, &env);
    pcGpuQuiet(0);
    cpuTimeBetween(2, 0);
    sandboxLeave(rk);
    pcWideStash(0);
    gteLoad(&gte);
}

/* Around battlingMain's VSync(battlingVsyncInterval). */
void pcBattling60Wait(void) {
    pcVSyncBetween(sb != NULL && active() && sb->recorded >= 3 ? battling60Between : NULL);
}

void pcBattling60WaitDone(void) {
    pcVSyncBetween(NULL);
    if (sb != NULL && sb->kept) {
        /* the picture between gone before the native frame draws over it:
         * the kept tiles that changed, as runs (vram.c vramKeepChanged) */
        int index = 0, x, y, w, h;
        unsigned long long bytes = vramKeepBytes();
        sb->kept = 0;
        hwClockHold(1);
        cpuTimeBetween(3, 1);
        vramKeepEnd();
        pcGpuQuiet(1);
        while (vramKeepChanged(&index, &x, &y, &w, &h, sb->run)) {
            RECT r;
            r.x = (s16)x;
            r.y = (s16)y;
            r.w = (s16)w;
            r.h = (s16)h;
            pcVramPutBack(&r, sb->run);
            bytes += (unsigned long long)w * h * 2;
        }
        pcVramKeepDevicePutBack(&sb->keptClip); /* the displayed buffer, over the CPU-kept tiles inside it */
        pcGpuQuiet(0);
        pcWideFramePutBack();
        vramBookkeepingPutBack();
        cpuTimeBetweenBytes(1, bytes);
        cpuTimeBetween(3, 0);
        hwClockHold(0);
    }
}
