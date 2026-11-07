#ifndef MAIN_GAME_STATE_H
#define MAIN_GAME_STATE_H

/* Persistent game state (Noah: kernel/gameState.cpp). */

#include "common.h"

/* Noah: sGameStateA4Sub4. The menu's equipment code writes the second
 * weapon (GameStateCharacter + 0x18: u16 status mask, u8 status chance,
 * u8 status type, then its power at m4[3].m0) through here, so the tail is
 * split: the 8-byte weapon records really start at the character's +0. */
typedef struct {
    /* 0x0 */ u8 m0_weaponPower;             /* m4[0].m0: weapon power (Noah), m4[3].m0: second weapon power */
    /* 0x1 */ u8 m1[3];
    /* 0x4 */ u16 m4_nextWeaponStatusMask;            /* m4[2].m4: second weapon's status mask (character + 0x18) */
    /* 0x6 */ u8 m6_nextWeaponStatusChance;             /* m4[2].m6: character + 0x1A */
    /* 0x7 */ u8 m7_nextWeaponStatusType;             /* m4[2].m7: second weapon's status type (character + 0x1B) */
} GameStateCharacterSub4;

/* Noah: sGameStateA4 (party member; battle: sBattleEntity.m0_base) */
typedef struct {
    /* 0x00 */ u16 m0_weaponStatusMask;
    /* 0x02 */ u8 m2_weaponStatusChance;
    /* 0x03 */ u8 m3_weaponStatusType;
    /* 0x04 */ GameStateCharacterSub4 m4_weaponSlots[4];
    /* 0x24 */ u8 m24[4];
    /* 0x28 */ u8 m28_attackBonus;
    /* 0x29 */ u8 m29_defenceBonus;
    /* 0x2A */ u8 m2A_agilityBonus;
    /* 0x2B */ u8 m2B_etherBonus;
    /* 0x2C */ u8 m2C_etherDefenceBonus;
    /* 0x2D */ u8 m2D_accessoryDefence;
    /* 0x2E */ u8 m2E_hitBonus;
    /* 0x2F */ u8 m2F_evadeBonus;
    /* 0x30 */ u8 m30_maxHpBonus;
    /* 0x31 */ u8 m31_maxMpBonus;
    /* 0x32 */ u16 m32_accessoryFlags;
    /* 0x34 */ u16 m34_monsterFlags;          /* Noah: s16; battle tests bits (& 0x400, & 0x800) */
    /* 0x36 */ u16 m36_monsterFlags2;
    /* 0x38 */ u16 m38_weaknessFlags;
    /* 0x3A */ s16 m3A_killCount;
    /* 0x3C */ s32 m3C_experience;
    /* 0x40 */ s32 m40_experience2;
    /* 0x44 */ s32 m44_expToNextLevel;
    /* 0x48 */ s32 m48_expToNextLevel2;
    /* 0x4C */ u16 m4C_HP;
    /* 0x4E */ u16 m4E_MaxHP;
    /* 0x50 */ u16 m50_MP;
    /* 0x52 */ u16 m52_MaxMP;
    /* 0x54 */ u8 m54_infinityCharge;
    /* 0x55 */ u8 m55_deathblowLearnRate;
    /* 0x56 */ u8 m56_battleCommandLoadout;
    /* 0x57 */ u8 m57;
    /* 0x58 */ u8 m58_attack;
    /* 0x59 */ u8 m59_defence;
    /* 0x5A */ u8 m5A_agility;
    /* 0x5B */ u8 m5B_ether;
    /* 0x5C */ u8 m5C_etherDefence;
    /* 0x5D */ u8 m5D;
    /* 0x5E */ u8 m5E_hitPercentage;
    /* 0x5F */ u8 m5F_evadePercentage;
    /* 0x60 */ u8 m60_effectChance;
    /* 0x61 */ u8 m61;
    /* 0x62 */ u8 m62_Level;
    /* 0x63 */ u8 m63_Level2;
    /* 0x64 */ u8 m64;
    /* 0x65 */ u8 m65;
    /* 0x66 */ u8 m66;
    /* 0x67 */ u8 m67;
    /* 0x68 */ u8 m68[2];
    /* 0x6A */ u8 m6A_weaponModifier[5]; /* the equip menu copies 5 bytes (Noah: s8 m6A_weaponModifier) */
    /* 0x6F */ u8 m6F_equipment[5];      /* [0] weapon (inventory 1); the equip menu copies 5 bytes */
    /* 0x74 */ u8 m74_accessorySlots[3];
    /* 0x77 */ u8 m77_baseWeight;
    /* 0x78 */ u8 m78_weightGain;
    /* 0x79 */ u8 m79_weightTenths;
    /* 0x7A */ u16 m7A_commandEnabledBF;
    /* 0x7C */ u16 m7C_statusFlags[4];       /* battle status words; [0] & 0xC000: down. Noah: m7C, m7E, m80, m82
                                  * (battle clears [0] and [2] in a loop, i += 2) */
    /* 0x84 */ u16 m84_statusFlags2[6];       /* Noah: m84, m86, m88, m8A, m8C, m8E (cleared the same way) */
    /* 0x90 */ u16 m90_deathblowProgress[8];
    /* 0xA0 */ u8 mA0_gearNum;
    /* 0xA1 */ u8 mA1_deathblowLearnBonus;
    /* 0xA2 */ u8 mA2[2];
} GameStateCharacter; /* 0xA4 */

/* One 8-byte gear equipment slot at gear + 0x12 + 8 * slot. The menu
 * fills weapon k from its gear weapon table: the status-effect mask (u16) at
 * gear + 0x10 + 8k (m10_weaponStatusMask for k = 0, else
 * m12_slots[k - 1].m6_nextWeaponStatusMask), then power, status chance and
 * status type. Battle rolls the weapon's status effect from these. */
typedef struct {
    /* 0x0 */ u8 m0_weaponPower;             /* weapon power */
    /* 0x1 */ u8 m1_weaponStatusChance;
    /* 0x2 */ u8 m2_weaponStatusType;             /* status type (100: the mask is also ORed into m86) */
    /* 0x3 */ u8 m3_weaponId;
    /* 0x4 */ u8 m4[2];
    /* 0x6 */ u16 m6_nextWeaponStatusMask; /* status mask of the next slot's weapon */
} GameStateGearSlot;

/* Noah: sGameStateA42 (gear; battle: sBattleEntity.mA4_gear) */
typedef struct {
    /* 0x00 */ u8 m0[2];
    /* 0x02 */ u8 m2_frame;           /* index into the menu's gear frame table */
    /* 0x03 */ u8 m3_engine;          /* index into the menu's gear engine table */
    /* 0x04 */ u8 m4_slotItem[4];     /* item per equipment slot (gears 5/13 use [0], [1], [3]) */
    /* 0x08 */ u8 m8_armor;           /* index into the menu's gear armor table */
    /* 0x09 */ u8 m9_accessorySlots[3];
    /* 0x0C */ u8 mC_weapon[4];       /* [0] index into the menu's gear weapon table;
                                       * the equip menu copies all 4 bytes */
    /* 0x10 */ s16 m10_weaponStatusMask;
    /* 0x12 */ GameStateGearSlot m12_slots[4]; /* Noah: s16 m12_maxEther at 0x12 (battle reads its low byte) */
    /* 0x32 */ u8 m32[0x38 - 0x32];
    /* 0x38 */ u16 m38_fuel;    /* Noah: s32 m38_fuel; OP_RESTORE_GEAR copies u16 0x3A -> 0x38 */
    /* 0x3A */ u16 m3A_maxFuel;
    /* 0x3C */ u8 m3C_engineOutput;
    /* 0x3D */ u8 m3D[2];
    /* 0x3F */ u8 m3F_attackMultiplier;
    /* 0x40 */ u16 m40_accessoryDefence;               /* accessory bonuses (menu func_801E433C) */
    /* 0x42 */ u16 m42_accessoryEtherDefence;
    /* 0x44 */ u16 m44_accessoryWeight;
    /* 0x46 */ u16 m46;               /* battle start adds m44 + m46 to m68 */
    /* 0x48 */ u16 m48;
    /* 0x4A */ u8 m4A_agilityPenalty;
    /* 0x4B */ u8 m4B;
    /* 0x4C */ u8 m4C;
    /* 0x4D */ u8 m4D_accessoryResponse;
    /* 0x4E */ u8 m4E_accessoryAgility;
    /* 0x4F */ u8 m4F_frameRepairLevel;
    /* 0x50 */ u8 m50_terrainAgility[4];
    /* 0x54 */ u8 m54_accessoryEtherAmp;
    /* 0x55 */ u8 m55_accessoryBonuses[3];             /* cleared together by the menu */
    /* 0x58 */ u8 m58[0x5C - 0x58];
    /* 0x5C */ u8 m5C_extraFileIndex;
    /* 0x5D */ u8 m5D_weaponAnimOffsets[3];
    /* 0x60 */ s32 m60_hp;
    /* 0x64 */ s32 m64_maxHp;
    /* 0x68 */ u16 m68_weight;
    /* 0x6A */ u16 m6A;
    /* 0x6C */ u16 m6C;
    /* 0x6E */ u16 m6E_elementResistanceMask;
    /* 0x70 */ u16 m70_defence;
    /* 0x72 */ u16 m72_etherDefence;
    /* 0x74 */ u8 m74_baseAttackMultiplier;
    /* 0x75 */ u8 m75_weightAllowance;
    /* 0x76 */ u8 m76[0x7C - 0x76];
    /* 0x7C */ u16 m7C_statusFlags;         /* battle status bits, as in GameStateCharacter */
    /* 0x7E */ u16 m7E_statusFlags1;
    /* 0x80 */ u16 m80_statusFlags2;
    /* 0x82 */ u16 m82_statusFlags3;
    /* 0x84 */ u16 m84_statusFlags4;
    /* 0x86 */ u16 m86_statusFlags5;
    /* 0x88 */ u8 m88_elementResistances[0x98 - 0x88];   /* per-bit accessory bonuses (menu) */
    /* 0x98 */ u8 m98_agility;
    /* 0x99 */ u8 m99_defenceDown;
    /* 0x9A */ u8 m9A[2];
    /* 0x9C */ u8 m9C;
    /* 0x9D */ u8 m9D;
    /* 0x9E */ u8 m9E_etherAmp;
    /* 0x9F */ u8 m9F_response;
    /* 0xA0 */ u8 mA0[0xA4 - 0xA0];
} GameStateGear; /* 0xA4 */

/* Noah: sGameStateSize20 */
typedef struct {
    /* 0x00 */ u16 m0_unlockedAbilitiesBitField[8];
    /* 0x10 */ u8 m10[0x17 - 0x10];
    /* 0x17 */ u8 m17_energyPerTurn; /* Noah name */
    /* 0x18 */ u8 m18[2];
    /* 0x1A */ u16 m1A_unlockedGearAbilities;
    /* 0x1C */ u8 m1C[4];
} GameStateSize20; /* 0x20 */

/* Noah: sGameState (gameState at 0x8006D634, file 3 in dir 0x10, 0x2358 bytes) */
typedef struct {
    /* 0x0000 */ u8 m0_names[31][0x14];
    /* 0x026C */ GameStateCharacter m26C_party[11];
    /* 0x0978 */ GameStateGear m978_gears[20];
    /* 0x1648 */ u8 m1648[0x16C0 - 0x1648];
    /* 0x16C0 */ GameStateSize20 m16C0_abilities[11];
    /* 0x1820 */ u16 m1820_worldmapPosition[6]; /* Noah: s16 array; the world map reads it with lhu */
    /* 0x182C */ u16 m182C_vehiclePosition[4];  /* Noah: SVECTOR m182C */
    /* 0x1834 */ u16 m1834_vehicleMode;
    /* 0x1836 */ u16 m1836_restoreWorldmapPosition;      /* 1 while the world map hands over to a menu */
    /* 0x1838 */ u8 m1838[0x183C - 0x1838];
    /* 0x183C */ u16 m183C_isOnGearBackup[3]; /* the world map copies m22B1 here before a menu */
    /* 0x1842 */ u16 m1842_disableWorldmapMinimap;
    /* 0x1844 */ u16 m1844_waypointShipPosition[2];
    /* 0x1848 */ u16 m1848_waypointShipWaypoint;
    /* 0x184A */ u16 m184A_waypointShipPlaced;
    /* 0x184C */ u16 m184C_flyingShipXLow;
    /* 0x184E */ u16 m184E_flyingShipPosition[5];  /* Noah: SVECTOR m184E (+ 0x1856) */
    /* 0x1858 */ u8 m1858[0x1924 - 0x1858];
    /* 0x1924 */ s32 m1924_gold;
    /* 0x1928 */ u8 m1928[0x1930 - 0x1928];
    /* 0x1930 */ u16 m1930_fieldVarsBackup[512];
    /* 0x1D30 */ u16 m1D30_partyMemberBitField;
    /* 0x1D32 */ u16 m1D32_partyFrameMask;
    /* 0x1D34 */ u8 m1D34_currentParty[3];
    /* 0x1D37 */ u8 m1D37;
    /* Inventory: item id = category << 8 | index; per category a count
     * array then an id array (getInventoryItemCounts/getInventoryItemIds). */
    /* 0x1D38 */ u8 m1D38_inventory1Counts[100];
    /* 0x1D9C */ u8 m1D9C_inventory1Ids[100];
    /* 0x1E00 */ u8 m1E00_inventory2Counts[200];
    /* 0x1EC8 */ u8 m1EC8_inventory2Ids[200];
    /* 0x1F90 */ u8 m1F90_inventory0Counts[150];
    /* 0x2026 */ u8 m2026_inventory0Ids[150];
    /* 0x20BC */ u8 m20BC_inventory3Counts[100];
    /* 0x2120 */ u8 m2120_inventory3Ids[100];
    /* 0x2184 */ u8 m2184_inventory4Counts[150];
    /* 0x221A */ u8 m221A_inventory4Ids[150];
    /* 0x22B0 */ u8 m22B0;
    /* 0x22B1 */ u8 m22B1_isOnGear[3];  /* Noah: m22B1_isOnGear */
    /* 0x22B4 */ u8 m22B4[0x22B6 - 0x22B4];
    /* 0x22B6 */ u16 m22B6_gameFlags;
    /* 0x22B8 */ u8 m22B8_ammoCounts[0x2318 - 0x22B8]; /* ammo left per ammo item (0..100), reached through the
                                                          * two biased bases below; retail labels D_8006F8BA/D_8006F8EA */
    /* 0x2318 */ u16 m2318_partyFrameLock;
    /* 0x231A */ u16 m231A_fieldID;
    /* 0x231C */ u16 m231C_cameraYaw;
    /* 0x231E */ s16 m231E_worldmapInitialPositionIndex;
    /* 0x2320 */ u16 m2320_worldmapMode;
    /* 0x2322 */ u16 m2322_previouslyPlayingMusic;
    /* 0x2324 */ u16 m2324_battleScriptVars[16]; /* battleScriptGameStateVars[0..15], kept in the save */
    /* 0x2344 */ u8 m2344[0x2358 - 0x2344];
} GameState;

extern GameState gameState;   /* Noah: gameState */

/* The ammo counts (m22B8_ammoCounts) are indexed by item id through two bases the
 * retail code folded into the address (xg-6gs.7): character weapon item ids
 * (Billy's guns, from 0x32) and gear slot item ids (from 2). Both bases sit
 * before the table; the constant expression keeps the original codegen. */
#define gameStateAmmoByWeaponItem ((u8*)&gameState + 0x2286)
#define gameStateAmmoByGearItem ((u8*)&gameState + 0x22B6)

#endif /* MAIN_GAME_STATE_H */
