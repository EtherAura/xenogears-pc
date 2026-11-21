/* Exercise native equipment, save and battle paths for original-game fixes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu/menu.h"
#include "main/game_state.h"
#include "main/sound.h"
#include "battle/battle.h"
#include "battle_loader/battle_loader.h"
#include "pc_gameplay.h"
#include "system/gameplay.h"
#include "test/env.h"

GameState gameState;
u8 gearShopGearPilotIds[20];
u8 gearPilotIds[20];
BattleBss_800CCCE8 battleEntities;
BattleBss_800CCCE8* pBattleEntities = &battleEntities;
u8 battleCharacters[4];
u8 battlePartyMemberCount, startBattleAttackAnimationVar2, startBattleAttackAnimationVar4;
u8 equipCandidateIds[256];
sBattleEntity* battleGetSlotStatusSub_currentBattleEntity;
GameStateGear* battleGetSlotStatusSub_currentBattleEntityGear;
sBattleEntity* startBattleAttackAnimationVar5;
sBattle800CDD40Sub* currentEntityBattleStats;
GameStateGear* startBattleAttackAnimationVar6;
MenuContext menuContext;
MenuContext* gMenuContext = &menuContext;
u16 computeGearBattleDamageSub1(void);
s16 computeBattleDamageSub1(void);
void computeBattleDamageSub3(u16*, u16*, s8*);
void spendBillyAmmo(void);
void initPartyBattleStats(void);
void backupEquipment(u8, u8);
void restoreEquipment(u8, u8);
void setEquipmentSlotItem(u8, s32, s32, s32, u8, u8);
void applyAllGearPartStats(MenuContext_330*, u8);
void computeGearStatusStats(MenuContext_330*, u8);
static MenuContext_330 context;
static MenuGearAccessory accessories[4];
static MenuGearFrame frame;
static MenuGearEngine engine;
static MenuGearArmor armor;
static MenuGearWeapon weapon;
static MenuSaveGame nativeSave, fixedSave, loadedSave;
static sBattle800CDD40Sub attack;
static int checks;
#include "test/check.h"

#ifndef _WIN32
sBattleConfig currentBattleConfig;
u8 battleCameraVar0;
s32 battleCameraMode, battleCameraModeInterpolation;
SVECTOR battleCameraEye, battleCameraAt;
BattleCamera_800D309C battleCameraBlock;
FP_VEC3 previousCameraEye2, previousCameraAt2;
SpriteTask* battleCameraModeCallback1;
SpriteTask* battleCameraModeCallback2;
void setBattleCameraMode4IfAllowed(void);

static int testDeusCamera(void) {
    /* Camera state at the reproduced side-target stall, from the native run.
     * The effect's camera wait cannot finish once mode 4 stops interpolation. */
    const SVECTOR eye = {1347, -1049, 963, 123};
    const SVECTOR at = {1347, -430, 2991, 456};
    const SVECTOR eyeTarget = {1340, -1042, 984, 789};
    const SVECTOR atTarget = {1340, -430, 3000, 321};
    const char* const overrides[] = {"", "off", "on"};
    for (size_t setting = 0; setting < sizeof(overrides) / sizeof(overrides[0]); ++setting)
    for (int scenario = 0; scenario < 8; ++scenario) {
        int enabled = setting != 1;
        testSetEnv("XG_DEUS_CAMERA_FIX", overrides[setting]);
        testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "off" : "on");
        memset(&currentBattleConfig, 0, sizeof(currentBattleConfig));
        currentBattleConfig.m0_battleId = scenario == 1 ? 0x41 : 0x40;
        currentBattleConfig.m2_arenaId = scenario == 2 ? 0x32 : 0x33;
        battleCameraVar0 = scenario == 5;
        battleCameraMode = scenario == 3 ? 2 : scenario == 7 ? 4 : 1;
        battleCameraModeInterpolation = scenario == 4 ? 5 : 1;
        battleCameraEye = eye; battleCameraAt = at;
        battleCameraBlock.m4_eyeTarget = eyeTarget; battleCameraBlock.mC_atTarget = atTarget;
        if (scenario == 6) { /* An already arrived camera needs no change. */
            battleCameraEye.vx = eyeTarget.vx; battleCameraEye.vy = eyeTarget.vy;
            battleCameraEye.vz = eyeTarget.vz;
            battleCameraAt.vx = atTarget.vx; battleCameraAt.vy = atTarget.vy;
            battleCameraAt.vz = atTarget.vz;
        }
        SVECTOR expectedEye = battleCameraEye, expectedAt = battleCameraAt;
        int mode = battleCameraMode, interpolation = battleCameraModeInterpolation;
        sBattleConfig config = currentBattleConfig;
        BattleBss_800CCCE8 entities = battleEntities;
        if (enabled && scenario == 0) {
            expectedEye.vx = eyeTarget.vx; expectedEye.vy = eyeTarget.vy; expectedEye.vz = eyeTarget.vz;
            expectedAt.vx = atTarget.vx; expectedAt.vy = atTarget.vy; expectedAt.vz = atTarget.vz;
        }
        setBattleCameraMode4IfAllowed();
        CHECK(battleCameraMode == (battleCameraVar0 ? mode : 4));
        CHECK(battleCameraModeInterpolation == (battleCameraVar0 ? interpolation : 5));
        CHECK(!memcmp(&battleCameraEye, &expectedEye, sizeof(expectedEye)));
        CHECK(!memcmp(&battleCameraAt, &expectedAt, sizeof(expectedAt)));
        CHECK(!memcmp(&battleCameraBlock.m4_eyeTarget, &eyeTarget, sizeof(eyeTarget)));
        CHECK(!memcmp(&battleCameraBlock.mC_atTarget, &atTarget, sizeof(atTarget)));
        CHECK(!memcmp(&currentBattleConfig, &config, sizeof(config)));
        CHECK(!memcmp(&battleEntities, &entities, sizeof(entities)));
    }
    return 0;
}

static int testBilly(void) {
    /* Element masks and ammo IDs verified independently on both retail discs. */
    static const u8 elements[] = {8, 4, 2, 1, 32, 16};
    MenuWeaponStats weapons[100] = {0};
    MenuAccessoryStats characterAccessory = {0};
    MenuContext_360 equipment = {0};
    MenuContext_33C drawFlags = {0};
    context.m0_weaponStats = weapons; context.m4_accessoryData = &characterAccessory;
    menuContext.m330_itemData = &context; menuContext.m360_equipment = &equipment;
    menuContext.m33C_drawFlags = &drawFlags; drawFlags.m30_partyMemberId[0] = 4;
    weapons[50].mC_power = 30; weapons[57].mC_power = 50;
    weapons[50].m8_statusMask = 0x1234; weapons[50].mA_statusChance = 13;
    weapons[57].m8_statusMask = 0x4321; weapons[57].mA_statusChance = 17;
    for (int i = 0; i < 6; ++i) weapons[67 + i].mC_power = elements[i];
    for (int loadout = 0; loadout < 11; ++loadout) {
        for (int first = 0; first < 6; ++first) for (int second = 0; second < 6; ++second) {
            memset(&gameState, 0, sizeof(gameState));
            GameStateCharacter* billy = &gameState.m26C_party[4];
            billy->m56_battleCommandLoadout = loadout;
            billy->m6F_equipment[0] = 50; billy->m6F_equipment[3] = 57;
            billy->m6F_equipment[1] = 67 + first; billy->m6F_equipment[2] = 67 + second;
            billy->m4_weaponSlots[1].m0_weaponPower = 4; billy->m4_weaponSlots[2].m0_weaponPower = 1;
            billy->m58_attack = 35; billy->m5B_ether = 50;
            GameState before = gameState;
            testSetEnv("XG_ORIGINAL_BUG_FIXES", "off");
            setupStatusMenuForCharacter1(&context, 4);
            CHECK(billy->m4_weaponSlots[1].m0_weaponPower == 4 && billy->m4_weaponSlots[2].m0_weaponPower == 1);
            GameState native = gameState;
            gameState = before;
            testSetEnv("XG_ORIGINAL_BUG_FIXES", "on");
            for (int repeat = 0; repeat < 3; ++repeat) {
                setupStatusMenuForCharacter1(&context, 4);
                CHECK(billy->m4_weaponSlots[1].m0_weaponPower == (loadout == 4 ? elements[first] : 4));
                CHECK(billy->m4_weaponSlots[2].m0_weaponPower == (loadout == 4 ? elements[second] : 1));
                GameState comparison = gameState;
                comparison.m26C_party[4].m4_weaponSlots[1].m0_weaponPower = 4;
                comparison.m26C_party[4].m4_weaponSlots[2].m0_weaponPower = 1;
                CHECK(!memcmp(&comparison, &native, sizeof(native)));
            }
            if (loadout != 4) continue;
            /* Real equipment preview/cancel restores the elements on refresh. */
            for (int slot = 1; slot <= 2; ++slot) {
                int selected = slot == 1 ? first : second;
                backupEquipment(0, 0);
                equipCandidateIds[0] = 67 + (selected + 1) % 6;
                setEquipmentSlotItem(0, slot, 0, 0, 1, 0);
                setupStatusMenuForCharacter1(&context, 4);
                CHECK(billy->m4_weaponSlots[slot].m0_weaponPower == elements[(selected + 1) % 6]);
                CHECK(billy->m4_weaponSlots[3 - slot].m0_weaponPower == elements[slot == 1 ? second : first]);
                restoreEquipment(0, 0); setupStatusMenuForCharacter1(&context, 4);
                CHECK(billy->m6F_equipment[slot] == 67 + selected && billy->m4_weaponSlots[slot].m0_weaponPower == elements[selected]);
            }
            buildSaveGameData(&fixedSave);
            memset(billy, 0, sizeof(*billy)); restoreSaveGameData(&fixedSave, &context);
            CHECK(!memcmp(billy, &fixedSave.m290_party[4], sizeof(*billy)));
            buildSaveGameData(&loadedSave);
            CHECK(!memcmp(&fixedSave, &loadedSave, sizeof(fixedSave)));
            /* Off keeps the retail refresh semantics, including already saved masks. */
            testSetEnv("XG_ORIGINAL_BUG_FIXES", "off");
            setupStatusMenuForCharacter1(&context, 4);
            CHECK(billy->m4_weaponSlots[1].m0_weaponPower == elements[first] && billy->m4_weaponSlots[2].m0_weaponPower == elements[second]);
            testSetEnv("XG_ORIGINAL_BUG_FIXES", "on");
            memset(&battleEntities, 0, sizeof(battleEntities));
            battleGetSlotStatusSub_currentBattleEntity = &battleEntities.m0_entities[0];
            battleGetSlotStatusSub_currentBattleEntity->m0_base = *billy;
            battleGetSlotStatusSub_currentBattleEntityGear = &battleEntities.m0_entities[0].mA4_gear;
            startBattleAttackAnimationVar2 = 0; startBattleAttackAnimationVar4 = 3;
            startBattleAttackAnimationVar5 = &battleEntities.m0_entities[3];
            startBattleAttackAnimationVar6 = &battleEntities.m0_entities[3].mA4_gear;
            currentEntityBattleStats = &attack;
            for (int slot = 1; slot <= 2; ++slot) {
                memset(&attack, 0, sizeof(attack));
                attack.mA_attackFlags = 0x100; attack.m1A_attackFormula = 2; attack.m11_power = 20;
                attack.m22_elementFlags = billy->m4_weaponSlots[slot].m0_weaponPower;
                startBattleAttackAnimationVar5->m0_base.m38_weaknessFlags = 0;
                CHECK(computeBattleDamageSub1() == 50);
                battleGetSlotStatusSub_currentBattleEntity->m0_base.m5B_ether = 80;
                CHECK(computeBattleDamageSub1() == 80);
                battleGetSlotStatusSub_currentBattleEntity->m0_base.m5B_ether = 50;
                for (int weak = 0; weak < 6; ++weak) {
                    u16 power = 100, defence = 40; s8 result = 0;
                    startBattleAttackAnimationVar5->m0_base.m38_weaknessFlags = elements[weak];
                    computeBattleDamageSub3(&power, &defence, &result);
                    CHECK(power == (elements[weak] == attack.m22_elementFlags ? 150 : 100));
                    CHECK(defence == 40 && result == 0);
                }
                startBattleAttackAnimationVar5->m0_base.m38_weaknessFlags = 0;
                /* Native armour resistance/absorption covers the four basic elements. */
                if (attack.m22_elementFlags <= 8) for (int mode = 0; mode < 3; ++mode) {
                    u16 power = 100, defence = 40; s8 result = 0;
                    startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[4] =
                        (attack.m22_elementFlags << 8) | (mode == 1 ? 4 : mode == 2 ? 8 : 0);
                    computeBattleDamageSub3(&power, &defence, &result);
                    CHECK(power == (mode == 1 ? 40 : 70));
                    CHECK(defence == 40 && result == (mode == 2 ? 4 : 0));
                }
                startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[4] = 0;
            }
            /* Changing the two element bytes leaves gun damage and ammo spending native. */
            memset(&attack, 0, sizeof(attack));
            attack.m1A_attackFormula = 2; attack.m11_power = 20; attack.m10_weaponMask = 0x90;
            CHECK(computeBattleDamageSub1() == 83);
            for (int enabled = 0; enabled < 2; ++enabled)
            for (int command = 0; command < 20; ++command) for (int empty = 0; empty < 2; ++empty) {
                testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
                memset(&gameState.m22B8_ammoCounts, 0, sizeof(gameState.m22B8_ammoCounts));
                gameStateAmmoByWeaponItem[50] = gameStateAmmoByWeaponItem[57] = empty ? 0 : 7;
                pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index = command;
                spendBillyAmmo();
                CHECK(gameStateAmmoByWeaponItem[50] == (empty ? 0 : command < 4 || command >= 7 ? 6 : 7));
                CHECK(gameStateAmmoByWeaponItem[57] == (empty ? 0 : command >= 6 ? 6 : 7));
            }
        }
    }
    {
        /* Def UP (m84[0] 0x4000, e.g. battle item 21) never raised physical
         * defence: retail tests 0x100. The option adds Def UP's half; the
         * 0x100 case, ether defence and Eth Def UP stay native. */
        s16 computeBattleDamageSub2(void);
        memset(&battleEntities, 0, sizeof(battleEntities));
        startBattleAttackAnimationVar4 = 3;
        startBattleAttackAnimationVar5 = &battleEntities.m0_entities[3];
        startBattleAttackAnimationVar6 = &battleEntities.m0_entities[3].mA4_gear;
        currentEntityBattleStats = &attack;
        startBattleAttackAnimationVar5->m0_base.m59_defence = 40;
        startBattleAttackAnimationVar5->m0_base.m2D_accessoryDefence = 10;
        startBattleAttackAnimationVar5->m0_base.m5C_etherDefence = 30;
        for (int enabled = 0; enabled < 2; ++enabled) {
            testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
            memset(&attack, 0, sizeof(attack));
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] = 0;
            CHECK(computeBattleDamageSub2() == 50);
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] = 0x4000;
            CHECK(computeBattleDamageSub2() == (enabled ? 70 : 50));
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[1] = 0x4000; /* its lock word too */
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] = 0;
            CHECK(computeBattleDamageSub2() == (enabled ? 70 : 50));
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[1] = 0;
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] = 0x100;
            CHECK(computeBattleDamageSub2() == 70);
            attack.m1B_defenceFormula = 2; /* accessory defence only */
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] = 0x4000;
            CHECK(computeBattleDamageSub2() == 10);
            attack.mA_attackFlags = 0x100; attack.m1B_defenceFormula = 1;
            CHECK(computeBattleDamageSub2() == 30);
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] = 0x4000;
            CHECK(computeBattleDamageSub2() == 45);
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] = 0;
        }
    }
    return 0;
}
#endif

#ifndef _WIN32
/* The item shop's cursor rows (retail menu_shop data: Exit, Sell, Buy, then
 * the submenu rows) and main's font call, which only records the arrow. */
s32 shopCursorPosXTable[7] = {73, 72, 67, 37, 36, 31, 24};
#if VERSION_JP
s32 shopCursorPosYTable[7] = {205, 185, 166, 205, 185, 166, 148};
#else
s32 shopCursorPosYTable[7] = {205, 185, 166, 201, 181, 162, 144};
#endif
static int arrowX, arrowY;
/* While the party card test records, every call's arguments, up to the buffer. */
typedef struct { s32 glyph; void* polys; void* font; s32 oddOrEven; s16 x, y; u16 scale; } LabelCall;
static int partyLabelRecording, partyLabelCalls;
static LabelCall partyLabelCall[16];
s32 setupStringInPolyFT4(struct Font* font, s32 glyphIndex, void* polys, s32 oddOrEven, s16 x, s16 y, u16 scale) {
    (void)font; (void)polys; (void)oddOrEven; (void)scale;
    if (glyphIndex == 0x108) { arrowX = x; arrowY = y; }
    if (partyLabelRecording) {
        if (partyLabelCalls < (int)(sizeof(partyLabelCall) / sizeof(partyLabelCall[0])))
            partyLabelCall[partyLabelCalls] = (LabelCall){glyphIndex, polys, font, oddOrEven, x, y, scale};
        ++partyLabelCalls;
    }
    return 1;
}
void updateCursorPolysLocation(s32 idx, u8 draw);

/* The US raised every menu's cursor 4 pixels (JP minus 4) but the shop's
 * Buy/Sell/Exit rows, whose arrow covered the label; the option raises them. */
static int testShopCursor(void) {
    static MenuContext_348 cursor;
    static MenuContext_33C flags;
    gMenuContext->m348_cursor = &cursor;
    gMenuContext->m33C_drawFlags = &flags;
    cursor.m15B_selectionWidth = 0x40;
    for (int enabled = 0; enabled < 2; ++enabled) {
        testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
        for (int idx = 0; idx < 7; ++idx) {
#if VERSION_JP
            int y = shopCursorPosYTable[idx];
#else
            int y = shopCursorPosYTable[idx] - (enabled && idx < 3 ? 4 : 0);
#endif
            updateCursorPolysLocation(idx, 1);
            CHECK(arrowX == shopCursorPosXTable[idx] && arrowY == y);
            CHECK(cursor.m50_polyG4[0].y0 == y - 0x24 && cursor.m50_polyG4[0].y2 == y - 0x14);
            CHECK(cursor.mC8_lines[0].y0 == y - 0x24 && cursor.mF8_lines[0].y2 == y - 0x14);
            CHECK(cursor.m50_polyG4[0].x0 == shopCursorPosXTable[idx] + 0x14);
        }
    }
    return 0;
}

/* The party menu's info card labels (menu_party setupPartyCardLabels): nine
 * glyph entries, 0xFFFF skipped, then the name. Entry 6 reads MP/ where every
 * other menu says EP; the option draws glyph 0x0E there. Synthetic tables. */
s32 partyMenuCardLabelGlyphs[9];
s32 partyMenuInfocardNameU[6], partyMenuInfocardNameV[6];
u16 textSpriteMode0, textSpriteMode1;
void setupPartyCardLabels(MenuContext_1E08* p, u8 slot, u8 row, s32* xTable, s32* yTable, s32 rowStep);

static int partyLabelsRun(const s32* glyphs) {
    static MenuContext_1E08 card;
    s32 xTable[17], yTable[17];
    for (int i = 0; i < 17; ++i) { xTable[i] = 100 + 7 * i; yTable[i] = 30 + 3 * i; }
    const u8 slot = 3, row = 2;
    const s32 rowStep = 40;
    memcpy(partyMenuCardLabelGlyphs, glyphs, sizeof(partyMenuCardLabelGlyphs));
    for (int enabled = 0; enabled < 2; ++enabled) {
        testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
        memset(&card, 0, sizeof(card));
        memset(partyLabelCall, 0, sizeof(partyLabelCall));
        partyLabelCalls = 0;
        partyLabelRecording = 1;
        setupPartyCardLabels(&card, slot, row, xTable, yTable, rowStep);
        partyLabelRecording = 0;
        CHECK(!memcmp(partyMenuCardLabelGlyphs, glyphs, sizeof(partyMenuCardLabelGlyphs)));
        int n = 0;
        for (int i = 0; i < 9; ++i) {
            if (glyphs[i] == 0xFFFF) continue;
            CHECK(n < partyLabelCalls);
            const LabelCall* c = &partyLabelCall[n];
            CHECK(c->glyph == (enabled && i == 6 ? 0x0E : glyphs[i]));
            CHECK(c->x == xTable[i] && c->y == rowStep * row + yTable[i]);
            CHECK(c->polys == &card.m0_polys[n * 2] && c->scale == 0x1000);
            CHECK(c->font == gMenuContext->m2DC_font && c->oddOrEven == gMenuContext->m308_oddOrEven);
            ++n;
        }
        CHECK(card.mBE8_0Length == n);
        CHECK(partyLabelCalls == n + 1);
        const LabelCall* name = &partyLabelCall[n];
        CHECK(name->glyph == slot + 0x14E && name->polys == card.m460_poly);
        CHECK(name->x == xTable[9] && name->y == rowStep * row + yTable[9]);
    }
    return 0;
}

static int testPartyCardLabels(void) {
    static const s32 shown[9] = {0x41, 0x42, 0xFFFF, 0x16, 0x43, 0x44, 0x16, 0x45, 0xFFFF};
    static const s32 skipped[9] = {0x41, 0x42, 0xFFFF, 0x16, 0x43, 0x44, 0xFFFF, 0x45, 0x46};
    void* font = gMenuContext->m2DC_font;
    s32 oddOrEven = gMenuContext->m308_oddOrEven;
    int enabled = pcGameplayOriginalBugFixes();
    static int fontStandIn;
    gMenuContext->m2DC_font = &fontStandIn;
    int failed = 0;
    for (int odd = 0; odd < 2 && !failed; ++odd) {
        gMenuContext->m308_oddOrEven = odd;
        failed = partyLabelsRun(shown) || partyLabelsRun(skipped);
    }
    partyLabelRecording = 0;
    partyLabelCalls = 0;
    memset(partyMenuCardLabelGlyphs, 0, sizeof(partyMenuCardLabelGlyphs));
    gMenuContext->m2DC_font = font;
    gMenuContext->m308_oddOrEven = oddOrEven;
    testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
    return failed;
}

/* A field's looping sound effect that a menu's sounds cut starts again:
 * the sound effect tracks and main's start/stop, as far as sfxkeep.c looks. */
SoundInstance* pSoundEffectsInstances;
static int sfxStarts, sfxLastId, sfxLastChannel, sfxLastVolume, sfxLastPan;
static u8 sfxLoopPoint;
static SoundTrack* sfxTracks(int ch) { return &pSoundEffectsInstances->m94_tracks[(ch & 0xFE) ^ 8]; }
void StopSoundEffect(s32 ch) {
    for (int i = 0; i < 2; ++i) sfxTracks(ch)[i].m0_flags = 0;
}
static void sfxStart(int id, int ch, int volume, int pan, int loops) {
    for (int i = 0; i < 2; ++i) {
        SoundTrack* t = &sfxTracks(ch)[i];
        t->m0_flags = 0x409; t->m8.m8_id = id; t->m18_loopStart = loops ? &sfxLoopPoint : NULL;
        t->m76_volume = volume << 8; t->m74_pan = pan << 8; t->m2_update = 0x170;
    }
}
void PlaySoundEffect2(s32 id, s32 ch, s32 vol, s32 pan) {
    ++sfxStarts; sfxLastId = id; sfxLastChannel = ch; sfxLastVolume = vol; sfxLastPan = pan;
    sfxStart(id, ch, vol, pan, 1);
}

static int testFieldSfx(void) {
    pSoundEffectsInstances = calloc(1, sizeof(SoundInstance) + 16 * sizeof(SoundTrack));
    for (int enabled = 0; enabled < 2; ++enabled) {
        testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
        memset(pSoundEffectsInstances->m94_tracks, 0, 16 * sizeof(SoundTrack));
        /* Field 103's crowd: sound 409 on channel 3 (tracks 14, 15), then the
         * script's volume (OPX_62); channel 1 a one-shot. */
        sfxStart(409, 6, 0x7F, 0x40, 1); pcFieldSfxPlayed(409, 0x7F, 0x40, 3);
        for (int i = 0; i < 2; ++i) sfxTracks(6)[i].m76_volume = 0x6F2 + i;
        sfxStart(77, 2, 0x7F, 0x40, 0); pcFieldSfxPlayed(77, 0x7F, 0x40, 1);
        unsigned mask = pcFieldSfxLoopingMask();
        CHECK(mask == (enabled ? 1u << 3 : 0u));
        CHECK(&pSoundEffectsInstances->m94_tracks[14] == sfxTracks(6));
        /* The menu's sound takes tracks 14 and 15 (playSoundEffect), the
         * one-shot ends. */
        sfxStart(94, 6, 0x60, 0x40, 0); StopSoundEffect(2);
        sfxStarts = 0;
        pcFieldSfxRestore(mask);
        CHECK(sfxStarts == (enabled ? 1 : 0));
        if (enabled) {
            CHECK(sfxLastId == 409 && sfxLastChannel == 6 && sfxLastVolume == 0x7F && sfxLastPan == 0x40);
            for (int i = 0; i < 2; ++i) {
                CHECK(sfxTracks(6)[i].m8.m8_id == 409 && sfxTracks(6)[i].m76_volume == 0x6F2 + i);
                CHECK(sfxTracks(6)[i].m74_pan == 0x4000 && (sfxTracks(6)[i].m2_update & 0x100));
            }
            CHECK(sfxTracks(2)[0].m0_flags == 0); /* the one-shot stays ended */
            /* Still playing: nothing restarts. */
            sfxStarts = 0; pcFieldSfxRestore(pcFieldSfxLoopingMask()); CHECK(sfxStarts == 0);
        }
    }
    free(pSoundEffectsInstances); pSoundEffectsInstances = NULL;
    return 0;
}

/* scriptfix.c's game state and calls: the current music, the field, the
 * script variables and the music instance's pitch and tempo. */
s32 currentlyPlayingMusic;
s32 fieldMapNumber;
static int scriptVariableReads, musicPitchCalls, musicTempoCalls;
static s32 scriptVariableValue, scriptVariableIndex; /* what getVariable returns, the index it was given */
/* pcFieldMusicStarted's reads: while scriptVariablesSplit is set, variable
 * 0x102 and variable 0 answer apart (anything else a marker); the first reads'
 * indices are kept in order. The pitch and tempo calls keep their arguments. */
static int scriptVariablesSplit;
static s32 scriptVariable102, scriptVariable0;
static s32 scriptVariableIndices[4];
typedef struct {
    SoundInstance* inst;
    s32 value, dur;
    int order; /* 1: the first of the pitch and tempo calls */
} MusicCall;
static MusicCall musicPitch, musicTempo;
static int musicCallOrder;
s32 getVariable(s32 varIndex) {
    if (scriptVariableReads < 4) scriptVariableIndices[scriptVariableReads] = varIndex;
    scriptVariableIndex = varIndex; ++scriptVariableReads;
    if (!scriptVariablesSplit) return scriptVariableValue;
    return varIndex == 0x102 ? scriptVariable102 : varIndex == 0 ? scriptVariable0 : 0x5A5A5A5A;
}
void setMusicInstancePitch(SoundInstance* inst, s32 val, s32 dur) {
    musicPitch = (MusicCall){inst, val, dur, ++musicCallOrder};
    ++musicPitchCalls;
}
void setMusicInstanceTempo(SoundInstance* inst, s32 tempo, s32 dur) {
    musicTempo = (MusicCall){inst, tempo, dur, ++musicCallOrder};
    ++musicTempoCalls;
}

/* Field 87's inn routine as scriptfix.c documents it: OP_PLAY_MUSIC 0D, OPX_A2,
 * OP_WAIT 0A, OP_START_FADE_TO_BLACK 3C, OP_WAIT 46, OP_RESTORE_HP,
 * OP_RESTORE_MP, OP_WAIT 1E, OP_START_FADE_IN 3C, OP_WAIT 46,
 * OP_SHOW_DIALOG_WINDOW_MODE0 0E, OP_WAIT_DIALOG, OP_WAIT 0A,
 * OP_DISABLE_RANDOM_BATTLES. Synthetic: no script from the disc. */
static const u8 innRoutine[] = {
    0x75, 0x0D, 0x80, 0xFE, 0xA2, 0x26, 0x0A, 0x80, 0xB4, 0x3C, 0x80, 0x26, 0x46, 0x80, 0x79, 0x7A, 0x26,
    0x1E, 0x80, 0xB3, 0x3C, 0x80, 0x26, 0x46, 0x80, 0xD2, 0x0E, 0x00, 0x10, 0x9C, 0x26, 0x0A, 0x80, 0x14,
};
#define INN_LAST_WAIT 30 /* the OP_WAIT 0A after the morning line */
#define INN_SCRIPT 96

/* A filler script around the routine; none of its bytes are 0x75. */
static void innScript(u8* code, int at) {
    for (int i = 0; i < INN_SCRIPT; ++i) code[i] = (u8)(0x30 + i % 0x40);
    if (at >= 0) memcpy(code + at, innRoutine, sizeof(innRoutine));
}

/* Runs the fix and checks the whole buffer against the expected one, and that
 * the music state and the stubs saw nothing else. */
static int innRun(int field, s32 music, u8* code, int size, const u8* expected) {
    u8 guard[INN_SCRIPT + 8];
    memcpy(guard, code, INN_SCRIPT + 8);
    currentlyPlayingMusic = music;
    scriptVariableReads = musicPitchCalls = musicTempoCalls = 0;
    pcFieldScriptFixes(field, code, size);
    CHECK(!memcmp(code, expected, size));
    CHECK(!memcmp(code + size, guard + size, INN_SCRIPT + 8 - size)); /* past size: untouched */
    CHECK(currentlyPlayingMusic == music);
    CHECK(scriptVariableReads == 0 && musicPitchCalls == 0 && musicTempoCalls == 0);
    return 0;
}
#define INN_RUN(...) do { if (innRun(__VA_ARGS__)) return 1; } while (0)

/* Field 87: the inn's last OP_WAIT 0A becomes OP_PLAY_MUSIC of the music that
 * played as the field loaded, only with the routine's bytes found whole. */
static int testNisanInn(void) {
    static const s32 musics[] = {0, 0x26, 0xEF, 0xF0, 0xFF, -1, 0x100};
    static const int places[] = {0, 40, INN_SCRIPT - (int)sizeof(innRoutine)};
    u8 code[INN_SCRIPT + 8], expected[INN_SCRIPT + 8];
    for (int enabled = 0; enabled < 2; ++enabled) {
        testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
        for (int m = 0; m < (int)(sizeof(musics) / sizeof(musics[0])); ++m)
        for (int p = 0; p < 3; ++p) {
            s32 music = musics[m];
            int at = places[p];
            int fixes = enabled && music >= 0 && music < 0xF0;
            /* The routine whole: the one edit, three bytes, when on. */
            memset(code, 0xEE, sizeof(code));
            innScript(code, at);
            memcpy(expected, code, sizeof(expected));
            if (fixes) {
                expected[at + INN_LAST_WAIT] = 0x75;
                expected[at + INN_LAST_WAIT + 1] = (u8)music;
                expected[at + INN_LAST_WAIT + 2] = 0x80;
            }
            INN_RUN(87, music, code, INN_SCRIPT, expected);
            /* Fixed once, the routine no longer matches: a second pass keeps it. */
            memcpy(expected, code, sizeof(expected));
            INN_RUN(87, music, code, INN_SCRIPT, expected);
            /* Another field with the same bytes: nothing. */
            static const int fields[] = {0, 86, 88, 93, 50, 213, 282, 326};
            for (int f = 0; f < (int)(sizeof(fields) / sizeof(fields[0])); ++f) {
                memset(code, 0xEE, sizeof(code));
                innScript(code, at);
                memcpy(expected, code, sizeof(expected));
                INN_RUN(fields[f], music, code, INN_SCRIPT, expected);
            }
            /* Any one byte of the routine different: nothing. */
            for (int k = 0; k < (int)sizeof(innRoutine); ++k) {
                memset(code, 0xEE, sizeof(code));
                innScript(code, at);
                code[at + k] ^= 0x01;
                memcpy(expected, code, sizeof(expected));
                INN_RUN(87, music, code, INN_SCRIPT, expected);
            }
            /* The routine cut short by the script's end: nothing, and nothing
             * written past the size given. */
            for (int cut = 1; cut <= (int)sizeof(innRoutine); ++cut) {
                memset(code, 0xEE, sizeof(code));
                innScript(code, at);
                memcpy(expected, code, sizeof(expected));
                INN_RUN(87, music, code, at + (int)sizeof(innRoutine) - cut, expected);
            }
        }
        /* No routine at all, and an empty script. */
        memset(code, 0xEE, sizeof(code));
        innScript(code, -1);
        memcpy(expected, code, sizeof(expected));
        INN_RUN(87, 0x26, code, INN_SCRIPT, expected);
        INN_RUN(87, 0x26, code, 0, expected);
        /* Two copies: the first one found is the one edited. */
        memset(code, 0xEE, sizeof(code));
        innScript(code, 0);
        memcpy(code + 48, innRoutine, sizeof(innRoutine));
        memcpy(expected, code, sizeof(expected));
        if (enabled) {
            expected[INN_LAST_WAIT] = 0x75; expected[INN_LAST_WAIT + 1] = 0x26;
            expected[INN_LAST_WAIT + 2] = 0x80;
        }
        INN_RUN(87, 0x26, code, INN_SCRIPT, expected);
    }
    return 0;
}

/* Field 213's rest after the Baptism as scriptfix.c documents it:
 * OP_FADE_MUSIC 0 over 60, OP_PLAY_MUSIC 0D, OPX_A2, OP_WAIT 240,
 * OP_SET_SCREEN_GEOMETRY; fixed, its first 14 bytes are the bed's
 * OP_PLAY_MUSIC 0D, OPX_A2, OP_WAIT 240, OP_WAIT 90, OP_WAIT 1. Synthetic. */
static const u8 wardRoutine[] = {
    0xFE, 0x0E, 0x00, 0x80, 0x3C, 0x80, 0x75, 0x0D, 0x80, 0xFE, 0xA2, 0x26, 0xF0, 0x80, 0xA0,
};
static const u8 wardRoutineFixed[] = {
    0x75, 0x0D, 0x80, 0xFE, 0xA2, 0x26, 0xF0, 0x80, 0x26, 0x5A, 0x80, 0x26, 0x01, 0x80,
};

/* The inn's filler script (no 0xFE byte in it) around the ward's routine. */
static void wardScript(u8* code, int at) {
    memset(code, 0xEE, INN_SCRIPT + 8);
    innScript(code, -1);
    if (at >= 0) memcpy(code + at, wardRoutine, sizeof(wardRoutine));
}

/* Field 213: the fade and the one OP_WAIT 240 become the bed's 14 bytes, only
 * with the routine's 15 bytes found whole; the last one stays. */
static int testWardRest(void) {
    /* Not 0x19: field 282 reads the game's progress with Emeralda's theme. */
    static const s32 musics[] = {0x0D, 0x26, 0x47, 0xFF, -1};
    static const int places[] = {0, 40, INN_SCRIPT - (int)sizeof(wardRoutine)};
    u8 code[INN_SCRIPT + 8], expected[INN_SCRIPT + 8];
    for (int enabled = 0; enabled < 2; ++enabled) {
        testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
        for (int m = 0; m < (int)(sizeof(musics) / sizeof(musics[0])); ++m)
        for (int p = 0; p < 3; ++p) {
            s32 music = musics[m];
            int at = places[p];
            /* The routine whole: its first 14 bytes replaced when on. */
            wardScript(code, at);
            memcpy(expected, code, sizeof(expected));
            if (enabled) {
                memcpy(expected + at, wardRoutineFixed, sizeof(wardRoutineFixed));
                CHECK(expected[at + 14] == 0xA0);
            }
            INN_RUN(213, music, code, INN_SCRIPT, expected);
            /* Fixed once, the routine no longer matches: a second pass keeps it. */
            memcpy(expected, code, sizeof(expected));
            INN_RUN(213, music, code, INN_SCRIPT, expected);
            /* Another field with the same bytes: nothing. */
            static const int fields[] = {0, 212, 214, 87, 50, 282, 326, 213 + 0x1000};
            for (int f = 0; f < (int)(sizeof(fields) / sizeof(fields[0])); ++f) {
                wardScript(code, at);
                memcpy(expected, code, sizeof(expected));
                INN_RUN(fields[f], music, code, INN_SCRIPT, expected);
            }
            /* Any one byte of the routine different (the kept last one too): nothing. */
            for (int k = 0; k < (int)sizeof(wardRoutine); ++k) {
                wardScript(code, at);
                code[at + k] ^= 0x01;
                memcpy(expected, code, sizeof(expected));
                INN_RUN(213, music, code, INN_SCRIPT, expected);
            }
            /* The routine cut short by the script's end: nothing, and nothing
             * written past the size given. */
            for (int cut = 1; cut <= (int)sizeof(wardRoutine); ++cut) {
                wardScript(code, at);
                memcpy(expected, code, sizeof(expected));
                INN_RUN(213, music, code, at + (int)sizeof(wardRoutine) - cut, expected);
            }
        }
        /* No routine at all, and an empty script. */
        wardScript(code, -1);
        memcpy(expected, code, sizeof(expected));
        INN_RUN(213, 0x0D, code, INN_SCRIPT, expected);
        INN_RUN(213, 0x0D, code, 0, expected);
        /* Only the 14 bytes that are rewritten, without the last: nothing. */
        wardScript(code, 40);
        code[40 + 14] = 0x30;
        memcpy(expected, code, sizeof(expected));
        INN_RUN(213, 0x0D, code, INN_SCRIPT, expected);
        /* A partial copy before a whole one: the whole one is edited. */
        wardScript(code, 30);
        memcpy(code + 10, wardRoutine, 14);
        memcpy(expected, code, sizeof(expected));
        if (enabled) memcpy(expected + 30, wardRoutineFixed, sizeof(wardRoutineFixed));
        INN_RUN(213, 0x0D, code, INN_SCRIPT, expected);
        /* Two copies: the first one found is the one edited. */
        wardScript(code, 0);
        memcpy(code + 48, wardRoutine, sizeof(wardRoutine));
        memcpy(expected, code, sizeof(expected));
        if (enabled) memcpy(expected, wardRoutineFixed, sizeof(wardRoutineFixed));
        INN_RUN(213, 0x0D, code, INN_SCRIPT, expected);
        /* Field 87's inn routine in field 213: nothing. */
        innScript(code, 20);
        memcpy(expected, code, sizeof(expected));
        INN_RUN(213, 0x26, code, INN_SCRIPT, expected);
    }
    return 0;
}

/* Field 50's motorcycle trail as scriptfix.c documents it:
 * OP_RUN_ENTITY_SCRIPT_BLOCKING 0A 24, OP_DELETE_ENTITY 0A, OP_DELETE_ENTITY 09,
 * OP_START_FADE_TO_BLACK 15, OP_WAIT 15, OP_START_FADE_IN 15; fixed, the fade
 * and its wait come before the two deletions (same bytes). Synthetic. */
static const u8 soldierRoutine[] = {
    0x09, 0x0A, 0x24, 0x29, 0x0A, 0x29, 0x09, 0xB4, 0x0F, 0x80, 0x26, 0x0F, 0x80, 0xB3, 0x0F, 0x80,
};
static const u8 soldierRoutineFixed[] = {
    0x09, 0x0A, 0x24, 0xB4, 0x0F, 0x80, 0x26, 0x0F, 0x80, 0x29, 0x0A, 0x29, 0x09, 0xB3, 0x0F, 0x80,
};
#define SOLDIER_DELETES 3 /* the two OP_DELETE_ENTITY, 4 bytes */
#define SOLDIER_FADE 7    /* OP_START_FADE_TO_BLACK and OP_WAIT, 6 bytes */

/* The inn's filler script (no 0x09 byte in it) around the trail's routine. */
static void soldierScript(u8* code, int at) {
    memset(code, 0xEE, INN_SCRIPT + 8);
    innScript(code, -1);
    if (at >= 0) memcpy(code + at, soldierRoutine, sizeof(soldierRoutine));
}

/* Field 50: the deletions and the fade swap places, only with the routine's
 * 16 bytes found whole; the blocking call before and the fade-in after stay. */
static int testSoldierFade(void) {
    /* Not 0x19: field 282 reads the game's progress with Emeralda's theme. */
    static const s32 musics[] = {0x0D, 0x26, 0xFF, -1};
    static const int places[] = {0, 40, INN_SCRIPT - (int)sizeof(soldierRoutine)};
    u8 code[INN_SCRIPT + 8], expected[INN_SCRIPT + 8];
    /* The fixed bytes are the routine's own, reordered. */
    CHECK(!memcmp(soldierRoutineFixed, soldierRoutine, SOLDIER_DELETES));
    CHECK(!memcmp(soldierRoutineFixed + SOLDIER_DELETES, soldierRoutine + SOLDIER_FADE, 6));
    CHECK(!memcmp(soldierRoutineFixed + SOLDIER_DELETES + 6, soldierRoutine + SOLDIER_DELETES, 4));
    CHECK(!memcmp(soldierRoutineFixed + 13, soldierRoutine + 13, 3));
    for (int enabled = 0; enabled < 2; ++enabled) {
        testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
        for (int m = 0; m < (int)(sizeof(musics) / sizeof(musics[0])); ++m)
        for (int p = 0; p < 3; ++p) {
            s32 music = musics[m];
            int at = places[p];
            /* The routine whole: its 16 bytes reordered when on. */
            soldierScript(code, at);
            memcpy(expected, code, sizeof(expected));
            if (enabled) memcpy(expected + at, soldierRoutineFixed, sizeof(soldierRoutineFixed));
            INN_RUN(50, music, code, INN_SCRIPT, expected);
            /* Fixed once, the routine no longer matches: a second pass keeps it. */
            memcpy(expected, code, sizeof(expected));
            INN_RUN(50, music, code, INN_SCRIPT, expected);
            /* Another field with the same bytes: nothing. */
            static const int fields[] = {0, 49, 51, 87, 213, 282, 326, 50 + 0x1000};
            for (int f = 0; f < (int)(sizeof(fields) / sizeof(fields[0])); ++f) {
                soldierScript(code, at);
                memcpy(expected, code, sizeof(expected));
                INN_RUN(fields[f], music, code, INN_SCRIPT, expected);
            }
            /* Any one byte of the routine different: nothing. */
            for (int k = 0; k < (int)sizeof(soldierRoutine); ++k) {
                soldierScript(code, at);
                code[at + k] ^= 0x01;
                memcpy(expected, code, sizeof(expected));
                INN_RUN(50, music, code, INN_SCRIPT, expected);
            }
            /* The routine cut short by the script's end: nothing, and nothing
             * written past the size given. */
            for (int cut = 1; cut <= (int)sizeof(soldierRoutine); ++cut) {
                soldierScript(code, at);
                memcpy(expected, code, sizeof(expected));
                INN_RUN(50, music, code, at + (int)sizeof(soldierRoutine) - cut, expected);
            }
        }
        /* No routine at all, and an empty script. */
        soldierScript(code, -1);
        memcpy(expected, code, sizeof(expected));
        INN_RUN(50, 0x26, code, INN_SCRIPT, expected);
        INN_RUN(50, 0x26, code, 0, expected);
        /* Already in the fixed order: nothing. */
        soldierScript(code, -1);
        memcpy(code + 40, soldierRoutineFixed, sizeof(soldierRoutineFixed));
        memcpy(expected, code, sizeof(expected));
        INN_RUN(50, 0x26, code, INN_SCRIPT, expected);
        /* Without the fade-in after: nothing. */
        soldierScript(code, 40);
        code[40 + 13] = 0x30;
        memcpy(expected, code, sizeof(expected));
        INN_RUN(50, 0x26, code, INN_SCRIPT, expected);
        /* A partial copy before a whole one: the whole one is edited. */
        soldierScript(code, 30);
        memcpy(code + 10, soldierRoutine, 13);
        memcpy(expected, code, sizeof(expected));
        if (enabled) memcpy(expected + 30, soldierRoutineFixed, sizeof(soldierRoutineFixed));
        INN_RUN(50, 0x26, code, INN_SCRIPT, expected);
        /* Two copies: the first one found is the one edited. */
        soldierScript(code, 0);
        memcpy(code + 48, soldierRoutine, sizeof(soldierRoutine));
        memcpy(expected, code, sizeof(expected));
        if (enabled) memcpy(expected, soldierRoutineFixed, sizeof(soldierRoutineFixed));
        INN_RUN(50, 0x26, code, INN_SCRIPT, expected);
        /* Fields 87's and 213's routines in field 50: nothing. */
        innScript(code, 20);
        memcpy(code + 60, wardRoutine, sizeof(wardRoutine));
        memcpy(expected, code, sizeof(expected));
        INN_RUN(50, 0x26, code, INN_SCRIPT, expected);
    }
    return 0;
}

/* Field 326's script start as scriptfix.c documents it: byte 0 the entrance
 * table's 0xFF, bytes 1-6 entrance 0's position, walkmesh and camera facing,
 * byte 7 its party facing (0xFF: from field variable 6). Synthetic. */
static const u8 orphanageStart[] = {0xFF, 0xFB, 0xFF, 0xFA, 0xFA, 0x00, 0xFF, 0xFF};
#define ORPHANAGE_PARTY_FACING 7

/* The inn's filler script (no 0xFF byte in it) after the orphanage's start;
 * at < 0: the filler alone. */
static void orphanageScript(u8* code, int at) {
    memset(code, 0xEE, INN_SCRIPT + 8);
    innScript(code, -1);
    if (at >= 0) memcpy(code + at, orphanageStart, sizeof(orphanageStart));
}

/* Field 326: entrance 0's party facing becomes 0 (up-screen), only with the
 * script's first 8 bytes the documented ones and the script longer than them. */
static int testOrphanageFacing(void) {
    /* Not 0x19: field 282 reads the game's progress with Emeralda's theme. */
    static const s32 musics[] = {0x0D, 0x26, 0xFF, -1};
    u8 code[INN_SCRIPT + 8], expected[INN_SCRIPT + 8];
    for (int enabled = 0; enabled < 2; ++enabled) {
        testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
        for (int m = 0; m < (int)(sizeof(musics) / sizeof(musics[0])); ++m) {
            s32 music = musics[m];
            /* The start whole: the one facing byte, when on. */
            orphanageScript(code, 0);
            memcpy(expected, code, sizeof(expected));
            if (enabled) expected[ORPHANAGE_PARTY_FACING] = 0x00;
            INN_RUN(326, music, code, INN_SCRIPT, expected);
            /* Fixed once (facing 0), the start no longer matches: a second pass keeps it. */
            memcpy(expected, code, sizeof(expected));
            INN_RUN(326, music, code, INN_SCRIPT, expected);
            /* Another field with the same bytes (302 walks in to entrance 0 too): nothing. */
            static const int fields[] = {0, 325, 327, 302, 87, 213, 50, 282, 326 + 0x1000};
            for (int f = 0; f < (int)(sizeof(fields) / sizeof(fields[0])); ++f) {
                orphanageScript(code, 0);
                memcpy(expected, code, sizeof(expected));
                INN_RUN(fields[f], music, code, INN_SCRIPT, expected);
            }
            /* Any one byte of the start different (the facing byte too): nothing. */
            for (int k = 0; k < (int)sizeof(orphanageStart); ++k) {
                orphanageScript(code, 0);
                code[k] ^= 0x01;
                memcpy(expected, code, sizeof(expected));
                INN_RUN(326, music, code, INN_SCRIPT, expected);
            }
            /* The start found later in the script: not the entrance table, nothing. */
            for (int at = 1; at <= INN_SCRIPT - (int)sizeof(orphanageStart); at += 13) {
                orphanageScript(code, at);
                memcpy(expected, code, sizeof(expected));
                INN_RUN(326, music, code, INN_SCRIPT, expected);
            }
            /* The script's size: up to the start's 8 bytes nothing (the whole
             * start of a script of 8 bytes too), from 9 the edit; nothing
             * written past the size given. */
            for (int size = 0; size <= 16; ++size) {
                orphanageScript(code, 0);
                memcpy(expected, code, sizeof(expected));
                if (enabled && size > (int)sizeof(orphanageStart)) expected[ORPHANAGE_PARTY_FACING] = 0x00;
                INN_RUN(326, music, code, size, expected);
            }
        }
        /* No start at all, and an empty script. */
        orphanageScript(code, -1);
        memcpy(expected, code, sizeof(expected));
        INN_RUN(326, 0x26, code, INN_SCRIPT, expected);
        INN_RUN(326, 0x26, code, 0, expected);
        /* Already facing up-screen: nothing. */
        orphanageScript(code, 0);
        code[ORPHANAGE_PARTY_FACING] = 0x00;
        memcpy(expected, code, sizeof(expected));
        INN_RUN(326, 0x26, code, INN_SCRIPT, expected);
        /* A second copy later: only the script's first one is edited. */
        orphanageScript(code, 0);
        memcpy(code + 48, orphanageStart, sizeof(orphanageStart));
        memcpy(expected, code, sizeof(expected));
        if (enabled) expected[ORPHANAGE_PARTY_FACING] = 0x00;
        INN_RUN(326, 0x26, code, INN_SCRIPT, expected);
        /* Fields 87's, 213's and 50's routines after the start in field 326:
         * the facing byte only, the routines untouched. */
        orphanageScript(code, 0);
        memcpy(code + 10, innRoutine, sizeof(innRoutine));
        memcpy(code + 50, wardRoutine, sizeof(wardRoutine));
        memcpy(code + 70, soldierRoutine, sizeof(soldierRoutine));
        memcpy(expected, code, sizeof(expected));
        if (enabled) expected[ORPHANAGE_PARTY_FACING] = 0x00;
        INN_RUN(326, 0x26, code, INN_SCRIPT, expected);
    }
    return 0;
}

/* Field 282's cue after Emeralda's laugh as scriptfix.c documents it:
 * OP_WAIT 6, OP_PLAY_MUSIC 19, OP_SHOW_DIALOG_WINDOW_MODE0 1E. Synthetic. */
static const u8 emeraldaCue[] = {0x26, 0x06, 0x80, 0x75, 0x19, 0x80, 0xD2, 0x1E, 0x00, 0x11};
#define EMERALDA_THEME 0x19
#define EMERALDA_PROGRESS 207

/* The inn's filler script (no 0x26 byte in it) around the cue. */
static void emeraldaScript(u8* code, int at) {
    memset(code, 0xEE, INN_SCRIPT + 8);
    innScript(code, -1);
    if (at >= 0) memcpy(code + at, emeraldaCue, sizeof(emeraldaCue));
}

/* Runs the fix with the game's progress (variable 0) at progress: the script
 * is never written (nor past size), the current music ends as after, the
 * progress read reads times (variable 0 only) and the pitch and tempo stay. */
static int emeraldaRun(int field, s32 music, s32 progress, u8* code, int size, s32 after, int reads) {
    u8 before[INN_SCRIPT + 8];
    memcpy(before, code, sizeof(before));
    currentlyPlayingMusic = music;
    scriptVariableValue = progress;
    scriptVariableIndex = -1;
    scriptVariableReads = musicPitchCalls = musicTempoCalls = 0;
    pcFieldScriptFixes(field, code, size);
    CHECK(!memcmp(code, before, sizeof(before)));
    CHECK(currentlyPlayingMusic == after);
    CHECK(scriptVariableReads == reads);
    CHECK(scriptVariableIndex == (reads ? 0 : -1));
    CHECK(musicPitchCalls == 0 && musicTempoCalls == 0);
    scriptVariableValue = 0;
    return 0;
}
#define EMERALDA_RUN(...) do { if (emeraldaRun(__VA_ARGS__)) return 1; } while (0)

/* Field 282: with Emeralda's theme current, progress 207 and the cue in the
 * script, the current music becomes 0xFF; the script stays as it is. The
 * checks run in the hook's order: the field, the music, then the progress
 * (read only past the first two), then the cue. */
static int testEmeraldaTheme(void) {
    static const int places[] = {0, 40, INN_SCRIPT - (int)sizeof(emeraldaCue)};
    static const s32 musics[] = {0, 0x18, 0x1A, 0x1E, 0x0D, 0xF0, 0xFF, -1, 0x119, -0xE7};
    static const s32 progresses[] = {0, 206, 208, 0xCF + 0x100, 0x10000 + 207, -1, -207};
    static const int fields[] = {0, 281, 283, 505, 87, 213, 50, 326, 282 + 0x1000};
    u8 code[INN_SCRIPT + 8];
    for (int enabled = 0; enabled < 2; ++enabled) {
        testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
        int reads = enabled; /* off: the hook returns before any guard */
        s32 fixed = enabled ? 0xFF : EMERALDA_THEME;
        for (int p = 0; p < 3; ++p) {
            int at = places[p];
            /* The cue whole: the theme forgotten when on. */
            emeraldaScript(code, at);
            EMERALDA_RUN(282, EMERALDA_THEME, EMERALDA_PROGRESS, code, INN_SCRIPT, fixed, reads);
            /* Forgotten once, 0xFF is not the theme: a second pass keeps it, no read. */
            EMERALDA_RUN(282, 0xFF, EMERALDA_PROGRESS, code, INN_SCRIPT, 0xFF, 0);
            /* The script ending right after the cue: the same. */
            EMERALDA_RUN(282, EMERALDA_THEME, EMERALDA_PROGRESS, code, at + (int)sizeof(emeraldaCue), fixed, reads);
            /* Another field with the same bytes and state: nothing, no read. */
            for (int f = 0; f < (int)(sizeof(fields) / sizeof(fields[0])); ++f)
                EMERALDA_RUN(fields[f], EMERALDA_THEME, EMERALDA_PROGRESS, code, INN_SCRIPT, EMERALDA_THEME, 0);
            /* Another current music (out of range too): nothing, no read. */
            for (int m = 0; m < (int)(sizeof(musics) / sizeof(musics[0])); ++m)
                EMERALDA_RUN(282, musics[m], EMERALDA_PROGRESS, code, INN_SCRIPT, musics[m], 0);
            /* Another progress: nothing, after the one read when on. */
            for (int g = 0; g < (int)(sizeof(progresses) / sizeof(progresses[0])); ++g)
                EMERALDA_RUN(282, EMERALDA_THEME, progresses[g], code, INN_SCRIPT, EMERALDA_THEME, reads);
            /* Any one byte of the cue different: nothing, after the read. */
            for (int k = 0; k < (int)sizeof(emeraldaCue); ++k) {
                emeraldaScript(code, at);
                code[at + k] ^= 0x01;
                EMERALDA_RUN(282, EMERALDA_THEME, EMERALDA_PROGRESS, code, INN_SCRIPT, EMERALDA_THEME, reads);
            }
            /* The cue cut short by the script's end: nothing, after the read. */
            for (int cut = 1; cut <= (int)sizeof(emeraldaCue); ++cut) {
                emeraldaScript(code, at);
                EMERALDA_RUN(282, EMERALDA_THEME, EMERALDA_PROGRESS, code, at + (int)sizeof(emeraldaCue) - cut,
                             EMERALDA_THEME, reads);
            }
        }
        /* No cue at all, and an empty script. */
        emeraldaScript(code, -1);
        EMERALDA_RUN(282, EMERALDA_THEME, EMERALDA_PROGRESS, code, INN_SCRIPT, EMERALDA_THEME, reads);
        EMERALDA_RUN(282, EMERALDA_THEME, EMERALDA_PROGRESS, code, 0, EMERALDA_THEME, reads);
        /* OP_PLAY_MUSIC 19 alone, without the laugh's wait or the dialog: nothing. */
        emeraldaScript(code, -1);
        memcpy(code + 40, emeraldaCue + 3, 3);
        EMERALDA_RUN(282, EMERALDA_THEME, EMERALDA_PROGRESS, code, INN_SCRIPT, EMERALDA_THEME, reads);
        /* A partial copy before a whole one, and two copies: forgotten once. */
        emeraldaScript(code, 30);
        memcpy(code + 10, emeraldaCue, 9);
        EMERALDA_RUN(282, EMERALDA_THEME, EMERALDA_PROGRESS, code, INN_SCRIPT, fixed, reads);
        emeraldaScript(code, 0);
        memcpy(code + 48, emeraldaCue, sizeof(emeraldaCue));
        EMERALDA_RUN(282, EMERALDA_THEME, EMERALDA_PROGRESS, code, INN_SCRIPT, fixed, reads);
        /* Fields 326's, 87's, 213's and 50's bytes around the cue in field 282:
         * the music only, the script untouched. */
        orphanageScript(code, 0);
        memcpy(code + 10, innRoutine, sizeof(innRoutine));
        memcpy(code + 50, wardRoutine, sizeof(wardRoutine));
        memcpy(code + 70, soldierRoutine, sizeof(soldierRoutine));
        memcpy(code + 86, emeraldaCue, sizeof(emeraldaCue));
        EMERALDA_RUN(282, EMERALDA_THEME, EMERALDA_PROGRESS, code, INN_SCRIPT, fixed, reads);
        /* The same without the cue: nothing, after the read. */
        code[86] = 0x30;
        EMERALDA_RUN(282, EMERALDA_THEME, EMERALDA_PROGRESS, code, INN_SCRIPT, EMERALDA_THEME, reads);
    }
    return 0;
}

/* Nisan late in the story as scriptfix.c's pcFieldMusicStarted reads it: Elly's
 * theme (0x1E) started in fields 85-100, 474 or 493 (the field number's low 12
 * bits) with variable 0x102 at 0x42 or more and progress (variable 0) above 207
 * gets pitch 1 and tempo 0x1C0, both at once (duration 0). */
#define ELLY_THEME 0x1E
#define ELLY_PITCH 1
#define ELLY_TEMPO 0x1C0
static SoundInstance ellyInstance, otherInstance;

/* Runs the hook in field with the two variables: reads is how many variables
 * it reads (0x102, then 0), applies whether pitch and tempo go to instance.
 * Nothing else changes: the current music, the field, the instances' bytes. */
static int ellyRun(s32 field, s32 music, void* instance, s32 var102, s32 progress, int reads, int applies) {
    static const MusicCall none = {NULL, -1, -1, 0};
    SoundInstance ellyBefore = ellyInstance, otherBefore = otherInstance;
    fieldMapNumber = field;
    currentlyPlayingMusic = 0x33;
    scriptVariablesSplit = 1;
    scriptVariable102 = var102;
    scriptVariable0 = progress;
    scriptVariableValue = 0x77;
    scriptVariableIndex = -1;
    for (int i = 0; i < 4; ++i) scriptVariableIndices[i] = -1;
    scriptVariableReads = musicPitchCalls = musicTempoCalls = musicCallOrder = 0;
    musicPitch = musicTempo = none;
    pcFieldMusicStarted(music, instance);
    CHECK(scriptVariableReads == reads);
    CHECK(scriptVariableIndices[0] == (reads >= 1 ? 0x102 : -1));
    CHECK(scriptVariableIndices[1] == (reads >= 2 ? 0 : -1));
    CHECK(scriptVariableIndices[2] == -1);
    CHECK(musicPitchCalls == applies && musicTempoCalls == applies);
    if (applies) {
        CHECK(musicPitch.inst == instance && musicPitch.value == ELLY_PITCH && musicPitch.dur == 0);
        CHECK(musicTempo.inst == instance && musicTempo.value == ELLY_TEMPO && musicTempo.dur == 0);
        CHECK(musicPitch.order == 1 && musicTempo.order == 2);
    } else {
        CHECK(musicPitch.inst == NULL && musicPitch.value == -1 && musicPitch.dur == -1 && musicPitch.order == 0);
        CHECK(musicTempo.inst == NULL && musicTempo.value == -1 && musicTempo.dur == -1 && musicTempo.order == 0);
    }
    CHECK(fieldMapNumber == field && currentlyPlayingMusic == 0x33 && scriptVariableValue == 0x77);
    CHECK(!memcmp(&ellyInstance, &ellyBefore, sizeof(ellyBefore)));
    CHECK(!memcmp(&otherInstance, &otherBefore, sizeof(otherBefore)));
    scriptVariablesSplit = 0;
    return 0;
}
#define ELLY_RUN(...) do { if (ellyRun(__VA_ARGS__)) return 1; } while (0)

/* The checks run in the hook's order: the option and the instance, the music,
 * the field (no read before these), variable 0x102, then the progress. */
static int ellyThemeRuns(void) {
    static const s32 nisan[] = {85, 86, 87, 90, 93, 94, 99, 100, 474, 493, 0x1000 + 93, 0xF000 + 474, 0x7000 + 85};
    static const s32 fields[] = {0, 84, 101, 473, 475, 492, 494, 282, 213, 50, 326, 505, 0xFFF, 0x100 + 93,
                                 0x1000 + 84, 0x1000 + 101, 0x2000 + 494, -1};
    static const s32 musics[] = {0, 0x1D, 0x1F, 0x0D, 0x19, 0x26, 0xF0, 0xFF, -1, 0x11E, 0x1001E, -ELLY_THEME};
    static const s32 lates[] = {0x42, 0x43, 0xFF, 0x7FFFFFFF};
    static const s32 earlies[] = {0x41, 0x40, 0, -1, -0x42, -0x7FFFFFFF - 1};
    static const s32 afters[] = {208, 209, 0x10000, 0x7FFFFFFF};
    static const s32 befores[] = {207, 206, 0, -1, -208, -0x7FFFFFFF - 1};
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
    SoundInstance* instances[] = {&ellyInstance, &otherInstance};
    for (int enabled = 0; enabled < 2; ++enabled) {
        testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
        int one = enabled, two = enabled ? 2 : 0; /* off: no read at all */
        for (int i = 0; i < 2; ++i) {
            void* inst = instances[i];
            /* Every Nisan field in the state: the pitch and tempo, when on. */
            for (int f = 0; f < COUNT(nisan); ++f) ELLY_RUN(nisan[f], ELLY_THEME, inst, 0x42, 208, two, enabled);
            /* Variable 0x102 at 0x42 or more and progress above 207: the same. */
            for (int v = 0; v < COUNT(lates); ++v)
                for (int g = 0; g < COUNT(afters); ++g)
                    ELLY_RUN(93, ELLY_THEME, inst, lates[v], afters[g], two, enabled);
            /* Another field: nothing, no read. */
            for (int f = 0; f < COUNT(fields); ++f) ELLY_RUN(fields[f], ELLY_THEME, inst, 0x42, 208, 0, 0);
            /* Another music (out of range too), in Nisan: nothing, no read. */
            for (int m = 0; m < COUNT(musics); ++m) {
                ELLY_RUN(93, musics[m], inst, 0x42, 208, 0, 0);
                ELLY_RUN(474, musics[m], inst, 0x42, 208, 0, 0);
            }
            /* Variable 0x102 below 0x42: nothing, after its read only. */
            for (int v = 0; v < COUNT(earlies); ++v)
                for (int g = 0; g < COUNT(afters); ++g)
                    ELLY_RUN(94, ELLY_THEME, inst, earlies[v], afters[g], one, 0);
            /* Progress 207 or less: nothing, after both reads. */
            for (int v = 0; v < COUNT(lates); ++v)
                for (int g = 0; g < COUNT(befores); ++g)
                    ELLY_RUN(474, ELLY_THEME, inst, lates[v], befores[g], two, 0);
            /* Neither: nothing, after the first read. */
            ELLY_RUN(493, ELLY_THEME, inst, 0x41, 207, one, 0);
        }
        /* No instance: nothing, no read, whatever the state. */
        for (int f = 0; f < COUNT(nisan); ++f) ELLY_RUN(nisan[f], ELLY_THEME, NULL, 0x42, 208, 0, 0);
        ELLY_RUN(93, 0x1D, NULL, 0x42, 208, 0, 0);
        ELLY_RUN(84, ELLY_THEME, NULL, 0x42, 208, 0, 0);
        ELLY_RUN(93, ELLY_THEME, NULL, 0x41, 207, 0, 0);
        /* The field scripts' hook in the same state, over a script without
         * field 87's inn routine: no pitch or tempo, the script kept. */
        fieldMapNumber = 93;
        scriptVariablesSplit = 1;
        scriptVariable102 = 0x42;
        scriptVariable0 = 208;
        scriptVariableReads = musicPitchCalls = musicTempoCalls = 0;
        u8 code[INN_SCRIPT + 8], before[INN_SCRIPT + 8];
        memset(code, 0xEE, sizeof(code));
        innScript(code, -1);
        memcpy(before, code, sizeof(before));
        currentlyPlayingMusic = ELLY_THEME;
        for (int f = 0; f < COUNT(nisan); ++f) pcFieldScriptFixes(nisan[f], code, INN_SCRIPT);
        CHECK(!memcmp(code, before, sizeof(before)) && currentlyPlayingMusic == ELLY_THEME);
        CHECK(scriptVariableReads == 0 && musicPitchCalls == 0 && musicTempoCalls == 0);
        scriptVariablesSplit = 0;
    }
#undef COUNT
    return 0;
}

/* The option, field, music, variables and stubs as they were before, pass or
 * fail, so the order of the tests cannot hide a failure. */
static int testEllyTheme(void) {
    int was = pcGameplayOriginalBugFixes();
    s32 music = currentlyPlayingMusic, field = fieldMapNumber;
    s32 value = scriptVariableValue, index = scriptVariableIndex;
    memset(&ellyInstance, 0xA5, sizeof(ellyInstance));
    memset(&otherInstance, 0x5A, sizeof(otherInstance));
    int failed = ellyThemeRuns();
    testSetEnv("XG_ORIGINAL_BUG_FIXES", was ? "on" : "off");
    currentlyPlayingMusic = music;
    fieldMapNumber = field;
    scriptVariableValue = value;
    scriptVariableIndex = index;
    scriptVariablesSplit = 0;
    scriptVariable102 = scriptVariable0 = 0;
    memset(scriptVariableIndices, 0, sizeof(scriptVariableIndices));
    scriptVariableReads = musicPitchCalls = musicTempoCalls = musicCallOrder = 0;
    memset(&musicPitch, 0, sizeof(musicPitch));
    memset(&musicTempo, 0, sizeof(musicTempo));
    return failed;
}
#endif

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char path[1200]; snprintf(path, sizeof(path), "%s/bugfixes.ini", argv[1]);
    testSetEnv("XG_SETTINGS", path);
    testSetEnv("XG_DEUS_CAMERA_FIX", "");
    if (!strcmp(argv[2], "write")) {
        testSetEnv("XG_ORIGINAL_BUG_FIXES", "off");
        pcGameplaySetOriginalBugFixes(1);
        CHECK(pcGameplayOriginalBugFixes());
        CHECK(pcGameplayDeusCameraFix());
        pcGameplaySetDeusCameraFix(0);
        CHECK(!pcGameplayDeusCameraFix());
    } else if (!strcmp(argv[2], "read")) {
        CHECK(pcGameplayOriginalBugFixes());
        CHECK(!pcGameplayDeusCameraFix());
        pcGameplaySetOriginalBugFixes(0);
        CHECK(!pcGameplayOriginalBugFixes());
        CHECK(!pcGameplayDeusCameraFix());
    } else {
        CHECK(!pcGameplayOriginalBugFixes());
        CHECK(pcGameplayDeusCameraFix());
        testSetEnv("XG_ORIGINAL_BUG_FIXES", "true"); CHECK(pcGameplayOriginalBugFixes());
        testSetEnv("XG_ORIGINAL_BUG_FIXES", "yes"); CHECK(pcGameplayOriginalBugFixes());
        testSetEnv("XG_ORIGINAL_BUG_FIXES", "off"); CHECK(!pcGameplayOriginalBugFixes());
        testSetEnv("XG_ORIGINAL_BUG_FIXES", "invalid"); CHECK(!pcGameplayOriginalBugFixes());
#ifndef _WIN32
        context.m8_gearFrames = &frame; context.mC_gearEngines = &engine;
        context.m10_gearArmors = &armor; context.m14_gearAccessories = accessories;
        context.m18_gearWeapons = &weapon;
        frame.m4_hp = 5000; frame.m8_weight = 800; frame.m14_agility = 10;
        frame.m15_etherAmp = 12; frame.m17_response = 20;
        engine.m6_fuel = 1000; engine.mC_output = 70; engine.mE_attackMultiplier = 5;
        armor.m8_defence = 100; armor.mA_etherDefence = 20; weapon.mE_power = 2;
        for (int i = 1; i < 4; ++i) {
            accessories[i].m1B_etherAmp = i == 1 ? 10 : i == 2 ? 10 : 5;
            accessories[i].mD_defence = i; accessories[i].mE_etherDefence = i + 1;
            accessories[i].m6_weight = 120 * i; accessories[i].m14_response = i;
            accessories[i].m10_terrainAgility[0] = i;
        }
        accessories[2].m15_type = 4; accessories[2].m16_value = 0x8000;
        accessories[2].m1A_resistanceAmount = 3;
        for (int id = 0; id < 20; ++id) {
            for (int empty = 0; empty < 2; ++empty) {
                memset(&gameState, 0, sizeof(gameState));
                GameStateGear* gear = &gameState.m978_gears[id];
                gameState.m26C_party[0].mA0_gearNum = id;
                gear->m60_hp = 4500; gear->m38_fuel = 800; gear->m75_weightAllowance = 2;
                gear->m54_accessoryEtherAmp = 99; /* discarded by a native Parts visit */
                for (int i = 0; i < 3; ++i) gear->m9_accessorySlots[i] = empty ? 0 : i + 1;
                GameState before = gameState;
                testSetEnv("XG_ORIGINAL_BUG_FIXES", "off");
                applyAllGearPartStats(&context, id); computeGearStatusStats(&context, id);
                CHECK(gear->m54_accessoryEtherAmp == 0);
                CHECK(context.m9C_gearStats.m18_etherAmp == 12);
                GameState native = gameState;
                MenuGearStats nativeStats = context.m9C_gearStats;
                buildSaveGameData(&nativeSave);
                gameState = before;
                testSetEnv("XG_ORIGINAL_BUG_FIXES", "on");
                for (int repeat = 0; repeat < 3; ++repeat) {
                    applyAllGearPartStats(&context, id); computeGearStatusStats(&context, id);
                    CHECK(gear->m54_accessoryEtherAmp == (empty ? 0 : 25));
                    CHECK(context.m9C_gearStats.m18_etherAmp == (empty ? 12 : 37));
                    GameState comparison = gameState;
                    comparison.m978_gears[id].m54_accessoryEtherAmp = 0;
                    CHECK(!memcmp(&comparison, &native, sizeof(native)));
                    MenuGearStats stats = context.m9C_gearStats;
                    stats.m18_etherAmp = 12;
                    CHECK(!memcmp(&stats, &nativeStats, sizeof(stats)));
                }
                /* Derived Ether amp is absent from the original save format. */
                buildSaveGameData(&fixedSave);
                CHECK(!memcmp(&nativeSave, &fixedSave, sizeof(fixedSave)));
                gear->m54_accessoryEtherAmp = 99;
                gear->m60_hp = 0; gear->m38_fuel = 0;
                restoreSaveGameData(&fixedSave, &context);
                CHECK(gear->m54_accessoryEtherAmp == (empty ? 0 : 25));
                CHECK(gear->m60_hp == 4500 && gear->m38_fuel == 800);
                buildSaveGameData(&loadedSave);
                CHECK(!memcmp(&fixedSave, &loadedSave, sizeof(loadedSave)));
                /* Actual battle initialization and Ether damage consume that bonus. */
                for (int enabled = 0; enabled < 2; ++enabled) {
                    testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
                    applyAllGearPartStats(&context, id);
                    memset(&battleEntities, 0, sizeof(battleEntities));
                    memset(battleCharacters, 0x7f, sizeof(battleCharacters));
                    battleCharacters[0] = 0;
                    battleEntities.m0_entities[0].m0_base = gameState.m26C_party[0];
                    battleEntities.m0_entities[0].mA4_gear = *gear;
                    initPartyBattleStats();
                    int amp = 12 + (enabled && !empty ? 25 : 0);
                    CHECK(battleEntities.m0_entities[0].mA4_gear.m9E_etherAmp == amp);
                    battleGetSlotStatusSub_currentBattleEntity = &battleEntities.m0_entities[0];
                    battleGetSlotStatusSub_currentBattleEntityGear = &battleEntities.m0_entities[0].mA4_gear;
                    battleGetSlotStatusSub_currentBattleEntity->m0_base.m5B_ether = 20;
                    currentEntityBattleStats = &attack;
                    attack.mA_attackFlags = 0x100; attack.m1A_attackFormula = 1;
                    CHECK(computeGearBattleDamageSub1() == amp * 5);
                }
                /* Removing the accessories and refreshing clears the fixed bonus. */
                memset(gear->m9_accessorySlots, 0, 3);
                applyAllGearPartStats(&context, id); computeGearStatusStats(&context, id);
                CHECK(gear->m54_accessoryEtherAmp == 0 && context.m9C_gearStats.m18_etherAmp == 12);
            }
        }
        CHECK(testBilly() == 0);
        CHECK(testDeusCamera() == 0);
        CHECK(testShopCursor() == 0);
        CHECK(testPartyCardLabels() == 0);
        CHECK(testFieldSfx() == 0);
        CHECK(testNisanInn() == 0);
        CHECK(testWardRest() == 0);
        CHECK(testSoldierFade() == 0);
        CHECK(testOrphanageFacing() == 0);
        CHECK(testEmeraldaTheme() == 0);
        CHECK(testEllyTheme() == 0);
#endif
    }
    printf("original bug fixes %s: %d checks passed\n", argv[2], checks);
    return 0;
}
