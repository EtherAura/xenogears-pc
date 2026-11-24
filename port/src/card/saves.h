#ifndef PORT_CARD_SAVES_H
#define PORT_CARD_SAVES_H

/*
 * Xenogears saves on the two memory cards (port/src/card: the images the game
 * uses, cards[]), read by the port itself, not the game's memory card code:
 * the bestiary's "catch up from a save" (extras/bestiary_record.c) and the
 * battle arena's save browser (ui/arena_saves.cpp, card/party.h).
 *
 * A save is one 8 KB block (include/menu/menu.h): the title frame and icon,
 * then from +0x100 the menu's MenuSaveGame, uncompressed:
 *   +0x100   MenuSaveSummary: play time in vblanks, the party's HP, EP and
 *            levels, the names of gameState.m0_names[0..10] as character
 *            codes (u16 each, the kernel text's code table, 01/07 member 27;
 *            buildSaveSummary encodes them, decodeCharacterNames decodes)
 *   +0x390   the 11 character records (gameState.m26C_party)
 *   +0xFC4   the 11 ability rows (gameState.m16C0_abilities)
 *   +0x1224  gameState from 0x1920 on: field variables (0 the story's
 *            progress, 82 the disc), the members who joined, inventories,
 *            the ammo-count table
 *   +0x1F00  the port's game id (pc_gameid.h), in the block's free tail
 * Byte 0x1FFF is the sum of bytes 0x100-0x1FFE (the game's own check,
 * src/menu/m_801C8D78.c). The directory names a save BASLUS-00664... (US)
 * or BISLPS-01160... (JP); both regions keep this one layout.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CARD_SAVE_MEMBERS 11 /* character ids 0-10 */

typedef struct {
    int level;           /* m62_Level */
    int hp, maxHp;       /* m4C_HP, m4E_MaxHP */
    int ep, maxEp;       /* m50_MP, m52_MaxMP */
} CardSaveMember;

typedef struct {
    int card;            /* 0 or 1: the slot */
    int block;           /* the save's first block, 1-15 */
    int region;          /* 'U' BASLUS-00664..., 'J' BISLPS-01160... */
    int number;          /* the game's save number ("No.12" in the title), 0 if the title has none */
    char title[80];      /* the directory's title as ASCII (card.h CardSave) */
    char location[80];   /* the title after "XENOGEARS/No.nn": the save screen's location line, as ASCII */
    int progress;        /* the story's progress: field variable 0 */
    int disc;            /* field variable 82: 1 or 2 */
    uint32_t playTime;   /* vblanks (60 a second) */
    uint32_t id;         /* the port's game id, 0 if saved without one */
    int joined;          /* bit c: member c has joined (gameState.m1D30, ids 0-10) */
    int party[3];        /* the save's current party, 255 for none */
    CardSaveMember members[CARD_SAVE_MEMBERS];
} CardGameSave;

/* This build's region: 'U' or 'J'. */
int cardGameRegion(void);

/* A save block's summary (card, block, region, number and titles left as
 * they are). 0 if the checksum fails: not a complete save. */
int cardGameSaveRead(const uint8_t* block, CardGameSave* out);

/* The complete saves on both cards, card 1's then card 2's, each in block
 * order: region 0 for both regions, else only 'U' or 'J' ones. Returns the
 * count (at most max). */
int cardGameSaves(CardGameSave* out, int max, int region);

/* The 8 KB block of a listed save in its card's image, NULL if that block
 * no longer holds a complete save of that name: valid until the card
 * changes (a copy keeps it). */
const uint8_t* cardGameSaveBlock(const CardGameSave* save);

/* Offsets in a save block (the header's table). */
#define CARD_SAVE_DATA 0x100                          /* MenuSaveGame */
#define CARD_SAVE_NAMES (CARD_SAVE_DATA + 0x24)       /* the names' character codes, 0x14 bytes each */
#define CARD_SAVE_PARTY (CARD_SAVE_DATA + 0x290)      /* character records, 0xA4 each */
#define CARD_SAVE_ABILITIES (CARD_SAVE_DATA + 0xEC4)  /* ability rows, 0x20 each */
#define CARD_SAVE_STATE (CARD_SAVE_DATA + 0x1124)     /* gameState + 0x1920 */
#define CARD_SAVE_STATE_FROM 0x1920
#define CARD_SAVE_STATE_BYTES 0xA38
/* gameState offset off (0x1920-0x2357) in a save block. */
#define CARD_SAVE_AT(off) (CARD_SAVE_STATE + (off) - CARD_SAVE_STATE_FROM)

#ifdef __cplusplus
}
#endif
#endif
