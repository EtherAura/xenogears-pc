// The native menu's perspective entry/exit and centred window expansion.
#ifndef PORT_UI_MOTION_H
#define PORT_UI_MOTION_H

#include "imgui.h"

namespace menuMotion {
// updateMenuTransforms (menu overlay): a sub-menu comes in (state 3, then 1)
// over 33 frames: frame 0 at z 0x800 not turned yet, then rx += 0x7C and
// z -= 0x30 each frame, until z would pass 0x200 (frame 33 is at rest).
constexpr unsigned OPENING_FRAMES = 33;
// It goes (state 4, then 2): frame 0 still at rest, then ry -= 0x60 and
// z += 0x40 each frame. The game shows frames 0 to 21 of it (Beetle, the
// Items box after a Circle press: turned from picture 2503 to 2523, gone on
// 2524 when the main menu is back), not the whole turn to z 0xE00 (48).
constexpr unsigned LEAVING_FRAMES = 22;
struct View {
    ImVec2 center;
    float focal, depth, sine, cosine, sx = 1, sy = 1;
    ImVec2 project(ImVec2 p) const;
    bool inverse(ImVec2 p, ImVec2* out) const;
};

// center: the projection's centre, the game screen's (160, 112) (the menu's
// SetGeomOffset(0xA0, 0x70), SetGeomScreen(0x200)); unit: window pixels per
// game pixel; frame: from 0.
View opening(ImVec2 center, float unit, unsigned frame);
View leaving(ImVec2 center, float unit, unsigned frame);
View expanding(ImVec2 center, ImVec2 size, float unit, unsigned frame);
// Clip text/sprite quads before projecting: a rectangular GPU scissor alone would
// let text outside a scrolling pane spill across its tilted edges.
void drawList(ImDrawList* list, const View& view, bool yaw = false);
} // namespace menuMotion
#endif
