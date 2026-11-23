// ImGui::TextWrapped for text that may be Japanese (xg-em4y).
#pragma once

#include "imgui.h"

// ImGui::TextWrapped, the same for text without CJK; Japanese and other CJK text
// broken as textLines breaks it (textwrap.h: between characters, never with 。、」
// or small kana beginning a line, never 「（ ending one), in TextWrapped's spacing.
void wrappedText(const char* fmt, ...) IM_FMTARGS(1);
