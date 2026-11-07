#ifndef PORT_SYSTEM_NETPLAY_H
#define PORT_SYSTEM_NETPLAY_H

/*
 * Netplay: two players over UDP in input-delay lockstep (netplay.c has the
 * whole story; xenogears --netplay host|join ...). The protocol and the
 * lockstep queue are netplay_core.h.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* main(): "--netplay ..." taken off the arguments (1), none (0), or a usage
 * error (-1). Then netplayConnect, before hwInit: the handshake and the
 * session's start options; 0 when there is no session. */
int netplayArgs(int* argc, char*** argv);
int netplayConnect(void);
int netplayActive(void);
/* 1 (and a note) in a session: `what` would change this side's game alone
 * (options.c starts, gauntlet.c, arena.c, the cards). */
int netplayRefuses(const char* what);

/* pad.c's padVBlank: every vblank, the pacing to real time (and the session's
 * end once Battling is left); then, where the pads are filled, this side's
 * input in and both pads out (waits for the other side's). */
void netplayPace(void);
void netplayFrame(unsigned local, unsigned* port1, unsigned* port2);
/* pad.c's padPumpEvents (also the settings menu's and a pause's loop): the
 * other side heard, and this one kept alive while no vblank comes. */
void netplayIdle(void);

/* ---- a session started from the F1 menu (netplay_launch.c) ---- */

typedef struct {
    int host;          /* 1: host (player 1), 0: join */
    char code[64];     /* the word both players type */
    int port;          /* host: its UDP port */
    char address[256]; /* join: host:port */
    int delay;         /* host: the input delay in vblanks, 1..15 */
    int progress;      /* host: the story progress the Gear list follows, 0..1000 */
    int versus;        /* host: a versus battle (else Battling) ... */
    int joinerPicks;   /* ... the formation player 2's choice, or ... */
    int table, config; /* ... this one (host); join: the formation chosen, if pick */
    int pick;          /* join: a formation chosen (for a host that lets player 2 choose) */
    int party[3];      /* host: the party (character ids, 255 none) */
} NetplayStart;
enum { NETPLAY_IDLE, NETPLAY_STARTING, NETPLAY_HOSTING, NETPLAY_JOINING, NETPLAY_CONNECTED, NETPLAY_OVER };

/* The session's process started: 1. */
int netplayLaunch(const NetplayStart* start);
/* Its state now (NETPLAY_*), and what goes with it: the port (hosting), the
 * host's address (joining), the other player's address (connected; and its
 * input delay), why it ended (over: netplay.c's word, "desync 1234", or
 * "exit N", "start ..."). */
int netplaySessionState(void);
const char* netplaySessionDetail(void);
int netplaySessionDelay(void);
int netplaySessionPaused(void); /* the other player paused */
int netplaySessionLost(void);   /* the connection is lost: the session waits for it (up to a minute) */
void netplaySessionStop(void);
/* The running game's story progress (field variable 0), for the host's Gears;
 * its party (a versus battle's: Fei and Citan when none). */
int netplaySessionStory(void);
void netplaySessionParty(int party[3]);
/* netplayPace without a session of its own: the game waits while one runs. */
void netplayLaunchWait(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_NETPLAY_H */
