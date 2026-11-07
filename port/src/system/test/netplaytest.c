/* Netplay's protocol and lockstep (netplay_core.c) over a loopback pair
 * (IPv4, and IPv6 where the system has it): addresses (host:port,
 * [ipv6]:port, their text), the packets' tags (SipHash-2-4 against the paper's vectors, every byte of a
 * packet covered, another code or size refused), the terms' ranges and the
 * settings' pages (each value within its range, the list's length), the
 * handshake (another code goes unanswered, another build is refused, the
 * right player connects), the lockstep queue (both sides get the same two
 * pads every vblank: each side's input of `delay` vblanks before, also with
 * lost datagrams), desync detection (both sides stop at the vblank whose
 * state hashes differ), leaving (BYE), and a pause's keep-alive (the other
 * side waits through it; without it, it gives up), a drop (nothing gets
 * through for a while, at the largest delay: the lockstep goes on from where
 * it stopped, the inputs sent before the drop sent again from the first the
 * other side lacks), an address change (the other side followed by its
 * session token), the versus formation player 2 chooses (or must), and the
 * settings a
 * session holds (settingsPin). Each side runs in its own thread, as two
 * processes would. Settings stay in memory (XG_SETTINGS=none). */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system/net.h"
#include "system/netplay_core.h"
#include "system/settings.h"
#include "test/env.h"

static int checks;
#include "test/check.h"

#define FRAMES 600

typedef struct {
    NpSession s;
    const NetAddr* joinTo; /* the joiner's host */
    uint64_t waitNs;       /* the handshake's */
    int connected;
    int frames;
    int desyncAt;   /* this side's hash differs from this vblank on (-1: never) */
    int pauseAt;    /* this side pauses before this vblank (-1: never) ... */
    int pauseMs;    /* ... this long ... */
    int pauseIdle;  /* ... calling npIdle (the event pump) meanwhile, or not */
    int byeAt;      /* this side leaves before this vblank (-1: never) */
    int blackoutAt; /* nothing this side sends gets through from this vblank ... */
    int blackoutMs; /* ... for this long */
    int moveAt;     /* this side moves to a new socket (another port) at this vblank */
    int endedAt;    /* the vblank whose npFrame returned 0 (-1: none) */
    int notes[NP_NOTE_COUNT]; /* NP_NOTE_* seen */
    unsigned p1[FRAMES], p2[FRAMES];
} Side;

static unsigned input(int host, uint32_t n) {
    return host ? (n * 37u + 1u) & 0xFFFFu : (n * 101u + 5u) & 0xFFFFu;
}

static uint64_t hashOf(uint32_t n) {
    return (uint64_t)n * 0x9E3779B97F4A7C15ull + 1;
}

static void noted(NpSession* s, int what, const char* text) {
    (void)text;
    Side* d = s->user;
    if (what >= 0 && what < NP_NOTE_COUNT) d->notes[what]++;
}

static int SDLCALL run(void* data) {
    Side* d = data;
    d->endedAt = -1;
    d->connected = d->s.host ? npHost(&d->s, d->waitNs) : npJoin(&d->s, d->joinTo, d->waitNs);
    if (!d->connected) return 0;
    for (int n = 0; n < d->frames; n++) {
        if (n == d->byeAt) {
            npBye(&d->s, 7);
            d->endedAt = n;
            return 0;
        }
        if (n == d->blackoutAt) npTestBlackout(&d->s, (uint64_t)d->blackoutMs * 1000000ull);
        if (n == d->moveAt && !npTestMove(&d->s)) return 0;
        if (n == d->pauseAt) {
            uint64_t until = SDL_GetTicksNS() + (uint64_t)d->pauseMs * 1000000ull;
            while (SDL_GetTicksNS() < until) {
                if (d->pauseIdle) npIdle(&d->s);
                SDL_Delay(5);
            }
        }
        uint64_t h = hashOf((uint32_t)n) ^ (d->desyncAt >= 0 && n >= d->desyncAt ? 1u : 0u);
        if (!npFrame(&d->s, (uint32_t)n, input(d->s.host, (uint32_t)n), h, &d->p1[n], &d->p2[n])) {
            d->endedAt = n;
            return 0;
        }
    }
    return 0;
}

static void side(Side* d, int host, const char* code, uint64_t exe) {
    memset(d, 0, sizeof *d);
    npInit(&d->s, host, code, exe, 'U');
    d->s.note = noted;
    d->s.user = d;
    d->waitNs = 3000000000ull;
    d->s.silentNs = 3000000000ull; /* a failing case ends sooner */
    d->frames = FRAMES;
    d->desyncAt = d->pauseAt = d->byeAt = d->blackoutAt = d->moveAt = -1;
}

/* A host on a free loopback port and a joiner for it (over ::1 with `v6`),
 * run to the end. */
static NetAddr hostAddr;
static int v6;
static int pair(Side* h, Side* j) {
    if (!npOpen(&h->s, 0) || !npOpen(&j->s, 0)) return 0;
    netLoopback(&hostAddr, v6, netLocalPort(h->s.s));
    j->joinTo = &hostAddr;
    SDL_Thread* a = SDL_CreateThread(run, "host", h);
    SDL_Thread* b = SDL_CreateThread(run, "join", j);
    SDL_WaitThread(a, NULL);
    SDL_WaitThread(b, NULL);
    npClose(&h->s);
    npClose(&j->s);
    return 1;
}

static Side H, J, X;

/* 20 settings, setting i within 0..i*10; the host's value i*7 (in range). */
static void settings(NpSession* s, int host) {
    s->settingsCount = 20;
    for (int i = 0; i < 20; i++) {
        s->settingsLo[i] = 0;
        s->settingsHi[i] = (uint16_t)(i * 10);
        s->terms.settings[i] = (uint16_t)(host ? i * 7 : 0);
    }
}

static int packets(void) {
    /* SipHash-2-4: key 00..0f, messages 00.. of 0, 8, 15 and 63 bytes */
    uint8_t k[16], m[64];
    for (int i = 0; i < 16; i++) k[i] = (uint8_t)i;
    for (int i = 0; i < 64; i++) m[i] = (uint8_t)i;
    uint64_t k0 = 0, k1 = 0;
    for (int i = 7; i >= 0; i--) k0 = k0 << 8 | k[i], k1 = k1 << 8 | k[8 + i];
    CHECK(npSipHash(k0, k1, m, 0) == 0x726fdb47dd0e0e31ull);
    CHECK(npSipHash(k0, k1, m, 8) == 0x93f5f5799a932462ull);
    CHECK(npSipHash(k0, k1, m, 15) == 0xa129ca6149be45e5ull);
    CHECK(npSipHash(k0, k1, m, 63) == 0x958a324ceb064572ull);

    NpSession a, b;
    npInit(&a, 1, "orange", 1, 'U');
    npInit(&b, 0, "orange", 1, 'U');
    uint8_t p[NP_PACKET] = {0};
    for (int i = 8; i < 56; i++) p[i] = (uint8_t)(i * 7);
    npSeal(&a, p, NP_INPUT);
    CHECK(npValid(&a, p, NP_PACKET) && npValid(&b, p, NP_PACKET)); /* the same code: the same key */
    CHECK(!npValid(&a, p, NP_PACKET - 1) && !npValid(&a, p, NP_PACKET + 1));
    for (int i = 0; i < NP_PACKET; i++) { /* any byte changed: dropped */
        uint8_t q[NP_PACKET];
        memcpy(q, p, sizeof q);
        q[i] ^= 0x10;
        CHECK(!npValid(&a, q, NP_PACKET));
    }
    NpSession c;
    npInit(&c, 0, "orangf", 1, 'U');
    CHECK(!npValid(&c, p, NP_PACKET)); /* another code */
    npSeal(&a, p, NP_SETTINGS + 1);
    CHECK(!npValid(&a, p, NP_PACKET)); /* a kind of no use */

    /* the terms: within range in and out, out of range refused */
    NpTerms t, u;
    memset(&t, 0, sizeof t);
    t.delay = 7;
    t.progress = 1000;
    memset(p, 0, sizeof p);
    npPutTerms(p, &t);
    CHECK(npGetTerms(p, &u) && u.delay == 7 && u.progress == 1000 && u.mode == NP_MODE_BATTLING);
    /* a versus battle's: the formation and the party */
    NpTerms v = t;
    v.mode = NP_MODE_VERSUS;
    v.table = NP_TABLES - 1;
    v.config = 15;
    v.party[0] = 0;
    v.party[1] = 15;
    v.party[2] = 255;
    npPutTerms(p, &v);
    CHECK(npGetTerms(p, &u) && u.mode == NP_MODE_VERSUS && u.table == NP_TABLES - 1 && u.config == 15 &&
          u.party[0] == 0 && u.party[1] == 15 && u.party[2] == 255);
    NpTerms w = v;
    w.table = NP_TABLES;
    npPutTerms(p, &w);
    CHECK(!npGetTerms(p, &u));
    w = v;
    w.config = 16;
    npPutTerms(p, &w);
    CHECK(!npGetTerms(p, &u));
    w = v;
    w.party[0] = 255; /* nobody first */
    npPutTerms(p, &w);
    CHECK(!npGetTerms(p, &u));
    w = v;
    w.party[2] = 16;
    npPutTerms(p, &w);
    CHECK(!npGetTerms(p, &u));
    w = v;
    w.mode = NP_MODE_VERSUS + 1;
    npPutTerms(p, &w);
    CHECK(!npGetTerms(p, &u));
    t.delay = 0;
    npPutTerms(p, &t);
    CHECK(!npGetTerms(p, &u));
    t.delay = 16;
    npPutTerms(p, &t);
    CHECK(!npGetTerms(p, &u));
    t.delay = 1;
    t.progress = 1001;
    npPutTerms(p, &t);
    CHECK(!npGetTerms(p, &u));

    /* the settings: 20 on two pages, each within its range */
    settings(&a, 1);
    settings(&b, 0);
    CHECK(npPages(&a) == 2 && npPages(&b) == 2);
    NpTerms got;
    memset(&got, 0, sizeof got);
    for (int page = 0; page < 2; page++) {
        int pg = -1;
        npPutSettings(&a, p, page, 77);
        CHECK(npGetSettings(&b, p, &got, &pg) && pg == page);
    }
    for (int i = 0; i < 20; i++) CHECK(got.settings[i] == a.terms.settings[i]);
    npPutSettings(&a, p, 1, 77);
    p[20 + 2 * 1] = 191; /* setting 19 (page 1, 2nd) above its range 0..190 */
    p[21 + 2 * 1] = 0;
    int pg;
    CHECK(!npGetSettings(&b, p, &got, &pg));
    npPutSettings(&a, p, 0, 77);
    b.settingsCount = 19; /* another list */
    CHECK(!npGetSettings(&b, p, &got, &pg));
    return 0;
}

/* Both sides' pads every vblank: the same, and the inputs `delay` before. */
static int lockstep(int delay, unsigned drop, int overV6) {
    v6 = overV6;
    side(&H, 1, "orange", 42);
    side(&J, 0, "orange", 42);
    H.s.terms.delay = delay;
    H.s.terms.progress = 123;
    H.s.terms.mode = NP_MODE_VERSUS;
    H.s.terms.table = 2;
    H.s.terms.config = 1;
    H.s.terms.party[0] = 0;
    H.s.terms.party[1] = 2;
    H.s.terms.party[2] = 255;
    H.s.testDrop = J.s.testDrop = drop;
    settings(&H.s, 1);
    settings(&J.s, 0);
    CHECK(pair(&H, &J));
    v6 = 0;
    CHECK(H.connected && J.connected);
    CHECK(netAddrIsV4(&H.s.peer) == !overV6 && netAddrIsV4(&J.s.peer) == !overV6);
    CHECK(J.s.terms.delay == delay && J.s.terms.progress == 123);
    CHECK(J.s.terms.mode == NP_MODE_VERSUS && J.s.terms.table == 2 && J.s.terms.config == 1 &&
          J.s.terms.party[0] == 0 && J.s.terms.party[1] == 2 && J.s.terms.party[2] == 255);
    for (int i = 0; i < 20; i++) CHECK(J.s.terms.settings[i] == i * 7); /* the host's, through the handshake */
    if (H.endedAt != -1 || J.endedAt != -1)
        fprintf(stderr, "host ended %d (%s), joiner %d (%s)\n", H.endedAt, H.s.why, J.endedAt, J.s.why);
    CHECK(H.endedAt == -1 && J.endedAt == -1);
    for (int n = 0; n < FRAMES; n++) {
        unsigned e1 = n < delay ? 0 : input(1, (uint32_t)(n - delay));
        unsigned e2 = n < delay ? 0 : input(0, (uint32_t)(n - delay));
        CHECK(H.p1[n] == J.p1[n] && H.p2[n] == J.p2[n]);
        CHECK(H.p1[n] == e1 && H.p2[n] == e2);
    }
    return 0;
}

static int handshake(void) {
    /* another code: no answer for the joiner; the host goes on waiting */
    side(&H, 1, "orange", 42);
    side(&J, 0, "lemon", 42);
    H.waitNs = J.waitNs = 600000000ull;
    CHECK(pair(&H, &J));
    CHECK(!H.connected && !J.connected);
    CHECK(strstr(J.s.why, "no answer") != NULL && strstr(H.s.why, "nobody joined") != NULL);
    CHECK(J.s.reason == NP_END_NO_ANSWER && H.s.reason == NP_END_NOBODY);

    /* another build: refused, and the host takes the next one */
    side(&H, 1, "orange", 42);
    side(&X, 0, "orange", 43);
    side(&J, 0, "orange", 42);
    H.frames = J.frames = 30;
    CHECK(npOpen(&H.s, 0) && npOpen(&X.s, 0) && npOpen(&J.s, 0));
    netLoopback(&hostAddr, 0, netLocalPort(H.s.s));
    X.joinTo = J.joinTo = &hostAddr;
    X.frames = 0;
    SDL_Thread* a = SDL_CreateThread(run, "host", &H);
    SDL_Thread* x = SDL_CreateThread(run, "other build", &X);
    SDL_WaitThread(x, NULL);
    SDL_Thread* b = SDL_CreateThread(run, "join", &J);
    SDL_WaitThread(a, NULL);
    SDL_WaitThread(b, NULL);
    npClose(&H.s);
    npClose(&X.s);
    npClose(&J.s);
    CHECK(!X.connected && strstr(X.s.why, "another version") != NULL && X.s.reason == NP_END_REFUSED_BUILD);
    CHECK(H.notes[NP_NOTE_REFUSED] >= 1);
    CHECK(H.connected && J.connected && H.endedAt == -1 && J.endedAt == -1);
    return 0;
}

static int ends(void) {
    /* a desync at vblank 300 on the joiner's side: both stop there */
    side(&H, 1, "orange", 42);
    side(&J, 0, "orange", 42);
    J.desyncAt = 300;
    CHECK(pair(&H, &J));
    CHECK(H.s.over && J.s.over);
    CHECK(strstr(H.s.why, "desync at vblank 300:") != NULL && strstr(J.s.why, "desync at vblank 300:") != NULL);
    CHECK(H.endedAt >= 300 && H.endedAt <= 300 + 3 && J.endedAt >= 300 && J.endedAt <= 300 + 3);
    CHECK(H.s.reason == NP_END_DESYNC && J.s.reason == NP_END_DESYNC && H.s.endFrame == 300 && J.s.endFrame == 300);

    /* the joiner leaves at vblank 200: the host ends with it */
    side(&H, 1, "orange", 42);
    side(&J, 0, "orange", 42);
    J.byeAt = 200;
    CHECK(pair(&H, &J));
    CHECK(H.s.over && strstr(H.s.why, "left the session") != NULL && H.endedAt >= 197 && H.endedAt <= 203);
    CHECK(H.s.reason == NP_END_LEFT && H.s.peerEnd == 7); /* the end the BYE gave */

    /* a pause of 0.6 s with a 0.3 s silence limit: kept alive, both go on */
    side(&H, 1, "orange", 42);
    side(&J, 0, "orange", 42);
    H.s.silentNs = J.s.silentNs = 300000000ull;
    H.pauseAt = 250;
    H.pauseMs = 600;
    H.pauseIdle = 1;
    CHECK(pair(&H, &J));
    CHECK(H.endedAt == -1 && J.endedAt == -1);
    CHECK(J.notes[NP_NOTE_PAUSED] == 1 && J.notes[NP_NOTE_BACK] == 1);
    for (int n = 0; n < FRAMES; n++) CHECK(H.p1[n] == J.p1[n] && H.p2[n] == J.p2[n]);

    /* the same pause without the event pump: the joiner gives up */
    side(&H, 1, "orange", 42);
    side(&J, 0, "orange", 42);
    H.s.silentNs = J.s.silentNs = 300000000ull;
    H.pauseAt = 250;
    H.pauseMs = 600;
    CHECK(pair(&H, &J));
    CHECK(J.s.over && strstr(J.s.why, "stopped answering") != NULL && J.s.reason == NP_END_SILENT);
    return 0;
}

/* A session's settings: the pin wins over the file's value and the
 * environment, a change is ignored, and it is never written. */
static int pins(void) {
    CHECK(testSetEnv("XG_SETTINGS", "none") == 0 && testSetEnv("XG_DIFFICULTY", "hard") == 0);
    settingsSet("text_speed", "2");
    CHECK(SDL_strcmp(settingsGet("difficulty"), "hard") == 0 && settingsOverride("difficulty") != NULL);
    settingsPin("difficulty", "normal");
    settingsPin("text_speed", "4");
    CHECK(settingsPinned("difficulty") && settingsPinned("text_speed") && !settingsPinned("volume"));
    CHECK(SDL_strcmp(settingsGet("difficulty"), "normal") == 0 && settingsOverride("difficulty") == NULL);
    CHECK(settingsInt("text_speed", 1) == 4);
    settingsSet("difficulty", "hard");
    settingsSetInt("text_speed", 8);
    CHECK(SDL_strcmp(settingsGet("difficulty"), "normal") == 0 && settingsInt("text_speed", 1) == 4);
    settingsSet("volume", "50"); /* others as ever */
    CHECK(settingsInt("volume", 0) == 50 && !settingsPinned("volume"));
    return 0;
}

/* Whether this system has IPv6 (a dual-stack socket): else the IPv6 run is
 * an IPv4 one, said so. */
static int haveV6(void) {
    netInit();
    NetSocket* s = netOpen(0);
    int yes = s != NULL && netSocketV6(s);
    netClose(s);
    netQuit();
    if (!yes) printf("netplaytest: no IPv6 here: its lockstep run goes over IPv4\n");
    return yes;
}

/* Addresses: "host:port", "[ipv6]:port", their text; refused forms. */
static int addresses(void) {
    NetAddr a, b;
    char text[80];
    netInit();
    CHECK(netResolveHostPort("127.0.0.1:7461", &a) && netAddrIsV4(&a) && a.port == 7461);
    netLoopback(&b, 0, 7461);
    CHECK(netAddrEqual(&a, &b));
    netAddrText(&a, text, sizeof text);
    CHECK(strcmp(text, "127.0.0.1:7461") == 0);
    CHECK(netResolveHostPort("[::1]:7462", &a) && !netAddrIsV4(&a) && a.port == 7462);
    netLoopback(&b, 1, 7462);
    CHECK(netAddrEqual(&a, &b));
    netAddrText(&a, text, sizeof text);
    CHECK(strcmp(text, "[::1]:7462") == 0);
    CHECK(netResolveHostPort("[2001:db8::5]:9", &a) && a.ip[0] == 0x20 && a.ip[1] == 0x01 && a.ip[15] == 5);
    netAddrText(&a, text, sizeof text);
    CHECK(strcmp(text, "[2001:db8::5]:9") == 0);
    CHECK(!netResolveHostPort("::1:7461", &a)); /* IPv6 with a port needs [] */
    CHECK(!netResolveHostPort("127.0.0.1", &a) && !netResolveHostPort("127.0.0.1:0", &a));
    CHECK(!netResolveHostPort("[::1]", &a) && !netResolveHostPort("[]:5", &a));
    netQuit();
    return 0;
}

/* A versus battle's formation: player 2's when the host lets it choose (and
 * refused without one), the host's otherwise. */
static void versusHost(Side* h, int pick) {
    h->s.terms.mode = NP_MODE_VERSUS;
    h->s.terms.pick = pick;
    h->s.terms.table = 2;
    h->s.terms.config = 1;
    h->s.terms.party[0] = 0;
    h->s.terms.party[1] = 2;
    h->s.terms.party[2] = 255;
    h->frames = 20;
}

static int picks(void) {
    side(&H, 1, "orange", 42);
    side(&J, 0, "orange", 42);
    versusHost(&H, 1);
    J.frames = 20;
    J.s.hasPick = 1;
    J.s.pickTable = NP_TABLES - 1;
    J.s.pickConfig = 15;
    CHECK(pair(&H, &J));
    CHECK(H.connected && J.connected);
    CHECK(H.s.terms.table == NP_TABLES - 1 && H.s.terms.config == 15 && J.s.terms.table == NP_TABLES - 1 &&
          J.s.terms.config == 15 && J.s.terms.party[1] == 2);

    side(&H, 1, "orange", 42);
    side(&J, 0, "orange", 42);
    versusHost(&H, 1);
    H.waitNs = J.waitNs = 600000000ull;
    CHECK(pair(&H, &J));
    CHECK(!J.connected && J.s.reason == NP_END_PICK && H.notes[NP_NOTE_REFUSED] >= 1);

    side(&H, 1, "orange", 42);
    side(&J, 0, "orange", 42);
    versusHost(&H, 0);
    J.frames = 20;
    J.s.hasPick = 1;
    J.s.pickTable = 7;
    J.s.pickConfig = 3;
    CHECK(pair(&H, &J));
    CHECK(H.connected && J.connected && J.s.terms.table == 2 && J.s.terms.config == 1);
    return 0;
}

/* A drop and an address change: the lockstep goes on where it stopped. */
static int drops(void) {
    /* the host's datagrams lost for 1.2 s at the largest delay: the joiner
     * stops, the host runs `delay` frames ahead and stops too; then the host's
     * inputs from the first the joiner lacks */
    side(&H, 1, "orange", 42);
    side(&J, 0, "orange", 42);
    H.s.terms.delay = 15;
    H.s.lostNs = J.s.lostNs = 300000000ull;
    H.blackoutAt = 200;
    H.blackoutMs = 1200;
    CHECK(pair(&H, &J));
    CHECK(H.endedAt == -1 && J.endedAt == -1);
    CHECK(J.notes[NP_NOTE_LOST] == 1 && J.notes[NP_NOTE_RESUMED] == 1);
    for (int n = 0; n < FRAMES; n++) {
        unsigned e1 = n < 15 ? 0 : input(1, (uint32_t)(n - 15)), e2 = n < 15 ? 0 : input(0, (uint32_t)(n - 15));
        CHECK(H.p1[n] == J.p1[n] && H.p2[n] == J.p2[n] && H.p1[n] == e1 && H.p2[n] == e2);
    }

    /* the joiner on another port at vblank 300 (a NAT's new mapping): followed */
    side(&H, 1, "orange", 42);
    side(&J, 0, "orange", 42);
    J.moveAt = 300;
    CHECK(pair(&H, &J));
    CHECK(H.endedAt == -1 && J.endedAt == -1 && H.notes[NP_NOTE_MOVED] == 1);
    for (int n = 0; n < FRAMES; n++) CHECK(H.p1[n] == J.p1[n] && H.p2[n] == J.p2[n]);
    return 0;
}

int main(void) {
    if (pins() || packets() || addresses() || lockstep(3, 0, 0) || lockstep(1, 0, haveV6()) || lockstep(6, 4, 0) ||
        handshake() || picks() || ends() || drops()) {
        fprintf(stderr, "netplaytest: failed after %d checks\n", checks);
        return 1;
    }
    printf("netplaytest: %d checks, 0 failed\n", checks);
    return 0;
}
