#ifndef PORT_EXTRAS_SPRITES_CATALOGUE_H
#define PORT_EXTRAS_SPRITES_CATALOGUE_H

#include <cstdint>
#include <vector>

namespace extras {
// Exact bundle bytes plus every frame's sampled texture indices, normalized
// for the field's VRAM slot. Empty means contextual/invalid: keep it separate.
// Compare the returned bytes themselves, not only a hash.
std::vector<uint8_t> npcIdentity(const uint8_t* bundle, int size, const uint16_t* vram, int slotX, int slotY);

// What the field gives an NPC for its sheet (xg-htn.35.68), from its archive
// alone (initFieldData: section 4's sheet i to VRAM slot i, unless the slot
// takes the texture file's), so the library can say so instead of leaving a
// blank or odd preview unexplained.
struct NpcSheet {
    enum Kind {
        OWN,         // section 4 has its sheet
        TEXTURE,     // the slot is the texture file's (04/B9+2n)
        NONE,        // neither: the slot shows whatever VRAM holds there
        PLACEHOLDER, // its section-4 sheet is the retail "No Image Printing" one
    } kind = NONE;
    struct Rect { int x, y, w, h; };
    std::vector<Rect> pages; // where its section-4 pages go (OWN, PLACEHOLDER)
};
// sheets: the field's section 4, decompressed; slot: the archive's VRAM slot.
NpcSheet npcSheet(const uint8_t* sheets, int size, int npc, int slotX, int slotY, bool fromTextureFile);
// Whether a section-4 page's pixels differ between two VRAM pictures (the
// field's without and with its texture file): the field loads over the sheet.
bool npcSheetReplaced(const NpcSheet& sheet, const uint16_t* before, const uint16_t* after);
// Every part of every frame samples texel index 0 (the field's palettes'
// transparent entry) in this VRAM: the NPC draws nothing there. False for
// sprites with embedded pieces or data it cannot read.
bool npcBlank(const uint8_t* bundle, int size, const uint16_t* vram, int slotX, int slotY);
}

#endif
