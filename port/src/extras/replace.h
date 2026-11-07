#ifndef PORT_EXTRAS_REPLACE_H
#define PORT_EXTRAS_REPLACE_H

/*
 * The installed replacement packs for the extras' pictures (replace.c): what
 * a view (view.h) draws in place of a primitive's native texels when the
 * viewer shows the Enhanced look. The game's own look-ups by content
 * (port/src/gpu/texture_pack.c, gp0.c), from the view's VRAM instead of the
 * game's:
 *
 *   - the texture pack (texture-packs, the 3D textures' Cel 8x): a patch's
 *     texels snapped as the game snaps them (texture.c): to the load that
 *     holds them (the sandbox GPU's uploads, vgpu.h xgVgpuUploadCover), else
 *     to the stable region of its texture page (vram.c vramStableRegion: one
 *     load's texels, connected in 8x8 blocks, here every texel the view's
 *     VRAM holds counting as one load, as a field's textures all come in
 *     together), else the patch itself; an RGBA image by texel and palette
 *     key, else the index map by texel key;
 *   - the sprite library (sprite-packs, Sprites -> Cel 8x): index maps by the
 *     cell a sprite part samples, probed a texel larger on each side as the
 *     game probes them (textureSpriteCelSample);
 *   - the library's poses (Enhanced only, as in the game: gp0.c spritePose):
 *     one whole-figure picture for an actor's parts, named by their cells,
 *     texel keys and placement (spritePoseKey), drawn as one quad in the
 *     reference part's frame. The in-between drawings (timed in the game by
 *     the actor's hold) are not shown.
 *
 * Content keys only: a replacement found here is the picture the game shows
 * for the same texels, and none is found for anything the packs do not
 * cover. Look-ups never change the views' VRAM or the game's.
 */

#include <stdint.h>

#include "vgpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* XgReplace.kind */
#define XG_REPLACE_NONE 0
#define XG_REPLACE_INDEXED 1 /* an index map coloured with the primitive's CLUT */
#define XG_REPLACE_RGBA 2    /* an RGBA image (alpha 0 clear, 128 STP) */
#define XG_REPLACE_SKIP 3    /* a part a pose stands for: not drawn */

/* The libraries (xgReplacePrims' `libraries`, xgReplaceInstalled). */
#define XG_REPLACE_PACK 0x01  /* texture pack */
#define XG_REPLACE_CELS 0x02  /* sprite library cels */
#define XG_REPLACE_POSES 0x04 /* sprite library poses (the Enhanced engine only) */

typedef struct {
    uint8_t kind;      /* XG_REPLACE_* */
    uint8_t scale;     /* replacement pixels per native texel */
    uint8_t pose;      /* a pose: drawn as the quad x, y with u, v 0..w, 0..h (FREE_UV) */
    int16_t u, v;      /* the image's native rectangle in the texture page (logical texels) */
    int16_t w, h;
    float x[4], y[4];  /* a pose's corners (primitive vertex order: 0 1 / 2 3), scene units */
    uint16_t clut;     /* a pose's CLUT (its reference part's) */
    uint64_t id;       /* the image (xgReplaceImage), unique */
} XgReplace;

/* An image found for a view: its pixels, kept here (copied from the library)
 * until about to be needed no more; NULL when gone (look it up again). */
typedef struct {
    const uint8_t* pixels; /* indices (one byte) or RGBA (four), pw x ph */
    int pw, ph;
    int indexed;
} XgReplaceImage;
const XgReplaceImage* xgReplaceImage(uint64_t id);

/* The libraries installed now (XG_REPLACE_*; poses only in the Enhanced
 * engine, where the game draws them). */
unsigned xgReplaceInstalled(void);

/* Those the player's settings draw in the game now (the appearances: 3D textures and
 * sprites as Cel 8x, or a texture pack of their own): what a viewer shows first. */
unsigned xgReplaceChosen(void);

typedef struct XgReplacer XgReplacer;
XgReplacer* xgReplacerCreate(void);
void xgReplacerDestroy(XgReplacer* r);

/* Each primitive's replacement (out[n]) from the libraries asked for, the
 * texels read from `vram` (the sandbox GPU's, or a view's own; gpu may be
 * NULL: no uploads to snap to). `version` and `identity` say when the VRAM
 * changed (answers are kept until then). poseParts: the actor's parts when
 * the primitives are exactly one sprite actor's (the sprite viewer), else 0
 * (no pose). Returns the primitives replaced; *pending: images still loading
 * (ask again next frame). */
int xgReplacePrims(XgReplacer* r, unsigned libraries, const XgVgpu* gpu, const uint16_t* vram, unsigned version,
                   uint64_t identity, const XgPrim* prims, int n, int poseParts, XgReplace* out, int* pending);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_REPLACE_H */
