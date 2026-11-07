/* Battling overlay, 0x8008BCC8-0x8008C3A8, between the two hand-written
 * objects (0x8008BB00, 0x8008C3A8). */
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

void projectBattlingShadow(ModelBlock* block, SVECTOR* out) {
    VECTOR v;
    VECTOR unused;

    v.vx = -8;
    v.vy = -8;
    v.vz = -0x10;
    VectorNormal(&v, &battlingShadowLightDir);
    battlingShadowLightDir.vx <<= 4;
    battlingShadowLightDir.vy <<= 4;
    battlingShadowLightDir.vz <<= 4;
    projectBattlingShadowVertices(block->m8_vertices, out, block->m2_numVertices);
}

void renderBattlingShadowBlock(ModelBlock* block, u8* prims, OTag* ot, SVECTOR* screenVertices) {
    s32 i;
    u8* mb;
    u8* p;

    i = block->m6_numMeshBlocks;
    currentModelInstanceDrawPrims = prims;
    currentOTEntry = ot;
    currentModelBlockVertices = screenVertices;
    p = block->m10_meshBlocks;
    /* the prim count goes through the parameter's own variable: that ties
     * it to a0 and leaves v1 for p, as in retail */
    block = (ModelBlock*)(uptr)block->m4_numPrims;
    fieldPolyCount2 += (s32)(sptr)block;
    while (g_currentModelBlockSubBlocks = p, --i != -1) {
        mb = g_currentModelBlockSubBlocks;
        g_currentModelBlockSubBlocks += 4;
        if (mb[0] & 8) {
            renderBattlingShadowQuads(g_currentModelBlockSubBlocks, *(s16*)(mb + 2));
        } else {
            renderBattlingShadowTris(g_currentModelBlockSubBlocks, *(s16*)(mb + 2));
        }
        p = g_currentModelBlockSubBlocks + *(s16*)(mb + 2) * 8;
    }
}

void initBattlingShadowModel(BattlingFlatModel* fm, ModelBlock* block) {
    s32 n;
    s32 i;
    s32 j;
    s32 nTri;
    s32 nQuad;
    SVECTOR* v;
    u8* p;
    u8* mb;

    fm->m0_numVertices = block->m2_numVertices;
    fm->m2_numPrims = block->m4_numPrims;
    fm->m4_vertices = block->m8_vertices;
    fm->mC_block = block;
    g_currentModelBlockSubBlocks = block->m10_meshBlocks;
    traceNextAlloc(0x13);
    v = allocateHeap(fm->m0_numVertices * 8, 2);
    n = fm->m0_numVertices;
    fm->m8_screenVertices = v;
    while (--n != -1) {
        v->vy = 0;
        v++;
    }
    traceNextAlloc(5);
    nTri = 0;
    nQuad = 0;
    n = block->m6_numMeshBlocks;
    while (--n != -1) {
        mb = g_currentModelBlockSubBlocks;
        g_currentModelBlockSubBlocks += 4;
        if (mb[0] & 8) {
            nQuad += *(s16*)(mb + 2);
        } else {
            nTri += *(s16*)(mb + 2);
        }
        g_currentModelBlockSubBlocks += *(s16*)(mb + 2) * 8;
    }
    n = nTri * (s32)sizeof(POLY_F3) + nQuad * (s32)sizeof(POLY_F4);
    p = allocateHeap(n * 2, 2);
    fm->m10_prims[0] = p;
    fm->m10_prims[1] = p + n;
    i = block->m6_numMeshBlocks;
    g_currentModelBlockSubBlocks = block->m10_meshBlocks;
    while (--i != -1) {
        mb = g_currentModelBlockSubBlocks;
        g_currentModelBlockSubBlocks += 4;
        if (mb[0] & 8) {
            j = *(s16*)(mb + 2);
            while (--j != -1) {
                setlen(p, 5);
                *(u32*)(p + PRIM_OFS(4)) = 0x28403030;
                p += sizeof(POLY_F4);
            }
        } else {
            j = *(s16*)(mb + 2);
            while (--j != -1) {
                setlen(p, 4);
                *(u32*)(p + PRIM_OFS(4)) = 0x20403030;
                p += sizeof(POLY_F3);
            }
        }
        g_currentModelBlockSubBlocks += *(s16*)(mb + 2) * 8;
    }
    copyBattlingWords(fm->m10_prims[1], fm->m10_prims[0], n);
}

void setBattlingNodeInstance(BattlingNode* node, void* data) {
    node->m0_type = 5;
    node->m4_data = data;
}

BattlingInstance* createBattlingInstance(BattlingNode* src) {
    BattlingInstance* inst;
    s32 type;
    BattlingNode* root;

    traceNextAlloc(7);
    inst = allocateHeap((s32)sizeof(BattlingInstance), 2);
    type = src->m0_type;
    root = battlingShadowRoot;
    inst->m4_source = src;
    inst->mC_flatModel = NULL;
    inst->m0_type = type;
    inst->m8_root = root;
    return inst;
}

void freeBattlingInstance(BattlingInstance* inst) {
    BattlingFlatModel* fm;

    fm = inst->mC_flatModel;
    if (fm != NULL) {
        if (fm->m8_screenVertices != NULL) {
            freeHeap(fm->m8_screenVertices);
        }
        if (fm->m10_prims[0] != NULL) {
            freeHeapDelayed(fm->m10_prims[0], 2);
        }
        freeHeap(fm);
    }
}

BattlingNode* cloneBattlingShadowTree(BattlingNode* src, BattlingNode* parent) {
    BattlingNode* node;
    BattlingInstance* inst;
    ModelBlock* block;

    traceNextAlloc(6);
    node = createBattlingNode();
    inst = createBattlingInstance(src);
    setBattlingNodeInstance(node, inst);
    if (inst->m0_type == 1) {
        block = ((BattlingModel*)src->m4_data)->m14_modelBlock;
        traceNextAlloc(5);
        inst->mC_flatModel = allocateHeap((s32)sizeof(BattlingFlatModel), 2);
        initBattlingShadowModel(inst->mC_flatModel, block);
    }
    battlingShadowNodeCount++;
    if (src->m94_child != NULL) {
        addBattlingNodeChild(node, cloneBattlingShadowTree(src->m94_child, node));
    }
    if (src->m90_next != NULL) {
        addBattlingNodeChild(parent, cloneBattlingShadowTree(src->m90_next, parent));
    }
    return node;
}

BattlingNode* createBattlingShadowTree(BattlingNode* src) {
    battlingShadowNodeCount = 0;
    return cloneBattlingShadowTree(src, NULL);
}

BattlingNode* createBattlingGearShadow(BattlingNode* src) {
    battlingShadowRoot = src;
    return createBattlingShadowTree(src);
}

void renderBattlingShadowTree(BattlingNode* node) {
    BattlingInstance* inst;
    BattlingFlatModel* fm;

    inst = node->m4_data;
    if (inst->m0_type == 1 && !(((BattlingModel*)inst->m4_source->m4_data)->m0_flags & 1)) {
        fm = inst->mC_flatModel;
        renderBattlingShadowBlock(fm->mC_block, fm->m10_prims[battlingBufferIndex], battlingLayerOt + 1, fm->m8_screenVertices);
    }
    if (node->m94_child != NULL) {
        renderBattlingShadowTree(node->m94_child);
    }
    if (node->m90_next != NULL) {
        renderBattlingShadowTree(node->m90_next);
    }
}
