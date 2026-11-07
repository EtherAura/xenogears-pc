#include "texture.h"
#include "system/prompt.h"

/* Standalone GPU tests have no input layer. */
__attribute__((weak)) int pcPadPromptGlyph(int slot) { return slot; }

typedef struct { uint64_t key; int slot, style, tpage, u, v; } Button;
/* Index-image identities, verified from both local retail disc resources:
 * 10/01 menu, 0C/03 battle. No image or palette bytes are embedded. */
static const Button buttons[] = {
    /* Filled from the verified asset facts below. */
#include "prompt_assets.inc"
};

int texturePromptRemap(TextureRequest from, TextureRequest* to) {
    if (from.window || from.h != 12 || (from.w != 12 && from.w != 11)) return 0;
    if (pcPadPromptGlyph(0)==0 && pcPadPromptGlyph(1)==1 && pcPadPromptGlyph(2)==2 && pcPadPromptGlyph(3)==3) return 0;
    int depth = (from.tpage >> 7) & 3;
    if (depth > 1) return 0;
    const DecodedTexture* texture = textureCacheGet(from);
    if (!texture) return 0;
    uint64_t key = textureGlyphKey(texture->texels,from.w,from.h,depth);
    const Button* button = NULL;
    for (unsigned i=0;i<sizeof(buttons)/sizeof(*buttons);++i)
        if (buttons[i].key == key && buttons[i].tpage == (from.tpage & ~0x60) &&
            buttons[i].u == from.u && buttons[i].v == from.v) { button=&buttons[i]; break; }
    if (!button) return 0;
    int glyph = pcPadPromptGlyph(button->slot);
    if (glyph == button->slot) return 0;
    *to = from;
    if (glyph >= 4 && glyph < PAD_GLYPH_COUNT) {
        /* An internal texture type, never written to GP0 or native VRAM. */
        to->tpage = 0x8180 | (from.tpage & 0x60);
        to->clut = glyph;
        to->u = to->v = 0;
        to->w = to->h = 12;
    } else if (glyph >= 0 && glyph < 4) {
        if (button->style == 0) {
            static const int u[] = {0,36,24,12};
            to->u=u[glyph];
        } else if (button->style == 3) {
            static const int u[] = {208,244,232,220};
            static const int clut[] = {31820,31823,31822,31821};
            to->u=u[glyph]; to->clut=from.clut + clut[glyph] - clut[button->slot];
            to->w=glyph==PAD_GLYPH_CROSS ? 11 : 12;
        } else {
            /* Tilted attack icons have only three faces. Their full native
             * button atlas supplies the missing fourth without moving UI. */
            static const int u[] = {80,116,104,92};
            to->tpage=(from.tpage & 0x60) | 143;
            to->clut=button->style==1 ? 32128 : from.clut;
            to->u=u[glyph]; to->v=140;
        }
    } else return 0;
    return 1;
}
