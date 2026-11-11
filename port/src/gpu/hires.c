/*
 * The hidden hi-res copy (docs/PORT_UPSCALE.md; the user's decisions in bd
 * xg-htn.33): its settings, its N, and what it does with transfers. vk.c
 * keeps the copy's images and passes, gp0.c gives it each primitive's
 * triangles.
 *
 * The exact 1x VRAM stays the game's: StoreImage, CLUTs and 4- and 8-bit
 * texels come from it, and frame dumps, widecheck and the Redux comparisons
 * see it unchanged. The copy only ever receives: every draw, fill, copy and
 * upload, N times larger, and the window shows it.
 *
 * Settings (settings.ini; XG_<KEY> overrides, port/src/system/settings.h):
 *   upscale=1          PS1 resolution (default): no copy unless precision
 *                      is explicitly enabled
 *   upscale=auto       N from the window: the picture's height in window
 *                      pixels per PS1 line (868 / 216 = 4 in a 1868x965
 *                      window at 16:9); 1 means the exact path. At most 4 on
 *                      a GPU without fragment shader interlock (each overlap
 *                      ends a batch there)
 *   upscale_gpu_ms=0   optional Auto GPU budget, off by default; 1..50 ms
 *                      per video frame. Fixed resolutions are unaffected.
 *   upscale=2..12      N fixed
 *   geometry_precision=0      PS1 integer vertices; 1 keeps
 *                             subpixel projected vertices, including at 1x
 *                             defaults off at 1x, on with upscaling
 *   geometry_seam_guard=1     small opaque perimeter overlap with precision
 *                             (projected 3D surfaces; sprites, HUD and text
 *                             keep their exact silhouette)
 *   perspective_textures=1    projective UVs on precise primitives; 0 affine
 *   upscale_color=true        true colour (default): no dither (shading is
 *                             smooth, as bright as the PS1's dither pattern),
 *                             blending kept in 8 bits; what each primitive
 *                             adds is the PS1's own 5-bit colour, so a glow
 *                             the PS1 drops stays dropped (no raised blacks)
 *   upscale_color=15dither1x  15-bit, the PS1's dither pattern at its own
 *                             pixel size (each N x N block one entry)
 *   upscale_color=15ditherhi  15-bit, dithered at the copy's pixel size
 *   upscale_color=raw         raw 8-bit colour: every colour in 8 bits, no
 *                             dither; dark areas come out brighter than the
 *                             PS1's (the dim glow it drops adds up)
 * N is also kept below what the device can hold (its largest image, a share
 * of its memory); a window being resized settles for half a second first.
 *
 * Transfers the CPU makes (StoreImage, change, LoadImage): an upload whose
 * 1x colours (bits 0-14) are, for at least 90% of its pixels, what VRAM has
 * there already keeps the copy's pixels where they are equal and takes only
 * bit 15 (the field's transition strips); one whose colours match a
 * StoreImage of the same size taken where the copy had drawn (the stash)
 * takes that StoreImage's hi-res pixels (the battle start's capture of the
 * frame, loaded into texture space). Everything else, and every pixel whose
 * colour changed, is replicated N x N. The optional upscale_cpu_effects=1
 * supplies an explicit display-only twin of the Gear arena pause blur.
 *
 * Env: XG_DUMP_HIRES=1 with XG_DUMP_FRAMES: beside each frame_<vblank>.png
 * the copy's display area as hires_<vblank>.png, and with a wide picture its
 * copy as hireswide_<vblank>.png (tools/hirescheck.py compares them);
 * XG_DUMP_HIRES=<lo-hi[/n],...>: only inside those vblank ranges (with /n
 * at least n vblanks apart).
 * XG_GEOMETRY_PROBE_CLEAR=1: magenta display-copy fills for crack sweeps;
 * the 1x image and game state are untouched.
 * XG_GEOMETRY_PROBE_SOLID=1: opaque display-copy triangles, so transparent
 * texture borders cannot be mistaken for cracks.
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_internal.h"
#include "interpolation.h"
#include "autores.h"
#include "gte_pc.h"
#include "system/settings.h"
#include "system/engine.h"
#include "pc_effect.h"

/* system/battlespeed.c: the game speed now, 1 outside battles and while a
 * battle holds 1x (weak: the GPU tests link no battle speed). */
extern int battleSpeedFactor(void) __attribute__((weak));

#define AUTO_SETTLE 30 /* vblanks a new auto N must last before it is made */
#define KEEP_SHARE 90  /* % of an upload's pixels whose colour must match */

static struct {
    int loaded;
    int upscale; /* 0 auto, 1 exact, 2..12 */
    int lastUpscale; /* the upscaled choice Shift+F4 goes back to (0 auto) */
    int color;   /* HIRES_COLOR_* */
    int orderedFallback;
    int precision, perspective, seamGuard, cpuEffects, slivers;
    int coarseLarge; /* upscale_large=half: large batches at N/2 (vk.c drawHiBatch; the Enhanced engine only) */
    int effectScale, effectMargin, effectExpanded, effectWidth, effectShifted;
    VRect effectKey;
    int pending, pendingFor;
    int failedAt; /* an N the device could not hold */
    int gpuMs, timingActive;
    int gpuMsDefault; /* upscale_gpu_ms unset: the device's default (hiresDeviceDefaults) */
    AutoResolution automatic;
    int dump;
    uint32_t dumpRange[32][3];
    int dumpRanges;
    uint32_t dumpLast;
    /* Tiles of VRAM where the copy has more than its 1x picture. */
    TileSet detail;
    /* The StoreImage stash (vk.c keeps the hi-res side). */
    struct {
        int used;
        VRect r;
        unsigned age;
        uint16_t px[HI_STASH_W * HI_STASH_H];
    } stash[HI_STASH_SLOTS];
    unsigned stashAge;
    /* hiresLoadPlan's choice for hiresLoadApply. */
    int planSlot;
    uint32_t keeps, stashes, replicates;
    uint32_t effectCaptures, effectBlurs;
} hr;

#define HI_AUTO_BUDGET_INTEGRATED 8 /* ms: the Auto GPU budget's default on an integrated GPU (hiresDeviceDefaults) */
#define COLOR_MODES 4
static const char* const colorNames[COLOR_MODES] = {"true", "15dither1x", "15ditherhi", "raw"};

static void load(void) {
    if (hr.loaded) {
        return;
    }
    hr.loaded = 1;
    settingsDescribe("gpu_ordered_batches", "gpu_ordered_batches: exact ordered GPU fallback on devices without fragment interlock, 0 off (default), 1 on; bounded 20 MiB arena");
    hr.orderedFallback=settingsBool("gpu_ordered_batches",0);
    settingsDescribe("upscale_cpu_effects", "upscale_cpu_effects: keep rendered detail through the Gear arena pause blur above 1x, 0 off (default), 1 on");
    hr.cpuEffects = settingsBool("upscale_cpu_effects", 0);
    settingsDescribe("upscale_large", "upscale_large: full (default) or half: the Enhanced engine draws large surfaces (arenas, Gears, "
                                      "screen-covering layers) at half the copy's resolution, text, the HUD and small things at full; "
                                      "cheaper on handheld GPUs, 2x2 blocks on their edges");
    const char* coarse = settingsGet("upscale_large");
    hr.coarseLarge = coarse != NULL && strcmp(coarse, "half") == 0;
    settingsDescribe("upscale_gpu_ms", "upscale_gpu_ms: Auto resolution GPU budget in milliseconds, 1..50; 0 off; unset: off on a "
                                       "discrete GPU, 8 on an integrated one (handhelds, laptops); fixed resolution is unaffected");
    hr.gpuMsDefault = settingsGet("upscale_gpu_ms") == NULL;
    hr.gpuMs = settingsInt("upscale_gpu_ms", 0);
    if (hr.gpuMs < 0 || hr.gpuMs > 50) hr.gpuMs = 0;
    if (hr.gpuMsDefault && gvkDeviceIntegrated() > 0) hr.gpuMs = HI_AUTO_BUDGET_INTEGRATED;
    settingsDescribe("geometry_precision", "geometry_precision: 1 for subpixel geometry in the display copy, 0 for PS1 vertices; defaults off at 1x, on with upscaling");
    settingsDescribe("perspective_textures", "perspective_textures: 1 for perspective-correct textures with subpixel geometry (default), 0 for affine");
    hr.perspective = settingsBool("perspective_textures", 1);
    settingsDescribe("geometry_seam_guard", "geometry_seam_guard: 1 for small opaque surface overlap with subpixel geometry (default), 0 for pure projected positions");
    hr.seamGuard = settingsBool("geometry_seam_guard", 1);
    settingsDescribe("geometry_slivers", "geometry_slivers: restore thin terrain, shadows and custom surfaces lost to integer culling with subpixel geometry, 0 off (default), 1 on");
    hr.slivers = settingsBool("geometry_slivers", 0);
    settingsDescribe("upscale", "upscale: 1 (the exact PS1 picture), auto (from the window's size) or 2..12 times "
                                "the PS1's resolution");
    settingsDescribe("upscale_color", "upscale_color: true (true colour), 15dither1x (15-bit, the PS1's dither), "
                                      "15ditherhi (15-bit, fine dither) or raw (8-bit, brighter darks than the PS1)");
    const char* s = settingsGet("upscale");
    hr.upscale = 1;
    if (s != NULL && SDL_strcasecmp(s, "auto") == 0) {
        hr.upscale = 0;
    } else if (s != NULL) {
        char* end;
        long v = strtol(s, &end, 10);
        if (end != s && *end == '\0' && v >= 1 && v <= 12) {
            hr.upscale = (int)v;
        } else {
            fprintf(stderr, "hires: upscale '%s' unknown (1, auto, 2..12); using 1\n", s);
        }
    }
    hr.precision = settingsBool("geometry_precision", hr.upscale != 1);
    gtePrecisionEnable(hr.precision || interpolationEnabled());
    s = settingsGet("upscale_color");
    hr.color = HIRES_COLOR_TRUE;
    for (int i = 0; s != NULL && i < COLOR_MODES; i++) {
        if (SDL_strcasecmp(s, colorNames[i]) == 0) {
            hr.color = i;
        }
    }
    if (s != NULL && SDL_strcasecmp(s, colorNames[hr.color]) != 0) {
        fprintf(stderr, "hires: upscale_color '%s' unknown (true, 15dither1x, 15ditherhi, raw); using true\n", s);
    }
    const char* d = getenv("XG_DUMP_HIRES");
    hr.dump = d != NULL && d[0] != '\0' && strcmp(d, "0") != 0;
    for (const char* p = d; hr.dump && strchr(d, '-') != NULL && *p != '\0' && hr.dumpRanges < 32;) {
        char* end;
        uint32_t* r = hr.dumpRange[hr.dumpRanges];
        r[0] = (uint32_t)strtoul(p, &end, 10);
        if (*end != '-') {
            break;
        }
        r[1] = (uint32_t)strtoul(end + 1, &end, 10);
        r[2] = *end == '/' ? (uint32_t)strtoul(end + 1, &end, 10) : 0;
        hr.dumpRanges++;
        p = *end == ',' ? end + 1 : end;
    }
}

/* ---- the settings menu (gpu.h) ------------------------------------------------- */

int gpuOrderedFallback(void) {load();return hr.orderedFallback;}
void gpuSetOrderedFallback(int on) {
    load();on=!!on;
    if(hr.orderedFallback==on)return;
    gvkSetOrderedFallback(on);
    hr.orderedFallback=on;settingsSetInt("gpu_ordered_batches",on);
}
int hiresUpscale(void) {
    load();
    return hr.upscale;
}

int hiresGpuBudget(void) { load(); return hr.gpuMs; }

/* Measured (docs/ENGINE_PLAN.md, xg-hbc.10): on an Intel UHD 770
 * the hi-res copy of a Gear battle costs 9.4 ms per vblank at 12x, 3.8 at 4x,
 * 1.7 at 2x; an 8 ms budget settled it at 6x with the native rate kept. A
 * discrete GPU holds 12x everywhere, so it keeps the budget off. */
void hiresDeviceDefaults(void) {
    load();
    if (hr.gpuMsDefault && hr.gpuMs == 0 && gvkDeviceIntegrated() > 0) hr.gpuMs = HI_AUTO_BUDGET_INTEGRATED;
}
void hiresGpuTimePause(void) {
    hr.timingActive = 0;
    autoResolutionRestart(&hr.automatic);
    gvkGpuTimeEnable(0);
}
void hiresSetGpuBudget(int ms) {
    load();
    if (ms < 0 || ms > 50) return;
    hr.gpuMs = ms;
    hr.gpuMsDefault = 0; /* chosen: the device's default no longer applies */
    hiresGpuTimePause();
    autoResolutionReset(&hr.automatic, 0);
    settingsSetInt("upscale_gpu_ms", ms);
    settingsSave();
}

void hiresSetUpscale(int v) {
    load();
    if (v < 0 || v > 12) {
        return;
    }
    hr.upscale = v;
    hiresGpuTimePause();
    autoResolutionReset(&hr.automatic, 0);
    if (settingsGet("geometry_precision") == NULL) {
        hr.precision = v != 1;
        gtePrecisionEnable(hr.precision || interpolationEnabled());
    }
    hr.failedAt = 0;
    char text[8];
    snprintf(text, sizeof(text), "%d", v);
    settingsSet("upscale", v == 0 ? "auto" : text);
    settingsSave();
}

/* Shift+F4 (pad.c): the exact picture, and back to the upscaled choice it
 * left (auto the first time). */
void hiresToggleUpscale(void) {
    load();
    if (hr.upscale != 1) {
        hr.lastUpscale = hr.upscale;
        hiresSetUpscale(1);
        presentShowNote("RESOLUTION 1X (EXACT)");
        return;
    }
    hiresSetUpscale(hr.lastUpscale);
    char text[32];
    snprintf(text, sizeof(text), hr.lastUpscale == 0 ? "RESOLUTION AUTO" : "RESOLUTION %dX", hr.lastUpscale);
    presentShowNote(text);
}

int hiresColor(void) {
    load();
    return hr.color;
}

void hiresSetColor(int c) {
    load();
    if (c >= 0 && c < COLOR_MODES) {
        hr.color = c;
        settingsSet("upscale_color", colorNames[c]);
        settingsSave();
    }
}

int hiresScaleNow(void) {
    int n = gvkHiScale();
    return n > 0 ? n : 1;
}

/* upscale_large=half (the Enhanced engine only; vk.c drawHiBatch takes it
 * per run of triangles: at least half a screen, at an even N of 4 or more,
 * not while smooth motion is capturing). */
int hiresCoarseLarge(void) { load(); return hr.coarseLarge && engineThis() == ENGINE_ENHANCED; }
void hiresSetCoarseLarge(int on) {
    load();
    hr.coarseLarge = on != 0;
    settingsSet("upscale_large", on ? "half" : "full");
    settingsSave();
}

int hiresCpuEffects(void) { load(); return hr.cpuEffects; }
void hiresSetCpuEffects(int on) {
    load();
    hr.cpuEffects = on != 0;
    hr.effectScale = 0;
    gvkHiEffectReset();
    settingsSetInt("upscale_cpu_effects", hr.cpuEffects);
    settingsSave();
}

/* Only the identified arena blur opts in. Never infer colour operations
 * from arbitrary uploads (palettes, decoded movies and game data stay 1x).
 * GPU sources are host caches, invalid after loads/resizes or view changes. */
static VRect effectRect(const void* rect) {
    const int16_t* r = rect;
    return (VRect){r[0],r[1],r[0]+r[2]-1,r[1]+r[3]-1};
}
void pcHiresBattlingCapture(const void* rect) {
    hr.effectScale = 0;
    gvkHiEffectReset();
    if (!hiresCpuEffects() || gvkHiScale() <= 1) return;
    VRect r = effectRect(rect), wide;
    int shifted = 0, hasWide = wideEffectRect(r, &wide, &shifted);
    gp0Flush();
    if (!gvkHiEffectCapture(r, hasWide ? &wide : NULL)) return;
    hr.effectCaptures++;
    hr.effectKey = r;
    hr.effectScale = gvkHiScale();
    hr.effectMargin = wideMargin();
    hr.effectExpanded = presentExpandedVertical();
    hr.effectWidth = hasWide ? wide.x1-wide.x0+1 : 0;
    hr.effectShifted = shifted;
}
void pcHiresBattlingBlur(const void* rect) {
    VRect r = effectRect(rect), wide;
    int shifted = 0, hasWide = wideEffectRect(r, &wide, &shifted);
    if (!hiresCpuEffects() || hr.effectScale <= 1 || hr.effectScale != gvkHiScale() ||
        memcmp(&r,&hr.effectKey,sizeof(r)) || hr.effectMargin != wideMargin() ||
        hr.effectExpanded != presentExpandedVertical() ||
        hr.effectWidth != (hasWide ? wide.x1-wide.x0+1 : 0) || hr.effectShifted != shifted) {
        hr.effectScale = 0; gvkHiEffectReset(); return;
    }
    gp0Flush();
    if (gvkHiEffectBlur(0,r,-1)) { hiresMarkDetail(r); hr.effectBlurs++; }
    if (hasWide) gvkHiEffectBlur(1,wide,shifted ? -1 : (hr.effectWidth-320)/2);
}

int hiresPrecision(void) { load(); return hr.precision; }
int hiresPerspective(void) { load(); return hr.perspective; }
int hiresSeamGuard(void) { load(); return hr.seamGuard; }
int hiresSlivers(void) { load(); return hr.slivers; }
void hiresSetSlivers(int on) {
    load();
    hr.slivers = on != 0;
    settingsSetInt("geometry_slivers", hr.slivers);
    settingsSave();
}
void hiresSetSeamGuard(int on) {
    load();
    hr.seamGuard = on != 0;
    settingsSetInt("geometry_seam_guard", hr.seamGuard);
    settingsSave();
}
void hiresSetPrecision(int on) {
    load();
    hr.precision = on != 0;
    gtePrecisionEnable(hr.precision || interpolationEnabled());
    settingsSetInt("geometry_precision", hr.precision);
    settingsSave();
}
void hiresSetPerspective(int on) {
    load();
    hr.perspective = on != 0;
    settingsSetInt("perspective_textures", hr.perspective);
    settingsSave();
}

/* ---- N ---------------------------------------------------------------------------- */

/* The bytes the copy takes at N (VRAM, the stash, the wide target's copy at
 * `cols` columns). */
static double bytesAt(int n, int cols) {
    return 4.0 * n * n * ((double)VRAM_W * VRAM_H + (double)HI_STASH_W * HI_STASH_SLOTS * HI_STASH_H + (double)cols * WIDE_H);
}

/* The N wanted for display d in a target of W x H pixels (0: none). */
static int wanted(const GpuDisplay* d, int W, int H) {
    if (hr.upscale == 1) {
        return hr.precision ? 1 : 0;
    }
    int n = hr.upscale;
    if (n == 0) {
        if (!d->enabled || d->rgb24 || d->w <= 0 || d->h <= 0 || W <= 0 || H <= 0) {
            return gvkHiScale(); /* a movie or a blank screen: as it is */
        }
        PresentConstants pc;
        presentConstants(d, W, H, &pc, NULL, 0);
        const WideView* v = wideCurrentView();
        int lines = v->wide && (v->top || v->bottom) ? v->h : d->h;
        n = pc.dest[3] / lines;
        if (!gvkHasInterlock() && n > 4) {
            n = 4;
        }
    }
    n = n > 12 ? 12 : n;
    /* What the device holds: VRAM 1024N across, the wide target 2048N down;
     * all of it in a share of its memory. */
    int maxDim;
    uint64_t heap;
    gvkHiLimits(&maxDim, &heap);
    int aw = 4, ah = 3;
    int wide = presentAspect(&aw, &ah) == PRESENT_ASPECT_WIDE || presentExpandedVertical();
    int cols = gvkHiWideWidth();
    if (wide && cols == 0) {
        cols = 240 * aw / ah + 8; /* a 320-wide picture's margins */
    }
    double budget = (double)heap * 0.4;
    budget = budget > 3.0e9 ? 3.0e9 : budget;
    while (n >= 2 && (VRAM_W * n > maxDim || (wide && WIDE_H * n > maxDim) || bytesAt(n, cols) > budget ||
                      (hr.failedAt != 0 && n >= hr.failedAt))) {
        n--;
    }
    return n >= 2 ? n : hr.precision ? 1 : 0;
}

void hiresVBlank(const GpuDisplay* d, int W, int H) {
    load();
    int want = wanted(d, W, H);
    int now = gvkHiScale();
    int timing = hr.upscale == 0 && hr.gpuMs > 0 && d->enabled && !d->rgb24 &&
                 d->w > 0 && d->h > 0 && W > 0 && H > 0;
    int active = gvkGpuTimeEnable(timing);
    if (timing && !active)
        gpuWarnOnce("autotime", "hires: GPU timing unavailable; Auto uses window and device limits");
    int ceiling = want > 0 ? want : 1;
    if (active != hr.timingActive) {
        if (hr.automatic.ceiling == 0) autoResolutionReset(&hr.automatic, ceiling);
        else autoResolutionRestart(&hr.automatic);
        hr.timingActive = active;
    }
    if (active) {
        autoResolutionCeiling(&hr.automatic, ceiling);
        /* hw.c's vblank set this frame's factor (battleSpeedVBlank) just
         * before this one, on this thread; the sample below is older. */
        autoResolutionSpeed(&hr.automatic, battleSpeedFactor != NULL ? battleSpeedFactor() : 1);
        double ms;
        unsigned frames;
        int scale;
        if (gvkGpuTimeTake(&ms, &frames, &scale) && scale == (now > 0 ? now : 1)) {
            int before = hr.automatic.limit;
            autoResolutionSample(&hr.automatic, scale, hr.gpuMs, ms, frames);
            if (before != hr.automatic.limit) {
                gpuLog("hires: Auto GPU %.3f ms/vblank, budget %d ms at %dx speed: limit %dx -> %dx",
                       hr.automatic.mean, hr.gpuMs, hr.automatic.factor, before, hr.automatic.limit);
            }
        }
        if (ceiling > hr.automatic.limit) {
            int n = hr.automatic.limit;
            want = n > 1 ? n : hr.precision ? 1 : 0;
        }
    }
    if (want != now && hr.upscale == 0 && now != 0 && want != 0) {
        /* auto: a window being resized settles first */
        if (want != hr.pending) {
            hr.pending = want;
            hr.pendingFor = 0;
        }
        if (++hr.pendingFor < AUTO_SETTLE) {
            gvkHiConfigure(now, hr.color);
            return;
        }
    }
    hr.pending = 0;
    hr.pendingFor = 0;
    int got = gvkHiConfigure(want, hr.color);
    if (got != now) {
        /* A new copy, refilled from 1x: nothing in it is more than 1x yet. */
        tilesClear(&hr.detail);
        memset(hr.stash, 0, sizeof(hr.stash));
        if (want != 0 && got == 0) {
            hr.failedAt = want;
        }
    }
}

/* ---- what the copy has drawn ---------------------------------------------------- */

void hiresMarkDetail(VRect r) {
    tilesAdd(&hr.detail, r);
}

/* A picture between's device-side keep of the displayed buffer (libgpu.c
 * pcVramKeepDevice): the copy's pixels there come back exactly, and so
 * does which of its tiles have detail. */
static struct {
    int on;
    VRect r;
    TileSet detail;
} detailKept;

void hiresDetailKeep(VRect r) {
    detailKept.r = r;
    detailKept.detail = hr.detail;
    detailKept.on = 1;
}

void hiresDetailPutBack(void) {
    if (!detailKept.on) {
        return;
    }
    detailKept.on = 0;
    VRect r = detailKept.r;
    int tx0 = r.x0 >> TILE_SHIFT, tx1 = r.x1 >> TILE_SHIFT;
    uint64_t m = (tx1 - tx0 == 63 ? UINT64_MAX : (UINT64_C(1) << (tx1 - tx0 + 1)) - 1) << tx0;
    for (int ty = r.y0 >> TILE_SHIFT; ty <= r.y1 >> TILE_SHIFT; ty++) {
        hr.detail.rows[ty] = (hr.detail.rows[ty] & ~m) | (detailKept.detail.rows[ty] & m);
    }
}

/* Tiles wholly inside r lose their detail (replicated over). */
static void clearDetail(VRect r) {
    int tx0 = (r.x0 + (1 << TILE_SHIFT) - 1) >> TILE_SHIFT, tx1 = ((r.x1 + 1) >> TILE_SHIFT) - 1;
    int ty0 = (r.y0 + (1 << TILE_SHIFT) - 1) >> TILE_SHIFT, ty1 = ((r.y1 + 1) >> TILE_SHIFT) - 1;
    for (int ty = ty0; ty <= ty1; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            hr.detail.rows[ty] &= ~(1ull << tx);
        }
    }
}

/* ---- transfers ------------------------------------------------------------------ */

/* The pixel gpuLoad writes over `old` (libgpu.c: the mask bits). */
static uint16_t loaded(uint16_t s, uint16_t old, int setMask, int checkMask) {
    return checkMask && (old & 0x8000) ? old : (uint16_t)(s | (setMask ? 0x8000 : 0));
}

int hiresLoadPlan(VRect r, const uint16_t* src, int pitch, int setMask, int checkMask) {
    if (gvkHiScale() == 0) {
        return -1;
    }
    int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1;
    long total = (long)w * h, need = (total * KEEP_SHARE + 99) / 100;
    if (tilesHit(&hr.detail, r)) {
        /* Over pixels the copy drew: keep them where the colour is the same. */
        vramSyncRect(r);
        long same = 0;
        for (int y = 0; y < h; y++) {
            const uint16_t* s = src + (size_t)y * pitch;
            const uint16_t* o = &vramShadow[r.y0 + y][r.x0];
            for (int x = 0; x < w; x++) {
                same += ((loaded(s[x], o[x], setMask, checkMask) ^ o[x]) & 0x7FFF) == 0;
            }
        }
        if (same >= need) {
            return HI_LOAD_KEEP;
        }
    }
    /* A StoreImage of the same size, loaded elsewhere. */
    int best = -1;
    long bestSame = need - 1;
    for (int k = 0; k < HI_STASH_SLOTS; k++) {
        if (!hr.stash[k].used || hr.stash[k].r.x1 - hr.stash[k].r.x0 + 1 != w || hr.stash[k].r.y1 - hr.stash[k].r.y0 + 1 != h) {
            continue;
        }
        long same = 0;
        for (int y = 0; y < h; y++) {
            const uint16_t* s = src + (size_t)y * pitch;
            const uint16_t* o = &vramShadow[r.y0 + y][r.x0];
            const uint16_t* t = &hr.stash[k].px[(size_t)y * w];
            for (int x = 0; x < w; x++) {
                same += ((loaded(s[x], o[x], setMask, checkMask) ^ t[x]) & 0x7FFF) == 0;
            }
        }
        if (same > bestSame) {
            bestSame = same;
            best = k;
        }
    }
    if (best >= 0) {
        hr.planSlot = best;
        return HI_LOAD_STASH;
    }
    return HI_LOAD_REPLICATE;
}

/* A put back of pixels as they were before a picture between (libgpu.c
 * pcVramPutBack): the detail kept where the colour agrees, as a load that
 * mostly agrees gets (hiresLoadPlan's KEEP), without its count or
 * readback; replicated where the copy drew nothing. */
int hiresPutBackPlan(VRect r) {
    if (gvkHiScale() == 0) {
        return -1;
    }
    return tilesHit(&hr.detail, r) ? HI_LOAD_KEEP : HI_LOAD_REPLICATE;
}

void hiresLoadApply(VRect r, int plan) {
    if (plan < 0 || gvkHiScale() == 0) {
        return;
    }
    const uint16_t* ref = NULL;
    int refPitch = 0;
    if (plan == HI_LOAD_STASH) {
        ref = hr.stash[hr.planSlot].px;
        refPitch = r.x1 - r.x0 + 1;
        hr.stash[hr.planSlot].age = ++hr.stashAge;
        hr.stashes++;
    }
    hr.keeps += plan == HI_LOAD_KEEP;
    hr.replicates += plan == HI_LOAD_REPLICATE;
    gvkHiLoad(r, &vramShadow[r.y0][r.x0], VRAM_W, plan, hr.planSlot, ref, refPitch);
    if (plan == HI_LOAD_REPLICATE) {
        clearDetail(r);
    } else {
        tilesAdd(&hr.detail, r);
    }
}

void hiresStore(VRect r) {
    int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1;
    if (gvkHiScale() == 0 || w > HI_STASH_W || h > HI_STASH_H || !tilesHit(&hr.detail, r)) {
        return;
    }
    /* The same rectangle again, else a free slot, else the oldest. */
    int k = -1;
    for (int i = 0; i < HI_STASH_SLOTS && k < 0; i++) {
        if (hr.stash[i].used && memcmp(&hr.stash[i].r, &r, sizeof(r)) == 0) {
            k = i;
        }
    }
    for (int i = 0; i < HI_STASH_SLOTS && k < 0; i++) {
        if (!hr.stash[i].used) {
            k = i;
        }
    }
    if (k < 0) {
        k = 0;
        for (int j = 1; j < HI_STASH_SLOTS; j++) {
            k = hr.stash[j].age < hr.stash[k].age ? j : k;
        }
    }
    hr.stash[k].used = 1;
    hr.stash[k].r = r;
    hr.stash[k].age = ++hr.stashAge;
    for (int y = 0; y < h; y++) {
        memcpy(&hr.stash[k].px[(size_t)y * w], &vramShadow[r.y0 + y][r.x0], (size_t)w * 2);
    }
    gvkHiStash(r, k);
}

void hiresMove(int sx, int sy, int dx, int dy, int w, int h, int setMask, int checkMask) {
    if (gvkHiScale() == 0) {
        return;
    }
    VPiece src[4], dst[4];
    int ns = vramWrap(sx, sy, w, h, src);
    int nd = vramWrap(dx, dy, w, h, dst);
    int detail = 0;
    for (int i = 0; i < ns; i++) {
        detail |= tilesHit(&hr.detail, src[i].r);
    }
    gvkHiMove(src, ns, dst, nd, w, h, setMask, checkMask);
    for (int i = 0; i < nd; i++) {
        if (detail) {
            tilesAdd(&hr.detail, dst[i].r);
        } else {
            clearDetail(dst[i].r);
        }
    }
}

/* At exit (vk.c): what the copy did with the CPU's uploads. */
void hiresSummary(void) {
    if (hr.cpuEffects) gpuLog("hires: CPU effects captures %u blurs %u", hr.effectCaptures, hr.effectBlurs);
    if (hr.keeps + hr.stashes + hr.replicates != 0) {
        gpuLog("hires: uploads: %u kept the copy's pixels in place, %u took a stashed StoreImage's, %u replicated",
               hr.keeps, hr.stashes, hr.replicates);
    }
}

/* ---- dumps -------------------------------------------------------------------------- */

static void dumpHi(const char* name, uint32_t vblank, int wide, int x, int y, int w, int h) {
    size_t n = (size_t)w * h;
    uint32_t* px = malloc(n * 4);
    uint8_t* rgb = malloc(n * 3);
    if (px != NULL && rgb != NULL && gvkHiRead(wide, px, x, y, w, h)) {
        for (size_t i = 0; i < n; i++) {
            rgb[i * 3] = (uint8_t)px[i];
            rgb[i * 3 + 1] = (uint8_t)(px[i] >> 8);
            rgb[i * 3 + 2] = (uint8_t)(px[i] >> 16);
        }
        char path[700];
        snprintf(path, sizeof(path), "%s/%s_%06u.png", dumpDir(), name, (unsigned)vblank);
        pngWrite(path, w, h, rgb);
    }
    free(px);
    free(rgb);
}

void hiresDump(const GpuDisplay* d, uint32_t vblank, int frameWritten) {
    load();
    int n = gvkHiScale();
    if (!hr.dump || n == 0 || !frameWritten || !d->enabled || d->rgb24 || d->w <= 0 || d->h <= 0 ||
        d->x + d->w > VRAM_W || d->y + d->h > VRAM_H) {
        return;
    }
    int in = hr.dumpRanges == 0;
    for (int i = 0; i < hr.dumpRanges; i++) {
        const uint32_t* r = hr.dumpRange[i];
        in |= vblank >= r[0] && vblank <= r[1] && (r[2] == 0 || vblank >= hr.dumpLast + r[2]);
    }
    if (!in) {
        return;
    }
    hr.dumpLast = vblank;
    dumpHi("hires", vblank, 0, d->x * n, d->y * n, d->w * n, d->h * n);
    const WideView* v = wideCurrentView();
    if (v->has && v->w > 0 && v->x + v->w <= gvkHiWideWidth()) {
        dumpHi("hireswide", vblank, 1, v->x * n, v->y * n, v->w * n, v->h * n);
    }
}

/* ---- save states (system/savestate.h) ---- */

/* After a load: the copy holds the old timeline's detail; it is made again
 * from the restored 1x pictures (replicated until the game draws again),
 * and nothing in it counts as more than its 1x picture any more. */
void hiresStateLoaded(void) {
    hr.effectScale = 0;
    gvkHiEffectReset();
    hiresGpuTimePause();
    autoResolutionReset(&hr.automatic, 0);
    tilesClear(&hr.detail);
    for (int i = 0; i < HI_STASH_SLOTS; i++) {
        hr.stash[i].used = 0;
    }
    gvkHiRefill();
}
