/*
 * Kernel: main(), boot logo, soft reboot, kernel debug menu (with its
 * Game of Life screen), game-mode overlay loading and party file loading.
 *
 * Provisional file 0x80019578-0x8001B6C4 (main executable); bounds come from
 * compiler/assembler changes and may move. Built with GCC 2.7.2 (PSY-Q), ASPSX < 2.50.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/sound.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "main/dialog.h"
#include "main/debug_font.h"
#include "stack.h"
#include "pc_gameid.h"

/* Noah: sBootMode (table gameModes, indexed by bootMode bootMode). */
typedef struct {
    /* 0x00 */ void (*m0_entryPoint)(void);
    /* 0x04 */ u32 m4_startOfZeroInit;
    /* 0x08 */ u32 m8_endOfZeroInit;
    /* 0x0C */ u32 mC_needOverlay;
} BootModeEntry;

/* TILE_8 as the life-game code writes it: rgb+code and x/y as whole words. */
typedef struct {
    OTag m0_tag;
    u32 m4_rgbc;     /* 0x70280000: code 0x70 (TILE_8), rgb 00/00/28 */
    u32 m8_xy;       /* (y << 16) | x */
} KernelTile;

/* Noah: sKernelDisplayContext */
typedef struct {
    DRAWENV m0_drawEnv;
    DISPENV m5C_dispEnv;
    OTag m70_ot;
    POLY_F3 m74_cursor;
} KernelDisplayContext;

/* .rodata word holding &D_8006FAF0 (only user: bootGame) */
extern void* overlayLoadAddress;
/* Noah: gameModes */
extern BootModeEntry gameModes[];
/* The port's heap arena and overlay reset (port/src/memory.c). */
u8* portHeapStart(void);
u8* portHeapEnd(void);
void portResetGameMode(s32 mode);
s32 portBootMode(void); /* port/src/system/options.c: XG_BOOT_MODE (or 1 for XG_FIELD), or -1 */
void portStartState(void); /* options.c: XG_PRESET, XG_PARTY, XG_GAMESTATE, XG_FIELD */
void gameStackScrub(void); /* port/src/system/gamestack.c: the dead stack below here to zeros */
s32 portStartPending(void); /* options.c: the extras ask for a start (a scene to replay) */
void cdBootTables(u8* fileTable, u16* directoryTable); /* port/src/cd/boot.c: started on disc 2 */
void deinitDebugFont(void);
void kernelBootMenu(void);
void fieldEntryPoint(void);
void battleEntryPoint(void);
void worldmapEntryPoint(void);
void battlingMain(void);
void enterMenu(void);
void movieEntryPoint(void);
/* The PS1 keeps this table in the boot header (assembly); its zero-init
 * ranges are overlay BSS addresses, which mean nothing on the PC, where the
 * port resets each overlay's data instead (portResetGameMode). */
BootModeEntry gameModes[7] = {
    { kernelBootMenu, 0, 0, 0 },
    { fieldEntryPoint, 0, 0, 1 },
    { battleEntryPoint, 0, 0, 1 },
    { worldmapEntryPoint, 0, 0, 1 },
    { battlingMain, 0, 0, 1 },
    { enterMenu, 0, 0, 0 },
    { movieEntryPoint, 0, 0, 1 },
};
void showSystemError(s32 errNo, s32 from);
void resetDefaultMemoryAllocStats(void);
void clearMemoryRange(u32, u32);
void EnterCriticalSection(void);
void FlushCache(void);
void ExitCriticalSection(void);
void func_80019548(void);
void resetHeapTraceState(void);
void resetInputs(void);
void* loadGameModeOverlay(s32 mode);
/* ---- read-only data of the boot header object (assembly, before kernel.c's
 * .rodata: kernel.c treats it as writable data) ---- */
/* 0 = CD, -1 = PC HDD (DTL); Noah: runningOnDTL */
extern s32 runningOnDTL;
/* 32 KB disc file table (in .rodata) */
extern u8 discFileTable[];
/* 128-byte disc directory table (in .rodata) */
extern u16 discDirectoryTable[];
/* bootMode (written despite living in .rodata) */
extern s32 bootMode;
/* ---- data (defined below, in retail .data order) and BSS ---- */
/* These two stay an assembly data subsegment in front of kernel.c's .data
 * (US 0x8004EAA0-0x8004F2BC, JP 0x8004E948-0x8004E964): the logo is game data. */
/* overlay file index per boot mode */
extern s32 gameModeOverlayFileIds[];
/* compressed boot logo image (US only) */
extern u8 compressedBootLogo[0x800]; /* the boot logo, compressed (main data labels its second part D_8004F0C0) */
/* system error count */
extern s32 systemErrorCount;
/* kernel menu cursor */
extern s32 kernelMenuCursor;
extern s32 D_8004F2F4;
extern s32 fieldBootedOnce;
extern s32 movieScrollingTextEnabled;
extern s32 worldmapSoundFilesPendingCount;
extern s32 fieldMusicLoadPending;
extern s32 fieldScriptEntityAlreadyInitialized;
extern s32 D_8004F310;
extern s32 D_8004F314;
extern s32 fieldScriptTimerTicks;
extern s32 typeOfPlayableCharacterLoaded;
extern s32 fieldRequestedGears;
extern s32 currentlyPlayingMusic;
extern s32 fieldScriptTimerMode;
extern s32 loadFieldSeqVar0;
extern s32 currentFieldId0;
extern s32 currentFieldId1;
extern s32 currentlyLoadedMusic;
extern s32 currentlyLoadedWave;
extern s32 musicVar1;
extern s32 compassGraphicDataLoaded;
extern s32 updateMusicState2Var2;
extern s32 fieldMapNumber;
extern s32 g_menuOpenCount;
extern s32 musicVar2;
extern s32 updateMusicState2Var1;
extern s32 needStartMusicInstanceSub0;
extern s32 needReloadWds;
extern s32 playMusicSubVar2;
extern s32 playMusicSubVar0;
extern s32 updateMusicState2Var3;
extern s32 mechaOverlayLoadingMode;
extern s32 characterOrGearsLoadingInProgress;
extern s32 fieldCompassVar;
extern s32 disableCharacterShadowsRendering;
extern s32 disableMechaRendering;
extern s16 playWorldmapMusic;
/* currentGameModeOverlay */
static void* currentGameModeOverlay;
/* oldBootMode (loaded overlay mode) */
static s32 oldBootMode;
/* kernel menu frame counter */
static s32 kernelMenuFrameCount;
/* kernel menu oddOrEven */
static s32 kernelMenuOddOrEven;
/* gKernelDisplayContext */
static KernelDisplayContext* gKernelDisplayContext;
/* kernel menu keep-running flag */
static s32 kernelMenuRunning;
/* life grid: alive cells, 28x40 */
static u8* lifeGameCells;
/* life grid: neighbour counts, 28x40 */
static u8* lifeGameNeighbourCounts;
/* life tiles, one buffer per frame */
static KernelTile* lifeGameTiles[2];
/* play time: seconds */
extern u8 g_playTimeSeconds;
/* play time: minutes */
extern u8 g_playTimeMinutes;
extern u8 battleEventReturnMode;
/* play time: hours */
extern u8 g_playTimeHours;
/* newPadButtonForDialogs */
extern u16 newPadButtonForDialogs;
/* newPadButtonForField */
extern u16 newPadButtonForField;
extern u8 menuReturnState0;
/* newPadButtonForScripts[0].m0_buttons */
extern u16 newPadButtonForScripts;
/* kernelMenuDrawEnvDoubleBuffer */
static KernelDisplayContext kernelMenuDrawEnvDoubleBuffer[2];
extern GameState* pKernelGameState;
extern void* partyCharacterBuffers[3];
extern s32 partyFieldActorIds[];
extern void* compassGraphicDataStaging;
extern void* loadFieldSeqVar1;
extern s32 rawFieldBundleSize;
extern void* rawFieldBundle;
extern s32 currentParty[];
extern LoadingBatchCommand partyLoadingBatchCommands[11]; /* to padReceiveBuffers */
extern void* partyCharacterBuffersCompressed[3];
extern s32 partyGearActorIds[];
extern s32 asyncPartyCharacterLoadingTable[];
/* ---- functions ---- */
void PCinit(void);
void PCclose(s32);
void initPadSystem(void);
void _bu_init(void);
void vsyncCallback(void);
void traceNextAlloc(s32);
void initKernelGlobals(void);
void initGameState(void);
void resetSpriteSystem(void);
void setBattlingMainTaskIndex(s32);
s32 getCurrentDiscNumber(void);
s32 isControllerConnected(s32);
s32 loadInputFromVSyncBuffer(void);
void showBootLogo(void);
void postBootLogoNop(void);
void dumpHeapToPcFile(const char*);
void forceFreeAllHeap(void);
void getLastAllocRequest(s32*, s32*);
void setGameMode(s32);
void loadPartyFiles(s32 arg0);
void processLoadedPartyFiles(void);
void loadRawFieldBundle(s32 arg0);
void startKernel(void);
void bootGame(s32);
void softReboot(void);
void initKernelMenuDrawContext(s32);
void setupKernelMenuDraw(void);
void printKernelMenu(void);
void lifeGameAddNeighbour(s32, s32);
s32 getHeapUser(void);
void setHeapUser(s32);
void setVsyncUserCallback(void (*callback)(void));
void shutdownSoundSystem(void);

/* The game's main(). Its retail name is unknown: under the name "main" GCC
 * inserts a `jal __main` call that retail lacks. */
void kernelMain(void) {
    RECT rect;
    void* file2;
    void* file3;
    void* file4;
    void* file5;
    s32 heapMode;
    void* buf;

    ResetCallback();
    SetGraphDebug(0);
    SetVideoMode(0);
    ResetGraph(0);
    rect.x = 0;
    rect.y = 0;
    rect.w = 0x180;
    rect.h = 0x1E0;
    ClearImage(&rect, 0, 0, 0);
    DrawSync(0);
    SetDispMask(1);
    InitGeom();
    initPadSystem();
    InitCARD(1);
    StartCARD();
    _bu_init();
    VSyncCallback(vsyncCallback);
    initHeap((HeapBlock*)portHeapStart(), (HeapBlock*)portHeapEnd());
    pcHeapStartRetail(portRetailHeapStart(0));
    SpuInit();
    /* Disc 2's executable embeds disc 2's file tables; the PC links disc 1's. */
    cdBootTables(discFileTable, discDirectoryTable);
    initCDAndFileSystem(discFileTable, discDirectoryTable, runningOnDTL);
    initSoundSystem(0);
    setCurrentDirectory(0, 1);
    file2 = allocateHeap(PC_HEAP_BYTES(getFileSize(2)), 0);
    file3 = allocateHeap(PC_HEAP_BYTES(getFileSize(3)), 0);
    file4 = allocateHeap(PC_HEAP_BYTES(getFileSize(4)), 0);
    file5 = allocateHeap(PC_HEAP_BYTES(getFileSize(5)), 0);
    readFile(2, file2, 0, 0);
    readFile(3, file3, 0, 0);
    readFile(4, file4, 0, 0);
    readFile(5, file5, 0, 0);
    waitReadCompletion(0);
    loadWds(file2, 0);
    wdsFile3 = loadWds(file3, 0);
    loadWds(file4, 0);
    wdsFile5 = loadWds(file5, 0);
    heapMode = getHeapUser();
    setHeapUser(6);
    buf = allocateHeap(PC_HEAP_BYTES(getFileSize(6)), 0);
    readFile(6, buf, 0, 0);
    waitReadCompletion(0);
    traceNextAlloc(0x30);
    initFont(mallocAndDecompress(buf, 1));
    freeHeap(buf);
    buf = allocateHeap(PC_HEAP_BYTES(getFileSize(7)), 0);
    readFile(7, buf, 0, 0);
    waitReadCompletion(0);
    traceNextAlloc(0x31);
    setupPrintDialogTextVar(mallocAndDecompress(buf, 1));
    freeHeap(buf);
    setHeapUser(heapMode);
    waitForMusic(0x10);
    freeHeap(file2);
    freeHeap(file3);
    freeHeap(file4);
    freeHeap(file5);
    initKernelGlobals();
    initGameState();
    pcGameIdNewGame(); /* a fresh game: no id until it saves (pc_gameid.h) */
    resetSpriteSystem();
    setBattlingMainTaskIndex(0);
    oldBootMode = -1;
    currentGameModeOverlay = 0;
    movieType.m0_type = 1;
    movieType.m2_returnMode = 1;
    movieType.m3_fadeParam = 0;
    if (getCurrentDiscNumber() == 1) {
        movieType.m1_number = 0x10;
    } else {
        movieType.m1_number = 7;
    }
    if (isControllerConnected(0) != 0 && newPadButtonForScripts == 0x90C) {
        while (newPadButtonForScripts == 0x90C) {
            loadInputFromVSyncBuffer();
            PORT_YIELD(); /* after a soft reset: until the vsync callback sees the buttons released */
        }
    }
#if !VERSION_JP
    showBootLogo();
    postBootLogoNop();
#endif
    /* A development start: the kernel's debug boot menu (mode 0) or any mode
     * directly, instead of the opening movie. */
    if (portBootMode() >= 0) {
        portStartState();
        setGameMode(portBootMode());
        gameStackScrub();
        bootGame(0);
    }
    setGameMode(6);
    /* The host libraries started above ran on this stack: their leftovers
     * would show in bootGame's frame (kept for the whole game) as a slot
     * nothing writes, different in every process. */
    gameStackScrub();
    bootGame(0);
}

void printfStub(void) {
}

/* setGameMode (Noah): select the boot mode; drop the loaded overlay if it changes. */
void setGameMode(s32 mode) {
    bootMode = mode;
    if (mode != oldBootMode) {
        if (currentGameModeOverlay != 0) {
            freeHeap(currentGameModeOverlay);
            currentGameModeOverlay = 0;
        }
        oldBootMode = -1;
    }
}

/* loadGameModeOverlay: load the overlay file of boot mode `mode` once. */
void* loadGameModeOverlay(s32 mode) {
    s32 heapMode;
    s32 dir;
    s32 dirOffset;
    s32 prev;

    if (oldBootMode != mode) {
        oldBootMode = mode;
        heapMode = getHeapUser();
        getCurrentDirectory(&dir, &dirOffset);
        setHeapUser(6);
        setCurrentDirectory(0, 1);
        prev = setHeapAllowFailure(1);
        currentGameModeOverlay = allocateHeap(PC_HEAP_BYTES(getFileSize(gameModeOverlayFileIds[mode])), 1);
        if (currentGameModeOverlay != 0) {
            readFile(gameModeOverlayFileIds[mode], currentGameModeOverlay, 0, 0);
        } else {
            oldBootMode = -1;
        }
        setHeapAllowFailure(prev);
        setCurrentDirectory(dir, dirOffset);
        setHeapUser(heapMode);
    }
    return currentGameModeOverlay;
}

/* The PS1 discards the stack before each mode (func_80019548 resets $sp) and
 * then boots again by recursion, often from deep inside the previous mode.
 * The PC unwinds back here instead, so modes do not nest on the stack. */
#if defined(__clang__) && defined(__aarch64__)
/* clang has __builtin_setjmp on some targets only (x86 among them, aarch64
 * not): the platform's own pair there (port/src/system/gamestack.c). */
int portBootSetjmp(void** context) __attribute__((returns_twice));
void portBootLongjmp(void** context) __attribute__((noreturn));
#define BOOT_SETJMP(context) portBootSetjmp(context)
#define BOOT_LONGJMP(context) portBootLongjmp(context)
#define BOOT_CONTEXT_WORDS 21
#else
#define BOOT_SETJMP(context) __builtin_setjmp(context)
#define BOOT_LONGJMP(context) __builtin_longjmp(context, 1)
#define BOOT_CONTEXT_WORDS 5
#endif
static void* bootContext[BOOT_CONTEXT_WORDS];
static s32 bootContextReady;
static s32 bootErrNo;

void bootGame(s32 errNo) {
    u32 ra;
    BootModeEntry* mode;

    bootErrNo = errNo;
    if (errNo != 0) {
        pcHeapUnsupported(); /* an error unwind cannot preserve allocation history */
        GET_RA(&ra);
        showSystemError(errNo, ra);
        bootErrNo = 0;
    }
    if (bootContextReady) {
        BOOT_LONGJMP(bootContext);
    }
    bootContextReady = 1;
    BOOT_SETJMP(bootContext);
    mode = &gameModes[bootMode];
    ResetGraph(1);
    DrawSyncCallback(0);
    setVsyncUserCallback(0);
    DrawSync(0);
    VSync(2);
    /* The old mode's debug font is a heap block (the kernel menu's): drop it
     * before the heap is reused (the PS1 keeps the stale pointer). */
    deinitDebugFont();
    resetHeap((HeapBlock*)portHeapStart());
    resetDefaultMemoryAllocStats();
    if (mode->mC_needOverlay != 0) {
        if (portOverlayCd()) {
            /* XG_OVERLAY_CD: the PS1's load, decompression and waits, into
             * scratch (the overlay is linked in); the compressed file's heap
             * block goes with the PS1's second resetHeap. */
            void* overlay = loadGameModeOverlay(bootMode);
            waitReadCompletion(0);
            if (overlay != NULL) {
                decompress(overlay, portOverlayScratch(2, *(s32*)overlay));
            }
            DrawSync(0);
            VSync(0);
            EnterCriticalSection();
            DrawSync(0);
            VSync(0);
            FlushCache();
            ExitCriticalSection();
            resetHeap((HeapBlock*)portHeapStart());
        }
    }
    /* Every mode, the kernel menu's and the menu's too: the slowdown
     * replay's mode mark, then the overlay's fresh data (memory.c). */
    portResetGameMode(bootMode);
    pcHeapResetRetail(portRetailHeapStart((s32)(mode - gameModes)));
    resetHeapTraceState();
    resetInputs();
    setGameMode(0);
    /* What the screen shows, for the wide picture (include/pc_wide.h); the
     * boot modes are its kinds. */
    pcWideSetMode((s32)(mode - gameModes));
    /* The mode's frames start from zeros below this one: no leftovers of
     * what ran before at that depth (a rewind capture's park among them)
     * in the slots they never write (port/src/system/gamestack.h). */
    gameStackScrub();
    mode->m0_entryPoint();
    bootGame(0);
}

/* Dump RAM 0x80000000-0x80200000 to the PC file c:\core (DTL dev kit). */
void dumpRamToPcFile(void) {
    s32 fd;

    PCinit();
    fd = PCcreat("c:\\core", 0);
    PCwrite(fd, (void*)0x80000000, 0x200000);
    PCclose(fd);
}

void resetDefaultMemoryAllocStats(void) {
    resetMemoryAllocStats(10, 0);
}

/* checkSoftReboot: L1+R1+SELECT+START */
void checkSoftReboot(void) {
    /* The PC also resets into a start the extras asked for (their cutscene
     * replay: port/src/system/options.c), as the player's combination would. */
    if (newPadButtonForScripts == 0x90C || portStartPending()) {
        softReboot();
    }
}

/* softReboot: shut the hardware down and re-enter the boot entry point. */
void softReboot(void) {
    SwExitCriticalSection();
    ResetGraph(0);
    shutdownCDAndFileSystem();
    shutdownSoundSystem();
    SpuQuit();
    setVsyncUserCallback(0);
    DrawSyncCallback(0);
    VSyncCallback(0);
    CdFlush();
    StopPAD();
    SwEnterCriticalSection();
    startKernel();
}

/* Boot logo: decompress, upload, fade in, hold ~110 frames, fade out. */
/* US only: the JP release has no boot logo here. */
#if !VERSION_JP
void showBootLogo(void) {
    DRAWENV drawEnv;
    DISPENV dispEnv;
    SPRT sprt;
    RECT rect;
    u8* data;
    s32 i;

    setHeapUser(6);
    data = mallocAndDecompress(compressedBootLogo, 1);
    rect.y = 0xF0;
    rect.w = 0x10;
    rect.x = 0;
    rect.h = 1;
    LoadImage(&rect, data + 0x14);
    rect.x = 0x280;
    rect.y = 0;
    rect.w = 0x40;
    rect.h = 0x30;
    LoadImage(&rect, data + 0x40);
    setlen(&sprt, 4);
    sprt.code = 0x64;
    sprt.x0 = 0x20;
    sprt.y0 = 0x58;
    sprt.v0 = 0;
    sprt.u0 = 0;
    sprt.w = 0x100;
    sprt.h = 0x30;
    sprt.clut = GetClut(0, 0xF0);
    SetDefDrawEnv(&drawEnv, 0, 0, 0x140, 0xE0);
    SetDefDispEnv(&dispEnv, 0, 0, 0x140, 0xE0);
    PutDrawEnv(&drawEnv);
    PutDispEnv(&dispEnv);
    DrawSync(0);
    for (i = 0; i < 0x80; i += 8) {
        sprt.r0 = i;
        sprt.g0 = i;
        sprt.b0 = i;
        DrawPrim(&sprt);
        VSync(0);
    }
    for (i = 0x6D; i != -1; i--) {
        VSync(0);
    }
    for (i = 0x80; i >= 0; i -= 8) {
        sprt.r0 = i;
        sprt.g0 = i;
        sprt.b0 = i;
        DrawPrim(&sprt);
        VSync(0);
    }
    freeHeap(data);
}
#endif

/* System error count. */
s32 systemErrorCount = 0;

/* System error messages, indexed by errNo - 0x80. */
char* systemErrorMessages[] = {
    "LsKernel:Program Not Defined",
    "LsKernel:PC File Not Found",
    "LsGetMem:Memory Not Enough",
    "LsFreeMem:Can't Release NULL Pointer",
    "LsGetMem:MCB Broken",
#if !VERSION_JP
    "LsFreeMem:This ptr isn't MCB",
#endif
};

/* System error screen: log MCBs to the PC (or red screen + hang on CD), then show the error forever. */
void showSystemError(s32 errNo, s32 from) {
    DRAWENV drawEnv[2];
    DISPENV dispEnv[2];
    RECT rect;
    RECT unused;
    s32 frame;
    s32 ptr;
    s32 size;

    frame = 0;
    if (runningOnDTL != 0 && runningOnDTL != -1) {
        dumpHeapToPcFile("c:\\lserrmem.txt");
    } else {
        rect.x = 0;
        rect.y = 0;
        rect.w = 0x280;
        rect.h = 0x1E0;
        ClearImage(&rect, 0xFF, 0, 0);
        while (1) {
        }
    }
    forceFreeAllHeap();
    SetDefDrawEnv(&drawEnv[0], 0, 0, 0x180, 0xF0);
    SetDefDispEnv(&dispEnv[0], 0, 0xF0, 0x180, 0xF0);
    SetDefDrawEnv(&drawEnv[1], 0, 0xF0, 0x180, 0xF0);
    SetDefDispEnv(&dispEnv[1], 0, 0, 0x180, 0xF0);
    DrawSyncCallback(0);
    setVsyncUserCallback(0);
    initDebugFont(0x10, 0x10, 0x120, 0xF0, 0x1F4, 0, 0x3C0, 0x100, 0x3C0, 0x1FF, 0);
    drawEnv[1].isbg = 1;
    drawEnv[0].isbg = 1;
    drawEnv[0].r0 = 0;
    drawEnv[0].g0 = 0;
    drawEnv[0].b0 = 0;
    drawEnv[1].r0 = 0;
    drawEnv[1].g0 = 0;
    drawEnv[1].b0 = 0;
    systemErrorCount++;
    SetDispMask(1);
loop:
        PutDrawEnv((frame & 1) ? &drawEnv[0] : &drawEnv[1]);
        PutDispEnv((frame & 1) ? &dispEnv[0] : &dispEnv[1]);
        printDebugTextOnScreen(0);
        logToScreen("System Error No %d\n", errNo);
        logToScreen("From  %08x\n", from);
        logToScreen("Count %d\n", systemErrorCount);
        logToScreen("Frame %d\n", frame);
#if VERSION_JP
        logToScreen("ErrMemFile -> c:\\lserrmem.txt\n");
#else
        logToScreen("MCBlog -> c:\\lserrmem.txt\n");
#endif
        logToScreen("\n");
        if (errNo & 0x80) {
            logToScreen("%s\n", systemErrorMessages[errNo - 0x80]);
            if (errNo == 0x82) {
                getLastAllocRequest(&ptr, &size);
                logToScreen("Program From %08x\n", ptr);
                logToScreen("Failure Size %d (%xh) byte \n", size, size);
            }
#if !VERSION_JP
            if (errNo == 0x85) {
                getLastAllocRequest(&ptr, &size);
                logToScreen("Program From %08x\n", ptr);
                logToScreen("Failure Pointer %p\n", size);
            }
#endif
        }
        VSync(0);
        frame++;
        goto loop;
}

/* initKernelMenuDrawContext */
void initKernelMenuDrawContext(s32 index) {
    KernelDisplayContext* ctx = &kernelMenuDrawEnvDoubleBuffer[index];

    ctx->m0_drawEnv.isbg = 1;
    ctx->m0_drawEnv.dtd = 1;
    ctx->m0_drawEnv.r0 = 0;
    ctx->m0_drawEnv.g0 = 0;
    ctx->m0_drawEnv.b0 = 0x20;
    SetPolyF3(&ctx->m74_cursor);
    ctx->m74_cursor.r0 = 0xFF;
    ctx->m74_cursor.g0 = 0xFF;
    ctx->m74_cursor.b0 = 0xFF;
}

/* setupKernelMenuDraw */
void setupKernelMenuDraw(void) {
    /* Never used, but retail's frame has these 8 extra bytes (0x48, not 0x40). */
    RECT unused;

    resetMemoryAllocStats(6, 0);
    initDebugFont(8, 0x10, 0x170, 0x1E0, 1000, 1, 0x3C0, 0x100, 0x3C0, 0x1FF, 0);
    SetDefDrawEnv(&kernelMenuDrawEnvDoubleBuffer[0].m0_drawEnv, 0, 0, 0x140, 0xE0);
    SetDefDrawEnv(&kernelMenuDrawEnvDoubleBuffer[1].m0_drawEnv, 0, 0xF0, 0x140, 0xE0);
    SetDefDispEnv(&kernelMenuDrawEnvDoubleBuffer[0].m5C_dispEnv, 0, 0xF0, 0x140, 0xE0);
    SetDefDispEnv(&kernelMenuDrawEnvDoubleBuffer[1].m5C_dispEnv, 0, 0, 0x140, 0xE0);
    initKernelMenuDrawContext(0);
    initKernelMenuDrawContext(1);
}

/* Kernel menu cursor. */
s32 kernelMenuCursor = 0;

/* Programmer of each kernel menu entry (Field .. Movie); unused. */
char* kernelMenuProgrammers[] = {
    "YOSHII",
    "HIGUCHI,MIYAGAWA,MASAKI",
    "KAZUMI",
    "SUGIMOTO",
    "HIGUCHI",
    "MASAKI",
};

void printKernelMenu(void) {
    char timeString[24];

    if (newPadButtonForField & 0x1000) {
        if (--kernelMenuCursor < 0) {
            kernelMenuCursor = 5;
        }
    }
    if (newPadButtonForField & 0x4000) {
        if (++kernelMenuCursor > 5) {
            kernelMenuCursor = 0;
        }
    }
    if (newPadButtonForDialogs & 0x20) {
        setGameMode(kernelMenuCursor + 1);
        kernelMenuRunning = 0;
    }
    sprintf(timeString, "%02d:%02d:%02d", g_playTimeHours, g_playTimeMinutes, g_playTimeSeconds);
    logToScreen(" XENOGEARS Kernel MENU\n  %s %s MODE\n\n", timeString, runningOnDTL ? "PC HDD" : "CD EMU");
    logToScreen("    Field\n    Battle\n    Worldmap\n    Battling\n    Menu\n    Movie\n\n");
    *(s32*)&gKernelDisplayContext->m74_cursor.x0 = ((kernelMenuCursor * 8 + 0x28) << 16) | 0x20;
    *(s32*)&gKernelDisplayContext->m74_cursor.x1 = ((kernelMenuCursor * 8 + 0x2C) << 16) | 0x27;
    *(s32*)&gKernelDisplayContext->m74_cursor.x2 = ((kernelMenuCursor * 8 + 0x30) << 16) | 0x20;
}

/* kernelBootMenu */
void kernelBootMenu(void) {
    OTag* ot;

    setupKernelMenuDraw();
    kernelMenuRunning = 1;
    kernelMenuOddOrEven = 0;
    do {
        do {
            kernelMenuFrameCount++;
            kernelMenuOddOrEven = kernelMenuFrameCount & 1;
            gKernelDisplayContext = &kernelMenuDrawEnvDoubleBuffer[kernelMenuOddOrEven];
            ot = &gKernelDisplayContext->m70_ot;
            TermPrim(ot);
            printDebugTextOnScreen(ot);
            printKernelMenu();
            AddPrim(ot, &gKernelDisplayContext->m74_cursor);
            DrawSync(0);
            VSync(0);
            PutDrawEnv(&gKernelDisplayContext->m0_drawEnv);
            PutDispEnv(&gKernelDisplayContext->m5C_dispEnv);
            DrawOTag(ot);
        } while (kernelMenuRunning != 0);
    } while (kernelMenuOddOrEven == 0);
    DrawSync(0);
    bootGame(0);
}

/* Game of Life: allocate the tile buffers and clear the 28x40 grids. */
void lifeGameInit(void) {
    s32 row;
    s32 col;

    lifeGameTiles[0] = allocateHeap(1120 * (s32)sizeof(KernelTile), 1);
    lifeGameTiles[1] = allocateHeap(1120 * (s32)sizeof(KernelTile), 1);
    lifeGameCells = allocateHeap(PC_HEAP_BYTES(0x460), 1);
    lifeGameNeighbourCounts = allocateHeap(PC_HEAP_BYTES(0x460), 1);
    for (row = 0; row < 28; row++) {
        for (col = 0; col < 40; col++) {
            lifeGameCells[row * 40 + col] = 0;
            lifeGameNeighbourCounts[row * 40 + col] = 0;
        }
    }
}

/* Game of Life: count one neighbour at (row, col), wrapping at the edges. */
void lifeGameAddNeighbour(s32 row, s32 col) {
    if (row < 0) {
        row = 28;
    }
    if (row > 28) {
        row = 0;
    }
    if (col < 0) {
        col = 40;
    }
    if (col > 40) {
        col = 0;
    }
    lifeGameNeighbourCounts[row * 40 + col]++;
}

/* Game of Life: draw live cells as TILE_8s, step one generation, reseed. */
void lifeGameStep(OTag* ot) {
    KernelTile* tile;
    s32 count;
    s32 row;
    s32 col;

    count = 0;
    tile = lifeGameTiles[kernelMenuOddOrEven];
    for (row = 0; row < 28; row++) {
        for (col = 0; col < 40; col++) {
            if (lifeGameCells[row * 40 + col] != 0) {
                setlen(tile, 2);
                tile->m4_rgbc = 0x70280000;
                tile->m8_xy = (col << 3) | (row << 19);
                AddPrim(ot, tile);
                tile++;
                count++;
                lifeGameAddNeighbour(row - 1, col - 1);
                lifeGameAddNeighbour(row - 1, col);
                lifeGameAddNeighbour(row - 1, col + 1);
                lifeGameAddNeighbour(row, col - 1);
                lifeGameAddNeighbour(row, col + 1);
                lifeGameAddNeighbour(row + 1, col - 1);
                lifeGameAddNeighbour(row + 1, col);
                lifeGameAddNeighbour(row + 1, col + 1);
            }
        }
    }
    for (row = 0; row < 0x460; row++) {
        if (lifeGameNeighbourCounts[row] != 2) {
            lifeGameCells[row] = lifeGameNeighbourCounts[row] == 3;
        }
        lifeGameNeighbourCounts[row] = 0;
    }
    if (count < 20) {
        count = 0;
        row = rand() % 28;
        col = rand() % 40;
        while (count < 20) {
            row += rand() % 3 - 1;
            col += rand() % 3 - 1;
            if (row < 0) {
                row = 28;
            }
            if (row > 28) {
                row = 0;
            }
            if (col < 0) {
                col = 40;
            }
            if (col > 40) {
                col = 0;
            }
            count++;
            lifeGameCells[row * 40 + col] = 1;
        }
    }
    row = rand() % 28;
    col = rand() % 40;
    lifeGameAddNeighbour(row - 1, col - 1);
    lifeGameAddNeighbour(row - 1, col);
    lifeGameAddNeighbour(row - 1, col + 1);
    lifeGameAddNeighbour(row, col - 1);
    lifeGameAddNeighbour(row, col + 1);
    lifeGameAddNeighbour(row + 1, col - 1);
    lifeGameAddNeighbour(row + 1, col);
    lifeGameAddNeighbour(row + 1, col + 1);
}

s32 D_8004F2F4 = 0;
s32 fieldBootedOnce = 0;
SoundInstance* savedMusicInstance = NULL;
s32 movieScrollingTextEnabled = 0;
s32 worldmapSoundFilesPendingCount = 0;
s32 fieldMusicLoadPending = -1;
s32 fieldScriptEntityAlreadyInitialized = 0;
s32 D_8004F310 = 0;
s32 D_8004F314 = 0;
s32 fieldScriptTimerTicks = 0;
s32 typeOfPlayableCharacterLoaded = 0;
s32 fieldRequestedGears = 0;
s32 currentlyPlayingMusic = 0xFF;
s32 fieldScriptTimerMode = 0xFF;
s32 loadFieldSeqVar0 = -1;
s32 currentFieldId0 = -1;
s32 currentFieldId1 = -1;
s32 currentlyLoadedMusic = -1;
s32 currentlyLoadedWave = -1;
s32 musicVar1 = -1;
s32 compassGraphicDataLoaded = 0;
s32 updateMusicState2Var2 = 0;
s32 fieldMapNumber = -1;
s32 g_menuOpenCount = 0;
s32 musicVar2 = 0;
s32 updateMusicState2Var1 = 0;
s32 needStartMusicInstanceSub0 = 0;
s32 needReloadWds = 0;
s32 playMusicSubVar2 = 1;
s32 playMusicSubVar0 = 0;
s32 updateMusicState2Var3 = 0;
s32 mechaOverlayLoadingMode = 0;
s32 characterOrGearsLoadingInProgress = 0;
s32 fieldCompassVar = 0;
s32 disableCharacterShadowsRendering = 0;
s32 disableMechaRendering = 0;
s16 playWorldmapMusic = 0;

/* Boot-time reset of kernel globals (called once from main). */
void initKernelGlobals(void) {
    s32 i;

    playMusicSubVar2 = 1;
    fieldScriptTimerMode = 0xFF;
    currentlyPlayingMusic = 0xFF;
    savedMusicInstance = NULL;
    updateMusicState2Var3 = 0;
    fieldBootedOnce = 0;
    typeOfPlayableCharacterLoaded = 0;
    fieldRequestedGears = 0;
    D_8004F314 = 0;
    D_8004F310 = 0;
    fieldScriptEntityAlreadyInitialized = 0;
    mechaOverlayLoadingMode = 0;
    needStartMusicInstanceSub0 = 0;
    needReloadWds = 0;
    characterOrGearsLoadingInProgress = 0;
    updateMusicState2Var1 = 0;
    musicVar2 = 0;
    g_menuOpenCount = 0;
    D_8004F2F4 = 0;
    compassGraphicDataLoaded = 0;
    updateMusicState2Var2 = 0;
    worldmapSoundFilesPendingCount = 0;
    playMusicSubVar0 = 0;
    movieScrollingTextEnabled = 0;
    disableMechaRendering = 0;
    disableCharacterShadowsRendering = 0;
    fieldCompassVar = 0;
    battleEventReturnMode = 0;
    menuReturnState0 = 0;
    playWorldmapMusic = 0;
    fieldScriptTimerTicks = 0;
    currentFieldId1 = -1;
    fieldMapNumber = -1;
    currentlyLoadedWave = -1;
    currentlyLoadedMusic = -1;
    currentFieldId0 = -1;
    loadFieldSeqVar0 = -1;
    musicVar1 = -1;
    fieldMusicLoadPending = -1;
    for (i = 0; i < 3; i++) {
        asyncPartyCharacterLoadingTable[i] = 0;
        partyGearActorIds[i] = 0;
        partyFieldActorIds[i] = 0;
        currentParty[i] = 0;
    }
    for (i = 3; i >= 0; i--) {
        fieldWdsSlots[i] = NULL;
    }
}

void resetFieldScriptEntityInit(void) {
    fieldScriptEntityAlreadyInitialized = 0;
}

void resetFieldAndPreloadPartyFiles(void) {
    currentFieldId1 = -1;
    currentFieldId0 = -1;
    resetMemoryAllocStats(8, 0);
    setCurrentDirectory(4, 0);
    loadPartyFiles(1);
}

s32 getPartyMemberGearNum(s32 arg0) {
    return pKernelGameState->m26C_party[arg0].mA0_gearNum;
}

void waitCdIdle(void) {
    while (isCDBusy() != 0) {
        PORT_YIELD();
    }
    waitReadCompletion(0);
}

void startLoadingPartyCharacterFiles(void) {
    s32 i;
    s32 n;
    s32 j;

    pKernelGameState = &gameState;
    j = 0;
    for (i = 0; i < 3; i++) {
        u8 c;
        currentParty[i] = 0xFF;
        c = pKernelGameState->m1D34_currentParty[i];
        if (c != 0xFF) {
            currentParty[j++] = c;
        }
    }
    for (i = 0, n = 0; i < 3; i++) {
        if (currentParty[i] != 0xFF) {
            partyLoadingBatchCommands[n].m0_fileIndex = currentParty[i] + 5;
            asyncPartyCharacterLoadingTable[i] = currentParty[i];
            partyLoadingBatchCommands[n].m4_loadPtr = partyCharacterBuffersCompressed[n] = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(currentParty[i] + 5)), 0);
            flagAllocation(partyCharacterBuffersCompressed[n]);
            n++;
        }
    }
    partyLoadingBatchCommands[n].m4_loadPtr = 0;
    partyLoadingBatchCommands[n].m0_fileIndex = 0;
    batchStartLoadingFiles(partyLoadingBatchCommands, 0, 0);
    typeOfPlayableCharacterLoaded = 1;
}

void startLoadingPartyGearFiles(void) {
    s32 i;
    s32 n;
    s32 j;
    s32 gear;

    pKernelGameState = &gameState;
    j = 0;
    for (i = 0; i < 3; i++) {
        u8 c;
        currentParty[i] = 0xFF;
        c = pKernelGameState->m1D34_currentParty[i];
        if (c != 0xFF) {
            currentParty[j++] = c;
        }
    }
    for (i = 0, n = 0; i < 3; i++) {
        if (currentParty[i] != 0xFF) {
            gear = getPartyMemberGearNum(currentParty[i]);
            if (gear == 0xFF) {
                gear = 0;
            }
            gear += 0x10;
            partyLoadingBatchCommands[n].m0_fileIndex = gear + 5;
            asyncPartyCharacterLoadingTable[i] = gear;
            partyLoadingBatchCommands[n].m4_loadPtr = partyCharacterBuffersCompressed[n] = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(gear + 5)), 0);
            flagAllocation(partyCharacterBuffersCompressed[n]);
            n++;
        }
    }
    partyLoadingBatchCommands[n].m4_loadPtr = 0;
    partyLoadingBatchCommands[n].m0_fileIndex = 0;
    batchStartLoadingFiles(partyLoadingBatchCommands, 0, 0);
    typeOfPlayableCharacterLoaded = 2;
}

void updatePartyFileSet(void) {
    waitCdIdle();
    if (characterOrGearsLoadingInProgress == 1) {
        processLoadedPartyFiles();
        if (fieldScriptEntityAlreadyInitialized != 0) {
            return;
        }
    } else if (fieldScriptEntityAlreadyInitialized != 0) {
        loadPartyFiles(0);
        return;
    }
    if ((fieldMapNumber & 0xC000) == 0) {
        fieldRequestedGears = 0;
    } else {
        fieldRequestedGears = 1;
    }
    characterOrGearsLoadingInProgress = 0;
    if (fieldRequestedGears == 0) {
        if (typeOfPlayableCharacterLoaded != 1) {
            startLoadingPartyCharacterFiles();
            characterOrGearsLoadingInProgress = 1;
        }
    } else {
        if (typeOfPlayableCharacterLoaded != 2) {
            startLoadingPartyGearFiles();
            characterOrGearsLoadingInProgress = 1;
        }
    }
}

void loadPartyFiles(s32 arg0) {
    s32 i;
    s32 n;

    setHeapAllowFailure(1);
    if (characterOrGearsLoadingInProgress == 1) {
        processLoadedPartyFiles();
    }
    waitCdIdle();
    for (i = 0, n = 0; i < 3; i++) {
        if (asyncPartyCharacterLoadingTable[i] != 0xFF) {
            partyLoadingBatchCommands[n].m0_fileIndex = asyncPartyCharacterLoadingTable[i] + 5;
            partyLoadingBatchCommands[n].m4_loadPtr = partyCharacterBuffersCompressed[n] = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(asyncPartyCharacterLoadingTable[i] + 5)), 1);
            if (partyLoadingBatchCommands[n].m4_loadPtr == 0) {
                for (i = 0; i < n; i++) {
                    unflagAllocation(partyCharacterBuffersCompressed[i]);
                    freeHeap(partyCharacterBuffersCompressed[i]);
                }
                setHeapAllowFailure(0);
                return;
            }
            flagAllocation(partyCharacterBuffersCompressed[n]);
            n++;
        }
    }
    if (arg0 != 0) {
        partyLoadingBatchCommands[n].m4_loadPtr = compassGraphicDataStaging = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(0xA7)), 1);
        if (partyLoadingBatchCommands[n].m4_loadPtr != 0) {
            flagAllocation(partyLoadingBatchCommands[n].m4_loadPtr);
            partyLoadingBatchCommands[n].m0_fileIndex = 0xA7;
            n++;
            compassGraphicDataLoaded = 1;
        }
        partyLoadingBatchCommands[n].m4_loadPtr = loadFieldSeqVar1 = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(0xA8)), 1);
        if (partyLoadingBatchCommands[n].m4_loadPtr != 0) {
            flagAllocation(partyLoadingBatchCommands[n].m4_loadPtr);
            partyLoadingBatchCommands[n].m0_fileIndex = 0xA8;
            n++;
            loadFieldSeqVar0 = 0;
        }
    }
    partyLoadingBatchCommands[n].m4_loadPtr = 0;
    partyLoadingBatchCommands[n].m0_fileIndex = 0;
    batchStartLoadingFiles(partyLoadingBatchCommands, 0, 0);
    characterOrGearsLoadingInProgress = 1;
    setHeapAllowFailure(0);
}

void processLoadedPartyFiles(void) {
    s32 i;

    if (characterOrGearsLoadingInProgress != 0) {
        waitCdIdle();
        for (i = 0; i < 3; i++) {
            unflagAllocation(partyCharacterBuffers[i]);
            if (currentParty[i] != 0xFF) {
                unflagAllocation(partyCharacterBuffersCompressed[i]);
                decompress(partyCharacterBuffersCompressed[i], partyCharacterBuffers[i]);
                freeHeap(partyCharacterBuffersCompressed[i]);
            }
        }
        characterOrGearsLoadingInProgress = 0;
    }
}

s32 loadNewField(s32 arg0, s32 arg1) {
    if (currentFieldId1 == arg1 && currentFieldId0 == arg0) {
        return 0;
    }
    if (isCDBusy() == 0) {
        waitReadCompletion(0);
        if (currentFieldId1 != -1) {
            unflagAllocation(rawFieldBundle);
            freeHeap(rawFieldBundle);
        }
        loadRawFieldBundle(arg0);
        currentFieldId1 = arg1;
        currentFieldId0 = arg0;
    }
    return -1;
}

void loadRawFieldBundle(s32 arg0) {
    s32 fileIndex = arg0 + 0xB8;

    rawFieldBundleSize = getFileSizeAligned(fileIndex);
    rawFieldBundle = allocateHeap(PC_HEAP_BYTES(rawFieldBundleSize), 1);
    flagAllocation(rawFieldBundle);
    readFile(fileIndex, rawFieldBundle, 0, 0x80);
}

void reloadWdsIfNeeded(void) {
    if (needReloadWds == 1) {
        unloadWds(worldmapWdsResult);
        needReloadWds = 0;
    }
}

void updateNeedStartMusicInstanceSub0(void) {
    if (needStartMusicInstanceSub0 == 1) {
        startMusicInstanceSub0(pMusic);
        if (updateMusicState2Var2 == 0) {
            freeSoundInstance(pMusic);
        } else {
            savedMusicInstance = pMusic;
        }
        needStartMusicInstanceSub0 = 0;
        updateMusicState2Var2 = 0;
    }
}

void resetBattleAudioState(void) {
    if (updateMusicState2Var3 != 0) {
        updateNeedStartMusicInstanceSub0();
        reloadWdsIfNeeded();
    }
    currentlyLoadedWave = -1;
    currentlyLoadedMusic = -1;
    updateMusicState2Var3 = 0;
}

/* US only (empty). */
#if !VERSION_JP
void postBootLogoNop(void) {
}
#endif
