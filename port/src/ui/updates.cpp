// The update check's screens (port/src/update/update_check.h, xg-4oq.14.3),
// in a build with the updater (XG_UPDATER) only:
//   - the question before the game (ui.h uiAskUpdates; menu.cpp runs it as a
//     screen of its own): once a day, or never; the cancel button or Esc
//     leaves it for the next start;
//   - System's Updates group (sections.cpp): a newer version found (its
//     release page, skip it), the setting, the last check and Check now.

#ifdef XG_UPDATER

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "imgui.h"
#include "l10n.h"
#include "menu.h"
#include "options.h"
#include "sfx.h"
#include "skin.h"

extern "C" {
#include "system/release.h"
#include "system/settings.h"
#include "update/update_check.h"
#include "update/update_install.h"
}

namespace menu {

namespace {

// A wrapped line in a colour (the menu's warning gold, its dimmed grey).
void colouredText(const ImVec4& colour, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// The next button on this line if it fits, else on the next (as sections.cpp's).
void sameLineFor(const char* label) {
    float w = ImGui::CalcTextSize(tr(label), NULL, true).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
}

// The setting's three states as the game's menus show choices (sections.cpp's
// choice): on one line, the chosen one marked; a drop-down list where the line
// is too narrow.
int settingChoice(int value) {
    static const char* const names[] = {N_("Ask at the next start"), N_("Once a day"), N_("Never")};
    float width = 2 * 14 * scale();
    for (const char* n : names) width += ImGui::CalcTextSize(tr(n)).x;
    ImGui::PushID("update_check");
    if (width <= ImGui::GetContentRegionAvail().x) {
        for (int i = 0; i < 3; i++) {
            if (i > 0) ImGui::SameLine(0, 14 * scale());
            ImGui::AlignTextToFramePadding();
            if (skin::selectable(tr(names[i]), value == i, 0, ImGui::CalcTextSize(tr(names[i])))) value = i;
        }
    } else {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##list", tr(names[value]))) {
            skin::frameWindow();
            for (int i = 0; i < 3; i++) {
                if (ImGui::Selectable(tr(names[i]), value == i)) value = i;
                if (value == i) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::PopID();
    return value;
}

// A time as the player's clock shows it ("YYYY-MM-DD HH:MM").
void localTime(long long t, char* out, size_t size) {
    time_t when = (time_t)t;
    struct tm parts;
#ifdef _WIN32
    bool ok = localtime_s(&parts, &when) == 0;
#else
    bool ok = localtime_r(&when, &parts) != NULL;
#endif
    if (!ok || strftime(out, size, "%Y-%m-%d %H:%M", &parts) == 0) snprintf(out, size, "-");
}

bool questionOpen;

// The one-click install (update/update_install.h): whether this copy can, looked at once (it writes a
// probe file into the game's folder), and the install's own state as the menu shows it.
struct {
    bool looked, possible, leaving;
    char why[256];
    UpdateInstallPlace place;
} inst;

// A popup in the game's message box (menu.h nextModal), as states.cpp's.
bool beginModal(const char* name) {
    nextModal();
    bool open = ImGui::BeginPopupModal(name, NULL,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoTitleBar);
    modalStyleEnd();
    if (open) skin::frameWindow(true);
    return open;
}

// Under the new version: Install and restart, its question, its progress, why it stopped.
void installRows(const UpdateStatus* s) {
    if (!inst.looked && !opt::indexing()) {
        inst.looked = true;
        inst.possible = updateInstallPlace(&inst.place, inst.why, sizeof inst.why) != 0;
    }
    UpdateInstallStatus now = updateInstallStatus();
    if (now.stage >= INSTALL_CHECKING && now.stage <= INSTALL_UNPACKING) {
        if (now.stage == INSTALL_DOWNLOADING && now.total > 0) {
            char over[64];
            snprintf(over, sizeof over, tr("%.1f of %.1f MB"), now.done / 1048576.0, now.total / 1048576.0);
            ImGui::ProgressBar((float)now.done / (float)now.total, ImVec2(-1, 0), over);
        } else {
            textLines(now.stage == INSTALL_CHECKING    ? "Looking at the release..."
                      : now.stage == INSTALL_VERIFYING ? "Checking the download..."
                      : now.stage == INSTALL_UNPACKING ? "Unpacking..."
                                                       : "Downloading...");
        }
        if (ImGui::Button(tr("Cancel the install"))) updateInstallCancel();
        return;
    }
    if (now.stage == INSTALL_READY) {
        textLines("Starting the new version...");
        if (!inst.leaving && !opt::indexing()) {
            inst.leaving = true;
            closeMenu(); // the game's next vblank puts it in place and starts it (update_install.c)
        }
        return;
    }
    if (now.stage == INSTALL_FAILED) {
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textWarning);
        ImGui::TextWrapped(tr("The install stopped: %s."), now.why); // the reason in English, as the log has it
        ImGui::PopStyleColor();
    }
    if (!inst.possible) {
        if (inst.looked) {
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textDimmed);
            ImGui::TextWrapped(tr("Installing it from here is not possible: %s."), inst.why);
            ImGui::PopStyleColor();
        }
        return;
    }
    if (ImGui::Button(tr("Install and restart")) && !opt::indexing()) ImGui::OpenPopup("##install-question");
    if (beginModal("##install-question")) {
        bool setup = inst.place.kind == RELEASE_SETUP;
        char text[600];
        snprintf(text, sizeof text,
                 tr(setup ? "Install Xenogears PC port %s now? The game downloads its setup and checks it, then "
                            "closes; Windows asks for permission, and the setup installs it and starts the game "
                            "again. Progress since your last save is lost; the settings, memory cards and save "
                            "states are kept."
                          : "Install Xenogears PC port %s now? The game downloads it, checks it, puts it in place of "
                            "this copy's files and starts again. Progress since your last save is lost; the "
                            "settings, memory cards and save states are kept."),
                 s->available);
        textLines(text);
        ImGui::Spacing();
        if (ImGui::Button(tr("Install and restart##go"))) {
            UpdateInstallPlace again; // looked at again: the folder, the fetcher
            char why[256];
            if (updateInstallPlace(&again, why, sizeof why)) {
                updateInstallStart(&again);
                fprintf(stderr, "update: install of %s started\n", s->available);
            } else {
                inst.possible = false;
                snprintf(inst.why, sizeof inst.why, "%s", why);
            }
            ImGui::CloseCurrentPopup();
        }
        sameLineFor("Cancel##install");
        if (ImGui::Button(tr("Cancel##install")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace

// ---- the question (ui.h uiAskUpdates) -----------------------------------------------------------

void updateQuestionOpened() {
    questionOpen = false;
}

// A window of its own placed as the game's message box (menu.h nextModal), not
// a popup: F1 may close the screen in any frame, and a popup left open would
// stay on ImGui's stack into the next opening of the menu.
bool drawUpdateQuestion() {
    if (!questionOpen) {
        ImGui::SetNextWindowFocus(); // the cursor on its first button
        questionOpen = true;
    }
    bool done = false;
    nextModal();
    ImGui::Begin("##updates-question", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    modalStyleEnd();
    skin::frameWindow(true);
    textLines("Look for a new version of the port once a day?");
    ImGui::Spacing();
    textLines("The game would ask GitHub for the latest release's small signed file; nothing about you is sent. A "
              "new version shows in a corner note and in F1, System, Updates, where this can be changed.");
    ImGui::Spacing();
    if (ImGui::Button(tr("Once a day"))) {
        updateSetSetting(UPDATE_ON);
        fprintf(stderr, "update: the player chose once a day\n");
        done = true;
    }
    sameLineFor("Never");
    if (ImGui::Button(tr("Never"))) {
        updateSetSetting(UPDATE_OFF);
        fprintf(stderr, "update: the player chose never\n");
        done = true;
    }
    if (!done && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))) {
        fprintf(stderr, "update: the question is left for the next start\n");
        sfx::play(sfx::BACK);
        done = true;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textDimmed);
    textLines("Cancel: ask again at the next start.");
    ImGui::PopStyleColor();
    ImGui::End();
    return done;
}

// ---- System > Updates (sections.cpp) ------------------------------------------------------------

void updatesRows() {
    opt::group(N_("Updates"));
    const UpdateStatus* s = updateStatus();
    const char* blocked = updateBlocked();
    const char* fetcher = nullptr;
    bool canFetch = updatePlatformFetch(&fetcher) != nullptr;
    UpdateSetting setting = updateSetting();

    // A newer version: first, where it is seen.
    if (s->available[0] != '\0' && opt::row("New version", "update_skip")) {
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textGood);
        ImGui::TextWrapped(tr("Xenogears PC port %s is out (this is %s)."), s->available,
                           releaseVersion()[0] ? releaseVersion() : tr("a development build"));
        ImGui::PopStyleColor();
        if (ImGui::Button(tr("Open the release page"))) {
            if (!SDL_OpenURL(s->availablePage)) {
                fprintf(stderr, "update: cannot open %s: %s\n", s->availablePage, SDL_GetError());
            }
        }
        sameLineFor("Skip this version");
        if (ImGui::Button(tr("Skip this version"))) updateSkip(s->available);
        if (releaseUpdatesItself(releaseKind()) && releaseKind() != RELEASE_ANDROID) {
            installRows(s);
            hint("Install and restart downloads it, checks it against the signed manifest, puts it in place of this "
                 "copy's files and starts the game again; the settings and saves are kept. Skipped, it is not shown "
                 "again; a later version is.");
        } else if (releaseUpdatesItself(releaseKind()) || releaseKind() == RELEASE_DEV) {
            hint("Its release page has the downloads. Skipped, it is not shown again; a later version is.");
        } else {
            hint("Your package manager installs it: its release page has the new package. Skipped, it is not "
                 "shown again; a later version is.");
        }
    }

    if (opt::row("Check for updates", "update_check")) {
        int chosen = settingChoice((int)setting);
        if (chosen != (int)setting) {
            updateSetSetting((UpdateSetting)chosen);
            setting = (UpdateSetting)chosen;
            if (setting == UPDATE_ON) updateStart(0); // due at once when never checked (or a day ago)
        }
        hint("Once a day at most, the game asks GitHub for the latest release's update file and its signature "
             "(two small files); nothing about you is sent. A new version shows here and in a corner note. Ask: "
             "the question before the game, at the next start.");
        if (blocked != nullptr) {
            colouredText(skin::textWarning, tr(releaseKind() == RELEASE_DEV
                                                   ? "This is a development build: it never checks."
                                                   : "Not in this run (headless, or an online session)."));
        } else if (!canFetch) {
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textWarning);
            ImGui::TextWrapped(tr("No check is possible: %s."), fetcher);
            ImGui::PopStyleColor();
        }
        overrideNote("update_check");
    }

    if (opt::row("Last check")) {
        char when[32];
        localTime(s->checkedAt, when, sizeof when);
        if (s->running) {
            textLines("Checking...");
        } else if (s->outcome == UPDATE_UNCHECKED) {
            if (s->checkedAt > 0) ImGui::TextWrapped(tr("Last checked %s."), when);
            else textLines("Not checked yet.");
        } else {
            switch (s->outcome) {
            case UPDATE_NEWER: ImGui::TextWrapped(tr("%s: version %s found."), when, s->version); break;
            case UPDATE_LATEST: ImGui::TextWrapped(tr("%s: this is the latest release."), when); break;
            case UPDATE_SKIPPED: ImGui::TextWrapped(tr("%s: version %s, skipped."), when, s->version); break;
            case UPDATE_NOT_OFFERED: ImGui::TextWrapped(tr("%s: no update is offered yet."), when); break;
            case UPDATE_NO_MANIFEST: ImGui::TextWrapped(tr("%s: the latest release has no update file."), when); break;
            case UPDATE_UNTRUSTED:
                ImGui::TextWrapped(tr("%s: the update file is not signed by the release key: nothing is offered."), when);
                break;
            case UPDATE_BAD_MANIFEST:
                ImGui::TextWrapped(tr("%s: the update file is of a kind this build cannot read."), when);
                break;
            case UPDATE_NO_FETCHER: ImGui::TextWrapped(tr("%s: no check is possible on this system."), when); break;
            default: ImGui::TextWrapped(tr("%s: the check failed."), when); break;
            }
            if (s->outcome != UPDATE_NEWER && s->outcome != UPDATE_LATEST && s->outcome != UPDATE_SKIPPED) {
                colouredText(skin::textDimmed, s->why); // the system's words, as the log has them
            }
        }
        ImGui::BeginDisabled(setting != UPDATE_ON || blocked != nullptr || !canFetch || s->running);
        if (ImGui::Button(tr("Check now"))) updateStart(1);
        ImGui::EndDisabled();
        hint("Check now looks at once, whenever the last check was. Only while checks are on.");
    }
}

} // namespace menu

#endif // XG_UPDATER
