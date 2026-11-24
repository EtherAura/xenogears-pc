/* A side's party for party vs party (party.h): packed from a save block,
 * and the range checks every record passes, ours and a peer's alike. */

#include "party.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "card.h"
#include "common.h"
#include "main/game_state.h"
#include "saves.h"

/* The record holds the PS1's records as bytes: their sizes and the fields
 * read here are the game's, and every target reads them little-endian. */
_Static_assert(sizeof(GameStateCharacter) == XG_PARTY_RECORD, "a character record is 0xA4 bytes on the PS1");
_Static_assert(sizeof(GameStateSize20) == XG_PARTY_ABILITY, "an ability row is 0x20 bytes on the PS1");
_Static_assert(sizeof(gameState.m0_names[0]) == XG_PARTY_NAME, "a name is 0x14 bytes");
_Static_assert(sizeof(gameState.m22B8_ammoCounts) == XG_PARTY_AMMO_TABLE, "the ammo-count table");
_Static_assert(offsetof(XgPartyRecord, m10_characters) == 0x10 && offsetof(XgPartyRecord, m1FC_abilities) == 0x1FC &&
                   offsetof(XgPartyRecord, m25C_names) == 0x25C && offsetof(XgPartyRecord, m298_itemIds) == 0x298 &&
                   offsetof(XgPartyRecord, m2C8_itemCounts) == 0x2C8 &&
                   offsetof(XgPartyRecord, m2F8_ammoIds) == 0x2F8 &&
                   offsetof(XgPartyRecord, m328_ammoCounts) == 0x328 &&
                   offsetof(XgPartyRecord, m358_ammoTable) == 0x358,
               "the party record's offsets");
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "the party record is the PS1's layout, little-endian"
#endif

#define ID_MAX 10           /* character ids 0-10 (m26C_party[11]) */
#define GEAR_COUNT 20       /* m978_gears[20] */
#define BILLY 4             /* the loadout whose attacks spend loaded rounds (spendBillyAmmo) */
#define LOADED_FIRST 0x32   /* gameStateAmmoByWeaponItem[id]: inside the ammo-count table */
#define LOADED_LAST 0x49
#define ITEM_LAST 0x30      /* initBattleInventory: inventory 0's ids 1-0x30 */
#define AMMO_FIRST 0x32     /* and inventory 1's 0x32-0x48 */
#define AMMO_LAST 0x48
#define COUNT_MAX 99
#define AMMO_LEFT_MAX 100   /* m22B8_ammoCounts: rounds left, 0-100 */
#define HP_MAX 999          /* rollMaxHpGrowth's cap */
#define EP_MAX 99           /* rollMaxMpGrowth's cap */
#define LEVEL_MAX 99
#define AP_MAX 7            /* setAPMarksPositions draws max AP - 1 marks into 6 lines (battle.h m908_apMarks) */

/* The printer's first two-byte code (the font's header +4, dialogFontVar0:
 * measured on both releases' discs): a byte from it on starts a pair. */
static int pairLead(int region) {
    return region == 'J' ? 0xF8 : 0xFE;
}

static unsigned rd16(const uint8_t* p) { return p[0] | p[1] << 8; }

static int fail(XgPartyWhy* why, int reason, int member, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
static int fail(XgPartyWhy* why, int reason, int member, const char* fmt, ...) {
    if (why != NULL) {
        va_list ap;
        va_start(ap, fmt);
        why->reason = reason;
        why->member = member;
        vsnprintf(why->text, sizeof(why->text), fmt, ap);
        va_end(ap);
    }
    return reason;
}

static int ok(XgPartyWhy* why) {
    if (why != NULL) {
        why->reason = XG_PARTY_OK;
        why->member = -1;
        why->text[0] = '\0';
    }
    return XG_PARTY_OK;
}

/* (a, b) is one of the code table's glyphs. */
static int inTable(const uint8_t* codes, int a, int b) {
    for (int i = 0; i < XG_PARTY_CODES; i++) {
        if (codes[2 * i] == a && codes[2 * i + 1] == b) return 1;
    }
    return 0;
}

static int allZero(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (p[i]) return 0;
    }
    return 1;
}

/* A name as the printer reads it: glyphs of the code table up to a 0, zeros
 * after. 0 and a reason in *bad if not. */
static int nameOk(const uint8_t* n, int region, const uint8_t* codes, const char** bad) {
    int lead = pairLead(region);
    int i = 0;
    while (i < XG_PARTY_NAME && n[i] != 0) {
        int c = n[i];
        if (c < 0x10) {
            *bad = c == 0x0F ? "the 0x0F escape" : "a control code below 0x10";
            return 0;
        }
        if (c >= lead) {
            if (i + 1 >= XG_PARTY_NAME || n[i + 1] == 0 || !inTable(codes, c, n[i + 1])) {
                *bad = "a two-byte glyph not in the code table";
                return 0;
            }
            i += 2;
        } else {
            if (!inTable(codes, 0, c)) {
                *bad = "a glyph not in the code table";
                return 0;
            }
            i++;
        }
    }
    if (i >= XG_PARTY_NAME) {
        *bad = "no terminator";
        return 0;
    }
    if (!allZero(n + i, XG_PARTY_NAME - i)) {
        *bad = "bytes after the terminator";
        return 0;
    }
    return 1;
}

/* A list of ids and counts: ids first..last, counts 1-99, no repeats, then
 * only zeros. The bad index, or -1. */
static int listBad(const uint8_t* ids, const uint8_t* counts, int first, int last) {
    int n = 0;
    while (n < XG_PARTY_ITEMS && ids[n] != 0) n++;
    for (int i = 0; i < n; i++) {
        if (ids[i] < first || ids[i] > last || counts[i] < 1 || counts[i] > COUNT_MAX) return i;
        for (int j = 0; j < i; j++) {
            if (ids[j] == ids[i]) return i;
        }
    }
    for (int i = n; i < XG_PARTY_ITEMS; i++) {
        if (ids[i] != 0 || counts[i] != 0) return i;
    }
    return -1;
}

int xgPartyCheck(const XgPartyRecord* r, const uint8_t* codes, XgPartyWhy* why) {
    if (codes == NULL) return fail(why, XG_PARTY_NO_CODES, -1, "no character-code table to check the names with");
    if (r->m0_format != XG_PARTY_FORMAT || r->m2_flags != 0 || rd16(r->m8_size) != XG_PARTY_BYTES || r->m7_zero != 0 ||
        !allZero(r->mA_zero, sizeof(r->mA_zero))) {
        return fail(why, XG_PARTY_BAD_HEADER, -1, "format %d, flags 0x%02x, size %u: not a format %d record",
                    r->m0_format, r->m2_flags, rd16(r->m8_size), XG_PARTY_FORMAT);
    }
    if (r->m1_region != cardGameRegion()) {
        return fail(why, XG_PARTY_BAD_REGION, -1, "packed for region '%c', this game is '%c'",
                    r->m1_region >= 0x20 && r->m1_region < 0x7F ? r->m1_region : '?', cardGameRegion());
    }
    int count = r->m3_count;
    if (count < 1 || count > XG_PARTY_MEMBERS) {
        return fail(why, XG_PARTY_BAD_MEMBERS, -1, "%d members, not 1-3", count);
    }
    for (int k = 0; k < XG_PARTY_MEMBERS; k++) {
        int id = r->m4_members[k];
        if (k >= count) {
            if (id != XG_PARTY_NONE || !allZero(r->m10_characters[k], XG_PARTY_RECORD) ||
                !allZero(r->m1FC_abilities[k], XG_PARTY_ABILITY) || !allZero(r->m25C_names[k], XG_PARTY_NAME)) {
                return fail(why, XG_PARTY_BAD_MEMBERS, k, "place %d is past the %d members but not empty", k + 1, count);
            }
            continue;
        }
        if (id > ID_MAX) return fail(why, XG_PARTY_BAD_MEMBERS, k, "place %d: id %d, not 0-10", k + 1, id);
        for (int j = 0; j < k; j++) {
            if (r->m4_members[j] == id) return fail(why, XG_PARTY_BAD_MEMBERS, k, "place %d: id %d again", k + 1, id);
        }
    }
    for (int k = 0; k < count; k++) {
        int id = r->m4_members[k];
        const uint8_t* c = r->m10_characters[k];
        int loadout = c[offsetof(GameStateCharacter, m56_battleCommandLoadout)];
        if (loadout != id) {
            return fail(why, XG_PARTY_BAD_LOADOUT, k, "member %d (id %d): command loadout %d", k + 1, id, loadout);
        }
        int gear = c[offsetof(GameStateCharacter, mA0_gearNum)];
        if (gear >= GEAR_COUNT && gear != 0xFF) {
            return fail(why, XG_PARTY_BAD_GEAR, k, "member %d (id %d): Gear number %d", k + 1, id, gear);
        }
        if (id == BILLY) {
            const uint8_t* eq = c + offsetof(GameStateCharacter, m6F_equipment);
            for (int s = 0; s <= 3; s += 3) {
                if (eq[s] < LOADED_FIRST || eq[s] > LOADED_LAST) {
                    return fail(why, XG_PARTY_BAD_LOADED_AMMO, k, "member %d (Billy): loaded rounds 0x%02x (m6F_equipment[%d]), not 0x%02x-0x%02x",
                                k + 1, eq[s], s, LOADED_FIRST, LOADED_LAST);
                }
            }
        }
        unsigned hp = rd16(c + offsetof(GameStateCharacter, m4C_HP));
        unsigned maxHp = rd16(c + offsetof(GameStateCharacter, m4E_MaxHP));
        if (maxHp < 1 || maxHp > HP_MAX || hp > maxHp) {
            return fail(why, XG_PARTY_BAD_HP, k, "member %d (id %d): HP %u of %u", k + 1, id, hp, maxHp);
        }
        unsigned ep = rd16(c + offsetof(GameStateCharacter, m50_MP));
        unsigned maxEp = rd16(c + offsetof(GameStateCharacter, m52_MaxMP));
        if (maxEp > EP_MAX || ep > maxEp) {
            return fail(why, XG_PARTY_BAD_EP, k, "member %d (id %d): EP %u of %u", k + 1, id, ep, maxEp);
        }
        int level = c[offsetof(GameStateCharacter, m62_Level)];
        int level2 = c[offsetof(GameStateCharacter, m63_Level2)];
        if (level < 1 || level > LEVEL_MAX || level2 < 1 || level2 > LEVEL_MAX) {
            return fail(why, XG_PARTY_BAD_LEVEL, k, "member %d (id %d): levels %d and %d", k + 1, id, level, level2);
        }
        int ap = r->m1FC_abilities[k][offsetof(GameStateSize20, m17_energyPerTurn)];
        if (ap < 1 || ap > AP_MAX) {
            return fail(why, XG_PARTY_BAD_AP, k, "member %d (id %d): AP per turn %d", k + 1, id, ap);
        }
        const char* bad = "";
        if (!nameOk(r->m25C_names[k], r->m1_region, codes, &bad)) {
            return fail(why, XG_PARTY_BAD_NAME, k, "member %d (id %d): name with %s", k + 1, id, bad);
        }
    }
    int at = listBad(r->m298_itemIds, r->m2C8_itemCounts, 1, ITEM_LAST);
    if (at >= 0) {
        return fail(why, XG_PARTY_BAD_ITEMS, -1, "item %d: id 0x%02x x%d", at + 1, r->m298_itemIds[at], r->m2C8_itemCounts[at]);
    }
    at = listBad(r->m2F8_ammoIds, r->m328_ammoCounts, AMMO_FIRST, AMMO_LAST);
    if (at >= 0) {
        return fail(why, XG_PARTY_BAD_AMMO, -1, "ammo %d: id 0x%02x x%d", at + 1, r->m2F8_ammoIds[at], r->m328_ammoCounts[at]);
    }
    for (int i = 0; i < XG_PARTY_AMMO_TABLE; i++) {
        if (r->m358_ammoTable[i] > AMMO_LEFT_MAX) {
            return fail(why, XG_PARTY_BAD_AMMO_TABLE, -1, "ammo-count table entry %d: %d", i, r->m358_ammoTable[i]);
        }
    }
    return ok(why);
}

int xgPartyMemberInfo(const XgPartyRecord* r, int k, XgPartyMember* out) {
    if (k < 0 || k >= r->m3_count || k >= XG_PARTY_MEMBERS) return 0;
    const uint8_t* c = r->m10_characters[k];
    out->id = r->m4_members[k];
    out->level = c[offsetof(GameStateCharacter, m62_Level)];
    out->hp = (int)rd16(c + offsetof(GameStateCharacter, m4C_HP));
    out->maxHp = (int)rd16(c + offsetof(GameStateCharacter, m4E_MaxHP));
    out->ep = (int)rd16(c + offsetof(GameStateCharacter, m50_MP));
    out->maxEp = (int)rd16(c + offsetof(GameStateCharacter, m52_MaxMP));
    return 1;
}

/* Name c's character codes (u16 each, 0 ends them) as the game decodes them
 * (decodeCharacterNames, decodeCharacterCodes). 0 if a code is past the
 * table, a glyph is none, or the bytes do not fit with their terminator. */
static int decodeName(const uint8_t* src, const uint8_t* codes, uint8_t* out) {
    int n = 0;
    memset(out, 0, XG_PARTY_NAME);
    for (int j = 0; j < XG_PARTY_NAME; j += 2) {
        unsigned code = rd16(src + j);
        if (code == 0) break;
        if (code >= XG_PARTY_CODES) return 0;
        int a = codes[2 * code], b = codes[2 * code + 1];
        if (b == 0 || n + (a ? 2 : 1) > XG_PARTY_NAME - 1) return 0;
        if (a) out[n++] = (uint8_t)a;
        out[n++] = (uint8_t)b;
    }
    return 1;
}

/* An inventory into the battle's list: ids first..last with their counts,
 * as initBattleInventory takes them (counts above 99 as 99), without empty
 * entries and repeats, at most XG_PARTY_ITEMS. */
static void takeList(const uint8_t* ids, const uint8_t* counts, int entries, int first, int last, uint8_t* outIds,
                     uint8_t* outCounts) {
    int n = 0;
    for (int i = 0; i < entries && n < XG_PARTY_ITEMS; i++) {
        int id = ids[i], count = counts[i] > COUNT_MAX ? COUNT_MAX : counts[i];
        if (id < first || id > last || count == 0) continue;
        int seen = 0;
        for (int j = 0; j < n; j++) seen |= outIds[j] == id;
        if (seen) continue;
        outIds[n] = (uint8_t)id;
        outCounts[n++] = (uint8_t)count;
    }
}

int xgPartyPack(XgPartyRecord* out, const uint8_t* b, const int members[XG_PARTY_MEMBERS], const uint8_t* codes,
                XgPartyWhy* why) {
    memset(out, 0, sizeof(*out));
    if (codes == NULL) return fail(why, XG_PARTY_NO_CODES, -1, "no character-code table to decode the names with");
    CardGameSave save;
    if (!cardGameSaveRead(b, &save)) return fail(why, XG_PARTY_BAD_SAVE, -1, "not a complete save (its checksum)");
    int count = 0;
    while (count < XG_PARTY_MEMBERS && members[count] != XG_PARTY_NONE) count++;
    for (int k = 0; k < XG_PARTY_MEMBERS; k++) {
        int id = members[k];
        if (k >= count ? id != XG_PARTY_NONE : id < 0 || id > ID_MAX) {
            return fail(why, XG_PARTY_BAD_MEMBERS, k, "place %d: id %d", k + 1, id);
        }
        if (k < count && !(save.joined & 1 << id)) {
            return fail(why, XG_PARTY_NOT_JOINED, k, "member %d (id %d) has not joined in this save", k + 1, id);
        }
    }
    if (count == 0) return fail(why, XG_PARTY_BAD_MEMBERS, -1, "no members");
    out->m0_format = XG_PARTY_FORMAT;
    out->m1_region = (uint8_t)cardGameRegion();
    out->m3_count = (uint8_t)count;
    out->m8_size[0] = XG_PARTY_BYTES & 0xFF;
    out->m8_size[1] = XG_PARTY_BYTES >> 8;
    for (int k = 0; k < XG_PARTY_MEMBERS; k++) {
        if (k >= count) {
            out->m4_members[k] = XG_PARTY_NONE;
            continue;
        }
        int id = members[k];
        out->m4_members[k] = (uint8_t)id;
        memcpy(out->m10_characters[k], b + CARD_SAVE_PARTY + id * XG_PARTY_RECORD, XG_PARTY_RECORD);
        memcpy(out->m1FC_abilities[k], b + CARD_SAVE_ABILITIES + id * XG_PARTY_ABILITY, XG_PARTY_ABILITY);
        if (!decodeName(b + CARD_SAVE_NAMES + id * XG_PARTY_NAME, codes, out->m25C_names[k])) {
            return fail(why, XG_PARTY_BAD_NAME, k, "member %d (id %d): a name the code table cannot decode", k + 1, id);
        }
    }
    takeList(b + CARD_SAVE_AT(offsetof(GameState, m2026_inventory0Ids)),
             b + CARD_SAVE_AT(offsetof(GameState, m1F90_inventory0Counts)), sizeof(gameState.m2026_inventory0Ids), 1,
             ITEM_LAST, out->m298_itemIds, out->m2C8_itemCounts);
    takeList(b + CARD_SAVE_AT(offsetof(GameState, m1D9C_inventory1Ids)),
             b + CARD_SAVE_AT(offsetof(GameState, m1D38_inventory1Counts)), sizeof(gameState.m1D9C_inventory1Ids),
             AMMO_FIRST, AMMO_LAST, out->m2F8_ammoIds, out->m328_ammoCounts);
    memcpy(out->m358_ammoTable, b + CARD_SAVE_AT(offsetof(GameState, m22B8_ammoCounts)), XG_PARTY_AMMO_TABLE);
    return xgPartyCheck(out, codes, why);
}
