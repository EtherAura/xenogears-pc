#ifndef PORT_GAUNTLET_H
#define PORT_GAUNTLET_H

/*
 * Gauntlets (xg-htn.35.17): F1 > Gameplay > Gauntlet runs the story's
 * bosses one after another, then puts the game back as it was.
 *
 *   - the bosses: the battles the field scripts start (ops 71, FE 84) with an
 *     enemy no random battle has, on both discs (port/src/extras/
 *     gauntlet_bosses.c, through the bestiary's readers); a Gear battle when
 *     an enemy is a Gear or the battle puts the party in Gears;
 *   - presets: every boss in story order (the bestiary's estimate: the
 *     lowest story progress the boss's field, or a field near it, sets),
 *     the character bosses, the Gear bosses; or a custom list and order;
 *   - the player picks the party (up to three of the members with them now)
 *     for each fight; a Gear fight takes only members with a Gear and is
 *     skipped when none is left;
 *   - recovery between fights: full (HP, EP, Gear HP and fuel), none (as
 *     the battle left them), 25% or 50% of each maximum, or revive only (a
 *     member knocked out comes back at 10% HP); items used stay used for the
 *     rest of the run, nothing is won: each fight starts from the state
 *     before the run with the HP and items carried over;
 *   - a lost fight: the Retry / Game over choice (port/src/game/battle/
 *     retryprompt.c); Game over ends the run;
 *   - the end (all cleared, Game over, an escape, or "Quit after this
 *     fight"): everything is put back as before the first fight (the
 *     memory checkpoint of port/src/game/main/gauntlet.c) and the game goes
 *     back to the field or world map; nothing touches a memory card;
 *   - records: each cleared gauntlet in gauntlets.json beside settings.ini
 *     (XG_GAUNTLET_RECORDS=file another file; none with XG_SETTINGS=none).
 *
 * Settings (settings.ini, XG_<KEY>):
 *   gauntlet_preset    story (default), character, gear or custom
 *   gauntlet_recovery  full (default), none, 25, 50 or revive
 *   gauntlet_custom    the custom list: boss keys "D-FFF-C" (disc, field,
 *                      config) separated by commas
 *   gauntlet_parties   the parties picked: "D-FFF-C=a.b.c" separated by
 *                      semicolons (character ids, x for none)
 * For headless runs: XG_GAUNTLET_AUTOSTART=<vsync> starts the selected
 * gauntlet (XG_GAUNTLET_MAX=n: its first n fights) once that vsync has
 * passed, with the parties above.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define PC_GAUNTLET_MAX 160
#define PC_GAUNTLET_TABLE 0x200 /* a field's 16 battle configs (0x20 bytes each) */

enum { PC_GAUNTLET_FULL, PC_GAUNTLET_NONE, PC_GAUNTLET_25, PC_GAUNTLET_50, PC_GAUNTLET_REVIVE, PC_GAUNTLET_RECOVERIES };
enum { PC_GAUNTLET_STORY, PC_GAUNTLET_CHARACTER, PC_GAUNTLET_GEAR, PC_GAUNTLET_CUSTOM, PC_GAUNTLET_PRESETS };
enum { PC_GAUNTLET_IDLE, PC_GAUNTLET_WAITING, PC_GAUNTLET_RUNNING };

/* A boss: where the story starts it and what it is. */
typedef struct {
    int disc, field, config; /* disc 1 or 2, field number, config index in its table */
    int story;               /* story progress estimate, -1 unknown */
    int dated;               /* the story from its own battle's script (a progress test or set) */
    int gear;                /* a Gear battle */
    int set;                 /* the enemy set (m0) */
    int music;               /* the theme the story plays before it (a field music id), -1 none */
    unsigned char table[PC_GAUNTLET_TABLE];
    char name[160];          /* its enemies' names, UTF-8 */
} PcGauntletBoss;

/* The members with the party now (game side, port/src/game/main/gauntlet.c). */
typedef struct {
    int count;
    unsigned char ids[11];        /* character ids */
    unsigned char hasGear[11];    /* the character has a Gear */
    unsigned char rawNames[11][20]; /* gameState names, the game's text */
    unsigned char party[3];       /* the current party (0xFF none) */
} PcGauntletRoster;

/* ---- the boss list (port/src/extras/gauntlet_bosses.c; weak: none without the extras) ---- */
typedef struct XgGauntletScan XgGauntletScan;
XgGauntletScan* xgGauntletScanBegin(void);       /* NULL: no disc image */
int xgGauntletScanStep(XgGauntletScan* s, int budget); /* 1 when done */
double xgGauntletScanProgress(const XgGauntletScan* s);
int xgGauntletScanDiscs(const XgGauntletScan* s);       /* bit 0 disc 1, bit 1 disc 2 */
/* The bosses in story order (malloc'd, the caller frees); the count. */
int xgGauntletScanResult(XgGauntletScan* s, PcGauntletBoss** out);
void xgGauntletScanFree(XgGauntletScan* s);
/* A gameState name (the game's text) as UTF-8, for the running version. */
int xgGauntletName(const unsigned char* text, int size, char* out, int outSize);

/* ---- the platform (gauntlet.c) ---- */
void pcGauntletKey(const PcGauntletBoss* b, char* out, int size); /* "D-FFF-C" */
int pcGauntletPresetOf(const char* name);       /* -1 unknown */
const char* pcGauntletPresetName(int preset);   /* settings value */
const char* pcGauntletPresetLabel(int preset);
const char* pcGauntletRecoveryName(int recovery);
const char* pcGauntletRecoveryLabel(int recovery);
int pcGauntletPreset(void);
void pcGauntletSetPreset(int preset);
int pcGauntletRecoverySetting(void);
void pcGauntletSetRecovery(int recovery);
/* The preset's bosses (indexes into bosses), the custom one from
 * gauntlet_custom; returns the count (at most max). */
int pcGauntletSelect(int preset, const PcGauntletBoss* bosses, int count, int* out, int max);
/* The custom list as indexes into bosses / from them. */
void pcGauntletSetCustom(const PcGauntletBoss* bosses, const int* list, int count);
/* The party picked for a boss (0xFF none); 0 when none was picked (then the
 * current party). */
int pcGauntletPartyFor(const PcGauntletBoss* b, unsigned char party[3]);
void pcGauntletSetPartyFor(const PcGauntletBoss* b, const unsigned char party[3]);
/* A fight's party as it will fight: picked members the roster has (a Gear
 * fight: only those with a Gear), in order; the count (0: skipped). */
int pcGauntletFightParty(const PcGauntletBoss* b, const PcGauntletRoster* roster, unsigned char out[3]);

/* Start a run of the bosses picked (indexes into bosses): it waits for the
 * field or the world map. Returns the fights it will run (skipped ones left
 * out), 0 when none can. */
int pcGauntletBegin(const char* name, const PcGauntletBoss* bosses, const int* picks, int count,
                    const PcGauntletRoster* roster);
void pcGauntletCancel(void); /* waiting: no run; running: quit after this fight */
int pcGauntletStatus(int* current, int* count, int* retries, int* skipped, int* quitting);
const char* pcGauntletName(void);
const char* pcGauntletFightName(int index);
/* The last run's end: 1 completed, 0 not, -1 none yet. */
int pcGauntletLastResult(void);

/* Records (gauntlets.json). */
typedef struct {
    char gauntlet[64];
    int fights, skipped, retries;
    unsigned int vblanks;
    char recovery[8], difficulty[8], when[24];
} PcGauntletRecord;
const char* pcGauntletRecordsPath(void);
int pcGauntletRecords(PcGauntletRecord* out, int max); /* the newest first */

/* The game side (port/src/game/main/gauntlet.c), for the menu. */
void pcGauntletRoster(PcGauntletRoster* out);

/* Tests: the env's autostart, the state as plain values. */
void pcGauntletPoll(void); /* each field/world-map start check: XG_GAUNTLET_AUTOSTART */

#ifdef __cplusplus
}
#endif

#endif
