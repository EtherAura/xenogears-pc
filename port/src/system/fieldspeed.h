#ifndef PORT_FIELDSPEED_H
#define PORT_FIELDSPEED_H

/*
 * Field speed (xg-mrj.6, fieldspeed.c): the field and the world map at 1x, 2x
 * or 4x, on the clock Battle speed runs on (battlespeed.h, hw.h
 * hwSetGameSpeed). Off by default; field_speed in settings.ini (0 off, else
 * 1, 2 or 4; XG_FIELD_SPEED for one run). A setting of its own: battles keep
 * battle_speed, whatever this one says.
 *
 * It applies while the screen is the field's or the world map's
 * (include/pc_wide.h pcWideGetMode: PC_WIDE_FIELD, PC_WIDE_WORLDMAP), so the
 * menus opened from them, the movie mode, a battle's loading and every other
 * mode run at 1x, and so does a movie the field plays itself, from its start
 * to its end (fieldSpeedMovie). battlespeed.c's vblank picks the factor and
 * keeps its own rules over it (online, the slowdown replay, CD audio).
 */

#include "savestate.h"

/* The setting: 0 off (the default), else the chosen speed, 1, 2 or 4. */
int fieldSpeedChosen(void);
void fieldSpeedSetChosen(int speed); /* saved; 0 turns it off */

/* port/src/game/field/wide.c: a field movie starts (pcFieldMovieBlock(1)) or
 * ends (pcFieldMovieBlock(0)), and the field mode starts or ends
 * (pcFieldWideExit: no movie). */
void fieldSpeedMovie(int on);

/* battlespeed.c's vblank, outside battles: the factor the field or the world
 * map asks for at this vblank, 1 for off, 1x and every other screen. */
int fieldSpeedWanted(void);

/* The movie flag, in battlespeed.c's part of a state (battleSpeedStateIo). */
void fieldSpeedStateIo(XgState* s);

#endif /* PORT_FIELDSPEED_H */
