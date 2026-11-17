/* Story battle speed (xg-mrj.7): the controller (battlespeed.c) with the
 * battle's class as the game records it (gameplay.c's pcGameplayRandomBattle
 * and pcGameplayBattleStart, the production file) and the settings
 * (settings.c), on hw.c's stepped clock. No disc, GPU or wall clock.
 * tools/test_story_battle_speed.py builds and runs it (XG_SETTINGS names a
 * scratch file) and its negative controls.
 *
 *   - saved speeds 0, 1, 2, 4: a random battle runs at the saved speed; a
 *     scripted one at 1x with a note, the key and the pad's chord step it
 *     alone (1, 2, 4), and the setting, its revision and its file stay as
 *     they were; the next random battle is at the saved speed again;
 *   - off: no note, no step, nothing of the clock touched;
 *   - the waits for the player, a new name, CD audio, online and the slowdown
 *     replay hold a stepped scripted battle at 1x as any other;
 *   - a scripted battle's speed ends at the next battle start (consecutive
 *     ones, a chain, a retry), at a state's load (the same battle's, another
 *     one's, a random one's) and is not part of a state's bytes;
 *   - a roll for another config, a used-up roll and a chained start are
 *     scripted;
 *   - --override: XG_BATTLE_SPEED holds every battle at its speed, as before.
 */
#include <math.h>

#include "../hw.c"
#include "../libapi.c"
#include "../battlespeed.c"
#include "../gameplay.h"
#include "pc_gameplay.h"

static int checks;
static const char* section = "start";
#define CHECK_CONTEXT fprintf(stderr, "[%s] ", section)
#include "test/check.h"

/* hw.c's and battlespeed.c's callees outside the clock. */
int cpuTimingOn; /* (hw.c's weak data reference: PE links need a definition) */
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

/* The corner note (gpu/present.c): what was asked for. */
static int notes;
static char lastNote[64];
void presentShowNote(const char* text) {
    notes++;
    SDL_strlcpy(lastNote, text, sizeof lastNote);
}
static int noteIs(const char* text) {
    return strcmp(lastNote, text) == 0;
}

/* A state: the two files' bytes in the order they give them (savestate.c's). */
struct XgState {
    int loading;
    size_t at;
    unsigned char bytes[8192];
};
int xgStateLoading(const XgState* s) { return s->loading; }
int xgStateSaving(const XgState* s) { return !s->loading; }
void xgStateIo(XgState* s, const char* tag, void* p, size_t n) {
    (void)tag;
    if (s->at + n > sizeof s->bytes) abort();
    if (s->loading) memcpy(p, s->bytes + s->at, n);
    else memcpy(s->bytes + s->at, p, n);
    s->at += n;
}
void gameplayStateIo(XgState* s);
static void stateSave(XgState* s) {
    memset(s, 0, sizeof *s);
    gameplayStateIo(s);
    battleSpeedStateIo(s);
}
static void stateLoad(XgState* s) {
    s->loading = 1;
    s->at = 0;
    gameplayStateIo(s);
    battleSpeedStateIo(s);
}
static XgState stateA, stateR, stateT;

/* n battle frames (battleRender's call, then its VSync): the factor they all
 * ran at, or -1. */
static int run(int n, int wait, unsigned text) {
    int factor = 0;
    for (int i = 0; i < n; i++) {
        battleSpeedBattleFrame(wait, text);
        hwWaitVSync();
        if (i == 0) {
            factor = hwGameSpeed();
        } else if (hwGameSpeed() != factor) {
            fprintf(stderr, "battle frame %d of %d: %dx after %dx\n", i, n, hwGameSpeed(), factor);
            return -1;
        }
    }
    return factor;
}

/* The battle's frames stop (its end): 1x once BATTLE_GAP vblanks pass. */
static int rest(void) {
    for (int i = 0; i < BATTLE_GAP + 1; i++) hwWaitVSync();
    return hwGameSpeed() == 1 && !battleSpeedInBattle() && !battleSpeedHidden();
}

/* The game's two ways into a battle (src/field/f_80078B5C.c's roll and
 * src/battle/b_80070E2C.c's start). */
static int randomStart(int config) {
    pcGameplayRandomBattle(config);
    pcGameplayBattleStart(config, 0);
    return pcGameplayBattleClass() == GAMEPLAY_BATTLE_RANDOM;
}
static int scriptedStart(int config, int chained) {
    pcGameplayBattleStart(config, chained);
    return pcGameplayBattleClass() == GAMEPLAY_BATTLE_SCRIPTED;
}

/* The settings file as it is on disk (none with XG_SETTINGS=none). */
static char fileWas[4096];
static size_t fileRead(char* buf, size_t size) {
    const char* path = settingsPath();
    FILE* f = path != NULL ? fopen(path, "rb") : NULL;
    size_t n = f != NULL ? fread(buf, 1, size - 1, f) : 0;
    if (f != NULL) fclose(f);
    buf[n] = '\0';
    return n;
}

/* The setting is what it was: its value, the settings' revision (no
 * settingsSet, no pin), the file's bytes, and no key of a story speed. */
static int unchanged(int saved, unsigned revision) {
    char now[sizeof fileWas];
    fileRead(now, sizeof now);
    if (battleSpeedChosen() != saved || settingsRevision() != revision || strcmp(now, fileWas) != 0 ||
        strstr(now, "story") != NULL || settingsPinned("battle_speed") || settingsOverride("battle_speed") != NULL) {
        fprintf(stderr, "battle_speed %d (saved %d), revision %u (%u), file:\n%s", battleSpeedChosen(), saved,
                settingsRevision(), revision, now);
        return 0;
    }
    return 1;
}

/* Nothing of the speed's happened so far (hwSetGameSpeed never acted). */
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

/* One saved speed: a random battle, a scripted one stepped by the key and
 * the chord, the holds, scripted battles in a row, a random one again. */
static int choice(int saved, int first) {
    int on = saved > 0, base = on ? saved : 1, n;
    battleSpeedSetChosen(saved);
    unsigned revision = settingsRevision();
    fileRead(fileWas, sizeof fileWas);
    notes = 0;

    section = "random";
    CHECK(randomStart(3));
    CHECK(run(30, 0, 0) == base && notes == 0);
    CHECK(battleSpeedChosen() == saved && rest());

    section = "scripted";
    CHECK(scriptedStart(7, 0) && notes == 0);
    CHECK(run(30, 0, 0) == 1); /* 1x, whatever the saved speed */
    CHECK(notes == on);
    CHECK(!on || noteIs("Battle speed 1x (story battle)"));
    CHECK(battleSpeedKey(SDL_SCANCODE_TAB, SDL_KMOD_LSHIFT) == 0 && run(4, 0, 0) == 1); /* not the control's key */
    CHECK(battleSpeedKey(SDL_SCANCODE_TAB, 0) == on); /* Tab */
    CHECK(run(30, 0, 0) == (on ? 2 : 1));
    CHECK(!on || noteIs("Battle speed 2x (story battle)"));
    battleSpeedPad(0);
    battleSpeedPad(0x0100);
    battleSpeedPad(0x0102); /* Select, then R2: the chord */
    CHECK(run(30, 0, 0) == (on ? 4 : 1));
    battleSpeedPad(0x0102); /* held on: once */
    CHECK(run(4, 0, 0) == (on ? 4 : 1));
    battleSpeedPad(0);
    CHECK(notes == (on ? 3 : 0));
    CHECK(!on || noteIs("Battle speed 4x (story battle)"));
    CHECK(unchanged(saved, revision));
    CHECK(battleSpeedStep() == on && run(10, 0, 0) == 1); /* 4 -> 1 -> 2 -> 4 */
    CHECK(battleSpeedStep() == on && battleSpeedStep() == on && run(10, 0, 0) == (on ? 4 : 1));
    CHECK(unchanged(saved, revision));
    if (!on) {
        section = "off";
        CHECK(notes == 0 && story.speed <= 1 && !story.noted && hwGameSpeed() == 1);
        CHECK(!first || inert());
    }

    if (on) {
        section = "safeguards"; /* the scripted battle at its 4x */
        CHECK(run(20, 1, 0) == 1); /* waiting for the player */
        CHECK(run(20, 0, 0) == 4);
        CHECK(run(TEXT_HOLD, 0, 0x4) == 1); /* a new name */
        CHECK(run(20, 0, 0x4) == 4);
        cdQueued = 100; /* CD audio, and CD_AUDIO_HOLD vblanks after it */
        CHECK(run(20, 0, 0x4) == 1);
        cdQueued = 0;
        CHECK(run(CD_AUDIO_HOLD, 0, 0x4) == 1);
        CHECK(run(20, 0, 0x4) == 4);
        online = 1;
        n = notes;
        CHECK(run(20, 0, 0x4) == 1);
        CHECK(battleSpeedStep() == 1 && notes == n + 1 && noteIs("Battle speed: 1x online"));
        online = 0;
        CHECK(run(20, 0, 0x4) == 4); /* (the control changed nothing online) */
        replaying = 1;
        CHECK(run(20, 0, 0x4) == 1);
        replaying = 0;
        CHECK(run(20, 0, 0) == 4);
        CHECK(unchanged(saved, revision));
    }

    section = "consecutive"; /* the next scripted battle: not the last one's 4x */
    CHECK(rest());
    n = notes;
    CHECK(scriptedStart(8, 0) && run(30, 0, 0) == 1 && notes == n + on);
    CHECK(battleSpeedStep() == on && run(10, 0, 0) == (on ? 2 : 1));

    section = "chain"; /* a battle event's chain: the next battle starts while frames still come */
    n = notes;
    CHECK(scriptedStart(9, 1) && run(30, 0, 0) == 1 && notes == n + on);
    CHECK(battleSpeedStep() == on && run(10, 0, 0) == (on ? 2 : 1));
    CHECK(unchanged(saved, revision));

    section = "random again";
    CHECK(rest());
    n = notes;
    CHECK(randomStart(3) && run(30, 0, 0) == base && notes == n);
    CHECK(unchanged(saved, revision) && rest());
    return 0;
}

/* At a saved 4x: the encounter marker, retries, states, a reset, the menu. */
static int sequences(void) {
    int n;
    battleSpeedSetChosen(4);
    unsigned revision = settingsRevision();
    fileRead(fileWas, sizeof fileWas);

    section = "markers";
    pcGameplayRandomBattle(3); /* a roll, then a battle of another config */
    CHECK(scriptedStart(5, 0) && run(20, 0, 0) == 1 && rest());
    CHECK(scriptedStart(3, 0) && run(20, 0, 0) == 1 && rest()); /* the roll was used up */
    pcGameplayRandomBattle(3); /* a roll, then a chained start of its config */
    CHECK(scriptedStart(3, 1) && run(20, 0, 0) == 1 && rest());
    CHECK(randomStart(3) && run(20, 0, 0) == 4);
    CHECK(scriptedStart(6, 1) && run(20, 0, 0) == 1); /* a chain out of a random battle */
    CHECK(rest() && randomStart(3) && run(20, 0, 0) == 4 && rest());
    CHECK(unchanged(4, revision));

    section = "retry";
    CHECK(randomStart(3) && run(20, 0, 0) == 4);
    pcGameplayBattleRetryClass(); /* port/src/game/main/retry.c: the battle again, its class kept */
    pcGameplayBattleStart(3, 1);
    CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_RANDOM && run(20, 0, 0) == 4 && rest());
    CHECK(scriptedStart(7, 0) && run(10, 0, 0) == 1);
    CHECK(battleSpeedStep() == 1 && battleSpeedStep() == 1 && run(10, 0, 0) == 4);
    n = notes;
    pcGameplayBattleRetryClass(); /* a scripted battle again: 1x again, with its note */
    pcGameplayBattleStart(7, 1);
    CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_SCRIPTED && run(20, 0, 0) == 1 && notes == n + 1);
    CHECK(unchanged(4, revision) && rest());

    section = "state";
    CHECK(scriptedStart(7, 0) && run(10, 0, 0) == 1);
    CHECK(battleSpeedStep() == 1 && battleSpeedStep() == 1 && run(10, 0, 0) == 4);
    stateSave(&stateA); /* scripted battle A, stepped to 4x */
    CHECK(battleSpeedStep() == 1 && story.speed == 1);
    stateSave(&stateT); /* the same moment at another speed of its own: the same bytes */
    CHECK(stateT.at == stateA.at && memcmp(stateT.bytes, stateA.bytes, stateA.at) == 0);
    CHECK(battleSpeedStep() == 1 && battleSpeedStep() == 1 && run(10, 0, 0) == 4);
    n = notes;
    stateLoad(&stateA); /* a rewind inside the battle: 1x again, one note however many loads */
    CHECK(run(10, 0, 0) == 1 && notes == n + 1 && noteIs("Battle speed 1x (story battle)"));
    stateLoad(&stateA);
    stateLoad(&stateA);
    CHECK(run(10, 0, 0) == 1 && notes == n + 1);
    CHECK(rest() && scriptedStart(8, 0) && run(10, 0, 0) == 1); /* scripted battle B at 2x */
    CHECK(battleSpeedStep() == 1 && run(10, 0, 0) == 2);
    stateLoad(&stateA); /* A's state: not B's 2x */
    CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_SCRIPTED && run(10, 0, 0) == 1);
    CHECK(rest() && randomStart(3) && run(10, 0, 0) == 4);
    stateSave(&stateR); /* a random battle */
    CHECK(rest() && scriptedStart(7, 0) && run(10, 0, 0) == 1);
    CHECK(battleSpeedStep() == 1 && run(10, 0, 0) == 2);
    stateLoad(&stateR); /* into the random battle: the saved speed */
    CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_RANDOM && run(10, 0, 0) == 4);
    stateLoad(&stateA); /* and from it into A */
    CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_SCRIPTED && run(10, 0, 0) == 1);
    CHECK(unchanged(4, revision) && rest());

    section = "reset"; /* the soft reset: the frames just stop */
    CHECK(scriptedStart(7, 0) && run(10, 0, 0) == 1);
    CHECK(battleSpeedStep() == 1 && battleSpeedStep() == 1 && run(10, 0, 0) == 4);
    for (int i = 0; i < 600; i++) hwWaitVSync();
    CHECK(hwGameSpeed() == 1 && !battleSpeedInBattle() && battleSpeedStep() == 0);
    CHECK(scriptedStart(8, 0) && run(20, 0, 0) == 1 && rest());
    CHECK(randomStart(3) && run(20, 0, 0) == 4 && rest());
    CHECK(unchanged(4, revision));

    section = "menu"; /* F1 > Gameplay sets the saved speed during a scripted battle */
    CHECK(scriptedStart(7, 0) && run(10, 0, 0) == 1);
    CHECK(battleSpeedStep() == 1 && battleSpeedStep() == 1 && run(10, 0, 0) == 4);
    battleSpeedSetChosen(2);
    CHECK(run(10, 0, 0) == 4 && battleSpeedChosen() == 2); /* the battle keeps its own */
    CHECK(rest() && randomStart(3) && run(20, 0, 0) == 2 && rest());
    CHECK(scriptedStart(7, 0) && run(4, 0, 0) == 1);
    CHECK(battleSpeedStep() == 1 && run(10, 0, 0) == 2);
    battleSpeedSetChosen(0); /* off: 1x, and no control */
    CHECK(run(10, 0, 0) == 1 && battleSpeedStep() == 0 && rest());
    return 0;
}

/* XG_BATTLE_SPEED=4 over a file's battle_speed=2 (the driver's): every battle
 * at 4x and no story note, until the control writes the setting. */
static int overridden(void) {
    section = "override";
    CHECK(battleSpeedChosen() == 4 && settingsOverride("battle_speed") != NULL);
    CHECK(scriptedStart(7, 0) && run(30, 0, 0) == 4 && notes == 0 && rest());
    CHECK(randomStart(3) && run(30, 0, 0) == 4 && notes == 0 && rest());
    CHECK(scriptedStart(8, 0) && run(30, 0, 0) == 4 && notes == 0);
    CHECK(battleSpeedStep() == 1 && battleSpeedChosen() == 1 && run(10, 0, 0) == 1); /* as before: 4 -> 1, written */
    CHECK(noteIs("Battle speed 1x"));
    CHECK(battleSpeedStep() == 1 && run(10, 0, 0) == 2); /* and on from the speed shown */
    CHECK(battleSpeedStep() == 1 && run(10, 0, 0) == 4);
    return 0;
}

int main(int argc, char** argv) {
    int override = argc > 1 && strcmp(argv[1], "--override") == 0;
    setEnv("XG_CLOCK", "step");
    setEnv("XG_FIELD_SPEED", "");
    setEnv("XG_BATTLE_SPEED_LOG", "");
    if (!override) setEnv("XG_BATTLE_SPEED", "");
    if (getenv("XG_SETTINGS") == NULL) setEnv("XG_SETTINGS", "none"); /* never the player's file */
    CHECK(SDL_Init(0));
    CHECK(hwInit() && stepClock);
    if (override) {
        if (overridden()) return 1;
    } else {
        static const int saved[] = {0, 1, 2, 4};
        for (int i = 0; i < 4; i++) {
            if (choice(saved[i], i == 0)) return 1;
        }
        if (sequences()) return 1;
        if (choice(0, 0)) return 1; /* off again */
    }
    SDL_Quit();
    printf("Story battle speed: %d checks passed%s\n", checks, override ? " (override)" : "");
    return 0;
}
