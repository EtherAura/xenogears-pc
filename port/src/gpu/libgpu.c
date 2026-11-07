/*
 * LIBGPU on the PC: the drawing and display environments, VRAM transfers,
 * ordering tables and draw synchronisation, over the GP0 processor (gp0.c)
 * and the Vulkan device (vk.c).
 *
 * The PS1 queues these requests for the GPU and its DMA, which run beside the
 * CPU. Here each request is carried out when it is made: the GP0 packets are
 * read then (so the game may reuse the memory as soon as DrawSync says so) and
 * the device draws them in order. What the game can observe of the timing is
 * kept: DrawSync(1) reports an empty queue, and DrawSyncCallback's callback
 * runs as an interrupt at the next safe point after the drawing was issued.
 */

#include <SDL3/SDL.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "psyq.h"
#include "system/hw.h"
#include "system/cputime.h"

#include "gpu_internal.h"
#include "interpolation.h"
#include "../system/savestate.h"
#include "texture.h"
#include "portrait_pack.h"
#include "gte_pc.h"
#include "pc_geometry.h"

/* ---- logging -------------------------------------------------------------------- */

void gpuLog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void gpuWarnOnce(const char* key, const char* fmt, ...) {
    static const char* seen[64];
    static int seenCount;
    for (int i = 0; i < seenCount; i++) {
        if (strcmp(seen[i], key) == 0) {
            return;
        }
    }
    if (seenCount < 64) {
        seen[seenCount++] = key;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ---- the clock, until port/src/system provides it ------------------------------------ */

/* Weak stand-ins for hw.h so the GPU layer links on its own; port/src/system's
 * definitions replace them. Without them nothing is delivered later, so
 * DrawSyncCallback callbacks and the GPU's own vblanks do not happen. */
__attribute__((weak)) double hwNow(void) {
    return (double)SDL_GetTicksNS() * 1e-9;
}

__attribute__((weak)) void hwScheduleAt(double at, HwEvent fn, void* arg) {
    (void)at;
    (void)fn;
    (void)arg;
    gpuWarnOnce("hwScheduleAt", "gpu: no hwScheduleAt (port/src/system): events dropped");
}

__attribute__((weak)) void hwSafePoint(void) {
}

/* LIBETC's video mode; port/src/system may own these. */
static s32 videoMode;

__attribute__((weak)) s32 SetVideoMode(s32 mode) {
    s32 prev = videoMode;
    hwSafePoint();
    videoMode = mode;
    return prev;
}

__attribute__((weak)) s32 GetVideoMode(void) {
    return videoMode;
}

/* ---- state ---------------------------------------------------------------------------- */

static struct {
    int ready;
    int debug;
    DRAWENV drawEnv;
    DISPENV dispEnv;
    GpuDisplay display;
    void (*drawSyncCallback)(void);
    int drawPending;
    int callbackScheduled;
    uint32_t vblanks;
    int externalVBlank;
    double nextVBlank;
    /* Since the last vblank (XG_PRESENT_LOG): what the game handed over. */
    int otags, prims, dispEnvs;
    uint32_t lastDrawVBlank; /* the last vblank with a DrawOTag or DrawPrim before it */
} g;

/* gpu.h gpuSetCapture: game code run outside the game draws into this. */
static const GpuCapture* capture;

void gpuSetCapture(const GpuCapture* c) {
    interpolationReset();
    capture = c;
}

int gpuCapturing(void) { return capture != NULL; }

int pcGeometrySlivers(void) { return hiresSlivers(); }

int pcGeometryActive(void) {
    return capture == NULL && hiresPrecision();
}

static int nativePacket(const WidePacket* p) {
    return !p || p->geometry != PC_GEOMETRY_ONLY;
}

static volatile sig_atomic_t vramDumpRequested;

static void onUsr1(int sig) {
    (void)sig;
    vramDumpRequested = 1;
}

/* XG_GPU_TRACE=<file>: the ordering tables, primitives and VRAM transfers the
 * game sends, as tools/redux_gputrace.lua writes them ("O w..." per DrawOTag,
 * "P w..." per DrawPrim, words in hex; "L", "M", "C", "D", "S" per
 * LoadImage, MoveImage, ClearImage, PutDrawEnv, PutDispEnv), so a PC run can be diffed with a Redux trace;
 * gpuTraceNote adds marker lines (the field sweep's frames). With
 * XG_GPU_TRACE_VBLANKS=1 also "V n" at each vblank (what was issued between
 * two vblanks: work/menu60 tells a picture the game did not draw again from
 * one it drew the same). */
static FILE* traceFile;
static int traceChecked;
static int traceVBlanks;

static FILE* traceOut(void) {
    if (!traceChecked) {
        const char* path = getenv("XG_GPU_TRACE");
        const char* vb = getenv("XG_GPU_TRACE_VBLANKS");
        traceChecked = 1;
        traceVBlanks = vb != NULL && vb[0] == '1';
        if (path != NULL && path[0] != '\0') {
            traceFile = fopen(path, "w");
        }
    }
    return traceFile;
}

void gpuTraceNote(const char* text) {
    if (traceOut() != NULL) {
        fprintf(traceFile, "%s\n", text);
    }
}

static void traceWords(const uint32_t* w, int n) {
    for (int i = 0; i < n; i++) {
        fprintf(traceFile, " %08x", w[i]);
    }
}

/* ---- vertical blank -------------------------------------------------------------------- */

/* The GPU's own vblanks (only while hw.c does not drive them: the test programs). */
#define VBLANK_PERIOD_NTSC (HW_FIELD_LINES_NTSC * HW_LINE_CLOCKS_NTSC / HW_VIDEO_HZ_NTSC)

/* XG_PRESENT_LOG=1: one line per vblank, to tell a game that stopped drawing
 * (otags/prims 0, the same display) from a window that stopped showing what
 * it draws (acquire timeouts, failed presents). */
static void logVBlank(void) {
    GvkPresentStats st;
    gvkPresentStats(&st);
    gpuLog("present: vb %u otag %d prim %d disp %d (%d,%d %dx%d%s) | acquire %s %.2fms image %u present %s | "
           "%dx%d %s %d images, window %dx%d%s%s | shown %u skipped %u no-picture %d recovered %u",
           g.vblanks, g.otags, g.prims, g.dispEnvs, g.display.x, g.display.y, g.display.w, g.display.h,
           g.display.enabled ? "" : " off", st.acquireName, st.acquireMs, st.image, st.presentName, st.width, st.height,
           st.mode, st.images, st.windowW, st.windowH, st.stale ? " stale" : "",
           st.surface < 0 ? " surface-lost" : st.surface == 0 ? " no-surface" : "", st.shown, st.skipped, st.noPicture,
           st.recoveries);
}

static void vblank(void) {
    gtePrecisionNewFrame();
    g.vblanks++;
    if (traceOut() != NULL && traceVBlanks) {
        fprintf(traceFile, "V %u\n", g.vblanks);
    }
    cpuTimingVBlank(g.vblanks);
    vramStampClock(g.vblanks);
    textureDumpVBlank(g.vblanks);
    gp0ProbeVBlank(g.vblanks);
    vramOwnedVBlank();
    wideVBlank(&g.display);
    dumpVBlank(&g.display, g.vblanks);
    if (vramDumpRequested) {
        vramDumpRequested = 0;
        dumpVram(g.vblanks);
        gpuLog("gpu: SIGUSR1: VRAM at vblank %u written to %s/vram_%06u.png and .bin", g.vblanks, dumpDir(), g.vblanks);
    }
    CPU_TIME_BEGIN(CPU_PRESENT);
    gvkPresent(&g.display, g.vblanks);
    CPU_TIME_END(CPU_PRESENT);
    if (g.otags + g.prims > 0) {
        g.lastDrawVBlank = g.vblanks;
    }
    if (presentLogging()) {
        logVBlank();
    }
    g.otags = g.prims = g.dispEnvs = 0;
}

/* ---- stall reports (port/src/system/stall.c, on its own thread) ------------------------ */

void gpuDescribe(char* out, size_t size) {
    GvkPresentStats st;
    gvkPresentStats(&st);
    snprintf(out, size,
             "vblank %u (last drawing before vblank %u), display %d,%d %dx%d%s; presented %u, skipped %u, "
             "last acquire %s, last present %s, swapchain %dx%d %s%s",
             g.vblanks, g.lastDrawVBlank, g.display.x, g.display.y, g.display.w, g.display.h,
             g.display.enabled ? "" : " (off)", st.shown, st.skipped, st.acquireName, st.presentName, st.width,
             st.height, st.mode, st.surface < 0 ? ", surface lost" : "");
}

/* SIGUSR1 while the game thread is stuck (no vblank comes to serve it): VRAM
 * through the device's second queue (else the CPU copy, stale wherever the
 * device drew since the last readback) and the wide target's frame rows. */
int gpuServeStalledDump(void) {
    if (!g.ready || !vramDumpRequested) {
        return 0;
    }
    vramDumpRequested = 0;
    uint32_t vb = g.vblanks;
    char path[700];
    uint16_t* px = malloc((size_t)WIDE_W * VRAM_H * 2);
    if (px == NULL) {
        return 0;
    }
    int fresh = gvkRescueRead(0, px, 0, 0, VRAM_W, VRAM_H);
    if (!fresh) {
        memcpy(px, vramShadow, sizeof(vramShadow));
    }
    snprintf(path, sizeof(path), "%s/vram_%06u_stuck", dumpDir(), (unsigned)vb);
    dumpImage15(path, px, VRAM_W, VRAM_H, 1);
    gpuLog("gpu: SIGUSR1 with the game thread stuck: VRAM (%s) written to %s.png and .bin",
           fresh ? "read from the device" : "the CPU copy: what the device drew may be missing", path);
    if (fresh && gvkRescueRead(1, px, 0, 0, WIDE_W, VRAM_H)) {
        snprintf(path, sizeof(path), "%s/wide_%06u_stuck", dumpDir(), (unsigned)vb);
        dumpImage15(path, px, WIDE_W, VRAM_H, 0);
        gpuLog("gpu: the wide target's frame rows written to %s.png", path);
    }
    free(px);
    return 1;
}

void gpuVBlank(void) {
    if (!g.ready) {
        return;
    }
    g.externalVBlank = 1;
    vblank();
}

void gpuPresentMenu(const char* shot) {
    interpolationReset();
    if (g.ready) {
        gvkPresentMenu(&g.display, g.vblanks, shot);
    }
}

/* The GPU's own vblanks, while nobody calls gpuVBlank. */
static void selfVBlank(void* arg) {
    (void)arg;
    if (g.externalVBlank || !g.ready) {
        return;
    }
    vblank();
    double now = hwNow();
    g.nextVBlank += VBLANK_PERIOD_NTSC;
    if (g.nextVBlank < now - 0.25) {
        g.nextVBlank = now + VBLANK_PERIOD_NTSC; /* fell far behind: resynchronise */
    }
    hwScheduleAt(g.nextVBlank, selfVBlank, NULL);
}

/* ---- init ------------------------------------------------------------------------------------ */

int gpuInit(SDL_Window* window) {
    memset(&g, 0, sizeof(g));
    gp0Reset();
    wideReset();
    vramReset();
    textureCacheReset();
    textureGlyphReset();
    if (!gvkInit(window)) {
        gpuLog("gpu: Vulkan initialisation failed");
        return 0;
    }
    dumpInit();
    textureDumpInit();
    texturePackBeetleInit(); /* a chosen Beetle pack tracks uploads from the first one */
#ifdef SIGUSR1
    signal(SIGUSR1, onUsr1);
#else
    signal(SIGBREAK, onUsr1); /* Windows: Ctrl+Break in the console */
#endif
    g.ready = 1;
    g.nextVBlank = hwNow() + VBLANK_PERIOD_NTSC;
    hwScheduleAt(g.nextVBlank, selfVBlank, NULL);
    return 1;
}

void gpuShutdown(void) {
    textureDumpShutdown();
    texturePackShutdown();
    portraitPackShutdown();
    textureCelShutdown();
    textureGlyphReset();
    textureCacheReset();
    if (g.ready) {
        gvkShutdown();
    }
    interpolationShutdown();
    g.ready = 0;
}

/* ---- draw synchronisation ---------------------------------------------------------------- */

static void drawDone(void* arg) {
    (void)arg;
    g.callbackScheduled = 0;
    if (g.drawPending && g.drawSyncCallback != NULL) {
        g.drawPending = 0;
        g.drawSyncCallback();
    }
}

/* pcGpuQuiet(1): a picture the Enhanced engine draws and takes away again
 * (the 60 Hz Battling's picture between, between.c): no drawing pending
 * for the game's DrawSync callback, no event queued for it, and its VRAM
 * put back leaves the bookkeeping and the wide picture alone. */
static int drawQuiet;
void pcGpuQuiet(int on) {
    drawQuiet = on;
}

void gpuDrawIssued(void) {
    gpuHostWrite();
    if (!drawQuiet) g.drawPending = 1;
}

/* The end of a queued request: the "GPU" is idle again at once, so the
 * callback is due now and runs at the next safe point. */
static void requestDone(void) {
    if (!drawQuiet && g.drawSyncCallback != NULL && g.drawPending && !g.callbackScheduled) {
        g.callbackScheduled = 1;
        hwScheduleAt(hwNow(), drawDone, NULL);
    }
}

s32 DrawSync(s32 mode) {
    hwSafePoint();
    (void)mode;
    return 0;
}

void* DrawSyncCallback(void (*func)(void)) {
    hwSafePoint();
    void (*prev)(void) = g.drawSyncCallback;
    g.drawSyncCallback = func;
    return (void*)prev;
}

/* ---- reset and debug ------------------------------------------------------------------ */

s32 ResetGraph(s32 mode) {
    hwSafePoint();
    switch (mode & 7) {
    case 0:
    case 5:
        interpolationReset();
        /* GP1(00h): drawing state and display back to power-on. */
        gp0Flush();
        gp0Reset();
        g.display.enabled = 0;
        hwSetDisplayTiming(0, 10);
        /* fallthrough */
    case 3:
        g.drawSyncCallback = NULL;
        g.drawPending = 0;
        memset(&g.drawEnv, 0xFF, sizeof(g.drawEnv));
        memset(&g.dispEnv, 0xFF, sizeof(g.dispEnv));
        return 0;
    default:
        /* GP1(01h): drop what is in the command buffer (nothing is pending). */
        return 0;
    }
}

s32 SetGraphDebug(s32 level) {
    hwSafePoint();
    s32 prev = g.debug;
    g.debug = level;
    return prev;
}

s32 GetGraphDebug(void) {
    return g.debug;
}

s32 GetGraphType(void) {
    return 0;
}

/* ---- display ---------------------------------------------------------------------------- */

void SetDispMask(s32 mask) {
    hwSafePoint();
    if (mask == 0) {
        memset(&g.dispEnv, 0xFF, sizeof(g.dispEnv));
    }
    g.display.enabled = mask != 0;
}

static int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* What the TV shows for a display environment: LIBGPU's PutDispEnv turns
 * disp.w into a horizontal resolution and screen into display ranges (in GPU
 * clocks and lines); the picture is that many pixels from disp.x, disp.y. */
static void displayArea(const DISPENV* env, GpuDisplay* d) {
    int pal = GetVideoMode() != 0;
    int hs = env->screen.x * 10 + 0x260;
    int he = env->screen.w ? hs + env->screen.w * 10 : hs + 0xA00;
    int vs = env->screen.y + (pal ? 0x13 : 0x10);
    int ve = env->screen.h ? vs + env->screen.h : vs + 0xF0;
    hs = clampInt(hs, 500, 3290);
    he = clampInt(he, hs + 80, 3290);
    vs = clampInt(vs, 16, pal ? 310 : 256);
    ve = clampInt(ve, vs + 2, pal ? 312 : 258);
    int clocks; /* GPU clocks per pixel */
    if (env->disp.w <= 280) {
        clocks = 10;
    } else if (env->disp.w <= 352) {
        clocks = 8;
    } else if (env->disp.w <= 400) {
        clocks = 7;
    } else if (env->disp.w <= 560) {
        clocks = 5;
    } else {
        clocks = 4;
    }
    int interlaced = env->disp.h > (pal ? 288 : 256);
    d->x = env->disp.x & (VRAM_W - 1);
    d->y = env->disp.y & (VRAM_H - 1);
    d->w = ((he - hs) / clocks + 2) & ~3;
    d->h = (ve - vs) * (interlaced ? 2 : 1);
    d->rgb24 = env->isrgb24 != 0;
    d->tvClock = hs - 0x260;
    d->tvLine = vs - (pal ? 0x13 : 0x10);
    d->clocks = clocks;
    d->rows = interlaced ? 2 : 1;
    d->frameLines = pal ? 288 : 240;
}

DISPENV* PutDispEnv(DISPENV* env) {
    hwSafePoint();
    if (capture != NULL) {
        return env;
    }
    g.dispEnvs++;
    if (traceOut() != NULL) {
        fprintf(traceFile, "S %d %d %d %d %d %d %d %d %d %d\n", env->disp.x, env->disp.y, env->disp.w, env->disp.h,
                env->screen.x, env->screen.y, env->screen.w, env->screen.h, env->isinter, env->isrgb24);
    }
    displayArea(env, &g.display);
    hwSetDisplayTiming(env->isinter || g.display.rows == 2, g.display.clocks); /* GP1(08h) */
    memcpy(&g.dispEnv, env, sizeof(DISPENV));
    return env;
}

const GpuDisplay* gpuDisplay(void) {
    return &g.display;
}

DISPENV* GetDispEnv(DISPENV* env) {
    memcpy(env, &g.dispEnv, sizeof(DISPENV));
    return env;
}

DISPENV* SetDefDispEnv(DISPENV* env, s32 x, s32 y, s32 w, s32 h) {
    hwSafePoint();
    env->disp.x = (s16)x;
    env->disp.y = (s16)y;
    env->disp.w = (s16)w;
    env->disp.h = (s16)h;
    env->screen.x = 0;
    env->screen.y = 0;
    env->screen.w = 0;
    env->screen.h = 0;
    env->isinter = 0;
    env->isrgb24 = 0;
    env->pad0 = 0;
    env->pad1 = 0;
    return env;
}

/* ---- drawing environment ------------------------------------------------------------------- */

static uint32_t modeWord(s32 dfe, s32 dtd, u16 tpage) {
    return 0xE1000000u | (dtd ? 0x200u : 0) | (dfe ? 0x400u : 0) | (tpage & 0x9FFu);
}

static uint32_t areaWord(uint32_t code, int x, int y) {
    x = clampInt(x, 0, VRAM_W - 1);
    y = clampInt(y, 0, VRAM_H - 1);
    return code | ((uint32_t)y & 0x3FF) << 10 | ((uint32_t)x & 0x3FF);
}

static uint32_t offsetWord(int x, int y) {
    return 0xE5000000u | ((uint32_t)y & 0x7FF) << 11 | ((uint32_t)x & 0x7FF);
}

static uint32_t texWindowWord(const RECT* tw) {
    if (tw == NULL) {
        return 0;
    }
    uint32_t mx = (uint32_t)(-tw->w & 0xFF) >> 3;
    uint32_t my = (uint32_t)(-tw->h & 0xFF) >> 3;
    uint32_t ox = (uint32_t)(tw->x & 0xFF) >> 3;
    uint32_t oy = (uint32_t)(tw->y & 0xFF) >> 3;
    return 0xE2000000u | oy << 15 | ox << 10 | my << 5 | mx;
}

/* The words of a DR_ENV for env (LIBGPU's SetDrawEnv); returns the count. With
 * `fill`, the background goes through the fill command when it can (PutDrawEnv)
 * rather than as a rectangle (SetDrawEnv). */
static int drawEnvWords(uint32_t* code, const DRAWENV* env, int fill) {
    code[0] = areaWord(0xE3000000u, env->clip.x, env->clip.y);
    code[1] = areaWord(0xE4000000u, env->clip.x + env->clip.w - 1, env->clip.y + env->clip.h - 1);
    code[2] = offsetWord(env->ofs[0], env->ofs[1]);
    code[3] = modeWord(env->dfe, env->dtd, env->tpage);
    code[4] = texWindowWord(&env->tw);
    code[5] = 0xE6000000u;
    int n = 6;
    if (env->isbg) {
        int x = env->clip.x;
        int y = env->clip.y;
        int w = clampInt(env->clip.w, 0, VRAM_W - 1);
        int h = clampInt(env->clip.h, 0, VRAM_H - 1);
        uint32_t rgb = (uint32_t)env->b0 << 16 | (uint32_t)env->g0 << 8 | env->r0;
        if (!fill || (x & 0x3F) || (w & 0x3F)) {
            x -= env->ofs[0];
            y -= env->ofs[1];
            code[n++] = 0x60000000u | rgb;
        } else {
            code[n++] = 0x02000000u | rgb;
        }
        code[n++] = ((uint32_t)(u16)y << 16) | (u16)x;
        code[n++] = ((uint32_t)(u16)h << 16) | (u16)w;
    }
    return n;
}

void SetDrawEnv(void* dr, DRAWENV* env) {
    hwSafePoint();
    DR_ENV* p = dr;
    int n = drawEnvWords(p->code, env, 0);
    setlen(p, n);
}

DRAWENV* PutDrawEnv(DRAWENV* env) {
    hwSafePoint();
    if (capture != NULL) {
        int n = drawEnvWords(env->dr_env.code, env, 1);
        setlen(&env->dr_env, n);
        TermPrim(&env->dr_env);
        capture->gp0(capture->user, env->dr_env.code, n);
        return env;
    }
    if (traceOut() != NULL) {
        fprintf(traceFile, "D %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", env->clip.x, env->clip.y,
                env->clip.w, env->clip.h, env->ofs[0], env->ofs[1], env->tw.x, env->tw.y, env->tw.w, env->tw.h,
                env->tpage, env->dtd, env->dfe, env->isbg, env->r0, env->g0, env->b0);
    }
    int n = drawEnvWords(env->dr_env.code, env, 1);
    setlen(&env->dr_env, n);
    TermPrim(&env->dr_env);
    gp0Write(env->dr_env.code, n);
    memcpy(&g.drawEnv, env, sizeof(DRAWENV));
    requestDone();
    return env;
}

DRAWENV* GetDrawEnv(DRAWENV* env) {
    hwSafePoint();
    memcpy(env, &g.drawEnv, sizeof(DRAWENV));
    return env;
}

DRAWENV* SetDefDrawEnv(DRAWENV* env, s32 x, s32 y, s32 w, s32 h) {
    hwSafePoint();
    env->clip.x = (s16)x;
    env->clip.y = (s16)y;
    env->clip.w = (s16)w;
    env->clip.h = (s16)h;
    env->tw.x = env->tw.y = env->tw.w = env->tw.h = 0;
    env->r0 = env->g0 = env->b0 = 0;
    env->dtd = 1;
    env->dfe = h < (GetVideoMode() ? 289 : 257);
    env->ofs[0] = (s16)x;
    env->ofs[1] = (s16)y;
    env->tpage = 10;
    env->isbg = 0;
    return env;
}

/* ---- ordering tables ----------------------------------------------------------------- */

/* The packet every ordering table ends with (LIBGPU's terminator): no words,
 * and the end-of-list link. */
static OTag terminator = OT_LINK_MASK;

static int isEnd(OTag tag) {
    return (tag & OT_LINK_MASK) == OT_LINK_MASK;
}

/* Feed a linked list of packets to GP0, as the DMA's linked-list mode does. */
static void walk(const OTag* p) {
    long guard = 0;
    if (traceOut() != NULL) {
        fputc('O', traceFile);
    }
    while (p != NULL) {
        OTag tag = *p;
        int len = (int)(tag >> 56);
        if (len != 0) {
            const WidePacket* wp = widePacket(p);
            if (traceFile != NULL && nativePacket(wp)) {
                traceWords((const uint32_t*)(p + 1), len);
            }
            /* pcWidePrim's treatment of this packet, for its wide copy. */
            gp0SetPacket(wp);
            interpolationPacket(p);
            gp0Write((const uint32_t*)(p + 1), len);
            if (wp != NULL) {
                gp0SetPacket(NULL);
            }
        }
        if (isEnd(tag)) {
            break;
        }
        p = (const OTag*)OT_NEXT(tag);
        if (++guard > 4000000) {
            gpuLog("gpu: ordering table does not end (loop?)");
            break;
        }
    }
    if (traceFile != NULL) {
        fputc('\n', traceFile);
    }
    if (p == NULL) {
        gpuWarnOnce("nullot", "gpu: ordering table links to NULL");
    }
}

OTag* ClearOTagR(OTag* ot, s32 n) {
    hwSafePoint();
    for (s32 i = n - 1; i > 0; i--) {
        ot[i] = OT_LINK(&ot[i - 1]);
    }
    ot[0] = OT_LINK(&terminator);
    return ot;
}

OTag* ClearOTag(OTag* ot, s32 n) {
    hwSafePoint();
    for (s32 i = 0; i < n - 1; i++) {
        ot[i] = OT_LINK(&ot[i + 1]);
    }
    ot[n - 1] = OT_LINK(&terminator);
    return ot;
}

/* The same walk into a capture (gpuSetCapture): no trace, no wide packets. */
static void walkCaptured(const OTag* p) {
    for (long guard = 0; p != NULL && guard < 4000000; guard++) {
        OTag tag = *p;
        int len = (int)(tag >> 56);
        if (len != 0 && nativePacket(widePacket(p))) {
            capture->gp0(capture->user, (const uint32_t*)(p + 1), len);
        }
        if (isEnd(tag)) {
            break;
        }
        p = (const OTag*)OT_NEXT(tag);
    }
}

void DrawOTag(OTag* p) {
    hwSafePoint();
    if (capture != NULL) {
        walkCaptured(p);
        return;
    }
    g.otags++;
    walk(p);
    widePacketsDone();
    interpolationPacketsDone();
    requestDone();
}

void DrawOTagEnv(void* ot, DRAWENV* env) {
    hwSafePoint();
    int n = drawEnvWords(env->dr_env.code, env, 1);
    setlen(&env->dr_env, n);
    setaddr(&env->dr_env, ot);
    if (capture != NULL) {
        walkCaptured(&env->dr_env.tag);
        return;
    }
    g.otags++;
    walk(&env->dr_env.tag);
    widePacketsDone();
    interpolationPacketsDone();
    memcpy(&g.drawEnv, env, sizeof(DRAWENV));
    requestDone();
}

void DrawPrim(void* p) {
    hwSafePoint();
    const WidePacket* wp = widePacket(p);
    if (capture != NULL) {
        if (nativePacket(wp)) capture->gp0(capture->user, (const uint32_t*)((OTag*)p + 1), ((P_TAG*)p)->len);
        return;
    }
    g.prims++;
    if (traceOut() != NULL && nativePacket(wp)) {
        fputc('P', traceFile);
        traceWords((const uint32_t*)((OTag*)p + 1), ((P_TAG*)p)->len);
        fputc('\n', traceFile);
    }
    gp0SetPacket(wp);
    interpolationPacket(p);
    gp0Write((const uint32_t*)((OTag*)p + 1), ((P_TAG*)p)->len);
    interpolationPacket(NULL);
    if (wp != NULL) {
        gp0SetPacket(NULL);
    }
}

/* ---- VRAM transfers ---------------------------------------------------------------------- */

/* Inside gpuMove: its own wide copy follows (wideMove). */
static int moving;

static int loadNoWide; /* pcVramPutBack: the wide picture and the bookkeeping left alone */
void gpuLoad(int x, int y, int w, int h, const uint16_t* src) {
    VPiece pieces[4];
    int n = vramWrap(x, y, w, h, pieces);
    /* A Beetle PSX HW pack's upload names (texture_beetle.c): a put back is the pixels as they were */
    if (!moving && !loadNoWide) beetleTrackLoad(x, y, w, h, src);
    for (int i = 0; i < n; i++) interpolationTransfer(pieces[i].r);
    int setMask = gp0SetMask();
    int checkMask = gp0CheckMask();
    gp0Flush();
    for (int i = 0; i < n; i++) vramKeepWrite(pieces[i].r); /* a picture between's keep (vram.c) */
    /* A CPU-to-VRAM transfer empties the texture cache, wherever it lands
     * (measured black-box, gp0.c's cache notes). */
    gp0InvalidateTexture();
    /* A put back (loadNoWide) keeps its marks: no provenance to classify. */
    int rendered = !moving && !loadNoWide && vramLoadRendered(x, y, w, h, src, setMask, checkMask);
    for (int i = 0; i < n; i++) {
        VPiece* pc = &pieces[i];
        if (checkMask) {
            vramSyncRect(pc->r);
        }
        /* The hi-res copy's side (hires.c): compared with the old picture
         * before the CPU copy takes the new one. Inside a move, gpuMove's. */
        int hi = moving ? -1 : loadNoWide ? hiresPutBackPlan(pc->r)
                                          : hiresLoadPlan(pc->r, src + (size_t)pc->dy * w + pc->dx, w, setMask, checkMask);
        for (int row = pc->r.y0; row <= pc->r.y1; row++) {
            const uint16_t* s = src + (size_t)(row - pc->r.y0 + pc->dy) * w + pc->dx;
            uint16_t* d = &vramShadow[row][pc->r.x0];
            int cols = pc->r.x1 - pc->r.x0 + 1;
            if (!setMask && !checkMask) {
                memcpy(d, s, (size_t)cols * 2);
                continue;
            }
            for (int c = 0; c < cols; c++) {
                if (!(checkMask && (d[c] & 0x8000))) {
                    d[c] = s[c] | (setMask ? 0x8000 : 0);
                }
            }
        }
        hiresLoadApply(pc->r, hi);
        if (!moving && !loadNoWide) { /* a put back of the pixels as they were: their marks stay */
            if (rendered) vramMarkCopied(pc->r, 1);
            else vramMarkUploaded(pc->r, checkMask);
        }
        gvkUpload(pc->r, &vramShadow[pc->r.y0][pc->r.x0], VRAM_W);
        if (!checkMask) vramResolveCpuWrite(pc->r);
        if (!moving && !loadNoWide) {
            wideLoad(pc->r);
        }
    }
    gpuDrawIssued();
}

static int moveCopy(int sx, int sy, int dx, int dy, int w, int h);

void gpuMove(int sx, int sy, int dx, int dy, int w, int h) {
    int setMask = gp0SetMask();
    int checkMask = gp0CheckMask();
    beetleTrackMove(sx, sy, dx, dy, w, h); /* a copy takes its source's upload names (texture_beetle.c) */
    /* So does a VRAM-to-VRAM copy (measured black-box, gp0.c). */
    gp0InvalidateTexture();
    interpolationCopy(0,sx,sy,dx,dy,w,h);
    VPiece provenance[4];
    int pn = vramWrap(sx, sy, w, h, provenance);
    TileSet sources = {0};
    for (int i = 0; i < pn; ++i) tilesAdd(&sources, provenance[i].r);
    int rendered = checkMask || vramWasRendered(&sources);
    if (vramKeeping()) { /* a picture between's keep (vram.c): the destination before the copy */
        VPiece kd[4];
        int nk = vramWrap(dx, dy, w, h, kd);
        for (int i = 0; i < nk; ++i) vramKeepWrite(kd[i].r);
    }
    moving = 1;
    int deviceDrawn = moveCopy(sx, sy, dx, dy, w, h);
    moving = 0;
    hiresMove(sx, sy, dx, dy, w, h, setMask, checkMask);
    /* The wide picture's part of the move (screen captures, menus over a
     * captured field). */
    VPiece src[4], dst[4];
    int ns = vramWrap(sx, sy, w, h, src);
    int nd = vramWrap(dx, dy, w, h, dst);
    for (int i = 0; i < nd; i++) interpolationTransfer(dst[i].r);
    /* The device path already marked its destination drawn. Count each
     * logical move once regardless of residency (which changes on load). */
    if (!deviceDrawn) for (int i = 0; i < nd; ++i) {
        vramMarkCopied(dst[i].r, rendered);
        vramResolveCpuWrite(dst[i].r);
    }
    if (ns == 1 && nd == 1) {
        wideMove(src[0].r, dst[0].r);
    } else {
        for (int i = 0; i < nd; i++) {
            wideLoad(dst[i].r);
        }
    }
}

/* A copy in the PS1's order (gpu_internal.h, COPY_PIECE): row by row, each
 * row in pieces that read their whole source before they write. */
static int moveCopy(int sx, int sy, int dx, int dy, int w, int h) {
    VPiece src[4], dst[4];
    int ns = vramWrap(sx, sy, w, h, src);
    int nd = vramWrap(dx, dy, w, h, dst);
    int setMask = gp0SetMask();
    int checkMask = gp0CheckMask();
    int srcDrawn = 0;
    gp0Flush();
    gpuDrawIssued();
    for (int i = 0; i < ns; i++) {
        srcDrawn |= vramIsDrawn(src[i].r);
    }
    int meet = vramCopyMeets(src, ns, dst, nd);
    if (setMask || checkMask || !srcDrawn) {
        /* On the CPU copy (brought up to date first where the mask bits need
         * it), then to the device. */
        if (setMask || checkMask) {
            for (int i = 0; i < ns; i++) {
                vramSyncRect(src[i].r);
            }
            for (int i = 0; i < nd; i++) {
                vramSyncRect(dst[i].r);
            }
        }
        for (int r = 0; r < h; r++) {
            const uint16_t* from = vramShadow[(sy + r) & (VRAM_H - 1)];
            uint16_t* to = vramShadow[(dy + r) & (VRAM_H - 1)];
            for (int x0 = 0; x0 < w; x0 += COPY_PIECE) {
                int n = w - x0 < COPY_PIECE ? w - x0 : COPY_PIECE;
                uint16_t piece[COPY_PIECE];
                for (int i = 0; i < n; i++) {
                    piece[i] = from[(sx + x0 + i) & (VRAM_W - 1)];
                }
                for (int i = 0; i < n; i++) {
                    uint16_t* d = &to[(dx + x0 + i) & (VRAM_W - 1)];
                    if (!(checkMask && (*d & 0x8000))) {
                        *d = piece[i] | (setMask ? 0x8000 : 0);
                    }
                }
            }
        }
        for (int i = 0; i < nd; i++) {
            gvkUpload(dst[i].r, &vramShadow[dst[i].r.y0][dst[i].r.x0], VRAM_W);
        }
        return 0;
    }
    /* The source has drawing the CPU copy has not seen: copy on the device,
     * through the scratch image; where source and destination meet, in the
     * steps that keep the PS1's order (vramCopySteps). */
    static uint16_t step[VRAM_H * (VRAM_W / COPY_PIECE)];
    static VRect rects[VRAM_H * (VRAM_W / COPY_PIECE)];
    int steps = meet ? vramCopySteps(sx, sy, dx, dy, w, h, step) : 1;
    for (int s = 0; s < steps; s++) {
        int nr = 1;
        rects[0] = (VRect){0, 0, w - 1, h - 1};
        if (meet) {
            nr = vramCopyStepRects(step, w, h, s, rects);
        }
        for (int pass = 0; pass < 2; pass++) {
            for (int k = 0; k < nr; k++) {
                VRect r = rects[k];
                VPiece p[4];
                int np = vramWrap((pass ? dx : sx) + r.x0, (pass ? dy : sy) + r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1, p);
                for (int i = 0; i < np; i++) {
                    if (pass == 0) {
                        gvkCopyToScratch(p[i].r, r.x0 + p[i].dx, r.y0 + p[i].dy);
                    } else {
                        gvkCopyFromScratch(r.x0 + p[i].dx, r.y0 + p[i].dy, p[i].r);
                    }
                }
            }
        }
    }
    for (int i = 0; i < nd; i++) {
        vramMarkDrawn(dst[i].r);
    }
    return 1;
}

void gpuFill(int x, int y, int w, int h, uint32_t rgb) {
    VPiece pieces[4];
    int n = vramWrap(x, y, w, h, pieces);
    uint16_t c = (uint16_t)(((rgb >> 3) & 0x1F) | ((rgb >> 11) & 0x1F) << 5 | ((rgb >> 19) & 0x1F) << 10);
    beetleTrackFill(x, y, w, h); /* a fill lets go of the upload names (texture_beetle.c) */
    for (int i = 0; i < n; i++) {
        VRect r = pieces[i].r;
        vramKeepWrite(r); /* a picture between's keep (vram.c) */
        gp0VramChanging(r);
        for (int row = r.y0; row <= r.y1; row++) {
            for (int col = r.x0; col <= r.x1; col++) {
                vramShadow[row][col] = c;
            }
        }
        gp0Fill(r, rgb);
        vramMarkCopied(r, 1);
        vramResolveCpuWrite(r);
    }
}

/* LIBGPU clamps the caller's rectangle in place (w to VRAM width, h to
 * height) before a transfer. */
static int clampRect(RECT* r, int wMax, int hMax) {
    r->w = (s16)clampInt(r->w, 0, wMax);
    r->h = (s16)clampInt(r->h, 0, hMax);
    return r->w > 0 && r->h > 0;
}

/* port/src/game/main/text_image.c: what the menus' text images upload (the
 * GPU's test programs, which have no game, define it empty: test/testclock.c). */
void pcTextImageUploaded(const void* data, s32 bytes);

/* The 60 Hz Battling's VRAM put back (port/src/game/battling/between.c):
 * the pixels the keep took (vram.c vramKeepChanged: the changed tiles, as
 * runs), written straight into VRAM, without the upload's hooks (the text
 * packs' scan of every upload, the trace, the heap request) and without a
 * safe point: PC-only work, no time. The wide picture is left alone too: a
 * load over the frame buffers would read as a CPU picture and replace
 * their wide copies with the 4:3 pixels (the pending native frame's HUD
 * back at its 4:3 places); the one buffer the picture between drew into is
 * kept and put back by its caller (wide.c pcWideFrameKeep/PutBack). The
 * hi-res copy keeps its detail where the colour agrees (hiresPutBackPlan:
 * what a load that mostly agrees gets, without the count). */
void pcVramPutBack(RECT* rect, void* data) {
    if (clampRect(rect, VRAM_W, VRAM_H)) {
        gp0ClearCache();
        loadNoWide = 1;
        gpuLoad(rect->x, rect->y, rect->w, rect->h, data);
        loadNoWide = 0;
    }
}

/* The buffer on display kept on the device across the picture between
 * (between.c): its native rectangle and the hi-res copy's picture of it go
 * to images of their own (vk.c gvkKeepFrame: no readback, no wait) and come
 * back after the wait with the copy's detail marks (hires.c), exactly; the
 * between's draws into the tiles wholly inside need no CPU keep (vram.c
 * vramKeepDraw), while its uploads, fills and CPU draws, which write the CPU
 * copy, keep their tiles on the CPU wherever they are, and go back through
 * pcVramPutBack first (the device's put back comes over them). 0 when the
 * device has no room for the images: the caller keeps the pixels on the
 * CPU (vramKeepRect) as before. */
int pcVramKeepDevice(RECT* rect) {
    RECT r = *rect;
    if (!clampRect(&r, VRAM_W, VRAM_H)) return 0;
    VRect v = {r.x, r.y, r.x + r.w - 1, r.y + r.h - 1};
    if (v.x0 < 0 || v.y0 < 0 || v.x1 > VRAM_W - 1 || v.y1 > VRAM_H - 1) return 0;
    /* whole tiles (the CPU keep's unit): a buffer of 218 lines ends inside
     * a tile row, which the draws would otherwise keep with a readback */
    v.x0 &= ~15;
    v.y0 &= ~15;
    v.x1 |= 15;
    v.y1 |= 15;
    gp0Flush();
    if (!gvkKeepFrame(v)) return 0;
    hiresDetailKeep(v);
    vramKeepDeviceRect(v);
    return 1;
}

/* After the CPU-kept tiles went back (vram.c vramKeepEnd first): the
 * device's pixels and the detail marks; where the picture between changed
 * a tile there, the texture cache cleared and the interpolation told of the
 * transfer, as the CPU put back of those tiles did (pcVramPutBack). */
void pcVramKeepDevicePutBack(RECT* rect) {
    RECT r = *rect;
    gp0Flush();
    if (vramKeepDeviceChanged()) {
        gp0ClearCache();
        if (clampRect(&r, VRAM_W, VRAM_H)) interpolationTransfer((VRect){r.x & ~15, r.y & ~15, (r.x + r.w - 1) | 15, (r.y + r.h - 1) | 15});
    }
    gvkKeepFramePutBack();
    hiresDetailPutBack();
}

void LoadImage(RECT* rect, void* data) {
    hwSafePoint();
    if (capture != NULL) {
        if (clampRect(rect, VRAM_W, VRAM_H)) {
            capture->load(capture->user, rect->x, rect->y, rect->w, rect->h, data);
        }
        return;
    }
    if (rect->w > 0 && rect->h > 0) {
        pcTextImageUploaded(data, rect->w * rect->h * 2);
    }
    if (traceOut() != NULL) {
        int cw = rect->w < 0 ? 0 : rect->w > 1024 ? 1024 : rect->w;
        int ch = rect->h < 0 ? 0 : rect->h > 512 ? 512 : rect->h;
        fprintf(traceFile, "L %d %d %d %d", rect->x, rect->y, rect->w, rect->h);
        traceWords(data, (cw * ch + 1) / 2);
        fputc('\n', traceFile);
    }
    if (clampRect(rect, VRAM_W, VRAM_H)) {
        gp0ClearCache(); /* retail _dws emits GP0(01h) before the upload */
        gpuLoad(rect->x, rect->y, rect->w, rect->h, data);
        textureGlyphUpload(rect->x, rect->y, rect->w, rect->h, data);
    }
    requestDone();
}

s32 StoreImage(RECT* rect, u32* data) {
    hwSafePoint();
    if (!clampRect(rect, VRAM_W, VRAM_H)) {
        return -1;
    }
    if (capture != NULL) {
        capture->store(capture->user, rect->x, rect->y, rect->w, rect->h, (uint16_t*)data);
        return 0;
    }
    gp0ClearCache(); /* retail _drs emits GP0(01h) before the read */
    VPiece pieces[4];
    int n = vramWrap(rect->x, rect->y, rect->w, rect->h, pieces);
    int w = rect->w;
    uint16_t* out = (uint16_t*)data;
    for (int i = 0; i < n; i++) {
        vramSyncRect(pieces[i].r);
    }
    for (int i = 0; i < n; i++) {
        VPiece* pc = &pieces[i];
        for (int row = pc->r.y0; row <= pc->r.y1; row++) {
            memcpy(out + (size_t)(row - pc->r.y0 + pc->dy) * w + pc->dx, &vramShadow[row][pc->r.x0],
                   (size_t)(pc->r.x1 - pc->r.x0 + 1) * 2);
        }
        if (!drawQuiet) hiresStore(pc->r); /* the hi-res copy keeps a stash (round trips) */
    }
    if (!drawQuiet) vramStoreCapture(rect->x, rect->y, rect->w, rect->h, out); /* the Battling between's keep: no capture of the frame's */
    requestDone();
    return 0;
}

s32 MoveImage(RECT* rect, s32 x, s32 y) {
    hwSafePoint();
    if (traceOut() != NULL) {
        fprintf(traceFile, "M %d %d %d %d %d %d\n", rect->x, rect->y, rect->w, rect->h, (int)x, (int)y);
    }
    if (rect->w == 0 || rect->h == 0) {
        return -1;
    }
    if (capture != NULL) {
        capture->move(capture->user, rect->x & 0x3FF, rect->y & 0x1FF, x & 0x3FF, y & 0x1FF,
                      ((rect->w - 1) & 0x3FF) + 1, ((rect->h - 1) & 0x1FF) + 1);
        return 0;
    }
    /* GP0(80h) sizes count from 1 and wrap like the hardware's. */
    gpuMove(rect->x & 0x3FF, rect->y & 0x1FF, x & 0x3FF, y & 0x1FF, ((rect->w - 1) & 0x3FF) + 1,
            ((rect->h - 1) & 0x1FF) + 1);
    requestDone();
    return 0;
}

void ClearImage(RECT* rect, s32 r, s32 g_, s32 b) {
    hwSafePoint();
    if (capture != NULL) {
        if (clampRect(rect, VRAM_W - 1, VRAM_H - 1)) {
            capture->fill(capture->user, rect->x, rect->y, rect->w, rect->h,
                          (uint32_t)(b & 0xFF) << 16 | (uint32_t)(g_ & 0xFF) << 8 | (uint32_t)(r & 0xFF));
        }
        return;
    }
    if (traceOut() != NULL) {
        fprintf(traceFile, "C %d %d %d %d %d %d %d\n", rect->x, rect->y, rect->w, rect->h, (int)(r & 255), (int)(g_ & 255),
                (int)(b & 255));
    }
    if (clampRect(rect, VRAM_W - 1, VRAM_H - 1)) {
        uint32_t rgb = (uint32_t)(b & 0xFF) << 16 | (uint32_t)(g_ & 0xFF) << 8 | (uint32_t)(r & 0xFF);
        gp0ClearImage(rect->x, rect->y, rect->w, rect->h, rgb);
    }
    /* LIBGPU's clear packet leaves the mask settings at 0. */
    gp0SetMaskBits(0);
    requestDone();
}

/* ---- dumps ---------------------------------------------------------------------------------- */

void gpuDumpVram(const char* path) {
    VRect all = {0, 0, VRAM_W - 1, VRAM_H - 1};
    vramSyncRect(all);
    dumpImage15(path, &vramShadow[0][0], VRAM_W, VRAM_H, 1);
}

/* ---- save states (system/savestate.h) --------------------------------------------------- */

/* The game's environments, the display it set, the DrawSync callback and
 * whether one is owed (a drawDone may be queued), the vblank count dumps
 * are numbered by. ready, externalVBlank and nextVBlank are this process's. */
void gpuStateIo(XgState* s) {
    XG_STATE_VAR(s, g.debug);
    XG_STATE_VAR(s, g.drawEnv);
    XG_STATE_VAR(s, g.dispEnv);
    XG_STATE_VAR(s, g.display);
    XG_STATE_VAR(s, g.drawSyncCallback);
    XG_STATE_VAR(s, g.drawPending);
    XG_STATE_VAR(s, g.callbackScheduled);
    XG_STATE_VAR(s, g.vblanks);
    XG_STATE_VAR(s, g.lastDrawVBlank);
}

/* After a load, the device side from the restored CPU side: VRAM uploaded
 * whole (the CPU copy is exact: a state is taken after a full readback),
 * the texture caches emptied (their keys are the old timeline's), the
 * hi-res copy refilled. The wide target's pixels come back in wideStateIo,
 * a pack movie's place in moviePackStateIo. */
void gpuStateLoaded(void) {
    interpolationReset();
    if (!g.ready) {
        return;
    }
    VRect all = {0, 0, VRAM_W - 1, VRAM_H - 1};
    gp0Flush();
    gvkUpload(all, &vramShadow[0][0], VRAM_W);
    gvkSubmit();
    textureCacheReset();
    textureGlyphReset();
    beetleTrackStateLoaded();
    hiresStateLoaded();
}
