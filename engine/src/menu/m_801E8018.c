/*
 * Shared helpers: label flags and selection, main menu setup, load/save sub-steps,
 * name rendering, poly setup, CD/PC loaders and the disc check. Provisional file
 * 0x801E8018-0x801E96A4; owns .rodata 0x801C5278-0x801C531C. gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "menu/menu.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/game_state.h"

s32 renderString(u8* text, u16* image, s16 width, s32 odd);
s32 getCdromFnd(void);
void setCdMode(s32);
void stopCdRead(s32);
s32 PCclose(s32 fd);

extern s32 itemTargetLabelX[];
extern s32 itemTargetLabelY[];
extern s32 etherListLabelX[];
extern s32 etherListLabelY[];
extern s32 D_801E9F30[];
extern s32 gearMenuLabelX[];
extern s32 gearMenuLabelY[];
extern u32 saveGameMainMenuConfig3[88]; /* Noah: saveGameMainMenuConfig3, [row][4][2] flattened */
/* The CD result buffer: 8 bytes up to yesNoPromptCancelled; checkInsertedDisc
 * tests its byte 1 (the error byte). The PC declares its full extent. */
extern u8 cdResultBuffer[8];
extern u8* fatFileTableBuffer;
extern u16* fatDirectoryTableBuffer;
extern sptr cdromFnd;

void loadPcFile(char* name, void* buf, s32 size);

/* Noah j_setupMenuContext4E0 */
void j_setupMenuContext4E0(u8 count, MenuContext_4E0* p, void* config, u8* flags) {
    setupMenuContext4E0(p, config, 4, count);
}

/* Noah countField33C_C (clear a flag array) */
void countField33C_C(u8 count, u8* arr) {
    s32 i;

    for (i = 0; i < count; i++) {
        arr[i] = 0;
    }
}

/* Noah updateMenuSelection2 (owns jtbl_801C5278) */
void updateMenuSelection2(s32 count, MenuContext_4E0* labels, void* unused, s32* xOffsets, u8* flags, u8 sel, u8 base,
                   u8 mode) {
    s32 off;

    off = 0;
    switch (mode) {
    case 0:
        countField33C_C(count, flags);
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x0 = cursorPosXTable[base + sel] + (s16)(xOffsets[sel] + 0x16);
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y0 = cursorPosYTable[base + sel] - 0x22;
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x1 = labels[sel].m7E_stringWidth + (cursorPosXTable[base + sel] + 0x16 + xOffsets[sel]);
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y1 = cursorPosYTable[base + sel] - 0x22;
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x2 = cursorPosXTable[base + sel] + (s16)(xOffsets[sel] + 0x16);
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y2 = cursorPosYTable[base + sel] - 0x15;
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x3 = labels[sel].m7E_stringWidth + (cursorPosXTable[base + sel] + 0x16 + xOffsets[sel]);
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y3 = cursorPosYTable[base + sel] - 0x15;
        break;
    case 1:
        initMemoryCardPolyVerts(labels[sel].m50_vertex, itemTargetLabelX[sel], itemTargetLabelY[0], labels[sel].m7E_stringWidth, 0xD);
        break;
    case 5:
        off = 8;
    case 2:
        initMemoryCardPolyVerts(labels[sel].m50_vertex, etherListLabelX[off + sel], etherListLabelY[base], labels[sel].m7E_stringWidth,
                      0xD);
        break;
    case 3:
        initMemoryCardPolyVerts(labels[sel].m50_vertex, 0x18, D_801E9F30[base], labels[sel].m7E_stringWidth, 0xD);
        break;
    case 4:
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x0 = 0x4C;
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y0 = 0x12;
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x1 = labels->m7E_stringWidth + 0x4C;
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y1 = 0x12;
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x2 = 0x4C;
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y2 = 0x1F;
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->x3 = labels->m7E_stringWidth + 0x4C;
        (labels[sel].m0_polys + gMenuContext->m308_oddOrEven)->y3 = 0x1F;
        break;
    case 6:
        initMemoryCardPolyVerts(labels[sel].m50_vertex, gearMenuLabelX[sel], gearMenuLabelY[sel], labels[sel].m7E_stringWidth, 0xD);
        break;
    }
    labels[sel].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
    flags[sel] = 1;
}

/* Noah setupMainMenuWithConfig */
void setupMainMenuWithConfig(s32 count, u32 (*config)[2]) {
    s32 i;
    s32 k;

    gMenuContext->m350_mainMenu->m1192_dimmed = 0;
    gMenuContext->m350_mainMenu->m1193_appliedDimmed = 0;
    gMenuContext->m33C_drawFlags->m9_drawMainMenu = 1;
    for (i = 1; i <= count; i++) {
        if (i != count) {
            gMenuContext->m350_mainMenu->m1188_numEntries2 = 0;
            for (k = 0; k < i; k++) {
                gMenuContext->m350_mainMenu->m1188_numEntries2 += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, config[k][0],
                    &gMenuContext->m350_mainMenu->m0_polys[gMenuContext->m350_mainMenu->m1188_numEntries2],
                    gMenuContext->m308_oddOrEven, 0xA0, 0x96, 0x1000);
            }
            gMenuContext->m350_mainMenu->m1190_0OddOrEven = gMenuContext->m308_oddOrEven;
        }
        gMenuContext->m350_mainMenu->m118C_numEntries = 0;
        if (i != 1) {
            for (k = 0; k < i - 1; k++) {
                gMenuContext->m350_mainMenu->m118C_numEntries += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, config[k][1],
                    &gMenuContext->m350_mainMenu->m8C0_polys[gMenuContext->m350_mainMenu->m118C_numEntries],
                    gMenuContext->m308_oddOrEven, 0xA0, 0x96, 0x1000);
            }
            gMenuContext->m350_mainMenu->m1191_8C0OddOrEven = gMenuContext->m308_oddOrEven;
        }
        for (k = 0; k < 2; k++) {
            menuDraw();
        }
    }
}

/* Noah processLoadSaveMenuSub3 (the second inner loop must be a goto loop) */
void processLoadSaveMenuSub3(u8 row) {
    MenuContext* ctx;
    s32 draw;
    s32 n;
    s32 i;

    ctx = gMenuContext;
    draw = 1;
    n = 1;
    gMenuContext->m354_subMenu->m1400_0Length = 0;
    ctx->m354_subMenu->m1404_500Length = 0;
    ctx->m33C_drawFlags->mA_354Enabled = 1;
    do {
        ctx = gMenuContext;
        gMenuContext->m354_subMenu->m1400_0Length = 0;
        ctx->m33A_numMenuEntries = 0;
        for (i = 0; i < n; i++) {
            if (saveGameMainMenuConfig3[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2] != 0xFFFF) {
                gMenuContext->m354_subMenu->m1400_0Length += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, saveGameMainMenuConfig3[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2],
                    &gMenuContext->m354_subMenu->m0_polys[gMenuContext->m354_subMenu->m1400_0Length], gMenuContext->m308_oddOrEven,
                    0xA0, 0x96, 0x1000);
                gMenuContext->m33A_numMenuEntries++;
            } else {
                draw = 0;
            }
        }
        gMenuContext->m354_subMenu->m1408_0OddOrEven = gMenuContext->m308_oddOrEven;
        if (draw) {
            for (i = 0; i < 2; i++) {
                menuDraw();
            }
        }
        gMenuContext->m354_subMenu->m1404_500Length = 0;
        i = 0;
        if (i < n) {
        loop2:
            if (saveGameMainMenuConfig3[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2] != 0xFFFF) {
                gMenuContext->m354_subMenu->m1404_500Length += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, saveGameMainMenuConfig3[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2 + 1],
                    &gMenuContext->m354_subMenu->m500_polys[gMenuContext->m354_subMenu->m1404_500Length],
                    gMenuContext->m308_oddOrEven, 0xA0, 0x96, 0x1000);
            }
            i++;
            if (i < n) goto loop2;
        }
        gMenuContext->m354_subMenu->m1409_500OddOrEven = gMenuContext->m308_oddOrEven;
        if (draw) {
            for (i = 0; i < 2; i++) {
                menuDraw();
            }
        }
        n++;
    } while (n < 5);
}

/* Noah updateMenuSelection */
void updateMenuSelection(u8 count, u8 sel, u32 (*config)[2]) {
    s32 i;
    s32 glyph;

    gMenuContext->m350_mainMenu->m1188_numEntries2 = 0;
    gMenuContext->m350_mainMenu->m118C_numEntries = 0;
    for (i = 0; i < count; i++) {
        if (i == sel) {
            glyph = config[i][0] + 0xD;
        } else {
            glyph = config[i][0];
        }
        gMenuContext->m350_mainMenu->m1188_numEntries2 += setupStringInPolyFT4(
            gMenuContext->m2DC_font, glyph,
            &gMenuContext->m350_mainMenu->m0_polys[gMenuContext->m350_mainMenu->m1188_numEntries2],
            gMenuContext->m308_oddOrEven, 0xA0, 0x96, 0x1000);
        gMenuContext->m350_mainMenu->m118C_numEntries += setupStringInPolyFT4(
            gMenuContext->m2DC_font, config[i][1],
            &gMenuContext->m350_mainMenu->m8C0_polys[gMenuContext->m350_mainMenu->m118C_numEntries],
            gMenuContext->m308_oddOrEven, 0xA0, 0x96, 0x1000);
    }
    gMenuContext->m350_mainMenu->m1190_0OddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m350_mainMenu->m1191_8C0OddOrEven = gMenuContext->m308_oddOrEven;
    updateCursorPolysLocation(sel, 1);
    gMenuContext->m33C_drawFlags->m4_drawCursor = 1;
}

/* Noah processLoadSaveMenuSub8 */
void processLoadSaveMenuSub8(u8 row) {
    s32 i;
    s32 glyph;

    gMenuContext->m354_subMenu->m1400_0Length = 0;
    gMenuContext->m354_subMenu->m1404_500Length = 0;
    for (i = 0; i < gMenuContext->m33A_numMenuEntries; i++) {
        if (i == gMenuContext->m338_currentSelectedSubMenuEntry) {
            glyph = saveGameMainMenuConfig3[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2] + 0xD;
        } else {
            glyph = saveGameMainMenuConfig3[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2];
        }
        gMenuContext->m354_subMenu->m1400_0Length += setupStringInPolyFT4(
            gMenuContext->m2DC_font, glyph, &gMenuContext->m354_subMenu->m0_polys[gMenuContext->m354_subMenu->m1400_0Length],
            gMenuContext->m308_oddOrEven, 0xA0, 0x96, 0x1000);
        gMenuContext->m354_subMenu->m1404_500Length += setupStringInPolyFT4(
            gMenuContext->m2DC_font, saveGameMainMenuConfig3[(row + gMenuContext->m336_menuSelectedEntry) * 8 + i * 2 + 1],
            &gMenuContext->m354_subMenu->m500_polys[gMenuContext->m354_subMenu->m1404_500Length], gMenuContext->m308_oddOrEven,
            0xA0, 0x96, 0x1000);
    }
    gMenuContext->m354_subMenu->m1408_0OddOrEven = gMenuContext->m308_oddOrEven;
    gMenuContext->m354_subMenu->m1409_500OddOrEven = gMenuContext->m308_oddOrEven;
    updateCursorPolysLocation(gMenuContext->m338_currentSelectedSubMenuEntry + 7, 1);
    gMenuContext->m33C_drawFlags->m4_drawCursor = 1;
}

/* Noah renderCharacterNameInVram */
void renderCharacterNameInVram(u8 slot, u8 pos) {
    u16* image;
    RECT rect;

    image = ALLOC_TEXT_IMAGE(0x3F6);
    bzero(image, 0x3F6);
    if (slot != 0xFF) {
        renderString(gameState.m0_names[slot / 2 * 2], image, 0x24, 0);
        renderString(gameState.m0_names[slot / 2 * 2 + 1], image, 0x24, 1);
    }
    rect.x = infocardNameU[pos / 2] + 0x180;
    rect.y = infocardNameV[pos / 2];
    rect.w = 0x28;
    rect.h = 0xD;
    LoadImage(&rect, image);
    DrawSync(0);
    freeHeap(image);
}

/* poly shade/semi-trans mode: 0 opaque, 1 additive (tpage |= 0x20), 2 plain, 3 dim */
void setMenuPolyShadeMode(POLY_FT4* p, u8 mode) {
    u8 col;

    SetShadeTex(p, 0);
    switch (mode) {
        /* the one-shot loop around case 1 makes reorg predict the branch to it as taken
           (a NOTE_INSN_LOOP_BEG right before its label), which fills that delay slot with
           `move a0,s1` from the target like retail */
        do {
        case 1:
            p->tpage |= 0x20;
            SetSemiTrans(p, 1);
            col = 0x21;
        } while (0);
        break;
    case 0:
        SetSemiTrans(p, 0);
    case 2:
        col = 0x80;
        break;
    case 3:
        col = 0x21;
        break;
    default:
        return;
    }
    p->r0 = col;
    p->g0 = col;
    p->b0 = col;
}

/* menu box: set the shade/semi-trans mode of all its polys */
void setMenuBoxShadeMode(u8 box, u8 mode) {
    s32 i;

    if (mode) {
        mode = 3;
    } else {
        mode = 2;
    }
    for (i = 0; i < 4; i++) {
        setMenuPolyShadeMode(&gMenuContext->m364_menuBoxes[box]->m0_corners[0][i * 2 + gMenuContext->m364_menuBoxes[box]->m71C_oddOrEven], mode);
    }
    for (i = 0; i < 4; i++) {
        setMenuPolyShadeMode(&gMenuContext->m364_menuBoxes[box]->m140_topEdge[0][i * 2 + gMenuContext->m364_menuBoxes[box]->m71C_oddOrEven], mode);
    }
    for (i = 0; i < 4; i++) {
        setMenuPolyShadeMode(&gMenuContext->m364_menuBoxes[box]->m1E0_bottomEdge[0][i * 2 + gMenuContext->m364_menuBoxes[box]->m71C_oddOrEven], mode);
    }
    for (i = 0; i < 4; i++) {
        setMenuPolyShadeMode(&gMenuContext->m364_menuBoxes[box]->m280_leftEdge[0][i * 2 + gMenuContext->m364_menuBoxes[box]->m71C_oddOrEven], mode);
    }
    for (i = 0; i < 4; i++) {
        setMenuPolyShadeMode(&gMenuContext->m364_menuBoxes[box]->m320_rightEdge[0][i * 2 + gMenuContext->m364_menuBoxes[box]->m71C_oddOrEven], mode);
    }
    for (i = 0; i < 2; i++) {
        setMenuPolyShadeMode(&gMenuContext->m364_menuBoxes[box]->m410_innerCorners[0][i * 2 + gMenuContext->m364_menuBoxes[box]->m71C_oddOrEven], mode);
    }
    setMenuPolyShadeMode(&gMenuContext->m364_menuBoxes[box]->m3C0_innerEdge[gMenuContext->m364_menuBoxes[box]->m71C_oddOrEven], mode);
}

/* Noah initMenuTiles1Sub */
void initMenuTiles1Sub(POLY_FT4* p) {
    SetSemiTrans(p, 1);
    SetShadeTex(p, 0);
    p->r0 = 0x80;
    p->g0 = 0x80;
    p->b0 = 0x80;
}

/* Noah setupMenuPolyFT4 */
void setupMenuPolyFT4(POLY_FT4* p, u16 x, u16 y, u8 u, u8 v, u16 w, u16 h) {
    p->x0 = x;
    p->y0 = y;
    p->y1 = y;
    p->x2 = x;
    p->u0 = u;
    p->u2 = u;
    p->x1 = x + w;
    p->y2 = y + h;
    p->x3 = x + w;
    p->y3 = y + h;
    p->v0 = v;
    p->u1 = u + w;
    p->v1 = v;
    p->v2 = v + h;
    p->u3 = u + w;
    p->v3 = v + h;
}

/* Noah initMemoryCardTransparentPoly */
void initMemoryCardTransparentPoly(POLY_FT4* p) {
    SetPolyFT4(p);
    SetSemiTrans(p, 0);
    SetShadeTex(p, 0);
    p->r0 = 0x80;
    p->g0 = 0x80;
    p->b0 = 0x80;
}

/* CD: stop/pause the drive, wait for CdControlB(8) */
void stopCdDrive(void) {
    if (getCdromFnd() == 0) {
        stopCdRead(0);
        waitReadCompletion(0);
        setCdMode(0);
        waitReadCompletion(0);
        VSync(3);
        do {
            VSync(3);
        } while (CdControlB(8, 0, cdResultBuffer) == 0);
    }
}

/* PC file load via PCopen/PCread/PCclose */
void loadPcFile(char* name, void* buf, s32 size) {
    s32 fd;

    fd = PCopen(name, 0, 0);
    PCread(fd, buf, size);
    PCclose(fd);
}

/* disc check: PC files or CD sector 0x17 "_XEN" + disc digit (a goto retry loop) */
#if VERSION_JP
/* JP follows this string with a byte of linker fill, so it stays assembly. */
extern char D_801C5308[];
#endif
s32 checkInsertedDisc(s32 disc) {
    s32 buf[4] = {0, 0, 0, 0};
    CdlLOC loc;
    s32 ret;
    s32 r;

    waitReadCompletion(0);
    if (getCdromFnd() != 0) {
        ret = 0;
        if (disc == 1) {
            loadPcFile("c:\\work\\cdrom.mdg", fatFileTableBuffer, 0x8000);
            loadPcFile("c:\\work\\cdrom.fid", fatDirectoryTableBuffer, 0x7A);
            loadPcFile("c:\\work\\cdrom.fnd", (void*)cdromFnd, 0x40000);
        } else {
            loadPcFile("c:\\work\\cdrom2.mdg", fatFileTableBuffer, 0x8000);
            loadPcFile("c:\\work\\cdrom2.fid", fatDirectoryTableBuffer, 0x7A);
#if VERSION_JP
            loadPcFile(D_801C5308, (void*)cdromFnd, 0x40000);
#else
            loadPcFile("c:\\work\\cdrom2.fnd", (void*)cdromFnd, 0x40000);
#endif
        }
        return ret;
    }
    CdIntToPos(0, &loc);
    do {
        VSync(3);
        CdControlB(1, 0, cdResultBuffer);
    } while (!(cdResultBuffer[0] & 0x10));
    do {
        VSync(3);
        CdControlB(1, 0, cdResultBuffer);
    } while (cdResultBuffer[0] & 0x10);
    do {
        VSync(3);
        r = CdControlB(1, 0, cdResultBuffer);
    } while (!(cdResultBuffer[0] & 2) || r == 0);
retry:
    do {
        VSync(3);
    } while (CdControlB(0x13, 0, cdResultBuffer) == 0);
    do {
        VSync(3);
    } while (CdControlB(2, (u8*)&loc, cdResultBuffer) == 0);
    r = CdControlB(0x15, 0, cdResultBuffer);
    if ((cdResultBuffer[0] & 1) && (cdResultBuffer[1] & 0x40) && r == 0) {
        return 2;
    }
    if (r == 0) {
        goto retry;
    }
    setCdMode(0xA0);
    waitReadCompletion(0);
    VSync(3);
    VSync(3);
    readRawSectorsFromDisk(0x17, buf, 0x10, 0, 0);
    waitReadCompletion(0);
    if (buf[1] == 0x4E45585F) {
        if (((u8*)buf)[3] == disc + 0x30) {
            readRawSectorsFromDisk(0x18, fatFileTableBuffer, 0x8000, 0, 0);
            ret = 0;
            waitReadCompletion(0);
            readRawSectorsFromDisk(0x28, fatDirectoryTableBuffer, 0x7A, 0, 0);
            waitReadCompletion(0);
        } else {
            ret = 3;
        }
    } else {
        ret = 2;
    }
    return ret;
}

#if VERSION_JP
INCLUDE_RODATA("menu/nonmatchings/m_801E8018", D_801C5308);
#endif
