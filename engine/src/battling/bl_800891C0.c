/* Battling overlay, 0x800891C0-0x8008BB00: file loading, the matrix setup,
 * math helpers and the scene graph. A new object's .rodata starts at
 * 0x800706E8 (initBattlingAnim's jump tables) after linker fill; its text starts
 * after battlingMain and by initBattlingAnim: cut at the first possible place. */
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
#include "pc_convert.h"

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
void initBattlingAnim(BattlingAnimRecord* rec, BattlingAnim* anim, BattlingNode* root);
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

/* Load a whole file into a new heap buffer. */
void* loadBattlingFile(s32 arg0) {
    void* buf = allocateHeap(getFileSize(arg0), 1);
    readFile(arg0, buf, 0, 0);
    return buf;
}

/* Copy the base matrix to two globals and scale the second one's diagonal
 * by the screen aspect (320x240). */
void setupBattlingAspectMatrix(s32 arg0, s32 arg1) {
    s16 a = ((arg0 << 12) / 320) * arg1 / arg0;
    s16 b = ((arg1 << 12) / 240) * arg1 / arg0;
    D_8009A2D8 = battlingIdentityMatrix;
    battlingAspectMatrix = battlingIdentityMatrix;
    battlingAspectMatrix.m[0][0] = a;
    battlingAspectMatrix.m[1][1] = b;
}

void initBattlingDispEnvs(s32 w, s32 h) {
    if (h > 0x100) {
        SetDefDispEnv(&battlingDisplayBuffers[0].m5C_dispEnv, 0, 0, w, h);
        SetDefDispEnv(&battlingDisplayBuffers[1].m5C_dispEnv, 0, 0, w, h);
        battlingDisplayBuffers[0].m5C_dispEnv.isinter = battlingDisplayBuffers[1].m5C_dispEnv.isinter = 1;
        battlingDisplayBuffers[0].m5C_dispEnv.screen.x = 0;
        battlingDisplayBuffers[0].m5C_dispEnv.screen.y = 0x10;
        battlingDisplayBuffers[0].m5C_dispEnv.screen.w = 0x100;
        battlingDisplayBuffers[0].m5C_dispEnv.screen.h = 0xD4;
        battlingDisplayBuffers[1].m5C_dispEnv.screen.x = 0;
        battlingDisplayBuffers[1].m5C_dispEnv.screen.y = 0x10;
        battlingDisplayBuffers[1].m5C_dispEnv.screen.w = 0x100;
        battlingDisplayBuffers[1].m5C_dispEnv.screen.h = 0xD4;
    } else {
        SetDefDispEnv(&battlingDisplayBuffers[0].m5C_dispEnv, 0, 0x100, w, h);
        SetDefDispEnv(&battlingDisplayBuffers[1].m5C_dispEnv, 0, 0, w, h);
        battlingDisplayBuffers[0].m5C_dispEnv.isinter = battlingDisplayBuffers[1].m5C_dispEnv.isinter = 0;
        battlingDisplayBuffers[0].m5C_dispEnv.screen.x = 0;
        battlingDisplayBuffers[0].m5C_dispEnv.screen.y = 0xA;
        battlingDisplayBuffers[0].m5C_dispEnv.screen.w = 0x100;
        battlingDisplayBuffers[0].m5C_dispEnv.screen.h = h;
        battlingDisplayBuffers[1].m5C_dispEnv.screen.x = 0;
        battlingDisplayBuffers[1].m5C_dispEnv.screen.y = 0xA;
        battlingDisplayBuffers[1].m5C_dispEnv.screen.w = 0x100;
        battlingDisplayBuffers[1].m5C_dispEnv.screen.h = h;
    }
    battlingScreenWidth = w;
    battlingScreenHeight = h;
    setupBattlingAspectMatrix(battlingScreenWidth, battlingScreenHeight);
    setlen(&battlingDisplayBuffers[0].mE8_clearTile, 3);
    *(u32*)&battlingDisplayBuffers[0].mE8_clearTile.r0 = 0x60000000;
    battlingDisplayBuffers[0].mE8_clearTile.w = w;
    battlingDisplayBuffers[0].mE8_clearTile.x0 = 0;
    battlingDisplayBuffers[0].mE8_clearTile.y0 = 0;
    battlingDisplayBuffers[0].mE8_clearTile.h = h;
    battlingDisplayBuffers[1].mE8_clearTile = battlingDisplayBuffers[0].mE8_clearTile;
}

void initBattlingDrawEnvs(s32 w, s32 h) {
    SetGeomOffset(w / 2, h / 2);
    SetGeomScreen(0x180);
    initFieldDrawEnvsSub0(w, h);
    if (h > 0x100) {
        SetDefDrawEnv(&battlingDisplayBuffers[0].m0_drawEnv, 0, 0, w, h);
        SetDefDrawEnv(&battlingDisplayBuffers[1].m0_drawEnv, 0, 0, w, h);
        battlingDisplayBuffers[0].m0_drawEnv.dfe = battlingDisplayBuffers[1].m0_drawEnv.dfe = 0;
    } else {
        SetDefDrawEnv(&battlingDisplayBuffers[0].m0_drawEnv, 0, 0, w, h);
        SetDefDrawEnv(&battlingDisplayBuffers[1].m0_drawEnv, 0, 0x100, w, h);
    }
    battlingDisplayBuffers[0].m0_drawEnv.dtd = battlingDisplayBuffers[1].m0_drawEnv.dtd = 1;
    battlingDisplayBuffers[0].m0_drawEnv.isbg = battlingDisplayBuffers[1].m0_drawEnv.isbg = 0;
    battlingDisplayBuffers[0].m0_drawEnv.tpage = battlingDisplayBuffers[1].m0_drawEnv.tpage = GetTPage(0, 2, 0x280, 0);
    SetDrawEnv(&battlingDisplayBuffers[0].m0_drawEnv.dr_env, &battlingDisplayBuffers[0].m0_drawEnv);
    SetDrawEnv(&battlingDisplayBuffers[1].m0_drawEnv.dr_env, &battlingDisplayBuffers[1].m0_drawEnv);
    SetDrawArea(&battlingDisplayBuffers[0].mD0_drawArea, &battlingDisplayBuffers[0].m0_drawEnv.clip);
    SetDrawArea(&battlingDisplayBuffers[1].mD0_drawArea, &battlingDisplayBuffers[1].m0_drawEnv.clip);
    SetDrawOffset(&battlingDisplayBuffers[0].mDC_drawOffset, battlingDisplayBuffers[0].m0_drawEnv.ofs);
    SetDrawOffset(&battlingDisplayBuffers[1].mDC_drawOffset, battlingDisplayBuffers[1].m0_drawEnv.ofs);
}

void setupBattlingScreenGeometry(s32 w, s32 h) {
    SetGeomOffset(w / 2, h / 2);
    SetGeomScreen((w << 8) / w);
    initFieldDrawEnvsSub0(w, h);
    setupBattlingAspectMatrix(w, h);
}

void resetBattlingDrawEnvs(void) {
    initBattlingDrawEnvs(battlingScreenWidth, battlingScreenHeight);
}

void initBattlingDisplayAndDrawEnvs(s32 w, s32 h) {
    initBattlingDispEnvs(w, h);
    initBattlingDrawEnvs(w, h);
}

void setBattlingLayerArea(BattlingLayer* layer, s32 x, s32 y, s32 w, s32 h, s32 yOfs) {
    RECT r;
    s16 ofs[2];

    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    SetDrawArea(&layer->m18_drawArea[0], &r);
    r.y = y + yOfs;
    SetDrawArea(&layer->m18_drawArea[1], &r);
    ofs[0] = x;
    ofs[1] = y;
    SetDrawOffset(&layer->m30_drawOffset[0], ofs);
    ofs[1] = y + yOfs;
    SetDrawOffset(&layer->m30_drawOffset[1], ofs);
    setlen(&layer->m48_clearTile[0], 3);
    *(u32*)&layer->m48_clearTile[0].r0 = 0x60000000;
    layer->m48_clearTile[0].x0 = 0;
    layer->m48_clearTile[0].y0 = 0;
    layer->m48_clearTile[0].w = w;
    layer->m48_clearTile[0].h = h;
    layer->m48_clearTile[1] = layer->m48_clearTile[0];
    layer->m16_flags |= 0xC;
}

void computeBattlingLookAtMatrix(MATRIX* m, SVECTOR* eye, SVECTOR* at, SVECTOR* up) {
    battlingLookAtTemp.vx = at->vx - eye->vx;
    battlingLookAtTemp.vy = at->vy - eye->vy;
    battlingLookAtTemp.vz = at->vz - eye->vz;
    battlingLookAtUp.vx = up->vx;
    battlingLookAtUp.vy = up->vy;
    battlingLookAtUp.vz = up->vz;
    VectorNormal(&battlingLookAtTemp, &battlingLookAtForward);
    OuterProduct12(&battlingLookAtUp, &battlingLookAtForward, &battlingLookAtTemp);
    VectorNormal(&battlingLookAtTemp, &battlingLookAtRight);
    OuterProduct12(&battlingLookAtForward, &battlingLookAtRight, &battlingLookAtTemp);
    VectorNormal(&battlingLookAtTemp, &battlingLookAtUp);
    m->m[0][0] = battlingLookAtRight.vx;
    m->m[0][1] = battlingLookAtRight.vy;
    m->m[0][2] = battlingLookAtRight.vz;
    m->m[1][0] = battlingLookAtUp.vx;
    m->m[1][1] = battlingLookAtUp.vy;
    m->m[1][2] = battlingLookAtUp.vz;
    m->m[2][0] = battlingLookAtForward.vx;
    m->m[2][1] = battlingLookAtForward.vy;
    m->m[2][2] = battlingLookAtForward.vz;
    ApplyMatrix(m, eye, &battlingLookAtTemp);
    MulMatrix2(&battlingAspectMatrix, m);
    m->t[0] = -battlingLookAtTemp.vx;
    m->t[1] = -battlingLookAtTemp.vy;
    m->t[2] = -battlingLookAtTemp.vz;
}

void setBattlingSceneCamera(BattlingScene* scene, VECTOR* eye, VECTOR* at) {
    SVECTOR up;
    SVECTOR dir;
    SVECTOR origin;

    up.vy = 0x1000;
    up.vz = 0;
    up.vx = 0;
    battlingViewOrigin = *at;
    dir.vx = eye->vx - at->vx;
    dir.vy = eye->vy - at->vy;
    dir.vz = eye->vz - at->vz;
    origin.vz = 0;
    origin.vx = 0;
    origin.vy = 0;
    computeBattlingLookAtMatrix(&scene->m4_root->mC_worldMatrix, &dir, &origin, &up);
}

BattlingNode* initBattlingNode(BattlingNode* node) {
    node->m0_type = 0;
    node->m8C_parent = NULL;
    node->m94_child = NULL;
    node->m90_next = NULL;
    node->m8_callback = NULL;
    node->m4_data = NULL;
    node->m34_translation.vz = 0;
    node->m34_translation.vy = 0;
    node->m34_translation.vx = 0;
    node->m44_rotation.vz = 0;
    node->m44_rotation.vy = 0;
    node->m44_rotation.vx = 0;
    node->m2C_position.vz = 0;
    node->m2C_position.vy = 0;
    node->m2C_position.vx = 0;
    node->m6C_baseMatrix = battlingIdentityMatrix;
    node->m4C_localMatrix = node->m6C_baseMatrix;
    node->mC_worldMatrix = node->m4C_localMatrix;
    return node;
}

BattlingNode* createBattlingNode(void) {
    traceNextAlloc(8);
    return initBattlingNode(allocateHeap((s32)sizeof(BattlingNode), 0));
}

void addBattlingNodeChild(BattlingNode* parent, BattlingNode* child) {
    BattlingNode* p;

    child->m8C_parent = parent;
    if (parent->m94_child == NULL) {
        parent->m94_child = child;
    } else {
        p = parent->m94_child;
        while (p->m90_next != NULL) {
            p = p->m90_next;
        }
        p->m90_next = child;
    }
}

void detachBattlingNode(BattlingNode* node) {
    BattlingNode* parent;
    BattlingNode* p;

    if (node != NULL && (parent = node->m8C_parent) != NULL) {
        node->m8C_parent = NULL;
        if (parent->m94_child != NULL) {
            if (parent->m94_child == node) {
                parent->m94_child = node->m90_next;
            } else {
                p = parent->m94_child;
                while (p->m90_next != node) {
                    p = p->m90_next;
                }
                p->m90_next = node->m90_next;
            }
            node->m90_next = NULL;
        }
    }
}

void freeBattlingNodeTree(BattlingNode* node) {
    if (node == NULL) {
        return;
    }
    freeBattlingNodeTree(node->m94_child);
    freeBattlingNodeTree(node->m90_next);
    switch (node->m0_type) {
    case 1:
        freeBattlingModel(node->m4_data);
        break;
    case 2:
        freeBattlingSkeleton(node->m4_data);
        break;
    case 5:
        freeBattlingInstance(node->m4_data);
        break;
    }
    if (node->m4_data != NULL) {
        freeHeap(node->m4_data);
    }
    freeHeap(node);
}

void setBattlingNodeModel(BattlingNode* node, void* data) {
    node->m4_data = data;
    node->m0_type = 1;
}

void func_80089E3C(BattlingNode* node) {
    node->m0_type = 3;
}

void func_80089E48(BattlingNode* node) {
    node->m0_type = 4;
}

void setBattlingNodeSkeleton(BattlingNode* node, void* data) {
    node->m4_data = data;
    node->m0_type = 2;
}

void setBattlingNodeLight(BattlingNode* node, void* data) {
    node->m4_data = data;
    node->m0_type = 6;
}

BattlingSkeleton* createBattlingSkeleton(void) {
    BattlingSkeleton* skel;

    traceNextAlloc(4);
    skel = allocateHeap((s32)sizeof(BattlingSkeleton), 0);
    skel->m10_scale[2] = 0x1000;
    skel->m10_scale[1] = 0x1000;
    skel->m10_scale[0] = 0x1000;
    skel->m4_bones = NULL;
    skel->m8_anims = NULL;
    return skel;
}

void freeBattlingSkeleton(BattlingSkeleton* skel) {
    s32 i;

    if (skel->m4_bones != NULL) {
        freeHeap(skel->m4_bones);
    }
    if (skel->m8_anims != NULL) {
        if (D_80091C2C == 0) {
            i = skel->m2_animCount;
            while (--i != -1) {
                if (skel->m8_anims[i].m0_data != NULL) {
                    freeHeap(skel->m8_anims[i].m4_channels);
                }
            }
        }
        freeHeap(skel->m8_anims);
    }
}

BattlingModel* initBattlingModel(BattlingModel* model) {
    model->m4_renderMode = 1;
    model->m0_flags = 0;
    model->m8_prims[0] = NULL;
    model->m8_prims[1] = NULL;
    model->m10_dynamicVertices = NULL;
    model->m14_modelBlock = NULL;
    model->m1C = 0;
    model->m1A_b = 0x40;
    model->m19_g = 0x40;
    model->m18_r = 0x40;
    return model;
}

BattlingModel* createBattlingModel(void) {
    traceNextAlloc(1);
    return initBattlingModel(allocateHeap((s32)sizeof(BattlingModel), 0));
}

void freeBattlingModel(BattlingModel* model) {
    if (model->m8_prims[0] != NULL) {
        freeHeapDelayed(model->m8_prims[0], 2);
    }
    freeModelBlock(model->m14_modelBlock);
}

void uploadBattlingTexture(s32* data) {
    if (battlingTexturePosX > 0) {
        uploadTextureToVram(data, 1, battlingTexturePosX, battlingTexturePosY, 1, battlingClutPosX, battlingClutPosY);
    } else {
        uploadTextureToVram(data, 0, 0, 0, 0, 0, 0);
    }
}

void setBattlingModelColor(BattlingNode* node, u8 r, u8 g, u8 b) {
    ((BattlingModel*)node->m4_data)->m18_r = r;
    ((BattlingModel*)node->m4_data)->m19_g = g;
    ((BattlingModel*)node->m4_data)->m1A_b = b;
    ((BattlingModel*)node->m4_data)->m0_flags |= 0x10;
}

void clearBattlingModelColor(BattlingNode* node) {
    ((BattlingModel*)node->m4_data)->m0_flags &= ~0x10;
}

void setBattlingTexturePos(s16 x, s16 y) {
    battlingTexturePosX = x;
    battlingTexturePosY = y;
}

void setBattlingClutPos(s16 x, s16 y) {
    battlingClutPosX = x;
    battlingClutPosY = y;
}

void setBattlingTextureClutPos(s16 x, s16 y, s16 clutX, s16 clutY) {
    battlingTexturePosX = x;
    battlingTexturePosY = y;
    battlingClutPosX = clutX;
    battlingClutPosY = clutY;
}

void clearBattlingTextureOverride(void) {
    battlingClutPosX = -1;
    battlingTexturePosX = -1;
}

void setupBattlingModelBlock(BattlingModel* model, ModelBlock* block) {
    model->m14_modelBlock = block;
    model->m10_dynamicVertices = initModelDynamicVertices(block, 1);
    initModel1(model->m14_modelBlock, &model->m8_prims[0], &model->m8_prims[1]);
    if (battlingTexturePosX >= 0) {
        setupOverrideTPageFull(GetTPage(0, 1, battlingTexturePosX, battlingTexturePosY));
    }
    if (battlingClutPosX >= 0) {
        setupOverrideClut(battlingClutPosX, battlingClutPosY);
    }
    initModel2(model->m14_modelBlock, model->m8_prims[0], 2);
    copyBattlingWords(model->m8_prims[1], model->m8_prims[0], model->m14_modelBlock->m34_primBufferSize);
    model->m0_flags |= 2;
}

BattlingLight* createBattlingLight(void) {
    BattlingLight* light;

    traceNextAlloc(0xB);
    light = allocateHeap(0x14, 0);
    light->m8_z = 0x10;
    light->m4_y = 0x10;
    light->m0_x = 0x10;
    light->m10_b = 0;
    light->mE_g = 0;
    light->mC_r = 0;
    return light;
}

void freeBattlingLight(void* p) {
    freeHeap(p);
}

BattlingLayer* createBattlingLayer(u16 otSize) {
    BattlingLayer* layer;
    s32 i;

    traceNextAlloc(0xC);
    layer = allocateHeap((s32)sizeof(BattlingLayer), 0);
    traceNextAlloc(0xC);
    layer->m4_ot[0] = allocateHeap(otSize * (2 * (s32)sizeof(OTag)), 0);
    layer->m4_ot[1] = layer->m4_ot[0] + otSize;
    layer->m16_flags = 1;
    layer->m17_otShift = 0xE;
    layer->m0 = 0;
    layer->m14_otSize = otSize;
    layer->mC_otLast[0] = &layer->m4_ot[0][otSize - 1];
    layer->mC_otLast[1] = &layer->m4_ot[1][otSize - 1];
    i = 1;
    while (i != otSize) {
        i <<= 1;
        if (i > 0x4000) {
            layer->m17_otShift = 0xE;
            break;
        }
        layer->m17_otShift--;
    }
    return layer;
}

void func_8008A3A0(void) {
}

void freeBattlingLayer(BattlingLayer* layer) {
    freeHeapDelayed(layer->m4_ot[0], 3);
    freeHeap(layer);
}

BattlingScene* createBattlingScene(BattlingLayer* layer) {
    BattlingScene* scene;
    BattlingLight* light;

    traceNextAlloc(9);
    scene = allocateHeap((s32)sizeof(BattlingScene), 0);
    scene->m0 = 0;
    scene->m4_root = &scene->m14_rootNode;
    scene->m8_lights[0] = &scene->mB0_lightNodes[0];
    scene->m8_lights[1] = &scene->mB0_lightNodes[1];
    scene->m8_lights[2] = &scene->mB0_lightNodes[2];
    initBattlingNode(&scene->m14_rootNode);
    initBattlingNode(&scene->mB0_lightNodes[0]);
    initBattlingNode(&scene->mB0_lightNodes[1]);
    initBattlingNode(&scene->mB0_lightNodes[2]);
    setBattlingNodeLight(scene->m8_lights[0], createBattlingLight());
    setBattlingNodeLight(scene->m8_lights[1], createBattlingLight());
    setBattlingNodeLight(scene->m8_lights[2], createBattlingLight());
    scene->m8_lights[0]->m34_translation.vy = scene->m8_lights[1]->m34_translation.vy =
        scene->m8_lights[2]->m34_translation.vy = -2;
    scene->m8_lights[0]->m34_translation.vx = 0;
    scene->m8_lights[0]->m34_translation.vz = 1;
    scene->m8_lights[1]->m34_translation.vx = -1;
    scene->m8_lights[1]->m34_translation.vz = -1;
    scene->m8_lights[2]->m34_translation.vx = 1;
    scene->m8_lights[2]->m34_translation.vz = -1;
    light = scene->m8_lights[0]->m4_data;
    light->mC_r = light->mE_g = light->m10_b = 0x800;
    ((BattlingLight*)scene->m8_lights[1]->m4_data)->mC_r = ((BattlingLight*)scene->m8_lights[1]->m4_data)->mE_g =
        ((BattlingLight*)scene->m8_lights[1]->m4_data)->m10_b = 0;
    *(BattlingLight*)scene->m8_lights[2]->m4_data = *(BattlingLight*)scene->m8_lights[1]->m4_data;
    scene->m284_layer = layer;
    scene->m288_r = scene->m289_g = scene->m28A_b = 0;
    scene->m288_r = scene->m289_g = scene->m28A_b = 0x10;
    applyBattlingLights(scene->m8_lights);
    return scene;
}

void freeBattlingScene(BattlingScene* scene) {
    freeHeap(scene->mB0_lightNodes[0].m4_data);
    freeHeap(scene->mB0_lightNodes[1].m4_data);
    freeHeap(scene->mB0_lightNodes[2].m4_data);
    freeBattlingLayer(scene->m284_layer);
    freeHeap(scene);
}

void enableBattlingModelBackColor(void) {
    battlingModelBackColorEnabled = 1;
}

void disableBattlingModelBackColor(void) {
    battlingModelBackColorEnabled = 0;
}

void renderBattlingModel(BattlingModel* model) {
    objectClippingMask = 0;
    if (battlingModelBackColorEnabled != 0) {
        if (model->m0_flags & 0x10) {
            gte_SetBackColor(model->m18_r, model->m19_g, model->m1A_b);
        } else {
            gte_SetBackColor(0x40, 0x40, 0x40);
        }
    }
    submitModelForRendering(model->m14_modelBlock, model->m8_prims[battlingBufferIndex], battlingLayerOt, model->m4_renderMode);
}

void setBattlingShadowColor(CVECTOR* c) {
    CVECTOR* cur;

    cur = &battlingShadowColors[battlingBufferIndex];
    if (c->r == cur->r && c->g == cur->g && c->b == cur->b) {
        battlingShadowColorChanged = 0;
    } else {
        battlingShadowColorChanged = 1;
        *cur = *c;
        cur->cd = 0x20;
    }
}

void applyBattlingShadowColor(BattlingNode* node) {
    BattlingFlatModel* fm;
    POLY_F3* prim;
    u32 color;
    s32 i;

    fm = ((BattlingInstance*)node->m4_data)->mC_flatModel;
    i = fm->m2_numPrims;
    prim = (POLY_F3*)fm->m10_prims[battlingBufferIndex];
    color = *(u32*)&battlingShadowColors[battlingBufferIndex];
    while (--i != -1) {
        *(u32*)&prim->r0 = color;
        prim++;
    }
}

void renderBattlingNodeTree(BattlingNode* node) {
    BattlingInstance* inst;
    BattlingFlatModel* fm;

    if (node->m8_callback != NULL) {
        node->m8_callback();
    }
    switch (node->m0_type) {
    case 2:
        createRotationMatrix(&node->m44_rotation, &node->mC_worldMatrix);
        if (D_8009289C != 0) {
            MulMatrix0(&node->m8C_parent->mC_worldMatrix, &node->mC_worldMatrix, &node->m6C_baseMatrix);
        } else {
            node->m6C_baseMatrix = node->mC_worldMatrix;
        }
        scaleBattlingMatrixBySVector(&node->mC_worldMatrix, ((BattlingSkeleton*)node->m4_data)->m10_scale);
        node->m4C_localMatrix = node->mC_worldMatrix;
        node->m4C_localMatrix.t[2] = 0;
        node->m4C_localMatrix.t[1] = 0;
        node->m4C_localMatrix.t[0] = 0;
        node->mC_worldMatrix.t[0] = node->m34_translation.vx - battlingViewOrigin.vx;
        node->mC_worldMatrix.t[1] = node->m34_translation.vy - battlingViewOrigin.vy;
        node->mC_worldMatrix.t[2] = node->m34_translation.vz - battlingViewOrigin.vz;
        CompMatrix(&node->m8C_parent->mC_worldMatrix, &node->mC_worldMatrix, &node->mC_worldMatrix);
        break;
    case 0:
    case 1:
        if (node->m8C_parent != NULL) {
            node->m34_translation.vx = node->m2C_position.vx;
            node->m34_translation.vy = node->m2C_position.vy;
            node->m34_translation.vz = node->m2C_position.vz;
            createRotationMatrix(&node->m44_rotation, &node->mC_worldMatrix);
            TransMatrix(&node->mC_worldMatrix, &node->m34_translation);
            MulMatrix0(&node->m8C_parent->m6C_baseMatrix, &node->mC_worldMatrix, &node->m6C_baseMatrix);
            CompMatrix(&node->m8C_parent->m4C_localMatrix, &node->mC_worldMatrix, &node->m4C_localMatrix);
            CompMatrix(&node->m8C_parent->mC_worldMatrix, &node->mC_worldMatrix, &node->mC_worldMatrix);
        }
        if (node->m0_type == 1 && !(((BattlingModel*)node->m4_data)->m0_flags & 1)) {
            setupLightMatrix(&node->m6C_baseMatrix);
            gte_SetRotMatrix(&node->mC_worldMatrix);
            gte_SetTransMatrix(&node->mC_worldMatrix);
            renderBattlingModel(node->m4_data);
        }
        break;
    case 5:
        inst = node->m4_data;
        if (inst->m0_type == 1) {
            node->mC_worldMatrix = inst->m4_source->m4C_localMatrix;
            gte_SetRotMatrix(&node->mC_worldMatrix);
            gte_SetTransMatrix(&node->mC_worldMatrix);
            if (battlingShadowColorChanged != 0) {
                applyBattlingShadowColor(node);
            }
            fm = ((BattlingInstance*)node->m4_data)->mC_flatModel;
            projectBattlingShadow(fm->mC_block, fm->m8_screenVertices);
        }
        break;
    }
    if (node->m94_child != NULL) {
        renderBattlingNodeTree(node->m94_child);
    }
    if (node->m90_next != NULL) {
        renderBattlingNodeTree(node->m90_next);
    }
}

void applyBattlingLights(BattlingNode** lights) {
    MulRotationMatrix(0, lights[0]->m4_data);
    MulRotationMatrix(1, lights[1]->m4_data);
    MulRotationMatrix(2, lights[2]->m4_data);
}

void beginBattlingLayer(BattlingLayer* layer) {
    ClearOTagR(layer->m4_ot[battlingBufferIndex], layer->m14_otSize);
    battlingLayerOt = layer->m4_ot[battlingBufferIndex];
    gDepthDivider = layer->m17_otShift;
}

void setBattlingOtSkipLayer(BattlingLayer* layer) {
    battlingOtSkipLayer = layer;
}

void startBattlingFrameTimer(void) {
    battlingFrameStartTime = GetRCnt(0xF2000001);
}

void skipEmptyBattlingOtEntries(s32 budget) {
    BattlingLayer* layer;
    s32 start;
    s32 limit;
    s32 i;
    s32 t;
    OTag* p;
    OTag* q;

    layer = battlingOtSkipLayer;
    if (layer == NULL) {
        return;
    }
    start = battlingFrameStartTime;
    battlingOtSkipLayer = NULL;
    if (budget != 0) {
        limit = budget * 0xF0;
    } else {
        limit = 0xC0;
    }
    for (i = layer->m14_otSize - 1; i >= 4; i--) {
        t = GetRCnt(0xF2000001) - start;
        if (t < 0) {
            t += 0x10000;
        }
        if (t > limit) {
            break;
        }
        /* An OT entry's length is 0, so its tag is the bare link (a kuseg address on the PS1). */
        p = (OTag*)layer->m4_ot[battlingBufferIndex][i];
        q = (OTag*)OT_NEXT(*p);
        if (((P_TAG*)q)->len == 0) {
            while (i >= 5) {
                q = (OTag*)OT_NEXT(*q);
                i--;
                if (((P_TAG*)q)->len != 0) {
                    break;
                }
            }
            *p = (*p & ~OT_LINK_MASK) | OT_LINK(q);
        }
    }
}

void addBattlingLayerToOt(BattlingLayer* layer) {
    setBattlingOtSkipLayer(layer);
    AddPrims(battlingCurrentOt, layer->mC_otLast[battlingBufferIndex], layer->m4_ot[battlingBufferIndex]);
    if (!(layer->m16_flags & 4)) {
        SetDrawArea(&layer->m18_drawArea[battlingBufferIndex], &battlingDisplayBuffer->m0_drawEnv.clip);
    }
    if (!(layer->m16_flags & 8)) {
        SetDrawOffset(&layer->m30_drawOffset[battlingBufferIndex], battlingDisplayBuffer->m0_drawEnv.ofs);
    }
    if (layer->m16_flags & 0x10) {
        AddPrim(battlingCurrentOt, &layer->m48_clearTile[battlingBufferIndex]);
    }
    AddPrim(battlingCurrentOt, &layer->m30_drawOffset[battlingBufferIndex]);
    AddPrim(battlingCurrentOt, &layer->m18_drawArea[battlingBufferIndex]);
}

BattlingDataFile* relocateBattlingDataFile(BattlingDataFile* file) {
    s32 delta;
    u32 i;

    if (file->mC_texture != 0) {
        uploadBattlingTexture(BL_FILE(file, mC_texture));
    }
    (void)delta;
    (void)i;
    return file;
}

BattlingDataFile* loadBattlingDataFile(s32 fileIndex) {
    BattlingDataFile* file;

    traceNextAlloc(0xA);
    file = allocateHeap(getFileSize(fileIndex), 0);
    readFile(fileIndex, file, 0, 0);
    waitReadCompletion(0);
    return relocateBattlingDataFile(file);
}

void resetBattlingAnim(BattlingAnim* anim) {
    BattlingAnimTrack* track;
    s32 i;
    s32 unused[1];

    anim->m10 = 0;
    anim->m12_frame = 0;
    track = anim->m8_tracks;
    for (i = 0; i < anim->m0_data->m6_trackCount; i++) {
        track->mC_wait = 0;
        track->mE_value = 0;
        track->m4_keys = track->m0_keysStart;
        track->m10_delta = 0;
        track++;
    }
}

/* The incoming pointer is the animation header and, from the records on, the
 * record cursor (one variable: retail keeps both in t1, copying the header to
 * s0 before the first call).
 * The NULL check never survives: its store is dead (flow deletes it, jump2
 * then the empty branch), but while flow runs it ends the first basic block
 * after the parameter copies, so combine cannot fold `rec = a0` into the
 * header copy; the copy then lands after `a0 = 0x10`, ahead of the store.
 * The do { } while (0) keeps base's copy before the m4 store (loop notes are
 * a scheduling barrier). */
void initBattlingAnim(BattlingAnimRecord* rec, BattlingAnim* anim, BattlingNode* root) {
    BattlingNode** bones;
    BattlingAnimData* data;
    BattlingAnimChannel* ch;
    BattlingAnimTrack* track;
    BattlingNode* node;
    sptr base;
    u32 i;

    if (rec == NULL) {
        data = NULL;
    }
    bones = ((BattlingSkeleton*)root->m4_data)->m4_bones;
    data = (BattlingAnimData*)rec;
    anim->m0_data = data;
    base = (sptr)data;
    traceNextAlloc(0x10);
    ch = allocateHeap(data->m4_channelCount * (s32)sizeof(BattlingAnimChannel) + data->m6_trackCount * (s32)sizeof(BattlingAnimTrack), 0);
    do {
        anim->m4_channels = ch;
        rec = data->m8_records;
        track = (BattlingAnimTrack*)(ch + data->m4_channelCount);
        anim->m8_tracks = track;
        for (i = 0; i < data->m4_channelCount; i++) {
            node = bones[rec->m1_bone];
            ch->m4_value = rec->m2_value;
            switch (rec->m0_type & 0x7F) {
            case 3:
                ch->m0_target = &node->m44_rotation.vx;
                ch->m6_isAngle = 1;
                break;
            case 4:
                ch->m0_target = &node->m44_rotation.vy;
                ch->m6_isAngle = 1;
                break;
            case 5:
                ch->m0_target = &node->m44_rotation.vz;
                ch->m6_isAngle = 1;
                break;
            case 6:
                ch->m0_target = &node->m2C_position.vx;
                ch->m6_isAngle = 0;
                break;
            case 7:
                ch->m0_target = &node->m2C_position.vy;
                ch->m6_isAngle = 0;
                break;
            case 8:
                ch->m0_target = &node->m2C_position.vz;
                ch->m6_isAngle = 0;
                break;
            }
            ch++;
            rec++;
        }
        for (i = 0; i < data->m6_trackCount; i++) {
            node = bones[rec->m1_bone];
            track->m4_keys = (u8*)(rec->m2_value + base);
            track->m0_keysStart = (u8*)(rec->m2_value + base);
            switch (rec->m0_type & 0x7F) {
            case 3:
                track->m8_target = &node->m44_rotation.vx;
                track->m12_isAngle = 1;
                break;
            case 4:
                track->m8_target = &node->m44_rotation.vy;
                track->m12_isAngle = 1;
                break;
            case 5:
                track->m8_target = &node->m44_rotation.vz;
                track->m12_isAngle = 1;
                break;
            case 6:
                track->m8_target = &node->m2C_position.vx;
                track->m12_isAngle = 0;
                break;
            case 7:
                track->m8_target = &node->m2C_position.vy;
                track->m12_isAngle = 0;
                break;
            case 8:
                track->m8_target = &node->m2C_position.vz;
                track->m12_isAngle = 0;
                break;
            }
            track++;
            rec++;
        }
    } while (0);
    resetBattlingAnim(anim);
}


BattlingNode* loadBattlingSkeleton(BattlingDataFile* file) {
    BattlingModel* model;
    u32 i = 0;
    BattlingNode* root;
    BattlingNode** nodes;
    u32 count;
    BattlingSkeleton* skel;
    BattlingNode* node;
    u32* data;
    BattlingBoneData* bones;
    u32* anims;
    BattlingAnim* animArray;
    u32* ptr; /* the model data, then the animation table (one variable: retail keeps both in s4) */

    ptr = (u32*)BL_FILE(file, m4_model);
    data = (u32*)BL_FILE(file, m0_skeleton);
    anims = (u32*)BL_FILE(file, m8_table);
    count = data[0];
    bones = (BattlingBoneData*)(data + 1);
    fieldModelRelocation((Model*)ptr);
    traceNextAlloc(0x12);
    nodes = allocateHeap(count * (s32)sizeof(BattlingNode*), 0);
    skel = createBattlingSkeleton();
    root = createBattlingNode();
    setBattlingNodeSkeleton(root, skel);
    skel->m4_bones = nodes;
    skel->m0_boneCount = count;
    skel->m18_boneData = bones;
    for (; i < count; i++) {
        node = createBattlingNode();
        nodes[i] = node;
        if (bones[i].m2_model != -1) {
            model = createBattlingModel();
            setBattlingNodeModel(node, model);
            setupBattlingModelBlock(model, (ModelBlock*)pcModelBlocks(ptr) + bones[i].m2_model);
        }
        if (bones[i].m0_parent == -1) {
            addBattlingNodeChild(root, node);
        } else {
            addBattlingNodeChild(nodes[bones[i].m0_parent], node);
        }
        node->m44_rotation.vx = bones[i].m4_rotation[0];
        node->m44_rotation.vy = bones[i].m4_rotation[1];
        node->m44_rotation.vz = bones[i].m4_rotation[2];
        node->m2C_position.vx = bones[i].mA_position[0];
        node->m2C_position.vy = bones[i].mA_position[1];
        node->m2C_position.vz = bones[i].mA_position[2];
    }
    if (anims != NULL) {
        ptr = anims;
        traceNextAlloc(0x11);
        animArray = allocateHeap(ptr[0] * (s32)sizeof(BattlingAnim), 0);
        skel->m8_anims = animArray;
        skel->m2_animCount = ptr[0];
        for (i = 0; i < ptr[0]; i++) {
            if (ptr[i + 1] != 0) {
                initBattlingAnim((BattlingAnimRecord*)BL_FILE_AT(file, ptr[i + 1]), &animArray[i], root);
            } else {
                animArray[i].m0_data = NULL;
            }
        }
    }
    return root;
}

s32 stepBattlingAnim(BattlingAnim* anim, s32 frames) {
    return advanceBattlingAnim(anim, frames, 1);
}

s16 interpolateBattlingAngle(s32 cur, s32 target, s32 step) {
    s32 d;
    s16 r;

    cur &= 0xFFF;
    d = (cur - target) & 0xFFF;
    r = cur;
    if (d != 0) {
        if (d < 0x800) {
            r = cur - R3000_DIV(d, step);
        } else {
            r = cur + R3000_DIV(0x1000 - d, step);
        }
    }
    return r;
}

s32 stepBattlingAngleTowards(s32 arg0, s32 arg1, s32 step) {
    s16 cur;
    s16 target;
    s32 d;

    cur = arg0;
    target = arg1;
    d = (arg0 - arg1) & 0xFFF;
    if (d != 0) {
        if (d == 0x800) {
            if (rand() & 1) {
                goto dec;
            }
            goto inc;
        }
        if (d < 0x800) {
        dec:
            cur -= step;
            if (((cur - target) & 0xFFF) > 0x800) {
                cur = target;
            }
        } else {
        inc:
            cur += step;
            if (((cur - target) & 0xFFF) < 0x800) {
                cur = target;
            }
        }
    }
    return cur;
}

s32 advanceBattlingAnim(BattlingAnim* anim, s32 frames, s32 steps) {
    BattlingAnimChannel* ch;
    BattlingAnimTrack* track;
    s32 i;
    s32 j;
    u8 c;
    s8 t;
    s32 d;
    s16 cur;
    s16* target;

    if (anim->m0_data == NULL) {
        return 1;
    }
    if (frames != 0) {
        if (anim->m0_data->m0_frameCount < anim->m12_frame + frames) {
            frames = anim->m0_data->m0_frameCount - anim->m12_frame;
        }
        anim->m12_frame += frames;
        steps -= frames;
        if (steps <= 0) {
            steps = 1;
        }
        ch = anim->m4_channels;
        if (steps == 1) {
            for (i = 0; i < anim->m0_data->m4_channelCount; i++, ch++) {
                *ch->m0_target = ch->m4_value;
            }
        } else {
            for (i = 0; i < anim->m0_data->m4_channelCount; i++, ch++) {
                target = ch->m0_target;
                cur = *target;
                if (ch->m6_isAngle != 0) {
                    *ch->m0_target = interpolateBattlingAngle(cur, (s16)ch->m4_value, steps);
                } else {
                    *target = cur + R3000_DIV((s16)ch->m4_value - cur, steps);
                }
            }
        }
        track = anim->m8_tracks;
        for (i = 0; i < anim->m0_data->m6_trackCount; i++, track++) {
            for (j = 0; j < frames; j++) {
                if (track->mC_wait != 0) {
                    track->mC_wait--;
                } else {
                    t = *track->m4_keys++;
                    c = t;
                    if (t & 0x80) {
                        d = c & 0x3F;
                        if (c & 0x40) {
                            d |= (*track->m4_keys++ << 24) >> 18;
                            track->m10_delta = d;
                        } else {
                            track->mC_wait = d;
                        }
                    } else {
                        track->m10_delta = (c << 25) >> 25;
                    }
                }
                track->mE_value += track->m10_delta;
            }
            if (steps == 1) {
                *track->m8_target = track->mE_value;
            } else {
                target = track->m8_target;
                cur = *target;
                if (track->m12_isAngle != 0) {
                    *track->m8_target = interpolateBattlingAngle(cur, track->mE_value, steps);
                } else {
                    *target = cur + (track->mE_value - cur) / steps;
                }
            }
        }
        if (anim->m12_frame == anim->m0_data->m0_frameCount && steps == 1) {
            return 1;
        }
        return 0;
    }
    /* frames == 0: the PS1 returns what is still in v0, m0_data (callers
     * keep bit 0 of it in a bitfield). */
    return (s32)(uptr)anim->m0_data;
}

BattlingThread* createBattlingThread(void* entry, s32 arg, u32* stack, s32 stackSize) {
    BattlingThread* t;
    s32 i;

    traceNextAlloc(3);
    t = allocateHeap((s32)sizeof(BattlingThread), 2);
    for (i = 31; i >= 0; i--) {
        t->m0_regs[i] = 0;
    }
    t->m80_stack = stack;
    t->m0_regs[28] = func_800405E4();
    t->m0_regs[31] = (uptr)entry;
    t->m0_regs[4] = arg;
    t->m0_regs[30] = t->m0_regs[29] = (uptr)(t->m80_stack + stackSize);
    resumeBattlingThread(t);
    return t;
}

void freeBattlingThread(void* p) {
    freeHeap(p);
}
