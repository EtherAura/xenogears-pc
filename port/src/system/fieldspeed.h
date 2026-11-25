#ifndef PORT_FIELDSPEED_H
#define PORT_FIELDSPEED_H

/*
 * Field speed (xg-mrj.6, fieldspeed.c): the field and the world map at 1x, 2x
 * or 4x, on the clock Battle speed runs on (battlespeed.h, hw.h
 * hwSetGameSpeed). Off by default; field_speed in settings.ini (0 off, else
 * 1, 2 or 4; XG_FIELD_SPEED for one run). A setting of its own: battles keep
 * battle_speed, whatever this one says. On the field and the world map Tab
 * and Select+R2 step it (fieldSpeedStep), as they step battle speed in a
 * battle; there a Select pressed with a shoulder button is the port's, not
 * the game's (selectchord.h).
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

/* The screens field_speed is for: the field (its movies too) and the world
 * map, without their menus. */
int fieldSpeedOnScreen(void);

/* The control (battlespeed.c's field_speed_key and field_speed_button, Tab
 * and Select+R2 by default, as battle speed's in a battle): on those screens
 * with the option on, the next of 1x, 2x, 4x, saved, with a note; online it
 * stays at 1x and says so. 1 when it acted, 0 elsewhere or with it off. */
int fieldSpeedStep(void);

/* port/src/game/field/wide.c: a field movie starts (pcFieldMovieBlock(1)) or
 * ends (pcFieldMovieBlock(0)), and the field mode starts or ends
 * (pcFieldWideExit: no movie). */
void fieldSpeedMovie(int on);

/* battlespeed.c's vblank, outside battles: the factor the field or the world
 * map asks for at this vblank, 1 for off, 1x and every other screen. */
int fieldSpeedWanted(void);

/* Speedup transitions (xg-htn.35.157): speedup_transitions in settings.ini
 * (0 off, the default; 1 on; XG_SPEEDUP_TRANSITIONS for one run). The main
 * menu's way in from the field and the world map (their fade, the load, the
 * menu's opening animation), its way out (the menu's closing, the reload,
 * the fade back) and a room change the player walks into on the field
 * (transitionFields: the old room's fade or strips, the new room's load; not
 * a scene's own change) run at HW_GAME_SPEED_MAX on the
 * same clock: the game's own frames, its pictures withheld between, the music
 * at its tempo. The game marks the stretches (include/pc_speedup.h
 * pcTransitionMenu, pcTransitionMenuIdle, pcTransitionRoom); the menu's loop,
 * waiting for the player, and the new room's fade-in, with the player in
 * control, are at 1x.
 * battlespeed.c's rules hold it at 1x as they hold the others (online, the
 * slowdown replay, CD audio). */
int transitionSpeedChosen(void);
void transitionSpeedSetChosen(int on); /* saved */
/* battlespeed.c's vblank: HW_GAME_SPEED_MAX inside a marked stretch with the
 * setting on, else 1. */
int transitionSpeedWanted(void);
/* Every mark off (reset.c: a soft reset never reaches the game's own ends). */
void transitionSpeedReset(void);

/* The movie flag and the transition's marks, in battlespeed.c's part of a
 * state (battleSpeedStateIo). */
void fieldSpeedStateIo(XgState* s);

#endif /* PORT_FIELDSPEED_H */
