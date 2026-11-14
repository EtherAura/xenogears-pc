/* Exercise native equipment, save and battle paths behind the shared option. */
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
    for (int enabled = 0; enabled < 2; ++enabled)
    for (int scenario = 0; scenario < 8; ++scenario) {
        testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
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
s32 setupStringInPolyFT4(struct Font* font, s32 glyphIndex, void* polys, s32 oddOrEven, s16 x, s16 y, u16 scale) {
    (void)font; (void)polys; (void)oddOrEven; (void)scale;
    if (glyphIndex == 0x108) { arrowX = x; arrowY = y; }
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
s32 getVariable(s32 varIndex) { (void)varIndex; ++scriptVariableReads; return 0; }
void setMusicInstancePitch(SoundInstance* inst, s32 val, s32 dur) { (void)inst; (void)val; (void)dur; ++musicPitchCalls; }
void setMusicInstanceTempo(SoundInstance* inst, s32 tempo, s32 dur) { (void)inst; (void)tempo; (void)dur; ++musicTempoCalls; }

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
#endif

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char path[1200]; snprintf(path, sizeof(path), "%s/bugfixes.ini", argv[1]);
    testSetEnv("XG_SETTINGS", path);
    if (!strcmp(argv[2], "write")) {
        testSetEnv("XG_ORIGINAL_BUG_FIXES", "off");
        pcGameplaySetOriginalBugFixes(1);
        CHECK(pcGameplayOriginalBugFixes());
    } else if (!strcmp(argv[2], "read")) {
        CHECK(pcGameplayOriginalBugFixes());
        pcGameplaySetOriginalBugFixes(0);
        CHECK(!pcGameplayOriginalBugFixes());
    } else {
        CHECK(!pcGameplayOriginalBugFixes());
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
        CHECK(testFieldSfx() == 0);
        CHECK(testNisanInn() == 0);
        CHECK(testWardRest() == 0);
#endif
    }
    printf("original bug fixes %s: %d checks passed\n", argv[2], checks);
    return 0;
}
