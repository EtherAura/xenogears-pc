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
 *     extended op 0xC0; on the PC seven objects, each kept by its name);
 *   - Battling's records in the save, battlingProgress (gameState + 0x2344:
 *     the Gears beaten in Bonus Battling, and the bonus settings, NUM OF
 *     MATCHES, COM LEVEL and vibration, which mode 1 reads and writes back).
 * So an arena session leaves the save as it was (the play time runs on).
 */
#include "common.h"
#include "main/game_state.h"
#include "pc_arena.h"

/* Battling's entry bytes (OPX_BF) and result (OPX_C0): battlingEntryParams[0..6]
 * on the PS1, each an object of its own on the PC (pc_link: separate names) */
extern u8 battlingEntryParams;
extern u8 battlingEntryMatchType;
extern u8 battlingEntryGear1;
extern u8 battlingEntryGear2;
extern u8 battlingEntryWinsNeeded;
extern u8 battlingEntryComLevel;
extern u8 battlingResult;
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

/* The entry bytes, by their names: to (save) or from savedEntry. */
static void entryBytes(s32 save) {
    u8* bytes[7];
    s32 i;

    bytes[0] = &battlingEntryParams;
    bytes[1] = &battlingEntryMatchType;
    bytes[2] = &battlingEntryGear1;
    bytes[3] = &battlingEntryGear2;
    bytes[4] = &battlingEntryWinsNeeded;
    bytes[5] = &battlingEntryComLevel;
    bytes[6] = &battlingResult;
    for (i = 0; i < 7; i++) {
        if (save) {
            savedEntry[i] = *bytes[i];
        } else {
            *bytes[i] = savedEntry[i];
        }
    }
}

int pcArenaSession(void) {
    return session;
}

/* The field takes the request: the player walks free in field `field`, its
 * music `music`. */
void pcArenaBegin(int music, int field) {
    savedMusic = music;
    savedField = field;
    entryBytes(1);
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

/* battlingMainTask, as Battling starts (a field's session, or a match's
 * start: port/src/system/arena.c). */
void pcArenaBattlingStart(void) {
    if (session == PC_ARENA_LEAVING) {
        session = PC_ARENA_IN;
        pcArenaKeyboard(1);
    } else if (pcArenaMatchOn() == 1) {
        pcArenaKeyboard(1);
    }
}

/* The field again after Battling: everything back; the music to play. */
int pcArenaEnd(void) {
    entryBytes(0);
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
static s32 versusMirror; /* a mirror match (port/src/game/battle/mirror.c) */
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

int pcVersusMirror(void) {
    return versus == PC_VERSUS_IN && versusMirror;
}

/* The field takes the request: it starts the battle now. */
void pcVersusBegin(int table, int config) {
    versusTable = table;
    versusConfig = config;
    versusMirror = pcVersusMirrorAsked();
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
    versusMirror = 0;
    pcArenaKeyboard(0);
}

/* The field again without a versus battle having started (it was cut short
 * before the field left). */
void pcVersusAbort(void) {
    versus = PC_VERSUS_IDLE;
    versusMirror = 0;
}

/* ---- a match's end: Rematch / Leave (xg-htn.35.142) ----------------------
 *
 * Where a match ends (versus.c pcVersusBattleOver, after its result line;
 * Battling's victory screen: port/src/game/battling/arena.c), in a local
 * match or an online session (pcArenaMatchOn), both players choose: each on
 * their own pad, a direction switches between Rematch and Leave, confirm
 * says ready, cancel takes it back. Either ready on Leave: Leave; both ready
 * on Rematch: Rematch (player 1 alone without a second pad, locally). The
 * game draws it where the match ended, in its own look (xg-htn.35.150):
 * the battle's dialogue window (versus.c), Battling's text (battling/
 * arena.c), from pcArenaPromptState.
 *
 * Online this is what keeps both games together: it reads only the two
 * pads' bytes as the game has them (pcPadHeldRemappedPort: online the
 * exchanged ones, the same on both sides at every vblank) and keeps its
 * state here, in the game's memory, so both sides decide at the same frame
 * of the same vblank. A rematch is then the game's own soft reset on both
 * (the caller's softReboot), into the session's start again. */

#define PROMPT_WAIT 30 /* the game's frames before a press counts (the one that closed the result is let go) */
#define PROMPT_DIRECTIONS 0xF000 /* Up, Right, Down, Left */
#define PROMPT_CONFIRM 0x20 /* remapped as the battle reads it */
#define PROMPT_CANCEL 0x40

s32 isControllerConnected(s32 port); /* src/main/pad.c */
s32 pcPadHeldRemappedPort(s32 port); /* src/main/pad.c */
u32 hwVSyncCount(void);              /* port/src/system: the log's vblank */

static s32 prompt;        /* 0 none, 1 choosing, 2 Leave decided, 3 Rematch decided */
static s32 promptFrames;
static s32 promptPlayers;
static u8 promptPick[2];  /* 0 Rematch, 1 Leave */
static u8 promptReady[2];
static u16 promptHeld[2];

void pcArenaPromptBegin(void) {
    s32 p;

    prompt = 1;
    promptFrames = 0;
    promptPlayers = pcArenaMatchOn() == 2 || isControllerConnected(1) ? 2 : 1;
    for (p = 0; p < 2; p++) {
        promptPick[p] = 0;
        promptReady[p] = 0;
        promptHeld[p] = (u16)pcPadHeldRemappedPort(p);
    }
    pcArenaLog("match over at vblank %u: Rematch / Leave (%d player%s choose)", hwVSyncCount(), promptPlayers,
               promptPlayers == 2 ? "s" : "");
}

int pcArenaPromptActive(void) {
    return prompt != 0;
}

int pcArenaPromptStep(int* sound) {
    s32 p;
    s32 held;
    s32 pressed;
    s32 ready;

    *sound = PC_PROMPT_SOUND_NONE;
    if (prompt != 1) {
        return PC_PROMPT_WAIT;
    }
    for (p = 0; p < promptPlayers; p++) {
        held = pcPadHeldRemappedPort(p);
        pressed = held & ~promptHeld[p];
        promptHeld[p] = (u16)held;
        if (promptFrames < PROMPT_WAIT) {
            continue;
        }
        if ((pressed & PROMPT_DIRECTIONS) && !promptReady[p]) {
            promptPick[p] ^= 1;
            *sound = PC_PROMPT_SOUND_MOVE;
        } else if ((pressed & PROMPT_CONFIRM) && !promptReady[p]) {
            promptReady[p] = 1;
            *sound = PC_PROMPT_SOUND_CONFIRM;
            pcArenaLog("  player %d: %s (vblank %u)", p + 1, promptPick[p] ? "Leave" : "Rematch", hwVSyncCount());
        } else if ((pressed & PROMPT_CANCEL) && promptReady[p]) {
            promptReady[p] = 0;
            *sound = PC_PROMPT_SOUND_CANCEL;
        }
    }
    promptFrames++;
    ready = 1;
    for (p = 0; p < promptPlayers; p++) {
        if (promptReady[p] && promptPick[p]) {
            prompt = 2;
            return PC_PROMPT_LEAVE;
        }
        ready &= promptReady[p];
    }
    if (ready) {
        prompt = 3;
        return PC_PROMPT_REMATCH;
    }
    return PC_PROMPT_WAIT;
}

int pcArenaPromptState(int* pick, int* ready, int* players) {
    s32 p;

    for (p = 0; p < 2; p++) {
        pick[p] = promptPick[p];
        ready[p] = promptReady[p];
    }
    *players = promptPlayers;
    return prompt;
}
