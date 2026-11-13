/* Real SDL virtual devices exercise profile persistence and input boundaries.
 * Run write, read, script and script2 in separate processes, with the same
 * directory; ini runs alone, in a directory of its own. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "system/pad.h"
#include "system/mouse.h"
#include "system/settings.h"
#include "system/system.h"
#include "ui/ui.h"
#include "test/env.h"
#include "gpu/shaders/prompt.glsl"

void hwSafePoint(void) {}
uint32_t hwVSyncCount(void) { return 0; }
int slowdownLogVBlank(int64_t* n) { return 0; }
int slowdownBiosMissed(void) { return 0; }
void uiRequestToggle(void) {}
int uiEvent(const SDL_Event* e) { return 0; }
int uiVBlank(void) { return 0; }
void quitNow(void) { exit(3); }
int cdSwapRequest(void) { return 0; }
void presentShowNote(const char* s) {}
void presentCycleAspect(void) {}
void presentCycleScale(void) {}
void hiresToggleUpscale(void) {}
void presentCycleFilter(void) {}
void presentCycleFill(void) {}
void presentToggleFullscreen(void) {}
void presentWindowEvent(const SDL_Event* e) {}
/* mouse.c's pointing: nothing to point at here */
int presentWindowToDisplay(float x, float y, float* dx, float* dy) { return 0; }
int pcWideGetMode(void) { return 0; }
int mouseMenuPoint(float x, float y, MousePlan* plan) { return 0; }
int mouseMenuCheck(int kind, int arg) { return 1; }
int mouseBattlePoint(float x, float y, MousePlan* plan) { return 0; }
int mouseBattleCheck(int target) { return 1; }
int mouseFieldTurnReady(void) { return 1; }
void rumbleGamePoll(float* large, float* small) { *large = *small = 0; } /* rumble.c's enhanced sources */
/* netplay.c: no session here */
int netplayActive(void) { return 0; }
void netplayPace(void) {}
void netplayIdle(void) {}
void netplayFrame(unsigned local, unsigned* port1, unsigned* port2) {}
void InitPAD(void*, s32, void*, s32);
void StartPAD(void);
static int checks;
#define CHECK_DETAIL SDL_GetError()
#include "test/check.h"

static SDL_JoystickID device(const char* name, Uint16 vendor, Uint16 product, const char* type) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.vendor_id = vendor;
    desc.product_id = product;
    desc.name = name;
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (!id) return 0;
    char guid[33], mapping[600];
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof(guid));
    snprintf(mapping, sizeof(mapping), "%s,%s,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftstick:b7,"
             "rightstick:b8,leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,"
             "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,type:%s,", guid, name, type);
    if (!SDL_SetGamepadMapping(id, mapping)) return 0;
    return id;
}

static void notify(Uint32 type, SDL_JoystickID id) {
    SDL_Event e = {0};
    e.type = type;
    e.gdevice.which = id;
    padDeviceEvent(&e);
}

static int setting(const char* key, const char* value) {
    const char* v = settingsGet(key);
    return v != NULL && strcmp(v, value) == 0;
}

static void neutral(SDL_Joystick* j) {
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) {
        SDL_SetJoystickVirtualAxis(j, a, a >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? SDL_JOYSTICK_AXIS_MIN : 0);
    }
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) SDL_SetJoystickVirtualButton(j, b, false);
    SDL_UpdateJoysticks();
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char path[1200];
    snprintf(path, sizeof(path), "%s/settings.ini", argv[1]);
    testSetEnv("XG_SETTINGS", path);
    if (strncmp(argv[2], "script", 6) == 0) {
        snprintf(path, sizeof(path), "%s/input.pad", argv[1]);
        FILE* f = fopen(path, "w");
        CHECK(f != NULL && fprintf(f, "0 0080\n") > 0 && fclose(f) == 0);
        testSetEnv("XG_PAD_SCRIPT", path);
    }
    if (strcmp(argv[2], "script2") == 0) {
        /* port 2 from its own script (the Battle arena's player 2) */
        snprintf(path, sizeof(path), "%s/input2.pad", argv[1]);
        FILE* f = fopen(path, "w");
        CHECK(f != NULL && fprintf(f, "0 1020\n") > 0 && fclose(f) == 0);
        testSetEnv("XG_PAD_SCRIPT2", path);
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES, "", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,
                            "0xc0de/0x0001,0xc0de/0x0002", SDL_HINT_OVERRIDE);
    CHECK(SDL_Init(SDL_INIT_GAMEPAD));
    SDL_JoystickID ps = device("Polish PlayStation", 0xc0de, 0x0001, "ps4");
    SDL_JoystickID xbox = device("Polish Xbox", 0xc0de, 0x0002, "xboxone");
    CHECK(ps != 0 && xbox != 0);
    SDL_Joystick* pj = SDL_OpenJoystick(ps);
    SDL_Joystick* xj = SDL_OpenJoystick(xbox);
    CHECK(pj != NULL && xj != NULL);
    neutral(pj); neutral(xj);
    if (strcmp(argv[2], "ini") == 0) {
        /* Hand-written values for padInit's loaders: the PlayStation pad's
         * profile is keyed by its GUID (a virtual device has no serial). */
        char guid[33];
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(ps), guid, sizeof(guid));
        snprintf(path, sizeof(path), "%s/settings.ini", argv[1]);
        FILE* f = fopen(path, "w");
        CHECK(f != NULL && fprintf(f, "key_cross=Space,none\nkey_select=none\npad_l2=righttrigger\n"
                                      "pad_cross=none\npad_circle=bogus\npad_stick_deadzone=200\n"
                                      "pad_%s_stick_deadzone=-5\n", guid) > 0 && fclose(f) == 0);
    }
    padInit();
    if (strcmp(argv[2], "ini") == 0) {
        padSelectProfile(-1);
        CHECK(padKey(4, 0) == SDL_SCANCODE_SPACE && padKey(4, 1) == SDL_SCANCODE_UNKNOWN);
        CHECK(padKey(13, 0) == SDL_SCANCODE_UNKNOWN && padKey(13, 1) == SDL_SCANCODE_UNKNOWN);
        CHECK(padGamepadInput(10) == PAD_INPUT_RIGHT_TRIGGER);
        CHECK(padGamepadInput(4) == PAD_INPUT_NONE);
        CHECK(padGamepadInput(5) == SDL_GAMEPAD_BUTTON_EAST); /* bogus keeps Circle's default */
        CHECK(padStickDeadzone() == 95);
        /* The device profile's own clamp; the other pad inherits the global 95. */
        CHECK(padProfileDevice(0) == ps && padProfileDevice(1) == xbox);
        padSelectProfile(0);
        CHECK(padStickDeadzone() == 0);
        padSelectProfile(1);
        CHECK(padStickDeadzone() == 95);
        printf("pad settings.ini (%s): %d checks passed\n", argv[2], checks);
        SDL_Quit(); return 0;
    }
    /* Regional conventions compose with bindings; both face pairs change,
     * and directions/shoulders/Start are untouched. */
    padSetRegionalControls(0, 1);
    padSetRegionalControls(1, 1);
    CHECK(pcPadRegionalButtons(0x20) == 0x40);
    CHECK(pcPadRegionalButtons(0x40) == 0x20);
    CHECK(pcPadRegionalButtons(0x10) == 0x80);
    CHECK(pcPadRegionalButtons(0x80) == 0x10);
    CHECK(pcPadRegionalButtons(0xff0f) == 0xff0f);
    CHECK(pcPadRegionalButtons(0xffff) == 0xffff);
    CHECK(pcPadPromptSlot(0) == 1 && pcPadPromptSlot(2) == 3 && pcPadPromptSlot(4) == 4);
    if (!strcmp(argv[2],"write")) {
        CHECK(pcPadPromptGlyph(0)==PAD_GLYPH_CROSS && pcPadPromptGlyph(1)==PAD_GLYPH_CIRCLE);
        CHECK(pcPadPromptGlyph(2)==PAD_GLYPH_SQUARE && pcPadPromptGlyph(3)==PAD_GLYPH_TRIANGLE);
        CHECK(pcPadDialogSlot(0)==1 && pcPadDialogSlot(4)==4);
    }
#if VERSION_JP
    CHECK(!padJapaneseControls() && !strcmp(padConfirmButton(), "cross"));
#else
    CHECK(padJapaneseControls() && !strcmp(padConfirmButton(), "circle"));
#endif
    padSetRegionalControls(0, 0);
    padSetRegionalControls(1, 0);
    CHECK(pcPadRegionalButtons(0x10) == 0x10 && pcPadRegionalButtons(0x20) == 0x20);
    CHECK(pcPadPromptSlot(1) == 1);
    CHECK(padProfileDevice(0) == ps && padProfileDevice(1) == xbox);
    if (strncmp(argv[2], "script", 6) == 0) {
        SDL_SetJoystickVirtualButton(pj, SDL_GAMEPAD_BUTTON_SOUTH, true);
        SDL_SetJoystickVirtualButton(xj, SDL_GAMEPAD_BUTTON_SOUTH, true);
        SDL_SetJoystickVirtualAxis(pj, SDL_GAMEPAD_AXIS_LEFTX, 30000);
        SDL_UpdateJoysticks();
        uint8_t first[8] = {0}, second[8] = {0};
        InitPAD(first, sizeof(first), second, sizeof(second));
        StartPAD(); padVBlank();
        unsigned buttons = ((unsigned)(uint8_t)~first[2] << 8) | (uint8_t)~first[3];
        unsigned buttons2 = ((unsigned)(uint8_t)~second[2] << 8) | (uint8_t)~second[3];
        if (strcmp(argv[2], "script2") == 0) {
            CHECK(buttons == 0x0080 && second[0] == 0x00 && buttons2 == 0x1020);
            padSetKeyboardPort(1); /* a script's port 2 is the script's alone */
            padVBlank();
            buttons2 = ((unsigned)(uint8_t)~second[2] << 8) | (uint8_t)~second[3];
            CHECK(second[0] == 0x00 && buttons2 == 0x1020);
            padSetKeyboardPort(0);
        } else {
            CHECK(buttons == 0x0080 && second[0] == 0xff);
        }
        printf("pad scripted isolation (%s): %d checks passed\n", argv[2], checks);
        SDL_Quit(); return 0;
    }
    if (strcmp(argv[2], "write") == 0) {
        CHECK(padStickDeadzone() == 50);
        padSelectProfile(-1);
        padSetGamepadInput(6, SDL_GAMEPAD_BUTTON_NORTH); /* default Square */
        padSelectProfile(0);
        padSetGamepadInput(4, SDL_GAMEPAD_BUTTON_EAST);  /* PS Cross: Circle physical */
        padSetStickDeadzone(25);
        padSelectProfile(1);
        padSetStickDeadzone(75);
        /* Keyboard: the settings menu's hotkeys can't be bound. */
        padSetKey(4, 0, SDL_SCANCODE_F1);
        padSetKey(4, 1, SDL_SCANCODE_ESCAPE);
        CHECK(padKey(4, 0) == SDL_SCANCODE_X && padKey(4, 1) == SDL_SCANCODE_UNKNOWN);
        /* A key drives one button: Circle takes X, Cross is left with none. */
        padSetKey(5, 0, SDL_SCANCODE_X);
        CHECK(padKey(5, 0) == SDL_SCANCODE_X && padKey(5, 1) == SDL_SCANCODE_UNKNOWN);
        CHECK(padKey(4, 0) == SDL_SCANCODE_UNKNOWN && padKey(4, 1) == SDL_SCANCODE_UNKNOWN);
        CHECK(setting("key_cross", "none") && setting("key_circle", "X"));
        /* A second key with no first becomes the first. */
        padSetKey(4, 1, SDL_SCANCODE_V);
        CHECK(padKey(4, 0) == SDL_SCANCODE_V && padKey(4, 1) == SDL_SCANCODE_UNKNOWN);
        /* Select's second key, taken by Cross's second slot, leaves Select one key. */
        padSetKey(4, 1, SDL_SCANCODE_RSHIFT);
        CHECK(padKey(4, 0) == SDL_SCANCODE_V && padKey(4, 1) == SDL_SCANCODE_RSHIFT);
        CHECK(padKey(13, 0) == SDL_SCANCODE_BACKSPACE && padKey(13, 1) == SDL_SCANCODE_UNKNOWN);
        CHECK(setting("key_select", "Backspace"));
        /* The defaults back, for read and the scripts. */
        padResetKeys();
        CHECK(padKey(4, 0) == SDL_SCANCODE_X && padKey(4, 1) == SDL_SCANCODE_UNKNOWN);
        CHECK(padKey(5, 0) == SDL_SCANCODE_C && padKey(5, 1) == SDL_SCANCODE_UNKNOWN);
        CHECK(padKey(13, 0) == SDL_SCANCODE_BACKSPACE && padKey(13, 1) == SDL_SCANCODE_RSHIFT);
    }
    if (strcmp(argv[2], "read") == 0) {
        /* padResetKeys's defaults, reloaded from settings.ini */
        CHECK(setting("key_cross", "X") && setting("key_circle", "C"));
        CHECK(setting("key_select", "Backspace,Right Shift"));
        CHECK(padKey(13, 0) == SDL_SCANCODE_BACKSPACE && padKey(13, 1) == SDL_SCANCODE_RSHIFT);
        CHECK(padKey(5, 0) == SDL_SCANCODE_C);
    }
    padSelectProfile(0);
    CHECK(padGamepadInput(4) == SDL_GAMEPAD_BUTTON_EAST && padGamepadInput(5) == PAD_INPUT_NONE);
    CHECK(padGamepadInput(6) == SDL_GAMEPAD_BUTTON_NORTH && padStickDeadzone() == 25);
    CHECK(strcmp(padInputLabel(padGamepadInput(4)), "Circle") == 0);
    CHECK(strcmp(padActionLabel("cross"), "Circle") == 0);
    CHECK(padActionGlyph("cross")==PAD_GLYPH_CIRCLE);
    CHECK(pcPadDialogSlot(1)==0); /* custom physical binding */
    CHECK(pcPadDialogSlot(0)<0); /* unbound: never display an actionable Circle */
    CHECK(padKey(4, 0) == SDL_SCANCODE_X); /* device profiles don't alter the keyboard */
    padSelectProfile(1);
    CHECK(padGamepadInput(4) == SDL_GAMEPAD_BUTTON_SOUTH && padStickDeadzone() == 75);
    CHECK(strcmp(padInputLabel(padGamepadInput(4)), "A") == 0);
    CHECK(strcmp(padInputLabel(PAD_INPUT_LEFT_TRIGGER), "LT") == 0);
    CHECK(padInputGlyph(SDL_GAMEPAD_BUTTON_SOUTH)==PAD_GLYPH_A);
    CHECK(padInputGlyph(PAD_INPUT_LEFT_TRIGGER)==PAD_GLYPH_LT);
    CHECK(padInputGlyph(SDL_GAMEPAD_BUTTON_BACK)==PAD_GLYPH_VIEW);
    CHECK(padInputGlyph(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2)==PAD_GLYPH_P2);
    CHECK(padInputGlyph(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1)==PAD_GLYPH_P3);
    CHECK(padInputGlyph(SDL_GAMEPAD_BUTTON_TOUCHPAD)==PAD_GLYPH_TOUCHPAD);
    /* Every bindable SDL button has a distinct, meaningful prompt. */
    for (int input=0;input<SDL_GAMEPAD_BUTTON_COUNT;++input)
        CHECK(padInputGlyph(input)!=PAD_GLYPH_OTHER);
    notify(SDL_EVENT_GAMEPAD_BUTTON_DOWN,xbox);
    CHECK(padActionGlyph("cross")==PAD_GLYPH_A && !strcmp(padActionLabel("cross"),"A"));
    CHECK(pcPadDialogSlot(1)<0 && pcPadDialogText(1)[1]==0x80+PAD_GLYPH_A);
    /* An edit-profile selection does not change the active pad's prompts. */
    padSelectProfile(0);
    CHECK(padActionGlyph("cross")==PAD_GLYPH_A && padInputGlyph(SDL_GAMEPAD_BUTTON_EAST)==PAD_GLYPH_CIRCLE);
    SDL_Event axis={0}; axis.type=SDL_EVENT_GAMEPAD_AXIS_MOTION;
    axis.gaxis.which=ps; axis.gaxis.axis=SDL_GAMEPAD_AXIS_LEFTX; axis.gaxis.value=8192;
    padDeviceEvent(&axis); CHECK(padActionGlyph("cross")==PAD_GLYPH_A);
    axis.gaxis.value=8193; padDeviceEvent(&axis); CHECK(padActionGlyph("cross")==PAD_GLYPH_CIRCLE);
    notify(SDL_EVENT_GAMEPAD_BUTTON_DOWN,xbox);
    /* All glyphs preserve the other packed dialogue plane and row padding. */
    for (int glyph=4;glyph<PAD_GLYPH_COUNT;++glyph) for (int odd=0;odd<2;++odd) {
        unsigned short pixels[13*5]; for (int i=0;i<13*5;++i) pixels[i]=0xa55a;
        pcPadPrintGlyph(pixels,5,odd,glyph);
        for (int y=0;y<13;++y) for (int x=0;x<12;++x) {
            unsigned bit=(x%4)*4+odd*2, mask=3u<<bit;
            CHECK(((pixels[y*5+x/4]>>bit)&3)==promptIndex(glyph,x,y));
            CHECK((pixels[y*5+x/4]&~(odd?0xcccc:0x3333))==(0xa55a&~(odd?0xcccc:0x3333)));
        }
        for (int y=0;y<13;++y) CHECK(pixels[y*5+3]==0xa55a && pixels[y*5+4]==0xa55a);
    }
    SDL_SetJoystickVirtualButton(pj, SDL_GAMEPAD_BUTTON_EAST, true);
    SDL_SetJoystickVirtualButton(xj, SDL_GAMEPAD_BUTTON_SOUTH, true);
    SDL_UpdateJoysticks();
    CHECK(padGamepadHeld(ps, 4) && !padGamepadHeld(ps, 5) && padGamepadHeld(xbox, 4));
    neutral(pj); neutral(xj);
    SDL_SetJoystickVirtualAxis(pj, SDL_GAMEPAD_AXIS_LEFTX, 8192);
    SDL_UpdateJoysticks();
    CHECK(!padGamepadHeld(ps, 3));
    SDL_SetJoystickVirtualAxis(pj, SDL_GAMEPAD_AXIS_LEFTX, 8193);
    SDL_UpdateJoysticks();
    CHECK(padGamepadHeld(ps, 3));
    SDL_SetJoystickVirtualAxis(pj, SDL_GAMEPAD_AXIS_LEFTX, -8192);
    SDL_UpdateJoysticks();
    CHECK(!padGamepadHeld(ps, 2));
    SDL_SetJoystickVirtualAxis(pj, SDL_GAMEPAD_AXIS_LEFTX, -8193);
    SDL_UpdateJoysticks();
    CHECK(padGamepadHeld(ps, 2));
    SDL_SetJoystickVirtualAxis(xj, SDL_GAMEPAD_AXIS_LEFTX, 20000);
    SDL_UpdateJoysticks();
    CHECK(!padGamepadHeld(xbox, 3));
    /* The instance ID changes on reconnect; the profile key must not. */
    SDL_CloseJoystick(pj);
    CHECK(SDL_DetachVirtualJoystick(ps));
    notify(SDL_EVENT_GAMEPAD_REMOVED, ps);
    CHECK(padActionGlyph("cross")==PAD_GLYPH_A);
    SDL_JoystickID again = device("Polish PlayStation", 0xc0de, 0x0001, "ps4");
    CHECK(again != 0 && again != ps);
    notify(SDL_EVENT_GAMEPAD_ADDED, again);
    padSelectProfile(0);
    CHECK(padGamepadInput(4) == SDL_GAMEPAD_BUTTON_EAST && padStickDeadzone() == 25);
    if (strcmp(argv[2], "read") == 0) {
        padResetGamepad();
        CHECK(padGamepadInput(4) == SDL_GAMEPAD_BUTTON_SOUTH && padStickDeadzone() == 50);
        CHECK(padGamepadInput(6) == SDL_GAMEPAD_BUTTON_NORTH); /* inherits customised default */
        padSetStickDeadzone(-10); CHECK(padStickDeadzone() == 0);
        padSetStickDeadzone(120); CHECK(padStickDeadzone() == 95);
    }
    notify(SDL_EVENT_GAMEPAD_REMOVED, again);
    notify(SDL_EVENT_GAMEPAD_REMOVED, xbox);
    CHECK(padActionLabel("cross") == NULL);
    CHECK(padActionGlyph("cross")==PAD_GLYPH_NONE && pcPadPromptGlyph(1)==PAD_GLYPH_CROSS);
    CHECK(strcmp(padInputLabel(SDL_GAMEPAD_BUTTON_SOUTH), "South") == 0);
    /* Nintendo's A/B and X/Y labels follow its physical button positions. */
    SDL_JoystickID nintendo=device("Polish Nintendo",0xc0de,0x0001,"switchpro");
    CHECK(nintendo!=0); notify(SDL_EVENT_GAMEPAD_ADDED,nintendo); padSelectProfile(0);
    CHECK(padInputGlyph(SDL_GAMEPAD_BUTTON_SOUTH)==PAD_GLYPH_B);
    CHECK(padInputGlyph(SDL_GAMEPAD_BUTTON_EAST)==PAD_GLYPH_A);
    CHECK(padInputGlyph(SDL_GAMEPAD_BUTTON_WEST)==PAD_GLYPH_Y);
    CHECK(padInputGlyph(SDL_GAMEPAD_BUTTON_NORTH)==PAD_GLYPH_X);
    CHECK(padInputGlyph(PAD_INPUT_LEFT_TRIGGER)==PAD_GLYPH_ZL);
    CHECK(padInputGlyph(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)==PAD_GLYPH_L);
    CHECK(padInputGlyph(SDL_GAMEPAD_BUTTON_MISC1)==PAD_GLYPH_CAPTURE);
    notify(SDL_EVENT_GAMEPAD_REMOVED,nintendo); CHECK(SDL_DetachVirtualJoystick(nintendo));
    SDL_CloseJoystick(xj);
    {
        /* The Battle arena's player 2 on the keyboard: with no second
         * gamepad, port 2 is connected while the keyboard plays there, and
         * the keyboard (nothing held) no longer reaches port 1. */
        CHECK(SDL_DetachVirtualJoystick(xbox) && SDL_DetachVirtualJoystick(again));
        uint8_t first[8] = {0}, second[8] = {0};
        InitPAD(first, sizeof(first), second, sizeof(second));
        StartPAD();
        padVBlank(); /* takes the detachments' events */
        padVBlank();
        CHECK(padProfileDevice(0) == 0 && padProfileDevice(1) == 0 && padKeyboardPort() == 0);
        CHECK(first[0] == 0x00 && second[0] == 0xff);
        padSetKeyboardPort(1);
        padVBlank();
        CHECK(padKeyboardPort() == 1 && first[0] == 0x00 && second[0] == 0x00 && second[2] == 0xff &&
              second[3] == 0xff);
        padSetKeyboardPort(0);
        padVBlank();
        CHECK(padKeyboardPort() == 0 && second[0] == 0xff);
        InitPAD(NULL, 0, NULL, 0);
    }
    SDL_Quit();
    printf("pad profiles (%s): %d checks passed\n", argv[2], checks);
    return 0;
}
