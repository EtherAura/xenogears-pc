/* The running release's character-code table (party.h xgPartyGameCodes),
 * from its disc image through the text system's read-only reader
 * (system/gametext_disc.c): the kernel text, 01/07, LZSS, an offset bundle
 * (u32 n, n u32 offsets) whose member at printDialogTextVar[27] (the 28th
 * offset: setupPrintDialogTextVar skips the count) is the table the game's
 * decodeCharacterCodes reads. Not in the unit tests: they bring their own. */

#include <stdlib.h>
#include <string.h>

#include "party.h"
#include "saves.h"
#include "system/gametext.h"

#define KERNEL_DIR 0x01
#define KERNEL_TEXT 0x07
#define CODE_BANK 27

static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int xgPartyGameCodes(uint8_t codes[XG_PARTY_CODE_TABLE]) {
    int disc = gtCurrentDisc();
    const char* region = cardGameRegion() == 'J' ? "JP" : "US";
    for (int i = 0; i < 2; i++, disc = 3 - disc) {
        GtImage* im = gtRetailImage(disc);
        if (im == NULL || strcmp(gtImageRegion(im), region) != 0) continue;
        int size = 0, bytes = 0;
        uint8_t* file = gtImageFile(im, KERNEL_DIR, KERNEL_TEXT, &size);
        uint8_t* text = file != NULL ? gtLzss(file, size, &bytes) : NULL;
        free(file);
        int ok = 0;
        if (text != NULL && bytes >= 8) {
            uint32_t n = rd32(text);
            if (n > CODE_BANK && 4 + 4 * (CODE_BANK + 1) <= (uint32_t)bytes) {
                uint32_t at = rd32(text + 4 + 4 * CODE_BANK);
                if (at <= (uint32_t)bytes && (uint32_t)bytes - at >= XG_PARTY_CODE_TABLE) {
                    memcpy(codes, text + at, XG_PARTY_CODE_TABLE);
                    ok = 1;
                }
            }
        }
        free(text);
        if (ok) return 1;
    }
    return 0;
}
