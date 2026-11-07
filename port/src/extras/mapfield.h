#ifndef PORT_EXTRAS_MAPFIELD_H
#define PORT_EXTRAS_MAPFIELD_H

/*
 * A field loaded and drawn by the field overlay's own code, in a sandbox
 * (mapfield.c), for the map viewer (maps.cpp).
 *
 * Loading follows the field mode's own start (fieldEntryPoint, bootField):
 * the kernel's start of a game in the sandbox (initKernelGlobals,
 * initGameState from the disc, resetSpriteSystem), then the field's party
 * and compass files, the archive (loadInitialField), its setup
 * (initFieldData: images, NPC sprite sheets, models, encounters, scripts,
 * triggers, dialogue, walkmesh, lights, camera, entity placement, and script
 * routine 0 of every actor, startAllEntityScripts) and its textures
 * (loadFieldGraphics). The files come from the disc image through the game's
 * own host-file path (hostfiles.h); nothing waits for the drive, a vblank or
 * the sound driver, and nothing is played.
 *
 * Drawing is the field's own frame without its update: the camera half of
 * updateAllEntities (updateCameraInterpolation, lookAtDivided,
 * setProjectionMatrixForField), then renderObjects, renderChars (which also
 * steps the sprites' animation), the background picture and the gears, into
 * an ordering table that DrawOTag hands to the sandbox's GPU. The free camera
 * is the same code from another eye and target. Overlays (walkmesh,
 * triggers, actor markers) are primitives of our own, projected the same way.
 */

#include <stdint.h>

#include "disc.h"
#include "vgpu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XG_FIELD_COUNT 730 /* 04/B8+2n on disc 1 */

/* What to start the field from. */
typedef struct {
    int useState;          /* 1: gameState (below) instead of the disc's initial game state */
    const void* gameState; /* a GameState as the game keeps it (e.g. the paused game's) */
    int storyProgress;     /* field variable 0 (story progress); -1: as the state has it */
    int entry;             /* field variable 2 (the entry point); -1: as the state has it */
} XgFieldStart;

typedef struct {
    int flags;        /* the archive's entity flags (0x40 actor without a model, 0x20 hidden, ...) */
    int script;       /* 1: a script actor (index < the script's actor count) */
    int model;        /* model index in section 2 (-1: none) */
    int hidden;       /* flag 0x20 after setup */
    int pos[3];       /* where it stands (world units) */
    int rot[3];       /* model rotation (4096 = 360 degrees) */
    int direction;    /* a script actor's facing (m108) */
    int walkmesh;     /* walkmesh layer, -1 */
    int triangle;     /* current triangle on it, -1 */
    int character;    /* playable character id, -1 */
    int npc;          /* section 3 bundle index from spriteSource, -1 */
    int sprite;       /* has a sprite */
} XgFieldActor;

typedef struct {
    int16_t v[3][3];        /* corners xyz */
    int16_t neighbour[3];   /* -1: edge */
    uint8_t material;       /* index into the material flags */
    int8_t thickness;       /* /4 */
    uint32_t flags;         /* the material's flags */
} XgFieldTri;

typedef struct {
    int16_t c[4][3]; /* corners xyz; the game tests xz, with the height of corner 0 */
} XgFieldTrigger;

/* Where the scripts send the player: another field or the world map
 * (xgFieldFindExits). */
typedef struct {
    int actor;   /* the actor whose script holds it */
    int routine; /* script slot, -1 not known */
    int field;   /* target field; 0x1000 + n: the world map, place n */
    int entry;   /* its entry point (field variable 2; the world map's start position) */
    int trigger; /* the trigger zone that leads there, -1 (an actor's touch) */
    int op;      /* 1 field, 2 world map */
} XgFieldExit;

typedef struct {
    int id;
    int startProgress, startEntry; /* scalar start selectors, including a paused-game start */
    int actors;  /* script actors */
    int objects; /* every entity (actors first) */
    XgFieldActor* actor;
    int layers;
    int tris[4];
    XgFieldTri* walk[4];
    int materials;
    int triggers;
    XgFieldTrigger* trigger;
    int exits;
    XgFieldExit* exit;
    int models;
    int eye[3], at[3]; /* the game camera after the start (world units) */
    int scrz;          /* its projection distance (320 x 224 screen) */
    int min[3], max[3]; /* the walkmesh's and entities' bounds */
    int music;         /* the music the field asked for (-1: none) */
    int mechas;        /* gears placed */
    int hostReads;     /* files read through the host-file path */
    char note[160];    /* how the load went */
} XgFieldInfo;

typedef struct XgField XgField;

/* The paused game's state (outside any sandbox: the game's own gameState),
 * for XgFieldStart.gameState; its size. 1 when copied. */
int xgFieldGameStateSize(void);
int xgFieldGameState(void* out, int size);

/* Load field `id` (0..729) from disc 1's image. NULL with the reason in why. */
XgField* xgFieldLoad(XgDisc* disc, int id, const XgFieldStart* start, char* why, int whySize);
void xgFieldDestroy(XgField* f);

/* The exits: what the scripts do with the player in each trigger zone and
 * at each actor (mapsFieldProbe: a few field updates from a fresh start
 * each). Fills info's exits (field 0x1000 + n: the world map's place n);
 * once per field. The view's world starts again (sprites from frame 0). */
int xgFieldFindExits(XgField* f);
/* Actual table entries read by this sandbox, for export provenance. */
int xgFieldSources(const XgField* f, int* entries, int capacity);
const XgFieldInfo* xgFieldInfo(const XgField* f);
typedef struct { int pc, slot, routine; int slotPC[8], slotRoutine[8], priority[8], delay[8]; } XgFieldVM;
int xgFieldVM(XgField* f, int actor, XgFieldVM* out, int vars[1024]);
/* Only at the Extras pause, outside any sandbox. Returns field/actor count. */
int xgFieldPausedVM(int actor, int* field, int* actors, XgFieldVM* out, int vars[1024]);

/* Layers to draw. */
#define XG_FIELD_MODELS 0x01
#define XG_FIELD_SPRITES 0x02
#define XG_FIELD_WALKMESH 0x04
#define XG_FIELD_TRIGGERS 0x08
#define XG_FIELD_MARKERS 0x10    /* actors' places */
#define XG_FIELD_BACKGROUND 0x20 /* the background picture, gears */
#define XG_FIELD_EXITS 0x40      /* triggers that lead to an exit, highlighted */
#define XG_FIELD_FILLED 0x80     /* walkmesh triangles filled (else edges) */
#define XG_FIELD_ANIMATE 0x100   /* the sprites step their animation (one field frame) */

typedef struct {
    float eye[3]; /* world units */
    float at[3];
    float fov;    /* vertical field of view, degrees (0: the game's) */
} XgFieldCamera;

/* One frame of the field (sprites step one animation frame when `animate`)
 * seen by the game's camera on its 320 x 224 screen, or by `cam` on a w x h
 * screen (w, h up to 1024). `selected`: an actor to mark (-1). The
 * primitives are then the GPU's; returns their count. */
int xgFieldDrawGame(XgField* f, unsigned layers, int animate, int selected);
int xgFieldDrawFree(XgField* f, const XgFieldCamera* cam, int w, int h, unsigned layers, int animate, int selected);
XgVgpu* xgFieldGpu(XgField* f);

/* A world point on the last drawn screen (x, y; 0 behind the camera). */
int xgFieldProject(XgField* f, const float p[3], float* x, float* y);

/* The field as a glTF binary (mapscene.c): what: XG_FIELD_MODELS (the
 * models the field draws, textured from its VRAM), XG_FIELD_WALKMESH,
 * XG_FIELD_TRIGGERS, XG_FIELD_MARKERS (an empty node per script actor);
 * unit: glTF units per field unit (0: 1/512). 1 if written, else the reason
 * in why. */
int xgFieldExportGlb(XgField* f, const char* path, unsigned what, float unit, char* why, int whySize);

/* The field's sandbox (mapscene.c and tests): inside and out, and its VRAM. */
void xgFieldEnter(XgField* f);
void xgFieldLeave(XgField* f);
const uint16_t* xgFieldVram(XgField* f);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_MAPFIELD_H */
