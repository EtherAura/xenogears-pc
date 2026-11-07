/*
 * Original-game bug fixes in the field scripts and their music (F1 > Gameplay >
 * Original-game bug fixes, off by default). Byte patches are made as a field's
 * script is decompressed (initFieldData), each only where the retail bytes are
 * found exactly (another disc's script, a modified one: nothing changes). US
 * and JP disc 1 have the same bytes in every script below.
 *
 *   Field 87, the Nisan inn's late-story variant (its innkeeper's routine
 *   when Nisan's variable 0x102 is 0x41 or more): it plays the rest jingle
 *   as music (OP_PLAY_MUSIC 0x0D), fades, restores HP and EP and fades in,
 *   but never starts the town's music again, so the field stays silent once
 *   the jingle ends. Every other inn plays its town theme after the rest
 *   (Bledavik 104/105: jingle 0x0C, then OP_PLAY_MUSIC 0x26; also 289, 313).
 *   The fix: the routine's last OP_WAIT 10, after the morning line, becomes
 *   OP_PLAY_MUSIC of the music that played as the field loaded (the town's,
 *   which the inn keeps: the inn's script plays no music of its own).
 *
 *   Field 213, the Kislev battlers' ward, Fei's rest after the Baptism (its
 *   director, variable 0x242 bits 0 and 2): the long Doze (music 0x0D, about
 *   620 vblanks with its release) plays over a black screen for OP_WAIT 240
 *   (about 490 vblanks: a unit is a 30 Hz script frame), then actor 1's routine 15 starts the ward's music
 *   (OP_72 0x47), which cuts the Doze during its last bar. The ward's bed rest
 *   (subroutine 0x3B08) plays the same Doze for OP_WAIT 240 + OP_WAIT 90 and
 *   lets it end (a wait is at most 255: its counter is a byte). The fix: the
 *   14 bytes from the OP_FADE_MUSIC before the Doze become OP_PLAY_MUSIC 0x0D,
 *   OPX_A2, OP_WAIT 240, OP_WAIT 90, OP_WAIT 1, as the bed's. The fade goes:
 *   it never ran (the OP_PLAY_MUSIC on the next frame stops the music it
 *   fades).
 *
 *   Field 282, the Yggdrasil, Emeralda's first scene (game progress 207):
 *   Gate Generator 3 (field 505) plays her theme (0x19), fades it out
 *   (OP_FADE_MUSIC 0) and leaves for the world map; the theme stays the
 *   current music (gameState 0x2322) though nothing is heard, so the scene's
 *   cue, OP_PLAY_MUSIC 0x19 after her laugh, is skipped (playMusicFromScript
 *   ignores the current music) and the scene stays without music. The fix:
 *   when the field loads in that state, the faded theme is forgotten (current
 *   music 0xFF), so the cue starts it as the script means to.
 *
 *   Nisan late in the story (variable 0x102 0x42 or more, progress above
 *   207): the town's rooms 93/94/474 play Elly's theme (0x1E) faster
 *   (OPX_0F pitch 1, OPX_10 tempo 0x1C0); the inner rooms keep the music that
 *   plays. Loading a save in one of them (the shop) starts the theme from the
 *   save's music id at its normal speed until the town's selection runs again.
 *   The fix: the theme started in Nisan in that state gets the town's pitch
 *   and tempo (pcFieldMusicStarted; the town's own selection sets the same
 *   values again).
 *
 *   Field 50, the Aveh desert motorcycle trail: the soldier Fei takes the
 *   motorcycle from ("Hey, wa~it!") and the motorcycle's actor are deleted
 *   (OP_DELETE_ENTITY 0x0A, 0x09) on the frame the fade to black starts, so
 *   the soldier vanishes in full light. The fix: OP_START_FADE_TO_BLACK 15
 *   and its OP_WAIT 15 come first, the two deletions after (same bytes,
 *   reordered), before OP_START_FADE_IN.
 *
 *   Field 326, the orphanage's front garden: its entrance 0, the one the
 *   world map uses (exit table: field 326, return mode 0), takes the party's
 *   facing from field variable 6, which only a field exit sets
 *   (SaveFieldAndDirections); the world map leaves it as the last exit left
 *   it. Leaving the garden for the world map walks down-screen, so coming
 *   back the party faces the gate instead of the garden. The fix: entrance
 *   0's facing byte (0xFF: take variable 6) becomes 0, up-screen into the
 *   garden, the way a walk in from field 302 (also entrance 0) faces anyway.
 */
#include "common.h"
#include "main/sound.h"
#include "pc_gameplay.h"

extern s32 currentlyPlayingMusic;
extern s32 fieldMapNumber;
s32 getVariable(s32 varIndex);
void setMusicInstancePitch(SoundInstance* inst, s32 val, s32 dur);
void setMusicInstanceTempo(SoundInstance* inst, s32 tempo, s32 dur);

/* OP_PLAY_MUSIC 0D, OPX_A2, OP_WAIT 0A, OP_START_FADE_TO_BLACK 3C, OP_WAIT 46,
 * OP_RESTORE_HP, OP_RESTORE_MP, OP_WAIT 1E, OP_START_FADE_IN 3C, OP_WAIT 46,
 * OP_SHOW_DIALOG_WINDOW_MODE0 0E, OP_WAIT_DIALOG, OP_WAIT 0A (-> the music),
 * OP_DISABLE_RANDOM_BATTLES. */
static const u8 nisanInn[] = {
    0x75, 0x0D, 0x80, 0xFE, 0xA2, 0x26, 0x0A, 0x80, 0xB4, 0x3C, 0x80, 0x26, 0x46, 0x80, 0x79, 0x7A, 0x26,
    0x1E, 0x80, 0xB3, 0x3C, 0x80, 0x26, 0x46, 0x80, 0xD2, 0x0E, 0x00, 0x10, 0x9C, 0x26, 0x0A, 0x80, 0x14,
};
#define NISAN_INN_WAIT 30 /* the OP_WAIT 0A that becomes OP_PLAY_MUSIC */

/* OP_FADE_MUSIC 0 over 60, OP_PLAY_MUSIC 0D, OPX_A2, OP_WAIT 240 (-> 330),
 * OP_SET_SCREEN_GEOMETRY; the bed's: OP_PLAY_MUSIC 0D, OPX_A2, OP_WAIT 240,
 * OP_WAIT 90 (and OP_WAIT 1 for the last of the 14 bytes). */
static const u8 wardRest[] = {
    0xFE, 0x0E, 0x00, 0x80, 0x3C, 0x80, 0x75, 0x0D, 0x80, 0xFE, 0xA2, 0x26, 0xF0, 0x80, 0xA0,
};
static const u8 wardRestFixed[] = {
    0x75, 0x0D, 0x80, 0xFE, 0xA2, 0x26, 0xF0, 0x80, 0x26, 0x5A, 0x80, 0x26, 0x01, 0x80,
};

/* Emeralda's laugh and cue: OP_WAIT 6, OP_PLAY_MUSIC 19,
 * OP_SHOW_DIALOG_WINDOW_MODE0 1E. */
static const u8 emeraldaCue[] = {0x26, 0x06, 0x80, 0x75, 0x19, 0x80, 0xD2, 0x1E, 0x00, 0x11};
#define EMERALDA_THEME 0x19
#define EMERALDA_PROGRESS 207

/* OP_RUN_ENTITY_SCRIPT_BLOCKING 0A 24, OP_DELETE_ENTITY 0A, OP_DELETE_ENTITY 09,
 * OP_START_FADE_TO_BLACK 15, OP_WAIT 15, OP_START_FADE_IN 15. */
static const u8 soldierFade[] = {
    0x09, 0x0A, 0x24, 0x29, 0x0A, 0x29, 0x09, 0xB4, 0x0F, 0x80, 0x26, 0x0F, 0x80, 0xB3, 0x0F, 0x80,
};
static const u8 soldierFadeFixed[] = {
    0x09, 0x0A, 0x24, 0xB4, 0x0F, 0x80, 0x26, 0x0F, 0x80, 0x29, 0x0A, 0x29, 0x09, 0xB3, 0x0F, 0x80,
};

/* The entrance table before the routines: 0xFF, then 7 bytes per entrance
 * (x, z, walkmesh, camera facing, party facing; 0xFF: from variables 8/6). */
static const u8 orphanageEntrance0[] = {0xFF, 0xFB, 0xFF, 0xFA, 0xFA, 0x00, 0xFF, 0xFF};
#define ORPHANAGE_FACING 7
#define ORPHANAGE_INTO_GARDEN 0

#define ELLY_THEME 0x1E
#define ELLY_PITCH 1
#define ELLY_TEMPO 0x1C0

static s32 find(const u8* code, s32 size, const u8* pattern, s32 length) {
    s32 i;
    s32 k;

    for (i = 0; i + length <= size; i++) {
        for (k = 0; k < length && code[i + k] == pattern[k]; k++) {
        }
        if (k == length) {
            return i;
        }
    }
    return -1;
}

void pcFieldScriptFixes(s32 field, u8* code, s32 size) {
    s32 at;
    s32 k;

    if (!pcGameplayOriginalBugFixes()) {
        return;
    }
    if (field == 87 && currentlyPlayingMusic >= 0 && currentlyPlayingMusic < 0xF0) {
        at = find(code, size, nisanInn, sizeof(nisanInn));
        if (at >= 0) {
            code[at + NISAN_INN_WAIT] = 0x75; /* OP_PLAY_MUSIC */
            code[at + NISAN_INN_WAIT + 1] = currentlyPlayingMusic;
            code[at + NISAN_INN_WAIT + 2] = 0x80; /* an immediate */
        }
    }
    if (field == 213) {
        at = find(code, size, wardRest, sizeof(wardRest));
        if (at >= 0) {
            for (k = 0; k < (s32)sizeof(wardRestFixed); k++) {
                code[at + k] = wardRestFixed[k];
            }
        }
    }
    if (field == 50) {
        at = find(code, size, soldierFade, sizeof(soldierFade));
        if (at >= 0) {
            for (k = 0; k < (s32)sizeof(soldierFadeFixed); k++) {
                code[at + k] = soldierFadeFixed[k];
            }
        }
    }
    if (field == 326 && size > (s32)sizeof(orphanageEntrance0)) {
        for (k = 0; k < (s32)sizeof(orphanageEntrance0) && code[k] == orphanageEntrance0[k]; k++) {
        }
        if (k == (s32)sizeof(orphanageEntrance0)) {
            code[ORPHANAGE_FACING] = ORPHANAGE_INTO_GARDEN;
        }
    }
    if (field == 282 && currentlyPlayingMusic == EMERALDA_THEME && getVariable(0) == EMERALDA_PROGRESS &&
        find(code, size, emeraldaCue, sizeof(emeraldaCue)) >= 0) {
        currentlyPlayingMusic = 0xFF;
    }
}

static s32 isNisan(s32 field) {
    return (field >= 85 && field <= 100) || field == 474 || field == 493;
}

void pcFieldMusicStarted(s32 music, void* instance) {
    if (!pcGameplayOriginalBugFixes() || instance == NULL) {
        return;
    }
    if (music == ELLY_THEME && isNisan(fieldMapNumber & 0xFFF) && getVariable(0x102) >= 0x42 &&
        getVariable(0) > 207) {
        setMusicInstancePitch((SoundInstance*)instance, ELLY_PITCH, 0);
        setMusicInstanceTempo((SoundInstance*)instance, ELLY_TEMPO, 0);
    }
}
