/*
 * Memory card directory scan, save slot cursor, save / load / copy / delete loops (to
 * 0x801CD710), then the menu's draw code (info card panels, menu boxes, memory card
 * tiles, labels). Provisional file 0x801C8D78-0x801D1EB0; owns .rodata
 * 0x801C50A8-0x801C50FC ("bu00:", "bu10:", "__tmp_file", jtbl_801C50C8, the save
 * title string). gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "menu/menu.h"
#include "main/game_state.h"
#include "main/dialog.h"
#include "main/memory.h"
#include "pc_gameid.h"

/* PSY-Q file syscall stubs in main (B0 table) */
extern s32 func_80040534(char* name, s32 mode);                          /* open */
extern s32 func_80040544(s32 fd, void* buf, s32 n);                      /* read */
extern s32 func_80040554(s32 fd, void* buf, s32 n);                      /* write */
extern s32 func_80040564(s32 fd);                                        /* close */
extern s32 func_80040574(char* name);                                    /* format */
extern struct DIRENTRY* func_80040584(char* name, struct DIRENTRY* dir); /* firstfile */
extern struct DIRENTRY* func_80040594(struct DIRENTRY* dir);             /* nextfile */
extern s32 func_800405A4(char* oldName, char* newName);                  /* rename */
extern s32 func_800405B4(char* name);                                    /* erase */
extern char* strcpy(char* dst, const char* src);
extern char* strcat(char* dst, const char* src);
extern s32 strcmp(const char* a, const char* b);
extern void playSoundEffectInFreeSlot(s32 arg);
extern s32 getCurrentDiscNumber(void);
u8 randomInRange(s32 arg0, s32 arg1);

extern u16 battleScriptGameStateVars[];
/* cursor position -> save slot (port * 16 + index), 30 entries. With no
 * cursor (0xFF) the PS1 reads saveCursorToSlot[255], a word of statusScreenLabelPositionX (48):
 * the full extent keeps the data up to there one object on the PC. */
extern s32 saveCursorToSlot[256];
extern s32 partyCardLabelGlyphs[]; /* 1E08 panel label glyphs, 9 per variant */
extern u8 processLoadSaveMenuSub6Sub2Var1[2][16];
extern MenuSaveSummary* processLoadSaveMenuSub6Sub2Var0;
extern u8 processLoadSaveMenuSub6Var0;

s32 runYesNoPrompt(u8 arg0);
void decodeCharacterNames(void);
void moveSaveCursorRight(s32 mode, s32 cur);
void moveSaveCursorLeft(s32 mode, s32 cur);
void moveSaveCursorDown(s32 mode, s32 cur);
void moveSaveCursorUp(s32 mode, s32 cur);
void transformAndAddPrim(s32 count, SVECTOR* verts, POLY_FT4* polys, s32 oddOrEven);
void drawMultiPolys(s32 count, POLY_FT4* polys, s32 oddOrEven);
void drawMenu_348_cursor(void);
void drawMenu_428(void);
void displayGoldAndPlayTime(void);
void drawCharacterInfoCards(void);
void drawCharacterInfoCardExtended(void);
void drawMenu_35CSub0(void);
void drawMenu_35C(void);
void drawMenu_360(void);
void drawMainMenu(void);
void drawMenuContext354(void);
void drawMenu_4D8_is_2(void);
void drawMenuMemoryCard_sub0(s32 start, s32 firstTile, s32 end);
void drawMenu_4D8_text(void);
void drawMenu_memoycard_tilesLines(void);
void drawMenuMemoryCard_52(void);
void drawMenuMemoryCard(void);
void drawMenu_34C(void);
void transformAndDrawPolyFT4(SVECTOR* verts, POLY_FT4* polys, s32 oddOrEven, s32 otIndex);
void drawMenuContext364_menuBox(s32 box, u8 drawInner);
void drawMenuBoxes(void);
void drawMenu2_sub_sub0(void);
void drawMenu2_sub_sub1(void);
void drawMenu2_sub_sub2(void);
void drawMenu2_sub_sub3(void);
void drawMenu2_sub_sub6(void);
void drawMenu2_sub_sub7(void);
void drawMenu2_sub_sub8(void);
void drawMenu2_drawCheckingMemoryCard(void);
void drawMenu2_sub_sub4(void);
void drawMenu2_sub_sub5(void);
void drawMenu2_sub(void);
void drawMenuBackground(void);
void drawPartyCard(MenuContext_1E08* p, u8 drawExtra);
void setupPartyCardLabels(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, u8 variant);
void setupPartyCardLevel(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable);
void setupPartyCardHpMp(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, u8 variant);
void drawMenu_1E08(void);
void drawScrollBarThumb(void);
void drawCharacterSwitchArrows(void);
void drawItemMenuList(void);
void drawEtherListMenu(void);
void drawEquipMenuLists(void);
void drawSkillMenu(void);
void drawMenuPointerGlyphs(void);
void drawMenu0(void);
void drawMenu2(void);
void drawMenu6(void);
u8 loadGameFromMemoryCard(void);
u8 saveGameToMemoryCard(u8 arg0);
u8 copySaveToOtherCard(void);
u8 deleteSaveFromMemoryCard(void);
u8 askFormatMemoryCard(u8 msg);
u8 findFreeSaveNumber(u8 port, u8 slot);
#if VERSION_JP
void buildSaveSummary(MenuSaveSummary* summary);
void buildSaveTitle(void);
#else
void buildSaveSummary(MenuSaveSummary* summary, s32 unused, u8 slot);
void buildSaveTitle(u8 saveNumber);
#endif
void applyLoadedSaveGame(MenuSaveGame* save);
void updateMemoryCardProgressBox(u8 mode);
s32 isSaveSlotUsable(s32 mode);
s32 findFirstUsableSaveSlot(s32 mode);
s32 updateSaveSlotCursorInput(s32 mode);

/* memory card directory scan (firstfile/nextfile) */
u8 scanMemoryCardDirectory(u8 port);
u8 scanMemoryCardDirectory(u8 port) {
    struct {
        u8 m0[0x28];
    } dir;
    char path[6];
    u8 count;
    s32 i;
    s32 retries;

    processLoadSaveMenuSub6Var2[port] = 0;
    retries = 5;
    for (i = 0; i < 16; i++) {
        gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[port * 16 + i] = 0xFF;
#if !VERSION_JP
        processLoadSaveMenuSub6Sub2Var1[port][i] = 0;
#endif
    }
    if (port == 0) {
        strcpy(path, "bu00:");
    } else {
        strcpy(path, "bu10:");
    }
    while (--retries != 0) {
        count = 0;
        if (func_80040584(path, (struct DIRENTRY*)&dir) == (struct DIRENTRY*)&dir) {
            do {
                strcpy(gMenuContext->m32C_memoryCardContext->m0_files[port * 16 + count].m18_name, dir.m0);
                count++;
            } while (func_80040594((struct DIRENTRY*)&dir) == (struct DIRENTRY*)&dir);
        }
        gMenuContext->m32C_memoryCardContext->m4F8A_fileCount[port] = count;
        break;
    }
    return count;
}

/* scan both card directories once */
void updateMemoryCardDirectoryScans(void);
void updateMemoryCardDirectoryScans(void) {
    MenuContext_32C* mc = gMenuContext->m32C_memoryCardContext;

    switch (mc->m4FE4_cardPresent[2]) {
    case 1:
        if (mc->m4F88_dirScanned[0] == 0) {
            if (scanMemoryCardDirectory(0) != 0) {
                gMenuContext->m334_cardOperation = 1;
            }
            gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[0] = 1;
        }
        if (gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[1] == 0) {
            if (scanMemoryCardDirectory(1) != 0) {
                gMenuContext->m334_cardOperation = 1;
            }
            gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[1] = 1;
        }
        break;
    case 2:
        if (mc->m4F88_dirScanned[0] == 0) {
            scanMemoryCardDirectory(0);
            gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[0] = 1;
        }
        if (gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[1] == 0) {
            scanMemoryCardDirectory(1);
            gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[1] = 1;
        }
        break;
    }
}

/* Noah readSaveTitleFrame */
s32 readSaveTitleFrame(char* name, void* buf) {
    s32 fd;

    fd = func_80040534(name, 3);
    if (fd == -1) {
        return -1;
    }
    if (func_80040544(fd, buf, 0x200) != 0x200) {
        func_80040564(fd);
        return -1;
    }
    func_80040564(fd);
    return 0;
}

/* Noah loadSaveMinimalData */
void loadSaveMinimalData(u8 port, u8 slot) {
    char fileName[6];
    char path[0x40];
    s32 retries = 1;
    s32 i;

    if (port == 0) {
        strcpy(fileName, "bu00:");
    } else {
        strcpy(fileName, "bu10:");
    }
    strcpy(path, fileName);
    strcat(path, gMenuContext->m32C_memoryCardContext->m0_files[port * 16 + slot].m18_name);
    while (readSaveTitleFrame(path, &gMenuContext->m32C_memoryCardContext->mB94_fileHeaders[port * 16 + slot]) == -1 && --retries != 0) {
        makeSaveBlockDisplay(port);
    }
    for (i = 0; i < gMenuContext->m32C_memoryCardContext->mB94_fileHeaders[port * 16 + slot].m3_blockNumber; i++) {
        gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[port * 16 + gMenuContext->m32C_memoryCardContext->m4F84_tileCount] = slot + port * 16;
        gMenuContext->m32C_memoryCardContext->m4F84_tileCount++;
    }
}

/* mark Xenogears saves (name prefix match) */
void processLoadSaveMenuSub6Sub2(s32 port);
void processLoadSaveMenuSub6Sub2(s32 port) {
    s32 i;
    s32 j;
    s32 match;
    MenuContext_32C* mc;

    for (i = 0; i < 16; i++) {
        gMenuContext->m32C_memoryCardContext->m4F8E_isGameSave[port * 16 + i] = 0;
    }
    for (i = 0; i < 15; i++) {
        mc = gMenuContext->m32C_memoryCardContext;
        for (j = 0, match = 1; j < 12; j++) {
            if (mc->m0_files[mc->m4FAE_tileFiles[port * 16 + i]].m18_name[j] != mc->m4FCE_saveName[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            gMenuContext->m32C_memoryCardContext->m4F8E_isGameSave[port * 16 + i] = 1;
#if !VERSION_JP
            processLoadSaveMenuSub6Sub2Var0 = &gMenuContext->m32C_memoryCardContext->mB94_fileHeaders[gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[port * 16 + i]].m100_summary;
            processLoadSaveMenuSub6Sub2Var1[port][processLoadSaveMenuSub6Sub2Var0->m23_slot] = 1;
#endif
        }
    }
}

/* Noah processLoadSaveMenuSub6: card scan/refresh per port, cursor sprite */
u8 processLoadSaveMenuSub6(void) {
    char path[0x40];
    u8 cardState[2];
    MenuContext_32C* mc;
    s32 wait;
    s32 i;
    s32 port;
    u8 status;
    u8 result;
    u8 flag;

    processLoadSaveMenuSub6Var0 = 0;
    gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 2;
    menuDraw();
    flag = result = 0;
    flag = result; /* the second set keeps flag from being folded to a constant */
    if (*(u16*)gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent == 0) {
        processLoadSaveMenuSub7();
        gMenuContext->m33C_drawFlags->m33_cardMessageShown = 0;
        gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 0;
        menuDraw();
        processLoadSaveMenuSub5(0x23);
        gMenuContext->m4D8_cardTilesMode = 1;
        gMenuContext->m33C_drawFlags->m2F_draw428 = 0;
        gMenuContext->m32C_memoryCardContext->m4F80_previousCursor = 0xFF;
        gMenuContext->m32C_memoryCardContext->m4F7C_cursor = 0;
        mc = gMenuContext->m32C_memoryCardContext;
        if (*(u16*)mc->m4FE4_cardPresent == 0) {
            mc->m4F8C_loadedFileCount[0] = mc->m4F8C_loadedFileCount[1] = 0xFF;
            menuDraw();
            wait = 0x3B;
            do {
                VSync(0);
            } while (--wait != 0);
            result = 1;
        }
        if (result == 0) {
            startMemoryCard();
            mc = gMenuContext->m32C_memoryCardContext;
            mc->m4FE8_prevCardPresent[0] = mc->m4FE8_prevCardPresent[1] = 0xFF;
            gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[0] = 0;
            gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[1] = 0;
            gMenuContext->m326_cardPollTimer = 0x3C;
            menuDraw();
            processLoadSaveMenuSub6Var0 = 1;
        }
        processLoadSaveMenuSub7();
    }
    processLoadSaveMenuSub6Var1 = 1;
    cardState[0] = gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[0];
    cardState[1] = gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[1];
    if (result == 0) {
        gMenuContext->m32A_soundEnabled = 0;
        for (port = 0; port < 2; port++) {
            status = 0;
            if (gMenuContext->m32C_memoryCardContext->m4F8A_fileCount[port] != gMenuContext->m32C_memoryCardContext->m4F8C_loadedFileCount[port]) {
                if (gMenuContext->m32C_memoryCardContext->m4F8A_fileCount[port] == 0) {
                    goto empty;
                }
                startMemoryCard();
                for (i = 0; i < 16; i++) {
                    gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[port * 16 + i] = 0xFF;
                }
                if (port == 0) {
                    strcpy(path, "bu00:");
                } else {
                    strcpy(path, "bu10:");
                }
                strcat(path, "__tmp_file");
                func_800405B4(path);
                gMenuContext->m32C_memoryCardContext->m4F84_tileCount = 0;
                processLoadSaveMenuSub6Var2[port] = 0;
                for (i = 0; i < gMenuContext->m32C_memoryCardContext->m4F8A_fileCount[port]; i++) {
                    if (gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[port] == 0 ||
                        cardState[0] != gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[0] ||
                        cardState[1] != gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[1]) {
                        goto bad;
                    }
                    loadSaveMinimalData(port, i);
                    loadMemoryCardIconToVram(port * 16 + i);
                    gMenuContext->m32C_memoryCardContext->m0_files[port * 16 + i].m58_loaded = 1;
                    menuDraw();
                    if (gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[port] != 0) {
                        continue;
                    }
                    status = port + 1;
                    goto abort;
                bad:
                    status = 2;
                abort:
                    port = 2;
                    result = 0;
                    break;
                }
                if (status != 0) {
                    continue;
                }
                for (; i < 16; i++) {
                    gMenuContext->m32C_memoryCardContext->m0_files[port * 16 + i].m58_loaded = 0;
                }
                gMenuContext->m32C_memoryCardContext->m4F8C_loadedFileCount[port] = gMenuContext->m32C_memoryCardContext->m4F8A_fileCount[port];
            }
        empty:
            if (status == 0 && gMenuContext->m32C_memoryCardContext->m4F8A_fileCount[port] == 0) {
                for (i = 0; i < 16; i++) {
                    gMenuContext->m32C_memoryCardContext->m0_files[port * 16 + i].m58_loaded = 0;
                }
                gMenuContext->m32C_memoryCardContext->m4F8C_loadedFileCount[port] = 0xFF;
            }
        }
        if (status != 0) {
            goto end;
        }
        processLoadSaveMenuSub6Sub2(0);
        processLoadSaveMenuSub6Sub2(1);
    }
    if (status == 0) {
        if (flag) {
            gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 1;
        }
        if (gMenuContext->m33C_drawFlags->m2F_draw428 != 0 && gMenuContext->m428_cardCursors->m144_followTile[0] != 0) {
            setXY4(&gMenuContext->m428_cardCursors->m0_polys[0][gMenuContext->m428_cardCursors->m148_oddOrEven[0]],
                   memoryCardTilesPositionX[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 8,
                   memoryCardTilesPositionY[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] - 6,
                   memoryCardTilesPositionX[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 0x18,
                   memoryCardTilesPositionY[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] - 6,
                   memoryCardTilesPositionX[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 8,
                   memoryCardTilesPositionY[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 10,
                   memoryCardTilesPositionX[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 0x18,
                   memoryCardTilesPositionY[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] + 10);
        }
    }
end:
    gMenuContext->m32A_soundEnabled = 1;
    processLoadSaveMenuSub6Var1 = 0x1E;
    return result;
}

/* is the slot under the cursor usable for mode (0 any, 1 Xenogears, 2 free) */
s32 isSaveSlotUsable(s32 mode) {
    MenuContext_32C* mc = gMenuContext->m32C_memoryCardContext;
    s32 result = 1;
    s32 cur = mc->m4F7C_cursor;

    switch (mode) {
    case 0:
        if ((*(u32*)mc->m4F88_dirScanned & 0xFFFF0000) != 0) {
            if (mc->m4FE4_cardPresent[saveCursorToSlot[cur] / 16] != 0) {
                if (mc->m4FAE_tileFiles[saveCursorToSlot[cur]] == 0xFF) {
                    result = 0;
                }
            } else {
                result = 0;
            }
        } else {
            result = 0;
        }
        break;
    case 1:
        if ((*(u32*)mc->m4F88_dirScanned & 0xFFFF0000) != 0) {
            if (mc->m4FE4_cardPresent[saveCursorToSlot[cur] / 16] != 0 && mc->m4FAE_tileFiles[saveCursorToSlot[cur]] != 0xFF) {
                if (mc->m4F8E_isGameSave[saveCursorToSlot[cur]] == 0) {
                    result = 0;
                }
            } else {
                result = 0;
            }
        } else {
            result = 0;
        }
        break;
    case 2:
        if (mc->m4FE4_cardPresent[saveCursorToSlot[cur] / 16] == 0) {
            result = 0;
        }
        break;
    }
    if (result) {
        gMenuContext->m4D8_cardTilesMode = 2;
    }
    return result;
}

/* first valid slot for mode (0 any, 1 Xenogears, 2 free) */
s32 findFirstUsableSaveSlot(s32 mode) {
    s32 found = 0xFF;
    s32 searching = 1;
    s32 end = 30;
    s32 start;
    s32 i;
    MenuContext_32C* mc;

    start = (gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[0] == 0) ? 15 : 0;
    if (gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[1] == 0) {
        end = 15;
    }
    for (i = start; i < end && searching; i++) {
        switch (mode) {
        case 0:
            mc = gMenuContext->m32C_memoryCardContext;
            if ((*(u32*)mc->m4F88_dirScanned & 0xFFFF0000) == 0) {
                searching = 0;
            } else if (mc->m4FE4_cardPresent[saveCursorToSlot[i] / 16] != 0 && mc->m4FAE_tileFiles[saveCursorToSlot[i]] != 0xFF) {
                found = i;
                searching = 0;
            }
            break;
        case 1:
            mc = gMenuContext->m32C_memoryCardContext;
            if ((*(u32*)mc->m4F88_dirScanned & 0xFFFF0000) == 0) {
                searching = 0;
            } else if (mc->m4FE4_cardPresent[saveCursorToSlot[i] / 16] != 0 && mc->m4FAE_tileFiles[saveCursorToSlot[i]] != 0xFF &&
                       mc->m4F8E_isGameSave[saveCursorToSlot[i]] != 0) {
                found = i;
                searching = 0;
            }
            break;
        case 2:
            if (gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[saveCursorToSlot[i] / 16] != 0) {
                found = i;
                searching = 0;
            }
            break;
        }
    }
    gMenuContext->m4D8_cardTilesMode = 2;
    return found;
}

/* cursor down a row (+3), fallback to next slot */
void moveSaveCursorRight(s32 mode, s32 cur) {
    switch (mode) {
    case 0:
        while (cur + 3 < 30) {
            if (gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[cur + 3]] != 0xFF) {
                gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur + 3;
                break;
            }
            cur += 3;
        }
        if (cur + 3 < 30) {
            break;
        }
        cur = gMenuContext->m32C_memoryCardContext->m4F7C_cursor + 3;
        if (cur < 30) {
            while (cur + 1 < 30) {
                if (gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[cur + 1]] != 0xFF) {
                    gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur + 1;
                    break;
                }
                cur++;
            }
        }
        break;
    case 1:
        while (cur + 3 < 30) {
            if (gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[cur + 3]] == 0xFF ||
                gMenuContext->m32C_memoryCardContext->m4F8E_isGameSave[saveCursorToSlot[cur + 3]] == 0) {
                cur += 3;
                continue;
            }
            gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur + 3;
            break;
        }

        if (cur + 3 < 30) {
            break;
        }
        cur = gMenuContext->m32C_memoryCardContext->m4F7C_cursor + 3;
        if (cur < 30) {
            while (cur + 1 < 30) {
                if (gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[cur + 1]] == 0xFF ||
                    gMenuContext->m32C_memoryCardContext->m4F8E_isGameSave[saveCursorToSlot[cur + 1]] == 0) {
                    cur++;
                    continue;
                }
                gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur + 1;
                break;
            }
        }
        break;
    case 2:
        if (gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[(cur + 3) / 15] != 0) {
            while (cur + 3 < 30) {
                gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur + 3;
                break;
            }
            if (cur + 3 < 30) {
                break;
            }
            cur = gMenuContext->m32C_memoryCardContext->m4F7C_cursor + 3;
            if (cur < 30) {
                while (cur + 1 < 30) {
                    gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur + 1;
                    break;
                }
            }
        }
        break;
    }
}

/* cursor up a row (-3), fallback to previous slot */
void moveSaveCursorLeft(s32 mode, s32 cur) {
    switch (mode) {
    case 0:
        while (cur - 3 >= 0) {
            if (gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[cur - 3]] != 0xFF) {
                gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur - 3;
                break;
            }
            cur -= 3;
        }
        if (cur - 3 >= 0) {
            break;
        }
        cur = gMenuContext->m32C_memoryCardContext->m4F7C_cursor - 3;
        if (cur >= 0) {
            while (cur - 1 >= 0) {
                if (gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[cur - 1]] != 0xFF) {
                    gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur - 1;
                    break;
                }
                cur--;
            }
        }
        break;
    case 1:
        while (cur - 3 >= 0) {
            if (gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[cur - 3]] == 0xFF ||
                gMenuContext->m32C_memoryCardContext->m4F8E_isGameSave[saveCursorToSlot[cur - 3]] == 0) {
                cur -= 3;
                continue;
            }
            gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur - 3;
            break;
        }

        if (cur - 3 >= 0) {
            break;
        }
        cur = gMenuContext->m32C_memoryCardContext->m4F7C_cursor - 3;
        if (cur >= 0) {
            while (cur - 1 >= 0) {
                if (gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[cur - 1]] == 0xFF ||
                    gMenuContext->m32C_memoryCardContext->m4F8E_isGameSave[saveCursorToSlot[cur - 1]] == 0) {
                    cur--;
                    continue;
                }
                gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur - 1;
                break;
            }
        }
        break;
    case 2:
        if (gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[(cur - 3) / 15] != 0) {
            while (cur - 3 >= 0) {
                gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur - 3;
                break;
            }
            if (cur - 3 >= 0) {
                break;
            }
            cur = gMenuContext->m32C_memoryCardContext->m4F7C_cursor - 3;
            if (cur >= 0) {
                while (cur - 1 >= 0) {
                    gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur - 1;
                    break;
                }
            }
        }
        break;

    }
}

/* cursor right/next slot (case 1 needs the continue-form loop) */
void moveSaveCursorDown(s32 mode, s32 cur) {
    switch (mode) {
    case 0:
        while (cur + 1 < 30) {
            if (gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[cur + 1]] != 0xFF) {
                gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur + 1;
                break;
            }
            cur++;
        }
        break;
    case 1:
        while (cur + 1 < 30) {
            if (gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[cur + 1]] == 0xFF ||
                gMenuContext->m32C_memoryCardContext->m4F8E_isGameSave[saveCursorToSlot[cur + 1]] == 0) {
                cur++;
                continue;
            }
            gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur + 1;
            break;
        }
        break;
    case 2:
        if (gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[(cur + 1) / 15] != 0 && cur + 1 < 30) {
            gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur + 1;
        }
        break;
    }
}

/* cursor left/previous slot */
void moveSaveCursorUp(s32 mode, s32 cur) {
    switch (mode) {
    case 0:
        while (cur - 1 >= 0) {
            if (gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[cur - 1]] != 0xFF) {
                gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur - 1;
                break;
            }
            cur--;
        }
        break;
    case 1:
        while (cur - 1 >= 0) {
            if (gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[cur - 1]] == 0xFF ||
                gMenuContext->m32C_memoryCardContext->m4F8E_isGameSave[saveCursorToSlot[cur - 1]] == 0) {
                cur--;
                continue;
            }
            gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur - 1;
            break;
        }
        break;
    case 2:
        if (gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[(cur - 1) / 15] != 0 && cur - 1 >= 0) {
            gMenuContext->m32C_memoryCardContext->m4F7C_cursor = cur - 1;
        }
        break;
    }
}

/* "bu00:", "bu10:" (0x801C50A8-0x801C50B4) and "__tmp_file" (0x801C50B8) are
 * emitted by the C above (retail's "" at 0x801C50C4 is the jump table's
 * alignment padding here: same bytes). */

/* slot list input handler (owns jtbl_801C50C8) */
s32 updateSaveSlotCursorInput(s32 mode) {
    s32 result = 0;

    switch (gMenuContext->m325_menuButton) {
    case 4:
        result = 1;
        break;
    case 5:
        result = 2;
        break;
    case 0:
        moveSaveCursorRight(mode, gMenuContext->m32C_memoryCardContext->m4F7C_cursor);
        break;
    case 2:
        moveSaveCursorLeft(mode, gMenuContext->m32C_memoryCardContext->m4F7C_cursor);
        break;
    case 1:
        moveSaveCursorDown(mode, gMenuContext->m32C_memoryCardContext->m4F7C_cursor);
        break;
    case 3:
        moveSaveCursorUp(mode, gMenuContext->m32C_memoryCardContext->m4F7C_cursor);
        break;
    }
    if (gMenuContext->m32C_memoryCardContext->m4F7C_cursor != gMenuContext->m32C_memoryCardContext->m4F80_previousCursor) {
        MenuContext_32C* mc = gMenuContext->m32C_memoryCardContext;
        showMemoryCardSave(mc->m4FAE_tileFiles[saveCursorToSlot[mc->m4F7C_cursor]], mc->m4F8E_isGameSave[saveCursorToSlot[mc->m4F7C_cursor]]);
        gMenuContext->m32C_memoryCardContext->m4F80_previousCursor = gMenuContext->m32C_memoryCardContext->m4F7C_cursor;
    }
    return result;
}

/* build the save title "XENOGEARS/no.NN <location>" */
#if VERSION_JP
/* JP: the save title "ゼノギアス／" in Shift-JIS, followed by 3 bytes of linker fill ("e_n"), so it
 * stays assembly (INCLUDE_RODATA at the end of the file). */
extern u32 D_801C50D0[];
#endif
#if VERSION_JP
void buildSaveTitle(void) {
#else
void buildSaveTitle(u8 saveNumber) {
#endif
#if VERSION_JP
    __builtin_memcpy(gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title, D_801C50D0, 13);
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[12] = 0x82;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[13] = gMenuContext->m2EC_playTime[0] + 0x4F;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[14] = 0x82;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[15] = gMenuContext->m2EC_playTime[1] + 0x4F;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[16] = 0x82;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[17] = gMenuContext->m2EC_playTime[2] + 0x4F;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[18] = 0x81;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[19] = 0x46;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[20] = 0x82;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[21] = gMenuContext->m2EC_playTime[3] + 0x4F;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[22] = 0x82;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[23] = gMenuContext->m2EC_playTime[4] + 0x4F;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[24] = 0x81;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[25] = 0x46;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[26] = 0x82;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[27] = gMenuContext->m2EC_playTime[5] + 0x4F;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[28] = 0x82;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[29] = gMenuContext->m2EC_playTime[6] + 0x4F;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[30] = 0x81;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[31] = 0x40;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[32] = 0;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[33] = 0;
#else
    s32 n = saveNumber + 1;

    /* "ＸＥＮＯＧＥＡＲＳ／ｎｏ．" in Shift-JIS */
    strcpy(gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title,
           "\x82\x77\x82\x64\x82\x6D\x82\x6E\x82\x66\x82\x64\x82\x60\x82\x71\x82\x72\x81\x5E\x82\x6D\x82\x8F\x81\x44");
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[26] = 0x82;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[27] = n / 10 + 0x4F;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[28] = 0x82;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[29] = n % 10 + 0x4F;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[30] = 0x81;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[31] = 0x40;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[32] = 0;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title[33] = 0;
#endif
    strcat(gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title,
           gMenuContext->m32C_memoryCardContext->m4FFC_saveLocationName);
}

/* yes/no prompt loop (m428 cursor), optional 180-frame timeout; an unused block local stops loop rotation */
s32 runYesNoPrompt(u8 waitForever) {
    u8 cardState[2];
    s32 result;
    u8 loop;
    u8 timer;

    cardState[0] = gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[0];
    cardState[1] = gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[1];
    loop = 1;
    if (menuVarUnk1 != 0) {
        gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 2;
    }
    yesNoPromptCancelled = 0;
    result = 0;
#if VERSION_JP
    timer = 60;
#else
    timer = 180;
#endif
    do {
        s32 unused; /* a block-scope declaration: keeps GCC from rotating the loop at the first exit */
        if (waitForever == 0) {
            gMenuContext->m428_cardCursors->m140_enabled[2] = 0;
            gMenuContext->m428_cardCursors->m140_enabled[3] = 0;
            if (gMenuContext->m325_menuButton != 8) {
                break;
            }
            if (--timer == 0) {
                break;
            }
        }
        menuDraw();
        if (waitForever != 0 && menuVarUnk1 != 0) {
            if (cardState[0] != gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[0]) {
                gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[0] = 0;
                gMenuContext->m325_menuButton = 5;
                needProcessLoadSaveMenuSub7 = 1;
            }
            if (cardState[1] != gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[1]) {
                gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[1] = 0;
                gMenuContext->m325_menuButton = 5;
                needProcessLoadSaveMenuSub7 = 1;
            }
        }
        switch (gMenuContext->m325_menuButton) {
        case 4:
            loop = 0;
            break;
        case 5:
            result = 0;
            loop = 0;
            yesNoPromptCancelled = 1;
            break;
        case 2:
            gMenuContext->m428_cardCursors->m140_enabled[2] = 1;
            gMenuContext->m428_cardCursors->m140_enabled[3] = 0;
            result = 1;
            break;
        case 0:
            gMenuContext->m428_cardCursors->m140_enabled[2] = 0;
            gMenuContext->m428_cardCursors->m140_enabled[3] = 1;
            result = 0;
            break;
        }
    } while (loop);
    gMenuContext->m428_cardCursors->m140_enabled[2] = 0;
    gMenuContext->m428_cardCursors->m140_enabled[3] = 0;
    gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 0;
    return result;
}

/* one or two yes/no questions (message ids; 0xFF: no second question) */
u8 askYesNoQuestion(u8 arg0, u8 arg1, u8 arg2) {
    u8 result;

    processLoadSaveMenuSub5(arg0);
    gMenuContext->m428_cardCursors->m140_enabled[3] = 1;
    result = runYesNoPrompt(arg2);
    processLoadSaveMenuSub7();
    if (arg1 != 0xFF && result != 0) {
        processLoadSaveMenuSub5(arg1);
        gMenuContext->m428_cardCursors->m140_enabled[3] = 1;
        result = runYesNoPrompt(arg2);
        processLoadSaveMenuSub7();
    }
    return result;
}

/* reset the save slot cursor (to the second card if only it is inserted) */
void resetSaveSlotCursor(void) {
    MenuContext* ctx = gMenuContext;

    ctx->m32C_memoryCardContext->m4F80_previousCursor = 0xFF;
    ctx->m32C_memoryCardContext->m4F7C_cursor = 0;
    if (ctx->m32C_memoryCardContext->m4FE4_cardPresent[0] == 0 && ctx->m32C_memoryCardContext->m4FE4_cardPresent[1] != 0) {
        ctx->m32C_memoryCardContext->m4F7C_cursor = 0xF;
    }
}

/* progress box (m44C) close/open/done; setRGB0 */
void updateMemoryCardProgressBox(u8 mode) {
    MenuContext_44C* p;

    switch (mode) {
    case 0:
        if (gMenuContext->m33C_drawFlags->m52_draw44C != 0) {
            gMenuContext->m33C_drawFlags->m52_draw44C = 0;
            menuDraw();
            freeHeap(gMenuContext->m44C_progressBox);
        }
        break;
    case 1:
        p = allocateHeap(PC_HEAP_SIZED((s32)sizeof(MenuContext_44C), (s32)0x7bc), 0);
        gMenuContext->m44C_progressBox = p;
        bzero(p, (s32)sizeof(MenuContext_44C));
        SetPolyF4(&gMenuContext->m44C_progressBox->m0_bar[gMenuContext->m308_oddOrEven]);
        setRGB0(&gMenuContext->m44C_progressBox->m0_bar[gMenuContext->m308_oddOrEven], 0xA0, 0xA0, 0);
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0x160, gMenuContext->m44C_progressBox->m30_line1, gMenuContext->m308_oddOrEven, 0xA0, 0x64, 0x1000);
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0x161, gMenuContext->m44C_progressBox->m3F0_line2, gMenuContext->m308_oddOrEven, 0xA0, 0x64, 0x1000);
        gMenuContext->m44C_progressBox->m7B8_oddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m33C_drawFlags->m52_draw44C = 1;
        gMenuContext->m44C_progressBox->m7B4_speed = 8;
        playSoundEffectInFreeSlot((gMenuContext->m2E4_musicSequence->u14.m14_id << 16) | 0xE0);
        playSoundEffectInFreeSlot((gMenuContext->m2E4_musicSequence->u14.m14_id << 16) | 0xE1);
        playSoundEffectInFreeSlot((gMenuContext->m2E4_musicSequence->u14.m14_id << 16) | 0x8F);
        break;
    case 2:
        if (gMenuContext->m33C_drawFlags->m52_draw44C != 0) {
            gMenuContext->m44C_progressBox->m7B0_width = 0x100;
            setRGB0(&gMenuContext->m44C_progressBox->m0_bar[gMenuContext->m44C_progressBox->m7B8_oddOrEven], 0, 0xA0, 0);
            gMenuContext->m33C_drawFlags->m52_draw44C = 2;
        }
        break;
    }
}

/* re-encode the character names in place (decodeCharacterCodes) */
void decodeCharacterNames(void) {
    u8 src[0x18];
    u8 dst[0x18];
    s32 i;
    s32 j;

    /* decodeCharacterCodes writes only the name and its terminator, but
     * the loop copies all 0x14 bytes of dst. Seed it once so later names
     * retain earlier names' tails without copying host stack addresses.
     * Unlike loadInitialGameState, the PS1's initial leftovers depend on
     * the caller (Beetle: 0x14, 1, 0, 0 after saving; code and overlay
     * addresses after a title load). Use zeros on the PC. Card saves encode
     * only the names through their terminators, so their bytes are unchanged. */
    for (j = 0; j < 0x18; j++) {
        dst[j] = 0;
    }
    for (i = 0; i < 31; i++) {
        for (j = 0; j < 0x14; j += 2) {
            src[j] = gameState.m0_names[i][j];
            src[j + 1] = gameState.m0_names[0][i * 0x14 + j + 1];
            if (gameState.m0_names[i][j] == 0 && gameState.m0_names[0][i * 0x14 + j + 1] == 0) {
                break;
            }
        }
        decodeCharacterCodes((u16*)src, dst, j / 2);
        for (j = 0; j < 0x14; j++) {
            gameState.m0_names[i][j] = dst[j];
        }
    }
}

/* restore gameState from loaded save data, then re-encode the names */
void applyLoadedSaveGame(MenuSaveGame* save) {
    s32 i;

    restoreSaveGameData(save, gMenuContext->m330_itemData);
    g_playTimeInVsync = save->m0_summary.m0_playTime;
    for (i = 0; i < 16; i++) {
        battleScriptGameStateVars[i] = gameState.m2324_battleScriptVars[i];
    }
    decodeCharacterNames();
}

/* load game */
u8 loadGameFromMemoryCard(void) {
    char path[0x40];
    s32 first;
    u8 result;
    s32 loop;
    u8 port;
    u8 retries;
    s32 fd;
    s32 handle;
    s32 offset;
    u8* buffer;
    u8* p;
    u8 sum;
    s32 i;
    s32 size; /* a pseudo equal to 0x100: reload rematerialises it in a3 for the compare */

    result = 1;
    size = 0x100;
    first = 1;
    loop = 1;
    resetSaveSlotCursor();
    do {
        if (processLoadSaveMenuSub6() != 0) {
            break;
        }
        if (first) {
            if (gMenuContext->m33C_drawFlags->m33_cardMessageShown != 0) {
                processLoadSaveMenuSub7();
            }
            first = 0;
            gMenuContext->m428_cardCursors->m140_enabled[0] = 1;
        }
        if (isSaveSlotUsable(1) == 0) {
            s32 slot = findFirstUsableSaveSlot(1);
            gMenuContext->m32C_memoryCardContext->m4F7C_cursor = slot;
            if (slot == 0xFF) {
                gMenuContext->m33C_drawFlags->m2F_draw428 = 0;
                gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 0;
                gMenuContext->m4D8_cardTilesMode = 1;
                gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 0;
                askYesNoQuestion(0x62, 0xFF, 0);
                result = 0; /* after the call: sched1 hoists it between the argument loads */
                break;
            }
        }
        gMenuContext->m33C_drawFlags->m2F_draw428 = 1;
        switch (updateSaveSlotCursorInput(1)) {
        case 1:
            gMenuContext->m428_cardCursors->m144_followTile[0] = 0;
            gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 0;
            port = 0;
            if (gMenuContext->m32C_memoryCardContext->m4F7C_cursor < 15) {
                strcpy(path, "bu00:");
            } else {
                strcpy(path, "bu10:");
                port = 1;
            }
            strcat(path, gMenuContext->m32C_memoryCardContext->m0_files[gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]]].m18_name);
            if (askYesNoQuestion(0x65, 0xFF, 1) == 0) {
                gMenuContext->m32C_memoryCardContext->m4F80_previousCursor = 0xFF;
                goto next;
            }
            goto confirmed;
        readFail:
            func_80040564(handle);
            goto freeBuffer;
        checksumOk:
            p = buffer + 0x100;
            pcGameIdLoadTail(p); /* the save's game id, in its free tail */
            loadSharedMenuData2(1);
            applyLoadedSaveGame((s32*)p);
            loadSharedMenuData2(0x11);
            goto freeBuffer;
        confirmed:
            processLoadSaveMenuSub5(0x3B);
            retries = 5;
            gMenuContext->m32A_soundEnabled = 0;
            do {
                fd = func_80040534(path, 1);
                if (fd == -1) {
                    fd = 0;
                    makeSaveBlockDisplay(port);
                }
                handle = fd;
                if (fd != 0) {
                    break;
                }
            } while (--retries != 0);
            /* dead store (flow deletes it): an insn between the loop exit and the test stops
             * thread_jumps from sending the break past the handle test */
            i = 0;
            /* one-shot loop: its depth weights p's references (p s0, retries s1 as retail) */
            do {
            if (handle != 0) {
                gMenuContext->m33C_drawFlags->m2F_draw428 = 0;
                buffer = allocateHeap(PC_HEAP_BYTES(0x2100), 1);
                offset = 0;
                p = buffer;
                gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 0;
                updateMemoryCardProgressBox(1);
                do {
                    menuDraw();
                    retries = 5;
                retryRead:
                    if (func_80040544(fd, p, size) != size) {
                        fd = 0;
                        makeSaveBlockDisplay(port);
                    }
                    if (fd == 0) {
                        if (--retries != 0) {
                            goto retryRead;
                        }
                        goto readFail;
                    }

                    offset += 0x100;
                    p += 0x100;
                } while (offset < gMenuContext->m32C_memoryCardContext->mB94_fileHeaders[32].m3_blockNumber << 13);
                func_80040564(fd);
                p = buffer + 0x100;
                sum = 0;
                for (i = 0; i < 0x1EFF; i++) {
                    sum += *p++;
                }
                if (sum == *p) {
                    goto checksumOk;
                }
                fd = 0;
            freeBuffer:
                freeHeap(buffer);
            }
            } while (0);
            updateMemoryCardProgressBox(2);
            processLoadSaveMenuSub7();
            if (fd != 0) {
                loop = 0;
                gMenuContext->m32A_soundEnabled = 1;
                playMenuSoundEffect(0x34);
                gMenuContext->m32A_soundEnabled = 0;
                askYesNoQuestion(0x5C, 0xFF, 0);
            } else {
                askYesNoQuestion(0x3E, 0xFF, 0);
                gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 2;
            }
            updateMemoryCardProgressBox(0);
            gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[0] = 0;
            gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[1] = 0;
            gMenuContext->m32C_memoryCardContext->m4F8C_loadedFileCount[0] = 0xFF;
            gMenuContext->m32C_memoryCardContext->m4F8C_loadedFileCount[1] = 0xFF;
        next:
            gMenuContext->m428_cardCursors->m144_followTile[0] = 1;
            break;
        case 2:
            loop = 0;
            result = 0;
            gMenuContext->m32A_soundEnabled = 1;
            break;
        }
    } while (loop);
    gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 1;
    gMenuContext->m334_cardOperation = 1;
    return result;
}

/* wait-for-confirm dialog, aborts when a card is inserted/removed */
u8 askFormatMemoryCard(u8 msg) {
    u8 cardState[2];
    u8 changed;
    u8 result;

    cardState[0] = gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[0];
    cardState[1] = gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[1];
    processLoadSaveMenuSub5(msg * 3 + 0x29);
    gMenuContext->m325_menuButton = 8;
    result = 0;
    gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 2;
    changed = 0;
    while (gMenuContext->m325_menuButton == 8) {
        menuDraw();
        if (cardState[0] != gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[0] ||
            cardState[1] != gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[1]) {
            changed = 1;
            break;
        }
    }
    processLoadSaveMenuSub7();
    if (changed == 0) {
        result = askYesNoQuestion(0x2F, 0xFF, 1);
    }
    return result;
}

/* first free save slot on a card when slot is 0xFF (US only: JP has it
 * inside its caller) */
#if !VERSION_JP
u8 findFreeSaveNumber(u8 port, u8 slot) {
    s32 i;
    s32 result = 0;

    if (slot == 0xFF) {
        for (i = 0; i < 15; i++) {
            if (processLoadSaveMenuSub6Sub2Var1[port][i] == 0) {
                result = i;
                break;
            }
        }
        return result;
    }
    return slot;
}
#endif

/* fill save summary (party HP/MP/levels, play time, slot), re-encode names, disc no. */
#if VERSION_JP
void buildSaveSummary(MenuSaveSummary* summary) {
#else
void buildSaveSummary(MenuSaveSummary* summary, s32 unused, u8 slot) {
#endif
    u8 src[0x18];
    u8 dst[0x18];
    s32 i;
    s32 j;

    for (j = 0; j < 16; j++) {
        gameState.m2324_battleScriptVars[j] = battleScriptGameStateVars[j];
    }
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
            summary->m1C_partyMemberId[i] = gMenuContext->m33C_drawFlags->m30_partyMemberId[i];
            summary->m4_hp[i] = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].m4C_HP;
            summary->mA_maxHp[i] = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].m4E_MaxHP;
            summary->m10_mp[i] = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].m50_MP;
            summary->m13_maxMp[i] = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].m52_MaxMP;
            summary->m16_level[i] = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].m62_Level;
            summary->m19_level2[i] = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].m63_Level2;
        } else {
            summary->m1C_partyMemberId[i] = 0xFF;
        }
    }
    summary->m0_playTime = g_playTimeInVsync;
    summary->m1F = 0;
#if !VERSION_JP
    summary->m23_slot = slot;
#endif
    for (i = 0; i < 31; i++) {
        for (j = 0; j < 0x14; j++) {
            src[j] = gameState.m0_names[i][j];
            dst[j] = 0;
        }
        encodeStringToCharCodes(src, (u16*)dst);
        for (j = 0; j < 0x14; j++) {
            gameState.m0_names[i][j] = dst[j];
        }
    }
    gameState.m1930_fieldVarsBackup[82] = (discChangeSaveActive == 0) ? getCurrentDiscNumber() - 1 : 1;
    buildSaveGameData((MenuSaveGame*)summary); /* the summary heads the save data */
    decodeCharacterNames();
}

/* save game loop (format, overwrite, tmp file, write, rename); an unused 8-byte local */
u8 saveGameToMemoryCard(u8 arg0) {
    char name[0x40];
    char path[0x40];
    char drive[6];
    char number[9]; /* US: slot digit; JP: play time digits + a random digit */
    u8 ok;
    u8 port;
    u8 retries;
    u8 number2;
    s32 fd;
    s32 offset;
    u8* buffer;
    u8* p;
    u8 sum;
    u8* q;
    s32 i;
    s32 loop;
    s32 handle;
    u8 first;
    u8 result;
#if !VERSION_JP
    u8 saveSlot;

    saveSlot = 0xFF;
#endif
    first = 1;
    loop = 1;
    result = 0;
    resetSaveSlotCursor();
    do {
        if (processLoadSaveMenuSub6() != 0) {
            result = 1;
            break;
        }
        if (first) {
            if (gMenuContext->m33C_drawFlags->m33_cardMessageShown != 0) {
                processLoadSaveMenuSub7();
            }
            first = 0;
            gMenuContext->m428_cardCursors->m140_enabled[0] = 1;
        }
        if (isSaveSlotUsable(2) == 0) {
            s32 slot = findFirstUsableSaveSlot(2);
            gMenuContext->m32C_memoryCardContext->m4F7C_cursor = slot;
            if (slot == 0xFF) {
                askYesNoQuestion(0xAC, 0xFF, 0);
                break;
            }
        }
        gMenuContext->m33C_drawFlags->m2F_draw428 = 1;
        switch (updateSaveSlotCursorInput(2)) {
        case 1:
            gMenuContext->m428_cardCursors->m144_followTile[0] = 0;
            gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 0;
            ok = 1;
#if VERSION_JP
            for (i = 0; i < 7; i++) {
                number[i] = gMenuContext->m2EC_playTime[i] + '0';
            }
            number[7] = randomInRange(0, 9) + '0';
            number[8] = 0;
#endif
            if (gMenuContext->m32C_memoryCardContext->m4F7C_cursor < 15) {
                strcpy(drive, "bu00:");
                port = 0;
            } else {
                strcpy(drive, "bu10:");
                port = 1;
            }
            if (gMenuContext->m32C_memoryCardContext->m4F74_cardStatus[port] == -2) {
                if (askFormatMemoryCard(port) == 0) {
                    goto cancel;
                }
                processLoadSaveMenuSub5(0x26);
                if (func_80040574(drive) != 0) {
                    processLoadSaveMenuSub7();
                    askYesNoQuestion(0x5C, 0xFF, 0);
                } else {
                    processLoadSaveMenuSub7();
                    ok = 0;
                    loop = 0;
                }
            }
            if (ok == 0) {
                goto done;
            }
            if (gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] != 0xFF) {
                if (gMenuContext->m32C_memoryCardContext->m4F8E_isGameSave[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] != 0) {
                    if (askYesNoQuestion(0x38, 0xFF, 1) == 0) {
                        goto no;
                    }
                    strcpy(path, drive);
                    strcat(path, gMenuContext->m32C_memoryCardContext->m0_files[gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]]].m18_name);
                    func_800405B4(path);
#if !VERSION_JP
                    saveSlot = (gMenuContext->m32C_memoryCardContext->mB94_fileHeaders + gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]])->m100_summary.m23_slot;
#endif
                } else {
                    askYesNoQuestion(0xC4, 0xFF, 0);
                cancel:
                    gMenuContext->m428_cardCursors->m144_followTile[0] = 1;
                    continue;
                }
            } else {
                if (askYesNoQuestion(0x5F, 0xFF, 1) == 0) {
                no:
                    ok = 0;
                    gMenuContext->m32C_memoryCardContext->m4F80_previousCursor = 0xFF;
                }
            }
            if (ok == 0) {
                goto done;
            }
            gMenuContext->m33C_drawFlags->m2F_draw428 = 0;
            gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 0;
            gMenuContext->m32C_memoryCardContext->m4F80_previousCursor = 0xFF;
            gMenuContext->m32C_memoryCardContext->m4F8C_loadedFileCount[0] = 0xFF;
            gMenuContext->m32C_memoryCardContext->m4F8C_loadedFileCount[1] = 0xFF;
#if !VERSION_JP
            number2 = findFreeSaveNumber(port, saveSlot);
            number[0] = number2 + '0';
            number[1] = 0;
#endif
            strcpy(name, drive);
            strcpy(path, drive);
            strcat(name, gMenuContext->m32C_memoryCardContext->m4FCE_saveName);
            strcat(name, number);
            strcat(path, "__tmp_file");
            processLoadSaveMenuSub5(0x32);
            gMenuContext->m32A_soundEnabled = 0;
            func_800405B4(path);
            retries = 3;
            do {
                fd = func_80040534(path, (gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m3_blockNumber << 16) | 0x200);
                if (fd == -1) {
                    fd = 0;
                    makeSaveBlockDisplay(port);
                } else {
                    handle = fd;
                }
                if (fd != 0) {
                    break;
                }
            } while (--retries != 0);
            func_80040564(handle);
            if (fd != 0) {
                retries = 3;
                do {
                    fd = func_80040534(path, 2);
                    if (fd == -1) {
                        fd = 0;
                        makeSaveBlockDisplay(port);
                    }
                    if (fd != 0) {
                        goto opened;
                    }
                } while (--retries != 0);
                func_800405B4(path);
            opened:
                menuDraw();
                if (fd != 0) {
#if VERSION_JP
                    buildSaveTitle();
#else
                    buildSaveTitle(number2);
#endif
                    retries = 3;
                    do {
                        if (func_80040554(fd, &gMenuContext->m32C_memoryCardContext->m4B94_saveHeader, 0x100) == -1) {
                            fd = 0;
                            makeSaveBlockDisplay(port);
                        }
                        if (fd != 0) {
                            goto written;
                        }
                    } while (--retries != 0);
                    func_80040564(fd);
                    func_800405B4(path);
                    goto end;
                writeFail:
                    func_80040564(handle);
                    strcpy(path, drive);
                    strcat(path, "__tmp_file");
                    func_800405B4(path);
                    goto renamed;
                written:
                    offset = 0x100;
                    buffer = allocateHeap(PC_HEAP_BYTES(0x1F00), 1);
                    bzero(buffer, 0x1F00);
                    p = buffer;
#if VERSION_JP
                    buildSaveSummary((MenuSaveSummary*)buffer);
#else
                    buildSaveSummary((MenuSaveSummary*)buffer, port, number2);
#endif
                    pcGameIdSaveTail(buffer); /* the game's id into the free tail, under the sum */
                    q = buffer;
                    sum = 0;
                    for (i = 0; i < 0x1EFF; i++) {
                        sum += *q++;
                    }
                    *q = sum;
                    updateMemoryCardProgressBox(1);
                    do {
                        menuDraw();
                        retries = 3;
                        while (1) {
                            if (func_80040554(fd, p, 0x100) != 0x100) {
                                fd = 0;
                                makeSaveBlockDisplay(port);
                            }
                            if (fd != 0) {
                                break;
                            }
                            if (--retries == 0) {
                                goto writeFail;
                            }
                        }
                        offset += 0x100;
                        p += 0x100;
                    } while (offset < gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m3_blockNumber << 13);
                    func_80040564(fd);
                    strcpy(path, drive);
                    strcat(path, "__tmp_file");
                    retries = 3;
                    do {
                        if (func_800405A4(path, name) == 0) {
                            fd = 0;
                            makeSaveBlockDisplay(port);
                        }
                        if (fd != 0) {
                            goto renamed;
                        }
                    } while (--retries != 0);
                    func_800405B4(path);
                renamed:
                    freeHeap(buffer);
                }
            }
        end:
            processLoadSaveMenuSub7();
            updateMemoryCardProgressBox(2);
            if (fd != 0) {
                gMenuContext->m32A_soundEnabled = 1;
                playMenuSoundEffect(0x34);
                gMenuContext->m32A_soundEnabled = 0;
                askYesNoQuestion(0x5C, 0xFF, 0);
            } else {
                askYesNoQuestion(0x35, 0xFF, 0);
            }
            updateMemoryCardProgressBox(0);
            gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 2;
            while (gMenuContext->m326_cardPollTimer != 1) {
                menuDraw();
            }
            gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[0] = 0;
            gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[1] = 0;
        done:
            gMenuContext->m428_cardCursors->m144_followTile[0] = 1;
            gMenuContext->m32A_soundEnabled = 1;
            if (arg0 != 0) {
                break;
            }
            /* fallthrough */
        case 2:
            loop = 0;
            break;
        }
    } while (loop);
    gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 1;
    gMenuContext->m334_cardOperation = 1;
    return result;
}

/* copy a save to the other card */
u8 copySaveToOtherCard(void) {
    char name[0x40];
    char dstDrive[6];
    char srcPath[0x40];
    char tmpPath[0x40];
    char srcDrive[6];
    u8 buffer[0x200];
    u8 header[0x200];
    s32 srcPort;
    u8 first;
    u8 result;
    u8 full;
    s32 loop;
    u8 ok;
    s32 dst;
    u8 retries;
    s32 phase;
    s32 msg2;
    s32 msg3;
    s32 msg;
    s32 fd;
    s32 srcFd;
    s32 dstFd;
    s32 offset;
    s32 i;
    s32 j;

    (void)&srcPort;
    full = 0;
    first = 1;
    loop = 1;
    result = 0;
    resetSaveSlotCursor();
    do {
        if (processLoadSaveMenuSub6() != 0) {
            result = 1;
            break;
        }
        if (first) {
            if (gMenuContext->m33C_drawFlags->m33_cardMessageShown != 0) {
                processLoadSaveMenuSub7();
            }
            first = 0;
            gMenuContext->m428_cardCursors->m140_enabled[0] = 1;
        }
        if (isSaveSlotUsable(0) == 0) {
            s32 slot = findFirstUsableSaveSlot(0);
            gMenuContext->m32C_memoryCardContext->m4F7C_cursor = slot;
            if (slot == 0xFF) {
                gMenuContext->m428_cardCursors->m140_enabled[0] = 0;
                gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 0;
                askYesNoQuestion(0x62, 0xFF, 0);
                break;
            }
        }
        gMenuContext->m33C_drawFlags->m2F_draw428 = 1;
        switch (updateSaveSlotCursorInput(0)) {
        case 1:
            gMenuContext->m428_cardCursors->m144_followTile[0] = 0;
            gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 0;
            dst = gMenuContext->m32C_memoryCardContext->m4F7C_cursor / 15 == 0;
            ok = 1;
            if (dst == 0) {
                msg = 0x47;
                msg2 = 0x4A;
                msg3 = 0x4D;
            } else {
                msg = 0xBB;
                msg2 = 0xBE;
                msg3 = 0xC1;
            }
            if (askYesNoQuestion(msg, 0xFF, 1) == 0) {
                ok = 0;
                gMenuContext->m32C_memoryCardContext->m4F80_previousCursor = 0xFF;
            }
            if (ok == 0) {
                goto done;
            }
            if (gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[dst] == 0) {
                processLoadSaveMenuSub5(msg2);
                if (gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[dst] == 0) {
#if VERSION_JP
                    i = 0x3B;
#else
                    i = 0xB3;
#endif
                    do {
                        VSync(0);
                    } while (--i != 0);
                    ok = 0;
                    loop = 0;
                }
                processLoadSaveMenuSub7();
            }
            if (ok == 0) {
                goto done;
            }
            if (processLoadSaveMenuSub6Var2[dst] >= 15) {
                processLoadSaveMenuSub6Var2[dst] = 0;
                gMenuContext->m33C_drawFlags->m2F_draw428 = 0;
                askYesNoQuestion(0xAC, 0xFF, 0);
                /* after the call: sched1 moves these two call-crossing sets above it, between the argument loads */
                ok = 0;
                loop = 0;
            }
            if (ok == 0) {
                goto done;
            }
            dst = gMenuContext->m32C_memoryCardContext->m4F7C_cursor / 15 == 0;
            if (dst) {
                strcpy(srcDrive, "bu00:");
                strcpy(dstDrive, "bu10:");
                srcPort = 0;
            } else {
                strcpy(srcDrive, "bu10:");
                strcpy(dstDrive, "bu00:");
                srcPort = 1;
            }
            if (gMenuContext->m32C_memoryCardContext->m4F74_cardStatus[dst] == -2) {
                if (askFormatMemoryCard(dst) != 0) {
                    processLoadSaveMenuSub5(0x26);
                    if (func_80040574(dstDrive) != 0) {
                        processLoadSaveMenuSub7();
                        askYesNoQuestion(0x5C, 0xFF, 0);
                    } else {
                        processLoadSaveMenuSub7();
                        ok = 0;
                    }
                } else {
                    gMenuContext->m428_cardCursors->m144_followTile[0] = 1;
                    continue;
                }
            }
            if (ok == 0) {
                goto done;
            }
            for (i = 0; i < gMenuContext->m32C_memoryCardContext->m4F8A_fileCount[dst]; i++) {
                if (strcmp(gMenuContext->m32C_memoryCardContext->m0_files[gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]]].m18_name, gMenuContext->m32C_memoryCardContext->m0_files[dst * 16 + i].m18_name) == 0) {
                    i = 0xFF;
                    break;
                }
            }
            if (i == 0xFF) {
                askYesNoQuestion(msg3, 0xFF, 0);
                ok = 0;
            }
            if (ok == 0) {
                goto done;
            }
            processLoadSaveMenuSub5(0x41);
            gMenuContext->m32A_soundEnabled = 0;
            gMenuContext->m33C_drawFlags->m2F_draw428 = 0;
            gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 0;
            gMenuContext->m32C_memoryCardContext->m4F80_previousCursor = 0xFF;
            gMenuContext->m32C_memoryCardContext->m4F8C_loadedFileCount[0] = 0xFF;
            gMenuContext->m32C_memoryCardContext->m4F8C_loadedFileCount[1] = 0xFF;
            strcpy(srcPath, srcDrive);
            strcpy(name, dstDrive);
            strcpy(tmpPath, dstDrive);
            strcat(srcPath, gMenuContext->m32C_memoryCardContext->m0_files[gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]]].m18_name);
            strcat(name, gMenuContext->m32C_memoryCardContext->m0_files[gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]]].m18_name);
            strcat(tmpPath, "__tmp_file");
            retries = 3;
            do {
                fd = func_80040534(srcPath, 1);
                if (fd == -1) {
                    fd = 0;
                    makeSaveBlockDisplay(srcPort);
                }
                if (fd != 0) {
                    goto opened;
                }
            } while (--retries != 0);
            goto finish;
        opened:
            srcFd = fd;
            retries = 3;
            do {
                if (func_80040544(fd, header, 0x200) == -1) {
                    fd = 0;
                    makeSaveBlockDisplay(srcPort);
                }
                if (fd != 0) {
                    goto headerRead;
                }
            } while (--retries != 0);
            func_80040564(srcFd);
            goto finish;
        headerRead:
            func_800405B4(tmpPath);
            retries = 1;
            do {
                dstFd = func_80040534(tmpPath, (header[3] << 16) | 0x200);
                if (dstFd == -1) {
                    fd = 0;
                    full = 1;
                }
                if (fd != 0) {
                    goto created;
                }
            } while (--retries != 0);
            func_80040564(srcFd);
        created:
            func_80040564(dstFd);
            if (fd == 0) {
                goto finish;
            }
            retries = 3;
            do {
                dstFd = func_80040534(tmpPath, 2);
                if (dstFd == -1) {
                    fd = 0;
                    makeSaveBlockDisplay(dst);
                }
                if (fd != 0) {
                    goto dstOpened;
                }
            } while (--retries != 0);
            func_80040564(srcFd);
            func_800405B4(tmpPath);
            goto finish;
        copyFail:
            func_80040564(srcFd);
            func_80040564(dstFd);
            strcpy(tmpPath, dstDrive);
            strcat(tmpPath, "__tmp_file");
            func_800405B4(tmpPath);
            goto finish;
        dstOpened:
            retries = 3;
            do {
                if (func_80040554(dstFd, header, 0x200) == -1) {
                    fd = 0;
                    makeSaveBlockDisplay(dst);
                }
                if (fd != 0) {
                    goto headerWritten;
                }
            } while (--retries != 0);
            func_80040564(srcFd);
            func_80040564(dstFd);
            dstFd = 0;
            func_800405B4(tmpPath);
        headerWritten:
            offset = 0x200;
            phase = 0;
            updateMemoryCardProgressBox(1);
            while (1) {
                menuDraw();
                if (phase == 0) {
                    retries = 3;
                    do {
                        if (func_80040544(fd, buffer, 0x200) != 0x200) {
                            makeSaveBlockDisplay(srcPort);
                            fd = 0;
                        }
                        if (fd != 0) {
                            goto blockRead;
                        }
                    } while (--retries != 0);
                    goto copyFail;
                blockRead:
                    phase = 1;
                    continue;
                }
                retries = 3;
                do {
                    if (func_80040554(dstFd, buffer, 0x200) != 0x200) {
                        fd = 0;
                        makeSaveBlockDisplay(dst);
                    }
                    if (fd != 0) {
                        goto blockWritten;
                    }
                } while (--retries != 0);
                goto copyFail;
            blockWritten:
                phase = 0;
                offset += 0x200;
                if (offset >= header[3] << 13) {
                    break;
                }
            }
            func_80040564(dstFd);
            func_80040564(fd);
            strcpy(tmpPath, dstDrive);
            strcat(tmpPath, "__tmp_file");
            func_800405A4(tmpPath, name);
        finish:
            if (ok == 0) {
                goto done;
            }
            updateMemoryCardProgressBox(2);
            processLoadSaveMenuSub7();
            if (fd != 0) {
                gMenuContext->m32A_soundEnabled = 1;
                playMenuSoundEffect(0x34);
                gMenuContext->m32A_soundEnabled = 0;
                askYesNoQuestion(0x5C, 0xFF, 0);
            } else if (full == 0) {
                askYesNoQuestion(0x44, 0xFF, 0);
            } else {
                askYesNoQuestion(0xAC, 0xFF, 0);
            }
            updateMemoryCardProgressBox(0);
            gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 2;
            while (gMenuContext->m326_cardPollTimer != 1) {
                menuDraw();
            }
            for (j = 0; j < 32; j++) {
                gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[j] = 0xFF;
                gMenuContext->m32C_memoryCardContext->m4F8E_isGameSave[j] = 0;
                gMenuContext->m32C_memoryCardContext->m0_files[j].m58_loaded = 0;
            }
            gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[0] = 0;
            gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[1] = 0;
            needProcessLoadSaveMenuSub7 = 1;
        done:
            gMenuContext->m428_cardCursors->m144_followTile[0] = 1;
            gMenuContext->m32A_soundEnabled = 1;
            /* fallthrough */
        case 2:
            loop = 0;
            break;
        }
    } while (loop);
    gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 1;
    gMenuContext->m334_cardOperation = 1;
    return result;
}

/* delete-save loop (select, confirm, erase("buX0:name"), rescan) */
u8 deleteSaveFromMemoryCard(void) {
    char path[0x40];
    s32 i;
    u8 first = 1;
    s32 loop = 1;
    s32 result = 0;

    resetSaveSlotCursor();
    do {
        if (processLoadSaveMenuSub6() != 0) {
            result = 1;
            break;
        }
        if (first) {
            first = 0;
            if (gMenuContext->m33C_drawFlags->m33_cardMessageShown != 0) {
                processLoadSaveMenuSub7();
            }
            gMenuContext->m428_cardCursors->m140_enabled[0] = 1;
        }
        if (isSaveSlotUsable(0) == 0) {
            s32 slot = findFirstUsableSaveSlot(0);
            gMenuContext->m32C_memoryCardContext->m4F7C_cursor = slot;
            if (slot == 0xFF) {
                gMenuContext->m428_cardCursors->m140_enabled[0] = 0;
                gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 0;
                askYesNoQuestion(0x62, 0xFF, 0);
                break;
            }
        }
        gMenuContext->m33C_drawFlags->m2F_draw428 = 1;
        switch (updateSaveSlotCursorInput(0)) {
        case 1:
            gMenuContext->m428_cardCursors->m144_followTile[0] = 0;
            gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 0;
            if (askYesNoQuestion(0x56, 0x59, 1) != 0) {
                processLoadSaveMenuSub5(0x50);
                gMenuContext->m33C_drawFlags->m2F_draw428 = 0;
                gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 0;
                gMenuContext->m32C_memoryCardContext->m4F80_previousCursor = 0xFF;
                if (gMenuContext->m32C_memoryCardContext->m4F7C_cursor < 15) {
                    strcpy(path, "bu00:");
                } else {
                    strcpy(path, "bu10:");
                }
                strcat(path, gMenuContext->m32C_memoryCardContext->m0_files[gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]]].m18_name);
                func_800405B4(path);
                processLoadSaveMenuSub7();
                gMenuContext->m32A_soundEnabled = 1;
                playMenuSoundEffect(0x34);
                gMenuContext->m32A_soundEnabled = 0;
                askYesNoQuestion(0x5C, 0xFF, 0);
                gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 2;
                while (gMenuContext->m326_cardPollTimer != 1) {
                    menuDraw();
                }
                for (i = 0; i < 32; i++) {
                    gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[i] = 0xFF;
                    gMenuContext->m32C_memoryCardContext->m4F8E_isGameSave[i] = 0;
                    gMenuContext->m32C_memoryCardContext->m0_files[i].m58_loaded = 0;
                }
                gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[0] = 0;
                gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[1] = 0;
                gMenuContext->m32C_memoryCardContext->m4F8C_loadedFileCount[0] = 0xFF;
                gMenuContext->m32C_memoryCardContext->m4F8C_loadedFileCount[1] = 0xFF;
                needProcessLoadSaveMenuSub7 = 1;
            }
            gMenuContext->m428_cardCursors->m144_followTile[0] = 1;
            /* fallthrough */
        case 2:
            loop = 0;
            break;
        }
    } while (loop);
    gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 1;
    gMenuContext->m334_cardOperation = 1;
    gMenuContext->m32A_soundEnabled = 1;
    return result;
}

/* Noah updateSaveState (needs the u8 ok flag) */
u8 updateSaveState(u8 arg0) {
    u8 ok;

    setupDraw428(0);
    gMenuContext->m33C_drawFlags->m2F_draw428 = 0;
    ok = 1;
    switch (gMenuContext->m338_currentSelectedSubMenuEntry) {
    case 0:
        gMenuContext->m334_cardOperation = 7;
        if (deleteSaveFromMemoryCard() != 0) {
            ok = 0;
        }
        break;
    case 1:
        gMenuContext->m334_cardOperation = 6;
        if (copySaveToOtherCard() != 0) {
            ok = 0;
        }
        break;
    case 2:
        if (menuToEnter != 2) {
            gMenuContext->m334_cardOperation = 3;
            if (saveGameToMemoryCard(arg0) != 0) {
                ok = 0;
            }
        } else {
            gMenuContext->m334_cardOperation = 2;
            if (loadGameFromMemoryCard() != 0) {
                ok = 0;
            }
        }
        break;
    }
    disableDraw428();
    return ok;
}

/* 1E08 party panel: labels (partyCardLabelGlyphs glyphs), slot number, portrait */
void setupPartyCardLabels(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, u8 variant) {
    s32 i;

    p->mBE8_0Length = 0;
    for (i = 0; i < 9; i++) {
        if (partyCardLabelGlyphs[variant * 9 + i] != 0xFFFF) {
            p->mBE8_0Length += setupStringInPolyFT4(gMenuContext->m2DC_font, partyCardLabelGlyphs[variant * 9 + i], &p->m0_polys[p->mBE8_0Length * 2],
                                             gMenuContext->m308_oddOrEven, xTable[i], row * 56 + yTable[i], 0x1000);
        }
    }
    setupStringInPolyFT4(gMenuContext->m2DC_font, row + 0x14B, p->m460_poly, gMenuContext->m308_oddOrEven, xTable[9], row * 56 + yTable[9],
                  0x1000);
    initMemoryCardTransparentPoly(&p->m4B0_poly[gMenuContext->m308_oddOrEven]);
    p->m4B0_poly[gMenuContext->m308_oddOrEven].tpage = GetTPage(0, 0, 0x180, 0);
    if (variant == 0) {
        p->m4B0_poly[gMenuContext->m308_oddOrEven].clut = (slot & 1) ? textSpriteMode1 : textSpriteMode0;
    } else {
        p->m4B0_poly[gMenuContext->m308_oddOrEven].clut = (gameState.m26C_party[slot].mA0_gearNum & 1) ? textSpriteMode0 : textSpriteMode1;
    }
    setupMenuPolyFT4(&p->m4B0_poly[gMenuContext->m308_oddOrEven], xTable[16], yTable[16] + row * 56, infocardNameU[variant * 3 + row] * 4,
                  infocardNameV[variant * 3 + row], variant * 24 + 0x48, 13);
}

/* 1E08 party panel: level digits (m62_Level; m63_Level2 converted, unused) */
void setupPartyCardLevel(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable) {
    s32 i;

    fillStringDisplayBuffer(gameState.m26C_party[slot].m62_Level);
    p->mBE0_500Length = 0;
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[6 + i] != 0xFF) {
            p->mBE0_500Length += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[6 + i],
                                               &p->m500_polys[p->mBE0_500Length * 2], gMenuContext->m308_oddOrEven,
                                               i * 8 + xTable[10], row * 56 + yTable[10], 0x1000);
        }
    }
    fillStringDisplayBuffer(gameState.m26C_party[slot].m63_Level2);
    p->mBE1_5F0Length = 0;
}

/* 1E08 party panel: HP/MaxHP (gear HP when variant != 0), MP/MaxMP digits */
void setupPartyCardHpMp(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, u8 variant) {
    s32 i;
    s32 k;
    s32 digits;
    s32 start;

    if (variant == 0) {
        fillStringDisplayBuffer(gameState.m26C_party[slot].m4C_HP);
        digits = 3;
        start = 6;
    } else {
        fillStringDisplayBuffer(gameState.m978_gears[gameState.m26C_party[slot].mA0_gearNum].m60_hp);
        digits = 5;
        start = 4;
    }
    p->mBE2_6E0Length = 0;
    for (i = 0; i < digits; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[start + i] != 0xFF) {
            p->mBE2_6E0Length += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[start + i],
                                               &p->m6E0_polys[p->mBE2_6E0Length * 2], gMenuContext->m308_oddOrEven,
                                               i * 8 + xTable[12], row * 56 + yTable[12], 0x1000);
        }
    }
    if (variant == 0) {
        fillStringDisplayBuffer(gameState.m26C_party[slot].m4E_MaxHP);
    } else {
        fillStringDisplayBuffer(gameState.m978_gears[gameState.m26C_party[slot].mA0_gearNum].m64_maxHp);
    }
    k = 0;
    p->mBE3_870Length = 0;
    for (i = 0; i < digits; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[start + i] != 0xFF) {
            p->mBE3_870Length += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[start + i],
                                               &p->m870_polys[p->mBE3_870Length * 2], gMenuContext->m308_oddOrEven,
                                               k * 8 + xTable[13], row * 56 + yTable[13], 0x1000);
            k++;
        }
    }
    if (variant == 0) {
        fillStringDisplayBuffer(gameState.m26C_party[slot].m50_MP);
        p->mBE4_A00Length = 0;
        for (i = 0; i < 2; i++) {
            if (gMenuContext->m31C_stringDisplayBuffer[7 + i] != 0xFF) {
                p->mBE4_A00Length += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[7 + i],
                                                   &p->mA00_polys[p->mBE4_A00Length * 2], gMenuContext->m308_oddOrEven,
                                                   i * 8 + xTable[14], row * 56 + yTable[14], 0x1000);
            }
        }
        fillStringDisplayBuffer(gameState.m26C_party[slot].m52_MaxMP);
        p->mBE5_AF0Length = 0;
        for (i = 0, k = 0; i < 2; i++) {
            if (gMenuContext->m31C_stringDisplayBuffer[7 + i] != 0xFF) {
                p->mBE5_AF0Length += setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m31C_stringDisplayBuffer[7 + i],
                                                   &p->mAF0_polys[p->mBE5_AF0Length * 2], gMenuContext->m308_oddOrEven,
                                                   k * 8 + xTable[15], row * 56 + yTable[15], 0x1000);
                k++;
            }
        }
    }
}

/* 1E08 party panel setup (CD81C + CDB1C + CDC6C) */
void setupPartyCard(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, u8 variant) {
    setupPartyCardLabels(p, slot, row, xTable, yTable, variant);
    setupPartyCardLevel(p, slot, row, xTable, yTable);
    setupPartyCardHpMp(p, slot, row, xTable, yTable, variant);
    p->mBE7_enabled = 1;
    p->mBE6_oddOrEven = gMenuContext->m308_oddOrEven;
}

/* Noah transformAndAddPrim */
void transformAndAddPrim(s32 count, SVECTOR* verts, POLY_FT4* polys, s32 oddOrEven) {
    s32 i;
    s32 depth;
    s32 flag;

    for (i = 0; i < count; i++) {
        RotTransPers4(&verts[i * 4 + 0], &verts[i * 4 + 1], &verts[i * 4 + 2], &verts[i * 4 + 3],
                      (s32*)&polys[i * 2 + oddOrEven].x0, (s32*)&polys[i * 2 + oddOrEven].x1,
                      (s32*)&polys[i * 2 + oddOrEven].x2, (s32*)&polys[i * 2 + oddOrEven].x3, &depth, &flag);
        pcTextureClassDefault(&polys[i * 2 + oddOrEven], PC_TEX_TEXT);
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &polys[i * 2 + oddOrEven]);
    }
}

/* Noah drawMultiPolys */
void drawMultiPolys(s32 count, POLY_FT4* polys, s32 oddOrEven) {
    s32 i;

    for (i = 0; i < count; i++) {
        pcTextureClassDefault(&polys[i * 2 + oddOrEven], PC_TEX_TEXT);
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &polys[i * 2 + oddOrEven]);
    }
}

/* Noah drawMenu_348_cursor */
void drawMenu_348_cursor(void) {
#if VERSION_JP
    if (gMenuContext->m33C_drawFlags->m3_drawSelectionBox != 0) {
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m348_cursor->mC8_lines[gMenuContext->m348_cursor->m159_oddOrEven]);
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m348_cursor->mF8_lines[gMenuContext->m348_cursor->m159_oddOrEven]);
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m348_cursor->m50_polyG4[gMenuContext->m348_cursor->m159_oddOrEven]);
    }
#endif
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m348_cursor->m128_drawMode[gMenuContext->m348_cursor->m159_oddOrEven]);
    if (gMenuContext->m33C_drawFlags->m4_drawCursor != 0) {
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m348_cursor->m0_cursor[gMenuContext->m348_cursor->m158_oddOrEven]);
    }
}

/* Noah drawMenu_428 */
void drawMenu_428(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m2F_draw428 != 0) {
        for (i = 0; i < 4; i++) {
            if (gMenuContext->m428_cardCursors->m140_enabled[i] != 0) {
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                        &gMenuContext->m428_cardCursors->m0_polys[0][i * 2 + gMenuContext->m428_cardCursors->m148_oddOrEven[i]]);
            }
        }
    }
}

/* Noah displayGoldAndPlayTime */
void displayGoldAndPlayTime(void) {
    if (gMenuContext->m33C_drawFlags->m5_drawGold != 0) {
        MenuContext_340* gold = gMenuContext->m340_gold;
        drawMultiPolys(gold->m320_goldPolyLength, gold->m0_goldPolys[0], gold->m324_oddOrEven);
        MENU_TEXT_PRIM(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m340_gold->m2D0_goldUnit[gMenuContext->m340_gold->m324_oddOrEven]);
    }
    if (gMenuContext->m33C_drawFlags->m6_drawPlayTime != 0) {
        drawMultiPolys(7, gMenuContext->m344_playTime->m0_playTimePolys[0], gMenuContext->m344_playTime->m370_oddOrEven);
        drawMultiPolys(4, gMenuContext->m344_playTime->m230_separators[0], gMenuContext->m344_playTime->m370_oddOrEven);
    }
}

/* Noah drawCharacterInfoCards */
void drawCharacterInfoCards(void) {
    s32 i;
    MenuContext_39C* card;

    for (i = 0; i < 3; i++) {
        card = gMenuContext->m39C_infoCards[i];
        if (gMenuContext->m33C_drawFlags->m0_isInfoCardEnabled[i] != 0) {
            pcTextureClass(&card->m0_portraitPoly[card->m1270_oddOrEven], PC_TEX_MENU_PORTRAIT);
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &card->m0_portraitPoly[card->m1270_oddOrEven]);
            MENU_TEXT_PRIM(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &card->m50_NamePoly[card->m1270_oddOrEven]);
            drawMultiPolys(card->m1279_FixedStringLength, card->mA0_FixedStrings[0], card->m1270_oddOrEven);
            drawMultiPolys(card->m1273_HPLength, card->mAF0_PolyHP[0], card->m1270_oddOrEven);
            drawMultiPolys(card->m1274_MaxHPLength, card->mBE0_PolyMaxHP[0], card->m1270_oddOrEven);
            drawMultiPolys(card->m1275_MPLength, card->mCD0_PolyMP[0], card->m1270_oddOrEven);
            drawMultiPolys(card->m1276_MaxMPLength, card->mD70_PolyMaxMP[0], card->m1270_oddOrEven);
            drawMultiPolys(card->m1277_E10Length, card->mE10_polys[0], card->m1270_oddOrEven);
            drawMultiPolys(card->m1271_LevelLength, card->m910_LevelPolys[0], card->m1270_oddOrEven);
        }
    }
}

/* Noah drawCharacterInfoCardExtended (statusMenu.cpp) */
void drawCharacterInfoCardExtended(void) {
    if (gMenuContext->m33C_drawFlags->m7_drawCurrentSelectedCharacterInfoCard != 0) {
        { int k; for (k = 0; k < 1; ++k) pcTextureClass(&(gMenuContext->m358_selectedInfoCard->m0_portrait)[k * 2 + gMenuContext->m358_selectedInfoCard->m2AE0_oddOrEven], PC_TEX_MENU_PORTRAIT); }
        transformAndAddPrim(1, gMenuContext->m358_selectedInfoCard->m1EA0_portraitVerts, gMenuContext->m358_selectedInfoCard->m0_portrait, gMenuContext->m358_selectedInfoCard->m2AE0_oddOrEven);
        transformAndAddPrim(1, gMenuContext->m358_selectedInfoCard->m1EC0_nameVerts, gMenuContext->m358_selectedInfoCard->m50_name, gMenuContext->m358_selectedInfoCard->m2AE0_oddOrEven);
        { int k; for (k = 0; k < gMenuContext->m358_selectedInfoCard->m2AED_1E00Length; ++k) pcTextureClass(&(gMenuContext->m358_selectedInfoCard->m1E00_polys[0])[k * 2 + gMenuContext->m358_selectedInfoCard->m2AEE_1E00OddOrEven], PC_TEX_HUD); }
        transformAndAddPrim(gMenuContext->m358_selectedInfoCard->m2AED_1E00Length, gMenuContext->m358_selectedInfoCard->m2AA0_onGearIconVerts[0], gMenuContext->m358_selectedInfoCard->m1E00_polys[0], gMenuContext->m358_selectedInfoCard->m2AEE_1E00OddOrEven);
        transformAndAddPrim(gMenuContext->m358_selectedInfoCard->m2AEC_labelLength, gMenuContext->m358_selectedInfoCard->m1EE0_labelVerts[0], gMenuContext->m358_selectedInfoCard->mA0_labels[0], gMenuContext->m358_selectedInfoCard->m2AE0_oddOrEven);
        transformAndAddPrim(gMenuContext->m358_selectedInfoCard->m2AE7_hpLength, gMenuContext->m358_selectedInfoCard->m2780_hpVerts[0], gMenuContext->m358_selectedInfoCard->m1630_hp[0], gMenuContext->m358_selectedInfoCard->m2AE0_oddOrEven);
        transformAndAddPrim(gMenuContext->m358_selectedInfoCard->m2AE8_maxHpLength, gMenuContext->m358_selectedInfoCard->m2820_maxHpVerts[0], gMenuContext->m358_selectedInfoCard->m17C0_maxHp[0], gMenuContext->m358_selectedInfoCard->m2AE0_oddOrEven);
        transformAndAddPrim(gMenuContext->m358_selectedInfoCard->m2AE9_mpLength, gMenuContext->m358_selectedInfoCard->m28C0_mpVerts[0], gMenuContext->m358_selectedInfoCard->m1950_mp[0], gMenuContext->m358_selectedInfoCard->m2AE0_oddOrEven);
        transformAndAddPrim(gMenuContext->m358_selectedInfoCard->m2AEA_maxMpLength, gMenuContext->m358_selectedInfoCard->m2960_maxMpVerts[0], gMenuContext->m358_selectedInfoCard->m1AE0_maxMp[0], gMenuContext->m358_selectedInfoCard->m2AE0_oddOrEven);
        transformAndAddPrim(gMenuContext->m358_selectedInfoCard->m2AE1_levelLength, gMenuContext->m358_selectedInfoCard->m2300_levelVerts[0], gMenuContext->m358_selectedInfoCard->mAF0_level[0], gMenuContext->m358_selectedInfoCard->m2AE0_oddOrEven);
        transformAndAddPrim(gMenuContext->m358_selectedInfoCard->m2AE3_CD0Length, gMenuContext->m358_selectedInfoCard->m23C0_experienceVerts[0], gMenuContext->m358_selectedInfoCard->mCD0_experience[0], gMenuContext->m358_selectedInfoCard->m2AE0_oddOrEven);
        transformAndAddPrim(gMenuContext->m358_selectedInfoCard->m2AE5_xpLength, gMenuContext->m358_selectedInfoCard->m25C0_xpVerts[0], gMenuContext->m358_selectedInfoCard->m11D0_xp[0], gMenuContext->m358_selectedInfoCard->m2AE0_oddOrEven);
        transformAndAddPrim(gMenuContext->m358_selectedInfoCard->m2AEB_1C70Length, gMenuContext->m358_selectedInfoCard->m2A00_weightVerts[0], gMenuContext->m358_selectedInfoCard->m1C70_weight[0], gMenuContext->m358_selectedInfoCard->m2AE0_oddOrEven);
    }
}

/* Noah drawMenu_35CSub0 (statusMenu.cpp) */
void drawMenu_35CSub0(void) {
    s32 i;
    s32 depth;
    s32 flag;

    for (i = 0; i < 7; i++) {
        if (gMenuContext->m35C_selectedStats->m32EA_rowEnabled[i] != 0) {
            if (gMenuContext->m35C_selectedStats->m32F2_drawSecondGauge != 0) {
                RotTransPers4(&gMenuContext->m35C_selectedStats->m31E0_gaugeVertex[i][0], &gMenuContext->m35C_selectedStats->m31E0_gaugeVertex[i][1],
                              &gMenuContext->m35C_selectedStats->m31E0_gaugeVertex[i][2], &gMenuContext->m35C_selectedStats->m31E0_gaugeVertex[i][3],
                              (s32*)&gMenuContext->m35C_selectedStats->m2228_gauge[i][gMenuContext->m35C_selectedStats->m32E3_2228OddOrEven[i]].x0,
                              (s32*)&gMenuContext->m35C_selectedStats->m2228_gauge[i][gMenuContext->m35C_selectedStats->m32E3_2228OddOrEven[i]].x1,
                              (s32*)&gMenuContext->m35C_selectedStats->m2228_gauge[i][gMenuContext->m35C_selectedStats->m32E3_2228OddOrEven[i]].x2,
                              (s32*)&gMenuContext->m35C_selectedStats->m2228_gauge[i][gMenuContext->m35C_selectedStats->m32E3_2228OddOrEven[i]].x3, &depth,
                              &flag);
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                        &gMenuContext->m35C_selectedStats->m2228_gauge[i][gMenuContext->m35C_selectedStats->m32E3_2228OddOrEven[i]]);
                transformAndAddPrim(gMenuContext->m35C_selectedStats->m32CE_1770Length[i], gMenuContext->m35C_selectedStats->m2D80_vertex[i][0],
                              gMenuContext->m35C_selectedStats->m1770_polys[i][0], gMenuContext->m35C_selectedStats->m32D5_1770OddOrEven[i]);
            }
            RotTransPers4(&gMenuContext->m35C_selectedStats->m3100_gaugeVertex[i][0], &gMenuContext->m35C_selectedStats->m3100_gaugeVertex[i][1],
                          &gMenuContext->m35C_selectedStats->m3100_gaugeVertex[i][2], &gMenuContext->m35C_selectedStats->m3100_gaugeVertex[i][3],
                          (s32*)&gMenuContext->m35C_selectedStats->m2030_gauge[i][gMenuContext->m35C_selectedStats->m32DC_2030OddOrEven[i]].x0,
                          (s32*)&gMenuContext->m35C_selectedStats->m2030_gauge[i][gMenuContext->m35C_selectedStats->m32DC_2030OddOrEven[i]].x1,
                          (s32*)&gMenuContext->m35C_selectedStats->m2030_gauge[i][gMenuContext->m35C_selectedStats->m32DC_2030OddOrEven[i]].x2,
                          (s32*)&gMenuContext->m35C_selectedStats->m2030_gauge[i][gMenuContext->m35C_selectedStats->m32DC_2030OddOrEven[i]].x3, &depth,
                          &flag);
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                    &gMenuContext->m35C_selectedStats->m2030_gauge[i][gMenuContext->m35C_selectedStats->m32DC_2030OddOrEven[i]]);
            transformAndAddPrim(gMenuContext->m35C_selectedStats->m32C0_EB0Length[i], gMenuContext->m35C_selectedStats->m2A00_vertex[i][0],
                          gMenuContext->m35C_selectedStats->mEB0_polys[i][0], gMenuContext->m35C_selectedStats->m32C7_EB0OddOrEven[i]);
        }
    }
}

/* Noah drawMenu_35C (statusMenu.cpp) */
void drawMenu_35C(void) {
    if (gMenuContext->m33C_drawFlags->m8_drawCurrentSelectedCharacterStats != 0) {
        transformAndAddPrim(gMenuContext->m35C_selectedStats->m32F3_0Length, gMenuContext->m35C_selectedStats->m2420_vertex[0], gMenuContext->m35C_selectedStats->m0_polys[0],
                      gMenuContext->m35C_selectedStats->m32F1_oddOrEven);
        drawMenu_35CSub0();
    }
}

/* Noah drawMenu_360 */
void drawMenu_360(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m4B_draw360 != 0) {
        for (i = 0; i < 5; i++) {
            if (gMenuContext->m360_equipment->m294_nameEnabled[i] != 0) {
                transformAndAddPrim(1, gMenuContext->m360_equipment->m0_names[i].m50_vertex, gMenuContext->m360_equipment->m0_names[i].m0_polys, gMenuContext->m360_equipment->m299_oddOrEven);
            }
        }
    }
}

/* Noah drawMainMenu */
void drawMainMenu(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m9_drawMainMenu != 0) {
        if (gMenuContext->m350_mainMenu->m1192_dimmed != gMenuContext->m350_mainMenu->m1193_appliedDimmed) {
            if (gMenuContext->m350_mainMenu->m1192_dimmed != 0) {
                for (i = 0; i < gMenuContext->m350_mainMenu->m118C_numEntries; i++) {
                    SetSemiTrans(&gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven], 1);
                    SetShadeTex(&gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven], 0);
                    gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven].tpage |= 0x20;
                    setRGB0(&gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven], 0x20, 0x20, 0x20);
                }
                for (i = 0; i < gMenuContext->m350_mainMenu->m1188_numEntries2; i++) {
                    SetSemiTrans(&gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven], 1);
                    SetShadeTex(&gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven], 0);
                    gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven].tpage |= 0x20;
                    setRGB0(&gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven], 0x20, 0x20, 0x20);
                }
            } else {
                for (i = 0; i < gMenuContext->m350_mainMenu->m118C_numEntries; i++) {
                    SetSemiTrans(&gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven], 0);
                    SetShadeTex(&gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven], 0);
                    gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven].tpage |= 0x20;
                    setRGB0(&gMenuContext->m350_mainMenu->m8C0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1191_8C0OddOrEven], 0x80, 0x80, 0x80);
                }
                for (i = 0; i < gMenuContext->m350_mainMenu->m1188_numEntries2; i++) {
                    SetSemiTrans(&gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven], 0);
                    SetShadeTex(&gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven], 0);
                    gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven].tpage |= 0x20;
                    setRGB0(&gMenuContext->m350_mainMenu->m0_polys[0][i * 2 + gMenuContext->m350_mainMenu->m1190_0OddOrEven], 0x80, 0x80, 0x80);
                }
            }
            gMenuContext->m350_mainMenu->m1193_appliedDimmed = gMenuContext->m350_mainMenu->m1192_dimmed;
        }
        for(i=0;i<gMenuContext->m350_mainMenu->m118C_numEntries;i++)
            pcTextureClass(&gMenuContext->m350_mainMenu->m8C0_polys[0][i*2+gMenuContext->m350_mainMenu->m1191_8C0OddOrEven],PC_TEX_HUD);
        for(i=0;i<gMenuContext->m350_mainMenu->m1188_numEntries2;i++)
            pcTextureClass(&gMenuContext->m350_mainMenu->m0_polys[0][i*2+gMenuContext->m350_mainMenu->m1190_0OddOrEven],PC_TEX_HUD);
        drawMultiPolys(gMenuContext->m350_mainMenu->m118C_numEntries, gMenuContext->m350_mainMenu->m8C0_polys[0], gMenuContext->m350_mainMenu->m1191_8C0OddOrEven);
        drawMultiPolys(gMenuContext->m350_mainMenu->m1188_numEntries2, gMenuContext->m350_mainMenu->m0_polys[0], gMenuContext->m350_mainMenu->m1190_0OddOrEven);
    }
}

/* Noah drawMenuContext354 */
void drawMenuContext354(void) {
    if (gMenuContext->m33C_drawFlags->mA_354Enabled != 0) {
        drawMultiPolys(gMenuContext->m354_subMenu->m1404_500Length, gMenuContext->m354_subMenu->m500_polys[0], gMenuContext->m354_subMenu->m1409_500OddOrEven);
        drawMultiPolys(gMenuContext->m354_subMenu->m1400_0Length, gMenuContext->m354_subMenu->m0_polys[0], gMenuContext->m354_subMenu->m1408_0OddOrEven);
    }
}

/* Noah drawMenu_4D8_is_2 (selected memory card tile flash) */
void drawMenu_4D8_is_2(void) {
    s32 i;
    u8 slot;
    u8 draw;
    MenuContext_3A8* tile;
    s32 depth;
    s32 flag;

    if (gMenuContext->m4D8_cardTilesMode == 2) {
        slot = gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]];
        for (i = 0; i < 0x20; i++) {
            draw = 1;
            if (slot == gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[i]) {
                if (slot == 0xFF) {
                    draw = i == saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor];
                }
                if (draw) {
                    tile = gMenuContext->m3A8_memoryCardTiles[i];
                    setRGB0(&tile->mB0_memoryCardOpaquePolys[gMenuContext->m308_oddOrEven], gMenuContext->m4D4_fadeColor,
                            gMenuContext->m4D4_fadeColor, gMenuContext->m4D4_fadeColor);
                    RotTransPers4(&tile->mE0_vertices[0], &tile->mE0_vertices[1], &tile->mE0_vertices[2], &tile->mE0_vertices[3],
                                  (s32*)&tile->mB0_memoryCardOpaquePolys[gMenuContext->m308_oddOrEven].x0,
                                  (s32*)&tile->mB0_memoryCardOpaquePolys[gMenuContext->m308_oddOrEven].x1,
                                  (s32*)&tile->mB0_memoryCardOpaquePolys[gMenuContext->m308_oddOrEven].x2,
                                  (s32*)&tile->mB0_memoryCardOpaquePolys[gMenuContext->m308_oddOrEven].x3, &depth, &flag);
                    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &tile->mB0_memoryCardOpaquePolys[gMenuContext->m308_oddOrEven]);
                    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &tile->m140_drawMode[gMenuContext->m308_oddOrEven]);
                }
            }
        }
    }
}

/* Noah drawMenuMemoryCard_sub0 (save icons on tiles) */
void drawMenuMemoryCard_sub0(s32 start, s32 firstTile, s32 end) {
    s32 i;
    s32 j;
    s32 tileIndex;
    MenuContext_3A8* tile;

    for (i = start, tileIndex = firstTile; i < end; i++) {
        if (gMenuContext->m32C_memoryCardContext->m0_files[i].m58_loaded != 0) {
            for (j = 0; j < gMenuContext->m32C_memoryCardContext->mB94_fileHeaders[i].m3_blockNumber; j++, tileIndex++) {
                tile = gMenuContext->m3A8_memoryCardTiles[tileIndex];
                setUV4(&tile->m0_memoryCardTransparentPolys[gMenuContext->m308_oddOrEven],
                       i * 16, gMenuContext->m32C_memoryCardContext->m0_files[i].m0_iconV[gMenuContext->m4CC_iconFrame],
                       i * 16 + 16, gMenuContext->m32C_memoryCardContext->m0_files[i].m0_iconV[gMenuContext->m4CC_iconFrame],
                       i * 16, gMenuContext->m32C_memoryCardContext->m0_files[i].m0_iconV[gMenuContext->m4CC_iconFrame] + 16,
                       i * 16 + 16, gMenuContext->m32C_memoryCardContext->m0_files[i].m0_iconV[gMenuContext->m4CC_iconFrame] + 16);
                tile->m0_memoryCardTransparentPolys[gMenuContext->m308_oddOrEven].clut = GetClut(i * 16, i / 16 + 0x1C1);
                { int k; for (k = 0; k < 1; ++k) pcTextureClass(&(tile->m0_memoryCardTransparentPolys)[k * 2 + gMenuContext->m308_oddOrEven], PC_TEX_HUD); }
                transformAndAddPrim(1, tile->mE0_vertices, tile->m0_memoryCardTransparentPolys, gMenuContext->m308_oddOrEven);
            }
        }
    }
}

/* Noah drawMenu_4D8_text (memoryCardTiles.cpp) */
void drawMenu_4D8_text(void) {
    u8 hasData[2];
    s32 i;
    s32 j;
    s32 count;
    u8 valid;

    hasData[1] = 0;
    hasData[0] = 0;
    if (gMenuContext->m4D8_cardTilesMode != 0) {
        for (i = 0; i < 2; i++) {
            if (gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[i] != 0 && gMenuContext->m33C_drawFlags->m68_drawCardLabels != 0) {
                valid = 1;
                if (gMenuContext->m33C_drawFlags->m2F_draw428 != 0 && i == saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor] / 16) {
                    valid = 0;
                }
                if (valid) {
                    setupStringInPolyFT4(gMenuContext->m2DC_font, 0x115, gMenuContext->m32C_memoryCardContext->m4D94_cardLabels[i].m0_label,
                                  gMenuContext->m308_oddOrEven, 0x1E + i * 0x90, 0x36, 0x1000);
                } else {
                    setupStringInPolyFT4(gMenuContext->m2DC_font, 0x122, gMenuContext->m32C_memoryCardContext->m4D94_cardLabels[i].m0_label,
                                  gMenuContext->m308_oddOrEven, 0x1E + i * 0x90, 0x36, 0x1000);
                }
                count = setupStringInPolyFT4(gMenuContext->m2DC_font, i + 0x162,
                                      gMenuContext->m32C_memoryCardContext->m4D94_cardLabels[i].m50_number,
                                      gMenuContext->m308_oddOrEven, 0x1B + i * 0x90, 0x36, 0x1000);
                hasData[i] = 1;
            }
            if (hasData[i] != 0) {
                for (j = 0; j < count; j++) {
                    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                            &gMenuContext->m32C_memoryCardContext->m4D94_cardLabels[i].m50_number[j * 2 + gMenuContext->m308_oddOrEven]);
                }
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                        &gMenuContext->m32C_memoryCardContext->m4D94_cardLabels[i].m0_label[gMenuContext->m308_oddOrEven]);
            }
        }
    }
}

/* Noah drawMenu_memoycard_tilesLines (memoryCardTiles.cpp) */
void drawMenu_memoycard_tilesLines(void) {
    s32 i;
    u8 slot;
    u8 selected;
    MenuContext_3A8* tile;
    MenuContext_32C* card;
    s32 depth;
    s32 flag;

    if (gMenuContext->m4D8_cardTilesMode != 0) {
        slot = gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]];
        for (i = 0; i < 0x20; i++) {
            tile = gMenuContext->m3A8_memoryCardTiles[i];
            card = gMenuContext->m32C_memoryCardContext;
            if (card->m4FE4_cardPresent[i / 16] != 0 && (i / 16) * 16 != i - 15) {
                selected = 0;
                if (slot == card->m4FAE_tileFiles[i] && gMenuContext->m4D8_cardTilesMode == 2) {
                    selected = 1;
                    if (slot == 0xFF) {
                        selected = i == saveCursorToSlot[card->m4F7C_cursor];
                    }
                }
                if (selected) {
                    setRGB0(&tile->m50_topRightLine[gMenuContext->m308_oddOrEven], 0xFF, 0, 0);
                    setRGB0(&tile->m80_leftBottomLine[gMenuContext->m308_oddOrEven], 0xFF, 0, 0);
                } else {
                    setRGB0(&tile->m50_topRightLine[gMenuContext->m308_oddOrEven], 0, 0xFF, 0);
                    setRGB0(&tile->m80_leftBottomLine[gMenuContext->m308_oddOrEven], 0, 0xFF, 0);
                }
                RotTransPers3(&tile->m100_topRightVerts[0], &tile->m100_topRightVerts[1], &tile->m100_topRightVerts[3], (s32*)&tile->m50_topRightLine[gMenuContext->m308_oddOrEven].x0,
                              (s32*)&tile->m50_topRightLine[gMenuContext->m308_oddOrEven].x1, (s32*)&tile->m50_topRightLine[gMenuContext->m308_oddOrEven].x2,
                              &depth, &flag);
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &tile->m50_topRightLine[gMenuContext->m308_oddOrEven]);
                RotTransPers3(&tile->m120_leftBottomVerts[0], &tile->m120_leftBottomVerts[2], &tile->m120_leftBottomVerts[3], (s32*)&tile->m80_leftBottomLine[gMenuContext->m308_oddOrEven].x0,
                              (s32*)&tile->m80_leftBottomLine[gMenuContext->m308_oddOrEven].x1, (s32*)&tile->m80_leftBottomLine[gMenuContext->m308_oddOrEven].x2,
                              &depth, &flag);
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &tile->m80_leftBottomLine[gMenuContext->m308_oddOrEven]);
            }
        }
    }
}

/* Noah drawMenuMemoryCard_52 (progress bar) */
void drawMenuMemoryCard_52(void) {
    MenuContext* ctx = gMenuContext;
    s32 width;

    if (ctx->m33C_drawFlags->m52_draw44C != 0) {
        setXY4(&ctx->m44C_progressBox->m0_bar[ctx->m44C_progressBox->m7B8_oddOrEven], 0x20, 0x61, ctx->m44C_progressBox->m7B0_width + 0x20, 0x61, 0x20, 0x68,
               ctx->m44C_progressBox->m7B0_width + 0x20, 0x68);
        if (ctx->m2D8_frameCounter % 6 >= 4 || ctx->m33C_drawFlags->m52_draw44C == 2) {
            drawMultiPolys(2, ctx->m44C_progressBox->m3F0_line2[0], ctx->m44C_progressBox->m7B8_oddOrEven);
        }
        drawMultiPolys(12, gMenuContext->m44C_progressBox->m30_line1[0], gMenuContext->m44C_progressBox->m7B8_oddOrEven);
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m44C_progressBox->m0_bar[gMenuContext->m44C_progressBox->m7B8_oddOrEven]);
        if (gMenuContext->m33C_drawFlags->m52_draw44C == 1) {
            width = gMenuContext->m44C_progressBox->m7B0_width + gMenuContext->m44C_progressBox->m7B4_speed;
            gMenuContext->m44C_progressBox->m7B0_width = width;
            if (width > 0x100) {
                gMenuContext->m44C_progressBox->m7B0_width = 0;
            }
        }
    }
}

/* Noah drawMenuMemoryCard */
void drawMenuMemoryCard(void) {
    drawMenu_4D8_is_2();
    drawMenuMemoryCard_sub0(0, 0, 0x10);
    drawMenuMemoryCard_sub0(0x10, 0x10, 0x20);
    drawMenu_memoycard_tilesLines();
    drawMenu_4D8_text();
    drawMenuMemoryCard_52();
    if (++gMenuContext->m4D0_iconFrameTimer == 0xF) {
        gMenuContext->m4D0_iconFrameTimer = 0;
        if (++gMenuContext->m4CC_iconFrame == 6) {
            gMenuContext->m4CC_iconFrame = 0;
        }
    }
    if (gMenuContext->m4D9_fadeDirection == 0) {
        gMenuContext->m4D4_fadeColor += 4;
        if (gMenuContext->m4D4_fadeColor > 0x80) {
            gMenuContext->m4D9_fadeDirection = 1;
            gMenuContext->m4D4_fadeColor = 0x7C;
        }
    } else {
        gMenuContext->m4D4_fadeColor -= 4;
        if (gMenuContext->m4D4_fadeColor < 0) {
            gMenuContext->m4D9_fadeDirection = 0;
            gMenuContext->m4D4_fadeColor = 4;
        }
    }
}

/* Noah drawMenu_34C (save/load file list) */
void drawMenu_34C(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->mB_drawSaveScreen != 0) {
        if (gMenuContext->m34C_saveScreen->m2DBC_showSummary == 0) {
            drawMultiPolys(0x20, gMenuContext->m34C_saveScreen->m0_polys[0], gMenuContext->m308_oddOrEven);
            setUV4(&gMenuContext->m34C_saveScreen->mA00_icon[gMenuContext->m308_oddOrEven],
                   gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] * 16,
                   gMenuContext->m32C_memoryCardContext
                       ->m0_files[gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]]]
                       .m0_iconV[gMenuContext->m4CC_iconFrame],
                   gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] * 16 + 15,
                   gMenuContext->m32C_memoryCardContext
                       ->m0_files[gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]]]
                       .m0_iconV[gMenuContext->m4CC_iconFrame],
                   gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] * 16,
                   gMenuContext->m32C_memoryCardContext
                           ->m0_files[gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]]]
                           .m0_iconV[gMenuContext->m4CC_iconFrame] + 15,
                   gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] * 16 + 15,
                   gMenuContext->m32C_memoryCardContext
                           ->m0_files[gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]]]
                           .m0_iconV[gMenuContext->m4CC_iconFrame] + 15);
            gMenuContext->m34C_saveScreen->mA00_icon[gMenuContext->m308_oddOrEven].clut =
                GetClut(gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] * 16,
                        gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[saveCursorToSlot[gMenuContext->m32C_memoryCardContext->m4F7C_cursor]] / 16 + 0x1C1);
            AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m34C_saveScreen->mA00_icon[gMenuContext->m308_oddOrEven]);
        } else {
            for (i = 0; i < 3; i++) {
                if (gMenuContext->m34C_saveScreen->mA98_slots[i].m878_enabled != 0) {
                    drawMultiPolys(gMenuContext->m34C_saveScreen->mA98_slots[i].m87A_labelLength, gMenuContext->m34C_saveScreen->mA98_slots[i].m50_label[0],
                                  gMenuContext->m34C_saveScreen->mA98_slots[i].m876_oddOrEven);
                    drawMultiPolys(gMenuContext->m34C_saveScreen->mA98_slots[i].m870_levelLength, gMenuContext->m34C_saveScreen->mA98_slots[i].m320_level[0],
                                  gMenuContext->m34C_saveScreen->mA98_slots[i].m877_oddOrEven);
                    drawMultiPolys(gMenuContext->m34C_saveScreen->mA98_slots[i].m871_level2Length, gMenuContext->m34C_saveScreen->mA98_slots[i].m410_level2[0],
                                  gMenuContext->m34C_saveScreen->mA98_slots[i].m877_oddOrEven);
                    drawMultiPolys(gMenuContext->m34C_saveScreen->mA98_slots[i].m872_hpLength, gMenuContext->m34C_saveScreen->mA98_slots[i].m500_hp[0],
                                  gMenuContext->m34C_saveScreen->mA98_slots[i].m877_oddOrEven);
                    drawMultiPolys(gMenuContext->m34C_saveScreen->mA98_slots[i].m873_maxHpLength, gMenuContext->m34C_saveScreen->mA98_slots[i].m5F0_maxHp[0],
                                  gMenuContext->m34C_saveScreen->mA98_slots[i].m877_oddOrEven);
                    drawMultiPolys(gMenuContext->m34C_saveScreen->mA98_slots[i].m874_mpLength, gMenuContext->m34C_saveScreen->mA98_slots[i].m6E0_mp[0],
                                  gMenuContext->m34C_saveScreen->mA98_slots[i].m877_oddOrEven);
                    drawMultiPolys(gMenuContext->m34C_saveScreen->mA98_slots[i].m875_maxMpLength, gMenuContext->m34C_saveScreen->mA98_slots[i].m780_maxMp[0],
                                  gMenuContext->m34C_saveScreen->mA98_slots[i].m877_oddOrEven);
                    /* retail bug: every slot's name uses slot 0's m877_oddOrEven */
                    pcTextureClass(&gMenuContext->m34C_saveScreen->mA98_slots[i].m0_name[gMenuContext->m34C_saveScreen->mA98_slots[0].m877_oddOrEven], PC_TEX_MENU_THUMB_PORTRAIT);
                    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                            &gMenuContext->m34C_saveScreen->mA98_slots[i].m0_name[gMenuContext->m34C_saveScreen->mA98_slots[0].m877_oddOrEven]);
                    MENU_TEXT_PRIM(&gMenuContext->m1D4_currentDrawContext->m70_ot[4],
                            &gMenuContext->m34C_saveScreen->mA98_slots[i].m820_nameImage[gMenuContext->m34C_saveScreen->mA98_slots[i].m879_oddOrEven]);
                }
            }
            drawMultiPolys(11, gMenuContext->m34C_saveScreen->m240C_separator1[0], gMenuContext->m34C_saveScreen->mA98_slots[0].m877_oddOrEven);
#if !VERSION_JP
            drawMultiPolys(4, gMenuContext->m34C_saveScreen->m2C7C_slotLabel1, gMenuContext->m34C_saveScreen->mA98_slots[0].m877_oddOrEven);
#endif
            drawMultiPolys(16, gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0], gMenuContext->m34C_saveScreen->mA98_slots[0].m877_oddOrEven);
        }
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m34C_saveScreen->mA50_polyG4[gMenuContext->m308_oddOrEven]);
    }
}

/* Noah transformAndDrawPolyFT4 */
void transformAndDrawPolyFT4(SVECTOR* verts, POLY_FT4* polys, s32 oddOrEven, s32 otIndex) {
    POLY_FT4* p;
    s32 depth;
    s32 flag;

    p = &polys[oddOrEven];
    RotTransPers4(&verts[0], &verts[1], &verts[2], &verts[3], (s32*)&p->x0, (s32*)&p->x1, (s32*)&p->x2, (s32*)&p->x3,
                  &depth, &flag);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[otIndex], p);
}

/* Noah drawMenuContext364_menuBox */
void drawMenuContext364_menuBox(s32 box, u8 drawInner) {
    MenuContext_364* b;
    s32 i;
    s32 depth;
    s32 flag;

    b = gMenuContext->m364_menuBoxes[box];
    for (i = 0; i < 4; i++) {
        transformAndDrawPolyFT4(b->m510_cornerVerts[i], b->m0_corners[i], b->m71C_oddOrEven, b->m718_otIndex);
    }
    if (drawInner != 0) {
        for (i = 0; i < 2; i++) {
            transformAndDrawPolyFT4(b->m6D0_innerCornerVerts[i], b->m410_innerCorners[i], b->m71C_oddOrEven, b->m718_otIndex);
        }
        transformAndDrawPolyFT4(b->m6B0_innerEdgeVerts, b->m3C0_innerEdge, b->m71C_oddOrEven, b->m718_otIndex);
    }
    transformAndDrawPolyFT4(b->m590_topEdgeLeftVerts, b->m140_topEdge[0], b->m71C_oddOrEven, b->m718_otIndex);
    transformAndDrawPolyFT4(b->m5B0_topEdgeRightVerts, b->m140_topEdge[1], b->m71C_oddOrEven, b->m718_otIndex);
    transformAndDrawPolyFT4(b->m5D0_bottomEdgeLeftVerts, b->m1E0_bottomEdge[0], b->m71C_oddOrEven, b->m718_otIndex);
    transformAndDrawPolyFT4(b->m5F0_bottomEdgeRightVerts, b->m1E0_bottomEdge[1], b->m71C_oddOrEven, b->m718_otIndex);
    transformAndDrawPolyFT4(b->m610_leftEdgeTopVerts, b->m280_leftEdge[0], b->m71C_oddOrEven, b->m718_otIndex);
    transformAndDrawPolyFT4(b->m630_leftEdgeBottomVerts, b->m280_leftEdge[1], b->m71C_oddOrEven, b->m718_otIndex);
    transformAndDrawPolyFT4(b->m650_rightEdgeTopVerts, b->m320_rightEdge[0], b->m71C_oddOrEven, b->m718_otIndex);
    transformAndDrawPolyFT4(b->m670_rightEdgeBottomVerts, b->m320_rightEdge[1], b->m71C_oddOrEven, b->m718_otIndex);
    RotTransPers4(&b->m690_backgroundVerts[0], &b->m690_backgroundVerts[1], &b->m690_backgroundVerts[2], &b->m690_backgroundVerts[3], (s32*)&b->m4B0_background[b->m71C_oddOrEven].x0,
                  (s32*)&b->m4B0_background[b->m71C_oddOrEven].x1, (s32*)&b->m4B0_background[b->m71C_oddOrEven].x2,
                  (s32*)&b->m4B0_background[b->m71C_oddOrEven].x3, &depth, &flag);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m4B0_background[b->m71C_oddOrEven]);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[b->m718_otIndex], &b->m4F8_drawMode[b->m71C_oddOrEven]);
}

/* Noah drawMenuBoxes (unused 8-byte local for the frame) */
void drawMenuBoxes(void) {
    s32 i;
    SVECTOR rot;
    VECTOR trans;
    MATRIX m;
    SVECTOR unused; /* retail frame has 8 more bytes */

    for (i = 0; i < 7; i++) {
        if (gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[i] != 0) {
            if (gMenuContext->m364_menuBoxes[i]->m714_transformDisabled == 0) {
                PushMatrix();
                rot.vz = 0;
                rot.vy = 0;
                rot.vx = 0;
                trans.vy = 0;
                trans.vx = 0;
                trans.vz = 0x200;
                createRotationMatrix(&rot, &m);
                TransMatrix(&m, &trans);
                SetRotMatrix(&m);
                SetTransMatrix(&m);
                drawMenuContext364_menuBox(i, gMenuContext->m364_menuBoxes[i]->m71D_drawInner);
                PopMatrix();
            } else {
                drawMenuContext364_menuBox(i, gMenuContext->m364_menuBoxes[i]->m71D_drawInner);
            }
        }
    }
}

/* Noah drawMenu2_sub_sub0 */
void drawMenu2_sub_sub0(void) {
    s32 i;

    for (i = 0; i < 4; i++) {
        if (gMenuContext->m33C_drawFlags->m34_drawLabels[i] != 0) {
            MENU_TEXT_PRIM(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m4E0_labels[i].m0_polys[gMenuContext->m4E0_labels[i].m7D_oddOrEven]);
        }
    }
}

/* Noah drawMenu2_sub_sub1 (JP draws the m6E0 labels; US compiles the body out) */
void drawMenu2_sub_sub1(void) {
    s32 i;

    for (i = 0; i < 8; i++) {
        if (gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels[i] != 0) {
#if VERSION_JP
            MENU_TEXT_PRIM(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m6E0_mainMenuLabels[i].m0_polys[gMenuContext->m6E0_mainMenuLabels[i].m7D_oddOrEven]);
#endif
        }
    }
}

/* Noah drawMenu2_sub_sub2 */
void drawMenu2_sub_sub2(void) {
    s32 i;

    for (i = 0; i < 6; i++) {
        if (gMenuContext->m33C_drawFlags->m14_drawSubMenuLabels[i] != 0) {
            if (gMenuContext->mAE0_subMenuLabels[i].m7F_transformEnabled != 0) {
                transformAndAddPrim(1, gMenuContext->mAE0_subMenuLabels[i].m50_vertex, gMenuContext->mAE0_subMenuLabels[i].m0_polys, gMenuContext->mAE0_subMenuLabels[i].m7D_oddOrEven);
            }
#if VERSION_JP
            else {
                MENU_TEXT_PRIM(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->mAE0_subMenuLabels[i].m0_polys[gMenuContext->mAE0_subMenuLabels[i].m7D_oddOrEven]);
            }
#endif
        }
    }
}

/* Noah drawMenu2_sub_sub3 (JP draws the mDE0 labels; US compiles the body out) */
void drawMenu2_sub_sub3(void) {
    s32 i;

    for (i = 0; i < 6; i++) {
        if (gMenuContext->m33C_drawFlags->m1A_drawSaveMenuLabels[i] != 0) {
#if VERSION_JP
            MENU_TEXT_PRIM(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->mDE0_saveMenuLabels[i].m0_polys[gMenuContext->mDE0_saveMenuLabels[i].m7D_oddOrEven]);
#endif
        }
    }
}

/* Noah drawMenu2_sub_sub6 */
void drawMenu2_sub_sub6(void) {
    s32 i;

    for (i = 0; i < 8; i++) {
        if (gMenuContext->m33C_drawFlags->m38_drawListMenuLabels[i] != 0) {
            transformAndAddPrim(1, gMenuContext->m10E0_listMenuLabels[i].m50_vertex, gMenuContext->m10E0_listMenuLabels[i].m0_polys, gMenuContext->m10E0_listMenuLabels[i].m7D_oddOrEven);
        }
    }
}

/* Noah drawMenu2_sub_sub7 */
void drawMenu2_sub_sub7(void) {
    s32 i;

    for (i = 0; i < 6; i++) {
        if (gMenuContext->m33C_drawFlags->m40_drawEquipMenuLabels[i] != 0) {
            transformAndAddPrim(1, gMenuContext->m14E0_equipMenuLabels[i].m50_vertex, gMenuContext->m14E0_equipMenuLabels[i].m0_polys, gMenuContext->m14E0_equipMenuLabels[i].m7D_oddOrEven);
        }
    }
}

/* Noah drawMenu2_sub_sub8 */
void drawMenu2_sub_sub8(void) {
    if (gMenuContext->m33C_drawFlags->m4E_drawSkillPageLabels[0] != 0) { /* only the first of the two labels is drawn */
        MENU_TEXT_PRIM(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m17E0_skillPageLabels[0].m0_polys[gMenuContext->m17E0_skillPageLabels[0].m7D_oddOrEven]);
    }
}

/* Noah drawMenu2_drawCheckingMemoryCard */
void drawMenu2_drawCheckingMemoryCard(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m2E_drawCheckingMemorycardText != 0) {
        for (i = 0; i < 3; i++) {
            MenuContext_4E0* p = gMenuContext->m1DE0_messageLines[i];
            if (p->m7F_transformEnabled != 0) {
                transformAndAddPrim(1, p->m50_vertex, p->m0_polys, p->m7D_oddOrEven);
            } else {
                MENU_TEXT_PRIM(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &p->m0_polys[p->m7D_oddOrEven]);
            }
        }
    }
}

/* Noah drawMenu2_sub_sub4 */
void drawMenu2_sub_sub4(void) {
    s32 i;

    for (i = 0; i < 6; i++) {
        if (gMenuContext->m33C_drawFlags->m54_drawGearMenuLabels[i] != 0) {
            if (gMenuContext->m18E0_gearMenuLabels[i].m7F_transformEnabled != 0) {
                transformAndAddPrim(1, gMenuContext->m18E0_gearMenuLabels[i].m50_vertex, gMenuContext->m18E0_gearMenuLabels[i].m0_polys, gMenuContext->m18E0_gearMenuLabels[i].m7D_oddOrEven);
            }
#if VERSION_JP
            else {
                MENU_TEXT_PRIM(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m18E0_gearMenuLabels[i].m0_polys[gMenuContext->m18E0_gearMenuLabels[i].m7D_oddOrEven]);
            }
#endif
        }
    }
}

/* Noah drawMenu2_sub_sub5 */
void drawMenu2_sub_sub5(void) {
    s32 i;

    for (i = 0; i < 4; i++) {
        if (gMenuContext->m33C_drawFlags->m5C_drawSoundMenuLabels[i] != 0) {
            MENU_TEXT_PRIM(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m1BE0_soundMenuLabels[i].m0_polys[gMenuContext->m1BE0_soundMenuLabels[i].m7D_oddOrEven]);
        }
    }
}

/* Noah drawMenu2_sub */
void drawMenu2_sub(void) {
    drawMenu2_sub_sub0();
    drawMenu2_sub_sub1();
    drawMenu2_sub_sub2();
    drawMenu2_sub_sub3();
    drawMenu2_sub_sub4();
    drawMenu2_sub_sub5();
    drawMenu2_sub_sub6();
    drawMenu2_sub_sub7();
    drawMenu2_sub_sub8();
    drawMenu2_drawCheckingMemoryCard();
}

/* Noah drawMenuBackground */
void drawMenuBackground(void) {
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[8], &gMenuContext->m348_cursor->m98_poly[gMenuContext->m308_oddOrEven]);
    AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[8], &gMenuContext->m348_cursor->m140_backgroundDrawMode[gMenuContext->m308_oddOrEven]);
}

/* draws one m1E08 party panel (drawMenu_1E08 helper) */
void drawPartyCard(MenuContext_1E08* p, u8 drawExtra) {
    if (p->mBE7_enabled != 0) {
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &p->m460_poly[p->mBE6_oddOrEven]);
        AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &p->m4B0_poly[p->mBE6_oddOrEven]);
        drawMultiPolys(p->mBE8_0Length, p->m0_polys, p->mBE6_oddOrEven);
        drawMultiPolys(p->mBE0_500Length, p->m500_polys, p->mBE6_oddOrEven);
        drawMultiPolys(p->mBE1_5F0Length, p->m5F0_polys, p->mBE6_oddOrEven);
        drawMultiPolys(p->mBE2_6E0Length, p->m6E0_polys, p->mBE6_oddOrEven);
        drawMultiPolys(p->mBE3_870Length, p->m870_polys, p->mBE6_oddOrEven);
        drawMultiPolys(p->mBE4_A00Length, p->mA00_polys, p->mBE6_oddOrEven);
        drawMultiPolys(p->mBE5_AF0Length, p->mAF0_polys, p->mBE6_oddOrEven);
        if (drawExtra != 0) {
            drawMultiPolys(5, p->m2D0_polys, p->mBE6_oddOrEven);
        }
    }
}

/* Noah drawMenu_1E08 */
void drawMenu_1E08(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m46_draw1E08 != 0) {
        for (i = 0; i < 3; i++) {
            drawPartyCard(gMenuContext->m1E08_partyCards[i], 1);
        }
    }
}

/* Noah FUN_Menu2__801d1464 (m43C) */
void drawScrollBarThumb(void) {
    if (gMenuContext->m33C_drawFlags->m49_draw43C != 0) {
        { int k; for (k = 0; k < 1; ++k) pcTextureClass(&(gMenuContext->m43C_scrollBarThumb->m0_polys)[k * 2 + gMenuContext->m43C_scrollBarThumb->m70_oddOrEven], PC_TEX_HUD); }
        transformAndAddPrim(1, gMenuContext->m43C_scrollBarThumb->m50_vertex, gMenuContext->m43C_scrollBarThumb->m0_polys, gMenuContext->m43C_scrollBarThumb->m70_oddOrEven);
    }
}

/* Noah FUN_Menu2__801d14b0 (m440) */
void drawCharacterSwitchArrows(void) {
    if (gMenuContext->m33C_drawFlags->m53_draw440 != 0) {
        { int k; for (k = 0; k < 4; ++k) pcTextureClass(&(gMenuContext->m440_arrows->m0_polys)[k * 2 + gMenuContext->m440_arrows->m1C0_oddOrEven], PC_TEX_HUD); }
        transformAndAddPrim(4, gMenuContext->m440_arrows->m140_vertex[0], gMenuContext->m440_arrows->m0_polys, gMenuContext->m440_arrows->m1C0_oddOrEven);
    }
}

/* Noah FUN_Menu2__801d14fc (m42C) */
void drawItemMenuList(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m48_draw42C != 0) {
        for (i = 0; i < 16; i++) {
            if (gMenuContext->m42C_itemMenu->m1184_enabled[i] != 0) {
                transformAndAddPrim(1, gMenuContext->m42C_itemMenu->m0_names[i].m50_vertex, gMenuContext->m42C_itemMenu->m0_names[i].m0_polys, gMenuContext->m42C_itemMenu->m0_names[i].m7D_oddOrEven);
                transformAndAddPrim(1, gMenuContext->m42C_itemMenu->m800_counts[i].m50_vertex, gMenuContext->m42C_itemMenu->m800_counts[i].m0_polys, gMenuContext->m42C_itemMenu->m800_counts[i].m7D_oddOrEven);
            }
        }
        if (gMenuContext->m42C_itemMenu->m1194_showSelected != 0) {
            transformAndAddPrim(1, gMenuContext->m42C_itemMenu->m1000_selectedName.m50_vertex, gMenuContext->m42C_itemMenu->m1000_selectedName.m0_polys, gMenuContext->m42C_itemMenu->m1000_selectedName.m7D_oddOrEven);
            transformAndAddPrim(1, gMenuContext->m42C_itemMenu->m1080_selectedCount.m50_vertex, gMenuContext->m42C_itemMenu->m1080_selectedCount.m0_polys, gMenuContext->m42C_itemMenu->m1080_selectedCount.m7D_oddOrEven);
            transformAndAddPrim(1, gMenuContext->m42C_itemMenu->m1100_description.m50_vertex, gMenuContext->m42C_itemMenu->m1100_description.m0_polys, gMenuContext->m42C_itemMenu->m1100_description.m7D_oddOrEven);
        }
    }
}

/* Noah FUN_Menu2__801d1640 (m430) */
void drawEtherListMenu(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m4A_draw430 != 0) {
        for (i = 0; i < 14; i++) {
            if (gMenuContext->m430_etherMenu->m1084_flags[i] != 0) {
                transformAndAddPrim(1, gMenuContext->m430_etherMenu->m0_names[i].m50_vertex, gMenuContext->m430_etherMenu->m0_names[i].m0_polys, gMenuContext->m430_etherMenu->m0_names[i].m7D_oddOrEven);
                transformAndAddPrim(1, gMenuContext->m430_etherMenu->m700_values[i].m50_vertex, gMenuContext->m430_etherMenu->m700_values[i].m0_polys, gMenuContext->m430_etherMenu->m700_values[i].m7D_oddOrEven);
            }
        }
        if (gMenuContext->m430_etherMenu->m1092_showSelected != 0) {
            transformAndAddPrim(1, gMenuContext->m430_etherMenu->mE00_selectedName.m50_vertex, gMenuContext->m430_etherMenu->mE00_selectedName.m0_polys, gMenuContext->m430_etherMenu->mE00_selectedName.m7D_oddOrEven);
            transformAndAddPrim(1, gMenuContext->m430_etherMenu->mE80_selectedValue.m50_vertex, gMenuContext->m430_etherMenu->mE80_selectedValue.m0_polys, gMenuContext->m430_etherMenu->mE80_selectedValue.m7D_oddOrEven);
            for (i = 0; i < 2; i++) {
                transformAndAddPrim(1, gMenuContext->m430_etherMenu->mF00_description[i].m50_vertex, gMenuContext->m430_etherMenu->mF00_description[i].m0_polys, gMenuContext->m430_etherMenu->mF00_description[i].m7D_oddOrEven);
            }
        }
        transformAndAddPrim(1, gMenuContext->m430_etherMenu->m1000_characterName.m50_vertex, gMenuContext->m430_etherMenu->m1000_characterName.m0_polys, gMenuContext->m430_etherMenu->m1000_characterName.m7D_oddOrEven);
    }
}

/* Noah FUN_Menu2__801d17c4 (m434) */
void drawEquipMenuLists(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m4C_draw434 != 0) {
        for (i = 0; i < 8; i++) {
            if (gMenuContext->m434_equipMenu->mA10_enabled[i] != 0) {
                transformAndAddPrim(1, gMenuContext->m434_equipMenu->m0_names[i].m50_vertex, gMenuContext->m434_equipMenu->m0_names[i].m0_polys, gMenuContext->m434_equipMenu->m0_names[i].m7D_oddOrEven);
                transformAndAddPrim(1, gMenuContext->m434_equipMenu->m400_counts[i].m50_vertex, gMenuContext->m434_equipMenu->m400_counts[i].m0_polys, gMenuContext->m434_equipMenu->m400_counts[i].m7D_oddOrEven);
            }
        }
        transformAndAddPrim(1, gMenuContext->m434_equipMenu->m800_characterName.m50_vertex, gMenuContext->m434_equipMenu->m800_characterName.m0_polys, gMenuContext->m434_equipMenu->m800_characterName.m7D_oddOrEven);
        if (gMenuContext->m434_equipMenu->mA18_drawDescription != 0) {
            for (i = 0; i < 3; i++) {
                transformAndAddPrim(1, gMenuContext->m434_equipMenu->m880_description[i].m50_vertex, gMenuContext->m434_equipMenu->m880_description[i].m0_polys, gMenuContext->m434_equipMenu->m880_description[i].m7D_oddOrEven);
            }
        }
    }
}

/* Noah FUN_Menu2__801d1914 (m438) */
void drawSkillMenu(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m4D_draw438 != 0) {
        for (i = 0; i < 13; i++) {
            if (gMenuContext->m438_skillMenu->m2596_enabled[i] != 0) {
                MENU_TEXT_PRIM(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m438_skillMenu->m0_names[i].m0_polys[gMenuContext->m438_skillMenu->m0_names[i].m7D_oddOrEven]);
                /* retail bug: the value label is drawn with the name label's m7D */
                MENU_TEXT_PRIM(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m438_skillMenu->m680_values[i].m0_polys[gMenuContext->m438_skillMenu->m0_names[i].m7D_oddOrEven]);
            }
        }
        transformAndAddPrim(1, gMenuContext->m438_skillMenu->mD00_characterName.m50_vertex, gMenuContext->m438_skillMenu->mD00_characterName.m0_polys, gMenuContext->m438_skillMenu->mD00_characterName.m7D_oddOrEven);
        for (i = 0; i < 13; i++) {
            drawMultiPolys(gMenuContext->m438_skillMenu->m257C_glyphCount[i], gMenuContext->m438_skillMenu->mD80_glyphs[i][0], gMenuContext->m438_skillMenu->m2589_glyphOddOrEven[i]);
            if (gMenuContext->m438_skillMenu->m25A3_showBar[i] != 0) {
                AddPrim(&gMenuContext->m1D4_currentDrawContext->m70_ot[4], &gMenuContext->m438_skillMenu->m21D0_bars[i][gMenuContext->m438_skillMenu->m25B0_barOddOrEven[i]]);
            }
        }
    }
}

/* Noah FUN_Menu2__801d1aac (m444[2]) */
void drawMenuPointerGlyphs(void) {
    s32 i;

    for (i = 0; i < 2; i++) {
        if (gMenuContext->m33C_drawFlags->m50_draw444[i] != 0) {
            transformAndAddPrim(1, gMenuContext->m444_pointerGlyphs[i]->m50_vertex, gMenuContext->m444_pointerGlyphs[i]->m0_polys, gMenuContext->m444_pointerGlyphs[i]->m75_oddOrEven);
        }
    }
}

/* Noah drawMenu0 */
void drawMenu0(void) {
    drawMenu_380();
    drawMenu2_sub();
    drawMenu_428();
    drawMenu_348_cursor();
    drawMenu_34C();
    drawMenuMemoryCard();
    drawCharacterInfoCards();
    drawCharacterInfoCardExtended();
    drawMenu_35C();
    drawMenu_360();
    displayGoldAndPlayTime();
    drawMenu_1E08();
    drawMenuPointerGlyphs();
    drawItemMenuList();
    drawEtherListMenu();
    drawEquipMenuLists();
    drawSkillMenu();
    drawScrollBarThumb();
    drawCharacterSwitchArrows();
    drawMenuBoxes();
    drawMainMenu();
    drawMenuContext354();
}

/* Noah drawMenu2 */
void drawMenu2(void) {
    drawMenu_380();
    drawMenu2_sub();
    drawMenu_428();
    drawMenu_348_cursor();
    drawMenu_34C();
    drawMenuMemoryCard();
    drawMainMenu();
    drawMenuContext354();
    drawMenuBoxes();
}

/* drawMenu6 (menuToEnter == 6) */
void drawMenu6(void) {
    drawMenu_380();
    drawMenu_428();
    drawMenu2_sub();
    drawMenu_348_cursor();
    drawMenu_34C();
    drawMenuMemoryCard();
    drawMenuBoxes();
    drawMenuContext354();
}

/* Noah drawMenuSpecificElements */
void drawMenuSpecificElements(void) {
    if (gMenuContext->m327_drawEnabled != 0) {
        switch (menuToEnter) {
        case 0:
            drawMenu0();
            break;
        case 2:
            drawMenu2();
            break;
        case 6:
            drawMenu6();
            break;
        }
    }
    drawMenuBackground();
}

/* Noah updateMenuTransforms */
void updateMenuTransforms(void) {
    switch (gMenuContext->m329_animationState) {
    case 4:
        gMenuContext->m1E0_menuTranslation.vz = 0x200;
        gMenuContext->m1D8_menuRotation.vz = 0;
        gMenuContext->m1D8_menuRotation.vy = 0;
        gMenuContext->m1D8_menuRotation.vx = 0;
        gMenuContext->m1E0_menuTranslation.vy = 0;
        gMenuContext->m1E0_menuTranslation.vx = 0;
        gMenuContext->m329_animationState = 2;
        break;
    case 3:
        gMenuContext->m1E0_menuTranslation.vz = 0x800;
        gMenuContext->m1D8_menuRotation.vz = 0;
        gMenuContext->m1D8_menuRotation.vy = 0;
        gMenuContext->m1D8_menuRotation.vx = 0;
        gMenuContext->m1E0_menuTranslation.vy = 0;
        gMenuContext->m1E0_menuTranslation.vx = 0;
        gMenuContext->m329_animationState = 1;
        break;
    case 2:
        gMenuContext->m1D8_menuRotation.vy -= 0x60;
        gMenuContext->m1E0_menuTranslation.vz += 0x40;
        if (gMenuContext->m1E0_menuTranslation.vz >= 0xE00) {
            gMenuContext->m329_animationState = 0;
        }
        break;
    case 1:
        gMenuContext->m1D8_menuRotation.vx += 0x7C;
        gMenuContext->m1E0_menuTranslation.vz -= 0x30;
        if (gMenuContext->m1E0_menuTranslation.vz < 0x200) {
            gMenuContext->m1E0_menuTranslation.vz = 0x200;
            gMenuContext->m1D8_menuRotation.vz = 0;
            gMenuContext->m1D8_menuRotation.vx = 0;
            gMenuContext->m1D8_menuRotation.vy = 0;
            gMenuContext->m329_animationState = 0;
        }
        break;
    }
    createRotationMatrix(&gMenuContext->m1D8_menuRotation, &gMenuContext->m1F0_menuMatrix);
    TransMatrix(&gMenuContext->m1F0_menuMatrix, &gMenuContext->m1E0_menuTranslation);
    SetRotMatrix(&gMenuContext->m1F0_menuMatrix);
    SetTransMatrix(&gMenuContext->m1F0_menuMatrix);
}

/* Noah processLoadSaveMenuSub1 (twin of processLoadSaveMenuSub1Alt2 = Sub1Alt2) */
void processLoadSaveMenuSub1(void) {
    gMenuContext->m329_animationState = 3;
    playMenuSoundEffect(0x5B);
}

#if VERSION_JP
/* JP's save title string and the linker fill after it (see buildSaveTitle). */
INCLUDE_RODATA("menu/nonmatchings/m_801C8D78", D_801C50D0);
#endif
