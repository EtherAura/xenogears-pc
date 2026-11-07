#ifndef PC_BESTIARY_H
#define PC_BESTIARY_H

/* The extras' bestiary (port/src/extras/bestiary_record.c): an enemy of
 * set `set` (0D/2+2n), type id `type`, showed itself in a battle (defeated
 * 0) or the party defeated it (1). A record of the player's own, beside
 * settings.ini; nothing the game reads. Weak: a build without the extras has
 * none, and the battle code then calls nothing. */
void pcBestiaryNote(int set, int type, int defeated) __attribute__((weak));
/* 1 when that record says the party has defeated this enemy type before
 * (on this disc and region): the enemy info window's "HP once beaten"
 * rule (port/src/game/battle/enemyinfo.c). Weak as above. */
int pcBestiaryKnown(int set, int type) __attribute__((weak));

#endif
