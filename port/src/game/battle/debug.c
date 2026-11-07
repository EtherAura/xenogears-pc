/*
 * The debug battle selector's text on the PC.
 *
 * The selector (battle_debug_selector) prints with the debug font. On the
 * development kit battleEntryPoint gave the battle its own font in the kit's
 * extra RAM (setDebugFontBuffer(0x80200000)); a retail console has no such
 * RAM, so the selector printed through the kernel menu's font, a heap block
 * the battle overlay has overwritten by then (Redux crashes in
 * printDebugTextOnScreen). The PC's bootGame drops the old mode's font, and the
 * selector gets the kit's font here, in memory of its own as on the kit; the
 * selector frees it (deinitDebugFont) before the battle loads.
 */
#include "common.h"
#include "main/debug_font.h"

static u64 selectorFont[0x18000 / 8]; /* DebugFont + 2 x 1000 SPRT_8 */

int portSweepEnabled(void);

void pcBattleSelectorFont(void) {
    if (portSweepEnabled()) {
        /* Content sweeps (XG_SWEEP_LOG) compare with PCSX-Redux, where the
         * selector has no font (bcap.py clears it, as retail would need):
         * a blank selector, so the battle intro's shards, cut from the
         * screen, match. */
        return;
    }
    setDebugFontBuffer((sptr)selectorFont);
    /* battleEntryPoint's development-kit font */
    initDebugFont(0x10, 0x10, 0x140, 0x100, 0x3E8, 0, 0x340, 0, 0x340, 0x20, 0);
}
