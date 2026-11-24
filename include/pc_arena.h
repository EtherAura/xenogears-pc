#ifndef PC_ARENA_H
#define PC_ARENA_H

/*
 * The Battle arena (F1 > Extras > Battle arena; xg-htn.35.16): Battling's
 * own PLAYER1 VS PLAYER2, from wherever the player walks, and back.
 *
 *   port/src/system/arena.c (.h)     the request, the menu's side, player 2's
 *                                    keyboard (platform layer)
 *   port/src/game/main/arena.c       the session and what it puts back (main)
 *   port/src/game/field/arena_field.c the field's side: the start as the
 *                                    arena's reception does it, the return
 *   port/src/game/battling/thread.c  Battling's task: pcArenaBattlingStart
 *   port/src/game/battle/versus.c    versus battles: player 2's choices
 *   port/src/game/battle/mirror.c    mirror matches: player 2's copy of the party
 *   port/src/game/battling/arena.c   Battling's side of a match: its music,
 *                                    its end's Rematch / Leave
 *   port/src/ui/arena.cpp            the menu block
 */

#ifdef __cplusplus
extern "C" {
#endif

/* The session (pcArenaSession; port/src/system/arena.h's phases, which add
 * PENDING for a request not taken yet). */
enum {
    PC_ARENA_IDLE = 0,
    PC_ARENA_STARTING = 2, /* taken: the arena's music loads in the field */
    PC_ARENA_LEAVING = 3,  /* op 0xBF written: the field leaves for Battling */
    PC_ARENA_IN = 4,       /* in Battling (or back in the field, before the music) */
};

/* Platform, for the game. */
void pcArenaLog(const char* fmt, ...); /* XG_ARENA_LOG=1 */
int pcArenaPending(void);       /* asked (or XG_ARENA), not taken yet */
void pcArenaTaken(void);        /* the field takes it now */
void pcArenaKeyboard(int arena); /* 1 in Battling: player 2's keyboard, if chosen */

/* Main (port/src/game/main/arena.c). */
int pcArenaSession(void); /* PC_ARENA_IDLE or PC_ARENA_STARTING.. */
void pcArenaBegin(int music, int field);
int pcArenaStartField(void);   /* the field it started in */
void pcArenaLeaving(void);
void pcArenaBattlingStart(void); /* battlingMainTask */
int pcArenaEnd(void);          /* back in the field: the music to play again */
int pcArenaAbort(void);        /* the player left the field first: its music */

/* Versus battles: player 2 commands an enemy formation (a battle table of
 * the battle debug selector, directory 0x20/3 file table + 7, and a config
 * in it) in a normal battle, the party the player's.
 *   platform (port/src/system/arena.c): the request (the menu, XG_VERSUS) */
enum {
    PC_VERSUS_IDLE = 0,
    PC_VERSUS_LEAVING = 3, /* the field starts the battle */
    PC_VERSUS_IN = 4,      /* in the battle */
};
int pcVersusPending(int* table, int* config);
int pcVersusGears(void); /* the party in its Gears (the setting) */
void pcVersusTaken(void);
/*   main (port/src/game/main/arena.c): the session and the checkpoint */
int pcVersusSession(void);
int pcVersusTable(void);
int pcVersusConfig(void);
void pcVersusBegin(int table, int config);
int pcVersusCheckpoint(void); /* battleMain's start: 1 for a versus battle */
void pcVersusRestore(void);   /* battleMain's end */
void pcVersusAbort(void);
/*   battle (port/src/game/battle/versus.c): battleMain and battleTickMain's
 *   hooks, randomInRange/getRandomValueInRange's plan */
void pcVersusBattleSetup(void);
void pcVersusMonsterTurn(unsigned char entity, int status);
void pcVersusBattleOver(void);
int pcVersusLastResult(void); /* 0x81 player 2 won, 0x40 bit player 1 escaped, else player 1 won; 0 none */
void pcVersusBattleEnd(void);
void pcVersusDraw(void);
int pcVersusRandom(int lo, int hi, int* out);

/* Mirror match (xg-htn.35.143): a versus battle where player 2 commands a copy
 * of player 1's party (its members in enemy slots 3-5, on foot), with the
 * party's own commands, in the mirror's stage (VERSUS_MIRROR_TABLE/CONFIG).
 *   platform (port/src/system/arena.c): the request is a mirror match */
#define VERSUS_MIRROR_TABLE 2  /* the stage: Jackal x2, Hobgob x2 (set 1, on foot) */
#define VERSUS_MIRROR_CONFIG 1
int pcVersusMirrorAsked(void);
/*   main (port/src/game/main/arena.c): this versus battle is a mirror match */
int pcVersusMirror(void);
/*   battle (port/src/game/battle/mirror.c): battleMain, battleTickMain,
 *   battleHandleInput and the battle loader's hooks */
void pcVersusBattleLoaded(void);
int pcVersusMirrorTurn(unsigned char entity);
int pcVersusMirrorPad2(void);
int pcVersusMirrorSlot(int entity);
void pcVersusMirrorSprites(void);
/* What each player sees (display only; mirror.c's header): their own side's
 * status bar, and the opponent's HP and EP only when the match lets them.
 *   platform: whose this machine shows: 0 one screen (the side whose turn
 *   began last), 1 or 2 that player's (online netplay's player number; for
 *   tests XG_VERSUS_VIEW=1|2); the Enemy info window of a versus battle lists
 *   the opponent's HP and EP (arena_opponent_hp, 0 by default; online the
 *   host's) */
int pcVersusPerspective(void);
int pcVersusOpponentHpSetting(void);
/*   battle (mirror.c): the status bar's hooks (src/battle/b_800728B8.c:
 *   HUD_SLOT, drawBattleMode1, battleDrawAPBar, wideBattleHud; b_8008A274.c,
 *   b_80070E2C.c) and Billy's reload (b_800941A4.c AMMO_SLOT) */
int pcBattleHudSlot(int slot);
int pcVersusViewBar(void);
void pcVersusViewSwap(int in);
void pcVersusViewTick(void);
void pcVersusViewTurn(unsigned char entity);
void pcVersusViewHpShown(void);
int pcVersusPartySlot(int slot);
void pcVersusSpeedLog(int entity, int wait); /* battleGetSlotStatusSub (b_800941A4.c PARTY_SPEED) */

/* Field (port/src/game/field/arena_field.c): fieldEntryPoint, where the
 * player could open the menu. */
void pcArenaFieldTick(void);
/* Platform: XG_ARENA=2,GEAR1,GEAR2[,WINS[,COM]] (the oracle's runs): 1 when
 * it wrote Battling's entry bytes for that match (each by its name). */
int pcArenaDirectEntry(void);

/* The battle music (xg-htn.35.146): the music ID the players chose for an
 * arena battle (the setting arena_music; online the host's, held for the
 * session), else `def`, the game's own (a versus battle's 0x1D, Battling's
 * 0x0B). Read where the battle's music starts, so both games of a session
 * start the same song at the same frame.
 *   platform: */
int pcArenaMusic(int def);
/* The field's music selector for music ID `id` (src/field/f_8008083C.c
 * playMusic, musicLookupTable): its sample bank file's number (0xFF: the
 * resident banks do) and flags (1: the second resident bank makes room);
 * 0 for an ID past the table. */
int pcArenaMusicSelector(int id, int* wave, int* flags);
/*   battling (port/src/game/battling/arena.c): a match's Battling starts
 *   from the kernel, without the field that loads its song: the song then,
 *   as the field loads it (thread.c, before battlingMainTask takes pMusic). */
void pcArenaBattlingMusic(void);

/* Matches (xg-htn.35.144, .142): started from anywhere, from their canonical
 * start, and ended by Rematch / Leave (port/src/system/arena.c has the
 * story; online: port/src/system/netplay.c).
 *   platform: */
int pcArenaMatchOn(void);       /* 1 a local match (the game kept), 2 an online session; 0 none */
void pcArenaMatchRematch(void); /* both chose Rematch: the game's softReboot comes next (the same start) */
void pcArenaMatchLeave(void);   /* a player chose Leave: the platform takes the game back at a vblank */
/*   main (port/src/game/main/arena.c): the end's prompt, each player on their
 *   own pad (both pads' bytes: online, the exchanged ones) */
enum { PC_PROMPT_WAIT, PC_PROMPT_REMATCH, PC_PROMPT_LEAVE };
enum { PC_PROMPT_SOUND_NONE, PC_PROMPT_SOUND_MOVE, PC_PROMPT_SOUND_CONFIRM, PC_PROMPT_SOUND_CANCEL };
void pcArenaPromptBegin(void);
int pcArenaPromptActive(void);
int pcArenaPromptStep(int* sound); /* each frame: PC_PROMPT_*, once; the sound to play */
/* The prompt as it is now: 0 none, 1 choosing, 2 Leave decided, 3 Rematch
 * decided; each player's pick (0 Rematch, 1 Leave) and whether they are
 * ready; players: 1 when player 1 decides alone (no second pad), else 2.
 * The game draws it (versus.c in the battle's dialogue window, battling/
 * arena.c in Battling's own text). */
int pcArenaPromptState(int* pick, int* ready, int* players);
/*   battling (port/src/game/battling/arena.c): updateBattlingVictoryScreen */
int pcArenaBattlingVictory(void); /* 1: the prompt holds the screen (its confirm ends nothing) */

#ifdef __cplusplus
}
#endif

#endif /* PC_ARENA_H */
