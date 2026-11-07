#ifndef PORT_EXTRAS_MAPRAST_H
#define PORT_EXTRAS_MAPRAST_H

/*
 * The map viewer's pictures for export (maprast.c): a GPU's primitives
 * (vgpu.h XgPrim) drawn on the CPU into an RGBA picture of any size, by the
 * rules the views' shader follows (shaders/view.frag): texture pages and
 * CLUTs read from VRAM, the texture window, texel 0 clear, modulation by the
 * vertex colour (/128), the four semi-transparency modes for STP texels (and
 * for whole untextured primitives), each primitive kept in its draw area;
 * affine colours and texture coordinates, 8-bit colour (5-bit channels
 * widened as c << 3 | c >> 2), no dithering or mask bits. Lines are one scene
 * unit wide, as the views draw them.
 *
 * On the CPU so that a picture can be as large as wanted (a whole world map)
 * and is the same in any build and headless.
 */

#include <stdint.h>

#include "vgpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The scene rectangle (x0, y0)-(x1, y1) (VRAM or screen units) drawn into a
 * w x h picture (rgba: w * h words, R in the low byte, rows top to bottom),
 * cleared to `background` first (0xAABBGGRR). textures 0: textured
 * primitives in their vertex colours. */
void xgRasterize(const XgPrim* prims, int count, const uint16_t* vram, float x0, float y0, float x1, float y1,
                 int w, int h, uint32_t background, int textures, uint32_t* rgba);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_MAPRAST_H */
