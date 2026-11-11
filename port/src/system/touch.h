/*
 * The touch pad (system/touch.c; xg-mrj.3.8): a PS1 pad drawn over the game
 * for a touchscreen, its presses ORed into port 1 as the keyboard's are, with
 * a menu button (F1) and a row of the hotkeys' functions (F2-F9).
 *
 *   touch_pad=auto|on|off   (settings.ini; XG_TOUCH_PAD) auto: shown from a
 *                           touch until a key or a controller is used; the
 *                           default on Android, off elsewhere
 *   touch_pad_opacity=N     10..100 percent (default 50)
 *   touch_pad_size=N        60..140 percent (default 100)
 *
 * XG_TOUCH_SCRIPT=file: lines "<vblank> down|move|up <finger> <x> <y>" (x, y
 * from 0 to 1 across the window), through the same path as the system's
 * touches, for tests and frame dumps.
 */
#ifndef PORT_TOUCH_H
#define PORT_TOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

union SDL_Event;

enum TouchControlKind {
    TOUCH_DPAD,     /* the four directions: pressed holds the directions' bits (1 up 2 right 4 down 8 left) */
    TOUCH_CROSS,
    TOUCH_CIRCLE,
    TOUCH_SQUARE,
    TOUCH_TRIANGLE,
    TOUCH_L1,
    TOUCH_L2,
    TOUCH_R1,
    TOUCH_R2,
    TOUCH_SELECT,
    TOUCH_START,
    TOUCH_MENU,     /* the settings menu (F1) */
    TOUCH_HOTKEYS,  /* shows and hides the hotkey row */
    TOUCH_HOTKEY,   /* one of the row's: hotkey is its index in touchHotkeyLabel */
    TOUCH_KINDS
};

typedef struct {
    int kind;
    int hotkey;
    float x, y;     /* centre, in the window's pixels */
    float w, h;     /* full size (a circle: w == h, the diameter) */
    int pressed;    /* TOUCH_DPAD: the directions held */
} TouchControl;

int touchEvent(const union SDL_Event* e); /* pad.c's pump, while the menu is closed: 1 when taken */
unsigned short touchVBlank(void);         /* pad.c, each vblank of live input: the buttons held */
void touchInputSeen(const union SDL_Event* e); /* every event: a key or controller hides an auto pad */
int touchShown(void);
/* The controls for a target of w x h pixels (the window's); at most max. */
int touchControls(TouchControl* out, int max, float w, float h);
float touchOpacity(void);
const char* touchHotkeyLabel(int hotkey); /* NULL past the last */

/* The settings menu's rows (Controls). */
int touchMode(void); /* 0 off, 1 on, 2 auto */
void touchSetMode(int mode);
int touchOpacityPercent(void);
void touchSetOpacityPercent(int percent);
int touchSizePercent(void);
void touchSetSizePercent(int percent);

#ifdef __cplusplus
}
#endif

#endif
