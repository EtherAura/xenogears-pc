/* Battling overlay, 0x8007B270-0x8007E528: sprite effects and 3D effects and
 * lines; its .rodata (spawnBattlingHitEffect's jump table) starts after the linker
 * fill that ends the previous object's. */
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

void loadBattlingGearColorClut(CVECTOR* c0, CVECTOR* c1) {
    s32 r, g, b;

    r = c0->r - 0x80;
    g = c0->g - 0x80;
    b = c0->b - 0x80;
    if (r < 0) {
        r = 0;
    }
    if (g < 0) {
        g = 0;
    }
    if (b < 0) {
        b = 0;
    }
    battlingGearColorClutEntries[0] = (((r >> 2) & 0x1F) + ((g << 3) & 0x3E0) + ((b << 8) & 0x7C00)) | 0x8000;
    r = c1->r - 0x80;
    g = c1->g - 0x80;
    b = c1->b - 0x80;
    if (r < 0) {
        r = 0;
    }
    if (g < 0) {
        g = 0;
    }
    if (b < 0) {
        b = 0;
    }
    battlingGearColorClutEntries[1] = (((r >> 2) & 0x1F) + ((g << 3) & 0x3E0) + ((b << 8) & 0x7C00)) | 0x8000;
    battlingGearColorClutEntries[2] = 0;
    battlingGearColorClutEntries[3] = 0x1111;
    battlingGearColorClut = LoadClut2((u32*)battlingGearColorClutEntries, battlingGearColorClutX, battlingGearColorClutY);
}

void initBattlingSpriteEffects(u32** tims) {
    TIM_IMAGE tim;
    RECT unused; /* retail frame has 8 more bytes */
    s32 i;
    s16* clut;
    BattlingEffect* p;
    u8 adjust = battlingEffect1FrameV[0] == 0;

    for (i = 0; i < 12; i++) {
        OpenTIM(tims[i + 1]);
        ReadTIM(&tim);
        battlingEffect0FrameU[i] = (u8)tim.prect->x << 2;
        battlingEffect0FrameV[i] = tim.prect->y;
        battlingEffect0FrameCluts[i] = GetClut(tim.crect->x, tim.crect->y);
        LoadImage(tim.crect, tim.caddr);
        LoadImage(tim.prect, tim.paddr);
    }
    battlingEffectTemplates[0].m0_u = battlingEffect0FrameU;
    battlingEffectTemplates[0].m4_v = battlingEffect0FrameV;
    battlingEffectTemplates[0].m8_w = 0x26;
    battlingEffectTemplates[0].m9_h = 0x26;
    battlingEffectTemplates[0].mC_clut = 0;
    battlingEffectTemplates[0].mE_tpage = GetTPage(0, 1, 0x3C0, 0x100);
    battlingEffectTemplates[0].mA_frameCount = 0xC;
    battlingEffectTemplates[0].m10_accel = 0;
    battlingEffectTemplates[0].mB_size = 0;

    OpenTIM(tims[13]);
    ReadTIM(&tim);
    clut = (s16*)tim.caddr;
    clut[0] = 0;
    for (i = 1; i < 16; i++) {
        clut[i] |= 0x8000;
    }
    if (adjust) {
        for (i = 0; i < 16; i++) {
            battlingEffect1FrameV[i] += tim.prect->y;
        }
    }
    LoadImage(tim.crect, tim.caddr);
    LoadImage(tim.prect, tim.paddr);
    battlingEffectTemplates[1].m0_u = battlingEffect1FrameU;
    battlingEffectTemplates[1].m4_v = battlingEffect1FrameV;
    battlingEffectTemplates[1].m8_w = 0x1F;
    battlingEffectTemplates[1].m9_h = 0x1F;
    battlingEffectTemplates[1].mC_clut = GetClut(tim.crect->x, tim.crect->y);
    battlingEffectTemplates[1].mE_tpage = GetTPage(0, 1, tim.prect->x, tim.prect->y);
    battlingEffectTemplates[1].mA_frameCount = 0x10;
    battlingEffectTemplates[1].m10_accel = -2;
    battlingEffectTemplates[1].mB_size = 0;

    OpenTIM(tims[16]);
    ReadTIM(&tim);
    clut = (s16*)tim.caddr;
    clut[0] = 0;
    for (i = 1; i < 16; i++) {
        clut[i] |= 0x8000;
    }
    if (adjust) {
        for (i = 0; i < 12; i++) {
            battlingEffect2FrameV[i] += tim.prect->y;
        }
    }
    LoadImage(tim.crect, tim.caddr);
    LoadImage(tim.prect, tim.paddr);
    battlingEffectTemplates[2].m0_u = battlingEffect2FrameU;
    battlingEffectTemplates[2].m4_v = battlingEffect2FrameV;
    battlingEffectTemplates[2].m8_w = 0xF;
    battlingEffectTemplates[2].m9_h = 0xF;
    battlingEffectTemplates[2].mC_clut = GetClut(tim.crect->x, tim.crect->y);
    battlingEffectTemplates[2].mE_tpage = GetTPage(0, 1, tim.prect->x, tim.prect->y);
    battlingEffectTemplates[2].mA_frameCount = 0xC;
    battlingEffectTemplates[2].m10_accel = 0;
    battlingEffectTemplates[2].mB_size = 1;
    battlingEffectTemplates[3] = battlingEffectTemplates[1];
    battlingEffectTemplates[3].mE_tpage = (battlingEffectTemplates[3].mE_tpage & ~0x60) | 0x40;

    OpenTIM(tims[26]);
    ReadTIM(&tim);
    if (adjust) {
        for (i = 0; i < 16; i++) {
            battlingEffect4FrameV[i] += tim.prect->y;
        }
    }
    LoadImage(tim.crect, tim.caddr);
    LoadImage(tim.prect, tim.paddr);
    battlingEffectTemplates[4].m0_u = battlingEffect2FrameU;
    battlingEffectTemplates[4].m4_v = battlingEffect4FrameV;
    battlingEffectTemplates[4].m8_w = 0xF;
    battlingEffectTemplates[4].m9_h = 0xF;
    battlingEffectTemplates[4].mC_clut = GetClut(tim.crect->x, tim.crect->y);
    battlingEffectTemplates[4].mE_tpage = GetTPage(0, 1, tim.prect->x, tim.prect->y);
    battlingEffectTemplates[4].mA_frameCount = 0x10;
    battlingEffectTemplates[4].m10_accel = 0;
    battlingEffectTemplates[4].mB_size = 2;

    for (i = 0, p = battlingSpriteEffects; i < 0x3C; i++, p++) {
        setlen(&p->m0_polys[0], 9);
        *(u32*)&p->m0_polys[0].r0 = 0x2C808080;
        p->m0_polys[0].code |= 2;
        p->m0_polys[1] = p->m0_polys[0];
        p->m50_active = 0;
        p->m51_frame = 0;
    }

    OpenTIM(tims[14]);
    ReadTIM(&tim);
    LoadImage(tim.crect, tim.caddr);
    LoadImage(tim.prect, tim.paddr);
    battlingShadowTimX = tim.prect->x;
    battlingShadowTimY = tim.prect->y;
    battlingShadowTimClut = GetClut(tim.crect->x, tim.crect->y);
    battlingShadowTimTPage = GetTPage(0, 2, tim.prect->x, tim.prect->y);

    OpenTIM(tims[17]);
    ReadTIM(&tim);
    clut = (s16*)tim.caddr;
    clut[0] = 0;
    for (i = 1; i < 16; i++) {
        clut[i] |= 0x8000;
    }
    LoadImage(tim.crect, tim.caddr);
    LoadImage(tim.prect, tim.paddr);
    battlingTrailU = (tim.prect->x << 2) & 0xFF;
    battlingTrailV = (u8)tim.prect->y;
    battlingTrailClut = GetClut(tim.crect->x, tim.crect->y);
    battlingTrailTPage = GetTPage(0, 1, tim.prect->x, tim.prect->y);
    battlingEffectFrame = 0x8000;
    battlingGearColorClutX = tim.crect->x;
    battlingGearColorClutY = tim.crect->y + 1;
    initBattlingDust();
    initBattlingSplash();
    initBattlingLines();
}

void updateBattlingSpriteEffects(void) {
    s32 i;
    BattlingEffect* p = battlingSpriteEffects;

    for (i = 0; i < 0x3C; i++, p++) {
        if (p->m50_active != 0) {
            if (p->m51_frame == p->m52_frameCount) {
                p->m50_active = 0;
            } else {
                p->m51_frame++;
                if (p->m53_type == 0) {
                    p->m60_view.anim.m6_speed += p->m60_view.anim.m4_accel;
                    p->m56_y += p->m60_view.anim.m6_speed;
                }
            }
        }
    }
    battlingEffectFrame++;
    battlingDrawnLineCount = battlingLineCount;
    battlingLineCount = 0;
}

void clearBattlingSpriteEffects(void) {
    s32 i;
    BattlingEffect* p = battlingSpriteEffects;

    for (i = 0x3B; i >= 0; i--) {
        p->m50_active = 0;
        p++;
    }
}

void drawBattlingSpriteEffects(MATRIX* m, MATRIX* rot, OTag* ot) {
    s32 i;
    BattlingEffect* p;
    BattlingEffectTemplate* t;
    POLY_FT4* poly;
    sptr c;
    OTag tag;
    OTag* slot;
    BattlingScratch* sp = BATTLING_SCRATCH;

    sp->m10_v[0].vz = 0;
    gte_SetRotMatrix(rot);
    sp->m10_v[0].vx = sp->m10_v[0].vy = -0x60;
    gte_ldv0(&sp->m10_v[0]);
    gte_rtv0();
    gte_stlvnl(&sp->m30_corners[0]);
    sp->m10_v[0].vx = sp->m10_v[0].vy = 0x60;
    gte_ldv0(&sp->m10_v[0]);
    gte_rtv0();
    gte_stlvnl(&sp->m30_corners[1]);
    sp->m10_v[0].vx = sp->m10_v[0].vy = -0x48;
    gte_ldv0(&sp->m10_v[0]);
    gte_rtv0();
    gte_stlvnl(&sp->m30_corners[2]);
    sp->m10_v[0].vx = sp->m10_v[0].vy = 0x48;
    gte_ldv0(&sp->m10_v[0]);
    gte_rtv0();
    gte_stlvnl(&sp->m30_corners[3]);
    sp->m10_v[0].vx = sp->m10_v[0].vy = -0x20;
    gte_ldv0(&sp->m10_v[0]);
    gte_rtv0();
    gte_stlvnl(&sp->m30_corners[4]);
    sp->m10_v[0].vx = sp->m10_v[0].vy = 0x20;
    gte_ldv0(&sp->m10_v[0]);
    gte_rtv0();
    gte_stlvnl(&sp->m30_corners[5]);
    gte_SetRotMatrix(m);
    gte_SetTransMatrix(m);
    for (p = battlingSpriteEffects, i = 0; i < 0x3C; i++, p++) {
        if (p->m50_active != 0 && p->m53_type == 0) {
            t = p->m60_view.anim.m0_template;
            sp->m10_v[0].vx = p->m54_x - sp->m0_camera.vx;
            sp->m10_v[0].vy = p->m56_y - sp->m0_camera.vy;
            sp->m10_v[0].vz = p->m58_z - sp->m0_camera.vz;
            sp->m10_v[2] = sp->m10_v[0];
            poly = &p->m0_polys[battlingBufferIndex];
            switch (t->mB_size) {
            case 0:
                sp->m10_v[0].vx += sp->m30_corners[0].vx;
                sp->m10_v[0].vy += sp->m30_corners[0].vy;
                sp->m10_v[0].vz += sp->m30_corners[0].vz;
                sp->m10_v[2].vx += sp->m30_corners[1].vx;
                sp->m10_v[2].vy += sp->m30_corners[1].vy;
                sp->m10_v[2].vz += sp->m30_corners[1].vz;
                break;
            case 1:
                sp->m10_v[0].vx += sp->m30_corners[2].vx;
                sp->m10_v[0].vy += sp->m30_corners[2].vy;
                sp->m10_v[0].vz += sp->m30_corners[2].vz;
                sp->m10_v[2].vx += sp->m30_corners[3].vx;
                sp->m10_v[2].vy += sp->m30_corners[3].vy;
                sp->m10_v[2].vz += sp->m30_corners[3].vz;
                break;
            case 2:
                sp->m10_v[0].vx += sp->m30_corners[4].vx;
                sp->m10_v[0].vy += sp->m30_corners[4].vy;
                sp->m10_v[0].vz += sp->m30_corners[4].vz;
                sp->m10_v[2].vx += sp->m30_corners[5].vx;
                sp->m10_v[2].vy += sp->m30_corners[5].vy;
                sp->m10_v[2].vz += sp->m30_corners[5].vz;
                break;
            }
            gte_ldv01(&sp->m10_v[0], &sp->m10_v[2]);
            gte_rtpt();
            gte_stsxy01(&poly->x0, &poly->x3);
            gte_stsz2(&sp->mB0_sz);
            poly->x1 = poly->x3;
            poly->y1 = poly->y0;
            poly->x2 = poly->x0;
            poly->y2 = poly->y3;
            poly->u0 = t->m0_u[p->m51_frame - 1];
            poly->v0 = t->m4_v[p->m51_frame - 1];
            poly->u1 = poly->u0 + t->m8_w;
            poly->u2 = poly->u0;
            poly->v1 = poly->v0;
            poly->v2 = poly->v0 + t->m9_h;
            poly->u3 = poly->u0 + t->m8_w;
            poly->v3 = poly->v0 + t->m9_h;
            if (t->mC_clut == 0) {
                poly->clut = battlingEffect0FrameCluts[p->m51_frame - 1];
            }
            if (sp->mB0_sz > 0x40) {
                sp->mB0_sz -= 0x40;
            }
            slot = (OTag*)((sp->mB0_sz >> 4) * sizeof(OTag) + (uptr)ot);
            tag = *slot;
            c = OT_LINK(poly);
            *slot = c;
            *(OTag*)c = tag | OT_LEN(9);
        }
    }
}

void setBattlingEffectColor(CVECTOR* arg0) {
    battlingEffectColor = *arg0;
}

void spawnBattlingSpriteEffect(SVECTOR* pos, s32 type) {
    s32 i;
    BattlingEffect* p = battlingSpriteEffects;
    BattlingEffectTemplate* t;
    POLY_FT4* poly;

    for (i = 0; i < 0x3C; i++, p++) {
        if (p->m50_active == 0) {
            break;
        }
    }
    if (i == 0x3C) {
        return;
    }
    p->m50_active = 1;
    t = &battlingEffectTemplates[type];
    p->m53_type = 0;
    p->m51_frame = 0;
    p->m60_view.anim.m0_template = t;
    p->m52_frameCount = t->mA_frameCount;
    p->m60_view.anim.m4_accel = t->m10_accel;
    p->m60_view.anim.m6_speed = 0;
    p->m54_x = pos->vx;
    p->m56_y = pos->vy;
    p->m58_z = pos->vz;
    poly = &p->m0_polys[0];
    poly->tpage = t->mE_tpage;
    if (type == 2) {
        poly->code &= ~1;
        poly->r0 = battlingEffectColor.r;
        poly->g0 = battlingEffectColor.g;
        poly->b0 = battlingEffectColor.b;
    } else {
        poly->code |= 1;
    }
    poly->clut = t->mC_clut;
    p->m0_polys[1] = *poly;
}

void drawBattlingTrailEffects(MATRIX* m, MATRIX* rot, OTag* ot) {
    s32 i;
    BattlingEffect* p;
    BattlingEffectTrail* pt;
    POLY_FT4* poly;
    s32 dx;
    s32 dy;
    s32 width;
    s32 nx;
    s32 ny;
    sptr c;
    OTag tag;
    OTag* slot;
    BattlingScratch* sp = BATTLING_SCRATCH;

    sp->m10_v[1].vx = 0;
    sp->m10_v[1].vz = 0;
    gte_SetRotMatrix(m);
    gte_SetTransMatrix(m);
    for (p = battlingSpriteEffects, i = 0; i < 0x3C; i++, p++) {
        if (p->m50_active != 0 && p->m53_type == 1) {
            sp->m10_v[0].vx = p->m54_x - sp->m0_camera.vx;
            sp->m10_v[0].vy = p->m56_y - sp->m0_camera.vy;
            sp->m10_v[0].vz = p->m58_z - sp->m0_camera.vz;
            sp->m10_v[2] = sp->m10_v[0];
            poly = &p->m0_polys[battlingBufferIndex];
            gte_ldv0(&sp->m10_v[0]);
            gte_rtps();
            gte_stsxy(&poly->x0);
            p->m60_view.trail.m8_sx = poly->x0;
            p->m60_view.trail.mA_sy = poly->y0;
            if (p->m60_view.trail.mC_prev != NULL) {
                pt = &p->m60_view.trail.mC_prev->m60_view.trail;
                dx = p->m60_view.trail.m8_sx - pt->m8_sx;
                dy = p->m60_view.trail.mA_sy - pt->mA_sy;
                c = SquareRoot0(dx * dx + dy * dy); /* distance */
                width = p->m60_view.trail.m14_width;
                nx = R3000_DIV(dx * width, (s32)c);
                ny = R3000_DIV(dy * width, (s32)c);
                sp->m10_v[0].vz = 0;
                sp->m10_v[0].vy = -nx;
                sp->m10_v[0].vx = ny;
                gte_SetRotMatrix(rot);
                gte_ldv0(&sp->m10_v[0]);
                gte_rtv0();
                gte_stlvnl(&sp->m30_corners[2]);
                sp->m10_v[0].vy = nx;
                sp->m10_v[0].vx = -ny;
                gte_ldv0(&sp->m10_v[0]);
                gte_rtv0();
                gte_stlvnl(&sp->m30_corners[3]);
                sp->m10_v[0].vx = p->m54_x - sp->m0_camera.vx;
                sp->m10_v[0].vy = p->m56_y - sp->m0_camera.vy;
                sp->m10_v[0].vz = p->m58_z - sp->m0_camera.vz;
                sp->m10_v[2] = sp->m10_v[0];
            } else {
                gte_SetRotMatrix(rot);
                sp->m10_v[1].vy = -p->m60_view.trail.m14_width;
                gte_ldv0(&sp->m10_v[1]);
                gte_rtv0();
                gte_stlvnl(&sp->m30_corners[2]);
                sp->m10_v[1].vy = p->m60_view.trail.m14_width;
                gte_ldv0(&sp->m10_v[1]);
                gte_rtv0();
                gte_stlvnl(&sp->m30_corners[3]);
            }
            sp->m10_v[0].vx += sp->m30_corners[2].vx;
            sp->m10_v[0].vy += sp->m30_corners[2].vy;
            sp->m10_v[0].vz += sp->m30_corners[2].vz;
            sp->m10_v[2].vx += sp->m30_corners[3].vx;
            sp->m10_v[2].vy += sp->m30_corners[3].vy;
            sp->m10_v[2].vz += sp->m30_corners[3].vz;
            gte_SetRotMatrix(m);
            gte_SetTransMatrix(m);
            gte_ldv01(&sp->m10_v[0], &sp->m10_v[2]);
            gte_rtpt();
            gte_stsxy01(&poly->x0, &poly->x1);
            gte_stsz2(&sp->mB0_sz);
            p->m60_view.trail.m0_x0 = poly->x0;
            p->m60_view.trail.m2_y0 = poly->y0;
            p->m60_view.trail.m4_x1 = poly->x1;
            p->m60_view.trail.m6_y1 = poly->y1;
            if (p->m60_view.trail.mC_prev != NULL) {
                if (((pt->mA_sy - p->m60_view.trail.mA_sy) * p->m60_view.trail.m0_x0 +
                     (p->m60_view.trail.m8_sx - pt->m8_sx) * p->m60_view.trail.m2_y0 +
                     pt->m8_sx * p->m60_view.trail.mA_sy - p->m60_view.trail.m8_sx * pt->mA_sy) *
                        ((pt->mA_sy - p->m60_view.trail.mA_sy) * pt->m0_x0 +
                         (p->m60_view.trail.m8_sx - pt->m8_sx) * pt->m2_y0 +
                         pt->m8_sx * p->m60_view.trail.mA_sy - p->m60_view.trail.m8_sx * pt->mA_sy) >
                    0) {
                    poly->x2 = pt->m0_x0;
                    poly->y2 = pt->m2_y0;
                    poly->x3 = pt->m4_x1;
                    poly->y3 = pt->m6_y1;
                } else {
                    poly->x3 = pt->m0_x0;
                    poly->y3 = pt->m2_y0;
                    poly->x2 = pt->m4_x1;
                    poly->y2 = pt->m6_y1;
                }
                c = 0x40 - (p->m51_frame << 3);
                poly->r0 = c;
                poly->g0 = c;
                poly->b0 = c;
                slot = (OTag*)((sp->mB0_sz >> 4) * sizeof(OTag) + (uptr)ot);
                tag = *slot;
                c = OT_LINK(poly);
                *slot = c;
                *(OTag*)c = tag | OT_LEN(9);
            }
        }
    }
}

void spawnBattlingTrailEffect(s32 arg0, VECTOR* pos, s32 owner, s32 arg3) {
    s32 i;
    BattlingEffect* p;
    BattlingEffect* q;
    POLY_FT4* poly;

    for (i = 0, p = battlingSpriteEffects; i < 0x3C; i++, p++) {
        if (p->m50_active != 0 && p->m60_view.trail.m18_owner == owner && p->m60_view.trail.m12_id == battlingFrameCounter) {
            return;
        }
    }
    p = battlingSpriteEffects;
    for (i = 0; i < 0x3C; i++, p++) {
        if (p->m50_active == 0) {
            break;
        }
    }
    if (i == 0x3C) {
        return;
    }
    poly = &p->m0_polys[0];
    poly->u0 = poly->u1 = poly->u2 = poly->u3 = (u8)battlingGearColorClutX * 4 + 8 + arg0 * 4;
    poly->v0 = poly->v1 = poly->v2 = poly->v3 = battlingGearColorClutY;
    poly->tpage = battlingTrailTPage;
    poly->clut = battlingGearColorClut;
    poly->code &= ~1;
    p->m0_polys[1] = *poly;
    p->m52_frameCount = 7;
    p->m53_type = 1;
    p->m50_active = 1;
    p->m51_frame = 0;
    p->m54_x = pos->vx;
    p->m56_y = pos->vy;
    p->m58_z = pos->vz;
    p->m60_view.trail.m12_id = battlingFrameCounter;
    p->m60_view.trail.m18_owner = owner;
    p->m60_view.trail.mC_prev = NULL;
    p->m60_view.trail.m10_frame = battlingEffectFrame;
    p->m60_view.trail.m14_width = battlingTrailWidths[arg3];
    for (q = battlingSpriteEffects, i = 0; i < 0x3C; i++, q++) {
        if (q->m50_active != 0 && q->m60_view.trail.m18_owner == owner && q != p && q->m53_type == 1 &&
            q->m60_view.trail.m10_frame == (u16)(battlingEffectFrame - 1)) {
            p->m60_view.trail.mC_prev = q;
        }
    }
}

void drawBattlingBeamEffects(MATRIX* m, s32 arg1, OTag* ot) {
    s32 i;
    BattlingEffect* p;
    BattlingEffect* prev;
    POLY_FT4* poly;
    sptr c;
    OTag tag;
    OTag* slot;
    BattlingScratch* sp = BATTLING_SCRATCH;

    gte_SetRotMatrix(m);
    gte_SetTransMatrix(m);
    for (p = battlingSpriteEffects, i = 0; i < 0x3C; i++, p++) {
        if (p->m50_active != 0 && p->m53_type == 2 && p->m60_view.beam.m0_prev != NULL) {
            poly = &p->m0_polys[battlingBufferIndex];
            sp->m10_v[0].vx = p->m54_x - sp->m0_camera.vx;
            sp->m10_v[0].vy = p->m56_y - sp->m0_camera.vy;
            sp->m10_v[0].vz = p->m58_z - sp->m0_camera.vz;
            sp->m10_v[1].vx = p->m60_view.beam.m4_pos[0] - sp->m0_camera.vx;
            sp->m10_v[1].vy = p->m60_view.beam.m4_pos[1] - sp->m0_camera.vy;
            sp->m10_v[1].vz = p->m60_view.beam.m4_pos[2] - sp->m0_camera.vz;
            sp->m10_v[2].vx = p->m60_view.beam.m0_prev->m54_x - sp->m0_camera.vx;
            sp->m10_v[2].vy = p->m60_view.beam.m0_prev->m56_y - sp->m0_camera.vy;
            sp->m10_v[2].vz = p->m60_view.beam.m0_prev->m58_z - sp->m0_camera.vz;
            sp->m10_v[3].vx = p->m60_view.beam.m0_prev->m60_view.beam.m4_pos[0] - sp->m0_camera.vx;
            sp->m10_v[3].vy = p->m60_view.beam.m0_prev->m60_view.beam.m4_pos[1] - sp->m0_camera.vy;
            sp->m10_v[3].vz = p->m60_view.beam.m0_prev->m60_view.beam.m4_pos[2] - sp->m0_camera.vz;
            gte_ldv3(&sp->m10_v[0], &sp->m10_v[1], &sp->m10_v[2]);
            gte_rtpt();
            gte_stsxy3(&poly->x0, &poly->x1, &poly->x2);
            gte_stsz(&sp->mB0_sz);
            gte_ldv0(&sp->m10_v[3]);
            gte_rtps();
            gte_stsxy(&poly->x3);
            c = 0x40 - (p->m51_frame << 3);
            poly->r0 = c;
            poly->g0 = c;
            poly->b0 = c;
            slot = (OTag*)((sp->mB0_sz >> 4) * sizeof(OTag) + (uptr)ot);
            tag = *slot;
            c = OT_LINK(poly);
            *slot = c;
            *(OTag*)c = tag | OT_LEN(9);
        }
    }
}

u32 makeBattlingEffectHandle(s32 arg0, s32 arg1, s32 arg2, s32 arg3) {
    return (arg3 & 0x7F) | ((arg0 << 7) & 0x80) | (arg1 << 24) | ((arg2 << 8) & 0xFFFF00);
}

void spawnBattlingBeamEffect(s32 arg0, VECTOR* pos, VECTOR* pos2, s32 owner) {
    s32 i;
    BattlingEffect* p;
    BattlingEffect* q;
    POLY_FT4* poly;

    for (i = 0, p = battlingSpriteEffects; i < 0x3C; i++, p++) {
        if (p->m50_active != 0 && p->m60_view.beam.m10_owner == owner && p->m60_view.beam.mC_id == battlingFrameCounter) {
            return;
        }
    }
    p = battlingSpriteEffects;
    for (i = 0; i < 0x3C; i++, p++) {
        if (p->m50_active == 0) {
            break;
        }
    }
    if (i == 0x3C) {
        return;
    }
    p->m52_frameCount = 7;
    p->m53_type = 2;
    p->m50_active = 1;
    p->m51_frame = 0;
    p->m54_x = pos->vx;
    p->m56_y = pos->vy;
    p->m58_z = pos->vz;
    p->m60_view.beam.mC_id = battlingFrameCounter;
    p->m60_view.beam.m4_pos[0] = pos2->vx;
    p->m60_view.beam.m4_pos[1] = pos2->vy;
    p->m60_view.beam.m4_pos[2] = pos2->vz;
    p->m60_view.beam.m10_owner = owner;
    p->m60_view.beam.m0_prev = NULL;
    p->m60_view.beam.mA_frame = battlingEffectFrame;
    poly = &p->m0_polys[0];
    poly->u0 = poly->u1 = poly->u2 = poly->u3 = (u8)battlingGearColorClutX * 4 + 8 + arg0 * 4;
    poly->v0 = poly->v1 = poly->v2 = poly->v3 = battlingGearColorClutY;
    poly->tpage = battlingTrailTPage;
    poly->clut = battlingGearColorClut;
    poly->code &= ~1;
    p->m0_polys[1] = *poly;
    for (q = battlingSpriteEffects, i = 0; i < 0x3C; i++, q++) {
        if (q->m50_active != 0 && q->m60_view.beam.m10_owner == owner && q != p && q->m53_type == 2 &&
            q->m60_view.beam.mA_frame == (u16)(battlingEffectFrame - 1)) {
            p->m60_view.beam.m0_prev = q;
        }
    }
}

void drawBattlingEffects(MATRIX* m, OTag* ot) {
    MATRIX t;

    *(VECTOR*)PSX_SCRATCH(0x1F800000) = battlingViewOrigin;
    TransposeMatrix(m, &t);
    drawBattlingSpriteEffects(m, &t, ot);
    drawBattlingTrailEffects(m, &t, ot);
    drawBattlingBeamEffects(m, 0, ot);
    gte_SetRotMatrix(m);
    gte_SetTransMatrix(m);
    drawBattlingDust(ot);
    drawBattlingSplash(ot);
    drawBattlingLines(ot);
}

void drawBattlingLineEffects(OTag* ot) {
    *(VECTOR*)PSX_SCRATCH(0x1F800000) = battlingViewOrigin;
    drawBattlingLines(ot);
}

void jitterBattlingSVector(SVECTOR* arg0) {
    arg0->vx += rand() % 48 - 0x18;
    arg0->vy += rand() % 48 - 0x18;
    arg0->vz += rand() % 48 - 0x18;
}

s32 spawnBattlingHitEffect(VECTOR* arg0, s32 arg1) {
    SVECTOR sp10;

    sp10.vx = arg0->vx;
    sp10.vy = arg0->vy;
    sp10.vz = arg0->vz;
    switch (arg1) {
    case 8:
        jitterBattlingSVector(&sp10);
    case 0:
        spawnBattlingSpriteEffect(&sp10, 0);
        break;
    case 9:
        jitterBattlingSVector(&sp10);
    case 1:
        spawnBattlingSpriteEffect(&sp10, 1);
        break;
    case 10:
        jitterBattlingSVector(&sp10);
    case 2:
        spawnBattlingSpriteEffect(&sp10, 2);
        break;
    case 11:
        jitterBattlingSVector(&sp10);
    case 3:
        spawnBattlingSpriteEffect(&sp10, 3);
        break;
    case 12:
        jitterBattlingSVector(&sp10);
    case 4:
        spawnBattlingSpriteEffect(&sp10, 4);
        break;
    }
    FALLOFF_RETURN(0); /* result unused */
}

s32 isBattlingTwoPointEffect(s32 arg0) {
    if (arg0 < 0x10) {
        return 0;
    }
    return arg0 < 0x20;
}

void jitterBattlingVector(VECTOR* arg0, VECTOR* arg1) {
    arg1->vx = arg0->vx + (rand() % 64) - 0x20;
    arg1->vy = arg0->vy + (rand() % 64) - 0x20;
    arg1->vz = arg0->vz + (rand() % 64) - 0x20;
}

void spawnBattlingLightning(VECTOR* from, VECTOR* to, s32 type) {
    VECTOR pos;
    VECTOR step;
    VECTOR prev;
    VECTOR next;
    CVECTOR col;
    s32 i;
    s32 j;
    s32 v;

    step = *to;
    step.vx -= from->vx;
    step.vy -= from->vy;
    step.vz -= from->vz;
    getBattlingVectorLength(&step);
    step.vx /= 7;
    step.vy /= 7;
    step.vz /= 7;
    for (j = 0; j < 3; j++) {
        pos = *from;
        prev = *from;
        for (i = 0; i < 7; i++) {
            pos.vx += step.vx;
            pos.vy += step.vy;
            pos.vz += step.vz;
            jitterBattlingVector(&pos, &next);
            switch (type) {
            case 0:
                col.r = rand() & 0x3F;
                col.g = rand() % 191 + 0x40;
                col.b = 0xFF;
                break;
            case 1:
                v = rand() % 256 + 0x40;
                if (v < 256) {
                    col.g = v;
                } else {
                    col.g = 0xFF;
                }
                col.r = col.g = col.g;
                col.b = v / 3;
                break;
            case 2:
                if (i & 1) {
                    col.r = col.g = col.b = 0xFF;
                } else {
                    col.r = 0xFF;
                    col.b = 0;
                    col.g = 0;
                }
                break;
            }
            if (i != 6) {
                addBattlingLine(&prev, &next, &col);
            } else {
                addBattlingLine(&prev, to, &col);
            }
            prev = next;
        }
    }
}

void spawnBattlingLightningKind(VECTOR* arg0, VECTOR* arg1, s32 arg2) {
    switch (arg2) {
    case 0x11:
        arg2 = 0;
        break;
    case 0x12:
        arg2 = 1;
        break;
    case 0x13:
        arg2 = 2;
        break;
    default:
        return;
    }
    spawnBattlingLightning(arg0, arg1, arg2);
}

void initBattlingDust(void) {
    s32 i;
    BattlingDust* a;

    battlingDustParticles = allocateHeap(0x9F6, 0);
    battlingDustTiles[0] = allocateHeap(255 * (s32)sizeof(TILE_1), 0);
    battlingDustTiles[1] = allocateHeap(255 * (s32)sizeof(TILE_1), 0);
    a = battlingDustParticles;
    for (i = 0; i < 0xFF; a++, i++) {
        setlen(&battlingDustTiles[0][i], 2);
        *(u32*)&battlingDustTiles[0][i].r0 = 0x6880B0F0;
        setlen(&battlingDustTiles[1][i], 2);
        *(u32*)&battlingDustTiles[1][i].r0 = 0x6880B0F0;
        a->m0_x = a->m2_y = a->m4_z = 0;
        a->m6_life = 0;
        a->m0_x = a->m4_z = 0;
    }
}

void spawnBattlingDust(VECTOR* pos, s32 count) {
    s32 i;
    BattlingDust* a;
    s32 x;
    s32 z;

    a = battlingDustParticles;
    for (i = 0; i < 251; i++, a++) {
        if (count == 0) {
            break;
        }
        if (a->m6_life == 0) {
            a->m0_x = pos->vx + (rand() % 64 - 0x20);
            x = a->m0_x;
            a->m4_z = pos->vz + (rand() % 64 - 0x20);
            z = a->m4_z;
            a->m2_y = a->m8_groundY = battlingHeightMap[(x >> 8) + (z >> 8) * 128].m0_height;
            a->m7_vy = -(rand() % 10 + 10);
            a->m6_life = 20;
            count--;
        }
    }
}

void drawBattlingDust(OTag* ot) {
    s32 i;
    s32 n;
    BattlingDust* a;
    TILE_1* p;
    BattlingScratch* s;
    s32 cx;
    s32 cy;
    s32 cz;
    OTag old;
    OTag tag;

    s = (BattlingScratch*)PSX_SCRATCH(0x1F800000);
    a = &battlingDustParticles[battlingBufferIndex];
    p = battlingDustTiles[battlingBufferIndex];
    n = 0;
    cx = s->m0_camera.vx;
    cy = s->m0_camera.vy;
    cz = s->m0_camera.vz;
    for (i = battlingBufferIndex; i < 252; i += 2, a += 2) {
        if (a->m6_life != 0) {
            s->m10_v[0].vx = a->m0_x - cx;
            s->m10_v[0].vy = a->m2_y - cy;
            s->m10_v[0].vz = a->m4_z - cz;
            if (n == 0) {
                gte_ldv0(&s->m10_v[0]);
                n = 1;
            } else if (n == 1) {
                gte_ldv1(&s->m10_v[0]);
                n = 2;
            } else {
                gte_ldv2(&s->m10_v[0]);
                gte_rtpt();
                n = 0;
                gte_stsxy3(&p[0].x0, &p[1].x0, &p[2].x0);
                /* addPrim with len 2 through the KUSEG address; the shared
                 * tag variable (set three times) keeps GCC's scheduler
                 * from boosting the mask, as in retail */
                gte_stsz1(&s->mB0_sz);
                old = ot[s->mB0_sz >> 4];
                ot[s->mB0_sz >> 4] = tag = OT_LINK(&p[0]);
                old |= OT_LEN(2);
                *(OTag*)tag = old;
                gte_stsz2(&s->mB0_sz);
                old = ot[s->mB0_sz >> 4];
                ot[s->mB0_sz >> 4] = tag = OT_LINK(&p[1]);
                old |= OT_LEN(2);
                *(OTag*)tag = old;
                gte_stsz(&s->mB0_sz);
                old = ot[s->mB0_sz >> 4];
                ot[s->mB0_sz >> 4] = tag = OT_LINK(&p[2]);
                old |= OT_LEN(2);
                *(OTag*)tag = old;
                p += 3;
            }
            a->m2_y += a->m7_vy;
            if (a->m2_y >= a->m8_groundY) {
                a->m6_life = 0;
            }
            a->m7_vy += 2;
        }
    }
}

void initBattlingSplash(void) {
    s32 i;
    TILE* p;
    BattlingDebris* c;

    battlingSplashParticles = allocateHeap(0x1950, 0);
    p = battlingSplashTiles[0] = allocateHeap(540 * (s32)sizeof(TILE), 0);
    battlingSplashTiles[1] = allocateHeap(540 * (s32)sizeof(TILE), 0);
    c = battlingSplashParticles;
    for (i = 0; i < 0x21C; i++, c++, p++) {
        setlen(p, 3);
        *(u32*)&p->r0 = 0x60FFD0A0;
        p->w = rand() % 3 + 2;
        p->h = rand() % 3 + 2;
        c->m0_x = c->m2_y = c->m4_z = 0;
        c->m8_vx = c->mA_vy = c->m9_vz = c->m6_life = 0;
    }
    copyBattlingWords(battlingSplashTiles[1], battlingSplashTiles[0], 540 * (s32)sizeof(TILE));
}

void spawnBattlingSplash(VECTOR* from, VECTOR* to) {
    VECTOR d;
    s32 dirX;
    s32 dirZ;
    s32 count;
    s32 spread;
    s32 dy;
    s32 life;
    s32 i;
    s32 r;
    s32 k;
    BattlingDebris* c;

    if (from->vy <= 0x80 && to->vy <= 0x80) {
        return;
    }
    d.vz = to->vx - from->vx;
    d.vx = from->vz - to->vz;
    c = battlingSplashParticles;
    d.vy = from->vy - to->vy;
    count = getBattlingVectorLength(&d) / 6;
    if (count <= 0) {
        count = 1;
    }
    d.vy = 0x1000;
    spread = getBattlingVectorLengthXZ(&d);
    if (spread < 16) {
        spread = 0;
    }
    if (d.vz != 0 || d.vx != 0) {
        VectorNormal(&d, &d);
        dirX = d.vx;
        dirZ = d.vz;
    }
    dy = abs(from->vy - to->vy);
    life = dy / 3 + 1;
    if ((u32)life > 120) {
        life = 120;
    }
    for (i = 0; i < 0x218; i++, c++) {
        if (count == 0) {
            break;
        }
        if (c->m6_life == 0) {
            if (spread != 0) {
                r = rand() % 4096;
                k = spread / 7;
                if (rand() & 1) {
                    c->m8_vx = ((dirX * r) >> 14) + k;
                    c->m9_vz = ((dirZ * r) >> 14) + k;
                } else {
                    c->m8_vx = -((dirX * r) >> 14) - k;
                    c->m9_vz = -((dirZ * r) >> 14) - k;
                }
            } else {
                c->m8_vx = (rand() & 0xF) - 8;
                c->m9_vz = (rand() & 0xF) - 8;
            }
            if (dy > 8) {
                s32 t = rand() % (dy / 2);
                c->m6_life = life;
                c->mA_vy = -t + 1;
            } else {
                c->mA_vy = -4;
                c->m6_life = 20;
            }
            c->m0_x = from->vx + (rand() % 32 - 0x10) + c->m8_vx * 2;
            c->m4_z = from->vz + (rand() % 32 - 0x10) + c->m9_vz * 2;
            c->m2_y = -0x10;
            count--;
        }
    }
}


void drawBattlingSplash(OTag* ot) {
    s32 i;
    s32 n;
    BattlingDebris* c;
    TILE* p;
    BattlingScratch* s;
    s32 cx;
    s32 cy;
    s32 cz;
    OTag old;
    OTag tag;

    s = (BattlingScratch*)PSX_SCRATCH(0x1F800000);
    n = 0;
    cx = s->m0_camera.vx;
    cy = s->m0_camera.vy;
    cz = s->m0_camera.vz;
    c = battlingSplashParticles;
    p = battlingSplashTiles[battlingBufferIndex];
    for (i = 0; i < 0x218; i++, c++) {
        if (c->m6_life != 0) {
            c->m0_x += (s8)c->m8_vx;
            c->m4_z += (s8)c->m9_vz;
            s->m10_v[0].vx = c->m0_x - cx;
            s->m10_v[0].vy = c->m2_y - cy;
            s->m10_v[0].vz = c->m4_z - cz;
            if (n == 0) {
                gte_ldv0(&s->m10_v[0]);
                n = 1;
            } else if (n == 1) {
                gte_ldv1(&s->m10_v[0]);
                n = 2;
            } else {
                gte_ldv2(&s->m10_v[0]);
                gte_rtpt();
                n = 0;
                gte_stsxy3(&p[0].x0, &p[1].x0, &p[2].x0);
                gte_stsz1(&s->mB0_sz);
                old = ot[s->mB0_sz >> 4];
                ot[s->mB0_sz >> 4] = tag = OT_LINK(&p[0]);
                old |= OT_LEN(3);
                *(OTag*)tag = old;
                gte_stsz2(&s->mB0_sz);
                old = ot[s->mB0_sz >> 4];
                ot[s->mB0_sz >> 4] = tag = OT_LINK(&p[1]);
                old |= OT_LEN(3);
                *(OTag*)tag = old;
                gte_stsz(&s->mB0_sz);
                old = ot[s->mB0_sz >> 4];
                ot[s->mB0_sz >> 4] = tag = OT_LINK(&p[2]);
                old |= OT_LEN(3);
                *(OTag*)tag = old;
                p += 3;
            }
            c->m2_y += c->mA_vy;
            c->mA_vy += 2;
            if (c->m2_y > 16) {
                c->m2_y = 16;
            }
            c->m6_life--;
        }
    }
}

void clearBattlingSplashAndDust(void) {
    s32 i;
    BattlingDebris* p;

    p = battlingSplashParticles;
    for (i = 0; i < 0x21C; i++) {
        p[i].m0_x = p[i].m2_y = p[i].m4_z = p[i].m6_life = 0;
    }
    battlingGear2.m90B_dustTimer = 0;
    battlingGear1.m90B_dustTimer = 0;
    for (i = 0; i < 0xFF; i++) {
        battlingDustParticles[i].m6_life = 0;
        battlingDustParticles[i].m0_x = battlingDustParticles[i].m2_y = battlingDustParticles[i].m4_z = 0;
    }
}

void initBattlingLines(void) {
    s32 i;
    BattlingDebugLine* p;

    p = battlingLines;
    for (i = 0; i < 100; i++) {
        setlen(&p[i], 3);
        setcode(&p[i], 0x40);
    }
    battlingLineCount = 0;
    battlingDrawnLineCount = 0;
}

void addBattlingLine(VECTOR* from, VECTOR* to, CVECTOR* color) {
    BattlingDebugLine* l;

    if (battlingLineCount < 100) {
        l = &battlingLines[battlingLineCount];
        l->m10_p0.vx = from->vx;
        l->m10_p0.vy = from->vy;
        l->m10_p0.vz = from->vz;
        l->m18_p1.vx = to->vx;
        l->m18_p1.vy = to->vy;
        l->m18_p1.vz = to->vz;
        l->m0_line.r0 = color->r;
        l->m0_line.g0 = color->g;
        l->m0_line.b0 = color->b;
        battlingLineCount++;
    }
}

void drawBattlingLines(OTag* ot) {
    s32 i;
    s32 otz;
    BattlingDebugLine* l;
    BattlingScratch* s;
    s32 cx;
    s32 cy;
    s32 cz;
    u32* e;
    OTag old;

    s = (BattlingScratch*)PSX_SCRATCH(0x1F800000);
    l = battlingLines;
    cx = s->m0_camera.vx;
    cy = s->m0_camera.vy;
    cz = s->m0_camera.vz;
    for (i = 0; i < battlingDrawnLineCount; i++, l++) {
        s->m10_v[1] = l->m10_p0;
        s->m10_v[2] = l->m18_p1;
        s->m10_v[1].vx -= cx;
        s->m10_v[1].vy -= cy;
        s->m10_v[1].vz -= cz;
        s->m10_v[2].vx -= cx;
        s->m10_v[2].vy -= cy;
        s->m10_v[2].vz -= cz;
        gte_ldv0(&s->m10_v[1]);
        gte_ldv1(&s->m10_v[2]);
        gte_rtpt();
        gte_stsxy0(&l->m0_line.x0);
        gte_stsxy1(&l->m0_line.x1);
        gte_stsz2(&otz);
        old = ot[otz >> 4];
        ot[otz >> 4] = OT_LINK(l);
        *(OTag*)OT_LINK(l) = old | OT_LEN(3);
    }
}
