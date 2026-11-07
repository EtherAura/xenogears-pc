// The retail battle loader/renderers run in a private hardware world. The
// controls choose its actors and moves; they never touch the paused game.
#include "ui/l10n.h"
#include "viewer.h"
#include "replace.h"
#include "view.h"
#include "battle_preview.h"
#include "battlefx_assets.h"
#include "entity_names.h"
#include "bookmark_source.h"
#include "picker.h"
#include "export_provenance.h"
#include "raster.h"
#include "battle_moves.h"
#include "bestiary_data.h"
#include <algorithm>
#include <memory>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
extern "C" {
#include "cd/cd.h"
#include "sprites_enemy.h"
#include "sprites_game.h"
#include "tables.h"
#include "writers.h"
}
namespace {
using namespace extras;

// The packs a battle picture can show (the Enhanced look): the texture pack and the
// sprite library's cels, by content (none ships for battle yet: the note says so).
const unsigned battleLibraries = XG_REPLACE_PACK | XG_REPLACE_CELS;
constexpr int LIMIT = 9000; // 5 minutes: the longest turns (Deus) run past 2
const char *const characters[] = {"Fei",      "Elly",    "Citan", "Bart",          "Billy",           "Rico",
                                  "Emeralda", "Chu-Chu", "Maria", "Citan (sword)", "Emeralda (adult)"};
struct Enemy {
    int set, entity;
    std::string label;
};
// What the battle offers (group 0: the menus' and the scripts' moves), a
// pose or Gear program (1), or any other animation the actor can play (2).
enum { M_ATTACK, M_DEATHBLOW, M_INFINITY, M_ETHER, M_SPECIAL, M_TURN, M_POSE, M_RAW, M_COUNT };
struct Move {
    int kind, id;
    std::string label;
    int program = 0;
    int category = M_RAW;
    bool gearTarget = false; // a Gear's deathblow or Infinity: the battle offers it against Gears only
};
struct State {
    XgDisc *disc = nullptr;
    XgBattlePreview *preview = nullptr;
    std::unique_ptr<View> view{new View};
    std::vector<int> arenas;
    std::vector<Enemy> enemies;
    std::vector<Move> moves;
    std::vector<BattleAbility> abilities;
    std::vector<XgBattleTurn> turns; // the enemy's (kind 6 plays turns[id])
    int turnsFound = 0, turnsEvent = 0;
    int character = 0, arena = 0, enemySet = 0, enemy = 0, gear = 0;
    int slot = 0, target = 3, selected = -1, frame = 0, group = 0;
    bool playing = false, full = false, allAbilities = false, enemyActs = false, audio = true;
    bool focused = false; // the focused view (viewer.h): the battle alone across the area
    double clock = 0;
    char filter[96]{};
};
State *S;
std::vector<uint8_t> read(int dir, int file) {
    int size = 0, e = xgDiscFileEntry(S->disc, dir, file);
    if (e < 0 || xgDiscEntry(S->disc, e).size <= 0 || xgDiscEntry(S->disc, e).size > 8 * 1024 * 1024)
        return {};
    auto *p = xgDiscReadEntry(S->disc, e, &size);
    std::vector<uint8_t> out;
    if (p)
        out.assign(p, p + size);
    free(p);
    return out;
}
void pause() {
    S->playing = false;
    S->clock = 0;
    xgBattlePreviewAudio(S->preview, 0);
}
void refresh() {
    if (!S->preview)
        return;
    S->view->show(xgBattlePreviewGpu(S->preview));
    int y = xgBattlePreviewScreenY(S->preview);
    S->view->setScene(0, y, 320, y + 224);
}
std::string printed(const char *fmt, ...) {
    char text[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    return text;
}
std::string named(const char *withName, const char *numbered, const std::vector<std::string> &names, int i) {
    return i < int(names.size()) && !names[i].empty() ? printed(withName, names[i].c_str()) : printed(numbered, i + 1);
}
// A party member's or Gear's commands as its battle menus offer them
// (battle_moves.h): the entries it can ever learn, by the battle's names.
void partyMoves(const int info[8], const BattleNames &names) {
    int types[41], flags[41];
    int count = xgBattlePreviewCommands(S->preview, S->slot, types, flags);
    const XgBattleLearnable &learn = names.learn;
    if (!info[1]) {
        for (int e = 0; e < 7 && e < count; e++) {
            int place, cost = xgBattleAttackCost(e, &place);
            S->moves.push_back({5, e,
                                cost == 3 ? std::string(tr("Attack: 3 AP"))
                                          : printed(tr("Attack: %d AP, hit %d"), cost, place),
                                0, M_ATTACK});
        }
        for (int d = 0; d < learn.deathblows && 7 + d < count; d++)
            S->moves.push_back({5, 7 + d, named(tr("Deathblow: %s"), tr("Deathblow %d"), names.deathblows, d), 0,
                                M_DEATHBLOW});
        for (int n = 0; n < learn.ethers && 22 + n < count; n++)
            S->moves.push_back({5, 22 + n, named(tr("Ether: %s"), tr("Ether %d"), names.spells, n), 0, M_ETHER});
        return;
    }
    // an entry plays the Gear's program of its attack type: one this Gear has
    for (int e = 0; e < count; e++)
        if ((types[e] & 0xFF) >= info[2])
            types[e] = -1;
    auto plays = [&](int e) { return e < count && types[e] >= 0; };
    for (int level = 0; level < 3 && 15 + level < count; level++) {
        if (plays(level))
            S->moves.push_back({5, level, printed(tr("Attack: level %d"), level + 1), 0, M_ATTACK});
        if (plays(15 + level))
            S->moves.push_back({5, 15 + level, printed(tr("Attack: level %d, target on foot"), level + 1), 0, M_ATTACK});
    }
    std::vector<std::string> gearMoves(names.gearMoves.begin() + std::min<size_t>(3, names.gearMoves.size()),
                                       names.gearMoves.end());
    // Combos against Gears only (updateGearDeathblowComboInput: a target on
    // foot gets the plain attack; their programs wait for a Gear's answer)
    for (int i = 0; i < learn.gearDeathblows && 3 + i < count; i++)
        if (plays(3 + i))
            S->moves.push_back(
                {5, 3 + i, named(tr("Deathblow: %s"), tr("Deathblow %d"), gearMoves, i), 0, M_DEATHBLOW, true});
    std::vector<std::string> infinity(gearMoves.begin() + std::min<size_t>(9, gearMoves.size()), gearMoves.end());
    for (int i = 0; i < 3 && 12 + i < count; i++)
        if (plays(12 + i) && (types[12 + i] & 0xFF) >= 0x44 && (types[12 + i] & 0xFF) <= 0x46)
            S->moves.push_back(
                {5, 12 + i, named(tr("Infinity: %s"), tr("Infinity %d"), infinity, i), 0, M_INFINITY, true});
    // the Gear's Ethers: its table's leading named entries (battle_moves.h),
    // none for a pilot who learns none (Maria: the JP Gear 17/18 tables hold
    // placeholder names there)
    for (int n = 0; learn.gearEthers && n < 16 && 21 + n < count && types[21 + n] &&
                    n < int(names.gearEthersUsed.size()) && names.gearEthersUsed[n];
         n++)
        if (plays(21 + n))
            S->moves.push_back({5, 21 + n, named(tr("Ether: %s"), tr("Ether %d"), names.gearEthers, n), 0, M_ETHER});
    for (int n = 0; n < 4 && 37 + n < count; n++)
        if (plays(37 + n) && (types[37 + n] & 0xFF) >= 0x47 && (types[37 + n] & 0xFF) <= 0x4A &&
            n < int(names.gearSpecialsUsed.size()) && names.gearSpecialsUsed[n])
            S->moves.push_back({5, 37 + n, named(tr("Special: %s"), tr("Special %d"), names.gearSpecials, n), 0, M_SPECIAL});
}
// The enemy's turns as its scripts queue them (battle_moves.h). A Gear's
// turn plays programs of its own table: one the table lacks comes from its
// battle event's extra file (the last bosses), which this scene has not run.
// A Gear's turns are played against a party Gear, as its battles have it.
void enemyMoves(const int info[8]) {
    int size = 0, entry = xgDiscFileEntry(S->disc, 0x0D, 2 + 2 * S->enemySet);
    uint8_t *set = entry >= 0 ? xgDiscReadEntry(S->disc, entry, &size) : nullptr;
    S->turns.assign(256, XgBattleTurn{});
    int n = xgBattleTurns(set, size, S->enemy, S->turns.data(), int(S->turns.size()), &S->turnsFound);
    free(set);
    S->turns.resize(std::max(0, n));
    for (int i = 0; i < int(S->turns.size()); i++) {
        const XgBattleTurn &t = S->turns[i];
        bool event = false;
        for (int k = 0; k < t.count && info[1]; k++)
            event |= (t.actions[k][0] == XG_ACT_ATTACK && t.actions[k][2] >= info[2]) ||
                     (t.actions[k][0] == XG_ACT_ANIMATION && t.actions[k][4] >= info[2]);
        if (event) { // listed nowhere; its index stays (bookmarks, the bestiary)
            S->turnsEvent++;
            continue;
        }
        char name[64] = "";
        if (t.nameIndex >= 0)
            xgBeastSetString(S->disc, S->enemySet, t.nameIndex, name, sizeof(name));
        std::string steps;
        for (int k = 0; k < t.count; k++) {
            const uint8_t *a = t.actions[k];
            std::string step = a[0] == XG_ACT_MOVE && !(a[4] | a[5]) ? std::string(tr("approach"))
                               : a[0] == XG_ACT_ATTACK                ? printed(tr("attack %d"), a[2])
                               : a[0] == XG_ACT_ABILITY ? printed(tr("ability %d"), (a[4] | a[5] << 8) & 0x1FF)
                               : a[0] == XG_ACT_ANIMATION ? printed(tr("animation %d"), a[4])
                                                          : std::string();
            if (!step.empty())
                steps += (steps.empty() ? "" : ", ") + step;
        }
        std::string label = *name && !strchr(name, '"') ? std::string(name) : printed(tr("Turn %d"), i + 1);
        label += " (" + steps + ")";
        if (t.script == 2)
            label = printed(tr("%s, when attacked"), label.c_str());
        else if (t.script == 3)
            label = printed(tr("%s, after the party's turn"), label.c_str());
        S->moves.push_back({6, i, label, 0, M_TURN, info[1] != 0});
    }
}
void catalog() {
    S->moves.clear();
    S->turns.clear();
    S->turnsFound = S->turnsEvent = 0;
    S->selected = -1;
    int info[8];
    if (!xgBattlePreviewInfo(S->preview, S->slot, info))
        return;
    char label[96];
    BattleNames names;
    if (S->slot < 3) {
        names = battleNames(S->disc, S->character, info[6], info[1] ? info[7] : -1);
        partyMoves(info, names);
    } else
        enemyMoves(info);
    // Poses and Gear programs; those the battle plays itself are named
    // (b_800B7C28.c: a Gear's turn starts with 0x1A and walks with 2, an
    // enemy Gear's ends with 0x1B, 0xFE returns with 4, cancelJumpAnim
    // 0x1F; a party sprite returns with 4, casts with 0x11 (0x1C for its
    // deathblow abilities), counters with 0x1B), and a Gear's command
    // programs by their command's name.
    std::vector<Move> poses;
    int types[41], flags[41], count = info[1] && S->slot < 3 ? xgBattlePreviewCommands(S->preview, S->slot, types, flags) : 0;
    for (int id = 0; id < info[2]; id++) {
        snprintf(label, sizeof(label), info[1] ? tr("Gear move %02d") : tr("Pose %02d"), id);
        std::string text = label;
        const char *role = nullptr;
        if (info[1])
            role = id == 2 ? tr("approach") : id == 4 ? tr("return") : id == 0x1A ? tr("turn start")
                   : id == 0x1B ? tr("turn end") : id == 0x1F ? tr("cancel") : nullptr;
        else if (S->slot < 3)
            role = id == 4 ? tr("return") : id == 0x11 ? tr("Ether casting") : id == 0x1B ? tr("counterattack")
                   : id == 0x1C ? tr("deathblow casting") : nullptr;
        if (role)
            text += std::string(": ") + role;
        else
            for (const auto &m : S->moves)
                if (m.kind == 5 && m.id < count && (types[m.id] & 0xFF) == id) {
                    text += " (" + m.label + ")";
                    break;
                }
        poses.push_back({info[1] ? 3 : S->slot >= 3 ? 4 : 0, id, text, 0, M_POSE});
    }
    // Every animation of the party member's special file and of the ability
    // banks (the ones its moves pick are named above).
    if (S->slot < 3 && !info[1]) {
        for (int id = 0; id < 14; id++) {
            snprintf(label, sizeof(label), tr("Special %02d"), id);
            std::string name =
                id >= 7 && id - 7 < int(names.deathblows.size()) ? names.deathblows[id - 7] : "";
            S->moves.push_back({1, id, name.empty() ? label : name, 0, M_RAW});
        }
        for (const auto &ability : S->abilities) {
            if (!S->allAbilities && (ability.id < info[4] || ability.id >= info[5] + 16))
                continue;
            for (int program = 0; program < ability.programs; program++) {
                auto text = ability.name;
                if (ability.programs > 1)
                    text += " / " + std::to_string(program + 1);
                S->moves.push_back({2, ability.id, std::move(text), program, M_RAW});
            }
        }
    }
    S->moves.insert(S->moves.end(), poses.begin(), poses.end());
}
bool build() {
    pause();
    XgBattlePreview *preview =
        xgBattlePreviewCreate(S->disc, S->character, S->arena, S->enemySet, S->gear, S->enemy);
    if (!preview) {
        error("This arena or actor combination could not load");
        return false;
    }
    xgBattlePreviewDestroy(S->preview);
    S->preview = preview;
    xgBattlePreviewPresentation(S->preview, S->slot, S->full);
    S->frame = 0;
    catalog();
    refresh();
    return true;
}
// Start a move on the current scene: a turn (kind 6) runs its actions, the
// target mask of each the target, or the enemy itself when its scripts aim
// it at an enemy slot.
bool start(int kind, int id, int program) {
    auto move = std::find_if(S->moves.begin(), S->moves.end(), [&](const Move &m) {
        return m.kind == kind && m.id == id && m.program == program;
    });
    int target[8];
    if (move != S->moves.end() && move->gearTarget &&
        !(xgBattlePreviewInfo(S->preview, S->target, target) && target[1])) {
        if (kind == 6)
            error("A Gear's turn is played against a Gear: choose Gear or Omnigear for the party");
        else
            error("This Gear move is used against Gears: choose a Gear enemy");
        return false;
    }
    if (kind != 6)
        return xgBattlePreviewPlay(S->preview, S->slot, S->target, kind, id, program, S->full);
    if (id < 0 || id >= int(S->turns.size()))
        return false;
    if (move == S->moves.end()) {
        error("This turn plays programs its battle event loads: the scene cannot show it");
        return false;
    }
    const XgBattleTurn &t = S->turns[id];
    uint8_t actions[XG_TURN_ACTIONS][8];
    for (int k = 0; k < t.count; k++) {
        memcpy(actions[k], t.actions[k], 8);
        int mask = actions[k][6] | actions[k][7] << 8;
        mask = !(t.variable[k] & 0xC0) && mask && !(mask & 7) ? 1 << S->slot : 1 << S->target;
        actions[k][6] = uint8_t(mask);
        actions[k][7] = uint8_t(mask >> 8);
    }
    return xgBattlePreviewTurn(S->preview, S->slot, S->target, actions[0], t.count, S->full);
}
bool play(int index) {
    if (index < 0 || index >= int(S->moves.size()))
        return false;
    Move move = S->moves[index];
    // Recreate the native world: no unfinished effect task, camera move,
    // damage state or sound from the last choice can leak into this one.
    if (!build())
        return false;
    auto found = std::find_if(S->moves.begin(), S->moves.end(), [&](const Move &m) {
        return m.kind == move.kind && m.id == move.id && m.program == move.program;
    });
    if (found == S->moves.end())
        return false;
    if (!start(move.kind, move.id, move.program)) {
        if (!found->gearTarget)
            error("Move %d could not start with these actors", move.id);
        return false;
    }
    S->selected = int(found - S->moves.begin());
    S->frame = 0;
    S->clock = 0;
    return true;
}
void step(int ticks) {
    if (!S->preview)
        return;
    ticks = std::clamp(ticks, 0, LIMIT - S->frame);
    if (ticks) {
        xgBattlePreviewStep(S->preview, ticks);
        S->frame += ticks;
        refresh();
        if (xgBattlePreviewFailed(S->preview)) {
            pause();
            error("Move exceeded the preview limits");
        }
    }
    if (S->frame == LIMIT)
        pause();
}
void seek(int frame) {
    int index = S->selected;
    pause();
    if (play(index))
        step(frame);
}
void toggle() {
    if (!S->preview && !build())
        return;
    if (S->playing)
        pause();
    else {
        S->playing = true;
        S->clock = 0;
        xgBattlePreviewAudio(S->preview, S->full && S->audio);
    }
}
bool background(int delta) {
    auto i = std::find(S->arenas.begin(), S->arenas.end(), S->arena);
    if (i == S->arenas.end() || S->arenas.empty())
        return false;
    int index = int(i - S->arenas.begin());
    int selected = S->selected, frame = S->frame;
    bool playing = S->playing;
    S->arena = S->arenas[(index + delta + int(S->arenas.size())) % S->arenas.size()];
    if (selected >= 0) {
        if (!play(selected))
            return false;
        step(frame);
    } else if (!build())
        return false;
    if (playing)
        toggle();
    return true;
}
bool image(const char *path) {
    const char *why = nullptr;
    XgDisc *d = xgDiscOpen(path, &why);
    if (!d) {
        error("%s", why ? why : "Cannot read disc");
        return false;
    }
    pause();
    xgBattlePreviewDestroy(S->preview);
    S->preview = nullptr;
    xgDiscClose(S->disc);
    S->disc = d;
    S->enemies.clear();
    S->arenas.clear();
    S->moves.clear();
    S->selected = -1;
    S->abilities = battleAbilities(d);
    for (int arena = 0; arena < 75; arena++) {
        auto bytes = read(0x0F, 6 + 2 * arena);
        if (bytes.size() >= 24 && bytes[0] == 5)
            S->arenas.push_back(arena);
    }
    EntityNames names(d);
    for (int set = 0; 3 + 2 * set <= xgDiscDirFiles(d, 0x0D); set++) {
        auto bytes = read(0x0D, 3 + 2 * set);
        for (int entity = 0; entity < xgEnemySpriteCount(bytes.data(), int(bytes.size())); entity++) {
            XgEnemySprite sprite;
            int kind = xgEnemySpriteGet(bytes.data(), int(bytes.size()), entity, &sprite);
            if (!kind)
                continue;
            char label[192];
            std::string name = names.enemy(set, entity);
            snprintf(label, sizeof(label), "set %03d / %d", set, entity);
            S->enemies.push_back({set, entity, entityLabel(name, label) + (kind < 0 ? tr(" (Gear)") : "")});
        }
    }
    if (!S->arenas.empty())
        S->arena = S->arenas.front();
    if (!S->enemies.empty()) {
        S->enemySet = S->enemies.front().set;
        S->enemy = S->enemies.front().entity;
    }
    return true;
}
void exportPng() {
    if (!S->preview)
        return;
    std::vector<uint32_t> pixels(320 * 224);
    xgVgpuRaster(xgBattlePreviewGpu(S->preview), 0, xgBattlePreviewScreenY(S->preview), 320, 224, 1,
                 0xFF000000, pixels.data());
    char file[96];
    snprintf(file, sizeof(file), "%s%d/battle/actor%d_move%d_frame%d.png", xgDiscRegion(S->disc),
             xgDiscNumber(S->disc), S->character, S->selected, S->frame);
    auto path = exportPath(file);
    if (!xgPngWrite(path.c_str(), pixels.data(), 320, 224)) {
        error("%s", xgWriterError());
        return;
    }
    std::vector<ExportSource> sources{exportSource(S->disc, 0x0F, 6 + 2 * S->arena, "arena"),
                                      exportSource(S->disc, 0x0D, 3 + 2 * S->enemySet, "enemy bank")};
    int info[8];
    if (S->slot < 3 && xgBattlePreviewInfo(S->preview, S->slot, info) && !info[1]) {
        sources.push_back(exportSource(S->disc, 0x2D, info[3] - 17, "normal party sprites"));
        if (S->selected >= 0) {
            const Move &m = S->moves[S->selected];
            if (m.kind == 1)
                sources.push_back(exportSource(S->disc, 0x2D, info[3], "party specials"));
            if (m.kind == 2) {
                sources.push_back(exportSource(S->disc, 0x0E, 0x22 + 2 * m.id, "ability controller"));
                sources.push_back(exportSource(S->disc, 0x0E, 0x23 + 2 * m.id, "ability texture"));
            }
        }
    }
    recordExport(path, "battle", sources,
                 {{"actor", std::to_string(S->character)},
                  {"move", std::to_string(S->selected)},
                  {"frame", std::to_string(S->frame)},
                  {"presentation", S->full ? "battle" : "actor"}});
    status("Saved battle preview: %s", path.c_str());
}
void opened() {
    S = new State;
    S->view->setInteractive(false);
    S->view->setWholeFit(false); // a battle screen: fills the focused view
    for (int number : {1, 2}) {
        for (int i = 0; i < 2; ++i) {
            const char* p = cdSwapPath(i);
            XgDisc* d = p && *p ? xgDiscOpen(p, nullptr) : nullptr;
            bool use = d && xgDiscNumber(d) == number;
            xgDiscClose(d);
            if (use && image(p)) return;
        }
    }
}
void closed() {
    pause();
    xgBattlePreviewDestroy(S->preview);
    xgDiscClose(S->disc);
    delete S;
    S = nullptr;
}
void battleOptions();
void setFocused(bool on);
void focusedView(const Area& a);

void draw(const Area &a) {
    if (viewerShortcuts()) {
        int delta =
            ImGui::IsKeyPressed(ImGuiKey_GamepadR2, false) - ImGui::IsKeyPressed(ImGuiKey_GamepadL2, false);
        if (delta) {
            background(delta);
            sound(1);
        }
    }
    if (S->playing) {
        S->clock += std::min(.1f, ImGui::GetIO().DeltaTime) * xgSpriteTickRate(2);
        int ticks = int(S->clock);
        S->clock -= ticks;
        if (ticks)
            step(ticks);
    }
    if (S->focused) {
        focusedView(a);
        return;
    }
    bool focus = false;
    float split = std::min(165 * a.unit, a.size().x * .32f);
    if (beginPane("battlelist", a.min, ImVec2(a.min.x + split, a.max.y), "Scene + moves")) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##actor", characters[S->character])) {
            for (int i = 0; i < 11; i++)
                if (ImGui::Selectable(characters[i], S->character == i)) {
                    S->character = i;
                    pause();
                }
            ImGui::EndCombo();
        }
        const char *forms[] = {tr("Character"), tr("Gear"), tr("Omnigear")};
        if (S->character == 7)
            S->gear = 0;
        ImGui::BeginDisabled(S->character == 7);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##form", &S->gear, forms, 3))
            pause();
        ImGui::EndDisabled();
        char label[96];
        snprintf(label, sizeof(label), tr("Arena %02d"), S->arena);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##arena", label)) {
            for (int i : S->arenas) {
                snprintf(label, sizeof(label), tr("Arena %02d"), i);
                if (ImGui::Selectable(label, S->arena == i)) {
                    S->arena = i;
                    pause();
                }
            }
            ImGui::EndCombo();
        }
        snprintf(label, sizeof(label), tr("Enemy %03d / %d"), S->enemySet, S->enemy);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##enemy", label)) {
            for (const auto &e : S->enemies)
                if (ImGui::Selectable(e.label.c_str(), S->enemySet == e.set && S->enemy == e.entity)) {
                    S->enemySet = e.set;
                    S->enemy = e.entity;
                    pause();
                }
            ImGui::EndCombo();
        }
        if (ImGui::Checkbox(tr("Enemy acts"), &S->enemyActs)) {
            S->slot = S->enemyActs ? 3 : 0;
            S->target = S->enemyActs ? 0 : 3;
            catalog();
            pause();
            xgBattlePreviewPresentation(S->preview, S->slot, S->full);
            refresh();
        }
        if (ImGui::Button(tr("Load scene"), ImVec2(-1, 0)))
            build();
        ImGui::Separator();
        const char *groups[] = {tr("Moves"), tr("Poses"), tr("All animations")};
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##group", &S->group, groups, 3);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", tr("Find a move"), S->filter, sizeof(S->filter));
        for (int i = 0; i < int(S->moves.size()); i++) {
            const auto &move = S->moves[i];
            if ((S->group == 0 && move.category >= M_POSE) || (S->group == 1 && move.category != M_POSE))
                continue;
            if (*S->filter && move.label.find(S->filter) == std::string::npos)
                continue;
            ImGui::PushID(i);
            if (listItem(move.label.c_str(), S->selected == i) && play(i)) {
                toggle();
                focus = true; // the move across the area once this frame is drawn
            }
            ImGui::PopID();
        }
        if (!S->preview)
            ImGui::TextWrapped(tr("Choose an arena and actors, then load the scene."));
    }
    endPane();
    if (beginPane("battleview", ImVec2(a.min.x + split + 8 * a.unit, a.min.y), a.max, "Battle preview",
                  false)) {
        if (S->selected >= 0 && S->selected < int(S->moves.size()))
            ImGui::TextWrapped("%s", S->moves[S->selected].label.c_str());
        if (ImGui::Checkbox(tr("Full battle"), &S->full)) {
            if (S->selected >= 0)
                seek(0);
            else {
                pause();
                xgBattlePreviewPresentation(S->preview, S->slot, S->full);
                refresh();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Options / export")))
            ImGui::OpenPopup("battleoptions");
        if (S->preview) {
            ImGui::SameLine();
            if (ImGui::Button(tr("Full view")))
                focus = true;
        }
        battleOptions();
        float bottom = ImGui::GetFrameHeightWithSpacing() * 2 + 6 * a.unit;
        S->view->setReplacements(lookLibraries(battleLibraries));
        if (S->preview)
            S->view->draw(ImVec2(ImGui::GetContentRegionAvail().x,
                                 std::max(1.f, ImGui::GetContentRegionAvail().y - bottom)));
        else
            ImGui::Dummy(ImVec2(1, std::max(1.f, ImGui::GetContentRegionAvail().y - bottom)));
        if (ImGui::Button(S->playing ? tr("Pause") : tr("Play")))
            toggle();
        ImGui::SameLine();
        if (ImGui::Button(tr("Step"))) {
            pause();
            step(1);
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Restart")))
            seek(0);
        ImGui::SameLine();
        if (ImGui::Button(tr("Fit")))
            S->view->fit();
        int frame = S->frame;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##frame", &frame, 0, LIMIT, tr("Frame %d")))
            seek(frame);
    }
    endPane();
    if (focus) setFocused(true);
}

// The Options window ("battleoptions", opened in the current window) and its disc picker.
void battleOptions() {
    if (commandPopup("battleoptions")) {
        if (ImGui::Button(tr("Bookmark"))) bookmarkSelection();
        if (ImGui::Checkbox(tr("Include all ability banks"), &S->allAbilities))
            catalog();
        if (ImGui::Checkbox(tr("Battle audio"), &S->audio))
            xgBattlePreviewAudio(S->preview, S->playing && S->full && S->audio);
        lookOptions(battleLibraries, lookCovered(S->view.get(), battleLibraries));
        if (ImGui::Button(tr("Open disc image...")))
            openPicker("battledisc", "Open a disc image", "", ".cue;.bin", false);
        if (ImGui::Button(tr("Export PNG")))
            exportPng();
        ImGui::TextWrapped(tr("L2/R2: previous/next arena. X: play/pause. Y: one frame. R3: fit."));
        ImGui::EndPopup();
    }
    std::string chosen;
    if (picker("battledisc", &chosen))
        image(chosen.c_str());
}

// ---- the focused view: the battle alone across the area, its controls in a bar --------------------

void setFocused(bool on) {
    if (on == S->focused || (on && !S->preview)) return;
    S->focused = on;
    S->view->fit(); // the new space
    if (on) focusEnter();
    else focusLeave();
}

void focusedView(const Area& a) {
    if (!S->preview) { setFocused(false); return; }
    FocusLayout f = focusLayout(a, 1);
    if (beginPane("battlefocus", f.min, f.max, nullptr, false)) {
        S->view->setReplacements(lookLibraries(battleLibraries));
        S->view->draw(ImGui::GetContentRegionAvail());
        bool move = S->selected >= 0 && S->selected < int(S->moves.size());
        focusPicture(move ? S->moves[S->selected].label.c_str() : nullptr);
    }
    endPane();
    if (beginFocusBar(f)) setFocused(false);
    if (focusCheckbox("Full battle", &S->full)) {
        if (S->selected >= 0)
            seek(0);
        else {
            pause();
            xgBattlePreviewPresentation(S->preview, S->slot, S->full);
            refresh();
        }
    }
    if (focusButton(S->playing ? "Pause" : "Play")) toggle();
    if (focusButton("Step")) {
        pause();
        step(1);
    }
    if (focusButton("Restart")) seek(0);
    focusText(tr("Frame %d"), S->frame);
    focusZoom(S->view.get(), 1); // 1:1: one battle-screen pixel per window pixel
    focusLook(battleLibraries, lookCovered(S->view.get(), battleLibraries));
    if (focusButton("Options")) ImGui::OpenPopup("battleoptions");
    battleOptions();
    endFocusBar();
}

bool back() {
    if (!S->focused) return false;
    setFocused(false);
    return true;
}
bool command(const char *cmd, const char *arg) {
    if (!strcmp(cmd, "image"))
        return image(arg);
    if (!strcmp(cmd, "focus")) {
        bool on = strcmp(arg, "0") != 0;
        setFocused(on);
        return S->focused == on;
    }
    if (!strcmp(cmd, "setup")) {
        int actor = 0, arena = 0, set = 0, gear = 0, enemy = 0;
        if (sscanf(arg, "%d %d %d %d %d", &actor, &arena, &set, &gear, &enemy) < 3 || actor < 0 ||
            actor > 10 || arena < 0 || arena >= 75 || set < 0 || set > 255 || gear < 0 || gear > 2 ||
            enemy < 0 || enemy >= 8)
            return false;
        S->character = actor;
        S->arena = arena;
        S->enemySet = set;
        S->gear = gear;
        S->enemy = enemy;
        return build();
    }
    if (!strcmp(cmd, "move")) {
        int slot = 0, target = 3, kind = 1, id = 0, program = 0, full = 1;
        if (sscanf(arg, "%d %d %d %d %d %d", &slot, &target, &kind, &id, &program, &full) != 6 || slot < 0 ||
            slot > 10 || target < 0 || target > 10 || kind < 0 || kind > 6 || id < 0 || id > 255 ||
            program < 0 || program > 63 || full < 0 || full > 1)
            return false;
        if (!build())
            return false;
        S->slot = slot;
        S->target = target;
        S->enemyActs = slot >= 3;
        S->full = full;
        S->allAbilities = true;
        catalog();
        for (int i = 0; i < int(S->moves.size()); i++)
            if (S->moves[i].kind == kind && S->moves[i].id == id && S->moves[i].program == program) {
                S->selected = i;
                break;
            }
        return start(kind, id, program);
    }
    if (!strcmp(cmd, "presentation")) {
        int slot, target, full;
        if (sscanf(arg, "%d %d %d", &slot, &target, &full) != 3 || slot < 0 || slot > 10 || target < 0 ||
            target > 10 || full < 0 || full > 1)
            return false;
        S->slot = slot;
        S->target = target;
        S->full = full;
        S->enemyActs = slot >= 3;
        pause();
        catalog();
        int ok = xgBattlePreviewPresentation(S->preview, slot, full);
        refresh();
        return ok;
    }
    if (!strcmp(cmd, "anim"))
        return play(atoi(arg));
    if (!strcmp(cmd, "seek")) {
        seek(atoi(arg));
        return true;
    }
    if (!strcmp(cmd, "step")) {
        pause();
        step(atoi(arg));
        return true;
    }
    if (!strcmp(cmd, "arena"))
        return background(atoi(arg));
    if (!strcmp(cmd, "finish")) {
        // step until the move's native sequence ends (or `arg` frames)
        int limit = std::clamp(atoi(arg), 1, LIMIT);
        pause();
        while (S->frame < limit && S->preview && xgBattlePreviewActive(S->preview) && !xgBattlePreviewFailed(S->preview))
            step(std::min(10, limit - S->frame));
        bool done = S->preview && !xgBattlePreviewActive(S->preview) && !xgBattlePreviewFailed(S->preview);
        const Move *m = S->selected >= 0 ? &S->moves[S->selected] : nullptr;
        fprintf(stderr, "battle finish: actor %d form %d enemy %d/%d move %d %d %d: %s, frame %d%s, %d primitives: %s\n",
                S->character, S->gear, S->enemySet, S->enemy, m ? m->kind : -1, m ? m->id : -1, m ? m->program : -1,
                done ? "ended" : "running", S->frame, xgBattlePreviewFailed(S->preview) ? ", failed" : "",
                S->preview ? xgVgpuPrimCount(xgBattlePreviewGpu(S->preview)) : 0, m ? m->label.c_str() : "");
        return true;
    }
    if (!strcmp(cmd, "info")) {
        xgBattlePreviewDump(S->preview);
        fprintf(stderr, "battle preview: %d moves, frame %d, %d primitives, screenY %d\n",
                int(S->moves.size()), S->frame,
                (S->preview ? xgVgpuPrimCount(xgBattlePreviewGpu(S->preview)) : 0),
                xgBattlePreviewScreenY(S->preview));
        return true;
    }
    if (!strcmp(cmd, "catalog")) {
        int counts[M_COUNT] = {};
        for (const auto &m : S->moves) {
            fprintf(stderr, "battle move: %d %d %d %s\n", m.kind, m.id, m.program, m.label.c_str());
            counts[m.category]++;
        }
        fprintf(stderr,
                "battle catalog: actor %d form %d slot %d enemy %d/%d: %d attacks, %d deathblows, %d infinity, "
                "%d ethers, %d specials, %d turns (%d seen, %d need their battle event), %d poses, %d other animations\n",
                S->character, S->gear, S->slot, S->enemySet, S->enemy, counts[M_ATTACK], counts[M_DEATHBLOW],
                counts[M_INFINITY], counts[M_ETHER], counts[M_SPECIAL], counts[M_TURN], S->turnsFound, S->turnsEvent,
                counts[M_POSE], counts[M_RAW]);
        return true;
    }
    if (!strcmp(cmd, "pause")) {
        pause();
        return true;
    }
    if (!strcmp(cmd, "toggle")) {
        toggle();
        return true;
    }
    if (!strcmp(cmd, "fit")) {
        S->view->fit();
        return true;
    }
    if (!strcmp(cmd, "export")) {
        exportPng();
        return true;
    }
    return false;
}
bool bookmark(Bookmark &b) {
    if (!S->preview || !bookmarkSource(b, S->disc, "battle",
                                       S->selected >= 0 ? S->moves[S->selected].label : "Battle scene"))
        return false;
    char value[128];
    snprintf(value, sizeof(value), "%d %d %d %d %d", S->character, S->arena, S->enemySet, S->gear, S->enemy);
    b.target.commands.push_back({"setup", value});
    if (S->selected >= 0) {
        const Move &m = S->moves[S->selected];
        snprintf(value, sizeof(value), "%d %d %d %d %d %d", S->slot, S->target, m.kind, m.id, m.program,
                 S->full);
        b.target.commands.push_back({"move", value});
        b.target.commands.push_back({"seek", std::to_string(S->frame)});
    } else {
        snprintf(value, sizeof(value), "%d %d %d", S->slot, S->target, S->full);
        b.target.commands.push_back({"presentation", value});
    }
    b.target.commands.push_back({"pause", ""});
    return true;
}
View *picture() {
    return S->preview ? S->view.get() : nullptr;
}
const Viewer viewer = {"battle", "Battle preview", "Native battle arena, actors and moves",
                       44,       opened,           closed,
                       draw,     back,             command,
                       pause,    nullptr,          bookmark,
                       picture};
} // namespace
XG_EXTRAS_VIEWER(viewer);
