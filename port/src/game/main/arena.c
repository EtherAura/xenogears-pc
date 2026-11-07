/*
 * The Battle arena's session (include/pc_arena.h; xg-htn.35.16).
 *
 * The game's own two-player Battling: after the Nortune purge, the arena's
 * reception (field 222, on both discs, US and JP) offers Bonus Battling. Its
 * script (actor 6, entry 6, run from the reception's dialogue choice) loads
 * Battling's song 0x0B as the field's music, fades it in and runs extended op
 * 0xBF (src/field/f_80086D8C.c OPX_BF) with mode 1, which starts Battling at
 * its main menu (src/battling/bl_80083DCC.c battlingMainTask, battlingEntryParams 1):
 * BONUS BATTLING, PRACTICE, TUTORIAL, EXIT. BONUS BATTLING > PLAYER1 VS
 * PLAYER2 (battlingGameMode 2) gives gear 1 (side 0) to pad 1 and gear 2
 * (side 1) to pad 2 (readBattlingGearPadInput), each player choosing from the
 * Gears the story allows (buildBattlingGearList: unlock level <= field
 * variable 0); a match ends back at the main menu, and EXIT goes back to the
 * field (exitBattling), which picks up where it left off from its save block.
 *
 * The Battle arena does the same from any field (arena_field.c), and keeps
 * here, in main's BSS (a soft reset ends a session), what to put back once
 * the field is back:
 *   - the field's music (the reception's script plays its own again);
 *   - the Battling entry bytes battlingEntryParams[0..6] (mode, fight, Gears, wins,
 *     COM level, and the story fight's result that field scripts read with
 *     extended op 0xC0);
 *   - Battling's records in the save, battlingProgress (gameState + 0x2344:
 *     the Gears beaten in Bonus Battling, and the bonus settings, NUM OF
 *     MATCHES, COM LEVEL and vibration, which mode 1 reads and writes back).
 * So an arena session leaves the save as it was (the play time runs on).
 */
#include "common.h"
#include "main/game_state.h"
#include "pc_arena.h"

extern u8 battlingEntryParams[];        /* Battling's entry bytes (OPX_BF) and result (OPX_C0) */
extern u8 battlingProgress[12]; /* BattlingKernelState, gameState + 0x2344 */
#if !VERSION_JP
extern s32 savedPartyIsOnGear[3];      /* who rides a Gear (US) */
#endif
extern u8 battleConfigs[0x220];
extern u8 requestedBattleConfig;
extern u8 battleTransitionEffect;

static s32 session; /* PC_ARENA_IDLE, STARTING, LEAVING or IN */
static s32 savedMusic;
static s32 savedField;
static u8 savedEntry[7];
static u8 savedRecords[sizeof(battlingProgress)];

int pcArenaSession(void) {
    return session;
}

/* The field takes the request: the player walks free in field `field`, its
 * music `music`. */
void pcArenaBegin(int music, int field) {
    savedMusic = music;
    savedField = field;
    __builtin_memcpy(savedEntry, battlingEntryParams, sizeof(savedEntry));
    __builtin_memcpy(savedRecords, battlingProgress, sizeof(savedRecords));
    session = PC_ARENA_STARTING;
}

int pcArenaStartField(void) {
    return savedField;
}

/* Op 0xBF's writes are done: the field leaves for Battling. */
void pcArenaLeaving(void) {
    session = PC_ARENA_LEAVING;
}

/* battlingMainTask, as Battling starts. */
void pcArenaBattlingStart(void) {
    if (session == PC_ARENA_LEAVING) {
        session = PC_ARENA_IN;
        pcArenaKeyboard(1);
    }
}

/* The field again after Battling: everything back; the music to play. */
int pcArenaEnd(void) {
    __builtin_memcpy(battlingEntryParams, savedEntry, sizeof(savedEntry));
    __builtin_memcpy(battlingProgress, savedRecords, sizeof(savedRecords));
    session = PC_ARENA_IDLE;
    pcArenaKeyboard(0);
    return savedMusic;
}

/* The player left the field while the song loaded: nothing was written
 * but the music; the field's music. */
int pcArenaAbort(void) {
    session = PC_ARENA_IDLE;
    pcArenaKeyboard(0);
    return savedMusic;
}

/* ---- Versus battles: player 2 commands an enemy formation ----------------
 *
 * The field starts the battle as it starts a random one (arena_field.c); the
 * battle takes the formation, battle table `table` (directory 0x20/3 file
 * table + 7, the battle debug selector's tables) config `config`, as the
 * debug selector loads it (port/src/game/battle/versus.c). The battle's
 * checkpoint, as the retry setting's (port/src/game/main/retry.c) but put
 * back after every versus battle, whoever wins: gameState (the party's HP,
 * EP, items, Gears...), savedPartyIsOnGear (US) and the battle request. Nothing is
 * won or lost: the save stays as it was. */

static s32 versus;  /* PC_VERSUS_IDLE, LEAVING or IN */
static s32 versusTable;
static s32 versusConfig;
static GameState versusState;
#if !VERSION_JP
static s32 versusOnGear[3];
#endif
static u8 versusConfigs[sizeof(battleConfigs)];
static u8 versusRequested;
static u8 versusTransition;

int pcVersusSession(void) {
    return versus;
}

int pcVersusTable(void) {
    return versusTable;
}

int pcVersusConfig(void) {
    return versusConfig;
}

/* The field takes the request: it starts the battle now. */
void pcVersusBegin(int table, int config) {
    versusTable = table;
    versusConfig = config;
    versus = PC_VERSUS_LEAVING;
}

/* battleMain, as the battle starts (before the formation goes in). */
int pcVersusCheckpoint(void) {
    if (versus != PC_VERSUS_LEAVING) {
        return 0;
    }
    versus = PC_VERSUS_IN;
    __builtin_memcpy(&versusState, &gameState, sizeof(versusState));
#if !VERSION_JP
    __builtin_memcpy(versusOnGear, savedPartyIsOnGear, sizeof(versusOnGear));
#endif
    __builtin_memcpy(versusConfigs, battleConfigs, sizeof(versusConfigs));
    versusRequested = requestedBattleConfig;
    versusTransition = battleTransitionEffect;
    pcArenaKeyboard(1);
    return 1;
}

/* battleMain's end (finishBattle has written the battle into gameState):
 * everything back as the battle found it. */
void pcVersusRestore(void) {
    if (versus != PC_VERSUS_IN) {
        return;
    }
    __builtin_memcpy(&gameState, &versusState, sizeof(versusState));
#if !VERSION_JP
    __builtin_memcpy(savedPartyIsOnGear, versusOnGear, sizeof(versusOnGear));
#endif
    __builtin_memcpy(battleConfigs, versusConfigs, sizeof(versusConfigs));
    requestedBattleConfig = versusRequested;
    battleTransitionEffect = versusTransition;
    versus = PC_VERSUS_IDLE;
    pcArenaKeyboard(0);
}

/* The field again without a versus battle having started (it was cut short
 * before the field left). */
void pcVersusAbort(void) {
    versus = PC_VERSUS_IDLE;
}
