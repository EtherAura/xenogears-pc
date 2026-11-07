#include "field_assets.h"
#include <string.h>

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

const char* xgFieldSectionName(int k) {
    static const char* const names[XG_FIELD_SECTIONS] = {
        "TIM images", "Walkmesh", "Models", "NPC sprite bundles", "NPC sprite sheets",
        "Scripts", "Encounters", "Dialogue", "Triggers"
    };
    return k >= 0 && k < XG_FIELD_SECTIONS ? names[k] : "?";
}

int xgFieldArchiveParse(const uint8_t* d, int size, XgFieldArchive* out) {
    XgFieldArchive a = {0};
    if (d == NULL || out == NULL || size < 0x190) return 0;
    uint32_t entitiesEnd = 0x190u + 16u * (d[0x18C] | d[0x18D] << 8);
    if (entitiesEnd > (uint32_t)size) return 0;
    for (int k = 0; k < XG_FIELD_SECTIONS; k++) {
        uint32_t n = rd32(d + 0x10C + 4 * k), at = rd32(d + 0x130 + 4 * k);
        if (n == 0 && at == 0) continue;
        if (n > (16u << 20) || at < entitiesEnd || at > (uint32_t)size - 4) return 0;
        /* The stream includes its final group's padding (0..7 bytes on
         * retail discs). The matched loader allocates the declared size
         * plus 0x10; retain the stream's full output within that bound. */
        uint32_t decoded = rd32(d + at);
        if (decoded < n || decoded > n + 0x10u) return 0;
        uint32_t end = (uint32_t)size;
        for (int j = 0; j < XG_FIELD_SECTIONS; j++) {
            uint32_t other = rd32(d + 0x130 + 4 * j);
            if (j != k && other == at) return 0;
            if (other > at && other < end) end = other;
        }
        if (end - at < 4) return 0;
        a.section[k] = (XgFieldSection){(int)at, (int)(end - at), (int)decoded};
    }
    *out = a; /* a failed parse leaves the caller's previous result alone */
    return 1;
}
