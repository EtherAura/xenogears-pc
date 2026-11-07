#ifndef PORT_EXTRAS_WRITERS_H
#define PORT_EXTRAS_WRITERS_H

/*
 * The extras' file writers (writers.c), all our own code over SDL3 and libc:
 *   PNG    SDL3's PNG encoder (SDL_SavePNG_IO, SDL 3.4) when the SDL has it,
 *          else our own minimal one (stored deflate blocks)
 *   WAV    PCM, 16-bit
 *   glTF   a GLB writer for the model viewer: meshes (positions, texture
 *          coordinates, vertex colours, indices), PNG textures with nearest
 *          filtering, a node hierarchy with TRS, TRS and morph animations; no skinning
 *          (docs/PORT_EXTRAS.md "Export").
 * Every writer returns 1 on success, 0 with the reason in xgWriterError().
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const char* xgWriterError(void);

/* A picture as PNG (RGBA bytes, 8 bits each, rows top to bottom). */
int xgPngWrite(const char* path, const uint32_t* rgba, int w, int h);
/* The same into memory (malloc'd; *size bytes); NULL on failure. */
uint8_t* xgPngEncode(const uint32_t* rgba, int w, int h, size_t* size);

/* 16-bit PCM, interleaved channels. */
int xgWavWrite(const char* path, const int16_t* samples, int frames, int channels, int rate);

/* ---- glTF (GLB) --------------------------------------------------------------------------- */

typedef struct XgGltf XgGltf;

XgGltf* xgGltfCreate(void);
void xgGltfDestroy(XgGltf* g);

/* A texture from PNG bytes (copied); its material index, -1 on failure.
 * Nearest filtering, repeat wrapping; blend: alpha mask (texel 0 clear) or
 * blend (semi-transparent, approximated). */
int xgGltfAddTexturedMaterial(XgGltf* g, const char* name, const uint8_t* png, size_t pngSize, int blend);
/* A material with vertex colours only. */
int xgGltfAddColourMaterial(XgGltf* g, const char* name);
/* Blend a material (a colour material's vertex alpha, a texture's alpha)
 * instead of masking it. */
void xgGltfSetBlend(XgGltf* g, int material, int blend);

/* A mesh of one primitive (triangles): positions xyz; uv (may be NULL);
 * colours rgba 0-1 (may be NULL); indices. Its index, -1 on failure. */
int xgGltfAddMesh(XgGltf* g, const char* name, int material, const float* positions, const float* uvs,
                  const float* colours, int vertices, const uint32_t* indices, int indexCount);
/* A mesh of several primitives (one per material): an empty mesh, then its
 * primitives (as xgGltfAddMesh's). Index of the mesh / of the primitive in
 * it, -1 on failure. A mesh needs at least one primitive to be written. */
int xgGltfAddEmptyMesh(XgGltf* g, const char* name);
int xgGltfAddPrimitive(XgGltf* g, int mesh, int material, const float* positions, const float* uvs,
                       const float* colours, int vertices, const uint32_t* indices, int indexCount);
/* Position deltas for one target of a primitive. Every primitive of a mesh
 * must have the same target count/order; weights are mesh defaults. */
int xgGltfAddMorphTarget(XgGltf* g, int mesh, int primitive, const float* deltas, int vertices);
int xgGltfSetMorphWeights(XgGltf* g, int mesh, const float* weights, int count);

/* A node: mesh (-1 none), parent node (-1 a root), translation, rotation
 * (quaternion xyzw), scale (each may be NULL). Parents come first. */
int xgGltfAddNode(XgGltf* g, const char* name, int mesh, int parent, const float* t, const float* r, const float* s);

/* An animation; then channels on it: path 't', 'r' or 's' of a node, with
 * `count` keys (times in seconds; values 3 or 4 floats each), linear. */
int xgGltfAddAnimation(XgGltf* g, const char* name);
int xgGltfAddChannel(XgGltf* g, int animation, int node, char path, const float* times, const float* values,
                     int count);
/* Discrete TRS keys, used for visibility without interpolation ramps. */
int xgGltfAddStepChannel(XgGltf* g, int animation, int node, char path, const float* times, const float* values,
                         int count);
/* One scalar per target per key (key-major). STEP preserves game ticks. */
int xgGltfAddWeightsChannel(XgGltf* g, int animation, int node, const float* times, const float* values,
                            int keys, int targets);

int xgGltfWriteGlb(XgGltf* g, const char* path);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_WRITERS_H */
