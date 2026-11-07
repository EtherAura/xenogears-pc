#ifndef MAIN_DEBUG_FONT_H
#define MAIN_DEBUG_FONT_H

/* On-screen debug text (Noah: debugFont; src/main/debug_font.c). */

#include "common.h"
#include "psyq.h"

struct DebugFont;

void initDebugFontClut(u16 fg, u16 bg);
void logToScreen(const char* fmt, ...);
void sprintf_screen(const char* fmt, ...); /* hand-written: a jump to logToScreen */
struct DebugFont* initDebugFont(s32 x, s32 y, s32 w, s32 h, s32 bufferSize, u32 flags, s32 tpageX, s32 tpageY,
                                s32 clutX, s32 clutY, u8* font);
void setDebugFontBuffer(sptr v);           /* nonzero: initDebugFont uses this memory */
void printDebugTextOnScreen(OTag* ot);

#endif /* MAIN_DEBUG_FONT_H */
