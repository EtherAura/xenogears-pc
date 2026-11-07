/*
 * Initial game state, battle loader, debug menu drawing/input.
 *
 * Provisional file 0x8001B970-0x8001C76C (main executable); bounds come from
 * compiler/assembler changes and may move. Built with GCC 2.6.0, ASPSX < 2.50.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "main/menu.h"
#include "main/dialog.h"
#include "main/debug_font.h"

/* debugMenuNames[7] ("Normal Menu", "Member Change", ...), in .data */
extern char* debugMenuNames[];
/* menuToEnter */
extern u8 menuToEnter;
/* g_menuOpenArg */
extern u8 g_menuOpenArg;
/* gameState.m1924_gold (gameState + 0x1924) */
/* menuSharedResources */
extern void* menuSharedResources;
extern void* mechaOverlayProbeBlock;
extern void* mechaOverlayMemoryBlock;
void drawMenuDebug(void);
void menu2_loadGame_entryPoint(void);
void partyChangeMenuEntryPoint(void);
void nameEntryMenuEntryPoint(void);
void shopMenuEntryPoint(void);
void gearShopMenuEntryPoint(void);
void setGameMode(s32);
void bootGame(s32);
/* gMenuContext */
extern u8 useDebugMenuList;
void setupScreenGeometryForMenu(void);
void setupMenuContextTransforms(void);
void processDebugMenuForMenuList(void);
void loadSequence(SeqFile*);
void playSoundEffect(s32);
void loadBattleMechaAndEnvironment(s32, s32, void*, void*, void*);
s32 getInputOverflowed(void);
void resetInputs(void);
s32 loadInputFromVSyncBuffer(void);
void dumpHeapBlocks(s32, s32, s32, s32);
void loadInitialGameState(void);
void setupMenuContext1D4(MenuDrawContext* arg0);
void drawMenuDebugUpdateInputs(void);
/* Battle transition sound effects, 3 per transition (0xFF = none). */
u8 battleTransitionSoundEffects[] = {
    1, 2, 0xFF, 3, 4, 0xFF, 5, 6, 7, 8, 9, 10, 1, 2, 0xFF, 1, 2, 0xFF,
};
/* Debug menu entry names (fixed 256-byte buffers). */
char debugMenuNameBuffers[7][0x100] = {
    "Normal Menu", "Member Change", "Load Game", "Enter Name", "Shop", "Robo", "CD Change",
};
char* debugMenuNames[] = {
    debugMenuNameBuffers[0], debugMenuNameBuffers[1], debugMenuNameBuffers[2], debugMenuNameBuffers[3],
    debugMenuNameBuffers[4], debugMenuNameBuffers[5], debugMenuNameBuffers[6],
};
extern s32* pRunningOnDTL;
extern u8 battleLoadDataVar0_raw[];
extern u8 battleLoadDataVar2[];
extern u8 battleLoadDataVar1[];
extern u16 newPadButtonForField;
extern s16 battleScriptGameStateVars[20];
extern MenuContext* gMenuContext;
extern LoadingBatchCommand battleLoadingCommands[4];
extern u8 D_8006F9DE;
u8 D_8005946C;
u8 battleInitVar0;
void* battleLoaderProbeBlock;
void* battleLoaderMemoryBlock;
u8 savedMenuSelectedEntry;
u8 dialogWindowColor[3];
u8 battleInitVar1;
u8 battleTransitionEffect;
s32 dialogWindowBlendMode;
void* battleCharacterConfigFile;
SeqFile* battleStartSeq;

/* What dst holds on the PS1 before the first name: words earlier calls left
 * on its stack, the same at every boot (Beetle: a cold boot of disc 1 or 2
 * and after a soft reset). The first name overwrites the first 4 bytes, the
 * last 4 are never copied. */
#if VERSION_JP
static const u8 loadInitialGameStateStack[0x18] = {
    0, 0, 0, 0, 0x0C, 0x87, 0x02, 0x80, 0x07, 0, 0, 0, 0x28, 0xF3, 0x0A, 0x80, 0x14, 0x85, 0x02, 0x80, 0, 0, 0, 0,
};
#else
static const u8 loadInitialGameStateStack[0x18] = {
    0, 0, 0, 0, 0xFC, 0x88, 0x02, 0x80, 0x07, 0, 0, 0, 0x98, 0xFC, 0x0A, 0x80, 0x04, 0x87, 0x02, 0x80, 0, 0, 0, 0,
};
#endif

void loadInitialGameState(void) {
    u8 src[0x18];
    u8 dst[0x18];
    s32 i;
    s32 j;
    void* data;

    /* Each name gets all 0x14 bytes of dst, of which decodeCharacterCodes
     * writes only the name and its end: after it, what the longer names
     * before it left, and before those, the stack's. The PS1's stack words,
     * so the names' bytes are the PS1's on every platform and build (they
     * were host stack addresses: netplay hashes them). */
    for (j = 0; j < 0x18; j++) {
        dst[j] = loadInitialGameStateStack[j];
    }
    setCurrentDirectory(0x10, 0);
    resetMemoryAllocStats(2, 0);
    data = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(3)), 1);
    readFile(3, data, 0, 0x80);
    waitReadCompletion(0);
    memmove(&gameState, data, 0x2358);
    freeHeap(data);
    for (i = 0; i < 31; i++) {
        for (j = 0; j < 0x14; j += 2) {
            src[j] = gameState.m0_names[i][j];
            /* flat index on purpose: keeps this address out of the j giv */
            src[j + 1] = gameState.m0_names[0][i * 0x14 + j + 1];
            if (gameState.m0_names[i][j] == 0xF && gameState.m0_names[0][i * 0x14 + j + 1] == 0) {
                break;
            }
        }
        decodeCharacterCodes((u16*)src, dst, j / 2);
        for (j = 0; j < 0x14; j++) {
            gameState.m0_names[i][j] = dst[j];
        }
    }
    /* The table has 16 entries (saves keep 16): on the PS1 the last four
     * stores clear g_spuVolumes' m0_mask and m4_mvol. The game does not
     * notice: m4_mvol is recomputed before every use, and at the boot call no
     * volume change is pending (Beetle: the mask is clear), so the PC keeps
     * the objects apart (tools/pc_link.py SEPARATE, xg-bwn). */
    for (j = 0; j < 20; j++) {
        battleScriptGameStateVars[j] = 0;
    }
    savedMenuSelectedEntry = 6;
    battleInitVar0 = 0;
}

void startBattleLoadingDuringEffect2(void) {
    loadBattleMechaAndEnvironment(D_8006F9DE, 0, battleLoadDataVar0_raw, battleLoadDataVar1, battleLoadDataVar2);
}

void initGameState(void) {
    battleInitVar1 = 1;
    D_8005946C = 0;
    loadInitialGameState();
    waitReadCompletion(0);
    dialogWindowColor[0] = 0x88;
    dialogWindowColor[1] = 0x76;
    dialogWindowColor[2] = 0x54;
    dialogWindowBlendMode = 2;
}

void loadBattleLoader(void) {
    s32 i;
    u8 sfx;
    sptr loaderAddr;

    resetMemoryAllocStats(2, 0);
    setCurrentDirectory(0xC, 0);
    /* The battle loader is linked in, so no padding up to its link address:
     * token blocks keep battleMain's frees balanced. */
    battleLoaderProbeBlock = allocateHeap(PC_HEAP_BYTES(4), 1);
    /* Unlike the field overlay formula, this original request includes the
     * extra header: its reserved block starts eight bytes before the link. */
    battleLoaderMemoryBlock = allocateHeap(PC_HEAP_SIZED(4, pcHeapReservation(battleLoaderProbeBlock, 0x801E4000u - 8)), 1);
    battleStartSeq = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(2)), 1);
    battleCharacterConfigFile = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(3)), 1);
    battleLoadingCommands[0].m0_fileIndex = 2;
    battleLoadingCommands[0].m4_loadPtr = battleStartSeq;
    battleLoadingCommands[1].m0_fileIndex = 3;
    battleLoadingCommands[1].m4_loadPtr = battleCharacterConfigFile;
    /* Only the two data files are read: the loader starts as a fresh load. */
    battleLoadingCommands[2].m0_fileIndex = 0;
    battleLoadingCommands[2].m4_loadPtr = NULL;
    if (portOverlayCd()) { /* XG_OVERLAY_CD: the PS1's batch, the loader's file discarded */
        battleLoadingCommands[2].m0_fileIndex = 4;
        battleLoadingCommands[2].m4_loadPtr = portOverlayScratch(0, getFileSizeAligned(4));
    }
    portResetModule("battle_loader");
    battleLoadingCommands[3].m0_fileIndex = 0;
    battleLoadingCommands[3].m4_loadPtr = 0;
    batchStartLoadingFiles(battleLoadingCommands, 0, 0x80);
    /* Wait for the battle start sequence, the batch's first file. The PS1's
     * batch has three files (the loader's last); the PC's has two unless
     * XG_OVERLAY_CD reads the loader's file too. Without this the sequence
     * was parsed while still loading: garbage sound effects, and its loader
     * could write into the next heap block (the battle data table). */
    while (isCDBusy() == (portOverlayCd() ? 3 : 2)) {
        PORT_YIELD();
    }
    loadSequence(battleStartSeq);
    if (battleTransitionEffect != 4) {
        for (i = 0; i < 3;) {
            sfx = battleTransitionSoundEffects[battleTransitionEffect * 3 + i];
            i++;
            if (sfx != 0xFF) {
                playSoundEffect((battleStartSeq->u14.m14_id << 16) | sfx);
            }
        }
    }
}

int pcVersusRandom(int lo, int hi, int* out); /* port/src/game/battle/versus.c */
u8 randomInRange(s32 arg0, s32 arg1) {
    u8 lo = arg0;
    u8 hi = arg1;
    s32 range;

    if (lo == 0xFF) {
        return 0xFF;
    }
    if (hi == 0) {
        return 0;
    }
    if (lo == hi) {
        return lo;
    }
    {
        /* a versus battle's enemy turn: player 2's choice (port/src/game/battle/versus.c) */
        int v;
        if (pcVersusRandom(lo, hi, &v)) {
            return v;
        }
    }
    range = hi - lo;
    if (range >= 0xFF) {
        return rand() & 0xFF;
    }
    return lo + R3000_MOD(rand() & 0xFF, range + 1);
}

void setupMenuContext1D4(MenuDrawContext* arg0) {
    arg0->m0_drawEnv.dtd = 1;
    arg0->m5C_dispEnv.screen.y = 10;
    arg0->m5C_dispEnv.screen.w = 0x100;
    arg0->m0_drawEnv.isbg = 0;
    arg0->m0_drawEnv.r0 = 0;
    arg0->m0_drawEnv.g0 = 0;
    arg0->m0_drawEnv.b0 = 0;
    arg0->m5C_dispEnv.screen.x = 0;
    arg0->m5C_dispEnv.screen.h = 0xD8;
}

void setupScreenGeometryForMenu(void) {
    SetGeomOffset(0xA0, 0x70);
    SetGeomScreen(0x200);
    SetDefDispEnv(&gMenuContext->m6C_drawContexts[0].m5C_dispEnv, 0, 0xE0, 0x140, 0xE0);
    SetDefDrawEnv(&gMenuContext->m6C_drawContexts[0].m0_drawEnv, 0, 0, 0x140, 0xE0);
    SetDefDispEnv(&gMenuContext->m6C_drawContexts[1].m5C_dispEnv, 0, 0, 0x140, 0xE0);
    SetDefDrawEnv(&gMenuContext->m6C_drawContexts[1].m0_drawEnv, 0, 0xE0, 0x140, 0xE0);
    setupMenuContext1D4(&gMenuContext->m6C_drawContexts[0]);
    setupMenuContext1D4(&gMenuContext->m6C_drawContexts[1]);
}

void setupMenuContextTransforms(void) {
    MenuContext* ctx = gMenuContext;

    ctx->m1E0_menuTranslation.vz = 0x800;
    ctx->m220_menuTranslation2.vz = 0x800;
    ctx->m1D8_menuRotation.vz = 0;
    ctx->m1D8_menuRotation.vy = 0;
    ctx->m1D8_menuRotation.vx = 0;
    ctx->m1E0_menuTranslation.vy = 0;
    ctx->m1E0_menuTranslation.vx = 0;
    ctx->m218_menuRotation2.vz = 0;
    ctx->m218_menuRotation2.vy = 0;
    ctx->m218_menuRotation2.vx = 0;
    ctx->m220_menuTranslation2.vy = 0;
    ctx->m220_menuTranslation2.vx = 0;
    ctx->m2E8 = 1;
    ctx->m329_animationState = 0;
}

void drawMenuDebugUpdateInputs(void) {
    s32 sel = 8;
    u16 buttons;

    if (getInputOverflowed() != 0) {
        resetInputs();
    } else {
        while (loadInputFromVSyncBuffer() != 0) {
            buttons = newPadButtonForField;
            if (buttons & 0x2000) {
                sel = 0;
                break;
            }
            if (buttons & 0x4000) {
                sel = 1;
                break;
            }
            if (buttons & 0x8000) {
                sel = 2;
                break;
            }
            if (buttons & 0x1000) {
                sel = 3;
                break;
            }
            if (buttons & 0x20) {
                sel = 4;
                break;
            }
            if (buttons & 0x100) {
                sel = 0xC;
                gMenuContext->m1E94_showMcbLog = !gMenuContext->m1E94_showMcbLog;
                break;
            }
            if (buttons & 4) {
                if (gMenuContext->m1E95_mcbLogPage != 0) {
                    gMenuContext->m1E95_mcbLogPage--;
                }
                break;
            }
            if (buttons & 1) {
                gMenuContext->m1E95_mcbLogPage++;
                break;
            }
        }
    }
    gMenuContext->m325_menuButton = sel;
}

void drawMenuDebug(void) {
    MenuContext* ctx;

    drawMenuDebugUpdateInputs();
    ctx = gMenuContext;
    ctx->m1D4_currentDrawContext = (ctx->m1D4_currentDrawContext == &ctx->m6C_drawContexts[0]) ? &ctx->m6C_drawContexts[1] : &ctx->m6C_drawContexts[0];
    ctx->m308_oddOrEven = ctx->m308_oddOrEven == 0;
    ClearOTagR(ctx->m1D4_currentDrawContext->m70_ot, 0x10);
    if (*pRunningOnDTL != -1) {
        if (gMenuContext->m1E94_showMcbLog) {
            dumpHeapBlocks(3, gMenuContext->m1E95_mcbLogPage, 0xF, 0x80AC);
        }
        if (*pRunningOnDTL != -1) {
            printDebugTextOnScreen(gMenuContext->m1D4_currentDrawContext->m70_ot);
        }
    }
    DrawSync(0);
    VSync(0);
    PutDrawEnv(&gMenuContext->m1D4_currentDrawContext->m0_drawEnv);
    PutDispEnv(&gMenuContext->m1D4_currentDrawContext->m5C_dispEnv);
    DrawOTag(&gMenuContext->m1D4_currentDrawContext->m70_ot[15]);
}

/* Noah: processDebugMenuForMenuList. Debug menu picker (when useDebugMenuList),
 * then load and run the chosen menu overlay. */
void processDebugMenuForMenuList(void) {
    s32 menu = 0;
    s32 sub = 0;
    u8 stay = 1;
    void* p0;
    void* p1;

    if (useDebugMenuList != 0) {
        do {
            logToScreen("\n\n     Menu=(%d)%s\n", menu, debugMenuNames[menu]);
            if (menu < 4) {
                if (sub < 11) {
                    logToScreen("\n\n     Chr =(%d)\n", sub);
                } else {
                    logToScreen("\n\n     Robo =(%d)\n", sub - 11);
                }
            } else {
                logToScreen(menu != 6 ? "\n\n     ShopNo =(%d)\n" : "\n\n     CdNo =(%d)\n", sub);
            }
            switch (gMenuContext->m325_menuButton) {
            case 4:
                stay = 0;
                break;
            case 0:
                menu++;
                sub = 0;
                if (menu > 6) {
                    menu = 0;
                }
                break;
            case 2:
                menu--;
                sub = 0;
                if (menu < 0) {
                    menu = 6;
                }
                break;
            case 3:
                if (menu < 4) {
                    sub++;
                    if (sub > 30) {
                        sub = 0;
                    }
                } else if (menu != 6) {
                    sub++;
                } else {
                    sub = sub == 0;
                }
                break;
            case 1:
                sub--;
                if (sub < 0) {
                    if (menu < 4) {
                        sub = 30;
                    } else if (menu != 6) {
                        sub = 0xFF;
                    } else {
                        sub = sub == 0;
                    }
                }
                break;
            }
            drawMenuDebug();
        } while (stay);
        menuToEnter = menu;
        g_menuOpenArg = sub;
        gMenuContext->m6C_drawContexts[0].m0_drawEnv.isbg = 0;
        gMenuContext->m6C_drawContexts[1].m0_drawEnv.isbg = 0;
        SetDispMask(0);
        gMenuContext->m1D4_currentDrawContext = &gMenuContext->m6C_drawContexts[1];
        SetDispMask(1);
    }
    setCurrentDirectory(0x10, 0);
    if (useDebugMenuList != 0) {
        gameState.m1924_gold = 999999999;
        resetMemoryAllocStats(2, 0);
        menuSharedResources = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(1)), 0);
        readFile(1, menuSharedResources, 0, 0x80);
        waitReadCompletion(0);
        if (menuToEnter == 5) {
            setCurrentDirectory(4, 0);
            if (portOverlayCd()) { /* XG_OVERLAY_CD: the PS1's read, discarded */
                readFile(0x6B9, portOverlayScratch(1, getFileSizeAligned(0x6B9)), 0, 0x80);
            }
            portResetModule("field_mecha"); /* linked in: a fresh load resets its data */
            waitReadCompletion(0);
            setCurrentDirectory(0x10, 0);
            gearShopOrderingTables[0] = allocateHeap(4096 * (s32)sizeof(OTag), 0);
            gearShopOrderingTables[1] = allocateHeap(4096 * (s32)sizeof(OTag), 0);
        }
        {
            /* The menu overlay (dir 0x10, file menu + 5) is linked in: a fresh
             * load resets its data. */
            static const char* const overlays[7] = {
                "menu", "menu_party", "menu", "menu_name_entry", "menu_shop", "menu_gear_shop", "menu",
            };
            if (portOverlayCd()) { /* XG_OVERLAY_CD: the PS1's read, discarded */
                readFile(menuToEnter + 5, portOverlayScratch(0, getFileSizeAligned(menuToEnter + 5)), 0, 0x80);
            }
            portResetModule(overlays[menuToEnter]);
        }
        waitReadCompletion(0);
    }
    setCurrentDirectory(0x10, 0);
    switch (menuToEnter) {
    case 0:
        menu2_loadGame_entryPoint();
        break;
    case 1:
        partyChangeMenuEntryPoint();
        break;
    case 3:
        nameEntryMenuEntryPoint();
        break;
    case 4:
        shopMenuEntryPoint();
        break;
    case 2:
    case 6:
        menu2_loadGame_entryPoint();
        setGameMode(1);
        break;
    case 5:
        gearShopMenuEntryPoint();
        break;
    }
    if (useDebugMenuList != 0) {
        if (menuToEnter == 5) {
            freeHeap(gearShopOrderingTables[0]);
            freeHeap(gearShopOrderingTables[1]);
        }
        useDebugMenuList = 1;
        bootGame(0);
    }
}

void enterMenu(void) {
    gMenuContext = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext), 0x1e98), 0);
    bzero(gMenuContext, sizeof(MenuContext));
    gMenuContext->m325_menuButton = 8;
    resetMemoryAllocStats(2, 0);
    gMenuContext->m1D4_currentDrawContext = &gMenuContext->m6C_drawContexts[1];
    gMenuContext->m1E94_showMcbLog = 0;
    gMenuContext->m1E95_mcbLogPage = 1;
    gMenuContext->m2D8_frameCounter = 0;
    gMenuContext->m327_drawEnabled = 0;
    setupScreenGeometryForMenu();
    if (useDebugMenuList != 0) {
        gMenuContext->m6C_drawContexts[0].m0_drawEnv.isbg = 1;
        gMenuContext->m6C_drawContexts[1].m0_drawEnv.isbg = 1;
    }
    setupMenuContextTransforms();
    VSync(0);
    PutDrawEnv(&gMenuContext->m6C_drawContexts[0].m0_drawEnv);
    PutDrawEnv(&gMenuContext->m6C_drawContexts[1].m0_drawEnv);
    PutDispEnv(&gMenuContext->m6C_drawContexts[0].m5C_dispEnv);
    PutDispEnv(&gMenuContext->m6C_drawContexts[1].m5C_dispEnv);
    SetDispMask(1);
    processDebugMenuForMenuList();
    useDebugMenuList = 1;
}
