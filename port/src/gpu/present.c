/*
 * How the PS1 picture is put in the window: the display options, the window
 * itself, and the on-screen note (the Vulkan pass that draws it is in vk.c,
 * shaders/present.frag).
 *
 * Options (settings.ini, port/src/system/settings.h; XG_<KEY> overrides):
 *   aspect=4:3tv    a TV's picture (default; "4:3" means this too): the
 *                   4:3 frame of a TV (2560 GPU clocks by 240 lines) with the
 *                   display area at the size and place the TV showed it, so
 *                   Xenogears' 216 lines are 1.48:1 with thin bands above and
 *                   below; what falls outside the frame is not shown
 *   expanded_vertical=0  original framing (default); 1 renders the missing
 *                   TV lines in fields and battles at the same projection.
 *                   Fixed artwork retains its authored framing.
 *   aspect=4:3fill  the display area fills a 4:3 picture, whatever the
 *                   display mode or range (216 lines drawn 11% taller)
 *   aspect=1:1      square pixels: each PS1 pixel square, so 320x240 stays
 *                   4:3 but 256-, 368-, 512- and 640-wide modes and 480i
 *                   change shape (320x216 is 1.48:1)
 *   aspect=16:9     wide shapes (16:9, 16:10, 21:9, 32:9 or any w:h wider
 *                   than 4:3): true widescreen (wide.c). A TV frame of that
 *                   shape, the picture as wide as it with more of the scene
 *                   at the sides; a frame that stays 4:3 (movies, screens
 *                   not ready for wide) sits in the middle of it
 *   widefill=bars   a frame that stays 4:3 in a wide frame (movies, screens
 *                   not ready for wide, a frame an effect keeps 4:3): black
 *                   beside it (default); widefill=zoom: the picture scaled
 *                   up to the wide frame's width, centred, its top and
 *                   bottom cut (integer scale: the whole factor whose width
 *                   comes closest to the wide picture's); widefill=blur: the
 *                   picture blurred and darkened beside it
 *   scale=fit       the largest rectangle of that shape, centred (default;
 *                   4:3 TV: the largest 4:3 frame)
 *   scale=integer   each PS1 pixel a whole number of window pixels across
 *                   and down, as large as fits. With square pixels that is
 *                   n x n; otherwise the two factors come as close to the
 *                   picture's shape as whole numbers can (4:3 fill: exact for
 *                   320 and 640 wide at 240/480 lines, 256x240 at 5x4; 4:3
 *                   TV: exact for every 320-wide picture). Centred; 4:3 TV
 *                   puts it where the TV did, kept inside the window. A
 *                   window smaller than 1x falls back to fit
 *   scale=stretch   the whole window (4:3 TV: the frame stretched to it)
 *   present_filter=nearest  how the picture is resampled to the window
 *                   (shaders/filters.glsl): nearest, sharp pixels (default);
 *                   linear, bilinear; sharp, each window pixel the average of
 *                   the picture under it (whole multiples exactly as nearest,
 *                   other scales blend only where two pixels meet: crisp and
 *                   even); bicubic, smooth (Catmull-Rom, no ringing); xbr,
 *                   edges in pixel art smoothed (xBR's rules: corners cut
 *                   along the edges, sharp at any scale). Integer scale stays
 *                   exact: linear and bicubic are nearest there, xbr is not.
 *                   The older key filter=nearest|linear is read when
 *                   present_filter is not set
 *   present_movie_filter=same  the same for 24-bit pictures (movies): same
 *                   (as present_filter, default) or one of the filters
 *   fullscreen=0|1  borderless fullscreen on the window's display
 *   window_width, window_height, window_x, window_y: the window when not
 *                   fullscreen or maximised (kept at exit; no position on
 *                   Wayland, where a program cannot see or set it)
 *
 * Hotkeys (port/src/system/pad.c): F3 aspect, F4 scale, F5 filter (in the
 * order above), F6 wide fill, F11 or Alt+Enter fullscreen; the settings menu
 * (F1, port/src/ui) sets the same through presentSet*. Each change is written
 * to settings.ini at once and shown for about 2 s (120 vblanks) in the
 * window's top-left corner, drawn by the present pass with the font below:
 * never in VRAM, never in the game's picture or its XG_DUMP_FRAMES dumps.
 *
 * The settings menu's performance overlay (port/src/ui/diagnostics.cpp,
 * perf_overlay) is drawn the same way, a panel of lines in that corner (the
 * note, when there is one, as its first line).
 */

#include <SDL3/SDL.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_internal.h"
#include "system/settings.h"

#define NOTE_VBLANKS 120
#define NOTE_PAD 2      /* font pixels around the text */
#define NOTE_MARGIN 4   /* font pixels from the window's corner */
#define MATCH_TOLERANCE 0.02

enum { SCALE_FIT, SCALE_INTEGER, SCALE_STRETCH };
enum { FILL_BARS, FILL_ZOOM, FILL_BLUR };

static const char* const scaleNames[] = {"fit", "integer", "stretch"};
static const char* const filterNames[] = {"nearest", "linear", "sharp", "bicubic", "xbr"};
static const char* const filterNotes[] = {"NEAREST", "LINEAR", "SHARP", "BICUBIC", "XBR (EDGES)"};
static const char* const fillNames[] = {"bars", "zoom", "blur"};
static const char* const aspectPresets[] = {"4:3tv", "4:3fill", "1:1", "16:9", "16:10", "21:9", "32:9"};
#define PRESET_COUNT (int)(sizeof(aspectPresets) / sizeof(aspectPresets[0]))

static struct {
    int loaded;
    SDL_Window* window;
    int headless;
    int aspect; /* PRESENT_ASPECT_* */
    int aspectW, aspectH;
    char aspectText[16];
    char custom[16]; /* a wide w:h from the file that is no preset: kept in F3's cycle */
    int scale;
    int filter;      /* PRESENT_FILTER_* */
    int movieFilter; /* for 24-bit pictures, PRESENT_FILTER_SAME: as filter */
    int expandedVertical;
    int createdW, createdH; /* the window as created from the settings (presentCreateWindow) */
    uint64_t createdNs;     /* ... and when: the compositor's own size for it is declined once */
    int sizeAsserted;
    int fill; /* widefill: FILL_* */
    char note[64];
    int noteLeft; /* vblanks */
    /* The last picture and target, for the hotkeys' notes. */
    GpuDisplay last;
    int lastTargetW, lastTargetH;
    /* Where the last picture went (presentWindowToDisplay): its target
     * rectangle, its source's size, and a wide picture's margin and extra lines. */
    int32_t lastDest[4], lastSrcW, lastSrcH, lastMargin, lastTop;
} pr;

/* ---- options -------------------------------------------------------------------- */

/* "4:3tv", "4:3fill", or "w:h": 4:3 (a TV's picture), 1:1, or wider than 4:3. */
static int parseAspect(const char* s, int* kind, int* w, int* h) {
    int a, b;
    char extra;
    if (s != NULL && (SDL_strcasecmp(s, "4:3tv") == 0 || SDL_strcasecmp(s, "4:3fill") == 0)) {
        *kind = SDL_strcasecmp(s, "4:3tv") == 0 ? PRESENT_ASPECT_4_3_TV : PRESENT_ASPECT_4_3_FILL;
        *w = 4;
        *h = 3;
        return 1;
    }
    if (s == NULL || sscanf(s, "%d:%d%c", &a, &b, &extra) != 2 || a <= 0 || b <= 0 || a > 10000 || b > 10000) {
        return 0;
    }
    if (a * 3 == b * 4) {
        *kind = PRESENT_ASPECT_4_3_TV;
        a = 4, b = 3;
    } else if (a == b) {
        *kind = PRESENT_ASPECT_SQUARE;
        a = b = 1;
    } else if (a * 3 > b * 4) {
        *kind = PRESENT_ASPECT_WIDE;
    } else {
        return 0;
    }
    *w = a;
    *h = b;
    return 1;
}

static void setAspect(int kind, int w, int h) {
    pr.aspect = kind;
    pr.aspectW = w;
    pr.aspectH = h;
    snprintf(pr.aspectText, sizeof(pr.aspectText), "%d:%d%s", w, h,
             kind == PRESENT_ASPECT_4_3_TV ? "tv" : kind == PRESENT_ASPECT_4_3_FILL ? "fill" : "");
}

static int parseFilter(const char* s, int* out) {
    for (int i = 0; i < PRESENT_FILTER_COUNT; i++) {
        if (SDL_strcasecmp(s, filterNames[i]) == 0) {
            *out = i;
            return 1;
        }
    }
    return 0;
}

static void loadOptions(void) {
    if (pr.loaded) {
        return;
    }
    pr.loaded = 1;
    const char* h = getenv("XG_HEADLESS");
    pr.headless = h != NULL && h[0] != '\0' && strcmp(h, "0") != 0;
    settingsDescribe("aspect", "aspect: 4:3tv (a TV's picture), 4:3fill (fills 4:3), 1:1 (square pixels), or wide: 16:9, "
                               "16:10, 21:9, 32:9, w:h");
    settingsDescribe("scale", "scale: fit, integer or stretch");
    settingsDescribe("widefill", "widefill: a 4:3 picture in a wide frame: bars (black sides), zoom (scaled up to the "
                                 "frame's width, top and bottom cut) or blur (blurred sides)");
    settingsDescribe("present_filter", "present_filter: nearest (sharp pixels), linear, sharp (crisp, even pixels at "
                                       "any scale), bicubic (smooth) or xbr (smoothed edges in pixel art); integer "
                                       "scale stays exact (linear and bicubic are nearest there)");
    settingsDescribe("present_movie_filter", "present_movie_filter: the same for movies (24-bit pictures): same (as "
                                             "present_filter) or a filter");
    settingsDescribe("fullscreen", "fullscreen: 1 or 0");
    settingsDescribe("window_width", "window_width, window_height: the window's size when not fullscreen");
    settingsDescribe("window_x", "window_x, window_y: its position (not on Wayland)");

    int kind, w, hh;
    const char* s = settingsGet("aspect");
    if (parseAspect(s, &kind, &w, &hh)) {
        setAspect(kind, w, hh);
    } else {
        if (s != NULL) {
            fprintf(stderr, "present: aspect '%s' unknown (4:3tv, 4:3fill, 1:1, or w:h wider than 4:3); using 4:3tv\n",
                    s);
        }
        setAspect(PRESENT_ASPECT_4_3_TV, 4, 3);
    }
    int preset = 0;
    for (int i = 0; i < PRESET_COUNT; i++) {
        preset |= strcmp(pr.aspectText, aspectPresets[i]) == 0;
    }
    if (!preset) {
        snprintf(pr.custom, sizeof(pr.custom), "%s", pr.aspectText);
    }

    settingsDescribe("expanded_vertical", "expanded_vertical: 1 renders extra scene lines in fields and battles; 0 keeps original framing");
    pr.expandedVertical = settingsBool("expanded_vertical", 0);
    pr.scale = SCALE_FIT;
    s = settingsGet("scale");
    if (s != NULL) {
        int found = 0;
        for (int i = 0; i < 3; i++) {
            if (SDL_strcasecmp(s, scaleNames[i]) == 0) {
                pr.scale = i;
                found = 1;
            }
        }
        if (!found) {
            fprintf(stderr, "present: scale '%s' unknown (fit, integer, stretch); using fit\n", s);
        }
    }
    pr.fill = FILL_BARS;
    s = settingsGet("widefill");
    if (s != NULL) {
        int found = 0;
        for (int i = 0; i < 3; i++) {
            if (SDL_strcasecmp(s, fillNames[i]) == 0) {
                pr.fill = i;
                found = 1;
            }
        }
        if (!found) {
            fprintf(stderr, "present: widefill '%s' unknown (bars, zoom, blur); using bars\n", s);
        }
    }
    /* The older filter=nearest|linear when present_filter is not set (or
     * only XG_FILTER is given for this run). */
    const char* key = "present_filter";
    s = settingsGet(key);
    if (s == NULL || (settingsOverride("filter") != NULL && settingsOverride(key) == NULL)) {
        key = "filter";
        s = settingsGet(key);
    }
    pr.filter = PRESENT_FILTER_NEAREST;
    if (s != NULL && !parseFilter(s, &pr.filter)) {
        fprintf(stderr, "present: %s '%s' unknown (nearest, linear, sharp, bicubic, xbr); using nearest\n", key, s);
    }
    pr.movieFilter = PRESENT_FILTER_SAME;
    s = settingsGet("present_movie_filter");
    if (s != NULL && SDL_strcasecmp(s, "same") != 0 && !parseFilter(s, &pr.movieFilter)) {
        fprintf(stderr, "present: present_movie_filter '%s' unknown (same, nearest, linear, sharp, bicubic, xbr); "
                        "using same\n", s);
    }
}

int presentAspect(int* w, int* h) {
    loadOptions();
    if (w != NULL) {
        *w = pr.aspectW;
    }
    if (h != NULL) {
        *h = pr.aspectH;
    }
    return pr.aspect;
}

int presentExpandedVertical(void) {
    loadOptions();
    return pr.expandedVertical;
}

void presentSetExpandedVertical(int on) {
    loadOptions();
    if (pr.expandedVertical != (on != 0)) gvkHiEffectReset();
    pr.expandedVertical = on != 0;
    settingsSetInt("expanded_vertical", pr.expandedVertical);
    settingsSave();
}

/* ---- layout ------------------------------------------------------------------------ */

typedef struct {
    int32_t dest[4]; /* the display area in the target */
    int32_t clip[4]; /* where it may show (4:3 TV: the TV's frame) */
    int sx, sy;      /* integer mode: window pixels per PS1 pixel; 0 otherwise */
} Layout;

static void fitRect(int W, int H, int num, int den, int32_t dest[4]) {
    int dw = W, dh = H;
    if ((int64_t)W * den > (int64_t)H * num) {
        dw = (int)((int64_t)H * num / den);
    } else {
        dh = (int)((int64_t)W * den / num);
    }
    dest[0] = (W - dw) / 2;
    dest[1] = (H - dh) / 2;
    dest[2] = dw;
    dest[3] = dh;
}

static int roundInt(double v) {
    return (int)floor(v + 0.5);
}

static int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* How far a shape is from the one wanted, as a ratio minus one. */
static double shapeError(double have, double want) {
    return have > want ? have / want - 1.0 : want / have - 1.0;
}

/* Whole-number factors for a w x h picture in a W x H window whose shape
 * comes closest to `want` (width / height): the largest height whose best
 * width matches the shape within MATCH_TOLERANCE, else the best match of all
 * (ties: the larger). 0 when not even 1x fits. */
static int integerFactors(int W, int H, int w, int h, double want, int* outX, int* outY) {
    int maxX = W / w, maxY = H / h;
    if (maxX < 1 || maxY < 1) {
        return 0;
    }
    double k = want * h / w; /* the horizontal factor per vertical one */
    int bestX = 0, bestY = 0;
    double bestE = 1e30;
    for (int sy = maxY; sy >= 1; sy--) {
        int base = (int)(sy * k);
        int lineX = 0;
        double lineE = 1e30;
        for (int sx = base; sx <= base + 1; sx++) {
            int c = sx < 1 ? 1 : sx > maxX ? maxX : sx;
            double e = shapeError((double)c * w / ((double)sy * h), want);
            if (e < lineE - 1e-9) {
                lineE = e;
                lineX = c;
            }
        }
        if (lineE <= MATCH_TOLERANCE) {
            *outX = lineX;
            *outY = sy;
            return 1;
        }
        if (lineE < bestE - 1e-9) {
            bestE = lineE;
            bestX = lineX;
            bestY = sy;
        }
    }
    *outX = bestX;
    *outY = bestY;
    return 1;
}

/* 4:3 TV: the display area inside the TV's frame. fit and stretch scale the
 * frame (2560 clocks by frameLines lines) to `frame`; integer sizes it by the
 * factors, centres it and keeps the picture inside the window. */
static void tvLayout(const GpuDisplay* d, int W, int H, Layout* lay) {
    double lines = (double)d->h / d->rows;
    if (pr.scale == SCALE_INTEGER) {
        double want = (d->w * d->clocks / 2560.0 * 4.0) / (lines / d->frameLines * 3.0);
        int sx, sy;
        if (integerFactors(W, H, d->w, d->h, want, &sx, &sy)) {
            double frameW = 2560.0 * sx / d->clocks;
            double frameH = (double)d->frameLines * d->rows * sy;
            lay->sx = sx;
            lay->sy = sy;
            lay->dest[2] = d->w * sx;
            lay->dest[3] = d->h * sy;
            lay->dest[0] = clampInt(roundInt((W - frameW) / 2.0 + (double)d->tvClock * sx / d->clocks), 0,
                                    W - lay->dest[2]);
            lay->dest[1] = clampInt(roundInt((H - frameH) / 2.0 + (double)d->tvLine * d->rows * sy), 0,
                                    H - lay->dest[3]);
            lay->clip[0] = roundInt((W - frameW) / 2.0);
            lay->clip[1] = roundInt((H - frameH) / 2.0);
            lay->clip[2] = roundInt(frameW);
            lay->clip[3] = roundInt(frameH);
            return;
        }
    }
    int32_t frame[4] = {0, 0, W, H};
    if (pr.scale != SCALE_STRETCH) {
        fitRect(W, H, 4, 3, frame);
    }
    lay->dest[0] = frame[0] + roundInt(d->tvClock * (double)frame[2] / 2560.0);
    lay->dest[1] = frame[1] + roundInt(d->tvLine * (double)frame[3] / d->frameLines);
    lay->dest[2] = roundInt(d->w * d->clocks * (double)frame[2] / 2560.0);
    lay->dest[3] = roundInt(lines * frame[3] / d->frameLines);
    memcpy(lay->clip, frame, sizeof(frame));
}

/* A wide aspect: a TV frame of that shape (1920 * w/h GPU clocks, the 4:3
 * frame's 2560 in its middle), and in it the picture with m pixels more on
 * each side (m 0: the 4:3 picture, the sides filled). */
static void wideLayout(const GpuDisplay* d, int W, int H, int m, Layout* lay) {
    double frameClocks = 1920.0 * pr.aspectW / pr.aspectH;
    double lines = (double)d->h / d->rows;
    int pw = d->w + 2 * m;
    double left = (frameClocks - 2560.0) / 2.0 + d->tvClock - (double)m * d->clocks;
    if (pr.scale == SCALE_INTEGER) {
        double want = (pw * d->clocks / 2560.0 * 4.0) / (lines / d->frameLines * 3.0);
        int sx, sy;
        if (integerFactors(W, H, pw, d->h, want, &sx, &sy)) {
            double frameW = frameClocks * sx / d->clocks;
            double frameH = (double)d->frameLines * d->rows * sy;
            double fx = (W - frameW) / 2.0, fy = (H - frameH) / 2.0;
            lay->sx = sx;
            lay->sy = sy;
            lay->dest[2] = pw * sx;
            lay->dest[3] = d->h * sy;
            lay->dest[0] = roundInt(fx + left * sx / d->clocks);
            lay->dest[1] = clampInt(roundInt(fy + (double)d->tvLine * d->rows * sy), 0, H - lay->dest[3]);
            lay->clip[0] = clampInt(roundInt(fx), 0, W);
            lay->clip[1] = clampInt(roundInt(fy), 0, H);
            lay->clip[2] = clampInt(roundInt(frameW), 0, W - lay->clip[0]);
            lay->clip[3] = clampInt(roundInt(frameH), 0, H - lay->clip[1]);
            return;
        }
    }
    int32_t frame[4] = {0, 0, W, H};
    if (pr.scale != SCALE_STRETCH) {
        fitRect(W, H, pr.aspectW, pr.aspectH, frame);
    }
    lay->dest[0] = frame[0] + roundInt(left * frame[2] / frameClocks);
    lay->dest[1] = frame[1] + roundInt(d->tvLine * (double)frame[3] / d->frameLines);
    lay->dest[2] = roundInt(pw * d->clocks * (double)frame[2] / frameClocks);
    lay->dest[3] = roundInt(lines * frame[3] / d->frameLines);
    memcpy(lay->clip, frame, sizeof(frame));
}

/* widefill=zoom: a frame that stays 4:3 in a wide frame, scaled up to the
 * frame's width (both directions alike), centred, its top and bottom cut by
 * the frame. Integer scale: the whole factors (in the picture's shape)
 * whose width comes closest to the wide picture's at that scale, never wider
 * than the window. */
static void zoomLayout(const GpuDisplay* d, int W, int H, Layout* lay) {
    wideLayout(d, W, H, 0, lay);
    if (pr.scale == SCALE_INTEGER && lay->sx > 0) {
        /* The wide picture this frame would be, at integer scale. */
        double m = (1920.0 * pr.aspectW / pr.aspectH / d->clocks - d->w) / 2.0;
        int mi = m > 0 ? (((int)ceil(m) + 3) & ~3) : 0;
        Layout wl;
        memset(&wl, 0, sizeof(wl));
        wideLayout(d, W, H, mi, &wl);
        if (wl.sx > 0) {
            double lines = (double)d->h / d->rows;
            double want = (d->w * d->clocks / 2560.0 * 4.0) / (lines / d->frameLines * 3.0);
            double k = want * d->h / d->w;
            int target = wl.dest[2], bestX = 0, bestY = 0, bestD = 1 << 30;
            for (int sy = 1; sy <= 64; sy++) {
                int base = (int)(sy * k), sx = 0;
                double e = 1e30;
                for (int c = base; c <= base + 1; c++) {
                    if (c < 1) {
                        continue;
                    }
                    double have = (double)c * d->w / ((double)sy * d->h);
                    double ce = have > want ? have / want - 1.0 : want / have - 1.0;
                    if (ce < e - 1e-9) {
                        e = ce;
                        sx = c;
                    }
                }
                int width = d->w * sx;
                if (sx < 1 || width > W) {
                    continue;
                }
                int dist = width > target ? width - target : target - width;
                if (dist < bestD || (dist == bestD && width > d->w * bestX)) {
                    bestD = dist;
                    bestX = sx;
                    bestY = sy;
                }
            }
            if (bestX > 0) {
                memcpy(lay->clip, wl.clip, sizeof(lay->clip));
                lay->sx = bestX;
                lay->sy = bestY;
                lay->dest[2] = d->w * bestX;
                lay->dest[3] = d->h * bestY;
                lay->dest[0] = lay->clip[0] + (lay->clip[2] - lay->dest[2]) / 2;
                lay->dest[1] = lay->clip[1] + (lay->clip[3] - lay->dest[3]) / 2;
                return;
            }
        }
        /* Below 1x: as fit. */
        memset(lay, 0, sizeof(*lay));
        int scale = pr.scale;
        pr.scale = SCALE_FIT;
        wideLayout(d, W, H, 0, lay);
        pr.scale = scale;
    }
    if (lay->dest[2] <= 0 || lay->clip[2] <= 0) {
        return;
    }
    double z = (double)lay->clip[2] / lay->dest[2];
    lay->sx = lay->sy = 0;
    lay->dest[2] = lay->clip[2];
    lay->dest[3] = roundInt(lay->dest[3] * z);
    lay->dest[0] = lay->clip[0];
    lay->dest[1] = lay->clip[1] + (lay->clip[3] - lay->dest[3]) / 2;
}

static void layoutFor(const GpuDisplay* d, int W, int H, Layout* lay) {
    memset(lay, 0, sizeof(*lay));
    lay->clip[2] = W;
    lay->clip[3] = H;
    const WideView* expanded = wideCurrentView();
    int originalH = d->h;
    GpuDisplay full = *d;
    if (expanded->wide && (expanded->top || expanded->bottom)) {
        full.h = expanded->h;
        full.tvLine -= expanded->top;
        d = &full;
    }
    int w = d->w, h = d->h;
    if (w <= 0 || h <= 0) {
        w = 320;
        h = 240;
    }
    if (pr.aspect == PRESENT_ASPECT_WIDE && d->clocks > 0 && d->w > 0 && d->h > 0) {
        const WideView* v = wideCurrentView();
        if (!v->wide && pr.fill == FILL_ZOOM) {
            zoomLayout(d, W, H, lay);
        } else {
            wideLayout(d, W, H, v->wide ? v->m : 0, lay);
        }
        return;
    }
    if (pr.aspect == PRESENT_ASPECT_4_3_TV && d->clocks > 0 && d->w > 0 && d->h > 0) {
        tvLayout(d, W, H, lay);
        return;
    }
    int num = 4 * (originalH > 0 ? originalH : h), den = 3 * h;
    if (pr.aspect == PRESENT_ASPECT_SQUARE) {
        num = w;
        den = h;
    }
    if (pr.scale == SCALE_STRETCH) {
        lay->dest[2] = W;
        lay->dest[3] = H;
        return;
    }
    if (pr.scale == SCALE_INTEGER) {
        int sx = 0, sy = 0;
        if (pr.aspect == PRESENT_ASPECT_SQUARE) {
            sx = sy = W / w < H / h ? W / w : H / h;
        } else {
            integerFactors(W, H, w, h, (4.0 * originalH) / (3.0 * h), &sx, &sy);
        }
        if (sx >= 1 && sy >= 1) {
            lay->sx = sx;
            lay->sy = sy;
            lay->dest[2] = w * sx;
            lay->dest[3] = h * sy;
            lay->dest[0] = (W - lay->dest[2]) / 2;
            lay->dest[1] = (H - lay->dest[3]) / 2;
            return;
        }
    }
    fitRect(W, H, num, den, lay->dest);
}

/* ---- the note ------------------------------------------------------------------------- */

/* A 5x7 font for the notes: upper case, digits and a little punctuation
 * (lower case is drawn as upper case; anything else as '?'). */
static const struct {
    char c;
    char rows[7][6];
} font[] = {
    {' ', {".....", ".....", ".....", ".....", ".....", ".....", "....."}},
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".###.", "#...#", "#....", ".###.", "....#", "#...#", ".###."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
    {':', {".....", "..#..", "..#..", ".....", "..#..", "..#..", "....."}},
    {'.', {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
    {',', {".....", ".....", ".....", ".....", ".##..", "..#..", ".#..."}},
    {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {'(', {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}},
    {')', {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}},
    {'/', {".....", "....#", "...#.", "..#..", ".#...", "#....", "....."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {'%', {"##...", "##..#", "...#.", "..#..", ".#...", "#..##", "...##"}},
    {'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
    {'<', {"...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#."}},
    {'>', {".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..."}},
    {'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
};
#define FONT_COUNT (int)(sizeof(font) / sizeof(font[0]))

static int glyphIndex(char c) {
    c = (char)toupper((unsigned char)c);
    for (int i = 0; i < FONT_COUNT; i++) {
        if (font[i].c == c) {
            return i;
        }
    }
    return FONT_COUNT - 1;
}

/* The note as a 1-bit picture (row after row, bit i of word i/32), at most
 * maxChars characters. Returns the words used, 0 if none fit. */
static int rasterNote(const char* text, int maxChars, uint32_t* bits, int cap, int* bw, int* bh) {
    int n = (int)strlen(text);
    if (n > maxChars) {
        n = maxChars;
    }
    if (n <= 0) {
        return 0;
    }
    *bw = n * 6 - 1 + 2 * NOTE_PAD;
    *bh = 7 + 2 * NOTE_PAD;
    int words = (*bw * *bh + 31) / 32;
    if (words > cap) {
        return 0;
    }
    memset(bits, 0, (size_t)words * 4);
    for (int i = 0; i < n; i++) {
        int g = glyphIndex(text[i]);
        for (int r = 0; r < 7; r++) {
            for (int c = 0; c < 5; c++) {
                if (font[g].rows[r][c] == '#') {
                    int bit = (NOTE_PAD + r) * *bw + NOTE_PAD + i * 6 + c;
                    bits[bit >> 5] |= 1u << (bit & 31);
                }
            }
        }
    }
    return words;
}

/* Lines of text ('\n' between them) as one 1-bit picture: each at most
 * maxChars characters, 9 font pixels apart; the last lines are left out when
 * the picture would not fit in cap words. Returns the words used, 0 if none. */
static int rasterLines(const char* text, int maxChars, uint32_t* bits, int cap, int* bw, int* bh) {
    const char* line[24];
    int len[24], n = 0, widest = 0;
    for (const char* p = text; *p && n < 24;) {
        const char* end = strchr(p, '\n');
        int l = end ? (int)(end - p) : (int)strlen(p);
        line[n] = p;
        len[n] = l > maxChars ? maxChars : l;
        widest = len[n] > widest ? len[n] : widest;
        n++;
        p += l + (end ? 1 : 0);
    }
    if (widest <= 0) {
        return 0;
    }
    *bw = widest * 6 - 1 + 2 * NOTE_PAD;
    for (; n > 0; n--) {
        *bh = n * 9 - 2 + 2 * NOTE_PAD;
        if ((*bw * *bh + 31) / 32 <= cap) break;
    }
    if (n == 0) {
        return 0;
    }
    int words = (*bw * *bh + 31) / 32;
    memset(bits, 0, (size_t)words * 4);
    for (int k = 0; k < n; k++) {
        for (int i = 0; i < len[k]; i++) {
            int g = glyphIndex(line[k][i]);
            for (int r = 0; r < 7; r++) {
                for (int c = 0; c < 5; c++) {
                    if (font[g].rows[r][c] == '#') {
                        int bit = (NOTE_PAD + k * 9 + r) * *bw + NOTE_PAD + i * 6 + c;
                        bits[bit >> 5] |= 1u << (bit & 31);
                    }
                }
            }
        }
    }
    return words;
}

/* port/src/ui/diagnostics.cpp: the performance overlay's lines, 0 when it is
 * off (weak: the GPU layer's test programs have no settings menu). */
extern int diagnosticsOverlayText(char* out, int size) __attribute__((weak));

static void note(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(pr.note, sizeof(pr.note), fmt, ap);
    va_end(ap);
    pr.noteLeft = NOTE_VBLANKS;
    fprintf(stderr, "present: %s\n", pr.note);
}

/* A short note in the window corner from another module (the disc swap key). */
void presentShowNote(const char* text) {
    note("%s", text);
}

void presentVBlank(void) {
    loadOptions();
    if (pr.noteLeft > 0) {
        pr.noteLeft--;
    }
}

/* ---- the present pass's constants ------------------------------------------------------- */

int presentConstants(const GpuDisplay* d, int W, int H, PresentConstants* pc, uint32_t* bits, int cap) {
    loadOptions();
    memset(pc, 0, sizeof(*pc));
    pc->disp[0] = d->x;
    pc->disp[1] = d->y;
    pc->disp[2] = d->w > 0 ? d->w : 1;
    pc->disp[3] = d->h > 0 ? d->h : 1;
    pc->rgb24 = d->rgb24;
    pc->enabled = d->enabled && d->w > 0 && d->h > 0;
    Layout lay;
    layoutFor(d, W, H, &lay);
    {
        const WideView* v = wideCurrentView();
        if (v->wide) {
            /* The wide picture (wide.c): the display area with its margins. */
            pc->src = 1;
            pc->disp[0] = v->x;
            pc->disp[1] = v->y;
            pc->disp[2] = v->w;
            pc->disp[3] = v->h;
        } else if (pr.aspect == PRESENT_ASPECT_WIDE) {
            pc->fill = pr.fill == FILL_BLUR;
        }
    }
    memcpy(pc->dest, lay.dest, sizeof(pc->dest));
    memcpy(pc->clip, lay.clip, sizeof(pc->clip));
    memcpy(pr.lastDest, lay.dest, sizeof(pr.lastDest));
    pr.lastSrcW = pc->disp[2];
    pr.lastSrcH = pc->disp[3];
    pr.lastMargin = pc->src ? wideCurrentView()->m : 0;
    pr.lastTop = pc->src ? wideCurrentView()->top : 0;
    /* Integer scaling stays exact: the smoothing filters are nearest there
     * (sharp is by itself); xbr is a whole-multiple scaler and stays. */
    int filter = d->rgb24 && pr.movieFilter != PRESENT_FILTER_SAME ? pr.movieFilter : pr.filter;
    if (lay.sx != 0 && filter != PRESENT_FILTER_XBR) {
        filter = PRESENT_FILTER_NEAREST;
    }
    pc->filterMode = filter;
    pc->picture = pc->codes = -1; /* vk.c: the picture prepass */
    if (d->w > 0 && d->h > 0) {
        pr.last = *d;
    }
    pr.lastTargetW = W;
    pr.lastTargetH = H;

    if (bits != NULL && cap > 0 && diagnosticsOverlayText != NULL) {
        char panel[1024];
        int at = 0;
        if (pr.noteLeft > 0 && pr.note[0] != '\0') {
            at = snprintf(panel, sizeof(panel), "%s\n", pr.note);
        }
        if (diagnosticsOverlayText(panel + at, (int)sizeof(panel) - at)) {
            /* A line about 1/60 of the window's height (whole font pixels), smaller if it would not fit across. */
            int widest = 0;
            for (const char* p = panel; *p;) {
                const char* end = strchr(p, '\n');
                int l = end ? (int)(end - p) : (int)strlen(p);
                widest = l > widest ? l : widest;
                p += l + (end ? 1 : 0);
            }
            int scale = H / 540 > 1 ? H / 540 : 1;
            while (scale > 1 && (widest * 6 - 1 + 2 * NOTE_PAD + 2 * NOTE_MARGIN) * scale > W) {
                scale--;
            }
            int maxChars = (W / scale - 2 * NOTE_MARGIN - 2 * NOTE_PAD + 1) / 6;
            int bw, bh;
            int words = rasterLines(panel, maxChars, bits, cap, &bw, &bh);
            if (words > 0) {
                pc->osd[0] = NOTE_MARGIN * scale;
                pc->osd[1] = NOTE_MARGIN * scale;
                pc->osd[2] = bw * scale;
                pc->osd[3] = bh * scale;
                pc->osdScale = scale;
                pc->osdStride = bw;
                return words;
            }
        }
    }
    if (pr.noteLeft <= 0 || pr.note[0] == '\0') {
        return 0;
    }
    /* About 1/40 of the window's height, smaller if the note would not fit
     * across; the smallest size cuts it short. */
    int len = (int)strlen(pr.note);
    int scale = H / 300 > 1 ? H / 300 : 1;
    while (scale > 1 && (len * 6 - 1 + 2 * NOTE_PAD + 2 * NOTE_MARGIN) * scale > W) {
        scale--;
    }
    int maxChars = (W / scale - 2 * NOTE_MARGIN - 2 * NOTE_PAD + 1) / 6;
    int bw, bh;
    int words = rasterNote(pr.note, maxChars, bits, cap, &bw, &bh);
    if (words == 0) {
        return 0;
    }
    pc->osd[0] = NOTE_MARGIN * scale;
    pc->osd[1] = NOTE_MARGIN * scale;
    pc->osd[2] = bw * scale;
    pc->osd[3] = bh * scale;
    pc->osdScale = scale;
    pc->osdStride = bw;
    return words;
}

int presentWindowToDisplay(float x, float y, float* dx, float* dy) {
    if (pr.lastDest[2] <= 0 || pr.lastDest[3] <= 0 || pr.lastSrcW <= 0 || pr.lastSrcH <= 0) {
        return 0;
    }
    /* The present pass maps the source rectangle linearly onto lastDest. */
    *dx = (x - (float)pr.lastDest[0]) * (float)pr.lastSrcW / (float)pr.lastDest[2] - (float)pr.lastMargin;
    *dy = (y - (float)pr.lastDest[1]) * (float)pr.lastSrcH / (float)pr.lastDest[3] - (float)pr.lastTop;
    return 1;
}

/* ---- hotkeys ----------------------------------------------------------------------------- */

void presentCycleAspect(void) {
    loadOptions();
    const char* order[PRESET_COUNT + 1];
    int n = 0;
    for (int i = 0; i < PRESET_COUNT; i++) {
        order[n++] = aspectPresets[i];
    }
    if (pr.custom[0] != '\0') {
        order[n++] = pr.custom;
    }
    int next = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(order[i], pr.aspectText) == 0) {
            next = (i + 1) % n;
        }
    }
    int kind = PRESENT_ASPECT_4_3_TV, w = 4, h = 3;
    parseAspect(order[next], &kind, &w, &h);
    setAspect(kind, w, h);
    settingsSet("aspect", pr.aspectText);
    settingsSave();
    if (kind == PRESENT_ASPECT_4_3_TV) {
        note("ASPECT 4:3 TV");
    } else if (kind == PRESENT_ASPECT_4_3_FILL) {
        note("ASPECT 4:3 FILL");
    } else if (kind == PRESENT_ASPECT_SQUARE) {
        note("ASPECT 1:1 (SQUARE PIXELS)");
    } else {
        note("ASPECT %s", pr.aspectText);
    }
}

void presentCycleScale(void) {
    loadOptions();
    static const int order[] = {SCALE_FIT, SCALE_INTEGER, SCALE_STRETCH};
    int next = 0;
    for (int i = 0; i < 3; i++) {
        if (order[i] == pr.scale) {
            next = order[(i + 1) % 3];
        }
    }
    pr.scale = next;
    settingsSet("scale", scaleNames[next]);
    settingsSave();
    if (next != SCALE_INTEGER) {
        note("SCALE %s", next == SCALE_FIT ? "FIT" : "STRETCH");
        return;
    }
    Layout lay;
    layoutFor(&pr.last, pr.lastTargetW, pr.lastTargetH, &lay);
    if (lay.sx == 0) {
        note("SCALE INTEGER: WINDOW BELOW 1X, FIT");
    } else if (lay.sx == lay.sy) {
        note("SCALE INTEGER %dX", lay.sx);
    } else {
        note("SCALE INTEGER %dX ACROSS, %dX DOWN", lay.sx, lay.sy);
    }
}

/* Linear and bicubic are nearest in integer scale (sharp is by itself). */
static int smoothing(int filter) {
    return filter == PRESENT_FILTER_LINEAR || filter == PRESENT_FILTER_BICUBIC;
}

void presentCycleFilter(void) {
    loadOptions();
    presentSetFilter((pr.filter + 1) % PRESENT_FILTER_COUNT);
    note(smoothing(pr.filter) && pr.scale == SCALE_INTEGER ? "FILTER %s (NOT IN INTEGER SCALE)" : "FILTER %s",
         filterNotes[pr.filter]);
}

void presentCycleFill(void) {
    loadOptions();
    pr.fill = (pr.fill + 1) % 3;
    settingsSet("widefill", fillNames[pr.fill]);
    settingsSave();
    note(pr.fill == FILL_BARS ? "WIDE FILL BARS" : pr.fill == FILL_ZOOM ? "WIDE FILL ZOOM" : "WIDE FILL BLUR");
}

void presentToggleFullscreen(void) {
    loadOptions();
    if (pr.window == NULL) {
        return;
    }
    int on = !(SDL_GetWindowFlags(pr.window) & SDL_WINDOW_FULLSCREEN);
    if (!presentSetFullscreen(on)) {
        note("FULLSCREEN FAILED");
        return;
    }
    note(on ? "FULLSCREEN" : "WINDOWED");
}

/* ---- the settings menu (port/src/ui) ---------------------------------------------------- */

const char* presentAspectText(void) {
    loadOptions();
    return pr.aspectText;
}

int presentSetAspectText(const char* text) {
    loadOptions();
    int kind, w, h;
    if (!parseAspect(text, &kind, &w, &h)) {
        return 0;
    }
    setAspect(kind, w, h);
    int preset = 0;
    for (int i = 0; i < PRESET_COUNT; i++) {
        preset |= strcmp(pr.aspectText, aspectPresets[i]) == 0;
    }
    if (!preset) {
        snprintf(pr.custom, sizeof(pr.custom), "%s", pr.aspectText); /* F3 keeps it in its cycle */
    }
    settingsSet("aspect", pr.aspectText);
    settingsSave();
    fprintf(stderr, "present: aspect %s\n", pr.aspectText);
    return 1;
}

int presentScaleMode(void) {
    loadOptions();
    return pr.scale;
}

void presentSetScale(int mode) {
    loadOptions();
    if (mode >= SCALE_FIT && mode <= SCALE_STRETCH) {
        pr.scale = mode;
        settingsSet("scale", scaleNames[mode]);
        settingsSave();
    }
}

int presentFilter(void) {
    loadOptions();
    return pr.filter;
}

void presentSetFilter(int filter) {
    loadOptions();
    if (filter >= 0 && filter < PRESENT_FILTER_COUNT) {
        pr.filter = filter;
        settingsSet("present_filter", filterNames[filter]);
        settingsSave();
    }
}

int presentMovieFilter(void) {
    loadOptions();
    return pr.movieFilter;
}

void presentSetMovieFilter(int filter) {
    loadOptions();
    if (filter >= PRESENT_FILTER_SAME && filter < PRESENT_FILTER_COUNT) {
        pr.movieFilter = filter;
        settingsSet("present_movie_filter", filter == PRESENT_FILTER_SAME ? "same" : filterNames[filter]);
        settingsSave();
    }
}

int presentFillMode(void) {
    loadOptions();
    return pr.fill;
}

void presentSetFill(int mode) {
    loadOptions();
    if (mode >= FILL_BARS && mode <= FILL_BLUR) {
        pr.fill = mode;
        settingsSet("widefill", fillNames[mode]);
        settingsSave();
    }
}

int presentSetFullscreen(int on) {
    loadOptions();
    if (pr.window == NULL) {
        return 0;
    }
    if (!SDL_SetWindowFullscreen(pr.window, on != 0)) {
        fprintf(stderr, "present: SDL_SetWindowFullscreen: %s\n", SDL_GetError());
        return 0;
    }
    settingsSet("fullscreen", on ? "1" : "0");
    settingsSave();
    return 1;
}

void presentSetWindowSize(int w, int h) {
    loadOptions();
    w = clampInt(w, 64, 16384);
    h = clampInt(h, 64, 16384);
    if (pr.window != NULL && !(SDL_GetWindowFlags(pr.window) & SDL_WINDOW_FULLSCREEN)) {
        if (SDL_GetWindowFlags(pr.window) & SDL_WINDOW_MAXIMIZED) {
            SDL_RestoreWindow(pr.window); /* a maximised window keeps its size otherwise */
        }
        SDL_SetWindowSize(pr.window, w, h);
    }
    /* Kept now: the resize event may not come (a tiling desktop, fullscreen). */
    settingsSetInt("window_width", w);
    settingsSetInt("window_height", h);
    settingsSave();
}

SDL_Window* presentWindow(void) {
    return pr.window;
}

/* ---- the window ------------------------------------------------------------------------ */

SDL_Window* presentCreateWindow(const char* title) {
    loadOptions();
    int w = clampInt(settingsInt("window_width", 960), 64, 16384);
    int h = clampInt(settingsInt("window_height", 720), 64, 16384);
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetStringProperty(p, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h);
    if (settingsGet("window_x") != NULL && settingsGet("window_y") != NULL) {
        /* Only where a display still shows some of it. */
        SDL_Rect r = {settingsInt("window_x", 0), settingsInt("window_y", 0), w, h};
        SDL_Rect bounds;
        SDL_DisplayID id = SDL_GetDisplayForRect(&r);
        if (id != 0 && SDL_GetDisplayBounds(id, &bounds) && SDL_HasRectIntersection(&r, &bounds)) {
            SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_X_NUMBER, r.x);
            SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_Y_NUMBER, r.y);
        }
    }
    SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
    /* The display's own pixels on a scaled desktop, so the compositor never
     * rescales the picture (integer scaling stays exact). */
    SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    pr.window = SDL_CreateWindowWithProperties(p);
    SDL_DestroyProperties(p);
    pr.createdW = w; pr.createdH = h; pr.createdNs = SDL_GetTicksNS(); pr.sizeAsserted = 0;
    if (pr.window != NULL && settingsBool("fullscreen", 0)) {
        /* After creation, as F11 does: a window created fullscreen came up
         * at its windowed size under KWin (Wayland). */
        SDL_SetWindowFullscreen(pr.window, true);
        SDL_SyncWindow(pr.window);
    }
    if (pr.window != NULL) {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(pr.window, &pw, &ph);
        fprintf(stderr, "present: window %dx%d (%dx%d pixels, %s)%s; aspect %s, scale %s, filter %s, movies %s\n", w, h,
                pw, ph, SDL_GetCurrentVideoDriver(), settingsBool("fullscreen", 0) ? ", fullscreen" : "", pr.aspectText,
                scaleNames[pr.scale], filterNames[pr.filter],
                pr.movieFilter == PRESENT_FILTER_SAME ? "same" : filterNames[pr.movieFilter]);
    }
    return pr.window;
}

static void keepInt(const char* key, int value) {
    if (settingsGet(key) == NULL || settingsInt(key, 0) != value) {
        settingsSetInt(key, value);
    }
}

void presentWindowEvent(const SDL_Event* e) {
    if (pr.window == NULL || pr.headless || e->window.windowID != SDL_GetWindowID(pr.window)) {
        return;
    }
    SDL_WindowFlags flags = SDL_GetWindowFlags(pr.window);
    int normal = !(flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED));
    /* Only real changes: the window as created matches the settings already,
     * and an XG_ override it was created from must not reach the file. */
    switch (e->type) {
    case SDL_EVENT_WINDOW_RESIZED:
        /* KWin (Plasma 6.7, Wayland) opens a window at a size of its own,
         * half the work area or the app's last, whatever the client asked:
         * a 12x picture squeezed into a third of the screen. A toplevel that
         * is neither maximized nor fullscreen may pick its own size, and KWin
         * keeps one asked for after its configure (xg-kma), so the size from
         * the settings is asked for once more; a tiling script that then
         * resizes again wins, and its size is kept as before. */
        if (normal && !pr.sizeAsserted && SDL_GetTicksNS() - pr.createdNs < UINT64_C(2000000000) &&
            (e->window.data1 != pr.createdW || e->window.data2 != pr.createdH)) {
            pr.sizeAsserted = 1;
            fprintf(stderr, "present: the compositor sized the window %dx%d; asking for the settings' %dx%d again\n",
                    (int)e->window.data1, (int)e->window.data2, pr.createdW, pr.createdH);
            SDL_SetWindowSize(pr.window, pr.createdW, pr.createdH);
            break;
        }
        if (normal) {
            keepInt("window_width", e->window.data1);
            keepInt("window_height", e->window.data2);
        }
        break;
    case SDL_EVENT_WINDOW_MOVED:
        /* Wayland windows have no position the program can see or set. */
        if (normal && SDL_strcmp(SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "", "wayland") != 0) {
            keepInt("window_x", e->window.data1);
            keepInt("window_y", e->window.data2);
        }
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        /* The desktop's own fullscreen control counts too. */
        if (settingsBool("fullscreen", 0) != (e->type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN)) {
            settingsSet("fullscreen", e->type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ? "1" : "0");
        }
        break;
    default:
        break;
    }
}
