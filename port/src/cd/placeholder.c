/*
 * Fields a disc does not hold. Disc 2 keeps 525 of the 730 field archives
 * only as 24-byte CDMAKE placeholders, and the story never enters those
 * fields on disc 2: the worldmap events 9, 13, 14 and 16 and field 342's
 * exit to 360 lead to such fields only from disc-1-only fields. A test start
 * that goes there anyway makes the PS1 read the field header past the
 * 24-byte block and stop with an address error in decompress (PCSX-Redux:
 * LoadAddressError at 0x80032EB4). The PC would read past the block too and
 * crash wherever the garbage offsets lead (often in another thread), so
 * initFieldData (src/field/f_8006FDEC.c) stops here with the reason.
 */

#include <stdio.h>

#include "cd.h"
#include "system/system.h"

void cdFieldNotOnDisc(int field) {
    fprintf(stderr, "cd: field %d is only a placeholder on this disc (disc %d): the PS1 stops here too\n", field,
            cdBootDiscNumber());
    quitNow();
}
