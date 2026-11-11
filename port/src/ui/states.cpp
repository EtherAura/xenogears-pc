// F1 > Save states (port/src/system/savestate.h, rewind.h; xg-htn.34.10):
// the ten slots with their pictures, saving, loading, deleting, the undo of
// the last load, and rewind's settings: the Saves section's Save states group
// and its Advanced group (options.h rows).

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "imgui.h"
#include "imgui_internal.h" // RegisterUserTexture: the slots' pictures
#include "l10n.h"
#include "menu.h"
#include "options.h"
#include "sfx.h"
#include "skin.h"

extern "C" {
#include "system/rewind.h"
#include "system/savestate.h"
}

namespace menu {

namespace {

const int SLOT_ROWS = STATE_SLOTS + 1; // the slots, then "before the last load" (index 0 in savestate.h)

struct Slot {
    XgStateInfo info{};
    bool known = false;
    ImTextureData* tex = nullptr;
};

struct {
    Slot slot[SLOT_ROWS]; // [0] undo, [1..10] the slots
    int serial = -1;      // stateSerial when read
    int lastFrame = -10;  // ImGui's frame count when last drawn (a new opening reads the slots again)
    int ask = 0;          // 1 overwrite, 2 delete: the modal's question for the selected slot
    char message[200] = "";
} S;

// The slot's picture as a texture of the menu's (ImGui's own Vulkan backend
// uploads it), made once and updated in place.
void setPicture(Slot& s) {
    const XgStateInfo& in = s.info;
    if (in.thumb == nullptr || in.thumbW <= 0 || in.thumbH <= 0) {
        return;
    }
    if (s.tex != nullptr && (s.tex->Width != in.thumbW || s.tex->Height != in.thumbH)) {
        return; // the thumbnail size is fixed (statefile.h); never resized
    }
    bool created = s.tex == nullptr;
    if (created) {
        s.tex = IM_NEW(ImTextureData)();
        s.tex->Create(ImTextureFormat_RGBA32, in.thumbW, in.thumbH);
    }
    unsigned char* px = (unsigned char*)s.tex->GetPixels();
    for (int i = 0; i < in.thumbW * in.thumbH; i++) {
        px[i * 4 + 0] = in.thumb[i * 3 + 0];
        px[i * 4 + 1] = in.thumb[i * 3 + 1];
        px[i * 4 + 2] = in.thumb[i * 3 + 2];
        px[i * 4 + 3] = 255;
    }
    if (created) {
        s.tex->SetStatus(ImTextureStatus_WantCreate);
        ImGui::RegisterUserTexture(s.tex);
    } else if (s.tex->Status == ImTextureStatus_OK) {
        ImTextureRect all = {0, 0, (unsigned short)in.thumbW, (unsigned short)in.thumbH};
        s.tex->Updates.push_back(all);
        s.tex->UpdateRect = all;
        s.tex->SetStatus(ImTextureStatus_WantUpdates);
    }
}

void readSlots() {
    for (int i = 0; i < SLOT_ROWS; i++) {
        Slot& s = S.slot[i];
        stateInfoFree(&s.info);
        s.known = stateSlotInfo(i, &s.info) != 0;
        if (s.known) {
            setPicture(s);
        }
        stateInfoFree(&s.info); // the picture is in the texture now
    }
    S.serial = stateSerial();
}

void picture(const Slot& s, float w, float h) {
    ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (s.known && s.tex != nullptr && s.info.exists) {
        ImTextureRef ref;
        ref._TexData = s.tex;
        ImU32 tint = s.info.usable ? IM_COL32_WHITE : IM_COL32(110, 110, 110, 255);
        dl->AddImage(ref, at, ImVec2(at.x + w, at.y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
    } else {
        dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), IM_COL32(0, 0, 0, 96));
    }
    ImGui::Dummy(ImVec2(w, h));
}

void when(int64_t savedAt, char* out, size_t size) {
    time_t t = (time_t)savedAt;
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    strftime(out, size, "%Y-%m-%d %H:%M", &tmv);
}

// One slot's line: its name, the picture, when, where.
bool slotRow(int i, bool selected) {
    const Slot& s = S.slot[i];
    float u = scale();
    ImGui::TableNextRow(0, 50 * u);
    ImGui::TableSetColumnIndex(0);
    ImGui::PushID(i);
    char name[64];
    snprintf(name, sizeof name, tr("Slot %d"), i);
    bool pressed = skin::selectable(name, selected, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 48 * u));
    ImGui::PopID();
    ImGui::TableSetColumnIndex(1);
    picture(s, 64 * u, 48 * u);
    ImGui::TableSetColumnIndex(2);
    if (!s.known || !s.info.exists) {
        ImGui::TextDisabled("%s", tr("Empty"));
        return pressed;
    }
    char date[32];
    when(s.info.savedAt, date, sizeof date);
    uint32_t t = s.info.playTime;
    if (!s.info.usable) {
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textDimmed);
    }
    ImGui::TextUnformatted(date);
    ImGui::Text(tr("Disc %d  %s"), s.info.disc, s.info.label);
    if (s.info.usable) {
        ImGui::Text(tr("Play time %u:%02u:%02u"), t / 3600, t / 60 % 60, t % 60);
    } else {
        if (s.info.otherBuild && s.info.appVersion[0] != '\0') {
            ImGui::Text(tr("Version %s's"), s.info.appVersion);
        } else {
            ImGui::TextUnformatted(tr("Another build's"));
        }
        ImGui::PopStyleColor();
    }
    return pressed;
}

// One setting's choices in a drop-down list (the menu's style for a few
// named values); returns the chosen value.
int listRow(const char* id, const int* values, const char* const* names, int count, int value) {
    int now = -1;
    for (int i = 0; i < count; i++) {
        if (values[i] == value) now = i;
    }
    int out = value;
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * scale(), 6 * scale()));
    bool open = ImGui::BeginCombo(id, now >= 0 ? tr(names[now]) : tr("Custom"));
    ImGui::PopStyleVar();
    if (open) {
        skin::frameWindow();
        for (int i = 0; i < count; i++) {
            if (ImGui::Selectable(tr(names[i]), now == i)) out = values[i];
            if (now == i) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return out;
}

// Rewind (port/src/system/rewind.h): on or off (the Save states group), how
// far back, how often, how much memory (its Advanced group).
void rewindRow(const RewindStats& st) {
    if (!opt::row("Rewind", "rewind")) return;
    static const int offOnValues[] = {0, 1};
    static const char* const offOn[] = {"Off", "On"};
    int on = listRow("##rewind", offOnValues, offOn, 2, st.on != 0);
    if (on != (st.on != 0)) {
        sfx::play(sfx::CHOOSE);
        rewindSetOn(on);
    }
    hint("Hold R (F10 when R plays a button) or Select+L2 on a gamepad to go back; let go to play on from there.");
    if (st.on) {
        hint("Kept now: %.1f s in %.0f MB; a capture takes %.1f ms of the game's time (at worst %.1f).", st.kept,
             (double)st.bytes / 1048576.0, st.captureMs, st.worstMs);
    } else {
        hint("Off: nothing is kept and the game runs as without it. On, each capture costs a few milliseconds.");
    }
    overrideNote("rewind");
}

void rewindSettings(const RewindStats& st) {
    opt::group("Advanced", opt::COLLAPSED);
    static const int seconds[] = {10, 30, 60, 120};
    static const char* const secondNames[] = {"10 s", "30 s", "60 s", "120 s"};
    if (opt::row("How far back", "rewind_seconds")) {
        ImGui::BeginDisabled(!st.on);
        int v = listRow("##seconds", seconds, secondNames, 4, st.seconds);
        if (v != st.seconds) rewindSetSeconds(v);
        ImGui::EndDisabled();
        hint("The seconds of play rewind keeps (with Rewind on). 30 s by default.");
        overrideNote("rewind_seconds");
    }
    static const int intervals[] = {1, 2, 4};
    static const char* const intervalNames[] = {"1", "2", "4"};
    if (opt::row("Capture every N frames", "rewind_interval")) {
        ImGui::BeginDisabled(!st.on);
        int v = listRow("##interval", intervals, intervalNames, 3, st.interval);
        if (v != st.interval) rewindSetInterval(v);
        ImGui::EndDisabled();
        hint("A capture every N frames: 1 goes back most smoothly, 4 costs least. 2 by default.");
        overrideNote("rewind_interval");
    }
    static const int budgets[] = {128, 256, 512, 1024};
    static const char* const budgetNames[] = {"128 MB", "256 MB", "512 MB", "1 GB"};
    if (opt::row("Rewind memory", "rewind_budget")) {
        ImGui::BeginDisabled(!st.on);
        int v = listRow("##budget", budgets, budgetNames, 4, st.budget);
        if (v != st.budget) rewindSetBudget(v);
        ImGui::EndDisabled();
        hint("The memory rewind takes at most: when it is full, the oldest captures go first. 256 MB by default.");
        overrideNote("rewind_budget");
    }
    static const int laneCounts[] = {1, 2, 4};
    static const char* const laneNames[] = {"1", "2", "4"};
    if (opt::row("Compare threads", "rewind_lanes")) {
        ImGui::BeginDisabled(!st.on);
        int v = listRow("##lanes", laneCounts, laneNames, 3, st.lanes);
        if (v != st.lanes) rewindSetLanes(v);
        ImGui::EndDisabled();
        hint("Threads comparing the game's memory at a capture (the game's own thread one of them): more make a "
             "capture shorter on a CPU with cores to spare. 4 by default.");
        overrideNote("rewind_lanes");
    }
}

bool beginModal(const char* name) {
    nextModal();
    bool open = ImGui::BeginPopupModal(name, NULL,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoTitleBar);
    modalStyleEnd();
    if (open) {
        skin::frameWindow(true);
    }
    return open;
}

} // namespace

void statesSection() {
    opt::group("Save states");
    const char* why = nullptr;
    if (!stateAvailable(&why)) {
        if (opt::row("Save states")) {
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textWarning);
            ImGui::TextWrapped("%s", tr(why));
            ImGui::PopStyleColor();
        }
        return;
    }
    int frame = ImGui::GetFrameCount();
    if (frame != S.lastFrame + 1 || S.serial != stateSerial()) {
        readSlots(); // opened again, or a state was written meanwhile
    }
    S.lastFrame = frame;
    int selected = stateSelectedSlot();
    const Slot& cur = S.slot[selected];
    bool exists = cur.known && cur.info.exists;

    if (opt::row("Selected slot")) {
        char label[64];
        snprintf(label, sizeof label, tr("Save to slot %d"), selected);
        if (ImGui::Button(label)) {
            sfx::play(sfx::CHOOSE);
            if (exists) {
                S.ask = 1;
            } else {
                stateRequestSave(selected);
                closeMenu(); // taken as the game goes on: the picture under the menu
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!exists || !cur.info.usable);
        snprintf(label, sizeof label, tr("Load slot %d"), selected);
        if (ImGui::Button(label)) {
            sfx::play(sfx::CHOOSE);
            stateRequestLoad(selected);
            closeMenu();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!exists);
        if (ImGui::Button(tr("Delete"))) {
            sfx::play(sfx::CHOOSE);
            S.ask = 2;
        }
        ImGui::EndDisabled();
        const Slot& undo = S.slot[STATE_UNDO];
        bool canUndo = undo.known && undo.info.exists && undo.info.usable;
        ImGui::BeginDisabled(!canUndo);
        if (ImGui::Button(tr("Undo the last load"))) {
            sfx::play(sfx::CHOOSE);
            stateRequestLoad(STATE_UNDO);
            closeMenu();
        }
        ImGui::EndDisabled();
        hint("F7 saves to the selected slot, F8 loads it, F9 and Shift+F9 choose the slot, Shift+F8 undoes the last "
             "load.");
        if (canUndo) {
            char date[32];
            when(undo.info.savedAt, date, sizeof date);
            hint("Back to the game as it was when a state was last loaded (%s, %s).", date, undo.info.label);
        }
        if (exists && !cur.info.usable) {
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textWarning);
            ImGui::TextWrapped(tr("Slot %d cannot be loaded: %s."), selected, cur.info.why);
            ImGui::PopStyleColor();
        }
#ifdef __ANDROID__
        // An update leaves the earlier version's states: kept (that version loads them), marked,
        // and deleted here on request (system/android.c says so once after the update).
        int others = 0;
        for (int i = 0; i <= STATE_SLOTS; i++) others += S.slot[i].known && S.slot[i].info.exists && S.slot[i].info.otherBuild;
        if (others > 0) {
            char label[96];
            if (others == 1) snprintf(label, sizeof label, "%s", tr("Delete the state another version made"));
            else snprintf(label, sizeof label, tr("Delete the %d states other versions made"), others);
            if (ImGui::Button(label)) {
                sfx::play(sfx::CHOOSE);
                S.ask = 3;
            }
            hint("States made before an update of the app cannot be loaded by this version: a state holds the "
                 "program's own memory, which an update moves. Memory card saves carry over.");
        }
#endif
        if (S.message[0] != '\0') {
            textLines(S.message);
        }
    }

    RewindStats rs;
    rewindStats(&rs);
    rewindRow(rs); // above the slots, so it shows without scrolling past them

    if (opt::plain()) {
        markBlock();
        float u = scale();
        if (ImGui::BeginTable("slots", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("slot", ImGuiTableColumnFlags_WidthFixed, 44 * u);
            ImGui::TableSetupColumn("picture", ImGuiTableColumnFlags_WidthFixed, 68 * u);
            ImGui::TableSetupColumn("about");
            for (int i = 1; i <= STATE_SLOTS; i++) {
                if (slotRow(i, i == selected)) {
                    sfx::play(sfx::MOVE);
                    stateSelectSlot(i);
                    selected = i;
                    S.message[0] = '\0';
                }
            }
            ImGui::EndTable();
        }
        hint("A save state is the whole game at a moment: loading it goes back there exactly. Loading never changes "
             "the memory cards: saves made since stay on them, and the game reads the cards again. States load only "
             "in the build of the port that saved them: after the port is updated or rebuilt, older states are "
             "refused. Keep long-term progress on memory cards.");
    }

    rewindSettings(rs);

    if (opt::indexing()) return;
    opt::plain(); // (the table closed: the popup outside it)
    if (S.ask != 0) {
        ImGui::OpenPopup("##stateask");
    }
    if (beginModal("##stateask")) {
        char q[160];
        if (S.ask == 1) {
            snprintf(q, sizeof q, tr("Save over slot %d?"), selected);
        } else if (S.ask == 3) {
            snprintf(q, sizeof q, "%s", tr("Delete every state other versions of the app made? This version cannot "
                                           "load them."));
        } else {
            snprintf(q, sizeof q, tr("Delete the state in slot %d?"), selected);
        }
        textLines(q);
        if (ImGui::Button(tr("Yes"))) {
            sfx::play(sfx::CHOOSE);
            if (S.ask == 1) {
                stateRequestSave(selected);
                closeMenu();
            } else if (S.ask == 3) {
                int gone = 0;
                for (int i = 0; i <= STATE_SLOTS; i++) {
                    if (S.slot[i].known && S.slot[i].info.exists && S.slot[i].info.otherBuild) gone += stateDeleteSlot(i);
                }
                snprintf(S.message, sizeof S.message, tr("%d states deleted."), gone);
                readSlots();
            } else {
                stateDeleteSlot(selected);
                snprintf(S.message, sizeof S.message, tr("Slot %d deleted."), selected);
                readSlots();
            }
            S.ask = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("No")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            sfx::play(sfx::BACK);
            S.ask = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace menu
