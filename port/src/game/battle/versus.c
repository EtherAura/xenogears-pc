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
 * enemy can do now as its script allows it now (its phases, counters, HP
 * tests...): the action lists it can build. Player 2 picks one of the actions
 * (named as the game names them: the attack's name from the battle's
 * strings), then one of its targets, in the battle's dialogue window on pad 2;
 * then the turn's script runs for real (every op, as executeMonsterScript
 * runs it) with the draws that make that choice. "Auto" leaves the turn to
 * the game's random draws; so does a turn with only one outcome, and a
 * missing pad 2. The enemies' reactions (when attacked, at the end of the
 * party's turn) stay the game's.
 *
 * The end: whoever wins, a line in the same window says so ("Player 1 wins",
 * "Player 2 wins"), then the battle ends the way that gives nothing and costs
 * nothing: battleRunningVar1 0x21 for a party that won (the victory without
 * rewards: finishBattle skips processBattleRewards, battle_boot returns to
 * the field), 0x40 for a wiped party (an escape's end, never the game over);
 * then the checkpoint goes back (pcVersusBattleEnd, battleMain's end).
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

s32 battleRenderDebugAndMain(void);
u8 executeMonsterScriptLower(sBattleScriptExecutionContext* ctx, u8 entity, u8 index);
u8 executeMonsterScriptUpper(sBattleScriptExecutionContext* ctx, u8 entity);
void skipScriptIfFalse(sBattleScriptExecutionContext* ctx);
void advanceBattleScript(sBattleScriptExecutionContext* ctx);
void executeMonsterScript(u8 entity, s32 unused);
u16 bitmaskCharacterCheck(u16 mask, u8 i);
void* allocateBattleHeap(s32 size, s32 flags);
s32 getCurrentDirectory(s32* directory, s32* offset);
s32 isControllerConnected(s32 port);
s32 pcPadHeldRemappedPort(s32 port); /* src/main/pad.c */
u32 portRandSeed(void);               /* port/src/system/libc.c */
void srand(u32 seed);
extern u8* currentBattleSpecificStrings;
extern u8 battleMonsterMapping[11];
extern u8 battleConfigs[0x220];
extern u8 requestedBattleConfig;

#define MAX_DRAWS 24     /* draws a turn's script makes (a target op redraws) */
#define MAX_RUNS 6000    /* probe runs a turn */
#define MAX_OUTCOMES 48  /* distinct action lists a turn */
#define MAX_CHOICES 24
#define LABEL 24         /* game-text bytes of a label */
#if VERSION_JP
#define PAGE 2           /* choices shown at once: the JP window shows no fourth line (measured) */
#else
#define PAGE 6           /* choices shown at once */
#endif

#define BUTTON_UP 0x1000
#define BUTTON_DOWN 0x4000
#define BUTTON_CONFIRM 0x20 /* remapped as the battle reads it */
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
} Outcome;

static Outcome outcomes[MAX_OUTCOMES];
static s32 outcomeCount;
static s32 runs;
static sBattleScriptEntity probeVars;

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

/* The turn's script once, as executeMonsterScript runs it, without the
 * ops that write elsewhere; the action list is left in battleScriptContext. */
static void probeRun(u8 m) {
    sBattleScriptExecutionContext ctx;
    u8 index = 0;
    s32 steps = 0;

    monstersScriptsEntities[m] = probeVars;
    __builtin_memset(battleScriptContext, 0, sizeof(battleScriptContext));
    drawAt = 0;
    ctx.m0_scriptPtr = monstersScriptsEntities[m].m0_scripts[0];
    while (*ctx.m0_scriptPtr != 0xFD && *ctx.m0_scriptPtr != 0xFF && steps++ < 4096) {
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
    runs++;
}

static void addOutcome(s32 depth) {
    s32 i;

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
    outcomeCount++;
}

/* Every value of draw `depth` (at most 256, spread over a wider range), each
 * walked further. */
static void explore(u8 m, s32 depth) {
    s32 lo;
    s32 hi;
    s32 step;
    s32 v;

    if (runs >= MAX_RUNS) {
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

static void probeTurn(u8 entity) {
    u8 m = entity - 3;
    u32 seed = portRandSeed();

    probeVars = monstersScriptsEntities[m];
    outcomeCount = 0;
    runs = 0;
    planMode = PLAN_PROBE;
    explore(m, 0);
    planMode = PLAN_OFF;
    monstersScriptsEntities[m] = probeVars;
    __builtin_memset(battleScriptContext, 0, sizeof(battleScriptContext));
    srand(seed); /* no draw reached rand(), but keep the stream as it was */
}

/* The turn's script for real, as executeMonsterScript runs it (every op),
 * the draws from the plan of player 2's choice. */
static void playRun(u8 entity) {
    sBattleScriptExecutionContext ctx;
    u8 m = entity - 3;
    u8 index = 0;
    s32 i;

    ctx.m0_scriptPtr = monstersScriptsEntities[m].m0_scripts[0];
    __builtin_memset(battleScriptContext, 0, sizeof(battleScriptContext));
    for (i = 0; i < 32; i++) {
        battleCurrentDamages[i].m47_battleAnimationToPlay = 0xFF;
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
        out[n++] = *s == 1 ? 0x10 : *s;
        s++;
    }
    out[n] = 0;
}

static void entityName(u8* out, s32 entity) {
    if (entity < 3) {
        gameText(out, gameState.m0_names[battleCharacters[entity] & 0x1F], 0x13);
    } else if (currentBattleSpecificStrings != NULL) {
        gameText(out, getDialogParamPointer(currentBattleSpecificStrings, battleMonsterMapping[entity]), LABEL - 1);
    } else {
        out[0] = 0;
    }
}

static void actionLabel(u8* out, const sBattleScriptContext* c) {
    s32 k;

    for (k = 0; k < 32; k++) {
        if (c[k].m0_attackType != 0 && c[k].m3_nameIndex != 0 && currentBattleSpecificStrings != NULL) {
            gameText(out, getDialogParamPointer(currentBattleSpecificStrings, c[k].m3_nameIndex), LABEL - 1);
            if (out[0] != 0) {
                return;
            }
        }
    }
    for (k = 0; k < 32; k++) {
        if (c[k].m0_attackType == 1 || c[k].m0_attackType == 2) {
#if VERSION_JP
            word(out, "ACTION");
#else
            word(out, "Attack");
#endif
            return;
        }
    }
    for (k = 0; k < 32; k++) {
        if (c[k].m0_attackType != 0) {
#if VERSION_JP
            word(out, "MOVE");
#else
            word(out, "Move");
#endif
            return;
        }
    }
#if VERSION_JP
    word(out, "NONE");
#else
    word(out, "Nothing");
#endif
}

static void targetLabel(u8* out, const sBattleScriptContext* c) {
    s32 k;
    s32 i;
    s32 count = 0;
    s32 first = -1;
    u16 mask = 0;

    /* an attack's or an ability's targets (types 1, 2, 13), else the first
     * entry's (a type 15 animation also names the actor itself) */
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
            if (__builtin_strcmp((const char*)orig[i], (const char*)orig[j]) == 0) {
                total++;
                n += j <= i;
            }
        }
        len = (s32)__builtin_strlen((const char*)l[i]);
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
#define WINDOW_W (TEXT_UNITS * 4 + 0x18)
#define WINDOW_X ((0x140 - WINDOW_W) / 2)
#define WINDOW_Y 0x34

static DialogWindow18 window;
static s32 shown;
static u8 text[(PAGE + 2) * (LABEL + 1) + 2];

/* drawBattleMode1/2 (src/battle/b_800728B8.c), after the dialogue windows. */
void pcVersusDraw(void) {
    if (shown) {
        updateAndRenderTextForDialogWindow(&window, D_800CCB04.m0_pCurrentBattleOT + 1, D_800CCB04.m30_battleOddOrEven);
    }
}

static int openWindow(const u8* lines, s32 count, s32 selected) {
    s32 h = count * 16 + 12;

    if (battleVar1 == NULL || battleVar1->mB0_isDialogWindowInitialized[WINDOW] ||
        battleVar1->mB8_isDialogWindowInitialized2[WINDOW]) {
        return 0;
    }
    allocateAndSetupBattleDialogWindow(WINDOW, WINDOW_X, WINDOW_Y, WINDOW_W, h, 1, 0);
    while (!battleVar1->mBF_isDialogWindowInitialized3[WINDOW]) {
        battleRenderDebugAndMain();
    }
    setupWindowSize2(&window, 0x380, 0x100, WINDOW_X + 0xC, WINDOW_Y + 8, TEXT_UNITS, count);
    window.m10_flags |= 2; /* no background tile: the window is the background */
    window.m68_charsPerStep = 0x20;
    resetDialogWindow18(&window);
    addDialogWindowsToOTSub2(&window, (u8*)lines);
    if (selected >= 0) {
        setupMultiChoiceColor(&window, 0xEF, 0x1E, 0xF0);
        setSelectedLineForMultiChoice(&window, selected);
    }
    shown = 1;
    return 1;
}

static void closeWindow(void) {
    if (shown) {
        shown = 0;
        dealocateDialogWindow18Memory(&window);
        clearBattleDialogWindow(WINDOW);
    }
}

static s32 append(s32 at, const u8* s) {
    while (*s != 0 && at < (s32)sizeof(text) - 2) {
        text[at++] = *s++;
    }
    return at;
}

/* Player 2 picks one of `count` labels under `title` (pad 2's Up/Down,
 * confirm; cancel when canCancel): the index, or -1 for cancel. -2 when the
 * window could not open. */
static s32 choose(const u8* title, u8 (*labels)[LABEL], s32 count, s32 canCancel) {
    s32 choice = 0;
    s32 first = -1;
    s32 held;
    s32 previous = pcPadHeldRemappedPort(1);
    s32 pressed;
    s32 i;
    s32 at;
    s32 shownCount;

    for (;;) {
        if (choice < first || choice >= first + PAGE || first < 0) {
            /* (re)build the page that holds the choice */
            closeWindow();
            first = choice - choice % PAGE;
            shownCount = count - first < PAGE ? count - first : PAGE;
            at = append(0, title);
            for (i = 0; i < shownCount; i++) {
                text[at++] = 1;
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
                pcArenaLog("  window: %d lines: %s", shownCount + 1, hex);
            }
            if (!openWindow(text, shownCount + 1, choice - first + 1)) {
                return -2;
            }
        }
        battleRenderDebugAndMain();
        held = pcPadHeldRemappedPort(1);
        pressed = held & ~previous;
        previous = held;
        if (!(window.m10_flags & 8)) {
            continue; /* still printing */
        }
        if (pressed & (BUTTON_UP | BUTTON_DOWN)) {
            choice = (choice + (pressed & BUTTON_UP ? count - 1 : 1)) % count;
            if (choice >= first && choice < first + PAGE) {
                setSelectedLineForMultiChoice(&window, choice - first + 1);
            }
            battleSoundEffect2(0x4C);
        } else if (pressed & BUTTON_CONFIRM) {
            battleSoundEffect2(0x4D);
            break;
        } else if ((pressed & BUTTON_CANCEL) && canCancel) {
            battleSoundEffect2(0x4C);
            choice = -1;
            break;
        }
    }
    closeWindow();
    return choice;
}

/* ---- battleTickMain: an enemy's turn ---- */

static u8 labels[MAX_CHOICES][LABEL];
static s32 actionOf[MAX_CHOICES]; /* the outcome each action choice stands for */

/* In place of battleTickMain's executeMonsterScript (src/battle/b_80070E2C.c). */
void pcVersusMonsterTurn(u8 entity, s32 status) {
    u8 title[LABEL];
    s32 actions;
    s32 targets;
    s32 targetOf[MAX_CHOICES];
    s32 a;
    s32 t;
    s32 i;
    s32 j;
    s32 chosen = -1;

    if (pcVersusSession() != PC_VERSUS_IN || isControllerConnected(1) == 0) {
        executeMonsterScript(entity, status);
        return;
    }
    probeTurn(entity);
    pcArenaLog("enemy %d's turn: %d outcomes in %d runs", entity, outcomeCount, runs);
    actions = 0;
    for (i = 0; i < outcomeCount && actions < MAX_CHOICES - 1; i++) {
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
        pcArenaLog("  outcome %d: type %d op %d anim %d name %d mask %x / type %d op %d anim %d name %d mask %x, %d draws", i,
                   outcomes[i].ctx[0].m0_attackType, outcomes[i].ctx[0].m1_operand, outcomes[i].ctx[0].m2_animationId,
                   outcomes[i].ctx[0].m3_nameIndex, outcomes[i].ctx[0].m6_mask, outcomes[i].ctx[1].m0_attackType,
                   outcomes[i].ctx[1].m1_operand, outcomes[i].ctx[1].m2_animationId, outcomes[i].ctx[1].m3_nameIndex,
                   outcomes[i].ctx[1].m6_mask, outcomes[i].planLen);
    }
    entityName(title, entity);
    while (outcomeCount > 1 && chosen < 0) {
        for (i = 0; i < actions; i++) {
            actionLabel(labels[i], outcomes[actionOf[i]].ctx);
        }
        numberSame(labels, actions);
        word(labels[actions], "AUTO");
        a = actions > 1 ? choose(title, labels, actions + 1, 0) : 0;
        if (a < 0 || a == actions) {
            break; /* Auto (or no window): the game's own draws */
        }
        __builtin_memcpy(title, labels[a], LABEL); /* the action, over its targets */
        targets = 0;
        for (i = 0; i < outcomeCount && targets < MAX_CHOICES; i++) {
            if (sameAction(outcomes[actionOf[a]].ctx, outcomes[i].ctx)) {
                targetLabel(labels[targets], outcomes[i].ctx);
                targetOf[targets++] = i;
            }
        }
        numberSame(labels, targets);
        t = targets > 1 ? choose(title, labels, targets, actions > 1) : 0;
        entityName(title, entity);
        if (t == -2) {
            break;
        }
        if (t >= 0) {
            chosen = targetOf[t];
        }
    }
    if (chosen < 0) {
        pcArenaLog("  the game's own draws");
        executeMonsterScript(entity, status);
        return;
    }
    pcArenaLog("  player 2 chose outcome %d", chosen);
    __builtin_memcpy(plan, outcomes[chosen].plan, sizeof(plan));
    planLen = outcomes[chosen].planLen;
    playRun(entity);
    if (__builtin_memcmp(battleScriptContext, outcomes[chosen].ctx, sizeof(battleScriptContext)) != 0) {
        pcArenaLog("  the turn's actions differ from the chosen outcome (an op the probe passes over)");
    }
}

/* ---- battleMain: the formation, the end ---- */

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

static u8 lastResult; /* pcVersusLastResult */

/* The last versus battle's end (battleRunningVar1 as the battle left it):
 * 0x81 player 2 won, 0x40 bit player 1 escaped, else player 1 won; 0 none. */
int pcVersusLastResult(void) {
    return lastResult;
}

/* battleMain, when its loop ends: who won, then the end that gives nothing
 * (0x21, a won battle without rewards) or costs nothing (0x40, an escape's
 * end, never the game over). */
void pcVersusBattleOver(void) {
    u8 r;
    u8 line[LABEL];
    s32 frames;
    s32 previous;
    s32 held;

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
    if (openWindow(line, 1, -1)) {
        previous = pcPadHeldRemappedPort(0) | pcPadHeldRemappedPort(1);
        for (frames = 0; frames < 180; frames++) {
            battleRenderDebugAndMain();
            held = pcPadHeldRemappedPort(0) | pcPadHeldRemappedPort(1);
            if ((held & ~previous & BUTTON_CONFIRM) && (window.m10_flags & 8) && frames >= 60) {
                break; /* a confirm closes it after a second */
            }
            previous = held;
        }
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
    pcVersusRestore();
}
