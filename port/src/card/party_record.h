#ifndef PORT_CARD_PARTY_RECORD_H
#define PORT_CARD_PARTY_RECORD_H

/* The fixed-layout party record is shared with the game-side overlays. */
#define XG_PARTY_FORMAT 2
#define XG_PARTY_MEMBERS 3
#define XG_PARTY_NONE 0xFF        /* m4_members past m3_count */
#define XG_PARTY_RECORD 0xA4      /* GameStateCharacter */
#define XG_PARTY_ABILITY 0x20     /* GameStateSize20 */
#define XG_PARTY_NAME 0x14        /* gameState.m0_names[c] */
#define XG_PARTY_ITEMS 0x30       /* the battle's item and ammo lists */
#define XG_PARTY_AMMO_TABLE 0x60  /* gameState.m22B8_ammoCounts */
#define XG_PARTY_GEARS 0x01       /* reserved flag */
#define XG_PARTY_GEAR_EQUIP 0x10 /* MenuSaveGearEquip, the part indices */
/* The kernel text's character-code table (01/07, the bundle's member at
 * printDialogTextVar[27]): code c is the glyph (a, b) at bytes 2c and 2c+1,
 * one byte b when a is 0, two bytes a b otherwise (decodeCharacterCodes). */
#define XG_PARTY_CODES 0x144
#define XG_PARTY_CODE_TABLE (2 * XG_PARTY_CODES)

typedef struct {
    /* 0x000 */ unsigned char m0_format;                  /* XG_PARTY_FORMAT */
    /* 0x001 */ unsigned char m1_region;                  /* 'U' or 'J': the game it was packed for */
    /* 0x002 */ unsigned char m2_flags;                   /* reserved, zero */
    /* 0x003 */ unsigned char m3_count;                   /* members, 1-3 */
    /* 0x004 */ unsigned char m4_members[XG_PARTY_MEMBERS]; /* character ids 0-10 in order (the first leads),
                                                     * XG_PARTY_NONE past m3_count */
    /* 0x007 */ unsigned char m7_zero;
    /* 0x008 */ unsigned char m8_size[2];                 /* the record's bytes (little-endian): XG_PARTY_BYTES */
    /* 0x00A */ unsigned char mA_zero[6];
    /* 0x010 */ unsigned char m10_characters[XG_PARTY_MEMBERS][XG_PARTY_RECORD]; /* gameState.m26C_party[id] as saved */
    /* 0x1FC */ unsigned char m1FC_abilities[XG_PARTY_MEMBERS][XG_PARTY_ABILITY]; /* gameState.m16C0_abilities[id] */
    /* 0x25C */ unsigned char m25C_names[XG_PARTY_MEMBERS][XG_PARTY_NAME]; /* gameState.m0_names[id]: the game's
                                                     * text bytes, 0-terminated, zeros after */
    /* 0x298 */ unsigned char m298_itemIds[XG_PARTY_ITEMS];    /* battleEffectBF.m42_ids: inventory 0's items 1-0x30 */
    /* 0x2C8 */ unsigned char m2C8_itemCounts[XG_PARTY_ITEMS]; /* their counts, 1-99 */
    /* 0x2F8 */ unsigned char m2F8_ammoIds[XG_PARTY_ITEMS];    /* battleAmmoItemIds: inventory 1's ammo 0x32-0x48 */
    /* 0x328 */ unsigned char m328_ammoCounts[XG_PARTY_ITEMS]; /* their counts, 1-99 */
    /* 0x358 */ unsigned char m358_ammoTable[XG_PARTY_AMMO_TABLE]; /* gameState.m22B8_ammoCounts, each 0-100 */
    /* 0x3B8 */ unsigned char m3B8_gearEquip[XG_PARTY_MEMBERS][XG_PARTY_GEAR_EQUIP];
} XgPartyRecord;
#define XG_PARTY_BYTES 0x3E8

#ifdef __cplusplus
static_assert(sizeof(XgPartyRecord) == XG_PARTY_BYTES, "the party record's layout");
#else
_Static_assert(sizeof(XgPartyRecord) == XG_PARTY_BYTES, "the party record's layout");
#endif

#endif
