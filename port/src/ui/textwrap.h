// Line breaking for the menus' Japanese (and other CJK) text (xg-em4y).
//
// ImGui's word wrap takes a run of characters without a space as one word: in
// Japanese, a whole sentence. It breaks such a run only where it overflows, so a
// line can begin with 。 or 、, and an ASCII word before Japanese ("GitHub に")
// can be left alone on its line. textWrapCut breaks as Japanese text does: between
// any two characters when either is CJK, at spaces as before, and never before a
// character that may not begin a line (、。」 small kana, ー ...) nor after one that
// may not end it (「（ ...), the common rules of JIS X 4051. A line holds at least
// one character, so text narrower than nothing still advances.
#pragma once

#include <cstddef>

// True when [text, end) holds a CJK character (the menus then use textWrapCut).
bool textHasCjk(const char* text, const char* end);

// Where the line starting at text ends: the first byte not on it (a space, which
// the caller skips, or the next line's first character), at most end; '\n' ends a
// line too. advance(c, ctx) is a character's width; width the line's.
const char* textWrapCut(const char* text, const char* end, float width, float (*advance)(unsigned c, void* ctx),
                        void* ctx);
