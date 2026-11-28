#ifndef XG_ARENA_ROSTER_H
#define XG_ARENA_ROSTER_H

#include "../card/party_record.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARENA_ROSTER_SLOTS 8
#define ARENA_ROSTER_STATS 6
#define ARENA_ROSTER_ITEMS 0x30 /* battle item IDs 1–0x30, indexed by ID minus one */
#define ARENA_LOADOUT_CODE 472 /* eight individually scaled enemies, assigned Gears and both inventories */
#define ARENA_ROSTER_PACKET (ARENA_LOADOUT_CODE + 2 * XG_PARTY_BYTES)

typedef struct {
    unsigned char bank, type;
} ArenaEnemy;

typedef struct {
    ArenaEnemy enemy[ARENA_ROSTER_SLOTS];
    ArenaEnemy gear[ARENA_ROSTER_SLOTS];
    unsigned short multiplier[ARENA_ROSTER_SLOTS][ARENA_ROSTER_STATS];
    unsigned char count, gearSlots;
    unsigned char items[ARENA_ROSTER_ITEMS];
} ArenaEnemyTeam;

typedef struct {
    ArenaEnemyTeam side[2]; /* enemies on each side */
    unsigned char stage;
    unsigned char callGear;
    unsigned char startInGear, allowExitGear;
    unsigned char mixed; /* both sides bring saved characters as well */
} ArenaLoadout;

typedef struct {
    ArenaLoadout loadout;
    int party[3];
    int fromSave;
    XgPartyRecord record;
    XgPartyRecord secondRecord;
} ArenaLoadoutChoice;

typedef struct {
    ArenaLoadoutChoice current, approved[2];
    unsigned char ready;
} ArenaLoadoutReview;

void pcArenaLoadoutChanged(ArenaLoadoutReview* review);
int pcArenaLoadoutReady(const ArenaLoadoutReview* review);
int pcArenaLoadoutApprove(ArenaLoadoutReview* review, int player, int approve);
int pcArenaLoadoutEncode(const ArenaLoadout* loadout, const int party[3], char code[ARENA_LOADOUT_CODE]);
int pcArenaLoadoutDecode(const char* code, ArenaLoadout* loadout, int party[3]);
int pcArenaRosterPack(const char* code, const XgPartyRecord* party, const XgPartyRecord* second,
                      unsigned char packet[ARENA_ROSTER_PACKET]);
int pcArenaRosterUnpack(const unsigned char* packet, int bytes, const unsigned char* codes,
                        char code[ARENA_LOADOUT_CODE], XgPartyRecord* record, XgPartyRecord* second);
int pcArenaRosterCallGear(void);
int pcArenaRosterExitGear(void);
int pcArenaRosterOnline(const ArenaLoadout* loadout, const char* disc, const char** why);
int pcArenaRosterMixedOnline(const ArenaLoadout* loadout, const XgPartyRecord* first, const XgPartyRecord* second,
                            const char* disc, const char** why);
const XgPartyRecord* pcArenaRosterSecondParty(void);
int pcArenaRosterCheckMixed(const ArenaLoadout* loadout, const XgPartyRecord* first, const XgPartyRecord* second,
                           const unsigned char* codes);
int pcArenaRosterStart(const ArenaLoadoutReview* review, const char** why);
const unsigned char* pcArenaRosterItemCounts(int side);
void pcArenaRosterUseItem(int side, int item);
const void* pcArenaRosterFootSheet(int slot);

#ifdef __cplusplus
}
#endif
#endif
