// The settings menu's look (skin.h): the game's menu windows, cursors, font
// and colours over ImGui.

#include "skin.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "imgui_internal.h"

extern "C" {
#include "cd/cd.h"
#include "system/settings.h"
#include "system/pad.h"
}
#include "gpu/shaders/prompt.glsl"

namespace skin {

// The game colours its menu text by texture modulation (0x80 = the
// texture's own colour) of the white letters and their navy outline
// (fontPalettes): the stat digits (setStatDigitsColor) 0x40,0x40,0x40 for an
// unchanged value, 0x80,0x40,0x40 for a rising one; the card screen's
// progress bar (updateMemoryCardProgressBox) 0xA0,0xA0,0 while it runs and
// 0,0xA0,0 when done (ImGui's tint stops at 0x80: 0xA0 is shown as 0x80).
const ImVec4 textHint(0.5f, 0.5f, 0.5f, 1.0f);
const ImVec4 textWarning(1.0f, 1.0f, 0.0f, 1.0f);
const ImVec4 textError(1.0f, 0.5f, 0.5f, 1.0f);
const ImVec4 textGood(0.0f, 1.0f, 0.0f, 1.0f);
// An entry the game shows as unavailable (the main menu while a sub-menu is
// up, drawMainMenu; an item that cannot be used, setMenuPolyShadeMode 1): its
// letters modulated by 0x20 and added to what is behind (abr 1, B + F/4).
// ImGui blends by alpha only: F/4 + 3B/4 is the same over black and stays
// within B/4 of it over the menu's dark windows.
const ImVec4 textDimmed(1.0f, 1.0f, 1.0f, 0.25f);

namespace {

#define LINE 13 // the game's text image is 13 rows: 11 of glyph and the outline
// Dark windows' inside: the game's own half-clear blend with a grey half as
// light (0x30 for 0x68), so it stays a Xenogears window and the picture still
// shows through, while the light letters (and the dimmed ones) stand out more.
#define DARK_FILL IM_COL32(0x30, 0x30, 0x30, 128)

struct Sprite {
    bool tried;
    ImFontAtlasRectId rect;
    int w, h, ox, oy;
};

// A window that opens as the game's message box does (frameWindow).
struct Opening {
    ImGuiID id;
    unsigned start, seen; // frames
};
// A window's draw list cut where its frame ends: its contents not shown yet.
struct Hidden {
    ImDrawList* list;
};
void contentsMark(const ImDrawList*, const ImDrawCmd*) {}

struct State {
    bool tried, native;
    ImVector<Opening> openings;
    ImVector<Hidden> hidden;
    float unit = 1.0f; // ImGui units per game pixel (the menu works in window pixels)
    Sprite sprites[0x200];
    Sprite buttons[4]; // SPRITE_CROSS...
    ImFontAtlasRectId frameRect = ImFontAtlasRectId_Invalid; // the window frame's 48 x 16 pieces
    unsigned frames;
    uint32_t textBody[2], textOutline[2];
    ImFont* optionFont = nullptr;
};

State st;

ImFontAtlas* atlas() {
    return ImGui::GetIO().Fonts;
}

// A picture into ImGui's texture (the font atlas: RGBA, kept when it grows).
ImFontAtlasRectId addPicture(const uint32_t* rgba, int w, int h) {
    ImFontAtlasRect r;
    ImFontAtlasRectId id = atlas()->AddCustomRect(w, h, &r);
    ImTextureData* tex = atlas()->TexData;
    if (id == ImFontAtlasRectId_Invalid || tex == nullptr || tex->Format != ImTextureFormat_RGBA32) {
        return ImFontAtlasRectId_Invalid;
    }
    for (int y = 0; y < h; y++) {
        memcpy(tex->GetPixelsAt(r.x, r.y + y), rgba + y * w, (size_t)w * 4);
    }
    return id;
}

// ---- the game's font as an ImGui font --------------------------------------------------

// Its glyphs drawn at a whole number of pixels per game pixel: the size asked
// for in pixels over the game's 13-row line. A whole number but for the
// menu's percentage scale (scalePercent): then each glyph is drawn at that
// scale into whole pixels (some game pixels one window pixel wider than
// others), so the letters keep the menu's proportions.
float glyphScale(ImFontConfig* src, ImFontBaked* baked, float* density) {
    *density = src->RasterizerDensity * baked->RasterizerDensity;
    float k = baked->Size * *density / LINE;
    if (fabsf(k - roundf(k)) < 0.01f) k = roundf(k);
    return k < 1 ? 1 : k;
}

bool fontContains(ImFontAtlas*, ImFontConfig*, ImWchar c) {
    return (c >= 0xe000 && c < 0xe000 + PAD_GLYPH_COUNT) || assets::fontCode(c) >= 0;
}

bool fontBakedInit(ImFontAtlas*, ImFontConfig*, ImFontBaked* baked, void*) {
    baked->Ascent = baked->Size;
    baked->Descent = 0.0f;
    return true;
}

bool fontLoadGlyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void*, ImWchar c, ImFontGlyph* out,
                   float* outAdvance) {
    uint8_t px[13][14];
    uint32_t icon[13][14] = {};
    int advance = 8;
    int code = assets::fontCode(c);
    bool button = c >= 0xe000 && c < 0xe000 + PAD_GLYPH_COUNT;
    if (button) {
        memset(px,0,sizeof(px));
        int glyph = c - 0xe000;
        advance = 14;
        if (glyph < 4) {
            static const int u[] = {0,36,24,12};
            assets::Image im = assets::vramImage(1,448,0,0,511,u[glyph],120,12,12,false);
            for (int y=0;im.rgba && y<12;++y) for (int x=0;x<12;++x) {
                icon[y][x]=im.rgba[y*12+x];
                px[y][x]=icon[y][x] != 0;
            }
            free(im.rgba);
        } else {
            for (int y=0;y<12;++y) for (int x=0;x<12;++x) {
                uint32_t texel = promptTexel(glyph,x,y);
                icon[y][x]=texel ? assets::rgba(texel,false) : 0;
                px[y][x]=texel != 0;
            }
        }
    } else if (code < 0 || !assets::fontGlyph(code, px, &advance)) {
        return false;
    }
    float density;
    float k = glyphScale(src, baked, &density);
    if (outAdvance != nullptr) {
        *outAdvance = advance * k / density;
        return true;
    }
    out->Codepoint = c;
    out->Colored = button;
    out->AdvanceX = advance * k / density;
    bool visible = false;
    for (int y = 0; y < 13; y++) {
        for (int x = 0; x < 14; x++) {
            visible |= px[y][x] != 0;
        }
    }
    if (!visible) {
        return true;
    }
    int w = (int)ceilf(14 * k - 0.01f), h = (int)ceilf(13 * k - 0.01f);
    ImFontAtlasRectId id = ImFontAtlasPackAddRect(atlas, w, h);
    if (id == ImFontAtlasRectId_Invalid) {
        return false;
    }
    ImTextureRect* r = ImFontAtlasPackGetRect(atlas, id);
    ImVector<uint32_t> pixels;
    pixels.resize(w * h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int ty = std::min(12, (int)(y / k)), tx = std::min(13, (int)(x / k));
            uint8_t v = px[ty][tx];
            int palette = src->FontLoaderFlags & 1;
            pixels[y * w + x] = button ? icon[ty][tx] :
                v == 1 ? st.textBody[palette] : v == 2 ? st.textOutline[palette] : 0;
        }
    }
    // The picture starts at the pen (its first column is the outline).
    out->X0 = 0.0f;
    out->Y0 = 0.0f;
    out->X1 = w / density;
    out->Y1 = h / density;
    out->Visible = true;
    out->PackId = id;
    ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, out, r, (const unsigned char*)pixels.Data,
                                       ImTextureFormat_RGBA32, w * 4);
    return true;
}

const ImFontLoader* fontLoader() {
    static ImFontLoader loader;
    loader.Name = "xenogears";
    loader.FontSrcContainsGlyph = fontContains;
    loader.FontBakedInit = fontBakedInit;
    loader.FontBakedLoadGlyph = fontLoadGlyph;
    return &loader;
}

uint16_t optionColor(uint16_t c) {
    // OP_SETUP_MULTICHOICE shades only the chosen line's text sprites.
    // PS1 texture modulation uses 0x80 as unity and clamps to five bits.
    int r = (c & 31) * 0xEF >> 7;
    int g = ((c >> 5) & 31) * 0x1E >> 7;
    int b = ((c >> 10) & 31) * 0xF0 >> 7;
    return (c & 0x8000) | (r > 31 ? 31 : r) | (g > 31 ? 31 : g) << 5 | (b > 31 ? 31 : b) << 10;
}

const Sprite* getSprite(int id) {
    if (st.native && id >= SPRITE_CROSS && id <= SPRITE_SQUARE) {
        // The buttons' 12 x 12 pictures, 8-bit at page (448, 0) row 120 with
        // the CLUT at (0, 511): cross, circle, triangle, square.
        static const int u[4] = {36, 0, 24, 12};
        Sprite& b = st.buttons[id - SPRITE_CROSS];
        if (!b.tried) {
            b.tried = true;
            b.rect = ImFontAtlasRectId_Invalid;
            assets::Image im = assets::vramImage(1, 448, 0, 0, 511, u[id - SPRITE_CROSS], 120, 12, 12, false);
            if (im.rgba != nullptr) {
                b.rect = addPicture(im.rgba, im.w, im.h);
                b.w = im.w;
                b.h = im.h;
                free(im.rgba);
            }
        }
        return b.rect != ImFontAtlasRectId_Invalid ? &b : nullptr;
    }
    if (!st.native || id < 0 || id >= (int)IM_COUNTOF(st.sprites)) {
        return nullptr;
    }
    Sprite& s = st.sprites[id];
    if (!s.tried) {
        s.tried = true;
        s.rect = ImFontAtlasRectId_Invalid;
        const assets::Image* im = assets::sprite(id, false);
        if (im != nullptr) {
            s.rect = addPicture(im->rgba, im->w, im->h);
            s.w = im->w;
            s.h = im->h;
            s.ox = im->ox;
            s.oy = im->oy;
        }
    }
    return s.rect != ImFontAtlasRectId_Invalid ? &s : nullptr;
}

// A piece of the window frame picture (u, v in it), mirrored where u1 < u0
// or v1 < v0, over [p0, p1].
void framePiece(ImDrawList* dl, const ImFontAtlasRect& r, ImVec2 p0, ImVec2 p1, float u0, float v0, float u1,
                float v1) {
    const float tw = (r.uv1.x - r.uv0.x) / r.w, th = (r.uv1.y - r.uv0.y) / r.h;
    dl->AddImage(atlas()->TexRef, p0, p1, ImVec2(r.uv0.x + u0 * tw, r.uv0.y + v0 * th),
                 ImVec2(r.uv0.x + u1 * tw, r.uv0.y + v1 * th));
}

} // namespace

bool init() {
    if (st.tried && (st.native || cdDiscSectorCount() == 0)) {
        return st.native;
    }
    // The first time; or again once a disc is in the drive when the first
    // try had none (the setup screen before the first image): the game's look
    // added then, over ImGui's own.
    bool again = st.tried;
    st.tried = true;
    ImGuiIO& io = ImGui::GetIO();
    st.native = assets::load();
    if (st.native) {
        uint16_t body, outline;
        assets::fontColors(&body, &outline);
        st.textBody[0] = assets::rgba(body, false);
        st.textOutline[0] = assets::rgba(outline, false);
        st.textBody[1] = assets::rgba(optionColor(body), false);
        st.textOutline[1] = assets::rgba(optionColor(outline), false);
        for (int palette = 0; palette < 2; ++palette) {
            ImFontConfig cfg;
            cfg.FontLoader = fontLoader();
            cfg.FontLoaderFlags = palette;
            cfg.SizePixels = LINE;
            snprintf(cfg.Name, sizeof(cfg.Name), "Xenogears dialogue%s", palette ? " selected" : "");
            ImFont* font = io.Fonts->AddFont(&cfg);
            if (palette) st.optionFont = font;
            // What the game's font lacks (characters in file paths) from ImGui's own.
            ImFontConfig merge;
            merge.MergeMode = true;
            io.Fonts->AddFontDefaultVector(&merge);
        }
        // The window frame: the 48 x 16 pieces of loadMenuSharedResources's
        // border sprites (0xFD-0x104), 4-bit at page (448, 0) with the CLUT at
        // (272, 508), drawn semi-transparent (initMenuTiles1Sub).
        assets::Image frame = assets::vramImage(0, 448, 0, 272, 508, 0, 132, 48, 16, true);
        if (frame.rgba != nullptr) {
            st.frameRect = addPicture(frame.rgba, frame.w, frame.h);
            free(frame.rgba);
        }
        if (again) {
            io.FontDefault = io.Fonts->Fonts[1]; // the game's (Fonts[0]: ImGui's, from the try without a disc)
        }
    } else if (!again) {
        io.Fonts->AddFontDefaultVector();
    }
    return st.native;
}

bool native() {
    return st.native;
}

const char* buttonGlyph(int glyph) {
    static char utf8[PAD_GLYPH_COUNT][4];
    if (glyph < 0 || glyph >= PAD_GLYPH_COUNT) return "";
    if (!st.native) return "";
    unsigned code = 0xe000 + glyph;
    utf8[glyph][0] = (char)(0xe0 | (code >> 12));
    utf8[glyph][1] = (char)(0x80 | ((code >> 6) & 63));
    utf8[glyph][2] = (char)(0x80 | (code & 63));
    return utf8[glyph];
}

const char* psButtonGlyph(const char* button) {
    static const char* const names[] = {"circle","cross","triangle","square","l1","r1","l2","r2",
        "l3","r3","start","select","up","down","left","right"};
    static const int ids[] = {0,1,2,3,PAD_GLYPH_L1,PAD_GLYPH_R1,PAD_GLYPH_L2,PAD_GLYPH_R2,
        PAD_GLYPH_L3,PAD_GLYPH_R3,PAD_GLYPH_START,PAD_GLYPH_SELECT,PAD_GLYPH_UP,PAD_GLYPH_DOWN,
        PAD_GLYPH_LEFT,PAD_GLYPH_RIGHT};
    for (unsigned i=0;i<IM_COUNTOF(names);++i) if (!strcmp(button,names[i])) return buttonGlyph(ids[i]);
    return "";
}

const char* actionGlyph(const char* button) {
    int glyph = padActionGlyph(button);
    return glyph == PAD_GLYPH_NONE ? psButtonGlyph(button) : buttonGlyph(glyph);
}

const char* inputGlyph(int input) { return buttonGlyph(padInputGlyph(input)); }

void beginFrame() {
    st.hidden.clear();
    // Every texel of the menu to a whole block of pixels: no filtering.
    if (ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest != nullptr) {
        ImGui::GetBackgroundDrawList()->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
    }
}

float unit() {
    return st.unit;
}

TextSize textSize() {
    static bool described;
    if (!described) {
        described = true;
        settingsDescribe("menu_text", "menu_text: the settings menu's letters, game (as large as the game's own menus) or "
                                  "compact (smaller, more at once)");
    }
    const char* v = settingsGet("menu_text");
    return v != nullptr && strcmp(v, "game") == 0 ? TEXT_GAME : TEXT_COMPACT;
}

void setTextSize(TextSize size) {
    settingsSet("menu_text", size == TEXT_GAME ? "game" : "compact");
    settingsSave();
}

bool darkWindows() {
    static bool described;
    if (!described) {
        described = true;
        settingsDescribe("menu_dark", "menu_dark: the settings menu's windows darker (1) or the game's grey (0)");
    }
    return settingsBool("menu_dark", 0) != 0;
}

void setDarkWindows(bool on) {
    settingsSetInt("menu_dark", on ? 1 : 0);
    settingsSave();
}

int unitFor(int w, int h) {
    // Game: the game's 320 x 216 picture as large as the window holds it in
    // whole pixels (4 at 1868 x 965, 10 at 3840 x 2160). Compact: room for
    // 480 x 300 game pixels (3 and 7 there).
    int s = textSize() == TEXT_GAME ? (w / 320 < h / 216 ? w / 320 : h / 216) : (w / 480 < h / 300 ? w / 480 : h / 300);
    return s < 1 ? 1 : s;
}

int scalePercent() {
    static bool described;
    if (!described) {
        described = true;
        settingsDescribe("menu_scale", "menu_scale: the settings menu's size in percent of its text size's (50 to 200, "
                                       "default 100)");
    }
    int p = settingsInt("menu_scale", SCALE_DEFAULT);
    return p < SCALE_MIN ? SCALE_MIN : p > SCALE_MAX ? SCALE_MAX : p;
}

void setScalePercent(int percent) {
    settingsSetInt("menu_scale", percent < SCALE_MIN ? SCALE_MIN : percent > SCALE_MAX ? SCALE_MAX : percent);
    settingsSave();
}

float menuUnitFor(int w, int h) {
    // The text size's whole pixels times the percentage: game size, the
    // game's screen that much larger or smaller (still the game's layout,
    // centred); compact, the panel's letters, boxes and gaps. At least one
    // window pixel per game pixel, and never more than the window holds the
    // game's 320 x 216 screen (both layouts need that much); 100% (or a
    // percentage that comes to a whole number) stays in whole pixels.
    float u = (float)unitFor(w, h) * (float)scalePercent() / 100.0f;
    float most = std::min((float)w / 320.0f, (float)h / 216.0f);
    u = std::min(u, most);
    if (fabsf(u - roundf(u)) < 0.01f) u = roundf(u);
    return u < 1.0f ? 1.0f : u;
}

void setUnit(float unit) {
    st.unit = unit;
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = s.ChildRounding = s.FrameRounding = s.PopupRounding = s.GrabRounding = s.TabRounding = 0;
    s.ScrollbarRounding = 0;
    s.WindowBorderSize = s.ChildBorderSize = s.PopupBorderSize = s.FrameBorderSize = 0;
    s.AntiAliasedLines = s.AntiAliasedLinesUseTex = s.AntiAliasedFill = false;
    // In game pixels: the game's lists are 16 rows apart, 13 of text and 3
    // between (setupItemMenuPage: rows 16 apart; processLoadSaveMenuSub5: lines
    // at 0xA0 + 16 i). Frames get no padding above and below their text, so a
    // control's row is as tall as a line of text; table rows are at least 16
    // (menu.h ROW, sections.cpp row()).
    s.WindowPadding = ImVec2(12, 10);
    s.FramePadding = ImVec2(4, 0);
    s.CellPadding = ImVec2(4, 0);
    s.ItemSpacing = ImVec2(8, 3);
    s.ItemInnerSpacing = ImVec2(5, 3);
    s.IndentSpacing = 12;
    s.ScrollbarSize = 5;
    s.GrabMinSize = 6;
    s.SeparatorTextBorderSize = 1;
    s.TouchExtraPadding = ImVec2(0, 0);
    s.ScaleAllSizes(st.unit);
    s.FontSizeBase = LINE * st.unit;
    s.FontScaleMain = 1.0f;
    s.FontScaleDpi = 1.0f;

    ImVec4* c = s.Colors;
    const ImVec4 none(0, 0, 0, 0);
    c[ImGuiCol_Text] = ImVec4(1, 1, 1, 1);
    c[ImGuiCol_TextDisabled] = textDimmed;
    c[ImGuiCol_WindowBg] = st.native ? none : ImVec4(0.2f, 0.2f, 0.24f, 0.95f);
    c[ImGuiCol_ChildBg] = none;
    c[ImGuiCol_PopupBg] = st.native ? none : ImVec4(0.2f, 0.2f, 0.24f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.75f, 0.75f, 0.85f, 0.5f);
    c[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.06f, 0.18f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_FrameBgActive] = ImVec4(1, 1, 1, 0.15f);
    c[ImGuiCol_Button] = ImVec4(0.0f, 0.0f, 0.06f, 0.18f);
    c[ImGuiCol_ButtonHovered] = c[ImGuiCol_FrameBgHovered];
    c[ImGuiCol_ButtonActive] = c[ImGuiCol_FrameBgActive];
    c[ImGuiCol_Header] = ImVec4(0.38f, 0.16f, 0.64f, 0.80f);
    c[ImGuiCol_HeaderHovered] = c[ImGuiCol_FrameBgHovered];
    c[ImGuiCol_HeaderActive] = c[ImGuiCol_FrameBgActive];
    c[ImGuiCol_CheckMark] = ImVec4(0.93f, 0.93f, 0.93f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.85f, 0.12f, 0.18f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.24f, 0.30f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.75f, 0.75f, 0.85f, 0.35f);
    c[ImGuiCol_SeparatorHovered] = c[ImGuiCol_SeparatorActive] = c[ImGuiCol_Separator];
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.06f, 0.06f, 1.0f); // the game's red scroll bar
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.95f, 0.15f, 0.15f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(1.0f, 0.25f, 0.25f, 1.0f);
    c[ImGuiCol_TableHeaderBg] = none;
    c[ImGuiCol_TableBorderLight] = ImVec4(0.75f, 0.75f, 0.85f, 0.18f);
    c[ImGuiCol_TableBorderStrong] = c[ImGuiCol_TableBorderLight];
    c[ImGuiCol_TableRowBg] = none;
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.55f, 0.55f, 0.85f, 0.45f);
    c[ImGuiCol_NavCursor] = st.native ? none : ImVec4(1.0f, 0.8f, 0.45f, 1.0f); // the pointer shows it
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0); // the game's message box dims nothing behind it
    c[ImGuiCol_InputTextCursor] = ImVec4(1, 1, 1, 1);
}

void box(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    float g = st.unit;
    // The background: a 0x68 grey at half over the picture
    // (setupMenuContext364); dark windows: DARK_FILL instead.
    dl->AddRectFilled(a, b, darkWindows() ? DARK_FILL : IM_COL32(0x68, 0x68, 0x68, 128));
    ImFontAtlasRect r;
    if (!st.native || st.frameRect == ImFontAtlasRectId_Invalid || !atlas()->GetCustomRect(st.frameRect, &r)) {
        dl->AddRect(a, b, IM_COL32(200, 200, 230, 200), 0, 0, g);
        return;
    }
    // draw380's pieces: 16 x 16 corners centred on the box's corners (the
    // picture's bottom left one, mirrored), 16-wide edges between them, each
    // edge two halves that stretch the picture's strip (u 0-7 top, 8-15
    // bottom; rows 0-6 left, 8-14 right of the 16 columns from u 16).
    float o = 8 * g, x0 = a.x, y0 = a.y, x1 = b.x, y1 = b.y;
    framePiece(dl, r, ImVec2(x0 - o, y0 - o), ImVec2(x0 + o, y0 + o), 32, 16, 48, 0);
    framePiece(dl, r, ImVec2(x1 - o, y0 - o), ImVec2(x1 + o, y0 + o), 48, 16, 32, 0);
    framePiece(dl, r, ImVec2(x0 - o, y1 - o), ImVec2(x0 + o, y1 + o), 32, 0, 48, 16);
    framePiece(dl, r, ImVec2(x1 - o, y1 - o), ImVec2(x1 + o, y1 + o), 48, 0, 32, 16);
    float midX = x0 + o + floorf((x1 - x0 - 2 * o) / (2 * g)) * g;
    float midY = y0 + o + floorf((y1 - y0 - 2 * o) / (2 * g)) * g;
    if (x1 - x0 > 2 * o) {
        framePiece(dl, r, ImVec2(x0 + o, y0 - o), ImVec2(midX, y0 + o), 0, 0, 7, 16);
        framePiece(dl, r, ImVec2(midX, y0 - o), ImVec2(x1 - o, y0 + o), 0, 0, 7, 16);
        framePiece(dl, r, ImVec2(x0 + o, y1 - o), ImVec2(midX, y1 + o), 8, 0, 15, 16);
        framePiece(dl, r, ImVec2(midX, y1 - o), ImVec2(x1 - o, y1 + o), 8, 0, 15, 16);
    }
    if (y1 - y0 > 2 * o) {
        framePiece(dl, r, ImVec2(x0 - o, y0 + o), ImVec2(x0 + o, midY), 16, 0, 32, 7);
        framePiece(dl, r, ImVec2(x0 - o, midY), ImVec2(x0 + o, y1 - o), 16, 0, 32, 7);
        framePiece(dl, r, ImVec2(x1 - o, y0 + o), ImVec2(x1 + o, midY), 16, 8, 32, 15);
        framePiece(dl, r, ImVec2(x1 - o, midY), ImVec2(x1 + o, y1 - o), 16, 8, 32, 15);
    }
}

void frameWindow(bool opens) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    ImDrawList* dl = w->DrawList;
    // A window the game would animate (its message boxes: iniMenuContext364And380
    // with animate 1, drawMenu_380) opens from its middle, 0x20 game pixels
    // wider and taller each frame from nothing, and what it holds shows after
    // it is whole (processLoadSaveMenuSub5 waits for m11_done): frame k of it
    // is min(32 (k + 1), its size) across and down. A window seen again after
    // a frame without it opens again.
    Opening* o = nullptr;
    for (Opening& c : st.openings) {
        if (c.id == w->ID) o = &c;
    }
    if (o == nullptr) {
        st.openings.push_back({w->ID, st.frames, st.frames});
        o = &st.openings.back();
    }
    if (o->seen + 1 < st.frames) o->start = st.frames;
    if (w->Hidden) o->start = st.frames + 1; // its first frame (sized, not shown)
    o->seen = st.frames;
    float g = st.unit, k = !opens ? FLT_MAX : st.frames < o->start ? 0.0f : float(st.frames - o->start + 1) * 32 * g;
    ImVec2 c(w->Pos.x + w->Size.x * 0.5f, w->Pos.y + w->Size.y * 0.5f);
    ImVec2 half(floorf(fminf(w->Size.x, k) * 0.5f / g) * g, floorf(fminf(w->Size.y, k) * 0.5f / g) * g);
    ImVec2 a(floorf(c.x / g) * g - half.x, floorf(c.y / g) * g - half.y), b(a.x + 2 * half.x, a.y + 2 * half.y);
    bool whole = k >= w->Size.x && k >= w->Size.y;
    // Its lines come two frames after it is whole (Beetle: the card check's
    // box whole on picture 2758, its text on 2760: processLoadSaveMenuSub5
    // sets them up after the loop, then draws twice).
    bool contents = !opens || (st.frames >= o->start &&
                               float(st.frames - o->start) >= ceilf(fmaxf(w->Size.x, w->Size.y) / (32 * g)) + 1);
    if (whole) {
        a = w->Pos;
        b = ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y);
    }
    // The window is the frame's inside; the border's glow reaches out of it.
    dl->PushClipRectFullScreen();
    // Over the menu's own text: a dark ground first, so the half-clear grey
    // of the game's window does not show the text beneath.
    dl->AddRectFilled(a, b, IM_COL32(8, 8, 12, 230));
    box(dl, a, b);
    dl->PopClipRect();
    if (!contents) {
        // menu.cpp drops what the window draws after this mark (hideUnopened):
        // a callback that does nothing, so it stays where it is when ImGui
        // puts a command before it (a modal's dimmed background).
        dl->AddCallback(contentsMark, nullptr);
        st.hidden.push_back({dl});
    }
}

// The window whose draw list this is, and whether it is inside one still
// opening (a list in a popup: all of it waits).
bool insideHidden(const ImDrawList* list) {
    for (ImGuiWindow* w : GImGui->Windows) {
        if (w->DrawList != list || w->RootWindow == nullptr || w->RootWindow == w) continue;
        for (const Hidden& h : st.hidden) {
            if (h.list == w->RootWindow->DrawList) return true;
        }
    }
    return false;
}

void hideUnopened(ImDrawList* original, ImDrawList* copy) {
    if (!hidesContents(original)) return;
    if (insideHidden(original)) {
        copy->CmdBuffer.resize(0);
        copy->IdxBuffer.resize(0);
        copy->VtxBuffer.resize(0);
        return;
    }
    for (int i = 0; i < copy->CmdBuffer.Size; i++) {
        if (copy->CmdBuffer[i].UserCallback == contentsMark) {
            copy->CmdBuffer.resize(i);
            break;
        }
    }
}

bool hidesContents(const ImDrawList* list) {
    for (const Hidden& h : st.hidden) {
        if (h.list == list) return true;
    }
    return !st.hidden.empty() && insideHidden(list);
}

void pushOptionText(bool selected) {
    ImGui::PushFont(selected && st.optionFont ? st.optionFont : ImGui::GetFont(), ImGui::GetStyle().FontSizeBase);
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? (st.optionFont ? ImVec4(1, 1, 1, 1) : ImVec4(1, 0.19f, 1, 1))
                                                : ImGui::GetStyleColorVec4(ImGuiCol_Text));
}

void popOptionText() {
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

bool selectable(const char* label, bool selected, ImGuiSelectableFlags flags, ImVec2 size) {
    pushOptionText(selected);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    bool pressed = ImGui::Selectable(label, selected, flags, size);
    ImGui::PopStyleColor();
    popOptionText();
    return pressed;
}

void focusWindow() {
#ifdef VERSION_JP
    ImGuiContext& g = *GImGui;
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (g.NavWindow != w || !g.NavId || !g.NavCursorVisible) return;
    ImRect r = ImGui::WindowRectRelToAbs(w, w->NavRectRel[g.NavLayer]);
    if (r.Max.y <= w->InnerClipRect.Min.y || r.Min.y >= w->InnerClipRect.Max.y) return;
    // As the game's band sits on its labels (updateCursorPolysLocation: from 2
    // left of and 2 above the label's text, 16 down): on the item's first line.
    float u = st.unit;
    selection(w->DrawList, ImVec2(r.Min.x - 2 * u, r.Min.y - 2 * u), ImVec2(r.Max.x + 2 * u, r.Min.y + 14 * u));
#endif
}

void selection(ImDrawList* dl, ImVec2 min, ImVec2 max) {
#ifdef VERSION_JP
    // The JP game's cursor band (drawMenu_348_cursor draws it only #if
    // VERSION_JP; the US game has none): initPrimitives_348_cursor's POLY_G4,
    // 0x80,0x80,0 at the top to black at the bottom, semi-transparent in
    // m128_drawMode's mode 0 (B/2 + F/2: alpha one half here), under two
    // LINE_F3 of 0,0x40,0 (top and right, left and bottom), opaque.
    ImU32 top = IM_COL32(0x80, 0x80, 0, 128), bottom = IM_COL32(0, 0, 0, 128), line = IM_COL32(0, 0x40, 0, 255);
    float u = st.unit;
    dl->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);
    dl->AddRectFilled(min, ImVec2(max.x, min.y + u), line);
    dl->AddRectFilled(ImVec2(min.x, max.y - u), max, line);
    dl->AddRectFilled(min, ImVec2(min.x + u, max.y), line);
    dl->AddRectFilled(ImVec2(max.x - u, min.y), max, line);
#else
    (void)dl, (void)min, (void)max;
#endif
}

void sprite(ImDrawList* dl, int id, ImVec2 origin, ImU32 tint, int scale) {
    const Sprite* s = getSprite(id);
    ImFontAtlasRect r;
    if (s == nullptr || !atlas()->GetCustomRect(s->rect, &r)) {
        return;
    }
    float g = st.unit * scale;
    ImVec2 p0(origin.x - s->ox * g, origin.y - s->oy * g);
    dl->AddImage(atlas()->TexRef, p0, ImVec2(p0.x + s->w * g, p0.y + s->h * g), r.uv0, r.uv1, tint);
}

void spriteAt(ImDrawList* dl, int id, ImVec2 topLeft, ImU32 tint) {
    const Sprite* s = getSprite(id);
    if (s != nullptr) {
        sprite(dl, id, ImVec2(topLeft.x + s->ox * st.unit, topLeft.y + s->oy * st.unit), tint);
    }
}

ImVec2 spriteSize(int id) {
    const Sprite* s = getSprite(id);
    return s != nullptr ? ImVec2((float)s->w, (float)s->h) : ImVec2(0, 0);
}

// A sprite's picture over [p0, p1], mirrored top to bottom if flipY.
void spriteStretched(ImDrawList* dl, int id, ImVec2 p0, ImVec2 p1, bool flipY) {
    const Sprite* s = getSprite(id);
    ImFontAtlasRect r;
    if (s == nullptr || !atlas()->GetCustomRect(s->rect, &r)) {
        return;
    }
    dl->AddImage(atlas()->TexRef, p0, p1, ImVec2(r.uv0.x, flipY ? r.uv1.y : r.uv0.y),
                 ImVec2(r.uv1.x, flipY ? r.uv0.y : r.uv1.y));
}

void scrollBar(ImDrawList* dl, ImVec2 top, float height, float thumbTop, float thumbHeight) {
    float g = st.unit, w = 8 * g, cap = 8 * g;
    if (!st.native || spriteSize(SPRITE_TRACK_CAP).x == 0) {
        dl->AddRectFilled(top, ImVec2(top.x + 2 * g, top.y + height), IM_COL32(160, 160, 176, 160));
        dl->AddRectFilled(ImVec2(top.x, top.y + cap + thumbTop), ImVec2(top.x + 4 * g, top.y + cap + thumbTop + thumbHeight),
                          IM_COL32(200, 16, 16, 255));
        return;
    }
    // initMenuTiles6: the cap at the top, the edge stretched down from it,
    // the cap mirrored at the bottom; the thumb over the edge.
    spriteStretched(dl, SPRITE_TRACK_EDGE, ImVec2(top.x, top.y + cap), ImVec2(top.x + w, top.y + height - cap), false);
    spriteStretched(dl, SPRITE_TRACK_CAP, top, ImVec2(top.x + w, top.y + cap), false);
    spriteStretched(dl, SPRITE_TRACK_CAP, ImVec2(top.x, top.y + height - cap), ImVec2(top.x + w, top.y + height), true);
    spriteStretched(dl, SPRITE_THUMB, ImVec2(top.x, top.y + cap + thumbTop),
                    ImVec2(top.x + w, top.y + cap + thumbTop + thumbHeight), false);
}

void pointer(ImDrawList* dl, ImVec2 at) {
    // updateMenuPointerGlyph: frames 4 down to 0, one every 6 vblanks.
    int frame = 4 - (int)(st.frames / 6 % 5);
    if (st.native) {
        sprite(dl, SPRITE_POINTER + frame, at);
        return;
    }
    float g = st.unit;
    dl->AddTriangleFilled(at, ImVec2(at.x + 7 * g, at.y + 5 * g), ImVec2(at.x, at.y + 10 * g), IM_COL32(220, 80, 90, 255));
}

void endFrame(ImGuiID skipWindow) {
    st.frames++;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* w = g.NavWindow;
    if (w == nullptr || g.NavId == 0 || !g.NavCursorVisible || w->ID == skipWindow || hidesContents(w->DrawList) ||
        w->Hidden) {
        return;
    }
    ImRect r = ImGui::WindowRectRelToAbs(w, w->NavRectRel[g.NavLayer]);
    float u = st.unit;
    const ImRect& clip = w->InnerClipRect;
    // Where the game's item list has it (updateMenuPointerGlyph mode 1: 16
    // left of the name, 3 below its top; setupItemMenuPage), on the item's
    // first line; in a pane, inside it (an item as wide as the pane has it
    // over its own left end); a popup's beside it. Not for an item scrolled
    // away.
    bool popup = (w->Flags & ImGuiWindowFlags_Popup) != 0;
    float x = r.Min.x - 16 * u;
    if (!popup && x < clip.Min.x + u) {
        x = clip.Min.x + u;
    }
    ImVec2 at(x, r.Min.y + 3 * u);
    if (r.Max.y <= clip.Min.y || r.Min.y >= clip.Max.y) {
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->PushClipRect(ImVec2(clip.Min.x - 14 * u, clip.Min.y), clip.Max, false);
    pointer(dl, at);
    dl->PopClipRect();
}

} // namespace skin
