/* Battle speed (xg-mrj.2): hw.c's game clock against the audio clock, the
 * sound driver's root counter on the audio clock (libapi.c) and the
 * controller's decisions (battlespeed.c), linked whole with their callees
 * outside the clock stubbed.
 *
 *   - until the factor first changes the audio clock is the game's time, to
 *     the bit (a run without battle speed is the same as before);
 *   - root counter 2 ticks at 240 Hz of the audio clock at every factor, its
 *     phase kept across a change; the game's vblanks keep their period, so at
 *     4x a vblank has a quarter of the ticks; the SPU's samples follow;
 *   - on the real clock the vblanks come `factor` times as often;
 *   - the controller: off changes nothing; on, battle frames run at the
 *     chosen factor with N-1 of N pictures withheld, 1x while the battle
 *     waits for the player and for the first frames of a new name, and when
 *     the battle frames stop; the control steps 1, 2, 4 and only in a battle.
 */
#include <math.h>

#include "../hw.c"
#include "../libapi.c"
#include "../battlespeed.c"
#include "../gameplay.h"
static int checks;
#include "test/check.h"

/* hw.c's callees outside the clock: nothing of theirs is under test. */
int cpuTimingOn; /* (hw.c's weak data reference: PE links need a definition) */
void etcVBlank(void) {}
void padInit(void) {}
void padVBlank(void) {}
void quitInit(void) {}
void quitPoll(int pump) { (void)pump; }
void stallInit(void) {}
void invariantVBlank(void) {}
void slowdownSafePoint(void) {}
int slowdownLogVBlank(int64_t* vblank) { (void)vblank; return 0; }
int netplayActive(void) { return 0; }
static uint32_t cdQueued;
uint32_t spuCdQueued(void) { return cdQueued; }

/* The SPU: how far its samples went (spu.c runs to floor(hwAudioTime * rate)). */
static double spuAudio;
void spuSync(double now) {
    double a = hwAudioTime(now);
    if (a > spuAudio) spuAudio = a;
}

/* The sound driver's tick: each one's audio time. */
static int ticks;
static double tickAt[4096];
static void audioTick(void) {
    if (ticks < 4096) tickAt[ticks] = hwAudioTime(hwNow());
    ticks++;
}

static int vblanks(int n) {
    for (int i = 0; i < n; i++) hwWaitVSync();
    return n;
}

/* The ticks' spacing since `from`: every one 1/240 s of the audio clock (to
 * the clock's rounding). */
static int evenTicks(int from) {
    double period = 0x44E8 / (HW_CPU_HZ / 8);
    for (int i = from + 1; i < ticks && i < 4096; i++) {
        if (fabs(tickAt[i] - tickAt[i - 1] - period) > 2e-6) {
            fprintf(stderr, "tick %d: %.9f after the last (%.9f)\n", i, tickAt[i] - tickAt[i - 1], period);
            return 0;
        }
    }
    return 1;
}

/* A battle frame each vblank for n vblanks; the factors and pictures seen. */
static int frames(int n, int wait, unsigned text, int* fourx, int* shown) {
    *fourx = *shown = 0;
    for (int i = 0; i < n; i++) {
        battleSpeedBattleFrame(wait, text);
        hwWaitVSync();
        *fourx += hwGameSpeed() == 4;
        *shown += !battleSpeedHidden();
    }
    return n;
}

/* The C runtime's environment too: on Windows getenv does not see SDL's. */
static void setEnv(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value);
#endif
    if (value[0] != '\0') SDL_setenv_unsafe(key, value, 1);
    else SDL_unsetenv_unsafe(key);
}

int main(void) {
    setEnv("XG_CLOCK", "step");
    setEnv("XG_SETTINGS", "none");
    setEnv("XG_BATTLE_SPEED", "");
    CHECK(SDL_Init(0));
    CHECK(hwInit() && stepClock);

    /* The audio clock is the game's time until the factor changes. */
    for (double t = 0; t < 100; t += 0.37) {
        CHECK(hwAudioTime(t) == t && hwAudioToGame(t) == t);
    }
    CHECK(hwGameSpeed() == 1);

    /* The sound driver's set-up (src/main/sound.c initSoundSystem). */
    s32 ev = OpenEvent(0xF2000002, 2, 0x1000, (void*)audioTick);
    CHECK(SetRCnt(0xF2000002, 0x44E8, 0x1000) && StartRCnt(0xF2000002));
    CHECK(EnableEvent((u32)ev));

    /* Off: battle frames change nothing (no factor, no picture withheld). */
    int fourx, shown;
    frames(240, 0, 0, &fourx, &shown);
    CHECK(fourx == 0 && shown == 240 && hwGameSpeed() == 1);
    CHECK(hwAudioTime(hwNow()) == hwNow());
    CHECK(battleSpeedStep() == 0 && battleSpeedChosen() == 0); /* the control does nothing either */

    /* 1x: 240 ticks a second of the game's time, 4.01 a vblank. */
    vblanks(60);
    int t0 = ticks;
    double g0 = hwNow(), a0 = spuAudio;
    vblanks(600);
    double seconds = hwNow() - g0;
    CHECK(fabs((ticks - t0) - 240 * seconds) <= 1);
    CHECK(fabs((spuAudio - a0) - seconds) < 1e-3);

    /* 4x (the controller, battle frames coming): the vblanks keep their
     * period on the game's clock, the ticks and the SPU a quarter of the rate
     * there: 240 Hz of audio time still, with no jump in the ticks' phase at
     * the change. */
    int before = ticks;
    battleSpeedSetChosen(4);
    frames(1, 0, 0, &fourx, &shown);
    CHECK(hwGameSpeed() == 4 && fourx == 1);
    double now = hwNow();
    CHECK(fabs(hwAudioToGame(hwAudioTime(now + 1.5)) - (now + 1.5)) < 1e-9);
    t0 = ticks;
    g0 = hwNow();
    a0 = spuAudio;
    double v0 = hwVSyncPeriod();
    uint32_t c0 = hwVSyncCount();
    frames(600, 0, 0, &fourx, &shown);
    seconds = hwNow() - g0;
    CHECK(fourx == 600 && shown == 150);
    CHECK(hwVSyncCount() - c0 == 600 && fabs(seconds - 600 * v0) < 2 * v0);
    CHECK(fabs((ticks - t0) - 240 * seconds / 4) <= 1);
    CHECK(fabs((spuAudio - a0) - seconds / 4) < 1e-3);
    CHECK(evenTicks(before > 0 ? before - 1 : 0));

    /* GetRCnt counts on the audio clock: within one tick, from 0 to 0x44E7. */
    s32 r = GetRCnt(0xF2000002);
    CHECK(r >= 0 && r < 0x44E8);

    /* 2x, then off (1x): the audio clock goes on from where it was. */
    before = ticks;
    battleSpeedSetChosen(2);
    frames(300, 0, 0, &fourx, &shown);
    CHECK(hwGameSpeed() == 2 && shown == 150);
    battleSpeedSetChosen(0);
    frames(1, 0, 0, &fourx, &shown);
    CHECK(hwGameSpeed() == 1);
    double a1 = hwAudioTime(hwNow()), g1 = hwNow();
    t0 = ticks;
    vblanks(300);
    CHECK(fabs((hwAudioTime(hwNow()) - a1) - (hwNow() - g1)) < 1e-9); /* an audio second a game second again */
    CHECK(fabs((ticks - t0) - 240 * (hwNow() - g1)) <= 1);
    CHECK(evenTicks(before > 0 ? before - 1 : 0));
    hwSetGameSpeed(9);
    CHECK(hwGameSpeed() == HW_GAME_SPEED_MAX);
    hwSetGameSpeed(1);

    /* The controller, on at 4x. */
    battleSpeedSetChosen(4);
    CHECK(battleSpeedChosen() == 4);
    frames(8, 0, 0, &fourx, &shown); /* the first frame's vblank sets the factor */
    CHECK(fourx == 8);
    frames(400, 0, 0, &fourx, &shown);
    CHECK(fourx == 400 && shown == 100); /* one picture in four */
    /* Waiting for the player: 1x, every picture. */
    frames(100, 1, 0, &fourx, &shown);
    CHECK(fourx == 0 && shown == 100);
    /* A new name: TEXT_HOLD frames at 1x, then 4x with the name still up. */
    frames(TEXT_HOLD, 0, 0x4, &fourx, &shown);
    CHECK(fourx == 0);
    frames(100, 0, 0x4, &fourx, &shown);
    CHECK(fourx == 100);
    /* Another name besides it: held again (TEXT_HOLD from its start); the
     * first one going away holds nothing. */
    frames(10, 0, 0x6, &fourx, &shown);
    CHECK(fourx == 0);
    frames(TEXT_HOLD - 10, 0, 0x2, &fourx, &shown);
    CHECK(fourx == 0);
    frames(100, 0, 0x2, &fourx, &shown);
    CHECK(fourx == 100);
    /* CD audio streaming: 1x until CD_AUDIO_HOLD vblanks after it stops. */
    cdQueued = 100;
    frames(50, 0, 0x2, &fourx, &shown);
    CHECK(fourx == 0);
    cdQueued = 0;
    frames(CD_AUDIO_HOLD, 0, 0x2, &fourx, &shown);
    CHECK(fourx == 0);
    frames(50, 0, 0x2, &fourx, &shown);
    CHECK(fourx == 50);
    /* The battle's frames stop (its end, a load): 1x once BATTLE_GAP vblanks pass. */
    vblanks(BATTLE_GAP + 1);
    CHECK(hwGameSpeed() == 1 && !battleSpeedInBattle() && !battleSpeedHidden());
    CHECK(battleSpeedStep() == 0); /* outside a battle the control does nothing */

    /* The control steps 4 -> 1 -> 2 -> 4, from the key and from the pad's
     * chord, Select+R2 by default: it acts when the chord completes (all of
     * it held, one of it down now), in either order or together. */
    frames(2, 0, 0, &fourx, &shown);
    CHECK(battleSpeedStep() == 1 && battleSpeedChosen() == 1);
    CHECK(battleSpeedKey(SDL_SCANCODE_TAB, 0) == 1 && battleSpeedChosen() == 2);
    CHECK(battleSpeedKey(SDL_SCANCODE_TAB, SDL_KMOD_LSHIFT) == 0 && battleSpeedChosen() == 2);
    battleSpeedPad(0x0002); /* R2 alone: nothing */
    CHECK(battleSpeedChosen() == 2);
    battleSpeedPad(0);
    battleSpeedPad(0x0100); /* Select alone: nothing */
    CHECK(battleSpeedChosen() == 2);
    battleSpeedPad(0x0102); /* Select held, then R2: the chord */
    CHECK(battleSpeedChosen() == 4);
    battleSpeedPad(0x0102); /* held on: once */
    CHECK(battleSpeedChosen() == 4);
    battleSpeedPad(0x0100); /* R2 let go and pressed again under Select: again */
    battleSpeedPad(0x0102);
    CHECK(battleSpeedChosen() == 1);
    battleSpeedPad(0);
    battleSpeedPad(0x0002); /* R2 held, then Select */
    battleSpeedPad(0x0102);
    CHECK(battleSpeedChosen() == 2);
    battleSpeedPad(0);
    battleSpeedPad(0x0102); /* both at once */
    CHECK(battleSpeedChosen() == 4);
    battleSpeedPad(0);
    battleSpeedPad(0x0102 | 0x0800 | 0x000C); /* inside the soft reset's L1+R1+Select+Start: not the control */
    CHECK(battleSpeedChosen() == 4);
    battleSpeedPad(0);
    battleSpeedPad(0x0101); /* rewind's Select+L2: not this chord */
    CHECK(battleSpeedChosen() == 4);
    battleSpeedPad(0);
    frames(4, 0, 0, &fourx, &shown);
    CHECK(hwGameSpeed() == 4);

    /* battle_speed_button: a button, a chord in either order, none; a name it
     * does not know is the default chord. */
    CHECK(parseButton(NULL) == 0x0102 && parseButton("") == 0x0102);
    CHECK(parseButton("select+r2") == 0x0102 && parseButton("R2+Select") == 0x0102);
    CHECK(parseButton("r2") == 0x0002 && parseButton("l1+r1") == 0x000C && parseButton(" select + l2 ") == 0x0101);
    CHECK(parseButton("none") == 0 && parseButton("triangle") == 0x0102 && parseButton("select+") == 0x0102);
    settingsSet("battle_speed_button", "r1");
    battleSpeedPad(0x0102); /* the default chord no longer acts */
    CHECK(battleSpeedChosen() == 4);
    battleSpeedPad(0x0008);
    CHECK(battleSpeedChosen() == 1);
    battleSpeedPad(0);
    settingsSet("battle_speed_button", "none");
    battleSpeedPad(0x010F);
    CHECK(battleSpeedChosen() == 1);
    battleSpeedPad(0);
    settingsUnset("battle_speed_button");
    battleSpeedSetChosen(4);

    /* The enemy info window's Select (gameplay.c): a lone Select toggles it on
     * its release; with a shoulder button it is a chord and toggles nothing. */
    int chord = 0;
    CHECK(!pcGameplaySelectTap(0, 0x0100, &chord) && pcGameplaySelectTap(0x0100, 0, &chord));
    CHECK(!pcGameplaySelectTap(0, 0x0100, &chord) && !pcGameplaySelectTap(0x0100, 0x0102, &chord) &&
          !pcGameplaySelectTap(0x0102, 0x0100, &chord) && !pcGameplaySelectTap(0x0100, 0, &chord));
    CHECK(!pcGameplaySelectTap(0, 0x0002, &chord) && !pcGameplaySelectTap(0x0002, 0x0102, &chord) &&
          !pcGameplaySelectTap(0x0102, 0x0002, &chord) && !pcGameplaySelectTap(0x0002, 0, &chord));
    CHECK(!pcGameplaySelectTap(0, 0x0101, &chord) && !pcGameplaySelectTap(0x0101, 0, &chord)); /* rewind's */
    CHECK(!pcGameplaySelectTap(0, 0x0100, &chord) && pcGameplaySelectTap(0x0100, 0, &chord)); /* lone again */
    CHECK(!pcGameplaySelectTap(0, 0x0140, &chord) && pcGameplaySelectTap(0x0140, 0x0040, &chord)); /* a face button is no chord */

    /* Off again: 1x at the next vblank. */
    battleSpeedSetChosen(0);
    frames(4, 0, 0, &fourx, &shown);
    CHECK(fourx == 0 && hwGameSpeed() == 1);

    /* The real clock: at 4x the game's time runs four times real time. */
    double was = hwNow();
    stepClock = 0;
    startNs = SDL_GetTicksNS();
    startAt = was;
    hwSetGameSpeed(4);
    uint64_t r0 = SDL_GetTicksNS();
    double h0 = hwNow();
    SDL_DelayNS(50000000);
    double ratio = (hwNow() - h0) / ((double)(SDL_GetTicksNS() - r0) * 1e-9);
    CHECK(fabs(ratio - 4) < 0.02);
    hwSetGameSpeed(1);
    r0 = SDL_GetTicksNS();
    h0 = hwNow();
    SDL_DelayNS(20000000);
    ratio = (hwNow() - h0) / ((double)(SDL_GetTicksNS() - r0) * 1e-9);
    CHECK(fabs(ratio - 1) < 0.02);

    SDL_Quit();
    printf("Battle speed: %d checks passed (%d sound ticks)\n", checks, ticks);
    return 0;
}
