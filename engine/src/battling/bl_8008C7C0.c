/* Battling overlay, 0x8008C7C0-0x80090F38: particles, fire, sound and the CPU
 * opponent, after the hand-written object at 0x8008C3A8. */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "main/dialog.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "main/model.h"
#include "main/debug_font.h"
#include "main/sound.h"
#include "battling/battling.h"

void AddPrims(void* ot, void* p0, void* p1);
VECTOR* ApplyMatrix(MATRIX* m, SVECTOR* v0, VECTOR* v1);
VECTOR* ApplyRotMatrix(SVECTOR* v0, VECTOR* v1);
MATRIX* CompMatrix(MATRIX* m0, MATRIX* m1, MATRIX* m2);
void DrawOTagEnv(void* ot, DRAWENV* env);
s32 GetRCnt(s32 spec);
MATRIX* MatrixNormal(MATRIX* m, MATRIX* n);
MATRIX* MulMatrix2(MATRIX* m0, MATRIX* m1);
void SetDrawArea(void* p, RECT* r);
void SetDrawEnv(void* dr_env, DRAWENV* env);
void SetDrawMode(DR_MODE* p, s32 dfe, s32 dtd, s32 tpage, RECT* tw);
void SetDrawOffset(void* p, u16* ofs);
void StopSoundEffect(s32 ch);
void bootGame(s32);
void checkSoftReboot(void);
void* createMusicInstance(void*);
void initFieldDrawEnvsSub0(s32 w, s32 h);
void* initModelDynamicVertices(ModelBlock* block, s32 flags);
void loadSequence(void*);
s32 rand(void);
void setupOverrideClut(s16 x, s16 y);
void startMusicInstance(void*, s32, s32);
void stopAllSounds(void);
s32 submitModelForRendering(ModelBlock* b, u8* prims, OTag* ot, s32 renderMode);
void traceNextAlloc(s32 type);
void unloadSequence(void*);
s32 uploadTextureToVram(s32* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY);
void setGameMode(s32);
void relocateModelBlock(ModelBlock* b);
void setupOverrideTPageFull(u16 arg0);
s32 getLargestAbsComponent(s32 x, s32 y, s32 z);
void patchSelfModifyingCode(s32, s32, s32, s32);
void MulRotationMatrix(u16 index, BattlingLight* light); /* main: LightDef, the same layout */
void setupLightMatrix(MATRIX* m);
void addPolyG4(OTag* ot, void* prim);
void addLineF2(OTag* ot, void* prim);
void addLineF3(OTag* ot, void* prim);
void addLineF4(OTag* ot, void* prim);
void addTile(OTag* ot, void* prim);
void addTile1(OTag* ot, void* prim);
void dumpHeapBlocks(s32, s32, s32, s32);
void freeHeapDelayed(void* p, s32 delay);
void updateDelayedFrees(void);
void flushDelayedFrees(void);
void decompress(void*, void*);
void mergeQueuedInputs(void);
void setDebugFontColor(s32, s32, s32);
void setDebugFontLineStart(s32, s32);
void setSoundEffectChannelVolume(s32 ch, s32 vol);
void setSoundEffectChannelPan(s32 ch, s32 pan);
s32 getPlayingSoundEffectMask(s32 id);
void setMusicInstanceTempo(void* a0, s32 a1, s32 a2);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
s32 getAngleCos(s32 a);
s32 getAngleSin(s32 a);
u32 func_800405E4(void);
void initBattlingTutorialCursor(u32** ot);
void initBattlingTutorialWindow(void);
void initBattlingTutorial(void);
void updateBattlingTutorial(void);
void updateBattlingVictoryScreen(BattlingScene* scene);
void renderBattlingGround(OTag*, s32, s32);
void scaleBattlingMatrixBySVector(MATRIX* m, s16* scale);
void copyBattlingWords(void* dst, void* src, s32 size); /* hand-written word copy */
void updateBattlingHitSparks(OTag* ot, MATRIX* m);
s32 canBattlingGearAddGauge(BattlingGear* gear, s32 arg1);
void pushBattlingGearInput(BattlingGear* g, u8 v);
void clearBattlingGearInput(BattlingGear* gear);
void drawBattlingGearMoveInfo(BattlingGear* gear);
s32 tryBattlingGearBackDash(BattlingGear* g);
void updateBattlingFightCamera(BattlingGear*, BattlingGear*);
void initBattlingSettings(void);
void resetBattlingMatch(void);
s32 startBattlingRound(void);
void updateBattlingFight(BattlingGear*, BattlingGear*);
void updateBattlingReplay(BattlingGear*, BattlingGear*);
void updateBattlingRoundResult(BattlingGear*, BattlingGear*);
void loadBattlingGearColorClut(CVECTOR* a, CVECTOR* b);
void initBattlingSpriteEffects(u32** ot);
void drawBattlingEffects(MATRIX* m, OTag* ot);
void drawBattlingLineEffects(OTag* ot);
void drawBattlingAnalogWarning(void* ot);
void initBattlingFont(void** files);
void buildBattlingGearList(s32);
void initBattlingPortraitSlots(void);
void drawBattlingGearSelect(void* ot, s32 arg1);
void loadBattlingDemoGears(void);
void loadBattlingPortraitPair(s32, s32);
s32 isBattlingMainMenuOpen(void);
void enterBattlingMainMenu(void);
void freeBattlingPortraitFile(void);
void freeBattlingScreenCopy(s32);
void clearBattlingGlyphs(void);
void drawBattlingTextOverlay(void* ot);
void initBattlingMenus(void);
void updateBattlingMenus(void);
void setBattlingArenaClearTiles(void);
void setBattlingBlackClearTiles(void);
void initBattlingArena(void);
void setBattlingGroundView(s32 x, s32 z, s32 angle);
void setBattlingGroundViewNarrow(s32 x, s32 z, s32 angle);
void getBattlingGroundNormal(SVECTOR* out);
s32 getBattlingGroundHeight(VECTOR* pos, s32 flag);
u32 getBattlingMapCell(VECTOR* pos); /* the map cell word (flags in bits 24-25) */
void applyBattlingStageColors(void);
void initBattlingRingWall(u32** ot);
void drawBattlingRingWall(OTag* ot, VECTOR* pos);
void updateBattlingOrbitCamera(s32);
void clearBattlingVram(s32 wide);
void setBattlingSceneMode(s32 mode);
void updateBattlingGearGlowLights(BattlingScene* scene, BattlingGear* gear, s32 idx);
s32 renderBattlingArenaScene(BattlingScene* scene);
s32 renderBattlingDemoScene(BattlingScene* scene);
s32 renderBattlingPauseScene(BattlingScene* scene);
void beginBattlingSceneLayer(BattlingScene* scene);
s32 renderBattlingFireScene(BattlingScene* scene);
void renderBattlingFireFrame(void);
void fadeOutBattlingFire(void);
void attachBattlingGearExtraModel(BattlingGear* gear);
void setupBattlingGear(BattlingGear* gear, BattlingDataFile* file, s32 side);
void updateBattlingFireHalfRate(void);
void startBattlingDemo(void);
void stopBattlingDemo(void);
void loadBattlingGearFile(s32 side, s32 id);
void exitBattling(void);
s32 shouldBattlingSkipGearSelect(void);
void getBattlingHudBarX(s32 arg0, s16* arg1, s32 arg2);
void getBattlingHudBarY(s32 arg0, s16* arg1, s32 arg2);
void buildBattlingHudPrims(BattlingHudPrims* arg0);
void setupBattlingTimQuad(TIM_IMAGE* tim, s32 x, s32 y, POLY_FT4* p, s32 mode);
void setupBattlingTimQuadMirrored(TIM_IMAGE* tim, s32 x, s32 y, POLY_FT4* p, s32 mode);
void initBattlingHudSprites(u32** files);
void addBattlingHudTPage(void);
void drawBattlingHudBar(void* ot, POLY_F4* p, s32 w, s32 side);
s32 setBattlingEtherWarningColor(BattlingGear* arg0, POLY_F4* arg1);
void drawBattlingHud(BattlingGear* g0, BattlingGear* g1);
void resetBattlingGroundSpans(void);
void drawBattlingGround(OTag* arg0, s32 arg1, s32 arg2);
void loadBattlingTextures(u32** arg0);
void drawBattlingGearShadowQuad(BattlingGear* arg0, OTag* ot, MATRIX* m);
void clearBattlingTimedLines(void);
s32 isBattlingGearDefeated(s32 arg0);
s32 checkBattlingAllDefeated(void);
void saveBattlingSettings(void);
void showBattlingHeapDump(void);
void initBattlingCursorSprite(s32 arg0);
void initBattlingSystem(void);
void* loadBattlingFile(s32 arg0);
void setupBattlingAspectMatrix(s32 arg0, s32 arg1);
void initBattlingDispEnvs(s32 w, s32 h);
void initBattlingDrawEnvs(s32 w, s32 h);
void initBattlingDisplayAndDrawEnvs(s32 w, s32 h);
void computeBattlingLookAtMatrix(MATRIX* m, SVECTOR* eye, SVECTOR* at, SVECTOR* up);
void setBattlingSceneCamera(BattlingScene* scene, VECTOR* eye, VECTOR* at);
BattlingNode* initBattlingNode(BattlingNode* node);
BattlingNode* createBattlingNode(void);
void addBattlingNodeChild(BattlingNode* parent, BattlingNode* child);
void freeBattlingNodeTree(BattlingNode* node);
void setBattlingNodeModel(BattlingNode* node, void* data);
void setBattlingNodeSkeleton(BattlingNode* node, void* data);
void setBattlingNodeLight(BattlingNode* node, void* data);
BattlingSkeleton* createBattlingSkeleton(void);
void freeBattlingSkeleton(BattlingSkeleton* skel);
BattlingModel* initBattlingModel(BattlingModel* model);
BattlingModel* createBattlingModel(void);
void freeBattlingModel(BattlingModel* model);
void setBattlingTexturePos(s16 x, s16 y);
void setBattlingClutPos(s16 x, s16 y);
void setBattlingTextureClutPos(s16 x, s16 y, s16 clutX, s16 clutY);
void clearBattlingTextureOverride(void);
void setupBattlingModelBlock(BattlingModel* model, ModelBlock* block);
BattlingLight* createBattlingLight(void);
BattlingLayer* createBattlingLayer(u16 otSize);
void freeBattlingLayer(BattlingLayer* layer);
BattlingScene* createBattlingScene(BattlingLayer* layer);
void disableBattlingModelBackColor(void);
void renderBattlingModel(BattlingModel* model);
void applyBattlingShadowColor(BattlingNode* node);
void renderBattlingNodeTree(BattlingNode* node);
void applyBattlingLights(BattlingNode** lights);
void beginBattlingLayer(BattlingLayer* layer);
void startBattlingFrameTimer(void);
void skipEmptyBattlingOtEntries(s32 budget);
void addBattlingLayerToOt(BattlingLayer* layer);
BattlingDataFile* relocateBattlingDataFile(BattlingDataFile* file);
void resetBattlingAnim(BattlingAnim* anim);
void initBattlingAnim(BattlingAnimData* data, BattlingAnim* anim, BattlingNode* root);
BattlingNode* loadBattlingSkeleton(BattlingDataFile* file);
s16 interpolateBattlingAngle(s32 cur, s32 target, s32 step);
s32 stepBattlingAngleTowards(s32 cur, s32 target, s32 step);
s32 advanceBattlingAnim(BattlingAnim* anim, s32 frames, s32 steps);
BattlingThread* createBattlingThread(void* entry, s32 arg, u32* stack, s32 stackSize);
void resumeBattlingThread(BattlingThread* t); /* hand-written: switch to a coroutine */
void yieldBattlingThread(void);
void projectBattlingShadow(ModelBlock* block, SVECTOR* out);
void renderBattlingShadowBlock(ModelBlock* block, u8* prims, OTag* ot, SVECTOR* screenVertices);
void initBattlingShadowModel(BattlingFlatModel* fm, ModelBlock* block);
void setBattlingNodeInstance(BattlingNode* node, void* data);
BattlingInstance* createBattlingInstance(BattlingNode* src);
void freeBattlingInstance(BattlingInstance* inst);
BattlingNode* cloneBattlingShadowTree(BattlingNode* src, BattlingNode* parent);
BattlingNode* createBattlingShadowTree(BattlingNode* src);
void projectBattlingShadowVertices(SVECTOR* dst, SVECTOR* src, s32 count);
void renderBattlingShadowTris(u8* indices, s32 count);
void renderBattlingShadowQuads(u8* indices, s32 count);
BattlingEmitter* createBattlingEmitter(s32 type, s32 shape);
void allocBattlingEmitterParticles(BattlingEmitter* pool, s32 count);
void emitBattlingParticles(BattlingEmitter* pool, MATRIX* m, s32 count);
void updateBattlingEmitterParticles(BattlingEmitter* pool, OTag* arg1, MATRIX* m);
void getBattlingBoneMatrix(BattlingNode* gear, s16 bone, MATRIX* out);
void resetBattlingFire(void);
void initBattlingFire(void);
void freeBattlingFire(void);
void flipBattlingFireBuffers(void);
void updateBattlingFire(void);
void drawBattlingFire(void* ot, s32 bright, s32 flash);
void initBattlingSoundChannels(void);
void ageBattlingSoundChannels(void);
void setBattlingGearHitSounds(BattlingGear* gear);
void playBattlingSound(s32 soundId, s32 mode, VECTOR* pos, s32 arg3);
void updateBattlingSoundPositions(void);
void releaseBattlingSoundChannels(void);
void stopBattlingSoundByTag(u8 tag);
void steerBattlingAiGear(BattlingGear* gear, s16 turn, s16 limit);
void practiceAiBackDash(BattlingGear* gear, BattlingAiState* st);
void practiceAiAttack(BattlingGear* gear, BattlingAiState* st, s32 arg2);
void practiceAiMagic(BattlingGear* gear, BattlingAiState* st);
void practiceAiGuard(BattlingGear* gear, BattlingAiState* st);
void practiceAiJumpMagic(BattlingGear* gear, BattlingAiState* st);
void practiceAiJump(BattlingGear* gear, BattlingAiState* st);
void practiceAiRunAway(BattlingGear* gear, BattlingAiState* st);
void practiceAiChase(BattlingGear* gear, BattlingAiState* st);
void practiceAiBattle(BattlingGear* gear, BattlingAiState* st, s32 arg2);
s32 isBattlingGearHpAbove(BattlingGear* gear, s32 percent);
s32 canBattlingGearUseEther(BattlingGear* gear, s32 arg1);
s32 getBattlingEtherGaugeLimit(BattlingGear* gear, BattlingAiState* ai);
s32 getBattlingAiGaugeRoom(BattlingGear* gear);
s32 shouldBattlingAiFireEther(BattlingGear* gear, s32 arg1);
s32 shouldBattlingAiBoost(BattlingGear* gear, s32 flag);
void rollBattlingAiDecisions(BattlingAiState* ai);
void fireBattlingAiEther(BattlingGear* gear);
s32 isBattlingGearInWater(BattlingGear* gear);
s32 steerBattlingAiOutOfWater(BattlingGear* gear, BattlingAiState* st);
s32 avoidBattlingAiWater(BattlingGear* gear, BattlingAiState* st);
/* The opponent's nearest projectile's move type: with none, the PS1 reads
 * low RAM through NULL (0 there, in PCSX-Redux). */
#define BL_NEAREST_SHOT_MOVE(g) ((g)->m8F8_nearestProjectile != NULL ? (g)->m8F8_nearestProjectile->m33_moveType : 0)
void tryBattlingAiGuard(BattlingGear* gear, BattlingAiState* st);
void enterBattlingAiWait(BattlingGear* gear);
void updateBattlingAiWait(BattlingGear* gear, BattlingAiState* st);
void queueBattlingAiRandomAttack(BattlingGear* gear);
void startBattlingAiAttack(BattlingGear* gear, BattlingAiState* st);
s32 queueBattlingAiSpecialMove(BattlingGear* gear, BattlingAiState* st);
void enterBattlingAiMelee(BattlingGear* gear);
s32 tryBattlingAiSpecialOrEther(BattlingGear* gear, BattlingAiState* st);
void updateBattlingAiMelee(BattlingGear* gear, BattlingAiState* st);
void enterBattlingAiApproach(BattlingGear* gear, s32 arg1);
void updateBattlingAiApproach(BattlingGear* gear, BattlingAiState* st);
void enterBattlingAiRetreat(BattlingGear* gear, s32 arg1);
void updateBattlingAiRetreat(BattlingGear* gear, BattlingAiState* st);
void loadBattlingAiPersonality(BattlingGear* gear);
void updateBattlingAi(BattlingGear* gear, BattlingAiState* unused);
extern s32 runningOnDTL;
void setVsyncUserCallback(void (*)(void));
extern s32 gDepthDivider;
extern s32 objectClippingMask;
extern s32 battlingMainTaskIndex;
extern u8 battlingEntryParams; /* main: byte 0 (mode) of the field->battling entry block; the field writes it as battlingEntryParams[0..5] */
extern u8* currentModelInstanceDrawPrims;
extern volatile s32 g_playTimeInVsync; /* main vsync counter, bumped by the vsync interrupt callback: volatile (retail reads it twice in battlingMain and setBattlingTextBlinkBlue; playBattlingSfx load order) */
extern u16 newPadButtonForDialogs;
extern u16 newPadButtonForField; /* main: pad 0 auto-repeat */
extern u8* g_currentModelBlockSubBlocks;
extern SVECTOR* currentModelBlockVertices;
extern OTag* currentOTEntry;
extern u16 newPadButtonForScripts;
extern u16 newPadButtonForScripts2;
extern s32 fieldPolyCount;
extern CVECTOR fogNearColor;
extern s32 fieldPolyCount2;
extern BattlingKernelState battlingProgress;
/* 0x80088BFC is a one-entry function table {battlingMainTask} (data in .text,
 * still inside the INCLUDE_ASM of battlingMainTaskTable); the VSync callback
 * func_80088C00 follows it. */
extern void* battlingMainTaskTable[];
extern s32 battlingDebugEnabled;
extern LoadingBatchCommand battlingStartupLoads[6];
extern s32 battlingPlaysOwnMusic;
extern u16 battlingHudSpriteClut[];
extern u8 battlingArenaRowStarts[];
extern u8 battlingArenaRowEnds[];
extern u8 battlingGroundTexInfo[0x30];
extern u16 D_80091956[]; /* 4 x {tpage, clut} at 0x80091954: [i*2-1] tpage, [i*2] clut */
extern char* battlingHeapTypeNames[23]; /* heap type names (resetMemoryAllocStats): pointers into battlingGearNames */
extern MATRIX battlingIdentityMatrix;
extern s32 D_80091C2C;
extern BattlingLayer* battlingOtSkipLayer;
extern BattlingParticleType battlingParticleTypes[];
extern BattlingEmitterShapeFn battlingEmitterShapeFuncs[];
extern void* battlingGroundBounceFuncs[];
extern u16 battlingFirePalette[];
extern u8 battlingHitSoundPairs[]; /* sound id pairs (0 = none), indexed by 2 * pair */
extern u8 battlingRankarHitSounds[];
extern u8 battlingDeathHitSounds[];
extern u8 battlingWyrmHitSounds[];
extern u8 battlingDefaultHitSounds[];
extern u8 battlingChuChuHitSounds[];
extern u8 battlingGearExtraModel[0x5F4]; /* a lone model block and its data (pcModelBlockOf) */
extern u8 battlingMoveInputs[14][3];
extern s16 battlingFireFade;
extern s32 battlingFireAnimating;
extern s32 battlingSceneMode;
extern s32 battlingNextSceneMode;
extern s32 battlingStoryGear1;
extern s32 battlingStoryGear2;
extern void* battlingGearFiles[2]; /* per side, loaded model data */
extern void* battlingSfxSequence;
extern u8* battlingGroundSpanEnds;
extern u8* battlingGroundSpanStarts;
extern u16 battlingShadowTPage;
extern u16 battlingShadowClut;
extern u8 battlingShadowU;
extern u8 battlingShadowV;
extern u8 battlingBonusGearUnlocked;
extern s32 battlingGpuTime;
extern s32 battlingHeapDumpStart;
extern s16 battlingTexturePosX;
extern s16 battlingTexturePosY;
extern s16 battlingClutPosX;
extern s16 battlingClutPosY;
extern s32 battlingModelBackColorEnabled;
extern CVECTOR battlingShadowColors[2];
extern s32 battlingFrameStartTime;
extern s32 battlingShadowNodeCount;
extern BattlingNode* battlingShadowRoot;
extern SVECTOR* battlingParticleScratchVerts;
extern SVECTOR* battlingParticleScratchOrigin;
extern BattlingEmitter* battlingSparkEmitter;
extern s32 battlingSparkBurst;
extern s16* battlingFireHeat;
extern s16* battlingFireHeatNext;
extern u8* battlingFireImage;
extern u8 battlingPracticeAiShown;
extern s32 battlingGearDistance;
extern s32 battlingGearDistance3D;
extern POLY_FT3* battlingGroundPolys[2];
extern s16 battlingScreenWidth;
extern u8 battlingGauge1V;
extern u8 battlingGauge2V;
extern BattlingDisplayBuffer* battlingDisplayBuffer; /* current display buffer */
extern s16 battlingScreenHeight; /* screen height */
extern BattlingDisplayBuffer* battlingPrevDisplayBuffer;
extern BattlingGearDef* battlingGearDefs;
extern u8* battlingDialogTexts;
extern u8 battlingRubberBandMode;
extern s32 battlingGearListCount;
extern BattlingStageColors* battlingStageColors;
extern s32 battlingRoundWinner;
extern s16 battlingVsyncInterval;
extern s32 D_8009289C;
extern u8 battlingBufferIndex; /* display buffer index */
extern s32 battlingNarrowGroundView;
extern u8 battlingStageColorIndex;
extern u8 battlingEventFight;
extern s32 battlingGameMode; /* game mode */
extern s32 D_800928CC;
extern u16 battlingDebugDisplayFlags; /* debug display flags */
extern BattlingMapCell* battlingHeightMap; /* 128 x 128 arena height map */
extern OTag* battlingLayerOt;
extern s32 battlingFrameCounter;
extern BattlingGearEntry** battlingGearList; /* selectable gears */
extern s32 battlingDebugMarkerCount;
extern s32 battlingBackColorB;
extern s32 battlingBackColorG;
extern s32 battlingShadowColorChanged;
extern s32 battlingBackColorR; /* back colour */
extern u8 battlingScreenClearFlag;
extern s32 battlingStartRequested;
extern s32 battlingSpeedScale;
extern void (*battlingDebugOtHook)(void*); /* optional per-frame OT hook */
extern s32 battlingGearAngle;
extern OTag* battlingCurrentOt; /* current OT */
extern void* battlingMusic;
extern POLY_G4 battlingBackdropGradient[2]; /* backdrop gradient */
extern DR_TPAGE battlingDemoBlurTPages[4];
extern SPRT battlingDemoBlurSprites[6];
extern BattlingPrim28 battlingHudSprites[8][2];
extern DR_TPAGE battlingHudTPages[2][2];
extern BattlingEffectLine battlingTimedLines[100];
extern POLY_FT4 battlingFirePolys[2];
extern TILE battlingFadeTiles[2];
extern DR_MODE battlingFadeDrawModes[2];
extern BattlingSoundChannel battlingSoundChannels[4];
extern BattlingAiState battlingGear1Ai; /* player 1 CPU state */
extern BattlingAiState battlingGear2Ai; /* player 2 CPU state */
extern VECTOR battlingLookAtForward;
extern VECTOR battlingViewOrigin; /* camera focus offset */
extern MATRIX battlingAspectMatrix;
extern VECTOR battlingLookAtRight;
extern BattlingGear battlingGear2;
extern VECTOR battlingCameraTarget; /* camera target */
extern DialogWindow18 battlingMessageWindow;
extern VECTOR battlingCameraPos; /* camera position */
extern BattlingGear battlingGear1;
extern BattlingSettings battlingSettings;
extern u8 D_80099DA2; /* tutorial step? (a scalar: CSE keeps it across struct stores) */
extern VECTOR battlingLookAtTemp;
extern BattlingDisplayBuffer battlingDisplayBuffers[2];
extern VECTOR battlingShadowLightDir;
extern MATRIX D_8009A2D8;
extern BattlingHudPrims battlingHudPrims[2];
extern VECTOR battlingLookAtUp;
extern BattlingMarker battlingDebugMarkers[31];

void resetBattlingParticleLineF4(BattlingParticleLineF4* p) {
    p->m10_trail[2] = p->m0_pos;
    p->m10_trail[1] = p->m10_trail[2];
    p->m10_trail[0] = p->m10_trail[1];
}

void initBattlingParticleLineF4(BattlingParticleLineF4* p, BattlingEmitter* e) {
    setLineF4(&p->m28_prims[0]);
    setSemiTrans(&p->m28_prims[0], 1);
    setRGB0(&p->m28_prims[0], e->m74_r, e->m75_g, e->m76_b);
    p->m28_prims[1] = p->m28_prims[0];
}

void renderBattlingParticleLineF4(BattlingParticleLineF4* p, OTag* ot) {
    LINE_F4* prim;
    s32 dummy;
    s32 otz;

    prim = &p->m28_prims[battlingBufferIndex];
    otz = RotTransPers4(&p->m0_pos, &p->m10_trail[0], &p->m10_trail[1], &p->m10_trail[2], (s32*)&prim->x0,
                        (s32*)&prim->x1, (s32*)&prim->x2, (s32*)&prim->x3, &dummy, &dummy);
    p->m10_trail[2] = p->m10_trail[1];
    p->m10_trail[1] = p->m10_trail[0];
    p->m10_trail[0] = p->m0_pos;
    addLineF4(&ot[otz >> 2], prim);
}

void resetBattlingParticleLineF3(BattlingParticleLineF3* p) {
    p->m10_trail[1] = p->m0_pos;
    p->m10_trail[0] = p->m10_trail[1];
}

void initBattlingParticleLineF3(BattlingParticleLineF3* p, BattlingEmitter* e) {
    setLineF3(&p->m20_prims[0]);
    setSemiTrans(&p->m20_prims[0], 1);
    setRGB0(&p->m20_prims[0], e->m74_r, e->m75_g, e->m76_b);
    p->m20_prims[1] = p->m20_prims[0];
}

void renderBattlingParticleLineF3(BattlingParticleLineF3* p, OTag* ot) {
    LINE_F3* prim;
    s32 otz;

    battlingParticleScratchVerts[0].vx = p->m0_pos.vx - battlingParticleScratchOrigin->vx;
    battlingParticleScratchVerts[0].vy = p->m0_pos.vy - battlingParticleScratchOrigin->vy;
    battlingParticleScratchVerts[0].vz = p->m0_pos.vz - battlingParticleScratchOrigin->vz;
    battlingParticleScratchVerts[1].vx = p->m10_trail[0].vx - battlingParticleScratchOrigin->vx;
    battlingParticleScratchVerts[1].vy = p->m10_trail[0].vy - battlingParticleScratchOrigin->vy;
    battlingParticleScratchVerts[1].vz = p->m10_trail[0].vz - battlingParticleScratchOrigin->vz;
    battlingParticleScratchVerts[2].vx = p->m10_trail[1].vx - battlingParticleScratchOrigin->vx;
    battlingParticleScratchVerts[2].vy = p->m10_trail[1].vy - battlingParticleScratchOrigin->vy;
    battlingParticleScratchVerts[2].vz = p->m10_trail[1].vz - battlingParticleScratchOrigin->vz;
    gte_ldv3c(battlingParticleScratchVerts);
    gte_rtpt();
    prim = &p->m20_prims[battlingBufferIndex];
    p->m10_trail[1] = p->m10_trail[0];
    p->m10_trail[0] = p->m0_pos;
    gte_stsxy3(&prim->x0, &prim->x1, &prim->x2);
    gte_stszotz(&otz);
    addLineF3(&ot[otz >> 2], prim);
}

void resetBattlingParticleLineF2(BattlingParticleLineF2* p) {
    p->m10_trail[0] = p->m0_pos;
}

void initBattlingParticleLineF2(BattlingParticleLineF2* p, BattlingEmitter* e) {
    setLineF2(&p->m18_prims[0]);
    setSemiTrans(&p->m18_prims[0], 1);
    setRGB0(&p->m18_prims[0], e->m74_r, e->m75_g, e->m76_b);
    p->m18_prims[1] = p->m18_prims[0];
}

void renderBattlingParticleLineF2(BattlingParticleLineF2* p, OTag* ot) {
    LINE_F2* prim;
    s32 dummy;
    s32 otz;

    prim = &p->m18_prims[battlingBufferIndex];
    otz = RotTransPers3(&p->m0_pos, &p->m10_trail[0], (SVECTOR*)&dummy, (s32*)&prim->x0, (s32*)&prim->x1, &dummy,
                        &dummy, &dummy);
    p->m10_trail[0] = p->m0_pos;
    addLineF2(&ot[otz >> 2], prim);
}

void resetBattlingParticleTile(void) {
}

void initBattlingParticleTile(BattlingParticleTile* p, BattlingEmitter* e) {
    setTile(&p->m10_prims[0]);
    setSemiTrans(&p->m10_prims[0], 1);
    p->m10_prims[0].h = rand() % 2 + 2;
    p->m10_prims[0].w = p->m10_prims[0].h * 2;
    setRGB0(&p->m10_prims[0], e->m74_r, e->m75_g, e->m76_b);
    p->m10_prims[1] = p->m10_prims[0];
}

void renderBattlingParticleTile(BattlingParticleTile* p, OTag* ot) {
    SVECTOR v;
    s32 otz;
    TILE* prim;

    v.vx = p->m0_pos.vx - battlingParticleScratchOrigin->vx;
    v.vy = p->m0_pos.vy - battlingParticleScratchOrigin->vy;
    v.vz = p->m0_pos.vz - battlingParticleScratchOrigin->vz;
    gte_ldv0(&v);
    gte_rtps();
    prim = &p->m10_prims[battlingBufferIndex];
    gte_stsxy(&prim->x0);
    gte_stszotz(&otz);
    addTile(&ot[otz >> 2], prim);
}

void resetBattlingParticleTile1(void) {
}

void initBattlingParticleTile1(BattlingParticleTile1* p, BattlingEmitter* e) {
    setTile1(&p->m10_prims[0]);
    setSemiTrans(&p->m10_prims[0], 1);
    setRGB0(&p->m10_prims[0], e->m74_r, e->m75_g, e->m76_b);
    p->m10_prims[1] = p->m10_prims[0];
}

void renderBattlingParticleTile1(BattlingParticleTile1* p, OTag* ot) {
    TILE_1* prim;
    s32 dummy;

    prim = &p->m10_prims[battlingBufferIndex];
    addTile1(&ot[RotTransPers(&p->m0_pos, (s32*)&prim->x0, &dummy, &dummy) >> 2], prim);
}

void getBattlingEmitterPosPoint(BattlingEmitter* e, SVECTOR* out) {
    *out = e->m1C_origin;
}

void getBattlingEmitterPosInBox(BattlingEmitter* e, SVECTOR* out) {
    SVECTOR v;
    VECTOR r;

    v.vx = R3000_MOD(rand(), e->m24_size[0]) - e->m2C_halfSize[0];
    v.vy = R3000_MOD(rand(), e->m24_size[1]) - e->m2C_halfSize[1];
    v.vz = R3000_MOD(rand(), e->m24_size[2]) - e->m2C_halfSize[2];
    ApplyRotMatrix(&v, &r);
    out->vx = e->m1C_origin.vx + r.vx;
    out->vy = e->m1C_origin.vy + r.vy;
    out->vz = e->m1C_origin.vz + r.vz;
}

void getBattlingEmitterPosInAlignedBox(BattlingEmitter* e, SVECTOR* out) {
    out->vx = e->m1C_origin.vx + R3000_MOD(rand(), e->m24_size[0]) - e->m2C_halfSize[0];
    out->vy = e->m1C_origin.vy + R3000_MOD(rand(), e->m24_size[1]) - e->m2C_halfSize[1];
    out->vz = e->m1C_origin.vz + R3000_MOD(rand(), e->m24_size[2]) - e->m2C_halfSize[2];
}

void getBattlingEmitterPosInRect(BattlingEmitter* e, SVECTOR* out) {
    SVECTOR v;
    VECTOR r;

    v.vx = R3000_MOD(rand(), e->m24_size[0]) - e->m2C_halfSize[0];
    v.vy = 0;
    v.vz = R3000_MOD(rand(), e->m24_size[2]) - e->m2C_halfSize[2];
    ApplyRotMatrix(&v, &r);
    out->vx = e->m1C_origin.vx + r.vx;
    out->vy = e->m1C_origin.vy + r.vy;
    out->vz = e->m1C_origin.vz + r.vz;
}

void getBattlingEmitterPosInDisc(BattlingEmitter* e, SVECTOR* out) {
    SVECTOR v;
    VECTOR r;
    s32 angle;
    s32 dist;

    angle = rand();
    dist = rand();
    v.vx = (getAngleCos(angle) * R3000_MOD(dist, e->m24_size[0])) >> 13;
    v.vy = 0;
    v.vz = (getAngleSin(angle) * R3000_MOD(dist, e->m24_size[2])) >> 13;
    ApplyRotMatrix(&v, &r);
    out->vx = e->m1C_origin.vx + r.vx;
    out->vy = e->m1C_origin.vy + r.vy;
    out->vz = e->m1C_origin.vz + r.vz;
}

void getBattlingEmitterPosOnCylinder(BattlingEmitter* e, SVECTOR* out) {
    SVECTOR v;
    VECTOR r;
    s32 angle; /* never set: retail reads an uninitialised register */

    /* Retail's register is the caller's s2: the emitter's address. */
    angle = (s32)(uptr)e;
    v.vx = (getAngleCos(angle) * e->m24_size[0]) >> 12;
    v.vy = R3000_MOD(rand(), e->m24_size[1]) - e->m24_size[1] / 2;
    v.vz = (getAngleSin(angle) * e->m24_size[2]) >> 12;
    ApplyRotMatrix(&v, &r);
    out->vx = e->m1C_origin.vx + r.vx;
    out->vy = e->m1C_origin.vy + r.vy;
    out->vz = e->m1C_origin.vz + r.vz;
}

/* sizeof each particle type of battlingParticleTypes (its mC_size is the PS1 size). */
static const s32 pcBattlingParticleSizes[5] = {
    sizeof(BattlingParticleLineF4), sizeof(BattlingParticleLineF3), sizeof(BattlingParticleLineF2),
    sizeof(BattlingParticleTile), sizeof(BattlingParticleTile1),
};

BattlingEmitter* createBattlingEmitter(s32 type, s32 shape) {
    BattlingEmitter* e;
    BattlingParticleType* t;

    traceNextAlloc(0x15);
    e = allocateHeap((s32)sizeof(BattlingEmitter), 0);
    e->m24_size[0] = 0x1000;
    e->m24_size[1] = 0x1000;
    e->m24_size[2] = 0x1000;
    e->m44_spread = 1;
    e->m4E_shape = shape;
    e->m2 = 0;
    e->m4 = 0;
    e->m6 = 0;
    e->m14_x = 0;
    e->m16_y = 0;
    e->m18_z = 0;
    e->m34_rotation.vx = 0;
    e->m34_rotation.vy = 0;
    e->m34_rotation.vz = 0;
    e->m3C_rotation2.vx = 0;
    e->m3C_rotation2.vy = 0;
    e->m3C_rotation2.vz = 0;
    e->m0_gravity = 0;
    e->m46 = 0;
    e->m48_speedMin = 0x100;
    e->m4A_speedRange = 0x100;
    e->m2C_halfSize[0] = e->m24_size[0] / 2;
    e->m2C_halfSize[1] = e->m24_size[1] / 2;
    e->m2C_halfSize[2] = e->m24_size[2] / 2;
    e->m5C_shape = battlingEmitterShapeFuncs[(s16)shape];
    e->m60_groundBounce = battlingGroundBounceFuncs[0];
    e->m10_particles = 0;
    e->m66_type = type;
    e->m64 = 0;
    e->m6A_life = 100;
    e->m74_r = 0xFF;
    e->m75_g = 0xFF;
    e->m76_b = 0xFF;
    e->m68 = 0;
    t = &battlingParticleTypes[e->m66_type];
    e->mC_particleSize = pcBattlingParticleSizes[e->m66_type]; /* the table has the PS1 sizes */
    e->m50_reset = t->m4_reset;
    e->m54_render = t->m8_render;
    e->m58_init = t->m0_init;
    return e;
}

void clearBattlingEmitterParticles(BattlingEmitter* pool) {
    s32 i;
    BattlingParticle* p;

    p = pool->m10_particles;
    for (i = 0; i < pool->mA_count; i++) {
        p->m6_life = 0;
        p = (BattlingParticle*)((u8*)p + pool->mC_particleSize);
    }
}

void allocBattlingEmitterParticles(BattlingEmitter* pool, s32 count) {
    s32 i;
    BattlingParticle* p;
    BattlingParticleInitFn init;

    if (pool->m10_particles != NULL) {
        freeHeapDelayed(pool->m10_particles, 3);
    }
    pool->mA_count = count;
    traceNextAlloc(0x14);
    pool->m10_particles = allocateHeap(pool->mC_particleSize * pool->mA_count, 0);
    init = pool->m58_init;
    p = pool->m10_particles;
    for (i = 0; i < pool->mA_count; i++) {
        init(p, pool);
        p->m6_life = 0;
        p = (BattlingParticle*)((u8*)p + pool->mC_particleSize);
    }
}

void emitBattlingParticles(BattlingEmitter* pool, MATRIX* m, s32 count) {
    SVECTOR dir;
    SVECTOR tmp;
    MATRIX rot;
    MATRIX local;
    MATRIX rot2;
    void (*onSpawn)(BattlingParticle*);
    void (*spawn)(struct BattlingEmitter*, BattlingParticle*);
    BattlingParticle* p;
    s32 i;
    s32 n;
    s32 a;
    s32 b;

    onSpawn = pool->m50_reset;
    spawn = pool->m5C_shape;
    local = *m;
    i = 0;
    pool->m1C_origin.vx = pool->m14_x + m->t[0];
    pool->m1C_origin.vy = pool->m16_y + m->t[1];
    pool->m1C_origin.vz = pool->m18_z + m->t[2];
    createRotationMatrix(&pool->m34_rotation, &rot);
    MulMatrix(&local, &rot);
    createRotationMatrix(&pool->m3C_rotation2, &rot2);
    MulMatrix(&rot2, &local);
    SetRotMatrix(&rot2);
    n = count;
    p = pool->m10_particles;
    for (; i < pool->mA_count; i++) {
        if (p->m6_life == 0) {
            if (--n == -1) {
                break;
            }
            a = R3000_MOD(rand(), pool->m44_spread);
            b = rand();
            dir.vy = -getAngleSin(a);
            a = getAngleCos(a);
            dir.vx = (getAngleCos(b) * a) >> 12;
            dir.vz = (getAngleSin(b) * a) >> 12;
            b = pool->m48_speedMin + R3000_MOD(rand(), pool->m4A_speedRange);
            gte_ldv0(&dir);
            gte_rtv0();
            gte_stsv(&tmp);
            gte_lddp(b);
            gte_ldsv(&tmp);
            gte_gpf12();
            gte_stsv(&p->m8_vx);
        }
        p = (BattlingParticle*)((u8*)p + pool->mC_particleSize);
    }
    SetRotMatrix(&local);
    n = count;
    p = pool->m10_particles;
    for (i = 0; i < pool->mA_count; i++) {
        if (p->m6_life == 0) {
            if (--n == -1) {
                break;
            }
            spawn(pool, p);
            p->m6_life = pool->m6A_life;
            onSpawn(p);
        }
        p = (BattlingParticle*)((u8*)p + pool->mC_particleSize);
    }
}

void bounceBattlingParticleOnGround(BattlingParticle* p) {
    if (p->mA_vy > 0) {
        if (p->m2_y > getBattlingGroundHeight(p, 0)) {
            p->mA_vy = -p->mA_vy / 2;
        }
    }
}

void bounceBattlingParticleOnFloor(BattlingParticle* p) {
    if (p->m2_y > 0) {
        p->mA_vy = -p->mA_vy / 2;
        if (abs(p->mA_vy) < 8) {
            p->m6_life = 0;
        }
    }
}

void updateBattlingEmitterParticles(BattlingEmitter* pool, OTag* arg1, MATRIX* m) {
    BattlingParticle* p;
    s32 i;
    MATRIX unused;
    SVECTOR sv;
    void (*draw)(BattlingParticle*, OTag*);

    battlingParticleScratchVerts = (void*)0x1F800000;
    battlingParticleScratchOrigin = (void*)0x1F800030;
    ((SVECTOR*)0x1F800030)->vx = battlingViewOrigin.vx;
    ((SVECTOR*)0x1F800030)->vy = battlingViewOrigin.vy;
    ((SVECTOR*)0x1F800030)->vz = battlingViewOrigin.vz;
    p = pool->m10_particles;
    draw = pool->m54_render;
    for (i = 0; i < pool->mA_count; i++) {
        if (p->m6_life != 0) {
            p->m6_life--;
            p->mA_vy += pool->m0_gravity;
            p->m0_x += p->m8_vx;
            p->m2_y += p->mA_vy;
            p->m4_z += p->mC_vz;
            if (p->m2_y > 0) {
                p->mA_vy = -p->mA_vy * 2 / 3 * 1;
                if (abs(p->mA_vy) < 4) {
                    p->m6_life = 0;
                }
            }
            draw(p, arg1);
        }
        p = (BattlingParticle*)((u8*)p + pool->mC_particleSize);
    }
}

void getBattlingBoneMatrix(BattlingNode* gear, s16 bone, MATRIX* out) {
    MATRIX m;

    *out = NODE_SKELETON(gear)->m4_bones[bone]->m4C_localMatrix;
}

void createBattlingSparkEmitter(void) {
    BattlingEmitter* pool;

    pool = createBattlingEmitter(1, 0);
    pool->m74_r = 0xFF;
    pool->m75_g = 0xA0;
    pool->m76_b = 0x70;
    allocBattlingEmitterParticles(pool, 0x100);
    pool->m0_gravity = 4;
    pool->m44_spread = 0x60;
    pool->m4A_speedRange = 0x60;
    pool->m48_speedMin = 4;
    pool->m68 = 0;
    pool->m6A_life = 0x20;
    battlingSparkEmitter = pool;
}

void setBattlingSparkBurst(s32 arg0) {
    battlingSparkBurst = arg0;
}

void updateBattlingSparkBurst(OTag* arg0, BattlingNode* gear, MATRIX* m, VECTOR* pos) {
    MATRIX norm;
    MATRIX bone;
    BattlingEmitter* pool;

    pool = battlingSparkEmitter;
    if (battlingSparkBurst >= 0x10) {
        getBattlingBoneMatrix(gear, 0x27, &bone);
        MatrixNormal(&bone, &norm);
        norm.t[2] = 0;
        norm.t[1] = 0;
        norm.t[0] = 0;
        pool->m14_x = pos->vx;
        pool->m16_y = pos->vy;
        pool->m18_z = pos->vz;
        pool->m3C_rotation2.vx = 0;
        pool->m3C_rotation2.vy = 0;
        pool->m3C_rotation2.vz = 0;
        pool->m34_rotation.vx = 0;
        pool->m34_rotation.vy = 0;
        pool->m34_rotation.vz = 0x800;
        emitBattlingParticles(pool, &norm, battlingSparkBurst >> 4);
        battlingSparkBurst -= 4;
    }
    gte_SetTransMatrix(m);
    gte_SetRotMatrix(m);
    updateBattlingEmitterParticles(pool, arg0, m);
}

INCLUDE_ASM("battling/nonmatchings/bl_8008C7C0", rotateScaleBattlingSVector);

INCLUDE_ASM("battling/nonmatchings/bl_8008C7C0", renderBattlingParticleTileAsm);

void resetBattlingFire(void) {
    battlingFireImage = 0;
    battlingFireHeat = 0;
    battlingFireHeatNext = 0;
}

void initBattlingFire(void) {
    s32 i;
    RECT rect;

    if (battlingFireImage == NULL) {
        battlingFireImage = allocateHeap(0x1500, 1);
        battlingFireHeat = allocateHeap(0x2BC0, 1);
        battlingFireHeatNext = allocateHeap(0x2BC0, 1);
    }
    for (i = 0x1570; i != -1; i--) {
        battlingFireHeatNext[i] = 0;
        battlingFireHeat[i] = 0;
    }
    for (i = 0; i < 0x1500; i++) {
        battlingFireImage[i] = 0;
    }
    for (i = 0; i < 0x100; i++) {
        battlingFirePalette[i] |= 0x8000;
    }
    rect.x = 0;
    rect.y = 0x1FD;
    rect.w = 0xFF;
    rect.h = 1;
    LoadImage(&rect, (u32*)battlingFirePalette);
    pcWideBattlingFireReset(); /* the margins' fire (port/src/game/battling/wide.c) */
}

void freeBattlingFire(void) {
    if (battlingFireImage != NULL) {
        freeHeapDelayed(battlingFireImage, 2);
        freeHeapDelayed(battlingFireHeat, 2);
        freeHeapDelayed(battlingFireHeatNext, 2);
        battlingFireImage = NULL;
        battlingFireHeat = NULL;
        battlingFireHeatNext = NULL;
    }
}

void flipBattlingFireBuffers(void) {
    s32 i;
    u8* dst8;
    u32* dst;
    u32* src;
    u32 v;

    dst8 = battlingFireImage;
    dst = (u32*)battlingFireHeat;
    src = (u32*)battlingFireHeatNext;
    for (i = 0xA7F; i != -1; i--) {
        v = *src++;
        *dst++ = v;
        *dst8++ = v;
        *dst8++ = v >> 16;
    }
}

void updateBattlingFire(void) {
    s32 x;
    s32 y;
    s32 v;
    s32 i;
    s16* src;
    s16* dst;
    s16* d;
    s16* up;
    s16* right;
    s16* left;
    s16* dr;
    s16* dl;
    s32 heat;

    if (battlingFireImage != NULL) {
        heat = 0;
        dst = battlingFireHeatNext;
        d = &dst[0x1490];
        for (i = 0; i < 0x70; i++) {
            switch (rand() & 3) {
            case 0:
                heat = 0x180;
                break;
            case 1:
                heat = 0;
                break;
            }
            d[i] = d[i + 0x70] = heat;
        }
        src = battlingFireHeat;
        up = src - 0x70;
        right = src + 1;
        left = src - 1;
        dr = src + 0x71;
        dl = src + 0x6F;
        for (y = 0x2F; y >= 2; y--) {
            i = y * 0x70 + 1;
            d = &dst[(y - 1) * 0x70];
            for (x = 1; x < 0x70; x++) {
                v = (up[i] + right[i] + left[i] + dr[i] + dl[i++]) / 5;
                if (v >= 4) {
                    v -= 3;
                }
                d[x] = v;
            }
        }
        flipBattlingFireBuffers();
        pcWideBattlingFireUpdate(battlingFireHeat);
    }
}

void drawBattlingFadeTile(void* ot, s32 c, s32 add) {
    TILE* t;

    t = &battlingFadeTiles[battlingBufferIndex];
    *(u32*)&t->r0 = c | (c << 8) | (c << 16) | 0x60000000;
    setlen(t, 3);
    *(u32*)&t->x0 = 0;
    *(u32*)&t->w = 0xDA0140;
    t->code |= 2;
    AddPrim(ot, t);
    if (add) {
        SetDrawMode(&battlingFadeDrawModes[battlingBufferIndex], 0, 1, GetTPage(0, 2, 0, 0), NULL);
    } else {
        SetDrawMode(&battlingFadeDrawModes[battlingBufferIndex], 0, 1, GetTPage(0, 1, 0, 0), NULL);
    }
    AddPrim(ot, &battlingFadeDrawModes[battlingBufferIndex]);
}

void drawBattlingFire(void* ot, s32 bright, s32 flash) {
    POLY_FT4* p;
    TILE* t;
    s32 c;

    p = &battlingFirePolys[battlingBufferIndex];
    setlen(p, 9);
    c = bright * 2 / 3;
    *(u32*)&p->r0 = c | (c << 8) | (c << 16) | 0x2C000000;
    if (c == 0x80) {
        p->code |= 1;
    } else {
        p->code &= ~1;
    }
    *(u32*)&p->x0 = 0;
    *(u32*)&p->x1 = 0x140;
    *(u32*)&p->x2 = 0xDA0000;
    *(u32*)&p->x3 = 0xDA0140;
    *(u16*)&p->u0 = 0;
    *(u16*)&p->u1 = 0x6F;
    *(u16*)&p->u2 = 0x2A00;
    *(u16*)&p->u3 = 0x2A6F;
    p->code |= 2;
    p->tpage = GetTPage(1, 1, 0x140, 0x100);
    p->clut = GetClut(0, 0x1FD);
    pcWideBattlingFire(ot, c, p->tpage); /* the margins' fire, drawn after p */
    AddPrim(ot, p);
    {
        RECT rect;
        rect.x = 0x140;
        rect.y = 0x100;
        rect.w = 0x38;
        rect.h = 0x2B;
        LoadImage(&rect, battlingFireImage);
    }
    if (flash) {
        t = &battlingFadeTiles[battlingBufferIndex];
        if (bright > 0x80) {
            c = bright * 2;
            *(u32*)&t->r0 = c | (c << 8) | (c << 16) | 0x60000000;
            setlen(t, 3);
            *(u32*)&t->x0 = 0;
            *(u32*)&t->w = 0xDA0140;
            t->code |= 2;
            AddPrim(ot, t);
        }
    }
    SetDrawMode(&battlingFadeDrawModes[battlingBufferIndex], 0, 1, GetTPage(0, 2, 0, 0), NULL);
    AddPrim(ot, &battlingFadeDrawModes[battlingBufferIndex]);
}

void initBattlingSoundChannels(void) {
    u32 i;
    u32 mask;
    BattlingSoundChannel* ch;

    mask = 0x300;
    stopAllSounds();
    for (i = 0; i < 4; i++) {
        ch = &battlingSoundChannels[i];
        ch->mC_voiceMask = mask;
        mask <<= 2;
        ch->m1_active = 0;
        ch->m4_age = 0;
        ch->m2_voice = i * 2;
    }
}

void ageBattlingSoundChannels(void) {
    if (battlingSoundChannels[0].m4_age != 0xFFFF) {
        battlingSoundChannels[0].m4_age++;
    }
    if (battlingSoundChannels[1].m4_age != 0xFFFF) {
        battlingSoundChannels[1].m4_age++;
    }
    if (battlingSoundChannels[2].m4_age != 0xFFFF) {
        battlingSoundChannels[2].m4_age++;
    }
    if (battlingSoundChannels[3].m4_age != 0xFFFF) {
        battlingSoundChannels[3].m4_age++;
    }
}

void setBattlingGearHitSounds(BattlingGear* gear) {
    switch (gear->m909_gearId) {
    case 9:
        gear->m1664_hitSounds = battlingChuChuHitSounds;
        break;
    case 0x1D:
        gear->m1664_hitSounds = battlingRankarHitSounds;
        break;
    case 0x1B:
        gear->m1664_hitSounds = battlingWyrmHitSounds;
        break;
    case 0x24:
        gear->m1664_hitSounds = battlingDeathHitSounds;
        break;
    default:
        gear->m1664_hitSounds = battlingDefaultHitSounds;
        break;
    }
}

void playBattlingSound(s32 soundId, s32 mode, VECTOR* pos, s32 arg3) {
    s32 i;
    s32 maxAge;
    s32 age;
    BattlingSoundChannel* ch;
    BattlingSoundChannel* best;

    maxAge = 0;
    best = &battlingSoundChannels[3];
    for (i = 0; i < 4; i++) {
        ch = &battlingSoundChannels[i];
        if (!ch->m1_active) {
            best = ch;
            break;
        }
        if (!mode && !ch->m0_mode) {
            best = ch;
            break;
        }
        age = ch->m4_age;
        if (maxAge < age) {
            maxAge = age;
            best = ch;
        }
    }
    ageBattlingSoundChannels();
    ch = best;
    ch->m0_mode = mode;
    ch->m8_soundId = soundId;
    ch->m1_active = 1;
    ch->m3_tag = arg3;
    ch->m20_follow = pos;
    if (pos != NULL) {
        ch->m10_position = *pos;
    }
    ch->m4_age = 0;
    if (mode == 0) {
        PlaySoundEffect2(ch->m8_soundId, ch->m2_voice, 0x7F, 0x40);
    }
}

int pcBattling60InBetween(void); /* port/src/game/battling/between.c */
void updateBattlingSoundPositions(void) {
    s32 i;
    BattlingSoundChannel* ch;
    SVECTOR v;
    s16 sxy[2];
    s32 sz;
    s32 x;
    s32 vol;
    s32 pan;

    if (pcBattling60InBetween()) return; /* Enhanced, 60 Hz: the picture between places no sounds */
    for (i = 0; i < 4; i++) {
        ch = &battlingSoundChannels[i];
        if (ch->m1_active == 0) {
            continue;
        }
        if (ch->m0_mode == 0) {
            continue;
        }
        if (ch->m0_mode == 1) {
            v.vx = ch->m10_position.vx;
            v.vy = ch->m10_position.vy;
            v.vz = ch->m10_position.vz;
        } else {
            v.vx = ch->m20_follow->vx;
            v.vy = ch->m20_follow->vy;
            v.vz = ch->m20_follow->vz;
        }
        v.vx -= battlingViewOrigin.vx;
        v.vy -= battlingViewOrigin.vy;
        v.vz -= battlingViewOrigin.vz;
        gte_ldv0(&v);
        gte_rtps();
        gte_stsxy(sxy);
        gte_stsz(&sz);
        x = sxy[0];
        if (x < 0) {
            x = 0;
        }
        if (x > 0x140) {
            x = 0x140;
        }
        vol = (0x3000 - sz) * 0x7F / 0x3000;
        if (vol < 0x28) {
            vol = 0x28;
        }
        if (vol > 0x7F) {
            vol = 0x7F;
        }
        pan = x * 0x7F / 0x140;
        if (ch->m4_age != 0) {
            setSoundEffectChannelPan(ch->m2_voice, pan);
            setSoundEffectChannelVolume(ch->m2_voice, vol);
        } else {
            PlaySoundEffect2(ch->m8_soundId, ch->m2_voice, vol, pan);
        }
    }
    ageBattlingSoundChannels();
}

void releaseBattlingSoundChannels(void) {
    s32 i;
    BattlingSoundChannel* ch;

    for (i = 0; i < 4; i++) {
        ch = &battlingSoundChannels[i];
        if (!(getPlayingSoundEffectMask(ch->m8_soundId) & ch->mC_voiceMask)) {
            ch->m1_active = 0;
        }
    }
    ageBattlingSoundChannels();
}

void playBattlingSfx(s32 id) {
    if (id != 0) {
        playBattlingSound(id + 0x60000, 0, NULL, g_playTimeInVsync);
    }
}

void playBattlingGearSound(BattlingGear* gear, s32 id, VECTOR* pos, s32 mode) {
    if (id != 0) {
        playBattlingSound(id + 0x60000, mode, pos, (id & 0x7F) | ((gear->mD0_status.flags >> 20) & 0x80));
    }
}

s32 playBattlingGearHitSound(BattlingGear* gear, s32 kind, VECTOR* pos, s32 mode) {
    s32 idx;

    if (kind == 0) {
        kind = rand() % 6 + 1;
    }
    idx = gear->m1664_hitSounds[kind];
    if (idx == 0xFF) {
        IMPLICIT_RETURN(0xFF);
    }
    kind = battlingHitSoundPairs[idx * 2];
    if (kind != 0) {
        playBattlingSound(kind | 0x60000, mode, pos, (kind & 0x7F) | ((gear->mD0_status.flags >> 20) & 0x80));
    }
    kind = battlingHitSoundPairs[idx * 2 + 1];
    if (kind != 0) {
        playBattlingSound(kind | 0x60000, mode, pos, (kind & 0x7F) | ((gear->mD0_status.flags >> 20) & 0x80));
    }
    FALLOFF_RETURN(0); /* $v0: a table address or the callee's; unused */
}

void stopBattlingSoundByTag(u8 tag) {
    s32 i;
    BattlingSoundChannel* ch;

    for (i = 0; i < 4; i++) {
        ch = &battlingSoundChannels[i];
        if (ch->m1_active != 0 && ch->m3_tag == tag) {
            StopSoundEffect(ch->m2_voice);
            ch->m1_active = 0;
        }
    }
}

s32 stopBattlingGearHitSound(BattlingGear* gear, s32 kind) {
    s32 idx;

    idx = gear->m1664_hitSounds[kind];
    if (idx == 0xFF) {
        IMPLICIT_RETURN(0xFF);
    }
    if (battlingHitSoundPairs[idx * 2] != 0) {
        stopBattlingSoundByTag((battlingHitSoundPairs[idx * 2] & 0x7F) | ((gear->mD0_status.flags >> 20) & 0x80));
    }
    if (battlingHitSoundPairs[idx * 2 + 1] != 0) {
        stopBattlingSoundByTag((battlingHitSoundPairs[idx * 2 + 1] & 0x7F) | ((gear->mD0_status.flags >> 20) & 0x80));
    }
    FALLOFF_RETURN(0); /* $v0: 0 or the callee's; unused */
}

void steerBattlingAiGear(BattlingGear* gear, s16 turn, s16 limit) {
    s32 decel;
    s32 accel;
    s32 i;
    s32 canAccel;
    VECTOR unused;

    decel = gear->m9C_decel;
    accel = gear->m98_accel;
    if (gear->mD0_status.flags & 0x100) {
        decel = decel * 2 / 3;
        canAccel = 0;
    } else {
        canAccel = 1;
    }
    for (i = 0; i < 2; i++) {
        if (limit != 0 && canAccel) {
            gear->m48_speed += accel;
            if (gear->m48_speed > limit) {
                gear->m48_speed = limit;
            }
        } else {
            gear->m48_speed -= decel;
            if (gear->m48_speed < 0) {
                gear->m48_speed = 0;
            }
        }
    }
    gear->m58_targetRotation = stepBattlingAngleTowards(gear->m58_targetRotation, turn, 0x40);
    gear->mCE_inputRotation = 0;
}

void practiceAiBackDash(BattlingGear* gear, BattlingAiState* st) {
    tryBattlingGearBackDash(gear);
    st->m4_timer = 0x3C;
}

void practiceAiAttack(BattlingGear* gear, BattlingAiState* st, s32 arg2) {
    queueBattlingAiRandomAttack(gear);
    st->m4_timer = arg2 ? 0x1E : 0xA;
}

void practiceAiMagic(BattlingGear* gear, BattlingAiState* st) {
    pushBattlingGearInput(gear, 3);
    st->m4_timer = 0x3C;
}

void practiceAiGuard(BattlingGear* gear, BattlingAiState* st) {
    st->m2C_b13 ^= 1;
    if (st->m2C_b13) {
        gear->mD0_status.flags |= 2;
        st->m4_timer = 0x3C;
    } else {
        gear->mD0_status.flags &= ~2;
        gear->mD0_status.flags &= ~0x38;
        st->m4_timer = 0x1E;
    }
}

void practiceAiJumpMagic(BattlingGear* gear, BattlingAiState* st) {
    pushBattlingGearInput(gear, 4);
    pushBattlingGearInput(gear, 3);
    st->m4_timer = 0x3C;
}

void practiceAiJump(BattlingGear* gear, BattlingAiState* st) {
    pushBattlingGearInput(gear, 4);
    st->m4_timer = 0x3C;
}

void practiceAiRunAway(BattlingGear* gear, BattlingAiState* st) {
    if (battlingGearDistance > 0x800) {
        st->m4_timer = 1;
        st->mC_speed = 0;
        return;
    }
    if (--st->m4_timer == -1) {
        st->mA_turn = rand() % 0x600 + 0x500;
        st->m4_timer = rand() % 50 + 10;
        st->mC_speed = 0xFF;
    }
}

void practiceAiChase(BattlingGear* gear, BattlingAiState* st) {
    if (battlingGearDistance < 0x100) {
        st->m4_timer = 1;
        st->mC_speed = 0;
        return;
    }
    if (--st->m4_timer == -1) {
        st->mA_turn = rand() % 0x600 - 0x300;
        st->m4_timer = rand() % 120 + 10;
        st->mC_speed = 0xFF;
    }
}

void practiceAiBattle(BattlingGear* gear, BattlingAiState* st, s32 arg2) {
    st->mF_comLevel = arg2;
    updateBattlingAi(gear, st);
}

void updateBattlingPracticeAi(BattlingGear* gear) {
    BattlingAiState* st;

    st = gear->m15FC_ai;
    battlingSettings.m6_p2Com = 1;
    if (battlingPracticeAiShown != D_80099DA2) {
        st->m4_timer = 0;
        st->mC_speed = 0;
        gear->mD0_status.flags &= ~2;
        gear->m48_speed = 0;
        gear->mD0_status.flags &= ~0x38;
        st->m2C_b13 = 0;
        battlingPracticeAiShown = D_80099DA2;
        gear->mCE_inputRotation = gear->mCC_sideInputRotation + 0x800;
    }
    if (st->m2C_b13) {
        gear->mD0_status.flags |= 2;
    }
    switch (D_80099DA2) {
    case 2:
    case 8:
    case 9:
    case 11:
    case 12:
    case 13:
        break;
    default:
        if (--st->m4_timer != -1) {
            return;
        }
        break;
    }
    switch (D_80099DA2) {
    case 3:
        practiceAiAttack(gear, st, 0);
        break;
    case 4:
        practiceAiAttack(gear, st, 1);
        break;
    case 5:
        practiceAiMagic(gear, st);
        break;
    case 6:
        practiceAiJumpMagic(gear, st);
        break;
    case 7:
        practiceAiJump(gear, st);
        break;
    case 9:
        practiceAiRunAway(gear, st);
        break;
    case 8:
        practiceAiChase(gear, st);
        break;
    case 10:
        practiceAiBackDash(gear, st);
        break;
    case 11:
        practiceAiBattle(gear, st, 0);
        return;
    case 12:
        practiceAiBattle(gear, st, 1);
        return;
    case 13:
        practiceAiBattle(gear, st, 2);
        return;
    case 2:
        battlingSettings.m6_p2Com = 0;
        return;
    case 1:
        practiceAiGuard(gear, st);
        break;
    case 0:
    default:
        st->m4_timer = 1;
        break;
    }
    steerBattlingAiGear(gear, st->mA_turn, st->mC_speed);
}

s32 isBattlingGearHpAbove(BattlingGear* gear, s32 percent) {
    return gear->mBC_maxHp * percent / 255 < gear->mB4_hp;
}

s32 canBattlingGearUseEther(BattlingGear* gear, s32 arg1) {
    if (arg1 != 0) {
        return canBattlingGearAddGauge(gear, gear->mBE_gaugeCost);
    }
    return gear->mB6_gauge < 0x1000 - gear->mBE_gaugeCost;
}

s32 getBattlingEtherGaugeLimit(BattlingGear* gear, BattlingAiState* ai) {
    return 0x1000 - gear->mBE_gaugeCost;
}

s32 getBattlingAiGaugeRoom(BattlingGear* gear) {
    s32 v;

    v = gear->m15FC_ai->m24_gaugeLimit;
    if (v - 0x200 >= gear->mB6_gauge) {
        return 2;
    }
    return !(v < gear->mB6_gauge);
}

s32 shouldBattlingAiFireEther(BattlingGear* gear, s32 arg1) {
    BattlingAiState* ai;

    ai = gear->m15FC_ai;
    if ((rand() & 0xFF) < (ai->m10 * 320) >> 4) {
        if (!(canBattlingGearUseEther(gear, 0) != 0 || isBattlingGearHpAbove(gear, 0xC0) != 0 ||
              (rand() & 0xFF) < (ai->m1C * 192) >> 4 ||
              (isBattlingGearHpAbove(gear, 0x80) != 0 && gear->mD8_opponent->mB4_hp < gear->mB4_hp))) {
            return 0;
        }
    } else if ((rand() & 0xFF) >= (ai->m1C * 320) >> 4) {
        return 0;
    }
    if ((rand() & 0xFF) < ai->m18) {
        if (gear->mD8_opponent->mC4_moveState == 4) {
            return 0;
        }
        if (isBattlingGearHpAbove(gear->mD8_opponent, 0x20) != 0) {
            return 1;
        }
        if (gear->mD8_opponent->mB4_hp < gear->mB4_hp) {
            return 0;
        }
    }
    return 1;
}

s32 shouldBattlingAiBoost(BattlingGear* gear, s32 flag) {
    BattlingAiState* ai;

    ai = gear->m15FC_ai;
    if (flag != 0 && (rand() & 0xFF) < ai->m10 && battlingGearDistance < 0x600) {
        return 0;
    }
    if (getBattlingAiGaugeRoom(gear) != 0) {
        return 1;
    }
    return (rand() & 0xFF) < ai->m1C;
}

void rollBattlingAiDecisions(BattlingAiState* ai) {
    s32 b;
    s32 r;

    ai->m2C_b9 = (rand() & 0xFF) < ai->m10;
    b = 0;
    ai->m2C_b10 = (rand() & 0xFF) < ai->m14;
    ai->m2C_b12 = rand();
    ai->m2C_b11 = (rand() & 0xFF) < ai->m18;
    if ((rand() & 0xFF) < ai->m10) {
        r = rand();
        r %= 10;
        b = r < 3;
    }
    ai->m2C_b8 = b;
    ai->m2C_b0_7 = rand();
    ai->m30_hitsTaken = ai->m0_gear->m1668_hitsTaken;
}

void fireBattlingAiEther(BattlingGear* gear) {
    if ((gear->mD8_opponent->mD0_status.flags & 0x60000000) != 0x20000000 || (rand() & 3) == 0) {
        if (battlingRubberBandMode != 0) {
            tryBattlingGearBackDash(gear);
            pushBattlingGearInput(gear, 4);
        } else if ((gear->mD0_status.flags & 0x60000000) == 0x20000000) {
            pushBattlingGearInput(gear, 4);
        }
        pushBattlingGearInput(gear, 3);
    }
}

s32 isBattlingGearInWater(BattlingGear* gear) {
    VECTOR v;

    v = gear->m0_position;
    v.vx -= 0x3F80;
    v.vz -= 0x3F80;
    if (v.vx > 0 && v.vz > 0) {
        return 1;
    }
    return (getBattlingMapCell(&gear->m0_position) & 0x3000000) == 0x1000000;
}

s32 steerBattlingAiOutOfWater(BattlingGear* gear, BattlingAiState* st) {
    VECTOR v;

    v = gear->m0_position;
    v.vx -= 0x3F80;
    v.vz -= 0x3F80;
    if ((ratan2(v.vx, v.vz) & 0xFFF) > 0x200) {
        st->mA_turn = 0x800 - battlingGearAngle;
    } else {
        st->mA_turn = 0xC00 - battlingGearAngle;
    }
    st->mC_speed = 0xFF;
    return 0;
}

s32 avoidBattlingAiWater(BattlingGear* gear, BattlingAiState* st) {
    if (battlingEventFight == 0 || !(gear->mD0_status.flags & 0x8000000)) {
        return 0;
    }
    if (isBattlingGearInWater(gear) != 0) {
        steerBattlingAiOutOfWater(gear, st);
        return 1;
    }
    if (isBattlingGearInWater(gear->mD8_opponent) != 0) {
        if (gear->mD8_opponent->m8F4_nearestProjectileDist < 0x800) {
            gear->mD0_status.flags |= 0x8000;
            st->mE_boost = 1;
            st->mC_speed = 0xFF;
            st->mA_turn = 0x800;
            gear->m58_targetRotation = 0x800;
            return 1;
        }
        if (gear->mD8_opponent->m8F4_nearestProjectileDist > 0x1000) {
            gear->mD0_status.flags &= ~0x8000;
            st->mE_boost = 0;
            st->mC_speed = 0;
            gear->m58_targetRotation = 0x800;
            return 1;
        }
        return 1; /* what the PS1 leaves in v0: the distance test's 1 */
    }
    return 0; /* what the PS1 leaves in v0: isBattlingGearInWater's 0 */
}

void tryBattlingAiGuard(BattlingGear* gear, BattlingAiState* st) {
    if (st->mF_comLevel >= 2) {
        gear->mD0_status.flags |= 2;
        clearBattlingGearInput(gear);
    } else if (st->mF_comLevel != 0) {
        if (st->m2C_b0_7 & 1) {
            gear->mD0_status.flags |= 2;
            clearBattlingGearInput(gear);
        }
    } else if (st->m2C_b0_7 % 6 == 0) {
        gear->mD0_status.flags |= 2;
        clearBattlingGearInput(gear);
    }
}

void enterBattlingAiWait(BattlingGear* gear) {
    BattlingAiState* ai;

    ai = gear->m15FC_ai;
    ai->m20_waitStep = 0;
    ai->m8_state = 0;
    ai->m4_timer = (2 - ai->mF_comLevel) * 30 + 90;
    rollBattlingAiDecisions(ai);
}

void updateBattlingAiWait(BattlingGear* gear, BattlingAiState* st) {
    st->mC_speed = 0;
    st->mE_boost = 0;
    st->mA_turn = 0;
    if (avoidBattlingAiWater(gear, st) != 0) {
        return;
    }
    if (battlingGearDistance < 0x200) {
        if (st->m2C_b12) {
            enterBattlingAiMelee(gear);
        } else if (st->mF_comLevel != 0) {
            enterBattlingAiRetreat(gear, 1);
        }
        if (gear->mD8_opponent->mC5_action == 2) {
            tryBattlingAiGuard(gear, st);
        }
        if (gear->m1668_hitsTaken + 2 < st->m30_hitsTaken) {
            enterBattlingAiMelee(gear);
        }
    }
    if (gear->mD8_opponent->m8F4_nearestProjectileDist < 0x400) {
        enterBattlingAiRetreat(gear, 3);
        st->mE_boost = 1;
    }
    switch (st->m20_waitStep) {
    case 0:
        if (--st->m4_timer < 0) {
            st->m20_waitStep++;
        }
        if (gear->mD8_opponent->mC4_moveState != 0) {
            return;
        }
        if (!st->m2C_b12) {
            return;
        }
        if (shouldBattlingAiFireEther(gear, 0) != 0) {
            fireBattlingAiEther(gear);
        }
        enterBattlingAiApproach(gear, 0);
        break;
    case 1:
        if (canBattlingGearUseEther(gear, 0) != 0) {
            enterBattlingAiApproach(gear, 0);
        }
        break;
    }
}

void queueBattlingAiRandomAttack(BattlingGear* gear) {
    s32 r;
    s32 n;

    r = rand() % 10;
    n = 1;
    if (r >= 2) {
        n = 2;
    }
    if (r >= 5) {
        n++;
    }
    while (n != 0) {
        n--;
        pushBattlingGearInput(gear, (rand() & 1) + 1);
    }
}

void startBattlingAiAttack(BattlingGear* gear, BattlingAiState* st) {
    if (gear->mD8_opponent->mC4_moveState == 4) {
        if (st->m2C_b9 && !st->m2C_b11) {
            fireBattlingAiEther(gear);
            st->m4_timer = 3;
        }
    } else {
        queueBattlingAiRandomAttack(gear);
        st->m4_timer = (2 - st->mF_comLevel) * 20 + (s16)(rand() % 20 + 1);
    }
    rollBattlingAiDecisions(st);
}

s32 queueBattlingAiSpecialMove(BattlingGear* gear, BattlingAiState* st) {
    s32 n;
    s32 i;
    s32 j;

    n = gear->m911_partCount;
    if (n == 0) {
        return 1;
    }
    j = 0;
    n = rand() % n;
    for (i = 0; i < 14; i++, j++) {
        if (gear->m1600_def->mA_moves[i] != 0 && gear->m80_moves[i].m3_alive != 0) {
            if (n == 0) {
                if (battlingMoveInputs[j][0] != 0) {
                    pushBattlingGearInput(gear, battlingMoveInputs[j][0]);
                }
                if (battlingMoveInputs[j][1] != 0) {
                    pushBattlingGearInput(gear, battlingMoveInputs[j][1]);
                }
                if (battlingMoveInputs[j][2] != 0) {
                    pushBattlingGearInput(gear, battlingMoveInputs[j][2]);
                }
                break;
            }
            n--;
        }
    }
    st->m4_timer = (2 - st->mF_comLevel) * 20 + (s16)(rand() % 20 + 1);
    rollBattlingAiDecisions(st);
    return 0;
}

void enterBattlingAiMelee(BattlingGear* gear) {
    BattlingAiState* ai;

    ai = gear->m15FC_ai;
    ai->m8_state = 1;
    startBattlingAiAttack(gear, ai);
    if (ai->mF_comLevel >= 2) {
        ai->m9_repeatCount = rand() % 8 + 1;
    } else if (ai->mF_comLevel != 0) {
        ai->m9_repeatCount = rand() % 6 + 1;
    } else {
        ai->m9_repeatCount = rand() % 4 + 1;
    }
    ai->mC_speed = 0;
    ai->mE_boost = 0;
    rollBattlingAiDecisions(ai);
}

s32 tryBattlingAiSpecialOrEther(BattlingGear* gear, BattlingAiState* st) {
    if (gear->m911_partCount != 0) {
        if (!(rand() & 1)) {
            return 1;
        }
        if (!(rand() & 1) || shouldBattlingAiFireEther(gear, 0) == 0) {
            if (battlingGearDistance <= 0x1000) {
                queueBattlingAiSpecialMove(gear, st);
                return 0;
            }
            return 1;
        }
    } else if (shouldBattlingAiFireEther(gear, 0) == 0) {
        return 1;
    }
    fireBattlingAiEther(gear);
    return 0;
}

void updateBattlingAiMelee(BattlingGear* gear, BattlingAiState* st) {
    if (avoidBattlingAiWater(gear, st) != 0) {
        return;
    }
    if (gear->mC5_action != 2 && gear->mD8_opponent->mC5_action == 2) {
        tryBattlingAiGuard(gear, st);
    }
    if (gear->mD8_opponent->m8F4_nearestProjectileDist > 0x200 && gear->mD8_opponent->m8F4_nearestProjectileDist < 0x600 && st->mF_comLevel != 0) {
        tryBattlingAiGuard(gear, st);
    }
    if (--st->m4_timer > 0) {
        return;
    }
    if (battlingGearDistance > 0x300) {
        switch (rand() % 10) {
        case 0:
            if ((rand() & 0xFF) >= st->m14) {
                enterBattlingAiRetreat(gear, 0);
            }
            break;
        case 2:
            if (gear->m911_partCount != 0) {
                queueBattlingAiSpecialMove(gear, st);
            } else {
                enterBattlingAiWait(gear);
            }
            break;
        case 3:
        case 4:
        case 5:
            if (tryBattlingAiSpecialOrEther(gear, st) != 0) {
                enterBattlingAiWait(gear);
            }
            break;
        case 1:
            enterBattlingAiWait(gear);
            break;
        case 6:
        case 7:
        case 8:
        case 9:
            enterBattlingAiApproach(gear, 0);
            break;
        }
    }
    if (st->m9_repeatCount-- != 0) {
        startBattlingAiAttack(gear, st);
    } else {
        enterBattlingAiRetreat(gear, rand() & 1);
    }
}

void enterBattlingAiApproach(BattlingGear* gear, s32 arg1) {
    BattlingAiState* ai;

    ai = gear->m15FC_ai;
    ai->m8_state = 3;
    ai->m9_repeatCount = rand() % 4 + 1;
    ai->m4_timer = 0;
    rollBattlingAiDecisions(ai);
    ai->mE_boost = shouldBattlingAiBoost(gear, 1);
    ai->m2C_b16_23 = 0;
}

void updateBattlingAiApproach(BattlingGear* gear, BattlingAiState* st) {
    s32 r;

    if (isBattlingGearInWater(gear->mD8_opponent) != 0 && battlingEventFight != 0 && (gear->mD0_status.flags & 0x8000000)) {
        enterBattlingAiWait(gear);
        return;
    }
    if (BL_NEAREST_SHOT_MOVE(gear->mD8_opponent) == 1 && gear->mD8_opponent->m8F4_nearestProjectileDist < 0x500 && st->mF_comLevel != 0) {
        st->mE_boost = 1;
        st->mA_turn = st->m2C_b12 ? 0x400 : -0x400;
        if (st->mF_comLevel >= 2 && (rand() & 0xFF) < st->m14 && shouldBattlingAiFireEther(gear, 0) != 0 && st->m2C_b12) {
            if ((rand() & 3) == 0) {
                pushBattlingGearInput(gear, 4);
            }
            fireBattlingAiEther(gear);
            st->m2C_b16_23 = 0;
        }
        st->m2C_b16_23++;
    }
    if (battlingGearDistance < 0x180) {
        if ((rand() & 3) == 0) {
            pushBattlingGearInput(gear, 4);
        }
        enterBattlingAiMelee(gear);
    }
    if (getBattlingAiGaugeRoom(gear) == 0) {
        st->mE_boost = 0;
    }
    if (st->m4_timer < 0) {
        st->mA_turn = rand() % 0x600 - 0x300;
        r = rand();
        st->m4_timer = (st->m2C_b10 ? r % 120 : r % 100) + 10;
        st->mC_speed = 0xFF;
        if (st->m9_repeatCount != 0) {
            st->m9_repeatCount--;
        } else {
            if ((rand() & 3) == 0) {
                pushBattlingGearInput(gear, 4);
            }
            if (tryBattlingAiSpecialOrEther(gear, st) != 0) {
                enterBattlingAiWait(gear);
            }
            st->m9_repeatCount = rand() % 4 + 1;
        }
    }
    st->m4_timer--;
}

void enterBattlingAiRetreat(BattlingGear* gear, s32 arg1) {
    BattlingAiState* ai;

    ai = gear->m15FC_ai;
    ai->m8_state = 2;
    if (rand() % 3 == 0) {
        tryBattlingGearBackDash(gear);
    }
    ai->m28_retreatDistance = rand() % 0x600 + 0x100;
    ai->m4_timer = 0;
    ai->m9_repeatCount = rand() % 5 + 3;
    rollBattlingAiDecisions(ai);
    ai->mE_boost = shouldBattlingAiBoost(gear, 0);
    ai->m2C_b16_23 = 0;
}

void updateBattlingAiRetreat(BattlingGear* gear, BattlingAiState* st) {
    s32 r;

    if (avoidBattlingAiWater(gear, st) != 0) {
        return;
    }
    if (BL_NEAREST_SHOT_MOVE(gear->mD8_opponent) == 1 && gear->mD8_opponent->m8F4_nearestProjectileDist < 0x500 && st->mF_comLevel != 0) {
        st->mE_boost = 1;
        st->mA_turn = st->m2C_b12 ? 0x400 : -0x400;
        if (st->mF_comLevel >= 2 && shouldBattlingAiFireEther(gear, 0) != 0 && st->m2C_b12) {
            if ((rand() & 3) == 0) {
                pushBattlingGearInput(gear, 4);
            }
            fireBattlingAiEther(gear);
            st->m2C_b16_23 = 0;
        }
        st->m2C_b16_23++;
    }
    if (getBattlingAiGaugeRoom(gear) == 0) {
        st->mE_boost = 0;
    }
    if (battlingGearDistance > st->m28_retreatDistance || (battlingRubberBandMode != 0 && battlingGearDistance3D > 0x4B0)) {
        if (battlingRubberBandMode != 0) {
            pushBattlingGearInput(gear, 4);
        }
        queueBattlingAiSpecialMove(gear, st);
        enterBattlingAiWait(gear);
    }
    if (st->m4_timer < 0 || st->mC_speed == 0) {
        st->mA_turn = rand() % 0x600 + 0x500;
        r = rand();
        st->m4_timer = (st->m2C_b10 ? r % 40 : r % 60) + 10;
        st->mC_speed = 0xFF;
        if (st->m9_repeatCount != 0) {
            st->m9_repeatCount--;
        } else {
            if (shouldBattlingAiFireEther(gear, 0) != 0) {
                pushBattlingGearInput(gear, 4);
                fireBattlingAiEther(gear);
            }
            st->m9_repeatCount = rand() % 5 + 3;
        }
    }
    st->m4_timer--;
}

void loadBattlingAiPersonality(BattlingGear* gear) {
    BattlingGearDef* stats;
    BattlingAiState* ai;

    stats = gear->m1600_def;
    ai = gear->m15FC_ai;
    ai->m10 = stats->m6_ai[0];
    ai->m14 = stats->m6_ai[1];
    ai->m18 = stats->m6_ai[2];
    ai->m1C = stats->m6_ai[3];
}

void initBattlingAi(BattlingGear* gear) {
    BattlingAiState* ai;

    ai = &battlingGear1Ai;
    if (gear->mD0_status.flags & 0x8000000) {
        ai = &battlingGear2Ai;
    }
    gear->m15FC_ai = ai;
    ai->m0_gear = gear;
    ai->m4_timer = 0;
    ai->m6 = 0x10;
    ai->m7 = 0xA;
    ai->mA_turn = 0;
    ai->mC_speed = 0;
    ai->m8_state = 0;
    ai->m9_repeatCount = 0;
    gear->m48_speed = 0;
    ai->m24_gaugeLimit = getBattlingEtherGaugeLimit(gear, ai);
    ai->mF_comLevel = battlingSettings.m0_comLevel;
    loadBattlingAiPersonality(gear);
    switch (rand() % 3) {
    case 0:
        enterBattlingAiWait(gear);
        break;
    case 1:
        enterBattlingAiMelee(gear);
        break;
    case 2:
        enterBattlingAiRetreat(gear, 0);
        break;
    case 3:
        enterBattlingAiApproach(gear, 0);
        break;
    }
    clearBattlingGearInput(gear);
}

void updateBattlingAi(BattlingGear* gear, BattlingAiState* unused) {
    BattlingAiState* ai;

    if (!(gear->mD0_status.flags & 0x40)) {
        return;
    }
    ai = gear->m15FC_ai;
    ai->mF_comLevel = battlingSettings.m0_comLevel;
    switch (ai->m8_state) {
    case 0:
        updateBattlingAiWait(gear, ai);
        break;
    case 1:
        updateBattlingAiMelee(gear, ai);
        break;
    case 2:
        updateBattlingAiRetreat(gear, ai);
        break;
    case 3:
        updateBattlingAiApproach(gear, ai);
        break;
    }
    if (ai->mE_boost) {
        gear->mD0_status.flags |= 0x8000;
    }
    if (ai->m2C_b13) {
        gear->mD0_status.flags |= 2;
    }
    steerBattlingAiGear(gear, ai->mA_turn, ai->mC_speed);
}
