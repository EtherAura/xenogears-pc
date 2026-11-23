/* Field speed (xg-mrj.6): the controller (fieldspeed.c), battlespeed.c's
 * vblank that picks the factor and hw.c's stepped clock under it, linked
 * whole, with the field's own movie calls as the game makes them
 * (port/src/game/field/wide.c pcFieldMovieBlock and pcFieldWideExit, the
 * production file). The screen is the wide picture's kind (include/pc_wide.h),
 * kept here as gpu/wide.c keeps it. No disc, GPU or wall clock.
 *
 *   - off and 1x: nothing changes (the factor, the audio clock's anchor, the
 *     pictures shown, battlespeed.c's state);
 *   - 2x and 4x on the field and the world map; 1x in their menus, the movie
 *     mode, a battle's loading and every other kind, from the first vblank;
 *   - battles run by battle_speed alone, the field by field_speed alone;
 *   - a field movie is at 1x from pcFieldMovieBlock(1) to pcFieldMovieBlock(0)
 *     with no CD audio queued, whatever the wide picture does with it; the
 *     flag ends with the field mode and travels in a state;
 *   - online, the slowdown replay and CD audio hold 1x as in battles;
 *   - the control: Tab and Select+R2 step field_speed on the field and the
 *     world map with it on, battle_speed's in a battle, nothing elsewhere;
 *   - Select as the port's chord button there (selectchord.c, linked
 *     whole): a chord with a shoulder button is kept from the game, a lone
 *     Select reaches it as it is let go, and with the option off, on other
 *     screens, online or with Start held the buttons go through as they are.
 *
 * With arguments it is the setting in a process of its own
 * (tools/test_field_speed.py: XG_SETTINGS names the file):
 *   --show            "field_speed=<n> battle_speed=<n>"
 *   --set-field N     fieldSpeedSetChosen(N), then --show
 *   --set-battle N    battleSpeedSetChosen(N), then --show
 */
#include <math.h>

#include "../hw.c"
#include "../battlespeed.c"
#include "../selectchord.c"
#include "../fieldspeed.h"
#include "common.h"
#include "field/field.h"
#include "pc_wide.h"

static int checks;
static const char* section = "start";
#define CHECK_CONTEXT fprintf(stderr, "[%s] ", section)
#include "test/check.h"

/* hw.c's and battlespeed.c's callees outside the clock. */
int cpuTimingOn; /* (hw.c's weak data reference) */
void etcVBlank(void) {}
void padInit(void) {}
void padVBlank(void) {}
void quitInit(void) {}
void quitPoll(int pump) { (void)pump; }
void stallInit(void) {}
void invariantVBlank(void) {}
void slowdownSafePoint(void) {}
static int replaying;
int slowdownLogVBlank(int64_t* vblank) { (void)vblank; return replaying; }
static int online;
int netplayActive(void) { return online; }
static uint32_t cdQueued;
uint32_t spuCdQueued(void) { return cdQueued; }
void spuSync(double now) { (void)now; }

/* What the game shows (gpu/wide.c's pcWideSetMode and pcWideBlock). */
static int kind = PC_WIDE_KERNEL;
static unsigned wideBlocked;
void pcFirstPersonEnd(void) {} /* the field also leaves its look mode on a transition */
int pcWideGetMode(void) { return kind; }
int pcWideSetMode(int k) {
    int old = kind;
    if (k >= 0 && k < PC_WIDE_KINDS) kind = k;
    return old;
}
void pcWideBlock(unsigned int reason, int on) {
    if (on) wideBlocked |= reason;
    else wideBlocked &= ~reason;
}

/* The field's data wide.c's movie call reads, and its two calls. */
MovieParams fieldMovieParams;
s32 onlyUpdateDirector;
void pcFieldMovieBlock(s32 on);
void pcFieldWideExit(void);

/* A state: the bytes in the order the files give them (savestate.c's). */
struct XgState {
    int loading;
    size_t at;
    unsigned char bytes[256];
};
void xgStateIo(XgState* s, const char* tag, void* p, size_t n) {
    (void)tag;
    if (s->at + n > sizeof s->bytes) abort();
    if (s->loading) memcpy(p, s->bytes + s->at, n);
    else memcpy(s->bytes + s->at, p, n);
    s->at += n;
}
static void stateSave(XgState* s) {
    memset(s, 0, sizeof *s);
    battleSpeedStateIo(s);
}
static void stateLoad(XgState* s) {
    s->loading = 1;
    s->at = 0;
    battleSpeedStateIo(s);
}

/* One vblank: the factor of the frame it starts. */
static int vblank(void) {
    hwWaitVSync();
    return hwGameSpeed();
}

/* n vblanks, each at `factor`, one picture in `factor` shown. */
static int steady(int n, int factor) {
    int shown = 0;
    for (int i = 0; i < n; i++) {
        if (vblank() != factor) {
            fprintf(stderr, "vblank %d of %d: %dx, not %dx\n", i, n, hwGameSpeed(), factor);
            return 0;
        }
        shown += !battleSpeedHidden();
    }
    return factor <= 1 ? shown == n : abs(shown * factor - n) <= factor;
}

/* n battle frames (battleRender's call, then its VSync), each at `factor`. */
static int battle(int n, int wait, int factor) {
    for (int i = 0; i < n; i++) {
        battleSpeedBattleFrame(wait, 0);
        if (vblank() != factor) {
            fprintf(stderr, "battle frame %d of %d: %dx, not %dx\n", i, n, hwGameSpeed(), factor);
            return 0;
        }
    }
    return 1;
}

/* Nothing of the speed's happened so far: the clock was never re-anchored
 * (hwSetGameSpeed never acted) and no picture is withheld. */
static int inert(void) {
    double now = hwNow();
    return gameSpeed == 1 && audioBaseA == 0 && audioBaseV == 0 && hwAudioTime(now) == now && !hidden && sinceShown == 0 &&
           !seenCdAudio;
}

/* The C runtime's environment too: on Windows getenv does not see SDL's. */
static void setEnv(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value);
#endif
    if (value[0] != '\0') SDL_setenv_unsafe(key, value, 1);
    else SDL_unsetenv_unsafe(key);
}

static int show(void) {
    printf("field_speed=%d battle_speed=%d\n", fieldSpeedChosen(), battleSpeedChosen());
    return 0;
}

static const int others[] = {PC_WIDE_KERNEL, PC_WIDE_BATTLE, PC_WIDE_BATTLING, PC_WIDE_MENU, PC_WIDE_MOVIE};
static const int places[] = {PC_WIDE_FIELD, PC_WIDE_WORLDMAP};
static const int fast[] = {2, 4};

int main(int argc, char** argv) {
    if (argc > 1) {
        if (!SDL_Init(0)) return 2;
        if (argc == 2 && strcmp(argv[1], "--show") == 0) return show();
        if (argc == 3 && strcmp(argv[1], "--set-field") == 0) {
            fieldSpeedSetChosen(atoi(argv[2]));
            return show();
        }
        if (argc == 3 && strcmp(argv[1], "--set-battle") == 0) {
            battleSpeedSetChosen(atoi(argv[2]));
            return show();
        }
        fprintf(stderr, "usage: fieldspeedtest [--show | --set-field N | --set-battle N]\n");
        return 2;
    }
    setEnv("XG_CLOCK", "step");
    setEnv("XG_SETTINGS", "none");
    setEnv("XG_BATTLE_SPEED", "");
    setEnv("XG_FIELD_SPEED", "");
    setEnv("XG_BATTLE_SPEED_LOG", "");
    CHECK(SDL_Init(0));
    CHECK(hwInit() && stepClock);
    XgState before, after;

    /* The defaults: both off. */
    section = "defaults";
    CHECK(fieldSpeedChosen() == 0 && battleSpeedChosen() == 0);
    CHECK(fieldSpeedWanted() == 1);

    /* Off, then 1x: every kind, CD audio and a movie leave the clock and
     * battlespeed.c's state as they were. */
    for (int pass = 0; pass < 2; pass++) {
        section = pass == 0 ? "off" : "1x";
        if (pass == 1) {
            fieldSpeedSetChosen(1);
            CHECK(fieldSpeedChosen() == 1 && battleSpeedChosen() == 0);
        }
        stateSave(&before);
        uint32_t c0 = hwVSyncCount();
        for (int k = 0; k < PC_WIDE_KINDS; k++) {
            pcWideSetMode(k);
            CHECK(steady(40, 1) && inert());
        }
        pcWideSetMode(PC_WIDE_FIELD);
        cdQueued = 100;
        CHECK(steady(40, 1) && inert());
        cdQueued = 0;
        pcFieldMovieBlock(1);
        CHECK(steady(40, 1) && inert());
        pcFieldMovieBlock(0);
        pcWideSetMode(PC_WIDE_WORLDMAP);
        CHECK(steady(40, 1) && inert());
        CHECK(hwVSyncCount() - c0 == 40u * (PC_WIDE_KINDS + 3));
        stateSave(&after);
        CHECK(before.at == after.at && memcmp(before.bytes, after.bytes, before.at) == 0);
        CHECK(fieldSpeedWanted() == 1);
    }

    /* The setting's values: 0, 1, 2, 4; battle_speed is not its business. */
    section = "normalize";
    static const int given[] = {-3, 0, 1, 2, 3, 4, 5, 99}, kept[] = {0, 0, 1, 2, 2, 4, 4, 4};
    for (int i = 0; i < 8; i++) {
        fieldSpeedSetChosen(given[i]);
        CHECK(fieldSpeedChosen() == kept[i] && battleSpeedChosen() == 0);
    }
    battleSpeedSetChosen(2);
    CHECK(fieldSpeedChosen() == 4 && battleSpeedChosen() == 2);
    fieldSpeedSetChosen(2);
    CHECK(fieldSpeedChosen() == 2 && battleSpeedChosen() == 2);
    battleSpeedSetChosen(0);
    CHECK(fieldSpeedChosen() == 2 && battleSpeedChosen() == 0);
    section = "contexts";
    pcWideSetMode(PC_WIDE_KERNEL);
    CHECK(steady(4, 1) && inert()); /* on, on another screen: still nothing happened */

    /* 2x and 4x on the field and the world map: the vblanks keep their period
     * on the game's clock, the audio clock gets a half or a quarter of it,
     * one picture in two or four is shown. */
    section = "field and world map";
    for (int f = 0; f < 2; f++) {
        for (int p = 0; p < 2; p++) {
            fieldSpeedSetChosen(fast[f]);
            pcWideSetMode(places[p]);
            CHECK(vblank() == fast[f]); /* from the first vblank */
            CHECK(steady(fast[f] * 4 - 1, fast[f]));
            double g0 = hwNow(), a0 = hwAudioTime(g0), period = hwVSyncPeriod();
            uint32_t c0 = hwVSyncCount();
            CHECK(steady(240, fast[f]));
            double g1 = hwNow(), a1 = hwAudioTime(g1);
            CHECK(hwVSyncCount() - c0 == 240 && fabs((g1 - g0) - 240 * period) < 2 * period);
            CHECK(fabs((a1 - a0) - (g1 - g0) / fast[f]) < 1e-9);
            pcWideSetMode(PC_WIDE_KERNEL);
            CHECK(vblank() == 1 && !battleSpeedHidden());
        }
    }

    /* Every other kind is at 1x from its first vblank, as a mode of its own
     * (bootGame) and nested (a menu over the field: set, then the old kind
     * put back), and the field's speed is back with the field. A battle
     * mode without battle frames is the battle's loading. */
    section = "contexts";
    for (int f = 0; f < 2; f++) {
        fieldSpeedSetChosen(fast[f]);
        for (int p = 0; p < 2; p++) {
            pcWideSetMode(places[p]);
            CHECK(vblank() == fast[f]);
            for (int o = 0; o < 5; o++) {
                int old = pcWideSetMode(others[o]);
                CHECK(old == places[p]);
                CHECK(fieldSpeedWanted() == 1);
                CHECK(steady(40, 1));
                pcWideSetMode(old);
                CHECK(vblank() == fast[f]);
                CHECK(steady(8, fast[f]));
            }
        }
    }

    /* Battles: battle_speed alone decides them, field_speed the field. */
    section = "battles";
    fieldSpeedSetChosen(4);
    pcWideSetMode(PC_WIDE_FIELD);
    CHECK(vblank() == 4);
    pcWideSetMode(PC_WIDE_BATTLE);
    CHECK(steady(30, 1));       /* loading */
    CHECK(battle(60, 0, 1));    /* battle speed off: 1x, whatever the field's */
    battleSpeedSetChosen(1);
    CHECK(battle(60, 0, 1) && fieldSpeedChosen() == 4);
    battleSpeedSetChosen(2);
    CHECK(fieldSpeedChosen() == 4 && battleSpeedChosen() == 2);
    CHECK(battle(60, 0, 2));    /* its own 2x, not the field's 4x */
    CHECK(battle(20, 1, 1));    /* waiting for the player */
    CHECK(battle(20, 0, 2));
    CHECK(battleSpeedStep() == 1 && battleSpeedChosen() == 4 && fieldSpeedChosen() == 4); /* the battle's control */
    fieldSpeedSetChosen(2);
    CHECK(battle(40, 0, 4) && battleSpeedChosen() == 4); /* 4x in the battle, the field's at 2x */
    pcWideSetMode(PC_WIDE_FIELD);
    CHECK(steady(BATTLE_GAP - 1, 4)); /* battle frames up to BATTLE_GAP vblanks ago: still the battle's */
    CHECK(vblank() == 2 && steady(40, 2));
    pcWideSetMode(PC_WIDE_BATTLE);
    CHECK(steady(30, 1));
    CHECK(battle(40, 0, 4));
    fieldSpeedSetChosen(0);     /* the field's off: the battle's stays */
    CHECK(battle(40, 0, 4) && battleSpeedChosen() == 4);
    pcWideSetMode(PC_WIDE_FIELD);
    hwWaitVSync();
    for (int i = 0; i < BATTLE_GAP + 1; i++) hwWaitVSync();
    CHECK(steady(40, 1));       /* ... and the field at 1x */
    fieldSpeedSetChosen(4);
    battleSpeedSetChosen(0);
    CHECK(fieldSpeedChosen() == 4 && battleSpeedChosen() == 0);
    CHECK(vblank() == 4);
    CHECK(battleSpeedStep() == 0 && fieldSpeedChosen() == 4); /* the battle's control: nothing on the field */

    /* A field movie, through the field's own calls, with no CD audio queued:
     * 1x from the first vblank after its start to its end, time after time. */
    section = "movie";
    cdQueued = 0;
    onlyUpdateDirector = 0;
    memset(&fieldMovieParams, 0, sizeof fieldMovieParams);
    for (int f = 0; f < 2; f++) {
        fieldSpeedSetChosen(fast[f]);
        pcWideSetMode(PC_WIDE_FIELD);
        CHECK(vblank() == fast[f] && steady(CD_AUDIO_HOLD + 8, fast[f])); /* (no CD audio holds it) */
        for (int rep = 0; rep < 4; rep++) {
            pcFieldMovieBlock(1);
            CHECK(kind == PC_WIDE_FIELD && spuCdQueued() == 0);
            CHECK(vblank() == 1 && !battleSpeedHidden()); /* the first vblank of the movie */
            CHECK((wideBlocked & PC_WIDE_BLOCK_MOVIE) != 0);
            CHECK(steady(rep == 0 ? 400 : 30, 1)); /* a silent movie */
            pcFieldMovieBlock(0);
            CHECK(vblank() == fast[f] && (wideBlocked & PC_WIDE_BLOCK_MOVIE) == 0);
            CHECK(steady(8, fast[f]));
        }
        /* The movie beside a running field, outside the frame buffers: the
         * wide picture is not blocked; the speed is held all the same. */
        onlyUpdateDirector = 2;
        fieldMovieParams.m2_buffer0X = fieldMovieParams.m6_buffer1X = 0x200;
        fieldMovieParams.m10_modeAndSize[1] = fieldMovieParams.m10_modeAndSize[2] = 64;
        pcFieldMovieBlock(1);
        CHECK((wideBlocked & PC_WIDE_BLOCK_MOVIE) == 0);
        CHECK(vblank() == 1 && steady(60, 1));
        pcFieldMovieBlock(0);
        onlyUpdateDirector = 0;
        memset(&fieldMovieParams, 0, sizeof fieldMovieParams);
        CHECK(vblank() == fast[f]);
        /* A movie with XA audio: the CD audio rule holds on after its end. */
        pcFieldMovieBlock(1);
        cdQueued = 100;
        CHECK(steady(50, 1));
        pcFieldMovieBlock(0);
        cdQueued = 0;
        CHECK(steady(CD_AUDIO_HOLD, 1));
        CHECK(steady(8, fast[f]));
        /* The setting changed inside a movie: the movie is known all along. */
        fieldSpeedSetChosen(0);
        CHECK(vblank() == 1);
        pcFieldMovieBlock(1);
        CHECK(steady(4, 1));
        fieldSpeedSetChosen(fast[f]);
        CHECK(steady(20, 1));
        pcFieldMovieBlock(0);
        CHECK(vblank() == fast[f]);
        /* Started twice, ended once (a flag, as the wide picture's block);
         * ended with none playing. */
        pcFieldMovieBlock(1);
        pcFieldMovieBlock(1);
        CHECK(steady(4, 1));
        pcFieldMovieBlock(0);
        CHECK(vblank() == fast[f]);
        pcFieldMovieBlock(0);
        CHECK(steady(4, fast[f]));
    }

    /* The flag's life: it ends with the field mode (pcFieldWideExit, the
     * field's start and end), means nothing on the world map, and a soft
     * reset in a movie leaves nothing behind for the next field. */
    section = "movie lifecycle";
    for (int f = 0; f < 2; f++) {
        fieldSpeedSetChosen(fast[f]);
        pcWideSetMode(PC_WIDE_FIELD);
        CHECK(vblank() == fast[f]);
        pcFieldMovieBlock(1);
        CHECK(steady(4, 1));
        pcFieldWideExit(); /* the field ends in the movie (a mode change) */
        pcWideSetMode(PC_WIDE_BATTLE);
        CHECK(steady(4, 1));
        pcWideSetMode(PC_WIDE_FIELD);
        CHECK(vblank() == fast[f] && (wideBlocked & PC_WIDE_BLOCK_MOVIE) == 0);
        pcFieldMovieBlock(1);
        CHECK(steady(4, 1));
        pcWideSetMode(PC_WIDE_KERNEL); /* a soft reset: no call of the field's */
        CHECK(steady(4, 1));
        pcWideSetMode(PC_WIDE_WORLDMAP); /* a save loaded on the world map */
        CHECK(vblank() == fast[f] && steady(8, fast[f]));
        pcWideSetMode(PC_WIDE_FIELD);
        pcFieldWideExit(); /* the field's start */
        CHECK(vblank() == fast[f] && steady(8, fast[f]));
    }

    /* Save states and rewind: the flag is in battleSpeedStateIo's part. A
     * state from inside a movie loads into the movie; one from before it
     * leaves none. */
    section = "state";
    for (int f = 0; f < 2; f++) {
        fieldSpeedSetChosen(fast[f]);
        pcWideSetMode(PC_WIDE_FIELD);
        CHECK(vblank() == fast[f]);
        stateSave(&before); /* no movie */
        pcFieldMovieBlock(1);
        CHECK(steady(6, 1));
        stateSave(&after); /* in the movie */
        CHECK(after.at == before.at && after.at > 0);
        stateLoad(&before); /* rewound to before it */
        CHECK(vblank() == fast[f] && steady(8, fast[f]));
        stateLoad(&after); /* and into it again */
        CHECK(vblank() == 1 && steady(30, 1));
        pcFieldMovieBlock(0);
        CHECK(vblank() == fast[f]);
        stateLoad(&after); /* after its end, back into it */
        CHECK(vblank() == 1 && steady(30, 1));
        stateLoad(&before);
        CHECK(vblank() == fast[f]);
    }

    /* battlespeed.c's rules hold over the field's speed too. */
    section = "safeguards";
    fieldSpeedSetChosen(4);
    pcWideSetMode(PC_WIDE_WORLDMAP);
    CHECK(vblank() == 4);
    online = 1;
    CHECK(steady(20, 1));
    online = 0;
    CHECK(vblank() == 4);
    replaying = 1;
    CHECK(steady(20, 1));
    replaying = 0;
    CHECK(vblank() == 4);
    cdQueued = 1;
    CHECK(steady(20, 1));
    cdQueued = 0;
    CHECK(steady(CD_AUDIO_HOLD, 1));
    CHECK(vblank() == 4);

    /* Turned off and changed over and over: each vblank has the factor of
     * the setting then, and at 1x the audio clock is the game's rate again. */
    section = "transitions";
    static const int seq[] = {4, 2, 1, 4, 0, 2, 4, 2, 0, 4, 1, 2, 0};
    for (int p = 0; p < 2; p++) {
        pcWideSetMode(places[p]);
        for (int i = 0; i < (int)(sizeof seq / sizeof seq[0]); i++) {
            int want = seq[i] <= 1 ? 1 : seq[i];
            fieldSpeedSetChosen(seq[i]);
            CHECK(vblank() == want);
            double g0 = hwNow(), a0 = hwAudioTime(g0);
            CHECK(steady(16, want));
            double g1 = hwNow(), a1 = hwAudioTime(g1);
            CHECK(fabs((a1 - a0) - (g1 - g0) / want) < 1e-9);
            if (want == 1) CHECK(!battleSpeedHidden() && sinceShown == 0);
            int old = pcWideSetMode(PC_WIDE_MENU); /* its menu, at every setting */
            CHECK(steady(6, 1));
            pcWideSetMode(old);
        }
    }
    CHECK(fieldSpeedChosen() == 0 && battleSpeedChosen() == 0 && hwGameSpeed() == 1);

    /* The control: Tab and Select+R2 step field_speed on the field and the
     * world map, with the option on; nowhere else, and in a battle they are
     * battle_speed's. */
    section = "control";
    enum { L2 = 0x0001, R2 = 0x0002, L1 = 0x0004, R1 = 0x0008, CROSS = 0x0040, SEL = 0x0100, START = 0x0800 };
    static const int cycle[] = {2, 4, 1, 2, 4};
    for (int p = 0; p < 2; p++) {
        pcWideSetMode(places[p]);
        fieldSpeedSetChosen(0);
        CHECK(fieldSpeedStep() == 0 && battleSpeedKey(SDL_SCANCODE_TAB, 0) == 0 && fieldSpeedChosen() == 0); /* off */
        battleSpeedPad(0);
        battleSpeedPad(SEL | R2);
        CHECK(fieldSpeedChosen() == 0);
        battleSpeedPad(0);
        fieldSpeedSetChosen(1);
        for (int i = 0; i < 5; i++) {
            CHECK(battleSpeedKey(SDL_SCANCODE_TAB, 0) == 1 && fieldSpeedChosen() == cycle[i] && battleSpeedChosen() == 0);
        }
        CHECK(battleSpeedKey(SDL_SCANCODE_TAB, SDL_KMOD_SHIFT) == 0 && fieldSpeedChosen() == 4); /* not the key */
        fieldSpeedSetChosen(1);
        battleSpeedPad(SEL); /* Select, then R2 */
        CHECK(fieldSpeedChosen() == 1);
        battleSpeedPad(SEL | R2);
        CHECK(fieldSpeedChosen() == 2);
        battleSpeedPad(SEL | R2); /* held: once */
        CHECK(fieldSpeedChosen() == 2);
        battleSpeedPad(0);
        battleSpeedPad(R2); /* R2, then Select */
        battleSpeedPad(R2 | SEL);
        CHECK(fieldSpeedChosen() == 4);
        battleSpeedPad(0);
        battleSpeedPad(SEL | R2); /* together */
        CHECK(fieldSpeedChosen() == 1);
        battleSpeedPad(L1 | R1 | START);
        battleSpeedPad(L1 | R1 | START | SEL | R2); /* inside the soft reset's buttons: nothing */
        CHECK(fieldSpeedChosen() == 1);
        battleSpeedPad(0);
        online = 1; /* online: a note, and 1x stays */
        CHECK(fieldSpeedStep() == 1 && fieldSpeedChosen() == 1);
        online = 0;
    }
    for (int o = 0; o < 5; o++) { /* the menus over them, a battle's loading, the kernel's screens, the movie mode */
        pcWideSetMode(others[o]);
        CHECK(fieldSpeedStep() == 0 && battleSpeedKey(SDL_SCANCODE_TAB, 0) == 0);
        battleSpeedPad(SEL | R2);
        battleSpeedPad(0);
        CHECK(fieldSpeedChosen() == 1 && battleSpeedChosen() == 0);
    }
    pcWideSetMode(PC_WIDE_BATTLE); /* in a battle: battle speed's control */
    battleSpeedSetChosen(1);
    battleSpeedBattleFrame(0, 0);
    CHECK(battleSpeedKey(SDL_SCANCODE_TAB, 0) == 1 && battleSpeedChosen() == 2 && fieldSpeedChosen() == 1);
    battleSpeedPad(SEL | R2);
    battleSpeedPad(0);
    CHECK(battleSpeedChosen() == 4 && fieldSpeedChosen() == 1);
    battleSpeedSetChosen(0);
    for (int i = 0; i < BATTLE_GAP + 2; i++) hwWaitVSync();
    settingsSet("field_speed_key", "F7"); /* the keys of its own */
    settingsSet("field_speed_button", "l1+r1");
    pcWideSetMode(PC_WIDE_FIELD);
    CHECK(battleSpeedKey(SDL_SCANCODE_TAB, 0) == 0 && fieldSpeedChosen() == 1);
    CHECK(battleSpeedKey(SDL_SCANCODE_F7, 0) == 1 && fieldSpeedChosen() == 2);
    battleSpeedPad(SEL | R2);
    battleSpeedPad(0);
    CHECK(fieldSpeedChosen() == 2);
    battleSpeedPad(L1 | R1);
    battleSpeedPad(0);
    CHECK(fieldSpeedChosen() == 4);

    /* Select as the port's chord button on the field and the world map. */
    section = "select chord";
    CHECK(speedControlHoldsSelect() == 0); /* field_speed_button without Select: */
    CHECK(selectChordFilter(SEL) == SEL && selectChordFilter(SEL | R2) == (SEL | R2) && selectChordFilter(0) == 0);
    settingsUnset("field_speed_key");
    settingsUnset("field_speed_button");
    for (int p = 0; p < 2; p++) {
        pcWideSetMode(places[p]);
        fieldSpeedSetChosen(0); /* off: as they are */
        static const unsigned plain[] = {SEL, SEL | R2, R2, 0, SEL | L1, SEL, 0};
        for (int i = 0; i < 7; i++) CHECK(selectChordFilter(plain[i]) == plain[i]);
        fieldSpeedSetChosen(1);
        CHECK(speedControlHoldsSelect() == 1);
        /* A lone Select: kept while held, the game's as it is let go. */
        CHECK(selectChordFilter(0) == 0);
        for (int i = 0; i < 5; i++) CHECK(selectChordFilter(SEL) == 0);
        for (int i = 0; i < SELECT_TAP; i++) CHECK(selectChordFilter(0) == SEL);
        CHECK(selectChordFilter(0) == 0);
        /* Select, then R2: neither reaches the game, R2 until let go. */
        CHECK(selectChordFilter(SEL) == 0 && selectChordFilter(SEL | R2) == 0 && selectChordFilter(SEL | R2) == 0);
        CHECK(selectChordFilter(R2) == 0 && selectChordFilter(0) == 0 && selectChordFilter(0) == 0);
        /* R2 held, then Select: R2 the game's all along, Select never. */
        CHECK(selectChordFilter(R2) == R2 && selectChordFilter(R2 | SEL) == R2 && selectChordFilter(R2) == R2);
        CHECK(selectChordFilter(0) == 0);
        /* Together (rewind's Select+L2 as well), and an L1 joining later. */
        CHECK(selectChordFilter(SEL | L2) == 0 && selectChordFilter(SEL | L2 | L1) == 0 && selectChordFilter(L1) == 0);
        CHECK(selectChordFilter(0) == 0 && selectChordFilter(0) == 0);
        /* Another button with Select: the game's; Select comes as it is let go. */
        CHECK(selectChordFilter(SEL | CROSS) == CROSS && selectChordFilter(CROSS) == (CROSS | SEL));
        for (int i = 1; i < SELECT_TAP; i++) CHECK(selectChordFilter(0) == SEL);
        CHECK(selectChordFilter(0) == 0);
        /* Pressed again while handed over: kept again. */
        CHECK(selectChordFilter(SEL) == 0 && selectChordFilter(0) == SEL && selectChordFilter(SEL) == 0);
        CHECK(selectChordFilter(SEL | R2) == 0 && selectChordFilter(0) == 0);
        /* Start: the soft reset's buttons go through as they are. */
        CHECK(selectChordFilter(SEL) == 0 && selectChordFilter(SEL | START) == (SEL | START));
        CHECK(selectChordFilter(SEL | START | L1 | R1) == (SEL | START | L1 | R1) && selectChordFilter(0) == 0);
        CHECK(selectChordFilter(0) == 0);
        /* Online, and on a menu over the screen: as they are. */
        online = 1;
        CHECK(selectChordFilter(SEL) == SEL && selectChordFilter(SEL | R2) == (SEL | R2) && selectChordFilter(0) == 0);
        online = 0;
        int old = pcWideSetMode(PC_WIDE_MENU);
        CHECK(selectChordFilter(SEL) == SEL && selectChordFilter(0) == 0);
        /* Select held as the menu closes: the game has it already, so it stays
         * held, and its release hands nothing over. */
        CHECK(selectChordFilter(SEL) == SEL);
        pcWideSetMode(old);
        CHECK(selectChordFilter(SEL) == SEL && selectChordFilter(0) == 0 && selectChordFilter(0) == 0);
        /* ... and as the menu opens: the game's at once. */
        CHECK(selectChordFilter(SEL) == 0);
        pcWideSetMode(PC_WIDE_MENU);
        CHECK(selectChordFilter(SEL) == SEL && selectChordFilter(0) == 0);
        pcWideSetMode(old);
        CHECK(selectChordFilter(0) == 0);
    }
    fieldSpeedSetChosen(0);
    CHECK(fieldSpeedChosen() == 0 && battleSpeedChosen() == 0);

    SDL_Quit();
    printf("Field speed: %d checks passed\n", checks);
    return 0;
}
