/*
 * Equip submenu entry, skills page (m438), character stats submenu helpers.
 * Provisional file 0x801E0F78-0x801E23CC. gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "menu/menu.h"
#include "main/memory.h"
#include "main/game_state.h"
#include "main/dialog.h"

/* equip submenu entry: allocate m35C/m360, then runEquipMenu(character, arg1, 0) */
u8 openEquipMenu(u8 character, u8 arg1);
u8 openEquipMenu(u8 character, u8 arg1) {
    gMenuContext->m35C_selectedStats = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_35C), 0x32f4), 0);
    bzero(gMenuContext->m35C_selectedStats, sizeof(MenuContext_35C));
    gMenuContext->m360_equipment = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_360), (s32)0x2ac), 0);
    bzero(gMenuContext->m360_equipment, (s32)sizeof(struct MenuContext_360));
    loadSharedMenuData2(3);
    runEquipMenu(character, arg1, 0);
    loadSharedMenuData2(0x13);
    return 1;
}

extern u8 skillPageLabelConfig[];

/* skill page setup: allocate m438, labels m17E0, gradient bars */
void initSkillMenu(void) {
    s32 i;
    s32 j;

    gMenuContext->m438_skillMenu = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_438), (s32)0x25c0), 0);
    bzero(gMenuContext->m438_skillMenu, (s32)sizeof(struct MenuContext_438));
    loadSharedMenuData2(4);
    j_setupMenuContext4E0(2, gMenuContext->m17E0_skillPageLabels, skillPageLabelConfig, gMenuContext->m33C_drawFlags->m4E_drawSkillPageLabels);
    updateMenuSelection2(2, gMenuContext->m17E0_skillPageLabels, skillPageLabelConfig, saveGameMainMenuConfig5, gMenuContext->m33C_drawFlags->m4E_drawSkillPageLabels, 0, 0, 4);
    iniMenuContext364And380(2, 0x44, 0xA, 0xE4, 0xC4, 0, 1, 4, 0);
    gMenuContext->m33C_drawFlags->m6_drawPlayTime = 0;
    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[1] = 0;
    for (i = 0, j = 0; i < 3; i++) {
        if (gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 0xFF &&
            gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 7 && gMenuContext->m33C_drawFlags->m30_partyMemberId[i] != 8) {
            j++;
        }
    }
    if (j >= 2) {
        initStatusMenuSub1(3, 0);
    } else {
        gMenuContext->m33C_drawFlags->m53_draw440 = 0;
    }
    for (i = 0; i < 13; i++) {
        for (j = 0; j < 2; j++) {
            SetPolyG4(gMenuContext->m438_skillMenu->m21D0_bars[i] + j);
            (gMenuContext->m438_skillMenu->m21D0_bars[i] + j)->r0 = 0;
            (gMenuContext->m438_skillMenu->m21D0_bars[i] + j)->g0 = 0xFF;
            (gMenuContext->m438_skillMenu->m21D0_bars[i] + j)->b0 = 0;
            (gMenuContext->m438_skillMenu->m21D0_bars[i] + j)->r1 = 0;
            (gMenuContext->m438_skillMenu->m21D0_bars[i] + j)->g1 = 0xFF;
            (gMenuContext->m438_skillMenu->m21D0_bars[i] + j)->b1 = 0;
            (gMenuContext->m438_skillMenu->m21D0_bars[i] + j)->r2 = 0;
            (gMenuContext->m438_skillMenu->m21D0_bars[i] + j)->g2 = 0;
            (gMenuContext->m438_skillMenu->m21D0_bars[i] + j)->b2 = 0;
            (gMenuContext->m438_skillMenu->m21D0_bars[i] + j)->r3 = 0;
            (gMenuContext->m438_skillMenu->m21D0_bars[i] + j)->g3 = 0;
            (gMenuContext->m438_skillMenu->m21D0_bars[i] + j)->b3 = 0;
        }
    }
}

/* teardown: m33C->m4D, box 2, free m438 */
void freeSkillMenu(void) {
    freeCharacterSwitchArrows();
    gMenuContext->m33C_drawFlags->m4D_draw438 = 0;
    processLoadSaveMenuSub7Sub0(2);
    menuDraw();
    countField33C_C(2, gMenuContext->m33C_drawFlags->m4E_drawSkillPageLabels);
    loadSharedMenuData2(0x14);
    freeHeap(gMenuContext->m438_skillMenu);
}

u32 computeSkillLearnPercent(u8 slot, u8 index);
/* near miss (average % of party[ch].m90 against the m438 table): register choice */
/* near miss (average % of party[ch].m90 against the m438 table): register choice */
u32 computeSkillLearnPercent(u8 slot, u8 index) {
    u16 idx;
    u32 total;
    s32 i;
    u32 count;
    s32 ch;
    u16* cur;
    s32 a;
    s32 b;
    u32 pct;
    MenuStatTable* tbl;
    MenuStatTable* row;
    s32 off;
    s32 t;

    if ((t = index < 7, total = 0, t) || (gameState.m22B6_gameFlags & 0x4000)) {
        i = 0;
        count = 0;
        ch = gMenuContext->m33C_drawFlags->m30_partyMemberId[slot];
        cur = gameState.m26C_party[ch].m90_deathblowProgress;
        tbl = gMenuContext->m438_skillMenu->m2578_skillRequirements;
        idx = index;
    loop:
        off = i * 2;
        a = *cur;
        row = &tbl[ch];
        b = *(u16*)(off + (uptr)row->m0_requirements[idx]);
        if (a != 0) {
            if (b != 0) {
                if (b != 0xFFFF) {
                    t = a * 100;
                    pct = (u32)t / b;
                    if (pct >= 100) {
                        total += 100;
                    } else {
                        total += pct;
                    }
                }
                count++;
            }
        } else if (b != 0) {
            count++;
        }
        i++;
        cur++;
        do { if (i < 7) goto loop; } while (0);
        if (count == 0) {
            return 0;
        }
        return total / count;
    }
    return 0;
}

extern u8 skillComboGlyphs[]; /* 13 learned-entry glyph strings, 5 bytes each (flat: a 2D index gets strength-reduced) */

void setupSkillEntryProgress(u8 index, u8 state, u16* image, u32 pct);
/* skill entry: learned glyphs (state 1) or percentage and gradient bar (state 2) */
void setupSkillEntryProgress(u8 index, u8 state, u16* image, u32 pct) {
    u8 codes[6];
    u8 str[8];
    RECT rect;
    s32 j;
    u8 glyph;
    u32 tens;

    codes[5] = 0;
    codes[3] = 0;
    codes[1] = 0;
    switch (state) {
    case 1:
        for (j = 0; j < 5; j++) {
            glyph = skillComboGlyphs[index * 5 + j];
            if (glyph != 0xFF) {
                gMenuContext->m438_skillMenu->m257C_glyphCount[index] += setupStringInPolyFT4(
                    gMenuContext->m2DC_font, glyph,
                    gMenuContext->m438_skillMenu->mD80_glyphs[index][gMenuContext->m438_skillMenu->m257C_glyphCount[index]],
                    gMenuContext->m308_oddOrEven, j * 16 + 0xD4, index * 13 + 0x1F, 0x1000);
            }
        }
        gMenuContext->m438_skillMenu->m2589_glyphOddOrEven[index] = gMenuContext->m308_oddOrEven;
        break;
    case 2:
        if (pct / 100) {
            codes[0] = 0x11;
            pct -= 100;
        } else {
            codes[0] = 0xC3;
        }
        tens = pct / 10;
        if (tens != 0 || codes[0] != 0) {
            codes[2] = tens + 0x10;
            codes[4] = pct % 10 + 0x10;
        } else {
            codes[2] = 0xC3;
            codes[4] = 0xC3;
        }
        decodeCharacterCodes((u16*)codes, str, 3);
        gMenuContext->m438_skillMenu->m680_values[index].m7E_stringWidth = renderString(str, image, 0x24, 1);
        rect.x = (index & 1) * 24 + 0x180;
        rect.y = (index / 2) * 13 + 0x80;
        rect.w = 0x28;
        rect.h = 0xD;
        LoadImage(&rect, image);
        DrawSync(0);
        (gMenuContext->m438_skillMenu->m680_values[index].m0_polys + gMenuContext->m308_oddOrEven)->x0 = 0x10A;
        (gMenuContext->m438_skillMenu->m680_values[index].m0_polys + gMenuContext->m308_oddOrEven)->y0 = index * 13 + 0x1F;
        (gMenuContext->m438_skillMenu->m680_values[index].m0_polys + gMenuContext->m308_oddOrEven)->x1 = gMenuContext->m438_skillMenu->m680_values[index].m7E_stringWidth + 0x10A;
        (gMenuContext->m438_skillMenu->m680_values[index].m0_polys + gMenuContext->m308_oddOrEven)->y1 = index * 13 + 0x1F;
        (gMenuContext->m438_skillMenu->m680_values[index].m0_polys + gMenuContext->m308_oddOrEven)->x2 = 0x10A;
        (gMenuContext->m438_skillMenu->m680_values[index].m0_polys + gMenuContext->m308_oddOrEven)->y2 = index * 13 + 0x2C;
        (gMenuContext->m438_skillMenu->m680_values[index].m0_polys + gMenuContext->m308_oddOrEven)->x3 = gMenuContext->m438_skillMenu->m680_values[index].m7E_stringWidth + 0x10A;
        (gMenuContext->m438_skillMenu->m680_values[index].m0_polys + gMenuContext->m308_oddOrEven)->y3 = index * 13 + 0x2C;
        pct = pct * 5400 / 10000;
        initMenuContext_1DE0(&gMenuContext->m438_skillMenu->m680_values[index], index, 0x80, 0x82);
        (gMenuContext->m438_skillMenu->m21D0_bars[index] + gMenuContext->m308_oddOrEven)->x0 = 0xD4;
        (gMenuContext->m438_skillMenu->m21D0_bars[index] + gMenuContext->m308_oddOrEven)->y0 = index * 13 + 0x23;
        (gMenuContext->m438_skillMenu->m21D0_bars[index] + gMenuContext->m308_oddOrEven)->x1 = pct + 0xD4;
        (gMenuContext->m438_skillMenu->m21D0_bars[index] + gMenuContext->m308_oddOrEven)->y1 = index * 13 + 0x23;
        (gMenuContext->m438_skillMenu->m21D0_bars[index] + gMenuContext->m308_oddOrEven)->x2 = 0xD4;
        (gMenuContext->m438_skillMenu->m21D0_bars[index] + gMenuContext->m308_oddOrEven)->y2 = index * 13 + 0x2B;
        (gMenuContext->m438_skillMenu->m21D0_bars[index] + gMenuContext->m308_oddOrEven)->x3 = pct + 0xD4;
        (gMenuContext->m438_skillMenu->m21D0_bars[index] + gMenuContext->m308_oddOrEven)->y3 = index * 13 + 0x2B;
        gMenuContext->m438_skillMenu->m25B0_barOddOrEven[index] = gMenuContext->m308_oddOrEven;
        gMenuContext->m438_skillMenu->m25A3_showBar[index] = 1;
        break;
    }
}

/* skill page for character slot: 13 entries, learned flag or % progress */
void setupSkillMenuPage(u8 slot) {
    RECT rect;
    u8 codes[8];
    u8 str[8];
    u16* image;
    s32 i;
    u8 show;
    u8 known;
    u8 state;
    u32 pct;

    codes[1] = 0;
    image = ALLOC_TEXT_IMAGE(0x3F6);
    for (i = 0; i < 13; i++) {
        gMenuContext->m438_skillMenu->m25A3_showBar[i] = 0;
        if (menu2CheckBitmaskMsbFirst(gameState.m16C0_abilities[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]].m0_unlockedAbilitiesBitField[0], i)) {
            show = 1;
            state = 1;
            known = 1;
        } else {
            pct = computeSkillLearnPercent(slot, i);
            if (pct >= 50) {
                show = 1;
                state = 2;
            } else {
                show = 0;
                state = 0;
            }
            known = 0;
        }
        if (show) {
            gMenuContext->m438_skillMenu->m0_names[i].m7E_stringWidth =
                renderString(getDeathblowName(gMenuContext->m33C_drawFlags->m30_partyMemberId[slot], i), image, 0x24, 0);
            if (i < 7 && known) {
                codes[0] = gMenuContext->m330_itemData->m20_perCharacter[gMenuContext->m33C_drawFlags->m30_partyMemberId[slot]][i + 7].m17_glyph + 0x10;
            } else {
                codes[0] = 0xF;
            }
            decodeCharacterCodes((u16*)codes, str, 1);
            gMenuContext->m438_skillMenu->m680_values[i].m7E_stringWidth = renderString(str, image, 0x24, 1);
            rect.x = (i & 1) * 24 + 0x180;
            rect.y = (i / 2) * 13 + 0x80;
            rect.w = 0x28;
            rect.h = 0xD;
            LoadImage(&rect, image);
            DrawSync(0);
            initMenuContext_1DE0(&gMenuContext->m438_skillMenu->m0_names[i], i, 0x80, 0x81);
            initMenuContext_1DE0(&gMenuContext->m438_skillMenu->m680_values[i], i, 0x80, 0x82);
            (gMenuContext->m438_skillMenu->m0_names[i].m0_polys + gMenuContext->m308_oddOrEven)->x0 = 0x5C;
            (gMenuContext->m438_skillMenu->m0_names[i].m0_polys + gMenuContext->m308_oddOrEven)->y0 = i * 13 + 0x1F;
            (gMenuContext->m438_skillMenu->m0_names[i].m0_polys + gMenuContext->m308_oddOrEven)->x1 = gMenuContext->m438_skillMenu->m0_names[i].m7E_stringWidth + 0x5C;
            (gMenuContext->m438_skillMenu->m0_names[i].m0_polys + gMenuContext->m308_oddOrEven)->y1 = i * 13 + 0x1F;
            (gMenuContext->m438_skillMenu->m0_names[i].m0_polys + gMenuContext->m308_oddOrEven)->x2 = 0x5C;
            (gMenuContext->m438_skillMenu->m0_names[i].m0_polys + gMenuContext->m308_oddOrEven)->y2 = i * 13 + 0x2C;
            (gMenuContext->m438_skillMenu->m0_names[i].m0_polys + gMenuContext->m308_oddOrEven)->x3 = gMenuContext->m438_skillMenu->m0_names[i].m7E_stringWidth + 0x5C;
            (gMenuContext->m438_skillMenu->m0_names[i].m0_polys + gMenuContext->m308_oddOrEven)->y3 = i * 13 + 0x2C;
            (gMenuContext->m438_skillMenu->m680_values[i].m0_polys + gMenuContext->m308_oddOrEven)->x0 = 0xC8;
            (gMenuContext->m438_skillMenu->m680_values[i].m0_polys + gMenuContext->m308_oddOrEven)->y0 = i * 13 + 0x1F;
            (gMenuContext->m438_skillMenu->m680_values[i].m0_polys + gMenuContext->m308_oddOrEven)->x1 = gMenuContext->m438_skillMenu->m680_values[i].m7E_stringWidth + 0xC8;
            (gMenuContext->m438_skillMenu->m680_values[i].m0_polys + gMenuContext->m308_oddOrEven)->y1 = i * 13 + 0x1F;
            (gMenuContext->m438_skillMenu->m680_values[i].m0_polys + gMenuContext->m308_oddOrEven)->x2 = 0xC8;
            (gMenuContext->m438_skillMenu->m680_values[i].m0_polys + gMenuContext->m308_oddOrEven)->y2 = i * 13 + 0x2C;
            (gMenuContext->m438_skillMenu->m680_values[i].m0_polys + gMenuContext->m308_oddOrEven)->x3 = gMenuContext->m438_skillMenu->m680_values[i].m7E_stringWidth + 0xC8;
            (gMenuContext->m438_skillMenu->m680_values[i].m0_polys + gMenuContext->m308_oddOrEven)->y3 = i * 13 + 0x2C;
            gMenuContext->m438_skillMenu->m0_names[i].m7D_oddOrEven = gMenuContext->m308_oddOrEven;
            gMenuContext->m438_skillMenu->m2596_enabled[i] = 1;
        } else {
            gMenuContext->m438_skillMenu->m2596_enabled[i] = 0;
        }
        gMenuContext->m438_skillMenu->m257C_glyphCount[i] = 0;
        setupSkillEntryProgress(i, state, image, pct);
    }
    freeHeap(image);
    setupCharacterNameLabel(&gMenuContext->m438_skillMenu->mD00_characterName, slot, 0, 2);
    gMenuContext->m33C_drawFlags->m4D_draw438 = 1;
}

void setupSkillMenuPage(u8 slot);

/* submenu loop called by openStatusMenu: character stats page, skips ids 7/8 */
u8 runSkillMenu(u8 slot) {
    u8 keepGoing;
    u8 prev;

    keepGoing = 1;
    prev = 0xFF;
    if (gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] == 7 || gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] == 8) {
        playMenuSoundEffect(4);
        return 1;
    }
    initSkillMenu();
    do {
        menuDraw();
        if (slot != prev) {
            setupSkillMenuPage(slot);
            prev = slot;
        }
        switch (gMenuContext->m325_menuButton) {
        case 5:
            keepGoing = 0;
            break;
        case 9:
            do {
                slot = findNextPartySlot(slot, 0, 0);
            } while (gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] == 7 || gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] == 8);
            break;
        case 10:
            do {
                slot = findNextPartySlot(slot, 1, 0);
            } while (gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] == 7 || gMenuContext->m33C_drawFlags->m30_partyMemberId[slot] == 8);
            break;
        }
    } while (keepGoing);
    freeSkillMenu();
    return 1;
}

/* Noah initStatusMenu-like: alloc m358/m35C/m360, first available character */
u8 initGearStatusMenu(void);
u8 initGearStatusMenu(void) {
    s32 i;

    gMenuContext->m358_selectedInfoCard = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_358), 0x2af0), 0);
    bzero(gMenuContext->m358_selectedInfoCard, sizeof(MenuContext_358));
    gMenuContext->m35C_selectedStats = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_35C), 0x32f4), 0);
    bzero(gMenuContext->m35C_selectedStats, sizeof(MenuContext_35C));
    gMenuContext->m360_equipment = allocateHeap(PC_HEAP_SIZED((s32)sizeof(struct MenuContext_360), (s32)0x2ac), 0);
    bzero(gMenuContext->m360_equipment, (s32)sizeof(struct MenuContext_360));
    loadSharedMenuData2(3);
    i = 0;
    while (1) {
        if (gMenuContext->m33C_drawFlags->m60_characterAvailable[i] != 0) {
            break;
        }
        i++;
    }
    initStatusMenuSub1(0, 1);
    return i;
}

/* label setup m18E0 */
void setupGearMenuLabels(u8 index) {
    j_setupMenuContext4E0(6, gMenuContext->m18E0_gearMenuLabels, &gearMenuLabelConfig[index], gMenuContext->m33C_drawFlags->m54_drawGearMenuLabels);
}

/* free m358/m35C/m360 */
void freeGearStatusMenu(void) {
    freeHeap(gMenuContext->m358_selectedInfoCard);
    freeHeap(gMenuContext->m35C_selectedStats);
    freeHeap(gMenuContext->m360_equipment);
    loadSharedMenuData2(0x13);
}
