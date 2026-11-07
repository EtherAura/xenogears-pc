#ifndef PORT_GAME_WORLDMAP_MAPSVIEW_H
#define PORT_GAME_WORLDMAP_MAPSVIEW_H

/*
 * The map viewer's way into the world map overlay (mapsview.c, PC only):
 * code compiled into the worldmap module that port/src/extras/mapworld.c
 * calls inside an extras sandbox whose world holds the worldmap module (and
 * a world of its own, system/world.h, for the sound driver). Never called by
 * the game.
 *
 * Coordinates: world units (a terrain chunk is 0x800 across, the map
 * worldmapSizeX x worldmapSizeY chunks, wrapping); y points down. The world
 * map draws relative to its position (worldmapRadarPosition3): the frame
 * the camera works in has x = world x - position x, y = world y, z = -(world
 * z - position z).
 */

#include "common.h"
#include "psyq.h"

/* The world map's start in mode 0 (walking: worldmapEntryPoint's defaults
 * for a new game) at story progress `progress` (field variable 0: it picks
 * the configuration, initWorldMap), as worldmapEntryPoint and
 * worldmapMode0_update set it up, without the fade-in, music, the party and
 * the tasks. Negative progress -8..-15 selects the special configurations
 * directly through those world modes. 1 when set up. */
s32 mapsWorldStart(s32 progress);

/* The story progress values from which each configuration of the walking
 * world map is used (initWorldMap's table): out[k] for configuration k;
 * followed by -8..-15 for the directly selected special configurations;
 * returns how many. */
s32 mapsWorldStages(s32* out, s32 max);

typedef struct {
    s32 sizeX, sizeZ;  /* chunks */
    s32 config;        /* the configuration (initWorldMapVar0) */
    s32 baseFile;      /* its first file in directory 0x24 */
    s32 startX, startZ; /* where the start put the party (world units) */
    s32 models;
    s32 exits[4];      /* exit areas per table (walking, gear, ...) */
} MapsWorldView;
void mapsWorldView(MapsWorldView* out);

/* Landmark i: its place (world units), whether hidden; 0 past the last. */
s32 mapsWorldModel(s32 i, s32* x, s32* y, s32* z, s32* hidden);
/* Exit area i of table t: rectangle (world x, z, width, depth), destination
 * field (with the game's flags), type; 0 past the last. */
s32 mapsWorldExit(s32 t, s32 i, s32* x, s32* z, s32* w, s32* d, s32* field, s32* type);

/* Move the world map's position to (x, z): the 9 x 9 chunks around it read
 * at once (the streaming's own host-file path), the rest kept loaded. */
void mapsWorldGoTo(s32 x, s32 z);
/* The terrain's heights of every loaded chunk set to 0 (top-down maps: a
 * flat ground seen straight down is an exact map); kept for chunks loaded
 * later while on. */
void mapsWorldFlatten(s32 on);

/* One frame of the world map's drawing on its 320 x 216 screen: the ground
 * (worldmapGroundPrepareRenderingTable, drawWorldmapGround), landmarks
 * (renderWorldmapModels), billboards, sky, into an OT of our own, then
 * DrawOTag. The camera: rotation rows (right, down, forward) and eye in the
 * position's frame (world units), projection distance H. layers: bit 0
 * ground, 1 landmarks, 2 billboards (trees), 3 sky and clouds, 4 every
 * ground tile (not the game's visibility test: for views it was not made
 * for), 5 depth fog (the ground's CLUTs by depth, as the game), 6 a frame of
 * the animated tiles (water) first, 7 the screen's centre at its middle
 * (else 120 lines down, as the walking map has it). */
void mapsWorldDraw(MATRIX* rotation, s32 eyeX, s32 eyeY, s32 eyeZ, s32 H, u32 layers);

#endif /* PORT_GAME_WORLDMAP_MAPSVIEW_H */
