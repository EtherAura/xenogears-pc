/*
 * Cursor, memory card screen cursors (m428), status menu labels, gold and play time
 * boxes. Provisional file 0x801D1EB0-0x801D29A8. gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "menu/menu.h"

extern s32 cardCursorStartX[];
extern s32 cardCursorStartY[];
extern s32 initStatusMenuSub0LabelX[];
extern s32 initStatusMenuSub0LabelY[];
extern s32 selectedSubMenuLabelOffsetX[];
extern u8 saveGameMainMenuConfig6[];

/* Noah processLoadSaveMenuSub1Alt2 (twin of processLoadSaveMenuSub1 = Sub1) */
void processLoadSaveMenuSub1Alt2(void) {
    gMenuContext->m329_animationState = 4;
    playMenuSoundEffect(0x5C);
}

/* Noah updateCursorPolysLocation */
void updateCursorPolysLocation(s32 idx, u8 draw) {
    setupStringInPolyFT4(gMenuContext->m2DC_font, 0x108, gMenuContext->m348_cursor, gMenuContext->m308_oddOrEven,
                  cursorPosXTable[idx], cursorPosYTable[idx], 0x1000);
    gMenuContext->m348_cursor->m158_oddOrEven = gMenuContext->m308_oddOrEven;
    if (draw) {
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->x0 = cursorPosXTable[idx] + 0x14;
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->y0 = cursorPosYTable[idx] - 0x24;
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->x1 =
            cursorPosXTable[idx] + (gMenuContext->m348_cursor->m15B_selectionWidth + 0x14);
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->y1 = cursorPosYTable[idx] - 0x24;
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->x2 = cursorPosXTable[idx] + 0x14;
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->y2 = cursorPosYTable[idx] - 0x14;
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->x3 =
            cursorPosXTable[idx] + (gMenuContext->m348_cursor->m15B_selectionWidth + 0x14);
        (gMenuContext->m348_cursor->m50_polyG4 + gMenuContext->m308_oddOrEven)->y3 = cursorPosYTable[idx] - 0x14;
        (gMenuContext->m348_cursor->mC8_lines + gMenuContext->m308_oddOrEven)->x0 = cursorPosXTable[idx] + 0x14;
        (gMenuContext->m348_cursor->mC8_lines + gMenuContext->m308_oddOrEven)->y0 = cursorPosYTable[idx] - 0x24;
        (gMenuContext->m348_cursor->mC8_lines + gMenuContext->m308_oddOrEven)->x1 =
            cursorPosXTable[idx] + (gMenuContext->m348_cursor->m15B_selectionWidth + 0x14);
        (gMenuContext->m348_cursor->mC8_lines + gMenuContext->m308_oddOrEven)->y1 = cursorPosYTable[idx] - 0x24;
        (gMenuContext->m348_cursor->mC8_lines + gMenuContext->m308_oddOrEven)->x2 =
            cursorPosXTable[idx] + (gMenuContext->m348_cursor->m15B_selectionWidth + 0x14);
        (gMenuContext->m348_cursor->mC8_lines + gMenuContext->m308_oddOrEven)->y2 = cursorPosYTable[idx] - 0x14;
        (gMenuContext->m348_cursor->mF8_lines + gMenuContext->m308_oddOrEven)->x0 = cursorPosXTable[idx] + 0x14;
        (gMenuContext->m348_cursor->mF8_lines + gMenuContext->m308_oddOrEven)->y0 = cursorPosYTable[idx] - 0x24;
        (gMenuContext->m348_cursor->mF8_lines + gMenuContext->m308_oddOrEven)->x1 = cursorPosXTable[idx] + 0x14;
        (gMenuContext->m348_cursor->mF8_lines + gMenuContext->m308_oddOrEven)->y1 = cursorPosYTable[idx] - 0x14;
        (gMenuContext->m348_cursor->mF8_lines + gMenuContext->m308_oddOrEven)->x2 =
            cursorPosXTable[idx] + (gMenuContext->m348_cursor->m15B_selectionWidth + 0x14);
        (gMenuContext->m348_cursor->mF8_lines + gMenuContext->m308_oddOrEven)->y2 = cursorPosYTable[idx] - 0x14;
        gMenuContext->m348_cursor->m159_oddOrEven = gMenuContext->m308_oddOrEven;
        gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 1;
    }
}

/* Noah resetCursorState */
void resetCursorState(void) {
    gMenuContext->m33C_drawFlags->m4_drawCursor = 0;
    gMenuContext->m33C_drawFlags->m3_drawSelectionBox = 0;
}

/* Noah setupDraw428 */
void setupDraw428(u8 mode) {
    s32 i;

    gMenuContext->m33C_drawFlags->m2F_draw428 = 0;
    switch (mode) {
    case 0:
        gMenuContext->m33C_drawFlags->m2F_draw428 = 1;
        gMenuContext->m428_cardCursors->m144_followTile[0] = 1;
        gMenuContext->m428_cardCursors->m144_followTile[1] = 1;
    case 2:
        for (i = 0; i < 4; i++) {
            setupStringInPolyFT4(gMenuContext->m2DC_font, 0x108, gMenuContext->m428_cardCursors->m0_polys + i,
                          gMenuContext->m308_oddOrEven, cardCursorStartX[i], cardCursorStartY[i], 0x800);
            gMenuContext->m428_cardCursors->m148_oddOrEven[i] = gMenuContext->m308_oddOrEven;
        }
        break;
    case 3:
        setupStringInPolyFT4(gMenuContext->m2DC_font, 0x108, gMenuContext->m428_cardCursors->m0_polys, gMenuContext->m308_oddOrEven,
                      0, 0, 0x800);
        gMenuContext->m428_cardCursors->m148_oddOrEven[0] = gMenuContext->m308_oddOrEven;
        break;
    case 1:
        break;
    }
}

/* Noah disableDraw428 */
void disableDraw428(void) {
    gMenuContext->m33C_drawFlags->m2F_draw428 = 0;
}

/* Noah initStatusMenuSub0 (statusMenu.cpp) */
void initStatusMenuSub0(u8 show) {
    s32 i;

    if (show) {
        setupMenuContext4E0(gMenuContext->mAE0_subMenuLabels, saveGameMainMenuConfig6, 4, 6);
        for (i = 0; i < 3; i++) {
            initMemoryCardPolyVerts(gMenuContext->mAE0_subMenuLabels[i + 3].m50_vertex, initStatusMenuSub0LabelX[i], initStatusMenuSub0LabelY[i],
                          gMenuContext->mAE0_subMenuLabels[i + 3].m7E_stringWidth, 0xD);
            gMenuContext->mAE0_subMenuLabels[i + 3].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
            gMenuContext->mAE0_subMenuLabels[i + 3].m7F_transformEnabled = 1;
            gMenuContext->m33C_drawFlags->m14_drawSubMenuLabels[i + 3] = 1;
        }
    } else {
        for (i = 0; i < 3; i++) {
            gMenuContext->m33C_drawFlags->m14_drawSubMenuLabels[i] = 0;
        }
    }
}

/* clears m33C->m14[0..5] */
void hideSubMenuLabels(void) {
    s32 i;

    for (i = 0; i < 6; i++) {
        gMenuContext->m33C_drawFlags->m14_drawSubMenuLabels[i] = 0;
    }
}

/* Noah setupStatusMenuForCharacter4 (statusMenu.cpp) */
void setupStatusMenuForCharacter4(void) {
    initStatusMenuSub0(0);
    (gMenuContext->mAE0_subMenuLabels[gMenuContext->m338_currentSelectedSubMenuEntry].m0_polys + gMenuContext->m308_oddOrEven)->x0 = cursorPosXTable[gMenuContext->m338_currentSelectedSubMenuEntry + 7] + 0x16 + selectedSubMenuLabelOffsetX[gMenuContext->m338_currentSelectedSubMenuEntry];
    (gMenuContext->mAE0_subMenuLabels[gMenuContext->m338_currentSelectedSubMenuEntry].m0_polys + gMenuContext->m308_oddOrEven)->y0 = cursorPosYTable[gMenuContext->m338_currentSelectedSubMenuEntry + 7] - 0x22;
    (gMenuContext->mAE0_subMenuLabels[gMenuContext->m338_currentSelectedSubMenuEntry].m0_polys + gMenuContext->m308_oddOrEven)->x1 = cursorPosXTable[gMenuContext->m338_currentSelectedSubMenuEntry + 7] + 0x16 + selectedSubMenuLabelOffsetX[gMenuContext->m338_currentSelectedSubMenuEntry] + gMenuContext->mAE0_subMenuLabels[gMenuContext->m338_currentSelectedSubMenuEntry].m7E_stringWidth;
    (gMenuContext->mAE0_subMenuLabels[gMenuContext->m338_currentSelectedSubMenuEntry].m0_polys + gMenuContext->m308_oddOrEven)->y1 = cursorPosYTable[gMenuContext->m338_currentSelectedSubMenuEntry + 7] - 0x22;
    (gMenuContext->mAE0_subMenuLabels[gMenuContext->m338_currentSelectedSubMenuEntry].m0_polys + gMenuContext->m308_oddOrEven)->x2 = cursorPosXTable[gMenuContext->m338_currentSelectedSubMenuEntry + 7] + 0x16 + selectedSubMenuLabelOffsetX[gMenuContext->m338_currentSelectedSubMenuEntry];
    (gMenuContext->mAE0_subMenuLabels[gMenuContext->m338_currentSelectedSubMenuEntry].m0_polys + gMenuContext->m308_oddOrEven)->y2 = cursorPosYTable[gMenuContext->m338_currentSelectedSubMenuEntry + 7] - 0x15;
    (gMenuContext->mAE0_subMenuLabels[gMenuContext->m338_currentSelectedSubMenuEntry].m0_polys + gMenuContext->m308_oddOrEven)->x3 = cursorPosXTable[gMenuContext->m338_currentSelectedSubMenuEntry + 7] + 0x16 + selectedSubMenuLabelOffsetX[gMenuContext->m338_currentSelectedSubMenuEntry] + gMenuContext->mAE0_subMenuLabels[gMenuContext->m338_currentSelectedSubMenuEntry].m7E_stringWidth;
    (gMenuContext->mAE0_subMenuLabels[gMenuContext->m338_currentSelectedSubMenuEntry].m0_polys + gMenuContext->m308_oddOrEven)->y3 = cursorPosYTable[gMenuContext->m338_currentSelectedSubMenuEntry + 7] - 0x15;
    gMenuContext->mAE0_subMenuLabels[gMenuContext->m338_currentSelectedSubMenuEntry].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m33C_drawFlags->m14_drawSubMenuLabels[gMenuContext->m338_currentSelectedSubMenuEntry] = 1;
}

/* gold box: iniMenuContext364And380(0, ...) + gold digits */
void updateGoldDisplay(void) {
    iniMenuContext364And380(0, 0xD4, 0xB2, 0x60, 0x10, 0, 0, 4, 0);
    updateGoldPolys(0xD8, 0xB6);
}

/* Noah initMenuForPlaytime */
void initMenuForPlaytime(void) {
    iniMenuContext364And380(1, 0xCC, 0xC6, 0x50, 0x10, 0, 0, 4, 0);
    initMenuContext33C_playTime(0xD0, 0xCA);
    gMenuContext->m33C_drawFlags->m6_drawPlayTime = 1;
}

/* Noah drawMenuPlayTime */
void drawMenuPlayTime(void) {
    if (gMenuContext->m33C_drawFlags->m6_drawPlayTime) {
        initMenuContext33C_playTime(0xD0, 0xCA);
    }
}
