/*
 * Netplay (xg-htn.35.16, online, stage 1): two players on two machines (or
 * two processes), Battling's PLAYER1 VS PLAYER2 in input-delay lockstep.
 *
 *   xenogears --netplay host --code WORD [--port P] [--delay N]
 *             [--progress N | --all-gears] [--versus TABLE,CONFIG|joiner [--party A,B,C]]
 *             [--party-vs-party --save CARD.NUMBER[.ID...]] [disc.cue]
 *   xenogears --netplay join HOST:PORT --code WORD [--battling | --formation TABLE,CONFIG]
 *             [--save CARD.NUMBER[.ID...]] [disc.cue]
 *   (the code also from XG_NETPLAY_CODE; the party record also from
 *   XG_NETPLAY_PARTY, its bytes in hex: the menu's)
 *
 * The host is player 1 (port 1, the left Gear), the one who joins player 2.
 * LAN, a direct address with a forwarded UDP port (default 7461), or the
 * players' own VPN: there is no server in between.
 *
 * Why lockstep holds: on the stepped clock (XG_CLOCK=step, hw.c) the game is
 * a function of its inputs: time stands still while it computes, a vblank
 * comes after the same work on every machine, and nothing else reaches it
 * (separate runs of one build give the same frames and sound for the same pad
 * script). So both processes boot the same way from power-on and at every
 * vblank fill both pads with the same two bytes: the host's and the joiner's,
 * each sampled `delay` vblanks earlier on its own machine and sent to the
 * other. A vblank waits until the other player's input for it is there. Each
 * vblank is also held back to real time (the stepped clock alone would run
 * as fast as the machine can).
 *
 * The session: the joiner sends HELLO, the host answers WELCOME with the
 * input delay and the story progress the Gear list follows (Battling's unlock
 * levels; --all-gears: every Gear). Both must run the same game: the same
 * compatibility id (tools/netplay_compat.py: a hash of the sources that decide
 * the game, made at each build; so a Linux and a Windows build of the same
 * sources play together, and the state hash below leaves out host addresses)
 * and, checked once the game runs, the same disc (a hash of its file table
 * and size); then each process starts from power-on into
 * Battling's main menu (XG_BOOT_MODE=4) with that progress (XG_GAMESTATE),
 * on the stepped clock. BONUS BATTLING > PLAYER1 VS PLAYER2 from there. When
 * the game leaves Battling (EXIT), the session ends.
 *
 * --versus: a versus battle instead (port/src/game/battle/versus.c): player 2
 * commands the formation (the battle debug selector's table and config) on
 * pad 2, the joiner's, against the host's party. Each player picks their own
 * side: with --versus joiner the formation is player 2's (--formation, sent
 * in its HELLO, range-checked; a joiner without one is refused, "pick"), with
 * --versus TABLE,CONFIG the host's. Its start is as canonical:
 * power-on into field 22 at the story's start (XG_FIELD, progress 0) with the
 * party given (--party: character ids, default 0,2: Fei and Citan, as the new
 * game makes them: their starting levels, equipment and Gears, never anyone's
 * save), and the formation asked for at once (XG_VERSUS): the battle starts
 * when the field first lets the party walk. "Party in its Gears" is the
 * host's (the settings below).
 *
 * --party-vs-party (xg-htn.35.143): each player's own party from a memory-card
 * save on their own machine, on foot, the host's in slots 0-2 against the
 * joiner's in 3-5 (port/src/game/battle/mirror.c), on the host's choice of
 * stage. Each side's party is a party record (port/src/card/party.h): the
 * menu packs it from the save chosen in its browser (XG_NETPLAY_PARTY), or
 * --save packs it here from this machine's cards (card 1 or 2, the save's
 * number, the members' character ids in order; the save's own party
 * without them) before the session puts the cards away. Each side checks
 * its own and the other's with the same rules (xgPartyCheck: every value
 * the battle indexes with in the game's ranges; edited saves are accepted
 * within them, so the other's party is theirs and not verified), sends its
 * own in the handshake (netplay_core.c PARTY pages; a joiner's refused gets
 * the reason in its WELCOME, a host's in the joiner's BYE: both sides say
 * why), and both machines apply both records from those bytes at each boot
 * of the session (system/arena.c pcArenaSessionParties: the same start as a
 * local party vs party), so a rematch is the same two parties, sent once.
 * Its start: power-on into field 22 at the story's start with the host's
 * party, the match asked for at once; the battle music, the opponent's HP
 * and the stage are the host's (the settings below). Each machine shows its
 * own player's side (the perspective: host 1, joiner 2).
 *
 * A match's end (xg-htn.35.142): when the versus battle is over (whoever
 * won), or a PLAYER1 VS PLAYER2 bout's victory screen is up, both players
 * choose Rematch or Leave (port/src/game/main/arena.c), each on their own
 * pad. The choice is pad input like any other: both games read the same two
 * pads' bytes at every vblank, so both decide at the same frame of the same
 * vblank, and what follows is the game's own. Both Rematch: the game's soft
 * reset on both sides at that frame, into the session's start again (its
 * options are still set: Battling's main menu, now its entry bytes asking
 * for the same two Gears straight away, port/src/game/battling/arena.c; or
 * field 22 with the formation asked for again, netplaySoftReset and
 * system/arena.c pcArenaSoftReset). The vblank count, the lockstep and the
 * state hash go on through the reset: no reconnect, no new handshake, and
 * both sides' states stay the same (a soft reset in a session from
 * Start+Select+L1+R1, which both games see too, does the same). Leave, by
 * either: the session ends ("over leave"), as below.
 *
 * The end of a session: one started from the F1 menu (XG_NETPLAY_STATUS)
 * quits, and the game that started it goes on (netplay_launch.c); one from
 * the command line goes to the title screen instead (its start's options
 * cleared, the settings the session held let go and the memory cards put
 * back in as the settings have them, the game's soft reset at the next
 * vblank; the stepped clock stays, held to real time: hw.c chose it at the
 * start), unless its window was closed.
 *
 * Settings: those that change the game (gameplay: difficulty, enemy and
 * reward multipliers, encounters, deathblows, retry, text speed and its
 * holding and auto-advance, the original-game fixes; the arena's party in
 * Gears, battle music, opponent's HP and the stage) are the host's on both sides: it sends them in the handshake
 * (SETTINGS, each range-checked), and both hold them for the session
 * (settingsPin: the settings menu shows them locked, a change is ignored,
 * nothing is written). Both also play without what one may have and the
 * other not: no memory cards (card1/card2 none), the game's own text and
 * battle voices (no other release's text or Japanese voices), and no free
 * camera (its heading is game state, its stick is not in the pads). Save states
 * are off in a session (savestate.c stateAvailable): a load on one side
 * alone would part the two games, and a state is the machine's whole memory,
 * not something to take from the other player.
 *
 * Started from the F1 menu (Extras > Battle arena > Online, netplay_launch.c),
 * this runs as a process of its own with XG_NETPLAY_STATUS=1: its state goes
 * to stdout as lines "XGNP <state> ...", which the menu reads: hosting <port>,
 * joining <host:port>, connected <address> <delay> (loading), ready (first
 * synchronized inputs), waiting, paused, back,
 * lost, resumed, moved <address>,
 * over <why> [vblank] (why: error nobody noanswer build region busy desync
 * disc left silent startingtimeout battling quit address leave; versus p1|p2|escaped: who
 * won; party vs party: noparty, partyrefused R ID (the other side refused
 * this side's party), peerparty R ID (this side refused the other's),
 * ownparty R ID (this side's own fails the check), stage (the host's stage
 * is not one of this game's list); R a card/party.h reason, ID the member's
 * character id or 255), rematch; and the host's refused party R ID (a
 * player's party refused: it waits on).
 *
 * Desyncs: every vblank each side hashes its state (rand()'s seed, gameState,
 * and in Battling both Gears' records) and sends the hash with its input; a
 * hash that differs from this side's for the same vblank stops the session
 * with the vblank it happened at. XG_NETPLAY_DESYNC_TEST=n flips a bit of
 * rand()'s seed at vblank n on this side (a check of the check);
 * XG_NETPLAY_TEST_BLACKOUT=n:ms sends nothing for ms milliseconds from vblank
 * n (a drop: the session waits, then goes on);
 * XG_NETPLAY_DUMP_AT=n:file writes the seed and gameState at vblank n (to
 * compare two sides: so the names' tails below were found);
 * XG_NETPLAY_TEST_UNCHECKED=1 sends this side's party record even where
 * its own check refuses it (as another build's might: the other side's
 * check is the one that counts);
 * XG_NETPLAY_HASH_LOG=file writes each vblank's hash ("vblank hash" lines:
 * the two sides' files are the same up to where either stopped). In party
 * vs party the hash also takes the two records (their hash, from the
 * handshake) and, in the battle, player 2's side as mirror.c keeps it
 * between its turns (pcVersusPartyHash): what gameState and the fighters do
 * not hold.
 *
 * The protocol, the handshake and the lockstep queue are netplay_core.c
 * (apart from the game, for the unit test); this file puts them to the game.
 *
 * Security: every datagram is one fixed 80-byte packet whose last 8 bytes
 * are a SipHash-2-4 tag keyed by the session code (both players type the same
 * code: anyone else's packets, and stray ones, are dropped); the host talks
 * to the first player who joins with the right code and build, nobody else;
 * what comes from the other side is pad bytes, hashes, the terms (the delay,
 * the progress, the mode and a versus battle's formation and party) and the
 * host's settings, each range-checked, and in party vs party the other
 * player's party record: game data, but only that record's bytes (at most
 * NP_PARTY_MAX, assembled only from the pages of the one record announced),
 * taken only once it passes xgPartyCheck, which bounds every value of it the
 * battle uses as an index, an offset or a count (card/party.c lists them);
 * never a file, a path or code. Each disc is the player's own. Nothing is
 * saved during a session. The other player learns your address. The code is
 * a word, not a strong password: it keeps strangers and stray traffic out, it
 * does not encrypt.
 */
#include <SDL3/SDL.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "main/game_state.h"
#include "battling/battling.h"
#include "battle/battle.h"
#include "pc_arena.h"
#include "cd/cd.h"
#include "card/card.h"
#include "card/party.h"
#include "arena.h"
#include "gauntlet.h"
#include "gameplay.h"
#include "hw.h"
#include "net.h"
#include "netplay.h"
#include "netplay_core.h"
#include "settings.h"
#include "speedup.h"
#include "system.h"
#if defined(__has_include)
#if __has_include("netplay_compat.h")
#include "netplay_compat.h" /* the build's (port/CMakeLists.txt netplay_compat) */
#endif
#endif
#ifndef XG_NETPLAY_COMPAT
#define XG_NETPLAY_COMPAT 0 /* no generated id (a checking tool's compile): the executable's hash */
#endif

int pcWideGetMode(void); /* include/pc_wide.h: 4 while Battling runs */
u32 portRandSeed(void);  /* libc.c */
void srand(u32 seed);
/* spu/output.c: a stall taken out of the clock (weak: the protocol's test links no sound output). */
extern void spuOutputClockGap(void) __attribute__((weak));
extern BattlingGear battlingGear1;
extern BattlingGear battlingGear2;
void softReboot(void); /* src/main/kernel.c: the game's soft reset */
int stateRestartAvailable(const char** why);          /* savestate.c (stateimage.h) */
void stateRequestRestart(int keep, void (*entry)(void));

#if VERSION_JP
#define REGION 'J'
#else
#define REGION 'U'
#endif
#define DEFAULT_PORT 7461
#define VERSUS_FIELD 22 /* a versus battle's start: a field the party walks in at the story's start */
#define WAIT_HELLO_NS 600000000000ull /* the host waits for a player this long */
#define WAIT_WELCOME_NS 60000000000ull

static struct {
    int active;
    int host;
    int synchronized; /* the first peer input has made it into the game */
    char code[64];
    int port;
    char joinTo[256];
    int hasPick, pickTable, pickConfig; /* joiner: --formation */
    int battlingOnly; /* joiner: --battling */
    NpTerms terms; /* the host's (from the arguments) */
    NpSession np;
    /* pacing */
    int paced;
    uint64_t wall0;
    double emu0;
    int seenBattling;
    int seenVersus;
    int64_t desyncTest;
    int status; /* XG_NETPLAY_STATUS=1: state lines on stdout */
#ifdef __ANDROID__
    FILE* statusFile;
#endif
    uint64_t lostShown; /* the connection lost: when the note was last put up */
    int64_t blackoutAt; /* XG_NETPLAY_TEST_BLACKOUT=<vblank>:<ms> */
    uint64_t blackoutNs;
    int after; /* a command-line session ended: the game plays on from the title, still held to real time */
    /* party vs party: this side's party (--save or XG_NETPLAY_PARTY), the
     * disc's code table the names are checked with, and both sides' once
     * connected (the host's first) */
    char save[64];
    int hasParty;
    XgPartyRecord party;
    char roster[ARENA_LOADOUT_CODE];
    uint8_t codes[XG_PARTY_CODE_TABLE];
    int haveCodes;
    XgPartyRecord sides[2];
    uint64_t sidesHash;
    int checkedIds[3]; /* the members of the record checked last (for the notes) */
    FILE* hashLog;     /* XG_NETPLAY_HASH_LOG */
} N;

void uiNetplayNotice(const char* text, int wait); /* ui/menu.cpp */
void gpuPresentMenu(const char* shot);           /* gpu/libgpu.c: the last picture again */

/* ---- the settings that change the game ---- */

enum { K_BOOL, K_INT, K_DIFFICULTY, K_TEXT_SPEED, K_STAGE };
#define STAGE_VALUES 75 /* arena_stage: stage n + 1, n one of the game's 75 (system/arena.c lists some) */
static struct {
    const char* key;
    int lo, hi, kind;
} logic[NP_SETTINGS_MAX];
static int logicCount;

static void logicAdd(NpSession* s, const char* key, int lo, int hi, int kind, int value) {
    int i = logicCount++;
    logic[i].key = key;
    logic[i].lo = lo;
    logic[i].hi = hi;
    logic[i].kind = kind;
    s->settingsLo[i] = (uint16_t)lo;
    s->settingsHi[i] = (uint16_t)hi;
    s->terms.settings[i] = (uint16_t)(value < lo ? lo : value > hi ? hi : value); /* the host's */
    s->settingsCount = logicCount;
}

/* Both sides make the list (the same build: the same list); the host's
 * values are what it sends. */
static void logicList(NpSession* s) {
    logicCount = 0;
    logicAdd(s, "speedup", 0, 1, K_BOOL, speedupEnabled() != 0);
    logicAdd(s, "original_bug_fixes", 0, 1, K_BOOL, pcGameplayOriginalBugFixes() != 0);
    logicAdd(s, "deus_camera_fix", 0, 1, K_BOOL, pcGameplayDeusCameraFix());
    logicAdd(s, "uninterrupted_combos", 0, 1, K_BOOL, pcGameplayUninterruptedCombos() != 0);
    logicAdd(s, "difficulty", 0, 1, K_DIFFICULTY, pcGameplayHardMode() != 0);
    for (int i = 0; i < GAMEPLAY_ENEMY_STAT_COUNT; i++) {
        logicAdd(s, pcGameplayEnemyKey(i), 25, 400, K_INT, pcGameplayEnemyMultiplier(i));
    }
    logicAdd(s, "encounter_rate", 0, 400, K_INT, pcGameplayEncounterRate());
    for (int i = 0; i < GAMEPLAY_REWARD_COUNT; i++) {
        logicAdd(s, pcGameplayRewardKey(i), 0, 1000, K_INT, pcGameplayRewardMultiplier(i));
    }
    logicAdd(s, "deathblow_learning", 0, 1000, K_INT, pcGameplayDeathblowLearning());
    logicAdd(s, "battle_retry", 0, 1, K_BOOL, pcGameplayBattleRetry() != 0);
    logicAdd(s, "text_speed", 0, 8, K_TEXT_SPEED, pcGameplayTextSpeed());
    logicAdd(s, "text_hold_fast", 0, 1, K_BOOL, pcGameplayTextHold() != 0);
    logicAdd(s, "text_auto_advance", 0, 10, K_INT, pcGameplayTextAutoAdvance());
    logicAdd(s, "arena_versus_gears", 0, 1, K_BOOL, pcVersusGearsSetting() != 0);
    /* the battle music: the song is game state (the driver and the SPU run in the game) */
    logicAdd(s, "arena_music", 0, ARENA_MUSIC_IDS, K_INT, pcArenaMusicSetting());
    /* the opponent's HP and EP in a versus battle: a picture only, but the match's
     * terms (both players see the same of each other) */
    logicAdd(s, "arena_opponent_hp", 0, 1, K_BOOL, pcVersusOpponentHpSetting() != 0);
    /* the stage of party vs party: one of the list's (the joiner checks it is) */
    logicAdd(s, "arena_stage", 1, STAGE_VALUES, K_STAGE, pcArenaStageSetting());
}

/* A stage setting's value is one of this game's list (system/arena.c). */
static int stageListed(int value) {
    for (int i = 0; i < pcArenaStageCount(); i++) {
        if (value == pcArenaStageId(i) + 1) return 1;
    }
    return 0;
}

/* The session's settings held on this side (both sides). */
static void logicPin(const NpTerms* t) {
    static const char* const fixed[][2] = {
        {"card1", "none"}, {"card2", "none"}, {"japanese_battle_voices", "0"}, {"japanese_cutscene_audio", "0"},
        /* The free camera writes its heading into the game's state (freecam.c: m10E_cameraTan,
         * m10C_camera2Tan; a field script reads it: a desync at vblank 669 in field 22), and its
         * right-stick turns are not in the pad bytes the sides exchange (xg-htn.35.139). */
        {"free_camera", "0"},
        {"text_dialogue", "disc"}, {"text_battle", "disc"}, {"text_menus", "disc"}, {"text_names", "disc"},
    };
    for (int i = 0; i < logicCount; i++) {
        char text[16];
        int v = t->settings[i];
        if (logic[i].kind == K_DIFFICULTY) SDL_snprintf(text, sizeof text, "%s", v ? "hard" : "normal");
        else if (logic[i].kind == K_TEXT_SPEED && v == 0) SDL_snprintf(text, sizeof text, "instant");
        else SDL_snprintf(text, sizeof text, "%d", v);
        settingsPin(logic[i].key, text);
    }
    for (size_t i = 0; i < sizeof fixed / sizeof fixed[0]; i++) settingsPin(fixed[i][0], fixed[i][1]);
}

static void setEnv(const char* name, const char* value);

static void say(const char* fmt, ...) {
    va_list ap;
    fputs("netplay: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* A state line for the menu that started this process (netplay_launch.c). */
static void status(const char* fmt, ...) {
    va_list ap;
    if (!N.status) return;
    FILE* out = stdout;
#ifdef __ANDROID__
    if (N.statusFile != NULL) out = N.statusFile;
#endif
    fputs("XGNP ", out);
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fputc('\n', out);
    fflush(out);
}

/* An end's word on the status line. */
static const char* endWord(const NpSession* s) {
    static const char* const words[] = {"error",  "error",   "nobody",       "noanswer", "build",
                                        "region", "busy",    "pick",         "desync",   "disc",
                                        "left",   "silent",  "noparty",      "partyrefused", "peerparty", "mode"};
    return s->reason >= 0 && s->reason < (int)(sizeof words / sizeof words[0]) ? words[s->reason] : "error";
}

/* A party's member at `place` (0-2) as a character id, 255 none. */
static int memberId(const int ids[3], int place) {
    return place >= 0 && place <= 2 && ids[place] >= 0 && ids[place] <= 10 ? ids[place] : 255;
}

static void statusEnd(const NpSession* s) {
    if (s->reason == NP_END_DESYNC) {
        status("over desync %u", s->endFrame);
    } else if (s->reason == NP_END_SILENT && !N.synchronized) {
        status("over startingtimeout");
    } else if (N.roster[0] && (s->reason == NP_END_PARTY_REFUSED ||
                              s->reason == NP_END_PEER_PARTY || s->reason == NP_END_NO_PARTY)) {
        status("over loadout");
    } else if (s->reason == NP_END_PARTY_REFUSED || s->reason == NP_END_PEER_PARTY) {
        /* the member: this side's own (its party refused), or the other's (refused here) */
        int own[3];
        for (int i = 0; i < 3; i++) own[i] = N.party.m4_members[i];
        status("over %s %d %d", endWord(s), s->partyReason,
               memberId(s->reason == NP_END_PARTY_REFUSED ? own : N.checkedIds, s->partyMember));
    } else {
        status("over %s", endWord(s));
    }
}

/* How a session ends of itself, the same on both sides (the BYE carries it:
 * the side that gets there first tells the other, a few vblanks behind). */
enum { END_NONE, END_BATTLING, END_VERSUS_P1, END_VERSUS_P2, END_VERSUS_ESCAPED, END_LEAVE, END_COUNT };
static const char* const endWords[END_COUNT] = {NULL, "battling", "versus p1", "versus p2", "versus escaped", "leave"};
static const char* const endTexts[END_COUNT] = {
    NULL, "Battling ended: the session is over", "the versus battle is over: player 1 won",
    "the versus battle is over: player 2 won", "the versus battle is over: player 1 escaped",
    "a player chose Leave: the session is over"};

/* A command-line session over: the game goes on from the title screen (the
 * header). The start's options cleared (an empty one is unset), the reset at
 * the next vblank (savestate.c), as the player's Start+Select+L1+R1. */
static void toTitle(void) {
    static const char* const options[] = {"XG_BOOT_MODE", "XG_FIELD", "XG_PARTY", "XG_VERSUS", "XG_GAMESTATE",
                                          "XG_MUSIC"};
    const char* why;
    for (size_t i = 0; i < sizeof options / sizeof options[0]; i++) setEnv(options[i], "");
    /* The session is over (nothing to keep together now): the player's own
     * settings and memory cards again (xg-htn.35.149). The title's game reads
     * the cards as newly inserted. */
    settingsUnpinAll();
    cardImagesReload();
    pcArenaSessionParties(NULL, NULL); /* the title's game is the player's own */
    N.after = 1;
    if (!stateRestartAvailable(&why)) {
        say("the game cannot go back to the title (%s): it ends", why);
        quitNow();
    }
    say("back to the title screen");
    stateRequestRestart(0, softReboot);
}

/* The session ends (`end`, or END_NONE: the protocol's reason): a message (a
 * box with a window), the other side told, then the program's own shut-down. */
static void stop(int end) {
    if (end == END_NONE && N.np.reason == NP_END_LEFT && N.np.peerEnd > 0 && N.np.peerEnd < END_COUNT) {
        end = N.np.peerEnd; /* the other side got to the same end first */
    }
    const char* text = end != END_NONE ? endTexts[end] : N.np.why;
    if (end == END_NONE && N.np.reason == NP_END_SILENT && !N.synchronized) {
        text = "The other player's game did not finish connecting.";
    }
    say("%s", text);
    if (end != END_NONE) status("over %s", endWords[end]);
    else statusEnd(&N.np);
    npBye(&N.np, end);
    N.active = 0;
    pcVersusSetPerspective(0);
    uiNetplayNotice(text, end != END_LEAVE || N.status);
    if (N.status) {
        quitNow(); /* the menu's session: the game that started it goes on */
    }
    toTitle();
}

/* port/src/system/arena.c: a player chose Leave at a match's end (both games
 * at the same frame: the header). */
void netplayLeave(void) {
    if (N.active) stop(END_LEAVE);
}

/* port/src/system/reset.c (through arena.c): the game's soft reset in a
 * session, a Rematch's or the players' own: the session's start again, not
 * its end. */
void netplaySoftReset(void) {
    if (!N.active) return;
    if (N.seenBattling || N.seenVersus) {
        say("rematch at vblank %u: the session's start again", hwVSyncCount());
        status("rematch");
    }
    N.seenBattling = 0;
    N.seenVersus = 0;
}

static void note(NpSession* s, int what, const char* text) {
    (void)s;
    switch (what) {
    case NP_NOTE_REFUSED: say("%s was refused (build, region, match type, formation, or party)", text); break;
    case NP_NOTE_REFUSED_PARTY:
        say("%s's party cannot be used (reason %d, member %d): refused; waiting on", text, N.np.partyReason,
            memberId(N.checkedIds, N.np.partyMember));
        status("refused party %d %d", N.np.partyReason, memberId(N.checkedIds, N.np.partyMember));
        break;
    case NP_NOTE_WAITING: say("waiting for the other player..."); status("waiting"); break;
    case NP_NOTE_PAUSED:
        if (N.synchronized) {
            say("the other player paused (the menu, or the game in the background)");
            status("paused");
        }
        N.lostShown = 0;
        break;
    case NP_NOTE_BACK:
        if (N.synchronized) {
            say("the other player is back");
            status("back");
        }
        uiNetplayNotice(NULL, 0);
        break;
    case NP_NOTE_LOST:
        if (N.synchronized) {
            say("the connection is lost: waiting for it to come back (up to %d s)", (int)(N.np.silentNs / 1000000000ull));
            status("lost");
        } else {
            say("starting the match: waiting for the other player's game to load");
        }
        N.lostShown = 0;
        break;
    case NP_NOTE_RESUMED:
        if (N.synchronized) {
            say("the connection is back: going on");
            status("resumed");
        }
        uiNetplayNotice(NULL, 0);
        break;
    case NP_NOTE_MOVED: say("the other player is at %s now", text); status("moved %s", text); break;
    }
}

/* While a vblank waits for the other side, keep the window responsive and
 * explain the wait. The first wait can be a slow start on the peer. */
static void pollQuit(void) {
    quitPoll(1);
    hwPausedFrame(); /* the stall watchdog: waiting here is no stall */
    uint64_t now = SDL_GetTicksNS();
    if ((N.np.lost || N.np.peerPaused) && now - N.lostShown > 1000000000ull) {
        N.lostShown = now;
        const char* notice = !N.synchronized ? "Starting the match. Waiting for the other player's game to load."
                             : N.np.lost ? "Connection lost. Waiting for the other player."
                                         : "The other player paused.";
        uiNetplayNotice(notice, 0);
        gpuPresentMenu(NULL);
    }
}

/* The program ends with the session still on (the window closed, Ctrl+C):
 * the other side told at once, not after NP_SILENT_NS of silence. */
static void byeAtExit(void) {
    if (N.active) {
        npBye(&N.np, END_NONE);
        status("over quit");
    }
}

/* ---- the session's start ---- */

static uint64_t hashFile(const char* path) {
    FILE* f = path != NULL ? fopen(path, "rb") : NULL;
    uint8_t buf[65536];
    size_t n;
    uint64_t h = NP_FNV0;
    if (f == NULL) return 0;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) h = npFnv(h, buf, n);
    fclose(f);
    return h;
}

uint64_t netplayCompatibilityId(void) {
    char path[4096];
    if (XG_NETPLAY_COMPAT != 0) return XG_NETPLAY_COMPAT;
    return netSelfPath(path, sizeof path) ? hashFile(path) : 0;
}

static void usage(void) {
    fprintf(stderr, "usage: xenogears --netplay host --code WORD [--port P] [--delay N] [--progress N | --all-gears]\n"
                    "                 [--versus TABLE,CONFIG|joiner [--party A,B,C]]\n"
                    "                 [--party-vs-party --save CARD.NUMBER[.ID...]] [disc.cue]\n"
                    "       xenogears --netplay join HOST:PORT --code WORD [--battling | --formation TABLE,CONFIG]\n"
                    "                 [--save CARD.NUMBER[.ID...]] [disc.cue]\n");
}

int netplayArgs(int* argc, char*** argv) {
    char** a = *argv;
    int n = *argc;
    int i = 2;

    if (n < 2 || SDL_strcmp(a[1], "--netplay") != 0) return 0;
    if (n < 3 || (SDL_strcmp(a[2], "host") != 0 && SDL_strcmp(a[2], "join") != 0)) {
        usage();
        return -1;
    }
    N.host = SDL_strcmp(a[2], "host") == 0;
    N.port = DEFAULT_PORT;
    N.terms.delay = 3;
    N.terms.progress = 0;
    N.terms.mode = NP_MODE_BATTLING;
    N.terms.party[0] = 0; /* Fei, Citan */
    N.terms.party[1] = 2;
    N.terms.party[2] = 255;
    i = 3;
    if (!N.host) {
        if (n < 4) {
            usage();
            return -1;
        }
        SDL_strlcpy(N.joinTo, a[3], sizeof N.joinTo);
        i = 4;
    }
    for (; i < n && a[i][0] == '-'; i++) {
        const char* v = i + 1 < n ? a[i + 1] : NULL;
        if (SDL_strcmp(a[i], "--code") == 0 && v != NULL) {
            SDL_strlcpy(N.code, v, sizeof N.code);
            i++;
        } else if (SDL_strcmp(a[i], "--port") == 0 && v != NULL && N.host) {
            N.port = atoi(v);
            i++;
        } else if (SDL_strcmp(a[i], "--delay") == 0 && v != NULL && N.host) {
            N.terms.delay = atoi(v);
            i++;
        } else if (SDL_strcmp(a[i], "--progress") == 0 && v != NULL && N.host) {
            N.terms.progress = atoi(v);
            i++;
        } else if (SDL_strcmp(a[i], "--versus") == 0 && v != NULL && N.host) {
            N.terms.mode = NP_MODE_VERSUS;
            N.terms.pick = SDL_strcmp(v, "joiner") == 0; /* player 2 chooses the formation it commands */
            if (N.terms.pick) N.terms.table = N.terms.config = 0;
            else if (SDL_sscanf(v, "%d,%d", &N.terms.table, &N.terms.config) != 2) N.terms.table = -1;
            i++;
        } else if (SDL_strcmp(a[i], "--battling") == 0 && !N.host) {
            N.battlingOnly = 1;
        } else if (SDL_strcmp(a[i], "--formation") == 0 && v != NULL && !N.host) {
            N.hasPick = SDL_sscanf(v, "%d,%d", &N.pickTable, &N.pickConfig) == 2 && N.pickTable >= 0 &&
                        N.pickTable < NP_TABLES && N.pickConfig >= 0 && N.pickConfig <= 15;
            if (!N.hasPick) {
                usage();
                return -1;
            }
            i++;
        } else if (SDL_strcmp(a[i], "--party") == 0 && v != NULL && N.host) {
            int got = SDL_sscanf(v, "%d,%d,%d", &N.terms.party[0], &N.terms.party[1], &N.terms.party[2]);
            for (int k = got < 0 ? 0 : got; k < 3; k++) N.terms.party[k] = 255;
            i++;
        } else if (SDL_strcmp(a[i], "--all-gears") == 0 && N.host) {
            N.terms.progress = 1000; /* past every unlock level, Argento's 999 too */
        } else if (SDL_strcmp(a[i], "--party-vs-party") == 0 && N.host) {
            N.terms.mode = NP_MODE_PARTY;
        } else if (SDL_strcmp(a[i], "--save") == 0 && v != NULL) {
            SDL_strlcpy(N.save, v, sizeof N.save);
            i++;
        } else {
            usage();
            return -1;
        }
    }
    if (N.code[0] == '\0' && SDL_getenv("XG_NETPLAY_CODE") != NULL) { /* from the menu (netplay_launch.c) */
        SDL_strlcpy(N.code, SDL_getenv("XG_NETPLAY_CODE"), sizeof N.code);
    }
    uint8_t check[NP_PACKET] = {0};
    NpTerms checked;
    npPutTerms(check, &N.terms);
    if (!npGetTerms(check, &checked)) { /* what the joiner would refuse */
        usage();
        return -1;
    }
    if (N.code[0] == '\0' || N.port <= 0 || N.port > 65535 || N.terms.delay < 1 || N.terms.delay > 15 ||
        N.terms.progress < 0 || N.terms.progress > 1000) {
        usage();
        return -1;
    }
    /* what is left: the program's name and the disc, as main() expects */
    a[i - 1] = a[0];
    *argv = a + i - 1;
    *argc = n - (i - 1);
    return 1;
}

/* The session's options, for both kinds of reader: the C library's getenv
 * (hw.c, options.c) and SDL_getenv (SDL's own copy, taken at its first use).
 * On Windows SDL_setenv_unsafe changes the process's block, not the C
 * runtime's copy that getenv reads: _putenv_s changes both. */
static void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    SDL_setenv_unsafe(name, value, 1);
#endif
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value, true);
}

/* netplay_core's check of a record from the other side (and the one this
 * side checks of its own): xgPartyCheck, with this disc's code table. */
static int checkParty(NpSession* s, const uint8_t* record, int bytes, int* member) {
    XgPartyRecord r;
    XgPartyWhy why;
    *member = -1;
    for (int i = 0; i < 3; i++) N.checkedIds[i] = 255;
    if (bytes != (int)sizeof r) {
        say("a party record of %d bytes, not %d: refused", bytes, (int)sizeof r);
        return XG_PARTY_BAD_HEADER;
    }
    memcpy(&r, record, sizeof r);
    for (int i = 0; i < 3; i++) N.checkedIds[i] = r.m4_members[i];
    int reason = xgPartyCheck(&r, N.haveCodes ? N.codes : NULL, &why);
    if (reason != XG_PARTY_OK) {
        *member = why.member;
        say("a party refused: %s", why.text);
    } else if (!s->host && s->terms.mode == NP_MODE_VERSUS) {
        for (int i = 0; i < 3; i++) {
            if (s->terms.party[i] != r.m4_members[i]) {
                *member = i;
                return XG_PARTY_BAD_MEMBERS;
            }
        }
    }
    return reason;
}

/* XG_NETPLAY_PARTY: a record's bytes in hex (the menu's, netplay_launch.c). */
static int partyFromHex(const char* hex, XgPartyRecord* out) {
    uint8_t* b = (uint8_t*)out;
    if (SDL_strlen(hex) != 2 * sizeof *out) return 0;
    for (size_t i = 0; i < sizeof *out; i++) {
        unsigned v;
        char pair[3] = {hex[2 * i], hex[2 * i + 1], 0};
        if (!SDL_isxdigit((unsigned char)pair[0]) || !SDL_isxdigit((unsigned char)pair[1]) ||
            SDL_sscanf(pair, "%2x", &v) != 1) {
            return 0;
        }
        b[i] = (uint8_t)v;
    }
    return 1;
}

/* This side's party, from the menu (XG_NETPLAY_PARTY) or --save (this
 * machine's cards, read now, put away again once the session holds its
 * settings: *cardsRead), checked as the other side's will be. 0 (said, and
 * the status line) where there is one but it cannot be used. */
static int ownParty(int* cardsRead) {
    const char* hex = SDL_getenv("XG_NETPLAY_PARTY");
    *cardsRead = 0;
    if ((hex == NULL || hex[0] == '\0') && N.save[0] == '\0') return 1; /* none brought */
    if (hex != NULL && hex[0] != '\0') {
        if (!partyFromHex(hex, &N.party)) {
            say("XG_NETPLAY_PARTY is not a party record's %d bytes in hex", (int)sizeof N.party);
            status("over ownparty %d 255", XG_PARTY_BAD_HEADER);
            return 0;
        }
    } else {
        cardImagesInit(); /* the player's cards as the settings have them, before the session's pins */
        *cardsRead = 1;
        if (!pcArenaPackSave(N.save, N.haveCodes ? N.codes : NULL, &N.party)) {
            say("--save %s: no party from that save", N.save);
            status("over ownparty %d 255", XG_PARTY_BAD_SAVE);
            return 0;
        }
    }
    int member = -1;
    int reason = checkParty(&N.np, (const uint8_t*)&N.party, (int)sizeof N.party, &member);
    const char* unchecked = SDL_getenv("XG_NETPLAY_TEST_UNCHECKED");
    if (reason != XG_PARTY_OK && unchecked != NULL && atoi(unchecked) != 0) {
        say("test: this side's party goes out unchecked (XG_NETPLAY_TEST_UNCHECKED), as another build's might");
    } else if (reason != XG_PARTY_OK) {
        say("this side's party cannot be used");
        status("over ownparty %d %d", reason, memberId(N.checkedIds, member));
        return 0;
    }
    N.hasParty = 1;
    say("this side's party: %d member(s) %d,%d,%d, record fnv1a64 %016llx", N.party.m3_count, N.party.m4_members[0],
        N.party.m4_members[1], N.party.m4_members[2],
        (unsigned long long)npPartyHash((const uint8_t*)&N.party, (int)sizeof N.party));
    return 1;
}

static int checkRosterAgreement(NpSession* session, const uint8_t* data, int bytes, int* member) {
    *member = -1;
    return bytes != session->partyBytes || memcmp(data, session->party, (size_t)bytes) != 0;
}

int netplayConnect(const char* disc) {
    NpSession* s = &N.np;
    int cardsRead = 0;
    int approvedParty[3];

    const char* st = SDL_getenv("XG_NETPLAY_STATUS");
    N.status = st != NULL && atoi(st) != 0;
#ifdef __ANDROID__
    const char* path = SDL_getenv("XG_NETPLAY_STATUS_FILE");
    if (N.status && path != NULL) N.statusFile = fopen(path, "a");
#endif
#ifndef _WIN32
    if (N.status) signal(SIGPIPE, SIG_IGN); /* the menu's end of the pipe gone: no end of this session */
#endif
    const char* approved = SDL_getenv("XG_NETPLAY_ROSTER");
    if (approved != NULL && approved[0] != 0) {
        ArenaLoadout roster;
        const char* why = "The approved loadout code is invalid.";
        if (!pcArenaLoadoutDecode(approved, &roster, approvedParty) ||
            !pcArenaLoadoutEncode(&roster, approvedParty, N.roster) || !pcArenaRosterOnline(&roster, disc, &why)) {
            say("%s", why);
            status("over loadout");
            return 0;
        }
        if (N.host) {
            N.terms.mode = NP_MODE_ROSTER;
            N.terms.table = 2;
            N.terms.config = 13;
            N.terms.pick = 0;
            for (int i = 0; i < 3; i++) N.terms.party[i] = approvedParty[i];
        }
    }
    npInit(s, N.host, N.code, netplayCompatibilityId(), REGION);
    say("this game's compatibility id: %016llx", (unsigned long long)s->compatId);
    s->note = note;
    s->poll = NULL; /* no window yet */
    s->terms = N.terms;
    s->battlingOnly = N.battlingOnly;
    s->hasPick = N.hasPick;
    s->pickTable = N.pickTable;
    s->pickConfig = N.pickConfig;
    s->partyCheck = N.roster[0] ? checkRosterAgreement : checkParty;
    N.haveCodes = xgPartyImageCodes(disc, N.codes);
    if (!ownParty(&cardsRead)) return 0;
    if (N.host && N.terms.mode == NP_MODE_PARTY && !N.hasParty) {
        say("party vs party needs this side's party: --save CARD.NUMBER[.ID...] (or the menu's)");
        status("over ownparty %d 255", XG_PARTY_BAD_MEMBERS);
        return 0;
    }
    if (N.roster[0]) {
        uint8_t packet[ARENA_ROSTER_PACKET];
        int bytes = pcArenaRosterPack(N.roster, N.hasParty ? &N.party : NULL, packet);
        if (N.hasParty) {
            char code[ARENA_LOADOUT_CODE];
            XgPartyRecord record;
            if (!pcArenaRosterUnpack(packet, bytes, N.haveCodes ? N.codes : NULL, code, &record)) {
                status("over loadout");
                return 0;
            }
        }
        npSetParty(s, packet, bytes);
    } else if (N.hasParty) {
        npSetParty(s, (const uint8_t*)&N.party, (int)sizeof N.party);
        if (N.host && s->terms.mode == NP_MODE_VERSUS) {
            for (int i = 0; i < 3; i++) s->terms.party[i] = N.party.m4_members[i];
        }
    }
    logicList(s);
    if (!npOpen(s, N.host ? N.port : 0)) {
        say("%s", s->why);
        statusEnd(s);
        return 0;
    }
    if (N.host) {
        say("hosting on UDP port %d (player 1); waiting for player 2 with the same code", netLocalPort(s->s));
        status("hosting %d", netLocalPort(s->s));
        if (!npHost(s, WAIT_HELLO_NS)) {
            say("%s", s->why);
            statusEnd(s);
            return 0;
        }
        char text[64];
        netAddrText(&s->peer, text, sizeof text);
        say("player 2 is %s", text);
    } else {
        NetAddr host;
        if (!netResolveHostPort(N.joinTo, &host)) {
            say("%s", netError());
            status("over address");
            return 0;
        }
        say("joining %s as player 2", N.joinTo);
        status("joining %s", N.joinTo);
        if (!npJoin(s, &host, WAIT_WELCOME_NS)) {
            say("%s: %s", N.joinTo, s->why);
            statusEnd(s);
            return 0;
        }
    }
    if ((s->terms.mode == NP_MODE_ROSTER) != (N.roster[0] != 0)) {
        status("over loadout");
        npBye(s, END_NONE);
        return 0;
    }
    if (s->terms.mode == NP_MODE_ROSTER) {
        s->terms.table = 2;
        s->terms.config = 13;
        for (int i = 0; i < 3; i++) s->terms.party[i] = approvedParty[i];
        say("both players approved the same enemy loadout");
    }
    say("connected: input delay %d vblanks, story progress %d", s->terms.delay, s->terms.progress);
    for (int i = 0; i < logicCount; i++) {
        if (logic[i].kind == K_STAGE && !stageListed(s->terms.settings[i])) { /* within range, but not one of ours */
            say("the host's stage (setting %d) is not one of this game's", s->terms.settings[i]);
            status("over stage");
            npBye(s, END_NONE);
            return 0;
        }
    }
    if (s->terms.mode == NP_MODE_PARTY) {
        /* both checked (the handshake: each side's of the other's, here of its own) */
        const XgPartyRecord* mine = &N.party;
        XgPartyRecord theirs;
        memcpy(&theirs, s->peerParty, sizeof theirs);
        N.sides[0] = N.host ? *mine : theirs;
        N.sides[1] = N.host ? theirs : *mine;
        N.sidesHash = npFnv(npFnv(NP_FNV0, &N.sides[0], sizeof N.sides[0]), &N.sides[1], sizeof N.sides[1]);
        pcArenaSessionParties(&N.sides[0], &N.sides[1]);
        say("party vs party: player 1 %d,%d,%d (record fnv1a64 %016llx), player 2 %d,%d,%d (%016llx); the other "
            "player's party is from their own save, not verified (only its ranges checked)",
            N.sides[0].m4_members[0], N.sides[0].m4_members[1], N.sides[0].m4_members[2],
            (unsigned long long)npPartyHash((const uint8_t*)&N.sides[0], (int)sizeof N.sides[0]),
            N.sides[1].m4_members[0], N.sides[1].m4_members[1], N.sides[1].m4_members[2],
            (unsigned long long)npPartyHash((const uint8_t*)&N.sides[1], (int)sizeof N.sides[1]));
    } else if (s->terms.mode == NP_MODE_ROSTER && N.hasParty) {
        pcArenaSessionVersus(ARENA_MATCH_ROSTER, s->terms.table, s->terms.config, &N.party);
    } else if (s->terms.mode == NP_MODE_VERSUS && (N.host ? N.hasParty : s->peerPartyBytes != 0)) {
        XgPartyRecord record = N.party;
        if (!N.host) memcpy(&record, s->peerParty, sizeof record);
        pcArenaSessionVersus(ARENA_MATCH_VERSUS, s->terms.table, s->terms.config, &record);
    }
    {
        char peer[64];
        netAddrText(&s->peer, peer, sizeof peer);
        status("connected %s %d %s", peer, s->terms.delay,
               s->terms.mode == NP_MODE_PARTY    ? "party"
               : (s->terms.mode == NP_MODE_VERSUS || s->terms.mode == NP_MODE_ROSTER) ? "versus"
                                                 : "battling");
    }
    logicPin(&s->terms);
    if (cardsRead) cardImagesReload(); /* --save read the player's cards: the session's game has none */
    char held[1024];
    size_t used = 0;
    held[0] = '\0';
    for (int i = 0; i < logicCount && used < sizeof held; i++) {
        used += (size_t)SDL_snprintf(held + used, sizeof held - used, " %s=%s", logic[i].key, settingsGet(logic[i].key));
    }
    say("the settings that change the game are the host's for the session:%s", held);
    /* the session's game, on the stepped clock: power-on into Battling's main
     * menu, or into a field with the versus battle asked for */
    char value[64];
    setEnv("XG_CLOCK", "step");
    setEnv("XG_MUSIC", ""); /* a start's music (options.c) reads the disc: one side's alone would part them */
    if (s->terms.mode == NP_MODE_PARTY) {
        /* field 22 (the boot into the field); the match's start itself is
         * system/arena.c's (pcArenaStartState: the session's parties) */
        SDL_snprintf(value, sizeof value, "%d", VERSUS_FIELD);
        setEnv("XG_FIELD", value);
        setEnv("XG_PARTY", "");
        setEnv("XG_VERSUS", "");
        setEnv("XG_GAMESTATE", "");
    } else if (s->terms.mode == NP_MODE_VERSUS || s->terms.mode == NP_MODE_ROSTER) {
        say("a versus battle: table %d config %d%s, party %d,%d,%d", s->terms.table, s->terms.config,
            N.host && s->terms.pick ? " (player 2's choice)" : "", s->terms.party[0],
            s->terms.party[1], s->terms.party[2]);
        SDL_snprintf(value, sizeof value, "%d", VERSUS_FIELD);
        setEnv("XG_FIELD", value);
        SDL_snprintf(value, sizeof value, "%d,%d,%d", s->terms.party[0], s->terms.party[1], s->terms.party[2]);
        setEnv("XG_PARTY", value);
        SDL_snprintf(value, sizeof value, "%d,%d", s->terms.table, s->terms.config);
        setEnv("XG_VERSUS", value);
        setEnv("XG_GAMESTATE", "0x1930=0");
    } else {
        setEnv("XG_BOOT_MODE", "4");
        SDL_snprintf(value, sizeof value, "0x1930=%d", s->terms.progress); /* field variable 0, the story progress */
        setEnv("XG_GAMESTATE", value);
    }
    const char* t = SDL_getenv("XG_NETPLAY_DESYNC_TEST");
    N.desyncTest = t != NULL ? atol(t) : -1;
    unsigned at = 0, ms = 0;
    t = SDL_getenv("XG_NETPLAY_TEST_BLACKOUT");
    N.blackoutAt = t != NULL && SDL_sscanf(t, "%u:%u", &at, &ms) == 2 ? (int64_t)at : -1;
    N.blackoutNs = (uint64_t)ms * 1000000ull;
    t = SDL_getenv("XG_NETPLAY_HASH_LOG");
    if (t != NULL && t[0] != '\0') N.hashLog = fopen(t, "w");
    s->poll = pollQuit;
    N.active = 1;
    /* each machine shows its own player's side of a mirror match (system/arena.c) */
    pcVersusSetPerspective(N.host ? 1 : 2);
    atexit(byeAtExit);
    return 1;
}

int netplayActive(void) {
    return N.active;
}

/* What would change this side's game alone (the extras' starts, a gauntlet,
 * the arena, a memory card): refused in a session, with a note. */
int netplayRefuses(const char* what) {
    if (!N.active) return 0;
    say("%s: not during an online session", what);
    uiNetplayNotice("Unavailable during an online session.", 0);
    return 1;
}

/* ---- every vblank ---- */

/* A Battling Gear's fight, without its host pointers (models, moves, the
 * ordering table's links: they differ between a Linux and a Windows build). */
static uint64_t gearHash(uint64_t h, const BattlingGear* g) {
#define FIELD(f) h = npFnv(h, &g->f, sizeof g->f)
    FIELD(m0_position);
    FIELD(m10_velocity);
    FIELD(m20_knockback);
    FIELD(m30_lungeVelocity);
    FIELD(m40_moveSpeed);
    FIELD(m44_lungeSpeed);
    FIELD(m48_speed);
    FIELD(m4C_animation);
    FIELD(m50_animFrame);
    FIELD(m54_rotY);
    FIELD(m58_targetRotation);
    FIELD(mB4_hp);
    FIELD(mB6_gauge);
    FIELD(mBA_damage);
    FIELD(mBC_maxHp);
    FIELD(mC4_moveState);
    FIELD(mF2_wins);
    FIELD(m644_moveDamage);
    FIELD(m970_hitDamage);
    FIELD(m9C3_move);
#undef FIELD
    return h;
}

/* What both games must agree on, every vblank: rand()'s seed and gameState
 * (the PS1's layout: no host pointers), and the fight: Battling's two Gears,
 * a battle's fighters (sBattleEntity: stats, no pointers). */
static uint64_t stateHash(void) {
    u32 seed = portRandSeed();
    uint64_t h = npFnv(NP_FNV0, &seed, sizeof seed);
    /* Hash each name through its terminator. The unused suffix is never
     * read by the game: loadInitialGameState seeds it with measured retail
     * leftovers, while decodeCharacterNames seeds it with zeros after card
     * operations. Those paths may leave different suffixes for the same name. */
    for (int i = 0; i < 31; i++) {
        const u8* name = gameState.m0_names[i];
        size_t n = 0;
        while (n < sizeof gameState.m0_names[i] && name[n] != 0) n++;
        h = npFnv(h, name, n < sizeof gameState.m0_names[i] ? n + 1 : n);
    }
    h = npFnv(h, (const u8*)&gameState + sizeof gameState.m0_names, sizeof gameState - sizeof gameState.m0_names);
    if (pcWideGetMode() == 4) {
        h = gearHash(h, &battlingGear1);
        h = gearHash(h, &battlingGear2);
    } else if (pcWideGetMode() == 2) {
        h = npFnv(h, &battleEntities.m0_entities, sizeof battleEntities.m0_entities);
        if (N.np.terms.mode == NP_MODE_PARTY) {
            u32 p2 = pcVersusPartyHash(); /* player 2's side between its turns (mirror.c) */
            h = npFnv(h, &p2, sizeof p2);
        }
        if (N.np.terms.mode == NP_MODE_ROSTER) {
            for (int side = 0; side < 2; side++) {
                const u8* counts = pcArenaRosterItemCounts(side);
                if (counts != NULL) h = npFnv(h, counts, ARENA_ROSTER_ITEMS);
            }
        }
    }
    if (N.np.terms.mode == NP_MODE_PARTY) h = npFnv(h, &N.sidesHash, sizeof N.sidesHash); /* the two records */
    return h;
}

static uint64_t discHash(void) {
    uint8_t sector[CD_RAW_SECTOR];
    int count = cdDiscSectorCount();
    uint64_t h = npFnv(NP_FNV0, &count, sizeof count);
    for (int lba = 0x18; lba < 0x29; lba++) { /* the file table (16 sectors) and the directory table */
        if (cdReadRawSector(lba, sector)) h = npFnv(h, sector + 24, 2048);
    }
    return h;
}

/* Each vblank held back to real time (the stepped clock alone would run as
 * fast as the machine can). */
static void paceToRealTime(void) {
    uint64_t wall = SDL_GetTicksNS();
    double emu = hwNow();
    if (!N.paced) {
        N.paced = 1;
        N.wall0 = wall;
        N.emu0 = emu;
        return;
    }
    double ahead = (emu - N.emu0) - (double)(wall - N.wall0) * 1e-9;
    if (ahead > 0.0005) {
        SDL_DelayPrecise((Uint64)(ahead * 1e9));
    } else if (ahead < -0.1) {
        N.wall0 = wall - (uint64_t)((emu - N.emu0) * 1e9); /* after a stall: on from now, no rush to catch up */
        /* So nothing that comes is late: the sound output must not skip what
         * the device's silence through the stall "owed" (spu/output.c). The
         * game never catches up here, so that debt would grow as fast as the
         * sound arrives and the session would stay silent (xg-htn.35.145). */
        if (spuOutputClockGap) spuOutputClockGap();
    }
}

void netplayPace(void) {
    pcArenaVBlank(); /* the Battle arena's matches (system/arena.c) */
    pcGauntletVBlank();
    if (!N.active) {
        if (N.after) paceToRealTime(); /* a command-line session's game after it */
        netplayLaunchWait(); /* a session started from the menu: this game waits */
        return;
    }
    int mode = pcWideGetMode();
    if (N.np.terms.mode == NP_MODE_VERSUS || N.np.terms.mode == NP_MODE_PARTY || N.np.terms.mode == NP_MODE_ROSTER) {
        int versus = pcVersusSession();
        if (versus == PC_VERSUS_IN) {
            N.seenVersus = 1;
        } else if (N.seenVersus && versus == PC_VERSUS_IDLE) {
            int r = pcVersusLastResult();
            stop(r == 0x81 ? END_VERSUS_P2 : (r & 0x40) ? END_VERSUS_ESCAPED : END_VERSUS_P1);
            return;
        }
    } else if (mode == 4) {
        N.seenBattling = 1;
    } else if (N.seenBattling) {
        stop(END_BATTLING);
        return;
    }
    paceToRealTime();
}

void netplayIdle(void) {
    if (!N.active) return;
    npIdle(&N.np);
    if (N.np.over) stop(END_NONE);
}

/* XG_NETPLAY_DUMP_AT=<vblank>:<file>: rand()'s seed (4 bytes) and gameState
 * as they are at that vblank, to compare two sides after a desync. */
static void dumpState(uint32_t n) {
    static int parsed;
    static unsigned at;
    static char path[512];
    if (!parsed) {
        const char* d = SDL_getenv("XG_NETPLAY_DUMP_AT");
        parsed = 1;
        if (d == NULL || SDL_sscanf(d, "%u:%511s", &at, path) != 2) path[0] = '\0';
    }
    if (path[0] != '\0' && at == n) {
        FILE* f = fopen(path, "wb");
        u32 seed = portRandSeed();
        if (f != NULL) {
            fwrite(&seed, sizeof seed, 1, f);
            fwrite(&gameState, sizeof gameState, 1, f);
            fclose(f);
        }
    }
}

void netplayFrame(unsigned local, unsigned* port1, unsigned* port2) {
    uint32_t n = hwVSyncCount();

    if (!N.np.started) {
        N.np.discHash = discHash();
    }
    if (N.blackoutAt >= 0 && (int64_t)n == N.blackoutAt) {
        say("test: nothing goes out for %llu ms (XG_NETPLAY_TEST_BLACKOUT)", (unsigned long long)(N.blackoutNs / 1000000));
        npTestBlackout(&N.np, N.blackoutNs);
    }
    if (N.desyncTest >= 0 && (int64_t)n == N.desyncTest) {
        srand(portRandSeed() ^ 1); /* XG_NETPLAY_DESYNC_TEST: a deliberate difference */
    }
    dumpState(n);
    uint64_t h = stateHash();
    if (N.hashLog != NULL) {
        fprintf(N.hashLog, "%u %016llx\n", n, (unsigned long long)h);
        if (n % 64 == 0) fflush(N.hashLog);
    }
    if (!npFrame(&N.np, n, local, h, port1, port2)) {
        if (N.hashLog != NULL) fflush(N.hashLog);
        stop(END_NONE);
    } else if (!N.synchronized && n - N.np.base >= (uint32_t)N.np.terms.delay) {
        N.synchronized = 1;
        say("both games are ready");
        status("ready");
        if (N.np.peerPaused) status("paused");
    }
}
