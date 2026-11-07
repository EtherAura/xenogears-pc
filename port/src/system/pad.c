/*
 * Pads (LIBAPI InitPAD/StartPAD, the BIOS pad reader) from SDL keyboard and
 * gamepads, and the window's event pump. Both run in the vblank interrupt,
 * before the game's vsync callback, as the BIOS reads the pads on the PS1.
 *
 * Each pad reports a digital pad: status 0, type 0x41, two bytes of buttons,
 * active low (bit order: Select L3 R3 Start Up Right Down Left, then L2 R2 L1
 * R1 Triangle Circle Cross Square). Port 1 is the keyboard and the first
 * gamepad; port 2 the second gamepad (no controller without one).
 *
 * Keyboard:  arrows      D-pad          X  Cross       C  Circle
 *            Z  Square   S  Triangle    Q  L1          E  R1
 *            1  L2       3  R2          Return  Start  Backspace or Right Shift  Select
 *            (the defaults: settings.ini key_<button>=<SDL key name>[,<second>],
 *            e.g. key_cross=X, key_select=Backspace,Right Shift; "none")
 *            F1  the settings menu (port/src/ui; the game pauses while it is open)
 *            F2  swap discs when the game asks for one (port/src/cd/swap.c)
 *            Tab in a battle, with Battle speed on: 1x, 2x, 4x (battlespeed.c,
 *                battle_speed_key; the pad's Select+R2 too, battle_speed_button)
 * Display:   F3  aspect: 4:3, 1:1 (square pixels), 16:9, 16:10, 21:9, 32:9
 *            F4  scale: fit, integer, stretch
 *            Shift+F4  resolution: exact 1x, or back to the upscaled
 *                choice (auto at first; port/src/gpu/hires.c)
 *            F5  filter: nearest, linear, sharp, bicubic, xbr
 *            F6  wide fill, for a 4:3 picture in a wide frame (movies, screens
 *                that stay 4:3): bars, zoom (scaled to the frame's width,
 *                top and bottom cut), blur
 *            F11 or Alt+Enter  fullscreen (that Return is not Start)
 *            Each is kept in settings.ini and noted in the window's corner
 *            (port/src/gpu/present.c).
 * Gamepad:   by position (south Cross, east Circle, west Square, north
 *            Triangle), shoulders L1/R1, triggers L2/R2, stick clicks L3/R3,
 *            back Select; the left stick also drives the D-pad (the defaults:
 *            pad_<button>=<SDL gamepad button name, lefttrigger, righttrigger
 *            or none>, pad_stick_dpad=1). The buttons are <button>: up down
 *            left right cross circle square triangle l1 r1 l2 r2 start select
 *            l3 r3. The settings menu rebinds them (pad.h). Guide (Home,
 *            PS), or both stick clicks together, opens the settings menu.
 *            After the menu closes, the keyboard and gamepads read as
 *            released until every button is let go (scripts are not held).
 * Mouse:     left button confirm, right cancel, wheel the D-pad, side
 *            buttons L1/R1 (mouse.c; mouse_game=0 turns it off).
 *
 * XG_PAD_SCRIPT=file: scripted input for headless runs, replacing live input
 * on port 1 and disconnecting port 2. Each line is "<vsync> <buttons>": from that vblank on,
 * port 1 holds those buttons (hex, the game's active-high bit order:
 * 0x0800 Start, 0x0040 Cross, 0x1000 Up...). '#' starts a comment. A line
 * "<vsync> key <name>" presses a hotkey at that vblank (an SDL key name,
 * "Alt+" or "Shift+" allowed: "900 key F3", "1200 key Alt+Return").
 *
 * XG_PAD_SCRIPT2=file: the same for port 2 (the second player: Battling's
 * PLAYER1 VS PLAYER2, the Battle arena, port/src/system/arena.h); with it
 * port 2 is connected and reads only the script.
 *
 * padSetKeyboardPort(1): the keyboard plays on port 2 instead of port 1 (the
 * Battle arena's second player on the keyboard while the first has the
 * gamepad); port 2 is connected then. Not saved; padSetKeyboardPort(0) puts
 * it back.
 *
 * XG_SLOWDOWN_PAD=file: while the slowdown replay is lined up with its log
 * (port/src/system/slowdown.c), port 1 reads this script instead, on the
 * log's vblanks: the reference run's own pad script.
 *
 * SendPAD registers the actuator buffers the game updates each frame; its
 * motor and the port's own feedback become gamepad rumble (rumble.c,
 * settings.ini vibration=, vibration_strength=).
 *
 * pad_log=<file> (settings.ini, or XG_PAD_LOG): a diagnostic log of the
 * input as the port sees it: every keyboard event SDL delivers ("key
 * <vblank> <scancode name> down|up[ repeat]") and port 1's buttons at each
 * vblank they change ("vb <vblank> <buttons> kbd=<keyboard's> pad=<gamepad's>
 * mouse=<mouse's>", hex, the game's active-high bit order). For keys that
 * seem to go missing between a streaming client, the compositor and the
 * game.
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "cd/cd.h"
#include "common.h"
#include "gpu/gpu.h"
#include "mouse.h"
#include "rumble.h"
#include "pad.h"
#include "savestate.h"
#include "settings.h"
#include "netplay.h"
#include "system.h"
#include "ui/ui.h"
#include "gpu/shaders/prompt.glsl"

#define BTN_L2 0x0001
#define BTN_R2 0x0002
#define BTN_L1 0x0004
#define BTN_R1 0x0008
#define BTN_TRIANGLE 0x0010
#define BTN_CIRCLE 0x0020
#define BTN_CROSS 0x0040
#define BTN_SQUARE 0x0080
#define BTN_SELECT 0x0100
#define BTN_L3 0x0200
#define BTN_R3 0x0400
#define BTN_START 0x0800
#define BTN_UP 0x1000
#define BTN_RIGHT 0x2000
#define BTN_DOWN 0x4000
#define BTN_LEFT 0x8000

#define TRIGGER_THRESHOLD 8192

static u8* padBuffer[2];
static s32 padLength[2];
static SDL_JoystickID promptPad;
static int padStarted;
static FILE* padLog;     /* pad_log / XG_PAD_LOG (none: NULL) */
static int padLogChecked;
static u16 padLogLast = 0xFFFF;

static void padLogOpen(void) {
    if (padLogChecked) return;
    padLogChecked = 1;
    const char* path = settingsGet("pad_log");
    if (path != NULL && path[0] != '\0' && strcmp(path, "0") != 0 && strcmp(path, "none") != 0) {
        padLog = fopen(path, "w");
        if (padLog != NULL) {
            fprintf(padLog, "# xenogears pad log: key <vblank> <scancode> down|up[ repeat]; vb <vblank> <port1> kbd= pad= mouse=\n");
            fflush(padLog);
        }
    }
}
static u8* actuator[2];
static SDL_Gamepad* gamepads[2];
static int returnTaken; /* Return went down as Alt+Enter: not Start until released */
static int heldAfterMenu; /* the settings menu closed: real input reads released until let go */

typedef struct {
    int64_t vsync;
    u16 buttons;
} ScriptStep;

typedef struct {
    ScriptStep* steps;
    int count;
    int pos;
} Script;

static Script script;       /* XG_PAD_SCRIPT, on this run's vblanks */
static Script replayScript; /* XG_SLOWDOWN_PAD, on the replayed log's vblanks */
static Script script2;      /* XG_PAD_SCRIPT2: port 2, on this run's vblanks */
static int keyboardPort;    /* the port the keyboard plays on (padSetKeyboardPort) */

/* XG_PAD_SCRIPT's hotkeys. */
static struct {
    int64_t vsync;
    SDL_Scancode key;
    SDL_Keymod mod;
} *scriptKeys;
static int scriptKeyCount;
static int scriptKeyPos;

/* The PS1 buttons and their default keys and gamepad inputs (pad.h). */
static const struct {
    const char* name; /* settings: key_<name>, pad_<name> */
    const char* label;
    u16 bit;
    SDL_Scancode keys[2];
    int pad; /* SDL_GamepadButton or PAD_INPUT_* */
} buttonInfo[PAD_BUTTONS] = {
    {"up", "Up", BTN_UP, {SDL_SCANCODE_UP}, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"down", "Down", BTN_DOWN, {SDL_SCANCODE_DOWN}, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"left", "Left", BTN_LEFT, {SDL_SCANCODE_LEFT}, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"right", "Right", BTN_RIGHT, {SDL_SCANCODE_RIGHT}, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {"cross", "Cross", BTN_CROSS, {SDL_SCANCODE_X}, SDL_GAMEPAD_BUTTON_SOUTH},
    {"circle", "Circle", BTN_CIRCLE, {SDL_SCANCODE_C}, SDL_GAMEPAD_BUTTON_EAST},
    {"square", "Square", BTN_SQUARE, {SDL_SCANCODE_Z}, SDL_GAMEPAD_BUTTON_WEST},
    {"triangle", "Triangle", BTN_TRIANGLE, {SDL_SCANCODE_S}, SDL_GAMEPAD_BUTTON_NORTH},
    {"l1", "L1", BTN_L1, {SDL_SCANCODE_Q}, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"r1", "R1", BTN_R1, {SDL_SCANCODE_E}, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"l2", "L2", BTN_L2, {SDL_SCANCODE_1}, PAD_INPUT_LEFT_TRIGGER},
    {"r2", "R2", BTN_R2, {SDL_SCANCODE_3}, PAD_INPUT_RIGHT_TRIGGER},
    {"start", "Start", BTN_START, {SDL_SCANCODE_RETURN}, SDL_GAMEPAD_BUTTON_START},
    {"select", "Select", BTN_SELECT, {SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_RSHIFT}, SDL_GAMEPAD_BUTTON_BACK},
    {"l3", "L3", BTN_L3, {SDL_SCANCODE_UNKNOWN}, SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {"r3", "R3", BTN_R3, {SDL_SCANCODE_UNKNOWN}, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
};

typedef struct {
    int pad[PAD_BUTTONS];
    int stickDpad;
    int deadzone; /* percent; 50 gives the original 16384 threshold */
    char prefix[64];
} GamepadBindings;

/* Keyboard and the fallback mapping; device profiles inherit this mapping
 * until a player customises them. The runtime instance ID is never saved. */
static struct {
    int loaded;
    SDL_Scancode keys[PAD_BUTTONS][PAD_KEYS];
    GamepadBindings gamepad;
} bind;
static GamepadBindings profiles[2];
static int selectedProfile;

/* ---- bindings ---- */

static void loadBindings(void) {
    if (bind.loaded) {
        return;
    }
    bind.loaded = 1;
    settingsDescribe("key_up", "key_<button>, pad_<button>: the keys (SDL key names, two at most, separated by a "
                               "comma, or none) and the gamepad input (an SDL gamepad button name, lefttrigger, "
                               "righttrigger or none) for each PS1 button");
    settingsDescribe("pad_stick_dpad", "pad_stick_dpad: 1 if the left stick also drives the D-pad");
    for (int i = 0; i < PAD_BUTTONS; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key_%s", buttonInfo[i].name);
        const char* v = settingsGet(key);
        bind.keys[i][0] = buttonInfo[i].keys[0];
        bind.keys[i][1] = buttonInfo[i].keys[1];
        if (v != NULL) {
            bind.keys[i][0] = bind.keys[i][1] = SDL_SCANCODE_UNKNOWN;
            char text[96];
            snprintf(text, sizeof(text), "%s", v);
            char* save = NULL;
            int n = 0;
            for (char* t = SDL_strtok_r(text, ",", &save); t != NULL && n < PAD_KEYS; t = SDL_strtok_r(NULL, ",", &save)) {
                while (*t == ' ') {
                    t++;
                }
                SDL_Scancode code = SDL_strcasecmp(t, "none") == 0 ? SDL_SCANCODE_UNKNOWN : SDL_GetScancodeFromName(t);
                if (code == SDL_SCANCODE_UNKNOWN && SDL_strcasecmp(t, "none") != 0) {
                    fprintf(stderr, "pad: %s: unknown key '%s'\n", key, t);
                }
                if (code != SDL_SCANCODE_UNKNOWN) {
                    bind.keys[i][n++] = code;
                }
            }
        }
        snprintf(key, sizeof(key), "pad_%s", buttonInfo[i].name);
        v = settingsGet(key);
        bind.gamepad.pad[i] = buttonInfo[i].pad;
        if (v != NULL) {
            int input = padInputFromName(v);
            if (input == PAD_INPUT_NONE && SDL_strcasecmp(v, "none") != 0) {
                fprintf(stderr, "pad: %s: unknown gamepad input '%s'\n", key, v);
            } else {
                bind.gamepad.pad[i] = input;
            }
        }
    }
    bind.gamepad.stickDpad = settingsBool("pad_stick_dpad", 1);
    bind.gamepad.deadzone = SDL_clamp(settingsInt("pad_stick_deadzone", 50), 0, 95);
    settingsDescribe("pad_stick_deadzone", "pad_stick_deadzone: left-stick D-pad threshold in percent (0..95, default 50)");
}

static void profileKey(const GamepadBindings* profile, const char* name, char* out, size_t size) {
    snprintf(out, size, "pad_%s%s", profile->prefix, name);
}

static void loadProfile(int port) {
    char prefix[64], key[96];
    snprintf(prefix, sizeof(prefix), "%s", profiles[port].prefix);
    profiles[port] = bind.gamepad;
    snprintf(profiles[port].prefix, sizeof(profiles[port].prefix), "%s", prefix);
    GamepadBindings* profile = &profiles[port];
    for (int b = 0; b < PAD_BUTTONS; b++) {
        profileKey(profile, buttonInfo[b].name, key, sizeof(key));
        const char* value = settingsGet(key);
        if (value != NULL && value[0] != '\0') {
            int input = padInputFromName(value);
            if (input != PAD_INPUT_NONE || SDL_strcasecmp(value, "none") == 0) {
                profile->pad[b] = input;
            }
        }
    }
    profileKey(profile, "stick_dpad", key, sizeof(key));
    profile->stickDpad = settingsBool(key, bind.gamepad.stickDpad);
    profileKey(profile, "stick_deadzone", key, sizeof(key));
    profile->deadzone = SDL_clamp(settingsInt(key, bind.gamepad.deadzone), 0, 95);
}

static GamepadBindings* selectedBindings(void) {
    loadBindings();
    return selectedProfile >= 0 && gamepads[selectedProfile] != NULL ? &profiles[selectedProfile] : &bind.gamepad;
}

static void saveProfile(GamepadBindings* profile) {
    char key[96];
    for (int b = 0; b < PAD_BUTTONS; b++) {
        profileKey(profile, buttonInfo[b].name, key, sizeof(key));
        settingsSet(key, padInputName(profile->pad[b]));
    }
    profileKey(profile, "stick_dpad", key, sizeof(key));
    settingsSetInt(key, profile->stickDpad);
    profileKey(profile, "stick_deadzone", key, sizeof(key));
    settingsSetInt(key, profile->deadzone);
    settingsSave();
    /* Identical models without serial numbers deliberately share a profile;
     * default edits also reach devices that have no saved overrides. */
    for (int i = 0; i < 2; i++) {
        if (gamepads[i] != NULL) {
            loadProfile(i);
        }
    }
}

static void saveKeys(int button) {
    char key[32], value[96] = "";
    snprintf(key, sizeof(key), "key_%s", buttonInfo[button].name);
    for (int k = 0; k < PAD_KEYS; k++) {
        if (bind.keys[button][k] != SDL_SCANCODE_UNKNOWN) {
            size_t n = strlen(value);
            snprintf(value + n, sizeof(value) - n, "%s%s", n > 0 ? "," : "", SDL_GetScancodeName(bind.keys[button][k]));
        }
    }
    settingsSet(key, value[0] != '\0' ? value : "none");
}

const char* padButtonName(int button) {
    return buttonInfo[button].name;
}

const char* padButtonLabel(int button) {
    return buttonInfo[button].label;
}

SDL_Scancode padKey(int button, int slot) {
    loadBindings();
    return bind.keys[button][slot];
}

int padKeyReserved(SDL_Scancode key) {
    return (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12) || key == SDL_SCANCODE_ESCAPE;
}

void padSetKey(int button, int slot, SDL_Scancode key) {
    loadBindings();
    if (key != SDL_SCANCODE_UNKNOWN && padKeyReserved(key)) {
        return;
    }
    /* A key drives one button: taken from wherever else it was. */
    for (int i = 0; i < PAD_BUTTONS && key != SDL_SCANCODE_UNKNOWN; i++) {
        for (int k = 0; k < PAD_KEYS; k++) {
            if (bind.keys[i][k] == key && !(i == button && k == slot)) {
                bind.keys[i][k] = SDL_SCANCODE_UNKNOWN;
                saveKeys(i);
            }
        }
    }
    bind.keys[button][slot] = key;
    if (slot == 1 && bind.keys[button][0] == SDL_SCANCODE_UNKNOWN) {
        bind.keys[button][0] = key; /* the first key first */
        bind.keys[button][1] = SDL_SCANCODE_UNKNOWN;
    }
    saveKeys(button);
    settingsSave();
}

int padGamepadInput(int button) {
    return selectedBindings()->pad[button];
}

void padSetGamepadInput(int button, int input) {
    GamepadBindings* profile = selectedBindings();
    for (int i = 0; i < PAD_BUTTONS && input != PAD_INPUT_NONE; i++) {
        if (i != button && profile->pad[i] == input) {
            profile->pad[i] = PAD_INPUT_NONE;
        }
    }
    profile->pad[button] = input;
    saveProfile(profile);
}

int padStickDpad(void) {
    return selectedBindings()->stickDpad;
}

void padSetStickDpad(int on) {
    GamepadBindings* profile = selectedBindings();
    profile->stickDpad = on != 0;
    saveProfile(profile);
}

int padStickDeadzone(void) {
    return selectedBindings()->deadzone;
}

void padSetStickDeadzone(int percent) {
    GamepadBindings* profile = selectedBindings();
    profile->deadzone = SDL_clamp(percent, 0, 95);
    saveProfile(profile);
}

int padProfilePort(void) {
    return selectedProfile >= 0 && gamepads[selectedProfile] != NULL ? selectedProfile : -1;
}

void padSelectProfile(int port) {
    if (port >= -1 && port < 2) {
        selectedProfile = port;
    }
}

SDL_JoystickID padProfileDevice(int port) {
    return port >= 0 && port < 2 && gamepads[port] != NULL ? SDL_GetGamepadID(gamepads[port]) : 0;
}

const char* padProfileName(int port) {
    return port >= 0 && port < 2 && gamepads[port] != NULL ? SDL_GetGamepadName(gamepads[port]) : "Default mapping";
}

void padResetKeys(void) {
    loadBindings();
    for (int i = 0; i < PAD_BUTTONS; i++) {
        bind.keys[i][0] = buttonInfo[i].keys[0];
        bind.keys[i][1] = buttonInfo[i].keys[1];
        saveKeys(i);
    }
    settingsSave();
}

void padResetGamepad(void) {
    GamepadBindings* profile = selectedBindings();
    if (profile == &bind.gamepad) {
        for (int i = 0; i < PAD_BUTTONS; i++) {
            profile->pad[i] = buttonInfo[i].pad;
        }
        profile->stickDpad = 1;
        profile->deadzone = 50;
        saveProfile(profile);
    } else {
        char key[96];
        for (int i = 0; i < PAD_BUTTONS; i++) {
            profileKey(profile, buttonInfo[i].name, key, sizeof(key));
            settingsSet(key, ""); /* empty: inherit the default mapping */
        }
        profileKey(profile, "stick_dpad", key, sizeof(key));
        settingsSet(key, "");
        profileKey(profile, "stick_deadzone", key, sizeof(key));
        settingsSet(key, "");
        settingsSave();
        for (int i = 0; i < 2; i++) {
            if (gamepads[i] != NULL) {
                loadProfile(i);
            }
        }
    }
}

const char* padInputName(int input) {
    if (input == PAD_INPUT_LEFT_TRIGGER) {
        return SDL_GetGamepadStringForAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    }
    if (input == PAD_INPUT_RIGHT_TRIGGER) {
        return SDL_GetGamepadStringForAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    }
    const char* name = input >= 0 ? SDL_GetGamepadStringForButton((SDL_GamepadButton)input) : NULL;
    return name != NULL ? name : "none";
}

int padInputFromName(const char* name) {
    if (SDL_GetGamepadAxisFromString(name) == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
        return PAD_INPUT_LEFT_TRIGGER;
    }
    if (SDL_GetGamepadAxisFromString(name) == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        return PAD_INPUT_RIGHT_TRIGGER;
    }
    SDL_GamepadButton b = SDL_GetGamepadButtonFromString(name);
    return b != SDL_GAMEPAD_BUTTON_INVALID ? (int)b : PAD_INPUT_NONE;
}

static const char* inputLabelFor(SDL_Gamepad* pad, int input) {
    SDL_GamepadType type = pad != NULL ? SDL_GetGamepadType(pad) : SDL_GAMEPAD_TYPE_UNKNOWN;
    int ps = type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5;
    int nintendo = type >= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && type <= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
    int xbox = type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE || type == SDL_GAMEPAD_TYPE_STANDARD;
    if (input >= SDL_GAMEPAD_BUTTON_SOUTH && input <= SDL_GAMEPAD_BUTTON_NORTH) {
        static const char* const labels[] = {NULL, "A", "B", "X", "Y", "Cross", "Circle", "Square", "Triangle"};
        SDL_GamepadButtonLabel label = pad != NULL ? SDL_GetGamepadButtonLabel(pad, (SDL_GamepadButton)input)
                                                   : SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
        if (label > SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN && label <= SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE) {
            return labels[label];
        }
        static const char* const positions[] = {"South", "East", "West", "North"};
        return positions[input];
    }
    switch (input) {
    case PAD_INPUT_NONE: return "-";
    case PAD_INPUT_LEFT_TRIGGER: return ps ? "L2" : nintendo ? "ZL" : xbox ? "LT" : "Left trigger";
    case PAD_INPUT_RIGHT_TRIGGER: return ps ? "R2" : nintendo ? "ZR" : xbox ? "RT" : "Right trigger";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ps ? "L1" : nintendo ? "L" : xbox ? "LB" : "Left shoulder";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ps ? "R1" : nintendo ? "R" : xbox ? "RB" : "Right shoulder";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return ps ? "L3" : "Left stick click";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return ps ? "R3" : "Right stick click";
    case SDL_GAMEPAD_BUTTON_BACK: return type == SDL_GAMEPAD_TYPE_PS5 ? "Create" : type == SDL_GAMEPAD_TYPE_PS4 ? "Share"
                                      : ps ? "Select" : nintendo ? "Minus" : type == SDL_GAMEPAD_TYPE_XBOXONE ? "View" : "Back";
    case SDL_GAMEPAD_BUTTON_START: return type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5 ? "Options"
                                       : nintendo ? "Plus" : type == SDL_GAMEPAD_TYPE_XBOXONE ? "Menu" : "Start";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-pad up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-pad down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-pad left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad right";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "Guide";
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
    case SDL_GAMEPAD_BUTTON_MISC1: return type == SDL_GAMEPAD_TYPE_PS5 ? "Microphone"
                                        : nintendo ? "Capture" : type == SDL_GAMEPAD_TYPE_XBOXONE ? "Share" : "Misc 1";
    default: return padInputName(input);
    }
}

const char* padInputLabel(int input) {
    int port = padProfilePort();
    return inputLabelFor(port >= 0 ? gamepads[port] : NULL, input);
}

static int promptPort(void) {
    for (int i=0;i<2;++i)
        if (gamepads[i] && SDL_GetGamepadID(gamepads[i]) == promptPad) return i;
    return gamepads[0] != NULL ? 0 : gamepads[1] != NULL ? 1 : -1;
}

const char* padActionLabel(const char* button) {
    int port = promptPort();
    if (port < 0) {
        return NULL;
    }
    for (int b = 0; b < PAD_BUTTONS; b++) {
        if (strcmp(buttonInfo[b].name, button) == 0) {
            return inputLabelFor(gamepads[port], profiles[port].pad[b]);
        }
    }
    return NULL;
}

static int inputGlyphFor(SDL_Gamepad* pad, int input) {
    static const char* const labels[] = {
        "Circle", "Cross", "Triangle", "Square", "A", "B", "X", "Y",
        "L1", "R1", "L2", "R2", "LB", "RB", "LT", "RT", "L", "R", "ZL", "ZR",
        "L3", "R3", "Start", "Select", "Back", "Menu", "Options", "Share", "Create", "Plus", "Minus",
        "D-pad up", "D-pad down", "D-pad left", "D-pad right", "South", "East", "West", "North", "-"
    };
    const char* label = inputLabelFor(pad,input);
    for (int i=0;i<(int)(sizeof(labels)/sizeof(*labels));++i)
        if (!strcmp(label,labels[i])) return i;
    if (input == SDL_GAMEPAD_BUTTON_LEFT_STICK) return PAD_GLYPH_L3;
    if (input == SDL_GAMEPAD_BUTTON_RIGHT_STICK) return PAD_GLYPH_R3;
    if (input == PAD_INPUT_LEFT_TRIGGER) return PAD_GLYPH_LT;
    if (input == PAD_INPUT_RIGHT_TRIGGER) return PAD_GLYPH_RT;
    if (input == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) return PAD_GLYPH_LB;
    if (input == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) return PAD_GLYPH_RB;
    if (input == SDL_GAMEPAD_BUTTON_BACK) return PAD_GLYPH_VIEW;
    if (input == SDL_GAMEPAD_BUTTON_GUIDE) return PAD_GLYPH_GUIDE;
    if (input == SDL_GAMEPAD_BUTTON_TOUCHPAD) return PAD_GLYPH_TOUCHPAD;
    switch (input) {
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return PAD_GLYPH_P1;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return PAD_GLYPH_P2;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return PAD_GLYPH_P3;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return PAD_GLYPH_P4;
    case SDL_GAMEPAD_BUTTON_MISC1:
        if (!strcmp(label,"Microphone")) return PAD_GLYPH_MICROPHONE;
        if (!strcmp(label,"Capture")) return PAD_GLYPH_CAPTURE;
        return PAD_GLYPH_M1;
    case SDL_GAMEPAD_BUTTON_MISC2: return PAD_GLYPH_M2;
    case SDL_GAMEPAD_BUTTON_MISC3: return PAD_GLYPH_M3;
    case SDL_GAMEPAD_BUTTON_MISC4: return PAD_GLYPH_M4;
    case SDL_GAMEPAD_BUTTON_MISC5: return PAD_GLYPH_M5;
    case SDL_GAMEPAD_BUTTON_MISC6: return PAD_GLYPH_M6;
    }
    return PAD_GLYPH_OTHER;
}

int padInputGlyph(int input) {
    int port = padProfilePort();
    return inputGlyphFor(port >= 0 ? gamepads[port] : NULL,input);
}

int padActionGlyph(const char* button) {
    int port = promptPort();
    if (port < 0) return PAD_GLYPH_NONE;
    for (int b=0;b<PAD_BUTTONS;++b)
        if (!strcmp(buttonInfo[b].name,button)) return inputGlyphFor(gamepads[port],profiles[port].pad[b]);
    return PAD_GLYPH_NONE;
}

int pcPadPromptGlyph(int originalSlot) {
    static const char* const buttons[] = {"circle","cross","triangle","square","l1","l2","r1","r2"};
    static const int defaults[] = {PAD_GLYPH_CIRCLE,PAD_GLYPH_CROSS,PAD_GLYPH_TRIANGLE,PAD_GLYPH_SQUARE,
                                  PAD_GLYPH_L1,PAD_GLYPH_L2,PAD_GLYPH_R1,PAD_GLYPH_R2};
    int slot = pcPadPromptSlot(originalSlot);
    if (slot < 0 || slot >= 8) return PAD_GLYPH_OTHER;
    int glyph = padActionGlyph(buttons[slot]);
    return glyph == PAD_GLYPH_NONE ? defaults[slot] : glyph;
}

int pcPadDialogSlot(int originalSlot) {
    int glyph = pcPadPromptGlyph(originalSlot);
    if (glyph >= 0 && glyph < 4) return glyph;
    if (glyph == PAD_GLYPH_L1) return 4;
    if (glyph == PAD_GLYPH_L2) return 5;
    if (glyph == PAD_GLYPH_R1) return 6;
    if (glyph == PAD_GLYPH_R2) return 7;
    return -1;
}

unsigned char* pcPadDialogText(int originalSlot) {
    /* Private PC-only two-byte characters, outside both retail font ranges.
     * Each glyph owns its string, so concurrent dialogue windows retain it. */
    static unsigned char strings[PAD_GLYPH_COUNT][3];
    int glyph = pcPadPromptGlyph(originalSlot);
    strings[glyph][0] = 0xff;
    strings[glyph][1] = 0x80 + glyph;
    return strings[glyph];
}

void pcPadPrintGlyph(unsigned short* out, int stride, int odd, int glyph) {
    unsigned shift = (odd & 1) * 2;
    for (int y=0;y<13;++y) for (int x=0;x<12;++x) {
        unsigned bit = (x & 3) * 4 + shift;
        unsigned short* word = out + y * stride + x / 4;
        *word = (*word & ~(3u << bit)) | (promptIndex(glyph,x,y) << bit);
    }
}

int pcPadIsGlyph(unsigned type, unsigned id) {
    return type == 0xff && id >= 0x80 && id < 0x80 + PAD_GLYPH_COUNT;
}

/* ---- SDL devices ---- */

static void addGamepad(SDL_JoystickID id) {
    for (int i = 0; i < 2; i++) {
        if (gamepads[i] != NULL && SDL_GetGamepadID(gamepads[i]) == id) {
            return;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (gamepads[i] == NULL) {
            gamepads[i] = SDL_OpenGamepad(id);
            if (gamepads[i] != NULL) {
                loadBindings();
                char guid[33];
                SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), guid, sizeof(guid));
                const char* serial = SDL_GetGamepadSerial(gamepads[i]);
                if (serial != NULL && serial[0] != '\0') {
                    uint64_t hash = UINT64_C(14695981039346656037);
                    for (const unsigned char* p = (const unsigned char*)serial; *p; p++) {
                        hash = (hash ^ *p) * UINT64_C(1099511628211);
                    }
                    snprintf(profiles[i].prefix, sizeof(profiles[i].prefix), "%s_%016llx_", guid,
                             (unsigned long long)hash);
                } else {
                    snprintf(profiles[i].prefix, sizeof(profiles[i].prefix), "%s_", guid);
                }
                loadProfile(i);
                fprintf(stderr, "pad: port %d is %s\n", i + 1, SDL_GetGamepadName(gamepads[i]));
            }
            return;
        }
    }
}

static void removeGamepad(SDL_JoystickID id) {
    for (int i = 0; i < 2; i++) {
        if (gamepads[i] != NULL && SDL_GetGamepadID(gamepads[i]) == id) {
            rumbleForget(gamepads[i]);
            SDL_CloseGamepad(gamepads[i]);
            gamepads[i] = NULL;
        }
    }
}

void padDeviceEvent(const SDL_Event* event) {
    if (event->type == SDL_EVENT_GAMEPAD_ADDED) {
        addGamepad(event->gdevice.which);
    } else if (event->type == SDL_EVENT_GAMEPAD_REMOVED) {
        removeGamepad(event->gdevice.which);
        if (promptPad == event->gdevice.which) promptPad=0;
    } else if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        promptPad=event->gbutton.which;
    } else if (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        int value=event->gaxis.value, axis=event->gaxis.axis;
        int threshold=TRIGGER_THRESHOLD;
        for (int i=0;i<2;++i) if (padProfileDevice(i)==event->gaxis.which) {
            if (axis==SDL_GAMEPAD_AXIS_LEFTX || axis==SDL_GAMEPAD_AXIS_LEFTY)
                threshold=profiles[i].deadzone*32768/100;
        }
        if ((axis >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER && value>TRIGGER_THRESHOLD) ||
            (axis < SDL_GAMEPAD_AXIS_LEFT_TRIGGER && (value>threshold || value<-threshold)))
            promptPad=event->gaxis.which;
    }
}

/* port/src/ui/diagnostics.cpp: its hotkeys (the performance overlay, its log, the
 * replacements' A/B comparison: settings.ini keys), ahead of these; 1 when it took
 * the key. Weak: programs without the settings menu. */
extern int diagnosticsHotkey(int scancode, int mod) __attribute__((weak));
/* battlespeed.c: the in-battle battle speed control, its key and its pad
 * button (weak: pad tests link this file without it). */
extern int battleSpeedKey(int scancode, int mod) __attribute__((weak));
extern void battleSpeedPad(unsigned buttons) __attribute__((weak));

static void keyDown(SDL_Scancode key, SDL_Keymod mod) {
    if (diagnosticsHotkey != NULL && diagnosticsHotkey((int)key, (int)mod)) {
        return;
    }
    if (battleSpeedKey != NULL && battleSpeedKey((int)key, (int)mod)) {
        return; /* the battle speed control (battlespeed.c), in a battle only */
    }
    switch (key) {
    case SDL_SCANCODE_F1:
        uiRequestToggle();
        break;
    case SDL_SCANCODE_F2: {
        /* The lid, but only while the game asks for a disc: a swap under the
         * running game breaks its next read (cd/swap.c). */
        int r = cdSwapRequest();
        presentShowNote(r < 0    ? "Disc swap: only when the game asks for a disc"
                        : r == 0 ? "Disc swap: no other disc image (set disc2 in the menu)"
                                 : "Disc swapped");
        break;
    }
    case SDL_SCANCODE_F3:
        presentCycleAspect();
        break;
    case SDL_SCANCODE_F4:
        if (mod & SDL_KMOD_SHIFT) {
            hiresToggleUpscale(); /* the resolution: exact 1x or upscaled */
        } else {
            presentCycleScale();
        }
        break;
    case SDL_SCANCODE_F5:
        presentCycleFilter();
        break;
    case SDL_SCANCODE_F6:
        presentCycleFill();
        break;
    case SDL_SCANCODE_F7:
        /* Save states (savestate.h): F7 save, F8 load (Shift: undo the last
         * load), F9 / Shift+F9 the next / previous slot. */
        if (stateRequestSave != NULL) {
            stateRequestSave(stateSelectedSlot());
        }
        break;
    case SDL_SCANCODE_F8:
        if (stateRequestLoad != NULL) {
            stateRequestLoad((mod & SDL_KMOD_SHIFT) ? STATE_UNDO : stateSelectedSlot());
        }
        break;
    case SDL_SCANCODE_F9:
        if (stateSelectSlot != NULL) {
            stateSelectSlot(stateSelectedSlot() + ((mod & SDL_KMOD_SHIFT) ? -1 : 1));
            stateNoteSlot();
        }
        break;
    case SDL_SCANCODE_F11:
        presentToggleFullscreen();
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        if (mod & SDL_KMOD_ALT) {
            returnTaken |= key == SDL_SCANCODE_RETURN;
            presentToggleFullscreen();
        }
        break;
    default:
        break;
    }
}

/* Guide, or one stick click while the other is held: the settings menu. */
static void menuButton(const SDL_GamepadButtonEvent* b) {
    SDL_Gamepad* pad = SDL_GetGamepadFromID(b->which);
    if (b->button == SDL_GAMEPAD_BUTTON_GUIDE ||
        (pad != NULL && b->button == SDL_GAMEPAD_BUTTON_LEFT_STICK &&
         SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) ||
        (pad != NULL && b->button == SDL_GAMEPAD_BUTTON_RIGHT_STICK &&
         SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK))) {
        uiRequestToggle();
    }
}

/* The window's events; the settings menu takes its input first while open. */
void padPumpEvents(void) {
    SDL_Event e;

    netplayIdle(); /* a netplay session: kept alive through the menu and pauses (netplay.c) */
    while (SDL_PollEvent(&e)) {
        if (padLog != NULL && (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP)) {
            fprintf(padLog, "key %u %s %s%s\n", hwVSyncCount(), SDL_GetScancodeName(e.key.scancode),
                    e.key.down ? "down" : "up", e.key.repeat ? " repeat" : "");
            fflush(padLog);
        }
        rumbleEvent(&e); /* the window's focus (rumble.c) */
        padDeviceEvent(&e);
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            menuButton(&e.gbutton);
        }
        if (uiEvent(&e)) {
            continue;
        }
        mouseEvent(&e); /* the mouse as the game's pad (mouse.c) */
        switch (e.type) {
        case SDL_EVENT_QUIT:
            /* The window closed: the game never polls for it. */
            quitNow();
            break;
        case SDL_EVENT_KEY_DOWN:
            if (!e.key.repeat) {
                keyDown(e.key.scancode, e.key.mod);
            }
            break;
        case SDL_EVENT_KEY_UP:
            if (e.key.scancode == SDL_SCANCODE_RETURN) {
                returnTaken = 0;
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
            break;
        default:
            if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST) {
                presentWindowEvent(&e);
            }
            break;
        }
    }
}

/* "<vsync> key [Alt+]<name>" */
static int scriptKey(const char* line) {
    unsigned long vsync;
    char name[40];
    if (sscanf(line, "%lu key %39s", &vsync, name) != 2) {
        return 0;
    }
    SDL_Keymod mod = SDL_KMOD_NONE;
    const char* n = name;
    if (SDL_strncasecmp(n, "Alt+", 4) == 0) {
        mod = SDL_KMOD_LALT;
        n += 4;
    } else if (SDL_strncasecmp(n, "Shift+", 6) == 0) {
        mod = SDL_KMOD_LSHIFT;
        n += 6;
    }
    SDL_Scancode key = SDL_GetScancodeFromName(n);
    if (key == SDL_SCANCODE_UNKNOWN) {
        fprintf(stderr, "pad: unknown key '%s' in script\n", n);
        return 1;
    }
    scriptKeys = realloc(scriptKeys, (size_t)(scriptKeyCount + 1) * sizeof(*scriptKeys));
    scriptKeys[scriptKeyCount].vsync = (int64_t)vsync;
    scriptKeys[scriptKeyCount].key = key;
    scriptKeys[scriptKeyCount].mod = mod;
    scriptKeyCount++;
    return 1;
}

static void loadScript(Script* sc, const char* path, int keys) {
    FILE* f = fopen(path, "r");
    char line[256];
    int cap = 0;

    if (f == NULL) {
        fprintf(stderr, "pad: cannot open %s\n", path);
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        unsigned long vsync;
        unsigned int buttons;
        if (line[0] == '#' || (keys && scriptKey(line)) || sscanf(line, "%lu %x", &vsync, &buttons) != 2) {
            continue;
        }
        if (sc->count == cap) {
            cap = cap ? cap * 2 : 64;
            sc->steps = realloc(sc->steps, (size_t)cap * sizeof(*sc->steps));
        }
        sc->steps[sc->count].vsync = (int64_t)vsync;
        sc->steps[sc->count].buttons = (u16)buttons;
        sc->count++;
    }
    fclose(f);
    fprintf(stderr, "pad: %d scripted steps from %s\n", sc->count, path);
}

void padInit(void) {
    const char* path = getenv("XG_PAD_SCRIPT");
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);

    for (int i = 0; ids != NULL && i < count; i++) {
        addGamepad(ids[i]);
    }
    SDL_free(ids);
    if (path != NULL && path[0] != '\0') {
        loadScript(&script, path, 1);
    }
    path = getenv("XG_SLOWDOWN_PAD");
    if (path != NULL && path[0] != '\0') {
        loadScript(&replayScript, path, 0);
    }
    path = getenv("XG_PAD_SCRIPT2");
    if (path != NULL && path[0] != '\0') {
        loadScript(&script2, path, 0);
    }
}

void padSetKeyboardPort(int port) {
    keyboardPort = port == 1;
}

int padKeyboardPort(void) {
    return keyboardPort;
}

/* ---- reading ---- */

int padRegionalControls(int japaneseGame) {
    return settingsBool(japaneseGame ? "us_controls_jp" : "jp_controls_us", 0);
}

void padSetRegionalControls(int japaneseGame, int on) {
    settingsSetInt(japaneseGame ? "us_controls_jp" : "jp_controls_us", !!on);
    settingsSave();
    heldAfterMenu = 1;
}

int padJapaneseControls(void) {
#if VERSION_JP
    return !padRegionalControls(1);
#else
    return padRegionalControls(0);
#endif
}

const char* padConfirmButton(void) { return padJapaneseControls() ? "circle" : "cross"; }
const char* padCancelButton(void) { return padJapaneseControls() ? "cross" : "circle"; }

unsigned pcPadRegionalButtons(unsigned buttons) {
#if VERSION_JP
    int swap = padRegionalControls(1);
#else
    int swap = padRegionalControls(0);
#endif
    /* Retail US swaps both pairs: Circle/Cross and Triangle/Square. */
    if (swap) buttons = (buttons & ~0xf0u) | ((buttons & 0x20u) << 1) | ((buttons & 0x40u) >> 1) |
                        ((buttons & 0x10u) << 3) | ((buttons & 0x80u) >> 3);
    return buttons;
}

int pcPadPromptSlot(int slot) {
#if VERSION_JP
    int swap = padRegionalControls(1);
#else
    int swap = padRegionalControls(0);
#endif
    return swap && slot >= 0 && slot < 4 ? slot ^ 1 : slot;
}

static u16 readKeyboard(void) {
    const bool* keys = SDL_GetKeyboardState(NULL);
    u16 buttons = 0;

    if (keys == NULL) {
        return 0;
    }
    loadBindings();
    for (int i = 0; i < PAD_BUTTONS; i++) {
        for (int k = 0; k < PAD_KEYS; k++) {
            SDL_Scancode key = bind.keys[i][k];
            if (key != SDL_SCANCODE_UNKNOWN && keys[key] && !(key == SDL_SCANCODE_RETURN && returnTaken)) {
                buttons |= buttonInfo[i].bit;
            }
        }
    }
    return buttons;
}

/* The free field camera's input (port/src/system/freecam.c): the right stick and the
 * camera keys (J/L turn, I/K tilt, U/O zoom, Home recentre; a key bound to a game button
 * stays the game's), sampled each vblank; R3 or Home recentres. As in Xenoblade (ZL + right
 * stick), holding the game's L2 turns the stick's up/down into zoom: up closer, down out. L2
 * still reaches the game (L2 + R2 together boards the Gear, as before). */
static struct { float turn, tilt, zoom; int recenter; } camIn;
static int keyBound(SDL_Scancode key) {
    loadBindings();
    for (int i = 0; i < PAD_BUTTONS; i++)
        for (int k = 0; k < PAD_KEYS; k++)
            if (bind.keys[i][k] == key) return 1;
    return 0;
}
static void sampleCamera(int live, u16 held) {
    float turn = 0, tilt = 0, zoom = 0; int rc = 0;
    if (live) {
        const bool* k = SDL_GetKeyboardState(NULL);
        if (k != NULL) {
            #define CAMKEY(sc) (k[sc] && !keyBound(sc))
            turn += CAMKEY(SDL_SCANCODE_L) - CAMKEY(SDL_SCANCODE_J);
            tilt += CAMKEY(SDL_SCANCODE_I) - CAMKEY(SDL_SCANCODE_K);
            zoom += CAMKEY(SDL_SCANCODE_O) - CAMKEY(SDL_SCANCODE_U);
            rc |= CAMKEY(SDL_SCANCODE_HOME);
            #undef CAMKEY
        }
        if (gamepads[0] != NULL) {
            float x = SDL_GetGamepadAxis(gamepads[0], SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
            float y = SDL_GetGamepadAxis(gamepads[0], SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
            const float dead = 0.2f;
            if (x > dead || x < -dead) turn += (x - (x > 0 ? dead : -dead)) / (1 - dead);
            if (y > dead || y < -dead) {
                float v = (y - (y > 0 ? dead : -dead)) / (1 - dead);
                if (held & BTN_L2) zoom += v; else tilt -= v;
            }
            rc |= SDL_GetGamepadButton(gamepads[0], SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        }
    }
    camIn.turn = turn < -1 ? -1 : turn > 1 ? 1 : turn;
    camIn.tilt = tilt < -1 ? -1 : tilt > 1 ? 1 : tilt;
    camIn.zoom = zoom < -1 ? -1 : zoom > 1 ? 1 : zoom;
    if (rc) camIn.recenter = 1;
}
void padCameraInput(float* turn, float* tilt, float* zoom, int* recenter) {
    *turn = camIn.turn; *tilt = camIn.tilt; *zoom = camIn.zoom;
    *recenter = camIn.recenter; camIn.recenter = 0;
}

static u16 readScript(Script* sc, int64_t now) {
    while (sc->pos + 1 < sc->count && sc->steps[sc->pos + 1].vsync <= now) {
        sc->pos++;
    }
    if (sc->count == 0 || sc->steps[sc->pos].vsync > now) {
        return 0;
    }
    return sc->steps[sc->pos].buttons;
}

static u16 readGamepad(SDL_Gamepad* pad, const GamepadBindings* profile) {
    u16 buttons = 0;

    loadBindings();
    for (int i = 0; i < PAD_BUTTONS; i++) {
        int input = profile->pad[i];
        int down = input == PAD_INPUT_LEFT_TRIGGER    ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > TRIGGER_THRESHOLD
                   : input == PAD_INPUT_RIGHT_TRIGGER ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > TRIGGER_THRESHOLD
                   : input >= 0                       ? SDL_GetGamepadButton(pad, (SDL_GamepadButton)input)
                                                      : 0;
        if (down) {
            buttons |= buttonInfo[i].bit;
        }
    }
    if (!profile->stickDpad) {
        return buttons;
    }
    Sint16 x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    Sint16 y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    int threshold = profile->deadzone * 32768 / 100;
    if (x < -threshold) {
        buttons |= BTN_LEFT;
    } else if (x > threshold) {
        buttons |= BTN_RIGHT;
    }
    if (y < -threshold) {
        buttons |= BTN_UP;
    } else if (y > threshold) {
        buttons |= BTN_DOWN;
    }
    return buttons;
}

int padGamepadHeld(SDL_JoystickID device, int button) {
    if (button < 0 || button >= PAD_BUTTONS) {
        return 0;
    }
    for (int i = 0; i < 2; i++) {
        if (gamepads[i] != NULL && SDL_GetGamepadID(gamepads[i]) == device) {
            return (readGamepad(gamepads[i], &profiles[i]) & buttonInfo[button].bit) != 0;
        }
    }
    return 0;
}

static void fillBuffer(int port, int connected, u16 buttons) {
    u8* p = padBuffer[port];

    if (p == NULL || padLength[port] < 4) {
        return;
    }
    if (!connected) {
        p[0] = 0xFF;
        return;
    }
    p[0] = 0x00;
    p[1] = 0x41;
    buttons = (u16)pcPadRegionalButtons(buttons);
    p[2] = (u8)~(buttons >> 8);
    p[3] = (u8)~buttons;
}

/* Vibration (rumble.c): the game's motors and the port's, once a vblank. */
static void updateRumble(int port) {
    if (port == 0) {
        const unsigned char* act[2] = {actuator[0], actuator[1]};
        rumbleVBlank(gamepads, act, script.count > 0 || replayScript.count > 0);
    }
}

void padVBlank(void) {
    padPumpEvents();
    while (scriptKeyPos < scriptKeyCount && scriptKeys[scriptKeyPos].vsync <= hwVSyncCount()) {
        keyDown(scriptKeys[scriptKeyPos].key, scriptKeys[scriptKeyPos].mod);
        scriptKeyPos++;
    }
    /* A netplay session: real time, its end; or, waiting for one started from
     * the menu, this game's wait (netplay.c), where F1 (a scripted one too,
     * taken just above) opens the menu. */
    netplayPace();
    if (uiVBlank()) {
        heldAfterMenu = 1; /* the menu was open: this vblank waited for it to close */
    }
    if (!padStarted) {
        return;
    }
    int64_t logNow;
    u16 port1, port2 = 0, live1 = 0, live2 = 0;
    int scripted = 1;
    if (replayScript.count > 0 && slowdownLogVBlank(&logNow)) {
        port1 = readScript(&replayScript, logNow); /* the reference run's pad, on its clock */
    } else if (script.count > 0) {
        port1 = readScript(&script, hwVSyncCount());
    } else {
        scripted = 0;
        port1 = 0;
        live1 = mouseVBlank();
        if (keyboardPort) {
            live2 = readKeyboard();
        } else {
            live1 |= readKeyboard();
        }
    }
    u16 logMouse = live1, logKbd = 0, logPad = 0;
    if (!scripted) {
        logKbd = readKeyboard();
    }
    if (!scripted && gamepads[0] != NULL) {
        logPad = readGamepad(gamepads[0], &profiles[0]);
        live1 |= logPad;
    }
    padLogOpen();
    if (padLog != NULL && (u16)(port1 | live1) != padLogLast) {
        padLogLast = (u16)(port1 | live1);
        fprintf(padLog, "vb %u %04x kbd=%04x pad=%04x mouse=%04x\n", hwVSyncCount(), padLogLast, logKbd, logPad,
                (unsigned)(logMouse & ~logKbd));
        fflush(padLog);
    }
    if (!scripted && gamepads[1] != NULL) {
        live2 |= readGamepad(gamepads[1], &profiles[1]);
    }
    int connected2 = !scripted && (gamepads[1] != NULL || keyboardPort);
    if (script2.count > 0) {
        port2 = readScript(&script2, hwVSyncCount());
        connected2 = 1;
        live2 = 0;
    }
    sampleCamera(!scripted && !heldAfterMenu, (u16)live1);
    if (heldAfterMenu) {
        /* What closed the menu (Return, Start, a stick click...) is not the game's. */
        heldAfterMenu = (live1 | live2) != 0;
        live1 = live2 = 0;
    }
    if (battleSpeedPad != NULL) {
        battleSpeedPad((unsigned)(port1 | live1)); /* its button, in a battle (battlespeed.c) */
    }
    if (netplayActive()) {
        /* netplay: this side's input out, both players' in (netplay.c) */
        unsigned a, b;
        netplayFrame(port1 | live1 | port2 | live2, &a, &b);
        fillBuffer(0, 1, (u16)a);
        fillBuffer(1, 1, (u16)b);
    } else if (!slowdownBiosMissed()) { /* a replay: the PS1's BIOS pad handler skipped this vblank */
        fillBuffer(0, 1, port1 | live1);
        fillBuffer(1, connected2, port2 | live2);
    }
    updateRumble(0);
    updateRumble(1);
}

/* ---- LIBAPI ---- */

void InitPAD(void* buf1, s32 len1, void* buf2, s32 len2) {
    hwSafePoint();
    padBuffer[0] = buf1;
    padBuffer[1] = buf2;
    padLength[0] = len1;
    padLength[1] = len2;
    for (int i = 0; i < 2; i++) {
        if (padBuffer[i] != NULL && padLength[i] > 0) {
            padBuffer[i][0] = 0xFF; /* nothing read yet */
        }
    }
}

void StartPAD(void) {
    hwSafePoint();
    padStarted = 1;
}

void StopPAD(void) {
    hwSafePoint();
    padStarted = 0;
    rumbleStopAll();
}

/* BIOS B(5Bh): whether the pad handler acknowledges the vblank interrupt. */
void ChangeClearPAD(s32 val) {
    hwSafePoint();
}

void SendPAD(void* buf1, s32 len1, void* buf2, s32 len2) {
    hwSafePoint();
    actuator[0] = len1 >= 3 ? buf1 : NULL;
    actuator[1] = len2 >= 3 ? buf2 : NULL;
}

/* ---- save states (savestate.h) ---- */

/* Rewind's gamepad input (rewind.c): Select and L2 held together on a
 * connected gamepad, as its mapping has them (the game sees them too until
 * the rewind starts; a scripted run reads none). */
int padRewindComboHeld(void) {
    for (int i = 0; i < 2; i++) {
        if (gamepads[i] != NULL && (readGamepad(gamepads[i], &profiles[i]) & (BTN_SELECT | BTN_L2)) ==
                                       (BTN_SELECT | BTN_L2)) {
            return 1;
        }
    }
    return 0;
}

/* The game's pad and actuator buffers (in main's BSS) and whether the pads
 * are started. After a load the scripts go on from the restored vblank, and
 * what is held now (the key that loaded) reaches the game only once let go. */
void padStateIo(XgState* s) {
    XG_STATE_VAR(s, padBuffer);
    XG_STATE_VAR(s, padLength);
    XG_STATE_VAR(s, actuator);
    XG_STATE_VAR(s, padStarted);
    if (xgStateLoading(s)) {
        script.pos = 0; /* readScript catches up from the start */
        script2.pos = 0;
        replayScript.pos = 0;
        scriptKeyPos = 0;
        while (scriptKeyPos < scriptKeyCount && scriptKeys[scriptKeyPos].vsync <= hwVSyncCount()) {
            scriptKeyPos++;
        }
        heldAfterMenu = 1;
    }
}
