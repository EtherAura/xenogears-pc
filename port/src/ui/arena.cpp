// PvP pages in F1 > Extras (port/src/system/arena.h; xg-htn.35.16): Battling's own
// two-player bouts from wherever the game is, who plays on what, and where
// the match is; the battle music; versus battles (a formation, or a mirror
// match: a copy of the party, or party vs party: each player's party from a
// save, arena_saves.cpp) and their party; and
// Online: a session with a player elsewhere (port/src/system/netplay_launch.c),
// in a window of its own. A match's end (Rematch or Leave) is the game's own,
// in its own windows (port/src/game/main/arena.c, versus.c, battling/arena.c).

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "arena_saves.h"
#include "imgui.h"
#include "imgui_internal.h" // NavActivateId: a choice made with the keyboard or a gamepad
#include "l10n.h"
#include "menu.h"
#include "sfx.h"
#include "skin.h"

extern "C" {
#include "cd/cd.h"
#include "extras/versus_list.h"
#include "system/arena.h"
#include "system/netplay.h"
#include "system/pad.h"
#include "system/settings.h"
}

extern "C" void pcArenaLog(const char* fmt, ...); // include/pc_arena.h: XG_ARENA_LOG=1

// The extras' formation list (none in a build without them).
extern "C" XgVersusScan* xgVersusScanBegin(void) __attribute__((weak));
extern "C" int xgVersusScanStep(XgVersusScan* s, int budget) __attribute__((weak));
extern "C" double xgVersusScanProgress(const XgVersusScan* s) __attribute__((weak));
extern "C" int xgVersusScanResult(XgVersusScan* s, XgVersusFormation** out) __attribute__((weak));
extern "C" void xgVersusScanFree(XgVersusScan* s) __attribute__((weak));
// ... and the jukebox's scores (extras/extras.h).
extern "C" int extrasJukeboxSongCount(void) __attribute__((weak));
extern "C" const char* extrasJukeboxSongTitle(int i, int* musicId) __attribute__((weak));
extern "C" void extrasJukeboxPreview(int musicId) __attribute__((weak));

namespace menu {

namespace {

const char* gamepadName(int port) {
    return padProfileDevice(port) != 0 ? padProfileName(port) : nullptr;
}

void players() {
    bool keyboard2 = pcArenaKeyboardOffered() && pcArenaPlayer2Keyboard();
    const char* pad1 = gamepadName(0);
    const char* pad2 = gamepadName(1);
    char line[320];
    if (keyboard2) {
        snprintf(line, sizeof line, tr("Player 1, the left Gear: %s. Player 2, the right Gear: the keyboard%s%s."),
                 pad1 != nullptr ? pad1 : tr("no gamepad (connect one)"), pad2 != nullptr ? tr(" and ") : "",
                 pad2 != nullptr ? pad2 : "");
    } else {
        snprintf(line, sizeof line, tr("Player 1, the left Gear: the keyboard%s%s. Player 2, the right Gear: %s."),
                 pad1 != nullptr ? tr(" and ") : "", pad1 != nullptr ? pad1 : "",
                 pad2 != nullptr ? pad2 : tr("a second gamepad (none connected)"));
    }
    markBlock();
    auto showDevices = [](const char* player, const char* gamepad, bool keyboard) {
        if (gamepad && keyboard) {
            ImGui::Text("%s: %s + %s", player, gamepad, tr("Keyboard"));
        } else {
            const char* device = gamepad ? gamepad : tr(keyboard ? "Keyboard" : "No gamepad is connected.");
            ImGui::Text("%s: %s", player, device);
        }
    };
    showDevices(tr("Player 1"), pad1, !keyboard2);
    showDevices(tr("Player 2"), pad2, keyboard2);
    hint("%s", line);
    if (pcArenaKeyboardOffered()) {
        bool on = keyboard2;
        if (ImGui::Checkbox(tr("Player 2 on the keyboard"), &on)) {
            sfx::play(sfx::CHOOSE);
            pcArenaSetPlayer2Keyboard(on);
        }
        hint("Player 1 then plays on the gamepad.");
    }
}

// The battle music (xg-htn.35.146; system/arena.c): the jukebox's scores by
// their titles, the game's own first; while the list is open the highlighted
// score plays quietly (the jukebox's player, over the paused game), and stops
// when the list closes (or the menu: extrasJukeboxEnd). The setting is read
// where the battle's music starts; online the host's is both players'.
struct {
    bool open = false;   // the list was open the frame before
    int previewing = -2; // the music ID it previews (-1: none; -2: nothing asked)
    int labelFor = -1;   // the setting the label is of
    std::string label;   // its title (kept while the menu is closed: no disc read to show it)
} BM;

void musicPicker() {
    if (extrasJukeboxSongCount == nullptr) return; // a build without the extras: no list
    markBlock();
    ImGui::TextUnformatted(tr("Battle music"));
    int value = pcArenaMusicSetting();
    if (value == 0) {
        BM.label = tr("Game's default");
        BM.labelFor = 0;
    } else if (BM.labelFor != value) {
        int n = extrasJukeboxSongCount();
        BM.label.clear();
        for (int i = 0; i < n && BM.label.empty(); i++) {
            int id = -1;
            const char* t = extrasJukeboxSongTitle(i, &id);
            if (t != nullptr && id + 1 == value) BM.label = t;
        }
        if (BM.label.empty()) {
            char text[64];
            snprintf(text, sizeof text, tr("Music %d"), value - 1);
            BM.label = text;
        }
        BM.labelFor = value;
    }
    ImGui::BeginDisabled(settingsPinned("arena_music") != 0);
    ImGui::SameLine();
    float width = fminf(skin::valueWidth(), ImGui::GetContentRegionAvail().x);
    bool open = skin::beginCombo("##arenamusic", BM.label.c_str(), 0, width);
    if (open) {
        int n = extrasJukeboxSongCount();
        int highlighted = -2;
        if (skin::comboItem(tr("Game's default"), value == 0)) pcArenaSetMusic(0);
        if (value == 0) ImGui::SetItemDefaultFocus();
        if (ImGui::IsItemHovered() || ImGui::IsItemFocused()) highlighted = -1;
        for (int i = 0; i < n; i++) {
            int id = -1;
            const char* t = extrasJukeboxSongTitle(i, &id);
            if (t == nullptr || id < 0 || id >= ARENA_MUSIC_IDS) continue;
            char label[256];
            snprintf(label, sizeof label, "%s##song%d", t, i);
            bool chosen = value == id + 1;
            if (skin::comboItem(label, chosen)) pcArenaSetMusic(id + 1);
            if (chosen) ImGui::SetItemDefaultFocus();
            if (ImGui::IsItemHovered() || ImGui::IsItemFocused()) highlighted = id;
        }
        skin::endCombo();
        if (highlighted != -2 && highlighted != BM.previewing) {
            extrasJukeboxPreview(highlighted);
            BM.previewing = highlighted;
        }
    } else if (BM.open) {
        extrasJukeboxPreview(-1); // the list closed: the preview stops
        BM.previewing = -2;
    }
    BM.open = open;
    ImGui::EndDisabled();
    hint("The battle music of Battling and versus battles: the game's own (Battling's theme, the regular battle "
         "theme), or any of the jukebox's songs. Online, the host's choice plays for both players.");
}

// The stage of a mirror match or party vs party (xg-htn.35.143; system/arena.c
// lists the stages, each checked by a run): named after where it appears in
// the game (the bestiary's places), with the rooms its battles are in. The
// setting is read when the match is asked; online the host's.
struct StageName {
    int id;            // the stage's number (the battle config's arena)
    const char* name;  // the place
    const char* where; // where in it the game's battles on this stage are
};
const StageName stageNames[] = {
    {3, N_("Mountain Path"), N_("The trail from Lahan up to Citan's house.")},
    {1, N_("Blackmoon Forest"), N_("Its entrance, the large tree and the Rankar clearing.")},
    {7, N_("Duneman Isle"), N_("The Aveh desert's dunes, below the sandfall and by the skeleton remains.")},
    {14, N_("Nisan"), N_("The town gate.")},
    {13, N_("Aveh Castle"), N_("A castle hall: up the ladder from the Bledavik sewers, and the tower passage.")},
    {9, N_("Nortune sewers"), N_("Under Kislev's capital: the channels and the maintenance passages.")},
    {10, N_("Nortune Gear dock"), N_("Under the battle arena: the hangar stairs, the storerooms and passages.")},
    {50, N_("Ethos headquarters"), N_("The front hall, the bishop's stairs and the landings.")},
    {71, N_("Ethos transport"), N_("On deck, outside the Ethos ship.")},
    {23, N_("Zeboim"), N_("The buried city under the Ethos dig site, and the dig site's transfer lobby.")},
    {30, N_("Lighthouse"), N_("The underground landing below the lighthouse: a ruined city.")},
    {35, N_("Shevat"), N_("The outer escape lift and the terrace.")},
    {59, N_("Solaris"), N_("Etrenank's lower levels, the Soylent System and Krelian's laboratory.")},
    {32, N_("Anima Relic 1"), N_("The relic's corridors and lift lobby, and the Nisan mausoleum's metal bridge.")},
    {44, N_("Grassland (world map)"), N_("The world map's grassland, where most random battles on foot are.")},
    {47, N_("Badlands (world map)"), N_("The world map's rocky wastes.")},
    {43, N_("Snowfield (world map)"), N_("The world map's snowfields.")},
};

const StageName* stageName(int id) {
    for (const StageName& s : stageNames)
        if (s.id == id) return &s;
    return nullptr;
}

void stagePicker() {
    markBlock();
    int value = pcArenaStageSetting(); // always one of the list's (Blackmoon Forest by default)
    char label[160];
    const StageName* shown = stageName(value - 1);
    if (shown == nullptr) snprintf(label, sizeof label, tr("Stage %d"), value - 1);
    ImGui::TextUnformatted(tr("Battle stage"));
    ImGui::BeginDisabled(settingsPinned("arena_stage") != 0);
    if (skin::beginCombo("##arenastage", shown != nullptr ? tr(shown->name) : label)) {
        for (int i = 0; i < pcArenaStageCount(); i++) {
            int id = pcArenaStageId(i);
            const StageName* s = stageName(id);
            char item[192], number[64];
            snprintf(number, sizeof number, tr("Stage %d"), id);
            snprintf(item, sizeof item, "%s##stage%d", s != nullptr ? tr(s->name) : number, id);
            bool chosen = value == id + 1;
            if (skin::comboItem(item, chosen)) pcArenaSetStage(id + 1);
            if (chosen) ImGui::SetItemDefaultFocus();
        }
        skin::endCombo();
    }
    ImGui::EndDisabled();
    if (shown != nullptr) hint("%s", tr(shown->where));
    hint("The party and player 2 stand where the game's own battles there put a party and its enemies.");
}

// The versus battle's party: the current one, Fei and Citan as a new game
// makes them, or members picked (as the save has them; a new game's before
// one is loaded, and online).
enum { PARTY_CURRENT, PARTY_NEW, PARTY_PICK };
const char* const memberNames[11] = {N_("Fei"),  N_("Elly"),     N_("Citan"),   N_("Bart"),  N_("Billy"),
                                     N_("Rico"), N_("Emeralda"), N_("Chu-Chu"), N_("Maria"), N_("Citan (sword)"),
                                     N_("Emeralda (adult)")};
struct PartyChoice {
    int mode = PARTY_CURRENT;
    int picked[3] = {0, 2, 255}; // in order, the first the leader
} parties[2];

// The party the picker gives now: ids (255 none); fromSave: as the save has them.
bool pickedParty(int party[3], bool online, bool* fromSave) {
    const PartyChoice& P = parties[online];
    bool loaded = pcArenaGameLoaded() != 0;
    *fromSave = loaded && !online;
    int mode = P.mode == PARTY_CURRENT && !loaded ? PARTY_NEW : P.mode;
    if (mode == PARTY_CURRENT) {
        if (pcArenaPartyNow(party) == 0) mode = PARTY_NEW;
    }
    if (mode == PARTY_NEW) {
        party[0] = 0;
        party[1] = 2;
        party[2] = 255;
        *fromSave = false;
    } else if (mode == PARTY_PICK) {
        for (int i = 0; i < 3; i++) party[i] = P.picked[i];
    }
    return party[0] != 255;
}

void partyPicker(bool online) {
    markBlock();
    PartyChoice& P = parties[online];
    bool loaded = pcArenaGameLoaded() != 0;
    if (!loaded && P.mode == PARTY_CURRENT) P.mode = PARTY_NEW;
    ImGui::TextUnformatted(tr("Your party"));
    ImGui::BeginDisabled(!loaded);
    if (ImGui::RadioButton(tr("The current party"), P.mode == PARTY_CURRENT)) {
        sfx::play(sfx::CHOOSE);
        P.mode = PARTY_CURRENT;
    }
    ImGui::EndDisabled();
    if (ImGui::RadioButton(tr("Fei and Citan, new game"), P.mode == PARTY_NEW)) {
        sfx::play(sfx::CHOOSE);
        P.mode = PARTY_NEW;
    }
    if (ImGui::RadioButton(tr("Choose the members##party"), P.mode == PARTY_PICK)) {
        sfx::play(sfx::CHOOSE);
        P.mode = PARTY_PICK;
    }
    if (P.mode == PARTY_PICK) {
        int joined = loaded && !online ? pcArenaMembersJoined() : 0x7FF;
        int count = 0;
        for (int i = 0; i < 3; i++) count += P.picked[i] != 255;
        for (int c = 0; c < 11; c++) {
            if (!(joined & (1 << c))) continue;
            int at = -1;
            for (int i = 0; i < 3; i++) {
                if (P.picked[i] == c) at = i;
            }
            bool on = at >= 0;
            char label[96];
            snprintf(label, sizeof label, "%s##member%d", tr(memberNames[c]), c);
            ImGui::BeginDisabled(!on && count >= 3);
            if (ImGui::Checkbox(label, &on)) {
                sfx::play(sfx::CHOOSE);
                if (on) {
                    for (int i = 0; i < 3; i++) {
                        if (P.picked[i] == 255) {
                            P.picked[i] = c;
                            break;
                        }
                    }
                } else {
                    for (int i = at; i < 2; i++) P.picked[i] = P.picked[i + 1]; // the others move up
                    P.picked[2] = 255;
                }
            }
            ImGui::EndDisabled();
        }
        if (P.picked[0] == 255) hint("Choose one to three members; the first leads.");
    }
    if (online) {
        hint("Online, members come as a new game makes them: their starting levels, equipment and Gears, never "
             "anyone's save.");
    } else if (!loaded) {
        hint("No game is loaded: members come as a new game makes them.");
    } else if (P.mode != PARTY_NEW) {
        hint("Members come as your save has them (levels, equipment, abilities, Gears), HP and EP full.");
    }
}

struct FormationChoice {
    int selected = -1;
    char filter[48] = "";
};
struct {
    XgVersusScan* scan = nullptr;
    bool scanned = false;
    std::string disc;
    std::vector<XgVersusFormation> list;
    FormationChoice local, online;
} formations;

int matchChoice(const char* const* labels, int count, int selected) {
    markBlock();
    ImGui::TextUnformatted(tr("Match type"));
    ImGui::SameLine();
    float width = fminf(skin::valueWidth(), ImGui::GetContentRegionAvail().x);
    if (skin::beginCombo("##match-type", tr(labels[selected]), 0, width)) {
        for (int i = 0; i < count; ++i) {
            if (skin::comboItem(tr(labels[i]), selected == i) && selected != i) {
                sfx::play(sfx::CHOOSE);
                selected = i;
            }
        }
        skin::endCombo();
    }
    return selected;
}

void allGearsOption() {
    markBlock();
    bool all = pcArenaAllGearsSetting() != 0;
    if (ImGui::Checkbox(tr("Every Gear"), &all)) {
        sfx::play(sfx::CHOOSE);
        pcArenaSetAllGears(all);
    }
    hint("All 49 Gears to choose from, Argento too, not only those the story has reached.");
}

bool matches(const XgVersusFormation& f, const char* filter) {
    if (filter[0] == '\0') return true;
    std::string a = f.label, b = filter;
    for (auto& c : a) c = (char)tolower((unsigned char)c);
    for (auto& c : b) c = (char)tolower((unsigned char)c);
    return a.find(b) != std::string::npos;
}

// Why a match cannot start now (pcArenaMatchAvailable), or where it starts.
void availability(bool available, const char* why) {
    if (available) {
        hint("From anywhere: your game waits in memory meanwhile (save states are off until you leave).");
    } else if (netplayActive()) {
        hint("Not in an online session's window.");
    } else if (pcArenaMatchKind() != 0) {
        hint("A match is on.");
    } else if (why != nullptr) {
        hint("%s", why);
    }
}

// A match from the menu (system/arena.c): where it is, and the way back.
void matchState(int phase, void (*resumeGame)()) {
    switch (phase) {
    case ARENA_PHASE_PENDING:
        ImGui::TextUnformatted(tr("Starting the match..."));
        break;
    case ARENA_PHASE_LEAVING:
        ImGui::TextUnformatted(tr("Back to your game..."));
        break;
    default:
        markBlock();
        hint("In a match: at its end both players choose Rematch or Leave. Leave brings your game back as it "
                  "was.");
        if (ImGui::Button(tr("Leave the match"))) {
            sfx::play(sfx::BACK);
            pcArenaMatchQuit();
            resumeGame();
        }
        break;
    }
}

// A mirror match (versusBlock): player 2 commands a copy of the party
// (include/pc_arena.h, port/src/game/battle/mirror.c); no formation to choose.
void mirrorBlock(void (*resumeGame)()) {
    partyPicker(false);
    const char* why = nullptr;
    bool available = pcArenaMatchAvailable(&why) != 0;
    int party[3];
    bool fromSave = false;
    bool partyOk = pickedParty(party, false, &fromSave);
    markBlock();
    ImGui::BeginDisabled(!available || !partyOk);
    if (ImGui::Button(tr("Fight##mirror"))) {
        if (pcArenaMatchStart(ARENA_MATCH_MIRROR, 0, 0, party, fromSave)) {
            sfx::play(sfx::CHOOSE);
            resumeGame();
        } else {
            sfx::play(sfx::BUZZ);
        }
    }
    ImGui::EndDisabled();
    availability(available, why);
}

// Party vs party (xg-htn.35.143): each player's party from a memory-card save
// (arena_saves.cpp), player 1's against player 2's on one machine; the match
// as a mirror match's (system/arena.c pcArenaMatchStartParty,
// port/src/game/battle/mirror.c).
void partyBlock(void (*resumeGame)()) {
    static XgPartyRecord record[2], logged[2];
    static bool haveLogged[2] = {false, false};
    bool ok[2] = {false, false};
    for (int p = 0; p < 2; p++) {
        ImGui::Dummy(ImVec2(0, 4 * skin::unit()));
        ok[p] = arenaSavesPanel(p + 1, &record[p]);
        if (!ok[p]) {
            haveLogged[p] = false;
            continue;
        }
        std::string members, english; // shown, and for the log
        char line[96];
        XgPartyMember m;
        for (int k = 0; xgPartyMemberInfo(&record[p], k, &m); k++) {
            snprintf(line, sizeof line, tr("%s (level %d)"), tr(memberNames[m.id]), m.level);
            if (k > 0) members += " / ";
            members += line;
            snprintf(line, sizeof line, "%s%s %d, HP %d/%d, EP %d/%d", k > 0 ? "; " : "", memberNames[m.id], m.level,
                     m.hp, m.maxHp, m.ep, m.maxEp);
            english += line;
        }
        int items = 0, rounds = 0;
        while (items < XG_PARTY_ITEMS && record[p].m298_itemIds[items] != 0) items++;
        while (rounds < XG_PARTY_ITEMS && record[p].m2F8_ammoIds[rounds] != 0) rounds++;
        hint("Party: %s, with %d kinds of items and %d kinds of rounds.", members.c_str(), items, rounds);
        if (!haveLogged[p] || memcmp(&logged[p], &record[p], sizeof record[p]) != 0) {
            uint32_t h = 2166136261u; // FNV-1a of the record's bytes: the same on every machine
            for (size_t i = 0; i < sizeof record[p]; i++) h = (h ^ ((const uint8_t*)&record[p])[i]) * 16777619u;
            pcArenaLog("party vs party: player %d's party record (%u bytes, fnv1a %08x): %s; %d items, %d ammo", p + 1,
                       (unsigned)sizeof record[p], (unsigned)h, english.c_str(), items, rounds);
            logged[p] = record[p];
            haveLogged[p] = true;
        }
    }
    ImGui::Dummy(ImVec2(0, 4 * skin::unit()));
    const char* why = nullptr;
    bool available = pcArenaMatchAvailable(&why) != 0;
    markBlock();
    ImGui::BeginDisabled(!available || !ok[0] || !ok[1]);
    if (ImGui::Button(tr("Fight##party"))) {
        if (pcArenaMatchStartParty(&record[0], &record[1], nullptr)) {
            sfx::play(sfx::CHOOSE);
            resumeGame();
        } else {
            sfx::play(sfx::BUZZ);
        }
    }
    ImGui::EndDisabled();
    if (available && (!ok[0] || !ok[1])) hint("Choose a party for each player.");
    availability(available, why);
}

// Each player sees their own side's HP and EP; the opponent's only with this
// (system/arena.c; the Enemy info window, port/src/game/battle/enemyinfo.c).
void opponentHpOption() {
    markBlock();
    bool opponentHp = pcVersusOpponentHpSetting() != 0;
    ImGui::BeginDisabled(settingsPinned("arena_opponent_hp") != 0);
    if (ImGui::Checkbox(tr("Opponent's HP and EP"), &opponentHp)) {
        sfx::play(sfx::CHOOSE);
        pcVersusSetOpponentHp(opponentHp);
    }
    ImGui::EndDisabled();
    hint("In battle, Select shows the opponent's members with their HP and EP (the Enemy info window). Off, "
         "neither player sees the other's. Online, the host's choice holds for both players.");
}

const XgVersusFormation* formationPicker(FormationChoice& choice) {
    markBlock();
    if (xgVersusScanBegin == nullptr) {
        hint("Not in this build (it reads the battles with the extras).");
        return nullptr;
    }
    const char* path = cdSwapPath(0);
    if (formations.disc != (path ? path : "")) { // another disc in the drive: its tables
        if (formations.scan != nullptr) xgVersusScanFree(formations.scan);
        formations = {};
        formations.disc = path ? path : "";
    }
    if (!formations.scanned) {
        if (formations.scan == nullptr) formations.scan = xgVersusScanBegin();
        if (formations.scan == nullptr || xgVersusScanStep(formations.scan, 12)) {
            if (formations.scan != nullptr) {
                XgVersusFormation* l = nullptr;
                int n = xgVersusScanResult(formations.scan, &l);
                if (n > 0) formations.list.assign(l, l + n);
                free(l);
                xgVersusScanFree(formations.scan);
                formations.scan = nullptr;
            }
            formations.scanned = true;
        } else {
            ImGui::ProgressBar((float)xgVersusScanProgress(formations.scan), ImVec2(-1, 0), tr("Reading the battles"));
            return nullptr;
        }
    }
    if (formations.list.empty()) {
        hint("No battles found on the disc in the drive.");
        return nullptr;
    }
    ImGui::TextUnformatted(tr("Enemy formation"));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##versusfilter", tr("Filter by enemy"), choice.filter, sizeof choice.filter);
    // The keyboard and gamepad enter the list (confirm on it) and move in it; a
    // formation chosen there takes the cursor on to the controls below, as a
    // list box's own child would keep it in the list (xg-htn.35.148).
    bool chosenByNav = false;
    if (ImGui::BeginListBox("##versuslist", ImVec2(-1, 6 * ROW * skin::unit()))) {
        for (int i = 0; i < (int)formations.list.size(); i++) {
            const XgVersusFormation& f = formations.list[i];
            if (!matches(f, choice.filter)) continue;
            char label[160];
            snprintf(label, sizeof label, "%s##f%d", f.label, i);
            if (ImGui::Selectable(label, choice.selected == i)) {
                if (choice.selected != i) sfx::play(sfx::MOVE);
                choice.selected = i;
                chosenByNav = ImGui::GetCurrentContext()->NavActivateId != 0;
            }
        }
        ImGui::EndListBox();
    }
    if (chosenByNav) ImGui::SetKeyboardFocusHere();
    if (choice.selected < 0) {
        hint("Choose an enemy formation.");
        return nullptr;
    }
    const auto& selected = formations.list[choice.selected];
    hint("Battle table %d, config %d.", selected.table, selected.config);
    return &selected;
}

// Versus battles: player 2 commands an enemy formation, or a copy of the
// party (a mirror match) (include/pc_arena.h), or party vs party.
void versusBlock(void (*resumeGame)()) {
    int kind = 0;
    int match = pcArenaMatchPhase(&kind);
    if (match != ARENA_PHASE_IDLE &&
        (kind == ARENA_MATCH_VERSUS || kind == ARENA_MATCH_MIRROR || kind == ARENA_MATCH_PARTY)) {
        matchState(match, resumeGame);
        return;
    }
    int table = 0, config = 0;
    int phase = pcVersusPhase(&table, &config);
    if (phase == ARENA_PHASE_PENDING) {
        ImGui::TextUnformatted(tr("Asked: the battle starts as soon as you are free to walk around in a field."));
        if (ImGui::Button(tr("Cancel##versus"))) {
            sfx::play(sfx::BACK);
            pcVersusCancel();
        }
        return;
    }
    if (phase != ARENA_PHASE_IDLE) {
        ImGui::TextUnformatted(tr("In a versus battle."));
        return;
    }
    enum { Formation, Mirror, Party };
    static const char* const modes[] = {N_("An enemy formation"), N_("Mirror match"), N_("Party vs party")};
    static int mode = Formation;
    mode = matchChoice(modes, IM_COUNTOF(modes), mode);
    switch (mode) {
    case Mirror:
        hint("Player 2 commands a copy of your party on foot, with its own command ring and copy of your items.");
        break;
    case Party:
        hint("Each player controls a party from a memory-card save. Both parties fight on foot.");
        break;
    default:
        hint("Player 2 uses the second controller to choose attacks and targets for an enemy formation.");
        break;
    }
    musicPicker();
    opponentHpOption();
    if (mode == Mirror || mode == Party) stagePicker();
    if (mode == Mirror) {
        mirrorBlock(resumeGame);
        return;
    }
    if (mode == Party) {
        partyBlock(resumeGame);
        return;
    }
    const XgVersusFormation* formation = formationPicker(formations.local);
    markBlock();
    bool gears = pcVersusGearsSetting() != 0;
    if (ImGui::Checkbox(tr("Party in its Gears"), &gears)) {
        sfx::play(sfx::CHOOSE);
        pcVersusSetGears(gears);
    }
    hint("Gear against Gear: the members who have a Gear fight in it (pick a formation of Gears).");
    partyPicker(false);
    const char* why = nullptr;
    bool available = pcArenaMatchAvailable(&why) != 0;
    int party[3];
    bool fromSave = false;
    bool partyOk = pickedParty(party, false, &fromSave);
    markBlock();
    ImGui::BeginDisabled(!available || formation == nullptr || !partyOk);
    if (ImGui::Button(tr("Fight"))) {
        if (pcArenaMatchStart(ARENA_MATCH_VERSUS, formation->table, formation->config, party, fromSave)) {
            sfx::play(sfx::CHOOSE);
            resumeGame();
        } else {
            sfx::play(sfx::BUZZ);
        }
    }
    ImGui::EndDisabled();
    availability(available, why);
}

enum OnlineMatch { OnlineBattling, OnlineVersus, OnlineParty };

struct {
    bool loaded = false;
    bool host = true;
    char code[64] = "";
    char address[256] = "";
    int port = 7461;
    int delay = 3;
    int mode = OnlineBattling;
    bool joinerPicks = true;
    XgPartyRecord record;    // this machine's party (the online browser's)
} O;

// Why the last session ended (netplay.c's word on its status line).
void endText(const char* d, char* out, size_t size) {
    const char* rest = strchr(d, ' ');
    rest = rest != nullptr ? rest + 1 : "";
    struct {
        const char* word;
        const char* text;
    } const ends[] = {
        {"battling", N_("Battling was left (EXIT in its menu).")},
        {"versus p1", N_("The versus battle is over: player 1 (the host) won.")},
        {"versus p2", N_("The versus battle is over: player 2 won.")},
        {"versus escaped", N_("The versus battle is over: player 1 (the host) escaped.")},
        {"leave", N_("A player chose Leave at the match's end.")},
        {"left", N_("The other player left.")},
        {"silent", N_("The other player stopped answering.")},
        {"quit", N_("The session's window was closed.")},
        {"nobody", N_("Nobody joined in time.")},
        {"noanswer", N_("No answer from the host: check the address, the port, the host's firewall and the code.")},
        {"build", N_("The host runs another version of the game: both players need the same one.")},
        {"region", N_("The host plays the other region's game (US/JP).")},
        {"busy", N_("The host is busy.")},
        {"pick", N_("Select Versus battle and choose an enemy formation, then join again.")},
        {"disc", N_("The other player's disc is not the same as yours.")},
        {"noparty", N_("Select Party vs party and choose a save, then join again.")},
        {"stage", N_("The host's battle stage is not one of this game's.")},
        {"address", N_("The address is not host:port, or that host is unknown.")},
        {"error", N_("The network could not be used (is the port in use?).")},
    };
    for (const auto& e : ends) {
        if (strncmp(d, e.word, strlen(e.word)) == 0 && (d[strlen(e.word)] == '\0' || d[strlen(e.word)] == ' ')) {
            snprintf(out, size, "%s", tr(e.text));
            return;
        }
    }
    // party vs party: "<word> <reason> <character id>" (netplay.c's header)
    int reason = 0, member = 255;
    char why[320];
    if (sscanf(d, "partyrefused %d %d", &reason, &member) >= 1) {
        arenaPartyReason(reason, member, why, sizeof why);
        snprintf(out, size, tr("The other player's game refused your party: %s"), why);
        return;
    }
    if (sscanf(d, "peerparty %d %d", &reason, &member) >= 1) {
        arenaPartyReason(reason, member, why, sizeof why);
        snprintf(out, size, tr("The host's party could not be used here: %s"), why);
        return;
    }
    if (sscanf(d, "ownparty %d %d", &reason, &member) >= 1) {
        arenaPartyReason(reason, member, why, sizeof why);
        snprintf(out, size, tr("Your party could not be used: %s"), why);
        return;
    }
    if (strncmp(d, "desync ", 7) == 0) {
        snprintf(out, size, tr("The two games went apart at vblank %s, so the session stopped."), rest);
    } else if (strncmp(d, "exit ", 5) == 0) {
        snprintf(out, size, tr("The session's window closed (exit code %s)."), rest);
    } else if (strncmp(d, "start ", 6) == 0) {
        snprintf(out, size, tr("The session could not start: %s"), rest);
    } else {
        snprintf(out, size, "%s", d);
    }
}

// Online: Battling with a player elsewhere, in a process of its own.
void onlineBlock() {
    if (netplayActive()) {
        markBlock();
        ImGui::TextUnformatted(tr("Online match in progress."));
        hint("At the end of a match, choose Rematch or Leave. EXIT in Battling closes the session.");
        return;
    }
    if (!O.loaded) {
        O.loaded = true;
        O.port = settingsInt("netplay_port", 7461);
        O.delay = settingsInt("netplay_delay", 3);
        const char* a = settingsGet("netplay_address");
        if (a != nullptr) snprintf(O.address, sizeof O.address, "%s", a);
    }
    int state = netplaySessionState();
    char line[512];
    if (state != NETPLAY_IDLE && state != NETPLAY_OVER) {
        const char* d = netplaySessionDetail();
        switch (state) {
        case NETPLAY_HOSTING:
            snprintf(line, sizeof line, tr("Waiting for player 2 on UDP port %s. They join with your address, this "
                                           "port and the code."), d);
            break;
        case NETPLAY_JOINING:
            snprintf(line, sizeof line, tr("Joining %s..."), d);
            break;
        case NETPLAY_CONNECTED:
            snprintf(line, sizeof line, tr("Playing with %s (input delay %d). They see your address, as you see "
                                           "theirs."), d, netplaySessionDelay());
            break;
        default:
            snprintf(line, sizeof line, "%s", tr("Starting the session..."));
            break;
        }
        textLines(line);
        int refusedReason = 0, refusedMember = 255;
        if (state == NETPLAY_HOSTING && netplaySessionRefusedParty(&refusedReason, &refusedMember)) {
            char why[320];
            arenaPartyReason(refusedReason, refusedMember, why, sizeof why);
            snprintf(line, sizeof line, tr("A player joined with a party that cannot be used, and was refused (they "
                                           "see why): %s Waiting for another."), why);
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textWarning);
            ImGui::TextWrapped("%s", line);
            ImGui::PopStyleColor();
        }
        if (netplaySessionPaused()) ImGui::TextUnformatted(tr("The other player paused."));
        if (netplaySessionLost()) {
            textLines("The connection is lost: the session waits for it to come back (up to a minute), then "
                      "goes on where it stopped.");
        }
        markBlock();
        if (ImGui::Button(tr("End the session"))) {
            sfx::play(sfx::BACK);
            netplaySessionStop();
        }
        hint("End the match for both players and return to the paused game.");
        return;
    }
    if (state == NETPLAY_OVER) {
        char why[400];
        endText(netplaySessionDetail(), why, sizeof why);
        snprintf(line, sizeof line, tr("The last session: %s"), why);
        textLines(line);
    }
    markBlock();
    if (ImGui::RadioButton(tr("Host (player 1)"), O.host)) O.host = true;
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Join (player 2)"), !O.host)) O.host = false;
    hint("Use the same game version, disc and session code. A session opens in its own window.");
    markBlock();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##netplaycode", tr("Session code"), O.code, sizeof O.code);
    hint("The same word for both players. It keeps others out of the session; it does not encrypt it.");
    if (O.host) {
        markBlock();
        ImGui::SetNextItemWidth(6 * ROW * skin::unit());
        if (ImGui::InputInt(tr("UDP port"), &O.port)) O.port = std::clamp(O.port, 1024, 65535);
        hint("Use a reachable UDP port. Play on the same network, over a VPN, or forward the port on the host's router.");
        markBlock();
        ImGui::SetNextItemWidth(6 * ROW * skin::unit());
        ImGui::SliderInt(tr("Input delay"), &O.delay, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp);
        hint("Vblanks between a press and what it does, the same on both screens: 2 on a home network, 3 to 6 over "
             "the internet.");
    } else {
        markBlock();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##netplayaddress", tr("The host's address:port"), O.address, sizeof O.address);
        hint("A name or an IPv4 address, then the port (192.168.1.20:7461); an IPv6 address goes in brackets "
             "([2001:db8::20]:7461).");
    }
    static const char* const modes[] = {N_("Battling"), N_("Versus battle"), N_("Party vs party")};
    O.mode = matchChoice(modes, IM_COUNTOF(modes), O.mode);
    hint("Choose the same match type on both sides.");
    const bool bringing = O.mode == OnlineParty;
    bool partyOk = false;
    const XgVersusFormation* formation = nullptr;
    switch (O.mode) {
    case OnlineBattling:
        if (O.host) allGearsOption();
        break;
    case OnlineVersus:
        if (O.host) {
            markBlock();
            ImGui::Checkbox(tr("Player 2 chooses the formation"), &O.joinerPicks);
            hint("On, Player 2 chooses the enemy formation when joining. Off, the host chooses it here.");
        }
        if (!O.host || !O.joinerPicks) formation = formationPicker(formations.online);
        if (O.host) {
            opponentHpOption();
            partyPicker(true);
        }
        break;
    case OnlineParty:
        if (O.host) {
            stagePicker();
            opponentHpOption();
        }
        partyOk = arenaSavesPanel(0, &O.record);
        hint("Both players bring a party from their own save. Edited saves are accepted within the game's ranges.");
        break;
    }
    if (O.host) musicPicker();
    markBlock();
    bool ready = O.code[0] != '\0' && (!bringing || partyOk) &&
                 (O.host ? O.mode != OnlineVersus || O.joinerPicks || formation : strchr(O.address, ':') != nullptr);
    ImGui::BeginDisabled(!ready);
    const char* start = O.host ? tr("Host##netplay") : tr("Join##netplay");
    if (ImGui::Button(start)) {
        NetplayStart s = {};
        s.host = O.host;
        snprintf(s.code, sizeof s.code, "%s", O.code);
        s.port = O.port;
        snprintf(s.address, sizeof s.address, "%s", O.address);
        s.delay = O.delay;
        s.progress = pcArenaAllGearsSetting() ? 1000 : netplaySessionStory();
        s.versus = O.host && O.mode == OnlineVersus;
        s.partyVsParty = O.host && bringing;
        s.hasParty = bringing && partyOk;
        if (s.hasParty) s.record = O.record;
        s.joinerPicks = s.versus && O.joinerPicks;
        s.pick = !O.host && formation;
        if (formation) {
            s.table = formation->table;
            s.config = formation->config;
        }
        if (s.versus) {
            bool fromSave = false;
            if (!pickedParty(s.party, true, &fromSave)) netplaySessionParty(s.party);
        }
        settingsDescribe("netplay_port", "Online play (F1 > Extras): the UDP port to host on (default 7461)");
        settingsDescribe("netplay_delay", "Online: the input delay to host with, in vblanks (1..8; default 3)");
        settingsDescribe("netplay_address", "Online: the last host joined, host:port");
        settingsSetInt("netplay_port", O.port);
        settingsSetInt("netplay_delay", O.delay);
        if (!O.host) settingsSet("netplay_address", O.address);
        settingsSave();
        sfx::play(netplayLaunch(&s) ? sfx::CHOOSE : sfx::BUZZ);
    }
    ImGui::EndDisabled();
    if (O.code[0] == '\0') hint("Enter the session code.");
    else if (!O.host && strchr(O.address, ':') == nullptr) hint("Enter the host's address and port.");
    else if (bringing && !partyOk) hint("Choose a party from a memory-card save.");
    else if (!ready) hint("Choose an enemy formation or let Player 2 choose.");
    else hint("Both players see each other's IP address. The host's gameplay settings apply to both players.");
}

void battlingBlock(void (*resumeGame)()) {
    players();
    allGearsOption();
    musicPicker();
    markBlock();
    int kind = 0;
    int match = pcArenaMatchPhase(&kind);
    switch (match != ARENA_PHASE_IDLE ? -1 : pcArenaPhase()) {
    case -1:
        if (kind == ARENA_MATCH_BATTLING) {
            matchState(match, resumeGame);
        } else {
            ImGui::TextUnformatted(tr("In a versus battle."));
        }
        break;
    case ARENA_PHASE_IDLE: {
        const char* why = nullptr;
        bool available = pcArenaMatchAvailable(&why) != 0;
        ImGui::BeginDisabled(!available);
        if (ImGui::Button(tr("Go to the arena"))) {
            if (pcArenaMatchStart(ARENA_MATCH_BATTLING, 0, 0, nullptr, 0)) {
                sfx::play(sfx::CHOOSE);
                resumeGame();
            } else {
                sfx::play(sfx::BUZZ);
            }
        }
        ImGui::EndDisabled();
        availability(available, why);
        if (available) hint("Choose PLAYER1 VS PLAYER2 in Battling. Both players can rematch or leave after a bout.");
        break;
    }
    case ARENA_PHASE_PENDING:
        ImGui::TextUnformatted(tr("Asked: it opens as soon as you are free to walk around in a field."));
        if (ImGui::Button(tr("Cancel"))) {
            sfx::play(sfx::BACK);
            pcArenaCancel();
        }
        break;
    case ARENA_PHASE_STARTING:
    case ARENA_PHASE_LEAVING:
        ImGui::TextUnformatted(tr("On the way to the arena."));
        break;
    default:
        ImGui::TextUnformatted(tr("In the arena: EXIT in its main menu brings you back."));
        break;
    }
}

} // namespace

void arenaSection(ArenaPart part, void (*resumeGame)()) {
    ImGui::PushID(static_cast<int>(part));
    switch (part) {
    case ArenaPart::Battling: battlingBlock(resumeGame); break;
    case ArenaPart::Versus: versusBlock(resumeGame); break;
    case ArenaPart::Online: onlineBlock(); break;
    }
    ImGui::PopID();
}

void arenaFallback(void (*resumeGame)()) {
    if (!netplayActive()) {
        ImGui::SeparatorText(tr("Battle arena"));
        arenaSection(ArenaPart::Battling, resumeGame);
        ImGui::SeparatorText(tr("Versus battle"));
        arenaSection(ArenaPart::Versus, resumeGame);
    }
    ImGui::SeparatorText(tr("Online"));
    arenaSection(ArenaPart::Online, resumeGame);
}

} // namespace menu
