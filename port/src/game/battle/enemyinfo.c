/*
 * Optional enemy information window (F1 > Gameplay > Enemy info, off by
 * default: pcGameplayEnemyInfo, port/src/system/gameplay.c). drawBattleMode1
 * calls this first each frame, under PLATFORM_PC; with the option off it
 * returns at once and the battle is the PS1's.
 *
 * Select toggles the window (a press, as an edge of what the pad holds:
 * pcPadHeldRemapped, src/main/pad.c, as the retry prompt reads it; the
 * battle's own reader answers Select with a button code nothing consumes).
 * It stands in the lower right, above the bottom message window, and in a
 * wide picture at the wide right edge (pcWidePrim, pc_wide.h). The frame is
 * the battle's own dialogue window, built by the battle's own window
 * routines (setupBattleDialogWindow, drawBattleDialogWindow in b_8008B478.c
 * and b_800769E8.c) into a window of ours: they address a window by its
 * slot, so slot 6 points at ours while they run and is put back after;
 * nothing of the game's windows changes.
 *
 * Who: every enemy on the field the game itself would name (its slot
 * filled, not hidden, monsterFlags 0x400 clear: the test around the turn's
 * name label, battleTickMain), one row each in slot order; while one enemy
 * carries the player's target marker (the spinning cursor:
 * updateCharacterBlinkingTask's mask, b_800B7C28.c, in the command ring,
 * the attack menu and ability, item and Gear target selection) only that
 * one. Each targetable part of a multi-part enemy is its own entity with
 * its own HP.
 *
 * What, from live battle state: the name (currentBattleSpecificStrings
 * through battleMonsterMapping, the string the turn's name label shows), in
 * the dialogue font (renderString into a RAM image here, never VRAM, drawn
 * as one opaque TILE per run of same pixel value in the font's own colours:
 * fontPalettes, the CLUT of an even text line); current/max HP (sBattleEntity
 * m4C/m4E; a Gear's mA4_gear m60/m64) as the ether menu's EP readout draws
 * its numbers (drawCircleMenuChi_updateSub0Sub3, b_8008B478.c): the
 * dialogue font's digits, from the ten 8-pixel digit images the battle
 * loader renders for the battle (itemLabelsIds, renderItemTargetsLabels:
 * the menus upload them to the text plane; here they are drawn from RAM as
 * the name is), 8 pixels apart, the slash the battle font's 0x71 4 pixels
 * on and 11 down, the readout's own spacing; then each
 * active status the game itself can name: setStatusEffectMessage
 * (b_800941A4.c) names a character's statuses by type 0 = m7C[0], 2 = m7C[2],
 * 5 = m84_statusFlags4[0], 7 = m84_statusFlags4[2], 9 = m84[4] (the words
 * startBattleAttackAnimationVar8_2 sets with those types),
 * setGearStatusEffectMessage (b_8009E268.c) a Gear's by type 0 = gear m7C,
 * 1 = m80_statusFlags2, 3 = m84_statusFlags4 (applyGearStatusEffect). Both
 * are called per set bit with pBattleEntities->m5FC7 saved and restored; a
 * bit they leave unnamed is not shown (nothing is invented). The names are
 * the game's own battle messages (battleAttackNameStrings).
 *
 * The "HP once beaten" option (pcGameplayEnemyInfoKnown): a row's HP is
 * left out until the extras' bestiary record (pcBestiaryKnown,
 * include/pc_bestiary.h: bestiary.txt beside settings.ini, kept across
 * games) says the party has defeated that enemy type before, on this disc
 * and region, with the key the battle reports (set currentBattleConfig[0],
 * type battleMonsterMapping - 1, pcBestiaryBattle). A build without the
 * extras has no record: every HP stays out while the option is on.
 */
#include "common.h"
#include "psyq.h"
#include "main/dialog.h"
#include "main/battle.h"
#include "battle/battle.h"
#include "pc_wide.h"
#include "pc_bestiary.h"

int pcGameplayEnemyInfo(void); /* port/src/system/gameplay.c */
int pcGameplayEnemyInfoKnown(void);
extern u8 currentBattleConfig[0x20]; /* [0]: the enemy set */
s32 pcPadHeldRemapped(void);   /* src/main/pad.c */
extern BattleBss_800CCCE8* pBattleEntities;
extern u8* battleAttackNameStrings;
extern u8* currentBattleSpecificStrings; /* this battle's strings: the enemies' names */
extern u8 battleMonsterMapping[11];      /* an entity's name string in them */
extern u16 updateTargetSelectionMarkerSub0_var0; /* the marked targets, one bit per entity */
extern void* battleFont;
extern s32 g_playTimeInVsync; /* play time in vsyncs: a gap means another battle, or the window was hidden */
void setStatusEffectMessage(u8 type, u16 flag);
void setGearStatusEffectMessage(u8 type, u16 mask);
s32 battleSetupStringInPolyFT4Large(s32 character, POLY_FT4 (*polys)[2], s16 x, s16 y);
void battleRenderPolyArray(POLY_FT4* p, s32 count, s32 oddOrEven);
void setupBattleDialogWindow(u8 idx);
void drawBattleDialogWindow(u8 idx, u16 x, u16 y, u16 w, u16 h);

#define WIN_SLOT 6        /* the window slot lent to ours while the game's routines build it */
#define WIN_RIGHT 0x134   /* the frame's inner right edge (its border reaches 8 pixels further) */
#define WIN_BOTTOM 0xBC   /* inner bottom edge: above the bottom message window (y 0xC4) */
#define WIN_TOP_MIN 0x48  /* never above the turn's name label window */
#define WIN_PAD_X 8
#define WIN_PAD_Y 6
#define WIN_GAP 8         /* between the longest name and the HP */
#define ROW_H 14          /* a name line (the dialogue font's 13 rows) */
#define STATUS_H 12
#define STATUS_INDENT 8
#define ENEMIES 8         /* entities 3..10 */
#define FIRST_ENEMY 3
#define STATUSES 4        /* status lines per enemy at most */
#define NAME_UNITS 0x23   /* renderString width (4-pixel units): 140 pixels */
#define STRIDE ((NAME_UNITS | 1) + 3)
#define ROWS 13
#define TILES 4000
#define HP_DIGITS 10      /* five of each */
#define HP_CELL 8         /* a digit cell: the EP readout's pitch */
#define HP_SLASH 8        /* the slash's room between the numbers (0x114..0x11C there) */
#define HP_SLASH_DX 4     /* the slash glyph's anchor in it */
#define HP_SLASH_DY 11    /* and below the digits' top (0xC6 -> 0xD1 there) */
#define HP_STRIDE ((2 | 1) + 3) /* a digit image: renderString at 2 units, (width | 1) + 3 halfwords a row */
#define NO_NAME 0xFF

typedef struct {
    u8 nameId;              /* the string rendered in nameImage, NO_NAME for none */
    u8 statusCount;         /* statuses rendered in statusImage */
    u8 statusIds[STATUSES];
    s32 nameWidth;
    s32 statusWidth[STATUSES];
    u16 nameImage[STRIDE * ROWS];
    u16 statusImage[STATUSES][STRIDE * ROWS];
} EnemyText;

typedef struct {
    u8 slot;
    u8 statusCount; /* shown (dropped when the window would not fit) */
    u8 hpShown;     /* the HP readout (not before a win, under the option) */
    u8 digits[HP_DIGITS]; /* hp's then max's */
    s32 hpCount;
    s32 maxCount;
} Row;

static EnemyText texts[ENEMIES];
static POLY_FT4 slashPolys[ENEMIES][2];
static TILE tiles[2][TILES];
static sBattleDialogWindow frame;
static DR_MODE restoreMode[2];
static s32 shown;
static s32 previousHeld;
static s32 lastDrawn = -1000; /* g_playTimeInVsync at the last frame drawn */

/* The message for one status bit of one word, by the game's own mapping. */
static u8 statusName(s32 gear, u8 type, u16 bit) {
    u8 saved = pBattleEntities->m5FC7_battleTickMain_var1;
    u8 id;

    pBattleEntities->m5FC7_battleTickMain_var1 = 0;
    if (gear) {
        setGearStatusEffectMessage(type, bit);
    } else {
        setStatusEffectMessage(type, bit);
    }
    id = pBattleEntities->m5FC7_battleTickMain_var1;
    pBattleEntities->m5FC7_battleTickMain_var1 = saved;
    return id;
}

/* The named statuses of entity e, in the game's word and bit order. */
static s32 collectStatuses(sBattleEntity* e, u8* ids) {
    static const u8 baseTypes[5] = {0, 2, 5, 7, 9};
    static const u8 gearTypes[3] = {0, 1, 3};
    u16 words[5];
    const u8* types;
    s32 count = 0;
    s32 words_n;
    s32 i;
    s32 j;
    s32 k;
    s32 gear = (e->m15A_flags & 0x80) != 0;

    if (gear) {
        words[0] = e->mA4_gear.m7C_statusFlags;
        words[1] = e->mA4_gear.m80_statusFlags2;
        words[2] = e->mA4_gear.m84_statusFlags4;
        types = gearTypes;
        words_n = 3;
    } else {
        words[0] = e->m0_base.m7C_statusFlags[0];
        words[1] = e->m0_base.m7C_statusFlags[2];
        words[2] = e->m0_base.m84_statusFlags2[0];
        words[3] = e->m0_base.m84_statusFlags2[2];
        words[4] = e->m0_base.m84_statusFlags2[4];
        types = baseTypes;
        words_n = 5;
    }
    for (i = 0; i < words_n; i++) {
        for (j = 15; j >= 0; j--) {
            u8 id;
            if (!(words[i] & (1 << j))) {
                continue;
            }
            id = statusName(gear, types[i], 1 << j);
            if (id == 0) {
                continue;
            }
            for (k = 0; k < count && ids[k] != id; k++) {
            }
            if (k == count && count < STATUSES) {
                ids[count++] = id;
            }
        }
    }
    return count;
}

/* One line of the dialogue font into image (renderString is the game's
 * one-line printer, renderStringWindow: cheap but not free, so callers
 * render only what changed); its width in pixels. */
static s32 renderLine(u8* text, u16* image) {
    s32 k;

    for (k = 0; k < STRIDE * ROWS; k++) {
        image[k] = 0;
    }
    return renderString(text, image, NAME_UNITS, 0);
}

static u8 colour(u16 c, s32 shift) {
    return ((c >> shift) & 0x1F) << 3;
}

/* A rendered line (stride halfwords a row) as TILE runs at x, y; the tiles
 * used so far, updated. */
static s32 drawImage(OTag* ot, s32 used, const u16* img, s32 stride, s32 width, s32 x, s32 y);
static s32 drawText(OTag* ot, s32 used, const u16* img, s32 width, s32 x, s32 y) {
    return drawImage(ot, used, img, STRIDE, width, x, y);
}

static s32 drawImage(OTag* ot, s32 used, const u16* img, s32 stride, s32 width, s32 x, s32 y) {
    TILE* t = tiles[D_800CCB04.m30_battleOddOrEven];
    s32 row;
    s32 px;

    for (row = 0; row < ROWS; row++) {
        px = 0;
        while (px < width + 4 && px < stride * 4) {
            s32 v = (img[row * stride + (px >> 2)] >> ((px & 3) * 4)) & 3;
            s32 start = px;
            u16 c;
            if (v == 0) {
                px++;
                continue;
            }
            while (px < stride * 4 && ((img[row * stride + (px >> 2)] >> ((px & 3) * 4)) & 3) == v) {
                px++;
            }
            if (used >= TILES) {
                return used;
            }
            c = fontPalettes[v]; /* the line's CLUT: 1 and 3 body, 2 outline */
            setlen(&t[used], 3);
            setcode(&t[used], 0x60); /* TILE, opaque */
            setRGB0(&t[used], colour(c, 0), colour(c, 5), colour(c, 10));
            t[used].x0 = x + start;
            t[used].y0 = y + row;
            t[used].w = px - start;
            t[used].h = 1;
            pcTextureTextPrim(ot, &t[used]);
            pcWidePrim(&t[used], pcWideView, 0);
            used++;
        }
    }
    return used;
}

/* The HP readout's digits, hp's then max's, most significant first. */
static void hpDigits(u32 hp, u32 maxHp, Row* r) {
    u32 values[2];
    s32 counts[2];
    s32 n = 0;
    s32 j;

    values[0] = hp;
    values[1] = maxHp;
    for (j = 0; j < 2; j++) {
        u32 v = values[j];
        s32 i = 0;
        u8 d[5];
        do {
            d[i++] = (u8)(v % 10);
            v /= 10;
        } while (v != 0 && i < 5);
        counts[j] = i;
        while (i > 0) {
            r->digits[n++] = d[--i];
        }
    }
    r->hpCount = counts[0];
    r->maxCount = counts[1];
}

static s32 hpWidthOf(const Row* r) {
    return r->hpShown ? (r->hpCount + r->maxCount) * HP_CELL + HP_SLASH : 0;
}

/* The readout at x, y (the digits' top), as the EP readout is drawn; the
 * tiles used so far, updated. */
static s32 drawHp(OTag* ot, s32 odd, s32 used, const Row* r, s32 row, s32 x, s32 y) {
    s32 k;
    s32 made;

    for (k = 0; k < r->hpCount + r->maxCount; k++) {
        const u16* img = itemLabelsIds[r->digits[k]].m0_image;
        if (img != NULL) {
            used = drawImage(ot, used, img, HP_STRIDE, HP_CELL, x, y);
        }
        x += HP_CELL;
        if (k == r->hpCount - 1) {
            made = battleSetupStringInPolyFT4Large(0x71, &slashPolys[row], (s16)(x + HP_SLASH_DX), (s16)(y + HP_SLASH_DY));
            for (made--; made >= 0; made--) {
                pcWidePrim(&slashPolys[row][made * 2 + odd], pcWideView, 0);
            }
            battleRenderPolyArray(slashPolys[row], 1, odd);
            x += HP_SLASH;
        }
    }
    return used;
}

/* The game's own window frame around the inner rectangle x, y, w, h, built
 * into ours by the battle's routines through the lent slot. */
static void buildFrame(s32 x, s32 y, s32 w, s32 h) {
    sBattleDialogWindow* saved = battleDialogWindows[WIN_SLOT];
    u8 savedReady = battleVar1->mB0_isDialogWindowInitialized[WIN_SLOT];

    battleDialogWindows[WIN_SLOT] = &frame;
    setupBattleDialogWindow(WIN_SLOT);
    drawBattleDialogWindow(WIN_SLOT, x, y, w, h);
    battleDialogWindows[WIN_SLOT] = saved;
    battleVar1->mB0_isDialogWindowInitialized[WIN_SLOT] = savedReady;
}

/* The frame's packets, as battleDrawAPBar links the game's windows (each
 * AddPrim goes in front: the draw mode is drawn first, then the background,
 * the edges, the corners). */
static void linkFrame(OTag* ot) {
    s32 odd = frame.m5A4_oddOrEven;
    s32 i;

    for (i = 0; i < frame.m5A0_cornerCount; i++) {
        pcTextureClass(&frame.m0_corners[0][i * 2 + odd], PC_TEX_HUD);
        pcWidePrim(&frame.m0_corners[0][i * 2 + odd], pcWideView, 0);
    }
    battleRenderPolyArray(frame.m0_corners[0], frame.m5A0_cornerCount, odd);
    for (i = 0; i < 2; i++) {
        AddPrim(ot, &frame.m280_leftEdge[i][odd]);
        AddPrim(ot, &frame.m320_rightEdge[i][odd]);
        AddPrim(ot, &frame.m140_topEdge[i][odd]);
        AddPrim(ot, &frame.m1E0_bottomEdge[i][odd]);
        pcWidePrim(&frame.m280_leftEdge[i][odd], pcWideView, 0);
        pcWidePrim(&frame.m320_rightEdge[i][odd], pcWideView, 0);
        pcWidePrim(&frame.m140_topEdge[i][odd], pcWideView, 0);
        pcWidePrim(&frame.m1E0_bottomEdge[i][odd], pcWideView, 0);
    }
    AddPrim(ot, &frame.m3C0_polyG4[odd]);
    pcWidePrim(&frame.m3C0_polyG4[odd], pcWideView, 0);
    AddPrim(ot, &frame.m408_drawMode[odd]);
}

void pcBattleEnemyInfo(void) {
    Row rows[ENEMIES];
    OTag* ot;
    s32 odd;
    s32 held;
    s32 pressed;
    s32 count = 0;
    s32 nameWidth = 0;
    s32 hpWidth = 0;
    s32 statusLines = 0;
    s32 innerW;
    s32 innerH;
    s32 x;
    s32 y;
    s32 used;
    s32 only = -1; /* the one marked target, else every enemy */
    s32 knownOnly;
    u16 marked;
    s32 i;
    s32 j;

    if (!pcGameplayEnemyInfo() || battleVar0 == NULL || battleVar1 == NULL || battleFont == NULL ||
        battleAttackNameStrings == NULL || currentBattleSpecificStrings == NULL) {
        return;
    }
    held = pcPadHeldRemapped();
    pressed = held & ~previousHeld;
    previousHeld = held;
    if (pressed & 0x100) {
        shown ^= 1;
    }
    if (!shown) {
        return;
    }
    odd = D_800CCB04.m30_battleOddOrEven;
    ot = D_800CCB04.m0_pCurrentBattleOT + 1;
    /* Rendered text is kept from frame to frame only while the window is
     * drawn every frame: after a gap (hidden, another battle: its names are
     * other strings at the same index) everything is rendered again. */
    if (g_playTimeInVsync - lastDrawn > 4 || g_playTimeInVsync < lastDrawn) {
        for (i = 0; i < ENEMIES; i++) {
            texts[i].nameId = NO_NAME;
            texts[i].statusCount = 0;
        }
    }
    lastDrawn = g_playTimeInVsync;

    knownOnly = pcGameplayEnemyInfoKnown();
    marked = updateTargetSelectionMarkerSub0_var0;
    if ((marked & 7) == 0 && (marked & 0x7F8) != 0 && (marked & (marked - 1)) == 0) {
        for (only = FIRST_ENEMY; !(marked & (1 << only)); only++) {
        }
    }

    /* The rows: who, with what rendered. */
    for (i = 0; i < ENEMIES; i++) {
        s32 slot = FIRST_ENEMY + i;
        sBattleEntity* e = &battleEntities.m0_entities[slot];
        EnemyText* t = &texts[i];
        Row* r;
        u8 ids[STATUSES];
        s32 n;

        if (!isBattleSlotFilled.m0_isBattleSlotFilled[slot] || (battleVisualEntities[slot].m3_hidden & 0x80) ||
            (e->m0_base.m34_monsterFlags & 0x400) || (only >= 0 && slot != only)) {
            t->nameId = NO_NAME;
            t->statusCount = 0;
            continue;
        }
        r = &rows[count++];
        r->slot = (u8)slot;
        if (t->nameId != battleMonsterMapping[slot]) {
            t->nameId = battleMonsterMapping[slot];
            t->nameWidth = renderLine(getDialogParamPointer(currentBattleSpecificStrings, t->nameId), t->nameImage);
        }
        n = collectStatuses(e, ids);
        for (j = 0; j < n; j++) {
            if (j >= t->statusCount || t->statusIds[j] != ids[j]) {
                t->statusIds[j] = ids[j];
                t->statusWidth[j] = renderLine(getDialogParamPointer(battleAttackNameStrings, ids[j]), t->statusImage[j]);
            }
        }
        t->statusCount = (u8)n;
        r->statusCount = (u8)n;
        r->hpShown = !knownOnly || (pcBestiaryKnown != NULL && battleMonsterMapping[slot] != 0 &&
                                    pcBestiaryKnown(currentBattleConfig[0], battleMonsterMapping[slot] - 1));
        if (e->m15A_flags & 0x80) {
            hpDigits((u32)(e->mA4_gear.m60_hp < 0 ? 0 : e->mA4_gear.m60_hp), (u32)e->mA4_gear.m64_maxHp, r);
        } else {
            hpDigits(e->m0_base.m4C_HP, e->m0_base.m4E_MaxHP, r);
        }
        if (t->nameWidth > nameWidth) nameWidth = t->nameWidth;
        if (hpWidthOf(r) > hpWidth) hpWidth = hpWidthOf(r);
        if (!r->hpShown) r->hpCount = r->maxCount = 0;
        statusLines += n;
    }
    if (count == 0) {
        return;
    }
    /* The window's size: statuses, then the last rows, go when it would not fit. */
    innerH = 2 * WIN_PAD_Y + count * ROW_H + statusLines * STATUS_H;
    for (i = count - 1; i >= 0 && WIN_BOTTOM - innerH < WIN_TOP_MIN; i--) {
        innerH -= rows[i].statusCount * STATUS_H;
        rows[i].statusCount = 0;
    }
    while (count > 1 && WIN_BOTTOM - innerH < WIN_TOP_MIN) {
        count--;
        innerH -= ROW_H;
    }
    innerW = 2 * WIN_PAD_X + nameWidth + (hpWidth ? WIN_GAP + hpWidth : 0);
    if (innerW < 0x40) innerW = 0x40;
    if (innerH < 0x18) innerH = 0x18;
    x = WIN_RIGHT - innerW;
    y = WIN_BOTTOM - innerH;

    /* Linked in front of everything else: drawn last, on top. The draw mode
     * the HUD's windows end with comes first, so what is drawn after the
     * window sees the mode it expects. */
    restoreMode[odd] = battleVar0->m8908_drMode[odd];
    AddPrim(ot, &restoreMode[odd]);
    used = 0;
    for (i = 0, j = y + WIN_PAD_Y; i < count; i++) {
        Row* r = &rows[i];
        EnemyText* t = &texts[r->slot - FIRST_ENEMY];
        s32 s;

        used = drawText(ot, used, t->nameImage, t->nameWidth, x + WIN_PAD_X, j);
        if (r->hpShown) {
            used = drawHp(ot, odd, used, r, i, x + innerW - WIN_PAD_X - hpWidthOf(r), j);
        }
        j += ROW_H;
        for (s = 0; s < r->statusCount; s++, j += STATUS_H) {
            used = drawText(ot, used, t->statusImage[s], t->statusWidth[s], x + WIN_PAD_X + STATUS_INDENT, j);
        }
    }
    buildFrame(x, y, innerW, innerH);
    linkFrame(ot);
}
