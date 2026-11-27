/* The save reader (card/saves.h) and the party record (card/party.h) on
 * synthetic 8 KB saves in synthetic card images: no disc, no real card.
 *
 *   card_savestest                              the checks
 *   card_savestest [--codes FILE] CARD [CARD2]  the saves of these images
 *       (read only) as the browser lists them; with FILE (the 0x288-byte
 *       code table, 01/07's member at printDialogTextVar[27]) each save's
 *       current party packed and checked too
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "card/card.h"
#include "card/party.h"
#include "card/saves.h"
#include "common.h"
#include "main/game_state.h"
#include "menu/menu.h"

int cardBusy(int slot) { return 0; } /* image.c's: no game here */

static int failures;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
            failures++;                                                  \
        }                                                                \
    } while (0)

#define REC(c) (CARD_SAVE_PARTY + (c) * XG_PARTY_RECORD)
#define F(field) offsetof(GameStateCharacter, field)
#define AT(field) CARD_SAVE_AT(offsetof(GameState, field))

/* ---- a code table like the game's: single bytes 0x10 up to the first
 * pair code (US 0xFE, JP 0xF8), two pairs, and (0, 0x0F), which a name
 * may still not have: the escape is refused by itself. */
static uint8_t codes[XG_PARTY_CODE_TABLE];
static int lead;
enum { CODE_PAIR = 0x140, CODE_ESCAPE = 0x141, CODE_PAIR2 = 0x142 };

static void makeCodes(void) {
    lead = cardGameRegion() == 'J' ? 0xF8 : 0xFE;
    memset(codes, 0, sizeof(codes));
    for (int i = 1; 0x0F + i < lead; i++) codes[2 * i + 1] = (uint8_t)(0x0F + i);
    codes[2 * CODE_PAIR] = (uint8_t)lead;
    codes[2 * CODE_PAIR + 1] = 0x05; /* a pair's second byte may be below 0x10 */
    codes[2 * CODE_ESCAPE + 1] = 0x0F;
    codes[2 * CODE_PAIR2] = (uint8_t)lead;
    codes[2 * CODE_PAIR2 + 1] = 0x41;
}
static unsigned codeOf(char ch) { return (unsigned)(uint8_t)ch - 0x0F; } /* "Fei" as character codes */

static void put16(uint8_t* p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t* p, uint32_t v) {
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}

static void seal(uint8_t* b) {
    unsigned sum = 0;
    for (int k = 0x100; k < 0x1FFF; k++) sum += b[k];
    b[0x1FFF] = (uint8_t)sum;
}

/* A save's name: a string of single-byte glyphs, as codes. */
static void saveName(uint8_t* b, int c, const char* s) {
    uint8_t* n = b + CARD_SAVE_NAMES + c * XG_PARTY_NAME;
    memset(n, 0, XG_PARTY_NAME);
    for (int i = 0; s[i] && i < 10; i++) put16(n + 2 * i, codeOf(s[i]));
}

static void member(uint8_t* b, int c, int level, int hp, int maxHp, int ep, int maxEp, int ap, int gear) {
    uint8_t* r = b + REC(c);
    put16(r + F(m4C_HP), hp);
    put16(r + F(m4E_MaxHP), maxHp);
    put16(r + F(m50_MP), ep);
    put16(r + F(m52_MaxMP), maxEp);
    r[F(m56_battleCommandLoadout)] = (uint8_t)c;
    r[F(m62_Level)] = r[F(m63_Level2)] = (uint8_t)level;
    put32(r + F(m44_expToNextLevel), 100 * (uint32_t)level); /* as a real save has them: 1 or more */
    put32(r + F(m48_expToNextLevel2), 90 * (uint32_t)level);
    r[F(mA0_gearNum)] = (uint8_t)gear;
    r[F(m58_attack)] = (uint8_t)(10 + c); /* something of its own, to see it copied */
    b[CARD_SAVE_ABILITIES + c * XG_PARTY_ABILITY + offsetof(GameStateSize20, m17_energyPerTurn)] = (uint8_t)ap;
    b[CARD_SAVE_ABILITIES + c * XG_PARTY_ABILITY] = (uint8_t)(0x40 + c);
}

/* Fei, Elly, Citan and Billy joined; Fei and Citan in the party. */
static void baseSave(uint8_t* b) {
    memset(b, 0, CARD_BLOCK);
    uint32_t t = (1 * 3600 + 2 * 60 + 3) * 60; /* play time, 1:02:03 */
    b[CARD_SAVE_DATA] = (uint8_t)t;
    b[CARD_SAVE_DATA + 1] = (uint8_t)(t >> 8);
    b[CARD_SAVE_DATA + 2] = (uint8_t)(t >> 16);
    saveName(b, 0, "Fei");
    saveName(b, 1, "Elly");
    saveName(b, 2, "Citan");
    saveName(b, 4, "Billy");
    member(b, 0, 41, 300, 332, 40, 46, 6, 0);
    member(b, 1, 36, 259, 259, 50, 50, 6, 2);
    member(b, 2, 36, 386, 386, 62, 62, 6, 3);
    member(b, 4, 49, 314, 314, 58, 58, 6, 5);
    b[REC(4) + F(m6F_equipment) + 0] = 0x35;
    b[REC(4) + F(m6F_equipment) + 3] = 0x3A;
    put16(b + AT(m1930_fieldVarsBackup), 149);
    put16(b + AT(m1D30_partyMemberBitField), 0x17 | 0x800); /* a bit past id 10 too, as saves have */
    b[AT(m1D34_currentParty) + 0] = 0;
    b[AT(m1D34_currentParty) + 1] = 2;
    b[AT(m1D34_currentParty) + 2] = 0xFF;
    /* items: 3 x5, an empty entry, 0x31 (not a battle item), 7 x120 (as 99), 3 again */
    static const uint8_t ids0[] = {3, 9, 0x31, 7, 3, 0x30};
    static const uint8_t counts0[] = {5, 0, 4, 120, 2, 1};
    for (int i = 0; i < 6; i++) {
        b[AT(m2026_inventory0Ids) + 10 + i] = ids0[i];
        b[AT(m1F90_inventory0Counts) + 10 + i] = counts0[i];
    }
    /* inventory 1: a weapon, two kinds of rounds, 0x49 (not ammo) */
    static const uint8_t ids1[] = {0x12, 0x35, 0x49, 0x3A};
    static const uint8_t counts1[] = {1, 20, 3, 99};
    for (int i = 0; i < 4; i++) {
        b[AT(m1D9C_inventory1Ids) + i] = ids1[i];
        b[AT(m1D38_inventory1Counts) + i] = counts1[i];
    }
    for (int i = 0; i < XG_PARTY_AMMO_TABLE; i++) b[AT(m22B8_ammoCounts) + i] = (uint8_t)(i % 101);
    b[AT(m22B8_ammoCounts) + 5] = 100;
    seal(b);
}

/* ---- card images ------------------------------------------------------ */

static void sjisTitle(uint8_t* block, const char* s) {
    uint8_t* o = block + 4;
    memset(o, 0, 64);
    for (int i = 0; s[i] && i < 31; i++) {
        char c = s[i];
        uint8_t a = 0x81, b = 0x40;
        if (c >= 'A' && c <= 'Z') a = 0x82, b = (uint8_t)(0x60 + c - 'A');
        else if (c >= 'a' && c <= 'z') a = 0x82, b = (uint8_t)(0x81 + c - 'a');
        else if (c >= '0' && c <= '9') a = 0x82, b = (uint8_t)(0x4F + c - '0');
        else if (c == '/') b = 0x5E;
        else if (c == '.') b = 0x44;
        *o++ = a;
        *o++ = b;
    }
}

static int addSave(int card, const char* name, const char* title, const uint8_t* save) {
    int block = cardCreateFile(cards[card].data, name, 1, NULL);
    if (block) {
        memcpy(cards[card].data + block * CARD_BLOCK, save, CARD_BLOCK);
        sjisTitle(cards[card].data + block * CARD_BLOCK, title);
    }
    return block;
}

static const char* const names[11] = {"Fei", "Elly", "Citan", "Bart", "Billy", "Rico", "Emeralda", "Chu-Chu",
                                      "Maria", "Citan (sword)", "Emeralda (adult)"};

static uint32_t fnv(const void* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t*)p)[i]) * 16777619u;
    return h;
}

static void testReader(void) {
    static uint8_t save[CARD_BLOCK], bad[CARD_BLOCK], other[CARD_BLOCK];
    baseSave(save);
    memcpy(bad, save, CARD_BLOCK);
    bad[0x1000] ^= 1; /* the sum no longer holds */
    memcpy(other, save, CARD_BLOCK);
    put16(other + AT(m1930_fieldVarsBackup), 52);
    put16(other + AT(m1930_fieldVarsBackup) + 2 * 82, 1); /* disc 2 */
    memcpy(other + 0x1F00, "XGID\x78\x56\x34\x12", 8);
    seal(other);

    CardGameSave s;
    CHECK(cardGameSaveRead(save, &s) == 1);
    CHECK(s.progress == 149 && s.disc == 1 && s.playTime == 3723 * 60 && s.id == 0);
    CHECK(s.joined == 0x17); /* ids 0-10 only */
    CHECK(s.party[0] == 0 && s.party[1] == 2 && s.party[2] == 0xFF);
    CHECK(s.members[0].level == 41 && s.members[0].hp == 300 && s.members[0].maxHp == 332);
    CHECK(s.members[2].ep == 62 && s.members[2].maxEp == 62 && s.members[4].level == 49);
    CHECK(cardGameSaveRead(bad, &s) == 0); /* a bad checksum */
    CHECK(cardGameSaveRead(other, &s) == 1 && s.disc == 2 && s.progress == 52 && s.id == 0x12345678);

    const char* mine = cardGameRegion() == 'J' ? "BISLPS-01160" : "BASLUS-00664";
    const char* theirs = cardGameRegion() == 'J' ? "BASLUS-00664" : "BISLPS-01160";
    char name[32];
    for (int c = 0; c < 2; c++) {
        cardFormatImage(cards[c].data);
        cards[c].present = 1;
    }
    snprintf(name, sizeof(name), "%s0", mine);
    int a = addSave(0, name, "XENOGEARS/No.07 Test Cave", save);
    snprintf(name, sizeof(name), "%s1", mine);
    int b = addSave(0, name, "XENOGEARS/No.08 Broken", bad);
    int c = addSave(0, "BASLUS-00999SAVE", "SOMETHING ELSE", save);
    snprintf(name, sizeof(name), "%s0", theirs);
    int d = addSave(1, name, "XENOGEARS/No.01 Elsewhere", save);
    snprintf(name, sizeof(name), "%s2", mine);
    int e = addSave(1, name, "XENOGEARS/No.12 Into Solaris", other);
    CHECK(a == 1 && b == 2 && c == 3 && d == 1 && e == 2);

    CardGameSave list[30];
    int n = cardGameSaves(list, 30, cardGameRegion());
    CHECK(n == 2); /* not the broken one, the other game's or the other region's */
    if (n == 2) {
        CHECK(list[0].card == 0 && list[0].block == 1 && list[0].region == cardGameRegion());
        CHECK(list[0].number == 7 && !strcmp(list[0].location, "Test Cave") && list[0].progress == 149);
        CHECK(!strcmp(list[0].title, "XENOGEARS/No.07 Test Cave"));
        CHECK(list[1].card == 1 && list[1].block == 2 && list[1].number == 12 && !strcmp(list[1].location, "Into Solaris"));
        CHECK(list[1].disc == 2 && list[1].id == 0x12345678);
        CHECK(cardGameSaveBlock(&list[0]) == cards[0].data + CARD_BLOCK);
        CHECK(cardGameSaveBlock(&list[1]) == cards[1].data + 2 * CARD_BLOCK);
    }
    /* both regions (the bestiary's list): card 1's other-region save in its place */
    n = cardGameSaves(list, 30, 0);
    CHECK(n == 3);
    if (n == 3) {
        CHECK(list[0].card == 0 && list[1].card == 1 && list[1].block == 1 && list[1].region != cardGameRegion());
        CHECK(list[2].card == 1 && list[2].block == 2);
    }
    CHECK(cardGameSaves(list, 1, 0) == 1 && list[0].card == 0);
    /* a slot without a card, an unformatted card */
    cards[1].present = 0;
    CHECK(cardGameSaves(list, 30, 0) == 1);
    cards[1].present = 1;
    memset(cards[1].data, 0, CARD_FRAME);
    CHECK(cardGameSaves(list, 30, 0) == 1);
    /* the listed block changed since: no longer a save */
    n = cardGameSaves(list, 30, cardGameRegion());
    CHECK(n == 1);
    cards[0].data[CARD_BLOCK + 0x1000] ^= 1;
    CHECK(cardGameSaveBlock(&list[0]) == NULL);
    cards[0].data[CARD_BLOCK + 0x1000] ^= 1;
    CHECK(cardGameSaveBlock(&list[0]) != NULL);
    cards[0].data[CARD_FRAME + 0x0A + 2] = 'X'; /* renamed: not this game's */
    CHECK(cardGameSaveBlock(&list[0]) == NULL);
}

/* ---- the party record ------------------------------------------------- */

static XgPartyRecord good;

static int checkOf(const XgPartyRecord* r) {
    XgPartyWhy why;
    int reason = xgPartyCheck(r, codes, &why);
    CHECK(why.reason == reason);
    return reason;
}

/* r with one byte of member k's record (or ability row) changed: the check's answer. */
static int withRecord(int k, size_t at, unsigned v, int wide) {
    XgPartyRecord r = good;
    r.m10_characters[k][at] = (uint8_t)v;
    if (wide) r.m10_characters[k][at + 1] = (uint8_t)(v >> 8);
    return checkOf(&r);
}

static int withAp(int k, int v) {
    XgPartyRecord r = good;
    r.m1FC_abilities[k][offsetof(GameStateSize20, m17_energyPerTurn)] = (uint8_t)v;
    return checkOf(&r);
}

static int withName(int k, const uint8_t* bytes, int n) {
    XgPartyRecord r = good;
    memset(r.m25C_names[k], 0, XG_PARTY_NAME);
    memcpy(r.m25C_names[k], bytes, (size_t)n);
    return checkOf(&r);
}

static void testPack(void) {
    static uint8_t save[CARD_BLOCK];
    baseSave(save);
    int members[3] = {4, 0, 2};
    XgPartyWhy why;
    CHECK(xgPartyPack(&good, save, members, codes, &why) == XG_PARTY_OK);
    CHECK(why.reason == XG_PARTY_OK && why.member == -1);
    if (why.reason != XG_PARTY_OK) printf("  pack: %s\n", why.text);
    CHECK(good.m0_format == XG_PARTY_FORMAT && good.m1_region == cardGameRegion() && good.m2_flags == 0);
    CHECK(good.m3_count == 3 && good.m4_members[0] == 4 && good.m4_members[1] == 0 && good.m4_members[2] == 2);
    CHECK(good.m8_size[0] == (XG_PARTY_BYTES & 0xFF) && good.m8_size[1] == XG_PARTY_BYTES >> 8);
    CHECK(!memcmp(good.m10_characters[0], save + REC(4), XG_PARTY_RECORD));
    CHECK(!memcmp(good.m10_characters[1], save + REC(0), XG_PARTY_RECORD));
    CHECK(!memcmp(good.m1FC_abilities[2], save + CARD_SAVE_ABILITIES + 2 * XG_PARTY_ABILITY, XG_PARTY_ABILITY));
    /* names as the game decodes them, zeros after */
    CHECK(!memcmp(good.m25C_names[0], "Billy\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", XG_PARTY_NAME));
    CHECK(!memcmp(good.m25C_names[1], "Fei\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", XG_PARTY_NAME));
    /* the battle's lists: 3 x5, 7 x99, 0x30 x1 (no empty entry, no 0x31, no repeat) */
    CHECK(good.m298_itemIds[0] == 3 && good.m2C8_itemCounts[0] == 5);
    CHECK(good.m298_itemIds[1] == 7 && good.m2C8_itemCounts[1] == 99);
    CHECK(good.m298_itemIds[2] == 0x30 && good.m2C8_itemCounts[2] == 1 && good.m298_itemIds[3] == 0);
    CHECK(good.m2F8_ammoIds[0] == 0x35 && good.m328_ammoCounts[0] == 20);
    CHECK(good.m2F8_ammoIds[1] == 0x3A && good.m328_ammoCounts[1] == 99 && good.m2F8_ammoIds[2] == 0);
    CHECK(!memcmp(good.m358_ammoTable, save + AT(m22B8_ammoCounts), XG_PARTY_AMMO_TABLE));
    /* the same bytes everywhere (Linux, Windows): a fixed save packs to a fixed record */
    uint32_t h = fnv(&good, sizeof(good));
    printf("card_savestest: record %u bytes, fnv1a %08x\n", (unsigned)sizeof(good), h);
    CHECK(h == (cardGameRegion() == 'J' ? 0xc7e17ae1u : 0xf6e9e1b0u));

    /* a member's numbers */
    XgPartyMember m;
    CHECK(xgPartyMemberInfo(&good, 1, &m) == 1 && m.id == 0 && m.level == 41 && m.hp == 300 && m.maxHp == 332);
    CHECK(m.ep == 40 && m.maxEp == 46);
    CHECK(xgPartyMemberInfo(&good, 3, &m) == 0 && xgPartyMemberInfo(&good, -1, &m) == 0);

    /* one or two members; the rest zero */
    XgPartyRecord r;
    int one[3] = {2, 255, 255};
    CHECK(xgPartyPack(&r, save, one, codes, NULL) == XG_PARTY_OK && r.m3_count == 1);
    CHECK(xgPartyMemberInfo(&r, 0, &m) == 1 && m.id == 2 && xgPartyMemberInfo(&r, 1, &m) == 0);
    CHECK(r.m4_members[1] == XG_PARTY_NONE && r.m4_members[2] == XG_PARTY_NONE);
    int two[3] = {1, 2, 255};
    CHECK(xgPartyPack(&r, save, two, codes, NULL) == XG_PARTY_OK && r.m3_count == 2);
    /* refusals */
    int none[3] = {255, 255, 255};
    CHECK(xgPartyPack(&r, save, none, codes, NULL) == XG_PARTY_BAD_MEMBERS);
    int gap[3] = {0, 255, 2};
    CHECK(xgPartyPack(&r, save, gap, codes, NULL) == XG_PARTY_BAD_MEMBERS);
    int twice[3] = {0, 0, 255};
    CHECK(xgPartyPack(&r, save, twice, codes, NULL) == XG_PARTY_BAD_MEMBERS);
    int past[3] = {11, 255, 255};
    CHECK(xgPartyPack(&r, save, past, codes, NULL) == XG_PARTY_BAD_MEMBERS);
    int bart[3] = {0, 3, 255};
    CHECK(xgPartyPack(&r, save, bart, codes, &why) == XG_PARTY_NOT_JOINED && why.member == 1);
    CHECK(xgPartyPack(&r, save, members, NULL, NULL) == XG_PARTY_NO_CODES);
    CHECK(xgPartyCheck(&good, NULL, NULL) == XG_PARTY_NO_CODES);
    static uint8_t broken[CARD_BLOCK];
    memcpy(broken, save, CARD_BLOCK);
    broken[0x200] ^= 0x80;
    CHECK(xgPartyPack(&r, broken, members, codes, NULL) == XG_PARTY_BAD_SAVE);
    memcpy(broken, save, CARD_BLOCK);
    put16(broken + CARD_SAVE_NAMES + 0 * XG_PARTY_NAME + 2, XG_PARTY_CODES); /* past the table */
    seal(broken);
    CHECK(xgPartyPack(&r, broken, members, codes, &why) == XG_PARTY_BAD_NAME && why.member == 1);
    memcpy(broken, save, CARD_BLOCK);
    put16(broken + CARD_SAVE_NAMES + 0 * XG_PARTY_NAME + 2, 0x143); /* a code with no glyph */
    seal(broken);
    CHECK(xgPartyPack(&r, broken, members, codes, NULL) == XG_PARTY_BAD_NAME);
    memcpy(broken, save, CARD_BLOCK); /* ten pairs: twenty bytes, no room for the terminator */
    for (int i = 0; i < 10; i++) put16(broken + CARD_SAVE_NAMES + 2 * i, CODE_PAIR2);
    seal(broken);
    CHECK(xgPartyPack(&r, broken, members, codes, NULL) == XG_PARTY_BAD_NAME);
    memcpy(broken, save, CARD_BLOCK); /* nine pairs fit */
    for (int i = 0; i < 9; i++) put16(broken + CARD_SAVE_NAMES + 2 * i, CODE_PAIR2);
    put16(broken + CARD_SAVE_NAMES + 18, 0);
    seal(broken);
    CHECK(xgPartyPack(&r, broken, members, codes, NULL) == XG_PARTY_OK && r.m25C_names[1][17] == 0x41 &&
          r.m25C_names[1][18] == 0);
    /* an edited save out of range is packed, then refused by the check */
    memcpy(broken, save, CARD_BLOCK);
    put16(broken + REC(0) + F(m4E_MaxHP), 1000);
    seal(broken);
    CHECK(xgPartyPack(&r, broken, members, codes, &why) == XG_PARTY_BAD_HP && why.member == 1);
    /* more than 0x30 items: the first 0x30 */
    memcpy(broken, save, CARD_BLOCK);
    for (int i = 0; i < 150; i++) {
        broken[AT(m2026_inventory0Ids) + i] = (uint8_t)(1 + i % 0x30);
        broken[AT(m1F90_inventory0Counts) + i] = 1;
    }
    seal(broken);
    CHECK(xgPartyPack(&r, broken, members, codes, NULL) == XG_PARTY_OK && r.m298_itemIds[0x2F] == 0x30);

    /* The Gear parts belong to the selected member's Gear, not their slot. */
    memcpy(broken, save, CARD_BLOCK);
    uint8_t* gear = broken + CARD_SAVE_DATA + offsetof(MenuSaveGame, m99C_gears) + 5 * sizeof(MenuSaveGear);
    gear[offsetof(GameStateGear, m2_frame)] = 7;
    gear[offsetof(GameStateGear, m3_engine)] = 13;
    seal(broken);
    CHECK(xgPartyPack(&r, broken, members, codes, NULL) == XG_PARTY_OK);
    CHECK(!memcmp(r.m3B8_gearEquip[0], gear, XG_PARTY_GEAR_EQUIP));

    /* ---- every bound of the check, just in and just out ---- */
    CHECK(checkOf(&good) == XG_PARTY_OK);
    r = good;
    r.m0_format = XG_PARTY_FORMAT + 1;
    CHECK(checkOf(&r) == XG_PARTY_BAD_HEADER);
    r = good;
    r.m2_flags = XG_PARTY_GEARS;
    CHECK(checkOf(&r) == XG_PARTY_BAD_HEADER);
    r = good;
    r.m8_size[0]++;
    CHECK(checkOf(&r) == XG_PARTY_BAD_HEADER);
    r = good;
    r.mA_zero[5] = 1;
    CHECK(checkOf(&r) == XG_PARTY_BAD_HEADER);
    r = good;
    r.m7_zero = 1;
    CHECK(checkOf(&r) == XG_PARTY_BAD_HEADER);
    r = good;
    r.m1_region = (uint8_t)(cardGameRegion() == 'J' ? 'U' : 'J'); /* the other region */
    CHECK(checkOf(&r) == XG_PARTY_BAD_REGION);
    /* members: 1-3, ids 0-10, no repeats, nothing past the count */
    r = good;
    r.m3_count = 0;
    CHECK(checkOf(&r) == XG_PARTY_BAD_MEMBERS);
    r = good;
    r.m3_count = 4;
    CHECK(checkOf(&r) == XG_PARTY_BAD_MEMBERS);
    r = good;
    r.m4_members[2] = 10; /* id 10 is in range: its own loadout then */
    r.m10_characters[2][F(m56_battleCommandLoadout)] = 10;
    CHECK(checkOf(&r) == XG_PARTY_OK);
    r.m4_members[2] = 11;
    r.m10_characters[2][F(m56_battleCommandLoadout)] = 11;
    CHECK(checkOf(&r) == XG_PARTY_BAD_MEMBERS);
    r = good;
    r.m4_members[2] = 4; /* Billy twice */
    CHECK(checkOf(&r) == XG_PARTY_BAD_MEMBERS);
    r = good;
    r.m3_count = 2; /* a third member left in place */
    CHECK(checkOf(&r) == XG_PARTY_BAD_MEMBERS);
    r.m4_members[2] = XG_PARTY_NONE;
    CHECK(checkOf(&r) == XG_PARTY_BAD_MEMBERS); /* its record is still there */
    memset(r.m10_characters[2], 0, XG_PARTY_RECORD);
    memset(r.m1FC_abilities[2], 0, XG_PARTY_ABILITY);
    CHECK(checkOf(&r) == XG_PARTY_BAD_MEMBERS); /* and its name */
    memset(r.m25C_names[2], 0, XG_PARTY_NAME);
    CHECK(checkOf(&r) == XG_PARTY_OK);
    /* the command loadout is the id */
    CHECK(withRecord(1, F(m56_battleCommandLoadout), 0, 0) == XG_PARTY_OK);
    CHECK(withRecord(1, F(m56_battleCommandLoadout), 1, 0) == XG_PARTY_BAD_LOADOUT);
    CHECK(withRecord(1, F(m56_battleCommandLoadout), 19, 0) == XG_PARTY_BAD_LOADOUT);
    /* the Gear number: below 20, or 0xFF */
    CHECK(withRecord(1, F(mA0_gearNum), 19, 0) == XG_PARTY_OK);
    CHECK(withRecord(1, F(mA0_gearNum), 20, 0) == XG_PARTY_BAD_GEAR);
    CHECK(withRecord(1, F(mA0_gearNum), 0xFE, 0) == XG_PARTY_BAD_GEAR);
    CHECK(withRecord(1, F(mA0_gearNum), 0xFF, 0) == XG_PARTY_OK);
    /* Billy's loaded rounds, both: 0x32-0x49 (an id past 0xD1 would write past gameState) */
    for (int s = 0; s <= 3; s += 3) {
        CHECK(withRecord(0, F(m6F_equipment) + s, 0x31, 0) == XG_PARTY_BAD_LOADED_AMMO);
        CHECK(withRecord(0, F(m6F_equipment) + s, 0x32, 0) == XG_PARTY_OK);
        CHECK(withRecord(0, F(m6F_equipment) + s, 0x49, 0) == XG_PARTY_OK);
        CHECK(withRecord(0, F(m6F_equipment) + s, 0x4A, 0) == XG_PARTY_BAD_LOADED_AMMO);
        CHECK(withRecord(0, F(m6F_equipment) + s, 0xD1, 0) == XG_PARTY_BAD_LOADED_AMMO);
        CHECK(withRecord(0, F(m6F_equipment) + s, 0xD2, 0) == XG_PARTY_BAD_LOADED_AMMO);
        CHECK(withRecord(0, F(m6F_equipment) + s, 0xFF, 0) == XG_PARTY_BAD_LOADED_AMMO);
    }
    CHECK(withRecord(1, F(m6F_equipment), 0xFF, 0) == XG_PARTY_OK); /* only Billy's are rounds */
    /* HP <= max HP, 1 <= max HP <= 999 (Fei: 300 of 332) */
    CHECK(withRecord(1, F(m4C_HP), 332, 1) == XG_PARTY_OK);
    CHECK(withRecord(1, F(m4C_HP), 333, 1) == XG_PARTY_BAD_HP);
    CHECK(withRecord(1, F(m4C_HP), 0, 1) == XG_PARTY_OK);
    {
        XgPartyRecord m = good;
        put16(m.m10_characters[1] + F(m4C_HP), 999);
        put16(m.m10_characters[1] + F(m4E_MaxHP), 999);
        CHECK(checkOf(&m) == XG_PARTY_OK);
        put16(m.m10_characters[1] + F(m4E_MaxHP), 1000);
        CHECK(checkOf(&m) == XG_PARTY_BAD_HP);
        put16(m.m10_characters[1] + F(m4C_HP), 0);
        put16(m.m10_characters[1] + F(m4E_MaxHP), 1);
        CHECK(checkOf(&m) == XG_PARTY_OK);
        put16(m.m10_characters[1] + F(m4E_MaxHP), 0);
        CHECK(checkOf(&m) == XG_PARTY_BAD_HP);
        /* EP <= max EP <= 99 (Fei: 40 of 46) */
        m = good;
        put16(m.m10_characters[1] + F(m50_MP), 46);
        CHECK(checkOf(&m) == XG_PARTY_OK);
        put16(m.m10_characters[1] + F(m50_MP), 47);
        CHECK(checkOf(&m) == XG_PARTY_BAD_EP);
        put16(m.m10_characters[1] + F(m50_MP), 99);
        put16(m.m10_characters[1] + F(m52_MaxMP), 99);
        CHECK(checkOf(&m) == XG_PARTY_OK);
        put16(m.m10_characters[1] + F(m52_MaxMP), 100);
        CHECK(checkOf(&m) == XG_PARTY_BAD_EP);
        put16(m.m10_characters[1] + F(m50_MP), 0);
        put16(m.m10_characters[1] + F(m52_MaxMP), 0);
        CHECK(checkOf(&m) == XG_PARTY_OK);
    }
    /* levels 1-99, both */
    CHECK(withRecord(2, F(m62_Level), 1, 0) == XG_PARTY_OK);
    CHECK(withRecord(2, F(m62_Level), 0, 0) == XG_PARTY_BAD_LEVEL);
    CHECK(withRecord(2, F(m62_Level), 99, 0) == XG_PARTY_OK);
    CHECK(withRecord(2, F(m62_Level), 100, 0) == XG_PARTY_BAD_LEVEL);
    CHECK(withRecord(2, F(m63_Level2), 0, 0) == XG_PARTY_BAD_LEVEL);
    CHECK(withRecord(2, F(m63_Level2), 100, 0) == XG_PARTY_BAD_LEVEL);
    /* AP per turn 1-7 */
    CHECK(withAp(2, 1) == XG_PARTY_OK);
    /* experience to the next levels: 1 or more (battle_result's level-up loop) */
    CHECK(withRecord(2, F(m44_expToNextLevel), 1, 1) == XG_PARTY_OK);
    CHECK(withRecord(2, F(m44_expToNextLevel), 0, 1) == XG_PARTY_BAD_EXP);
    CHECK(withRecord(2, F(m48_expToNextLevel2), 0, 1) == XG_PARTY_BAD_EXP);
    {
        XgPartyRecord r = good;
        uint8_t* c = r.m10_characters[1];
        put32(c + F(m48_expToNextLevel2), 0x80000000u); /* negative */
        CHECK(checkOf(&r) == XG_PARTY_BAD_EXP);
        put32(c + F(m48_expToNextLevel2), 0x7FFFFFFFu);
        CHECK(checkOf(&r) == XG_PARTY_OK);
    }
    CHECK(withAp(2, 0) == XG_PARTY_BAD_AP);
    CHECK(withAp(2, 7) == XG_PARTY_OK);
    CHECK(withAp(2, 8) == XG_PARTY_BAD_AP);
    /* names */
    const uint8_t fei[] = {'F', 'e', 'i'};
    CHECK(withName(1, fei, 3) == XG_PARTY_OK);
    {
        XgPartyRecord m = good; /* a byte left in the tail */
        m.m25C_names[1][19] = 'x';
        CHECK(checkOf(&m) == XG_PARTY_BAD_NAME);
        m.m25C_names[1][19] = 0;
        m.m25C_names[1][4] = 0x10;
        CHECK(checkOf(&m) == XG_PARTY_BAD_NAME);
    }
    const uint8_t escape[] = {'F', 0x0F, 0x05, 0x01}; /* inserts member 1's name: in the table, still refused */
    CHECK(withName(1, escape, 4) == XG_PARTY_BAD_NAME);
    const uint8_t control[] = {'F', 0x01, 'i'};
    CHECK(withName(1, control, 3) == XG_PARTY_BAD_NAME);
    uint8_t full[XG_PARTY_NAME];
    memset(full, 'a', sizeof(full));
    CHECK(withName(1, full, XG_PARTY_NAME - 1) == XG_PARTY_OK); /* 19 glyphs and the terminator */
    CHECK(withName(1, full, XG_PARTY_NAME) == XG_PARTY_BAD_NAME); /* no terminator */
    const uint8_t empty[] = {0};
    CHECK(withName(1, empty, 1) == XG_PARTY_OK);
    const uint8_t lastSingle[] = {'F', (uint8_t)(lead - 1)};
    CHECK(withName(1, lastSingle, 2) == XG_PARTY_OK); /* the last single-byte glyph */
    const uint8_t pair[] = {'F', (uint8_t)lead, 0x05, 'i'}; /* a pair's second byte below 0x10 */
    CHECK(withName(1, pair, 4) == XG_PARTY_OK);
    const uint8_t pairUnknown[] = {'F', (uint8_t)lead, 0x06};
    CHECK(withName(1, pairUnknown, 3) == XG_PARTY_BAD_NAME);
    const uint8_t pairCut[] = {'F', (uint8_t)lead}; /* the pair would swallow the terminator */
    CHECK(withName(1, pairCut, 2) == XG_PARTY_BAD_NAME);
    uint8_t pairLast[XG_PARTY_NAME];
    memset(pairLast, 'a', sizeof(pairLast));
    pairLast[18] = (uint8_t)lead;
    pairLast[19] = 0x41;
    CHECK(withName(1, pairLast, XG_PARTY_NAME) == XG_PARTY_BAD_NAME); /* ends at the last byte: no terminator */
    codes[2 * 7 + 1] = 0; /* a glyph the table does not have */
    CHECK(withName(1, fei, 3) == XG_PARTY_OK);
    const uint8_t missing[] = {'F', (uint8_t)(0x0F + 7)};
    CHECK(withName(1, missing, 2) == XG_PARTY_BAD_NAME);
    makeCodes();
    /* items: ids 1-0x30, counts 1-99, no repeats, no gaps */
    r = good;
    r.m298_itemIds[2] = 0x31;
    CHECK(checkOf(&r) == XG_PARTY_BAD_ITEMS);
    r.m298_itemIds[2] = 0x30;
    r.m2C8_itemCounts[2] = 99;
    CHECK(checkOf(&r) == XG_PARTY_OK);
    r.m2C8_itemCounts[2] = 100;
    CHECK(checkOf(&r) == XG_PARTY_BAD_ITEMS);
    r.m2C8_itemCounts[2] = 0;
    CHECK(checkOf(&r) == XG_PARTY_BAD_ITEMS);
    r = good;
    r.m298_itemIds[2] = 3; /* a repeat */
    CHECK(checkOf(&r) == XG_PARTY_BAD_ITEMS);
    r = good;
    r.m298_itemIds[5] = 4; /* after the end: a gap */
    r.m2C8_itemCounts[5] = 1;
    CHECK(checkOf(&r) == XG_PARTY_BAD_ITEMS);
    r = good;
    r.m2C8_itemCounts[5] = 1; /* a count without an id */
    CHECK(checkOf(&r) == XG_PARTY_BAD_ITEMS);
    r = good;
    for (int i = 0; i < XG_PARTY_ITEMS; i++) {
        r.m298_itemIds[i] = (uint8_t)(i + 1);
        r.m2C8_itemCounts[i] = 1;
    }
    CHECK(checkOf(&r) == XG_PARTY_OK); /* all 0x30 */
    memset(r.m298_itemIds, 0, sizeof(r.m298_itemIds));
    memset(r.m2C8_itemCounts, 0, sizeof(r.m2C8_itemCounts));
    CHECK(checkOf(&r) == XG_PARTY_OK); /* none */
    /* ammo: ids 0x32-0x48, counts 1-99 */
    r = good;
    r.m2F8_ammoIds[1] = 0x48;
    CHECK(checkOf(&r) == XG_PARTY_OK);
    r.m2F8_ammoIds[1] = 0x49;
    CHECK(checkOf(&r) == XG_PARTY_BAD_AMMO);
    r.m2F8_ammoIds[1] = 0x32;
    CHECK(checkOf(&r) == XG_PARTY_OK);
    r.m2F8_ammoIds[1] = 0x31;
    CHECK(checkOf(&r) == XG_PARTY_BAD_AMMO);
    r = good;
    r.m328_ammoCounts[1] = 100;
    CHECK(checkOf(&r) == XG_PARTY_BAD_AMMO);
    r.m328_ammoCounts[1] = 0;
    CHECK(checkOf(&r) == XG_PARTY_BAD_AMMO);
    r = good;
    r.m2F8_ammoIds[1] = 0x35; /* a repeat */
    CHECK(checkOf(&r) == XG_PARTY_BAD_AMMO);
    r = good;
    r.m2F8_ammoIds[3] = 0x40; /* a gap */
    r.m328_ammoCounts[3] = 1;
    CHECK(checkOf(&r) == XG_PARTY_BAD_AMMO);
    /* the ammo-count table: 0-100 */
    r = good;
    r.m358_ammoTable[0x5F] = 100;
    CHECK(checkOf(&r) == XG_PARTY_OK);
    r.m358_ammoTable[0x5F] = 101;
    CHECK(checkOf(&r) == XG_PARTY_BAD_AMMO_TABLE);
    r.m358_ammoTable[0x5F] = 0xFF;
    CHECK(checkOf(&r) == XG_PARTY_BAD_AMMO_TABLE);
    /* the reason names the member's place */
    r = good;
    r.m1FC_abilities[2][offsetof(GameStateSize20, m17_energyPerTurn)] = 9;
    CHECK(xgPartyCheck(&r, codes, &why) == XG_PARTY_BAD_AP && why.member == 2 && why.text[0] != '\0');
}

/* ---- by hand: the saves of real card images (read only) --------------- */

static int show(int argc, char** argv) {
    const char* codesFile = NULL;
    int at = 1;
    if (argc > 2 && !strcmp(argv[1], "--codes")) {
        codesFile = argv[2];
        at = 3;
    }
    int haveCodes = codesFile != NULL && cardLoadFile(codesFile, codes, sizeof(codes));
    if (codesFile != NULL && !haveCodes) {
        printf("card_savestest: %s is not a %d-byte code table\n", codesFile, XG_PARTY_CODE_TABLE);
        return 1;
    }
    for (int c = 0; c < 2; c++) {
        cards[c].present = at + c < argc && cardLoadFile(argv[at + c], cards[c].data, CARD_SIZE);
        if (at + c < argc && !cards[c].present) printf("card %d: %s could not be read\n", c + 1, argv[at + c]);
    }
    CardGameSave list[30];
    int all = cardGameSaves(list, 30, 0);
    int n = cardGameSaves(list, 30, cardGameRegion());
    printf("%d save(s) of this region ('%c'), %d of both\n", n, cardGameRegion(), all);
    for (int i = 0; i < n; i++) {
        const CardGameSave* s = &list[i];
        unsigned t = s->playTime / 60;
        printf("card %d block %2d  No.%02d %-20s story %3d  disc %d  %u:%02u:%02u  id %s\n", s->card + 1, s->block,
               s->number, s->location, s->progress, s->disc, t / 3600, t / 60 % 60, t % 60, s->id ? "yes" : "none");
        printf("   ");
        for (int c = 0; c < CARD_SAVE_MEMBERS; c++) {
            if (s->joined & 1 << c) {
                printf(" %s %d (HP %d/%d)", names[c], s->members[c].level, s->members[c].hp, s->members[c].maxHp);
            }
        }
        printf("\n");
        if (haveCodes) {
            XgPartyRecord r;
            XgPartyWhy why;
            int reason = xgPartyPack(&r, cardGameSaveBlock(s), s->party, codes, &why);
            printf("    party %d,%d,%d packed: %s%s\n", s->party[0], s->party[1], s->party[2],
                   reason == XG_PARTY_OK ? "ok" : "refused, ", why.text);
            for (int k = 0; reason == XG_PARTY_OK && k < r.m3_count; k++) {
                printf("      %s:", names[r.m4_members[k]]);
                for (int j = 0; j < XG_PARTY_NAME && r.m25C_names[k][j]; j++) printf(" %02x", r.m25C_names[k][j]);
                printf("\n");
            }
            printf("    each joined member alone:");
            for (int c = 0; c < CARD_SAVE_MEMBERS; c++) {
                int alone[3] = {c, 255, 255};
                if (!(s->joined & 1 << c)) continue;
                reason = xgPartyPack(&r, cardGameSaveBlock(s), alone, codes, &why);
                printf(" %s %s", names[c], reason == XG_PARTY_OK ? "ok" : why.text);
            }
            printf("\n");
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    makeCodes();
    if (argc > 1) return show(argc, argv);
    testReader();
    testPack();
    printf(failures ? "card_savestest: %d failure(s)\n" : "card_savestest: ok\n", failures);
    return failures != 0;
}
