
#include "common.h"
s32 pcFieldNativeFrameGoesOn(void); /* f_80072140.c, xg-hbc.19.3 */
#include "pc_retail_heap.h"
#include "psyq.h"
#include "pc_gameplay.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "field/field.h"
#include "field/screen_effects.h"

#include "gte_pc.h"
#include "pc_interpolation.h"
#include "battle/pc_wide_fx.h" /* GTE work without libgte's safe points */
extern s32 fieldMapNumber;
void portResetModule(const char* name);
void pcFieldMovieBlock(s32 on);
void fadeTransitionGridCorner(POLY_GT4* p, s32 corner, s32 radius, s16* color);

/* LIBGPU primitive macros (not in psyq.h yet) */

/* 0x800B11AC: the five 64-pixel-wide screen strips of the transition
 * effect (setupScreenTransitionPoly sets them up, renderScreenTransitionPoly renders them). Retail
 * addresses every array relative to one base, so they are one object. */
typedef struct {
    /* 0x000 */ DR_MODE m0_drawModes[5][2];
    /* 0x078 */ RECT m78_textureWindows[5][2];
    /* 0x0C8 */ POLY_FT4 mC8_polys[5][2];     /* 0x800B1274 */
    /* 0x258 */ SVECTOR m258_vertices[5][4];  /* 0x800B1404 */
} TransitionStrips; /* size 0x2F8 */

/* Heap block (transitionFadeGrid) of the fade-out grid: 14 rows x 20 columns of
 * 16x16 gouraud quads over the screen and each corner's colour. */
typedef struct {
    /* 0x0000 */ POLY_GT4 m0_polys[2][14 * 20];
    /* 0x71C0 */ s16 m71C0_colors[4][14 * 20];
} FadeGrid; /* size 0x7A80 */

/* Noah: sParticlePerTypeConfig (gParticlePerTypeConfig at 0x800AF27C):
 * half size, centre, then the four UVs. */
typedef struct {
    /* 0x00 */ u16 m0_halfWidth;
    /* 0x02 */ u16 m2_halfHeight;
    /* 0x04 */ u16 m4_centerX;
    /* 0x06 */ u16 m6_centerY;
    /* 0x08 */ u16 m8_u0;
    /* 0x0A */ u16 mA_v0;
    /* 0x0C */ u16 mC_u1;
    /* 0x0E */ u16 mE_v1;
    /* 0x10 */ u16 m10_u2;
    /* 0x12 */ u16 m12_v2;
    /* 0x14 */ u16 m14_u3;
    /* 0x16 */ u16 m16_v3;
} sParticlePerTypeConfig; /* size 0x18 */

typedef struct {
    /* 0x0 */ u16 m0_u;
    /* 0x2 */ u16 m2_v;
    /* 0x4 */ u16 m4_w;
    /* 0x6 */ u16 m6_h;
} UVRect;

/* 0x800AEF10: the debug overlay's 109 sprites: screen position, image
 * (index into debugCameraDigitImages) and flags (bits 0-3: flip mode, 4-7: blend). */
typedef struct {
    /* 0x0 */ u16 m0_x;
    /* 0x2 */ u16 m2_y;
    /* 0x4 */ u16 m4_image;
    /* 0x6 */ u16 m6_flags;
} DebugDigitSetup;

typedef struct {
    u32 m[0x8000 / 4];
} Block8000;

/* ---- field .data / .bss ---- */
extern s32 g_frameOddOrEven;
extern sFieldRenderContext* pCurrentFieldRenderingContext;
extern FieldBss_800AF880 fieldSceneState;    /* camera block (eye, at, cameraTan) */
extern DR_MODE gDialogDrModes[2][16];
extern s32 fieldTransitionMode;
extern s32 fieldTransitionFadeInLength;
extern s32 fieldTransitionInProgress;
extern s32 fieldGraphicsUploaded;
extern s32* fieldGraphicsAllocation;
extern s32 fullScreenTransitionEffectRenderMode;
extern s32 fullScreenTransitionEffectRenderParam;
extern s16 fieldScreenFadedOut;
extern s32 fieldDebugDisable;
extern u16 g_padButtonForField;                  /* g_padButtonForField */
extern void* mechaOverlayBuffer;                /* mechaOverlayBuffer */
extern s32 NumMechas;
extern s32 OPX_86Param;
extern s32 D_800ADB50;
extern void* D_800AFE80;
extern void* D_800B069C;
/* transition strips / fade grid */
extern TransitionStrips fieldTransitionStrips;
extern SVECTOR transitionStripRotation;              /* strip rotation */
extern s32 transitionStripScale;                  /* strip scale (0x1000 = 1.0) */
extern FadeGrid* transitionFadeGrid;
extern s32 transitionFadeGridRadius;                  /* fade grid radius */
/* VRAM backup of the particle texture page (0x3C0, 0x100, 64x256) */
extern s32 isBackBufferRamCopyEnabled;
extern u32* backBufferRamCopy;
extern RECT backBufferRamCopyRect;
/* movie player */
extern s32 onlyUpdateDirector;
extern s32 fieldMoviePlayerStatus;
extern s32 fieldMovieDisplayBuffer;
extern s32 fieldMovieStarted;
extern s32 fieldMovieFlags;
extern s32 fieldMovieStopRequest;
extern uptr fieldHeapTopMarker;
extern s32 fieldMovieCurrentFrame;                  /* movie frame (set by handleFieldMovieFrame) */
extern s32 fieldMovieWaitingFirstFrame;
extern s32 movieScrollingTextActive;
extern s32 color24BitComponentIndex;
extern u32* color24BitSource;
extern u32 color24BitWord;
extern u32* color15BitDest;
/* debug overlay */
extern s32 debugCameraDigitsAllocated;
extern s32 debugCameraDigitsFrameCounter;
extern s32 debugCameraDigitsCursorFrame;
extern POLY_FT4* debugCameraDigitPolys[2];
extern UVRect debugCameraDigitImages[117]; /* to debugCameraDigits */
extern DebugDigitSetup debugCameraDigits[0x6D]; /* the overlay's 109 prims (to debugCameraDigitsAllocated) */
/* particles */
extern sParticlePerTypeConfig gParticlePerTypeConfig[21]; /* to cameraToParticleMapping */
/* ---- main .data / .bss ---- */
extern s32 movieScrollingTextEnabled;
extern s32 fieldMusicLoadPending;
extern s32 currentlyPlayingMusic;
extern s32 mechaOverlayLoadingMode;
extern s32 rawFieldBundleSize;                  /* rawFieldBundle size */
extern u8* rawFieldBundle;
extern void* partyCharacterBuffers[3];             /* Noah: partyCharacterBuffersRaw */
extern void* partyCharacterBuffersCompressed[3];
/* ---- video player overlay (0x801D3000) ---- */
extern s32 strSingleSliceMode;
extern s32 strNextUploadEnabled;
s32 initStrPlayer(u16 width, u16 height, u16 depth, u16 sliceWidth, u16 ringSize, u16 vlcMode, u16 mode);
void startStrPlayback(s32 fileIndex, s32 offset, u16 startFrame, u16 endFrame, u16 channel, s32 flags, u16 audio,
                      u16 x0, u16 y0, u16 x1, u16 y1, u16 maxHeight, void (*callback)(u16, u16, u16));
void updateStrPlayer(void);
void freeStrPlayer(void);
void freeMechaModule(void);
/* ---- field functions ---- */
void setupFieldRenderingContext(void);
void clearFieldOrderingTable(void);
void updateAndRenderField(void);
void updateFieldInputs(void);
void updateFieldDialogsOverMovie(void);
void initFontSystem(void);
void fieldPerFrameReset(void);
void updateMusicState(void);
void brightenFieldCluts(void);
void ClearCacheAfterOverlayLoad(void);
void updateMovieSoundEvents(void);
void unloadMovieSoundSequence(void);
void loadMovieSoundSequence(void);
void stopFieldSoundEffects(void);
void copyScreenToTransitionStrips(void);
void deleteAllParticleEffects(void);
void renderMovieScrollingText(void);
void uploadScrollingTextTextures(void);
void initMovieScrollingText(void);
void freeMovieScrollingText(void);
void updateMovieScrollingText(void);
void captureScreenForTransition(s32 semiTrans, s32 abr);
void renderScreenTransitionPoly(void);
void setupScreenTransitionPoly(s32 semiTrans, s32 abr);
void renderScreenTransition(void);
void renderTransitionFadeGrid(void);
void initTransitionFadeGrid(void);
void freeTransitionFadeGrid(void);
void saveBackBufferRamCopy(void);
void restoreBackBufferRamCopy(void);
void reallocBackBufferRamCopy(s32 flags);
void flushFrame(void);
void freeFieldData(void);
void initFieldData(void);
void loadFieldGraphics(void);
void freeFieldGraphics(void);
void loadTimToVram(u32* addr, s16 px, s16 py, s16 cx, s16 cy, s16 cw, s16 ch);
void resetPortraitLoadingStatus(void);
void releaseAllDialogWindows(void);
/* Retail passes playMusic a second argument, which it ignores. */
#define playMusic(music, unused) playMusic(music)
void setupRGBFaderSlot0_fadeIn(s32 steps);
void setupRGBCalcSlot0_fadeToBlack(s32 steps);
/* Defined with one parameter in f_80071A64.c; callers pass the frame index
 * too (Noah: setupScreenTransitionPoly(sTag*, int)). */
void loadMechaOverlay_start(void);
void loadMechaOverlay_finalize(void);
s32 length2d(s32 x, s32 y);
/* ---- main functions ---- */
void checkSoftReboot(void);
void deinitDebugFont(void);
void updatePartyFileSet(void);
void processLoadedPartyFiles(void);
void mergeFreeHeapBlocks(void);
void seekToFile(s32);
void setCdVolume(s32, s32);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
/* ---- SDK (not in psyq.h yet) ---- */
void SetDrawMode(DR_MODE* p, s32 dfe, s32 dtd, s32 tpage, RECT* tw);

/*
 * The wide picture (include/pc_wide.h). The transition strips and the fade
 * grid draw a capture of the screen at (0x2C0, 0x100); when that capture was
 * made from a wide frame it has margins, and the wide picture gets the same
 * effect over them: extra pieces only it draws, laid out as the game lays
 * its own (strips: from vertices further out through the same GTE matrix;
 * grid: more rows/columns, faded by the same rule), textured from the
 * capture's margins (pcWidePrimTex2D). The game's own pieces are marked as
 * handled. A capture made from a picture with no wide version has black
 * margins, and the pieces show them. The extra pieces are projected with the
 * GTE macros (pcWideRotAverage4), not libgte: each libgte call is a safe point
 * that moves the stepped clock on, so the game's timing would change.
 */
#define WIDE_CAPTURE_X 0x2C0
#define WIDE_CAPTURE_Y 0x100
#define WIDE_GRID_COLS 18 /* per side: 268 / 16, rounded up */

/* The fade grid's extra quads' corner colours (as m71C0_colors). */
static s16 wideGridColors[4][16][20 + 2 * WIDE_GRID_COLS];

/* Whether the capture has margins for the wide picture's current width. */
static s32 wideCaptureReady(void) {
    s32 top, bottom;
    return (pcWideView != 0 || pcVerticalCull != 0) &&
           pcWideCaptureLines(WIDE_CAPTURE_X, WIDE_CAPTURE_Y, &top, &bottom) &&
           pcWideCaptureMargin(WIDE_CAPTURE_X, WIDE_CAPTURE_Y) >= pcWideView;
}

/* Set while the strips or the grid show a capture without a wide copy that
 * none can be made for: those frames stay 4:3. */
static s32 wideEffectBlocked;
static s32 wideEffectDrawn; /* the strips or the grid in this frame */
static s32 transition60Strips; /* Enhanced, 60 Hz: renderScreenTransitionPoly ran for the frame being built */

/* An effect frame: returns whether to draw its wide pieces. */
static s32 wideEffectFrame(void) {
    RECT capture;

    wideEffectDrawn = 1;
    if ((pcWideView != 0 || pcVerticalCull != 0) && !wideCaptureReady()) {
        /* Captured while the picture was not wide (a movie's last frame), or
         * the strips' LoadImages replaced the wide copy: one from VRAM, with
         * black margins. */
        setRECT(&capture, WIDE_CAPTURE_X, WIDE_CAPTURE_Y, 0x140, 0xE0);
        pcWideCaptureVram(&capture);
    }
    pcWideCaptureZoom(WIDE_CAPTURE_X, WIDE_CAPTURE_Y);
    if (!wideEffectBlocked && (pcWideView != 0 || pcVerticalCull != 0) && !wideCaptureReady()) {
        wideEffectBlocked = 1;
        pcWideBlock(PC_WIDE_BLOCK_EFFECT, 1);
    }
    return !wideEffectBlocked && (pcWideView != 0 || pcVerticalCull != 0);
}

/* The strips and the grid are gone. */
static void wideEffectDone(void) {
    if (wideEffectBlocked) {
        wideEffectBlocked = 0;
        pcWideBlock(PC_WIDE_BLOCK_EFFECT, 0);
    }
}

/* Each frame's start (clearFieldOrderingTable): a frame without the strips or
 * the grid ends the effect (every loop that shows them, here and in bootField,
 * draws them each frame). */
void pcFieldWideFrameStart(void) {
    if (!wideEffectDrawn) {
        wideEffectDone();
    }
    wideEffectDrawn = 0;
}

/* The strips' margin pieces, linked before them (drawn after them, under the
 * same draw mode). transform: the strips are placed by the GTE (scale or
 * rotation set) rather than at their rest positions. */
static void wideTransitionStrips(s32 transform) {
    GteState gte;
    POLY_FT4* src;
    POLY_FT4* q;
    SVECTOR v[4];
    s32 m = pcWideView;
    s32 side;
    s32 k;
    s32 x0;
    s32 x1;
    s32 flag;
    s32 i;

    if (!wideEffectFrame()) {
        return;
    }
    for (i = 0; i < 5; i++) {
        pcWidePrim(&fieldTransitionStrips.mC8_polys[i][g_frameOddOrEven], 0, PC_WIDE_HANDLED);
    }
    src = &fieldTransitionStrips.mC8_polys[0][g_frameOddOrEven];
    gteSave(&gte);
    for (side = 0; side < 2; side++) {
        for (k = 0;; k++) {
            /* 64 pixels at most, the width of the game's own strips. */
            x0 = side == 0 ? -m + k * 64 : 0x140 + k * 64;
            x1 = x0 + 64;
            if (side == 0 ? x0 >= 0 : x0 >= 0x140 + m) {
                break;
            }
            if (side == 0 ? x1 > 0 : x1 > 0x140 + m) {
                x1 = side == 0 ? 0 : 0x140 + m;
            }
            q = pcWideAlloc(sizeof(POLY_FT4));
            if (q == NULL) {
                break;
            }
            *q = *src;
            if (transform) {
                /* Screen x = 160 + 2 * vertex x at rest (setupScreenTransitionPoly). */
                setVector(&v[0], (x0 - 0xA0) / 2, -0x38, 0);
                setVector(&v[1], (x1 - 0xA0) / 2, -0x38, 0);
                setVector(&v[2], (x0 - 0xA0) / 2, 0x38, 0);
                setVector(&v[3], (x1 - 0xA0) / 2, 0x38, 0);
                pcWideRotAverage4(&v[0], &v[1], &v[2], &v[3], &q->x0, &q->x1, &q->x2, &q->x3, &flag);
            } else {
                setXYWH(q, x0, 0, x1 - x0, 0xDF);
            }
            setUVWH(q, 0, 0, x1 - x0, 0xDF);
            addPrim(pCurrentFieldRenderingContext->m80D4_uiOT, q);
            pcWidePrimTex(q, 0, PC_WIDE_ONLY | PC_WIDE_HANDLED, x0);
            pcInterpolationTransitionPrim(&fieldTransitionStrips, 16 + side * 32 + k, q);
        }
    }
    /* Keep the original strips untouched; separate pieces carry the extra
     * captured rows through the same scale/rotation and colour. */
    {
        s32 top, bottom, y0, y1, x;
        pcWideCaptureLines(WIDE_CAPTURE_X, WIDE_CAPTURE_Y, &top, &bottom);
        for (side = 0; side < 2; side++) {
            y0 = side == 0 ? -top : (transform ? 224 : 223);
            y1 = side == 0 ? 0 : 224 + bottom;
            if (y1 <= y0) continue;
            for (x = -m; x < 320 + m; x += 64) {
                x1 = x + 64;
                if (x1 > 320 + m) x1 = 320 + m;
                q = pcWideAlloc(sizeof(POLY_FT4));
                if (q == NULL) break;
                *q = *src;
                if (transform) {
                    setVector(&v[0], (x - 160) / 2, (y0 - 112) / 2, 0);
                    setVector(&v[1], (x1 - 160) / 2, (y0 - 112) / 2, 0);
                    setVector(&v[2], (x - 160) / 2, (y1 - 112) / 2, 0);
                    setVector(&v[3], (x1 - 160) / 2, (y1 - 112) / 2, 0);
                    pcWideRotAverage4(&v[0], &v[1], &v[2], &v[3], &q->x0, &q->x1, &q->x2, &q->x3, &flag);
                } else {
                    setXYWH(q, x, y0, x1 - x, y1 - y0);
                }
                setUVWH(q, 0, 0, x1 - x, y1 - y0);
                addPrim(pCurrentFieldRenderingContext->m80D4_uiOT, q);
                pcWidePrimTex2D(q, 0, PC_WIDE_ONLY | PC_WIDE_HANDLED, x, y0);
                pcInterpolationTransitionPrim(&fieldTransitionStrips, 128 + side * 64 + (x+m)/64, q);
            }
        }
    }
    gteLoad(&gte);
}

/* The strips' capture kept across setVramStripSemiTransBit's LoadImages,
 * which replace its wide copy: read before them, loaded back after with the
 * mask bit set on every texel as they set it on the 4:3 part. */
static u16 wideStripsBuf[(0x140 + 2 * 0x200) * 0x100];
static s32 wideStripsMargin, wideStripsTop, wideStripsBottom, wideStripsSaved;

static void wideStripsSave(void) {
    RECT r;

    setRECT(&r, WIDE_CAPTURE_X, WIDE_CAPTURE_Y, 0x140, 0xE0);
    wideStripsSaved = pcWideCaptureReadFrame(&r, wideStripsBuf, sizeof(wideStripsBuf) / sizeof(u16),
                                               &wideStripsMargin, &wideStripsTop, &wideStripsBottom);
}

static void wideStripsRestore(void) {
    RECT r;
    s32 n = (0x140 + 2 * wideStripsMargin) * (0xE0 + wideStripsTop + wideStripsBottom);
    s32 i;

    if (!wideStripsSaved || wideStripsMargin != pcWideView) {
        return;
    }
    for (i = 0; i < n; i++) {
        wideStripsBuf[i] |= 0x8000;
    }
    setRECT(&r, WIDE_CAPTURE_X, WIDE_CAPTURE_Y, 0x140, 0xE0);
    pcWideCaptureLoadFrame(&r, wideStripsBuf, wideStripsMargin, wideStripsTop, wideStripsBottom, pcWideView != 0 && pcWideShifts());
}

/* The fade grid's extra rows/columns, linked before its quads. */
static void wideFadeGrid(void) {
    GteState gte;
    POLY_GT4* q;
    s32 n = (pcWideView + 15) / 16;
    s32 i;
    s32 c;
    s32 j;

    if (!wideEffectFrame()) {
        return;
    }
    for (i = 0; i < 14 * 20; i++) {
        pcWidePrim(&transitionFadeGrid->m0_polys[g_frameOddOrEven][i], 0, PC_WIDE_HANDLED);
    }
    if (n > WIDE_GRID_COLS) {
        n = WIDE_GRID_COLS;
    }
    gteSave(&gte);
    for (i = pcVerticalCull ? -1 : 0; i < (pcVerticalCull ? 15 : 14); i++) {
        for (j = -n; j < 20 + n; j++) {
            if (i >= 0 && i < 14 && j >= 0 && j < 20) continue;
            c = j + WIDE_GRID_COLS;
            q = pcWideAlloc(sizeof(POLY_GT4));
            if (q == NULL) break;
            *q = transitionFadeGrid->m0_polys[g_frameOddOrEven][0];
            setXYWH(q, j * 16, i * 16, 16, 16);
            setUVWH(q, 0, 0, 16, 16);
            setRGB0(q, wideGridColors[0][i+1][c], wideGridColors[0][i+1][c], wideGridColors[0][i+1][c]);
            setRGB1(q, wideGridColors[1][i+1][c], wideGridColors[1][i+1][c], wideGridColors[1][i+1][c]);
            setRGB2(q, wideGridColors[2][i+1][c], wideGridColors[2][i+1][c], wideGridColors[2][i+1][c]);
            setRGB3(q, wideGridColors[3][i+1][c], wideGridColors[3][i+1][c], wideGridColors[3][i+1][c]);
            fadeTransitionGridCorner(q, 0, transitionFadeGridRadius, &wideGridColors[0][i+1][c]);
            fadeTransitionGridCorner(q, 1, transitionFadeGridRadius, &wideGridColors[1][i+1][c]);
            fadeTransitionGridCorner(q, 2, transitionFadeGridRadius, &wideGridColors[2][i+1][c]);
            fadeTransitionGridCorner(q, 3, transitionFadeGridRadius, &wideGridColors[3][i+1][c]);
            addPrim(pCurrentFieldRenderingContext->m80D4_uiOT, q);
            pcWidePrimTex2D(q, 0, PC_WIDE_ONLY | PC_WIDE_HANDLED, j * 16, i * 16);
            pcInterpolationTransitionPrim(transitionFadeGrid, 512 + (i+1)*(20+2*WIDE_GRID_COLS)+c, q);
        }
    }
    gteLoad(&gte);
}

/* Sets a MATRIX translation. */
void setMatrixTranslation(MATRIX* m, s32 x, s32 y, s32 z) {
    m->t[0] = x;
    m->t[1] = y;
    m->t[2] = z;
}

/* Frees two field buffers (D_800AFE80, D_800B069C). */
void func_800A55C8(void) {
    freeHeap(D_800AFE80);
    freeHeap(D_800B069C);
}

/* Sets the grey level of the five transition strips (next frame's prims). */
s32 pcFieldOffTick(void);
s32 pcFieldSubFrames(void);
s32 pcFieldSubIndex(void);
s32 pcFieldLerp(s32 prev, s32 cur, s32 k); /* f_80072140.c, xg-hbc.19.3 */

void setFieldTransitionPolyColor(s32 c) {
    s32 i;

    pcFieldPicAdd(PC_FIELD_PIC_TRANSITION, c); /* Enhanced oracle */
    for (i = 0; i < 5; i++) {
        setRGB0(&fieldTransitionStrips.mC8_polys[i][(g_frameOddOrEven + 1) & 1], c, c, c);
    }
}

/* Fader slot 0: additive fade from white over `steps` frames (no mode checks,
 * unlike setupRGBFaderSlot0_fadeIn). */
void setupRGBFaderSlot0_fadeFromWhite(s32 steps) {
    s32 step = R3000_DIV(-0x10000, steps);

    fieldScreenEffects.m4C_screenEffects[0].m38_current.m8_B = 0xFF00;
    fieldScreenEffects.m4C_screenEffects[0].m38_current.m4_G = 0xFF00;
    fieldScreenEffects.m4C_screenEffects[0].m38_current.m0_R = 0xFF00;
    fieldScreenEffects.m4C_screenEffects[0].m54_duration = steps + 1;
    fieldScreenEffects.m4C_screenEffects[0].m52_isActive = 1;
    fieldScreenEffects.m4C_screenEffects[0].m50_mode = 1;
    fieldScreenEffects.m4C_screenEffects[0].m44_step.m8_B = step;
    fieldScreenEffects.m4C_screenEffects[0].m44_step.m4_G = step;
    fieldScreenEffects.m4C_screenEffects[0].m44_step.m0_R = step;
}

/* Fader slot 0: additive fade to white over `steps` frames. */
void setupRGBFaderSlot0_fadeToWhite(s32 steps) {
    s32 step = R3000_DIV(0x10000, steps);

    fieldScreenEffects.m4C_screenEffects[0].m38_current.m8_B = 0;
    fieldScreenEffects.m4C_screenEffects[0].m38_current.m4_G = 0;
    fieldScreenEffects.m4C_screenEffects[0].m38_current.m0_R = 0;
    fieldScreenEffects.m4C_screenEffects[0].m54_duration = steps + 1;
    fieldScreenEffects.m4C_screenEffects[0].m52_isActive = 1;
    fieldScreenEffects.m4C_screenEffects[0].m50_mode = 1;
    fieldScreenEffects.m4C_screenEffects[0].m44_step.m8_B = step;
    fieldScreenEffects.m4C_screenEffects[0].m44_step.m4_G = step;
    fieldScreenEffects.m4C_screenEffects[0].m44_step.m0_R = step;
}

/* Sets the STP bit of every pixel of a 64 x h VRAM block. */
void setVramStripSemiTransBit(s32 x, s32 y, s32 h) {
    RECT rect;
    u32* buf;
    u32* p;
    s32 i;

    rect.x = x;
    rect.y = y;
    rect.w = 0x40;
    rect.h = h;
    buf = allocateHeap(PC_HEAP_BYTES(h << 7), 1);
    StoreImage(&rect, buf);
    DrawSync(0);
    p = buf;
    for (i = 0; i < h << 5; i += 8) {
        p[0] |= 0x80008000;
        p[1] |= 0x80008000;
        p[2] |= 0x80008000;
        p[3] |= 0x80008000;
        p[4] |= 0x80008000;
        p[5] |= 0x80008000;
        p[6] |= 0x80008000;
        p[7] |= 0x80008000;
        p += 8;
    }
    LoadImage(&rect, buf);
    DrawSync(0);
    freeHeap(buf);
}

/* Sets up the transition strips and copies the current frame into the five
 * 64-wide VRAM strips at (0x2C0, 0x100) with the STP bit set. */
void captureScreenForTransition(s32 semiTrans, s32 abr) {
    s32 i;
    s32 x;
    s32 wide;
    RECT screen;

    setupScreenTransitionPoly(semiTrans, abr);
    for (i = 0; i < 2; i++) {
        clearFieldOrderingTable();
        renderScreenTransitionPoly();
        renderScreenTransition();
    }
    wide = wideCaptureReady();
    for (i = 0, x = 0x2C0; i < 5; i++, x += 0x40) {
        setVramStripSemiTransBit(x, 0x100, 0xE0);
    }
    /* The strips' LoadImages replaced the capture's wide copy. Frame buffer
     * (0, 0) shows the strips at rest, so the capture with its margins: copy
     * it again, with the mask bit on the margins too. */
    if (wide) {
        setRECT(&screen, 0, 0, 0x140, 0xE0);
        pcWideCaptureMask(&screen, 0x2C0, 0x100);
    }
    for (i = 0; i < 2; i++) {
        clearFieldOrderingTable();
        renderScreenTransitionPoly();
        renderScreenTransition();
    }
}

void renderFullScreenTransitionEffect(void) {
    s32 i;
    s32 x;
    s32 level;
    s32 prevLevel; /* Enhanced: the level before the tick's step (xg-hbc.19.3) */

    if (fullScreenTransitionEffectRenderMode != 0) {
        deinitDebugFont();
        resetPortraitLoadingStatus();
        saveBackBufferRamCopy();
        if (fullScreenTransitionEffectRenderMode == 1 || fullScreenTransitionEffectRenderMode == 4) {
            copyScreenToTransitionStrips();
            DrawSync(0);
            clearFieldOrderingTable();
            flushFrame();
        }
        flushFrame();
    again:
        switch (fullScreenTransitionEffectRenderMode) {
        case 4:
            captureScreenForTransition(1, 1);
            initTransitionFadeGrid();
            fieldScreenFadedOut = 1;
            setupRGBFaderSlot0_fadeIn(fullScreenTransitionEffectRenderParam);
            transitionFadeGridRadius = 0;
            for (i = 0; i < fullScreenTransitionEffectRenderParam; i++) {
                fieldPerFrameReset();
                renderTransitionFadeGrid();
                updateAndRenderField();
                updateMusicState();
                if (pcFieldNativeFrameGoesOn()) { i--; continue; } /* Enhanced: native frames count, at their end (xg-hbc.14, .19.3) */
                transitionFadeGridRadius += 6;
            }
            DrawSync(0);
            freeTransitionFadeGrid();
            break;
        case 1:
        case 2:
            captureScreenForTransition(1, 1);
            fieldScreenFadedOut = 1;
            setupRGBFaderSlot0_fadeIn(fullScreenTransitionEffectRenderParam);
            level = 0x800000; prevLevel = level;
            for (i = 0; i < fullScreenTransitionEffectRenderParam; i++) {
                fieldPerFrameReset();
                renderScreenTransitionPoly();
                updateAndRenderField();
                updateMusicState();
                /* Enhanced (xg-hbc.19.3): the frames between show the colour through the tick's step */
                setFieldTransitionPolyColor(pcFieldSubIndex() > 0 ? pcFieldLerp(prevLevel, level, pcFieldSubIndex()) >> 16 : level >> 16);
                if (pcFieldNativeFrameGoesOn()) { i--; continue; } /* Enhanced: the frames of a native frame repeat it, the progression counts native frames at their end (xg-hbc.14, .19.3) */
                prevLevel = level;
                level -= 0x800000 / fullScreenTransitionEffectRenderParam;
                if (level < 0) {
                    level = 0;
                }
            }
            break;
        case 3:
            setupScreenTransitionPoly(1, 1);
            wideStripsSave();
            for (i = 0, x = 0x2C0; i < 5; i++, x += 0x40) {
                setVramStripSemiTransBit(x, 0x100, 0xE0);
            }
            wideStripsRestore();
            setupRGBCalcSlot0_fadeToBlack(fullScreenTransitionEffectRenderParam);
            level = 0; prevLevel = level;
            setFieldTransitionPolyColor(0);
            for (i = 0; i < fullScreenTransitionEffectRenderParam; i++) {
                fieldPerFrameReset();
                renderScreenTransitionPoly();
                updateAndRenderField();
                updateMusicState();
                /* Enhanced (xg-hbc.19.3): the frames between show the colour through the tick's step */
                setFieldTransitionPolyColor(pcFieldSubIndex() > 0 ? pcFieldLerp(prevLevel, level, pcFieldSubIndex()) >> 16 : level >> 16);
                if (pcFieldNativeFrameGoesOn()) { i--; continue; } /* Enhanced: the frames of a native frame repeat it, the progression counts native frames at their end (xg-hbc.14, .19.3) */
                prevLevel = level;
                level += 0x800000 / fullScreenTransitionEffectRenderParam;
            }
            for (;;) {
                if (fullScreenTransitionEffectRenderMode != 3) {
                    goto again;
                }
                fieldPerFrameReset();
                renderScreenTransitionPoly();
                updateAndRenderField();
                updateMusicState();
            }
        }
        fullScreenTransitionEffectRenderMode = 0;
        restoreBackBufferRamCopy();
        wideEffectDone();
        initFontSystem();
    }
}

void transitionFields(void) {
    RECT rect;
    s32 i;
    s32 mode;
    s32 length;
    s32 level;
    s32 prevLevel; /* Enhanced: the level before the tick's step (xg-hbc.19.3) */
    s32 stripGrew = 0; /* Enhanced: the tick grew the strips */
    u8* bundle;

    deinitDebugFont();
    deleteAllParticleEffects();
    stopFieldSoundEffects();
    releaseAllDialogWindows();
    if (fieldTransitionMode != 6) {
        saveBackBufferRamCopy();
        if (fieldTransitionMode != 4) {
            copyScreenToTransitionStrips();
        }
    }
    DrawSync(0);
    clearFieldOrderingTable();
    flushFrame();
    freeFieldData();
    bundle = allocateHeap(PC_HEAP_BYTES(rawFieldBundleSize), 0);
    memcpy(bundle, rawFieldBundle, rawFieldBundleSize);
    unflagAllocation(rawFieldBundle);
    freeHeap(rawFieldBundle);
    if (fieldTransitionMode != 6) {
        reallocBackBufferRamCopy(1);
    }
    rawFieldBundle = allocateHeap(PC_HEAP_BYTES(rawFieldBundleSize), 1);
    memcpy(rawFieldBundle, bundle, rawFieldBundleSize);
    flagAllocation(rawFieldBundle);
    freeHeap(bundle);
    switch (fieldTransitionMode) {
    case 6:
        setupRGBCalcSlot0_fadeToBlack(fieldTransitionFadeInLength);
        for (i = 0; i < fieldTransitionFadeInLength; i++) {
            clearFieldOrderingTable();
            updateScreenEffectSlots(pCurrentFieldRenderingContext->m80D4_uiOT, g_frameOddOrEven);
            renderScreenTransition();
        }
        clearFieldOrderingTable();
        renderScreenTransition();
        updatePartyFileSet();
        processLoadedPartyFiles();
        mode = fieldTransitionMode;
        length = fieldTransitionFadeInLength;
        initFieldData();
        loadFieldGraphics();
        freeFieldGraphics();
        fieldTransitionMode = mode;
        fieldTransitionFadeInLength = length;
        if (fieldMusicLoadPending == -1) {
            playMusic(currentlyPlayingMusic, 0);
        }
        setupRGBFaderSlot0_fadeIn(fieldTransitionFadeInLength);
        break;
    case 0:
        setupScreenTransitionPoly(0, 0);
        setupRGBCalcSlot0_fadeToBlack(fieldTransitionFadeInLength);
        for (i = 0; i < fieldTransitionFadeInLength; i++) {
            clearFieldOrderingTable();
            updateScreenEffectSlots(pCurrentFieldRenderingContext->m80D4_uiOT, g_frameOddOrEven);
            renderScreenTransitionPoly();
            renderScreenTransition();
        }
        clearFieldOrderingTable();
        renderScreenTransition();
        updatePartyFileSet();
        processLoadedPartyFiles();
        mode = fieldTransitionMode;
        length = fieldTransitionFadeInLength;
        initFieldData();
        loadFieldGraphics();
        freeFieldGraphics();
        fieldTransitionMode = mode;
        fieldTransitionFadeInLength = length;
        if (fieldMusicLoadPending == -1) {
            playMusic(currentlyPlayingMusic, 0);
        }
        setupRGBFaderSlot0_fadeIn(fieldTransitionFadeInLength);
        break;
    case 1:
        setupScreenTransitionPoly(0, 0);
        setupRGBFaderSlot0_fadeToWhite(fieldTransitionFadeInLength);
        for (i = 0; i < fieldTransitionFadeInLength; i++) {
            clearFieldOrderingTable();
            updateScreenEffectSlots(pCurrentFieldRenderingContext->m80D4_uiOT, g_frameOddOrEven);
            renderScreenTransitionPoly();
            renderScreenTransition();
        }
        flushFrame();
        updatePartyFileSet();
        processLoadedPartyFiles();
        mode = fieldTransitionMode;
        length = fieldTransitionFadeInLength;
        initFieldData();
        loadFieldGraphics();
        freeFieldGraphics();
        fieldTransitionMode = mode;
        fieldTransitionFadeInLength = length;
        if (fieldMusicLoadPending == -1) {
            playMusic(currentlyPlayingMusic, 0);
        }
        setupRGBFaderSlot0_fadeFromWhite(fieldTransitionFadeInLength);
        break;
    case 2:
    case 4:
        captureScreenForTransition(1, 1);
        updatePartyFileSet();
        processLoadedPartyFiles();
        mode = fieldTransitionMode;
        length = fieldTransitionFadeInLength;
        initFieldData();
        loadFieldGraphics();
        if (fieldGraphicsUploaded == 1) {
            while (isCDBusy()) {
                clearFieldOrderingTable();
                renderScreenTransitionPoly();
                renderScreenTransition();
                if (transitionStripScale < 0x22C0) {
                    transitionStripScale += 0x20;
                }
            }
            freeHeap(fieldGraphicsAllocation);
            fieldGraphicsUploaded = 0;
            brightenFieldCluts();
        }
        fieldTransitionInProgress = 1;
        fieldTransitionMode = mode;
        fieldTransitionFadeInLength = length;
        if (fieldMusicLoadPending == -1) {
            playMusic(currentlyPlayingMusic, 0);
        }
        level = 0x800000; prevLevel = level;
        setupRGBFaderSlot0_fadeIn(fieldTransitionFadeInLength);
        for (i = 0; i < fieldTransitionFadeInLength; i++) {
            fieldPerFrameReset();
            if (pcFieldSubFrames() > 1 && (pcFieldOffTick() ? stripGrew : transitionStripScale < 0x22C0)) {
                /* Enhanced (xg-hbc.19.3): the strips grow at the end of the tick (below); the frames
                 * of the native frame show them through that step: the tick 1/N of it, the frame
                 * between at N = 2 where it took them */
                s32 kept = transitionStripScale;
                if (pcFieldOffTick()) {
                    transitionStripScale += pcFieldLerp(0, 0x20, pcFieldSubIndex() + 1) - 0x20;
                } else {
                    transitionStripScale += pcFieldLerp(0, 0x20, 1);
                }
                renderScreenTransitionPoly();
                transitionStripScale = kept;
            } else {
                renderScreenTransitionPoly();
            }
            if (!pcFieldOffTick()) stripGrew = 0;
            updateAndRenderField();
            updateMusicState();
            /* Enhanced (xg-hbc.19.3): the frames between show the colour through the tick's step */
            setFieldTransitionPolyColor(pcFieldSubIndex() > 0 ? pcFieldLerp(prevLevel, level, pcFieldSubIndex()) >> 16 : level >> 16);
            if (pcFieldNativeFrameGoesOn()) { i--; continue; } /* Enhanced: the frames of a native frame repeat it, the progression counts native frames at their end (xg-hbc.14, .19.3) */
            prevLevel = level;
            level -= 0x800000 / fieldTransitionFadeInLength;
            if (level < 0) {
                level = 0;
            }
            if (transitionStripScale < 0x22C0) {
                transitionStripScale += 0x20;
                stripGrew = 1;
            }
        }
        break;
    case 3:
        setupScreenTransitionPoly(0, 0);
        loadFieldGraphics();
        clearFieldOrderingTable();
        renderScreenTransitionPoly();
        renderScreenTransition();
        updatePartyFileSet();
        processLoadedPartyFiles();
        mode = fieldTransitionMode;
        length = fieldTransitionFadeInLength;
        fieldTransitionInProgress = 1;
        initFieldData();
        freeFieldGraphics();
        fieldTransitionMode = mode;
        fieldTransitionFadeInLength = length;
        if (fieldMusicLoadPending == -1) {
            playMusic(currentlyPlayingMusic, 0);
        }
        for (i = 0; i < 4; i++) {
            fieldPerFrameReset();
            renderScreenTransitionPoly();
            updateAndRenderField();
            updateMusicState();
            if (pcFieldNativeFrameGoesOn()) { i--; continue; } /* Enhanced: native frames count, at their end (xg-hbc.14, .19.3) */
        }
        break;
    case 5:
        setupScreenTransitionPoly(0, 0);
        loadFieldGraphics();
        clearFieldOrderingTable();
        renderScreenTransitionPoly();
        renderScreenTransition();
        updatePartyFileSet();
        processLoadedPartyFiles();
        mode = fieldTransitionMode;
        length = fieldTransitionFadeInLength;
        fieldTransitionInProgress = 1;
        initFieldData();
        freeFieldGraphics();
        setRECT(&rect, 0x2C0, 0x100, 0x140, 0xFF);
        fieldTransitionMode = mode;
        fieldTransitionFadeInLength = length;
        MoveImage(&rect, 0x140, 0xFF);
        if (fieldMusicLoadPending == -1) {
            playMusic(currentlyPlayingMusic, 0);
        }
        for (i = 0; i < 4; i++) {
            fieldPerFrameReset();
            renderScreenTransitionPoly();
            updateAndRenderField();
            updateMusicState();
            if (pcFieldNativeFrameGoesOn()) { i--; continue; } /* Enhanced: native frames count, at their end (xg-hbc.14, .19.3) */
        }
        break;
    }
    if (fieldTransitionMode != 6) {
        restoreBackBufferRamCopy();
    }
    fieldTransitionMode = 2;
    fieldTransitionFadeInLength = 0x20;
    fieldTransitionInProgress = 0;
    wideEffectDone();
    initFontSystem();
    mergeFreeHeapBlocks();
}

/* Rotates/scales the transition strips (transitionStripRotation, transitionStripScale) and links
 * them into the UI ordering table. */
void renderScreenTransitionPoly(void) {
    MATRIX m;
    VECTOR scale;
    s32 p;
    s32 flag;
    s32 i;

    createRotationMatrix(&transitionStripRotation, &m);
    m.t[2] = 0;
    m.t[1] = 0;
    m.t[0] = 0;
    scale.vx = transitionStripScale;
    scale.vy = transitionStripScale;
    scale.vz = transitionStripScale;
    ScaleMatrix(&m, &scale);
    pcFieldPicAdd(PC_FIELD_PIC_TRANSITION, transitionStripScale); /* Enhanced oracle */
    transition60Strips = 1; /* Enhanced, 60 Hz (renderScreenTransition) */
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    wideTransitionStrips(transitionStripScale != 0x1000);
    for (i = 0; i < 5; i++) {
        if (transitionStripScale != 0x1000) {
            RotAverage4(&fieldTransitionStrips.m258_vertices[i][0], &fieldTransitionStrips.m258_vertices[i][1],
                        &fieldTransitionStrips.m258_vertices[i][2], &fieldTransitionStrips.m258_vertices[i][3],
                        &fieldTransitionStrips.mC8_polys[i][g_frameOddOrEven].x0, &fieldTransitionStrips.mC8_polys[i][g_frameOddOrEven].x1,
                        &fieldTransitionStrips.mC8_polys[i][g_frameOddOrEven].x2, &fieldTransitionStrips.mC8_polys[i][g_frameOddOrEven].x3, &p,
                        &flag);
        }
        pcInterpolationTransitionPrim(&fieldTransitionStrips, i, &fieldTransitionStrips.mC8_polys[i][g_frameOddOrEven]);
        addPrim(pCurrentFieldRenderingContext->m80D4_uiOT, &fieldTransitionStrips.mC8_polys[i][g_frameOddOrEven]);
        addPrim(pCurrentFieldRenderingContext->m80D4_uiOT, &fieldTransitionStrips.m0_drawModes[i][g_frameOddOrEven]);
    }
}

/* Initialises the transition strips: five 64x224 quads textured from the
 * VRAM copy at (0x2C0, 0x100), in both frame buffers. */
void setupScreenTransitionPoly(s32 semiTrans, s32 abr) {
    s32 i;
    POLY_FT4* p;
    POLY_FT4* p1;
    SVECTOR* v;

    pcInterpolationLifetime(&fieldTransitionStrips);
    transitionStripScale = 0x1000;
    transitionStripRotation.vx = 0;
    transitionStripRotation.vy = 0;
    transitionStripRotation.vz = 0;
    for (i = 0; i < 5; i++) {
        p = &fieldTransitionStrips.mC8_polys[i][0];
        p1 = &fieldTransitionStrips.mC8_polys[i][1];
        v = fieldTransitionStrips.m258_vertices[i];
        SetPolyFT4(p);
        setVector(&v[0], i * 32 - 0x50, -0x38, 0);
        setVector(&v[1], i * 32 - 0x30, -0x38, 0);
        setVector(&v[2], i * 32 - 0x50, 0x38, 0);
        setVector(&v[3], i * 32 - 0x30, 0x38, 0);
        setXYWH(p, i << 6, 0, 0x40, 0xDF);
        setRECT(&fieldTransitionStrips.m78_textureWindows[i][0], 0, 0, 0xFF, 0xFF);
        setRECT(&fieldTransitionStrips.m78_textureWindows[i][1], 0, 0, 0xFF, 0xFF);
        SetDrawMode(&fieldTransitionStrips.m0_drawModes[i][0], 0, 0, GetTPage(2, abr, (i << 6) + 0x2C0, 0x100),
                    &fieldTransitionStrips.m78_textureWindows[i][0]);
        SetDrawMode(&fieldTransitionStrips.m0_drawModes[i][1], 0, 0, GetTPage(2, abr, (i << 6) + 0x2C0, 0x100),
                    &fieldTransitionStrips.m78_textureWindows[i][1]);
        setRGB0(p, 0x80, 0x80, 0x80);
        SetSemiTrans(p, semiTrans);
        setUVWH(p, 0, 0, 0x40, 0xDF);
        p->tpage = GetTPage(2, abr, (i << 6) + 0x2C0, 0x100);
        *p1 = *p;
    }
}

/*
 * Enhanced, 60 Hz (field60, xg-hbc.19.4): the loops around renderScreenTransition
 * (the strips zooming while a field loads, the fades before a field change,
 * the fade back from a battle event) present a frame every two vblanks
 * (VSync(2)); their state stays the Original's. Inside the wait, at its first
 * vblank (libetc.c pcVSyncBetween, PC-only work: no stepped-clock time), the
 * picture halfway between the two frames before is drawn into the buffer on
 * display (shown is the frame before last: double buffered), which the frame
 * after clears and draws whole: the strips at the halfway scale, turn and
 * colour, the fader slots at the halfway colour, the field map screen (Select)
 * at its halfway brightness, through the game's own renderScreenTransitionPoly,
 * renderRGBFaders and renderFieldMapScreen into the half the frame on
 * display no longer needs, with every value they change put back. Only
 * between two frames of one run (two vblanks apart each) that drew the same
 * parts. With Smooth motion's interpolation on (Speedup) that path keeps them.
 */
int pcGameplayField60(void);
int interpolationEnabled(void);
u32 hwVSyncCount(void);
void pcVSyncBetween(void (*draw)(void));
s32 pcFieldFaderSlotsLinked(void); /* f_80071A64.c */
void renderRGBFaders(OTag* ot, s32 buf); /* f_8007D93C.c */
void pcFieldPicHold(s32 on);       /* f_80072140.c */
typedef struct {
    u32 vblank;  /* when it was built */
    s32 strips;  /* renderScreenTransitionPoly linked them */
    s32 scale;
    SVECTOR rot;
    u8 stripColour[3];
    s32 faders;  /* the slots updateScreenEffectSlots linked */
    u8 fader[2][3];
    s32 map;     /* the field map screen (f_800AA9DC.c): its brightness, or -1 */
} Transition60Frame;
static Transition60Frame transition60[3]; /* by frame, mod 3 */
static u32 transition60Frames;
static s32 transition60Map = -1; /* renderFieldMapScreen's brightness for the frame being built */
void renderFieldMapScreen(s32 color); /* f_800AA9DC.c */

void pcTransition60MapScreen(s32 color) {
    transition60Map = color;
}

static void transition60Between(void) {
    Transition60Frame* a;
    Transition60Frame* b;
    Transition60Frame* c;
    s32 cur = g_frameOddOrEven, prev = 1 - cur, i, k;
    sFieldRenderContext* kept = pCurrentFieldRenderingContext;
    sFieldRenderContext* shown = &fieldRenderContext[cur];
    POLY_FT4 keptPolys[5];
    DR_MODE keptModes[2];
    TILE keptTiles[2];
    ScreenEffectSlot keptSlots[2];
    s32 keptScale = transitionStripScale, keptDrawn = wideEffectDrawn;
    SVECTOR keptRot = transitionStripRotation;
    GteState gte;

    if (transition60Frames < 3) {
        return;
    }
    a = &transition60[(transition60Frames - 3) % 3];
    b = &transition60[(transition60Frames - 2) % 3];
    c = &transition60[(transition60Frames - 1) % 3]; /* the frame waiting */
    if (b->vblank - a->vblank != 2 || c->vblank - b->vblank != 2 || a->strips != b->strips || a->faders != b->faders ||
        (a->map < 0) != (b->map < 0) || (!b->strips && !b->faders && b->map < 0)) {
        return;
    }
    gteSave(&gte);
    pcFieldPicHold(1);
    pcWideStash(1);
    for (i = 0; i < 5; i++) {
        keptPolys[i] = fieldTransitionStrips.mC8_polys[i][prev];
    }
    for (i = 0; i < 2; i++) {
        keptSlots[i] = fieldScreenEffects.m4C_screenEffects[i];
        keptModes[i] = fieldScreenEffects.m4C_screenEffects[i].m0_drawMode[prev];
        keptTiles[i] = fieldScreenEffects.m4C_screenEffects[i].m18_tile[prev];
    }
    /* Built into the half the frame on display drew from (drawn already). */
    g_frameOddOrEven = prev;
    pCurrentFieldRenderingContext = &fieldRenderContext[prev];
    ClearOTagR(pCurrentFieldRenderingContext->m80D4_uiOT, 8);
    if (b->faders) {
        for (i = 0; i < 2; i++) {
            ScreenEffectSlot* slot = &fieldScreenEffects.m4C_screenEffects[i];
            slot->m52_isActive = (b->faders >> i) & 1;
            slot->m38_current.m0_R = ((a->fader[i][0] + b->fader[i][0]) / 2) << 8;
            slot->m38_current.m4_G = ((a->fader[i][1] + b->fader[i][1]) / 2) << 8;
            slot->m38_current.m8_B = ((a->fader[i][2] + b->fader[i][2]) / 2) << 8;
        }
        renderRGBFaders(pCurrentFieldRenderingContext->m80D4_uiOT, prev);
    }
    if (b->strips) {
        transitionStripScale = a->scale + (b->scale - a->scale) / 2;
        transitionStripRotation.vx = (s16)(a->rot.vx + (s16)(b->rot.vx - a->rot.vx) / 2);
        transitionStripRotation.vy = (s16)(a->rot.vy + (s16)(b->rot.vy - a->rot.vy) / 2);
        transitionStripRotation.vz = (s16)(a->rot.vz + (s16)(b->rot.vz - a->rot.vz) / 2);
        for (i = 0; i < 5; i++) {
            for (k = 0; k < 3; k++) {
                (&fieldTransitionStrips.mC8_polys[i][prev].r0)[k] = (u8)((a->stripColour[k] + b->stripColour[k]) / 2);
            }
        }
        renderScreenTransitionPoly();
    }
    if (b->map >= 0) {
        /* Its primitives are rebuilt whole by every call: the half on display's are free. */
        renderFieldMapScreen((a->map + b->map) / 2);
    }
    ClearImage(&shown->m0_drawEnv.clip, 0, 0, 0);
    PutDrawEnv(&shown->m0_drawEnv);
    DrawOTag(&pCurrentFieldRenderingContext->m80D4_uiOT[7]);
    pcWideStash(0);
    g_frameOddOrEven = cur;
    pCurrentFieldRenderingContext = kept;
    transitionStripScale = keptScale;
    transitionStripRotation = keptRot;
    wideEffectDrawn = keptDrawn;
    transition60Strips = 0;
    transition60Map = -1;
    for (i = 0; i < 5; i++) {
        fieldTransitionStrips.mC8_polys[i][prev] = keptPolys[i];
    }
    for (i = 0; i < 2; i++) {
        fieldScreenEffects.m4C_screenEffects[i] = keptSlots[i];
        fieldScreenEffects.m4C_screenEffects[i].m0_drawMode[prev] = keptModes[i];
        fieldScreenEffects.m4C_screenEffects[i].m18_tile[prev] = keptTiles[i];
    }
    pcFieldPicHold(0);
    gteLoad(&gte);
}

/* The frame being presented: what it drew. */
static void transition60Record(void) {
    Transition60Frame* f = &transition60[transition60Frames % 3];
    s32 cur = g_frameOddOrEven, i;

    f->vblank = hwVSyncCount();
    f->strips = transition60Strips;
    f->scale = transitionStripScale;
    f->rot = transitionStripRotation;
    f->stripColour[0] = fieldTransitionStrips.mC8_polys[0][cur].r0;
    f->stripColour[1] = fieldTransitionStrips.mC8_polys[0][cur].g0;
    f->stripColour[2] = fieldTransitionStrips.mC8_polys[0][cur].b0;
    f->faders = pcFieldFaderSlotsLinked();
    for (i = 0; i < 2; i++) {
        f->fader[i][0] = fieldScreenEffects.m4C_screenEffects[i].m18_tile[cur].r0;
        f->fader[i][1] = fieldScreenEffects.m4C_screenEffects[i].m18_tile[cur].g0;
        f->fader[i][2] = fieldScreenEffects.m4C_screenEffects[i].m18_tile[cur].b0;
    }
    f->map = transition60Map;
    transition60Frames++;
    transition60Strips = 0;
    transition60Map = -1;
}

/* Presents a frame: waits, clears, puts the environments and draws the UI
 * ordering table. */
void renderScreenTransition(void) {
    pcInterpolationFrameBegin(fieldMapNumber & 0xFFF, pCurrentFieldRenderingContext->m0_drawEnv.clip.x,
                              pCurrentFieldRenderingContext->m0_drawEnv.clip.y,
                              pCurrentFieldRenderingContext->m0_drawEnv.clip.w,
                              pCurrentFieldRenderingContext->m0_drawEnv.clip.h);
    DrawSync(0);
    transition60Record(); /* Enhanced, 60 Hz (above) */
    pcVSyncBetween(pcGameplayField60() && !interpolationEnabled() ? transition60Between : NULL);
    VSync(2);
    pcVSyncBetween(NULL);
    ClearImage(&pCurrentFieldRenderingContext->m0_drawEnv.clip, 0, 0, 0);
    PutDrawEnv(&pCurrentFieldRenderingContext->m0_drawEnv);
    PutDispEnv(&pCurrentFieldRenderingContext->mB8_displayEnv);
    DrawOTag(&pCurrentFieldRenderingContext->m80D4_uiOT[7]);
    pcInterpolationFrameEnd();
}

/* Darkens one corner of a fade-grid quad once the fade radius passes it. */
void fadeTransitionGridCorner(POLY_GT4* p, s32 corner, s32 radius, s16* color) {
    VECTOR d;
    VECTOR sq;
    s32 cx = 0xA0;
    s32 cy = 0x70;

    d.vz = 0;
    switch (corner) {
    case 0:
        d.vx = cx - p->x0;
        d.vy = cy - p->y0;
        Square0(&d, &sq);
        if ((SquareRoot0(sq.vx + sq.vy) >> 1) < radius) {
            *color -= 6;
            if (*color < 0) {
                *color = 0;
            }
            setRGB0(p, *color, *color, *color);
        }
        break;
    case 1:
        d.vx = cx - p->x1;
        d.vy = cy - p->y1;
        Square0(&d, &sq);
        if ((SquareRoot0(sq.vx + sq.vy) >> 1) < radius) {
            *color -= 6;
            if (*color < 0) {
                *color = 0;
            }
            setRGB1(p, *color, *color, *color);
        }
        break;
    case 2:
        d.vx = cx - p->x2;
        d.vy = cy - p->y2;
        Square0(&d, &sq);
        if ((SquareRoot0(sq.vx + sq.vy) >> 1) < radius) {
            *color -= 6;
            if (*color < 0) {
                *color = 0;
            }
            setRGB2(p, *color, *color, *color);
        }
        break;
    case 3:
        d.vx = cx - p->x3;
        d.vy = cy - p->y3;
        Square0(&d, &sq);
        if ((SquareRoot0(sq.vx + sq.vy) >> 1) < radius) {
            *color -= 6;
            if (*color < 0) {
                *color = 0;
            }
            setRGB3(p, *color, *color, *color);
        }
        break;
    }
}

/* Advances and renders the fade grid (fades the quads' corners out from the
 * screen centre as transitionFadeGridRadius grows). */
void renderTransitionFadeGrid(void) {
    s32 i;
    s32 j;
    POLY_GT4* p;

    wideFadeGrid();
    for (i = 0; i < 14; i++) {
        for (j = 0; j < 20; j++) {
            p = &transitionFadeGrid->m0_polys[g_frameOddOrEven][i * 20 + j];
            fadeTransitionGridCorner(p, 0, transitionFadeGridRadius, &transitionFadeGrid->m71C0_colors[0][i * 20 + j]);
            fadeTransitionGridCorner(p, 1, transitionFadeGridRadius, &transitionFadeGrid->m71C0_colors[1][i * 20 + j]);
            fadeTransitionGridCorner(p, 2, transitionFadeGridRadius, &transitionFadeGrid->m71C0_colors[2][i * 20 + j]);
            fadeTransitionGridCorner(p, 3, transitionFadeGridRadius, &transitionFadeGrid->m71C0_colors[3][i * 20 + j]);
            pcInterpolationTransitionPrim(transitionFadeGrid, i*20+j, p);
            addPrim(pCurrentFieldRenderingContext->m80D4_uiOT, &transitionFadeGrid->m0_polys[g_frameOddOrEven][i * 20 + j]);
        }
    }
    addPrim(pCurrentFieldRenderingContext->m80D4_uiOT, &gDialogDrModes[g_frameOddOrEven][4]);
}

/* Allocates and initialises the fade grid. */
void initTransitionFadeGrid(void) {
    s32 i;
    s32 j;
    POLY_GT4* p;
    POLY_GT4* p1;

    transitionFadeGrid = allocateHeap(PC_HEAP_SIZED(sizeof(FadeGrid), 0x7A80), 1);
    pcInterpolationLifetime(transitionFadeGrid);
    for (i = 0; i < 4 * 16 * (20 + 2 * WIDE_GRID_COLS); i++) {
        (&wideGridColors[0][0][0])[i] = 0x80;
    }
    for (i = 0; i < 14; i++) {
        for (j = 0; j < 20; j++) {
            p = &transitionFadeGrid->m0_polys[0][i * 20 + j];
            p1 = &transitionFadeGrid->m0_polys[1][i * 20 + j];
            transitionFadeGrid->m71C0_colors[0][i * 20 + j] = 0x80;
            transitionFadeGrid->m71C0_colors[1][i * 20 + j] = 0x80;
            transitionFadeGrid->m71C0_colors[2][i * 20 + j] = 0x80;
            transitionFadeGrid->m71C0_colors[3][i * 20 + j] = 0x80;
            SetPolyGT4(p);
            setRGB0(p, 0x80, 0x80, 0x80);
            setRGB1(p, 0x80, 0x80, 0x80);
            setRGB2(p, 0x80, 0x80, 0x80);
            setRGB3(p, 0x80, 0x80, 0x80);
            setXYWH(p, j << 4, i << 4, 0x10, 0x10);
            setUVWH(p, (j << 4) & 0x3F, i << 4, 0x10, 0x10);
            p->tpage = GetTPage(2, 1, (j / 4) * 64 + 0x2C0, 0x100);
            SetSemiTrans(p, 1);
            *p1 = *p;
        }
    }
}

/* Frees the fade grid. */
void freeTransitionFadeGrid(void) {
    freeHeap(transitionFadeGrid);
}

/* Movie: initialises the video player overlay (initStrPlayer). */
void initFieldMoviePlayer(void) {
    resetMemoryAllocStats(4, 0);
    if (onlyUpdateDirector == 2) {
        strSingleSliceMode = 1;
    } else {
        strSingleSliceMode = 0;
    }
    initStrPlayer(0x140, 0xE0, 0x80, 0x10, 0x20, 0x800, fieldMovieParams.m16_is24Bit);
    fieldMoviePlayerStatus = 0;
    resetMemoryAllocStats(8, 0);
}

/* Movie: per-frame callback from the player (frame number, buffer). */
void handleFieldMovieFrame(u16 a0, s32 a1, u16 a2) {
    fieldMovieCurrentFrame = a0;
    fieldMovieWaitingFirstFrame = 0;
    if (a2 == 0) {
        fieldMovieDisplayBuffer = 1;
    } else {
        fieldMovieDisplayBuffer = 0;
    }
    if (onlyUpdateDirector == 0 && movieScrollingTextActive == 0) {
        DrawSync(0);
        pCurrentFieldRenderingContext = &fieldRenderContext[fieldMovieDisplayBuffer];
        if (fieldMovieParams.m16_is24Bit == 1) {
            fieldRenderContext[fieldMovieDisplayBuffer & 1].mB8_displayEnv.isrgb24 = 1;
        }
    }
}

/* Movie: starts playback (startStrPlayback) with the fieldMovieParams parameters. */
void startFieldMoviePlayback(void) {
    s32 mode;

    fieldMovieCurrentFrame = 0;
    resetMemoryAllocStats(4, 0);
    if (fieldMoviePlayerStatus == 0) {
        setCurrentDirectory(0x18, 1);
        mode = 1;
        if (fieldMovieParams.m18_soundSequenceId != 0xFF || (fieldMovieFlags & 0x40)) {
            mode = 3;
        }
        startStrPlayback(fieldMovieParams.m0_movieId + 2, fieldMovieParams.mA_sectorOffset, fieldMovieParams.mC_startFrame, fieldMovieParams.mE_lastFrame, 1, mode,
                      fieldMovieParams.m1A_loop, fieldMovieParams.m2_buffer0X, fieldMovieParams.m4_buffer0Y, fieldMovieParams.m6_buffer1X, fieldMovieParams.m8_buffer1Y, 0xE0,
                      handleFieldMovieFrame);
        setCurrentDirectory(4, 0);
    }
    resetMemoryAllocStats(8, 0);
}

/* Movie: runs the player n times. */
void updateFieldMoviePlayer(s32 n) {
    s32 i;

    checkSoftReboot();
    if (fieldMoviePlayerStatus == 0) {
        for (i = 0; i < n; i++) {
            updateStrPlayer();
            updateMovieSoundEvents();
        }
    }
}

/* Waits until the CD is idle. */
void waitCdIdleRenderingField(void) {
    do {
        fieldPerFrameReset();
        updateAndRenderField();
    } while (isCDBusy() != 0 || g_frameOddOrEven != 0);
    CdDataSync(0);
}

/* Movie: gives back party sprite buffers 1 and 2 (restoring the VRAM that
 * restorePartyBuffersAfterMovie saved into them). */
void releasePartyBuffersForMovie(void) {
    RECT rect;

    if (onlyUpdateDirector == 2) {
        unflagAllocation(partyCharacterBuffers[1]);
        unflagAllocation(partyCharacterBuffers[2]);
        freeHeap(partyCharacterBuffers[1]);
        freeHeap(partyCharacterBuffers[2]);
    } else {
        rect.x = 0x200;
        rect.y = 0;
        rect.w = 0x140;
        rect.h = 0x80;
        LoadImage(&rect, partyCharacterBuffers[1]);
        DrawSync(0);
        rect.x = 0x200;
        rect.y = 0x80;
        rect.w = 0x140;
        rect.h = 0x80;
        LoadImage(&rect, partyCharacterBuffers[2]);
        DrawSync(0);
        unflagAllocation(partyCharacterBuffers[1]);
        unflagAllocation(partyCharacterBuffers[2]);
        freeHeap(partyCharacterBuffers[1]);
        freeHeap(partyCharacterBuffers[2]);
    }
}

/* Movie: reallocates party sprite buffers 1 and 2 (reloading the party
 * sprites, or saving VRAM 0x200,0 320x256 into them). */
void restorePartyBuffersAfterMovie(void) {
    RECT rect;
    LoadingBatchCommand cmds[4];
    s32 i;
    s32 n;

    if (onlyUpdateDirector == 2) {
        partyCharacterBuffers[1] = allocateHeap(PC_HEAP_BYTES(0x14000), 0);
        partyCharacterBuffers[2] = allocateHeap(PC_HEAP_BYTES(0x14000), 0);
        n = 0;
        flagAllocation(partyCharacterBuffers[1]);
        flagAllocation(partyCharacterBuffers[2]);
        setCurrentDirectory(4, 0);
        for (i = 1; i < 3; i++) {
            if (asyncPartyCharacterLoadingTable[i] != 0xFF) {
                cmds[n].m0_fileIndex = asyncPartyCharacterLoadingTable[i] + 5;
                cmds[n].m4_loadPtr = partyCharacterBuffersCompressed[i] = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(asyncPartyCharacterLoadingTable[i] + 5)), 1);
                n++;
            }
        }
        cmds[n].m4_loadPtr = 0;
        cmds[n].m0_fileIndex = 0;
        batchStartLoadingFiles(cmds, 0, 0);
        waitReadCompletion(0);
        for (i = 1; i < 3; i++) {
            if (currentParty[i] != 0xFF) {
                decompress(partyCharacterBuffersCompressed[i], partyCharacterBuffers[i]);
                freeHeap(partyCharacterBuffersCompressed[i]);
            }
        }
    } else {
        partyCharacterBuffers[1] = allocateHeap(PC_HEAP_BYTES(0x14000), 0);
        partyCharacterBuffers[2] = allocateHeap(PC_HEAP_BYTES(0x14000), 0);
        flagAllocation(partyCharacterBuffers[1]);
        flagAllocation(partyCharacterBuffers[2]);
        setRECT(&rect, 0x200, 0, 0x140, 0x80);
        StoreImage(&rect, partyCharacterBuffers[1]);
        DrawSync(0);
        setRECT(&rect, 0x200, 0x80, 0x140, 0x80);
        StoreImage(&rect, partyCharacterBuffers[2]);
        DrawSync(0);
    }
}

/* Next 24-bit colour byte from color24BitSource, as a 5-bit value (0 stays 0,
 * otherwise at least 1). */
s32 getNext24BitColorComponent(void) {
    u32 v;
    s32 n = color24BitComponentIndex;

    if ((n & 3) == 0) {
        u32* p = color24BitSource;
        u32 w = *p;
        color24BitSource = p + 1;
        color24BitWord = w;
    }
    color24BitComponentIndex = n + 1;
    v = color24BitWord & 0xFF;
    color24BitWord >>= 8;
    if (v != 0) {
        v >>= 3;
        if (v == 0) {
            v = 1;
        }
    }
    return v;
}

/* Converts the 24-bit image in VRAM (5 strips of 96 x 224 halfwords at
 * x = 0) to 15-bit and uploads it at (0, 256). */
void convert24BitScreenTo15Bit(s32 unused) {
    RECT rect;
    u32* src;
    u32* dst;
    s32 i;
    s32 j;
    u32 c;

    src = allocateHeap(PC_HEAP_BYTES(0xA800), 0);
    dst = allocateHeap(PC_HEAP_BYTES(0x7000), 0);
    for (i = 0; i < 5; i++) {
        rect.x = i * 0x60;
        rect.y = 0;
        rect.w = 0x60;
        rect.h = 0xE0;
        StoreImage(&rect, src);
        DrawSync(0);
        color24BitSource = src;
        color15BitDest = dst;
        color24BitComponentIndex = 0;
        for (j = 0; j < 0x1C00; j++) {
            c = getNext24BitColorComponent();
            c |= getNext24BitColorComponent() << 5;
            c |= getNext24BitColorComponent() << 10;
            c |= getNext24BitColorComponent() << 16;
            c |= getNext24BitColorComponent() << 21;
            c |= getNext24BitColorComponent() << 26;
            *color15BitDest = c;
            color15BitDest++;
        }
        rect.x = i << 6;
        rect.y = 0x100;
        rect.w = 0x40;
        rect.h = 0xE0;
        LoadImage(&rect, dst);
        DrawSync(0);
    }
    freeHeap(src);
    freeHeap(dst);
}

/* Movie: shows frames 0x687-0x18E1 in 640-wide 24-bit mode. */
s32 playMovieScrollingTextSection(void) {
    RECT rect;

    if (movieScrollingTextEnabled == 0 || fieldMovieCurrentFrame < 0x687) {
        IMPLICIT_RETURN(0);
    }
    if (fieldMovieCurrentFrame < 0x18E2) {
        movieScrollingTextActive = 1;
        strNextUploadEnabled = 0;
        setRECT(&rect, 0, 0, 0x500, 0x200);
        ClearImage(&rect, 0, 0, 0);
        DrawSync(0);
        VSync(0);
        SetDefDrawEnv(&fieldRenderContext[0].m0_drawEnv, 0, 0, 0x280, 0xE0);
        SetDefDrawEnv(&fieldRenderContext[1].m0_drawEnv, 0, 0x100, 0x280, 0xE0);
        SetDefDispEnv(&fieldRenderContext[0].mB8_displayEnv, 0, 0x100, 0x280, 0xE0);
        SetDefDispEnv(&fieldRenderContext[1].mB8_displayEnv, 0, 0, 0x280, 0xE0);
        fieldRenderContext[1].mB8_displayEnv.isrgb24 = 0;
        fieldRenderContext[0].mB8_displayEnv.isrgb24 = 0;
        PutDispEnv(&pCurrentFieldRenderingContext->mB8_displayEnv);
        PutDrawEnv(&pCurrentFieldRenderingContext->m0_drawEnv);
        setRECT(&rect, 0x300, 0, 0x200, 0x100);
        ClearImage(&rect, 0, 0, 0);
        uploadScrollingTextTextures();
        VSync(0);
        DrawSync(0);
        while (fieldMovieCurrentFrame < 0x18E2) {
            checkSoftReboot();
            setupFieldRenderingContext();
            if (fieldMovieCurrentFrame < 0x18DE) {
                renderMovieScrollingText();
            }
            DrawSync(0);
            VSync(2);
            ClearImage(&pCurrentFieldRenderingContext->m0_drawEnv.clip, 0, 0, 0);
            PutDispEnv(&pCurrentFieldRenderingContext->mB8_displayEnv);
            PutDrawEnv(&pCurrentFieldRenderingContext->m0_drawEnv);
            DrawOTag(&pCurrentFieldRenderingContext->m80D4_uiOT[7]);
            updateMovieScrollingText();
            updateFieldMoviePlayer(5);
        }
        movieScrollingTextActive = 0;
        strNextUploadEnabled = 1;
        DrawSync(0);
        VSync(0);
        SetDefDrawEnv(&fieldRenderContext[0].m0_drawEnv, 0, 0, 0x140, 0xE0);
        SetDefDrawEnv(&fieldRenderContext[1].m0_drawEnv, 0, 0x100, 0x140, 0xE0);
        SetDefDispEnv(&fieldRenderContext[0].mB8_displayEnv, 0, 0x100, 0x140, 0xE0);
        SetDefDispEnv(&fieldRenderContext[1].mB8_displayEnv, 0, 0, 0x140, 0xE0);
        fieldRenderContext[1].mB8_displayEnv.isrgb24 = 1;
        fieldRenderContext[0].mB8_displayEnv.isrgb24 = 1;
        fieldRenderContext[1].mB8_displayEnv.isinter = 0;
        fieldRenderContext[0].mB8_displayEnv.isinter = 0;
        freeMovieScrollingText();
    }
    FALLOFF_RETURN(0); /* result unused */
}

/* Japanese cutscene audio (port/src/cd/regional.c): for a scene of US Disc 2
 * that the JP game plays without XA audio, the JP script's sound sequence
 * (0xFF: none); -1 to play the movie as it is. */
int pcRegionalFieldMovieSound(int field, int stream);

/* Plays the movie described by fieldMovieParams (video player overlay). */
void startFieldTransition(void) {
    RECT rect;
    u32* buf;
    void* saved;
    uptr end;
    s32 i;

    /* With Japanese cutscene audio, US Disc 2's movies 8-11 play as the JP
     * game plays their scenes: JP's scripts show the same pictures (parts of
     * its movie 6) with OPX_A0, which turns the movie's XA audio off
     * (fieldMovieFlags 0x40) and plays a sound sequence of effects timed to
     * the frames; the US scripts play the US streams with their XA (English
     * voices) through OP_SET_2D_BACKGROUND. The sequences and their event
     * lists are the same on both discs, and the events count from the first
     * frame of the movie played, so they fall on the same pictures. */
    {
        s32 sequence = pcRegionalFieldMovieSound(fieldMapNumber & 0xFFF, fieldMovieParams.m0_movieId);
        if (sequence >= 0) {
            fieldMovieParams.m18_soundSequenceId = sequence;
            fieldMovieFlags |= 0x40;
        }
    }
    fieldMovieStopRequest = 0;
    fieldMovieWaitingFirstFrame = 0;
    fieldMovieDisplayBuffer = 0;
    buf = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(0xA9)), 0);
    readFile(0xA9, buf, 0, 0x80);
    fieldMovieCurrentFrame = 0;
    movieScrollingTextActive = 0;
    waitCdIdleRenderingField();
    setCurrentDirectory(0x18, 0);
    seekToFile(fieldMovieParams.m0_movieId);
    setCurrentDirectory(4, 0);
    waitCdIdleRenderingField();
    if (NumMechas != 0) {
        freeMechaModule();
        ClearCacheAfterOverlayLoad();
        flushFrame();
        freeHeap(mechaOverlayBuffer);
    }
    deleteAllParticleEffects();
    if (onlyUpdateDirector != 2) {
        setRECT(&rect, 0x140, 0, 0xC0, 0x100);
        LoadImage(&rect, buf);
        DrawSync(0);
        freeHeap(buf);
        buf = allocateHeap(PC_HEAP_BYTES(0x18000), 0);
        StoreImage(&rect, buf);
    }
    end = fieldHeapTopMarker;
    if (mechaOverlayLoadingMode == 0) {
        /* The video player is linked in: a token block keeps the frees
         * balanced, and its data is reset as a fresh copy leaves it. */
        saved = allocateHeap(PC_HEAP_SIZED(8, pcHeapReservation((void*)end, 0x801D3000u)), 1);
        portResetModule("video_player");
    } else {
        saved = allocateHeap(PC_HEAP_BYTES(8), 1);
    }
    freeHeap(buf);
    loadMovieSoundSequence();
    initMovieScrollingText();
    fieldMovieWaitingFirstFrame = 1;
    releasePartyBuffersForMovie();
    ClearCacheAfterOverlayLoad();
    mergeFreeHeapBlocks();
    /* A movie in the frame buffers (and the field's dialogs over one) has no
     * wide picture: 4:3 until it ends (include/pc_wide.h). */
    pcFieldMovieBlock(1);
    initFieldMoviePlayer();
    startFieldMoviePlayback();
    fieldMovieStarted = 1;
    do {
        VSync(0);
        updateFieldMoviePlayer(3);
    } while (fieldMovieWaitingFirstFrame != 0);
    do {
        switch (onlyUpdateDirector) {
        case 1:
            setupFieldRenderingContext();
            updateFieldDialogsOverMovie();
            updateFieldMoviePlayer(6);
            break;
        case 0:
            DrawSync(0);
            VSync(0);
            PutDispEnv(&pCurrentFieldRenderingContext->mB8_displayEnv);
            PutDrawEnv(&pCurrentFieldRenderingContext->m0_drawEnv);
            updateFieldMoviePlayer(3);
            DrawSync(0);
            VSync(0);
            PutDispEnv(&pCurrentFieldRenderingContext->mB8_displayEnv);
            PutDrawEnv(&pCurrentFieldRenderingContext->m0_drawEnv);
            updateFieldMoviePlayer(3);
            playMovieScrollingTextSection();
            break;
        case 2:
            fieldPerFrameReset();
            updateAndRenderField();
            updateFieldMoviePlayer(9);
            break;
        }
        if (fieldDebugDisable == 0) {
            if (onlyUpdateDirector == 2) {
                if (g_padButtonForField & 0x80) {
                    break;
                }
                if (fieldMovieStopRequest != 0) {
                    break;
                }
            } else {
                updateFieldInputs();
                if (g_padButtonForField & 0x20) {
                    break;
                }
            }
        } else if (fieldMovieFlags & 0x80) {
            updateFieldInputs();
            if (g_padButtonForField & 0x20) {
                setCdVolume(0, 10);
                for (i = 0; i < 5; i++) {
                    VSync(0);
                }
                break;
            }
        }
        if (onlyUpdateDirector == 2 && fieldMovieStopRequest != 0) {
            break;
        }
    } while (fieldMovieParams.m1A_loop != 0 || fieldMovieCurrentFrame < fieldMovieParams.mE_lastFrame);
    VSync(0);
    DrawSync(0);
    freeStrPlayer();
    ClearCacheAfterOverlayLoad();
    PutDispEnv(&pCurrentFieldRenderingContext->mB8_displayEnv);
    PutDrawEnv(&pCurrentFieldRenderingContext->m0_drawEnv);
    VSync(0);
    DrawSync(0);
    freeHeap(saved);
    mergeFreeHeapBlocks();
    restorePartyBuffersAfterMovie();
    loadMechaOverlay_start();
    setRECT(&rect, 0, fieldMovieDisplayBuffer << 8, 0x1E0, 0xE0);
    MoveImage(&rect, 0, fieldMovieDisplayBuffer << 8);
    DrawSync(0);
    VSync(0);
    pCurrentFieldRenderingContext = &fieldRenderContext[1];
    PutDispEnv(&fieldRenderContext[1].mB8_displayEnv);
    PutDrawEnv(&pCurrentFieldRenderingContext->m0_drawEnv);
    if (onlyUpdateDirector != 2) {
        convert24BitScreenTo15Bit(0);
    }
    VSync(0);
    fieldRenderContext[0].mB8_displayEnv.isrgb24 = 0;
    pCurrentFieldRenderingContext = &fieldRenderContext[0];
    PutDispEnv(&fieldRenderContext[0].mB8_displayEnv);
    PutDrawEnv(&pCurrentFieldRenderingContext->m0_drawEnv);
    setRECT(&rect, 0, 0x100, 0x140, 0xE0);
    MoveImage(&rect, 0, 0);
    DrawSync(0);
    VSync(0);
    fieldRenderContext[1].mB8_displayEnv.isrgb24 = 0;
    pCurrentFieldRenderingContext = &fieldRenderContext[1];
    PutDispEnv(&fieldRenderContext[1].mB8_displayEnv);
    PutDrawEnv(&pCurrentFieldRenderingContext->m0_drawEnv);
    if (OPX_86Param != 0) {
        D_800ADB50 = 1;
    } else {
        D_800ADB50 = 0;
    }
    loadMechaOverlay_finalize();
    if (onlyUpdateDirector != 2) {
        setCurrentDirectory(4, 0);
        resetMemoryAllocStats(8, 0);
        fieldGraphicsUploaded = 0;
        loadFieldGraphics();
        freeFieldGraphics();
        fullScreenTransitionEffectRenderParam = 0x20;
        fullScreenTransitionEffectRenderMode = 1;
    }
    unloadMovieSoundSequence();
    fieldMovieParams.m18_soundSequenceId = 0xFF;
    pcFieldMovieBlock(0);
    onlyUpdateDirector = 0;
    fieldMoviePlayerStatus = -1;
}

/* Loads the debug digit TIM (file 0xAA) to VRAM (0x380, 0), CLUT (0, 0xE8). */
void loadDebugDigitTexture(void) {
    u32* buf;

    resetMemoryAllocStats(8, 0);
    setCurrentDirectory(4, 0);
    buf = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(0xAA)), 1);
    readFile(0xAA, buf, 0, 0x80);
    waitReadCompletion(0);
    loadTimToVram(buf, 0x380, 0, 0, 0xE8, 0, 0);
    DrawSync(0);
    freeHeap(buf);
}

/* Frees the debug overlay prims. */
void freeDebugCameraDigits(void) {
    if (debugCameraDigitsAllocated != 0) {
        debugCameraDigitsAllocated = 0;
        DrawSync(0);
        freeHeap(debugCameraDigitPolys[0]);
        freeHeap(debugCameraDigitPolys[1]);
    }
}

/* Sets debug prim idx's UVs to its image (flipped vertically), offset by
 * (du, dv). */
void setDebugDigitUV(s32 idx, s32 du, s32 dv) {
    s32 unused[6]; /* never used; keeps the retail frame size */
    s32 j = debugCameraDigits[idx].m4_image;
    s32 v = debugCameraDigitImages[j].m2_v + dv;
    s32 w = debugCameraDigitImages[j].m4_w;
    s32 u = debugCameraDigitImages[j].m0_u + du;
    s32 u1 = u + w;

    setPolyFT4UVsClamped(&debugCameraDigitPolys[g_frameOddOrEven][idx], u, v + debugCameraDigitImages[j].m6_h - 1, u1, v + debugCameraDigitImages[j].m6_h - 1, u,
                  v - 1, u1, v - 1);
}

/* Debug overlay: camera yaw and pitch as hex digits plus a blinking cursor
 * (109 prims set up by initDebugCameraDigits). */
void renderDebugCameraDigits(void) {
    s32 i;
    s32 yaw;
    s32 pitch;
    s32 count;

    if (debugCameraDigitsAllocated != 0) {
        count = 0x6D;
        i = 0;
        pitch = ratan2(length2d((fieldSceneState.m10_cameraAt.vx - fieldSceneState.m0_cameraEye.vx) >> 16,
                                (fieldSceneState.m10_cameraAt.vz - fieldSceneState.m0_cameraEye.vz) >> 16),
                       (fieldSceneState.m10_cameraAt.vy - fieldSceneState.m0_cameraEye.vy) >> 16) &
                0xFFF;
        yaw = fieldSceneState.m10E_cameraTan & 0xFFF;
        for (; i < 16; i++) {
            setDebugDigitUV(i, (yaw >> 4) & 0xF, 0);
            addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[4], &debugCameraDigitPolys[g_frameOddOrEven][i]);
        }
        for (; i < 29; i++) {
            setDebugDigitUV(i, 0, (pitch >> 4) & 0xF);
            addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[4], &debugCameraDigitPolys[g_frameOddOrEven][i]);
        }
        for (; i < 34; i++) {
            setDebugDigitUV(i, (yaw & 0xF) << 3, 0);
            yaw >>= 2;
            addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[4], &debugCameraDigitPolys[g_frameOddOrEven][i]);
        }
        for (; i < 39; i++) {
            setDebugDigitUV(i, (pitch & 0xF) << 3, 0);
            pitch >>= 2;
            addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[4], &debugCameraDigitPolys[g_frameOddOrEven][i]);
        }
        for (; i < 40; i++) {
            if ((debugCameraDigitsFrameCounter & 0xF) == 0) {
                debugCameraDigitsCursorFrame++;
            }
            if (debugCameraDigitsCursorFrame >= 3) {
                debugCameraDigitsCursorFrame = 0;
            }
            setDebugDigitUV(i, 0, debugCameraDigitsCursorFrame << 3);
            addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[4], &debugCameraDigitPolys[g_frameOddOrEven][i]);
        }
        for (; i < 42; i++) {
            setDebugDigitUV(i, (debugCameraDigitsFrameCounter >> 2) & 0xF, 0);
            addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[4], &debugCameraDigitPolys[g_frameOddOrEven][i]);
        }
        for (; i < 43; i++) {
            if (!(debugCameraDigitsFrameCounter & 0x10)) {
                addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[4], &debugCameraDigitPolys[g_frameOddOrEven][i]);
            }
        }
        for (; i < count; i++) {
            addPrim(&pCurrentFieldRenderingContext->m80D4_uiOT[4], &debugCameraDigitPolys[g_frameOddOrEven][i]);
        }
        debugCameraDigitsFrameCounter++;
    }
}


/* k, k2 and k3 are three copies of i (lagging one iteration behind the
 * increment): loop.c combines them into one giv worth reducing (one copy is
 * not), which puts k's zero init after the hoisted table bases as in retail.
 * off is the reduced i * sizeof(POLY_FT4). */
void initDebugCameraDigits(void) {
    s32 i;
    s32 abr;
    POLY_FT4* p;
    POLY_FT4* p1;
    s32 x;
    s32 y;
    s32 u;
    s32 v;
    s32 w;
    s32 h;
    s32 j;
    s32 k;
    s32 off;
    s32 k2;
    s32 k3;

    loadDebugDigitTexture();
    abr = 0;
    debugCameraDigitsCursorFrame = 0;
    resetMemoryAllocStats(8, 0);
    debugCameraDigitPolys[0] = allocateHeap(PC_HEAP_SIZED(0x6D * sizeof(POLY_FT4), 0x6D * 0x28), 0);
    debugCameraDigitPolys[1] = allocateHeap(PC_HEAP_SIZED(0x6D * sizeof(POLY_FT4), 0x6D * 0x28), 0);
    for (i = 0, k = 0, k2 = 0, k3 = 0, off = 0; i < 0x6D;) {
        p = (POLY_FT4*)((u8*)debugCameraDigitPolys[0] + off);
        p1 = (POLY_FT4*)((u8*)debugCameraDigitPolys[1] + off);
        SetPolyFT4(p);
        setRGB0(p, 0x80, 0x80, 0x80);
        p->clut = GetClut(0, 0xE8);
        switch (((&debugCameraDigits[0].m6_flags)[k * 4] >> 4) & 0xF) {
        case 0:
            abr = 1;
            break;
        case 1:
            abr = 2;
            break;
        }
        p->tpage = GetTPage(0, abr, 0x380, 0);
        SetSemiTrans(p, 1);
        x = (&debugCameraDigits[0].m0_x)[k2 * 4];
        y = (&debugCameraDigits[0].m2_y)[k2 * 4];
        j = (&debugCameraDigits[0].m4_image)[k2 * 4];
        w = (&debugCameraDigitImages[0].m4_w)[j * 4];
        h = (&debugCameraDigitImages[0].m6_h)[j * 4];
        u = (&debugCameraDigitImages[0].m0_u)[j * 4];
        v = (&debugCameraDigitImages[0].m2_v)[j * 4];
        setXYWH(p, x, y, w, h);
        switch ((&debugCameraDigits[0].m6_flags)[k3 * 4] & 0xF) {
        case 0:
            setPolyFT4UVsClamped(p, u, v, u + w, v, u, v + h, u + w, v + h);
            break;
        case 1:
            setPolyFT4UVsClamped(p, u + w - 1, v, u - 1, v, u + w - 1, v + h, u - 1, v + h);
            break;
        case 2:
            setPolyFT4UVsClamped(p, u, v + h - 1, u + w, v + h - 1, u, v - 1, u + w, v - 1);
            break;
        case 3:
            setPolyFT4UVsClamped(p, u + w - 1, v + h - 1, u - 1, v + h - 1, u + w - 1, v - 1, u - 1, v - 1);
            break;
        }
        *p1 = *p;
        i++;
        off = i * sizeof(POLY_FT4);
        k = i;
        k2 = i;
        k3 = i;
    }
    debugCameraDigitsAllocated = 1;
}

void initParticlesPoly(sParticle* pParticle, s32 type, s32 particleAbr) {
    u8 unused[0x58]; /* never used; keeps the retail frame size */
    POLY_FT4* p = &pParticle->m50_poly[0];
    POLY_FT4* p1 = &pParticle->m50_poly[1];
    s32 w;
    s32 h;
    s32 cx;
    s32 cy;
    s32 x0;
    s32 y0;
    s32 x1;
    s32 y1;

    SetPolyFT4(p);
    w = gParticlePerTypeConfig[type].m0_halfWidth;
    h = gParticlePerTypeConfig[type].m2_halfHeight;
    cx = gParticlePerTypeConfig[type].m4_centerX;
    cy = gParticlePerTypeConfig[type].m6_centerY;
    pParticle->mA0_worldspaceVertices[0].vz = 0;
    pParticle->mA0_worldspaceVertices[1].vz = 0;
    pParticle->mA0_worldspaceVertices[2].vz = 0;
    pParticle->mA0_worldspaceVertices[3].vz = 0;
    setRGB0(p, 0x80, 0x80, 0x80);
    x0 = (cx << 4) + (-w << 4);
    y0 = (cy << 4) + (-h << 4);
    x1 = (w << 4) + (cx << 4);
    y1 = (h << 4) + (cy << 4);
    pParticle->mA0_worldspaceVertices[0].vx = x0;
    pParticle->mA0_worldspaceVertices[0].vy = y0;
    pParticle->mA0_worldspaceVertices[1].vx = x1;
    pParticle->mA0_worldspaceVertices[1].vy = y0;
    pParticle->mA0_worldspaceVertices[2].vx = x0;
    pParticle->mA0_worldspaceVertices[2].vy = y1;
    pParticle->mA0_worldspaceVertices[3].vx = x1;
    pParticle->mA0_worldspaceVertices[3].vy = y1;
    setPolyFT4UVsClamped(p, gParticlePerTypeConfig[type].m8_u0, gParticlePerTypeConfig[type].mA_v0 + 0x40, gParticlePerTypeConfig[type].mC_u1 - 1,
                  gParticlePerTypeConfig[type].mE_v1 + 0x40, gParticlePerTypeConfig[type].m10_u2, gParticlePerTypeConfig[type].m12_v2 + 0x3F,
                  gParticlePerTypeConfig[type].m14_u3 - 1, gParticlePerTypeConfig[type].m16_v3 + 0x3F);
    SetSemiTrans(p, 1);
    p->tpage = GetTPage(0, particleAbr, 0x3C0, 0x140);
    p->clut = GetClut(0x100, 0xF7);
    *p1 = *p;
}

/* Moves the particle VRAM backup into a new heap block. */
void reallocBackBufferRamCopy(s32 flags) {
    Block8000* p;

    if (isBackBufferRamCopyEnabled == 1) {
        resetMemoryAllocStats(8, 0);
        p = allocateHeap(PC_HEAP_BYTES(0x8000), flags);
        *p = *(Block8000*)backBufferRamCopy;
        freeHeap(backBufferRamCopy);
        backBufferRamCopy = (u32*)p;
    }
}

/* Saves the particle texture page (0x3C0, 0x100, 64x256) to the heap. */
void saveBackBufferRamCopy(void) {
    if (isBackBufferRamCopyEnabled != 1) {
        isBackBufferRamCopyEnabled = 1;
        resetMemoryAllocStats(8, 0);
        backBufferRamCopy = allocateHeap(PC_HEAP_BYTES(0x8000), 1);
        backBufferRamCopyRect.x = 0x3C0;
        backBufferRamCopyRect.y = 0x100;
        backBufferRamCopyRect.w = 0x40;
        backBufferRamCopyRect.h = 0x100;
        StoreImage(&backBufferRamCopyRect, backBufferRamCopy);
        DrawSync(0);
    }
}

/* Restores the particle texture page saved by saveBackBufferRamCopy. */
void restoreBackBufferRamCopy(void) {
    if (isBackBufferRamCopyEnabled != 0) {
        backBufferRamCopyRect.x = 0x3C0;
        isBackBufferRamCopyEnabled = 0;
        backBufferRamCopyRect.y = 0x100;
        backBufferRamCopyRect.w = 0x40;
        backBufferRamCopyRect.h = 0x100;
        LoadImage(&backBufferRamCopyRect, backBufferRamCopy);
        DrawSync(0);
        freeHeap(backBufferRamCopy);
    }
}
