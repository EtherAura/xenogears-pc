/*
 * Versus battles, the battle's side (include/pc_arena.h; F1 > Extras > Battle
 * arena; xg-htn.35.16): player 2 commands the enemy formation of a normal
 * battle, the party stays the player's.
 *
 * The formation: battle table `table` of the battle debug selector (directory
 * 0x20/3, file table + 7: 16 configs of 0x20 bytes), config `config`, put in
 * as battleDebugSelector puts it in (src/battle_debug_selector), when
 * battleMain starts, after the checkpoint of everything the battle changes
 * (port/src/game/main/arena.c).
 *
 * Player 2's choices are the enemy AI's own. On an enemy's turn battleTickMain
 * runs its script (executeMonsterScript, src/battle/b_80079674.c), whose only
 * choices are its draws (randomInRange, getRandomValueInRange): the random
 * numbers of ops 0x3E and 0x3F it then branches on, and the random target ops
 * (0x40-0x61), which draw until they find a target they have not tried. Here,
 * before the turn, the script is run again and again with every value of the
 * first draw of every op that draws (a depth-first walk; a target op's later
 * draws take the next values in turn, so it tries the targets from the chosen
 * one on), each run from the same state (the enemy's script variables and the
 * action list put back; the ops that write elsewhere, 0x07 0x28 0x29 0x2B 0x2D
 * 0x2F 0x31 0x39-0x3C 0x63 0x69 0x70-0x74, passed over). That gives what this
 * enemy's AI would do now. The command list also reads attack sequences from
 * the other guarded blocks of the enemy's scripts. A manual choice can use a
 * technique the AI normally saves for another turn or an HP threshold.
 * Player 2 picks one of the actions
 * (named as the game names them: the attack's name from the battle's
 * strings; an attack without one by what it is, Physical or Ether, its power
 * and "all", from the entry its damage uses: xg-htn.35.140), then one of its
 * targets, on pad 2;
 * then the turn's script runs for real for a current outcome. An additional
 * technique queues its battle commands without changing story state or
 * executing the surrounding AI phase changes. A missing controller still
 * leaves the turn to the game. Enemy reactions stay the game's.
 *
 * The acting player chooses from the native battle ring. Attack opens a
 * side window of techniques, and Item opens the game's inventory screen.
 * The native battle cursor selects a target in either flow.
 *
 * The end: whoever wins, a line in the same window says so ("Player 1 wins",
 * "Player 2 wins"); in a match (one started from the menu, which kept the
 * game it came from, or an online session: include/pc_arena.h) both players
 * then choose Rematch or Leave (port/src/game/main/arena.c) while the battle
 * goes on drawing, the line still up: a rematch is the game's soft reset
 * into the match's start again, Leave the platform's (the kept game back, or
 * the session's end). Otherwise the battle ends the way that gives nothing
 * and costs nothing: battleRunningVar1 0x21 for a party that won (the
 * victory without rewards: finishBattle skips processBattleRewards,
 * battle_boot returns to the field), 0x40 for a wiped party (an escape's
 * end, never the game over); then the checkpoint goes back
 * (pcVersusBattleEnd, battleMain's end).
 *
 * Gear against Gear (xg-htn.35.16 R-C): with the setting arena_versus_gears
 * the party's members who have a Gear fight in it (gameState.m22B1_isOnGear,
 * as the battle debug selector sets it; put back with the rest), against a
 * formation of enemy Gears that player 2 commands as above. Player 2's own
 * Gears from the save cannot take the enemy side: the battle keeps a party
 * Gear's numbers in tables sized for the three party slots (battleEntities
 * m1058_partyBattleStats[3], m2228_mechaBattleStats[3], loadPartyMembers),
 * loads party Gear models to fixed party texture places
 * (battleMechaPartyTextureLocations), has one Gear command ring and cockpit
 * HUD, and moves enemies only through their set's monster scripts.
 *
 * Without a versus battle every hook returns at once (executeMonsterScript is
 * called as before): normal battles are unchanged.
 */
#include "common.h"
#include "psyq.h"
#include "main/dialog.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "main/memory.h"
#include "battle/battle.h"
#include "pc_arena.h"
#include "pc_gameplay.h"
#include "pc_backlog.h"
#include "../../system/arena_roster.h"
#include "versus.h"

s32 battleRenderDebugAndMain(void);
void battleSoundEffect(u8 id);
extern u8 startCharacterJumpToEnemyVar0; /* native battle input sound switch */
u8 executeMonsterScriptLower(sBattleScriptExecutionContext* ctx, u8 entity, u8 index);
u8 executeMonsterScriptUpper(sBattleScriptExecutionContext* ctx, u8 entity);
void skipScriptIfFalse(sBattleScriptExecutionContext* ctx);
void advanceBattleScript(sBattleScriptExecutionContext* ctx);
void executeMonsterScript(u8 entity, s32 unused);
u16 bitmaskCharacterCheck(u16 mask, u8 i);
u8 selectNewSlotByDirection(u8 inputSlot, u8 direction);
void initBattleG3Array(void);
void removeTargetSelectionCursors(void);
void drawBattleDialogWindow(u8 idx, u16 x, u16 y, u16 w, u16 h);
void loadImageSync(RECT* rect, void* data);
void* allocateBattleHeap(s32 size, s32 flags);
s32 getCurrentDirectory(s32* directory, s32* offset);
s32 isControllerConnected(s32 port);
s32 pcPadHeldRemappedPort(s32 port); /* src/main/pad.c */
u32 portRandSeed(void);               /* port/src/system/libc.c */
u32 hwVSyncCount(void);               /* port/src/system: the log's vblank */
void srand(u32 seed);
extern u8* currentBattleSpecificStrings;
extern u8* battleConfigFile2;
extern u8 currentBattleConfig[0x20];
extern u8 battleMonsterMapping[11];
extern u8 isEntityReadyForBattle[11];
extern u8 battleConfigs[0x220];
extern u8 requestedBattleConfig;
extern u8 targetsPerPriority[12];
extern sBattle800D2204 D_800D2204[];
extern u8 isTargetValid(u8 actor, u8 target);
extern u8 numValidTarget;
extern u8 selectedItemRow, selectedItemColumn, abilitySelectedTarget;
extern u16 abilityTargetBitmask;
void executeItem(u8 entity);
u8 drawCircleMenuItem_update(u8 entity);
s32 updateBattleMenuSpriteForMenu(u8 entity, u8 set, u8 dimOnly);
extern u8* battleCommandsSetups[19];
extern u8 battleInputButton;

#define MAX_DRAWS 24     /* draws a turn's script makes (a target op redraws) */
#define MAX_RUNS 6000    /* probe runs a turn */
#define MAX_OUTCOMES 192 /* distinct action lists and legal target sets */
#define MAX_CHOICES 128
#define LABEL 48         /* game-text bytes of a label */
#if VERSION_JP
#define PAGE 2           /* choices shown at once: the JP window shows no fourth line (measured) */
#define GLYPH_LEAD 0xF8
#else
#define PAGE 6           /* choices shown at once */
#define GLYPH_LEAD 0xFE
#endif

#define BUTTON_RIGHT 0x2000
#define BUTTON_LEFT 0x8000
#define BUTTON_UP 0x1000
#define BUTTON_DOWN 0x4000
#define BUTTON_CONFIRM 0x20 /* remapped as the battle reads it */
#define BUTTON_EXECUTE (BUTTON_CONFIRM | 0x80 | 0x10) /* the battle command buttons */
#define BUTTON_CANCEL 0x40

/* ---- the plan: what randomInRange and getRandomValueInRange answer ---- */

/* One value per op that draws: its first draw answers the plan's value (the
 * lowest past the plan), the op's further draws the next values in turn,
 * wrapping: a random target op that draws again until it finds a target it
 * has not tried yet (selectRandomPC...) then tries them from the plan's on,
 * and ends. */
enum { PLAN_OFF, PLAN_PROBE, PLAN_PLAY };
static s32 planMode;
static u16 plan[MAX_DRAWS];
static s32 planLen;
static s32 drawAt;  /* ops that drew so far this run: the plan's index */
static s32 opDraws; /* draws so far in the op running */
static s32 opBase;
static u16 drawLo[MAX_DRAWS];
static u16 drawHi[MAX_DRAWS];

/* src/main/game_init.c randomInRange and src/battle/b_80089B50.c
 * getRandomValueInRange, past their fixed answers: 1 and *out on a turn of
 * player 2's, 0 otherwise (the game's own draw). */
int pcVersusRandom(int lo, int hi, int* out) {
    s32 k;
    s32 v;

    if (planMode == PLAN_OFF) {
        return 0;
    }
    if (hi < lo) {
        *out = lo;
        return 1;
    }
    if (opDraws == 0) {
        k = drawAt++;
        if (k < MAX_DRAWS) {
            drawLo[k] = lo;
            drawHi[k] = hi;
        }
        v = k < planLen ? plan[k] : lo;
        opBase = v < lo ? lo : v > hi ? hi : v;
        v = opBase;
    } else {
        v = lo + ((opBase - lo) + opDraws) % (hi - lo + 1);
    }
    opDraws++;
    *out = v;
    return 1;
}

/* ---- the probe: what this enemy's turn script can do now ---- */

typedef struct {
    sBattleScriptContext ctx[32];
    u16 plan[MAX_DRAWS];
    u8 planLen;
    u8 technique;
} Outcome;

static Outcome outcomes[MAX_OUTCOMES];
static s32 outcomeCount;
static s32 runs;
static s32 runLimit;
static sBattleScriptEntity probeVars;
static u8* techniqueStart;
static const u8* techniqueEnd;
static u8* techniqueSetup[8]; /* earlier target selectors used by a guarded body */

static int writesElsewhere(u8 op) {
    switch (op) {
    case 0x07: /* battle script game-state variable */
    case 0x28: case 0x29: /* its own stats */
    case 0x2B: case 0x2D: case 0x2F: /* a target's stats, Gear HP */
    case 0x31: /* battle script game-state variable */
    case 0x39: case 0x3A: case 0x3B: case 0x3C: /* experience, gold, drops */
    case 0x63: /* hidden or shown (moves it) */
    case 0x69: /* hit result */
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: /* event variable, commands, rows, next turn, slots */
        return 1;
    }
    return 0;
}

/* Story scripts can name a fixed slot that is absent in a custom team.
 * Keep authored targets that exist. For a missing target, the approach and
 * the strike need the same replacement, on the side called for by the move. */
enum RosterTargetPool { OPPONENT, LIVING_ALLY, FALLEN_ALLY, TARGET_POOLS };

static int rosterTargetPool(const sBattleScriptContext* strike) {
    if (strike->m0_attackType == 1 && strike->m1_operand < 200) {
        const sBattle800CDD40Sub* row = &battleEntities.m35D8_enemiesBattleStats[strike->m1_operand];
        switch (row->m16_effectType) {
        case 1: case 5: case 7: return LIVING_ALLY; /* heal, cure, repair */
        case 6: return FALLEN_ALLY; /* revive */
        }
    }
    return OPPONENT;
}

static void resolveRosterTargets(u8 entity) {
    u16 present = 0;
    u16 candidates[TARGET_POOLS][11];
    int counts[TARGET_POOLS] = {0};
    int i;

    if (!pcArenaRosterActive()) return;
    for (i = 0; i < 11; i++) {
        int pool, ko;
        u16 bit;
        const sBattleEntity* target = &battleEntities.m0_entities[i];
        if (!isBattleSlotFilled.m0_isBattleSlotFilled[i] || battleVisualEntities[i].m3_hidden) continue;
        bit = characterIdToTargetBitmask(i);
        present |= bit;
        ko = (apConfigArray[i].m1_gearState ? target->mA4_gear.m7C_statusFlags : target->m0_base.m7C_statusFlags[0]) & 0x8000;
        pool = pcArenaRosterSide(i) == pcArenaRosterSide(entity) ? (ko ? FALLEN_ALLY : LIVING_ALLY) : OPPONENT;
        if (pool != FALLEN_ALLY && (ko || isEntityReadyForBattle[i] == 0xFF)) continue;
        candidates[pool][counts[pool]++] = bit;
    }
    for (i = 0; i < 32; i++) {
        sBattleScriptContext* c = &battleScriptContext[i];
        const sBattleScriptContext* strike = c;
        int pool;
        if (c->m0_attackType != 1 && c->m0_attackType != 2) continue;
        c->m6_mask &= present;
        if (c->m6_mask) continue;
        if (c->m0_attackType == 2 && i + 1 < 32 && c[1].m0_attackType == 1) {
            strike = &c[1];
            if (strike->m6_mask & present) {
                c->m6_mask = strike->m6_mask & present;
                continue;
            }
        }
        pool = rosterTargetPool(strike);
        if (c->m0_attackType == 1 && i > 0 && c[-1].m0_attackType == 2) {
            int j;
            for (j = 0; j < counts[pool]; j++) c->m6_mask |= c[-1].m6_mask & candidates[pool][j];
        }
        if (!c->m6_mask && counts[pool]) {
            opDraws = 0;
            c->m6_mask = candidates[pool][randomInRange(0, counts[pool] - 1)];
        }
    }
}

/* The turn's script once, as executeMonsterScript runs it, without the
 * ops that write elsewhere; the action list is left in battleScriptContext. */
static void probeRun(u8 m) {
    sBattleScriptExecutionContext ctx;
    u8 index = 0;
    s32 steps = 0;

    monstersScriptsEntities[m] = probeVars;
    __builtin_memset(battleScriptContext, 0, sizeof(battleScriptContext));
    drawAt = 0;
    if (techniqueStart != NULL) {
        s32 i;
        for (i = 0; i < 8; i++) {
            if (techniqueSetup[i] == NULL) continue;
            ctx.m0_scriptPtr = techniqueSetup[i];
            opDraws = 0;
            executeMonsterScriptLower(&ctx, m, 0);
        }
    }
    ctx.m0_scriptPtr = techniqueStart == NULL ? monstersScriptsEntities[m].m0_scripts[0] : techniqueStart;
    while (*ctx.m0_scriptPtr != 0xFD && *ctx.m0_scriptPtr != 0xFF && steps++ < 4096) {
        if (index >= 32 || (techniqueEnd != NULL && ctx.m0_scriptPtr >= techniqueEnd)) {
            break;
        }
        opDraws = 0;
        if (*ctx.m0_scriptPtr >= 0x80) {
            if (executeMonsterScriptUpper(&ctx, m) == 0) {
                skipScriptIfFalse(&ctx);
            }
        } else if (writesElsewhere(*ctx.m0_scriptPtr)) {
            advanceBattleScript(&ctx);
        } else {
            index = executeMonsterScriptLower(&ctx, m, index);
        }
    }
    resolveRosterTargets(m + 3);
    runs++;
}

/* Phase, story and actor management commands must stay in the AI. An
 * auxiliary technique may animate, announce a move and apply its effects. */
static int executableTechnique(void) {
    s32 action = 0;
    s32 i;

    for (i = 0; i < 32 && battleScriptContext[i].m0_attackType != 0; i++) {
        const sBattleScriptContext* c = &battleScriptContext[i];
        switch (c->m0_attackType) {
        case 1:
            if (c->m1_operand >= 200 || c->m6_mask == 0) return 0;
            action = 1;
            break;
        case 2:
            if (c->m6_mask == 0) return 0;
            break;
        case 13:
            action = 1;
            break;
        case 3: case 4: case 12: case 14: case 15: case 16:
            break;
        default:
            return 0;
        }
    }
    return action;
}

static void addOutcome(s32 depth) {
    s32 i;

    if (techniqueStart != NULL && !executableTechnique()) {
        return;
    }
    for (i = 0; i < outcomeCount; i++) {
        if (__builtin_memcmp(outcomes[i].ctx, battleScriptContext, sizeof(battleScriptContext)) == 0) {
            return;
        }
    }
    if (outcomeCount >= MAX_OUTCOMES) {
        return;
    }
    __builtin_memcpy(outcomes[outcomeCount].ctx, battleScriptContext, sizeof(battleScriptContext));
    __builtin_memcpy(outcomes[outcomeCount].plan, plan, sizeof(plan));
    outcomes[outcomeCount].planLen = depth;
    outcomes[outcomeCount].technique = techniqueStart != NULL;
    outcomeCount++;
}

/* Every value of draw `depth` (at most 256, spread over a wider range), each
 * walked further. */
static void explore(u8 m, s32 depth) {
    s32 lo;
    s32 hi;
    s32 step;
    s32 v;

    if (runs >= runLimit || outcomeCount >= MAX_OUTCOMES) {
        return;
    }
    planLen = depth;
    probeRun(m);
    if (drawAt <= depth || depth >= MAX_DRAWS) {
        addOutcome(depth);
        return;
    }
    lo = drawLo[depth];
    hi = drawHi[depth];
    step = (hi - lo) / 256 + 1;
    for (v = lo; v <= hi; v += step) {
        plan[depth] = v;
        explore(m, depth + 1);
    }
    if ((hi - lo) % step != 0) {
        plan[depth] = hi;
        explore(m, depth + 1);
    }
}

static int selectsTarget(u8 op) {
    return (op >= 0x40 && op <= 0x4B) || (op >= 0x5A && op <= 0x61) || (op >= 0x64 && op <= 0x68);
}

/* A phase body can read an actor mask that the script set before branching.
 * Replay the targeting instruction alone when the body reads that register
 * before selecting a target of its own. For example, Alpha Weltall chooses a
 * Gear before deciding which attack animation and damage rows to queue. */
static void findTechniqueSetup(u8* script, u8* body, u8* end) {
    u8 used = 0;
    u8 defined = 0;
    u8* p;

    __builtin_memset(techniqueSetup, 0, sizeof(techniqueSetup));
    for (p = body; p < end; p += 4) {
        if (selectsTarget(p[0]) && p[1] < 8) defined |= 1 << p[1];
        if (p[0] == 0x52 && p[1] == 6 && p[2] < 8) used |= (1 << p[2]) & ~defined;
    }
    for (p = body; used && p > script;) {
        p -= 4;
        if (selectsTarget(p[0]) && p[1] < 8 && (used & (1 << p[1]))) {
            techniqueSetup[p[1]] = p;
            used &= ~(1 << p[1]);
        }
    }
}

/* The file starts with 24 offsets to enemy script headers and then the
 * string table offset. Each header starts with four script offsets. Bound
 * the scan by the next script, next enemy or string table, whichever is
 * first. The engine has already loaded this bank and its runtime scripts. */
static void probeTechniques(u8 entity) {
    s32 bank;
    s32 type = battleMonsterMapping[entity] - 1;
    const u8* data = pcArenaRosterBank(entity, &bank, &type);
    const u16* offsets = (const u16*)(data == NULL ? battleConfigFile2 : data);
    u8 m = entity - 3;
    const u16* header;
    u8* block;
    s32 from;
    s32 end;
    s32 i;
    s32 script;

    if (offsets == NULL || type < 0 || type >= 8) return;
    from = offsets[type];
    end = offsets[24];
    if (from < 0x32 || from + 8 > end) return;
    for (i = 0; i < 24; i++) {
        if (offsets[i] > from && offsets[i] < end) end = offsets[i];
    }
    block = (u8*)offsets + from;
    header = (const u16*)block;
    for (script = 0; script < 4; script++) {
        u8* cursor;
        s32 length = end - from;
        s32 start = header[script];
        if (script == 1 || start < 8 || start >= length ||
            monstersScriptsEntities[m].m0_scripts[script] != block + start) continue;
        for (i = 0; i < 4; i++) {
            if (header[i] > start && header[i] < length) length = header[i];
        }
        cursor = block + start;
        while (cursor + 4 <= block + length && *cursor != 0xFF) {
            u8* body = cursor;
            while (body + 4 <= block + length && *body >= 0x80 && *body < 0xF0) body += 4;
            cursor = body;
            while (cursor + 4 <= block + length && *cursor < 0x80) cursor += 4;
            if (cursor > body) {
                techniqueStart = body;
                techniqueEnd = cursor;
                findTechniqueSetup(block + start, body, cursor);
                runLimit = runs + 512;
                explore(m, 0);
            }
            if (cursor + 4 > block + length || *cursor == 0xFF) break;
            if (*cursor >= 0xF0 || cursor == body) cursor += 4;
        }
    }
    techniqueStart = NULL;
    techniqueEnd = NULL;
}

static void probeTurn(u8 entity) {
    u8 m = entity - 3;
    u32 seed = portRandSeed();

    probeVars = monstersScriptsEntities[m];
    outcomeCount = 0;
    runs = 0;
    planMode = PLAN_PROBE;
    runLimit = MAX_RUNS;
    explore(m, 0);
    probeTechniques(entity);
    planMode = PLAN_OFF;
    monstersScriptsEntities[m] = probeVars;
    __builtin_memset(battleScriptContext, 0, sizeof(battleScriptContext));
    srand(seed); /* no draw reached rand(), but keep the stream as it was */
}

/* The turn's script for real, as executeMonsterScript runs it (every op),
 * the draws from the plan of player 2's choice. */
static void playRun(u8 entity, const Outcome* outcome) {
    sBattleScriptExecutionContext ctx;
    u8 m = entity - 3;
    u8 index = 0;
    s32 i;

    ctx.m0_scriptPtr = monstersScriptsEntities[m].m0_scripts[0];
    __builtin_memset(battleScriptContext, 0, sizeof(battleScriptContext));
    for (i = 0; i < 32; i++) {
        battleCurrentDamages[i].m47_battleAnimationToPlay = 0xFF;
    }
    if (outcome->technique) {
        __builtin_memcpy(battleScriptContext, outcome->ctx, sizeof(battleScriptContext));
        return;
    }
    drawAt = 0;
    planMode = PLAN_PLAY;
    while (*ctx.m0_scriptPtr != 0xFD && *ctx.m0_scriptPtr != 0xFF) {
        opDraws = 0;
        if (*ctx.m0_scriptPtr >= 0x80) {
            if (executeMonsterScriptUpper(&ctx, m) == 0) {
                skipScriptIfFalse(&ctx);
            }
        } else {
            index = executeMonsterScriptLower(&ctx, m, index);
        }
    }
    resolveRosterTargets(entity);
    planMode = PLAN_OFF;
}

/* ---- labels, as game text ---- */

/* Fixed words, in the font's single-byte codes (docs/ASSETS.md: 0x10 space,
 * 0x16-0x1F digits, 0x20-0x39 A-Z, 0x3D-0x56 a-z on US; the JP table lacks
 * some letters, so its words use the ones it has). */
static void word(u8* out, const char* s) {
#if VERSION_JP
    static const char narrow[] = " ()+,-./0123456789=ACDEFGILMNOPRSTUVXYZ[]\1abcdefgiklmnoprstuwxy";
#else
    static const char narrow[] = " +,-./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ[]=abcdefghijklmnopqrstuvwxyz!\"#%&'():?";
#endif
    s32 n = 0;
    s32 i;

    for (; *s != 0 && n < LABEL - 1; s++) {
        for (i = 0; narrow[i] != 0; i++) {
            if (narrow[i] == *s) {
                out[n++] = 0x10 + i;
                break;
            }
        }
    }
    out[n] = 0;
}

/* A string of the game's (0-terminated, a new line becomes a space). */
static void gameText(u8* out, const u8* s, s32 max) {
    s32 n = 0;

    if (max > LABEL - 1) {
        max = LABEL - 1;
    }
    while (s != NULL && *s != 0 && n < max) {
        u8 ch = *s++;
        if (ch >= GLYPH_LEAD) {
            if (n + 2 > max) break;
            out[n++] = ch;
            out[n++] = *s++;
        } else {
            out[n++] = ch == 1 ? 0x10 : ch;
        }
    }
    out[n] = 0;
}

unsigned char* pcVersusEnemyText(int entity, int name) {
    s32 bank, type;
    const u8* data = pcArenaRosterBank(entity, &bank, &type);
    u8* strings;
    static const u8 empty[] = {0};

    if (data != NULL) {
        strings = (u8*)data + ((const u16*)data)[24];
        if (name < 0) name = type + 1;
    } else {
        strings = currentBattleSpecificStrings;
        if (name < 0) name = battleMonsterMapping[entity];
    }
    if (strings == NULL || (u32)name >= *(const u32*)strings) return (u8*)empty;
    return getDialogParamPointer(strings, name);
}

static void entityName(u8* out, s32 entity) {
    if (entity < 3) {
        gameText(out, gameState.m0_names[battleCharacters[entity] & 0x1F], 0x13);
    } else if (currentBattleSpecificStrings != NULL) {
        gameText(out, pcVersusEnemyText(entity, -1), LABEL - 1);
    } else {
        out[0] = 0;
    }
}

enum {
    MOVE_STRIKE, MOVE_COMBO, MOVE_CHAIN, MOVE_ETHER, MOVE_DARK,
    MOVE_HEAL, MOVE_STATUS, MOVE_DRAIN, MOVE_FIXED, MOVE_CURE,
    MOVE_REVIVE, MOVE_REPAIR, MOVE_HALVE, MOVE_REVENGE, MOVE_MOVE,
    MOVE_STANCE, MOVE_WAIT, MOVE_SUMMON, MOVE_ALL, MOVE_LIGHT,
    MOVE_HEAVY, MOVE_NEUTRAL,
};

/* Fixed descriptions only for moves without a name in the acting enemy's
 * script. The second spelling uses the Japanese retail font. */
static void moveWord(u8* out, s32 id) {
#if VERSION_JP
#define MOVE_TEXT(us, jp) jp
#else
#define MOVE_TEXT(us, jp) us
#endif
    static const char* const names[] = {
        MOVE_TEXT("Physical strike", "\xFD\x1B\xFD\xC1\xFA\x2E\xDB"), /* 物理攻撃 */
        MOVE_TEXT("Martial combo", "\xFD\xF2\xFB\xD0\xFA\x2E\xDB"), /* 連続攻撃 */
        MOVE_TEXT("Mixed combo", "\xFD\xF2\xFB\xD0\xF9\x77"), /* 連続技 */
        MOVE_TEXT("Ether attack", "\xAA\x55\xBA\xD1\xFA\x2E\xDB"), /* エーテル攻撃 */
        MOVE_TEXT("Dark Matter", "\xF8\xA1\xFA\x48\xFC\xA8"), /* 暗黒波 */
        MOVE_TEXT("Restore HP", "\x66\x69\xD9\xFD\x14"), /* ＨＰ回復 */
        MOVE_TEXT("Status change", "\xFB\x37\xFB\xE4\xF4\xF8\xF4"), /* 状態変化 */
        MOVE_TEXT("Drain", "\xF9\x86\xFA\xE1"), /* 吸収 */
        MOVE_TEXT("Special damage", "\xFC\x88\xFA\xD7\xFA\x2E\xDB"), /* 特殊攻撃 */
        MOVE_TEXT("Clear status", "\xFB\x37\xFB\xE4\xD9\xFD\x14"), /* 状態回復 */
        MOVE_TEXT("Revive", "\xFD\x14\xF9\x36"), /* 復活 */
        MOVE_TEXT("Repair Gear", "\xF9\x6A\xFB\xE0\xD9\xFD\x14"), /* 機体回復 */
        MOVE_TEXT("Fractional HP attack", "\xF9\x35\xFA\x42\xFA\x2E\xDB"), /* 割合攻撃 */
        MOVE_TEXT("Missing HP attack", "\xFC\xCB\xDB"), /* 反撃 */
        MOVE_TEXT("Change position", "\xF8\xB0\xFC\x80"), /* 移動 */
        MOVE_TEXT("Change stance", "\xFA\x31\x6F"), /* 構え */
        MOVE_TEXT("Wait", "\xFB\xE3\xF9\x6A"), /* 待機 */
        MOVE_TEXT("Reinforcements", "\xFB\xC2\xF8\xDC"), /* 増援 */
        MOVE_TEXT(" (all)", "\x10\xFB\xA1\xFB\xE0"), /*  全体 */
        MOVE_TEXT("Light strike", "\xFA\xD2\xFA\x2E\xDB"), /* 弱攻撃 */
        MOVE_TEXT("Heavy strike", "\xF9\xA1\xFA\x2E\xDB"), /* 強攻撃 */
        MOVE_TEXT("Strike", "\xFB\xDD\xDB"), /* 打撃 */
    };
#undef MOVE_TEXT
#if VERSION_JP
    gameText(out, (const u8*)names[id], LABEL - 1);
#else
    word(out, names[id]);
#endif
}

static int labelBytes(const u8* text) {
    int len = 0;
    while (text[len]) len += text[len] >= GLYPH_LEAD ? 2 : 1;
    return len;
}

/* Only these commands actually display a name. Bytes in the same position
 * of a move, camera or wait command have unrelated meanings. */
static int isMoveName(int entity, int index) {
    const u8* s;
    if (index <= 0) return 0;
    s = pcVersusEnemyText(entity, index);
    /* Empty placeholders, countdown digits and quoted dialogue are also
     * stored in the enemy string bank. They are not technique names. */
    int hasText = 0;
    if (s[0] < 0x10 || (s[0] == 0xFF && s[1] == 0xFF)) return 0;
    while (*s) {
        unsigned glyph = *s++;
        if (glyph >= GLYPH_LEAD) glyph = (glyph << 8) | *s++;
#if VERSION_JP
        if (glyph == 0x59 || glyph == 0x5C || glyph == 0xF801 ||
            (glyph >= 0xF81B && glyph <= 0xF81E)) return 0;
        if ((glyph >= 0x18 && glyph <= 0x21) || glyph == 0x4F) continue;
#else
        if (glyph == 0x58 || glyph == 0x7B || glyph == 0x7C || glyph == 0x84 ||
            glyph == 0x85 || glyph == 0x8F || glyph == 0x92) return 0;
        if ((glyph >= 0x16 && glyph <= 0x1F) || (glyph >= 0x61 && glyph <= 0x6A)) continue;
#endif
        if (glyph > 0x10) hasText = 1;
    }
    return hasText;
}

/* Retail enemy banners often append a slash and an explanation of the
 * mechanics. Display the technique itself in the player's selector. */
static void copyMoveName(u8* out, const u8* source) {
    int bytes = 0, end = 0;
#if VERSION_JP
    const int slash = 0x17, wideSlash = 0x56, wideSpace = 0x4F;
#else
    const int slash = 0x15, wideSlash = 0x79, wideSpace = 0x10;
#endif
    while (*source == 0x10 || *source == wideSpace) source++;
    while (source[bytes] && source[bytes] != slash && source[bytes] != wideSlash) {
        int glyph = source[bytes];
        bytes += glyph >= GLYPH_LEAD ? 2 : 1;
        if (glyph > 0x10 && glyph != wideSpace) end = bytes;
    }
    gameText(out, source, end);
}

static int moveNameIndex(int entity, const sBattleScriptContext* c) {
    int name = 0, k;
    for (k = 0; k < 32 && c[k].m0_attackType; k++) {
        switch (c[k].m0_attackType) {
        case 12: case 13:
            if (isMoveName(entity, c[k].m3_nameIndex)) name = c[k].m3_nameIndex;
            break;
        case 16: name = 0; break;
        case 1: return isMoveName(entity, c[k].m3_nameIndex) ? c[k].m3_nameIndex : name;
        }
    }
    return name;
}

/* Match a technique across AI branches using its whole sequence of effects
 * and strikes. Different actors can use the same local animation numbers. */
static int sameTechnique(const sBattleScriptContext* a, const sBattleScriptContext* b) {
    int i = 0, j = 0, matched = 0;
    for (;;) {
        while (i < 32 && a[i].m0_attackType && a[i].m0_attackType != 1 && a[i].m0_attackType != 13) i++;
        while (j < 32 && b[j].m0_attackType && b[j].m0_attackType != 1 && b[j].m0_attackType != 13) j++;
        if (i == 32 || !a[i].m0_attackType) return matched && (j == 32 || !b[j].m0_attackType);
        if (j == 32 || a[i].m0_attackType != b[j].m0_attackType) return 0;
        if (a[i].m0_attackType == 1) {
            if (a[i].m1_operand != b[j].m1_operand || a[i].m2_animationId != b[j].m2_animationId) return 0;
        } else if (a[i].m4_valueLow != b[j].m4_valueLow || a[i].m5_valueHigh != b[j].m5_valueHigh) return 0;
        i++; j++; matched = 1;
    }
}

/* A type-13 command plays an ability from 0E/022 + twice its effect ID.
 * These contiguous ranges map a named party ability to its native text bank
 * and entry. Derived from the learnable commands in 12/02, the animation
 * flags in 0C/03 and the battle overlay's per-character animation bases.
 * US and JP share the indices. Read the spelling from the active game. */
static const u8* abilityMoveName(int effect) {
    static const struct { u8 first, last, bank, entry; } ranges[] = {
        {0, 5, 0, 7},
        {7, 13, 20, 1},
        {14, 14, 20, 0},
        {22, 25, 1, 7},
        {26, 37, 20, 16},
        {47, 50, 2, 7},
        {51, 54, 9, 7},
        {55, 65, 20, 32},
        {74, 77, 3, 7},
        {78, 87, 20, 48},
        {94, 94, 4, 7},
        {95, 95, 4, 9},
        {96, 96, 4, 8},
        {97, 106, 20, 64},
        {113, 115, 5, 7},
        {116, 119, 20, 80},
        {124, 126, 6, 7},
        {127, 129, 10, 7},
        {131, 138, 20, 96},
        {143, 150, 20, 112},
        {151, 155, 20, 128},
    };
    unsigned int i;
    for (i = 0; i < sizeof ranges / sizeof ranges[0]; i++) {
        if (effect >= ranges[i].first && effect <= ranges[i].last)
            return getDeathblowName(ranges[i].bank, ranges[i].entry + effect - ranges[i].first);
    }
    return NULL;
}

static void actionLabel(u8* out, s32 entity, const sBattleScriptContext* c) {
    int name = moveNameIndex(entity, c), first = -1, strikes = 0, repeat = 1, effect = -1;
    int descriptor = MOVE_WAIT, k, len, last = -1;
    int bank = currentBattleConfig[0], type = battleMonsterMapping[entity] - 1;
    unsigned int targets = 0;
    out[0] = 0;
    if (!name) {
        for (k = 0; k < outcomeCount; k++) {
            int sibling;
            if (!sameTechnique(c, outcomes[k].ctx)) continue;
            sibling = moveNameIndex(entity, outcomes[k].ctx);
            if (!sibling) continue;
            if (name && name != sibling) { name = 0; break; }
            name = sibling;
        }
    }
    for (k = 0; k < 32 && c[k].m0_attackType; k++) {
        if (c[k].m0_attackType == 13 && effect < 0)
            effect = c[k].m4_valueLow | ((c[k].m5_valueHigh & 1) << 8);
        if (c[k].m0_attackType == 2) descriptor = MOVE_MOVE;
        if (c[k].m0_attackType == 3 || c[k].m0_attackType == 4) descriptor = MOVE_STANCE;
        if (c[k].m0_attackType == 7) descriptor = MOVE_SUMMON;
        if (c[k].m0_attackType != 1 || c[k].m1_operand >= 200) continue;
        if (first < 0) first = k;
        last = k;
        if (c[k].m1_operand != c[first].m1_operand || c[k].m2_animationId != c[first].m2_animationId) repeat = 0;
        if (c[k].m6_mask & (c[k].m6_mask - 1)) targets = 1;
        strikes++;
    }
    if (name) copyMoveName(out, pcVersusEnemyText(entity, name));
    if (!out[0] && effect >= 0) gameText(out, abilityMoveName(effect), LABEL - 1);
    pcArenaRosterBank(entity, &bank, &type);
    /* On-foot Id chains the same deathblows as Fei. His own script doesn't
     * announce them. Use Fei's localized names and the order of both hits. */
    if (!out[0] && bank == 51 && type == 0 && strikes == 2 &&
        c[first].m1_operand == 8 && c[last].m1_operand == 8 && c[last].m2_animationId == 29) {
        if (c[first].m2_animationId == 29) gameText(out, getDeathblowName(0, 0), LABEL - 1);
        if (c[first].m2_animationId == 30) {
            gameText(out, getDeathblowName(0, 1), LABEL / 2);
            len = labelBytes(out);
            word(out + len, " + ");
            len = labelBytes(out);
            gameText(out + len, getDeathblowName(0, 0), LABEL - len - 1);
        }
    }
    if (!out[0]) {
        if (first >= 0) {
            const sBattle800CDD40Sub* row = &battleEntities.m35D8_enemiesBattleStats[c[first].m1_operand];
            switch (row->m16_effectType) {
            case 1: descriptor = MOVE_HEAL; break;
            case 2: descriptor = MOVE_STATUS; break;
            case 3: descriptor = MOVE_DRAIN; break;
            case 4: descriptor = row->m1A_attackFormula == 0 ? MOVE_HALVE :
                                 row->m1A_attackFormula == 2 ? MOVE_REVENGE : MOVE_FIXED; break;
            case 5: descriptor = MOVE_CURE; break;
            case 6: descriptor = MOVE_REVIVE; break;
            case 7: descriptor = MOVE_REPAIR; break;
            default:
                /* An Ether damage formula alone does not identify a spell:
                 * melee animations can use it, including Grahf's strikes. */
                if (effect >= 0) descriptor = MOVE_ETHER;
                else if (strikes > 1) descriptor = repeat ? MOVE_COMBO : MOVE_CHAIN;
                else if (row->m11_power < 20) descriptor = MOVE_LIGHT;
                else if (row->m11_power > 20) descriptor = MOVE_HEAVY;
                else descriptor = row->mA_attackFlags & 0x100 ? MOVE_NEUTRAL : MOVE_STRIKE;
            }
        } else if (effect >= 0) descriptor = MOVE_ETHER;
        if (effect == 210) descriptor = MOVE_DARK; /* Id's party-wide vortex */
        if (bank == 56 && (type == 0 || type == 1) && effect < 0 && first >= 0 &&
            c[first].m2_animationId == 29) descriptor = MOVE_COMBO;
        moveWord(out, descriptor);
        if (targets) {
            u8 all[16];
            moveWord(all, MOVE_ALL);
            len = labelBytes(out);
            if (len + labelBytes(all) < LABEL) __builtin_memcpy(out + len, all, labelBytes(all) + 1);
        }
    }
    len = labelBytes(out);
    if (repeat && strikes > 1 && strikes < 10 && len + 3 < LABEL) {
        char suffix[] = " x2";
        suffix[2] = '0' + strikes;
        word(out + len, suffix);
    }
}

/* A legacy script can enqueue two different banners back to back for one
 * strike. The last one introduces the action. Remove superseded consecutive
 * announcements only in a manually selected turn, leaving later hits, speech,
 * target masks, damage commands and the ordinary AI's script alone. */
static u8 chosenMoveText[LABEL];

u8* pcVersusMoveText(int entity, int idx) {
    const sBattleScriptContext* c = &battleScriptContext[idx];
    if (pcVersusSession() == PC_VERSUS_IN &&
        ((c->m0_attackType == 12 && c->m1_operand == 0xFF && !c->m3_nameIndex) ||
         ((c->m0_attackType == 1 || c->m0_attackType == 13) && c->m3_nameIndex == 0xFF)))
        return chosenMoveText;
    return pcVersusEnemyText(entity, c->m3_nameIndex);
}

static void resolveMoveCaptions(sBattleScriptContext* c, int entity, const u8* label) {
    int i, kept = 0, hasAction = 0;
    int name = moveNameIndex(entity, c);
    __builtin_memcpy(chosenMoveText, label, LABEL);
    for (i = 0; i < 32 && c[i].m0_attackType; i++) {
        if (c[i].m0_attackType == 1 || c[i].m0_attackType == 13) {
            hasAction = 1;
            if (name && c[i].m3_nameIndex == name) c[i].m3_nameIndex = 0xFF;
        }
        if (c[i].m0_attackType == 12 && isMoveName(entity, c[i].m3_nameIndex)) {
            if (i + 1 < 32 && c[i + 1].m0_attackType == 12 && isMoveName(entity, c[i+1].m3_nameIndex)) continue;
            if (c[i].m3_nameIndex == name) {
                c[i].m1_operand = 0xFF;
                c[i].m3_nameIndex = 0;
            }
        }
        c[kept++] = c[i];
    }
    if (kept != i) __builtin_memset(c + kept, 0, (32 - kept) * sizeof(*c));
    if (!name && hasAction && kept < 31) {
        __builtin_memmove(c + 1, c, kept * sizeof(*c));
        __builtin_memset(c, 0, sizeof(*c));
        c[0].m0_attackType = 12;
        c[0].m1_operand = 0xFF;
    }
}

/* An action list's targets: an attack's or an ability's (types 1, 2, 13),
 * else the first entry's (a type 15 animation also names the actor itself). */
static u16 targetMask(const sBattleScriptContext* c) {
    s32 k;
    u16 mask = 0;

    for (k = 0; k < 32 && mask == 0; k++) {
        if (c[k].m0_attackType == 1 || c[k].m0_attackType == 2 || c[k].m0_attackType == 13) {
            mask = c[k].m6_mask;
        }
    }
    for (k = 0; k < 32 && mask == 0; k++) {
        if (c[k].m0_attackType != 0) {
            mask = c[k].m6_mask;
        }
    }
    return mask;
}

static void targetLabel(u8* out, const sBattleScriptContext* c) {
    s32 i;
    s32 count = 0;
    s32 first = -1;
    u16 mask = targetMask(c);

    for (i = 0; i < 11; i++) {
        if (bitmaskCharacterCheck(mask, i)) {
            if (first < 0) {
                first = i;
            }
            count++;
        }
    }
    if (count == 1) {
        entityName(out, first);
    } else if (count > 1) {
        word(out, "ALL");
    } else {
#if VERSION_JP
        word(out, "NONE");
#else
        word(out, "None");
#endif
    }
}

/* The action list without its targets (the masks). */
static int sameAction(const sBattleScriptContext* a, const sBattleScriptContext* b) {
    s32 k;

    for (k = 0; k < 32; k++) {
        if (a[k].m0_attackType != b[k].m0_attackType || a[k].m1_operand != b[k].m1_operand ||
            a[k].m2_animationId != b[k].m2_animationId || a[k].m3_nameIndex != b[k].m3_nameIndex ||
            a[k].m4_valueLow != b[k].m4_valueLow || a[k].m5_valueHigh != b[k].m5_valueHigh) {
            return 0;
        }
    }
    return 1;
}

/* Labels that read the same get numbers (" 1", " 2"...): two attacks the
 * battle's strings do not name. */
static void numberSame(u8 (*l)[LABEL], s32 count) {
    u8 orig[MAX_CHOICES][LABEL];
    s32 i;
    s32 j;
    s32 n;
    s32 total;
    s32 len;

    __builtin_memcpy(orig, l, sizeof(orig[0]) * count);
    for (i = 0; i < count; i++) {
        total = 0;
        n = 0;
        for (j = 0; j < count; j++) {
            if (labelBytes(orig[i]) == labelBytes(orig[j]) && !__builtin_memcmp(orig[i], orig[j], labelBytes(orig[i]))) {
                total++;
                n += j <= i;
            }
        }
        len = labelBytes(l[i]);
        if (total > 1 && n < 10 && len + 3 < LABEL) {
            char number[3] = {' ', (char)('0' + n), 0};
            word(l[i] + len, number); /* the font's codes differ: JP digits are 0x18-0x21 */
        }
    }
}

/* ---- player 2's window (the battle dialogue window, as the retry prompt's,
 * port/src/game/battle/retryprompt.c) ---- */

#define WINDOW 0
#define TEXT_UNITS 0x26 /* 152 pixels */
#define WINDOW_Y 0x34

static DialogWindow18 window;
static int enemyRingShown;
static int enemyItemMenu;
static s32 shown;
static s32 choosing;
static s32 choosingPort;
static s32 partySelection;
static u8 text[(PAGE + 2) * (LABEL + 1) + 2];

void pcVersusPartySelection(int player) {
    partySelection = player;
}

int pcVersusEnemyRing(void) {
    return enemyRingShown;
}

int pcVersusEnemyItemMenu(void) {
    return enemyItemMenu;
}

int pcVersusEnemyItemPad2(void) {
    return enemyItemMenu && choosingPort == 1;
}

int pcVersusPrivateSelection(void) {
    int player = choosing ? choosingPort + 1 : partySelection;
    int viewer = pcVersusPerspective();
    return viewer != 0 && player != 0 && player != viewer;
}

/* drawBattleMode1/2 (src/battle/b_800728B8.c), after the dialogue windows. */
void pcVersusDraw(void) {
    pcVersusMirrorLabel(); /* a mirror match: player 2's name label (mirror.c) */
    if (shown) {
        /* The text has to finish printing on both peers before input goes
         * through. Keep its update and discard only the opponent's draw. */
        OTag hidden = 0;
        updateAndRenderTextForDialogWindow(&window, pcVersusPrivateSelection() ? &hidden : D_800CCB04.m0_pCurrentBattleOT + 1,
                                           D_800CCB04.m30_battleOddOrEven);
    }
}

/* The window, `count` lines of `units` 4-pixel units, at x; the text
 * `lines` in it (fewer lines than the window may have). */
static int openWindowSized(const u8* lines, s32 count, s32 units, s32 selected, s32 x) {
    s32 h = count * 16 + 12;
    s32 w = units * 4 + 0x18;
    if (x < 0) x = (0x140 - w) / 2;

    if (battleVar1 == NULL || battleVar1->mB0_isDialogWindowInitialized[WINDOW] ||
        battleVar1->mB8_isDialogWindowInitialized2[WINDOW]) {
        return 0;
    }
    allocateAndSetupBattleDialogWindow(WINDOW, x, WINDOW_Y, w, h, 1, 0);
    while (!battleVar1->mBF_isDialogWindowInitialized3[WINDOW]) {
        battleRenderDebugAndMain();
    }
    setupWindowSize2(&window, 0x380, 0x100, x + 0xC, WINDOW_Y + 8, units, count);
    window.m10_flags |= 2; /* no background tile: the window is the background */
    window.m68_charsPerStep = 0x20;
    resetDialogWindow18(&window);
    if (pcBacklogIgnore) pcBacklogIgnore(&window); /* the port's prompt: not the game's text */
    addDialogWindowsToOTSub2(&window, (u8*)lines);
    if (selected >= 0) {
        setupMultiChoiceColor(&window, 0xEF, 0x1E, 0xF0);
        setSelectedLineForMultiChoice(&window, selected);
    }
    shown = 1;
    return 1;
}

static int openWindow(const u8* lines, s32 count, s32 selected) {
    return openWindowSized(lines, count, TEXT_UNITS, selected, -1);
}

static void closeWindow(void) {
    if (shown) {
        shown = 0;
        dealocateDialogWindow18Memory(&window);
        clearBattleDialogWindow(WINDOW);
    }
}

static s32 append(s32 at, const u8* s) {
    s32 n = labelBytes(s);
    if (at + n < (s32)sizeof(text) - 1) {
        __builtin_memcpy(text + at, s, n);
        at += n;
    }
    return at;
}

/* The active player picks a label. The title is optional. Returns the index,
 * -1 on cancel and -2 if the window could not open. */
static s32 choose(const u8* title, u8 (*labels)[LABEL], s32 count, s32 canCancel) {
    s32 choice = 0;
    s32 first = -1;
    s32 held;
    s32 previous = pcPadHeldRemappedPort(choosingPort);
    s32 pressed;
    s32 i;
    s32 at;
    s32 shownCount;
    s32 units;
    s32 width;

    /* Measure rendered widths so the side window stays as narrow as its text. */
    u16 image[(0x49 + 3) * 13];
    const s32 firstChoiceLine = title != NULL;
    units = TEXT_UNITS;
    if (title != NULL) {
        width = renderString((u8*)title, image, 0x49, 0) / 4 + 2;
        if (width > units) units = width;
    }
    for (i = 0; i < count; i++) {
        width = renderString(labels[i], image, 0x49, 0) / 4 + 2;
        if (width > units) units = width;
    }

    for (;;) {
        if (choice < first || choice >= first + PAGE || first < 0) {
            /* (re)build the page that holds the choice */
            closeWindow();
            first = choice - choice % PAGE;
            shownCount = count - first < PAGE ? count - first : PAGE;
            at = title != NULL ? append(0, title) : 0;
            for (i = 0; i < shownCount; i++) {
                if (i != 0 || firstChoiceLine) text[at++] = 1;
                at = append(at, labels[first + i]);
            }
            text[at] = 0;
            {
                char hex[3 * sizeof(text)];
                s32 k;
                for (k = 0; k < at && k < (s32)sizeof(text); k++) {
                    hex[3 * k] = "0123456789abcdef"[text[k] >> 4];
                    hex[3 * k + 1] = "0123456789abcdef"[text[k] & 15];
                    hex[3 * k + 2] = ' ';
                }
                hex[3 * k] = 0;
                pcArenaLog("  window: %d lines: %s", shownCount + firstChoiceLine, hex);
            }
            if (!openWindowSized(text, shownCount + firstChoiceLine, units, choice - first + firstChoiceLine, 8)) {
                return -2;
            }
        }
        battleRenderDebugAndMain();
        held = pcPadHeldRemappedPort(choosingPort);
        pressed = held & ~previous;
        previous = held;
        if (!(window.m10_flags & 8)) {
            continue; /* still printing */
        }
        if (pressed & (BUTTON_UP | BUTTON_DOWN)) {
            choice = (choice + (pressed & BUTTON_UP ? count - 1 : 1)) % count;
            if (choice >= first && choice < first + PAGE) {
                setSelectedLineForMultiChoice(&window, choice - first + firstChoiceLine);
            }
            battleSoundEffect(0x4C);
        } else if (pressed & BUTTON_EXECUTE) {
            battleSoundEffect(0x4D);
            break;
        } else if ((pressed & BUTTON_CANCEL) && canCancel) {
            battleSoundEffect(0x4E);
            choice = -1;
            break;
        }
    }
    closeWindow();
    return choice;
}

/* ---- battleTickMain: an enemy's turn ---- */

static s32 enemyTurnResolved;

typedef struct {
    sBattleEntity foot, gear;
    sBattleVisualEntity footVisual;
    sBattleScriptEntity footScript, gearScript;
    sUnkMonsterStatus footStatus, gearStatus;
    u8 gearSaved;
} EnemyGearState;

static EnemyGearState enemyGear[ARENA_ROSTER_SLOTS];

void characterPerformDefendAction(u8 entity);
void clearEntityFlag15A_1(u8 entity);
void releaseSpriteActor(s32 entity);
void freeMecha_battle(s32 entity);
void createBattleSpriteActor(u32 entity, s32 visualBufferIndex, s32 animationId);
void initMechaForBattle(u32 entity);
void mechaInitEnvironmentMechaMesh(s32 entity, u16 flags, void* controls, void* model,
                                 s16 tpageX, s16 tpageY, s16 clutX, s16 clutY, SVECTOR* position);
extern sBattleMechaInitData* battleMechaInitData;

int pcVersusCallGearSetting(void);
int pcVersusExitGearSetting(void);
void callGearRelocateSlot(u8 slot);
void callGearSwapSprite(s32 slot);
void createGearLoadingFileRequests(s32 slot);
void pcVersusMirrorGearHud(int slot, int inGear);
void markEnemyDead(u8 slot);
void clearBattleAnimationData(void);
void sampleBattleTerrainHeight(SpriteActorCore* actor);
extern s32 battlePartyFileMapping[14][2];
extern sBattleSlotLayout battleSlotLayout[32];
extern u16 party1C_InitialValues[16];
extern u16 battleMechaTextureSlotsUsed;

static int partyGearRulesActive(void) {
    return pcVersusSession() == PC_VERSUS_IN && !pcArenaRosterEnemyTeams();
}

int pcVersusPartyGearTextureSlot(int slot) {
    return partyGearRulesActive() && slot >= 0 && slot < 3 ? slot : -1;
}

int pcVersusPartyExitEnabled(void) {
    return partyGearRulesActive() && (pcArenaRosterActive() ? pcArenaRosterExitGear() : pcVersusExitGearSetting());
}

static int partyGearSlot(int slot) {
    return slot >= 0 && (slot < 3 || pcVersusMirrorSlot(slot));
}

int pcVersusPartyCanExitGear(int slot) {
    return partyGearSlot(slot) && battleVisualEntities[slot].m4_isGear && pcVersusPartyExitEnabled();
}

static int partyCanCallGear(int slot) {
    sBattleEntity* entity = &battleEntities.m0_entities[slot];
    return !(battleConfigs[requestedBattleConfig * 0x20 + 1] & 0x40) &&
           (pcArenaRosterActive() ? pcArenaRosterCallGear() : pcVersusCallGearSetting()) &&
           entity->m0_base.mA0_gearNum < 20 && entity->mA4_gear.m60_hp > 0;
}

void pcVersusPartyGearMenu(int slot) {
    if (!partyGearSlot(slot) || !partyGearRulesActive()) return;
    sBattleVar2Sub* menu = &battleVar2->m0_entityMenus[slot];
    menu->m1C_isCommandEnabled[7] = partyCanCallGear(slot) ? 0 : party1C_InitialValues[7];
    if (pcVersusPartyCanExitGear(slot)) {
        menu->mC_extraCommandsMapping2[2] = battleCommandsSetups[0][7];
        menu->m1C_isCommandEnabled[8] = 0;
    }
}

static void setPartyGearForm(int slot, int inGear) {
    s32 i;
    apConfigArray[slot].m1_gearState = inGear ? 2 : 0;
    battleVisualEntities[slot].m4_isGear = inGear;
    if (inGear) battleEntities.m0_entities[slot].m15A_flags |= 0x80;
    else battleEntities.m0_entities[slot].m15A_flags &= ~0x80;
    battleVar2->m2EB_hpChanged[slot] = 1;
    if (slot >= 3) {
        pcVersusMirrorGearHud(slot, inGear);
        return;
    }
    battleVar0->m835C_apFuelLabels[slot].m1E1_barMode = inGear && battleCharacters[slot] != 7 ? 2 : 1;
    for (i = 0; i < 3; i++) {
        if (gameState.m1D34_currentParty[i] == battleCharacters[slot]) gameState.m22B1_isOnGear[i] = inGear;
    }
}

static void boardPartyGear(int slot) {
    updateCharacterBlinkingTask(0);
    markEnemyDead(slot); /* Free the old foot position before choosing a Gear position. */
    if (slot < 3) {
        callGearRelocateSlot(slot);
        callGearSwapSprite(slot);
    } else {
        sBattleVisualEntity* visual = &battleVisualEntities[slot];
        s32 position = slot - 3;
        sBattleSlotLayout* layout = &battleSlotLayout[0x18 + position];
        visual->m0_positionSlot = position;
        visual->m1_indexInSlot = 0;
        visual->mA_X = battleMechaInitData->m100_gearPositions[position][1].vx;
        visual->mC_Z = battleMechaInitData->m100_gearPositions[position][1].vy;
        layout->m0_count = layout->m1_mask = 1;
        releaseSpriteActor(slot);
        createGearLoadingFileRequests(slot);
        waitReadCompletion(0);
        finalizeBattleMechaLoadingCommands(slot, 0x140 + position * 64, 0x100, 0, slot + 0x1C0);
        initMechaForBattle(slot);
    }
    setPartyGearForm(slot, 1);
    setCameraVisibleEntities(-1);
    pcArenaLog("  player %d called Gear for slot %d", slot < 3 ? 1 : 2, slot);
}

static void dismountPartyGear(int slot) {
    s32 dir, sub, file, index;
    void* sprite;
    sBattleVisualEntity* visual;
    sBattleSlotLayout* layout;
    Vec2s16* position;
    SpriteActorCore* core;

    visual = &battleVisualEntities[slot];
    s32 character = visual->m2_characterId;
    u8* config = battleConfigs + requestedBattleConfig * 0x20;
    s32 secondSide = slot >= 3;
    s32 footSlot = config[secondSide ? 0x18 + slot - 3 : 4 + slot] & 7;
    layout = &battleSlotLayout[footSlot + 8 * secondSide];
    for (index = 0; layout->m1_mask & (1 << index); index++) {}

    updateCharacterBlinkingTask(0);
    markEnemyDead(slot); /* Release the Gear's position. */
    clearBattleAnimationData();
    freeMecha_battle(slot);
    if (secondSide) {
        freeMecha_battle(pcVersusGearPartSlot(slot, 0));
        freeMecha_battle(pcVersusGearPartSlot(slot, 1));
    } else {
        battleMechaTextureSlotsUsed &= ~(1u << slot);
    }
    releaseSpriteActor(slot);
    layout->m0_count++;
    layout->m1_mask |= 1 << index;
    visual->m0_positionSlot = footSlot;
    visual->m1_indexInSlot = index;
    position = secondSide ? &battleMechaInitData->m4_slotPositions[footSlot].mC_monsters[index]
                          : &battleMechaInitData->m4_slotPositions[footSlot].m0_party[index];
    visual->mA_X = position->vx;
    visual->mC_Z = position->vy;
    setPartyGearForm(slot, 0);

    /* Mirror sprites retain their file buffer when the Gear takes over. */
    if (!secondSide) {
        getCurrentDirectory(&dir, &sub);
        setCurrentDirectory(0x2C, 1);
        file = battlePartyFileMapping[character][0];
        sprite = allocateHeap(getFileSizeAligned(file), 0);
        readFile(file, sprite, 0, 0x80);
        waitReadCompletion(0);
        setCurrentDirectory(dir, sub);
        battleBssBlock.m8D24_visualBuffers[slot].m0_spriteData = sprite;
        battleBssBlock.m8D24_visualBuffers[slot].m8_vramParam = 0;
    }
    createBattleSpriteActor(slot, slot, 1);
    core = battleBssBlock.m8C8C_spriteActorCores[slot];
    core->m7C_extra->m0 = battlePartyFileMapping[character][1];
    freeHeap(core->m20_object->m2C_subSpriteBuffer);
    core->m20_object->m30_subSprites = core->m20_object->m2C_subSpriteBuffer = allocateHeap(0x300, 0);
    sampleBattleTerrainHeight(core);
    setCameraVisibleEntities(-1);
    pcArenaLog("  player %d exited Gear at slot %d", secondSide + 1, slot);
}

int pcVersusCallPartyGear(int slot) {
    if (!partyGearSlot(slot) || !partyGearRulesActive()) return 0;
    if (battleVisualEntities[slot].m4_isGear) {
        if (!pcVersusPartyCanExitGear(slot)) return 0;
        dismountPartyGear(slot);
    } else if (partyCanCallGear(slot)) {
        boardPartyGear(slot);
    }
    return 1;
}

int pcVersusEnemyTurnResolved(void) { return enemyTurnResolved; }

static int mountEnemyGear(u8 slot) {
    const u8* controls;
    const u8* model;
    const u8* data;
    const u16* scripts;
    sBattleVisualEntity* visual;
    sBattleEntity* entity;
    s32 x, position, side, bank, type;
    s32 i;
    u8 m = slot - 3;
    EnemyGearState* saved = &enemyGear[m];

    if (!pcArenaRosterGear(slot, &controls, &model, &x, &position)) return 0;
    side = pcArenaRosterSide(slot);
    pcArenaRosterGearActivated(slot);
    data = pcArenaRosterBank(slot, &bank, &type);
    scripts = (const u16*)(data + ((const u16*)data)[type]);
    visual = &battleVisualEntities[slot];
    entity = &battleEntities.m0_entities[slot];
    saved->foot = *entity;
    saved->footVisual = *visual;
    saved->footScript = monstersScriptsEntities[m];
    saved->footStatus = unknownMonsterStatus0[m];
    releaseSpriteActor(slot);
    __builtin_memcpy(entity, data + 0x32 + type * sizeof(*entity), sizeof(*entity));
    pcGameplayEnemyLoaded(entity, slot, bank, type);
    entity->mA4_gear.m60_hp = entity->mA4_gear.m64_maxHp;
    entity->m15A_flags |= 0x80;
    visual->m4_isGear = 0x80;
    visual->m0_positionSlot = position;
    visual->m1_indexInSlot = 0;
    visual->mA_X = battleMechaInitData->m100_gearPositions[position][side].vx;
    visual->mC_Z = battleMechaInitData->m100_gearPositions[position][side].vy;
    apConfigArray[slot].m1_gearState = 1;
    for (i = 0; i < 4; i++) {
        monstersScriptsEntities[m].m0_scripts[i] = scripts[i] == 0xFFFF ? NULL : (u8*)scripts + scripts[i];
    }
    unknownMonsterStatus0[m].m0_hasWhenAttackedScript = scripts[2] != 0xFFFF;
    unknownMonsterStatus0[m].m1_hasEndOfTurnScript = scripts[3] != 0xFFFF;
    __builtin_memset(monstersScriptsEntities[m].m10_s32Vars, 0, sizeof(monstersScriptsEntities[m].m10_s32Vars));
    __builtin_memset(monstersScriptsEntities[m].m20_s16Vars, 0, sizeof(monstersScriptsEntities[m].m20_s16Vars));
    __builtin_memset(monstersScriptsEntities[m].m30_s8Vars, 0, sizeof(monstersScriptsEntities[m].m30_s8Vars));
    if (saved->gearSaved) {
        *entity = saved->gear;
        monstersScriptsEntities[m] = saved->gearScript;
        unknownMonsterStatus0[m] = saved->gearStatus;
    }
    mechaInitEnvironmentMechaMesh(slot, 0x80, (void*)controls, (void*)model, x, 0x100, 0, slot + 0x1C0, NULL);
    initMechaForBattle(slot);
    pcArenaLog("  player %d mounted Gear %d.%d for slot %d", side + 1, bank, type, slot);
    return 1;
}

static void exitEnemyGear(u8 slot) {
    u8 m = slot - 3;
    EnemyGearState* saved = &enemyGear[m];
    saved->gear = battleEntities.m0_entities[slot];
    saved->gearScript = monstersScriptsEntities[m];
    saved->gearStatus = unknownMonsterStatus0[m];
    saved->gearSaved = 1;
    freeMecha_battle(slot);
    releaseSpriteActor(slot);
    battleEntities.m0_entities[slot] = saved->foot;
    monstersScriptsEntities[m] = saved->footScript;
    unknownMonsterStatus0[m] = saved->footStatus;
    battleVisualEntities[slot] = saved->footVisual;
    apConfigArray[slot].m1_gearState = 0;
    pcArenaRosterGearDeactivated(slot);
    int index = saved->footVisual.m2_characterId + 3;
    const void* sheet = pcArenaRosterFootSheet(slot);
    if (sheet != NULL) {
        uploadNpcSpriteSheet((s32*)sheet, battleBssBlock.m8D24_visualBuffers[index].m4_vramX, 0x100);
        DrawSync(0);
    }
    createBattleSpriteActor(slot, index, 1);
    pcArenaLog("  player %d exited Gear at slot %d", pcArenaRosterSide(slot) + 1, slot);
}

void pcVersusDeployMirrorGears(void) {
    int slot;
    if (!pcVersusMirror() || !pcVersusGears()) return;
    for (slot = 3; slot < 6; slot++) {
        if (pcVersusMirrorSlot(slot) && battleEntities.m0_entities[slot].m0_base.mA0_gearNum < 20 &&
            battleEntities.m0_entities[slot].mA4_gear.m60_hp > 0) boardPartyGear(slot);
    }
}

int pcVersusDeployEnemyGear(int slot) {
    return pcArenaRosterStartInGear() && mountEnemyGear(slot);
}

static u8 labels[MAX_CHOICES][LABEL];
static s32 actionOf[MAX_CHOICES]; /* the outcome each action choice stands for */

/* The acting monster's name label (battleTickMain: previousEntityTurn's
 * first, in dialogue window 5 as setupStringsForMonsterNames sets it up),
 * centred where drawMonsterNames centres it. */
#if VERSION_JP
#define NAME_CENTER 0x3A
#else
#define NAME_CENTER 0x40
#endif
#define NAME_X 8
#define NAME_W ((NAME_CENTER - NAME_X) * 2)
#define NAME_Y 0x2A
#define NAME_H 0x12

static s32 nameWide; /* window 5 widened for a long label */

/* The label: the game centres it on NAME_CENTER (drawMonsterNames); one
 * longer than the window gets leading spaces until it starts where the
 * window's text starts, and the window grows to the right for it. */
static void showLabel(const u8* s) {
    sMonsterTurnSub* l = &previousEntityTurn.m8_nameLabels[0];
    u8 text[LABEL + 40];
    s32 t = renderString((u8*)s, l->m58_ramBufferFromMonsterName, 0x39, 0);
    s32 w = t;
    s32 pad;
    s32 n;

    for (pad = 1; t + 16 > NAME_W && NAME_CENTER + w / 2 - t < NAME_X + 8 && pad < 40 && w < 0x39 * 4 - 8; pad++) {
        for (n = 0; n < pad; n++) {
            text[n] = 0x10; /* a space */
        }
        for (; s[n - pad] != 0 && n < (s32)sizeof(text) - 1; n++) {
            text[n] = s[n - pad];
        }
        text[n] = 0;
        w = renderString(text, l->m58_ramBufferFromMonsterName, 0x39, 0);
    }
    l->m5E_currentMonsterNameStringWidth = w;
    loadImageSync(&l->m50_monsterNameVRamRect, l->m58_ramBufferFromMonsterName);
    if (t + 16 > NAME_W) {
        n = NAME_CENTER + w / 2 + 8 - NAME_X; /* to the text's end, and a margin */
        drawBattleDialogWindow(5, NAME_X, NAME_Y, n > 0x140 - 2 * NAME_X ? 0x140 - 2 * NAME_X : n, NAME_H);
        nameWide = 1;
    } else if (nameWide) {
        drawBattleDialogWindow(5, NAME_X, NAME_Y, NAME_W, NAME_H);
        nameWide = 0;
    }
    battleVar1->mB0_isDialogWindowInitialized[5] = 1;
    l->m5D_monsterNameIsDisplayed = 1;
}

/* A monster script can show multiple names before hiding any of them.
 * Slot zero is the actor above the arena. The other labels share window 4. */
int pcVersusAttackNameRowsAfter(int label) {
    int rows = 0;
    int i;
    if (pcVersusSession() == PC_VERSUS_IN) {
        for (i = label + 1; i < 8; i++) {
            if (previousEntityTurn.m8_nameLabels[i].m5D_monsterNameIsDisplayed) rows++;
        }
    }
    return rows;
}

void pcVersusFitAttackNames(void) {
    int rows, extra;
    if (pcVersusSession() != PC_VERSUS_IN) return;
    rows = pcVersusAttackNameRowsAfter(0);
    extra = rows > 1 ? (rows - 1) * 13 : 0;
    drawBattleDialogWindow(4, 0x20, 0xC8 - extra, 0xF4, 0x12 + extra);
    battleVar1->mB0_isDialogWindowInitialized[4] = rows != 0;
}

static void hideLabel(void) {
    if (nameWide) {
        drawBattleDialogWindow(5, NAME_X, NAME_Y, NAME_W, NAME_H);
        nameWide = 0;
    }
    battleVar1->mB0_isDialogWindowInitialized[5] = 0;
    previousEntityTurn.m8_nameLabels[0].m5D_monsterNameIsDisplayed = 0;
}

int pcVersusActionPromptActive(void) {
    return choosing;
}

/* Mouse targeting stays with the party while player 2 owns a prompt. */
int pcVersusChoosing(void) {
    return choosing || pcVersusMirrorPad2();
}

/* Keep the battle's directional cursor for choosing individual targets. */
static const u16 directionBits[4] = {0x2000, 0x4000, 0x8000, 0x1000};

static s32 padPressed(s32* previous) {
    s32 held = pcPadHeldRemappedPort(choosingPort);
    s32 pressed = held & ~*previous;

    *previous = held;
    return pressed;
}

static s32 pressedDirection(s32 pressed) {
    s32 d;

    for (d = 0; d < 4 && !(pressed & directionBits[d]); d++) {
    }
    return d;
}

/* From target `cur` in direction d: the game's own step among the targets'
 * entities (spatial), else the next or the one before. */
static s32 stepTarget(s32 cur, s32 d, const u8* focus, s32 count, s32 spatial) {
    s32 i;
    u8 n;

    if (spatial) {
        n = selectNewSlotByDirection(focus[cur], d);
        for (i = 0; i < count; i++) {
            if (focus[i] == n) {
                return i;
            }
        }
        return cur;
    }
    return count < 2 ? cur : (cur + (d == 0 || d == 1 ? 1 : count - 1)) % count;
}

/* Player 2 picks one of `count` outcomes (targetOf, their targets maskOf):
 * the outcome, or -1 for cancel. */
static s32 pickTarget(u8 entity, const s32* targetOf, const u16* maskOf, s32 count, s32 preferred) {
    u8 focus[MAX_CHOICES];
    u8 reached[MAX_CHOICES];
    s32 queue[MAX_CHOICES];
    s32 spatial = count <= 11;
    s32 cur = -1;
    s32 shown = -1;
    s32 previous = pcPadHeldRemappedPort(choosingPort);
    s32 pressed;
    s32 head;
    s32 tail;
    s32 i;
    s32 j;
    s32 d;
    s32 n;

    /* each outcome's entity: the directions tell them apart when each has
     * one target, a different one */
    for (i = 0; i < count; i++) {
        focus[i] = 0xFF;
        for (j = 0; j < 11; j++) {
            if (bitmaskCharacterCheck(maskOf[i], j)) {
                if (focus[i] != 0xFF) {
                    spatial = 0; /* more than one */
                } else {
                    focus[i] = j;
                }
            }
        }
        for (j = 0; j < i; j++) {
            spatial &= focus[j] != focus[i];
        }
        spatial &= focus[i] != 0xFF;
        if (cur < 0 && preferred >= 0 && preferred < 11 && bitmaskCharacterCheck(maskOf[i], preferred)) {
            cur = i; /* the one the enemy faces first (the party's selectDefaultTarget) */
        }
    }
    if (cur < 0) {
        cur = 0;
    }
    if (spatial) {
        for (i = 0; i < 12; i++) {
            targetsPerPriority[i] = i < count ? focus[i] : 0xFF;
        }
        /* every target reachable from the first by the directions */
        __builtin_memset(reached, 0, sizeof(reached));
        reached[cur] = 1;
        queue[0] = cur;
        for (head = 0, tail = 1; head < tail; head++) {
            for (d = 0; d < 4; d++) {
                n = stepTarget(queue[head], d, focus, count, 1);
                if (!reached[n]) {
                    reached[n] = 1;
                    queue[tail++] = n;
                }
            }
        }
        spatial = tail == count;
    }
    pcArenaLog("  targets: %d, %s", count, spatial ? "by direction" : "in a row");
    initBattleG3Array(); /* the arrows (mC6_isTargetSelectionCursorVisible) */
    battleVar2->m2E9_targetConfirmed = 0;
    for (;;) {
        if (cur != shown) {
            updateCharacterBlinkingTask(maskOf[cur]);
            for (d = 0; d < 4; d++) {
                battleG3->mE6_isDirectionEnabled[d] = stepTarget(cur, d, focus, count, spatial) != cur;
            }
            shown = cur;
        }
        battleRenderDebugAndMain();
        pressed = padPressed(&previous);
        d = pressedDirection(pressed);
        if (d < 4) {
            battleSoundEffect(0x4C);
            cur = stepTarget(cur, d, focus, count, spatial);
        } else if (pressed & BUTTON_EXECUTE) {
            battleSoundEffect(0x4D);
            removeTargetSelectionCursors();
            return targetOf[cur];
        } else if (pressed & BUTTON_CANCEL) {
            battleSoundEffect(0x4E);
            removeTargetSelectionCursors();
            return -1;
        }
    }
}

enum { RING_ATTACK, RING_ITEM, RING_DEFEND, RING_UTILITY, RING_ABORT };

static s32 chooseCommand(u8 entity, int canAttack, int hasItems, int canCallGear, int canExitGear) {
    const int gear = canCallGear || canExitGear;
    const int onGear = apConfigArray[entity].m1_gearState != 0;
    sBattleVar2Sub* menu = &battleVar2->m0_entityMenus[entity];
    sBattleVar2Sub savedMenu = *menu;
    u8 savedNames[4];
    s32 choice = canAttack ? RING_ATTACK : RING_DEFEND;
    s32 shownChoice = -1;
    s32 previous = pcPadHeldRemappedPort(choosingPort);

    __builtin_memcpy(savedNames, battleVar2->m2C0_circleMenuCommandsNames, sizeof savedNames);
    /* Use the usual Attack, Item and Defend glyphs. The top slot is the PvP command. */
    u8* const names = onGear ? menu->m8_extraCommandsMapping : menu->m0_circleMenuBattleCommandsMapping;
    __builtin_memcpy(names, battleCommandsSetups[0], 3);
    names[RING_UTILITY] = gear ? battleCommandsSetups[0][7] : 0xFF;
    menu->m1C_isCommandEnabled[9] = !canAttack;
    menu->m1C_isCommandEnabled[11] = !hasItems;

    enemyRingShown = 1;

    pcArenaLog("  command ring for player %d (vblank %u)", choosingPort + 1, hwVSyncCount());
    for (;;) {
        if (choice != shownChoice) {
            battleVar1->mA3_641COddOrEven = updateBattleMenuSpriteForMenu(entity, (onGear ? 16 : 1) + choice, 0);
            battleVar1->mCB_drawCommandRing = 1;
            shownChoice = choice;
        }
        battleRenderDebugAndMain();
        s32 pressed = padPressed(&previous);
        if (pressed & (BUTTON_RIGHT | BUTTON_DOWN | BUTTON_LEFT | BUTTON_UP)) {
            if (pressed & BUTTON_RIGHT) choice = RING_ATTACK;
            else if (pressed & BUTTON_DOWN) choice = RING_ITEM;
            else if (pressed & BUTTON_LEFT) choice = RING_DEFEND;
            else if (gear) choice = RING_UTILITY;
            battleSoundEffect(0x4C);
        } else if (battleInputButton == 0xFF || !isControllerConnected(choosingPort)) {
            choice = RING_ABORT;
            break;
        } else if (pressed & BUTTON_EXECUTE) {
            if ((choice == RING_ATTACK && !canAttack) || (choice == RING_ITEM && !hasItems)) {
                battleSoundEffect(0x4F);
                continue;
            }
            battleSoundEffect(0x4D);
            break;
        }
    }
    battleVar1->mCB_drawCommandRing = 0;
    enemyRingShown = 0;
    *menu = savedMenu;
    __builtin_memcpy(battleVar2->m2C0_circleMenuCommandsNames, savedNames, sizeof savedNames);
    return choice;
}

/* Team membership, including two teams of monsters on the same physical
 * side of the retail slot array. 0x2000 permits manual retargeting to either
 * team; 0x1000 still tells us which team the command should open on. */
static u16 commandTargets(u8 entity, u16 flags, int attackRange, u8* preferred) {
    int preferredSide = pcArenaRosterSide(entity) ^ ((flags & 0x1000) != 0);
    int first = -1, best = -1, target;
    int current = battleVar2->m0_entityMenus[entity].m3C_currentTarget;
    u16 mask = 0;
    for (target = 0; target < 11; target++) {
        const sBattleEntity* e = &battleEntities.m0_entities[target];
        int side = pcArenaRosterSide(target);
        u16 status = apConfigArray[target].m1_gearState ? e->mA4_gear.m7C_statusFlags : e->m0_base.m7C_statusFlags[0];
        if (!isBattleSlotFilled.m0_isBattleSlotFilled[target] || battleVisualEntities[target].m3_hidden) continue;
        if ((flags & 0x4000) ? target != entity : (!(flags & 0x2000) && side != preferredSide)) continue;
        if ((flags & 0x8000) ? !(status & 0x8000) : (status & 0xC001)) continue;
        if (attackRange && !isTargetValid(entity, target)) continue;
        mask |= characterIdToTargetBitmask(target);
        if (first < 0) first = target;
        if (side == preferredSide && (best < 0 || target == ((flags & 0x1000) ? current : entity))) best = target;
    }
    *preferred = best >= 0 ? best : first >= 0 ? first : 0xFF;
    return mask;
}

int pcVersusInitCommandTargets(u8 entity, u16 flags, int attackRange) {
    int n = 0, target;
    if (pcVersusSession() != PC_VERSUS_IN) return 0;
    abilityTargetBitmask = commandTargets(entity, flags, attackRange, &abilitySelectedTarget);
    __builtin_memset(targetsPerPriority, 0xFF, sizeof targetsPerPriority);
    for (target = 0; target < 11; target++) {
        if (bitmaskCharacterCheck(abilityTargetBitmask, target)) targetsPerPriority[n++] = target;
    }
    numValidTarget = n;
    return 1;
}

/* The retail row numbers repeat across teams for custom formations. */
u16 pcVersusGroupTargets(u8 selected, u16 eligible, int rowOnly) {
    u16 group = 0;
    int i;
    for (i = 0; i < 11; i++) {
        if (pcArenaRosterSide(i) != pcArenaRosterSide(selected)) continue;
        if (rowOnly && battleVisualEntities[i].m0_positionSlot != battleVisualEntities[selected].m0_positionSlot) continue;
        group |= characterIdToTargetBitmask(i);
    }
    return eligible & group;
}

/* The custom picker polls held buttons. Let the regular battle input reader
 * consume that same press before handing it the inventory again. */
static void finishEnemyMenuPress(void) {
    while (pcPadHeldRemappedPort(choosingPort) & (BUTTON_EXECUTE | BUTTON_CANCEL)) {
        battleRenderDebugAndMain();
        if (battleInputButton == 0xFF || !isControllerConnected(choosingPort)) break;
    }
}

/* useItemInSlot supplies the regular menu and asks which PvP targets are legal. */
int pcVersusEnemyItemTarget(u8 entity, u16 flags) {
    if (!enemyItemMenu) return -1;

    u8 preferred;
    u16 mask = commandTargets(entity, flags, 0, &preferred);
    if (!mask) {
        battleSoundEffect(0x4F);
        return 0;
    }
    s32 targets[11];
    u16 masks[11];
    int count = 0;
    int candidate, i;
    for (candidate = 0; candidate < 11; candidate++) {
        if (!bitmaskCharacterCheck(mask, candidate)) continue;
        u16 choice = characterIdToTargetBitmask(candidate);
        if (flags & 7) {
            choice = pcVersusGroupTargets(candidate, mask, (flags & 7) == 2);
            for (i = 0; i < count && masks[i] != choice; i++) {}
            if (i < count) {
                if (candidate == preferred) targets[i] = preferred;
                continue;
            }
        }
        targets[count] = candidate;
        masks[count++] = choice;
    }
    u8 savedTargets[sizeof targetsPerPriority];
    sBattle800c3e24 savedCursor = *battleG3;
    u8 savedConfirmed = battleVar2->m2E9_targetConfirmed;
    __builtin_memcpy(savedTargets, targetsPerPriority, sizeof savedTargets);
    /* The target picker sounds its own input. Silence the native pad reader underneath it. */
    u8 inputSounds = startCharacterJumpToEnemyVar0;
    startCharacterJumpToEnemyVar0 = 0;
    int target = pickTarget(entity, targets, masks, count, preferred);
    finishEnemyMenuPress();
    startCharacterJumpToEnemyVar0 = inputSounds;
    __builtin_memcpy(targetsPerPriority, savedTargets, sizeof savedTargets);
    *battleG3 = savedCursor;
    battleVar2->m2E9_targetConfirmed = savedConfirmed;
    if (target < 0) {
        updateCharacterBlinkingTask(characterIdToTargetBitmask(entity));
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (targets[i] == target) abilityTargetBitmask = masks[i];
    }
    abilitySelectedTarget = target;
    return 1;
}

static s32 chooseEnemyItem(u8 entity, const u8* inventory) {
    sBattleItemList saved = battleEffectBF;
    u8 savedRow = selectedItemRow, savedColumn = selectedItemColumn;
    battleEffectBF.m0_flags = 0;
    __builtin_memset(battleEffectBF.m42_ids, 0, sizeof battleEffectBF.m42_ids);
    __builtin_memset(battleEffectBF.m12_counts, 0, sizeof battleEffectBF.m12_counts);
    int slot = 0, id;
    for (id = 1; id <= ARENA_ROSTER_ITEMS; id++) {
        if (inventory[id - 1]) {
            battleEffectBF.m42_ids[slot] = id;
            battleEffectBF.m12_counts[slot++] = inventory[id - 1];
        }
    }
    pcArenaLog("  native item screen for player %d (vblank %u)", choosingPort + 1, hwVSyncCount());
    /* Only the native inventory reads and sounds pad presses during this part of an enemy turn. */
    u8 inputSounds = startCharacterJumpToEnemyVar0;
    enemyItemMenu = 1;
    startCharacterJumpToEnemyVar0 = 0;
    finishEnemyMenuPress();
    startCharacterJumpToEnemyVar0 = 1;
    int selected = drawCircleMenuItem_update(entity) == 1;
    startCharacterJumpToEnemyVar0 = inputSounds;
    enemyItemMenu = 0;
    battleVar1->mCB_drawCommandRing = 0;
    if (selected) {
        int item = battleVar2->m2E6_menuSelection;
        slot = selectedItemRow * 2 + selectedItemColumn;
        pcArenaLog("  confirmed item %d on target %d (vblank %u)", item, abilitySelectedTarget, hwVSyncCount());
        choosing = 0;
        executeItem(entity);
        if (battleEffectBF.m12_counts[slot] < inventory[item - 1]) pcArenaRosterUseItem(choosingPort, item);
        enemyTurnResolved = 1;
        pcArenaLog("  player %d used item %d with slot %d, target %d, %d left",
                   choosingPort + 1, item, entity, abilitySelectedTarget, inventory[item - 1]);
    }
    battleEffectBF = saved;
    selectedItemRow = savedRow;
    selectedItemColumn = savedColumn;
    return selected;
}

/* In place of battleTickMain's executeMonsterScript (src/battle/b_80070E2C.c). */
void pcVersusMonsterTurn(u8 entity, s32 status) {
    u8 title[LABEL];
    s32 actions;
    s32 targets;
    s32 targetOf[MAX_CHOICES];
    u16 masks[MAX_CHOICES];
    s32 a;
    s32 t;
    s32 i;
    s32 j;
    s32 chosen = -1;
    s32 canCallGear = pcArenaRosterCallGear() && pcArenaRosterGear(entity, NULL, NULL, NULL, NULL);
    s32 canExitGear = pcArenaRosterCanExitGear(entity);
    const sBattleEntity* actor = &battleEntities.m0_entities[entity];

    enemyTurnResolved = 0;
    choosingPort = pcArenaRosterSide(entity);
    const u8* inventory = pcArenaRosterItemCounts(choosingPort);
    int hasItems = 0;
    if (inventory != NULL && !apConfigArray[entity].m1_gearState) {
        for (i = 0; i < ARENA_ROSTER_ITEMS; i++) hasItems |= inventory[i] != 0;
    }
    if (pcVersusSession() == PC_VERSUS_IN) clearEntityFlag15A_1(entity);
    if (pcVersusSession() != PC_VERSUS_IN || isControllerConnected(choosingPort) == 0 ||
        (actor->m0_base.m7C_statusFlags[0] & 0x2080) || (actor->m0_base.m7C_statusFlags[2] & 0x3000)) {
        executeMonsterScript(entity, status);
        resolveRosterTargets(entity);
        return;
    }
    probeTurn(entity);
    pcArenaLog("enemy %d's turn: %d outcomes in %d runs", entity, outcomeCount, runs);
    actions = 0;
    for (i = 0; i < outcomeCount && actions < MAX_CHOICES; i++) {
        for (j = 0; j < actions; j++) {
            if (sameAction(outcomes[actionOf[j]].ctx, outcomes[i].ctx)) {
                break;
            }
        }
        if (j == actions) {
            actionOf[actions++] = i;
        }
    }
    for (i = 0; i < outcomeCount; i++) {
        for (j = 0; j < 32; j++) { /* the attack the label describes (actionLabel): the first type 1 */
            if (outcomes[i].ctx[j].m0_attackType == 1 && outcomes[i].ctx[j].m1_operand < 200) {
                const sBattle800CDD40Sub* e = &battleEntities.m35D8_enemiesBattleStats[outcomes[i].ctx[j].m1_operand];
                pcArenaLog("  outcome %d's entry %d (context %d): flags %x type %d elements %x attackFlags %x power %d "
                           "effect %d formula %d/%d chance %d status %d/%x elementFlags %x hit %d/%d",
                           i, outcomes[i].ctx[j].m1_operand, j, e->m0_flags, e->m2_attackType, e->m8_elements,
                           (u16)e->mA_attackFlags, e->m11_power, e->m16_effectType, e->m1A_attackFormula,
                           e->m1B_defenceFormula, e->m1C_chance, e->m1D_statusType, e->m1E_statusMask,
                           e->m22_elementFlags, e->m15_hitBonus, e->m14_etherHitBonus);
                break;
            }
        }
        pcArenaLog("  outcome %d: type %d op %d anim %d name %d mask %x / type %d op %d anim %d name %d mask %x, %d draws", i,
                   outcomes[i].ctx[0].m0_attackType, outcomes[i].ctx[0].m1_operand, outcomes[i].ctx[0].m2_animationId,
                   outcomes[i].ctx[0].m3_nameIndex, outcomes[i].ctx[0].m6_mask, outcomes[i].ctx[1].m0_attackType,
                   outcomes[i].ctx[1].m1_operand, outcomes[i].ctx[1].m2_animationId, outcomes[i].ctx[1].m3_nameIndex,
                   outcomes[i].ctx[1].m6_mask, outcomes[i].planLen);
    }
    choosing = 1;
    while (chosen < 0) {
        updateCharacterBlinkingTask(characterIdToTargetBitmask(entity));
        s32 command = chooseCommand(entity, actions != 0, hasItems, canCallGear, canExitGear);
        if (command == RING_DEFEND) {
            updateCharacterBlinkingTask(0);
            choosing = 0;
            characterPerformDefendAction(entity);
            enemyTurnResolved = 1;
            pcArenaLog("  player %d defended with slot %d", choosingPort + 1, entity);
            return;
        }
        if (command == RING_ITEM) {
            if (chooseEnemyItem(entity, inventory)) return;
            continue;
        }
        if (command == RING_UTILITY && (canCallGear || canExitGear)) {
            updateCharacterBlinkingTask(0);
            choosing = 0;
            if (canCallGear) mountEnemyGear(entity);
            else exitEnemyGear(entity);
            setCameraVisibleEntities(-1);
            enemyTurnResolved = 1;
            return;
        }
        if (command != RING_ATTACK) break;

        for (i = 0; i < actions; i++) {
            actionLabel(labels[i], entity, outcomes[actionOf[i]].ctx);
        }
        numberSame(labels, actions);
        pcArenaLog("  attack selector: %d actions (vblank %u)", actions, hwVSyncCount());
        a = choose(NULL, labels, actions, 1);
        if (a == -2) break;
        if (a < 0) continue;
        pcArenaLog("  selected action %d", a);
        __builtin_memcpy(title, labels[a], LABEL);
        targets = 0;
        for (i = 0; i < outcomeCount && targets < MAX_CHOICES; i++) {
            if (sameAction(outcomes[actionOf[a]].ctx, outcomes[i].ctx)) {
                targetLabel(labels[targets], outcomes[i].ctx);
                masks[targets] = targetMask(outcomes[i].ctx);
                targetOf[targets++] = i;
            }
        }
        if (targets > 1 && battleG3 != NULL) {
            u8 savedTargets[sizeof(targetsPerPriority)];
            sBattle800c3e24 savedCursor = *battleG3;
            u8 savedArrows = battleVar1->mC6_isTargetSelectionCursorVisible;
            u8 savedConfirmed = battleVar2->m2E9_targetConfirmed;
            __builtin_memcpy(savedTargets, targetsPerPriority, sizeof(savedTargets));
            chosen = pickTarget(entity, targetOf, masks, targets, battleVar2->m0_entityMenus[entity].m3C_currentTarget);
            __builtin_memcpy(targetsPerPriority, savedTargets, sizeof(savedTargets));
            *battleG3 = savedCursor;
            battleVar1->mC6_isTargetSelectionCursorVisible = savedArrows;
            battleVar2->m2E9_targetConfirmed = savedConfirmed;
            continue;
        }
        numberSame(labels, targets);
        t = targets > 1 ? choose(title, labels, targets, 1) : 0;
        if (t == -2) {
            break;
        }
        if (t >= 0) {
            chosen = targetOf[t];
        }
    }
    updateCharacterBlinkingTask(0);
    choosing = 0;
    if (chosen < 0) {
        pcArenaLog("  the game's own draws");
        executeMonsterScript(entity, status);
        resolveRosterTargets(entity);
        return;
    }
    pcArenaLog("  player %d chose outcome %d", choosingPort + 1, chosen);
    __builtin_memcpy(plan, outcomes[chosen].plan, sizeof(plan));
    planLen = outcomes[chosen].planLen;
    playRun(entity, &outcomes[chosen]);
    if (__builtin_memcmp(battleScriptContext, outcomes[chosen].ctx, sizeof(battleScriptContext)) != 0) {
        pcArenaLog("  the turn's actions differ from the chosen outcome (an op the probe passes over)");
    }
    resolveMoveCaptions(battleScriptContext, entity, title);
}

/* ---- battleMain: the formation, the end ---- */

int pcArenaApplyMatchParty(void);

/* battleMain, before it copies its config: a versus battle's checkpoint and
 * formation (as battleDebugSelector loads a table). */
void pcVersusBattleSetup(void) {
    s32 i;
    s32 c;
    s32 dir;
    s32 sub;
    s32 file;
    s32 size;
    u8* buf;

    if (!pcVersusCheckpoint()) {
        pcVersusMirrorSetup(); /* not a versus battle: no mirror match either */
        return;
    }
    getCurrentDirectory(&dir, &sub);
    setCurrentDirectory(0x20, 3);
    file = pcVersusTable() + 7;
    size = getFileSizeAligned(file);
    buf = allocateBattleHeap(size, 1);
    readFile(file, buf, 0, 0x80);
    waitReadCompletion(0);
    __builtin_memcpy(battleConfigs, buf, 0x200);
    freeHeap(buf);
    setCurrentDirectory(dir, sub);
    requestedBattleConfig = pcVersusConfig();
    if (pcArenaApplyMatchParty()) {
        /* A story formation can force a substitute Gear party. */
        battleConfigs[requestedBattleConfig * 0x20 + 1] &= ~0x10;
    }
    if (pcArenaRosterConfig(battleConfigs + requestedBattleConfig * 0x20)) {
        __builtin_memset(enemyGear, 0, sizeof enemyGear);
        int ready = pcArenaRosterStartInGear();
        for (i = 0; i < 3; i++) {
            c = gameState.m1D34_currentParty[i];
            gameState.m22B1_isOnGear[i] = ready && c != 0xFF && gameState.m26C_party[c].mA0_gearNum != 0xFF;
        }
        return;
    }
    pcVersusMirrorSetup(); /* a mirror match: the copy's places in the formation (mirror.c) */
    if (pcVersusGears() && !(battleConfigs[requestedBattleConfig * 0x20 + 1] & 0x40)) {
        /* Gear against Gear: the members with a Gear ride it (the battle loader
         * takes m22B1_isOnGear; config flag 0x40 is a battle without Gears) */
        for (i = 0; i < 3; i++) {
            c = gameState.m1D34_currentParty[i];
            gameState.m22B1_isOnGear[i] = c != 0xFF && gameState.m26C_party[c].mA0_gearNum != 0xFF;
        }
    }
    pcArenaLog("versus battle: table %d config %d: set %d, enemies %d,%d,%d,%d,%d,%d,%d,%d",
               pcVersusTable(), pcVersusConfig(), battleConfigs[requestedBattleConfig * 0x20],
               battleConfigs[requestedBattleConfig * 0x20 + 8], battleConfigs[requestedBattleConfig * 0x20 + 9],
               battleConfigs[requestedBattleConfig * 0x20 + 10], battleConfigs[requestedBattleConfig * 0x20 + 11],
               battleConfigs[requestedBattleConfig * 0x20 + 12], battleConfigs[requestedBattleConfig * 0x20 + 13],
               battleConfigs[requestedBattleConfig * 0x20 + 14], battleConfigs[requestedBattleConfig * 0x20 + 15]);
}

/* The native sprite task frees its input after transferring it to VRAM. */
void* pcVersusRosterVisuals(void* stock) {
    s32 bytes;
    const u8* packed = pcArenaRosterVisuals(&bytes);
    u8* native;
    if (packed == NULL) return stock;
    freeHeap(stock);
    native = allocateBattleHeap(bytes, 1);
    __builtin_memcpy(native, packed, bytes);
    return native;
}

void pcVersusRosterHideParty(void) {
    s32 i;
    if (!pcArenaRosterEnemyTeams()) return;
    for (i = 0; i < 3; i++) {
        battleVisualEntities[i].m3_hidden = 1;
        battleVisualEntities[i].m2_characterId = 0x7F;
        isBattleSlotFilled.m0_isBattleSlotFilled[i] = 0;
    }
}

static s16 gearPositionInBounds(s32 value, s16 min, s16 max) {
    if (min < max) {
        if (value < min) return min;
        if (value > max) return max;
    }
    return (s16)value;
}

/* Foot-only stages can have empty Gear slots. Place those behind their own
 * side's foot formation, in staggered rows along the stage's line of battle. */
static void placeMissingGearPositions(Vec2s16 party, Vec2s16 enemies) {
    static const s32 lanes[] = {0, -1, 1};
    sBattleMechaInitData* stage = battleMechaInitData;
    s32 dx = party.vx - enemies.vx;
    s32 dz = party.vy - enemies.vy;
    s32 absX = dx < 0 ? -dx : dx;
    s32 absZ = dz < 0 ? -dz : dz;
    s32 span = absX > absZ ? absX : absZ;
    s32 row, side;

    if (span == 0) {
        dx = 1;
        span = 1;
    }
    dx = dx * 256 / span;
    dz = dz * 256 / span;
    for (row = 0; row < 8; row++) {
        s32 lane = lanes[row % 3];
        s32 depth = 1 + row / 3;
        for (side = 0; side < 2; side++) {
            Vec2s16* gear = &stage->m100_gearPositions[row][side];
            if (gear->vx != 0 || gear->vy != 0) continue;
            Vec2s16 anchor = side == 0 ? party : enemies;
            s32 sign = side == 0 ? 1 : -1;
            s32 x = anchor.vx + sign * dx * depth - dz * lane;
            s32 z = anchor.vy + sign * dz * depth + dx * lane;
            gear->vx = gearPositionInBounds(x, stage->m34E_minX, stage->m34C_maxX);
            gear->vy = gearPositionInBounds(z, stage->m350_minZ, stage->m352_maxZ);
        }
    }
}

/* Move the native deployment rows away from each other on the stage's
 * existing line of battle. The Gear rows use the same offset so Call Gear
 * and the return from an attack still use the wider formation. */
void pcVersusRosterSpaceTeams(void) {
    u8 party[3], enemies[3];
    sBattleSlotPositions* slots;
    sBattleSlotPositions* enemyRow;
    Vec2s16 a, b;
    s32 dx, dz, row, i;
    unsigned rows = 0;

    if (pcVersusSession() != PC_VERSUS_IN) return;
    slots = battleMechaInitData->m4_slotPositions;
    if (!pcArenaRosterActive()) {
        if (!pcVersusGears() && !pcVersusCallGearSetting()) return;
        const u8* config = battleConfigs + requestedBattleConfig * 0x20;
        if (pcVersusMirror() && config[2] == 71) {
            /* The Ethos transport has no native Gear positions. Keep the
             * party Gears on the broad, rear half of the deck, clear of the
             * front edge where the generic staggered rows put their feet. */
            static const Vec2s16 deck[3][2] = {
                {{780, 128}, {150, 128}},
                {{842, 76}, {250, 128}},
                {{715, 54}, {332, 51}},
            };
            __builtin_memcpy(battleMechaInitData->m100_gearPositions, deck, sizeof deck);
            return;
        }
        s32 partyRow = config[4] & 0x7F;
        if (partyRow >= 7) return;
        for (i = 0; i < 8; i++) {
            s32 opponentRow = config[0x18 + i] & 0x7F;
            if ((config[8 + i] & 0x7F) == 0x7F || opponentRow >= 7) continue;
            placeMissingGearPositions(slots[partyRow].m0_party[0], slots[opponentRow].mC_monsters[0]);
            break;
        }
        return;
    }
    if (pcVersusStage(party, enemies) < 0) return;
    enemyRow = &slots[enemies[0] & 0x7F];
    a = slots[party[0] & 0x7F].m0_party[0];
    b = enemyRow->mC_monsters[0];
    if (pcArenaRosterCallGear() || pcArenaRosterStartInGear()) placeMissingGearPositions(a, b);
    dx = (a.vx - b.vx) / 10;
    dz = (a.vy - b.vy) / 10;
    for (i = 0; i < 3; i++) rows |= 1u << (party[i] & 0x7F);
    for (row = 0; row < 7; row++) {
        if (!(rows & (1u << row))) continue;
        for (i = 0; i < 3; i++) {
            slots[row].m0_party[i].vx += dx;
            slots[row].m0_party[i].vy += dz;
        }
    }
    for (i = 0; i < 5; i++) {
        enemyRow->mC_monsters[i].vx -= dx;
        enemyRow->mC_monsters[i].vy -= dz;
    }
    for (row = 0; row < 8; row++) {
        Vec2s16* gear = battleMechaInitData->m100_gearPositions[row];
        gear[0].vx += dx;
        gear[0].vy += dz;
        gear[1].vx -= dx;
        gear[1].vy -= dz;
    }
    pcArenaLog("custom arena: deployment anchors (%d,%d) and (%d,%d), each moved outward by (%d,%d)",
               a.vx, a.vy, b.vx, b.vy, dx, dz);
}

/* Retail story scripts can force a turn on a fixed slot or modify the
 * encounter's commands and layout. A custom match keeps the agreed setup.
 * PC selectors mean the opposing team in an enemy match. */
int pcVersusRosterTargetOp(const u8* op, int actor) {
    s32 other;
    s32 gear = 0;
    s32 rowMode = 0;
    s32 all = 0;
    s32 countOnly = 0;
    s32 i;
    s32 n = 0;
    u16 pool[8];
    u16 mask = 0;

    if (!pcArenaRosterActive()) return 0;
    if (op[0] >= 0x70 && op[0] <= 0x74) return 1;
    if (!pcArenaRosterEnemyTeams()) return 0;
    switch (op[0]) {
    case 0x40: other = 1; break;
    case 0x41: other = 1; rowMode = 1; break;
    case 0x42: other = 0; rowMode = 1; break;
    case 0x43: other = 1; rowMode = -1; break;
    case 0x44: other = 0; rowMode = -1; break;
    case 0x49: other = 1; rowMode = 1; gear = 1; break;
    case 0x4A: other = 1; gear = 1; break;
    case 0x4B: other = 0; gear = 1; break;
    case 0x58: case 0x59:
        other = op[0] == 0x58; gear = -1; countOnly = 1; break;
    case 0x65: case 0x66:
        other = op[0] == 0x65; gear = -1; all = 1; break;
    default:
        return 0;
    }
    if (op[1] >= (countOnly ? 16 : 8)) return 1;
    for (i = 3; i < 11; i++) {
        s32 own = pcArenaRosterSide(actor + 3) == pcArenaRosterSide(i);
        s32 sameRow = battleVisualEntities[actor + 3].m0_positionSlot == battleVisualEntities[i].m0_positionSlot;
        s32 inGear = battleVisualEntities[i].m4_isGear != 0;
        s32 status = inGear ? battleEntities.m0_entities[i].mA4_gear.m7C_statusFlags
                            : battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0];
        if (own == other || (gear >= 0 && gear != inGear) ||
            (rowMode != 0 && sameRow != (rowMode > 0)) ||
            !isBattleSlotFilled.m0_isBattleSlotFilled[i] ||
            battleVisualEntities[i].m3_hidden || (status & 0xC002)) continue;
        pool[n++] = characterIdToTargetBitmask(i);
        mask |= pool[n - 1];
    }
    if (countOnly) monstersScriptsEntities[actor].m30_s8Vars[op[1]] = n;
    else monstersScriptsEntities[actor].m20_s16Vars[op[1]] = all ? mask : n == 0 ? 0 : pool[randomInRange(0, n - 1)];
    return 1;
}

int pcVersusRosterResult(unsigned mask) {
    s32 i;
    s32 alive = 0;
    for (i = 3; i < 11; i++) {
        if (bitmaskCharacterCheck(mask, i)) alive |= 1 << pcArenaRosterSide(i);
    }
    return (alive & 1) == 0 ? 0x81 : (alive & 2) == 0 ? 1 : 0;
}

static u8 lastResult; /* pcVersusLastResult */

/* The last versus battle's end (battleRunningVar1 as the battle left it):
 * 0x81 player 2 won, 0x40 bit player 1 escaped, else player 1 won; 0 none. */
int pcVersusLastResult(void) {
    return lastResult;
}

void softReboot(void); /* src/main/kernel.c */

/* A match's end's Rematch / Leave (port/src/game/main/arena.c), in the
 * result's window (xg-htn.35.150): under the result, a line for each player,
 * their pick between the two in brackets (the narrow font is 8 pixels to a
 * character, so nothing moves), and OK once they are ready:
 *   Player 1 wins
 *   Player 1  [Rematch]  Leave
 *   Player 2   Rematch  [Leave]  OK
 * (JP, whose narrow font lacks some letters: 1P, 2P, RETRY, EXIT, READY).
 * The lines are printed again when a pick or a ready changes, at once: the
 * printer takes a line a step (a new line ends its step) and clears the
 * window first, so its steps are run here, where the change is seen, into
 * an ordering table nothing draws (the frame's own draw, pcVersusDraw, then
 * shows the whole text), not one line a frame after a blank one. */
#if VERSION_JP
#define PROMPT_REMATCH "RETRY"
#define PROMPT_LEAVE "EXIT"
#define PROMPT_READY "  READY"
#define PROMPT_P1 "1P" /* the result above already reads PLAYER 1 */
#define PROMPT_P2 "2P"
#define PROMPT_CHARS 25 /* the widest line, ready: 2 + 11 + 5 + 7 */
#else
#define PROMPT_REMATCH "Rematch"
#define PROMPT_LEAVE "Leave"
#define PROMPT_READY "  OK"
#define PROMPT_P1 "Player 1"
#define PROMPT_P2 "Player 2"
#define PROMPT_CHARS 31 /* the widest line, ready: 8 + 13 + 6 + 4 */
#endif
#define PROMPT_UNITS (PROMPT_CHARS * 2 + 1) /* narrow characters are 2 units (getDialogFontCharacterWidth) */

static u8 promptText[3 * (PROMPT_CHARS + 1) + LABEL + 2];
static s32 promptShownPick[2];
static s32 promptShownReady[2];

/* One piece of a line, in the font's codes. */
static s32 promptPiece(s32 at, const char* s) {
    u8 piece[LABEL];
    s32 i;

    word(piece, s);
    for (i = 0; piece[i] != 0 && at < (s32)sizeof(promptText) - 2; i++) {
        promptText[at++] = piece[i];
    }
    return at;
}

/* The window's text: the result, then each player's line. */
static void promptBuild(const u8* result, const s32* pick, const s32* ready, s32 players) {
    s32 at = 0;
    s32 p;

    while (result[at] != 0 && at < LABEL) {
        promptText[at] = result[at];
        at++;
    }
    for (p = 0; p < players; p++) {
        promptText[at++] = 1; /* a new line */
        at = promptPiece(at, p == 0 ? PROMPT_P1 : PROMPT_P2);
        at = promptPiece(at, pick[p] == 0 ? "  [" PROMPT_REMATCH "]  " : "   " PROMPT_REMATCH "  [");
        at = promptPiece(at, pick[p] == 1 ? PROMPT_LEAVE "]" : PROMPT_LEAVE " ");
        if (ready[p]) {
            at = promptPiece(at, PROMPT_READY);
        }
        promptShownPick[p] = pick[p];
        promptShownReady[p] = ready[p];
    }
    promptText[at] = 0;
}

/* The window again with the lines as they are now, when they changed and
 * the last text is printed. */
static void promptUpdate(const u8* result) {
    OTag scratch = 0; /* the steps' sprites go here; the frame's draw links them again */
    s32 pick[2];
    s32 ready[2];
    s32 players;
    s32 p;
    s32 changed = 0;

    if (!shown || pcArenaPromptState(pick, ready, &players) != 1 || !(window.m10_flags & 8)) {
        return;
    }
    for (p = 0; p < players; p++) {
        changed |= pick[p] != promptShownPick[p] || ready[p] != promptShownReady[p];
    }
    if (!changed) {
        return;
    }
    promptBuild(result, pick, ready, players);
    resetDialogWindow18(&window);
    addDialogWindowsToOTSub2(&window, promptText);
    for (p = 0; p < 8 && !(window.m10_flags & 8); p++) {
        updateAndRenderTextForDialogWindow(&window, &scratch, D_800CCB04.m30_battleOddOrEven);
    }
}

/* A match's end (the header): Rematch or Leave, the battle drawn meanwhile;
 * it leaves only by the soft reset or the platform's (never returns). */
static void matchPrompt(const u8* result) {
    s32 sound;
    s32 d;

    pcArenaPromptBegin();
    promptShownPick[0] = promptShownPick[1] = -1; /* printed at once */
    for (;;) {
        promptUpdate(result);
        battleRenderDebugAndMain();
        d = pcArenaPromptStep(&sound);
        if (sound == PC_PROMPT_SOUND_CONFIRM) {
            battleSoundEffect2(0x4D);
        } else if (sound != PC_PROMPT_SOUND_NONE) {
            battleSoundEffect2(0x4C);
        }
        if (d == PC_PROMPT_REMATCH) {
            pcArenaMatchRematch();
            softReboot();
        } else if (d == PC_PROMPT_LEAVE) {
            pcArenaMatchLeave(); /* the game is taken back at a vblank: the battle draws on until then */
        }
    }
}

/* battleMain, when its loop ends: who won, then the end that gives nothing
 * (0x21, a won battle without rewards) or costs nothing (0x40, an escape's
 * end, never the game over); in a match, Rematch / Leave first. */
void pcVersusBattleOver(void) {
    u8 r;
    u8 line[LABEL];
    s32 frames;
    s32 previous;
    s32 held;
    s32 opened;

    if (pcVersusSession() != PC_VERSUS_IN) {
        return;
    }
    r = performAttackSub3_var0.m2_battleRunningVar1;
    lastResult = r;
#if VERSION_JP
    word(line, r == 0x81 ? "PLAYER 2" : (r & 0x40) ? "ESCAPE" : "PLAYER 1");
#else
    word(line, r == 0x81 ? "Player 2 wins" : (r & 0x40) ? "Player 1 escaped" : "Player 1 wins");
#endif
    line[LABEL - 1] = 0;
    pcArenaLog("versus battle over: window 0 %s", battleVar1 != NULL && !battleVar1->mB0_isDialogWindowInitialized[WINDOW] &&
                                                     !battleVar1->mB8_isDialogWindowInitialized2[WINDOW] ? "free" : "busy");
    if (pcArenaMatchOn()) {
        /* the window for the prompt's lines too (matchPrompt); a whole line a step */
        opened = openWindowSized(line, 1 + (pcArenaMatchOn() == 2 || isControllerConnected(1) ? 2 : 1), PROMPT_UNITS, -1, -1);
        if (opened) {
            window.m68_charsPerStep = 0xFF;
        }
    } else {
        opened = openWindow(line, 1, -1);
    }
    if (opened) {
        previous = pcPadHeldRemappedPort(0) | pcPadHeldRemappedPort(1);
        for (frames = 0; frames < 180; frames++) {
            battleRenderDebugAndMain();
            held = pcPadHeldRemappedPort(0) | pcPadHeldRemappedPort(1);
            if ((held & ~previous & BUTTON_CONFIRM) && (window.m10_flags & 8) && frames >= 60) {
                break; /* a confirm closes it after a second */
            }
            previous = held;
        }
    }
    if (pcArenaMatchOn()) {
        pcArenaLog("versus battle over: %s", r == 0x81 ? "player 2 won" : (r & 0x40) ? "player 1 escaped" : "player 1 won");
        matchPrompt(line); /* the result stays up above it */
    }
    if (opened) {
        closeWindow();
    }
    performAttackSub3_var0.m2_battleRunningVar1 = r == 0x81 || (r & 0x40) ? 0x40 : 0x21;
    pcArenaLog("versus battle over: result 0x%x, ends as 0x%x", r, performAttackSub3_var0.m2_battleRunningVar1);
}

/* battleMain's end, after finishBattle: everything as the battle found it. */
void pcVersusBattleEnd(void) {
    if (pcVersusSession() == PC_VERSUS_IN) {
        pcArenaLog("versus battle: everything back");
    }
    pcVersusMirrorEnd();
    pcVersusRestore();
}

/* ---- for the mirror match (mirror.c, versus.h) ---- */

void pcVersusWord(u8* out, const char* s) {
    word(out, s);
}

void pcVersusGameText(u8* out, const u8* s, s32 max) {
    gameText(out, s, max);
}

void pcVersusShowLabel(const u8* s) {
    showLabel(s);
}

void pcVersusHideLabel(void) {
    hideLabel();
}
