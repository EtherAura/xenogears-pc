/*
 * Vibration from the game's events (rumble.c), read from the game as it
 * runs; nothing here writes its state.
 *
 * The enhanced mode (vibration=enhanced) authors, for pad 1 (levels of the
 * large and small motors, 0..1, before vibration_strength; vblanks):
 *   hit taken      a party member's HP damage as its number appears
 *                  (createDamageDisplayIfValid, src/battle/b_800BD1FC.c):
 *                  large 0.35..0.8, small 0.25..0.45 by the damage's share of
 *                  the max HP (a third or more: full), held 3, decaying 10
 *   knocked out    the first such hit with the member's HP at 0: large 0.9,
 *                  small 0.5, 6 + 18 (once until its HP is back)
 *   hit dealt      an enemy's HP damage: a tap, small 0.35..0.7, large
 *                  0.1..0.25 (half its max HP or more: full), 2 + 6
 *   gear impacts   either on a gear: heavier and longer, large 0.6..1.0 and
 *                  small 0.35..0.6 taken (4 + 14), large 0.4..0.7 and small
 *                  0.3..0.5 dealt (3 + 12)
 *   deathblow      a deathblow or gear combo set off (battleVar2
 *                  ->m2E4_deathblowCompleted becoming non-zero): a build-up,
 *                  large 0.45 and small 0.75 over 8, held 4, decaying 10
 *   camera shakes  the battle's (its shake task's current offset,
 *                  startBattleCameraShake: explosions, big blows) and a
 *                  field's (fieldSceneState's shake amplitude, script op
 *                  OP_SETUP_CAMERA_SHAKE: earthquakes, impacts), as long as
 *                  they last: large up to 0.85, small up to 0.35, full at
 *                  an offset of 12 (battle) or an amplitude of 28 (field);
 *                  measured in the opening (New Game to Fei's room): field
 *                  amplitudes 8 to 32, the gear fight's offsets up to 8
 * Misses, heals and MP changes do not vibrate. The arena's own vibration
 * (the game's motor) is in both modes (rumble.c).
 *
 * The arena offers its Vibration option only for an analog pad (type 7xh,
 * a DualShock in analog mode); the port's pad is digital, so with vibration
 * on and a pad with motors on that port it is offered as with a DualShock
 * (pcRumbleOffered, src/battling/bl_8007E528.c PLATFORM_PC).
 */

#include "common.h"
#include "battle/battle.h"
#include "field/field.h"
#include "main/battle.h"
#include "pc_wide.h"
#include "rumble.h"
#include "savestate.h"

extern BattleShake* battleCameraShakeTask;
extern FieldBss_800AF880 fieldSceneState;

static u8 knockedOut[3];
static int deathblowWas;

static float clamp01(float x) {
    return x < 0 ? 0 : x > 1 ? 1 : x;
}

/* The game shows HP damage (type 0, 5, 7 or 8) of value on an entity. */
void pcRumbleBattleDamage(int entity, int type, int value) {
    if (rumbleMode() != RUMBLE_ENHANCED || entity < 0 || entity >= 11 || value <= 0 ||
        !(type == 0 || type == 5 || type == 7 || type == 8)) {
        return;
    }
    const sBattleEntity* e = &battleEntities.m0_entities[entity];
    int gear = battleBssBlock.m4_visualEntities[entity].m4_isGear != 0;
    int hp = gear ? e->mA4_gear.m60_hp : e->m0_base.m4C_HP;
    int maxHp = gear ? e->mA4_gear.m64_maxHp : e->m0_base.m4E_MaxHP;
    float share = maxHp > 0 ? (float)value / (float)maxHp : 0.5f;
    if (entity < 3) {
        float f = clamp01(share * 3);
        if (gear) {
            rumbleAdd(0.6f + 0.4f * f, 0.35f + 0.25f * f, 0, 4, 14, "gear hit taken");
        } else {
            rumbleAdd(0.35f + 0.45f * f, 0.25f + 0.2f * f, 0, 3, 10, "hit taken");
        }
        if (hp > 0) {
            knockedOut[entity] = 0;
        } else if (!knockedOut[entity]) {
            knockedOut[entity] = 1;
            rumbleAdd(0.9f, 0.5f, 0, 6, 18, "knocked out");
        }
    } else {
        float f = clamp01(share * 2);
        if (gear) {
            rumbleAdd(0.4f + 0.3f * f, 0.3f + 0.2f * f, 0, 3, 12, "gear hit dealt");
        } else {
            rumbleAdd(0.1f + 0.15f * f, 0.35f + 0.35f * f, 0, 2, 6, "hit dealt");
        }
    }
}

/* The arena's Vibration option for a pad with motors (see the header). */
int pcRumbleOffered(int port) {
    return rumbleOffered(port);
}

static int absMax3(int a, int b, int c) {
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    c = c < 0 ? -c : c;
    return a > b ? (a > c ? a : c) : (b > c ? b : c);
}

void rumbleGamePoll(float* large, float* small) {
    static int shown = -1;
    int mode = pcWideGetMode(), amp = 0;
    float shake = 0;
    if (mode == PC_WIDE_BATTLE && battleVar2 != NULL) {
        int deathblow = battleVar2->m2E4_deathblowCompleted != 0;
        if (deathblow && !deathblowWas) {
            rumbleAdd(0.45f, 0.75f, 8, 4, 10, "deathblow");
        }
        deathblowWas = deathblow;
        if (battleCameraShakeTask != NULL) {
            const BattleShake* s = battleCameraShakeTask;
            amp = absMax3(s->m1C_current.vx, s->m1C_current.vy, s->m1C_current.vz);
            shake = clamp01((float)amp / 12.0f); /* measured: up to 8 in the opening's gear fight */
        }
    } else {
        deathblowWas = 0;
        knockedOut[0] = knockedOut[1] = knockedOut[2] = 0;
        if (mode == PC_WIDE_FIELD && fieldSceneState.m1A8_cameraShakeEnabled) {
            const s32* a = fieldSceneState.m1B0_cameraShakeAmplitude;
            amp = absMax3(a[0] >> 16, a[1] >> 16, a[2] >> 16);
            shake = clamp01((float)amp / 28.0f); /* measured: 8 to 32 in the opening's Lahan scenes */
        }
    }
    *large = 0.85f * shake;
    *small = 0.35f * shake;
    if (amp != shown) {
        rumbleLog("rumble: %s camera shake %d (%.0f%%)", mode == PC_WIDE_BATTLE ? "battle" : "field", amp, shake * 100);
        shown = amp;
    }
}

/* Save states (savestate.h): no motor keeps running across a load, and the
 * port's own feedback starts from the restored battle. */
void rumbleGameStateLoaded(void) {
    __builtin_memset(knockedOut, 0, sizeof(knockedOut));
    deathblowWas = 0;
    rumbleStopAll();
}
