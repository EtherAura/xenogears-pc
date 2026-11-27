#include "extras.h"
#include "viewer.h"
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
    drawOptions(area, "##arena", modeHelp, [] { menu::arenaSection(part, leaveIntoGame); },
                menu::opt::submenuTitle(menu::EXTRAS), focusMode);
    focusMode = menu::opt::takeSubmenuEnter();
    if (focusMode) modeHelp = {};
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
    nullptr, nullptr, nullptr, nullptr, nullptr, true, true,
};

XG_EXTRAS_VIEWER(arena);
XG_EXTRAS_VIEWER(versus);
XG_EXTRAS_VIEWER(gauntlet);

} // namespace
} // namespace extras
