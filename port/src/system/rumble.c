/*
 * Controller vibration (rumble.h), settings.ini:
 *
 *   vibration=game      (default) the game's own vibration, as a DualShock
 *                       gets it: Xenogears drives its pad's motor only in the
 *                       gear battling arena (startPadVibration, src/battling:
 *                       hits, knock-downs, landings), and only with the
 *                       arena's own Vibration option on (rumble_game.c offers
 *                       that option on a pad with motors; US default off, JP
 *                       on). Nothing else in the retail game vibrates: the
 *                       battle's stopBattleVibration is empty.
 *   vibration=enhanced  that, and the port's feedback (rumble_game.c): hits
 *                       taken and dealt, knock-outs, gear impacts, deathblows,
 *                       and the battle's and fields' camera shakes
 *                       (earthquakes, explosions)
 *   vibration=off
 *   vibration_strength=100   0..100 (%), both modes
 *
 * The game's motor: it hands SendPAD a 4-byte buffer per port (src/main/pad.c
 * PadActuator), sent with each pad read while byte 0 is set; bytes 1 and 2
 * are the read command's 4th and 5th bytes, which switch a pad's motor on
 * without configuration commands when the first is 40h..7Fh and the second
 * has bit 0 set (psx-spx, Controllers - Vibration/Rumble Control, "old
 * method": one motor, digital; on the two-motor pads the right/small one).
 * Here: SDL's high frequency (small) motor, full on at the strength.
 *
 * Everything runs on the vblank clock in the vblank handler (pad.c), so the
 * effects keep the game's pace; each level is sent for 250 ms and re-sent
 * while it lasts, so a pad stops by itself when vblanks stop. It stops at
 * once when the settings menu or the extras open, the system pauses the
 * game, the window loses the focus (and stays off until it is back), the pad
 * is disconnected, StopPAD, and quit. Nothing here writes the game's state.
 *
 * XG_RUMBLE_LOG=1 logs each level sent ("rumble: v<vblank> pad <n> large
 * <0..1> small <0..1>") and each enhanced effect.
 */

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <string.h>

#include "rumble.h"
#include "settings.h"

uint32_t hwVSyncCount(void);

#define EFFECTS 12
#define SEND_MS 250
#define REFRESH_NS 100000000ull
#define STEPS 32 /* a level is sent in 32nds: fewer reports while one decays */

typedef struct {
    float large, small;
    int attack, hold, decay, age;
} Effect;

static struct {
    Effect fx[EFFECTS];
    int count;
    SDL_Gamepad* pads[2];
    int scripted;
    int unfocused;
    Uint16 sentLarge[2], sentSmall[2];
    Uint64 sentAt[2];
} r;

static const char* const modeNames[] = {"off", "game", "enhanced"};

void rumbleLog(const char* fmt, ...) {
    static int on = -1;
    if (on < 0) {
        on = SDL_getenv("XG_RUMBLE_LOG") != NULL;
    }
    if (!on) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, fmt, ap);
    va_end(ap);
}

const char* rumbleModeName(int mode) {
    return mode >= 0 && mode <= RUMBLE_ENHANCED ? modeNames[mode] : "game";
}

int rumbleMode(void) {
    settingsDescribe("vibration", "vibration: game (default): the game's own controller vibration (only the gear "
                                  "battling arena, with its Vibration option on); enhanced: also the port's for hits, "
                                  "deathblows, gear impacts and camera shakes; off");
    const char* v = settingsGet("vibration");
    if (v == NULL) {
        return RUMBLE_GAME;
    }
    if (SDL_strcasecmp(v, "off") == 0 || SDL_strcmp(v, "0") == 0) {
        return RUMBLE_OFF;
    }
    if (SDL_strcasecmp(v, "enhanced") == 0 || SDL_strcmp(v, "2") == 0) {
        return RUMBLE_ENHANCED;
    }
    return RUMBLE_GAME;
}

void rumbleSetMode(int mode) {
    settingsSet("vibration", rumbleModeName(mode));
    if (mode != RUMBLE_ENHANCED) {
        r.count = 0;
    }
}

int rumbleStrength(void) {
    settingsDescribe("vibration_strength", "vibration_strength: 0..100 (%), default 100");
    int v = settingsInt("vibration_strength", 100);
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

void rumbleSetStrength(int percent) {
    settingsSetInt("vibration_strength", percent < 0 ? 0 : percent > 100 ? 100 : percent);
}

static int hasMotors(SDL_Gamepad* pad) {
    return pad != NULL && SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
}

int rumbleSupported(int port) {
    return port >= 0 && port < 2 && hasMotors(r.pads[port]);
}

int rumbleOffered(int port) {
    return !r.scripted && rumbleMode() != RUMBLE_OFF && rumbleSupported(port);
}

void rumbleAdd(float large, float small, int attack, int hold, int decay, const char* what) {
    if (rumbleMode() != RUMBLE_ENHANCED) {
        return;
    }
    Effect e = {large, small, attack, hold, decay, 0};
    if (r.count < EFFECTS) {
        r.fx[r.count++] = e;
    } else {
        int weakest = 0;
        for (int i = 1; i < EFFECTS; i++) {
            if (r.fx[i].large + r.fx[i].small < r.fx[weakest].large + r.fx[weakest].small) {
                weakest = i;
            }
        }
        r.fx[weakest] = e;
    }
    rumbleLog("rumble: v%u %s: large %.2f small %.2f (%d+%d+%d vblanks)", hwVSyncCount(), what, large, small, attack,
              hold, decay);
}

static float level(const Effect* e) {
    int t = e->age;
    if (t < e->attack) {
        return (float)(t + 1) / (float)(e->attack + 1);
    }
    t -= e->attack;
    if (t < e->hold) {
        return 1.0f;
    }
    t -= e->hold;
    return 1.0f - (float)(t + 1) / (float)(e->decay + 1);
}

static Uint16 toMotor(float x) {
    int q = (int)(x * STEPS + 0.5f);
    q = q < 0 ? 0 : q > STEPS ? STEPS : q;
    return (Uint16)(q * 65535 / STEPS);
}

static void send(int port, float large, float small) {
    SDL_Gamepad* pad = r.pads[port];
    Uint16 l = toMotor(large), s = toMotor(small);
    if (pad == NULL) {
        r.sentLarge[port] = r.sentSmall[port] = 0;
        return;
    }
    Uint64 now = SDL_GetTicksNS();
    if (l == 0 && s == 0) {
        if (r.sentLarge[port] != 0 || r.sentSmall[port] != 0) {
            SDL_RumbleGamepad(pad, 0, 0, 0);
            rumbleLog("rumble: v%u pad %d large 0 small 0", hwVSyncCount(), port + 1);
        }
        r.sentLarge[port] = r.sentSmall[port] = 0;
        return;
    }
    if (l != r.sentLarge[port] || s != r.sentSmall[port] || now - r.sentAt[port] > REFRESH_NS) {
        if (l != r.sentLarge[port] || s != r.sentSmall[port]) {
            rumbleLog("rumble: v%u pad %d large %.2f small %.2f", hwVSyncCount(), port + 1, l / 65535.0, s / 65535.0);
        }
        SDL_RumbleGamepad(pad, l, s, SEND_MS);
        r.sentLarge[port] = l;
        r.sentSmall[port] = s;
        r.sentAt[port] = now;
    }
}

/* The game's motor on (see the header). */
static int motorOn(const unsigned char* a) {
    return a != NULL && a[0] != 0 && (a[1] & 0xC0) == 0x40 && (a[2] & 1) != 0;
}

void rumbleVBlank(SDL_Gamepad* pads[2], const unsigned char* actuators[2], int scripted) {
    r.pads[0] = pads[0];
    r.pads[1] = pads[1];
    r.scripted = scripted;
    int mode = rumbleMode();
    float k = (float)rumbleStrength() / 100.0f;
    float shakeLarge = 0, shakeSmall = 0;
    if (mode == RUMBLE_ENHANCED) {
        rumbleGamePoll(&shakeLarge, &shakeSmall);
    } else {
        r.count = 0;
    }
    for (int port = 0; port < 2; port++) {
        float large = 0, small = 0;
        int on = motorOn(actuators[port]);
        if (mode == RUMBLE_GAME && on) {
            small = 1.0f;
        } else if (mode == RUMBLE_ENHANCED) {
            if (on) {
                large = 0.5f;
                small = 0.9f;
            }
            if (port == 0) {
                large = SDL_max(large, shakeLarge);
                small = SDL_max(small, shakeSmall);
                for (int i = 0; i < r.count; i++) {
                    float v = level(&r.fx[i]);
                    large = SDL_max(large, r.fx[i].large * v);
                    small = SDL_max(small, r.fx[i].small * v);
                }
            }
        }
        if (r.unfocused) {
            large = small = 0;
        }
        send(port, large * k, small * k);
    }
    for (int i = 0; i < r.count;) {
        if (++r.fx[i].age >= r.fx[i].attack + r.fx[i].hold + r.fx[i].decay) {
            r.fx[i] = r.fx[--r.count];
        } else {
            i++;
        }
    }
}

void rumbleStopAll(void) {
    for (int port = 0; port < 2; port++) {
        if (r.pads[port] != NULL && (r.sentLarge[port] != 0 || r.sentSmall[port] != 0)) {
            SDL_RumbleGamepad(r.pads[port], 0, 0, 0);
            rumbleLog("rumble: v%u pad %d stopped", hwVSyncCount(), port + 1);
        }
        r.sentLarge[port] = r.sentSmall[port] = 0;
    }
    r.count = 0;
}

void rumbleForget(SDL_Gamepad* pad) {
    for (int port = 0; port < 2; port++) {
        if (r.pads[port] == pad) {
            r.pads[port] = NULL;
            r.sentLarge[port] = r.sentSmall[port] = 0;
        }
    }
}

void rumbleEvent(const SDL_Event* e) {
    if (e->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        rumbleStopAll();
        r.unfocused = 1;
    } else if (e->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        r.unfocused = 0;
    }
}

int rumblePadHasMotors(SDL_Gamepad* pad) {
    return hasMotors(pad);
}

int rumbleTest(SDL_Gamepad* pad) {
    int mode = rumbleMode();
    float k = (float)rumbleStrength() / 100.0f;
    if (mode == RUMBLE_OFF || !hasMotors(pad)) {
        return 0;
    }
    /* the game's motor alone, or the enhanced mode's hit taken at its heaviest */
    Uint16 large = mode == RUMBLE_ENHANCED ? toMotor(0.8f * k) : 0;
    Uint16 small = toMotor((mode == RUMBLE_ENHANCED ? 0.45f : 1.0f) * k);
    SDL_RumbleGamepad(pad, large, small, 300);
    rumbleLog("rumble: test large %.2f small %.2f", large / 65535.0, small / 65535.0);
    return 1;
}
