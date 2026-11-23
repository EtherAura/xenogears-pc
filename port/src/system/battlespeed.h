#ifndef PORT_BATTLESPEED_H
#define PORT_BATTLESPEED_H

/*
 * Battle speed (xg-mrj.2, battlespeed.c): character and Gear battles at 1x,
 * 2x or 4x. Off by default; battle_speed in settings.ini (0 off, else the
 * chosen 1, 2 or 4; XG_BATTLE_SPEED for one run), F1 > Gameplay > Battle
 * speed, and in a battle the control (battle_speed_key, default Tab; the
 * pad's battle_speed_button, default the chord Select+R2, buttons battles
 * never read) steps it 1x, 2x, 4x, 1x. The same file holds field speed's
 * control (fieldspeed.h): the same keys by default, on the field and the
 * world map.
 *
 * At 2x or 4x the game runs that many of its own frames (vblanks, each with
 * its own pad input, as always) per picture shown, and the audio clock keeps
 * real time (hw.h hwSetGameSpeed): the music keeps its tempo, sound effects
 * play at their own pitch and length as the game starts them. While the
 * battle waits for the player (the command menus, target selection, a page
 * waiting for the confirm button, event dialogue), for the first moments of
 * each new name it puts up, and while CD audio streams, it runs at 1x.
 *
 * The saved speed is for random battles (the ones the encounter roll starts,
 * gameplay.h's class). A scripted battle (a boss, a story fight) starts at 1x
 * with a note; the control steps that battle alone and saves nothing.
 */

#include "savestate.h"

/* The setting: 0 off (the default), else the chosen speed, 1, 2 or 4. */
int battleSpeedChosen(void);
void battleSpeedSetChosen(int speed); /* saved; 0 turns it off */

/* The in-battle control: the next speed (1 -> 2 -> 4 -> 1), with a note.
 * Only while a battle runs and the option is on; 1 when it acted. */
int battleSpeedStep(void);
/* The controls' keys (battle_speed_key in a battle, field_speed_key on the
 * field and the world map: fieldspeed.h fieldSpeedStep; as diagnostics'
 * hotkeys: "Tab", "Shift+Tab", "none"): 1 when this key press was one of
 * them and it acted. */
int battleSpeedKey(int scancode, int mod);
/* pad.c, each vblank: port 1's buttons (pad.c's active-high bits, before the
 * game's remapping); a control acts when its chord (battle_speed_button,
 * field_speed_button) completes (all of it held, one of it down this
 * vblank). */
void battleSpeedPad(unsigned buttons);
/* selectchord.c, each vblank: whether a chord of the controls' with Select
 * can start on this screen (field_speed_button holds Select, the option is
 * on, the field or the world map, not online), so a live Select is kept from
 * the game until it is known to be one. */
int speedControlHoldsSelect(void);

/* port/src/game/battle/speed.c, once per battle frame (battleRender): wait is
 * nonzero while the battle waits for the player; text has a bit per name it
 * shows (a bit that comes on holds 1x for a moment). */
void battleSpeedBattleFrame(int wait, unsigned text);
/* gameplay.c, as a battle starts (pcGameplayBattleStart: every start, a
 * chain's and a retry's too): a scripted battle's own speed begins at 1x. */
void battleSpeedBattleStart(void);

/* hw.c's vblank, before the GPU's: the factor of the frame this vblank
 * starts (hwSetGameSpeed) and whether its picture is shown. */
void battleSpeedVBlank(void);
/* vk.c: this vblank's picture is withheld (a game frame between two shown). */
int battleSpeedHidden(void);
/* The factor now (1 outside battles), for notes and the performance panel. */
int battleSpeedFactor(void);
/* A battle is running (battle frames are coming). */
int battleSpeedInBattle(void);

void battleSpeedStateIo(XgState* s);

#endif /* PORT_BATTLESPEED_H */
