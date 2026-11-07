#ifndef PORT_RUMBLE_H
#define PORT_RUMBLE_H

/* Controller vibration (rumble.c): the game's own DualShock motor (native)
 * and the port's authored feedback (enhanced, rumble_game.c), mixed per pad
 * on the vblank clock and sent to SDL's rumble. Nothing here changes the
 * game's state or timing. */

#ifdef __cplusplus
extern "C" {
#endif

struct SDL_Gamepad;
union SDL_Event;

enum { RUMBLE_OFF, RUMBLE_GAME, RUMBLE_ENHANCED };

int rumbleMode(void);          /* settings.ini vibration=off|game|enhanced (default game) */
void rumbleSetMode(int mode);
int rumbleStrength(void);      /* vibration_strength=0..100 (%, default 100) */
void rumbleSetStrength(int percent);
const char* rumbleModeName(int mode);

/* pad.c: each vblank, the pads and the actuator buffers the game gave
 * SendPAD (NULL: none); scripted: the input is a script (the game's own
 * vibration option is then never offered, so scripted runs do not depend on
 * the pads connected). */
void rumbleVBlank(struct SDL_Gamepad* pads[2], const unsigned char* actuators[2], int scripted);
void rumbleEvent(const union SDL_Event* e); /* pad.c pump: window focus */
void rumbleStopAll(void);                    /* pause, the settings menu, StopPAD, quit */
void rumbleForget(struct SDL_Gamepad* pad);  /* pad.c: that pad is being closed */
int rumbleSupported(int port);               /* that port's pad has rumble motors */
/* A short pulse on that pad at the current mode and strength (the settings
 * menu's test, while the game waits); 0: no motors there or vibration off. */
int rumbleTest(struct SDL_Gamepad* pad);
int rumblePadHasMotors(struct SDL_Gamepad* pad);

/* The enhanced mode's effects (rumble_game.c): levels 0..1 for the large
 * (low frequency) and small (high frequency) motors; an envelope of attack,
 * hold and decay vblanks. */
void rumbleAdd(float large, float small, int attack, int hold, int decay, const char* what);
/* rumble_game.c, each vblank in enhanced mode: the game's continuous sources
 * (camera shakes) as levels, and its edge events (deathblows) as rumbleAdd. */
void rumbleGamePoll(float* large, float* small);
/* (and the game's PLATFORM_PC hooks, rumble_game.c: pcRumbleBattleDamage,
 * pcRumbleOffered) */
int rumbleOffered(int port); /* rumble.c: the game may offer its own vibration option */
void rumbleLog(const char* fmt, ...); /* XG_RUMBLE_LOG=1 */

#ifdef __cplusplus
}
#endif

#endif
