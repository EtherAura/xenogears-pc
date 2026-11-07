/*
 * Shared helpers: submenu cleanup, item and equipment stats, gear stats, save data
 * build/restore, debug item effects, menu box and memory card screen primitives,
 * label rendering. Provisional file 0x801E3088-0x801E8018; owns .rodata
 * 0x801C51DC-0x801C5278. gcc-2.6.3-psx -O2 -G0, ASPSX 2.34.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "menu/menu.h"
#include "main/game_state.h"
#include "main/memory.h"
#include "pc_gameplay.h"
#include "pc_glyph.h"

void debugGiveAllItems(void);
void debugUnlockAllAbilities(void);
extern u8 gearPilotIds[20]; /* gear -> pilot (party member) for the 20 gears (gears 17-19: 15, past the 11 characters, as on the PS1) */
void applyGearAccessoryBonuses(MenuContext_330* ctx, u8 gearId);
void applyGearWeaponStats(MenuContext_330* ctx, u8 gearId);
void applyGearFrameStats(MenuContext_330* ctx, u8 gearId);
void applyGearArmorStats(MenuContext_330* ctx, u8 gearId);
void applyGearEngineStats(MenuContext_330* ctx, u8 gearId);
u8 computeGearAgilityPenalty(u8 gearId);
void initMenuContext3A8_MemoryCardPolys(s32 tileId);
void initMenuContext3A8_Lines(s32 tileId);
extern s32 memoryCardSlotsX[];
extern s32 memoryCardSlotsY[];
extern s32 saveScreenIconX[]; /* memory card screen: save icon frame x */
extern s32 saveScreenIconY[]; /* memory card screen: save icon frame y */
extern u16 textSpriteMode0; /* textSpriteMode0 (CLUT) */
extern s32 saveSlotLabelGlyphs[]; /* glyphs of the save slot label, 0xFFFF = none */
extern s32 saveSlotLabelPositionX[]; /* label glyph x */
extern s32 saveSlotLabelPositionY[]; /* label glyph y */
void initMenuContext34C_polys(void);
void initMenuContext34C_polys2(void);
u16* func_800405C4(s32 sjis); /* BIOS B(51h) Krom2RawAdd */
extern u8 kanjiGlyphIsDoubleByte;  /* 1 while the glyph comes from the kanji ROM as is */
extern u16 asciiToSjisTable[0x80] PORT_ALIAS; /* ASCII -> SJIS (the codes 20h..7Fh, right after infocardNameV) */
void narrowGlyphTo12Pixels(u8* p);
u16* getKanjiRomGlyph(u8* str);
extern s32 savePlayTimeGlyphX[9]; /* play time glyph x positions */
extern s32 saveSlotMemberGlyphX[]; /* leader name x per slot */
extern s32 saveSlotMemberGlyphY[]; /* leader name y per slot */
extern s32 saveSlotLevelPositionX;   /* level x */
extern s32 saveSlotLevelPositionY;   /* level y */
extern s32 saveSlotHpPositionX;   /* HP x */
extern s32 saveSlotHpPositionY;   /* HP y */
extern s32 saveSlotMaxHpPositionX;   /* max HP x */
extern s32 saveSlotMaxHpPositionY;   /* max HP y */
extern s32 saveSlotMpPositionX;   /* MP x */
extern s32 saveSlotMpPositionY;   /* MP y */
extern s32 saveSlotMaxMpPositionX;   /* max MP x */
extern s32 saveSlotMaxMpPositionY;   /* max MP y */
void decodeCharacterCodes(u16* codes, u8* out, u32 n);                 /* main dialog.c */
s32 renderString(u8* text, u16* image, s16 width, s32 odd);    /* main dialog.c: render text to a 4bpp image */
extern s32 saveTitleGlyphX; /* save name glyphs x */
extern s32 saveTitleGlyphY; /* save name glyphs y */
void setupSaveSlotLabels(void);
void setupSavePlayTimeAndNumber(MenuSaveSummary* info);
void setupSaveSlotNameSprite(u8 slot, MenuSaveSummary* info);
void setupSaveSlotLevel(u8 slot, MenuSaveSummary* info);
void setupSaveSlotHp(u8 slot, MenuSaveSummary* info);
void setupSaveSlotMp(u8 slot, MenuSaveSummary* info);
void renderSaveSlotNameImage(u8 slot, MenuSaveSummary* info, s32 cardSlot);
void setupSaveTitleGlyphQuads(void);
void renderSaveTitleImage(s32 slot);
void setupSaveSummaryDisplay(s32 cardSlot);
extern u16 memoryCardIconPaletteStaging[16];
extern RECT tempRectForMemoryCardIconLoading;
extern RECT tempRectForMemoryCardIconPaletteLoading;
extern u16 textSpriteMode1; /* textSpriteMode1 (CLUT) */
u8* getDialogParamPointer(u8* buf, s32 idx);

/* submenu cleanup after menu2_executeMainMenuSelection */
void cleanupMainMenuSelection(u8 arg0) {
    switch (gMenuContext->m336_menuSelectedEntry + arg0) {
    case 0:
    case 7:
    case 9:
        break;
    case 1:
    case 8:
        endMemoryCardSession();
        break;
    case 2:
        gMenuContext->m33C_drawFlags->m7_drawCurrentSelectedCharacterInfoCard = 0;
        gMenuContext->m33C_drawFlags->m8_drawCurrentSelectedCharacterStats = 0;
        gMenuContext->m33C_drawFlags->m4B_draw360 = 0;
        freeGearStatusMenu();
        break;
    case 3:
        freeEtherListMenu(0);
        break;
    case 4:
        freeItemMenu();
        break;
    case 5:
        freeEquipMenu();
        freeStatusStatsAndEquipNames();
        break;
    case 6:
        gMenuContext->m33C_drawFlags->m7_drawCurrentSelectedCharacterInfoCard = 0;
        gMenuContext->m33C_drawFlags->m8_drawCurrentSelectedCharacterStats = 0;
        gMenuContext->m33C_drawFlags->m4B_draw360 = 0;
        hideSubMenuLabels();
        freeStatusMenu();
        break;
    }
}

/* use an item on a character: HP/MP heal, stat boosts, status; returns 1 when nothing could be healed */
s32 applyItemToCharacter(MenuContext_330* ctx, u8 charId, u8 itemId) {
    MenuUsableItem* item;
    GameStateCharacter* c;
    u8 hpFull;
    u8 mpFull;
    s32 hpRate;
    s32 mpRate;

    hpFull = 0;
    mpFull = 0;
    item = ctx->m1C_usableItems;
    item += itemId;
    c = &gameState.m26C_party[charId];
    mpRate = 10;
    if (item->mA_flags & 0x8000) {
        if (c->m4C_HP == c->m4E_MaxHP) {
            hpFull = 1;
        } else {
            hpRate = 50;
            c->m4C_HP += item->m8_amount * hpRate;
        }
    }
    if (item->mA_flags & 0x4000) {
        if (c->m50_MP == c->m52_MaxMP) {
            mpFull = 1;
        } else {
            c->m50_MP += mpRate * item->m8_amount;
        }
    }
    if (c->m4C_HP > c->m4E_MaxHP) {
        c->m4C_HP = c->m4E_MaxHP;
    }
    if (c->m50_MP > c->m52_MaxMP) {
        c->m50_MP = c->m52_MaxMP;
    }
    if (item->mA_flags & 4) {
        if (item->mC_flags & 0x8000) {
            c->m58_attack += item->m8_amount;
        }
        if (item->mC_flags & 0x4000) {
            c->m59_defence += item->m8_amount;
        }
        if (item->mC_flags & 0x2000) {
            c->m5B_ether += item->m8_amount;
        }
        if (item->mC_flags & 0x1000) {
            c->m5C_etherDefence += item->m8_amount;
        }
        if (item->mC_flags & 0x800) {
            c->m4E_MaxHP += item->m8_amount;
        }
        if (item->mC_flags & 0x400) {
            c->m52_MaxMP += item->m8_amount;
        }
        if (c->m58_attack > 200) {
            c->m58_attack = 200;
        }
        if (c->m59_defence > 200) {
            c->m59_defence = 200;
        }
        if (c->m5B_ether > 200) {
            c->m5B_ether = 200;
        }
        if (c->m5C_etherDefence > 200) {
            c->m5C_etherDefence = 200;
        }
        if (c->m4E_MaxHP > 999) {
            c->m4E_MaxHP = 999;
        }
        if (c->m52_MaxMP > 99) {
            c->m52_MaxMP = 99;
        }
    }
    if (item->mA_flags & 2) {
        if (item->mC_flags & 0x8000) {
            c->m78_weightGain += (u8)item->mC_flags;
            if (c->m78_weightGain > 200) {
                c->m78_weightGain = 200;
            }
        } else if (c->m78_weightGain < (item->mC_flags & 0xFF)) {
            c->m78_weightGain = 0;
        } else {
            c->m78_weightGain -= (u8)item->mC_flags;
        }
    }
    if (item->mA_flags & 1) {
        switch (item->m8_amount) {
        case 1:
            debugGiveAllItems();
            break;
        case 2:
            debugUnlockAllAbilities();
            break;
        }
    }
    if (item->mA_flags & 0x8000) {
        if (item->mA_flags & 0x4000) {
            if (hpFull && mpFull) {
                return mpFull;
            }
            return 0;
        }
        return hpFull;
    }
    if (item->mA_flags & 0x4000) {
        return mpFull;
    }
    return 0;
}

/* ether heal: HP += caster ether * spell power, or gear HP += max/10 */
void applyEtherHeal(MenuContext_330* ctx, u8 casterId, u8 targetId, u8 etherId, u8 onGear) {
    GameStateCharacter* caster;
    GameStateCharacter* target;
    GameStateGear* gear;
    MenuEtherData* ether;

    caster = &gameState.m26C_party[casterId];
    target = &gameState.m26C_party[targetId];
    gear = &gameState.m978_gears[gameState.m26C_party[casterId].mA0_gearNum];
    if (onGear == 0) {
        ether = ctx->m20_perCharacter[casterId];
        ether += etherId;
        target->m4C_HP += caster->m5B_ether * ether->m11_power;
        if (target->m4C_HP > target->m4E_MaxHP) {
            target->m4C_HP = target->m4E_MaxHP;
        }
    } else {
        gear->m60_hp += (u32)gear->m64_maxHp / 10;
        if ((u32)gear->m60_hp > (u32)gear->m64_maxHp) {
            gear->m60_hp = gear->m64_maxHp;
        }
    }
}

/* recompute a character's equipment bonuses (accessories m74[3], weapons m6A/m6F) */
void setupStatusMenuForCharacter1(MenuContext_330* ctx, u8 charId) {
    GameStateCharacter* c;
    MenuAccessoryStats* acc;
    MenuWeaponStats* weapon;
    u8 i;
    u8 value;

    c = &gameState.m26C_party[charId];
    c->m2D_accessoryDefence = 0;
    c->m32_accessoryFlags = 0;
    c->m28_attackBonus = 0;
    c->m29_defenceBonus = 0;
    c->m2A_agilityBonus = 0;
    c->m2B_etherBonus = 0;
    c->m2C_etherDefenceBonus = 0;
    c->m2E_hitBonus = 0;
    c->m2F_evadeBonus = 0;
    c->m30_maxHpBonus = 0;
    c->m31_maxMpBonus = 0;
    c->m7C_statusFlags[1] = 0;
    c->m7C_statusFlags[3] = 0;
    c->m84_statusFlags2[1] = 0;
    c->m84_statusFlags2[3] = 0;
    c->m84_statusFlags2[5] = 0;
    c->mA1_deathblowLearnBonus = 0;
    for (i = 0; i < 3; i++) {
        acc = ctx->m4_accessoryData;
        acc += c->m74_accessorySlots[i];
        c->m2D_accessoryDefence += acc->m8_defence;
        switch (acc->m9_type) {
        case 1:
            c->m7C_statusFlags[1] |= acc->mA_value;
            break;
        case 2:
            c->m7C_statusFlags[3] |= acc->mA_value;
            break;
        case 3:
            c->m84_statusFlags2[1] |= acc->mA_value;
            break;
        case 4:
            c->m84_statusFlags2[3] |= acc->mA_value;
            break;
        case 7:
            c->m84_statusFlags2[5] |= acc->mA_value;
            break;
        case 5:
            c->m32_accessoryFlags |= acc->mA_value;
            break;
        case 8:
        case 9:
            c->m30_maxHpBonus += acc->mA_value;
            break;
        case 10:
            c->mA1_deathblowLearnBonus += acc->mA_value;
            break;
        }
        value = acc->mC_flags;
        if (acc->mC_flags & 0x8000) {
            c->m28_attackBonus += value;
        }
        if (acc->mC_flags & 0x4000) {
            c->m29_defenceBonus += value;
        }
        if (acc->mC_flags & 0x2000) {
            c->m2A_agilityBonus += value;
        }
        if (acc->mC_flags & 0x1000) {
            c->m2B_etherBonus += value;
        }
        if (acc->mC_flags & 0x800) {
            c->m2C_etherDefenceBonus += value;
        }
        if (acc->mC_flags & 0x400) {
            c->m2E_hitBonus += value;
        }
        if (acc->mC_flags & 0x200) {
            c->m2F_evadeBonus += value;
        }
        if (acc->mC_flags & 0x100) {
            c->m2D_accessoryDefence += value;
        }
    }
    weapon = ctx->m0_weaponStats;
    weapon += c->m6A_weaponModifier[0];
    c->m4_weaponSlots[0].m0_weaponPower = weapon->mC_power;
    c->m0_weaponStatusMask = weapon->m8_statusMask;
    c->m2_weaponStatusChance = weapon->mA_statusChance;
    c->m3_weaponStatusType = weapon->mB_statusType;
    if (c->m56_battleCommandLoadout == 4) {
        weapon = ctx->m0_weaponStats;
        weapon += c->m6F_equipment[0];
        c->m4_weaponSlots[0].m0_weaponPower = weapon->mC_power;
        c->m0_weaponStatusMask = weapon->m8_statusMask;
        c->m2_weaponStatusChance = weapon->mA_statusChance;
        c->m3_weaponStatusType = weapon->mB_statusType;
        weapon = ctx->m0_weaponStats;
        weapon += c->m6F_equipment[3];
        c->m4_weaponSlots[3].m0_weaponPower = weapon->mC_power;
        c->m4_weaponSlots[2].m4_nextWeaponStatusMask = weapon->m8_statusMask;
        c->m4_weaponSlots[2].m6_nextWeaponStatusChance = weapon->mA_statusChance;
        c->m4_weaponSlots[2].m7_nextWeaponStatusType = weapon->mB_statusType;
        /* Billy's two Square attacks read element masks from these ammo slots.
         * Retail refreshes only the handgun and shotgun, leaving stale masks. */
        if (pcGameplayOriginalBugFixes()) {
            c->m4_weaponSlots[1].m0_weaponPower = ctx->m0_weaponStats[c->m6F_equipment[1]].mC_power;
            c->m4_weaponSlots[2].m0_weaponPower = ctx->m0_weaponStats[c->m6F_equipment[2]].mC_power;
        }
    }
    if (c->m3_weaponStatusType == 100) {
        c->m84_statusFlags2[5] |= c->m0_weaponStatusMask;
    }
}

/* Noah setupStatusMenuForCharacter2: computed stats into m330->mB8 */
void setupStatusMenuForCharacter2(MenuContext_330* ctx, u8 charId) {
    GameStateCharacter* c;

    c = &gameState.m26C_party[charId];
    if (c->m56_battleCommandLoadout == 4) {
        ctx->mB8_computedStats[0] = (c->m4_weaponSlots[0].m0_weaponPower + c->m4_weaponSlots[3].m0_weaponPower) * 6 / 10;
    } else {
        ctx->mB8_computedStats[0] = c->m4_weaponSlots[0].m0_weaponPower + (c->m58_attack + c->m28_attackBonus);
    }
    ctx->mB8_computedStats[1] = c->m5E_hitPercentage + c->m2E_hitBonus;
    ctx->mB8_computedStats[2] = c->m2D_accessoryDefence + (c->m59_defence + c->m29_defenceBonus);
    ctx->mB8_computedStats[3] = c->m5F_evadePercentage + c->m2F_evadeBonus;
    ctx->mB8_computedStats[4] = c->m5B_ether + c->m2B_etherBonus;
    ctx->mB8_computedStats[5] = c->m5C_etherDefence + c->m2C_etherDefenceBonus;
    ctx->mB8_computedStats[6] = c->m5A_agility + c->m2A_agilityBonus;
    if (ctx->mB8_computedStats[0] > 250) {
        ctx->mB8_computedStats[0] = 250;
    }
    if (ctx->mB8_computedStats[1] > 99) {
        ctx->mB8_computedStats[1] = 99;
    }
    if (ctx->mB8_computedStats[2] > 250) {
        ctx->mB8_computedStats[2] = 250;
    }
    if (ctx->mB8_computedStats[3] > 99) {
        ctx->mB8_computedStats[3] = 99;
    }
    if (ctx->mB8_computedStats[4] > 250) {
        ctx->mB8_computedStats[4] = 250;
    }
    if (ctx->mB8_computedStats[5] > 250) {
        ctx->mB8_computedStats[5] = 250;
    }
    if (ctx->mB8_computedStats[6] > 20) {
        ctx->mB8_computedStats[6] = 16;
    }
}

/* gear status numbers into m330->m9C (gear 7 derived from party member 7) */
void computeGearStatusStats(MenuContext_330* ctx, u8 gearId) {
    GameStateGear* gear;
    GameStateCharacter* pilot;
    s32 power;

    if (gameState.m22B6_gameFlags & 0x1000) {
        gearPilotIds[9] = 10;
    }
    if (gearId == 7) {
        gameState.m978_gears[7].m60_hp = gameState.m26C_party[7].m4C_HP * 50;
        gameState.m978_gears[7].m64_maxHp = gameState.m26C_party[7].m4E_MaxHP * 50;
        gameState.m978_gears[7].m3C_engineOutput = gameState.m26C_party[7].m58_attack + gameState.m26C_party[7].m28_attackBonus;
        gameState.m978_gears[7].m70_defence = (gameState.m26C_party[7].m59_defence + gameState.m26C_party[7].m29_defenceBonus) * 12;
        gameState.m978_gears[7].m72_etherDefence = (gameState.m26C_party[7].m5C_etherDefence + gameState.m26C_party[7].m2C_etherDefenceBonus) * 6;
        gameState.m978_gears[7].m98_agility = gameState.m26C_party[7].m5A_agility + gameState.m26C_party[7].m2A_agilityBonus;
    }
    gear = &gameState.m978_gears[gearId];
    pilot = &gameState.m26C_party[gearPilotIds[gearId]];
    ctx->m9C_gearStats.m0_hp = gear->m60_hp;
    ctx->m9C_gearStats.m4_maxHp = gear->m64_maxHp;
    ctx->m9C_gearStats.m8_defence = gear->m70_defence + gear->m40_accessoryDefence;
    ctx->m9C_gearStats.mA_etherDefence = pilot->m5C_etherDefence + pilot->m2C_etherDefenceBonus + gear->m42_accessoryEtherDefence + gear->m72_etherDefence;
    ctx->m9C_gearStats.mC_totalWeight = gear->m68_weight + gear->m44_accessoryWeight;
    ctx->m9C_gearStats.mE = gear->m6A;
    ctx->m9C_gearStats.m10_fuel = gear->m38_fuel;
    ctx->m9C_gearStats.m12_maxFuel = gear->m3A_maxFuel;
    power = gear->m3C_engineOutput * (gear->m74_baseAttackMultiplier + gear->m55_accessoryBonuses[1]);
    ctx->m9C_gearStats.m14_attack = ((gearId == 5 || gearId == 13) ? (gear->m12_slots[0].m0_weaponPower + gear->m12_slots[2].m0_weaponPower) * 6 / 10 : gear->m12_slots[0].m0_weaponPower) + power;
    ctx->m9C_gearStats.m16_response = gear->m9F_response + gear->m4D_accessoryResponse;
    ctx->m9C_gearStats.m17_agility = gear->m98_agility - gear->m4A_agilityPenalty;
    ctx->m9C_gearStats.m18_etherAmp = gear->m9E_etherAmp + gear->m54_accessoryEtherAmp;
    ctx->m9C_gearStats.m19 = gear->m9D;
    ctx->m9C_gearStats.m1A = gear->m9C;
}

/* recompute a gear's equipment, then copy accessories (and weapons) to its alternate gear slot */
void refreshGearEquipment(MenuContext_330* ctx, u8 gearId) {
    u8 i;

    applyGearAccessoryBonuses(ctx, gearId);
    applyGearWeaponStats(ctx, gearId);
    switch (gearId) {
    case 0:
        for (i = 0; i < 3; i++) {
            gameState.m978_gears[1].m9_accessorySlots[i] = gameState.m978_gears[0].m9_accessorySlots[i];
        }
        applyGearAccessoryBonuses(ctx, 1);
        break;
    case 1:
        for (i = 0; i < 3; i++) {
            gameState.m978_gears[15].m9_accessorySlots[i] = gameState.m978_gears[1].m9_accessorySlots[i];
        }
        applyGearAccessoryBonuses(ctx, 15);
        break;
    case 2:
        for (i = 0; i < 3; i++) {
            gameState.m978_gears[10].m9_accessorySlots[i] = gameState.m978_gears[2].m9_accessorySlots[i];
        }
        applyGearAccessoryBonuses(ctx, 10);
        break;
    case 3:
        for (i = 0; i < 3; i++) {
            gameState.m978_gears[11].m9_accessorySlots[i] = gameState.m978_gears[3].m9_accessorySlots[i];
        }
        applyGearAccessoryBonuses(ctx, 11);
        break;
    case 4:
        for (i = 0; i < 3; i++) {
            gameState.m978_gears[12].m9_accessorySlots[i] = gameState.m978_gears[4].m9_accessorySlots[i];
        }
        applyGearAccessoryBonuses(ctx, 12);
        gameState.m978_gears[12].mC_weapon[0] = gameState.m978_gears[4].mC_weapon[0];
        applyGearWeaponStats(ctx, 12);
        break;
    case 5:
        for (i = 0; i < 3; i++) {
            gameState.m978_gears[13].m9_accessorySlots[i] = gameState.m978_gears[5].m9_accessorySlots[i];
        }
        applyGearAccessoryBonuses(ctx, 13);
        gameState.m978_gears[13].mC_weapon[0] = gameState.m978_gears[5].mC_weapon[0];
        gameState.m978_gears[13].mC_weapon[3] = gameState.m978_gears[5].mC_weapon[3];
        gameState.m978_gears[13].m4_slotItem[0] = gameState.m978_gears[5].m4_slotItem[0];
        gameState.m978_gears[13].m4_slotItem[3] = gameState.m978_gears[5].m4_slotItem[3];
        applyGearWeaponStats(ctx, 13);
        break;
    case 6:
        for (i = 0; i < 3; i++) {
            gameState.m978_gears[14].m9_accessorySlots[i] = gameState.m978_gears[6].m9_accessorySlots[i];
        }
        applyGearAccessoryBonuses(ctx, 14);
        break;
    }
}

/* recompute gear frame, engine, armor stats */
void applyGearPartStats(MenuContext_330* ctx, u8 gearId) {
    applyGearFrameStats(ctx, gearId);
    applyGearEngineStats(ctx, gearId);
    applyGearArmorStats(ctx, gearId);
}

/* gear frame stats (HP, m98/m9D-m9F) from m330->m8 table */
void applyGearFrameStats(MenuContext_330* ctx, u8 gearId) {
    GameStateGear* gear;
    MenuGearFrame* frame;

    gear = &gameState.m978_gears[gearId];
    frame = ctx->m8_gearFrames;
    frame += gear->m2_frame;
    gear->m60_hp = frame->m4_hp;
    gear->m64_maxHp = frame->m4_hp;
    gear->m98_agility = frame->m14_agility;
    gear->m9E_etherAmp = frame->m15_etherAmp;
    gear->m9D = frame->m16;
    gear->m9F_response = frame->m17_response;
    if ((u32)gear->m60_hp > (u32)gear->m64_maxHp) {
        gear->m60_hp = gear->m64_maxHp;
    }
}

/* gear armor stats (m70/m72) from m330->m10 table */
void applyGearArmorStats(MenuContext_330* ctx, u8 gearId) {
    GameStateGear* gear;
    MenuGearArmor* armor;

    gear = &gameState.m978_gears[gearId];
    armor = ctx->m10_gearArmors;
    armor += gear->m8_armor;
    gear->m70_defence = armor->m8_defence;
    gear->m72_etherDefence = armor->mA_etherDefence;
}

/* gear engine stats (max fuel, m3C-m3F) from m330->mC table */
void applyGearEngineStats(MenuContext_330* ctx, u8 gearId) {
    GameStateGear* gear;
    MenuGearEngine* engine;

    gear = &gameState.m978_gears[gearId];
    engine = ctx->mC_gearEngines;
    engine += gear->m3_engine;
    gear->m3A_maxFuel = engine->m6_fuel;
    gear->m3C_engineOutput = engine->mC_output;
    gear->m3D[0] = engine->mD;
    gear->m3D[1] = engine->mE_attackMultiplier;
    gear->m3F_attackMultiplier = engine->mE_attackMultiplier;
    if (gear->m38_fuel > gear->m3A_maxFuel) {
        gear->m38_fuel = gear->m3A_maxFuel;
    }
}

/* recompute a gear's accessory bonuses (m330->m14 table), pilot ability bits */
void applyGearAccessoryBonuses(MenuContext_330* ctx, u8 gearId) {
    GameStateGear* gear;
    u16* abilities;
    u16* flags;
    MenuGearAccessory* acc;
    u8 i;
    u8 j;

    gear = &gameState.m978_gears[gearId];
    abilities = &gameState.m16C0_abilities[gearPilotIds[gearId]].m0_unlockedAbilitiesBitField[2];
    flags = &gameState.m16C0_abilities[gearPilotIds[gearId]].m1A_unlockedGearAbilities;
    gear->m40_accessoryDefence = 0;
    gear->m42_accessoryEtherDefence = 0;
    gear->m44_accessoryWeight = 0;
    gear->m48 = 0;
    gear->m4C = 0;
    gear->m4D_accessoryResponse = 0;
    gear->m4E_accessoryAgility = 0;
    gear->m4F_frameRepairLevel = 0;
    gear->m6E_elementResistanceMask = 0;
    gear->m54_accessoryEtherAmp = 0;
    for (j = 0; j < 16; j++) {
        gear->m88_elementResistances[j] = 0;
    }
    for (j = 0; j < 4; j++) {
        gear->m50_terrainAgility[j] = 0;
    }
    for (j = 0; j < 3; j++) {
        gear->m55_accessoryBonuses[j] = 0;
    }
    gear->m7E_statusFlags1 = 0;
    gear->m82_statusFlags3 = 0;
    gear->m86_statusFlags5 &= 0xF000;
    *abilities &= 0xFB6F;
    for (i = 0; i < 3; i++) {
        acc = ctx->m14_gearAccessories;
        acc += gear->m9_accessorySlots[i];
        gear->m40_accessoryDefence += acc->mD_defence;
        gear->m42_accessoryEtherDefence += acc->mE_etherDefence;
        gear->m44_accessoryWeight += acc->m6_weight;
        gear->m4C += acc->m18;
        gear->m4D_accessoryResponse += acc->m14_response;
        gear->m54_accessoryEtherAmp += acc->m1B_etherAmp;
        for (j = 0; j < 4; j++) {
            gear->m50_terrainAgility[j] += acc->m10_terrainAgility[j];
        }
        switch (acc->m15_type) {
        case 1:
            gear->m7E_statusFlags1 |= acc->m16_value;
            break;
        case 2:
            gear->m82_statusFlags3 |= acc->m16_value;
            break;
        case 3:
            gear->m86_statusFlags5 |= acc->m16_value;
            break;
        case 4:
            gear->m6E_elementResistanceMask |= acc->m16_value;
            for (j = 0; j < 16; j++) {
                if (acc->m16_value & (0x8000 >> j)) {
                    gear->m88_elementResistances[j] += acc->m1A_resistanceAmount;
                }
            }
            break;
        case 5:
            gear->m4F_frameRepairLevel += (u8)acc->m16_value;
            break;
        case 6:
            if ((*abilities & 0x1000) && (*abilities & 0x800)) {
                *abilities |= 0x400;
            }
            break;
        case 7:
            if ((*abilities & 0x200) && (*abilities & 0x100)) {
                *abilities |= 0x80;
            }
            break;
        case 8:
            if ((*abilities & 0x40) && (*abilities & 0x20)) {
                *abilities |= 0x10;
            }
            break;
        case 9:
            gear->m48 |= acc->m16_value;
        case 10:
            gear->m55_accessoryBonuses[1] += (u8)acc->m16_value;
            break;
        case 11:
            gear->m55_accessoryBonuses[2] += (u8)acc->m16_value;
            break;
        }
    }
    gear->m4A_agilityPenalty = computeGearAgilityPenalty(gearId);
    if (gear->m4F_frameRepairLevel) {
        *flags |= 0x8000;
    } else if (gearId == gameState.m26C_party[gearPilotIds[gearId]].mA0_gearNum) {
        *flags &= 0x7FFF;
    }
}

/* gear weapon stats from m330->m18 (three weapon slots for gears 5/13) */
void applyGearWeaponStats(MenuContext_330* ctx, u8 gearId) {
    GameStateGear* gear;
    MenuGearWeapon* weapon;

    gear = &gameState.m978_gears[gearId];
    weapon = ctx->m18_gearWeapons;
    weapon += gear->mC_weapon[0];
    gear->m12_slots[0].m0_weaponPower = weapon->mE_power;
    gear->m10_weaponStatusMask = weapon->m12_statusMask;
    gear->m12_slots[0].m1_weaponStatusChance = weapon->m10_statusChance;
    gear->m12_slots[0].m2_weaponStatusType = weapon->m11_statusType;
    gear->m5C_extraFileIndex = weapon->m0_extraFileIndex;
    gear->m5D_weaponAnimOffsets[0] = weapon->m1_weaponAnimOffset0;
    gear->m5D_weaponAnimOffsets[1] = weapon->m2_weaponAnimOffset1;
    gear->m5D_weaponAnimOffsets[2] = weapon->m3_weaponAnimOffset2;
    if (gear->m12_slots[0].m2_weaponStatusType == 100) {
        gear->m86_statusFlags5 &= 0xFFF;
        gear->m86_statusFlags5 |= gear->m10_weaponStatusMask;
    }
    if (gearId == 5 || gearId == 13) {
        weapon = ctx->m18_gearWeapons;
        weapon += gear->m4_slotItem[0];
        gear->m12_slots[0].m0_weaponPower = weapon->mE_power;
        gear->m10_weaponStatusMask = weapon->m12_statusMask;
        gear->m12_slots[0].m1_weaponStatusChance = weapon->m10_statusChance;
        gear->m12_slots[0].m2_weaponStatusType = weapon->m11_statusType;
        gear->m5D_weaponAnimOffsets[0] = weapon->m1_weaponAnimOffset0;
        weapon = ctx->m18_gearWeapons;
        weapon += gear->m4_slotItem[1];
        gear->m12_slots[1].m0_weaponPower = weapon->mE_power;
        gear->m12_slots[0].m6_nextWeaponStatusMask = weapon->m12_statusMask;
        gear->m12_slots[1].m1_weaponStatusChance = weapon->m10_statusChance;
        gear->m12_slots[1].m2_weaponStatusType = weapon->m11_statusType;
        gear->m5D_weaponAnimOffsets[1] = weapon->m2_weaponAnimOffset1;
        weapon = ctx->m18_gearWeapons;
        weapon += gear->m4_slotItem[3];
        gear->m12_slots[2].m0_weaponPower = weapon->mE_power;
        gear->m12_slots[1].m6_nextWeaponStatusMask = weapon->m12_statusMask;
        gear->m12_slots[2].m1_weaponStatusChance = weapon->m10_statusChance;
        gear->m12_slots[2].m2_weaponStatusType = weapon->m11_statusType;
        gear->m5D_weaponAnimOffsets[2] = weapon->m3_weaponAnimOffset2;
    }
}

/* gear value from accessory weight: max(0, (m44/120 - m75) / 2) */
u8 computeGearAgilityPenalty(u8 gearId) {
    GameStateGear* gear;
    s16 value;

    gear = &gameState.m978_gears[gearId];
    value = ((u16)(gear->m44_accessoryWeight / 120) - gear->m75_weightAllowance) / 2;
    if (value < 0) {
        value = 0;
    }
    return value;
}

/* gear data: derived value (maxHp / 10 * 2 / 9, rounded to 10) at perGear + 0x5C8 + 0x24 */
void computeGearHealFuelCost(MenuContext_330* ctx, u8 gearId) {
    MenuGearDataSub* sub;

    sub = (MenuGearDataSub*)ctx->m4C_perGear[gearId];
    sub = (MenuGearDataSub*)((u8*)sub + 0x5C8); /* two steps, as retail computes it */
    sub->m24_fuelCost = (u32)gameState.m978_gears[gearId].m64_maxHp / 10 * 2 / 9;
    sub->m24_fuelCost = (u16)(sub->m24_fuelCost / 10) * 10;
}

/* build the save file's game data (gameState copy, gears packed to 0x3C bytes) */
void buildSaveGameData(MenuSaveGame* save) {
    GameStateCharacter* srcC;
    GameStateCharacter* dstC;
    GameStateGear* src;
    MenuSaveGear* dst;
    u8 i;

    for (i = 0; i < 11; i++) {
        dstC = &save->m290_party[i];
        srcC = &gameState.m26C_party[i];
        *dstC = *srcC;
    }
    for (i = 0; i < 20; i++) {
        dst = &save->m99C_gears[i];
        src = &gameState.m978_gears[i];
        dst->m0_equip = *(MenuSaveGearEquip*)src;
        dst->m10_weapons = *(MenuSaveGearWeapons*)&src->m10_weaponStatusMask;
        dst->m28_weaponStats = *(s32*)&src->m5C_extraFileIndex;
        dst->m34_fuel = src->m38_fuel;
        dst->m2C_hp = src->m60_hp;
        dst->m38_defenceDown = src->m99_defenceDown;
        dst->m39_baseAttackMultiplier = src->m74_baseAttackMultiplier;
        dst->m3A_weightAllowance = src->m75_weightAllowance;
    }
    save->m0_summary.m24_characterNames = *(MenuSaveCharacterNames*)gameState.m0_names[0];
    save->m100_gearNames = *(MenuSaveGearNames*)gameState.m0_names[11];
    save->mE4C = *(MenuSaveBlock1648*)gameState.m1648;
    save->mEC4_abilities = *(MenuSaveBlock16C0*)gameState.m16C0_abilities;
    save->m1024_worldmapPosition = *(MenuSaveBlock1820*)gameState.m1820_worldmapPosition;
    memcpy(save->m1124_gameStateTail, (u8*)&gameState + 0x1920, 0xA38);
}

/* restore gameState from the save file's game data, recomputing gear stats */
void restoreSaveGameData(MenuSaveGame* save, MenuContext_330* ctx) {
    GameStateCharacter* srcC;
    GameStateCharacter* dstC;
    MenuSaveGear* src;
    GameStateGear* dst;
    u8 i;

    for (i = 0; i < 11; i++) {
        dstC = &gameState.m26C_party[i];
        srcC = &save->m290_party[i];
        *dstC = *srcC;
    }
    *(MenuSaveBlock16C0*)gameState.m16C0_abilities = save->mEC4_abilities;
    for (i = 0; i < 20; i++) {
        dst = &gameState.m978_gears[i];
        src = &save->m99C_gears[i];
        *(MenuSaveGearEquip*)dst = src->m0_equip;
        *(MenuSaveGearWeapons*)&dst->m10_weaponStatusMask = src->m10_weapons;
        *(s32*)&dst->m5C_extraFileIndex = src->m28_weaponStats;
        applyGearFrameStats(ctx, i);
        applyGearArmorStats(ctx, i);
        applyGearEngineStats(ctx, i);
        applyGearAccessoryBonuses(ctx, i);
        dst->m38_fuel = src->m34_fuel;
        dst->m60_hp = src->m2C_hp;
        dst->m99_defenceDown = src->m38_defenceDown;
        dst->m74_baseAttackMultiplier = src->m39_baseAttackMultiplier;
        dst->m75_weightAllowance = src->m3A_weightAllowance;
    }
    *(MenuSaveCharacterNames*)gameState.m0_names[0] = save->m0_summary.m24_characterNames;
    *(MenuSaveGearNames*)gameState.m0_names[11] = save->m100_gearNames;
    *(MenuSaveBlock1648*)gameState.m1648 = save->mE4C;
    *(MenuSaveBlock1820*)gameState.m1820_worldmapPosition = save->m1024_worldmapPosition;
    memcpy((u8*)&gameState + 0x1920, save->m1124_gameStateTail, 0xA38);
}

/* debug item effect 1: every item x10 in all five inventories */
void debugGiveAllItems(void) {
    u8 i;

    for (i = 1; i < 72; i++) {
        gameState.m1D9C_inventory1Ids[i] = i;
        gameState.m1D38_inventory1Counts[i] = 10;
    }
    for (i = 1; i < 150; i++) {
        gameState.m1EC8_inventory2Ids[i] = i;
        gameState.m1E00_inventory2Counts[i] = 10;
    }
    for (i = 1; i < 76; i++) {
        gameState.m2026_inventory0Ids[i + 2] = i;
        gameState.m1F90_inventory0Counts[i + 2] = 10;
    }
    for (i = 1; i < 72; i++) {
        gameState.m2120_inventory3Ids[i] = i;
        gameState.m20BC_inventory3Counts[i] = 10;
    }
    for (i = 1; i < 105; i++) {
        gameState.m221A_inventory4Ids[i] = i;
        gameState.m2184_inventory4Counts[i] = 10;
    }
}

/* debug item effect 2: whole party, all abilities (m16C0 bits) */
void debugUnlockAllAbilities(void) {
    gameState.m1D30_partyMemberBitField = 0x7FF;
    gameState.m16C0_abilities[0].m0_unlockedAbilitiesBitField[0] = 0xFFF8;
    gameState.m16C0_abilities[0].m0_unlockedAbilitiesBitField[1] = 0xFF00;
    gameState.m16C0_abilities[0].m0_unlockedAbilitiesBitField[2] = 0xFFF0;
    gameState.m16C0_abilities[0].m0_unlockedAbilitiesBitField[3] = 0xFE00;
    gameState.m16C0_abilities[0].m1A_unlockedGearAbilities = 0xE000;
    gameState.m16C0_abilities[0].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[1].m0_unlockedAbilitiesBitField[0] = 0xFFE0;
    gameState.m16C0_abilities[1].m0_unlockedAbilitiesBitField[1] = 0xFFF0;
    gameState.m16C0_abilities[1].m0_unlockedAbilitiesBitField[2] = 0xFFF0;
    gameState.m16C0_abilities[1].m0_unlockedAbilitiesBitField[3] = 0xFFF0;
    gameState.m16C0_abilities[1].m1A_unlockedGearAbilities = 0xC000;
    gameState.m16C0_abilities[1].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[2].m0_unlockedAbilitiesBitField[0] = 0xFFE0;
    gameState.m16C0_abilities[2].m0_unlockedAbilitiesBitField[1] = 0xFFE0;
    gameState.m16C0_abilities[2].m0_unlockedAbilitiesBitField[2] = 0xFFF0;
    gameState.m16C0_abilities[2].m0_unlockedAbilitiesBitField[3] = 0xFF00;
    gameState.m16C0_abilities[2].m1A_unlockedGearAbilities = 0x8000;
    gameState.m16C0_abilities[2].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[3].m0_unlockedAbilitiesBitField[0] = 0xFFE0;
    gameState.m16C0_abilities[3].m0_unlockedAbilitiesBitField[1] = 0xFFC0;
    gameState.m16C0_abilities[3].m0_unlockedAbilitiesBitField[2] = 0xFFF0;
    gameState.m16C0_abilities[3].m0_unlockedAbilitiesBitField[3] = 0xFF00;
    gameState.m16C0_abilities[3].m1A_unlockedGearAbilities = 0xF000;
    gameState.m16C0_abilities[3].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[4].m0_unlockedAbilitiesBitField[0] = 0xFFC0;
    gameState.m16C0_abilities[4].m0_unlockedAbilitiesBitField[1] = 0xFFC0;
    gameState.m16C0_abilities[4].m0_unlockedAbilitiesBitField[2] = 0xFFF0;
    gameState.m16C0_abilities[4].m0_unlockedAbilitiesBitField[3] = 0xFFC0;
    gameState.m16C0_abilities[4].m1A_unlockedGearAbilities = 0xE000;
    gameState.m16C0_abilities[4].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[5].m0_unlockedAbilitiesBitField[0] = 0xFFC0;
    gameState.m16C0_abilities[5].m0_unlockedAbilitiesBitField[1] = 0xF000;
    gameState.m16C0_abilities[5].m0_unlockedAbilitiesBitField[2] = 0xFFF0;
    gameState.m16C0_abilities[5].m0_unlockedAbilitiesBitField[3] = 0xF000;
    gameState.m16C0_abilities[5].m1A_unlockedGearAbilities = 0x8000;
    gameState.m16C0_abilities[5].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[6].m0_unlockedAbilitiesBitField[0] = 0xFFC0;
    gameState.m16C0_abilities[6].m0_unlockedAbilitiesBitField[1] = 0xFF00;
    gameState.m16C0_abilities[6].m0_unlockedAbilitiesBitField[2] = 0xFFF0;
    gameState.m16C0_abilities[6].m0_unlockedAbilitiesBitField[3] = 0xFF00;
    gameState.m16C0_abilities[6].m1A_unlockedGearAbilities = 0x8000;
    gameState.m16C0_abilities[6].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[7].m0_unlockedAbilitiesBitField[0] = 0x0;
    gameState.m16C0_abilities[7].m0_unlockedAbilitiesBitField[1] = 0xFF00;
    gameState.m16C0_abilities[7].m0_unlockedAbilitiesBitField[2] = 0x0;
    gameState.m16C0_abilities[7].m0_unlockedAbilitiesBitField[3] = 0xFF00;
    gameState.m16C0_abilities[7].m1A_unlockedGearAbilities = 0x0;
    gameState.m16C0_abilities[7].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[8].m0_unlockedAbilitiesBitField[0] = 0x0;
    gameState.m16C0_abilities[8].m0_unlockedAbilitiesBitField[1] = 0xF800;
    gameState.m16C0_abilities[8].m0_unlockedAbilitiesBitField[2] = 0xFFF0;
    gameState.m16C0_abilities[8].m0_unlockedAbilitiesBitField[3] = 0x0;
    gameState.m16C0_abilities[8].m1A_unlockedGearAbilities = 0xE000;
    gameState.m16C0_abilities[8].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[9].m0_unlockedAbilitiesBitField[0] = 0xFFE0;
    gameState.m16C0_abilities[9].m0_unlockedAbilitiesBitField[1] = 0xFFE0;
    gameState.m16C0_abilities[9].m0_unlockedAbilitiesBitField[2] = 0xFFF0;
    gameState.m16C0_abilities[9].m0_unlockedAbilitiesBitField[3] = 0xFFE0;
    gameState.m16C0_abilities[9].m1A_unlockedGearAbilities = 0x8000;
    gameState.m16C0_abilities[9].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[10].m0_unlockedAbilitiesBitField[0] = 0xFFC0;
    gameState.m16C0_abilities[10].m0_unlockedAbilitiesBitField[1] = 0xFF00;
    gameState.m16C0_abilities[10].m0_unlockedAbilitiesBitField[2] = 0xFFF0;
    gameState.m16C0_abilities[10].m0_unlockedAbilitiesBitField[3] = 0xFF00;
    gameState.m16C0_abilities[10].m1A_unlockedGearAbilities = 0x8000;
    gameState.m16C0_abilities[10].m17_energyPerTurn = 7;
}

/* Noah setupMenuContext364: init a menu box's frame primitives */
void setupMenuContext364(u8 boxId) {
    MenuContext_364* box;
    RECT tw;
    u8 i;

    box = gMenuContext->m364_menuBoxes[boxId];
    tw.y = 0;
    tw.x = 0;
    tw.h = 0x100;
    tw.w = 0x100;
    gMenuContext->m33C_drawFlags->m20_menuBoxEnabled[boxId] = 0;
    gMenuContext->m33C_drawFlags->m27_menuBoxAnimating[boxId] = 0;
    for (i = 0; i < 2; i++) {
        SetPolyG4(&box->m4B0_background[i]);
        (&box->m4B0_background[i])->r0 = 0x68;
        (&box->m4B0_background[i])->g0 = 0x68;
        (&box->m4B0_background[i])->b0 = 0x68;
        (&box->m4B0_background[i])->r1 = 0x68;
        (&box->m4B0_background[i])->g1 = 0x68;
        (&box->m4B0_background[i])->b1 = 0x68;
        (&box->m4B0_background[i])->r2 = 0x68;
        (&box->m4B0_background[i])->g2 = 0x68;
        (&box->m4B0_background[i])->b2 = 0x68;
        (&box->m4B0_background[i])->r3 = 0x68;
        (&box->m4B0_background[i])->g3 = 0x68;
        (&box->m4B0_background[i])->b3 = 0x68;
        SetSemiTrans(&box->m4B0_background[i], 1);
        SetDrawMode(&box->m4F8_drawMode[i], 0, 0, GetTPage(0, 0, gMenuContext->m46C_menuBorders[0][4], gMenuContext->m46C_menuBorders[0][5]), &tw);
    }
    for (i = 0; i < 4; i++) {
        SetPolyFT4(&box->m140_topEdge[0][i]);
        SetShadeTex(&box->m140_topEdge[0][i], 1);
        (&box->m140_topEdge[0][i])->r0 = 0xFF;
        (&box->m140_topEdge[0][i])->g0 = 0xFF;
        (&box->m140_topEdge[0][i])->b0 = 0xFF;
        (&box->m140_topEdge[0][i])->tpage = GetTPage(gMenuContext->m46C_menuBorders[0][1], 0, gMenuContext->m46C_menuBorders[0][4], gMenuContext->m46C_menuBorders[0][5]);
        (&box->m140_topEdge[0][i])->clut = GetClut(gMenuContext->m46C_menuBorders[0][2], gMenuContext->m46C_menuBorders[0][3]);
        SetPolyFT4(&box->m1E0_bottomEdge[0][i]);
        SetShadeTex(&box->m1E0_bottomEdge[0][i], 1);
        (&box->m1E0_bottomEdge[0][i])->r0 = 0xFF;
        (&box->m1E0_bottomEdge[0][i])->g0 = 0xFF;
        (&box->m1E0_bottomEdge[0][i])->b0 = 0xFF;
        (&box->m1E0_bottomEdge[0][i])->tpage = GetTPage(gMenuContext->m46C_menuBorders[1][1], 0, gMenuContext->m46C_menuBorders[1][4], gMenuContext->m46C_menuBorders[1][5]);
        (&box->m1E0_bottomEdge[0][i])->clut = GetClut(gMenuContext->m46C_menuBorders[1][2], gMenuContext->m46C_menuBorders[1][3]);
        SetPolyFT4(&box->m280_leftEdge[0][i]);
        SetShadeTex(&box->m280_leftEdge[0][i], 1);
        (&box->m280_leftEdge[0][i])->r0 = 0xFF;
        (&box->m280_leftEdge[0][i])->g0 = 0xFF;
        (&box->m280_leftEdge[0][i])->b0 = 0xFF;
        (&box->m280_leftEdge[0][i])->tpage = GetTPage(gMenuContext->m46C_menuBorders[2][1], 0, gMenuContext->m46C_menuBorders[2][4], gMenuContext->m46C_menuBorders[2][5]);
        (&box->m280_leftEdge[0][i])->clut = GetClut(gMenuContext->m46C_menuBorders[2][2], gMenuContext->m46C_menuBorders[2][3]);
        SetPolyFT4(&box->m320_rightEdge[0][i]);
        SetShadeTex(&box->m320_rightEdge[0][i], 1);
        (&box->m320_rightEdge[0][i])->r0 = 0xFF;
        (&box->m320_rightEdge[0][i])->g0 = 0xFF;
        (&box->m320_rightEdge[0][i])->b0 = 0xFF;
        (&box->m320_rightEdge[0][i])->tpage = GetTPage(gMenuContext->m46C_menuBorders[3][1], 0, gMenuContext->m46C_menuBorders[3][4], gMenuContext->m46C_menuBorders[3][5]);
        (&box->m320_rightEdge[0][i])->clut = GetClut(gMenuContext->m46C_menuBorders[3][2], gMenuContext->m46C_menuBorders[3][3]);
    }
}

/* Noah initMenuContext3A8_MemoryCardPolys */
void initMenuContext3A8_MemoryCardPolys(s32 tileId) {
    s32* pX;
    s32* pY;
    MenuContext_3A8* tile;
    RECT tw;
    s32 i;

    i = 0;
    pX = &memoryCardTilesPositionX[tileId];
    pY = &memoryCardTilesPositionY[tileId];
    tile = gMenuContext->m3A8_memoryCardTiles[tileId];
    while (i < 2) {
        initMemoryCardTransparentPoly(&tile->m0_memoryCardTransparentPolys[i]);
        tile->m0_memoryCardTransparentPolys[i].x0 = *pX;
        tile->m0_memoryCardTransparentPolys[i].y0 = *pY;
        tile->m0_memoryCardTransparentPolys[i].x1 = *pX + 0x10;
        tile->m0_memoryCardTransparentPolys[i].y1 = *pY;
        tile->m0_memoryCardTransparentPolys[i].x2 = *pX;
        tile->m0_memoryCardTransparentPolys[i].y2 = *pY + 0x10;
        tile->m0_memoryCardTransparentPolys[i].x3 = *pX + 0x10;
        tile->m0_memoryCardTransparentPolys[i].y3 = *pY + 0x10;
        tile->m0_memoryCardTransparentPolys[i].tpage = GetTPage(0, 0, 0x140, 0x80);
        SetPolyF4(&tile->mB0_memoryCardOpaquePolys[i]);
        tile->mB0_memoryCardOpaquePolys[i].r0 = 0x80;
        tile->mB0_memoryCardOpaquePolys[i].g0 = 0x80;
        tile->mB0_memoryCardOpaquePolys[i].b0 = 0x80;
        SetSemiTrans(&tile->mB0_memoryCardOpaquePolys[i], 1);
        tile->mB0_memoryCardOpaquePolys[i].x0 = *pX;
        tile->mB0_memoryCardOpaquePolys[i].y0 = *pY;
        tile->mB0_memoryCardOpaquePolys[i].x1 = *pX + 0x10;
        tile->mB0_memoryCardOpaquePolys[i].y1 = *pY;
        tile->mB0_memoryCardOpaquePolys[i].x2 = *pX;
        tile->mB0_memoryCardOpaquePolys[i].y2 = *pY + 0x10;
        tile->mB0_memoryCardOpaquePolys[i].x3 = *pX + 0x10;
        tile->mB0_memoryCardOpaquePolys[i].y3 = *pY + 0x10;
        initMemoryCardPolyVerts(tile->mE0_vertices, *pX, *pY, 0x10, 0x10);
        i++;
    }
    tw.y = 0;
    tw.x = 0;
    tw.h = 0x100;
    tw.w = 0x100;
    SetDrawMode(&tile->m140_drawMode[0], 0, 0, GetTPage(0, 2, 0x140, 0x80), &tw);
    SetDrawMode(&tile->m140_drawMode[1], 0, 0, GetTPage(0, 2, 0x140, 0x80), &tw);
}

/* Noah initMenuContext3A8_Lines */
void initMenuContext3A8_Lines(s32 tileId) {
    s32* pX;
    s32* pY;
    MenuContext_3A8* tile;
    s32 i;

    pX = &memoryCardTilesPositionX[tileId];
    pY = &memoryCardTilesPositionY[tileId];
    tile = gMenuContext->m3A8_memoryCardTiles[tileId];
    for (i = 0; i < 2; i++) {
        SetLineF3(&tile->m50_topRightLine[i]);
        tile->m50_topRightLine[i].r0 = 0;
        tile->m50_topRightLine[i].g0 = 0xFF;
        tile->m50_topRightLine[i].b0 = 0;
        tile->m50_topRightLine[i].x0 = *pX;
        tile->m50_topRightLine[i].y0 = *pY;
        tile->m50_topRightLine[i].x1 = *pX + 0x10;
        tile->m50_topRightLine[i].y1 = *pY;
        tile->m50_topRightLine[i].x2 = *pX + 0x10;
        tile->m50_topRightLine[i].y2 = *pY + 0x10;
        initMemoryCardPolyVerts(tile->m100_topRightVerts, *pX, *pY, 0x10, 0x10);
        SetLineF3(&tile->m80_leftBottomLine[i]);
        tile->m80_leftBottomLine[i].r0 = 0;
        tile->m80_leftBottomLine[i].g0 = 0xFF;
        tile->m80_leftBottomLine[i].b0 = 0;
        tile->m80_leftBottomLine[i].x0 = *pX;
        tile->m80_leftBottomLine[i].y0 = *pY;
        tile->m80_leftBottomLine[i].x1 = *pX;
        tile->m80_leftBottomLine[i].y1 = *pY + 0x10;
        tile->m80_leftBottomLine[i].x2 = *pX + 0x10;
        tile->m80_leftBottomLine[i].y2 = *pY + 0x10;
        initMemoryCardPolyVerts(tile->m120_leftBottomVerts, *pX, *pY, 0x10, 0x10);
    }
}

/* Noah initMenuContext3A8_memoryCardDisplay (allocate the 32 tiles) */
void initMenuContext3A8_memoryCardDisplay(void) {
    s32 i;

    for (i = 0; i < 0x20; i++) {
        gMenuContext->m3A8_memoryCardTiles[i] = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_3A8), 0x158), 0);
        bzero(gMenuContext->m3A8_memoryCardTiles[i], sizeof(MenuContext_3A8));
        initMenuContext3A8_MemoryCardPolys(i);
        initMenuContext3A8_Lines(i);
    }
}

/* free the 32 memory card tiles */
void freeMemoryCardTiles(void) {
    s32 i;

    for (i = 0; i < 0x20; i++) {
        freeHeap(gMenuContext->m3A8_memoryCardTiles[i]);
    }
}

/* Noah initMenuContext34C_polys */
void initMenuContext34C_polys(void) {
    s32 i;
    s32 j;

    for (i = 0; i < 0x20; i++) {
        for (j = 0; j < 2; j++) {
            initMemoryCardTransparentPoly(&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j]);
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->x0 = memoryCardSlotsX[i % 21] + (i / 21) * 8;
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->y0 = memoryCardSlotsY[i / 21];
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->x1 = memoryCardSlotsX[i % 21] + (i / 21) * 8 + 0xC;
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->y1 = memoryCardSlotsY[i / 21];
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->x2 = memoryCardSlotsX[i % 21] + (i / 21) * 8;
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->y2 = memoryCardSlotsY[i / 21] + 0x10;
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->x3 = memoryCardSlotsX[i % 21] + (i / 21) * 8 + 0xC;
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->y3 = memoryCardSlotsY[i / 21] + 0x10;
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->u0 = (i % 16) * 16;
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->v0 = (i / 16) * 16 + 0xE0;
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->u1 = (i % 16) * 16 + 0xC;
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->v1 = (i / 16) * 16 + 0xE0;
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->u2 = (i % 16) * 16;
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->v2 = (i / 16) * 16 + 0xEF;
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->u3 = (i % 16) * 16 + 0xC;
            (&gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j])->v3 = (i / 16) * 16 + 0xEF;
            gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j].tpage = GetTPage(0, 0, 0x140, 0x80);
            gMenuContext->m34C_saveScreen->m0_polys[0][i * 2 + j].clut = GetClut(0, 0x1C0);
        }
    }
}

/* Noah initMenuContext34C_polys2 */
void initMenuContext34C_polys2(void) {
    s32 i;

    for (i = 0; i < 2; i++) {
        initMemoryCardTransparentPoly(&gMenuContext->m34C_saveScreen->mA00_icon[i]);
        (&gMenuContext->m34C_saveScreen->mA00_icon[i])->x0 = saveScreenIconX[0];
        (&gMenuContext->m34C_saveScreen->mA00_icon[i])->y0 = saveScreenIconY[0];
        (&gMenuContext->m34C_saveScreen->mA00_icon[i])->x1 = saveScreenIconX[0] + 0x20;
        (&gMenuContext->m34C_saveScreen->mA00_icon[i])->y1 = saveScreenIconY[0];
        (&gMenuContext->m34C_saveScreen->mA00_icon[i])->x2 = saveScreenIconX[0];
        (&gMenuContext->m34C_saveScreen->mA00_icon[i])->y2 = saveScreenIconY[0] + 0x20;
        (&gMenuContext->m34C_saveScreen->mA00_icon[i])->x3 = saveScreenIconX[0] + 0x20;
        (&gMenuContext->m34C_saveScreen->mA00_icon[i])->y3 = saveScreenIconY[0] + 0x20;
        gMenuContext->m34C_saveScreen->mA00_icon[i].tpage = GetTPage(0, 0, 0x140, 0x80);
        SetPolyG4(&gMenuContext->m34C_saveScreen->mA50_polyG4[i]);
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->r0 = 0x80;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->g0 = 0;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->b0 = 0x80;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->r1 = 0;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->g1 = 0;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->b1 = 0x80;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->r2 = 0x10;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->g2 = 0;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->b2 = 0x10;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->r3 = 0;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->g3 = 0;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->b3 = 0x10;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->x0 = 0;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->y0 = 0x4A;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->x1 = 0x140;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->y1 = 0x4A;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->x2 = 0;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->y2 = 0x8A;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->x3 = 0x140;
        (&gMenuContext->m34C_saveScreen->mA50_polyG4[i])->y3 = 0x8A;
    }
}

/* memory card screen: the three save slot labels and slot number sprites (m34C->mA98) */
void setupSaveSlotLabels(void) {
    s32 i;
    s32 j;

    for (i = 0; i < 3; i++) {
        gMenuContext->m34C_saveScreen->mA98_slots[i].m87A_labelLength = 0;
        for (j = 0; j < 9; j++) {
            if (saveSlotLabelGlyphs[j] != 0xFFFF) {
                gMenuContext->m34C_saveScreen->mA98_slots[i].m87A_labelLength += setupStringInPolyFT4(gMenuContext->m2DC_font, saveSlotLabelGlyphs[j],
                    &gMenuContext->m34C_saveScreen->mA98_slots[i].m50_label[gMenuContext->m34C_saveScreen->mA98_slots[i].m87A_labelLength],
                    gMenuContext->m308_oddOrEven, saveSlotLabelPositionX[j] + i * 0x50, saveSlotLabelPositionY[j], 0x1000);
            }
        }
        gMenuContext->m34C_saveScreen->mA98_slots[i].m876_oddOrEven = gMenuContext->m308_oddOrEven;
        initMemoryCardTransparentPoly(&gMenuContext->m34C_saveScreen->mA98_slots[i].m820_nameImage[gMenuContext->m308_oddOrEven]);
        gMenuContext->m34C_saveScreen->mA98_slots[i].m820_nameImage[gMenuContext->m308_oddOrEven].tpage = GetTPage(0, 0, 0x180, 0);
        gMenuContext->m34C_saveScreen->mA98_slots[i].m820_nameImage[gMenuContext->m308_oddOrEven].clut = textSpriteMode0;
        setupMenuPolyFT4(&gMenuContext->m34C_saveScreen->mA98_slots[i].m820_nameImage[gMenuContext->m308_oddOrEven], saveSlotLabelPositionX[0] + i * 0x50,
                      saveSlotLabelPositionY[0] + 7, infocardNameU[i + 6] * 4, infocardNameV[i + 6], 0x48, 0xD);
        gMenuContext->m34C_saveScreen->mA98_slots[i].m879_oddOrEven = gMenuContext->m308_oddOrEven;
    }
}

/* Noah initMenuContext34C (unused RECT local keeps retail's 0x20 frame) */
void initMenuContext34C(void);
void initMenuContext34C(void) {
    RECT unused; /* never used; retail's frame keeps its 8 bytes */

    gMenuContext->m34C_saveScreen = allocateHeap(PC_HEAP_SIZED(sizeof(MenuContext_34C), 0x2dc0), 0);
    bzero(gMenuContext->m34C_saveScreen, sizeof(MenuContext_34C));
    initMenuContext34C_polys();
    initMenuContext34C_polys2();
}

/* free m34C, clear m33C->mB */
void freeMenuContext34C(void) {
    freeHeap(gMenuContext->m34C_saveScreen);
    gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 0;
}

/* ClearImage(0x140,0xE0,0x40,0x20), clear m33C->mB */
void clearSaveTitleImage(void) {
    RECT rect;

    rect.x = 0x140;
    rect.y = 0xE0;
    rect.w = 0x40;
    rect.h = 0x20;
    ClearImage(&rect, 0, 0, 0);
    gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 0;
}

/* squeeze a 16x16 glyph's rows to 12 bytes (ORs byte pairs) */
void narrowGlyphTo12Pixels(u8* p) {
    s32 i;
    u8* q;

    for (i = 0; i < 16; i++) {
        q = p + i * 16;
        q[0] = q[0];
        q[1] = q[1] | q[2];
        q[2] = q[3];
        q[3] = q[4];
        q[4] = q[5] | q[6];
        q[5] = q[7];
        q[6] = q[8];
        q[7] = q[9] | q[10];
        q[8] = q[11];
        q[9] = q[12];
        q[10] = q[13] | q[14];
        q[11] = q[15];
    }
}

/* glyph pointer for a character: ASCII via SJIS table asciiToSjisTable, then Krom2RawAdd */
u16* getKanjiRomGlyph(u8* str) {
    u8 hi;
    u8 lo;

    hi = str[0];
    lo = str[1];
    kanjiGlyphIsDoubleByte = 1;
    if (hi < 0x80) {
        if (hi >= 0x20) {
            lo = asciiToSjisTable[hi];
            hi = asciiToSjisTable[hi] >> 8;
            kanjiGlyphIsDoubleByte = 0;
        } else {
            hi = 0x81;
            lo = 0x40;
            kanjiGlyphIsDoubleByte = 0;
        }
    }
    return func_800405C4((hi << 8) | lo);
}

/* render a save's title (SJIS, 12x16 glyphs, 4bpp) to VRAM 0x140,0xE0 */
void renderSaveTitleImage(s32 slot) {
    u8* bits;
    u16* image;
    u8* str;
    u16* glyph;
    u8* dst;
    s32 nBytes;
    s32 nChars;
    s32 row;
    s32 bit;
    RECT rect;
#ifdef PLATFORM_PC
    u32 drawn = 0; /* the characters composed on the image's shown row */
#endif

    bits = allocateHeap(PC_HEAP_BYTES(0x100), 1);
    image = allocateHeap(PC_HEAP_BYTES(0x1000), 1);
    bzero(image, 0x1000);
    nBytes = 0;
    nChars = 0;
    str = gMenuContext->m32C_memoryCardContext->mB94_fileHeaders[slot].m4_title;
    for (nChars = 0; nChars < 32; nChars++) {
        if (*str == 0) {
            break;
        }
        dst = bits;
        glyph = getKanjiRomGlyph(str);
        if (glyph != (u16*)-1) {
            for (row = 0; row < 16; row++) {
                for (bit = 7; bit >= 0; bit--) {
                    *dst++ = (*glyph >> bit) & 1;
                }
                for (bit = 15; bit >= 8; bit--) {
                    *dst++ = (*glyph >> bit) & 1;
                }
                glyph++;
            }
            narrowGlyphTo12Pixels(bits);
            for (row = 0; row < 16; row++) {
                for (bit = 0; bit < 12; bit++) {
                    image[row * 64 + bit / 4 + (nChars % 16) * 4 + (nChars / 16) * 1024] |= bits[row * 16 + bit] << ((bit % 4) * 4);
                }
            }
#ifdef PLATFORM_PC
            if (nChars >= 16) {
                drawn |= 1u << (nChars - 16);
            }
#endif
        }
        str++;
        nBytes++;
        if (kanjiGlyphIsDoubleByte) {
            str++;
            nBytes++;
        }
        if (nBytes >= 64) {
            break;
        }
    }
    rect.x = 0x140;
    rect.y = 0xE0;
    rect.w = 0x40;
    rect.h = 0x20;
    LoadImage(&rect, image);
    DrawSync(0);
#ifdef PLATFORM_PC
    /* Enhanced text: each character of the shown row (the quads of
     * setupSaveTitleGlyphQuads read v F0h..FFh of this image) is a glyph
     * piece keyed by its narrowed BIOS bits, so a glyph pack made from the
     * player's own BIOS (tools/glyph_bios_pack.py) can overlay it in HD. */
    for (row = 0; row < 16; row++) {
        if (drawn & (1u << row)) {
            pcGlyphTile(GetTPage(0, 0, 0x140, 0x80), row * 16, 0xF0, 12, 16);
        }
    }
#endif
    freeHeap(bits);
    freeHeap(image);
}

/* memory card screen: play time and slot number of a save */
void setupSavePlayTimeAndNumber(MenuSaveSummary* info) {
    s32 i;

    setupStringInPolyFT4(gMenuContext->m2DC_font, 0xEE, gMenuContext->m34C_saveScreen->m240C_separator1, gMenuContext->m308_oddOrEven, savePlayTimeGlyphX[0], 0x7A, 0x1000);
    setupStringInPolyFT4(gMenuContext->m2DC_font, 0xEE, gMenuContext->m34C_saveScreen->m24AC_separator2, gMenuContext->m308_oddOrEven, savePlayTimeGlyphX[1], 0x7A, 0x1000);
    updatePlayTimeString(info->m0_playTime);
    for (i = 0; i < 7; i++) {
        setupStringInPolyFT4(gMenuContext->m2DC_font, gMenuContext->m2EC_playTime[i], gMenuContext->m34C_saveScreen->m254C_playTime[i],
                      gMenuContext->m308_oddOrEven, savePlayTimeGlyphX[i + 2], 0x7A, 0x1000);
    }
#if !VERSION_JP
    setupStringInPolyFT4(gMenuContext->m2DC_font, 0x17, gMenuContext->m34C_saveScreen->m2C7C_slotLabel1, gMenuContext->m308_oddOrEven, 8, 0x66, 0x1000);
    setupStringInPolyFT4(gMenuContext->m2DC_font, 0x32, gMenuContext->m34C_saveScreen->m2CCC_slotLabel2, gMenuContext->m308_oddOrEven, 0x10, 0x66, 0x1000);
    setupStringInPolyFT4(gMenuContext->m2DC_font, (info->m23_slot + 1) / 10, gMenuContext->m34C_saveScreen->m2D1C_slotTens, gMenuContext->m308_oddOrEven, 0x10, 0x6E, 0x1000);
    setupStringInPolyFT4(gMenuContext->m2DC_font, (info->m23_slot + 1) % 10, gMenuContext->m34C_saveScreen->m2D6C_slotOnes, gMenuContext->m308_oddOrEven, 0x18, 0x6E, 0x1000);
#endif
}

/* memory card screen: leader name sprite of a save slot */
void setupSaveSlotNameSprite(u8 slot, MenuSaveSummary* info) {
    setupStringInPolyFT4(gMenuContext->m2DC_font, info->m1C_partyMemberId[slot] + 0x14E, &gMenuContext->m34C_saveScreen->mA98_slots[slot].m0_name,
                  gMenuContext->m308_oddOrEven, saveSlotMemberGlyphX[slot], saveSlotMemberGlyphY[slot], 0x1000);
}

/* memory card screen: leader level digits (level2 buffer filled but not drawn) */
void setupSaveSlotLevel(u8 slot, MenuSaveSummary* info) {
    s32 i;

    fillStringDisplayBuffer(info->m16_level[slot]);
    gMenuContext->m34C_saveScreen->mA98_slots[slot].m870_levelLength = 0;
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 6] != 0xFF) {
            gMenuContext->m34C_saveScreen->mA98_slots[slot].m870_levelLength += setupStringInPolyFT4(gMenuContext->m2DC_font,
                gMenuContext->m31C_stringDisplayBuffer[i + 6],
                &gMenuContext->m34C_saveScreen->mA98_slots[slot].m320_level[gMenuContext->m34C_saveScreen->mA98_slots[slot].m870_levelLength],
                gMenuContext->m308_oddOrEven, slot * 0x50 + saveSlotLevelPositionX + i * 8, saveSlotLevelPositionY, 0x1000);
        }
    }
    fillStringDisplayBuffer(info->m19_level2[slot]);
    gMenuContext->m34C_saveScreen->mA98_slots[slot].m871_level2Length = 0;
}

/* memory card screen: leader HP / max HP digits */
void setupSaveSlotHp(u8 slot, MenuSaveSummary* info) {
    s32 i;
    s32 j;

    fillStringDisplayBuffer(info->m4_hp[slot]);
    gMenuContext->m34C_saveScreen->mA98_slots[slot].m872_hpLength = 0;
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 6] != 0xFF) {
            gMenuContext->m34C_saveScreen->mA98_slots[slot].m872_hpLength += setupStringInPolyFT4(gMenuContext->m2DC_font,
                gMenuContext->m31C_stringDisplayBuffer[i + 6],
                &gMenuContext->m34C_saveScreen->mA98_slots[slot].m500_hp[gMenuContext->m34C_saveScreen->mA98_slots[slot].m872_hpLength],
                gMenuContext->m308_oddOrEven, slot * 0x50 + saveSlotHpPositionX + i * 8, saveSlotHpPositionY, 0x1000);
        }
    }
    j = 0;
    fillStringDisplayBuffer(info->mA_maxHp[slot]);
    gMenuContext->m34C_saveScreen->mA98_slots[slot].m873_maxHpLength = 0;
    for (i = 0; i < 3; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 6] != 0xFF) {
            gMenuContext->m34C_saveScreen->mA98_slots[slot].m873_maxHpLength += setupStringInPolyFT4(gMenuContext->m2DC_font,
                gMenuContext->m31C_stringDisplayBuffer[i + 6],
                &gMenuContext->m34C_saveScreen->mA98_slots[slot].m5F0_maxHp[gMenuContext->m34C_saveScreen->mA98_slots[slot].m873_maxHpLength],
                gMenuContext->m308_oddOrEven, slot * 0x50 + saveSlotMaxHpPositionX + j * 8, saveSlotMaxHpPositionY, 0x1000);
            j++;
        }
    }
}

/* memory card screen: leader MP / max MP digits */
void setupSaveSlotMp(u8 slot, MenuSaveSummary* info) {
    s32 i;
    s32 j;

    fillStringDisplayBuffer(info->m10_mp[slot]);
    gMenuContext->m34C_saveScreen->mA98_slots[slot].m874_mpLength = 0;
    for (i = 0; i < 2; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 7] != 0xFF) {
            gMenuContext->m34C_saveScreen->mA98_slots[slot].m874_mpLength += setupStringInPolyFT4(gMenuContext->m2DC_font,
                gMenuContext->m31C_stringDisplayBuffer[i + 7],
                &gMenuContext->m34C_saveScreen->mA98_slots[slot].m6E0_mp[gMenuContext->m34C_saveScreen->mA98_slots[slot].m874_mpLength],
                gMenuContext->m308_oddOrEven, slot * 0x50 + saveSlotMpPositionX + i * 8, saveSlotMpPositionY, 0x1000);
        }
    }
    j = 0;
    fillStringDisplayBuffer(info->m13_maxMp[slot]);
    gMenuContext->m34C_saveScreen->mA98_slots[slot].m875_maxMpLength = 0;
    for (i = 0; i < 2; i++) {
        if (gMenuContext->m31C_stringDisplayBuffer[i + 7] != 0xFF) {
            gMenuContext->m34C_saveScreen->mA98_slots[slot].m875_maxMpLength += setupStringInPolyFT4(gMenuContext->m2DC_font,
                gMenuContext->m31C_stringDisplayBuffer[i + 7],
                &gMenuContext->m34C_saveScreen->mA98_slots[slot].m780_maxMp[gMenuContext->m34C_saveScreen->mA98_slots[slot].m875_maxMpLength],
                gMenuContext->m308_oddOrEven, slot * 0x50 + saveSlotMaxMpPositionX + j * 8, saveSlotMaxMpPositionY, 0x1000);
            j++;
        }
    }
}

/* memory card screen: render a save's leader name (from the save summary) to VRAM */
void renderSaveSlotNameImage(u8 slot, MenuSaveSummary* info, s32 cardSlot) {
    RECT rect;
    u8 name[0x18];
    u8 text[0x18];
    MenuSaveSummary* summary;
    u16* image;
    s32 i;

    summary = &gMenuContext->m32C_memoryCardContext->mB94_fileHeaders[cardSlot].m100_summary;
    for (i = 0; i < 20; i += 2) {
        name[i] = summary->m24_characterNames.m0_names[info->m1C_partyMemberId[slot]].m0_name[i];
        name[i + 1] = summary->m24_characterNames.m0_names[info->m1C_partyMemberId[slot]].m0_name[i + 1];
        if (name[i] == 0 && name[i + 1] == 0) {
            break;
        }
    }
    decodeCharacterCodes((u16*)name, text, i / 2);
    image = ALLOC_TEXT_IMAGE(0x3F6);
    bzero(image, 0x3F6);
    renderString(text, image, 0x24, 0);
    rect.x = infocardNameU[slot + 6] + 0x180;
    rect.y = infocardNameV[slot + 6];
    rect.w = 0x28;
    rect.h = 0xD;
    LoadImage(&rect, image);
    DrawSync(0);
    freeHeap(image);
}

/* memory card screen: 16 save-name glyph quads (m34C->m277C) */
void setupSaveTitleGlyphQuads(void) {
    s32 i;

    for (i = 0; i < 16; i++) {
        initMemoryCardTransparentPoly(&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven]);
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->x0 = saveTitleGlyphX + i * 12;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->y0 = saveTitleGlyphY;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->x1 = saveTitleGlyphX + i * 12 + 12;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->y1 = saveTitleGlyphY;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->x2 = saveTitleGlyphX + i * 12;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->y2 = saveTitleGlyphY + 16;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->x3 = saveTitleGlyphX + i * 12 + 12;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->y3 = saveTitleGlyphY + 16;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->u0 = i * 16;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->v0 = 0xF0;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->u1 = i * 16 + 12;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->v1 = 0xF0;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->u2 = i * 16;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->v2 = 0xFF;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->u3 = i * 16 + 12;
        (&gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven])->v3 = 0xFF;
        gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven].tpage = GetTPage(0, 0, 0x140, 0x80);
        gMenuContext->m34C_saveScreen->m277C_titleGlyphs[0][i * 2 + gMenuContext->m308_oddOrEven].clut = GetClut(0, 0x1C0);
    }
}

/* memory card screen: build the summary display of one save (party, play time, name) */
void setupSaveSummaryDisplay(s32 cardSlot) {
    MenuSaveSummary* summary;
    s32 i;

    summary = &gMenuContext->m32C_memoryCardContext->mB94_fileHeaders[cardSlot].m100_summary;
    setupSaveSlotLabels();
    for (i = 0; i < 3; i++) {
        if (summary->m1C_partyMemberId[i] != 0xFF) {
            gMenuContext->m34C_saveScreen->mA98_slots[i].m878_enabled = 1;
            setupSaveSlotNameSprite(i, summary);
            setupSaveSlotLevel(i, summary);
            setupSaveSlotHp(i, summary);
            setupSaveSlotMp(i, summary);
            renderSaveSlotNameImage(i, summary, cardSlot);
        } else {
            gMenuContext->m34C_saveScreen->mA98_slots[i].m878_enabled = 0;
        }
        gMenuContext->m34C_saveScreen->mA98_slots[i].m877_oddOrEven = gMenuContext->m308_oddOrEven;
    }
    setupSavePlayTimeAndNumber(summary);
    setupSaveTitleGlyphQuads();
}

/* memory card screen: show a save (title image, optional summary), m33C->mB = 1 */
void showMemoryCardSave(s32 cardSlot, u8 withSummary);
void showMemoryCardSave(s32 cardSlot, u8 withSummary) {
    clearSaveTitleImage();
    if (cardSlot != 0xFF) {
        if (withSummary) {
            setupSaveSummaryDisplay(cardSlot);
            renderSaveTitleImage(cardSlot);
            gMenuContext->m34C_saveScreen->m2DBC_showSummary = 1;
        } else {
            renderSaveTitleImage(cardSlot);
            gMenuContext->m34C_saveScreen->m2DBC_showSummary = 0;
        }
        gMenuContext->m33C_drawFlags->mB_drawSaveScreen = 1;
    }
}

/* Noah loadMemoryCardIconToVram: upload the palette and three icon frames,
 * then select the six animation rows for this save slot. */
void loadMemoryCardIconToVram(s32 slot) {
    u8 invalid;
    s32 i;
    s32 base;
    s32 y;
    s32 index;    /* Save-record offset in 32-bit words. */
    s32 offset;   /* Mode 12 byte offset; mode 13 third-frame row. */
    s32 offset13; /* Mode 11 final-frame row; mode 13 byte offset. */
    s32 offset11; /* Mode 11 byte offset for the first frame. */

    invalid = 1;
    tempRectForMemoryCardIconLoading.x = slot * 4 - (s16)((slot / 16) * 0x40 - 0x140);
    tempRectForMemoryCardIconLoading.w = 4;
    tempRectForMemoryCardIconLoading.h = 0x10;
    tempRectForMemoryCardIconPaletteLoading.x = slot << 4;
    tempRectForMemoryCardIconPaletteLoading.y = slot / 16 + 0x1C1;
    tempRectForMemoryCardIconPaletteLoading.w = 0x10;
    tempRectForMemoryCardIconPaletteLoading.h = 1;
    memmove(memoryCardIconPaletteStaging, gMenuContext->m32C_memoryCardContext->mB94_fileHeaders[slot].m60_iconsPalette, 0x20);
    LoadImage(&tempRectForMemoryCardIconPaletteLoading, memoryCardIconPaletteStaging);
    DrawSync(0);
    for (i = 0; i < 3; i++) {
        tempRectForMemoryCardIconLoading.y = i * 0x20 + (y = (base = (slot / 16) * 16) + 0x80);
        LoadImage(&tempRectForMemoryCardIconLoading, &gMenuContext->m32C_memoryCardContext->mB94_fileHeaders[slot].m80_icon[i * 0x80]);
        DrawSync(0);
    }
    switch (gMenuContext->m32C_memoryCardContext->mB94_fileHeaders[slot].m2_iconDisplayFlag) {
    case 0x11:
        index = slot * 23;
        offset11 = index * 4;
        ((MenuContext_32C_5C*)((u8*)gMenuContext->m32C_memoryCardContext->m0_files + offset11))->m0_iconV[0] = y;
        gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[1] = y;
        gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[2] = y;
        gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[3] = y;
        gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[4] = y;
        offset13 = y;
        gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[5] = offset13;
        invalid = 0;
        break;
    case 0x12:
    {
        s32 rowA0;
        /* Two single-pass blocks preserve gcc-2.6.3 allocator weights.
         * Both execute exactly once; retain them for the matching build. */
        do {
            do {
                index = slot * 23;
                offset = index * 4;
                rowA0 = base + 0xA0;
                ((MenuContext_32C_5C*)((u8*)gMenuContext->m32C_memoryCardContext->m0_files + offset))->m0_iconV[0] = y;
                gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[1] = rowA0;
                gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[2] = y;
                gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[3] = rowA0;
                gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[4] = y;
                gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[5] = rowA0;
                invalid = 0;
            } while (0);
        } while (0);
        break;
    }
    case 0x13:
    {
        s32 rowA0;
        index = slot * 23;
        offset13 = index * 4;
        rowA0 = base + 0xA0;
        ((MenuContext_32C_5C*)((u8*)gMenuContext->m32C_memoryCardContext->m0_files + offset13))->m0_iconV[0] = y;
        gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[1] = rowA0;
        offset = base + 0xC0;
        gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[2] = offset;
        gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[3] = y;
        gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[4] = rowA0;
        gMenuContext->m32C_memoryCardContext->m0_files[slot].m0_iconV[5] = offset;
        invalid = 0;
        break;
    }
    }
    processLoadSaveMenuSub6Var2[slot / 16] += gMenuContext->m32C_memoryCardContext->mB94_fileHeaders[slot].m3_blockNumber;
    if (invalid) {
        gMenuContext->m32C_memoryCardContext->m0_files[slot].m58_loaded = 0;
    }
}

/* Noah initMenuContext_1DE0: set up a label's two POLY_FT4s (texture page,
 * UVs, CLUT) for text rendered at index/startY (mode 0) or for a mode-given
 * texture (mode & 0x7F: colour mode + 1; bit 7 clear: semi-transparent) */
void initMenuContext_1DE0(MenuContext_4E0* label, s32 index, s32 startY, u8 mode) {
    s32 i;
    POLY_FT4* p;
    u8 semiTrans;

    for (i = 0; i < 2; i++) {
        p = &label->m0_polys[i];
        semiTrans = 0;
        initMemoryCardTransparentPoly(p);
        if (mode == 0) {
            label->m7C_colorMode = index & 1;
            p->tpage = GetTPage(0, 0, 0x140, 0);
            setUVWH(p, ((index / 2) & 1) << 7, ((index + startY) / 4) * 13, label->m7E_stringWidth, 13);
        } else {
            if (!(mode & 0x80)) {
                semiTrans = 0x20;
                SetSemiTrans(p, 1);
                p->r0 = semiTrans;
                p->g0 = semiTrans;
                p->b0 = semiTrans;
            }
            label->m7C_colorMode = (mode & 0x7F) - 1;
            p->tpage = semiTrans | GetTPage(0, 0, 0x180, 0x80);
            setUVWH(p, (index & 1) * 0x60, (index / 2) * 13 + startY, label->m7E_stringWidth, 13);
        }
        label->m0_polys[i].clut = label->m7C_colorMode ? textSpriteMode1 : textSpriteMode0;
    }
    label->m7F_transformEnabled = 0;
}

/* Noah setupMenuContext4E0: render pairs of menu labels to VRAM and set their quads */
void setupMenuContext4E0(MenuContext_4E0* labels, u8* config, s32 startY, s32 count) {
    RECT* rect;
    s32 i;

    for (i = 0; i < count; i += 2) {
        labels[i].m7E_stringWidth = renderString(getDialogParamPointer(gMenuContext->m2E0_textBundle, config[i]),
                                                  gMenuContext->m4E0_labels[0].m78_imageData, 0x18, 0);
        rect = &labels[i].m70_rect;
        labels[i + 1].m7E_stringWidth = renderString(getDialogParamPointer(gMenuContext->m2E0_textBundle, config[i + 1]),
                                                      gMenuContext->m4E0_labels[0].m78_imageData, 0x18, 1);
        labels[i].m70_rect.x = ((i / 2) & 1) * 0x20 + 0x140;
        labels[i].m70_rect.y = ((i + startY) / 4) * 13;
        labels[i].m70_rect.w = 0x1C;
        labels[i].m70_rect.h = 0xD;
        labels[i + 1].m70_rect = labels[i].m70_rect;
        initMenuContext_1DE0(&labels[i], i, startY, 0);
        initMenuContext_1DE0(&labels[i + 1], i + 1, startY, 0);
        LoadImage(rect, gMenuContext->m4E0_labels[0].m78_imageData);
        DrawSync(0);
    }
}
