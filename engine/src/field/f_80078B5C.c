
#include "common.h"
s32 pcFieldNativeFrameGoesOn(void); /* f_80072140.c, xg-hbc.19.3 */
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "main/menu.h"
#include "main/sound.h"
#include "field/field.h"
#include "gte.h"
#include "pc_gameplay.h"
#include "pc_slide.h"

void getTriangleNormalAndAdjustY(SVECTOR* vec0, SVECTOR* vec1, SVECTOR* vec2, SVECTOR* outputPosition, VECTOR* pOutputTriangleNormal);
s32 updateEntityEventCode3Sub3Sub2(s16 angle, SVECTOR* edge, VECTOR* delta, s32 unused);
s32 updateEntityEventCode3Sub4Sub1(VECTOR* delta, VECTOR* position, sFieldScriptEntity* entity, SVECTOR* edge, SVECTOR* hit, s32 mode);
extern s32 updateEntityEventCode3Var1;
/* random encounters (Noah: fieldRandomBattles.cpp) */
extern FieldBss_800B2184 op9DVar1;
extern FieldBss_800AF880 fieldSceneState;
extern s32 playMusicAuthorized;
extern s32 fieldExecuteVar3;
extern s32 fieldChangePrevented;
extern s16 fieldRandomBattleVar;
extern u8 fieldTransitionCompleted;
extern s32 bBattleSequenceInitialized;
extern s32 fieldDebugDisable;
extern u8 encounterProbabilityWeight[16];                /* Noah: encounterProbabilityWeight (main) */
extern u8 requestedBattleConfig;                    /* Noah: battleInitVar2 (main) */
extern u8 battleInitVar1;                    /* Noah: battleInitVar1 (main) */
void setInitialEncounterData(void);
void EntityMoveCheck1Sub1Sub1(s32* a, s32* b, s32 c);
/* bootField (Noah) */
extern s32 fieldBootedOnce;
extern u8 menuReturnState0;
extern s32 NumMechas;
extern s32 fieldTransitionInProgress;
extern s32 fieldGraphicsUploaded;
extern s32 transitionStripScale;
extern void* fieldGraphicsAllocation;
extern s32 worldmapSoundFilesPendingCount;
extern s32 currentlyLoadedMusic;
extern s32 updateMusicState2Var2;
void initFontSystem(void);
void saveBackBufferRamCopy(void);
void loadInitialField(void);
void convert24BitScreenTo15Bit(s32);
void initFieldDrawEnvs(void);
void copyScreenToTransitionStrips(void);
void copyScreenToVram(s32, s32);
void captureScreenForTransition(s32, s32);
void initFieldData(void);
void loadFieldGraphics(void);
void setMechaBytecodeToggle(s32);
void renderScreenTransitionPoly(void);
void renderScreenTransition(void);
void setFieldTransitionPolyColor(s32);
void brightenFieldCluts(void);
void runFieldResumeScript(void);
void resetInputs(void);
void clearMusic2(void);
/* Retail passes playMusic a second argument, which it ignores. */
#define playMusic(music, unused) playMusic(music)
void syncKernelAndFieldStates(void);
void setupRGBFaderSlot0_fadeIn(s32);
void fieldPerFrameReset(void);
void updateAndRenderField(void);
s32 updateMusicState(void);
void restoreBackBufferRamCopy(void);
void mergeFreeHeapBlocks(void);
void deinitDebugFont(void);
/* field menu (openFieldMenu) */
extern s32 g_menuIdToOpen;
extern u8 g_menuDisabled;
extern void* mechaOverlayBuffer;
extern uptr fieldHeapTopMarker;
extern void* menuSharedResources;
extern s16 menuVramSaveSources[12];               /* menu VRAM save areas: source (x, y) x6 */
extern s16 menuVramSaveDestinations[12];               /* menu VRAM save areas: destination (x, y) x6 */
extern u8 useDebugMenuList;
extern u8 menuToEnter;
extern s16 partyGearSwitchFlags[3];
extern s32 gDepthDivider;
extern u8 kernelAndFieldStatesSynced;
extern s32 fieldMapNumber;
extern s32 OPX_86Param;
extern s32 D_800ADB50;
extern s32 sceneSCRZ;
extern s32 fieldRequestedGears;
extern s32 typeOfPlayableCharacterLoaded;
extern void* partyCharacterBuffers[3];              /* Noah: partyCharacterBuffersRaw */
extern u8 renderCharsDisabled;
extern s32 g_menuOpenCount;
void SetDrawMode(DR_MODE* p, s32 dfe, s32 dtd, s32 tpage, RECT* tw);
void freePartyCharacterBuffers(void);
void moveImageSync(s16 w, s16 h, s16 x, s16 y, s32 dstX, s32 dstY);
void swapFieldRenderContext(void);
void renderFieldMenuFadeFrame(s32 level);
void updateGearSwitchDisabledFlag(void);
void ClearCacheAfterOverlayLoad(void);
void enterMenu(void);
void setVar(s32 index, s32 value);
void freeFieldGraphics(void);
void allocatePartyCharacterBuffers(void);
void updatePartyFileSet(void);
void processLoadedPartyFiles(void);
void runMenuReturnScript(void);
void loadGameModeOverlay(s32 mode);
void func_80281204(s32 encounter);
s32 updateEntityEventCode3Sub3Sub1(VECTOR* delta, VECTOR* position, sFieldScriptEntity* entity, SVECTOR* edge, SVECTOR* hit, s32 mode, u32* flags);
s32 length2d(s32 x, s32 y);
extern s16 compassQuadCornersX[9][4];            /* quad shapes: corner x */
extern s16 compassQuadCornersZ[9][4];            /* quad shapes: corner z */
extern s16 compassQuadCornersU[9][4];            /* quad shapes: corner u */
extern s16 compassQuadCornersV[9][4];            /* quad shapes: corner v */
extern s16 compassQuadTextures[4][6];            /* quad textures: GetTPage(tp, abr, x, y), GetClut(x, y) */
extern s32 loadCompleted;
/* ---- globals ---- */
extern s32 fieldMusicLoadPending;
extern s32 currentlyPlayingMusic;
extern s32 g_updateAllEntitiesSub2Var0;
extern s16 updateEntityEventCode4Var0;
s32 updateMusicState2(s32);
extern u8 battleEventReturnMode;
extern GameState* pKernelGameState;
extern s32 fieldMusicBeforeBattle;
extern s32 mechaOverlayLoadingMode;                  /* Noah: mechaOverlayLoadingMode? */
extern s16 playWorldmapMusic;
extern s32 fieldScriptEntityAlreadyInitialized;
extern s32 D_8004F310;
extern s32 newBootMode;
void saveStateToKernel(void);
void resetBattleAudioState(void);
void startReloadingResidentWds(void);
void finishReloadingResidentWds(void);
void setGameMode(s32 mode);
void initGameState(void);
void bootGame(s32);
extern s32 g_frameOddOrEven;
extern sFieldRenderContext* pCurrentFieldRenderingContext;
extern TILE fieldMenuFadeTiles[2];
extern DR_MODE fieldMenuFadeDrawModes[2];
extern RECT fieldMenuMoveRect;
void clearFieldOrderingTable(void);
void pcFieldWideMenu(s32 open); /* port/src/game/field/wide.c */
void flushFrame(void);
void EnterCriticalSection(void);               /* EnterCriticalSection */
void FlushCache(void);
void ExitCriticalSection(void);               /* ExitCriticalSection */
extern s32 gearSwitchingEnabled;
extern s32 g_playerControlledActor;
extern u8 gearSwitchDisabled;
/* mouse (ref decomp: FieldSetControllerBuffers/SetMouseSpeed/SetMousePosition) */
extern s8* mousePadBuffers[2];               /* controller receive buffers, per port */
extern u16 mouseSpeedX;                  /* mouse speed x */
extern u16 mouseSpeedY;                  /* mouse speed y */
extern s32 mouseCursorX[2];               /* mouse x, per port (scaled by speed) */
extern s32 mouseCursorY[2];               /* mouse y, per port */
extern s32 mouseCursorMinX;                  /* mouse x min */
extern s32 mouseCursorMinY;                  /* mouse y min */
extern s32 mouseCursorMaxX;                  /* mouse x max */
extern s32 mouseCursorMaxY;                  /* mouse y max */
void updateMouseCursor(s32 port);
s32 getAngleSin(s32 angle);
s32 getAngleCos(s32 angle);
extern s16 entityMoveSpeedFactor;
extern s32 positionInScratchBufferForMove;                  /* Noah: positionInScratchBufferForMove (words) */
s32 getLargestMagnitudeComponent(s32 x, s32 y, s32 z);

void pcFieldPreviewRestore(void); /* f_8008083C.c */
void pcFieldNativeEndScope(void); /* f_80072140.c */
s32 updateMusicState(void) {
    s32 n;

    if (pcFieldNativeFrameGoesOn()) {
        /* Enhanced (xg-hbc.19.3): what follows the frame runs once a native frame, at its end (the
         * Original's vblank: the music load polls the drive, the input lock counts down), from the
         * native frame's state (the preview put back first: the random stir below is the game's) */
        return g_updateAllEntitiesSub2Var0;
    }
    pcFieldPreviewRestore();
    pcFieldNativeEndScope();
    if (!pcFieldOffTick()) rand(); /* Enhanced: the per-frame stir at the native cadence (the scripts' RNG sequence) */
    /* Enhanced (xg-hbc.13): the music load is a state machine that advances a
     * step per call (start the read, wait for the drive, start the instance),
     * polled by the scripts' field-change opcodes; a step per 60 Hz frame
     * cleared it a native frame early and Lahan's exit walk started a frame
     * before the Original's. It steps on the tick frames. */
    if (fieldMusicLoadPending == -1 && !pcFieldOffTick()) {
        fieldMusicLoadPending = updateMusicState2(currentlyPlayingMusic);
    }
    n = g_updateAllEntitiesSub2Var0;
    if (n != 0 && !pcFieldOffTick()) { /* Enhanced: the input lock's 4 frames are native frames (xg-hbc.14) */
        g_updateAllEntitiesSub2Var0 = --n;
    }
    return n;
}

extern s32 load2dAnimVar;
extern s32 isBackBufferRamCopyEnabled;
extern s32 asyncLoadingVar1;

s32 isFieldTransitionPermittedByLoading(void) {
    if (loadCompleted != 0) {
        return -1;
    }
    if (isCDBusy() == 0 && fieldMusicLoadPending == 0 && load2dAnimVar == 0 && isBackBufferRamCopyEnabled == 0 && asyncLoadingVar1 == 0xFF) {
        return 0;
    }
    return -1;
}

void brightenFieldCluts(void) {
    RECT rect;
    u32* buf;
    s32 i;

    if (updateEntityEventCode4Var0 != 0) {
        fieldRenderContext[0].m0_drawEnv.dtd = 0;
        fieldRenderContext[1].m0_drawEnv.dtd = 0;
        buf = allocateHeap(PC_HEAP_BYTES(0x4000), 0);
        rect.x = 0;
        rect.y = 0x1E0;
        rect.w = 0x100;
        rect.h = 0x20;
        StoreImage(&rect, buf);
        DrawSync(0);
        for (i = 0; i < 0x1000; i++) {
            if (buf[i] & 0xFFFF) {
                buf[i] |= 0xC63;
            }
            if (buf[i] & 0xFFFF0000) {
                buf[i] |= 0x0C630000;
            }
        }
        LoadImage(&rect, buf);
        DrawSync(0);
        freeHeap(buf);
    }
}

void bootField(void) {
    RECT rect;
    s32 volume;
    s32 i;
    s32 step;

    initFontSystem();
    saveBackBufferRamCopy();
    setCurrentDirectory(4, 0);
    loadInitialField();
    flushFrame();
    if (fieldBootedOnce == 0) {
        convert24BitScreenTo15Bit(0);
    }
    initFieldDrawEnvs();
    if (fieldBootedOnce == 0) {
        rect.x = 0;
        rect.y = 0x100;
        rect.w = 0x140;
        rect.h = 0xE0;
        MoveImage(&rect, 0, 0);
    }
    g_frameOddOrEven = 1;
    copyScreenToTransitionStrips();
    copyScreenToVram(0, 0x100);
    DrawSync(0);
    clearFieldOrderingTable();
    flushFrame();
    if (menuReturnState0 == 1 || battleEventReturnMode == 1) {
        captureScreenForTransition(0, 0);
    } else {
        captureScreenForTransition(1, 1);
    }
    fieldBootedOnce = 1;
    waitReadCompletion(0);
    setCurrentDirectory(4, 0);
    pcSlideEdgeLoader(PC_SLIDE_LOADER_INIT);
    initFieldData();
    pcSlideEdgeLoader(PC_SLIDE_LOADER_OFF);
    loadFieldGraphics();
    fieldTransitionInProgress = 1;
    if (NumMechas != 0) {
        setMechaBytecodeToggle(1);
    }
    if (menuReturnState0 == 1 || battleEventReturnMode == 1) {
        step = 0;
    } else {
        step = 0x20;
    }
    volume = 0x800000;
    if (fieldGraphicsUploaded == 1) {
    wait:
        clearFieldOrderingTable();
        renderScreenTransitionPoly();
        renderScreenTransition();
        if (battleEventReturnMode != 1) {
            if (transitionStripScale < 0x22C0) {
                transitionStripScale += step;
            }
        } else {
            setFieldTransitionPolyColor(volume >> 16);
            volume -= 0x40000;
            if (volume < 0) {
                volume = 0;
            }
        }
        if (isCDBusy() != 0) {
            goto wait;
        }
        DrawSync(0);
        freeHeap(fieldGraphicsAllocation);
        fieldGraphicsUploaded = 0;
        brightenFieldCluts();
    }
    if (battleEventReturnMode == 1) {
        do {
            clearFieldOrderingTable();
            renderScreenTransitionPoly();
            renderScreenTransition();
            setFieldTransitionPolyColor(volume >> 16);
            volume -= 0x40000;
        } while (volume >= 0);
    }
    if (menuReturnState0 == 1) {
        rect.y = 0;
        rect.x = 0;
        rect.w = 0x140;
        rect.h = 0xE0;
        MoveImage(&rect, 0x200, 0);
    }
    if (worldmapSoundFilesPendingCount != 0) {
        startMusicInstanceSub0(pMusic);
        freeSoundInstance(pMusic);
        unloadWds(worldmapWdsResult);
        worldmapSoundFilesPendingCount = 0;
    }
    runFieldResumeScript();
    D_8004F310 = 0;
    fieldScriptEntityAlreadyInitialized = 0;
    flushFrame();
    resetInputs();
    fieldMusicLoadPending = 0;
    if (menuReturnState0 == 1) {
        currentlyPlayingMusic = 0xE;
        clearMusic2();
    }
    if (currentlyLoadedMusic != currentlyPlayingMusic) {
        resetBattleAudioState();
        fieldMusicLoadPending = -1;
        if (savedMusicInstance != 0) {
            updateMusicState2Var2 = 1;
        }
        pcSlideEdgeLoader(PC_SLIDE_LOADER_MUSIC);
        playMusic(currentlyPlayingMusic, 1);
        pcSlideEdgeLoader(PC_SLIDE_LOADER_OFF);
    } else {
        clearMusic2();
    }
    syncKernelAndFieldStates();
    if (menuReturnState0 != 1) {
        if (battleEventReturnMode != 1) {
            setupRGBFaderSlot0_fadeIn(0x20);
            volume = 0x800000;
            for (i = 0; i < 0x20; i++) {
                fieldPerFrameReset();
                renderScreenTransitionPoly();
                updateAndRenderField();
                updateMusicState();
                if (menuReturnState0 != 1) {
                    setFieldTransitionPolyColor(volume >> 16);
                    if (pcFieldNativeFrameGoesOn()) { i--; continue; } /* Enhanced: the frames of a native frame repeat it, the progression counts native frames at their end (xg-hbc.14, .19.3) */
                    volume -= 0x40000;
                    if (volume < 0) {
                        volume = 0;
                    }
                    if (transitionStripScale < 0x22C0) {
                        transitionStripScale += step;
                    }
                }
            }
        } else {
            setupRGBFaderSlot0_fadeIn(0x20);
        }
    } else {
        for (i = 0; i < 8; i++) {
            fieldPerFrameReset();
            renderScreenTransitionPoly();
            updateAndRenderField();
            updateMusicState();
            if (pcFieldNativeFrameGoesOn()) { i--; continue; } /* Enhanced: native frames count, at their end (xg-hbc.14, .19.3) */
        }
    }
    if (NumMechas != 0) {
        setMechaBytecodeToggle(0);
    }
    restoreBackBufferRamCopy();
    mergeFreeHeapBlocks();
    deinitDebugFont();
    initFontSystem();
    fieldTransitionInProgress = 0;
}

static s32 checkForRandomEncounterTick(void) {
    s32 i;
    s32 total;
    s32 sum;
    s32 found;
    u8* weights;
    s32 sums[16];

    if (playMusicAuthorized == 0 || fieldExecuteVar3 == 0 || fieldChangePrevented == 0 || fieldMusicLoadPending == -1 || op9DVar1.m114_encounterTimer == 0 ||
        fieldRandomBattleVar == -1 || loadCompleted == 1 || fieldTransitionCompleted == 0) {
        IMPLICIT_RETURN(0);
    }
    if (--op9DVar1.m110_encounterDataCountdown == 0) {
        setInitialEncounterData();
    }
    for (i = 0; i < op9DVar1.m118_encounterCount; i++) {
        if (op9DVar1.m11C_encounterTriggerTimes[i] != 0xFFFF) {
            op9DVar1.m11C_encounterTriggerTimes[i]--;
        }
    }
    for (i = 0; i < op9DVar1.m118_encounterCount; i++) {
        if (op9DVar1.m11C_encounterTriggerTimes[i] == 0) {
            op9DVar1.m11C_encounterTriggerTimes[i] = 0xFFFF;
            goto found_entry;
        }
    }
    IMPLICIT_RETURN(0);
found_entry:
    total = 0;
    weights = encounterProbabilityWeight;
    for (i = 0; i < 16; i++) {
        total += weights[i];
    }
    sum = 0;
    for (i = 0; i < 16; i++) {
        sums[i] = sum;
        sum += weights[i];
    }
    total = (rand() * (total + 1)) >> 15;
    found = 0;
    for (i = 15; i >= 0; i--) {
        if (weights[i] != 0 && sums[i] < total) {
            found++;
            break;
        }
    }
    if (found) {
        op9DVar1.m10C_battleMusicId = op9DVar1.mEC_encounterMusicIds[i];
        requestedBattleConfig = i;
        pcGameplayRandomBattle(i); /* a random battle, for the reward settings */
        battleInitVar1 = 0;
        if (mechaOverlayLoadingMode == 0) {
            loadGameModeOverlay(2);
        }
        playMusicAuthorized = 0;
        bBattleSequenceInitialized = 1;
        if (fieldDebugDisable == 0) {
            func_80281204(i);
        }
    }
    FALLOFF_RETURN(0); /* result unused */
}

s32 checkForRandomEncounter(void) {
    static int fraction;
    int i;
    int ticks = pcFieldOffTick() ? 0 : pcGameplayEncounterTicks(&fraction); /* Enhanced: native cadence */
    for (i = 0; i < ticks; i++) {
        checkForRandomEncounterTick();
    }
    return 0;
}

void fieldChangeGameMode(s32 mode) {
    battleEventReturnMode = 0;
    switch (mode) {
    case 0:
        saveStateToKernel();
        currentlyPlayingMusic = fieldMusicBeforeBattle;
        pKernelGameState->m2322_previouslyPlayingMusic = fieldMusicBeforeBattle;
        pKernelGameState->m2320_worldmapMode = pKernelGameState->m1930_fieldVarsBackup[1];
        if (mechaOverlayLoadingMode != 0) {
            return;
        }
        setGameMode(2);
        break;
    case 1:
        if (playWorldmapMusic == 1) {
            resetBattleAudioState();
            startReloadingResidentWds();
            waitReadCompletion(0);
            finishReloadingResidentWds();
            resetBattleAudioState();
        }
        if (mechaOverlayLoadingMode != 0) {
            return;
        }
        setGameMode(3);
        break;
    case 2:
        pKernelGameState->m2322_previouslyPlayingMusic = currentlyPlayingMusic;
        pKernelGameState->m2320_worldmapMode = pKernelGameState->m1930_fieldVarsBackup[1];
        if (mechaOverlayLoadingMode != 0) {
            return;
        }
        setGameMode(4);
        fieldScriptEntityAlreadyInitialized++;
        break;
    case 3:
        D_8004F310 = 0;
        fieldScriptEntityAlreadyInitialized = 0;
        if (mechaOverlayLoadingMode != 0) {
            return;
        }
        if (newBootMode & 0x80) {
            initGameState();
        }
        setGameMode(newBootMode & 0x7F);
        break;
    }
    bootGame(0);
}

void signalFieldMechaAnimEvent(void) {
}

/* Noah: swap the field render contexts (flip g_frameOddOrEven) */
void swapFieldRenderContext(void) {
    g_frameOddOrEven = (g_frameOddOrEven + 1) % 2;
    pCurrentFieldRenderingContext = &fieldRenderContext[g_frameOddOrEven];
    PutDispEnv(&pCurrentFieldRenderingContext->mB8_displayEnv);
    PutDrawEnv(&pCurrentFieldRenderingContext->m0_drawEnv);
}

/* Render one frame with a full-screen tile of grey level `level` * 4 over
 * the previous frame (copied to the back buffer). */
void renderFieldMenuFadeFrame(s32 level) {
    clearFieldOrderingTable();
    fieldMenuFadeTiles[g_frameOddOrEven].r0 = fieldMenuFadeTiles[g_frameOddOrEven].g0 = fieldMenuFadeTiles[g_frameOddOrEven].b0 = level * 4;
    addPrim(pCurrentFieldRenderingContext->mCC_OT, &fieldMenuFadeTiles[g_frameOddOrEven]);
    addPrim(pCurrentFieldRenderingContext->mCC_OT, &fieldMenuFadeDrawModes[g_frameOddOrEven]);
    flushFrame();
    MoveImage(&fieldMenuMoveRect, 0, g_frameOddOrEven << 8);
    PutDispEnv(&pCurrentFieldRenderingContext->mB8_displayEnv);
    PutDrawEnv(&pCurrentFieldRenderingContext->m0_drawEnv);
    DrawOTag(&pCurrentFieldRenderingContext->mCC_OT[1]);
}

void updateGearSwitchDisabledFlag(void) {
    if (gearSwitchingEnabled != 0 && !(fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m14_currentTriangleFlag & 0xC0)) {
        gearSwitchDisabled = 0;
    } else {
        gearSwitchDisabled = 1;
    }
    if (op9DVar1.m1C8_gearSwitchOverride != 0xFF) {
        gearSwitchDisabled = op9DVar1.m1C8_gearSwitchOverride;
    }
}

void moveImageSync(s16 w, s16 h, s16 x, s16 y, s32 dstX, s32 dstY) {
    RECT rect;

    rect.w = w;
    rect.h = h;
    rect.x = x;
    rect.y = y;
    MoveImage(&rect, dstX, dstY);
    DrawSync(0);
}

void ClearCacheAfterOverlayLoad(void) {
    flushFrame();
    EnterCriticalSection(); /* EnterCriticalSection */
    FlushCache();
    ExitCriticalSection(); /* ExitCriticalSection */
}

/* The overlay in each menu file (dir 0x10, file menu + 5; disc.yaml): files
 * 5, 7 and 0xB are copies of the main menu. */
static const char* const menuOverlayModules[7] = {
    "menu", "menu_party", "menu", "menu_name_entry", "menu_shop", "menu_gear_shop", "menu",
};

/* open the field menu g_menuIdToOpen: free the field VRAM/heap, run enterMenu, reload */
void openFieldMenu(void) {
    RECT rect;
    LoadingBatchCommand load[5];
    uptr heapTop;
    void* walkMeshCopy;
    void* menuBuffer;
    void* vramSave0;
    void* vramSave1;
    void* buf;
    s32 i;
    void* mechaState = NULL;
    u32 sfxLoops = 0;

    walkMeshCopy = NULL;
    if (g_menuIdToOpen == 0x80 && g_menuDisabled != 0) {
        return;
    }
    setlen(&fieldMenuFadeTiles[0], 3);
    setcode(&fieldMenuFadeTiles[0], 0x60);
    SetSemiTrans(&fieldMenuFadeTiles[0], 1);
    fieldMenuFadeTiles[0].w = 0x140;
    fieldMenuFadeTiles[0].b0 = 0;
    fieldMenuFadeTiles[0].g0 = 0;
    fieldMenuFadeTiles[0].r0 = 0;
    fieldMenuFadeTiles[0].y0 = 0;
    fieldMenuFadeTiles[0].x0 = 0;
    fieldMenuFadeTiles[0].h = 0xE0;
    fieldMenuFadeTiles[1] = fieldMenuFadeTiles[0];
    SetDrawMode(&fieldMenuFadeDrawModes[0], 0, 0, GetTPage(0, 2, 0, 0), NULL);
    SetDrawMode(&fieldMenuFadeDrawModes[1], 0, 0, GetTPage(0, 2, 0, 0), NULL);
    freePartyCharacterBuffers();
    if (NumMechas != 0) {
        setCurrentDirectory(4, 0);
        /* The menus load over field_mecha on the PS1, so its code and data
         * are kept aside; on the PC nothing loads over it: token blocks
         * keep the heap calls. */
        walkMeshCopy = allocateHeap(PC_HEAP_SIZED(4, getFileSizeAligned(0x6B9)), 0);
        freeHeap(mechaOverlayBuffer);
    }
    heapTop = fieldHeapTopMarker;
    setCurrentDirectory(0x10, 0);
    if (mechaOverlayLoadingMode == 1) {
        menuBuffer = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned((g_menuIdToOpen + 5) & 0x7F)), 1);
    } else {
        /* The menu overlays are linked in, so no padding up to their link
         * address: a token block keeps the frees balanced. */
        menuBuffer = allocateHeap(PC_HEAP_SIZED(4, pcHeapReservation((void*)heapTop, 0x801C5000u)), 1);
    }
    load[2].m0_fileIndex = 0;
    load[2].m4_loadPtr = NULL;
    load[3].m0_fileIndex = 0;
    load[3].m4_loadPtr = NULL;
    load[0].m0_fileIndex = 1;
    load[0].m4_loadPtr = menuSharedResources = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(1)), 1);
    /* Only the menu data file is read: the menu overlay (and, for the gear
     * shop, field_mecha) is linked in and starts as a fresh load. */
    load[1].m0_fileIndex = 0;
    load[1].m4_loadPtr = NULL;
    if (portOverlayCd()) { /* XG_OVERLAY_CD: the PS1's batch, the overlay files discarded */
        load[1].m0_fileIndex = (g_menuIdToOpen & 0x7F) + 5;
        load[1].m4_loadPtr = mechaOverlayLoadingMode == 1
                                 ? menuBuffer
                                 : portOverlayScratch(0, getFileSizeAligned(load[1].m0_fileIndex));
        if ((g_menuIdToOpen & 0x7F) == 5 && mechaOverlayLoadingMode == 0) {
            load[2].m0_fileIndex = 0xC;
            load[2].m4_loadPtr = portOverlayScratch(1, getFileSizeAligned(0xC));
        }
    }
    portResetModule(menuOverlayModules[g_menuIdToOpen & 0x7F]);
    if ((g_menuIdToOpen & 0x7F) == 5 && mechaOverlayLoadingMode == 0) {
        /* The gear shop gets a fresh field_mecha (the PS1 loads file 0xC
         * over it); the field's gears get theirs back after. */
        if (NumMechas != 0) {
            mechaState = portSaveModule("field_mecha");
        }
        portResetModule("field_mecha");
    }
    waitReadCompletion(0);
    batchStartLoadingFiles(load, 0, 0);
    setCurrentDirectory(4, 0);
    rect.w = 0x40;
    rect.h = 0x20;
    for (i = 0; i < 6; i++) {
        rect.x = menuVramSaveSources[i * 2];
        rect.y = menuVramSaveSources[i * 2 + 1];
        MoveImage(&rect, menuVramSaveDestinations[i * 2], menuVramSaveDestinations[i * 2 + 1]);
        DrawSync(0);
    }
    moveImageSync(0x40, 0x100, 0x3C0, 0x100, 0x300, 0);
    moveImageSync(0x40, 0x100, 0x2C0, 0x100, 0x280, 0);
    fieldMenuMoveRect.x = 0x2C0;
    fieldMenuMoveRect.y = 0x100;
    fieldMenuMoveRect.w = 0x140;
    fieldMenuMoveRect.h = 0xE0;
    copyScreenToTransitionStrips();
    MoveImage(&fieldMenuMoveRect, 0, 0x100);
    DrawSync(0);
    for (i = 0; i < 0x20; i++) {
        renderFieldMenuFadeFrame(i);
    }
    flushFrame();
    fieldMenuMoveRect.x = 0;
    fieldMenuMoveRect.y = 0;
    swapFieldRenderContext();
    deinitDebugFont();
    MoveImage(&fieldMenuMoveRect, 0, 0xE0);
    flushFrame();
    waitReadCompletion(0);
    menuReturnState0 = 0;
    useDebugMenuList = 0;
    menuToEnter = g_menuIdToOpen & 0x7F;
    for (i = 0; i < 3; i++) {
        partyGearSwitchFlags[i] = pKernelGameState->m22B1_isOnGear[i];
    }
    updateGearSwitchDisabledFlag();
    gearShopOrderingTables[0] = (OTag*)fieldRenderContext[0].mCC_OT;
    gearShopOrderingTables[1] = (OTag*)fieldRenderContext[1].mCC_OT;
    ClearCacheAfterOverlayLoad();
    {
        /* The menu's own kind for the wide picture (include/pc_wide.h). */
        s32 wideKind = pcWideSetMode(PC_WIDE_MENU);
        sfxLoops = pcFieldSfxLoopingMask(); /* original-game bug fixes: sfxkeep.c */
        pcFieldWideMenu(1); /* the field's opt-out is not the menu's */
        enterMenu();
        pcFieldWideMenu(0);
        pcWideSetMode(wideKind);
    }
    ClearCacheAfterOverlayLoad();
    gDepthDivider = 2;
    if (menuReturnState0 == 0 && (g_menuIdToOpen & 0x7F) == 2) {
        kernelAndFieldStatesSynced = 1;
        setVar(0x46, 0);
        setVar(4, 4);
        fieldMapNumber = 4;
        pKernelGameState->m2320_worldmapMode = 0;
        pKernelGameState->m1930_fieldVarsBackup[1] = 0;
        pKernelGameState->m231A_fieldID = 4;
    }
    if (menuReturnState0 == 2) {
        kernelAndFieldStatesSynced = 1;
        setVar(0x46, 2);
        setVar(4, pKernelGameState->m231A_fieldID & 0x3FFF);
        if ((pKernelGameState->m231A_fieldID & 0x3FFF) < 0x400) {
            pKernelGameState->m2320_worldmapMode = pKernelGameState->m1930_fieldVarsBackup[42];
        }
    }
    flushFrame();
    fieldMenuMoveRect.x = 0;
    fieldMenuMoveRect.y = 0xE0;
    fieldMenuMoveRect.w = 0x140;
    fieldMenuMoveRect.h = 0xE0;
    MoveImage(&fieldMenuMoveRect, 0x140, 0);
    flushFrame();
    fieldMenuMoveRect.x = 0x140;
    fieldMenuMoveRect.y = 0;
    MoveImage(&fieldMenuMoveRect, 0, 0);
    MoveImage(&fieldMenuMoveRect, 0, 0x100);
    flushFrame();
    PutDispEnv(&pCurrentFieldRenderingContext->mB8_displayEnv);
    PutDrawEnv(&pCurrentFieldRenderingContext->m0_drawEnv);
    fieldMenuMoveRect.x = 0x2C0;
    fieldMenuMoveRect.y = 0x100;
    renderFieldMenuFadeFrame(0x1F);
    renderFieldMenuFadeFrame(0x1F);
    rect.w = 0x40;
    rect.h = 0x100;
    rect.x = 0x300;
    rect.y = 0;
    vramSave0 = allocateHeap(PC_HEAP_BYTES(0x8000), 1);
    StoreImage(&rect, vramSave0);
    DrawSync(0);
    rect.w = 0x40;
    rect.h = 0x100;
    rect.x = 0x280;
    rect.y = 0;
    vramSave1 = allocateHeap(PC_HEAP_BYTES(0x8000), 1);
    StoreImage(&rect, vramSave1);
    DrawSync(0);
    for (i = 0; i < 6; i++) {
        rect.w = 0x40;
        rect.h = 0x20;
        rect.x = menuVramSaveDestinations[i * 2];
        rect.y = menuVramSaveDestinations[i * 2 + 1];
        MoveImage(&rect, menuVramSaveSources[i * 2], menuVramSaveSources[i * 2 + 1]);
        DrawSync(0);
    }
    setCurrentDirectory(4, 0);
    resetMemoryAllocStats(8, 0);
    fieldGraphicsUploaded = 0;
    loadFieldGraphics();
    if (OPX_86Param != 0) {
        for (i = 0x20; i < 0x3F; i++) {
            renderFieldMenuFadeFrame(i);
        }
        D_800ADB50 = 1;
    } else {
        for (i = 0x1F; i >= 0; i--) {
            renderFieldMenuFadeFrame(i);
        }
        renderFieldMenuFadeFrame(0);
        D_800ADB50 = 0;
    }
    freeFieldGraphics();
    flushFrame();
    rect.w = 0x40;
    rect.h = 0x100;
    rect.x = 0x2C0;
    rect.y = 0x100;
    LoadImage(&rect, vramSave1);
    DrawSync(0);
    rect.x = 0x3C0;
    LoadImage(&rect, vramSave0);
    DrawSync(0);
    freeHeap(vramSave1);
    freeHeap(vramSave0);
    freeHeap(menuBuffer);
    setCurrentDirectory(4, 0);
    if (NumMechas != 0) {
        if (mechaOverlayLoadingMode == 0) {
            mechaOverlayBuffer = allocateHeap(PC_HEAP_SIZED(4, pcHeapReservation((void*)heapTop, 0x801DC000u)), 1);
        } else {
            mechaOverlayBuffer = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(0x6B9)), 1);
        }
        if (mechaState != NULL) {
            portRestoreModule("field_mecha", mechaState);
        }
        freeHeap(walkMeshCopy);
    }
    SetGeomOffset(0xA0, 0x70);
    SetGeomScreen(sceneSCRZ);
    allocatePartyCharacterBuffers();
    if (g_menuIdToOpen == 1) {
        asyncPartyCharacterLoadingTable[0] = asyncPartyCharacterLoadingTable[1] = asyncPartyCharacterLoadingTable[2] = 0xFF;
        fieldRequestedGears = 0;
        typeOfPlayableCharacterLoaded = 0;
        updatePartyFileSet();
        processLoadedPartyFiles();
        flushFrame();
        fieldChangePrevented = 0;
        renderCharsDisabled = 1;
    } else {
        for (i = 0; i < 3; i++) {
            if (asyncPartyCharacterLoadingTable[i] != 0xFF) {
                buf = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(asyncPartyCharacterLoadingTable[i] + 5)), 1);
                readFile(asyncPartyCharacterLoadingTable[i] + 5, buf, 0, 0x80);
                waitReadCompletion(0);
                decompress(buf, partyCharacterBuffers[i]);
                freeHeap(buf);
            }
        }
        runMenuReturnScript();
        flushFrame();
    }
    /* The menu's sounds cut the field's looping ones on their channel;
     * start them again unless the menu leaves the field (a loaded game, the
     * load menu's new game, the party menu's field change). */
    if (sfxLoops != 0 && menuReturnState0 != 2 && (g_menuIdToOpen & 0x7F) != 1 && (g_menuIdToOpen & 0x7F) != 2) {
        pcFieldSfxRestore(sfxLoops);
    }
    g_menuIdToOpen = 0xFF;
    initFontSystem();
    g_menuOpenCount = 0;
}

/* set the four UVs of a POLY_FT4, each clamped to 0..255 */
void setPolyFT4UVsClamped(POLY_FT4* p, s16 u0, s16 v0, s16 u1, s16 v1, s16 u2, s16 v2, s16 u3, s16 v3) {
    if (u0 < 0) {
        u0 = 0;
    }
    if (u1 < 0) {
        u1 = 0;
    }
    if (u2 < 0) {
        u2 = 0;
    }
    if (u3 < 0) {
        u3 = 0;
    }
    if (v0 < 0) {
        v0 = 0;
    }
    if (v1 < 0) {
        v1 = 0;
    }
    if (v2 < 0) {
        v2 = 0;
    }
    if (v3 < 0) {
        v3 = 0;
    }
    if (u0 >= 0x100) {
        u0 = 0xFF;
    }
    if (u1 >= 0x100) {
        u1 = 0xFF;
    }
    if (u2 >= 0x100) {
        u2 = 0xFF;
    }
    if (u3 >= 0x100) {
        u3 = 0xFF;
    }
    if (v0 >= 0x100) {
        v0 = 0xFF;
    }
    if (v1 >= 0x100) {
        v1 = 0xFF;
    }
    if (v2 >= 0x100) {
        v2 = 0xFF;
    }
    if (v3 >= 0x100) {
        v3 = 0xFF;
    }
    p->u0 = u0;
    p->v0 = v0;
    p->u1 = u1;
    p->v1 = v1;
    p->u2 = u2;
    p->v2 = v2;
    p->u3 = u3;
    p->v3 = v3;
}

extern s16 compassShadowQuadCorners[4][8];            /* quad corners (x, z) x4, per quad */
extern u8 compassShadowQuadUVs[4][8];             /* quad UVs (u, v - 0xC0) x4, per quad */
/* set up the four 16x16 shadow quads (fieldCompassVar2[0x15..0x18]) from the vertex/UV tables */
void initCompassShadowPoly(void) {
    s32 i;

    for (i = 0; i < 4; i++) {
        FieldQuad* quad = &fieldCompassVar2[0x15 + i];
        POLY_FT4* p = &fieldCompassVar2[0x15 + i].m20_poly[0];
        POLY_FT4* q = &fieldCompassVar2[0x15 + i].m20_poly[1];

        SetPolyFT4(p);
        quad->m0_vertices[0].vx = ((s16*)compassShadowQuadCorners)[i * 8 ];
        quad->m0_vertices[0].vy = 0;
        quad->m0_vertices[0].vz = ((s16*)compassShadowQuadCorners)[i * 8 + 1];
        quad->m0_vertices[1].vx = ((s16*)compassShadowQuadCorners)[i * 8 + 2];
        quad->m0_vertices[1].vy = 0;
        quad->m0_vertices[1].vz = ((s16*)compassShadowQuadCorners)[i * 8 + 3];
        quad->m0_vertices[2].vx = ((s16*)compassShadowQuadCorners)[i * 8 + 4];
        quad->m0_vertices[2].vy = 0;
        quad->m0_vertices[2].vz = ((s16*)compassShadowQuadCorners)[i * 8 + 5];
        quad->m0_vertices[3].vx = ((s16*)compassShadowQuadCorners)[i * 8 + 6];
        quad->m0_vertices[3].vy = 0;
        quad->m0_vertices[3].vz = ((s16*)compassShadowQuadCorners)[i * 8 + 7];
        p->r0 = 0x80;
        p->g0 = 0x80;
        p->b0 = 0x80;
        setPolyFT4UVsClamped(p, ((u8*)compassShadowQuadUVs)[i * 8 ], ((u8*)compassShadowQuadUVs)[i * 8 + 1] + 0xC0, ((u8*)compassShadowQuadUVs)[i * 8 + 2], ((u8*)compassShadowQuadUVs)[i * 8 + 3] + 0xC0,
                      ((u8*)compassShadowQuadUVs)[i * 8 + 4], ((u8*)compassShadowQuadUVs)[i * 8 + 5] + 0xC0, ((u8*)compassShadowQuadUVs)[i * 8 + 6], ((u8*)compassShadowQuadUVs)[i * 8 + 7] + 0xC0);
        SetSemiTrans(p, 1);
        p->tpage = GetTPage(0, 2, 0x280, 0x1C0);
        p->clut = GetClut(0x100, 0xF2);
        *q = *p;
    }
}

/* set up a quad from the shape tables: corners x from compassQuadCornersX[xi],
 * z from compassQuadCornersZ[zi], UVs from compassQuadCornersU[xi] / compassQuadCornersV[zi],
 * texture page and CLUT from compassQuadTextures[ti] */
void initCompassPoly(FieldQuad* quad, s32 xi, s32 zi, s32 ti) {
    POLY_FT4* p = &quad->m20_poly[0];
    POLY_FT4* q = &quad->m20_poly[1];
    u8 unused[0x88]; /* unused; reserves the 0x88-byte hole in the retail frame */

    SetPolyFT4(p);
    quad->m0_vertices[0].vx = compassQuadCornersX[xi][0];
    quad->m0_vertices[0].vy = 0;
    quad->m0_vertices[0].vz = compassQuadCornersZ[zi][0];
    quad->m0_vertices[1].vx = compassQuadCornersX[xi][1];
    quad->m0_vertices[1].vy = 0;
    quad->m0_vertices[1].vz = compassQuadCornersZ[zi][1];
    quad->m0_vertices[2].vx = compassQuadCornersX[xi][2];
    quad->m0_vertices[2].vy = 0;
    quad->m0_vertices[2].vz = compassQuadCornersZ[zi][2];
    quad->m0_vertices[3].vx = compassQuadCornersX[xi][3];
    quad->m0_vertices[3].vy = 0;
    quad->m0_vertices[3].vz = compassQuadCornersZ[zi][3];
    p->r0 = 0x80;
    p->g0 = 0x80;
    p->b0 = 0x80;
    p->tpage = GetTPage(compassQuadTextures[ti][0], compassQuadTextures[ti][1], compassQuadTextures[ti][2], compassQuadTextures[ti][3]);
    p->clut = GetClut(compassQuadTextures[ti][4], compassQuadTextures[ti][5]);
    setPolyFT4UVsClamped(p, compassQuadCornersU[xi][0], compassQuadCornersV[zi][0], compassQuadCornersU[xi][1], compassQuadCornersV[zi][1],
                  compassQuadCornersU[xi][2], compassQuadCornersV[zi][2], compassQuadCornersU[xi][3], compassQuadCornersV[zi][3]);
    *q = *p;
}

/* set up a 48x48 semi-transparent textured quad (both buffers) */
void initCharacterShadowPoly(FieldQuad* quad) {
    POLY_FT4* p = &quad->m20_poly[0];
    POLY_FT4* q = &quad->m20_poly[1];

    SetPolyFT4(p);
    quad->m0_vertices[3].vx = -0x18;
    quad->m0_vertices[3].vy = 0;
    quad->m0_vertices[3].vz = -0x18;
    quad->m0_vertices[2].vx = 0x18;
    quad->m0_vertices[2].vy = 0;
    quad->m0_vertices[2].vz = -0x18;
    quad->m0_vertices[1].vx = -0x18;
    quad->m0_vertices[1].vy = 0;
    quad->m0_vertices[1].vz = 0x18;
    quad->m0_vertices[0].vx = 0x18;
    quad->m0_vertices[0].vy = 0;
    quad->m0_vertices[0].vz = 0x18;
    quad->m20_poly[0].r0 = 0x80;
    quad->m20_poly[0].g0 = 0x80;
    quad->m20_poly[0].b0 = 0x80;
    quad->m20_poly[0].tpage = GetTPage(0, 2, 0x280, 0x1E0);
    quad->m20_poly[0].clut = GetClut(0x100, 0xF3);
    SetSemiTrans(p, 1);
    quad->m20_poly[0].u0 = 0;
    quad->m20_poly[0].v0 = 0xE0;
    quad->m20_poly[0].u1 = 0xF;
    quad->m20_poly[0].v1 = 0xE0;
    quad->m20_poly[0].u2 = 0;
    quad->m20_poly[0].v2 = 0xEF;
    quad->m20_poly[0].u3 = 0xF;
    quad->m20_poly[0].v3 = 0xEF;
    *q = *p;
}

/* ref decomp: FieldRenderQuad */
void drawCompassArrowSegment(OTag* ot, FieldQuad* quad, MATRIX* m, s32 buf) {
    POLY_FT4* p = &quad->m20_poly[buf];
    s32 depth;
    s32 flag;

    PushMatrix();
    SetRotMatrix(m);
    SetTransMatrix(m);
    RotAverage4(&quad->m0_vertices[0], &quad->m0_vertices[1], &quad->m0_vertices[2], &quad->m0_vertices[3],
                &p->x0, &p->x1, &p->x2, &p->x3, &depth, &flag);
    addPrim(&ot[1], p);
    PopMatrix();
}

/* like drawCompassArrowSegment, then turn the quad into a 16x10 billboard standing on
 * the midpoint of its projected back edge */
void drawCompassLetters(OTag* ot, FieldQuad* quad, MATRIX* m, s32 buf) {
    POLY_FT4* p = &quad->m20_poly[buf];
    s32 depth;
    s32 flag;
    s32 x;
    s32 y;
    s32 right;

    PushMatrix();
    SetRotMatrix(m);
    SetTransMatrix(m);
    RotAverage4(&quad->m0_vertices[0], &quad->m0_vertices[1], &quad->m0_vertices[2], &quad->m0_vertices[3],
                &p->x0, &p->x1, &p->x2, &p->x3, &depth, &flag);
    x = (p->x3 + p->x2) / 2;
    y = p->y3;
    p->y1 = p->y0 = y - 10;
    p->y2 = y;
    p->y3 = y;
    right = x + 8;
    x = x - 8;
    p->x0 = x;
    p->x1 = right;
    p->x2 = x;
    p->x3 = right;
    addPrim(&ot[1], p);
    PopMatrix();
}

void setMousePadBuffers(s8* buf0, s8* buf1) {
    mousePadBuffers[0] = buf0;
    mousePadBuffers[1] = buf1;
}

void setMouseCursorBounds(s32 xMin, s32 xMax, s32 yMin, s32 yMax) {
    mouseCursorMinX = xMin * mouseSpeedX;
    mouseCursorMaxX = xMax * mouseSpeedX;
    mouseCursorMinY = yMin * mouseSpeedY;
    mouseCursorMaxY = yMax * mouseSpeedY;
}

void setMouseSpeedScale(u16 speedX, u16 speedY) {
    mouseSpeedX = speedX;
    mouseSpeedY = speedY;
}

void setMouseCursorPosition(s32 port, s32 x, s32 y) {
    mouseCursorX[port] = x * mouseSpeedX;
    mouseCursorY[port] = y * mouseSpeedY;
}

s32 getMouseState(s32 port, s32* out) {
    updateMouseCursor(port);
    out[0] = mouseCursorX[port] / mouseSpeedX;
    out[1] = mouseCursorY[port] / mouseSpeedY;
    out[2] = -0x100;
    out[3] = mousePadBuffers[port][4];
    out[4] = mousePadBuffers[port][5];
    if (mousePadBuffers[port][0] == 0 && mousePadBuffers[port][1] == 0x12) {
        out[2] = ~mousePadBuffers[port][3] & 0xC;
    }
    FALLOFF_RETURN(0); /* result unused */
}

void updateMouseCursor(s32 port) {
    if (mousePadBuffers[port][0] == 0 && mousePadBuffers[port][1] == 0x12) {
        mouseCursorX[port] += mousePadBuffers[port][4];
        mouseCursorY[port] += mousePadBuffers[port][5];
        if (mouseCursorX[port] > mouseCursorMaxX) {
            mouseCursorX[port] = mouseCursorMaxX;
        } else if (mouseCursorX[port] < mouseCursorMinX) {
            mouseCursorX[port] = mouseCursorMinX;
        }
        if (mouseCursorY[port] > mouseCursorMaxY) {
            mouseCursorY[port] = mouseCursorMaxY;
        } else if (mouseCursorY[port] < mouseCursorMinY) {
            mouseCursorY[port] = mouseCursorMinY;
        }
    }
}

void getTriangleNormalAndAdjustY(SVECTOR* vec0, SVECTOR* vec1, SVECTOR* vec2, SVECTOR* outputPosition, VECTOR* pOutputTriangleNormal) {
    VECTOR normalizedVec0To1;
    VECTOR normalizedVec0To2;
    VECTOR vec;

    vec.vx = vec1->vx - vec0->vx;
    vec.vy = vec1->vy - vec0->vy;
    vec.vz = vec1->vz - vec0->vz;
    VectorNormal(&vec, &normalizedVec0To1);
    vec.vx = vec2->vx - vec0->vx;
    vec.vy = vec2->vy - vec0->vy;
    vec.vz = vec2->vz - vec0->vz;
    VectorNormal(&vec, &normalizedVec0To2);
    OuterProduct12(&normalizedVec0To1, &normalizedVec0To2, pOutputTriangleNormal);
    if (pOutputTriangleNormal->vy == 0) {
        outputPosition->vy = 0;
    } else {
        outputPosition->vy = vec0->vy + (-(pOutputTriangleNormal->vx * (outputPosition->vx - vec0->vx)) - pOutputTriangleNormal->vz * (outputPosition->vz - vec0->vz)) / pOutputTriangleNormal->vy;
    }
}

s32 findTriangleInWalkMesh(s32 posX, s32 posZ, s32 walkmeshId, SVECTOR* position, VECTOR* triangleNormal) {
    u8 unused0[0x30]; /* unused locals: keep vec0 at sp+0x48 and the 0xA0 frame */
    SVECTOR vec0;
    SVECTOR vec1;
    SVECTOR vec2;
    SVECTOR resultPosition;
    s32 opz[3];
    u8 unused1[0xC];
    sWalkMeshTriangle* tris;
    SVECTOR* pVertices;
    s32 count;
    s32 refPos;
    s32 pos0;
    s32 pos1;
    s32 pos2;
    s32 i;

    resultPosition.vx = posX;
    resultPosition.vy = 0;
    resultPosition.vz = posZ;
    tris = walkMeshTriangle[walkmeshId];
    count = g_walkMeshNumTrianglePerBlock[walkmeshId];
    pVertices = walkMeshVertices[walkmeshId];
    refPos = (posX << 16) + posZ;
    for (i = 0; i < count; i++) {
        pos0 = (pVertices[tris[i].m0_verticeIndex[0]].vx << 16) + pVertices[tris[i].m0_verticeIndex[0]].vz;
        pos1 = (pVertices[tris[i].m0_verticeIndex[1]].vx << 16) + pVertices[tris[i].m0_verticeIndex[1]].vz;
        pos2 = (pVertices[tris[i].m0_verticeIndex[2]].vx << 16) + pVertices[tris[i].m0_verticeIndex[2]].vz;
        gte_ldsxy3(pos0, pos1, refPos);
        gte_nclip();
        gte_stopz(&opz[0]);
        if (opz[0] >= 0) {
            gte_ldsxy3(pos1, pos2, refPos);
            gte_nclip();
            gte_stopz(&opz[1]);
            if (opz[1] >= 0) {
                gte_ldsxy3(pos2, pos0, refPos);
                gte_nclip();
                gte_stopz(&opz[2]);
                if (opz[2] >= 0) {
                    vec0.vx = pVertices[tris[i].m0_verticeIndex[0]].vx;
                    vec0.vy = pVertices[tris[i].m0_verticeIndex[0]].vy;
                    vec0.vz = pVertices[tris[i].m0_verticeIndex[0]].vz;
                    vec1.vx = pVertices[tris[i].m0_verticeIndex[1]].vx;
                    vec1.vy = pVertices[tris[i].m0_verticeIndex[1]].vy;
                    vec1.vz = pVertices[tris[i].m0_verticeIndex[1]].vz;
                    vec2.vx = pVertices[tris[i].m0_verticeIndex[2]].vx;
                    vec2.vy = pVertices[tris[i].m0_verticeIndex[2]].vy;
                    vec2.vz = pVertices[tris[i].m0_verticeIndex[2]].vz;
                    getTriangleNormalAndAdjustY(&vec0, &vec1, &vec2, &resultPosition, triangleNormal);
                    position->vx = resultPosition.vx;
                    position->vy = resultPosition.vy;
                    position->vz = resultPosition.vz;
                    return i;
                }
            }
        }
    }
    position->vx = 0;
    position->vy = 0;
    position->vz = 0;
    triangleNormal->vx = 0;
    triangleNormal->vy = 0;
    triangleNormal->vz = 0;
    return 0;
}

/* -1 if point is outside triangle (v0, v1, v2) in the XZ plane, else 0 */
s32 isPointInsideTriangleXZ(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* point) {
    VECTOR edge;
    VECTOR toPoint;
    VECTOR cross;

    edge.vx = v1->vx - v0->vx;
    edge.vy = 0;
    edge.vz = v1->vz - v0->vz;
    toPoint.vx = point->vx - v0->vx;
    toPoint.vy = 0;
    toPoint.vz = point->vz - v0->vz;
    OuterProduct0(&edge, &toPoint, &cross);
    if (cross.vy < 0) {
        goto fail;
    }
    edge.vx = v2->vx - v1->vx;
    edge.vy = 0;
    edge.vz = v2->vz - v1->vz;
    toPoint.vx = point->vx - v1->vx;
    toPoint.vy = 0;
    toPoint.vz = point->vz - v1->vz;
    OuterProduct0(&edge, &toPoint, &cross);
    if (cross.vy >= 0) {
        goto ok;
    }
fail:
    return -1;
ok:
    edge.vx = v0->vx - v2->vx;
    edge.vy = 0;
    edge.vz = v0->vz - v2->vz;
    toPoint.vx = point->vx - v2->vx;
    toPoint.vy = 0;
    toPoint.vz = point->vz - v2->vz;
    OuterProduct0(&edge, &toPoint, &cross);
    return cross.vy >> 31;
}

void computeDeltaMove(VECTOR* param_1, s32 param_2, u32 param_3) {
    s32 iVar1;

    param_2 = (param_2 << 4) * entityMoveSpeedFactor >> 0xC;
    iVar1 = getAngleSin(param_3 & 0xFFF);
    param_1->vx = iVar1 * param_2;
    iVar1 = getAngleCos(param_3 & 0xFFF);
    param_1->vy = 0;
    param_1->vz = -(iVar1 * param_2);
}

s32 fp_atan2(VECTOR* v) {
    return -ratan2(v->vz, v->vx) & 0xFFF;
}

s32 updateEntityEventCode3Sub3Sub2(s16 angle, SVECTOR* edge, VECTOR* delta, s32 unused) {
    s16 edgeAngle;
    s16 result;
    s32 rel;
    s32 len;
    VECTOR v;
    VECTOR n;

    rel = (0xC00 - angle) & 0xFFF;
    edgeAngle = -ratan2(edge[1].vz - edge[0].vz, edge[1].vx - edge[0].vx) & 0xFFF;
    result = edgeAngle;
    rel = rel + edgeAngle & 0xFFF;
    if ((u32)(rel - 0x80) >= 0xF01) {
        delta->vx = 0;
        delta->vy = 0;
        delta->vz = 0;
        return edgeAngle;
    }
    if (rel < 0x800) {
        v.vx = edge[0].vx - edge[1].vx;
        v.vy = 0;
        v.vz = edge[0].vz - edge[1].vz;
        result = edgeAngle + 0x800 & 0xFFF;
    } else {
        v.vx = edge[1].vx - edge[0].vx;
        v.vy = 0;
        v.vz = edge[1].vz - edge[0].vz;
    }
    VectorNormal(&v, &n);
    len = length2d(delta->vx >> 12, delta->vz >> 12);
    delta->vx = n.vx * len;
    delta->vy = 0;
    delta->vz = n.vz * len;
    return result;
}

s32 updateEntityEventCode3Sub4(VECTOR* position, sFieldScriptEntity* entity, SVECTOR* edge, s16 angle) {
    VECTOR delta;
    SVECTOR hit;
    VECTOR unused[2]; /* unused; keeps the 0x78 frame */
    s32 a;

    a = angle & 0xFFF;
    delta.vx = position->vx + getAngleSin(a) * 0x40;
    delta.vz = position->vz + getAngleCos(a) * -0x40;
    if (updateEntityEventCode3Sub4Sub1(&delta, &entity->m20_position, entity, edge, &hit, -1) == -1) {
        delta.vx = position->vx;
        delta.vy = position->vy;
        delta.vz = position->vz;
        updateEntityEventCode3Sub3Sub2(angle, edge, &delta, 0);
    } else {
        a = angle - 0x100;
        a &= 0xFFF;
        delta.vx = position->vx + getAngleSin(a) * 0x40;
        delta.vz = position->vz + getAngleCos(a) * -0x40;
        if (updateEntityEventCode3Sub4Sub1(&delta, &entity->m20_position, entity, edge, &hit, -1) == -1) {
            delta.vx = position->vx;
            delta.vy = position->vy;
            delta.vz = position->vz;
            updateEntityEventCode3Sub3Sub2(angle, edge, &delta, 0);
        } else {
            a = angle + 0x100;
            a &= 0xFFF;
            delta.vx = position->vx + getAngleSin(a) * 0x40;
            delta.vz = position->vz + getAngleCos(a) * -0x40;
            if (updateEntityEventCode3Sub4Sub1(&delta, &entity->m20_position, entity, edge, &hit, -1) == -1) {
                delta.vx = position->vx;
                delta.vy = position->vy;
                delta.vz = position->vz;
                updateEntityEventCode3Sub3Sub2(angle, edge, &delta, 0);
            } else {
                delta.vx = position->vx;
                delta.vy = position->vy;
                delta.vz = position->vz;
            }
        }
    }
    if (updateEntityEventCode3Sub4Sub1(&delta, &entity->m20_position, entity, edge, &hit, 0) == -1) {
        return -1;
    }
    if (!(entity->m0_fieldScriptFlags & 0x40000)) {
        if ((hit.vy << 16) < entity->m20_position.vy && updateEntityEventCode3Var1 == 0) {
            return -1;
        }
    } else {
        hit.vy = entity->mEC_elevation;
    }
    delta.vy = (hit.vy << 16) - entity->m20_position.vy;
    position->vx = delta.vx;
    position->vy = delta.vy;
    position->vz = delta.vz;
    entity->m72_elevation = (entity->m20_position.vy + position->vy) >> 16;
    return 0;
}

s32 updateEntityEventCode3Sub3(VECTOR* position, sFieldScriptEntity* entity, SVECTOR* edge, s16 angle) {
    VECTOR delta;
    VECTOR savedDelta;
    SVECTOR hit;
    SVECTOR savedHit;
    VECTOR dir;
    VECTOR normal;
    u32 flags;
    s32 a;
    s32 len;
    s32 ang = angle;

    a = ang - 0x100;
    a &= 0xFFF;
    delta.vx = position->vx + getAngleSin(a) * 0x40;
    delta.vz = position->vz + getAngleCos(a) * -0x40;
    if (updateEntityEventCode3Sub3Sub1(&delta, &entity->m20_position, entity, edge, &hit, -1, &flags) == -1) {
        delta.vx = position->vx;
        delta.vy = position->vy;
        delta.vz = position->vz;
        updateEntityEventCode3Sub3Sub2(ang, edge, &delta, flags);
    } else {
        a = ang + 0x100;
        a &= 0xFFF;
        delta.vx = position->vx + getAngleSin(a) * 0x40;
        delta.vz = position->vz + getAngleCos(a) * -0x40;
        if (updateEntityEventCode3Sub3Sub1(&delta, &entity->m20_position, entity, edge, &hit, -1, &flags) == -1) {
            delta.vx = position->vx;
            delta.vy = position->vy;
            delta.vz = position->vz;
            updateEntityEventCode3Sub3Sub2(ang, edge, &delta, flags);
        } else {
            a = angle & 0xFFF;
            delta.vx = position->vx + getAngleSin(a) * 0x40;
            delta.vz = position->vz + getAngleCos(a) * -0x40;
            if (updateEntityEventCode3Sub3Sub1(&delta, &entity->m20_position, entity, edge, &hit, -1, &flags) == -1) {
                delta.vx = position->vx;
                delta.vy = position->vy;
                delta.vz = position->vz;
                updateEntityEventCode3Sub3Sub2(ang, edge, &delta, flags);
            } else {
                delta.vx = position->vx;
                delta.vy = position->vy;
                delta.vz = position->vz;
            }
        }
    }
    if (updateEntityEventCode3Sub3Sub1(&delta, &entity->m20_position, entity, edge, &hit, 0, &flags) == -1) {
        return -1;
    }
    savedDelta.vx = delta.vx;
    savedDelta.vy = delta.vy;
    savedDelta.vz = delta.vz;
    savedHit.vx = hit.vx;
    savedHit.vy = hit.vy;
    savedHit.vz = hit.vz;
    if ((s16)(entity->m20_position.vy >> 16) > hit.vy) {
    slide:
        dir.vx = -delta.vx >> 8;
        dir.vy = ((hit.vy << 16) - entity->m20_position.vy) >> 8;
        dir.vz = -delta.vz >> 8;
        VectorNormal(&dir, &normal);
        len = length2d(delta.vx >> 8, delta.vz >> 8);
        delta.vx = -(len * normal.vx) >> 4;
        delta.vy = len * normal.vy >> 4;
        delta.vz = -(len * normal.vz) >> 4;
        if (updateEntityEventCode3Sub3Sub1(&delta, &entity->m20_position, entity, edge, &hit, 0, &flags) == -1) {
            return -1;
        }
        entity->m0_fieldScriptFlags |= 0x4000000;
    } else {
        if (flags & 0x200000) {
            goto slide;
        }
        if (flags & 0x420000) {
            if (!(entity->m14_currentTriangleFlag & 0x420000)) {
                goto ok;
            }
            goto slide;
        }
        if (hit.vy < (s16)(entity->m20_position.vy >> 16) + 0x40) {
            goto slide;
        }
    ok:
        delta.vx = savedDelta.vx;
        delta.vy = savedDelta.vy;
        delta.vz = savedDelta.vz;
        hit.vx = savedHit.vx;
        hit.vy = savedHit.vy;
        hit.vz = savedHit.vz;
    }
    delta.vy = (hit.vy << 16) - entity->m20_position.vy;
    position->vx = delta.vx;
    position->vy = delta.vy;
    position->vz = delta.vz;
    entity->m72_elevation = (entity->m20_position.vy + position->vy) >> 16;
    return 0;
}

s32 updateEntityEventCode3Sub3Sub1(VECTOR* delta, VECTOR* position, sFieldScriptEntity* entity, SVECTOR* edge, SVECTOR* hit, s32 mode, u32* flags) {
    VECTOR dummyNormal;
    sWalkMeshTriangle* pTriangles;
    SVECTOR* pVertices;
    s32 triangleId;
    s32 tempTriangleId;
    s32 endPosition;
    s32 mask;
    s32 startPosition;
    s32 iterationCount;
    s32 collisionFlag;
    s32 vert0;
    s32 vert1;
    s32 vert2;
    u32 triangleFlags;
    s32 skipHeightCheck;

    pTriangles = (sWalkMeshTriangle*)fieldSceneState.m2A4_walkMeshTriangle[entity->m10_walkmeshId];
    pVertices = (SVECTOR*)fieldSceneState.m2B4_walkMeshVertices[entity->m10_walkmeshId];
    triangleId = entity->m8_currentWalkMeshTriangle[entity->m10_walkmeshId];
    if (triangleId == -1) {
        return -1;
    }
    endPosition = (((position->vx + delta->vx) >> 16) << 16) + ((position->vz + delta->vz) >> 16);
    startPosition = ((position->vx >> 16) << 16) + (position->vz >> 16);
    hit->vx = (position->vx + delta->vx) >> 16;
    mask = 0;
    hit->vz = (position->vz + delta->vz) >> 16;
    hit->vy = 0;
    if (!((entity->m4_flags >> (entity->m10_walkmeshId + 3)) & 1)) {
        mask = -(op9DVar1.m48_noUpdatesToPartyMembers == 0);
    }
    triangleFlags = walkMeshVar1[pTriangles[triangleId].mC_indexInWalkmeshData1] & mask;
    if ((triangleFlags & 0x400000) || mode == 0x80) {
        skipHeightCheck = 1;
    } else {
        skipHeightCheck = 0;
    }
    for (iterationCount = 0; iterationCount < 0x20; iterationCount++) {
        tempTriangleId = triangleId;
        vert0 = (pVertices[pTriangles[triangleId].m0_verticeIndex[0]].vx << 16) + pVertices[pTriangles[triangleId].m0_verticeIndex[0]].vz;
        vert1 = (pVertices[pTriangles[triangleId].m0_verticeIndex[1]].vx << 16) + pVertices[pTriangles[triangleId].m0_verticeIndex[1]].vz;
        vert2 = (pVertices[pTriangles[triangleId].m0_verticeIndex[2]].vx << 16) + pVertices[pTriangles[triangleId].m0_verticeIndex[2]].vz;
        collisionFlag = 0;
        if (NormalClip(vert0, vert1, endPosition) < 0) {
            collisionFlag |= 1;
        }
        if (NormalClip(vert1, vert2, endPosition) < 0) {
            collisionFlag |= 2;
        }
        if (NormalClip(vert2, vert0, endPosition) < 0) {
            collisionFlag |= 4;
        }
        switch (collisionFlag) {
        case 0:
            iterationCount = 0xFF;
            break;
        case 1:
            triangleId = pTriangles[triangleId].m6_connectivity[0];
            break;
        case 2:
            triangleId = pTriangles[triangleId].m6_connectivity[1];
            break;
        case 3:
            if (NormalClip(vert1, endPosition, startPosition) < 0) {
                triangleId = pTriangles[triangleId].m6_connectivity[0];
                collisionFlag = 1;
            } else {
                triangleId = pTriangles[triangleId].m6_connectivity[1];
                collisionFlag = 2;
            }
            break;
        case 4:
            triangleId = pTriangles[triangleId].m6_connectivity[2];
            break;
        case 5:
            if (NormalClip(vert0, endPosition, startPosition) < 0) {
                triangleId = pTriangles[triangleId].m6_connectivity[2];
                collisionFlag = 4;
            } else {
                triangleId = pTriangles[triangleId].m6_connectivity[0];
                collisionFlag = 1;
            }
            break;
        case 6:
            if (NormalClip(vert2, endPosition, startPosition) < 0) {
                triangleId = pTriangles[triangleId].m6_connectivity[1];
                collisionFlag = 2;
            } else {
                triangleId = pTriangles[triangleId].m6_connectivity[2];
                collisionFlag = 4;
            }
            break;
        case 7:
            triangleId = -1;
            break;
        }
        triangleFlags = walkMeshVar1[pTriangles[triangleId].mC_indexInWalkmeshData1] & mask;
        *flags = triangleFlags;
        if ((u8)(entity->m0_fieldScriptFlags >> 8 & 7) & triangleFlags >> 5) {
            triangleId = -1;
            break;
        }
        if ((triangleFlags & 0x800000) && entity->m10_walkmeshId == 0) {
            triangleId = -1;
            break;
        }
        if ((triangleFlags & 0x400000) && skipHeightCheck == 0) {
            getTriangleNormalAndAdjustY(&pVertices[pTriangles[triangleId].m0_verticeIndex[0]], &pVertices[pTriangles[triangleId].m0_verticeIndex[1]],
                          &pVertices[pTriangles[triangleId].m0_verticeIndex[2]], hit, &dummyNormal);
            if (hit->vy < (position->vy >> 16)) {
                triangleId = -1;
                break;
            }
        }
        if (triangleId == -1) {
            break;
        }
    }
    if (triangleId != -1 && iterationCount != 0x20) {
        if (mode == -1) {
            return 0;
        }
        getTriangleNormalAndAdjustY(&pVertices[pTriangles[triangleId].m0_verticeIndex[0]], &pVertices[pTriangles[triangleId].m0_verticeIndex[1]],
                      &pVertices[pTriangles[triangleId].m0_verticeIndex[2]], hit, &dummyNormal);
        return 0;
    }
    switch (collisionFlag) {
    case 1:
        edge[0].vx = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[0]].vx;
        edge[0].vy = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[0]].vy;
        edge[0].vz = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[0]].vz;
        edge[1].vx = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[1]].vx;
        edge[1].vy = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[1]].vy;
        edge[1].vz = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[1]].vz;
        break;
    case 2:
        edge[0].vx = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[1]].vx;
        edge[0].vy = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[1]].vy;
        edge[0].vz = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[1]].vz;
        edge[1].vx = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[2]].vx;
        edge[1].vy = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[2]].vy;
        edge[1].vz = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[2]].vz;
        break;
    case 4:
        edge[0].vx = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[2]].vx;
        edge[0].vy = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[2]].vy;
        edge[0].vz = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[2]].vz;
        edge[1].vx = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[0]].vx;
        edge[1].vy = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[0]].vy;
        edge[1].vz = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[0]].vz;
        break;
    }
    return -1;
}

void EntityMoveCheck1Sub1Sub1(s32* a, s32* b, s32 c) {
    s32 v = *a;
    if (c >= 0) {
        *a = v;
        v += c;
    } else {
        *a = v;
    }
    *b = v;
}

s32 updateEntityEventCode3Sub4Sub1(VECTOR* delta, VECTOR* position, sFieldScriptEntity* entity, SVECTOR* edge, SVECTOR* hit, s32 mode) {
    VECTOR dummyNormal;
    sWalkMeshTriangle* pTriangles;
    SVECTOR* pVertices;
    s32 triangleId;
    s32 tempTriangleId;
    s32 endPosition;
    s32 mask;
    s32 startPosition;
    s32 iterationCount;
    s32 collisionFlag;
    s32 vert0;
    s32 vert1;
    s32 vert2;
    u32 flags;

    pTriangles = (sWalkMeshTriangle*)fieldSceneState.m2A4_walkMeshTriangle[entity->m10_walkmeshId];
    pVertices = (SVECTOR*)fieldSceneState.m2B4_walkMeshVertices[entity->m10_walkmeshId];
    triangleId = entity->m8_currentWalkMeshTriangle[entity->m10_walkmeshId];
    if (triangleId == -1) {
        return -1;
    }
    endPosition = (((position->vx + delta->vx) >> 16) << 16) + ((position->vz + delta->vz) >> 16);
    startPosition = ((position->vx >> 16) << 16) + (position->vz >> 16);
    hit->vx = (position->vx + delta->vx) >> 16;
    mask = 0;
    hit->vz = (position->vz + delta->vz) >> 16;
    hit->vy = 0;
    if (!((entity->m4_flags >> (entity->m10_walkmeshId + 3)) & 1)) {
        mask = -(op9DVar1.m48_noUpdatesToPartyMembers == 0);
    }
    for (iterationCount = 0; iterationCount < 0x20; iterationCount++) {
        tempTriangleId = triangleId;
        vert0 = (pVertices[pTriangles[triangleId].m0_verticeIndex[0]].vx << 16) + pVertices[pTriangles[triangleId].m0_verticeIndex[0]].vz;
        vert1 = (pVertices[pTriangles[triangleId].m0_verticeIndex[1]].vx << 16) + pVertices[pTriangles[triangleId].m0_verticeIndex[1]].vz;
        vert2 = (pVertices[pTriangles[triangleId].m0_verticeIndex[2]].vx << 16) + pVertices[pTriangles[triangleId].m0_verticeIndex[2]].vz;
        collisionFlag = 0;
        if (NormalClip(vert0, vert1, endPosition) < 0) {
            collisionFlag |= 1;
        }
        if (NormalClip(vert1, vert2, endPosition) < 0) {
            collisionFlag |= 2;
        }
        if (NormalClip(vert2, vert0, endPosition) < 0) {
            collisionFlag |= 4;
        }
        switch (collisionFlag) {
        case 0:
            iterationCount = 0xFF;
            break;
        case 1:
            triangleId = pTriangles[triangleId].m6_connectivity[0];
            break;
        case 2:
            triangleId = pTriangles[triangleId].m6_connectivity[1];
            break;
        case 3:
            if (NormalClip(vert1, endPosition, startPosition) < 0) {
                triangleId = pTriangles[triangleId].m6_connectivity[0];
                collisionFlag = 1;
            } else {
                triangleId = pTriangles[triangleId].m6_connectivity[1];
                collisionFlag = 2;
            }
            break;
        case 4:
            triangleId = pTriangles[triangleId].m6_connectivity[2];
            break;
        case 5:
            if (NormalClip(vert0, endPosition, startPosition) < 0) {
                triangleId = pTriangles[triangleId].m6_connectivity[2];
                collisionFlag = 4;
            } else {
                triangleId = pTriangles[triangleId].m6_connectivity[0];
                collisionFlag = 1;
            }
            break;
        case 6:
            if (NormalClip(vert2, endPosition, startPosition) < 0) {
                triangleId = pTriangles[triangleId].m6_connectivity[1];
                collisionFlag = 2;
            } else {
                triangleId = pTriangles[triangleId].m6_connectivity[2];
                collisionFlag = 4;
            }
            break;
        case 7:
            triangleId = -1;
            break;
        }
        flags = walkMeshVar1[pTriangles[triangleId].mC_indexInWalkmeshData1] & mask;
        if ((u8)(entity->m0_fieldScriptFlags >> 9 & 3) & flags >> 3) {
            triangleId = -1;
            break;
        }
        if ((u8)(entity->m0_fieldScriptFlags >> 8 & 7) & flags >> 5) {
            triangleId = -1;
            break;
        }
        if ((flags & 0x800000) && entity->m10_walkmeshId == 0) {
            triangleId = -1;
            break;
        }
        if (triangleId == -1) {
            break;
        }
    }
    if (triangleId != -1 && iterationCount != 0x20) {
        if (mode == -1) {
            return 0;
        }
        getTriangleNormalAndAdjustY(&pVertices[pTriangles[triangleId].m0_verticeIndex[0]], &pVertices[pTriangles[triangleId].m0_verticeIndex[1]],
                      &pVertices[pTriangles[triangleId].m0_verticeIndex[2]], hit, &dummyNormal);
        return 0;
    }
    switch (collisionFlag) {
    case 1:
        edge[0].vx = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[0]].vx;
        edge[0].vy = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[0]].vy;
        edge[0].vz = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[0]].vz;
        edge[1].vx = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[1]].vx;
        edge[1].vy = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[1]].vy;
        edge[1].vz = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[1]].vz;
        break;
    case 2:
        edge[0].vx = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[1]].vx;
        edge[0].vy = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[1]].vy;
        edge[0].vz = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[1]].vz;
        edge[1].vx = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[2]].vx;
        edge[1].vy = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[2]].vy;
        edge[1].vz = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[2]].vz;
        break;
    case 4:
        edge[0].vx = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[2]].vx;
        edge[0].vy = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[2]].vy;
        edge[0].vz = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[2]].vz;
        edge[1].vx = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[0]].vx;
        edge[1].vy = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[0]].vy;
        edge[1].vz = pVertices[pTriangles[tempTriangleId].m0_verticeIndex[0]].vz;
        break;
    }
    return -1;
    
}

/* Noah: allocateScratchBufferLocationForMoveCheck (size in words) */
void* allocateScratchBufferLocationForMoveCheck(s32 size) {
    s32 old = positionInScratchBufferForMove;
    positionInScratchBufferForMove = old + size;
    return (void*)(uptr)(old * 4 + PSX_SCRATCH(0x1F800000));
}

/* Noah: freeScratchBuffer (size in words) */
void freeScratchBuffer(s32 size) {
    positionInScratchBufferForMove -= size;
}

s32 checkCameraCollision(VECTOR* position, SVECTOR* edge, s16* output) {
    SVECTOR hit;
    VECTOR normal;
    s32 opz;
    sWalkMeshTriangle* pTriangles;
    SVECTOR* pVertices;
    s32 x;
    s32 z;
    s32 refPos;
    s32 posX;
    s32 posZ;
    s32 refPos2;
    s32 triangleId;
    s32 lastTriangle;
    s32 iterationCount;
    s32 collisionFlag;
    s32 vert0;
    s32 vert1;
    s32 vert2;
    s32 t;

    pTriangles = (sWalkMeshTriangle*)fieldSceneState.m2A4_walkMeshTriangle[fieldSceneState.m2D4_numWalkMesh - 1];
    pVertices = (SVECTOR*)fieldSceneState.m2B4_walkMeshVertices[fieldSceneState.m2D4_numWalkMesh - 1];
    refPos = ((position->vx >> 16) << 16) + (position->vz >> 16);
    x = position->vx >> 16;
    z = position->vz >> 16;
    if (x < fieldSceneState.m15C_cameraLimits[0]) {
        posX = fieldSceneState.m15C_cameraLimits[0];
    } else {
        t = fieldSceneState.m15C_cameraLimits[0] + fieldSceneState.m15C_cameraLimits[2];
        if (t < x) {
            posX = t;
        } else {
            posX = x;
        }
    }
    if (fieldSceneState.m15C_cameraLimits[1] < z) {
        posZ = fieldSceneState.m15C_cameraLimits[1];
    } else {
        t = fieldSceneState.m15C_cameraLimits[1] + fieldSceneState.m15C_cameraLimits[3];
        if (z < t) {
            posZ = t;
        } else {
            posZ = z;
        }
    }
    refPos2 = (posX << 16) + posZ;
    triangleId = findTriangleInWalkMesh(posX, posZ, fieldSceneState.m2D4_numWalkMesh - 1, &hit, &normal);
    for (iterationCount = 0; iterationCount < 0xF0; iterationCount++) {
        lastTriangle = triangleId;
        vert0 = (pVertices[pTriangles[triangleId].m0_verticeIndex[0]].vx << 16) + pVertices[pTriangles[triangleId].m0_verticeIndex[0]].vz;
        vert1 = (pVertices[pTriangles[triangleId].m0_verticeIndex[1]].vx << 16) + pVertices[pTriangles[triangleId].m0_verticeIndex[1]].vz;
        vert2 = (pVertices[pTriangles[triangleId].m0_verticeIndex[2]].vx << 16) + pVertices[pTriangles[triangleId].m0_verticeIndex[2]].vz;
        collisionFlag = 0;
        gte_ldsxy3(vert0, vert1, refPos);
        gte_nclip();
        gte_stopz(&opz);
        gte_ldsxy3(vert1, vert2, refPos);
        gte_nclip();
        if (opz < 0) {
            collisionFlag |= 1;
        }
        gte_stopz(&opz);
        gte_ldsxy3(vert2, vert0, refPos);
        gte_nclip();
        if (opz < 0) {
            collisionFlag |= 2;
        }
        gte_stopz(&opz);
        if (opz < 0) {
            collisionFlag |= 4;
        }
        switch (collisionFlag) {
        case 0:
            iterationCount = 0xFF;
            break;
        case 1:
            triangleId = pTriangles[triangleId].m6_connectivity[0];
            break;
        case 2:
            triangleId = pTriangles[triangleId].m6_connectivity[1];
            break;
        case 3:
            gte_ldsxy3(vert1, refPos, refPos2);
            gte_nclip();
            gte_stopz(&opz);
            if (opz < 0) {
                triangleId = pTriangles[triangleId].m6_connectivity[0];
                collisionFlag = 1;
            } else {
                triangleId = pTriangles[triangleId].m6_connectivity[1];
                collisionFlag = 2;
            }
            break;
        case 4:
            triangleId = pTriangles[triangleId].m6_connectivity[2];
            break;
        case 5:
            gte_ldsxy3(vert0, refPos, refPos2);
            gte_nclip();
            gte_stopz(&opz);
            if (opz < 0) {
                triangleId = pTriangles[triangleId].m6_connectivity[2];
                collisionFlag = 4;
            } else {
                triangleId = pTriangles[triangleId].m6_connectivity[0];
                collisionFlag = 1;
            }
            break;
        case 6:
            gte_ldsxy3(vert2, refPos, refPos2);
            gte_nclip();
            gte_stopz(&opz);
            if (opz < 0) {
                triangleId = pTriangles[triangleId].m6_connectivity[1];
                collisionFlag = 2;
            } else {
                triangleId = pTriangles[triangleId].m6_connectivity[2];
                collisionFlag = 4;
            }
            break;
        case 7:
            triangleId = -1;
            break;
        }
        if (!(fieldSceneState.m2A0_walkMeshVar1[pTriangles[triangleId].mC_indexInWalkmeshData1] & 0x800000)) {
            triangleId = -1;
            break;
        }
        if (triangleId == -1) {
            break;
        }
    }
    if (triangleId != -1 && iterationCount != 0xF0) {
        return 0;
    }
    switch (collisionFlag) {
    case 1:
        edge[0].vx = pVertices[pTriangles[lastTriangle].m0_verticeIndex[0]].vx;
        edge[0].vy = pVertices[pTriangles[lastTriangle].m0_verticeIndex[0]].vy;
        edge[0].vz = pVertices[pTriangles[lastTriangle].m0_verticeIndex[0]].vz;
        edge[1].vx = pVertices[pTriangles[lastTriangle].m0_verticeIndex[1]].vx;
        edge[1].vy = pVertices[pTriangles[lastTriangle].m0_verticeIndex[1]].vy;
        edge[1].vz = pVertices[pTriangles[lastTriangle].m0_verticeIndex[1]].vz;
        break;
    case 2:
        edge[0].vx = pVertices[pTriangles[lastTriangle].m0_verticeIndex[1]].vx;
        edge[0].vy = pVertices[pTriangles[lastTriangle].m0_verticeIndex[1]].vy;
        edge[0].vz = pVertices[pTriangles[lastTriangle].m0_verticeIndex[1]].vz;
        edge[1].vx = pVertices[pTriangles[lastTriangle].m0_verticeIndex[2]].vx;
        edge[1].vy = pVertices[pTriangles[lastTriangle].m0_verticeIndex[2]].vy;
        edge[1].vz = pVertices[pTriangles[lastTriangle].m0_verticeIndex[2]].vz;
        break;
    case 4:
        edge[0].vx = pVertices[pTriangles[lastTriangle].m0_verticeIndex[2]].vx;
        edge[0].vy = pVertices[pTriangles[lastTriangle].m0_verticeIndex[2]].vy;
        edge[0].vz = pVertices[pTriangles[lastTriangle].m0_verticeIndex[2]].vz;
        edge[1].vx = pVertices[pTriangles[lastTriangle].m0_verticeIndex[0]].vx;
        edge[1].vy = pVertices[pTriangles[lastTriangle].m0_verticeIndex[0]].vy;
        edge[1].vz = pVertices[pTriangles[lastTriangle].m0_verticeIndex[0]].vz;
        break;
    }
    output[0] = x;
    output[1] = z;
    output[2] = posX;
    output[3] = posZ;
    return -1;
}

s32 EntityMoveCheck1Sub1(sFieldScriptEntity* entity, s32 walkmeshId, s32* pOutputAltitude, VECTOR* triangleNormal,
                  s16* pOutputTriangleId, s32* pOutputAltitude2) {
    SVECTOR testedPosition;
    sWalkMeshTriangle* pTriangles;
    SVECTOR* pVertices;
    s32 triangleId;
    s32 refPos;
    s32 mask;
    s32 refPos2;
    s32 iterationCount;
    s32 collisionFlag;
    s32 vert0;
    s32 vert1;
    s32 vert2;
    s32 triangleParamD;

    pTriangles = (sWalkMeshTriangle*)fieldSceneState.m2A4_walkMeshTriangle[walkmeshId];
    pVertices = (SVECTOR*)fieldSceneState.m2B4_walkMeshVertices[walkmeshId];
    triangleId = entity->m8_currentWalkMeshTriangle[walkmeshId];
    if (triangleId == -1) {
        return -1;
    }
    refPos = (((entity->m20_position.vx + entity->m30_stepVector.vx) >> 16) << 16) +
             ((entity->m20_position.vz + entity->m30_stepVector.vz) >> 16);
    refPos2 = ((entity->m20_position.vx >> 16) << 16) + (entity->m20_position.vz >> 16);
    testedPosition.vx = (entity->m20_position.vx + entity->m30_stepVector.vx) >> 16;
    mask = 0;
    testedPosition.vz = (entity->m20_position.vz + entity->m30_stepVector.vz) >> 16;
    testedPosition.vy = 0;

    if (!((entity->m4_flags >> (walkmeshId + 3)) & 1)) {
        mask = -(op9DVar1.m48_noUpdatesToPartyMembers == 0);
    }
    for (iterationCount = 0; iterationCount < 0x20; iterationCount++) {
        vert0 = (pVertices[pTriangles[triangleId].m0_verticeIndex[0]].vx << 16) + pVertices[pTriangles[triangleId].m0_verticeIndex[0]].vz;
        vert1 = (pVertices[pTriangles[triangleId].m0_verticeIndex[1]].vx << 16) + pVertices[pTriangles[triangleId].m0_verticeIndex[1]].vz;
        vert2 = (pVertices[pTriangles[triangleId].m0_verticeIndex[2]].vx << 16) + pVertices[pTriangles[triangleId].m0_verticeIndex[2]].vz;
        collisionFlag = 0;
        if (NormalClip(vert0, vert1, refPos) < 0) {
            collisionFlag |= 1;
        }
        if (NormalClip(vert1, vert2, refPos) < 0) {
            collisionFlag |= 2;
        }
        if (NormalClip(vert2, vert0, refPos) < 0) {
            collisionFlag |= 4;
        }
        switch (collisionFlag) {
        case 0:
            iterationCount = 0xFF;
            break;
        case 1:
            triangleId = pTriangles[triangleId].m6_connectivity[0];
            break;
        case 2:
            triangleId = pTriangles[triangleId].m6_connectivity[1];
            break;
        case 3:
            if (NormalClip(vert1, refPos, refPos2) < 0) {
                triangleId = pTriangles[triangleId].m6_connectivity[0];
            } else {
                triangleId = pTriangles[triangleId].m6_connectivity[1];
            }
            break;
        case 4:
            triangleId = pTriangles[triangleId].m6_connectivity[2];
            break;
        case 5:
            if (NormalClip(vert0, refPos, refPos2) < 0) {
                triangleId = pTriangles[triangleId].m6_connectivity[2];
            } else {
                triangleId = pTriangles[triangleId].m6_connectivity[0];
            }
            break;
        case 6:
            if (NormalClip(vert2, refPos, refPos2) < 0) {
                triangleId = pTriangles[triangleId].m6_connectivity[1];
            } else {
                triangleId = pTriangles[triangleId].m6_connectivity[2];
            }
            break;
        case 7:
            triangleId = -1;
            break;
        }
        if (triangleId == -1) {
            goto fail;
        }
    }
    if (iterationCount == 0x20) {
        goto fail;
    }
    getTriangleNormalAndAdjustY(&pVertices[pTriangles[triangleId].m0_verticeIndex[0]], &pVertices[pTriangles[triangleId].m0_verticeIndex[1]],
                  &pVertices[pTriangles[triangleId].m0_verticeIndex[2]], &testedPosition, triangleNormal);
    *pOutputTriangleId = triangleId;
    triangleParamD = pTriangles[triangleId].mD_thicknessDiv4 * 4;
    if (triangleParamD < 0) {
        triangleParamD = 0;
    }
    if (entity->m10_walkmeshId != walkmeshId) {
        if (walkMeshVar1[pTriangles[triangleId].mC_indexInWalkmeshData1] & 0x800000 & mask) {
            *pOutputAltitude = 0x7FFFFFFF;
            *pOutputAltitude2 = 0x7FFFFFFF;
        } else {
            *pOutputAltitude = testedPosition.vy;
            EntityMoveCheck1Sub1Sub1(pOutputAltitude, pOutputAltitude2, triangleParamD);
        }
    } else {
        if (walkMeshVar1[pTriangles[triangleId].mC_indexInWalkmeshData1] & 0x800000 & mask) {
            *pOutputAltitude = 0x7FFFFFFF;
            *pOutputAltitude2 = 0x7FFFFFFF;
        } else if (entity->m30_stepVector.vx == 0 && entity->m30_stepVector.vy == 0 && entity->m30_stepVector.vz == 0) {
            *pOutputAltitude = testedPosition.vy;
            EntityMoveCheck1Sub1Sub1(pOutputAltitude, pOutputAltitude2, triangleParamD);
        } else {
            *pOutputAltitude = entity->m72_elevation;
            EntityMoveCheck1Sub1Sub1(pOutputAltitude, pOutputAltitude2, triangleParamD);
        }
    }
    return 0;
fail:
    return -1;
}

/* normalize a fixed-point vector, sign-flipped so its largest component
 * is positive */
void normalizeVectorMajorAxisPositive(VECTOR* v, VECTOR* out) {
    s32 sign = getLargestMagnitudeComponent(v->vx, v->vy, v->vz);

    v->vx >>= 12;
    v->vy >>= 12;
    v->vz >>= 12;
    if (sign < 0) {
        v->vx = -v->vx;
        v->vy = -v->vy;
        v->vz = -v->vz;
    }
    VectorNormal(v, out);
}

/* the component of (x, y, z) with the largest magnitude */
s32 getLargestMagnitudeComponent(s32 x, s32 y, s32 z) {
    s32 ax = x;
    s32 ay = y;
    s32 az = z;

    if (ax < 0) {
        ax = -ax;
    }
    if (ay < 0) {
        ay = -ay;
    }
    if (az < 0) {
        az = -az;
    }
    if (ax >= ay && ax >= az) {
        return x;
    }
    if (ay >= ax && ay >= az) {
        return y;
    }
    if (az >= ax && az >= ay) {
        return z;
    }
    return 0;
}
