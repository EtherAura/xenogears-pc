// F1 > Extras > Battle arena (port/src/system/arena.h; xg-htn.35.16): Battling's own
// two-player bouts from wherever the player walks, who plays on what, and
// where the session is; versus battles; and Online: a session with a player
// elsewhere (port/src/system/netplay_launch.c), in a window of its own.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "imgui.h"
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
int pcWideGetMode(void); // include/pc_wide.h: what the game shows (1: a field)
}

// The extras' formation list (none in a build without them).
extern "C" XgVersusScan* xgVersusScanBegin(void) __attribute__((weak));
extern "C" int xgVersusScanStep(XgVersusScan* s, int budget) __attribute__((weak));
extern "C" double xgVersusScanProgress(const XgVersusScan* s) __attribute__((weak));
extern "C" int xgVersusScanResult(XgVersusScan* s, XgVersusFormation** out) __attribute__((weak));
extern "C" void xgVersusScanFree(XgVersusScan* s) __attribute__((weak));

namespace menu {

namespace {

const int WIDE_FIELD = 1; // PC_WIDE_FIELD

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
    textLines(line);
    if (pcArenaKeyboardOffered()) {
        bool on = keyboard2;
        if (ImGui::Checkbox(tr("Player 2 on the keyboard"), &on)) {
            sfx::play(sfx::CHOOSE);
            pcArenaSetPlayer2Keyboard(on);
        }
        hint("Player 1 then plays on the gamepad.");
    }
}

struct {
    XgVersusScan* scan = nullptr;
    bool scanned = false;
    std::string disc;  // the image the list is of
    std::vector<XgVersusFormation> list;
    int selected = -1; // in list
    char filter[48] = "";
} V;

bool matches(const XgVersusFormation& f, const char* filter) {
    if (filter[0] == '\0') return true;
    std::string a = f.label, b = filter;
    for (auto& c : a) c = (char)tolower((unsigned char)c);
    for (auto& c : b) c = (char)tolower((unsigned char)c);
    return a.find(b) != std::string::npos;
}

// Versus battles: player 2 commands an enemy formation (include/pc_arena.h).
void versusBlock() {
    ImGui::Dummy(ImVec2(0, 6 * scale()));
    ImGui::SeparatorText(tr("Versus battle"));
    textLines("Player 2 commands an enemy formation in a normal battle: on each enemy's turn, pad 2 picks what "
              "that enemy can do now, then its target. Your party fights as it is. Afterwards everything is as it "
              "was: nothing won, nothing lost.");
    if (xgVersusScanBegin == nullptr) {
        hint("Not in this build (it reads the battles with the extras).");
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
    const char* path = cdSwapPath(0);
    if (path != nullptr && V.disc != path) { // another disc in the drive: its tables
        if (V.scan != nullptr) xgVersusScanFree(V.scan);
        V = {};
        V.disc = path;
    }
    if (!V.scanned) {
        if (V.scan == nullptr) V.scan = xgVersusScanBegin();
        if (V.scan == nullptr || xgVersusScanStep(V.scan, 12)) {
            if (V.scan != nullptr) {
                XgVersusFormation* l = nullptr;
                int n = xgVersusScanResult(V.scan, &l);
                V.list.assign(l, l + (n > 0 ? n : 0));
                free(l);
                xgVersusScanFree(V.scan);
                V.scan = nullptr;
            }
            V.scanned = true;
        } else {
            ImGui::ProgressBar((float)xgVersusScanProgress(V.scan), ImVec2(-1, 0), tr("Reading the battles"));
            return;
        }
    }
    if (V.list.empty()) {
        hint("No battles found on the disc in the drive.");
        return;
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##versusfilter", tr("Filter by enemy"), V.filter, sizeof V.filter);
    if (ImGui::BeginListBox("##versuslist", ImVec2(-1, 6 * ROW * scale()))) {
        for (int i = 0; i < (int)V.list.size(); i++) {
            const XgVersusFormation& f = V.list[i];
            if (!matches(f, V.filter)) continue;
            char label[160];
            snprintf(label, sizeof label, "%s##f%d", f.label, i);
            if (ImGui::Selectable(label, V.selected == i)) {
                if (V.selected != i) sfx::play(sfx::MOVE);
                V.selected = i;
            }
        }
        ImGui::EndListBox();
    }
    bool gears = pcVersusGearsSetting() != 0;
    if (ImGui::Checkbox(tr("Party in its Gears"), &gears)) {
        sfx::play(sfx::CHOOSE);
        pcVersusSetGears(gears);
    }
    hint("Gear against Gear: the members who have a Gear fight in it (pick a formation of Gears).");
    bool field = pcWideGetMode() == WIDE_FIELD && !netplayActive();
    bool chosen = V.selected >= 0 && V.selected < (int)V.list.size();
    ImGui::BeginDisabled(!field || !chosen);
    if (ImGui::Button(tr("Fight"))) {
        const XgVersusFormation& f = V.list[V.selected];
        if (pcVersusRequest(f.table, f.config)) {
            sfx::play(sfx::CHOOSE);
            closeMenu();
        } else {
            sfx::play(sfx::BUZZ);
        }
    }
    ImGui::EndDisabled();
    if (chosen) {
        hint("Battle table %d, config %d.", V.list[V.selected].table, V.list[V.selected].config);
    }
    if (netplayActive()) {
        hint("Not in an online session's window.");
    } else if (!field) {
        hint("From a field (a town, a dungeon...), not the world map, a battle or a menu.");
    }
}

struct {
    bool loaded = false;
    bool host = true;
    char code[64] = "";
    char address[256] = "";
    int port = 7461;
    int delay = 3;
    bool versus = false;
    bool joinerPicks = true;
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
        {"left", N_("The other player left.")},
        {"silent", N_("The other player stopped answering.")},
        {"quit", N_("The session's window was closed.")},
        {"nobody", N_("Nobody joined in time.")},
        {"noanswer", N_("No answer from the host: check the address, the port, the host's firewall and the code.")},
        {"build", N_("The host runs another version of the game: both players need the same one.")},
        {"region", N_("The host plays the other region's game (US/JP).")},
        {"busy", N_("The host is busy.")},
        {"pick", N_("The host lets you choose the formation: choose one in the Versus battle list above, then join "
                    "again.")},
        {"disc", N_("The other player's disc is not the same as yours.")},
        {"address", N_("The address is not host:port, or that host is unknown.")},
        {"error", N_("The network could not be used (is the port in use?).")},
    };
    for (const auto& e : ends) {
        if (strncmp(d, e.word, strlen(e.word)) == 0 && (d[strlen(e.word)] == '\0' || d[strlen(e.word)] == ' ')) {
            snprintf(out, size, "%s", tr(e.text));
            return;
        }
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
    ImGui::Dummy(ImVec2(0, 6 * scale()));
    ImGui::SeparatorText(tr("Online"));
    if (netplayActive()) {
        textLines("This window is an online session. It ends when Battling is left (EXIT), or when either player "
                  "closes their window.");
        return;
    }
    textLines("Battling's PLAYER1 VS PLAYER2, or a versus battle, with a player elsewhere: on your network, at an "
              "address you share, or over a VPN you both use. The session plays in a window of its own, from "
              "power-on; this game waits until it ends. Both players need the same version of the game (a Linux "
              "and a Windows build of one version play together) and the same disc.");
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
        if (netplaySessionPaused()) ImGui::TextUnformatted(tr("The other player paused."));
        if (netplaySessionLost()) {
            textLines("The connection is lost: the session waits for it to come back (up to a minute), then "
                      "goes on where it stopped.");
        }
        if (ImGui::Button(tr("End the session"))) {
            sfx::play(sfx::BACK);
            netplaySessionStop();
        }
        return;
    }
    if (state == NETPLAY_OVER) {
        char why[400];
        endText(netplaySessionDetail(), why, sizeof why);
        snprintf(line, sizeof line, tr("The last session: %s"), why);
        textLines(line);
    }
    if (ImGui::RadioButton(tr("Host (player 1)"), O.host)) O.host = true;
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Join (player 2)"), !O.host)) O.host = false;
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##netplaycode", tr("Session code"), O.code, sizeof O.code);
    hint("The same word for both players. It keeps others out of the session; it does not encrypt it.");
    if (O.host) {
        ImGui::SetNextItemWidth(12 * ROW * scale() / 2);
        if (ImGui::InputInt(tr("UDP port"), &O.port)) O.port = O.port < 1024 ? 1024 : O.port > 65535 ? 65535 : O.port;
        ImGui::SetNextItemWidth(12 * ROW * scale() / 2);
        ImGui::SliderInt(tr("Input delay"), &O.delay, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp);
        hint("Vblanks between a press and what it does, the same on both screens: 2 on a home network, 3 to 6 over "
             "the internet.");
        if (ImGui::RadioButton(tr("Battling"), !O.versus)) O.versus = false;
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("Versus battle##online"), O.versus)) O.versus = true;
        if (O.versus) {
            bool chosen = V.selected >= 0 && V.selected < (int)V.list.size();
            ImGui::Checkbox(tr("Player 2 chooses the formation"), &O.joinerPicks);
            if (O.joinerPicks) {
                hint("Each player picks their own side: player 2 the formation they command, you your party.");
            } else if (chosen) {
                snprintf(line, sizeof line, tr("Player 2 commands %s (the formation chosen above)."),
                         V.list[V.selected].label);
                textLines(line);
            } else {
                hint("Choose a formation in the Versus battle list above.");
            }
            hint("Your party as a new game makes them: their starting levels, equipment and Gears, never anyone's "
                 "save.");
        } else {
            hint("Your Gear list (as far as your story has reached, or Every Gear above) is both players'.");
        }
        hint("Your gameplay settings (difficulty, text speed...) are both players' for the session.");
    } else {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##netplayaddress", tr("The host's address:port"), O.address, sizeof O.address);
        if (V.selected >= 0 && V.selected < (int)V.list.size()) {
            snprintf(line, sizeof line, tr("For a versus battle where you choose: %s (from the Versus battle list "
                                           "above)."), V.list[V.selected].label);
            textLines(line);
        } else {
            hint("For a versus battle where you choose the formation, pick one in the Versus battle list above.");
        }
        hint("A name or an IPv4 address, then the port (192.168.1.20:7461); an IPv6 address goes in brackets "
             "([2001:db8::20]:7461).");
    }
    hint("The other player sees your IP address, as you see theirs: play with people you trust. Hosting needs the "
         "UDP port reachable: the same network, a VPN you both use (Tailscale, ZeroTier...), or the port forwarded "
         "on your router.");
    bool formation = V.selected >= 0 && V.selected < (int)V.list.size();
    bool ready = O.code[0] != '\0' &&
                 (O.host ? !O.versus || O.joinerPicks || formation : strchr(O.address, ':') != nullptr);
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button(O.host ? tr("Host##netplay") : tr("Join##netplay"))) {
        NetplayStart s = {};
        s.host = O.host;
        snprintf(s.code, sizeof s.code, "%s", O.code);
        s.port = O.port;
        snprintf(s.address, sizeof s.address, "%s", O.address);
        s.delay = O.delay;
        s.progress = pcArenaAllGearsSetting() ? 1000 : netplaySessionStory();
        s.versus = O.host && O.versus && (O.joinerPicks || formation);
        s.joinerPicks = O.joinerPicks;
        s.pick = !O.host && formation;
        if (formation) {
            s.table = V.list[V.selected].table;
            s.config = V.list[V.selected].config;
        }
        if (s.versus) netplaySessionParty(s.party);
        settingsDescribe("netplay_port", "Online (F1 > Extras > Battle arena): the UDP port to host on (default 7461)");
        settingsDescribe("netplay_delay", "Online: the input delay to host with, in vblanks (1..8; default 3)");
        settingsDescribe("netplay_address", "Online: the last host joined, host:port");
        settingsSetInt("netplay_port", O.port);
        settingsSetInt("netplay_delay", O.delay);
        if (!O.host) settingsSet("netplay_address", O.address);
        settingsSave();
        sfx::play(netplayLaunch(&s) ? sfx::CHOOSE : sfx::BUZZ);
    }
    ImGui::EndDisabled();
}

} // namespace

// The block at the end of the Extras section.
void arenaSection() {
    markBlock();
    ImGui::Dummy(ImVec2(0, 6 * scale()));
    ImGui::SeparatorText(tr("Battle arena"));
    textLines("Battling's two-player bouts from where you walk: BONUS BATTLING, PLAYER1 VS PLAYER2. EXIT in its "
              "menu brings you back; the save stays as it was.");
    players();
    bool all = pcArenaAllGearsSetting() != 0;
    if (ImGui::Checkbox(tr("Every Gear"), &all)) {
        sfx::play(sfx::CHOOSE);
        pcArenaSetAllGears(all);
    }
    hint("All 49 Gears to choose from, Argento too, not only those the story has reached.");
    switch (pcArenaPhase()) {
    case ARENA_PHASE_IDLE: {
        bool field = pcWideGetMode() == WIDE_FIELD && !netplayActive();
        ImGui::BeginDisabled(!field);
        if (ImGui::Button(tr("Go to the arena"))) {
            sfx::play(sfx::CHOOSE);
            pcArenaRequest();
            closeMenu();
        }
        ImGui::EndDisabled();
        if (netplayActive()) {
            hint("Not in an online session's window.");
        } else if (!field) {
            hint("From a field (a town, a dungeon...), not the world map, a battle or a menu.");
        } else {
            hint("It opens as soon as you are free to walk around.");
        }
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
    versusBlock();
    onlineBlock();
}

} // namespace menu
