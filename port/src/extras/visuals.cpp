// Flat visual library. Each section opens a fresh private preview world.
#include "bookmarks.h"
#include "library.h"
#include "ui/l10n.h"
#include "viewer.h"
#include <algorithm>
#include <cstring>

namespace {
using namespace extras;
const char* const ids[] = {"sprites", "models", "battle", "battlefx", "maps", "gallery"};
struct Section {
    const char* id;
    const char* name;
    int engine;
    const char* source;
};
const Section sections[] = {
    {"party", N_("Party sprites"), 0, "party"},
    {"gear", N_("Gear sprites"), 0, "gear"},
    {"npc", N_("Field NPCs"), 0, "npc"},
    {"battle-sprites", N_("Battle party sprites"), 0, "battle"},
    {"special", N_("Battle specials"), 0, "special"},
    {"effects", N_("Spell sprites"), 0, "effects"},
    {"enemy-sprites", N_("Enemy sprites"), 0, "enemy"},
    {"field-gears", N_("Field Gear models"), 1, "gears"},
    {"battle-models", N_("Battle Gear models"), 1, "battle"},
    {"enemy-models", N_("Enemy models"), 1, "enemies"},
    {"arenas", N_("Arena models"), 1, "arenas"},
    {"fields", N_("Field models"), 1, "fields"},
    {"world", N_("World map models"), 1, "world"},
    {"battling", N_("Battling models"), 1, "battling"},
    {"battle", N_("Battle scene"), 2, ""},
    {"abilities", N_("Ability effects"), 3, "abilities"},
    {"common", N_("Common effects"), 3, "common"},
    {"trails", N_("Gear trails"), 3, "gears"},
    {"maps", N_("Map viewer"), 4, ""},
    {"gallery", N_("Gallery"), 5, ""},
};
constexpr int count = sizeof(sections) / sizeof(*sections);
struct State {
    const Viewer* engines[6]{};
    int section = 0;
    bool index = true, focusIndex = true;
    std::vector<int> history;
    ViewerTarget pending;
    std::string image;
};
State* S;
const Viewer* engine() { return S->engines[sections[S->section].engine]; }
void suspend() {
    if (auto* v = engine()) {
        if (v->command && strcmp(v->id, "gallery"))
            v->command("pause", "");
        if (v->suspended)
            v->suspended();
    }
}
void discard() {
    int e = sections[S->section].engine;
    if (auto* v = S->engines[e]) {
        if (v->closed) v->closed();
        S->engines[e] = nullptr;
    }
}
void select(int section) {
    discard();
    S->section = section;
    S->index = false;
    S->image.clear();
    int e = sections[section].engine;
    logEvent("library: %s (%s)", sections[section].name, ids[e]);
    S->engines[e] = registeredViewer(ids[e]);
    if (engine()->opened) engine()->opened();
    if (*sections[section].source) engine()->command("section", sections[section].source);
    if (engine()->resumed) engine()->resumed();
    resetViewerPosition();
}
void index() {
    discard();
    logEvent("library: index");
    // The cursor back on the page left (S->section), as the game's menus
    // come back to the entry chosen.
    S->index = S->focusIndex = true;
    S->history.clear();
    S->image.clear();
    resetViewerPosition();
}
void opened() { S = new State; }
void closed() {
    for (auto* v : S->engines)
        if (v && v->closed)
            v->closed();
    delete S;
    S = nullptr;
}
bool command(const char*, const char*);
void draw(const Area& area) {
    if (!S->pending.viewer.empty()) {
        ViewerTarget target = std::move(S->pending);
        S->pending = {};
        S->history.push_back(S->section);
        for (const auto& c : target.commands) {
            if (!command(c.name.c_str(), c.argument.c_str())) {
                int previous = S->history.back();
                S->history.pop_back();
                select(previous);
                error("Cannot open linked asset: %s", c.name.c_str());
                break;
            }
        }
    }
    if (S->index) {
        ImVec2 listMax(area.max.x, area.max.y - 60 * area.unit);
        if (beginPane("##sections", area.min, listMax, nullptr, true)) {
            int columns = area.size().x / area.unit >= 440 ? 2 : 1;
            float width = ImGui::GetContentRegionAvail().x / columns;
            float start = ImGui::GetCursorPosX();
            for (int i = 0; i < count; ++i) {
                if (i % columns)
                    ImGui::SameLine(start + width);
                ImGui::PushID(sections[i].id);
                auto& g = *ImGui::GetCurrentContext();
                // shown in the menu's language, its id the English name's (ui/l10n.h)
                std::string label = std::string(tr(sections[i].name)) + "###" + sections[i].name;
                bool focused = g.NavCursorVisible && g.NavId == ImGui::GetID(label.c_str());
                if (ImGui::Selectable(label.c_str(), focused, ImGuiSelectableFlags_NoPadWithHalfSpacing, ImVec2(width - 8 * area.unit, 0))) {
                    select(i);
                    sound(2);
                }
                if (S->index && (ImGui::IsItemFocused() || (ImGui::IsItemHovered() &&
                    (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0))))
                    S->section = i;
                if (S->focusIndex && S->section == i) {
                    ImGui::SetKeyboardFocusHere(-1);
                    ImGui::SetScrollHereY(.5f);
                }
                ImGui::PopID();
            }
            S->focusIndex = false;
        }
        endPane();
        if (S->index) {
            const Section& section = sections[S->section];
            static const char* const descriptions[] = {
                N_("Characters, poses and animation."), N_("Gear models, parts and animation."),
                N_("Actors, arena and battle camera."), N_("Spells, effects and Gear trails."),
                N_("Explore fields and the world map."), N_("Pictures, movies and story scenes.")};
            if (beginPane("##description", ImVec2(area.min.x, listMax.y + 12 * area.unit), area.max, nullptr, true)) {
                ImGui::TextUnformatted(tr(section.name));
                ImGui::TextWrapped("%s", tr(descriptions[section.engine]));
            }
            endPane();
        }
        return;
    }
    if (viewerShortcuts()) {
        int delta = ImGui::IsKeyPressed(ImGuiKey_GamepadR1) - ImGui::IsKeyPressed(ImGuiKey_GamepadL1);
        if (sections[S->section].engine <= 1) {
            int vertical = (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown)) -
                           (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp));
            int horizontal = (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)) -
                             (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft));
            if (vertical || horizontal || delta) {
                ImGui::NavMoveRequestCancel();
                ImGui::SetNavCursorVisibleAfterMove();
            }
            if (delta || vertical) engine()->command("character", std::to_string(delta ? delta : vertical).c_str());
            if (horizontal) engine()->command("cycle", std::to_string(horizontal).c_str());
        } else if (delta && !engine()->command("page", delta > 0 ? "1" : "-1")) {
            // L1/R1 change the open page's tabs (the gallery's, the map
            // viewer's), never the library page (xg-htn.35.93: a player
            // expecting tabs was taken out of the gallery). The effects'
            // sources are that page's tabs: one engine, three sections.
            int e = sections[S->section].engine, next = S->section;
            do next = (next + delta + count) % count;
            while (sections[next].engine != e);
            if (next != S->section) {
                select(next);
                sound(1);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            engine()->command("toggle", "");
            // Space is playback here; prevent its ImGui activation from also
            // opening the selected asset's Options menu.
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
                auto& g = *ImGui::GetCurrentContext();
                g.NavActivateId = g.NavActivateDownId = g.NavActivatePressedId = 0;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false))
            engine()->command("step", "1");
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadR3, false))
            engine()->command("fit", "");
    }
    ImGui::PushID(sections[S->section].id);
    engine()->draw(area);
    ImGui::PopID();
}
bool back() {
    if (S->index)
        return false;
    if (engine()->back && engine()->back())
        return true;
    if (!S->history.empty()) {
        int previous = S->history.back();
        S->history.pop_back();
        select(previous);
    } else index();
    return true;
}
bool command(const char* cmd, const char* arg) {
    if (!strcmp(cmd, "index")) { index(); return true; }
    if (!strcmp(cmd, "section")) {
        for (int i = 0; i < count; ++i)
            if (!strcmp(arg, sections[i].id)) {
                select(i);
                return true;
            }
        return false;
    }
    if (!strcmp(cmd, "family")) {
        for (int e = 0; e < 6; ++e)
            if (!strcmp(arg, ids[e])) {
                for (int i = 0; i < count; ++i)
                    if (sections[i].engine == e) {
                        select(i);
                        S->image.clear();
                        return true;
                    }
            }
        return false;
    }
    if (S->index)
        select(S->section);
    int e = sections[S->section].engine;
    const char* source = nullptr;
    std::string kind;
    if (!strcmp(cmd, "source")) {
        kind = arg;
        kind.resize(kind.find(' ') == std::string::npos ? kind.size() : kind.find(' '));
        source = kind.c_str();
    } else if (e == 1 && (!strcmp(cmd, "field") || !strcmp(cmd, "world")))
        source = !strcmp(cmd, "field") ? "fields" : "world";
    if (source && (e == 0 || e == 1 || e == 3)) {
        if (e == 0 && kind == "ability")
            source = "effects";
        for (int i = 0; i < count; ++i)
            if (sections[i].engine == e && !strcmp(source, sections[i].source)) {
                if (i != S->section) {
                    std::string image = S->image;
                    select(i);
                    S->image = image;
                    if (!image.empty()) engine()->command("image", image.c_str());
                }
                break;
            }
    }
    if (!strcmp(cmd, "image"))
        S->image = arg;
    return engine()->command && engine()->command(cmd, arg);
}
void suspended() { if (!S->index) suspend(); }
void resumed() {
    resetViewerPosition();
    if (!S->index && engine()->resumed) engine()->resumed();
}
bool bookmark(Bookmark& b) { return !S->index && engine()->bookmark && engine()->bookmark(b); }
View* picture() { return !S->index && engine()->picture ? engine()->picture() : nullptr; }
bool thumbnail(std::vector<uint32_t>& rgba) { return !S->index && engine()->thumbnail && engine()->thumbnail(rgba); }
const Viewer viewer = {"visuals",
                       "Visual library",
                       "Sprites, models, battle previews, maps and gallery",
                       20,
                       opened,
                       closed,
                       draw,
                       back,
                       command,
                       suspended,
                       resumed,
                       bookmark,
                       picture,
                       thumbnail};
} // namespace
XG_EXTRAS_VIEWER(viewer);
namespace extras {
const char* visualFamily() { return S && !S->index ? ids[sections[S->section].engine] : ""; }
const char* visualTitle() { return S && !S->index ? sections[S->section].name : N_("Visual library"); } // tr() where shown
bool visualReturn() {
    if (!S || S->history.empty())
        return false;
    int previous = S->history.back();
    S->history.pop_back();
    select(previous);
    return true;
}
bool visualFollow(const ViewerTarget& target) {
    if (!S || S->index || !S->pending.viewer.empty())
        return false;
    if (S->history.size() >= ViewerNavigation::MAX_DEPTH || target.commands.size() > 32)
        return false;
    S->pending = target;
    return true;
}
} // namespace extras
