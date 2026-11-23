/*
 * Battle speed (xg-mrj.2; battlespeed.h): character and Gear battles at 1x,
 * 2x or 4x.
 *
 * The game's own frame stays the unit: at 2x or 4x it runs 2 or 4 vblanks of
 * its time per picture shown (hw.c hwSetGameSpeed), each vblank as on the PS1
 * (its pad input read, its callbacks run, its CD sectors and other interrupts
 * at their times), and every frame drawn into VRAM; only the picture of every
 * second or fourth vblank is presented (vk.c, battleSpeedHidden), so the game
 * thread never waits for the display in between. The audio clock keeps real
 * time: the sound driver's 240 Hz tick and the SPU's samples follow it, so the
 * music keeps its tempo and pitch, and a sound effect plays at its own pitch
 * and length from the moment the game starts it (more of them per second, as
 * the battle goes faster). On the stepped clock (XG_CLOCK=step) the game's
 * timeline is the same at every speed; only the audio clock's share of it
 * changes.
 *
 * When it applies: while battle frames come (battleRender calls
 * battleSpeedBattleFrame each frame, port/src/game/battle/speed.c), so the
 * field's transition into a battle, the battle's loading before its first
 * frame and the way back run at 1x, and Battling (the arena, a fighting game
 * of its own) is never touched. Inside a battle it runs at 1x while the
 * battle waits for the player (menus, target arrows, a page waiting for the
 * confirm button, event dialogue: the D-pad's auto-repeat, the cursors and
 * the menus' animation are per frame) and for the first TEXT_HOLD frames of
 * each new name the battle puts up (the attack or deathblow banner, the
 * acting enemy's name; they stay up for the rest of the turn, which then goes
 * at speed). Also at 1x: online (netplay paces both sides in real time), the
 * slowdown replay (its frame boundaries are the log's) and CD audio
 * streaming (XA arrives with the CD's sectors, on the game's clock, and would
 * overrun the SPU's input queue).
 *
 * Its decisions are part of save states (battleSpeedStateIo): a state taken
 * at 4x loads at 4x with the audio clock where it was (hw.c saves the clock).
 *
 * Story battles (xg-mrj.7): only a battle the encounter roll started runs at
 * the saved speed. A scripted one (gameplay.c's class: bosses, story fights,
 * battles a script starts, chains) starts at 1x with a note, and the control
 * steps a speed of its own that ends with it and is never saved (`story`,
 * below). XG_BATTLE_SPEED, the environment's choice for one run, holds every
 * battle at its speed as before (tools/test_battle_speed.py, statecheck.py).
 *
 * XG_BATTLE_SPEED_LOG=<file>: a line per vblank in a battle and at each
 * change of the factor, "<vblank>
 * <factor> <hidden> <wait> <text> <textHold> <game time> <audio time>"
 * (tools/test_battle_speed.py).
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battlespeed.h"
#include "gameplay.h"
#include "netplay.h"
#include "settings.h"
#include "spu/spu.h"
#include "system.h"

void presentShowNote(const char* text) __attribute__((weak)); /* gpu/present.c */
/* fieldspeed.c: the field's and the world map's factor, and its part of a
 * state (weak: battlespeedtest links this file without it). */
extern int fieldSpeedChosen(void) __attribute__((weak));
extern int fieldSpeedWanted(void) __attribute__((weak));
extern int fieldSpeedOnScreen(void) __attribute__((weak));
extern int fieldSpeedStep(void) __attribute__((weak));
extern void fieldSpeedStateIo(XgState* s) __attribute__((weak));
/* gameplay.c: the class of the battle that started last (weak: fieldspeedtest
 * links this file without it, and every battle is then the saved speed's). */
extern int pcGameplayBattleClass(void) __attribute__((weak));

/* Battle frames at most this many vblanks apart are one battle: battleRender
 * waits 1 vblank a frame; its loads (battleLoadFileFrom0xC) take a few more. */
#define BATTLE_GAP 8
/* CD audio seen this many vblanks ago still holds 1x (XA arrives in bursts). */
#define CD_AUDIO_HOLD 120
/* A new name on the screen: its first battle frames at 1x (0.75 s). */
#define TEXT_HOLD 45

/* ---- the setting (host) ---- */

static int cachedChosen, cachedValid;
static unsigned cachedRevision;

/* A scripted battle's own speed (host: a choice of the player's like the
 * setting, never in a state and never saved). It lives from one battle start
 * to the next (battleSpeedBattleStart, a retry's too) and a state's load ends
 * it (battleSpeedStateIo): the battle loaded may be another one. Read and
 * written on the game's thread only, as the rest of this file. */
static struct {
    int speed;  /* 2 or 4 once the control stepped it there; else 1x */
    int noted;  /* the battle's corner note was asked for */
    int exempt; /* XG_BATTLE_SPEED holds every battle at its speed (read with cachedChosen) */
} story;

static int clampChosen(int v) {
    return v <= 0 ? 0 : v == 1 ? 1 : v <= 3 ? 2 : 4;
}

static void describe(void) {
    settingsDescribe("battle_speed", "battle_speed: 0 off (default); 1, 2 or 4: battles at that speed (music at its tempo), "
                                     "stepped in a battle with battle_speed_key or the gamepad's Select+R2");
    settingsDescribe("battle_speed_key", "battle_speed_key: the in-battle battle speed control on the keyboard (default Tab; "
                                         "Shift+, Ctrl+, Alt+ or none)");
    settingsDescribe("battle_speed_button", "battle_speed_button: the in-battle battle speed control on the pad: a button or a "
                                            "chord of them joined by + (l1, r1, l2, r2, select; battles never read them), or "
                                            "none (default select+r2)");
}

int battleSpeedChosen(void) {
    if (!cachedValid || cachedRevision != settingsRevision()) {
        cachedChosen = clampChosen(settingsInt("battle_speed", 0));
        cachedRevision = settingsRevision(); /* after the read: the first one loads the file */
        cachedValid = 1;
        const char* env = settingsOverride("battle_speed"); /* (until the key is set in the run) */
        story.exempt = env != NULL && env[0] != '\0';
    }
    return cachedChosen;
}

/* The battle that started last runs at a speed of its own: the option is on,
 * the encounter roll did not start it, and the environment does not choose. */
static int storyBattle(void) {
    return battleSpeedChosen() > 0 && !story.exempt && pcGameplayBattleClass != NULL &&
           pcGameplayBattleClass() != GAMEPLAY_BATTLE_RANDOM;
}

static void storyNote(void) {
    if (presentShowNote) {
        char text[48];
        snprintf(text, sizeof text, "Battle speed %dx (story battle)", story.speed > 1 ? story.speed : 1);
        presentShowNote(text);
    }
}

void battleSpeedBattleStart(void) {
    story.speed = 1;
    story.noted = 0;
}

void battleSpeedSetChosen(int speed) {
    describe();
    settingsSetInt("battle_speed", clampChosen(speed));
    settingsSave();
    cachedValid = 0;
}

/* ---- the game's side (state) ---- */

static uint32_t lastBattleFrame; /* hwVSyncCount() at the latest battle frame */
static int seenBattle;           /* a battle frame came since the start (lastBattleFrame is one) */
static int battleWait;           /* the latest battle frame: the battle waits for the player */
static unsigned battleText;      /* ... and the text it shows (speed.c's bits) */
static int textHold;             /* battle frames still at 1x for a new text */
static uint32_t lastCdAudio;     /* hwVSyncCount() when CD audio was last queued */
static int seenCdAudio;
static int sinceShown;           /* vblanks since the last picture shown */
static int hidden;               /* this vblank's picture is withheld */

/* XG_BATTLE_SPEED_LOG (host). */
static FILE* logFile;
static int logChecked;
static int loggedFactor = 1;

void battleSpeedBattleFrame(int wait, unsigned text) {
    if (hwInWorld()) {
        return; /* the extras' battle scenes (a world of their own): not the game's battle */
    }
    if (text & ~battleText) {
        textHold = TEXT_HOLD; /* a name came up */
    } else if (textHold > 0) {
        textHold--;
    }
    lastBattleFrame = hwVSyncCount();
    seenBattle = 1;
    battleWait = wait != 0;
    battleText = text;
    if (!story.noted && storyBattle() && !netplayActive()) {
        story.noted = 1; /* a scripted battle's first frame: 1x, whatever the saved speed */
        storyNote();
    }
}

int battleSpeedInBattle(void) {
    return seenBattle && (uint32_t)(hwVSyncCount() - lastBattleFrame) <= BATTLE_GAP;
}

int battleSpeedFactor(void) {
    return hwGameSpeed();
}

int battleSpeedHidden(void) {
    return hidden;
}

/* The factor the next frame runs at: a battle's by battle_speed, and outside
 * battles the field's and the world map's by field_speed (fieldspeed.c, 1
 * when the screen is neither). Neither setting reaches the other's frames.
 * With both off or at 1x nothing here is touched; with either on, CD audio
 * is watched on every screen, so its hold is known when a fast one comes. */
static int wanted(void) {
    int chosen = battleSpeedChosen();
    int fieldOn = fieldSpeedChosen != NULL && fieldSpeedChosen() > 1;
    if (chosen > 0 && battleSpeedInBattle() && storyBattle()) {
        chosen = story.speed > 1 ? story.speed : 1; /* a scripted battle: its own speed, not the saved one */
    }
    if (chosen <= 1 && !fieldOn) {
        return 1;
    }
    uint32_t now = hwVSyncCount();
    if (spuCdQueued() > 0) {
        lastCdAudio = now;
        seenCdAudio = 1;
    }
    int factor;
    if (battleSpeedInBattle()) {
        factor = battleWait || textHold > 0 ? 1 : chosen;
    } else {
        factor = fieldOn ? fieldSpeedWanted() : 1;
    }
    if (factor <= 1 || netplayActive()) {
        return 1;
    }
    if (seenCdAudio && (uint32_t)(now - lastCdAudio) <= CD_AUDIO_HOLD) {
        return 1;
    }
    int64_t replay;
    if (slowdownLogVBlank(&replay)) {
        return 1;
    }
    return factor;
}

void battleSpeedVBlank(void) {
    hwSetGameSpeed(wanted());
    int factor = hwGameSpeed();
    if (factor <= 1) {
        hidden = 0;
        sinceShown = 0;
    } else {
        hidden = ++sinceShown < factor;
        if (!hidden) {
            sinceShown = 0;
        }
    }
    if (!logChecked) {
        const char* path = getenv("XG_BATTLE_SPEED_LOG");
        logChecked = 1;
        logFile = path != NULL && path[0] != '\0' ? fopen(path, "w") : NULL;
    }
    if (logFile != NULL && (battleSpeedInBattle() || factor != loggedFactor)) {
        loggedFactor = factor; /* (and the vblank the factor goes back to 1 after a battle) */
        double now = hwNow();
        fprintf(logFile, "%u %d %d %d %x %d %.9f %.9f\n", hwVSyncCount(), factor, hidden, battleWait, battleText, textHold,
                now, hwAudioTime(now));
    }
}

/* ---- the control (host) ---- */

int battleSpeedStep(void) {
    int chosen = battleSpeedChosen();
    if (chosen == 0 || !battleSpeedInBattle()) {
        return 0;
    }
    if (netplayActive()) {
        if (presentShowNote) presentShowNote("Battle speed: 1x online");
        return 1;
    }
    if (storyBattle()) {
        /* A scripted battle: this battle's speed alone; the setting and its file are not touched. */
        int now = story.speed > 1 ? story.speed : 1;
        story.speed = now == 1 ? 2 : now == 2 ? 4 : 1;
        story.noted = 1;
        storyNote();
        return 1;
    }
    int next = chosen == 1 ? 2 : chosen == 2 ? 4 : 1;
    battleSpeedSetChosen(next);
    story.speed = next; /* (XG_BATTLE_SPEED's hold ends with this write: a scripted battle goes on from here) */
    story.noted = 1;
    if (presentShowNote) {
        char text[48];
        snprintf(text, sizeof text, "Battle speed %dx", next);
        presentShowNote(text);
    }
    return 1;
}

enum { MOD_SHIFT = 1, MOD_CTRL = 2, MOD_ALT = 4 };
typedef struct {
    SDL_Scancode key;
    int mods;
    unsigned button; /* the pad bits of the chord (pad.c's active-high order), 0 for none */
} SpeedControl;
static struct {
    unsigned revision;
    int valid;
    SpeedControl battle; /* battle_speed_key, battle_speed_button: in a battle */
    SpeedControl field;  /* field_speed_key, field_speed_button: on the field and the world map (fieldspeed.c) */
} control;

/* "Tab", "Shift+Tab", "none" (as port/src/ui/diagnostics.cpp's hotkeys). */
static void parseKey(SpeedControl* c, const char* s) {
    char buf[64];
    SDL_strlcpy(buf, s != NULL && s[0] != '\0' ? s : "Tab", sizeof buf);
    char* name = buf;
    c->mods = 0;
    for (;;) {
        char* plus = strchr(name, '+');
        if (plus == NULL || plus[1] == '\0') break;
        *plus = '\0';
        if (!SDL_strcasecmp(name, "shift")) c->mods |= MOD_SHIFT;
        else if (!SDL_strcasecmp(name, "ctrl") || !SDL_strcasecmp(name, "control")) c->mods |= MOD_CTRL;
        else if (!SDL_strcasecmp(name, "alt")) c->mods |= MOD_ALT;
        name = plus + 1;
    }
    c->key = SDL_strcasecmp(name, "none") == 0 ? SDL_SCANCODE_UNKNOWN : SDL_GetScancodeFromName(name);
    if (c->key == SDL_SCANCODE_UNKNOWN && SDL_strcasecmp(name, "none") != 0) {
        c->key = SDL_SCANCODE_TAB; /* not a key name: the default */
        c->mods = 0;
    }
}

/* The pad's control: a button or a chord ("select+r2", either order) of the
 * shoulders and Select, which character and Gear battles never read
 * (battleHandleInput, b_80089B50.c: only the debug kit's), or none. Not a
 * known name: the default chord. On the field and the world map Select is
 * the game's too (the area map, the minimap): there selectchord.c keeps a
 * chord of it from the game. */
#define DEFAULT_CHORD 0x0102u /* Select + R2 */
#define PAD_SELECT 0x0100u
static unsigned parseButton(const char* s) {
    static const struct {
        const char* name;
        unsigned bit;
    } names[] = {{"l2", 0x0001}, {"r2", 0x0002}, {"l1", 0x0004}, {"r1", 0x0008}, {"select", PAD_SELECT}};
    char buf[64];
    if (s == NULL || s[0] == '\0') return DEFAULT_CHORD;
    if (!SDL_strcasecmp(s, "none")) return 0;
    SDL_strlcpy(buf, s, sizeof buf);
    unsigned chord = 0;
    for (char* name = buf; name != NULL;) {
        char* plus = strchr(name, '+');
        if (plus != NULL) *plus = '\0';
        while (*name == ' ') name++;
        for (char* end = name + strlen(name); end > name && end[-1] == ' ';) *--end = '\0';
        size_t i = 0;
        for (; i < sizeof names / sizeof names[0] && SDL_strcasecmp(name, names[i].name) != 0; i++) {
        }
        if (i == sizeof names / sizeof names[0]) return DEFAULT_CHORD;
        chord |= names[i].bit;
        name = plus != NULL ? plus + 1 : NULL;
    }
    return chord;
}

static void loadControl(void) {
    if (!control.valid || control.revision != settingsRevision()) {
        describe();
        settingsDescribe("field_speed_key", "field_speed_key: the field speed control on the keyboard, on the field and the "
                                            "world map (default Tab; Shift+, Ctrl+, Alt+ or none)");
        settingsDescribe("field_speed_button", "field_speed_button: the field speed control on the pad, as battle_speed_button "
                                               "(default select+r2; there the game does not see a Select pressed with a "
                                               "shoulder button)");
        parseKey(&control.battle, settingsGet("battle_speed_key"));
        control.battle.button = parseButton(settingsGet("battle_speed_button"));
        parseKey(&control.field, settingsGet("field_speed_key"));
        control.field.button = parseButton(settingsGet("field_speed_button"));
        control.revision = settingsRevision();
        control.valid = 1;
    }
}

static int keyIs(const SpeedControl* c, int scancode, int mods) {
    return c->key != SDL_SCANCODE_UNKNOWN && scancode == (int)c->key && mods == c->mods;
}

int battleSpeedKey(int scancode, int mod) {
    loadControl();
    int mods = (mod & SDL_KMOD_SHIFT ? MOD_SHIFT : 0) | (mod & SDL_KMOD_CTRL ? MOD_CTRL : 0) |
               (mod & SDL_KMOD_ALT ? MOD_ALT : 0);
    if (keyIs(&control.battle, scancode, mods) && battleSpeedStep()) {
        return 1;
    }
    return fieldSpeedStep != NULL && keyIs(&control.field, scancode, mods) && fieldSpeedStep();
}

/* pad.c, each vblank: port 1's buttons (live or scripted, before the game's
 * own remapping); a control acts when its chord completes: every button of
 * it held, one of them down this vblank (in either order, or together).
 * Battles do nothing with the buttons; on the field and the world map
 * selectchord.c keeps a live Select chord from the game. */
static unsigned lastButtons;
static int chordDown(unsigned chord, unsigned buttons, unsigned down) {
    /* (not inside the soft reset's L1+R1+Select+Start) */
    return chord != 0 && (buttons & chord) == chord && (down & chord) && (buttons & 0x0900) != 0x0900;
}
void battleSpeedPad(unsigned buttons) {
    loadControl();
    unsigned down = buttons & ~lastButtons;
    lastButtons = buttons;
    if (chordDown(control.battle.button, buttons, down) && battleSpeedStep()) {
        return;
    }
    if (fieldSpeedStep != NULL && chordDown(control.field.button, buttons, down)) {
        fieldSpeedStep();
    }
}

int speedControlHoldsSelect(void) {
    if (fieldSpeedChosen == NULL || fieldSpeedOnScreen == NULL) {
        return 0;
    }
    loadControl();
    return (control.field.button & PAD_SELECT) && fieldSpeedChosen() > 0 && fieldSpeedOnScreen() && !netplayActive();
}

/* ---- save states (savestate.h) ---- */

void battleSpeedStateIo(XgState* s) {
    XG_STATE_VAR(s, lastBattleFrame);
    XG_STATE_VAR(s, seenBattle);
    XG_STATE_VAR(s, battleWait);
    XG_STATE_VAR(s, battleText);
    XG_STATE_VAR(s, textHold);
    XG_STATE_VAR(s, lastCdAudio);
    XG_STATE_VAR(s, seenCdAudio);
    XG_STATE_VAR(s, sinceShown);
    XG_STATE_VAR(s, hidden);
    if (xgStateLoading != NULL && xgStateLoading(s)) {
        /* A scripted battle's own speed is not in a state: the battle loaded
         * may be another one, so it is at 1x again (a note when that slows it). */
        if (story.speed > 1) story.noted = 0;
        story.speed = 1;
    }
    if (fieldSpeedStateIo != NULL) {
        fieldSpeedStateIo(s); /* the field's movie flag (fieldspeed.c) */
    }
}
