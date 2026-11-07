#ifndef MENU_SHOP_H
#define MENU_SHOP_H

/* Shop menus (menu_shop 10/09, menu_gear_shop 10/0A): contexts hung off
 * gMenuContext->m450 / m454 / m458. Offset-tagged; "len/odd" pairs are the
 * poly count and the display buffer they were built for (func_801C94CC draws
 * them). Offsets are US: in JP m5A0_textPolys has 12 entries instead of 22,
 * so everything from mC80 on is 0x320 lower. */

#include "menu/menu.h"

void SetLineF2(void* p); /* libgpu */

#ifndef setXY3
#define setXY3(p, _x0, _y0, _x1, _y1, _x2, _y2)                                   \
    ((p)->x0 = (_x0), (p)->y0 = (_y0), (p)->x1 = (_x1), (p)->y1 = (_y1), (p)->x2 = (_x2), \
     (p)->y2 = (_y2))
#endif

/* gMenuContext->m450_shop, allocated by func_801C5600 (US 0x4788 bytes, JP 0x4468) */
typedef struct MenuShop {
    /* 0x0000 */ POLY_FT4 m0_polys[9][2];       /* m46A5 / m46A6 (func_801D0220: one glyph per entry) */
    /* 0x02D0 */ POLY_FT4 m2D0_polys[9][2];     /* m46A9 / m46A8 */
#if VERSION_JP
    /* 0x05A0 */ POLY_FT4 m5A0_textPolys[12][2];
#else
    /* 0x05A0 */ POLY_FT4 m5A0_textPolys[22][2]; /* m46AB / m46AA */
#endif
    /* 0x0C80 */ POLY_FT4 mC80_number1[9][2];   /* m46AD / m46AC (func_801D06D8) */
    /* 0x0F50 */ POLY_FT4 mF50_number2[9][2];   /* m46AF / m46AE */
    /* 0x1220 */ POLY_FT4 m1220_number[9][2];   /* m46B4 / m46B3 (func_801D0C20) */
    /* 0x14F0 */ POLY_FT4 m14F0_number4[9][2];  /* m46B8 / m46B7, shown if m46B9 */
    /* 0x17C0 */ POLY_FT4 m17C0_polys[10][2];   /* m46BB / m46BA */
    /* 0x1AE0 */ POLY_FT4 m1AE0_number3[9][2];  /* m46B1 / m46B0 */
    /* 0x1DB0 */ POLY_FT4 m1DB0_rows[8][4][2];  /* m468C[i] / m4694[i] */
    /* 0x27B0 */ POLY_FT4 m27B0_attackChange[9][3][2];       /* m46BC[i] / m46CE[i] */
    /* 0x3020 */ POLY_FT4 m3020_defenceChange[9][3][2];       /* m46C5[i] / m46D7[i] */
    /* 0x3890 */ LINE_F3 m3890_lines[9][2];     /* drawn if m469C[i] */
    /* 0x3A40 */ LINE_F3 m3A40_lines[9][2];
    /* 0x3BF0 */ LINE_F2 m3BF0_line[2];         /* drawn with mC80.. if m46B2 */
    /* 0x3C10 */ u8 m3C10[0x3C30 - 0x3C10];
    /* 0x3C30 */ MenuContext_4E0 m3C30_labels[8]; /* drawn if m4684[i] */
    /* 0x4030 */ MenuContext_4E0 m4030_labels[8];
    /* 0x4430 */ MenuContext_4E0 m4430_label;   /* drawn if m46A7 */
    /* 0x44B0 */ MenuContext_4E0 m44B0_label;   /* drawn if m46B5 */
    /* 0x4530 */ MenuContext_4E0 m4530_label;   /* drawn if m46B6 */
    /* 0x45B0 */ MenuContext_4E0 m45B0_label;   /* drawn if m4785 */
    /* 0x4630 */ u8* m4630_textBundles[9];  /* name banks (getDialogParamPointer): [0] usable items, [1] weapons,
                                            * [2] accessories; gear shop: [1] weapons,
                                            * [2] accessories, [3] armor, [4] frames, [5] engines (buy list),
                                            * [6] armor, [7] frames, [8] engines (owned part) (MF3) */
    /* 0x4654 */ u8 m4654_buyQuantities[0x30]; /* per gMenuContext->m1E30_shopItemIds entry: how many are being bought */
    /* 0x4684 */ u8 m4684_labelEnabled[8];
    /* 0x468C */ u8 m468C_rowLength[8];
    /* 0x4694 */ u8 m4694_rowOddOrEven[8];
    /* 0x469C */ u8 m469C_lineEnabled[9];
    /* 0x46A5 */ u8 m46A5_0Length;
    /* 0x46A6 */ u8 m46A6_0OddOrEven;
    /* 0x46A7 */ u8 m46A7_4430Enabled;
    /* 0x46A8 */ u8 m46A8_2D0OddOrEven;
    /* 0x46A9 */ u8 m46A9_2D0Length;
    /* 0x46AA */ u8 m46AA_textOddOrEven;
    /* 0x46AB */ u8 m46AB_textLength;
    /* 0x46AC */ u8 m46AC_C80OddOrEven;
    /* 0x46AD */ u8 m46AD_C80Length;
    /* 0x46AE */ u8 m46AE_F50OddOrEven;
    /* 0x46AF */ u8 m46AF_F50Length;
    /* 0x46B0 */ u8 m46B0_1AE0OddOrEven;
    /* 0x46B1 */ u8 m46B1_1AE0Length;
    /* 0x46B2 */ u8 m46B2_numbersEnabled;
    /* 0x46B3 */ u8 m46B3_1220OddOrEven;
    /* 0x46B4 */ u8 m46B4_1220Length;
    /* 0x46B5 */ u8 m46B5_44B0Enabled;
    /* 0x46B6 */ u8 m46B6_4530Enabled;
    /* 0x46B7 */ u8 m46B7_14F0OddOrEven;
    /* 0x46B8 */ u8 m46B8_14F0Length;
    /* 0x46B9 */ u8 m46B9_14F0Enabled;
    /* 0x46BA */ u8 m46BA_17C0OddOrEven;
    /* 0x46BB */ u8 m46BB_17C0Length;
    /* 0x46BC */ u8 m46BC_27B0Length[9];
    /* 0x46C5 */ u8 m46C5_3020Length[9];
    /* 0x46CE */ u8 m46CE_27B0OddOrEven[9];
    /* 0x46D7 */ u8 m46D7_3020OddOrEven[9];
    /* 0x46E0 */ u16 m46E0_attack[16];  /* gear shop: per party member, gear attack before a purchase (MF3) */
    /* 0x4700 */ u16 m4700_defence[16]; /* gear shop: per party member, gear defence (MF3) */
    /* 0x4720 */ u8 m4720_stock[5 * 20]; /* gear shop: [kind * 20 + i] item ids on sale (MF3) */
    /* 0x4784 */ u8 m4784;
    /* 0x4785 */ u8 m4785_45B0Enabled;
    /* 0x4786 */ u8 m4786[2];
} MenuShop;

/* gMenuContext->m454_gearShop, allocated by func_801C5664 (0x1F00 bytes) */
typedef struct MenuGearShop {
    /* 0x0000 */ POLY_FT4 m0_poly[2];         /* 96x13 text strip at (0x10, 0x20) (func_801C6E74) */
    /* 0x0050 */ u8 m50[0x80 - 0x50];
    /* 0x0080 */ POLY_FT4 m80_polys[2];      /* glyph 0x166 at (0x108, 0x18) (func_801C6E74), drawn with m1EE1 */
    /* 0x00D0 */ POLY_FT4 mD0_blinkPolys[3][4];  /* m1ED0_length[0] / m1ED4_oddOrEven[0] (func_801CF184) */
    /* 0x02B0 */ POLY_FT4 m2B0_sparkles[2 * 11 * 2];      /* [slot * 22 + n * 2 + odd]: m1ED0_length[1 + slot] / m1ED4_oddOrEven[1 + slot] */
    /* 0x0990 */ u8 m990[0xD00 - 0x990];
    /* 0x0D00 */ POLY_FT4 mD00_spark[18][2];           /* m1ED0_length[3] / m1ED4_oddOrEven[3] */
    /* 0x12A0 */ POLY_FT4 m12A0_statLabels[14][2];          /* 14 polys, m1EED_oddOrEven */
    /* 0x1700 */ POLY_FT4 m1700_statDigits[5][5][2];        /* m1EE8_length[i] / m1EED_oddOrEven */
    /* 0x1ED0 */ u8 m1ED0_length[4];     /* [0]: blink glyphs (func_801CF184) */
    /* 0x1ED4 */ u8 m1ED4_oddOrEven[4];
    /* 0x1ED8 */ u8 m1ED8_mode[4];       /* [0]: blink enabled; func_801CFF18 sets [1..3] = 3 */
    /* 0x1EDC */ u8 m1EDC_timer[4];
    /* 0x1EE0 */ u8 m1EE0_blinkFrame;
    /* 0x1EE1 */ u8 m1EE1_80OddOrEven;
    /* 0x1EE2 */ s16 m1EE2_frame;       /* func_801CEEA8: glyph 0x172 + frame */
    /* 0x1EE4 */ s16 m1EE4_frame[2];    /* func_801CEA68 slots 1-2 */
    /* 0x1EE8 */ u8 m1EE8_length[5];
    /* 0x1EED */ u8 m1EED_oddOrEven;
    /* 0x1EEE */ u8 m1EEE[2];
    /* 0x1EF0 */ u16 m1EF0_blinkX;
    /* 0x1EF2 */ u16 m1EF2_blinkY;
    /* 0x1EF4 */ u16 m1EF4_x[2];        /* func_801CEA68 slots 1-2 */
    /* 0x1EF8 */ u16 m1EF8_y[2];
    /* 0x1EFC */ u16 m1EFC_sparkX;
    /* 0x1EFE */ u16 m1EFE_sparkY;
} MenuGearShop;

/* gMenuContext->m458_files[i] (0x14 bytes each, func_801D5D38): a gear's two
 * model files loaded by func_801CF9BC */
typedef struct MenuShopFiles {
    /* 0x00 */ void* m0_mechaData2;
    /* 0x04 */ void* m4_mechaData1;
    /* 0x08 */ SVECTOR m8_position; /* mechaInitNewMecha position */
    /* 0x10 */ u8 m10[2];
    /* 0x12 */ u8 m12_loaded;      /* 1 once the model is set up (func_801CFAB8) */
    /* 0x13 */ u8 m13;
} MenuShopFiles;

/* gMenuContext->m1E20 (shop, gear shop, menu_name_entry; allocated 0xDEC bytes by
 * menu_name_entry func_801C52B4). menu_name_entry is the name entry screen: a grid of 36
 * five-character strings, the name being typed, a cursor. MF6 view; names
 * follow MF5's view of the same struct. mD20/mD50 are LINE_F3
 * (func_801CA558 calls SetLineF3 on them); func_801CA558 initialises
 * m190_grid and mCD0 as one run of 74 POLY_FT4 (mCD0 = m190_grid[72..73]). */
typedef struct MenuContext_1E20 {
    /* 0x000 */ POLY_FT4 m0_buttonIcon1[2];             /* glyph 0xF9 (func_801CADC8) */
    /* 0x050 */ POLY_FT4 m50_buttonIcon2[2];            /* glyph 0xFC */
    /* 0x0A0 */ POLY_FT4 mA0_label[2 * 2];        /* glyph 0xF0, mDEA of them */
    /* 0x140 */ POLY_FT4 m140_portrait[2];           /* glyph 0x14B, transformed from mDA0 */
    /* 0x190 */ POLY_FT4 m190_grid[36 * 2]; /* [i * 2 + oddOrEven]: the 36 strings */
    /* 0xCD0 */ POLY_FT4 mCD0_typedName[2];           /* the typed name (VRAM 0x180,0xEA), transformed from mDC0 */
    /* 0xD20 */ LINE_F3 mD20_gridCursorTopRight[2];            /* grid cursor (func_801CB33C) */
    /* 0xD50 */ LINE_F3 mD50_gridCursorLeftBottom[2];
    /* 0xD80 */ LINE_F2 mD80_cursor[2];
    /* 0xDA0 */ SVECTOR mDA0_portraitVerts[4];
    /* 0xDC0 */ SVECTOR mDC0_typedNameVerts[4];
    /* 0xDE0 */ u8 mDE0_oddOrEven;          /* m0, m50, mA0 */
    /* 0xDE1 */ u8 mDE1_oddOrEven;          /* m140 */
    /* 0xDE2 */ u8 mDE2_oddOrEven;          /* m190_grid */
    /* 0xDE3 */ u8 mDE3_oddOrEven;          /* mCD0 */
    /* 0xDE4 */ u8 mDE4_oddOrEven;          /* mD20, mD50 */
    /* 0xDE5 */ u8 mDE5_drawQuads;          /* m140, mCD0 */
    /* 0xDE6 */ u8 mDE6_draw;
    /* 0xDE7 */ u8 mDE7_blinkTimer;
    /* 0xDE8 */ u8 mDE8_cursorPos;          /* next character of the typed name */
    /* 0xDE9 */ u8 mDE9_nameLength;         /* maximum name length (mCD0 width = mDE9 * glyph width) */
    /* 0xDEA */ u8 mDEA_numA0;
    /* 0xDEB */ u8 mDEB;
} MenuContext_1E20; /* 0xDEC */

/* One shop's stock (gMenuContext->m1E2C[g_menuOpenArg]): three categories
 * of 30 item slots (0: empty) (MF4); size 0x5C */
typedef struct MenuShopList {
    /* 0x00 */ u8 m0_items[90];
    /* 0x5A */ u8 m5A[2];
} MenuShopList;

/* The shop overlays' own copies of menu library functions that some of
 * their files call without prototypes (unmasked arguments and results): the
 * PC sees the definitions' prototypes here, those files keep their K&R
 * declarations under #ifndef PLATFORM_PC. */
u8 askYesNoQuestion(u8 arg0, u8 arg1, u8 arg2);
u16 menu2CheckBitmask(u16 mask, u8 bit);
u16 getGearsEquippingPart(u8 item, u8 category); /* gear shop */

#endif /* MENU_SHOP_H */
