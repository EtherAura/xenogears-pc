#ifndef PC_SPEEDUP_H
#define PC_SPEEDUP_H
#ifdef PLATFORM_PC
/* Battle's VSync(0): optional deadline-based pacing after a host overrun.
 * Original timing remains available with Speedup off. */
void pcBattleVSync(void);
/* Field's main flip uses the same optional catch-up. The field still
 * enforces its authored minimum frame interval after drawing. */
void pcFieldVSync(void);
/* Speedup transitions (port/src/system/fieldspeed.c, xg-htn.35.157): the
 * main menu's way in from the field or the world map (their fade, its load,
 * the menu's own opening) and its way out run on the fast clock, as Field
 * speed runs. pcTransitionMenu(1) where the field or the world map starts
 * opening the main menu, (0) where it has the screen back; any other menu
 * passes 0. pcTransitionMenuIdle(1) while the main menu's loop waits for
 * the player, (0) as it leaves for the way out. */
void pcTransitionMenu(int on);
void pcTransitionMenuIdle(int idle);
/* ... and a room change the player walked into: from the exit's request
 * (walkAndChangeField and OP_CHANGE_FIELD_WHEN_READY2 pass whether the
 * player was free just before they lock them: a scene's change, made with
 * the player already locked, passes 0 and keeps its own pace) through
 * transitionFields (the old room's fade or strips, the new room's load and
 * setup, and for the strip and cross-fade modes the new room's first frames
 * inside it) to (0) where the player's update first finds the player free
 * to move (playerCanRun), or at transitionFields' return at the latest, so
 * a room that opens on a scene never runs it fast; the fade-in after that
 * keeps 1x. */
void pcTransitionRoom(int on);
#endif
#endif
