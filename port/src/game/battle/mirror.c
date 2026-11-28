/*
 * Mirror match (xg-htn.35.143; the Battle arena's versus battles,
 * include/pc_arena.h): player 2 commands a copy of player 1's party, on
 * foot, in a normal battle.
 *
 * The battle has one party side (entities 0-2) and builds enemies only from
 * its set's monster data and scripts (entities 3-10), so the copy takes
 * enemy slots 3-5, one for each member, member k in entity 3 + k:
 *   - the formation (versus.c pcVersusBattleSetup, the mirror's stage, a
 *     small set on foot): its first enemies in slots 3.., one per member,
 *     the rest empty, no battle event, no Gears (config flag 0x40); or the
 *     stage the players chose (pcVersusStage: system/arena.c's list), with
 *     that stage's party slots and player 2's places from a real battle of
 *     that stage, the formation's enemy set the same;
 *   - once the loader has built the battle (battleMain, after
 *     startBattleLoader: pcVersusBattleLoaded): each member's battle entity
 *     (its record with equipment, abilities, HP, EP, statuses: the whole
 *     sBattleEntity the loader made of gameState) copied over the enemy's,
 *     so no enemy data, multiplier (pcGameplayEnemyLoaded) or script is left:
 *     the scripts are an empty one; its AP, its command ring (the party's
 *     menus), its visual entity's character id (the effects and animations
 *     pick a character's by it: processBattleAnimation's
 *     abilityAnimationTable0) and its party attack table
 *     (m1058_partyBattleStats: below);
 *   - the sprites (the battle loader's createBattlePlayerSpriteActors, then
 *     pcVersusMirrorSprites): the member's battle sprite file (directory
 *     0x2C/1, battlePartyFileMapping) read again into a VRAM column of its
 *     own (the party's sprites take x 0x100-0x13F, y 0x1C0-0x1FF, one
 *     64-column texture page for three: the copy takes the next page,
 *     x 0x180-0x1BF; free in the mirror's stage and in each listed one,
 *     measured: no stage's upload there) and the entity's
 *     own palette row (y 0x1C0 + entity, as every battle sprite), in the
 *     party's own colours (the user's choice: only the name label tells
 *     player 2's copy apart). They jump in as the party does.
 *
 * Player 2's turn (battleTickMain, pcVersusMirrorTurn): the party's own turn
 * (setupTurnRenderLoop, reproduced here: it waits on the party's time bar
 * HUD, which an enemy slot has none of) with the battle's input read from
 * pad 2 (battleHandleInput, pcVersusMirrorPad2): the party's command ring
 * with the member's real commands, its menus and their code as they are
 * (attack: the target, then the hits as the AP allow, the deathblows a
 * learned combo string completes; defend; the AP combos), and the name
 * label "Fei (P2)" (window 5, as an enemy's turn shows its name; the ring 16
 * pixels lower so the label does not cover its Up slot, as versus.c's). That
 * code is the party's, written for entities 0-2:
 *   - battleCharacters[entity] (a 4-byte array) is read through
 *     BATTLE_CHARACTER (include/battle/battle.h): pcBattleCharacter gives
 *     the copy's ids for entities 3-5;
 *   - BATTLE_FOOT_ROWS and BATTLE_GEAR_ROWS route player 2's combat
 *     commands to their own tables;
 *   - an attack resolves (startBattleAttackAnimation) from
 *     m35D8_enemiesBattleStats for an attacker past 2: the member's 38
 *     entries are copied there for each of its turns (the battle has no
 *     real enemy to use that table);
 *   - the turn's end frees the party's turn structures only for entities
 *     0-2 (freePlayerTurnStructs): here for the copy.
 * Items: the battle keeps the party's inventory in battleEffectBF (its
 * counts and ids, made from the save's by initBattleInventory; the item menu
 * lists it, executeItem takes one from it, battle_result writes it back to
 * the save). Player 2 has a copy of it as the battle began (p2Items), put
 * in for each of its turns and taken out again after: its items are its
 * own, and what it uses never leaves player 1's.
 * Billy's guns are player 2's own too: the battle's ammo list
 * (battleAmmoItemIds and Counts, from inventory 1 at the start), the rounds
 * loaded (gameState m22B8_ammoCounts, spent by spendBillyAmmo) and Billy's
 * gun records (gameState m26C_party[4]: its first 0x24 bytes and
 * m6F_equipment, which loadBillyAmmo and the turn's ammo test use) are put
 * in with the items; a reload's battle entities are the copy's (loadBillyAmmo
 * through AMMO_SLOT: pcVersusPartySlot).
 * Its speed is the party's (battleGetSlotStatusSub through PARTY_SPEED: its
 * agility less its last command's penalty, from its rows of
 * m1058_partyBattleStats), not an enemy's (agility alone).
 * Escape is player 2 giving up: player 1 wins. Deathblow learning is the
 * party's alone (startBattleAttackAnimation: attacker < 3), so the copy's
 * hits teach player 1's save nothing; and the whole battle is inside the
 * versus checkpoint anyway (port/src/game/main/arena.c), put back after.
 * The copy's character ids are its members' own, so the gameState rows the
 * party's code reads by character id (m16C0_abilities, m26C_party,
 * m978_gears: learned deathblows and ethers) are already the copy's: only
 * Billy's guns are swapped in a mirror match (a party from another save,
 * the next step, will swap the rest around its turns).
 *
 * The name label: the item and ether lists and the combo menu render their
 * text where the label's is (VRAM 0x3C0,0: their names and descriptions),
 * so while one of the menus' dialogue windows (0-3) is up the label is put
 * away, and when they are all down it is rendered and shown again
 * (pcVersusMirrorLabel, each frame): "Fei (P2)" for the whole turn but under
 * a menu.
 *
 * What each player sees (the user's choice): their own side's status bar, as
 * though they were player 1. On one screen the bar is the side's whose turn
 * began last (a party member's: the party's; a copy's: player 2's three, as
 * though player 2 were player 1); online each machine its own player's
 * (pcVersusPerspective: netplay's player number, or XG_VERSUS_VIEW=1|2).
 * Player 2's bar is the battle's own: its code (updatePolyArray3A88,
 * renderGearHP, updatePortraits, battleRenderPortraitSelection,
 * battleRenderPlayerPortraits, the fills in battleDrawAPBar, the turn gauge
 * in wideBattleHud) runs again on a HUD of player 2's (p2Hud0/p2Hud1, as
 * battleVar0/battleVar1: the HUD pointers swapped around it), its slots'
 * numbers read through HUD_SLOT (src/battle/b_800728B8.c: slots 3-5). The
 * copy's turn has the party's marks of a turn in that HUD: the portrait's
 * pulse (battleTickMain's highlight, its pulse in battleUpdateInputs_mode1:
 * pcVersusViewTick), the time bar flying to the turn gauge at the left
 * (setupTurnRenderLoop's status 3, which the turn waits for as the party's
 * does) and the AP marks on it.
 * Display only: player 2's HUD is built every frame of a mirror match,
 * whichever bar is shown (so the turn's wait on its gauge is the same on
 * both screens of a session); the view only picks which HUD's packets are
 * linked. Its build writes its own HUD and nothing the battle reads: the
 * display numbers the bar's code leaves behind (currentEntity*,
 * printIntegerToStringBattleBuffer) and the GTE are put back after, and the
 * work is in hwClockHold (no time on the stepped clock, nothing delivered),
 * so the game's frames and their vblanks are the same whichever is shown.
 *
 * Party vs party (the same hooks; pcVersusPartyMatch, include/pc_arena.h):
 * each side's party from its own party record (port/src/card/party.h, a
 * memory-card save's members), player 1's applied to the game at the
 * match's start (port/src/system/arena.c), player 2's built here from its
 * record's bytes alone (p2Record: the only input, as online it will be what
 * the other machine sent), into slots 3-5 as the copy is:
 *   - its members as the loader makes the party: their records with HP and
 *     EP full, no Gear (the number 0xFF) and their own command loadout, as
 *     loadPartyMembers puts them into a battle entity; its attack rows read
 *     from the disc by character id where the party's are
 *     (pcVersusPartyFiles: loadPartyMembers' table, reloc[id + 5]); then the
 *     game's own initPartyBattleStats run on them (accessory and bonus
 *     stats, the cost changes in its rows, its per-character fixes) in slots
 *     0-2 with its rows in gameState, and player 1's put back after
 *     (partyBuild); its AP per turn its abilities' (m17), its menus its
 *     loadout's (battleCommandsSetups, as batteLoaderPhase2_2), and the
 *     battle's turn order made again with them in it (batteLoaderPhase2_1:
 *     each member's first wait from its own speed);
 *   - the gameState rows the party's code reads by character id (its record
 *     in m26C_party, its abilities in m16C0_abilities, its name in m0_names)
 *     are player 2's own for its turns (swapRows, with the items): so Fei
 *     against Fei reads each side's own deathblows, ethers, Billy's guns;
 *   - its items, rounds and ammo-count table its record's;
 *   - its status bar its own members': its boxes where its member count puts
 *     them (battlePartyLayoutType and the boxes' visual ids for player 2's
 *     HUD: hudIn), built as the loader builds the party's
 *     (batteLoaderPhase3_0_sub1), and its members' portraits. The portraits
 *     are battle font glyphs (0x61 + box), one place in VRAM for both bars:
 *     those of the side whose bar is shown are loaded there when the bar
 *     shown changes (portraitsLoad, the loader's loadBattleCharacterPortraits
 *     with that side's ids). Display only, as the bar: nothing the battle
 *     reads.
 *
 * Without a mirror match every hook returns at once.
 */
#include "common.h"
#include "psyq.h"
#include "main/filesystem.h"
#include "main/dialog.h"
#include "main/game_state.h"
#include "main/memory.h"
#include "main/sprite.h"
#include "battle/battle.h"
#include "battle_loader/battle_loader.h"
#include "gte_pc.h"
#include "pc_arena.h"
#include "versus.h"
#include "../../card/party_record.h"

/* the battle loader (battle_loader overlay) */
extern s32 battlePartyFileMapping[14][2]; /* per character: battle sprite file, attack animation file (dir 0x2C/1) */
extern u8 battleSpriteWidthPerCharacter[12];
extern u8 currentBattleLoaderTransitionEffect;
void createBattleSpriteActor(u32 entity, s32 visualBufferIndex, s32 animationId);
/* the battle */
extern u8 battleMonsterMapping[11];
extern u8* currentBattleSpecificStrings;
extern u16 party1C_InitialValues[16];
extern u8 newBattleInputButton2;
extern u8 battleInputButton;
extern u8 startCharacterJumpToEnemyVar0;
extern u8 battleCharacters[4];
extern u16 partySavedCommandEnabledBF[16]; /* the party's command masks at the battle's start (battleStartPartyFixups) */
s32 battleRenderDebugAndMain(void);
void sampleBattleTerrainHeight(SpriteActorCore* actor);
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animation);
u8 getEntityToFace(u8 entity);
void handleMenuSelectEnemy(u8 entity);
void initBattleRenderStruct2(void);
void setupGearTurnHud(u8 entity);
s32 handleGearMenuSelectEnemy(u8 entity);
int pcVersusCallGearSetting(void);
void pcVersusApplySavedGears(const XgPartyRecord* party);
void resetBattleVar2_2CC(void);
void clearEntityFlag15A_1(u8 entity);
void freeBattleVar1_AE(void);
void freeBattleVar1_96(void);
void freeBattleRenderStruct2(u8 entity);
void freeBillyBattleStructs(u8 entity);
void performAutoPartyMemberTurn(u8 entity);
void loadMonsterAttackName(void);
void displayAndWaitMonsterAttackName(void);
void removeCurrentMonsterNameString(void);
s32 getCurrentDirectory(s32* directory, s32* offset);
u16 bitmaskCharacterCheck(u16 mask, u8 i);
u32 hwVSyncCount(void); /* port/src/system: the log's vblank */
void hwClockHold(int on); /* port/src/system/hw.c */
/* the status bar (src/battle/b_800728B8.c, b_80079674.c) */
extern u8 battlePartyLayoutType;
extern u16 partyMemberSpritesOffset[3 * 3];
void updatePolyArray3A88(void);
void renderGearHP(void);
void updatePortraits(void);
void battleRenderPortraitSelection(void);
void battleRenderPlayerPortraits(void);
void battleRenderPolyArray(POLY_FT4* p, s32 count, s32 oddOrEven);
void setAPMarksPositions(u8 maxAP, u8 remainingAP);
/* the numbers its code leaves behind (b_800728B8.c, b_8008A274.c) */
extern s16 currentEntityMaxHpCopy;
extern s16 currentEntityMaxHpForDisplay;
extern s16 currentEntityHpCopy;
extern s16 currentEntityHpForDisplay;
extern s32 currentEntityGearHpCopy;
extern s32 currentEntityGearMaxHpForDisplay;
extern s32 currentEntityGearHpForDisplay;
extern u8 currentEntityHpForDisplayBuffer[3];
extern u8 currentEntityMaxHpForDisplayBuffer[3];
extern u8 currentEntityGearMaxHpForDisplayBuffer[5];
extern u8 printIntegerToStringBattleBuffer[9];
/* Billy's ammo (the battle loader's list, from inventory 1) */
extern u8 battleAmmoItemIds[];
extern u8 battleAmmoItemCounts[];
/* party vs party: the loader's party code (src/battle_loader/bld_801E4048.c,
 * src/battle/b_800941A4.c, b_800769E8.c) run for player 2's */
extern u8* battleCommandsSetups[19];
extern u8 battlePartyMemberCount;
extern sBattleEntity* battleGetSlotStatusSub_currentBattleEntity;
extern GameStateGear* battleGetSlotStatusSub_currentBattleEntityGear;
void initPartyBattleStats(void);
void batteLoaderPhase2_1(void);
void batteLoaderPhase3_0_sub1(void);

#define MIRROR_SPRITE_X 0x180 /* the copy's VRAM column (the header) */
#define MIRROR_SPRITE_Y 0x1C0
#define RING_DY 16            /* the ring under the name label (versus.c) */
#define STATS_ROWS 38         /* m1058_partyBattleStats' entries a member */

static s32 mirrorOn;     /* this battle is a mirror match */
static u8 mirrorChar[3]; /* the copy's character ids (entity 3 + k), 0x7F none */
static void* mirrorSprite[3];
static s32 p2Turn;       /* the entity whose turn player 2 plays now, 0 none */
static s32 forfeit;
static u8 emptyScript[4] = {0xFF, 0xFF, 0xFF, 0xFF}; /* a monster script that does nothing */
static u8 p2Items[2][0x30]; /* player 2's inventory: battleEffectBF's counts and ids (the header) */
/* player 2's Billy's guns (the header): the ammo list (ids, counts), the
 * rounds loaded, Billy's gun records and his equipment */
static u8 p2AmmoItems[2][0x30];
static u8 p2Ammo[sizeof(gameState.m22B8_ammoCounts)];
#define GUN_BYTES 0x24 /* a character's weapon records: m0..m4_weaponSlots[3] */
static u8 p2Gun[GUN_BYTES];
static u8 p2GunEquipment[sizeof(gameState.m26C_party[4].m6F_equipment)];
/* The names the copy's turns and the enemy info show ("Fei (P2)"), and the
 * party's for player 2's enemy info ("Fei"), as the battle's own strings: a
 * dialogue parameter block (getDialogParamPointer: 4 bytes, then each
 * string's offset from the block's start). String 1 + k is copy k's, 4 + k
 * party member k's (NAME_PARTY), 0 empty. */
#define NAMES 7
#define NAME_PARTY 4
static u8 mirrorStrings[4 + 2 * NAMES + NAMES * (VERSUS_LABEL + 1)];
/* the name label of player 2's turn (pcVersusMirrorLabel) */
static s32 labelOn;
static u8 p2Label[VERSUS_LABEL];
/* what each player sees (the header) */
static sBattleVar0 p2Hud0; /* player 2's status bar: battleVar0 and battleVar1 as its code uses them */
static sBattleVar1 p2Hud1;
static s32 p2HudReady;
static sBattleVar0* hudSaved0;
static sBattleVar1* hudSaved1;
static s32 hudPass;  /* the bar's code is on player 2's HUD: HUD_SLOT reads slots 3-5 */
static s32 viewSide; /* the side whose turn began last: 1 player 1's, 2 player 2's */
/* party vs party (the header) */
static s32 partyOn;
static u8 p2Record[XG_PARTY_BYTES]; /* player 2's party record, as the match has it */
static u8 p2Count;                  /* its members */
static u8 p2Layout;                 /* its bar's box layout (battlePartyLayoutType: members - 1) */
static GameStateCharacter p2Party[3]; /* its rows of gameState (swapRows), member k's */
static GameStateSize20 p2Abilities[3];
static u8 p2Names[3][sizeof(gameState.m0_names[0])];
static sBattle800CDD40Sub p2Rows[3][STATS_ROWS]; /* its attack rows from the disc (pcVersusPartyFiles) */
static sBattle800CDD40Sub p2GearRows[3][42];
#define PORTRAIT_BYTES 0x460 /* a 24x24 8-bit TIM (loadBattleCharacterPortraits) */
#define PORTRAITS 12         /* characters 0-10, and 11 (battleForceOnGear's) */
static u8 portraitFile[PORTRAITS * PORTRAIT_BYTES]; /* the loader's portrait file (reloc[36]) */
static s32 portraitsShown; /* whose portraits the glyphs have in VRAM: 1 player 1's, 2 player 2's */
static u8 hudSavedIds[3];  /* hudIn: the party's boxes' visual ids, its layout */
static u8 hudSavedLayout;
/* partyBuild: what running the party's code for player 2 changes of player 1's */
static struct {
    sBattleEntity entities[3];
    u8 rows[sizeof(battleEntities.m1058_partyBattleStats) + sizeof(battleEntities.m2228_mechaBattleStats)];
    s32 exp[3][2];
    u16 maxHpMp[3][2];
    u8 stats[3][8];
    s32 damage[3];
    s32 gearDamage[3];
    u8 characters[4];
    u8 count;
    sBattleEntity* entity;
    GameStateGear* gear;
    GameStateGear gears[20];
} keep;

int pcVersusMirror(void);
static void swapBytes(u8* a, u8* b, s32 n);

int pcVersusMirrorSlot(s32 entity) {
    return mirrorOn && entity >= 3 && entity < 6 && mirrorChar[entity - 3] != 0x7F;
}

sBattle800CDD40Sub* pcVersusMirrorRows(int entity, int gear) {
    return gear ? p2GearRows[entity - 3] : p2Rows[entity - 3];
}

static int gearEnabled(void) {
    return pcVersusCallGearSetting() || pcVersusGears();
}

/* HUD_SLOT (src/battle/b_800728B8.c): the slot whose numbers the bar's code
 * reads for its box `slot`. */
int pcBattleHudSlot(int slot) {
    return hudPass ? slot + 3 : slot;
}

/* battleGetSlotStatusSub (src/battle/b_800941A4.c): an entity's wait to its
 * next turn as its speed gives it (before the game's jitter), for the log;
 * the copy's by the party's formula (PARTY_SPEED: its agility less its last
 * command's penalty), as its member's. */
void pcVersusSpeedLog(int entity, int wait) {
    u8 row;

    if (!mirrorOn || (entity >= 3 && !pcVersusMirrorSlot(entity))) {
        return;
    }
    row = battleEntities.m5FC2_battleGetSlotStatusSub_current28Index;
    pcArenaLog("mirror match: entity %d's wait %d (agility %d, command row %d, its penalty %d; vblank %u)", entity, wait,
               battleEntities.m0_entities[entity].m0_base.m5A_agility, row,
               (s8)BATTLE_FOOT_ROWS(&battleEntities, entity)[row].m27_agilityPenalty, hwVSyncCount());
}

/* AMMO_SLOT (src/battle/b_800941A4.c loadBillyAmmo): the battle entity of
 * the acting side's party slot `slot`: on player 2's turn its copy's. */
int pcVersusPartySlot(int slot) {
    return p2Turn != 0 && slot < 3 ? slot + 3 : slot;
}

/* The bar to show: the player's own online (or XG_VERSUS_VIEW), else the
 * side whose turn began last. */
static s32 viewShown(void) {
    s32 p = pcVersusPerspective();

    return p == 1 || p == 2 ? p : viewSide;
}

/* BATTLE_CHARACTER (include/battle/battle.h): the party's code asks for a
 * slot's character id. A mirror match's copy: its member's; anything else
 * the same read as before the hook (the array, past its end too, as the
 * party's code always read it). */
u8 pcBattleCharacter(u32 entity) {
    if (entity >= 3 && pcVersusMirrorSlot((s32)entity)) {
        return mirrorChar[entity - 3];
    }
    return battleCharacters[entity];
}

int pcVersusMirrorPad2(void) {
    return p2Turn != 0;
}

int pcVersusMirrorForfeit(void) {
    return mirrorOn && forfeit;
}

/* Online party vs party (netplay.c's state hash, every vblank of a battle):
 * player 2's side as this file keeps it outside its turns, which gameState
 * and the battle entities do not hold: its record, rows, names, items,
 * rounds and attack rows, the copies' ids and the turn's state. Game state
 * only (the same on both machines): not the HUD, the portraits, the label or
 * the sprites' pointers (display). FNV-1a, 32 bits. */
static u32 hashBytes(u32 h, const void* p, s32 n) {
    const u8* b = p;
    s32 i;

    for (i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

u32 pcVersusPartyHash(void) {
    u32 h = 2166136261u;

    if (!mirrorOn || !partyOn) {
        return 0;
    }
    h = hashBytes(h, p2Record, sizeof(p2Record));
    h = hashBytes(h, p2Party, sizeof(p2Party));
    h = hashBytes(h, p2Abilities, sizeof(p2Abilities));
    h = hashBytes(h, p2Names, sizeof(p2Names));
    h = hashBytes(h, p2Rows, sizeof(p2Rows));
    h = hashBytes(h, p2GearRows, sizeof(p2GearRows));
    h = hashBytes(h, p2Items, sizeof(p2Items));
    h = hashBytes(h, p2AmmoItems, sizeof(p2AmmoItems));
    h = hashBytes(h, p2Ammo, sizeof(p2Ammo));
    h = hashBytes(h, mirrorChar, sizeof(mirrorChar));
    h = hashBytes(h, &p2Count, sizeof(p2Count));
    h = hashBytes(h, &p2Turn, sizeof(p2Turn));
    h = hashBytes(h, &forfeit, sizeof(forfeit));
    return h;
}

/* ---- the formation (pcVersusBattleSetup) ---- */

/* Party vs party: player 2's members' gameState rows from its record, as
 * player 1's are applied to the game (port/src/system/arena.c
 * partyFromRecord): HP and EP full, the command loadout the member's own,
 * and the saved Gear kept when the match uses it. */
static void partyRecordRows(void) {
    s32 k;
    u8 c;

    for (k = 0; k < p2Count; k++) {
        c = p2Record[PC_PARTY_MEMBERS + k];
        __builtin_memcpy(&p2Party[k], p2Record + PC_PARTY_CHARACTERS + k * sizeof(GameStateCharacter),
                         sizeof(p2Party[k]));
        p2Party[k].m4C_HP = p2Party[k].m4E_MaxHP;
        p2Party[k].m50_MP = p2Party[k].m52_MaxMP;
        if (!gearEnabled()) p2Party[k].mA0_gearNum = 0xFF;
        p2Party[k].m56_battleCommandLoadout = c;
        __builtin_memcpy(&p2Abilities[k], p2Record + PC_PARTY_ABILITIES + k * sizeof(GameStateSize20),
                         sizeof(p2Abilities[k]));
        __builtin_memcpy(p2Names[k], p2Record + PC_PARTY_NAMES + k * sizeof(p2Names[k]), sizeof(p2Names[k]));
    }
}

void pcVersusMirrorSetup(void) {
    extern u8 battleConfigs[0x220];
    extern u8 requestedBattleConfig;
    u8* c = &battleConfigs[requestedBattleConfig * 0x20];
    u16 mask = gameState.m1D30_partyMemberBitField & gameState.m1D32_partyFrameMask & 0x7FF;
    s32 members = 0;
    s32 enemy = c[8] & 0x7F;
    s32 i;

    mirrorOn = pcVersusMirror();
    forfeit = 0;
    p2Turn = 0;
    labelOn = 0;
    p2HudReady = 0;
    hudPass = 0;
    viewSide = 1; /* the party's bar until a copy's turn */
    partyOn = 0;
    portraitsShown = 1; /* the loader's: the party's */
    __builtin_memset(p2Rows, 0, sizeof(p2Rows));
    __builtin_memset(p2GearRows, 0, sizeof(p2GearRows));
    for (i = 0; i < 3; i++) {
        mirrorChar[i] = 0x7F;
        mirrorSprite[i] = NULL;
    }
    if (!mirrorOn) {
        return;
    }
    if (pcVersusPartyMatch()) {
        const u8* r = pcVersusPartyRecord(2);

        if (r != NULL) {
            __builtin_memcpy(p2Record, r, sizeof(p2Record));
            p2Count = p2Record[PC_PARTY_COUNT];
            p2Layout = p2Count - 1;
            partyOn = 1;
            partyRecordRows();
        } else {
            pcArenaLog("party vs party: no record for player 2: a copy of the party instead");
        }
    }
    if (partyOn) {
        members = p2Count; /* player 2's own party (its record) */
    } else {
        for (i = 0; i < 3; i++) {
            if (bitmaskCharacterCheck(mask, gameState.m1D34_currentParty[i])) {
                members++; /* as loadPartyMembers counts them */
            }
        }
    }
    /* one of the set's first enemy for each member, where the formation has
     * its first ones; the rest empty */
    for (i = 0; i < 8; i++) {
        c[8 + i] = i < members ? enemy : 0x7F;
        c[0x10 + i] = 0;
    }
    c[1] &= ~(0x20 | 0x40); /* No story event. */
    if (!gearEnabled()) c[1] |= 0x40;
    pcArenaLog("%s: %d members, set %d, enemy %d in their places", partyOn ? "party vs party" : "mirror match",
               members, c[0], enemy);
    /* the stage the players chose (include/pc_arena.h pcVersusStage): its
     * number, the party's slots and player 2's places from a real battle of
     * that stage; the formation's enemies (their sprites, sounds) the same */
    {
        u8 party[3];
        u8 places[3];
        s32 stage = pcVersusStage(party, places);

        if (stage >= 0) {
            c[2] = (u8)stage;
            for (i = 0; i < 3; i++) {
                c[4 + i] = party[i];
                c[0x18 + i] = places[i];
            }
            pcArenaLog("%s: stage %d, the party in slots %d,%d,%d, player 2 in places %d,%d,%d",
                       partyOn ? "party vs party" : "mirror match", stage, party[0], party[1], party[2], places[0],
                       places[1], places[2]);
        }
    }
}

/* ---- party vs party: player 2's battlers (the header) ---- */

/* loadPartyMembers, its character file's table relocated: player 2's attack
 * rows (reloc[id + 5], 38 entries as the party's) and the portrait file
 * (reloc[36]), read as the loader reads the party's. */
void pcVersusPartyFiles(u8** reloc) {
    u8* p;
    u32 n;
    s32 k;

    if (!partyOn) {
        return;
    }
    for (k = 0; k < p2Count; k++) {
        u8* footFile = reloc[p2Record[PC_PARTY_MEMBERS + k] + 5];
        n = *(u32*)footFile;
        p = mallocAndDecompress(footFile, 1);
        __builtin_memcpy(p2Rows[k], p, n < sizeof(p2Rows[k]) ? n : sizeof(p2Rows[k]));
        freeHeap(p);
        if (p2Party[k].mA0_gearNum < 20) {
            u8* gearFile = reloc[p2Party[k].mA0_gearNum + 17];
            n = *(u32*)gearFile;
            p = mallocAndDecompress(gearFile, 1);
            __builtin_memcpy(p2GearRows[k], p, n < sizeof(p2GearRows[k]) ? n : sizeof(p2GearRows[k]));
            freeHeap(p);
        }
    }
    n = *(u32*)reloc[36]; /* LZSS: the decompressed size first */
    p = mallocAndDecompress(reloc[36], 1);
    __builtin_memcpy(portraitFile, p, n < sizeof(portraitFile) ? n : sizeof(portraitFile));
    freeHeap(p);
}

/* Player 2's rows of gameState in (its members' records, abilities and
 * names, by character id), player 1's out; or back. */
static void swapRows(void) {
    s32 k;
    u8 c;

    for (k = 0; k < 3; k++) {
        c = mirrorChar[k];
        if (c == 0x7F) {
            continue;
        }
        swapBytes((u8*)&gameState.m26C_party[c], (u8*)&p2Party[k], sizeof(p2Party[k]));
        swapBytes((u8*)&gameState.m16C0_abilities[c], (u8*)&p2Abilities[k], sizeof(p2Abilities[k]));
        swapBytes(gameState.m0_names[c], p2Names[k], sizeof(p2Names[k]));
    }
}

/* One battler's numbers, for the log: what the game made of a member. */
static void partyLog(s32 e) {
    sBattleEntity* b = &battleEntities.m0_entities[e];
    const u8* bytes = (const u8*)b;
    const u8* rows = (const u8*)BATTLE_FOOT_ROWS(&battleEntities, e);
    u32 h = 2166136261u;
    u32 r = 2166136261u;
    u32 i;

    /* the battler without its status timers (its record, Gear, modes, shares, flags) and its attack rows */
    for (i = 0; i < 0x15C; i++) {
        h = (h ^ bytes[i]) * 16777619u;
    }
    for (i = 0; i < sizeof(sBattle800CDD40Sub) * STATS_ROWS; i++) {
        r = (r ^ rows[i]) * 16777619u;
    }
    pcArenaLog("party vs party: entity %d character %d: level %d, HP %d/%d, EP %d/%d, attack %d, defence %d, agility %d, "
               "ether %d/%d, hit %d, evade %d, AP %d, battler fnv1a %08x, rows %08x",
               e, BATTLE_CHARACTER(e), b->m0_base.m62_Level, b->m0_base.m4C_HP, b->m0_base.m4E_MaxHP, b->m0_base.m50_MP,
               b->m0_base.m52_MaxMP, b->m0_base.m58_attack, b->m0_base.m59_defence, b->m0_base.m5A_agility,
               b->m0_base.m5B_ether, b->m0_base.m5C_etherDefence, b->m0_base.m5E_hitPercentage,
               b->m0_base.m5F_evadePercentage, apConfigArray[e].m4_currentAP, h, r);
}

/* Each side's Billy (character 4) and his guns' rounds left (the ammo-count
 * table's entries for the ammo loaded), for the log: player 2's are its own. */
static void partyLogBilly(void) {
    GameStateCharacter* p1 = NULL;
    GameStateCharacter* p2 = NULL;
    s32 k;

    for (k = 0; k < 3; k++) {
        if (battleCharacters[k] == 4) {
            p1 = &gameState.m26C_party[4];
        }
        if (mirrorChar[k] == 4) {
            p2 = &p2Party[k];
        }
    }
    if (p1 != NULL || p2 != NULL) {
        pcArenaLog("party vs party: Billy's rounds: P1 %d %d, P2 %d %d",
                   p1 != NULL ? gameStateAmmoByWeaponItem[p1->m6F_equipment[0]] : -1,
                   p1 != NULL ? gameStateAmmoByWeaponItem[p1->m6F_equipment[3]] : -1,
                   p2 != NULL ? p2Ammo[p2->m6F_equipment[0] - 0x32] : -1,
                   p2 != NULL ? p2Ammo[p2->m6F_equipment[3] - 0x32] : -1);
    }
}

/* Build Player 2's saved party with the native stat initializer, then
 * move its combatants into slots 3–5 and restore Player 1's records. */
static void partyBuild(void) {
    sBattleVar2Sub* menu;
    sBattleEntity* b;
    u8 setups;
    s32 k;
    s32 e;
    s32 j;
    u8 c;

    /* what running the party's code changes of player 1's */
    __builtin_memcpy(keep.entities, battleEntities.m0_entities, sizeof(keep.entities));
    __builtin_memcpy(keep.rows, (u8*)battleEntities.m1058_partyBattleStats, sizeof(keep.rows)); /* and m2228 after it */
    __builtin_memcpy(keep.exp, battleEntities.mFD0_savedM3C, sizeof(keep.exp));
    __builtin_memcpy(keep.maxHpMp, battleEntities.m1000_savedMaxHpMp, sizeof(keep.maxHpMp));
    __builtin_memcpy(keep.stats, battleEntities.m1028_savedStats, sizeof(keep.stats));
    __builtin_memcpy(keep.damage, battleEntities.m5F54_damageTaken, sizeof(keep.damage));
    __builtin_memcpy(keep.gearDamage, battleEntities.m5F60_gearDamageTaken, sizeof(keep.gearDamage));
    __builtin_memcpy(keep.characters, battleCharacters, sizeof(keep.characters));
    keep.count = battlePartyMemberCount;
    keep.entity = battleGetSlotStatusSub_currentBattleEntity;
    keep.gear = battleGetSlotStatusSub_currentBattleEntityGear;
    /* player 2's members in slots 0-2, as loadPartyMembers makes the party's
     * (on foot, the Gear number 0xFF reads Gear 0), its rows in gameState */
    swapRows();
    __builtin_memcpy(keep.gears, gameState.m978_gears, sizeof(keep.gears));
    if (gearEnabled()) pcVersusApplySavedGears((const XgPartyRecord*)p2Record);
    for (k = 0; k < 3; k++) {
        c = mirrorChar[k];
        battleCharacters[k] = c;
        if (c == 0x7F) {
            continue;
        }
        __builtin_memset(&battleEntities.m0_entities[k], 0, sizeof(sBattleEntity));
        __builtin_memcpy(&battleEntities.m0_entities[k].m0_base, &gameState.m26C_party[c], sizeof(GameStateCharacter));
        u8 gear = gameState.m26C_party[c].mA0_gearNum;
        __builtin_memcpy(&battleEntities.m0_entities[k].mA4_gear, &gameState.m978_gears[gear < 20 ? gear : 0], sizeof(GameStateGear));
        __builtin_memcpy(battleEntities.m1058_partyBattleStats[k], p2Rows[k], sizeof(p2Rows[k]));
        __builtin_memcpy(battleEntities.m2228_mechaBattleStats[k], p2GearRows[k], sizeof(p2GearRows[k]));
    }
    initPartyBattleStats();
    for (k = 0; k < 3; k++) {
        c = mirrorChar[k];
        if (c == 0x7F) {
            continue;
        }
        e = 3 + k;
        b = &battleEntities.m0_entities[e];
        __builtin_memcpy(b, &battleEntities.m0_entities[k], sizeof(sBattleEntity));
        __builtin_memcpy(p2Rows[k], battleEntities.m1058_partyBattleStats[k], sizeof(p2Rows[k]));
        __builtin_memcpy(p2GearRows[k], battleEntities.m2228_mechaBattleStats[k], sizeof(p2GearRows[k]));
        b->m15A_flags &= 0x7F; /* on foot (batteLoaderPhase1_3) */
        /* battleStartPartyFixups' fix of character 8's abilities, on its own row */
        if (c == 8 && (gameState.m16C0_abilities[8].m1A_unlockedGearAbilities & 0x2000)) {
            gameState.m16C0_abilities[8].m0_unlockedAbilitiesBitField[1] |= 0x800;
        }
        /* its AP per turn (batteLoaderPhase2_2) and levels (batteLoaderPhase1_3) */
        __builtin_memset(&apConfigArray[e], 0, sizeof(apConfigArray[e]));
        apConfigArray[e].m4_currentAP = gameState.m16C0_abilities[c].m17_energyPerTurn;
        apConfigArray[e].m5_savedLevels[0] = b->m0_base.m62_Level;
        apConfigArray[e].m5_savedLevels[1] = b->m0_base.m63_Level2;
        /* Build its command ring from the save, as the party loader does. */
        menu = &battleVar2->m0_entityMenus[e];
        __builtin_memset(menu, 0, sizeof(*menu));
        for (j = 0; j < 8; j++) {
            menu->m0_circleMenuBattleCommandsMapping[j] = battleCommandsSetups[b->m0_base.m56_battleCommandLoadout][j];
        }
        setups = c != 7 ? 16 : 18;
        for (j = 0; j < 4; j++) {
            menu->m8_extraCommandsMapping[j] = battleCommandsSetups[setups][j];
            menu->mC_extraCommandsMapping2[j] = battleCommandsSetups[17][j];
        }
        for (j = 0; j < 16; j++) {
            menu->m1C_isCommandEnabled[j] = b->m0_base.m7A_commandEnabledBF & party1C_InitialValues[j];
        }
        if (currentBattleConfig.m1_flags & 0x80) {
            menu->m1C_isCommandEnabled[8] = party1C_InitialValues[8];
            b->m0_base.m7A_commandEnabledBF |= party1C_InitialValues[8];
        }
    }
    swapRows();
    /* player 1's back */
    __builtin_memcpy(battleEntities.m0_entities, keep.entities, sizeof(keep.entities));
    __builtin_memcpy((u8*)battleEntities.m1058_partyBattleStats, keep.rows, sizeof(keep.rows));
    __builtin_memcpy(battleEntities.mFD0_savedM3C, keep.exp, sizeof(keep.exp));
    __builtin_memcpy(battleEntities.m1000_savedMaxHpMp, keep.maxHpMp, sizeof(keep.maxHpMp));
    __builtin_memcpy(battleEntities.m1028_savedStats, keep.stats, sizeof(keep.stats));
    __builtin_memcpy(battleEntities.m5F54_damageTaken, keep.damage, sizeof(keep.damage));
    __builtin_memcpy(battleEntities.m5F60_gearDamageTaken, keep.gearDamage, sizeof(keep.gearDamage));
    __builtin_memcpy(battleCharacters, keep.characters, sizeof(keep.characters));
    battlePartyMemberCount = keep.count;
    battleGetSlotStatusSub_currentBattleEntity = keep.entity;
    battleGetSlotStatusSub_currentBattleEntityGear = keep.gear;
    __builtin_memcpy(gameState.m978_gears, keep.gears, sizeof(keep.gears));
}

/* ---- the copy (battleMain, after the loader) ---- */

static void buildNames(void) {
    s32 at = 4 + 2 * NAMES;
    s32 k;
    u8 c;
    u8 name[VERSUS_LABEL];
    u8 tag[8];
    s32 n;
    s32 i;

    __builtin_memset(mirrorStrings, 0, sizeof(mirrorStrings));
    pcVersusWord(tag, " (P2)");
    for (k = 0; k < NAMES; k++) {
        *(u16*)(mirrorStrings + 4 + 2 * k) = (u16)at;
        c = k == 0 ? 0x7F : k < NAME_PARTY ? mirrorChar[k - 1] : battleCharacters[k - NAME_PARTY];
        if (c == 0x7F) {
            mirrorStrings[at++] = 0;
            continue;
        }
        pcVersusGameText(name, partyOn && k < NAME_PARTY ? p2Names[k - 1] : gameState.m0_names[c], VERSUS_LABEL - 7);
        for (n = 0; name[n] != 0; n++) {
            mirrorStrings[at++] = name[n];
        }
        for (i = 0; k < NAME_PARTY && tag[i] != 0; i++) {
            mirrorStrings[at++] = tag[i];
        }
        mirrorStrings[at++] = 0;
    }
}

void pcVersusBattleLoaded(void) {
    s32 k;
    s32 e;

    if (!mirrorOn) {
        return;
    }
    if (partyOn) {
        /* player 2's own members (its record), built by the party's code */
        for (k = 0; k < 3; k++) {
            mirrorChar[k] = k < p2Count && isBattleSlotFilled.m0_isBattleSlotFilled[3 + k] ? p2Record[PC_PARTY_MEMBERS + k]
                                                                                           : 0x7F;
        }
        partyBuild();
    }
    for (k = 0; k < 3; k++) {
        e = 3 + k;
        if (partyOn) {
            if (mirrorChar[k] == 0x7F) {
                continue;
            }
        } else {
            mirrorChar[k] = 0x7F;
            if (battleCharacters[k] == 0x7F || !isBattleSlotFilled.m0_isBattleSlotFilled[e]) {
                continue;
            }
            mirrorChar[k] = battleCharacters[k];
            /* the member as the loader made it, over the enemy */
            __builtin_memcpy(&battleEntities.m0_entities[e], &battleEntities.m0_entities[k], sizeof(sBattleEntity));
        }
        battleBssBlock.m4_visualEntities[e].m2_characterId = mirrorChar[k];
        battleBssBlock.m4_visualEntities[e].m3_hidden = 0;
        battleBssBlock.m4_visualEntities[e].m4_isGear = 0;
        battleMonsterMapping[e] = k + 1; /* mirrorStrings */
        monstersScriptsEntities[k].m0_scripts[0] = emptyScript;
        monstersScriptsEntities[k].m0_scripts[1] = emptyScript;
        monstersScriptsEntities[k].m0_scripts[2] = emptyScript;
        monstersScriptsEntities[k].m0_scripts[3] = emptyScript;
        unknownMonsterStatus0[k].m0_hasWhenAttackedScript = 0;
        unknownMonsterStatus0[k].m1_hasEndOfTurnScript = 0;
        if (!partyOn) {
            apConfigArray[e] = apConfigArray[k];
            apConfigArray[e].m0_storedAP = 0;
            apConfigArray[e].m1_gearState = 0;
            battleEntities.m0_entities[e].m15A_flags &= ~0x80;
            __builtin_memcpy(p2Rows[k], battleEntities.m1058_partyBattleStats[k], sizeof(p2Rows[k]));
            __builtin_memcpy(p2GearRows[k], battleEntities.m2228_mechaBattleStats[k], sizeof(p2GearRows[k]));
            /* the same start to its turns as the member's */
            isBattleSlotFilled.m24_battleSlotStatusVar0[e] = isBattleSlotFilled.m24_battleSlotStatusVar0[k];
            isBattleSlotFilled.m3A_numTicksBeforeReady[e] = isBattleSlotFilled.m3A_numTicksBeforeReady[k];
        }
        /* its command mask as the battle's start keeps the member's: put back
         * when a confusion ends (battleStartPartyFixups saves the party's, after this) */
        partySavedCommandEnabledBF[e] = battleEntities.m0_entities[e].m0_base.m7A_commandEnabledBF;
        if (!partyOn) {
            /* the member's ring (batteLoaderPhase2_2 sets up the party's; party vs party: partyBuild) */
            battleVar2->m0_entityMenus[e] = battleVar2->m0_entityMenus[k];
        }
        battleVar2->m0_entityMenus[e].m3C_currentTarget = getEntityToFace(e);
        battleBssBlock.m4_visualEntities[e].m6_direction =
            getDirectionBetween2BattleEntities(e, battleVar2->m0_entityMenus[e].m3C_currentTarget);
        pcArenaLog("mirror match: entity %d is character %d (HP %d/%d, EP %d, AP %d)", e, mirrorChar[k],
                   battleEntities.m0_entities[e].m0_base.m4C_HP, battleEntities.m0_entities[e].m0_base.m4E_MaxHP,
                   battleEntities.m0_entities[e].m0_base.m50_MP, apConfigArray[e].m4_currentAP);
    }
    buildNames();
    currentBattleSpecificStrings = mirrorStrings;
    if (partyOn) {
        /* player 2's items, rounds and their table: its record's (Billy's guns
         * are in its rows: swapRows) */
        __builtin_memcpy(p2Items[0], p2Record + PC_PARTY_ITEM_COUNTS, sizeof(p2Items[0]));
        __builtin_memcpy(p2Items[1], p2Record + PC_PARTY_ITEM_IDS, sizeof(p2Items[1]));
        __builtin_memcpy(p2AmmoItems[0], p2Record + PC_PARTY_AMMO_IDS, sizeof(p2AmmoItems[0]));
        __builtin_memcpy(p2AmmoItems[1], p2Record + PC_PARTY_AMMO_COUNTS, sizeof(p2AmmoItems[1]));
        __builtin_memcpy(p2Ammo, p2Record + PC_PARTY_AMMO_TABLE, sizeof(p2Ammo));
        /* the battle's turn order made again with player 2's members in it:
         * each one's first wait from its own speed (the party's formula:
         * PARTY_SPEED), as the loader makes every side's */
        batteLoaderPhase2_1();
        for (k = 0; k < 6; k++) {
            if (k < 3 ? battleCharacters[k] != 0x7F : mirrorChar[k - 3] != 0x7F) {
                partyLog(k);
            }
        }
        return;
    }
    /* player 2's items and Billy's guns: player 1's as the battle begins */
    __builtin_memcpy(p2Items[0], battleEffectBF.m12_counts, sizeof(p2Items[0]));
    __builtin_memcpy(p2Items[1], battleEffectBF.m42_ids, sizeof(p2Items[1]));
    __builtin_memcpy(p2AmmoItems[0], battleAmmoItemIds, sizeof(p2AmmoItems[0]));
    __builtin_memcpy(p2AmmoItems[1], battleAmmoItemCounts, sizeof(p2AmmoItems[1]));
    __builtin_memcpy(p2Ammo, gameState.m22B8_ammoCounts, sizeof(p2Ammo));
    __builtin_memcpy(p2Gun, &gameState.m26C_party[4], sizeof(p2Gun));
    __builtin_memcpy(p2GunEquipment, gameState.m26C_party[4].m6F_equipment, sizeof(p2GunEquipment));
}

static void swapBytes(u8* a, u8* b, s32 n) {
    u8 t;
    s32 i;

    for (i = 0; i < n; i++) {
        t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

/* Player 2's inventory and Billy's guns in, player 1's out, or back (the
 * header); party vs party, its gameState rows too. */
static void swapItems(void) {
    swapBytes(battleEffectBF.m12_counts, p2Items[0], sizeof(p2Items[0]));
    swapBytes(battleEffectBF.m42_ids, p2Items[1], sizeof(p2Items[1]));
    swapBytes(battleAmmoItemIds, p2AmmoItems[0], sizeof(p2AmmoItems[0]));
    swapBytes(battleAmmoItemCounts, p2AmmoItems[1], sizeof(p2AmmoItems[1]));
    swapBytes(gameState.m22B8_ammoCounts, p2Ammo, sizeof(p2Ammo));
    if (partyOn) {
        swapRows(); /* its own rows, Billy's guns with them */
        return;
    }
    swapBytes((u8*)&gameState.m26C_party[4], p2Gun, sizeof(p2Gun));
    swapBytes(gameState.m26C_party[4].m6F_equipment, p2GunEquipment, sizeof(p2GunEquipment));
}

/* ---- the sprites (createBattlePlayerSpriteActors) ---- */

void pcVersusMirrorSprites(void) {
    s32 dir;
    s32 sub;
    s32 x = MIRROR_SPRITE_X;
    s32 k;
    s32 e;
    s32 c;
    s32 file;
    SpriteActorCore* core;

    if (!mirrorOn) {
        return;
    }
    getCurrentDirectory(&dir, &sub);
    for (k = 0; k < 3; k++) {
        e = 3 + k;
        c = mirrorChar[k];
        if (c == 0x7F) {
            continue;
        }
        setCurrentDirectory(0x2C, 1);
        file = battlePartyFileMapping[c][0];
        mirrorSprite[k] = allocateHeap(getFileSizeAligned(file), 0);
        readFile(file, mirrorSprite[k], 0, 0x80);
        waitReadCompletion(0);
        battleBssBlock.m8D24_visualBuffers[e].m0_spriteData = mirrorSprite[k];
        battleBssBlock.m8D24_visualBuffers[e].m8_vramParam = 0;
        battleBssBlock.m8D24_visualBuffers[e].m6_vramY = MIRROR_SPRITE_Y;
        battleBssBlock.m8D24_visualBuffers[e].m4_vramX = x;
        x += battleSpriteWidthPerCharacter[c];
        createBattleSpriteActor(e, e, 1);
        core = battleBssBlock.m8C8C_spriteActorCores[e];
        core->m7C_extra->m0 = battlePartyFileMapping[c][1]; /* its attack animations */
        freeHeap(core->m20_object->m2C_subSpriteBuffer);
        core->m20_object->m30_subSprites = core->m20_object->m2C_subSpriteBuffer = allocateHeap(0x300, 0);
        if (currentBattleLoaderTransitionEffect == 0) {
            sampleBattleTerrainHeight(core);
            core->mA0_destination.vx = core->m0_position.vx >> 16;
            core->mA0_destination.vy = core->m0_position.vy >> 16;
            core->mA0_destination.vz = core->m0_position.vz >> 16;
            spriteActorSetPlayingAnimation(core, 0x17); /* jumps in, as the party */
        }
        pcArenaLog("mirror match: entity %d's sprite: file %d (%d bytes) at VRAM %x,%x", e, file,
                   getFileSizeAligned(file), battleBssBlock.m8D24_visualBuffers[e].m4_vramX, MIRROR_SPRITE_Y);
    }
    setCurrentDirectory(dir, sub);
}

/* battleMain's end: the copy's sprite files (releaseSpriteActor frees the
 * party's own only). */
void pcVersusMirrorEnd(void) {
    s32 k;

    for (k = 0; k < 3; k++) {
        if (mirrorSprite[k] != NULL) {
            freeHeap(mirrorSprite[k]);
            mirrorSprite[k] = NULL;
            battleBssBlock.m8D24_visualBuffers[3 + k].m0_spriteData = NULL;
        }
    }
    mirrorOn = 0;
    p2Turn = 0;
    labelOn = 0;
    p2HudReady = 0;
    partyOn = 0;
}

/* ---- what each player sees: player 2's status bar (the header) ---- */

/* Player 2's HUD in place of the party's (the bar's code then builds and
 * draws player 2's), and back. Party vs party: its boxes too, which the
 * bar's code takes from the party's visual entities (a box for each slot
 * 0-2 with a character) and battlePartyLayoutType (where they are). */
static void hudIn(void) {
    s32 k;

    hudSaved0 = battleVar0;
    hudSaved1 = battleVar1;
    battleVar0 = &p2Hud0;
    battleVar1 = &p2Hud1;
    if (partyOn) {
        for (k = 0; k < 3; k++) {
            hudSavedIds[k] = battleBssBlock.m4_visualEntities[k].m2_characterId;
            battleBssBlock.m4_visualEntities[k].m2_characterId = mirrorChar[k];
        }
        hudSavedLayout = battlePartyLayoutType;
        battlePartyLayoutType = p2Layout;
    }
    hudPass = 1;
}

static void hudOut(void) {
    s32 k;

    battleVar0 = hudSaved0;
    battleVar1 = hudSaved1;
    if (partyOn) {
        for (k = 0; k < 3; k++) {
            battleBssBlock.m4_visualEntities[k].m2_characterId = hudSavedIds[k];
        }
        battlePartyLayoutType = hudSavedLayout;
    }
    hudPass = 0;
}

/* Party vs party: the portraits of side `side`'s members into the battle
 * font's portrait glyphs (the header), as the loader loads the party's. */
static void portraitsLoad(s32 side) {
    u8 ids[3];
    s32 k;

    for (k = 0; k < 3; k++) {
        ids[k] = battleCharacters[k];
        if (side == 2) {
            battleCharacters[k] = mirrorChar[k];
        }
    }
    loadBattleCharacterPortraits(portraitFile, 0x61);
    for (k = 0; k < 3; k++) {
        battleCharacters[k] = ids[k];
    }
    portraitsShown = side;
}

/* Player 2's HUD as the party's is set up (the battle loader's frames,
 * labels, portraits, textures: the same members, so the same), with no turn
 * in it and every box's numbers to make. */
static void hudReady(void) {
    s32 k;

    if (p2HudReady || battleVar0 == NULL || battleVar1 == NULL) {
        return;
    }
    __builtin_memcpy(&p2Hud0, battleVar0, sizeof(p2Hud0));
    __builtin_memcpy(&p2Hud1, battleVar1, sizeof(p2Hud1));
    for (k = 0; k < 3; k++) {
        p2Hud1.m90_perPCTimeBarStatus[k] = 0;
        p2Hud1.m7C_hpDisplayDirty[k] = 1;
        p2Hud1.mE0_hpDigitCounts[k] = 0;
        p2Hud1.mEC_apFuelDigitCounts[k] = 0;
    }
    for (k = 0; k < 4; k++) {
        p2Hud1.m74_timeBarsLengths[k] = 0;
        p2Hud1.m7F_drawTimeBarFill[k] = 0;
    }
    p2Hud1.m97_apMarkCount = 0;
    p2Hud1.mAF_drawApCounter = 0;
    p2Hud0.m6415_drawHighlight = 0;
    p2Hud0.m6416_highlightFadingOut = 0;
    if (partyOn) {
        /* its own boxes, as the loader makes the party's: on foot
         * (batteLoaderPhase1_3's bar mode), their frames, portraits and
         * labels where its layout puts them (batteLoaderPhase3_0_sub1) */
        for (k = 0; k < 3; k++) {
            p2Hud1.m78_apBarFrameLengths[k] = 0;
        }
        hudIn();
        batteLoaderPhase3_0_sub1();
        hudOut();
    }
    for (k = 0; k < 3; k++) {
        u8 character = mirrorChar[k];
        p2Hud0.m835C_apFuelLabels[k].m1E1_barMode =
            character == 0x7F ? 0 : character != 7 && battleVisualEntities[k + 3].m4_isGear ? 2 : 1;
    }
    p2HudReady = 1;
}

/* What the bar's code leaves behind outside the HUD, put back after
 * player 2's bar is built: the display numbers and the GTE. */
typedef struct {
    s16 maxHpCopy, maxHp, hpCopy, hp;
    s32 gearHpCopy, gearMaxHp, gearHp;
    u8 hpDigits[3], maxHpDigits[3], gearDigits[5], integer[9];
    GteState gte;
} HudScratch;

static void scratchSave(HudScratch* s) {
    s->maxHpCopy = currentEntityMaxHpCopy;
    s->maxHp = currentEntityMaxHpForDisplay;
    s->hpCopy = currentEntityHpCopy;
    s->hp = currentEntityHpForDisplay;
    s->gearHpCopy = currentEntityGearHpCopy;
    s->gearMaxHp = currentEntityGearMaxHpForDisplay;
    s->gearHp = currentEntityGearHpForDisplay;
    __builtin_memcpy(s->hpDigits, currentEntityHpForDisplayBuffer, sizeof(s->hpDigits));
    __builtin_memcpy(s->maxHpDigits, currentEntityMaxHpForDisplayBuffer, sizeof(s->maxHpDigits));
    __builtin_memcpy(s->gearDigits, currentEntityGearMaxHpForDisplayBuffer, sizeof(s->gearDigits));
    __builtin_memcpy(s->integer, printIntegerToStringBattleBuffer, sizeof(s->integer));
    gteSave(&s->gte);
}

static void scratchLoad(const HudScratch* s) {
    currentEntityMaxHpCopy = s->maxHpCopy;
    currentEntityMaxHpForDisplay = s->maxHp;
    currentEntityHpCopy = s->hpCopy;
    currentEntityHpForDisplay = s->hp;
    currentEntityGearHpCopy = s->gearHpCopy;
    currentEntityGearMaxHpForDisplay = s->gearMaxHp;
    currentEntityGearHpForDisplay = s->gearHp;
    __builtin_memcpy(currentEntityHpForDisplayBuffer, s->hpDigits, sizeof(s->hpDigits));
    __builtin_memcpy(currentEntityMaxHpForDisplayBuffer, s->maxHpDigits, sizeof(s->maxHpDigits));
    __builtin_memcpy(currentEntityGearMaxHpForDisplayBuffer, s->gearDigits, sizeof(s->gearDigits));
    __builtin_memcpy(printIntegerToStringBattleBuffer, s->integer, sizeof(s->integer));
    gteLoad(&s->gte);
}

/* drawBattleMode1, after the party's bar is built (the battle's own code,
 * as on the PS1): player 2's built too, every frame of a mirror match; then
 * 1 when player 2's is the bar to show (drawn here, the party's not), else
 * 0 (the party's drawn by the caller). */
void pcVersusMirrorGearHud(int slot, int inGear) {
    if (p2HudReady)
        p2Hud0.m835C_apFuelLabels[slot - 3].m1E1_barMode = inGear && mirrorChar[slot - 3] != 7 ? 2 : 1;
}

int pcVersusViewBar(void) {
    HudScratch scratch;
    s32 shows2;

    if (!mirrorOn || battleVar0 == NULL || battleVar1 == NULL) {
        return 0;
    }
    shows2 = viewShown() == 2;
    hwClockHold(1);
    hudReady();
    scratchSave(&scratch);
    /* what the battle changes in the party's (a Gear's bar mode; the ring
     * up, which shows the turn gauge's fill); party vs party: its labels are
     * its own boxes' (hudReady), on foot */
    if (partyOn && portraitsShown != (shows2 ? 2 : 1)) {
        portraitsLoad(shows2 ? 2 : 1); /* the shown side's portraits (the header) */
    }
    p2Hud1.mCB_drawCommandRing = battleVar1->mCB_drawCommandRing;
    p2Hud1.mAF_drawApCounter = 0; /* the AP counter is the turn's (the party's HUD's), below */
    hudIn();
    updatePolyArray3A88();
    renderGearHP();
    updatePortraits();
    if (shows2) {
        battleRenderPortraitSelection();
        battleRenderPlayerPortraits();
    }
    hudOut();
    scratchLoad(&scratch);
    if (shows2 && battleVar1->mAF_drawApCounter) {
        /* the turn's AP counter, which battleRenderPlayerPortraits draws with the party's bar */
        battleRenderPolyArray(battleVar0->m9C8_APCounterDisplayPolys[0], battleVar1->m7B_apCounterLength,
                              battleVar1->mA4_oddOrEven7B);
    }
    hwClockHold(0);
    return shows2;
}

/* battleDrawAPBar's time bar fills and wideBattleHud's turn gauge: player
 * 2's HUD in (1) while its bar is the one shown, then back (0). */
static s32 viewSwapped;
void pcVersusViewSwap(int in) {
    if (in) {
        if (mirrorOn && p2HudReady && !hudPass && viewShown() == 2) {
            hudIn();
            viewSwapped = 1;
        }
    } else if (viewSwapped) {
        hudOut();
        viewSwapped = 0;
    }
}

/* battleUpdateInputs_mode1, each vblank: what it does for the party's HUD,
 * for player 2's (the time bar's flight to the gauge, the portrait's pulse). */
void pcVersusViewTick(void) {
    sBattleVar0* p = &p2Hud0;

    if (!mirrorOn || !p2HudReady) {
        return;
    }
    p2Hud1.mA9_timeBarNumMoveSteps += 6;
    p2Hud1.mAB_timeBarNumAnimationSteps++;
    if (p->m6415_drawHighlight != 0) {
        if (p->m6416_highlightFadingOut == 0) {
            p->m6410_highlightLevel += 4;
            if (p->m6410_highlightLevel > 0x80) {
                p->m6410_highlightLevel = 0x7C;
                p->m6416_highlightFadingOut = 1;
            }
        } else {
            p->m6410_highlightLevel -= 4;
            if (p->m6410_highlightLevel < 0) {
                p->m6410_highlightLevel = 4;
                p->m6416_highlightFadingOut = 0;
            }
        }
    }
}

/* copyBattleVar1_m7C (battleTickMain, a turn's end): the party's bar shows
 * the numbers the turn changed now, not as the damage is dealt (its HP
 * marked changed); player 2's then too (all three made again, from the
 * numbers as they are). */
void pcVersusViewHpShown(void) {
    s32 k;

    if (!mirrorOn || !p2HudReady) {
        return;
    }
    for (k = 0; k < 3; k++) {
        p2Hud1.m7C_hpDisplayDirty[k] = 1;
    }
}

/* battleTickMain: entity's turn begins. */
void pcVersusViewTurn(u8 entity) {
    if (!mirrorOn) {
        return;
    }
    if (entity < 3) {
        viewSide = 1;
    } else if (pcVersusMirrorSlot(entity)) {
        viewSide = 2;
    }
    pcArenaLog("mirror match: entity %d's turn (vblank %u), side %d; party time bar states %d %d %d", entity,
               hwVSyncCount(), viewSide, battleVar1 != NULL ? battleVar1->m90_perPCTimeBarStatus[0] : -1,
               battleVar1 != NULL ? battleVar1->m90_perPCTimeBarStatus[1] : -1,
               battleVar1 != NULL ? battleVar1->m90_perPCTimeBarStatus[2] : -1);
}

/* ---- the enemy info window in a versus battle (enemyinfo.c) ---- */

/* The first slot of the side it lists: the opponent of the side shown,
 * player 2's copy (3) or, on player 2's screen, the party (0). */
int pcVersusInfoFirst(void) {
    return mirrorOn && viewShown() == 2 ? 0 : 3;
}

/* Slot's name in this battle's strings (currentBattleSpecificStrings). */
int pcVersusInfoName(int slot) {
    return mirrorOn && slot >= 0 && slot < 3 ? NAME_PARTY + slot : battleMonsterMapping[slot];
}

/* ---- player 2's turn (battleTickMain) ---- */

/* pcVersusDraw (versus.c), each frame: player 2's name label up for its
 * turn, but under the menus that render their text where its is (the
 * header). */
void pcVersusMirrorLabel(void) {
    s32 menus;
    s32 up;

    if (!labelOn || battleVar1 == NULL) {
        return;
    }
    menus = battleVar1->mB0_isDialogWindowInitialized[0] | battleVar1->mB0_isDialogWindowInitialized[1] |
            battleVar1->mB0_isDialogWindowInitialized[2] | battleVar1->mB0_isDialogWindowInitialized[3];
    up = battleVar1->mB0_isDialogWindowInitialized[5] && previousEntityTurn.m8_nameLabels[0].m5D_monsterNameIsDisplayed;
    hwClockHold(1); /* work the PS1 has none of: no time on the stepped clock */
    if (menus && up) {
        pcVersusHideLabel();
    } else if (!menus && !up) {
        pcVersusShowLabel(p2Label); /* its text again: the menu's was where its is */
    }
    hwClockHold(0);
}

/* The ring for menu `menu`, as setupTurnRenderLoop draws it, RING_DY lower. */
static void drawRing(u8 entity, u8 menu) {
    POLY_FT4* p;
    s32 i;
    s32 k;

    battleVar1->mCB_drawCommandRing = 0;
    battleVar1->mA3_641COddOrEven = updateBattleMenuSpriteForMenu(entity, menu, battleVar2->m2E0_isBattleMenuDisabled);
    for (i = 0; i < 2; i++) {
        for (k = 0; k < battleVar1->mD0_battleCommandLength[i]; k++) {
            p = &battleVar0->m641C_commandGlyphs[i][k * 2 + battleVar1->mA3_641COddOrEven];
            p->y0 += RING_DY;
            p->y1 += RING_DY;
            p->y2 += RING_DY;
            p->y3 += RING_DY;
        }
    }
    battleVar1->mCB_drawCommandRing = 1;
}

/* Escape gives the match to Player 1. Exit Gear uses the same ring slot. */
static int confirmForfeit(void) {
    if (battleInputButton != 4 && battleInputButton != 6 && battleInputButton != 7) return 0;
    pcArenaLog("mirror match: player 2 gave up (vblank %u)", hwVSyncCount());
    forfeit = 1;
    performAttackSub3_var0.m2_battleRunningVar1 = 1;
    battleVar2->m2DE_actionDone = 1;
    return 1;
}

/* setupTurnRenderLoop for player 2's member: the same, its time bar and AP
 * marks in player 2's HUD (the header). */
static void turnLoop(u8 entity) {
    sBattleVar2Sub* menu = &battleVar2->m0_entityMenus[entity];
    sBattleEntity* self = &battleEntities.m0_entities[entity];
    u8 c = mirrorChar[entity - 3];
    u8 box = entity - 3;
    int inGear = apConfigArray[entity].m1_gearState != 0;
    u8 target;
    s32 i;

    pcVersusPartySelection(2);
    battleVar2->m2EA_notDefending = 1;
    battleVar2->m2E9_targetConfirmed = 0;
    battleVar2->m2DE_actionDone = 0;
    battleVar2->m2E0_isBattleMenuDisabled = 0;
    battleVar2->m2E1_waitBeforeNextAttackInput = 0;
    battleVar2->m2E2_previousActiveBattleMenu = 0xFF;
    battleVar2->m2E3_hasAttacked = 0;
    battleVar2->m2E4_deathblowCompleted = 0;
    battleVar2->m2DC_attackIndex = 0;
    battleVar2->m2D4_remainingAP = battleVar2->m2D5_maxAP = apConfigArray[entity].m4_currentAP;
    p2Hud1.m90_perPCTimeBarStatus[box] = 3;
    battleVar2->m2E7_gearAttackCount = 0;
    newBattleInputButton2 = 0xFF;
    battleBssBlock.mA79 = 0;
    /* the wait for its time bar to reach the turn gauge, as the party's
     * (player 2's HUD moves it on every screen: the header), which also
     * reads the pads again: the last turn's end left 0xFF (its end) */
    battleInputButton = 8;
    for (i = 0; i < 120 && p2Hud1.m90_perPCTimeBarStatus[box] != 1; i++) {
        battleRenderDebugAndMain();
    }
    pcArenaLog("mirror match: player 2's time bar at the gauge after %d frames (vblank %u)", i, hwVSyncCount());
    startCharacterJumpToEnemyVar0 = 1;
    battleSoundEffect2(0x5A);
    startCharacterJumpToEnemyVar0 = 0;
    for (i = 0; i < 16; i++) {
        menu->m1C_isCommandEnabled[i] = self->m0_base.m7A_commandEnabledBF & party1C_InitialValues[i];
    }
    pcVersusPartyGearMenu(entity);
    if (c == 7 && inGear) {
        menu->m1C_isCommandEnabled[13] = party1C_InitialValues[13];
        menu->m1C_isCommandEnabled[4] = party1C_InitialValues[4];
    }
    if (c == 4) {
        int hasPrimary, hasSecondary;
        if (inGear) {
            hasPrimary = gameStateAmmoByGearItem[self->mA4_gear.m4_slotItem[0]];
            hasSecondary = gameStateAmmoByGearItem[self->mA4_gear.m4_slotItem[3]];
        } else {
            hasPrimary = gameStateAmmoByWeaponItem[self->m0_base.m6F_equipment[0]];
            hasSecondary = gameStateAmmoByWeaponItem[self->m0_base.m6F_equipment[3]];
        }
        if (!hasPrimary) menu->m1C_isCommandEnabled[0] = party1C_InitialValues[0];
        if (!hasSecondary) menu->m1C_isCommandEnabled[2] = party1C_InitialValues[2];
    }
    if (menu->m1C_isCommandEnabled[9] == 0) {
        battleVar2->m2DD_currentActiveBattleMenu = 1;
        previousNewBattleInputButton[0] = 0;
    } else {
        battleVar2->m2DD_currentActiveBattleMenu = 4;
        previousNewBattleInputButton[0] = 3;
    }
    if (self->m0_base.m7C_statusFlags[0] & 2) {
        battleVar2->m2DD_currentActiveBattleMenu = 2;
        previousNewBattleInputButton[0] = 1;
    }
    if (inGear) {
        battleVar2->m2DD_currentActiveBattleMenu = menu->m1C_isCommandEnabled[9] ? 19 : 16;
        previousNewBattleInputButton[0] = menu->m1C_isCommandEnabled[9] ? 3 : 0;
        setupGearTurnHud(entity);
    } else {
        initBattleRenderStruct2();
    }
    resetBattleVar2_2CC();
    clearEntityFlag15A_1(entity);
    setCameraVisibleEntities(characterIdToTargetBitmask(entity) | characterIdToTargetBitmask(menu->m3C_currentTarget));
    target = menu->m3C_currentTarget == 0xFF ? 0 : menu->m3C_currentTarget;
    updateCharacterBlinkingTask(characterIdToTargetBitmask(target));
    while (battleVar2->m2DE_actionDone == 0 && performAttackSub3_var0.m2_battleRunningVar1 == 0 &&
           battleVar2->m2DB_turnEnded == 0) {
        /* the AP marks on the turn gauge, in player 2's HUD */
        if (!inGear) {
            hudIn();
            setAPMarksPositions(battleVar2->m2D5_maxAP, battleVar2->m2D4_remainingAP);
            hudOut();
        }
        if (battleVar2->m2E2_previousActiveBattleMenu != battleVar2->m2DD_currentActiveBattleMenu) {
            battleVar2->m2F6_doublePressPending = 0;
            battleVar2->m2E2_previousActiveBattleMenu = battleVar2->m2DD_currentActiveBattleMenu;
            drawRing(entity, battleVar2->m2DD_currentActiveBattleMenu);
        }
        if (battleInputButton == 0xFF) {
            break;
        }
        battleRenderDebugAndMain();
        if (battleVar2->m2DB_turnEnded != 0) {
            break;
        }
        if (battleVar2->m2E0_isBattleMenuDisabled == 0) {
            startCharacterJumpToEnemyVar0 = 1;
        }
        switch (battleVar2->m2DD_currentActiveBattleMenu) {
        case 1:
            drawCircleMenuAttackActive(entity);
            break;
        case 2:
            drawCircleMenuItemActive(entity);
            break;
        case 3:
            drawCircleMenuDefend(entity);
            break;
        case 4:
            drawCircleMenuChi(entity);
            break;
        case 100:
            battleVar2->m2E2_previousActiveBattleMenu = 0xFF;
        case 5:
            handleMenuSelectEnemy(entity);
            break;
        case 7:
            drawCircleMenuCombo(entity);
            break;
        case 8:
            drawCircleMenuItemSecondaryActive(entity);
            break;
        case 9:
            if (!confirmForfeit()) drawBattleMenuEscape(entity);
            break;
        case 10:
            drawCircleMenuCallGear(entity);
            break;
        case 16:
            drawGearCircleMenuAttackActive(entity);
            break;
        case 17:
            drawGearCircleMenuItemActive(entity);
            break;
        case 18:
            drawGearCircleMenuCharge(entity);
            break;
        case 19:
            drawGearCircleMenuChi(entity);
            break;
        case 21:
            drawGearCircleMenuBooster(entity);
            break;
        case 22:
            drawGearCircleMenuItemSecondaryActive(entity);
            break;
        case 23:
            if (pcVersusPartyCanExitGear(entity) || !confirmForfeit()) drawGearBattleMenuEscape(entity);
            break;
        case 24:
            drawGearCircleMenuSpecial(entity);
            break;
        case 101:
            battleVar2->m2E2_previousActiveBattleMenu = 0xFF;
        case 25:
            handleGearMenuSelectEnemy(entity);
            break;
        default:
            battleVar2->m2DD_currentActiveBattleMenu = inGear ? 16 : 1;
            break;
        }
    }
    updateCharacterBlinkingTask(0);
    battleVar1->mCB_drawCommandRing = 0;
    battleVar1->mAF_drawApCounter = 0;
    p2Hud1.m90_perPCTimeBarStatus[box] = 0;
    p2Hud1.m7F_drawTimeBarFill[box] = 0;
    p2Hud1.m7C_hpDisplayDirty[box] = 1; /* its AP stored, as setupTurnRenderLoop's end */
    if (!inGear && battleVar2->m2E3_hasAttacked) {
        apConfigArray[entity].m0_storedAP += battleVar2->m2D4_remainingAP;
        if (apConfigArray[entity].m0_storedAP > 0x1C) {
            apConfigArray[entity].m0_storedAP = 0x1C;
        }
    }
    startCharacterJumpToEnemyVar0 = 0;
    stopBattleVibration(entity, 0x100);
    /* freePlayerTurnStructs frees these for the party's slots only */
    freeBattleVar1_AE();
    freeBattleVar1_96();
    freeBattleRenderStruct2(entity);
    freeBillyBattleStructs(entity);
    battleBssBlock.mA78_startPlayingAttackAnimationVar0 = 0;
    pcVersusPartySelection(0);
}

/* battleTickMain, an enemy's turn: 1 when it was player 2's member's (done
 * here, the party's way), 0 for the game's own enemy turn. */
int pcVersusMirrorTurn(u8 entity) {
    sBattleEntity* self;
    u8 label[VERSUS_LABEL];
    s32 k;

    if (!pcVersusMirrorSlot(entity)) {
        return 0;
    }
    k = entity - 3;
    self = &battleEntities.m0_entities[entity];
    pcArenaLog("mirror match: player 2's %d (entity %d), HP %d, AP %d, status %x %x %x (vblank %u)", mirrorChar[k], entity,
               self->m0_base.m4C_HP, apConfigArray[entity].m4_currentAP, self->m0_base.m7C_statusFlags[0],
               self->m0_base.m7C_statusFlags[1], self->m0_base.m7C_statusFlags[2], hwVSyncCount());
    /* its portrait's highlight in player 2's bar, where battleTickMain puts
     * the party's (its pulse: pcVersusViewTick) */
    hudReady();
    setXYWH(&p2Hud0.m63C8_portraitHighlight[D_800CCB04.m30_battleOddOrEven],
            k * 0x60 + 0x10 + partyMemberSpritesOffset[(partyOn ? p2Layout : battlePartyLayoutType) * 3 + k], 8, 0x18,
            0x18);
    p2Hud0.m6414_highlightOddOrEven = D_800CCB04.m30_battleOddOrEven;
    p2Hud0.m6415_drawHighlight = 1;
    int inGear = apConfigArray[entity].m1_gearState != 0;
    if (inGear ? (self->mA4_gear.m7C_statusFlags & 0x2080)
               : ((self->m0_base.m7C_statusFlags[0] & 0x2080) || (self->m0_base.m7C_statusFlags[2] & 0x1000))) {
        p2Hud0.m6415_drawHighlight = 0;
        return 1;
    }
    if (inGear) {
        __builtin_memcpy(battleEntities.m35D8_enemiesBattleStats, p2GearRows[k], sizeof(p2GearRows[k]));
    } else {
        __builtin_memcpy(battleEntities.m35D8_enemiesBattleStats, p2Rows[k], sizeof(p2Rows[k]));
    }
    pcVersusGameText(label, getDialogParamPointer(currentBattleSpecificStrings, battleMonsterMapping[entity]),
                     VERSUS_LABEL - 1);
    pcVersusShowLabel(label);
    __builtin_memcpy(p2Label, label, sizeof(p2Label));
    labelOn = 1; /* kept up for the turn (pcVersusMirrorLabel) */
    p2Turn = entity;
    swapItems(); /* its own items */
    if (!inGear && (self->m0_base.m7C_statusFlags[2] & 0x2000)) {
        performAutoPartyMemberTurn(entity); /* the party's own automatic turn (confused...) */
    } else {
        turnLoop(entity);
    }
    swapItems(); /* player 1's back */
    p2Turn = 0;
    pcArenaLog("mirror match: items (id x count) P1 %d x%d, %d x%d, %d x%d; P2 %d x%d, %d x%d, %d x%d",
               battleEffectBF.m42_ids[0], battleEffectBF.m12_counts[0], battleEffectBF.m42_ids[1],
               battleEffectBF.m12_counts[1], battleEffectBF.m42_ids[2], battleEffectBF.m12_counts[2], p2Items[1][0],
               p2Items[0][0], p2Items[1][1], p2Items[0][1], p2Items[1][2], p2Items[0][2]);
    if (partyOn) {
        partyLogBilly();
    }
    labelOn = 0;
    pcVersusHideLabel();
    previousEntityTurn.m0_lastEntityTurn = entity;
    battleVar1->m97_apMarkCount = 0;
    p2Hud1.m97_apMarkCount = 0;
    updateCharacterBlinkingTask(0);
    loadMonsterAttackName();
    displayAndWaitMonsterAttackName();
    removeCurrentMonsterNameString();
    p2Hud0.m6415_drawHighlight = 0; /* where battleTickMain ends the party's */
    pcArenaLog("mirror match: player 2's turn over: P1 HP %d %d %d, P2 HP %d %d %d", battleEntities.m0_entities[0].m0_base.m4C_HP,
               battleEntities.m0_entities[1].m0_base.m4C_HP, battleEntities.m0_entities[2].m0_base.m4C_HP,
               battleEntities.m0_entities[3].m0_base.m4C_HP, battleEntities.m0_entities[4].m0_base.m4C_HP,
               battleEntities.m0_entities[5].m0_base.m4C_HP);
    return 1;
}
