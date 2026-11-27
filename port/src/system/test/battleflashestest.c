/* Disable battle flashes: setting persistence, additive battle fader draw
 * suppression (b_800B15D8.c), and glass-shatter initial/fade brightness
 * (b_800B6F0C.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "battle/battle.h"
#include "pc_gameplay.h"
#include "system/gameplay.h"
#include "test/env.h"

static int checks;
#include "test/check.h"

#ifndef _WIN32
/* Symbols referenced by battleFader_draw (b_800B15D8.c) and
 * initBattleStartEffect / updateBattleStartEffect (b_800B6F0C.c). */
u8* shapeTransfertTableCurrentEntry;
u8* shapeTransfertTableEnd;
OTag* characterRenderingOT;
BattleBss_800C3EB0 battleBssBlock;
VECTOR effectGravity;

void SetDrawMode(DR_MODE* p, int dfe, int dtd, int tpage, RECT* tw) {
    (void)dfe; (void)dtd; (void)tw;
    memset(p, 0, sizeof(*p));
    p->code[0] = (u32)tpage;
}
void SetPolyF4(void* p) { memset(p, 0, sizeof(POLY_F4)); }
void SetPolyFT3(void* p) { memset(p, 0, sizeof(POLY_FT3)); }
void SetSemiTrans(void* p, int abe) { (void)p; (void)abe; }
void SetShadeTex(void* p, int tge) { (void)p; (void)tge; }
u16 GetTPage(int tp, int abr, int x, int y) { (void)tp; (void)x; (void)y; return (u16)abr; }
void AddPrim(void* ot, void* p) { (void)ot; (void)p; }
s32 SquareRoot0(s32 a) { return a > 0 ? 1 : 0; }
void Square0(VECTOR* v0, VECTOR* v1) { *v1 = *v0; }
s32 ratan2(s32 y, s32 x) { (void)y; (void)x; return 0; }
MATRIX* RotMatrixZYX(SVECTOR* r, MATRIX* m) { (void)r; memset(m, 0, sizeof(*m)); return m; }
VECTOR* ApplyMatrixLV(MATRIX* m, VECTOR* v0, VECTOR* v1) { (void)m; *v1 = *v0; return v1; }
void createRotationMatrix(SVECTOR* rot, MATRIX* m) { (void)rot; memset(m, 0, sizeof(*m)); }
s32 pcWideSqrt(s32 x) { return x > 0 ? 1 : 0; }
s32 pcWideAtan2(s32 y, s32 x) { (void)y; (void)x; return 0; }
s32 addClamp255(s32 value, s32 delta) {
    s32 s = value + delta;
    return s < 0 ? 0 : s > 255 ? 255 : s;
}

void battleFader_draw(TaskHeader* task);
sBattleStartEffect* initBattleStartEffect(sBattleStartEffect* e);
void updateBattleStartEffect(TaskHeader* task);

static int drawFader(u8 abe) {
    static u8 scratch[256];
    static OTag ot[4];
    sBattleFader fader;

    memset(scratch, 0, sizeof(scratch));
    memset(ot, 0, sizeof(ot));
    memset(&fader, 0, sizeof(fader));
    fader.m1C_drawTask.m4_data = &fader;
    fader.m41_tpageABE = abe;
    fader.m48_currentColor[0] = 0xFF;
    fader.m48_currentColor[1] = 0xCC;
    fader.m48_currentColor[2] = 0x88;

    shapeTransfertTableCurrentEntry = scratch;
    shapeTransfertTableEnd = scratch + sizeof(scratch);
    characterRenderingOT = ot;
    battleFader_draw(&fader.m1C_drawTask);
    return shapeTransfertTableCurrentEntry > scratch;
}

static int behavior(void) {
    static sBattleStartEffect effect;

    /* Off (original): every fader mode draws, and shatter shards start at 0xFF
     * and dim by -6 per update. */
    testSetEnv("XG_DISABLE_BATTLE_FLASHES", "0");
    CHECK(drawFader(0));
    CHECK(drawFader(1));
    CHECK(drawFader(2));
    CHECK(drawFader(3));

    memset(&effect, 0, sizeof(effect));
    effect.m0_updateTask.m4_data = &effect;
    battleBssBlock.m8C84_oddOrEven = 0;
    initBattleStartEffect(&effect);
    CHECK(effect.m3C_triangleData[0][0][0].m24_poly[0].r0 == 0xFF);
    CHECK(effect.m3C_triangleData[0][0][0].m24_poly[0].g0 == 0xFF);
    CHECK(effect.m3C_triangleData[0][0][0].m24_poly[0].b0 == 0xFF);
    effect.m3C_triangleData[0][0][0].m64_delay = 0;
    updateBattleStartEffect(&effect.m0_updateTask);
    CHECK(effect.m3C_triangleData[0][0][0].m24_poly[0].r0 == 0xF9);

    /* On: additive flashes (abe 1 and 3) are skipped; subtractive fades
     * (abe 2) still draw; shatter shards start at normal brightness (0x80)
     * and dim by -3 per update. */
    testSetEnv("XG_DISABLE_BATTLE_FLASHES", "1");
    CHECK(!drawFader(1));
    CHECK(!drawFader(3));
    CHECK(drawFader(2));
    CHECK(drawFader(0));

    memset(&effect, 0, sizeof(effect));
    effect.m0_updateTask.m4_data = &effect;
    battleBssBlock.m8C84_oddOrEven = 0;
    initBattleStartEffect(&effect);
    CHECK(effect.m3C_triangleData[0][0][0].m24_poly[0].r0 == 0x80);
    CHECK(effect.m3C_triangleData[0][0][0].m24_poly[0].g0 == 0x80);
    CHECK(effect.m3C_triangleData[0][0][0].m24_poly[0].b0 == 0x80);
    effect.m3C_triangleData[0][0][0].m64_delay = 0;
    updateBattleStartEffect(&effect.m0_updateTask);
    CHECK(effect.m3C_triangleData[0][0][0].m24_poly[0].r0 == 0x7D);
    return 0;
}
#endif

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char path[1200];
    snprintf(path, sizeof(path), "%s/battleflashes.ini", argv[1]);
    testSetEnv("XG_SETTINGS", path);
    if (!strcmp(argv[2], "write")) {
        CHECK(!pcGameplayDisableBattleFlashes());
        pcGameplaySetDisableBattleFlashes(1);
        CHECK(pcGameplayDisableBattleFlashes());
    } else if (!strcmp(argv[2], "read")) {
        CHECK(pcGameplayDisableBattleFlashes());
    } else {
        CHECK(!pcGameplayDisableBattleFlashes()); /* off by default */
        testSetEnv("XG_DISABLE_BATTLE_FLASHES", "on");
        CHECK(pcGameplayDisableBattleFlashes());
        testSetEnv("XG_DISABLE_BATTLE_FLASHES", "0");
        CHECK(!pcGameplayDisableBattleFlashes());
#ifndef _WIN32
        if (behavior()) return 1;
#endif
    }
    printf("disable battle flashes %s: %d checks passed\n", argv[2], checks);
    return 0;
}
