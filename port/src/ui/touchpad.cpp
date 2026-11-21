// The touch pad's art (system/touch.h; xg-mrj.3.8, xg-mrj.3.20): drawn in the
// present pass over the game (menu.cpp uiRecord, while the menu is closed;
// the Controls section shows it over the menu while its rows are changed)
// with ImGui's shapes. Figures of our own, nothing taken from elsewhere: each
// control a plate of dark glass, lighter toward its top, with a light rim and
// a soft shadow around it so the rim reads over a bright picture as over a
// dark one; its mark drawn in lines, the lettering (L1, START, the hotkeys'
// words: as a controller's print, the same in every language) a handful of
// strokes of our own too. A control held turns light, with a ring of light
// around it, and fades back when let go.

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <float.h>
#include <math.h>

extern "C" {
#include "system/touch.h"
}

namespace menu {

namespace {

const float PI = 3.14159265f;

struct Colour {
    float r, g, b;
};

const Colour GLASS_TOP = {62, 74, 110}, GLASS_BOTTOM = {12, 16, 30};
const Colour LIT_TOP = {250, 252, 255}, LIT_BOTTOM = {196, 210, 236};
const Colour RIM = {236, 241, 252}, INK = {242, 245, 252}, INK_LIT = {14, 18, 32};
const Colour GLOW = {150, 190, 255}, SHADOW = {0, 0, 0};

Colour mix(Colour a, Colour b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

ImU32 rgba(Colour c, float a) {
    return IM_COL32((int)(c.r + 0.5f), (int)(c.g + 0.5f), (int)(c.b + 0.5f),
                    (int)(std::min(1.0f, std::max(0.0f, a)) * 255 + 0.5f));
}

// A frame's measures: the pad's opacity (a the glass's; s the lines', which
// keep more of theirs as the pad is turned down, or a faint pad's marks are
// lost over a bright picture), a hundredth of the short side at the size the
// pad is laid out at, and the rims' width.
struct Pen {
    ImDrawList* d;
    float a, s, u, line;
};

typedef ImVector<ImVec2> Path;

void circlePath(Path& p, ImVec2 c, float r) {
    int n = std::min(96, std::max(24, (int)(r * 0.8f)));
    p.resize(0);
    for (int i = 0; i < n; i++) {
        float t = 2 * PI * i / n;
        p.push_back(ImVec2(c.x + cosf(t) * r, c.y + sinf(t) * r));
    }
}

// A polygon (its corners in order) with each corner rounded: r[i] along both
// of corner i's edges. A right angle gets a quarter circle, another a curve
// through the same two points.
void roundedPath(Path& p, const ImVec2* v, const float* r, int n) {
    p.resize(0);
    for (int i = 0; i < n; i++) {
        ImVec2 at = v[i], prev = v[(i + n - 1) % n], next = v[(i + 1) % n];
        ImVec2 d0(at.x - prev.x, at.y - prev.y), d1(next.x - at.x, next.y - at.y);
        float l0 = sqrtf(d0.x * d0.x + d0.y * d0.y), l1 = sqrtf(d1.x * d1.x + d1.y * d1.y);
        if (r[i] <= 0 || l0 <= 0 || l1 <= 0) {
            p.push_back(at);
            continue;
        }
        d0.x /= l0, d0.y /= l0, d1.x /= l1, d1.y /= l1;
        float k = std::min(r[i], std::min(l0, l1) / 2);
        ImVec2 t0(at.x - d0.x * k, at.y - d0.y * k), t1(at.x + d1.x * k, at.y + d1.y * k);
        int steps = std::min(16, std::max(3, (int)(k * 0.35f)));
        bool square = fabsf(d0.x * d1.x + d0.y * d1.y) < 0.01f;
        ImVec2 c(t0.x + d1.x * k, t0.y + d1.y * k); // the quarter circle's centre
        for (int s = 0; s <= steps; s++) {
            float f = (float)s / steps;
            if (square) {
                float a = f * PI / 2;
                p.push_back(ImVec2(c.x + (t0.x - c.x) * cosf(a) + (t1.x - c.x) * sinf(a),
                                   c.y + (t0.y - c.y) * cosf(a) + (t1.y - c.y) * sinf(a)));
            } else {
                float a = (1 - f) * (1 - f), b = 2 * f * (1 - f), e = f * f;
                p.push_back(ImVec2(t0.x * a + at.x * b + t1.x * e, t0.y * a + at.y * b + t1.y * e));
            }
        }
    }
}

void boxPath(Path& p, ImVec2 lo, ImVec2 hi, float r) {
    const ImVec2 v[4] = {lo, ImVec2(hi.x, lo.y), hi, ImVec2(lo.x, hi.y)};
    const float rs[4] = {r, r, r, r};
    roundedPath(p, v, rs, 4);
}

// A band around a closed path: col on the path, nothing `spread` out from it.
void halo(ImDrawList* d, const Path& p, float spread, ImU32 col) {
    int n = p.Size;
    if (n < 3 || spread <= 0 || (col & IM_COL32_A_MASK) == 0) return;
    float area = 0;
    for (int i = 0; i < n; i++) {
        const ImVec2& a = p[i];
        const ImVec2& b = p[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    float out = area > 0 ? 1.0f : -1.0f;
    ImVec2 uv = d->_Data->TexUvWhitePixel;
    ImU32 clear = col & ~IM_COL32_A_MASK;
    d->PrimReserve(n * 6, n * 2);
    unsigned base = d->_VtxCurrentIdx;
    for (int i = 0; i < n; i++) {
        ImVec2 at = p[i], prev = p[(i + n - 1) % n], next = p[(i + 1) % n];
        ImVec2 e0(at.x - prev.x, at.y - prev.y), e1(next.x - at.x, next.y - at.y);
        float l0 = sqrtf(e0.x * e0.x + e0.y * e0.y), l1 = sqrtf(e1.x * e1.x + e1.y * e1.y);
        ImVec2 n0(l0 > 0 ? e0.y / l0 * out : 0, l0 > 0 ? -e0.x / l0 * out : 0);
        ImVec2 n1(l1 > 0 ? e1.y / l1 * out : 0, l1 > 0 ? -e1.x / l1 * out : 0);
        ImVec2 m(n0.x + n1.x, n0.y + n1.y);
        float lm = sqrtf(m.x * m.x + m.y * m.y);
        if (lm > 0.001f) m.x /= lm, m.y /= lm;
        else m = n0;
        // as far out as the edges' own offset, not past twice it at a sharp corner
        float along = std::max(0.5f, m.x * n0.x + m.y * n0.y);
        d->PrimWriteVtx(at, uv, col);
        d->PrimWriteVtx(ImVec2(at.x + m.x * spread / along, at.y + m.y * spread / along), uv, clear);
    }
    for (int i = 0; i < n; i++) {
        unsigned a = base + 2 * i, b = base + 2 * ((i + 1) % n);
        d->PrimWriteIdx((ImDrawIdx)a);
        d->PrimWriteIdx((ImDrawIdx)(a + 1));
        d->PrimWriteIdx((ImDrawIdx)(b + 1));
        d->PrimWriteIdx((ImDrawIdx)a);
        d->PrimWriteIdx((ImDrawIdx)(b + 1));
        d->PrimWriteIdx((ImDrawIdx)b);
    }
}

// A path filled, top's colour at y0 down to bottom's at y1.
void fill(ImDrawList* d, const Path& p, bool concave, float y0, float y1, Colour top, Colour bottom, float a) {
    int first = d->VtxBuffer.Size;
    if (concave) d->AddConcavePolyFilled(p.Data, p.Size, rgba(top, a));
    else d->AddConvexPolyFilled(p.Data, p.Size, rgba(top, a));
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(d, first, d->VtxBuffer.Size, ImVec2(0, y0), ImVec2(0, y1),
                                                  rgba(top, 1), rgba(bottom, 1));
}

// A rim: a light line with a dark one under it, a little wider, so it shows
// over a light picture as over a dark one.
void rim(const Pen& pen, const Path& p, float a) {
    pen.d->AddPolyline(p.Data, p.Size, rgba(SHADOW, 0.5f * a * pen.s), ImDrawFlags_Closed, pen.line * 2.6f);
    pen.d->AddPolyline(p.Data, p.Size, rgba(RIM, a * pen.s), ImDrawFlags_Closed, pen.line);
}

// A control's plate: shadow, glass (light by `lit`, 0..1), rim.
void plate(const Pen& pen, const Path& p, bool concave, float y0, float y1, float lit) {
    halo(pen.d, p, 1.5f * pen.u, rgba(SHADOW, 0.42f * pen.a));
    if (lit > 0) halo(pen.d, p, (0.5f + 1.3f * lit) * pen.u, rgba(GLOW, 0.6f * pen.s * lit));
    float glass = 0.72f * pen.a;
    fill(pen.d, p, concave, y0, y1, mix(GLASS_TOP, LIT_TOP, lit), mix(GLASS_BOTTOM, LIT_BOTTOM, lit),
         glass + (0.92f * pen.s - glass) * lit);
    rim(pen, p, 0.85f + 0.15f * lit);
}

// A mark's colour: light on the glass, dark on a lit plate.
ImU32 ink(const Pen& pen, float lit, float a = 1) {
    return rgba(mix(INK, INK_LIT, lit), a * pen.s);
}

// How lit each control is: at once when held, fading when let go.
float level(int kind, int slot, bool on) {
    static float levels[TOUCH_KINDS][8];
    float& v = levels[kind][slot & 7];
    float dt = std::min(0.05f, std::max(0.001f, ImGui::GetIO().DeltaTime));
    v = on ? 1.0f : std::max(0.0f, v - dt / 0.14f);
    return v * v * (3 - 2 * v);
}

// ---- lettering ----------------------------------------------------------------------
//
// Strokes on a grid 6 high and (most letters) 4 wide, y down; -1 lifts the
// pen, -2 ends.

const float* strokes(char c, float* width) {
    static const float A[] = {0.1f, 6, 2, 0, 3.9f, 6, -1, 0.9f, 4.1f, 3.1f, 4.1f, -2};
    static const float C[] = {3.7f, 1.2f, 3.0f, 0.3f, 2, 0, 1.1f, 0.4f, 0.4f, 1.5f, 0.2f, 3, 0.4f, 4.5f, 1.1f, 5.6f,
                              2, 6, 3.0f, 5.7f, 3.7f, 4.8f, -2};
    static const float D[] = {0.3f, 0, 0.3f, 6, 1.9f, 6, 3.0f, 5.5f, 3.6f, 4.4f, 3.8f, 3, 3.6f, 1.6f, 3.0f, 0.5f,
                              1.9f, 0, 0.3f, 0, -2};
    static const float E[] = {3.6f, 0, 0.3f, 0, 0.3f, 6, 3.6f, 6, -1, 0.3f, 3, 3.0f, 3, -2};
    static const float F[] = {3.6f, 0, 0.3f, 0, 0.3f, 6, -1, 0.3f, 3, 2.9f, 3, -2};
    static const float I[] = {0.3f, 0, 0.3f, 6, -2};
    static const float L[] = {0.3f, 0, 0.3f, 6, 3.6f, 6, -2};
    static const float O[] = {2, 0, 3.1f, 0.4f, 3.7f, 1.5f, 3.8f, 3, 3.7f, 4.5f, 3.1f, 5.6f, 2, 6, 0.9f, 5.6f,
                              0.3f, 4.5f, 0.2f, 3, 0.3f, 1.5f, 0.9f, 0.4f, 2, 0, -2};
    static const float P[] = {0.3f, 6, 0.3f, 0, 2.3f, 0, 3.3f, 0.4f, 3.7f, 1.6f, 3.3f, 2.8f, 2.3f, 3.2f, 0.3f, 3.2f, -2};
    static const float R[] = {0.3f, 6, 0.3f, 0, 2.3f, 0, 3.3f, 0.4f, 3.7f, 1.5f, 3.3f, 2.6f, 2.3f, 3, 0.3f, 3, -1,
                              2.0f, 3, 3.8f, 6, -2};
    static const float S[] = {3.6f, 1.1f, 3.0f, 0.3f, 2, 0, 1, 0.3f, 0.4f, 1.2f, 0.7f, 2.2f, 1.6f, 2.8f, 2.6f, 3.2f,
                              3.4f, 3.8f, 3.6f, 4.8f, 3.0f, 5.7f, 2, 6, 1, 5.7f, 0.3f, 4.9f, -2};
    static const float T[] = {0.1f, 0, 3.9f, 0, -1, 2, 0, 2, 6, -2};
    static const float V[] = {0.1f, 0, 2, 6, 3.9f, 0, -2};
    static const float ONE[] = {1.0f, 1.3f, 2.3f, 0, 2.3f, 6, -2};
    static const float TWO[] = {0.4f, 1.5f, 0.9f, 0.5f, 2, 0, 3.1f, 0.5f, 3.5f, 1.6f, 3.1f, 2.8f, 0.3f, 6, 3.8f, 6, -2};
    *width = c == 'I' ? 0.6f : 4;
    switch (c) {
    case 'A': return A;
    case 'C': return C;
    case 'D': return D;
    case 'E': return E;
    case 'F': return F;
    case 'I': return I;
    case 'L': return L;
    case 'O': return O;
    case 'P': return P;
    case 'R': return R;
    case 'S': return S;
    case 'T': return T;
    case 'V': return V;
    case '1': return ONE;
    case '2': return TWO;
    default: return nullptr;
    }
}

const float LETTER_GAP = 1.5f;

// A word's width in the grid's units, or 0 when a letter of it has no strokes
// (lower case counts as its capital).
float letteringWidth(const char* text) {
    float total = 0, width;
    for (; *text != '\0'; text++) {
        char c = *text >= 'a' && *text <= 'z' ? (char)(*text - 'a' + 'A') : *text;
        if (strokes(c, &width) == nullptr) return 0;
        total += width + LETTER_GAP;
    }
    return total - LETTER_GAP;
}

// A word of those letters, `height` high, centred on c.
void lettering(ImDrawList* d, ImVec2 c, float height, ImU32 col, const char* text) {
    float k = height / 6, thick = std::max(1.5f, height * 0.15f), width;
    float x = c.x - letteringWidth(text) * k / 2, y = c.y - height / 2;
    for (; *text != '\0'; text++, x += (width + LETTER_GAP) * k) {
        char ch = *text >= 'a' && *text <= 'z' ? (char)(*text - 'a' + 'A') : *text;
        const float* s = strokes(ch, &width);
        if (s == nullptr) continue;
        ImVec2 pts[20];
        int n = 0;
        for (;; s += 2) {
            if (s[0] < 0) {
                if (n > 1) d->AddPolyline(pts, n, col, 0, thick);
                n = 0;
                if (s[0] < -1.5f) break;
                s -= 1; // the lift is one value, not a pair
                continue;
            }
            if (n < 20) pts[n++] = ImVec2(x + s[0] * k, y + s[1] * k);
        }
    }
}

// ---- the controls -------------------------------------------------------------------

// A face button's mark, r the button's radius.
void mark(ImDrawList* d, int kind, ImVec2 c, float r, ImU32 col, float t) {
    float s = r * 0.44f;
    Path p;
    switch (kind) {
    case TOUCH_TRIANGLE: {
        const float k = 0.866f; // cos 30
        float e = s * 1.22f;
        const ImVec2 v[3] = {ImVec2(c.x, c.y - e), ImVec2(c.x + e * k, c.y + e * 0.5f), ImVec2(c.x - e * k, c.y + e * 0.5f)};
        // far enough along the edges that the line's inner side still has a curve to follow
        const float rs[3] = {t * 1.9f, t * 1.9f, t * 1.9f};
        roundedPath(p, v, rs, 3);
        // the figure sits a little low in its circle: its centre of weight on the button's
        for (int i = 0; i < p.Size; i++) p[i].y += e * 0.08f;
        d->AddPolyline(p.Data, p.Size, col, ImDrawFlags_Closed, t);
        break;
    }
    case TOUCH_CIRCLE:
        d->AddCircle(c, s * 0.92f, col, 0, t);
        break;
    case TOUCH_CROSS: {
        // one figure, so the two strokes do not show twice where they cross
        float e = s * 0.82f, w = t * 0.5f * 0.7071f;
        const ImVec2 v[12] = {
            {c.x, c.y - 2 * w},         {c.x + e - w, c.y - e - w}, {c.x + e + w, c.y - e + w}, {c.x + 2 * w, c.y},
            {c.x + e + w, c.y + e - w}, {c.x + e - w, c.y + e + w}, {c.x, c.y + 2 * w},         {c.x - e + w, c.y + e + w},
            {c.x - e - w, c.y + e - w}, {c.x - 2 * w, c.y},         {c.x - e - w, c.y - e + w}, {c.x - e + w, c.y - e - w},
        };
        d->AddConcavePolyFilled(v, 12, col);
        break;
    }
    case TOUCH_SQUARE:
        boxPath(p, ImVec2(c.x - s * 0.8f, c.y - s * 0.8f), ImVec2(c.x + s * 0.8f, c.y + s * 0.8f), t);
        d->AddPolyline(p.Data, p.Size, col, ImDrawFlags_Closed, t);
        break;
    default:
        break;
    }
}

// The D-pad: a plus with rounded ends, each arm lit while held, an arrow at
// its end.
void dpad(const Pen& pen, const TouchControl& c) {
    ImDrawList* d = pen.d;
    float half = c.w / 2, arm = c.w * 0.175f; // an arm's half width
    float round = arm * 0.5f, inner = arm * 0.3f;
    ImVec2 o(c.x, c.y);
    const ImVec2 v[12] = {
        {o.x - arm, o.y - half}, {o.x + arm, o.y - half}, {o.x + arm, o.y - arm}, {o.x + half, o.y - arm},
        {o.x + half, o.y + arm}, {o.x + arm, o.y + arm},  {o.x + arm, o.y + half}, {o.x - arm, o.y + half},
        {o.x - arm, o.y + arm},  {o.x - half, o.y + arm}, {o.x - half, o.y - arm}, {o.x - arm, o.y - arm},
    };
    const float rs[12] = {round, round, inner, round, round, inner, round, round, inner, round, round, inner};
    Path p;
    roundedPath(p, v, rs, 12);
    halo(d, p, 1.5f * pen.u, rgba(SHADOW, 0.42f * pen.a));
    fill(d, p, true, o.y - half, o.y + half, GLASS_TOP, GLASS_BOTTOM, 0.72f * pen.a);
    // directions: up, right, down, left (the touch.h bits 1 2 4 8)
    const float dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
    float lit[4];
    for (int i = 0; i < 4; i++) {
        lit[i] = level(TOUCH_DPAD, i, (c.pressed >> i) & 1);
        if (lit[i] <= 0) continue;
        // the arm, from the hub to its end
        ImVec2 a(o.x + dx[i] * arm - dy[i] * arm, o.y + dy[i] * arm - dx[i] * arm);
        ImVec2 b(o.x + dx[i] * half + dy[i] * arm, o.y + dy[i] * half + dx[i] * arm);
        ImVec2 lo(std::min(a.x, b.x), std::min(a.y, b.y)), hi(std::max(a.x, b.x), std::max(a.y, b.y));
        int corners = i == 0 ? ImDrawFlags_RoundCornersTop : i == 1 ? ImDrawFlags_RoundCornersRight
                    : i == 2 ? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersLeft;
        d->AddRectFilled(lo, hi, rgba(LIT_BOTTOM, 0.9f * pen.s * lit[i]), round, corners);
    }
    rim(pen, p, 0.85f);
    d->AddCircle(o, arm * 0.42f, rgba(RIM, 0.3f * pen.s), 0, pen.line * 0.8f);
    for (int i = 0; i < 4; i++) {
        // the arrow, pointing out
        float at = half * 0.6f, s = arm * 0.5f;
        ImVec2 tip(o.x + dx[i] * (at + s * 1.1f), o.y + dy[i] * (at + s * 1.1f));
        ImVec2 l(o.x + dx[i] * at - dy[i] * s, o.y + dy[i] * at - dx[i] * s);
        ImVec2 r(o.x + dx[i] * at + dy[i] * s, o.y + dy[i] * at + dx[i] * s);
        d->AddTriangleFilled(tip, l, r, ink(pen, lit[i], 0.92f));
    }
}

// A stick: a shallow well with a mark for each way, and its cap where it is
// pushed.
void stick(const Pen& pen, const TouchControl& c) {
    ImDrawList* d = pen.d;
    float r = c.w / 2, lit = level(c.kind, 0, c.held != 0);
    ImVec2 o(c.x, c.y);
    Path p;
    circlePath(p, o, r);
    halo(d, p, 1.5f * pen.u, rgba(SHADOW, 0.34f * pen.a));
    fill(d, p, false, o.y - r, o.y + r, GLASS_BOTTOM, GLASS_TOP, 0.5f * pen.a);
    rim(pen, p, 0.6f);
    const float dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
    for (int i = 0; i < 4; i++) {
        float at = r * 0.8f, s = r * 0.09f;
        ImVec2 tip(o.x + dx[i] * (at + s), o.y + dy[i] * (at + s));
        ImVec2 l(o.x + dx[i] * at - dy[i] * s * 1.3f, o.y + dy[i] * at - dx[i] * s * 1.3f);
        ImVec2 q(o.x + dx[i] * at + dy[i] * s * 1.3f, o.y + dy[i] * at + dx[i] * s * 1.3f);
        d->AddTriangleFilled(tip, l, q, rgba(INK, 0.6f * pen.s));
    }
    float reach = touchStickTravel(&c), cap = r * 0.5f;
    ImVec2 k(o.x + c.sx * reach, o.y + c.sy * reach);
    circlePath(p, k, cap);
    halo(d, p, 1.8f * pen.u, rgba(SHADOW, 0.5f * pen.a));
    if (lit > 0) halo(d, p, (0.5f + 1.3f * lit) * pen.u, rgba(GLOW, 0.6f * pen.s * lit));
    Colour top = mix({104, 118, 158}, {158, 178, 222}, lit), bottom = mix({28, 34, 58}, {46, 58, 96}, lit);
    fill(d, p, false, k.y - cap, k.y + cap, top, bottom, (0.86f + 0.1f * lit) * pen.s);
    rim(pen, p, 0.9f + 0.1f * lit);
    d->AddCircle(k, cap * 0.56f, rgba(RIM, (0.4f + 0.3f * lit) * pen.s), 0, pen.line * 0.8f);
}

// A round button's plate; its mark is the caller's.
float roundPlate(const Pen& pen, const TouchControl& c, bool on) {
    float lit = level(c.kind, c.hotkey, on);
    Path p;
    circlePath(p, ImVec2(c.x, c.y), c.w / 2);
    plate(pen, p, false, c.y - c.w / 2, c.y + c.w / 2, lit);
    return lit;
}

float boxPlate(const Pen& pen, const TouchControl& c, float rounding, bool on) {
    float lit = level(c.kind, c.hotkey, on);
    Path p;
    boxPath(p, ImVec2(c.x - c.w / 2, c.y - c.h / 2), ImVec2(c.x + c.w / 2, c.y + c.h / 2), rounding);
    plate(pen, p, false, c.y - c.h / 2, c.y + c.h / 2, lit);
    return lit;
}

} // namespace

void touchpadDraw(ImDrawList* d, float w, float h) {
    TouchControl cs[32];
    int n = touchControls(cs, 32, w, h);
    Pen pen;
    pen.d = d;
    pen.a = touchOpacity();
    pen.s = powf(pen.a, 0.6f);
    pen.u = std::min(w, h) / 100 * touchFittedSizePercent(w, h) / 100;
    pen.line = std::max(1.5f, 0.3f * pen.u);
    // The hotkeys' row: its words all at the size the longest has room for.
    float rowHeight = FLT_MAX;
    for (int i = 0; i < n; i++) {
        if (cs[i].kind != TOUCH_HOTKEY) continue;
        float width = letteringWidth(touchHotkeyLabel(cs[i].hotkey));
        if (width > 0) rowHeight = std::min(rowHeight, std::min(cs[i].h * 0.28f, cs[i].w * 0.84f / width * 6));
    }
    for (int i = 0; i < n; i++) {
        const TouchControl& c = cs[i];
        ImVec2 o(c.x, c.y);
        bool on = c.pressed != 0;
        switch (c.kind) {
        case TOUCH_DPAD:
            dpad(pen, c);
            break;
        case TOUCH_STICK_LEFT:
        case TOUCH_STICK_RIGHT:
            stick(pen, c);
            break;
        case TOUCH_CROSS:
        case TOUCH_CIRCLE:
        case TOUCH_SQUARE:
        case TOUCH_TRIANGLE: {
            float lit = roundPlate(pen, c, on);
            mark(d, c.kind, o, c.w / 2, ink(pen, lit), std::max(2.0f, 0.085f * c.w));
            break;
        }
        case TOUCH_L1:
        case TOUCH_L2:
        case TOUCH_R1:
        case TOUCH_R2: {
            static const char* const names[] = {"L1", "L2", "R1", "R2"};
            float lit = boxPlate(pen, c, c.h * 0.36f, on);
            lettering(d, o, c.h * 0.42f, ink(pen, lit), names[c.kind - TOUCH_L1]);
            break;
        }
        case TOUCH_SELECT:
        case TOUCH_START: {
            float lit = boxPlate(pen, c, c.h / 2, on);
            lettering(d, o, c.h * 0.34f, ink(pen, lit), c.kind == TOUCH_START ? "START" : "SELECT");
            break;
        }
        case TOUCH_MENU: {
            // three bars: the settings menu
            float lit = roundPlate(pen, c, on), bw = c.w * 0.21f, bh = c.w * 0.032f;
            for (int k = -1; k <= 1; k++) {
                float y = o.y + k * c.w * 0.14f;
                d->AddRectFilled(ImVec2(o.x - bw, y - bh), ImVec2(o.x + bw, y + bh), ink(pen, lit), bh);
            }
            break;
        }
        case TOUCH_HOTKEYS: {
            // four squares: the hotkeys' row
            float lit = roundPlate(pen, c, on), s = c.w * 0.085f, g = c.w * 0.115f;
            for (int k = 0; k < 4; k++) {
                ImVec2 m(o.x + (k & 1 ? g : -g), o.y + (k & 2 ? g : -g));
                d->AddRectFilled(ImVec2(m.x - s, m.y - s), ImVec2(m.x + s, m.y + s), ink(pen, lit), s * 0.35f);
            }
            break;
        }
        case TOUCH_HOTKEY: {
            float lit = boxPlate(pen, c, c.h * 0.3f, on);
            lettering(d, o, rowHeight, ink(pen, lit), touchHotkeyLabel(c.hotkey));
            break;
        }
        case TOUCH_VIEW: {
            // an eye: the first-person view, light while it shows
            float lit = roundPlate(pen, c, on || c.on), s = c.w * 0.27f, e = c.w * 0.15f;
            ImVec2 pts[24];
            for (int k = 0; k < 12; k++) {
                float f = k / 12.0f;
                pts[k] = ImVec2(o.x - s + 2 * s * f, o.y - e * sinf(PI * f));
                pts[12 + k] = ImVec2(o.x + s - 2 * s * f, o.y + e * sinf(PI * f));
            }
            d->AddPolyline(pts, 24, ink(pen, lit), ImDrawFlags_Closed, pen.line * 1.15f);
            d->AddCircleFilled(o, e * 0.55f, ink(pen, lit));
            break;
        }
        case TOUCH_RECENTRE: {
            // a ring between four marks: the camera back to the game's view
            float lit = roundPlate(pen, c, on), r = c.w / 2;
            d->AddCircle(o, r * 0.3f, ink(pen, lit), 0, pen.line * 1.15f);
            const float dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
            for (int k = 0; k < 4; k++)
                d->AddLine(ImVec2(o.x + dx[k] * r * 0.42f, o.y + dy[k] * r * 0.42f),
                           ImVec2(o.x + dx[k] * r * 0.66f, o.y + dy[k] * r * 0.66f), ink(pen, lit), pen.line * 1.15f);
            break;
        }
        default:
            break;
        }
    }
}

} // namespace menu
