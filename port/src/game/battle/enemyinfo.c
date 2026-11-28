/*
 * Optional enemy information window (F1 > Gameplay > Enemy info, off by
 * default: pcGameplayEnemyInfo, port/src/system/gameplay.c). drawBattleMode1
 * calls this first each frame, under PLATFORM_PC; with the option off it
 * returns at once and the battle is the PS1's.
 *
 * Select toggles the window when it is released on its own (edges of what
 * the pad holds: pcPadHeldRemapped, src/main/pad.c, as the retry prompt
 * reads it; the battle's own reader answers Select with a button code
 * nothing consumes). Select with a shoulder button is a chord of the port's
 * (battle speed's Select+R2, rewind's Select+L2) and toggles nothing
 * (pcGameplaySelectTap, port/src/system/gameplay.c).
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
 *
 * A versus battle (include/pc_arena.h; the user's rule, xg-htn.35.143): the
 * match's own setting decides, not the Enemy info one: with "Opponent's HP
 * and EP" (arena_opponent_hp, off by default; online the host's for both
 * players) Select toggles the window and it lists the opponent's members
 * with their HP and, under each, their EP (m50/m52 as the ether menu's
 * readout has it, "EP" in the dialogue font; not for a Gear or a member
 * without EP, as many enemies are), every HP
 * shown (no bestiary rule); without it there is no window at all, so no
 * opponent HP or EP shows. The opponent is the other side from the one this
 * screen shows (mirror.c pcVersusInfoFirst): in a mirror match player 2's
 * copy ("Fei (P2)") on player 1's screen, the party ("Fei", the mirror's
 * strings) on player 2's; in a versus battle against a formation the
 * enemies. Select is read from the pad of the player whose screen this is
 * (both pads on one screen).
 */
#include "common.h"
#include "psyq.h"
#include "main/dialog.h"
#include "main/battle.h"
#include "main/sprite.h"
#include "battle/battle.h"
#include "pc_wide.h"
#include "pc_bestiary.h"
#include "pc_arena.h"
#include "versus.h"

int pcGameplayEnemyInfo(void); /* port/src/system/gameplay.c */
int pcGameplayEnemyInfoKnown(void);
int pcGameplaySelectTap(int previous, int held, int* chord);
extern u8 currentBattleConfig[0x20]; /* [0]: the enemy set */
s32 pcPadHeldRemapped(void);   /* src/main/pad.c */
s32 pcPadHeldRemappedPort(s32 port);
extern BattleBss_800CCCE8* pBattleEntities;
extern u8* battleAttackNameStrings;
extern u8* currentBattleSpecificStrings; /* this battle's strings: the enemies' names */
extern u8 battleMonsterMapping[11];      /* an entity's name string in them */
extern u8 isEntityReadyForBattle[11];
extern u16 partyMemberSpritesOffset[9];
extern u16 updateTargetSelectionMarkerSub0_var0; /* the marked targets, one bit per entity */
extern void* battleFont;
extern s32 g_playTimeInVsync; /* play time in vsyncs: a gap means another battle, or the window was hidden */
void setStatusEffectMessage(u8 type, u16 flag);
void setGearStatusEffectMessage(u8 type, u16 mask);
s32 battleSetupStringInPolyFT4Large(s32 character, POLY_FT4 (*polys)[2], s16 x, s16 y);
s32 battleSetupStringInPolyFT4Small(s32 character, POLY_FT4 (*polys)[2], s16 x, s16 y);
void battleSetupTextPoly(POLY_FT4* p);
void tintFT4(POLY_FT4* poly, s32 from, s32 to, u8 color);
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
#define BATTLE_SLOTS (FIRST_ENEMY + ENEMIES)
#define PARTY 3           /* a versus battle on player 2's screen: entities 0..2 */
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

/* A readout: a value and its maximum, as digits. */
typedef struct {
    u8 digits[HP_DIGITS]; /* the value's then the maximum's */
    s32 hpCount;          /* the value's digits */
    s32 maxCount;
} Readout;

typedef struct {
    u8 slot;
    u8 statusCount; /* shown (dropped when the window would not fit) */
    u8 hpShown;     /* the HP readout (not before a win, under the option) */
    u8 epShown;     /* the EP line (a versus battle's opponent) */
    Readout hp;
    Readout ep;
} Row;

static EnemyText texts[BATTLE_SLOTS];
static POLY_FT4 slashPolys[ENEMIES][2];
static POLY_FT4 epSlashPolys[ENEMIES][2];
static u16 epImage[STRIDE * ROWS]; /* "EP" */
static s32 epWidth = -1;          /* -1: to render */
static TILE tiles[2][TILES];
static sBattleDialogWindow frame;
static DR_MODE restoreMode[2];
static s32 shown;
static s32 previousHeld;
static int selectChord; /* Select is part of a chord until it is released (pcGameplaySelectTap) */
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

/* A readout's digits, the value's then the maximum's, most significant first. */
static void hpDigits(u32 hp, u32 maxHp, Readout* r) {
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

static s32 readoutWidth(const Readout* r) {
    return (r->hpCount + r->maxCount) * HP_CELL + HP_SLASH;
}

static s32 hpWidthOf(const Row* r) {
    return r->hpShown ? readoutWidth(&r->hp) : 0;
}

/* The readout at x, y (the digits' top), as the EP readout is drawn, its
 * slash in slash; the tiles used so far, updated. */
static s32 drawHp(OTag* ot, s32 odd, s32 used, const Readout* r, POLY_FT4 (*slash)[2], s32 x, s32 y) {
    s32 k;
    s32 made;

    for (k = 0; k < r->hpCount + r->maxCount; k++) {
        const u16* img = itemLabelsIds[r->digits[k]].m0_image;
        if (img != NULL) {
            used = drawImage(ot, used, img, HP_STRIDE, HP_CELL, x, y);
        }
        x += HP_CELL;
        if (k == r->hpCount - 1) {
            made = battleSetupStringInPolyFT4Large(0x71, slash, (s16)(x + HP_SLASH_DX), (s16)(y + HP_SLASH_DY));
            for (made--; made >= 0; made--) {
                pcWidePrim(&slash[0][made * 2 + odd], pcWideView, 0);
            }
            battleRenderPolyArray(slash[0], 1, odd);
            x += HP_SLASH;
        }
    }
    return used;
}

static void entityHp(const sBattleEntity* e, Readout* hp) {
    if (e->m15A_flags & 0x80) {
        hpDigits(e->mA4_gear.m60_hp < 0 ? 0 : e->mA4_gear.m60_hp, e->mA4_gear.m64_maxHp, hp);
    } else {
        hpDigits(e->m0_base.m4C_HP, e->m0_base.m4E_MaxHP, hp);
    }
}

/* Enemy rosters occupy slots 3..10. Use the party HUD without its AP row. */
#define ROSTER_TIME_W 56
#define ROSTER_PORTRAIT_SIZE PC_ARENA_BATTLE_PORTRAIT_SIZE
#define ROSTER_PORTRAIT_RIM PC_ARENA_BATTLE_PORTRAIT_RIM
#define ROSTER_PORTRAIT_PARTS 63 /* the sprite actor's six-bit part count */
#define ROSTER_PORTRAIT_PIXELS (ROSTER_PORTRAIT_SIZE * ROSTER_PORTRAIT_SIZE)
#define ROSTER_PORTRAITS_VISIBLE 5

typedef struct {
    OTag* ot;
    s32 odd;
    s32 wideX;
} RosterHud;

static TILE rosterPortraits[ENEMIES][2][2];
typedef struct {
    u16 palette[256];
    u8 pixels[ROSTER_PORTRAIT_PIXELS];
    u8 ready;
} RosterSpritePortrait;

static RosterSpritePortrait rosterSpritePortrait[ENEMIES];
static POLY_FT4 rosterPortraitDraw[ENEMIES][2];
static s32 rosterPortraitSlot[ROSTER_PORTRAITS_VISIBLE];
static POLY_FT4 rosterHp[ENEMIES][40][2];
static POLY_GT4 rosterTimeBar[ENEMIES][2];
static POLY_FT4 rosterTimer[ENEMIES][12][2];
static POLY_FT4 rosterAp[ROSTER_PORTRAITS_VISIBLE][16][2];
/* Mixed battles use both sprite strips. These portraits draw from their
 * native palette without borrowing any of the sprites' VRAM. */
static TILE mixedPortraitDraw[ROSTER_PORTRAITS_VISIBLE][2][ROSTER_PORTRAIT_PIXELS];

void hwClockHold(int on); /* system/hw.c */
void pcGpuQuiet(int on); /* gpu/libgpu.c */
void gpuReadVram(u16* dst); /* gpu/vram.c */

void pcBattleRosterHudReset(void) {
    __builtin_memset(rosterSpritePortrait, 0, sizeof(rosterSpritePortrait));
    __builtin_memset(rosterPortraitSlot, -1, sizeof(rosterPortraitSlot));
}

/* The original tiles are packed into 4-bit or 8-bit texture pages. Sample
 * the loaded pages and CLUTs at native resolution, including shared tiles. */
static u16 rosterSpritePixel(const u16 (*vram)[1024], const SubSprite* p,
                             const SpriteActorCore* actor, s32 u, s32 v) {
    const SpriteVramData* sprite = actor->m24_vramData;
    const u16* cluts = (const u16*)sprite->mC_clutData;
    s32 depth = (p->mA_tpage >> 7) & 1;
    s32 bits = 4 << depth;
    s32 shift = 2 - depth;
    s32 x = (p->mA_tpage & 15) * 64 + (u >> shift);
    s32 y = (p->mA_tpage & 16) * 16 + v;
    u16 word = vram[y & 511][x & 1023];
    s32 color = (word >> ((u & ((1 << shift) - 1)) * bits)) & ((1 << bits) - 1);
    s32 clutX = (p->mC_clut & 63) * 16 + color;
    s32 clutY = (p->mC_clut >> 6) & 511;
    s32 offset = clutX - sprite->m8_clut.vx;

    /* The intro fades the battle's live CLUTs. For the actor's own row,
     * sample the selected palette from the original animation bundle. */
    if (clutY == sprite->m8_clut.vy && cluts != NULL && offset >= 0 && offset < *cluts * 16) {
        return cluts[2 + *cluts * actor->m3C_x14 * 16 + offset];
    }
    return vram[clutY][clutX & 1023];
}

static void captureRosterPortrait(RosterSpritePortrait* portrait, const SpriteActorCore* actor,
                                  const u16 (*vram)[1024]) {
    const SubSprite* parts = actor->m20_object->m30_subSprites;
    RECT bounds[ROSTER_PORTRAIT_PARTS];
    u16 silhouette[ROSTER_PORTRAIT_PIXELS] = {0};
    const s32 fit = ROSTER_PORTRAIT_SIZE - 2 * ROSTER_PORTRAIT_RIM;
    s32 count = actor->m40_draw.bits.x2_subSpriteCount;
    s32 left = 32767, top = 32767, right = -32768, bottom = -32768;
    s32 size, marginX, marginY;
    s32 flipX;
    s32 i;

    /* Keep the frame's authored mirror bit and use the left-facing sprite
     * direction. The actor's live facing can change during the battle. */
    flipX = actor->mAC_x3;

    for (i = 0; i < count; i++) {
        const SubSprite* part = &parts[i];
        const SubgroupTransform* groups = actor->m20_object->m34_perSubgroupTransform;
        s32 x = part->m0_offsetX;
        s32 y = part->m2_offsetY;
        s32 w = part->m6_width + part->m8_widthAdjust;
        s32 h = part->m7_height + part->m9_heightAdjust;

        bounds[i].w = 0;
        if (w <= 0 || h <= 0 || part->m6_width == 0 || part->m7_height == 0) continue;
        if (groups != NULL) {
            const SubgroupTransform* group = &groups[part->m14_attrs.bits.x0_transformId];
            x += group->m0_translateX;
            y += group->m1_translateY;
        }
        bounds[i] = (RECT){x, y, w, h};
        if (x < left) left = x;
        if (y < top) top = y;
        if (x + w > right) right = x + w;
        if (y + h > bottom) bottom = y + h;
    }
    if (right <= left || bottom <= top) return;

    size = right - left > bottom - top ? right - left : bottom - top;
    marginX = (ROSTER_PORTRAIT_SIZE - (right - left) * fit / size) / 2;
    marginY = (ROSTER_PORTRAIT_SIZE - (bottom - top) * fit / size) / 2;
    for (i = count - 1; i >= 0; i--) {
        const SubSprite* part = &parts[i];
        const RECT* rect = &bounds[i];
        s32 x0, y0, x1, y1;
        s32 x, y;

        if (rect->w == 0) continue;
        x0 = marginX + (rect->x - left) * fit / size;
        y0 = marginY + (rect->y - top) * fit / size;
        x1 = marginX + (rect->x + rect->w - left) * fit / size;
        y1 = marginY + (rect->y + rect->h - top) * fit / size;
        if (x0 == x1 || y0 == y1) continue;

        for (y = y0; y < y1; y++) {
            s32 v = (2 * (y - y0) + 1) * part->m7_height / (2 * (y1 - y0));
            if (part->m14_attrs.bits.x5_tileFlipY) v = part->m7_height - 1 - v;
            for (x = x0; x < x1; x++) {
                s32 u = (2 * (x - x0) + 1) * part->m6_width / (2 * (x1 - x0));
                s32 dst = flipX ? ROSTER_PORTRAIT_SIZE - 1 - x : x;
                u16 color;
                if (part->m14_attrs.bits.x4_tileFlipX) u = part->m6_width - 1 - u;
                color = rosterSpritePixel(vram, part, actor, (part->m4_texcoordX + u) & 255,
                                         (part->m5_texcoordY + v) & 255);
                if (color != 0) silhouette[y * ROSTER_PORTRAIT_SIZE + dst] = color;
            }
        }
    }
    count = 1; /* Palette entry 0 is transparent. */
    for (i = 0; i < ROSTER_PORTRAIT_PIXELS; i++) {
        u16 color = silhouette[i];
        s32 entry;
        for (entry = 0; entry < count && portrait->palette[entry] != color; entry++) {}
        if (entry == count && count < 256) portrait->palette[count++] = color;
        if (entry == 256) {
            s32 distance = 32767;
            s32 c;
            /* Several differently coloured parts can exceed one CLUT. */
            for (c = 1; c < 256; c++) {
                u16 p = portrait->palette[c];
                s32 r = (color & 31) - (p & 31);
                s32 g = ((color >> 5) & 31) - ((p >> 5) & 31);
                s32 b = ((color >> 10) & 31) - ((p >> 10) & 31);
                s32 delta = r * r + g * g + b * b;
                if (delta < distance) {
                    distance = delta;
                    entry = c;
                }
            }
        }
        portrait->pixels[i] = entry;
    }
    portrait->ready = count > 1;
}

static void captureRosterPortraits(void) {
    static u16 snapshot[512][1024];
    s32 readVram = 0;
    s32 i;
    for (i = pcArenaRosterFirstEnemy() - FIRST_ENEMY; i < ENEMIES; i++) {
        s32 slot = i + FIRST_ENEMY;
        const SpriteActorCore* actor;
        if (rosterSpritePortrait[i].ready || !isBattleSlotFilled.m0_isBattleSlotFilled[slot] ||
            pcArenaRosterPortrait(slot, NULL) != NULL) continue;
        actor = battleBssBlock.m8C8C_spriteActorCores[slot];
        if (actor == NULL || actor->m3C_x0_type != 1 || actor->m24_vramData == NULL ||
            actor->m20_object == NULL || actor->m20_object->m30_subSprites == NULL ||
            actor->m40_draw.bits.x2_subSpriteCount == 0 ||
            actor->m44_currentAnimationBundle != actor->m48_defaultAnimationBundle) continue;
        if (!readVram) {
            gpuReadVram(snapshot[0]);
            readVram = 1;
        }
        captureRosterPortrait(&rosterSpritePortrait[i], actor, snapshot);
    }
}

/* Enemy-only Versus has no party actors. Borrow the first row of their
 * sprite strip and the unused party portrait/actor CLUTs for the visible
 * five 8-bit portraits. */
static const s16 rosterPortraitClutY[ROSTER_PORTRAITS_VISIBLE] = {0x1F9, 0x1F8, 0x1F7, 0x1C0, 0x1C1};

/* Both HUD renderers use the fighter's owning side, independent of the
 * camera angle, the attacking actor or the screen used to view the match. */
static s32 rosterPortraitColumn(s32 side, s32 x) {
    return side == 0 ? x : ROSTER_PORTRAIT_SIZE - 1 - x;
}

static void prepareRosterPortraits(const s32* slots, s32 count) {
    s32 uploading = 0;
    s32 i;
    for (i = 0; i < count; i++) {
        s32 slot = slots[i];
        const u16* palette;
        const u8* pixels;
        u8 oriented[ROSTER_PORTRAIT_PIXELS];
        RECT rect;
        if (rosterPortraitSlot[i] == slot) continue;
        pixels = pcArenaRosterPortrait(slot, &palette);
        if (pixels == NULL) {
            RosterSpritePortrait* fallback = &rosterSpritePortrait[slot - FIRST_ENEMY];
            if (!fallback->ready) continue;
            pixels = fallback->pixels;
            palette = fallback->palette;
        }
        if (!uploading) {
            hwClockHold(1);
            pcGpuQuiet(1);
            uploading = 1;
        }
        rect = (RECT){0, rosterPortraitClutY[i], 256, 1};
        LoadImage(&rect, (void*)palette);
        rect = (RECT){0x100 + i * (ROSTER_PORTRAIT_SIZE / 2), 0x1C0, ROSTER_PORTRAIT_SIZE / 2, ROSTER_PORTRAIT_SIZE};
        s32 side = pcArenaRosterSide(slot), y, x;
        for (y = 0; y < ROSTER_PORTRAIT_SIZE; y++) {
            for (x = 0; x < ROSTER_PORTRAIT_SIZE; x++) {
                oriented[y * ROSTER_PORTRAIT_SIZE + x] = pixels[y * ROSTER_PORTRAIT_SIZE + rosterPortraitColumn(side, x)];
            }
        }
        LoadImage(&rect, oriented);
        rosterPortraitSlot[i] = slot;
    }
    if (uploading) {
        pcGpuQuiet(0);
        hwClockHold(0);
    }
}

static void drawMixedPortrait(const RosterHud* hud, s32 index, s32 slot, s32 x, s32 y, s32 rim) {
    const u16* palette;
    const u8* pixels;
    TILE* tile = mixedPortraitDraw[index][hud->odd];
    s32 side = pcArenaRosterSide(slot);
    s32 endPixel = ROSTER_PORTRAIT_SIZE - rim;
    s32 row, col;

    if (slot < pcArenaRosterFirstEnemy()) {
        pixels = pcVersusSavedPortrait(slot, &palette);
    } else {
        pixels = pcArenaRosterPortrait(slot, &palette);
        if (pixels == NULL && rosterSpritePortrait[slot - FIRST_ENEMY].ready) {
            pixels = rosterSpritePortrait[slot - FIRST_ENEMY].pixels;
            palette = rosterSpritePortrait[slot - FIRST_ENEMY].palette;
        }
    }
    if (pixels == NULL) return;
    for (row = rim; row < endPixel; row++) {
        const u8* line = pixels + row * ROSTER_PORTRAIT_SIZE;
        for (col = rim; col < endPixel;) {
            u16 color = palette[line[rosterPortraitColumn(side, col)]];
            s32 end = col + 1;
            while (end < endPixel && palette[line[rosterPortraitColumn(side, end)]] == color) end++;
            if (color != 0 && (color & 0x7FFF) != 0x7FFF) {
                setlen(tile, 3);
                setcode(tile, 0x60);
                setRGB0(tile, (color & 31) << 3, ((color >> 5) & 31) << 3, ((color >> 10) & 31) << 3);
                tile->x0 = x + col;
                tile->y0 = y + row;
                tile->w = end - col;
                tile->h = 1;
                AddPrim(hud->ot, tile);
                pcWidePrim(tile++, hud->wideX, 0);
            }
            col = end;
        }
    }
}

static void drawPortraitSquare(const RosterHud* hud, TILE* tile, s32 x, s32 y, s32 size, u8 r, u8 g, u8 b) {
    setlen(tile, 3);
    setcode(tile, 0x60);
    setRGB0(tile, r, g, b);
    tile->x0 = x;
    tile->y0 = y;
    tile->w = tile->h = size;
    AddPrim(hud->ot, tile);
    pcWidePrim(tile, hud->wideX, 0);
}

static void drawRosterPortrait(const RosterHud* hud, s32 index, s32 slot, s32 x, s32 y) {
    TILE* box = rosterPortraits[index][hud->odd];
    s32 rim = slot < pcArenaRosterFirstEnemy() ? 0 : ROSTER_PORTRAIT_RIM;
    s32 size = ROSTER_PORTRAIT_SIZE - 2 * rim;

    if (battleVar0->m6415_drawHighlight && slot == battleVar2->m2D3_currentEntityTurn) {
        POLY_F4* pulse = &battleVar0->m63C8_portraitHighlight[hud->odd];
        s32 level = battleVar0->m6410_highlightLevel;
        setXYWH(pulse, x, y, ROSTER_PORTRAIT_SIZE, ROSTER_PORTRAIT_SIZE);
        setRGB0(pulse, level, level, level);
        /* The sort order draws the background and portrait before this overlay. */
        AddPrim(hud->ot, pulse);
        AddPrim(hud->ot, &battleVar0->m63F8_portraitHighlightDrMode[hud->odd]);
        pcWidePrim(pulse, hud->wideX, 0);
    }

    if (pcArenaRosterMixed()) {
        drawMixedPortrait(hud, index, slot, x, y, rim);
    } else if (rosterPortraitSlot[index] == slot) {
        POLY_FT4* p = &rosterPortraitDraw[index][hud->odd];
        s32 u = index * ROSTER_PORTRAIT_SIZE + rim;
        s32 v = 0xC0 + rim;
        SetPolyFT4(p);
        SetShadeTex(p, 1);
        setXY4(p, x + rim, y + rim, x + rim + size, y + rim,
               x + rim, y + rim + size, x + rim + size, y + rim + size);
        setUV4(p, u, v, u + size, v, u, v + size, u + size, v + size);
        p->tpage = GetTPage(1, 0, 0x100, 0x1C0);
        p->clut = GetClut(0, rosterPortraitClutY[index]);
        pcTextureClass(p, PC_TEX_BATTLE_PORTRAIT);
        AddPrim(hud->ot, p);
        pcWidePrim(p, hud->wideX, 0);
    }

    drawPortraitSquare(hud, &box[0], x + rim, y + rim, size, 0xFF, 0xFF, 0xFF);
    /* The saved portraits already have their inner rim in the TIM. */
    if (rim != 0) {
        drawPortraitSquare(hud, &box[1], x, y, ROSTER_PORTRAIT_SIZE, 0x28, 0x18, 0x08);
    }
}

/* Use the party's large numbers, slash, advance and warning colours.
 * Enemy HP can have five digits. Fit long readouts horizontally inside
 * their column without reducing the height of the original glyphs. */
static void drawRosterHp(const RosterHud* hud, s32 index, s32 slot, s32 x, s32 y, s32 width) {
    const sBattleEntity* actor = &battleEntities.m0_entities[slot];
    POLY_FT4 (*polys)[2] = rosterHp[index];
    Readout hp;
    s32 count = 0;
    s32 digits;
    s32 naturalWidth;
    u8 tint;
    s32 k;

    entityHp(actor, &hp);
    digits = hp.hpCount + hp.maxCount;
    naturalWidth = (digits + 1) * HP_CELL;
    for (k = 0; k <= digits; k++) {
        s32 glyph = k == hp.hpCount ? 0x71 : 0x67 + hp.digits[k - (k > hp.hpCount)];
        count += battleSetupStringInPolyFT4Large(glyph, &polys[count], x + k * HP_CELL, y);
    }
    tint = actor->m0_base.m4C_HP <= actor->m0_base.m4E_MaxHP / 8 ? 2
           : actor->m0_base.m4C_HP <= actor->m0_base.m4E_MaxHP / 4 ? 1 : 0;
    if (tint) tintFT4(polys[0], 0, count, tint);
    for (k = 0; k < count; k++) {
        POLY_FT4* poly = &polys[k][hud->odd];
        if (naturalWidth > width) {
            poly->x0 = x + (poly->x0 - x) * width / naturalWidth;
            poly->x1 = x + (poly->x1 - x) * width / naturalWidth;
            poly->x2 = x + (poly->x2 - x) * width / naturalWidth;
            poly->x3 = x + (poly->x3 - x) * width / naturalWidth;
        }
        pcTextureClass(poly, PC_TEX_TEXT);
        AddPrim(hud->ot, poly);
        pcWidePrim(poly, hud->wideX, 0);
    }
}

static s32 rosterTimeFill(s32 slot) {
    s32 total = battleSlotStatusVar0.m0_battleSlotStatusVar0[slot];
    s32 remaining = battleSlotStatusVar0.m16_numTicksBeforeReady[slot];

    if (isEntityReadyForBattle[slot] == 0xFF) return 0;
    if (isEntityReadyForBattle[slot] == 1) return ROSTER_TIME_W;
    if (total <= 0 || remaining >= total) return 0;
    if (remaining <= 0) return ROSTER_TIME_W;
    return (total - remaining) * ROSTER_TIME_W / total;
}

/* Use the original status HUD's Time glyph and time-bar texture, including
 * its separate Haste and Slow palettes. */
static void drawRosterTime(const RosterHud* hud, s32 index, s32 slot, s32 x, s32 y) {
    POLY_FT4 (*timer)[2] = rosterTimer[index];
    s32 odd = hud->odd;
    s32 count = battleSetupStringInPolyFT4Large(0x8F, timer, x + 23, y + 5);
    s32 shadeFrom;
    s32 fill = rosterTimeFill(slot);
    s32 i;

    count += battleSetupStringInPolyFT4Small(0x52, &timer[count], x + 27, y + 2);
    shadeFrom = count;
    count += battleSetupStringInPolyFT4Small(0x53, &timer[count], x + 27, y + 2);
    for (i = 0; i < count; i++) {
        POLY_FT4* part = &timer[i][odd];
        if (i >= shadeFrom) battleSetupTextPoly(part);
        pcTextureClass(part, PC_TEX_HUD);
        AddPrim(hud->ot, part);
        pcWidePrim(part, hud->wideX, 0);
    }
    if (fill > 0) {
        const sBattleEntity* actor = &battleEntities.m0_entities[slot];
        POLY_GT4* bar = &rosterTimeBar[index][odd];
        s32 u = (battleVar0->mA234_glyphTextures[0].m10_tpage_X & 0x3F) << 1;
        s32 v = battleVar0->mA234_glyphTextures[0].m14_tpage_Y;

        *bar = battleVar0->m5A0_timeBarFills[0][odd];
        setXY4(bar, x, y, x + fill, y, x, y + 4, x + fill, y + 4);
        setUV4(bar, u, v, u + fill * 2, v, u, v + 4, u + fill * 2, v + 4);
        if ((actor->m0_base.m84_statusFlags2[0] | actor->m0_base.m84_statusFlags2[1]) & 0x8000) {
            bar->clut = battleVar0->mA2B2_hasteTimeBarClut;
        } else if (actor->m0_base.m7C_statusFlags[0] & 0x1000) {
            bar->clut = battleVar0->mA2B0_slowTimeBarClut;
        } else {
            bar->clut = battleVar0->mA2AC_timeBarClut;
        }
        pcTextureClass(bar, PC_TEX_HUD);
        AddPrim(hud->ot, bar);
        pcWidePrim(bar, hud->wideX, 0);
    }
}

static void drawRosterAp(const RosterHud* hud, s32 index, s32 slot, s32 x, s32 y) {
    POLY_FT4 (*polys)[2] = rosterAp[index];
    s32 ap = apConfigArray[slot].m0_storedAP;
    s32 count = battleSetupStringInPolyFT4Large(0x90, polys, x + 11, y);
    s32 i;

    if (ap >= 10) count += battleSetupStringInPolyFT4Large(0x83 + ap / 10, &polys[count], x + 39, y - 2);
    count += battleSetupStringInPolyFT4Large(0x83 + ap % 10, &polys[count], x + 45, y - 2);
    for (i = 0; i < count; i++) {
        POLY_FT4* poly = &polys[i][hud->odd];
        pcTextureClass(poly, PC_TEX_HUD);
        AddPrim(hud->ot, poly);
        pcWidePrim(poly, hud->wideX, 0);
    }
}

void pcBattleRosterHud(void) {
    s32 slots[ENEMIES];
    s32 count = 0;
    s32 side;
    s32 columns;
    s32 i;
    RosterHud hud;

    s32 mixed = pcArenaRosterMixed();
    if ((!mixed && !pcArenaRosterEnemyTeams()) || battleVar0 == NULL || battleVar1 == NULL || battleVar2 == NULL ||
        battleFont == NULL || currentBattleSpecificStrings == NULL) {
        return;
    }
    side = pcVersusHudSide();
    for (i = mixed ? 0 : FIRST_ENEMY; i < BATTLE_SLOTS; i++) {
        if (!isBattleSlotFilled.m0_isBattleSlotFilled[i]) continue;
        if (pcArenaRosterSide(i) == side) slots[count++] = i;
    }
    if (count == 0) return;
    captureRosterPortraits();
    if (!mixed) prepareRosterPortraits(slots, count);
    columns = count <= 3 ? count : pcWideView >= 24 ? 4 : 3;
    hud = (RosterHud){D_800CCB04.m0_pCurrentBattleOT + 1, D_800CCB04.m30_battleOddOrEven, 0};
    for (i = 0; i < count; i++) {
        /* Any second row is on the right, clear of the selector. */
        s32 col = i < columns ? i : columns - (count - i);
        s32 y = i < columns ? 0 : 40;
        s32 x;
        s32 hpWidth;

        if (columns == 4) {
            x = col * 76 - 8;
            hpWidth = 44 + pcWideView / 2;
            hud.wideX = col * pcWideView / 2 - pcWideView;
        } else {
            x = partyMemberSpritesOffset[(columns - 1) * 3 + col] + col * 96;
            hpWidth = count == 1 ? 92 : 68;
            hud.wideX = columns > 1 ? (2 * col - columns + 1) * pcWideView / (columns - 1) : 0;
        }
        drawRosterPortrait(&hud, i, slots[i], x + 16, y + 8);
        drawRosterHp(&hud, i, slots[i], x + 44, y + 16, hpWidth);
        drawRosterTime(&hud, i, slots[i], x + 45, y + 26);
        if (mixed && slots[i] < pcArenaRosterFirstEnemy()) drawRosterAp(&hud, i, slots[i], x + 45, y + 39);
    }
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
    s32 count = 0;
    s32 nameWidth = 0;
    s32 hpWidth = 0;
    s32 statusLines = 0;
    s32 epLines = 0;
    s32 innerW;
    s32 innerH;
    s32 x;
    s32 y;
    s32 used;
    s32 only = -1; /* the one marked target, else every one listed */
    s32 knownOnly;
    s32 versus;
    s32 view;
    s32 first = FIRST_ENEMY; /* the side listed: its first slot, how many, their bits */
    s32 slots = ENEMIES;
    u16 side = 0x7F8;
    u16 marked;
    s32 i;
    s32 j;

    versus = pcVersusSession() == PC_VERSUS_IN;
    if (!(versus ? pcVersusOpponentHpSetting() : pcGameplayEnemyInfo()) || battleVar0 == NULL || battleVar1 == NULL ||
        battleFont == NULL || battleAttackNameStrings == NULL || currentBattleSpecificStrings == NULL) {
        return;
    }
    if (versus) {
        /* Select on the pad of the player whose screen this is */
        view = pcVersusPerspective();
        held = view == 2 ? pcPadHeldRemappedPort(1)
               : view == 1 ? pcPadHeldRemappedPort(0)
                           : pcPadHeldRemappedPort(0) | pcPadHeldRemappedPort(1);
        first = pcVersusInfoFirst();
        if (pcArenaRosterMixed()) {
            first = 0;
            slots = BATTLE_SLOTS;
            side = 0;
            for (i = 0; i < slots; i++) {
                if (pcArenaRosterSide(i) != pcVersusHudSide()) side |= 1 << i;
            }
        } else if (first == 0) {
            slots = PARTY;
            side = 0x7;
        } else if (pcArenaRosterEnemyTeams()) {
            s32 shownSide = pcVersusHudSide();
            side = 0;
            for (i = first; i < first + slots; i++) {
                if (pcArenaRosterSide(i) != shownSide) side |= 1 << i;
            }
        }
    } else {
        held = pcPadHeldRemapped();
    }
    if (pcGameplaySelectTap(previousHeld, held, &selectChord)) {
        shown ^= 1;
    }
    previousHeld = held;
    if (!shown) {
        return;
    }
    odd = D_800CCB04.m30_battleOddOrEven;
    ot = D_800CCB04.m0_pCurrentBattleOT + 1;
    /* Rendered text is kept from frame to frame only while the window is
     * drawn every frame: after a gap (hidden, another battle: its names are
     * other strings at the same index) everything is rendered again. */
    if (g_playTimeInVsync - lastDrawn > 4 || g_playTimeInVsync < lastDrawn) {
        for (i = 0; i < BATTLE_SLOTS; i++) {
            texts[i].nameId = NO_NAME;
            texts[i].statusCount = 0;
        }
        epWidth = -1;
    }
    lastDrawn = g_playTimeInVsync;

    knownOnly = pcGameplayEnemyInfoKnown();
    marked = updateTargetSelectionMarkerSub0_var0;
    if ((marked & ~side & 0x7FF) == 0 && (marked & side) != 0 && (marked & (marked - 1)) == 0) {
        for (only = first; !(marked & (1 << only)); only++) {
        }
    }

    /* The rows: who, with what rendered. */
    for (i = 0; i < slots; i++) {
        s32 slot = first + i;
        sBattleEntity* e = &battleEntities.m0_entities[slot];
        EnemyText* t = &texts[i];
        Row* r;
        u8 ids[STATUSES];
        u8 nameId;
        s32 n;

        if (!(side & (1 << slot)) || !isBattleSlotFilled.m0_isBattleSlotFilled[slot] || (battleVisualEntities[slot].m3_hidden & 0x80) ||
            (slot >= FIRST_ENEMY && (e->m0_base.m34_monsterFlags & 0x400)) || (only >= 0 && slot != only)) {
            t->nameId = NO_NAME;
            t->statusCount = 0;
            continue;
        }
        r = &rows[count++];
        r->slot = (u8)slot;
        nameId = versus ? (u8)pcVersusInfoName(slot) : battleMonsterMapping[slot];
        if (t->nameId != nameId) {
            t->nameId = nameId;
            u8* name = (pcArenaRosterEnemyTeams() || (pcArenaRosterMixed() && slot >= pcArenaRosterFirstEnemy()))
                           ? pcVersusEnemyText(slot, -1)
                           : getDialogParamPointer(currentBattleSpecificStrings, t->nameId);
            t->nameWidth = renderLine(name, t->nameImage);
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
        r->hpShown = versus || !knownOnly ||
                     (pcBestiaryKnown != NULL && battleMonsterMapping[slot] != 0 &&
                      pcBestiaryKnown(currentBattleConfig[0], battleMonsterMapping[slot] - 1));
        entityHp(e, &r->hp);
        r->epShown = versus && !(e->m15A_flags & 0x80) && e->m0_base.m52_MaxMP != 0; /* a Gear, and many an enemy, have none */
        if (r->epShown) {
            hpDigits(e->m0_base.m50_MP, e->m0_base.m52_MaxMP, &r->ep);
            if (readoutWidth(&r->ep) > hpWidth) hpWidth = readoutWidth(&r->ep);
            epLines++;
        }
        if (t->nameWidth > nameWidth) nameWidth = t->nameWidth;
        if (hpWidthOf(r) > hpWidth) hpWidth = hpWidthOf(r);
        if (!r->hpShown) r->hp.hpCount = r->hp.maxCount = 0;
        statusLines += n;
    }
    if (count == 0) {
        return;
    }
    if (epLines != 0 && epWidth < 0) {
        u8 word[VERSUS_LABEL];
        pcVersusWord(word, "EP");
        epWidth = renderLine(word, epImage);
    }
    if (epLines != 0 && STATUS_INDENT + epWidth > nameWidth) {
        nameWidth = STATUS_INDENT + epWidth;
    }
    /* The window's size: statuses, then the last rows, go when it would not fit. */
    innerH = 2 * WIN_PAD_Y + count * ROW_H + epLines * ROW_H + statusLines * STATUS_H;
    for (i = count - 1; i >= 0 && WIN_BOTTOM - innerH < WIN_TOP_MIN; i--) {
        innerH -= rows[i].statusCount * STATUS_H;
        rows[i].statusCount = 0;
    }
    while (count > 1 && WIN_BOTTOM - innerH < WIN_TOP_MIN) {
        count--;
        innerH -= ROW_H + (rows[count].epShown ? ROW_H : 0);
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
        EnemyText* t = &texts[r->slot - first];
        s32 s;

        used = drawText(ot, used, t->nameImage, t->nameWidth, x + WIN_PAD_X, j);
        if (r->hpShown) {
            used = drawHp(ot, odd, used, &r->hp, &slashPolys[i], x + innerW - WIN_PAD_X - hpWidthOf(r), j);
        }
        j += ROW_H;
        if (r->epShown) { /* "EP", its readout under the HP's */
            used = drawText(ot, used, epImage, epWidth, x + WIN_PAD_X + STATUS_INDENT, j);
            used = drawHp(ot, odd, used, &r->ep, &epSlashPolys[i], x + innerW - WIN_PAD_X - readoutWidth(&r->ep), j);
            j += ROW_H;
        }
        for (s = 0; s < r->statusCount; s++, j += STATUS_H) {
            used = drawText(ot, used, t->statusImage[s], t->statusWidth[s], x + WIN_PAD_X + STATUS_INDENT, j);
        }
    }
    buildFrame(x, y, innerW, innerH);
    linkFrame(ot);
}
