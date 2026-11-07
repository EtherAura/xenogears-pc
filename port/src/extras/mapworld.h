#ifndef PORT_EXTRAS_MAPWORLD_H
#define PORT_EXTRAS_MAPWORLD_H

/*
 * The world map loaded and drawn by the world map overlay's own code, in a
 * sandbox (mapworld.c), for the map viewer (maps.cpp).
 *
 * Loading is worldmapEntryPoint's and worldmapMode0_update's set-up (a new
 * game's defaults, the configuration for the story progress chosen): the
 * archive (landmarks, exits, billboards), the textures, and the terrain's
 * streaming around a position (setupInitialGrid, loadWorldmapChunk, through
 * the streaming's own host-file path). The landmarks and exits come from the
 * archive as the game set them up.
 *
 * Drawing is the ground task's frame: the ground around the position
 * (worldmapGroundPrepareRenderingTable, drawWorldmapGround), the landmarks
 * (renderWorldmapModels), billboards and sky, on the world map's own 320 x
 * 216 screen, from a free camera (the game's matrices from another eye; the
 * game draws the ground up to 0xF00 deep and landmarks to 0xD80, as on the
 * PS1). A whole area's top-down map is drawn chunk by chunk, each streamed
 * in and seen straight down with its heights flattened (a flat ground seen
 * straight down is an exact map), and put together on the CPU (maprast.h).
 */

#include <stdint.h>

#include "disc.h"
#include "vgpu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XG_WORLD_SCREEN_W 320
#define XG_WORLD_SCREEN_H 216
#define XG_WORLD_CHUNK 0x800 /* world units across a terrain chunk */

typedef struct {
    int x, y, z; /* world units */
    int hidden;
} XgWorldModel;

typedef struct {
    int x, z, w, d; /* rectangle (world units) */
    int field;      /* destination field (the game's value: 0x400 and up, flags in the high bits) */
    int type;
    int table;      /* which table (0 walking, 1-3 others) */
} XgWorldExit;

typedef struct {
    int progress;    /* story progress it was set up for */
    int sizeX, sizeZ; /* chunks */
    int config;      /* the configuration */
    int baseFile;    /* its first file in directory 0x24 */
    int startX, startZ;
    int models;
    XgWorldModel* model;
    int exits;
    XgWorldExit* exit;
    int stages;      /* story progress, then negative special modes -8..-15 */
    int stage[17];
    char note[128];
} XgWorldInfo;

typedef struct XgWorld XgWorld;

/* progress >= 0: story stage; -8..-15: a special world configuration. */
XgWorld* xgWorldLoad(XgDisc* disc, int progress, char* why, int whySize);
void xgWorldDestroy(XgWorld* w);
/* Actual table entries read by this sandbox, for export provenance. */
int xgWorldSources(const XgWorld* w, int* entries, int capacity);
const XgWorldInfo* xgWorldInfo(const XgWorld* w);
XgVgpu* xgWorldGpu(XgWorld* w);

/* Layers (mapsview.h mapsWorldDraw's, and overlays). */
#define XG_WORLD_GROUND 0x01
#define XG_WORLD_LANDMARKS 0x02
#define XG_WORLD_TREES 0x04
#define XG_WORLD_SKY 0x08
#define XG_WORLD_ALLTILES 0x10
#define XG_WORLD_FOG 0x20
#define XG_WORLD_ANIMATE 0x40  /* the animated tiles (water) step a frame */
#define XG_WORLD_CENTRED 0x80  /* the projection's centre at the screen's middle (top-down) */
#define XG_WORLD_EXITS 0x100   /* exit areas outlined */
#define XG_WORLD_MARKS 0x200   /* landmarks marked */

/* One frame from `eye` toward `at` (world units, y down) on the world
 * map's 320 x 216 screen: the terrain streamed around `at` first when it
 * left the loaded area. fov: vertical, degrees (0: the game's). Returns the
 * primitives drawn. */
int xgWorldDraw(XgWorld* w, const float eye[3], const float at[3], float fov, unsigned layers);

/* The whole area top-down: pxPerChunk pixels per chunk (sizeX * px by
 * sizeZ * px, RGBA, malloc'd). Takes a while (every chunk streamed). */
uint32_t* xgWorldTopDown(XgWorld* w, int pxPerChunk, unsigned layers, int* outW, int* outH);

/* A world point on the last drawn screen; 0 behind the camera. */
int xgWorldProject(XgWorld* w, const float p[3], float* x, float* y);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_MAPWORLD_H */
