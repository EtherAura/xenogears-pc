/*
 * A Battle arena match in Battling (include/pc_arena.h): its music, and its
 * end's Rematch / Leave.
 *
 * The music (xg-htn.35.146). A field's session starts Battling as the
 * reception does, after the field has loaded the song (arena_field.c); a
 * match (one from the menu, which keeps the game it came from, or an online
 * session) starts it from the kernel (mode 4), where no field loads one, so
 * pMusic, which Battling plays (battlingMainTask: battlingPlaysOwnMusic is 0),
 * would be none. Here, before battlingMainTask (thread.c), the song is
 * loaded and started the way the field does it (src/field/f_8008083C.c
 * playMusic, updateMusicState2, playMusicSub): the field's music selector
 * for the song (its sample bank file 1C/13 + 2n, or none for one the
 * resident banks play; flag 1: the second resident bank, wdsFile3 here as
 * the kernel loaded it, makes room first), the bank, the score 1C/14 + 2n in
 * battleMusic, its instance started at full volume. The song is Battling's
 * own 0x0B unless the players chose another (pcArenaMusic). All of it reads
 * the disc and runs the game's own sound driver in the game's memory: both
 * games of a session do the same at the same frame.
 *
 * The end (xg-htn.35.142, .150): PLAYER1 VS PLAYER2's victory screen
 * (src/battling/bl_800725B0.c updateBattlingVictoryScreen) holds the players'
 * Rematch / Leave (port/src/game/main/arena.c) in a local match or an online
 * session, in place of the winner's confirm that ends it: the winning Gear
 * turns on, the result stays up, and the choice is drawn as that screen
 * draws its result (its 640-pixel-wide picture): Battling's font and text
 * (printBattlingText, the result's scale 0x1C0), left-aligned at the
 * result's edge, above it and left of the Gear: each player's name, then
 * REMATCH and LEAVE, the pick blinking yellow as a Battling menu's selected
 * item does (setBattlingTextBlinkYellow); a player who is ready has READY
 * after their name and the pick steady. Battling's text holds 100
 * glyphs a frame, which the result's text nearly fills: the prompt's glyphs
 * are polygons of their own, put in place of battlingGlyphPolys while it
 * prints, so the game's text is drawn as before. Only drawing: the choice is
 * the game's memory and pads.
 *
 * Rematch: the same two Gears again, as many wins needed and the same COM
 * level, from the match's start: the game's soft reset into Battling, its
 * entry bytes (battlingEntryParams and the five after it, main's data, which
 * a soft reset keeps, as on the PS1; on the PC each is an object of its own,
 * so each is written by its name) asking for the PC's own entry 3, PLAYER1
 * VS PLAYER2 without the menus (src/battling/bl_80083DCC.c battlingMainTask,
 * match type 3).
 * Leave: the platform takes the game back (the kept game, or the session's
 * end). Without a match, or another mode than PLAYER1 VS PLAYER2, nothing
 * changes.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "main/sound.h"
#include "battling/battling.h"
#include "pc_arena.h"

#define ENTRY_REMATCH 3 /* battlingEntryParams[0]: neither the menu (1) nor a story fight (2, 0 once taken) */
#define MATCH_PVP 3     /* battlingEntryParams[1]: PLAYER1 VS PLAYER2 (the PC's) */

extern u8 battlingEntryParams;     /* the entry's kind */
extern u8 battlingEntryMatchType;  /* battlingEntryParams[1] on the PS1 */
extern u8 battlingEntryGear1;      /* [2] */
extern u8 battlingEntryGear2;      /* [3] */
extern u8 battlingEntryWinsNeeded; /* [4] */
extern u8 battlingEntryComLevel;   /* [5] */
extern s32 battlingGameMode;
extern BattlingGear battlingGear1;
extern BattlingGear battlingGear2;
extern BattlingSettings battlingSettings;
void playBattlingSfx(s32 id);
void softReboot(void); /* src/main/kernel.c */

/* ---- the music ---- */

#define ARENA_SONG 0x0B /* Battling's own (the reception loads it before op 0xBF) */
#define MUSIC_DIR 0x1C
#define MUSIC_SCORE(id) ((id) * 2 + 0x14)
#define MUSIC_BANK(wave) ((wave) * 2 + 0x13)

extern u8 battleMusic[0x3200]; /* main: the music's score (the field's and the world map's buffer) */
extern s32 currentlyLoadedWave;
extern s32 playMusicSubVar0;
extern s32 playMusicSubVar2;
extern s32 playWorldmapMusic;
SoundInstance* createMusicInstance(void* data);
void startMusicInstance(SoundInstance* inst, s32 volume, s32 arg);

void pcArenaBattlingMusic(void) {
    s32 id;
    s32 wave;
    s32 flags;
    s32 dir;
    s32 sub;
    Wds* wds;

    if (pcArenaMatchOn() == 0 || pcArenaSession() != PC_ARENA_IDLE) {
        return; /* no match, or a field's session: the field loaded the song */
    }
    id = pcArenaMusic(ARENA_SONG);
    if (!pcArenaMusicSelector(id, &wave, &flags)) {
        return;
    }
    getCurrentDirectory(&dir, &sub);
    setCurrentDirectory(MUSIC_DIR, 0);
    if (flags == 1 && playMusicSubVar0 == 0 && wdsFile3 != NULL) {
        /* playMusicSub: the large bank takes the second resident one's room */
        playWorldmapMusic = 1;
        unloadWds(wdsFile3);
        playMusicSubVar0 = 1;
        playMusicSubVar2 = 0;
    }
    if (wave != 0xFF && wave != currentlyLoadedWave) {
        wds = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(MUSIC_BANK(wave))), 1);
        if (wds != NULL) {
            readFile(MUSIC_BANK(wave), wds, 0, 0x80);
            waitReadCompletion(0);
            loadWds(wds, 0);
            waitForMusic(0x10);
            freeHeap(wds);
            currentlyLoadedWave = wave;
        }
    }
    if (getFileSizeAligned(MUSIC_SCORE(id)) <= (s32)sizeof(battleMusic)) {
        readFile(MUSIC_SCORE(id), battleMusic, 0, 0x80);
        waitReadCompletion(0);
        pMusic = createMusicInstance(battleMusic);
        startMusicInstance(pMusic, 0x7F, 0);
        pcArenaLog("Battling's music: %d (sample bank %d, flags %d)", id, wave, flags);
    }
    setCurrentDirectory(dir, sub);
}

/* ---- the end's Rematch / Leave, drawn as the victory screen draws its result ---- */

#define PROMPT_GLYPHS 101 /* drawBattlingChar's limit (the prompt draws 48 at most) */
#define PROMPT_X 0x18     /* the result's left edge (updateBattlingVictoryScreen) */
#define PROMPT_INDENT 0x30
#define PROMPT_GAP 0x20
#define PROMPT_TOP 0x0C   /* above the result (y 0x9A..) and left of the Gear */
#define ROW 0x14          /* printBattlingText's line */

extern POLY_FT4* battlingGlyphPolys[2];
extern s32 battlingGlyphCount;
extern s32 battlingTextScale;
extern s16 battlingTextX;
extern s16 battlingTextY;
extern u8 battlingTextR;
extern u8 battlingTextG;
extern u8 battlingTextB;
extern u8 battlingBufferIndex;
extern OTag* battlingCurrentOt;
void setBattlingTextPos(s32 x, s32 y);
void setBattlingTextScale(s32 scale);
void setBattlingTextBlinkYellow(s32 on);
void printBattlingText(u8* str);
s32 getBattlingTextWidth(u8* str);
void pcWideBattlingGlyphs(s32 first, s32 count, s32 side); /* wide.c */

static POLY_FT4 promptGlyphs[2][PROMPT_GLYPHS];

/* One player's three rows: who (and READY once they are), then Rematch and
 * Leave, the pick blinking while they choose, steady once they are ready. */
static void promptPlayer(s32 y, s32 player, s32 pick, s32 ready) {
    static u8 names[2][8] = {"PLAYER1", "PLAYER2"};
    static u8 words[2][8] = {"REMATCH", "LEAVE"};
    static u8 readyWord[] = "READY";
    s32 o;

    setBattlingTextBlinkYellow(0);
    if (ready) {
        setBattlingTextPos(PROMPT_X + getBattlingTextWidth(names[player]) + PROMPT_GAP, y);
        printBattlingText(readyWord);
    }
    setBattlingTextPos(PROMPT_X, y);
    printBattlingText(names[player]);
    battlingTextX += PROMPT_INDENT;
    for (o = 0; o < 2; o++) {
        setBattlingTextBlinkYellow(o == pick && !ready);
        if (o == pick && ready) {
            battlingTextR = battlingTextG = 0xFF; /* chosen: steady yellow */
            battlingTextB = 0;
        }
        printBattlingText(words[o]);
    }
    setBattlingTextBlinkYellow(0);
}

/* updateBattlingVictoryScreen, before its own text: the prompt's glyphs in
 * this frame's ordering table, the game's text state as it was. */
static void promptDraw(void) {
    s32 pick[2];
    s32 ready[2];
    s32 players;
    s32 p;
    s32 i;
    POLY_FT4* polys = battlingGlyphPolys[battlingBufferIndex];
    s32 count = battlingGlyphCount;
    s32 scale = battlingTextScale;
    s16 x = battlingTextX;
    s16 y = battlingTextY;
    u8 r = battlingTextR;
    u8 g = battlingTextG;
    u8 b = battlingTextB;

    if (pcArenaPromptState(pick, ready, &players) != 1) {
        return;
    }
    battlingGlyphPolys[battlingBufferIndex] = promptGlyphs[battlingBufferIndex];
    battlingGlyphCount = 0;
    setBattlingTextScale(0x1C0); /* the result's: the screen is 640 wide */
    for (p = 0; p < players; p++) {
        promptPlayer(PROMPT_TOP + p * 3 * ROW + p * 8, p, pick[p], ready[p]);
    }
    pcWideBattlingGlyphs(0, battlingGlyphCount, 0); /* true widescreen: to the left edge, with the result */
    for (i = 0; i < battlingGlyphCount; i++) {
        AddPrim(battlingCurrentOt, &promptGlyphs[battlingBufferIndex][i]);
    }
    battlingGlyphPolys[battlingBufferIndex] = polys;
    battlingGlyphCount = count;
    battlingTextScale = scale;
    battlingTextX = x;
    battlingTextY = y;
    battlingTextR = r;
    battlingTextG = g;
    battlingTextB = b;
}

int pcArenaBattlingVictory(void) {
    int sound;
    int d;

    if (battlingGameMode != 2 || !pcArenaMatchOn()) {
        return 0;
    }
    if (!pcArenaPromptActive()) {
        pcArenaPromptBegin();
    }
    d = pcArenaPromptStep(&sound);
    if (sound == PC_PROMPT_SOUND_MOVE) {
        playBattlingSfx(0x1E);
    } else if (sound == PC_PROMPT_SOUND_CONFIRM) {
        playBattlingSfx(0x21);
    } else if (sound == PC_PROMPT_SOUND_CANCEL) {
        playBattlingSfx(0x24);
    }
    if (d == PC_PROMPT_REMATCH) {
        battlingEntryParams = ENTRY_REMATCH;
        battlingEntryMatchType = MATCH_PVP;
        battlingEntryGear1 = battlingGear1.m909_gearId;
        battlingEntryGear2 = battlingGear2.m909_gearId;
        battlingEntryWinsNeeded = battlingSettings.m7_winsNeeded;
        battlingEntryComLevel = battlingSettings.m0_comLevel;
        pcArenaLog("rematch: Gears %d and %d, %d wins needed", battlingEntryGear1, battlingEntryGear2,
                   battlingEntryWinsNeeded);
        pcArenaMatchRematch();
        softReboot();
    } else if (d == PC_PROMPT_LEAVE) {
        pcArenaMatchLeave(); /* the game is taken back at a vblank: the screen goes on until then */
    }
    promptDraw();
    return 1;
}
