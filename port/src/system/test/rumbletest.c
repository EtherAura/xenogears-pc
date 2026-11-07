/* Vibration (rumble.c) on virtual SDL gamepads, one with motors and one
 * without: the game's motor bytes (psx-spx's one-motor method), the modes and
 * the strength, the enhanced effects' envelopes, and every stop (the focus,
 * StopPAD/pause, a disconnected pad), plus when the game's own option is
 * offered. Settings stay in memory (XG_SETTINGS=none). */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include "system/rumble.h"
#include "system/settings.h"

static uint32_t vblank;
uint32_t hwVSyncCount(void) { return vblank; }
void rumbleGamePoll(float* large, float* small) { *large = *small = 0; } /* the game's sources: none here */

static int checks;
#define CHECK_DETAIL SDL_GetError()
#include "test/check.h"

static int calls;
static Uint16 lastLarge, lastSmall;
static bool SDLCALL motors(void* userdata, Uint16 large, Uint16 small) {
    (void)userdata;
    calls++;
    lastLarge = large;
    lastSmall = small;
    return true;
}

static SDL_Gamepad* pad(const char* name, int withMotors) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = name;
    if (withMotors) desc.Rumble = motors;
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    return id ? SDL_OpenGamepad(id) : NULL;
}

/* n vblanks with these actuator buffers */
static void run(SDL_Gamepad* pads[2], const unsigned char* act[2], int scripted, int n) {
    while (n-- > 0) {
        vblank++;
        rumbleVBlank(pads, act, scripted);
    }
}

static int near(Uint16 v, double level) {
    double d = v / 65535.0 - level;
    return d > -0.04 && d < 0.04;
}

int main(void) {
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0"); /* only the virtual pads (and no slow probing of real ones) */
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Gamepad* withMotors = pad("rumble test pad", 1);
    SDL_Gamepad* without = pad("plain test pad", 0);
    CHECK(withMotors != NULL && without != NULL);
    CHECK(rumblePadHasMotors(withMotors) && !rumblePadHasMotors(without));

    SDL_Gamepad* pads[2] = {withMotors, without};
    unsigned char on[4] = {1, 0x40, 1, 0}, off[4] = {1, 0x40, 0, 0}, idle[4] = {0, 0x40, 1, 0}, notOld[4] = {1, 0x01, 1, 0};
    const unsigned char* act[2] = {NULL, on};

    /* the default: the game's motor, the small one, full */
    CHECK(rumbleMode() == RUMBLE_GAME && rumbleStrength() == 100);
    act[0] = on;
    run(pads, act, 0, 1);
    CHECK(calls == 1 && lastLarge == 0 && lastSmall == 65535);
    run(pads, act, 0, 3);
    CHECK(calls == 1); /* the same level is not sent again within the refresh */
    act[0] = off;
    run(pads, act, 0, 1);
    CHECK(calls == 2 && lastLarge == 0 && lastSmall == 0);
    act[0] = idle;
    run(pads, act, 0, 1);
    CHECK(calls == 2);
    act[0] = notOld; /* the 4th byte must be 40h..7Fh */
    run(pads, act, 0, 1);
    CHECK(calls == 2);

    /* strength */
    rumbleSetStrength(50);
    act[0] = on;
    run(pads, act, 0, 1);
    CHECK(calls == 3 && near(lastSmall, 0.5) && lastLarge == 0);
    rumbleSetStrength(100);
    run(pads, act, 0, 1);
    CHECK(calls == 4 && lastSmall == 65535);

    /* off: stops, and the game's motor does nothing */
    rumbleSetMode(RUMBLE_OFF);
    run(pads, act, 0, 1);
    CHECK(calls == 5 && lastSmall == 0);
    run(pads, act, 0, 5);
    CHECK(calls == 5);
    CHECK(!rumbleOffered(0));

    /* the game's option: offered for a pad with motors, never to a script */
    rumbleSetMode(RUMBLE_GAME);
    run(pads, act, 0, 1);
    CHECK(rumbleOffered(0) && !rumbleOffered(1));
    run(pads, act, 1, 1);
    CHECK(!rumbleOffered(0));

    /* enhanced: the game's motor on both motors, and an effect's envelope */
    rumbleSetMode(RUMBLE_ENHANCED);
    run(pads, act, 0, 1);
    CHECK(near(lastLarge, 0.5) && near(lastSmall, 0.9));
    act[0] = NULL;
    run(pads, act, 0, 1);
    CHECK(lastLarge == 0 && lastSmall == 0);
    rumbleAdd(0.8f, 0.4f, 0, 2, 4, "test hit");
    run(pads, act, 0, 1);
    CHECK(near(lastLarge, 0.8) && near(lastSmall, 0.4));
    run(pads, act, 0, 2);
    CHECK(lastLarge < 0.8 * 65535 && lastLarge > 0); /* decaying */
    run(pads, act, 0, 4);
    CHECK(lastLarge == 0 && lastSmall == 0); /* 2 + 4 vblanks after its first */
    rumbleAdd(0.6f, 0.6f, 4, 0, 0, "test ramp");
    run(pads, act, 0, 1);
    CHECK(near(lastLarge, 0.6 / 5) && near(lastSmall, 0.6 / 5));
    run(pads, act, 0, 3);
    CHECK(near(lastLarge, 0.6 * 4 / 5));
    run(pads, act, 0, 1);
    CHECK(lastLarge == 0);

    /* the focus: stops at once, nothing while away, back after */
    rumbleAdd(0.7f, 0.7f, 0, 30, 0, "test long");
    run(pads, act, 0, 1);
    CHECK(near(lastLarge, 0.7));
    SDL_Event e = {0};
    e.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    rumbleEvent(&e);
    CHECK(lastLarge == 0 && lastSmall == 0);
    int before = calls;
    rumbleAdd(0.7f, 0.7f, 0, 30, 0, "test unfocused");
    run(pads, act, 0, 5);
    CHECK(calls == before);
    e.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    rumbleEvent(&e);
    run(pads, act, 0, 1);
    CHECK(near(lastLarge, 0.7));

    /* pause/StopPAD/quit: stops at once and forgets the effects */
    rumbleStopAll();
    CHECK(lastLarge == 0 && lastSmall == 0);
    before = calls;
    run(pads, act, 0, 3);
    CHECK(calls == before);

    /* a pad disconnected: nothing is sent to it any more */
    act[0] = on;
    run(pads, act, 0, 1);
    CHECK(lastSmall != 0);
    rumbleForget(withMotors);
    rumbleStopAll();
    before = calls;
    run((SDL_Gamepad*[2]){NULL, without}, act, 0, 3);
    CHECK(calls == before);

    /* the settings menu's test pulse */
    CHECK(rumbleTest(withMotors) == 1 && lastLarge != 0);
    CHECK(rumbleTest(without) == 0);
    rumbleSetMode(RUMBLE_OFF);
    CHECK(rumbleTest(withMotors) == 0);

    SDL_CloseGamepad(withMotors);
    SDL_CloseGamepad(without);
    SDL_Quit();
    printf("rumbletest: %d checks, 0 failed\n", checks);
    return 0;
}
