#ifndef PC_GLYPH_H
#define PC_GLYPH_H
/* Metadata beside the game's unchanged text images and atlas pieces. */
void pcGlyphDialog(const void* image, int stride, int width, int odd);
/* A proportional glyph's independent indices (width <= 14), at pixel x.
 * Neighbouring glyph outlines combine by OR, as the native line image does. */
void pcGlyphProportional(const void* image, int stride, int x, int width, int odd,
                         const unsigned char* indices);
void pcGlyphClearColumns(const void* image, int stride, int odd, int first, int last);
void pcGlyphClearRAM(const void* image, int bytes, unsigned planes);
void pcGlyphTile(unsigned tpage, int u, int v, int w, int h);
#endif
