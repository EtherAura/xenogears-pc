#ifndef PORT_SYSTEM_GAMEID_H
#define PORT_SYSTEM_GAMEID_H

/* The running game's identity (include/pc_gameid.h): its id, 0 before its
 * first save; and whether it is a fresh game (the kernel's new game, nothing
 * loaded) rather than a save written before ids existed. */
#ifdef __cplusplus
extern "C" {
#endif
unsigned portGameId(void);
int portGameFresh(void);
/* A record keeper's hook (weak, the extras' bestiary): the game changed
 * (a new game, a loaded save, a save state), or the running game was just
 * given its id (assigned 1): its records move under that id. */
void pcBestiaryGameChanged(unsigned id, int fresh, int assigned) __attribute__((weak));
#ifdef __cplusplus
}
#endif
#endif
