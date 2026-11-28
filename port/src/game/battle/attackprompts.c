/* The two attack help pages end with four single-quad button icons. Their
 * captions, including the separately drawn Gear fuel digits, carry the same
 * command flags as the icon they describe in both regions' layout tables. */
#include "common.h"
#include "psyq.h"
#include "battle/battle.h"
#include "pc_pad.h"

extern u16* battleMenuGraphicConfigs2[19];
extern void* battleFont;

void pcBattleArrangeAttackPrompts(void) {
    const sBattleMenuGlyph* row;
    const sBattleMenuGlyph* icons;
    const u16* font = battleFont;
    const u8* commands;
    POLY_FT4* polys;
    POLY_FT4* icon[4];
    int at[4] = {0, 0, 0, 0}; /* right, bottom, left, top */
    int dest[4], dx[4], dy[4];
    int menu = battleVar2->m2E2_previousActiveBattleMenu;
    int count = battleVar1->mD0_battleCommandLength[0];
    u8 owner[sizeof(battleVar0->m641C_commandGlyphs[0]) / (2 * sizeof(POLY_FT4))];
    int i, used = 0, moved = 0;

    if ((menu != 5 && menu != 25) || count < 4 || count > (int)sizeof(owner)) return;
    polys = &battleVar0->m641C_commandGlyphs[0][battleVar1->mA3_641COddOrEven];
    for (i = 0; i < 4; i++) {
        icon[i] = &polys[(count - 4 + i) * 2];
        if (icon[i]->x0 > icon[at[0]]->x0) at[0] = i;
        if (icon[i]->y0 > icon[at[1]]->y0) at[1] = i;
        if (icon[i]->x0 < icon[at[2]]->x0) at[2] = i;
        if (icon[i]->y0 < icon[at[3]]->y0) at[3] = i;
    }
    pcPadAttackPositions(dest);
    for (i = 0; i < 4; i++) {
        const POLY_FT4* target = icon[at[dest[i]]];
        dx[i] = target->x0 - icon[i]->x0;
        dy[i] = target->y0 - icon[i]->y0;
        moved |= dx[i] != 0 || dy[i] != 0;
    }
    if (!moved) return;

    row = (const sBattleMenuGlyph*)battleMenuGraphicConfigs2[menu == 5 ? 4 : 18];
    icons = row;
    while (icons->m0_id != 0xFFFF && icons->m0_id != 0x0B) icons++;
    if (icons->m0_id == 0xFFFF) return;
    commands = (const u8*)&battleVar2->m0_entityMenus[battleVar2->m2D3_currentEntityTurn];
    for (; row->m0_id != 0xFFFF; row++) {
        int id = row->m0_id;
        int pieces;
        if (id >= 0x4000) {
            id = commands[id & 0xFF];
            if (id == 0xFF) continue;
        }
        /* The battle font stores a halfword offset per glyph after its count. */
        if ((u32)id >= *(const u32*)font) return;
        pieces = *(const s16*)((const u8*)font + font[2 + id]);
        if (pieces < 0 || pieces > count - used) return;
        for (i = 0; i < 4 && icons[i].m6_flags != row->m6_flags; i++) {}
        if (i == 4) return;
        while (pieces-- > 0) owner[used++] = i;
    }
    if (used != count) return;
    for (i = 0; i < count; i++) {
        POLY_FT4* p = &polys[i * 2];
        int x = dx[owner[i]], y = dy[owner[i]];
        p->x0 += x; p->x1 += x; p->x2 += x; p->x3 += x;
        p->y0 += y; p->y1 += y; p->y2 += y; p->y3 += y;
    }
}
