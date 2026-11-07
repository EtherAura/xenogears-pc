#ifndef PORT_GAME_FIELD_MAPSVIEW_H
#define PORT_GAME_FIELD_MAPSVIEW_H

/*
 * The map viewer's way into the field overlay (mapsview.c, PC only): code
 * compiled into the field module, so it reaches the overlay's own state, that
 * port/src/extras/mapfield.c calls inside an extras sandbox (sandbox.h) whose
 * world holds the field and field_mecha modules. Never called by the game.
 */

#include "common.h"
#include "psyq.h"

/* The field mode's start up to its first frame (the kernel's game start,
 * the sound driver's set-up included, then fieldEntryPoint and bootField
 * without their waits, music, screen captures and transitions). The caller
 * has entered a world of its own (port/src/system/world.h) for the sound
 * driver's events, root counter and SPU. state: a GameState to start from (NULL: the
 * disc's initial one); storyProgress, entry: field variables 0 and 2 (-1: as
 * the state has them). 1 when the field set up. */
s32 mapsFieldStart(s32 field, const void* state, s32 storyProgress, s32 entry);

/* The game camera: the camera half of updateAllEntities run `frames` times
 * (the scripts' camera settles), then kept. */
void mapsFieldSettleCamera(s32 frames);
/* The kept game camera put back; or another camera: its rotation (rows:
 * right, down, forward, as lookAtDivided makes them) and eye (world units),
 * made into the projection the field's renderers use (the field's world
 * scale, m244, as setProjectionMatrixForField applies it), the translation
 * kept in 32 bits (the game's own path takes it through 16-bit GTE vectors:
 * an eye past 10922 units from the origin would wrap). `tan` is the
 * camera's heading (sprites face it); eye and target (16.16) are kept for
 * the background picture. */
void mapsFieldGameCamera(void);
void mapsFieldFreeCamera(MATRIX* rotation, s32 eyeX, s32 eyeY, s32 eyeZ, VECTOR* eye, VECTOR* at, s32 tan);

/* One frame of drawing (renderObjects, renderChars, the background picture
 * and the gears, the sprites' VRAM, the OTs linked and DrawOTag) on a w x h
 * screen with projection distance H, the depth shift for the OT, and the OT
 * entry the drawing starts from (the ground OT linked there; what is beyond
 * it is not drawn): -1 the field's own (linkOTIndex, 0x720: the game's far
 * clip), 0xFFF everything. layers:
 * bit 0 models, 1 sprites, 5 background and gears, 8 the sprites step
 * their animation (mapfield.h). */
void mapsFieldDraw(u32 layers, s32 w, s32 h, s32 H, s32 depthShift, s32 farIndex);

/* The camera drawn with (the projection matrix the field's renderers use). */
void mapsFieldProjection(MATRIX* out);

/* What the viewer shows. */
typedef struct {
    s32 objects;    /* entities */
    s32 actors;     /* script actors (entities 0..actors-1) */
    s32 layers;     /* walkmesh layers */
    s32 tris[4];    /* triangles per layer */
    u8* triangles[4];  /* 14-byte triangles (sWalkMeshTriangle) */
    SVECTOR* vertices[4];
    s32* materialFlags;
    u8* walkMesh;   /* the walkmesh section as decompressed */
    u8* triggers;   /* 0x18-byte zones */
    s32 eye[3], at[3]; /* the camera (world units) */
    s32 scrz;
    s32 mechas;
    s32 models;     /* models in section 2 */
    s32 player;     /* the actor the player controls */
    s32 music;      /* the music requested by the setup scripts, -1 none */
    u8* script;     /* the script bytecode (pCurrentFieldScriptFile) */
    u8* scriptHeader; /* the script file: 0x80 sign bits, actor count, 0x40-byte entry tables */
} MapsFieldView;
void mapsFieldView(MapsFieldView* out);

typedef struct {
    s32 flags, script, hidden, sprite;
    s32 pos[3], rot[3]; /* a script actor's script position, else its matrix's (16-bit, as the GTE takes it) */
    s32 direction, walkmesh, triangle, character;
    s32 model, npc; /* section 2 model or section 3 sprite bundle, -1 none */
    s32 pc, slot, priority; /* its script's position, the slot running, that slot's priority */
} MapsFieldActor;
void mapsFieldActor(s32 index, MapsFieldActor* out);
/* Read-only VM snapshot: variable indices are byte addresses (even 000-7FE). */
typedef struct { s32 pc, slot, routine; s32 slotPC[8], slotRoutine[8], priority[8], delay[8]; } MapsFieldVM;
s32 mapsFieldVM(s32 actor, MapsFieldVM* out, s32* vars);

/* The models the field draws (renderObjects' entities: placed room models
 * and script actors with a model): the native block, its primitive buffer
 * (as initModel2 set it up and lit it), and where the field puts it. */
typedef struct {
    void* block;  /* ModelBlock */
    u8* prims;
    MATRIX m;     /* rotation (4.12) and translation (16-bit, as the GTE takes it) */
    s32 entity, hidden, flags;
} MapsFieldModel;
s32 mapsFieldModels(MapsFieldModel* out, s32 max);

/* What the scripts do with the player at (x, y, z) (on its walkmesh where
 * one is under it): up to `frames` field updates (updateAllEntities: the
 * actors' scripts and movement, as the field's main loop runs them). 1: they
 * asked for another field (*field, *entry: field variable 2), 2: for the
 * world map (*field: the place, *entry: its start position), 0: nothing.
 * The world has run on: start it again before drawing. */
s32 mapsFieldProbe(s32 x, s32 y, s32 z, s32 frames, s32* field, s32* entry);

#endif /* PORT_GAME_FIELD_MAPSVIEW_H */
