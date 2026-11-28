/*
 * The Battle arena, the platform's side (arena.h, include/pc_arena.h;
 * xg-htn.35.16): the request the menu makes (F1 > Extras > Battle arena,
 * port/src/ui/arena.cpp), what it shows, and player 2's keyboard.
 *
 * Battling's PLAYER1 VS PLAYER2 reads pad 1 for gear 1 and pad 2 for gear 2.
 * Port 2 is the second gamepad (pad.c); with only one gamepad, the setting
 * arena_player2_keyboard (default 0) puts the keyboard on port 2 while
 * Battling runs (pad.c padSetKeyboardPort), so player 1 plays on the gamepad
 * and player 2 on the keyboard. padSetKeyboardPort is weak: a pad.c without
 * it still builds, port 2 then only from a second gamepad.
 *
 * The setting arena_all_gears (default 0) offers every Gear in the arena's
 * Gear select (buildBattlingGearList), not only those the story has reached.
 *
 * XG_ARENA=1: asked from the start (headless runs): the arena starts at the
 * first moment the player walks free in a field.
 *
 * Versus battles (include/pc_arena.h): the menu asks for a formation, a
 * battle table and a config (port/src/extras/versus_list.c lists them from
 * the disc); XG_VERSUS=<table>,<config> asks from the start (headless).
 *
 * Matches from anywhere (xg-htn.35.144): the menu's Go to the arena and
 * Fight work wherever the game is (the title, a menu, a battle, the world
 * map, a field). At the next vblank the game is kept in memory, a whole
 * state as a save takes it (savestate.c stateRequestRestart, never on disk),
 * and starts again by a soft reset into the match's canonical start, the one
 * an online session boots into (netplay.c):
 *   - Battling: kernel mode 4, Battling's main menu (BONUS BATTLING >
 *     PLAYER1 VS PLAYER2), with the kept game's story progress (field
 *     variable 0: the Gear list) and Battling's records and bonus settings
 *     (gameState + 0x2344); Every Gear as in a field's session;
 *   - a versus battle: field 22 at the story's start (field variable 0 = 0),
 *     the party chosen, the formation asked for at once, as XG_VERSUS asks:
 *     the battle starts when the field first lets the party walk (on disc 1
 *     when its image is at hand). The party: the current one, Fei and Citan
 *     as a new game makes them, or members picked; the kept game's members
 *     come as its save has them (their records, abilities, names and Gears),
 *     HP, EP and Gear HP and fuel full; a new game's otherwise, and always
 *     before a game was loaded (the title).
 * Each soft reset while the match runs (a Rematch, the player's own
 * Start+Select+L1+R1) starts it again the same way; for a rematch Battling
 * goes straight into the same two Gears' PLAYER1 VS PLAYER2 (its entry bytes,
 * port/src/game/battling/arena.c). Leave at the match's end (the game's side:
 * port/src/game/main/arena.c), EXIT in Battling's menu, a versus battle
 * that ends without the prompt, or Leave the match in the menu: the kept
 * game is put back at the next vblank, exactly as it was. Save states (and
 * rewind) are off meanwhile (savestate.c stateAvailable). For online matches
 * netplay.c owns the kept game and the negotiated start.
 * XG_ARENA_MATCH=<vblank>:battling | <vblank>:versus,<table>,<config>[,current|new|a.b.c]
 * | <vblank>:mirror[,current|new|a.b.c] | <vblank>:party,<save>,<save> asks for
 * a match as the menu does, at that vblank (headless runs). A party vs party
 * <save> is <card>.<number>[.<id>[.<id>[.<id>]]]: card 1 or 2, the save's
 * number (No.12 in its title), then the members' character ids in order (the
 * save's own party without them), player 1's first: party,2.12.0.2.4,1.2
 * is card 2's No.12 with Fei, Citan and Billy against card 1's No.02 as it
 * was saved. A mirror match or party vs party takes ,arena=<n> at the end:
 * stage n (the game's number, 0..74, one of the stage list's below), else
 * the setting arena_stage's: 800:mirror,0.2.4,arena=23 and
 * 800:party,2.12.0.2.4,1.2,arena=23 are fought on stage 23 (the Zeboim
 * ruins); a stage not in the list is refused (the match not asked).
 *
 * Mirror matches (xg-htn.35.143; the battle's side: port/src/game/battle/
 * mirror.c): a versus battle where player 2 commands a copy of player 1's
 * party, the mirror's formation (include/pc_arena.h VERSUS_MIRROR_TABLE and
 * CONFIG) on the stage the players chose (the stage list below, Blackmoon
 * Forest by default: pcVersusStage);
 * from the menu as a match (ARENA_MATCH_MIRROR), XG_VERSUS=mirror from the
 * start. Local only for now: online sessions offer none.
 *
 * Party vs party (xg-htn.35.143, ARENA_MATCH_PARTY): a mirror match whose
 * two sides each come from a party record (port/src/card/party.h: a memory
 * card save's members, packed and checked), player 1's in slots 0-2 and
 * player 2's in 3-5, on foot. The two records are the match's only input
 * from the saves (online they will be what each machine sends): player 1's
 * is applied here at each canonical start (partyFromRecord: its members'
 * records, abilities and names, HP and EP full, no Gear, its items and
 * rounds; nothing else of the save), player 2's by the battle
 * (port/src/game/battle/mirror.c, pcVersusPartyRecord). A rematch applies
 * both again from the same bytes: the same parties, fresh.
 *
 * Online party vs party (xg-htn.35.143, step 2.4; netplay.c): the session's
 * process is handed both records once its handshake is over
 * (pcArenaSessionParties: the host's as player 1, the joiner's as player 2,
 * each checked by both machines), and each of its boots starts the match
 * the way a local one starts (pcArenaStartState: the same code, player 1's
 * record applied the same way), player 2's taken by the battle as above. A
 * rematch (the session's soft reset) applies them again; nothing is sent
 * again.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "common.h"
#include "psyq.h"
#include "main/game_state.h"
#include "arena.h"
#include "arena_roster.h"
#include "card/party.h"
#include "card/saves.h"
#include "cd/cd.h"
#include "gauntlet.h"
#include "hw.h"
#include "gpu/gpu.h"
#include "pc_arena.h"
#include "pc_wide.h"
#include "savestate.h"
#include "settings.h"
#include "stateimage.h"

_Static_assert((int)ARENA_PHASE_IDLE == (int)PC_ARENA_IDLE && (int)ARENA_PHASE_STARTING == (int)PC_ARENA_STARTING &&
                   (int)ARENA_PHASE_LEAVING == (int)PC_ARENA_LEAVING && (int)ARENA_PHASE_IN == (int)PC_ARENA_IN,
               "the menu's phases are the session's");
_Static_assert((int)ARENA_PHASE_LEAVING == (int)PC_VERSUS_LEAVING && (int)ARENA_PHASE_IN == (int)PC_VERSUS_IN,
               "the menu's phases are the versus session's");

void padSetKeyboardPort(int port) __attribute__((weak));

/* Party vs party reads the on-foot part of the record (pc_arena.h). */
_Static_assert(offsetof(XgPartyRecord, m3B8_gearEquip) == PC_PARTY_BYTES &&
                   offsetof(XgPartyRecord, m3_count) == PC_PARTY_COUNT &&
                   offsetof(XgPartyRecord, m4_members) == PC_PARTY_MEMBERS &&
                   offsetof(XgPartyRecord, m10_characters) == PC_PARTY_CHARACTERS &&
                   offsetof(XgPartyRecord, m1FC_abilities) == PC_PARTY_ABILITIES &&
                   offsetof(XgPartyRecord, m25C_names) == PC_PARTY_NAMES &&
                   offsetof(XgPartyRecord, m298_itemIds) == PC_PARTY_ITEM_IDS &&
                   offsetof(XgPartyRecord, m2C8_itemCounts) == PC_PARTY_ITEM_COUNTS &&
                   offsetof(XgPartyRecord, m2F8_ammoIds) == PC_PARTY_AMMO_IDS &&
                   offsetof(XgPartyRecord, m328_ammoCounts) == PC_PARTY_AMMO_COUNTS &&
                   offsetof(XgPartyRecord, m358_ammoTable) == PC_PARTY_AMMO_TABLE,
               "the party record's layout as pc_arena.h gives it to the game");
_Static_assert(sizeof(GameStateCharacter) == XG_PARTY_RECORD && sizeof(GameStateSize20) == XG_PARTY_ABILITY &&
                   sizeof(gameState.m0_names[0]) == XG_PARTY_NAME &&
                   sizeof(gameState.m22B8_ammoCounts) == XG_PARTY_AMMO_TABLE,
               "the party record holds the game's own records");

static int pending;
static int envRead;
static int keyboardOn; /* the keyboard is on port 2 now */

static int versusPending;
int pcVersusSessionBusy(void);

int netplayRefuses(const char* what) __attribute__((weak)); /* system/netplay.c */

int pcArenaRequest(void) {
    if (netplayRefuses != NULL && netplayRefuses("the arena")) return 0; /* the session's own game only */
    if (pcArenaSession() != PC_ARENA_IDLE || pcVersusSessionBusy()) {
        return 0;
    }
    pending = 1;
    return 1;
}

void pcArenaCancel(void) {
    pending = 0;
}

static unsigned char directParams[6]; /* XG_ARENA=2,GEAR1,GEAR2[,WINS[,COM]]: a match without the menus */
static int directAsked;

/* main: Battling's entry bytes, battlingEntryParams[0..5] and the result [6]
 * on the PS1; on the PC each an object of its own (pc_link: separate names),
 * so each is written by its name (port/src/game/battling/arena.c too) */
extern u8 battlingEntryParams, battlingEntryMatchType, battlingEntryGear1, battlingEntryGear2;
extern u8 battlingEntryWinsNeeded, battlingEntryComLevel;

int pcArenaPending(void) {
    if (!envRead) {
        const char* s = getenv("XG_ARENA");
        envRead = 1;
        if (s != NULL && atoi(s) != 0) {
            pending = 1;
            if (atoi(s) == 2) {
                /* the entry block Battling reads: mode 2 (an event fight, the Gear
                 * select skipped), match type, the two Gears, wins needed, COM level
                 * (headless oracle runs, work/battling60) */
                int v[4] = {0, 1, 2, 3};
                const char* p = s;
                int n = 0;
                while (n < 4 && (p = strchr(p, ',')) != NULL) {
                    v[n++] = atoi(++p);
                }
                directParams[0] = 2;
                directParams[1] = 0;
                directParams[2] = (unsigned char)v[0];
                directParams[3] = (unsigned char)v[1];
                directParams[4] = (unsigned char)v[2];
                directParams[5] = (unsigned char)v[3];
                directAsked = 1;
            }
        }
    }
    return pending;
}

/* The field's session (arena_field.c), in place of the main menu's entry. */
int pcArenaDirectEntry(void) {
    if (!directAsked) {
        return 0;
    }
    battlingEntryParams = directParams[0];
    battlingEntryMatchType = directParams[1];
    battlingEntryGear1 = directParams[2];
    battlingEntryGear2 = directParams[3];
    battlingEntryWinsNeeded = directParams[4];
    battlingEntryComLevel = directParams[5];
    directAsked = 0;
    return 1;
}

void pcArenaTaken(void) {
    pending = 0;
}

int pcArenaPhase(void) {
    int session = pcArenaSession();
    return session != PC_ARENA_IDLE ? session : pending ? ARENA_PHASE_PENDING : ARENA_PHASE_IDLE;
}

int pcArenaKeyboardOffered(void) {
    return padSetKeyboardPort != NULL;
}

int pcArenaPlayer2Keyboard(void) {
    settingsDescribe("arena_player2_keyboard", "Battle arena: player 2 plays on the keyboard, player 1 on the first "
                                               "gamepad, while Battling runs (0/1)");
    return settingsBool("arena_player2_keyboard", 0);
}

void pcArenaSetPlayer2Keyboard(int on) {
    settingsSetInt("arena_player2_keyboard", on != 0);
    settingsSave();
    if (pcArenaSession() == PC_ARENA_IN) {
        pcArenaKeyboard(1);
    }
}

int pcArenaAllGearsSetting(void) {
    settingsDescribe("arena_all_gears", "Battle arena: every Gear to choose from, whatever the story has reached (0/1)");
    return settingsBool("arena_all_gears", 0);
}

void pcArenaSetAllGears(int on) {
    settingsSetInt("arena_all_gears", on != 0);
    settingsSave();
}

/* ---- the battle music (xg-htn.35.146) ----
 *
 * The players choose the song of arena battles from the jukebox's list
 * (port/src/ui/arena.cpp); the game starts it where it starts the battle's
 * music: a versus battle's as the field starts a random battle's (the
 * field's own load, arena_field.c), Battling's as the field loads it before
 * op 0xBF (a field's session) or, in a match (started from the kernel, no
 * field), as the field would (port/src/game/battling/arena.c). Online the
 * setting is one of those that change the game: the host's, sent in the
 * handshake and held on both sides (netplay.c), so both games load the same
 * song at the same frame. */

int pcArenaMusicSetting(void) {
    settingsDescribe("arena_music", "Battle arena: the battle music, 0 the game's own, n the game's music ID n - 1 "
                                    "(F1 > Extras > Battle arena lists them by title)");
    int v = settingsInt("arena_music", 0);
    return v >= 0 && v <= ARENA_MUSIC_IDS ? v : 0;
}

void pcArenaSetMusic(int value) {
    settingsSetInt("arena_music", value >= 0 && value <= ARENA_MUSIC_IDS ? value : 0);
    settingsSave();
}

int pcArenaMusic(int def) {
    int v = pcArenaMusicSetting();
    int id = v > 0 ? v - 1 : def;
    pcArenaLog("battle music: %d%s", id, v > 0 ? " (chosen)" : " (the game's)");
    return id;
}

/* The field's data, as the disc has it (constant: the field never writes it). */
extern unsigned char musicLookupTable[];

int pcArenaMusicSelector(int id, int* wave, int* flags) {
    if (id < 0 || id >= ARENA_MUSIC_IDS) return 0;
    *wave = musicLookupTable[2 * id];
    *flags = musicLookupTable[2 * id + 1];
    return 1;
}

/* ---- the stage of a mirror match or party vs party (xg-htn.35.143) ----
 *
 * The players choose the battle's stage (its arena: the config's +2,
 * m2_arenaId; directory 0x0F files 2n + 6 and 2n + 7, the model bundle and
 * the stage data with its slot positions and cameras) from this list; the
 * formation stays the mirror's (its enemy set loads as before). Each one is
 * checked by a run (work/agents/BD/PVP/NOTES.md, the stage section): player
 * 2's sprites (their VRAM column, x 0x180, y 0x1C0: no upload of the stage's
 * there), all six places 3 v 3 and 1 v 3, the camera. The slot bytes are a
 * real battle's of that stage (the field's or world map's table the game
 * reads; named beside each): the party's position slots (+4..+6) and player
 * 2's places, as the first three enemies (+0x18..+0x1A), so each side
 * stands where the game puts a party and its enemies there. The menu names
 * them (port/src/ui/arena.cpp). Online (step 2.4) the host's choice will be
 * one of the session's terms, as arena_music: the setting below, which a
 * session pins. */
static const struct {
    unsigned char id;        /* the stage */
    unsigned char party[3];  /* the config's +4..+6 */
    unsigned char places[3]; /* +0x18..+0x1A */
} stages[] = {
    {3, {0, 0, 0}, {0, 0, 0}},  /* Mountain Path: field 15 config 7 (random; places of its 2nd-4th enemies) */
    {1, {0, 0, 0}, {0, 0, 0}},  /* Blackmoon Forest: field 22 config 3 (random) */
    {7, {0, 0, 0}, {0, 0, 0}},  /* Duneman Isle: field 47 config 0 (random) */
    {14, {0, 0, 0}, {0, 0, 0}}, /* Nisan: field 474 config 0 (the town gate's battle) */
    {13, {0, 0, 0}, {0, 0, 0}}, /* Aveh Castle: field 147 config 8 (random) */
    {9, {1, 1, 1}, {1, 1, 1}},  /* Nortune sewers: field 239 config 1 (random) */
    {10, {0, 0, 0}, {0, 0, 0}}, /* Nortune Gear dock: field 256 config 1 (random) */
    {50, {0, 0, 0}, {0, 0, 0}}, /* Ethos headquarters: field 339 config 0 (random) */
    {71, {0, 0, 0}, {0, 0, 0}}, /* Ethos transport, on deck: field 333 config 0 (random) */
    {23, {0, 0, 0}, {0, 0, 0}}, /* Zeboim: field 363 config 7 (the dig site's; the ruins' slot 2 cuts player 1 off) */
    {30, {0, 0, 0}, {1, 1, 1}}, /* Lighthouse: field 403 config 0 (random) */
    {35, {0, 0, 0}, {0, 0, 0}}, /* Shevat: field 449 config 1 (random) */
    {38, {0, 0, 0}, {0, 0, 0}}, /* Grahf's on-foot battle: table 14 config 3 */
    {59, {0, 0, 0}, {0, 0, 0}}, /* Solaris: field 546 config 0 (random) */
    {32, {1, 1, 1}, {1, 1, 1}}, /* Anima Relic 1: field 632 config 0 (random) */
    {44, {0, 0, 0}, {0, 0, 0}}, /* world map grassland: world map 0 area 2 config 0 */
    {47, {0, 0, 0}, {0, 0, 0}}, /* world map badlands: world map 0 area 8 config 4 */
    {43, {0, 0, 0}, {0, 0, 0}}, /* world map snowfield: world map 0 area 13 config 3 */
};
#define STAGES ((int)(sizeof stages / sizeof stages[0]))
/* The default (a setting's value: stage n is n + 1): Blackmoon Forest, the
 * mirror match's stage before there was a list, with the places of a real
 * battle there (the old ones put player 2's third member at the back, by
 * the status bar). */
#define STAGE_DEFAULT (1 + 1)

/* The list's entry for a setting's value (n stage n - 1), -1 none. */
static int stageEntry(int value) {
    for (int i = 0; i < STAGES; i++) {
        if (value == stages[i].id + 1) return i;
    }
    return -1;
}

int pcArenaStageCount(void) {
    return STAGES;
}

int pcArenaStageId(int i) {
    return i >= 0 && i < STAGES ? stages[i].id : -1;
}

int pcArenaStageSetting(void) {
    settingsDescribe("arena_stage", "Battle arena: the stage of a mirror match or party vs party, n stage n - 1 of "
                                    "those F1 > Extras > Battle arena lists (default 2, Blackmoon Forest; online the "
                                    "host's)");
    int v = settingsInt("arena_stage", STAGE_DEFAULT);
    return stageEntry(v) >= 0 ? v : STAGE_DEFAULT;
}

void pcArenaSetStage(int value) {
    settingsSetInt("arena_stage", stageEntry(value) >= 0 ? value : STAGE_DEFAULT);
    settingsSave();
}

/* buildBattlingGearList (src/battling/bl_8007E528.c), as Battling starts:
 * only a Battle arena session, never the reception's own Bonus Battling. */
int pcArenaAllGears(void) {
    return (pcArenaSession() == PC_ARENA_IN || pcArenaMatchKind() == ARENA_MATCH_BATTLING) && pcArenaAllGearsSetting();
}

int pcVersusGearsSetting(void) {
    settingsDescribe("arena_versus_gears", "Battle arena: in a versus battle the party fights in its Gears (0/1)");
    return settingsBool("arena_versus_gears", 0);
}

void pcVersusSetGears(int on) {
    settingsSetInt("arena_versus_gears", on != 0);
    settingsSave();
}

int pcVersusCallGearSetting(void) { return settingsBool("arena_versus_call_gear", 0); }

void pcVersusSetCallGear(int on) {
    settingsSetInt("arena_versus_call_gear", on != 0);
    settingsSave();
}

int pcVersusExitGearSetting(void) { return settingsBool("arena_versus_exit_gear", 0); }

void pcVersusSetExitGear(int on) {
    settingsSetInt("arena_versus_exit_gear", on != 0);
    settingsSave();
}

/* pcVersusBattleSetup (port/src/game/battle/versus.c): a versus battle's
 * party in its Gears. */
int pcVersusGears(void) {
    return pcVersusSession() == PC_VERSUS_IN && pcVersusGearsSetting();
}

/* ---- what each player sees (xg-htn.35.143) ----
 *
 * Each player sees their own side's status bar, as though they were player 1
 * (port/src/game/battle/mirror.c); the opponent's HP and EP only when the
 * match lets them (the Enemy info window, port/src/game/battle/enemyinfo.c).
 * Both are pictures only: the two games of a session stay the same whatever
 * each shows. */

static int perspective;         /* this machine's player online (netplay.c), 0 one screen */
static int perspectiveEnv = -1; /* XG_VERSUS_VIEW: -1 not read yet */

int pcVersusPerspective(void) {
    if (perspectiveEnv < 0) {
        const char* s = getenv("XG_VERSUS_VIEW");
        perspectiveEnv = s != NULL && (strcmp(s, "1") == 0 || strcmp(s, "2") == 0) ? s[0] - '0' : 0;
    }
    return perspectiveEnv != 0 ? perspectiveEnv : perspective;
}

void pcVersusSetPerspective(int player) {
    perspective = player == 1 || player == 2 ? player : 0;
}

int pcVersusOpponentHpSetting(void) {
    settingsDescribe("arena_opponent_hp", "Battle arena: in a versus battle the Enemy info window (Select) lists the "
                                          "opponent's HP and EP (0/1; online the host's)");
    return settingsBool("arena_opponent_hp", 0);
}

void pcVersusSetOpponentHp(int on) {
    settingsSetInt("arena_opponent_hp", on != 0);
    settingsSave();
}

void pcArenaKeyboard(int arena) {
    int on = arena && pcArenaKeyboardOffered() && pcArenaPlayer2Keyboard();
    if (on != keyboardOn && padSetKeyboardPort != NULL) {
        keyboardOn = on;
        padSetKeyboardPort(on);
    }
}

/* ---- versus battles ---- */

static int versusTable;
static int versusConfig;
static int versusEnvRead;
static int versusMirror; /* the request is a mirror match or party vs party (VERSUS_KIND_*), 0 not */

/* a versus battle asked for or running */
int pcVersusSessionBusy(void) {
    return versusPending || pcVersusSession() != PC_VERSUS_IDLE;
}

int pcVersusRequest(int table, int config) {
    if (table < 0 || table > 252 || config < 0 || config > 15 || pcVersusSession() != PC_VERSUS_IDLE ||
        pcArenaSession() != PC_ARENA_IDLE || pending) {
        return 0;
    }
    versusTable = table;
    versusConfig = config;
    versusPending = 1;
    versusMirror = 0;
    return 1;
}

int pcVersusRequestMirror(void) {
    if (!pcVersusRequest(VERSUS_MIRROR_TABLE, VERSUS_MIRROR_CONFIG)) {
        return 0;
    }
    versusMirror = VERSUS_KIND_MIRROR;
    return 1;
}

int pcVersusMirrorAsked(void) {
    return versusMirror;
}

void pcVersusCancel(void) {
    versusPending = 0;
}

int pcVersusPending(int* table, int* config) {
    if (!versusEnvRead) {
        const char* s = getenv("XG_VERSUS");
        int t, c;
        versusEnvRead = 1;
        if (s != NULL && strcmp(s, "mirror") == 0) {
            pcVersusRequestMirror();
        } else if (s != NULL && sscanf(s, "%d,%d", &t, &c) == 2) {
            pcVersusRequest(t, c);
        }
    }
    if (versusPending) {
        *table = versusTable;
        *config = versusConfig;
    }
    return versusPending;
}

void pcVersusTaken(void) {
    versusPending = 0;
}

int pcVersusPhase(int* table, int* config) {
    int session = pcVersusSession();
    *table = session != PC_VERSUS_IDLE ? pcVersusTable() : versusTable;
    *config = session != PC_VERSUS_IDLE ? pcVersusConfig() : versusConfig;
    return session != PC_VERSUS_IDLE ? session : versusPending ? ARENA_PHASE_PENDING : ARENA_PHASE_IDLE;
}

/* A session's steps on stderr with XG_ARENA_LOG=1 (versus battles: each
 * enemy turn's outcomes and player 2's choices). */
void pcArenaLog(const char* fmt, ...) {
    static int on = -1;
    va_list ap;

    if (on < 0) {
        const char* s = getenv("XG_ARENA_LOG");
        on = s != NULL && atoi(s) != 0;
    }
    if (!on) {
        return;
    }
    fputs("arena: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ---- matches from anywhere (xg-htn.35.144; the header above) ---- */

#define VERSUS_FIELD 22     /* a versus battle's start, as netplay.c's */
#define TITLE_FIELD 490     /* the title screen's field (the logo scene; the extras' field_locations.cpp) */
#define BATTLING_RECORDS 0x2344 /* Battling's records and bonus settings in gameState (12 bytes) */
#define BATTLING_RECORDS_SIZE 12

enum { MATCH_IDLE, MATCH_ASKED, MATCH_ON, MATCH_RETURNING };

void softReboot(void);           /* src/main/kernel.c: the game's soft reset */
int pcWideGetMode(void);         /* include/pc_wide.h */
int portGameMode(void);          /* memory.c: the mode bootGame started last (kernel.c's numbers) */
int netplayActive(void) __attribute__((weak));
int netplayMenuStartingBattle(void) __attribute__((weak));
void netplayLeave(void) __attribute__((weak));
void netplaySoftReset(void) __attribute__((weak));
void pcVersusApplySavedGears(const XgPartyRecord* party);

static struct {
    int phase;        /* MATCH_* */
    int kind;         /* ARENA_MATCH_BATTLING, ARENA_MATCH_VERSUS, ARENA_MATCH_MIRROR or ARENA_MATCH_PARTY */
    int table, config; /* versus: the formation */
    int party[3];     /* versus: character ids, 255 none */
    int fromSave;     /* versus: the members as the kept game has them */
    int hasRecord;    /* versus: an explicitly selected memory-card party */
    int armed;        /* 1: the next kernel boot starts the match; 2: this boot did */
    int rematch;      /* Battling: the game set its entry bytes for the rematch */
    int seen;         /* the match's mode reached since the start (Battling; the versus battle) */
    uint32_t askedAt; /* the vblank asked at */
    XgPartyRecord records[2]; /* party vs party: player 1's and player 2's parties (checked) */
    int stage;        /* mirror match, party vs party: the stage as arena_stage gives it (0 the match's own) */
} M;
/* The checked online match records, reapplied on every session boot. */
static struct {
    int kind, table, config;
    XgPartyRecord records[2];
    int party[3]; /* player 1's members (character ids, 255 none) */
} S;
static GameState keptState; /* the kept game's gameState, as the match was asked for */
static int gameSeen;        /* a field past the title, the world map or a battle since the boot: a game is loaded */
static int holdMenuIntro; /* shatter buffers left to draw before revealing the battle */
enum { MENU_PICTURE_W = 512, MENU_PICTURE_H = 288 };
static uint16_t menuPicture[MENU_PICTURE_W * MENU_PICTURE_H];
static int haveMenuPicture;

static int matchOnline(void) {
    return netplayActive != NULL && netplayActive();
}

void pcArenaCaptureMenuPicture(void) {
    haveMenuPicture = gpuCaptureMenu15(menuPicture, MENU_PICTURE_W, MENU_PICTURE_H);
}

char* pcArenaWriteMenuPicture(void) {
    if (!haveMenuPicture) return NULL;
    char* directory = SDL_GetPrefPath(NULL, "xenogears");
    char* path = NULL;
    if (directory != NULL) {
        SDL_asprintf(&path, "%s.xg-arena-%016llx-%08x.rgb555", directory,
                     (unsigned long long)SDL_GetTicksNS(), SDL_rand_bits());
        SDL_free(directory);
    }
    if (path != NULL && !SDL_SaveFile(path, menuPicture, sizeof menuPicture)) {
        SDL_RemovePath(path);
        SDL_free(path);
        path = NULL;
    }
    haveMenuPicture = 0;
    return path;
}

static void readMenuPicture(void) {
    const char* path = SDL_getenv("XG_NETPLAY_INTRO_FILE");
    if (path == NULL || *path == '\0') return;
    size_t size = 0;
    void* data = SDL_LoadFile(path, &size);
    if (data != NULL && size == sizeof menuPicture) {
        memcpy(menuPicture, data, size);
        haveMenuPicture = 1;
    }
    SDL_free(data);
}

/* Upload the menu as the same texture the retail shatter would sample,
 * including the side columns when the battle uses widescreen. */
int pcArenaUploadMenuShatter(void) {
    if (!holdMenuIntro) return -1;
    enum { W = 320, H = 224, MAX_MARGIN = 384 };
    static uint16_t picture[(2 * W + 2 * MAX_MARGIN) * H];
    int margin = pcWideView;
    if (margin < 0 || margin > MAX_MARGIN) margin = 0;
    int width = W + 2 * margin;
    uint16_t* center = picture + width * H;
    for (int y = 0; y < H; y++) {
        const uint16_t* row = menuPicture + y * MENU_PICTURE_H / H * MENU_PICTURE_W;
        for (int x = 0; x < width; x++) {
            picture[y * width + x] = haveMenuPicture ? row[x * MENU_PICTURE_W / width] : 0x8000;
        }
        memcpy(center + y * W, picture + y * width + margin, W * sizeof *picture);
    }
    RECT texture = {0x2C0, 0x100, W, H};
    LoadImage(&texture, center);
    pcWideCaptureLoad(&texture, picture, margin, 0);
    DrawSync(0);
    pcArenaLog("arena intro: glass shatter uses %s", haveMenuPicture ? "the menu" : "a blank backdrop");
    return margin;
}

/* Hold the settings page on screen until the new battle can draw its shatter. */
int pcArenaHoldBattleIntro(void) {
    const char* versus = getenv("XG_VERSUS");
    int wantsIntro = (M.armed && M.kind != ARENA_MATCH_BATTLING) || pcGauntletMenuIntroActive() ||
                     (matchOnline() && (S.kind || (versus != NULL && *versus != '\0')));
    /* The first draw fills only the back buffer. Keep the staging field
     * hidden until the shatter has drawn into both buffers. */
    holdMenuIntro = wantsIntro ? 2 : 0;
    if (holdMenuIntro && !haveMenuPicture && matchOnline()) readMenuPicture();
    if (holdMenuIntro) pcArenaLog("arena intro: holding the picture during battle setup");
    return wantsIntro;
}

int pcArenaBattleIntroHidden(void) {
    if (netplayMenuStartingBattle && netplayMenuStartingBattle()) return 1;
    return holdMenuIntro && (M.armed || matchOnline() || pcGauntletMenuIntroActive());
}

void pcArenaGlassShatterStarted(void) {
    if (!holdMenuIntro) return;
    if (--holdMenuIntro == 0) {
        pcArenaLog("arena intro: showing the glass shatter at vblank %u", hwVSyncCount());
    }
}

static int gameLoaded(void) {
    return gameSeen && !(portGameMode() == 1 && gameState.m231A_fieldID == TITLE_FIELD);
}

int pcArenaMatchKind(void) {
    return M.phase != MATCH_IDLE ? M.kind : 0;
}

int pcArenaMatchPhase(int* kind) {
    if (kind != NULL) *kind = M.kind;
    switch (M.phase) {
    case MATCH_ASKED: return ARENA_PHASE_PENDING;
    case MATCH_ON: return ARENA_PHASE_IN;
    case MATCH_RETURNING: return ARENA_PHASE_LEAVING;
    default: return ARENA_PHASE_IDLE;
    }
}

int pcArenaMatchAvailable(const char** why) {
    if (why) *why = NULL;
    if (matchOnline()) {
        if (why) *why = "Not in an online session's window.";
        return 0;
    }
    if (M.phase != MATCH_IDLE || pcArenaSession() != PC_ARENA_IDLE || pcVersusSessionBusy() || pending) {
        if (why) *why = "A match is on.";
        return 0;
    }
    return stateRestartAvailable(why);
}

static int matchStart(int kind, int table, int config, const int party[3], int fromSave, int stage,
                      const XgPartyRecord* record) {
    if (!pcArenaMatchAvailable(NULL) || (kind != ARENA_MATCH_BATTLING && kind != ARENA_MATCH_VERSUS &&
                                         kind != ARENA_MATCH_MIRROR && kind != ARENA_MATCH_PARTY && kind != ARENA_MATCH_ROSTER)) {
        return 0;
    }
    if (kind == ARENA_MATCH_MIRROR || kind == ARENA_MATCH_PARTY) {
        table = VERSUS_MIRROR_TABLE;
        config = VERSUS_MIRROR_CONFIG;
    }
    if (kind != ARENA_MATCH_BATTLING) {
        int members = 0;
        if (table < 0 || table > 252 || config < 0 || config > 15 || party == NULL || party[0] < 0 || party[0] > 10) {
            return 0;
        }
        for (int i = 0; i < 3; i++) {
            if (party[i] == 255) continue;
            if (party[i] < 0 || party[i] > 10 || (members & (1 << party[i]))) return 0;
            members |= 1 << party[i];
        }
        for (int i = 0; i < 3; i++) M.party[i] = party[i];
    }
    M.kind = kind;
    M.hasRecord = record != NULL;
    if (M.hasRecord) M.records[0] = *record;
    M.table = table;
    M.config = config;
    M.fromSave = fromSave && gameLoaded() && kind != ARENA_MATCH_PARTY;
    M.stage = kind != ARENA_MATCH_MIRROR && kind != ARENA_MATCH_PARTY ? 0 : stageEntry(stage) >= 0 ? stage : STAGE_DEFAULT;
    M.rematch = 0;
    M.seen = 0;
    M.armed = 1;
    M.askedAt = hwVSyncCount();
    M.phase = MATCH_ASKED;
    __builtin_memcpy(&keptState, &gameState, sizeof keptState);
    if (kind == ARENA_MATCH_PARTY) {
        pcArenaLog("match asked at vblank %u in mode %d (a game %s): party vs party table %d config %d, player 1 %d,%d,%d, "
                   "player 2 %d,%d,%d (their records)",
                   M.askedAt, portGameMode(), gameSeen ? "loaded" : "not loaded", table, config, M.party[0], M.party[1],
                   M.party[2], M.records[1].m4_members[0], M.records[1].m4_members[1], M.records[1].m4_members[2]);
    } else if (kind != ARENA_MATCH_BATTLING) {
        pcArenaLog("match asked at vblank %u in mode %d (a game %s): %s table %d config %d, party %d,%d,%d%s",
                   M.askedAt, portGameMode(), gameSeen ? "loaded" : "not loaded",
                   kind == ARENA_MATCH_MIRROR ? "mirror match" : "versus battle", table, config, M.party[0],
                   M.party[1], M.party[2], M.hasRecord || M.fromSave ? " (from a save)" : " (a new game's)");
    } else {
        pcArenaLog("match asked at vblank %u in mode %d, field %d (a game %s): Battling, story progress %d", M.askedAt,
                   portGameMode(), keptState.m231A_fieldID, gameSeen ? "loaded" : "not loaded",
                   keptState.m1930_fieldVarsBackup[0]);
    }
    if (M.stage != 0) pcArenaLog("match stage: %d", M.stage - 1);
    stateRequestRestart(1, softReboot);
    return 1;
}

/* A mirror match's or party vs party's stage: the setting's (arena_stage), as
 * the menu shows it when the match is asked; kept for its rematches. */
int pcArenaMatchStart(int kind, int table, int config, const int party[3], int fromSave) {
    return kind != ARENA_MATCH_PARTY && matchStart(kind, table, config, party, fromSave, pcArenaStageSetting(), NULL);
}

static void recordMembers(const XgPartyRecord* record, int party[3]) {
    for (int i = 0; i < 3; i++) party[i] = record->m4_members[i];
}

int pcArenaMatchStartSaved(int kind, int table, int config, const XgPartyRecord* record) {
    uint8_t codes[XG_PARTY_CODE_TABLE];
    int party[3];
    if (kind != ARENA_MATCH_MIRROR && kind != ARENA_MATCH_VERSUS && kind != ARENA_MATCH_ROSTER) return 0;
    if (record == NULL || !xgPartyGameCodes(codes) || xgPartyCheck(record, codes, NULL) != XG_PARTY_OK) return 0;
    recordMembers(record, party);
    return matchStart(kind, table, config, party, 0, pcArenaStageSetting(), record);
}

/* The FNV-1a of a record's bytes, for the log: the same on every machine. */
static uint32_t recordHash(const XgPartyRecord* r) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof *r; i++) h = (h ^ ((const uint8_t*)r)[i]) * 16777619u;
    return h;
}

static int matchStartParty(const XgPartyRecord* p1, const XgPartyRecord* p2, int stage, const char** why) {
    uint8_t codes[XG_PARTY_CODE_TABLE];
    XgPartyWhy refused;
    int party[3];
    if (why) *why = NULL;
    if (p1 == NULL || p2 == NULL || !pcArenaMatchAvailable(why)) return 0;
    int haveCodes = xgPartyGameCodes(codes);
    for (int p = 0; p < 2; p++) {
        /* the browser checked them; checked again here, as a peer's will be */
        if (xgPartyCheck(p ? p2 : p1, haveCodes ? codes : NULL, &refused) != XG_PARTY_OK) {
            pcArenaLog("party vs party: player %d's party refused: %s", p + 1, refused.text);
            if (why) *why = "A party could not be used.";
            return 0;
        }
    }
    for (int i = 0; i < 3; i++) party[i] = i < p1->m3_count ? p1->m4_members[i] : 255;
    M.records[0] = *p1;
    M.records[1] = *p2;
    pcArenaLog("party vs party: player 1's record fnv1a %08x, player 2's %08x", (unsigned)recordHash(p1),
               (unsigned)recordHash(p2));
    return matchStart(ARENA_MATCH_PARTY, 0, 0, party, 0, stage, NULL);
}

int pcArenaMatchStartParty(const XgPartyRecord* p1, const XgPartyRecord* p2, const char** why) {
    return matchStartParty(p1, p2, pcArenaStageSetting(), why);
}

int pcVersusStage(unsigned char party[3], unsigned char places[3]) {
    /* a local match's own (as asked); else the setting (XG_VERSUS=mirror, and
     * online, where the session pins it to the host's) */
    int local = M.phase == MATCH_ON && (M.kind == ARENA_MATCH_MIRROR || M.kind == ARENA_MATCH_PARTY);
    int custom = pcArenaRosterStage();
    int i = stageEntry(custom ? custom : local ? M.stage : pcArenaStageSetting());
    if (i < 0) return -1;
    for (int k = 0; k < 3; k++) {
        party[k] = stages[i].party[k];
        places[k] = stages[i].places[k];
    }
    return stages[i].id;
}

const unsigned char* pcVersusPartyRecord(int player) {
    if (player != 1 && player != 2) return NULL;
    if (player == 2 && pcArenaRosterMixed()) return (const unsigned char*)pcArenaRosterSecondParty();
    if (S.kind == ARENA_MATCH_PARTY && matchOnline()) return (const unsigned char*)&S.records[player - 1];
    if (M.phase != MATCH_ON || M.kind != ARENA_MATCH_PARTY) return NULL;
    return (const unsigned char*)&M.records[player - 1];
}

void pcArenaSessionParties(const XgPartyRecord* player1, const XgPartyRecord* player2) {
    if (player1 == NULL || player2 == NULL) {
        memset(&S, 0, sizeof S);
        return;
    }
    S.records[0] = *player1;
    S.records[1] = *player2;
    recordMembers(player1, S.party);
    S.kind = ARENA_MATCH_PARTY;
    S.table = VERSUS_MIRROR_TABLE;
    S.config = VERSUS_MIRROR_CONFIG;
    pcArenaLog("party vs party online: player 1's record fnv1a %08x, player 2's %08x", (unsigned)recordHash(player1),
               (unsigned)recordHash(player2));
}

void pcArenaSessionVersus(int kind, int table, int config, const int members[3], const XgPartyRecord* party) {
    S.kind = kind;
    S.table = table;
    S.config = config;
    if (party) {
        S.records[0] = *party;
        recordMembers(party, S.party);
    } else {
        memset(&S.records[0], 0, sizeof S.records[0]);
        memcpy(S.party, members, sizeof S.party);
    }
}

/* Back to the kept game (at the next vblank). */
static void matchReturn(const char* why) {
    if (M.phase != MATCH_ON && M.phase != MATCH_ASKED) return;
    pcArenaLog("match over (%s): back to the kept game", why);
    M.phase = MATCH_RETURNING;
    M.armed = 0;
    pcArenaKeyboard(0);
    stateRequestReturn();
}

void pcArenaMatchQuit(void) {
    matchReturn("left from the menu");
}

int pcArenaMatchOn(void) {
    if (matchOnline()) return 2;
    return M.phase == MATCH_ON ? 1 : 0;
}

void pcArenaMatchRematch(void) {
    pcArenaLog("rematch at vblank %u", hwVSyncCount());
    if (!matchOnline() && M.phase == MATCH_ON) {
        M.rematch = M.kind == ARENA_MATCH_BATTLING; /* Battling's entry bytes are the game's (battling/arena.c) */
    }
}

void pcArenaMatchLeave(void) {
    pcArenaLog("leave at vblank %u", hwVSyncCount());
    if (matchOnline()) {
        if (netplayLeave != NULL) netplayLeave();
        return;
    }
    matchReturn("Leave");
}

/* port/src/system/reset.c: a soft reset while the match runs starts it
 * again; one without a match ends the loaded game. Online the session's own
 * start comes again (its options are still set: netplay.c). */
void pcArenaSoftReset(void) {
    holdMenuIntro = 0;
    if (M.phase == MATCH_ASKED || M.phase == MATCH_ON) {
        M.armed = 1;
        M.seen = 0;
        if (M.kind != ARENA_MATCH_BATTLING && cdBootDiscNumber() != 1 && cdSwapForReset()) {
            if (cdBootDiscNumber() == 1) {
                cdBootTablesReload(); /* field 22 is disc 1's: its tables (the kept game's disc comes back with it) */
            } else {
                cdSwapForReset(); /* not disc 1 either: the one before */
            }
        }
    } else {
        M.armed = 0;
        if (!stateKept()) gameSeen = 0; /* a gauntlet also boots with the original game parked */
    }
    if (matchOnline()) {
        versusEnvRead = 0; /* XG_VERSUS asked again */
        if (netplaySoftReset != NULL) netplaySoftReset();
    }
}

int pcArenaBootMode(void) {
    if (!M.armed) return -1;
    return M.kind == ARENA_MATCH_BATTLING ? 4 : 1;
}

/* A member as the kept game has them: their record, abilities, name and
 * Gear; HP, EP, the Gear's HP and fuel full. */
static void memberFromSave(int c) {
    GameStateCharacter* m = &gameState.m26C_party[c];
    *m = keptState.m26C_party[c];
    gameState.m16C0_abilities[c] = keptState.m16C0_abilities[c];
    __builtin_memcpy(gameState.m0_names[c], keptState.m0_names[c], sizeof gameState.m0_names[c]);
    m->m4C_HP = m->m4E_MaxHP;
    m->m50_MP = m->m52_MaxMP;
    if (m->mA0_gearNum < 20) {
        GameStateGear* g = &gameState.m978_gears[m->mA0_gearNum];
        *g = keptState.m978_gears[m->mA0_gearNum];
        __builtin_memcpy(gameState.m0_names[11 + m->mA0_gearNum], keptState.m0_names[11 + m->mA0_gearNum],
                         sizeof gameState.m0_names[0]);
        g->m60_hp = g->m64_maxHp;
        g->m38_fuel = g->m3A_maxFuel;
    }
}

/* Copy Player 1's battle data into the fresh game. Party vs party applies
 * Player 2's record separately in mirror.c. */
static void partyFromRecord(const XgPartyRecord* r, int allowGears) {
    memcpy(gameState.m1D34_currentParty, r->m4_members, XG_PARTY_MEMBERS);
    for (int k = 0; k < r->m3_count && k < XG_PARTY_MEMBERS; k++) {
        int c = r->m4_members[k];
        GameStateCharacter* m = &gameState.m26C_party[c];
        memcpy(m, r->m10_characters[k], sizeof *m);
        m->m4C_HP = m->m4E_MaxHP;
        m->m50_MP = m->m52_MaxMP;
        if (!allowGears) m->mA0_gearNum = 0xFF;
        m->m56_battleCommandLoadout = (u8)c;
        memcpy(&gameState.m16C0_abilities[c], r->m1FC_abilities[k], sizeof gameState.m16C0_abilities[c]);
        memcpy(gameState.m0_names[c], r->m25C_names[k], sizeof gameState.m0_names[c]);
        gameState.m1D30_partyMemberBitField |= 1 << c;
        gameState.m1D32_partyFrameMask |= 1 << c; /* as the field's join op */
    }
    memset(gameState.m2026_inventory0Ids, 0, sizeof gameState.m2026_inventory0Ids);
    memset(gameState.m1F90_inventory0Counts, 0, sizeof gameState.m1F90_inventory0Counts);
    memset(gameState.m1D9C_inventory1Ids, 0, sizeof gameState.m1D9C_inventory1Ids);
    memset(gameState.m1D38_inventory1Counts, 0, sizeof gameState.m1D38_inventory1Counts);
    for (int i = 0; i < XG_PARTY_ITEMS && r->m298_itemIds[i] != 0; i++) {
        gameState.m2026_inventory0Ids[i] = r->m298_itemIds[i];
        gameState.m1F90_inventory0Counts[i] = r->m2C8_itemCounts[i];
    }
    for (int i = 0; i < XG_PARTY_ITEMS && r->m2F8_ammoIds[i] != 0; i++) {
        gameState.m1D9C_inventory1Ids[i] = r->m2F8_ammoIds[i];
        gameState.m1D38_inventory1Counts[i] = r->m328_ammoCounts[i];
    }
    memcpy(gameState.m22B8_ammoCounts, r->m358_ammoTable, sizeof gameState.m22B8_ammoCounts);
    for (int i = 0; i < 3; i++) gameState.m22B1_isOnGear[i] = 0;
    if (allowGears) pcVersusApplySavedGears(r);
}

/* The staging field may fill vacant slots during initialization. Apply the
 * checked record immediately before the battle loads its party. */
int pcArenaApplyMatchParty(void) {
    const XgPartyRecord* record;
    int kind;
    if (S.kind && matchOnline()) {
        if (!S.records[0].m3_count) return 0;
        record = &S.records[0];
        kind = S.kind;
    } else if (M.phase == MATCH_ON && (M.hasRecord || M.kind == ARENA_MATCH_PARTY)) {
        record = &M.records[0];
        kind = M.kind;
    } else {
        return 0;
    }
    int allowGears = kind == ARENA_MATCH_ROSTER
                         ? pcArenaRosterCallGear() || pcArenaRosterStartInGear()
                         : pcVersusCallGearSetting() || pcVersusGearsSetting();
    partyFromRecord(record, allowGears);
    return 1;
}

static void versusStart(int kind, const int party[3], int table, int config, int hasRecord, int fromSave) {
    gameState.m1930_fieldVarsBackup[0] = 0;
    gameState.m231A_fieldID = VERSUS_FIELD;
    if (!hasRecord) {
        for (int i = 0; i < 3; i++) {
            gameState.m1D34_currentParty[i] = (u8)party[i];
            if (party[i] == 255) continue;
            gameState.m1D30_partyMemberBitField |= 1 << party[i];
            if (fromSave) memberFromSave(party[i]);
        }
    }
    versusTable = table;
    versusConfig = config;
    versusPending = 1;
    versusMirror = kind == ARENA_MATCH_MIRROR ? VERSUS_KIND_MIRROR : kind == ARENA_MATCH_PARTY ? VERSUS_KIND_PARTY : 0;
    versusEnvRead = 1;
    pcArenaLog("match start: %s table %d config %d in field %d, party %d,%d,%d",
               kind == ARENA_MATCH_PARTY ? "party vs party" : versusMirror ? "mirror match" : "versus battle", table,
               config, VERSUS_FIELD, party[0], party[1], party[2]);
}

/* options.c portStartState: the match's start, applied to the fresh game. */
int pcArenaStartState(void) {
    if (S.kind && matchOnline()) {
        versusStart(S.kind, S.party, S.table, S.config, S.records[0].m3_count != 0, 0);
        return 1;
    }
    if (!M.armed) return 0;
    if (M.kind == ARENA_MATCH_BATTLING) {
        gameState.m1930_fieldVarsBackup[0] = keptState.m1930_fieldVarsBackup[0];
        __builtin_memcpy((u8*)&gameState + BATTLING_RECORDS, (const u8*)&keptState + BATTLING_RECORDS,
                         BATTLING_RECORDS_SIZE);
        if (!M.rematch) {
            battlingEntryParams = 1; /* Battling's main menu, as the field's op 0xBF asks */
            battlingEntryMatchType = battlingEntryGear1 = battlingEntryGear2 = 0;
            battlingEntryWinsNeeded = battlingEntryComLevel = 0;
        }
        pcArenaLog("match start: Battling%s (entry %d: match type %d, Gears %d and %d, wins %d)",
                   M.rematch ? ", a rematch" : "", battlingEntryParams, battlingEntryMatchType, battlingEntryGear1,
                   battlingEntryGear2, battlingEntryWinsNeeded);
        M.rematch = 0;
    } else {
        versusStart(M.kind, M.party, M.table, M.config, M.hasRecord || M.kind == ARENA_MATCH_PARTY, M.fromSave);
    }
    M.armed = 2;
    M.phase = MATCH_ON;
    return 1;
}

/* A party vs party <save> (the header: XG_ARENA_MATCH's, and netplay's
 * --save): its record packed from the cards as the save browser packs it
 * (port/src/ui/arena_saves.cpp), its names decoded with `codes`. */
int pcArenaPackSave(const char* spec, const uint8_t* codes, XgPartyRecord* out) {
    int v[5] = {0, 0, 255, 255, 255};
    int n = sscanf(spec, "%d.%d.%d.%d.%d", &v[0], &v[1], &v[2], &v[3], &v[4]);
    static CardGameSave envSaves[2 * 15]; /* both cards' blocks */
    XgPartyWhy why;
    int members[3] = {255, 255, 255};
    if (n < 2) {
        fprintf(stderr, "arena: a save is <card>.<number>[.<id>...], not '%s'\n", spec);
        return 0;
    }
    CardGameSave* list = envSaves;
    int count = cardGameSaves(list, (int)(sizeof envSaves / sizeof envSaves[0]), cardGameRegion());
    for (int i = 0; i < count; i++) {
        if (list[i].card != v[0] - 1 || list[i].number != v[1]) continue;
        const uint8_t* block = cardGameSaveBlock(&list[i]);
        if (n > 2) {
            for (int k = 0; k < 3; k++) members[k] = v[2 + k];
        } else {
            for (int k = 0, m = 0; k < 3; k++) { /* the save's own party, as far as its members have joined */
                int id = list[i].party[k];
                if (id < CARD_SAVE_MEMBERS && (list[i].joined & 1 << id) && (m == 0 || members[0] != id) &&
                    (m < 2 || members[1] != id)) {
                    members[m++] = id;
                }
            }
        }
        if (block == NULL || codes == NULL || xgPartyPack(out, block, members, codes, &why) != XG_PARTY_OK) {
            fprintf(stderr, "arena: card %d No.%02d: %s\n", v[0], v[1],
                    block == NULL ? "the save changed" : codes == NULL ? "no character-code table (the disc)" : why.text);
            return 0;
        }
        pcArenaLog("party vs party: card %d No.%02d (%s), members %d,%d,%d", v[0], v[1], list[i].location, members[0],
                   members[1], members[2]);
        return 1;
    }
    fprintf(stderr, "arena: no save No.%02d of this game on card %d\n", v[1], v[0]);
    return 0;
}

static int envParty(const char* spec, XgPartyRecord* out) {
    uint8_t codes[XG_PARTY_CODE_TABLE];
    return pcArenaPackSave(spec, xgPartyGameCodes(codes) ? codes : NULL, out);
}

/* arena replay form: roster,BANK.TYPE[+BANK.TYPE...],BANK.TYPE[+...].
 * "party" or "party=0.9.3" in the first field selects Player 1's party. */
static int envRoster(const char* text, int stage) {
    ArenaLoadoutReview review = {0};
    int* party = review.current.party;
    const char* why = "Expected two enemy teams in bank.type notation.";
    party[0] = 0;
    party[1] = 2;
    party[2] = 255;
    review.current.loadout.stage = stage;
    for (int side = 0; side < 2; side++) {
        ArenaEnemyTeam* team = &review.current.loadout.side[side];
        if (side == 0 && strncmp(text, "party", 5) == 0) {
            text += 5;
            if (*text == '=') {
                int bytes = 0;
                if (sscanf(text + 1, "%d.%d.%d%n", &party[0], &party[1], &party[2], &bytes) != 3) goto refused;
                text += 1 + bytes;
            }
            if (*text++ != ',') goto refused;
            continue;
        }
        for (;;) {
            int bank, type, end = 0;
            if (sscanf(text, "%d.%d%n", &bank, &type, &end) != 2 || bank < 0 || bank > 255 ||
                type < 0 || type >= 8 || team->count == ARENA_ROSTER_SLOTS) goto refused;
            int slot = team->count++;
            for (int i = 0; i < ARENA_ROSTER_STATS; i++) team->multiplier[slot][i] = 100;
            team->enemy[slot] = (ArenaEnemy){bank, type};
            text += end;
            if (*text == '@') {
                if (sscanf(text + 1, "%d.%d%n", &bank, &type, &end) != 2 || bank < 0 ||
                    bank > 255 || type < 0 || type >= 8) goto refused;
                team->gear[slot] = (ArenaEnemy){bank, type};
                team->gearSlots |= 1 << slot;
                review.current.loadout.callGear = 1;
                text += end + 1;
            }
            if (*text != '+') break;
            text++;
        }
        if (*text != (side == 0 ? ',' : 0)) goto refused;
        if (side == 0) text++;
    }
    pcArenaLoadoutApprove(&review, 0, 1);
    pcArenaLoadoutApprove(&review, 1, 1);
    if (pcArenaRosterStart(&review, &why)) return 1;
refused:
    fprintf(stderr, "arena: XG_ARENA_MATCH roster: %s\n", why);
    return 0;
}

/* XG_ARENA_MATCH (the header): a request as the menu makes it. */
static void envMatch(void) {
    static int parsed;
    static unsigned at;
    static char what[160];
    if (!parsed) {
        const char* s = getenv("XG_ARENA_MATCH");
        parsed = 1;
        if (s == NULL || sscanf(s, "%u:%159s", &at, what) != 2) what[0] = '\0';
    }
    if (what[0] == '\0' || hwVSyncCount() < at || !pcArenaMatchAvailable(NULL)) return;
    /* ,arena=<n>: a mirror match's or party vs party's stage n (one of the
     * list's), else the setting's */
    int stage = pcArenaStageSetting();
    char* field = strstr(what, ",arena=");
    if (field != NULL) {
        int n = atoi(field + 7);
        char* rest = strchr(field + 1, ',');
        if (rest == NULL) rest = field + strlen(field);
        memmove(field, rest, strlen(rest) + 1);
        stage = n + 1;
        if (n < 0 || stageEntry(stage) < 0) {
            fprintf(stderr, "arena: XG_ARENA_MATCH: arena=%d is not one of the stages listed (system/arena.c)\n", n);
            what[0] = '\0';
            return;
        }
    }
    if (strncmp(what, "roster,", 7) == 0) {
        envRoster(what + 7, stage - 1);
    } else if (strncmp(what, "battling", 8) == 0) {
        pcArenaMatchStart(ARENA_MATCH_BATTLING, 0, 0, NULL, 0);
    } else if (strncmp(what, "party,", 6) == 0) {
        static XgPartyRecord envSides[2];
        XgPartyRecord* sides = envSides;
        char* second = strchr(what + 6, ',');
        if (second == NULL) {
            fprintf(stderr, "arena: XG_ARENA_MATCH: expected <vblank>:party,<save>,<save>\n");
        } else {
            *second++ = '\0';
            if (envParty(what + 6, &sides[0]) && envParty(second, &sides[1])) {
                matchStartParty(&sides[0], &sides[1], stage, NULL);
            }
        }
    } else {
        int t = 0, c = 0, party[3] = {0, 2, 255}, fromSave = 1;
        int kind = ARENA_MATCH_VERSUS;
        char how[64] = "current";
        if (strncmp(what, "mirror", 6) == 0) {
            kind = ARENA_MATCH_MIRROR; /* mirror[,party] */
            t = VERSUS_MIRROR_TABLE;
            c = VERSUS_MIRROR_CONFIG;
            if (what[6] == ',') snprintf(how, sizeof how, "%.*s", (int)sizeof how - 1, what + 7);
        }
        if (kind == ARENA_MATCH_VERSUS && sscanf(what, "versus,%d,%d,%63s", &t, &c, how) < 2) {
            fprintf(stderr, "arena: XG_ARENA_MATCH: expected <vblank>:battling, <vblank>:versus,T,C[,party], "
                            "<vblank>:mirror[,party][,arena=N] or <vblank>:party,<save>,<save>[,arena=N]\n");
        } else if (strcmp(how, "new") == 0) {
            fromSave = 0;
            matchStart(kind, t, c, party, fromSave, stage, NULL);
        } else {
            if (strcmp(how, "current") == 0) {
                for (int i = 0; i < 3; i++) party[i] = gameState.m1D34_currentParty[i] > 10 ? 255 : gameState.m1D34_currentParty[i];
                if (party[0] == 255) party[0] = 0, party[1] = 2, party[2] = 255;
            } else {
                party[1] = party[2] = 255;
                sscanf(how, "%d.%d.%d", &party[0], &party[1], &party[2]);
            }
            matchStart(kind, t, c, party, fromSave, stage, NULL);
        }
    }
    what[0] = '\0'; /* once */
}

/* Every vblank (netplay.c netplayPace, the pads' vblank). */
void pcArenaVBlank(void) {
    int mode = pcWideGetMode();
    int booted = portGameMode();
    switch (M.phase) {
    case MATCH_IDLE:
        if (!stateKept() && ((booted == 1 && gameState.m231A_fieldID != TITLE_FIELD) || booted == 2 || booted == 3)) {
            gameSeen = 1; /* a field past the title, a battle or the world map (not during a match: its own) */
        }
        envMatch();
        break;
    case MATCH_ASKED:
        if (!stateBusy() && !stateKept()) {
            pcArenaLog("match not started: the game could not be kept");
            M.phase = MATCH_IDLE;
            M.armed = 0;
        }
        break;
    case MATCH_ON:
        if (M.kind == ARENA_MATCH_BATTLING) {
            if (mode == 4) {
                M.seen = 1;
            } else if (M.seen) {
                matchReturn("Battling left");
            }
        } else if (pcVersusSession() == PC_VERSUS_IN) {
            M.seen = 1;
        } else if (M.seen && pcVersusSession() == PC_VERSUS_IDLE) {
            matchReturn("the versus battle ended");
        }
        break;
    case MATCH_RETURNING:
        if (!stateKept()) {
            M.phase = MATCH_IDLE;
            if (M.kind == ARENA_MATCH_ROSTER) pcArenaRosterClear();
            pcArenaLog("match over: the kept game is back at vblank %u", hwVSyncCount());
        }
        break;
    }
}

/* ---- save states (savestate.h) ---- */

/* A session asked for (the sessions themselves are game memory). After a
 * load the keyboard goes to the port the restored session wants. */
void arenaStateIo(XgState* s) {
    XG_STATE_VAR(s, pending);
    XG_STATE_VAR(s, versusPending);
    XG_STATE_VAR(s, versusTable);
    XG_STATE_VAR(s, versusConfig);
    XG_STATE_VAR(s, versusMirror);
    if (xgStateLoading(s)) {
        pcArenaKeyboard(pcArenaSession() == PC_ARENA_IN);
    }
}
