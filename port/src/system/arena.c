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
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "pc_arena.h"
#include "savestate.h"
#include "settings.h"

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

int pcArenaDirectEntry(unsigned char params[6]) {
    if (!directAsked) {
        return 0;
    }
    for (int i = 0; i < 6; i++) {
        params[i] = directParams[i];
    }
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

/* buildBattlingGearList (src/battling/bl_8007E528.c), as Battling starts:
 * only a Battle arena session, never the reception's own Bonus Battling. */
int pcArenaAllGears(void) {
    return pcArenaSession() == PC_ARENA_IN && pcArenaAllGearsSetting();
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
    return 1;
}

void pcVersusCancel(void) {
    versusPending = 0;
}

int pcVersusPending(int* table, int* config) {
    if (!versusEnvRead) {
        const char* s = getenv("XG_VERSUS");
        int t, c;
        versusEnvRead = 1;
        if (s != NULL && sscanf(s, "%d,%d", &t, &c) == 2) {
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

/* ---- save states (savestate.h) ---- */

/* A session asked for (the sessions themselves are game memory). After a
 * load the keyboard goes to the port the restored session wants. */
void arenaStateIo(XgState* s) {
    XG_STATE_VAR(s, pending);
    XG_STATE_VAR(s, versusPending);
    XG_STATE_VAR(s, versusTable);
    XG_STATE_VAR(s, versusConfig);
    if (xgStateLoading(s)) {
        pcArenaKeyboard(pcArenaSession() == PC_ARENA_IN);
    }
}
