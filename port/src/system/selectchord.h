#ifndef PORT_SELECTCHORD_H
#define PORT_SELECTCHORD_H

/*
 * Select as the port's chord button (selectchord.c; xg-mrj.6). Select with a
 * shoulder button (L1, R1, L2, R2) is a control of the port's: battle speed's
 * and field speed's Select+R2, rewind's Select+L2. Battles do nothing with
 * those buttons, but on the field Select opens the area map and on the world
 * map it turns the minimap on or off, so there a chord must not reach the
 * game as a Select press.
 *
 * While a chord can start on this screen (battlespeed.h
 * speedControlHoldsSelect: Field speed on, on the field or the world map), a
 * live Select going down is kept from the game until it is known: a shoulder
 * button held as it goes down, or going down while it is held, makes it a
 * chord, and neither Select nor that button reaches the game (the button
 * until it is let go). Let go on its own, Select is handed to the game then,
 * as a press of SELECT_TAP vblanks, so the area map and the minimap still
 * answer it, as the player lets go rather than as they press. With Start
 * held nothing is kept (the game's soft reset is Start+Select+L1+R1).
 *
 * The rule for what makes a chord is the battle enemy window's
 * (gameplay.c pcGameplaySelectTap). Off, nothing is kept and the game sees
 * the buttons as they are, so a run without the option is the PS1's.
 */

/* pad.c, each vblank: port 1's live buttons (pad.c's active-high bits,
 * before the game's own remapping; never a script's), and what the game is
 * handed of them. */
unsigned selectChordFilter(unsigned live);

#endif /* PORT_SELECTCHORD_H */
