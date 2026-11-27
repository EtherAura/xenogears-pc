#include "extras.h"
#include "viewer.h"
#include "imgui_internal.h"
#include "ui/l10n.h"
#include "ui/menu.h"
#include "ui/options.h"

namespace extras {
namespace {

menu::OptionHelp modeHelp;
bool focusMode;

void openArenaMode() {
    menu::opt::clearSubmenu(menu::EXTRAS);
    modeHelp = {};
    focusMode = true;
}

bool backArenaMode() {
    if (!menu::opt::popSubmenu(menu::EXTRAS)) return false;
    modeHelp = {};
    focusMode = true;
    return true;
}

template<menu::ArenaPart part>
void drawArenaMode(const Area& area) {
    const float rightStick = ImGui::GetKeyData(ImGuiKey_GamepadRStickDown)->AnalogValue -
                             ImGui::GetKeyData(ImGuiKey_GamepadRStickUp)->AnalogValue;
    const float helpHeight = fminf(61 * area.unit, area.size().y * 0.42f);
    const ImVec2 helpMin(area.min.x, area.max.y - helpHeight);
    const ImVec2 paneMax(area.max.x, helpMin.y - 6 * area.unit);
    std::string description;
    if (beginPane("##arena", area.min, paneMax, menu::opt::submenuTitle(menu::EXTRAS))) {
        ImGuiWindow* pane = ImGui::GetCurrentWindow();
        if (focusMode) {
            ImGui::SetScrollY(0);
            ImGui::FocusWindow(pane);
            ImGui::NavInitWindow(pane, true);
            ImGui::SetNavCursorVisible(true);
            focusMode = false;
        }
        if (modeHelp.overflow() <= 0 && fabsf(rightStick) > 0.2f && viewerShortcuts()) {
            ImGui::SetScrollY(ImGui::GetScrollY() + rightStick * 6 * area.unit);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 7 * area.unit);
        modeHelp.begin();
        menu::arenaSection(part, leaveIntoGame);
        description = modeHelp.finish();
        ImGuiContext& g = *ImGui::GetCurrentContext();
        float y = g.NavWindow == pane && g.NavId && g.NavCursorVisible
                      ? ImGui::WindowRectRelToAbs(pane, pane->NavRectRel[g.NavLayer]).GetCenter().y
                      : -FLT_MAX;
        menu::opt::finishPage(y, false);
        ImGui::PopStyleVar();
    }
    endPane(false);
    modeHelp.draw(description, helpMin, area.max, area.unit, rightStick);
    if (menu::opt::takeSubmenuEnter()) {
        modeHelp = {};
        focusMode = true;
    }
}

void closeArena() {
    extrasJukeboxPreview(-1);
}

void drawGauntlet(const Area& area) {
    if (beginPane("##gauntlet", area.min, area.max)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 7 * area.unit);
        menu::gauntletSection(leaveIntoGame);
        ImGui::PopStyleVar();
    }
    endPane(false);
}

const Viewer arena = {
    "battle-arena", N_("Battle arena"), N_("Two-player Gear Battling, local or online."), 1,
    openArenaMode, closeArena, drawArenaMode<menu::ArenaPart::Battling>, backArenaMode, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, true, true,
};
const Viewer versus = {
    "versus-battle", N_("Versus battle"), N_("Play on this device or online."), 2,
    openArenaMode, closeArena, drawArenaMode<menu::ArenaPart::Versus>, backArenaMode, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, true, true,
};
const Viewer gauntlet = {
    "boss-gauntlet", N_("Boss gauntlet"), N_("Fight story bosses back to back."), 4,
    nullptr, nullptr, drawGauntlet, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, true,
};

XG_EXTRAS_VIEWER(arena);
XG_EXTRAS_VIEWER(versus);
XG_EXTRAS_VIEWER(gauntlet);

} // namespace
} // namespace extras
