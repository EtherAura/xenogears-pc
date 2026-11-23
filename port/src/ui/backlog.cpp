// The dialogue backlog's screen (menu.h drawBacklog; system/backlog.h;
// xg-htn.57): the text the game has shown, read again in a window of the
// game's own (skin.h frameWindow: its frame, opening as its message boxes
// open), the lines as the printer drew them (their pixels, in the font's
// colours of the time) and a movie's captions in the game's dialogue font.
// The game is paused meanwhile, as for the settings menu (menu.cpp runs it).
//
// The newest text at the bottom, where it opens. Up and Down scroll a line,
// L1 and R1 (Page Up, Page Down) a window's height, Home and End to either
// end, the mouse wheel three lines; the cancel button, Esc, H or
// Select+Square closes it.

#include <float.h>
#include <math.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "assets.h"
#include "imgui.h"
#include "imgui_internal.h" // RegisterUserTexture: the page picture
#include "l10n.h"
#include "menu.h"
#include "skin.h"

extern "C" {
#include "system/backlog.h"
}

namespace menu {

namespace {

constexpr int LINE = 14;  // a text row: the printer's 13 pixel lines and one between, as the game spaces them
constexpr int GAP = 8;    // between two texts
constexpr int PAD = 10;   // the window's inside around the text
constexpr int WIDTH = 288; // the window's inside, game pixels
constexpr int TEX_W = 320, TEX_H = 512; // the page picture (game pixels; the part in use is the window's inside)

struct Item {
    BacklogEntry e;
    int top, height; // in the content, game pixels
};

struct {
    ImTextureData* tex = nullptr;
    bool registered = false;
    std::vector<Item> items;
    int scroll = 0;     // game pixels from the content's top to the view's
    bool follow = true; // at the newest: stays there
    bool chordWas = true;
    int drawnScroll = -1, drawnCount = -1, drawnW = 0, drawnH = 0;
    uint32_t drawnNewest = 0;
} B;

int captionHeight(const char* text, int width) {
    ImFont* font = skin::dialogueFont();
    if (font == nullptr || text == nullptr) return LINE;
    ImVec2 size = font->CalcTextSizeA((float)LINE, FLT_MAX, (float)width, text);
    return std::max(LINE, (int)ceilf(size.y / LINE) * LINE);
}

// The entries and where each sits, oldest first; the content's height.
int layout(int width) {
    B.items.clear();
    int y = 0, n = backlogCount();
    for (int i = 0; i < n; i++) {
        Item it{};
        if (!backlogGet(i, &it.e)) continue;
        it.height = it.e.text != nullptr ? captionHeight(it.e.text, width) : it.e.rows * LINE;
        if (it.height <= 0) continue;
        if (!B.items.empty()) y += GAP;
        it.top = y;
        y += it.height;
        B.items.push_back(it);
    }
    return y;
}

// The visible part of the content into the page picture: each kept row's
// pixels in its colours, clear elsewhere (captions are drawn as text over it).
void paint(int w, int h) {
    if (B.tex == nullptr) {
        B.tex = IM_NEW(ImTextureData)();
        B.tex->Create(ImTextureFormat_RGBA32, TEX_W, TEX_H);
    }
    uint32_t* px = (uint32_t*)B.tex->GetPixels();
    memset(px, 0, (size_t)TEX_W * TEX_H * 4);
    for (const Item& it : B.items) {
        if (it.e.pixels == nullptr || it.top + it.height <= B.scroll || it.top >= B.scroll + h) continue;
        uint32_t body = assets::rgba(it.e.body, false) | 0xFF000000u;
        uint32_t outline = assets::rgba(it.e.outline, false) | 0xFF000000u;
        int cols = std::min(it.e.width, w);
        for (int r = 0; r < it.e.rows; r++) {
            for (int y = 0; y < BACKLOG_LINE; y++) {
                int dy = it.top + r * LINE + y - B.scroll;
                if (dy < 0 || dy >= h) continue;
                const uint8_t* src = it.e.pixels + (size_t)(r * BACKLOG_LINE + y) * it.e.stride;
                uint32_t* dst = px + (size_t)dy * TEX_W;
                for (int x = 0; x < cols; x++) {
                    uint8_t v = src[x];
                    if (v != 0) dst[x] = v & 1 ? body : outline;
                }
            }
        }
    }
    if (B.tex->Status == ImTextureStatus_OK) {
        ImTextureRect all = {0, 0, (unsigned short)TEX_W, (unsigned short)TEX_H};
        B.tex->Updates.push_back(all);
        B.tex->UpdateRect = all;
        B.tex->SetStatus(ImTextureStatus_WantUpdates);
    } else if (B.tex->Status != ImTextureStatus_WantCreate && B.tex->Status != ImTextureStatus_WantUpdates) {
        B.tex->SetStatus(ImTextureStatus_WantCreate);
    }
    if (!B.registered) {
        ImGui::RegisterUserTexture(B.tex);
        B.registered = true;
    }
}

bool pressed(ImGuiKey k) {
    return ImGui::IsKeyPressed(k, true);
}

} // namespace

void backlogOpened() {
    B.follow = true;
    B.drawnScroll = -1;
    B.chordWas = true; // the chord that opened it is let go before it can close it
}

bool drawBacklog() {
    ImGuiIO& io = ImGui::GetIO();
    float u = scale();
    int w = WIDTH;
    int h = (int)floorf(io.DisplaySize.y / u) - 2 * PAD - 48;
    h = std::max(3 * LINE, std::min(h, 15 * LINE + 6));
    w = std::min(w, std::max(160, (int)floorf(io.DisplaySize.x / u) - 2 * PAD - 32));
    w = std::min(w, TEX_W);
    h = std::min(h, TEX_H);

    int content = layout(w);
    h = std::min(h, std::max(content, 3 * LINE)); // no taller than what it holds (the game is paused: it holds still)
    int most = std::max(0, content - h);
    // The controls.
    int step = 0;
    if (pressed(ImGuiKey_GamepadDpadUp)) step -= LINE;
    if (pressed(ImGuiKey_GamepadDpadDown)) step += LINE;
    if (pressed(ImGuiKey_GamepadL1) || pressed(ImGuiKey_PageUp)) step -= h - LINE;
    if (pressed(ImGuiKey_GamepadR1) || pressed(ImGuiKey_PageDown)) step += h - LINE;
    if (io.MouseWheel != 0) step -= (int)lroundf(io.MouseWheel * 3) * LINE;
    if (pressed(ImGuiKey_Home)) B.scroll = 0, B.follow = false;
    if (pressed(ImGuiKey_End)) B.follow = true;
    if (step != 0) {
        B.scroll = std::max(0, std::min(most, (B.follow ? most : B.scroll) + step));
        B.follow = B.scroll >= most;
    }
    if (B.follow) B.scroll = most;
    B.scroll = std::max(0, std::min(B.scroll, most));
    bool chord = padHeld("select") && padHeld("square");
    bool close = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                 (ImGui::IsKeyPressed(ImGuiKey_H, false) && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift) ||
                 (chord && !B.chordWas);
    B.chordWas = chord;

    uint32_t newest = B.items.empty() ? 0 : B.items.back().e.serial;
    if (B.scroll != B.drawnScroll || (int)B.items.size() != B.drawnCount || newest != B.drawnNewest || w != B.drawnW ||
        h != B.drawnH) {
        paint(w, h);
        B.drawnScroll = B.scroll;
        B.drawnCount = (int)B.items.size();
        B.drawnNewest = newest;
        B.drawnW = w;
        B.drawnH = h;
    }

    ImVec2 size((w + 2 * PAD) * u, (h + 2 * PAD) * u);
    ImVec2 pos(floorf((io.DisplaySize.x - size.x) * 0.5f), floorf((io.DisplaySize.y - size.y) * 0.5f));
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PAD * u, PAD * u)); // its inside: where it clips
    ImGui::Begin("##backlog", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    skin::frameWindow(true);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 inner(pos.x + PAD * u, pos.y + PAD * u), innerEnd(inner.x + w * u, inner.y + h * u);
    if (B.items.empty()) {
        ImGui::SetCursorScreenPos(inner);
        textLines("Nothing yet: the text the game shows from now on is kept here.");
    } else if (B.tex != nullptr) {
        // (Its upload, a new one or an update, comes before the draw: ImGui_ImplVulkan_RenderDrawData.)
        ImTextureRef ref;
        ref._TexData = B.tex;
        dl->AddImage(ref, inner, innerEnd, ImVec2(0, 0), ImVec2((float)w / TEX_W, (float)h / TEX_H));
    }
    ImFont* font = skin::dialogueFont();
    if (font != nullptr) {
        dl->PushClipRect(inner, innerEnd, true);
        for (const Item& it : B.items) {
            if (it.e.text == nullptr || it.top + it.height <= B.scroll || it.top >= B.scroll + h) continue;
            dl->AddText(font, LINE * u, ImVec2(inner.x, inner.y + (it.top - B.scroll) * u), IM_COL32_WHITE, it.e.text,
                        nullptr, w * u);
        }
        dl->PopClipRect();
    }
    if (content > h) {
        // The game's list scroll bar at the inside's right edge.
        float track = h * u, thumb = std::max(8 * u, track * h / content - 16 * u);
        float top = most > 0 ? (track - 16 * u - thumb) * B.scroll / most : 0;
        skin::scrollBar(dl, ImVec2(innerEnd.x + 1 * u, inner.y), track, top, thumb);
    }
    ImGui::End();
    return close;
}

} // namespace menu
