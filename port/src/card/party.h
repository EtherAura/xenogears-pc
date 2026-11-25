#ifndef PORT_CARD_PARTY_H
#define PORT_CARD_PARTY_H

/*
 * A party record (xg-htn.35.143, party vs party): one side's party for a
 * versus battle on foot, packed from a memory-card save (saves.h) and 1-3
 * members chosen from those who joined. Each player packs their own from a
 * save on their own machine; online, the record is what one machine sends
 * the other, so it is fixed in layout and free of pointers: the PS1's own
 * records, byte for byte (little-endian, as every target is), and the same
 * bytes on Linux and Windows (only byte arrays: no padding).
 *
 * The same check (xgPartyCheck) guards a record packed here and one received
 * from a peer: every value the battle indexes or divides with is in range
 * (work/agents/BD/PVP/STEP2_DESIGN.md section 3). Edited saves are accepted
 * as long as they pass it: the check keeps memory safe, it cannot tell an
 * edited save from a real one.
 *
 * What the battle on foot reads of a record (party.c's list): the checks
 * below bound every field it uses as a table index, pointer offset, loop
 * bound or divisor, or that the apply step does not force (HP, EP, the Gear
 * number, the command loadout); the rest it reads only as bits, in switches,
 * compares and sums, or not at all.
 *
 * Format 1 (this layout) has no Gears. The Gears step makes format 2: these
 * bytes, then each member's Gear record (0xA4) and Gear name (0x14), about
 * 552 bytes more, XG_PARTY_GEARS in m2_flags and m8_size the whole record's.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XG_PARTY_FORMAT 1
#define XG_PARTY_MEMBERS 3
#define XG_PARTY_NONE 0xFF        /* m4_members past m3_count */
#define XG_PARTY_RECORD 0xA4      /* GameStateCharacter */
#define XG_PARTY_ABILITY 0x20     /* GameStateSize20 */
#define XG_PARTY_NAME 0x14        /* gameState.m0_names[c] */
#define XG_PARTY_ITEMS 0x30       /* the battle's item and ammo lists */
#define XG_PARTY_AMMO_TABLE 0x60  /* gameState.m22B8_ammoCounts */
#define XG_PARTY_GEARS 0x01       /* m2_flags: the Gears step's format 2 (never set in format 1) */
/* The kernel text's character-code table (01/07, the bundle's member at
 * printDialogTextVar[27]): code c is the glyph (a, b) at bytes 2c and 2c+1,
 * one byte b when a is 0, two bytes a b otherwise (decodeCharacterCodes). */
#define XG_PARTY_CODES 0x144
#define XG_PARTY_CODE_TABLE (2 * XG_PARTY_CODES)

typedef struct {
    /* 0x000 */ uint8_t m0_format;                  /* XG_PARTY_FORMAT */
    /* 0x001 */ uint8_t m1_region;                  /* 'U' or 'J': the game it was packed for */
    /* 0x002 */ uint8_t m2_flags;                   /* 0 in format 1 */
    /* 0x003 */ uint8_t m3_count;                   /* members, 1-3 */
    /* 0x004 */ uint8_t m4_members[XG_PARTY_MEMBERS]; /* character ids 0-10 in order (the first leads),
                                                     * XG_PARTY_NONE past m3_count */
    /* 0x007 */ uint8_t m7_zero;
    /* 0x008 */ uint8_t m8_size[2];                 /* the record's bytes (little-endian): XG_PARTY_BYTES */
    /* 0x00A */ uint8_t mA_zero[6];
    /* 0x010 */ uint8_t m10_characters[XG_PARTY_MEMBERS][XG_PARTY_RECORD]; /* gameState.m26C_party[id] as saved */
    /* 0x1FC */ uint8_t m1FC_abilities[XG_PARTY_MEMBERS][XG_PARTY_ABILITY]; /* gameState.m16C0_abilities[id] */
    /* 0x25C */ uint8_t m25C_names[XG_PARTY_MEMBERS][XG_PARTY_NAME]; /* gameState.m0_names[id]: the game's
                                                     * text bytes, 0-terminated, zeros after */
    /* 0x298 */ uint8_t m298_itemIds[XG_PARTY_ITEMS];    /* battleEffectBF.m42_ids: inventory 0's items 1-0x30 */
    /* 0x2C8 */ uint8_t m2C8_itemCounts[XG_PARTY_ITEMS]; /* their counts, 1-99 */
    /* 0x2F8 */ uint8_t m2F8_ammoIds[XG_PARTY_ITEMS];    /* battleAmmoItemIds: inventory 1's ammo 0x32-0x48 */
    /* 0x328 */ uint8_t m328_ammoCounts[XG_PARTY_ITEMS]; /* their counts, 1-99 */
    /* 0x358 */ uint8_t m358_ammoTable[XG_PARTY_AMMO_TABLE]; /* gameState.m22B8_ammoCounts, each 0-100 */
} XgPartyRecord;
#define XG_PARTY_BYTES 0x3B8

#ifdef __cplusplus
static_assert(sizeof(XgPartyRecord) == XG_PARTY_BYTES, "the party record's layout");
#else
_Static_assert(sizeof(XgPartyRecord) == XG_PARTY_BYTES, "the party record's layout");
#endif

/* Why a pack or a check refused (xgPartyPack, xgPartyCheck return one). */
enum {
    XG_PARTY_OK = 0,
    XG_PARTY_BAD_SAVE,       /* pack: not a complete save (its checksum) */
    XG_PARTY_NO_CODES,       /* no character-code table (the disc could not be read) */
    XG_PARTY_NOT_JOINED,     /* pack: a member who has not joined in that save */
    XG_PARTY_BAD_HEADER,     /* format, size, flags or a reserved byte */
    XG_PARTY_BAD_REGION,     /* packed for the other region's game */
    XG_PARTY_BAD_MEMBERS,    /* 1-3 ids 0-10, no repeats, no gaps; nothing in an unused slot */
    XG_PARTY_BAD_LOADOUT,    /* the command loadout (m56) is not the member's id */
    XG_PARTY_BAD_GEAR,       /* the Gear number (mA0) is not below 20 or 0xFF */
    XG_PARTY_BAD_LOADED_AMMO, /* Billy's loaded rounds (m6F_equipment[0], [3]) not 0x32-0x49 */
    XG_PARTY_BAD_HP,         /* HP above max HP, max HP 0 or above 999 */
    XG_PARTY_BAD_EP,         /* EP above max EP, max EP above 99 */
    XG_PARTY_BAD_LEVEL,      /* a level (m62, m63) not 1-99 */
    XG_PARTY_BAD_AP,         /* AP per turn (m17) not 1-7 */
    XG_PARTY_BAD_NAME,       /* not glyphs of the code table, a code below 0x10 (the 0x0F escape), no
                              * terminator, or something after it */
    XG_PARTY_BAD_ITEMS,      /* an item id not 1-0x30, a count not 1-99, a repeat or a gap */
    XG_PARTY_BAD_AMMO,       /* an ammo id not 0x32-0x48, a count not 1-99, a repeat or a gap */
    XG_PARTY_BAD_AMMO_TABLE, /* an ammo-count table entry above 100 */
    XG_PARTY_BAD_EXP,        /* experience to the next level (m44, m48) not 1 or more */
    XG_PARTY_REASONS
};

/* What was refused: the reason (XG_PARTY_*), the member's place (0-2) or -1,
 * and an English line for logs. */
typedef struct {
    int reason;
    int member;
    char text[160];
} XgPartyWhy;

/* Packs the members (1-3 character ids in order, 255 after the last) of a
 * save block (8 KB, CARD_BLOCK) into *out and checks it (xgPartyCheck).
 * codes: the code table (XG_PARTY_CODE_TABLE bytes) the names are decoded
 * with (xgPartyGameCodes). The items and ammo come as the battle would take
 * them from the save's inventories (bld_801E4048.c initBattleInventory):
 * items 1-0x30, ammo 0x32-0x48, counts above 99 as 99, empty entries and
 * repeats left out, at most 0x30 of each. Returns XG_PARTY_OK or why not
 * (why may be NULL). */
int xgPartyPack(XgPartyRecord* out, const uint8_t* block, const int members[XG_PARTY_MEMBERS],
                const uint8_t* codes, XgPartyWhy* why);

/* Every range check of the record (the reasons above). codes: the code
 * table every name glyph must be one of (the game's names can only have
 * those). Returns XG_PARTY_OK or why not (why may be NULL). */
int xgPartyCheck(const XgPartyRecord* r, const uint8_t* codes, XgPartyWhy* why);

/* Member k's numbers in a record, for summaries and logs: 1 if k is one of
 * its members (k < m3_count), else 0. */
typedef struct {
    int id, level, hp, maxHp, ep, maxEp;
} XgPartyMember;
int xgPartyMemberInfo(const XgPartyRecord* r, int k, XgPartyMember* out);

/* party_game.c (the game's build only): the running release's code table,
 * read from its disc image (cd: never the drive's state). 1 when read. */
int xgPartyGameCodes(uint8_t codes[XG_PARTY_CODE_TABLE]);
/* The same from the disc image at `path` (a .cue or .bin of this region's
 * release), read and closed again: 1 when read. */
int xgPartyImageCodes(const char* path, uint8_t codes[XG_PARTY_CODE_TABLE]);

#ifdef __cplusplus
}
#endif
#endif
