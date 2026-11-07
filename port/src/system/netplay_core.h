#ifndef PORT_SYSTEM_NETPLAY_CORE_H
#define PORT_SYSTEM_NETPLAY_CORE_H

/*
 * Netplay's protocol and lockstep (netplay_core.c), apart from the game:
 * the packets and their tags, the handshake, the input queue and the state
 * hashes' comparison. netplay.c puts it to the game (the pads, the state it
 * hashes, the session's start and end); the unit test runs two of these over
 * a loopback pair (port/src/system/test/netplaytest.c).
 *
 * Nothing here quits or shows anything: a session that ends (the other side
 * left or went silent, a desync, another disc) sets `over` and `why`, and
 * the call returns 0.
 */

#include <stddef.h>
#include <stdint.h>

#include "net.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NP_MAGIC 0x504E4758u /* "XGNP" */
#define NP_VERSION 3
#define NP_PACKET 80
#define NP_TAG 72 /* the tag's place: SipHash-2-4 of bytes 0..71 */
#define NP_RING 256
#define NP_INPUTS 8 /* each INPUT carries the inputs of its frame and the 7 before */
#define NP_PAGE 18 /* settings per SETTINGS packet */
#define NP_SETTINGS_MAX (3 * NP_PAGE)

enum { NP_HELLO = 1, NP_WELCOME, NP_INPUT, NP_BYE, NP_SETTINGS };
enum { NP_REFUSE_NONE, NP_REFUSE_BUILD, NP_REFUSE_REGION, NP_REFUSE_BUSY, NP_REFUSE_PICK };

/* What the host decides and the joiner takes (range-checked on arrival). */
enum { NP_MODE_BATTLING, NP_MODE_VERSUS };
#define NP_TABLES 253 /* versus: the battle debug selector's tables 0..252, 16 configs each */

typedef struct {
    int delay;    /* input delay in vblanks, 1..15 */
    int progress; /* the story progress the Gear list follows, 0..1000 */
    int mode;     /* NP_MODE_*: Battling's PLAYER1 VS PLAYER2, or a versus battle */
    int table, config; /* versus: the formation (table 0..252, config 0..15) */
    int party[3];      /* versus: the party's character ids (0..15, 255 none; the first one someone) */
    int pick;          /* host, versus: the formation is player 2's choice (its HELLO's); not sent */
    /* the settings that change the game (netplay.c's list, the same build on
     * both sides): the host's values, each within settingsLo..Hi */
    uint16_t settings[NP_SETTINGS_MAX];
} NpTerms;

/* Why a session ended (NpSession.reason). */
enum {
    NP_END_NONE,
    NP_END_ERROR,          /* the socket (why says) */
    NP_END_NOBODY,         /* host: nobody joined in time */
    NP_END_NO_ANSWER,      /* joiner: no WELCOME (the address, the port, a firewall, the code) */
    NP_END_REFUSED_BUILD,  /* joiner: the host runs another game (another compatibility id) */
    NP_END_REFUSED_REGION, /* ... another region */
    NP_END_BUSY,           /* ... the host is busy */
    NP_END_PICK,           /* ... the host lets player 2 choose the formation, and none came */
    NP_END_DESYNC,         /* the state hashes differ (at endFrame) */
    NP_END_DISC,           /* another disc */
    NP_END_LEFT,           /* the other player left (BYE) */
    NP_END_SILENT,         /* the other player went silent (NpSession.silentNs) */
};

/* What happens, for the program's messages (NpSession.note). */
enum {
    NP_NOTE_REFUSED,   /* host: a player of another build or region (text: the address) */
    NP_NOTE_WAITING,   /* a vblank waited a second for the other side */
    NP_NOTE_PAUSED,    /* the other side paused (its menu, the background) */
    NP_NOTE_BACK,      /* ... and goes on */
    NP_NOTE_LOST,      /* nothing heard for NP_LOST_NS: the connection is lost, the session waits */
    NP_NOTE_RESUMED,   /* ... and it is back: the lockstep goes on where it stopped */
    NP_NOTE_MOVED,     /* the other side's address changed (text: the new one) */
    NP_NOTE_COUNT
};

typedef struct NpSession NpSession;
struct NpSession {
    int host;
    int region; /* 'U' or 'J' */
    uint64_t k0, k1;
    uint64_t compatId; /* which builds play together (netplay.c: tools/netplay_compat.py's id) */
    uint64_t nonce;
    int hasPick, pickTable, pickConfig; /* joiner: its choice of formation, in its HELLO */
    NetSocket* s;
    NetAddr peer;
    NpTerms terms;
    int settingsCount; /* the list's length, and each one's range (both sides) */
    uint16_t settingsLo[NP_SETTINGS_MAX], settingsHi[NP_SETTINGS_MAX];
    uint8_t welcome[NP_PACKET]; /* host: the answer, sent again to a repeated HELLO */
    uint8_t pages[3][NP_PACKET]; /* ... and its settings */
    /* lockstep */
    int started;
    uint32_t base;
    uint16_t mine[NP_RING], theirs[NP_RING];
    uint32_t mineTag[NP_RING], theirsTag[NP_RING];
    uint64_t myHash[NP_RING], peerHash[NP_RING];
    uint32_t myHashTag[NP_RING], peerHashTag[NP_RING];
    uint64_t discHash; /* 0: not checked */
    int discChecked;
    uint64_t lastSend, lastHeard;
    uint64_t token;      /* the session's (the joiner's nonce): the other side at a new address */
    uint32_t haveThrough; /* the other side's inputs: all of them up to this frame */
    uint32_t peerNeed;    /* the first of this side's inputs the other side lacks */
    int lost;
    uint32_t sentNewest, sentHashFrame; /* the last INPUT, again from a pause */
    uint64_t sentHash;
    int peerPaused;
    int idleCalls;
    uint64_t silentNs; /* the other side's silence that ends the session (NP_SILENT_NS) */
    uint64_t lostNs;   /* ... that counts as a lost connection (NP_LOST_NS) */
    /* the end */
    int over;
    int reason;        /* NP_END_* */
    uint32_t endFrame; /* NP_END_DESYNC: the vblank */
    int peerEnd;       /* NP_END_LEFT: the end the other side's BYE gave (the program's code; 0 none) */
    char why[200];
    /* the program's */
    void (*note)(NpSession* s, int what, const char* text);
    void (*poll)(void); /* while a vblank waits (the window can close) */
    void* user;
    /* tests: every testDrop-th datagram this side sends is lost (0: none);
     * none goes out until testBlackoutUntil (npTestBlackout) */
    unsigned testDrop, testSent;
    uint64_t testBlackoutUntil;
};

/* SipHash-2-4 (the packets' tag). */
uint64_t npSipHash(uint64_t k0, uint64_t k1, const uint8_t* m, size_t n);
/* FNV-1a 64 (the state and disc hashes, the key from the code). */
#define NP_FNV0 14695981039346656037ull
uint64_t npFnv(uint64_t h, const void* p, size_t n);

/* A session's start: its side, the code both players type, this build's
 * compatibility id and region. Then npOpen (the socket: the host's port, 0 for any). */
void npInit(NpSession* s, int host, const char* code, uint64_t compatId, int region);
int npOpen(NpSession* s, int port);
void npClose(NpSession* s);

/* Packets: the header and tag put on, and checked (size, magic, version,
 * kind, tag). */
void npSeal(const NpSession* s, uint8_t* p, int kind);
int npValid(const NpSession* s, const uint8_t* p, int size);
/* WELCOME's terms in and out; npGetTerms returns 0 when one is out of range. */
void npPutTerms(uint8_t* p, const NpTerms* t);
int npGetTerms(const uint8_t* p, NpTerms* t);
/* The settings' pages: how many, one put into a SETTINGS packet (after
 * `nonce`), and one taken (0: another list's length, or a value out of its
 * range; the values land in t->settings). */
int npPages(const NpSession* s);
void npPutSettings(const NpSession* s, uint8_t* p, int page, uint64_t nonce);
int npGetSettings(const NpSession* s, const uint8_t* p, NpTerms* t, int* page);

/* The handshake. Host: the first player with the right code, build and
 * region, within `waitNs` (refused ones get their answer and are noted);
 * the terms are s->terms. Joiner: HELLO to `host` until the WELCOME, within
 * `waitNs`; the terms land in s->terms. 1 when connected; 0 with s->why. */
int npHost(NpSession* s, uint64_t waitNs);
int npJoin(NpSession* s, const NetAddr* host, uint64_t waitNs);

/* Lockstep, once per vblank n (counting on from any start, one by one):
 * `local` is this side's input now, `hash` the state's hash at n. Out come
 * the two pads for n (player 1 the host's): the inputs each side gave `delay`
 * vblanks before, 0 for the first `delay`. Waits for the other side's;
 * 0 when the session ended. */
int npFrame(NpSession* s, uint32_t n, unsigned local, uint64_t hash, unsigned* port1, unsigned* port2);
/* The event pump, also from a pause's loop: the other side heard, and this
 * one kept alive (its last input again, marked paused) while no vblank comes. */
void npIdle(NpSession* s);
/* The other side told this one leaves; `end` (0..255, the program's code, 0
 * none) is why, when the session ended of itself (the game's end, which the
 * other side reaches a few vblanks later): it says the same then. */
void npBye(NpSession* s, int end);
/* The other side's silence after which the connection counts as lost (the
 * session waits for it), and after which the session ends. */
#define NP_LOST_NS 3000000000ull
#define NP_SILENT_NS 60000000000ull

/* Tests: nothing this side sends goes out for `ns` from now (an outage); a
 * new socket on another port in place of this side's (an address change). */
void npTestBlackout(NpSession* s, uint64_t ns);
int npTestMove(NpSession* s);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_NETPLAY_CORE_H */
