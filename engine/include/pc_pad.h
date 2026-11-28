#ifndef PC_PAD_H
#define PC_PAD_H
int pcPadPromptSlot(int slot);
void pcPadAttackPositions(int positions[4]); /* right, bottom, left, top */
int pcPadDialogSlot(int slot);
unsigned char* pcPadDialogText(int slot);
int pcPadIsGlyph(unsigned type, unsigned id);
void pcPadPrintGlyph(unsigned short* out, int stride, int odd, int glyph);
#endif
