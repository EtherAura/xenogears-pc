#ifndef PORT_ARENA_H
#define PORT_ARENA_H

/*
 * The Battle arena, for the menu (F1 > Extras > Battle arena, port/src/ui/
 * arena.cpp; xg-htn.35.16): Battling's own PLAYER1 VS PLAYER2 from any
 * field, and back. The game's side and the whole story: include/pc_arena.h,
 * port/src/game/main/arena.c.
 *
 * Settings (settings.ini, XG_<KEY>):
 *   arena_player2_keyboard  1: while Battling runs, the keyboard plays on
 *                           port 2 (player 2) and port 1 is the first
 *                           gamepad's (player 1); 0 (default): port 2 is
 *                           the second gamepad
 *   arena_versus_gears      1: in a versus battle the party fights in its
 *                           Gears (the members who have one); 0 (default):
 *                           on foot or in Gears as it walks
 *   netplay_port, netplay_delay, netplay_address
 *                           Online (port/src/ui/arena.cpp): the port and
 *                           input delay to host with, the last host joined
 *   arena_all_gears         1: every Gear (Argento too) in the arena's Gear
 *                           select; 0 (default): those the story has
 *                           reached, as the game lists them
 *   arena_music             the battle music of arena battles (Battling,
 *                           versus battles; online the host's): 0 (default)
 *                           the game's own, n the game's music ID n - 1
 *                           (1..ARENA_MUSIC_IDS; the menu lists the
 *                           jukebox's songs by title)
 *   arena_opponent_hp       1: in a versus battle the Enemy info window
 *                           (Select) lists the opponent's HP and EP, whatever
 *                           the Enemy info setting; 0 (default): no window
 *                           there (online the host's)
 *   arena_stage             the stage of a mirror match or party vs party:
 *                           0 (default) the match's own (the mirror's
 *                           formation, in Blackmoon Forest), n stage n - 1 of
 *                           the list (pcArenaStageId; any other value is 0);
 *                           a match keeps the one it was asked with
 * XG_ARENA=1: asked from the start (headless runs).
 * XG_VERSUS=<table>,<config>: a versus battle asked from the start;
 * XG_VERSUS=mirror: a mirror match.
 * XG_ARENA_MATCH=<vblank>:party,<card>.<number>[.<id>...],<card>.<number>[.<id>...]:
 * party vs party from those saves (system/arena.c's header); a mirror match
 * or party vs party ends with ,arena=<n> for stage n (one of the list's).
 * XG_VERSUS_VIEW=1|2: this window shows that player's side in a mirror match
 * (as an online session's player does), not the side whose turn it is.
 */

#include "card/party.h"

#ifdef __cplusplus
extern "C" {
#endif

/* pcArenaPhase (the same values as include/pc_arena.h's session). */
enum {
    ARENA_PHASE_IDLE,     /* nothing asked */
    ARENA_PHASE_PENDING,  /* asked: starts at the next moment the player walks free in a field */
    ARENA_PHASE_STARTING, /* taken: the arena's music loads in the field */
    ARENA_PHASE_LEAVING,  /* the field leaves for Battling */
    ARENA_PHASE_IN,       /* in Battling (or back in the field, before the music) */
};

int pcArenaRequest(void); /* 1: asked */
void pcArenaCaptureMenuPicture(void);
char* pcArenaWriteMenuPicture(void); /* SDL-allocated path, owned by the session launcher */
void pcArenaCancel(void); /* a request not taken yet */
int pcArenaPhase(void);
int pcArenaKeyboardOffered(void); /* the keyboard can play as player 2 (pad.c padSetKeyboardPort) */
int pcArenaPlayer2Keyboard(void);
void pcArenaSetPlayer2Keyboard(int on);
int pcArenaAllGearsSetting(void); /* every Gear in the arena's Gear select */
void pcArenaSetAllGears(int on);
#define ARENA_MUSIC_IDS 74 /* the game's music IDs 0..73 (the field's music selector, US and JP) */
int pcArenaMusicSetting(void); /* arena_music: 0 the game's own, else music ID + 1 */
void pcArenaSetMusic(int value);
/* The stages a mirror match or party vs party can be fought on (system/arena.c
 * lists them, each checked by a run): the list's i-th stage's number (0..74;
 * -1 past the list); arena_stage (0 the match's own, else stage number + 1,
 * one of the list's), as pcArenaMusicSetting gives arena_music. */
int pcArenaStageCount(void);
int pcArenaStageId(int i);
int pcArenaStageSetting(void);
void pcArenaSetStage(int value);

/* Versus battles (player 2 commands an enemy formation; include/pc_arena.h):
 * battle table 0..252 (the debug selector's), config 0..15. pcVersusPhase:
 * ARENA_PHASE_IDLE, PENDING, LEAVING (the field starts the battle) or IN, and
 * the formation asked for or running. */
int pcVersusRequest(int table, int config);
int pcVersusRequestMirror(void); /* a mirror match (include/pc_arena.h): player 2 commands a copy of the party */
int pcVersusGearsSetting(void); /* the party in its Gears (those who have one) */
void pcVersusSetGears(int on);
int pcVersusCallGearSetting(void);
void pcVersusSetCallGear(int on);
int pcVersusExitGearSetting(void);
void pcVersusSetExitGear(int on);
/* What each player sees (include/pc_arena.h): this machine's player, set by
 * netplay (1 host, 2 joiner; 0 one screen); the opponent's HP and EP. */
void pcVersusSetPerspective(int player);
int pcVersusPerspective(void);
int pcVersusOpponentHpSetting(void);
void pcVersusSetOpponentHp(int on);
void pcVersusCancel(void);
int pcVersusPhase(int* table, int* config);

/* Matches from anywhere (xg-htn.35.144; system/arena.c has the story): the
 * game kept in memory, the match from its canonical start, then back.
 * pcArenaMatchStart: Battling (table, config, party unused) or a versus
 * battle (the formation; the party's character ids 0..10, 255 none, the
 * first someone; fromSave: the members as the save has them, else as a new
 * game makes them; ignored before a game is loaded). pcArenaMatchPhase:
 * ARENA_PHASE_IDLE, PENDING (asked: at the next vblank), IN (the match runs)
 * or LEAVING (the kept game comes back), and the match's kind. */
enum {
    ARENA_MATCH_BATTLING = 1,
    ARENA_MATCH_VERSUS = 2,
    ARENA_MATCH_MIRROR = 3, /* a versus battle against a copy of the party */
    ARENA_MATCH_ROSTER = 5, /* a player-built enemy roster */
    ARENA_MATCH_PARTY = 4,  /* party vs party: each player's party from a save (pcArenaMatchStartParty) */
};
int pcArenaMatchAvailable(const char** why); /* 0 with why (a sentence) where a match cannot start now */
int pcArenaMatchStart(int kind, int table, int config, const int party[3], int fromSave); /* not ARENA_MATCH_PARTY */
int pcArenaMatchStartSaved(int kind, int table, int config, const XgPartyRecord* party);
/* Party vs party (system/arena.c's header): player 1's party against player
 * 2's, each a checked party record (card/party.h; they may be the same save,
 * the same members). 1 when asked; 0 with why where not (a match cannot
 * start now, or a record fails xgPartyCheck). */
int pcArenaMatchStartParty(const XgPartyRecord* player1, const XgPartyRecord* player2, const char** why);
/* A save's party as XG_ARENA_MATCH gives it (<card>.<number>[.<id>...]),
 * packed from the cards with the code table `codes`: 1 when packed and
 * checked (the reason on stderr where not). */
int pcArenaPackSave(const char* spec, const unsigned char* codes, XgPartyRecord* out);
/* Online party vs party (netplay.c): the session's two records, player 1's
 * (the host's) and player 2's, checked; each of the session's boots starts
 * the match with them (system/arena.c's header). NULL, NULL: none. */
void pcArenaSessionParties(const XgPartyRecord* player1, const XgPartyRecord* player2);
void pcArenaSessionVersus(int kind, int table, int config, const int members[3], const XgPartyRecord* party);
int pcArenaMatchPhase(int* kind);
int pcArenaMatchKind(void); /* the match's kind while one is asked or runs, else 0 */
void pcArenaMatchQuit(void); /* the menu's Leave the match: the kept game back */
void pcArenaVBlank(void);    /* every vblank (netplay.c netplayPace) */

#ifdef __cplusplus
}
#endif

#endif /* PORT_ARENA_H */
