#include "raster.h"
/*
 * A sprite actor shown by the game's own sprite code, in a sandbox
 * (sprites_game.h).
 */

#include "sprites_game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "main/sprite.h"
#include "main/task.h"
#include "psyq.h"
#include "sandbox.h"
#include "battlefx_decode.h"
#include "models.h"
#include "field_mecha/mecha.h"

/* ---- the game code it runs ---------------------------------------------------------------- */

/* src/main/sprite_actor.c */
SpriteActor* createSpriteActor(AnimationBundle* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg5);
SpriteActor* createSpriteActorEX(AnimationBundle* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg5,
                                 s32 vramParam);
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animationId);
void setAnimationBundle(SpriteActorCore* actor, AnimationBundle* bundle);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
void setSpriteActorStaticFrame(SpriteActorCore* actor, s32 frame, s32 flipX, s32 x4);
void setGraphicEntityScale(SpriteActorCore* actor, s32 scale);
void spriteActorSetColor(SpriteActorCore* actor, s32 r, s32 g, s32 b);
void OP_INIT_ENTITY_SCRIPT_sub0Sub3(SpriteActorCore* actor, s32 count);
void OP_INIT_ENTITY_SCRIPT_sub0Sub5(SpriteActorCore* actor, s32 value);
void OP_INIT_ENTITY_SCRIPT_sub0Sub9(SpriteActorCore* actor);
void updateSpriteMovement(SpriteActorCore* actor);
void initFieldEntitySub4Sub5Sub0(SpriteVramData* vram, AnimationBundle* bundle, Vec2s16 vramLocation, Vec2s16 clut,
                                 s32 unused);
void savePointCallbackC_sub1(SpriteActorCore* actor);
/* src/main/sprite.c */
void uploadCharacterSprites(void);
void renderSpriteActor(SpriteActorCore* actor, OTag* ot);
extern SpriteActorCore* spriteTransfertListHead;
extern u8* shapeTransfertTableCurrentEntry;
/* src/main/sprite_bytecode.c: each opcode's length, operands included */
extern u8 sizePerBytecodeTable[0x80];
/* src/main/shape_transfer.c */
void setCharacterRenderingOT(OTag* ot);
void setCurrentRenderingMatrix(MATRIX* m);
void shapeTransfert(void);
void clearShapeTransfertTableEntry(s32 index);
/* src/main/task.c */
void execSpritesCallbacksList1(void);
void execSpritesCallbacksList2(void);
TaskHeader* allocateTaskPair(s32, TaskHeader*, void (*)(TaskHeader*), void (*)(TaskHeader*), void (*)(TaskHeader*));
void battleFxViewCamera(MATRIX* view, int scene);
void battleFxViewStart(s32 enabled, s32 models);
void battleFxViewModels(int (*command)(void*, int, int));
void battleFxViewRenderModels(MATRIX*, OTag*, int, int);
void mechaPlayAnimation_battle(u16, u16, s32);
void* modelsBattleMecha(s32);
void freeMecha_battle(s32);
int modelsViewSoundStubs(void);
s32 battleSpriteViewTerrain(SpriteActorCore* actor, SpriteActorCore* target);
s32 battleFxViewCommands(void);
s32 battleFxViewWobbles(void);
void battleFxViewCapture(void (*capture)(void*), void* data);
void battleFxViewArguments(int (*bounds)(void*, const void*, int));
void battleFxViewBackground(uint8_t* rgb);
void battleFxViewContext(SpriteActorCore*, SpriteActorCore*, OTag*, int);
int battleFxViewSceneVisible(void);
int battleFxScreenViewActive(void);
void battleFxScreenViewStep(OTag*, int, int);
int battleFxScreenViewState(void);
extern s32 gDepthDivider;
extern s32 allocateSavePointMeshDataSub0_var1; /* update tasks alive (effect sprites) */
extern TaskHeader* spriteCallback2Var2;
void savePointCallback8(TaskHeader*);
void savePointCallback8Counted(TaskHeader*);
void battleSpriteEffect_20_update(TaskHeader*);
SpriteActorCore* spriteBytecode2ExtendedE0(SpriteActorCore*, u16*, SpriteVramData*);
void allocateSavePointMeshDataSub0_callback(TaskHeader*);
void registerSpriteCallback2Sub0(TaskHeader*);
/* src/main/filesystem.c: the sector-aligned VRAM upload files' decoder (the
 * game's own, on its development-host path: no CD involved) and the
 * streaming state it reads */
s32* allocateBufferForVramUpload(s32 count, s32 flags);
void processImageFileSectorPc(s32 arg0, s32 arg1);
extern u8* readDestination;
extern s32 streamSectorSlotCount;
extern s32 readInProgress;
extern s32 remainingReadSize;
extern s32 readCursorIndex;
extern u16 streamNextReadSequence;
extern u16 streamNextDataSequence;
/* src/main/model.c */
s32 uploadTextureToVram(s32* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY);
/* the sprite code's globals */
extern u8 isBattleOverlayLoaded;
extern u8 isWorldMapOverlayLoaded;
extern s32 fieldDrawEnvsInitialized;
extern s32 battleDefaultEntityScale;
extern SpriteVramData createSavePointMeshData_mode5;
extern SpriteVramData createSavePointMeshData_mode6; /* the battle's effect sprites (op 0xBD) */

/* ---- the view ------------------------------------------------------------------------------- */

/* The camera: face on, the projection plane at half its distance, so the
 * GTE divides by exactly 2 (H / SZ = 0x8000): a field sprite's pixel (6
 * world units) is 3 screen units, a battle sprite's (2 units) one. */
#define VIEW_H 512
#define VIEW_DIST 1024
#define OT_SIZE 0x1000 /* the field's and the battle's (spriteCallback_render0 tests 1..0xFFF) */
#define FX_OT_SIZE 0x4000 /* native animated mecha depth range */
#define SHAPE_ARENA 0x3C00 /* the field's allocateShapeTransfert */
#define FIELD_SCALE 0xC00  /* resetFieldScriptEntityGraphicEntity: m2C_scale; the matrix: scale3d * 3 / 4 */
#define BATTLE_SCALE 0x2000 /* allocateBattleSpriteActor */
#define HEAP_ROOM (512 << 10) /* the heap a drawn frame must have free (the arena is 8 MB) */

struct XgSprite {
    XgSandbox* sb;
    XgSpriteSetup setup;
    uint8_t* bundle; /* copies of the caller's data, padded */
    int bundleSize;
    uint8_t* companion;
    int companionSize;
    uint8_t* effects;
    int effectsSize;
    uint8_t* fragments;
    int fragmentsSize;
    uint8_t* casterBundle;
    int casterSize;
    XgSpriteModel* models;
    int pendingModel, loadedModel, loadedExtra;
    int anims, frames, palettes, colours, prebaked;
    int animsAt, framesAt, clutAt;
    uint8_t* animFlags;
    int16_t* animSkipped; /* 4 per animation: opcodes stepped over, -1 */
    uint8_t* patch[4];    /* selected bundle, effects, fragments, companion */
    /* the world */
    OTag* ot;
    SpriteActorCore* actor;
    SpriteActorCore* target;
    SpriteActorCore* caster;
    uint32_t* screen;
    uint16_t* initialVram;
    int showScene;
    int capture;
    TaskHeader* rootTask;
    u8* bundleInHeap;
    int parity;
    int alive; /* effect sprites (update tasks) after the last frame drawn */
    int heapFull; /* a frame was not run: the sandbox's heap is nearly full */
    int haveBounds, bx0, by0, bx1, by1;
    int actorBounds, actorX0, actorY0, actorX1, actorY1;
};

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int rd16(const uint8_t* p) {
    return p[0] | p[1] << 8;
}

static int rds16(const uint8_t* p) {
    return (int16_t)(p[0] | p[1] << 8);
}

/* ---- walking the bytecode ------------------------------------------------------------------ */

/* Opcodes that need the rest of a battle around the actor (its effects,
 * camera, faders, damage numbers, target lists, terrain, pad) or attach 3D
 * models (0xF7 reads a pointer the game never sets). In the viewer's copy of
 * the bytecode they become opcodes of the same length that no VM handles
 * (0x83, 0xB1, 0xF0, 0xF4: the field's and the battle's VM and
 * executeSpriteBytecode step over them), so the actor's own frames, waits
 * and moves play as the game plays them, without the battle's part. */
static int battleOnlyOp(int op, int battle, const uint8_t* operands, int dynamic) {
    switch (op) {
    case 0xF5: case 0xF6: case 0xF7:
        return 1;
    case 0xBC: {
        /* Places taken from the battle's acting sprite
         * (processBattleAnimationSub0_var1, read without a check: 1, 3,
         * 25-31) or its sprite list (2, 4: the paused game's on the field). */
        int type = operands[0], idx = type & 0x3F;
        return dynamic || ((type & 0x80) && (idx == 1 || idx == 3 || (idx >= 25 && idx <= 31) ||
                                             (!battle && (idx == 2 || idx == 4))));
    }
    case 0xE8: case 0xCA: case 0xCB: case 0xFB: case 0xC3: case 0xEC: case 0xF9: case 0x8B: case 0xF8:
    case 0xF3: case 0x9E: case 0x88: case 0x89: case 0xC2:
        return battle;
    default:
        return 0;
    }
}

static const uint8_t noOp[5] = {0, 0x83, 0xB1, 0xF0, 0xF4}; /* by length */

typedef struct {
    const uint8_t* data;
    int size;
    uint8_t* seen;  /* a bit per byte: walked */
    uint8_t* patch; /* a bit per byte: an opcode to step over (kept over every animation) */
} Region;

typedef struct {
    Region r[4]; /* selected bundle, effects, fragments, companion or preview actors */
    int battle;
    int party;
    int effectsAnims;
    int nativeEffects;
    int flags;
    int skipped[4]; /* the first different opcodes stepped over */
    int todo[512][2];
    int todoCount;
} Walk;

static void walkPatch(Walk* w, int region, int pc, int op) {
    Region* r = &w->r[region];
    r->patch[pc >> 3] |= (uint8_t)(1 << (pc & 7));
    w->flags |= XG_SPRITE_ANIM_SKIPS;
    for (int i = 0; i < 4; i++) {
        if (w->skipped[i] == op) {
            break;
        }
        if (w->skipped[i] < 0) {
            w->skipped[i] = op;
            break;
        }
    }
}

static void walkPush(Walk* w, int region, int at) {
    if (at < 0 || at >= w->r[region].size) {
        w->flags |= XG_SPRITE_ANIM_UNSAFE; /* code out of the bundle: do not run it */
        return;
    }
    if (w->r[region].seen[at >> 3] & (1 << (at & 7))) {
        return;
    }
    if (w->todoCount < 512) {
        w->todo[w->todoCount][0] = region;
        w->todo[w->todoCount][1] = at;
        w->todoCount++;
    } else {
        w->flags |= XG_SPRITE_ANIM_UNSAFE;
    }
}

/* An animation header at `at` (u16 flags, u16 bytecode offset from +2,
 * frame table offsets): its bytecode (setCurrentAnimationPtr). One spawned
 * by the opcode at spawnPc also says what it is made as
 * (getSavePointCreationMode1: bits 8-10, plus 8 for bit 14): 5 takes the
 * battle's effect sprites' frames, 6 a set only the battle's Gears load
 * (createSavePointMeshData_mode6), 10-13 follow the battle's camera
 * (battleCameraControlSpriteInit, the battle's): such a spawn is stepped
 * over where the viewer cannot make it. */
static void walkAnimation(Walk* w, int region, int at, int spawnRegion, int spawnPc) {
    const Region* r = &w->r[region];
    if (at < 0 || at + 4 > r->size) {
        w->flags |= XG_SPRITE_ANIM_UNSAFE;
        return;
    }
    if (spawnPc >= 0) {
        int h = rd16(r->data + at);
        int mode = ((h >> 8) & 7) + ((h >> 14) & 1) * 8;
        if ((mode == 6 && w->r[2].data == NULL && !w->nativeEffects) || (mode == 5 && w->r[1].data == NULL) || (mode >= 10 && mode <= 13 && !w->battle)) {
            walkPatch(w, spawnRegion, spawnPc, w->r[spawnRegion].data[spawnPc]);
            return;
        }
    }
    walkPush(w, region, at + 2 + rd16(r->data + at + 2));
}

/* Every opcode an animation can reach, following both ways of each branch
 * and the animations it spawns (0xE0 from its own bytecode, 0xBD the
 * battle's effect sprites), as the VMs advance (sizePerBytecodeTable). */
static void walkRun(Walk* w) {
    while (w->todoCount > 0) {
        w->todoCount--;
        int region = w->todo[w->todoCount][0];
        int pc = w->todo[w->todoCount][1];
        Region* r = &w->r[region];
        for (;;) {
            if (pc < 0 || pc >= r->size) {
                w->flags |= XG_SPRITE_ANIM_UNSAFE;
                break;
            }
            if (r->seen[pc >> 3] & (1 << (pc & 7))) {
                break;
            }
            r->seen[pc >> 3] |= (uint8_t)(1 << (pc & 7));
            int op = r->data[pc];
            if (op < 0x80) {
                pc++;
                continue;
            }
            /* The battle VM reads BE's 16-bit frame/wait word, unlike the
             * field VM's two-byte form in sizePerBytecodeTable. */
            int size = op == 0xBE && w->battle ? 3 : sizePerBytecodeTable[op - 0x80];
            if (pc + size > r->size) {
                w->flags |= XG_SPRITE_ANIM_UNSAFE;
                break;
            }
            const uint8_t* p = r->data + pc + 1;
            int nativeFx = w->nativeEffects && (xgBattleFxCommandSafe(r->data, r->size, pc) ||
                                               xgBattleFxModelSafe(r->data, r->size, pc));
            nativeFx |= w->nativeEffects && (op == 0xC2 ||
                (op == 0xBC && (p[0] & 0x80) && ((p[0] & 63) == 1 || (p[0] & 63) == 3 ||
                                                ((p[0] & 63) >= 25 && (p[0] & 63) <= 31))));
            if (nativeFx && (op == 0xC3 || op == 0xEC || op == 0xF9 || op == 0xE8) &&
                (p[0] == 0x18 || p[0] == 0x54)) w->flags |= XG_SPRITE_ANIM_SCREEN;
            /* Party clips also own the native limb transforms, velocity,
             * parameter stack and terrain. Preserve their resets (08),
             * reversal (09), damping (17) and floor sample (1F), as well as
             * bank switches (68/69) and target jumps (61/C2). Without 08 a
             * rotated limb carries into later frames of a combined attack. */
            nativeFx |= w->party && ((op == 0xC3 && (p[0] == 0x08 || p[0] == 0x09 || p[0] == 0x1F ||
                                                    p[0] == 0x68 || p[0] == 0x69)) ||
                                     (op == 0xEC && (p[0] == 0x17 || p[0] == 0x61)) ||
                                     (op == 0xE8 && p[0] == 0x17) || op == 0xC2);
            if ((!nativeFx && battleOnlyOp(op, w->battle, p, 0)) || (op == 0xC8 && battleOnlyOp(p[0], w->battle, p + 1, 1))) {
                walkPatch(w, region, pc, op == 0xC8 ? p[0] : op);
                pc += size; /* as the VM will: a plain step */
                continue;
            }
            int stop = 0;
            switch (op) {
            case 0x80: case 0x81: case 0x82: case 0x85: case 0x8E:
                stop = 1; /* the end, a restart of this animation, a return (followed at the call) */
                break;
            case 0x8F:
                stop = w->battle;
                break;
            case 0xE1: case 0xD4:
                walkPush(w, region, pc + rds16(p));
                stop = 1;
                break;
            case 0xE2: case 0xE4:
                walkPush(w, region, pc + rds16(p)); /* the call, the loop's jump; then on */
                break;
            case 0xFA:
                walkPush(w, region, pc + rds16(p + 1));
                break;
            case 0xFB:
                walkPush(w, region, pc + rds16(p));
                break;
            case 0xE0:
                w->flags |= XG_SPRITE_ANIM_EFFECTS;
                walkAnimation(w, region, pc + 1 + rds16(p), region, pc);
                break;
            case 0xE3:
                if (w->nativeEffects) walkAnimation(w, region, pc + rds16(p), region, -1);
                break;
            case 0xBD:
                if (w->r[1].data != NULL && p[0] < w->effectsAnims) {
                    w->flags |= XG_SPRITE_ANIM_EFFECTS;
                    int at = (int)rd32(w->r[1].data + 4);
                    walkAnimation(w, 1, at + rd16(w->r[1].data + at + 2 + 2 * p[0]), region, pc);
                } else {
                    walkPatch(w, region, pc, op); /* no effect sprites to spawn from */
                }
                break;
            case 0xA0: case 0xA1: case 0xA5: case 0xA6: case 0xA9: case 0xAA: case 0xAB:
            case 0xED: case 0xEE: case 0xEF:
                w->flags |= XG_SPRITE_ANIM_MOVES;
                break;
            case 0xC0: case 0xC1:
                w->flags |= XG_SPRITE_ANIM_MOVES | XG_SPRITE_ANIM_RANDOM;
                break;
            case 0xC4: case 0xAC: case 0xE5:
                w->flags |= XG_SPRITE_ANIM_RANDOM;
                break;
            case 0xC8:
                if (p[0] == 0xE5) {
                    w->flags |= XG_SPRITE_ANIM_RANDOM;
                }
                break;
            default:
                break;
            }
            if (stop) {
                break;
            }
            pc += size;
        }
    }
}

static int walkAnimations(XgSprite* s) {
    Walk* w = calloc(1, sizeof(Walk));
    s->animFlags = calloc((size_t)s->anims + 1, 1);
    s->animSkipped = calloc((size_t)s->anims * 4 + 4, sizeof(int16_t));
    s->patch[0] = calloc((size_t)s->bundleSize / 8 + 1, 1);
    if (s->effects != NULL) {
        s->patch[1] = calloc((size_t)s->effectsSize / 8 + 1, 1);
    }
    if (w == NULL || s->animFlags == NULL || s->animSkipped == NULL || s->patch[0] == NULL ||
        (s->effects != NULL && s->patch[1] == NULL)) {
        free(w);
        return 0;
    }
    w->battle = s->setup.battle;
    w->party = s->companion != NULL;
    w->nativeEffects = s->setup.nativeEffects;
    w->r[0].data = s->bundle;
    w->r[0].size = s->bundleSize;
    w->r[0].seen = malloc((size_t)s->bundleSize / 8 + 1);
    w->r[0].patch = s->patch[0];
    if (s->effects != NULL && s->effectsSize > 16) {
        w->r[1].data = s->effects;
        w->r[1].size = s->effectsSize;
        w->r[1].seen = malloc((size_t)s->effectsSize / 8 + 1);
        w->r[1].patch = s->patch[1];
        int at = (int)rd32(s->effects + 4);
        w->effectsAnims = at >= 0 && at + 2 <= s->effectsSize ? rd16(s->effects + at) & 0x3F : 0;
    }
    if (s->fragments != NULL) {
        s->patch[2] = calloc((size_t)s->fragmentsSize / 8 + 1, 1);
        w->r[2].data = s->fragments;
        w->r[2].size = s->fragmentsSize;
        w->r[2].patch = s->patch[2];
        w->r[2].seen = calloc((size_t)s->fragmentsSize / 8 + 1, 1);
        if (s->patch[2] == NULL || w->r[2].seen == NULL) {
            free(w->r[0].seen); free(w->r[1].seen); free(w->r[2].seen); free(w);
            return 0;
        }
    }
    const uint8_t* normal = s->companion ? s->companion : s->casterBundle;
    int normalSize = s->companion ? s->companionSize : s->casterSize;
    if (normal != NULL) {
        s->patch[3] = calloc((size_t)normalSize / 8 + 1, 1);
        w->r[3].data = normal;
        w->r[3].size = normalSize;
        w->r[3].patch = s->patch[3];
        w->r[3].seen = calloc((size_t)normalSize / 8 + 1, 1);
        if (s->patch[3] == NULL || w->r[3].seen == NULL) {
            for (int r = 0; r < 4; r++) free(w->r[r].seen);
            free(w);
            return 0;
        }
    }
    if (w->r[0].seen == NULL || (w->r[1].data != NULL && w->r[1].seen == NULL)) {
        free(w->r[0].seen); free(w->r[1].seen); free(w->r[2].seen); free(w->r[3].seen); free(w);
        return 0;
    }
    for (int a = 0; a < s->anims && w->r[0].seen != NULL && (w->r[1].data == NULL || w->r[1].seen != NULL); a++) {
        for (int r = 0; r < 4; r++) if (w->r[r].seen) memset(w->r[r].seen, 0, (size_t)w->r[r].size / 8 + 1);
        w->flags = 0;
        for (int i = 0; i < 4; i++) {
            w->skipped[i] = -1;
        }
        w->todoCount = 0;
        int at = s->animsAt + rd16(s->bundle + s->animsAt + 2 + 2 * a);
        walkAnimation(w, 0, at, 0, -1);
        walkRun(w);
        s->animFlags[a] = (uint8_t)w->flags;
        for (int i = 0; i < 4; i++) {
            s->animSkipped[a * 4 + i] = (int16_t)w->skipped[i];
        }
    }
    /* A spawned effect sprite that ends or restarts plays an animation of its
     * own set by number (0x80 its default, 0 here; 0x82 its own id): every
     * animation of the effect sprites is walked too, for their patches. */
    for (int a = 0; a < w->effectsAnims && w->r[1].seen != NULL; a++) {
        memset(w->r[0].seen, 0, (size_t)s->bundleSize / 8 + 1);
        memset(w->r[1].seen, 0, (size_t)s->effectsSize / 8 + 1);
        w->todoCount = 0;
        int at = (int)rd32(s->effects + 4);
        walkAnimation(w, 1, at + rd16(s->effects + at + 2 + 2 * a), 1, -1);
        walkRun(w);
    }
    for (int a = 0; w->r[2].data != NULL && a < (rd16(s->fragments + rd32(s->fragments + 4)) & 63); a++) {
        for (int r = 0; r < 4; r++) if (w->r[r].seen != NULL) memset(w->r[r].seen, 0, (size_t)w->r[r].size / 8 + 1);
        w->todoCount = 0;
        int at = (int)rd32(s->fragments + 4);
        walkAnimation(w, 2, at + rd16(s->fragments + at + 2 + 2 * a), 2, -1);
        walkRun(w);
    }
    for (int a = 0; normal != NULL && a < (rd16(normal + rd32(normal + 4)) & 63); a++) {
        for (int r = 0; r < 4; r++) if (w->r[r].seen != NULL) memset(w->r[r].seen, 0, (size_t)w->r[r].size / 8 + 1);
        w->todoCount = 0;
        int at = (int)rd32(normal + 4);
        walkAnimation(w, 3, at + rd16(normal + at + 2 + 2 * a), 3, -1);
        walkRun(w);
    }
    free(w->r[3].seen);
    free(w->r[2].seen);
    free(w->r[0].seen);
    free(w->r[1].seen);
    free(w);
    return 1;
}

/* The viewer's copy of a bytecode region with the battle's opcodes stepped
 * over (walkAnimations). */
static void applyPatches(u8* copy, const uint8_t* patch, int size) {
    for (int pc = 0; patch != NULL && pc < size; pc++) {
        if ((patch[pc >> 3] & (1 << (pc & 7))) && copy[pc] >= 0x80) {
            copy[pc] = noOp[sizePerBytecodeTable[copy[pc] - 0x80]];
        }
    }
}

/* ---- the world --------------------------------------------------------------------------------- */

/* The sector-aligned VRAM upload file (field textures, 04/B9+2n) through the
 * game's own decoder, a sector at a time as its development-host loader
 * feeds it (loadImageFileToVram's cdromFnd path: readImageFileSectorFromPc
 * then processImageFileSectorPc), its positions as stored (mode 0, the
 * field's: the decoder's own state is zero in a fresh world). */
static int uploadSectorFile(const uint8_t* file, int size) {
    /* The native decoder retains the header's row counts in a busy slot;
     * subsequent payload sectors must use a different slot. */
    enum { SLOTS = 4 };
    s32* buf = allocateBufferForVramUpload(SLOTS, 0);
    if (buf == NULL) {
        return 0;
    }
    readDestination = (u8*)buf + SLOTS * 8 + 0x24;
    streamSectorSlots = (SectorSlot*)(buf + 1);
    streamSectorSlotCount = SLOTS;
    memset(streamSectorSlots, 0, SLOTS * sizeof(*streamSectorSlots));
    readCursorIndex = 0;
    streamNextReadSequence = 0;
    streamNextDataSequence = 0;
    remainingReadSize = (size + 0x7FF) & ~0x7FF;
    readInProgress = 1;
    for (int at = 0; at < size && readInProgress > 0; at += 0x800) {
        int i = 0;
        while (i < SLOTS && streamSectorSlots[i].m0_state != 0) i++;
        if (i == SLOTS) break;
        int n = size - at < 0x800 ? size - at : 0x800;
        memset(readDestination + i * 0x800, 0, 0x800);
        memcpy(readDestination + i * 0x800, file + at, (size_t)n);
        streamSectorSlots[i].m0_state = 1;
        streamSectorSlots[i].m2_sequence = streamNextReadSequence++;
        remainingReadSize -= 0x800;
        processImageFileSectorPc(0, 0);
    }
    readInProgress = 0;
    remainingReadSize = 0;
    streamSectorSlots = NULL;
    streamSectorSlotCount = 0;
    readDestination = NULL;
    freeHeap(buf);
    return 1;
}

/* Field section 0: TIMs (uploadFieldImages: each TIM's CLUT and picture to
 * their own places). Read here, not through OpenTIM/ReadTIM, whose cursor
 * the sandbox does not keep apart from the game's. */
static void uploadTims(XgVgpu* gpu, const uint8_t* data, int size) {
    if (size < 8) {
        return;
    }
    int count = (int)rd32(data);
    for (int i = 0; i < count && 4 + 4 * i + 4 <= size; i++) {
        int at = (int)rd32(data + 4 + 4 * i);
        if (at < 0 || at + 8 > size || rd32(data + at) != 0x10) {
            continue;
        }
        uint32_t flags = rd32(data + at + 4);
        int p = at + 8;
        if (flags & 8) { /* CLUT block: length, x, y, w, h, colours */
            if (p + 12 > size) {
                continue;
            }
            int len = (int)rd32(data + p);
            int x = rd16(data + p + 4), y = rd16(data + p + 6), w = rd16(data + p + 8), h = rd16(data + p + 10);
            if (p + 12 + w * h * 2 <= size) {
                xgVgpuLoad(gpu, x, y, w, h, (const uint16_t*)(data + p + 12));
            }
            p += len;
        }
        if (p + 12 <= size) {
            int x = rd16(data + p + 4), y = rd16(data + p + 6), w = rd16(data + p + 8), h = rd16(data + p + 10);
            if (p + 12 + w * h * 2 <= size) {
                xgVgpuLoad(gpu, x, y, w, h, (const uint16_t*)(data + p + 12));
            }
        }
    }
}

/* What the field and the battle put in VRAM before their sprites are made. */
static int prepareVram(XgSandbox* sb, const XgSpriteSetup* st) {
    XgVgpu* gpu = xgSandboxGpu(sb);
    int ok = 1;
    xgSandboxEnter(sb);
    if (st->tims != NULL) {
        uploadTims(gpu, st->tims, st->timsSize); /* initFieldData: section 0 first */
    }
    if (st->sheets != NULL && st->sheetsSize >= 4) {
        /* initFieldData: section 4's sheet i to slot i, unless the texture
         * file holds that slot */
        u8* sheets = xgSandboxCopy(st->sheets, (size_t)st->sheetsSize);
        if (sheets == NULL) ok = 0;
        int count = sheets != NULL ? (int)rd32(sheets) : 0;
        for (int i = 0; i < count && i < st->slotCount && 4 + 4 * i + 4 <= st->sheetsSize; i++) {
            int at = (int)rd32(sheets + 4 + 4 * i);
            if (!st->slots[i].fromTextureFile && at > 0 && at < st->sheetsSize) {
                uploadNpcSpriteSheet((s32*)(sheets + at), st->slots[i].x, (s16)st->slots[i].y);
            }
        }
        if (sheets != NULL) {
            freeHeap(sheets);
        }
    }
    if (st->textureFile != NULL && !st->nativeEffects) {
        ok &= uploadSectorFile(st->textureFile, st->textureFileSize); /* loadFieldGraphics, after initFieldData */
    }
    if (st->effectsTexture != NULL) {
        /* mainBattleSpriteCallback_phase3: 2C/01 as packed uploads */
        s32* t = xgSandboxCopy(st->effectsTexture, (size_t)st->effectsTextureSize);
        if (t != NULL) {
            uploadTextureToVram(t, 0, 0, 0, 0, 0, 0);
            freeHeap(t);
        } else ok = 0;
    }
    if (st->textureFile != NULL && st->nativeEffects) {
        /* A battle loads its common bank first, then the ability's uploads:
         * the latter can replace palettes/pages in the shared FX area. */
        ok &= uploadSectorFile(st->textureFile, st->textureFileSize);
    }
    if (st->enemySheet != NULL) {
        s32* sheet = xgSandboxCopy(st->enemySheet, (size_t)st->enemySheetSize);
        if (sheet != NULL) {
            uploadNpcSpriteSheet(sheet, st->vramX, (s16)st->vramY);
            freeHeap(sheet);
        } else ok = 0;
    }
    xgSandboxLeave(sb);
    return ok;
}

int xgSpriteFieldVram(const XgSpriteSetup* setup, uint16_t* out) {
    if (setup == NULL || out == NULL || setup->battle || setup->nativeEffects) return 0;
    XgSandbox* sb = xgSandboxCreate(NULL, 8 << 20);
    if (sb == NULL) return 0;
    int ok = prepareVram(sb, setup);
    if (ok) memcpy(out, xgVgpuVram(xgSandboxGpu(sb)), XG_VRAM_W * XG_VRAM_H * sizeof(uint16_t));
    xgSandboxDestroy(sb);
    return ok;
}

/* Unhook actors whose frames are nowhere (effect sprites of a set the game
 * has not loaded here): the PS1 reads low RAM for them, the PC would fault. */
static void dropFramelessActors(void) {
    SpriteActorCore* p = spriteTransfertListHead;
    while (p != NULL) {
        SpriteActorCore* next = p->m20_object->m38_pNext;
        if (p->m24_vramData == NULL || p->m24_vramData->m0_spriteData == 0) {
            savePointCallbackC_sub1(p);
        }
        p = next;
    }
}

/* A fresh world (sandbox.h: the modules as loaded, an empty heap; VRAM as
 * prepareVram left it) with the actor made as the game makes it. Inside. */
static void fxRootTask(TaskHeader* task) { (void)task; }
static void fxRootDelete(TaskHeader* task) {
    SpriteActorCore* c = task->m4_data;
    allocateSavePointMeshDataSub0_callback(task);
    registerSpriteCallback2Sub0(task + 1);
    task->m14_ownerId.m30 = 1;
    c->m64_spriteByteCode = NULL; c->mA8_x1C = 0;
    /* Keep the final controller state until this owned world is reset. */
}
static int argumentBounds(void* data, const void* p, int bytes) {
    XgSprite* s = data;
    return bytes > 0 && xgSandboxOwns(s->sb, p) &&
           xgSandboxOwns(s->sb, (const uint8_t*)p + bytes - 1);
}
static void captureScreen(void* data) {
    XgSprite* s = data;
    uint16_t* vram = xgVgpuVram(xgSandboxGpu(s->sb));
    if (!s->screen) return;
    for (int y = 0; y < 224; y++) for (int x = 0; x < 320; x++) {
        uint32_t p = s->screen[y * 320 + x];
        vram[y * 1024 + x] = (p >> 3 & 31) | (p >> 6 & 0x3E0) | (p >> 9 & 0x7C00);
    }
    xgVgpuVramChanged(xgSandboxGpu(s->sb));
}

static int modelCommand(void* data, int op, int arg) {
    XgSprite* s = data;
    if (op == 0x46) { s->pendingModel = arg; return 1; }
    if (op == 0x51) {
        freeMecha_battle(11); s->loadedModel = s->loadedExtra = -1;
        return 1;
    }
    for (int k = 0; k < s->setup.modelCount; k++) {
        const XgSpriteModel* m = &s->models[k];
        if (m->id != s->pendingModel) continue;
        if (arg == 2 && s->loadedModel < 0) {
            if (!xgModelsBattleEffectLoad(11, m->model, m->modelSize, m->animation, m->animationSize)) return 0;
            s->loadedModel = m->id;
        }
        if (s->loadedModel != m->id) return 0;
        for (int i = 0; i < m->extraCount; i++) {
            const XgSpriteModelExtra* e = &m->extras[i];
            if (e->animation == arg && s->loadedExtra != e->file) {
                if (!xgModelsBattleEffectExtra(11, e->data, e->size)) return 0;
                s->loadedExtra = e->file;
            }
        }
        mechaPlayAnimation_battle(11, arg == 2 ? 1 : 2, arg);
        return 1;
    }
    return 0;
}

static int newWorld(XgSprite* s, int palette) {
    const XgSpriteSetup* st = &s->setup;
    xgSandboxReset(s->sb, 0);
    if (st->nativeEffects) {
        /* VRAM and native state are restored below; the recorder's GPU
         * registers must also start identically for a reopened bookmark. */
        const uint32_t registers[] = {0xE1000000, 0xE2000000};
        xgVgpuGp0(xgSandboxGpu(s->sb), registers, 2);
    }
    if (s->initialVram) {
        memcpy(xgVgpuVram(xgSandboxGpu(s->sb)), s->initialVram, 1024 * 512 * sizeof(uint16_t));
        xgVgpuVramChanged(xgSandboxGpu(s->sb));
    }
    if (st->nativeEffects || s->companion != NULL) xgSandboxZeroHeap(s->sb);
    xgSandboxEnter(s->sb);
    s->actor = s->target = s->caster = NULL;
    isBattleOverlayLoaded = (u8)(st->battle ? 1 : 0);
    if (st->battle) battleFxViewStart(st->nativeEffects, st->modelCount);
    s->pendingModel = s->loadedModel = s->loadedExtra = -1;
    if (st->modelCount) modelsViewSoundStubs();
    isWorldMapOverlayLoaded = 0;
    battleDefaultEntityScale = BATTLE_SCALE;
    /* The field counts its frames in pairs of vblanks (its draw-env flag is
     * 1): two VM ticks a frame. The battle ticks once a vblank. */
    fieldDrawEnvsInitialized = st->battle ? 0 : 1;
    allocateShapeTransfert(SHAPE_ARENA, 0);
    s->ot = xgSandboxAlloc((st->modelCount ? FX_OT_SIZE : OT_SIZE) * sizeof(OTag));
    s->bundleInHeap = xgSandboxCopy(s->bundle, (size_t)s->bundleSize);
    if (s->bundleInHeap != NULL) {
        applyPatches(s->bundleInHeap, s->patch[0], s->bundleSize);
    }
    AnimationBundle* companion = NULL;
    if (s->companion != NULL) {
        companion = xgSandboxCopy(s->companion, (size_t)s->companionSize);
        if (companion == NULL) { xgSandboxLeave(s->sb); return 0; }
        applyPatches((u8*)companion, s->patch[3], s->companionSize);
    }
    if (s->effects != NULL) {
        u8* fx = xgSandboxCopy(s->effects, (size_t)s->effectsSize);
        if (fx != NULL) {
            applyPatches(fx, s->patch[1], s->effectsSize);
            /* mainBattleSpriteCallback_phase3's set-up of the effect sprites */
            Vec2s16 at = {0x380, 0};
            Vec2s16 clut = {0, 0x1D1};
            initFieldEntitySub4Sub5Sub0(&createSavePointMeshData_mode5, (AnimationBundle*)fx, at, clut, 0);
        }
    }
    if (s->ot == NULL || s->bundleInHeap == NULL) {
        xgSandboxLeave(s->sb);
        return 0;
    }
    if (st->nativeEffects) {
        /* Native model keyframe events also refer to the selected ability
         * bank, even when that file has no separate fragment bundle. */
        Vec2s16 at = {st->vramX, st->vramY}, clut = {st->clutX, st->clutY};
        initFieldEntitySub4Sub5Sub0(&createSavePointMeshData_mode6, (AnimationBundle*)s->bundleInHeap, at, clut, 0);
    }
    if (s->fragments != NULL) {
        u8* bank = xgSandboxCopy(s->fragments, (size_t)s->fragmentsSize);
        if (bank == NULL) { xgSandboxLeave(s->sb); return 0; }
        applyPatches(bank, s->patch[2], s->fragmentsSize);
        Vec2s16 at = {0x380, 0x100}, clut = {0, 0x1F4};
        initFieldEntitySub4Sub5Sub0(&createSavePointMeshData_mode6, (AnimationBundle*)bank, at, clut, 0);
    }
    if (st->nativeEffects) gDepthDivider = 2;
    AnimationBundle* b = (AnimationBundle*)s->bundleInHeap;
    SpriteActor* a;
    if (st->battle) {
        /* createBattleSpriteActor: the CLUT row stays, the palette picks
         * the colours (initBattleSpriteActorVram's vramParam) */
        a = createSpriteActorEX(st->special && companion != NULL ? companion : b,
                               (s16)st->clutX, (s16)st->clutY, (s16)st->vramX, (s16)st->vramY, 0x20, palette);
    } else if (palette != 0) {
        /* OPX_15: createSpriteActorEX with the palette's CLUT column */
        a = createSpriteActorEX(b, (s16)(st->clutX + palette * 16), (s16)st->clutY, (s16)st->vramX, (s16)st->vramY,
                                0x40, palette);
    } else {
        a = createSpriteActor(b, (s16)st->clutX, (s16)st->clutY, (s16)st->vramX, (s16)st->vramY, 0x40);
    }
    /* The target: a VRAM place of its own (the PS1's display area, unused
     * in the sandbox's VRAM), should an opcode make it show a frame. */
    SpriteActor* t = createSpriteActor(st->special && companion != NULL ? companion : b, 0, 0x1FF, 0, 0x100, 0x40);
    if (a == NULL || t == NULL) {
        xgSandboxLeave(s->sb);
        return 0;
    }
    SpriteActorCore* c = &a->m0_spriteActorCore;
    if (st->subSprites > 0) {
        OP_INIT_ENTITY_SCRIPT_sub0Sub3(c, st->subSprites); /* the party's 0x20 parts */
    }
    if (st->battle) {
        /* allocateBattleSpriteActor, createBattleSpriteActor */
        setGraphicEntityScale(c, BATTLE_SCALE);
        c->m82_entityScale = BATTLE_SCALE;
        c->m3C_x1A = 1; /* no battle terrain under it */
        c->m24_vramData->m4_vramLocation.vx = (s16)st->vramX;
        c->m24_vramData->m4_vramLocation.vy = (s16)st->vramY;
        c->m7C_extra->mE_vramLocation = c->m24_vramData->m4_vramLocation;
    } else {
        /* resetFieldScriptEntityGraphicEntity */
        OP_INIT_ENTITY_SCRIPT_sub0Sub5(c, 3);
        c->m2C_scale = FIELD_SCALE;
        c->m82_entityScale = 0x2000;
    }
    c->mB0_x0_animationId = -1; /* at its end an animation stays (no default to return to) */
    /* Where the field puts a new actor (resetFieldScriptEntityGraphicEntity:
     * its entity's place, standing, gravity 0x10000) and the colour it gives
     * it (the entity's sprite colour, 0x80 grey by default): here the
     * origin. The heap is not cleared, so nothing is left to chance. */
    memset(&c->m0_position, 0, sizeof(c->m0_position));
    memset(&c->mC_step, 0, sizeof(c->mC_step));
    c->m84_maxY = 0;
    c->m1C_gravity = 0x10000;
    c->m28_r = c->m29_g = c->m2A_b = 0x80;
    /* Party previews keep the native normal/special pair. Other sources
     * have no companion (setAnimationBundle takes NULL as no swap). */
    c->m4C_specialAnimation = NULL;
    if (companion != NULL) {
        c->m48_defaultAnimationBundle = st->special ? companion : b;
        c->m4C_specialAnimation = st->special ? b : companion;
        setAnimationBundle(c, b); /* static frames belong to the selected bank */
    }
    c->m78_terrainTriangle = 0;
    /* Target opcodes find a second actor in front of this one, never drawn
     * or run. A party target uses its normal bank for reaction poses. */
    SpriteActorCore* tc = &t->m0_spriteActorCore;
    tc->m24_vramData->m4_vramLocation.vx = 0;
    tc->m24_vramData->m4_vramLocation.vy = 0x100;
    tc->m7C_extra->mE_vramLocation = tc->m24_vramData->m4_vramLocation;
    memset(&tc->m0_position, 0, sizeof(tc->m0_position));
    memset(&tc->mC_step, 0, sizeof(tc->mC_step));
    tc->m84_maxY = 0;
    tc->m28_r = tc->m29_g = tc->m2A_b = 0x80;
    tc->m4C_specialAnimation = NULL;
    tc->m78_terrainTriangle = 0;
    tc->m0_position.vx = (st->battle ? 0x180 : 0x200) << 16;
    tc->m82_entityScale = c->m82_entityScale;
    tc->m2C_scale = c->m2C_scale;
    savePointCallbackC_sub1(tc); /* out of the frame transfer list: nothing of it is sent */
    c->m74_targetEntitySprite = tc;
    if (s->casterBundle) {
        AnimationBundle* normal = xgSandboxCopy(s->casterBundle, s->casterSize);
        applyPatches((u8*)normal, s->patch[3], s->casterSize);
        /* The battle loader reserves 32 party parts. Its normal bank's
         * header understates this for some poses; child clones use it too. */
        u16* frames = (u16*)((u8*)normal + normal->m8_spriteDataOffset);
        *frames = (*frames & 0x81FF) | (0x20 << 9);
        SpriteActor* caster = createSpriteActorEX(normal, 0, 0x1C0, 0x100, 0x1C0, 0x20, 0);
        SpriteActor* target = createSpriteActorEX(normal, 0, 0x1C1, 0x140, 0x1C0, 0x20, 0);
        if (!caster || !target) { xgSandboxLeave(s->sb); return 0; }
        s->caster = &caster->m0_spriteActorCore;
        tc = &target->m0_spriteActorCore;
        setGraphicEntityScale(s->caster, BATTLE_SCALE); setGraphicEntityScale(tc, BATTLE_SCALE);
        s->caster->m82_entityScale = tc->m82_entityScale = BATTLE_SCALE;
        spriteActorSetColor(s->caster, 0x80, 0x80, 0x80);
        spriteActorSetColor(tc, 0x80, 0x80, 0x80);
        s->caster->m0_position.vx = -192 * 65536;
        tc->m0_position.vx = 192 * 65536;
        s->caster->m0_position.vy = tc->m0_position.vy = 96 * 65536;
        s->caster->m74_targetEntitySprite = tc;
        tc->m74_targetEntitySprite = s->caster;
        tc->m70_parent = s->caster;
        spriteActorSetPlayingAnimation(s->caster, 0); spriteActorSetPlayingAnimation(tc, 0);
        OP_INIT_ENTITY_SCRIPT_sub0Sub9(s->caster); OP_INIT_ENTITY_SCRIPT_sub0Sub9(tc);
        c->m74_targetEntitySprite = tc;
        c->m70_parent = s->caster;
    }
    if ((companion != NULL || st->nativeEffects) && !battleSpriteViewTerrain(c, tc)) {
        xgSandboxLeave(s->sb);
        return 0;
    }
    if (st->nativeEffects) {
        TaskHeader* root = allocateTaskPair(2 * sizeof(TaskHeader), NULL, fxRootTask, fxRootTask, NULL);
        root->m4_data = root[1].m4_data = c;
        c->m6C_owner = root;
        if (!s->caster) c->m70_parent = tc;
        battleFxViewCapture(captureScreen, s);
        battleFxViewArguments(argumentBounds);
        battleFxViewModels(modelCommand);
    }
    s->actor = c;
    s->target = tc;
    s->parity = 0;
    s->alive = 0;
    s->heapFull = 0;
    s->showScene = 0;
    s->rootTask = NULL;
    if (s->screen) memset(s->screen, 0, 320 * 224 * sizeof(*s->screen));
    return 1;
}

/* ---- the bundle ---------------------------------------------------------------------------------- */

XgSprite* xgSpriteCreate(const XgSpriteSetup* setup, char* why, int whySize) {
    XgSprite* s = calloc(1, sizeof(XgSprite));
    if (s == NULL) {
        snprintf(why, (size_t)whySize, "out of memory");
        return NULL;
    }
    s->setup = *setup;
    const uint8_t* b = setup->bundle;
    int size = setup->bundleSize;
    if (b == NULL || size < 16 || rd32(b) < 3 || rd32(b) > (setup->nativeEffects ? 64u : 16u)) {
        snprintf(why, (size_t)whySize, "not a sprite bundle");
        free(s);
        return NULL;
    }
    s->animsAt = (int)rd32(b + 4);
    s->framesAt = (int)rd32(b + 8);
    s->clutAt = (int)rd32(b + 12);
    if (s->animsAt < 16 || s->animsAt + 4 > size || s->framesAt < 16 || s->framesAt + 4 > size || (s->clutAt != 0 && s->clutAt < 16) ||
        s->clutAt + 4 > size) {
        snprintf(why, (size_t)whySize, "not a sprite bundle (its offsets run out of it)");
        free(s);
        return NULL;
    }
    /* Copies, padded: readers run a little past the end. */
    s->bundle = calloc((size_t)size + 64, 1);
    if (s->bundle == NULL) {
        free(s);
        snprintf(why, (size_t)whySize, "out of memory");
        return NULL;
    }
    memcpy(s->bundle, b, (size_t)size);
    s->bundleSize = size;
    if (setup->companion != NULL) {
        if (!setup->battle || !xgBattleFxBundle(setup->companion, setup->companionSize)) {
            xgSpriteDestroy(s); snprintf(why, (size_t)whySize, "not a paired battle sprite bundle"); return NULL;
        }
        s->companion = calloc((size_t)setup->companionSize + 64, 1);
        if (s->companion == NULL) { xgSpriteDestroy(s); snprintf(why, (size_t)whySize, "out of memory"); return NULL; }
        memcpy(s->companion, setup->companion, (size_t)setup->companionSize);
        s->companionSize = setup->companionSize;
    }
    s->setup.companion = NULL;
    if (setup->effects != NULL && setup->effectsSize >= 16) {
        s->effects = calloc((size_t)setup->effectsSize + 64, 1);
        if (s->effects != NULL) {
            memcpy(s->effects, setup->effects, (size_t)setup->effectsSize);
            s->effectsSize = setup->effectsSize;
        }
    }
    if (setup->fragments != NULL && xgBattleFxBundle(setup->fragments, setup->fragmentsSize)) {
        s->fragments = calloc((size_t)setup->fragmentsSize + 64, 1);
        if (s->fragments == NULL) { xgSpriteDestroy(s); snprintf(why, (size_t)whySize, "out of memory"); return NULL; }
        memcpy(s->fragments, setup->fragments, (size_t)setup->fragmentsSize);
        s->fragmentsSize = setup->fragmentsSize;
    }
    s->setup.fragments = NULL;
    s->setup.bundle = NULL;
    s->setup.effects = NULL;
    if (setup->caster && xgBattleFxBundle(setup->caster, setup->casterSize)) {
        s->casterBundle = malloc(setup->casterSize);
        if (!s->casterBundle) { xgSpriteDestroy(s); return NULL; }
        memcpy(s->casterBundle, setup->caster, setup->casterSize);
        s->casterSize = setup->casterSize;
    }
    s->setup.caster = NULL;
    if (setup->modelCount < 0 || setup->modelCount > 32 || (setup->modelCount && !setup->models)) {
        xgSpriteDestroy(s); snprintf(why, whySize, "invalid ability models"); return NULL;
    }
    s->models = calloc(setup->modelCount, sizeof(*s->models));
    if (setup->modelCount && !s->models) { xgSpriteDestroy(s); return NULL; }
    for (int i = 0; i < setup->modelCount; i++) {
        const XgSpriteModel* src = &setup->models[i];
        XgSpriteModel* dst = &s->models[i];
        *dst = *src;
        dst->model = dst->animation = NULL; dst->extras = NULL;
        if (src->modelSize < 24 || src->animationSize < 16 || src->extraCount < 0 || src->extraCount > 256) {
            xgSpriteDestroy(s); snprintf(why, whySize, "invalid ability model files"); return NULL;
        }
        dst->model = malloc(src->modelSize); dst->animation = malloc(src->animationSize);
        XgSpriteModelExtra* extras = calloc(src->extraCount, sizeof(*extras));
        dst->extras = extras;
        if (!dst->model || !dst->animation || (src->extraCount && !extras)) { xgSpriteDestroy(s); return NULL; }
        memcpy((void*)dst->model, src->model, src->modelSize);
        memcpy((void*)dst->animation, src->animation, src->animationSize);
        for (int j = 0; j < src->extraCount; j++) {
            extras[j] = src->extras[j];
            extras[j].data = malloc(extras[j].size);
            if (!extras[j].data) { xgSpriteDestroy(s); return NULL; }
            memcpy((void*)extras[j].data, src->extras[j].data, extras[j].size);
        }
    }
    s->setup.models = NULL;
    if (setup->nativeEffects) s->screen = calloc(320 * 224, sizeof(*s->screen));
    /* The container's count (low 6 bits), the frame table's count (9 bits)
     * and pre-baked bit (isVramPreBacked), the palettes' header: 16-colour
     * rows per palette, palettes - 1 (initFieldEntitySub4Sub5Sub0,
     * uploadCharacterSprite). */
    s->anims = rd16(s->bundle + s->animsAt) & 0x3F;
    int fh = rd16(s->bundle + s->framesAt);
    s->frames = fh & 0x1FF;
    s->prebaked = (fh >> 15) & 1;
    if (!s->clutAt) {
        /* Native root has no palette. Point the owned copy at its padded
         * zero header; child bundles retain their original offsets. */
        s->clutAt = size;
        s->bundle[12] = size; s->bundle[13] = size >> 8;
        s->bundle[14] = size >> 16; s->bundle[15] = size >> 24;
    }
    s->colours = rd16(s->bundle + s->clutAt) * 16;
    s->palettes = rd16(s->bundle + s->clutAt + 2) + 1;
    if (setup->enemySheet != NULL && setup->enemyPalette >= s->palettes && setup->enemyPalette < 16) {
        /* The native loader keeps all control bytes together. Some slots
         * select CLUT words in the following control data, beyond the
         * bundle's declared table; retain those exact words when bounded. */
        int wanted = setup->enemyPalette + 1;
        if (s->colours > 0 && wanted <= (size - s->clutAt - 4) / (s->colours * 2)) s->palettes = wanted;
    }
    if (s->colours > 0 && s->clutAt + 4 + s->palettes * s->colours * 2 > size) {
        s->palettes = (size - s->clutAt - 4) / (s->colours * 2);
        if (s->palettes < 1) {
            s->palettes = 1;
        }
    }
    const char* const mods[] = {"battle", NULL};
    const char* const fxMods[] = {"battle", "field_mecha", "battle_fx_sprite_wobble", "battle_fx_model_shatter",
                                 "battle_fx_tiled_sprite", "battle_fx_sprite_band", "battle_fx_screen_ripple", NULL};
    s->sb = xgSandboxCreate(setup->nativeEffects ? fxMods : setup->battle ? mods : NULL, 8u << 20);
    if (s->sb == NULL) {
        snprintf(why, (size_t)whySize, "out of memory (sandbox)");
        xgSpriteDestroy(s);
        return NULL;
    }
    if (!walkAnimations(s)) {
        snprintf(why, (size_t)whySize, "out of memory (bytecode walk)");
        xgSpriteDestroy(s);
        return NULL;
    }
    prepareVram(s->sb, &s->setup);
    if (setup->nativeEffects) {
        s->initialVram = malloc(1024 * 512 * sizeof(uint16_t));
        if (!s->initialVram) { xgSpriteDestroy(s); return NULL; }
        memcpy(s->initialVram, xgVgpuVram(xgSandboxGpu(s->sb)), 1024 * 512 * sizeof(uint16_t));
    }
    s->setup.textureFile = NULL; /* used up */
    s->setup.tims = NULL;
    s->setup.sheets = NULL;
    s->setup.enemySheet = NULL;
    s->setup.slots = NULL;
    s->setup.effectsTexture = NULL;
    if (!newWorld(s, 0)) {
        snprintf(why, (size_t)whySize, "the game could not make the actor (the sandbox's heap is full)");
        xgSpriteDestroy(s);
        return NULL;
    }
    xgSandboxLeave(s->sb);
    return s;
}

void xgSpriteDestroy(XgSprite* s) {
    if (s == NULL) {
        return;
    }
    xgSandboxDestroy(s->sb);
    free(s->bundle);
    free(s->companion);
    free(s->effects);
    free(s->fragments);
    free(s->casterBundle);
    for (int i = 0; s->models && i < s->setup.modelCount; i++) {
        XgSpriteModel* m = &s->models[i];
        free((void*)m->model); free((void*)m->animation);
        for (int j = 0; m->extras && j < m->extraCount; j++) free((void*)m->extras[j].data);
        free((void*)m->extras);
    }
    free(s->models);
    free(s->screen);
    free(s->initialVram);
    free(s->animFlags);
    free(s->animSkipped);
    free(s->patch[0]);
    free(s->patch[1]);
    free(s->patch[2]);
    free(s->patch[3]);
    free(s);
}

int xgSpriteAnimationCount(const XgSprite* s) {
    return s->anims;
}

int xgSpriteFrameCount(const XgSprite* s) {
    return s->frames;
}

int xgSpritePaletteCount(const XgSprite* s) {
    return s->palettes;
}

int xgSpriteColours(const XgSprite* s) {
    return s->colours;
}

int xgSpritePrebaked(const XgSprite* s) {
    return s->prebaked;
}

int xgSpriteUnits(const XgSprite* s) {
    return s->setup.battle ? 1 : 3; /* 2 or 6 world units, halved by the camera */
}

int xgSpriteBattle(const XgSprite* s) {
    return s->setup.battle;
}

int xgSpriteTicksPerStep(const XgSprite* s) {
    return s->setup.battle ? 1 : 2;
}

int xgSpriteAnimationDirections(const XgSprite* s, int anim) {
    if (anim < 0 || anim >= s->anims) {
        return 1;
    }
    int at = s->animsAt + rd16(s->bundle + s->animsAt + 2 + 2 * anim);
    if (at < 0 || at + 2 > s->bundleSize) {
        return 1;
    }
    int mode = rd16(s->bundle + at) & 3; /* setCurrentAnimationPtr: mA8_x14 */
    return mode == 1 ? 4 : mode == 2 ? 8 : 1;
}

int xgSpriteAnimationFlags(const XgSprite* s, int anim) {
    return anim >= 0 && anim < s->anims && s->animFlags != NULL ? s->animFlags[anim] : XG_SPRITE_ANIM_UNSAFE;
}

int xgSpriteAnimationAlias(const XgSprite* s, int anim) {
    if (anim < 0 || anim >= s->anims || s->animsAt + 2 + 2 * (anim + 1) > s->bundleSize) return -1;
    int offset = rd16(s->bundle + s->animsAt + 2 + 2 * anim);
    for (int a = 0; a < anim; a++) {
        if (rd16(s->bundle + s->animsAt + 2 + 2 * a) == offset) return a;
    }
    return -1;
}

int xgSpriteAnimationSkipped(const XgSprite* s, int anim, int i) {
    return anim >= 0 && anim < s->anims && i >= 0 && i < 4 && s->animSkipped != NULL ? s->animSkipped[anim * 4 + i] : -1;
}

/* setSpriteActorAngle: 4 directions take ((angle + 0x600) >> 10) & 3, 8 take
 * ((angle + 0x500) >> 9) & 7; the last ones mirror earlier tables (4: 3 is
 * table 1 mirrored; 8: 5, 6, 7 are tables 3, 2, 1 mirrored). */
int xgSpriteDirectionAngle(int directions, int d) {
    if (directions == 8) {
        return ((d & 7) * 0x200 - 0x500) & 0xFFF;
    }
    if (directions == 4) {
        return ((d & 3) * 0x400 - 0x600 + 0x200) & 0xFFF;
    }
    return 0;
}

int xgSpriteDirectionMirrored(int directions, int d) {
    return directions == 8 ? (d & 7) >= 5 : directions == 4 ? (d & 3) == 3 : 0;
}

/* ---- running it ---------------------------------------------------------------------------------- */
static void drawFrame(XgSprite* s, int advance);

int xgSpritePlay(XgSprite* s, int anim, int direction, int palette) {
    if (anim < 0 || anim >= s->anims || (xgSpriteAnimationFlags(s, anim) & XG_SPRITE_ANIM_UNSAFE)) {
        return 0;
    }
    if (palette < 0 || palette >= s->palettes) {
        palette = 0;
    }
    if (!newWorld(s, palette)) {
        return 0;
    }
    int dirs = xgSpriteAnimationDirections(s, anim);
    int angle = xgSpriteDirectionAngle(dirs, direction);
    s->showScene = s->caster != NULL;
    s->capture = !!(s->animFlags[anim] & XG_SPRITE_ANIM_SCREEN);
    if (s->caster) {
        SpriteActorCore* parent = s->caster;
        SpriteVramData* vram = s->actor->m24_vramData;
        parent->m6C_owner = s->actor->m6C_owner;
        s->target->m6C_owner = parent->m6C_owner;
        savePointCallbackC_sub1(s->actor);
        int at = s->animsAt + rd16(s->bundle + s->animsAt + 2 + anim * 2);
        s->actor = spriteBytecode2ExtendedE0(parent, (u16*)(s->bundleInHeap + at), vram);
        s->actor->m44_currentAnimationBundle = s->actor->m48_defaultAnimationBundle =
            (AnimationBundle*)s->bundleInHeap;
        s->actor->mB0_x0_animationId = -1;
        s->rootTask = s->actor->m6C_owner;
        setTaskUpdateFunction(s->rootTask, fxRootTask);
        setTaskDeleteFunction(s->rootTask, fxRootDelete);
        battleFxViewContext(s->caster, s->target, s->ot, 0);
    }
    /* resetFieldScriptEntityGraphicEntity's order: the animation, then the
     * facing (the field sets it each frame from the actor's direction). */
    spriteActorSetPlayingAnimation(s->actor, s->setup.special && s->companion != NULL ? ~anim : anim);
    setSpriteActorAngle(s->actor, (s16)angle);
    if (s->capture) drawFrame(s, 0);
    /* resetFieldScriptEntityGraphicEntity then runs the VM once, so the
     * actor has its first frame when it is first drawn */
    OP_INIT_ENTITY_SCRIPT_sub0Sub9(s->actor);
    execSpritesCallbacksList2();
    xgSandboxLeave(s->sb);
    return 1;
}

void xgSpriteFacing(XgSprite* s, int anim, int direction) {
    if (!s || !s->actor || anim < 0 || anim >= s->anims) return;
    xgSandboxEnter(s->sb);
    setSpriteActorAngle(s->actor, (s16)xgSpriteDirectionAngle(xgSpriteAnimationDirections(s, anim), direction));
    drawFrame(s, 0);
    xgSandboxLeave(s->sb);
}

int xgSpriteShowFrame(XgSprite* s, int frame, int flip, int palette) {
    if (palette < 0 || palette >= s->palettes) {
        palette = 0;
    }
    if (!newWorld(s, palette)) {
        return 0;
    }
    setSpriteActorStaticFrame(s->actor, frame, flip ? 1 : 0, 0);
    xgSandboxLeave(s->sb);
    return 1;
}

/* One drawn frame, as the field's renderChars and the battle's frame loop
 * run their sprites (see the header). Inside. */
static void drawFrame(XgSprite* s, int advance) {
    XgVgpu* gpu = xgSandboxGpu(s->sb);
    /* Room in the heap for what a frame may make (effect sprites): game code
     * that is refused memory writes through NULL, so a frame that might be
     * refused does not run (the picture stays). */
    void* room = allocateHeap(HEAP_ROOM, 0);
    if (room == NULL) {
        s->heapFull = 1;
        return;
    }
    freeHeap(room);
    xgVgpuClearPrims(gpu);
    if (s->showScene) xgVgpuSetArea(gpu, 0, 0, 319, 223, 0, 0);
    else xgVgpuSetArea(gpu, -1024, -1024, 1023, 1023, 0, 0);
    ClearOTagR(s->ot, s->setup.modelCount ? FX_OT_SIZE : OT_SIZE);
    clearShapeTransfertTableEntry(s->parity);
    s->parity ^= 1;
    MATRIX view = {{{0x1000, 0, 0}, {0, 0x1000, 0}, {0, 0, 0x1000}}, {0, 0, VIEW_DIST}};
    if (s->companion != NULL) {
        /* Follow the actor's travel, preserving its poses and jump height
         * in the fixed sprite preview. Physics still runs in world space. */
        view.t[0] = -(s->actor->m0_position.vx >> 16);
        view.t[2] -= s->actor->m0_position.vz >> 16;
    }
    SetGeomOffset(s->setup.nativeEffects ? 160 : 0, s->setup.nativeEffects ? 112 : 0);
    SetGeomScreen(VIEW_H);
    setCharacterRenderingOT(s->ot);
    setCurrentRenderingMatrix(&view);
    int ripple = s->setup.nativeEffects && battleFxScreenViewActive();
    if (s->setup.nativeEffects) {
        battleFxViewCamera(&view, s->showScene);
        setCurrentRenderingMatrix(&view);
        battleFxViewContext(s->caster ? s->caster : s->actor, s->target, s->ot, s->parity);
    }
    dropFramelessActors();
    uploadCharacterSprites();
    if (!ripple) execSpritesCallbacksList1();
    if (!ripple && (advance || (!s->setup.nativeEffects && s->companion == NULL))) execSpritesCallbacksList2();
    if (s->showScene) {
        uint8_t rgb[3];
        XgPrim background = {0};
        battleFxViewBackground(rgb);
        background.count = 4;
        background.x[1] = background.x[3] = 320;
        background.y[2] = background.y[3] = 224;
        background.area[2] = 319; background.area[3] = 223;
        background.r[0] = rgb[0]; background.g[0] = rgb[1]; background.b[0] = rgb[2];
        if (ripple) {
            captureScreen(s);
            background.u[1] = background.u[3] = 320;
            background.v[2] = background.v[3] = 224;
            background.flags = XG_PRIM_TEXTURED | XG_PRIM_RAW | XG_PRIM_FREE_UV;
            background.tpage = 2 << 7;
        }
        xgVgpuAddPrim(gpu, &background);
    }
    SpriteActorCore* c = s->actor;
    if (!s->setup.battle) {
        /* renderFieldCharacterSprites: the camera's rotation (none here)
         * scaled by the actor's 3D scale x 3/4, and the entity's colour
         * (setSpriteColorIfNoFog: the default 0x80 grey) */
        MATRIX* m = &c->m20_object->mC_matrix;
        memset(m, 0, sizeof(*m));
        m->m[0][0] = m->m[1][1] = m->m[2][2] = FIELD_SCALE;
        spriteActorSetColor(c, 0x80, 0x80, 0x80);
    }
    /* Two OT entries in front of its depth, as renderFieldCharacterSprites
     * puts it (effects at its place are behind it). */
    int depth = ((VIEW_DIST >> 2) >> 2) - 2;
    POLY_FT4* actorPrims = (POLY_FT4*)shapeTransfertTableCurrentEntry;
    if (!ripple && !s->rootTask && c->m3C_x0_type == 1) renderSpriteActor(c, &s->ot[depth]);
    if (!ripple && s->showScene && battleFxViewSceneVisible()) {
        renderSpriteActor(s->caster, &s->ot[depth]);
        renderSpriteActor(s->target, &s->ot[depth]);
    }
    s->actorBounds = 0;
    if (s->companion != NULL) {
        s->actorX0 = s->actorY0 = 1 << 30;
        s->actorX1 = s->actorY1 = -(1 << 30);
        for (POLY_FT4* p = actorPrims; (u8*)p < shapeTransfertTableCurrentEntry; p++) {
            int x[4] = {p->x0, p->x1, p->x2, p->x3}, y[4] = {p->y0, p->y1, p->y2, p->y3};
            for (int k = 0; k < 4; k++) {
                if (x[k] < s->actorX0) s->actorX0 = x[k];
                if (y[k] < s->actorY0) s->actorY0 = y[k];
                if (x[k] > s->actorX1) s->actorX1 = x[k];
                if (y[k] > s->actorY1) s->actorY1 = y[k];
            }
            s->actorBounds = 1;
        }
    }
    if (advance && !ripple && (!s->rootTask || s->rootTask->m8_updateCallback == fxRootTask)) {
        /* renderChars after drawing; the battle's battleSpriteUpdate */
        OP_INIT_ENTITY_SCRIPT_sub0Sub9(c);
        if (s->companion != NULL || s->rootTask) {
            updateSpriteMovement(c);
            if (c->mAC_x6) {
                OP_INIT_ENTITY_SCRIPT_sub0Sub9(c);
                updateSpriteMovement(c);
            }
        }
    }
    shapeTransfert();
    if (advance && !ripple && s->showScene) {
        OP_INIT_ENTITY_SCRIPT_sub0Sub9(s->caster);
        OP_INIT_ENTITY_SCRIPT_sub0Sub9(s->target);
        updateSpriteMovement(s->caster);
        updateSpriteMovement(s->target);
    }
    if (ripple) battleFxScreenViewStep(s->ot, s->parity, advance);
    if (!ripple && s->setup.modelCount) battleFxViewRenderModels(&view, s->ot, s->parity, advance);
    DrawOTag(&s->ot[(s->setup.modelCount ? FX_OT_SIZE : OT_SIZE) - 1]);
    s->alive = allocateSavePointMeshDataSub0_var1 - (s->setup.nativeEffects ? 1 : 0) +
               (s->setup.nativeEffects && battleFxScreenViewActive());
    if (s->rootTask && !s->rootTask->m14_ownerId.m30) s->alive--;
    if (s->setup.modelCount && modelsBattleMecha(11)) s->alive++;
    if (s->capture && s->screen) xgSpriteRaster(s, 0, 0, 320, 224, 0xFF201810, s->screen);
}

int xgSpriteStep(XgSprite* s, int advance) {
    xgSandboxEnter(s->sb);
    drawFrame(s, advance);
    xgSandboxLeave(s->sb);
    return xgVgpuPrimCount(xgSandboxGpu(s->sb));
}

XgVgpu* xgSpriteGpu(XgSprite* s) {
    return xgSandboxGpu(s->sb);
}

static uint32_t mix(uint32_t h, uint32_t v) {
    h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
    return h;
}

void xgSpriteGetState(const XgSprite* s, XgSpriteState* out) {
    memset(out, 0, sizeof(*out));
    const SpriteActorCore* c = s->actor;
    if (c == NULL) {
        return;
    }
    /* The actor and the task counts are in the sandbox's heap and its world:
     * the heap is the sandbox's own memory, read as it is. */
    out->frame = (u16)c->m34_currentSpriteFrame;
    out->flipX = c->m3C_x3_flipX;
    out->flipY = c->m3C_x4;
    out->step = c->mA8_x16_bytecodeStep;
    out->ended = c->m64_spriteByteCode == NULL || c->mA8_x1C == 0 || s->heapFull;
    out->heapFull = s->heapFull;
    if (s->setup.nativeEffects) {
        xgSandboxEnter(s->sb);
        out->nativeCommands = battleFxViewCommands();
        out->wobbles = battleFxViewWobbles();
        xgSandboxLeave(s->sb);
    }
    out->subSprites = c->m40_draw.bits.x2_subSpriteCount;
    uint32_t h = 0;
    if (s->setup.nativeEffects) {
        xgSandboxEnter(s->sb);
        h = mix(h, battleFxScreenViewState());
        if (s->setup.modelCount) {
            const sLoadedMechas* m = modelsBattleMecha(11);
            if (m && m->m4_bones) {
                h = mix(h, m->m10_bytecode0 ? (u8*)m->m10_bytecode0 - (u8*)m->m8_bytecodeTable[0] : -1);
                h = mix(h, m->m40_waitTimer); h = mix(h, m->m44_delayTimer);
                h = mix(h, m->m98_eventFrame); h = mix(h, m->m9C_eventIndex);
                for (int b = 0; b < m->m4_bones->mA_numBones; b++) {
                    const sMechaBone* bone = &m->m4_bones[b];
                    h = mix(h, bone->m54_rotationAngles.vx);
                    h = mix(h, bone->m54_rotationAngles.vy);
                    h = mix(h, bone->m54_rotationAngles.vz);
                    for (int k = 0; k < 3; k++) {
                        h = mix(h, bone->m5C_translation[k]);
                        const sMechaInitVar2Sub* track = bone->m70_animTracks[k];
                        if (track && track->m0_inUse) h = mix(h, track->m10_currentTime);
                    }
                }
            }
        }
        xgSandboxLeave(s->sb);
    }
    h = mix(h, (uint32_t)(c->m64_spriteByteCode != NULL ? c->m64_spriteByteCode - (u8*)s->bundleInHeap : -1));
    h = mix(h, (uint32_t)c->m9E_wait);
    h = mix(h, (uint32_t)(u16)c->m34_currentSpriteFrame);
    h = mix(h, (uint32_t)c->mA8_xB_frame);
    h = mix(h, (uint32_t)c->m3C_x3_flipX << 1 | c->m3C_x4);
    h = mix(h, (uint32_t)c->m8C_stackPosition);
    if (s->companion != NULL) {
        h = mix(h, c->m0_position.vx); h = mix(h, c->m0_position.vy); h = mix(h, c->m0_position.vz);
        h = mix(h, c->mC_step.vx); h = mix(h, c->mC_step.vy); h = mix(h, c->mC_step.vz);
        h = mix(h, c->m44_currentAnimationBundle == c->m48_defaultAnimationBundle);
    }
    /* Native FX roots leave unused stack bytes uninitialised. They are not
     * VM state and cannot identify a replay across separately built worlds. */
    int stackStart = s->setup.nativeEffects ? c->m8C_stackPosition : 0;
    for (int i = stackStart; i < 16; i++) {
        h = mix(h, c->m8E_stack[i]);
    }
    h = mix(h, (uint32_t)c->m28_r | (uint32_t)c->m29_g << 8 | (uint32_t)c->m2A_b << 16);
    out->effects = s->alive;
    h = mix(h, (uint32_t)s->alive);
    if (s->setup.nativeEffects) {
        xgSandboxEnter(s->sb);
        /* An invisible child may be waiting to emit its first polygon.
         * Root state plus today's picture must not end a sequence early. */
        for (TaskHeader* t = spriteCallback2Var2; t; t = t->m18_pNext) {
            if (t->m8_updateCallback == savePointCallback8 || t->m8_updateCallback == savePointCallback8Counted ||
                t->m8_updateCallback == battleSpriteEffect_20_update) {
                const SpriteActorCore* child = t->m4_data;
                h = mix(h, child->m9E_wait); h = mix(h, child->mA8_x16_bytecodeStep);
                h = mix(h, child->m0_position.vx); h = mix(h, child->m0_position.vy);
                h = mix(h, child->m0_position.vz);
            }
        }
        xgSandboxLeave(s->sb);
    }
    /* and the picture: effect sprites have states of their own */
    const XgPrim* p = xgVgpuPrims(xgSandboxGpu(s->sb));
    int n = xgVgpuPrimCount(xgSandboxGpu(s->sb));
    for (int i = 0; i < n; i++) {
        const uint32_t* w = (const uint32_t*)&p[i];
        for (size_t k = 0; k < sizeof(XgPrim) / 4; k++) {
            h = mix(h, w[k]);
        }
    }
    out->key = h;
}

static int primBounds(XgVgpu* gpu, int* x0, int* y0, int* x1, int* y1) {
    const XgPrim* p = xgVgpuPrims(gpu);
    int n = xgVgpuPrimCount(gpu);
    int ax = 1 << 30, ay = 1 << 30, bx = -(1 << 30), by = -(1 << 30);
    for (int i = 0; i < n; i++) {
        if (p[i].flags & XG_PRIM_FILL) {
            continue;
        }
        for (int k = 0; k < p[i].count; k++) {
            ax = p[i].x[k] < ax ? p[i].x[k] : ax;
            ay = p[i].y[k] < ay ? p[i].y[k] : ay;
            bx = p[i].x[k] > bx ? p[i].x[k] : bx;
            by = p[i].y[k] > by ? p[i].y[k] : by;
        }
    }
    if (ax > bx) {
        return 0;
    }
    *x0 = ax;
    *y0 = ay;
    *x1 = bx;
    *y1 = by;
    return 1;
}

int xgSpritePrimBounds(XgSprite* s, int* x0, int* y0, int* x1, int* y1) {
    return primBounds(xgSandboxGpu(s->sb), x0, y0, x1, y1);
}

int xgSpritePreviewBounds(XgSprite* s, int* x0, int* y0, int* x1, int* y1) {
    if (s->showScene) { *x0 = *y0 = 0; *x1 = 320; *y1 = 224; return 1; }
    if (s->companion == NULL) return xgSpritePrimBounds(s, x0, y0, x1, y1);
    if (!s->actorBounds) return 0;
    *x0 = s->actorX0; *y0 = s->actorY0;
    *x1 = s->actorX1; *y1 = s->actorY1;
    return 1;
}

int xgSpriteFrameBounds(XgSprite* s, int* x0, int* y0, int* x1, int* y1) {
    if (!s->haveBounds) {
        s->haveBounds = -1;
        int ax = 1 << 30, ay = 1 << 30, bx = -(1 << 30), by = -(1 << 30);
        if (newWorld(s, 0)) {
            for (int f = 1; f <= s->frames; f++) {
                for (int flip = 0; flip < 2; flip++) {
                    setSpriteActorStaticFrame(s->actor, f, flip, 0);
                    drawFrame(s, 0);
                    int a, b, c, d;
                    if (primBounds(xgSandboxGpu(s->sb), &a, &b, &c, &d)) {
                        ax = a < ax ? a : ax;
                        ay = b < ay ? b : ay;
                        bx = c > bx ? c : bx;
                        by = d > by ? d : by;
                    }
                }
            }
            xgSandboxLeave(s->sb);
        }
        if (ax <= bx) {
            s->haveBounds = 1;
            s->bx0 = ax;
            s->by0 = ay;
            s->bx1 = bx;
            s->by1 = by;
        }
    }
    if (s->haveBounds != 1) {
        return 0;
    }
    *x0 = s->bx0;
    *y0 = s->by0;
    *x1 = s->bx1;
    *y1 = s->by1;
    return 1;
}

/* ---- pictures --------------------------------------------------------------------------------------- */

void xgSpriteRaster(XgSprite* s, int x0, int y0, int w, int h, uint32_t bg, uint32_t* rgba) {
    xgVgpuRaster(xgSandboxGpu(s->sb), x0, y0, w, h, 1.0 / xgSpriteUnits(s), bg, rgba);
}

void xgSpriteDebugDump(const XgSprite* s) {
    const SpriteActorCore* c = s->actor;
    if (c == NULL) {
        return;
    }
    const SpriteObject* o = c->m20_object;
    fprintf(stderr, "sprites: actor pos %d %d %d scale %d shift %d anchor %d %d vram %d %d clut %d %d\n",
            c->m0_position.vx >> 16, c->m0_position.vy >> 16, c->m0_position.vz >> 16, c->m2C_scale, c->m40_draw.bits.x8,
            o ? o->m3C_anchorX : 0, o ? o->m3D_anchorY : 0, c->m24_vramData->m4_vramLocation.vx, c->m24_vramData->m4_vramLocation.vy,
            c->m24_vramData->m8_clut.vx, c->m24_vramData->m8_clut.vy);
    fprintf(stderr, "sprites: VM pc %td wait %d velocity %d %d %d gravity %d bank %s\n",
            c->m64_spriteByteCode != NULL ? c->m64_spriteByteCode - s->bundleInHeap : -1,
            c->m9E_wait, c->mC_step.vx, c->mC_step.vy, c->mC_step.vz, c->m1C_gravity,
            c->m44_currentAnimationBundle == c->m48_defaultAnimationBundle ? "normal" : "special");
    if (!o) return;
    fprintf(stderr, "sprites: matrix %d %d %d / %d %d %d / %d %d %d t %d %d %d\n", o->mC_matrix.m[0][0],
            o->mC_matrix.m[0][1], o->mC_matrix.m[0][2], o->mC_matrix.m[1][0], o->mC_matrix.m[1][1], o->mC_matrix.m[1][2],
            o->mC_matrix.m[2][0], o->mC_matrix.m[2][1], o->mC_matrix.m[2][2], o->mC_matrix.t[0], o->mC_matrix.t[1],
            o->mC_matrix.t[2]);
    for (int i = 0; i < c->m40_draw.bits.x2_subSpriteCount; i++) {
        const SubSprite* p = &o->m30_subSprites[i];
        fprintf(stderr, "sprites:   sub %d: ofs %d %d tex %d %d size %d %d adj %d %d tpage %04x clut %04x bits %08x\n", i,
                p->m0_offsetX, p->m2_offsetY, p->m4_texcoordX, p->m5_texcoordY, p->m6_width, p->m7_height,
                p->m8_widthAdjust, p->m9_heightAdjust, p->mA_tpage, p->mC_clut, p->m14_attrs.raw);
    }
    if (c->m3C_x0_type == 1 && o->m34_perSubgroupTransform != NULL) {
        for (int i = 0; i < 8; i++) {
            const SubgroupTransform* t = &o->m34_perSubgroupTransform[i];
            fprintf(stderr, "sprites:   group %d: t %d %d r %d %d %d\n", i, t->m0_translateX, t->m1_translateY,
                    t->m2_rotateX, t->m4_rotateY, t->m6_rotateZ);
        }
    }
}

/* src/main/kernel.c: the field the game runs (the low 12 bits; -1 none) */
extern s32 fieldMapNumber;

int xgSpriteGameField(void) {
    return fieldMapNumber >= 0 ? (int)(fieldMapNumber & 0xFFF) : -1;
}

/* port/src/system/libc.c */
u32 portRandSeed(void);

unsigned xgSpriteGameRandSeed(void) {
    return xgSandboxCurrent() == NULL ? portRandSeed() : 0;
}
