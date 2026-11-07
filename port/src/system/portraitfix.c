/* Independently identified pixel defects in the retail dialogue TIMs.
 * File IDs are in directory 04; US/JP and both discs contain identical TIMs.
 * No replacement artwork: fill 24 black texels and clear four stray texels. */
#include <stdint.h>
#include "pc_gameplay.h"

typedef struct {
    uint64_t fingerprint;
    unsigned char file, paletteIndex, count;
    unsigned short color;
    unsigned short pixels[5]; /* y * 64 + x in the original 64x64 texture */
} PortraitFix;

static const PortraitFix fixes[] = {
    {UINT64_C(0x72f088a99b0dac7d), 0x5f,   0, 3, 0x8000, {1500, 1501, 1567}}, /* Adult Emeralda: eyelash */
    {UINT64_C(0x44a7d1f21751900e), 0x63,   0, 1, 0x8000, {1442}},             /* Id: eyelash */
    {UINT64_C(0xce49c18cabd754ca), 0x70, 255, 4, 0, {2942, 3006, 3070, 3134}}, /* Miang: detached strip */
    {UINT64_C(0xb5c54ff4a401eb29), 0x81,   0, 5, 0x8000, {3088, 3154, 3165, 3235, 3302}}, /* Vanderkaum: beard/suit */
    {UINT64_C(0x30c93e6109f2a029), 0x82,   0, 2, 0x8000, {2774, 2840}},       /* Shakhan: jaw */
    {UINT64_C(0x89e33485c0ffaa16), 0x91,   0, 3, 0x8000, {1301, 1302, 1303}}, /* Franz: eye */
    {UINT64_C(0xc754b7a741dce7d8), 0x93, 189, 3, 0x8000, {1495, 1496, 1561}}, /* Yui: eye */
    {UINT64_C(0x6ce943d009be587c), 0x94,   0, 1, 0x8000, {1692}},             /* Midori: pupil */
    {UINT64_C(0x0c22611f4c0a3f1c), 0x95,  60, 1, 0x8000, {3231}},             /* Chief Lee: chin */
    {UINT64_C(0x15d992bd56c99899), 0x97,  39, 4, 0x8000, {1497, 1498, 1579, 1626}}, /* Alice: eyes */
    {UINT64_C(0xc47dd53c72e8d341), 0x99,   0, 1, 0x8000, {1520}},             /* Timothy: eye */
};

void pcGameplayPortraitTim(void* tim, unsigned int bytes, unsigned int file) {
    unsigned char* data = tim;
    /* The bounds and whole-file fingerprint also protect modified assets and
     * make repeated calls harmless. These retail TIMs are exactly 4640 bytes:
     * 256-entry CLUT at 20, then 4096 eight-bit texels at 544. */
    if (!data || bytes != 4640 || !pcGameplayOriginalBugFixes()) return;
    for (unsigned int i = 0; i < sizeof(fixes) / sizeof(fixes[0]); ++i) {
        const PortraitFix* fix = &fixes[i];
        if (file != fix->file) continue;
        uint64_t hash = UINT64_C(14695981039346656037);
        for (unsigned int j = 0; j < bytes; ++j)
            hash = (hash ^ data[j]) * UINT64_C(1099511628211);
        if (hash != fix->fingerprint) return;
        /* Black repairs use an unused CLUT entry; Miang uses its existing
         * transparent entry unchanged. No other texel changes colour/alpha.
         * 0x0000 is transparent; 0x8000 is black in the opaque portrait quad. */
        data[20 + fix->paletteIndex * 2] = fix->color & 0xff;
        data[21 + fix->paletteIndex * 2] = fix->color >> 8;
        for (unsigned int j = 0; j < fix->count; ++j)
            data[544 + fix->pixels[j]] = fix->paletteIndex;
        return;
    }
}
