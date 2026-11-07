/*
 * The mouse as the game's pad (mouse.h): its buttons and wheel become short
 * presses of the PS1 buttons, through the same path as the keyboard's (pad.c
 * ORs them into port 1), so the game sees nothing it could not see from a pad
 * and its logic is unchanged:
 *
 *   left button     the game's confirm button (US Cross, JP Circle; pad.h
 *                   padConfirmButton): advances dialogue, chooses in menus
 *   right button    its cancel button (US Circle, JP Cross)
 *   wheel           the D-pad, a press a notch: up and down (sideways
 *                   wheels: left and right), menus' and lists' cursors
 *   back / forward  (the side buttons) L1 and R1: a menu's previous and next
 *                   page or character; a field's camera turn
 *
 * settings.ini (each read at the click; the settings menu's Controls section):
 *   mouse_game=1    (default) all of the above; 0: the mouse does nothing in
 *                   the game
 *   mouse_point=1   (default) a click on a main menu command or an item list
 *                   entry moves the game's cursor there (D-pad presses) and
 *                   confirms (mouse_menu.c); beside them it does nothing;
 *                   in the menu's other screens a click confirms as above
 *   mouse_target=0  (default off) while the game asks for a battle target,
 *                   a click on one moves the game's target there and
 *                   confirms (mouse_battle.c; an attack's: aims there, and a
 *                   click on the aimed one is the confirm button), beside
 *                   them it does nothing
 *   mouse_camera=0  (default off) dragging with the right button sideways
 *                   turns the camera as L1 (left) and R1 (right) do: in a
 *                   field a 45-degree turn for each sixth of the window's
 *                   width dragged (each waiting for the one before it,
 *                   mouse_camera.c), on the world map L1/R1 held a vblank
 *                   for each 90th; a right click that hardly moved is still
 *                   the cancel button (on its release)
 * Without mouse input nothing changes. Not taken:
 *   - what the settings menu has (port/src/ui takes the mouse while it is
 *     open; a click that closes it is let go after), and the extras';
 *   - the click that brings the window to the front (a quarter of a second
 *     after the window gets the focus);
 *   - presses that waited while the game was not reading its pad (the
 *     extras alone, the setup screen, loading).
 * A press lasts 4 vblanks and the next starts 4 after it (the game reads its
 * pad once a frame, at 20 to 60 frames a second: each press is one press to
 * it); a pointing plan's moves 2 and 2 (the menus read each vblank's pad).
 * At most 16 wait. XG_MOUSE_LOG=1 logs each click where the game's picture has it.
 */

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <string.h>

#include "gpu/gpu.h"
#include "mouse.h"
#include "pad.h"
#include "savestate.h"
#include "settings.h"

int pcWideGetMode(void); /* include/pc_wide.h */
#define PC_WIDE_FIELD 1
#define PC_WIDE_WORLDMAP 3

#define QUEUE 16
#define STALE_NS 1000000000ull
#define FOCUS_GUARD_NS 250000000ull
#define CHECK_WAIT 8      /* vblanks a plan's confirm waits for the game's cursor to get there */
#define DRAG_START 6.0f   /* window units a right press may move and still be a click */
#define FIELD_TURNS 6.0f  /* a field's 45-degree turns for a drag across the window */
#define WORLD_HOLDS 90.0f /* vblanks of the world map's turn for a drag across the window */
#define TURN_WAIT 40      /* vblanks a field's turn waits for the one before it */

/* The game's bits (active high, as pad.c's). */
static const struct {
    const char* name;
    unsigned short bit;
} buttonBits[] = {
    {"up", 0x1000},     {"right", 0x2000}, {"down", 0x4000}, {"left", 0x8000},  {"l1", 0x0004},
    {"r1", 0x0008},     {"triangle", 0x0010}, {"circle", 0x0020}, {"cross", 0x0040}, {"square", 0x0080},
};

typedef struct {
    unsigned short bits;
    unsigned char hold, gap;
    unsigned char check; /* MOUSE_CHECK_*: the game's cursor must be at checkArg first */
    int checkArg;
    Uint64 at;
} Press;

static struct {
    Press queue[QUEUE];
    int count;
    int left;  /* vblanks the current press is held */
    int after; /* then released */
    unsigned short now;
    Uint64 focusAt;
    float wheelX, wheelY; /* partial notches (smooth wheels and touchpads) */
    int rightDown, dragging;
    float dragX, dragMoved, dragTurn, dragWidth;
    int worldHold; /* vblanks of the world map's L1 (<0) or R1 (>0) still to hold */
    int checkWait; /* vblanks a plan's confirm has waited for the game's cursor */
} m;

static unsigned short bitOf(const char* button) {
    for (size_t i = 0; i < sizeof buttonBits / sizeof buttonBits[0]; i++) {
        if (strcmp(buttonBits[i].name, button) == 0) {
            return buttonBits[i].bit;
        }
    }
    return 0;
}

static int setting(const char* key, int def, const char* about) {
    settingsDescribe(key, about);
    return settingsBool(key, def) != 0;
}

int mouseGameEnabled(void) {
    return setting("mouse_game", 1, "mouse_game: 1 (default): in the game, the left button confirms, the right "
                                    "cancels, the wheel moves the cursor, the side buttons are L1 and R1; 0: off");
}

int mousePointEnabled(void) {
    return setting("mouse_point", 1, "mouse_point: 1 (default): a click on a main menu command or an item list "
                                     "entry moves the game's cursor there and confirms");
}

int mouseTargetEnabled(void) {
    return setting("mouse_target", 0, "mouse_target: 1: a click on a battle target chooses it (0, default: off)");
}

int mouseCameraEnabled(void) {
    return setting("mouse_camera", 0, "mouse_camera: 1: dragging with the right button turns the camera in fields "
                                      "and on the world map (0, default: off)");
}

void mouseLog(const char* fmt, ...) {
    if (SDL_getenv("XG_MOUSE_LOG") == NULL) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, ap);
    va_end(ap);
}

static void push(unsigned short bits, int hold, int gap, int check, int checkArg, Uint64 at) {
    if (bits == 0 || m.count >= QUEUE) {
        return;
    }
    Press p = {bits, (unsigned char)hold, (unsigned char)gap, (unsigned char)check, checkArg, at};
    m.queue[m.count++] = p;
}

static void pushPlan(const MousePlan* plan, Uint64 at) {
    if (m.count + plan->count + 1 > QUEUE) {
        return; /* (a click while the last one's plan still runs) */
    }
    for (int i = 0; i < plan->count; i++) {
        push(plan->bits[i], 2, 2, 0, 0, at);
    }
    if (plan->confirm) {
        push(bitOf(padConfirmButton()), 4, 4, plan->check, plan->checkArg, at);
    }
}

/* A point of the window (SDL's window units) in the game's display area. */
static int toDisplay(const SDL_Event* e, float wx, float wy, float* x, float* y) {
    SDL_Window* w = SDL_GetWindowFromID(e->button.windowID);
    float k = w != NULL ? SDL_GetWindowPixelDensity(w) : 1.0f;
    return presentWindowToDisplay(wx * (k > 0 ? k : 1.0f), wy * (k > 0 ? k : 1.0f), x, y);
}

static int cameraMode(void) {
    int mode = pcWideGetMode();
    return mode == PC_WIDE_FIELD || mode == PC_WIDE_WORLDMAP ? mode : 0;
}

void mouseEvent(const SDL_Event* e) {
    switch (e->type) {
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        m.focusAt = e->window.timestamp;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (!mouseGameEnabled() || (m.focusAt != 0 && e->button.timestamp - m.focusAt < FOCUS_GUARD_NS)) {
            break;
        }
        Uint64 at = e->button.timestamp;
        switch (e->button.button) {
        case SDL_BUTTON_LEFT: {
            float x = 0, y = 0;
            MousePlan plan;
            int mapped = toDisplay(e, e->button.x, e->button.y, &x, &y), pointed = 0;
            if (mapped && mousePointEnabled()) {
                pointed = mouseMenuPoint(x, y, &plan);
            }
            if (mapped && pointed == 0 && mouseTargetEnabled()) {
                pointed = mouseBattlePoint(x, y, &plan);
            }
            mouseLog("mouse: click %.0f,%.0f -> display %.1f,%.1f%s (%d moves%s)", e->button.x, e->button.y, x, y,
                     pointed > 0 ? " pointing" : pointed < 0 ? " beside" : mapped ? "" : " no picture",
                     pointed > 0 ? plan.count : 0, pointed > 0 && plan.confirm ? ", confirm" : "");
            if (pointed > 0) {
                pushPlan(&plan, at);
            } else if (pointed == 0) {
                push(bitOf(padConfirmButton()), 4, 4, 0, 0, at);
            }
            break;
        }
        case SDL_BUTTON_RIGHT:
            if (mouseCameraEnabled() && cameraMode()) {
                SDL_Window* w = SDL_GetWindowFromID(e->button.windowID);
                int ww = 0, wh = 0;
                if (w == NULL || !SDL_GetWindowSize(w, &ww, &wh) || ww <= 0) {
                    ww = 960;
                }
                m.rightDown = 1; /* a drag, or a click when it is let go */
                m.dragging = 0;
                m.dragX = e->button.x;
                m.dragMoved = m.dragTurn = 0;
                m.dragWidth = (float)ww;
            } else {
                push(bitOf(padCancelButton()), 4, 4, 0, 0, at);
            }
            break;
        case SDL_BUTTON_X1: push(bitOf("l1"), 4, 4, 0, 0, at); break;
        case SDL_BUTTON_X2: push(bitOf("r1"), 4, 4, 0, 0, at); break;
        default: break;
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (m.rightDown) {
            float dx = e->motion.x - m.dragX;
            m.dragX = e->motion.x;
            m.dragMoved += dx < 0 ? -dx : dx;
            if (m.dragMoved >= DRAG_START) {
                m.dragging = 1;
            }
            if (!m.dragging) {
                break;
            }
            /* Dragging the scene right turns the camera as R1 does. */
            m.dragTurn += dx;
            if (cameraMode() == PC_WIDE_WORLDMAP) {
                float unit = m.dragWidth / WORLD_HOLDS;
                while (m.dragTurn >= unit) {
                    m.worldHold = m.worldHold < 0 ? 1 : m.worldHold + 1;
                    m.dragTurn -= unit;
                }
                while (m.dragTurn <= -unit) {
                    m.worldHold = m.worldHold > 0 ? -1 : m.worldHold - 1;
                    m.dragTurn += unit;
                }
                if (m.worldHold > 90) m.worldHold = 90;
                if (m.worldHold < -90) m.worldHold = -90;
            } else if (cameraMode() == PC_WIDE_FIELD) {
                float unit = m.dragWidth / FIELD_TURNS;
                while (m.dragTurn >= unit) {
                    push(bitOf("r1"), 2, 2, MOUSE_CHECK_FIELD_TURN, 0, e->motion.timestamp);
                    m.dragTurn -= unit;
                }
                while (m.dragTurn <= -unit) {
                    push(bitOf("l1"), 2, 2, MOUSE_CHECK_FIELD_TURN, 0, e->motion.timestamp);
                    m.dragTurn += unit;
                }
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.button == SDL_BUTTON_RIGHT && m.rightDown) {
            if (!m.dragging) {
                push(bitOf(padCancelButton()), 4, 4, 0, 0, e->button.timestamp);
            }
            m.rightDown = m.dragging = 0;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        if (!mouseGameEnabled()) {
            break;
        }
        float dir = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        m.wheelY += e->wheel.y * dir;
        m.wheelX += e->wheel.x * dir;
        Uint64 at = e->wheel.timestamp;
        while (m.wheelY >= 1.0f) { push(bitOf("up"), 4, 4, 0, 0, at); m.wheelY -= 1.0f; }
        while (m.wheelY <= -1.0f) { push(bitOf("down"), 4, 4, 0, 0, at); m.wheelY += 1.0f; }
        while (m.wheelX >= 1.0f) { push(bitOf("right"), 4, 4, 0, 0, at); m.wheelX -= 1.0f; }
        while (m.wheelX <= -1.0f) { push(bitOf("left"), 4, 4, 0, 0, at); m.wheelX += 1.0f; }
        break;
    }
    default:
        break;
    }
}

static int checkOk(const Press* p) {
    switch (p->check) {
    case MOUSE_CHECK_MENU_ENTRY:
    case MOUSE_CHECK_ITEM:
        return mouseMenuCheck(p->check, p->checkArg);
    case MOUSE_CHECK_TARGET:
        return mouseBattleCheck(p->checkArg);
    case MOUSE_CHECK_FIELD_TURN:
        return cameraMode() == PC_WIDE_FIELD && mouseFieldTurnReady();
    default:
        return 1;
    }
}

unsigned short mouseVBlank(void) {
    unsigned short held = 0;
    /* The world map's turn: held while the drag lasts. */
    if (m.worldHold != 0) {
        held = m.worldHold > 0 ? bitOf("r1") : bitOf("l1");
        m.worldHold += m.worldHold > 0 ? -1 : 1;
        if (cameraMode() != PC_WIDE_WORLDMAP) {
            m.worldHold = 0;
            held = 0;
        }
    }
    if (m.left > 0) {
        m.left--;
        return held | m.now;
    }
    if (m.after > 0) {
        m.after--;
        return held;
    }
    /* The next press, unless it waited while the game was not reading; a
     * plan's confirm only where the game's cursor is what the plan wanted. */
    Uint64 now = SDL_GetTicksNS();
    while (m.count > 0) {
        Press p = m.queue[0];
        int wait = p.check == MOUSE_CHECK_FIELD_TURN ? TURN_WAIT : CHECK_WAIT;
        if (now - p.at <= STALE_NS && !checkOk(&p) && ++m.checkWait <= wait) {
            return held; /* (a frame late; the last turn still running) */
        }
        memmove(m.queue, m.queue + 1, (size_t)(m.count - 1) * sizeof m.queue[0]);
        m.count--;
        if (now - p.at > STALE_NS) {
            continue;
        }
        if (m.checkWait > wait) {
            mouseLog("mouse: dropped (the game's cursor is not where the click wanted it: check %d, %d)", p.check,
                     p.checkArg);
            m.checkWait = 0;
            m.count = 0; /* the game went elsewhere: the rest of that plan too */
            break;
        }
        m.checkWait = 0;
        m.now = p.bits;
        m.left = p.hold - 1;
        m.after = p.gap;
        return held | p.bits;
    }
    return held;
}

void mouseReset(void) {
    m.count = 0;
    m.left = m.after = 0;
    m.now = 0;
    m.wheelX = m.wheelY = 0;
    m.rightDown = m.dragging = 0;
    m.worldHold = 0;
    m.checkWait = 0;
}

/* Save states (savestate.h): presses and drags under way belong to the
 * moment before a load. */
void mouseStateLoaded(void) {
    m.count = 0;
    m.left = 0;
    m.after = 0;
    m.now = 0;
    m.wheelX = m.wheelY = 0;
    m.rightDown = m.dragging = 0;
    m.worldHold = 0;
    m.checkWait = 0;
}
