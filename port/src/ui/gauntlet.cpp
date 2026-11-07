// F1 > Gameplay > Gauntlet (port/src/system/gauntlet.h): the bosses from the
// discs (the extras' reader), the preset or custom list, the party of each
// fight, the recovery, Start, the run's progress and the records.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "l10n.h"
#include "menu.h"
#include "sfx.h"
#include "skin.h"

extern "C" {
#include "system/gauntlet.h"
}

// The extras' boss list and text (absent from a build without them).
extern "C" XgGauntletScan* xgGauntletScanBegin(void) __attribute__((weak));
extern "C" int xgGauntletScanStep(XgGauntletScan* s, int budget) __attribute__((weak));
extern "C" double xgGauntletScanProgress(const XgGauntletScan* s) __attribute__((weak));
extern "C" int xgGauntletScanDiscs(const XgGauntletScan* s) __attribute__((weak));
extern "C" int xgGauntletScanResult(XgGauntletScan* s, PcGauntletBoss** out) __attribute__((weak));
extern "C" void xgGauntletScanFree(XgGauntletScan* s) __attribute__((weak));
extern "C" int xgGauntletName(const unsigned char* text, int size, char* out, int outSize) __attribute__((weak));

namespace menu {

namespace {

struct {
    XgGauntletScan* scan = nullptr;
    bool scanned = false;
    int discs = 0;
    std::vector<PcGauntletBoss> bosses;
    int addChoice = 0;
} G;

void finishScan() {
    PcGauntletBoss* list = nullptr;
    int n = xgGauntletScanResult(G.scan, &list);
    G.discs = xgGauntletScanDiscs(G.scan);
    G.bosses.assign(list, list + (n > 0 ? n : 0));
    free(list);
    xgGauntletScanFree(G.scan);
    G.scan = nullptr;
    G.scanned = true;
}

std::string memberName(const PcGauntletRoster& r, int id) {
    for (int k = 0; k < r.count; k++) {
        if (r.ids[k] != id) continue;
        char name[64] = "";
        if (xgGauntletName != nullptr) xgGauntletName(r.rawNames[k], 20, name, sizeof name);
        if (name[0]) return name;
        break;
    }
    char fallback[16];
    snprintf(fallback, sizeof fallback, tr("Member %d"), id);
    return fallback;
}

std::string timeText(unsigned vblanks) {
    unsigned s = (unsigned)(vblanks / 59.94 + 0.5);
    char t[32];
    snprintf(t, sizeof t, "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
    return t;
}

// A label, then its choices on one line as the game's menus show them (the
// chosen one magenta), or a drop-down list where they do not fit.
int pick(const char* label, const char* id, const char* const* names, int count, int value) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(label));
    ImGui::SameLine(0, 20 * scale());
    float width = (count - 1) * 14 * scale();
    for (int i = 0; i < count; i++) width += ImGui::CalcTextSize(tr(names[i])).x;
    int chosen = value;
    ImGui::PushID(id);
    if (width > ImGui::GetContentRegionAvail().x) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##list", tr(names[value]))) {
            skin::frameWindow();
            for (int i = 0; i < count; i++) {
                if (ImGui::Selectable(tr(names[i]), value == i)) chosen = i;
            }
            ImGui::EndCombo();
        }
    } else {
        for (int i = 0; i < count; i++) {
            if (i) ImGui::SameLine(0, 14 * scale());
            if (skin::selectable(tr(names[i]), value == i, 0, ImGui::CalcTextSize(tr(names[i])))) chosen = i;
        }
    }
    ImGui::PopID();
    if (chosen != value) sfx::play(sfx::CHOOSE);
    return chosen;
}

void tooltip(const char* fmt, ...) IM_FMTARGS(1);
void tooltip(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetMainViewport()->Size.x * 0.6f);
    ImGui::TextV(tr(fmt), ap);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    va_end(ap);
}

// The party pickers of one fight: three lists of the members (a Gear fight:
// those with a Gear) and "-".
bool canFight(const PcGauntletBoss& b, const PcGauntletRoster& r, int id) {
    for (int k = 0; k < r.count; k++) {
        if (r.ids[k] == id) return !b.gear || r.hasGear[k];
    }
    return false;
}

void partyPickers(const PcGauntletBoss& b, const PcGauntletRoster& r, int row) {
    unsigned char party[3];
    if (!pcGauntletPartyFor(&b, party)) memcpy(party, r.party, 3);
    for (int slot = 0; slot < 3; slot++) {
        if (party[slot] != 0xFF && !canFight(b, r, party[slot])) party[slot] = 0xFF; /* as it will fight */
    }
    bool changed = false;
    float width = (ImGui::GetContentRegionAvail().x - 12 * scale()) / 3;
    for (int slot = 0; slot < 3; slot++) {
        ImGui::PushID(row * 4 + slot);
        if (slot) ImGui::SameLine(0, 6 * scale());
        ImGui::SetNextItemWidth(width);
        std::string shown = party[slot] == 0xFF ? "-" : memberName(r, party[slot]);
        if (ImGui::BeginCombo("##member", shown.c_str())) {
            skin::frameWindow();
            if (ImGui::Selectable("-", party[slot] == 0xFF)) {
                party[slot] = 0xFF;
                changed = true;
            }
            for (int k = 0; k < r.count; k++) {
                if (b.gear && !r.hasGear[k]) continue;
                if (ImGui::Selectable(memberName(r, r.ids[k]).c_str(), party[slot] == r.ids[k])) {
                    party[slot] = r.ids[k];
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
    if (changed) {
        sfx::play(sfx::CHOOSE);
        pcGauntletSetPartyFor(&b, party);
    }
}

void fightList(int preset, const PcGauntletRoster& roster) {
    std::vector<int> picks(PC_GAUNTLET_MAX);
    int n = pcGauntletSelect(preset, G.bosses.data(), (int)G.bosses.size(), picks.data(), PC_GAUNTLET_MAX);
    picks.resize(n);
    bool custom = preset == PC_GAUNTLET_CUSTOM;
    if (n == 0) {
        hint(custom ? "No boss yet: add them below." : "No boss of this kind on the discs.");
    } else {
        float height = std::min(n, 4) * 2 * 20.0f * scale() + 8 * scale();
        ImGui::BeginChild("##gauntlet_fights", ImVec2(0, height), ImGuiChildFlags_Borders);
        int move = -1, moveBy = 0, remove = -1;
        for (int i = 0; i < n; i++) {
            const PcGauntletBoss& b = G.bosses[picks[i]];
            unsigned char party[3];
            bool none = pcGauntletFightParty(&b, &roster, party) == 0;
            ImGui::PushID(i);
            if (custom) {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%d. %s%s%s", i + 1, b.name, b.gear ? tr(" (Gear)") : "", none ? tr(" - skipped") : "");
            } else {
                ImGui::TextWrapped("%d. %s%s%s", i + 1, b.name, b.gear ? tr(" (Gear)") : "", none ? tr(" - skipped") : "");
            }
            if (ImGui::IsItemHovered()) {
                tooltip("%s battle; disc %d, field %d, config %d; story %s%d%s", b.gear ? tr("Gear") : tr("Character"),
                        b.disc, b.field, b.config, b.dated ? "" : tr("about "), b.story,
                        none ? tr("; skipped: nobody picked can fight it") : "");
            }
            if (custom) {
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Up")) && i > 0) move = i, moveBy = -1;
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Down")) && i + 1 < n) move = i, moveBy = 1;
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Remove"))) remove = i;
            }
            partyPickers(b, roster, i);
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (move >= 0) {
            std::swap(picks[move], picks[move + moveBy]);
            pcGauntletSetCustom(G.bosses.data(), picks.data(), n);
            sfx::play(sfx::MOVE);
        } else if (remove >= 0) {
            picks.erase(picks.begin() + remove);
            pcGauntletSetCustom(G.bosses.data(), picks.data(), (int)picks.size());
            sfx::play(sfx::BACK);
        }
    }
    if (custom && !G.bosses.empty()) {
        if (G.addChoice >= (int)G.bosses.size()) G.addChoice = 0;
        ImGui::SetNextItemWidth(-60 * scale());
        const PcGauntletBoss& shown = G.bosses[G.addChoice];
        if (ImGui::BeginCombo("##gauntlet_add", shown.name)) {
            skin::frameWindow();
            for (int i = 0; i < (int)G.bosses.size(); i++) {
                ImGui::PushID(i);
                std::string label = std::string(G.bosses[i].name) + (G.bosses[i].gear ? "  (Gear)" : "");
                if (ImGui::Selectable(label.c_str(), i == G.addChoice)) G.addChoice = i;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Add")) && (int)picks.size() < PC_GAUNTLET_MAX) {
            picks.push_back(G.addChoice);
            pcGauntletSetCustom(G.bosses.data(), picks.data(), (int)picks.size());
            sfx::play(sfx::CHOOSE);
        }
    }
}

} // namespace

// The Gauntlet block at the end of the Gameplay section.
namespace {

/* gauntlet.c's preset labels, for the catalogs */
const char* const presetLabels[PC_GAUNTLET_PRESETS] = {N_("Every boss, story order"), N_("Character bosses"),
                                                       N_("Gear bosses"), N_("Custom")};

const char* label(const char* gauntlet) {
    int preset = pcGauntletPresetOf(gauntlet);
    return preset >= 0 ? tr(presetLabels[preset]) : gauntlet;
}

// The records of the cleared runs.
void records() {
    PcGauntletRecord list[6];
    int r = pcGauntletRecords(list, 6);
    if (r > 0) {
        ImGui::Dummy(ImVec2(0, 4 * scale()));
        ImGui::TextUnformatted(tr("Cleared"));
        for (int i = 0; i < r; i++) {
            char line[256];
            snprintf(line, sizeof line, tr("%s: %d fights in %s, %d retries, recovery %s, %s (%s)"),
                     label(list[i].gauntlet), list[i].fights,
                     timeText(list[i].vblanks).c_str(), list[i].retries, list[i].recovery, list[i].difficulty,
                     list[i].when);
            ImGui::Bullet();
            textLines(line);
        }
    }
    const char* path = pcGauntletRecordsPath();
    hint("Records: %s", path ? path : "none (no settings file)");
}

} // namespace

// The Gauntlet block at the end of the Gameplay section.
void gauntletSection() {
    markBlock();
    ImGui::Dummy(ImVec2(0, 6 * scale()));
    ImGui::SeparatorText(tr("Gauntlet"));
    textLines("The story's bosses one after another, each with the party you pick. Afterwards the game is as it "
              "was before: no EXP, items or gold are kept, and no save is touched.");
    int current = 0, count = 0, retries = 0, skipped = 0, quitting = 0;
    int state = pcGauntletStatus(&current, &count, &retries, &skipped, &quitting);
    if (state == PC_GAUNTLET_WAITING) {
        ImGui::Text(tr("%s: %d fights, waiting to start."), label(pcGauntletName()), count);
        if (skipped) ImGui::Text(tr("%d skipped: nobody picked can fight them."), skipped);
        hint("It starts when you are free to move in a field, or on foot or in Gears on the world map.");
        if (ImGui::Button(tr("Cancel"))) {
            sfx::play(sfx::BACK);
            pcGauntletCancel();
        }
        records();
        return;
    }
    if (state == PC_GAUNTLET_RUNNING) {
        ImGui::Text(tr("%s: fight %d of %d"), label(pcGauntletName()), current + 1, count);
        textLines(pcGauntletFightName(current));
        ImGui::Text(tr("Retries: %d"), retries);
        if (quitting) {
            hint("The run ends after this fight; the game is then put back as it was before it.");
        } else if (ImGui::Button(tr("Quit after this fight"))) {
            sfx::play(sfx::BACK);
            pcGauntletCancel();
        }
        records();
        return;
    }
    if (xgGauntletScanBegin == nullptr) {
        hint("Not in this build (it reads the bosses with the extras).");
        return;
    }
    if (!G.scanned) {
        if (G.scan == nullptr) {
            if (ImGui::Button(tr("Find the bosses on the discs"))) {
                sfx::play(sfx::CHOOSE);
                G.scan = xgGauntletScanBegin();
                if (G.scan == nullptr) G.scanned = true;
            }
            records();
            return;
        }
        if (xgGauntletScanStep(G.scan, 24)) {
            finishScan();
        } else {
            ImGui::ProgressBar((float)xgGauntletScanProgress(G.scan), ImVec2(-1, 0), tr("Reading the discs"));
            return;
        }
    }
    if (G.bosses.empty()) {
        hint("No disc image to read the bosses from.");
        return;
    }
    if ((G.discs & 3) != 3) {
        hint("Only disc %d's image was found: the other disc's bosses are missing (System > disc2).",
             G.discs & 1 ? 1 : 2);
    }
    PcGauntletRoster roster;
    pcGauntletRoster(&roster);

    static const char* presetNames[PC_GAUNTLET_PRESETS] = {N_("Story order"), N_("Character"), N_("Gear"), N_("Custom")};
    int preset = pcGauntletPreset();
    int presetPicked = pick("Bosses", "##gauntlet_preset", presetNames, PC_GAUNTLET_PRESETS, preset);
    if (presetPicked != preset) pcGauntletSetPreset(preset = presetPicked);
    hint("%s", label(pcGauntletPresetName(preset)));
    static const char* recoveryNames[PC_GAUNTLET_RECOVERIES] = {N_("Full"), N_("None"), N_("25%"), N_("50%"),
                                                               N_("Revive only")}; /* as gauntlet.c's */
    int recovery = pcGauntletRecoverySetting();
    int recoveryPicked = pick("Recovery", "##gauntlet_recovery", recoveryNames, PC_GAUNTLET_RECOVERIES, recovery);
    if (recoveryPicked != recovery) pcGauntletSetRecovery(recovery = recoveryPicked);
    hint("Between fights: Full restores HP, EP, Gear HP and fuel; None keeps them as the fight left them; "
         "25%% or 50%% of each maximum; Revive only brings a knocked-out member back at 10%% HP. Items used "
         "stay used for the run.");

    if (roster.count == 0) {
        hint("Start a game first: the fights use the party you have.");
        return;
    }
    fightList(preset, roster);
    if (ImGui::Button(tr("Start"))) {
        std::vector<int> picks(PC_GAUNTLET_MAX);
        int n = pcGauntletSelect(preset, G.bosses.data(), (int)G.bosses.size(), picks.data(), PC_GAUNTLET_MAX);
        sfx::play(pcGauntletBegin(pcGauntletPresetName(preset), G.bosses.data(), picks.data(), n, &roster) > 0
                      ? sfx::CHOOSE
                      : sfx::BUZZ);
    }
    hint("It starts when you are free to move in a field, or on foot or in Gears on the world map. A lost fight "
         "offers Retry or Game over; Game over ends the run. The story order is the scripts' estimate.");
    int last = pcGauntletLastResult();
    if (last >= 0) hint(last ? "The last run was cleared." : "The last run ended before its last fight.");
    records();
}

} // namespace menu
