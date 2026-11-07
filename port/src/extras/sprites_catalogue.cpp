#include "sprites_catalogue.h"
#include <algorithm>
#include <set>

namespace extras {
namespace {
unsigned u16(const uint8_t* p) { return p[0] | unsigned(p[1]) << 8; }
unsigned u32(const uint8_t* p) { return u16(p) | u16(p + 2) << 16; }
}

std::vector<uint8_t> npcIdentity(const uint8_t* b, int size, const uint16_t* vram, int slotX, int slotY) {
    if (!b || size < 32) return {};
    unsigned frames = u32(b + 8), palettes = u32(b + 12);
    if (frames > unsigned(size - 2) || palettes > unsigned(size - 4) || !u16(b + palettes)) return {};
    unsigned count = u16(b + frames) & 511;
    if (!count || count + 1 > (unsigned(size) - frames) / 2) return {};
    std::vector<uint8_t> key;
    for (int i = 0; i < 4; ++i) key.push_back(uint8_t(unsigned(size) >> (8 * i)));
    key.insert(key.end(), b, b + size); // includes all animations, palettes and embedded pieces
    if (!(u16(b + frames) & 0x8000)) return key;
    if (!vram) return {};
    std::set<unsigned> tiles;
    for (unsigned f = 1; f <= count; ++f) {
        unsigned at = frames + u16(b + frames + 2 * f);
        if (at > unsigned(size - 4)) return {};
        unsigned parts = b[at] & 63;
        if (parts > (unsigned(size) - at - 4) / 2) return {};
        for (unsigned p = 0; p < parts; ++p) {
            unsigned tile = frames + u16(b + at + 4 + 2 * p);
            if (tile > unsigned(size - 5) || (b[tile] & 16)) return {}; // global page/CLUT depends on scene
            tiles.insert(tile);
        }
    }
    for (unsigned tile : tiles) {
        const uint8_t* t = b + tile;
        int depth = t[0] & 1, shift = depth ? 1 : 2, bits = depth ? 8 : 4;
        int pageX = ((slotX + ((t[0] >> 1) & 7) * 64) & 1023) & ~63;
        int pageY = slotY & 256;
        int u = uint8_t(((slotX & 63) >> shift) + t[1]), v = uint8_t((slotY & 255) + t[2]);
        // renderSpriteActor subtracts one U texel for a mirrored part,
        // clamping at zero. Vertex endpoints wrap to eight bits before
        // interpolation, so include the interval between those endpoints.
        int mirror = std::max(0, u - 1);
        int end = uint8_t(u + t[3] - 1), mirrorEnd = uint8_t(mirror + t[3] - 1 - (u == 0));
        int x0 = std::min({u, end, mirror, mirrorEnd}), x1 = std::max({u, end, mirror, mirrorEnd});
        int endY = uint8_t(v + t[4] - 1), y0 = std::min(v, endY), y1 = std::max(v, endY);
        for (int n : {x1 - x0 + 1, y1 - y0 + 1}) {
            key.push_back(uint8_t(n)); key.push_back(uint8_t(n >> 8));
        }
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
            uint16_t word = vram[((pageY + y) & 511) * 1024 + ((pageX + (x >> shift)) & 1023)];
            key.push_back(uint8_t((word >> ((x & ((1 << shift) - 1)) * bits)) & ((1 << bits) - 1)));
        }
    }
    return key;
}

namespace {
// FNV-1a 64 of the retail "No Image Printing" sheet (its 64x77 header and
// words): field 66 NPC 0 of disc 1, the same in US and JP, and 119 more NPCs
// of each (work/issue-viewer-browsing/npc-sheet-metadata-USA.json).
constexpr uint64_t PLACEHOLDER_SHEET = 0xd79467e8c4f299d7ull;
uint64_t fnv(const uint8_t* p, size_t n) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
}

NpcSheet npcSheet(const uint8_t* s, int size, int npc, int slotX, int slotY, bool fromTextureFile) {
    NpcSheet out;
    if (fromTextureFile) {
        out.kind = NpcSheet::TEXTURE;
        return out;
    }
    if (!s || size < 8 || npc < 0 || unsigned(npc) >= u32(s) || 8 + 4 * unsigned(npc) > unsigned(size)) return out;
    unsigned at = u32(s + 4 + 4 * npc);
    if (at == 0 || at > unsigned(size) - 4) return out;
    unsigned count = u32(s + at);
    if (count == 0 || count > 16 || at + 4 + 4 * count > unsigned(size)) return out;
    bool placeholder = false;
    for (unsigned k = 0; k < count; ++k) {
        unsigned page = at + u32(s + at + 4 + 4 * k);
        if (page > unsigned(size) - 4) return NpcSheet();
        unsigned w = u16(s + page), h = u16(s + page + 2);
        if (size_t(w) * h * 2 > size_t(unsigned(size) - page - 4)) return NpcSheet();
        placeholder |= fnv(s + page, 4 + size_t(w) * h * 2) == PLACEHOLDER_SHEET;
        out.pages.push_back({slotX + 64 * int(k), slotY, int(w), int(h)}); // uploadNpcSpriteSheet
    }
    out.kind = placeholder ? NpcSheet::PLACEHOLDER : NpcSheet::OWN;
    return out;
}

bool npcSheetReplaced(const NpcSheet& sheet, const uint16_t* before, const uint16_t* after) {
    if (!before || !after) return false;
    for (const auto& r : sheet.pages)
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x) {
                size_t at = size_t(y & 511) * 1024 + (x & 1023);
                if (before[at] != after[at]) return true;
            }
    return false;
}

bool npcBlank(const uint8_t* b, int size, const uint16_t* vram, int slotX, int slotY) {
    // The frames and parts as npcIdentity reads them.
    if (!b || !vram || size < 32) return false;
    unsigned frames = u32(b + 8);
    if (frames > unsigned(size - 2) || !(u16(b + frames) & 0x8000)) return false;
    unsigned count = u16(b + frames) & 511;
    if (!count || count + 1 > (unsigned(size) - frames) / 2) return false;
    bool any = false;
    for (unsigned f = 1; f <= count; ++f) {
        unsigned at = frames + u16(b + frames + 2 * f);
        if (at > unsigned(size - 4)) return false;
        unsigned parts = b[at] & 63;
        if (parts > (unsigned(size) - at - 4) / 2) return false;
        for (unsigned p = 0; p < parts; ++p) {
            unsigned tile = frames + u16(b + at + 4 + 2 * p);
            if (tile > unsigned(size - 5) || (b[tile] & 16)) return false;
            const uint8_t* t = b + tile;
            int depth = t[0] & 1, shift = depth ? 1 : 2, bits = depth ? 8 : 4;
            int pageX = ((slotX + ((t[0] >> 1) & 7) * 64) & 1023) & ~63, pageY = slotY & 256;
            int u = uint8_t(((slotX & 63) >> shift) + t[1]), v = uint8_t((slotY & 255) + t[2]);
            for (int y = 0; y < t[4]; ++y)
                for (int x = 0; x < t[3]; ++x) {
                    int tx = uint8_t(u + x), ty = uint8_t(v + y);
                    uint16_t word = vram[((pageY + ty) & 511) * 1024 + ((pageX + (tx >> shift)) & 1023)];
                    if ((word >> ((tx & ((1 << shift) - 1)) * bits)) & ((1 << bits) - 1)) return false;
                    any = true;
                }
        }
    }
    return any;
}
}
