/*
 * Sound config submenu; memory card session start/end. Provisional file
 * 0x801D9808-0x801D9F98. gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "menu/menu.h"

s32 processSoundMenuSub0(void);
void changeSoundOption(s32 mode);

void _bu_init(void);
s32 EnableEvent(u32 ev);
void EnterCriticalSection(void);
void ExitCriticalSection(void);
CdlCB CdReadCallback(CdlCB func);

extern CdlCB memoryCard_backupSyncCallback;
extern CdlCB memoryCard_backupReadyCallback;
extern CdlCB memoryCard_backupReadCallback;
extern u8 menuConfigDefault[];
extern s32 soundMenuTable0[];
extern u8 soundMenuConfig[];

/* Noah processSoundMenu (sound config submenu, main menu slot 7) */
u8 processSoundMenu(void) {
    u8 cont;
    u8 first;
    u8 apply;
    s32 mode;

    cont = 1;
    first = 1;
    apply = 0;
    do {
        menuDraw();
        if (first) {
            j_setupMenuContext4E0(4, gMenuContext->m1BE0_soundMenuLabels, soundMenuConfig, gMenuContext->m33C_drawFlags->m5C_drawSoundMenuLabels);
            processLoadSaveMenuSub3(0);
            gMenuContext->m339_previousSelectedSubMenuEntry = 0xFF;
            switch (processSoundMenuSub0()) {
            case 0:
                mode = 0;
                break;
            case 1:
                mode = 2;
                break;
            case 2:
                mode = 1;
                break;
            }
            first = 0;
            gMenuContext->m338_currentSelectedSubMenuEntry = mode;
        }
        if (gMenuContext->m338_currentSelectedSubMenuEntry != gMenuContext->m339_previousSelectedSubMenuEntry) {
            updateMenuSelection2(6, gMenuContext->m1BE0_soundMenuLabels, infocardNameU, soundMenuTable0, gMenuContext->m33C_drawFlags->m5C_drawSoundMenuLabels,
                          gMenuContext->m338_currentSelectedSubMenuEntry, 7, 0);
            processLoadSaveMenuSub8(0);
            gMenuContext->m339_previousSelectedSubMenuEntry = gMenuContext->m338_currentSelectedSubMenuEntry;
        }
        switch (gMenuContext->m325_menuButton) {
        case 4:
            apply = 1;
        case 5:
            cont = 0;
            break;
        case 1:
            if (gMenuContext->m338_currentSelectedSubMenuEntry > 0) {
                gMenuContext->m338_currentSelectedSubMenuEntry--;
            } else {
                gMenuContext->m338_currentSelectedSubMenuEntry = gMenuContext->m33A_numMenuEntries - 1;
            }
            break;
        case 3:
            if (++gMenuContext->m338_currentSelectedSubMenuEntry >= gMenuContext->m33A_numMenuEntries) {
                gMenuContext->m338_currentSelectedSubMenuEntry = 0;
            }
            break;
        }
    } while (cont);
    if (apply) {
        switch (gMenuContext->m338_currentSelectedSubMenuEntry) {
        case 0:
            mode = 0;
            break;
        case 1:
            mode = 2;
            break;
        case 2:
            mode = 1;
            break;
        }
        changeSoundOption(mode);
    }
    countField33C_C(4, gMenuContext->m33C_drawFlags->m5C_drawSoundMenuLabels);
    gMenuContext->m33C_drawFlags->m4_drawCursor = 0;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 0;
    return 1;
}

/* Noah startMemoryCard (open/enable the 4 card events) */
void startMemoryCard(void) {
    closeMemoryCardsEvents();
    VSync(0);
    InitCARD(1);
    StartCARD();
    _bu_init();
    DrawSync(0);
    VSync(0);
    EnterCriticalSection();
    gMenuContext->m32C_memoryCardContext->m4FEC_events[0] = OpenEvent(0xF4000001, 4, 0x2000, 0);
    gMenuContext->m32C_memoryCardContext->m4FEC_events[1] = OpenEvent(0xF4000001, 0x8000, 0x2000, 0);
    gMenuContext->m32C_memoryCardContext->m4FEC_events[2] = OpenEvent(0xF4000001, 0x100, 0x2000, 0);
    gMenuContext->m32C_memoryCardContext->m4FEC_events[3] = OpenEvent(0xF4000001, 0x2000, 0x2000, 0);
    EnableEvent(gMenuContext->m32C_memoryCardContext->m4FEC_events[0]);
    EnableEvent(gMenuContext->m32C_memoryCardContext->m4FEC_events[1]);
    EnableEvent(gMenuContext->m32C_memoryCardContext->m4FEC_events[2]);
    EnableEvent(gMenuContext->m32C_memoryCardContext->m4FEC_events[3]);
    ExitCriticalSection();
}

/* memory card session start: message 0x20, card init, save the CD callbacks */
u8 processLoadSaveMenuSub4(void) {
    u8 ret;

    processLoadSaveMenuSub5(0x20);
    ret = 1;
    gMenuContext->m33C_drawFlags->m33_cardMessageShown = 1;
    while (gMenuContext->m329_animationState != 0) {
        menuDraw();
    }
    gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[3] = 1;
    menuDraw();
    startMemoryCard();
    DrawSync(0);
    VSync(0);
    EnterCriticalSection();
    memoryCard_backupSyncCallback = CdSyncCallback(0);
    memoryCard_backupReadyCallback = CdReadyCallback(0);
    memoryCard_backupReadCallback = CdReadCallback(0);
    ExitCriticalSection();
    gMenuContext->m32C_memoryCardContext->m4FE8_prevCardPresent[0] = gMenuContext->m32C_memoryCardContext->m4FE8_prevCardPresent[1] = 0xFF;
    gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[0] = 0;
    gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[1] = 0;
    gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 2;
    gMenuContext->m326_cardPollTimer = 0x3C;
    menuDraw();
    menuDraw();
    if (!processLoadSaveMenuSub6()) {
        ret = 0;
        if (gMenuContext->m33C_drawFlags->m33_cardMessageShown) {
            processLoadSaveMenuSub7();
            gMenuContext->m33C_drawFlags->m33_cardMessageShown = 0;
        }
    }
    return ret;
}

/* memory card session end: restores the CD callbacks */
void endMemoryCardSession(void) {
    s32 i;

    gMenuContext->m4D8_cardTilesMode = 0;
    clearSaveTitleImage();
    freeMemoryCardTiles();
    freeMenuContext34C();
    for (i = 0; i < 32; i++) {
        gMenuContext->m32C_memoryCardContext->m0_files[i].m58_loaded = 0;
    }
    gMenuContext->m32C_memoryCardContext->m4F8C_loadedFileCount[0] = 0xFF;
    gMenuContext->m32C_memoryCardContext->m4F8C_loadedFileCount[1] = 0xFF;
    processLoadSaveMenuSub6Var2[1] = 0;
    processLoadSaveMenuSub6Var2[0] = 0;
    DrawSync(0);
    VSync(0);
    EnterCriticalSection();
    CdSyncCallback(memoryCard_backupSyncCallback);
    CdReadyCallback(memoryCard_backupReadyCallback);
    CdReadCallback(memoryCard_backupReadCallback);
    ExitCriticalSection();
}

/* Noah processLoadSaveMenuSub0 */
void processLoadSaveMenuSub0(void) {
    if (menuToEnter != 2) {
        j_setupMenuContext4E0(6, gMenuContext->mDE0_saveMenuLabels, menuConfigDefault, gMenuContext->m33C_drawFlags->m1A_drawSaveMenuLabels);
    } else {
        j_setupMenuContext4E0(6, gMenuContext->mDE0_saveMenuLabels, menuConfig2, gMenuContext->m33C_drawFlags->m1A_drawSaveMenuLabels);
    }
}
