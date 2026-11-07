/*
 * True widescreen: the wide picture beside VRAM (include/pc_wide.h has the
 * game's side).
 *
 * With a wide aspect (present.c: aspect=16:9, 21:9, 32:9, w:h), every
 * drawing command whose draw area is a frame buffer is executed twice by
 * gp0.c: once into VRAM as always, and once into the wide target with the
 * drawing offset moved right by the margin M and the draw area widened by M
 * on each side. The GTE is unchanged, so the margins show what the camera
 * sees beside the 4:3 picture; the centre of the wide picture is the 4:3
 * picture pixel for pixel (the triangle walker is exact under whole-pixel
 * shifts), and VRAM never changes.
 *
 * The wide target (gpu_internal.h: WIDE_W x WIDE_H, R16 like VRAM):
 *   - rows 0..511: frame buffer F's pixel at VRAM (x, y) is at (x - F.x0 + M,
 *     y + F.dy). Expanded vertical framing packs each buffer into its own
 *     256-line slot, with extra scene rows above and below. Frame buffers are the full-screen draw areas at the display's x
 *     (buffers stacked vertically, as every Xenogears mode has them);
 *   - rows 512..: screen captures. A MoveImage of a whole frame buffer to
 *     texture space keeps the wide picture in slot k at (0, 512 + 256k), so
 *     a MoveImage back (menus over the field) or a primitive textured from
 *     the capture (transitions) can use the margins too. A whole-screen
 *     capture of a picture without margins (a movie's last frame, a picture
 *     made on the CPU) gets black ones: what the 4:3 picture showed beside it.
 *
 * Each frame buffer starts a frame when it is first drawn after it was
 * displayed: its margins are cleared (the game's clears reach them only
 * when they fill the whole width), and it is "clean". Pictures that come
 * from VRAM rather than drawing (a LoadImage, a MoveImage from outside the
 * frame buffers, drawing with a draw area that is no frame buffer) are
 * copied into the centre; when they replace whole rows, the margins there
 * take each row's colour when it has one all across (a cleared background),
 * else black. A display that shows nothing drawn wide (a screen's first
 * frame, a picture loaded straight into it) is shown from VRAM the same way.
 * Things the wide picture cannot follow make it unclean, and the
 * present pass then shows the 4:3 picture with filled sides instead: a
 * primitive textured from a capture with margins or from a frame buffer,
 * unless the game marked it PC_WIDE_HANDLED (it draws the wide version
 * itself).
 *
 * Margins and policy: M comes from the aspect setting, the display's width
 * and the TV frame (the 4:3 TV frame is 2560 GPU clocks wide; a w:h frame is
 * 1920 * w/h), rounded up to a multiple of 4 so dithering stays aligned. M is
 * 0 (the frame is drawn exactly as on the PS1, nothing extra) unless the
 * aspect is wide, the display is 15-bit and 256..640 pixels wide, the kind
 * of screen the game shows (pcWideSetMode) is ready for wide, and nothing
 * blocks it (pcWideBlock). The game's culling widens by pcWideCull = M + a
 * guard band (XG_WIDE_GUARD, default 128 pixels).
 *
 * Environment:
 *   XG_WIDE_MODES=all|none|<kinds>   which kinds may go wide (kernel, field,
 *                          battle, worldmap, battling, menu, movie; comma
 *                          separated); default: wideReadyDefault below
 *   XG_WIDE_GUARD=<n>      the cull margin's guard band (default 128)
 *   XG_WIDE_LOG=1          log frame buffers, captures, unclean frames and
 *                          what each display shows
 *   XG_WIDE_NOSHIFT=1      ignore pcWidePrim's dx (2D stays where the 4:3
 *                          picture has it), so tools/widecheck.py can compare
 *                          the whole centre
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gpu_internal.h"
#include "../system/savestate.h"
#include "texture.h"
#include "pc_wide.h"
#include "pc_geometry.h"

/* The kinds whose wide picture is ready (pcWideSetMode). A mode's agent adds
 * its kind here when its culling, 2D layout and effects are done. */
static const char* const wideReadyDefault = "kernel,battle,menu,field,battling,worldmap";

static const char* const kindNames[PC_WIDE_KINDS] = {"kernel", "field", "battle", "worldmap",
                                                       "battling", "menu", "movie"};

int pcWideView;
int pcWideCull;
int pcVerticalCull;
int pcWideOnlyScope;
int wideSlotsLive;

typedef struct {
    int used;
    VRect r;       /* in VRAM */
    int m;         /* horizontal margin */
    int active;    /* a display copy exists, including at horizontal margin 0 */
    int top, bottom, dy; /* extra draw rows and native-to-copy Y offset */
    int vertical;  /* this frame has extra scene lines */
    int kind;      /* mode that defined this framebuffer's dimensions */
    int needBegin; /* displayed since it was last drawn */
    int clean, shifted;
    /* Where the shifted centre differs from VRAM (frame buffer pixels):
     * nMask rectangles, or all of it when maskAll (shifted by a copy whose
     * places are not known). tools/widecheck.py compares the rest. */
    int maskAll, nMask;
    VRect mask[WIDE_MASKS];
    const char* why;
    int hasDirty; /* VRAM drawn by commands the wide target did not get */
    VRect dirty;
} WideFb;

typedef struct {
    int used;
    VRect r; /* the capture in VRAM */
    int m;   /* its margins in the slot (0: none) */
    int row; /* its first native row in the target */
    int top, bottom; /* extra captured rows around the native rectangle */
    int shifted; /* its centre is not VRAM's (made from a shifted frame) */
    int narrow;  /* made from a picture without margins: black ones */
    int whole;   /* a whole screen (with margins), not part of one */
    unsigned age; /* when it was made */
} WideSlot;

#define MAX_FBS 4
#define PACKETS 65536 /* a power of 2 */
#define ARENAS 3
#define ARENA_BYTES (2 << 20)

static struct {
    int m;          /* the margin of the frame being drawn */
    int guard;
    int noShift; /* XG_WIDE_NOSHIFT: pcWidePrim's dx ignored (validation) */
    int logMoves;
    unsigned vblank;
    int kind;
    unsigned ready; /* bit per kind */
    unsigned block; /* PC_WIDE_BLOCK_* */
    int keepMargins; /* pcWideKeepMargins */
    int narrowBackdrop; /* pcWideNarrowBackdrop */
    int log;
    GpuDisplay disp; /* the last display */
    WideFb fb[MAX_FBS];
    WideSlot slot[WIDE_SLOTS];
    unsigned slotAge;
    WideView view;
    /* pcWidePrim's table: packet address -> treatment. */
    const void* key[PACKETS];
    WidePacket val[PACKETS];
    int used[PACKETS];
    int count;
    /* pcWideAlloc */
    _Alignas(16) uint8_t arenaMem[ARENAS][ARENA_BYTES];
    size_t arenaUsed[ARENAS];
    int arena;
} wd;

/* ---- setup and policy ------------------------------------------------------------ */

static void parseModes(const char* s) {
    wd.ready = 0;
    if (strcmp(s, "all") == 0) {
        wd.ready = (1u << PC_WIDE_KINDS) - 1;
        return;
    }
    if (strcmp(s, "none") == 0) {
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", s);
    for (char* t = strtok(buf, ", "); t != NULL; t = strtok(NULL, ", ")) {
        int found = 0;
        for (int k = 0; k < PC_WIDE_KINDS; k++) {
            if (strcmp(t, kindNames[k]) == 0) {
                wd.ready |= 1u << k;
                found = 1;
            }
        }
        if (!found) {
            gpuLog("wide %u: XG_WIDE_MODES: unknown kind '%s'", wd.vblank, t);
        }
    }
}

void wideReset(void) {
    gvkHiEffectReset();
    memset(&wd, 0, sizeof(wd));
    pcWideView = pcWideCull = pcVerticalCull = 0;
    wideSlotsLive = 0;
    const char* e = getenv("XG_WIDE_MODES");
    parseModes(e != NULL && e[0] != '\0' ? e : wideReadyDefault);
    e = getenv("XG_WIDE_GUARD");
    wd.guard = e != NULL && e[0] != '\0' ? atoi(e) : 128;
    if (wd.guard < 0) {
        wd.guard = 0;
    }
    e = getenv("XG_WIDE_LOG");
    wd.log = e != NULL && e[0] != '\0' && strcmp(e, "0") != 0;
    e = getenv("XG_WIDE_NOSHIFT");
    wd.noShift = e != NULL && e[0] != '\0' && strcmp(e, "0") != 0;
    wd.kind = PC_WIDE_KERNEL;
}

static int marginFor(const GpuDisplay* d);

/* Menus retain the extra rows captured from their field, world map or title
 * backdrop. Movies keep their authored framing. */
static int verticalFor(const GpuDisplay* d) {
    return presentExpandedVertical() &&
           (wd.kind == PC_WIDE_FIELD || wd.kind == PC_WIDE_BATTLE || wd.kind == PC_WIDE_MENU ||
            wd.kind == PC_WIDE_WORLDMAP) &&
           wd.block == 0 && d->enabled && !d->rgb24 && d->rows == 1 && d->frameLines == 240 &&
           d->w >= 256 && d->w <= 640 && d->h >= 192 && d->h < 240 &&
           d->tvLine >= 0 && d->tvLine + d->h <= 240;
}

int wideActive(void) { return wd.m != 0 || verticalFor(&wd.disp); }

/* A new margin now (a mode or block change takes effect at once, so the
 * new screen's first frame is drawn with it). */
static void setMargin(int m) {
    if (m != wd.m) gvkHiEffectReset();
    if (m != wd.m && wd.log) {
        gpuLog("wide %u: margin %d (display %d,%d %dx%d%s, kind %s)", wd.vblank, m, wd.disp.x, wd.disp.y, wd.disp.w, wd.disp.h,
               wd.disp.rgb24 ? " 24-bit" : "", kindNames[wd.kind]);
    }
    wd.m = m;
    pcWideView = m;
    pcWideCull = m > 0 ? m + wd.guard : 0;
    pcVerticalCull = verticalFor(&wd.disp) ? 32 : 0;
}

int pcWideGetMode(void) { return wd.kind; }

int pcWideSetMode(int kind) {
    int old = wd.kind;
    if (kind >= 0 && kind < PC_WIDE_KINDS) {
        wd.kind = kind;
        textureDumpMode(kind);
        setMargin(marginFor(&wd.disp));
    }
    return old;
}

void pcWideBlock(unsigned int reason, int on) {
    if (on) {
        wd.block |= reason;
    } else {
        wd.block &= ~reason;
    }
    setMargin(marginFor(&wd.disp));
}

/* ---- the opt-out list (pcWideOptOut) ---------------------------------------------- */

/* Lines "<kind> <id> <reason>"; '#' starts a comment. */
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#endif
static const char optOutBuiltin[] = {
#embed "wide_optout.txt" suffix(, )
    0};
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#define OPTOUT_MAX 1024
static struct {
    int loaded, n;
    struct {
        short kind, id;
        char why[96];
    } e[OPTOUT_MAX];
} optOut;

static void optOutParse(const char* s) {
    while (*s != '\0') {
        const char* end = strchr(s, '\n');
        size_t len = end != NULL ? (size_t)(end - s) : strlen(s);
        char line[256], kind[32];
        int id, used = 0;
        snprintf(line, sizeof(line), "%.*s", (int)(len < sizeof(line) ? len : sizeof(line) - 1), s);
        s += len + (end != NULL);
        if (sscanf(line, " %31s %d %n", kind, &id, &used) < 2 || kind[0] == '#' || optOut.n >= OPTOUT_MAX) {
            continue;
        }
        for (int k = 0; k < PC_WIDE_KINDS; k++) {
            if (strcmp(kind, kindNames[k]) == 0) {
                optOut.e[optOut.n].kind = (short)k;
                optOut.e[optOut.n].id = (short)id;
                snprintf(optOut.e[optOut.n].why, sizeof(optOut.e[0].why), "%s", line + used);
                optOut.n++;
            }
        }
    }
}

const char* pcWideOptOut(int kind, int id) {
    if (!optOut.loaded) {
        optOut.loaded = 1;
        const char* e = getenv("XG_WIDE_OPTOUT");
        if (e == NULL || e[0] == '\0') {
            optOutParse(optOutBuiltin);
        } else if (strcmp(e, "none") != 0) {
            FILE* f = fopen(e, "rb");
            char* text = NULL;
            long size = 0;
            if (f != NULL && fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0 &&
                (text = calloc((size_t)size + 1, 1)) != NULL && fread(text, 1, (size_t)size, f) == (size_t)size) {
                optOutParse(text);
            } else {
                gpuLog("wide: XG_WIDE_OPTOUT: cannot read %s", e);
            }
            free(text);
            if (f != NULL) {
                fclose(f);
            }
        }
    }
    for (int i = 0; i < optOut.n; i++) {
        if (optOut.e[i].kind == kind && optOut.e[i].id == id) {
            return optOut.e[i].why;
        }
    }
    return NULL;
}

int wideMargin(void) {
    return wd.m;
}

/* M for a display: the w:h frame's width in GPU clocks, less the picture's,
 * halved, in pixels. */
static int marginFor(const GpuDisplay* d) {
    int aw, ah;
    if (presentAspect(&aw, &ah) != PRESENT_ASPECT_WIDE || !(wd.ready & (1u << wd.kind)) || wd.block != 0) {
        return 0;
    }
    if (!d->enabled || d->rgb24 || d->w < 256 || d->w > 640 || d->clocks <= 0) {
        return 0;
    }
    double frameClocks = 1920.0 * aw / ah;
    double m = (frameClocks / d->clocks - d->w) / 2.0;
    if (m <= 0) {
        return 0;
    }
    int mi = ((int)ceil(m) + 3) & ~3;
    int cap = ((WIDE_FRAME_W - d->w) / 2) & ~3;
    return mi < cap ? mi : cap;
}

/* ---- frame buffers --------------------------------------------------------------- */

static int overlaps(VRect a, VRect b) {
    return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1;
}

static int inside(VRect a, VRect b) {
    return a.x0 >= b.x0 && a.x1 <= b.x1 && a.y0 >= b.y0 && a.y1 <= b.y1;
}

static int fbWidth(const WideFb* f) {
    return f->r.x1 - f->r.x0 + 1;
}

/* A full-screen area at the display's x: a frame buffer. */
static int candidate(VRect r) {
    int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1;
    int dw = wd.disp.w > 0 ? wd.disp.w : 320;
    /* Not taller than the display (a clear over both buffers of a 218-line
     * mode would join them into one, whose margins the next frame clears). */
    int dh = wd.disp.h > 256 ? wd.disp.h : 256;
    return r.x0 == (wd.disp.w > 0 ? wd.disp.x : 0) && (w == dw || w == 320) && h >= 192 && h <= dh && w <= 640;
}

static WideFb* findFb(VRect r, int create) {
    int widest = wd.disp.w > 320 ? wd.disp.w : 320;
    for (int i = 0; i < MAX_FBS; i++) {
        if (create && candidate(r) && wd.fb[i].used && wd.fb[i].kind != wd.kind &&
            wd.fb[i].r.y0 == r.y0 && wd.fb[i].r.y1 != r.y1) wd.fb[i].used = 0;
        if (wd.fb[i].used && fbWidth(&wd.fb[i]) > widest) {
            /* A wider mode's (a 640-wide screen's, the display is narrower
             * now): its draw areas are not this mode's frame buffers. */
            wd.fb[i].used = 0;
            if (wd.log) {
                gpuLog("wide %u: frame buffer (%d,%d) %d wide dropped", wd.vblank, wd.fb[i].r.x0, wd.fb[i].r.y0,
                       fbWidth(&wd.fb[i]));
            }
        }
        if (wd.fb[i].used && inside(r, wd.fb[i].r)) {
            return &wd.fb[i];
        }
    }
    if (!create || !candidate(r)) {
        return NULL;
    }
    WideFb* f = NULL;
    for (int i = 0; i < MAX_FBS; i++) {
        /* Another mode's buffers on the same rows go. */
        if (wd.fb[i].used && overlaps(wd.fb[i].r, (VRect){0, r.y0, WIDE_FRAME_W - 1, r.y1})) {
            WideFb* old = &wd.fb[i];
            int shownEnd = wd.disp.y + wd.disp.h - 1;
            /* Field -> map creates the back buffer at y=216 while the
             * field's 224-row front buffer is still displayed at y=0.
             * Only its eight hidden native rows overlap. Its expanded
             * picture lives in the other 256-row slot: retain that front
             * picture until the map actually replaces it, instead of
             * briefly falling back to the smaller native view. */
            if (old->active && old->vertical && old->clean && old->kind != wd.kind &&
                wd.disp.enabled && old->r.y0 == wd.disp.y && old->r.y0 == 0 &&
                shownEnd < r.y0 && shownEnd < old->r.y1 && r.y0 > 0 &&
                inside((VRect){wd.disp.x, wd.disp.y, wd.disp.x + wd.disp.w - 1, shownEnd}, old->r)) {
                old->bottom += old->r.y1 - shownEnd;
                old->r.y1 = shownEnd;
                continue;
            }
            wd.fb[i].used = 0;
        }
    }
    for (int i = 0; i < MAX_FBS && f == NULL; i++) {
        if (!wd.fb[i].used) {
            f = &wd.fb[i];
        }
    }
    if (f == NULL) {
        f = &wd.fb[0];
    }
    memset(f, 0, sizeof(*f));
    f->used = 1;
    f->kind = wd.kind;
    f->r = r;
    f->needBegin = 1;
    if (wd.log) {
        gpuLog("wide %u: frame buffer (%d,%d) %dx%d", wd.vblank, r.x0, r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1);
    }
    return f;
}

/* Whether r overlaps a frame buffer. */
static int overFb(VRect r) {
    for (int i = 0; i < MAX_FBS; i++) {
        if (wd.fb[i].used && overlaps(wd.fb[i].r, r)) {
            return 1;
        }
    }
    return 0;
}

static void unclean(WideFb* f, const char* why) {
    if (f->clean && wd.log) {
        gpuLog("wide %u: frame buffer (%d,%d) 4:3 this frame: %s", wd.vblank, f->r.x0, f->r.y0, why);
    }
    if (f->clean) {
        f->why = why;
    }
    f->clean = 0;
}

/* The extra scene rows f gets above and below it this frame (expanded
 * vertical framing), and its rows' offset in the wide target. */
static void frameLines(const WideFb* f, int* top, int* bottom, int* dy) {
    int h = f->r.y1 - f->r.y0 + 1;
    *top = *bottom = *dy = 0;
    if (verticalFor(&wd.disp)) {
        *top = wd.disp.tvLine;
        *bottom = 240 - *top - h;
        if (*bottom < 0) *bottom = 0;
        if (h + *top + *bottom <= 252) {
            /* Battle buffers touch at y=216; fields use y=256. Each gets
             * its own 256-line slot, so neither expansion can touch the other. */
            *dy = ((f->r.y0 == 0 ? 0 : 256) + *top - f->r.y0 + 3) & ~3;
            /* Whole multiples of four preserve the native dither phase. */
        } else *top = *bottom = 0;
    }
}

void pcWideFrameLines(int x, int y, int* top, int* bottom) {
    WideFb* f = wideActive() ? findFb((VRect){x, y, x, y}, 0) : NULL;
    int dy;
    *top = *bottom = 0;
    if (f != NULL) {
        frameLines(f, top, bottom, &dy);
    }
}

/* A new frame in f: margins cleared, and (with a new margin) the centre put
 * back in step with VRAM. */
static void begin(WideFb* f) {
    int w = fbWidth(f), h = f->r.y1 - f->r.y0 + 1;
    int m = wd.m, top, bottom, dy;
    frameLines(f, &top, &bottom, &dy);
    int changed = !f->active || f->m != m || f->dy != dy || f->top != top || f->bottom != bottom;
    if (changed) {
        gvkCopy(0, f->r.x0, f->r.y0, 1, m, f->r.y0 + dy, w, h);
        f->hasDirty = 0;
    }
    if (!wd.keepMargins || changed) {
        gp0WideFill((VRect){0, f->r.y0 + dy - top, m - 1, f->r.y1 + dy + bottom}, 0);
        gp0WideFill((VRect){m + w, f->r.y0 + dy - top, w + 2*m - 1, f->r.y1 + dy + bottom}, 0);
        gp0WideFill((VRect){m, f->r.y0 + dy - top, m + w - 1, f->r.y0 + dy - 1}, 0);
        gp0WideFill((VRect){m, f->r.y1 + dy + 1, m + w - 1, f->r.y1 + dy + bottom}, 0);
        f->shifted = 0;
        f->maskAll = 0;
        f->nMask = 0;
    }
    f->m = m;
    f->top = top;
    f->bottom = bottom;
    f->dy = dy;
    f->active = 1;
    f->vertical = top != 0 || bottom != 0;
    f->needBegin = 0;
    f->clean = 1;
    f->why = NULL;
}

static VRect clipTo(VRect a, VRect b) {
    VRect r = {a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1,
               a.y1 < b.y1 ? a.y1 : b.y1};
    return r;
}

/* VRAM rectangle r (inside f) copied to the wide picture's centre. */
static void mirror(WideFb* f, VRect r) {
    gvkCopy(0, r.x0, r.y0, 1, r.x0 - f->r.x0 + f->m, r.y0 + f->dy, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1);
}

/* What commands the wide target did not get drew in f, into its centre,
 * before anything else draws or reads it there. */
static void flushDirty(WideFb* f) {
    if (f != NULL && f->hasDirty) {
        f->hasDirty = 0;
        f->vertical = 0; /* a draw without extra lines cannot supply them */
        if (f->active) {
            mirror(f, f->dirty);
        }
    }
}

void wideNoteDraw(VRect r) {
    if (!wideActive()) {
        return;
    }
    for (int i = 0; i < MAX_FBS; i++) {
        WideFb* f = &wd.fb[i];
        if (!f->used || !f->active || !overlaps(f->r, r)) {
            continue;
        }
        VRect c = clipTo(r, f->r);
        if (!f->hasDirty) {
            f->dirty = c;
            f->hasDirty = 1;
            if (wd.log) {
                gpuLog("wide %u: frame buffer (%d,%d) drawn outside the wide target at (%d,%d)", wd.vblank, f->r.x0,
                       f->r.y0, c.x0, c.y0);
            }
        } else {
            f->dirty.x0 = c.x0 < f->dirty.x0 ? c.x0 : f->dirty.x0;
            f->dirty.y0 = c.y0 < f->dirty.y0 ? c.y0 : f->dirty.y0;
            f->dirty.x1 = c.x1 > f->dirty.x1 ? c.x1 : f->dirty.x1;
            f->dirty.y1 = c.y1 > f->dirty.y1 ? c.y1 : f->dirty.y1;
        }
    }
}

/* f, ready for drawing in this frame; NULL when the frame is not wide. */
static WideFb* drawing(WideFb* f) {
    if (f == NULL || !wideActive()) {
        return NULL;
    }
    if (f->needBegin || !f->active || f->m != wd.m) {
        begin(f);
    }
    flushDirty(f);
    return f;
}

/* Margins for a picture that has none (VRAM columns x0..x1, rows y0..y1),
 * on target rows ty0.. beside its centre at m..m+w-1: each row in the
 * colour VRAM has all across it (a cleared background goes on to the
 * screen's edges), else black. */
static void plainMargins(int x0, int x1, int y0, int y1, int ty0, int m, int w) {
    if (m <= 0 || y0 > y1) {
        return;
    }
    x1 = x1 < VRAM_W ? x1 : VRAM_W - 1;
    y1 = y1 < VRAM_H ? y1 : VRAM_H - 1;
    /* The CPU's copy of VRAM is current there unless the GPU drew since. */
    int known = x0 <= x1 && !vramIsDrawn((VRect){x0, y0, x1, y1});
    int runStart = y0;
    uint32_t runRgb = 0;
    for (int y = y0; y <= y1 + 1; y++) {
        uint32_t rgb = 0;
        if (y <= y1 && known) {
            const uint16_t* row = &vramShadow[y][0];
            uint16_t c = row[x0];
            int x = x0 + 1;
            while (x <= x1 && row[x] == c) {
                x++;
            }
            if (x > x1) {
                rgb = (uint32_t)(c & 31) << 3 | (uint32_t)((c >> 5) & 31) << 11 | (uint32_t)((c >> 10) & 31) << 19;
            }
        }
        if (y > y1 || (y > runStart && rgb != runRgb)) {
            int t0 = ty0 + runStart - y0, t1 = ty0 + y - 1 - y0;
            gp0WideFill((VRect){0, t0, m - 1, t1}, runRgb);
            gp0WideFill((VRect){m + w, t0, w + 2 * m - 1, t1}, runRgb);
            runStart = y;
        }
        runRgb = rgb;
    }
}

/* Margins on rows y0..y1 of f where a picture without margins replaced
 * those rows (plainMargins), unless the game keeps the last frame's margins. */
static void pictureMargins(WideFb* f, int y0, int y1) {
    if (wd.keepMargins || wd.narrowBackdrop || y0 > y1) {
        return;
    }
    plainMargins(f->r.x0, f->r.x1, y0, y1, y0 + f->dy, f->m, fbWidth(f));
}

/* Whether the PC_WIDE_BELOW rows under f are free: inside VRAM's height, and
 * no other frame buffer there (fields: 224-255 and 480-511; battle's buffers
 * touch, so it has none). */
static int belowFree(const WideFb* f) {
    if (f->top || f->bottom) return 0; /* vertical bands own the spare rows */
    VRect below = {0, f->r.y1 + 1, WIDE_FRAME_W - 1, f->r.y1 + WIDE_BELOW_ROWS};
    if (below.y1 >= VRAM_H) {
        return 0;
    }
    for (int i = 0; i < MAX_FBS; i++) {
        if (&wd.fb[i] != f && wd.fb[i].used && overlaps(wd.fb[i].r, below)) {
            return 0;
        }
    }
    return 1;
}

int wideDrawTarget(int clipX0, int clipY0, int clipX1, int clipY1, WideDraw* out) {
    if (!wideActive()) {
        return 0;
    }
    VRect clip = {clipX0, clipY0, clipX1, clipY1};
    WideFb* f = drawing(findFb(clip, 1));
    if (f == NULL) {
        return 0;
    }
    int shift = wd.m - f->r.x0;
    out->shift = shift;
    out->shiftY = f->dy;
    out->edgeY0 = clipY0 + f->dy;
    out->edgeY1 = clipY1 + f->dy;
    out->clipY0 = clipY0 + f->dy - (clipY0 <= f->r.y0 ? f->top : 0);
    out->clipY1 = clipY1 + f->dy + (clipY1 >= f->r.y1 ? f->bottom : 0);
    out->edgeX0 = clipX0 + shift;
    out->edgeX1 = clipX1 + shift;
    /* Only the sides at the frame buffer's edges widen (a script may narrow
     * the draw area). */
    out->clipX0 = (clipX0 <= f->r.x0 ? f->r.x0 - wd.m : clipX0) + shift;
    out->clipX1 = (clipX1 >= f->r.x1 ? f->r.x1 + wd.m : clipX1) + shift;
    out->fb = (int)(f - wd.fb);
    out->fbY1 = f->r.y1 + f->dy;
    out->belowY1 = out->fbY1 + (belowFree(f) ? WIDE_BELOW_ROWS : 0);
    return 1;
}

/* r (frame buffer pixels) joins f's shifted places: merged into a rectangle
 * it touches, else a new one, else (all used) the one it grows least. */
static void addMask(WideFb* f, VRect r) {
    int w = fbWidth(f), h = f->r.y1 - f->r.y0 + 1;
    r = clipTo(r, (VRect){0, 0, w - 1, h - 1});
    if (r.x0 > r.x1 || r.y0 > r.y1 || f->maskAll) {
        return;
    }
    int best = -1;
    int bestGrowth = 0;
    for (int i = 0; i < f->nMask; i++) {
        VRect m = f->mask[i];
        VRect u = {m.x0 < r.x0 ? m.x0 : r.x0, m.y0 < r.y0 ? m.y0 : r.y0, m.x1 > r.x1 ? m.x1 : r.x1,
                   m.y1 > r.y1 ? m.y1 : r.y1};
        int growth = (u.x1 - u.x0 + 1) * (u.y1 - u.y0 + 1) - (m.x1 - m.x0 + 1) * (m.y1 - m.y0 + 1);
        int touches = r.x0 <= m.x1 + 1 && m.x0 <= r.x1 + 1 && r.y0 <= m.y1 + 1 && m.y0 <= r.y1 + 1;
        if (touches || (f->nMask == WIDE_MASKS && (best < 0 || growth < bestGrowth))) {
            best = i;
            bestGrowth = growth;
            if (touches) {
                break;
            }
        }
    }
    if (best < 0) {
        f->mask[f->nMask++] = r;
        return;
    }
    VRect* m = &f->mask[best];
    *m = (VRect){m->x0 < r.x0 ? m->x0 : r.x0, m->y0 < r.y0 ? m->y0 : r.y0, m->x1 > r.x1 ? m->x1 : r.x1,
                 m->y1 > r.y1 ? m->y1 : r.y1};
}

/* f's centre is not VRAM's in a place not known: all of it. */
static void shiftedAll(WideFb* f) {
    f->shifted = 1;
    f->maskAll = 1;
}

/* Whether f's centre differs from VRAM inside r (VRAM coordinates). */
static int shiftedIn(const WideFb* f, VRect r) {
    if (!f->shifted || f->maskAll) {
        return f->shifted;
    }
    for (int i = 0; i < f->nMask; i++) {
        VRect m = f->mask[i];
        if (overlaps((VRect){m.x0 + f->r.x0, m.y0 + f->r.y0, m.x1 + f->r.x0, m.y1 + f->r.y0}, r)) {
            return 1;
        }
    }
    return 0;
}

void wideNoteShifted(const WideDraw* d, VRect box) {
    WideFb* f = &wd.fb[d->fb];
    f->shifted = 1;
    /* target x = frame buffer x + m, target y = VRAM y + dy */
    int y = f->dy + f->r.y0;
    addMask(f, (VRect){box.x0 - f->m, box.y0 - y, box.x1 - f->m, box.y1 - y});
}

/* ---- captures ---------------------------------------------------------------------- */

static void dropSlots(VRect r) {
    for (int k = 0; k < WIDE_SLOTS; k++) {
        if (wd.slot[k].used && overlaps(wd.slot[k].r, r)) {
            wd.slot[k].used = 0;
            if (wd.log) {
                gpuLog("wide %u: capture (%d,%d) dropped", wd.vblank, wd.slot[k].r.x0, wd.slot[k].r.y0);
            }
        }
    }
    wideSlotsLive = 0;
    for (int k = 0; k < WIDE_SLOTS; k++) {
        wideSlotsLive |= wd.slot[k].used;
    }
}

void wideNoteWrite(VRect r) {
    dropSlots(r);
}

/* A slot for a new capture: a free one, else the oldest part of a screen
 * (effects grab many strips a frame), else the oldest. */
static int slotFor(int whole) {
    int k = -1;
    for (int i = 0; i < WIDE_SLOTS && k < 0; i++) {
        if (!wd.slot[i].used) {
            k = i;
        }
    }
    for (int pass = 0; pass < 2 && k < 0; pass++) {
        for (int i = 0; i < WIDE_SLOTS; i++) {
            WideSlot* s = &wd.slot[i];
            if ((pass == 1 || !s->whole) && (k < 0 || s->age < wd.slot[k].age)) {
                k = i;
            }
        }
    }
    wd.slot[k].whole = whole;
    wd.slot[k].age = ++wd.slotAge;
    return k;
}

/* A row of black for the margins of captures and pictures without any. */
static const uint16_t blackRow[WIDE_W];

/* The capture at dst of src: the wide picture of src when f (the frame
 * buffer it is in) was drawn wide this frame, else the 4:3 picture in VRAM
 * (f NULL: VRAM at src, a whole-screen picture), with black margins when it
 * is the whole screen. Primitives textured from it use it in the wide pass,
 * and make the frame 4:3 unless the game handles them or the margins are
 * the black ones. */
static void capture(WideFb* f, VRect src, VRect dst) {
    int w = src.x1 - src.x0 + 1, h = src.y1 - src.y0 + 1;
    dropSlots(dst);
    if (h > WIDE_SLOT_H) {
        return;
    }
    flushDirty(f);
    /* The wide picture only when it was whole (a frame shown 4:3 may lack
     * what VRAM has). */
    int wideSrc = f != NULL && f->active && f->m == wd.m && f->clean;
    int whole = f == NULL || (src.x0 <= f->r.x0 && src.x1 >= f->r.x1);
    int full = wideSrc && whole;
    int narrow = whole && !full && wd.m > 0;
    int sm = full ? f->m : narrow ? wd.m : 0;
    if (w + 2 * sm > WIDE_W - WIDE_SLOT_X) {
        return;
    }
    int top = 0, bottom = 0;
    if (full && f->vertical && src.y0 == f->r.y0 && h >= 192) {
        top = f->top;
        bottom = f->r.y1 + f->bottom - src.y1;
        if (bottom < 0) bottom = 0;
        if (h + top + bottom > WIDE_SLOT_H) top = bottom = 0;
    }
    int k = slotFor(sm > 0 || top || bottom);
    WideSlot* s = &wd.slot[k];
    s->used = 1;
    s->r = dst;
    s->m = sm;
    s->row = WIDE_SLOT_Y + k * WIDE_SLOT_H + top;
    s->top = top;
    s->bottom = bottom;
    s->shifted = wideSrc && shiftedIn(f, src); /* only where the frame's centre differs */
    s->narrow = narrow;
    if (wideSrc && !narrow) {
        gvkCopy(1, src.x0 - f->r.x0 + f->m - sm, src.y0 + f->dy - top, 1, WIDE_SLOT_X, s->row - top, w + 2 * sm, h + top + bottom);
    } else {
        gvkCopy(0, src.x0, src.y0, 1, WIDE_SLOT_X + sm, s->row, w, h);
    }
    if (narrow) {
        gvkUploadWide((VRect){WIDE_SLOT_X, s->row, WIDE_SLOT_X + sm - 1, s->row + h - 1}, blackRow, 0);
        gvkUploadWide((VRect){WIDE_SLOT_X + sm + w, s->row, WIDE_SLOT_X + 2 * sm + w - 1, s->row + h - 1}, blackRow,
                      0);
    }
    /* Rows of the capture below its frame buffer, and the slot's rows below
     * the capture (texels a stretched triangle's interpolation reaches): what
     * VRAM has there, as the PS1 reads them. */
    int below = wideSrc && f->r.y1 < src.y1 ? f->r.y1 + 1 - src.y0 : h;
    if (below < h) {
        gvkCopy(0, src.x0, src.y0 + below, 1, WIDE_SLOT_X + sm, s->row + below, w, h - below);
    }
    int extra = WIDE_SLOT_H - top - h - bottom;
    if (dst.y0 + h + bottom + extra > VRAM_H) {
        extra = VRAM_H - dst.y0 - h - bottom;
    }
    if (extra > 0 && dst.x0 + w <= VRAM_W) {
        gvkCopy(0, dst.x0, dst.y0 + h + bottom, 1, WIDE_SLOT_X + sm, s->row + h + bottom, w, extra);
    }
    wideSlotsLive = 1;
    if (wd.log) {
        gpuLog("wide %u: capture (%d,%d) %dx%d -> (%d,%d), margins %d, lines %d/%d%s", wd.vblank, src.x0, src.y0, w, h, dst.x0,
               dst.y0, sm, top, bottom, narrow ? " (black)" : "");
    }
}

static WideSlot* slotAt(VRect r) {
    for (int k = 0; k < WIDE_SLOTS; k++) {
        WideSlot* s = &wd.slot[k];
        if (s->used && s->r.x0 == r.x0 && s->r.y0 == r.y0 && s->r.x1 == r.x1 && s->r.y1 == r.y1) {
            return s;
        }
    }
    return NULL;
}

/* A movie's last frame has no scene margins. The presentation pass may
 * already be zooming it to the wide frame; preserve that framing when the
 * field starts animating its capture. Only the private display copy changes. */
void pcWideCaptureZoom(int x, int y) {
    if (!wideActive() || wd.m == 0 || presentFillMode() != 1) return;
    int aw, ah;
    if (presentAspect(&aw, &ah) != PRESENT_ASPECT_WIDE || wd.disp.clocks <= 0 || ah <= 0) return;
    for (int k = 0; wideSlotsLive && k < WIDE_SLOTS; k++) {
        WideSlot* s = &wd.slot[k];
        if (!s->used || !s->narrow || s->r.x0 != x || s->r.y0 != y) continue;
        int w = s->r.x1-s->r.x0+1, h = s->r.y1-s->r.y0+1;
        double z = 1920.0 * aw / ah / (w * wd.disp.clocks);
        if (z <= 1.0) return;
        int vertical = verticalFor(&wd.disp);
        int top = vertical ? wd.disp.tvLine : 0;
        int bottom = vertical ? wd.disp.frameLines-top-h : 0;
        if (bottom < 0) bottom = 0;
        if (h+top+bottom > WIDE_SLOT_H) top = bottom = 0;
        int width = w+2*s->m, height = h+top+bottom;
        uint16_t* pixels = malloc(((size_t)w*h+(size_t)width*height)*sizeof(*pixels));
        if (!pixels) return;
        uint16_t* out = pixels+w*h;
        gp0Flush();
        gvkReadWide(pixels, WIDE_SLOT_X+s->m, s->row, w, h);
        double centreY = wd.disp.frameLines*0.5-wd.disp.tvLine;
        for (int yy = 0; yy < height; yy++) {
            int sy = (int)floor(wd.disp.h*0.5+(yy-top+0.5-centreY)/z);
            for (int xx = 0; xx < width; xx++) {
                int sx = (int)floor(w*0.5+(xx-s->m+0.5-w*0.5)/z);
                out[yy*width+xx] = sx >= 0 && sx < w && sy >= 0 && sy < h ? pixels[sy*w+sx] : 0;
            }
        }
        s->row += top-s->top;
        s->top = top; s->bottom = bottom;
        s->narrow = 0; s->shifted = 1;
        gvkUploadWide((VRect){WIDE_SLOT_X,s->row-top,WIDE_SLOT_X+width-1,s->row-top+height-1},out,width);
        free(pixels);
        return;
    }
}

int pcWideCapture(const void* rect, int x, int y) {
    const int16_t* r = rect; /* RECT: x, y, w, h */
    if (!wideActive() || r[2] <= 0 || r[3] <= 0) {
        return 0;
    }
    VRect src = {r[0], r[1], r[0] + r[2] - 1, r[1] + r[3] - 1};
    VRect dst = {x, y, x + r[2] - 1, y + r[3] - 1};
    gp0Flush();
    WideFb* f = findFb(src, 0);
    if (f == NULL) {
        /* Taller than the frame buffer it starts in (a battle's 224-line
         * capture of the world map's 216-line screen): the rows below come
         * from what lies there, as in VRAM. */
        f = findFb((VRect){src.x0, src.y0, src.x1, src.y0}, 0);
    }
    if (f == NULL) {
        return 0;
    }
    capture(f, src, dst);
    WideSlot* s = slotAt(dst);
    return s != NULL ? s->m : 0;
}

int pcWideCaptureVram(const void* rect) {
    const int16_t* r = rect; /* RECT: x, y, w, h */
    if (!wideActive() || r[2] <= 0 || r[3] <= 0) {
        return 0;
    }
    VRect dst = {r[0], r[1], r[0] + r[2] - 1, r[1] + r[3] - 1};
    gp0Flush();
    capture(NULL, dst, dst);
    WideSlot* s = slotAt(dst);
    return s != NULL ? s->m : 0;
}

int pcWideCaptureMask(const void* rect, int x, int y) {
    const int16_t* r = rect;
    int m = pcWideCapture(rect, x, y);
    WideSlot* s = wideActive() ? slotAt((VRect){x, y, x + r[2] - 1, y + r[3] - 1}) : NULL;
    if (s == NULL) {
        return 0;
    }
    /* The margins as the CPU leaves the 4:3 part: each texel's mask bit set. */
    int w = r[2] + 2 * m, h = r[3] + s->top + s->bottom;
    int row = s->row - s->top;
    uint16_t* buf = malloc((size_t)w * h * sizeof(uint16_t));
    if (buf == NULL) {
        return 0;
    }
    gvkReadWide(buf, WIDE_SLOT_X, row, w, h);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        buf[i] |= 0x8000;
    }
    /* Only the mask bit changed: the hi-res copy keeps its pixels. */
    gvkUploadWideMasked((VRect){WIDE_SLOT_X, row, WIDE_SLOT_X + w - 1, row + h - 1}, buf, w);
    free(buf);
    /* The centre stays the wide picture's (2D moved to the edges shows once);
     * the slot is marked shifted when that differs from VRAM's. */
    return m;
}

void pcWideKeepMargins(int on) {
    wd.keepMargins = on;
}

void pcWideNarrowBackdrop(int on) {
    wd.narrowBackdrop = on;
}

int pcWideCapturedScene(int x, int y, int w, int h) {
    if (!wideActive() || w <= 0 || h <= 0) return 0;
    WideSlot* s = wideSlotsLive ? slotAt((VRect){x,y,x+w-1,y+h-1}) : NULL;
    return s && !s->narrow && (s->m || s->top || s->bottom);
}

int pcWideCaptureMargin(int x, int y) {
    if (!wideActive()) {
        return 0;
    }
    for (int k = 0; wideSlotsLive && k < WIDE_SLOTS; k++) {
        WideSlot* s = &wd.slot[k];
        if (s->used && s->r.x0 <= x && x <= s->r.x1 && s->r.y0 <= y && y <= s->r.y1) {
            return s->m;
        }
    }
    return 0;
}

int pcWideCaptureLines(int x, int y, int* top, int* bottom) {
    for (int k = 0; wideActive() && wideSlotsLive && k < WIDE_SLOTS; k++) {
        WideSlot* s = &wd.slot[k];
        if (s->used && s->r.x0 <= x && x <= s->r.x1 && s->r.y0 <= y && y <= s->r.y1) {
            *top = s->top;
            *bottom = s->bottom;
            return 1;
        }
    }
    *top = *bottom = 0;
    return 0;
}

int pcWideCaptureReadFrame(const void* rect, void* buf, int maxPixels, int* m, int* top, int* bottom) {
    const int16_t* r = rect;
    WideSlot* s = wideActive() && wideSlotsLive ? slotAt((VRect){r[0], r[1], r[0] + r[2] - 1, r[1] + r[3] - 1}) : NULL;
    if (s == NULL || (r[2] + 2*s->m) * (r[3] + s->top + s->bottom) > maxPixels) return 0;
    gp0Flush();
    *m = s->m; *top = s->top; *bottom = s->bottom;
    gvkReadWide(buf, WIDE_SLOT_X, s->row - s->top, r[2] + 2*s->m, r[3] + s->top + s->bottom);
    return 1;
}

/* Resolve the logical capture again after LoadImage: its private slot may
 * have moved. The arena blur covers the authored rows, as its native twin. */
int wideEffectRect(VRect key, VRect* physical, int* shifted) {
    WideSlot* s = wideActive() && wideSlotsLive ? slotAt(key) : NULL;
    if (!s) return 0;
    *physical = (VRect){WIDE_SLOT_X, s->row, WIDE_SLOT_X + key.x1 - key.x0 + 2*s->m,
                       s->row + key.y1 - key.y0};
    *shifted = s->shifted;
    return 1;
}

int pcWideCaptureRead(const void* rect, void* buf, int maxPixels) {
    const int16_t* r = rect;
    WideSlot* s = wd.m != 0 && wideSlotsLive ? slotAt((VRect){r[0], r[1], r[0] + r[2] - 1, r[1] + r[3] - 1}) : NULL;
    if (s == NULL || s->m == 0 || (r[2] + 2 * s->m) * r[3] > maxPixels) {
        return 0;
    }
    gp0Flush();
    gvkReadWide(buf, WIDE_SLOT_X, s->row, r[2] + 2 * s->m, r[3]);
    return s->m;
}

void pcWideCaptureLoadFrame(const void* rect, const void* buf, int m, int top, int bottom, int shifted) {
    const int16_t* r = rect;
    int w = r[2] + 2 * m, h = r[3] + top + bottom;
    if (!wideActive() || m < 0 || top < 0 || bottom < 0 || r[2] <= 0 || h <= 0 || h > WIDE_SLOT_H || w > WIDE_W - WIDE_SLOT_X) {
        return;
    }
    gp0Flush();
    VRect dst = {r[0], r[1], r[0] + r[2] - 1, r[1] + r[3] - 1};
    dropSlots(dst);
    int k = slotFor(1);
    WideSlot* s = &wd.slot[k];
    s->used = 1;
    s->r = dst;
    s->m = m;
    s->row = WIDE_SLOT_Y + k * WIDE_SLOT_H + top;
    s->top = top; s->bottom = bottom;
    s->shifted = shifted;
    s->narrow = 0;
    gvkUploadWide((VRect){WIDE_SLOT_X, s->row - top, WIDE_SLOT_X + w - 1, s->row - top + h - 1}, buf, w);
    wideSlotsLive = 1;
    if (wd.log) {
        gpuLog("wide %u: capture (%d,%d) %dx%d loaded, margins %d", wd.vblank, dst.x0, dst.y0, r[2], h, m);
    }
}

void pcWideCaptureLoad(const void* rect, const void* buf, int m, int shifted) {
    if (m > 0) pcWideCaptureLoadFrame(rect, buf, m, 0, 0, shifted);
}

/* A frame-sized picture in texture space: a screen capture (one made with
 * StoreImage and LoadImage, or a frame buffer that was not drawn wide). */
static int captureSized(VRect r) {
    int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1;
    int dw = wd.disp.w > 0 ? wd.disp.w : 320;
    /* No taller than the display: a capture copies a frame buffer (battle
     * stages load 320x256 backdrop textures). */
    int dh = wd.disp.h > 0 ? wd.disp.h : 240;
    /* Not where frame buffers go (a mode's first picture before it draws). */
    return (w == dw || w == 320) && h >= 192 && h <= dh && h <= WIDE_SLOT_H && !candidate(r);
}

/* ---- textures ----------------------------------------------------------------------- */

uint32_t wideTexture(const WideDraw* d, uint32_t tpage, int u0, int u1, int v0, int v1, const WidePacket* pkt) {
    WideFb* f = &wd.fb[d->fb];
    int depth = (int)((tpage >> 7) & 3);
    int tx = (int)(tpage & 0xF) * 64;
    int ty = (int)((tpage >> 4) & 1) * 256;
    int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    VRect t = {tx + (u0 >> shift), ty + v0, tx + (u1 >> shift), ty + v1};
    /* The largest u and v are not sampled (right and bottom edges are not
     * drawn): a 16-line tile reading one 16-line capture must not count as
     * reading the next one too (the field distortion's strips). */
    if (t.x1 > t.x0) {
        t.x1--;
    }
    if (t.y1 > t.y0) {
        t.y1--;
    }
    int handled = pkt != NULL && (pkt->flags & (PC_WIDE_HANDLED | PC_WIDE_ONLY));
    int texDx = pkt != NULL ? pkt->texDx : 0;
    int texDy = pkt != NULL ? pkt->texDy : 0;
    if (depth != 2) {
        /* 4- and 8-bit texels are palette indices, not a picture of the
         * screen: whatever VRAM holds there (the field's opening credits draw
         * sprites whose texture page a screen capture overwrote), the wide
         * pass reads the same and draws the same where the 4:3 picture has
         * it, so nothing about the frame changes. */
        return 0;
    }
    for (int k = 0; wideSlotsLive && k < WIDE_SLOTS; k++) {
        WideSlot* s = &wd.slot[k];
        if (s->used && overlaps(s->r, t)) {
            if (!s->top && !s->bottom && !handled) f->vertical = 0;
            /* A capture with black margins: the wide picture draws what VRAM
             * does, and the margins stay what the frame has there. */
            if (!handled && !s->narrow) {
                unclean(f, "textured from a screen capture");
            }
            if (s->shifted) {
                shiftedAll(f);
            }
            return WIDE_TEX(WIDE_SLOT_X + s->m - s->r.x0 + texDx, s->row - s->r.y0 + texDy);
        }
    }
    for (int i = 0; i < MAX_FBS; i++) {
        WideFb* g = &wd.fb[i];
        VRect gr = g->r;
        if (g->used && pkt != NULL && (pkt->flags & PC_WIDE_BELOW) && belowFree(g)) {
            gr.y1 += WIDE_BELOW_ROWS; /* its scratch rows too */
        }
        if (g->used && g->active && overlaps(gr, t)) {
            if (!g->vertical && !handled) f->vertical = 0;
            if (!handled) {
                unclean(f, "textured from a frame buffer");
            }
            return WIDE_TEX(g->m - g->r.x0 + texDx, g->dy + texDy);
        }
    }
    return 0;
}

int wideTransitionCaptureRect(VRect* rect) {
    for (int k = 0; wideSlotsLive && k < WIDE_SLOTS; k++) {
        const WideSlot* s = &wd.slot[k];
        if (s->used && s->r.x0 == 704 && s->r.y0 == 256 && s->r.x1 == 1023 && s->r.y1 == 479) {
            *rect = (VRect){WIDE_SLOT_X, s->row-s->top, WIDE_SLOT_X+320+2*s->m-1,
                           s->row-s->top+WIDE_SLOT_H-1};
            return 1;
        }
    }
    return 0;
}

/* ---- transfers ------------------------------------------------------------------------ */

void wideFill(VRect r, uint32_t rgb) {
    if (wideSlotsLive) {
        dropSlots(r);
    }
    if (!wideActive()) {
        return;
    }
    findFb(r, 1); /* a full-screen clear names its frame buffer */
    for (int i = 0; i < MAX_FBS; i++) {
        WideFb* f = &wd.fb[i];
        if (!f->used || !overlaps(f->r, r) || drawing(f) == NULL) {
            continue;
        }
        VRect c = clipTo(r, f->r);
        VRect t = {c.x0 - f->r.x0 + f->m, c.y0 + f->dy, c.x1 - f->r.x0 + f->m, c.y1 + f->dy};
        if (r.y0 <= f->r.y0) t.y0 -= f->top;
        if (r.y1 >= f->r.y1) t.y1 += f->bottom;
        if (r.x0 <= f->r.x0) {
            t.x0 = 0;
        }
        if (r.x1 >= f->r.x1) {
            t.x1 = fbWidth(f) + 2 * f->m - 1;
        }
        gp0WideFill(t, rgb);
    }
}

/* A picture that comes from VRAM (a LoadImage, a MoveImage the wide
 * picture cannot copy) at r: into the centres of the frame buffers it
 * covers, with black margins beside whole rows of it. */
static void pictureInto(VRect r) {
    for (int i = 0; i < MAX_FBS; i++) {
        WideFb* f = &wd.fb[i];
        if (!f->used || !overlaps(f->r, r)) {
            continue;
        }
        VRect c = clipTo(r, f->r);
        /* A small CPU overlay (the game's PAUSE label) patches the frozen
         * frame. It neither starts a new draw nor replaces the extra rows.
         * Whole pictures still have only their authored native framing. */
        int picture = c.x0 == f->r.x0 && c.x1 == f->r.x1 &&
                      c.y0 == f->r.y0 && c.y1 - c.y0 + 1 >= 192;
        if (picture || !f->active || f->m != wd.m) {
            if (drawing(f) == NULL) continue;
        } else {
            flushDirty(f);
        }
        mirror(f, c);
        if (picture) f->vertical = 0; /* CPU artwork has no extra scene rows */
        if (c.x0 <= f->r.x0 && c.x1 >= f->r.x1) {
            pictureMargins(f, c.y0, c.y1);
        }
    }
}

void wideLoad(VRect r) {
    if (wideSlotsLive) {
        dropSlots(r);
    }
    if (!wideActive()) {
        return;
    }
    if (findFb(r, 0) == NULL && captureSized(r)) {
        capture(NULL, r, r);
        return;
    }
    pictureInto(r);
}

void wideMove(VRect src, VRect dst) {
    WideSlot* s = wideSlotsLive ? slotAt(src) : NULL;
    if (s == NULL && wideSlotsLive && src.y1 - src.y0 + 1 >= 192) {
        /* The world map captures 216 rows; menus restore 224. The extra
         * display rows supply that taller copy without reading unrelated
         * native VRAM below the capture. Keep the capture's original size. */
        for (int k = 0; k < WIDE_SLOTS; k++) {
            WideSlot* candidate = &wd.slot[k];
            if (candidate->used && candidate->r.x0 == src.x0 && candidate->r.x1 == src.x1 &&
                candidate->r.y0 == src.y0 && candidate->r.y1 < src.y1 &&
                src.y1 <= candidate->r.y1 + candidate->bottom) {
                s = candidate;
                break;
            }
        }
    }
    WideSlot keep = s != NULL ? *s : (WideSlot){0};
    if (wideSlotsLive) {
        dropSlots(dst);
    }
    if (!wideActive()) {
        return;
    }
    WideFb* from = findFb(src, 0);
    WideFb* to = findFb(dst, 0);
    if (to == NULL && from != NULL && candidate(dst)) {
        /* A picture copied to where the next screen's buffers go. */
        to = findFb(dst, 1);
        from = findFb(src, 0);
    }
    if (to == NULL) {
        if (from == NULL && !overFb(dst)) {
            /* Taller than the frame buffer it starts in (a field's 224-line
             * grab of the world map's 216-line screen), to texture space: as
             * pcWideCapture. */
            from = findFb((VRect){src.x0, src.y0, src.x1, src.y0}, 0);
            if (from != NULL && !(src.x0 <= from->r.x0 && src.x1 >= from->r.x1)) {
                from = NULL;
            }
        }
        if (from != NULL || captureSized(dst)) {
            capture(from, from != NULL ? src : dst, dst);
        }
        /* Over frame buffers without being inside one. */
        pictureInto(dst);
        return;
    }
    if (from != NULL && (!from->active || from->m != wd.m)) {
        from = NULL;
    }
    flushDirty(from);
    to = drawing(to);
    if (to == NULL) {
        return;
    }
    int w = dst.x1 - dst.x0 + 1, h = dst.y1 - dst.y0 + 1;
    int full = dst.x0 <= to->r.x0 && dst.x1 >= to->r.x1;
    int tw = fbWidth(to) + 2 * to->m;
    if (wd.log && (keep.used || wd.logMoves++ < 1000000)) {
        gpuLog("wide %u: move (%d,%d) %dx%d -> (%d,%d)%s", wd.vblank, src.x0, src.y0, w, h, dst.x0, dst.y0,
               keep.used ? " from a capture" : "");
    }
    if (keep.used) {
        to->vertical = keep.top || keep.bottom;
        /* A capture back into a frame: with its margins. */
        int x = dst.x0 - to->r.x0 + to->m - keep.m;
        int sx = WIDE_SLOT_X, cw = w + 2 * keep.m;
        if (x < 0) {
            sx -= x;
            cw += x;
            x = 0;
        }
        if (x + cw > tw) {
            cw = tw - x;
        }
        if (cw > 0) {
            int top = dst.y0 == to->r.y0 ? (keep.top < to->top ? keep.top : to->top) : 0;
            int available = to->r.y1 + to->bottom - dst.y1;
            int sourceBottom = keep.r.y1 + keep.bottom - src.y1;
            int bottom = dst.y0 == to->r.y0 && h >= wd.disp.h ? (sourceBottom < available ? sourceBottom : available) : 0;
            if (bottom < 0) bottom = 0;
            gvkCopy(1, sx, keep.row - top, 1, x, dst.y0 + to->dy - top, cw, h + top + bottom);
        }
        if (keep.shifted) {
            shiftedAll(to);
        }
        if (full && keep.m < to->m && !wd.keepMargins && !wd.narrowBackdrop) {
            /* Beyond the capture's margins (made with a smaller margin): black. */
            if (x > 0) {
                gp0WideFill((VRect){0, dst.y0 + to->dy, x - 1, dst.y0 + to->dy + h - 1}, 0);
            }
            if (x + cw < tw) {
                gp0WideFill((VRect){x + cw, dst.y0 + to->dy, tw - 1, dst.y0 + to->dy + h - 1}, 0);
            }
        }
        if (s != NULL) {
            *s = keep; /* a move from the capture does not change it */
            wideSlotsLive = 1;
        }
        return;
    }
    if (from != NULL) {
        to->vertical = from->vertical;
        int sfull = src.x0 <= from->r.x0 && src.x1 >= from->r.x1;
        if (sfull && full) {
            int top = src.y0 == from->r.y0 && dst.y0 == to->r.y0 ? (from->top < to->top ? from->top : to->top) : 0;
            int sa = from->r.y1 + from->bottom - src.y1, da = to->r.y1 + to->bottom - dst.y1;
            int bottom = src.y0 == from->r.y0 && dst.y0 == to->r.y0 && h >= wd.disp.h ? (sa < da ? sa : da) : 0;
            if (bottom < 0) bottom = 0;
            gvkCopy(1, 0, src.y0 + from->dy - top, 1, 0, dst.y0 + to->dy - top, tw, h + top + bottom);
        } else {
            gvkCopy(1, src.x0 - from->r.x0 + from->m, src.y0 + from->dy, 1, dst.x0 - to->r.x0 + to->m, dst.y0 + to->dy, w, h);
        }
        /* What the copied picture had beyond the 4:3 one comes with it. */
        if (from->shifted) {
            to->shifted = 1;
            to->maskAll |= from->maskAll;
            for (int i = 0; i < from->nMask && !to->maskAll; i++) {
                VRect m = clipTo(from->mask[i], (VRect){src.x0 - from->r.x0, src.y0 - from->r.y0,
                                                        src.x1 - from->r.x0, src.y1 - from->r.y0});
                int ox = (dst.x0 - to->r.x0) - (src.x0 - from->r.x0), oy = (dst.y0 - to->r.y0) - (src.y0 - from->r.y0);
                if (m.x0 <= m.x1 && m.y0 <= m.y1) {
                    addMask(to, (VRect){m.x0 + ox, m.y0 + oy, m.x1 + ox, m.y1 + oy});
                }
            }
        }
        return;
    }
    /* A picture from outside the frame buffers drawn wide (one the game
     * kept, a screen of the mode before): as a LoadImage. */
    pictureInto(dst);
}

/* ---- vertical blank ------------------------------------------------------------------------ */

/* Why display d shows no wide picture (f: the frame buffer it shows). */
static const char* noneWhy(const GpuDisplay* d, const WideFb* f) {
    static char kindWhy[PC_WIDE_KINDS][48];
    if (d->rgb24) {
        return "24-bit display (a movie)";
    }
    if (!(wd.ready & (1u << wd.kind))) {
        snprintf(kindWhy[wd.kind], sizeof(kindWhy[0]), "kind %s not ready", kindNames[wd.kind]);
        return kindWhy[wd.kind];
    }
    if (wd.block & PC_WIDE_BLOCK_MOVIE) {
        return "blocked: movie";
    }
    if (wd.block & PC_WIDE_BLOCK_EFFECT) {
        return "blocked: effect";
    }
    if (wd.block & PC_WIDE_BLOCK_OPTOUT) {
        return "blocked: opt-out";
    }
    if (d->w < 256 || d->w > 640) {
        return "display width out of range";
    }
    if (f == NULL) {
        return "no frame buffer drawn at the display";
    }
    return "frame buffer drawn without this margin";
}

void wideVBlank(const GpuDisplay* d) {
    wd.vblank++;
    wd.disp = *d;
    WideView* v = &wd.view;
    memset(v, 0, sizeof(*v));
    for (int i = 0; i < MAX_FBS; i++) {
        flushDirty(&wd.fb[i]);
    }
    WideFb* shown = NULL;
    if (d->enabled && d->w > 0 && d->h > 0 && !d->rgb24) {
        WideFb* f = NULL;
        for (int i = 0; i < MAX_FBS; i++) {
            if (wd.fb[i].used && wd.fb[i].r.x0 <= d->x && d->x <= wd.fb[i].r.x1 && wd.fb[i].r.y0 <= d->y &&
                d->y <= wd.fb[i].r.y1) {
                f = &wd.fb[i];
            }
        }
        int next = marginFor(d);
        if (f != NULL) {
            /* Shown now: its next drawing starts a new frame. Shown wide only
             * when drawn with the margin of the frames being drawn now or
             * from now on (not by an earlier screen, or before the aspect
             * changed), and as wide as the display. */
            f->needBegin = 1;
            v->has = f->active && (f->m > 0 || f->top > 0 || f->bottom > 0) && (f->m == wd.m || f->m == next) && fbWidth(f) >= d->w;
        }
        shown = f;
        if (!v->has && next > 0 && d->w + 2 * next <= WIDE_W && d->h <= WIDE_H - WIDE_VIEW_Y) {
            /* Nothing drawn wide is shown (a screen's first frame before it
             * is drawn, a picture loaded straight into the display): VRAM's
             * picture with black margins, made in the rows below the
             * captures. */
            int w = d->x + d->w <= VRAM_W ? d->w : VRAM_W - d->x;
            int h = d->y + d->h <= VRAM_H ? d->h : VRAM_H - d->y;
            gvkUploadWide((VRect){0, WIDE_VIEW_Y, d->w + 2 * next - 1, WIDE_VIEW_Y + d->h - 1}, blackRow, 0);
            gvkCopy(0, d->x, d->y, 1, next, WIDE_VIEW_Y, w, h);
            plainMargins(d->x, d->x + w - 1, d->y, d->y + h - 1, WIDE_VIEW_Y, next, d->w);
            v->has = 1;
            v->m = next;
            v->x = 0;
            v->y = WIDE_VIEW_Y;
            v->w = d->w + 2 * next;
            v->h = d->h;
            v->clean = 1;
            /* Until a scene is drawn, keep Zoom applied to this narrow
             * VRAM picture just as it was before the mode handover. */
            v->wide = presentAspect(NULL, NULL) == PRESENT_ASPECT_WIDE && presentFillMode() != 1;
            if (wd.log) {
                gpuLog("wide %u: display (%d,%d) shown from VRAM", wd.vblank, d->x, d->y);
            }
            f = NULL;
        }
        if (v->has && f != NULL && d->y + d->h - 1 > f->r.y1) {
            /* Display lines below the frame buffer (a 240-line display of a
             * 224-line buffer): VRAM's, with plain margins. */
            int y0 = f->r.y1 + 1, n = d->y + d->h - y0;
            if (y0 + n > VRAM_H) {
                n = VRAM_H - y0;
            }
            if (n > 0) {
                gvkCopy(0, f->r.x0, y0, 1, f->m, y0 + f->dy, fbWidth(f), n);
                plainMargins(f->r.x0, f->r.x1, y0, y0 + n - 1, y0 + f->dy, f->m, fbWidth(f));
            }
        }
        if (v->has && f != NULL) {
            v->m = f->m;
            v->x = d->x - f->r.x0;
            v->y = d->y + f->dy;
            v->w = d->w + 2 * f->m;
            v->h = d->h;
            v->clean = f->clean;
            v->shifted = f->shifted;
            /* The shifted places, in the 4:3 frame's pixels (the display). */
            v->nMask = f->maskAll ? -1 : 0;
            for (int i = 0; !f->maskAll && i < f->nMask; i++) {
                VRect m = f->mask[i];
                int ox = f->r.x0 - d->x, oy = f->r.y0 - d->y;
                m = clipTo((VRect){m.x0 + ox, m.y0 + oy, m.x1 + ox, m.y1 + oy}, (VRect){0, 0, d->w - 1, d->h - 1});
                if (m.x0 <= m.x1 && m.y0 <= m.y1) {
                    v->mask[v->nMask++] = m;
                }
            }
            v->why = f->why;
            if (f->vertical && f->clean && verticalFor(d) && d->y == f->r.y0) {
                v->top = d->tvLine;
                v->bottom = 240 - d->tvLine - d->h;
                v->y -= v->top;
                v->h += v->top + v->bottom;
            }
            int aspectWide = presentAspect(NULL, NULL) == PRESENT_ASPECT_WIDE;
            if (!aspectWide && (v->top || v->bottom)) {
                /* An aspect change can show a frame drawn with the previous
                 * horizontal margins until the next draw. Keep its centre. */
                v->x += v->m;
                v->w -= 2 * v->m;
                v->m = 0;
            }
            v->wide = f->clean && (aspectWide || v->top || v->bottom);
        }
    }
    if (!v->has && d->enabled && d->w > 0 && d->h > 0 && presentAspect(NULL, NULL) == PRESENT_ASPECT_WIDE) {
        v->none = noneWhy(d, shown);
    }
    if (wd.log) {
        /* The display and what it shows, when that changes. */
        static int last[10];
        int now[10] = {d->x, d->y, d->w, d->h, shown != NULL ? shown->r.x0 : -1, shown != NULL ? shown->r.y0 : -1,
                      shown != NULL ? shown->r.y1 : -1, v->has ? v->m : -1, v->top, v->bottom};
        if (memcmp(now, last, sizeof(now)) != 0) {
            memcpy(last, now, sizeof(now));
            gpuLog("wide %u: display (%d,%d) %dx%d, frame buffer (%d,%d)-%d, wide margin %d, lines %d/%d (vertical %d, clean %d, kind %d)", wd.vblank, d->x, d->y,
                   d->w, d->h, now[4], now[5], now[6], now[7], v->top, v->bottom,
                   shown ? shown->vertical : 0, shown ? shown->clean : 0, wd.kind);
        }
    }
    setMargin(marginFor(d));
    if (!wideActive()) {
        for (int i = 0; i < MAX_FBS; i++) {
            wd.fb[i].m = 0;
            wd.fb[i].active = 0;
        }
    }
}

const WideView* wideCurrentView(void) {
    return &wd.view;
}

int wideFrameView(VRect native, WideView* view) {
    memset(view, 0, sizeof(*view));
    if (!wideActive()) return 0;
    const WideFb* f = NULL;
    for (int i = 0; i < MAX_FBS; i++)
        if (wd.fb[i].used && !memcmp(&wd.fb[i].r, &native, sizeof(native))) { f = &wd.fb[i]; break; }
    if (!f || !f->active) return 0;
    if (!f->clean || f->hasDirty) return -1;
    view->has = view->wide = view->clean = 1;
    view->m = f->m; view->top = f->top; view->bottom = f->bottom;
    view->x = 0; view->y = f->r.y0 + f->dy - f->top;
    view->w = fbWidth(f) + 2 * f->m;
    view->h = f->r.y1 - f->r.y0 + 1 + f->top + f->bottom;
    return 1;
}

/* ---- pcWidePrim's table ------------------------------------------------------------------- */

static unsigned hashPtr(const void* p) {
    uint64_t v = (uint64_t)(uintptr_t)p;
    v ^= v >> 29;
    v *= 0x9E3779B97F4A7C15ull;
    return (unsigned)(v >> 40) & (PACKETS - 1);
}

static WidePacket* packetSlot(void* prim) {
    if (prim == NULL) return NULL;
    unsigned h = hashPtr(prim);
    while (wd.key[h] != NULL && wd.key[h] != prim) h = (h + 1) & (PACKETS - 1);
    if (wd.key[h] == NULL) {
        if (wd.count >= PACKETS / 2) {
            gpuWarnOnce("widepackets", "GPU: more than %d tagged packets before a DrawOTag", PACKETS / 2);
            return NULL;
        }
        wd.key[h] = prim;
        wd.used[wd.count++] = (int)h;
        memset(&wd.val[h], 0, sizeof(wd.val[h]));
    }
    return &wd.val[h];
}

void pcWidePrimTex2D(void* prim, int dx, unsigned int flags, int texDx, int texDy) {
    if (!wideActive()) return;
    WidePacket* p = packetSlot(prim);
    if (!p) return;
    /* XG_WIDE_NOSHIFT keeps 2D where the 4:3 picture has it; scratch pieces
     * (PC_WIDE_BELOW) are placed with dx, not laid out. */
    p->dx = wd.noShift && !(flags & PC_WIDE_BELOW) ? 0 : dx;
    p->flags = flags;
    p->texDx = texDx;
    p->texDy = texDy;
}

void pcWidePrimTex(void* prim, int dx, unsigned int flags, int texDx) {
    pcWidePrimTex2D(prim, dx, flags, texDx, 0);
}

void pcTextureClass(void* prim, int kind) {
    if (kind < 0 || kind >= PC_TEX_CLASSES) return;
    if (wd.kind == PC_WIDE_BATTLE) {
        if (kind == PC_TEX_HUD) kind = PC_TEX_BATTLE_HUD;
        if (kind == PC_TEX_TEXT) kind = PC_TEX_BATTLE_TEXT;
    }
    WidePacket* p = packetSlot(prim);
    if (p) { p->texClass = kind; p->classified = 1; }
}

void pcFadePrim(void* prim) {
    WidePacket* p = packetSlot(prim);
    if (p) p->fade = 1;
}

void pcGeometryPrim(void* prim, int treatment) {
    WidePacket* p = packetSlot(prim);
    if (p) p->geometry = treatment;
}

void pcTextureClassDefault(void* prim, int kind) {
    const WidePacket* p = widePacket(prim);
    if (!p || !p->classified) pcTextureClass(prim, kind);
}

int pcWideShifts(void) {
    return !wd.noShift;
}

void pcWidePrim(void* prim, int dx, unsigned int flags) {
    pcWidePrimTex(prim, dx, flags, 0);
}

const WidePacket* widePacket(const void* packet) {
    if (wd.count == 0) {
        return NULL;
    }
    unsigned h = hashPtr(packet);
    while (wd.key[h] != NULL) {
        if (wd.key[h] == packet) {
            return &wd.val[h];
        }
        h = (h + 1) & (PACKETS - 1);
    }
    return NULL;
}

#define POSE_GROUPS 256
static PoseGroup poseGroups[POSE_GROUPS];
static int poseGroupCount;

void pcSpritePosePart(const void* actor, void* prim, const void* v0, const void* v1, const void* v2,
                      unsigned tpage, unsigned clut, int u, int v, int w, int h) {
    if (!actor || !prim) return;
    /* An actor's parts are submitted together; a new run starts a group. */
    if (!poseGroupCount || poseGroups[poseGroupCount - 1].actor != actor) {
        if (poseGroupCount == POSE_GROUPS) return;
        PoseGroup* g = &poseGroups[poseGroupCount++];
        memset(g, 0, sizeof(*g));
        g->actor = actor;
    }
    PoseGroup* g = &poseGroups[poseGroupCount - 1];
    WidePacket* p = packetSlot(prim);
    if (!p) return;
    if (g->n == POSE_PARTS) { g->overflow = 1; return; }
    g->part[g->n++] = (PosePart){.xy = {v0, v1, v2}, .cell = {(uint16_t)tpage, (uint16_t)clut, 0, u, v, w, h, 0, 0}}; /* sprite space: pcSpritePoseSpace */
    p->poseGroup = poseGroupCount;
}

void pcSpritePoseSpace(void* prim, int x0, int y0, int x1, int y1, int x2, int y2) {
    if (!prim || !poseGroupCount) return;
    WidePacket* p = packetSlot(prim);
    if (!p || p->poseGroup != poseGroupCount) return;
    PoseGroup* g = &poseGroups[poseGroupCount - 1];
    if (!g->n) return;
    PosePart* part = &g->part[g->n - 1];   /* the part pcSpritePosePart registered for this packet */
    part->sx[0] = (int16_t)x0; part->sy[0] = (int16_t)y0;
    part->sx[1] = (int16_t)x1; part->sy[1] = (int16_t)y1;
    part->sx[2] = (int16_t)x2; part->sy[2] = (int16_t)y2;
    part->space = 1;
}

static unsigned poseSerial;
unsigned widePoseSerial(void) { return poseSerial; }

PoseGroup* widePoseGroup(int group) {
    return group >= 1 && group <= poseGroupCount ? &poseGroups[group - 1] : NULL;
}

void widePacketsDone(void) {
    if (poseGroupCount) ++poseSerial;
    poseGroupCount = 0;
    for (int i = 0; i < wd.count; i++) {
        wd.key[wd.used[i]] = NULL;
    }
    wd.count = 0;
    /* pcWideAlloc's arenas turn over with each DrawOTag. */
    wd.arena = (wd.arena + 1) % ARENAS;
    wd.arenaUsed[wd.arena] = 0;
}

/* A picture drawn while the game's next DrawOTag is pending (the Enhanced
 * engine's pictures between two vblanks, libetc.c pcVSyncBetween): the game's
 * registrations for its DrawOTag are put aside (pcWideStash(1)) while the
 * picture registers and draws its own, then put back (pcWideStash(0)). */
static struct {
    const void* key;
    WidePacket val;
}* wideStashed;
static int wideStashedCount, wideStashedCap, wideStashedPoses;
static PoseGroup wideStashedGroups[POSE_GROUPS];

void pcWideStash(int on) {
    if (on) {
        if (wd.count > wideStashedCap) {
            void* grown = realloc(wideStashed, (size_t)wd.count * sizeof(*wideStashed));
            if (!grown) {
                wideStashedCount = -1; /* nothing put aside: the picture shares the game's table */
                return;
            }
            wideStashed = grown;
            wideStashedCap = wd.count;
        }
        wideStashedCount = wd.count;
        for (int i = 0; i < wd.count; i++) {
            int h = wd.used[i];
            wideStashed[i].key = wd.key[h];
            wideStashed[i].val = wd.val[h];
            wd.key[h] = NULL;
        }
        wd.count = 0;
        wideStashedPoses = poseGroupCount;
        memcpy(wideStashedGroups, poseGroups, (size_t)poseGroupCount * sizeof(PoseGroup));
        poseGroupCount = 0;
        return;
    }
    if (wideStashedCount < 0) {
        wideStashedCount = 0;
        return;
    }
    for (int i = 0; i < wd.count; i++) {
        wd.key[wd.used[i]] = NULL; /* the picture's own, if it did not draw them */
    }
    wd.count = 0;
    for (int i = 0; i < wideStashedCount; i++) {
        WidePacket* p = packetSlot((void*)wideStashed[i].key);
        if (p) *p = wideStashed[i].val;
    }
    wideStashedCount = 0;
    memcpy(poseGroups, wideStashedGroups, (size_t)wideStashedPoses * sizeof(PoseGroup));
    poseGroupCount = wideStashedPoses;
}

/* A frame buffer's wide picture kept aside and put back (the Enhanced
 * engine's screen ripple, fx_801FC000.c ripple60Between/Restore): a picture
 * between drawn into the buffer on display reaches its wide rows too (the
 * margins, the shifted 2D), which the 4:3 StoreImage/LoadImage pair does not
 * cover; the rows with their margins and extra lines are read before the
 * picture and written back after it, with the frame's flags (shifted places,
 * masks, clean). One kept picture at a time. The rows stay on the device
 * when it has room for them (vk.c gvkKeepWide: copies both ways, no
 * readback, and the hi-res copy's rows come back as they were, not
 * replicated from 1x); else they are read back and uploaded. */
static struct {
    int on;
    int device; /* kept by gvkKeepWide */
    int idx;
    WideFb fb;
    uint16_t* pixels;
    size_t cap;
} wideKept;

int pcWideFrameKeep(const void* rect) {
    const int16_t* r = rect;
    VRect native = {r[0], r[1], r[0] + r[2] - 1, r[1] + r[3] - 1};
    WideFb* f = NULL;
    wideKept.on = 0;
    if (!wideActive()) return 0;
    for (int i = 0; i < MAX_FBS; i++)
        if (wd.fb[i].used && !memcmp(&wd.fb[i].r, &native, sizeof(native))) { f = &wd.fb[i]; break; }
    if (!f || !f->active) return 0;
    int w = fbWidth(f) + 2 * f->m, h = f->r.y1 - f->r.y0 + 1 + f->top + f->bottom;
    gp0Flush();
    wideKept.device = gvkKeepWide(0, f->r.y0 + f->dy - f->top, w, h);
    if (!wideKept.device) {
        size_t need = (size_t)w * h;
        if (need > wideKept.cap) {
            uint16_t* grown = realloc(wideKept.pixels, need * sizeof(uint16_t));
            if (!grown) return 0;
            wideKept.pixels = grown;
            wideKept.cap = need;
        }
        gvkReadWide(wideKept.pixels, 0, f->r.y0 + f->dy - f->top, w, h);
    }
    wideKept.fb = *f;
    wideKept.idx = (int)(f - wd.fb);
    wideKept.on = 1;
    return 1;
}

void pcWideFramePutBack(void) {
    if (!wideKept.on) return;
    wideKept.on = 0;
    WideFb* f = &wd.fb[wideKept.idx];
    const WideFb* k = &wideKept.fb;
    /* the same frame buffer, laid out as when it was kept */
    if (!wideActive() || !f->used || !f->active || memcmp(&f->r, &k->r, sizeof(k->r)) || f->m != k->m ||
        f->dy != k->dy || f->top != k->top || f->bottom != k->bottom) {
        return;
    }
    int w = fbWidth(f) + 2 * f->m, h = f->r.y1 - f->r.y0 + 1 + f->top + f->bottom, y = f->r.y0 + f->dy - f->top;
    gp0Flush();
    if (wideKept.device) {
        gvkKeepWidePutBack();
    } else {
        gvkUploadWide((VRect){0, y, w - 1, y + h - 1}, wideKept.pixels, w);
    }
    *f = *k;
}

/* ---- pcWideAlloc ------------------------------------------------------------------------ */

static void* packetAlloc(int bytes) {
    if (bytes <= 0) {
        return NULL;
    }
    size_t n = ((size_t)bytes + 15) & ~(size_t)15;
    if (wd.arenaUsed[wd.arena] + n > ARENA_BYTES) {
        gpuWarnOnce("widealloc", "wide: pcWideAlloc arena full (%d bytes)", ARENA_BYTES);
        return NULL;
    }
    void* p = wd.arenaMem[wd.arena] + wd.arenaUsed[wd.arena];
    wd.arenaUsed[wd.arena] += n;
    /* Ignored UV halfwords and host alignment bytes are saved too. Do not
     * retain an older packet in those bytes after arena reuse or a load. */
    memset(p, 0, n);
    return p;
}

void* pcWideAlloc(int bytes) {
    return wideActive() ? packetAlloc(bytes) : NULL;
}

void* pcGeometryAlloc(const void* packet, int bytes) {
    if (!pcGeometryActive() || !hiresSlivers() || !packet) return NULL;
    void* p = packetAlloc(bytes);
    if (!p) return NULL;
    WidePacket* tag = packetSlot(p);
    if (!tag) return NULL;
    memcpy(p, packet, (size_t)bytes);
    const WidePacket* source = widePacket(packet);
    if (source) *tag = *source;
    tag->geometry = PC_GEOMETRY_ONLY;
    return p;
}

void pcGeometryBlend(void* packet, int abr) {
    WidePacket* tag = packetSlot(packet);
    if (tag && tag->geometry == PC_GEOMETRY_ONLY) tag->geometryAbr = (abr & 3) + 1;
}

/* ---- save states (system/savestate.h) ------------------------------------------------- */

/* The game's choices (kind, block, margins kept, backdrop), the frame
 * buffers' and capture slots' bookkeeping, pcWidePrim's table as the game
 * filled it for its next DrawOTag (live at a vblank: PC_GEOMETRY_ONLY tags
 * matter at 4:3 too), the pcWideAlloc packets linked into its ordering
 * tables, and the variables game code reads. While wide pictures or
 * captures exist, the target's rows above the view rows too (read back at a
 * save, uploaded at a load: game code reads captures back). The
 * environment's switches (guard, log, modes ready) are this process's. */
#define STATE_WIDE_ROWS WIDE_VIEW_Y
#define STATE_WIDE_STRIP 512 /* rows per transfer: 2 MB, the device's staging size */

void wideStateIo(XgState* s) {
    int guard = wd.guard, noShift = wd.noShift, logMoves = wd.logMoves, log = wd.log;
    unsigned ready = wd.ready;
    int liveCount = wd.count;
    xgStateIo(s, "wd.head", &wd, offsetof(__typeof__(wd), key));
    if (xgStateLoading(s)) {
        wd.guard = guard;
        wd.noShift = noShift;
        wd.logMoves = logMoves;
        wd.log = log;
        wd.ready = ready;
        for (int i = 0; i < liveCount; i++) {
            wd.key[wd.used[i]] = NULL;
        }
    }
    int count = (int)xgStateLen(s, "wd.count", (size_t)liveCount);
    if (count < 0 || count > PACKETS) {
        count = 0;
    }
    /* The live entries in one chunk (a rewind capture compares chunk by chunk:
     * their number changes from frame to frame). */
    typedef struct {
        int h;
        const void* key;
        WidePacket val;
    } Live;
    Live* live = count > 0 ? calloc((size_t)count, sizeof(Live)) : NULL;
    for (int i = 0; live != NULL && xgStateSaving(s) && i < count; i++) {
        live[i].h = wd.used[i];
        live[i].key = wd.key[wd.used[i]];
        live[i].val = wd.val[wd.used[i]];
    }
    xgStateIo(s, "wd.packets", live, live != NULL ? (size_t)count * sizeof(Live) : 0);
    if (xgStateLoading(s)) {
        for (int i = 0; live != NULL && i < count; i++) {
            int h = live[i].h & (PACKETS - 1);
            wd.used[i] = h;
            wd.key[h] = live[i].key;
            wd.val[h] = live[i].val;
        }
        wd.count = live != NULL ? count : 0;
    }
    free(live);
    XG_STATE_VAR(s, wd.arena);
    for (int a = 0; a < ARENAS; a++) {
        size_t used = xgStateLen(s, "wd.arenaUsed", wd.arenaUsed[a]);
        used = used <= ARENA_BYTES ? used : 0;
        xgStateIo(s, "wd.arenaMem", wd.arenaMem[a], used);
        if (xgStateLoading(s)) {
            wd.arenaUsed[a] = used;
        }
    }
    XG_STATE_VAR(s, pcWideView);
    XG_STATE_VAR(s, pcWideCull);
    XG_STATE_VAR(s, pcVerticalCull);
    XG_STATE_VAR(s, pcWideOnlyScope);
    XG_STATE_VAR(s, wideSlotsLive);

    size_t strip = (size_t)WIDE_W * STATE_WIDE_STRIP;
    int saving = xgStateSaving(s);
    size_t rows = xgStateLen(s, "wide.rows", saving && (wideActive() || wideSlotsLive) ? STATE_WIDE_ROWS : 0);
    int columns = gvkWideUsedWidth();
    xgStateIo(s, "wide.columns", &columns, sizeof columns);
    if (xgStateLoading(s)) gvkRestoreWideWidth(columns);
    uint16_t* px = rows > 0 ? malloc(strip * sizeof(uint16_t)) : NULL;
    for (size_t y = 0; y < rows && y < STATE_WIDE_ROWS; y += STATE_WIDE_STRIP) {
        if (px != NULL && saving && !xgStateLate(s, 1, 0, (int)y, WIDE_W, STATE_WIDE_STRIP, px, WIDE_W)) {
            gvkReadWide(px, 0, (int)y, WIDE_W, STATE_WIDE_STRIP);
        }
        xgStateIo(s, "wide.target", px, px != NULL ? strip * sizeof(uint16_t) : 0);
        if (px != NULL && xgStateLoading(s)) {
            VRect r = {0, (int)y, WIDE_W - 1, (int)y + STATE_WIDE_STRIP - 1};
            gvkRestoreWide(r, px, WIDE_W);
        }
    }
    free(px);
}
