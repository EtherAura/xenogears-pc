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
#include "options.h"
#include "sfx.h"
#include "skin.h"

extern "C" {
#include "cd/cd.h"
#include "extras/versus_list.h"
#include "extras/bestiary_data.h"
#include "system/arena_roster.h"
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
extern "C" XgDisc* xgDiscOpen(const char*, const char**) __attribute__((weak));
extern "C" void xgDiscClose(XgDisc*) __attribute__((weak));
extern "C" int xgBeastsRead(XgDisc*, XgBeast**, int) __attribute__((weak));
// ... and the jukebox's scores (extras/extras.h).
extern "C" int extrasJukeboxSongCount(void) __attribute__((weak));
extern "C" const char* extrasJukeboxSongTitle(int i, int* musicId) __attribute__((weak));
extern "C" void extrasJukeboxPreview(int musicId) __attribute__((weak));

namespace menu {

namespace {

bool needsArenaPicture;
bool launchAfterArenaPicture;
NetplayStart nextOnlineMatch;

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
    auto showDevices = [](const char* player, const char* gamepad, bool keyboard) {
        opt::row(player);
        if (gamepad && keyboard) {
            ImGui::Text("%s + %s", gamepad, tr("Keyboard"));
        } else {
            const char* device = gamepad ? gamepad : tr(keyboard ? "Keyboard" : "No gamepad is connected.");
            textLines(device);
        }
    };
    showDevices(N_("Player 1"), pad1, !keyboard2);
    hint("%s", line);
    showDevices(N_("Player 2"), pad2, keyboard2);
    hint("%s", line);
    if (pcArenaKeyboardOffered()) {
        opt::row(N_("Player 2 on the keyboard"));
        bool on = keyboard2;
        if (skin::checkbox("##p2-keyboard", &on)) {
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
    opt::row(N_("Battle music"));
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
    bool open = skin::beginCombo("##arenamusic", BM.label.c_str());
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
    {38, N_("Grahf's arena"), N_("The stage from Grahf's on-foot battle.")},
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
    opt::row(N_("Battle stage"));
    int value = pcArenaStageSetting(); // always one of the list's (Blackmoon Forest by default)
    char label[160];
    const StageName* shown = stageName(value - 1);
    if (shown == nullptr) snprintf(label, sizeof label, tr("Stage %d"), value - 1);
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

const char* const memberNames[11] = {N_("Fei"),  N_("Elly"),     N_("Citan"),   N_("Bart"),  N_("Billy"),
                                     N_("Rico"), N_("Emeralda"), N_("Chu-Chu"), N_("Maria"), N_("Citan (sword)"),
                                     N_("Emeralda (adult)")};

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

char rosterCode[ARENA_LOADOUT_CODE] = "";

void allGearsOption() {
    opt::row(N_("Every Gear"));
    bool all = pcArenaAllGearsSetting() != 0;
    if (skin::checkbox("##all-gears", &all)) {
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
    XgPartyRecord record;
    bool partyOk = arenaSavesPanel(1, &record);
    const char* why = nullptr;
    bool available = pcArenaMatchAvailable(&why) != 0;
    opt::plain();
    markBlock();
    ImGui::BeginDisabled(!available || !partyOk);
    if (ImGui::Button(tr("Fight##mirror"))) {
        if (pcArenaMatchStartSaved(ARENA_MATCH_MIRROR, 0, 0, &record)) {
            needsArenaPicture = true;
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
        opt::plain();
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
    opt::plain();
    ImGui::Dummy(ImVec2(0, 4 * skin::unit()));
    const char* why = nullptr;
    bool available = pcArenaMatchAvailable(&why) != 0;
    markBlock();
    ImGui::BeginDisabled(!available || !ok[0] || !ok[1]);
    if (ImGui::Button(tr("Fight##party"))) {
        if (pcArenaMatchStartParty(&record[0], &record[1], nullptr)) {
            needsArenaPicture = true;
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
    opt::row(N_("Opponent's HP and EP"));
    bool opponentHp = pcVersusOpponentHpSetting() != 0;
    ImGui::BeginDisabled(settingsPinned("arena_opponent_hp") != 0);
    if (skin::checkbox("##opponent-hp", &opponentHp)) {
        sfx::play(sfx::CHOOSE);
        pcVersusSetOpponentHp(opponentHp);
    }
    ImGui::EndDisabled();
    hint("In battle, Select shows the opponent's members with their HP and EP (the Enemy info window). Off, "
         "neither player sees the other's. Online, the host's choice holds for both players.");
}


struct RosterCatalog {
    std::string disc;
    std::vector<XgBeast> beasts, gears;
} rosterCatalog;

bool loadRosterCatalog(const char* path) {
    if (path == nullptr || xgBeastsRead == nullptr) return false;
    if (rosterCatalog.disc == path) return !rosterCatalog.beasts.empty();
    rosterCatalog = {};
    rosterCatalog.disc = path;
    XgDisc* disc = xgDiscOpen(path, nullptr);
    if (disc == nullptr) return false;
    XgBeast* read = nullptr;
    int count = xgBeastsRead(disc, &read, 0);
    for (int i = 0; i < count; i++) {
        if (read[i].visual == 1 && read[i].hp > 0) rosterCatalog.beasts.push_back(read[i]);
        if (read[i].visual == -1 && read[i].gear && read[i].hp > 0) rosterCatalog.gears.push_back(read[i]);
    }
    free(read);
    xgDiscClose(disc);
    return !rosterCatalog.beasts.empty();
}

const XgBeast* rosterEnemy(const std::vector<XgBeast>& catalog, ArenaEnemy id) {
    auto match = std::find_if(catalog.begin(), catalog.end(), [id](const XgBeast& beast) {
        return beast.set == id.bank && beast.type == id.type;
    });
    return match == catalog.end() ? nullptr : &*match;
}

bool showRosterReview(const char* code, const XgPartyRecord& record) {
    ArenaLoadout loadout = {};
    int party[3];
    if (!pcArenaLoadoutDecode(code, &loadout, party) || !loadRosterCatalog(cdSwapPath(0))) {
        ImGui::TextUnformatted(tr("This loadout cannot be reviewed with the current disc."));
        return false;
    }
    if (const StageName* stage = stageName(loadout.stage)) ImGui::Text("%s %s", tr("Stage"), tr(stage->name));
    else ImGui::Text(tr("Stage %d"), loadout.stage);
    if (loadout.side[0].count == 0) {
        ImGui::TextUnformatted(tr(loadout.startInGear ? "Start in Gear" : "Start on foot"));
        if (loadout.callGear) ImGui::TextUnformatted(tr("Enable Gear"));
        if (loadout.allowExitGear) ImGui::TextUnformatted(tr("Exit Gear allowed"));
    }
    bool valid = false;
    for (int i = 0; i < pcArenaStageCount(); i++) valid |= pcArenaStageId(i) == loadout.stage;
    for (int side = 0; side < 2; side++) {
        ImGui::SeparatorText(tr(side == 0 ? "Player 1" : "Player 2"));
        const ArenaEnemyTeam& team = loadout.side[side];
        if (team.count == 0) {
            ImGui::TextUnformatted(tr("Character party"));
            if (!record.m3_count) ImGui::TextUnformatted(tr("Player 1 chooses their own save."));
            XgPartyMember member;
            for (int i = 0; xgPartyMemberInfo(&record, i, &member); i++) {
                ImGui::BulletText(tr("%s (level %d)"), tr(memberNames[member.id]), member.level);
            }
            int count = 0, types = 0;
            for (int i = 0; i < XG_PARTY_ITEMS && record.m298_itemIds[i] != 0; i++) {
                types++;
                count += record.m2C8_itemCounts[i];
            }
            if (types) ImGui::Text(tr("Save items: %d types, %d total"), types, count);
            continue;
        }
        ImGui::TextWrapped(tr("HP %u%%  Physical attack %u%%  Ether %u%%"),
                    team.multiplier[0], team.multiplier[1], team.multiplier[2]);
        ImGui::TextWrapped(tr("Defense %u%%  Ether defense %u%%  Agility %u%%"),
                    team.multiplier[3], team.multiplier[4], team.multiplier[5]);
        for (int i = 0; i < team.count; i++) {
            const XgBeast* enemy = rosterEnemy(rosterCatalog.beasts, team.enemy[i]);
            if (enemy == nullptr) {
                ImGui::BulletText(tr("Unavailable enemy (%u.%u)"), team.enemy[i].bank, team.enemy[i].type);
                valid = false;
                continue;
            }
            ImGui::BulletText("%s", enemy->name[0] ? enemy->name : tr("Enemy"));
            ImGui::TextWrapped(tr("Base HP %d  ATK %d  ETH %d  DEF %d  EDEF %d  AGI %d"), enemy->hp, enemy->attack,
                        enemy->ether, enemy->defence, enemy->etherDefence, enemy->agility);
            if ((loadout.callGear || loadout.startInGear) && (team.gearSlots & (1 << i))) {
                const XgBeast* gear = rosterEnemy(rosterCatalog.gears, team.gear[i]);
                if (gear != nullptr) ImGui::Text(tr("Gear %s"), gear->name[0] ? gear->name : tr("Assigned"));
                else valid = false;
            }
        }
        int types = 0, count = 0;
        for (int quantity : team.items) {
            if (quantity) types++;
            count += quantity;
        }
        if (types) ImGui::Text(tr("Save items: %d types, %d total"), types, count);
    }
    return valid;
}

void confirmLocalRoster(ArenaLoadoutReview& review, bool canApprove, SDL_JoystickID* pad, bool* held,
                        const char*& error, bool& reviewing, void (*resumeGame)()) {
    for (int player = 0; player < 2; player++) {
        SDL_JoystickID device = padProfileDevice(player);
        if (device != pad[player]) {
            pcArenaLoadoutApprove(&review, player, 0);
            pad[player] = device;
            held[player] = true;
        }
        bool pressed = device != 0 && padGamepadHeld(device, 13); // Select in the controller profile
        if (pressed && !held[player] && canApprove)
            pcArenaLoadoutApprove(&review, player, !(review.ready & (1 << player)));
        held[player] = pressed;
    }
    const char* why = nullptr;
    bool available = pcArenaMatchAvailable(&why);
    markBlock();
    ImGui::SeparatorText(tr("Confirm loadout"));
    ImGui::TextUnformatted(tr("Select: Confirm"));
    hint("Each player presses Select on their controller.");
    ImGui::BeginDisabled(!canApprove);
    if (pad[0] == 0 && ImGui::Button(tr("Confirm as Player 1"))) pcArenaLoadoutApprove(&review, 0, 1);
    ImGui::EndDisabled();
    ImGui::Text("%s  ·  %s", tr(review.ready & 1 ? "Player 1 ready" : "Waiting for Player 1"),
                tr(review.ready & 2 ? "Player 2 ready" : "Waiting for Player 2"));
    ImGui::BeginDisabled(!available || !canApprove || !pcArenaLoadoutReady(&review));
    if (ImGui::Button(tr("Fight##custom"))) {
        if (pcArenaRosterStart(&review, &error)) {
            needsArenaPicture = true;
            pcArenaLoadoutChanged(&review);
            reviewing = false;
            resumeGame();
        }
    }
    ImGui::EndDisabled();
    if (error != nullptr) hint("%s", tr(error));
    availability(available, why);
    if (ImGui::Button(tr("Back to teams"))) {
        reviewing = false;
        pcArenaLoadoutChanged(&review);
    }
}

void rosterBlock(bool online, void (*resumeGame)()) {
    if (online) rosterCode[0] = 0;
    static ArenaLoadoutReview review{};
    static std::string discPath;
    static bool both = false;
    static bool setup = false;
    static bool reviewing = false;
    static char localDraft[ARENA_LOADOUT_CODE];
    static char filter[64] = "";
    static int selected[2] = {-1, -1};
    static int quantities[2] = {1, 1};
    static bool held[2] = {};
    static SDL_JoystickID pad[2] = {};
    static const char* error = nullptr;
    int session = netplaySessionState();
    if (session != NETPLAY_IDLE && session != NETPLAY_OVER) {
        pcArenaLoadoutChanged(&review);
        hint("End the online session to edit the loadout.");
        return;
    }
    const char* path = cdSwapPath(0);
    if (!loadRosterCatalog(path)) {
        hint("Enemy team selection requires a Xenogears disc.");
        return;
    }
    const auto& beasts = rosterCatalog.beasts;
    const auto& gears = rosterCatalog.gears;
    if (!setup || discPath != path) {
        setup = true;
        discPath = path;
        review = {};
        reviewing = false;
        selected[0] = selected[1] = -1;
        error = nullptr;
        for (auto& team : review.current.loadout.side) {
            for (auto& multiplier : team.multiplier) multiplier = 100;
        }
    }
    if (!online && reviewing) {
        opt::plain();
        markBlock();
        ImGui::SeparatorText(tr("Review enemy stats"));
        bool canConfirm = showRosterReview(localDraft, review.current.record);
        confirmLocalRoster(review, canConfirm, pad, held, error, reviewing, resumeGame);
        return;
    }
    bool changed = false;
    opt::row(N_("Enemy teams on both sides"));
    changed |= skin::checkbox("##both-enemy-teams", &both);
    hint("Give both players enemy teams, or use a character party for Player 1.");
    if (!both && review.current.loadout.side[0].count != 0) {
        review.current.loadout.side[0].count = 0;
        review.current.loadout.side[0].gearSlots = 0;
        memset(review.current.loadout.side[0].items, 0, ARENA_ROSTER_ITEMS);
    }
    static unsigned char startInGear = 0, callGear = 0, allowExitGear = 0;
    ArenaLoadout& loadout = review.current.loadout;
    auto gearOption = [&changed](const char* label, const char* description, const char* id, unsigned char& value) {
        opt::row(label);
        bool on = value != 0;
        if (skin::checkbox(id, &on)) {
            value = on;
            changed = true;
        }
        hint("%s", tr(description));
    };
    if (!both) {
        gearOption(N_("Start in Gear"), N_("Start compatible fighters in their assigned Gears."),
                   "##start-in-gear", startInGear);
        gearOption(N_("Enable Gear"), N_("Let both players call an assigned Gear during a fight."),
                   "##call-gear", callGear);
        gearOption(N_("Allow Exit Gear"), N_("Allow switching back to fighting on foot after entering a Gear."),
                   "##exit-gear", allowExitGear);
    }
    loadout.startInGear = both ? 0 : startInGear;
    loadout.callGear = both ? 0 : callGear;
    loadout.allowExitGear = both ? 0 : allowExitGear;
    bool usesAssignedGears = loadout.callGear || loadout.startInGear;
    stagePicker();
    unsigned char stage = pcArenaStageSetting() - 1;
    changed |= stage != review.current.loadout.stage;
    review.current.loadout.stage = stage;
    int party[3] = {0, 2, 255}; // hidden by the enemy-team battle when both sides use enemies
    XgPartyRecord record{};
    bool partyOk = both || online || arenaSavesPanel(1, &record);
    if (!both && record.m3_count) {
        for (int i = 0; i < 3; i++) party[i] = record.m4_members[i];
    }
    changed |= memcmp(review.current.party, party, sizeof(party)) != 0 ||
               memcmp(&review.current.record, &record, sizeof record) != 0;
    memcpy(review.current.party, party, sizeof(party));
    review.current.record = record;
    opt::row(N_("Filter by enemy"));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##custom-enemy-filter", filter, sizeof(filter));
    hint("Filter the enemy choices by name.");
    const char* stats[ARENA_ROSTER_STATS] = {
        N_("HP"), N_("Physical attack"), N_("Ether"), N_("Defense"), N_("Ether defense"), N_("Agility"),
    };
    for (int side = both ? 0 : 1; side < 2; side++) {
        opt::plain();
        ArenaEnemyTeam& team = review.current.loadout.side[side];
        int cap = side == 0 ? 3 : 5;
        ImGui::PushID(side);
        opt::group(side == 0 ? N_("Player 1 enemies") : N_("Player 2 enemies"));
        opt::row(N_("Enemy"));
        if (skin::beginCombo("##enemy", selected[side] < 0 ? tr("Choose an enemy") : beasts[selected[side]].name,
                             ImGuiComboFlags_HeightLarge)) {
            for (int i = 0; i < (int)beasts.size(); i++) {
                const auto& b = beasts[i];
                std::string name = b.name;
                std::string query = filter;
                auto lower = [](unsigned char ch) { return (char)tolower(ch); };
                std::transform(name.begin(), name.end(), name.begin(), lower);
                std::transform(query.begin(), query.end(), query.begin(), lower);
                if (name.find(query) == std::string::npos) continue;
                char label[110];
                snprintf(label, sizeof(label), "%s (%d.%d)##enemy%d",
                         b.name[0] ? b.name : tr("Enemy"), b.set, b.type, i);
                if (skin::comboItem(label, selected[side] == i)) selected[side] = i;
                if (selected[side] == i) ImGui::SetItemDefaultFocus();
            }
            skin::endCombo();
        }
        hint("Choose the enemy to add to this team.");
        opt::row(N_("Quantity"));
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##quantity", &quantities[side], 1, cap);
        hint("Number of copies to add. This team can have up to %d enemies.", cap);
        opt::plain();
        markBlock();
        ImGui::BeginDisabled(selected[side] < 0 || team.count + quantities[side] > cap);
        if (ImGui::Button(tr("Add"))) {
            const auto& b = beasts[selected[side]];
            for (int i = 0; i < quantities[side]; i++) {
                team.enemy[team.count++] = ArenaEnemy{(unsigned char)b.set, (unsigned char)b.type};
            }
            changed = true;
        }
        ImGui::EndDisabled();
        hint("Add the chosen enemy to this team.");
        for (int i = 0; i < team.count; i++) {
            ArenaEnemy id = team.enemy[i];
            auto found = std::find_if(beasts.begin(), beasts.end(), [id](const XgBeast& b) {
                return b.set == id.bank && b.type == id.type;
            });
            char name[110];
            snprintf(name, sizeof name, "%d. %s", i + 1, found != beasts.end() ? found->name : tr("Enemy"));
            opt::row(name);
            ImGui::PushID(i);
            bool remove = ImGui::SmallButton(tr("Remove"));
            ImGui::PopID();
            hint("Remove %s from this team.", name);
            if (remove) {
                for (int j = i + 1; j < team.count; j++) {
                    team.enemy[j - 1] = team.enemy[j];
                    team.gear[j - 1] = team.gear[j];
                }
                unsigned lower = (1u << i) - 1;
                team.gearSlots = (team.gearSlots & lower) | ((team.gearSlots >> 1) & ~lower);
                team.count--;
                if (team.count == 0) memset(team.items, 0, ARENA_ROSTER_ITEMS);
                i--;
                changed = true;
                continue;
            }
            if (usesAssignedGears) {
                opt::row(N_("Gear"));
                ArenaEnemy gear = team.gear[i];
                bool assigned = (team.gearSlots & (1 << i)) != 0;
                auto choice = std::find_if(gears.begin(), gears.end(), [gear](const XgBeast& b) {
                    return b.set == gear.bank && b.type == gear.type;
                });
                ImGui::PushID(i);
                if (skin::beginCombo("##gear", assigned && choice != gears.end() ? choice->name : tr("No Gear assigned"),
                                     ImGuiComboFlags_HeightLarge)) {
                    if (skin::comboItem(tr("No Gear assigned"), !assigned)) {
                        team.gearSlots &= ~(1 << i);
                        changed = true;
                    }
                    for (const auto& b : gears) {
                        char label[100];
                        snprintf(label, sizeof(label), "%s (%d.%d)",
                                 b.name[0] ? b.name : tr("Gear"), b.set, b.type);
                        bool current = assigned && b.set == gear.bank && b.type == gear.type;
                        if (skin::comboItem(label, current)) {
                            team.gear[i] = ArenaEnemy{(unsigned char)b.set, (unsigned char)b.type};
                            team.gearSlots |= 1 << i;
                            changed = true;
                        }
                        if (current) ImGui::SetItemDefaultFocus();
                    }
                    skin::endCombo();
                }
                ImGui::PopID();
                hint("Assign a Gear to %s.", name);
            }
        }
        opt::plain();
        markBlock();
        bool showStats = ImGui::CollapsingHeader(tr("Enemy stats"));
        hint("%s %u%%  %s %u%%  %s %u%%\n%s %u%%  %s %u%%  %s %u%%", tr(stats[0]), team.multiplier[0],
             tr(stats[1]), team.multiplier[1], tr(stats[2]), team.multiplier[2], tr(stats[3]), team.multiplier[3],
             tr(stats[4]), team.multiplier[4], tr(stats[5]), team.multiplier[5]);
        if (showStats) {
            for (int i = 0; i < ARENA_ROSTER_STATS; i++) {
                opt::row(stats[i]);
                int percent = team.multiplier[i];
                ImGui::SetNextItemWidth(-1);
                ImGui::PushID(i);
                bool adjusted = ImGui::SliderInt("##multiplier", &percent, 25, 400, "%d%%");
                ImGui::PopID();
                if (adjusted) {
                    team.multiplier[i] = percent;
                    changed = true;
                }
                hint("Scale this stat for every enemy on this team.");
            }
        }
        opt::plain();
        markBlock();
        bool showItems = ImGui::CollapsingHeader(tr("Use items from a save"));
        int itemTypes = 0;
        for (unsigned char count : team.items) itemTypes += count != 0;
        hint("Item types available in battle: %d", itemTypes);
        if (showItems && online && side == 0) {
            hint("Player 1 chooses their own items.");
        } else if (showItems) {
            static_assert(ARENA_ROSTER_ITEMS == XG_PARTY_ITEMS);
            const uint8_t* save = arenaItemSavePicker(side);
            opt::plain();
            ImGui::BeginDisabled(save == nullptr || team.count == 0);
            if (ImGui::Button(tr("Use this save's items"))) {
                changed |= xgPartyReadItems(save, team.items) != 0;
            }
            ImGui::EndDisabled();
            if (itemTypes != 0) {
                ImGui::SameLine();
                if (ImGui::Button(tr("Clear items"))) {
                    memset(team.items, 0, ARENA_ROSTER_ITEMS);
                    changed = true;
                }
            }
        }
        ImGui::PopID();
    }
    if (changed) {
        pcArenaLoadoutChanged(&review);
        error = nullptr;
    }
    bool canApprove = partyOk && review.current.loadout.side[1].count != 0 &&
                      (!both || review.current.loadout.side[0].count != 0);
    if (online) {
        rosterCode[0] = '\0';
        memset(loadout.side[0].items, 0, sizeof loadout.side[0].items);
        if (canApprove) pcArenaLoadoutEncode(&loadout, party, rosterCode);
        return;
    }
    opt::plain();
    markBlock();
    ImGui::BeginDisabled(!canApprove);
    if (ImGui::Button(tr("Review enemy stats"))) {
        reviewing = pcArenaLoadoutEncode(&review.current.loadout, party, localDraft) != 0;
        pcArenaLoadoutChanged(&review);
        held[0] = held[1] = true;
    }
    ImGui::EndDisabled();
}

const XgVersusFormation* formationPicker(FormationChoice& choice) {
    opt::row(N_("Enemy formation"));
    if (xgVersusScanBegin == nullptr) {
        ImGui::TextUnformatted(tr("Unavailable"));
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
        ImGui::TextUnformatted(tr("Unavailable"));
        hint("No battles found on the disc in the drive.");
        return nullptr;
    }
    const char* preview = choice.selected < 0 ? tr("Choose an enemy formation.") : formations.list[choice.selected].label;
    if (skin::beginCombo("##versuslist", preview, ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < (int)formations.list.size(); i++) {
            const XgVersusFormation& f = formations.list[i];
            if (!matches(f, choice.filter)) continue;
            char label[160];
            snprintf(label, sizeof label, "%s##f%d", f.label, i);
            if (skin::comboItem(label, choice.selected == i)) {
                if (choice.selected != i) sfx::play(sfx::CHOOSE);
                choice.selected = i;
            }
            if (choice.selected == i) ImGui::SetItemDefaultFocus();
        }
        skin::endCombo();
    }
    const XgVersusFormation* selected = choice.selected < 0 ? nullptr : &formations.list[choice.selected];
    if (selected) hint("%s. Battle table %d, config %d.", selected->label, selected->table, selected->config);
    else hint("Choose an enemy formation.");
    opt::row(N_("Filter by enemy"));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##versusfilter", choice.filter, sizeof choice.filter);
    hint("Filter the formations by enemy name.");
    return selected;
}

bool versusStatus(void (*resumeGame)()) {
    int kind = 0;
    int match = pcArenaMatchPhase(&kind);
    if (match != ARENA_PHASE_IDLE &&
        (kind == ARENA_MATCH_VERSUS || kind == ARENA_MATCH_MIRROR || kind == ARENA_MATCH_PARTY || kind == ARENA_MATCH_ROSTER)) {
        matchState(match, resumeGame);
        return true;
    }
    int table = 0, config = 0;
    int phase = pcVersusPhase(&table, &config);
    if (phase == ARENA_PHASE_PENDING) {
        ImGui::TextUnformatted(tr("Asked: the battle starts as soon as you are free to walk around in a field."));
        if (ImGui::Button(tr("Cancel##versus"))) {
            sfx::play(sfx::BACK);
            pcVersusCancel();
        }
        return true;
    }
    if (phase != ARENA_PHASE_IDLE) {
        ImGui::TextUnformatted(tr("In a versus battle."));
        return true;
    }
    return false;
}

void versusGearOptions() {
    opt::row(N_("Start in Gear"));
    bool startsInGear = pcVersusGearsSetting() != 0;
    if (skin::checkbox("##party-gears", &startsInGear)) {
        sfx::play(sfx::CHOOSE);
        pcVersusSetGears(startsInGear);
    }
    hint("Start compatible fighters in their assigned Gears.");

    opt::row(N_("Enable Gear"));
    bool call = pcVersusCallGearSetting() != 0;
    if (skin::checkbox("##call-gear", &call)) {
        sfx::play(sfx::CHOOSE);
        pcVersusSetCallGear(call);
    }
    hint("Let the party call its Gears in formations that support them.");

    opt::row(N_("Allow Exit Gear"));
    bool exit = pcVersusExitGearSetting() != 0;
    if (skin::checkbox("##exit-gear", &exit)) {
        sfx::play(sfx::CHOOSE);
        pcVersusSetExitGear(exit);
    }
    hint("Allow switching back to fighting on foot after entering a Gear.");
}

void versusFormation(void (*resumeGame)()) {
    musicPicker();
    opponentHpOption();
    const XgVersusFormation* formation = formationPicker(formations.local);
    versusGearOptions();
    XgPartyRecord record;
    bool partyOk = arenaSavesPanel(1, &record);
    const char* why = nullptr;
    bool available = pcArenaMatchAvailable(&why) != 0;
    opt::plain();
    markBlock();
    ImGui::BeginDisabled(!available || formation == nullptr || !partyOk);
    if (ImGui::Button(tr("Fight"))) {
        if (pcArenaMatchStartSaved(ARENA_MATCH_VERSUS, formation->table, formation->config, &record)) {
            needsArenaPicture = true;
            sfx::play(sfx::CHOOSE);
            resumeGame();
        } else {
            sfx::play(sfx::BUZZ);
        }
    }
    ImGui::EndDisabled();
    availability(available, why);
}

bool customEnemyTeams(bool& custom) {
    opt::row(N_("Enemy team"));
    if (skin::beginCombo("##enemy-team", tr(custom ? "Custom teams" : "Preset formation"))) {
        if (skin::comboItem(tr("Preset formation"), !custom)) {
            custom = false;
            sfx::play(sfx::CHOOSE);
        }
        if (skin::comboItem(tr("Custom teams"), custom)) {
            custom = true;
            sfx::play(sfx::CHOOSE);
        }
        skin::endCombo();
    }
    hint("Choose an existing enemy formation or build custom teams.");
    return custom;
}

void versusBlock(void (*resumeGame)()) {
    if (versusStatus(resumeGame)) return;
    if (opt::beginSubmenu("formation", N_("Enemy battle"), nullptr,
                          N_("Fight preset enemy formations or build custom teams for two players."))) {
        static bool custom = false;
        if (customEnemyTeams(custom)) {
            musicPicker();
            rosterBlock(false, resumeGame);
        } else {
            versusFormation(resumeGame);
        }
        opt::endSubmenu();
    }
    if (opt::beginSubmenu("mirror", N_("Mirror match"), nullptr,
                          N_("Player 2 commands a copy of your party, with its own command ring and copy of your items."))) {
        musicPicker();
        opponentHpOption();
        stagePicker();
        versusGearOptions();
        mirrorBlock(resumeGame);
        opt::endSubmenu();
    }
    if (opt::beginSubmenu("local-party", N_("Party vs party"), nullptr,
                          N_("Each player controls a party from a memory-card save."))) {
        musicPicker();
        opponentHpOption();
        stagePicker();
        versusGearOptions();
        partyBlock(resumeGame);
        opt::endSubmenu();
    }

}

enum OnlineMatch { OnlineBattling, OnlineVersus, OnlineParty, OnlineCustom };

struct {
    bool loaded = false;
    bool host = true;
    char code[64] = "";
    char address[256] = "";
    int port = 7461;
    int delay = 3;
    bool joinerPicks = true;
    bool custom = false;     // the active session's mode
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
        {"startingtimeout", N_("The other player's game did not finish connecting.")},
        {"silent", N_("The other player stopped answering.")},
        {"quit", N_("The session's window was closed.")},
        {"nobody", N_("Nobody joined in time.")},
        {"noanswer", N_("No answer from the host: check the address, the port, the host's firewall and the code.")},
        {"build", N_("The host runs another version of the game: both players need the same one.")},
        {"region", N_("The host plays the other region's game (US/JP).")},
        {"busy", N_("The host is busy.")},
        {"pick", N_("Select Versus battle and choose an enemy formation, then join again.")},
        {"mode", N_("The host is playing a different match type. Ask them to host Gear Battling.")},
        {"disc", N_("The other player's disc is not the same as yours.")},
        {"loadout", N_("Both players need to approve the same valid custom loadout.")},
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


NetplayStart draftedRoster = {};
bool reviewingDraft;
bool rosterLobbyError;

bool reviewOnlineRoster() {
    NetplayStart agreed = {};
    char proposal[ARENA_LOADOUT_CODE];
    char detail[200];
    int state = netplayRosterLobbyState(&agreed, proposal, detail, sizeof detail);
    if (state == NETPLAY_ROSTER_IDLE && !reviewingDraft && !rosterLobbyError) return false;

    markBlock();
    if (state == NETPLAY_ROSTER_REVIEW || reviewingDraft) {
        ImGui::SeparatorText(tr("Review enemy stats"));
        const char* roster = reviewingDraft ? draftedRoster.roster : proposal;
        bool canConfirm = showRosterReview(roster, reviewingDraft ? draftedRoster.record : agreed.record);
        ImGui::BeginDisabled(!canConfirm);
        const char* confirm = reviewingDraft ? tr("Propose loadout") : tr("Confirm loadout");
        if (ImGui::Button(confirm)) {
            if (reviewingDraft) {
                reviewingDraft = false;
                rosterLobbyError = !netplayRosterLobbyStart(&draftedRoster);
            } else {
                netplayRosterLobbyConfirm(proposal, &agreed.record);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
    } else if (state == NETPLAY_ROSTER_READY) {
        // Keep the agreed stats on the last rendered frame for the shatter.
        ImGui::SeparatorText(tr("Loadout confirmed"));
        showRosterReview(agreed.roster, agreed.record);
        nextOnlineMatch = agreed;
        needsArenaPicture = launchAfterArenaPicture = true;
        netplayRosterLobbyStop();
        return true;
    } else if (state == NETPLAY_ROSTER_FAILED || rosterLobbyError) {
        ImGui::TextWrapped("%s", rosterLobbyError ? tr("The lobby could not start.") : tr(detail));
    } else {
        const char* waiting = proposal[0] ? "Loadout confirmed. Starting the match."
                                          : "Waiting for Player 1 to confirm the loadout.";
        if (agreed.host) waiting = proposal[0] ? "Waiting for Player 2 to confirm the loadout."
                                                   : "Waiting for Player 2 to propose a loadout.";
        ImGui::TextUnformatted(tr(waiting));
        if (!agreed.host && proposal[0]) showRosterReview(agreed.roster, agreed.record);
    }
    if (ImGui::Button(tr(reviewingDraft ? "Back to teams" : "Close lobby"))) {
        reviewingDraft = rosterLobbyError = false;
        netplayRosterLobbyStop();
    }
    return true;
}

bool onlineStatus() {
    if (netplayActive()) {
        markBlock();
        ImGui::TextUnformatted(tr("Online match in progress."));
        hint("At the end of a match, choose Rematch or Leave. EXIT in Battling closes the session.");
        return true;
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
        case NETPLAY_LOADING:
            snprintf(line, sizeof line, "%s", tr("Starting the match. Waiting for both games to finish loading."));
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
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textWarning);
            if (O.custom) {
                ImGui::TextWrapped("%s", tr("The joining player approved a different loadout. Waiting for a match."));
            } else {
                char why[320];
                arenaPartyReason(refusedReason, refusedMember, why, sizeof why);
                ImGui::TextWrapped(tr("A player joined with a party that cannot be used, and was refused (they "
                                      "see why): %s Waiting for another."), why);
            }
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
        return true;
    }
    return reviewOnlineRoster();
}

// A session runs in its own process. Only the chosen match's controls are shown here.
void onlineBlock(OnlineMatch mode) {
    if (!O.loaded) {
        O.loaded = true;
        O.port = settingsInt("netplay_port", 7461);
        O.delay = settingsInt("netplay_delay", 3);
        const char* a = settingsGet("netplay_address");
        if (a != nullptr) snprintf(O.address, sizeof O.address, "%s", a);
    }
    int state = netplaySessionState();
    if (state == NETPLAY_OVER) {
        opt::plain();
        markBlock();
        char why[400], line[512];
        endText(netplaySessionDetail(), why, sizeof why);
        snprintf(line, sizeof line, tr("The last session: %s"), why);
        textLines(line);
    }
    opt::row(N_("Connection"));
    if (skin::beginCombo("##netplay-role", tr(O.host ? "Host (player 1)" : "Join (player 2)"))) {
        if (skin::comboItem(tr("Host (player 1)"), O.host)) O.host = true;
        if (skin::comboItem(tr("Join (player 2)"), !O.host)) O.host = false;
        skin::endCombo();
    }
    hint("Use the same game version, disc and session code. A session opens in its own window.");
    if (mode == OnlineBattling) hint("After connecting, choose BONUS BATTLING, then PLAYER1 VS PLAYER2.");
    opt::row(N_("Session code"));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##netplaycode", O.code, sizeof O.code, ImGuiInputTextFlags_Password);
    hint("The same word for both players. It keeps others out of the session; it does not encrypt it.");
    if (O.host) {
        opt::row(N_("UDP port"));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputInt("##netplay-port", &O.port)) O.port = std::clamp(O.port, 1024, 65535);
        hint("Use a reachable UDP port. Play on the same network, over a VPN, or forward the port on the host's router.");
        opt::row(N_("Input delay"));
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##netplay-delay", &O.delay, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp);
        hint("Vblanks between a press and what it does, the same on both screens: 2 on a home network, 3 to 6 over "
             "the internet.");
    } else {
        opt::row(N_("The host's address:port"));
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##netplayaddress", O.address, sizeof O.address);
        hint("A name or an IPv4 address, then the port (192.168.1.20:7461); an IPv6 address goes in brackets "
             "([2001:db8::20]:7461).");
    }
    const bool custom = mode == OnlineCustom;
    const bool bringing = mode == OnlineParty || (mode == OnlineVersus && O.host);
    bool partyOk = false;
    const uint8_t* hostItemSave = nullptr;
    const XgVersusFormation* formation = nullptr;
    switch (mode) {
    case OnlineBattling:
        if (O.host) allGearsOption();
        break;
    case OnlineVersus:
        if (O.host) {
            opt::row(N_("Player 2 chooses the formation"));
            skin::checkbox("##joiner-picks", &O.joinerPicks);
            hint("On, Player 2 chooses the enemy formation when joining. Off, the host chooses it here.");
        }
        if (!O.host || !O.joinerPicks) formation = formationPicker(formations.online);
        if (O.host) {
            opponentHpOption();
            versusGearOptions();
            partyOk = arenaSavesPanel(0, &O.record);
        }
        break;
    case OnlineParty:
        if (O.host) {
            stagePicker();
            opponentHpOption();
            versusGearOptions();
        }
        partyOk = arenaSavesPanel(0, &O.record);
        hint("Both players bring a party from their own save. Edited saves are accepted within the game's ranges.");
        break;
    case OnlineCustom:
        if (O.host) {
            partyOk = arenaSavesPanel(0, &O.record);
            hint("Choose your save if you want to play a character party.");
            opt::plain();
            markBlock();
            hostItemSave = arenaHostItemSavePicker(ImGui::CollapsingHeader(tr("Use items from a save")));
            hint("Optional inventory for your team in enemy vs enemy.");
        } else {
            rosterBlock(true, nullptr);
        }
        break;
    }
    if (O.host) musicPicker();
    bool ready = O.code[0] != '\0' && (!bringing || partyOk) && (!custom || O.host || rosterCode[0]) &&
                 (O.host ? mode != OnlineVersus || O.joinerPicks || formation : strchr(O.address, ':') != nullptr);
    const char* start = custom ? (O.host ? tr("Open lobby") : tr("Review enemy stats"))
                               : (O.host ? tr("Host##netplay") : tr("Join##netplay"));
    opt::plain();
    markBlock();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button(start)) {
        NetplayStart s = {};
        s.host = O.host;
        snprintf(s.code, sizeof s.code, "%s", O.code);
        s.port = O.port;
        snprintf(s.address, sizeof s.address, "%s", O.address);
        s.delay = O.delay;
        s.progress = pcArenaAllGearsSetting() ? 1000 : netplaySessionStory();
        s.versus = O.host && mode == OnlineVersus;
        s.partyVsParty = O.host && mode == OnlineParty;
        s.battlingOnly = !O.host && mode == OnlineBattling;
        s.hasParty = (bringing || (custom && O.host)) && partyOk;
        if (s.hasParty) s.record = O.record;
        s.joinerPicks = s.versus && O.joinerPicks;
        s.pick = !O.host && formation;
        if (custom && !O.host) memcpy(s.roster, rosterCode, sizeof(s.roster));
        if (hostItemSave != nullptr) xgPartyReadItems(hostItemSave, s.rosterHostItems);
        if (formation) {
            s.table = formation->table;
            s.config = formation->config;
        }
        if (s.versus) {
            for (int i = 0; i < 3; i++) s.party[i] = s.record.m4_members[i];
        }
        settingsDescribe("netplay_port", "Online play (F1 > Extras > Versus battle): the UDP port to host on (default 7461)");
        settingsDescribe("netplay_delay", "Online: the input delay to host with, in vblanks (1..8; default 3)");
        settingsDescribe("netplay_address", "Online: the last host joined, host:port");
        settingsSetInt("netplay_port", O.port);
        settingsSetInt("netplay_delay", O.delay);
        if (!O.host) settingsSet("netplay_address", O.address);
        settingsSave();
        O.custom = custom;
        if (custom && O.host) {
            rosterLobbyError = !netplayRosterLobbyStart(&s);
        } else if (custom) {
            draftedRoster = s;
            reviewingDraft = true;
        } else {
            nextOnlineMatch = s;
            needsArenaPicture = launchAfterArenaPicture = true;
        }
    }
    ImGui::EndDisabled();
    if (O.code[0] == '\0') hint("Enter the session code.");
    else if (!O.host && strchr(O.address, ':') == nullptr) hint("Enter the host's address and port.");
    else if (bringing && !partyOk) hint("Choose a party from a memory-card save.");
    else if (custom && !O.host && !rosterCode[0]) hint("Build a valid loadout to review it.");
    else if (!ready) hint("Choose an enemy formation or let Player 2 choose.");
    else if (custom && O.host) hint("Player 2 proposes the teams. Review their stats here before confirming.");
    else hint("Both players see each other's IP address. The host's gameplay settings apply to both players.");
}

void onlineMenu() {
    const struct {
        OnlineMatch mode;
        const char* id;
        const char* label;
        const char* description;
    } modes[] = {
        {OnlineBattling, "online-battling", N_("Battling"), N_("Two-player Gear Battling online.")},
        {OnlineVersus, "online-formation", N_("Enemy battle"), N_("Fight preset enemy formations or build custom teams for two players.")},
        {OnlineParty, "online-party", N_("Party vs party"), N_("Both players bring a party from their own save.")},
    };
    for (const auto& mode : modes) {
        if (opt::beginSubmenu(mode.id, mode.label, nullptr, mode.description)) {
            static bool custom = false;
            onlineBlock(mode.mode == OnlineVersus && customEnemyTeams(custom) ? OnlineCustom : mode.mode);
            opt::endSubmenu();
        }
    }
}

void versusMenu(void (*resumeGame)()) {
    if (opt::atRoot()) opt::group(N_("Local play"));
    versusBlock(resumeGame);
    if (opt::atRoot()) opt::group(N_("Online play"));
    onlineMenu();
}

void battlingBlock(void (*resumeGame)()) {
    players();
    allGearsOption();
    musicPicker();
    opt::plain();
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

void battleArena(void (*resumeGame)()) {
    if (onlineStatus()) return;
    static bool online = false;
    opt::row(N_("Play mode"));
    if (skin::beginCombo("##arena-play-mode", tr(online ? "Online play" : "Local play"))) {
        if (skin::comboItem(tr("Local play"), !online) && online) {
            online = false;
            sfx::play(sfx::CHOOSE);
        }
        if (skin::comboItem(tr("Online play"), online) && !online) {
            online = true;
            sfx::play(sfx::CHOOSE);
        }
        skin::endCombo();
    }
    hint(online ? "Two-player Gear Battling online." : "Two-player Gear Battling on this device.");
    if (online) onlineBlock(OnlineBattling);
    else battlingBlock(resumeGame);
}

} // namespace

void arenaFinishMenuFrame() {
    if (!needsArenaPicture) return;
    needsArenaPicture = false;
    pcArenaCaptureMenuPicture();
    if (launchAfterArenaPicture) {
        launchAfterArenaPicture = false;
        sfx::play(netplayLaunch(&nextOnlineMatch) ? sfx::CHOOSE : sfx::BUZZ);
    }
}

void captureBattleMenuOnRender() {
    needsArenaPicture = true;
}

void arenaSection(ArenaPart part, void (*resumeGame)()) {
    ImGui::PushID(static_cast<int>(part));
    opt::beginPage("arena", EXTRAS);
    switch (part) {
    case ArenaPart::Battling: battleArena(resumeGame); break;
    case ArenaPart::Versus:
        if (!onlineStatus()) versusMenu(resumeGame);
        break;
    }
    opt::endPage();
    ImGui::PopID();
}

void arenaFallback(void (*resumeGame)()) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(0, 0, 8, 255));
    if (onlineStatus()) return;
    opt::beginPage("arena-fallback", EXTRAS);
    if (opt::atRoot()) {
        opt::group(N_("Battle arena"));
        battleArena(resumeGame);
    }
    versusMenu(resumeGame);
    opt::endPage();
}

} // namespace menu
