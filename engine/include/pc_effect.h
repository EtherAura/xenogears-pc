#ifndef PC_EFFECT_H
#define PC_EFFECT_H

/* Display-only counterparts of identified CPU colour effects. Native VRAM,
 * the game's RAM copy and its timing remain authoritative. */
void pcHiresBattlingCapture(const void* rect);
void pcHiresBattlingBlur(const void* rect);

#endif
