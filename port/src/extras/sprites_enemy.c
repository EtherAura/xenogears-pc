#include "sprites_enemy.h"
#include <string.h>

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int rd16(const uint8_t* p) { return p[0] | p[1] << 8; }

int xgEnemySpriteCount(const uint8_t* data, int size) {
    if (!data || size < 8 || !data[0] || data[0] > 8) return 0;
    int header = 8 + 12 * data[0];
    /* +4 ends the control-data copy; sheets follow it in the same file. */
    uint32_t end = rd32(data + 4);
    return header <= size && end >= (uint32_t)header && end <= (uint32_t)size ? data[0] : 0;
}

int xgEnemySpriteGet(const uint8_t* data, int size, int slot, XgEnemySprite* out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    int count = xgEnemySpriteCount(data, size);
    if (slot < 0 || slot >= count) return 0;
    const uint8_t* chosen = data + 8 + 12 * slot;
    if (chosen[8]) return -1;
    uint32_t controlEnd = rd32(data + 4), sheetAt[8];
    int sheetX[8], uploaded = 0, x = data[1] * 0x40 + 0x140;
    for (int i = 0; i <= slot; i++) {
        const uint8_t* e = data + 8 + 12 * i;
        uint32_t at = rd32(e + 4);
        if (e[8]) {
            if (at >= 8) x += 0x40;
            continue;
        }
        int vx;
        if (at < 8) {
            if (at >= (uint32_t)uploaded) return 0;
            vx = sheetX[at];
            at = sheetAt[at];
        } else {
            if (at < controlEnd || at > (uint32_t)size - 4) return 0;
            uint32_t columns = rd32(data + at);
            if (!columns || columns > 16 || at + 4 + 4 * columns > (uint32_t)size) return 0;
            vx = x;
            sheetAt[uploaded] = at; sheetX[uploaded++] = x;
            x += (int)columns * 0x40;
            if (x > 0x2C0) x = 0;
        }
        if (i != slot) continue;
        uint32_t bundle = rd32(e), sheetEnd = (uint32_t)size;
        for (int j = 0; j < count; j++) {
            uint32_t t = rd32(data + 12 + 12 * j);
            if (t > at && t < sheetEnd) sheetEnd = t;
        }
        if (bundle < (uint32_t)(8 + 12 * count) || bundle >= controlEnd || sheetEnd < at + 4) return 0;
        uint32_t columns = rd32(data + at);
        if (!columns || columns > 16 || 4 + 4 * columns > sheetEnd - at) return 0;
        for (uint32_t j = 0; j < columns; j++) {
            uint32_t offset = rd32(data + at + 4 + 4 * j);
            if (offset < 4 + 4 * columns || offset > sheetEnd - at - 4) return 0;
            const uint8_t* image = data + at + offset;
            int w = rd16(image), h = rd16(image + 2);
            if (!w || !h || w > 1024 || h > 512 || (uint32_t)(w * h * 2) > sheetEnd - at - offset - 4) return 0;
        }
        /* Keep the native control-data tail: some configuration palettes
         * read past an individual bundle into that same copy. */
        out->bundleOffset = (int)bundle; out->bundleSize = (int)(controlEnd - bundle);
        out->sheetOffset = (int)at; out->sheetSize = (int)(sheetEnd - at);
        out->vramX = vx; out->palette = e[11];
        return 1;
    }
    return 0;
}
