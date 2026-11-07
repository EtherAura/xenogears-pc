/*
 * PC start options for the world map, for validation runs without story
 * saves. worldmapEntryPoint applies them right after its own defaults (the
 * ones the kernel menu's Worldmap entry gets), the same moment the Redux
 * scripts poke the same gameState words (US 0x80070F38, the
 * resetMemoryAllocStats(3, 0) call):
 *
 *   XG_WM_MODE=n        gameState.m2320_worldmapMode: 1-7 the world map, 8-18
 *                       the event modes
 *   XG_WM_SCENARIO=n    gameState.m1930_fieldVarsBackup[0], the story state
 *                       that picks the map configuration below mode 8
 *   XG_WM_POS=n         start at the configuration's position n
 *                       (m231E_worldmapInitialPositionIndex; also clears
 *                       m1836_restoreWorldmapPosition so the file's position
 *                       is used, as after a field exit)
 *   XG_WM_VEHICLE=n     gameState.m1834_vehicleMode (the kernel menu's is
 *                       0x4003, the flying Yggdrasil; 0 on foot)
 *   XG_WM_GEAR=1        the party rides its gears (m22B1_isOnGear)
 *   XG_WM_PARTY=a,b,c   gameState.m1D34_currentParty (255: an empty slot)
 *   XG_WM_POKE=OFF[:SIZE]=VALUE,...
 *                       any gameState field, as XG_GAMESTATE
 *                       (port/src/system/options.c) but at this later
 *                       moment: OFF its PS1 byte offset (the layout is the
 *                       same on the PC), SIZE 1, 2 (default) or 4
 *
 * Numbers are decimal, or hex with 0x. This file is compiled like the game C
 * (no libc headers); the text comes from portOption (port/src/system/options.c).
 */
#include "common.h"
#include "main/game_state.h"

const char* portOption(const char* name);

/* A number at *s (decimal, 0x hex, a leading '-'); *s moves past it. */
static s32 parseNumber(const char** s) {
    const char* p = *s;
    s32 sign = 1;
    s32 base = 10;
    s32 v = 0;

    if (*p == '-') {
        sign = -1;
        p++;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }
    for (;;) {
        s32 d;
        if (*p >= '0' && *p <= '9') {
            d = *p - '0';
        } else if (base == 16 && *p >= 'a' && *p <= 'f') {
            d = *p - 'a' + 10;
        } else if (base == 16 && *p >= 'A' && *p <= 'F') {
            d = *p - 'A' + 10;
        } else {
            break;
        }
        v = v * base + d;
        p++;
    }
    *s = p;
    return v * sign;
}

static s32 option(const char* name, s32* out) {
    const char* s = portOption(name);

    if (s == NULL) {
        return 0;
    }
    *out = parseNumber(&s);
    return 1;
}

void portWorldmapStart(void) {
    const char* s;
    s32 v;
    s32 i;

    if (option("XG_WM_MODE", &v)) {
        gameState.m2320_worldmapMode = v;
    }
    if (option("XG_WM_SCENARIO", &v)) {
        gameState.m1930_fieldVarsBackup[0] = v;
    }
    if (option("XG_WM_POS", &v)) {
        gameState.m231E_worldmapInitialPositionIndex = v;
        gameState.m1836_restoreWorldmapPosition = 0;
    }
    if (option("XG_WM_VEHICLE", &v)) {
        gameState.m1834_vehicleMode = v;
    }
    if (option("XG_WM_GEAR", &v)) {
        for (i = 0; i < 3; i++) {
            gameState.m22B1_isOnGear[i] = v != 0;
        }
    }
    s = portOption("XG_WM_PARTY");
    for (i = 0; s != NULL && i < 3 && *s != '\0'; i++) {
        gameState.m1D34_currentParty[i] = parseNumber(&s);
        if (*s == ',') {
            s++;
        }
    }
    s = portOption("XG_WM_POKE");
    while (s != NULL && *s != '\0') {
        u8* p = (u8*)&gameState;
        u32 off = parseNumber(&s);
        s32 size = 2;

        if (*s == ':') {
            s++;
            size = parseNumber(&s);
        }
        if (*s != '=') {
            break;
        }
        s++;
        v = parseNumber(&s);
        if ((size == 1 || size == 2 || size == 4) && off + size <= sizeof(gameState)) {
            /* little endian, as the PS1 */
            for (i = 0; i < size; i++) {
                p[off + i] = (u32)v >> (i * 8);
            }
        }
        if (*s == ',') {
            s++;
        }
    }
}
