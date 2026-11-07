/*
 * The field save block (src/field/f_800A19B0.c: saveFieldStateToSaveBlock,
 * loadFieldStateFromSaveBlock, restoreFieldStateFromSaveBlock) copies two PS1
 * address ranges that span several globals with one memcpy each:
 *
 *   FIELD_SAVE_BACKGROUND  0x800B007C, 0x38 bytes: backgroundPoly, then
 *                          OPX_80Params;
 *   FIELD_SAVE_EFFECTS     0x800B2078, 0x2E4 bytes: the screen effects
 *                          (fieldScreenEffects), six small globals, then the first
 *                          0x1D8 bytes of op9DVar1.
 *
 * On the PC those globals are separate objects, and some are bigger
 * (backgroundPoly is a pointer, FieldScreenEffects holds primitives), so each
 * range is copied object by object, in PS1 address order, at its PC size.
 * The save block's cursor moves by pcFieldSaveRangeSize().
 */
#include "common.h"
#include "psyq.h"
#include "field/field.h"
#include "field/screen_effects.h"

extern void* backgroundPoly;
extern FieldBss_800B0080 OPX_80Params;
extern FieldScreenEffects fieldScreenEffects;
extern u8 windowOpenBF[2];
extern u8 fieldRandomBattleVar[2];
extern u8 deltaMoveVar0[2];
extern u8 inputAllowedMask2[2];
extern u8 dialogWindowFlag2[4];
extern u8 numInitializedFieldScriptEntities[4];
extern FieldBss_800B2184 op9DVar1;

typedef struct {
    void* p;
    u32 size;
} SavePiece;

/* op9DVar1 (0x800B2184) up to the range's end, 0x800B235C. */
#define OP9D_SAVED (0x800B235C - 0x800B2184)

static const SavePiece background[] = {
    {&backgroundPoly, sizeof(backgroundPoly)},
    {&OPX_80Params, sizeof(OPX_80Params)},
};

static const SavePiece effects[] = {
    {&fieldScreenEffects, sizeof(fieldScreenEffects)},
    {windowOpenBF, sizeof(windowOpenBF)},
    {fieldRandomBattleVar, sizeof(fieldRandomBattleVar)},
    {deltaMoveVar0, sizeof(deltaMoveVar0)},
    {inputAllowedMask2, sizeof(inputAllowedMask2)},
    {dialogWindowFlag2, sizeof(dialogWindowFlag2)},
    {numInitializedFieldScriptEntities, sizeof(numInitializedFieldScriptEntities)},
    {&op9DVar1, OP9D_SAVED},
};

/* The PS1 ranges are contiguous: the pieces must add up to them. */
typedef char backgroundCheck[sizeof(OPX_80Params) == 0x34 ? 1 : -1];
typedef char effectsCheck[sizeof(op9DVar1) >= OP9D_SAVED ? 1 : -1];

static void pieces(s32 range, const SavePiece** p, s32* n) {
    if (range == FIELD_SAVE_BACKGROUND) {
        *p = background;
        *n = sizeof(background) / sizeof(background[0]);
    } else {
        *p = effects;
        *n = sizeof(effects) / sizeof(effects[0]);
    }
}

s32 pcFieldSaveRangeSize(s32 range) {
    const SavePiece* p;
    s32 n, i, size = 0;

    pieces(range, &p, &n);
    for (i = 0; i < n; i++) {
        size += p[i].size;
    }
    return size;
}

u8* pcFieldSaveRange(u8* cursor, s32 range, s32 load) {
    const SavePiece* p;
    s32 n, i;

    pieces(range, &p, &n);
    for (i = 0; i < n; i++) {
        if (load) {
            __builtin_memcpy(p[i].p, cursor, p[i].size);
        } else {
            __builtin_memcpy(cursor, p[i].p, p[i].size);
        }
        cursor += p[i].size;
    }
    return cursor;
}
