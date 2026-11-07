#ifndef PORT_EXTRAS_MODELS_H
#define PORT_EXTRAS_MODELS_H

/*
 * The model and animation viewers' game side (models.c): a scene of models
 * set up, animated and drawn by the game's own code in a sandbox (sandbox.h),
 * and its export to glTF (models_export.c).
 *
 * Two kinds of model, each through the code the game uses for it:
 *   - "Mechas": Gears, enemies and arenas. A model bundle [textures, model
 *     archive, bone links, gear info (, arena bone placements)] and, for the
 *     animated ones, an animation file [animation, sound]. The Gear overlay
 *     (field_mecha, 10/0C) does everything: mechaInitNewMecha uploads the
 *     textures, relocates the model, builds the skeleton from the bone links
 *     and sets up the primitives; mechaPlayAnimation starts an animation
 *     program; renderMechas advances the animation VM one game tick and
 *     draws every bone's part where the skeleton puts it, lit in real time
 *     as the field and the gear shop light their Gears.
 *   - Model archives without a skeleton (field models, world landmarks):
 *     main's model code (fieldModelRelocation, initModel1/2,
 *     submitModelForRendering), as modelprev.c.
 *
 * A scene is a recipe (textures, models), kept as copies of the data:
 * xgSceneBuild makes a fresh sandbox world and runs it. Playing an animation
 * builds the scene again and starts the program, so frame n of an animation
 * is always the same picture (scrubbing replays the ticks).
 *
 * The game tick: the Gear VM advances one step per renderMechas(.., 1) call,
 * which the field makes once a field frame: every 2 vblanks at the field's
 * normal rate (the PS1's non-interlaced NTSC vblank, 59.826 Hz, / 2; op D6's
 * slower field rates slow the Gears down with it); the battle's copy steps
 * once per two vblanks whatever its frame rate (tables.h xgGearStepRate has
 * both). XG_MECHA_TICK_HZ is the field's.
 *
 * The camera: the view's GTE projection, as the game's own (SetGeomOffset,
 * SetGeomScreen) on a virtual screen of at most 1024 x 1024 (the GTE's
 * screen coordinates saturate at 1023; the render loops cull what is left of
 * or above 0), so zooming changes the projection, never the picture's
 * resolution.
 */

#include <stdint.h>

#include "vgpu.h"
#include "models_types.h"
#include "system/hw.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XG_MECHA_TICK_HZ (HW_VIDEO_HZ_NTSC / (HW_LINE_CLOCKS_NTSC * HW_FIELD_LINES_NTSC) / 2)

typedef struct XgScene XgScene;

XgScene* xgSceneCreate(void);
void xgSceneDestroy(XgScene* s);
void xgSceneClear(XgScene* s); /* no steps, nothing built */

/* ---- the recipe (each step keeps a copy of its data) ----------------------------------------- */

/* Packed VRAM uploads, each record at its own place (uploadTextureToVram). */
void xgSceneAddPackedTextures(XgScene* s, const uint8_t* data, int size);
/* A field's texture file (04/B9+2n: sector-aligned uploads), through the
 * file loader's own sector handler (filesystem.c processImageFileSectorPc,
 * the path the field's loadImageFileToVram takes from a PC). */
void xgSceneAddFieldTextures(XgScene* s, const uint8_t* data, int size);
/* TIM pictures back to back (a field's section 0 members), each LoadImage'd
 * at its own place, as the field's uploadFieldImages does. */
void xgSceneAddTims(XgScene* s, const uint8_t* data, int size);
/* An offset bundle of TIMs ([count][offsets]: a field's section 0, the world
 * map's texture files), each member's TIMs as xgSceneAddTims. */
void xgSceneAddTimBundle(XgScene* s, const uint8_t* data, int size);

/* A mecha: model bundle and animation file (NULL: none). */
enum {
    XG_MECHA_ANIMATED = 0, /* a field Gear: the Gear overlay's loader, VM and renderer */
    XG_MECHA_ARENA = 1,    /* an arena: no animation; bone placements from bundle member 4 (battle loader) */
    XG_MECHA_BATTLE = 2,   /* a battle Gear or an enemy: the battle's copies of them (a scene holds one kind) */
};
void xgSceneAddMecha(XgScene* s, const uint8_t* model, int modelSize, const uint8_t* anim, int animSize, int how);

/* A model archive: block `block`, or every block (-1); initMode for
 * initModel2 (0: the archive's own colours, 1: lit once), renderMode for
 * submitModelForRendering. */
void xgSceneAddModel(XgScene* s, const uint8_t* data, int size, int block, int initMode, int renderMode);
/* A BattlingDataFile (directory 31), through Battling's skeleton/clip code. */
void xgSceneAddBattling(XgScene* s, const uint8_t* data, int size);
int xgSceneBattling(const XgScene* s);
void xgSceneBattlingTiming(XgScene* s, int animation, XgBattlingTiming* timing);
/* Repeat a Battling clip without rebuilding its final pose or native blend. */
void xgSceneRepeatBattling(XgScene* s);
/* Embedded field MIMe targets, driven by the game's morph decoder. Field
 * scripts supply their weights; these are target controls, not clip names. */
int xgSceneMorphCount(const XgScene* s);
int xgSceneMorphWeight(XgScene* s, int target);
void xgSceneSetMorph(XgScene* s, int target, int weight, int ticks);
/* Owned loading steps and current morph weights in a new, unbuilt sandbox.
 * Used for exports so the live scene and its pending transitions stay intact. */
XgScene* xgSceneCopy(const XgScene* s);
/* Inside the scene's sandbox: target IDs of this part, and original/full
 * target positions. target=-1 means base; targets are local to the part.
 * Morphs returns -1 when the target mapping cannot be exported. */
int xgScenePartMorphs(XgScene* s, void* block, int* targets, int max);
int xgScenePartMorphPositions(XgScene* s, void* block, int target, float* xyz, int vertices);
/* Inside: current native positions for pose diagnostics; NULL queries count. */
int xgScenePartVertices(void* block, float* xyz, int max);

/* Build: a fresh sandbox world, the steps in order, the first pose. 1 if it
 * worked, else 0 with the reason in why. */
int xgSceneBuild(XgScene* s, char* why, int whySize);
int xgSceneBuilt(const XgScene* s);

/* ---- what is in it ---------------------------------------------------------------------------- */

typedef struct {
    int parts;      /* model blocks drawn */
    int polygons;
    int bones;      /* the first mecha's bones (root included); 0: none */
    int animations; /* the first mecha's animation programs */
    float min[3], max[3]; /* bounds of the vertices where the current pose puts them (model units) */
} XgSceneInfo;
void xgSceneInfo(XgScene* s, XgSceneInfo* out);

/* ---- animation (the first mecha) --------------------------------------------------------------- */

/* Build again and start program `anim` (mechaPlayAnimation): frame 0. */
int xgScenePlay(XgScene* s, int anim, char* why, int whySize);
int xgSceneAnimation(const XgScene* s); /* the program playing, -1 none */
/* Advance `ticks` game ticks (renderMechas, nothing kept of the drawing). */
void xgSceneTick(XgScene* s, int ticks);
int xgSceneFrame(const XgScene* s); /* ticks since the program started */
/* To frame n of the program playing: from here if n is ahead, else built
 * again and replayed. */
void xgSceneSeek(XgScene* s, int frame);

/* Program `anim`'s bytecode words (to the next program in the file), at
 * most max; their count. */
int xgSceneProgram(XgScene* s, int anim, uint16_t* out, int max);

/* The battle's copy of the VM: the extra animation file program `anim`
 * loads first (op 0x04; a file of directory 0x2A), -1 if none; that file's
 * bytes for the next xgScenePlay (which gives it to the mecha as ops 0x04 and
 * 0x05 would, without the CD). */
int xgSceneProgramExtraFile(XgScene* s, int anim);
void xgSceneSetExtraFile(XgScene* s, const uint8_t* data, int size);
int xgSceneBattle(const XgScene* s); /* the battle's copies run the scene */

/* The VM's state (the first mecha). */

void xgSceneVm(XgScene* s, XgVmState* out);

/* ---- drawing ----------------------------------------------------------------------------------- */

typedef struct {
    int yaw, pitch;   /* 4096 = 360 degrees */
    float distance;   /* from the target, model units */
    float target[3];  /* the world point at the screen's centre */
    float zoom;       /* the projection plane: zoom * h */
    int w, h;         /* the virtual screen (each at most 1024) */
    int lightFollows; /* the light turns with the view (else fixed in the world, as the game's) */
} XgCamera;

/* Draw the current pose (no tick) into the scene's GPU; returns the
 * primitives drawn. The GPU's primitives are then the picture (vgpu.h). */
int xgSceneDraw(XgScene* s, const XgCamera* c);
XgVgpu* xgSceneGpu(XgScene* s);
/* Enable native, bounded trail events before building an effects scene. */
void xgSceneSetTrails(XgScene* s, int on);
int xgSceneTrailSegments(XgScene* s);
/* Hide the Gears' ground shadow (its texture is the field's, not loaded). */
void xgSceneSetShadow(XgScene* s, int on);

/* ---- texture pages ----------------------------------------------------------------------------- */

typedef struct {
    uint16_t tpage; /* page x / 64 bits 0-3, y / 256 bit 4, blend 5-6, depth 7-8 */
    uint16_t clut;  /* the CLUT word (0 for 15-bit pages) */
    int semi;       /* any polygon on it semi-transparent */
    int polygons;
    int u0, v0, u1, v1; /* the texels its polygons use */
} XgScenePage;
/* The (page, CLUT) pairs the scene's textured polygons use. */
int xgScenePages(XgScene* s, XgScenePage* out, int max);
/* A page through its CLUT as 256 x 256 RGBA (texel 0 clear; semi: STP
 * texels at half alpha). */
void xgSceneBakePage(XgScene* s, uint16_t tpage, uint16_t clut, int semi, uint32_t* rgba);

/* ---- export (models_export.c) ------------------------------------------------------------------ */

typedef struct {
    float unit;       /* glTF units per model unit (default 1/1024) */
    const int* animations; /* the programs to export (each played from a fresh build) */
    int animationCount;
    const int* morphs; /* global MIMe targets to export as out-and-back clips */
    int morphCount;
    int morphTicks;   /* ticks in each direction (default 30) */
    int maxTicks;     /* per animation: until it stops, or this many ticks */
    double tickRate;  /* game ticks a second (0: XG_MECHA_TICK_HZ) */
    const char* name; /* the root node's name */
    /* Called before each program plays (from a fresh build): what the scene
     * needs from the disc first (the battle's extra animation files). */
    void (*beforePlay)(XgScene* s, int animation, void* user);
    void* user;
} XgExportOptions;
/* A glTF binary of the scene: all rigs, including sampled spawned lifetimes,
 * with bone TRS and independent mesh visibility; shared model-block meshes
 * (a primitive per material), textures baked from the sandbox's
 * VRAM, MIMe position targets with current default weights, and animation
 * sampled at every game tick. The source scene is untouched. 1 if written. */
int xgSceneExportGlb(XgScene* s, const char* path, const XgExportOptions* o, char* why, int whySize);

/* ---- for models_export.c ----------------------------------------------------------------------- */

/* One drawn part: its block (native ModelBlock), its primitive buffer, its
 * placement (3 x 3 rotation-scale rows and translation, model units), its
 * bone (-1: none) and that bone's parent (-1: the root). */

int xgSceneParts(XgScene* s, XgScenePart* out, int max);
/* Currently loaded field gears, as posed by renderMechasInField. Called
 * while a caller's field/field_mecha sandbox is entered; no world swap. */
int xgModelsFieldMechaParts(XgScenePart* out, int max);

/* The first mecha's bones, local to their parents (bone 0: the root, in the
 * world): rotation-scale rows and translation; visible. Returns the count. */

int xgSceneBones(XgScene* s, XgSceneBone* out, int max);
/* Every current rig, including split-off battle slots. Rigs refreshes the
 * pose without ticking; -1 if max is too small. Bones validates the lifetime
 * and copies local matrices (0 if freed/replaced, -1 if max is too small). */
int xgSceneRigs(XgScene* s, XgSceneRig* out, int max);
int xgSceneRigBones(XgScene* s, const XgSceneRig* rig, XgSceneBone* out, int max);

/* Inside/outside the scene's sandbox (for reading the native blocks). */
void xgSceneEnter(XgScene* s);
void xgSceneLeave(XgScene* s);

/* Inside an owned battle sandbox: native spell rigs and their extra clips.
 * Uses the same bounded VM/event preparation as the model viewer. */
void* xgModelsBattleEffectLoad(int slot, const uint8_t* model, int modelSize, const uint8_t* anim, int animSize);
int xgModelsBattleEffectExtra(int slot, const uint8_t* extra, int size);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_MODELS_H */
