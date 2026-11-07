// The settings menu (ui.h): Dear ImGui over the presented picture while the
// game is paused. This file runs it: the loop that stands in for the vblank
// while the menu is open, input, the Vulkan side (vk.c records it into the
// present pass) and XG_UI_SCRIPT. The sections are in sections.cpp.

#include <SDL3/SDL.h>
#include <algorithm>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_internal.h" // GetKeyData: the right stick's analog value
#include "imgui_impl_vulkan.h"
#include "dim.h"
#include "l10n.h"
#include "menu.h"
#include "motion.h"
#include "options.h"
#include "sfx.h"
#include "skin.h"

extern "C" {
#include "gpu/gpu.h"
#include "cd/regional.h"
#include "spu/output.h"
#include "system/pad.h"
#include "system/rumble.h"
#include "system/settings.h"
#include "system/system.h"
#include "ui/ui.h"
void extrasJukeboxTick(void) __attribute__((weak));
void extrasJukeboxEnd(void) __attribute__((weak));
}

#define IMAGE_COUNT 8 // ImGui's vertex buffers in flight: two passes a frame (dump, window), two frames

namespace menu {

const char* const sectionNames[SECTION_COUNT] = {N_("Display"), N_("Audio"),  N_("Controls"),
                                                 N_("Gameplay"), N_("Packs"),  N_("Saves"),
                                                 N_("System"),   N_("Extras"), N_("Search")};
// The section list's names: it is narrow, as the game's main menu.
const char* const listNames[SECTION_COUNT] = {N_("Display"), N_("Audio"),  N_("Controls"), N_("Gameplay"), N_("Packs"),
                                              N_("Saves"),   N_("System"), N_("Extras"),   N_("Search")};

namespace {

// XG_UI_SCRIPT (ui.h).
struct Step {
    unsigned long at; // menu frame, or vblank with onVBlank
    bool onVBlank;
    char cmd[16];
    char arg[256];
    bool done;
};

// A press made by the script, let go two frames (or vblanks) later.
struct Release {
    unsigned long at;
    bool onVBlank;
    int kind; // 0 key (SDL scancode), 1 ImGui gamepad key, 2 virtual gamepad button, 3 its axis, 4 left mouse button, 5 right
    int code;
};

struct State {
    bool want;       // asked to open (F1, Guide, L3+R3)
    bool open;
    bool closing;    // asked to close: the loop ends after this frame
    bool ready;      // ImGui context and the SDL backend
    bool haveDevice; // vk.c gave us its device
    bool vkReady;    // the Vulkan backend
    bool rendered;   // this frame's draw data is ready for uiRecord
    bool setup, scriptLoaded;
    UiVulkan dev;
    VkFormat pipelineFormat;
    float unit;          // window pixels per game pixel (ImGui works in window pixels; skin.h menuUnitFor)
    int setupSection;    // the setup screen's page to open at
    bool helpWanted, backWanted; // the legend's help and back, clicked
    bool restoreContent;         // a click on nothing took the cursor out of the section: back to it
    bool suspendWanted;  // the system put the program in the background (or minimised it: pause_minimized)
    const char* suspendWhy;
    uint64_t resumeAt;   // XG_UI_SCRIPT "background <ms>": when the scripted foreground comes (ns)
    uint64_t lastVBlankNs;   // the real time of the last vblank (a gap: the system slept)
    int resumeLog;           // vblanks after a resume still to log (XG_UI_SUSPEND_LOG)
    float pixelsPerUnit; // window pixels per SDL window coordinate (the desktop's scale)
    int section;
    bool focusSections; // put the nav cursor on the section list
    bool enterContent;  // put it on the section's first control
    bool inContent;     // last frame the cursor was in the section (Esc and B leave it first)
    bool opened;        // a section is open: its pane across the panel, the commands' arc gone (user; Back brings it back)
    bool jumpFocus;     // a search result (or Ctrl+F) opened the section: the cursor goes where the row asks (options.h)
    bool squarePressed; // the PS1 Square button pressed this frame (its key or gamepad input): reset the row
    ImGuiID contentId;  // the section pane, as an item of the menu window
    bool escapeFree;    // last frame nothing in the menu would take Esc or B
    bool padQuiet;      // opened with gamepad buttons held: no gamepad navigation until all are let go
    ImGuiID listWindow; // the window holding the section list (its cursor is the red arrow)
    ImGuiID lastNav;    // the item the cursor was on last frame (a move sounds)
    int arrowEntry;     // the list entry the red arrow is on (-1: none)
    int shownSection;   // the section the pane showed last frame
    unsigned long openedAt; // st.frames when the menu last opened
    ImGuiID lastContentNav;  // the pane's cursor item last frame (scrollToBlock)
    ImGuiID leftFrom;        // Left was pressed on this item: no move means back to the list
    unsigned long closedAt;
    bool leaving;
    ImVec2 centre;           // the opening's projection centre (game mode: the game screen's)
    int modalStyles;         // nextModal's pushed style variables
    unsigned playsSeen;      // sfx::plays() after the last frame's sounds
    unsigned long quietUntil; // no cursor sound for a move before this frame (something else sounded)
    ImVec2 mouse = ImVec2(-FLT_MAX, -FLT_MAX);
    ImRect pane;
    ImDrawList* paneDrawList;
    unsigned long frames;
    const char* shot;
    Step* steps;
    int stepCount;
    Release releases[16];
    int releaseCount;
    SDL_Joystick* vpad;
};

State st;
// Transform copies for the renderer. ImGui retains private write pointers
// and table channels in its original lists between frames.
std::vector<ImDrawList*> projectedLists;

// ---- XG_UI_SCRIPT ------------------------------------------------------------------

// A virtual axis at rest: a trigger's is the bottom of the joystick range
// (SDL maps all of it to 0..32767), a stick's the middle.
Sint16 restingAxis(int axis) {
    return axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? SDL_JOYSTICK_AXIS_MIN : 0;
}

void loadScript() {
    const char* path = getenv("XG_UI_SCRIPT");
    if (path == NULL || path[0] == '\0') {
        return;
    }
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "ui: cannot open XG_UI_SCRIPT %s\n", path);
        return;
    }
    char line[512];
    int cap = 0;
    bool wantPad = false;
    char padType[32] = "";
    while (fgets(line, sizeof(line), f) != NULL) {
        Step s = {};
        char when[24];
        int n = sscanf(line, "%23s %15s %255[^\r\n]", when, s.cmd, s.arg);
        if (n < 2 || when[0] == '#') {
            continue;
        }
        s.onVBlank = when[0] == 'v';
        s.at = strtoul(when + (s.onVBlank ? 1 : 0), NULL, 10);
        wantPad |= strcmp(s.cmd, "vpad") == 0;
        if (!strcmp(s.cmd,"vpad-type")) {
            snprintf(padType,sizeof(padType),"%.31s",s.arg);
            wantPad=true;
        }
        if (st.stepCount == cap) {
            cap = cap ? cap * 2 : 32;
            st.steps = (Step*)realloc(st.steps, (size_t)cap * sizeof(Step));
        }
        st.steps[st.stepCount++] = s;
    }
    fclose(f);
    fprintf(stderr, "ui: %d scripted steps from %s\n", st.stepCount, path);
    if (wantPad && !SDL_WasInit(SDL_INIT_GAMEPAD)) {
        // A headless run has no gamepads (main.c): here only this one, never a
        // controller of whoever plays on the machine (no HIDAPI, every other
        // device ignored as a gamepad).
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
        SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, "0xc0de/0x0007,0x0000/0x0000");
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
            fprintf(stderr, "ui: no gamepads for the virtual one: %s\n", SDL_GetError());
        }
    }
    if (wantPad) {
        // A virtual gamepad, so a script drives the same path a real one does;
        // it has motors, and logs what they are told ("ui: vpad rumble").
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "Xenogears script pad";
        desc.Rumble = [](void*, Uint16 large, Uint16 small) {
            fprintf(stderr, "ui: vpad rumble v%u large %.2f small %.2f\n", hwVSyncCount(), large / 65535.0, small / 65535.0);
            return true;
        };
        if (padType[0]) { desc.vendor_id=0xc0de; desc.product_id=0x0007; }
        SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        if (id && padType[0]) {
            char guid[33],mapping[768];
            SDL_GUIDToString(SDL_GetJoystickGUIDForID(id),guid,sizeof(guid));
            snprintf(mapping,sizeof(mapping),"%s,Xenogears script pad,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,"
                "leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,"
                "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,type:%s,",guid,padType);
            SDL_SetGamepadMapping(id,mapping);
        }
        st.vpad = id != 0 ? SDL_OpenJoystick(id) : NULL;
        if (st.vpad == NULL) {
            fprintf(stderr, "ui: no virtual gamepad: %s\n", SDL_GetError());
        }
        for (int a = 0; st.vpad != NULL && a < SDL_GAMEPAD_AXIS_COUNT; a++) {
            SDL_SetJoystickVirtualAxis(st.vpad, a, restingAxis(a));
        }
    }
}

void pushKey(SDL_Scancode code, bool down) {
    SDL_Event e = {};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.timestamp = SDL_GetTicksNS();
    e.key.windowID = presentWindow() != NULL ? SDL_GetWindowID(presentWindow()) : 0;
    e.key.scancode = code;
    e.key.key = SDL_GetKeyFromScancode(code, SDL_KMOD_NONE, false);
    e.key.down = down;
    SDL_PushEvent(&e);
}

ImGuiKey padKey(const char* name) {
    static const struct {
        const char* name;
        ImGuiKey key;
    } keys[] = {{"up", ImGuiKey_GamepadDpadUp},       {"down", ImGuiKey_GamepadDpadDown},
                {"left", ImGuiKey_GamepadDpadLeft},   {"right", ImGuiKey_GamepadDpadRight},
                {"a", ImGuiKey_GamepadFaceDown},      {"b", ImGuiKey_GamepadFaceRight},
                {"x", ImGuiKey_GamepadFaceLeft},      {"y", ImGuiKey_GamepadFaceUp},
                {"triangle", ImGuiKey_GamepadFaceUp}, {"l1", ImGuiKey_GamepadL1}, {"r1", ImGuiKey_GamepadR1},
                {"start", ImGuiKey_GamepadStart},     {"back", ImGuiKey_GamepadBack}};
    for (const auto& k : keys) {
        if (SDL_strcasecmp(name, k.name) == 0) {
            return k.key;
        }
    }
    return ImGuiKey_None;
}

void release(const Step& s, int kind, int code, unsigned long after = 2) {
    if (st.releaseCount < (int)IM_COUNTOF(st.releases)) {
        st.releases[st.releaseCount++] = {s.at + after, s.onVBlank, kind, code};
    }
}

void runStep(Step& s) {
    s.done = true;
    if (!strcmp(s.cmd,"vpad-type")) {
        // The virtual device's label family is selected before it is opened.
    } else if (st.setup && strncmp(s.cmd, "setup-", 6) == 0) {
        ImGui::ClearActiveID(); // an externally selected path replaces the editor's pending text
        if (discSetupCommand(s.cmd, s.arg)) st.closing = true;
    } else if (strcmp(s.cmd, "key") == 0 || strcmp(s.cmd, "hold") == 0) {
        // "hold <key> <frames>": held that long (the repeat), else let go two later.
        char name[64] = "";
        unsigned long frames = 2;
        if (s.cmd[0] == 'h') {
            sscanf(s.arg, "%63s %lu", name, &frames);
        } else {
            snprintf(name, sizeof(name), "%s", s.arg);
        }
        SDL_Scancode code = SDL_GetScancodeFromName(name);
        if (code == SDL_SCANCODE_UNKNOWN) {
            fprintf(stderr, "ui: script: unknown key '%s'\n", name);
            return;
        }
        pushKey(code, true);
        release(s, 0, code, frames);
    } else if (strcmp(s.cmd, "pad") == 0) {
        ImGuiKey key = padKey(s.arg);
        if (key == ImGuiKey_None || !st.open) {
            fprintf(stderr, "ui: script: pad '%s' (up down left right a b x y l1 r1 start back, menu open)\n", s.arg);
            return;
        }
        ImGui::GetIO().AddKeyEvent(key, true);
        release(s, 1, key);
    } else if (strcmp(s.cmd, "vpad") == 0 && SDL_GetGamepadAxisFromString(s.arg) != SDL_GAMEPAD_AXIS_INVALID) {
        SDL_GamepadAxis a = SDL_GetGamepadAxisFromString(s.arg); // a trigger pulled, a stick pushed
        if (st.vpad != NULL) {
            SDL_SetJoystickVirtualAxis(st.vpad, a, SDL_JOYSTICK_AXIS_MAX);
            release(s, 3, a);
        }
    } else if (strcmp(s.cmd, "vpad") == 0) {
        SDL_GamepadButton b = SDL_GetGamepadButtonFromString(s.arg);
        if (st.vpad == NULL || b == SDL_GAMEPAD_BUTTON_INVALID) {
            fprintf(stderr, "ui: script: vpad '%s' (an SDL gamepad button name: a b x y dpup start guide leftstick...)\n",
                    s.arg);
            return;
        }
        SDL_SetJoystickVirtualButton(st.vpad, b, true);
        release(s, 2, b);
    } else if (strcmp(s.cmd, "mouse") == 0 || strcmp(s.cmd, "click") == 0) {
        // The mouse, in window coordinates (as SDL gives them): moved there,
        // or a left click there.
        float x = 0, y = 0;
        sscanf(s.arg, "%f %f", &x, &y);
        SDL_Event e = {};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.timestamp = SDL_GetTicksNS();
        e.motion.windowID = presentWindow() != NULL ? SDL_GetWindowID(presentWindow()) : 0;
        e.motion.x = x;
        e.motion.y = y;
        SDL_PushEvent(&e);
        if (s.cmd[0] == 'c') {
            SDL_Event b = {};
            b.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            b.button.timestamp = SDL_GetTicksNS();
            b.button.windowID = e.motion.windowID;
            b.button.button = SDL_BUTTON_LEFT;
            b.button.down = true;
            b.button.clicks = 1;
            b.button.x = x;
            b.button.y = y;
            SDL_PushEvent(&b);
            release(s, 4, 0);
        }
    } else if (strcmp(s.cmd, "text") == 0) {
        // Typed text, into the field being edited (s.arg lives as long as the script).
        SDL_Event e = {};
        e.type = SDL_EVENT_TEXT_INPUT;
        e.text.timestamp = SDL_GetTicksNS();
        e.text.windowID = presentWindow() != NULL ? SDL_GetWindowID(presentWindow()) : 0;
        e.text.text = s.arg;
        SDL_PushEvent(&e);
    } else if (strcmp(s.cmd, "section") == 0) {
        // A section by (the start of) its English name; the merged ones' old
        // names too: cards, states (Saves), about (System).
        for (int i = 0; i < SECTION_COUNT; i++) {
            if (SDL_strncasecmp(s.arg, sectionNames[i], strlen(s.arg)) == 0 ||
                (i == SAVES && (SDL_strcasecmp(s.arg, "cards") == 0 || SDL_strcasecmp(s.arg, "states") == 0 ||
                                SDL_strcasecmp(s.arg, "memory cards") == 0 || SDL_strcasecmp(s.arg, "save states") == 0)) ||
                (i == SYSTEM && SDL_strcasecmp(s.arg, "about") == 0)) {
                st.section = i;
                st.enterContent = true; /* opened, the cursor on its first control */
                st.opened = true;
            }
        }
    } else if (strcmp(s.cmd, "shot") == 0) {
        static char name[256];
        snprintf(name, sizeof(name), "%s", s.arg);
        st.shot = name;
    } else if (strcmp(s.cmd, "close") == 0) {
        st.closing = true;
    } else if (strcmp(s.cmd, "wheel") == 0) {
        // The mouse wheel, notches (positive: up), over the window.
        SDL_Event e = {};
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.timestamp = SDL_GetTicksNS();
        e.wheel.windowID = presentWindow() != NULL ? SDL_GetWindowID(presentWindow()) : 0;
        e.wheel.y = (float)atof(s.arg);
        e.wheel.integer_y = (int)e.wheel.y;
        e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        SDL_PushEvent(&e);
    } else if (strcmp(s.cmd, "rclick") == 0 || strcmp(s.cmd, "rdown") == 0) {
        // A right click there (window coordinates); rdown: pressed only (a
        // drag: "mouse" moves, then "rup").
        float x = 0, y = 0;
        sscanf(s.arg, "%f %f", &x, &y);
        SDL_Event b = {};
        b.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        b.button.timestamp = SDL_GetTicksNS();
        b.button.windowID = presentWindow() != NULL ? SDL_GetWindowID(presentWindow()) : 0;
        b.button.button = SDL_BUTTON_RIGHT;
        b.button.down = true;
        b.button.clicks = 1;
        b.button.x = x;
        b.button.y = y;
        SDL_PushEvent(&b);
        if (s.cmd[1] == 'c') {
            release(s, 5, 0);
        }
    } else if (strcmp(s.cmd, "rup") == 0) {
        SDL_Event b = {};
        b.type = SDL_EVENT_MOUSE_BUTTON_UP;
        b.button.timestamp = SDL_GetTicksNS();
        b.button.windowID = presentWindow() != NULL ? SDL_GetWindowID(presentWindow()) : 0;
        b.button.button = SDL_BUTTON_RIGHT;
        SDL_PushEvent(&b);
    } else if (strcmp(s.cmd, "background") == 0) {
        // The system's "going to the background" (a handheld's suspend, a
        // phone's app switch), and its "back" <ms> later, as SDL sends them.
        SDL_Event e = {};
        e.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
        SDL_PushEvent(&e);
        e.type = SDL_EVENT_DID_ENTER_BACKGROUND;
        SDL_PushEvent(&e);
        st.resumeAt = SDL_GetTicksNS() + (uint64_t)strtoul(s.arg, NULL, 10) * 1000000ull;
    } else {
        fprintf(stderr, "ui: script: unknown command '%s'\n", s.cmd);
    }
}

// The script's steps due now: on menu frames while open, on vblanks (lines
// "v<vblank> ...") while the game runs. all: every pending release of that
// kind now (the menu closes: nothing it pressed stays held).
void runScript(bool onVBlank, unsigned long now, bool all = false) {
    for (int i = 0; i < st.releaseCount;) {
        Release r = st.releases[i];
        if (r.onVBlank != onVBlank || (r.at > now && !all)) {
            i++;
            continue;
        }
        if (r.kind == 0) {
            pushKey((SDL_Scancode)r.code, false);
        } else if (r.kind == 1) {
            if (st.ready) {
                ImGui::GetIO().AddKeyEvent((ImGuiKey)r.code, false);
            }
        } else if (r.kind == 2 && st.vpad != NULL) {
            SDL_SetJoystickVirtualButton(st.vpad, r.code, false);
        } else if (r.kind == 3 && st.vpad != NULL) {
            SDL_SetJoystickVirtualAxis(st.vpad, r.code, restingAxis(r.code));
        } else if (r.kind == 4 || r.kind == 5) {
            SDL_Event b = {};
            b.type = SDL_EVENT_MOUSE_BUTTON_UP;
            b.button.timestamp = SDL_GetTicksNS();
            b.button.windowID = presentWindow() != NULL ? SDL_GetWindowID(presentWindow()) : 0;
            b.button.button = r.kind == 4 ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
            SDL_PushEvent(&b);
        }
        st.releases[i] = st.releases[--st.releaseCount];
    }
    for (int i = 0; i < st.stepCount && !all; i++) {
        Step& s = st.steps[i];
        if (!s.done && s.onVBlank == onVBlank && s.at <= now) {
            runStep(s);
        }
    }
}

// Any gamepad button held, or a trigger pulled, on any gamepad.
bool gamepadHeld() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    bool held = false;
    for (int i = 0; i < count && !held; i++) {
        SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
        for (int b = 0; pad != NULL && b < SDL_GAMEPAD_BUTTON_COUNT && !held; b++) {
            held = SDL_GetGamepadButton(pad, (SDL_GamepadButton)b);
        }
        held |= pad != NULL && (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000 ||
                                SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000);
    }
    SDL_free(ids);
    return held;
}

// ---- the game's buttons ------------------------------------------------------------------

// The menu follows the PS1 buttons as the game does, through their keys and
// gamepad inputs (pad.h, settings key_<button> and pad_<button>): the D-pad
// moves, the game's confirm button chooses and its cancel button goes back
// (US Cross and Circle, JP Circle and Cross), L1 and R1 change the section,
// Start leaves. ImGui sees them as its gamepad keys; the SDL backend's own
// gamepad reading is off (it would take the pad's buttons by place, not by
// binding). Enter, Esc, the arrows, Space and Tab keep their keyboard
// meanings too.
struct NavButton {
    const char* button; // pad.h's name
    ImGuiKey key;
};
#define BUTTON_CHOOSE padConfirmButton()
#define BUTTON_BACK padCancelButton()
const NavButton navButtons[] = {
    {"up", ImGuiKey_GamepadDpadUp},     {"down", ImGuiKey_GamepadDpadDown},   {"left", ImGuiKey_GamepadDpadLeft},
    {"right", ImGuiKey_GamepadDpadRight}, {"confirm", ImGuiKey_GamepadFaceDown}, {"cancel", ImGuiKey_GamepadFaceRight},
    {"triangle", ImGuiKey_GamepadFaceUp}, {"l1", ImGuiKey_GamepadL1}, {"r1", ImGuiKey_GamepadR1},
    {"start", ImGuiKey_GamepadStart},
};
#define NAV_BUTTONS (int)IM_COUNTOF(navButtons)

bool keyHeld[SDL_SCANCODE_COUNT]; // every key, from the events (open or not)
bool navDown[NAV_BUTTONS];        // what ImGui was last told

int padButton(const char* name) {
    if (!strcmp(name, "confirm")) name = padConfirmButton();
    if (!strcmp(name, "cancel")) name = padCancelButton();
    for (int i = 0; i < PAD_BUTTONS; i++) {
        if (strcmp(padButtonName(i), name) == 0) {
            return i;
        }
    }
    return -1;
}

// Keys ImGui reads as keyboard keys itself: never also a game button here.
bool keyboardOwn(SDL_Scancode k) {
    switch (k) {
    case SDL_SCANCODE_UP: case SDL_SCANCODE_DOWN: case SDL_SCANCODE_LEFT: case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_TAB:
    case SDL_SCANCODE_SPACE: case SDL_SCANCODE_BACKSPACE: case SDL_SCANCODE_DELETE: case SDL_SCANCODE_HOME:
    case SDL_SCANCODE_END: case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_PAGEDOWN:
        return true;
    default:
        return false;
    }
}

// Is the PS1 button held: one of its keys (not while text is typed), its
// gamepad input on any gamepad, or for the D-pad the left stick when it
// drives the D-pad.
bool buttonHeld(int b, bool typing) {
    if (b < 0) {
        return false;
    }
    for (int slot = 0; slot < PAD_KEYS && !typing; slot++) {
        SDL_Scancode k = ::padKey(b, slot);
        if (k != SDL_SCANCODE_UNKNOWN && !keyboardOwn(k) && keyHeld[k]) {
            return true;
        }
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    bool held = false;
    for (int i = 0; i < count && !held; i++) {
        held = padGamepadHeld(ids[i], b);
    }
    SDL_free(ids);
    return held;
}

// Each frame: the buttons' changes to ImGui, and the right stick (it scrolls).
// quiet: take what is held as already known (the menu just opened: a button
// held then is no press).
void feedButtons(bool quiet) {
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    bool typing = io.WantTextInput;
    for (int i = 0; i < NAV_BUTTONS; i++) {
        bool down = buttonHeld(padButton(navButtons[i].button), typing);
        if (down != navDown[i] && !quiet) {
            io.AddKeyEvent(navButtons[i].key, down);
        }
        navDown[i] = down;
    }
    // Square resets the row the cursor is on (options.h): read here, not
    // given to ImGui (its gamepad Square is the windowing button).
    static bool squareDown;
    bool square = buttonHeld(padButton("square"), typing);
    st.squarePressed = square && !squareDown && !quiet;
    squareDown = square;
    float stick = 0.0f;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; i++) {
        SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
        float v = pad != NULL ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f : 0.0f;
        stick = fabsf(v) > fabsf(stick) ? v : stick;
    }
    SDL_free(ids);
    stick = fabsf(stick) < 0.25f ? 0.0f : stick;
    io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickUp, stick < 0, stick < 0 ? -stick : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickDown, stick > 0, stick > 0 ? stick : 0.0f);
}

// Nothing of the buttons left held in ImGui (the menu closes).
void releaseButtons() {
    for (int i = 0; i < NAV_BUTTONS; i++) {
        if (navDown[i]) {
            ImGui::GetIO().AddKeyEvent(navButtons[i].key, false);
            navDown[i] = false;
        }
    }
}

// The key a PS1 button has for the legend ("X"), or NULL.
const char* buttonKeyName(const char* button) {
    int b = padButton(button);
    for (int slot = 0; b >= 0 && slot < PAD_KEYS; slot++) {
        SDL_Scancode k = ::padKey(b, slot);
        if (k != SDL_SCANCODE_UNKNOWN && !keyboardOwn(k)) {
            const char* name = SDL_GetScancodeName(k);
            return name != NULL && name[0] != '\0' ? name : NULL;
        }
    }
    return NULL;
}

// ---- look -----------------------------------------------------------------------------

// The game's pixels as blocks of window pixels, as large as the text size
// and the menu's percentage ask (skin.h menuUnitFor: whole pixels at 100%,
// so the letters and frames stay sharp at every window size); read each
// frame, so a change applies at once.
void updateScale() {
    int pw = 0, ph = 0, lw = 0, lh = 0;
    SDL_GetWindowSizeInPixels(presentWindow(), &pw, &ph);
    SDL_GetWindowSize(presentWindow(), &lw, &lh);
    st.pixelsPerUnit = lw > 0 ? (float)pw / (float)lw : 1.0f;
    float s = skin::menuUnitFor(pw, ph);
    if (s == st.unit) {
        return;
    }
    st.unit = s;
    skin::setUnit(s);
}

// ---- setting up --------------------------------------------------------------------------

void checkVk(VkResult r) {
    if (r != VK_SUCCESS) {
        fprintf(stderr, "ui: vulkan error %d\n", (int)r);
    }
}

void VKAPI_PTR missingVk(void) {
    fprintf(stderr, "ui: a Vulkan function the menu never uses was called\n");
}

PFN_vkVoidFunction loadVk(const char* name, void* user) {
    const UiVulkan* d = (const UiVulkan*)user;
    PFN_vkVoidFunction f = ((PFN_vkGetInstanceProcAddr)d->getInstanceProcAddr)((VkInstance)d->instance, name);
    // The backend loads its window helpers' surface and swapchain functions
    // too; without a window (headless) some drivers have none, and the menu
    // never calls them.
    if (f == NULL && strstr(name, "KHR") != NULL) {
        f = (PFN_vkVoidFunction)missingVk;
    }
    return f;
}

// ImGui works in window pixels (frame()); SDL's text input area is in window
// coordinates.
void (*sdlSetImeData)(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData*);

void setImeData(ImGuiContext* ctx, ImGuiViewport* vp, ImGuiPlatformImeData* data) {
    ImGuiPlatformImeData d = *data;
    if (!st.setup && st.open && st.frames - st.openedAt < menuMotion::OPENING_FRAMES) {
        auto view = menuMotion::opening(st.centre, skin::unit(), unsigned(st.frames-st.openedAt));
        ImVec2 bottom = view.project(ImVec2(d.InputPos.x, d.InputPos.y+d.InputLineHeight));
        d.InputPos = view.project(d.InputPos);
        d.InputLineHeight = fabsf(bottom.y-d.InputPos.y);
    }
    float k = st.pixelsPerUnit > 0 ? 1.0f / st.pixelsPerUnit : 1.0f;
    d.InputPos = ImVec2(d.InputPos.x * k, d.InputPos.y * k);
    d.InputLineHeight *= k;
    sdlSetImeData(ctx, vp, &d);
}

bool begin() {
    if (st.ready) {
        return true;
    }
    if (presentWindow() == NULL || !st.haveDevice) {
        fprintf(stderr, "ui: no window or no Vulkan device: no settings menu\n");
        return false;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL; // a fixed layout: nothing to keep
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    skin::init(); // the game's look from the disc (or ImGui's own font if it has none)
    if (getenv("XG_UI_DEBUG") != NULL) {
        // ImGui's own log of navigation, focus and input events, on stdout.
        ImGui::GetCurrentContext()->DebugLogFlags |=
            ImGuiDebugLogFlags_EventNav | ImGuiDebugLogFlags_EventFocus | ImGuiDebugLogFlags_EventIO |
            ImGuiDebugLogFlags_OutputToTTY;
    }
    ImGui_ImplSDL3_InitForVulkan(presentWindow());
    // The gamepads are read here, by the game's bindings (feedButtons).
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, NULL, 0);
    // The text cursor's place for input methods, in window coordinates again.
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    sdlSetImeData = pio.Platform_SetImeDataFn;
    pio.Platform_SetImeDataFn = setImeData;

    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, loadVk, &st.dev);
    ImGui_ImplVulkan_InitInfo info = {};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = (VkInstance)st.dev.instance;
    info.PhysicalDevice = (VkPhysicalDevice)st.dev.physical;
    info.Device = (VkDevice)st.dev.device;
    info.QueueFamily = st.dev.queueFamily;
    info.Queue = (VkQueue)st.dev.queue;
    info.DescriptorPoolSize = 16;
    info.MinImageCount = 2;
    info.ImageCount = IMAGE_COUNT;
    info.UseDynamicRendering = true; // the pipeline comes with the first target's format (uiRecord)
    info.CheckVkResultFn = checkVk;
    ImGui_ImplVulkan_Init(&info);
    dim::init((VkDevice)st.dev.device, loadVk, &st.dev);
    st.vkReady = true;
    st.ready = true;
    return true;
}

// ---- a frame -------------------------------------------------------------------------------

bool popupOpen() {
    return ImGui::IsPopupOpen((const char*)NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

// Blocks: an option and its description lines (sections.cpp marks where each
// begins, menu.h markBlock). When the keyboard or gamepad cursor comes to an
// option, the pane scrolls so its whole block shows; the first block reaches
// up to the section's top, the last down to its end, so the first option
// shows the section from its top and the last scrolls to the very bottom. A
// block taller than the pane: its option at the top, as much as fits below.
struct HelpBlock { float y; std::string text; };
std::vector<HelpBlock> helpBlocks;
ImGuiWindow* helpWindow;
std::string helpText;
int helpIndex;
ImVector<float> blocks; // screen y of each block's top, this frame

void scrollToBlock(ImGuiWindow* w, float endY) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if (g.NavWindow != w || g.NavId == 0 || g.NavId == st.lastContentNav || !g.NavCursorVisible ||
        ImGui::IsAnyItemActive()) {
        if (g.NavWindow == w) {
            st.lastContentNav = g.NavId;
        }
        return;
    }
    st.lastContentNav = g.NavId;
    if (blocks.Size == 0) {
        return;
    }
    // Content coordinates: 0 the section's top, independent of the scroll.
    float origin = w->DC.CursorStartPos.y;
    ImRect item = ImGui::WindowRectRelToAbs(w, w->NavRectRel[g.NavLayer]);
    float iy0 = item.Min.y - origin, iy1 = item.Max.y - origin;
    float y0 = 0.0f, y1 = endY - origin;
    float mid = (iy0 + iy1) * 0.5f; // the item's frame may start a pixel above its row
    for (int i = 0; i < blocks.Size; i++) {
        float b = blocks[i] - origin;
        if (b <= mid) {
            y0 = i == 0 ? 0.0f : b;
        } else {
            y1 = b;
            break;
        }
    }
    bool last = y1 == endY - origin;
    float view = w->Size.y - w->WindowPadding.y * 2;
    float scroll = w->Scroll.y, want = scroll;
    if (y1 - y0 <= view) {
        if (y0 < scroll) {
            want = y0;
        } else if (y1 > scroll + view) {
            want = y1 - view;
        }
    } else if (iy0 < scroll) {
        want = iy0;
    } else if (iy1 > scroll + view || (last && y1 > scroll + view)) {
        want = last ? fminf(iy0, y1 - view) : iy1 - view;
    }
    want = fmaxf(0.0f, fminf(want, w->ScrollMax.y));
    if (want != scroll) {
        ImGui::SetScrollY(w, want);
    }
}

// ---- the game's screen ----------------------------------------------------------------

// The section list is the game's main menu: a ball for each command (red for
// the chosen one), its name beside it, and the red arrow on the cursor.
// The game's seven commands lie on one circle (cursorPosXTable and
// cursorPosYTable, menu overlay 0x801E9A00 and 0x801E9A2C: 0x17,0x61 ...
// 0x49,0xC9), centre (-54.6, 198.1), radius 127.5, one every 8.96 degrees.
// The settings menu has ten (nine sections and Back). Ten on the game's
// circle were cramped (7.15 degrees apart, the top ones 9 pixels: xg-htn.35.92.2),
// so they lie on that circle enlarged (user): the same sweep, from
// the game's first command's angle to its seventh's, scaled about the point
// where the first ball touches the screen's left edge (x 7) and the last ends
// on the menu picture's last line (y 205). Compact: 1.65 times both ways, which
// gives ten entries the game's own spacing (13 pixels at the top to 21 at the
// bottom). On the game's screen the names must stay left of the game's sheet
// (x 0x7A), so there only the height is 1.65 times and the arc keeps the
// game's width (x 7 to 73).
ImVec2 commandPoint(int i, bool game) {
    const float cx = -54.6f, cy = 198.1f, r = 127.5f;
    const float a0 = atan2f(97 - cy, 23 - cx), a1 = atan2f(201 - cy, 73 - cx);
    const float kx = game ? 1.0f : 1.65f, ky = 1.65f;
    float a = a0 + (a1 - a0) * (float)i / (float)SECTION_COUNT;
    float x = 7 + kx * r * (cosf(a) - cosf(a0));
    float y = 205 - ky * r * (sinf(a1) - sinf(a));
    return ImVec2(roundf(x), roundf(y));
}
// From a command's point (the menu file's sprite table, placed at 0xA0,0x96
// as setupMainMenuWithConfig does): its ball (sprites 0x109 + k, red 0x116 + k)
// 16 x 16 with its top left at (-7, -5); its name (sprites 0x131 - k, nine rows
// of the menu's letters) from (+2, -2): the dialogue font's capitals are rows
// 2 to 10 of its 13, so its line starts at -4; the red arrow (0x108) has its
// tip on the point.
const ImVec2 kBall(-7, -5), kLabel(2, -4);
// The setup screen's four commands: the game's own title menu's places
// (loadSaveGameMenuExecute: cursorPosXTable/cursorPosYTable entries 7 to 10,
// 0x18,0x90 0x1F,0xA2 0x24,0xB5 0x25,0xC9), top to bottom.
const ImVec2 kSetupCommands[SETUP_SECTIONS + 2] = {{0x18, 0x90}, {0x1F, 0xA2}, {0x24, 0xB5}, {0x25, 0xC9}};
const char* const kSetupNames[SETUP_SECTIONS] = {N_("Discs"), "BIOS"};

// The game's boxes (iniMenuContext364And380: x, y, width, height of the
// inside), game pixels.
struct Box {
    int x, y, w, h;
};
// The message box (processLoadSaveMenuSub5: 0x7A, 0x96, 0xBC x 0x40; three
// lines at x 0x84, y 0xA0 + 16 i): the description window and the modals.
const Box kMessageBox = {0x7A, 0x96, 0xBC, 0x40};
const int kMessageText = 10; // its lines' left and top inside it
// The section's sheet: the message box's width and left edge, the item
// list's top and height (openItemMenu: box 3 at 0xC, 0xA, 0x124 x 0x84), so
// it stands right of the commands and 8 above the message box.
const Box kSheet = {0x7A, 0x0A, 0xBC, 0x84};
// An open section on the game's screen: the item list's box itself, across
// the screen as the game shows it once a command is chosen (the commands gone).
const Box kItemBox = {0x0C, 0x0A, 0x124, 0x84};
// Inside a list box (setupItemMenuPage, updateMenuPointerGlyph): the names 28
// right of the box, the first row 4 down, the pointer 12 right (menu.h's
// pointer: skin::endFrame); the scroll bar on the box's inner left edge
// (initMenuTiles6), 8 wide.
const int kListText = 28, kListTop = 4;

// Where the menu's things go this frame, in window pixels.
struct Layout {
    bool game;      // menu_text=game: on the game's screen, at the game's places
    float u;        // window pixels per game pixel
    ImVec2 origin;  // game mode: the game menu picture's (0, 0); compact: the commands' frame
    ImRect sheet, help, foot;
    ImVec2 at(float x, float y) const { return ImVec2(origin.x + x * u, origin.y + y * u); }
    ImRect box(const Box& b) const { return ImRect(at(b.x, b.y), at(b.x + b.w, b.y + b.h)); }
    bool setup;     // the setup screen's commands (kSetupCommands)
    ImVec2 command(int i) const {
        ImVec2 p = setup ? kSetupCommands[i] : commandPoint(i, game);
        return at(p.x, p.y);
    }
    ImVec2 centre() const { return game ? at(160, 112) : ImVec2(); } // the menu's projection centre
};

// opened: a section is open (its pane across the panel; else beside the commands).
Layout layout(bool opened) {
    ImGuiIO& io = ImGui::GetIO();
    Layout L;
    L.u = skin::unit();
    float u = L.u;
    L.game = skin::textSize() == skin::TEXT_GAME;
    L.setup = st.setup;
    if (L.game) {
        // The game's menu picture (320 x 216: its display is 216 lines) as
        // the window shows the game's own menu at the 4:3 TV framing: across
        // the middle, its line 0 110 game lines above the window's middle
        // (measured: XG_DUMP_PRESENT of the game's menu in a 320 x 240 window
        // has it from line 10, as Beetle's TV picture). No legend: the game
        // has none (the help window and its popup say the keys).
        L.origin = ImVec2(floorf(io.DisplaySize.x * 0.5f - 160 * u), floorf(io.DisplaySize.y * 0.5f - 110 * u));
        // A window too low for the TV's framing (the menu's percentage up to
        // what the window holds): the game's screen kept inside it.
        L.origin.y = std::max(0.0f, std::min(L.origin.y, floorf(io.DisplaySize.y - 216 * u)));
        L.origin.x = std::max(0.0f, L.origin.x);
        L.sheet = L.box(opened ? kItemBox : kSheet);
        L.help = L.box(kMessageBox);
        return L;
    }
    // Compact: the port's own panel, up to 560 x 336 game pixels: the
    // commands' column, the sheet beside it, the message box under the sheet
    // and the legend at the bottom.
    const int footH = 17, edge = 8, gap = 8, listW = 148; // the enlarged arc's longest names end at 136
    int cw = (int)(io.DisplaySize.x / u), ch = (int)(io.DisplaySize.y / u);
    int pw = cw - 2 * edge < 560 ? cw - 2 * edge : 560, ph = ch - 2 * edge < 336 ? ch - 2 * edge : 336;
    // The message box's three lines, or two where the panel is low (a large
    // menu size in a small window), so the sheet keeps room for its rows.
    const int helpH = ph < 280 ? kMessageBox.h - 16 : kMessageBox.h;
    ImVec2 o(floorf((cw - pw) / 2.0f) * u, floorf((ch - ph) / 2.0f) * u);
    auto at = [&](int x, int y) { return ImVec2(o.x + x * u, o.y + y * u); };
    const int left = opened ? edge : edge + listW + gap; // open: the whole panel's width
    L.sheet = ImRect(at(left, edge), at(pw - edge, ph - edge - footH - gap - helpH - gap));
    L.help = ImRect(at(left, ph - edge - footH - gap - helpH), at(pw - edge, ph - edge - footH - gap));
    L.foot = ImRect(at(edge, ph - edge - footH), at(pw - edge, ph - edge));
    // The commands' circle with its last ball's bottom (216) on the column's.
    L.origin = ImVec2(o.x + edge * u, o.y + (ph - edge - footH - gap - 216) * u);
    return L;
}

bool listEntry(int index, const char* name, bool chosen, ImVec2 point, bool dimmed, bool* focused) {
    float u = skin::unit();
    ImVec2 at(point.x + kBall.x * u, point.y + kBall.y * u);
    ImVec2 label(point.x + kLabel.x * u, point.y + kLabel.y * u);
    float width = label.x - at.x + ImGui::CalcTextSize(name).x;
    ImGui::SetCursorScreenPos(at);
    ImGui::PushID(index);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
    bool pressed = ImGui::Selectable("##entry", false, 0, ImVec2(width, 16 * u));
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    *focused = ImGui::IsItemFocused();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // While a sub-menu is up the game dims its commands (drawMainMenu, m1192:
    // modulated by 0x20 and added: skin::textDimmed).
    ImU32 tint = dimmed ? ImGui::GetColorU32(skin::textDimmed) : IM_COL32_WHITE;
    if (skin::spriteSize(skin::SPRITE_BALL).x > 0) {
        skin::spriteAt(dl, chosen ? skin::SPRITE_BALL_RED : skin::SPRITE_BALL, at, tint);
    }
    dl->AddText(label, dimmed ? tint : ImGui::GetColorU32(ImGuiCol_Text), name);
    return pressed;
}

// A direction pressed this frame, or held long enough to step again: the
// game's auto-repeat (src/main/pad.c: newPadButtonForField has a held button
// again 32 vblanks after the press, then every 4th; the menu reads it in
// updateMenuInput), which frame() gives ImGui's navigation repeat.
bool stepped(ImGuiKey key) {
    return ImGui::IsKeyPressed(key, (ImGuiInputFlags)ImGuiInputFlags_Repeat | (ImGuiInputFlags)ImGuiInputFlags_RepeatRateNavMove,
                               ImGuiKeyOwner_Any);
}

// ---- the input in use -----------------------------------------------------------------

// The legend (and the help popup's key lines) shows one way of playing: the
// keyboard and mouse, or a controller, whichever was last used deliberately
// (xg-htn.35.19). Deliberate: a key pressed (not a repeat, not a modifier
// alone), a mouse button or wheel, the mouse moved some way (12 window
// coordinates within a quarter of a second, over two events at least: not
// the one motion a window gets when it appears under the cursor); a
// controller's button, a trigger pulled past a quarter, a stick pushed past
// its dead zone (Controls, at least 30%). Events of every kind count, while
// the game runs too (uiEvent sees them all). A controller connecting changes
// nothing; the controller in use going, with none left, means the keyboard.
// Before any of that: a controller if one is connected when the menu opens.
enum Method { METHOD_UNSET, METHOD_KEYS, METHOD_PAD };
struct InputUse {
    Method method = METHOD_UNSET;
    float motion = 0;      // mouse movement adding up
    int motionEvents = 0;
    uint64_t motionAt = 0; // its last event (ns)
};
InputUse inputUse;

bool modifierKey(SDL_Scancode k) {
    return (k >= SDL_SCANCODE_LCTRL && k <= SDL_SCANCODE_RGUI) || k == SDL_SCANCODE_CAPSLOCK ||
           k == SDL_SCANCODE_NUMLOCKCLEAR || k == SDL_SCANCODE_SCROLLLOCK || k == SDL_SCANCODE_MODE ||
           k == SDL_SCANCODE_APPLICATION;
}

void noteInput(const SDL_Event* e) {
    Method m = METHOD_UNSET;
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        if (!e->key.repeat && !modifierKey(e->key.scancode)) m = METHOD_KEYS;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL:
        m = METHOD_KEYS;
        break;
    case SDL_EVENT_MOUSE_MOTION: {
        uint64_t now = e->motion.timestamp;
        if (now - inputUse.motionAt > 250000000ull) {
            inputUse.motion = 0;
            inputUse.motionEvents = 0;
        }
        inputUse.motionAt = now;
        inputUse.motion += fabsf(e->motion.xrel) + fabsf(e->motion.yrel);
        if (++inputUse.motionEvents >= 2 && inputUse.motion >= 12) m = METHOD_KEYS;
        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        m = METHOD_PAD;
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        int v = abs(e->gaxis.value);
        bool trigger = e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        int dead = std::max(padStickDeadzone(), 30) * 32767 / 100;
        if (trigger ? v > 8192 : v > dead) m = METHOD_PAD;
        break;
    }
    case SDL_EVENT_GAMEPAD_REMOVED: {
        int count = 0, left = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        for (int i = 0; i < count; i++) left += ids[i] != e->gdevice.which;
        SDL_free(ids);
        if (inputUse.method == METHOD_PAD && left == 0) m = METHOD_KEYS;
        break;
    }
    default:
        break;
    }
    if (m != METHOD_UNSET) {
        if (m != METHOD_KEYS) {
            inputUse.motion = 0;
            inputUse.motionEvents = 0;
        }
        inputUse.method = m;
    }
}

// The way of playing the legend shows now.
bool usingPad() {
    if (inputUse.method == METHOD_UNSET) {
        int count = 0;
        SDL_free(SDL_GetGamepads(&count));
        inputUse.method = count > 0 ? METHOD_PAD : METHOD_KEYS;
    }
    return inputUse.method == METHOD_PAD;
}

// The legend's pieces for the way in use: a controller's buttons as the game
// shows them (its bindings, the controller's own symbols), or the keys bound
// now with Enter, Esc and the arrows (pad.h; a rebinding shows at once).
struct Legend {
    char choose[64], back[64], move[64], section[64], help[64];
};

Legend legend() {
    Legend l;
    auto pad = [](const char* button) {
        const char* g = skin::native() ? skin::actionGlyph(button) : padActionLabel(button);
        return g != nullptr && g[0] != '\0' ? g : nullptr;
    };
    auto join = [](char* out, const char* a, const char* b, const char* sep = "/") {
        std::string t = std::string(a != nullptr ? a : "") + (a != nullptr && b != nullptr ? sep : "") + (b != nullptr ? b : "");
        snprintf(out, 64, "%.63s", t.c_str()); // (a key name is short; a long one is cut)
    };
    if (usingPad()) {
        join(l.choose, pad(BUTTON_CHOOSE), nullptr);
        join(l.back, pad(BUTTON_BACK), nullptr);
        char ud[64], lr[64];
        join(ud, pad("up"), pad("down"), "");
        join(lr, pad("left"), pad("right"), "");
        join(l.move, ud, lr, "");
        join(l.section, pad("l1"), pad("r1"));
        join(l.help, pad("triangle"), nullptr);
    } else {
        join(l.choose, buttonKeyName(BUTTON_CHOOSE), "Enter");
        join(l.back, buttonKeyName(BUTTON_BACK), "Esc");
        // The arrows (the game's font has them; ImGui's own, without a disc, not).
        snprintf(l.move, sizeof(l.move), "%s", skin::native() ? "\xe2\x86\x91\xe2\x86\x93\xe2\x86\x90\xe2\x86\x92" : tr("Arrows"));
        join(l.section, buttonKeyName("l1"), buttonKeyName("r1"));
        snprintf(l.help, sizeof(l.help), "H");
    }
    return l;
}

// What resets a row (menu.h resetKeysText): Square's glyph on a controller,
// else its key and Delete.
const char* resetText() {
    static char text[96];
    if (usingPad()) {
        const char* g = skin::native() ? skin::actionGlyph("square") : padActionLabel("square");
        snprintf(text, sizeof text, "%s", g != nullptr && g[0] != '\0' ? g : "Square");
    } else {
        const char* k = buttonKeyName("square");
        snprintf(text, sizeof text, "%s%sDelete", k != nullptr ? k : "", k != nullptr ? tr(" or ") : "");
    }
    return text;
}

// The legend as lines (the help popup's, where no legend is shown).
std::string legendText() {
    Legend l = legend();
    std::string text;
    auto line = [&](const char* keys, const char* what) {
        if (keys[0] == '\0') return;
        if (!text.empty()) text += "\n";
        text += keys;
        text += ": ";
        text += what;
    };
    line(l.choose, tr("choose"));
    line(l.back, tr("back"));
    line(l.move, tr("move"));
    line(l.section, tr("section"));
    return text;
}

// A click in the menu's own window that hits nothing it can choose (the
// space between its boxes, the legend's text): ImGui would take the cursor
// out of the section to that window; it stays where it was (restored the
// next frame, settingsClickRestore). Not for a command (the cursor goes to it).
void noteSettingsClick() {
    ImGuiContext& g = *GImGui;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && st.inContent && !popupOpen() &&
        g.HoveredWindow == ImGui::GetCurrentWindow() && g.HoveredId == 0 && !st.backWanted) {
        st.restoreContent = true;
    }
}

void settingsClickRestore(ImGuiWindow* content) {
    if (st.restoreContent && content != nullptr) {
        ImGui::FocusWindow(content); // its last item again (ImGui keeps it per window)
        ImGui::SetNavCursorVisible(true);
    }
    st.restoreContent = false;
}

void drawFrame() {
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiStyle& style = ImGui::GetStyle();
    float u = skin::unit();

    // The setup screen (ui.h uiDiscSetup) is the same screen: its two pages
    // and Start and Quit on the commands' arc.
    const int sections = st.setup ? (int)SETUP_SECTIONS : (int)SECTION_COUNT;
    const int entries = st.setup ? (int)SETUP_SECTIONS + 2 : (int)SECTION_COUNT + 1;

    // F1 uses field-choice text for enabled values. Keep this local to
    // Settings; extras viewers can still use their own list selections.
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));

    // The section pane is a nav scope of its own: Right (or Enter, A) on the
    // list goes into it, Esc or B comes back out (ImGui puts the cursor on the
    // pane itself then, from either side: which way it went decides).
    if (st.contentId != 0 && ImGui::GetFocusID() == st.contentId) {
        if (st.inContent) {
            st.focusSections = true;
            st.opened = false;
            sfx::play(sfx::BACK);
        } else {
            st.enterContent = true;
            st.opened = true;
        }
    }

    // Left on an option with nothing to its left (ImGui found nowhere to
    // move) goes back to the section list, as Right on the list comes in.
    ImGuiContext& g = *ImGui::GetCurrentContext();
    bool padNav = (io.BackendFlags & ImGuiBackendFlags_HasGamepad) && (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad);
    if (st.leftFrom != 0) {
        if (g.NavId == st.leftFrom && st.inContent && !popupOpen() && !ImGui::IsAnyItemActive()) {
            st.focusSections = true; // (its sound came with the press: sounds())
            st.opened = false;
        }
        st.leftFrom = 0;
    }
    if (st.inContent && g.NavId != 0 && g.NavCursorVisible && !popupOpen() && !ImGui::IsAnyItemActive() &&
        !capturing() &&
        (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || (padNav && ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)))) {
        st.leftFrom = g.NavId;
    }

    // Leaving: F1 is taken by uiEvent; Esc, Start and B here, when nothing
    // in the menu uses them (a list open, a field being edited, a capture,
    // the cursor in a section).
    bool pad = (io.BackendFlags & ImGuiBackendFlags_HasGamepad) && (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad);
    bool free = st.escapeFree && !st.inContent && !capturing();
    if (st.backWanted) {
        // The legend's back, clicked: as Esc (from the section to the
        // commands, from the commands out).
        st.backWanted = false;
        st.restoreContent = false; // (the click that asked for it: not to be undone)
        if (st.inContent) {
            st.focusSections = true;
            st.opened = false;
            sfx::play(sfx::BACK);
        } else if (!popupOpen()) {
            st.closing = true;
        }
    }
    if (free && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                 (pad && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))) {
        st.closing = true;
    }
    if (pad && !capturing() && !popupOpen() && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false)) {
        st.closing = true;
    }
    // L1/R1: the previous and next section.
    if (pad && !capturing() && !popupOpen() && !ImGui::IsAnyItemActive()) {
        int step = ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) ? -1 : ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) ? 1 : 0;
        if (step != 0) {
            st.section = (st.section + step + sections) % sections;
            if (st.opened) {
                st.enterContent = true; // the next section, open, from its first control
            } else {
                st.focusSections = true;
            }
        }
    }

    // A search result chosen (options.h): its section opens at its row.
    int jumpTo = 0;
    if (!st.setup && opt::takeJump(&jumpTo)) {
        st.section = jumpTo;
        st.opened = true;
        st.jumpFocus = true;
    }
    // Ctrl+F: the Search page, its box taking the keys.
    if (!st.setup && !capturing() && !popupOpen() && !io.WantTextInput &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) {
        st.section = SEARCH;
        st.opened = true;
        st.jumpFocus = true;
        opt::focusSearch();
        sfx::play(sfx::CHOOSE);
    }

    // Commands follow the game's arc, but Up/Down still follow their
    // order (with the game's held-direction repeat: stepped()). Spatial
    // scoring can otherwise jump into the sheet beside them.
    int commandFocus = st.focusSections ? st.section : -1;
    if (g.NavWindow && g.NavWindow->ID == st.listWindow && st.arrowEntry >= 0 &&
        !popupOpen() && !capturing()) {
        int step = (stepped(ImGuiKey_DownArrow) || (pad && stepped(ImGuiKey_GamepadDpadDown))) ? 1 :
                   (stepped(ImGuiKey_UpArrow) || (pad && stepped(ImGuiKey_GamepadDpadUp))) ? -1 : 0;
        if (step) {
            ImGui::NavMoveRequestCancel();
            commandFocus = (st.arrowEntry + step + entries) % entries;
            if (commandFocus < sections) st.section = commandFocus;
            st.focusSections = true;
        }
    }

    // The game's main menu: the commands on its arc, the chosen section's
    // sheet in a box beside them, its description in the message box under
    // it (game mode: at the game's places on the game's screen; compact: in
    // the port's panel, with the keys' legend under it).
    Layout L = layout(st.opened);
    ImRect pane = L.sheet, help = L.help, foot = L.foot;
    st.pane = pane;
    st.centre = L.centre();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Settings", NULL,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    st.listWindow = ImGui::GetCurrentWindow()->ID;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (st.opened) {
        dl->PushClipRectFullScreen();
        skin::box(dl, help.Min, help.Max);
        dl->PopClipRect();
    }

    // The list. It sits in the window itself (not a child): a focus put on
    // it (opening, L1/R1) can move right into the section's child. Dimmed
    // while the cursor is in the section, as the game's main menu is while a
    // sub-menu is up; the red arrow on the entry the cursor is on, over it.
    int arrowWas = st.arrowEntry;
    st.arrowEntry = -1;
    for (int i = 0; i < entries && !st.opened; i++) {
        bool focused;
        const char* name = tr(i >= sections ? (st.setup ? (i == sections ? "Start" : "Quit") : "Back")
                                            : st.setup ? kSetupNames[i] : listNames[i]);
        if (i >= sections) {
            if (listEntry(i, name, false, L.command(i), st.inContent, &focused)) {
                if (!st.setup || i > sections) {
                    st.closing = true; // Back; Quit
                } else if (setupStart()) {
                    st.closing = true; // Start, with valid images
                }
            }
        } else if (listEntry(i, name, st.section == i, L.command(i), st.inContent, &focused)) {
            st.section = i;
            st.enterContent = true;
            st.opened = true;
            sfx::play(sfx::CHOOSE);
        }
        if (focused && ImGui::GetCurrentContext()->NavCursorVisible) {
            st.arrowEntry = i;
        }
        if (i == commandFocus) {
            ImGui::FocusItem(); // the cursor, without choosing
        }
        if (i >= sections) {
            continue;
        }
        if (focused && !st.focusSections) {
            st.section = i; // the section follows the cursor
            // Right goes into it (ImGui's own move scores the tall pane as below).
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || (pad && ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight))) {
                st.enterContent = true;
                st.opened = true;
                sfx::play(sfx::CHOOSE);
            }
        }
    }
    // The arrow where the cursor was when this frame began (a move shows
    // next frame, as the game's: updateCursorPolysLocation after the input).
    if (arrowWas >= 0 && !st.opened) {
        skin::sprite(dl, skin::SPRITE_ARROW, L.command(arrowWas));
    }
    st.focusSections = false;
    // The commands alone until one is chosen: no pane, nothing in it (the
    // game's main menu shows its commands and status; a chosen command's
    // menu then takes the screen). Esc, B or the legend's back come back.
    ImGuiWindow* content = nullptr;
    if (!st.opened) {
        st.inContent = false;
        st.contentId = 0;
        st.shownSection = -1;
        helpText = tr("Choose a section to open it.");
    } else {

    // The section in a child that scrolls inside the box, with the game's
    // list scroll bar at the box's inner left edge (its own, not ImGui's):
    // its rows where the game's item list has its rows (kListText, kListTop);
    // in compact, its name above them.
    // The child from 2 inside the box's top and bottom (where its frame's
    // light line is: what scrolls is cut there), as wide as the box and 12
    // more, so its right margin is 8, not 20.
    float headH = L.game ? 0.0f : 16 * u;
    ImGui::SetCursorScreenPos(ImVec2(pane.Min.x + 8 * u, pane.Min.y + headH + 2 * u));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2((kListText - 8) * u, (kListTop - 2) * u));
    ImGui::BeginChild("content", ImVec2(pane.GetWidth() + 4 * u, pane.GetHeight() - headH - 4 * u),
                      ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    content = ImGui::GetCurrentWindow();
    settingsClickRestore(content);
    // The box, its heading and the scrolled contents in this draw list.
    st.paneDrawList = content->DrawList;
    st.paneDrawList->PushClipRectFullScreen();
    skin::box(st.paneDrawList, pane.Min, pane.Max);
    if (!L.game) {
        st.paneDrawList->AddText(ImVec2(pane.Min.x + kListText * u, pane.Min.y + kListTop * u), IM_COL32_WHITE,
                                 tr(st.setup ? setupSectionNames[st.section] : sectionNames[st.section]));
    }
    st.paneDrawList->PopClipRect();
    skin::focusWindow(); // the JP game's cursor band (none in the US game)
    if (st.shownSection != st.section) {
        helpIndex = 0;
        ImGui::SetScrollY(0.0f); // each section from its top (one child shows them all)
        st.shownSection = st.section;
    }
    // The right stick scrolls the section (text the cursor cannot reach: the licences).
    float stick = ImGui::GetKeyData(ImGuiKey_GamepadRStickDown)->AnalogValue -
                  ImGui::GetKeyData(ImGuiKey_GamepadRStickUp)->AnalogValue;
    if (fabsf(stick) > 0.2f) {
        ImGui::SetScrollY(ImGui::GetScrollY() + stick * 6.0f * u);
    }
    if (st.jumpFocus) {
        // The cursor where the row asks (options.h: a search result's control,
        // the search box), not on the first control.
        ImGui::FocusWindow(ImGui::GetCurrentWindow());
        ImGui::SetNavCursorVisible(true);
        st.jumpFocus = false;
        st.enterContent = false;
    }
    if (st.enterContent) {
        // The cursor on the section's first control (not choosing it).
        ImGui::FocusWindow(ImGui::GetCurrentWindow());
        ImGui::NavInitWindow(ImGui::GetCurrentWindow(), true);
        ImGui::SetNavCursorVisible(true);
        st.enterContent = false;
    }
    blocks.resize(0);
    helpBlocks.clear();
    helpWindow = st.setup || st.section != EXTRAS ? content : nullptr;
    if (st.setup) {
        drawSetupSection(st.section);
    } else {
        drawSection(st.section);
    }
    helpWindow = nullptr;
    {
        // Square (or Delete) on a changed row: back to its defaults (options.h).
        float y = -FLT_MAX;
        if (g.NavWindow == content && g.NavId && g.NavCursorVisible) {
            y = ImGui::WindowRectRelToAbs(content, content->NavRectRel[g.NavLayer]).GetCenter().y;
        } else if (ImGui::IsWindowHovered() && ImGui::IsMousePosValid()) {
            y = io.MousePos.y;
        }
        bool reset = !st.setup && !capturing() && !io.WantTextInput && !popupOpen() && !ImGui::IsAnyItemActive() &&
                     (st.squarePressed || ImGui::IsKeyPressed(ImGuiKey_Delete, false));
        opt::finishPage(y, reset);
    }
    if (!helpBlocks.empty()) {
        // Keyboard/gamepad focus and mouse hover use the same row bounds.
        float y = -FLT_MAX;
        if (ImGui::IsWindowHovered() && ImGui::IsMousePosValid() &&
            (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) y = io.MousePos.y;
        else if (g.NavWindow == content && g.NavId && g.NavCursorVisible) {
            ImRect nav = ImGui::WindowRectRelToAbs(content, content->NavRectRel[g.NavLayer]);
            y = nav.GetCenter().y;
        }
        if (y != -FLT_MAX) {
            helpIndex = 0;
            for (int i = 1; i < int(helpBlocks.size()); i++) {
                if (helpBlocks[i].y > y) break;
                helpIndex = i;
            }
        }
        helpIndex = std::clamp(helpIndex, 0, int(helpBlocks.size()) - 1);
        int described = helpIndex;
        while (described > 0 && helpBlocks[described].text.empty()) --described;
        helpText = helpBlocks[described].text;
    } else {
        Legend keys = legend();
        char text[256];
        if (st.setup) {
            snprintf(text, sizeof(text), "%s", tr("Choose your disc images and BIOS, then Start. Quit leaves without "
                                                  "changing anything."));
        } else if (st.section == EXTRAS) {
            snprintf(text, sizeof(text), "%s", tr("Choose an extra to open it. The game stays paused. Back returns to Settings."));
        } else {
            snprintf(text, sizeof(text), tr("Choose a setting, then use Left and Right to change it. %s returns to the commands."),
                     keys.back[0] != '\0' ? keys.back : tr("Back"));
        }
        helpText = text;
    }
    ImGui::Dummy(ImVec2(0, 2 * u));
    scrollToBlock(content, ImGui::GetCursorScreenPos().y);
    st.inContent = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    ImGui::EndChild();
    st.contentId = ImGui::GetItemID();
    } // st.opened: the pane

    // The scroll bar: the thumb as long as the part shown, where it is; the
    // mouse may drag it or click the track.
    if (st.opened) {
        float trackH = pane.GetHeight(), inner = trackH - 16 * u;
        float total = content->ContentSize.y + content->WindowPadding.y * 2, view = content->Size.y;
        float shown = total > view ? view / total : 1.0f;
        float thumbH = floorf(fmaxf(8 * u, inner * shown) / u) * u;
        float maxScroll = content->ScrollMax.y;
        float thumbY = maxScroll > 0 ? floorf((inner - thumbH) * (content->Scroll.y / maxScroll) / u) * u : 0.0f;
        if (maxScroll > 0) {
            st.paneDrawList->PushClipRectFullScreen();
            skin::scrollBar(st.paneDrawList, pane.Min, trackH, thumbY, thumbH);
            st.paneDrawList->PopClipRect();
            ImGui::SetCursorScreenPos(pane.Min);
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            ImGui::InvisibleButton("##scroll", ImVec2(8 * u, trackH));
            ImGui::PopItemFlag();
            if (ImGui::IsItemActive()) {
                // The thumb's middle follows the mouse.
                float at = (io.MousePos.y - pane.Min.y - 8 * u - thumbH / 2) / fmaxf(1.0f, inner - thumbH);
                ImGui::SetScrollY(content, fminf(1.0f, fmaxf(0.0f, at)) * maxScroll);
            }
        }
    }

    // The description in the message box: its lines where the game's are
    // (10 in, 10 down, 16 apart); more than fits scrolls (the help popup has it all).
    bool helpClicked = false;
    if (st.opened) {
    ImGui::SetCursorScreenPos(ImVec2(help.Min.x + kMessageText * u, help.Min.y + kMessageText * u));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("description", ImVec2(help.GetWidth() - 2 * kMessageText * u,
                                            floorf((help.GetHeight() / u - kMessageText - 6) / 16 + 0.01f) * 16 * u),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    static std::string previousHelp;
    if (previousHelp != helpText) ImGui::SetScrollY(0.0f);
    previousHelp = helpText;
    textLines(helpText.empty() ? tr("Choose an option for its description.") : helpText.c_str());
    helpClicked = ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    ImGui::EndChild();
    } // st.opened: the description
    if (!capturing() && !io.WantTextInput && !popupOpen() && (helpClicked || st.helpWanted ||
        ImGui::IsKeyPressed(ImGuiKey_H, false) || (pad && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false)))) {
        ImGui::OpenPopup("Option help");
    }
    st.helpWanted = false;
    nextModal();
    bool helpOpen = ImGui::BeginPopupModal("Option help", nullptr, ImGuiWindowFlags_AlwaysAutoResize |
                                           ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    modalStyleEnd();
    if (helpOpen) {
        skin::frameWindow(true);
        textLines(helpText.c_str());
        if (L.game) {
            // The keys, as the game's legend would say them (no legend in game mode).
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
            textLines(legendText().c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::Button(tr("Back")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
            (pad && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    noteSettingsClick();
    if (L.game) {
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    // The controls of the menu itself for the way of playing in use
    // (legend()); shorter where the window is narrow.
    ImDrawList* fl = ImGui::GetWindowDrawList();
    Legend keys = legend();
    struct Part {
        char text[128];
    };
    auto part = [](Part& p, const char* keysText, const char* what, bool last = false) {
        if (keysText[0] == '\0') {
            p.text[0] = '\0';
        } else if (what != nullptr) {
            snprintf(p.text, sizeof(p.text), "%s: %s%s", keysText, what, last ? "" : "  ");
        } else {
            snprintf(p.text, sizeof(p.text), "%s%s", keysText, last ? "" : "  ");
        }
    };
    Part variants[3][5];
    // On a changed setting the reset keys join the legend (no Reset button on the row).
    const char* reset = st.inContent && opt::cursorRowChanged() ? resetText() : "";
    part(variants[0][0], keys.choose, tr("choose"));
    part(variants[0][1], keys.back, tr("back"));
    part(variants[0][2], keys.move, tr("move"));
    part(variants[0][3], keys.section, tr("section"), reset[0] == '\0');
    part(variants[0][4], reset, tr("reset"), true);
    part(variants[1][0], keys.choose, nullptr);
    part(variants[1][1], keys.back, nullptr);
    part(variants[1][2], keys.move, nullptr);
    part(variants[1][3], keys.section, nullptr, reset[0] == '\0');
    part(variants[1][4], reset, tr("reset"), true);
    part(variants[2][0], keys.choose, tr("choose"));
    part(variants[2][1], keys.back, tr("back"), reset[0] == '\0');
    part(variants[2][2], reset, tr("reset"), true);
    variants[2][3].text[0] = variants[2][4].text[0] = '\0';
    auto partWidth = [&](const Part& p) {
        return ImGui::CalcTextSize(p.text).x;
    };
    char helpLabel[96];
    snprintf(helpLabel, sizeof(helpLabel), "%s%s", keys.help, keys.help[0] != '\0' ? tr(": help") : "");
    float helpWidth = ImGui::CalcTextSize(helpLabel).x;
    float room = foot.GetWidth() - helpWidth - 16 * u;
    int pick = 2;
    for (int v = 0; v < 3; v++) {
        float w = 0;
        for (int i = 0; i < 5; i++) {
            w += partWidth(variants[v][i]);
        }
        if (w <= room) {
            pick = v;
            break;
        }
    }
    float x = foot.Min.x + 4 * u, y = foot.Min.y + 2 * u;
    // Its back and help parts can be clicked (the others say what a key does
    // where the cursor is): hit by hand, not ImGui items (they are no cursor
    // stops, and the click keeps the cursor where it is: settingsClick).
    auto clicked = [&](ImVec2 at, const char* text) {
        ImVec2 size = ImGui::CalcTextSize(text);
        return size.x > 0 && !popupOpen() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
               ImRect(at, ImVec2(at.x + size.x, at.y + size.y + 2 * u)).Contains(io.MousePos);
    };
    for (int i = 0; i < 5; i++) {
        const Part& p = variants[pick][i];
        fl->AddText(ImVec2(x, y), ImGui::GetColorU32(ImGuiCol_Text), p.text);
        if (i == 1 && clicked(ImVec2(x, y), p.text)) st.backWanted = true;
        x += ImGui::CalcTextSize(p.text).x;
    }
    fl->AddText(ImVec2(foot.Max.x - helpWidth - 4*u, y), ImGui::GetColorU32(ImGuiCol_Text), helpLabel);
    if (clicked(ImVec2(foot.Max.x - helpWidth - 4 * u, y), helpLabel)) st.helpWanted = true;
    (void)style;
    ImGui::End();
    ImGui::PopStyleColor();
}

// The game's menu sounds for what the cursor did this frame: a move, a
// choice (a button, a box ticked, a slider or a field taken); leaving
// sounds where it happens (drawFrame, uiEvent).
void sounds() {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiIO& io = ImGui::GetIO();
    // The game's cursor sound comes with each direction it reads, a press or
    // a held one's repeat, whether the cursor can move or not, and with L1
    // and R1 (updateMenuInput: effect 1); not while text is typed or a key
    // is being bound.
    static const ImGuiKey directions[] = {ImGuiKey_UpArrow,         ImGuiKey_DownArrow,        ImGuiKey_LeftArrow,
                                          ImGuiKey_RightArrow,      ImGuiKey_GamepadDpadUp,    ImGuiKey_GamepadDpadDown,
                                          ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight};
    bool step = false, held = false;
    bool other = sfx::plays() != st.playsSeen; // this frame already sounded (Right choosing a section)
    if (!io.WantTextInput && !capturing() && !st.padQuiet && !other) {
        for (ImGuiKey k : directions) {
            step |= stepped(k);
            held |= ImGui::IsKeyDown(k);
        }
        step |= ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false);
    }
    // The cursor moved otherwise (Tab, Home, End, a page key, a click), not
    // as the result of what just sounded (going back to the commands: the
    // cancel button's sound, not the cursor's).
    if (sfx::plays() != st.playsSeen) st.quietUntil = st.frames + 4;
    bool moved = g.NavId != st.lastNav && g.NavId != 0 && st.lastNav != 0 && g.NavCursorVisible && !held &&
                 st.frames >= st.quietUntil;
    if (step || moved) {
        sfx::play(sfx::MOVE);
    }
    st.lastNav = g.NavId;
    if ((g.ActiveIdIsJustActivated || g.NavActivateId != 0) && st.frames >= st.quietUntil) {
        sfx::play(sfx::CHOOSE);
    }
    if (sfx::plays() != st.playsSeen) st.quietUntil = st.frames + 4;
    st.playsSeen = sfx::plays();
}

void frame() {
    if (extrasJukeboxTick) extrasJukeboxTick();
    for (ImDrawList* list : projectedLists) IM_DELETE(list);
    projectedLists.clear();
    updateScale();
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    {
        // ImGui in window pixels: each game pixel a whole block of them
        // (uiEvent brings the mouse into pixels too).
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(presentWindow(), &pw, &ph);
        ImGui::GetIO().DisplaySize = ImVec2((float)pw, (float)ph);
        ImGui::GetIO().DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        // One vblank a frame, as the game's menu counts its time (its
        // animations and the pad's repeat are in frames), whatever the
        // clock did. The game's held direction steps again 32 vblanks after
        // its press, then every 4 (src/main/pad.c); ImGui's navigation
        // repeats after 0.72 KeyRepeatDelay, then every 0.8 KeyRepeatRate,
        // and a key held t seconds repeats once t passes the delay: at
        // frame 32, 36, 40...
        ImGuiIO& io = ImGui::GetIO();
        float vblank = (float)hwVSyncPeriod();
        io.DeltaTime = vblank;
        io.KeyRepeatDelay = 31.5f * vblank / 0.72f;
        io.KeyRepeatRate = 4.0f * vblank / 0.80f;
    }
    feedButtons(st.frames == st.openedAt);
    if (st.padQuiet) {
        st.padQuiet = gamepadHeld();
    }
    // A binding takes the gamepad; the buttons that opened the menu (Guide,
    // L3+R3, anything else held) are not a press in it. Navigation is off
    // meanwhile, but ImGui keeps following the buttons (clearing HasGamepad
    // would forget them, and a button still held would then press).
    ImGuiIO& io = ImGui::GetIO();
    if (capturing() || st.padQuiet) {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    } else {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    }
    // The menu comes in and goes as the game's sub-menus do (motion.h),
    // turned about the game screen's middle in game mode (the projection's
    // centre), else the window's.
    Layout L = layout(st.opened);
    st.centre = L.game ? L.centre() : ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2);
    unsigned age = unsigned(st.frames - st.openedAt);
    bool moving = !st.setup && (age < menuMotion::OPENING_FRAMES || st.leaving);
    menuMotion::View view = st.leaving ? menuMotion::leaving(st.centre, skin::unit(), unsigned(st.frames - st.closedAt))
                                       : menuMotion::opening(st.centre, skin::unit(), age);
    if (!st.setup && ImGui::IsMousePosValid(&st.mouse)) {
        ImVec2 mouse = st.mouse;
        if (moving) {
            if (st.leaving) mouse = ImVec2(-FLT_MAX, -FLT_MAX);
            else if (!view.inverse(mouse, &mouse)) mouse = ImVec2(-FLT_MAX, -FLT_MAX);
        }
        io.AddMousePosEvent(mouse.x, mouse.y);
    }
    ImGui::NewFrame();
    skin::beginFrame();
    drawFrame();
    sounds();
    skin::endFrame(st.listWindow);
    ImGui::Render();
    ImDrawData* data = ImGui::GetDrawData();
    for (int i = 0; i < data->CmdLists.Size; i++) {
        ImDrawList* original = data->CmdLists[i];
        bool hides = skin::hidesContents(original);
        if (!moving && !hides) continue;
        ImDrawList* copy = original->CloneOutput();
        projectedLists.push_back(copy);
        data->CmdLists[i] = copy;
        if (hides) skin::hideUnopened(original, copy); // a message box still opening: its frame alone
        if (moving) menuMotion::drawList(copy, view, st.leaving);
    }
    data->TotalVtxCount = data->TotalIdxCount = 0;
    for (ImDrawList* list : data->CmdLists) {
        data->TotalVtxCount += list->VtxBuffer.Size; data->TotalIdxCount += list->IdxBuffer.Size;
    }
    st.escapeFree = !popupOpen() && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput;
    st.rendered = true;
}

// ---- suspend and resume -------------------------------------------------------------

// The system sends the program to the background (SDL's app lifecycle: a
// handheld's suspend, a phone's app switch), or minimises its window with
// pause_minimized on: the game waits in the vblank, as it does while this
// menu is open: its clock stands still (hw.c hwPause: the time away is
// taken out, so it goes on where it was, without catching up), the sound
// fades out and back in (spu/output.c), nothing is drawn. Back (foreground,
// restored, shown): it goes on at the next vblank's pace.
bool pauseMinimized() {
    static bool described;
    if (!described) {
        described = true;
        settingsDescribe("pause_minimized", "pause_minimized: 1 pauses the game while its window is minimised "
                                            "(0, default: it plays on)");
    }
    return settingsBool("pause_minimized", 0) != 0;
}

void lifecycle(const SDL_Event* e) {
    switch (e->type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        st.suspendWanted = true;
        st.suspendWhy = "the system put it in the background";
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
        if (pauseMinimized()) {
            st.suspendWanted = true;
            st.suspendWhy = "its window is minimised";
        }
        break;
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
    case SDL_EVENT_DID_ENTER_FOREGROUND:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
        st.suspendWanted = false;
        break;
    default:
        break;
    }
}

// The scripted foreground (XG_UI_SCRIPT "background <ms>").
void scriptedResume() {
    if (st.resumeAt != 0 && SDL_GetTicksNS() >= st.resumeAt) {
        st.resumeAt = 0;
        SDL_Event e = {};
        e.type = SDL_EVENT_WILL_ENTER_FOREGROUND;
        SDL_PushEvent(&e);
        e.type = SDL_EVENT_DID_ENTER_FOREGROUND;
        SDL_PushEvent(&e);
    }
}

// In the vblank (uiVBlank) or the menu's loop: until it is back.
void suspend(bool menuOpen) {
    uint64_t from = SDL_GetTicksNS();
    unsigned vblank = hwVSyncCount();
    fprintf(stderr, "ui: paused at vblank %u: %s\n", vblank, st.suspendWhy ? st.suspendWhy : "background");
    if (!menuOpen) {
        hwPause(1);
        spuOutputPause(1);
    }
    rumbleStopAll(); // no pad vibrating while away
    while (st.suspendWanted) {
        padPumpEvents(); // the window's events, quitting too
        scriptedResume();
        hwPausedFrame(); // (the stall watchdog: waiting here is no stall)
        SDL_WaitEventTimeout(NULL, 50);
    }
    if (!menuOpen) {
        spuOutputPause(0);
        hwPause(0);
    }
    fprintf(stderr, "ui: going on at vblank %u after %.2f s away\n", hwVSyncCount(),
            (double)(SDL_GetTicksNS() - from) * 1e-9);
    if (getenv("XG_UI_SUSPEND_LOG") != nullptr) st.resumeLog = 30;
    st.lastVBlankNs = 0;
}

void run() {
    if (!skin::native() && skin::init()) {
        st.unit = 0; // the game's look now (a disc came after a first opening without one): its style
    }
    hwPause(1);
    spuOutputPause(1);
    rumbleStopAll(); // the game is paused: its vibration too (the extras open from here)
    {
        int lw = 0, lh = 0, pw = 0, ph = 0;
        SDL_GetWindowSize(presentWindow(), &lw, &lh);
        SDL_GetWindowSizeInPixels(presentWindow(), &pw, &ph);
        fprintf(stderr, "ui: settings menu open (vblank %u, window %dx%d, %dx%d pixels); the game is paused\n",
                hwVSyncCount(), lw, lh, pw, ph);
    }
    st.open = true;
    st.closing = false;
    st.leaving = false;
    st.section = st.setup ? st.setupSection : 0;
    st.shownSection = -1;
    st.arrowEntry = -1;
    st.leftFrom = 0;
    st.focusSections = true;
    st.enterContent = false;
    st.inContent = false;
    st.opened = false;
    st.escapeFree = false;
    st.padQuiet = true;
    st.lastNav = 0;
    st.openedAt = st.frames;
    sfx::opened();
    if (!st.setup) {
        sfx::play(sfx::OPEN); // as a sub-menu's box starts turning in (processLoadSaveMenuSub1)
    }
    for (int i = 0; i < st.releaseCount; i++) {
        // What the script pressed to open the menu is let go in it (no vblanks now).
        if (st.releases[i].onVBlank) {
            st.releases[i].onVBlank = false;
            st.releases[i].at = st.frames + 2;
        }
    }
    // ImGui's keys are as they were (uiEvent passes it the releases made while
    // closed), so a key held from the game is no press in the menu.
    ImGui::SetNavCursorVisible(true);
    sectionsOpened();
    uint64_t next = SDL_GetTicksNS();
    while (true) {
        padPumpEvents(); // the window's events: ImGui's through uiEvent, the hotkeys, quitting
        runScript(false, st.frames);
        if (st.closing && !st.leaving) {
            st.leaving = true;
            st.closedAt = st.frames;
            // As a sub-menu starts turning away (processLoadSaveMenuSub1Alt2).
            sfx::play(st.setup ? sfx::BACK : sfx::CLOSE);
        }
        if (st.leaving && st.frames - st.closedAt >= menuMotion::LEAVING_FRAMES) break;
        if (st.suspendWanted) {
            suspend(true); // nothing drawn meanwhile; the menu as it was after
            next = SDL_GetTicksNS();
        }
        frame();
        gpuPresentMenu(st.shot);
        st.shot = NULL;
        st.rendered = false;
        hwPausedFrame();
        st.frames++;
        uint64_t now = SDL_GetTicksNS();
        next += (uint64_t)(hwVSyncPeriod() * 1e9); // the menu redraws at the vblank rate
        if (next > now) {
            SDL_DelayNS(next - now);
        } else {
            next = now;
        }
    }
    runScript(false, st.frames, true);
    releaseButtons();
    // Nothing of the menu's left on the window: text input, mouse capture, its cursor shape.
    SDL_StopTextInput(presentWindow());
    SDL_CaptureMouse(false);
    SDL_SetCursor(SDL_GetDefaultCursor());
    st.open = false;
    settingsSave();
    if (extrasJukeboxEnd) extrasJukeboxEnd();
    spuOutputPause(0);
    hwPause(0);
    fprintf(stderr, "ui: settings menu closed; the game goes on\n");
}

} // namespace

// ---- for sections.cpp ------------------------------------------------------------------

void textLines(const char* text) {
    // ImGui's wrapped text puts its lines a font height (13) apart; the
    // game's are 16 apart (processLoadSaveMenuSub5, the lists): each line an
    // item of its own. In the menu's language (l10n.h).
    text = tr(text);
    ImFont* font = ImGui::GetFont();
    float size = ImGui::GetFontSize(), width = ImGui::GetContentRegionAvail().x;
    const char* end = text + strlen(text);
    if (text == end) {
        ImGui::NewLine();
        return;
    }
    for (const char* p = text; p < end;) {
        const char* nl = (const char*)memchr(p, '\n', (size_t)(end - p));
        const char* lineEnd = nl != nullptr ? nl : end;
        if (p == lineEnd) {
            ImGui::NewLine();
        }
        while (p < lineEnd) {
            const char* cut = font->CalcWordWrapPosition(size, p, lineEnd, width);
            if (cut <= p) {
                unsigned c;
                cut = p + ImTextCharFromUtf8(&c, p, lineEnd); // a word wider than the line
            }
            ImGui::TextUnformatted(p, cut);
            p = cut;
            while (p < lineEnd && *p == ' ') p++;
        }
        p = nl != nullptr ? nl + 1 : end;
    }
}

void nextModal() {
    // Game mode: the message box's width and left edge (processLoadSaveMenuSub5,
    // as askYesNoQuestion's prompts), its text 10 in; its bottom on the message
    // box's, growing up if it holds more. Compact: as wide as its text wants up
    // to 360 game pixels, in the middle of the window.
    Layout L = layout(st.opened);
    float u = L.u;
    ImGuiIO& io = ImGui::GetIO();
    if (L.game) {
        float w = kMessageBox.w * u;
        ImGui::SetNextWindowPos(L.at(kMessageBox.x, kMessageBox.y + kMessageBox.h), ImGuiCond_Always, ImVec2(0, 1));
        ImGui::SetNextWindowSizeConstraints(ImVec2(w, kMessageBox.h * u), ImVec2(w, io.DisplaySize.y - 16 * u));
    } else {
        float w = std::min(360 * u, io.DisplaySize.x - 40 * u);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, io.DisplaySize.y - 16 * u));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kMessageText * u, kMessageText * u));
    st.modalStyles++;
}

void modalStyleEnd() {
    if (st.modalStyles > 0) {
        ImGui::PopStyleVar(st.modalStyles);
        st.modalStyles = 0;
    }
}

float scale() {
    return st.unit > 0 ? st.unit : 1.0f;
}

void markBlock() {
    if (opt::indexing()) return; // (the Search page's invisible pass)
    blocks.push_back(ImGui::GetCursorScreenPos().y);
    if (helpWindow == ImGui::GetCurrentWindow()) helpBlocks.push_back({ImGui::GetCursorScreenPos().y, {}});
}

SDL_Window* window() {
    return presentWindow();
}

const char* resetKeysText() {
    return resetText();
}

void closeMenu() {
    st.closing = true;
}

void showSetupSection(int section) {
    st.section = section;
    st.enterContent = true;
}

void hint(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char text[4096];
    vsnprintf(text, sizeof(text), tr(fmt), ap); // l10n.h: the format in the menu's language
    va_end(ap);
    opt::noteHint(text); // the Search page's index
    if (helpWindow == ImGui::GetCurrentWindow() && !helpBlocks.empty()) {
        if (!helpBlocks.back().text.empty()) helpBlocks.back().text += "\n";
        helpBlocks.back().text += text;
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    textLines(text);
    ImGui::PopStyleColor();
}

void overrideNote(const char* key) {
    if (settingsPinned(key)) {
        hint("Locked for this online session: both players play with the host's setting.");
        return;
    }
    const char* v = settingsOverride(key);
    if (v == NULL) {
        return;
    }
    char name[64] = "XG_";
    size_t n = 3;
    for (const char* p = key; *p != '\0' && n + 1 < sizeof(name); p++) {
        name[n++] = (char)(isalnum((unsigned char)*p) ? toupper((unsigned char)*p) : '_');
    }
    name[n] = '\0';
    hint("Set by the environment: %s=%s (a change here replaces it for this run)", name, v);
}

} // namespace menu

using namespace menu;

// ---- ui.h ----------------------------------------------------------------------------------

extern "C" void uiRequestToggle(void) {
    if (st.open && capturing()) {
        return; /* binding a gamepad button: Guide or L3+R3 are not a request */
    }
    if (st.open) {
        st.closing = true;
    } else {
        st.want = true;
    }
}

extern "C" int uiIsOpen(void) {
    return st.open;
}

extern "C" int uiEvent(const SDL_Event* e) {
    noteInput(e);
    lifecycle(e);
    if ((e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP) && e->key.scancode < SDL_SCANCODE_COUNT) {
        keyHeld[e->key.scancode] = e->key.down;
    }
    if (!st.open) {
        // Keys and buttons let go while the game runs: ImGui must not find
        // them still down when the menu opens again.
        if (st.ready && (e->type == SDL_EVENT_KEY_UP || e->type == SDL_EVENT_MOUSE_BUTTON_UP)) {
            ImGui_ImplSDL3_ProcessEvent(e);
        }
        return 0;
    }
    if (capturing() && captureEvent(e)) {
        return 1;
    }
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (e->key.scancode == SDL_SCANCODE_F1) {
            if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
                st.closing = true;
            }
            return 1;
        }
        // The display hotkeys and Alt+Enter work in the menu too (pad.c).
        if ((e->key.scancode >= SDL_SCANCODE_F2 && e->key.scancode <= SDL_SCANCODE_F12) ||
            ((e->key.scancode == SDL_SCANCODE_RETURN || e->key.scancode == SDL_SCANCODE_KP_ENTER) &&
             (e->key.mod & SDL_KMOD_ALT))) {
            return 0;
        }
        ImGui_ImplSDL3_ProcessEvent(e);
        return 1;
    case SDL_EVENT_MOUSE_MOTION: {
        // Into window pixels, as ImGui works (frame()).
        SDL_Event m = *e;
        m.motion.x *= st.pixelsPerUnit;
        m.motion.y *= st.pixelsPerUnit;
        st.mouse = ImVec2(m.motion.x, m.motion.y);
        ImGui_ImplSDL3_ProcessEvent(&m);
        return 1;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.button == SDL_BUTTON_RIGHT) {
            // The right button goes back, as Esc: out of a list or a field,
            // from an option to the commands, from the commands out.
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, e->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            return 1;
        }
        ImGui_ImplSDL3_ProcessEvent(e);
        return 1;
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        ImGui_ImplSDL3_ProcessEvent(e);
        return 1;
    default:
        // Window, focus and device events: ImGui sees them, and so does pad.c.
        ImGui_ImplSDL3_ProcessEvent(e);
        return 0;
    }
}

extern "C" int uiVBlank(void) {
    if (!st.scriptLoaded) {
        st.scriptLoaded = true;
        loadScript();
    }
    if (st.stepCount > 0) {
        runScript(true, hwVSyncCount());
    }
    if (st.suspendWanted) {
        suspend(false);
    }
    static const bool suspendLog = getenv("XG_UI_SUSPEND_LOG") != nullptr;
    if (suspendLog) {
        // XG_UI_SUSPEND_LOG: the real time between the vblanks after a
        // resume (the pace goes on: no burst of catching up), and after a
        // second without vblanks and without an event (the system slept, a
        // debugger stopped it: hw.c skips its clock ahead then).
        uint64_t now = SDL_GetTicksNS();
        if (st.lastVBlankNs != 0 && now - st.lastVBlankNs > 1000000000ull && st.resumeLog == 0) {
            fprintf(stderr, "ui: no vblank for %.2f s without an event (vblank %u)\n",
                    (double)(now - st.lastVBlankNs) * 1e-9, hwVSyncCount());
            st.resumeLog = 30;
        } else if (st.resumeLog > 0 && st.lastVBlankNs != 0) {
            fprintf(stderr, "ui: after resume: vblank %u %.2f ms\n", hwVSyncCount(),
                    (double)(now - st.lastVBlankNs) * 1e-6);
            st.resumeLog--;
        }
        st.lastVBlankNs = now;
    }
    if (!st.want) {
        return 0;
    }
    st.want = false;
    if (!begin()) {
        return 0;
    }
    run();
    return 1;
}

extern "C" int uiDiscSetup(const char* region, int flags, char* chosen, unsigned long size) {
    if (!begin()) return 0;
    if (!st.scriptLoaded) { st.scriptLoaded = true; loadScript(); }
    discSetupOpened(region, (flags & UI_SETUP_NO_DISC) != 0);
    st.setup = true;
    st.setupSection = (flags & UI_SETUP_BIOS) ? SETUP_BIOS : SETUP_DISCS;
    run();
    st.setup = false;
    st.want = false;
    if (discSetupAccepted() && chosen != nullptr && size > 0) {
        snprintf(chosen, size, "%s", discSetupChosen());
    }
    return discSetupAccepted();
}

extern "C" void uiVulkanReady(const UiVulkan* dev) {
    st.dev = *dev;
    st.haveDevice = true;
    sectionsVulkan(dev->physical, dev->getInstanceProcAddr, dev->instance);
}

extern "C" void uiRecord(void* cmd, int w, int h, int format, const int* picture) {
    if (!st.open) {
        const char* text = picture ? regionalSubtitle() : nullptr;
        if (!text || !begin() || !skin::native()) return;
        // Zoom may put the movie's edges outside the presentation target.
        // Captions belong to the visible picture, including after a resize.
        ImVec2 visibleMin(float(std::max(0,picture[0])), float(std::max(0,picture[1])));
        ImVec2 visibleMax(float(std::min(w,picture[0]+picture[2])), float(std::min(h,picture[1]+picture[3])));
        float visibleWidth=visibleMax.x-visibleMin.x, visibleHeight=visibleMax.y-visibleMin.y;
        if (visibleWidth<16 || visibleHeight<24) return;
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(float(w), float(h));
        io.DisplayFramebufferScale = ImVec2(1, 1);
        ImGui::NewFrame();
        float u = std::max(1.0f, floorf(std::min(visibleWidth/320, visibleHeight/224)));
        ImFont* font = io.FontDefault != nullptr ? io.FontDefault : io.Fonts->Fonts[0];
        float width;
        ImVec2 size;
        do {
            width=std::min(288*u, visibleWidth-8*u);
            size=font->CalcTextSizeA(14*u, FLT_MAX, width, text);
            if (size.y+16*u<=visibleHeight || u<=1) break;
            --u;
        } while (true);
        ImVec2 at(floorf(visibleMin.x+(visibleWidth-size.x)*0.5f),
                  floorf(visibleMax.y-size.y-8*u));
        ImDrawList* overlay=ImGui::GetForegroundDrawList();
        overlay->PushClipRect(visibleMin,visibleMax,true);
        overlay->AddText(font,14*u,at,IM_COL32_WHITE,text,nullptr,width);
        overlay->PopClipRect();
        ImGui::Render();
    } else if (!st.rendered || !st.vkReady) return;
    ImDrawData* dd = ImGui::GetDrawData();
    if (dd == NULL || dd->DisplaySize.x <= 0 || dd->DisplaySize.y <= 0) {
        return;
    }
    if ((VkFormat)format != st.pipelineFormat) {
        // The first target, or a swapchain of another format: its pipeline.
        if (st.pipelineFormat != VK_FORMAT_UNDEFINED) {
            PFN_vkDeviceWaitIdle waitIdle =
                (PFN_vkDeviceWaitIdle)loadVk("vkDeviceWaitIdle", &st.dev);
            waitIdle((VkDevice)st.dev.device);
        }
        VkFormat f = (VkFormat)format;
        ImGui_ImplVulkan_PipelineInfo pi = {};
        pi.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pi.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        pi.PipelineRenderingCreateInfo.pColorAttachmentFormats = &f;
        ImGui_ImplVulkan_CreateMainPipeline(&pi);
        st.pipelineFormat = f;
    }
    // The game's picture darkened as its own menu darkens it: 0x80 of 0xFF
    // off each channel of its 5-bit colours (16 of 31: 132 of 255 in 8 bits).
    if (st.open) dim::record((VkCommandBuffer)cmd, w, h, (VkFormat)format, 132.0f / 255.0f);
    // Drawn to the target's size whatever the window's is now.
    dd->FramebufferScale = ImVec2(w / dd->DisplaySize.x, h / dd->DisplaySize.y);
    ImGui_ImplVulkan_RenderDrawData(dd, (VkCommandBuffer)cmd);
}

extern "C" void uiVulkanShutdown(void) {
    for (ImDrawList* list : projectedLists) IM_DELETE(list);
    projectedLists.clear();
    dim::shutdown();
    if (st.vkReady) {
        ImGui_ImplVulkan_Shutdown();
        st.vkReady = false;
    }
    st.haveDevice = false;
}
