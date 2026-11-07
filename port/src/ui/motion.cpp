#include "motion.h"

#include <algorithm>
#include <cmath>

namespace menuMotion {
ImVec2 View::project(ImVec2 p) const {
    float x = (p.x - center.x) * sx, y = (p.y - center.y) * sy;
    float k = focal / (depth + y * sine);
    return ImVec2(center.x + x * k, center.y + y * cosine * k);
}

bool View::inverse(ImVec2 p, ImVec2* out) const {
    float y = p.y - center.y;
    float d = focal * cosine - y * sine;
    if (std::fabs(d) < 0.001f || sx < 0.001f || sy < 0.001f) return false;
    y = y * depth / d;
    float k = focal / (depth + y * sine);
    if (k <= 0) return false;
    *out = ImVec2(center.x + (p.x - center.x) / (sx * k), center.y + y / sy);
    return true;
}

View opening(ImVec2 center, float unit, unsigned frame) {
    if (frame >= OPENING_FRAMES) return {center, 512 * unit, 512 * unit, 0, 1};
    float angle = float(frame * 0x7c) * (6.28318530718f / 4096);
    return {center, 512 * unit, float(0x800 - frame * 0x30) * unit, std::sin(angle), std::cos(angle)};
}

View leaving(ImVec2 center, float unit, unsigned frame) {
    float angle = -float(frame * 0x60) * (6.28318530718f / 4096);
    return {center, 512 * unit, float(0x200 + frame * 0x40) * unit,
            std::sin(angle), std::cos(angle)};
}

View expanding(ImVec2 center, ImVec2 size, float unit, unsigned frame) {
    float extent = float(frame + 1) * 32 * unit;
    return {center, 512 * unit, 512 * unit, 0, 1,
            std::min(1.0f, extent / size.x), std::min(1.0f, extent / size.y)};
}

namespace {
ImDrawVert interpolate(const ImDrawVert& a, const ImDrawVert& b, float t) {
    ImDrawVert v;
    v.pos = ImVec2(a.pos.x + (b.pos.x - a.pos.x) * t, a.pos.y + (b.pos.y - a.pos.y) * t);
    v.uv = ImVec2(a.uv.x + (b.uv.x - a.uv.x) * t, a.uv.y + (b.uv.y - a.uv.y) * t);
    v.col = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        float ac = float((a.col >> shift) & 255), bc = float((b.col >> shift) & 255);
        v.col |= ImU32(ac + (bc - ac) * t + 0.5f) << shift;
    }
    return v;
}

int clip(const ImDrawVert* in, int n, ImDrawVert* out, int axis, float edge, bool greater) {
    int count = 0;
    for (int i = 0; i < n; i++) {
        const ImDrawVert& a = in[i];
        const ImDrawVert& b = in[(i + 1) % n];
        float av = axis ? a.pos.y : a.pos.x, bv = axis ? b.pos.y : b.pos.x;
        bool ai = greater ? av >= edge : av <= edge, bi = greater ? bv >= edge : bv <= edge;
        if (ai) out[count++] = a;
        if (ai != bi) out[count++] = interpolate(a, b, (edge - av) / (bv - av));
    }
    return count;
}
}

void drawList(ImDrawList* list, const View& view, bool yaw) {
    ImVector<ImDrawVert> vertices;
    ImVector<ImDrawIdx> indices;
    ImVector<ImDrawCmd> commands;
    auto project = [&](ImVec2 p) {
        if (!yaw) return view.project(p);
        ImVec2 swapped(view.center.x + p.y - view.center.y, view.center.y + p.x - view.center.x);
        View horizontal = view;
        horizontal.sine = -horizontal.sine; // Y rotation: z = -x sin(ry)
        ImVec2 q = horizontal.project(swapped);
        return ImVec2(view.center.x + q.y - view.center.y, view.center.y + q.x - view.center.x);
    };
    for (const ImDrawCmd& original : list->CmdBuffer) {
        ImDrawCmd cmd = original;
        cmd.IdxOffset = indices.Size;
        cmd.VtxOffset = vertices.Size;
        cmd.ElemCount = 0;
        if (original.UserCallback) { commands.push_back(cmd); continue; }
        ImVec2 corners[] = {project(ImVec2(cmd.ClipRect.x, cmd.ClipRect.y)),
                            project(ImVec2(cmd.ClipRect.z, cmd.ClipRect.y)),
                            project(ImVec2(cmd.ClipRect.z, cmd.ClipRect.w)),
                            project(ImVec2(cmd.ClipRect.x, cmd.ClipRect.w))};
        ImVec4 bounds(corners[0].x, corners[0].y, corners[0].x, corners[0].y);
        for (ImVec2 p : corners) {
            bounds.x = std::min(bounds.x, p.x); bounds.y = std::min(bounds.y, p.y);
            bounds.z = std::max(bounds.z, p.x); bounds.w = std::max(bounds.w, p.y);
        }
        cmd.ClipRect = bounds;
        for (unsigned i = 0; i + 2 < original.ElemCount; i += 3) {
            ImDrawVert a[12], b[12];
            for (int j = 0; j < 3; j++)
                a[j] = list->VtxBuffer[original.VtxOffset + list->IdxBuffer[original.IdxOffset + i + j]];
            int n = clip(a, 3, b, 0, original.ClipRect.x, true);
            n = clip(b, n, a, 0, original.ClipRect.z, false);
            n = clip(a, n, b, 1, original.ClipRect.y, true);
            n = clip(b, n, a, 1, original.ClipRect.w, false);
            if (n < 3) continue;
            if (unsigned(vertices.Size) - cmd.VtxOffset + n > 65535) {
                commands.push_back(cmd);
                cmd.IdxOffset = indices.Size; cmd.VtxOffset = vertices.Size; cmd.ElemCount = 0;
            }
            ImDrawIdx base = ImDrawIdx(unsigned(vertices.Size) - cmd.VtxOffset);
            for (int j = 0; j < n; j++) { a[j].pos = project(a[j].pos); vertices.push_back(a[j]); }
            for (int j = 1; j + 1 < n; j++) {
                indices.push_back(base); indices.push_back(ImDrawIdx(base + j)); indices.push_back(ImDrawIdx(base + j + 1));
                cmd.ElemCount += 3;
            }
        }
        commands.push_back(cmd);
    }
    list->VtxBuffer.swap(vertices); list->IdxBuffer.swap(indices); list->CmdBuffer.swap(commands);
}
} // namespace menuMotion
