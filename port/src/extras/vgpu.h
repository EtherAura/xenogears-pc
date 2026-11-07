#ifndef PORT_EXTRAS_VGPU_H
#define PORT_EXTRAS_VGPU_H

/*
 * A GPU for game code run outside the game (vgpu.c): what the sandbox's
 * captured LIBGPU calls do (gpu.h gpuSetCapture), kept for a viewer.
 *
 *   - VRAM: 1024 x 512 halfwords of its own. LoadImage, StoreImage,
 *     MoveImage, ClearImage and the GP0 transfer and fill commands act on it
 *     as the GPU's do (16-bit, wrapping at the edges; no mask bits).
 *   - Drawing: the GP0 packets of DrawOTag, DrawOTagEnv and DrawPrim are not
 *     rasterised here but kept as primitives (XgPrim), in drawing order, with
 *     the state the GPU would draw them with (texture page, window, draw area
 *     and offset). view.h draws them with our Vulkan device at any size.
 *
 * A viewer can also fill one itself: xgVgpuLoad for pictures, xgVgpuGp0 for
 * its own packets, xgVgpuAddPrim for primitives of its own (which may use
 * XG_PRIM_FREE_UV: texture coordinates from the page's corner, without the
 * PS1's 8-bit wrap, for a whole picture as one primitive).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "gpu/gpu.h" /* GpuCapture (a C header without C++ guards) */

#define XG_VRAM_W 1024
#define XG_VRAM_H 512

/* XgPrim.flags */
#define XG_PRIM_TEXTURED 0x01
#define XG_PRIM_SEMI 0x02     /* semi-transparent (mode: tpage bits 5-6) */
#define XG_PRIM_RAW 0x04      /* texture not modulated by the colour */
#define XG_PRIM_GOURAUD 0x08
#define XG_PRIM_LINE 0x10     /* a line from vertex 0 to 1 */
#define XG_PRIM_FILL 0x20     /* GP0(02h): a rectangle of colour, no draw area or offset */
#define XG_PRIM_FREE_UV 0x40  /* u, v count from the page corner, no 8-bit wrap or window */

/* One primitive as the GPU would draw it: vertex positions with the draw
 * offset applied (VRAM coordinates), 3 or 4 vertices (a quad is 0-1-2 and
 * 1-2-3, as the GPU splits it). Rectangles (GP0 60h-7Fh) become quads. */
typedef struct {
    int16_t x[4], y[4];
    int16_t u[4], v[4];
    uint8_t r[4], g[4], b[4];
    uint16_t clut;   /* the CLUT word: x / 16 in bits 0-5, y in bits 6-14 */
    uint16_t tpage;  /* page x / 64 in 0-3, y / 256 in 4, blend 5-6, depth 7-8 (0 4-bit, 1 8-bit, 2 15-bit) */
    uint32_t window; /* GP0(E2h) bits: mask x, mask y, offset x, offset y (5 bits each) */
    int16_t area[4]; /* draw area: x0, y0, x1, y1 (inclusive) */
    uint8_t count;   /* 3 or 4 (2 for a line) */
    uint8_t flags;   /* XG_PRIM_* */
} XgPrim;

typedef struct XgVgpu XgVgpu;

XgVgpu* xgVgpuCreate(void);
void xgVgpuDestroy(XgVgpu* g);
/* Unique for this GPU's lifetime, even when an allocator reuses its address. */
uint64_t xgVgpuIdentity(const XgVgpu* g);

/* The capture to install with gpuSetCapture (sandbox.c does it). */
const GpuCapture* xgVgpuCapture(XgVgpu* g);

/* VRAM, 1024 x 512, and a number that changes whenever it does (a view
 * uploads it again then). */
uint16_t* xgVgpuVram(XgVgpu* g);
unsigned xgVgpuVramVersion(const XgVgpu* g);
void xgVgpuVramChanged(XgVgpu* g); /* after writing xgVgpuVram yourself */
void xgVgpuClearVram(XgVgpu* g);

/* VRAM transfers, as LoadImage and friends do them. */
void xgVgpuLoad(XgVgpu* g, int x, int y, int w, int h, const uint16_t* src);
void xgVgpuStore(XgVgpu* g, int x, int y, int w, int h, uint16_t* dst);
/* The latest load (of the last 64, nothing written into it since) holding VRAM x0, y0 to
 * x1, y1 (inclusive): 1 and its rectangle in out (x0, y0, x1, y1). As the game's texture
 * look-ups snap a patch to its upload (port/src/gpu/texture.c), for replace.c. */
int xgVgpuUploadCover(const XgVgpu* g, int x0, int y0, int x1, int y1, int* out);

/* GP0 words (one or more whole commands). */
void xgVgpuGp0(XgVgpu* g, const uint32_t* words, int count);

/* The primitives drawn since the last clear. */
int xgVgpuPrimCount(const XgVgpu* g);
const XgPrim* xgVgpuPrims(const XgVgpu* g);
void xgVgpuClearPrims(XgVgpu* g);
void xgVgpuAddPrim(XgVgpu* g, const XgPrim* p);

/* The drawing state the next primitives get (the draw area, as DRAWENV's
 * clip, and the offset): the sandbox's PutDrawEnv sets it too. */
void xgVgpuSetArea(XgVgpu* g, int x0, int y0, int x1, int y1, int ofsX, int ofsY);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_VGPU_H */
