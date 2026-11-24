#ifndef PORT_ARENA_H
#define PORT_ARENA_H

/*
 * The Battle arena, for the menu (F1 > Extras > Battle arena, port/src/ui/
 * arena.cpp; xg-htn.35.16): Battling's own PLAYER1 VS PLAYER2 from any
 * field, and back. The game's side and the whole story: include/pc_arena.h,
 * port/src/game/main/arena.c.
 *
 * Settings (settings.ini, XG_<KEY>):
 *   arena_player2_keyboard  1: while Battling runs, the keyboard plays on
 *                           port 2 (player 2) and port 1 is the first
 *                           gamepad's (player 1); 0 (default): port 2 is
 *                           the second gamepad
 *   arena_versus_gears      1: in a versus battle the party fights in its
 *                           Gears (the members who have one); 0 (default):
 *                           on foot or in Gears as it walks
 *   netplay_port, netplay_delay, netplay_address
 *                           Online (port/src/ui/arena.cpp): the port and
 *                           input delay to host with, the last host joined
 *   arena_all_gears         1: every Gear (Argento too) in the arena's Gear
 *                           select; 0 (default): those the story has
 *                           reached, as the game lists them
 *   arena_music             the battle music of arena battles (Battling,
 *                           versus battles; online the host's): 0 (default)
 *                           the game's own, n the game's music ID n - 1
 *                           (1..ARENA_MUSIC_IDS; the menu lists the
 *                           jukebox's songs by title)
 * XG_ARENA=1: asked from the start (headless runs).
 * XG_VERSUS=<table>,<config>: a versus battle asked from the start;
 * XG_VERSUS=mirror: a mirror match.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* pcArenaPhase (the same values as include/pc_arena.h's session). */
enum {
    ARENA_PHASE_IDLE,     /* nothing asked */
    ARENA_PHASE_PENDING,  /* asked: starts at the next moment the player walks free in a field */
    ARENA_PHASE_STARTING, /* taken: the arena's music loads in the field */
    ARENA_PHASE_LEAVING,  /* the field leaves for Battling */
    ARENA_PHASE_IN,       /* in Battling (or back in the field, before the music) */
};

int pcArenaRequest(void); /* 1: asked */
void pcArenaCancel(void); /* a request not taken yet */
int pcArenaPhase(void);
int pcArenaKeyboardOffered(void); /* the keyboard can play as player 2 (pad.c padSetKeyboardPort) */
int pcArenaPlayer2Keyboard(void);
void pcArenaSetPlayer2Keyboard(int on);
int pcArenaAllGearsSetting(void); /* every Gear in the arena's Gear select */
void pcArenaSetAllGears(int on);
#define ARENA_MUSIC_IDS 74 /* the game's music IDs 0..73 (the field's music selector, US and JP) */
int pcArenaMusicSetting(void); /* arena_music: 0 the game's own, else music ID + 1 */
void pcArenaSetMusic(int value);

/* Versus battles (player 2 commands an enemy formation; include/pc_arena.h):
 * battle table 0..252 (the debug selector's), config 0..15. pcVersusPhase:
 * ARENA_PHASE_IDLE, PENDING, LEAVING (the field starts the battle) or IN, and
 * the formation asked for or running. */
int pcVersusRequest(int table, int config);
int pcVersusRequestMirror(void); /* a mirror match (include/pc_arena.h): player 2 commands a copy of the party */
int pcVersusGearsSetting(void); /* the party in its Gears (those who have one) */
void pcVersusSetGears(int on);
void pcVersusCancel(void);
int pcVersusPhase(int* table, int* config);

/* Matches from anywhere (xg-htn.35.144; system/arena.c has the story): the
 * game kept in memory, the match from its canonical start, then back.
 * pcArenaMatchStart: Battling (table, config, party unused) or a versus
 * battle (the formation; the party's character ids 0..10, 255 none, the
 * first someone; fromSave: the members as the save has them, else as a new
 * game makes them; ignored before a game is loaded). pcArenaMatchPhase:
 * ARENA_PHASE_IDLE, PENDING (asked: at the next vblank), IN (the match runs)
 * or LEAVING (the kept game comes back), and the match's kind. */
enum { ARENA_MATCH_BATTLING = 1, ARENA_MATCH_VERSUS = 2, ARENA_MATCH_MIRROR = 3 /* a versus battle against a copy of the party */ };
int pcArenaMatchAvailable(const char** why); /* 0 with why (a sentence) where a match cannot start now */
int pcArenaMatchStart(int kind, int table, int config, const int party[3], int fromSave);
int pcArenaMatchPhase(int* kind);
int pcArenaMatchKind(void); /* the match's kind while one is asked or runs, else 0 */
void pcArenaMatchQuit(void); /* the menu's Leave the match: the kept game back */
int pcArenaGameLoaded(void); /* a game is being played (not the title before one) */
int pcArenaPartyNow(int party[3]); /* the current party (ids 0..10, 255 none, the first someone if any): how many */
int pcArenaMembersJoined(void);    /* the members who have joined: bit n for character n (0..10) */
void pcArenaVBlank(void);    /* every vblank (netplay.c netplayPace) */

#ifdef __cplusplus
}
#endif

#endif /* PORT_ARENA_H */
