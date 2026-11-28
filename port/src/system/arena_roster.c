/* The approved arena loadout and its native battle resources. The original
 * enemy banks keep their own scripts, tables and strings. Only the visual
 * container is repacked, so it can use the game's shared sprite loader. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "arena_roster.h"
#include "cd/cd.h"
#include "extras/disc.h"
#include "extras/sprites_enemy.h"
#include "pc_arena.h"

typedef struct {
    ArenaEnemy enemy;
    uint8_t* bank;
    uint8_t* visuals;
    int bankSize, visualSize;
    XgEnemySprite sprite;
} RosterType;

typedef struct {
    RosterType type;
    int column, position, control, model;
    int replacesFootSheet;
} RosterGear;

typedef struct {
    ArenaLoadout loadout;
    RosterType types[ARENA_ROSTER_SLOTS];
    RosterGear gear[ARENA_ROSTER_SLOTS];
    uint8_t* visuals;
    int visualBytes;
    int typeCount;
    unsigned char ids[ARENA_ROSTER_SLOTS];
    unsigned char split, count, activeGears, online;
    unsigned char items[2][ARENA_ROSTER_ITEMS];
} RosterAssets;

static RosterAssets roster;
extern int netplayActive(void) __attribute__((weak));

/* The asset reader belongs to Extras. Minimal builds keep the stock arena. */
extern XgDisc* xgDiscOpen(const char*, const char**) __attribute__((weak));
extern void xgDiscClose(XgDisc*) __attribute__((weak));
extern int xgDiscFileEntry(const XgDisc*, int, int) __attribute__((weak));
extern uint8_t* xgDiscReadEntry(XgDisc*, int, int*) __attribute__((weak));
extern int xgEnemySpriteGet(const uint8_t*, int, int, XgEnemySprite*) __attribute__((weak));

static unsigned u16le(const uint8_t* p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned u32le(const uint8_t* p) { return u16le(p) | u16le(p + 2) << 16; }

static void put32(uint8_t* p, unsigned value) {
    p[0] = value;
    p[1] = value >> 8;
    p[2] = value >> 16;
    p[3] = value >> 24;
}

static void freeAssets(RosterAssets* a) {
    for (int i = 0; i < a->typeCount; i++) {
        free(a->types[i].bank);
        free(a->types[i].visuals);
    }
    for (int i = 0; i < ARENA_ROSTER_SLOTS; i++) {
        free(a->gear[i].type.bank);
        free(a->gear[i].type.visuals);
    }
    free(a->visuals);
    memset(a, 0, sizeof(*a));
}

void pcArenaRosterClear(void) {
    freeAssets(&roster);
}

static int refused(const char** why, const char* message) {
    if (why != NULL) *why = message;
    return 0;
}

static int readType(XgDisc* disc, ArenaEnemy id, RosterType* type, int gear) {
    int bankEntry = xgDiscFileEntry(disc, 0x0D, 2 + 2 * id.bank);
    int visualEntry = xgDiscFileEntry(disc, 0x0D, 3 + 2 * id.bank);

    if (id.type >= 8 || bankEntry < 0 || visualEntry < 0) return 0;
    type->enemy = id;
    type->bank = xgDiscReadEntry(disc, bankEntry, &type->bankSize);
    type->visuals = xgDiscReadEntry(disc, visualEntry, &type->visualSize);
    if (type->bank == NULL || type->bankSize < 0x32 + (id.type + 1) * 0x170 ||
        xgEnemySpriteGet(type->visuals, type->visualSize, id.type, &type->sprite) != (gear ? -1 : 1)) return 0;

    const uint8_t* b = type->bank;
    unsigned strings = u16le(b + 0x30);
    unsigned script = u16le(b + 2 * id.type);
    const uint8_t* record = b + 0x32 + id.type * 0x170;
    return strings + 4 <= (unsigned)type->bankSize && script + 8 <= strings &&
           script >= (unsigned)(0x32 + (id.type + 1) * 0x170) &&
           (gear ? u32le(record + 0xA4 + 0x64) != 0 : u16le(record + 0x4e) != 0);
}

static int addType(RosterAssets* a, XgDisc* disc, ArenaEnemy id) {
    for (int i = 0; i < a->typeCount; i++) {
        if (a->types[i].enemy.bank == id.bank && a->types[i].enemy.type == id.type) return i;
    }
    int i = a->typeCount++;
    return readType(disc, id, &a->types[i], 0) ? i : -1;
}

static int readGear(XgDisc* disc, ArenaEnemy id, RosterGear* gear) {
    if (!readType(disc, id, &gear->type, 1)) return 0;
    const uint8_t* raw = gear->type.visuals;
    const uint8_t* entry = raw + 8 + 12 * id.type;
    unsigned controlEnd = u32le(raw + 4);
    unsigned control = u32le(entry);
    unsigned model = u32le(entry + 4);
    if (model < 8) {
        if (model >= id.type) return 0;
        entry = raw + 8 + 12 * model;
        if (entry[8] == 0) return 0;
        control = u32le(entry);
        model = u32le(entry + 4);
    }
    if (control < 8 + 12u * raw[0] || control >= controlEnd ||
        model < controlEnd || model >= (unsigned)gear->type.visualSize) return 0;
    gear->control = control;
    gear->model = model;
    return 1;
}

static int packVisuals(RosterAssets* a, const char** why) {
    int header = 8 + 12 * a->typeCount;
    int controls = header;
    int sheets = 0;
    int columns = 0;
    int source[ARENA_ROSTER_SLOTS], sheetId[ARENA_ROSTER_SLOTS], sheetColumn[ARENA_ROSTER_SLOTS];
    int spriteCount = 0;
    int instances[ARENA_ROSTER_SLOTS] = {0};
    for (int i = 0; i < a->typeCount; i++) {
        const RosterType* type = &a->types[i];
        const uint8_t* sheet = type->visuals + type->sprite.sheetOffset;
        controls += type->sprite.bundleSize;
        source[i] = i;
        for (int j = 0; j < i; j++) {
            const RosterType* previous = &a->types[j];
            if (previous->sprite.sheetSize == type->sprite.sheetSize &&
                memcmp(sheet, previous->visuals + previous->sprite.sheetOffset, (size_t)type->sprite.sheetSize) == 0) {
                source[i] = j;
                break;
            }
        }
        if (source[i] != i) {
            sheetId[i] = sheetId[source[i]];
            continue;
        }
        sheetId[i] = spriteCount++;
        sheetColumn[sheetId[i]] = columns;
        sheets += type->sprite.sheetSize;
        columns += u32le(sheet);
    }
    for (int i = 0; i < a->count; i++) instances[sheetId[a->ids[i]]]++;
    for (int i = 0; i < a->count; i++) {
        if (a->gear[i].type.bank == NULL) continue;
        /* A solo pilot frees its sprite strip as soon as it calls. Copies
         * still on foot need the original strip until they leave the fight. */
        int id = sheetId[a->ids[i]];
        a->gear[i].replacesFootSheet = instances[id] == 1;
        a->gear[i].column = instances[id] == 1 ? sheetColumn[id] : columns++;
    }
    if (columns > 7) return refused(why, "The combined sprites and Gear slots exceed this arena's texture space.");
    a->visualBytes = controls + sheets;
    a->visuals = calloc(1, (size_t)a->visualBytes);
    if (a->visuals == NULL) return refused(why, "The enemy visuals could not be allocated.");
    a->visuals[0] = a->typeCount;
    put32(a->visuals + 4, (unsigned)controls);
    int controlAt = header;
    int sheetAt = controls;
    for (int i = 0; i < a->typeCount; i++) {
        const RosterType* type = &a->types[i];
        const XgEnemySprite* s = &type->sprite;
        uint8_t* entry = a->visuals + 8 + 12 * i;
        put32(entry, (unsigned)controlAt);
        put32(entry + 4, source[i] == i ? (unsigned)sheetAt : (unsigned)sheetId[i]);
        entry[11] = s->palette;
        memcpy(a->visuals + controlAt, type->visuals + s->bundleOffset, (size_t)s->bundleSize);
        controlAt += s->bundleSize;
        if (source[i] == i) {
            memcpy(a->visuals + sheetAt, type->visuals + s->sheetOffset, (size_t)s->sheetSize);
            sheetAt += s->sheetSize;
        }
    }
    return 1;
}

static int prepare(RosterAssets* a, const ArenaLoadout* loadout, const char* path, const char** why) {
    int total = loadout->side[0].count + loadout->side[1].count;
    if (loadout->side[0].count > 3 || loadout->side[1].count == 0 || total > ARENA_ROSTER_SLOTS ||
        loadout->side[1].count > 5) return refused(why, "Choose up to three enemies for Player 1 and one to five for Player 2.");
    int validStage = 0;
    for (int i = 0; i < pcArenaStageCount(); i++) {
        validStage |= pcArenaStageId(i) == loadout->stage;
    }
    if (!validStage) return refused(why, "Choose an arena.");
    for (int side = 0; side < 2; side++) {
        for (int i = 0; i < ARENA_ROSTER_STATS; i++) {
            int value = loadout->side[side].multiplier[i];
            if (value < 25 || value > 400) return refused(why, "Enemy stat multipliers must be between 25 and 400 percent.");
        }
    }
    if (xgDiscOpen == NULL || xgEnemySpriteGet == NULL) return refused(why, "The roster uses the Extras disc reader.");
    XgDisc* disc = path == NULL ? NULL : xgDiscOpen(path, NULL);
    if (disc == NULL) return refused(why, "The current Xenogears disc could not be read.");
    a->loadout = *loadout;
    a->split = loadout->side[0].count;
    if (a->split != 0) {
        a->loadout.callGear = a->loadout.startInGear = a->loadout.allowExitGear = 0;
    }
    for (int side = 0; side < 2; side++) {
        for (int i = 0; i < loadout->side[side].count; i++) {
            int id = addType(a, disc, loadout->side[side].enemy[i]);
            if (id < 0) {
                xgDiscClose(disc);
                return refused(why, "An enemy has no compatible on-foot battle record.");
            }
            RosterGear* gear = &a->gear[a->count];
            if ((a->loadout.callGear || a->loadout.startInGear) && (loadout->side[side].gearSlots & (1 << i))) {
                if (!readGear(disc, loadout->side[side].gear[i], gear)) {
                    xgDiscClose(disc);
                    return refused(why, "An assigned Gear has no compatible battle record.");
                }
                gear->position = i;
            }
            a->ids[a->count++] = id;
        }
    }
    xgDiscClose(disc);
    return packVisuals(a, why);
}

void pcArenaLoadoutChanged(ArenaLoadoutReview* review) { review->ready = 0; }

int pcArenaLoadoutReady(const ArenaLoadoutReview* review) {
    if (review->ready != 3) return 0;
    return memcmp(&review->current, &review->approved[0], sizeof(review->current)) == 0 &&
           memcmp(&review->current, &review->approved[1], sizeof(review->current)) == 0;
}

int pcArenaLoadoutApprove(ArenaLoadoutReview* review, int player, int approve) {
    if (player < 0 || player > 1) return 0;
    unsigned mask = 1u << player;
    if (approve) {
        memcpy(&review->approved[player], &review->current, sizeof(review->current));
        review->ready |= mask;
    } else {
        review->ready &= ~mask;
    }
    return pcArenaLoadoutReady(review);
}

static int validLoadout(const ArenaLoadout* loadout, const int party[3]) {
    int members = 0;
    if (loadout->side[0].count > 3 || loadout->side[1].count == 0 || loadout->side[1].count > 5 ||
        loadout->stage >= 75 || loadout->callGear > 1 || party[0] == 255) return 0;
    if (loadout->startInGear > 1 || loadout->allowExitGear > 1) return 0;
    for (int i = 0; i < 3; i++) {
        if (party[i] == 255) continue;
        if (party[i] < 0 || party[i] > 10 || (members & (1 << party[i]))) return 0;
        members |= 1 << party[i];
    }
    for (int side = 0; side < 2; side++) {
        const ArenaEnemyTeam* team = &loadout->side[side];
        if (team->gearSlots >= (1u << team->count)) return 0;
        for (int i = 0; i < ARENA_ROSTER_ITEMS; i++) {
            if (team->items[i] > 99 || (team->count == 0 && team->items[i] != 0)) return 0;
        }
        for (int i = 0; i < ARENA_ROSTER_STATS; i++) {
            if (team->multiplier[i] < 25 || team->multiplier[i] > 400) return 0;
        }
        for (int i = 0; i < team->count; i++) {
            if (team->enemy[i].type >= 8 || ((team->gearSlots & (1 << i)) && team->gear[i].type >= 8)) return 0;
        }
    }
    return 1;
}

int pcArenaLoadoutEncode(const ArenaLoadout* loadout, const int party[3], char code[ARENA_LOADOUT_CODE]) {
    static const char hex[] = "0123456789abcdef";
    uint8_t bytes[ARENA_LOADOUT_CODE / 2];
    int n = 0;
    if (!validLoadout(loadout, party)) return 0;
    unsigned flags = loadout->startInGear | (loadout->allowExitGear << 1);
    for (int side = 0; side < 2; side++) {
        for (int i = 0; i < ARENA_ROSTER_ITEMS; i++) {
            if (loadout->side[side].items[i] != 0) flags |= 4u << side;
        }
    }
    int usesAssignedGears = loadout->callGear || loadout->startInGear;
    bytes[n++] = flags ? 2 : 1;
    bytes[n++] = loadout->stage;
    bytes[n++] = loadout->callGear;
    for (int i = 0; i < 3; i++) bytes[n++] = party[i];
    for (int side = 0; side < 2; side++) {
        const ArenaEnemyTeam* team = &loadout->side[side];
        bytes[n++] = team->count;
        bytes[n++] = usesAssignedGears ? team->gearSlots : 0;
        for (int i = 0; i < ARENA_ROSTER_STATS; i++) {
            bytes[n++] = team->multiplier[i] & 0xff;
            bytes[n++] = team->multiplier[i] >> 8;
        }
        for (int i = 0; i < team->count; i++) {
            bytes[n++] = team->enemy[i].bank;
            bytes[n++] = team->enemy[i].type;
            if (usesAssignedGears && (team->gearSlots & (1 << i))) {
                bytes[n++] = team->gear[i].bank;
                bytes[n++] = team->gear[i].type;
            }
        }
    }
    if (flags) {
        bytes[n++] = flags;
        for (int side = 0; side < 2; side++) {
            if (!(flags & (4u << side))) continue;
            memcpy(bytes + n, loadout->side[side].items, ARENA_ROSTER_ITEMS);
            n += ARENA_ROSTER_ITEMS;
        }
    }
    for (int i = 0; i < n; i++) {
        code[2 * i] = hex[bytes[i] >> 4];
        code[2 * i + 1] = hex[bytes[i] & 15];
    }
    code[n * 2] = 0;
    return n * 2;
}

static int hexDigit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

int pcArenaLoadoutDecode(const char* code, ArenaLoadout* loadout, int party[3]) {
    uint8_t bytes[ARENA_LOADOUT_CODE / 2];
    int length = strlen(code);
    int at = 0;
    ArenaLoadout decoded = {0};
    if (length < 12 || length >= ARENA_LOADOUT_CODE || (length & 1)) return 0;
    length /= 2;
    for (int i = 0; i < length; i++) {
        int high = hexDigit(code[i * 2]), low = hexDigit(code[i * 2 + 1]);
        if (high < 0 || low < 0) return 0;
        bytes[i] = high * 16 + low;
    }
    unsigned version = bytes[at++];
    if (version != 1 && version != 2) return 0;
    decoded.stage = bytes[at++];
    decoded.callGear = bytes[at++];
    for (int i = 0; i < 3; i++) party[i] = bytes[at++];
    for (int side = 0; side < 2; side++) {
        ArenaEnemyTeam* team = &decoded.side[side];
        if (at + 2 + 2 * ARENA_ROSTER_STATS > length) return 0;
        team->count = bytes[at++];
        team->gearSlots = bytes[at++];
        if (team->count > ARENA_ROSTER_SLOTS) return 0;
        for (int i = 0; i < ARENA_ROSTER_STATS; i++, at += 2) team->multiplier[i] = u16le(bytes + at);
        for (int i = 0; i < team->count; i++) {
            if (at + 2 > length) return 0;
            team->enemy[i] = (ArenaEnemy){bytes[at], bytes[at + 1]};
            at += 2;
            if (team->gearSlots & (1 << i)) {
                if (at + 2 > length) return 0;
                team->gear[i] = (ArenaEnemy){bytes[at], bytes[at + 1]};
                at += 2;
            }
        }
    }
    if (version == 2) {
        if (at == length) return 0;
        unsigned flags = bytes[at++];
        if (flags & ~15u) return 0;
        decoded.startInGear = flags & 1;
        decoded.allowExitGear = (flags >> 1) & 1;
        for (int side = 0; side < 2; side++) {
            if (!(flags & (4u << side))) continue;
            if (at + ARENA_ROSTER_ITEMS > length) return 0;
            memcpy(decoded.side[side].items, bytes + at, ARENA_ROSTER_ITEMS);
            at += ARENA_ROSTER_ITEMS;
        }
    }
    if (!decoded.callGear && !decoded.startInGear && (decoded.side[0].gearSlots || decoded.side[1].gearSlots)) return 0;
    if (at != length || !validLoadout(&decoded, party)) return 0;
    *loadout = decoded;
    return 1;
}

int pcArenaRosterPack(const char* code, const XgPartyRecord* party, unsigned char packet[ARENA_ROSTER_PACKET]) {
    size_t bytes = strlen(code) + 1;
    if (bytes > ARENA_LOADOUT_CODE) return 0;
    memcpy(packet, code, bytes);
    if (party != NULL) {
        memcpy(packet + bytes, party, sizeof *party);
        bytes += sizeof *party;
    }
    return (int)bytes;
}

int pcArenaRosterUnpack(const unsigned char* packet, int bytes, const unsigned char* codes,
                        char code[ARENA_LOADOUT_CODE], XgPartyRecord* record) {
    if (bytes <= 0 || bytes > ARENA_ROSTER_PACKET) return 0;
    const unsigned char* end = memchr(packet, 0, (size_t)bytes);
    if (end == NULL || end - packet >= ARENA_LOADOUT_CODE) return 0;
    int textBytes = (int)(end - packet) + 1;
    int recordBytes = bytes - textBytes;
    ArenaLoadout roster;
    int party[3];
    memcpy(code, packet, (size_t)textBytes);
    if (!pcArenaLoadoutDecode(code, &roster, party)) return 0;
    memset(record, 0, sizeof *record);
    if (roster.side[0].count != 0) return recordBytes == 0;
    if (recordBytes != sizeof *record) return 0;
    memcpy(record, end + 1, sizeof *record);
    if (xgPartyCheck(record, codes, NULL) != XG_PARTY_OK) return 0;
    for (int i = 0; i < 3; i++) {
        if (party[i] != record->m4_members[i]) return 0;
    }
    return 1;
}

int pcArenaRosterOnline(const ArenaLoadout* loadout, const char* disc, const char** why) {
    RosterAssets next = {0};
    if (!prepare(&next, loadout, disc, why)) {
        freeAssets(&next);
        return 0;
    }
    next.online = 1;
    freeAssets(&roster);
    roster = next;
    return 1;
}

int pcArenaRosterStart(const ArenaLoadoutReview* review, const char** why) {
    if (!pcArenaLoadoutReady(review)) return refused(why, "Both players must approve the loadout.");
    if (!pcArenaMatchAvailable(why)) return 0;
    const ArenaLoadoutChoice* chosen = &review->current;
    RosterAssets next = {0};
    if (!prepare(&next, &chosen->loadout, cdSwapPath(0), why)) {
        freeAssets(&next);
        return 0;
    }
    freeAssets(&roster);
    roster = next;
    int started = chosen->record.m3_count != 0
                      ? pcArenaMatchStartSaved(ARENA_MATCH_ROSTER, 2, 13, &chosen->record)
                      : pcArenaMatchStart(ARENA_MATCH_ROSTER, 2, 13, chosen->party, chosen->fromSave);
    if (!started) {
        pcArenaRosterClear();
        return refused(why, "The match could not start.");
    }
    return 1;
}

int pcArenaRosterActive(void) {
    return roster.count != 0 && (roster.online ? netplayActive != NULL && netplayActive()
                                               : pcArenaMatchKind() == ARENA_MATCH_ROSTER);
}

int pcArenaRosterSide(int entity) {
    if (entity < 3) return 0;
    return !pcArenaRosterActive() || entity - 3 >= roster.split;
}

int pcArenaRosterEnemyTeams(void) { return pcArenaRosterActive() && roster.split != 0; }

int pcArenaRosterConfig(unsigned char* config) {
    if (!pcArenaRosterActive()) return 0;
    unsigned char party[3], places[3];
    pcVersusStage(party, places);
    unsigned char position = places[0] & 0x7F;
    roster.activeGears = 0;
    for (int side = 0; side < 2; side++) {
        memcpy(roster.items[side], roster.loadout.side[side].items, ARENA_ROSTER_ITEMS);
    }
    config[1] = (config[1] & ~0x60) | (roster.loadout.callGear || roster.loadout.startInGear ? 0x80 : 0xC0);
    config[2] = roster.loadout.stage;
    config[3] = 0;
    memcpy(config + 4, party, 3);
    for (int i = 0; i < ARENA_ROSTER_SLOTS; i++) {
        config[8+i] = i < roster.count ? roster.ids[i] : 0x7F;
        config[16+i] = 0;
        config[24+i] = i < roster.split ? (party[0] & 0x7F) | 0x80 : position;
    }
    return 1;
}

int pcArenaRosterStage(void) { return pcArenaRosterActive() ? roster.loadout.stage + 1 : 0; }

const unsigned char* pcArenaRosterBank(int slot, int* bank, int* type) {
    if (!pcArenaRosterActive() || slot < 3 || slot >= roster.count + 3) return NULL;
    const RosterType* entry = roster.activeGears & (1 << (slot - 3)) ? &roster.gear[slot - 3].type
                                                                    : &roster.types[roster.ids[slot - 3]];
    *bank = entry->enemy.bank;
    *type = entry->enemy.type;
    return entry->bank;
}

const unsigned char* pcArenaRosterVisuals(int* bytes) {
    if (!pcArenaRosterActive()) return NULL;
    *bytes = roster.visualBytes;
    return roster.visuals;
}

int pcArenaRosterMultiplier(int slot, int stat) {
    if (!pcArenaRosterActive() || slot < 3 || slot >= roster.count + 3 || stat < 0 || stat >= ARENA_ROSTER_STATS) return 0;
    return roster.loadout.side[pcArenaRosterSide(slot)].multiplier[stat];
}

int pcArenaRosterGear(int slot, const unsigned char** controls, const unsigned char** model, int* x, int* position) {
    if (!pcArenaRosterActive() || slot < 3 || slot >= roster.count + 3 ||
        (roster.activeGears & (1 << (slot - 3)))) return 0;
    const RosterGear* gear = &roster.gear[slot - 3];
    if (gear->type.bank == NULL) return 0;
    if (controls != NULL) *controls = gear->type.visuals + gear->control;
    if (model != NULL) *model = gear->type.visuals + gear->model;
    if (x != NULL) *x = 0x140 + gear->column * 64;
    if (position != NULL) *position = gear->position;
    return 1;
}

void pcArenaRosterGearActivated(int slot) { roster.activeGears |= 1 << (slot - 3); }

void pcArenaRosterGearDeactivated(int slot) { roster.activeGears &= ~(1 << (slot - 3)); }

const void* pcArenaRosterFootSheet(int slot) {
    int i = slot - 3;
    if (!roster.gear[i].replacesFootSheet) return NULL;
    const RosterType* foot = &roster.types[roster.ids[i]];
    return foot->visuals + foot->sprite.sheetOffset;
}

int pcArenaRosterCallGear(void) { return pcArenaRosterActive() && roster.loadout.callGear; }

int pcArenaRosterExitGear(void) { return pcArenaRosterActive() && roster.loadout.allowExitGear; }

int pcArenaRosterStartInGear(void) { return pcArenaRosterActive() && roster.loadout.startInGear; }

int pcArenaRosterCanExitGear(int slot) {
    return pcArenaRosterExitGear() && slot >= 3 && slot < roster.count + 3 &&
           (roster.activeGears & (1 << (slot - 3))) != 0;
}

const unsigned char* pcArenaRosterItemCounts(int side) {
    return pcArenaRosterActive() ? roster.items[side] : NULL;
}

void pcArenaRosterUseItem(int side, int item) {
    unsigned char* count = &roster.items[side][item - 1];
    if (*count) --*count;
}
