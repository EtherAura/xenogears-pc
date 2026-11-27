/* The gauntlet uses the memory card's party data at a fresh battle boot. */
#include "common.h"
#include "pc_retail_heap.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "menu/menu.h"
#include "pc_arena.h"
#include "pc_convert.h"
#include "../../card/party_record.h"

void decodeCharacterCodes(u16* codes, u8* out, u32 count);
int pcStartIsolatedMusic(s32 id, s32 wave, s32 flags);
void pcVersusRebuildGearStats(MenuContext_330* ctx, u8 gearId, u8 pilot);
extern u16 battleScriptGameStateVars[16];

void pcGauntletDecodeName(const u8* raw, u8* name) {
    u8 text[20] = {0};
    u32 index, used = 0;
    for (index = 0; index < 20; index += 2) {
        u8 glyph[3];
        u16 code = raw[index] | (raw[index + 1] << 8);
        if (code == 0 || code >= 0x144) break;
        decodeCharacterCodes(&code, glyph, 1);
        u32 width = glyph[1] != 0 ? 2 : 1;
        if (used + width >= sizeof text) break;
        __builtin_memcpy(text + used, glyph, width);
        used += width;
    }
    __builtin_memcpy(name, text, sizeof text);
}

static void** readGearTables(u32** storage) {
    s32 dir, sub;
    getCurrentDirectory(&dir, &sub);
    setCurrentDirectory(0x10, 0);
    *storage = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(2)), 1);
    readFile(2, *storage, 0, 0x80);
    waitReadCompletion(0);
    setCurrentDirectory(dir, sub);
    return pcRelocTable(*storage);
}

static void loadGearParts(MenuContext_330* items, void** bundle) {
    items->m8_gearFrames = (void*)mallocAndDecompress(bundle[17], 0);
    items->m10_gearArmors = (void*)mallocAndDecompress(bundle[18], 0);
    items->mC_gearEngines = (void*)mallocAndDecompress(bundle[19], 0);
    items->m14_gearAccessories = (void*)mallocAndDecompress(bundle[20], 0);
}

static void freeGearParts(MenuContext_330* items) {
    freeHeap(items->m14_gearAccessories);
    freeHeap(items->mC_gearEngines);
    freeHeap(items->m10_gearArmors);
    freeHeap(items->m8_gearFrames);
}

void pcVersusApplySavedGears(const XgPartyRecord* party) {
    s32 i;
    u32* raw;
    MenuContext_330 items;
    void** bundle = readGearTables(&raw);
    u32 frames = *(u32*)bundle[17] / sizeof(MenuGearFrame);
    u32 armors = *(u32*)bundle[18] / sizeof(MenuGearArmor);
    u32 engines = *(u32*)bundle[19] / sizeof(MenuGearEngine);
    u32 accessories = *(u32*)bundle[20] / sizeof(MenuGearAccessory);
    u32 weapons = *(u32*)bundle[43] / sizeof(MenuGearWeapon);
    loadGearParts(&items, bundle);
    items.m18_gearWeapons = (void*)mallocAndDecompress(bundle[43], 0);
    for (i = 0; i < party->m3_count; i++) {
        GameStateCharacter* pilot = &gameState.m26C_party[party->m4_members[i]];
        u8 id = pilot->mA0_gearNum;
        if (id >= 20) continue;
        GameStateGear equipped = gameState.m978_gears[id];
        __builtin_memcpy(&equipped, party->m3B8_gearEquip[i], XG_PARTY_GEAR_EQUIP);
        if (equipped.m2_frame >= frames || items.m8_gearFrames[equipped.m2_frame].m4_hp == 0 ||
            equipped.m8_armor >= armors || equipped.m3_engine >= engines ||
            equipped.m9_accessorySlots[0] >= accessories || equipped.m9_accessorySlots[1] >= accessories ||
            equipped.m9_accessorySlots[2] >= accessories || equipped.mC_weapon[0] >= weapons ||
            ((id == 5 || id == 13) && (equipped.m4_slotItem[0] >= weapons || equipped.m4_slotItem[1] >= weapons ||
                                      equipped.m4_slotItem[3] >= weapons))) {
            pcArenaLog("saved Gear %d has invalid parts", id);
            pilot->mA0_gearNum = 0xFF;
            continue;
        }
        GameStateGear* gear = &gameState.m978_gears[id];
        *gear = equipped;
        pcVersusRebuildGearStats(&items, id, party->m4_members[i]);
        gear->m38_fuel = gear->m3A_maxFuel;
    }
    freeHeap(items.m18_gearWeapons);
    freeGearParts(&items);
    freeHeap(bundle);
    freeHeap(raw);
}

int pcGauntletRestoreSave(const u8* block) {
    MenuSaveGame save;
    MenuContext_330 items;
    u32* raw;
    s32 i, j;
    __builtin_memcpy(&save, block + 0x100, sizeof save);
    void** bundle = readGearTables(&raw);
    /* The save keeps Gear part indices; the normal card loader uses these
     * disc tables to rebuild each Gear's derived stats from its parts. */
    u32 frames = *(u32*)bundle[17] / sizeof(MenuGearFrame);
    u32 armors = *(u32*)bundle[18] / sizeof(MenuGearArmor);
    u32 engines = *(u32*)bundle[19] / sizeof(MenuGearEngine);
    u32 accessories = *(u32*)bundle[20] / sizeof(MenuGearAccessory);
    s32 valid = 1;
    for (i = 0; i < 20; i++) {
        const u8* equip = (const u8*)&save.m99C_gears[i].m0_equip;
        if (equip[offsetof(GameStateGear, m2_frame)] >= frames ||
            equip[offsetof(GameStateGear, m8_armor)] >= armors ||
            equip[offsetof(GameStateGear, m3_engine)] >= engines) {
            valid = 0;
        }
        for (j = 0; j < 3; j++) {
            if (equip[offsetof(GameStateGear, m9_accessorySlots) + j] >= accessories) valid = 0;
        }
    }
    if (valid) {
        loadGearParts(&items, bundle);
        u16 bootField = gameState.m231A_fieldID;
        restoreSaveGameData(&save, &items);
        gameState.m231A_fieldID = bootField;
        for (i = 0; i < 31; i++) pcGauntletDecodeName(gameState.m0_names[i], gameState.m0_names[i]);
        __builtin_memcpy(battleScriptGameStateVars, gameState.m2324_battleScriptVars, sizeof battleScriptGameStateVars);
        freeGearParts(&items);
    }
    freeHeap(bundle);
    freeHeap(raw);
    return valid;
}

void pcGauntletStartMusic(int id) {
    int wave, flags;
    if (!pcArenaMusicSelector(id, &wave, &flags)) id = 0x1D;
    if (pcArenaMusicSelector(id, &wave, &flags)) pcStartIsolatedMusic(id, wave, flags);
}
