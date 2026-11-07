/*
 * FE status (second half of f_80086D8C.c: 0x8008B45C-0x8008FA38), 92/95 OK.
 * Every OK function matched with gcc-2.7.2-psx -O2 -G0 (also -G8),
 * maspsx --aspsx-version=2.34 (trymatch --as src/field/f_80086D8C.c
 * -I work/agents/FE/include: field.h = repo field.h + FE changes).
 *   8008B45C OPX_6F                                OK
 *   8008B518 OPX_SET_CAMERA_ANGLE                         OK
 *   8008B5D4 OPX_1B                                OK
 *   8008B894 OP_FINALIZE_PARTY_CHARACTER_LOADING          OK
 *   8008B978 runInitScriptForNewlyLoadedPC                                not yet: near-miss (Noah runInitScriptForNewlyLoadedPC); only the register/spill choice differs: retail keeps backupActorId in s7 and spills backupMaxCycles (ours the reverse), which reorders the restore block; C in near_misses.c
 *   8008BC80 OPX_C6                                OK
 *   8008BDD8 OP_ADD_TO_CURRENT_PARTY                      OK
 *   8008BF38 copyPartySlotFromNext                                OK
 *   8008C180 emptyPartySlot                                not yet: near-miss (Noah emptyPartySlot); needs &actorArray in a register (only via an artificial local), then s5/s6 swapped; C in near_misses.c
 *   8008C334 OP_REMOVE_FROM_CURRENT_PARTY                 OK
 *   8008C7D8 OPX_16                                OK
 *   8008C84C OP_FADE_MUSIC                                OK
 *   8008C938 OPX_0F                                OK
 *   8008CA60 OPX_SET_TEMPO                                OK
 *   8008CB4C OPX_11                                OK
 *   8008CC74 OPX_12                                OK
 *   8008CD48 OPX_13                                       OK
 *   8008CDD4 OPX_14                                OK
 *   8008CE64 OP_CLEAR_PARTY_FRAME_MASK                    OK
 *   8008CED0 OP_SET_PARTY_FRAME_MASK                      OK
 *   8008CF3C getCharacter                                OK
 *   8008CF9C OP_SET_DIALOG_PORTRAIT                       OK
 *   8008CFEC OPX_0C                                       OK
 *   8008D078 OPX_09                                       OK
 *   8008D0F4 OP_SET_CURRENT_ACTOR_SCALE                   OK
 *   8008D180 OP_SET_3D_SCALE                              OK
 *   8008D230 OPX_39                                OK
 *   8008D26C OPX_04                                       OK
 *   8008D2E0 writeU16ToScript                                OK
 *   8008D30C isActorNearActorXZ                                OK
 *   8008D380 copyActorPositions                                OK
 *   8008D570 func_8008D570                                OK
 *   8008D5C8 OPX_25                                OK
 *   8008D604 OPX_07                                       OK
 *   8008D684 OPX_SET_FLAG_BIT_FIELD                       OK
 *   8008D700 OPX_0B                                OK
 *   8008D780 OP_FB                                        OK
 *   8008D808 emitScriptJumpToPosition                                OK
 *   8008DA04 emitScriptWalkToPosition                                OK
 *   8008DAFC OPX_49                                OK
 *   8008DB2C OPX_AA                                OK
 *   8008DB68 addPartyGearHP                                OK
 *   8008DBF0 damagePartyGearHP                                OK
 *   8008DC74 OPX_AB                                OK
 *   8008DD6C OPX_AC                                OK
 *   8008DE64 OP_F9                                        OK
 *   8008DEBC OPX_2C                                OK
 *   8008DF44 OPX_2D                                OK
 *   8008DFCC OPX_2E                                OK
 *   8008E054 OPX_2F                                OK
 *   8008E0DC jumpIfMask3                                OK
 *   8008E148 jumpIfMask2                                OK
 *   8008E1B4 OPX_38                                OK
 *   8008E298 OPX_34                                OK
 *   8008E2EC OPX_35                                OK
 *   8008E340 OP_JUMP_IF_ACTOR_LOWER_FLAGS                 OK
 *   8008E394 OP_JUMP_IF_ACTOR_UPPER_FLAGS                 OK
 *   8008E3E8 OPX_30                                OK
 *   8008E414 OPX_31                                OK
 *   8008E440 OPX_32                                OK
 *   8008E46C OPX_33                                OK
 *   8008E498 storeScriptResultToVar                                OK
 *   8008E4EC OPX_28                                OK
 *   8008E518 OPX_29                                OK
 *   8008E544 OPX_2A                                OK
 *   8008E570 OPX_2B                                OK
 *   8008E59C OP_SET_FLAGS_DYNAMIC                         OK
 *   8008E718 setInitialEncounterData                                OK
 *   8008E85C OP_SET_ENCOUNTER_DATA                        OK
 *   8008E8C8 OP_F6                                        OK
 *   8008E9F8 OPX_61                                       OK
 *   8008EA58 OPX_A0                                OK
 *   8008EC30 OP_SET_2D_BACKGROUND                         not yet: near-miss; retail copies the mode value into v1 before masking (move v1,v0; andi v0,0xF); ours keeps it in v0; C in near_misses.c
 *   8008EE14 OPX_67                                OK
 *   8008EF5C OP_TRANSITION_EFFECT_1                       OK
 *   8008EFA0 OP_TRANSITION_EFFECT_2                       OK
 *   8008EFE4 OPX_9E                                OK
 *   8008F070 OP_TRANSITION_EFFECT_3                       OK
 *   8008F0B4 OPX_9A                                       OK
 *   8008F1C8 OPX_5F                                       OK
 *   8008F2D8 OP_SET_TRANSPARENCY_MODE                     OK
 *   8008F348 OPX_SET_SCREEN_BOUNDS_PADDING                OK
 *   8008F394 OPX_8D                                OK
 *   8008F3D0 OPX_8C                                       OK
 *   8008F444 OPX_62                                       OK
 *   8008F4A0 OPX_63                                OK
 *   8008F4FC OPX_65                                       OK
 *   8008F558 OPX_66                                       OK
 *   8008F5E4 OPX_64                                OK
 *   8008F668 OP_PLAY_SOUND_EFFECT                         OK
 *   8008F6AC OPX_5D                                       OK
 *   8008F724 OP_72                                        OK
 *   8008F76C OP_PLAY_MUSIC                                OK
 *   8008F7B8 playMusicFromScript                                OK
 *   8008F90C OP_SETUP_CAMERA_SHAKE                        OK
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "main/sprite.h"
#include "main/sound.h"
#include "field/field.h"
#include "field/screen_effects.h"

extern s16 g_menuOpenArgFromScript;
extern s16 wideImageSpritesVisible;
extern GameState* pKernelGameState;
void freeScriptMarkerSprites(void);
void initScriptMarkerSprites(void);
void drawScriptMarker(s32, s32, s32, s32);
extern s32 fieldExectuteMaxCycles;
extern s32 g_breakCurrentScript;
extern s32 paletteFadeUploadPending;
extern u8* paletteFadeOriginal;
extern u8* paletteFadeBuffer;
extern RECT paletteFadeRect[];
extern u8 D_800B225F[];
extern u8 battlingEntryParams[];
extern s32 fieldMusicLoadPending;
extern s32 playMusicAuthorized;
extern s32 fieldExecuteVar3;
extern s32 loadCompleted;
extern s32 fieldBattleRequested;
extern s32 battlingModeReady;
void setBattlingMainTaskIndex(s32);
/* Byte settings 0x800B2354..0x800B2358: [0] EX 0xD9, [1]/[2] OPX_B8, [3] EX 0xD8,
 * [4] Noah: pauseDisabled. Must be an aggregate: the byte store may alias the script PC. */
extern u8 updateCharacterVar4[];
extern s32 movieScrollingTextEnabled;
extern s32 g_playerControlledActor;
void initDebugCameraDigits(void);
void saveBackBufferRamCopy(void);
void restoreBackBufferRamCopy(void);
s32 getCharacter(s32);
extern s32 particleCreationCharacter;
extern s32 particleCreationCharacterOption;
extern s32 particleCreationCharacterOption2[2];
extern s32 particleCreationSlot[];           /* Noah: particleCreationSlot ([0]; array-typed: retail keeps its address in a register) */
void resetParticleCreationSetup(s32);
void OPX_C8Sub(s32 slot);
extern s32 particleVar1;
extern s32 disableParticles;
s32 createParticleEffect(s32 actorId);
void stopParticleEffect(s32 actorId, s32 slot);
/* Mecha field arguments (EX 0x88 / EX 0x89), indexed by mecha slot */
extern s16 cameraTrackingMode[];   /* EX 0x8A */
extern s32 OPX_86Param;
extern s32 OPX_81Params[3];
extern CVECTOR OPX_82Param0[3]; /* Noah: OPX_82Param0 (sColor) */
extern s16 OPX_82Param3[4];  /* Noah: OPX_82Param3[3], [3] = OPX_82Param4 */
extern s32 fieldMovieCurrentFrame;
extern void* scriptTimBuffer;
extern s16 linkOTIndex[];
s32 isLoadCompleted(void);
void func_8003633C(u8 x);
extern s32 currentParty[3];  /* Noah: currentParty (main) */
extern s32 fieldMapNumber;     /* Noah: fieldMapNumber (main) */
extern s32 fieldExecuteVar1;
extern void* asyncPartyCharacterLoadingBuffer;
extern s32 asyncLoadingVar1;
extern s32 asyncPartyCharacterLoadingCharacterIndex;
extern s32 asyncPartyCharacterLoadingIndex;
extern s32 load2dAnimVar;
void spriteActorSetSpecialAnimation(SpriteActorCore* actor, void* animation);
void OPX_4D(void);
extern s32 mechaOverlayLoadingMode;
extern void* scriptWdsBuffer;
extern s32 scriptWdsFileIndex;
extern s32 scriptWdsSlot;
extern MATRIX mechaFieldArgs2[2] PORT_ALIAS;
extern SVECTOR fieldObjectRenderingVar2;
void setupRGBCalcMode(s32, s32, s32, s32, s32, s32);
void setupScreenDistortion(s32);
void setScreenDistortionParams(s32, s32, s32, s32, s32, s32, s32);
void freeScreenDistortion(void);
/* ---- FE: declarations for the second half (0x8008B45C-0x8008FA38) ---- */
/* Field bss aggregates (field.h): retail accesses actorArray, the party /
 * camera / encounter globals as struct members (CSE reloads, unreordered
 * stores), so this half goes through fieldSceneState / op9DVar1. */
extern FieldBss_800AF880 fieldSceneState;
extern s16 fieldCameraShakeCounter; /* fieldSceneState.m1AA_cameraShakeCounter, addressed on its own (retail codegen) */
extern FieldBss_800B2184 op9DVar1;
extern SVECTOR computeProjectionMatrixAngles;
extern s32 OPX8E_param0;          /* Noah: screen bounds padding (EX 0x8E) */
extern s32 OPX8E_param1;
extern s16 partySlotSelectionMasks[];        /* party-slot masks for EX 0xAB/0xAC */
extern s32 fieldMovieStarted;
extern u8* fieldActorSetupParams;          /* Noah: fieldActorSetupParams (= fieldSceneState.m29C; scalar access here) */
extern s32 g_frameOddOrEven;          /* prim double-buffer index */
extern sFieldEntity* pCurrentFieldEntity;
extern s32 currentScriptFinished;
extern s32 partyGearActorIds[];
extern sPartyCharacterBuffer* partyCharacterBuffers[];  /* Noah: partyCharacterBuffersRaw */
extern s32 fullScreenTransitionEffectRenderParam;
extern s32 fullScreenTransitionEffectRenderMode;
extern s32 fieldMovieFlags;
extern s32 onlyUpdateDirector;
extern s32 iRam800adb70;
extern s32 updateMusicState2Var3;
extern s32 currentlyPlayingMusic;
extern s32 musicVar1;
extern s32 musicVar2;
void OPX_13Sub(s32 actor);
void updateScriptActor3dRotation(s32 actor);
void setFieldDrawEnvClip(s32 a, s32 b, s32 c, s32 d);
s32 length2d(s32 x, s32 y);
void setMusicInstancePitch(void* inst, s32 val, s32 dur);
void setMusicInstanceTempo(void* inst, s32 val, s32 dur);
void setMusicInstancePan(void* inst, s32 val, s32 dur);
void setMusicInstanceTrackMuteMask(void* inst, s32 val);
void setTransparencyMode(SpriteActorCore* actor, u32 mode);
void fadeSoundEffectChannelVolume(s32 a, s32 b, s32 c);
void setSoundEffectChannelVolume(s32 a, s32 b);
void setSoundEffectChannelPan(s32 a, s32 b);
void playFieldSoundEffect(s32 a, s32 b);
s32 getPlayingSoundEffectMask(s32 a);
void resetBattleAudioState(void);
void clearMusic2(void);
/* Retail passes playMusic a second argument, which it ignores. */
#define playMusic(music, unused) playMusic(music)
void playMusicFromScript(void);
void runInitScriptForNewlyLoadedPC(s32 character);
void resetFieldScriptEntityValues(s32 actor);
void executeFieldScript(s32 maxCycles);
void copyActorPositions(s32 dest, s32 source);
void doPCCollisionCheckAfterLoading(void);
void resetFieldScriptEntityGraphicEntity(s32 actor, s32 a1, void* setup, s32 a3, s32 a4, s32 a5, s32 a6);
s32 findFreePartySlot(s32 character, s32* slot);
void startPartyCharacterASyncLoading(s32 character, s32 slot);
s32 findCharacterInParty(s32 character);
void copyPartySlotFromNext(s32 slot);
void emptyPartySlot(s32 slot);
void jumpIfMask3(u16 mask);
void jumpIfMask2(u16 mask);                 /* Noah: jumpIfMask */
void storeScriptResultToVar(u16 value);
void setInitialEncounterData(void);
void writeU16ToScript(s32 value, s32 offset);
s32 addPartyGearHP(s32 slot, u32 amount);
s32 damagePartyGearHP(s32 slot, u32 amount);

/* Resets both display environments' screen rectangles (0, 10, 256, 216). */
void setupFieldDisplayEnv(void) {
    fieldRenderContext[0].mB8_displayEnv.screen.x = 0;
    fieldRenderContext[0].mB8_displayEnv.screen.y = 10;
    fieldRenderContext[0].mB8_displayEnv.screen.w = 256;
    fieldRenderContext[0].mB8_displayEnv.screen.h = 216;
    fieldRenderContext[1].mB8_displayEnv.screen.x = 0;
    fieldRenderContext[1].mB8_displayEnv.screen.y = 10;
    fieldRenderContext[1].mB8_displayEnv.screen.w = 256;
    fieldRenderContext[1].mB8_displayEnv.screen.h = 216;
}

void OP_SET_PAUSE_DISABLED(void) {
    updateCharacterVar4[4] = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

/* Opcode EX 0xDF: 0 = clear VRAM and reset the draw/display environments,
 * 1/2 = set/clear wideImageSpritesVisible. */
void OPX_DF(void) {
    RECT rect;

    switch (getImmediateOrVariableUnsigned(1)) {
    case 0:
        rect.x = 0;
        rect.y = 0;
        rect.w = 0x500;
        rect.h = 0x200;
        ClearImage(&rect, 0, 0, 0);
        DrawSync(0);
        VSync(0);
        SetDefDrawEnv(&fieldRenderContext[0].m0_drawEnv, 0, 0, 0x280, 0xE0);
        SetDefDrawEnv(&fieldRenderContext[1].m0_drawEnv, 0, 0x100, 0x280, 0xE0);
        SetDefDispEnv(&fieldRenderContext[0].mB8_displayEnv, 0, 0x100, 0x280, 0xE0);
        SetDefDispEnv(&fieldRenderContext[1].mB8_displayEnv, 0, 0, 0x280, 0xE0);
        setupFieldDisplayEnv();
        break;
    case 1:
        wideImageSpritesVisible = 1;
        break;
    case 2:
        wideImageSpritesVisible = 0;
        break;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Opcode EX 0xC5 */
void OPX_C5(void) {
    s32 a = getImmediateOrVariableUnsigned(1);
    s32 b = getImmediateOrVariableUnsigned(3);
    pCurrentFieldScriptActor->m128_attachedMechaBone = (a << 12) | b;
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

/* Opcode EX 0xD4 */
void OPX_D4(void) {
    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) {
    case 0:
        initScriptMarkerSprites();
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
        break;
    case 2:
        freeScriptMarkerSprites();
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
        break;
    case 1:
        drawScriptMarker(getImmediateOrVariableUnsigned(2), getImmediateOrVariableUnsigned(4), getImmediateOrVariableUnsigned(6), getImmediateOrVariableUnsigned(8));
        pCurrentFieldScriptActor->mCC_scriptPC += 10;
        break;
    case 3:
        setScriptMarkerColor(getImmediateOrVariableUnsigned(2), getImmediateOrVariableUnsigned(4), getImmediateOrVariableUnsigned(6), getImmediateOrVariableUnsigned(8));
        pCurrentFieldScriptActor->mCC_scriptPC += 10;
        break;
    }
}

/* Opcode EX 0xDE */
void OPX_DE(void) {
    s32 a = getImmediateOrVariableUnsigned(1);
    s32 b = getImmediateOrVariableUnsigned(3);
    pKernelGameState->m16C0_abilities[a].m1A_unlockedGearAbilities |= b;
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

/* Opcode EX 0xDD: save (0) / restore (1) / free (2) a 256-pixel-wide VRAM band
 * kept in two heap buffers. h is shared by cases 0 and 1 (global pseudo),
 * y is per case: that is what gives retail's register assignment. */
void OPX_DD(void) {
    s32 h;
    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) {
    case 0:
        fieldExectuteMaxCycles += 0x20;
        {
        s32 y = getImmediateOrVariableUnsigned(2);
        h = getImmediateOrVariableUnsigned(4);
        paletteFadeOriginal = allocateHeap(PC_HEAP_BYTES(h << 9), 0);
        paletteFadeBuffer = allocateHeap(PC_HEAP_BYTES(h << 9), 0);
        paletteFadeRect[0].x = 0;
        paletteFadeRect[0].y = y;
        paletteFadeRect[0].w = 0x100;
        paletteFadeRect[0].h = h;
        StoreImage(paletteFadeRect, (u32*)paletteFadeOriginal);
        }
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC += 6;
        break;
    case 1:
        fieldExectuteMaxCycles += 0x20;
        {
        s32 y = getImmediateOrVariableUnsigned(2);
        h = getImmediateOrVariableUnsigned(4);
        fadePaletteColors(0x100, h, (u16*)(paletteFadeBuffer + y * 0x200), (u16*)(paletteFadeOriginal + y * 0x200));
        }
        paletteFadeUploadPending = 1;
        pCurrentFieldScriptActor->mCC_scriptPC += 6;
        break;
    case 2:
        freeHeap(paletteFadeOriginal);
        freeHeap(paletteFadeBuffer);
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
        break;
    case 3:
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
        g_breakCurrentScript = 1;
        break;
    }
}

/* Opcode EX 0xDC */
void OPX_DC(void) {
    s32 index = getImmediateOrVariableUnsigned(1);
    D_800B225F[index] = getImmediateOrVariableUnsigned(3);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

/* Opcode EX 0xD3: scale two values by ratios and store them in variables. */
void OPX_D3(void) {
    s32 a = getImmediateOrVariableUnsigned(1);
    s32 b = getImmediateOrVariableUnsigned(3);
    s32 c = getImmediateOrVariableUnsigned(5);
    s32 d = getImmediateOrVariableUnsigned(7);
    s32 e = getImmediateOrVariableUnsigned(9);
    s32 f = getImmediateOrVariableUnsigned(0xB);
    s32 x = (R3000_DIV(e << 16, c) * a) >> 16;
    s32 y = (R3000_DIV(f << 16, d) * b) >> 16;

    setVar(readU16FromScript(0xD), x);
    setVar(readU16FromScript(0xF), y);
    pCurrentFieldScriptActor->mCC_scriptPC += 0x11;
}

void OPX_D2(void) {
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Opcode EX 0xD1 */
void OPX_D1(void) {
    pKernelGameState->m22B6_gameFlags |= 0x4000;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

/* Opcode EX 0xE1: copy gear slot arg1 to gear slot arg3. */
void OPX_E1(void) {
    s32 src = getImmediateOrVariableUnsigned(1);
    s32 dst = getImmediateOrVariableUnsigned(3);
    pKernelGameState->m978_gears[dst] = pKernelGameState->m978_gears[src];
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

/* Opcode EX 0xD0: copy party character arg1 (stats and abilities) to slot arg3. */
void OPX_D0(void) {
    s32 src = getImmediateOrVariableUnsigned(1);
    s32 dst = getImmediateOrVariableUnsigned(3);
    GameState* gs;
    pKernelGameState->m26C_party[dst] = pKernelGameState->m26C_party[src];
    pKernelGameState->m16C0_abilities[dst] = pKernelGameState->m16C0_abilities[src];
    gs = pKernelGameState;
    if (dst == 9) {
        gs->m22B6_gameFlags |= 0x2000;
    }
    if (dst == 10) {
        pKernelGameState->m22B6_gameFlags |= 0x1000;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

/* Opcode EX 0xC0 */
void OPX_C0(void) {
    setVar(readU16FromScript(1), battlingEntryParams[6]);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Opcode EX 0xBF */
void OPX_BF(void) {
    if (playMusicAuthorized == 0 || fieldExecuteVar3 == 0 || loadCompleted != 0 || fieldMusicLoadPending == -1) {
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
    } else {
        setBattlingMainTaskIndex(0);
        battlingEntryParams[0] = getImmediateOrVariableUnsigned(1);
        battlingEntryParams[1] = getImmediateOrVariableUnsigned(3);
        battlingEntryParams[2] = getImmediateOrVariableUnsigned(5);
        battlingEntryParams[3] = getImmediateOrVariableUnsigned(7);
        battlingEntryParams[4] = getImmediateOrVariableUnsigned(9);
        battlingEntryParams[5] = getImmediateOrVariableUnsigned(0xB);
        fieldBattleRequested = 1;
        battlingModeReady = 0;
        pCurrentFieldScriptActor->mCC_scriptPC += 0xD;
    }
}

/* Opcode EX 0xD5 */
void OPX_D5(void) {
    setVar(readU16FromScript(1), pKernelGameState->m1844_waypointShipPosition[0]);
    setVar(readU16FromScript(3), pKernelGameState->m1844_waypointShipPosition[1]);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

/* Opcode EX 0xD6 */
void OPX_D6(void) {
    setVar(readU16FromScript(1), pKernelGameState->m184E_flyingShipPosition[0]);
    setVar(readU16FromScript(3), pKernelGameState->m184E_flyingShipPosition[2]);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

/* Opcode EX 0xD8 */
void OPX_D8(void) {
    updateCharacterVar4[3] = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

/* Opcode EX 0xD9 */
void OPX_D9(void) {
    updateCharacterVar4[0] = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

/* Opcode EX 0xD7 */
void OPX_D7(void) {
    pKernelGameState->m184E_flyingShipPosition[0] = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    pKernelGameState->m184E_flyingShipPosition[2] = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    pKernelGameState->m184E_flyingShipPosition[3] = 0;
    pKernelGameState->m184E_flyingShipPosition[1] = 0;
    pKernelGameState->m184E_flyingShipPosition[4] = 1;
    pCurrentFieldScriptActor->mCC_scriptPC += 6;
}

/* Opcode EX 0xB9 */
void OPX_B9(void) {
    setVar(readU16FromScript(1), pKernelGameState->m182C_vehiclePosition[0]);
    setVar(readU16FromScript(3), pKernelGameState->m182C_vehiclePosition[1]);
    setVar(readU16FromScript(5), pKernelGameState->m182C_vehiclePosition[2]);
    setVar(readU16FromScript(7), pKernelGameState->m182C_vehiclePosition[3]);
    pCurrentFieldScriptActor->mCC_scriptPC += 9;
}

/* Opcode EX 0xBE */
void OPX_BE(void) {
    movieScrollingTextEnabled = 1;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

/* Opcode EX 0xBA */
void OPX_BA(void) {
    pKernelGameState->m182C_vehiclePosition[0] = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    pKernelGameState->m182C_vehiclePosition[1] = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    pKernelGameState->m182C_vehiclePosition[2] = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    pKernelGameState->m182C_vehiclePosition[3] = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    pCurrentFieldScriptActor->mCC_scriptPC += 10;
}

/* Opcode EX 0xBB */
void OPX_BB(void) {
    setVar(readU16FromScript(1), pKernelGameState->m1834_vehicleMode);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Opcode EX 0xBC */
void OPX_BC(void) {
    pKernelGameState->m1834_vehicleMode = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]);
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

void OPX_B8(void) {
    s32 value = getImmediateOrVariableUnsigned(2);
    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] == 0) {
        updateCharacterVar4[1] = value;
    } else {
        updateCharacterVar4[2] = value;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

/* Opcode EX 0xB7 */
void OPX_B7(void) {
    op9DVar1.m1C8_gearSwitchOverride = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Opcode EX 0xB6: make actor arg1 the player-controlled actor (flag 0x4000).
 * The flag words are written through a plain u32 pointer (not a struct
 * member): retail reloads actorArray in the loop, so the store may alias it. */
void OPX_B6(void) {
    s32 actor = readCharacter(1);
    s32 i;
    s32 n;

    if (actor != 0xFF) {
        if (actor == partyFieldActorIds[0]) {
            op9DVar1.m1CA_playerIsntPartyLeader = 0;
        } else {
            op9DVar1.m1CA_playerIsntPartyLeader = 1;
        }
        g_playerControlledActor = actor;
        op9DVar1.m1BA_actorCameraTracked = actor;
        n = g_totalActors;
        for (i = 0; i < n; i++) {
            *(u32*)fieldSceneState.m290_actorArray[i].m4C_scriptEntity &= 0xFEFFBFFF;
        }
        *(u32*)fieldSceneState.m290_actorArray[actor].m4C_scriptEntity |= 0x4000;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OPX_B5(void) {
    op9DVar1.m1C4_warpPartyRequest++;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

/* Opcode EX 0xB1 */
void OPX_B1(void) {
    initDebugCameraDigits();
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OPX_AF(void) {
    MATRIX m;
    MATRIX m2;
    SVECTOR in;
    SVECTOR out;
    s32 flag;
    s32 a;

    m.t[0] = m.t[1] = m.t[2] = 0;
    /* An empty gear slot (a cold start into field 537 runs this before any
     * gear is loaded): getMechaBoneMatrix leaves m untouched and retail
     * rotates the point by whatever the stack held, the same on every PS1
     * run but host pointers here (xg-9di). Identity: the point passes
     * through unchanged. */
    m.m[0][0] = m.m[1][1] = m.m[2][2] = 0x1000;
    m.m[0][1] = m.m[0][2] = m.m[1][0] = m.m[1][2] = m.m[2][0] = m.m[2][1] = 0;
    a = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xB]);
    getMechaBoneMatrix(&m, &m2, a, getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xB]));
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    in.vx = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xB]);
    in.vy = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xB]);
    in.vz = getVar08(9, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xB]);
    RotTransSV(&in, &out, &flag);
    setVar(readU16FromScript(0xC), out.vx);
    setVar(readU16FromScript(0xE), out.vy);
    setVar(readU16FromScript(0x10), out.vz);
    pCurrentFieldScriptActor->mCC_scriptPC += 0x12;
}

void OP_RESTORE_GEAR(void) {
    s32 i;

    for (i = 0; i < 20; i++) {
        pKernelGameState->m978_gears[i].m60_hp = pKernelGameState->m978_gears[i].m64_maxHp;
        pKernelGameState->m978_gears[i].m38_fuel = pKernelGameState->m978_gears[i].m3A_maxFuel;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

/* Opcode EX 0xA3 */
void OPX_A3(void) {
    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] == 0) {
        saveBackBufferRamCopy();
    } else {
        restoreBackBufferRamCopy();
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OPX_A2(void) {
    if (fieldMusicLoadPending == -1) {
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC += 1;
    }
    g_breakCurrentScript = 1;
}

/* Opcode EX 0xC7: store the gear number of party character arg1 (0xFF if absent). */
void OPX_C7(void) {
    s32 c = getCharacter(getImmediateOrVariableUnsigned(1));

    if (c != 0xFF) {
        setVar(readU16FromScript(3), pKernelGameState->m26C_party[c].mA0_gearNum);
    } else {
        setVar(readU16FromScript(3), 0xFF);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_ASSIGN_GEAR(void) {
    s32 c = getImmediateOrVariableUnsigned(1);
    pKernelGameState->m26C_party[c].mA0_gearNum = getImmediateOrVariableUnsigned(3);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_TOGGLE_FRAME_LOCK(void) {
    s32 c = getCharacter(getImmediateOrVariableUnsigned(2));

    if (c != 0xFF) {
        if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] == 0) {
            pKernelGameState->m2318_partyFrameLock |= 1 << c;
        } else {
            pKernelGameState->m2318_partyFrameLock &= ~(1 << c);
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

void OP_SET_MENU_OPEN_ARG(void) {
    u8 param = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    g_menuOpenArgFromScript = param ^ 1;
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OPX_98(void) {
    op9DVar1.m28_soundMaxDistance = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Opcode EX 0xC1: read the sprite state of party member arg5. */
void OPX_C1(void) {
    s32 actor = partyFieldActorIds[getImmediateOrVariableUnsigned(5)];
    SpriteActorCore* sprite;

    fieldExectuteMaxCycles += 4;
    if (actor != 0xFF) {
        sprite = fieldSceneState.m290_actorArray[actor].m4_pVramSpriteSheet;
        setVar(readU16FromScript(1), sprite->m7C_extra->mC_animationSignal);
        setVar(readU16FromScript(3), actor);
        if (sprite->m7C_extra->mC_animationSignal != 1) {
            sprite->m7C_extra->mC_animationSignal = 0;
        }
    } else {
        setVar(readU16FromScript(1), 0);
        setVar(readU16FromScript(3), 0);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

/* Clears the 8 XY pairs of the current particle slot (Noah: resetParticleXY;
 * Noah has m30[2] where retail indexes m30[i]). */
void resetParticleXY(void) {
    s32 i;

    for (i = 0; i < 8; i++) {
        particleCreationSetup.m0_configs[particleCreationSlot[0]].m30_angleOffsets[i].vx = particleCreationSetup.m0_configs[particleCreationSlot[0]].m30_angleOffsets[i].vy = 0;
    }
}

/* Opcode EX 0xC2: like OP_RESET_PARTICLE_CONFIGS with an immediate/variable character. */
void OPX_C2(void) {
    s32 character = getImmediateOrVariableUnsigned(1);

    if (character == 0xFF) {
        character = 0;
    }
    particleCreationCharacter = getImmediateOrVariableUnsigned(1);
    particleCreationCharacterOption = getImmediateOrVariableUnsigned(3);
    particleCreationCharacterOption2[0] = getImmediateOrVariableUnsigned(5);
    particleCreationCharacterOption2[1] = getImmediateOrVariableUnsigned(7);
    pCurrentFieldScriptActor->mCC_scriptPC += 9;
    resetParticleCreationSetup(character);
    switch (particleCreationCharacterOption) {
    case 0:
        particleCreationCharacterOption = 0;
        break;
    case 1:
        particleCreationCharacterOption = 0x10;
        break;
    case 2:
        particleCreationCharacterOption = 0x20;
        break;
    case 3:
        particleCreationCharacterOption = 0x30;
        break;
    }
    fieldExectuteMaxCycles += 4;
}

void OP_RESET_PARTICLE_CONFIGS(void) {
    s32 character = readCharacter(1);

    if (character == 0xFF) {
        character = 0;
    }
    particleCreationCharacter = character;
    particleCreationCharacterOption = getImmediateOrVariableUnsigned(2);
    particleCreationCharacterOption2[0] = getImmediateOrVariableUnsigned(4);
    particleCreationCharacterOption2[1] = getImmediateOrVariableUnsigned(6);
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
    resetParticleCreationSetup(character);
    switch (particleCreationCharacterOption) {
    case 0:
        particleCreationCharacterOption = 0;
        break;
    case 1:
        particleCreationCharacterOption = 0x10;
        break;
    case 2:
        particleCreationCharacterOption = 0x20;
        break;
    case 3:
        particleCreationCharacterOption = 0x30;
        break;
    }
    fieldExectuteMaxCycles += 4;
}

void OPX_A6(void) {
    SpriteActorCore* sprite = fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet;
    s32 a = (getImmediateOrVariableUnsigned(1) & 0xF) << 6;
    s32 b = ((getImmediateOrVariableUnsigned(1) >> 4) << 8) + getImmediateOrVariableUnsigned(3);
    allocateSpriteVramLocations(sprite, 2, 0);
    pCurrentFieldScriptActor->m12C_x12 = (sprite->m7C_extra->m18_vramLocations[2] = a);
    pCurrentFieldScriptActor->m130_x0 = (sprite->m7C_extra->m18_vramLocations[3] = b);
    pCurrentFieldScriptActor->m12C_x10 = 1;
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OPX_A7(void) {
    SpriteActorCore* sprite = fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet;
    s32 a = (getImmediateOrVariableUnsigned(1) & 0xF) << 6;
    s32 b = ((getImmediateOrVariableUnsigned(1) >> 4) << 8) + getImmediateOrVariableUnsigned(3);
    s32 c = (getImmediateOrVariableUnsigned(5) & 0xF) << 6;
    s32 d = ((getImmediateOrVariableUnsigned(5) >> 4) << 8) + getImmediateOrVariableUnsigned(7);

    allocateSpriteVramLocations(sprite, 3, 0);
    pCurrentFieldScriptActor->m12C_x12 = (sprite->m7C_extra->m18_vramLocations[2] = a);
    pCurrentFieldScriptActor->m130_x0 = (sprite->m7C_extra->m18_vramLocations[3] = b);
    pCurrentFieldScriptActor->m130_x9 = (sprite->m7C_extra->m18_vramLocations[4] = c);
    pCurrentFieldScriptActor->m130_x13 = (sprite->m7C_extra->m18_vramLocations[5] = d);
    pCurrentFieldScriptActor->mCC_scriptPC += 9;
    pCurrentFieldScriptActor->m12C_x10 = 2;
}

void OPX_BD(void) {
    u16 flags = 0;

    switch (getImmediateOrVariableUnsigned(1)) {
    case 1:
        flags = 0x80;
        break;
    case 2:
        flags = 0x40;
        break;
    }
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m2A_flags |= flags;
    fieldExectuteMaxCycles += 4;
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OP_SETUP_PARTICLE_ANGLE(void) {
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m24_speedMultiplier = getImmediateOrVariableUnsigned(1);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m2A_flags |= getImmediateOrVariableUnsigned(3) << 8;
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m76_rotationAngle = getImmediateOrVariableUnsigned(5);
    fieldExectuteMaxCycles += 4;
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OPX_C8(void) {
    OPX_C8Sub(0);
}

/* Opcode EX 0xC9: OPX_C8 for XY pairs 4..7. */
void OPX_C9(void) {
    OPX_C8Sub(4);
}

/* Noah: OPX_C8Sub. Reads 8 values (flags byte at +0x11) into 4 XY pairs from pair `slot`. */
void OPX_C8Sub(s32 slot) {
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m30_angleOffsets[slot].vx = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m30_angleOffsets[slot].vy = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m30_angleOffsets[slot + 1].vx = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m30_angleOffsets[slot + 1].vy = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m30_angleOffsets[slot + 2].vx = getVar08(9, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m30_angleOffsets[slot + 2].vy = getVar04(0xB, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m30_angleOffsets[slot + 3].vx = getVar02(0xD, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m30_angleOffsets[slot + 3].vy = getVar01(0xF, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    fieldExectuteMaxCycles += 4;
    pCurrentFieldScriptActor->mCC_scriptPC += 0x12;
}

void OP_SETUP_PARTICLE(void) {
    particleCreationSlot[0] = getImmediateOrVariableUnsigned(1);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m24_speedMultiplier = 1;
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m52_actorId = particleCreationCharacter;
    particleVar1 = particleCreationSetup.m0_configs[particleCreationSlot[0]].m52_actorId;
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m0 = 0;
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m76_rotationAngle = 0;
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m6_maxParticles = getImmediateOrVariableUnsigned(3);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m2_sWait = getImmediateOrVariableUnsigned(5);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m4_eWait = getImmediateOrVariableUnsigned(7);
    resetParticleXY();
    fieldExectuteMaxCycles += 4;
    pCurrentFieldScriptActor->mCC_scriptPC += 9;
}

void OP_SETUP_PARTICLE_POSITION(void) {
    particleCreationSetup.m0_configs[particleCreationSlot[0]].mC_sPos.vx = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].mC_sPos.vy = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].mC_sPos.vz = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m14_ePos.vx = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m14_ePos.vy = getVar08(9, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m14_ePos.vz = getVar04(0xB, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    fieldExectuteMaxCycles += 4;
    pCurrentFieldScriptActor->mCC_scriptPC += 0xE;
}

void OP_SETUP_LIFE1(void) {
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m8_speed = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m1C_gravity.vx = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m1C_gravity.vy = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m1C_gravity.vz = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m26_sRange = getVar08(9, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m28_eRange = getVar04(0xB, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    fieldExectuteMaxCycles += 4;
    pCurrentFieldScriptActor->mCC_scriptPC += 0xE;
}

void OP_SETUP_LIFE2(void) {
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m56_psWait = getImmediateOrVariableUnsigned(1);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m58_peWait = getImmediateOrVariableUnsigned(3);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m54_shape = getImmediateOrVariableUnsigned(5);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m2A_flags = getImmediateOrVariableUnsigned(7) | (getImmediateOrVariableUnsigned(9) << 1) | particleCreationCharacterOption;
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m72_mechaIndex = particleCreationCharacterOption2[0];
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m74_boneIndex = particleCreationCharacterOption2[1];
    fieldExectuteMaxCycles += 4;
    pCurrentFieldScriptActor->mCC_scriptPC += 0xB;
}

void OP_SETUP_SCALE(void) {
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m5A_scale.vx = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m5A_scale.vy = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m5A_scale.vz = 0;
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m62_scaleOffset.vx = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m62_scaleOffset.vy = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m62_scaleOffset.vz = 0;
    fieldExectuteMaxCycles += 4;
    pCurrentFieldScriptActor->mCC_scriptPC += 0xA;
}

void OP_SETUP_PARTICLE_COLOR(void) {
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m6A_color[0] = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m6A_color[1] = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m6A_color[2] = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m6E_colorOffset[0] = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m6E_colorOffset[1] = getVar08(9, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    particleCreationSetup.m0_configs[particleCreationSlot[0]].m6E_colorOffset[2] = getVar04(0xB, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    fieldExectuteMaxCycles += 4;
    pCurrentFieldScriptActor->mCC_scriptPC += 0xE;
}

void OP_CREATE_PARTICLE_EFFECT_FOR_CURRENT_ACTOR(void) {
    fieldExectuteMaxCycles += 4;
    if (disableParticles == 0) {
        createParticleEffect(currentFieldActorId);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OPX_97(void) {
    fieldExectuteMaxCycles += 4;
    stopParticleEffect(currentFieldActorId, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]);
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

/* Opcode EX 0x8B: store the party slot of the current actor (0xFF if none). */
void OPX_8B(void) {
    s32 i;

    for (i = 0; i < 3; i++) {
        if (partyFieldActorIds[i] == currentFieldActorId) {
            setVar(readU16FromScript(1), i);
            goto end;
        }
    }
    setVar(readU16FromScript(1), 0xFF);
end:
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_SETUP_MECHA_FIELD_ARGS(void) {
    s32 i = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);

    op9DVar1.m164_lightNearColors[i].vx = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    op9DVar1.m164_lightNearColors[i].vy = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    op9DVar1.m164_lightNearColors[i].vz = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    op9DVar1.m17C_lightFarColors[i].vx = getVar08(9, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    op9DVar1.m17C_lightFarColors[i].vy = getVar04(0xB, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    op9DVar1.m17C_lightFarColors[i].vz = getVar02(0xD, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    op9DVar1.m194_lightRadii[i] = getVar01(0xF, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    pCurrentFieldScriptActor->mCC_scriptPC += 0x12;
}

void OPX_89(void) {
    s32 i = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    s32 actor;

    op9DVar1.m1A0_lightPositions[i].vx = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    op9DVar1.m1A0_lightPositions[i].vz = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    op9DVar1.m1A0_lightPositions[i].vy = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    actor = readCharacter(0xA);
    if (actor != 0xFF) {
        op9DVar1.m15E_lightActorIds[i] = actor;
    } else {
        op9DVar1.m15E_lightActorIds[i] = -1;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 0xB;
}

/* Opcode EX 0x8A */
void OPX_8A(void) {
    cameraTrackingMode[0] = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Opcode EX 0x70 */
void OPX_70(void) {
    op9DVar1.m4E_objectMoveFlags = getImmediateOrVariableUnsigned(1) - 0x80;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_86(void) {
    OPX_86Param = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OPX_80(void) {
    OPX_80Params.m0_OPX_80Params[0] = readU16FromScript(1);
    OPX_80Params.m0_OPX_80Params[1] = readU16FromScript(3);
    OPX_80Params.m0_OPX_80Params[2] = readU16FromScript(5);
    if (OPX_80Params.m0_OPX_80Params[2] == 0) {
        OPX_80Params.m0_OPX_80Params[2]++;
    }
    OPX_80Params.m0_OPX_80Params[3] = readU16FromScript(7);
    OPX_80Params.m0_OPX_80Params[4] = 0;
    OPX_80Params.m0_OPX_80Params[5] = readU16FromScript(9);
    OPX_80Params.m0_OPX_80Params[6] = readU16FromScript(0xB);
    OPX_80Params.m0_OPX_80Params[7] = readU16FromScript(0xD);
    pCurrentFieldScriptActor->mCC_scriptPC += 0xF;
}

void OPX_81(void) {
    OPX_81Params[0] = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    OPX_81Params[2] = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    OPX_81Params[1] = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

void OPX_82(void) {
    OPX_82Param0[0].r = getImmediateOrVariableUnsigned(1);
    OPX_82Param0[0].g = getImmediateOrVariableUnsigned(3);
    OPX_82Param0[0].b = getImmediateOrVariableUnsigned(5);
    OPX_82Param0[1].r = getImmediateOrVariableUnsigned(7);
    OPX_82Param0[1].g = getImmediateOrVariableUnsigned(9);
    OPX_82Param0[1].b = getImmediateOrVariableUnsigned(0xB);
    OPX_82Param0[2].r = getImmediateOrVariableUnsigned(0xD);
    OPX_82Param0[2].g = getImmediateOrVariableUnsigned(0xF);
    OPX_82Param0[2].b = getImmediateOrVariableUnsigned(0x11);
    OPX_82Param3[0] = getImmediateOrVariableUnsigned(0x13);
    OPX_82Param3[1] = getImmediateOrVariableUnsigned(0x15);
    OPX_82Param3[2] = getImmediateOrVariableUnsigned(0x17);
    pCurrentFieldScriptActor->mCC_scriptPC += 0x19;
    OPX_82Param3[3] = 1;
}

void OPX_7F(void) {
    if (fieldBattleRequested == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 1;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
    }
    g_breakCurrentScript = 1;
}

void OPX_85(void) {
    setVar(readU16FromScript(1), fieldMovieCurrentFrame);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Opcode EX 0x77: 0 = load file 0x7FB+n, 1 = upload it as a TIM, else free it. */
void OPX_77(void) {
    s32 file;
    s32 v;
    s32 n;
    s32 clut;
    s32 x;
    s32 y;

    if (isLoadCompleted() == -1) {
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
        return;
    }
    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] == 0) {
        n = getVar80(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
        setCurrentDirectory(4, 0);
        file = n + 0x7FB;
        scriptTimBuffer = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(file)), 0);
        readFile(file, scriptTimBuffer, 0, 0x80);
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
    } else if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] == 1) {
        v = getVar10(8, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xA]);
        clut = v + 0xE8;
        if (v == 0xFF) {
            clut = -1;
        }
        x = getVar40(4, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xA]);
        y = getVar20(6, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xA]);
        if (y >= 0x100 && x >= 0x2C0) {
            saveBackBufferRamCopy();
        }
        loadTimToVram(scriptTimBuffer, x, y, 0, clut, 0, 0);
        pCurrentFieldScriptActor->mCC_scriptPC += 0xB;
    } else {
        freeHeap(scriptTimBuffer);
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
    }
    g_breakCurrentScript = 1;
}

void OPX_7A(void) {
}

void OPX_79(void) {
}

void OPX_78(void) {
}

void OPX_unused(void) {
}

void OPX_7C(void) {
}

void OPX_7D(void) {
}

void OPX_7E(void) {
}

void OPX_7B(void) {
}

/* Waits (VSync) until isLoadCompleted reports the CD idle. */
void waitForLoadCompleted(void) {
    while (isLoadCompleted() != 0) {
        VSync(0);
    }
}

/* Returns -1 while a load is pending or the CD is busy, else completes the read and returns 0. */
s32 isLoadCompleted(void) {
    if (loadCompleted != 0) {
        return -1;
    }
    if (isCDBusy() != 0) {
        return -1;
    }
    waitReadCompletion(0);
    return 0;
}

/* Opcode EX 0x6C */
void OPX_6C(void) {
    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] == 0) {
        func_8003633C(0);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OP_SET_LINK_OT(void) {
    linkOTIndex[0] = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_6B(void) {
    s32 c = getCharacter(getImmediateOrVariableUnsigned(3));
    s32 v;

    if (c != 0xFF) {
        v = getImmediateOrVariableUnsigned(1) - pKernelGameState->m26C_party[c].m77_baseWeight;
        if (v < 0) {
            v = 0;
        }
        pKernelGameState->m26C_party[c].m78_weightGain = v;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

/* Opcode EX 0x69 */
void OPX_69(void) {
    s32 c = getCharacter(getImmediateOrVariableUnsigned(3));
    s32 v;

    if (c != 0xFF) {
        v = pKernelGameState->m26C_party[c].m77_baseWeight + pKernelGameState->m26C_party[c].m78_weightGain;
        setVar(readU16FromScript(1), v);
    } else {
        setVar(readU16FromScript(1), 0);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

/* Noah: findFreePartySlot. -1 if character is already in the party or no slot is free. */
s32 findFreePartySlot(s32 character, s32* slot) {
    s32 i;

    for (i = 0; i < 3; i++) {
        if (currentParty[i] == character) {
            break;
        }
        if (currentParty[i] == 0xFF) {
            *slot = i;
            return 0;
        }
    }
    return -1;
}

/* Noah: startPartyCharacterASyncLoading. The file index is a block-local
 * variable in each branch (retail keeps it in s0, the character in s1). */
void startPartyCharacterASyncLoading(s32 character, s32 slot) {
    asyncPartyCharacterLoadingCharacterIndex = character;
    asyncPartyCharacterLoadingIndex = slot;
    setCurrentDirectory(4, 0);
    if (fieldExecuteVar1 == 0) {
        waitReadCompletion(0);
    }
    if ((fieldMapNumber & 0xC000) == 0) {
        s32 file = character + 5;
        s32 size = getFileSizeAligned(file);
        asyncPartyCharacterLoadingTable[asyncPartyCharacterLoadingIndex] = character;
        asyncPartyCharacterLoadingBuffer = allocateHeap(PC_HEAP_BYTES(size), 0);
        readFile(file, asyncPartyCharacterLoadingBuffer, 0, 0x80);
    } else {
        s32 file;
        character = getPartyMemberGearNum(character);
        if (character == 0xFF) {
            character = 0;
        }
        character += 0x10;
        file = character + 5;
        asyncPartyCharacterLoadingBuffer = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(file)), 0);
        asyncPartyCharacterLoadingTable[asyncPartyCharacterLoadingIndex] = character;
        readFile(file, asyncPartyCharacterLoadingBuffer, 0, 0x80);
    }
    if (fieldExecuteVar1 == 0) {
        waitReadCompletion(0);
    }
    asyncLoadingVar1 = 1;
}

void OPX_4D(void) {
    u8 value = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    pCurrentFieldScriptActor->mEA_forcedAnimation = ~value;
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

/* Opcode EX 0x4C: OPX_4D, then clear flag 0x10000. */
void OPX_4C(void) {
    OPX_4D();
    pCurrentFieldScriptActor->m4_flags &= ~0x10000;
}

void OP_WAIT_LOADING_SPECIAL_2D_ANIMATION(void) {
    if (isLoadCompleted() == 0) {
        load2dAnimVar = 0;
        {
            /* OP_LOAD_SPECIAL_2D_ANIMATION frees the animation file it
             * replaces, then allocates the new one: the PC heap can hand
             * back that very block where the PS1's put the file elsewhere
             * (field 315, Bart's laugh: in retail the dialogue's blocks
             * follow the old file, Beetle RAM). setAnimationBundle compares
             * addresses only and would keep the old file's offsets into it
             * (garbage frames uploaded over the portraits, xg-ze3), so the
             * actor forgets it showed a bundle at that address. */
            SpriteActorCore* sprite = fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet;
            if (sprite->m44_currentAnimationBundle == pCurrentFieldScriptActor->m120_special2dAnimation) {
                sprite->m44_currentAnimationBundle = NULL;
            }
        }
        spriteActorSetSpecialAnimation(fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet, pCurrentFieldScriptActor->m120_special2dAnimation);
        pCurrentFieldScriptActor->mCC_scriptPC += 1;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
    }
    g_breakCurrentScript = 1;
}

void OP_FREE_SPECIAL_2D_ANIMATION(void) {
    if (pCurrentFieldScriptActor->m124_special2dAnimationId != -1) {
        freeHeap(pCurrentFieldScriptActor->m120_special2dAnimation);
        pCurrentFieldScriptActor->m124_special2dAnimationId = -1;
    }
    g_breakCurrentScript = 1;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

/* Opcode EX 0xB0: 1 = finish loading a sound bank (loadWds), else start loading one. */
void OPX_B0(void) {
    s32 v;

    if (isLoadCompleted() == 0) {
        if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] == 1) {
            fieldWdsSlots[scriptWdsSlot] = loadWds(scriptWdsBuffer, 0);
            waitForMusic(0x10);
            freeHeap(scriptWdsBuffer);
            if (scriptWdsSlot == 3) {
                wdsFile5 = fieldWdsSlots[3];
            }
            g_breakCurrentScript = 1;
            pCurrentFieldScriptActor->mCC_scriptPC += 2;
        } else {
            scriptWdsSlot = getImmediateOrVariableUnsigned(2);
            unloadWds(fieldWdsSlots[scriptWdsSlot]);
            v = getImmediateOrVariableUnsigned(4);
            scriptWdsFileIndex = v;
            if (!(v & 0x80)) {
            load_default:
                setCurrentDirectory(0x1C, 0);
                scriptWdsFileIndex += 2;
            } else {
                if (mechaOverlayLoadingMode == 1) {
                    scriptWdsFileIndex = 4;
                    goto load_default;
                }
                scriptWdsFileIndex = (v & 0x7F) + 0x1F;
                setCurrentDirectory(0x2C, 1);
            }
            scriptWdsBuffer = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(scriptWdsFileIndex)), 0);
            readFile(scriptWdsFileIndex, scriptWdsBuffer, 0, 0x80);
            setCurrentDirectory(4, 0);
            pCurrentFieldScriptActor->mCC_scriptPC += 6;
        }
    } else {
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
    }
}

void OP_LOAD_SPECIAL_2D_ANIMATION(void) {
    s32 id;
    s32 file;
    s32 size;

    if (load2dAnimVar == 0 && loadCompleted == 0) {
        if (isLoadCompleted() != 0) {
            g_breakCurrentScript = 1;
            pCurrentFieldScriptActor->mCC_scriptPC -= 1;
            return;
        }
        if (pCurrentFieldScriptActor->m124_special2dAnimationId != -1) {
            freeHeap(pCurrentFieldScriptActor->m120_special2dAnimation);
            pCurrentFieldScriptActor->m124_special2dAnimationId = -1;
        }
        id = getImmediateOrVariableUnsigned(1);
        setCurrentDirectory(4, 0);
        file = id + 0x77A;
        size = getFileSizeAligned(file);
        pCurrentFieldScriptActor->m124_special2dAnimationId = file;
        pCurrentFieldScriptActor->m120_special2dAnimation = allocateHeap(PC_HEAP_BYTES(size + 8), 0);
        readFile(file, pCurrentFieldScriptActor->m120_special2dAnimation, 0, 0x80);
        if (fieldExecuteVar1 == 0) {
            waitReadCompletion(0);
        }
        load2dAnimVar = 1;
        pCurrentFieldScriptActor->mCC_scriptPC += 3;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
    }
    g_breakCurrentScript = 1;
}

/* Opcode EX 0x46: set (0) or clear flag 0x20000. */
void OPX_46(void) {
    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] == 0) {
        pCurrentFieldScriptActor->m4_flags |= 0x20000;
    } else {
        pCurrentFieldScriptActor->m4_flags &= ~0x20000;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OPX_3D(void) {
    s32 i = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);

    mechaFieldArgs2[0].m[i][0] = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    mechaFieldArgs2[0].m[i][1] = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    mechaFieldArgs2[0].m[i][2] = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    pCurrentFieldScriptActor->mCC_scriptPC += 0xA;
}

void OPX_3E(void) {
    s32 i = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);

    mechaFieldArgs2[1].m[0][i] = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    mechaFieldArgs2[1].m[1][i] = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    mechaFieldArgs2[1].m[2][i] = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    pCurrentFieldScriptActor->mCC_scriptPC += 0xA;
}

void OPX_3F(void) {
    op9DVar1.mD8_mechaBackColor[0] = getImmediateOrVariableUnsigned(1);
    op9DVar1.mD8_mechaBackColor[1] = getImmediateOrVariableUnsigned(3);
    op9DVar1.mD8_mechaBackColor[2] = getImmediateOrVariableUnsigned(5);
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OPX_47(void) {
    fieldObjectRenderingVar2.pad = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_MECHA_PLAY_ANIMATION(void) {
    if (fieldExecuteVar1 != 0) {
        mechaPlayAnimation(getImmediateOrVariableUnsigned(1), 0, getImmediateOrVariableUnsigned(3));
        op9DVar1.m60_mechaCurrentAnimation[getImmediateOrVariableUnsigned(1)] = getImmediateOrVariableUnsigned(3);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OPX_5B(void) {
    pCurrentFieldScriptActor->m11E_rotationSpeed = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_SETUP_SCREEN_EFFECT1(void) {
    s32 a = getImmediateOrVariableUnsigned(9);
    s32 b = getImmediateOrVariableUnsigned(3);
    s32 c = getImmediateOrVariableUnsigned(5);
    s32 d = getImmediateOrVariableUnsigned(7);
    s32 e = getImmediateOrVariableUnsigned(1);

    setupRGBCalcMode(1, a, b, c, d, e);
    pCurrentFieldScriptActor->mCC_scriptPC += 0xB;
}

void OP_SETUP_SCREEN_DISTORTION(void) {
    setupScreenDistortion(0);
    pCurrentFieldScriptActor->mCC_scriptPC += 0xF;
}

void OP_SCREEN_DISTORTION_FADE_OUT(void) {
    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) {
    case 0:
        setScreenDistortionParams(0, 0, 0, 0, 0, 0, getImmediateOrVariableUnsigned(2));
        fieldScreenEffects.m2_screenDistortionFadeOut = 1;
        pCurrentFieldScriptActor->mCC_scriptPC += 4;
        break;
    case 1:
        if (fieldScreenEffects.m0_screenDistortionRunning == 0) {
            pCurrentFieldScriptActor->mCC_scriptPC += 2;
        } else {
            pCurrentFieldScriptActor->mCC_scriptPC -= 1;
        }
        break;
    case 2:
        fieldScreenEffects.m0_screenDistortionRunning = 0;
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
        break;
    case 3:
        freeScreenDistortion();
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
        break;
    }
    g_breakCurrentScript = 1;
}

void OPX_6F(void) {
    op9DVar1.m0_renderModelRotationAngles.vx = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    op9DVar1.m0_renderModelRotationAngles.vz = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    op9DVar1.m0_renderModelRotationAngles.vy = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

void OPX_SET_CAMERA_ANGLE(void) {
    computeProjectionMatrixAngles.vx = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    computeProjectionMatrixAngles.vz = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    computeProjectionMatrixAngles.vy = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

void OPX_1B(void) {
    sFieldEntitySub0* sub;
    u8* cur;
    u8* other;
    ModelBlock* block;
    u32* data;
    s32 du;
    s32 dv;
    s32 i;
    s32 j;
    u32 header;
    s32 count;
    POLY_FT3* p3;
    POLY_FT3* q3;
    POLY_FT4* p4;
    POLY_FT4* q4;

    sub = fieldSceneState.m290_actorArray[currentFieldActorId].m0_model;
    cur = sub->m8_primBuffers[g_frameOddOrEven];
    other = sub->m8_primBuffers[(g_frameOddOrEven + 1) & 1];
    block = sub->m4_pModelBlock;
    data = (u32*)block->m10_meshBlocks;
    du = (s16)readS16FromScript(1);
    dv = (s16)readS16FromScript(3);
    for (i = block->m6_numMeshBlocks; i > 0; i--) {
        header = *data;
        count = header >> 16;
        if ((header & 0xFF) == 0xC4 || (header & 0xFF) == 0xC8) {
            data++;
            continue;
        }
        data++;
        if (!(header & 8)) {
            p3 = (POLY_FT3*)cur;
            q3 = (POLY_FT3*)other;
            for (j = 0; j < count; j++) {
                p3->u0 += du;
                p3->u1 += du;
                p3->u2 += du;
                p3->v0 += dv;
                p3->v1 += dv;
                p3->v2 += dv;
                q3->u0 = p3->u0;
                q3->u1 = p3->u1;
                q3->u2 = p3->u2;
                q3->v0 = p3->v0;
                q3->v1 = p3->v1;
                q3->v2 = p3->v2;
                data++;
                data++;
                p3++;
                q3++;
            }
            cur = (u8*)p3;
            other = (u8*)q3;
        } else {
            p4 = (POLY_FT4*)cur;
            q4 = (POLY_FT4*)other;
            for (j = 0; j < count; j++) {
                p4->u0 += du;
                p4->u1 += du;
                p4->u2 += du;
                p4->u3 += du;
                p4->v0 += dv;
                p4->v1 += dv;
                p4->v2 += dv;
                p4->v3 += dv;
                q4->u0 = p4->u0;
                q4->u1 = p4->u1;
                q4->u2 = p4->u2;
                q4->u3 = p4->u3;
                q4->v0 = p4->v0;
                q4->v1 = p4->v1;
                q4->v2 = p4->v2;
                q4->v3 = p4->v3;
                data++;
                data++;
                p4++;
                q4++;
            }
            cur = (u8*)p4;
            other = (u8*)q4;
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_FINALIZE_PARTY_CHARACTER_LOADING(void) {
    if (asyncLoadingVar1 != 0xFF && isCDBusy() == 0) {
        waitReadCompletion(0);
        decompress(asyncPartyCharacterLoadingBuffer, partyCharacterBuffers[asyncPartyCharacterLoadingIndex]);
        freeHeap(asyncPartyCharacterLoadingBuffer);
        runInitScriptForNewlyLoadedPC(asyncPartyCharacterLoadingCharacterIndex);
        asyncLoadingVar1 = 0xFF;
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC += 1;
    } else {
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
    }
}

/* Noah: runInitScriptForNewlyLoadedPC. backupPC is an int (spilled as a word,
 * reloaded as its low half); backupMaxCycles is read before backupActorId. */
/* getScriptEntryPoint was called undeclared here (int result): its u16
 * prototype changes the code. */
void runInitScriptForNewlyLoadedPC(s32 character) {
    sFieldEntity* backupEntity;
    sFieldScriptEntity* backupActor;
    s32 backupBreak;
    s32 backupActorId;
    s32 backupMaxCycles;
    s32 backupPC;
    s32 i;
    s32 entryPoint;

    backupEntity = pCurrentFieldEntity;
    backupActor = pCurrentFieldScriptActor;
    pKernelGameState->m1D30_partyMemberBitField |= 1 << asyncPartyCharacterLoadingCharacterIndex;
    if (fieldExecuteVar1 != 0) {
        backupPC = backupActor->mCC_scriptPC;
        backupBreak = g_breakCurrentScript;
        backupMaxCycles = fieldExectuteMaxCycles;
        backupActorId = currentFieldActorId;
        for (i = 0; i < g_totalActors; i++) {
            entryPoint = getScriptEntryPoint(i, 0);
            if (pCurrentFieldScriptFile[entryPoint] == 0x16 && pCurrentFieldScriptFile[entryPoint + 1] == character) {
                pCurrentFieldEntity = &fieldSceneState.m290_actorArray[i];
                pCurrentFieldScriptActor = pCurrentFieldEntity->m4C_scriptEntity;
                resetFieldScriptEntityValues(i);
                currentFieldActorId = i;
                fieldSceneState.m290_actorArray[i].m4C_scriptEntity->mCC_scriptPC = entryPoint;
                pCurrentFieldScriptActor->mCC_scriptPC = getScriptEntryPoint(i, 0);
                currentScriptFinished = 0;
                entryPoint = getScriptEntryPoint(i, 0);
                copyActorPositions(i, op9DVar1.mE8_playerControlledActor);
                executeFieldScript(0xFFFF);
                doPCCollisionCheckAfterLoading();
                pKernelGameState->m1D30_partyMemberBitField |= 1 << asyncPartyCharacterLoadingCharacterIndex;
                if (fieldMapNumber & 0xC000) {
                    pCurrentFieldEntity = &fieldSceneState.m290_actorArray[partyGearActorIds[asyncPartyCharacterLoadingIndex]];
                    pCurrentFieldScriptActor = pCurrentFieldEntity->m4C_scriptEntity;
                    resetFieldScriptEntityValues(partyGearActorIds[asyncPartyCharacterLoadingIndex]);
                    fieldSceneState.m290_actorArray[i].m4C_scriptEntity->mCC_scriptPC = entryPoint;
                    currentFieldActorId = partyGearActorIds[asyncPartyCharacterLoadingIndex];
                    pCurrentFieldScriptActor->mCC_scriptPC = getScriptEntryPoint(partyGearActorIds[asyncPartyCharacterLoadingIndex], 0);
                    currentScriptFinished = 0;
                    executeFieldScript(0xFFFF);
                }
                break;
            }
        }
        pCurrentFieldEntity = backupEntity;
        pCurrentFieldScriptActor = backupActor;
        g_breakCurrentScript = backupBreak;
        currentFieldActorId = backupActorId;
        fieldExectuteMaxCycles = backupMaxCycles;
        backupActor->mCC_scriptPC = backupPC;
    }
}

void OPX_C6(void) {
    s32 slot;
    s32 character;

    if (asyncLoadingVar1 == 0xFF && loadCompleted == 0 && isLoadCompleted() == 0) {
        waitReadCompletion(0);
        character = getImmediateOrVariableUnsigned(1);
        if (character != 0xFF) {
            if (findFreePartySlot(character, &slot) == 0) {
                pKernelGameState->m22B1_isOnGear[slot] = 0;
                currentParty[slot] = character;
                startPartyCharacterASyncLoading(character, slot);
                pCurrentFieldScriptActor->mCC_scriptPC += 3;
            } else {
                pKernelGameState->m1D30_partyMemberBitField |= 1 << character;
                pCurrentFieldScriptActor->mCC_scriptPC += 5;
            }
        } else {
            pCurrentFieldScriptActor->mCC_scriptPC += 5;
        }
    } else {
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
    }
}

void OP_ADD_TO_CURRENT_PARTY(void) {
    s32 slot;
    s32 character;

    if (asyncLoadingVar1 == 0xFF && loadCompleted == 0 && isLoadCompleted() == 0) {
        waitReadCompletion(0);
        if (findFreePartySlot(pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1], &slot) == 0) {
            pKernelGameState->m22B1_isOnGear[slot] = 0;
            character = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
            currentParty[slot] = character;
            startPartyCharacterASyncLoading(character, slot);
            pCurrentFieldScriptActor->mCC_scriptPC += 2;
        } else {
            pKernelGameState->m1D30_partyMemberBitField |= 1 << pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
            pCurrentFieldScriptActor->mCC_scriptPC += 4;
        }
    } else {
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
    }
}

void copyPartySlotFromNext(s32 slot) {
    s32 actor;
    sFieldScriptEntity* entity;

    actor = partyFieldActorIds[slot + 1];
    if (actor == 0xFF) {
        asyncPartyCharacterLoadingTable[slot] = asyncPartyCharacterLoadingTable[slot + 1];
        currentParty[slot] = currentParty[slot + 1];
        partyFieldActorIds[slot] = partyFieldActorIds[slot + 1];
        currentParty[slot + 1] = 0xFF;
        asyncPartyCharacterLoadingTable[slot + 1] = 0xFF;
        partyFieldActorIds[slot + 1] = 0xFF;
    } else {
        *partyCharacterBuffers[slot] = *partyCharacterBuffers[slot + 1];
        asyncPartyCharacterLoadingTable[slot] = asyncPartyCharacterLoadingTable[slot + 1];
        currentParty[slot] = currentParty[slot + 1];
        partyFieldActorIds[slot] = partyFieldActorIds[slot + 1];
        entity = fieldSceneState.m290_actorArray[actor].m4C_scriptEntity;
        if (!(entity->m126_spriteSource & 0x80)) {
            resetFieldScriptEntityGraphicEntity(actor, slot, partyCharacterBuffers[slot], 1, 0, slot, 1);
        } else {
            resetFieldScriptEntityGraphicEntity(actor, entity->m127_vramSlot, (void*)(((s32*)fieldActorSetupParams)[(entity->m126_spriteSource & 0x7F) + 1] + (sptr)fieldActorSetupParams),
                          entity->m130_x1C, entity->m134_x0,
                          fieldSceneState.m290_actorArray[actor].m4C_scriptEntity->m126_spriteSource, fieldSceneState.m290_actorArray[actor].m4C_scriptEntity->m134_x4);
        }
        currentParty[slot + 1] = 0xFF;
        asyncPartyCharacterLoadingTable[slot + 1] = 0xFF;
        partyFieldActorIds[slot + 1] = 0xFF;
    }
}

void emptyPartySlot(s32 slot) {
    sFieldEntity* backupEntity;
    sFieldScriptEntity* backupActor;
    s32 backupActorId;
    u16 backupPC;
    sFieldScriptEntity* actor;
    sFieldEntity* e;

    if (partyFieldActorIds[slot] == 0xFF) {
        currentParty[slot] = 0xFF;
        asyncPartyCharacterLoadingTable[slot] = 0xFF;
    } else {
        backupEntity = pCurrentFieldEntity;
        backupActor = pCurrentFieldScriptActor;
        backupActorId = currentFieldActorId;
        pCurrentFieldEntity = &fieldSceneState.m290_actorArray[partyFieldActorIds[slot]];
        backupPC = backupActor->mCC_scriptPC;
        pCurrentFieldScriptActor = pCurrentFieldEntity->m4C_scriptEntity;
        resetFieldScriptEntityValues(partyFieldActorIds[slot]);
        currentFieldActorId = partyFieldActorIds[slot];
        e = &fieldSceneState.m290_actorArray[currentFieldActorId];
        e->m58_flags = (e->m58_flags & 0xF07F) | 0x200;
        resetFieldScriptEntityGraphicEntity(currentFieldActorId, 0, partyCharacterBuffers[0], 1, 0, 0, 1);
        actor = pCurrentFieldScriptActor;
        actor->m0_fieldScriptFlags |= 1;
        g_breakCurrentScript = 0;
        pCurrentFieldScriptActor = backupActor;
        pCurrentFieldEntity = backupEntity;
        currentFieldActorId = backupActorId;
        actor->mCC_scriptPC = backupPC;
        actor->m4_flags |= 0x100000;
        actor->m0_fieldScriptFlags |= 0x20000;
        actor->m4_flags |= 0x400;
        partyFieldActorIds[slot] = 0xFF;
        currentParty[slot] = 0xFF;
        asyncPartyCharacterLoadingTable[slot] = 0xFF;
    }
}

void OP_REMOVE_FROM_CURRENT_PARTY(void) {
    s32 loading;
    s32 slot;
    sFieldScriptEntity* entity;

    loading = asyncLoadingVar1;
    if (loading != 0xFF) {
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
        return;
    }
    DrawSync(0);
    slot = findCharacterInParty(getCharacter(pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]));
    if (slot != -1) {
        if (fieldExecuteVar1 == 0) {
            switch (slot) {
            case 0:
                if (currentParty[1] == loading) {
                    currentParty[0] = loading;
                    asyncPartyCharacterLoadingTable[0] = loading;
                    pKernelGameState->m22B1_isOnGear[0] = 0;
                } else {
                    *partyCharacterBuffers[0] = *partyCharacterBuffers[1];
                    currentParty[0] = currentParty[1];
                    asyncPartyCharacterLoadingTable[0] = asyncPartyCharacterLoadingTable[1];
                    currentParty[1] = 0xFF;
                    asyncPartyCharacterLoadingTable[1] = 0xFF;
                    pKernelGameState->m22B1_isOnGear[0] = pKernelGameState->m22B1_isOnGear[1];
                    if (currentParty[2] != 0xFF) {
                        *partyCharacterBuffers[1] = *partyCharacterBuffers[2];
                        currentParty[1] = currentParty[2];
                        asyncPartyCharacterLoadingTable[1] = asyncPartyCharacterLoadingTable[2];
                        currentParty[2] = 0xFF;
                        asyncPartyCharacterLoadingTable[2] = 0xFF;
                        pKernelGameState->m22B1_isOnGear[1] = pKernelGameState->m22B1_isOnGear[2];
                    }
                }
                break;
            case 1:
                if (currentParty[2] == loading) {
                    currentParty[1] = loading;
                    asyncPartyCharacterLoadingTable[1] = loading;
                    pKernelGameState->m22B1_isOnGear[1] = 0;
                } else {
                    *partyCharacterBuffers[1] = *partyCharacterBuffers[2];
                    currentParty[1] = currentParty[2];
                    asyncPartyCharacterLoadingTable[1] = asyncPartyCharacterLoadingTable[2];
                    currentParty[2] = 0xFF;
                    asyncPartyCharacterLoadingTable[2] = 0xFF;
                    pKernelGameState->m22B1_isOnGear[1] = pKernelGameState->m22B1_isOnGear[2];
                    pKernelGameState->m22B1_isOnGear[2] = 0;
                }
                break;
            case 2:
                currentParty[2] = loading;
                asyncPartyCharacterLoadingTable[2] = loading;
                pKernelGameState->m22B1_isOnGear[2] = 0;
                break;
            }
        } else {
            switch (slot) {
            case 0:
                pKernelGameState->m22B1_isOnGear[0] = pKernelGameState->m22B1_isOnGear[1];
                pKernelGameState->m22B1_isOnGear[1] = pKernelGameState->m22B1_isOnGear[2];
                pKernelGameState->m22B1_isOnGear[2] = 0;
                emptyPartySlot(0);
                copyPartySlotFromNext(0);
                copyPartySlotFromNext(1);
                break;
            case 1:
                pKernelGameState->m22B1_isOnGear[1] = pKernelGameState->m22B1_isOnGear[2];
                pKernelGameState->m22B1_isOnGear[2] = 0;
                emptyPartySlot(1);
                copyPartySlotFromNext(1);
                break;
            case 2:
                pKernelGameState->m22B1_isOnGear[2] = 0;
                emptyPartySlot(2);
                break;
            }
            if (currentParty[0] != 0xFF && partyFieldActorIds[0] != 0xFF) {
                op9DVar1.mE8_playerControlledActor = partyFieldActorIds[0];
                entity = fieldSceneState.m290_actorArray[partyFieldActorIds[0]].m4C_scriptEntity;
                entity->m0_fieldScriptFlags = (entity->m0_fieldScriptFlags | 0x4400) & ~0x80;
            } else {
                op9DVar1.mE8_playerControlledActor = 0;
            }
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OPX_16(void) {
    if (pCurrentFieldScriptActor->m12C_xC) {
        freeHeap(pCurrentFieldScriptActor->m114_movementBoundingZone);
        pCurrentFieldScriptActor->m12C_xC = 0;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OP_FADE_MUSIC(void) {
    s32 a;

    if (updateMusicState2Var3 != 0) {
        a = getImmediateOrVariableUnsigned(1);
        playBattleMusic(pMusic, a, getImmediateOrVariableUnsigned(3));
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 5;
    } else if (currentlyPlayingMusic == 0xFF) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 5;
    } else if (fieldExecuteVar1 == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 5;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC - 1;
    }
    g_breakCurrentScript = 1;
}

void OPX_0F(void) {
    s32 a;

    if (updateMusicState2Var3 != 0) {
        a = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]);
        setMusicInstancePitch(pMusic, a, getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]));
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 6;
    } else if (currentlyPlayingMusic == 0xFF) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 6;
    } else if (fieldExecuteVar1 == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 6;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC - 1;
    }
    g_breakCurrentScript = 1;
}

void OPX_SET_TEMPO(void) {
    s32 a;

    if (updateMusicState2Var3 != 0) {
        a = getImmediateOrVariableUnsigned(1);
        setMusicInstanceTempo(pMusic, a, getImmediateOrVariableUnsigned(3));
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 5;
    } else if (currentlyPlayingMusic == 0xFF) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 5;
    } else if (fieldExecuteVar1 == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 5;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC - 1;
    }
    g_breakCurrentScript = 1;
}

void OPX_11(void) {
    s32 a;

    if (updateMusicState2Var3 != 0) {
        a = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]);
        setMusicInstancePan(pMusic, a, getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]));
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 6;
    } else if (currentlyPlayingMusic == 0xFF) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 6;
    } else if (fieldExecuteVar1 == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 6;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC - 1;
    }
    g_breakCurrentScript = 1;
}

void OPX_12(void) {
    if (updateMusicState2Var3 != 0) {
        setMusicInstanceTrackMuteMask(pMusic, getImmediateOrVariableUnsigned(1));
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 3;
    } else if (currentlyPlayingMusic == 0xFF) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 3;
    } else if (fieldExecuteVar1 == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 3;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC - 1;
    }
    g_breakCurrentScript = 1;
}

void OPX_13(void) {
    pCurrentFieldScriptActor->m10A_soundId = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->m10D_soundMode = 0;
    pCurrentFieldScriptActor->m10C_soundVolume = getImmediateOrVariableUnsigned(3);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
    OPX_13Sub(currentFieldActorId);
    if (pCurrentFieldScriptActor->m10A_soundId == 0) {
        pCurrentFieldScriptActor->m10D_soundMode = 0xFF;
    }
}

void OPX_14(void) {
    pCurrentFieldScriptActor->m10A_soundId = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->m10D_soundMode = 0x80;
    pCurrentFieldScriptActor->m10C_soundVolume = getImmediateOrVariableUnsigned(3);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
    OPX_13Sub(currentFieldActorId);
    if (pCurrentFieldScriptActor->m10A_soundId == 0) {
        pCurrentFieldScriptActor->m10D_soundMode = 0xFF;
    }
}

void OP_CLEAR_PARTY_FRAME_MASK(void) {
    s32 character;

    character = getCharacter(getImmediateOrVariableUnsigned(1));
    if (character != 0xFF) {
        pKernelGameState->m1D32_partyFrameMask &= ~(1 << character);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_SET_PARTY_FRAME_MASK(void) {
    s32 character;

    character = getCharacter(getImmediateOrVariableUnsigned(1));
    if (character != 0xFF) {
        pKernelGameState->m1D32_partyFrameMask |= 1 << character;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

s32 getCharacter(s32 character) {
    if (character == 0xFF) {
        return currentParty[2];
    }
    if (character == 0xFE) {
        return currentParty[1];
    }
    if (character == 0xFD) {
        return currentParty[0];
    }
    if (character == 0xFC) {
        return 0xFF;
    }
    return character;
}

void OP_SET_DIALOG_PORTRAIT(void) {
    pCurrentFieldScriptActor->m80_dialogPortrait = getCharacter(getImmediateOrVariableUnsigned(1));
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_0C(void) {
    op9DVar1.m1C_OPX_0CParams0[0] = readU16FromScript(1);
    op9DVar1.m1C_OPX_0CParams0[2] = readU16FromScript(3);
    op9DVar1.m1C_OPX_0CParams0[1] = readU16FromScript(5);
    op9DVar1.m22_OPX_0CParams1[0] = readU16FromScript(7);
    op9DVar1.m22_OPX_0CParams1[2] = readU16FromScript(9);
    op9DVar1.m22_OPX_0CParams1[1] = readU16FromScript(0xB);
    pCurrentFieldScriptActor->mCC_scriptPC += 0xD;
}

void OPX_09(void) {
    if (getImmediateOrVariableUnsigned(1) == 0) {
        pCurrentFieldScriptActor->m4_flags &= ~0x800;
    } else {
        pCurrentFieldScriptActor->m4_flags |= 0x800;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_SET_CURRENT_ACTOR_SCALE(void) {
    s32 scale;

    scale = getImmediateOrVariableUnsigned(1);
    /* Model-only actors have no sprite. The PS1's write through NULL goes
     * to low RAM; the PC still applies the actor's 3D scale below. */
    if (fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet != NULL) {
        fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet->m2C_scale = (u32)(scale * 3) >> 2;
    }
    pCurrentFieldScriptActor->mF4_scale3d[0] = scale;
    pCurrentFieldScriptActor->mF4_scale3d[1] = scale;
    pCurrentFieldScriptActor->mF4_scale3d[2] = scale;
    updateScriptActor3dRotation(currentFieldActorId);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_SET_3D_SCALE(void) {
    s32 x;
    s32 y;
    s32 z;

    x = getImmediateOrVariableUnsigned(1);
    y = getImmediateOrVariableUnsigned(3);
    z = getImmediateOrVariableUnsigned(5);
    if (fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet != NULL) {
        fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet->m2C_scale = 0xC00;
    }
    pCurrentFieldScriptActor->mF4_scale3d[0] = x;
    pCurrentFieldScriptActor->mF4_scale3d[1] = y;
    pCurrentFieldScriptActor->mF4_scale3d[2] = z;
    updateScriptActor3dRotation(currentFieldActorId);
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OPX_39(void) {
    op9DVar1.m8_entityMoveSpeedFactor = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_04(void) {
    fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet->m82_entityScale = getImmediateOrVariableUnsigned(1) << 1;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_00(void) {
}

void writeU16ToScript(s32 value, s32 offset) {
    pCurrentFieldScriptFile[offset + 1] = value >> 8;
    pCurrentFieldScriptFile[offset] = value;
}

s32 isActorNearActorXZ(s32 a, s32 b) {
    if (length2d(fieldSceneState.m290_actorArray[a].mC_matrix.t[0] - fieldSceneState.m290_actorArray[b].mC_matrix.t[0],
                      fieldSceneState.m290_actorArray[a].mC_matrix.t[2] - fieldSceneState.m290_actorArray[b].mC_matrix.t[2]) < 16) {
        return 0;
    }
    return -1;
}

void copyActorPositions(s32 dest, s32 source) {
    sFieldScriptEntity* pDest;
    sFieldScriptEntity* pSource;
    s32 i;

    pSource = fieldSceneState.m290_actorArray[source].m4C_scriptEntity;
    pDest = fieldSceneState.m290_actorArray[dest].m4C_scriptEntity;
    for (i = 0; i < 4; i++) {
        pDest->m8_currentWalkMeshTriangle[i] = pSource->m8_currentWalkMeshTriangle[i];
    }
    pDest->m10_walkmeshId = pSource->m10_walkmeshId;
    pDest->m50_surfaceNormal.vx = pSource->m50_surfaceNormal.vx;
    pDest->m50_surfaceNormal.vy = pSource->m50_surfaceNormal.vy;
    pDest->m50_surfaceNormal.vz = pSource->m50_surfaceNormal.vz;
    pDest->m20_position.vx = pSource->m20_position.vx;
    pDest->m20_position.vy = pSource->m20_position.vy;
    pDest->m20_position.vz = pSource->m20_position.vz;
    pDest->mEC_elevation = pSource->mEC_elevation;
    pDest->m72_elevation = pSource->m72_elevation;
    pDest->m14_currentTriangleFlag = pSource->m14_currentTriangleFlag;
    fieldSceneState.m290_actorArray[dest].m4_pVramSpriteSheet->m84_maxY = fieldSceneState.m290_actorArray[source].m4_pVramSpriteSheet->m84_maxY;
    fieldSceneState.m290_actorArray[dest].m4_pVramSpriteSheet->m0_position.vx = fieldSceneState.m290_actorArray[source].m4_pVramSpriteSheet->m0_position.vx;
    fieldSceneState.m290_actorArray[dest].m4_pVramSpriteSheet->m0_position.vy = fieldSceneState.m290_actorArray[source].m4_pVramSpriteSheet->m0_position.vy;
    fieldSceneState.m290_actorArray[dest].m4_pVramSpriteSheet->m0_position.vz = fieldSceneState.m290_actorArray[source].m4_pVramSpriteSheet->m0_position.vz;
    fieldSceneState.m290_actorArray[dest].mC_matrix.t[0] = fieldSceneState.m290_actorArray[source].mC_matrix.t[0];
    fieldSceneState.m290_actorArray[dest].mC_matrix.t[1] = fieldSceneState.m290_actorArray[source].mC_matrix.t[1];
    fieldSceneState.m290_actorArray[dest].mC_matrix.t[2] = fieldSceneState.m290_actorArray[source].mC_matrix.t[2];
}

void func_8008D570(void) {
    s32 i;

    for (i = 0; i < 3; i++) {
        if (partyFieldActorIds[i] == currentFieldActorId) {
            op9DVar1.m18_backgroundClearColor[3] &= ~(1 << i);
        }
    }
}

void OPX_25(void) {
    op9DVar1.m49_cameraFollowHeight = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OPX_07(void) {
    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) {
    case 0:
        pCurrentFieldScriptActor->m4_flags &= ~0x400;
        break;
    case 1:
        pCurrentFieldScriptActor->m4_flags |= 0x400;
        break;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OPX_SET_FLAG_BIT_FIELD(void) {
    u32 index;
    s32 bit;

    index = readU16FromScript(1) >> 4;
    bit = 1 << (readU16FromScript(1) & 0xF);
    setVar(index, getVariable(index) | bit);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_0B(void) {
    u32 index;
    s32 bit;

    index = readU16FromScript(1) >> 4;
    bit = 1 << (readU16FromScript(1) & 0xF);
    setVar(index, getVariable(index) & ~bit);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_FB(void) {
    u32 index;
    s32 bit;

    index = readU16FromScript(1) >> 4;
    bit = 1 << (readU16FromScript(1) & 0xF);
    if (getVariable(index) & bit) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 5;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(3);
    }
}

void emitScriptJumpToPosition(s32 a, s32 b, s32 c) {
    pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC] = 0x57;
    pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] = 0x81;
    writeU16ToScript(a, pCurrentFieldScriptActor->mCC_scriptPC + 2);
    writeU16ToScript(b, pCurrentFieldScriptActor->mCC_scriptPC + 4);
    writeU16ToScript(c, pCurrentFieldScriptActor->mCC_scriptPC + 6);
    writeU16ToScript(0xC, pCurrentFieldScriptActor->mCC_scriptPC + 8);
    pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xA] = 0xFF;
    pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xB] = 0x57;
    pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xC] = 0x8F;
    pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD] = 0x26;
    pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xE] = 1;
    pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xF] = 0x80;
    pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x10] = 0x57;
    pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11] = 0xF;
}

void emitScriptWalkToPosition(s32 a, s32 b) {
    pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xC] = 0x4B;
    writeU16ToScript(a, pCurrentFieldScriptActor->mCC_scriptPC + 0xD);
    writeU16ToScript(b, pCurrentFieldScriptActor->mCC_scriptPC + 0xF);
    pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11] = 0xFF;
    pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x12] = 0xFF;
    pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x13] = 0x80;
    pCurrentFieldScriptActor->mCC_scriptPC += 0xC;
}

void OPX_49(void) {
    pCurrentFieldScriptActor->m75_parentActorId = 0xFF;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OPX_AA(void) {
    op9DVar1.m1BA_actorCameraTracked = readCharacter(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

s32 addPartyGearHP(s32 slot, u32 amount) {
    s32 gear;
    s32 clamped;

    gear = getPartyMemberGearNum(currentParty[slot]);
    if (gear == 0xFF) {
        return 0xFF;
    }
    pKernelGameState->m978_gears[gear].m60_hp += amount;
    clamped = (u32)pKernelGameState->m978_gears[gear].m60_hp > (u32)pKernelGameState->m978_gears[gear].m64_maxHp;
    if (clamped) {
        pKernelGameState->m978_gears[gear].m60_hp = pKernelGameState->m978_gears[gear].m64_maxHp;
    }
    return clamped;
}

s32 damagePartyGearHP(s32 slot, u32 amount) {
    s32 gear;
    s32 hp;

    gear = getPartyMemberGearNum(currentParty[slot]);
    if (gear == 0xFF) {
        return 0xFF;
    }
    hp = pKernelGameState->m978_gears[gear].m60_hp - amount;
    if (hp <= 0) {
        hp = 1;
    }
    pKernelGameState->m978_gears[gear].m60_hp = hp;
    return hp;
}

void OPX_AB(void) {
    s32 i;
    s32 amount;
    s32 mask;

    amount = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]);
    mask = partySlotSelectionMasks[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3] & 3];
    for (i = 0; i < 3; i++) {
        if (currentParty[i] != 0xFF && (mask & 1)) {
            addPartyGearHP(i, amount);
        }
        mask >>= 1;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

void OPX_AC(void) {
    s32 i;
    s32 amount;
    s32 mask;

    amount = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]);
    mask = partySlotSelectionMasks[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3] & 3];
    for (i = 0; i < 3; i++) {
        if (currentParty[i] != 0xFF && (mask & 1)) {
            damagePartyGearHP(i, amount);
        }
        mask >>= 1;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

void OP_F9(void) {
    s32 actor;

    actor = readCharacter(1);
    if (actor != 0xFF) {
        pCurrentFieldScriptActor->m75_parentActorId = actor;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OPX_2C(void) {
    s32 actor;
    sFieldScriptEntity* entity;

    actor = readCharacter(1);
    if (actor != 0xFF) {
        entity = fieldSceneState.m290_actorArray[actor].m4C_scriptEntity;
        setVar(readU16FromScript(1), entity->m0_fieldScriptFlags);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_2D(void) {
    s32 actor;
    sFieldScriptEntity* entity;

    actor = readCharacter(1);
    if (actor != 0xFF) {
        entity = fieldSceneState.m290_actorArray[actor].m4C_scriptEntity;
        setVar(readU16FromScript(1), ((u16*)&entity->m0_fieldScriptFlags)[1]);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_2E(void) {
    s32 actor;
    sFieldScriptEntity* entity;

    actor = readCharacter(1);
    if (actor != 0xFF) {
        entity = fieldSceneState.m290_actorArray[actor].m4C_scriptEntity;
        setVar(readU16FromScript(1), entity->m4_flags);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_2F(void) {
    s32 actor;
    sFieldScriptEntity* entity;

    actor = readCharacter(1);
    if (actor != 0xFF) {
        entity = fieldSceneState.m290_actorArray[actor].m4C_scriptEntity;
        setVar(readU16FromScript(1), ((u16*)&entity->m4_flags)[1]);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void jumpIfMask3(u16 mask) {
    if (readU16FromScript(1) & mask) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 6;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(4);
    }
}

void jumpIfMask2(u16 mask) {
    if (readU16FromScript(1) & mask) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 5;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(3);
    }
}

void OPX_38(void) {
    s32 a;
    s32 b;
    s32 dist;

    dist = 0;
    a = readCharacter(3);
    b = readCharacter(4);
    if (a != 0xFF && b != 0xFF) {
        /* integer halves of the 16.16 positions (lh +2) */
        dist = length2d(((s16*)&fieldSceneState.m290_actorArray[a].m4C_scriptEntity->m20_position.vx)[1] - ((s16*)&fieldSceneState.m290_actorArray[b].m4C_scriptEntity->m20_position.vx)[1],
                             ((s16*)&fieldSceneState.m290_actorArray[a].m4C_scriptEntity->m20_position.vz)[1] - ((s16*)&fieldSceneState.m290_actorArray[b].m4C_scriptEntity->m20_position.vz)[1]);
    }
    setVar(readU16FromScript(1), dist);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OPX_34(void) {
    jumpIfMask3(((u16*)&fieldSceneState.m290_actorArray[readCharacter(3)].m4C_scriptEntity->m0_fieldScriptFlags)[0]);
}

void OPX_35(void) {
    jumpIfMask3(((u16*)&fieldSceneState.m290_actorArray[readCharacter(3)].m4C_scriptEntity->m0_fieldScriptFlags)[1]);
}

void OP_JUMP_IF_ACTOR_LOWER_FLAGS(void) {
    jumpIfMask3(((u16*)&fieldSceneState.m290_actorArray[readCharacter(3)].m4C_scriptEntity->m4_flags)[0]);
}

void OP_JUMP_IF_ACTOR_UPPER_FLAGS(void) {
    jumpIfMask3(((u16*)&fieldSceneState.m290_actorArray[readCharacter(3)].m4C_scriptEntity->m4_flags)[1]);
}

void OPX_30(void) {
    jumpIfMask2(((u16*)&pCurrentFieldScriptActor->m0_fieldScriptFlags)[0]);
}

void OPX_31(void) {
    jumpIfMask2(((u16*)&pCurrentFieldScriptActor->m0_fieldScriptFlags)[1]);
}

void OPX_32(void) {
    jumpIfMask2(((u16*)&pCurrentFieldScriptActor->m4_flags)[0]);
}

void OPX_33(void) {
    jumpIfMask2(((u16*)&pCurrentFieldScriptActor->m4_flags)[1]);
}

void storeScriptResultToVar(u16 value) {
    setVar(readU16FromScript(1), value);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_28(void) {
    storeScriptResultToVar(((u16*)&pCurrentFieldScriptActor->m0_fieldScriptFlags)[0]);
}

void OPX_29(void) {
    storeScriptResultToVar(((u16*)&pCurrentFieldScriptActor->m0_fieldScriptFlags)[1]);
}

void OPX_2A(void) {
    storeScriptResultToVar(((u16*)&pCurrentFieldScriptActor->m4_flags)[0]);
}

void OPX_2B(void) {
    storeScriptResultToVar(((u16*)&pCurrentFieldScriptActor->m4_flags)[1]);
}

void OP_SET_FLAGS_DYNAMIC(void) {
    u32 flags;

    flags = readU16FromScript(2);
    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) {
    case 0:
        pCurrentFieldScriptActor->m0_fieldScriptFlags |= flags;
        break;
    case 1:
        pCurrentFieldScriptActor->m0_fieldScriptFlags |= flags << 16;
        break;
    case 2:
        pCurrentFieldScriptActor->m4_flags |= flags;
        break;
    case 3:
        pCurrentFieldScriptActor->m4_flags |= flags << 16;
        break;
    case 4:
        pCurrentFieldScriptActor->m0_fieldScriptFlags &= ~flags;
        break;
    case 5:
        pCurrentFieldScriptActor->m0_fieldScriptFlags &= ~(flags << 16);
        break;
    case 6:
        pCurrentFieldScriptActor->m4_flags &= ~flags;
        break;
    case 7:
        pCurrentFieldScriptActor->m4_flags &= ~(flags << 16);
        break;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

void setInitialEncounterData(void) {
    s32 i;
    s32 j;
    s32 r;

    op9DVar1.m110_encounterDataCountdown = op9DVar1.m114_encounterTimer;
    if (op9DVar1.m118_encounterCount == 0) {
        op9DVar1.m114_encounterTimer = 0;
        return;
    }
    for (i = 0; i < 32; i++) {
        op9DVar1.m11C_encounterTriggerTimes[i] = 0xFFFF;
    }
    for (i = 0; i < op9DVar1.m118_encounterCount; i++) {
    retry:
        r = ((rand() * (op9DVar1.m114_encounterTimer + 1)) >> 15) & 0xFFFF;
        for (j = 0; j < 32; j++) {
            if (op9DVar1.m11C_encounterTriggerTimes[j] == r) {
                goto retry;
            }
        }
        op9DVar1.m11C_encounterTriggerTimes[i] = r;
    }
    for (i = 0; i < op9DVar1.m118_encounterCount; i++) {
        op9DVar1.m11C_encounterTriggerTimes[i]++;
    }
}

void OP_SET_ENCOUNTER_DATA(void) {
    op9DVar1.m114_encounterTimer = getImmediateOrVariableUnsigned(1);
    op9DVar1.m118_encounterCount = getImmediateOrVariableUnsigned(3);
    if (op9DVar1.m118_encounterCount > 0x20) {
        op9DVar1.m118_encounterCount = 0x20;
    }
    setInitialEncounterData();
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_F6(void) {
    sFieldScriptEntity* actor;
    SpriteActor* sprite;

    actor = pCurrentFieldScriptActor;
    switch (pCurrentFieldScriptFile[actor->mCC_scriptPC + 1]) {
    case 0:
        if (actor->m0_fieldScriptFlags & 0x8000) {
            actor->m0_fieldScriptFlags &= ~0x8000;
        }
        actor = pCurrentFieldScriptActor;
        if (actor->m4_flags & 0x80000) {
            sprite = fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet;
            sprite->m0_spriteActorCore.m18_moveSpeed = 0;
            sprite->m0_spriteActorCore.mC_step.vz = 0;
            sprite->m0_spriteActorCore.mC_step.vx = 0;
            actor->m4_flags &= ~0x80000;
        }
        break;
    case 1:
        actor->m0_fieldScriptFlags |= 0x8000;
        actor->m11C_lockedRotation = actor->m106_currentRotation;
        break;
    case 2:
        actor->m4_flags |= 0x80000;
        break;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OPX_61(void) {
    if (fieldMovieStarted == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC - 1;
    } else {
        fieldMovieStarted = 0;
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 1;
    }
    g_breakCurrentScript = 1;
}

void OPX_A0(void) {
    if (playMusicAuthorized == 0) {
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
        return;
    }
    fieldMovieParams.m0_movieId = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xB]);
    fieldMovieParams.mA_sectorOffset = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xB]);
    fieldMovieParams.mC_startFrame = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xB]);
    fieldMovieParams.mE_lastFrame = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xB]);
    fieldMovieParams.m18_soundSequenceId = getVar08(9, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xB]);
    fieldMovieParams.m10_modeAndSize[1] = 0x140;
    fieldMovieFlags = 0x40;
    fieldMovieParams.m10_modeAndSize[2] = 0x100;
    fieldMovieParams.m6_buffer1X = 0;
    fieldMovieParams.m4_buffer0Y = 0;
    fieldMovieParams.m2_buffer0X = 0;
    fieldMovieParams.m16_is24Bit = 1;
    fieldMovieParams.m8_buffer1Y = 0x100;
    fieldMovieParams.m1A_loop = 0;
    fieldMovieParams.m10_modeAndSize[0] &= 0xF;
    onlyUpdateDirector = 0;
    iRam800adb70 = 1;
    g_breakCurrentScript = 1;
    pCurrentFieldScriptActor->mCC_scriptPC += 0xC;
}

int pcRegionalFieldMovieMusic(s32 field, s32 stream);

void OP_SET_2D_BACKGROUND(void) {
    s32 temp;
    if (playMusicAuthorized == 0) {
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
        return;
    }
    {
        s32 music = pcRegionalFieldMovieMusic(fieldMapNumber & 0xFFF, getImmediateOrVariableUnsigned(1));
        if (music >= 0) {
            /* JP's preceding PLAY_MUSIC loads the movie's samples, even
             * though the score is silent. Keep the ordinary asynchronous
             * loader and retry this FE opcode until that setup is ready. */
            if (isLoadCompleted() != 0 || musicVar2 == 1 || fieldMusicLoadPending == -1) {
                g_breakCurrentScript = 1;
                pCurrentFieldScriptActor->mCC_scriptPC -= 1;
                return;
            }
            if (currentlyPlayingMusic != music) {
                musicVar1 = -1;
                resetBattleAudioState();
                currentlyPlayingMusic = music;
                fieldMusicLoadPending = -1;
                playMusic(music, 0);
                g_breakCurrentScript = 1;
                pCurrentFieldScriptActor->mCC_scriptPC -= 1;
                return;
            }
        }
    }
    fieldMovieParams.m0_movieId = getImmediateOrVariableUnsigned(1);
    fieldMovieParams.mA_sectorOffset = getImmediateOrVariableUnsigned(3);
    fieldMovieParams.mE_lastFrame = getImmediateOrVariableUnsigned(5);
    temp = fieldMovieParams.m10_modeAndSize[0] = getImmediateOrVariableUnsigned(7);
    fieldMovieParams.m10_modeAndSize[1] = 0x140;
    fieldMovieParams.m10_modeAndSize[2] = 0x100;
    fieldMovieParams.m10_modeAndSize[0] &= 0xF;
    fieldMovieFlags = temp & 0xC0;
    fieldMovieParams.mC_startFrame = 1;
    switch (fieldMovieParams.m10_modeAndSize[0]) {
    case 0:
        fieldMovieParams.m2_buffer0X = 0x140;
        fieldMovieParams.m4_buffer0Y = 0;
        fieldMovieParams.m6_buffer1X = 0x140;
        fieldMovieParams.m8_buffer1Y = 0x100;
        onlyUpdateDirector = 1;
        fieldMovieParams.m16_is24Bit = 0;
        break;
    case 1:
        fieldMovieParams.m6_buffer1X = 0;
        fieldMovieParams.m4_buffer0Y = 0;
        fieldMovieParams.m2_buffer0X = 0;
        fieldMovieParams.m8_buffer1Y = 0x100;
        onlyUpdateDirector = 0;
        fieldMovieParams.m16_is24Bit = 0;
        break;
    case 2:
        fieldMovieParams.m6_buffer1X = 0;
        fieldMovieParams.m4_buffer0Y = 0;
        fieldMovieParams.m2_buffer0X = 0;
        fieldMovieParams.m8_buffer1Y = 0x100;
        onlyUpdateDirector = 0;
        fieldMovieParams.m16_is24Bit = 1;
        break;
    }
    fieldMovieParams.m18_soundSequenceId = 0xFF;
    fieldMovieParams.m1A_loop = 0;
    iRam800adb70 = 1;
    g_breakCurrentScript = 1;
    pCurrentFieldScriptActor->mCC_scriptPC += 9;
}

void OPX_67(void) {
    s32 mode;
    s32 temp;

    fieldMovieParams.m0_movieId = getImmediateOrVariableUnsigned(1);
    fieldMovieParams.mA_sectorOffset = getImmediateOrVariableUnsigned(3);
    fieldMovieParams.mC_startFrame = getImmediateOrVariableUnsigned(5);
    fieldMovieParams.mE_lastFrame = getImmediateOrVariableUnsigned(7);
    fieldMovieParams.m10_modeAndSize[0] = getImmediateOrVariableUnsigned(9);
    mode = fieldMovieParams.m10_modeAndSize[0];
    if (mode == 0xFE) {
        fieldMovieParams.m1A_loop = 1;
    } else {
        fieldMovieParams.m1A_loop = 0;
    }
    temp = getImmediateOrVariableUnsigned(0xB);
    fieldMovieParams.m2_buffer0X = temp;
    fieldMovieParams.m6_buffer1X = temp;
    temp = getImmediateOrVariableUnsigned(0xD);
    fieldMovieParams.m4_buffer0Y = temp;
    fieldMovieParams.m8_buffer1Y = temp;
    fieldMovieParams.m10_modeAndSize[1] = getImmediateOrVariableUnsigned(0xF);
    fieldMovieParams.m10_modeAndSize[2] = getImmediateOrVariableUnsigned(0x11);
    fieldMovieFlags = mode & 0x40;
    fieldMovieParams.m18_soundSequenceId = 0xFF;
    onlyUpdateDirector = 2;
    fieldMovieParams.m16_is24Bit = 0;
    fieldMovieParams.m10_modeAndSize[0] &= 0xF;
    iRam800adb70 = 1;
    pCurrentFieldScriptActor->mCC_scriptPC += 0x13;
}

void OP_TRANSITION_EFFECT_1(void) {
    fullScreenTransitionEffectRenderParam = getImmediateOrVariableUnsigned(1);
    fullScreenTransitionEffectRenderMode = 1;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_TRANSITION_EFFECT_2(void) {
    fullScreenTransitionEffectRenderParam = getImmediateOrVariableUnsigned(1);
    fullScreenTransitionEffectRenderMode = 2;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_9E(void) {
    s32 a;
    s32 b;
    s32 c;

    a = getImmediateOrVariableUnsigned(1);
    b = getImmediateOrVariableUnsigned(3);
    c = getImmediateOrVariableUnsigned(5);
    setFieldDrawEnvClip(a, b, c, getImmediateOrVariableUnsigned(7));
    pCurrentFieldScriptActor->mCC_scriptPC += 9;
}

void OP_TRANSITION_EFFECT_3(void) {
    fullScreenTransitionEffectRenderParam = getImmediateOrVariableUnsigned(1);
    fullScreenTransitionEffectRenderMode = 3;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_9A(void) {
    s32 actor;
    sFieldScriptEntity* entity;

    actor = readCharacter(2);
    if (actor != 0xFF) {
        entity = fieldSceneState.m290_actorArray[actor].m4C_scriptEntity;
        if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] & 1) {
            entity->mFC_spriteColor[0] = getImmediateOrVariableUnsigned(3);
            entity->mFC_spriteColor[1] = getImmediateOrVariableUnsigned(5);
            entity->mFC_spriteColor[2] = getImmediateOrVariableUnsigned(7);
        }
        if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] & 2) {
            entity->mFF_spriteColor2[0] = getImmediateOrVariableUnsigned(3);
            entity->mFF_spriteColor2[1] = getImmediateOrVariableUnsigned(5);
            entity->mFF_spriteColor2[2] = getImmediateOrVariableUnsigned(7);
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 9;
}

void OPX_5F(void) {
    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] & 1) {
        pCurrentFieldScriptActor->mFC_spriteColor[0] = getImmediateOrVariableUnsigned(2);
        pCurrentFieldScriptActor->mFC_spriteColor[1] = getImmediateOrVariableUnsigned(4);
        pCurrentFieldScriptActor->mFC_spriteColor[2] = getImmediateOrVariableUnsigned(6);
    }
    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] & 2) {
        pCurrentFieldScriptActor->mFF_spriteColor2[0] = getImmediateOrVariableUnsigned(2);
        pCurrentFieldScriptActor->mFF_spriteColor2[1] = getImmediateOrVariableUnsigned(4);
        pCurrentFieldScriptActor->mFF_spriteColor2[2] = getImmediateOrVariableUnsigned(6);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

void OP_SET_TRANSPARENCY_MODE(void) {
    setTransparencyMode(fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet, getImmediateOrVariableUnsigned(1));
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_SET_SCREEN_BOUNDS_PADDING(void) {
    OPX8E_param0 = getImmediateOrVariableUnsigned(1);
    OPX8E_param1 = getImmediateOrVariableUnsigned(3);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OPX_8D(void) {
    op9DVar1.m1B8_keptSoundChannels = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_8C(void) {
    s32 a;
    s32 b;

    a = getImmediateOrVariableUnsigned(3) << 1;
    b = getImmediateOrVariableUnsigned(1);
    fadeSoundEffectChannelVolume(a, b, getImmediateOrVariableUnsigned(5));
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OPX_62(void) {
    s32 a;

    a = getImmediateOrVariableUnsigned(3) << 1;
    setSoundEffectChannelVolume(a, getImmediateOrVariableUnsigned(1));
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OPX_63(void) {
    s32 a;

    a = getImmediateOrVariableUnsigned(3) << 1;
    setSoundEffectChannelPan(a, getImmediateOrVariableUnsigned(1));
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OPX_65(void) {
    s32 a;

    a = getImmediateOrVariableUnsigned(1);
    playFieldSoundEffect(a, getImmediateOrVariableUnsigned(3));
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OPX_66(void) {
    s32 a;
    s32 b;
    s32 c;

    a = getImmediateOrVariableUnsigned(1);
    b = getImmediateOrVariableUnsigned(5);
    c = getImmediateOrVariableUnsigned(3);
    OP_C4SubSub1(a, b, c, getImmediateOrVariableUnsigned(7));
    pCurrentFieldScriptActor->mCC_scriptPC += 9;
}

void OPX_64(void) {
    s32 a;

    a = getPlayingSoundEffectMask(-1);
    if ((a & (getImmediateOrVariableUnsigned(1) << 8)) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 3;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC - 1;
    }
    g_breakCurrentScript = 1;
}

void OP_PLAY_SOUND_EFFECT(void) {
    playFieldSoundEffect(getImmediateOrVariableUnsigned(1), 3);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_5D(void) {
    s32 a;
    s32 b;

    a = getImmediateOrVariableUnsigned(1);
    b = getImmediateOrVariableUnsigned(5);
    OP_C4SubSub1(a, b, getImmediateOrVariableUnsigned(3), 3);
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OP_72(void) {
    if (playMusicAuthorized == 0) {
        g_breakCurrentScript = 1;
    } else {
        musicVar1 = 0;
        playMusicFromScript();
    }
}

void OP_PLAY_MUSIC(void) {
    if (playMusicAuthorized == 0) {
        g_breakCurrentScript = 1;
    } else {
        musicVar1 = -1;
        playMusicFromScript();
    }
}

void playMusicFromScript(void) {
    s32 music;

    music = getImmediateOrVariableUnsigned(1);
    if (fieldExecuteVar1 == 0) {
        clearMusic2();
        if (music != currentlyPlayingMusic) {
            resetBattleAudioState();
            fieldMusicLoadPending = -1;
        }
        currentlyPlayingMusic = music;
        pCurrentFieldScriptActor->mCC_scriptPC += 3;
        return;
    }
    if (isLoadCompleted() != 0 || playMusicAuthorized == 0) {
        g_breakCurrentScript = 1;
        return;
    }
    if (musicVar2 == 1) goto brk1;
    if (fieldMusicLoadPending == -1) goto brk2;
        if (music != currentlyPlayingMusic) {
            resetBattleAudioState();
            currentlyPlayingMusic = music;
            fieldMusicLoadPending = -1;
            playMusic(music, 0);
        }
        pCurrentFieldScriptActor->mCC_scriptPC += 3;
    return;
brk1:
    g_breakCurrentScript = 1;
    return;
brk2:
    g_breakCurrentScript = 1;
}

void OP_SETUP_CAMERA_SHAKE(void) {
    s32 x;
    s32 y;
    s32 z;
    s32 steps;

    x = getImmediateOrVariableUnsigned(1);
    y = getImmediateOrVariableUnsigned(3);
    z = getImmediateOrVariableUnsigned(5);
    steps = getImmediateOrVariableUnsigned(7);
    if (steps == 0) {
        steps = 1;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 9;
    fieldCameraShakeCounter = steps;
    fieldSceneState.m1A8_cameraShakeEnabled = 1;
    fieldSceneState.m1BC_cameraShakeStep[0] = ((x << 16) - fieldSceneState.m1B0_cameraShakeAmplitude[0]) / steps;
    fieldSceneState.m1BC_cameraShakeStep[1] = ((z << 16) - fieldSceneState.m1B0_cameraShakeAmplitude[1]) / steps;
    fieldSceneState.m1BC_cameraShakeStep[2] = ((y << 16) - fieldSceneState.m1B0_cameraShakeAmplitude[1]) / steps;
    if (x == 0 && z == 0 && y == 0) {
        fieldCameraShakeCounter = steps + 2;
        fieldSceneState.m1AC_cameraShakeStopping = 1;
    } else {
        fieldSceneState.m1AC_cameraShakeStopping = 0;
    }
}
