// Repeated skin::setUnit calls (window resizes, Menu size changes, leaving
// Extras) must scale from a clean ImGuiStyle rather than compounding padding
// and minimum sizes across calls (xg-c7gy).

#include "ui/assets.h"
#include "ui/skin.h"
#include "imgui_internal.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <initializer_list>

extern "C" {
#include "cd/cd.h"
#include "system/pad.h"

int cdDiscSectorCount(void) { return 0; }
int padActionGlyph(const char*) { return PAD_GLYPH_NONE; }
int padInputGlyph(int) { return PAD_GLYPH_NONE; }
}

namespace assets {
bool load() { return false; }
const Image* sprite(int, bool) { return nullptr; }
Image vramImage(int, int, int, int, int, int, int, int, int, bool) { return {}; }
int fontCode(uint32_t) { return -1; }
bool fontGlyph(int, uint8_t[13][14], int*) { return false; }
void fontColors(uint16_t* body, uint16_t* outline) { *body = *outline = 0; }
uint32_t rgba(uint16_t, bool) { return 0; }
} // namespace assets

static int checks, failures;
#include "test/check.h"

static bool sameVec2(ImVec2 a, ImVec2 b) {
    return std::fabs(a.x - b.x) < 0.01f && std::fabs(a.y - b.y) < 0.01f;
}

static void finishTextures() {
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
        if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
            tex->SetTexID((ImTextureID)1);
            tex->SetStatus(ImTextureStatus_OK);
        } else if (tex->Status == ImTextureStatus_WantDestroy) {
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

static float measureGroupFrame(float unit) {
    skin::setUnit(unit);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(560.0f * unit, 336.0f * unit);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("pane", nullptr, ImGuiWindowFlags_NoDecoration);
    float y0 = ImGui::GetCursorScreenPos().y;
    ImGui::SeparatorText("Buttons");
    ImGui::TextUnformatted("Button convention");
    float span = ImGui::GetCursorScreenPos().y - y0;
    ImGui::End();
    ImGui::Render();
    finishTextures();
    return span;
}

int main() {
    SDL_setenv_unsafe("XG_SETTINGS", "none", 1);

    for (float target : {1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 7.0f}) {
        ImGuiContext* freshCtx = ImGui::CreateContext();
        ImGui::SetCurrentContext(freshCtx);
        skin::setUnit(target);
        const ImGuiStyle fresh = ImGui::GetStyle();
        ImGui::DestroyContext(freshCtx);

        ImGuiContext* cycledCtx = ImGui::CreateContext();
        ImGui::SetCurrentContext(cycledCtx);
        for (float step : {2.0f, 7.0f, 4.0f, 1.5f, 10.0f, 3.0f, target}) {
            skin::setUnit(step);
        }
        const ImGuiStyle& s = ImGui::GetStyle();
        EXPECT(skin::unit() == target);
        EXPECT(sameVec2(s.SeparatorTextPadding, fresh.SeparatorTextPadding));
        EXPECT(s.SeparatorTextBorderSize == fresh.SeparatorTextBorderSize);
        EXPECT(sameVec2(s.WindowPadding, fresh.WindowPadding));
        EXPECT(sameVec2(s.WindowMinSize, fresh.WindowMinSize));
        EXPECT(sameVec2(s.FramePadding, fresh.FramePadding));
        EXPECT(sameVec2(s.CellPadding, fresh.CellPadding));
        EXPECT(sameVec2(s.ItemSpacing, fresh.ItemSpacing));
        EXPECT(sameVec2(s.ItemInnerSpacing, fresh.ItemInnerSpacing));
        EXPECT(s.IndentSpacing == fresh.IndentSpacing);
        EXPECT(s.ColumnsMinSpacing == fresh.ColumnsMinSpacing);
        EXPECT(s.ScrollbarSize == fresh.ScrollbarSize);
        EXPECT(s.ScrollbarPadding == fresh.ScrollbarPadding);
        EXPECT(s.GrabMinSize == fresh.GrabMinSize);
        EXPECT(s.TabMinWidthShrink == fresh.TabMinWidthShrink);
        EXPECT(s.SeparatorSize == fresh.SeparatorSize);
        EXPECT(s.InputTextCursorSize == fresh.InputTextCursorSize);
        EXPECT(s.FontSizeBase == fresh.FontSizeBase);
        ImGui::DestroyContext(cycledCtx);
    }

    // Actual SeparatorText layout height across repeated scale switches.
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    skin::init();

    float firstAt4 = measureGroupFrame(4.0f);
    for (float step : {2.0f, 7.0f, 3.0f, 10.0f, 1.5f, 7.0f}) {
        measureGroupFrame(step);
    }
    float afterCycledAt4 = measureGroupFrame(4.0f);
    EXPECTF(std::fabs(afterCycledAt4 - firstAt4) < 0.01f, "got %.2f vs %.2f", afterCycledAt4, firstAt4);
    EXPECTF(afterCycledAt4 <= 44.0f * 4.0f, "span %.2f exceeded compact group height", afterCycledAt4);

    // Menu font size scales FontSizeBase independently and clamps to [75, 150].
    skin::setFontScalePercent(125);
    EXPECT(skin::fontScalePercent() == 125);
    skin::setUnit(4.0f);
    EXPECTF(std::fabs(ImGui::GetStyle().FontSizeBase - 13.0f * 4.0f * 1.25f) < 0.01f,
            "got %.2f", ImGui::GetStyle().FontSizeBase);
    skin::setFontScalePercent(50);
    EXPECT(skin::fontScalePercent() == skin::FONT_SCALE_MIN);
    skin::setFontScalePercent(250);
    EXPECT(skin::fontScalePercent() == skin::FONT_SCALE_MAX);
    skin::setFontScalePercent(skin::FONT_SCALE_DEFAULT);
    skin::setUnit(4.0f);
    EXPECTF(std::fabs(ImGui::GetStyle().FontSizeBase - 13.0f * 4.0f) < 0.01f,
            "got %.2f", ImGui::GetStyle().FontSizeBase);

    // skin::checkbox bakes bevel+red sprites into the current font atlas and toggles on click.
    {
        bool off = false, on = true;
        ImGuiIO& curIo = ImGui::GetIO();
        curIo.DisplaySize = ImVec2(560.0f * 4.0f, 336.0f * 4.0f);
        curIo.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(curIo.DisplaySize);
        ImGui::Begin("checkpane", nullptr, ImGuiWindowFlags_NoDecoration);
        ImVec2 startPos = ImGui::GetCursorScreenPos();
        EXPECT(!skin::checkbox("Every Gear", &off));
        ImVec2 boxEnd = ImGui::GetItemRectMax();
        EXPECTF(std::fabs((boxEnd.y - startPos.y) - ImGui::GetFrameHeight()) < 0.5f,
                "height %.2f vs %.2f", boxEnd.y - startPos.y, ImGui::GetFrameHeight());
        EXPECT(!skin::checkbox("Party in its Gears", &on));
        ImGui::End();
        ImGui::Render();
        finishTextures();

        // Hover first so ImGui tracks the window/item, then press and release to toggle.
        curIo.AddMousePosEvent(startPos.x + 8.0f, startPos.y + 8.0f);
        auto runCheckboxFrame = [&]() {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(curIo.DisplaySize);
            ImGui::Begin("checkpane", nullptr, ImGuiWindowFlags_NoDecoration);
            bool toggled = skin::checkbox("Every Gear", &off);
            ImGui::End();
            ImGui::Render();
            finishTextures();
            return toggled;
        };
        EXPECT(!runCheckboxFrame());
        curIo.AddMouseButtonEvent(0, true);
        runCheckboxFrame();
        curIo.AddMouseButtonEvent(0, false);
        bool changed = runCheckboxFrame();
        EXPECT(changed && off);

        // Verify the baked 13x13 bevel+red sprite was actually added to the font atlas.
        ImTextureData* tex = curIo.Fonts->TexData;
        EXPECT(tex != nullptr && tex->Format == ImTextureFormat_RGBA32);
        bool foundBevel = false, foundRed = false;
        if (tex != nullptr && tex->Pixels != nullptr) {
            const uint32_t bevel = IM_COL32(204, 204, 218, 255);
            const uint32_t red = IM_COL32(248, 56, 56, 255);
            const uint32_t redHot = IM_COL32(255, 76, 76, 255);
            for (int y = 0; y < tex->Height; y++) {
                const uint32_t* row = (const uint32_t*)tex->GetPixelsAt(0, y);
                for (int x = 0; x < tex->Width; x++) {
                    if (row[x] == bevel) foundBevel = true;
                    if (row[x] == red || row[x] == redHot) foundRed = true;
                }
            }
        }
        EXPECT(foundBevel && foundRed);
    }

    ImGui::DestroyContext(ctx);
    std::printf("skintest: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
