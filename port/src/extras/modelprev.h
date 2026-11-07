#ifndef PORT_EXTRAS_MODELPREV_H
#define PORT_EXTRAS_MODELPREV_H

/*
 * A model archive shown by the game's own model code (modelprev.c), the
 * sandbox's first user: in a sandbox (sandbox.h) the archive is copied into
 * the heap, relocated (fieldModelRelocation, the PC's native conversion),
 * its primitives set up and lit once (initModel1, initModel2 mode 1, as a
 * field's static models), and its textures uploaded
 * by uploadTextureToVram (packed VRAM uploads) into the sandbox's VRAM; each
 * draw projects it with the GTE (submitModelForRendering, mode 0) into an
 * ordering table that DrawOTag hands to the sandbox's GPU. view.h draws the
 * result. The model viewer (xg-htn.3) builds on this.
 */

#include <stdint.h>

#include "vgpu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XgModelPreview XgModelPreview;

/* textures: packed VRAM upload files (xgTexturesCheck), may be none. NULL
 * on failure, with the reason in why. */
XgModelPreview* xgModelPreviewCreate(const uint8_t* model, int size, const uint8_t* const* textures,
                                     const int* textureSizes, int textureCount, char* why, int whySize);
void xgModelPreviewDestroy(XgModelPreview* m);

int xgModelPreviewParts(const XgModelPreview* m);
int xgModelPreviewPrims(const XgModelPreview* m);   /* polygons in all parts */
int xgModelPreviewRadius(const XgModelPreview* m);  /* from the parts' bounding boxes */

/* Draw it seen from rotation (yaw, pitch; 4096 = 360 degrees) at `distance`
 * on a w x h screen (the projection's centre in its middle, H = h). The
 * primitives are then the GPU's (xgVgpuPrims); returns their count. */
int xgModelPreviewDraw(XgModelPreview* m, int yaw, int pitch, int distance, int w, int h);
XgVgpu* xgModelPreviewGpu(XgModelPreview* m);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_MODELPREV_H */
