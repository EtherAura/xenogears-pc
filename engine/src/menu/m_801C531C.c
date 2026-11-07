/*
 * Main menu overlay (disc 01/11, loaded at 0x801C5000; Noah menus/menuHandler.cpp):
 * the submenu dispatcher, the main menu loop and its context setup/teardown, the load
 * game entry point. Provisional file 0x801C531C-0x801C65F4; owns .rodata
 * 0x801C5000-0x801C5028 (the dispatcher's jump table). gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "menu/menu.h"
#include "main/memory.h"
#include "main/filesystem.h"

extern u8 menuReturnState0;
extern u8 processLoadSaveMenuVar0;
extern s32 mainMenuConfig3[]; /* label x offsets (updateMenuSelection2) */
extern s32 saveGameMainMenuConfig4[];
extern u32 saveGameMainMenuConfig[][2];
extern u8 saveGameMainMenuConfig2[8]; /* read as 8 labels: [4..7] is saveGameMainMenuConfig6 */

void loadInitialGameState(void);
s32 getCurrentDiscNumber(void);
void stopSequence(void*);
void unloadSequence(void*);
void initMenuContext32C(u8 arg0);
void initMenuContext33C(u8 arg0);
void initMenuContext350MainMenu(u8 arg0);
void initMenuContext354(u8 arg0);
void initMenuContext330(u8 arg0);
void initMenuContext340_goldOwned(u8 arg0);
void initMenuContext344_playTime(u8 arg0);
void initMenuContext348(u8 arg0);
void initMenuContext39C(u8 arg0);

/* Noah menu2_executeMainMenuSelection */
u8 menu2_executeMainMenuSelection(u8 arg0) {
    u8 ret;
    u8 flag;

    menuVarUnk1 = 1;
    ret = 1;
    flag = 1;
    switch (gMenuContext->m336_menuSelectedEntry + arg0) {
    case 7:
        flag = processSoundMenu();
        break;
    case 8:
        if (processLoadSaveMenu(1, 0)) {
            menuReturnState0 = 2;
            ret = 0;
        }
        break;
    case 1:
        flag = processLoadSaveMenu(0, processLoadSaveMenuVar0);
        break;
    case 2:
        flag = openGearMenu();
        break;
    case 3:
        flag = openEtherMenu(gMenuContext->m4DC_currentlySelectedCharacter, 1);
        break;
    case 4:
        flag = openItemMenu();
        break;
    case 5:
        flag = openEquipMenu(gMenuContext->m4DC_currentlySelectedCharacter, 1);
        break;
    case 6:
        flag = openStatusMenu();
        break;
    case 9:
        loadInitialGameState();
    case 0:
        flag = 0;
        ret = 0;
        break;
    }
    gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 0;
    if (flag) {
        processLoadSaveMenuSub1Alt2();
        if (menuToEnter == 0) {
            processLoadSaveMenuSub2(1, 0);
        } else if (menuToEnter == 2) {
            j_setupMenuContext4E0(8, gMenuContext->m6E0_mainMenuLabels, saveGameMainMenuConfig2, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
            gMenuContext->m348_cursor->m15B_selectionWidth = 0x4C;
        }
    }
    cleanupMainMenuSelection(arg0);
    freeCharacterSwitchArrows();
    gMenuContext->m350_mainMenu->m1192_dimmed = 0;
    gMenuContext->m350_mainMenu->m1193_appliedDimmed = 1;
    gMenuContext->m33C_drawFlags->m4_drawCursor = 1;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 1;
    gMenuContext->m337_previousMenuSelectedEntry = 0xFF;
    gMenuContext->m33C_drawFlags->mA_354Enabled = 0;
    menuVarUnk0 = 1;
    return ret;
}

/* Noah mainMenuExecute */
void mainMenuExecute(void) {
    u8 cont;
    u8 ok;

    cont = 1;
    do {
        menuDraw();
        switch (gMenuContext->m325_menuButton) {
        case 4:
            ok = 1;
            if (gMenuContext->m336_menuSelectedEntry == 2 && gMenuContext->m33B_numGears == 0) {
                ok = 0;
                playMenuSoundEffect(4);
            }
            if (ok) {
                gMenuContext->m350_mainMenu->m1192_dimmed = 1;
                resetCursorState();
                countField33C_C(8, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
                cont = menu2_executeMainMenuSelection(0);
            }
            break;
        case 5:
            cont = 0;
            break;
        case 1:
            if (gMenuContext->m336_menuSelectedEntry > 0) {
                gMenuContext->m336_menuSelectedEntry--;
            } else {
                gMenuContext->m336_menuSelectedEntry = 6;
            }
            break;
        case 3:
            if (++gMenuContext->m336_menuSelectedEntry >= 7) {
                gMenuContext->m336_menuSelectedEntry = 0;
            }
            break;
        }
        if (gMenuContext->m336_menuSelectedEntry != gMenuContext->m337_previousMenuSelectedEntry) {
            updateMenuSelection(7, gMenuContext->m336_menuSelectedEntry, mainMenuConfig);
            updateMenuSelection2(8, gMenuContext->m6E0_mainMenuLabels, mainMenuConfig2, mainMenuConfig3, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels,
                          gMenuContext->m336_menuSelectedEntry, 0, 0);
            gMenuContext->m337_previousMenuSelectedEntry = gMenuContext->m336_menuSelectedEntry;
        }
    } while (cont);
}

/* menuToEnter 6: memory card check then load menu */
void cdChangeMenuExecute(void) {
    u8 flag;

    g_menuOpenArg = 1;
    menuVarUnk1 = 0;
    flag = 1;
    processLoadSaveMenuSub1();
    while (gMenuContext->m329_animationState != 0) {
        menuDraw();
    }
    setupDraw428(0);
    if ((u8)askYesNoQuestion(0x7D, 0xFF, 1) || yesNoPromptCancelled) {
        /* retail tests this result as an int (no andi): called through an int view */
        if (((s32 (*)(s32, s32, s32))askYesNoQuestion)(0x80, 0xFF, 1)) {
            flag = 0;
        }
    }
    disableDraw428();
    if (flag) {
        processLoadSaveMenuVar0 = discChangeSaveActive = 1;
        menu2_executeMainMenuSelection(0);
        processLoadSaveMenuVar0 = 0;
        discChangeSaveActive = 0;
    }
    gMenuContext->m33C_drawFlags->m4_drawCursor = 0;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 0;
    checkCurrentDiscNumber(g_menuOpenArg);
}

/* Noah loadSaveGameMenuExecute */
void loadSaveGameMenuExecute(void);
void loadSaveGameMenuExecute(void) {
    u8 cont;

    cont = 1;
#if VERSION_JP
    gMenuContext->m336_menuSelectedEntry = 1;
#endif
    setupMainMenuWithConfig(4, saveGameMainMenuConfig);
    j_setupMenuContext4E0(8, gMenuContext->m6E0_mainMenuLabels, saveGameMainMenuConfig2, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
    gMenuContext->m2D8_frameCounter = 0;
    do {
        menuDraw();
        switch (gMenuContext->m325_menuButton) {
        case 4:
            gMenuContext->m350_mainMenu->m1192_dimmed = 1;
            resetCursorState();
            countField33C_C(8, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
            gMenuContext->m348_cursor->m15B_selectionWidth = 0x40;
            cont = menu2_executeMainMenuSelection(7);
            menuVarUnk0 = 0;
            gMenuContext->m2D8_frameCounter = 0;
            break;
        case 1:
            if (gMenuContext->m336_menuSelectedEntry > 0) {
                gMenuContext->m336_menuSelectedEntry--;
            } else {
                gMenuContext->m336_menuSelectedEntry = 2;
            }
            gMenuContext->m2D8_frameCounter = 0;
            break;
        case 3:
            if (++gMenuContext->m336_menuSelectedEntry >= 3) {
                gMenuContext->m336_menuSelectedEntry = 0;
            }
            gMenuContext->m2D8_frameCounter = 0;
            break;
        }
        if (gMenuContext->m336_menuSelectedEntry != gMenuContext->m337_previousMenuSelectedEntry) {
            updateMenuSelection(3, gMenuContext->m336_menuSelectedEntry, saveGameMainMenuConfig);
            updateMenuSelection2(8, gMenuContext->m6E0_mainMenuLabels, saveGameMainMenuConfig2, saveGameMainMenuConfig4, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels,
                          gMenuContext->m336_menuSelectedEntry, 0, 0);
            gMenuContext->m337_previousMenuSelectedEntry = gMenuContext->m336_menuSelectedEntry;
        }
        if (getCurrentDiscNumber() == 1 && gMenuContext->m2D8_frameCounter > 600) {
            cont = 0;
            menuReturnState0 = 1;
        }
    } while (cont);
    countField33C_C(8, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
}

/* Noah initMenuContext32C */
void initMenuContext32C(u8 arg0) {
    if (arg0) {
        gMenuContext->m32C_memoryCardContext = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_32C), 0x5034), 0);
        bzero(gMenuContext->m32C_memoryCardContext, sizeof(MenuContext_32C));
    } else {
        freeHeap(gMenuContext->m32C_memoryCardContext);
    }
}

/* Noah initMenuContext33C */
void initMenuContext33C(u8 arg0) {
    if (arg0) {
        gMenuContext->m33C_drawFlags = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_33C), 0x6c), 0);
        bzero(gMenuContext->m33C_drawFlags, sizeof(MenuContext_33C));
    } else {
        freeHeap(gMenuContext->m33C_drawFlags);
    }
}

/* Noah initMenuContext350MainMenu */
void initMenuContext350MainMenu(u8 arg0) {
    if (arg0) {
        gMenuContext->m350_mainMenu = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_350), 0x1194), 0);
        bzero(gMenuContext->m350_mainMenu, sizeof(MenuContext_350));
    } else {
        freeHeap(gMenuContext->m350_mainMenu);
    }
}

/* Noah initMenuContext354 */
void initMenuContext354(u8 arg0) {
    if (arg0) {
        gMenuContext->m354_subMenu = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_354), (s32)0x140c), 0);
        bzero(gMenuContext->m354_subMenu, (s32)sizeof(struct MenuContext_354));
    } else {
        freeHeap(gMenuContext->m354_subMenu);
    }
}

/* Noah initMenuContext330 */
void initMenuContext330(u8 arg0) {
    if (arg0) {
        gMenuContext->m330_itemData = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_330), (s32)0xcc), 0);
        bzero(gMenuContext->m330_itemData, (s32)sizeof(struct MenuContext_330));
    } else {
        freeHeap(gMenuContext->m330_itemData);
    }
}

/* Noah initMenuContext340_goldOwned */
void initMenuContext340_goldOwned(u8 arg0) {
    if (arg0) {
        gMenuContext->m340_gold = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_340), (s32)0x328), 0);
        bzero(gMenuContext->m340_gold, (s32)sizeof(struct MenuContext_340));
    } else {
        freeHeap(gMenuContext->m340_gold);
    }
}

/* Noah initMenuContext344_playTime */
void initMenuContext344_playTime(u8 arg0) {
    if (arg0) {
        gMenuContext->m344_playTime = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_344), (s32)0x374), 0);
        bzero(gMenuContext->m344_playTime, (s32)sizeof(struct MenuContext_344));
    } else {
        freeHeap(gMenuContext->m344_playTime);
    }
}

/* Noah initMenuContext348 */
void initMenuContext348(u8 arg0) {
    if (arg0) {
        gMenuContext->m348_cursor = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_348), 0x15c), 0);
        bzero(gMenuContext->m348_cursor, sizeof(MenuContext_348));
    } else {
        freeHeap(gMenuContext->m348_cursor);
    }
}

/* Noah initMenuContext39C */
void initMenuContext39C(u8 arg0) {
    s32 i;

    if (arg0) {
        for (i = 0; i < 3; i++) {
            gMenuContext->m39C_infoCards[i] = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_39C), 0x127c), 0);
            bzero(gMenuContext->m39C_infoCards[i], sizeof(MenuContext_39C));
        }
    } else {
        for (i = 0; i < 3; i++) {
            freeHeap(gMenuContext->m39C_infoCards[i]);
        }
    }
}

/* Noah initMenuContext */
void initMenuContext(void) {
    initMenuContext33C(1);
    initMenuContext350MainMenu(1);
    initMenuContext354(1);
    initMenuContext330(1);
    initMenuContext348(1);
    gMenuContext->m428_cardCursors = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_428), 0x14c), 0);
    bzero(gMenuContext->m428_cardCursors, sizeof(MenuContext_428));
    switch (menuToEnter) {
    case 0:
        initMenuContext32C(1);
        initMenuContext340_goldOwned(1);
        initMenuContext344_playTime(1);
        initMenuContext39C(1);
        break;
    case 2:
    case 6:
        initMenuContext32C(1);
        break;
    }
}

/* Noah menu2_mainLoop, the teardown half */
void menu2_mainLoop(void) {
    s32 menu = menuToEnter;

    if (menu != 2 && menu < 3 && menu == 0) {
        resetCursorState();
        processLoadSaveMenuSub2(0, 0);
        gMenuContext->m33C_drawFlags->m6_drawPlayTime = 0;
        gMenuContext->m33C_drawFlags->m5_drawGold = 0;
        if (g_menuOpenArg == 0) {
            savedMenuSelectedEntry = gMenuContext->m336_menuSelectedEntry;
        }
    }
    menuDraw();
    menuDraw();
    gMenuContext->m327_drawEnabled = 0;
    menuDraw();
    do {
        menuDraw();
    } while (gMenuContext->m308_oddOrEven != 0);
    initMenuContext33C(0);
    initMenuContext350MainMenu(0);
    initMenuContext354(0);
    initMenuContext330(0);
    initMenuContext348(0);
    freeHeap(gMenuContext->m428_cardCursors);
    freeHeap(gMenuContext->m2DC_font);
    freeHeap(gMenuContext->m2E0_textBundle);
    freeHeap(gMenuContext->m4E0_labels[0].m78_imageData);
    if (useDebugMenuList) {
        stopSequence(gMenuContext->m2E4_musicSequence);
        menuDraw();
        unloadSequence(gMenuContext->m2E4_musicSequence);
        menuDraw();
        freeHeap(gMenuContext->m2E4_musicSequence);
    }
    switch (menuToEnter) {
    case 0:
        initMenuContext32C(0);
        initMenuContext340_goldOwned(0);
        initMenuContext344_playTime(0);
        initMenuContext39C(0);
        freeHeap(gMenuContext->m364_menuBoxes[0]);
        freeHeap(gMenuContext->m380_menuBoxAnims[0]);
        freeHeap(gMenuContext->m364_menuBoxes[1]);
        freeHeap(gMenuContext->m380_menuBoxAnims[1]);
        break;
    case 2:
    case 6:
        initMenuContext32C(0);
        break;
    }
    freeHeap(gMenuContext);
}

/* Noah menu2_loadGame_entryPoint */
void menu2_loadGame_entryPoint(void) {
    initMenuContext();
    setupMainMenu();
    gMenuContext->m327_drawEnabled = 1;
    gMenuContext->m32A_soundEnabled = 1;
    switch (menuToEnter) {
    case 0:
        setupMenu0();
        mainMenuExecute();
        break;
    case 2:
        menuReturnState0 = 0;
        loadSaveGameMenuExecute();
        gMenuContext->m33C_drawFlags->m9_drawMainMenu = 0;
        gMenuContext->m33C_drawFlags->m4_drawCursor = 0;
        gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 0;
        switch (menuReturnState0) {
        case 0:
            checkCurrentDiscNumber(0);
            break;
        case 2:
            checkCurrentDiscNumber(gameState.m1930_fieldVarsBackup[82]);
            break;
        }
        break;
    case 6:
        cdChangeMenuExecute();
        break;
    }
    menu2_mainLoop();
}

/* read the save-location name list (dir 0x10 file 1), skip gameState.m1930_fieldVarsBackup[0] lines, copy 30 bytes to m32C->m4FFC */
void loadSaveLocationName(void) {
    u8* buf;
    u8* p;
    s32 i;
    u16 count;
    s32 j;

    for (i = 0; i < 2; i++) {
        gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[i] = 0;
        gMenuContext->m32C_memoryCardContext->m4F8A_fileCount[i] = 0;
        gMenuContext->m32C_memoryCardContext->m4F8C_loadedFileCount[i] = 0xFF;
    }
    gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 0;
    for (i = 0; i < 32; i++) {
        gMenuContext->m32C_memoryCardContext->m0_files[i].m58_loaded = 0;
        gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[i] = 0xFF;
    }
    count = gameState.m1930_fieldVarsBackup[0];
    i = 0;
    setCurrentDirectory(0x10, 1);
    p = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(1)), 1);
    readFile(1, p, 0, 0x80);
    waitReadCompletion(0);
    /* the allocation goes through p (block-local, s0) so that i gets s1 and buf inherits s0 */
    buf = p;
    if (count != 0) {
        for (;;) {
            if (buf[i] >= 0x80) {
                i += 2;
                continue;
            }
            /* one-shot loop: weights the '\n' constant's use (loop depth) so it gets a0 */
            do {
                if (buf[i] == '\n') {
                    goto found;
                }
            } while (0);
            i++;
            continue;
        found:
            count--;
            i++;
            if (count == 0) {
                break;
            }
        }
    }
    for (j = 0; j < 30; j++) {
        gMenuContext->m32C_memoryCardContext->m4FFC_saveLocationName[j] = buf[i++]; /* i as biv: src giv starts at i + buf */
    }
    gMenuContext->m32C_memoryCardContext->m501A_saveLocationNameEnd = gMenuContext->m32C_memoryCardContext->m501B_saveLocationNameEnd2 = 0;
    setCurrentDirectory(0x10, 0);
    freeHeap(buf);
}
