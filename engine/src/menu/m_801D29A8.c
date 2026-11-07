/*
 * Info card slide in/out, main menu setup, message box, glyph marker and arrows.
 * Provisional file 0x801D29A8-0x801D3674. gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "menu/menu.h"
#include "main/memory.h"
#include "main/game_state.h"
#include "main/dialog.h"

extern s32 statusMenuArrowX[];
extern s32 statusMenuArrowY[];

/* Noah processLoadSaveMenuSub2 (info cards slide in/out) */
void processLoadSaveMenuSub2(u8 show, u8 hideGold) {
    s32 i;

    if (show) {
        j_setupMenuContext4E0(8, gMenuContext->m6E0_mainMenuLabels, mainMenuConfig2, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
        setupInfoCardAnimation(0x100, 0x86, 0x60, 6, 8, 0);
        setupInfoCardAnimation(0x108, 0x3E, 0x68, 0x3E, 8, 1);
        setupInfoCardAnimation(0x110, -10, 0x70, 0x76, 8, 2);
    } else {
        countField33C_C(8, gMenuContext->m33C_drawFlags->mC_drawMainMenuLabels);
        setupInfoCardAnimation(0x60, 6, 0x100, 0x86, 8, 0);
        setupInfoCardAnimation(0x68, 0x3E, 0x108, 0x3E, 8, 1);
        setupInfoCardAnimation(0x70, 0x76, 0x110, -10, 8, 2);
    }
    while (gMenuContext->m0_infoCardAnimations[0].m23_isAnimationDone == 0 && gMenuContext->m0_infoCardAnimations[1].m23_isAnimationDone == 0 &&
           gMenuContext->m0_infoCardAnimations[2].m23_isAnimationDone == 0) {
        for (i = 0; i < 3; i++) {
            if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
                updateCharacterInfoCard(i, gMenuContext->m33C_drawFlags->m30_partyMemberId[i]);
            }
        }
        menuDraw();
        for (i = 0; i < 3; i++) {
            if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
                infocardAnimationStep(i);
            }
        }
    }
    if (show) {
        gMenuContext->m0_infoCardAnimations[0].m0_startX = 0x60;
        gMenuContext->m0_infoCardAnimations[0].m8_startY = 6;
        gMenuContext->m0_infoCardAnimations[1].m0_startX = 0x68;
        gMenuContext->m0_infoCardAnimations[1].m8_startY = 0x3E;
        gMenuContext->m0_infoCardAnimations[2].m0_startX = 0x70;
        gMenuContext->m0_infoCardAnimations[2].m8_startY = 0x76;
        gMenuContext->m0_infoCardAnimations[0].m1C_YOffset = 0;
        gMenuContext->m0_infoCardAnimations[0].m18_XOffset = 0;
        gMenuContext->m0_infoCardAnimations[1].m1C_YOffset = 0;
        gMenuContext->m0_infoCardAnimations[1].m18_XOffset = 0;
        gMenuContext->m0_infoCardAnimations[2].m1C_YOffset = 0;
        gMenuContext->m0_infoCardAnimations[2].m18_XOffset = 0;
        for (i = 0; i < 3; i++) {
            if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
                updateCharacterInfoCard(i, gMenuContext->m33C_drawFlags->m30_partyMemberId[i]);
            }
        }
        playMenuSoundEffect(0x5D);
        if (!hideGold) {
            updateGoldDisplay();
        }
        gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[1] = 1;
        gMenuContext->m33C_drawFlags->m6_drawPlayTime = 1;
    } else {
        gMenuContext->m33C_drawFlags->m0_isInfoCardEnabled[0] = gMenuContext->m33C_drawFlags->m0_isInfoCardEnabled[1] =
            gMenuContext->m33C_drawFlags->m0_isInfoCardEnabled[2] = 0;
        if (!hideGold) {
            gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[0] = 0;
            gMenuContext->m33C_drawFlags->m5_drawGold = 0;
        }
    }
    menuDraw();
}

/* Noah setupMenu0 */
void setupMenu0(void) {
    s32 i;
    s32 j;
    s32 gear;

    if (menuToEnter == 0) {
        for (j = 0; j < 2; j++) {
            gMenuContext->m364_menuBoxes[j] = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_364), (s32)0x720), 0);
            bzero(gMenuContext->m364_menuBoxes[j], (s32)sizeof(struct MenuContext_364));
            gMenuContext->m380_menuBoxAnims[j] = allocateHeap(PC_HEAP_BYTES(0x18), 0);
            bzero(gMenuContext->m380_menuBoxAnims[j], 0x18);
            setupMenuContext364(j);
        }
        playMenuSoundEffect(0x5E);
    }
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF) {
            renderCharacterNameInVram(gMenuContext->m33C_drawFlags->m30_partyMemberId[i], i * 2);
            gear = gameState.m26C_party[gMenuContext->m33C_drawFlags->m30_partyMemberId[i]].mA0_gearNum;
            if (gear != 0xFF) {
                renderCharacterNameInVram(gear + 0xB, (i + 3) * 2);
            } else {
                renderCharacterNameInVram(0xFF, (i + 3) * 2);
            }
        }
    }
    setupMainMenuWithConfig(8, mainMenuConfig);
    processLoadSaveMenuSub2(1, 0);
    initMenuForPlaytime();
}

/* character portrait/status refresh: 7C3C/7CFC/8DE4/8EA4 */
void setupStatusMenuForCharacter3(u8 a, u8 b) {
    setupStatusMenuForCharacter3Sub0(a, b);
    setupStatusMenuForCharacter3Sub1(a, b, gameState.m22B1_isOnGear[a]);
    menuDraw();
    setupStatusMenuForCharacter3Sub2(a, 0, 0, b);
    menuDraw();
    setupStatusScreenEquipNames(a, 0, 0, b);
}

/* Noah processLoadSaveMenuSub5 (3-line message box) */
void processLoadSaveMenuSub5(u8 msg) {
    MenuContext_380* box;
    MenuContext_4E0* p;
    s32 i;
    s32 x;

    iniMenuContext364And380(2, 0x7A, 0x96, 0xBC, 0x40, 1, 1, 4, 0);
    box = gMenuContext->m380_menuBoxAnims[2];
    while (!box->m11_done) {
        menuDraw();
    }
    x = 0x84;
    for (i = 0; i < 4; i++) {
        gMenuContext->m1DE0_messageLines[i] = allocateHeap(PC_HEAP_SIZED((s32)sizeof(MenuContext_4E0), (s32)0x80), 0);
        bzero(gMenuContext->m1DE0_messageLines[i], (s32)sizeof(MenuContext_4E0));
        if ((i & 1) == 0) {
            gMenuContext->m1DE0_messageLines[i]->m78_imageData = ALLOC_TEXT_IMAGE(0x5CA);
            gMenuContext->m1DE0_messageLines[i]->m70_rect.x = 0x140;
            gMenuContext->m1DE0_messageLines[i]->m70_rect.y = (i / 2) * 0xD + 0x4E;
            gMenuContext->m1DE0_messageLines[i]->m70_rect.w = 0x3A;
            gMenuContext->m1DE0_messageLines[i]->m70_rect.h = 0xD;
        } else {
            gMenuContext->m1DE0_messageLines[i]->m78_imageData = gMenuContext->m1DE0_messageLines[i - 1]->m78_imageData;
        }
    }
    for (i = 0; i < 3; i++) {
        p = gMenuContext->m1DE0_messageLines[i];
        p->m7E_stringWidth =
            renderString(getDialogParamPointer(gMenuContext->m2E0_textBundle, msg + i), p->m78_imageData, 0x36, i - (i / 2) * 2);
        initMenuContext_1DE0(p, i, 0, 0);
        setupMenuPolyFT4(&p->m0_polys[gMenuContext->m308_oddOrEven], x, i * 16 + 0xA0, 0, (i / 2) * 0xD + 0x4E,
                      p->m7E_stringWidth, 0xD);
        initMemoryCardPolyVerts(p->m50_vertex, x, i * 16 + 0xA0, p->m7E_stringWidth, 0xD);
        p->m7D_oddOrEven = gMenuContext->m308_oddOrEven;
        p->m7F_transformEnabled = 1;
    }
    LoadImage(&gMenuContext->m1DE0_messageLines[0]->m70_rect, gMenuContext->m1DE0_messageLines[0]->m78_imageData);
    LoadImage(&gMenuContext->m1DE0_messageLines[2]->m70_rect, gMenuContext->m1DE0_messageLines[2]->m78_imageData);
    DrawSync(0);
    gMenuContext->m33C_drawFlags->m2E_drawCheckingMemorycardText = 1;
    freeHeap(gMenuContext->m1DE0_messageLines[0]->m78_imageData);
    freeHeap(gMenuContext->m1DE0_messageLines[2]->m78_imageData);
    menuDraw();
    menuDraw();
}

/* Noah processLoadSaveMenuSub7 (close message box) */
void processLoadSaveMenuSub7(void) {
    s32 i;

    if (gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[2]) {
        processLoadSaveMenuSub7Sub0(2);
        gMenuContext->m33C_drawFlags->m2E_drawCheckingMemorycardText = 0;
        for (i = 0; i < 4; i++) {
            freeHeap(gMenuContext->m1DE0_messageLines[i]);
        }
    }
    menuDraw();
}

/* glyph 0x107 marker in m43C */
void setupScrollBarThumb(s32 x, s32 y, s32 h) {
    if (!gMenuContext->m33C_drawFlags->m49_draw43C) {
        gMenuContext->m43C_scrollBarThumb = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_43C), (s32)0x74), 0);
        bzero(gMenuContext->m43C_scrollBarThumb, (s32)sizeof(struct MenuContext_43C));
    }
    setupStringInPolyFT4(gMenuContext->m2DC_font, 0x107, gMenuContext->m43C_scrollBarThumb, gMenuContext->m308_oddOrEven, x, y, 0x1000);
    initMemoryCardPolyVerts(gMenuContext->m43C_scrollBarThumb->m50_vertex, x, y, 8, h);
    gMenuContext->m43C_scrollBarThumb->m70_oddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m33C_drawFlags->m49_draw43C = 1;
}

/* free m43C */
void freeScrollBarThumb(void) {
    gMenuContext->m33C_drawFlags->m49_draw43C = 0;
    freeHeap(gMenuContext->m43C_scrollBarThumb);
}

/* left/right arrows 0x164/0x165 in m440 */
void initStatusMenuSub1(u8 which, u8 mode) {
    s32 i;
    s32 count;

    if (mode == 0) {
        count = gMenuContext->m32B_numPartyMembers;
    } else {
        count = gMenuContext->m33B_numGears;
    }
    if (count != 1) {
        if (!gMenuContext->m33C_drawFlags->m67_arrowsAllocated) {
            gMenuContext->m440_arrows = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_440), (s32)0x1c4), 0);
            bzero(gMenuContext->m440_arrows, (s32)sizeof(struct MenuContext_440));
            gMenuContext->m33C_drawFlags->m67_arrowsAllocated = 1;
        }
        for (i = 0; i < 2; i++) {
            setupStringInPolyFT4(gMenuContext->m2DC_font, 0x164 + i, &gMenuContext->m440_arrows->m0_polys[i * 4],
                          gMenuContext->m308_oddOrEven, statusMenuArrowX[i], statusMenuArrowY[which], 0x1000);
        }
        for (i = 0; i < 4; i++) {
            initMemoryCardPolyVerts(gMenuContext->m440_arrows->m140_vertex[i], gMenuContext->m440_arrows->m0_polys[i * 2 + gMenuContext->m308_oddOrEven].x0,
                          gMenuContext->m440_arrows->m0_polys[i * 2 + gMenuContext->m308_oddOrEven].y0,
                          gMenuContext->m440_arrows->m0_polys[i * 2 + gMenuContext->m308_oddOrEven].x1 -
                              gMenuContext->m440_arrows->m0_polys[i * 2 + gMenuContext->m308_oddOrEven].x0,
                          gMenuContext->m440_arrows->m0_polys[i * 2 + gMenuContext->m308_oddOrEven].y3 -
                              gMenuContext->m440_arrows->m0_polys[i * 2 + gMenuContext->m308_oddOrEven].y0);
        }
        gMenuContext->m440_arrows->m1C0_oddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m33C_drawFlags->m53_draw440 = 1;
    }
}
