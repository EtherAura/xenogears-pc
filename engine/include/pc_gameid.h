#ifndef PC_GAMEID_H
#define PC_GAMEID_H

#ifdef PLATFORM_PC
/* The game's identity (port/src/system/gameid.c): a random 32-bit id each
 * game gets at its first save, kept in the save block's free tail (block
 * +0x1F00, after the game data at +0x100..+0x1C5B and inside the checksum;
 * the PS1 game ignores it) so records of the player's own, such as the
 * extras' bestiary, can belong to one game and follow it from save to
 * save. A game the kernel starts has none until it saves; a loaded save
 * gives its own, or none when it was written without one. */
void pcGameIdNewGame(void);              /* kernel boot: a fresh game, no id yet */
void pcGameIdLoadTail(const u8* data);   /* a save's data (block +0x100) is loaded */
void pcGameIdSaveTail(u8* data);         /* a save's data is about to be summed and written */
#endif

#endif
