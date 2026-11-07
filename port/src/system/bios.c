/*
 * BIOS services beyond the kernel's events, and the SN debugger's PC file
 * calls. The game reaches most BIOS functions through its own small stubs
 * (asm/us/main/30C54.s, 3EF74.s), so they keep those stubs' names here.
 *
 * The memory card services (LIBCARD, _bu_init, _card_info and the BIOS file
 * calls with their "bu" device) are in port/src/card.
 */

#include <stdio.h>

#include "common.h"
#include "system.h"

/* ---- other BIOS calls ---- */

/* B(51h) Krom2RawAdd and the ROM's kanji font: krom.c. */

/* GetGp: battling saves $gp in its thread contexts; the PC has no $gp. */
u32 func_800405E4(void) {
    hwSafePoint();
    return 0;
}

/* ---- LIBSN: the SN debugger's host files ----
 *
 * There is no debugger, so they fail, except inside an extras sandbox that
 * serves them from a disc image (biosSetHostFiles; port/src/extras/hostfiles.c):
 * there the game's file system runs its own host-file path (cdromFnd), whose
 * reads finish at once and never touch the drive. */

static const PcHostFiles* hostFiles;

void biosSetHostFiles(const PcHostFiles* files) {
    hostFiles = files;
}

/* PCinit */
s32 PCinit(void) {
    hwSafePoint();
    return 0;
}

s32 PCopen(char* name, s32 flags, s32 perms) {
    hwSafePoint();
    if (hostFiles != NULL) {
        return hostFiles->open(hostFiles->user, name);
    }
    return -1;
}

s32 PCcreat(char* name, s32 perms) {
    hwSafePoint();
    return -1;
}

s32 PClseek(s32 fd, s32 offset, s32 mode) {
    hwSafePoint();
    if (hostFiles != NULL) {
        return hostFiles->seek(hostFiles->user, fd, offset, mode);
    }
    return -1;
}

s32 PCread(s32 fd, char* buf, u32 len) {
    hwSafePoint();
    if (hostFiles != NULL) {
        return hostFiles->read(hostFiles->user, fd, buf, len);
    }
    return -1;
}

s32 PCwrite(s32 fd, char* buf, u32 len) {
    hwSafePoint();
    return -1;
}

s32 PCclose(s32 fd) {
    hwSafePoint();
    if (hostFiles != NULL) {
        return hostFiles->close(hostFiles->user, fd);
    }
    return -1;
}
