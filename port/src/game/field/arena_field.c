/*
 * The Battle arena, the field's side (include/pc_arena.h; the session and
 * what the arena's reception does: port/src/game/main/arena.c).
 *
 * fieldEntryPoint calls this each frame where the player could open the
 * menu (src/field/f_800775C0.c: no mode change, script exit, field change
 * or load pending). The field's own steps, as the reception's script makes
 * them before and after Battling (field 222, actor 6 entry 6; fields 221 and
 * 223 load the same song before their story fights):
 *   - taken (the player free, by the field's own test for its menu, and no
 *     battle, Battling or song asked for):
 *     op 0x72 0x0B, Battling's song as the field's music at volume 0
 *     (f_80086D8C.c OP_72, playMusicFromScript); the field loads it;
 *   - loaded (this is called again once nothing loads): op 0xFE 0x0E 0x7F
 *     0xF0, the song's fade in (OP_FADE_MUSIC), and op 0xFE 0xBF 1 0 0 0 0 0
 *     (OPX_BF): Battling's main menu; the field leaves for it on its next
 *     odd frame (fieldEntryPoint's battlingModeReady test);
 *   - back from Battling: the field's own music again, as op 0x75 would
 *     play it (OP_PLAY_MUSIC: loaded again even when it is the same song,
 *     since Battling changed its tempo), and the session's bytes back.
 * A versus battle starts here too (startVersusBattle).
 * Without a request this returns at once: the game is as it was.
 */
#include "common.h"
#include "field/field.h"
#include "main/sound.h"
#include "pc_arena.h"

#define ARENA_SONG 0x0B /* Battling's music (fields 221-223 load it before op 0xBF) */

extern u8 battlingEntryParams[];
extern s32 fieldBattleRequested;
extern s32 battlingModeReady;
extern s32 bBattleSequenceInitialized;
extern s32 playMusicAuthorized;
extern s32 loadCompleted;
extern s32 fieldMusicLoadPending;
extern s32 currentlyPlayingMusic;
extern s32 musicVar1;
extern s32 musicVar2;
extern s32 updateMusicState2Var3;
extern s32 fieldMapNumber;
extern s32 playerCanRun;
extern s32 g_menuIdToOpen;
extern u8 g_menuDisabled;
extern u8 fieldTransitionCompleted;
extern s32 g_playerControlledActor;
extern sFieldEntity* D_800AFB10;
extern FieldBss_800B2184 op9DVar1;
extern u8 requestedBattleConfig;
extern u8 battleInitVar1;
extern s32 mechaOverlayLoadingMode;
void loadGameModeOverlay(s32 mode);
void resetBattleAudioState(void);
void playMusic(s32 musicId);
void setBattlingMainTaskIndex(s32 index); /* battlingMainTaskIndex */

/* The player walks free, and a song may start now (playMusicFromScript's
 * own waits). */
static int playerFree(void) {
    return g_menuDisabled == 0 && g_menuIdToOpen == 0xFF && playerCanRun == 1 && fieldTransitionCompleted != 0 &&
           (D_800AFB10[g_playerControlledActor].m4C_scriptEntity->m0_fieldScriptFlags & 0x1800) == 0 &&
           playMusicAuthorized != 0 && bBattleSequenceInitialized == 0 && battlingModeReady == -1 &&
           fieldBattleRequested == 0 && loadCompleted == 0 && musicVar2 != 1 && fieldMusicLoadPending != -1;
}

/* playMusicFromScript's start of a song while the field runs; volume0: op
 * 0x72 (at volume 0, for a fade in), else op 0x75. */
static void startSong(s32 music, s32 volume0) {
    musicVar1 = volume0 ? 0 : -1;
    resetBattleAudioState();
    currentlyPlayingMusic = music;
    fieldMusicLoadPending = -1;
    playMusic(music);
}

/* A versus battle (include/pc_arena.h): the field starts it as it starts a
 * random battle (f_80078B5C.c checkForRandomEncounter), with the regular
 * battle theme; the battle puts the formation in (versus.c). */
static void startVersusBattle(s32 config) {
    op9DVar1.m10C_battleMusicId = 0x1D;
    requestedBattleConfig = config;
    battleInitVar1 = 0;
    if (mechaOverlayLoadingMode == 0) {
        loadGameModeOverlay(2);
    }
    playMusicAuthorized = 0;
    bBattleSequenceInitialized = 1;
}

void pcArenaFieldTick(void) {
    s32 music;
    int table, config;

    switch (pcArenaSession()) {
    case PC_ARENA_IDLE:
        pcArenaKeyboard(0); /* after a soft reset in Battling */
        if (pcVersusSession() == PC_VERSUS_LEAVING) {
            pcVersusAbort(); /* the field is back, and no battle took it */
        }
        if (pcVersusPending(&table, &config) && pcVersusSession() == PC_VERSUS_IDLE && playerFree()) {
            pcVersusTaken();
            pcVersusBegin(table, config);
            startVersusBattle(config);
            return;
        }
        if (!pcArenaPending() || !playerFree()) {
            return;
        }
        pcArenaTaken();
        pcArenaBegin(currentlyPlayingMusic, fieldMapNumber);
        startSong(ARENA_SONG, 1);
        return;
    case PC_ARENA_STARTING:
        if (fieldMapNumber != pcArenaStartField()) {
            /* the player left the field while the song loaded: the next
             * field's script may have played its own music, else the song
             * would go on there */
            music = pcArenaAbort();
            if (currentlyPlayingMusic == ARENA_SONG && music != ARENA_SONG) {
                startSong(music, 0);
            }
            return;
        }
        if (!playerFree()) {
            return;
        }
        if (updateMusicState2Var3 != 0) {
            playBattleMusic(pMusic, 0x7F, 0xF0);
        }
        setBattlingMainTaskIndex(0);
        if (!pcArenaDirectEntry(battlingEntryParams)) { /* XG_ARENA=2,...: straight into a match (the oracle's runs) */
            battlingEntryParams[0] = 1; /* Battling's main menu */
            battlingEntryParams[1] = 0;
            battlingEntryParams[2] = 0;
            battlingEntryParams[3] = 0;
            battlingEntryParams[4] = 0;
            battlingEntryParams[5] = 0;
        }
        fieldBattleRequested = 1;
        battlingModeReady = 0;
        pcArenaLeaving();
        return;
    case PC_ARENA_LEAVING:
        if (battlingModeReady == -1) {
            /* a battle went first (the field is back from it): ask again */
            pcArenaBegin(pcArenaEnd(), fieldMapNumber);
        }
        return;
    case PC_ARENA_IN:
        if (musicVar2 == 1 || loadCompleted != 0 || fieldMusicLoadPending == -1 || playMusicAuthorized == 0) {
            return;
        }
        music = pcArenaEnd();
        startSong(music, 0);
        return;
    }
}
