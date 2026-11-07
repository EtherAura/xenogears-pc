#ifndef PC_INTERPOLATION_H
#define PC_INTERPOLATION_H
enum { PC_INTERP_MODEL, PC_INTERP_MECHA, PC_INTERP_PARTICLE, PC_INTERP_SPRITE, PC_INTERP_SHADOW, PC_INTERP_TRANSITION, PC_INTERP_WAVE,
       PC_INTERP_TERRAIN /* depth-cued (the world map's ground, billboards): a palette change keeps the face moving */ };
/* Display metadata only: these never wait, deliver events or update game C. */
void pcInterpolationScene(void);
/* Authored distortion capture: source offsets are in field-frame pixels,
 * independent of framebuffer parity. The replay records copies and sampling in ordering-table order. */
enum { PC_INTERP_CAPTURE_BEGIN = 1, PC_INTERP_CAPTURE_SKIP,
       PC_INTERP_CAPTURE_BODY, PC_INTERP_CAPTURE_SAMPLE, PC_INTERP_CAPTURE_MOVE,
       PC_INTERP_CAPTURE_FREEZE, PC_INTERP_CAPTURE_STASH,
       PC_INTERP_CAPTURE_END = 16 };
void pcInterpolationCapturePrim(void* primitive, int op, int sourceX, int sourceY);
/* Wide wave tiles are omitted at negative width. Retain the unclamped pose
 * in metadata, then apply that omission after interpolating the edges. */
void pcInterpolationWaveEdgePrim(void* primitive, int u0, int u1, int dx0, int dx1);
/* Frozen 320x224 transition at VRAM (704,256), including its wide slot.
 * Only these authored packets may sample the separately owned capture. */
void pcInterpolationTransitionPrim(const void* owner, unsigned part, void* primitive);
void pcInterpolationFrameBegin(int field, int x, int y, int w, int h);
/* FrameBegin's scene tag for the world map (fields are 0..0xFFF). */
enum { PC_INTERP_WORLDMAP = 0x1000 };
void pcInterpolationFrameEnd(void);
void pcInterpolationObject(const void* owner, const void* model);
void pcInterpolationObjectType(const void* owner, const void* model, int kind);
void pcInterpolationModel(const void* primitives);
void pcInterpolationPrim(void* primitive);
void pcInterpolationDiscretePrim(void* primitive);
void pcInterpolationHeapFree(const void* start, int bytes);
/* A pooled object begins a new authored lifetime without freeing its slot. */
void pcInterpolationLifetime(const void* owner);
/* Explicit authored part identity for scratch/double-buffered primitives. */
void pcInterpolationKey(const void* owner, const void* resource, unsigned part, void* primitive);
void pcInterpolationKeyType(const void* owner, const void* resource, unsigned part, void* primitive, int kind);
int pcInterpolationBuilding(void);
void pcInterpolationPlacement(const void* owner, const void* resource, unsigned part, void* primitive, float x, float y, float z);
void pcSpriteInterpolationPrim(void* actor, void* primitive, unsigned part);
#endif
