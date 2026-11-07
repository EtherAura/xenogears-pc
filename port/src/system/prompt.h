#ifndef PORT_PROMPT_H
#define PORT_PROMPT_H

/* Face symbols use the game's dialogue remap-slot order. Other symbols are
 * small, original keycaps shared by the host UI and the native renderer. */
enum {
    PAD_GLYPH_NONE = -1,
    PAD_GLYPH_CIRCLE, PAD_GLYPH_CROSS, PAD_GLYPH_TRIANGLE, PAD_GLYPH_SQUARE,
    PAD_GLYPH_A, PAD_GLYPH_B, PAD_GLYPH_X, PAD_GLYPH_Y,
    PAD_GLYPH_L1, PAD_GLYPH_R1, PAD_GLYPH_L2, PAD_GLYPH_R2,
    PAD_GLYPH_LB, PAD_GLYPH_RB, PAD_GLYPH_LT, PAD_GLYPH_RT,
    PAD_GLYPH_L, PAD_GLYPH_R, PAD_GLYPH_ZL, PAD_GLYPH_ZR,
    PAD_GLYPH_L3, PAD_GLYPH_R3, PAD_GLYPH_START, PAD_GLYPH_SELECT,
    PAD_GLYPH_BACK, PAD_GLYPH_MENU, PAD_GLYPH_OPTIONS, PAD_GLYPH_SHARE,
    PAD_GLYPH_CREATE, PAD_GLYPH_PLUS, PAD_GLYPH_MINUS,
    PAD_GLYPH_UP, PAD_GLYPH_DOWN, PAD_GLYPH_LEFT, PAD_GLYPH_RIGHT,
    PAD_GLYPH_SOUTH, PAD_GLYPH_EAST, PAD_GLYPH_WEST, PAD_GLYPH_NORTH,
    PAD_GLYPH_UNBOUND, PAD_GLYPH_OTHER,
    PAD_GLYPH_VIEW, PAD_GLYPH_GUIDE, PAD_GLYPH_TOUCHPAD,
    PAD_GLYPH_P1, PAD_GLYPH_P2, PAD_GLYPH_P3, PAD_GLYPH_P4,
    PAD_GLYPH_M1, PAD_GLYPH_M2, PAD_GLYPH_M3, PAD_GLYPH_M4, PAD_GLYPH_M5, PAD_GLYPH_M6,
    PAD_GLYPH_MICROPHONE, PAD_GLYPH_CAPTURE,
    PAD_GLYPH_COUNT
};

int padInputGlyph(int input); /* selected controller profile */
int padActionGlyph(const char* button); /* same active controller as padActionLabel */
int pcPadPromptGlyph(int originalSlot); /* retail symbol -> region -> physical binding */
int pcPadDialogSlot(int originalSlot); /* native PS1 string slot, or -1 for a keycap */
unsigned char* pcPadDialogText(int originalSlot);
void pcPadPrintGlyph(unsigned short* out, int stride, int odd, int glyph);

#endif
