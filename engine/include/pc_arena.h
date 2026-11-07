#ifndef PC_ARENA_H
#define PC_ARENA_H

/*
 * The Battle arena (F1 > Extras > Battle arena; xg-htn.35.16): Battling's
 * own PLAYER1 VS PLAYER2, from wherever the player walks, and back.
 *
 *   port/src/system/arena.c (.h)     the request, the menu's side, player 2's
 *                                    keyboard (platform layer)
 *   port/src/game/main/arena.c       the session and what it puts back (main)
 *   port/src/game/field/arena_field.c the field's side: the start as the
 *                                    arena's reception does it, the return
 *   port/src/game/battling/thread.c  Battling's task: pcArenaBattlingStart
 *   port/src/game/battle/versus.c    versus battles: player 2's choices
 *   port/src/ui/arena.cpp            the menu block
 */

#ifdef __cplusplus
extern "C" {
#endif

/* The session (pcArenaSession; port/src/system/arena.h's phases, which add
 * PENDING for a request not taken yet). */
enum {
    PC_ARENA_IDLE = 0,
    PC_ARENA_STARTING = 2, /* taken: the arena's music loads in the field */
    PC_ARENA_LEAVING = 3,  /* op 0xBF written: the field leaves for Battling */
    PC_ARENA_IN = 4,       /* in Battling (or back in the field, before the music) */
};

/* Platform, for the game. */
void pcArenaLog(const char* fmt, ...); /* XG_ARENA_LOG=1 */
int pcArenaPending(void);       /* asked (or XG_ARENA), not taken yet */
int pcArenaDirectEntry(unsigned char params[6]); /* XG_ARENA=2,...: Battling's entry block for a match without the menus (once) */
void pcArenaTaken(void);        /* the field takes it now */
void pcArenaKeyboard(int arena); /* 1 in Battling: player 2's keyboard, if chosen */

/* Main (port/src/game/main/arena.c). */
int pcArenaSession(void); /* PC_ARENA_IDLE or PC_ARENA_STARTING.. */
void pcArenaBegin(int music, int field);
int pcArenaStartField(void);   /* the field it started in */
void pcArenaLeaving(void);
void pcArenaBattlingStart(void); /* battlingMainTask */
int pcArenaEnd(void);          /* back in the field: the music to play again */
int pcArenaAbort(void);        /* the player left the field first: its music */

/* Versus battles: player 2 commands an enemy formation (a battle table of
 * the battle debug selector, directory 0x20/3 file table + 7, and a config
 * in it) in a normal battle, the party the player's.
 *   platform (port/src/system/arena.c): the request (the menu, XG_VERSUS) */
enum {
    PC_VERSUS_IDLE = 0,
    PC_VERSUS_LEAVING = 3, /* the field starts the battle */
    PC_VERSUS_IN = 4,      /* in the battle */
};
int pcVersusPending(int* table, int* config);
int pcVersusGears(void); /* the party in its Gears (the setting) */
void pcVersusTaken(void);
/*   main (port/src/game/main/arena.c): the session and the checkpoint */
int pcVersusSession(void);
int pcVersusTable(void);
int pcVersusConfig(void);
void pcVersusBegin(int table, int config);
int pcVersusCheckpoint(void); /* battleMain's start: 1 for a versus battle */
void pcVersusRestore(void);   /* battleMain's end */
void pcVersusAbort(void);
/*   battle (port/src/game/battle/versus.c): battleMain and battleTickMain's
 *   hooks, randomInRange/getRandomValueInRange's plan */
void pcVersusBattleSetup(void);
void pcVersusMonsterTurn(unsigned char entity, int status);
void pcVersusBattleOver(void);
int pcVersusLastResult(void); /* 0x81 player 2 won, 0x40 bit player 1 escaped, else player 1 won; 0 none */
void pcVersusBattleEnd(void);
void pcVersusDraw(void);
int pcVersusRandom(int lo, int hi, int* out);

/* Field (port/src/game/field/arena_field.c): fieldEntryPoint, where the
 * player could open the menu. */
void pcArenaFieldTick(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_ARENA_H */
