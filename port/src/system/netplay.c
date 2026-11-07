/*
 * Netplay (xg-htn.35.16, online, stage 1): two players on two machines (or
 * two processes), Battling's PLAYER1 VS PLAYER2 in input-delay lockstep.
 *
 *   xenogears --netplay host --code WORD [--port P] [--delay N]
 *             [--progress N | --all-gears] [--versus TABLE,CONFIG|joiner [--party A,B,C]]
 *             [disc.cue]
 *   xenogears --netplay join HOST:PORT --code WORD [--formation TABLE,CONFIG] [disc.cue]
 *   (the code also from XG_NETPLAY_CODE)
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
 * host's (the settings below). When the versus battle is over (whoever won),
 * the session ends.
 *
 * Settings: those that change the game (gameplay: difficulty, enemy and
 * reward multipliers, encounters, deathblows, retry, text speed and its
 * holding and auto-advance, the original-game fixes; the arena's party in
 * Gears) are the host's on both sides: it sends them in the handshake
 * (SETTINGS, each range-checked), and both hold them for the session
 * (settingsPin: the settings menu shows them locked, a change is ignored,
 * nothing is written). Both also play without what one may have and the
 * other not: no memory cards (card1/card2 none), the game's own text and
 * battle voices (no other release's text or Japanese voices). Save states
 * are off in a session (savestate.c stateAvailable): a load on one side
 * alone would part the two games, and a state is the machine's whole memory,
 * not something to take from the other player.
 *
 * Started from the F1 menu (Extras > Battle arena > Online, netplay_launch.c),
 * this runs as a process of its own with XG_NETPLAY_STATUS=1: its state goes
 * to stdout as lines "XGNP <state> ...", which the menu reads: hosting <port>,
 * joining <host:port>, connected <address> <delay>, waiting, paused, back,
 * lost, resumed, moved <address>,
 * over <why> [vblank] (why: error nobody noanswer build region busy desync
 * disc left silent battling quit address; versus p1|p2|escaped: who won).
 *
 * Desyncs: every vblank each side hashes its state (rand()'s seed, gameState,
 * and in Battling both Gears' records) and sends the hash with its input; a
 * hash that differs from this side's for the same vblank stops the session
 * with the vblank it happened at. XG_NETPLAY_DESYNC_TEST=n flips a bit of
 * rand()'s seed at vblank n on this side (a check of the check);
 * XG_NETPLAY_TEST_BLACKOUT=n:ms sends nothing for ms milliseconds from vblank
 * n (a drop: the session waits, then goes on);
 * XG_NETPLAY_DUMP_AT=n:file writes the seed and gameState at vblank n (to
 * compare two sides: so the names' tails below were found).
 *
 * The protocol, the handshake and the lockstep queue are netplay_core.c
 * (apart from the game, for the unit test); this file puts them to the game.
 *
 * Security: every datagram is one fixed 64-byte packet whose last 8 bytes
 * are a SipHash-2-4 tag keyed by the session code (both players type the same
 * code: anyone else's packets, and stray ones, are dropped); the host talks
 * to the first player who joins with the right code and build, nobody else;
 * what comes from the other side is only pad bytes, hashes, the delay and
 * the progress (both range-checked): never a file, a path, code or game data.
 * Each disc is the player's own. Nothing is saved during a session. The other
 * player learns your address. The code is a word, not a strong password: it
 * keeps strangers and stray traffic out, it does not encrypt.
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
#include "arena.h"
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
extern BattlingGear battlingGear1;
extern BattlingGear battlingGear2;

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
    char code[64];
    int port;
    char joinTo[256];
    int hasPick, pickTable, pickConfig; /* joiner: --formation */
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
    uint64_t lostShown; /* the connection lost: when the note was last put up */
    int64_t blackoutAt; /* XG_NETPLAY_TEST_BLACKOUT=<vblank>:<ms> */
    uint64_t blackoutNs;
} N;

void presentShowNote(const char* text); /* gpu/present.c */
void gpuPresentMenu(const char* shot);  /* gpu/libgpu.c: the last picture again */

/* ---- the settings that change the game ---- */

enum { K_BOOL, K_INT, K_DIFFICULTY, K_TEXT_SPEED };
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
}

/* The session's settings held on this side (both sides). */
static void logicPin(const NpTerms* t) {
    static const char* const fixed[][2] = {
        {"card1", "none"}, {"card2", "none"}, {"japanese_battle_voices", "0"}, {"japanese_cutscene_audio", "0"},
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
    fputs("XGNP ", stdout);
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

/* An end's word on the status line. */
static const char* endWord(const NpSession* s) {
    static const char* const words[] = {"error", "error", "nobody", "noanswer", "build", "region",
                                        "busy",  "pick",  "desync", "disc",     "left",  "silent"};
    return s->reason >= 0 && s->reason < (int)(sizeof words / sizeof words[0]) ? words[s->reason] : "error";
}

static void statusEnd(const NpSession* s) {
    if (s->reason == NP_END_DESYNC) status("over desync %u", s->endFrame);
    else status("over %s", endWord(s));
}

/* How a session ends of itself, the same on both sides (the BYE carries it:
 * the side that gets there first tells the other, a few vblanks behind). */
enum { END_NONE, END_BATTLING, END_VERSUS_P1, END_VERSUS_P2, END_VERSUS_ESCAPED, END_COUNT };
static const char* const endWords[END_COUNT] = {NULL, "battling", "versus p1", "versus p2", "versus escaped"};
static const char* const endTexts[END_COUNT] = {
    NULL, "Battling ended: the session is over", "the versus battle is over: player 1 won",
    "the versus battle is over: player 2 won", "the versus battle is over: player 1 escaped"};

/* The session ends (`end`, or END_NONE: the protocol's reason): a message (a
 * box with a window), the other side told, then the program's own shut-down. */
static void stop(int end) {
    if (end == END_NONE && N.np.reason == NP_END_LEFT && N.np.peerEnd > 0 && N.np.peerEnd < END_COUNT) {
        end = N.np.peerEnd; /* the other side got to the same end first */
    }
    const char* text = end != END_NONE ? endTexts[end] : N.np.why;
    say("%s", text);
    if (end != END_NONE) status("over %s", endWords[end]);
    else statusEnd(&N.np);
    npBye(&N.np, end);
    N.active = 0;
    if (SDL_getenv("XG_HEADLESS") == NULL) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Xenogears netplay", text, NULL);
    }
    quitNow();
}

static void note(NpSession* s, int what, const char* text) {
    (void)s;
    switch (what) {
    case NP_NOTE_REFUSED: say("%s has another build or region: refused", text); break;
    case NP_NOTE_WAITING: say("waiting for the other player..."); status("waiting"); break;
    case NP_NOTE_PAUSED: say("the other player paused (the menu, or the game in the background)"); status("paused"); break;
    case NP_NOTE_BACK: say("the other player is back"); status("back"); break;
    case NP_NOTE_LOST:
        say("the connection is lost: waiting for it to come back (up to %d s)", (int)(N.np.silentNs / 1000000000ull));
        status("lost");
        N.lostShown = 0;
        break;
    case NP_NOTE_RESUMED: say("the connection is back: going on"); status("resumed"); break;
    case NP_NOTE_MOVED: say("the other player is at %s now", text); status("moved %s", text); break;
    }
}

/* While a vblank waits for the other side: the window can still close, the
 * stall watchdog knows the wait is meant, and with the connection lost, a
 * note on the picture says so. */
static void pollQuit(void) {
    quitPoll(1);
    hwPausedFrame(); /* the stall watchdog: waiting here is no stall */
    uint64_t now = SDL_GetTicksNS();
    if (N.np.lost && now - N.lostShown > 1000000000ull) {
        N.lostShown = now;
        presentShowNote("Connection lost: waiting for the other player to come back");
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

static uint64_t compatId(void) {
    char path[4096];
    if (XG_NETPLAY_COMPAT != 0) return XG_NETPLAY_COMPAT;
    return netSelfPath(path, sizeof path) ? hashFile(path) : 0;
}

static void usage(void) {
    fprintf(stderr, "usage: xenogears --netplay host --code WORD [--port P] [--delay N] [--progress N | --all-gears]\n"
                    "                 [--versus TABLE,CONFIG|joiner [--party A,B,C]] [disc.cue]\n"
                    "       xenogears --netplay join HOST:PORT --code WORD [--formation TABLE,CONFIG] [disc.cue]\n");
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

int netplayConnect(void) {
    NpSession* s = &N.np;

    const char* st = SDL_getenv("XG_NETPLAY_STATUS");
    N.status = st != NULL && atoi(st) != 0;
#ifndef _WIN32
    if (N.status) signal(SIGPIPE, SIG_IGN); /* the menu's end of the pipe gone: no end of this session */
#endif
    npInit(s, N.host, N.code, compatId(), REGION);
    say("this game's compatibility id: %016llx", (unsigned long long)s->compatId);
    s->note = note;
    s->poll = NULL; /* no window yet */
    s->terms = N.terms;
    s->hasPick = N.hasPick;
    s->pickTable = N.pickTable;
    s->pickConfig = N.pickConfig;
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
    say("connected: input delay %d vblanks, story progress %d", s->terms.delay, s->terms.progress);
    {
        char peer[64];
        netAddrText(&s->peer, peer, sizeof peer);
        status("connected %s %d %s", peer, s->terms.delay, s->terms.mode == NP_MODE_VERSUS ? "versus" : "battling");
    }
    logicPin(&s->terms);
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
    if (s->terms.mode == NP_MODE_VERSUS) {
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
    s->poll = pollQuit;
    N.active = 1;
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
    presentShowNote("Not during an online session");
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
    }
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

void netplayPace(void) {
    if (!N.active) {
        netplayLaunchWait(); /* a session started from the menu: this game waits */
        return;
    }
    int mode = pcWideGetMode();
    if (N.np.terms.mode == NP_MODE_VERSUS) {
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
    }
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
    if (!npFrame(&N.np, n, local, stateHash(), port1, port2)) {
        stop(END_NONE);
    }
}
