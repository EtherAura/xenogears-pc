#ifndef PORT_GPU_INTERNAL_H
#define PORT_GPU_INTERNAL_H

/* Internals shared by the files of port/src/gpu. */

#include <stdint.h>

#include "gpu.h"

#define VRAM_W 1024
#define VRAM_H 512

/* ---- vram.c: the CPU copy of VRAM and what the GPU has changed since ----- */

/* The CPU copy. Exact wherever the GPU has not drawn since the last readback;
 * vramSync() brings it up to date. */
extern uint16_t vramShadow[VRAM_H][VRAM_W];

/* Regions are tracked in 16x16 tiles. */
#define TILE_SHIFT 4
#define TILES_X (VRAM_W >> TILE_SHIFT)
#define TILES_Y (VRAM_H >> TILE_SHIFT)

typedef struct {
    uint64_t rows[TILES_Y]; /* bit x of rows[y]: tile (x, y) */
} TileSet;

/* A rectangle in VRAM, inclusive bounds, already inside 0..1023 x 0..511. */
typedef struct {
    int x0, y0, x1, y1;
} VRect;

void tilesClear(TileSet* t);
void tilesAdd(TileSet* t, VRect r);
int tilesHit(const TileSet* t, VRect r);
/* Tiles the CPU reads after the frame drew them (vram.c): a native draw,
 * whether r is such, and the vertical blank that carries them over. */
void vramOwnedDraw(VRect r);
int vramCpuOwned(VRect r);
void vramOwnedVBlank(void);
void vramOwnedReset(void); /* none (a GPU reset) */

/* Mark a region the GPU has drawn (the CPU copy is stale there). */
void vramMarkDrawn(VRect r);
int vramIsDrawn(VRect r);
/* A CPU-rasterized draw has been uploaded. Its intersecting tiles must have
 * been synchronized before the draw; retain rendered provenance and versions. */
void vramResolveCpuDraw(VRect r);
/* CPU-authoritative draws may share a physical upload until a GPU read. */
void vramDeferCpuDraw(VRect r);
void vramFlushCpuWrites(VRect r);
/* Discard superseded device pixels only in completely overwritten tiles. */
void vramResolveCpuWrite(VRect r);
/* Bring the CPU copy up to date where the GPU has drawn inside r. */
void vramSyncRect(VRect r);
/* Decoded texture dependencies. Generations change on writes, never on
 * readback. Rendered provenance survives readback; a partial upload only
 * clears tiles it completely replaces (safe conservative capture exclusion). */
void vramReset(void);
uint64_t vramTileGeneration(int x, int y);
/* The newest generation in a block of VRAM_BLOCK_TILES x VRAM_BLOCK_TILES
 * tiles (64x64 pixels): an early answer to whether anything under a texture
 * changed, before its tiles are visited (texture.c current). */
#define VRAM_BLOCK_SHIFT 2
#define VRAM_BLOCK_TILES (1 << VRAM_BLOCK_SHIFT)
uint64_t vramBlockGeneration(int bx, int by);
int vramWasRendered(const TileSet* tiles);
void vramMarkUploaded(VRect r, int checkMask);
void vramMarkCopied(VRect r, int rendered);
/* Classify a whole native upload before overwriting it, independently of
 * the display copy's scale/history. StoreImage supplies its complete output
 * (contiguous rows), including transfers that wrap either VRAM edge. */
int vramLoadRendered(int x, int y, int w, int h, const uint16_t* src, int setMask, int checkMask);
void vramStoreCapture(int x, int y, int w, int h, const uint16_t* pixels);
/* The keep around a picture between (port/src/game/battling/between.c):
 * while it is on, a tile is kept (its pixels aside) before its first write;
 * vramKeepWrite from every VRAM write path (uploads, moves, fills, draws). */
void vramKeepWrite(VRect r);
/* A draw (device-only: the CPU copy is not written): the tiles wholly inside
 * the device-kept rectangle (vramKeepDeviceRect, from libgpu.c
 * pcVramKeepDevice) need no keep. */
void vramKeepDraw(VRect r);
void vramKeepDeviceRect(VRect r);
int vramKeepDeviceChanged(void); /* after vramKeepEnd: a tile inside it changed */
int vramKeeping(void);
/* Most recent unchanged upload/copy wholly covering the sample. Tile
 * validity is conservative: even an adjacent write in its tile invalidates. */
int vramUploadCover(VRect sample, VRect* upload);
/* The same, telling which ring entry it was (index, generation), and whether
 * that entry is still the upload it was, unchanged (texture.c's snap memo). */
int vramUploadCoverAt(VRect sample, VRect* upload, unsigned* index, uint64_t* gen);
int vramUploadIntact(unsigned index, VRect r, uint64_t gen);
/* The newest block generation under a rectangle (vramBlockGeneration over
 * its blocks). */
uint64_t vramBlockNewest(VRect r);
/* Counts the times every tile's history started over (vramReset, a state
 * load): generations alone cannot tell such a VRAM from the one before. */
extern uint32_t vramLayoutSerial;
/* Upload stamps (1 + the vblank of the upload that last wrote a pixel, 0
 * after draws, fills and rendered copies) and the stable region around a
 * sample: the box, inside page, of the load whose stamps run from..to
 * (vram.c). The decoder masks the box's other pixels. */
void vramStampClock(uint32_t vblank);
uint32_t vramStamp(int x, int y);
int vramStableRegion(VRect sample, VRect page, VRect* region, uint32_t* from, uint32_t* to);
/* Split a rectangle that may run past the edges of VRAM into the pieces the
 * PS1 wraps it into: at most 4, each with its offset inside the rectangle. */
typedef struct {
    VRect r;
    int dx, dy;
} VPiece;
int vramWrap(int x, int y, int w, int h, VPiece out[4]);

/* VRAM-to-VRAM copies (GP0(80h), MoveImage): psx-spx "VRAM to VRAM blitting"
 * says nothing of overlap. Measured with Beetle PSX as a black box
 * (work/clean-room/measure/gpu, M-G9: 588 copies overlapping in x and y
 * both ways, with mask settings and across the edges, every pixel): a copy
 * goes row by row from its first row, each row in pieces of COPY_PIECE
 * halfwords from its first column, and a piece reads its whole source
 * before it writes. libgpu.c moveCopy does that on the CPU copy; the device
 * and the hi-res copy (vk.c gvkHiMove) do the same in steps. */
#define COPY_PIECE 128
/* Whether the source and destination pieces of a copy meet. */
int vramCopyMeets(const VPiece* src, int ns, const VPiece* dst, int nd);
/* The steps of a copy: each piece (row r, piece k at step[r * pieces + k],
 * pieces = (w + COPY_PIECE - 1) / COPY_PIECE) gets one more than the latest
 * step of an earlier piece whose destination its source meets, else 0. The
 * pieces of a step may all read and then all write. Returns the number of
 * steps. step needs h * pieces entries. */
int vramCopySteps(int sx, int sy, int dx, int dy, int w, int h, uint16_t* step);
/* The pieces of step s as rectangles in the copy's own coordinates (joined
 * along rows and down rows with the same runs); returns how many (at most
 * h * pieces). */
int vramCopyStepRects(const uint16_t* step, int w, int h, int s, VRect* out);

/* ---- vk.c: the Vulkan device --------------------------------------------- */

/* One vertex of a triangle drawn into VRAM (draw.vert / draw.frag). */
typedef struct {
    float x, y;    /* VRAM pixel coordinates (pixel centres at +0.5) */
    float u, v;    /* texel coordinates; the shader rounds to nearest */
    float r, g, b; /* 0..255 */
    uint32_t mode; /* DRAW_* */
    uint32_t tex;  /* tpage | clut << 16 */
    uint32_t texWindow; /* GP0(E2h) bits 0-19 */
    uint32_t clipMin;   /* x | y << 16 */
    uint32_t clipMax;
    uint32_t span;      /* the triangle's rows in the span buffer (ivec4 index), or ~0 */
    uint32_t wideTex;   /* wide pass: 15-bit texels from the wide target (WIDE_TEX), else 0 */
    uint32_t wideEdge;  /* wide pass: the 4:3 draw area's x0 | x1 << 16 in target x */
} GpuVertex;

/* GpuVertex.mode (keep in sync with shaders/draw.frag). */
#define DRAW_TEXTURED 0x001u
#define DRAW_RAW 0x002u        /* texture without modulation */
#define DRAW_SEMI 0x004u       /* semi-transparency enabled */
#define DRAW_SEMI_SHIFT 3      /* bits 3-4: the semi-transparency mode */
#define DRAW_DITHER 0x020u
#define DRAW_SET_MASK 0x040u
#define DRAW_CHECK_MASK 0x080u
#define DRAW_FILL 0x100u       /* GP0(02h): no clip, mask or blending */
#define DRAW_GOURAUD 0x200u    /* colour from the spans, not the vertex */
#define DRAW_PRECISE 0x400u    /* display copy: sample attributes at the projected position */
#define DRAW_SURFACE 0x800u    /* display copy: original 3D surface coverage */
#define DRAW_FRINGE 0x1000u    /* display copy: fill uncovered seam pixels */
#define DRAW_RESOLVED 0x4000u /* final native words, opaque including black */
#define DRAW_CACHED 0x2000u    /* native decoded texels appended to span rows */
#define DRAW_FADE 0x8000u      /* PC: every texel blended half and half (pcFadePrim), STP or not */

typedef struct {
    int x, y, w, h; /* VRAM area shown */
    int rgb24;
    int enabled;
    /* Where a TV shows it (present.c's 4:3 TV): the visible frame is 2560
     * GPU clocks from 260h by 240 lines from line 10h (PAL 288 from 13h,
     * LIBGPU's origins); the picture starts tvClock clocks and tvLine lines
     * into it, each pixel `clocks` wide, `rows` rows per line (2 interlaced).
     * clocks 0: not set yet. */
    int tvClock, tvLine, clocks, rows, frameLines;
} GpuDisplay;

int gvkInit(void* sdlWindow);
void gvkShutdown(void);
int gvkHasInterlock(void);
/* 1: the chosen device is an integrated GPU, -1 a discrete or other one, 0 none chosen yet. */
int gvkDeviceIntegrated(void);
/* hires.c: the settings whose defaults depend on the device, applied once it is chosen. */
void hiresDeviceDefaults(void);
int gvkOrderedWrites(void);
uint32_t gvkOrderedOverflow(void);
void gvkSetOrderedFallback(int on);
/* Append triangles (n vertices, a multiple of 3). */
void gvkDraw(const GpuVertex* v, int n);
/* The same, for a PS1 triangle walked in fixed point (gp0.c): `ints` int32 of
 * span data (a multiple of 4) go to the span buffer and the vertices' `span`
 * field is set to where they landed. */
void gvkDrawSpans(GpuVertex* v, int n, const int32_t* spans, int ints);
/* End the current batch: what follows sees everything drawn so far. */
void gvkBarrier(void);
/* Flush narrow work; nonzero means independent wide enhanced work remains.
 * Its wide dependency sets must stay live until a full/wide boundary. */
int gvkNativeBarrier(void);
/* Write a rectangle inside VRAM from rows of `pitch` halfwords. */
void gvkUpload(VRect r, const uint16_t* src, int pitch);
/* The same into the wide target (wide.c). */
void gvkUploadWide(VRect r, const uint16_t* src, int pitch);
/* Save-state storage includes hidden columns. Restore those native pixels
 * without treating the storage width as an active high-resolution width. */
int gvkWideUsedWidth(void);
void gvkRestoreWideWidth(int columns);
void gvkRestoreWide(VRect r, const uint16_t* src, int pitch);
/* Copy a rectangle inside VRAM to another place inside VRAM. Both pieces of a
 * copy go through a scratch image, so overlap behaves like a snapshot. */
void gvkCopyToScratch(VRect src, int sx, int sy);
void gvkCopyFromScratch(int sx, int sy, VRect dst);
/* A picture between's device-side keep (libgpu.c pcVramKeepDevice, wide.c
 * pcWideFrameKeep): VRAM's rectangle r and the hi-res copy's picture of it,
 * or the wide target's rows (x, y, w, h) and their hi-res copy, copied to
 * images of their own, and put back from them; 0 when the device has no
 * room for the images (nothing kept: keep the pixels on the CPU). */
int gvkKeepFrame(VRect r);
void gvkKeepFramePutBack(void);
int gvkKeepWide(int x, int y, int w, int h);
void gvkKeepWidePutBack(void);
/* Read a VRAM rectangle into rows of `pitch` halfwords, waiting for the device. */
void gvkReadback(VRect r, uint16_t* dst, int pitch);
/* Show the display area (vertical blank number `vblank`). */
void gvkPresent(const GpuDisplay* d, uint32_t vblank);
/* The settings menu's frames (the game paused): the same display again with
 * the menu drawn over it (port/src/ui); shot: also write it as <shot>.png in
 * the present dump directory (dumpPresentNamed). */
void gvkPresentMenu(const GpuDisplay* d, uint32_t vblank, const char* shot);
/* The wide target (wide.c): triangles drawn into it, after the current
 * batch's VRAM draws (coordinates in the target; texels from VRAM, or from
 * the target itself where GpuVertex.wideTex says so). */
void gvkDrawWide(const GpuVertex* v, int n);
void gvkDrawSpansWide(GpuVertex* v, int n, const int32_t* spans, int ints);
/* Copy a rectangle w x h from (sx, sy) to (dx, dy); `from`/`to` are 0 for
 * VRAM, 1 for the wide target. Regions inside one image must not overlap. */
void gvkCopy(int from, int sx, int sy, int to, int dx, int dy, int w, int h);
/* Read a rectangle of the wide target (w*h halfwords), waiting. */
void gvkReadWide(uint16_t* dst, int x, int y, int w, int h);
/* Rewind captures: a copy of VRAM (wide 0) or the wide target (wide 1) queued
 * behind the work so far, not waited for; its pixels' place in the buffer
 * gvkLateWait gives (pitch w), or -1 (none: read now). gvkLateWait waits for
 * the copies if they are not done yet (normally they are long done). */
ptrdiff_t gvkLateRead(int wide, VRect r);
const uint16_t* gvkLateWait(void);
/* Hand recorded work to the device without waiting. */
void gvkSubmit(void);

/* The present path's state, for XG_PRESENT_LOG and stall reports. */
#define PRESENT_NOT_TRIED 0x7FFFFFFF /* lastAcquire/lastPresent: no call */
typedef struct {
    uint32_t shown, skipped, recoveries;
    uint64_t nativeFrames, hostFrames, missedHostSlots, autoFrames, pending;
    int hostTarget, hostActive;
    int lastAcquire, lastPresent; /* VkResult */
    const char* acquireName;
    const char* presentName;
    uint32_t image;
    double acquireMs;
    int noPicture; /* vblanks in a row the visible window got no new picture */
    int width, height, images;
    int windowW, windowH;
    int haveSwapchain, stale;
    int surface; /* 1, 0 (none: headless), -1 (lost, being made again) */
    const char* mode;
} GvkPresentStats;
void gvkPresentStats(GvkPresentStats* out);
int presentLogging(void);
/* Read a rectangle of VRAM (wide 0) or of the wide target (wide 1) from
 * another thread while the game thread is stuck, through a queue of its own;
 * 0 when that cannot be done (no second queue, the device busy). */
int gvkRescueRead(int wide, uint16_t* dst, int x, int y, int w, int h);

/* ---- the hi-res copy (hires.c policy, vk.c device side) -------------------
 *
 * With upscale=auto or 2..12 (settings.ini), every drawing command and
 * transfer also goes to a hidden copy of VRAM and of the wide target at N
 * times the resolution, and the window shows the copy. The game never reads
 * it: StoreImage, CLUTs and 4- and 8-bit texels come from the 1x VRAM, which
 * stays exactly as it is. upscale=1 (the default): no copy at all. */

/* A vertex of the copy's triangles (shaders/hires.vert, hires.frag). */
typedef struct {
    float x, y;    /* 1x coordinates (VRAM, or the wide target's) */
    float u, v;    /* the texel coordinates at the vertex */
    float r, g, b; /* 0..255 */
    uint32_t mode; /* DRAW_* */
    uint32_t tex;  /* tpage | clut << 16 */
    uint32_t texWindow;
    uint32_t clipMin, clipMax;
    uint32_t uvMin, uvMax; /* the texels the 1x pass sampled: u | v << 16, each 16-bit signed */
    uint32_t wideTex, wideEdge;
    /* The VRAM copy's triangle rasterised where the wide pass rasterised it
     * (x | y << 16, whole 1x pixels, signed; 0 for the wide pass and for
     * primitives drawn into VRAM alone): the device then gives both copies
     * the same coverage, texels and shades, and the fragment stores them
     * back at the VRAM place (hires.frag). */
    uint32_t rasterShift;
    float w; /* 1 affine; view depth / 4096 for perspective textures */
    uint32_t sample[4]; /* immutable decoded texture: word base, UV origin, size, filter */
} HiVertex;

/* hires.c: upscale_color */
#define HIRES_COLOR_TRUE 0     /* no dither, blending in 8 bits (shaders/hires.frag) */
#define HIRES_COLOR_DITHER_1X 1 /* 15-bit, the PS1's dither at its own pixel size */
#define HIRES_COLOR_DITHER_HI 2 /* 15-bit, dithered at the copy's pixel size */
#define HIRES_COLOR_RAW 3       /* 8-bit fronts and blending, no dither */

/* vk.c. The copy's N now (0: none). */
int gvkHiScale(void);
/* Make the copy N times (0: none), refilled from the 1x images, with colour
 * mode `color`; between frames. Returns the N made (0 when the device could
 * not hold it). */
int gvkHiConfigure(int n, int color);
/* What the device allows: the largest image side; its device-local memory. */
void gvkHiLimits(int* maxDim, uint64_t* heapBytes);
/* Optional whole-submission GPU timing, read at existing fence waits. Old
 * epochs (resolution/setting/state changes) never enter a new sample. */
int gvkGpuTimeEnable(int on);
void gvkGpuTimeReset(void);
int gvkGpuTimeTake(double* ms, unsigned* frames, int* scale);
/* Triangles into the copy of VRAM / of the wide target, with the batch. */
void gvkHiDraw(const HiVertex* v, int n);
void gvkHiDrawWide(const HiVertex* v, int n);
struct TextureSample;
int gvkHiFeedbackSource(VRect source);
void gvkHiDrawCaptured(const HiVertex* v,int n,int wide);
void gvkHiDrawTexture(const HiVertex* v, int n, int wide, const struct TextureSample* sample, int filter);
/* An upload's side in the copy, recorded before its 1x upload: `data` (rows
 * of `pitch` halfwords) is the new 1x picture of VRAM rectangle r. how 0:
 * replicated; 1: pixels whose colour VRAM already has there keep the copy's
 * pixels (bit 15 from the upload); 2: the same against stash `slot`, whose 1x
 * picture is `ref` (rows of refPitch). */
#define HI_LOAD_REPLICATE 0
#define HI_LOAD_KEEP 1
#define HI_LOAD_STASH 2
void gvkHiResolveCpu(VRect r, const uint16_t* data, int pitch);
void gvkHiLoad(VRect r, const uint16_t* data, int pitch, int how, int slot, const uint16_t* ref, int refPitch);
/* The copy of VRAM rectangle r kept in stash slot (StoreImage). */
#define HI_STASH_SLOTS 2
#define HI_STASH_W 320
#define HI_STASH_H 256
void gvkHiStash(VRect r, int slot);
/* A private, out-of-place source for the arena CPU blur. Both rectangles
 * are native coordinates; a NULL wide rectangle means a narrow capture. */
int gvkHiEffectCapture(VRect narrow, const VRect* wide);
int gvkHiEffectBlur(int target, VRect dst, int narrowCentre);
void gvkHiEffectReset(void);
int wideEffectRect(VRect key, VRect* physical, int* shifted);
/* MoveImage's side in the copy (the pieces vramWrap gives for w x h). */
void gvkHiMove(const VPiece* src, int ns, const VPiece* dst, int nd, int w, int h, int setMask, int checkMask);
/* An upload into the wide target whose only change is the mask bit on every
 * pixel (pcWideCaptureMask): the copy keeps its pixels and gets the bit. */
void gvkUploadWideMasked(VRect r, const uint16_t* src, int pitch);
/* Read hi-res pixels (x, y, w, h in the copy's pixels) of the copy of VRAM
 * (wide 0) or of the wide target (1), waiting; 0 if it cannot. */
int gvkHiRead(int wide, uint32_t* dst, int x, int y, int w, int h);
/* How wide the copy of the wide target is now (1x columns). */
int gvkHiWideWidth(void);
int gvkHiWideHeight(void);
void gvkHiTrimWide(int rows);
/* Column x1 of the wide target is about to be drawn (gp0.c, before the
 * draw): the copy grows to it. Copies and uploads say so themselves. */
void gvkWideTouch(int x1);

/* hires.c */
/* At each vblank, after the present pass (target: the window's size). */
void hiresVBlank(const GpuDisplay* d, int targetW, int targetH);
void hiresGpuTimePause(void); /* menu/movie/blank: keep cap, invalidate samples */
/* gp0.c: tiles of VRAM where the copy has drawn (more than its 1x picture). */
void hiresMarkDetail(VRect r);
/* libgpu.c pcVramKeepDevice: which tiles of r have detail, kept across a
 * picture between and put back with the copy's pixels there. */
void hiresDetailKeep(VRect r);
void hiresDetailPutBack(void);
/* libgpu.c: a LoadImage piece. hiresLoadPlan before the CPU copy takes the
 * new picture (it compares with the old one), hiresLoadApply after it and
 * before the 1x upload. */
int hiresLoadPlan(VRect r, const uint16_t* src, int pitch, int setMask, int checkMask);
int hiresPutBackPlan(VRect r);
void hiresLoadApply(VRect r, int plan);
/* A StoreImage piece (the CPU copy current there). */
void hiresStore(VRect r);
/* A MoveImage, after its 1x part. */
void hiresMove(int sx, int sy, int dx, int dy, int w, int h, int setMask, int checkMask);
/* dump.c: hires_<vblank>.png (XG_DUMP_HIRES). */
void hiresDump(const GpuDisplay* d, uint32_t vblank, int frameWritten);
/* At exit: a line on the uploads it kept hi-res. */
void hiresSummary(void);

/* ---- present.c: the display options' side of the present pass ------------ */

/* The present pass's push constants (shaders/present.frag). */
typedef struct {
    int32_t disp[4];   /* VRAM x, y, width, height (src 1: in the wide target) */
    int32_t dest[4];   /* target x, y, width, height */
    int32_t osd[4];    /* the note: target x, y, width, height (width 0: none) */
    int32_t clip[4];   /* the picture shows only inside: x, y, width, height */
    int32_t rgb24;
    int32_t enabled;
    int32_t filterMode; /* PRESENT_FILTER_* (shaders/filters.glsl), never SAME */
    int32_t osdScale;  /* target pixels per font pixel */
    int32_t osdStride; /* the note's bitmap width in pixels */
    int32_t osdBase;   /* its first word in the frame's span buffer (vk.c) */
    int32_t src;       /* 0: VRAM; 1: the wide target (wide.c) */
    int32_t fill;      /* beside a 4:3 picture in a wide frame: 0 black, 1 blurred picture */
    int32_t picture;   /* the picture prepass (vk.c): its pixels' first word in the span buffer, -1 none */
    int32_t codes;     /* its xbr codes, -1 none */
} PresentConstants;

/* The constants for a target of w x h pixels. The note's 1-bit bitmap, if
 * one is showing, goes to bits (at most cap words; the caller places it and
 * sets osdBase); returns its size in words, else 0. */
int presentConstants(const GpuDisplay* d, int w, int h, PresentConstants* pc, uint32_t* bits, int cap);
/* Once per vblank (the note's timer). */
void presentVBlank(void);

/* ---- gp0.c: the GP0 command processor ------------------------------------- */

void gp0Reset(void);
/* Feed words to GP0 as the DMA or the CPU would. */
void gp0Write(const uint32_t* words, int n);
/* The current drawing state, for libgpu.c. */
void gp0SetMaskBits(uint32_t e6);
int gp0SetMask(void);
int gp0CheckMask(void);
/* End the current batch (before a transfer). */
void gp0Flush(void);
/* The texture cache: invalidate its texels; clear it and the CLUT cache
 * (GP0(01h)). */
void gp0InvalidateTexture(void);
void gp0ClearCache(void);
/* VRAM is about to change in r (gp0.c: owed texture-cache walks are done first). */
void gp0VramChanging(VRect r);
void gp0ClearImage(int x, int y, int w, int h, uint32_t rgb);
/* Fill a rectangle inside VRAM on the device (the CPU copy is the caller's). */
void gp0Fill(VRect r, uint32_t rgb);
/* The packet being fed (pcWidePrim's treatment, or NULL), for the wide copy. */
struct WidePacket;
void gp0SetPacket(const struct WidePacket* p);
void gp0ProbeVBlank(uint32_t vblank);
/* Fill a rectangle of the wide target. */
void gp0WideFill(VRect r, uint32_t rgb);

/* ---- wide.c: true widescreen (include/pc_wide.h) ---------------------------
 *
 * The wide target is a WIDE_W x WIDE_H image like VRAM. Its first VRAM_H rows
 * mirror each frame buffer F at x - F.x0 + M on the same rows (M: the margin
 * each side; up to WIDE_FRAME_W wide in all, a 640-wide display's too); the
 * optional vertical expansion repacks frames into separate 256-line slots
 * with aligned Y shifts (native VRAM coordinates never move). The
 * rows below hold screen captures, slot k at (0, WIDE_SLOT_Y + 256k), and
 * below those a picture of the display made from VRAM (WIDE_VIEW_Y). */
#define WIDE_W 2048
#define WIDE_H 2048
#define WIDE_FRAME_W 2048
#define WIDE_SLOT_X 0
#define WIDE_SLOT_Y 512
#define WIDE_SLOT_H 256
#define WIDE_SLOTS 4
/* A display that shows nothing drawn wide: its picture with black margins. */
#define WIDE_VIEW_Y (WIDE_SLOT_Y + WIDE_SLOTS * WIDE_SLOT_H)
/* GpuVertex.wideTex: 15-bit texels at (x + dx, y + dy) of the target. */
#define WIDE_TEX(dx, dy) (0x80000000u | ((uint32_t)(dy)&0x7FFFu) << 16 | ((uint32_t)(dx)&0xFFFFu))

typedef struct WidePacket {
    int dx;         /* moved in the wide picture */
    unsigned flags; /* PC_WIDE_* */
    int texDx;      /* added to texel x (captures) */
    int texDy;      /* added after native texture Y wrapping */
    int texClass;   /* PC_TEX_*; independent of aspect and wide flags */
    int classified;
    int geometry;   /* PC_GEOMETRY_*; independent of aspect and wide flags */
    int geometryAbr; /* 0 inherit, 1..4 private untextured blend mode */
    int poseGroup;   /* 1 + the actor's part group (pcSpritePosePart), else 0 */
    int fade;        /* drawn half transparent (pcFadePrim: scenery between the free camera and the party) */
} WidePacket;



/* A drawing command's wide copy, if its draw area is (inside) a frame buffer
 * and the frame is wide: target x = VRAM x + shift, the draw area widened to
 * clipX0..clipX1 (target x), edgeX0..edgeX1 the unwidened one. */
typedef struct {
    int shift;
    int shiftY;
    int clipX0, clipX1;
    int clipY0, clipY1;
    int edgeX0, edgeX1;
    int edgeY0, edgeY1;
    int fb;
    int fbY1;    /* the frame buffer's last row */
    int belowY1; /* the last row PC_WIDE_BELOW primitives may draw (scratch) */
} WideDraw;
/* PC_WIDE_BELOW: the scratch rows below a frame buffer in the wide target. */
#define WIDE_BELOW_ROWS 32

void wideReset(void);
/* Rectangles kept per frame for tools/widecheck.py (WideView.mask). */
#define WIDE_MASKS 32
/* At each vertical blank, before the dumps and the present pass: what this
 * vblank shows, and the margin of the frame that follows. */
void wideVBlank(const GpuDisplay* d);
typedef struct {
    int has;         /* the display shows a frame buffer drawn wide */
    int wide;        /* ... and shown from the copy (wide aspect or expanded Y) */
    int m;           /* its margin */
    int top, bottom; /* extra display lines, independent of horizontal margins */
    int x, y, w, h;  /* the picture with margins, in the wide target */
    int clean, shifted;
    /* Where a shifted picture's centre is not the 4:3 frame (2D moved away
     * or drawn only in the wide picture), in the 4:3 frame's pixels: nMask
     * rectangles, or -1 when that is not known (the whole centre). */
    int nMask;
    VRect mask[WIDE_MASKS];
    const char* why; /* why not clean */
    const char* none; /* !has, aspect wide: why the display has no wide picture */
} WideView;
const WideView* wideCurrentView(void);
/* Read-only bounds of an already drawn framebuffer's expanded image.
 * 1 ready, 0 original/unavailable, -1 unsafe wide contents. */
int wideFrameView(VRect native, WideView* view);
/* The complete immutable transition slot, including rows the triangle's
 * native UV clamp can reach below the captured screen. */
int wideTransitionCaptureRect(VRect* rect);
int wideMargin(void);
int wideActive(void);

/* gp0.c */
int wideDrawTarget(int clipX0, int clipY0, int clipX1, int clipY1, WideDraw* out);
/* The wide pass's texel source for a primitive textured from (tpage, u, v
 * bounds); marks the frame 4:3 when it samples a capture or a frame without
 * PC_WIDE_HANDLED. */
uint32_t wideTexture(const WideDraw* wd, uint32_t tpage, int u0, int u1, int v0, int v1, const WidePacket* pkt);
/* A packet moved or drawn only in the wide picture was drawn: box (target
 * coordinates) is where the wide picture's centre differs from VRAM's for it
 * (where it was drawn, or where VRAM has it and the wide picture does not). */
void wideNoteShifted(const WideDraw* wd, VRect box);
/* VRAM written by drawing (screen captures there go stale). */
void wideNoteWrite(VRect r);
/* VRAM written by a drawing command the wide target did not get (its draw
 * area is no frame buffer): the frame buffers' wide pictures take it. */
void wideNoteDraw(VRect r);
extern int wideSlotsLive;
/* libgpu.c transfers, after VRAM has them. */
void wideFill(VRect r, uint32_t rgb);
void wideLoad(VRect r);
void wideMove(VRect src, VRect dst);
/* pcWidePrim's table: the treatment of a packet, and the end of a DrawOTag. */
const WidePacket* widePacket(const void* packet);
void widePacketsDone(void);

/* ---- libgpu.c -------------------------------------------------------------- */

void gpuLoad(int x, int y, int w, int h, const uint16_t* src);
void gpuMove(int sx, int sy, int dx, int dy, int w, int h);
void gpuFill(int x, int y, int w, int h, uint32_t rgb);
/* Note that drawing happened (for DrawSyncCallback). */
void gpuDrawIssued(void);
void gpuHostWrite(void);
/* What the display shows now (the last PutDispEnv and SetDispMask). */
const GpuDisplay* gpuDisplay(void);
int gpuCapturing(void);

/* ---- dump.c ------------------------------------------------------------- */

int pngWrite(const char* path, int w, int h, const uint8_t* rgb);
void dumpInit(void);
void dumpVBlank(const GpuDisplay* d, uint32_t vblank);
/* VRAM into the dump directory as vram_<vblank>.png/.bin. */
void dumpVram(uint32_t vblank);
/* The dump directory (XG_DUMP_FRAMES, else the working directory). */
const char* dumpDir(void);
/* w x h 15-bit pixels as <path>.png, and with bin also raw as <path>.bin. */
void dumpImage15(const char* path, const uint16_t* px, int w, int h, int bin);
/* The display area as 8-bit RGB (w*h*3 bytes) from the CPU copy. */
void displayToRgb(const GpuDisplay* d, uint8_t* out);
/* XG_DUMP_PRESENT: whether this vblank's presented image is wanted, and the
 * image itself (w*h RGBA bytes). */
int dumpPresentWanted(uint32_t vblank);
void dumpPresent(uint32_t vblank, int w, int h, const uint8_t* rgba);
/* The same as <name>.png in XG_DUMP_PRESENT's directory (else XG_DUMP_FRAMES'
 * or the working directory), whether it changed or not. */
void dumpPresentNamed(const char* name, int w, int h, const uint8_t* rgba);

/* ---- logging --------------------------------------------------------------- */

void gpuLog(const char* fmt, ...);
/* Report a missing feature once per distinct key. */
void gpuWarnOnce(const char* key, const char* fmt, ...);

/* ---- save states (system/savestate.h) ------------------------------------ */

/* vk.c: the hi-res copies made again from the 1x VRAM and wide target. */
void gvkHiRefill(void);
/* hires.c: the copy's bookkeeping forgotten and the copy refilled. */
void hiresStateLoaded(void);
/* vram.c: VRAM as the last save took it (its thumbnail). */
const uint16_t* vramSaved(void);

#endif /* PORT_GPU_INTERNAL_H */
