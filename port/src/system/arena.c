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
 * rewind) are off meanwhile (savestate.c stateAvailable). Online the
 * session is a process of its own and its own start (netplay.c): nothing
 * is kept here.
 * XG_ARENA_MATCH=<vblank>:battling | <vblank>:versus,<table>,<config>[,current|new|a.b.c]
 * | <vblank>:mirror[,current|new|a.b.c] asks for a match as the menu does,
 * at that vblank (headless runs).
 *
 * Mirror matches (xg-htn.35.143; the battle's side: port/src/game/battle/
 * mirror.c): a versus battle where player 2 commands a copy of player 1's
 * party, in the mirror's stage (include/pc_arena.h VERSUS_MIRROR_TABLE and
 * CONFIG); from the menu as a match (ARENA_MATCH_MIRROR), XG_VERSUS=mirror
 * from the start. Local only for now: online sessions offer none.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "main/game_state.h"
#include "arena.h"
#include "cd/cd.h"
#include "hw.h"
#include "pc_arena.h"
#include "savestate.h"
#include "settings.h"
#include "stateimage.h"

_Static_assert((int)ARENA_PHASE_IDLE == (int)PC_ARENA_IDLE && (int)ARENA_PHASE_STARTING == (int)PC_ARENA_STARTING &&
                   (int)ARENA_PHASE_LEAVING == (int)PC_ARENA_LEAVING && (int)ARENA_PHASE_IN == (int)PC_ARENA_IN,
               "the menu's phases are the session's");
_Static_assert((int)ARENA_PHASE_LEAVING == (int)PC_VERSUS_LEAVING && (int)ARENA_PHASE_IN == (int)PC_VERSUS_IN,
               "the menu's phases are the versus session's");

void padSetKeyboardPort(int port) __attribute__((weak));

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
static int versusMirror; /* the request is a mirror match */

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
    versusMirror = 1;
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
void netplayLeave(void) __attribute__((weak));
void netplaySoftReset(void) __attribute__((weak));

static struct {
    int phase;        /* MATCH_* */
    int kind;         /* ARENA_MATCH_BATTLING, ARENA_MATCH_VERSUS or ARENA_MATCH_MIRROR */
    int table, config; /* versus: the formation */
    int party[3];     /* versus: character ids, 255 none */
    int fromSave;     /* versus: the members as the kept game has them */
    int armed;        /* 1: the next kernel boot starts the match; 2: this boot did */
    int rematch;      /* Battling: the game set its entry bytes for the rematch */
    int seen;         /* the match's mode reached since the start (Battling; the versus battle) */
    uint32_t askedAt; /* the vblank asked at */
} M;
static GameState keptState; /* the kept game's gameState, as the match was asked for */
static int gameSeen;        /* a field past the title, the world map or a battle since the boot: a game is loaded */

static int matchOnline(void) {
    return netplayActive != NULL && netplayActive();
}

int pcArenaGameLoaded(void) {
    return gameSeen;
}

int pcArenaPartyNow(int party[3]) {
    int n = 0;
    party[0] = party[1] = party[2] = 255;
    for (int i = 0; i < 3; i++) {
        int c = gameState.m1D34_currentParty[i];
        if (c <= 10) party[n++] = c; /* in order, the first slot someone (the field's player) */
    }
    return n;
}

int pcArenaMembersJoined(void) {
    return gameState.m1D30_partyMemberBitField & 0x7FF;
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

int pcArenaMatchStart(int kind, int table, int config, const int party[3], int fromSave) {
    if (!pcArenaMatchAvailable(NULL) ||
        (kind != ARENA_MATCH_BATTLING && kind != ARENA_MATCH_VERSUS && kind != ARENA_MATCH_MIRROR)) {
        return 0;
    }
    if (kind == ARENA_MATCH_MIRROR) {
        table = VERSUS_MIRROR_TABLE;
        config = VERSUS_MIRROR_CONFIG;
    }
    if (kind == ARENA_MATCH_VERSUS || kind == ARENA_MATCH_MIRROR) {
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
    M.table = table;
    M.config = config;
    M.fromSave = fromSave && gameSeen;
    M.rematch = 0;
    M.seen = 0;
    M.armed = 1;
    M.askedAt = hwVSyncCount();
    M.phase = MATCH_ASKED;
    __builtin_memcpy(&keptState, &gameState, sizeof keptState);
    if (kind != ARENA_MATCH_BATTLING) {
        pcArenaLog("match asked at vblank %u in mode %d (a game %s): %s table %d config %d, party %d,%d,%d%s",
                   M.askedAt, portGameMode(), gameSeen ? "loaded" : "not loaded",
                   kind == ARENA_MATCH_MIRROR ? "mirror match" : "versus battle", table, config, M.party[0],
                   M.party[1], M.party[2], M.fromSave ? " (from the save)" : " (a new game's)");
    } else {
        pcArenaLog("match asked at vblank %u in mode %d, field %d (a game %s): Battling, story progress %d", M.askedAt,
                   portGameMode(), keptState.m231A_fieldID, gameSeen ? "loaded" : "not loaded",
                   keptState.m1930_fieldVarsBackup[0]);
    }
    stateRequestRestart(1, softReboot);
    return 1;
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
        gameSeen = 0;
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

/* options.c portStartState: the match's start, applied to the fresh game. */
int pcArenaStartState(void) {
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
        for (int i = 0; i < 3; i++) {
            gameState.m1D34_currentParty[i] = (u8)M.party[i];
            if (M.party[i] != 255) gameState.m1D30_partyMemberBitField |= 1 << M.party[i];
        }
        gameState.m1930_fieldVarsBackup[0] = 0;
        gameState.m231A_fieldID = VERSUS_FIELD;
        if (M.fromSave) {
            for (int i = 0; i < 3; i++) {
                if (M.party[i] != 255) memberFromSave(M.party[i]);
            }
        }
        versusTable = M.table;
        versusConfig = M.config;
        versusPending = 1;
        versusMirror = M.kind == ARENA_MATCH_MIRROR;
        versusEnvRead = 1;
        pcArenaLog("match start: %s table %d config %d in field %d, party %d,%d,%d",
                   versusMirror ? "mirror match" : "versus battle", M.table, M.config, VERSUS_FIELD, M.party[0],
                   M.party[1], M.party[2]);
    }
    M.armed = 2;
    M.phase = MATCH_ON;
    return 1;
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
    if (strncmp(what, "battling", 8) == 0) {
        pcArenaMatchStart(ARENA_MATCH_BATTLING, 0, 0, NULL, 0);
    } else {
        int t = 0, c = 0, party[3] = {0, 2, 255}, fromSave = 1;
        int kind = ARENA_MATCH_VERSUS;
        char how[64] = "current";
        if (strncmp(what, "mirror", 6) == 0) {
            kind = ARENA_MATCH_MIRROR; /* mirror[,party] */
            t = VERSUS_MIRROR_TABLE;
            c = VERSUS_MIRROR_CONFIG;
            if (what[6] == ',') snprintf(how, sizeof how, "%s", what + 7);
        }
        if (kind == ARENA_MATCH_VERSUS && sscanf(what, "versus,%d,%d,%63s", &t, &c, how) < 2) {
            fprintf(stderr, "arena: XG_ARENA_MATCH: expected <vblank>:battling, <vblank>:versus,T,C[,party] or "
                            "<vblank>:mirror[,party]\n");
        } else if (strcmp(how, "new") == 0) {
            fromSave = 0;
            pcArenaMatchStart(kind, t, c, party, fromSave);
        } else {
            if (strcmp(how, "current") == 0) {
                for (int i = 0; i < 3; i++) party[i] = gameState.m1D34_currentParty[i] > 10 ? 255 : gameState.m1D34_currentParty[i];
                if (party[0] == 255) party[0] = 0, party[1] = 2, party[2] = 255;
            } else {
                party[1] = party[2] = 255;
                sscanf(how, "%d.%d.%d", &party[0], &party[1], &party[2]);
            }
            pcArenaMatchStart(kind, t, c, party, fromSave);
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
        if ((booted == 1 && gameState.m231A_fieldID != TITLE_FIELD) || booted == 2 || booted == 3) {
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
