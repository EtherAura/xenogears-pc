/* Battling overlay, 0x80075060-0x8007B270: gear physics and input, replay and
 * round flow (updateBattlingRound-updateBattlingRoundResult share strings). animateBattlingVictoryGear calls
 * updateBattlingGearAnimation without an argument, so this file has no prototype for it;
 * its .rodata ends with "COM2 VICTORY" and 3 bytes of linker fill. drawBattlingGearMoveInfo
 * has its own "%s" although updateBattlingVictoryScreen uses one too. */
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
extern u8 battlingResult; /* battlingEntryParams[6] */
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

/* This file calls these without prototypes: retail passes unmasked ints
 * for their u8/u16 parameters and uses their results unmasked. */
void updateBattlingGearAnimation();

void computeBattlingResult(s32 arg0) {
    BattlingGear* g = &battlingGear1;
    s32 a;

    if (arg0) {
        if (isBattlingGearHpAbove(g, 0xE0)) {
            battlingResult = 2;
        } else if (isBattlingGearHpAbove(g, 0x10)) {
            battlingResult = 1;
        } else {
            battlingResult = 3;
        }
    } else if (battlingGear1.m1660_attackPresses == 0 && battlingGear1.m165C_etherPresses == 0) {
        battlingResult = 0x88;
    } else if (battlingGear1.m165C_etherPresses == 0) {
        battlingResult = 0x82;
    } else {
        a = g->mBC_maxHp * 176 / 255;
        if (a < g->m1654_etherHpLoss) {
            battlingResult = 0x83;
        } else if (g->mBC_maxHp * 160 / 255 < g->m1658_boostHpLoss) {
            battlingResult = 0x84;
        } else if (a < g->m1658_boostHpLoss + g->m1654_etherHpLoss) {
            battlingResult = 0x85;
        } else {
            g = g->mD8_opponent;
            if (isBattlingGearHpAbove(g, 0xE0)) {
                battlingResult = 0x86;
            } else if (isBattlingGearHpAbove(g, 0x10)) {
                battlingResult = 0x81;
            } else {
                battlingResult = 0x87;
            }
        }
    }
}

#if VERSION_JP
#define BL_RUBBER_MODE "RUBBER CONNECTION MODE"
#define BL_RUBBER_BATTLE "RUBBER CONNECTION BATTLE"
#else
#define BL_RUBBER_MODE "RUBBER BAND MODE"
#define BL_RUBBER_BATTLE "RUBBER BAND BATTLE"
#endif
void updateBattlingRound(BattlingGear* a, BattlingGear* b) {
    char buf[16];

    battlingRoundFrames++;
    setBattlingTextPos(0xA0, 0x64);
    if ((!isBattlingGearHpAbove(a, 0x60) || !isBattlingGearHpAbove(b, 0x60)) && battlingEventFight != 0
        && !isBattlingGearInWater(a) && !isBattlingGearInWater(b)) {
        battlingGearAnimInfo[10].m0_type = 1;
        battlingGearAnimInfo[10].m1_next = 10;
        startBattlingEvent();
    }
    if (battlingRoundOver != 0) {
        if (battlingKoTimer++ > 0x3C) {
            setBattlingSceneMode(battlingRoundWinner == 2 ? 2 : 4);
#if !VERSION_JP
            printfStub("gm");
            allocateHeap(1, 0);
            printfStub(" fin\n");
#endif
        }
        if (battlingRoundWinner == 2) {
            printBattlingTextCentered("DRAW GAME");
        } else {
            printBattlingTextCentered("KNOCK OUT!!");
        }
    } else if (a->mD0_status.flags & 0x800000) {
        if (b->mD0_status.flags & 0x800000) {
            battlingRoundOver = 1;
            battlingFightActive = 0;
            battlingRoundWinner = 2;
            battlingDrawGameCount++;
        } else {
            battlingGear2.mF2_wins++;
            battlingFightActive = 0;
            battlingRoundOver = 1;
            battlingRoundWinner = 1;
            if (b->mD0_status.flags & 0x40) {
                battlingActivePad = 0;
            } else {
                battlingActivePad = 1;
            }
            computeBattlingResult(0);
        }
    } else if (b->mD0_status.flags & 0x800000) {
        battlingRoundOver = 1;
        battlingFightActive = 0;
        battlingRoundWinner = 0;
        battlingActivePad = 0;
        battlingGear1.mF2_wins++;
        computeBattlingResult(1);
        if ((b->mD0_status.flags & 0x40) && battlingGameMode != 3) {
#if !VERSION_JP
            printfStub("00");
            allocateHeap(1, 2);
            printfStub(" fin\n");
#endif
            markBattlingGearDefeated(b->m909_gearId);
#if !VERSION_JP
            printfStub("11");
            allocateHeap(1, 2);
            printfStub(" ctrl\n");
#endif
        }
    }
    if (battlingRoundIntroTimer != 0) {
        if (battlingRoundIntroTimer < 30) {
            if (getBattlingSceneMode() != 7 && battlingGameMode != 4) {
                printBattlingTextCentered("FIGHT!!");
            }
            battlingFightActive = 1;
        } else if (battlingRoundIntroTimer < 60) {
            if (getBattlingSceneMode() != 7) {
                if (battlingGameMode == 4) {
                    printBattlingTextCentered("START");
                    if (battlingRubberBandMode) {
                        printBattlingTextCentered("");
                        printBattlingTextCentered(BL_RUBBER_MODE);
                    }
                } else {
                    printBattlingTextCentered("READY");
                    if (battlingRubberBandMode) {
                        printBattlingTextCentered("");
                        printBattlingTextCentered(BL_RUBBER_BATTLE);
                    }
                }
            }
        } else {
            if (getBattlingSceneMode() != 7) {
                if (battlingGameMode == 4) {
                    printBattlingTextCentered("PRACTICE");
                    if (battlingRubberBandMode) {
                        printBattlingTextCentered("");
                        printBattlingTextCentered(BL_RUBBER_MODE);
                    }
                } else {
                    sprintf(buf, "ROUND %d", battlingRoundNumber);
                    printBattlingTextCentered(buf);
                    if (battlingRubberBandMode) {
                        printBattlingTextCentered("");
                        printBattlingTextCentered(BL_RUBBER_BATTLE);
                    }
                }
            }
        }
        battlingRoundIntroTimer--;
    }
    battlingGearDistance3D = getBattlingDistance(a, b);
    battlingGearDistance = getBattlingDistanceXZ(&a->m0_position, &b->m0_position);
    battlingGearAngle = ratan2(a->m0_position.vx - b->m0_position.vx, a->m0_position.vz - b->m0_position.vz);
}

s32 func_8007570C(BattlingGear* a, BattlingGear* b, s32 scale) {
    return (b->m15EC * scale) / 32 - a->m15EE;
}

s32 func_80075738(BattlingGear* a, BattlingGear* b) {
    return b->m15EA - a->m15E8;
}

void func_80075748(void) {
}

s32 isBattlingCircleOnSegment(s32 x0, s32 z0, s32 x1, s32 z1, s32 px, s32 pz, s32 r) {
    VECTOR v;
    VECTOR sq;
    s32 dx, dz, len, nx, nz, along, perp, inside;

    v.vx = x1 - x0;
    v.vy = z1 - z0;
    dx = px - x0;
    dz = pz - z0;
    v.vz = r;
    Square0(&v, &sq);
    len = SquareRoot0(sq.vx + sq.vy);
    if (len == 0) {
        return 0;
    }
    nx = (v.vx << 12) / len;
    nz = (v.vy << 12) / len;
    along = (dx * nx + dz * nz) / 4096;
    perp = (-(dx * nz) + dz * nx) / 4096;
    inside = (u32)(perp * perp - sq.vz) >> 31;
    if (along < 0) {
        return 0;
    }
    return (along <= len + r) ? inside : 0;
}

s32 intersectBattlingCircleSegment(s32 x0, s32 z0, s32 x1, s32 z1, s32 px, s32 pz, s32 r) {
    VECTOR v;
    VECTOR sq;
    s32 dx, dz, len, nx, nz, along, perp, inside, d;

    v.vx = x1 - x0;
    v.vy = z1 - z0;
    dx = px - x0;
    dz = pz - z0;
    v.vz = r;
    Square0(&v, &sq);
    len = SquareRoot0(sq.vx + sq.vy);
    if (len == 0) {
        return 0;
    }
    nx = (v.vx << 12) / len;
    nz = (v.vy << 12) / len;
    along = (dx * nx + dz * nz) / 4096;
    perp = (-(dx * nz) + dz * nx) / 4096;
    d = perp * perp - sq.vz;
    inside = (u32)d >> 31;
    if (along < 0) {
        inside = 0;
    }
    if (len + r < along) {
        inside = 0;
    }
    if (inside) {
        if (d < 0) {
            d = -d;
        }
        d = along - SquareRoot0(d);
        battlingHitPointX = d * nx / 4096 + x0;
        battlingHitPointZ = d * nz / 4096 + z0;
    }
    return inside;
}

s32 isBattlingCircleOnQuad(VECTOR* q, s32 x, s32 z, s32 r) {
    s32 ret;
    battlingHitPointX = x;
    battlingHitPointZ = z;
    if (isBattlingCircleOnSegment(q[1].vx, q[1].vz, q[0].vx, q[0].vz, x, z, r)) {
        ret = 1;
    } else if (isBattlingCircleOnSegment(q[3].vx, q[3].vz, q[2].vx, q[2].vz, x, z, r)) {
        ret = 1;
    } else if (isBattlingCircleOnSegment(q[0].vx, q[0].vz, q[2].vx, q[2].vz, x, z, r)) {
        ret = 1;
    } else {
        ret = isBattlingCircleOnSegment(q[1].vx, q[1].vz, q[3].vx, q[3].vz, x, z, r) != 0;
    }
    return ret;
}

s32 checkBattlingGearHits(BattlingGear* g) {
    VECTOR top;
    VECTOR bot;
    VECTOR hit;
    VECTOR best;
    BattlingProjectile* h;
    BattlingGearShape* s;
    s32 i;
    s32 found;
    s32 t;
    s32 v;
    s32 hits;
    s32 bestDmg;
    s32 type;
    s32 d1, d2;

    hits = 0;
    top = g->m93C_footPoint;
    bot = g->m92C_topPoint;
    bestDmg = 0;
    type = 0;
    for (i = 0; i < 8; i++) {
        h = &g->mD8_opponent->m64C_projectiles[i];
        if (h->m30_active == 0) {
            continue;
        }
        found = 0;
        if (bot.vy < h->m0_position.vy && h->m0_position.vy < top.vy) {
            t = ((h->m0_position.vy - top.vy) << 12) / (bot.vy - top.vy);
            hit.vx = ((t * (bot.vx - top.vx)) >> 12) + top.vx;
            hit.vy = ((t * (bot.vy - top.vy)) >> 12) + top.vy;
            hit.vz = ((t * (bot.vz - top.vz)) >> 12) + top.vz;
            found = intersectBattlingCircleSegment(h->m10_prevPosition.vx, h->m10_prevPosition.vz, h->m0_position.vx, h->m0_position.vz, hit.vx, hit.vz, g->m8FC_params->m13_radius);
        } else if (h->m34_distance < g->m8FC_params->m13_radius) {
            t = abs(bot.vy - h->m10_prevPosition.vy);
            d1 = abs(top.vy - h->m10_prevPosition.vy);
            d2 = abs(h->m0_position.vy - h->m10_prevPosition.vy);
            if (t < d1) {
                if (t < d2) {
                    hit = bot;
                    found = 1;
                }
            } else if (d1 < d2) {
                found = 1;
                hit = top;
            }
        }
        if (found) {
            g->m918_glow = 0xFF;
            g->mD4_cues.flags |= 0x20;
            t = (s16)h->m3C_damage;
            if ((u32)(((ratan2(h->m20_velocity.vx, h->m20_velocity.vz) - g->m54_rotY) & 0xFFF) - 0x601) >= 0x3FF) {
                if (g->mD0_status.flags & 4) {
                    playBattlingGearHitSound(g, 15, &hit, 1);
                    t /= 2;
                    g->m918_glow = 0x80;
                    goto done;
                }
            } else {
                t *= 2;
                type = 3;
            }
            playBattlingGearHitSound(g, 12, &hit, 1);
        done:
            v = t * 2 / 3;
            g->mE8_stun += v;
            g->m916_stagger += v;
            g->m91C_lastHitPoint = hit;
            if (t != 0) {
                hits++;
            }
            battlingHitSparkPos = hit;
            battlingHitSparkCount = 16;
            g->mC8_comboTimer = t / 3 + 12;
            bestDmg = t;
            best = hit;
            h->m30_active = 0;
            break;
        }
    }
    for (i = 0; i < 16; i++) {
        s = &g->mD8_opponent->m104_shapes[i];
        if (s->m44_hit.b.m45_state != 1) {
            continue;
        }
        if (s->m44_hit.flags & 1) {
            continue;
        }
        found = 0;
        if (s->m44_hit.flags & 2) {
            if (bot.vy < s->m0_points[0].vy && s->m0_points[0].vy < top.vy) {
                t = ((s->m0_points[0].vy - top.vy) << 12) / (bot.vy - top.vy);
                hit.vx = ((t * (bot.vx - top.vx)) >> 12) + top.vx;
                hit.vy = ((t * (bot.vy - top.vy)) >> 12) + top.vy;
                hit.vz = ((t * (bot.vz - top.vz)) >> 12) + top.vz;
                found = intersectBattlingCircleSegment(s->m0_points[1].vx, s->m0_points[1].vz, s->m0_points[0].vx, s->m0_points[0].vz, hit.vx, hit.vz, g->m8FC_params->m13_radius);
            }
        } else {
            t = (s->m0_points[0].vy + s->m0_points[1].vy + s->m0_points[2].vy + s->m0_points[3].vy) / 4;
            if (bot.vy < t && t < top.vy) {
                t = ((t - top.vy) << 12) / (bot.vy - top.vy);
                hit.vx = ((t * (bot.vx - top.vx)) >> 12) + top.vx;
                hit.vy = ((t * (bot.vy - top.vy)) >> 12) + top.vy;
                hit.vz = ((t * (bot.vz - top.vz)) >> 12) + top.vz;
                found = isBattlingCircleOnQuad(s->m0_points, hit.vx, hit.vz, g->m8FC_params->m13_radius);
            }
        }
        if (found) {
            g->mD8_opponent->mD0_status.flags &= ~0x4000000;
            hit.vx = battlingHitPointX;
            hit.vz = battlingHitPointZ;
            if (g->mD0_status.flags & 4) {
                playBattlingGearHitSound(g, 15, &hit, 1);
                t = s->m44_hit.b.m47_damage >> 1;
                battlingHitSparkPos = hit;
                battlingHitSparkCount = 4;
                g->m918_glow = 0xC0;
                g->mD4_cues.flags |= 0x20;
            } else {
                playBattlingGearHitSound(g, s->m40_effect, &hit, 1);
                spawnBattlingHitEffect(&hit, 0);
                battlingHitSparkPos = hit;
                battlingHitSparkCount = 16;
                g->m918_glow = 0xFF;
                g->mD4_cues.flags &= ~0x20;
                t = s->m44_hit.b.m47_damage;
            }
            g->mC8_comboTimer = t;
            g->mE8_stun += t * 6 / 5;
            g->m916_stagger += t * 6 / 5;
            g->m91C_lastHitPoint = hit;
            if (t != 0) {
                hits++;
            }
            if (bestDmg < t) {
                bestDmg = t;
                best = s->m0_points[0];
                type = s->m44_hit.b.m46_type;
            }
            ageBattlingGearHitShapes(g->mD8_opponent);
            break;
        }
    }
    if (hits) {
        g->m100_hitType = type + 1;
        g->m970_hitDamage = bestDmg;
        g->m974_hitPoint = best;
        g->m1668_hitsTaken++;
    } else {
        g->m100_hitType = 0;
    }
    return 0;
}

void pushBattlingGearInput(BattlingGear* g, u8 v) {
    if (g->m9C2_inputQueueCount < 32) {
        g->m9A0_inputQueue[g->m9C0_inputQueueWrite++ & 0x1F] = v;
        g->m9C2_inputQueueCount++;
    }
}

s32 popBattlingGearInput(BattlingGear* g) {
    u8 v;
    if (g->m9C2_inputQueueCount == 0) {
        return 0;
    }
    v = g->m9A0_inputQueue[g->m9C1_inputQueueRead++ & 0x1F];
    g->m9C2_inputQueueCount--;
    return v;
}

void clearBattlingGearInput(BattlingGear* g) {
    g->m9C2_inputQueueCount = 0;
    g->m9C0_inputQueueWrite = 0;
    g->m9C1_inputQueueRead = 0;
    g->m9C3_move = 0;
}

void printBattlingGearInputQueue(BattlingGear* g) {
    s32 i;
    s32 idx;

    idx = g->m9C1_inputQueueRead;
    sprintf_screen("%s:");
    for (i = 0; i < g->m9C2_inputQueueCount; i++) {
        sprintf_screen("%d", g->m9A0_inputQueue[idx++ & 0x1F]);
    }
    sprintf_screen("\n");
}

/* 0x80091198 (.data): names of the input sequences ("" then A/B strings). */
char* battlingMoveNames[] = {
    "", "A", "B", "AA", "AB", "BA", "BB", "AAA", "AAB", "ABA", "ABB", "BAA", "BAB", "BBA", "BBB",
};

void beginBattlingGearFrame(BattlingGear* g) {
    g->m40_moveSpeed = 0;
    g->m100_hitType = 0;
    if (getBattlingSceneMode() != 7) {
        g->mD0_status.flags &= ~0x8000;
        g->mD0_status.flags &= ~2;
    }
    g->m15F2_speedScale = (g->m15F4_baseSpeedScale * battlingSpeedScale) >> 8;
    g->mD0_status.flags &= ~0x80000;
    setBattlingTextPos(!g->mD0_status.bits.b27_side ? 0x28 : 0xF0, 0x28);
    if (g->m9C3_move != 0) {
        battlingSideStates[g->mD0_status.bits.b27_side].m0_text = battlingMoveNames[g->m9C3_move];
        battlingSideStates[g->mD0_status.bits.b27_side].m4_strength = g->m1600_def->mA_moves[g->m9C3_move - 1] * g->m1600_def->m0_power / 100;
    }
}

void drawBattlingGearMoveInfo(BattlingGear* g) {
    char buf[16];
    s32 x;
    s32 y;

    y = 10;
    x = g->mD0_status.bits.b27_side ? 0xB4 : 0x14;
    if (battlingGameMode != 4) {
        y = 0xB2;
    }
    setBattlingTextPos(x, y);
    sprintf(buf, "%s", battlingSideStates[g->mD0_status.bits.b27_side].m0_text);
    setBattlingTextScale(battlingSideStates[g->mD0_status.bits.b27_side].m0_text == battlingEtherMoveName ? 0xE7 : 0x100);
    printBattlingText(buf);
    setBattlingTextScale(0x100);
    setBattlingTextPos(x + 0x34, y);
    sprintf(buf, "STR:%d", battlingSideStates[g->mD0_status.bits.b27_side].m4_strength);
    printBattlingText(buf);
    y += 0x14;
    setBattlingTextPos(x, y);
    sprintf(buf, "FRAME:%d-%d", battlingSideStates[g->mD0_status.bits.b27_side].mC_startFrame >> 4,
            battlingSideStates[g->mD0_status.bits.b27_side].m10_endFrame >> 4);
    printBattlingText(buf);
}

s32 tryBattlingGearBackDash(BattlingGear* g) {
    if (battlingGearDistance <= 0x200 && g->mC5_action != 4) {
        if (!(g->mD0_status.flags & 0x1000000)) {
            if ((g->mD0_status.flags & 0x40000) && (g->mD8_opponent->mD0_status.flags & 0x40000)) {
                if ((g->mD0_status.flags & 0x60000000) != 0x20000000 || (g->m90A_flags & 2)) {
                    clearBattlingGearInput(g);
                    pushBattlingGearInput(g, 5);
                }
            }
        }
    }
    FALLOFF_RETURN(0); /* result unused */
}

s32 readBattlingGearPadInput(BattlingGear* g) {
    u8 sx;
    u8 sy;
    u16 held;
    u16 pressed;
    s32 x, y;
    s32 accel, decel;
    s32 canAccel;
    s32 angle;
    s32 mag;
    s32 angle2;
    s32 analog;

    if (g->mD0_status.bits.b27_side) {
        analog = (u32)(isControllerConnected(1) - 3) < 2;
        held = newPadButtonForScripts2;
        pressed = newPadButtonForDialogs2;
        if (analog) {
            sx = pad1LeftStickX;
            sy = pad1LeftStickY;
        } else {
            sx = getDpadStickX(held);
            sy = getDpadStickY(newPadButtonForScripts2);
        }
    } else {
        analog = (u32)(isControllerConnected(0) - 3) < 2;
        held = newPadButtonForScripts;
        pressed = newPadButtonForDialogs;
        if (analog) {
            sx = pad0LeftStickX;
            sy = pad0LeftStickY;
        } else {
            sx = getDpadStickX(held);
            sy = getDpadStickY(newPadButtonForScripts);
        }
    }
    if (g->mD0_status.flags & 0x40) {
        IMPLICIT_RETURN(0);
    }
    if (battlingCameraSide != g->mD0_status.bits.b17_facing) {
        if (battlingCameraSide) {
            g->mCE_inputRotation = -g->mCC_sideInputRotation;
        } else {
            g->mCE_inputRotation = g->mCC_sideInputRotation;
        }
        g->m648_baseRotation += 0x800;
        g->mFC_turnRotation += 0x800;
        g->mD0_status.bits.b17_facing = battlingCameraSide;
    }
    if (pressed & 0x10) {
        pushBattlingGearInput(g, 1);
        g->m1660_attackPresses++;
    }
    if (pressed & 0x20) {
        pushBattlingGearInput(g, 2);
        g->m1660_attackPresses++;
    }
    if (pressed & 0x8) {
        pushBattlingGearInput(g, 3);
        g->m165C_etherPresses++;
    }
    if (pressed & 0x80) {
        clearBattlingGearInput(g);
        pushBattlingGearInput(g, 4);
    }
    if (held & 0x40) {
        g->mD0_status.flags |= 0x8000;
    }
    if (pressed & 0x40) {
        clearBattlingGearInput(g);
    }
    decel = g->m9C_decel;
    if (held & 4) {
        g->mD0_status.flags |= 2;
        clearBattlingGearInput(g);
        decel *= 4;
        g->mD0_status.flags |= 0x100;
    } else {
        g->mD0_status.bits.b3_combo = 0;
    }
    if ((g->mD0_status.flags & 0x60000000) == 0x60000000 && !(g->m90A_flags & 4)) {
        accel = g->m98_accel / 2;
    } else {
        accel = g->m98_accel;
    }
    if ((g->mD0_status.flags & 0x60000000) == 0x20000000 && (g->m90A_flags & 4)) {
        accel = g->m98_accel / 2;
    }
    if (g->mD0_status.flags & 0x100) {
        canAccel = 0;
        decel = decel * 2 / 3;
    } else {
        canAccel = 1;
    }
    x = (sy - 0x80) * 2;
    y = (sx - 0x80) * 2;
    angle = ratan2(x, y) + g->mCE_inputRotation;
#if VERSION_JP
    if (analog) {
        if (SquareRoot0(x * x + y * y) < 0x30) {
            angle = 0;
        }
        if (x < 0 && canAccel) {
            g->m8C_inputUp += accel;
            if (abs(x) < g->m8C_inputUp) {
                g->m8C_inputUp = abs(x);
            }
        } else {
            g->m8C_inputUp -= decel;
            if (g->m8C_inputUp < 0) {
                g->m8C_inputUp = 0;
            }
        }
        if (x > 0 && canAccel) {
            g->m94_inputDown += accel;
            if (x < g->m94_inputDown) {
                g->m94_inputDown = x;
            }
        } else {
            g->m94_inputDown -= decel;
            if (g->m94_inputDown < 0) {
                g->m94_inputDown = 0;
            }
        }
        if (y < 0 && canAccel) {
            g->m88_inputLeft += accel;
            if (abs(y) < g->m88_inputLeft) {
                g->m88_inputLeft = abs(y);
            }
        } else {
            g->m88_inputLeft -= decel;
            if (g->m88_inputLeft < 0) {
                g->m88_inputLeft = 0;
            }
        }
        if (y > 0 && canAccel) {
            g->m90_inputRight += accel;
            if (y < g->m90_inputRight) {
                g->m90_inputRight = y;
            }
        } else {
            g->m90_inputRight -= decel;
            if (g->m90_inputRight < 0) {
                g->m90_inputRight = 0;
            }
        }
        if (held & 0xF000) {
            showBattlingAnalogWarning(10);
        }
    } else {
        if ((held & 0x1000) && canAccel) {
            g->m8C_inputUp += accel;
            if (g->m8C_inputUp > 0x100) {
                g->m8C_inputUp = 0x100;
            }
        } else {
            g->m8C_inputUp -= decel;
            if (g->m8C_inputUp < 0) {
                g->m8C_inputUp = 0;
            }
        }
        if ((held & 0x4000) && canAccel) {
            g->m94_inputDown += accel;
            if (g->m94_inputDown > 0x100) {
                g->m94_inputDown = 0x100;
            }
        } else {
            g->m94_inputDown -= decel;
            if (g->m94_inputDown < 0) {
                g->m94_inputDown = 0;
            }
        }
        if ((held & 0x8000) && canAccel) {
            g->m88_inputLeft += accel;
            if (g->m88_inputLeft > 0x100) {
                g->m88_inputLeft = 0x100;
            }
        } else {
            g->m88_inputLeft -= decel;
            if (g->m88_inputLeft < 0) {
                g->m88_inputLeft = 0;
            }
        }
        if ((held & 0x2000) && canAccel) {
            g->m90_inputRight += accel;
            if (g->m90_inputRight > 0x100) {
                g->m90_inputRight = 0x100;
            }
        } else {
            g->m90_inputRight -= decel;
            if (g->m90_inputRight < 0) {
                g->m90_inputRight = 0;
            }
        }
        if (!(held & 0xF000)) {
            angle = 0;
        }
    }
#else
    if (held & 0xF000) {
        if ((held & 0x1000) && canAccel) {
            g->m8C_inputUp += accel;
            if (g->m8C_inputUp > 0x100) {
                g->m8C_inputUp = 0x100;
            }
        } else {
            g->m8C_inputUp -= decel;
            if (g->m8C_inputUp < 0) {
                g->m8C_inputUp = 0;
            }
        }
        if ((held & 0x4000) && canAccel) {
            g->m94_inputDown += accel;
            if (g->m94_inputDown > 0x100) {
                g->m94_inputDown = 0x100;
            }
        } else {
            g->m94_inputDown -= decel;
            if (g->m94_inputDown < 0) {
                g->m94_inputDown = 0;
            }
        }
        if ((held & 0x8000) && canAccel) {
            g->m88_inputLeft += accel;
            if (g->m88_inputLeft > 0x100) {
                g->m88_inputLeft = 0x100;
            }
        } else {
            g->m88_inputLeft -= decel;
            if (g->m88_inputLeft < 0) {
                g->m88_inputLeft = 0;
            }
        }
        if ((held & 0x2000) && canAccel) {
            g->m90_inputRight += accel;
            if (g->m90_inputRight > 0x100) {
                g->m90_inputRight = 0x100;
            }
        } else {
            g->m90_inputRight -= decel;
            if (g->m90_inputRight < 0) {
                g->m90_inputRight = 0;
            }
        }
        if (!(held & 0xF000)) {
            angle = 0;
        }
    } else {
        if (SquareRoot0(x * x + y * y) < 0x30) {
            angle = 0;
        }
        if (x < 0 && canAccel) {
            g->m8C_inputUp += accel;
            if (abs(x) < g->m8C_inputUp) {
                g->m8C_inputUp = abs(x);
            }
        } else {
            g->m8C_inputUp -= decel;
            if (g->m8C_inputUp < 0) {
                g->m8C_inputUp = 0;
            }
        }
        if (x > 0 && canAccel) {
            g->m94_inputDown += accel;
            if (x < g->m94_inputDown) {
                g->m94_inputDown = x;
            }
        } else {
            g->m94_inputDown -= decel;
            if (g->m94_inputDown < 0) {
                g->m94_inputDown = 0;
            }
        }
        if (y < 0 && canAccel) {
            g->m88_inputLeft += accel;
            if (abs(y) < g->m88_inputLeft) {
                g->m88_inputLeft = abs(y);
            }
        } else {
            g->m88_inputLeft -= decel;
            if (g->m88_inputLeft < 0) {
                g->m88_inputLeft = 0;
            }
        }
        if (y > 0 && canAccel) {
            g->m90_inputRight += accel;
            if (y < g->m90_inputRight) {
                g->m90_inputRight = y;
            }
        } else {
            g->m90_inputRight -= decel;
            if (g->m90_inputRight < 0) {
                g->m90_inputRight = 0;
            }
        }
    }
#endif
    x = g->m8C_inputUp - g->m94_inputDown;
    y = g->m90_inputRight - g->m88_inputLeft;
    angle2 = ratan2(y, x);
    mag = SquareRoot0(x * x + y * y);
    if (mag > 0x100) {
        mag = 0x100;
    }
    if (mag < 0x30) {
        mag = 0;
    }
    g->m48_speed = mag;
    if (mag != 0) {
        g->m58_targetRotation = angle2 + g->mCE_inputRotation;
    } else {
        g->m58_targetRotation = 0;
    }
    angle &= 0xFFF;
    if (angle >= 0x201 && angle < 0x600) {
        tryBattlingGearBackDash(g);
        g->mD0_status.flags |= 0x1000000;
    } else {
        g->mD0_status.flags &= ~0x1000000;
    }
    if ((g->mD0_status.flags & 0x80000) && g->m48_speed != 0) {
        if (abs((angle2 & 0xFFF) - ((ratan2(sy - 0x80, sx - 0x80) + 0x400) & 0xFFF)) > 0x200) {
            g->mD0_status.flags |= 0x80000;
        }
    }
    FALLOFF_RETURN(0); /* result unused */
}

s32 updateBattlingGearHealth(BattlingGear* g) {

    if (g->mC2_flash != 0) {
        g->mC2_flash -= 16;
    }
    if (g->mC4_moveState == 5) {
        IMPLICIT_RETURN(0);
    }
    if (g->mE8_stun > 20) {
        g->mE8_stun = 20;
    }
    if (g->mE8_stun != 0) {
        g->mE8_stun--;
    } else {
        g->mD0_status.flags &= ~0x1000;
    }
    if (g->m916_stagger != 0) {
        g->m916_stagger--;
    }
    if (g->m914_inputLock != 0) {
        g->m914_inputLock--;
    }
    if (g->mC8_comboTimer != 0) {
        g->mC8_comboTimer--;
    } else {
        g->mF0_comboHits = 0;
        g->mD0_status.flags = (g->mD0_status.flags & ~0x700000) | 0x300000;
    }
    if (g->mC3_backDashTimer != 0) {
        g->mC5_action = 4;
        g->mC3_backDashTimer--;
    } else if (g->mC5_action == 4) {
        g->mC5_action = 0;
    }
    if (g->mCA_downTimer != 0) {
        g->mCA_downTimer--;
    }
    if (g->mB6_gauge != 0) {
        if (g->mC4_moveState & 1) {
            if ((g->mD0_status.flags & 0x60000000) == 0x60000000) {
                if (g->m90A_flags & 4) {
                    g->mB6_gauge -= 8;
                } else {
                    g->mB6_gauge -= 2;
                }
            } else if ((g->mD0_status.flags & 0x60000000) == 0x20000000) {
                g->mB6_gauge -= 6;
            } else {
                g->mB6_gauge -= 4;
            }
        } else {
            g->mB6_gauge -= 8;
        }
        if (g->mB6_gauge < 0) {
            g->mB6_gauge = 0;
        }
    }
    if (g->mC4_moveState == 4) {
        if (g->mCA_downTimer == 0) {
            g->mC4_moveState = 0;
            g->mC5_action = 0;
        }
    } else {
        if (g->mB0_groundY - 0x30 < g->m0_position.vy) {
            g->mC4_moveState &= ~2;
        }
        if (g->m48_speed != 0) {
            g->mC4_moveState |= 1;
        } else {
            g->mC4_moveState &= ~1;
        }
    }
    if (g->mD0_status.flags & 0x800) {
        switch (g->m90E_holdAnimation) {
        case 9:
            if (g->mCA_downTimer == 0) {
                g->mD0_status.flags |= 0x400;
            }
            break;
        case 15:
            if (g->mC5_action != 4) {
                g->mD0_status.flags |= 0x400;
            }
            break;
        case 2:
            if (g->mC4_moveState & 2) {
                g->mD0_status.flags |= 0x400;
            }
            break;
        }
    }
    g->mB4_hp -= g->mBA_damage;
    g->mB4_hp -= g->m970_hitDamage;
    if (g->mBA_damage != 0) {
        if (g->m910_damageSfxTimer == 0) {
            if (getBattlingSceneMode() != 4) {
                playBattlingSfx(0x2D);
            }
            g->m910_damageSfxTimer = 20;
        }
        g->m910_damageSfxTimer--;
    } else {
        g->m910_damageSfxTimer = 0;
    }
    if (battlingEventFight != 0 && !isBattlingGearHpAbove(g, 0x50) && g->mD0_status.bits.b27_side) {
        g->mB4_hp = g->mBC_maxHp * 80 / 256;
    }
    if (g->mB4_hp < 0) {
        g->mB4_hp = 0;
    }
    if (g->mB8_barHp != g->mB4_hp) {
        g->mB8_barHp = g->mB4_hp;
        g->mC1_bar = g->mC0_hpBar;
        g->mC0_hpBar = R3000_DIV(g->mB4_hp * 128 + g->mBC_maxHp - 1, g->mBC_maxHp);
        if (g->mC0_hpBar == 1) {
            g->mC0_hpBar = 2;
        }
        g->mC2_flash = 0xF0;
    }
    g->mBA_damage = 0;
    g->m970_hitDamage = 0;
    if (battlingGameMode != 4 && g->mB4_hp == 0 && battlingGameMode != 6 && !(g->mD0_status.flags & 0x800000)) {
        g->m4C_animation = 13;
        g->m4E_lastAnimation = 0xFF;
        g->mD0_status.flags |= 0x1000;
        g->mC4_moveState = 5;
        g->mC5_action = 0;
        g->mD0_status.flags |= 0x400;
        g->m10_velocity.vy -= 0x50;
        if (g->mD0_status.bits.b27_side) {
            g->m20_knockback.vx -= getAngleCos(battlingGearAngle) >> 9;
            g->m20_knockback.vz -= getAngleSin(battlingGearAngle) >> 9;
        } else {
            g->m20_knockback.vx += getAngleCos(battlingGearAngle) >> 9;
            g->m20_knockback.vz += getAngleSin(battlingGearAngle) >> 9;
        }
        g->mD0_status.flags |= 0x800000;
        vibrateBattlingGearPad(g, 20);
        g->m100_hitType = 0;
    }
    FALLOFF_RETURN(0); /* result unused */
}

s32 pushBattlingGearKnockback(BattlingGear* g, s32 angle, s32 shift, s32 dy) {
    s32 clamped;

    g->m20_knockback.vx += getAngleCos(angle) >> shift;
    g->m20_knockback.vz += getAngleSin(angle) >> shift;
    if (g->m10_velocity.vy >= -0x8B) {
        g->m10_velocity.vy -= dy;
    }
    clamped = g->m10_velocity.vy < -0x82;
    if (clamped) {
        g->m10_velocity.vy = clamped = -0x82;
    }
    return clamped;
}

void knockBattlingGearDown(BattlingGear* g, s32 angle, s32 shift, s32 dy) {
    g->m4C_animation = 8;
    g->m4E_lastAnimation = 0xFF;
    g->mCA_downTimer = 0x3C;
    g->m916_stagger = 0x28;
    g->mE8_stun = 0x28;
    g->mC4_moveState = 4;
    g->mC5_action = 0;
    g->mD0_status.flags |= 0x1000;
    g->mD4_cues.flags &= ~0x10;
    g->mD0_status.flags &= ~0x400;
    pushBattlingGearKnockback(g, angle, shift, dy);
}

s32 vibrateBattlingGearPad(BattlingGear* g, s32 duration) {
    if (getBattlingSceneMode() != 4) {
        if (g->mD0_status.bits.b27_side) {
            if ((battlingGameMode == 2 || battlingGameMode == 4) && (battlingSettings.m4_p2Vibration & 1) && !(g->mD0_status.flags & 0x40)) {
                startPadVibration(1, duration);
            }
        } else {
            if ((battlingSettings.m3_p1Vibration & 1) && !(g->mD0_status.flags & 0x40)) {
                startPadVibration(0, duration);
            }
        }
    }
    FALLOFF_RETURN(0); /* result unused */
}

s32 reactBattlingGearToHit(BattlingGear* g) {
    s32 rumble;
    s32 angle;
    s32 k;

    rumble = 4;
    g->mF0_comboHits++;
    if (g->mCA_downTimer != 0) {
        if (g->m4C_animation == 9) {
            g->m4C_animation = 12;
            g->m4E_lastAnimation = 0xFF;
            g->mD0_status.flags |= 0x1000;
            goto end;
        }
    } else {
        if (g->mD0_status.flags & 4) {
            rumble = 2;
            goto end;
        }
        if (g->m974_hitPoint.vy < (g->m92C_topPoint.vy - g->m93C_footPoint.vy) * 2 / 3 + g->m93C_footPoint.vy) {
            if ((g->mD0_status.flags & 0x700000) == 0x100000) {
                g->m4C_animation = 7;
                g->m4E_lastAnimation = 0xFF;
                g->mD0_status.flags = (g->mD0_status.flags | 0x1000) & ~0x700000;
            } else {
                g->m4C_animation = 6;
                g->m4E_lastAnimation = 0xFF;
                g->mD0_status.flags = ((g->mD0_status.flags | 0x1000) & ~0x700000) | 0x100000;
            }
        } else {
            if ((g->mD0_status.flags & 0x700000) == 0x200000) {
                g->m4C_animation = 7;
                g->m4E_lastAnimation = 0xFF;
                g->mD0_status.flags = (g->mD0_status.flags | 0x1000) & ~0x700000;
            } else {
                g->m4C_animation = 5;
                g->m4E_lastAnimation = 0xFF;
                g->mD0_status.flags = ((g->mD0_status.flags | 0x1000) & ~0x700000) | 0x200000;
            }
        }
    }
    angle = ratan2(g->m0_position.vx - g->m974_hitPoint.vx, g->m0_position.vz - g->m974_hitPoint.vz);
    if (getBattlingDistanceXZ(&g->m0_position, &g->mD8_opponent->m0_position) < getBattlingDistanceXZ(&g->m974_hitPoint, &g->mD8_opponent->m0_position)) {
        angle += 0x800;
    }
    k = 1;
    switch ((g->m100_hitType - 1) & 7) {
    case 2:
        g->m4C_animation = 6;
        g->m4E_lastAnimation = 0xFF;
        g->mD0_status.flags |= 0x1000;
        pushBattlingGearKnockback(g, angle, 12, 100);
        goto end;
    case 3:
        knockBattlingGearDown(g, angle, 11, 0x80);
        goto end;
    case 1:
        k = 0;
        break;
    }
    if (g->m916_stagger >= 0x26 || g->mF0_comboHits >= 4) {
        knockBattlingGearDown(g, angle, 10, 0xA0);
        rumble = 15;
        g->mE8_stun = 0;
        g->m916_stagger = 0;
    } else {
        pushBattlingGearKnockback(g, angle, 10, k ? 0x1E : 0);
    }
end:
    vibrateBattlingGearPad(g, rumble);
    FALLOFF_RETURN(0); /* result unused */
}

BattlingMove* advanceBattlingGearCombo(BattlingGear* g, s32 which) {
    s32 i = g->m9C3_move * 2;
    if (which) {
        g->m9C3_move = battlingComboNextMoves[i];
    } else {
        g->m9C3_move = battlingComboNextMoves[i + 1];
    }
    return &g->m80_moves[g->m9C3_move];
}

void setBattlingGearFootEffectFlag(BattlingGear* g) {
    g->mD0_status.flags |= 0x80000;
}

s32 updateBattlingGearActions(BattlingGear* g) {
    BattlingMove* cmd;
    s32 flag;

    if (g->mC4_moveState == 5) {
        IMPLICIT_RETURN(0);
    }
    if (g->m100_hitType != 0) {
        reactBattlingGearToHit(g);
        IMPLICIT_RETURN(0);
    }
    flag = 0;
    if (g->mE8_stun != 0) {
        clearBattlingGearInput(g);
        IMPLICIT_RETURN(0);
    }
    g->mD0_status.flags &= ~1;
    if (g->m914_inputLock != 0) {
        clearBattlingGearInput(g);
    }
    if ((g->mD0_status.flags & 0x800) && g->mC5_action != 4) {
        g->mC5_action = 0;
    }
    if (g->mC5_action == 0 && g->mC4_moveState != 4) {
        switch (popBattlingGearInput(g)) {
        case 0:
            clearBattlingGearInput(g);
            break;
        case 1:
            if (g->m914_inputLock != 0) {
                break;
            }
            cmd = advanceBattlingGearCombo(g, 1);
            goto have_cmd;
        case 2:
            if (g->m914_inputLock != 0) {
                break;
            }
            cmd = advanceBattlingGearCombo(g, 0);
        have_cmd:
            if (g->mC4_moveState & 2) {
                break;
            }
            if (cmd->m0_anim != 0) {
                g->m4C_animation = cmd->m0_anim + 0x12;
                g->m4E_lastAnimation = 0xFF;
                g->mD0_status.flags |= 0x1000;
                if (cmd->m1_lunge != 0) {
                    flag = 1;
                }
                g->mD0_status.flags |= 1;
                g->mC5_action = 2;
                g->m84_move = cmd;
                g->m644_moveDamage = g->m1600_def->mA_moves[g->m9C3_move - 1] * g->m1600_def->m0_power / 100;
            } else {
                g->m914_inputLock = 15;
                clearBattlingGearInput(g);
            }
            break;
        case 3:
            if (g->m914_inputLock == 0 && g->m1600_def->m18_etherPower != 0 && g->m909_gearId != 0x29) {
                if (!(g->mC4_moveState & 2)) {
                    g->m4C_animation = 3;
                    g->m4E_lastAnimation = 0xFF;
                    g->mD0_status.flags |= 0x1000;
                } else {
                    g->m4C_animation = 4;
                    g->m4E_lastAnimation = 0xFF;
                    g->mD0_status.flags |= 0x1000;
                }
                flag = 1;
                g->mC5_action = 2;
                g->mD0_status.flags |= 1;
            }
            break;
        case 4:
            if (g->mCA_downTimer != 0) {
                IMPLICIT_RETURN(0);
            }
            if (g->mC4_moveState & 2) {
                break;
            }
            spawnBattlingHitEffect(&g->m93C_footPoint, 9);
            g->m10_velocity.vy -= 0x8C;
            g->mC4_moveState |= 2;
            stopBattlingGearHitSound(g, 10);
            playBattlingGearHitSound(g, 10, &g->m93C_footPoint, 2);
            break;
        case 5:
            g->m4C_animation = 15;
            g->m4E_lastAnimation = 0xFF;
            g->mD0_status.flags |= 0x1000;
            playBattlingGearHitSound(g, 14, g, 2);
            g->mC3_backDashTimer = 6;
            g->mC5_action = 4;
            break;
        }
    }
    g->m52_moveAnimSpeed = 0;
    g->mD0_status.flags &= ~4;
    switch (g->mC4_moveState) {
    case 0:
        g->m58_targetRotation = 0;
        switch (g->mC5_action) {
        case 0:
            if (g->mD0_status.flags & 2) {
                g->m4C_animation = 0x11;
                if (g->mD0_status.bits.b3_combo == 2) {
                    g->mD0_status.flags |= 4;
                } else {
                    g->mD0_status.bits.b3_combo++;
                }
            } else {
                g->m4C_animation = 0;
            }
            break;
        case 2:
            if (flag) {
                g->m44_lungeSpeed += 0x40;
            }
            break;
        case 4:
            setBattlingGearFootEffectFlag(g);
            break;
        }
        g->mFC_turnRotation = stepBattlingAngleTowards(g->mFC_turnRotation, g->m58_targetRotation, 0x100);
        break;
    case 1:
        g->m52_moveAnimSpeed = g->m48_speed * g->m15F2_speedScale / 4096;
        switch (g->mC5_action) {
        case 0:
            g->m4C_animation = 1;
            break;
        case 2:
            if (flag) {
                g->m44_lungeSpeed += 0x40;
            }
            break;
        case 4:
            setBattlingGearFootEffectFlag(g);
            break;
        }
        g->mFC_turnRotation = stepBattlingAngleTowards(g->mFC_turnRotation, g->m58_targetRotation, 0x100);
        if ((g->mD0_status.flags & 0x60000000) == 0x20000000 && !(g->m90A_flags & 2)) {
            g->mD0_status.flags &= ~0x8000;
        }
        break;
    case 2:
        g->m48_speed = 0;
        g->m44_lungeSpeed = 0;
        g->m58_targetRotation = 0;
        if (g->mC5_action == 0) {
            g->m4C_animation = 2;
        }
        break;
    case 3:
        if (g->mC5_action == 0) {
            g->m4C_animation = 2;
        }
        break;
    case 4:
        g->m48_speed = 0;
        break;
    }
    g->m40_moveSpeed = (g->m48_speed * g->m15F0_moveScale * g->m15F2_speedScale) >> 16;
    if (!(g->mD0_status.flags & 0x8000)) {
        g->mD4_cues.flags &= ~0x40;
    }
    if ((g->mD0_status.flags & 0x8000) && g->mC5_action == 0 && !(g->mD4_cues.flags & 0x40) && g->m40_moveSpeed > 0x30
        && g->mC4_moveState != 2 && g->mC4_moveState != 3) {
        if (addBattlingGearGauge(g, 0x20, 2)) {
            g->m40_moveSpeed *= 2;
            g->m52_moveAnimSpeed *= 2;
        } else {
            g->mD0_status.flags &= ~0x8000;
            g->mD4_cues.flags |= 0x40;
        }
        if (battlingCameraLag >= 2) {
            battlingCameraLag = 1;
        }
    }
    if (g->mC4_moveState == 0 || (g->mD4_cues.flags & 0x40)) {
        g->mD0_status.flags &= ~0x8000;
    }
    FALLOFF_RETURN(0); /* result unused */
}

void snapBattlingGearToFeet(BattlingGear* g) {
    g->m0_position = g->m93C_footPoint;
    g->m4C_animation = 0;
    g->mC4_moveState = 0;
    g->mC5_action = 0;
    g->mD0_status.flags |= 0x2000000;
}

void computeBattlingGearVelocity(BattlingGear* g) {
    VECTOR d;
    s32 angle;
    s32 speed;
    s32 scale;

    g->m6C_prevPosition = g->m0_position;
    g->m95C_prevFootPoint = g->m93C_footPoint;
    speed = g->m40_moveSpeed;
    angle = battlingGearAngle;
    if (g->mD0_status.bits.b27_side) {
        angle += 0x800;
    }
    if (g->mC4_moveState == 0) {
        if (g->mFC_turnRotation != 0) {
            g->m648_baseRotation = angle;
        } else {
            g->m648_baseRotation = stepBattlingAngleTowards(g->m648_baseRotation, angle, 0x100);
        }
        if (battlingCameraLag >= 5) {
            battlingCameraLag = 4;
        }
    } else if (g->mC4_moveState == 1) {
        if (battlingGearDistance > 0x100) {
            if (g->mFC_turnRotation != 0) {
                g->m648_baseRotation = angle;
            } else {
                g->m648_baseRotation = stepBattlingAngleTowards(g->m648_baseRotation, angle, 0x100);
            }
        }
        if (battlingCameraLag >= 3) {
            battlingCameraLag = 2;
        }
    }
    if (g->mC5_action == 4) {
        speed = -(g->mC3_backDashTimer * 80) / 3;
        g->m54_rotY = angle;
        g->m648_baseRotation = angle;
        g->mFC_turnRotation = 0;
        g->m40_moveSpeed = 0;
        g->m94_inputDown = 0;
        g->m8C_inputUp = 0;
        g->m88_inputLeft = 0;
        g->m90_inputRight = 0;
    } else {
        g->m54_rotY = g->m648_baseRotation + g->mFC_turnRotation;
    }
    if (g->mC5_action == 2) {
        if ((g->mD0_status.flags & 1) || (g->mC4_moveState & 2)) {
            g->m90C_attackRotation = angle;
        }
        g->m54_rotY = g->m90C_attackRotation;
        g->mD0_status.flags = (g->mD0_status.flags | 0x100) & ~0x80000;
    } else {
        g->mD0_status.flags &= ~0x100;
    }
    if (g->mD0_status.flags & 0x40000) {
        g->m10_velocity.vx = -(getAngleCos(g->m648_baseRotation + g->mFC_turnRotation) * speed) >> 12;
        g->m10_velocity.vz = -(getAngleSin(g->m648_baseRotation + g->mFC_turnRotation) * speed) >> 12;
    } else {
        g->mD0_status.flags &= ~0x80000;
    }
    g->m10_velocity.vy += 10;
    if (g->m44_lungeSpeed != 0 && g->m0_position.vy == g->mB0_groundY) {
        g->m44_lungeSpeed = g->m44_lungeSpeed * 160 / 256;
        g->m30_lungeVelocity.vx = -(getAngleCos(angle) * g->m44_lungeSpeed) >> 12;
        g->m30_lungeVelocity.vz = -(getAngleSin(angle) * g->m44_lungeSpeed) >> 12;
        g->m10_velocity.vx += g->m30_lungeVelocity.vx;
        g->m10_velocity.vy += g->m30_lungeVelocity.vy;
        g->m10_velocity.vz += g->m30_lungeVelocity.vz;
    }
    if (battlingGearDistance3D > 0x280 && battlingRubberBandMode != 0) {
        d.vx = g->mD8_opponent->m0_position.vx;
        d.vy = g->mD8_opponent->m0_position.vy;
        d.vz = g->mD8_opponent->m0_position.vz;
        d.vx -= g->m0_position.vx;
        d.vy -= g->m0_position.vy;
        d.vz -= g->m0_position.vz;
        d.vx /= 48;
        d.vy /= 48;
        d.vz /= 48;
        g->m10_velocity.vx += d.vx;
        g->m10_velocity.vy += d.vy;
        g->m10_velocity.vz += d.vz;
    }
    scale = 0x80;
    switch (g->mD0_status.bits.b29_ground) {
    case 0:
        scale = 0xFF;
        break;
    case 1:
    case 3:
        scale = 0;
        break;
    case 2:
        break;
    }
    if (g->mC4_moveState == 4) {
        scale = 0xFF;
    }
    if (g->mD0_status.flags & 0x40000) {
        g->m20_knockback.vx = g->m20_knockback.vx * scale / 256;
        g->m20_knockback.vz = g->m20_knockback.vz * scale / 256;
    }
    g->m10_velocity.vx += g->m20_knockback.vx;
    g->m10_velocity.vy += g->m20_knockback.vy;
    g->m10_velocity.vz += g->m20_knockback.vz;
}

s32 pickBattlingGearContactPoint(VECTOR* p, VECTOR* q, BattlingGear* g, VECTOR** out) {
    s32 dz, dx;
    s32 sa, sb, sc;
    s32 ab, ac;

    dz = p->vz - q->vz;
    dx = q->vx - p->vx;
    sa = dz * g->m6C_prevPosition.vx + dx * g->m6C_prevPosition.vz + p->vx * q->vz - q->vx * p->vz;
    sb = dz * g->m92C_topPoint.vx + dx * g->m92C_topPoint.vz + p->vx * q->vz - q->vx * p->vz;
    sc = dz * g->m93C_footPoint.vx + dx * g->m93C_footPoint.vz + p->vx * q->vz - q->vx * p->vz;
    if (sa < 0) {
        sa = -1;
    } else if (sa > 0) {
        sa = 1;
    }
    if (sb < 0) {
        sb = -1;
    } else if (sb > 0) {
        sb = 1;
    }
    if (sc < 0) {
        sc = -1;
    } else if (sc > 0) {
        sc = 1;
    }
    ab = sb * sa;
    ac = sc * sa;
    if (ab < 0) {
        if (ac < 0) {
            if (getBattlingDistanceXZ(&g->m92C_topPoint, p) > getBattlingDistanceXZ(&g->m93C_footPoint, p)) {
                *out = &g->m92C_topPoint;
            } else {
                *out = &g->m93C_footPoint;
            }
            return 0;
        }
        *out = &g->m92C_topPoint;
        return 1;
    }
    if (ac < 0) {
        *out = &g->m93C_footPoint;
        return 2;
    }
    if (getBattlingDistanceXZ(&g->m92C_topPoint, p) < getBattlingDistanceXZ(&g->m93C_footPoint, p)) {
        *out = &g->m92C_topPoint;
    } else {
        *out = &g->m93C_footPoint;
    }
    return 3;
}

s32 separateBattlingGears(BattlingGear* a, BattlingGear* b) {
    VECTOR unused;
    VECTOR ta;
    VECTOR tb;
    VECTOR mid;
    VECTOR perp;
    VECTOR* ra;
    VECTOR* rb;
    s32 radA, radB;
    s32 d, r, angle;
    s32 wa, wb, sum;

    if (battlingGearDistance <= 0x300 && ((a->m93C_footPoint.vy < b->m92C_topPoint.vy && b->m93C_footPoint.vy < a->m93C_footPoint.vy)
        || (b->m92C_topPoint.vy < a->m92C_topPoint.vy && a->m92C_topPoint.vy < b->m93C_footPoint.vy)
        || (b->m93C_footPoint.vy < a->m92C_topPoint.vy && a->m93C_footPoint.vy < b->m93C_footPoint.vy)
        || (a->m92C_topPoint.vy < b->m92C_topPoint.vy && b->m92C_topPoint.vy < a->m93C_footPoint.vy))) {
        mid.vx = (a->m6C_prevPosition.vx + b->m6C_prevPosition.vx) / 2;
        mid.vz = (a->m6C_prevPosition.vz + b->m6C_prevPosition.vz) / 2;
        perp.vz = b->m6C_prevPosition.vx - a->m6C_prevPosition.vx + mid.vz;
        perp.vx = a->m6C_prevPosition.vz - b->m6C_prevPosition.vz + mid.vx;
        radA = a->m8FC_params->m3_bodyRadius;
        radB = b->m8FC_params->m3_bodyRadius;
        pickBattlingGearContactPoint(&mid, &perp, a, &ra);
        pickBattlingGearContactPoint(&mid, &perp, b, &rb);
        ta = *ra;
        tb = *rb;
        ta.vx -= a->m6C_prevPosition.vx;
        ta.vy -= a->m6C_prevPosition.vy;
        ta.vz -= a->m6C_prevPosition.vz;
        tb.vx -= b->m6C_prevPosition.vx;
        tb.vy -= b->m6C_prevPosition.vy;
        tb.vz -= b->m6C_prevPosition.vz;
        ta.vx += a->m0_position.vx;
        ta.vy += a->m0_position.vy;
        ta.vz += a->m0_position.vz;
        tb.vx += b->m0_position.vx;
        tb.vy += b->m0_position.vy;
        tb.vz += b->m0_position.vz;
        d = getBattlingDistanceXZ(&ta, &tb);
        r = radA + radB;
        if (d < r) {
            angle = getBattlingDistanceXZ(&a->m6C_prevPosition, &ta);
            if (getBattlingDistanceXZ(&a->m6C_prevPosition, &tb) < angle) {
                d = d + r;
            } else {
                d = r - d;
            }
            angle = ratan2(a->m6C_prevPosition.vx - b->m6C_prevPosition.vx, a->m6C_prevPosition.vz - b->m6C_prevPosition.vz);
            wa = getBattlingVectorLengthXZ(&a->m10_velocity);
            wb = getBattlingVectorLengthXZ(&b->m10_velocity);
            sum = wa + wb;
            if (sum == 0) {
                wb = 1;
                wa = 1;
                sum = 2;
            }
            wa = (wa * d << 8) / sum;
            wb = (wb * d << 8) / sum;
            a->m0_position.vx += (getAngleCos(angle) * wb) >> 20;
            a->m0_position.vz += (getAngleSin(angle) * wb) >> 20;
            b->m0_position.vx -= (getAngleCos(angle) * wa) >> 20;
            b->m0_position.vz -= (getAngleSin(angle) * wa) >> 20;
        }
    }
    FALLOFF_RETURN(0); /* result unused */
}

void moveBattlingGearOnGround(BattlingGear* gear) {
    VECTOR unused; /* retail frame has an unused 16-byte slot */
    gear->mB0_groundY = getBattlingGroundHeight(gear, 1);
    gear->mD0_status.bits.b29_ground = getBattlingMapCell(gear) >> 24;
    if (gear->mD0_status.bits.b29_ground == 3) {
        gear->m90B_dustTimer = 0xF;
    }
    clampBattlingMoveToArena(gear, &gear->m10_velocity, 16000);
    gear->mD0_status.bits.b18 = 0;
    if (gear->mB0_groundY < gear->m0_position.vy + gear->m10_velocity.vy) {
        gear->mD0_status.bits.b18 = 1;
        if (gear->mC4_moveState == 4) {
            if (!(gear->mD4_cues.flags & 0x10)) {
                playBattlingGearHitSound(gear, 0xD, gear, 2);
                vibrateBattlingGearPad(gear, 6);
            }
            gear->mD4_cues.flags |= 0x10;
            if (gear->m10_velocity.vy >= 0x40) {
                gear->m10_velocity.vy = -gear->m10_velocity.vy / 3;
            } else {
                gear->m10_velocity.vy = 0;
            }
        } else {
            gear->m10_velocity.vy = 0;
        }
        gear->m0_position.vy = gear->mB0_groundY;
    }
    gear->m0_position.vx += gear->m10_velocity.vx;
    gear->m0_position.vy += gear->m10_velocity.vy;
    gear->m0_position.vz += gear->m10_velocity.vz;
}

void syncBattlingGearModelTransform(BattlingGear* gear) {
    gear->m5C_model->m34_translation.vx = gear->m0_position.vx;
    gear->m5C_model->m34_translation.vy = gear->m0_position.vy;
    gear->m5C_model->m34_translation.vz = gear->m0_position.vz;
    gear->m5C_model->m44_rotation.vy = gear->m54_rotY;
}

void func_80078ED4(s16* arg0) {
    arg0[0] = 0x100;
    arg0[2] = 0x10;
    arg0[1] = 0;
    arg0[3] = 0;
    arg0[4] = 0;
    arg0[5] = 0x30;
    arg0[6] = 0x30;
}

void initBattlingGear(BattlingGear* gear) {
    BattlingNode* model = gear->m5C_model;
    BattlingGearParams* params = gear->m8FC_params;
    BattlingSkeleton* sub;
    s32 i;

    gear->m0_position.vx = gear->m0_position.vy = gear->m0_position.vz = 0;
    gear->m10_velocity.vx = gear->m10_velocity.vy = gear->m10_velocity.vz = 0;
    gear->m20_knockback.vx = gear->m20_knockback.vy = gear->m20_knockback.vz = 0;
    sub = NODE_SKELETON(model);
    sub->m10_scale[0] = sub->m10_scale[1] = sub->m10_scale[2] = params->m20_scale;
    gear->m98_accel = 0x10;
    gear->m9C_decel = 0x10;
    gear->mA8 = 0x60;
    gear->mB4_hp = 0x12C;
    gear->mB8_barHp = 0x12C;
    gear->mBC_maxHp = 0x12C;
    gear->mBE_gaugeCost = 0x480;
    gear->mC2_flash = 0xF0;
    gear->m54_rotY = 0;
    gear->mC4_moveState = 0;
    gear->mC5_action = 0;
    gear->mB6_gauge = 0;
    gear->mBA_damage = 0;
    gear->m910_damageSfxTimer = 0;
    gear->mC0_hpBar = 0x7F;
    gear->mC1_bar = 0x7F;
    gear->m970_hitDamage = 0;
    gear->m9C3_move = 0;
    gear->mE8_stun = 0;
    gear->m916_stagger = 0;
    gear->mC8_comboTimer = 0;
    gear->mCA_downTimer = 0;
    for (i = 0; i < 8; i++) {
        gear->m64C_projectiles[i].m30_active = 0;
        gear->m64C_projectiles[i].m40_life = 0;
    }
    for (i = 15; i >= 0; i--) {
        gear->m104_shapes[i].m44_hit.b.m45_state = 0;
    }
    gear->mF4 = 1;
    clearBattlingGearInput(gear);
    gear->m4E_lastAnimation = 0xFF;
    gear->m4C_animation = 0;
    gear->mC3_backDashTimer = 0;
    gear->mD0_status.bits.b12 = 0;
    gear->mD0_status.bits.b25 = 1;
    gear->mD0_status.bits.b17_facing = 1;
    gear->mCE_inputRotation = gear->mCC_sideInputRotation + 0x800;
    gear->mD4_cues.bits.b2_3 = 0;
    gear->mD4_cues.bits.b0_1 = 0;
    gear->mD0_status.bits.b23 = 0;
    gear->m15F0_moveScale = params->mF_moveScale;
    gear->m15F2_speedScale = gear->m15F4_baseSpeedScale = params->mC_speedScale;
    gear->m15F6 = 0x100;
    gear->m15F8 = 0;
    gear->m1654_etherHpLoss = 0;
    gear->m1658_boostHpLoss = 0;
    gear->m165C_etherPresses = 0;
    gear->m1660_attackPresses = 0;
    gear->m90B_dustTimer = 0;
    gear->mD0_status.bits.b2 = 0;
    gear->mD0_status.bits.b1 = 0;
    gear->mD0_status.bits.b3_combo = 0;
    gear->mD4_cues.bits.b4 = 0;
    gear->m84_move = &battlingIdleMove;
    battlingIdleMove.m2_effects = 1;
    battlingSideStates[gear->mD0_status.bits.b27_side].m0_text = "";
    battlingSideStates[gear->mD0_status.bits.b27_side].m8_prevText = 0;
    battlingSideStates[gear->mD0_status.bits.b27_side].m4_strength = 0;
    battlingSideStates[gear->mD0_status.bits.b27_side].mC_startFrame = 0;
    battlingSideStates[gear->mD0_status.bits.b27_side].m10_endFrame = 0;
    initBattlingAi(gear);
    func_80078ED4(gear->m15D8);
    initBattlingGearShadowQuad(gear);
    gear->m914_inputLock = 0;
    gear->m1668_hitsTaken = 0;
    gear->mD4_cues.bits.b6 = 0;
}

void updateBattlingGearBodyPoints(BattlingGear* gear) {
    VECTOR v1;
    VECTOR v2;
    VECTOR unused; /* retail frame has an unused 16-byte slot */
    BattlingGearParams* params = gear->m8FC_params;

    getBattlingGearPartPosition(gear, params->m1_foot1Part, params->m4_foot1Vertex, &v1);
    getBattlingGearPartPosition(gear, params->m2_foot2Part, params->m6_foot2Vertex, &v2);
    gear->m93C_footPoint.vx = (v1.vx + v2.vx) / 2;
    gear->m93C_footPoint.vy = (v1.vy + v2.vy) / 2;
    gear->m93C_footPoint.vz = (v1.vz + v2.vz) / 2;
    if ((gear->mD0_status.flags & 0x80000) && (gear->mD0_status.flags & 0x60000000) != 0x20000000 && !(gear->m90A_flags & 1)) {
        spawnBattlingHitEffect(&v1, 9);
        spawnBattlingHitEffect(&v2, 9);
    }
    if (gear->m90B_dustTimer != 0) {
        if (v1.vy + 0x40 > gear->mB0_groundY - 0x20 && gear->m96E_prevFoot1Y + 6 < v1.vy) {
            spawnBattlingDust(&v1, 0x20);
        }
        if (v2.vy + 0x40 > gear->mB0_groundY - 0x20 && gear->m96C_prevFoot2Y + 6 < v2.vy) {
            spawnBattlingDust(&v2, 0x20);
        }
        gear->m90B_dustTimer--;
    }
    if ((gear->mD0_status.flags & 0x60000000) == 0x20000000) {
        spawnBattlingSplash(&gear->m93C_footPoint, &gear->m95C_prevFootPoint);
    }
    if (getBattlingSceneMode() != 7 && (gear->mD0_status.flags & 0x60000000) == 0x20000000 && gear->m93C_footPoint.vy > 0x80 &&
        gear->m95C_prevFootPoint.vy < 0x80) {
        playBattlingGearSound(gear, 0x3A, &gear->m93C_footPoint, 2);
    }
    gear->m96E_prevFoot1Y = v1.vy;
    gear->m96C_prevFoot2Y = v2.vy;
    getBattlingGearPartPosition(gear, params->m0_topPart, params->m8_topVertex, &gear->m92C_topPoint);
    gear->m92C_topPoint.vy -= 0x38;
    gear->m94C_target.vx = (gear->m93C_footPoint.vx + gear->m92C_topPoint.vx) / 2;
    gear->m94C_target.vz = (gear->m93C_footPoint.vz + gear->m92C_topPoint.vz) / 2;
    gear->m94C_target.vy = (gear->m93C_footPoint.vy + gear->m92C_topPoint.vy) / 2;
    if ((gear->mD0_status.flags & 0x8000) && gear->mC5_action == 0 && (gear->mC4_moveState & 1)) {
        setBattlingEffectColor(&gear->m15D4_color);
        spawnBattlingBeamEffect(gear->mD0_status.bits.b27_side, &gear->m92C_topPoint, &gear->m93C_footPoint, gear->mD0_status.bits.b27_side);
        if (!(gear->mD0_status.flags & 0x10000)) {
            stopBattlingGearHitSound(gear, 8);
            playBattlingGearHitSound(gear, 8, &gear->m93C_footPoint, 2);
        }
    }
    if (gear->mC5_action != 4) {
        if (gear->mC4_moveState != 1) {
            gear->mD4_cues.bits.b0_1 = 0;
        } else {
            gear->mD4_cues.bits.b0_1 = 1;
            if (gear->mD0_status.flags & 0x8000) {
                gear->mD4_cues.bits.b0_1 = 2;
            }
        }
        if (gear->mC5_action == 2) {
            gear->mD4_cues.bits.b0_1 = 3;
        }
    }
    if (gear->mD4_cues.bits.b0_1 != gear->mD4_cues.bits.b2_3) {
        switch (gear->mD4_cues.bits.b0_1) {
        case 0:
        case 3:
            stopBattlingGearHitSound(gear, 7);
            stopBattlingGearHitSound(gear, 9);
            break;
        case 1:
            playBattlingGearHitSound(gear, 7, gear, 2);
            stopBattlingGearHitSound(gear, 9);
            break;
        case 2:
            playBattlingGearHitSound(gear, 9, &gear->m93C_footPoint, 2);
            stopBattlingGearHitSound(gear, 7);
            break;
        }
    }
    gear->mD0_status.bits.b16 = gear->mD0_status.bits.b15_scriptTurn;
    gear->mC6_prevMoveState = gear->mC4_moveState;
    gear->mD4_cues.bits.b2_3 = gear->mD4_cues.bits.b0_1;
}

void updateBattlingFightCamera(BattlingGear* a, BattlingGear* b) {
    VECTOR v1;
    VECTOR v2;
    s32 angle;
    s32 dist;
    s32 r;
    s32 len;
    s32 y;
    s32 t;

    if (battlingEventStarted == 0) {
        angle = ratan2(a->m0_position.vx - b->m0_position.vx, a->m0_position.vz - b->m0_position.vz);
        len = getBattlingDistanceXZ(a, b);
        battlingGearAngle = angle;
        battlingGearDistance = len;
        dist = getBattlingDistance(a, b);
        r = dist * 2 / 3 + 0xC0;
        battlingCameraTarget.vx = (a->m0_position.vx + b->m0_position.vx) / 2;
        battlingCameraTarget.vy = (a->m0_position.vy + b->m0_position.vy) / 2 - 0xA0;
        battlingCameraTarget.vz = (a->m0_position.vz + b->m0_position.vz) / 2;
        v1.vx = battlingCameraTarget.vx + ((getAngleCos(angle - 0x400) * r) >> 12);
        v1.vz = battlingCameraTarget.vz + ((getAngleSin(angle - 0x400) * r) >> 12);
        v2.vx = battlingCameraTarget.vx + ((getAngleCos(angle + 0x400) * r) >> 12);
        v2.vz = battlingCameraTarget.vz + ((getAngleSin(angle + 0x400) * r) >> 12);
        v1.vx -= battlingCameraPos.vx;
        v1.vy -= battlingCameraPos.vy;
        v1.vz -= battlingCameraPos.vz;
        v2.vx -= battlingCameraPos.vx;
        v2.vy -= battlingCameraPos.vy;
        v2.vz -= battlingCameraPos.vz;
        len = getBattlingVectorLengthXZ(&v1);
        if (getBattlingVectorLengthXZ(&v2) < len) {
            battlingCameraSideAngle = 0x400;
            battlingCameraSide = 0;
        } else {
            battlingCameraSideAngle = -0x400;
            battlingCameraSide = 1;
        }
        dist /= 4;
        if (dist > 0x300) {
            dist = 0x300;
        }
        y = dist + 0x40;
        v1.vy = battlingCameraTarget.vy - y;
        v1.vx = battlingCameraTarget.vx + ((getAngleCos(angle + battlingCameraSideAngle) * r) >> 12);
        v1.vz = battlingCameraTarget.vz + ((getAngleSin(angle + battlingCameraSideAngle) * r) >> 12);
        v2.vx = (v1.vx - battlingCameraPos.vx) / battlingCameraLag;
        v2.vz = (v1.vz - battlingCameraPos.vz) / battlingCameraLag;
        clampBattlingMoveToArena(&battlingCameraPos, &v2, 0x3A00);
        battlingCameraPos.vx += v2.vx;
        battlingCameraPos.vz += v2.vz;
        len = getBattlingGroundHeight(&battlingCameraPos, 0) - 0x100;
        if (len < v1.vy) {
            v1.vy = len;
        }
        t = (v1.vy - battlingCameraPos.vy) / battlingCameraLag;
        battlingCameraLag = 100;
        battlingCameraPos.vy += t;
    }
}

void initBattlingSettings(void) {
    createBattlingHitSparkEmitter();
    createBattlingSparkEmitter();
    loadBattlingSettings();
    battlingSettings.m5_p1Com = 1;
    battlingSettings.m9_speedIndex = 3;
    battlingSpeedScale = 0x100;
    battlingSettings.m6_p2Com = 0;
    battlingSettings.m2_frameSkip = 0;
    battlingSettings.mA_comAi = 0;
    battlingSettings.mC_speed = 0x100;
}

void func_80079B04(void) {
}

void resetBattlingMatch(void) {
    battlingRoundNumber = 0;
    battlingGear1.mF2_wins = 0;
    battlingGear2.mF2_wins = 0;
    battlingDrawGameCount = 0;
    battlingFightTime = 0;
    battlingActivePad = 0;
}

s32 startBattlingRound(void) {
    battlingGear1.mCC_sideInputRotation = 0x400;
    battlingGear2.mCC_sideInputRotation = -0x400;
    initBattlingGear(&battlingGear1);
    initBattlingGear(&battlingGear2);
    clearBattlingEmitterParticles(battlingHitSparkEmitter);
    clearBattlingSplashAndDust();
    clearBattlingSpriteEffects();
    startBattlingIntroCamera();
    battlingCameraSide = 1;
    battlingCameraSideAngle = -1;
    setBattlingSparkBurst(0);
    battlingRoundOver = 0;
    battlingKoTimer = 0;
    battlingRoundWinner = 0;
    battlingRoundIntroTimer = 0x5A;
    battlingRoundFrames = 0;
    battlingHitSparkCount = 0;
    hideBattlingHelpLabels();
    battlingResult = 0;
    initBattlingSoundChannels();
    resetBattlingEvent();
    battlingFightActive = 0;
    D_800928F0 = 0;
    battlingGearAnimInfo[10].m0_type = 0;
    battlingGearAnimInfo[10].m1_next = 0;
    battlingFrameSkipTimer = 0;
    battlingRoundNumber++;
    dealocateDialogWindow18MemorySub(&battlingMessageWindow);
    battlingSettings.m2_frameSkip = 0;
    if (battlingEntryParams != 0) {
        switch ((battlingRoundNumber - 1) % 5) {
        case 3:
            battlingStageColorIndex = 1;
            break;
        case 4:
            battlingStageColorIndex = 2;
            break;
        default:
            battlingStageColorIndex = 0;
            break;
        }
    }
    FALLOFF_RETURN(0); /* result unused */
}

void saveBattlingGearReplayFlags(BattlingGear* gear) {
    gear->m15CC_replayCur->mA_b8 = gear->mD0_status.bits.b2;
    gear->m15CC_replayCur->m6_angleFlags.bits.b13 = gear->mD0_status.bits.b15_scriptTurn;
    gear->m15CC_replayCur->m6_angleFlags.bits.b14 = gear->mD0_status.bits.b19;
}

void loadBattlingGearReplayFlags(BattlingGear* gear) {
    gear->mD0_status.bits.b2 = gear->m15CC_replayCur->mA_b8;
    gear->mD0_status.bits.b15_scriptTurn = gear->m15CC_replayCur->m6_angleFlags.bits.b13;
    gear->mD0_status.bits.b19 = gear->m15CC_replayCur->m6_angleFlags.bits.b14;
}

void resetBattlingKoTimer(void) {
    battlingKoTimer = 0;
}

void updateBattlingFight(BattlingGear* a, BattlingGear* b) {
#if VERSION_JP
    s32 swapped;
    u16 pressed;
#endif

    if (battlingFrameSkipTimer != 0) {
        battlingFrameSkipTimer--;
        return;
    }
    battlingFrameSkipTimer = battlingSettings.m2_frameSkip;
    a->m15CC_replayCur = &a->m9CC_replay[battlingReplayIndex];
    b->m15CC_replayCur = &b->m9CC_replay[battlingReplayIndex];
    a->m15D0_replayNext = a->m15CC_replayCur;
    b->m15D0_replayNext = b->m15CC_replayCur;
    battlingReplayIndex++;
    a->mD0_status.bits.b6 = battlingSettings.m5_p1Com;
    b->mD0_status.bits.b6 = battlingSettings.m6_p2Com;
    updateBattlingProjectiles(a);
    updateBattlingProjectiles(b);
    beginBattlingGearFrame(a);
    beginBattlingGearFrame(b);
    battlingHitSparkCount = 0;
    checkBattlingGearHits(a);
    checkBattlingGearHits(b);
    setDebugFontLineStart(0xA, 0x60);
    showBattlingAnalogWarning(0);
#if VERSION_JP
    swapped = 0;
#endif
    if (battlingFightActive != 0) {
        if (getInputOverflowed()) {
            resetInputs();
        } else {
        loop:
            if (loadInputFromVSyncBuffer()) {
#if VERSION_JP
                pressed = newPadButtonForDialogs;
                if (((pressed | newPadButtonForDialogs2) & 0x800) && battlingRoundIntroTimer < 0x14) {
                    if (pressed & 0x800) {
#else
                if ((((newPadButtonForDialogs | newPadButtonForDialogs2) & 0x800) && battlingRoundIntroTimer < 0x14) || isControllerConnected(0) == 0 ||
                    (isControllerConnected(1) == 0 && battlingGameMode == 2)) {
                    if ((newPadButtonForDialogs & 0x800) || isControllerConnected(0) == 0) {
#endif
                        battlingActivePad = 0;
                    } else {
                        if (b->mD0_status.bits.b6) {
                            goto next;
                        }
                        battlingActivePad = 1;
                    }
                    if (battlingEventFight == 0) {
                        openBattlingPauseMenu(battlingGameMode == 4 ? 2 : 1);
                    }
                }
            next:
#if VERSION_JP
                /* JP: pads swapped (2P side) */
                if (D_80091C98 & 1) {
                    u16 t;
                    t = newPadButtonForScripts;
                    newPadButtonForScripts = newPadButtonForScripts2;
                    newPadButtonForScripts2 = t;
                    t = newPadButtonForDialogs;
                    newPadButtonForDialogs = newPadButtonForDialogs2;
                    newPadButtonForDialogs2 = t;
                    t = newPadButtonForField;
                    newPadButtonForField = newPadButtonForField2;
                    newPadButtonForField2 = t;
                    if (!swapped) {
                        t = pad0LeftStickX;
                        pad0LeftStickX = pad1LeftStickX;
                        pad1LeftStickX = t;
                        t = pad0LeftStickY;
                        pad0LeftStickY = pad1LeftStickY;
                        pad1LeftStickY = t;
                        swapped = 1;
                    }
                }
#endif
                readBattlingGearPadInput(a);
                readBattlingGearPadInput(b);
                goto loop;
            }
        }
    } else {
        mergeQueuedInputs();
    }
    if (battlingFightActive != 0) {
        if (battlingFightTime != 0x2BF1F) {
            battlingFightTime++;
        }
        if (battlingGameMode == 4) {
            updateBattlingPracticeAi(b);
        } else {
            updateBattlingAi(a, NULL);
            updateBattlingAi(b, NULL);
        }
    }
    if (battlingRoundOver != 0) {
        a->m48_speed = 0;
        b->m48_speed = 0;
        clearBattlingGearInput(a);
        clearBattlingGearInput(b);
    }
    updateBattlingRound(a, b);
    updateBattlingGearHealth(a);
    updateBattlingGearHealth(b);
    setDebugFontLineStart(0x4A, 0);
    updateBattlingGearActions(a);
    setDebugFontLineStart(0x6A, 0);
    updateBattlingGearActions(b);
    setDebugFontLineStart(0xA, 0x80);
    computeBattlingGearVelocity(a);
    computeBattlingGearVelocity(b);
    moveBattlingGearOnGround(b);
    moveBattlingGearOnGround(a);
    separateBattlingGears(a, b);
    syncBattlingGearModelTransform(b);
    syncBattlingGearModelTransform(a);
    updateBattlingEvent();
    updateBattlingGearAnimation(a);
    updateBattlingGearAnimation(b);
    saveBattlingGearReplayFlags(a);
    saveBattlingGearReplayFlags(b);
    updateBattlingGearBodyPoints(a);
    updateBattlingGearBodyPoints(b);
    runBattlingGearAnimEvents(a, a->m998_stepStartFrame, a->m99A_stepFrames);
    runBattlingGearAnimEvents(b, b->m998_stepStartFrame, b->m99A_stepFrames);
    updateBattlingSpriteEffects();
    if (battlingRubberBandMode != 0) {
        spawnBattlingLightningKind(&a->m94C_target, &b->m94C_target, 0x13);
    }
}

void startBattlingReplay(s32 arg0) {
    if (arg0 < 0xFF) {
        battlingResultTimer = arg0 - 2;
        battlingReplayIndex -= arg0;
    } else {
        battlingResultTimer = 0xFF;
    }
    battlingResultGearPoints[0] = battlingGear1.m0_position;
    battlingResultGearPoints[1] = battlingGear2.m0_position;
    battlingResultGearPoints[2] = battlingGear1.m93C_footPoint;
    battlingResultGearPoints[3] = battlingGear2.m93C_footPoint;
    battlingResultGearPoints[3].vy = 0x100;
    battlingResultGearPoints[2].vy = 0x100;
    battlingResultGearPoints[1].vy = 0x100;
    battlingResultGearPoints[0].vy = 0x100;
}

void updateBattlingReplay(BattlingGear* a, BattlingGear* b) {
    u8 idx = battlingReplayIndex;

    battlingReplayIndex = idx + 1;
    a->m15CC_replayCur = &a->m9CC_replay[idx];
    b->m15CC_replayCur = &b->m9CC_replay[idx];
    a->m15D0_replayNext = &a->m9CC_replay[battlingReplayIndex];
    b->m15D0_replayNext = &b->m9CC_replay[battlingReplayIndex];
    if (battlingResultTimer == 0xFF) {
        a->m15D0_replayNext->m6_angleFlags.bits.b12 = 1;
        b->m15D0_replayNext->m6_angleFlags.bits.b12 = 1;
        a->m15D0_replayNext->mA = 1;
        b->m15D0_replayNext->mA = 1;
        a->m15D0_replayNext->m9_frameStep = 1;
        b->m15D0_replayNext->m9_frameStep = 1;
    }
    if (battlingFrameCounter & 8) {
        extern s32 battlingGlyphCount; /* the text glyphs so far */
        s32 glyphs = battlingGlyphCount;
        setBattlingTextPos(0x10, 0x10);
        printBattlingText("REPLAY");
        pcWideBattlingGlyphs(glyphs, battlingGlyphCount, 0); /* to the left edge (wide.c) */
    }
    updateBattlingProjectiles(a);
    updateBattlingProjectiles(b);
    beginBattlingGearFrame(a);
    beginBattlingGearFrame(b);
    a->m95C_prevFootPoint = a->m93C_footPoint;
    b->m95C_prevFootPoint = b->m93C_footPoint;
    a->m6C_prevPosition = a->m0_position;
    b->m6C_prevPosition = b->m0_position;
    battlingHitSparkCount = 0;
    if (battlingCameraLag >= 2) {
        battlingCameraLag = 1;
    }
    mergeQueuedInputs();
    if (battlingActivePad == 1) {
        if (newPadButtonForDialogs2 & 0x20) {
            setBattlingSceneMode(8);
        }
        turnBattlingOrbitCamera(newPadButtonForScripts2);
    } else {
        if (newPadButtonForDialogs & 0x20) {
            setBattlingSceneMode(8);
        }
        turnBattlingOrbitCamera(newPadButtonForScripts);
    }
    playBattlingGearReplayFrame(a);
    playBattlingGearReplayFrame(b);
    syncBattlingGearModelTransform(a);
    syncBattlingGearModelTransform(b);
    a->mD0_status.bits.b29_ground = (getBattlingMapCell(a) & 0x3000000) >> 24;
    b->mD0_status.bits.b29_ground = (getBattlingMapCell(b) & 0x3000000) >> 24;
    loadBattlingGearReplayFlags(a);
    loadBattlingGearReplayFlags(b);
    updateBattlingGearBodyPoints(a);
    updateBattlingGearBodyPoints(b);
    checkBattlingGearHits(a);
    checkBattlingGearHits(b);
    updateBattlingSpriteEffects();
    if (battlingRubberBandMode != 0) {
        spawnBattlingLightningKind(&a->m94C_target, &b->m94C_target, 0x13);
    }
    if (battlingResultTimer == 0xFF) {
        startBattlingOrbitCamera();
    }
    if (battlingResultTimer == 0) {
        setBattlingSceneMode(8);
    }
    battlingResultTimer--;
}

void setBattlingGearWinnerPose(BattlingGear* gear) {
    battlingSpeedScale = 0x100;
    if (battlingResultTimer < 0x78) {
        gear->m4F_animSpeed = 0x10;
        gear->m52_moveAnimSpeed = 0;
        gear->m4C_animation = 0x10;
        gear->mD0_status.flags |= 0x400;
    } else {
        gear->m4F_animSpeed = 0x10;
        gear->m52_moveAnimSpeed = 0;
        gear->m4C_animation = 0;
        gear->mD0_status.flags |= 0x400;
    }
}

void setBattlingGearLoserPose(BattlingGear* gear) {
    battlingSpeedScale = 0x100;
    gear->m4F_animSpeed = 0x10;
    gear->m52_moveAnimSpeed = 0;
    gear->m4C_animation = 9;
    gear->mD0_status.flags |= 0x2000400;
}

void startBattlingWinnerCamera(BattlingGear* gear) {
    s32 angle;
    s32 y;
    BattlingGearParams* params = gear->m8FC_params;

    battlingWinCamAngle = params->m24_winCamAngle;
    battlingWinCamTargetHeight = params->m28_winCamTargetHeight;
    battlingWinCamHeight = params->m26_winCamHeight;
    battlingWinCamDistance = params->m2A_winCamDistance;
    SetGeomScreen(0x200);
    angle = rand();
    battlingCameraTarget.vy = y = gear->m0_position.vy;
    battlingCameraTarget.vx = gear->m0_position.vx;
    y -= 0x400;
    battlingCameraTarget.vz = gear->m0_position.vz;
    battlingCameraPos.vy = y - battlingWinCamHeight;
    battlingCameraPos.vx = battlingCameraTarget.vx + ((getAngleCos(angle) << 2) * battlingWinCamDistance >> 12);
    battlingCameraPos.vz = battlingCameraTarget.vz + ((getAngleSin(angle) << 2) * battlingWinCamDistance >> 12);
}

void prepareBattlingRoundResult(void) {
    BattlingGearParams* p;

    clearBattlingSplashAndDust();
    clearBattlingEmitterParticles(battlingHitSparkEmitter);
    clearBattlingSpriteEffects();
    hideBattlingHelpLabels();
    initBattlingSoundChannels();
    if (battlingRoundWinner != 0) {
        p = battlingGear2.m8FC_params;
    } else {
        p = battlingGear1.m8FC_params;
    }
    if (p->m2C_winAnimLoops != 0) {
        battlingGearAnimInfo[16].m0_type = 1;
        battlingGearAnimInfo[16].m1_next = 0x10;
    } else {
        battlingGearAnimInfo[16].m0_type = 2;
        battlingGearAnimInfo[16].m1_next = -1;
    }
    battlingGear1.m0_position.vy = 0x100;
    battlingGear2.m0_position.vy = 0x100;
    battlingGear1.m6C_prevPosition.vy = 0x100;
    battlingGear2.m6C_prevPosition.vy = 0x100;
}

void updateBattlingWinnerCamera(BattlingGear* gear) {
    VECTOR v;
    u16 pad;

    if (battlingDebugEnabled != 0) {
        pad = newPadButtonForScripts;
        if (pad & 0x1000) {
            battlingWinCamTargetHeight += 4;
        }
        if (pad & 0x4000) {
            battlingWinCamTargetHeight -= 4;
        }
        if (pad & 0x10) {
            battlingWinCamHeight += 4;
        }
        if (pad & 0x40) {
            battlingWinCamHeight -= 4;
        }
        if (pad & 0x2000) {
            battlingWinCamAngle -= 0x20;
        }
        if (pad & 0x8000) {
            battlingWinCamAngle += 0x20;
        }
        if (pad & 0x8) {
            battlingWinCamDistance -= 0x10;
        }
        if (pad & 0x2) {
            battlingWinCamDistance += 0x10;
        }
        sprintf_screen("ANG %x\n", battlingWinCamAngle & 0xFFF);
        sprintf_screen("REF %x\n", battlingWinCamTargetHeight);
        sprintf_screen("CAM %x\n", battlingWinCamHeight);
        sprintf_screen("LEN %x\n", battlingWinCamDistance);
    }
    v = gear->m94C_target;
    v.vy = gear->m0_position.vy - battlingWinCamTargetHeight;
    moveBattlingCameraTargetTo(&v, 8);
    v.vy = gear->m0_position.vy - battlingWinCamHeight;
    v.vx = gear->m0_position.vx + ((getAngleCos(gear->m54_rotY + battlingWinCamAngle) * battlingWinCamDistance) >> 12);
    v.vz = gear->m0_position.vz + ((getAngleSin(gear->m54_rotY + battlingWinCamAngle) * battlingWinCamDistance) >> 12);
    moveBattlingCameraTo(&v, 0x10);
    while (getBattlingDistance(&battlingCameraPos, &gear->m0_position) < 0x200) {
        battlingCameraPos.vy -= 2;
        battlingCameraPos.vx -= 2;
    }
}

void startBattlingRoundResult(void) {
    battlingResultTimer = 0x96;
    battlingSpeedScale = 0x100;
    SetGeomScreen(0x200);
    battlingGear1.m0_position = battlingResultGearPoints[0];
    battlingGear2.m0_position = battlingResultGearPoints[1];
    battlingGear1.m93C_footPoint = battlingResultGearPoints[2];
    battlingGear2.m93C_footPoint = battlingResultGearPoints[3];
    battlingGear1.m95C_prevFootPoint = battlingGear1.m93C_footPoint;
    battlingGear2.m95C_prevFootPoint = battlingGear2.m93C_footPoint;
    battlingGear1.m6C_prevPosition = battlingGear1.m0_position;
    battlingGear2.m6C_prevPosition = battlingGear2.m0_position;
    if (battlingRoundWinner != 0) {
        startBattlingWinnerCamera(&battlingGear2);
    } else {
        startBattlingWinnerCamera(&battlingGear1);
    }
}

void updateBattlingRoundResult(BattlingGear* a, BattlingGear* b) {
    char buf[0x40];
    s32 y;

    if (battlingResultTimer == 0x95) {
        prepareBattlingRoundResult();
    }
    a->mD0_status.bits.b6 = 0;
    b->mD0_status.bits.b6 = 0;
    beginBattlingGearFrame(a);
    beginBattlingGearFrame(b);
    battlingHitSparkCount = 0;
    mergeQueuedInputs();
    if (battlingActivePad == 1) {
        y = newPadButtonForDialogs2 & 0x20;
    } else {
        y = newPadButtonForDialogs & 0x20;
    }
    if (y) {
        setBattlingSceneMode(2);
    }
    if (--battlingResultTimer == -1) {
        if (battlingDebugEnabled == 0 || battlingGameMode == 3) {
            setBattlingSceneMode(2);
        } else {
            a->mD0_status.flags |= 0x2000000;
            b->mD0_status.flags |= 0x2000000;
            battlingResultTimer = 0x95;
        }
    }
    setBattlingTextPos(0xA0, 0xA0);
    switch (battlingRoundWinner) {
    case 0:
        updateBattlingWinnerCamera(a);
        switch (battlingGameMode) {
        case 1:
            printBattlingTextCentered("YOU WERE VICTORIOUS");
            break;
        case 2:
            printBattlingTextCentered("1PLAYER VICTORY");
            break;
        case 3:
            printBattlingTextCentered("COM1 VICTORY");
            break;
        }
        break;
    case 1:
        updateBattlingWinnerCamera(b);
        switch (battlingGameMode) {
        case 1:
            printBattlingTextCentered("YOU WERE DEFEATED");
            sprintf(buf, "     BY %s", battlingGearEntries[b->m909_gearId].m8_name);
            printBattlingTextCentered(buf);
            break;
        case 2:
            printBattlingTextCentered("2PLAYER VICTORY");
            break;
        case 3:
            printBattlingTextCentered("COM2 VICTORY");
            break;
        }
        break;
    case 2:
        printBattlingTextCentered("DRAW GAME");
        break;
    }
    y = getBattlingGroundHeight(&battlingCameraPos, 0) - battlingWinCamHeight;
    if (y < battlingCameraPos.vy) {
        battlingCameraPos.vy = y;
    }
    a->m48_speed = 0;
    b->m48_speed = 0;
    clearBattlingGearInput(a);
    clearBattlingGearInput(b);
    beginBattlingGearFrame(a);
    beginBattlingGearFrame(b);
    battlingHitSparkCount = 0;
    computeBattlingGearVelocity(a);
    computeBattlingGearVelocity(b);
    moveBattlingGearOnGround(b);
    moveBattlingGearOnGround(a);
    separateBattlingGears(a, b);
    syncBattlingGearModelTransform(b);
    syncBattlingGearModelTransform(a);
    if (a->mD0_status.flags & 0x800000) {
        setBattlingGearWinnerPose(b);
        setBattlingGearLoserPose(a);
    } else {
        setBattlingGearWinnerPose(a);
        setBattlingGearLoserPose(b);
    }
    updateBattlingGearAnimation(a);
    updateBattlingGearAnimation(b);
    updateBattlingGearBodyPoints(a);
    updateBattlingGearBodyPoints(b);
    runBattlingGearAnimEvents(a, a->m998_stepStartFrame, a->m99A_stepFrames);
    runBattlingGearAnimEvents(b, b->m998_stepStartFrame, b->m99A_stepFrames);
    updateBattlingSpriteEffects();
}

void animateBattlingVictoryGear(BattlingGear* gear, s32 arg1) {
    gear->m15CC_replayCur = gear->m9CC_replay;
    gear->m4F_animSpeed = 0x10;
    gear->m4C_animation = arg1;
    gear->m52_moveAnimSpeed = 0;
    battlingSpeedScale = 0x100;
    updateBattlingGearAnimation(); /* no argument: a0 still holds gear */
    runBattlingGearAnimEvents(gear, gear->m998_stepStartFrame, gear->m99A_stepFrames);
}
