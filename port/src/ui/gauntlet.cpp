// Boss gauntlet setup, progress and records in F1 > Extras.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"
#include "arena_saves.h"
#include "wrapped.h"
#include "l10n.h"
#include "menu.h"
#include "sfx.h"
#include "skin.h"

extern "C" {
#include "system/gauntlet.h"
#include "cd/cd.h"
}

// The disc scanner is available in builds with Extras.
extern "C" XgGauntletScan* xgGauntletScanBegin(void) __attribute__((weak));
extern "C" int xgGauntletScanStep(XgGauntletScan* s, int budget) __attribute__((weak));
extern "C" int xgGauntletScanDiscs(const XgGauntletScan* s) __attribute__((weak));
extern "C" int xgGauntletScanResult(XgGauntletScan* s, PcGauntletBoss** out) __attribute__((weak));
extern "C" void xgGauntletScanFree(XgGauntletScan* s) __attribute__((weak));

namespace menu {

namespace {

struct {
    XgGauntletScan* scan = nullptr;
    bool scanned = false;
    int discs = 0;
    std::string paths[2];
    std::vector<PcGauntletBoss> bosses;
} G;

void scanBosses() {
    const char* first = cdSwapPath(0);
    const char* second = cdSwapPath(1);
    std::string paths[] = {first ? first : "", second ? second : ""};
    if (paths[1] < paths[0]) std::swap(paths[0], paths[1]);
    if (paths[0] != G.paths[0] || paths[1] != G.paths[1]) {
        if (G.scan != nullptr) xgGauntletScanFree(G.scan);
        G.scan = nullptr;
        G.scanned = false;
        G.discs = 0;
        G.bosses.clear();
        G.paths[0] = paths[0];
        G.paths[1] = paths[1];
    }
    if (G.scanned) return;
    if (G.scan == nullptr) G.scan = xgGauntletScanBegin();
    if (G.scan == nullptr) {
        G.scanned = true;
        return;
    }
    if (!xgGauntletScanStep(G.scan, 64)) return;
    PcGauntletBoss* list = nullptr;
    int n = xgGauntletScanResult(G.scan, &list);
    G.discs = xgGauntletScanDiscs(G.scan);
    if (n > 0) G.bosses.assign(list, list + n);
    free(list);
    xgGauntletScanFree(G.scan);
    G.scan = nullptr;
    G.scanned = true;
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
    ImGui::SameLine();
    float avail = ImGui::GetContentRegionAvail().x;
    float w = skin::valueWidth();
    if (w > avail && avail > 0.0f) w = avail;
    if (w < avail) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + floorf(avail - w));
    int chosen = value;
    ImGui::PushID(id);
    if (skin::beginCombo("##list", tr(names[value]), 0, w)) {
        for (int i = 0; i < count; i++) {
            if (skin::comboItem(tr(names[i]), value == i)) chosen = i;
            if (value == i) ImGui::SetItemDefaultFocus();
        }
        skin::endCombo();
    }
    ImGui::PopID();
    if (chosen != value) sfx::play(sfx::CHOOSE);
    return chosen;
}

/* gauntlet.c's preset labels, for the catalogs */
const char* const presetLabels[PC_GAUNTLET_PRESETS] = {N_("Every boss, story order"), N_("Character bosses"),
                                                       N_("Gear bosses"), N_("Custom")};

const char* label(const char* gauntlet) {
    if (strcmp(gauntlet, "saved-story") == 0) return tr("Bosses through the save");
    int preset = pcGauntletPresetOf(gauntlet);
    return preset >= 0 ? tr(presetLabels[preset]) : gauntlet;
}

// The records of the cleared runs.
void records() {
    PcGauntletRecord list[6];
    int r = pcGauntletRecords(list, 6);
    if (r > 0) {
        ImGui::Dummy(ImVec2(0, 4 * skin::unit()));
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

void gauntletSection(void (*resumeGame)()) {
    textLines("Fight the bosses through the selected save's story progress using its party.");
    int current = 0, count = 0, retries = 0, skipped = 0, quitting = 0;
    int state = pcGauntletStatus(&current, &count, &retries, &skipped, &quitting);
    bool fromAnywhere = pcGauntletLaunchActive() != 0;
    if (fromAnywhere && state != PC_GAUNTLET_RUNNING) {
        hint("Starting or leaving the gauntlet...");
        if (state == PC_GAUNTLET_WAITING && ImGui::Button(tr("Leave gauntlet"))) {
            sfx::play(sfx::BACK);
            pcGauntletLeave();
            resumeGame();
        }
        return;
    }
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
        if (fromAnywhere && ImGui::Button(tr("Leave gauntlet"))) {
            sfx::play(sfx::BACK);
            pcGauntletLeave();
            resumeGame();
        }
        records();
        return;
    }
    if (xgGauntletScanBegin == nullptr) {
        hint("Not in this build (it reads the bosses with the extras).");
        return;
    }
    scanBosses();
    const uint8_t* save = gauntletSavePicker();
    int fights = save != nullptr && G.scanned && !G.bosses.empty()
                     ? pcGauntletSavedFights(G.bosses.data(), (int)G.bosses.size(), save) : 0;
    if (!G.scanned) {
        hint("Reading bosses from the discs...");
    } else if (G.bosses.empty()) {
        hint("No bosses found on the configured discs.");
    } else if (save != nullptr) {
        if (fights < 0) hint("The selected save has no usable party.");
        else if (fights == 0) hint("No bosses are available at this save's story progress.");
    }

    static const char* recoveryNames[PC_GAUNTLET_RECOVERIES] = {N_("Full"), N_("None"), N_("25%"), N_("50%"),
                                                               N_("Revive only")}; /* as gauntlet.c's */
    int recovery = pcGauntletRecoverySetting();
    int recoveryPicked = pick("Recovery", "##gauntlet_recovery", recoveryNames, PC_GAUNTLET_RECOVERIES, recovery);
    if (recoveryPicked != recovery) pcGauntletSetRecovery(recovery = recoveryPicked);
    const char* why = nullptr;
    bool available = pcGauntletLaunchAvailable(&why) != 0;
    ImGui::BeginDisabled(!available || fights <= 0);
    if (ImGui::Button(tr("Start"))) {
        bool started = pcGauntletLaunch(G.bosses.data(), (int)G.bosses.size(), save) > 0;
        sfx::play(started ? sfx::CHOOSE : sfx::BUZZ);
        if (started) {
            captureBattleMenuOnRender();
            resumeGame();
        }
    }
    ImGui::EndDisabled();
    if (why) hint("%s", tr(why));
    if (G.scanned && G.discs != 0 && G.discs != 3) {
        hint("Only disc %d is configured. The boss list uses that disc.", G.discs == 1 ? 1 : 2);
    }
    hint("When you leave the gauntlet, play resumes where you were.");
    hint("Between fights: Full restores HP, EP, Gear HP and fuel; None keeps them as the fight left them; "
         "25%% or 50%% of each maximum; Revive only brings a knocked-out member back at 10%% HP. Items used "
         "stay used for the run.");
    int last = pcGauntletLastResult();
    if (last >= 0) hint(last ? "The last run was cleared." : "The last run ended before its last fight.");
    records();
}

} // namespace menu
