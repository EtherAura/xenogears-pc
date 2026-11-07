/*
 * Menu shared resources and globals, primitive setup, menuDraw / input / play time,
 * info card animation, bit mask helpers, memory card event helpers. Provisional file
 * 0x801C65F4-0x801C8D78; owns .rodata 0x801C5028-0x801C50A8 ("BASLUS-00664",
 * "BASLUS-01160", jtbl_801C5048). gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "menu/menu.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "pc_convert.h"

char* strcpy(char* dst, const char* src);
void loadMenuImageBundle(u32* arg0);
void loadSequence(void*);
void computeMenuBorder(void* font, s32 glyph, s32* polyCount, s32* colorDepth, s32* clutX, s32* clutY,
                   s32* tpageX, s32* tpageY);

extern void* menuSharedResources;
extern void* fieldSeq;

#include "main/game_state.h"
#include "main/dialog.h"
#include "pollhost.h"

extern u16 menu2Bitmask[];
extern u16 menu2BitmaskMsbFirst[];
extern u32 menu2Bitmask32[];
extern u8 initMenuLabelConfig[];

void setupPolyG4(POLY_G4* p, s32 r, s32 g, s32 b);
void updateMenuInput(void);
void updateMemoryCardPolling(void);
void checkSoftReboot(void);
u8 randomInRange(s32 arg0, s32 arg1);
extern s32* pRunningOnDTL;
extern u16 newPadButtonForDialogs;
extern u16 newPadButtonForField;
s32 isControllerConnected(s32 port);
void resumeMusic(void);
void pauseMusic(void);
s32 getInputOverflowed(void);
void resetInputs(void);
s32 loadInputFromVSyncBuffer(void);
void playSoundEffect(s32);
s32 getCurrentDiscNumber(void);
void func_800404C4(u32 ev, s32 spec);
s32 func_80040494(u32 ev);
s32 _card_info(s32 chan);
void EnterCriticalSection(void);
void ExitCriticalSection(void);
void CloseEvent(u32 ev);
extern s32 memoryCardEventStatus[];

/* Noah loadMenuSharedResources */
void loadMenuSharedResources(void);
void loadMenuSharedResources(void) {
    TIM_IMAGE tim;
    s32 border[18];
    uptr* bundle;
    u8* data;
    s32 i;

    bundle = menuSharedResources;
    bundle = (uptr*)pcRelocTable((u32*)bundle);
    data = mallocAndDecompress((void*)bundle[1], 1);
    OpenTIM((u32*)data);
    ReadTIM(&gMenuContext->m32C_memoryCardContext->mB80_iconTim);
#if VERSION_JP
    strcpy(gMenuContext->m32C_memoryCardContext->m4FCE_saveName, "BISLPS-01160");
#else
    strcpy(gMenuContext->m32C_memoryCardContext->m4FCE_saveName, "BASLUS-00664");
    strcpy(gMenuContext->m32C_memoryCardContext->m501C_saveName2, "BASLUS-01160");
#endif
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m0_magic[0] = 'S';
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m0_magic[1] = 'C';
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m2_iconDisplayFlag = 0x11;
    gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m3_blockNumber = 1;
    bzero(gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m4_title, 0x5C);
    memmove(gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m60_iconsPalette,
            gMenuContext->m32C_memoryCardContext->mB80_iconTim.caddr, 0x20);
    memmove(gMenuContext->m32C_memoryCardContext->m4B94_saveHeader.m80_icon,
            gMenuContext->m32C_memoryCardContext->mB80_iconTim.paddr, 0x80);
    freeHeap(data);
    data = mallocAndDecompress((void*)bundle[2], 1);
    loadMenuImageBundle((u32*)data);
    freeHeap(data);
    gMenuContext->m2DC_font = mallocAndDecompress((void*)bundle[3], 0);
    gMenuContext->m2E0_textBundle = mallocAndDecompress((void*)bundle[4], 0);
    computeMenuBorder(gMenuContext->m2DC_font, 0xE0, &border[0], &border[1], &border[2], &border[3],
                  &border[4], &border[5]);
    computeMenuBorder(gMenuContext->m2DC_font, 0x14B, &border[0], &border[1], &border[2], &border[3],
                  &border[4], &border[5]);
    computeMenuBorder(gMenuContext->m2DC_font, 0x14C, &border[6], &border[7], &border[8], &border[9],
                  &border[10], &border[11]);
    computeMenuBorder(gMenuContext->m2DC_font, 0x14D, &border[12], &border[13], &border[14], &border[15],
                  &border[16], &border[17]);
    border[10] += 0xC;
    data = mallocAndDecompress((void*)bundle[5], 1);
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
            OpenTIM((u32*)(data + gMenuContext->m33C_drawFlags->m30_partyMemberId[i] * 0xB20));
            ReadTIM(&tim);
            tim.crect->x = border[i * 6 + 2];
            tim.crect->y = border[i * 6 + 3];
            tim.prect->x = border[i * 6 + 4];
            tim.prect->y = border[i * 6 + 5];
            LoadImage(tim.crect, tim.caddr);
            LoadImage(tim.prect, tim.paddr);
        }
    }
    DrawSync(0);
    freeHeap(data);
    if (useDebugMenuList) {
        setCurrentDirectory(0x10, 2);
        fieldSeq = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(5)), 0);
        readFile(5, fieldSeq, 0, 0x80);
        waitReadCompletion(0);
        setCurrentDirectory(0x10, 0);
        loadSequence(fieldSeq);
    }
    gMenuContext->m2E4_musicSequence = fieldSeq;
    freeHeap(menuSharedResources);
    freeHeap(bundle);
}

/* Noah initMenuGlobals */
void initMenuGlobals(void) {
    s32 i;
    u16 mask;
    s32 id;

    if (menuToEnter == 0 && g_menuOpenArg == 0) {
        gMenuContext->m336_menuSelectedEntry = savedMenuSelectedEntry;
    } else {
        gMenuContext->m336_menuSelectedEntry = 1;
    }
    gMenuContext->m337_previousMenuSelectedEntry = 0xFF;
    gMenuContext->m326_cardPollTimer = 0x3C;
    gMenuContext->m334_cardOperation = 0;
    gMenuContext->m335 = 0;
    gMenuContext->m32B_numPartyMembers = 0;
    mask = gameState.m1D30_partyMemberBitField & gameState.m1D32_partyFrameMask & 0x7FF;
    for (i = 0; i < 16; i++) {
        if (menu2CheckBitmask(mask, i)) {
            gMenuContext->m30C_characterAvailable[i] = 1;
        } else {
            gMenuContext->m30C_characterAvailable[i] = 0;
        }
    }
    for (i = 0; i < 3; i++) {
        gMenuContext->m33C_drawFlags->m5C_drawSoundMenuLabels[i + 4] = 0;
        id = gameState.m1D34_currentParty[i];
        if (id != 0xFF && gMenuContext->m30C_characterAvailable[id] != 0) {
            gMenuContext->m33C_drawFlags->m30_partyMemberId[i] = id;
            gMenuContext->m32B_numPartyMembers++;
            if (gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].mA0_gearNum != 0xFF) {
                gMenuContext->m33C_drawFlags->m5C_drawSoundMenuLabels[i + 4] = 1;
                gMenuContext->m33B_numGears++;
            }
        } else {
            gMenuContext->m33C_drawFlags->m30_partyMemberId[i] = 0xFF;
        }
    }
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
            gMenuContext->m4DC_currentlySelectedCharacter = i;
            break;
        }
    }
    loadMenuSharedResources();
}

/* Noah resetMenuOddOrEven */
void resetMenuOddOrEven(void) {
    gMenuContext->m308_oddOrEven = 0;
}

/* clears m4CC/m4D0/m4D8/m4D9/m4D4 fade state */
void resetMemoryCardDisplayState(void) {
    gMenuContext->m4CC_iconFrame = 0;
    gMenuContext->m4D0_iconFrameTimer = 0;
    gMenuContext->m4D8_cardTilesMode = 0;
    gMenuContext->m4D9_fadeDirection = 0;
    gMenuContext->m4D4_fadeColor = 0;
}

/* Noah loadBlankImage (unused RECT reserves stack) */
void loadBlankImage(void) {
    RECT rect;
    RECT unused; /* only reserves stack */
    u16* buf;

    buf = allocateHeap(PC_HEAP_BYTES(0x20), 0);
    bzero(buf, 0x20);
    buf[1] = 0x7FFF;
    rect.y = 0x1C0;
    rect.w = 0x10;
    rect.x = 0;
    rect.h = 1;
    LoadImage(&rect, buf);
    DrawSync(0);
    freeHeap(buf);
}

/* Noah initMenuContext4E0 */
void initMenuContext4E0(void) {
    initFontPalettes(0, 0x1D1);
    gMenuContext->m4E0_labels[0].m78_imageData = ALLOC_LABEL_IMAGE(0x38E);
    setupMenuContext4E0(&gMenuContext->m4E0_labels[0], initMenuLabelConfig, 0, 4);
    loadBlankImage();
}

/* Noah computeMenuBorders (unused s32[10] reserves stack) */
void computeMenuBorders(void) {
    s32 unused[10]; /* only reserves stack */

    computeMenuBorder(gMenuContext->m2DC_font, 0xFE, &gMenuContext->m46C_menuBorders[0][0],
                  &gMenuContext->m46C_menuBorders[0][1], &gMenuContext->m46C_menuBorders[0][2],
                  &gMenuContext->m46C_menuBorders[0][3], &gMenuContext->m46C_menuBorders[0][4],
                  &gMenuContext->m46C_menuBorders[0][5]);
    computeMenuBorder(gMenuContext->m2DC_font, 0x103, &gMenuContext->m46C_menuBorders[1][0],
                  &gMenuContext->m46C_menuBorders[1][1], &gMenuContext->m46C_menuBorders[1][2],
                  &gMenuContext->m46C_menuBorders[1][3], &gMenuContext->m46C_menuBorders[1][4],
                  &gMenuContext->m46C_menuBorders[1][5]);
    computeMenuBorder(gMenuContext->m2DC_font, 0x100, &gMenuContext->m46C_menuBorders[2][0],
                  &gMenuContext->m46C_menuBorders[2][1], &gMenuContext->m46C_menuBorders[2][2],
                  &gMenuContext->m46C_menuBorders[2][3], &gMenuContext->m46C_menuBorders[2][4],
                  &gMenuContext->m46C_menuBorders[2][5]);
    computeMenuBorder(gMenuContext->m2DC_font, 0x101, &gMenuContext->m46C_menuBorders[3][0],
                  &gMenuContext->m46C_menuBorders[3][1], &gMenuContext->m46C_menuBorders[3][2],
                  &gMenuContext->m46C_menuBorders[3][3], &gMenuContext->m46C_menuBorders[3][4],
                  &gMenuContext->m46C_menuBorders[3][5]);
}

/* Noah initPrimitives_348_cursor */
void initPrimitives_348_cursor(void) {
    RECT rect;
    s32 i;

    rect.y = 0;
    rect.x = 0;
    rect.h = 0x100;
    rect.w = 0x100;
    resetCursorState();
    for (i = 0; i < 2; i++) {
        setupPolyG4(&gMenuContext->m348_cursor->m50_polyG4[i], 0x80, 0x80, 0);
        SetSemiTrans(&gMenuContext->m348_cursor->m50_polyG4[i], 1);
        SetLineF3(&gMenuContext->m348_cursor->mC8_lines[i]);
        (gMenuContext->m348_cursor->mC8_lines + i)->r0 = 0;
        (gMenuContext->m348_cursor->mC8_lines + i)->g0 = 0x40;
        (gMenuContext->m348_cursor->mC8_lines + i)->b0 = 0;
        SetLineF3(&gMenuContext->m348_cursor->mF8_lines[i]);
        (gMenuContext->m348_cursor->mF8_lines + i)->r0 = 0;
        (gMenuContext->m348_cursor->mF8_lines + i)->g0 = 0x40;
        (gMenuContext->m348_cursor->mF8_lines + i)->b0 = 0;
        SetPolyF4(&gMenuContext->m348_cursor->m98_poly[i]);
        (gMenuContext->m348_cursor->m98_poly + i)->x0 = 0;
        (gMenuContext->m348_cursor->m98_poly + i)->y0 = 0;
        (gMenuContext->m348_cursor->m98_poly + i)->x1 = 0x140;
        (gMenuContext->m348_cursor->m98_poly + i)->y1 = 0;
        (gMenuContext->m348_cursor->m98_poly + i)->x2 = 0;
        (gMenuContext->m348_cursor->m98_poly + i)->y2 = 0xE0;
        (gMenuContext->m348_cursor->m98_poly + i)->x3 = 0x140;
        (gMenuContext->m348_cursor->m98_poly + i)->y3 = 0xE0;
        (gMenuContext->m348_cursor->m98_poly + i)->r0 = 0x80;
        (gMenuContext->m348_cursor->m98_poly + i)->g0 = 0x80;
        (gMenuContext->m348_cursor->m98_poly + i)->b0 = 0x80;
        SetSemiTrans(&gMenuContext->m348_cursor->m98_poly[i], 1);
        SetDrawMode(&gMenuContext->m348_cursor->m128_drawMode[i], 0, 0, GetTPage(0, 0, 0x140, 0x80), &rect);
        SetDrawMode(&gMenuContext->m348_cursor->m140_backgroundDrawMode[i], 0, 0, GetTPage(0, 2, 0x180, 0), &rect);
    }
}

/* menu resource load (0-7)/free (16-23), dir 0x10 file 2 */
void loadSharedMenuData2(u8 arg0) {
    uptr* bundle;
    s32 i;
    void* raw;

    if (arg0 < 16) {
        setCurrentDirectory(0x10, 0);
        bundle = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(2)), 1);
        readFile(2, bundle, 0, 0x80);
        waitReadCompletion(0);
        raw = bundle;
        bundle = (uptr*)pcRelocTable((u32*)raw);
    }
    switch (arg0) {
    case 0:
        gMenuContext->m330_itemData->m1C_usableItems = mallocAndDecompress((void*)bundle[1], 0);
        gMenuContext->m42C_itemMenu->m1180_descriptions = mallocAndDecompress((void*)bundle[15], 0);
        break;
    case 1:
        gMenuContext->m330_itemData->m8_gearFrames = mallocAndDecompress((void*)bundle[17], 0);
        gMenuContext->m330_itemData->m10_gearArmors = mallocAndDecompress((void*)bundle[18], 0);
        gMenuContext->m330_itemData->mC_gearEngines = mallocAndDecompress((void*)bundle[19], 0);
        gMenuContext->m330_itemData->m14_gearAccessories = mallocAndDecompress((void*)bundle[20], 0);
        break;
    case 2:
        for (i = 0; i < 3; i++) {
            if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
                gMenuContext->m330_itemData->m20_perCharacter[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]] =
                    mallocAndDecompress((void*)bundle[4 + gMenuContext->m33C_drawFlags->m30_partyMemberId[i]], 0);
            }
        }
        gMenuContext->m430_etherMenu->m1080_descriptions = mallocAndDecompress((void*)bundle[16], 0);
        break;
    case 3:
        gMenuContext->m330_itemData->m0_weaponStats = mallocAndDecompress((void*)bundle[2], 0);
        gMenuContext->m330_itemData->m4_accessoryData = mallocAndDecompress((void*)bundle[3], 0);
        gMenuContext->m330_itemData->m18_gearWeapons = mallocAndDecompress((void*)bundle[43], 0);
        gMenuContext->m330_itemData->m14_gearAccessories = mallocAndDecompress((void*)bundle[20], 0);
        break;
    case 4:
        for (i = 0; i < 3; i++) {
            if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
                gMenuContext->m330_itemData->m20_perCharacter[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]] =
                    mallocAndDecompress((void*)bundle[4 + gMenuContext->m33C_drawFlags->m30_partyMemberId[i]], 0);
            }
        }
        gMenuContext->m438_skillMenu->m2578_skillRequirements = mallocAndDecompress((void*)bundle[44], 0);
        break;
    case 5:
    case 6:
        for (i = 0; i < 3; i++) {
            if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF &&
                gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].mA0_gearNum != 0xFF) {
                gMenuContext->m330_itemData->m4C_perGear[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].mA0_gearNum] =
                    mallocAndDecompress(
                        (void*)bundle[23 + gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].mA0_gearNum], 0);
                computeGearHealFuelCost(gMenuContext->m330_itemData,
                              gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].mA0_gearNum);
            }
        }
        if (arg0 == 5) {
            gMenuContext->m430_etherMenu->m1080_descriptions = mallocAndDecompress((void*)bundle[21], 0);
        } else {
            gMenuContext->m430_etherMenu->m1080_descriptions = mallocAndDecompress((void*)bundle[22], 0);
        }
        break;
    case 7:
        gMenuContext->m434_equipMenu->mA00_textBundles[0] = mallocAndDecompress((void*)bundle[53], 0);
        gMenuContext->m434_equipMenu->mA00_textBundles[1] = mallocAndDecompress((void*)bundle[54], 0);
        gMenuContext->m434_equipMenu->mA00_textBundles[2] = mallocAndDecompress((void*)bundle[55], 0);
        gMenuContext->m434_equipMenu->mA00_textBundles[3] = mallocAndDecompress((void*)bundle[56], 0);
        break;
    case 16:
        freeHeap(gMenuContext->m330_itemData->m1C_usableItems);
        freeHeap(gMenuContext->m42C_itemMenu->m1180_descriptions);
        break;
    case 17:
        freeHeap(gMenuContext->m330_itemData->m8_gearFrames);
        freeHeap(gMenuContext->m330_itemData->m10_gearArmors);
        freeHeap(gMenuContext->m330_itemData->mC_gearEngines);
        freeHeap(gMenuContext->m330_itemData->m14_gearAccessories);
        break;
    case 18:
        for (i = 0; i < 3; i++) {
            if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
                freeHeap(gMenuContext->m330_itemData->m20_perCharacter[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]]);
            }
        }
        freeHeap(gMenuContext->m430_etherMenu->m1080_descriptions);
        break;
    case 19:
        freeHeap(gMenuContext->m330_itemData->m0_weaponStats);
        freeHeap(gMenuContext->m330_itemData->m4_accessoryData);
        freeHeap(gMenuContext->m330_itemData->m18_gearWeapons);
        freeHeap(gMenuContext->m330_itemData->m14_gearAccessories);
        break;
    case 20:
        for (i = 0; i < 3; i++) {
            if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
                freeHeap(gMenuContext->m330_itemData->m20_perCharacter[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]]);
            }
        }
        freeHeap(gMenuContext->m438_skillMenu->m2578_skillRequirements);
        break;
    case 21:
    case 22:
        for (i = 0; i < 3; i++) {
            if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF &&
                gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].mA0_gearNum != 0xFF) {
                freeHeap(gMenuContext->m330_itemData->m4C_perGear[gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].mA0_gearNum]);
            }
        }
        freeHeap(gMenuContext->m430_etherMenu->m1080_descriptions);
        break;
    case 23:
        freeHeap(gMenuContext->m434_equipMenu->mA00_textBundles[0]);
        freeHeap(gMenuContext->m434_equipMenu->mA00_textBundles[1]);
        freeHeap(gMenuContext->m434_equipMenu->mA00_textBundles[2]);
        freeHeap(gMenuContext->m434_equipMenu->mA00_textBundles[3]);
        break;
    }
    if (arg0 < 16) {
        freeHeap(raw);
        freeHeap(bundle);
    }
}

/* Noah setupMainMenu */
void setupMainMenu(void) {
    gMenuContext->m350_mainMenu->m1180_backgroundRect.x = 0x2C0;
    gMenuContext->m350_mainMenu->m1180_backgroundRect.y = 0x100;
    gMenuContext->m350_mainMenu->m1180_backgroundRect.w = 0x140;
    gMenuContext->m350_mainMenu->m1180_backgroundRect.h = 0xE0;
    gMenuContext->m348_cursor->m15B_selectionWidth = 0x40;
    initMenuGlobals();
    resetMenuOddOrEven();
    initMenuContext4E0();
    initPrimitives_348_cursor();
    computeMenuBorders();
    switch (menuToEnter) {
    case 2:
        gMenuContext->m348_cursor->m15B_selectionWidth = 0x4C;
    case 0:
    case 6:
        loadSaveLocationName();
        resetMemoryCardDisplayState();
        break;
    }
}

/* Noah menuDraw (POLLHOST for the SN break) */
void menuDraw(void) {
    MenuContext* ctx;
    s32 odd;

    if (*pRunningOnDTL != -1) {
        POLLHOST();
    }
    updateMenuInput();
    if (menuVarUnk0) {
        checkSoftReboot();
    }
    ctx = gMenuContext;
    ctx->m1D4_currentDrawContext = (ctx->m1D4_currentDrawContext == &ctx->m6C_drawContexts[0])
                                       ? &ctx->m6C_drawContexts[1]
                                       : &ctx->m6C_drawContexts[0];
    ctx->m308_oddOrEven = ctx->m308_oddOrEven == 0;
    ClearOTagR(ctx->m1D4_currentDrawContext->m70_ot, 0x10);
    randomInRange(0, 0xFF);
    updateMenuTransforms();
    gMenuContext->m2D8_frameCounter++;
    updatePlayTimeString(g_playTimeInVsync);
    drawMenuPlayTime();
    drawMenuSpecificElements();
    odd = gMenuContext->m308_oddOrEven == 0;
    DrawSync(0);
    VSync(0);
    PutDrawEnv(&gMenuContext->m1D4_currentDrawContext->m0_drawEnv);
    PutDispEnv(&gMenuContext->m1D4_currentDrawContext->m5C_dispEnv);
    /* The captured field or worldmap behind the menu: the menu is wide even
     * when that screen was 4:3 (port/src/game/menu/wide.c). */
    pcWideNarrowBackdrop(1);
    MoveImage(&gMenuContext->m350_mainMenu->m1180_backgroundRect, 0, odd * 0xE0);
    pcWideNarrowBackdrop(0);
    menuWideLayout(&gMenuContext->m1D4_currentDrawContext->m70_ot[15]);
    DrawOTag(&gMenuContext->m1D4_currentDrawContext->m70_ot[15]);
    updateMemoryCardPolling();
    updateMemoryCardDirectoryScans();
}

/* Noah updateMenuInput */
void updateMenuInput(void) {
    u8 paused;
    u8 retry;
    s32 time;
    u8 sel;

#if !VERSION_JP /* JP does not pause while the pad is unplugged */
    retry = 1;
    paused = 0;
    do {
        if (isControllerConnected(0) == 0) {
            if (paused == 0) {
                pauseMusic();
                paused++;
                time = g_playTimeInVsync;
            }
        } else {
            if (paused != 0) {
                resumeMusic();
                g_playTimeInVsync = time;
            }
            retry--;
        }
    } while (retry);
#endif
    sel = 8;
    if (getInputOverflowed()) {
        resetInputs();
    } else {
        while (loadInputFromVSyncBuffer()) {
            if (newPadButtonForField & 0x2000) {
                sel = 0;
                playMenuSoundEffect(1);
                break;
            }
            if (newPadButtonForField & 0x4000) {
                sel = 1;
                playMenuSoundEffect(1);
                break;
            }
            if (newPadButtonForField & 0x8000) {
                sel = 2;
                playMenuSoundEffect(1);
                break;
            }
            if (newPadButtonForField & 0x1000) {
                sel = 3;
                playMenuSoundEffect(1);
                break;
            }
            if (newPadButtonForDialogs & 0x20) {
                sel = 4;
                playMenuSoundEffect(2);
                break;
            }
            if (newPadButtonForDialogs & 0x40) {
                sel = 5;
                playMenuSoundEffect(3);
                break;
            }
            if (newPadButtonForDialogs & 0x80) {
                sel = 6;
                break;
            }
            if (newPadButtonForDialogs & 0x10) {
                sel = 7;
                break;
            }
            if (newPadButtonForField & 4) {
                sel = 10;
                playMenuSoundEffect(1);
                break;
            }
            if (newPadButtonForField & 8) {
                sel = 9;
                playMenuSoundEffect(1);
                break;
            }
            if (newPadButtonForDialogs & 0x100) {
                sel = 12;
                break;
            }
        }
    }
    gMenuContext->m325_menuButton = sel;
}

/* Noah updatePlayTimeString */
void updatePlayTimeString(u32 t) {
    u32 v0, v1, v2, v3, v4, v5;

    v0 = t / 21600000;
    t %= 21600000;
    v1 = t / 2160000;
    t %= 2160000;
    v2 = t / 216000;
    t %= 216000;
    v3 = t / 36000;
    t %= 36000;
    v4 = t / 3600;
    t %= 3600;
    v5 = t / 600;
    t %= 600;
    gMenuContext->m2EC_playTime[0] = v0;
    gMenuContext->m2EC_playTime[1] = v1;
    gMenuContext->m2EC_playTime[2] = v2;
    gMenuContext->m2EC_playTime[3] = v3;
    gMenuContext->m2EC_playTime[4] = v4;
    gMenuContext->m2EC_playTime[5] = v5;
    gMenuContext->m2EC_playTime[6] = t / 60;
}

/* Noah fillStringDisplayBuffer */
void fillStringDisplayBuffer(u32 value) {
    u32 div;
    s32 i;
    u8 digit;

    div = 100000000;
    for (i = 0; i < 9; i++) {
        digit = value / div;
        value %= div;
        div /= 10;
        gMenuContext->m31C_stringDisplayBuffer[i] = digit;
    }
    for (i = 1; i < 9; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i] != 0) {
            if (gMenuContext->m31C_stringDisplayBuffer[i - 1] == 0) {
                gMenuContext->m31C_stringDisplayBuffer[i - 1] = 0xFF;
            }
            return;
        }
        gMenuContext->m31C_stringDisplayBuffer[i - 1] = 0xFF;
    }
}

/* Noah setupPolyG4-like: SetPolyG4 + top colour */
void setupPolyG4(POLY_G4* p, s32 r, s32 g, s32 b) {
    SetPolyG4(p);
    p->r0 = r;
    p->g0 = g;
    p->b0 = b;
    p->r1 = r;
    p->g1 = g;
    p->b1 = b;
    p->r2 = 0;
    p->g2 = 0;
    p->b2 = 0;
    p->r3 = 0;
    p->g3 = 0;
    p->b3 = 0;
}

/* Noah setupInfoCardAnimation */
void setupInfoCardAnimation(s32 startX, s32 startY, s32 endX, s32 endY, u8 numSteps, u8 slot) {
    s32 dx;
    s32 dy;

    gMenuContext->m0_infoCardAnimations[slot].m0_startX = startX;
    gMenuContext->m0_infoCardAnimations[slot].m8_startY = startY;
    gMenuContext->m0_infoCardAnimations[slot].m4_endX = endX;
    gMenuContext->m0_infoCardAnimations[slot].mC_endY = endY;
    if (endX < startX) {
        dx = startX - endX;
        gMenuContext->m0_infoCardAnimations[slot].m20_XReversed = 1;
    } else {
        dx = endX - startX;
        gMenuContext->m0_infoCardAnimations[slot].m20_XReversed = 0;
    }
    if (endY < startY) {
        dy = startY - endY;
        gMenuContext->m0_infoCardAnimations[slot].m21_YReversed = 1;
    } else {
        dy = endY - startY;
        gMenuContext->m0_infoCardAnimations[slot].m21_YReversed = 0;
    }
    if (dx >= dy) {
        gMenuContext->m0_infoCardAnimations[slot].m10_XStep = 0x100;
        gMenuContext->m0_infoCardAnimations[slot].m14_YStep = R3000_DIV(dy << 8, dx);
    } else {
        gMenuContext->m0_infoCardAnimations[slot].m14_YStep = 0x100;
        gMenuContext->m0_infoCardAnimations[slot].m10_XStep = (dx << 8) / dy;
    }
    gMenuContext->m0_infoCardAnimations[slot].m22_numSteps = numSteps;
    gMenuContext->m0_infoCardAnimations[slot].m18_XOffset = 0;
    gMenuContext->m0_infoCardAnimations[slot].m1C_YOffset = 0;
    gMenuContext->m0_infoCardAnimations[slot].m23_isAnimationDone = 0;
}

/* Noah infocardAnimationStep */
void infocardAnimationStep(u8 slot) {
    s32 i;

    for (i = 0; i < gMenuContext->m0_infoCardAnimations[slot].m22_numSteps; i++) {
        if (gMenuContext->m0_infoCardAnimations[slot].m20_XReversed) {
            gMenuContext->m0_infoCardAnimations[slot].m18_XOffset -= gMenuContext->m0_infoCardAnimations[slot].m10_XStep;
        } else {
            gMenuContext->m0_infoCardAnimations[slot].m18_XOffset += gMenuContext->m0_infoCardAnimations[slot].m10_XStep;
        }
        if (gMenuContext->m0_infoCardAnimations[slot].m21_YReversed) {
            gMenuContext->m0_infoCardAnimations[slot].m1C_YOffset -= gMenuContext->m0_infoCardAnimations[slot].m14_YStep;
        } else {
            gMenuContext->m0_infoCardAnimations[slot].m1C_YOffset += gMenuContext->m0_infoCardAnimations[slot].m14_YStep;
        }
    }
    if (gMenuContext->m0_infoCardAnimations[slot].m10_XStep == 0x100) {
        if (gMenuContext->m0_infoCardAnimations[slot].m20_XReversed) {
            if ((gMenuContext->m0_infoCardAnimations[slot].m18_XOffset / 256) + gMenuContext->m0_infoCardAnimations[slot].m0_startX < gMenuContext->m0_infoCardAnimations[slot].m4_endX) {
                gMenuContext->m0_infoCardAnimations[slot].m23_isAnimationDone = 1;
            }
        } else {
            if (gMenuContext->m0_infoCardAnimations[slot].m4_endX < (gMenuContext->m0_infoCardAnimations[slot].m18_XOffset / 256) + gMenuContext->m0_infoCardAnimations[slot].m0_startX) {
                gMenuContext->m0_infoCardAnimations[slot].m23_isAnimationDone = 1;
            }
        }
    } else {
        if (gMenuContext->m0_infoCardAnimations[slot].m21_YReversed) {
            if ((gMenuContext->m0_infoCardAnimations[slot].m1C_YOffset / 256) + gMenuContext->m0_infoCardAnimations[slot].m8_startY < gMenuContext->m0_infoCardAnimations[slot].mC_endY) {
                gMenuContext->m0_infoCardAnimations[slot].m23_isAnimationDone = 1;
            }
        } else {
            if (gMenuContext->m0_infoCardAnimations[slot].mC_endY < (gMenuContext->m0_infoCardAnimations[slot].m1C_YOffset / 256) + gMenuContext->m0_infoCardAnimations[slot].m8_startY) {
                gMenuContext->m0_infoCardAnimations[slot].m23_isAnimationDone = 1;
            }
        }
    }
}

/* Noah initMemoryCardPolyVerts */
void initMemoryCardPolyVerts(SVECTOR* v, u16 x, u16 y, u16 w, u16 h) {
    v[0].vx = x - 0xA0;
    v[0].vy = y - 0x70;
    v[0].vz = 0;
    v[1].vx = x + w - 0xA0;
    v[1].vy = y - 0x70;
    v[1].vz = 0;
    v[2].vx = x - 0xA0;
    v[2].vy = y + h - 0x70;
    v[2].vz = 0;
    v[3].vx = x + w - 0xA0;
    v[3].vy = y + h - 0x70;
    v[3].vz = 0;
}

/* Noah playMenuSoundEffect */
void playMenuSoundEffect(u8 sfx) {
    if (gMenuContext->m32A_soundEnabled) {
        playSoundEffect((gMenuContext->m2E4_musicSequence->u14.m14_id << 16) | sfx);
    }
}

/* Noah menu2CheckBitmask family: bit mask menu2BitmaskMsbFirst[bit] */
u16 getMenuBitMaskMsbFirst(u8 arg0) {
    return menu2BitmaskMsbFirst[arg0];
}

/* Noah menu2CheckBitmask family: bit mask menu2Bitmask[bit] */
u16 getMenuBitMask(u8 arg0) {
    return menu2Bitmask[arg0];
}

/* Noah menu2CheckBitmask family: ~menu2BitmaskMsbFirst[bit] */
u16 getMenuClearMaskMsbFirst(u8 arg0) {
    return ~menu2BitmaskMsbFirst[arg0];
}

/* Noah menu2CheckBitmask family: ~menu2Bitmask[bit] */
u16 getMenuClearMask(u8 arg0) {
    return ~menu2Bitmask[arg0];
}

/* Noah menu2CheckBitmask family: mask & menu2BitmaskMsbFirst[bit] */
u16 menu2CheckBitmaskMsbFirst(u16 arg0, u8 arg1) {
    return menu2BitmaskMsbFirst[arg1] & arg0;
}

/* Noah menu2CheckBitmask family: mask & menu2Bitmask[bit] */
u16 menu2CheckBitmask(u16 mask, u8 bit) {
    return menu2Bitmask[bit] & mask;
}

/* Noah menu2CheckBitmask family: mask & menu2Bitmask32[bit] (32-bit) */
u32 menu2CheckBitmask32(u32 mask, u8 bit) {
    return mask & menu2Bitmask32[bit];
}

/* Noah checkCurrentDiscNumber */
void checkCurrentDiscNumber(u8 disc) {
    u8 loop;
    u8 n;

    processLoadSaveMenuSub1();
    loop = 1;
    while (gMenuContext->m329_animationState != 0) {
        menuDraw();
    }
    setupDraw428(0);
    while (loop) {
        if (getCurrentDiscNumber() == disc + 1) {
            loop = 0;
        } else {
            stopCdDrive();
            processLoadSaveMenuSub5(disc * 3 - 0x7D);
            if (checkInsertedDisc(disc + 1)) {
                processLoadSaveMenuSub7();
                processLoadSaveMenuSub5(0x89);
                n = 0x1D;
                do {
                    menuDraw();
                } while (--n);
                processLoadSaveMenuSub7();
                menuDraw();
            } else {
                processLoadSaveMenuSub7();
                loop = 0;
            }
        }
    }
    disableDraw428();
}

/* UnDeliverEvent x4 on the card events */
void undeliverMemoryCardEvents(void) {
    func_800404C4(0xF4000001, 4);
    func_800404C4(0xF4000001, 0x8000);
    func_800404C4(0xF4000001, 0x100);
    func_800404C4(0xF4000001, 0x2000);
}

/* wait for a card event (3 new device, 1 error, 0 I/O end, 2 timeout) */
u8 waitMemoryCardEvent(void) {
    while (1) {
        if (func_80040494(gMenuContext->m32C_memoryCardContext->m4FEC_events[3]) == 1) {
            undeliverMemoryCardEvents();
            return 3;
        }
        if (func_80040494(gMenuContext->m32C_memoryCardContext->m4FEC_events[1]) == 1) {
            undeliverMemoryCardEvents();
            return 1;
        }
        if (func_80040494(gMenuContext->m32C_memoryCardContext->m4FEC_events[0]) == 1) {
            undeliverMemoryCardEvents();
            return 0;
        }
        if (func_80040494(gMenuContext->m32C_memoryCardContext->m4FEC_events[2]) == 1) {
            undeliverMemoryCardEvents();
            return 2;
        }
    }
}

/* card info on a channel -> memoryCardEventStatus status */
s32 getMemoryCardStatus(s32 chan) {
    if (_card_info(chan)) {
        return memoryCardEventStatus[waitMemoryCardEvent()];
    }
    return -1;
}

/* Noah closeMemoryCardsEvents */
void closeMemoryCardsEvents(void) {
    menuDraw();
    EnterCriticalSection();
    CloseEvent(gMenuContext->m32C_memoryCardContext->m4FEC_events[0]);
    CloseEvent(gMenuContext->m32C_memoryCardContext->m4FEC_events[1]);
    CloseEvent(gMenuContext->m32C_memoryCardContext->m4FEC_events[2]);
    CloseEvent(gMenuContext->m32C_memoryCardContext->m4FEC_events[3]);
    ExitCriticalSection();
}

/* poll one memory card slot */
u8 pollMemoryCardSlot(u8 chan) {
    u8 ok;
    s32 r;
    s32 i;

    ok = 1;
    gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[chan] = 1;
    r = getMemoryCardStatus(chan != 0 ? 0x10 : 0);
    if (r == 0 && gMenuContext->m32C_memoryCardContext->m4F74_cardStatus[chan] == -1) {
        r = 1;
        gMenuContext->m32C_memoryCardContext->m4F74_cardStatus[chan] = 0;
    } else {
        gMenuContext->m32C_memoryCardContext->m4F74_cardStatus[chan] = r;
    }
    if (r == -1) {
        gMenuContext->m32C_memoryCardContext->m4F8A_fileCount[chan] = 0;
        gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[chan] = 0;
        processLoadSaveMenuSub6Var2[chan] = 0;
        for (i = 0; i < 16; i++) {
            gMenuContext->m32C_memoryCardContext->m4FAE_tileFiles[chan * 16 + i] = 0xFF;
            gMenuContext->m32C_memoryCardContext->m4F8E_isGameSave[chan * 16 + i] = 0;
            gMenuContext->m32C_memoryCardContext->m0_files[chan * 16 + i].m58_loaded = 0;
        }
    }
    if (r == -2) {
        ok = 0;
    }
    if (gMenuContext->m32C_memoryCardContext->m4FE8_prevCardPresent[chan] != gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[chan]) {
        gMenuContext->m32C_memoryCardContext->m4F88_dirScanned[chan] = 0;
        gMenuContext->m32C_memoryCardContext->m4FE8_prevCardPresent[chan] = gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[chan];
    }
    return ok;
}

/* periodic card poll from menuDraw */
void updateMemoryCardPolling(void) {
    if (gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2]) {
        if (++gMenuContext->m326_cardPollTimer > processLoadSaveMenuSub6Var1) {
            pollMemoryCardSlot(0);
            pollMemoryCardSlot(1);
            if (gMenuContext->m32C_memoryCardContext->m4F74_cardStatus[0] == -1 &&
                gMenuContext->m32C_memoryCardContext->m4F74_cardStatus[1] == -1) {
                gMenuContext->m334_cardOperation = 0;
            }
            gMenuContext->m326_cardPollTimer = 0;
        }
    }
}

/* wait 59 frames unless no card */
void makeSaveBlockDisplay(u8 chan) {
    s32 n;

    if (gMenuContext->m32C_memoryCardContext->m4F74_cardStatus[chan] != -2) {
        gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 2;
        n = 0x3B;
        do {
            menuDraw();
        } while (--n);
        gMenuContext->m32C_memoryCardContext->m4FE4_cardPresent[2] = 0;
    }
}

/* wait 59 vsyncs unless no card */
void waitMemoryCardVSyncs(u8 chan) {
    s32 n;

    if (gMenuContext->m32C_memoryCardContext->m4F74_cardStatus[chan] != -2) {
        n = 0x3B;
        do {
            VSync(0);
        } while (--n);
    }
}
