/*
 * Netplay's protocol and lockstep (netplay_core.h), apart from the game.
 *
 * A packet is 80 bytes, little-endian: 0 magic "XGNP", 4 version, 5 kind,
 * 72 the tag (SipHash-2-4 of bytes 0..71, keyed from the session code), the
 * rest by kind:
 *   HELLO    8 the build's compatibility id, 16 a nonce, 24 the region ('U'/'J'),
 *            25 a formation chosen (1), 26 its table (16 bits), 28 its config,
 *            29 a party brought (1), 30 its record's bytes (16 bits), 32 its
 *            hash (FNV-1a 64 of the bytes), 64 the sender's own version (0
 *            before 6: below)
 *   WELCOME  8 the build's compatibility id, 16 the joiner's nonce, 24 the input delay,
 *            25 a refusal (NP_REFUSE_*), 26 the story progress (16 bits),
 *            28 the mode, 29 the party (3 ids), 32 the table (16 bits),
 *            34 the config, 36 NP_REFUSE_PARTY's reason (the check's, 1..255),
 *            37 the member's place (0-2, 255 none); party vs party: 38 the
 *            host's record's bytes (16 bits), 40 its hash, 48 the hash of the
 *            joiner's record as the host took it
 *   INPUT    8 the first frame f, 12 how many (1..8), 13 paused (1: sent
 *            from a pause, not a vblank), 16 the first frame of the other
 *            side's inputs this side lacks, 20 a vblank, 24 the state's hash
 *            at it, 32 the disc's hash, 40 the inputs for frames f.. (16 bits
 *            each), 56 the session's token
 *   BYE      8 why (the program's code: a session's own end, 0 none), 9 1:
 *            this side refused the other's party (the joiner, the host's), 10
 *            the reason, 11 the member's place, 16 the session's token
 *   SETTINGS 8 the joiner's nonce, 16 the page, 17 the pages, 18 the
 *            settings' count, 20 18 values (16 bits each): the host's
 *            settings that change the game, one page of them
 *   PARTY    8 the joiner's nonce, 16 the page, 17 the pages, 18 the record's
 *            bytes (16 bits), 20 its hash, 28 44 of its bytes (from page * 44;
 *            zeros past its end): party vs party, each side's party record,
 *            the joiner's with each HELLO, the host's after its WELCOME
 * Party vs party: a side takes only the pages of the record the other side
 * announced (HELLO, WELCOME: its length and hash, the joiner's nonce), in any
 * order, again or lost and sent again with the next HELLO or its answer;
 * once all are in and the bytes have that hash, the program's check
 * (partyCheck) decides: the host refuses a joiner's party in its WELCOME (the
 * reason and the member, and waits on for another player), the joiner the
 * host's in a BYE (both ends then say why).
 * Older builds (versions NP_VERSION_OLDEST up, whose HELLO and WELCOME have
 * these fields where these have them): the host answers an older HELLO with
 * a WELCOME in its version refusing it as another build, and the joiner
 * sends its HELLO in each older version too, its byte 64 its own version,
 * which a host as new leaves (its own HELLO comes in its own version) and an
 * older one refuses as another build; so both ends say "another version"
 * where they would otherwise wait for nothing.
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
#define PARTY_AT 28                 /* PARTY: the record's bytes' place */
#define SENDER_VERSION 64           /* HELLO: the sender's own version */

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

void npSealVersion(const NpSession* s, uint8_t* p, int kind, int version) {
    put32(p, NP_MAGIC);
    p[4] = (uint8_t)version;
    p[5] = (uint8_t)kind;
    put64(p + NP_TAG, npSipHash(s->k0, s->k1, p, NP_TAG));
}

void npSeal(const NpSession* s, uint8_t* p, int kind) {
    npSealVersion(s, p, kind, NP_VERSION);
}

int npValid(const NpSession* s, const uint8_t* p, int size) {
    return size == NP_PACKET && get32(p) == NP_MAGIC && p[4] == NP_VERSION && p[5] >= NP_HELLO && p[5] <= NP_PARTY &&
           rd64(p + NP_TAG) == npSipHash(s->k0, s->k1, p, NP_TAG);
}

int npOlder(const NpSession* s, const uint8_t* p, int size, int kind) {
    if (size != NP_PACKET || get32(p) != NP_MAGIC || p[4] < NP_VERSION_OLDEST || p[4] >= NP_VERSION || p[5] != kind ||
        rd64(p + NP_TAG) != npSipHash(s->k0, s->k1, p, NP_TAG)) {
        return 0;
    }
    return p[4];
}

void npPutTerms(uint8_t* p, const NpTerms* t) {
    p[24] = (uint8_t)t->delay;
    put16(p + 26, (uint16_t)t->progress);
    p[28] = (uint8_t)t->mode;
    for (int i = 0; i < 3; i++) p[29 + i] = (uint8_t)t->party[i];
    put16(p + 32, (uint16_t)t->table);
    p[34] = (uint8_t)t->config;
}

/* A versus battle's member: one of the 11 characters (gameState.m26C_party[11]), or none. */
static int member(int id) {
    return id <= 10 || id == 255;
}

int npGetTerms(const uint8_t* p, NpTerms* t) {
    if (p[24] < 1 || p[24] > 15 || get16(p + 26) > 1000 || p[28] > NP_MODE_PARTY) return 0;
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

/* ---- party vs party: the records' pages ---- */

uint64_t npPartyHash(const uint8_t* record, int bytes) {
    return npFnv(NP_FNV0, record, bytes > 0 ? (size_t)bytes : 0);
}

int npSetParty(NpSession* s, const uint8_t* record, int bytes) {
    if (bytes < 0 || bytes > NP_PARTY_MAX || (bytes > 0 && record == NULL)) return 0;
    memset(s->party, 0, sizeof s->party);
    if (bytes > 0) memcpy(s->party, record, (size_t)bytes);
    s->partyBytes = bytes;
    s->partyHash = bytes > 0 ? npPartyHash(record, bytes) : 0;
    return 1;
}

int npPartyPages(int bytes) {
    return bytes > 0 && bytes <= NP_PARTY_MAX ? (bytes + NP_PARTY_DATA - 1) / NP_PARTY_DATA : 0;
}

void npPutParty(uint8_t* p, uint64_t nonce, const uint8_t* record, int bytes, uint64_t hash, int page) {
    int at = page * NP_PARTY_DATA;
    int n = bytes - at < NP_PARTY_DATA ? bytes - at : NP_PARTY_DATA;
    memset(p, 0, NP_PACKET);
    put64(p + 8, nonce);
    p[16] = (uint8_t)page;
    p[17] = (uint8_t)npPartyPages(bytes);
    put16(p + 18, (uint16_t)bytes);
    put64(p + 20, hash);
    if (n > 0) memcpy(p + PARTY_AT, record + at, (size_t)n);
}

void npAssemblyStart(NpAssembly* a, uint64_t nonce, uint64_t hash, int bytes) {
    if (a->bytes == bytes && a->nonce == nonce && a->hash == hash) return; /* the same record: its pages kept */
    memset(a, 0, sizeof *a);
    if (npPartyPages(bytes) == 0) return; /* none, or too long */
    a->nonce = nonce;
    a->hash = hash;
    a->bytes = bytes;
    a->pages = npPartyPages(bytes);
}

int npAssemblyTake(NpAssembly* a, const uint8_t* p, int* done) {
    int page = p[16];
    *done = 0;
    if (a->bytes == 0 || rd64(p + 8) != a->nonce || rd64(p + 20) != a->hash || get16(p + 18) != a->bytes ||
        p[17] != a->pages || page >= a->pages) {
        return 0; /* not the announced record's */
    }
    int at = page * NP_PARTY_DATA;
    int n = a->bytes - at < NP_PARTY_DATA ? a->bytes - at : NP_PARTY_DATA;
    memcpy(a->data + at, p + PARTY_AT, (size_t)n);
    if (!a->have[page]) {
        a->have[page] = 1;
        a->got++;
    }
    if (a->got == a->pages) {
        if (npPartyHash(a->data, a->bytes) == a->hash) {
            *done = 1;
        } else { /* pages with that hash but other bytes: all of them again */
            memset(a->have, 0, sizeof a->have);
            a->got = 0;
        }
    }
    return 1;
}

static void send(NpSession* s, const NetAddr* to, const uint8_t* p);

/* This side's record's pages to `to` (the tests: last to first, each twice,
 * a decoy's between them). */
static void sendParty(NpSession* s, const NetAddr* to, uint64_t nonce) {
    uint8_t p[NP_PACKET];
    int pages = npPartyPages(s->partyBytes);
    int decoy = s->testDecoy != NULL ? npPartyPages(s->testDecoyBytes) : 0;
    for (int k = 0; k < pages; k++) {
        int page = s->testPartyShuffle ? pages - 1 - k : k;
        npPutParty(p, nonce, s->party, s->partyBytes, s->partyHash, page);
        npSeal(s, p, NP_PARTY);
        send(s, to, p);
        if (s->testPartyShuffle) send(s, to, p);
        if (k < decoy) {
            npPutParty(p, nonce, s->testDecoy, s->testDecoyBytes, npPartyHash(s->testDecoy, s->testDecoyBytes), k);
            npSeal(s, p, NP_PARTY);
            send(s, to, p);
        }
    }
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
    s->partyMember = -1;
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

/* Host: an older build's HELLO (its byte 64 not a newer build's own
 * version) answered in its version: another build (npHost's header). */
static int olderHello(NpSession* s, const uint8_t* p, int size, const NetAddr* from) {
    int version = npOlder(s, p, size, NP_HELLO);
    if (version == 0 || p[SENDER_VERSION] > version) return 0; /* none, or a newer joiner's (it says its own too) */
    uint8_t w[NP_PACKET] = {0};
    char text[64];
    put64(w + 8, s->compatId);
    memcpy(w + 16, p + 16, 8); /* its nonce */
    w[24] = (uint8_t)s->terms.delay;
    w[25] = NP_REFUSE_BUILD;
    npSealVersion(s, w, NP_WELCOME, version);
    send(s, from, w);
    netAddrText(from, text, sizeof text);
    note(s, NP_NOTE_REFUSED, text);
    return 1;
}

/* The host's answer to the HELLO `p` from `from` (party vs party: its
 * party's pages in *a): 1 the player joins; 0 refused (answered and noted),
 * or with *wait 1 the HELLO waits for its party's pages. */
static int answer(NpSession* s, const uint8_t* p, const NetAddr* from, NpAssembly* a, int* wait) {
    char text[64];
    int refuse = p[24] != s->region ? NP_REFUSE_REGION : rd64(p + 8) != s->compatId ? NP_REFUSE_BUILD : 0;
    int reason = 0, place = 255;
    NpTerms t = s->terms;
    *wait = 0;
    if (!refuse && t.mode == NP_MODE_VERSUS && t.pick) { /* player 2's formation */
        if (p[25] != 1 || get16(p + 26) >= NP_TABLES || p[28] > 15) {
            refuse = NP_REFUSE_PICK;
        } else {
            t.table = get16(p + 26);
            t.config = p[28];
        }
    }
    if (!refuse && t.mode == NP_MODE_PARTY) { /* player 2's party, in its pages */
        int bytes = get16(p + 30);
        if (p[29] != 1 || npPartyPages(bytes) == 0) {
            refuse = NP_REFUSE_NO_PARTY;
        } else {
            int done = 0, member = -1;
            npAssemblyStart(a, rd64(p + 16), rd64(p + 32), bytes);
            done = a->got == a->pages && npPartyHash(a->data, a->bytes) == a->hash;
            if (!done) {
                *wait = 1;
                return 0;
            }
            reason = s->partyCheck != NULL ? s->partyCheck(s, a->data, a->bytes, &member) : 0;
            if (reason != 0) {
                refuse = NP_REFUSE_PARTY;
                reason = reason > 255 ? 255 : reason;
                place = member >= 0 && member <= 2 ? member : 255;
            }
        }
    }
    memset(s->welcome, 0, sizeof s->welcome);
    put64(s->welcome + 8, s->compatId);
    memcpy(s->welcome + 16, p + 16, 8); /* the joiner's nonce */
    npPutTerms(s->welcome, &t);
    s->welcome[25] = (uint8_t)refuse;
    s->welcome[36] = (uint8_t)reason;
    s->welcome[37] = (uint8_t)place;
    if (t.mode == NP_MODE_PARTY) {
        put16(s->welcome + 38, (uint16_t)s->partyBytes);
        put64(s->welcome + 40, s->partyHash);
        put64(s->welcome + 48, a->hash);
    }
    npSeal(s, s->welcome, NP_WELCOME);
    send(s, from, s->welcome);
    netAddrText(from, text, sizeof text);
    if (refuse) {
        if (refuse == NP_REFUSE_PARTY) {
            s->partyReason = reason;
            s->partyMember = place == 255 ? -1 : place;
            note(s, NP_NOTE_REFUSED_PARTY, text);
        } else {
            note(s, NP_NOTE_REFUSED, text);
        }
        return 0;
    }
    s->pageCount = 0;
    for (int i = 0; i < npPages(s); i++) {
        npPutSettings(s, s->pages[s->pageCount], i, rd64(p + 16));
        npSeal(s, s->pages[s->pageCount], NP_SETTINGS);
        send(s, from, s->pages[s->pageCount++]);
    }
    if (t.mode == NP_MODE_PARTY) {
        for (int i = 0; i < npPartyPages(s->partyBytes); i++) {
            npPutParty(s->pages[s->pageCount], rd64(p + 16), s->party, s->partyBytes, s->partyHash, i);
            npSeal(s, s->pages[s->pageCount], NP_PARTY);
            send(s, from, s->pages[s->pageCount++]);
        }
        memcpy(s->peerParty, a->data, (size_t)a->bytes);
        s->peerPartyBytes = a->bytes;
        s->peerPartyHash = a->hash;
    }
    s->terms = t;
    s->peer = *from;
    s->token = rd64(p + 16);
    s->lastHeard = SDL_GetTicksNS();
    return 1;
}

int npHost(NpSession* s, uint64_t waitNs) {
    uint8_t p[NP_PACKET], hello[NP_PACKET];
    NetAddr from, helloFrom;
    uint64_t until = SDL_GetTicksNS() + waitNs;
    NpAssembly a;    /* the joiner's party */
    int waiting = 0; /* `hello` from `helloFrom` waits for its party's pages */

    memset(&a, 0, sizeof a);
    if (s->terms.mode == NP_MODE_PARTY && npPartyPages(s->partyBytes) == 0) {
        return end(s, NP_END_ERROR, "party vs party without a party of this side's");
    }
    for (;;) {
        if (SDL_GetTicksNS() > until) return end(s, NP_END_NOBODY, "nobody joined");
        if (s->poll != NULL) s->poll();
        if (!netWait(s->s, 50)) continue;
        int size = netRecv(s->s, &from, p, sizeof p);
        int wait = 0;
        if (!npValid(s, p, size)) {
            olderHello(s, p, size, &from);
            continue;
        }
        if (p[5] == NP_PARTY) {
            int done = 0;
            if (!waiting || !netAddrEqual(&from, &helloFrom) || !npAssemblyTake(&a, p, &done) || !done) continue;
            if (answer(s, hello, &helloFrom, &a, &wait)) return 1; /* all of the HELLO's party is in */
            waiting = wait;
            continue;
        }
        if (p[5] != NP_HELLO) continue;
        if (answer(s, p, &from, &a, &wait)) return 1;
        waiting = wait;
        if (wait) {
            memcpy(hello, p, sizeof hello);
            helloFrom = from;
        }
    }
}

/* Joiner: this side's HELLO, in its version and (marked) in each older one,
 * then its party's pages. */
static void sendHello(NpSession* s) {
    uint8_t p[NP_PACKET];
    for (int version = NP_VERSION; version >= NP_VERSION_OLDEST; version--) {
        memset(p, 0, sizeof p);
        put64(p + 8, s->compatId);
        put64(p + 16, s->nonce);
        p[24] = (uint8_t)s->region;
        if (version == NP_VERSION) {
            p[25] = (uint8_t)(s->hasPick != 0);
            put16(p + 26, (uint16_t)s->pickTable);
            p[28] = (uint8_t)s->pickConfig;
            p[29] = (uint8_t)(s->partyBytes > 0);
            put16(p + 30, (uint16_t)s->partyBytes);
            put64(p + 32, s->partyHash);
        }
        p[SENDER_VERSION] = NP_VERSION;
        npSealVersion(s, p, NP_HELLO, version);
        send(s, &s->peer, p);
    }
    if (s->partyBytes > 0) sendParty(s, &s->peer, s->nonce);
}

/* Joiner: the host refused this side's HELLO (WELCOME's byte 25). */
static int refused(NpSession* s, const uint8_t* p) {
    int r = p[25] == NP_REFUSE_BUILD      ? NP_END_REFUSED_BUILD
            : p[25] == NP_REFUSE_REGION   ? NP_END_REFUSED_REGION
            : p[25] == NP_REFUSE_PICK     ? NP_END_PICK
            : p[25] == NP_REFUSE_NO_PARTY ? NP_END_NO_PARTY
            : p[25] == NP_REFUSE_PARTY    ? NP_END_PARTY_REFUSED
                                          : NP_END_BUSY;
    if (r == NP_END_PARTY_REFUSED) {
        s->partyReason = p[36];
        s->partyMember = p[37] <= 2 ? p[37] : -1;
    }
    return end(s, r, "the host refused: %s",
               r == NP_END_REFUSED_BUILD    ? "another version of the game"
               : r == NP_END_REFUSED_REGION ? "another region (US/JP)"
               : r == NP_END_PICK           ? "it lets player 2 choose the formation: choose one"
               : r == NP_END_NO_PARTY       ? "it plays party vs party: bring a party"
               : r == NP_END_PARTY_REFUSED  ? "this side's party cannot be used"
                                            : "busy");
}

int npJoin(NpSession* s, const NetAddr* host, uint64_t waitNs) {
    uint8_t p[NP_PACKET];
    NetAddr from;
    uint64_t until = SDL_GetTicksNS() + waitNs, last = 0;
    int welcomed = 0, partyDone = 0;
    unsigned pages = 0, all = (1u << npPages(s)) - 1;
    NpTerms t = s->terms;
    NpAssembly a; /* the host's party */

    memset(&a, 0, sizeof a);
    s->peer = *host;
    for (;;) {
        uint64_t now = SDL_GetTicksNS();
        if (now > until) return end(s, NP_END_NO_ANSWER, "no answer (the address, the port, a firewall, or the code)");
        if (s->poll != NULL) s->poll();
        if (now - last > HELLO_EVERY_NS) {
            sendHello(s);
            last = now;
        }
        if (!netWait(s->s, 50)) continue;
        int size = netRecv(s->s, &from, p, sizeof p);
        if (!npValid(s, p, size)) {
            /* an older host's answer (to the older HELLO): it refuses this build */
            if (npOlder(s, p, size, NP_WELCOME) && netAddrEqual(&from, &s->peer) && rd64(p + 16) == s->nonce) {
                return end(s, NP_END_REFUSED_BUILD, "the host refused: another version of the game");
            }
            continue;
        }
        if (!netAddrEqual(&from, &s->peer)) continue;
        if (p[5] == NP_SETTINGS && rd64(p + 8) == s->nonce) {
            int page;
            if (npGetSettings(s, p, &t, &page)) pages |= 1u << page;
        }
        if (p[5] == NP_PARTY) {
            int done = 0;
            if (npAssemblyTake(&a, p, &done) && done) partyDone = 1;
        }
        if (p[5] == NP_WELCOME && rd64(p + 16) == s->nonce) {
            if (p[25] != NP_REFUSE_NONE) return refused(s, p);
            NpTerms w = t;
            if (npGetTerms(p, &w)) { /* out of range: not ours */
                if (w.mode != NP_MODE_PARTY) {
                    welcomed = 1;
                    t = w;
                } else if (s->partyBytes > 0 && rd64(p + 48) == s->partyHash && npPartyPages(get16(p + 38)) > 0) {
                    welcomed = 1; /* party vs party, with this side's party (as the host took it) */
                    t = w;
                    npAssemblyStart(&a, s->nonce, rd64(p + 40), get16(p + 38));
                    partyDone = a.got == a.pages && npPartyHash(a.data, a.bytes) == a.hash;
                }
            }
        }
        if (welcomed && pages == all && (t.mode != NP_MODE_PARTY || partyDone)) {
            s->terms = t;
            s->token = s->nonce;
            s->lastHeard = SDL_GetTicksNS();
            if (t.mode == NP_MODE_PARTY) {
                int member = -1;
                int reason = s->partyCheck != NULL ? s->partyCheck(s, a.data, a.bytes, &member) : 0;
                if (reason != 0) { /* the host's party: refused here, and the host told */
                    uint8_t bye[NP_PACKET] = {0};
                    s->partyReason = reason > 255 ? 255 : reason;
                    s->partyMember = member >= 0 && member <= 2 ? member : -1;
                    bye[9] = 1;
                    bye[10] = (uint8_t)s->partyReason;
                    bye[11] = (uint8_t)(s->partyMember < 0 ? 255 : s->partyMember);
                    put64(bye + 16, s->token);
                    npSeal(s, bye, NP_BYE);
                    for (int i = 0; i < 3; i++) send(s, &s->peer, bye);
                    return end(s, NP_END_PEER_PARTY, "the host's party cannot be used: refused");
                }
                memcpy(s->peerParty, a.data, (size_t)a.bytes);
                s->peerPartyBytes = a.bytes;
                s->peerPartyHash = a.hash;
            }
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
            if (s->host) { /* our WELCOME was lost (or its pages) */
                send(s, &from, s->welcome);
                for (int i = 0; i < s->pageCount; i++) send(s, &from, s->pages[i]);
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
            if (p[9] == 1) { /* the joiner checked this side's party, and refused it */
                s->partyReason = p[10];
                s->partyMember = p[11] <= 2 ? p[11] : -1;
                end(s, NP_END_PARTY_REFUSED, "the other player refused this side's party");
            } else {
                end(s, NP_END_LEFT, "the other player left the session");
            }
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

void npTestResend(NpSession* s) {
    if (s->over || !s->started) return;
    receive(s);
    if (!s->over) sendInput(s, s->sentNewest, s->sentHashFrame, s->sentHash, 0);
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
