#ifndef PORT_GAME_BATTLE_VERSUS_H
#define PORT_GAME_BATTLE_VERSUS_H

/*
 * Between versus.c (player 2 commands an enemy formation) and mirror.c
 * (player 2 commands a copy of player 1's party): the label and text helpers
 * versus.c has, and the mirror's steps versus.c runs. include/pc_arena.h has
 * the hooks the battle calls.
 */

#define VERSUS_LABEL 24 /* game-text bytes of a label */

/* versus.c */
void pcVersusWord(u8* out, const char* s);                /* fixed words in the narrow font's codes */
void pcVersusGameText(u8* out, const u8* s, s32 max);     /* a string of the game's, at most max bytes */
void pcVersusShowLabel(const u8* s);                      /* the acting entity's name label (window 5) */
void pcVersusHideLabel(void);

/* mirror.c */
void pcVersusMirrorSetup(void); /* pcVersusBattleSetup: the formation as the mirror wants it */
void pcVersusMirrorEnd(void);   /* pcVersusBattleEnd: what the mirror loaded, freed */
int pcVersusMirrorForfeit(void); /* player 2 chose Escape: player 1 won */

#endif
