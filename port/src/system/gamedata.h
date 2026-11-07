#ifndef PORT_SYSTEM_GAMEDATA_H
#define PORT_SYSTEM_GAMEDATA_H

/*
 * The game modules' initialised data, read from the player's disc at every
 * launch (gamedata.c). The executable holds the data's layout only: zero
 * storage in each module's data section and, per module, the copies that fill
 * it from the module's file on the disc (tools/pc_link.py writes them).
 */

#include <stddef.h>
#include <stdint.h>

/* Read every module's file from the image (.cue or raw .bin), check that it is
 * this build's release (region, file sizes, a hash of the bytes used) and fill
 * the modules' data. Nothing is written unless every module checks out. 1 on
 * success; otherwise 0 and *why says what is wrong. Before portModulesInit:
 * the overlay resets, the soft reset and the extras' sandbox keep what this
 * leaves. */
int portDataLoad(const char* image, char* why, size_t size);

/* The same checks without filling anything: 1 if the image holds this build's
 * data. */
int portDataCheck(const char* image, char* why, size_t size);

/* "US" or "JP": the release whose data this build lays out. */
const char* portDataRegionName(void);

/* Original boot-header heap boundaries, read from the player's disc. Zero
 * means absent/unsupported; these are independent of native overlay layouts. */
uint32_t portRetailHeapStart(int mode);

#endif /* PORT_SYSTEM_GAMEDATA_H */
