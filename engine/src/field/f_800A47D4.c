#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "pc_gameplay.h"
#include "main/memory.h"
#include "field/field.h"
#include "field/screen_effects.h"

void SetPolyFT4(void* p);
void SetTile(void* p);
#include "pc_interpolation.h"

/* LIBGPU primitive macros (not in psyq.h yet) */
#define setRGB0(p, _r0, _g0, _b0) ((p)->r0 = (_r0), (p)->g0 = (_g0), (p)->b0 = (_b0))
#define setXYWH(p, _x0, _y0, _w, _h)                                          \
    ((p)->x0 = (_x0), (p)->y0 = (_y0), (p)->x1 = (_x0) + (_w), (p)->y1 = (_y0), \
     (p)->x2 = (_x0), (p)->y2 = (_y0) + (_h), (p)->x3 = (_x0) + (_w), (p)->y3 = (_y0) + (_h))
#define setUVWH(p, _u0, _v0, _w, _h)                                          \
    ((p)->u0 = (_u0), (p)->v0 = (_v0), (p)->u1 = (_u0) + (_w), (p)->v1 = (_v0), \
     (p)->u2 = (_u0), (p)->v2 = (_v0) + (_h), (p)->u3 = (_u0) + (_w), (p)->v3 = (_v0) + (_h))
void SetDrawMove(DR_MOVE* p, RECT* rect, s32 x, s32 y);

/* Noah: screenDistrotionAllocated (field .data) */
extern s32 screenDistrotionAllocated;

void setScreenDistortionParams(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f, s32 frames);

void freeScreenDistortion(void) {
    fieldScreenEffects.m0_screenDistortionRunning = 0;
    if (screenDistrotionAllocated != 0) {
        freeHeap(fieldScreenEffects.m3C_drawMove[0]);
        freeHeap(fieldScreenEffects.m3C_drawMove[1]);
        freeHeap(fieldScreenEffects.m44_polys[0]);
        freeHeap(fieldScreenEffects.m44_polys[1]);
        screenDistrotionAllocated = 0;
    }
}

/* DR_MOVE source origins for the 15 64x16 strips copied to (0x3C0, i*16):
 * {x, y} pairs; only setupScreenDistortion reads it. Flat u16[30] indexed [i * 2]:
 * as u16[15][2] (or a struct array) GCC does not strength-reduce the x
 * column into its own pointer as retail does. */
extern u16 screenDistortionStripOrigins[30];

void setupScreenDistortion(s32 noParams) {
    s32 i;
    s32 j;
    RECT rect;

    fieldScreenEffects.m0_screenDistortionRunning = 1;
    if (screenDistrotionAllocated == 0) {
        fieldScreenEffects.m3C_drawMove[0] = allocateHeap(PC_HEAP_SIZED(16 * sizeof(DR_MOVE), 16 * 0x18), 0);
        fieldScreenEffects.m3C_drawMove[1] = allocateHeap(PC_HEAP_SIZED(16 * sizeof(DR_MOVE), 16 * 0x18), 0);
        fieldScreenEffects.m44_polys[0] = allocateHeap(PC_HEAP_SIZED(360 * sizeof(POLY_FT4), 360 * 0x28), 0);
        fieldScreenEffects.m44_polys[1] = allocateHeap(PC_HEAP_SIZED(360 * sizeof(POLY_FT4), 360 * 0x28), 0);
        screenDistrotionAllocated = 1;
        for (i = 0; i < 17; i++) {
            for (j = 0; j < 20; j++) {
                POLY_FT4* p = &fieldScreenEffects.m44_polys[0][j + i * 20];
                POLY_FT4* p2 = &fieldScreenEffects.m44_polys[1][j + i * 20];

                SetPolyFT4(p);
                SetSemiTrans(p, 0);
                setRGB0(p, 0x80, 0x80, 0x80);
                setXYWH(p, j << 4, i << 4, 0x10, 0x10);
                if (i >= 14) {
                    /* rows 14-16 sample the 64x16 strips DR_MOVEd to x 0x3C0;
                     * v2/v3 are written as a + (b + 16): (a + b) + 16 lets
                     * combine fold both shifts into one */
                    p->u0 = (j << 4) & 0x3F;
                    p->v0 = ((j >> 2) << 4) + (i - 14) * 0x50;
                    p->u1 = ((j << 4) & 0x3F) + 0x10;
                    p->v1 = ((j >> 2) << 4) + (i - 14) * 0x50;
                    p->u2 = (j << 4) & 0x3F;
                    p->v2 = ((j >> 2) << 4) + ((i - 14) * 0x50 + 0x10);
                    p->u3 = ((j << 4) & 0x3F) + 0x10;
                    p->v3 = ((j >> 2) << 4) + ((i - 14) * 0x50 + 0x10);
                    p->tpage = GetTPage(2, 0, 0x3C0, 0);
                    *p2 = *p;
                } else {
                    setUVWH(p, (j << 4) & 0x3F, i << 4, 0x10, 0x10);
                    p->tpage = GetTPage(2, 0, (j << 4) & 0xFFC0, 0);
                    *p2 = *p;
                    p2->tpage = GetTPage(2, 0, (j << 4) & 0xFFC0, 0x100);
                }
            }
        }
        rect.y = 0x20;
        rect.w = 0x140;
        rect.x = 0;
        rect.h = 0xC0;
        SetDrawMove(fieldScreenEffects.m3C_drawMove[0], &rect, 0, 0);
        rect.y = 0x120;
        SetDrawMove(fieldScreenEffects.m3C_drawMove[1], &rect, 0, 0x100);
        rect.w = 0x40;
        rect.h = 0x10;
        for (i = 0; i < 15; i++) {
            rect.x = screenDistortionStripOrigins[i * 2];
            rect.y = screenDistortionStripOrigins[i * 2 + 1];
            SetDrawMove(&fieldScreenEffects.m3C_drawMove[0][i + 1], &rect, 0x3C0, i << 4);
            rect.y = screenDistortionStripOrigins[i * 2 + 1] + 0x100;
            SetDrawMove(&fieldScreenEffects.m3C_drawMove[1][i + 1], &rect, 0x3C0, i << 4);
        }
    }
    if (noParams == 0) {
        s32 a = getImmediateOrVariableUnsigned(1);
        s32 b = getImmediateOrVariableUnsigned(3);
        s32 c = getImmediateOrVariableUnsigned(5);
        s32 d = getImmediateOrVariableUnsigned(7);
        s32 e = getImmediateOrVariableUnsigned(9);
        s32 f = getImmediateOrVariableUnsigned(0xB);
        s32 g = getImmediateOrVariableUnsigned(0xD);

        setScreenDistortionParams(a, b, c, d, e, f, g);
    }
    fieldScreenEffects.m2_screenDistortionFadeOut = 0;
}

/* Noah: setScreenDistortionParams: step each parameter from its current
 * value towards the target (16.16) over `frames` frames */
void setScreenDistortionParams(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f, s32 frames) {
    if (frames == 0) {
        frames = 1;
    }
    fieldScreenEffects.m4_distortionParamDivider = frames;
    fieldScreenEffects.m20_distortionParam[0] = ((a << 16) - fieldScreenEffects.m8_distortionParamDiff[0]) / frames;
    fieldScreenEffects.m20_distortionParam[1] = ((b << 16) - fieldScreenEffects.m8_distortionParamDiff[1]) / frames;
    fieldScreenEffects.m20_distortionParam[2] = ((c << 16) - fieldScreenEffects.m8_distortionParamDiff[2]) / frames;
    fieldScreenEffects.m20_distortionParam[3] = ((d << 16) - fieldScreenEffects.m8_distortionParamDiff[3]) / frames;
    fieldScreenEffects.m20_distortionParam[4] = ((e << 16) - fieldScreenEffects.m8_distortionParamDiff[4]) / frames;
    fieldScreenEffects.m20_distortionParam[5] = ((f << 16) - fieldScreenEffects.m8_distortionParamDiff[5]) / frames;
}

/* 0xC0-byte primitive block per frame buffer, linked after the strips: it
 * is gDialogDrModes[buffer][3] (rows of 16 DR_MODEs), which the PC aliases
 * (port/src/game/field/bss.c), so there it takes that row's stride. */
typedef struct {
    DR_MODE m0_modes[16];
} Unk800B1E18;

/* Noah: pCurrentFieldRenderingContext */
extern sFieldRenderContext* pCurrentFieldRenderingContext;
/* Noah: g_frameOddOrEven */
extern s32 g_frameOddOrEven;
extern Unk800B1E18 D_800B1E18[2];
/* main: cosine lookup (4096 = 360 degrees) */
s32 getAngleSin(s32 angle);

#define POLYS fieldScreenEffects.m44_polys[g_frameOddOrEven]

/*
 * The wide picture (include/pc_wide.h). The distortion redraws the frame in
 * place as a 20 x 17 grid of 16 x 16 tiles textured from the frame itself:
 * rows 0-13 drawn 32 lines low (each reads lines nothing has overwritten yet),
 * then a DR_MOVE puts the frame back up, and rows 14-16 come from copies of
 * the bottom 48 lines made first (the strips at 0x3C0). In the wide picture
 * the margins get the same: more columns of tiles, the wave continued into
 * them. The core's copy of the DR_MOVE moves whole wide rows; the strips have
 * no margins, so the margins' lines 176-207 are first copied into scratch
 * beside the frame (PC_WIDE_BELOW) and the margin tiles of rows 14 and 15
 * read them there; lines 208-223 are kept by drawing no margin tiles over
 * them (rows 11-13 only draw what rows 14-16 cover), and the DR_MOVE carries
 * them to lines 176-191, where row 16's margin tiles read them. The game's own
 * tiles are marked as handled.
 *
 * With expanded vertical framing the display copy also has scene rows above
 * and below the frame buffer (pcWideFrameLines; 10 and 6 in fields), at 4:3
 * too. The game pins its picture's top and bottom edges (row 0 starts at line
 * 0, row 16 ends at line 224), so a band continues the row beside it: tiles
 * with that row's column edges (the wave's x), flat in y, across the whole
 * width. Nothing of the game reads or moves the bands, but the tiles redraw
 * them from themselves and the game's rows 12-13, drawn 32 lines low, run
 * into the bottom band in the copy: so both bands are first copied into
 * scratch beside the frame too (below the margins' blocks), and the tiles
 * read them there; the bottom band is put back from it before its tiles, so
 * transparent (black) texels leave the band's own pixels, as in the top band.
 * VRAM, the GTE and the game's state are untouched: every piece is wide-only
 * and the wave comes from getAngleSin, as the game's.
 *
 * The scratch (x from 320 + M on, frame rows 0-223) is written by pieces
 * placed with pcWidePrim's dx: the GPU wraps vertex x to 11 bits (-1024 to
 * 1023) before the wide picture's shift, so a vertex at 320 + 2M would not
 * survive past M = 352.
 */
#define WIDE_COLS_MAX 34 /* per side: M / 16 + 2 for M up to 512 */
#define WIDE_BAND_Y 0x40  /* scratch rows of the bands' copies, below the margins' blocks */
#define WIDE_BAND_MAX 16  /* rows a band tile reaches (fields: 10 above, 6 below) */

static s32 wideReady;   /* this frame's distortion has its wide version */
static s32 wideMargins; /* ... with margins (pcWideView) */
static s32 wideTop;     /* expanded rows above the frame buffer */
static s32 wideBottom;  /* ... and below it */
static s32 wideOrigin;  /* the bands' copy: frame x of its first piece (a tile edge at or left of -M) */
static s32 widePiece;   /* ... each piece's width (a whole number of tiles) */

/* The wave's displacement of column j's left edge (as the game computes it). */
static s32 wideWaveDx(s32 j) {
    s32 ampX = (s16)(fieldScreenEffects.m8_distortionParamDiff[0] >> 16);
    s32 stepX = (s16)(fieldScreenEffects.m8_distortionParamDiff[2] >> 16);

    return (getAngleSin(fieldScreenEffects.m38_phaseX + j * stepX) * ampX) >> 12;
}

/* Four 16-row blocks beside the visible right edge. Expanded vertical
 * framing owns the old scratch rows below line 224, so they cannot also
 * hold this stash. Stacking the blocks at y=0..63 keeps them in the same
 * frame's texture mapping and fits even at the maximum margin of 512. */
static s32 wideStashX(void) {
    return 0x140 + pcWideView;
}

static s32 wideStashY(s32 q) {
    return q << 4;
}

/* Where the bands' copy of frame column x, band row r (0..wideTop - 1 above,
 * then the rows below) is in the scratch: x and y in frame coordinates. The
 * band (-M..320 + M) is cut at tile edges into pieces no wider than the
 * margins' blocks (128 at least), stacked below them, so the scratch reaches
 * no further right than they do (the hi-res copy of the wide target grows
 * with it). */
static s32 wideBandPiece(s32 x) {
    return x < wideOrigin ? 0 : (x - wideOrigin) / widePiece;
}

static s32 wideBandX(s32 x) {
    return wideStashX() + x - (wideOrigin + wideBandPiece(x) * widePiece);
}

static s32 wideBandY(s32 x, s32 r) {
    return WIDE_BAND_Y + wideBandPiece(x) * (wideTop + wideBottom) + r;
}

/* A tile: a copy of the game's tile `src` (its lines y0..y3) from x0 to x1,
 * textured from the source columns s0..s0 + 16 of page `tpage` at lines
 * v0..v1 (texel x = texDx + u, u 0 at s0; texel y = texDy + v). The wide
 * picture's own edges (x -M and 320 + M) are pinned as the PS1 pins the
 * screen's: the tile whose source crosses one starts or ends there, on screen
 * and in its source, and tiles wholly past one are left out, so no texel from
 * beyond the picture (or from the wrong block of the stash) shows at its
 * edges as the wave moves. */
static void wideTileTex(const POLY_FT4* src, s32 x0, s32 x1, s32 s0, s32 v0, s32 v1, u16 tpage, s32 texDx, s32 texDy,
                        unsigned int flags, s32 captureX, s32 captureY, s32 captureOp) {
    s32 lo = -pcWideView;
    s32 hi = 0x140 + pcWideView;
    s32 u0 = 0;
    s32 u1 = 0x10;
    s32 edge0;
    s32 edge1;
    POLY_FT4* q;

    if (s0 + 0x10 <= lo || s0 >= hi) {
        return;
    }
    if (s0 < lo) {
        u0 = lo - s0;
        x0 = lo;
    }
    if (s0 + 0x10 > hi) {
        u1 = hi - s0;
        x1 = hi;
    }
    edge0 = x0; edge1 = x1;
    /* Keep an invisible authored endpoint for smoothing when a pinned
     * boundary tile moves out of view. Its native triangle has zero area. */
    if (x1 <= x0) {
        if (x0 == lo) x0 = x1;
        else x1 = x0;
    }
    if ((q = pcWideAlloc(sizeof(POLY_FT4))) == NULL) {
        return;
    }
    *q = *src;
    q->x0 = q->x2 = x0;
    q->x1 = q->x3 = x1;
    q->u0 = q->u2 = u0;
    q->u1 = q->u3 = u1;
    q->v0 = q->v1 = v0;
    q->v2 = q->v3 = v1;
    q->tpage = tpage;
    addPrim(&pCurrentFieldRenderingContext->mCC_OT[1], q);
    pcWidePrimTex2D(q, 0, PC_WIDE_ONLY | PC_WIDE_HANDLED | flags, texDx, texDy);
    pcInterpolationCapturePrim(q, captureOp, captureX, captureY);
    pcInterpolationWaveEdgePrim(q, u0, u1, edge0-x0, edge1-x1);
    pcInterpolationKeyType(&fieldScreenEffects, fieldScreenEffects.m44_polys[0],
        0x80000000u ^ (unsigned)(s0 + 1024) * 16777619u ^ (unsigned)v0 * 65537u ^
        (unsigned)v1 * 8191u ^ (unsigned)captureY * 131071u ^ (unsigned)captureOp * 524287u,
        q, PC_INTERP_WAVE);
}

/* A 16-line tile of the margins at line v of the frame (or the stash). */
static void wideTile(const POLY_FT4* src, s32 x0, s32 x1, s32 s0, s32 v, u16 tpage, s32 texDx, unsigned int flags, s32 captureY, s32 captureOp) {
    wideTileTex(src, x0, x1, s0, v, v + 0x10, tpage, texDx, 0, flags, flags & PC_WIDE_BELOW ? texDx : s0, captureY, captureOp);
}

/* Column edges of the margins for one row: left edges of columns -n..-1 and
 * 20..19+n, and the right edge of the last of each side. right20: the left
 * edge of column 20 (the game's right edge of column 19). */
static void wideEdges(s32 n, s32 right20, s32* left, s32* right) {
    s32 k;

    for (k = 0; k <= n; k++) {
        /* left[k]: column -n + k's left edge; left[n] = 0 (the game's) */
        left[k] = k == n ? 0 : ((k - n) << 4) + wideWaveDx(k - n);
        /* right[k]: column 20 + k's left edge; right[n]: the last right edge */
        right[k] = k == 0 ? right20 : ((20 + k) << 4) + wideWaveDx(20 + k);
    }
}

/* The columns of a band (below: the bottom one) beside game row `row` (0 or
 * 16), its tiles flat from line y0 to y1, read from the bands' copy. */
static void wideBandTiles(s32 row, s32 below, s32 y0, s32 y1) {
    s32 n = wideMargins ? pcWideView / 16 + 2 : 0;
    s32 rows = below ? wideBottom : wideTop;
    s32 r0 = below ? wideTop : 0;
    s32 left[WIDE_COLS_MAX + 1];
    s32 right[WIDE_COLS_MAX + 1];
    s32 j;
    s32 k;
    s32 s0;
    POLY_FT4 tile = POLYS[row * 20];
    u16 tpage = GetTPage(2, 0, 0, g_frameOddOrEven << 8);

    if (rows == 0) {
        return;
    }
    tile.y0 = tile.y1 = y0;
    tile.y2 = tile.y3 = y1;
    for (j = 0; j < 20; j++) {
        s0 = j << 4;
        wideTileTex(&tile, POLYS[row * 20 + j].x0, POLYS[row * 20 + j].x1, s0, 0, rows, tpage, wideBandX(s0),
                    wideBandY(s0, r0), 0, wideBandX(s0), wideBandY(s0, r0), PC_INTERP_CAPTURE_SAMPLE);
    }
    if (n == 0) {
        return;
    }
    wideEdges(n, POLYS[row * 20 + 19].x1, left, right);
    for (k = 0; k < n; k++) {
        s0 = (k - n) << 4;
        wideTileTex(&tile, left[k], left[k + 1], s0, 0, rows, tpage, wideBandX(s0), wideBandY(s0, r0), 0, wideBandX(s0), wideBandY(s0, r0), PC_INTERP_CAPTURE_SAMPLE);
        s0 = (20 + k) << 4;
        wideTileTex(&tile, right[k], right[k + 1], s0, 0, rows, tpage, wideBandX(s0), wideBandY(s0, r0), 0, wideBandX(s0), wideBandY(s0, r0), PC_INTERP_CAPTURE_SAMPLE);
    }
}

/* Lines y0..y0 + rows - 1 of the frame, all of -M..320 + M, as straight
 * copies: from the frame into the bands' copy (save), or back (restore). */
static void wideBandCopy(s32 y0, s32 rows, s32 r0, s32 save) {
    s32 x;
    s32 w;
    s32 end = 0x140 + pcWideView;
    POLY_FT4* p;

    for (x = -pcWideView; x < end; x += w) {
        w = end - x < 0x40 ? end - x : 0x40;
        if (wideBandPiece(x + w - 1) != wideBandPiece(x)) {
            /* a copy stays in one piece of the bands' copy */
            w = wideOrigin + (wideBandPiece(x) + 1) * widePiece - x;
        }
        p = pcWideAlloc(sizeof(POLY_FT4));
        if (p == NULL) {
            return;
        }
        *p = POLYS[0];
        if (save) {
            setXYWH(p, 0, wideBandY(x, r0), w, rows); /* at x wideBandX(x): pcWidePrim's dx */
        } else {
            setXYWH(p, x, y0, w, rows);
        }
        pcInterpolationCapturePrim(p, save ? PC_INTERP_CAPTURE_STASH : PC_INTERP_CAPTURE_SAMPLE,
            save ? x : wideBandX(x), save ? y0 : wideBandY(x, r0));
        setUVWH(p, 0, 0, w, rows);
        p->tpage = GetTPage(2, 0, 0, g_frameOddOrEven << 8);
        addPrim(&pCurrentFieldRenderingContext->mCC_OT[1], p);
        if (save) {
            pcWidePrimTex2D(p, wideBandX(x), PC_WIDE_ONLY | PC_WIDE_HANDLED | PC_WIDE_BELOW, x, y0);
        } else {
            pcWidePrimTex2D(p, 0, PC_WIDE_ONLY | PC_WIDE_HANDLED, wideBandX(x), wideBandY(x, r0));
        }
    }
}

/* Rows 14-16 of the margins (after the DR_MOVE): from the stash. Then the
 * band below the frame, from its copy: put back as it was (the game's rows
 * 12-13 ran into it in the copy), then redrawn by row 16's columns. */
static void wideBottomRows(void) {
    s32 n = pcWideView / 16 + 2; /* one past the edge: the wave moves it */
    s32 left[WIDE_COLS_MAX + 1];
    s32 right[WIDE_COLS_MAX + 1];
    s32 row;
    s32 k;
    s32 b;
    POLY_FT4 tile;
    POLY_FT4* src = &tile;
    u16 tpage = GetTPage(2, 0, 0, g_frameOddOrEven << 8);

    /* linked first: drawn after the band is put back */
    wideBandTiles(16, 1, 0xE0, 0xE0 + wideBottom);
    if (wideBottom != 0) {
        wideBandCopy(0xE0, wideBottom, wideTop, 0);
    }
    if (!wideMargins) {
        return;
    }
    /* linked 14 first: row 16 is drawn first, before row 14 covers the lines
     * it reads */
    for (row = 14; row < 17; row++) {
        tile = POLYS[row * 20];
        if (row == 14 && tile.y0 > 0xB0) {
            /* The game's row 11 shows above it where the wave lowers it; the
             * margins have no row 11 (its lines are row 16's source): row 14
             * reaches up to line 176 instead. */
            tile.y0 = tile.y1 = 0xB0;
        }
        b = row - 14;
        /* from the game's right edge, also where the wave leaves it short of
         * 320: the PS1's last columns there keep stale lines (the TV's
         * overscan hides them; mid-picture they are a flickering line), so
         * the margin's first tile covers them (the frame counts as shifted) */
        wideEdges(n, POLYS[row * 20 + 19].x1, left, right);
        for (k = 0; k < n; k++) {
            if (row == 16) {
                /* lines 208-223, moved up to 176 by the DR_MOVE */
                wideTile(src, left[k], left[k + 1], (k - n) << 4, 0xB0, tpage, (k - n) << 4, 0, 0x20, PC_INTERP_CAPTURE_SAMPLE);
                wideTile(src, right[k], right[k + 1], (20 + k) << 4, 0xB0, tpage, (20 + k) << 4, 0, 0x20, PC_INTERP_CAPTURE_SAMPLE);
                continue;
            }
            /* left: block b, its x -M at stashX; right: block 2 + b, its x 320 there */
            wideTile(src, left[k], left[k + 1], (k - n) << 4, wideStashY(b), tpage,
                     wideStashX() + pcWideView + ((k - n) << 4), PC_WIDE_BELOW,
                     0, PC_INTERP_CAPTURE_SAMPLE);
            wideTile(src, right[k], right[k + 1], (20 + k) << 4, wideStashY(2 + b), tpage,
                     wideStashX() - 0x140 + ((20 + k) << 4), PC_WIDE_BELOW,
                     0, PC_INTERP_CAPTURE_SAMPLE);
        }
    }
}

/* Rows 0-10 of the margins (before the DR_MOVE), linked row 0 first so they
 * are drawn bottom up as the game's are; each reads the frame where its game
 * counterpart would. Rows 11-13 are left out (rows 14-16 cover what they
 * draw), and row 10 stops at line 208, so lines 208-223 keep the frame's.
 * Then the band above the frame, beside row 0 (which starts at line 0). */
static void wideTopRows(void) {
    s32 n = pcWideView / 16 + 2; /* one past the edge: the wave moves it */
    s32 left[WIDE_COLS_MAX + 1];
    s32 right[WIDE_COLS_MAX + 1];
    s32 row;
    s32 k;
    POLY_FT4 src;
    u16 tpage = GetTPage(2, 0, 0, g_frameOddOrEven << 8);

    if (wideMargins) {
        wideEdges(n, 0x140, left, right);
        for (row = 0; row < 11; row++) {
            src = POLYS[row * 20];
            if (src.y2 > 0xD0) {
                src.y2 = src.y3 = 0xD0;
            }
            for (k = 0; k < n; k++) {
                wideTile(&src, left[k], left[k + 1], (k - n) << 4, row << 4, tpage, (k - n) << 4, 0, 0, PC_INTERP_CAPTURE_BODY);
                wideTile(&src, right[k], right[k + 1], (20 + k) << 4, row << 4, tpage, (20 + k) << 4, 0, 0, PC_INTERP_CAPTURE_BODY);
            }
        }
    }
    wideBandTiles(0, 0, -wideTop, 0);
}

/* The stash (drawn first): each block cleared, then the margin's lines copied
 * in pieces 64 wide at most; and the bands' copy, cleared, then copied. */
static void wideStash(void) {
    s32 m = pcWideView;
    s32 q;
    s32 x;
    s32 w;
    POLY_FT4* p;
    TILE* t;

    /* linked first: drawn after the clear below */
    if (wideTop != 0) {
        wideBandCopy(-wideTop, wideTop, 0, 1);
    }
    if (wideBottom != 0) {
        wideBandCopy(0xE0, wideBottom, wideTop, 1);
    }
    for (x = wideOrigin; x < 0x140 + m && wideTop + wideBottom != 0; x += widePiece) {
        /* each piece of the bands' copy cleared first: a copy leaves black
         * texels out */
        t = pcWideAlloc(sizeof(TILE));
        if (t == NULL) {
            return;
        }
        SetTile(t);
        setRGB0(t, 0, 0, 0);
        t->x0 = 0; /* at x wideStashX(): pcWidePrim's dx */
        t->y0 = wideBandY(x, 0);
        t->w = widePiece;
        t->h = wideTop + wideBottom;
        addPrim(&pCurrentFieldRenderingContext->mCC_OT[1], t);
        pcWidePrim(t, wideStashX(), PC_WIDE_ONLY | PC_WIDE_BELOW | PC_WIDE_NOEXTEND);
        pcInterpolationCapturePrim(t, PC_INTERP_CAPTURE_SKIP, 0, 0);
    }
    if (!wideMargins) {
        return;
    }
    for (q = 0; q < 4; q++) {
        for (x = 0; x < m; x += 0x40) {
            w = m - x < 0x40 ? m - x : 0x40;
            p = pcWideAlloc(sizeof(POLY_FT4));
            if (p == NULL) {
                return;
            }
            *p = POLYS[0];
            setXYWH(p, 0, wideStashY(q), w, 0x10); /* at x wideStashX() + x: pcWidePrim's dx */
            setUVWH(p, 0, 0xB0 + (q % 2) * 0x10, w, 0x10);
            p->tpage = GetTPage(2, 0, 0, g_frameOddOrEven << 8);
            addPrim(&pCurrentFieldRenderingContext->mCC_OT[1], p);
            /* from x -M (left) or 320 (right) of the frame */
            pcWidePrimTex(p, wideStashX() + x, PC_WIDE_ONLY | PC_WIDE_HANDLED | PC_WIDE_BELOW, (q < 2 ? -m : 0x140) + x);
            pcInterpolationCapturePrim(p, PC_INTERP_CAPTURE_STASH, (q < 2 ? -m : 0x140) + x, 0);
        }
        t = pcWideAlloc(sizeof(TILE));
        if (t == NULL) {
            return;
        }
        SetTile(t);
        setRGB0(t, 0, 0, 0);
        t->x0 = 0; /* at x wideStashX(): pcWidePrim's dx */
        t->y0 = wideStashY(q);
        t->w = m;
        t->h = 0x10;
        addPrim(&pCurrentFieldRenderingContext->mCC_OT[1], t);
        pcWidePrim(t, wideStashX(), PC_WIDE_ONLY | PC_WIDE_BELOW | PC_WIDE_NOEXTEND);
        pcInterpolationCapturePrim(t, PC_INTERP_CAPTURE_SKIP, 0, 0);
    }
}

/* Whether this frame gets the wide version: a wide frame whose stash fits the
 * scratch (four blocks of M: M <= 512, 32:9 included), or a frame with
 * expanded rows. The game's tiles are marked as handled. */
static s32 wideDistortBegin(void) {
    s32 m = pcWideView;
    s32 i;

    pcWideFrameLines(0, g_frameOddOrEven << 8, &wideTop, &wideBottom);
    wideTop = wideTop < WIDE_BAND_MAX ? wideTop : WIDE_BAND_MAX;
    wideBottom = wideBottom < WIDE_BAND_MAX ? wideBottom : WIDE_BAND_MAX;
    wideMargins = m != 0 && m <= 0x200;
    wideReady = m <= 0x200 && (m != 0 || wideTop != 0 || wideBottom != 0);
    /* M = 512: three pieces of 512, rows 64-111 */
    wideOrigin = -((m + 15) & ~15);
    widePiece = m > 0x80 ? (m + 15) & ~15 : 0x80;
    if (wideReady) {
        for (i = 0; i < 17 * 20; i++) {
            pcWidePrim(&POLYS[i], 0, PC_WIDE_HANDLED);
        }
    }
    return wideReady;
}

/* Noah: updateAndRenderScreenDistortion: advance the parameters, then warp
 * the 20x17 grid of 16x16 FT4 tiles with two cosine waves and link it (plus
 * the DR_MOVEs that snapshot the frame into VRAM) into OT[1] */
s32 pcFieldSplit(s32 d); /* f_80072140.c */
void updateAndRenderScreenDistortion(void) {
    s32 row;
    s32 j;
    s32 angleX;
    s32 angleY;
    s32 dx;
    s32 dy;
    s32 ampX;
    s32 ampY;
    s32 stepX;
    s32 stepY;
    POLY_FT4* p;

    if (fieldScreenEffects.m0_screenDistortionRunning == 0) {
        return;
    }
    if (fieldScreenEffects.m2_screenDistortionFadeOut == 0) {
        if (fieldScreenEffects.m4_distortionParamDivider <= 0) {
            goto render;
        }
    update:
        if (!pcFieldOffTick()) { /* Enhanced phase 3: the wave parameters step at the native cadence */
            fieldScreenEffects.m8_distortionParamDiff[0] += fieldScreenEffects.m20_distortionParam[0];
            fieldScreenEffects.m8_distortionParamDiff[1] += fieldScreenEffects.m20_distortionParam[1];
            fieldScreenEffects.m8_distortionParamDiff[2] += fieldScreenEffects.m20_distortionParam[2];
            fieldScreenEffects.m8_distortionParamDiff[3] += fieldScreenEffects.m20_distortionParam[3];
            fieldScreenEffects.m8_distortionParamDiff[4] += fieldScreenEffects.m20_distortionParam[4];
            fieldScreenEffects.m8_distortionParamDiff[5] += fieldScreenEffects.m20_distortionParam[5];
            fieldScreenEffects.m4_distortionParamDivider--;
        }
    } else {
        if (fieldScreenEffects.m4_distortionParamDivider > 0) {
            goto update;
        }
        fieldScreenEffects.m8_distortionParamDiff[5] = 0;
        fieldScreenEffects.m8_distortionParamDiff[4] = 0;
        fieldScreenEffects.m8_distortionParamDiff[3] = 0;
        fieldScreenEffects.m8_distortionParamDiff[2] = 0;
        fieldScreenEffects.m8_distortionParamDiff[1] = 0;
        fieldScreenEffects.m8_distortionParamDiff[0] = 0;
        fieldScreenEffects.m0_screenDistortionRunning = 0;
    }
render:
    /* m38 must be touched first: CSE bases the block's other addresses on
     * it, and retail keeps exactly that one in a register */
    /* (Enhanced, xg-hbc.19.3: at 60 Hz each frame takes its part of the native frame's phase step,
     * pcFieldSplit; the wave ran twice as fast) */
    fieldScreenEffects.m38_phaseX += (s16)pcFieldSplit((s16)(fieldScreenEffects.m8_distortionParamDiff[4] >> 16));
    fieldScreenEffects.m3A_phaseY += (s16)pcFieldSplit((s16)(fieldScreenEffects.m8_distortionParamDiff[5] >> 16));
    ampX = (s16)(fieldScreenEffects.m8_distortionParamDiff[0] >> 16);
    ampY = (s16)(fieldScreenEffects.m8_distortionParamDiff[1] >> 16);
    stepX = (s16)(fieldScreenEffects.m8_distortionParamDiff[2] >> 16);
    stepY = (s16)(fieldScreenEffects.m8_distortionParamDiff[3] >> 16);
    angleY = fieldScreenEffects.m3A_phaseY + stepY * 11;
    pcFieldPicAdd(PC_FIELD_PIC_DISTORT, fieldScreenEffects.m38_phaseX); /* Enhanced oracle: the wave as drawn */
    pcFieldPicAdd(PC_FIELD_PIC_DISTORT, fieldScreenEffects.m3A_phaseY);
    pcFieldPicAdd(PC_FIELD_PIC_DISTORT, ampX | ampY << 16);
    pcFieldPicAdd(PC_FIELD_PIC_DISTORT, stepX | stepY << 16);

    for (row = 14; row < 17; row++) {
        dy = (getAngleSin(angleY) * ampY) >> 12;
        angleY += stepY;
        angleX = fieldScreenEffects.m38_phaseX;
        for (j = 0; j < 20; j++) {
            dx = (getAngleSin(angleX) * ampX) >> 12;
            angleX += stepX;
            p = &POLYS[row * 20 + j];
            /* the special cases re-index with row * 20 + j: CSE substitutes
             * the tested constant (j == 19, row == 16, ...), as retail does */
            if (j != 0) {
                p->x0 = p->x2 = p[-1].x1 = p[-1].x3 = (j << 4) + dx;
                if (j == 19) {
                    POLYS[row * 20 + j].x1 = POLYS[row * 20 + j].x3 = dx + 0x140;
                }
            } else {
                POLYS[row * 20 + j].x0 = POLYS[row * 20 + j].x2 = 0;
            }
            POLYS[row * 20 + j].y0 = POLYS[row * 20 + j].y1 = POLYS[row * 20 + j - 20].y2 =
                POLYS[row * 20 + j - 20].y3 = (row << 4) + dy - 0x30;
            if (row == 16) {
                POLYS[row * 20 + j].y2 = POLYS[row * 20 + j].y3 = 0xE0;
            }
            {
                pcInterpolationKeyType(&fieldScreenEffects, fieldScreenEffects.m44_polys[0], row*20+j, p, PC_INTERP_WAVE);
                s32 strip = (row - 14) * 5 + (j >> 2);
                pcInterpolationCapturePrim(p, PC_INTERP_CAPTURE_SAMPLE | (row == 14 && j == 0 ? PC_INTERP_CAPTURE_END : 0),
                    screenDistortionStripOrigins[strip * 2] + ((j & 3) << 4) - p->u0,
                    screenDistortionStripOrigins[strip * 2 + 1] - p->v0);
            }
            addPrim(&pCurrentFieldRenderingContext->mCC_OT[1], p);
        }
    }

    /* drawn after the DR_MOVE (include/pc_wide.h) */
    if (wideDistortBegin()) {
        wideBottomRows();
    }
    pcInterpolationCapturePrim(fieldScreenEffects.m3C_drawMove[g_frameOddOrEven], PC_INTERP_CAPTURE_MOVE, 0, 0);
    addPrim(&pCurrentFieldRenderingContext->mCC_OT[1], fieldScreenEffects.m3C_drawMove[g_frameOddOrEven]);

    angleY = fieldScreenEffects.m3A_phaseY;
    for (row = 0; row < 14; row++) {
        dy = (getAngleSin(angleY) * ampY) >> 12;
        angleY += stepY;
        angleX = fieldScreenEffects.m38_phaseX;
        for (j = 0; j < 20; j++) {
            dx = (getAngleSin(angleX) * ampX) >> 12;
            angleX += stepX;
            p = &POLYS[row * 20 + j];
            if (row != 0) {
                if (row == 13) {
                    POLYS[row * 20 + j - 20].y2 = POLYS[row * 20 + j - 20].y3 = POLYS[row * 20 + j].y0 =
                        POLYS[row * 20 + j].y1 = dy + 0xF0;
                    dy = (getAngleSin(angleY) * ampY) >> 12;
                    POLYS[row * 20 + j].y2 = POLYS[row * 20 + j].y3 = dy + 0xF0;
                } else {
                    p->y0 = p->y1 = p[-20].y2 = p[-20].y3 = (row << 4) + 0x20 + dy;
                }
            } else {
                POLYS[row * 20 + j].y0 = POLYS[row * 20 + j].y1 = 0x20;
            }
            if (j != 0) {
                POLYS[row * 20 + j].x0 = POLYS[row * 20 + j].x2 = POLYS[row * 20 + j - 1].x1 =
                    POLYS[row * 20 + j - 1].x3 = (j << 4) + dx;
                if (j == 19) {
                    POLYS[row * 20 + j].x1 = POLYS[row * 20 + j].x3 = 0x140;
                }
            } else {
                POLYS[row * 20 + j].x0 = POLYS[row * 20 + j].x2 = 0;
            }
            pcInterpolationKeyType(&fieldScreenEffects, fieldScreenEffects.m44_polys[0], row*20+j, p, PC_INTERP_WAVE);
            pcInterpolationCapturePrim(p, PC_INTERP_CAPTURE_BODY, ((j << 4) & ~63), 0);
            addPrim(&pCurrentFieldRenderingContext->mCC_OT[1], p);
        }
    }

    if (wideReady) {
        wideTopRows(); /* drawn before the DR_MOVE, after the stash */
    }
    for (row = 0; row < 15; row++) {
        pcInterpolationCapturePrim(&fieldScreenEffects.m3C_drawMove[g_frameOddOrEven][row + 1], row == 14 ? PC_INTERP_CAPTURE_FREEZE : PC_INTERP_CAPTURE_SKIP, 0, 0);
        addPrim(&pCurrentFieldRenderingContext->mCC_OT[1], &fieldScreenEffects.m3C_drawMove[g_frameOddOrEven][row + 1]);
    }
    if (wideReady) {
        wideStash(); /* drawn first */
    }
    pcInterpolationCapturePrim(&D_800B1E18[g_frameOddOrEven], PC_INTERP_CAPTURE_BEGIN, 0, 0);
    addPrim(&pCurrentFieldRenderingContext->mCC_OT[1], &D_800B1E18[g_frameOddOrEven]);
}
