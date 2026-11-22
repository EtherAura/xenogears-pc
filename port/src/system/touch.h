/*
 * The touch pad (system/touch.c; xg-mrj.3.8, xg-mrj.3.20): a PS1 pad drawn
 * over the game for a touchscreen, its presses ORed into port 1 as the
 * keyboard's are, with a menu button (F1) and a row of the hotkeys' functions
 * (F2-F9). It shows over the settings menu too, and moves about it as a
 * controller does (touch.c).
 *
 *   touch_pad=auto|on|off   (settings.ini; XG_TOUCH_PAD) auto: shown from a
 *                           touch until a key or a controller is used; the
 *                           default on Android, off elsewhere
 *   touch_pad_layout=buttons|sticks  buttons (the default): the D-pad and
 *                           the buttons. sticks: a left stick as well (the
 *                           directions, eight ways) and, while the Enhanced
 *                           engine's free camera or first-person view is
 *                           allowed, a right stick for the camera (L2 held:
 *                           up and down zoom, as a controller's) with a
 *                           button that puts the camera back and one that
 *                           enters and leaves the first-person view
 *   touch_pad_opacity=N     10..100 percent (default 50)
 *   touch_pad_size=N        60..140 percent (default 100); a pad that would
 *                           not fit the screen at that size is drawn at the
 *                           largest that does
 *
 * XG_TOUCH_SCRIPT=file: lines "<vblank> down|move|up <finger> <x> <y>" (x, y
 * from 0 to 1 across the window), through the same path as the system's
 * touches, for tests and frame dumps.
 *
 * The controls keep off the display's cutouts (xg-mrj.3.13: a camera hole;
 * on Android the activity's, elsewhere XG_TOUCH_CUTOUT="x,y,w,h[;...]" in
 * fractions of the window, for tests): the group a covered control belongs
 * to moves clear of it.
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
    TOUCH_STICK_LEFT,  /* the directions, eight ways: pressed as the D-pad's, sx and sy the stick */
    TOUCH_STICK_RIGHT, /* the camera: sx and sy the stick */
    TOUCH_VIEW,     /* the first-person view, entered and left: on while it shows */
    TOUCH_RECENTRE, /* the camera back to the game's view, while held */
    TOUCH_KINDS
};

typedef struct {
    int kind;
    int hotkey;
    float x, y;     /* centre, in the window's pixels */
    float w, h;     /* full size (a circle: w == h, the diameter) */
    int pressed;    /* TOUCH_DPAD and TOUCH_STICK_LEFT: the directions held */
    int held;       /* a stick: a finger is on it */
    float sx, sy;   /* a stick: where it is pushed, -1..1 (right and down positive) */
    int on;         /* TOUCH_VIEW: the first-person view shows */
} TouchControl;

int touchEvent(const union SDL_Event* e); /* pad.c's pump, while the menu is closed: 1 when taken */
/* ui/menu.cpp, while the settings menu is open: 1 when the event is the pad's (a finger that
 * landed on one of its controls, and the mouse SDL makes of it); the others are the menu's. */
int touchMenuEvent(const union SDL_Event* e);
/* ui/menu.cpp: a finger holds the PS1 button of that name (pad.h's: "up", "cross", "start"...). */
int touchButtonHeld(const char* name);
unsigned short touchVBlank(void);         /* pad.c, each vblank of live input: the buttons held */
/* pad.c, each vblank of live input: the right stick (-1..1, right and down
 * positive, 0 while no finger is on it) and whether the camera is asked back
 * to the game's view. Returns 0 when there is nothing of either. */
int touchCamera(float* x, float* y, int* recentre);
void touchInputSeen(const union SDL_Event* e); /* every event: a key or controller hides an auto pad */
int touchShown(void);
/* The controls for a target of w x h pixels (the window's); at most max. */
int touchControls(TouchControl* out, int max, float w, float h);
/* A stick's reach, in the same pixels: how far from its centre its cap goes. */
float touchStickTravel(const TouchControl* stick);
float touchOpacity(void);
const char* touchHotkeyLabel(int hotkey); /* NULL past the last */

/* The settings menu's rows (Controls). */
int touchMode(void); /* 0 off, 1 on, 2 auto */
void touchSetMode(int mode);
int touchLayout(void); /* 0 buttons, 1 buttons and sticks */
void touchSetLayout(int layout);
int touchOpacityPercent(void);
void touchSetOpacityPercent(int percent);
int touchSizePercent(void);
void touchSetSizePercent(int percent);
/* The size the pad is drawn at on a target of w x h pixels: the setting, or
 * the largest that fits when the setting's does not. */
int touchFittedSizePercent(float w, float h);

#ifdef __cplusplus
}
#endif

#endif
