/* Native battle globals that the platform's effect previews need. */
#include "common.h"
#include "psyq.h"
#include "field_mecha/mecha.h"
#include "battle/battle.h"
#include "main/memory.h"
#include "modelsview.h"
extern MATRIX battleIdentityMatrix;
extern sMechaInitVar3 battleMechaInitVar3;
extern SpriteActorCore* processBattleAnimationSub0_var1;
extern u8 battleSpritesDisabled;
extern SVECTOR battleCameraEye, battleCameraAt;
extern SVECTOR battleSceneRenderingMatrixOffset;
extern s16 battleEnvTextureRectW, battleEnvTextureRectH, battleEnvTextureRectX, battleEnvTextureRectY;
extern s16 effectMechaEventCount;
extern s16 pendingEffectMechaEvents;
void setupBattleDrawEnvs(u8*, u8*);
void rebuildBattleEnvironmentVramBuffer(void);
void lookAtNoDivide(MATRIX*, SVECTOR*, SVECTOR*, SVECTOR*);
void updateBattleCamera(void);
static s32 fxViewEnabled, fxViewCommands, fxViewWobbles;
static void (*fxViewCapture)(void*);
static void* fxViewCaptureData;
static s32 (*fxViewBounds)(void*, const void*, s32);
static s32 (*fxViewModelCommand)(void*, s32, s32);
static s32 fxViewModels;
static MATRIX fxViewColour;
void battleFxViewInitActor(SpriteActorCore* actor) {
    /* Retail actor setup leaves scratch fields and colour bytes untouched.
     * An isolated preview must not inherit host pointers from a freed task. */
    if (fxViewEnabled) memset(actor, 0, sizeof(*actor));
}
void battleFxViewModels(s32 (*command)(void*, s32, s32)) { fxViewModelCommand = command; }
void battleFxViewArguments(s32 (*bounds)(void*, const void*, s32)) { fxViewBounds = bounds; }
void battleFxScreenViewStart(void);
void battleFxViewCapture(void (*capture)(void*), void* data) {
    fxViewCapture = capture;
    fxViewCaptureData = data;
}
void battleFxViewStart(s32 enabled, s32 models) {
    fxViewEnabled = enabled;
    fxViewCommands = fxViewWobbles = 0;
    fxViewModels = models;
    if (enabled) {
        if (models) {
            memset(&fxViewColour, 0, sizeof(fxViewColour));
            fxViewColour.m[0][0] = fxViewColour.m[1][0] = fxViewColour.m[2][0] = 0x600;
            modelsBattleInit(&fxViewColour, 16, 24, 32);
        }
        /* A flat owned stage gives native environment colour ramps and
         * framebuffer effects their backdrop, without borrowing a map. */
        battleMechas[31] = allocateHeap(sizeof(sLoadedMechas), 0);
        battleMechas[31]->m10E_num118 = 0;
        battleMechas[31]->m118_colorAnims = NULL;
        battleEnvTextureRectW = 16; battleEnvTextureRectH = 1;
        battleEnvTextureRectX = 0; battleEnvTextureRectY = 0x1D0;
        battleBssBlock.mB70_renderStructs[0].m0_drawEnv.r0 =
        battleBssBlock.mB70_renderStructs[1].m0_drawEnv.r0 = 16;
        battleBssBlock.mB70_renderStructs[0].m0_drawEnv.g0 =
        battleBssBlock.mB70_renderStructs[1].m0_drawEnv.g0 = 24;
        battleBssBlock.mB70_renderStructs[0].m0_drawEnv.b0 =
        battleBssBlock.mB70_renderStructs[1].m0_drawEnv.b0 = 32;
        setupBattleDrawEnvs(&battleBssBlock.mB70_renderStructs[0].m0_drawEnv.r0,
                           &battleBssBlock.mB70_renderStructs[1].m0_drawEnv.r0);
        battleCameraEye.vz = -1024;
        previousCameraEye2.vz = -1024 * 65536;
        battleCameraBlock.m4_eyeTarget = battleCameraEye;
        battleCameraBlock.mC_atTarget = battleCameraAt;
    }
}
void battleFxViewBackground(u8* rgb) {
    rebuildBattleEnvironmentVramBuffer();
    rgb[0] = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.r0;
    rgb[1] = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.g0;
    rgb[2] = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.b0;
}
/* The viewer owns every module here. Keep type-specific native helpers
 * away from plain controller actors and replace only the nested frame loop. */
s32 battleFxViewCommand(SpriteActorCore* actor, s32 op, u8* args) {
    if (fxViewEnabled) {
        fxViewCommands++;
        fxViewWobbles += op == 0x19;
        switch (op) {
        case 0x0D: case 0x0E: case 0x17: case 0x21: case 0x22: case 0x28: case 0x29:
        case 0x2A: case 0x2B: case 0x34: case 0x40: case 0x46: case 0x47: case 0x49: case 0x4A: case 0x4B:
            if (fxViewBounds && !fxViewBounds(fxViewCaptureData, args, 1)) return 1;
        }
        if (op == 0x46 || op == 0x47 || op == 0x51) {
            if (op == 0x46) { effectMechaEventCount = 0; pendingEffectMechaEvents = 0; }
            if (fxViewModelCommand) fxViewModelCommand(fxViewCaptureData, op, op == 0x51 ? 0 : args[0]);
            return 1;
        }
        if (op == 0x50 && !fxViewModels) return 1;
        if ((op >= 0x0A && op <= 0x0C) || (op >= 0x49 && op <= 0x4B)) {
            if (!actor->m20_object) return 1;
        }
        if (op == 0x1D || op == 0x2C || (op >= 0x5B && op <= 0x5E)) {
            if (actor->m3C_x0_type != 2 || !actor->m20_object) return 1;
            if (op == 0x1D && !((ModelObject*)actor->m20_object)->m34_modelBlock) return 1;
        }
        if (op == 0x24 || op == 0x25 || op == 0x27) {
            if (actor->m3C_x0_type != 1 || !actor->m20_object) return 1;
        }
        if (op == 0x27 && !actor->m20_object->m2C_subSpriteBuffer) {
            /* JP's helper uses the separately allocated pointer, which
             * is null for native children with inline sub-sprites. */
            SubSprite* sub = actor->m20_object->m30_subSprites;
            s32 i;
            for (i = 0; i < actor->m40_draw.bits.x2_subSpriteCount; i++, sub++) {
                s32 x = (sub->mC_clut & 63) + args[0];
                s32 y = ((sub->mC_clut >> 6) & 511) + args[1];
                sub->mC_clut = x | (y << 6);
            }
            return 1;
        }
        if (op == 0x18 || op == 0x54) {
            if (fxViewCapture) fxViewCapture(fxViewCaptureData);
            if (op == 0x54) { battleFxScreenViewStart(); return 1; }
        }
    }
    (void)args;
    return 0;
}
s32 battleFxViewCommands(void) {
    return fxViewCommands;
}
s32 battleFxViewWobbles(void) {
    return fxViewWobbles;
}
void battleFxViewCamera(MATRIX* view, s32 scene) {
    if (scene) {
        SVECTOR up = {0, 4096, 0, 0};
        updateBattleCamera();
        lookAtNoDivide(view, &battleCameraEye, &battleCameraAt, &up);
    }
    view->t[0] += battleSceneRenderingMatrixOffset.vx;
    view->t[1] += battleSceneRenderingMatrixOffset.vy;
    view->t[2] += battleSceneRenderingMatrixOffset.vz;
    battleIdentityMatrix = *view;
}
s32 battleFxViewSceneVisible(void) { return !battleSpritesDisabled; }
void battleFxViewRenderModels(MATRIX* view, OTag* ot, s32 parity, s32 advance) {
    if (fxViewModels) {
        MATRIX light = {{{0x546, -0xE39, 0x546}, {0, 0, 0}, {0, 0, 0}}, {0, 0, 0}};
        sLoadedMechas* stage = battleMechas[31];
        battleMechas[31] = NULL; /* the flat backdrop has no skeleton */
        modelsBattleRender(view, &light, ot, parity, advance);
        battleMechas[31] = stage;
    }
}
void battleFxViewContext(SpriteActorCore* caster, SpriteActorCore* target, OTag* ot, s32 parity) {
    processBattleAnimationSub0_var1 = caster;
    battleBssBlock.m8C8C_spriteActorCores[0] = caster;
    battleBssBlock.m8C8C_spriteActorCores[1] = target;
    battleBssBlock.m8C54_currentOT = ot;
    battleBssBlock.m8C84_oddOrEven = parity;
}
/* Owned flat ground for the sprite viewer's paired party clips. Native
 * terrain sampling and jump integration need a floor even without a map. */
s32 battleSpriteViewTerrain(SpriteActorCore* actor, SpriteActorCore* target) {
    s32 i;
    battleTerrainVertices = allocateHeap(4 * sizeof(SVECTOR), 0);
    battleTerrainTriangles = allocateHeap(2 * sizeof(sBattleTerrainTriangle), 0);
    if (battleTerrainVertices == NULL || battleTerrainTriangles == NULL) return 0;
    battleTerrainTriangleCount = 2;
    for (i = 0; i < 4; i++) {
        battleTerrainVertices[i].vx = (i & 1) ? 0x1000 : -0x1000;
        battleTerrainVertices[i].vy = 0;
        battleTerrainVertices[i].vz = (i & 2) ? 0x1000 : -0x1000;
        battleTerrainVertices[i].pad = 0;
    }
    for (i = 0; i < 2; i++) {
        sBattleTerrainTriangle* t = &battleTerrainTriangles[i];
        t->m0_vertexIndices[0] = i ? 3 : 0;
        t->m0_vertexIndices[1] = 2;
        t->m0_vertexIndices[2] = 1;
        if (i) { t->m0_vertexIndices[1] = 1; t->m0_vertexIndices[2] = 2; }
        t->m6_neighborIndices[0] = t->m6_neighborIndices[1] = t->m6_neighborIndices[2] = 1 - i;
        t->mC_flags = t->mD_visited = 0;
    }
    actor->m3C_x1A = 0;
    actor->m78_terrainTriangle = target->m78_terrainTriangle = 0;
    /* Helper 61 compares the target with its home position. */
    actor->mA8_x1E = actor->mAC_x0 = 0;
#if VERSION_JP
    target->mA8_x1E = 0; target->mAC_x0 = 1;
#else
    target->mA8_x1E = 1; target->mAC_x0 = 0;
#endif
    battleBssBlock.m4_visualEntities[1].mA_X = target->m0_position.vx >> 16;
    return 1;
}
s32 battleFxViewTrailSegments(void) {
    s32 i, n = 0;
    for (i = 0; battleMechaInitVar3.m0_entries != NULL && i < battleMechaInitVar3.m4_count; i++) {
        n += battleMechaInitVar3.m0_entries[i].m16_age >= 0 &&
             battleMechaInitVar3.m0_entries[i].m16_age < battleMechaInitVar3.m0_entries[i].m1E_lifetime;
    }
    return n;
}
