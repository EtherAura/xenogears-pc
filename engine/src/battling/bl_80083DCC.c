/* Battling overlay, 0x80083DCC-0x800891C0: scene drawing and lights, loading
 * and the main loop, ending with the debug stats display battlingMain, whose
 * last string is followed by 3 stray bytes of linker fill. */

#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "pc_geometry.h"
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
extern u8 battlingEntryMatchType; /* battlingEntryParams[1] */
extern u8 battlingEntryGear1; /* battlingEntryParams[2] */
extern u8 battlingEntryGear2; /* battlingEntryParams[3] */
extern u8 battlingEntryWinsNeeded; /* battlingEntryParams[4] */
extern u8 battlingEntryComLevel; /* battlingEntryParams[5] */
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
void func_80088C00(void); /* port/src/game/battling/thread.c */
extern s32 battlingDebugEnabled;
extern LoadingBatchCommand battlingStartupLoads[6];
extern s32 battlingPlaysOwnMusic;
extern u16 battlingHudSpriteClut[];
extern u8 battlingArenaRowStarts[];
extern u8 battlingArenaRowEnds[];
extern u8 battlingGroundTexInfo[0x30];
extern u16 D_80091956[]; /* 4 x {tpage, clut} at 0x80091954: [i*2-1] tpage, [i*2] clut */
extern BattlingGearEntry battlingGearEntries[49]; /* [22]: ARGENTO, unlocked when every stage is cleared */
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

void updateBattlingGearGlowLights(BattlingScene* scene, BattlingGear* gear, s32 idx) {
    BattlingNode* node;
    MATRIX unused;
    s32 glow;

    node = scene->m8_lights[idx];
    glow = gear->m918_glow;
    if (glow != 0) {
        gear->m918_glow -= 0x18;
        if (gear->m918_glow > glow) {
            gear->m918_glow = 0;
        }
    }
    if (gear->mD4_cues.flags & 0x20) {
        NODE_LIGHT(node)->mC_r = (gear->mD8_opponent->m15D4_color.r * glow) / 16;
        NODE_LIGHT(node)->mE_g = (gear->mD8_opponent->m15D4_color.g * glow) / 16;
        NODE_LIGHT(node)->m10_b = (gear->mD8_opponent->m15D4_color.b * glow) / 16;
    } else {
        NODE_LIGHT(node)->mC_r = glow << 4;
        NODE_LIGHT(node)->mE_g = glow << 3;
        NODE_LIGHT(node)->m10_b = 0;
    }
    /* The first frame runs before applyBattlingStageColors sets battlingStageColors:
     * the PS1 reads low RAM (0 there, in PCSX-Redux). */
    if (battlingStageColors != NULL && battlingStageColors->m10_dim != 0) {
        NODE_LIGHT(node)->mC_r = (s16)NODE_LIGHT(node)->mC_r / 2;
        NODE_LIGHT(node)->mE_g = (s16)NODE_LIGHT(node)->mE_g / 2;
        NODE_LIGHT(node)->m10_b = (s16)NODE_LIGHT(node)->m10_b / 2;
    }
    NODE_LIGHT(node)->m0_x = gear->m0_position.vx;
    NODE_LIGHT(node)->m4_y = gear->m0_position.vy;
    NODE_LIGHT(node)->m8_z = gear->m0_position.vz;
    NODE_LIGHT(node)->m0_x -= gear->mD8_opponent->m0_position.vx;
    NODE_LIGHT(node)->m4_y -= gear->mD8_opponent->m0_position.vy;
    NODE_LIGHT(node)->m8_z -= gear->mD8_opponent->m0_position.vz;
    MulRotationMatrix(idx, NODE_LIGHT(node));
    node = scene->m8_lights[2];
    NODE_LIGHT(node)->mC_r = NODE_LIGHT(node)->mE_g = NODE_LIGHT(node)->m10_b = 0;
    NODE_LIGHT(node)->m0_x = gear->m0_position.vx;
    NODE_LIGHT(node)->m4_y = gear->m0_position.vy;
    NODE_LIGHT(node)->m8_z = gear->m0_position.vz;
    NODE_LIGHT(node)->m0_x -= battlingCameraPos.vx;
    NODE_LIGHT(node)->m4_y -= battlingCameraPos.vy;
    NODE_LIGHT(node)->m8_z -= battlingCameraPos.vz;
    MulRotationMatrix(2, NODE_LIGHT(node));
}

/* Battle scene draw: camera, gears, lights, ring wall, radar, backdrop. */
/* Enhanced, 60 Hz Battling (xg-hbc.19.2): the picture between two frames of
 * the match, port/src/game/battling/between.c: at a render's entry the
 * frame's memory and transforms are recorded; inside the VSync(2) wait the
 * previous frame's render runs again in a sandbox, halfway back. */
void pcBattling60RenderHook(BattlingScene* scene, int demo);
void pcBattling60Wait(void);
void pcBattling60WaitDone(void);
void portBattlingSweepFrame(void); /* port/src/game/battling/sweep.c */

s32 renderBattlingArenaScene(BattlingScene* scene) {
    MATRIX m2;
    MATRIX m;
    VECTOR unused;
    BattlingLayer* layer;
    BattlingLight* light;

    pcBattling60RenderHook(scene, 0); /* Enhanced, 60 Hz (above) */
    layer = scene->m284_layer;
    beginBattlingLayer(layer);
    setBattlingSceneCamera(scene, &battlingCameraPos, &battlingCameraTarget);
    if (battlingSceneMode != 4 && battlingGameMode != 4 && battlingSceneMode != 8) {
        drawBattlingHud(&battlingGear1, &battlingGear2);
    } else if (battlingGameMode == 4) {
        extern s32 battlingGlyphCount; /* the text glyphs so far */
        s32 glyphs = battlingGlyphCount;
        drawBattlingGearMoveInfo(&battlingGear1);
        pcWideBattlingGlyphs(glyphs, battlingGlyphCount, battlingGear1.mD0_status.bits.b27_side);
        glyphs = battlingGlyphCount;
        if (battlingDebugEnabled != 0) {
            drawBattlingGearMoveInfo(&battlingGear2);
            pcWideBattlingGlyphs(glyphs, battlingGlyphCount, battlingGear2.mD0_status.bits.b27_side);
        }
    }
    drawBattlingAnalogWarning(battlingCurrentOt);
    drawBattlingTextOverlay(battlingCurrentOt);
    m = scene->m4_root->mC_worldMatrix;
    battlingGear1.m60_node->mC_worldMatrix = battlingGear2.m60_node->mC_worldMatrix = m;
    light = scene->m8_lights[0]->m4_data;
    light->mC_r = light->mE_g = light->m10_b = 0x800;
    MulRotationMatrix(0, scene->m8_lights[0]->m4_data);
    gte_SetBackColor(battlingBackColorR, battlingBackColorG, battlingBackColorB);
    updateBattlingGearGlowLights(scene, &battlingGear1, 1);
    renderBattlingNodeTree(battlingGear1.m5C_model);
    updateBattlingGearGlowLights(scene, &battlingGear2, 1);
    renderBattlingNodeTree(battlingGear2.m5C_model);
    gte_SetRotMatrix(&m);
    gte_SetTransMatrix(&m);
    updateBattlingSoundPositions();
    drawBattlingEffects(&m, layer->m4_ot[battlingBufferIndex]);
    gte_SetRotMatrix(&m);
    gte_SetTransMatrix(&m);
    updateBattlingHitSparks(layer->m4_ot[battlingBufferIndex], &scene->m4_root->mC_worldMatrix);
    m2 = battlingIdentityMatrix;
    m2.t[1] = -battlingViewOrigin.vy;
    CompMatrix(&m, &m2, &m2);
    gte_SetRotMatrix(&m2);
    gte_SetTransMatrix(&m2);
    applyBattlingStageColors();
    drawBattlingRingWall(layer->m4_ot[battlingBufferIndex], &battlingCameraPos);
    drawBattlingGearShadowQuad(&battlingGear1, layer->m4_ot[battlingBufferIndex], &m2);
    drawBattlingGearShadowQuad(&battlingGear2, layer->m4_ot[battlingBufferIndex], &m2);
    gte_SetRotMatrix(&m2);
    gte_SetTransMatrix(&m2);
    resetBattlingGroundSpans();
    if (battlingNarrowGroundView != 0) {
        setBattlingGroundViewNarrow(battlingCameraPos.vx, battlingCameraPos.vz, ratan2(battlingCameraPos.vx - battlingCameraTarget.vx, battlingCameraPos.vz - battlingCameraTarget.vz));
    } else {
        setBattlingGroundView(battlingCameraPos.vx, battlingCameraPos.vz, ratan2(battlingCameraPos.vx - battlingCameraTarget.vx, battlingCameraPos.vz - battlingCameraTarget.vz));
    }
    drawBattlingGround(layer->m4_ot[battlingBufferIndex], battlingCameraTarget.vx, battlingCameraTarget.vz);
    addBattlingLayerToOt(layer);
    addBattlingHudTPage();
    addPolyG4(battlingCurrentOt, &battlingBackdropGradient[battlingBufferIndex]);
    return 0;
}

/* Scene draw for the non-battle modes (gears and lights only). */
s32 renderBattlingDemoScene(BattlingScene* scene) {
    MATRIX unused1;
    MATRIX m;
    VECTOR unused2;
    BattlingLayer* layer;
    BattlingLight* light;

    pcBattling60RenderHook(scene, 1); /* Enhanced, 60 Hz (above) */
    layer = scene->m284_layer;
    beginBattlingLayer(layer);
    setBattlingSceneCamera(scene, &battlingCameraPos, &battlingCameraTarget);
    clearBattlingGlyphs();
    m = scene->m4_root->mC_worldMatrix;
    battlingGear1.m60_node->mC_worldMatrix = battlingGear2.m60_node->mC_worldMatrix = m;
    disableBattlingModelBackColor();
    light = scene->m8_lights[0]->m4_data;
    light->mC_r = light->mE_g = light->m10_b = 0x800;
    MulRotationMatrix(0, scene->m8_lights[0]->m4_data);
    gte_SetBackColor(battlingBackColorR, battlingBackColorG, battlingBackColorB);
    updateBattlingGearGlowLights(scene, &battlingGear1, 1);
    renderBattlingNodeTree(battlingGear1.m5C_model);
    updateBattlingGearGlowLights(scene, &battlingGear2, 1);
    renderBattlingNodeTree(battlingGear2.m5C_model);
    gte_SetRotMatrix(&m);
    gte_SetTransMatrix(&m);
    updateBattlingHitSparks(layer->m4_ot[battlingBufferIndex], &scene->m4_root->mC_worldMatrix);
    drawBattlingLineEffects(layer->m4_ot[battlingBufferIndex]);
    addBattlingLayerToOt(layer);
    addBattlingHudTPage();
    pcWideBattlingDemo(battlingCurrentOt); /* the last frame's margins (wide.c) */
    AddPrim(battlingCurrentOt, &battlingDemoBlurSprites[4 + battlingBufferIndex]);
    AddPrim(battlingCurrentOt, &battlingDemoBlurTPages[2 + battlingBufferIndex]);
    AddPrim(battlingCurrentOt, &battlingDemoBlurSprites[2 + battlingBufferIndex]);
    AddPrim(battlingCurrentOt, &battlingDemoBlurSprites[battlingBufferIndex]);
    AddPrim(battlingCurrentOt, &battlingDemoBlurTPages[battlingBufferIndex]);
    return 0;
}

/* Scene draw callback: layer, screen fade, flush. */
s32 renderBattlingPauseScene(BattlingScene* scene) {
    BattlingLayer* layer;

    layer = scene->m284_layer;
    beginBattlingLayer(layer);
    drawBattlingTextOverlay(layer->m4_ot[battlingBufferIndex] + 1);
    addBattlingLayerToOt(layer);
    addBattlingHudTPage();
    return 0;
}

/* Scene draw callback: layer only. */
void beginBattlingSceneLayer(BattlingScene* scene) {
    beginBattlingLayer(scene->m284_layer);
}

/* Scene draw callback: layer, fade quad, screen fade, flush. */
s32 renderBattlingFireScene(BattlingScene* scene) {
    BattlingLayer* layer;

    layer = scene->m284_layer;
    drawBattlingFire(layer->m4_ot[battlingBufferIndex] + 2, 0xC0, 0);
    drawBattlingTextOverlay(layer->m4_ot[battlingBufferIndex] + 1);
    addBattlingLayerToOt(layer);
    addBattlingHudTPage();
    return 0;
}

/* Draws the fade overlay while battlingFireFade is set. */
void renderBattlingFireFrame(void) {
    if (battlingFireFade != 0) {
        updateBattlingFire();
        drawBattlingGearSelect(battlingCurrentOt, 0);
        drawBattlingTextOverlay(battlingCurrentOt);
        drawBattlingFire(battlingCurrentOt, 0xC0, 1);
        yieldBattlingThread();
    }
}

/* Steps the fade-in battlingFireFade by 4 and finishes it. */
void fadeOutBattlingFire(void) {
    if (battlingFireFade != 0) {
        battlingFireFade -= 4;
        if (battlingFireFade <= 0) {
            battlingFireFade = 0;
            battlingFireAnimating = 0;
            if (battlingMusic != NULL) /* none when started from the kernel menu: the PS1 writes low RAM */
            setMusicInstanceTempo(battlingMusic, 0x100, 0);
            battlingSpeedScale = 0x100;
            freeBattlingFire();
        } else {
            drawBattlingFire(battlingCurrentOt, battlingFireFade, 1);
        }
    } else {
        battlingFireAnimating = 0;
    }
}

/* Attaches the extra model battlingGearExtraModel to bone 12 of the gear. */
void attachBattlingGearExtraModel(BattlingGear* gear) {
    BattlingNode* bone;
    BattlingNode* node;
    BattlingModel* model;

    bone = NODE_SKELETON(gear->m5C_model)->m4_bones[12];
    node = createBattlingNode();
    model = createBattlingModel();
    setBattlingNodeModel(node, model);
    setupBattlingModelBlock(model, pcModelBlockOf(battlingGearExtraModel));
    addBattlingNodeChild(bone, node);
    node->m44_rotation.vy = 0xC00;
    node->m44_rotation.vx = 0;
    node->m44_rotation.vz = 0x400;
}

/* Sets up a gear from its data file: side, opponent, skeleton, model flags,
 * parameters, part counts, then uploads its CLUT and portrait (mirrored for
 * side 0) and its icon to VRAM. */
void setupBattlingGear(BattlingGear* gear, BattlingDataFile* file, s32 side) {
    RECT rect;
    void* p; /* the skeleton root, later the mirrored portrait */
    s32 i;
    s32 x;
    u8* src;
    u8* dst;
    u8* params;

    gear->mD0_status.bits.b27_side = side;
    if (side != 0) {
        setBattlingTextureClutPos(0x380, 0, 0, 0x1FE);
        gear->mD8_opponent = &battlingGear1;
    } else {
        setBattlingTextureClutPos(0x3C0, 0, 0, 0x1FF);
        gear->mD8_opponent = &battlingGear2;
    }
    relocateBattlingDataFile(file);
    p = loadBattlingSkeleton(file);
    gear->m60_node = createBattlingNode();
    addBattlingNodeChild(gear->m60_node, p);
    gear->m5C_model = p;
    gear->m1600_def = &battlingGearDefs[gear->m909_gearId];
    gear->m90A_flags = 0;
    switch (gear->m909_gearId) {
    case 36:
    case 37:
        gear->m90A_flags |= 1;
    case 38:
        gear->m90A_flags |= 2;
        break;
    case 3:
    case 14:
    case 27:
    case 34:
    case 35:
    case 39:
    case 41:
    case 42:
        gear->m90A_flags |= 4;
        break;
    case 13:
        clearBattlingTextureOverride();
        attachBattlingGearExtraModel(gear);
        break;
    }
    setBattlingGearHitSounds(gear);
    params = BL_FILE(file, m10_params);
    gear->m8FC_params = (BattlingGearParams*)params;
    gear->m7C_animParams = BL_FILE(file, m14_animParams);
    gear->m80_moves = (BattlingMove*)BL_FILE(file, m18_moves);
    gear->m900_eventOffsets = (s16*)(params + 0x34);
    gear->m904_hiddenParts = (u8*)(*(u32*)(params + 0x30) + (uptr)params);
    gear->m908_hiddenPartCount = params[0xE];
    gear->m15D4_color.r = params[0x10];
    gear->m15D4_color.g = params[0x11];
    gear->m15D4_color.b = params[0x12];
    gear->m911_partCount = 0;
    gear->m912_count = 0;
    for (i = 0; i < 14; i++) {
        if (gear->m1600_def->mA_moves[i] != 0) {
            if (gear->m80_moves[i].m3_alive != 0) {
                gear->m911_partCount++;
            } else {
                gear->m912_count++;
            }
        }
    }
    rect.x = 0;
    rect.y = side + 0x1F6;
    rect.w = 0x100;
    rect.h = 1;
    LoadImage(&rect, BL_FILE(file, m20_images));
    rect.x = side * 16 + 0x380;
    rect.y = 0x100;
    rect.w = 0xB;
    rect.h = 0x16;
    if (side != 0) {
        LoadImage(&rect, (u8*)BL_FILE(file, m20_images) + 0x200);
    } else {
        src = (u8*)BL_FILE(file, m20_images) + 0x200;
        p = allocateHeap(0x1E4, 0);
        dst = p;
        for (i = 0; i < 0x16; i++) {
            for (x = 0; x < 0x16; x++) {
                *dst++ = src[0x15 - x];
            }
            src += 0x16;
        }
        LoadImage(&rect, p);
        freeHeapDelayed(p, 1);
    }
    rect.x = 0x3A0;
    rect.y = side * 8 + 0x100;
    rect.w = 0x10;
    rect.h = 8;
    LoadImage(&rect, (u8*)BL_FILE(file, m20_images) + 0x3E4);
}

/* Blinks battlingCurrentOt every other frame while battlingFireAnimating is set. */
void updateBattlingFireHalfRate(void) {
    if (battlingFireAnimating != 0 && (g_playTimeInVsync & 1)) {
        updateBattlingFire();
    }
}

/* Enters mode 5 (clear VRAM, sub-mode 6). */
void startBattlingDemo(void) {
    battlingScreenClearFlag &= 0xFE;
    clearBattlingVram(0);
    setBattlingSceneMode(5);
    battlingSettings.m6_p2Com = 1;
    battlingSettings.m5_p1Com = 1;
    battlingGameMode = 6;
}

/* Leaves mode 5. */
void stopBattlingDemo(void) {
    battlingSettings.m6_p2Com = 0;
    battlingSettings.m5_p1Com = 0;
    battlingScreenClearFlag |= 1;
}

/* Loads gear model id for side (file id + 2 in directory 0x30). */
void loadBattlingGearFile(s32 side, s32 id) {
    if (side != 0) {
        battlingGear2.m909_gearId = id;
    } else {
        battlingGear1.m909_gearId = id;
    }
    if (battlingGearFiles[side] != NULL) {
        freeHeap(battlingGearFiles[side]);
        battlingGearFiles[side] = NULL;
    }
    setCurrentDirectory(0x30, 1);
    battlingGearFiles[side] = loadBattlingFile(id + 2);
    setCurrentDirectory(0x30, 0);
}

/* Frees the loaded model of side. */
void freeBattlingGearFile(s32 side) {
    waitReadCompletion(0);
    if (battlingGearFiles[side] != NULL) {
        freeHeap(battlingGearFiles[side]);
        battlingGearFiles[side] = NULL;
    }
}

/* Allocate the load buffer of a file load entry (file index at +0). */
void allocBattlingLoadBuffer(LoadingBatchCommand* arg0, s32 arg1) {
    arg0->m4_loadPtr = allocateHeap(getFileSizeAligned(arg0->m0_fileIndex), arg1);
}

void exitBattling(void) {
    unloadSequence(battlingSfxSequence);
    if (battlingPlaysOwnMusic != 0) {
        startMusicInstanceSub0(battlingMusic);
        freeSoundInstance(battlingMusic);
    }
    saveBattlingSettings();
    setGameMode(1);
    DrawSync(0);
    VSync(2);
    battlingEntryParams = 1;
    bootGame(0);
}

s32 shouldBattlingSkipGearSelect(void) {
    s32 ret = 0;
    if (battlingNextSceneMode == 5 || battlingNextSceneMode == 7 || (battlingNextSceneMode == 1 && battlingGameMode == 4 && battlingEntryParams == 0)) {
        ret = 1;
    }
    return ret;
}

/* Battling main task: load everything, then the menu / battle state loop. */
void battlingMainTask(void) {
    BattlingScene* task;
    void* music;
    s32 timer;
    s32 state;
    s32 hold = 0; /* the thread's registers start zeroed (createBattlingThread) */
    u32* p;
    void* tmp;
    u32* pcFile;

    battlingDebugDisplayFlags = 0;
    battlingScreenClearFlag |= 1;
    battlingHeightMap = allocateHeap(0x10010, 0);
    task = createBattlingScene(createBattlingLayer(0x1000));
    relocateModelBlock((ModelBlock*)battlingGearExtraModel);
    allocBattlingLoadBuffer(&battlingStartupLoads[0], 0);
    allocBattlingLoadBuffer(&battlingStartupLoads[1], 0);
    allocBattlingLoadBuffer(&battlingStartupLoads[2], 1);
    allocBattlingLoadBuffer(&battlingStartupLoads[3], 1);
    allocBattlingLoadBuffer(&battlingStartupLoads[4], 1);
    batchStartLoadingFiles(battlingStartupLoads, 0, 0);
    music = battlingStartupLoads[0].m4_loadPtr;
    battlingSfxSequence = battlingStartupLoads[1].m4_loadPtr;
    initBattlingDisplayAndDrawEnvs(0x140, 0xDA);
    clearBattlingTimedLines();
    patchSelfModifyingCode(1, 1, 0x40, 0x40);
    battlingFireAnimating = 0;
    setVsyncUserCallback(updateBattlingFireHalfRate);
    initDebugFontClut(0x7FFF, 0x8000);
    initFontPalettes(0x140, 0xFF);
    initBattlingMenus();
    initBattlingSettings();
    resetBattlingFire();
    battlingGearFiles[1] = NULL;
    battlingGearFiles[0] = NULL;
    battlingGear2.m60_node = 0;
    battlingGear1.m60_node = 0;
    freeBattlingScreenCopy(1);
    waitReadCompletion(0);
    loadSequence(battlingSfxSequence);
    if (battlingPlaysOwnMusic != 0) {
        battlingMusic = createMusicInstance(music);
        startMusicInstance(battlingMusic, 0x7F, 0);
    } else {
        battlingMusic = pMusic;
    }
    decompress(battlingStartupLoads[3].m4_loadPtr, battlingHeightMap);
    freeHeap(battlingStartupLoads[3].m4_loadPtr);
    initBattlingArena();
    p = (u32*)mallocAndDecompress(battlingStartupLoads[2].m4_loadPtr, 0);
    freeHeap(battlingStartupLoads[2].m4_loadPtr);
    doPointerRelocation(p);
    battlingDialogTexts = PC_RELOC_ENTRY(p, 1);
    battlingGearDefs = PC_RELOC_ENTRY(p, 2);
    buildBattlingGearList(battlingEntryParams == 1);
    p = (u32*)mallocAndDecompress(battlingStartupLoads[4].m4_loadPtr, 1);
    freeHeap(battlingStartupLoads[4].m4_loadPtr);
    doPointerRelocation(p);
    pcFile = p;
    p = (u32*)pcRelocTable(pcFile); /* the file's offsets as native pointers */
    initBattlingRingWall(p);
    initBattlingSpriteEffects(p);
    initBattlingFont(p);
    loadBattlingTextures((u32**)p);
    initBattlingHudSprites((u32**)p);
    initBattlingTutorialCursor(p);
    freeHeap(p);
    freeHeap(pcFile);
    initBattlingPortraitSlots();
    initBattlingTutorialWindow();
    yieldBattlingThread();
    D_8009289C = 0;

top:
    timer = 0x4650;
    flushDelayedFrees();
    freeBattlingNodeTree(battlingGear1.m60_node);
    freeBattlingNodeTree(battlingGear2.m60_node);
    flushDelayedFrees();
    enterBattlingMainMenu();
    battlingFireFade = 0xFF;
    battlingFireAnimating = 0;
    initBattlingFire();
    resetBattlingMatch();
    dealocateDialogWindow18MemorySub(&battlingMessageWindow);
    battlingNextSceneMode = -1;
    battlingEventFight = 0;
    battlingStageColorIndex = 0;
    if (battlingEntryParams == 1) {
        battlingVsyncInterval = 0;
        state = 0;
    } else {
        if (battlingEntryParams == 2) {
            battlingEventFight = 1;
            battlingRubberBandMode = 0;
        }
        battlingEntryParams = 0;
        battlingSettings.m0_comLevel = battlingEntryComLevel;
        battlingSettings.m7_winsNeeded = battlingEntryWinsNeeded;
        freeBattlingPortraitFile();
        battlingSettings.m5_p1Com = 0;
        battlingStoryGear1 = battlingEntryGear1;
        battlingStoryGear2 = battlingEntryGear2;
        switch (battlingEntryMatchType) {
        case 0:
            battlingSettings.m6_p2Com = 1;
            setBattlingSceneMode(1);
            battlingGameMode = 1;
            loadBattlingPortraitPair(battlingStoryGear1, battlingStoryGear2);
            if (battlingStoryGear2 == 5) {
                battlingStageColorIndex = 1;
            }
            break;
        case 1:
            battlingSettings.m6_p2Com = 1;
            setBattlingSceneMode(1);
            battlingGameMode = 4;
            break;
        case 2:
            battlingSettings.m6_p2Com = 0;
            setBattlingSceneMode(7);
            battlingGameMode = 5;
            initBattlingTutorial();
            break;
        }
        loadBattlingGearFile(0, battlingStoryGear1);
        state = 1;
        loadBattlingGearFile(1, battlingStoryGear2);
        battlingStartRequested = 0;
        battlingVsyncInterval = 2;
    }
    if (shouldBattlingSkipGearSelect() != 0) {
        waitReadCompletion(0);
        state = 10;
    }
    while (state != 10) {
        if ((newPadButtonForScripts & ~1) || (newPadButtonForScripts2 & ~1)) {
            timer = 0x4650;
        }
        if ((newPadButtonForScripts & 1) && isBattlingMainMenuOpen() != 0) {
            hold++;
            if (hold == 0x78) {
                timer = 0;
            }
        } else {
            hold = 0;
        }
        if (timer == 0) {
            if (battlingFireFade != 0) {
                loadBattlingDemoGears();
                battlingFireFade = 0;
                waitReadCompletion(0);
                startBattlingDemo();
                break;
            }
        } else {
            timer--;
        }
        beginBattlingSceneLayer(task);
        switch (state) {
        case 0:
            updateBattlingMenus();
            if (battlingStartRequested != 0) {
                battlingVsyncInterval = 2;
                state = 1;
            }
            if (battlingFrameCounter & 1) {
                updateBattlingFire();
            }
            break;
        case 1:
            mergeQueuedInputs();
            if (shouldBattlingSkipGearSelect() == 0) {
                updateBattlingFire();
                drawBattlingGearSelect(battlingCurrentOt, 0);
            }
            if (waitReadCompletion(1) == 0) {
                state = 10;
            }
            break;
        }
        renderBattlingFireScene(task);
        yieldBattlingThread();
    }
    battlingVsyncInterval = 0;
    if (shouldBattlingSkipGearSelect() != 0) {
        battlingFireFade = 0;
        battlingFireAnimating = 0;
        freeBattlingFire();
        freeBattlingPortraitFile();
    }
    tmp = mallocAndDecompress(battlingGearFiles[0], 0);
    freeHeap(battlingGearFiles[0]);
    battlingGearFiles[0] = tmp;
    renderBattlingFireFrame();
    tmp = mallocAndDecompress(battlingGearFiles[1], 0);
    freeHeap(battlingGearFiles[1]);
    battlingGearFiles[1] = tmp;
    renderBattlingFireFrame();
    setupBattlingGear(&battlingGear1, battlingGearFiles[0], 0);
    renderBattlingFireFrame();
    setupBattlingGear(&battlingGear2, battlingGearFiles[1], 1);
    renderBattlingFireFrame();
    loadBattlingGearColorClut(&battlingGear1.m15D4_color, &battlingGear2.m15D4_color);
    setBattlingArenaClearTiles();
    if (battlingFireFade != 0) {
        battlingFireAnimating = 1;
        if (battlingMusic != NULL) /* none when started from the kernel menu: the PS1 writes low RAM */
        setMusicInstanceTempo(battlingMusic, 0x158, 0);
        battlingSpeedScale = 0x200;
    }
    battlingRoundWinner = 2;
restart:
    startBattlingRound();
    /* a loop that never repeats: the inner loop only exits by goto; loop
     * optimisation hoists the gear addresses above the stores */
    while (1) {
        battlingGear1.m0_position.vx = 0x3E80;
        battlingGear1.m0_position.vy = 0;
        battlingGear1.m0_position.vz = 0x3F80;
        battlingGear2.m0_position.vx = 0x4080;
        battlingGear2.m0_position.vy = 0;
        battlingGear2.m0_position.vz = 0x3F80;
        while (1) {
            setDebugFontColor(0, 0xFF, 0);
            battlingVsyncInterval = 2;
            battlingNarrowGroundView = 0;
            battlingSceneMode = battlingNextSceneMode;
            fadeOutBattlingFire();
            updateAndRenderTextForDialogWindow(&battlingMessageWindow, battlingCurrentOt, battlingBufferIndex);
            switch (battlingSceneMode) {
            case 1:
                updateBattlingFight(&battlingGear1, &battlingGear2);
                SetGeomScreen(0xC0);
                if (battlingNextSceneMode == 1) {
                    updateBattlingFightCamera(&battlingGear1, &battlingGear2);
                }
                renderBattlingArenaScene(task);
                break;
            case 6:
                updateBattlingVictoryScreen(task);
                break;
            case 7:
                updateBattlingTutorial();
                SetGeomScreen(0xC0);
                renderBattlingArenaScene(task);
                if (battlingNextSceneMode == 3) {
                    battlingVsyncInterval = 0;
                    yieldBattlingThread();
                    goto end_battle;
                }
                break;
            case 4:
                SetGeomScreen(0x800);
                updateBattlingReplay(&battlingGear1, &battlingGear2);
                if (battlingNextSceneMode != 8) {
                    updateBattlingOrbitCamera(1);
                    battlingNarrowGroundView = 1;
                }
                renderBattlingArenaScene(task);
                break;
            case 8:
                SetGeomScreen(0x200);
                updateBattlingRoundResult(&battlingGear1, &battlingGear2);
                renderBattlingArenaScene(task);
                break;
            case 0:
                updateBattlingMenus();
                renderBattlingPauseScene(task);
                battlingVsyncInterval = 0;
                break;
            case 5:
                mergeQueuedInputs();
                if (newPadButtonForDialogs != 0) {
                    newPadButtonForDialogs = 0;
                    newPadButtonForField = 0;
                    stopBattlingDemo();
                    battlingVsyncInterval = 0;
                    setBattlingBlackClearTiles();
                    yieldBattlingThread();
                    goto top;
                }
                updateBattlingFight(&battlingGear1, &battlingGear2);
                SetGeomScreen(0xC0);
                updateBattlingFightCamera(&battlingGear1, &battlingGear2);
                renderBattlingDemoScene(task);
                break;
            case 3:
            end_battle:
                if (battlingEntryParams == 0) {
                    exitBattling();
                }
                setBattlingBlackClearTiles();
                goto top;
            case 2:
                if (battlingSettings.m7_winsNeeded != 0) {
                    u8 v;
                    if (battlingGear1.mF2_wins == battlingSettings.m7_winsNeeded) {
                        v = battlingSettings.m5_p1Com;
                    } else if (battlingGear2.mF2_wins == battlingSettings.m7_winsNeeded) {
                        v = battlingSettings.m6_p2Com;
                    } else {
                        goto again;
                    }
                    if (v != 0) {
                        goto end_battle;
                    }
                    setBattlingSceneMode(6);
                    break;
                }
            again:
                setBattlingSceneMode(1);
                goto restart;
            }
            setDebugFontLineStart(0x9E, 0);
            setDebugFontColor(0xFF, 0xFF, 0);
            yieldBattlingThread();
        }
    }
}

void getBattlingHudPointLeft(u16* arg0, u16* arg1) {
    arg1[0] = arg0[0] + 0x18;
    arg1[1] = arg0[1] + 6;
    arg1[0] = arg1[0] + 0x4F;
}

void getBattlingHudPointRight(u16* arg0, u16* arg1) {
    arg1[0] = 0x8B - arg0[0];
    arg1[1] = 0x20 - arg0[1];
    arg1[0] = arg1[0] + 0x4F;
}

void getBattlingHudBarX(s32 arg0, s16* arg1, s32 arg2) {
    if (arg0 != 0) {
        *arg1 = 0xDA - arg2;
    } else {
        *arg1 = arg2 + 0x67;
    }
}

void getBattlingHudBarY(s32 arg0, s16* arg1, s32 arg2) {
    if (arg0 != 0) {
        *arg1 = 0x20 - arg2;
    } else {
        *arg1 = arg2 + 6;
    }
}

extern u16 battlingHudPoints[8][2];
/* Storage view for the overlapping six-triangle source and mirror walks.
 * The named view retains the logical three triangles per side and four quads;
 * every array access in the builder stays within its declared storage. */
typedef struct HudStorage {
    /* 0x000 */ DR_TPAGE m0_tpage, m8_tpage;
    /* 0x010 */ LINE_F4 m10_frame[4];
    /* 0x080 */ POLY_F4 m80_bars[6];
    /* 0x110 */ DR_TPAGE m110_tpage;
    /* 0x118 */ POLY_F4 m118_bars[6], m1A8_bars[6];
    /* 0x238 */ union {
        POLY_F3 m0_source[6];
        struct {
            u8 m0_pad[3 * sizeof(POLY_F3)]; /* 0x3C; the PC's POLY_F3 is bigger */
            POLY_F3 m3C_dest[6];
        } m0_mirror;
        struct {
            POLY_F3 m0_tris[3], m3C_mirror[3];
            POLY_F4 m78_quads[4];
        } m0_named;
    } m238_storage;
} HudStorage;

/* Build frame, health bars, mirrored triangles and fixed HUD quads.
 * These single-pass scopes preserve the old GCC scheduling and allocation:
 * nine scopes on the actual Y read give it 16 weighted references; the packed
 * store group raises the first five coordinate values together. Only the
 * existing loads, stores and coordinate decrements are emitted. Verified in
 * both regional builds, with no volatile qualifiers or register pins. */
void buildBattlingHudPrims(BattlingHudPrims* arg0) {
    HudStorage* h = (HudStorage*)arg0;
    s32 i;
    u16 y0, x2;
    u32 color, q2v0, q2v1, q2v2, q2v3, q3v0, q3v1, q3v2, q3v3;

    SetDrawTPage(&h->m0_tpage, 0, 1, GetTPage(0, 2, 0, 0));
    SetDrawTPage(&h->m8_tpage, 0, 0, GetTPage(0, 1, 0, 0));
    setlen(&h->m10_frame[0], 6);
    *(u32*)&h->m10_frame[0].r0 = 0x4C000000;
    h->m10_frame[0].pad = 0x55555555;
    setlen(&h->m10_frame[1], 6);
    *(u32*)&h->m10_frame[1].r0 = 0x4C000000;
    h->m10_frame[1].pad = 0x55555555;
    setlen(&h->m10_frame[2], 6);
    *(u32*)&h->m10_frame[2].r0 = 0x4C000000;
    h->m10_frame[2].pad = 0x55555555;
    setlen(&h->m10_frame[3], 6);
    *(u32*)&h->m10_frame[3].r0 = 0x4C000000;
    h->m10_frame[3].pad = 0x55555555;
    getBattlingHudPointLeft(battlingHudPoints[0], &h->m10_frame[0].x0);
    getBattlingHudPointLeft(battlingHudPoints[1], &h->m10_frame[0].x1);
    getBattlingHudPointLeft(battlingHudPoints[2], &h->m10_frame[0].x2);
    getBattlingHudPointLeft(battlingHudPoints[3], &h->m10_frame[0].x3);
    getBattlingHudPointLeft(battlingHudPoints[3], &h->m10_frame[1].x0);
    getBattlingHudPointLeft(battlingHudPoints[4], &h->m10_frame[1].x1);
    getBattlingHudPointLeft(battlingHudPoints[5], &h->m10_frame[1].x2);
    getBattlingHudPointLeft(battlingHudPoints[0], &h->m10_frame[1].x3);
    getBattlingHudPointRight(battlingHudPoints[0], &h->m10_frame[2].x0);
    getBattlingHudPointRight(battlingHudPoints[1], &h->m10_frame[2].x1);
    getBattlingHudPointRight(battlingHudPoints[2], &h->m10_frame[2].x2);
    getBattlingHudPointRight(battlingHudPoints[3], &h->m10_frame[2].x3);
    getBattlingHudPointRight(battlingHudPoints[3], &h->m10_frame[3].x0);
    getBattlingHudPointRight(battlingHudPoints[4], &h->m10_frame[3].x1);
    getBattlingHudPointRight(battlingHudPoints[5], &h->m10_frame[3].x2);
    getBattlingHudPointRight(battlingHudPoints[0], &h->m10_frame[3].x3);
    MargePrim(&h->m10_frame[0], &h->m10_frame[1]);
    MargePrim(&h->m10_frame[2], &h->m10_frame[3]);
    h->m10_frame[0].x0 = 0x1D;
    h->m10_frame[2].x0 = 0x121;
    setlen(&h->m80_bars[0], 5);
    *(u32*)&h->m80_bars[0].r0 = 0x280000FF;
    setlen(&h->m80_bars[1], 5);
    *(u32*)&h->m80_bars[1].r0 = 0x280000FF;
    setlen(&h->m80_bars[2], 5);
    *(u32*)&h->m80_bars[2].r0 = 0x280000FF;
    setlen(&h->m80_bars[3], 5);
    *(u32*)&h->m80_bars[3].r0 = 0x280000FF;
    setlen(&h->m80_bars[4], 5);
    *(u32*)&h->m80_bars[4].r0 = 0x280000FF;
    setlen(&h->m80_bars[5], 5);
    *(u32*)&h->m80_bars[5].r0 = 0x280000FF;
    getBattlingHudPointLeft(battlingHudPoints[0], &h->m80_bars[0].x0);
    getBattlingHudPointLeft(battlingHudPoints[7], &h->m80_bars[0].x1);
    getBattlingHudPointLeft(battlingHudPoints[5], &h->m80_bars[0].x2);
    getBattlingHudPointLeft(battlingHudPoints[4], &h->m80_bars[0].x3);
    getBattlingHudPointLeft(battlingHudPoints[7], &h->m80_bars[1].x0);
    getBattlingHudPointLeft(battlingHudPoints[6], &h->m80_bars[1].x1);
    getBattlingHudPointLeft(battlingHudPoints[4], &h->m80_bars[1].x2);
    getBattlingHudPointLeft(battlingHudPoints[3], &h->m80_bars[1].x3);
    getBattlingHudPointLeft(battlingHudPoints[6], &h->m80_bars[2].x0);
    getBattlingHudPointLeft(battlingHudPoints[1], &h->m80_bars[2].x1);
    getBattlingHudPointLeft(battlingHudPoints[3], &h->m80_bars[2].x2);
    getBattlingHudPointLeft(battlingHudPoints[2], &h->m80_bars[2].x3);
    getBattlingHudPointRight(battlingHudPoints[0], &h->m80_bars[3].x0);
    getBattlingHudPointRight(battlingHudPoints[7], &h->m80_bars[3].x1);
    getBattlingHudPointRight(battlingHudPoints[5], &h->m80_bars[3].x2);
    getBattlingHudPointRight(battlingHudPoints[4], &h->m80_bars[3].x3);
    getBattlingHudPointRight(battlingHudPoints[7], &h->m80_bars[4].x0);
    getBattlingHudPointRight(battlingHudPoints[6], &h->m80_bars[4].x1);
    getBattlingHudPointRight(battlingHudPoints[4], &h->m80_bars[4].x2);
    getBattlingHudPointRight(battlingHudPoints[3], &h->m80_bars[4].x3);
    getBattlingHudPointRight(battlingHudPoints[6], &h->m80_bars[5].x0);
    getBattlingHudPointRight(battlingHudPoints[1], &h->m80_bars[5].x1);
    getBattlingHudPointRight(battlingHudPoints[3], &h->m80_bars[5].x2);
    getBattlingHudPointRight(battlingHudPoints[2], &h->m80_bars[5].x3);
    SetDrawTPage(&h->m110_tpage, 0, 1, GetTPage(0, 1, 0, 0));
    copyBattlingWords(h->m118_bars, h->m80_bars, sizeof(h->m80_bars));
    copyBattlingWords(h->m1A8_bars, h->m80_bars, sizeof(h->m80_bars));
    for (i = 0; i < 6; i++) {
        setlen(&h->m118_bars[i], 5);
        *(u32*)&h->m118_bars[i].r0 = 0x28806060;
    }
    for (i = 0; i < 6; i++) {
        setlen(&h->m1A8_bars[i], 5);
        *(u32*)&h->m1A8_bars[i].r0 = 0x280000FF;
    }
    *(u32*)&h->m238_storage.m0_named.m0_tris[0].x0 = 0x200014;
    *(u32*)&h->m238_storage.m0_named.m0_tris[0].x1 = 0x20001C;
    *(u32*)&h->m238_storage.m0_named.m0_tris[0].x2 = 0x28001C;
    *(u32*)&h->m238_storage.m0_named.m0_tris[1].x0 = 0x200013;
    *(u32*)&h->m238_storage.m0_named.m0_tris[1].x1 = 0x290013;
    *(u32*)&h->m238_storage.m0_named.m0_tris[1].x2 = 0x29001B;
    *(u32*)&h->m238_storage.m0_named.m0_tris[2].x0 = 0x320013;
    *(u32*)&h->m238_storage.m0_named.m0_tris[2].x1 = 0x2A0013;
    *(u32*)&h->m238_storage.m0_named.m0_tris[2].x2 = 0x2A001B;
    /* Six source triangles and six mirror triangles deliberately overlap in HUD storage. */
    for (i = 0; i < 6; i++) {
        setlen(&h->m238_storage.m0_source[i], 4);
        *(u32*)&h->m238_storage.m0_source[i].r0 = 0x2000FF00;
        setlen(&h->m238_storage.m0_mirror.m3C_dest[i], 4);
        *(u32*)&h->m238_storage.m0_mirror.m3C_dest[i].r0 = 0x2000FF00;
        h->m238_storage.m0_mirror.m3C_dest[i].y0 = h->m238_storage.m0_source[i].y0;
        h->m238_storage.m0_mirror.m3C_dest[i].y1 = h->m238_storage.m0_source[i].y1;
        h->m238_storage.m0_mirror.m3C_dest[i].y2 = h->m238_storage.m0_source[i].y2;
        h->m238_storage.m0_mirror.m3C_dest[i].x0 = 0x140 - h->m238_storage.m0_source[i].x0;
        h->m238_storage.m0_mirror.m3C_dest[i].x1 = 0x140 - h->m238_storage.m0_source[i].x1;
        h->m238_storage.m0_mirror.m3C_dest[i].x2 = 0x140 - h->m238_storage.m0_source[i].x2;
    }
    color = 0x280000FF;
    q2v0 = 0x320006;
    q2v1 = 0x36000A;
    q2v2 = 0x4C0006;
    q2v3 = 0x48000A;
    q3v0 = 0x32013A;
    q3v1 = 0x360136;
    q3v2 = 0x4C013A;
    q3v3 = 0x480136;
    *(u32*)&h->m238_storage.m0_named.m78_quads[0].r0 = 0x28000000;
    *(u32*)&h->m238_storage.m0_named.m78_quads[1].r0 = 0x28000000;
    h->m238_storage.m0_named.m78_quads[0].x1 = 0x63;
    h->m238_storage.m0_named.m78_quads[0].x3 = 0x5E;
    h->m238_storage.m0_named.m78_quads[0].y1 = 9;
    h->m238_storage.m0_named.m78_quads[0].y0 = 9;
    h->m238_storage.m0_named.m78_quads[0].y3 = 20;
    h->m238_storage.m0_named.m78_quads[0].y2 = 20;
    h->m238_storage.m0_named.m78_quads[1].x2 = 0x122;
    h->m238_storage.m0_named.m78_quads[1].x0 = 0x122;
    h->m238_storage.m0_named.m78_quads[1].x1 = 0xE3;
    h->m238_storage.m0_named.m78_quads[1].x3 = 0xDE;
    h->m238_storage.m0_named.m78_quads[1].y1 = 19;
    h->m238_storage.m0_named.m78_quads[1].y0 = 19;
    do {
        do {
            do {
                do {
                    do {
                        do {
                            do {
                                do {
                                    do {
                                        y0 = h->m238_storage.m0_named.m3C_mirror[1].y0;
                                    } while (0);
                                } while (0);
                            } while (0);
                        } while (0);
                    } while (0);
                } while (0);
            } while (0);
        } while (0);
    } while (0);
    h->m238_storage.m0_named.m78_quads[0].x2 = 30;
    h->m238_storage.m0_named.m78_quads[0].x0 = 30;
    h->m238_storage.m0_named.m78_quads[1].y3 = 30;
    h->m238_storage.m0_named.m78_quads[1].y2 = 30;
    do {
        x2 = h->m238_storage.m0_named.m3C_mirror[1].x2;
    } while (0);
    setlen(&h->m238_storage.m0_named.m78_quads[0],5);
    setlen(&h->m238_storage.m0_named.m78_quads[1],5);
    setlen(&h->m238_storage.m0_named.m78_quads[2],5);
    do {
        *(u32*)&h->m238_storage.m0_named.m78_quads[2].r0 = color;
        *(u32*)&h->m238_storage.m0_named.m78_quads[2].x0 = q2v0;
        *(u32*)&h->m238_storage.m0_named.m78_quads[2].x1 = q2v1;
        *(u32*)&h->m238_storage.m0_named.m78_quads[2].x2 = q2v2;
        *(u32*)&h->m238_storage.m0_named.m78_quads[2].x3 = q2v3;
        setlen(&h->m238_storage.m0_named.m78_quads[3],5);
        *(u32*)&h->m238_storage.m0_named.m78_quads[3].r0 = color;
        *(u32*)&h->m238_storage.m0_named.m78_quads[3].x0 = q3v0;
    } while (0);
    do {
        y0--;
        h->m238_storage.m0_named.m3C_mirror[1].y0 = y0;
        x2--;
        h->m238_storage.m0_named.m3C_mirror[1].x2 = x2;
    } while (0);
    *(u32*)&h->m238_storage.m0_named.m78_quads[3].x1 = q3v1;
    *(u32*)&h->m238_storage.m0_named.m78_quads[3].x2 = q3v2;
    *(u32*)&h->m238_storage.m0_named.m78_quads[3].x3 = q3v3;
}


/* Build a textured quad (both buffers) showing a whole TIM at (x, y). */
void setupBattlingTimQuad(TIM_IMAGE* tim, s32 x, s32 y, POLY_FT4* p, s32 mode) {
    s32 scale;
    s32 w;

    switch (mode) {
    case 0:
        scale = 4;
        break;
    case 1:
        scale = 2;
        break;
    case 2:
        scale = 1;
        break;
    }
    setlen(p, 9);
    p->code = 0x2D;
    p->clut = GetClut(tim->crect->x, tim->crect->y);
    p->tpage = GetTPage(mode, 0, tim->prect->x, tim->prect->y);
    p->x0 = p->x2 = x;
    w = x + tim->prect->w * scale;
    if (scale == 2) {
        p->x3 = w + 1;
    } else {
        p->x3 = w;
    }
    p->x1 = p->x3 = p->x3; /* sic: retail re-reads x3 and stores it twice */
    p->y0 = p->y1 = y;
    p->y2 = p->y3 = tim->prect->h + y;
    p->u0 = p->u2 = tim->prect->x * scale;
    p->u1 = p->u3 = (tim->prect->x + tim->prect->w) * scale;
    p->v0 = p->v1 = tim->prect->y;
    p->v2 = p->v3 = tim->prect->y + tim->prect->h;
    p[1] = p[0];
}

/* Same as setupBattlingTimQuad but mirrored horizontally. */
void setupBattlingTimQuadMirrored(TIM_IMAGE* tim, s32 x, s32 y, POLY_FT4* p, s32 mode) {
    s32 scale;

    switch (mode) {
    case 0:
        scale = 4;
        break;
    case 1:
        scale = 2;
        break;
    case 2:
        scale = 1;
        break;
    }
    setlen(p, 9);
    p->code = 0x2D;
    p->clut = GetClut(tim->crect->x, tim->crect->y);
    p->tpage = GetTPage(mode, 0, tim->prect->x, tim->prect->y);
    p->x0 = p->x2 = x + tim->prect->w * scale;
    p->x1 = p->x3 = x;
    p->u0 = p->u2 = tim->prect->x * scale - 1;
    p->u1 = p->u3 = (tim->prect->x + tim->prect->w) * scale - 1;
    p->y0 = p->y1 = y;
    p->y2 = p->y3 = tim->prect->h + y;
    p->v0 = p->v1 = tim->prect->y;
    p->v2 = p->v3 = tim->prect->y + tim->prect->h;
    p[1] = p[0];
}

/* Upload the title/panel TIMs (file table entries 22, 23) and build the
 * title, panel and sprite primitives of battlingHudSprites (both buffers). */
void initBattlingHudSprites(u32** files) {
    TIM_IMAGE tim;
    RECT rect;
    s16* clut;
    BattlingPrim28* sp;

    OpenTIM(files[22]);
    ReadTIM(&tim);
    clut = (s16*)tim.caddr;
    clut[0] = 0;
    clut[1] = 0x8000;
    LoadImage(tim.crect, tim.caddr);
    LoadImage(tim.prect, tim.paddr);
    setupBattlingTimQuad(&tim, 6, 7, &battlingHudSprites[0][0].ft4, 0);
    sp = &battlingHudSprites[2][0];
    setupBattlingTimQuadMirrored(&tim, 0x13A - tim.prect->w * 4, 7, &battlingHudSprites[1][0].ft4, 0);
    SetDrawTPage(&battlingHudTPages[0][0], 0, 1, GetTPage(1, 0, 0x380, 0x100));
    battlingHudTPages[0][1] = battlingHudTPages[0][0];
    SetDrawTPage(&battlingHudTPages[1][0], 0, 1, GetTPage(0, 0, 0x380, 0x100));
    battlingHudTPages[1][1] = battlingHudTPages[1][0];
    setlen(&sp->sprt, 4);
    sp->sprt.code = 0x65;
    *(u32*)&sp->sprt.x0 = 0x90007;
    *(u16*)&sp->sprt.u0 = 0;
    *(u32*)&sp->sprt.w = 0x160016;
    sp->sprt.clut = GetClut(0, 0x1F6);
    battlingHudSprites[3][0] = battlingHudSprites[2][1] = *sp;
    sp = &battlingHudSprites[3][0];
    sp->sprt.x0 = 0x123;
    sp->sprt.u0 = 0x20;
    sp->sprt.clut = GetClut(0, 0x1F7);
    battlingHudSprites[3][1] = *sp;
    OpenTIM(files[23]);
    ReadTIM(&tim);
    LoadImage(tim.prect, tim.paddr);
    setupBattlingTimQuad(&tim, 6, 0x20, &battlingHudSprites[6][0].ft4, 0);
    setupBattlingTimQuadMirrored(&tim, 0x13A - tim.prect->w * 4, 0x20, &battlingHudSprites[7][0].ft4, 0);
    sp = &battlingHudSprites[4][0];
    setlen(&battlingHudSprites[6][0].ft4, 9);
    *(u32*)&battlingHudSprites[7][1].ft4.r0 = *(u32*)&battlingHudSprites[7][0].ft4.r0 = *(u32*)&battlingHudSprites[6][1].ft4.r0 = *(u32*)&battlingHudSprites[6][0].ft4.r0 = 0x2C000080;
    setlen(&battlingHudSprites[6][1].ft4, 9);
    setlen(&battlingHudSprites[7][0].ft4, 9);
    setlen(&battlingHudSprites[7][1].ft4, 9);
    setlen(&sp->sprt, 4);
    sp->sprt.code = 0x65;
    *(u32*)&sp->sprt.w = 0x80040;
    *(u16*)&sp->sprt.u0 = 0x80;
    battlingGauge1V = battlingHudSprites[6][0].ft4.v0;
    battlingGauge2V = battlingHudSprites[7][0].ft4.v0;
    battlingHudSprites[7][0].ft4.clut = battlingHudSprites[7][1].ft4.clut = battlingHudSprites[6][0].ft4.clut = battlingHudSprites[6][1].ft4.clut = battlingHudSprites[0][0].ft4.clut;
    sp->sprt.clut = GetClut(0x3A0, 0x110);
    *(u32*)&sp->sprt.x0 = 0xB001E;
    battlingHudSprites[5][0] = *sp;
    sp = &battlingHudSprites[5][0];
    *(u16*)&sp->sprt.u0 = 0x880;
    *(u32*)&sp->sprt.x0 = 0x1500E3;
    battlingHudSprites[4][1] = battlingHudSprites[4][0];
    battlingHudSprites[5][1] = battlingHudSprites[5][0];
    rect.x = 0x3A0;
    rect.y = 0x110;
    rect.w = 0x10;
    rect.h = 1;
    LoadImage(&rect, battlingHudSpriteClut);
}

void addBattlingHudTPage(void) {
    AddPrim(battlingCurrentOt, &battlingHudPrims[battlingBufferIndex].m8_tpage);
}

void drawBattlingHudBar(void* ot, POLY_F4* p, s32 w, s32 side) {
    s32 d;

    if (w >= 0x38) {
        getBattlingHudBarX(side, &p[0].x1, 0x38);
        getBattlingHudBarX(side, &p[0].x3, 0x34);
        AddPrim(ot, &p[0]);
    } else {
        getBattlingHudBarX(side, &p[0].x1, w);
        getBattlingHudBarX(side, &p[0].x3, w - 4);
        AddPrim(ot, &p[0]);
        return;
    }
    if (w >= 0x48) {
        getBattlingHudBarX(side, &p[1].x1, 0x48);
        getBattlingHudBarX(side, &p[1].x3, 0x40);
        getBattlingHudBarY(side, &p[1].y3, 0x10);
        AddPrim(ot, &p[1]);
    } else {
        getBattlingHudBarX(side, &p[1].x1, w);
        d = w - 0x38;
        getBattlingHudBarX(side, &p[1].x3, w - (d / 4 + 4));
        getBattlingHudBarY(side, &p[1].y3, d / 2 + 8);
        AddPrim(ot, &p[1]);
        return;
    }
    getBattlingHudBarX(side, &p[2].x1, w);
    getBattlingHudBarX(side, &p[2].x3, w - 8);
    AddPrim(ot, &p[2]);
}

s32 setBattlingEtherWarningColor(BattlingGear* arg0, POLY_F4* arg1) {
    if (canBattlingGearUseEther(arg0, 0) != 0) {
        return 0;
    }
    if (canBattlingGearUseEther(arg0, 1) != 0) {
        setlen(arg1, 5);
        *(u32*)&arg1->r0 = 0x2800FFFF;
    } else {
        setlen(arg1, 5);
        *(u32*)&arg1->r0 = 0x280000FF;
    }
    return 1;
}

/* Add the HUD primitives of both gears to the OT. */
void drawBattlingHud(BattlingGear* g0, BattlingGear* g1) {
    BattlingHudPrims* h;
    POLY_FT4* p;
    s32 t;
    s32 i;

    h = &battlingHudPrims[battlingBufferIndex];
    i = g0->mF2_wins;
    if (i > 0) {
        AddPrim(battlingCurrentOt, &h->m238_tris[0]);
    }
    if (i >= 2) {
        AddPrim(battlingCurrentOt, &h->m238_tris[1]);
    }
    if (i >= 3) {
        AddPrim(battlingCurrentOt, &h->m238_tris[2]);
    }
    i = g1->mF2_wins;
    if (i > 0) {
        AddPrim(battlingCurrentOt, &h->m274_trisMirror[0]);
    }
    if (i >= 2) {
        AddPrim(battlingCurrentOt, &h->m274_trisMirror[1]);
    }
    if (i >= 3) {
        AddPrim(battlingCurrentOt, &h->m274_trisMirror[2]);
    }
    if (setBattlingEtherWarningColor(g0, &h->m2B0_quads[2])) {
        AddPrim(battlingCurrentOt, &h->m2B0_quads[2]);
    }
    if (setBattlingEtherWarningColor(g1, &h->m2B0_quads[3])) {
        AddPrim(battlingCurrentOt, &h->m2B0_quads[3]);
    }
    AddPrim(battlingCurrentOt, &battlingHudSprites[0][battlingBufferIndex]);
    AddPrim(battlingCurrentOt, &battlingHudSprites[1][battlingBufferIndex]);
    AddPrim(battlingCurrentOt, &battlingHudSprites[2][battlingBufferIndex]);
    AddPrim(battlingCurrentOt, &battlingHudSprites[3][battlingBufferIndex]);
    AddPrim(battlingCurrentOt, &battlingHudTPages[0][battlingBufferIndex]);
    AddPrim(battlingCurrentOt, &battlingHudSprites[4][battlingBufferIndex]);
    AddPrim(battlingCurrentOt, &battlingHudSprites[5][battlingBufferIndex]);
    AddPrim(battlingCurrentOt, &battlingHudTPages[1][battlingBufferIndex]);
    AddPrim(battlingCurrentOt, &battlingHudSprites[7][battlingBufferIndex]);
    AddPrim(battlingCurrentOt, &battlingHudSprites[6][battlingBufferIndex]);
    pcWideBattlingHud(battlingCurrentOt); /* the corners to the wide edges (wide.c) */
    AddPrim(battlingCurrentOt, &h->m2B0_quads[0]);
    AddPrim(battlingCurrentOt, &h->m2B0_quads[1]);
    AddPrim(battlingCurrentOt, &h->m10_frame[0]);
    AddPrim(battlingCurrentOt, &h->m10_frame[2]);
    drawBattlingHudBar(battlingCurrentOt, &h->m80_bars[0], battlingGear1.mC0_hpBar, 0);
    drawBattlingHudBar(battlingCurrentOt, &h->m80_bars[3], battlingGear2.mC0_hpBar, 1);

    i = (s16)g0->mB6_gauge >> 6;
    p = &battlingHudSprites[6][battlingBufferIndex].ft4;
    p->r0 = (i >= 0x39) ? g_playTimeInVsync * 8 : 0xFF;
    p->y0 = p->y1 = 0x60 - i;
    p->v0 = p->v1 = battlingGauge1V - (i - 0x40);
    i = (s16)g1->mB6_gauge >> 6;
    p = &battlingHudSprites[7][battlingBufferIndex].ft4;
    p->r0 = (i >= 0x39) ? g_playTimeInVsync * 8 : 0xFF;
    p->y0 = p->y1 = 0x60 - i;
    p->v0 = p->v1 = battlingGauge1V - (i - 0x40);

    if (g0->mC2_flash != 0) {
        t = 0x100 - g0->mC2_flash;
        for (i = 0; i < 3; i++) {
            h->m1A8_bars[i].r0 = ((t * 3) >> 3) + ((g0->mC2_flash * 0xFF) >> 8);
            h->m1A8_bars[i].g0 = (t * 3) >> 3;
            h->m1A8_bars[i].b0 = t >> 1;
        }
        drawBattlingHudBar(battlingCurrentOt, &h->m1A8_bars[0], g0->mC1_bar, 0);
    }
    if (g1->mC2_flash != 0) {
        t = 0x100 - g1->mC2_flash;
        for (i = 3; i < 6; i++) {
            h->m1A8_bars[i].r0 = ((t * 3) >> 3) + ((g1->mC2_flash * 0xFF) >> 8);
            h->m1A8_bars[i].g0 = (t * 3) >> 3;
            h->m1A8_bars[i].b0 = t >> 1;
        }
        drawBattlingHudBar(battlingCurrentOt, &h->m1A8_bars[3], g1->mC1_bar, 1);
    }
    for (i = 0; i < 6; i++) {
        AddPrim(battlingCurrentOt, &h->m118_bars[i]);
    }
    AddPrim(battlingCurrentOt, &h->m0_tpage);
}

void initBattlingHud(void) {
    buildBattlingHudPrims(&battlingHudPrims[0]);
    battlingHudPrims[1] = battlingHudPrims[0];
}

void resetBattlingGroundSpans(void) {
    s32 i;
    for (i = 0; i < 0x80; i++) {
        battlingGroundSpanEnds[i] = 0;
        battlingGroundSpanStarts[i] = 0xFF;
    }
}

/* Rasterise one polygon edge into the per-scanline min/max spans. */
void addBattlingGroundSpanEdge(s32 x0, s32 y0, s32 x1, s32 y1) {
    s32 t;
    s32 x;
    s32 dx;
    s32 xi;

    if (y0 == y1) {
        return;
    }
    if (y1 < y0) {
        t = x1;
        x1 = x0;
        x0 = t;
        t = y1;
        y1 = y0;
        y0 = t;
    }
    x = x0 << 8;
    dx = R3000_DIV((x1 - x0) << 8, y1 - y0);
    for (; y0 < y1; y0++, x += dx) {
        if (y0 < 0) {
            continue;
        }
        if (y0 >= 0x80) {
            return;
        }
        xi = x >> 8;
        if (xi < battlingGroundSpanStarts[y0]) {
            if (xi < battlingArenaRowStarts[y0]) {
                xi = battlingArenaRowStarts[y0];
            }
            battlingGroundSpanStarts[y0] = xi;
        }
        if (battlingGroundSpanEnds[y0] < xi) {
            if (battlingArenaRowEnds[y0] + 1 < xi) {
                xi = battlingArenaRowEnds[y0] + 1;
            }
            battlingGroundSpanEnds[y0] = xi;
        }
    }
}

void drawBattlingGround(OTag* arg0, s32 arg1, s32 arg2) {
    fogNearColor.cd = 0x24;
    gte_ldrgb(&fogNearColor);
    copyBattlingWords((void*)0x1F800120, battlingGroundTexInfo, 0x30);
    renderBattlingGround(arg0, arg1, arg2);
    pcWideBattlingGround(arg0, arg1, arg2); /* the margins' floor (wide.c) */
}

void initBattlingGroundPrims(void) {
    s32 i;
    POLY_FT3* p;

    battlingGroundSpanEnds = (u8*)0x1F800000;
    battlingGroundSpanStarts = (u8*)0x1F800080;
    battlingGroundPolys[0] = allocateHeap(1800 * (s32)sizeof(POLY_FT3), 0);
    battlingGroundPolys[1] = allocateHeap(1800 * (s32)sizeof(POLY_FT3), 0);
    p = battlingGroundPolys[0];
    for (i = 0; i < 0x708; i++) {
        setlen(p, 7);
        setcode(p, 0x24);
        p++;
    }
    copyBattlingWords(battlingGroundPolys[1], battlingGroundPolys[0], 1800 * (s32)sizeof(POLY_FT3));
}

/* Upload the battling TIMs (file table arg0) to VRAM. */
void loadBattlingTextures(u32** arg0) {
    TIM_IMAGE tim;
    RECT unused; /* unused 8-byte local: retail's frame has the slot */
    s32 i;
    s16* clut;

    for (i = 0; i < 4; i++) {
        OpenTIM(arg0[18 + i]);
        ReadTIM(&tim);
        D_80091956[i * 2] = GetClut(tim.crect->x, tim.crect->y);
        D_80091956[i * 2 - 1] = GetTPage(1, 1, tim.prect->x, tim.prect->y);
        LoadImage(tim.crect, tim.caddr);
        LoadImage(tim.prect, tim.paddr);
    }
    OpenTIM(arg0[14]);
    ReadTIM(&tim);
    battlingShadowClut = GetClut(tim.crect->x, tim.crect->y);
    battlingShadowTPage = GetTPage(0, 2, tim.prect->x, tim.prect->y);
    battlingShadowU = tim.prect->x * 4;
    battlingShadowV = tim.prect->y;
    clut = (s16*)tim.caddr;
    clut[0] = 0;
    for (i = 1; i < 16; i++) {
        clut[i] |= 0x8000;
    }
    LoadImage(tim.crect, tim.caddr);
    LoadImage(tim.prect, tim.paddr);
    for (i = 28; i < 37; i++) {
        OpenTIM(arg0[i]);
        ReadTIM(&tim);
        LoadImage(tim.crect, tim.caddr);
        LoadImage(tim.prect, tim.paddr);
    }
}

/* Set up the gear's shadow quad (both buffers). */
void initBattlingGearShadowQuad(BattlingGear* arg0) {
    POLY_FT4* p = &arg0->m1604_shadow[0];

    *(u32*)&p->r0 = 0x2C101010;
    setlen(p, 9);
    p->code |= 2;
    p->clut = battlingShadowClut;
    p->tpage = battlingShadowTPage;
    *(u16*)&p->u0 = battlingShadowU | (battlingShadowV << 8);
    *(u16*)&p->u1 = (battlingShadowU + 0x3F) | (battlingShadowV << 8);
    *(u16*)&p->u2 = battlingShadowU | ((battlingShadowV + 0x3F) << 8);
    *(u16*)&p->u3 = (battlingShadowU + 0x3F) | ((battlingShadowV + 0x3F) << 8);
    arg0->m1604_shadow[1] = arg0->m1604_shadow[0];
}

/* Project the gear's shadow quad under it and add it to the OT. */
void drawBattlingGearShadowQuad(BattlingGear* arg0, OTag* ot, MATRIX* m) {
    SVECTOR v[4];
    VECTOR pos;
    VECTOR unused;
    SVECTOR rot;
    MATRIX mat;
    s32 opz;
    s32 sz0;
    s32 sz1;
    s32 sz2;
    s32 sz3;
    POLY_FT4* p;
    s32 size;
    s32 nsize;
    s32 z;

    if ((arg0->mD0_status.flags & 0x60000000) == 0x20000000) {
        return;
    }
    size = (arg0->m93C_footPoint.vy + arg0->m92C_topPoint.vy) / 32 + 0x90;
    if (size <= 0) {
        return;
    }
    nsize = -size;
    pos.vx = (arg0->m93C_footPoint.vx + arg0->m92C_topPoint.vx) / 2;
    pos.vz = (arg0->m93C_footPoint.vz + arg0->m92C_topPoint.vz) / 2;
    v[0].vx = v[1].vx = v[0].vz = v[2].vz = nsize;
    pos.vy = 0;
    v[3].vx = v[2].vx = v[1].vz = v[3].vz = size;
    mat.t[0] = pos.vx - battlingViewOrigin.vx;
    v[0].vy = v[1].vy = v[2].vy = v[3].vy = 0;
    p = &arg0->m1604_shadow[battlingBufferIndex];
    mat.t[1] = getBattlingGroundHeight(&pos, 0);
    mat.t[2] = pos.vz - battlingViewOrigin.vz;
    getBattlingGroundNormal(&rot);
    mat.m[0][0] = 0x1000;
    mat.m[0][1] = 0;
    mat.m[0][2] = 0;
    mat.m[1][0] = rot.vx;
    mat.m[1][1] = rot.vy;
    mat.m[1][2] = rot.vz;
    mat.m[2][0] = 0;
    mat.m[2][1] = 0;
    mat.m[2][2] = 0x1000;
    CompMatrix(m, &mat, &mat);
    gte_SetRotMatrix(&mat);
    gte_SetTransMatrix(&mat);
    gte_ldv3c(v);
    gte_rtpt();
    gte_nclip();
    gte_stopz(&opz);
    GteState saved;
    int rescue = opz < 0;
    if (rescue) {
        if (!pcGeometryRescue() || !(p = pcGeometryAlloc(p, sizeof(*p)))) return;
        gteSave(&saved);
    }
    gte_stsz3(&sz0, &sz1, &sz2);
    gte_stsxy3_ft4(p);
    gte_ldv0(&v[3]);
    gte_rtps();
    gte_stsz(&sz3);
    gte_stsxy(&p->x3);
    z = sz0;
    if (sz1 < z) {
        z = sz1;
    }
    if (sz2 < z) {
        z = sz2;
    }
    if (sz3 < z) {
        z = sz3;
    }
    opz = z >> 4;
    AddPrim(&ot[opz], p);
    if (rescue) gteLoad(&saved);
}

/* Queue a debug axis marker at arg0 (drawn by drawBattlingDebugMarkers). */
void addBattlingDebugMarker(VECTOR* arg0) {
    SVECTOR* p;
    SVECTOR* base;
    if (battlingDebugMarkerCount < 31) {
        base = &battlingDebugMarkers[0].m60_pos;
        p = (SVECTOR*)((u8*)base + battlingDebugMarkerCount * sizeof(BattlingMarker));
        p->vx = arg0->vx;
        p->vy = arg0->vy;
        battlingDebugMarkerCount++;
        p->vz = arg0->vz;
    }
}

/* Draw the queued debug axis markers and empty the queue. */
void drawBattlingDebugMarkers(void* ot) {
    SVECTOR v[6];
    s32 i;
    s32 j;
    BattlingMarker* m;

    for (i = 0; i < battlingDebugMarkerCount; i++) {
        m = &battlingDebugMarkers[i];
        v[0].vx = m->m60_pos.vx - 0x20;
        v[0].vy = m->m60_pos.vy;
        v[0].vz = m->m60_pos.vz;
        v[1].vx = m->m60_pos.vx + 0x20;
        v[1].vy = m->m60_pos.vy;
        v[1].vz = m->m60_pos.vz;
        v[2].vx = m->m60_pos.vx;
        v[2].vy = m->m60_pos.vy - 0x20;
        v[2].vz = m->m60_pos.vz;
        v[3].vx = m->m60_pos.vx;
        v[3].vy = m->m60_pos.vy + 0x20;
        v[3].vz = m->m60_pos.vz;
        v[4].vx = m->m60_pos.vx;
        v[4].vy = m->m60_pos.vy;
        v[4].vz = m->m60_pos.vz - 0x20;
        v[5].vx = m->m60_pos.vx;
        v[5].vy = m->m60_pos.vy;
        v[5].vz = m->m60_pos.vz + 0x20;
        for (j = 0; j < 6; j++) {
            v[j].vx -= battlingViewOrigin.vx;
            v[j].vy -= battlingViewOrigin.vy;
            v[j].vz -= battlingViewOrigin.vz;
        }
        gte_ldv3(&v[0], &v[1], &v[2]);
        gte_rtpt();
        gte_stsxy3(&m->m0_lines[battlingBufferIndex][0].x0, &m->m0_lines[battlingBufferIndex][0].x1, &m->m0_lines[battlingBufferIndex][1].x0);
        gte_ldv3(&v[3], &v[4], &v[5]);
        gte_rtpt();
        gte_stsxy3(&m->m0_lines[battlingBufferIndex][1].x1, &m->m0_lines[battlingBufferIndex][2].x0, &m->m0_lines[battlingBufferIndex][2].x1);
        setlen(&m->m0_lines[battlingBufferIndex][0], 3);
        *(u32*)&m->m0_lines[battlingBufferIndex][0].r0 = 0x400000FF;
        setlen(&m->m0_lines[battlingBufferIndex][1], 3);
        *(u32*)&m->m0_lines[battlingBufferIndex][1].r0 = 0x4000FF00;
        setlen(&m->m0_lines[battlingBufferIndex][2], 3);
        *(u32*)&m->m0_lines[battlingBufferIndex][2].r0 = 0x40FF0000;
        addLineF2(ot, &m->m0_lines[battlingBufferIndex][0]);
        addLineF2(ot, &m->m0_lines[battlingBufferIndex][1]);
        addLineF2(ot, &m->m0_lines[battlingBufferIndex][2]);
    }
    battlingDebugMarkerCount = 0;
}

/* Take a free line slot: endpoints from two VECTORs, colour from flag bits
 * (1 = blue, 2 = red, 4 = green). */
BattlingEffectLine* addBattlingTimedLine(VECTOR* arg0, VECTOR* arg1, s32 arg2) {
    s32 i;
    BattlingEffectLine* p;

    for (i = 0; i < 100; i++) {
        p = &battlingTimedLines[i];
        if (p->m30_timer == 0) {
            p->m30_timer = 1;
            p->m20_start.vx = arg0->vx;
            p->m20_start.vy = arg0->vy;
            p->m20_start.vz = arg0->vz;
            p->m28_end.vx = arg1->vx;
            p->m28_end.vy = arg1->vy;
            p->m28_end.vz = arg1->vz;
            p->m0_line[0].r0 = (arg2 & 2) * 0x7F;
            p->m0_line[0].g0 = (arg2 & 4) * 0x3F;
            p->m0_line[0].b0 = (arg2 & 1) * 0xFF;
            p->m0_line[1].r0 = (arg2 & 2) * 0x7F;
            p->m0_line[1].g0 = (arg2 & 4) * 0x3F;
            p->m0_line[1].b0 = (arg2 & 1) * 0xFF;
            return p;
        }
    }
    return NULL;
}

void addBattlingTimedLineWithTimer(VECTOR* arg0, VECTOR* arg1, s32 arg2, s16 arg3) {
    BattlingEffectLine* p = addBattlingTimedLine(arg0, arg1, arg2);
    if (p != NULL) {
        p->m30_timer = arg3;
    }
}

void clearBattlingTimedLines(void) {
    s32 i;
    for (i = 0; i < 100; i++) {
        battlingTimedLines[i].m30_timer = 0;
    }
}

/* Project and draw the active lines (timer counts down). */
void drawBattlingTimedLines(void* ot) {
    s32 i;
    SVECTOR s;
    SVECTOR e;
    BattlingEffectLine* p;

    for (i = 0; i < 100; i++) {
        if (battlingTimedLines[i].m30_timer != 0) {
            p = &battlingTimedLines[i];
            p->m30_timer--;
            s = p->m20_start;
            e = p->m28_end;
            s.vx -= battlingViewOrigin.vx;
            s.vy -= battlingViewOrigin.vy;
            s.vz -= battlingViewOrigin.vz;
            e.vx -= battlingViewOrigin.vx;
            e.vy -= battlingViewOrigin.vy;
            e.vz -= battlingViewOrigin.vz;
            gte_ldv01(&s, &e);
            gte_rtpt();
            gte_stsxy01(&p->m0_line[battlingBufferIndex].x0, &p->m0_line[battlingBufferIndex].x1);
            setlen(&p->m0_line[battlingBufferIndex], 3);
            setcode(&p->m0_line[battlingBufferIndex], 0x40);
            addLineF2(ot, &p->m0_line[battlingBufferIndex]);
        }
    }
}

void normalizeBattlingVector(VECTOR* arg0, VECTOR* arg1) {
    VECTOR v;
    s32 sq;
    s32 len;

    v = *arg0;
    sq = getLargestAbsComponent(v.vx, v.vy, v.vz);
    if (sq < 0) {
        sq = -sq;
    }
    len = SquareRoot0(sq);
    v.vx = R3000_DIV(v.vx, len);
    v.vy = R3000_DIV(v.vy, len);
    v.vz = R3000_DIV(v.vz, len);
    VectorNormal(&v, arg1);
}

void normalizeBattlingVectorS(VECTOR* arg0, SVECTOR* arg1) {
    VECTOR v;
    s32 sq;
    s32 len;

    v = *arg0;
    sq = getLargestAbsComponent(v.vx, v.vy, v.vz);
    if (sq < 0) {
        sq = -sq;
    }
    len = SquareRoot0(sq);
    v.vx = R3000_DIV(v.vx, len);
    v.vy = R3000_DIV(v.vy, len);
    v.vz = R3000_DIV(v.vz, len);
    VectorNormalS(&v, arg1);
}

void normalizeBattlingSVector(SVECTOR* arg0, SVECTOR* arg1) {
    VECTOR v;
    s32 sq;
    s32 len;

    v.vx = arg0->vx;
    v.vy = arg0->vy;
    v.vz = arg0->vz;
    sq = getLargestAbsComponent(v.vx, v.vy, v.vz);
    if (sq < 0) {
        sq = -sq;
    }
    len = SquareRoot0(sq);
    v.vx = R3000_DIV(v.vx, len);
    v.vy = R3000_DIV(v.vy, len);
    v.vz = R3000_DIV(v.vz, len);
    VectorNormalS(&v, arg1);
}

s32 getBattlingVectorLength(VECTOR* arg0) {
    VECTOR out;
    gte_ldlvl(arg0);
    gte_sqr0();
    gte_stlvnl(&out);
    return SquareRoot0(out.vx + out.vy + out.vz);
}

s32 getBattlingVectorLengthXZ(VECTOR* arg0) {
    VECTOR out;
    gte_ldlvl(arg0);
    gte_sqr0();
    gte_stlvnl(&out);
    return SquareRoot0(out.vx + out.vz);
}

s32 getBattlingDistance(VECTOR* arg0, VECTOR* arg1) {
    VECTOR d;
    d.vx = arg1->vx - arg0->vx;
    d.vy = arg1->vy - arg0->vy;
    d.vz = arg1->vz - arg0->vz;
    gte_ldlvl(&d);
    gte_sqr0();
    gte_stlvnl(&d);
    return SquareRoot0(d.vx + d.vy + d.vz);
}

s32 getBattlingDistanceXZ(VECTOR* arg0, VECTOR* arg1) {
    VECTOR d;
    d.vx = arg1->vx - arg0->vx;
    d.vz = arg1->vz - arg0->vz;
    gte_ldlvl(&d);
    gte_sqr0();
    gte_stlvnl(&d);
    return SquareRoot0(d.vx + d.vz);
}

/* K&R definition: markBattlingGearDefeated calls it without an argument. */
void setBattlingGearDefeated(arg0)
    s32 arg0;
{
    s32 bit = 1;
    s32 sh = arg0 & 7;
    battlingProgress.m0_flags[arg0 >> 3] |= (bit << sh);
}

s32 isBattlingGearDefeated(s32 arg0) {
    s32 bit = 1;
    s32 sh = arg0 & 7;
    return battlingProgress.m0_flags[arg0 >> 3] & (bit << sh);
}

void clearBattlingGearDefeated(s32 arg0) {
    s32 bit = 1;
    bit = bit << (arg0 & 7);
    battlingProgress.m0_flags[arg0 >> 3] &= ~bit;
}

void setBattlingAllDefeated(void) {
    battlingProgress.m8_x10_allCleared = 1;
}

void unlockBattlingBonusGear(void) {
    if (battlingProgress.m8_x10_allCleared && battlingBonusGearUnlocked == 0) {
        battlingBonusGearUnlocked = 1;
        battlingGearList[battlingGearListCount++] = &battlingGearEntries[22];
    }
}

s32 checkBattlingAllDefeated(void) {
    s32 i;
    for (i = 0; i < 49; i++) {
        if (i != 22 && isBattlingGearDefeated(i) == 0) {
            return 0;
        }
    }
    battlingProgress.m8_x10_allCleared = 1;
    unlockBattlingBonusGear();
    return 0;
}

/* Store the settings into the kernel copy (see loadBattlingSettings). */
void saveBattlingSettings(void) {
    if (battlingEntryParams != 0) {
        battlingProgress.m8_valid = 1;
        battlingProgress.m8_x4 = battlingSettings.m3_p1Vibration;
        battlingProgress.m8_x5 = battlingSettings.m4_p2Vibration;
        battlingProgress.m8_x6 = battlingSettings.m7_winsNeeded;
        battlingProgress.m8_xD = battlingSettings.m0_comLevel;
    }
}

/* Read the settings back from the kernel copy (or set defaults). JP also
 * sets battlingSettings.m3_p1Vibration when battlingEntryParams is 0, defaults 9B/9C to 1 and does not
 * clear the flag bytes. */
void loadBattlingSettings(void) {
    s32 i;
#if VERSION_JP
    if (battlingEntryParams == 0) {
        battlingSettings.m3_p1Vibration = 1;
    } else {
#else
    if (battlingEntryParams != 0) {
#endif
        battlingBonusGearUnlocked = 0;
        if (battlingProgress.m8_valid == 1) {
            battlingSettings.m3_p1Vibration = battlingProgress.m8_x4;
            battlingSettings.m4_p2Vibration = battlingProgress.m8_x5;
            battlingSettings.m7_winsNeeded = battlingProgress.m8_x6;
            battlingSettings.m0_comLevel = battlingProgress.m8_xD;
#if !VERSION_JP
            if (battlingProgress.m8_x10_allCleared) {
                for (i = 0; i < 8; i++) {
                    battlingProgress.m0_flags[i] = 0;
                }
            }
#endif
        } else {
#if VERSION_JP
            battlingSettings.m3_p1Vibration = 1;
            battlingSettings.m4_p2Vibration = 1;
#else
            battlingSettings.m3_p1Vibration = 0;
            battlingSettings.m4_p2Vibration = 0;
#endif
            battlingSettings.m7_winsNeeded = 2;
            battlingSettings.m0_comLevel = 0;
            saveBattlingSettings();
        }
    }
}

/* The PS1 passes its caller's argument on in a0 (updateBattlingGearActions
 * calls it with the gear id). */
void markBattlingGearDefeated(s32 gearId) {
    setBattlingGearDefeated(gearId);
    checkBattlingAllDefeated();
}

INCLUDE_ASM("battling/nonmatchings/bl_80083DCC", battlingMainTaskTable);

void showBattlingHeapDump(void) {
    if (newPadButtonForScripts & 4) {
        battlingHeapDumpStart++;
    }
    if (newPadButtonForScripts & 1) {
        battlingHeapDumpStart--;
    }
    if (battlingHeapDumpStart < 0) {
        battlingHeapDumpStart = 0;
    }
    dumpHeapBlocks(1, battlingHeapDumpStart, 10, 0x80AD);
}

void initBattlingCursorSprite(s32 arg0) {
    BattlingDisplayBuffer* p = &battlingDisplayBuffers[arg0];
    setlen(&p->m74_sprtTag, 3);
    p->m7B_code = 0x7D;
    p->m80_uv = 0x3000;
    p->m82_clut = GetClut(0x3F0, 0xC0);
}

void initBattlingSystem(void) {
    RECT unused; /* unused 8-byte local: retail's frame has the slot */

    DrawSyncCallback(func_80088C00);
    InitGeom();
    resetMemoryAllocStats(6, (sptr)battlingHeapTypeNames);
    setCurrentDirectory(0x30, 0);
    initDebugFont(4, 2, 0x138, 0xDA, 0x14, 1, 0x3C0, 0x1F0, 0x3C0, 0x1EF, 0);
    initBattlingCursorSprite(0);
    initBattlingCursorSprite(1);
    switch (runningOnDTL) {
    case -1:
        D_800928CC = 2;
        break;
    case 0:
        D_800928CC = 1;
        break;
    default:
        D_800928CC = 0;
        break;
    }
    battlingDisplayBuffer = &battlingDisplayBuffers[0];
    battlingPrevDisplayBuffer = &battlingDisplayBuffers[1];
    setBattlingTexturePos(-1, -1);
    setBattlingClutPos(-1, -1);
    battlingVsyncInterval = 2;
    D_8009289C = 1;
    battlingDebugOtHook = NULL;
    battlingFrameCounter = 0;
    battlingBufferIndex = 0;
    battlingScreenClearFlag = 0;
    battlingDebugOtHook = NULL;
    battlingDebugDisplayFlags = 7;
    initBattlingSoundChannels();
}

INCLUDE_RODATA("battling/nonmatchings/bl_80083DCC", battlingGearNames);

/* Battling main loop (never returns). */
void battlingMain(void) {
    DISPENV disp;
    BattlingThread* task;
    s32 cpu;
    u32 fps;
    u32 prev = 0;
    u8 buf;

    initBattlingSystem();
    task = createBattlingThread(battlingMainTaskTable[battlingMainTaskIndex], 0, (void*)0x801FE000, 0x400);
    g_playTimeInVsync; /* dead volatile read: retail loads it into v0 here */
loop:
        fieldPolyCount2 = 0;
        fieldPolyCount = 0;
        buf = (battlingFrameCounter + 1) & 1;
        battlingBufferIndex = buf;
        battlingPrevDisplayBuffer = &battlingDisplayBuffers[battlingFrameCounter & 1];
        battlingFrameCounter++;
        battlingDisplayBuffer = &battlingDisplayBuffers[buf];
        battlingCurrentOt = &battlingDisplayBuffer->m70_ot;
        disp = battlingDisplayBuffer->m5C_dispEnv;
        checkSoftReboot();
        TermPrim(battlingCurrentOt);
        if ((battlingDebugDisplayFlags & 0x10) && battlingDebugOtHook != NULL) {
            battlingDebugOtHook(battlingCurrentOt);
        }
        printDebugTextOnScreen(battlingCurrentOt);
        resumeBattlingThread(task);
        portBattlingSweepFrame(); /* XG_SWEEP_LOG (port/src/game/battling/sweep.c) */
        releaseBattlingSoundChannels();
        updateDelayedFrees();
        if (battlingScreenClearFlag & 1) {
            AddPrim(battlingCurrentOt, &battlingDisplayBuffer->mE8_clearTile);
        }
        cpu = VSync(1);
        /* divu by zero gives all ones on the R3000; x86 traps */
        fps = g_playTimeInVsync != prev ? 60 / (g_playTimeInVsync - prev) : 0xFFFFFFFF;
        prev = g_playTimeInVsync;
        if (battlingDebugDisplayFlags & 8) {
            showBattlingHeapDump();
        }
        if (battlingDebugDisplayFlags & 1) {
            logToScreen("POLYGON:%4d/%4d\n", fieldPolyCount, fieldPolyCount2);
        }
        if (battlingDebugDisplayFlags & 2) {
            logToScreen("CPU/GPU:%4d/%3d\n", cpu, battlingGpuTime);
        }
        if (battlingDebugDisplayFlags & 4) {
            logToScreen("RATE   : %3dfps\n", fps);
        }
        setDebugFontColor(0xFF, 0xFF, 0xFF);
        skipEmptyBattlingOtEntries(battlingVsyncInterval);
        pcBattling60Wait(); /* Enhanced, 60 Hz: the picture between, inside the wait (between.c) */
        VSync(battlingVsyncInterval);
        pcBattling60WaitDone();
        startBattlingFrameTimer();
        DrawSync(0);
        PutDispEnv(&disp);
        DrawOTagEnv(battlingCurrentOt, &battlingDisplayBuffer->m0_drawEnv);
        goto loop;
}
