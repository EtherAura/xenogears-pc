/*
 * The menus' wide layout (include/pc_wide.h). Every menu overlay has a copy:
 * menu here, and menu_shop, menu_party, menu_name_entry, menu_gear_shop
 * through port/src/game/<overlay>/wide.c, which include this file.
 *
 * Each overlay's menuDraw calls menuWideLayout() just before its DrawOTag.
 * It walks the frame's ordering table, finds the part of the menu each packet
 * belongs to (by its address: the gMenuContext structures) and moves whole
 * parts in the wide picture with pcWidePrim. The 4:3 picture, VRAM and the
 * game never see any of it, and with a 4:3 aspect it does nothing.
 *
 * The layout (the same at every wide aspect):
 *   - the main menu: its command list, the sub-menus that open over it and
 *     the hand cursor go to the left edge, the gold and play time boxes to the
 *     right edge; the screens' panels (status, equipment, items, abilities,
 *     gear, save) stay centred, with the dimmed command list beside them;
 *   - name entry: the name box goes to the left edge, Accept/Cancel to the
 *     right, the character grid stays centred;
 *   - centred as a whole: the shops and the gear shop (their Buy/Sell list
 *     sits under the panels it opens), party change, and the title's menu
 *     (Continue), which belongs to the title's centred logo.
 * Parts move by the whole margin, so at every aspect the groups sit at the
 * picture's edges as they sit at the 4:3 picture's (the user's choice, also
 * on 32:9).
 *
 * XG_MENU_WIDE_LOG=1 prints the parts on screen (name, packets, extent in the
 * 4:3 picture, offset) whenever they change.
 */
#include "common.h"
#include "menu/menu.h"
#include "menu/shop.h"

const char* portOption(const char* name);
u32 hwVSyncCount(void);
int printf(const char* fmt, ...);

/* The parts of a menu. */
enum {
    MW_OTHER, /* not in any menu structure (the gear shop's model) */
    MW_CONTEXT,
    MW_GOLD,
    MW_TIME,
    MW_CURSOR,
    MW_DARKEN,
    MW_CURSORBOX,
    MW_SAVE,
    MW_MAINMENU,
    MW_SUBMENU,
    MW_SELCARD,
    MW_STATS,
    MW_EQUIPNAMES,
    MW_BOX,
    MW_INFOCARD,
    MW_CARDTILE,
    MW_CARDCURSOR,
    MW_ITEMLIST,
    MW_ETHERLIST,
    MW_EQUIPLIST,
    MW_SKILLLIST,
    MW_SCROLLTHUMB,
    MW_ARROWS,
    MW_POINTER,
    MW_PROGRESS,
    MW_SHOP,
    MW_GEARSHOP,
    MW_RESERVECARD,
    MW_PARTYCARD,
    MW_NAMEBUTTONS,
    MW_NAMEBOX,
    MW_NAMEENTRY,
    MW_CARDLABELS,
    MW_MESSAGE,
    MW_LABELS,
    MW_MAINLABELS,
    MW_SUBLABELS,
    MW_SAVELABELS,
    MW_LISTLABELS,
    MW_EQUIPLABELS,
    MW_SKILLPAGE,
    MW_GEARLABELS,
    MW_SOUNDLABELS,
    MW_KINDS
};

static const char* const mwNames[MW_KINDS] = {
    "other",      "context",   "gold",        "time",       "cursor",     "darken",      "cursorbox",
    "save",       "mainmenu",  "submenu",     "selcard",    "stats",      "equipnames",  "box",
    "infocard",   "cardtile",  "cardcursor",  "itemlist",   "etherlist",  "equiplist",   "skilllist",
    "scrollthumb", "arrows",   "pointer",     "progress",   "shop",       "gearshop",    "reservecard",
    "partycard",  "namebuttons", "namebox",   "nameentry",  "cardlabels", "message",     "labels",
    "mainlabels", "sublabels", "savelabels",  "listlabels", "equiplabels", "skillpage",  "gearlabels",
    "soundlabels",
};

/* What a packet belongs to. */
typedef struct {
    const u8* lo;
    const u8* hi;
    s16 kind;
    s16 index;
} MwRange;

/* One part of the menu on screen this frame. */
typedef struct {
    s16 kind;
    s16 index;
    s16 count;
    s16 x0, x1, y0, y1;
    s16 dx;
} MwPart;

#define MW_RANGES 128
#define MW_PARTS 128
#define MW_PACKETS 8192

static MwRange mwRanges[MW_RANGES];
static s32 mwRangeCount;
static MwPart mwParts[MW_PARTS];
static s32 mwPartCount;
static void* mwPackets[MW_PACKETS];
static s16 mwPacketPart[MW_PACKETS];
static s32 mwPacketCount;
static s32 mwLog = -1;
static u32 mwLastSignature;

static void mwAdd(const void* p, s32 size, s32 kind, s32 index) {
    if (p == NULL || mwRangeCount >= MW_RANGES) {
        return;
    }
    mwRanges[mwRangeCount].lo = p;
    mwRanges[mwRangeCount].hi = (const u8*)p + size;
    mwRanges[mwRangeCount].kind = kind;
    mwRanges[mwRangeCount].index = index;
    mwRangeCount++;
}

#define MW_ADD(p, kind, i) mwAdd((p), (s32)sizeof(*(p)), (kind), (i))

static void mwBuildRanges(void) {
    MenuContext* c = gMenuContext;
    s32 i;

    mwRangeCount = 0;
    /* Inner structures first: the first range that holds a packet wins. */
    MW_ADD(c->m340_gold, MW_GOLD, 0);
    MW_ADD(c->m344_playTime, MW_TIME, 0);
    if (c->m348_cursor != NULL) {
        MW_ADD(&c->m348_cursor->m0_cursor, MW_CURSOR, 0);
        MW_ADD(&c->m348_cursor->m98_poly, MW_DARKEN, 0);
        MW_ADD(c->m348_cursor, MW_CURSORBOX, 0);
    }
    MW_ADD(c->m34C_saveScreen, MW_SAVE, 0);
    MW_ADD(c->m350_mainMenu, MW_MAINMENU, 0);
    MW_ADD(c->m354_subMenu, MW_SUBMENU, 0);
    MW_ADD(c->m358_selectedInfoCard, MW_SELCARD, 0);
    MW_ADD(c->m35C_selectedStats, MW_STATS, 0);
    MW_ADD(c->m360_equipment, MW_EQUIPNAMES, 0);
    for (i = 0; i < 7; i++) {
        MW_ADD(c->m364_menuBoxes[i], MW_BOX, i);
    }
    for (i = 0; i < 3; i++) {
        MW_ADD(c->m39C_infoCards[i], MW_INFOCARD, i);
    }
    for (i = 0; i < 0x20; i++) {
        MW_ADD(c->m3A8_memoryCardTiles[i], MW_CARDTILE, i);
    }
    MW_ADD(c->m428_cardCursors, MW_CARDCURSOR, 0);
    MW_ADD(c->m42C_itemMenu, MW_ITEMLIST, 0);
    MW_ADD(c->m430_etherMenu, MW_ETHERLIST, 0);
    MW_ADD(c->m434_equipMenu, MW_EQUIPLIST, 0);
    MW_ADD(c->m438_skillMenu, MW_SKILLLIST, 0);
    MW_ADD(c->m43C_scrollBarThumb, MW_SCROLLTHUMB, 0);
    MW_ADD(c->m440_arrows, MW_ARROWS, 0);
    for (i = 0; i < 2; i++) {
        MW_ADD(c->m444_pointerGlyphs[i], MW_POINTER, i);
    }
    MW_ADD(c->m44C_progressBox, MW_PROGRESS, 0);
    MW_ADD(c->m450_shop, MW_SHOP, 0);
    MW_ADD(c->m454_gearShop, MW_GEARSHOP, 0);
    for (i = 0; i < 6; i++) {
        MW_ADD(c->m1DF0_reserveCards[i], MW_RESERVECARD, i);
    }
    for (i = 0; i < 3; i++) {
        MW_ADD(c->m1E08_partyCards[i], MW_PARTYCARD, i);
    }
    if (c->m1E20_nameEntry != NULL) {
        MenuContext_1E20* n = c->m1E20_nameEntry;
        MW_ADD(&n->m0_buttonIcon1, MW_NAMEBUTTONS, 0);
        MW_ADD(&n->m50_buttonIcon2, MW_NAMEBUTTONS, 0);
        MW_ADD(&n->m140_portrait, MW_NAMEBOX, 0);
        MW_ADD(&n->mCD0_typedName, MW_NAMEBOX, 0);
        MW_ADD(&n->mD80_cursor, MW_NAMEBOX, 0);
        MW_ADD(n, MW_NAMEENTRY, 0);
    }
    if (c->m32C_memoryCardContext != NULL) {
        MW_ADD(&c->m32C_memoryCardContext->m4D94_cardLabels, MW_CARDLABELS, 0);
    }
    for (i = 0; i < 4; i++) {
        MW_ADD(c->m1DE0_messageLines[i], MW_MESSAGE, i);
    }
    /* The label arrays, one part per label. */
    for (i = 0; i < 50; i++) {
        static const struct {
            s16 first, count, kind;
        } groups[] = {
            {0, 4, MW_LABELS},       {4, 8, MW_MAINLABELS},  {12, 6, MW_SUBLABELS},
            {18, 6, MW_SAVELABELS},  {24, 8, MW_LISTLABELS}, {32, 6, MW_EQUIPLABELS},
            {38, 2, MW_SKILLPAGE},   {40, 6, MW_GEARLABELS}, {46, 4, MW_SOUNDLABELS},
        };
        s32 g;
        for (g = 0; g < 8 && i >= groups[g + 1].first; g++) {
        }
        mwAdd(&c->m4E0_labels[i], (s32)sizeof(MenuContext_4E0), groups[g].kind, i - groups[g].first);
    }
    MW_ADD(c, MW_CONTEXT, 0);
}

/* A packet's x and y extent (its GP0 words follow the 64-bit tag). */
static s32 mwExtent(const OTag* p, s32* x0, s32* x1, s32* y0, s32* y1) {
    const u32* w = (const u32*)(p + 1);
    s32 len = (s32)(*p >> 56);
    u32 op = w[0] >> 24;
    s32 n, i, at;
    s32 xs[4], ys[4];

    if (op >= 0x20 && op < 0x40) { /* polygon */
        n = op & 8 ? 4 : 3;
        at = 1;
        for (i = 0; i < n; i++) {
            if ((op & 0x10) && i > 0) {
                at++; /* the vertex's colour */
            }
            if (at >= len) {
                return 0;
            }
            xs[i] = (s32)(w[at] << 21) >> 21;
            ys[i] = (s32)((w[at] >> 16) << 21) >> 21;
            at += op & 4 ? 2 : 1;
        }
    } else if (op >= 0x40 && op < 0x60) { /* line: the first segment */
        s32 step = op & 0x10 ? 2 : 1;
        n = 2;
        if (1 + step >= len) {
            return 0;
        }
        for (i = 0; i < 2; i++) {
            at = 1 + i * step;
            xs[i] = (s32)(w[at] << 21) >> 21;
            ys[i] = (s32)((w[at] >> 16) << 21) >> 21;
        }
    } else if (op >= 0x60 && op < 0x80) { /* rectangle */
        s32 size = (op >> 3) & 3;
        s32 wd, ht;
        at = op & 4 ? 3 : 2;
        if (size == 0) {
            if (at >= len) {
                return 0;
            }
            wd = w[at] & 0xFFFF;
            ht = w[at] >> 16;
        } else {
            wd = ht = size == 1 ? 1 : size == 2 ? 8 : 16;
        }
        n = 2;
        xs[0] = (s32)(w[1] << 21) >> 21;
        ys[0] = (s32)((w[1] >> 16) << 21) >> 21;
        xs[1] = xs[0] + wd - 1;
        ys[1] = ys[0] + ht - 1;
    } else {
        return 0;
    }
    *x0 = *x1 = xs[0];
    *y0 = *y1 = ys[0];
    for (i = 1; i < n; i++) {
        *x0 = xs[i] < *x0 ? xs[i] : *x0;
        *x1 = xs[i] > *x1 ? xs[i] : *x1;
        *y0 = ys[i] < *y0 ? ys[i] : *y0;
        *y1 = ys[i] > *y1 ? ys[i] : *y1;
    }
    return 1;
}

static s32 mwPartFor(const void* packet) {
    const u8* a = packet;
    s32 kind = MW_OTHER;
    s32 index = 0;
    s32 i, k;

    for (i = 0; i < mwRangeCount; i++) {
        if (a >= mwRanges[i].lo && a < mwRanges[i].hi) {
            kind = mwRanges[i].kind;
            index = mwRanges[i].index;
            break;
        }
    }
    for (k = 0; k < mwPartCount; k++) {
        if (mwParts[k].kind == kind && mwParts[k].index == index) {
            return k;
        }
    }
    if (mwPartCount >= MW_PARTS) {
        return -1;
    }
    k = mwPartCount++;
    mwParts[k].kind = kind;
    mwParts[k].index = index;
    mwParts[k].count = 0;
    mwParts[k].x0 = mwParts[k].y0 = 0x7FFF;
    mwParts[k].x1 = mwParts[k].y1 = -0x8000;
    mwParts[k].dx = 0;
    return k;
}

static void mwLogParts(void) {
    u32 sig = 2166136261u;
    s32 k;

    for (k = 0; k < mwPartCount; k++) {
        sig = (sig ^ (u32)mwParts[k].kind) * 16777619u;
        sig = (sig ^ (u32)mwParts[k].index) * 16777619u;
        sig = (sig ^ (u32)(mwParts[k].dx & 0xFFFF)) * 16777619u;
    }
    if (sig == mwLastSignature) {
        return;
    }
    mwLastSignature = sig;
    printf("menuwide %u: menu %d, view %d, %d parts\n", hwVSyncCount(), menuToEnter, pcWideView, mwPartCount);
    for (k = 0; k < mwPartCount; k++) {
        printf("  %-12s %2d  n=%-4d x %4d..%-4d y %4d..%-4d dx %d\n", mwNames[mwParts[k].kind], mwParts[k].index,
               mwParts[k].count, mwParts[k].x0, mwParts[k].x1, mwParts[k].y0, mwParts[k].y1, mwParts[k].dx);
    }
}

/* The layout: each part's offset in the wide picture. */
static void mwDecide(void) {
    s32 l = pcWideView;
    s32 k;

    for (k = 0; k < mwPartCount; k++) {
        MwPart* part = &mwParts[k];
        s32 left = 0, right = 0;

        switch (menuToEnter) {
        case 0: /* the main menu */
            left = part->kind == MW_MAINMENU || part->kind == MW_SUBMENU || part->kind == MW_CURSOR ||
                   part->kind == MW_CURSORBOX || part->kind == MW_MAINLABELS || part->kind == MW_SAVELABELS;
            right = part->kind == MW_GOLD || part->kind == MW_TIME || (part->kind == MW_BOX && part->index < 2);
            break;
        case 3: /* name entry */
            left = part->kind == MW_NAMEBOX || part->kind == MW_MESSAGE || (part->kind == MW_BOX && part->index == 3);
            right = part->kind == MW_NAMEBUTTONS || part->kind == MW_LABELS;
            break;
        }
        part->dx = left ? -l : right ? l : 0;
    }
    /* A label drawn inside the selection box (JP: the help label of the
     * entry the cursor is on) goes with it. */
    for (k = 0; k < mwPartCount; k++) {
        MwPart* box = &mwParts[k];
        s32 j;
        if (box->kind != MW_CURSORBOX || box->dx == 0) {
            continue;
        }
        for (j = 0; j < mwPartCount; j++) {
            MwPart* part = &mwParts[j];
            s32 cx = (part->x0 + part->x1) / 2, cy = (part->y0 + part->y1) / 2;
            if (part->dx == 0 && part->kind >= MW_LABELS && cx >= box->x0 && cx <= box->x1 && cy >= box->y0 &&
                cy <= box->y1) {
                part->dx = box->dx;
            }
        }
    }
}

void menuWideLayout(OTag* head) {
    const OTag* p;
    s32 guard = 0;
    s32 i, k, x0, x1, y0, y1;

    if (mwLog < 0) {
        const char* s = portOption("XG_MENU_WIDE_LOG");
        mwLog = s != NULL && s[0] != '0';
    }
    if (pcWideView == 0 && !mwLog) {
        return;
    }
    mwBuildRanges();
    mwPartCount = 0;
    mwPacketCount = 0;
    for (p = head; p != NULL && guard++ < 100000;) {
        OTag tag = *p;
        if ((tag >> 56) != 0 && mwExtent(p, &x0, &x1, &y0, &y1)) {
            k = mwPartFor(p);
            if (k >= 0) {
                MwPart* part = &mwParts[k];
                part->count++;
                part->x0 = x0 < part->x0 ? x0 : part->x0;
                part->x1 = x1 > part->x1 ? x1 : part->x1;
                part->y0 = y0 < part->y0 ? y0 : part->y0;
                part->y1 = y1 > part->y1 ? y1 : part->y1;
                if (mwPacketCount < MW_PACKETS) {
                    mwPackets[mwPacketCount] = (void*)p;
                    mwPacketPart[mwPacketCount] = k;
                    mwPacketCount++;
                }
            }
        }
        if ((tag & OT_LINK_MASK) == OT_LINK_MASK) {
            break;
        }
        p = (const OTag*)OT_NEXT(tag);
    }
    mwDecide();
    if (mwLog) {
        mwLogParts();
    }
    if (pcWideView == 0) {
        return;
    }
    for (i = 0; i < mwPacketCount; i++) {
        if (mwParts[mwPacketPart[i]].dx != 0) {
            pcWidePrim(mwPackets[i], mwParts[mwPacketPart[i]].dx, 0);
        }
    }
}
