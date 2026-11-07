#ifndef PORT_EXTRAS_LIBRARY_H
#define PORT_EXTRAS_LIBRARY_H

#include "disc.h"
#include "imgui_internal.h"
#include "ui/skin.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace extras {
// Preview once when the cursor arrives, so a held highlight never restarts
// playback. A stationary mouse must not steal the controller's selection.
inline bool libraryHighlight(bool confirmed, ImGuiID& previous) {
    const auto& io = ImGui::GetIO();
    bool hover = ImGui::IsItemHovered() && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0);
    bool focus = ImGui::IsItemFocused() && ImGui::GetCurrentContext()->NavCursorVisible;
    if (!confirmed && !focus && !hover)
        return false;
    ImGuiID id = ImGui::GetItemID();
    if (!confirmed && previous == id)
        return false;
    previous = id;
    return true;
}

// Draw over the preview without taking space or becoming a navigation item.
// Positions are one-based; the game's animation ids remain zero-based.
inline void libraryCounter(const char* label, int position, int total) {
    char text[64];
    std::snprintf(text, sizeof(text), "%s %02d/%02d", label, position, total);
    ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    ImVec2 size = ImGui::CalcTextSize(text);
    float pad = 4.0f * skin::unit();
    ImVec2 at(std::max(min.x + pad, max.x - size.x - 3 * pad), min.y + 2 * pad);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(at.x - pad, at.y - pad), ImVec2(at.x + size.x + pad, at.y + size.y + pad),
                      IM_COL32(24, 26, 40, 230));
    dl->AddText(at, ImGui::GetColorU32(ImGuiCol_Text), text);
}

// Short labels at the preview's bottom left (over the last item drawn), one
// a line ('\n' between them), in the game's gold: what a record is when it
// is not plain artwork.
inline void libraryTag(const char* text) {
    if (!text || !*text) return;
    ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    ImVec2 size = ImGui::CalcTextSize(text);
    float pad = 4.0f * skin::unit();
    ImVec2 at(min.x + 3 * pad, std::max(min.y + pad, max.y - size.y - 3 * pad));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(min, max, true);
    dl->AddRectFilled(ImVec2(at.x - pad, at.y - pad), ImVec2(at.x + size.x + pad, at.y + size.y + pad),
                      IM_COL32(24, 26, 40, 230));
    dl->AddText(at, ImGui::GetColorU32(skin::textWarning), text);
    dl->PopClipRect();
}

inline uint64_t libraryFileHash(XgDisc* disc, int dir, int file) {
    int at = xgDiscFileEntry(disc, dir, file), size = 0;
    if (at < 0 || xgDiscEntry(disc, at).size <= 0)
        return 0;
    auto* bytes = xgDiscReadEntry(disc, at, &size);
    if (!bytes)
        return 0;
    uint64_t hash = 14695981039346656037ull;
    for (int i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    free(bytes);
    return hash;
}
// Each flat library section owns its selection, playback and camera. The
// engines use one current pointer, so switch it only outside their callbacks.
template <class T> class LibraryStates {
    std::vector<std::pair<std::string, T*>> states;

  public:
    bool select(const char* id, T*& current, void (*open)()) {
        if (states.empty()) {
            states.push_back({id, current});
            return true;
        }
        for (auto& s : states)
            if (s.first == id) {
                current = s.second;
                return false;
            }
        open();
        states.push_back({id, current});
        return true;
    }
    void close(T*& current, void (*destroy)()) {
        if (states.empty())
            destroy();
        else
            for (auto& s : states) {
                current = s.second;
                destroy();
            }
        states.clear();
    }
};

// A native command window above the equipment/preview sheet. Keep its
// background opaque so the underlying list does not show through its text.
inline bool libraryOptions(const char* id, bool& requested, float width, float height) {
    if (requested) { ImGui::OpenPopup(id); requested = false; }
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    float u = (float)skin::unit();
    ImGui::SetNextWindowSize(ImVec2(std::min(width * u, screen.x - 32), std::min(height * u, screen.y - 64)));
    ImGui::SetNextWindowPos(ImVec2(screen.x * .5f, screen.y * .5f), ImGuiCond_Appearing, ImVec2(.5f, .5f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 5 * u);
    bool open = ImGui::BeginPopup(id);
    ImGui::PopStyleVar();
    if (!open) return false;
    if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(0);
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRectFullScreen();
    dl->AddRectFilled(w->Pos, ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y), IM_COL32(8, 8, 12, 255));
    skin::box(dl, w->Pos, ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y));
    dl->PopClipRect();
    return true;
}
} // namespace extras
#endif
