/* Battling overlay, 0x800732CC-0x80075060 (.rodata from 0x8006FBF8): attacks,
 * projectiles and hit shapes. It starts after the hand-written block. */

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

void createBattlingHitSparkEmitter(void) {
    BattlingEmitter* light;

    light = createBattlingEmitter(3, 0);
    light->m74_r = 0x80;
    light->m75_g = 0x80;
    light->m76_b = 0xC0;
    allocBattlingEmitterParticles(light, 0x60);
    light->m0_gravity = 4;
    light->m44_spread = 0x300;
    light->m48_speedMin = 8;
    light->m4A_speedRange = 0x20;
    light->m68 = 0;
    light->m6A_life = 0x20;
    battlingHitSparkEmitter = light;
}

void updateBattlingHitSparks(OTag* arg0, MATRIX* m) {
    BattlingEmitter* light;

    light = battlingHitSparkEmitter;
    light->m14_x = battlingHitSparkPos.vx;
    light->m16_y = battlingHitSparkPos.vy;
    light->m18_z = battlingHitSparkPos.vz;
    light->m34_rotation.vx = 0;
    light->m34_rotation.vy = 0;
    light->m34_rotation.vz = 0;
    emitBattlingParticles(light, &battlingIdentityMatrix, battlingHitSparkCount);
    gte_SetTransMatrix(m);
    gte_SetRotMatrix(m);
    updateBattlingEmitterParticles(light, arg0, m);
}

void fireBattlingProjectile(VECTOR* pos, VECTOR* from, BattlingGear* gear, s32 kind, s32 arg4, s32 arg5) {
    VECTOR dir;
    VECTOR delta;
    BattlingProjectile* shot;
    BattlingProjectileKind* info;
    BattlingGear* opponent;
    s32 i;
    SVECTOR unused;

    for (i = 0; i < 9; i++) {
        if (gear->m64C_projectiles[i].m30_active == 0) {
            shot = &gear->m64C_projectiles[i];
            break;
        }
    }
    if (i == 8) {
        return;
    }
    opponent = gear->mD8_opponent;
    delta.vx = opponent->m0_position.vx - pos->vx;
    delta.vy = opponent->m0_position.vy - pos->vy - 0x90;
    delta.vz = opponent->m0_position.vz - pos->vz;
    info = &battlingProjectileKinds[kind];
    if (from != NULL) {
        dir.vx = pos->vx - from->vx;
        dir.vy = pos->vy - from->vy;
        dir.vz = pos->vz - from->vz;
    } else {
        dir = delta;
    }
    normalizeBattlingVectorS(&dir, &shot->m28_direction);
    shot->m3E_homing = info->m0_homing;
    shot->m31_speed = info->m4_speed;
    shot->m40_life = info->m3_life;
    shot->m32_drawType = info->m2_drawType;
    shot->m33_moveType = info->m5_moveType;
    shot->m38_handle = arg5;
    shot->m3C_damage = arg4;
    if (info->m6_sound != 0) {
        playBattlingGearHitSound(gear, info->m6_sound, shot, 2);
    }
    scaleBattlingSVector(&shot->m28_direction, &shot->m20_velocity, shot->m31_speed);
    shot->m0_position = *pos;
    shot->m10_prevPosition = shot->m0_position;
    shot->m30_active = 1;
}

s32 updateBattlingProjectiles(BattlingGear* gear) {
    BattlingGear* opponent;
    BattlingProjectile* shot;
    s32 i;
    s32 dist;
    SVECTOR half;
    VECTOR delta;
    SVECTOR norm;
    u8 color[4];

    opponent = gear->mD8_opponent;
    gear->m8F4_nearestProjectileDist = 0x10000;
    setBattlingEffectColor(&gear->m15D4_color);
    for (i = 0; i < 8; i++) {
        shot = &gear->m64C_projectiles[i];
        if (shot->m30_active == 0) {
            continue;
        }
        if (--shot->m40_life == -1) {
            shot->m30_active = 0;
            continue;
        }
        if (getBattlingGroundHeight(shot, 0) < shot->m0_position.vy) {
            shot->m30_active = 0;
            spawnBattlingHitEffect(&shot->m0_position, 1);
            continue;
        }
        shot->m10_prevPosition = shot->m0_position;
        delta.vx = opponent->m94C_target.vx - shot->m0_position.vx;
        delta.vy = opponent->m94C_target.vy - shot->m0_position.vy;
        delta.vz = opponent->m94C_target.vz - shot->m0_position.vz;
        dist = getBattlingVectorLength(&delta);
        shot->m34_distance = dist;
        if (dist < gear->m8F4_nearestProjectileDist) {
            gear->m8F4_nearestProjectileDist = dist;
            gear->m8F8_nearestProjectile = shot;
        }
        VectorNormalS(&delta, &norm);
        LoadAverageShort12(&norm, &shot->m28_direction, shot->m3E_homing, 0x1000 - shot->m3E_homing, &shot->m28_direction);
        scaleBattlingSVector(&shot->m28_direction, &shot->m20_velocity, shot->m31_speed);
        switch (shot->m32_drawType) {
        case 0:
            half.vx = shot->m20_velocity.vx;
            half.vy = shot->m20_velocity.vy;
            half.vz = shot->m20_velocity.vz;
            half.vx /= 2;
            half.vy /= 2;
            half.vz /= 2;
            shot->m0_position.vx += half.vx;
            shot->m0_position.vy += half.vy;
            shot->m0_position.vz += half.vz;
            spawnBattlingHitEffect(&shot->m0_position, 10);
            shot->m0_position.vx += half.vx;
            shot->m0_position.vy += half.vy;
            shot->m0_position.vz += half.vz;
            spawnBattlingHitEffect(&shot->m0_position, 10);
            break;
        case 1:
            shot->m0_position.vx += shot->m20_velocity.vx;
            shot->m0_position.vy += shot->m20_velocity.vy;
            shot->m0_position.vz += shot->m20_velocity.vz;
            spawnBattlingHitEffect(&shot->m0_position, 2);
            break;
        case 2:
            color[0] = 0xFF;
            color[2] = 0x40;
            color[1] = ((battlingFrameCounter + i) << 6) - 1;
            shot->m0_position.vx += shot->m20_velocity.vx;
            shot->m0_position.vy += shot->m20_velocity.vy;
            shot->m0_position.vz += shot->m20_velocity.vz;
            addBattlingLine(&shot->m10_prevPosition, &shot->m0_position, color);
            break;
        case 3:
            shot->m0_position.vx += shot->m20_velocity.vx;
            shot->m0_position.vy += shot->m20_velocity.vy;
            shot->m0_position.vz += shot->m20_velocity.vz;
            spawnBattlingTrailEffect(gear->mD0_status.bits.b27_side, &shot->m0_position, shot->m38_handle, 2);
            break;
        case 4:
            color[0] = color[1] = rand() % 191 + 0x40;
            color[2] = 0xFF;
            shot->m0_position.vx += shot->m20_velocity.vx;
            shot->m0_position.vy += shot->m20_velocity.vy;
            shot->m0_position.vz += shot->m20_velocity.vz;
            addBattlingLine(&shot->m10_prevPosition, &shot->m0_position, color);
            break;
        case 5:
            color[0] = color[1] = rand() % 191 + 0x40;
            color[2] = 0xFF;
            shot->m0_position.vx += shot->m20_velocity.vx;
            shot->m0_position.vy += shot->m20_velocity.vy;
            shot->m0_position.vz += shot->m20_velocity.vz;
            addBattlingLine(&shot->m10_prevPosition, &shot->m0_position, color);
            spawnBattlingTrailEffect(gear->mD0_status.bits.b27_side, &shot->m0_position, shot->m38_handle, 2);
            break;
        }
        switch (shot->m33_moveType) {
        case 0:
            break;
        case 1:
            if (shot->m31_speed < 0x70) {
                shot->m31_speed += 0x10;
            }
            break;
        case 2:
            shot->m3E_homing = 0x600 - shot->m40_life * 0x30;
            break;
        case 3:
            shot->m3E_homing = 0;
            break;
        }
    }
    FALLOFF_RETURN(0); /* result unused */
}

void getBattlingGearPartPosition(BattlingGear* gear, s32 partIndex, s32 vertex, VECTOR* out) {
    BattlingNode* part;

    part = NODE_SKELETON(gear->m5C_model)->m4_bones[partIndex];
    if (vertex != 0 && part->m0_type == 1) {
        gte_SetRotMatrix(&part->m4C_localMatrix);
        gte_SetTransMatrix(&part->m4C_localMatrix);
        gte_ldv0(&NODE_MODEL(part)->m14_modelBlock->m8_vertices[vertex - 1]);
        gte_rt();
        gte_stlvnl(out);
        out->vx += gear->m0_position.vx;
        out->vy += gear->m0_position.vy;
        out->vz += gear->m0_position.vz;
    } else {
        out->vx = part->m4C_localMatrix.t[0] + gear->m0_position.vx;
        out->vy = part->m4C_localMatrix.t[1] + gear->m0_position.vy;
        out->vz = part->m4C_localMatrix.t[2] + gear->m0_position.vz;
    }
}

void ageBattlingGearHitShapes(BattlingGear* gear) {
    s32 i;
    BattlingGearShape* trail;

    for (i = 0; i < 16; i++) {
        trail = &gear->m104_shapes[i];
        if (trail->m44_hit.bits.state == 2) {
            trail->m44_hit.bits.state = 1;
        } else {
            trail->m44_hit.bits.state = 0;
        }
    }
}

void setBattlingGearHitShape(VECTOR* pos, VECTOR* dir, s32 flag, BattlingEffectCmd* cmd, BattlingGearShape* trail, s32 arg5, BattlingGear* gear) {
    trail->m0_points[1] = trail->m0_points[0];
    trail->m0_points[3] = trail->m0_points[2];
    trail->m0_points[0] = *pos;
    trail->m0_points[2] = *dir;
    trail->m44_hit.bits.b1 = flag;
    trail->m43_part = arg5;
    trail->m44_hit.bits.state = 2;
    trail->m44_hit.bits.damage = gear->m644_moveDamage;
    trail->m50_move = gear->m84_move;
    trail->m44_hit.bits.type = cmd->m1_type;
    battlingHitShapeCount++;
}

s32 canBattlingGearAddGauge(BattlingGear* gear, s32 amount) {
    s32 total;

    total = gear->mB6_gauge + amount;
    if (total > 0x1000) {
        if ((total - 0xFF1) / 20 >= gear->mB4_hp) {
            return 0;
        }
    }
    return 1;
}

s32 addBattlingGearGauge(BattlingGear* gear, s32 amount, s32 mode) {
    s32 cost;

    if (battlingGameMode == 4) {
        return 1;
    }
    if (battlingGameMode == 6) {
        return 1;
    }
    gear->mB6_gauge += amount;
    if (gear->mB6_gauge > 0x1000) {
        cost = (gear->mB6_gauge - 0x1000) / 20;
        if (cost >= gear->mB4_hp) {
            gear->mB6_gauge -= amount;
            gear->mBA_damage = 0;
            if (getBattlingSceneMode() != 4) {
                playBattlingSfx(0x2E);
            }
            return 0;
        }
        gear->mB6_gauge = 0x1000;
        gear->mBA_damage = cost;
        switch (mode) {
        case 1:
            gear->m1654_etherHpLoss += gear->mBA_damage;
            break;
        case 2:
            gear->m1658_boostHpLoss += gear->mBA_damage;
            break;
        }
    }
    return 1;
}

void runBattlingGearEffectCommand(BattlingGear* gear, BattlingEffectCmd* cmd) {
    VECTOR a;
    VECTOR b;
    s32 handle;

    handle = makeBattlingEffectHandle(gear->mD0_status.bits.b27_side, cmd->m2_partA, cmd->m4_vertexA, 1);
    getBattlingGearPartPosition(gear, cmd->m2_partA, cmd->m4_vertexA, &a);
    if (isBattlingTwoPointEffect(cmd->m1_type) != 0) {
        getBattlingGearPartPosition(gear, cmd->m3_partB, cmd->m6_vertexB, &b);
        if (cmd->m1_type == 0x10) {
            spawnBattlingBeamEffect(gear->mD0_status.bits.b27_side, &a, &b, handle);
        } else {
            spawnBattlingLightningKind(&a, &b, cmd->m1_type);
        }
    } else {
        if (cmd->m2_partA != cmd->m3_partB || cmd->m4_vertexA != cmd->m6_vertexB) {
            getBattlingGearPartPosition(gear, cmd->m3_partB, cmd->m6_vertexB, &b);
            a.vx = (a.vx + b.vx) / 2;
            a.vy = (a.vy + b.vy) / 2;
            a.vz = (a.vz + b.vz) / 2;
        }
        setBattlingEffectColor(&gear->m15D4_color);
        if (cmd->m1_type >= 0x20) {
            spawnBattlingTrailEffect(gear->mD0_status.bits.b27_side, &a, handle, cmd->m1_type - 0x20);
        } else {
            spawnBattlingHitEffect(&a, cmd->m1_type);
        }
    }
}

void runBattlingGearAttackCommand(BattlingGear* gear, BattlingEffectCmd* cmd, s32 firstFrame) {
    VECTOR a;
    VECTOR b;
    s32 handle;
    s32 single;
    BattlingGearShape* trail;
    s32 i;
    s32 found;
    VECTOR unused;
    s32 power;

    /* getBattlingGearPartPosition sets vx-vz only, and the pads go along
     * into the projectiles and hit shapes (nothing reads them). On the PS1
     * they are whatever an earlier call left in this frame (Beetle: shot 0's
     * pad -0x8B, 0, -0x48 in one fight); here host garbage, so 0: runs and
     * save states repeat exactly. */
    a.pad = 0;
    b.pad = 0;
    handle = makeBattlingEffectHandle(gear->mD0_status.bits.b27_side, cmd->m2_partA, cmd->m4_vertexA, 0);
    if (cmd->m2_partA == cmd->m3_partB && cmd->m4_vertexA == cmd->m6_vertexB) {
        getBattlingGearPartPosition(gear, cmd->m2_partA, cmd->m4_vertexA, &a);
        if (gear->m84_move->m2_effects != 0 && !(cmd->m1_type & 0x40)) {
            spawnBattlingTrailEffect(gear->mD0_status.bits.b27_side, &a, handle, 0);
        }
        b = a;
        single = 1;
    } else {
        getBattlingGearPartPosition(gear, cmd->m2_partA, cmd->m4_vertexA, &a);
        getBattlingGearPartPosition(gear, cmd->m3_partB, cmd->m6_vertexB, &b);
        if (gear->m84_move->m2_effects != 0 && !(cmd->m1_type & 0x40)) {
            spawnBattlingBeamEffect(gear->mD0_status.bits.b27_side, &a, &b, handle);
        }
        single = 0;
    }
    if (firstFrame == 0) {
        return;
    }
    if (cmd->m1_type == 0x20) {
        if ((addBattlingGearGauge(gear, gear->mBE_gaugeCost, 1) != 0 && getBattlingSceneMode() != 4) ||
            (getBattlingSceneMode() == 4 && (gear->m15D0_replayNext->m6_angleFlags.flags & 0x8000))) {
            if (!single) {
                a.vx = (a.vx + b.vx) / 2;
                a.vy = (a.vy + b.vy) / 2;
                a.vz = (a.vz + b.vz) / 2;
            }
            fireBattlingProjectile(&a, NULL, gear, 0, gear->m1600_def->m18_etherPower, handle);
            clearBattlingGearInput(gear);
            gear->m15CC_replayCur->m6_angleFlags.flags |= 0x8000;
        } else {
            spawnBattlingHitEffect(&a, 9);
            clearBattlingGearInput(gear);
            gear->m15CC_replayCur->m6_angleFlags.flags &= 0x7FFF;
        }
        battlingSideStates[gear->mD0_status.bits.b27_side].mC_startFrame = battlingSideStates[gear->mD0_status.bits.b27_side].m10_endFrame = gear->m99E_frameAccum;
        battlingSideStates[gear->mD0_status.bits.b27_side].m0_text = battlingSideStates[gear->mD0_status.bits.b27_side].m8_prevText = battlingEtherMoveName;
        battlingSideStates[gear->mD0_status.bits.b27_side].m4_strength = gear->m1600_def->m18_etherPower;
        return;
    }
    if (battlingSideStates[gear->mD0_status.bits.b27_side].m8_prevText != battlingSideStates[gear->mD0_status.bits.b27_side].m0_text) {
        battlingSideStates[gear->mD0_status.bits.b27_side].mC_startFrame = gear->m99E_frameAccum;
        battlingSideStates[gear->mD0_status.bits.b27_side].m8_prevText = battlingSideStates[gear->mD0_status.bits.b27_side].m0_text;
    }
    battlingSideStates[gear->mD0_status.bits.b27_side].m10_endFrame = gear->m99E_frameAccum;
    power = gear->m644_moveDamage;
    switch (cmd->m1_type) {
    case 4:
        fireBattlingProjectile(&a, NULL, gear, 1, power, handle);
        return;
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26:
        if (single) {
            fireBattlingProjectile(&a, NULL, gear, cmd->m1_type - 0x20, power, handle);
        } else {
            fireBattlingProjectile(&a, &b, gear, cmd->m1_type - 0x20, power, handle);
        }
        return;
    }
    found = 0;
    for (i = 0; i < 16; i++) {
        trail = &gear->m104_shapes[i];
        if (trail->m44_hit.bits.state == 1 && trail->m4C_handle == handle && trail->m48_frame != battlingFrameCounter) {
            setBattlingGearHitShape(&a, &b, single, cmd, trail, cmd->m2_partA, gear);
            trail->m44_hit.bits.b0 = 0;
            trail->m48_frame = battlingFrameCounter;
            found = 1;
            return;
        }
    }
    if (found) {
        return;
    }
    for (i = 0; i < 16 && !found; i++) {
        trail = &gear->m104_shapes[i];
        if (trail->m44_hit.bits.state == 0) {
            setBattlingGearHitShape(&a, &b, single, cmd, trail, cmd->m2_partA, gear);
            trail->m4C_handle = handle;
            trail->m44_hit.bits.b0 = 1;
            trail->m48_frame = battlingFrameCounter;
            found = 1;
            return;
        }
    }
}

INCLUDE_RODATA("battling/nonmatchings/bl_800732CC", battlingEtherString);

void runBattlingGearAnimEvents(BattlingGear* gear, s16 frame, s16 count) {
    VECTOR unused;
    BattlingEffectCmd* started[20];
    u8 soundPlayed;
    u8* events;
    BattlingAnimEvent* event;
    BattlingEffectCmd* cmd;
    s32 numStarted;
    s32 i;

    /* Never set here: retail's s4 still holds battlingMainTask's `hold`
     * (updateBattlingFight leaves s4 alone), 0 unless L2 is held in the
     * main menu. */
    numStarted = 0;
    if (count == 0) {
        count = 1;
    }
    if (gear->m50_animFrame == frame) {
        return;
    }
    i = gear->m4C_animation;
    gear->m50_animFrame = frame;
    i = gear->m900_eventOffsets[i];
    if (i != 0) {
        soundPlayed = 0;
        battlingHitShapeCount = 0;
        events = (u8*)gear->m8FC_params + i;
        while (--count != -1) {
            event = (BattlingAnimEvent*)events;
            while (event->m0_start != 0xFF) {
                if (frame < event->m0_start || event->m1_end < frame) {
                    goto next;
                }
                cmd = (BattlingEffectCmd*)((u8*)gear->m8FC_params + event->m2_cmdOffset);
                switch (cmd->m0_kind) {
                case 0:
                    if (frame == event->m0_start) {
                        gear->mD0_status.bits.b26_effectStart = 1;
                    }
                    runBattlingGearAttackCommand(gear, cmd, gear->mD0_status.bits.b26_effectStart);
                    if (frame == event->m1_end) {
                        gear->mD0_status.bits.b26_effectStart = 0;
                    }
                    break;
                case 1:
                    if (!soundPlayed) {
                        playBattlingGearSound(gear, cmd->m2_partA, gear, 2);
                        soundPlayed = 1;
                        playBattlingGearSound(gear, cmd->m3_partB, gear, 2);
                    }
                    break;
                case 2:
                    for (i = 0; i < numStarted; i++) {
                        if (started[i] == cmd) {
                            goto next;
                        }
                    }
                    if (numStarted < 20) {
                        started[numStarted++] = cmd;
                        runBattlingGearEffectCommand(gear, cmd);
                    }
                    break;
                case 3:
                    snapBattlingGearToFeet(gear);
                    break;
                case 4:
                    NODE_MODEL(NODE_SKELETON(gear->m5C_model)->m4_bones[cmd->m1_type])->m0_flags |= 1;
                    break;
                case 5:
                    NODE_MODEL(NODE_SKELETON(gear->m5C_model)->m4_bones[cmd->m1_type])->m0_flags &= ~1;
                    break;
                default:
                    continue;
                }
            next:
                event++;
            }
            frame++;
        }
    }
    ageBattlingGearHitShapes(gear);
}

void resetBattlingGearPartVisibility(BattlingGear* g) {
    BattlingNode** parts;
    s32 i;
    s32 pad[1];

    parts = NODE_SKELETON(g->m5C_model)->m4_bones;
    for (i = 0; i < NODE_SKELETON(g->m5C_model)->m0_boneCount; i++) {
        if (parts[i]->m0_type == 1) {
            NODE_MODEL(parts[i])->m0_flags &= ~1;
        }
    }
    for (i = 0; i < g->m908_hiddenPartCount; i++) {
        NODE_MODEL(parts[g->m904_hiddenParts[i]])->m0_flags |= 1;
    }
    if (g->m909_gearId == 13) {
        NODE_MODEL(parts[13])->m0_flags |= 1;
    }
}

void playBattlingGearReplayFrame(BattlingGear* g) {
    BattlingReplayFrame* pos;
    BattlingReplayFrame* def;
    BattlingAnim* bone;

    pos = g->m15CC_replayCur;
    def = g->m15D0_replayNext;
    g->m0_position.vx = pos->m0_x;
    g->m0_position.vy = pos->m2_y;
    g->m0_position.vz = pos->m4_z;
    g->m4C_animation = def->m8_anim;
    g->m54_rotY = pos->m6_angleFlags.bits.angle;
    bone = &NODE_SKELETON(g->m5C_model)->m8_anims[def->m8_anim];
    if (def->m6_angleFlags.bits.b12) {
        resetBattlingGearPartVisibility(g);
        resetBattlingAnim(bone);
    }
    runBattlingGearAnimEvents(g, bone->m12_frame, g->m15D0_replayNext->m9_frameStep);
    if (def->mA != 0) {
        advanceBattlingAnim(bone, def->m9_frameStep, def->mA);
    }
}

void updateBattlingGearAnimation(BattlingGear* g) {
    BattlingReplayFrame* st;
    BattlingAnimInfo* info;
    BattlingAnim* bone;
    s32 i;
    s32 n;
    s32 frames;
    s32 speed;
    s32 old;
    u8 anim;

    st = g->m15CC_replayCur;
    st->m6_angleFlags.bits.angle = g->m54_rotY;
    st->m0_x = g->m0_position.vx;
    st->m2_y = g->m0_position.vy;
    st->m4_z = g->m0_position.vz;
    speed = 0;
    st->m8_anim = g->m4C_animation;
    info = &battlingGearAnimInfo[g->m4C_animation];
    bone = &NODE_SKELETON(g->m5C_model)->m8_anims[g->m4C_animation];
    st->m6_angleFlags.bits.b12 = 0;
    anim = g->m4C_animation;
    frames = 0;
    if (anim != g->m4E_lastAnimation || (g->mD0_status.flags & 0x10000000)) {
        info = &battlingGearAnimInfo[anim];
        g->m4E_lastAnimation = anim;
        g->mD0_status.flags = (g->mD0_status.flags | 0x1000) & ~0x800;
        if (!(g->mD0_status.flags & 0x10000000)) {
            g->m99C_blendSteps = g->m7C_animParams[g->m4C_animation * 2];
        }
        if (g->mD0_status.flags & 0x2000000) {
            g->m99C_blendSteps = 1;
            g->mD0_status.flags &= ~0x2000000;
        }
        g->m4F_animSpeed = g->m7C_animParams[g->m4C_animation * 2 + 1] * g->m15F2_speedScale / 256;
        bone = &NODE_SKELETON(g->m5C_model)->m8_anims[g->m4C_animation];
        resetBattlingGearPartVisibility(g);
        resetBattlingAnim(bone);
        g->m99E_frameAccum = 0;
        g->m50_animFrame = -1;
        g->mD0_status.flags &= ~0x10000000;
        st->m6_angleFlags.bits.b12 = 1;
        if (info->m0_type == 2) {
            g->m90E_holdAnimation = g->m4C_animation;
        } else {
            g->m90E_holdAnimation = 0;
        }
    }
    old = g->m99E_frameAccum;
    g->m99E_frameAccum += g->m4F_animSpeed + g->m52_moveAnimSpeed;
    n = (g->m99E_frameAccum >> 4) - (old >> 4);
    g->m998_stepStartFrame = bone->m12_frame;
    g->m99A_stepFrames = n;
    for (i = 0; i < n; i++) {
        if (g->m99C_blendSteps != 1) {
            g->m99C_blendSteps--;
            if (g->m99C_blendSteps <= 0) {
                g->m99C_blendSteps = 1;
            }
        }
    }
    switch (info->m0_type) {
    case 0:
        if (!g->mD0_status.bits.b11_animEnd) {
            g->mD0_status.bits.b11_animEnd = advanceBattlingAnim(bone, n, g->m99C_blendSteps);
            frames = n;
            speed = g->m99C_blendSteps;
            if (g->mD0_status.bits.b11_animEnd) {
                if (info->m1_next == -1) {
                    g->mD0_status.flags &= ~0x1000;
                } else {
                    g->m4C_animation = info->m1_next;
                }
            }
        }
        break;
    case 1:
        g->mD0_status.flags &= ~0x1000;
        g->mD0_status.bits.b11_animEnd = advanceBattlingAnim(bone, n, g->m99C_blendSteps);
        frames = n;
        speed = g->m99C_blendSteps;
        if (g->mD0_status.bits.b11_animEnd) {
            g->mD0_status.flags |= 0x10000000;
            if (info->m1_next != -1) {
                g->m4C_animation = info->m1_next;
            }
        }
        break;
    case 2:
        g->mD0_status.flags &= ~0x1000;
        if (!g->mD0_status.bits.b11_animEnd) {
            g->mD0_status.bits.b11_animEnd = advanceBattlingAnim(bone, n, g->m99C_blendSteps);
            frames = n;
            speed = g->m99C_blendSteps;
        }
        if (g->mD0_status.flags & 0x400) {
            if (info->m1_next == -1) {
                g->mD0_status.flags &= ~0x1000;
            } else {
                g->m4C_animation = info->m1_next;
            }
            g->mD0_status.flags &= ~0x400;
        }
        break;
    case 3:
        if (!g->mD0_status.bits.b11_animEnd) {
            g->mD0_status.bits.b11_animEnd = advanceBattlingAnim(bone, n, g->m99C_blendSteps);
            frames = n;
            speed = g->m99C_blendSteps;
        }
        break;
    }
    st->m9_frameStep = frames;
    st->mA = speed;
}
