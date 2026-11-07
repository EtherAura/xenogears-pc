#ifndef PORT_PAD_H
#define PORT_PAD_H

/*
 * The pads' bindings (pad.c) for the settings menu (port/src/ui): each PS1
 * button has up to two keys and one gamepad input, kept in settings.ini as
 * key_<name> and pad_<name>. A change applies at once and is written at once.
 */

#include <SDL3/SDL.h>
#include "prompt.h"

#define PAD_BUTTONS 16 /* up down left right cross circle square triangle l1 r1 l2 r2 start select l3 r3 */
#define PAD_KEYS 2

/* Gamepad inputs: an SDL_GamepadButton, or one of these. */
#define PAD_INPUT_NONE (-1)
#define PAD_INPUT_LEFT_TRIGGER (-2)
#define PAD_INPUT_RIGHT_TRIGGER (-3)

void padCameraInput(float* turn, float* tilt, float* zoom, int* recenter); /* the free field camera's stick and keys (-1..1; recenter once) */
const char* padButtonName(int button);  /* "cross": the settings keys' suffix */
const char* padButtonLabel(int button); /* "Cross" */

SDL_Scancode padKey(int button, int slot); /* SDL_SCANCODE_UNKNOWN: none */
/* Bind a key (SDL_SCANCODE_UNKNOWN clears): taken from any other button. */
void padSetKey(int button, int slot, SDL_Scancode key);
int padKeyReserved(SDL_Scancode key);      /* F1-F12, Escape: hotkeys, never bound */

int padGamepadInput(int button);           /* PAD_INPUT_* or an SDL_GamepadButton */
void padSetGamepadInput(int button, int input); /* taken from any other button */
int padStickDpad(void);                    /* the left stick drives the D-pad too */
void padSetStickDpad(int on);
int padStickDeadzone(void);                /* percentage, 0..95; default 50 */
void padSetStickDeadzone(int percent);
/* -1 edits the default mapping; 0/1 edits that connected controller's
 * profile. GUID plus serial when available; otherwise a model shares it. */
int padProfilePort(void);
void padSelectProfile(int port);
SDL_JoystickID padProfileDevice(int port);
const char* padProfileName(int port);
/* The same effective mapping and thresholds for gameplay and both host UIs. */
int padGamepadHeld(SDL_JoystickID device, int button);
const char* padInputLabel(int input);      /* selected controller's physical label */
const char* padActionLabel(const char* button); /* last deliberately used pad's mapped label, NULL without one */
void padDeviceEvent(const SDL_Event* event); /* all events, including while the extras own the loop */
const char* padInputName(int input);       /* SDL's name ("a", "leftshoulder", "lefttrigger") or "none" */
int padInputFromName(const char* name);

void padResetKeys(void);                   /* the defaults (pad.c's header) */
void padResetGamepad(void);

/* The port the keyboard plays on: 0 (port 1, the default) or 1 (port 2: the
 * Battle arena's second player, port/src/system/arena.h). Not saved. */
void padSetKeyboardPort(int port);
int padKeyboardPort(void);

/* Regional face-button conventions, independent of physical bindings. */
int padRegionalControls(int japaneseGame);
void padSetRegionalControls(int japaneseGame, int on);
int padJapaneseControls(void);
const char* padConfirmButton(void);
const char* padCancelButton(void);
unsigned pcPadRegionalButtons(unsigned buttons);
int pcPadPromptSlot(int slot);

#endif /* PORT_PAD_H */
