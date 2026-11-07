/*
 * Battle effect files (tables.h "battle effects"): what abilityPlayAnimationSub1
 * and loadEffectFragmentsAndAudio (battle overlay) read for ability n:
 *   0E/22+2n   an offset bundle: members 0-2 a sprite animation bundle's
 *              (animation table, frames, palettes; set up at VRAM 0x380,0x100
 *              with CLUT row 0x1F4), then any number of further members, each
 *              a SEDS bank ("seds": loadSequence), a WDS bank ("wds ":
 *              loadWds, then the file is cut after it when it is the last) or
 *              another sprite bundle (initFieldEntitySub4Sub5Sub0, same place);
 *              bits 12-13 of the animation table's first word choose who stays
 *              on screen (abilityPlayAnimationSub1);
 *   0E/23+2n   its texture uploads (sector-aligned, loadImageFileToVram),
 *              skipped when 16 bytes or less.
 * Files 0E/02-21 are the battle's effect code overlays (battleLoadFileFrom0xC).
 */

#include "tables.h"

#include <stdlib.h>
#include <string.h>

#include "tables_int.h"

#define FX_DIR 0x0E
#define FX_FIRST 0x22

static struct {
    int loaded;
    int count;
    XgBattleEffect* fx;
} B;

static void readEffect(XgDisc* d, int n, XgBattleEffect* e) {
    memset(e, 0, sizeof(*e));
    e->id = n;
    e->file = FX_FIRST + 2 * n;
    e->sedsId = -1;
    e->wdsBank = -1;
    int size;
    uint8_t* b = tablesRead(d, FX_DIR, e->file, &size);
    int texEntry = xgDiscFileEntry(d, FX_DIR, e->file + 1);
    int tex = texEntry >= 0 ? xgDiscEntry(d, texEntry).size : 0;
    if (tex > 0x10) {
        e->textureFile = e->file + 1;
        e->textureSize = tex;
    }
    if (b == NULL || size < 16) {
        free(b);
        return;
    }
    e->size = size;
    uint32_t count = tablesRd32(b);
    if (count < 3 || count > 64 || 4 + 4 * count > (uint32_t)size) {
        free(b); /* not a bundle: a placeholder */
        e->size = 0;
        return;
    }
    e->members = (int)count;
    uint32_t table = tablesRd32(b + 4);
    if (table + 2 <= (uint32_t)size) {
        uint16_t w = tablesRd16(b + table);
        e->mode = (w >> 12) & 3;
        e->animations = w & 0x3F;
    }
    for (uint32_t i = 3; i < count; i++) {
        uint32_t o = tablesRd32(b + 4 + 4 * i);
        if (o + 0x18 > (uint32_t)size) {
            continue;
        }
        if (memcmp(b + o, "seds", 4) == 0) {
            if (e->seds++ == 0) {
                e->sedsId = tablesRd16(b + o + 0x14);
            }
        } else if (memcmp(b + o, "wds ", 4) == 0) {
            if (e->wds++ == 0) {
                e->wdsBank = (int)tablesRd32(b + o + 0x20);
            }
        } else {
            e->sprites++;
        }
    }
    free(b);
}

static void loadEffects(void) {
    if (B.loaded) {
        return;
    }
    B.loaded = 1;
    XgDisc* d = tablesDisc(1);
    int files = d != NULL ? xgDiscDirFiles(d, FX_DIR) : 0;
    int n = files >= FX_FIRST ? (files - FX_FIRST) / 2 + 1 : 0;
    B.fx = calloc((size_t)(n > 0 ? n : 1), sizeof(*B.fx));
    if (B.fx == NULL) {
        return;
    }
    for (int i = 0; i < n; i++) {
        readEffect(d, i, &B.fx[i]);
    }
    B.count = n;
}

int xgBattleEffectCount(void) {
    loadEffects();
    return B.count;
}

int xgBattleEffectGet(int n, XgBattleEffect* out) {
    loadEffects();
    if (n < 0 || n >= B.count) {
        return 0;
    }
    *out = B.fx[n];
    return 1;
}

void tablesBattleReset(void) {
    free(B.fx);
    memset(&B, 0, sizeof(B));
}
