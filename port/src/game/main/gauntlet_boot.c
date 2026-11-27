/* The gauntlet uses the memory card's party data at a fresh battle boot. */
#include "common.h"
#include "pc_retail_heap.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "menu/menu.h"
#include "pc_arena.h"
#include "pc_convert.h"

void decodeCharacterCodes(u16* codes, u8* out, u32 count);
int pcStartIsolatedMusic(s32 id, s32 wave, s32 flags);
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

int pcGauntletRestoreSave(const u8* block) {
    MenuSaveGame save;
    MenuContext_330 items;
    s32 dir, sub, i, j;
    __builtin_memcpy(&save, block + 0x100, sizeof save);
    getCurrentDirectory(&dir, &sub);
    setCurrentDirectory(0x10, 0);
    u32* raw = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(2)), 1);
    readFile(2, raw, 0, 0x80);
    waitReadCompletion(0);
    void** bundle = pcRelocTable(raw);
    setCurrentDirectory(dir, sub);
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
        items.m8_gearFrames = (void*)mallocAndDecompress(bundle[17], 0);
        items.m10_gearArmors = (void*)mallocAndDecompress(bundle[18], 0);
        items.mC_gearEngines = (void*)mallocAndDecompress(bundle[19], 0);
        items.m14_gearAccessories = (void*)mallocAndDecompress(bundle[20], 0);
        u16 bootField = gameState.m231A_fieldID;
        restoreSaveGameData(&save, &items);
        gameState.m231A_fieldID = bootField;
        for (i = 0; i < 31; i++) pcGauntletDecodeName(gameState.m0_names[i], gameState.m0_names[i]);
        __builtin_memcpy(battleScriptGameStateVars, gameState.m2324_battleScriptVars, sizeof battleScriptGameStateVars);
        freeHeap(items.m14_gearAccessories);
        freeHeap(items.mC_gearEngines);
        freeHeap(items.m10_gearArmors);
        freeHeap(items.m8_gearFrames);
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
