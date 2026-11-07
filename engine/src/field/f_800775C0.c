/* f_800775C0.c (US 0x800775C0-0x80078B5C).
 *
 * COMPILER: gcc-2.7.2-psx, -O2 -G0, ASPSX 2.34. -G0 is required: with -G8
 * GCC treats 8-byte externs (RECT compassDataRect) as small data and CSEs their
 * addresses differently (initCompassData). gcc-2.6.3/2.6.0 fail
 * loadMechaOverlay_finalize and fieldEntryPoint; gcc-2.7.2-cdk fails 9 of 13. */
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "main/sound.h"
#include "pollhost.h"
#include "field/field.h"
#include "field/screen_effects.h"

extern s32 fieldMapNumber;
extern s16 fieldBattleMusicId; /* op9DVar1.m10C_battleMusicId, addressed on its own (retail codegen) */
/* "Clear OTAG": retail pads it with a stray 'k' (0x8006FB8B), so the
 * string stays assembly .rodata. */
extern char strClearOT[];
extern s32 compassGraphicDataLoaded;                  /* compassGraphicDataLoaded */
extern u32* compassGraphicDataStaging;                 /* compassGraphicDataStaging */
/* compassImageRects (Noah: compass.cpp): 8 rows of imageX, imageY, palX, palY,
 * palW, palH; retail indexes it as a flat s16 array (row * 6 + column). */
extern s16 compassTimVramCoords[8 * 6];
extern s16 D_800C2690;
extern s16 D_800C2692;
extern s16 D_800C38FC;
extern s16 D_800C38FE;
extern RECT compassDataRect;                 /* compassDataRect */
void loadTimToVram(u32* tim, s16 x, s16 y, s16 palX, s16 palY, s16 palW, s16 palH); /* loadTimToVram */
u32 doPointerRelocation(u32* data);
extern s32 fieldGpuTime;
extern void* partyCharacterBuffers[3];
extern s32 fieldProfileTimestamp;
extern s32 fieldDebugDisable;
extern s32 bBattleSequenceInitialized;
extern s16 updateEntityEventCode4Var0;
extern s32 g_playerControlledActor;
extern FieldBss_800AF880 fieldSceneState;
extern FieldMechaState mechaList;
extern LoadingBatchCommand mechaOverlayBatchLoadingTable[16]; /* mechaOverlayBatchLoadingTable */
extern void* mechaDataTable2[4]; /* one per mecha (FieldMechaState.m0_mechaList) */
extern void* mechaDataTable1[4];
extern void* mechaOverlayBuffer;                   /* mechaOverlayBuffer */
extern s32 mechaOverlayLoadingMode;
extern uptr fieldHeapTopMarker;
void waitForLoadCompleted(void);                  /* waitForLoading */
void reallocBackBufferRamCopy(s32);                   /* reallocBackBufferRamCopy */
extern s16 numMaxMechaOverlayEntries;                  /* numMaxMechaOverlayEntries */
extern MATRIX* mechaFinalizeVar0;              /* mecha overlay */
void ClearCacheAfterOverlayLoad(void);
void mechaInitOverlay(s32);                /* mecha overlay: mechaInitOverlay */
/* fieldEntryPoint */
extern s32 runningOnDTL;                  /* runningOnDTL */
extern s32 fieldScriptEntityAlreadyInitialized;
extern s32 bootModeReady;
extern s32 playMusicAuthorized;
extern s32 fieldExecuteVar2;
extern s32 fieldExecuteVar3;
extern s32 battlingModeReady;
extern s32 updateMusicState2Var1;
extern s32 musicVar2;
extern s32 positionInScratchBufferForMove;
extern s32 fieldGraphicsUploaded;
extern s32 isBackBufferRamCopyEnabled;
extern s32 fieldMovieStarted;
extern s32 D_800ADC04;
extern GameState* pKernelGameState;           /* pKernelGameState */
extern s32 fieldBootedOnce;
extern u8 menuReturnState0;
extern s32 currentlyPlayingMusic;
extern s32 fieldRequestedGears;
extern u8 fieldTransitionCompleted;
extern s32 g_playTimeInVsync;                  /* play time in vsyncs */
extern s32 g_frameOddOrEven;                  /* g_frameOddOrEven */
extern u16 g_padButtonForField;
extern u16 padButtonForField2;
extern u16 padButtonForScripts;
extern s32 currentFieldId1;
extern void* rawFieldBundle;
extern s32 fieldMusicBeforeBattle;
extern u8 battleTransitionEffect;
extern FieldBss_800B2184 op9DVar1;
extern s32 currentlyLoadedMusic;
extern s32 updateMusicState2Var2;
extern s32 fieldMusicLoadPending;
extern s32 fieldBattleMusicPending;
extern s32 battleStartSaveWritePending;
extern s32 D_8004F310;
extern s32 fieldChangePrevented;
extern s32 asyncLoadingVar1;
extern s32 load2dAnimVar;
extern s32 fieldCompassVar;
extern s32 disableCharacterShadowsRendering;
extern s32 disableMechaRendering;
extern s32 playerCanRun;
extern s32 g_menuIdToOpen;
extern u8 gearSwitchDisabled;
extern s32 iRam800adb70;
extern u8 g_menuDisabled;
extern u16 g_menuOpenArgFromScript;
extern u8 g_menuOpenArg;
extern s32 typeOfPlayableCharacterLoaded;
/* Retail passes loadFieldSeq an argument, which it ignores. */
#define loadFieldSeq(unused) loadFieldSeq()
void func_802811EC(void);
void setVar(s32, s32);
void updatePartyFileSet(void);
void processLoadedPartyFiles(void);
void resetParticleCreationSetup(s32);
void bootField(void);
s32 isControllerConnected(s32);
void pauseMusic(void);
void resumeMusic(void);
void uploadBuiltinTexture(s32, s32);
void checkSoftReboot(void);
void updateAndRenderField(void);
void renderFullScreenTransitionEffect(void);
s32 isFieldTransitionPermittedByLoading(void);
s32 isFieldBattlePrevented(void);
void releaseAllDialogWindows(void);
void resetBattleAudioState(void);
/* Retail passes playMusic a second argument, which it ignores. */
#define playMusic(music, unused) playMusic(music)
void saveFieldStateToSaveBlock(void);
void saveStateToKernel(void);
void transitionFields(void);
void resetInputs(void);
void updateGearSwitchDisabledFlag(void);
void togglePartyGears(void);
void showFieldMapScreen(void);
void startFieldTransition(void);
void openFieldMenu(void);
void updateMusicState(void);
void restoreBackBufferRamCopy(void);
void deleteAllParticleEffects(void);
void stopFieldSoundEffects(void);
void freeFieldData(void);
void unloadFieldSeq(void);
void fieldChangeGameMode(s32);
void initFieldMouse(void);
s32 loadNewField(s32, s32);
void loadMechaOverlay_start(void);
void loadMechaOverlay_finalize(void);
void clearFieldOrderingTable(void);
void updateFieldInputs(void);
void func_80281B00(const char*);
void syncKernelAndFieldStates(void);

void initFieldMouseAndDirectory(void) {
    resetMemoryAllocStats(8, 0);
    setCurrentDirectory(4, 0);
    initFieldMouse();
}

void flushFrame(void) {
    DrawSync(0);
    VSync(0);
}

void initCompassData(void) {
    s32 i;
    u32* ptrs;

    if (compassGraphicDataLoaded == 0) {
        compassGraphicDataStaging = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(0xA7)), 1);
        flagAllocation(compassGraphicDataStaging);
        readFile(0xA7, compassGraphicDataStaging, 0, 0x80);
        waitReadCompletion(0);
    }
    unflagAllocation(compassGraphicDataStaging);
    compassGraphicDataLoaded = 0;
    D_800C2692 = 0;
    D_800C2690 = 0;
    D_800C38FE = 0;
    D_800C38FC = 0;
    ptrs = compassGraphicDataStaging;
    for (i = 0; i < 8; i++) {
        ptrs++;
        /* The table stays unrelocated on the PC: offsets from its start. */
        loadTimToVram((u32*)((u8*)compassGraphicDataStaging + *ptrs), compassTimVramCoords[i * 6], compassTimVramCoords[i * 6 + 1],
                      compassTimVramCoords[i * 6 + 2], compassTimVramCoords[i * 6 + 3], compassTimVramCoords[i * 6 + 4], compassTimVramCoords[i * 6 + 5]);
        DrawSync(0);
    }
    compassDataRect.x = 0;
    compassDataRect.y = 0xFB;
    compassDataRect.w = 0x10;
    compassDataRect.h = 1;
    StoreImage(&compassDataRect, (u32*)compassData2);
    DrawSync(0);
    freeHeap(compassGraphicDataStaging);
}

void loadInitialField(void) {
    waitReadCompletion(0);
    while (loadNewField((fieldMapNumber & 0xFFF) * 2, 0) != 0) {
    }
}

void recordFieldGpuTime(void) {
    fieldGpuTime = VSync(1);
}

void initMechaFieldArgs2(MATRIX* m, s32 m00, s32 m01, s32 m02, s32 m10, s32 m11, s32 m12, s32 m20, s32 m21, s32 m22) {
    m->m[0][0] = m00;
    m->m[0][1] = m01;
    m->m[0][2] = m02;
    m->m[1][0] = m10;
    m->m[1][1] = m11;
    m->m[1][2] = m12;
    m->m[2][0] = m20;
    m->m[2][1] = m21;
    m->m[2][2] = m22;
}

void loadMechaOverlay_start(void) {
    s32 i;
    uptr tmp;

    if (mechaList.m88_numMechas != 0) {
        waitForLoadCompleted();
        setCurrentDirectory(4, 0);
        reallocBackBufferRamCopy(0);
        tmp = fieldHeapTopMarker;
        if (mechaOverlayLoadingMode == 0) {
            /* field_mecha is linked in, so no padding up to its link address:
             * a token block keeps the frees balanced. */
            mechaOverlayBuffer = allocateHeap(PC_HEAP_SIZED(4, pcHeapReservation((void*)tmp, 0x801DC000u)), 1);
        } else {
            mechaOverlayBuffer = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(0x6B9)), 1);
        }
        reallocBackBufferRamCopy(1);
        for (i = 0; i < mechaList.m88_numMechas; i++) {
            mechaOverlayBatchLoadingTable[i * 2 + 1].m0_fileIndex = mechaList.m0_mechaList[i] + 0x6BB;
            mechaOverlayBatchLoadingTable[i * 2 + 1].m4_loadPtr = mechaDataTable1[i] =
                allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(mechaList.m0_mechaList[i] + 0x6BB)), 1);
        }
        for (i = 0; i < mechaList.m88_numMechas; i++) {
            mechaOverlayBatchLoadingTable[i * 2].m0_fileIndex = mechaList.m0_mechaList[i] + 0x6BA;
            mechaOverlayBatchLoadingTable[i * 2].m4_loadPtr = mechaDataTable2[i] =
                allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(mechaList.m0_mechaList[i] + 0x6BA)), 0);
        }
        /* Only the gear files are read: field_mecha starts as a fresh load
         * leaves it. */
        mechaOverlayBatchLoadingTable[i * 2].m0_fileIndex = 0;
        mechaOverlayBatchLoadingTable[i * 2].m4_loadPtr = NULL;
        if (portOverlayCd()) { /* XG_OVERLAY_CD: the PS1's batch, field_mecha's file discarded */
            mechaOverlayBatchLoadingTable[i * 2].m0_fileIndex = 0x6B9;
            mechaOverlayBatchLoadingTable[i * 2].m4_loadPtr =
                mechaOverlayLoadingMode == 1 ? mechaOverlayBuffer : portOverlayScratch(0, getFileSizeAligned(0x6B9));
            mechaOverlayBatchLoadingTable[i * 2 + 1].m0_fileIndex = 0;
            mechaOverlayBatchLoadingTable[i * 2 + 1].m4_loadPtr = NULL;
        }
        if (mechaOverlayLoadingMode == 0) {
            portResetModule("field_mecha");
        }
        waitForLoadCompleted();
        batchStartLoadingFiles(mechaOverlayBatchLoadingTable, 0, 0);
    }
}

void loadMechaOverlay_finalize(void) {
    s32 i;
    SVECTOR* v;

    if (mechaList.m88_numMechas != 0) {
        waitForLoadCompleted();
        ClearCacheAfterOverlayLoad();
        mechaInitOverlay(numMaxMechaOverlayEntries);
        mechaFinalizeVar0 = &mechaList.m40_mechaFieldArgs2[1];
        SetBackColor(mechaList.m80_mechaBackColor[0], mechaList.m80_mechaBackColor[1],
                     mechaList.m80_mechaBackColor[2]);
        for (i = 0; i < mechaList.m88_numMechas; i++) {
            v = &mechaList.m10_initMechaTempVar[i];
            v->vx = 0;
            v->vy = 0;
            v->vz = 0;
            mechaInitNewMecha(i, 0, mechaDataTable2[i], mechaDataTable1[i],
                          0x240 - (i + mechaList.m83_mechaList2[i]) * 0x40, 0x100, 0, i + 0xFC,
                          v);
            freeHeap(mechaDataTable1[i]);
            mechaList.m30_mechaList3[i] = loadedMechas[i]->m1C_scale;
        }
        resetMemoryAllocStats(8, 0);
    }
}

void loadMechaOVerlayAndRegisterMechas(void) {
    loadMechaOverlay_start();
    loadMechaOverlay_finalize();
}

void allocatePartyCharacterBuffers(void) {
    resetMemoryAllocStats(8, 0);
    partyCharacterBuffers[0] = allocateHeap(PC_HEAP_BYTES(0x14000), 0);
    partyCharacterBuffers[1] = allocateHeap(PC_HEAP_BYTES(0x14000), 0);
    partyCharacterBuffers[2] = allocateHeap(PC_HEAP_BYTES(0x14000), 0);
    flagAllocation(partyCharacterBuffers[0]);
    flagAllocation(partyCharacterBuffers[1]);
    flagAllocation(partyCharacterBuffers[2]);
}

void freePartyCharacterBuffers(void) {
    unflagAllocation(partyCharacterBuffers[0]);
    unflagAllocation(partyCharacterBuffers[1]);
    unflagAllocation(partyCharacterBuffers[2]);
    freeHeap(partyCharacterBuffers[0]);
    freeHeap(partyCharacterBuffers[1]);
    freeHeap(partyCharacterBuffers[2]);
}

void pcFieldSubAdvance(void);
s32 pcFieldOffTick(void);
void pcFieldPreviewSettle(void); /* f_8008083C.c, xg-hbc.19.3 */
s32 pcFieldNativeFrameGoesOn(void);
void pcFieldNativeFrameDone(void);
s32 pcFieldNativeParity(void); /* f_80072140.c */
void fieldPerFrameReset(void) {
    fieldProfileTimestamp = VSync(1);
    clearFieldOrderingTable();
    /* Enhanced: the 60 Hz tick parity (pcFieldOffTick) advances here, where the
     * frame starts, so that updateFieldInputs' carry of the frame between's
     * presses sees this frame's parity. Advanced in updateAndRenderField, it
     * lagged the input update by a frame: an off-tick press was never carried
     * (a sign's dialogue could not be closed) and a tick frame's own press was
     * presented again two frames later (the dialogue reopened). */
    pcFieldFrameIndex++;
    pcFieldSubAdvance(); /* xg-hbc.19.3: the frame's place in the native frame (f_80072140.c) */
    updateFieldInputs();
    if (fieldDebugDisable == 0) {
        func_80281B00(strClearOT);
    }
    if (!pcFieldOffTick()) {
        /* Enhanced (xg-hbc.19.3): the kernel's copy of the field (the variables of the leader's
         * place and facing, the play time, the timer) once a native frame, from its state: a frame
         * between would copy its preview, which stays there when the next tick does not sync */
        syncKernelAndFieldStates();
    }
}

/* Noah isFieldBattlePrevented: -1 while the player is jumping during a battle sequence */
s32 isFieldBattlePrevented(void) {
    if (bBattleSequenceInitialized == 1 && updateEntityEventCode4Var0 == 0 && (fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m0_fieldScriptFlags & 0x800)) {
        return -1;
    }
    return 0;
}

void pcFieldWideExit(void);
void pcGauntletFieldTick(void); /* port/src/game/field/gauntlet_field.c */
void pcArenaFieldTick(void); /* port/src/game/field/arena_field.c */

void fieldEntryPoint(void) {
    s32 midNative = 0; /* Enhanced: inside a native frame (its frames between to come) */
    s32 mode;
    s32 firstFrame;
    s32 comboHeld;
    s32 playTime;
    RECT unused;

    if (runningOnDTL != -1) {
        fieldDebugDisable = 0;
    } else {
        fieldDebugDisable = 1;
    }
    pcFieldWideExit(); /* nothing blocked from before (a soft reset mid-movie) */
    ClearCacheAfterOverlayLoad();
    if (fieldDebugDisable == 0) {
        DrawSyncCallback(recordFieldGpuTime);
    }
    fieldWdsSlot1 = wdsFile3;
    fieldWdsSlots[3] = wdsFile5;
    resetMemoryAllocStats(8, 0);
    if (fieldDebugDisable == 0 && mechaOverlayLoadingMode == 0) {
        setCurrentDirectory(4, 0);
        readFile(0xAD, (void*)0x80280000, 0, 0x80);
        waitReadCompletion(0);
        ClearCacheAfterOverlayLoad();
    }
    if (fieldScriptEntityAlreadyInitialized == 0) {
        asyncPartyCharacterLoadingTable[0] = asyncPartyCharacterLoadingTable[1] = asyncPartyCharacterLoadingTable[2] = 0xFF;
    }
    loadFieldSeq(0);
    allocatePartyCharacterBuffers();
    comboHeld = 0;
    bootModeReady = playMusicAuthorized = fieldExecuteVar2 = fieldExecuteVar3 = battlingModeReady = -1;
    updateMusicState2Var1 = 0;
    musicVar2 = 0;
    positionInScratchBufferForMove = 0;
    fieldGraphicsUploaded = 0;
    isBackBufferRamCopyEnabled = 0;
    fieldMovieStarted = 0;
    D_800ADC04 = 2;
    if (fieldDebugDisable == 0) {
        func_802811EC();
    }
    initFieldMouseAndDirectory();
    pKernelGameState = &gameState;
    fieldMapNumber = gameState.m231A_fieldID;
    gameState.m1930_fieldVarsBackup[1] = gameState.m2320_worldmapMode;
    gameState.m1930_fieldVarsBackup[4] = gameState.m231C_cameraYaw >> 9;
    if (fieldBootedOnce == 0) {
        menuReturnState0 = 0;
        currentlyPlayingMusic = 0xFF;
    } else {
        currentlyPlayingMusic = gameState.m2322_previouslyPlayingMusic;
    }
    if (fieldDebugDisable == 1) {
        pKernelGameState->m1930_fieldVarsBackup[40] = 1;
        setVar(0x50, 1);
    }
    initCompassData();
    fieldRequestedGears = 0;
    updatePartyFileSet();
    processLoadedPartyFiles();
    fieldHeapTopMarker = (uptr)allocateHeap(PC_HEAP_BYTES(4), 1);
    if (fieldDebugDisable == 0) {
        POLLHOST();
        resetParticleCreationSetup(g_playerControlledActor);
        particleCreationSetup.m0_configs[0].m0 = 1;
        particleCreationSetup.m0_configs[0].m6_maxParticles = 0x10;
    }
    bootField();
    firstFrame = 0;
    fieldTransitionCompleted = 1;

    while (1) {
#if !VERSION_JP
        /* US only: wait while the controller is disconnected */
        if (!midNative && isControllerConnected(0) == 0) {
            pcFieldPreviewSettle(); /* Enhanced: the native frame back before the wait (xg-hbc.19.3) */
            playTime = g_playTimeInVsync;
            pauseMusic();
            uploadBuiltinTexture(0x88, (((g_frameOddOrEven + 1) & 1) << 8) | 100);
            do {
                DrawSync(0);
                VSync(2);
                updateFieldInputs();
                checkSoftReboot();
            } while (isControllerConnected(0) == 0);
            resumeMusic();
            g_playTimeInVsync = playTime;
        }
#endif
        if (!midNative && (g_padButtonForField & 0x800) && !(padButtonForScripts & 0x40) && op9DVar1.m1D4_pauseDisabled == 0) {
            pcFieldPreviewSettle(); /* Enhanced (xg-hbc.19.3) */
            playTime = g_playTimeInVsync;
            pauseMusic();
            uploadBuiltinTexture(0x88, (((g_frameOddOrEven + 1) & 1) << 8) | 100);
            do {
                DrawSync(0);
                VSync(2);
                updateFieldInputs();
                checkSoftReboot();
            } while (!(g_padButtonForField & 0x800));
            resumeMusic();
            g_playTimeInVsync = playTime;
        }
        if (fieldDebugDisable == 1) {
            setVar(0x50, 1);
        }
        checkSoftReboot();
        fieldPerFrameReset();
        updateAndRenderField();
        if (pcFieldNativeFrameGoesOn()) {
            /* Enhanced (xg-hbc.19.3): the rest of the loop runs once a native frame, at its end (the
             * Original's vblank), with its presses and parity (pcFieldNativeFrameDone) */
            midNative = 1;
            continue;
        }
        midNative = 0;
        pcFieldNativeFrameDone();
        renderFullScreenTransitionEffect();
        if (pcFieldNativeParity() == 1 && playMusicAuthorized == 0 && isFieldTransitionPermittedByLoading() == 0 && isFieldBattlePrevented() == 0) {
            if (currentFieldId1 != -1) {
                unflagAllocation(rawFieldBundle);
                freeHeap(rawFieldBundle);
            }
            pcFieldPreviewSettle(); /* Enhanced: a battle starts from the native frame (xg-hbc.19.3) */
            if (firstFrame == 0) {
                firstFrame = 1;
                fieldMusicBeforeBattle = currentlyPlayingMusic;
            }
            releaseAllDialogWindows();
            if (bBattleSequenceInitialized == 1) {
#if VERSION_JP
                /* JP reaches both through the aggregate: one base register */
                battleTransitionEffect = op9DVar1.m1D1_randomBattleTransitionEffect;
                fieldMusicBeforeBattle = currentlyPlayingMusic;
                if (currentlyLoadedMusic != op9DVar1.m10C_battleMusicId) {
                    if (currentlyLoadedMusic != -1) {
                        updateMusicState2Var2 = 1;
                    }
                    resetBattleAudioState();
                    fieldMusicLoadPending = -1;
                    currentlyPlayingMusic = op9DVar1.m10C_battleMusicId;
                    playMusic(op9DVar1.m10C_battleMusicId, 1);
                }
#else
                battleTransitionEffect = op9DVar1.m1D1_randomBattleTransitionEffect;
                fieldMusicBeforeBattle = currentlyPlayingMusic;
                if (currentlyLoadedMusic != fieldBattleMusicId) {
                    if (currentlyLoadedMusic != -1) {
                        updateMusicState2Var2 = 1;
                    }
                    resetBattleAudioState();
                    fieldMusicLoadPending = -1;
                    currentlyPlayingMusic = fieldBattleMusicId;
                    playMusic(fieldBattleMusicId, 1);
                }
#endif
                bBattleSequenceInitialized = 0;
                fieldBattleMusicPending = 1;
            } else {
                if (battleStartSaveWritePending == 0) {
                    fieldScriptEntityAlreadyInitialized++;
                    saveFieldStateToSaveBlock();
                }
                mode = 0;
                if (fieldBattleMusicPending == 1) {
                    playBattleMusic(pMusic, 0x7F, 0);
                }
                fieldBattleMusicPending = 0;
                break;
            }
        }
        if (fieldChangePrevented == 0 && fieldMusicLoadPending == 0 && asyncLoadingVar1 == 0xFF && load2dAnimVar == 0 &&
            loadNewField((fieldMapNumber & 0xFFF) * 2, 0) == 0 && isCDBusy() == 0 && fieldScreenEffects.m4C_screenEffects[0].m54_duration == 0) {
            fieldTransitionCompleted = 0;
            pcFieldPreviewSettle(); /* Enhanced (xg-hbc.19.3) */
            saveStateToKernel();
            waitReadCompletion(0);
            transitionFields();
            resetInputs();
            fieldTransitionCompleted = 1;
        }
        if (pcFieldNativeParity() == 1) {
            if (fieldExecuteVar3 == 0 && isFieldTransitionPermittedByLoading() == 0) {
                pcFieldPreviewSettle(); /* Enhanced (xg-hbc.19.3) */
                waitReadCompletion(0);
                mode = 1;
                if (currentFieldId1 != -1) {
                    unflagAllocation(rawFieldBundle);
                    freeHeap(rawFieldBundle);
                }
                break;
            }
            if (pcFieldNativeParity() == 1) {
                if (battlingModeReady == 0 && isFieldTransitionPermittedByLoading() == 0) {
                    pcFieldPreviewSettle(); /* Enhanced (xg-hbc.19.3) */
                    waitReadCompletion(0);
                    if (currentFieldId1 != -1) {
                        unflagAllocation(rawFieldBundle);
                        freeHeap(rawFieldBundle);
                    }
                    D_8004F310++;
                    saveFieldStateToSaveBlock();
                    mode = 2;
                    break;
                }
                if (pcFieldNativeParity() == 1 && bootModeReady == 0 && isFieldTransitionPermittedByLoading() == 0) {
                    pcFieldPreviewSettle(); /* Enhanced (xg-hbc.19.3) */
                    waitReadCompletion(0);
                    if (currentFieldId1 != -1) {
                        unflagAllocation(rawFieldBundle);
                        freeHeap(rawFieldBundle);
                    }
                    resetBattleAudioState();
                    mode = 3;
                    break;
                }
            }
        }
        if (fieldDebugDisable == 0) {
            if (padButtonForField2 & 0x40) {
                fieldCompassVar = (fieldCompassVar + 1) & 1;
            }
            if (padButtonForField2 & 0x10) {
                disableCharacterShadowsRendering = (disableCharacterShadowsRendering + 1) & 1;
            }
            if (padButtonForField2 & 0x80) {
                disableMechaRendering = (disableMechaRendering + 1) & 1;
            }
            if ((padButtonForScripts & 0x40) && (g_padButtonForField & 0x100) && fieldChangePrevented == -1 && fieldMusicLoadPending == 0 &&
                isBackBufferRamCopyEnabled == 0) {
                fieldMapNumber = 0;
                fieldChangePrevented = 0;
                setVar(2, 0);
            }
        }
        if (bootModeReady == -1 && playMusicAuthorized == -1 && fieldExecuteVar3 == -1 && isFieldTransitionPermittedByLoading() == 0 &&
            fieldChangePrevented == -1) {
            if ((padButtonForScripts & 3) == 0) {
                comboHeld = 0;
            }
            if ((padButtonForScripts & 1) && playerCanRun == 1 && (padButtonForScripts & 2) && comboHeld == 0) {
                comboHeld = 1;
                updateGearSwitchDisabledFlag();
                if (g_menuIdToOpen == 0xFF &&
                    (fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m0_fieldScriptFlags & 0x1800) == 0 &&
                    gearSwitchDisabled == 0) {
                    pcFieldPreviewSettle(); /* Enhanced (xg-hbc.19.3) */
                    togglePartyGears();
                }
            }
            if ((g_padButtonForField & 0x100) && fieldChangePrevented == -1 && playerCanRun == 1) {
                pcFieldPreviewSettle(); /* Enhanced (xg-hbc.19.3) */
                showFieldMapScreen();
            }
            if (iRam800adb70 != 0 && pcFieldNativeParity() == 1) {
                pcFieldPreviewSettle(); /* Enhanced (xg-hbc.19.3) */
                startFieldTransition();
                iRam800adb70 = 0;
            }
            if (g_menuIdToOpen != 0xFF && pcFieldNativeParity() == 0 &&
                (fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m0_fieldScriptFlags & 0x1800) == 0) {
                releaseAllDialogWindows();
                pcFieldPreviewSettle(); /* Enhanced: the menu opens on the native frame (xg-hbc.19.3) */
                openFieldMenu();
                g_menuIdToOpen = 0xFF;
            }
            if ((g_padButtonForField & 0x10) && g_menuDisabled == 0 && g_menuIdToOpen == 0xFF && playerCanRun == 1) {
                g_menuIdToOpen = 0x80;
                g_menuOpenArg = g_menuOpenArgFromScript;
            }
            if (g_menuDisabled == 0 && g_menuIdToOpen == 0xFF && playerCanRun == 1) {
                pcGauntletFieldTick(); /* a gauntlet's first fight (F1), where the menu could open */
            }
            pcArenaFieldTick(); /* F1 > Extras > Battle arena: its start and return (arena_field.c) */
        }
        updateMusicState();
    }
    pcFieldPreviewSettle(); /* Enhanced (xg-hbc.19.3) */
    updateGearSwitchDisabledFlag();
    restoreBackBufferRamCopy();
    syncKernelAndFieldStates();
    deleteAllParticleEffects();
    stopFieldSoundEffects();
    releaseAllDialogWindows();
    DrawSync(0);
    VSync(0);
    freeFieldData();
    freePartyCharacterBuffers();
    unloadFieldSeq();
    typeOfPlayableCharacterLoaded = 0;
    freeHeap((void*)fieldHeapTopMarker);
    pcFieldWideExit(); /* port/src/game/field/wide.c */
    fieldChangeGameMode(mode);
}
