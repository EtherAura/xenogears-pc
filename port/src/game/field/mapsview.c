/*
 * The map viewer's way into the field overlay (mapsview.h; PC only, called
 * inside an extras sandbox, never by the game).
 */
#include "common.h"
#include "psyq.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "main/memory.h"
#include "main/sound.h"
#include "field/field.h"
#include "field_mecha/mecha.h"
#include "mapsview.h"
#include "pc_convert.h"

/* main */
void updatePartyFileSet(void);
void processLoadedPartyFiles(void);
void shapeTransfert(void);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
void spriteActorSetDirection(SpriteActorCore* actor, s32 direction);
extern GameState* pKernelGameState;
extern s32 currentlyPlayingMusic;
extern s32 gDepthDivider;

/* field */
void allocatePartyCharacterBuffers(void);
void initFieldMouseAndDirectory(void);
void initCompassData(void);
void initFontSystem(void);
void loadInitialField(void);
void initFieldData(void);
void loadFieldGraphics(void);
void brightenFieldCluts(void);
void setupFieldDisplayEnv(void);
void setFieldDrawEnvClip(s32 x, s32 y, s32 w, s32 h);
void initFieldDrawEnvsSub0(s32 w, s32 h);
void renderObjects(void);
void clearShapeTransfertTableEntry(s32 index);
void setCharacterRenderingOT(OTag* ot);
void setCurrentRenderingMatrix(MATRIX* m);
void uploadCharacterSprites(void);
void execSpritesCallbacksList1(void);
void execSpritesCallbacksList2(void);
void OP_INIT_ENTITY_SCRIPT_sub0Sub9(SpriteActorCore* actor);
void renderFieldCharacterSprites(OTag* ot, s32 oddOrEven);
void renderCharShadows(OTag* ot, s32 oddOrEven);
void renderBackgroundPolyIfEnabled(void);
void updateMechasPosForField(void);
extern s32 disableMechaRendering;
extern s32 mechaList3[];
void updateCameraInterpolation(void);
void updateAllEntities(void);
void lookAtDivided(MATRIX* m, VECTOR* eye, VECTOR* at, VECTOR* up);
void setProjectionMatrixForField(void);
void linkOT(OTag* dst, OTag* ot, s32 index);
s32 length2d(s32 x, s32 z);
extern FieldBss_800AF880 fieldSceneState;
extern FieldBss_800B2184 op9DVar1;
extern sFieldRenderContext* pCurrentFieldRenderingContext;
extern s32 fieldDebugDisable;
extern s32 fieldMapNumber;
extern s32 fieldScriptEntityAlreadyInitialized;
extern s32 bootModeReady;
extern s32 playMusicAuthorized;
extern s32 fieldExecuteVar2;
extern s32 fieldExecuteVar3;
extern s32 battlingModeReady;
extern s32 fieldGraphicsUploaded;
extern s32* fieldGraphicsAllocation;
extern s32 fieldBootedOnce;
extern u8 menuReturnState0;
extern s32 fieldRequestedGears;
extern uptr fieldHeapTopMarker;
extern s32 g_frameOddOrEven;
extern s32 fieldUseGroundOT;
extern s32 g_updateAllEntitiesSub2Var0;
extern MATRIX g_cameraMatrix2;
extern s32 cameraDeltaTan;
extern void* fieldTriggerData;
extern u8 renderCharsDisabled;
extern s32 fieldChangePrevented;
extern s32 fieldMusicLoadPending;
extern s32 OPX8E_param0; /* isObjectOcluded's margins around the 320 x 224 screen (a script op sets them) */
extern s32 OPX8E_param1;

/* The OT the viewer draws into: ours, in the sandbox's heap, with room past
 * its end (the render loops index it by depth without a bound). */
#define MAPS_OT_SLACK 0x40000

typedef struct {
    VECTOR eye, at;
    MATRIX camera, camera2, projection, projectionTemp, projectionTemp2;
    s16 cameraTan, camera2Tan;
} MapsCamera;

static MapsCamera mapsGameCamera;
static sFieldRenderContext* mapsContext;

static void mapsSaveCamera(MapsCamera* c) {
    c->eye = fieldSceneState.m0_cameraEye;
    c->at = fieldSceneState.m10_cameraAt;
    c->camera = fieldSceneState.m110_cameraMatrix;
    c->camera2 = g_cameraMatrix2;
    c->projection = fieldSceneState.m1E4_projectionMatrix;
    c->projectionTemp = fieldSceneState.m130_projectionTempMatrix;
    c->projectionTemp2 = fieldSceneState.m224_projectionTempMatrix;
    c->cameraTan = fieldSceneState.m10E_cameraTan;
    c->camera2Tan = fieldSceneState.m10C_camera2Tan;
}

static void mapsLoadCamera(MapsCamera* c) {
    fieldSceneState.m0_cameraEye = c->eye;
    fieldSceneState.m10_cameraAt = c->at;
    fieldSceneState.m110_cameraMatrix = c->camera;
    g_cameraMatrix2 = c->camera2;
    fieldSceneState.m1E4_projectionMatrix = c->projection;
    fieldSceneState.m130_projectionTempMatrix = c->projectionTemp;
    fieldSceneState.m224_projectionTempMatrix = c->projectionTemp2;
    fieldSceneState.m10E_cameraTan = c->cameraTan;
    fieldSceneState.m10C_camera2Tan = c->camera2Tan;
}

/* updateAllEntities' last part: each sprite faces its way as seen from a
 * camera looking along `tan`. */
static void mapsFaceSprites(s32 tan) {
    s32 i;
    sFieldEntity* e;
    sFieldScriptEntity* se;

    for (i = 0; i < g_totalActors; i++) {
        e = &fieldSceneState.m290_actorArray[i];
        if ((e->m58_flags & 0xF40) && !(e->m58_flags & 0x20) && e->m4_pVramSpriteSheet != NULL) {
            se = e->m4C_scriptEntity;
            if (!(se->m4_flags & 0x1000000)) {
                setSpriteActorAngle(e->m4_pVramSpriteSheet, (s16)(tan + se->m108_rotation3));
            } else {
                spriteActorSetDirection(e->m4_pVramSpriteSheet, se->m108_rotation3);
            }
        }
    }
}

/* port/src/game/main/mapsview.c: kernelMain's set-up, from the sound driver on */
void mapsStartKernel(void);

s32 mapsFieldStart(s32 field, const void* state, s32 storyProgress, s32 entry) {
    mapsStartKernel();
    if (state != NULL) {
        gameState = *(const GameState*)state;
    }
    gameState.m231A_fieldID = field;
    if (storyProgress >= 0) {
        gameState.m1930_fieldVarsBackup[0] = storyProgress;
    }
    if (entry >= 0) {
        gameState.m1930_fieldVarsBackup[2] = entry;
    }

    /* fieldEntryPoint */
    fieldDebugDisable = 1;
    fieldWdsSlot1 = wdsFile3;
    fieldWdsSlots[3] = wdsFile5;
    resetMemoryAllocStats(8, 0);
    if (fieldScriptEntityAlreadyInitialized == 0) {
        asyncPartyCharacterLoadingTable[0] = asyncPartyCharacterLoadingTable[1] = asyncPartyCharacterLoadingTable[2] =
            0xFF;
    }
    loadFieldSeq();
    allocatePartyCharacterBuffers();
    bootModeReady = playMusicAuthorized = fieldExecuteVar2 = fieldExecuteVar3 = battlingModeReady = -1;
    fieldGraphicsUploaded = 0;
    initFieldMouseAndDirectory();
    pKernelGameState = &gameState;
    fieldMapNumber = gameState.m231A_fieldID;
    gameState.m1930_fieldVarsBackup[1] = gameState.m2320_worldmapMode;
    gameState.m1930_fieldVarsBackup[4] = gameState.m231C_cameraYaw >> 9;
    menuReturnState0 = 0;
    currentlyPlayingMusic = 0xFF;
    pKernelGameState->m1930_fieldVarsBackup[40] = 1;
    setVar(0x50, 1);
    initCompassData();
    fieldRequestedGears = 0;
    updatePartyFileSet();
    processLoadedPartyFiles();
    fieldHeapTopMarker = (uptr)allocateHeap(4, 1);

    /* bootField */
    initFontSystem();
    setCurrentDirectory(4, 0);
    loadInitialField();
    /* initFieldDrawEnvs without its vblank waits */
    InitGeom();
    SetGeomOffset(0xA0, 0x70);
    SetDefDrawEnv(&fieldRenderContext[0].m0_drawEnv, 0, 0, 0x140, 0xE0);
    SetDefDrawEnv(&fieldRenderContext[1].m0_drawEnv, 0, 0x100, 0x140, 0xE0);
    SetDefDrawEnv(&fieldRenderContext[0].m5C_backgroundRect, 0, 0, 0x140, 0xE0);
    SetDefDrawEnv(&fieldRenderContext[1].m5C_backgroundRect, 0, 0x100, 0x140, 0xE0);
    SetDefDispEnv(&fieldRenderContext[0].mB8_displayEnv, 0, 0x100, 0x140, 0xE0);
    SetDefDispEnv(&fieldRenderContext[1].mB8_displayEnv, 0, 0, 0x140, 0xE0);
    setFieldDrawEnvClip(0, 0, 0x140, 0xE0);
    setupFieldDisplayEnv();
    initFieldDrawEnvsSub0(0x140, 0xF0);
    g_frameOddOrEven = 1;
    fieldBootedOnce = 1;
    setCurrentDirectory(4, 0);
    initFieldData();
    loadFieldGraphics();
    if (fieldGraphicsUploaded == 1) {
        /* the host-file read is done: what bootField does once the CD is idle */
        freeHeap(fieldGraphicsAllocation);
        fieldGraphicsUploaded = 0;
        brightenFieldCluts();
    }
    fieldScriptEntityAlreadyInitialized = 0;
    /* bootField: the field's music would load now; taken as loaded (the
     * scripts wait for it before they change fields) */
    fieldMusicLoadPending = 0;

    if (fieldSceneState.m290_actorArray == NULL || fieldSceneState.m298_walkMesh == NULL) {
        return 0;
    }
    mapsContext = (sFieldRenderContext*)allocateHeap(sizeof(sFieldRenderContext) + MAPS_OT_SLACK, 0);
    return mapsContext != NULL;
}

/* The camera half of updateAllEntities. */
static void mapsStepCamera(void) {
    VECTOR eye;
    VECTOR at;

    fieldSceneState.m10E_cameraTan = ratan2(fieldSceneState.m10_cameraAt.vz - fieldSceneState.m0_cameraEye.vz,
                                       fieldSceneState.m10_cameraAt.vx - fieldSceneState.m0_cameraEye.vx) - 0x400;
    fieldSceneState.m10C_camera2Tan = ratan2(fieldSceneState.m40_cameraAt2.vz - fieldSceneState.m30_cameraEye2.vz,
                                        fieldSceneState.m40_cameraAt2.vx - fieldSceneState.m30_cameraEye2.vx) - 0x400;
    cameraDeltaTan = ratan2(length2d((fieldSceneState.m10_cameraAt.vx - fieldSceneState.m0_cameraEye.vx) >> 16,
                                     (fieldSceneState.m10_cameraAt.vz - fieldSceneState.m0_cameraEye.vz) >> 16),
                            (fieldSceneState.m10_cameraAt.vy - fieldSceneState.m0_cameraEye.vy) >> 16);
    updateCameraInterpolation();
    eye.vx = fieldSceneState.m0_cameraEye.vx + fieldSceneState.m60_cameraOffset.vx;
    eye.vy = fieldSceneState.m0_cameraEye.vy + fieldSceneState.m60_cameraOffset.vy;
    eye.vz = fieldSceneState.m0_cameraEye.vz + fieldSceneState.m60_cameraOffset.vz;
    at.vx = fieldSceneState.m10_cameraAt.vx + fieldSceneState.m60_cameraOffset.vx;
    at.vy = fieldSceneState.m10_cameraAt.vy + fieldSceneState.m60_cameraOffset.vy;
    at.vz = fieldSceneState.m10_cameraAt.vz + fieldSceneState.m60_cameraOffset.vz;
    if (g_updateAllEntitiesSub2Var0 != 0) {
        lookAtDivided(&fieldSceneState.m110_cameraMatrix, &eye, &at, &fieldSceneState.m20_cameraUp);
        g_cameraMatrix2 = fieldSceneState.m110_cameraMatrix;
    } else {
        fieldSceneState.m110_cameraMatrix = g_cameraMatrix2;
        lookAtDivided(&g_cameraMatrix2, &eye, &at, &fieldSceneState.m20_cameraUp);
    }
    setProjectionMatrixForField();
}

void mapsFieldSettleCamera(s32 frames) {
    s32 i;

    for (i = 0; i < frames; i++) {
        mapsStepCamera();
    }
    mapsFaceSprites(fieldSceneState.m10E_cameraTan);
    mapsSaveCamera(&mapsGameCamera);
}

void mapsFieldGameCamera(void) {
    mapsLoadCamera(&mapsGameCamera);
    mapsFaceSprites(fieldSceneState.m10E_cameraTan);
}

void mapsFieldFreeCamera(MATRIX* rotation, s32 eyeX, s32 eyeY, s32 eyeZ, VECTOR* eye, VECTOR* at, s32 tan) {
    MATRIX* p = &fieldSceneState.m1E4_projectionMatrix;
    s32 scale = fieldSceneState.m244_worldScaleFactor;
    s32 r;
    s32 c;
    long long t;

    mapsLoadCamera(&mapsGameCamera);
    fieldSceneState.m0_cameraEye = *eye;
    fieldSceneState.m10_cameraAt = *at;
    fieldSceneState.m10E_cameraTan = tan;
    fieldSceneState.m110_cameraMatrix = *rotation;
    for (r = 0; r < 3; r++) {
        t = 0;
        for (c = 0; c < 3; c++) {
            p->m[r][c] = (s16)(((s32)rotation->m[r][c] * scale) >> 12);
        }
        t = (long long)rotation->m[r][0] * eyeX + (long long)rotation->m[r][1] * eyeY + (long long)rotation->m[r][2] * eyeZ;
        p->t[r] = (s32)(-(t * scale) >> 24);
    }
    SetRotMatrix(p);
    SetTransMatrix(p);
    mapsFaceSprites(fieldSceneState.m10E_cameraTan);
}

/* renderChars, with the sprites' animation step (their callbacks: the
 * sprite bytecode) only when `animate`: still frames draw the same pose. */
static void mapsRenderChars(s32 animate) {
    s32 i;
    sFieldEntity* e;

    if (renderCharsDisabled == 1) {
        return;
    }
    clearShapeTransfertTableEntry(g_frameOddOrEven);
    setCharacterRenderingOT(pCurrentFieldRenderingContext->mCC_OT);
    setCurrentRenderingMatrix(&fieldSceneState.m1E4_projectionMatrix);
    uploadCharacterSprites();
    if (animate) {
        execSpritesCallbacksList1();
        execSpritesCallbacksList2();
    }
    renderFieldCharacterSprites(pCurrentFieldRenderingContext->mCC_OT, g_frameOddOrEven);
    for (i = 0; i < g_totalActors; i++) {
        e = &fieldSceneState.m290_actorArray[i];
        if (!(e->m58_flags & 0x20) && (e->m58_flags & 0x40)) {
            if ((e->m4C_scriptEntity->m4_flags & 0x600) != 0x200 && !(e->m4C_scriptEntity->m4_flags & 0x1000) &&
                !(e->m4C_scriptEntity->m0_fieldScriptFlags & 1)) {
                OP_INIT_ENTITY_SCRIPT_sub0Sub9(e->m4_pVramSpriteSheet);
            }
        } else if (e->m4C_scriptEntity->m4_flags & 0x1000000) {
            OP_INIT_ENTITY_SCRIPT_sub0Sub9(e->m4_pVramSpriteSheet);
        }
    }
    renderCharShadows(pCurrentFieldRenderingContext->mCC_OT, g_frameOddOrEven);
}

/* renderMechasInField's placement and drawing, with its animation tick
 * controlled by the viewer's play/pause state. */
static void mapsRenderMechas(s32 animate) {
    s32 i;
    s32 j = 0;
    sFieldEntity* e;
    sFieldScriptEntity* se;
    sLoadedMechas* m;
    if (disableMechaRendering != 0 || op9DVar1.mE0_numMechas == 0) {
        return;
    }
    updateMechasPosForField();
    SetBackColor(op9DVar1.mD8_mechaBackColor[0], op9DVar1.mD8_mechaBackColor[1], op9DVar1.mD8_mechaBackColor[2]);
    /* renderFieldCharacterSprites also places Gears. Do that branch here
     * so hiding the Sprite layer keeps the Gears at their field positions. */
    for (i = 0; i < g_totalActors && j < 10; i++) {
        e = &fieldSceneState.m290_actorArray[i];
        se = e->m4C_scriptEntity;
        if (!(e->m58_flags & 0x40) || !(se->m4_flags & 0x2000)) {
            continue;
        }
        m = loadedMechas[j];
        if (m != NULL && m->m4_bones != NULL) {
            if (!(se->m0_fieldScriptFlags & 0x10000) && !(se->m14_currentTriangleFlag & 0x200002) && !(se->m4_flags & 0x800)) {
                m->m4A_flags &= ~1;
            } else {
                m->m4A_flags |= 1;
            }
            m->m34_visible = (e->m58_flags & 0x20) == 0;
            if (!(se->m4_flags & 0x20000)) {
                m->m4_bones[0].m54_rotationAngles.vy = se->m108_rotation3 + 0xC00;
            } else {
                se->m106_currentRotation = se->m108_rotation3 = m->m4_bones[0].m54_rotationAngles.vy - 0xC00;
            }
            m->m1C_scale = se->mF4_scale3d[0] * mechaList3[j] >> 12;
            m->m60_groundY = se->m20_position.vy >> 16;
            m->m4_bones[0].m5C_translation[0] = se->m20_position.vx >> 16;
            m->m4_bones[0].m5C_translation[2] = se->m20_position.vz >> 16;
            se->m4_flags &= ~0x200;
        }
        j++;
    }
    renderMechas(&fieldSceneState.m1E4_projectionMatrix, (MATRIX*)op9DVar1.m98_mechaFieldArgs2,
                 pCurrentFieldRenderingContext->mCC_OT, g_frameOddOrEven, animate ? 1 : -1);
}

void mapsFieldDraw(u32 layers, s32 w, s32 h, s32 H, s32 depthShift, s32 farIndex) {
    sFieldRenderContext* ctx = mapsContext;
    s32 depth = gDepthDivider;
    s32 marginX = OPX8E_param0;
    s32 marginY = OPX8E_param1;

    if (ctx == NULL) {
        return;
    }
    ctx->m0_drawEnv = fieldRenderContext[0].m0_drawEnv;
    pCurrentFieldRenderingContext = ctx;
    g_frameOddOrEven = 0;
    ClearOTagR(ctx->m80D4_uiOT, 8);
    ClearOTagR(ctx->mCC_OT, 0x1000);
    ClearOTagR(ctx->m40D0_secondaryOT, 0x1000);
    SetGeomOffset(w / 2, h / 2);
    SetGeomScreen(H);
    initFieldDrawEnvsSub0(w, h);
    gDepthDivider = depthShift;
    /* a screen larger than the game's: the models' culling test (against
     * 320 x 224 plus these margins) keeps what is on it */
    if (w > 0x140 && OPX8E_param0 < w) {
        OPX8E_param0 = w;
    }
    if (h > 0xE0 && OPX8E_param1 < h) {
        OPX8E_param1 = h;
    }
    if (layers & 1) {
        renderObjects();
    }
    if (layers & 2) {
        mapsRenderChars((layers & 0x100) != 0);
    }
    if (layers & 0x20) {
        renderBackgroundPolyIfEnabled();
        mapsRenderMechas((layers & 0x100) != 0);
    }
    gDepthDivider = depth;
    OPX8E_param0 = marginX;
    OPX8E_param1 = marginY;
    /* updateAndRenderField's end: the sprites' pictures into VRAM, the OTs
     * linked and drawn */
    shapeTransfert();
    if (farIndex < 0) {
        farIndex = op9DVar1.m50_linkOTIndex;
    }
    if (fieldUseGroundOT != 0) {
        linkOT(&ctx->mCC_OT[farIndex], ctx->m40D0_secondaryOT, farIndex);
    }
    linkOT(&ctx->m80D4_uiOT[7], ctx->mCC_OT, farIndex);
    DrawOTag(&ctx->m80D4_uiOT[7]);
    pCurrentFieldRenderingContext = &fieldRenderContext[0];
}

void mapsFieldProjection(MATRIX* out) {
    *out = fieldSceneState.m1E4_projectionMatrix;
}

void mapsFieldView(MapsFieldView* out) {
    s32 i;
    s32 n;

    out->objects = fieldSceneState.m28C_totalObjects;
    out->actors = g_totalActors;
    out->layers = fieldSceneState.m2D4_numWalkMesh;
    if (out->layers < 0 || out->layers > 4) {
        out->layers = 0;
    }
    for (i = 0; i < 4; i++) {
        n = i < out->layers ? fieldSceneState.m2C4_walkMeshNumTrianglePerBlock[i] : 0;
        out->tris[i] = n > 0 && n < 0x10000 ? n : 0;
        out->triangles[i] = i < out->layers ? (u8*)fieldSceneState.m2A4_walkMeshTriangle[i] : NULL;
        out->vertices[i] = i < out->layers ? (SVECTOR*)fieldSceneState.m2B4_walkMeshVertices[i] : NULL;
    }
    out->materialFlags = fieldSceneState.m2A0_walkMeshVar1;
    out->walkMesh = (u8*)fieldSceneState.m298_walkMesh;
    out->triggers = (u8*)fieldTriggerData;
    out->eye[0] = fieldSceneState.m0_cameraEye.vx >> 16;
    out->eye[1] = fieldSceneState.m0_cameraEye.vy >> 16;
    out->eye[2] = fieldSceneState.m0_cameraEye.vz >> 16;
    out->at[0] = fieldSceneState.m10_cameraAt.vx >> 16;
    out->at[1] = fieldSceneState.m10_cameraAt.vy >> 16;
    out->at[2] = fieldSceneState.m10_cameraAt.vz >> 16;
    out->scrz = fieldSceneState.m178_sceneSCRZ;
    out->mechas = op9DVar1.mE0_numMechas;
    out->models = fieldSceneState.m294_models != NULL ? fieldSceneState.m294_models[0] : 0;
    out->player = op9DVar1.mE8_playerControlledActor;
    out->music = currentlyPlayingMusic >= 0 && currentlyPlayingMusic < 0x80 ? currentlyPlayingMusic : -1;
    out->script = pCurrentFieldScriptFile;
    out->scriptHeader = (u8*)rawFieldScriptData;
}

void mapsFieldActor(s32 index, MapsFieldActor* out) {
    sFieldEntity* e = &fieldSceneState.m290_actorArray[index];
    sFieldScriptEntity* se;

    out->flags = e->m58_flags;
    out->script = index < g_totalActors;
    out->hidden = (e->m58_flags & 0x20) != 0;
    out->sprite = e->m4_pVramSpriteSheet != NULL;
    /* set from the archive's u16s; the GTE takes them as s16 */
    out->pos[0] = (s16)e->mC_matrix.t[0];
    out->pos[1] = (s16)e->mC_matrix.t[1];
    out->pos[2] = (s16)e->mC_matrix.t[2];
    out->rot[0] = e->m50_modelRotation.vx;
    out->rot[1] = e->m50_modelRotation.vy;
    out->rot[2] = e->m50_modelRotation.vz;
    out->direction = 0;
    out->walkmesh = out->triangle = out->character = -1;
    out->model = out->npc = -1;
    if (!(e->m58_flags & 0x40) && e->m0_model != NULL && fieldSceneState.m294_models != NULL) {
        s32* models = fieldSceneState.m294_models;
        s32 k;
        for (k = 0; k < models[0]; k++) {
            if (pcModelBlocks((u8*)models + models[k + 1]) == e->m0_model->m4_pModelBlock) {
                out->model = k;
                break;
            }
        }
    }
    if (out->script && e->m4C_scriptEntity != NULL) {
        se = e->m4C_scriptEntity;
        out->pos[0] = se->m20_position.vx >> 16;
        out->pos[1] = se->m20_position.vy >> 16;
        out->pos[2] = se->m20_position.vz >> 16;
        out->direction = se->m108_rotation3;
        out->walkmesh = se->m10_walkmeshId;
        if (out->walkmesh >= 0 && out->walkmesh < 4) {
            out->triangle = se->m8_currentWalkMeshTriangle[out->walkmesh];
        }
        if (out->sprite && !(se->m4_flags & 0x2000)) {
            if (se->m126_spriteSource & 0x80) out->npc = se->m126_spriteSource & 0x7F;
            else out->character = se->mE4_playableCharacterId;
        }
        out->pc = se->mCC_scriptPC;
        out->slot = se->mCE_currentScriptSlot;
        out->priority = se->m8C_scriptSlots[se->mCE_currentScriptSlot & 7].m4_x12_priority;
    }
}

s32 mapsFieldModels(MapsFieldModel* out, s32 max) {
    s32 i;
    s32 n = 0;
    sFieldEntity* e;

    for (i = 0; i < fieldSceneState.m28C_totalObjects && n < max; i++) {
        e = &fieldSceneState.m290_actorArray[i];
        if ((e->m58_flags & 0x40) || e->m0_model == NULL || e->m0_model->m4_pModelBlock == NULL) {
            continue;
        }
        out[n].block = e->m0_model->m4_pModelBlock;
        out[n].prims = e->m0_model->m8_primBuffers[0];
        out[n].m = e->mC_matrix;
        out[n].m.t[0] = (s16)out[n].m.t[0];
        out[n].m.t[1] = (s16)out[n].m.t[1];
        out[n].m.t[2] = (s16)out[n].m.t[2];
        out[n].entity = i;
        out[n].hidden = (e->m58_flags & 0x20) != 0;
        out[n].flags = e->m58_flags;
        n++;
    }
    return n;
}

s32 mapsFieldProbe(s32 x, s32 y, s32 z, s32 frames, s32* field, s32* entry) {
    s32 player = op9DVar1.mE8_playerControlledActor;
    s32 map = fieldMapNumber;
    s32 walk;
    s32 tri;
    s32 i;
    sFieldScriptEntity* se;
    SVECTOR at;
    VECTOR normal;

    if (player < 0 || player >= g_totalActors) {
        return 0;
    }
    se = fieldSceneState.m290_actorArray[player].m4C_scriptEntity;
    walk = se->m10_walkmeshId;
    if (walk >= 0 && walk < fieldSceneState.m2D4_numWalkMesh) {
        tri = findTriangleInWalkMesh(x, z, walk, &at, &normal);
        if (tri != 0 || at.vx != 0 || at.vz != 0) {
            se->m8_currentWalkMeshTriangle[walk] = tri;
            y = at.vy;
        }
    }
    se->m20_position.vx = x << 16;
    se->m20_position.vy = y << 16;
    se->m20_position.vz = z << 16;
    for (i = 0; i < frames; i++) {
        updateAllEntities();
        if (fieldMapNumber != map || fieldChangePrevented == 0) {
            *field = fieldMapNumber & 0xFFF;
            *entry = fieldVars[2];
            return 1;
        }
        if (fieldExecuteVar3 == 0) {
            *field = pKernelGameState->m231A_fieldID;
            *entry = pKernelGameState->m231E_worldmapInitialPositionIndex;
            return 2;
        }
    }
    return 0;
}

/* No interpreter, clock, camera or renderer is called by inspection. */
s32 mapsFieldVM(s32 actor, MapsFieldVM* out, s32* vars) {
    s32 i;
    sFieldScriptEntity* e;
    if (!out || !vars || actor < 0 || actor >= g_totalActors || !fieldSceneState.m290_actorArray) return 0;
    e = fieldSceneState.m290_actorArray[actor].m4C_scriptEntity;
    if (!e || !rawFieldScriptData) return 0;
    out->pc = e->mCC_scriptPC; out->slot = e->mCE_currentScriptSlot & 7;
    out->routine = e->m8C_scriptSlots[out->slot].m3_scriptIndex;
    for (i = 0; i < 8; i++) {
        out->slotPC[i] = e->m8C_scriptSlots[i].m0_scriptPC;
        out->slotRoutine[i] = e->m8C_scriptSlots[i].m3_scriptIndex;
        out->priority[i] = e->m8C_scriptSlots[i].m4_x12_priority;
        out->delay[i] = e->m8C_scriptSlots[i].m2_delay;
    }
    for (i = 0; i < 0x400; i++) vars[i] = getVariable(i * 2);
    return 1;
}
