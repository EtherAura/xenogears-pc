/* Battling overlay: the gear viewer / results screen (0x800725B0-0x80072D18;
 * .rodata 0x8006FBC8-0x8006FBF8). Hand-written helpers follow as their own
 * assembly object (0x80072D18-0x800732CC). */

#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "main/dialog.h"
#include "main/memory.h"
#include "main/debug_font.h"
#include "battling/battling.h"

void LoadAverageShort12(SVECTOR*, SVECTOR*, s32, s32, SVECTOR*);
u16 LoadClut2(u32* clut, s32 x, s32 y);
MATRIX* MulMatrix2(MATRIX* m0, MATRIX* m1);
s32 getInputOverflowed(void);
s32 loadInputFromVSyncBuffer(void);
s32 ratan2(s32 y, s32 x);
void resetInputs(void);
s32 sprintf(char* buf, const char* fmt, ...);
void printfStub(); /* main: empty stub (debug print), called with a string */
void patchSelfModifyingCode(s32, s32, s32, s32);
s32 isControllerConnected(s32 port);
u8 getDpadStickX(s32 buttons);
u8 getDpadStickY(s32 buttons);
void startPadVibration(); /* main: (s32 port, s16 duration); called unprototyped */
void mergeQueuedInputs(void);
void setDebugFontLineStart(s32 x, s32 y);
void createRotationMatrix(SVECTOR* rot, MATRIX* m); /* main: Noah createRotationMatrix */
s32 getAngleCos(s32 angle);
s32 getAngleSin(s32 angle);
void moveBattlingCameraTargetTo(VECTOR* v, s32 n);
void moveBattlingCameraTo(VECTOR* v, s32 n);
void resetBattlingEvent(void);
void startBattlingEvent(void);
void updateBattlingEvent(void);
void endBattlingVictoryScreen(void);
void scaleBattlingSVector(SVECTOR*, SVECTOR*, s32);
void copyBattlingWords(void* dst, void* src, s32 size);
void createBattlingHitSparkEmitter(void);
s32 updateBattlingProjectiles(BattlingGear* gear);
void getBattlingGearPartPosition(BattlingGear* gear, s32 partIndex, s32 vertex, VECTOR* out);
void ageBattlingGearHitShapes(BattlingGear* gear);
s32 addBattlingGearGauge(BattlingGear* gear, s32 amount, s32 mode);
void runBattlingGearAttackCommand(BattlingGear* gear, BattlingEffectCmd* cmd, s32 firstFrame);
void runBattlingGearAnimEvents(BattlingGear* gear, s16 frame, s16 count);
void resetBattlingGearPartVisibility(BattlingGear* g);
void playBattlingGearReplayFrame(BattlingGear* g);
void updateBattlingGearAnimation(BattlingGear* g);
void updateBattlingRound(BattlingGear* a, BattlingGear* b);
s32 isBattlingCircleOnSegment(s32 x0, s32 z0, s32 x1, s32 z1, s32 px, s32 pz, s32 r);
s32 intersectBattlingCircleSegment(s32 x0, s32 z0, s32 x1, s32 z1, s32 px, s32 pz, s32 r);
s32 isBattlingCircleOnQuad(VECTOR* q, s32 x, s32 z, s32 r);
s32 checkBattlingGearHits(BattlingGear* g);
void pushBattlingGearInput(BattlingGear* g, u8 v);
s32 popBattlingGearInput(BattlingGear* g);
void clearBattlingGearInput(BattlingGear* g);
void beginBattlingGearFrame(BattlingGear* g);
s32 tryBattlingGearBackDash(BattlingGear* g);
s32 readBattlingGearPadInput(BattlingGear* g);
s32 updateBattlingGearHealth(BattlingGear* g);
s32 pushBattlingGearKnockback(BattlingGear* g, s32 angle, s32 shift, s32 dy);
void knockBattlingGearDown(BattlingGear* g, s32 angle, s32 shift, s32 dy);
s32 vibrateBattlingGearPad(BattlingGear* g, s32 duration);
s32 reactBattlingGearToHit(BattlingGear* g);
BattlingMove* advanceBattlingGearCombo(BattlingGear* g, s32 which);
void setBattlingGearFootEffectFlag(BattlingGear* g);
s32 updateBattlingGearActions(BattlingGear* g);
void snapBattlingGearToFeet(BattlingGear* g);
void computeBattlingGearVelocity(BattlingGear* g);
s32 pickBattlingGearContactPoint(VECTOR* p, VECTOR* q, BattlingGear* g, VECTOR** out);
s32 separateBattlingGears(BattlingGear* a, BattlingGear* b);
void func_80078ED4(s16* arg0);
void initBattlingGear(BattlingGear* gear);
void updateBattlingGearBodyPoints(BattlingGear* gear);
void animateBattlingVictoryGear(BattlingGear* gear, s32 arg1);
void updateBattlingSpriteEffects(void);
void clearBattlingSpriteEffects(void);
void drawBattlingSpriteEffects(MATRIX* m, MATRIX* rot, OTag* ot);
void setBattlingEffectColor(CVECTOR* arg0);
void spawnBattlingSpriteEffect(SVECTOR* pos, s32 type);
void drawBattlingTrailEffects(MATRIX* m, MATRIX* rot, OTag* ot);
void spawnBattlingTrailEffect(s32 arg0, VECTOR* pos, s32 owner, s32 arg3);
void drawBattlingBeamEffects(MATRIX* m, s32 arg1, OTag* ot);
u32 makeBattlingEffectHandle(s32 arg0, s32 arg1, s32 arg2, s32 arg3);
void spawnBattlingBeamEffect(s32 arg0, VECTOR* pos, VECTOR* pos2, s32 owner);
s32 spawnBattlingHitEffect(VECTOR* arg0, s32 arg1);
s32 isBattlingTwoPointEffect(s32 arg0);
void jitterBattlingVector(VECTOR* arg0, VECTOR* arg1);
void spawnBattlingLightning(VECTOR* from, VECTOR* to, s32 type);
void spawnBattlingLightningKind(VECTOR* arg0, VECTOR* arg1, s32 arg2);
void initBattlingDust(void);
void spawnBattlingDust(VECTOR* pos, s32 count);
void drawBattlingDust(OTag* ot);
void initBattlingSplash(void);
void spawnBattlingSplash(VECTOR* a, VECTOR* b);
void drawBattlingSplash(OTag* ot);
void clearBattlingSplashAndDust(void);
void initBattlingLines(void);
void addBattlingLine(VECTOR* from, VECTOR* to, CVECTOR* color);
void drawBattlingLines(OTag* ot);
void showBattlingAnalogWarning(s32 arg0);
void setBattlingTextPos(s32, s32);
void setBattlingTextScale(s32);
void printBattlingText(u8* str);
void printBattlingTextCentered(u8* str);
void hideBattlingHelpLabels(void);
u8* getBattlingComLevelName(void);
void openBattlingPauseMenu(s32);
void clearBattlingGlyphs(void);
void drawBattlingTextOverlay(void* ot);
s32 getBattlingGroundHeight(VECTOR* pos, s32 flag);
u32 getBattlingMapCell(VECTOR* pos); /* the map cell word (flags in bits 24-25) */
void clampBattlingMoveToArena(VECTOR* a, VECTOR* b, s32 c);
void startBattlingIntroCamera(void);
void turnBattlingOrbitCamera(s32);
void startBattlingOrbitCamera(void);
void setBattlingSceneMode(s32);
s32 getBattlingSceneMode(void);
void printBattlingFightTime(void);
void initBattlingGearShadowQuad(BattlingGear* gear);
void normalizeBattlingVectorS(VECTOR*, SVECTOR*);
s32 getBattlingVectorLength(VECTOR*);
s32 getBattlingVectorLengthXZ(VECTOR* v); /* xz length */
s32 getBattlingDistance(VECTOR* a, VECTOR* b); /* distance */
s32 getBattlingDistanceXZ(VECTOR* a, VECTOR* b); /* xz distance */
void loadBattlingSettings(void);
void markBattlingGearDefeated(); /* void(void) in bl_80083DCC; updateBattlingGearActions passes an argument */
void initBattlingDisplayAndDrawEnvs(s32, s32);
void freeBattlingNodeTree(BattlingNode*);
BattlingLayer* createBattlingLayer(u16 otSize);
BattlingScene* createBattlingScene(BattlingLayer* layer);
void freeBattlingScene(BattlingScene*);
void renderBattlingNodeTree(BattlingNode*);
void beginBattlingLayer(BattlingLayer* layer);
void addBattlingLayerToOt(BattlingLayer* layer);
void resetBattlingAnim(BattlingAnim* bone);
s32 stepBattlingAngleTowards(s32 cur, s32 target, s32 step);
s32 advanceBattlingAnim(BattlingAnim* bone, s32 arg1, s32 arg2);
void yieldBattlingThread(void);
BattlingNode* createBattlingGearShadow(BattlingNode* src);
void renderBattlingShadowTree(BattlingNode*);
BattlingEmitter* createBattlingEmitter(s32 type, s32 shape);
void clearBattlingEmitterParticles(BattlingEmitter* pool);
void allocBattlingEmitterParticles(BattlingEmitter* pool, s32 count);
void emitBattlingParticles(BattlingEmitter* pool, MATRIX* m, s32 count);
void updateBattlingEmitterParticles(BattlingEmitter* pool, OTag* arg1, MATRIX* m);
void createBattlingSparkEmitter(void);
void setBattlingSparkBurst(s32 arg0);
void initBattlingSoundChannels(void);
void playBattlingSfx(s32);
void playBattlingGearSound(BattlingGear* gear, s32 id, VECTOR* pos, s32 mode);
s32 playBattlingGearHitSound(BattlingGear* gear, s32 kind, VECTOR* pos, s32 mode);
s32 stopBattlingGearHitSound(BattlingGear* gear, s32 kind);
void updateBattlingPracticeAi(BattlingGear* gear);
s32 isBattlingGearHpAbove(BattlingGear* g, s32 fraction);
s32 isBattlingGearInWater(BattlingGear* g);
void initBattlingAi(BattlingGear* gear);
void updateBattlingAi(BattlingGear* gear, BattlingAiState* unused);
extern u8 battlingEntryParams; /* main: byte 0 (mode) of the field->battling entry block; the field writes it as battlingEntryParams[0..5] */
extern u8 pad0LeftStickX;
extern u8 pad1LeftStickX;
extern u8 pad0LeftStickY;
extern u8 pad1LeftStickY;
extern u16 newPadButtonForDialogs; /* main: pad 0 newly pressed */
extern u16 newPadButtonForDialogs2; /* main: pad 1 newly pressed */
extern u16 newPadButtonForField; /* main: pad 0 auto-repeat */
extern u16 newPadButtonForField2;
extern u16 newPadButtonForScripts; /* main: pad 0 buttons */
extern u16 newPadButtonForScripts2; /* pad 1 buttons */
/* "COM2 VICTORY": retail pads it with stray bytes (US 96 08, JP 77 52) before
 * spawnBattlingHitEffect's jump table, so it stays assembly .rodata. */
extern char D_8006FDD8[];
extern BattlingScene* battlingVictoryScene;
extern BattlingProjectileKind battlingProjectileKinds[];
extern char* battlingEtherMoveName;
extern BattlingAnimInfo battlingGearAnimInfo[36]; /* to battlingComboNextMoves */
extern u8 battlingComboNextMoves[0x20];
extern char* battlingMoveNames[];
extern s32 battlingDebugEnabled;
extern u8 battlingEffect1FrameU[16];
extern u8 battlingEffect1FrameV[16];
extern u8 battlingEffect2FrameU[16];
extern u8 battlingEffect2FrameV[12];
extern u8 battlingEffect4FrameV[16];
extern s16 battlingTrailWidths[];
extern BattlingGearEntry battlingGearEntries[49];
extern MATRIX battlingIdentityMatrix;
#if VERSION_JP
extern u8 D_80091C98; /* JP-only (a JP address; updateBattlingFight) */
#endif
extern BattlingNode* battlingVictoryShadow;
extern BattlingGear* battlingVictoryGear;
extern u8 battlingVictoryTextToggle;
extern s32 battlingVictoryPitch;
extern s32 battlingVictorySpin;
extern s32 battlingVictoryY;
extern s32 battlingVictoryZ;
extern s32 battlingVictoryScale;
extern SVECTOR battlingVictoryRotation; /* rotation for createRotationMatrix (vy: D_80092632) */
extern s32 battlingRoundOver;
extern s32 battlingRoundIntroTimer;
extern s32 battlingKoTimer;
extern BattlingEmitter* battlingHitSparkEmitter; /* tutorial light (createBattlingHitSparkEmitter) */
extern s32 battlingHitSparkCount;
extern BattlingMove battlingIdleMove;
extern s32 battlingHitShapeCount;
extern s32 battlingHitPointX;
extern s32 battlingHitPointZ;
extern u8 battlingFrameSkipTimer;
extern s32 battlingWinCamAngle;
extern s32 battlingWinCamTargetHeight;
extern s32 battlingWinCamHeight;
extern s32 battlingWinCamDistance;
extern s16 battlingShadowTimX;
extern s16 battlingShadowTimY;
extern u16 battlingShadowTimClut;
extern u16 battlingShadowTimTPage;
extern s16 battlingTrailU;
extern s16 battlingTrailV;
extern u16 battlingTrailClut;
extern u16 battlingTrailTPage;
extern s16 battlingGearColorClutX;
extern s16 battlingGearColorClutY;
extern u16 battlingGearColorClut;
extern s32 battlingEffectFrame;
extern u16 battlingGearColorClutEntries[4];
extern s32 battlingLineCount;
extern s32 battlingDrawnLineCount;
extern CVECTOR battlingEffectColor;
extern BattlingDust* battlingDustParticles;
extern TILE_1* battlingDustTiles[2]; /* D_800926C4 = [1] */
extern BattlingDebris* battlingSplashParticles;
extern TILE* battlingSplashTiles[2]; /* D_800926D0 = [1] */
extern s32 battlingGearDistance;
extern s32 battlingGearDistance3D;
extern u8 battlingCameraLag;
extern u8 battlingRubberBandMode;
extern s32 battlingRoundWinner;
extern u8 battlingBufferIndex;
extern s32 battlingResultTimer;
extern u8 battlingStageColorIndex;
extern u8 battlingReplayIndex;
extern u8 battlingEventFight;
extern s32 battlingGameMode;
extern u16 battlingDebugDisplayFlags; /* debug display flags */
extern u8 battlingFightActive;
extern BattlingMapCell* battlingHeightMap; /* 128x128 ground map */
extern s32 battlingFrameCounter;
extern u8 D_800928F0;
extern u8 battlingCameraSide;
extern u8 battlingActivePad;
extern s32 battlingCameraSideAngle;
extern s32 battlingDrawGameCount;
extern s32 battlingSpeedScale;
extern s32 battlingGearAngle;
extern OTag* battlingCurrentOt; /* current OT */
extern u8 battlingEventStarted;
extern s32 battlingFightTime;
extern s32 battlingRoundFrames;
extern s32 battlingRoundNumber;
extern VECTOR battlingHitSparkPos;
extern VECTOR battlingResultGearPoints[4];
extern BattlingEffectTemplate battlingEffectTemplates[5];
extern BattlingEffect battlingSpriteEffects[60];
extern u8 battlingEffect0FrameU[12];
extern u8 battlingEffect0FrameV[12];
extern u16 battlingEffect0FrameCluts[12];
extern BattlingDebugLine battlingLines[100];
extern VECTOR battlingViewOrigin;
extern BattlingSideState battlingSideStates[2];
extern MATRIX battlingAspectMatrix;
extern BattlingGear battlingGear2;
extern VECTOR battlingCameraTarget; /* camera target */
extern DialogWindow18 battlingMessageWindow;
extern VECTOR battlingCameraPos; /* camera position */
extern BattlingGear battlingGear1;
extern BattlingSettings battlingSettings;

void startBattlingVictoryScreen(BattlingGear* gear) {
    BattlingGearParams* params;

    patchSelfModifyingCode(5, 4, 0x40, 0x40);
    battlingVictoryScene = createBattlingScene(createBattlingLayer(0x10));
    battlingVictoryShadow = createBattlingGearShadow(gear->m5C_model);
    initBattlingDisplayAndDrawEnvs(0x280, 0xDA);
    SetGeomScreen(0x400);
    battlingDebugDisplayFlags = 0;
    battlingVictoryGear = gear;
    initBattlingGear(gear);
    battlingViewOrigin.vz = 0;
    battlingViewOrigin.vy = 0;
    battlingViewOrigin.vx = 0;
    params = gear->m8FC_params;
    battlingVictoryTextToggle = 1;
    battlingVictoryPitch = params->m14_victoryPitch;
    battlingVictorySpin = params->m16_victorySpin;
    battlingVictoryY = params->m18_victoryY;
    battlingVictoryZ = params->m1A_victoryZ;
    battlingVictoryScale = params->m1C_victoryScale;
    battlingVictoryRotation.vy = params->m1E_victoryYaw;
}

void endBattlingVictoryScreen(void) {
    yieldBattlingThread();
    hideBattlingHelpLabels();
    patchSelfModifyingCode(1, 1, 0x40, 0x40);
    freeBattlingScene(battlingVictoryScene);
    freeBattlingNodeTree(battlingVictoryShadow);
    initBattlingDisplayAndDrawEnvs(0x140, 0xDA);
    SetGeomScreen(0xC0);
    setBattlingSceneMode(3);
    setBattlingTextScale(0x100);
    clearBattlingGlyphs();
}

void computeBattlingShadowMatrix(BattlingNode* model, MATRIX* m, MATRIX* out) {
    MATRIX local;

    *out = *m;
    local = battlingIdentityMatrix;
    local.t[0] = model->m34_translation.vx - battlingViewOrigin.vx;
    local.t[1] = -battlingViewOrigin.vy;
    local.t[2] = model->m34_translation.vz - battlingViewOrigin.vz;
    CompMatrix(m, &local, &local);
    out->t[0] = local.t[0];
    out->t[1] = local.t[1];
    out->t[2] = local.t[2];
}

void updateBattlingVictoryScreen(BattlingScene* ctx) {
    BattlingGear* gear;
    s32 y;
    MATRIX unused1;
    MATRIX m;
    MATRIX unused2;
    char buf[64];

    gear = battlingVictoryGear;
    mergeQueuedInputs();
    if (gear->m909_gearId != 7) {
        if (newPadButtonForScripts & 0x2000) {
            battlingVictorySpin--;
        }
        if (newPadButtonForScripts & 0x8000) {
            battlingVictorySpin++;
        }
    }
    if (battlingVictorySpin < -0x80) {
        battlingVictorySpin = -0x80;
    }
    if (battlingVictorySpin > 0x80) {
        battlingVictorySpin = 0x80;
    }
    if (battlingActivePad == 1) {
        if (newPadButtonForDialogs2 & 0x20) {
            endBattlingVictoryScreen();
            return;
        }
        if (newPadButtonForDialogs2 & 1) {
            battlingVictoryTextToggle++;
        }
    } else {
        if (newPadButtonForDialogs & 0x20) {
            endBattlingVictoryScreen();
            return;
        }
        if (newPadButtonForDialogs & 1) {
            battlingVictoryTextToggle++;
        }
    }
    if (battlingVictoryTextToggle & 1) {
        extern s32 battlingGlyphCount; /* the text glyphs so far */
        s32 glyphs = battlingGlyphCount;
        if (battlingGameMode == 2 || battlingGameMode == 3) {
            y = 0x9A;
        } else {
            y = 0x86;
        }
        setBattlingTextScale(0x1C0);
        setBattlingTextPos(0x18, y & 0xFF);
#if VERSION_JP
        printBattlingText("RESULT");
#else
        printBattlingText("WINNER");
#endif
        if (!(battlingGameMode == 2 || battlingGameMode == 3)) {
            printBattlingText("LEVEL");
        }
#if VERSION_JP
        printBattlingText("FIGHT");
#else
        printBattlingText("MATCHES");
#endif
        printBattlingText("TIME");
#if VERSION_JP
        setBattlingTextPos(0x88, y & 0xFF);
#else
        setBattlingTextPos(0xA8, y & 0xFF);
#endif
        printBattlingText(battlingGearEntries[gear->m909_gearId].m8_name);
        if (!(battlingGameMode == 2 || battlingGameMode == 3)) {
            sprintf(buf, "%s", getBattlingComLevelName());
            printBattlingText(buf);
        }
        sprintf(buf, "%d/%d VS %s", gear->mF2_wins, battlingRoundNumber, battlingGearEntries[gear->mD8_opponent->m909_gearId].m8_name);
        printBattlingText(buf);
        printBattlingFightTime();
        pcWideBattlingGlyphs(glyphs, battlingGlyphCount, 0); /* the result to the left edge (wide.c) */
    }
    drawBattlingTextOverlay(battlingCurrentOt);
    battlingVictoryRotation.vx = battlingVictoryPitch;
    battlingVictoryRotation.vy += battlingVictorySpin;
    battlingVictoryRotation.vz = 0;
    createRotationMatrix(&battlingVictoryRotation, &m);
    MulMatrix2(&battlingAspectMatrix, &m);
    m.t[0] = 0;
    m.t[1] = battlingVictoryY;
    m.t[2] = battlingVictoryZ;
    beginBattlingLayer(ctx->m284_layer);
    animateBattlingVictoryGear(gear, 0);
    gear->m5C_model->m34_translation.vx = gear->m5C_model->m34_translation.vy = gear->m5C_model->m34_translation.vz = 0;
    gear->m5C_model->m44_rotation.vy = 0;
    NODE_SKELETON(gear->m5C_model)->m10_scale[0] = NODE_SKELETON(gear->m5C_model)->m10_scale[1] = NODE_SKELETON(gear->m5C_model)->m10_scale[2] = battlingVictoryScale;
    gear->m60_node->mC_worldMatrix = m;
    renderBattlingNodeTree(gear->m5C_model);
    addBattlingLayerToOt(ctx->m284_layer);
    beginBattlingLayer(battlingVictoryScene->m284_layer);
    computeBattlingShadowMatrix(gear->m5C_model, &m, &battlingVictoryShadow->mC_worldMatrix);
    renderBattlingNodeTree(battlingVictoryShadow);
    gte_SetRotMatrix(&battlingVictoryShadow->mC_worldMatrix);
    gte_SetTransMatrix(&battlingVictoryShadow->mC_worldMatrix);
    renderBattlingShadowTree(battlingVictoryShadow);
    addBattlingLayerToOt(battlingVictoryScene->m284_layer);
}
