// The touch pad's art (system/touch.h; xg-mrj.3.8): drawn in the present
// pass over the game (menu.cpp uiRecord, while the menu is closed) with
// ImGui's shapes. Plain figures of our own: dark translucent plates with a
// light rim, the buttons' marks drawn as lines, nothing taken from elsewhere.

#include "imgui.h"

#include <algorithm>
#include <float.h>
#include <math.h>

extern "C" {
#include "system/touch.h"
}

namespace menu {

namespace {

ImU32 rgba(int r, int g, int b, float a) {
    return IM_COL32(r, g, b, (int)(std::min(1.0f, std::max(0.0f, a)) * 255 + 0.5f));
}

void label(ImDrawList* d, ImVec2 c, float size, ImU32 ink, const char* text) {
    ImFont* font = ImGui::GetFont();
    ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0, text);
    d->AddText(font, size, ImVec2(floorf(c.x - extent.x / 2), floorf(c.y - extent.y / 2)), ink, text);
}

// A face button's mark, r the button's radius.
void mark(ImDrawList* d, int kind, ImVec2 c, float r, ImU32 ink, float t) {
    float s = r * 0.46f;
    switch (kind) {
    case TOUCH_TRIANGLE: {
        const float k = 0.866f; // cos 30
        d->AddTriangle(ImVec2(c.x, c.y - s), ImVec2(c.x + s * k, c.y + s * 0.5f), ImVec2(c.x - s * k, c.y + s * 0.5f),
                       ink, t);
        break;
    }
    case TOUCH_CIRCLE:
        d->AddCircle(c, s * 0.85f, ink, 0, t);
        break;
    case TOUCH_CROSS:
        d->AddLine(ImVec2(c.x - s * 0.75f, c.y - s * 0.75f), ImVec2(c.x + s * 0.75f, c.y + s * 0.75f), ink, t);
        d->AddLine(ImVec2(c.x - s * 0.75f, c.y + s * 0.75f), ImVec2(c.x + s * 0.75f, c.y - s * 0.75f), ink, t);
        break;
    case TOUCH_SQUARE:
        d->AddRect(ImVec2(c.x - s * 0.7f, c.y - s * 0.7f), ImVec2(c.x + s * 0.7f, c.y + s * 0.7f), ink, 0, 0, t);
        break;
    default:
        break;
    }
}

// The D-pad: a plus of four arms around a hub, each arm lit while held, an
// arrow at its end.
void dpad(ImDrawList* d, const TouchControl& c, ImU32 plate, ImU32 lit, ImU32 rim, ImU32 ink, ImU32 inkLit, float t) {
    float half = c.w / 2, arm = c.w * 0.18f; // an arm's half width
    ImVec2 o(c.x, c.y);
    // directions: up, right, down, left (the touch.h bits 1 2 4 8)
    const float dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
    d->AddRectFilled(ImVec2(o.x - arm, o.y - arm), ImVec2(o.x + arm, o.y + arm), plate);
    for (int i = 0; i < 4; i++) {
        ImVec2 a(o.x + dx[i] * arm - dy[i] * arm, o.y + dy[i] * arm - dx[i] * arm);
        ImVec2 b(o.x + dx[i] * half + dy[i] * arm, o.y + dy[i] * half + dx[i] * arm);
        ImVec2 lo(std::min(a.x, b.x), std::min(a.y, b.y)), hi(std::max(a.x, b.x), std::max(a.y, b.y));
        bool on = (c.pressed >> i) & 1;
        int corners = i == 0 ? ImDrawFlags_RoundCornersTop : i == 1 ? ImDrawFlags_RoundCornersRight
                    : i == 2 ? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersLeft;
        d->AddRectFilled(lo, hi, on ? lit : plate, arm * 0.35f, corners);
        // the arrow, pointing out
        float at = half * 0.66f, s = arm * 0.55f;
        ImVec2 tip(o.x + dx[i] * (at + s), o.y + dy[i] * (at + s));
        ImVec2 l(o.x + dx[i] * at - dy[i] * s, o.y + dy[i] * at - dx[i] * s);
        ImVec2 r(o.x + dx[i] * at + dy[i] * s, o.y + dy[i] * at + dx[i] * s);
        d->AddTriangleFilled(tip, l, r, on ? inkLit : ink);
    }
    // the plus's outline, twelve corners
    ImVec2 p[12] = {
        {o.x - arm, o.y - half}, {o.x + arm, o.y - half}, {o.x + arm, o.y - arm}, {o.x + half, o.y - arm},
        {o.x + half, o.y + arm}, {o.x + arm, o.y + arm},  {o.x + arm, o.y + half}, {o.x - arm, o.y + half},
        {o.x - arm, o.y + arm},  {o.x - half, o.y + arm}, {o.x - half, o.y - arm}, {o.x - arm, o.y - arm},
    };
    d->AddPolyline(p, 12, rim, ImDrawFlags_Closed, t);
}

} // namespace

void touchpadDraw(ImDrawList* d, float w, float h) {
    TouchControl cs[32];
    int n = touchControls(cs, 32, w, h);
    float a = touchOpacity();
    float u = std::min(w, h) / 100;
    float t = std::max(1.5f, 0.4f * u); // the lines' width
    ImU32 plate = rgba(14, 18, 28, 0.6f * a), lit = rgba(214, 224, 240, 0.7f * a);
    ImU32 rim = rgba(226, 232, 244, 0.85f * a), ink = rgba(232, 236, 246, 0.95f * a), inkLit = rgba(14, 18, 28, a);
    for (int i = 0; i < n; i++) {
        const TouchControl& c = cs[i];
        ImVec2 o(c.x, c.y), lo(c.x - c.w / 2, c.y - c.h / 2), hi(c.x + c.w / 2, c.y + c.h / 2);
        bool on = c.pressed != 0;
        switch (c.kind) {
        case TOUCH_DPAD:
            dpad(d, c, plate, lit, rim, ink, inkLit, t);
            break;
        case TOUCH_CROSS:
        case TOUCH_CIRCLE:
        case TOUCH_SQUARE:
        case TOUCH_TRIANGLE:
            d->AddCircleFilled(o, c.w / 2, on ? lit : plate);
            d->AddCircle(o, c.w / 2, rim, 0, t);
            mark(d, c.kind, o, c.w / 2, on ? inkLit : ink, t * 1.4f);
            break;
        case TOUCH_L1:
        case TOUCH_L2:
        case TOUCH_R1:
        case TOUCH_R2: {
            static const char* const names[] = {"L1", "L2", "R1", "R2"};
            d->AddRectFilled(lo, hi, on ? lit : plate, c.h * 0.3f);
            d->AddRect(lo, hi, rim, c.h * 0.3f, 0, t);
            label(d, o, c.h * 0.55f, on ? inkLit : ink, names[c.kind - TOUCH_L1]);
            break;
        }
        case TOUCH_SELECT:
        case TOUCH_START:
            d->AddRectFilled(lo, hi, on ? lit : plate, c.h / 2);
            d->AddRect(lo, hi, rim, c.h / 2, 0, t);
            label(d, o, c.h * 0.5f, on ? inkLit : ink, c.kind == TOUCH_START ? "START" : "SELECT");
            break;
        case TOUCH_MENU: {
            // three bars: the settings menu
            d->AddCircleFilled(o, c.w / 2, on ? lit : plate);
            d->AddCircle(o, c.w / 2, rim, 0, t);
            float bw = c.w * 0.22f;
            for (int k = -1; k <= 1; k++)
                d->AddLine(ImVec2(o.x - bw, o.y + k * c.w * 0.14f), ImVec2(o.x + bw, o.y + k * c.w * 0.14f),
                           on ? inkLit : ink, t * 1.3f);
            break;
        }
        case TOUCH_HOTKEYS: {
            // four squares: the hotkeys' row
            d->AddCircleFilled(o, c.w / 2, on ? lit : plate);
            d->AddCircle(o, c.w / 2, rim, 0, t);
            float s = c.w * 0.09f, g = c.w * 0.12f;
            for (int k = 0; k < 4; k++) {
                ImVec2 m(o.x + (k & 1 ? g : -g), o.y + (k & 2 ? g : -g));
                d->AddRectFilled(ImVec2(m.x - s, m.y - s), ImVec2(m.x + s, m.y + s), on ? inkLit : ink);
            }
            break;
        }
        case TOUCH_HOTKEY:
            d->AddRectFilled(lo, hi, on ? lit : plate, c.h * 0.25f);
            d->AddRect(lo, hi, rim, c.h * 0.25f, 0, t);
            label(d, o, c.h * 0.45f, on ? inkLit : ink, touchHotkeyLabel(c.hotkey));
            break;
        default:
            break;
        }
    }
}

} // namespace menu
