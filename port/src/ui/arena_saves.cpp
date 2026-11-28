// The save browser of party vs party (arena_saves.h, xg-htn.35.143): the
// saves of this region on both memory cards (card/saves.h), a member picker
// for the one picked, and its party record packed and checked (card/party.h),
// one browser for each player. Each save is copied when the cards are read,
// so what is packed is what was listed, whatever the game writes later.

#include "arena_saves.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h" // NavActivateId: a choice made with the keyboard or a gamepad
#include "l10n.h"
#include "menu.h"
#include "options.h"
#include "sfx.h"
#include "skin.h"

extern "C" {
#include "card/card.h"
#include "card/saves.h"
}

extern "C" void pcArenaLog(const char* fmt, ...); // include/pc_arena.h: XG_ARENA_LOG=1

// The extras' story chapter names (none in a build without them).
extern "C" const char* xgStoryName(int progress) __attribute__((weak));

namespace menu {

namespace {

const char* const memberNames[CARD_SAVE_MEMBERS] = {
    N_("Fei"),  N_("Elly"),     N_("Citan"),   N_("Bart"),  N_("Billy"),          N_("Rico"),
    N_("Emeralda"), N_("Chu-Chu"), N_("Maria"), N_("Citan (sword)"), N_("Emeralda (adult)")};

// What a refused party shows (party.h XG_PARTY_*), with the member's name for %s.
const char* const reasons[XG_PARTY_REASONS] = {
    "",
    N_("This save is not complete any more: read the memory cards again."),
    N_("The disc's text could not be read, and the members' names come from it."),
    N_("%s has not joined in this save."),
    N_("This party could not be made from the save."),
    N_("This save is of the other region's game."),
    N_("Choose one to three different members."),
    N_("%s's record is not one the game makes (its command loadout)."),
    N_("%s's Gear number is outside the game's range."),
    N_("Billy's loaded rounds are outside the game's range."),
    N_("%s's HP is outside the game's range (at most its maximum, a maximum of 1 to 999)."),
    N_("%s's EP is outside the game's range (at most its maximum, a maximum of up to 99)."),
    N_("%s's level is outside the game's range (1 to 99)."),
    N_("%s's AP per turn is outside the game's range (1 to 7)."),
    N_("%s's name has characters the game's names cannot have."),
    N_("The save's items are outside the game's ranges (1 to 99 of each)."),
    N_("The save's ammunition is outside the game's ranges (1 to 99 of each)."),
    N_("The rounds left in the save's ammunition are outside the game's range (0 to 100)."),
    N_("%s's experience to the next level is outside the game's range (1 or more)."),
};
static_assert(sizeof reasons / sizeof reasons[0] == XG_PARTY_REASONS, "a text for each reason");

struct Save {
    CardGameSave info;
    std::vector<uint8_t> block; // the 8 KB block as it was read
};

struct Browser {
    std::vector<Save> saves;
    int lastFrame = -10;              // the panel's last frame: shown again later, the cards are read again
    int selected = -1;                // in saves
    int picked[3] = {255, 255, 255};  // character ids in order, 255 after the last
    bool dirty = true;                // the record is to pack again
    int reason = XG_PARTY_BAD_MEMBERS;
    int reasonMember = -1;
    XgPartyRecord record;
    bool haveCodes = false;
    uint8_t codes[XG_PARTY_CODE_TABLE];
};
Browser browsers[7];      // local players 1 and 2, online, gauntlet, two roster item lists, host items
Browser* B = &browsers[0]; // the one being drawn

int pickedCount() {
    int n = 0;
    while (n < 3 && B->picked[n] != 255) n++;
    return n;
}

// The save's own party, as far as its members have joined.
void pickSaveParty(const CardGameSave& s) {
    int n = 0;
    for (int i = 0; i < 3; i++) B->picked[i] = 255;
    for (int i = 0; i < 3; i++) {
        int id = s.party[i];
        bool again = false;
        for (int j = 0; j < n; j++) again |= B->picked[j] == id;
        if (id < CARD_SAVE_MEMBERS && (s.joined & 1 << id) && !again) B->picked[n++] = id;
    }
}

void readCards(bool partyRecord = true) {
    int card = -1, block = -1;
    std::vector<uint8_t> was;
    if (B->selected >= 0 && B->selected < (int)B->saves.size()) {
        card = B->saves[B->selected].info.card;
        block = B->saves[B->selected].info.block;
        was = B->saves[B->selected].block;
    }
    CardGameSave list[2 * CARD_BLOCKS];
    int n = cardGameSaves(list, 2 * CARD_BLOCKS, cardGameRegion());
    B->saves.clear();
    B->selected = -1;
    for (int i = 0; i < n; i++) {
        const uint8_t* b = cardGameSaveBlock(&list[i]);
        if (b == nullptr) continue;
        Save s;
        s.info = list[i];
        s.block.assign(b, b + CARD_BLOCK);
        if (s.info.card == card && s.info.block == block && s.block == was) B->selected = (int)B->saves.size();
        B->saves.push_back(std::move(s));
    }
    if (B->selected < 0) {
        for (int i = 0; i < 3; i++) B->picked[i] = 255; // that save is gone or changed
    }
    if (partyRecord && !B->haveCodes) B->haveCodes = xgPartyGameCodes(B->codes) != 0;
    B->dirty = true;
}

void pack() {
    B->dirty = false;
    B->reasonMember = -1;
    if (B->selected < 0 || B->selected >= (int)B->saves.size() || pickedCount() == 0) {
        B->reason = XG_PARTY_BAD_MEMBERS;
        return;
    }
    XgPartyWhy why;
    B->reason = xgPartyPack(&B->record, B->saves[B->selected].block.data(), B->picked, B->haveCodes ? B->codes : nullptr, &why);
    if (B->reason != XG_PARTY_OK) {
        B->reasonMember = why.member >= 0 && why.member < 3 ? B->picked[why.member] : -1;
        pcArenaLog("party vs party: the save's party cannot be used: %s", why.text);
    }
}

// "Into Solaris": the extras' chapter name, else the title's.
std::string placeOf(const CardGameSave& s) {
    const char* chapter = xgStoryName != nullptr ? xgStoryName(s.progress) : nullptr;
    return chapter != nullptr && chapter[0] != '\0' ? chapter : s.location;
}

std::string saveLabel(const CardGameSave& s, bool showTime = false) {
    char label[256];
    std::string place = placeOf(s);
    if (s.number > 0) {
        snprintf(label, sizeof label, tr("Slot %d, No.%02d: %s"), s.card + 1, s.number, place.c_str());
    } else {
        snprintf(label, sizeof label, tr("Slot %d: %s"), s.card + 1, place.c_str());
    }
    if (!showTime) return label;
    char time[32];
    unsigned minutes = s.playTime / 3600;
    snprintf(time, sizeof time, " (%u:%02u)", minutes / 60, minutes % 60);
    return std::string(label) + time;
}

void saveList(bool* chosenByNav) {
    std::string current = B->selected < 0 ? tr("Choose a save") : saveLabel(B->saves[B->selected].info, true);
    ImGui::BeginDisabled(B->saves.empty());
    if (skin::beginCombo("##arenasavelist", current.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < (int)B->saves.size(); i++) {
            const CardGameSave& s = B->saves[i].info;
            ImGui::PushID(i);
            if (skin::comboItem(saveLabel(s, true).c_str(), B->selected == i)) {
                if (B->selected != i) {
                    sfx::play(sfx::CHOOSE);
                    B->selected = i;
                    pickSaveParty(s);
                    B->dirty = true;
                }
                *chosenByNav = ImGui::GetCurrentContext()->NavActivateId != 0;
            }
            if (B->selected == i) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        skin::endCombo();
    }
    ImGui::EndDisabled();
}

void memberPicker(const CardGameSave& s, bool focus) {
    int count = pickedCount();
    for (int c = 0; c < CARD_SAVE_MEMBERS; c++) {
        bool joined = (s.joined & (1 << c)) != 0;
        int at = -1;
        for (int i = 0; i < 3; i++) {
            if (B->picked[i] == c) at = i;
        }
        bool on = at >= 0;
        const CardSaveMember& m = s.members[c];
        opt::row(memberNames[c]);
        ImGui::PushID(c);
        if (focus && joined) {
            ImGui::SetKeyboardFocusHere(); // the list's choice moves the cursor on to its members
            focus = false;
        }
        ImGui::BeginDisabled(!joined || (!on && count >= 3));
        if (skin::checkbox("##member", &on)) {
            sfx::play(sfx::CHOOSE);
            if (on) {
                for (int i = 0; i < 3; i++) {
                    if (B->picked[i] == 255) {
                        B->picked[i] = c;
                        break;
                    }
                }
            } else {
                for (int i = at; i < 2; i++) B->picked[i] = B->picked[i + 1]; // the others move up
                B->picked[2] = 255;
            }
            B->dirty = true;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        if (joined) hint("%s: level %d, HP %d/%d", tr(memberNames[c]), m.level, m.hp, m.maxHp);
        else hint("%s has not joined in this save.", tr(memberNames[c]));
    }
    count = pickedCount();
    if (count == 0) {
        hint("Choose one to three of its members; the first leads.");
        return;
    }
    std::string order;
    for (int i = 0; i < count; i++) {
        if (i > 0) order += " / ";
        order += tr(memberNames[B->picked[i]]);
    }
    hint("In this order: %s.", order.c_str());
}

} // namespace

static const uint8_t* saveBlockPicker(int browser) {
    B = &browsers[browser];
    int frame = ImGui::GetFrameCount();
    if (frame != B->lastFrame + 1) readCards(false);
    B->lastFrame = frame;
    std::string current = B->selected < 0 ? tr("Choose a save") : saveLabel(B->saves[B->selected].info, true);
    ImGui::BeginDisabled(B->saves.empty());
    if (skin::beginCombo("##arena_save", current.c_str(), 0, ImGui::GetContentRegionAvail().x)) {
        for (int i = 0; i < (int)B->saves.size(); i++) {
            ImGui::PushID(i);
            bool selected = B->selected == i;
            if (skin::comboItem(saveLabel(B->saves[i].info, true).c_str(), selected)) {
                B->selected = i;
                sfx::play(sfx::CHOOSE);
            }
            if (selected || (B->selected < 0 && i == 0)) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        skin::endCombo();
    }
    ImGui::EndDisabled();
    if (B->saves.empty()) hint("No saves of this game on the memory cards (F1 > Saves > Memory cards).");
    return B->selected < 0 ? nullptr : B->saves[B->selected].block.data();
}

const uint8_t* gauntletSavePicker() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Memory card save"));
    ImGui::SameLine();
    return saveBlockPicker(3);
}

const uint8_t* arenaItemSavePicker(int side) {
    opt::row(N_("Memory card save"));
    return saveBlockPicker(4 + side);
}

const uint8_t* arenaHostItemSavePicker(bool show) {
    constexpr int browser = 6;
    if (show) {
        opt::row(N_("Memory card save"));
        const uint8_t* selected = saveBlockPicker(browser);
        opt::plain();
        if (selected != nullptr && ImGui::Button(tr("Clear items"))) {
            browsers[browser].selected = -1;
            return nullptr;
        }
        return selected;
    }
    const Browser& b = browsers[browser];
    return b.selected < 0 ? nullptr : b.saves[b.selected].block.data();
}

void arenaPartyReason(int reason, int characterId, char* out, size_t size) {
    const char* who = characterId >= 0 && characterId < CARD_SAVE_MEMBERS ? tr(memberNames[characterId]) : tr("A member");
    if (reason <= 0 || reason >= XG_PARTY_REASONS) {
        snprintf(out, size, tr("That party could not be used (reason %d)."), reason);
        return;
    }
    snprintf(out, size, tr(reasons[reason]), who);
}

bool arenaSavesPanel(int player, XgPartyRecord* out) {
    B = &browsers[player == 2 ? 1 : player == 0 ? 2 : 0];
    int frame = ImGui::GetFrameCount();
    if (frame > B->lastFrame + 1) readCards(); // shown again: the cards may have changed meanwhile
    B->lastFrame = frame;
    opt::plain();
    ImGui::PushID("arenasaves");
    ImGui::PushID(player);
    opt::row(player == 2 ? N_("Player 2's save") : player == 0 ? N_("Memory card save") : N_("Player 1's save"));
    bool chosenByNav = false;
    saveList(&chosenByNav);
    if (B->saves.empty()) {
        hint("No saves of this game on the memory cards (F1 > Saves > Memory cards).");
    }
    bool ok = false;
    if (B->selected < 0 || B->selected >= (int)B->saves.size()) {
        if (!B->saves.empty()) {
            hint("Choose a save: the members come as it has them (levels, equipment, abilities), with its items.");
        }
    } else {
        const CardGameSave& s = B->saves[B->selected].info;
        std::string place = placeOf(s);
        unsigned seconds = s.playTime / 60;
        hint("Story %d: %s. Disc %d, %u:%02u:%02u played.", s.progress, place.c_str(), s.disc, seconds / 3600,
             seconds / 60 % 60, seconds % 60);
        memberPicker(s, chosenByNav);
        if (B->dirty) pack();
        ok = B->reason == XG_PARTY_OK;
        if (!ok && pickedCount() > 0) {
            opt::plain();
            char line[512];
            const char* who = B->reasonMember >= 0 && B->reasonMember < CARD_SAVE_MEMBERS ? tr(memberNames[B->reasonMember])
                                                                                       : "";
            snprintf(line, sizeof line, tr(reasons[B->reason]), who);
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textWarning);
            ImGui::TextWrapped("%s", line);
            ImGui::PopStyleColor();
            hint("Edited saves are accepted: only values outside the game's own ranges are refused.");
        }
        if (ok) *out = B->record;
    }
    opt::plain();
    markBlock();
    if (ImGui::Button(tr("Read the memory cards again"))) {
        sfx::play(sfx::CHOOSE);
        readCards();
    }
    hint("Refresh the save list from both memory cards.");
    ImGui::PopID();
    ImGui::PopID();
    return ok;
}

} // namespace menu
