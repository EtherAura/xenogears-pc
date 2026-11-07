/*
 * Validation switch for the wide battle effects (include/pc_wide.h): runs
 * screen effects the debug selector's battles never reach.
 *
 * XG_BATTLE_WIDETEST=<effect>@<vblank>[,...]: at the first battle frame at or
 * after that vblank, start the effect as its battle opcode does:
 *   shatter   opcode 0x18: the screen breaks into flying triangles
 *   ripple    opcode 0x54: the screen ripples and flies off (the overlay's
 *             own loop)
 *   warp      the circle warp FX (battle_fx_circle_warp)
 *   win       the enemies' HP (and gear HP) down to 1, so the battle ends
 *             soon and the result screen shows (as XG_BATTLE_POKE, sweep.c)
 * XG_BATTLE_HUDLOG=1 (with XG_SWEEP_LOG): one "H" line per battle frame gives
 * the HUD's state (port/src/system/sweeplog.c): battle mode, acting entity,
 * open menu, then flags (command ring, menu glyphs BA8/27C8/1E68, combo
 * input, AP counter, target cursor, gear fuel, gear HUD) and the dialog
 * windows as bits, to find frames that show a given part of it.
 * The game's random numbers and state are not otherwise touched, so a 4:3
 * run and a wide run of the same pad script stay comparable
 * (tools/widecheck.py).
 */
#include "common.h"
#include "battle/battle.h"

const char* portOption(const char* name);
int portSweepEnabled(void);
void portSweepLog(const char* tag, const int* values, int count);
extern u8 currentBattleMode;
void battleSpriteEffect_18(SpriteActorCore* actor, u8* args);
void playFxScreenRipple(void);
void createFxCircleWarp(void);

#define WIDETEST_MAX 8

static s32 loaded;
static s32 hudLog;
static s32 count;
static struct {
    char name[12];
    s32 at;
} tests[WIDETEST_MAX];

static void parse(void) {
    const char* s = portOption("XG_BATTLE_WIDETEST");
    s32 n, v;

    loaded = 1;
    hudLog = portOption("XG_BATTLE_HUDLOG") != NULL;
    while (s != NULL && *s != '\0' && count < WIDETEST_MAX) {
        n = 0;
        while (*s != '\0' && *s != '@' && *s != ',' && n < (s32)sizeof(tests[0].name) - 1) {
            tests[count].name[n++] = *s++;
        }
        tests[count].name[n] = '\0';
        v = -1;
        if (*s == '@') {
            s++;
            v = 0;
            while (*s >= '0' && *s <= '9') {
                v = v * 10 + (*s++ - '0');
            }
        }
        tests[count++].at = v;
        while (*s != '\0' && *s != ',') {
            s++;
        }
        if (*s == ',') {
            s++;
        }
    }
}

static s32 named(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* battleRender calls this first thing each battle frame. */
void portBattleWideTest(void) {
    s32 now, i;

    if (!loaded) {
        parse();
    }
    /* Nothing else without the switches: even VSync(-1) is a safe point that
     * may deliver an interrupt earlier than the PS1 build would. */
    if (count == 0 && !hudLog) {
        return;
    }
    now = VSync(-1);
    if (hudLog && portSweepEnabled() && battleVar1 != NULL) {
        s32 v[5];

        v[0] = currentBattleMode;
        v[1] = battleBssBlock.mA72_currentEntity;
        v[2] = battleVar1->mB7_openMenuType;
        v[3] = battleVar1->mCB_drawCommandRing | battleVar1->m9C_renderBA8 << 1 | battleVar1->m9E_render27C8 << 2 |
               battleVar1->m9D_render1E68 << 3 | battleVar1->mA8_drawComboInput << 4 |
               battleVar1->mAF_drawApCounter << 5 | battleVar1->mC6_isTargetSelectionCursorVisible << 6 |
               battleVar1->mC7_drawGearFuel << 7 | battleVar1->mAD_drawGearHud << 8;
        v[4] = 0;
        for (i = 0; i < 7; i++) {
            v[4] |= battleVar1->mB0_isDialogWindowInitialized[i] << i;
        }
        portSweepLog("H", v, 5);
    }
    if (count == 0) {
        return;
    }
    for (i = 0; i < count; i++) {
        if (tests[i].at < 0 || now < tests[i].at) {
            continue;
        }
        tests[i].at = -1;
        if (named(tests[i].name, "shatter")) {
            battleSpriteEffect_18(NULL, NULL);
        } else if (named(tests[i].name, "ripple")) {
            playFxScreenRipple();
        } else if (named(tests[i].name, "warp")) {
            createFxCircleWarp();
        } else if (named(tests[i].name, "win")) {
            s32 k;
            for (k = 3; k < 11; k++) {
                sBattleEntity* e = &battleEntities.m0_entities[k];
                if (e->m0_base.m4C_HP > 1) {
                    e->m0_base.m4C_HP = 1;
                }
                if (e->mA4_gear.m60_hp > 1) {
                    e->mA4_gear.m60_hp = 1;
                }
            }
        }
    }
}
