/* Private picture-member snapshots for text-image packs. No game data is
 * built in; the bounded format binds player-exported data to its source. */
#include <stdlib.h>
#include <string.h>
#include "gametext.h"

enum { PIC_TIMS = GT_PICTURE_TIMS, PIC_TEXTURES = GT_PICTURE_TEXTURES, PIC_SPRITES = GT_PICTURE_SPRITES };
static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

/* Only the four known picture members can use this private data format.
 * Bounds are checked before the game sees any stored offsets or lengths. */
static int validMember(const uint8_t* data, int size, int kind) {
    if (!data || size < 8 || size > (int)GT_PICTURE_MEMBER_MAX) return 0;
    if (kind == PIC_SPRITES) {
        int length;
        char* text = gtSpriteLayoutTemplate(data, size, &length);
        int ok = text != NULL;
        free(text);
        return ok;
    }
    uint32_t n = rd32(data);
    if (!n || n > 256 || 4 + 4 * n > (uint32_t)size) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t at = rd32(data + 4 + 4 * i);
        if (at < 4 + 4 * n || at > (uint32_t)size - 8) return 0;
        if (kind == PIC_TIMS) {
            uint32_t flags = rd32(data + at + 4), q = at + 8;
            if (rd32(data + at) != 0x10 || (flags & ~11u)) return 0;
            for (int block = 0; block < ((flags & 8) ? 2 : 1); block++) {
                if (q > (uint32_t)size - 12) return 0;
                uint32_t bytes = rd32(data + q);
                unsigned w = rd16(data + q + 8), h = rd16(data + q + 10);
                if (bytes < 12 || bytes > (uint32_t)size - q || !w || !h ||
                    w > 1024 || h > 512 || (uint64_t)w * h * 2 > bytes - 12) return 0;
                q += bytes;
            }
        } else if (kind == PIC_TEXTURES) {
            if (at > (uint32_t)size - 16 || (rd32(data + at) != 0x1100 && rd32(data + at) != 0x1101)) return 0;
            unsigned w = rd16(data + at + 12), h = rd16(data + at + 14);
            if (!w || !h || w > 1024 || h > 512 || (uint64_t)w * h * 2 > (uint32_t)size - at - 16) return 0;
        } else return 0;
    }
    return 1;
}

static void wr32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void wr64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint64_t rd64(const uint8_t* p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

uint8_t* gtPictureSnapshot(const uint8_t* base, int baseSize, const uint8_t* data, int size,
                           int kind, int* outSize) {
    *outSize = 0;
    if (!validMember(base, baseSize, kind) || !validMember(data, size, kind)) return NULL;
    uint8_t* out = malloc((size_t)size + 32);
    if (!out) return NULL;
    memcpy(out, "XGPMEM1\0", 8);
    wr32(out + 8, (uint32_t)kind);
    wr32(out + 12, (uint32_t)size);
    wr64(out + 16, gtSpriteFingerprint(base, baseSize));
    wr64(out + 24, gtSpriteFingerprint(data, size));
    memcpy(out + 32, data, (size_t)size);
    *outSize = size + 32;
    return out;
}

uint8_t* gtPictureRestore(const uint8_t* base, int baseSize, const uint8_t* file, int size,
                          int kind, int* outSize) {
    *outSize = 0;
    if (!file || size < 40 || size > (int)GT_PICTURE_MEMBER_MAX + 32 ||
        memcmp(file, "XGPMEM1\0", 8) || rd32(file + 8) != (uint32_t)kind ||
        rd32(file + 12) != (uint32_t)size - 32 || !validMember(base, baseSize, kind) ||
        rd64(file + 16) != gtSpriteFingerprint(base, baseSize) ||
        rd64(file + 24) != gtSpriteFingerprint(file + 32, size - 32) ||
        !validMember(file + 32, size - 32, kind)) return NULL;
    if (kind == PIC_SPRITES && rd32(base) != rd32(file + 32)) return NULL;
    uint8_t* out = malloc((size_t)size - 32);
    if (out) { memcpy(out, file + 32, (size_t)size - 32); *outSize = size - 32; }
    return out;
}

