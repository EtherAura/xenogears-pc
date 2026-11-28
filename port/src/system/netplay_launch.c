/* The two-player loadout review and the menu state for an online match. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "main/game_state.h"
#include "arena.h"
#include "cd/cd.h"
#include "hw.h"
#include "net.h"
#include "netplay.h"
#include "netplay_core.h"
#include "pad.h"
#include "settings.h"
#include "spu/output.h"
#include "system.h"
#include "ui/ui.h"

static struct {
    SDL_Mutex* lock;
    int state;
    char detail[256];
    int delay;
    int paused;
    int lost;
    int refusedReason, refusedMember; /* host, party vs party: the last party refused (reason 0: none) */
} L;

static void copy(char* out, size_t size, const char* text) {
    SDL_strlcpy(out, text, size);
}


/* The custom roster lobby uses the same authenticated record exchange as a
 * match. Its socket closes before the battle handshake begins. While it runs,
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
        char resolved[ARENA_LOADOUT_CODE], requested[ARENA_LOADOUT_CODE];
        ArenaLoadout proposal;
        if (!pcArenaRosterUnpack(data, bytes, R.haveCodes ? R.codes : NULL, roster, &party) ||
            !pcArenaLoadoutDecode(roster, &loadout, ids) ||
            !pcArenaLoadoutDecode(R.start.roster, &proposal, ids)) return 1;
        memset(loadout.side[0].items, 0, sizeof loadout.side[0].items);
        return !pcArenaLoadoutEncode(&loadout, ids, resolved) || !pcArenaLoadoutEncode(&proposal, ids, requested) ||
               strcmp(resolved, requested) != 0;
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

void netplayMenuStatus(const char* line) {
    SDL_LockMutex(L.lock);
    parse(line);
    SDL_UnlockMutex(L.lock);
}

int netplayLaunch(const NetplayStart* o) {
    if (!L.lock) L.lock = SDL_CreateMutex();
    if (!L.lock) return 0;
    SDL_LockMutex(L.lock);
    if (L.state != NETPLAY_IDLE && L.state != NETPLAY_OVER) {
        SDL_UnlockMutex(L.lock);
        return 0;
    }
    L.state = NETPLAY_STARTING;
    L.detail[0] = 0;
    L.paused = L.lost = L.refusedReason = L.delay = 0;
    SDL_UnlockMutex(L.lock);
    if (netplayMenuConnect(o)) return 1;
    if (netplaySessionState() != NETPLAY_OVER) netplayMenuStatus("over error");
    return 0;
}

int netplaySessionState(void) {
    if (!L.lock) return NETPLAY_IDLE;
    SDL_LockMutex(L.lock);
    int state = L.state;
    SDL_UnlockMutex(L.lock);
    return state;
}

const char* netplaySessionDetail(void) {
    static char text[sizeof L.detail];
    if (!L.lock) return "";
    SDL_LockMutex(L.lock);
    copy(text, sizeof text, L.detail);
    SDL_UnlockMutex(L.lock);
    return text;
}

int netplaySessionDelay(void) { return L.delay; }
int netplaySessionPaused(void) { return L.paused; }
int netplaySessionLost(void) { return L.lost; }

int netplaySessionRefusedParty(int* reason, int* member) {
    if (!L.lock) return 0;
    SDL_LockMutex(L.lock);
    *reason = L.refusedReason;
    *member = L.refusedMember;
    SDL_UnlockMutex(L.lock);
    return *reason > 0;
}

void netplaySessionStop(void) { netplayMenuCancel(); }

int netplaySessionStory(void) {
    int v = gameState.m1930_fieldVarsBackup[0]; /* field variable 0: Battling's unlock levels follow it */
    return v < 0 ? 0 : v > 1000 ? 1000 : v;
}
