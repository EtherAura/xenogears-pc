/*
 * Palette fades (hand-written assembly at 0x80026F44 in the PS1 build;
 * config/handwritten.yaml). PC versions that drive the GTE the same way: the
 * PS1 code uses GPF (sf=1, lm=0: IRn = clamp((IR0 * IRn) >> 12)) as a
 * three-lane multiplier, with each 15-bit colour split into its fields in
 * place (red 0x1F, green 0x3E0, blue 0x7C00, not shifted down).
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"

#define GTE_IR0 8
#define GTE_IR1 9
#define GTE_IR2 10
#define GTE_IR3 11

/* Noah: fadePaletteColors. dst[i] = src[i] scaled by level/32 (level above 32
 * counts as 32), each field rounded down. A colour that fades to 0 (fully
 * transparent) but was not 0 becomes 1, and bit 15 (semi-transparency) is
 * kept. */
void fadePaletteColors(s32 count, s32 level, u16* dst, u16* src) {
    u32 c;
    u32 r;

    if (level >= 0x20) {
        level = 0x20;
    }
    gteMtc2(GTE_IR0, (u32)level << 7);
    while (--count != -1) {
        c = *src++;
        gteMtc2(GTE_IR1, c & 0x1F);
        gteMtc2(GTE_IR2, c & 0x3E0);
        gteMtc2(GTE_IR3, c & 0x7C00);
        gte_gpf12();
        r = (gteMfc2(GTE_IR1) & 0x1F) | (gteMfc2(GTE_IR2) & 0x3E0) | (gteMfc2(GTE_IR3) & 0x7C00);
        if (c != 0 && r == 0) {
            r |= 1;
        }
        *dst++ = (c & 0x8000) | r;
    }
}

/* Noah: lerpPaletteColors. dst[i] = from[i] + (to[i] - from[i]) * level/32 per
 * field (level above 32 counts as 32). The difference is masked back into
 * the field before the add, so a negative step wraps within the field and its
 * carry spills into the next one, as on the PS1. No bit 15 handling. */
void lerpPaletteColors(s32 count, s32 level, u16* dst, u16* from, u16* to) {
    u32 a;
    u32 b;
    u32 r;
    u32 g;

    if (level >= 0x21) {
        level = 0x20;
    }
    gteMtc2(GTE_IR0, (u32)level << 7);
    for (count++; --count != 0;) {
        b = *to++;
        a = *from++;
        gteMtc2(GTE_IR1, (b & 0x1F) - (a & 0x1F));
        gteMtc2(GTE_IR2, (b & 0x3E0) - (a & 0x3E0));
        gteMtc2(GTE_IR3, (b & 0x7C00) - (a & 0x7C00));
        gte_gpf12();
        r = (a & 0x1F) + (gteMfc2(GTE_IR1) & 0x1F);
        g = (a & 0x3E0) + (gteMfc2(GTE_IR2) & 0x3E0);
        *dst++ = r | g | ((a & 0x7C00) + (gteMfc2(GTE_IR3) & 0x7C00));
    }
}
