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
 * XG_ARENA=1: asked from the start (headless runs).
 * XG_VERSUS=<table>,<config>: a versus battle asked from the start.
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

/* Versus battles (player 2 commands an enemy formation; include/pc_arena.h):
 * battle table 0..252 (the debug selector's), config 0..15. pcVersusPhase:
 * ARENA_PHASE_IDLE, PENDING, LEAVING (the field starts the battle) or IN, and
 * the formation asked for or running. */
int pcVersusRequest(int table, int config);
int pcVersusGearsSetting(void); /* the party in its Gears (those who have one) */
void pcVersusSetGears(int on);
void pcVersusCancel(void);
int pcVersusPhase(int* table, int* config);

#ifdef __cplusplus
}
#endif

#endif /* PORT_ARENA_H */
