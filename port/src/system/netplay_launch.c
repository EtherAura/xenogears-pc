/*
 * A netplay session started from the F1 menu (Extras > Battle arena >
 * Online, port/src/ui/arena.cpp): this program again, as a process of its own
 * (`--netplay host|join ...`, netplay.c), in its own window from power-on.
 * Its state comes back on its stdout (XG_NETPLAY_STATUS=1: "XGNP ..." lines);
 * its log goes where this one's goes. The session code goes to it in its
 * environment (XG_NETPLAY_CODE), not on its command line, which other users
 * of the machine can read; so does party vs party's party record, packed
 * here from the save chosen in the menu (XG_NETPLAY_PARTY, its bytes in hex:
 * the session's process has no memory cards). The disc is the player's own
 * (settings "disc", else the image in the drive).
 *
 * The session's process gets this one's environment but for what belongs to
 * this run alone (a pad or menu script, a quit vblank, dumps, save-state
 * harness: XG_PAD_SCRIPT, XG_PAD_SCRIPT2, XG_UI_SCRIPT, XG_QUIT_AT,
 * XG_DUMP_FRAMES, XG_DUMP_RANGE, XG_AUDIO_DUMP, XG_STATE_SAVE, XG_STATE_LOAD,
 * XG_STATE_EVERY, XG_STATE_RELOAD); XG_NETPLAY_CHILD_<NAME>=value gives it
 * <NAME>=value (tests).
 *
 * Meanwhile the local game waits (netplayLaunchWait, from the vblank): its
 * clock and sound paused as for the menu, F1 still opens the menu (to see the
 * session or end it), and when the session's process ends the game goes on.
 * Closing this window ends the session's process too.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ANDROID__
#include "android.h"
#include "engine.h"
static char sessionPath[4096];
static FILE* sessionOutput;
static int sessionReturned;
#endif

#include "common.h"
#include "main/game_state.h"
#include "arena.h"
#include "cd/cd.h"
#include "hw.h"
#include "net.h"
#include "netplay.h"
#include "netplay_core.h"
#include "settings.h"
#include "spu/output.h"
#include "system.h"
#include "ui/ui.h"

static struct {
    SDL_Process* proc;
    char* introPath;
    int state;
    char detail[256];
    int delay;
    int paused;
    int lost;
    int refusedReason, refusedMember; /* host, party vs party: the last party refused (reason 0: none) */
    char line[512];
    size_t used;
} L;

static void removeIntroPicture(void) {
    if (L.introPath == NULL) return;
    SDL_RemovePath(L.introPath);
    SDL_free(L.introPath);
    L.introPath = NULL;
}

static void copy(char* out, size_t size, const char* text) {
    SDL_strlcpy(out, text, size);
}


/* The custom roster lobby uses the same authenticated record exchange as a
 * match. Its socket closes before the session process begins. While it runs,
 * both players stay in their own game menus, including on Android. */
static struct {
    SDL_Thread* worker;
    SDL_Mutex* lock;
    SDL_AtomicInt cancelled;
    NetplayStart start;
    char proposal[ARENA_LOADOUT_CODE];
    XgPartyRecord reviewRecord;
    char detail[200];
    uint8_t codes[XG_PARTY_CODE_TABLE];
    int haveCodes;
    int phase, confirmed;
} R;

static int rosterLobbyCancelled(void) {
    return SDL_GetAtomicInt(&R.cancelled);
}

/* Called with the lobby locked. */
static void setRosterReview(const char* roster, const XgPartyRecord* record) {
    if (strcmp(R.proposal, roster) != 0 || memcmp(&R.reviewRecord, record, sizeof *record) != 0) {
        copy(R.proposal, sizeof R.proposal, roster);
        R.reviewRecord = *record;
        R.confirmed = 0;
    }
    R.phase = NETPLAY_ROSTER_REVIEW;
}

/* The joiner proposes enemy teams and rules. Player 1's save and inventory
 * are supplied only by the host and are part of the final review. */
static int rosterLobbyCheck(NpSession* session, const uint8_t* data, int bytes, int* member) {
    char roster[ARENA_LOADOUT_CODE];
    XgPartyRecord party;
    ArenaLoadout loadout;
    int ids[3];
    *member = -1;

    if (!session->host) {
        char resolved[ARENA_LOADOUT_CODE];
        ArenaLoadout proposal;
        if (!pcArenaRosterUnpack(data, bytes, R.haveCodes ? R.codes : NULL, roster, &party) ||
            !pcArenaLoadoutDecode(roster, &loadout, ids) ||
            !pcArenaLoadoutDecode(R.start.roster, &proposal, ids)) return 1;
        memset(loadout.side[0].items, 0, sizeof loadout.side[0].items);
        return !pcArenaLoadoutEncode(&loadout, ids, resolved) ||
               strcmp(resolved, R.start.roster) != 0;
    }
    if (bytes <= 0 || bytes > ARENA_LOADOUT_CODE || data[bytes - 1] != 0 ||
        strlen((const char*)data) + 1 != (size_t)bytes ||
        !pcArenaLoadoutDecode((const char*)data, &loadout, ids)) return 1;

    SDL_LockMutex(R.lock);
    memset(&party, 0, sizeof party);
    if (loadout.side[0].count == 0) {
        if (!R.start.hasParty) {
            copy(R.detail, sizeof R.detail, "Choose your save before hosting a match with a character party.");
            R.phase = NETPLAY_ROSTER_FAILED;
            SDL_UnlockMutex(R.lock);
            return XG_PARTY_BAD_MEMBERS;
        }
        party = R.start.record;
        for (int i = 0; i < 3; i++) ids[i] = party.m4_members[i];
    } else {
        memcpy(loadout.side[0].items, R.start.rosterHostItems, sizeof loadout.side[0].items);
    }
    if (!pcArenaLoadoutEncode(&loadout, ids, roster)) {
        SDL_UnlockMutex(R.lock);
        return 1;
    }
    setRosterReview(roster, &party);
    int confirmed = R.confirmed;
    SDL_UnlockMutex(R.lock);
    if (!confirmed) return NP_RECORD_PENDING;
    uint8_t final[ARENA_ROSTER_PACKET];
    int length = pcArenaRosterPack(roster, party.m3_count != 0 ? &party : NULL, final);
    return !npSetParty(session, final, length);
}

static int reviewFinalRoster(NpSession* session) {
    const uint8_t* data = session->host ? session->party : session->peerParty;
    int bytes = session->host ? session->partyBytes : session->peerPartyBytes;
    char roster[ARENA_LOADOUT_CODE];
    XgPartyRecord party;
    if (!pcArenaRosterUnpack(data, bytes, R.haveCodes ? R.codes : NULL, roster, &party)) return 0;

    SDL_LockMutex(R.lock);
    setRosterReview(roster, &party);
    SDL_UnlockMutex(R.lock);

    uint64_t until = SDL_GetTicksNS() + 300000000000ull;
    uint32_t bothReady = 0;
    for (uint32_t frame = 1; !rosterLobbyCancelled() && SDL_GetTicksNS() < until; frame++) {
        SDL_LockMutex(R.lock);
        unsigned ready = R.confirmed != 0;
        SDL_UnlockMutex(R.lock);
        unsigned p1, p2;
        if (!npFrame(session, frame, ready, 0, &p1, &p2)) return 0;
        if (bothReady == 0 && p1 && p2) bothReady = frame;
        if (bothReady != 0 && frame == bothReady + 2) {
            SDL_LockMutex(R.lock);
            copy(R.start.roster, sizeof R.start.roster, roster);
            R.start.record = party;
            R.start.hasParty = party.m3_count != 0;
            SDL_UnlockMutex(R.lock);
            return 1;
        }
        SDL_Delay(50);
    }
    return 0;
}

static int rosterLobbyRun(void* unused) {
    (void)unused;
    NetplayStart start = R.start;
    char lobbyCode[sizeof start.code + sizeof "roster review:"];
    SDL_snprintf(lobbyCode, sizeof lobbyCode, "roster review:%s", start.code);
    NpSession session;
#if VERSION_JP
    const int region = 'J';
#else
    const int region = 'U';
#endif
    npInit(&session, start.host, lobbyCode, netplayCompatibilityId(), region);
    session.terms.mode = NP_MODE_ROSTER;
    session.terms.delay = 1;
    session.terms.table = 2;
    session.terms.config = 13;
    session.terms.party[0] = 0;
    session.terms.party[1] = 2;
    session.terms.party[2] = 255;
    session.partyCheck = rosterLobbyCheck;
    session.reviewRoster = start.host;
    session.cancel = rosterLobbyCancelled;
    session.silentNs = 5000000000ull;

    const char* failure = NULL;
    NetAddr address;
    int agreed = 0;
    if (!npOpen(&session, start.host ? start.port : 0)) {
        failure = session.why;
    } else {
        const uint64_t wait = 300000000000ull;
        if (!start.host) {
            npSetParty(&session, (const uint8_t*)start.roster, (int)strlen(start.roster) + 1);
        }
        if (start.host) agreed = npHost(&session, wait);
        else if (netResolveHostPort(start.address, &address)) agreed = npJoin(&session, &address, wait);
        else failure = netError();
        if (agreed && session.terms.mode == NP_MODE_ROSTER) {
            agreed = reviewFinalRoster(&session);
            if (agreed) {
                const uint64_t until = SDL_GetTicksNS() + 500000000ull;
                while (!rosterLobbyCancelled() && SDL_GetTicksNS() < until && !session.over) {
                    npIdle(&session);
                    SDL_Delay(25);
                }
                agreed = !session.over;
            }
        } else {
            agreed = 0;
        }
        if (failure == NULL) failure = session.why;
        npClose(&session);
    }

    SDL_LockMutex(R.lock);
    if (agreed && !rosterLobbyCancelled()) {
        R.phase = NETPLAY_ROSTER_READY;
    } else {
        if (!R.detail[0]) copy(R.detail, sizeof R.detail, failure != NULL && *failure ? failure : "The lobby could not complete.");
        R.phase = NETPLAY_ROSTER_FAILED;
    }
    SDL_UnlockMutex(R.lock);
    return 0;
}

void netplayRosterLobbyStop(void) {
    if (R.worker == NULL) return;
    SDL_SetAtomicInt(&R.cancelled, 1);
    SDL_WaitThread(R.worker, NULL);
    R.worker = NULL;
    SDL_DestroyMutex(R.lock);
    R.lock = NULL;
}

int netplayRosterLobbyStart(const NetplayStart* start) {
    netplayRosterLobbyStop();
    R.lock = SDL_CreateMutex();
    if (R.lock == NULL) return 0;
    R.start = *start;
    R.haveCodes = xgPartyGameCodes(R.codes);
    R.proposal[0] = R.detail[0] = 0;
    R.phase = NETPLAY_ROSTER_WAITING;
    R.confirmed = 0;
    SDL_SetAtomicInt(&R.cancelled, 0);
    R.worker = SDL_CreateThread(rosterLobbyRun, "PvP roster lobby", NULL);
    if (R.worker != NULL) return 1;
    SDL_DestroyMutex(R.lock);
    R.lock = NULL;
    return 0;
}

int netplayRosterLobbyState(NetplayStart* start, char* proposal, char* detail, size_t detailSize) {
    if (R.worker == NULL) return NETPLAY_ROSTER_IDLE;
    SDL_LockMutex(R.lock);
    int phase = R.phase;
    *start = R.start;
    copy(proposal, ARENA_LOADOUT_CODE, R.proposal);
    copy(detail, detailSize, R.detail);
    if (phase == NETPLAY_ROSTER_REVIEW) {
        start->record = R.reviewRecord;
        if (R.confirmed) phase = NETPLAY_ROSTER_WAITING;
    }
    SDL_UnlockMutex(R.lock);
    return phase;
}

void netplayRosterLobbyConfirm(const char* proposal, const XgPartyRecord* party) {
    if (R.worker == NULL) return;
    SDL_LockMutex(R.lock);
    if (R.phase == NETPLAY_ROSTER_REVIEW && strcmp(R.proposal, proposal) == 0 &&
        memcmp(&R.reviewRecord, party, sizeof *party) == 0) R.confirmed = 1;
    SDL_UnlockMutex(R.lock);
}

static void parse(const char* line) {
    if (SDL_strncmp(line, "XGNP ", 5) != 0) return; /* the game's own output */
    line += 5;
#ifdef __ANDROID__
    if (SDL_strcmp(line, "activity returned") == 0) {
        sessionReturned = 1;
        return;
    }
#endif
    if (SDL_strncmp(line, "hosting ", 8) == 0) {
        L.state = NETPLAY_HOSTING;
        copy(L.detail, sizeof L.detail, line + 8);
    } else if (SDL_strncmp(line, "joining ", 8) == 0) {
        L.state = NETPLAY_JOINING;
        copy(L.detail, sizeof L.detail, line + 8);
    } else if (SDL_strncmp(line, "connected ", 10) == 0) {
        char peer[64] = "";
        int delay = 0;
        if (sscanf(line + 10, "%63s %d", peer, &delay) >= 1) {
            L.state = NETPLAY_LOADING;
            copy(L.detail, sizeof L.detail, peer);
            L.delay = delay;
        }
    } else if (SDL_strcmp(line, "ready") == 0 && L.state == NETPLAY_LOADING) {
        L.state = NETPLAY_CONNECTED;
    } else if (SDL_strcmp(line, "paused") == 0) {
        L.paused = 1;
    } else if (SDL_strcmp(line, "back") == 0) {
        L.paused = 0;
    } else if (SDL_strcmp(line, "lost") == 0) {
        L.lost = 1;
    } else if (SDL_strcmp(line, "resumed") == 0) {
        L.lost = 0;
    } else if (SDL_strncmp(line, "refused party ", 14) == 0) {
        int reason = 0, member = 255;
        if (sscanf(line + 14, "%d %d", &reason, &member) >= 1 && reason > 0) {
            L.refusedReason = reason;
            L.refusedMember = member;
        }
    } else if (SDL_strncmp(line, "moved ", 6) == 0 &&
               (L.state == NETPLAY_LOADING || L.state == NETPLAY_CONNECTED)) {
        copy(L.detail, sizeof L.detail, line + 6);
    } else if (SDL_strncmp(line, "over ", 5) == 0) {
        L.state = NETPLAY_OVER;
        L.paused = 0;
        L.lost = 0;
        copy(L.detail, sizeof L.detail, line + 5);
    }
}

static void outputBytes(const char* buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            L.line[L.used] = '\0';
            if (L.used > 0) parse(L.line);
            L.used = 0;
        } else if (L.used + 1 < sizeof L.line) {
            L.line[L.used++] = buf[i];
        }
    }
}

static void readOutput(void) {
    SDL_IOStream* out = SDL_GetProcessOutput(L.proc);
    char buf[256];
    size_t n;
    while (out != NULL && (n = SDL_ReadIO(out, buf, sizeof buf)) > 0) {
        outputBytes(buf, n);
    }
}

/* The process's news; when it has ended, its end. */
static void checkProcess(void) {
#ifdef __ANDROID__
    if (sessionOutput != NULL) {
        char buf[256];
        size_t n;
        clearerr(sessionOutput); /* the Activity may have appended more */
        while ((n = fread(buf, 1, sizeof buf, sessionOutput)) > 0) outputBytes(buf, n);
        if (sessionReturned || ferror(sessionOutput)) {
            fclose(sessionOutput);
            sessionOutput = NULL;
            SDL_RemovePath(sessionPath);
            removeIntroPicture();
            if (L.state != NETPLAY_OVER) {
                L.state = NETPLAY_OVER;
                copy(L.detail, sizeof L.detail, "exit");
            }
        }
        return;
    }
#endif
    int code = 0;
    if (L.proc == NULL) return;
    readOutput();
    if (SDL_WaitProcess(L.proc, false, &code)) {
        readOutput();
        if (L.state != NETPLAY_OVER) {
            L.state = NETPLAY_OVER;
            SDL_snprintf(L.detail, sizeof L.detail, "exit %d", code);
        }
        fprintf(stderr, "netplay: the session's process ended (%s)\n", L.detail);
        SDL_DestroyProcess(L.proc);
        L.proc = NULL;
        removeIntroPicture();
    }
}

static void stopAtExit(void) {
#ifdef __ANDROID__
    if (sessionOutput != NULL) androidActivityAction("stopNetplay");
#endif
    if (L.proc != NULL) SDL_KillProcess(L.proc, false); /* it tells the other player itself */
    removeIntroPicture();
}

int netplayLaunch(const NetplayStart* o) {
    static int atExit;
    char port[16], delay[16], progress[16], versus[32], party[32];
    static char hex[2 * sizeof(XgPartyRecord) + 1];
    const char* args[24];
    int n = 0;

    checkProcess();
#ifdef __ANDROID__
    if (sessionOutput != NULL) return 0;
    args[n++] = "xenogears";
#else
    char exe[4096];
    if (L.proc != NULL || !netSelfPath(exe, sizeof exe)) return 0;
    args[n++] = exe;
#endif
    args[n++] = "--netplay";
    if (o->host) {
        SDL_snprintf(port, sizeof port, "%d", o->port);
        SDL_snprintf(delay, sizeof delay, "%d", o->delay);
        SDL_snprintf(progress, sizeof progress, "%d", o->progress);
        args[n++] = "host";
        args[n++] = "--port";
        args[n++] = port;
        args[n++] = "--delay";
        args[n++] = delay;
        args[n++] = "--progress";
        args[n++] = progress;
        if (o->partyVsParty) {
            args[n++] = "--party-vs-party";
        } else if (o->versus) {
            if (o->joinerPicks) SDL_snprintf(versus, sizeof versus, "joiner");
            else SDL_snprintf(versus, sizeof versus, "%d,%d", o->table, o->config);
            SDL_snprintf(party, sizeof party, "%d,%d,%d", o->party[0], o->party[1], o->party[2]);
            args[n++] = "--versus";
            args[n++] = versus;
            args[n++] = "--party";
            args[n++] = party;
        }
    } else {
        args[n++] = "join";
        args[n++] = o->address;
        if (o->battlingOnly) args[n++] = "--battling";
        if (o->pick) {
            SDL_snprintf(versus, sizeof versus, "%d,%d", o->table, o->config);
            args[n++] = "--formation";
            args[n++] = versus;
        }
    }
#ifndef __ANDROID__
    const char* disc = settingsGet("disc");
    if ((disc == NULL || disc[0] == '\0') && cdSwapPath(0) != NULL) args[n++] = cdSwapPath(0);
#endif
    L.introPath = pcArenaWriteMenuPicture();
#ifdef __ANDROID__
    if (L.introPath != NULL) {
        args[n++] = "--netplay-intro-file";
        args[n++] = L.introPath;
    }
#endif
    args[n] = NULL;
    hex[0] = 0;
    if (o->hasParty) {
        const uint8_t* b = (const uint8_t*)&o->record;
        for (size_t i = 0; i < sizeof o->record; i++) SDL_snprintf(hex + 2 * i, 3, "%02x", b[i]);
    }

#ifdef __ANDROID__
    const char* options[] = {o->code, o->roster, hex, engineName(engineThis())};
#ifdef VERSION_JP
    const char* region = "jp";
#else
    const char* region = "us";
#endif
    sessionReturned = 0;
    if (androidStartNetplay(&args[1], options, region, sessionPath, sizeof sessionPath)) {
        sessionOutput = fopen(sessionPath, "r");
        if (sessionOutput == NULL) androidActivityAction("stopNetplay");
    }
#else
    SDL_Environment* env = SDL_CreateEnvironment(true);
    SDL_PropertiesID props = SDL_CreateProperties();
    if (env == NULL || props == 0) {
        if (env != NULL) SDL_DestroyEnvironment(env);
        if (props != 0) SDL_DestroyProperties(props);
        removeIntroPicture();
        return 0;
    }
    static const char* const mine[] = {"XG_PAD_SCRIPT",  "XG_PAD_SCRIPT2", "XG_UI_SCRIPT",  "XG_QUIT_AT",
                                       "XG_DUMP_FRAMES", "XG_DUMP_RANGE",  "XG_AUDIO_DUMP", "XG_STATE_SAVE",
                                       "XG_STATE_LOAD",  "XG_STATE_EVERY", "XG_STATE_RELOAD", "XG_INVARIANT_LOG"};
    for (size_t i = 0; i < sizeof mine / sizeof mine[0]; i++) SDL_UnsetEnvironmentVariable(env, mine[i]);
    char** vars = SDL_GetEnvironmentVariables(env);
    for (int i = 0; vars != NULL && vars[i] != NULL; i++) {
        static const char prefix[] = "XG_NETPLAY_CHILD_";
        char* eq = SDL_strchr(vars[i], '=');
        if (eq != NULL && SDL_strncmp(vars[i], prefix, sizeof prefix - 1) == 0) {
            *eq = '\0';
            SDL_SetEnvironmentVariable(env, vars[i] + sizeof prefix - 1, eq + 1, true);
        }
    }
    SDL_free(vars);
    SDL_SetEnvironmentVariable(env, "XG_NETPLAY_STATUS", "1", true);
    SDL_SetEnvironmentVariable(env, "XG_ENGINE_LAUNCHED", "1", true); /* this engine, whatever the setting says */
    SDL_SetEnvironmentVariable(env, "XG_NETPLAY_CODE", o->code, true);
    if (L.introPath != NULL) SDL_SetEnvironmentVariable(env, "XG_NETPLAY_INTRO_FILE", L.introPath, true);
    else SDL_UnsetEnvironmentVariable(env, "XG_NETPLAY_INTRO_FILE");
    if (o->roster[0] != 0) SDL_SetEnvironmentVariable(env, "XG_NETPLAY_ROSTER", o->roster, true);
    else SDL_UnsetEnvironmentVariable(env, "XG_NETPLAY_ROSTER");

    if (o->hasParty) {
        SDL_SetEnvironmentVariable(env, "XG_NETPLAY_PARTY", hex, true);
    } else {
        SDL_UnsetEnvironmentVariable(env, "XG_NETPLAY_PARTY");
    }
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)args);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_INHERITED);
    L.proc = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
#endif
    memset(L.detail, 0, sizeof L.detail);
    L.used = 0;
    L.paused = 0;
    L.lost = 0;
    L.delay = 0;
    L.refusedReason = 0;
    L.refusedMember = 255;
#ifdef __ANDROID__
    if (sessionOutput == NULL) {
        removeIntroPicture();
        L.state = NETPLAY_OVER;
        copy(L.detail, sizeof L.detail, "error");
        return 0;
    }
#else
    if (L.proc == NULL) {
        removeIntroPicture();
        L.state = NETPLAY_OVER;
        SDL_snprintf(L.detail, sizeof L.detail, "start %s", SDL_GetError());
        fprintf(stderr, "netplay: the session's process did not start: %s\n", SDL_GetError());
        return 0;
    }
#endif
    L.state = NETPLAY_STARTING;
    fprintf(stderr, "netplay: started a session's process (%s)\n", o->host ? "host" : "join");
    if (!atExit) {
        atExit = 1;
        atexit(stopAtExit);
    }
    return 1;
}

int netplaySessionState(void) {
    checkProcess();
    return L.state;
}

const char* netplaySessionDetail(void) {
    return L.detail;
}

int netplaySessionDelay(void) {
    return L.delay;
}

int netplaySessionPaused(void) {
    return L.paused;
}

int netplaySessionLost(void) {
    return L.lost;
}

int netplaySessionRefusedParty(int* reason, int* member) {
    if (L.refusedReason <= 0) return 0;
    *reason = L.refusedReason;
    *member = L.refusedMember;
    return 1;
}

void netplaySessionStop(void) {
#ifdef __ANDROID__
    if (sessionOutput != NULL) androidActivityAction("stopNetplay");
#endif
    if (L.proc != NULL) SDL_KillProcess(L.proc, false);
}

int netplaySessionStory(void) {
    int v = gameState.m1930_fieldVarsBackup[0]; /* field variable 0: Battling's unlock levels follow it */
    return v < 0 ? 0 : v > 1000 ? 1000 : v;
}

/* From the vblank (netplay.c netplayPace): while a session's process runs,
 * the game here waits, as in the settings menu. */
void netplayLaunchWait(void) {
    checkProcess();
    if (L.proc == NULL) return;
    fprintf(stderr, "netplay: the game waits while the online session runs\n");
    while (L.proc != NULL) {
        hwPause(1);
        spuOutputPause(1);
        padPumpEvents(); /* the window: F1, closing */
        uiVBlank();      /* F1: the menu, with the session's state */
        hwPausedFrame();
        checkProcess();
        SDL_Delay(20);
    }
    spuOutputPause(0);
    hwPause(0);
    fprintf(stderr, "netplay: the session is over (%s); the game goes on\n", L.detail);
}
