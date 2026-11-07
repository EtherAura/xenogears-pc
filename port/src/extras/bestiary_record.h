#ifndef PORT_EXTRAS_BESTIARY_RECORD_H
#define PORT_EXTRAS_BESTIARY_RECORD_H

/*
 * The bestiary's progress (xg-htn.35.25): which enemy types the player has
 * met and defeated, per game (xg-htn.35.131). A game's record lives in
 * bestiary-<id>.txt beside settings.ini, under the game's id
 * (port/src/system/gameid.h: given at its first save and kept in the save
 * block's free tail); games saved before ids existed, and saves without one,
 * share bestiary.txt, the installation's record of old. A fresh game keeps
 * its record in memory until its first save (then the file is written),
 * seeded from the record chosen to carry over (settings bestiary_carry: a
 * New Game+ of the bestiary, consumed at that first save). Never in a
 * memory card. With XG_SETTINGS=none nothing is written: this run only.
 *
 * The battle code reports into the running game's record
 * (include/pc_bestiary.h); the bestiary shows a chosen record (the game's,
 * or a save's by its id, or the shared one), xgBestiaryView.
 *
 * A key is the disc's region ('U' or 'J'), its number (1, 2), the enemy set
 * and the type id.
 */

#ifdef __cplusplus
extern "C" {
#endif

enum { XG_BESTIARY_SEEN = 1, XG_BESTIARY_DEFEATED = 2, XG_BESTIARY_CAUGHT_UP = 4 };

/* ---- the viewed record (the game's unless xgBestiaryView chose another) */

/* XG_BESTIARY_* bits of a key, 0 when never met. */
int xgBestiaryState(int region, int disc, int set, int type);
/* "Catch up from a save": seen, marked as caught up (not met in a battle).
 * Returns 1 when it was not seen before. Call xgBestiarySave after a batch. */
int xgBestiaryCatchUp(int region, int disc, int set, int type);
void xgBestiarySave(void);
/* Keys known (met, defeated or caught up). */
int xgBestiaryCount(void);
/* Forget everything in it (the bestiary's reset): the file too. */
void xgBestiaryReset(void);

/* ---- which record is viewed */

/* View a save's record (its id; 0: the shared record of games without one),
 * or the running game's again. */
void xgBestiaryView(unsigned id);
void xgBestiaryViewGame(void);
/* The viewed record: 1 when it is the running game's. */
int xgBestiaryViewingGame(void);
/* The running game's id (0 before its first save) and whether it is a fresh
 * game (nothing loaded). */
unsigned xgBestiaryGameId(void);
int xgBestiaryGameFresh(void);

/* ---- carry-over (New Game+ of the bestiary) */

/* The next fresh game starts with a copy of record id (0: the shared one);
 * XG_BESTIARY_CARRY_NONE cancels. Kept in settings until a fresh game saves. */
#define XG_BESTIARY_CARRY_NONE 0xFFFFFFFFu
void xgBestiaryCarry(unsigned id);
unsigned xgBestiaryCarryPending(void); /* the id, or XG_BESTIARY_CARRY_NONE */

/* The story progress (field variable 0) of the game in progress (only
 * meaningful when the extras opened over the paused game). */
int xgBestiaryGameProgress(void);

/* This game's saves on the two memory cards (port/src/card: the images the
 * game uses), for "catch up from a save" and for viewing a save's record:
 * the save block's copy of the game state (MenuSaveGame +0x1124 holds
 * gameState +0x1920 on, so field variable n is at block +0x1234 + 2n;
 * checksum byte 0x1FFF the sum of 0x100-0x1FFE, src/menu/m_801C8D78.c), and
 * the game id in the block's free tail (+0x1F00, pc_gameid.h). One layout
 * for US and JP (include/menu/menu.h has no regional split). */
typedef struct {
    int slot, block;
    int region; /* 'U' BASLUS-00664..., 'J' BISLPS-01160... */
    int disc;   /* field variable 82: 0 disc 1, 1 disc 2 */
    int progress;
    unsigned id; /* 0: saved without one (the shared record) */
    char title[80];
} XgBestiarySave;
int xgBestiarySaves(XgBestiarySave* out, int max);

#ifdef __cplusplus
}
#endif
#endif
