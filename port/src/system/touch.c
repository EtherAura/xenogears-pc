/*
 * The touch pad (touch.h; xg-mrj.3.8). Each finger takes the control it
 * lands on and keeps it while it moves: on the D-pad its direction follows
 * the finger (eight ways, from the pad's centre), on the face buttons it
 * slides from one to the next; L1, L2, R1, R2, Select and Start are held
 * while touched; the menu button, the hotkey button and the hotkey row act
 * when the finger lifts on them. The layout is in hundredths of the
 * window's short side, so it keeps its proportions on any screen; the art
 * is port/src/ui/touchpad.cpp's.
 *
 * While the pad is in use the touches' mouse events (SDL makes a mouse of
 * the first finger) are dropped, or a tap would also be a click to the
 * mouse's pad (mouse.c). With the pad off, they go there as before. The
 * settings menu takes touches as the mouse (port/src/ui/menu.cpp).
 */

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "touch.h"

/* port/src/system/pad.c and port/src/ui: weak, so tests link this file alone. */
extern void padHotkey(int scancode, int mod) __attribute__((weak));
extern void uiRequestToggle(void) __attribute__((weak));
extern int uiIsOpen(void) __attribute__((weak));
extern unsigned hwVSyncCount(void) __attribute__((weak));

#define B_L2 0x0001
#define B_R2 0x0002
#define B_L1 0x0004
#define B_R1 0x0008
#define B_TRIANGLE 0x0010
#define B_CIRCLE 0x0020
#define B_CROSS 0x0040
#define B_SQUARE 0x0080
#define B_SELECT 0x0100
#define B_START 0x0800
#define B_UP 0x1000
#define B_RIGHT 0x2000
#define B_DOWN 0x4000
#define B_LEFT 0x8000

#define FINGERS 10
#define MAX_CONTROLS 32

static const struct {
    const char* label;
    int scancode;
    int mod;
} hotkeys[] = {
    {"Save", SDL_SCANCODE_F7, 0},   {"Load", SDL_SCANCODE_F8, 0},   {"Slot", SDL_SCANCODE_F9, 0},
    {"Disc", SDL_SCANCODE_F2, 0},   {"Aspect", SDL_SCANCODE_F3, 0}, {"Scale", SDL_SCANCODE_F4, 0},
    {"Filter", SDL_SCANCODE_F5, 0}, {"Fill", SDL_SCANCODE_F6, 0},
};
#define HOTKEYS ((int)(sizeof hotkeys / sizeof hotkeys[0]))

static struct {
    SDL_FingerID id;
    int used;
    int control; /* index into the layout at the touch, or -1 */
    int kind, hotkey;
    float x, y;  /* the finger now, window pixels */
} fingers[FINGERS];

static int autoShown, rowOpen;
static float winW, winH;

/* ---- settings ---------------------------------------------------------------------- */

int touchMode(void) {
    settingsDescribe("touch_pad", "touch_pad: the touch pad over the game: auto (shown from a touch until a key or "
                                  "a controller is used), on, off");
    const char* s = settingsGet("touch_pad");
    if (s != NULL && SDL_strcasecmp(s, "on") == 0) return 1;
    if (s != NULL && SDL_strcasecmp(s, "off") == 0) return 0;
    if (s != NULL && SDL_strcasecmp(s, "auto") == 0) return 2;
#ifdef __ANDROID__
    return 2;
#else
    return 0;
#endif
}

void touchSetMode(int mode) {
    settingsSet("touch_pad", mode == 1 ? "on" : mode == 0 ? "off" : "auto");
}

int touchOpacityPercent(void) {
    int v = settingsInt("touch_pad_opacity", 50);
    return v < 10 ? 10 : v > 100 ? 100 : v;
}

void touchSetOpacityPercent(int percent) {
    settingsSetInt("touch_pad_opacity", percent);
}

int touchSizePercent(void) {
    int v = settingsInt("touch_pad_size", 100);
    return v < 60 ? 60 : v > 140 ? 140 : v;
}

void touchSetSizePercent(int percent) {
    settingsSetInt("touch_pad_size", percent);
}

float touchOpacity(void) {
    return touchOpacityPercent() / 100.0f;
}

int touchShown(void) {
    int mode = touchMode();
    return mode == 1 || (mode == 2 && autoShown);
}

const char* touchHotkeyLabel(int hotkey) {
    return hotkey >= 0 && hotkey < HOTKEYS ? hotkeys[hotkey].label : NULL;
}

/* ---- the layout -------------------------------------------------------------------- */

static void add(TouchControl* out, int* n, int max, int kind, int hotkey, float x, float y, float w, float h) {
    if (*n >= max) return;
    TouchControl* c = &out[(*n)++];
    c->kind = kind;
    c->hotkey = hotkey;
    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;
    c->pressed = 0;
}

static unsigned short bitsOf(int kind) {
    switch (kind) {
    case TOUCH_CROSS: return B_CROSS;
    case TOUCH_CIRCLE: return B_CIRCLE;
    case TOUCH_SQUARE: return B_SQUARE;
    case TOUCH_TRIANGLE: return B_TRIANGLE;
    case TOUCH_L1: return B_L1;
    case TOUCH_L2: return B_L2;
    case TOUCH_R1: return B_R1;
    case TOUCH_R2: return B_R2;
    case TOUCH_SELECT: return B_SELECT;
    case TOUCH_START: return B_START;
    default: return 0;
    }
}

/* The D-pad's directions for a point (dx, dy) from its centre, r its half size. */
static int dpadDirections(float dx, float dy, float r) {
    if (dx * dx + dy * dy < (0.18f * r) * (0.18f * r)) return 0;
    int d = 0;
    float ax = fabsf(dx), ay = fabsf(dy);
    /* eight sectors: a diagonal within 22.5 degrees of 45 (tan 22.5 = 0.414) */
    if (ay > 0.414f * ax) d |= dy < 0 ? 1 : 4;
    if (ax > 0.414f * ay) d |= dx > 0 ? 2 : 8;
    return d;
}

static unsigned short dpadBits(int d) {
    return (unsigned short)((d & 1 ? B_UP : 0) | (d & 2 ? B_RIGHT : 0) | (d & 4 ? B_DOWN : 0) | (d & 8 ? B_LEFT : 0));
}

static int buildLayout(TouchControl* out, int max, float w, float h) {
    int n = 0;
    float u = (w < h ? w : h) / 100.0f * touchSizePercent() / 100.0f;
    float base = h - 27 * u;
    add(out, &n, max, TOUCH_DPAD, 0, 24 * u, base, 34 * u, 34 * u);
    float fx = w - 24 * u, d = 11.5f * u, size = 13 * u;
    add(out, &n, max, TOUCH_CROSS, 0, fx, base + d, size, size);
    add(out, &n, max, TOUCH_CIRCLE, 0, fx + d, base, size, size);
    add(out, &n, max, TOUCH_SQUARE, 0, fx - d, base, size, size);
    add(out, &n, max, TOUCH_TRIANGLE, 0, fx, base - d, size, size);
    add(out, &n, max, TOUCH_L2, 0, 13 * u, 7 * u, 20 * u, 8.5f * u);
    add(out, &n, max, TOUCH_L1, 0, 13 * u, 17.5f * u, 20 * u, 8.5f * u);
    add(out, &n, max, TOUCH_R2, 0, w - 13 * u, 7 * u, 20 * u, 8.5f * u);
    add(out, &n, max, TOUCH_R1, 0, w - 13 * u, 17.5f * u, 20 * u, 8.5f * u);
    add(out, &n, max, TOUCH_SELECT, 0, w / 2 - 10 * u, h - 6 * u, 15 * u, 6.5f * u);
    add(out, &n, max, TOUCH_START, 0, w / 2 + 10 * u, h - 6 * u, 15 * u, 6.5f * u);
    add(out, &n, max, TOUCH_HOTKEYS, 0, w / 2 - 6.5f * u, 6 * u, 9 * u, 9 * u);
    add(out, &n, max, TOUCH_MENU, 0, w / 2 + 6.5f * u, 6 * u, 9 * u, 9 * u);
    if (rowOpen) {
        float bw = 13 * u, gap = 1.5f * u, total = HOTKEYS * bw + (HOTKEYS - 1) * gap;
        for (int i = 0; i < HOTKEYS; i++)
            add(out, &n, max, TOUCH_HOTKEY, i, w / 2 - total / 2 + bw / 2 + i * (bw + gap), 17 * u, bw, 7.5f * u);
    }
    return n;
}

static int inside(const TouchControl* c, float x, float y, float margin) {
    if (c->kind >= TOUCH_CROSS && c->kind <= TOUCH_TRIANGLE) {
        float r = c->w / 2 + margin, dx = x - c->x, dy = y - c->y;
        return dx * dx + dy * dy <= r * r;
    }
    return fabsf(x - c->x) <= c->w / 2 + margin && fabsf(y - c->y) <= c->h / 2 + margin;
}

/* The control at (x, y): the face buttons by the nearest, the others by their area. */
static int hit(const TouchControl* cs, int n, float x, float y, int faceOnly) {
    float u = (winW < winH ? winW : winH) / 100.0f;
    int best = -1;
    float bestDist = 0;
    for (int i = 0; i < n; i++) {
        const TouchControl* c = &cs[i];
        int face = c->kind >= TOUCH_CROSS && c->kind <= TOUCH_TRIANGLE;
        if (faceOnly && !face) continue;
        if (face) {
            float dx = x - c->x, dy = y - c->y, dist = dx * dx + dy * dy, r = c->w * 0.75f;
            if (dist <= r * r && (best < 0 || dist < bestDist)) {
                best = i;
                bestDist = dist;
            }
        } else if (best < 0 && inside(c, x, y, c->kind == TOUCH_DPAD ? 6 * u : 1.5f * u)) {
            best = i;
        }
    }
    return best;
}

int touchControls(TouchControl* out, int max, float w, float h) {
    int n = buildLayout(out, max, w, h);
    float sx = winW > 0 ? w / winW : 1, sy = winH > 0 ? h / winH : 1;
    for (int f = 0; f < FINGERS; f++) {
        if (!fingers[f].used || fingers[f].control < 0) continue;
        for (int i = 0; i < n; i++) {
            TouchControl* c = &out[i];
            if (c->kind != fingers[f].kind || c->hotkey != fingers[f].hotkey) continue;
            if (c->kind == TOUCH_DPAD) {
                c->pressed |= dpadDirections(fingers[f].x * sx - c->x, fingers[f].y * sy - c->y, c->w / 2);
            } else {
                c->pressed = 1;
            }
        }
    }
    return n;
}

/* ---- fingers ------------------------------------------------------------------------ */

static void windowSize(SDL_WindowID id) {
    SDL_Window* win = id ? SDL_GetWindowFromID(id) : NULL;
    if (win == NULL) {
        int count = 0;
        SDL_Window** all = SDL_GetWindows(&count);
        win = all != NULL && count > 0 ? all[0] : NULL;
        SDL_free(all);
    }
    int w = 0, h = 0;
    if (win != NULL && SDL_GetWindowSizeInPixels(win, &w, &h) && w > 0 && h > 0) {
        winW = (float)w;
        winH = (float)h;
    }
}

static void fingerDown(SDL_FingerID id, float x, float y) {
    autoShown = 1;
    int f = 0;
    while (f < FINGERS && fingers[f].used && fingers[f].id != id) f++;
    if (f == FINGERS) return;
    TouchControl cs[MAX_CONTROLS];
    int n = buildLayout(cs, MAX_CONTROLS, winW, winH);
    int i = hit(cs, n, x, y, 0);
    fingers[f].id = id;
    fingers[f].used = 1;
    fingers[f].control = i;
    fingers[f].kind = i >= 0 ? cs[i].kind : -1;
    fingers[f].hotkey = i >= 0 ? cs[i].hotkey : 0;
    fingers[f].x = x;
    fingers[f].y = y;
}

static void fingerMove(SDL_FingerID id, float x, float y) {
    for (int f = 0; f < FINGERS; f++) {
        if (!fingers[f].used || fingers[f].id != id) continue;
        fingers[f].x = x;
        fingers[f].y = y;
        int k = fingers[f].kind;
        if (k >= TOUCH_CROSS && k <= TOUCH_TRIANGLE) {
            /* sliding over the face buttons: the one under the finger now */
            TouchControl cs[MAX_CONTROLS];
            int n = buildLayout(cs, MAX_CONTROLS, winW, winH);
            int i = hit(cs, n, x, y, 1);
            if (i >= 0) fingers[f].kind = cs[i].kind;
        }
    }
}

static void fingerUp(SDL_FingerID id, float x, float y, int cancelled) {
    for (int f = 0; f < FINGERS; f++) {
        if (!fingers[f].used || fingers[f].id != id) continue;
        fingers[f].used = 0;
        int k = fingers[f].kind;
        if (cancelled || (k != TOUCH_MENU && k != TOUCH_HOTKEYS && k != TOUCH_HOTKEY)) continue;
        TouchControl cs[MAX_CONTROLS];
        int n = buildLayout(cs, MAX_CONTROLS, winW, winH);
        int i = hit(cs, n, x, y, 0);
        if (i < 0 || cs[i].kind != k || cs[i].hotkey != fingers[f].hotkey) continue; /* lifted elsewhere */
        if (k == TOUCH_MENU && uiRequestToggle != NULL) {
            uiRequestToggle();
        } else if (k == TOUCH_HOTKEYS) {
            rowOpen = !rowOpen;
        } else if (k == TOUCH_HOTKEY && padHotkey != NULL) {
            padHotkey(hotkeys[fingers[f].hotkey].scancode, hotkeys[fingers[f].hotkey].mod);
        }
    }
}

static void releaseAll(void) {
    for (int f = 0; f < FINGERS; f++) fingers[f].used = 0;
}

int touchEvent(const SDL_Event* e) {
    switch (e->type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        if (touchMode() == 0) return 0;
        windowSize(e->tfinger.windowID);
        if (e->type == SDL_EVENT_FINGER_DOWN) {
            fingerDown(e->tfinger.fingerID, e->tfinger.x * winW, e->tfinger.y * winH);
        } else if (e->type == SDL_EVENT_FINGER_MOTION) {
            fingerMove(e->tfinger.fingerID, e->tfinger.x * winW, e->tfinger.y * winH);
        } else {
            fingerUp(e->tfinger.fingerID, e->tfinger.x * winW, e->tfinger.y * winH,
                     e->type == SDL_EVENT_FINGER_CANCELED);
        }
        return 1;
    case SDL_EVENT_MOUSE_MOTION:
        return e->motion.which == SDL_TOUCH_MOUSEID && touchMode() != 0;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return e->button.which == SDL_TOUCH_MOUSEID && touchMode() != 0;
    case SDL_EVENT_MOUSE_WHEEL:
        return e->wheel.which == SDL_TOUCH_MOUSEID && touchMode() != 0;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        releaseAll(); /* no finger lifts while the app is away */
        return 0;
    default:
        return 0;
    }
}

void touchInputSeen(const SDL_Event* e) {
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        /* a keyboard (a key of the game's or a hotkey); the system's Back stays with the touches */
        if (e->key.scancode != SDL_SCANCODE_AC_BACK) autoShown = 0;
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        autoShown = 0;
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (e->gaxis.value > 16000 || e->gaxis.value < -16000) autoShown = 0;
        break;
    default:
        break;
    }
}

/* ---- the scripted touches (XG_TOUCH_SCRIPT) ---------------------------------------- */

typedef struct {
    unsigned vblank;
    int what; /* 0 down, 1 move, 2 up */
    int finger;
    float x, y;
} Step;

static Step* steps;
static int stepCount, stepAt, scriptRead;

static void readScript(void) {
    scriptRead = 1;
    const char* path = SDL_getenv("XG_TOUCH_SCRIPT");
    if (path == NULL || path[0] == '\0') return;
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "touch: cannot open %s\n", path);
        return;
    }
    char line[200], what[8];
    int cap = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        Step s;
        if (line[0] == '#' || sscanf(line, "%u %7s %d %f %f", &s.vblank, what, &s.finger, &s.x, &s.y) != 5) continue;
        s.what = strcmp(what, "down") == 0 ? 0 : strcmp(what, "move") == 0 ? 1 : strcmp(what, "up") == 0 ? 2 : -1;
        if (s.what < 0) continue;
        if (stepCount == cap) {
            cap = cap ? cap * 2 : 32;
            Step* grown = realloc(steps, (size_t)cap * sizeof *steps);
            if (grown == NULL) break;
            steps = grown;
        }
        steps[stepCount++] = s;
    }
    fclose(f);
    fprintf(stderr, "touch: %d scripted touches from %s\n", stepCount, path);
}

static void runScript(void) {
    if (!scriptRead) readScript();
    unsigned now = hwVSyncCount != NULL ? hwVSyncCount() : 0;
    while (stepAt < stepCount && steps[stepAt].vblank <= now) {
        const Step* s = &steps[stepAt++];
        SDL_Event e;
        memset(&e, 0, sizeof e);
        e.type = s->what == 0 ? SDL_EVENT_FINGER_DOWN : s->what == 1 ? SDL_EVENT_FINGER_MOTION : SDL_EVENT_FINGER_UP;
        e.tfinger.fingerID = (SDL_FingerID)(s->finger + 1);
        e.tfinger.x = s->x;
        e.tfinger.y = s->y;
        if (uiIsOpen != NULL && uiIsOpen()) continue; /* the menu takes its own script (XG_UI_SCRIPT) */
        touchEvent(&e);
    }
}

unsigned short touchVBlank(void) {
    runScript();
    if (winW <= 0) windowSize(0);
    unsigned short bits = 0;
    TouchControl cs[MAX_CONTROLS];
    int n = touchControls(cs, MAX_CONTROLS, winW, winH);
    for (int i = 0; i < n; i++) {
        if (!cs[i].pressed) continue;
        bits |= cs[i].kind == TOUCH_DPAD ? dpadBits(cs[i].pressed) : bitsOf(cs[i].kind);
    }
    return bits;
}
