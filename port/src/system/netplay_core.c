/*
 * Netplay's protocol and lockstep (netplay_core.h), apart from the game.
 *
 * A packet is 80 bytes, little-endian: 0 magic "XGNP", 4 version, 5 kind,
 * 72 the tag (SipHash-2-4 of bytes 0..71, keyed from the session code), the
 * rest by kind:
 *   HELLO    8 the build's compatibility id, 16 a nonce, 24 the region ('U'/'J'),
 *            25 a formation chosen (1), 26 its table (16 bits), 28 its config
 *   WELCOME  8 the build's compatibility id, 16 the joiner's nonce, 24 the input delay,
 *            25 a refusal (NP_REFUSE_*), 26 the story progress (16 bits),
 *            28 the mode, 29 the party (3 ids), 32 the table (16 bits),
 *            34 the config
 *   INPUT    8 the first frame f, 12 how many (1..8), 13 paused (1: sent
 *            from a pause, not a vblank), 16 the first frame of the other
 *            side's inputs this side lacks, 20 a vblank, 24 the state's hash
 *            at it, 32 the disc's hash, 40 the inputs for frames f.. (16 bits
 *            each), 56 the session's token
 *   BYE      8 why (the program's code: a session's own end, 0 none), 16 the
 *            session's token
 *   SETTINGS 8 the joiner's nonce, 16 the page, 17 the pages, 18 the
 *            settings' count, 20 18 values (16 bits each): the host's
 *            settings that change the game, one page of them
 * Anything else (another size, magic, version or kind, a wrong tag, another
 * address than the other player's) is dropped unread; but an INPUT or BYE
 * with the session's token (the joiner's nonce) is the other player at a new
 * address (a NAT's new port, another network), who is followed there, and a
 * HELLO from someone else during a session gets "busy".
 *
 * A short drop: neither side gets ahead of the other by more than the delay,
 * so a lost connection stops both; each side's INPUT says which of the other's
 * inputs it lacks first, and the other sends from there (also those it sent
 * before the drop), so the lockstep goes on from the last vblank both had,
 * as soon as packets come through again (within NP_SILENT_NS).
 */
#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "netplay_core.h"

#define RESEND_NS 16000000ull       /* a vblank waiting: its INPUT again this often */
#define IDLE_NS 100000000ull        /* no vblank this long: a pause */
#define HELLO_EVERY_NS 250000000ull /* the joiner's HELLO until the answer */
#define PAGE_AT 20                  /* SETTINGS: the values' place */

/* ---- SipHash-2-4 (Aumasson and Bernstein's keyed hash, from its paper) ---- */

#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))
#define SIPROUND                                                                                                       \
    do {                                                                                                               \
        v0 += v1; v1 = ROTL(v1, 13); v1 ^= v0; v0 = ROTL(v0, 32);                                                      \
        v2 += v3; v3 = ROTL(v3, 16); v3 ^= v2;                                                                         \
        v0 += v3; v3 = ROTL(v3, 21); v3 ^= v0;                                                                         \
        v2 += v1; v1 = ROTL(v1, 17); v1 ^= v2; v2 = ROTL(v2, 32);                                                      \
    } while (0)

static uint64_t rd64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}

uint64_t npSipHash(uint64_t k0, uint64_t k1, const uint8_t* m, size_t n) {
    uint64_t v0 = k0 ^ 0x736f6d6570736575ull, v1 = k1 ^ 0x646f72616e646f6dull;
    uint64_t v2 = k0 ^ 0x6c7967656e657261ull, v3 = k1 ^ 0x7465646279746573ull;
    size_t i;
    for (i = 0; i + 8 <= n; i += 8) {
        uint64_t b = rd64(m + i);
        v3 ^= b;
        SIPROUND;
        SIPROUND;
        v0 ^= b;
    }
    uint64_t b = (uint64_t)n << 56;
    for (size_t j = 0; i + j < n; j++) b |= (uint64_t)m[i + j] << (8 * j);
    v3 ^= b;
    SIPROUND;
    SIPROUND;
    v0 ^= b;
    v2 ^= 0xff;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}

uint64_t npFnv(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

/* ---- packets ---- */

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void npSeal(const NpSession* s, uint8_t* p, int kind) {
    put32(p, NP_MAGIC);
    p[4] = NP_VERSION;
    p[5] = (uint8_t)kind;
    put64(p + NP_TAG, npSipHash(s->k0, s->k1, p, NP_TAG));
}

int npValid(const NpSession* s, const uint8_t* p, int size) {
    return size == NP_PACKET && get32(p) == NP_MAGIC && p[4] == NP_VERSION && p[5] >= NP_HELLO && p[5] <= NP_SETTINGS &&
           rd64(p + NP_TAG) == npSipHash(s->k0, s->k1, p, NP_TAG);
}

void npPutTerms(uint8_t* p, const NpTerms* t) {
    p[24] = (uint8_t)t->delay;
    put16(p + 26, (uint16_t)t->progress);
    p[28] = (uint8_t)t->mode;
    for (int i = 0; i < 3; i++) p[29 + i] = (uint8_t)t->party[i];
    put16(p + 32, (uint16_t)t->table);
    p[34] = (uint8_t)t->config;
}

static int member(int id) {
    return id < 16 || id == 255;
}

int npGetTerms(const uint8_t* p, NpTerms* t) {
    if (p[24] < 1 || p[24] > 15 || get16(p + 26) > 1000 || p[28] > NP_MODE_VERSUS) return 0;
    if (p[28] == NP_MODE_VERSUS && (get16(p + 32) >= NP_TABLES || p[34] > 15 || p[29] == 255 || !member(p[29]) ||
                                   !member(p[30]) || !member(p[31]))) {
        return 0;
    }
    t->delay = p[24];
    t->progress = get16(p + 26);
    t->mode = p[28];
    for (int i = 0; i < 3; i++) t->party[i] = p[29 + i];
    t->table = get16(p + 32);
    t->config = p[34];
    return 1;
}

int npPages(const NpSession* s) {
    return s->settingsCount > 0 ? (s->settingsCount + NP_PAGE - 1) / NP_PAGE : 0;
}

void npPutSettings(const NpSession* s, uint8_t* p, int page, uint64_t nonce) {
    memset(p, 0, NP_PACKET);
    put64(p + 8, nonce);
    p[16] = (uint8_t)page;
    p[17] = (uint8_t)npPages(s);
    p[18] = (uint8_t)s->settingsCount;
    for (int i = 0; i < NP_PAGE && page * NP_PAGE + i < s->settingsCount; i++) {
        put16(p + PAGE_AT + 2 * i, s->terms.settings[page * NP_PAGE + i]);
    }
}

int npGetSettings(const NpSession* s, const uint8_t* p, NpTerms* t, int* page) {
    int pg = p[16];
    if (p[18] != s->settingsCount || p[17] != npPages(s) || pg >= npPages(s)) return 0;
    uint16_t v[NP_PAGE];
    for (int i = 0; i < NP_PAGE && pg * NP_PAGE + i < s->settingsCount; i++) {
        int k = pg * NP_PAGE + i;
        v[i] = get16(p + PAGE_AT + 2 * i);
        if (v[i] < s->settingsLo[k] || v[i] > s->settingsHi[k]) return 0;
    }
    for (int i = 0; i < NP_PAGE && pg * NP_PAGE + i < s->settingsCount; i++) t->settings[pg * NP_PAGE + i] = v[i];
    *page = pg;
    return 1;
}

static void send(NpSession* s, const NetAddr* to, const uint8_t* p) {
    if (s->testDrop != 0 && ++s->testSent % s->testDrop == 0) return; /* the test's lost datagram */
    if (s->testBlackoutUntil != 0 && SDL_GetTicksNS() < s->testBlackoutUntil) return; /* the test's outage */
    netSend(s->s, to, p, NP_PACKET);
}

static void note(NpSession* s, int what, const char* text) {
    if (s->note != NULL) s->note(s, what, text);
}

static int end(NpSession* s, int reason, const char* fmt, ...) {
    va_list ap;
    if (s->over) return 0;
    s->over = 1;
    s->reason = reason;
    va_start(ap, fmt);
    vsnprintf(s->why, sizeof s->why, fmt, ap);
    va_end(ap);
    return 0;
}

/* ---- the session ---- */

void npInit(NpSession* s, int host, const char* code, uint64_t compatId, int region) {
    memset(s, 0, sizeof *s);
    s->host = host;
    s->region = region;
    s->compatId = compatId;
    s->k0 = npFnv(npFnv(NP_FNV0, "xg-netplay-k0:", 14), code, strlen(code));
    s->k1 = npFnv(npFnv(NP_FNV0, "xg-netplay-k1:", 14), code, strlen(code));
    uint64_t now = SDL_GetTicksNS();
    s->nonce = npFnv(NP_FNV0, &now, sizeof now) ^ (uint64_t)(uintptr_t)s;
    s->terms.delay = 3;
    s->silentNs = NP_SILENT_NS;
    s->lostNs = NP_LOST_NS;
    /* no frame known yet (a tag of 0 would claim frame 0) */
    memset(s->mineTag, 0xFF, sizeof s->mineTag);
    memset(s->theirsTag, 0xFF, sizeof s->theirsTag);
    memset(s->myHashTag, 0xFF, sizeof s->myHashTag);
    memset(s->peerHashTag, 0xFF, sizeof s->peerHashTag);
}

int npOpen(NpSession* s, int port) {
    if (!netInit()) return end(s, NP_END_ERROR, "%s", netError());
    s->s = netOpen(port);
    if (s->s == NULL) return end(s, NP_END_ERROR, "%s", netError());
    return 1;
}

void npClose(NpSession* s) {
    if (s->s != NULL) {
        netClose(s->s);
        s->s = NULL;
        netQuit();
    }
}

int npHost(NpSession* s, uint64_t waitNs) {
    uint8_t p[NP_PACKET];
    NetAddr from;
    char text[64];
    uint64_t until = SDL_GetTicksNS() + waitNs;

    for (;;) {
        if (SDL_GetTicksNS() > until) return end(s, NP_END_NOBODY, "nobody joined");
        if (s->poll != NULL) s->poll();
        if (!netWait(s->s, 50)) continue;
        int size = netRecv(s->s, &from, p, sizeof p);
        if (!npValid(s, p, size) || p[5] != NP_HELLO) continue;
        int refuse = p[24] != s->region ? NP_REFUSE_REGION : rd64(p + 8) != s->compatId ? NP_REFUSE_BUILD : 0;
        NpTerms t = s->terms;
        if (!refuse && t.mode == NP_MODE_VERSUS && t.pick) { /* player 2's formation */
            if (p[25] != 1 || get16(p + 26) >= NP_TABLES || p[28] > 15) {
                refuse = NP_REFUSE_PICK;
            } else {
                t.table = get16(p + 26);
                t.config = p[28];
            }
        }
        memset(s->welcome, 0, sizeof s->welcome);
        put64(s->welcome + 8, s->compatId);
        memcpy(s->welcome + 16, p + 16, 8); /* the joiner's nonce */
        npPutTerms(s->welcome, &t);
        s->welcome[25] = (uint8_t)refuse;
        npSeal(s, s->welcome, NP_WELCOME);
        send(s, &from, s->welcome);
        for (int i = 0; i < npPages(s) && !refuse; i++) {
            npPutSettings(s, s->pages[i], i, rd64(p + 16));
            npSeal(s, s->pages[i], NP_SETTINGS);
            send(s, &from, s->pages[i]);
        }
        netAddrText(&from, text, sizeof text);
        if (refuse) {
            note(s, NP_NOTE_REFUSED, text);
            continue;
        }
        s->terms = t;
        s->peer = from;
        s->token = rd64(p + 16);
        s->lastHeard = SDL_GetTicksNS();
        return 1;
    }
}

int npJoin(NpSession* s, const NetAddr* host, uint64_t waitNs) {
    uint8_t p[NP_PACKET];
    NetAddr from;
    uint64_t until = SDL_GetTicksNS() + waitNs, last = 0;
    int welcomed = 0;
    unsigned pages = 0, all = (1u << npPages(s)) - 1;
    NpTerms t = s->terms;

    s->peer = *host;
    for (;;) {
        uint64_t now = SDL_GetTicksNS();
        if (now > until) return end(s, NP_END_NO_ANSWER, "no answer (the address, the port, a firewall, or the code)");
        if (s->poll != NULL) s->poll();
        if (now - last > HELLO_EVERY_NS) {
            memset(p, 0, sizeof p);
            put64(p + 8, s->compatId);
            put64(p + 16, s->nonce);
            p[24] = (uint8_t)s->region;
            p[25] = (uint8_t)(s->hasPick != 0);
            put16(p + 26, (uint16_t)s->pickTable);
            p[28] = (uint8_t)s->pickConfig;
            npSeal(s, p, NP_HELLO);
            send(s, &s->peer, p);
            last = now;
        }
        if (!netWait(s->s, 50)) continue;
        int size = netRecv(s->s, &from, p, sizeof p);
        if (!npValid(s, p, size) || !netAddrEqual(&from, &s->peer)) continue;
        if (p[5] == NP_SETTINGS && rd64(p + 8) == s->nonce) {
            int page;
            if (npGetSettings(s, p, &t, &page)) pages |= 1u << page;
        }
        if (p[5] == NP_WELCOME && rd64(p + 16) == s->nonce) {
            if (p[25] != NP_REFUSE_NONE) {
                int r = p[25] == NP_REFUSE_BUILD    ? NP_END_REFUSED_BUILD
                        : p[25] == NP_REFUSE_REGION ? NP_END_REFUSED_REGION
                        : p[25] == NP_REFUSE_PICK   ? NP_END_PICK
                                                    : NP_END_BUSY;
                return end(s, r, "the host refused: %s", r == NP_END_REFUSED_BUILD    ? "another version of the game"
                                                         : r == NP_END_REFUSED_REGION ? "another region (US/JP)"
                                                         : r == NP_END_PICK ? "it lets player 2 choose the formation: choose one"
                                                                            : "busy");
            }
            welcomed |= npGetTerms(p, &t); /* out of range: not ours */
        }
        if (welcomed && pages == all) {
            s->terms = t;
            s->token = s->nonce;
            s->lastHeard = SDL_GetTicksNS();
            return 1;
        }
    }
}

static void compareHash(NpSession* s, uint32_t frame) {
    int i = frame % NP_RING;
    if (s->myHashTag[i] == frame && s->peerHashTag[i] == frame && s->myHash[i] != s->peerHash[i]) {
        end(s, NP_END_DESYNC, "desync at vblank %u: the two games differ (this side %016llx, the other %016llx)", frame,
            (unsigned long long)s->myHash[i], (unsigned long long)s->peerHash[i]);
        s->endFrame = frame;
    }
}

static void receive(NpSession* s) {
    uint8_t p[NP_PACKET];
    NetAddr from;
    int budget = 256;

    while (budget-- > 0 && !s->over) {
        int size = netRecv(s->s, &from, p, sizeof p);
        if (size < 0) break;
        if (!npValid(s, p, size)) continue; /* nobody's */
        if (!netAddrEqual(&from, &s->peer)) {
            uint64_t token = p[5] == NP_INPUT ? rd64(p + 56) : p[5] == NP_BYE ? rd64(p + 16) : 0;
            if (token != 0 && token == s->token) { /* the other player at a new address */
                char text[64];
                s->peer = from;
                netAddrText(&from, text, sizeof text);
                note(s, NP_NOTE_MOVED, text);
            } else {
                if (p[5] == NP_HELLO && s->host) { /* someone else with the code: busy */
                    uint8_t busy[NP_PACKET] = {0};
                    put64(busy + 8, s->compatId);
                    memcpy(busy + 16, p + 16, 8);
                    busy[24] = 1;
                    busy[25] = NP_REFUSE_BUSY;
                    npSeal(s, busy, NP_WELCOME);
                    send(s, &from, busy);
                }
                continue;
            }
        }
        s->lastHeard = SDL_GetTicksNS();
        if (s->lost) {
            s->lost = 0;
            note(s, NP_NOTE_RESUMED, NULL);
        }
        switch (p[5]) {
        case NP_HELLO:
            if (s->host) { /* our WELCOME was lost */
                send(s, &from, s->welcome);
                for (int i = 0; i < npPages(s); i++) send(s, &from, s->pages[i]);
            }
            break;
        case NP_INPUT: {
            uint32_t first = get32(p + 8);
            int count = p[12] > NP_INPUTS ? NP_INPUTS : p[12];
            for (int k = 0; k < count; k++) {
                uint32_t f = first + (uint32_t)k;
                s->theirs[f % NP_RING] = get16(p + 40 + 2 * k);
                s->theirsTag[f % NP_RING] = f;
            }
            while (s->theirsTag[(s->haveThrough + 1) % NP_RING] == s->haveThrough + 1) s->haveThrough++;
            uint32_t need = get32(p + 16);
            if ((int32_t)(need - s->peerNeed) > 0) s->peerNeed = need;
            if ((p[13] != 0) != s->peerPaused) {
                s->peerPaused = p[13] != 0;
                note(s, s->peerPaused ? NP_NOTE_PAUSED : NP_NOTE_BACK, NULL);
            }
            uint32_t hf = get32(p + 20);
            s->peerHash[hf % NP_RING] = rd64(p + 24);
            s->peerHashTag[hf % NP_RING] = hf;
            compareHash(s, hf);
            if (!s->discChecked && s->discHash != 0) {
                s->discChecked = 1;
                if (rd64(p + 32) != s->discHash) end(s, NP_END_DISC, "the other player's disc is not the same as this one");
            }
            break;
        }
        case NP_BYE:
            s->peerEnd = p[8];
            end(s, NP_END_LEFT, "the other player left the session");
            break;
        }
    }
}

/* This side's inputs up to `newest`: from the first the other side lacks
 * (after a drop, those it lacks since), else the last eight. */
static void sendInput(NpSession* s, uint32_t newest, uint32_t hashFrame, uint64_t hash, int paused) {
    uint8_t p[NP_PACKET] = {0};
    uint32_t lo = s->base + (uint32_t)s->terms.delay; /* the first frame with an input */
    uint32_t first = (int32_t)(newest - lo) >= NP_INPUTS - 1 ? newest - (NP_INPUTS - 1) : lo;
    if ((int32_t)(s->peerNeed - lo) >= 0 && (int32_t)(newest - s->peerNeed) >= 0) first = s->peerNeed;
    int count = (int32_t)(newest - first) + 1;
    if (count > NP_INPUTS) count = NP_INPUTS;
    if (count < 0) count = 0;
    put32(p + 8, first);
    p[12] = (uint8_t)count;
    p[13] = (uint8_t)(paused != 0);
    put32(p + 16, s->haveThrough + 1);
    put32(p + 20, hashFrame);
    put64(p + 24, hash);
    put64(p + 32, s->discHash);
    for (int k = 0; k < count; k++) {
        uint32_t f = first + (uint32_t)k;
        put16(p + 40 + 2 * k, s->mineTag[f % NP_RING] == f ? s->mine[f % NP_RING] : 0);
    }
    put64(p + 56, s->token);
    npSeal(s, p, NP_INPUT);
    send(s, &s->peer, p);
    s->lastSend = SDL_GetTicksNS();
    s->sentNewest = newest;
    s->sentHashFrame = hashFrame;
    s->sentHash = hash;
}

void npIdle(NpSession* s) {
    if (s->over || !s->started) return;
    receive(s);
    if (!s->over && ++s->idleCalls > 1 && SDL_GetTicksNS() - s->lastSend > IDLE_NS) {
        sendInput(s, s->sentNewest, s->sentHashFrame, s->sentHash, 1);
    }
}

void npTestBlackout(NpSession* s, uint64_t ns) {
    s->testBlackoutUntil = SDL_GetTicksNS() + ns;
}

int npTestMove(NpSession* s) {
    NetSocket* next = netOpen(0);
    if (next == NULL) return 0;
    netClose(s->s);
    s->s = next;
    return 1;
}

void npBye(NpSession* s, int end) {
    uint8_t p[NP_PACKET] = {0};
    if (s->s == NULL) return;
    p[8] = (uint8_t)end;
    put64(p + 16, s->token);
    npSeal(s, p, NP_BYE);
    for (int i = 0; i < 3; i++) netSend(s->s, &s->peer, p, NP_PACKET);
}

int npFrame(NpSession* s, uint32_t n, unsigned local, uint64_t hash, unsigned* port1, unsigned* port2) {
    uint32_t delay = (uint32_t)s->terms.delay;
    uint32_t f = n + delay;
    int waited = 0;

    *port1 = *port2 = 0;
    if (s->over) return 0;
    if (!s->started) {
        s->started = 1;
        s->base = n;
        s->haveThrough = n + delay - 1; /* the first `delay` frames have no input */
        s->peerNeed = n + delay;
    }
    s->idleCalls = 0;
    s->mine[f % NP_RING] = (uint16_t)local;
    s->mineTag[f % NP_RING] = f;
    s->myHash[n % NP_RING] = hash;
    s->myHashTag[n % NP_RING] = n;
    sendInput(s, f, n, hash, 0);
    receive(s);
    compareHash(s, n);
    if (n >= s->base + delay) {
        while (!s->over && s->theirsTag[n % NP_RING] != n) {
            uint64_t now = SDL_GetTicksNS();
            if (now - s->lastSend > RESEND_NS) sendInput(s, f, n, hash, 0);
            if (now - s->lastHeard > s->silentNs) end(s, NP_END_SILENT, "the other player stopped answering");
            if (!waited && now - s->lastHeard > 1000000000ull) {
                note(s, NP_NOTE_WAITING, NULL);
                waited = 1;
            }
            if (!s->lost && !s->over && now - s->lastHeard > s->lostNs) {
                s->lost = 1;
                note(s, NP_NOTE_LOST, NULL);
            }
            netWait(s->s, 2);
            receive(s);
            if (s->poll != NULL) s->poll();
        }
    }
    if (s->over) return 0;
    uint16_t a = n >= s->base + delay ? s->mine[n % NP_RING] : 0;
    uint16_t b = n >= s->base + delay ? s->theirs[n % NP_RING] : 0;
    *port1 = s->host ? a : b;
    *port2 = s->host ? b : a;
    return 1;
}
