/*
 * The card's directory (block 0, frames 1..15: one frame per data block),
 * as psx-spx "Memory Card Data Format" describes it:
 *
 *   00h  allocation state (51h first block, 52h middle, 53h last; A0h free,
 *        A1h..A3h deleted first/middle/last)
 *   04h  file size in bytes (first block only)
 *   08h  next block of the file minus 1, FFFFh for the last
 *   0Ah  file name, 20 characters and a 00h (first block only)
 *   7Fh  checksum
 *
 * Deleting a file only changes the states (the name stays, so the BIOS can
 * undelete it). Used by the BIOS device (bu.c) and the command line (mcs.c).
 */

#include <string.h>

#include "card.h"

uint8_t* cardDirFrame(uint8_t* data, int block) {
    return data + block * CARD_FRAME;
}

static int stateOf(const uint8_t* data, int block) {
    return data[block * CARD_FRAME];
}

static int isFree(int state) {
    return (state & 0xF0) == 0xA0;
}

/* The BIOS's wildcards: '?' takes any one character, '*' the rest of the
 * name. An empty pattern lists every file ("bu00:" as this game uses it). */
int cardNameMatch(const char* pattern, const char* name) {
    if (pattern[0] == '\0') {
        return 1;
    }
    for (;; pattern++, name++) {
        if (*pattern == '*') {
            return 1;
        }
        if (*pattern == '?') {
            if (*name == '\0') {
                return 1; /* the BIOS stops comparing when '?' meets the end of the name */
            }
            continue;
        }
        if (*pattern != *name) {
            return 0;
        }
        if (*pattern == '\0') {
            return 1;
        }
    }
}

static void frameName(const uint8_t* data, int block, char* out) {
    memcpy(out, data + block * CARD_FRAME + 0x0A, CARD_NAME_MAX);
    out[CARD_NAME_MAX] = '\0';
}

int cardFindFile(const uint8_t* data, const char* name) {
    char n[CARD_NAME_MAX + 1];
    for (int b = 1; b < CARD_BLOCKS; b++) {
        if (stateOf(data, b) != DIR_FIRST) {
            continue;
        }
        frameName(data, b, n);
        if (cardNameMatch(name, n)) {
            return b;
        }
    }
    return 0;
}

int cardFileBlocks(const uint8_t* data, int first, int* blocks, int max) {
    int n = 0;
    int b = first;
    while (b >= 1 && b < CARD_BLOCKS && n < max) {
        blocks[n++] = b;
        const uint8_t* f = data + b * CARD_FRAME;
        int next = f[8] | f[9] << 8;
        if (next == 0xFFFF) {
            return n;
        }
        b = next + 1;
    }
    return -1; /* a broken chain */
}

int cardFreeBlocks(const uint8_t* data) {
    int n = 0;
    for (int b = 1; b < CARD_BLOCKS; b++) {
        n += isFree(stateOf(data, b));
    }
    return n;
}

/* The lowest free blocks, in order, linked into one file. */
int cardCreateFile(uint8_t* data, const char* name, int nblocks, int* changedFrames) {
    int blocks[CARD_BLOCKS];
    int n = 0;
    for (int b = 1; b < CARD_BLOCKS && n < nblocks; b++) {
        if (isFree(stateOf(data, b))) {
            blocks[n++] = b;
        }
    }
    if (nblocks < 1 || n < nblocks) {
        return 0;
    }
    for (int i = 0; i < n; i++) {
        uint8_t* f = data + blocks[i] * CARD_FRAME;
        memset(f, 0, CARD_FRAME);
        f[0] = i == 0 ? DIR_FIRST : i == n - 1 ? DIR_LAST : DIR_MIDDLE;
        if (i == 0) {
            uint32_t size = (uint32_t)nblocks * CARD_BLOCK;
            f[4] = size & 0xFF;
            f[5] = (size >> 8) & 0xFF;
            f[6] = (size >> 16) & 0xFF;
            strncpy((char*)f + 0x0A, name, CARD_NAME_MAX);
        }
        int next = i == n - 1 ? 0xFFFF : blocks[i + 1] - 1;
        f[8] = next & 0xFF;
        f[9] = next >> 8;
        cardSetFrameChecksum(f);
        if (changedFrames != NULL) {
            changedFrames[i] = blocks[i];
        }
    }
    return blocks[0];
}

int cardDeleteFile(uint8_t* data, int first, int* changedFrames) {
    int blocks[CARD_BLOCKS];
    int n = cardFileBlocks(data, first, blocks, CARD_BLOCKS);
    if (n < 0) {
        n = 1; /* a broken chain: at least the first block goes */
        blocks[0] = first;
    }
    for (int i = 0; i < n; i++) {
        uint8_t* f = data + blocks[i] * CARD_FRAME;
        if (f[0] >= DIR_FIRST && f[0] <= DIR_LAST) {
            f[0] += DIR_DELETED_FIRST - DIR_FIRST;
        }
        cardSetFrameChecksum(f);
        if (changedFrames != NULL) {
            changedFrames[i] = blocks[i];
        }
    }
    return n;
}
