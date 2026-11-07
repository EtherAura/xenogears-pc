#ifndef PORT_EXTRAS_TABLES_H
#define PORT_EXTRAS_TABLES_H

/*
 * Tables no public source has, recovered at run time from the player's disc
 * and the game's own code (tables*.c; xg-htn.9). The facts behind each are in
 * docs/ASSETS.md; nothing here is a dump: every table is read from the disc
 * or computed from what the game code does.
 *
 *   names      the world map's place names, field names carried from them
 *              along the fields' exits, the save screen's story chapters
 *   music      the field's music selector (score, sample bank, flags), each
 *              score's own name and loop point
 *   sounds     sound effect names where the game has them, the reverb the
 *              game sets for effects
 *   battle     the abilities' effect files (their members and flags)
 *   rates      sprite and Gear animation playback rates
 *
 * Use: from the extras' C or C++ (extern "C"), on the game thread, outside a
 * sandbox (the functions enter their own when they run game code). The first
 * call reads what it needs from the disc and keeps it; strings are UTF-8,
 * owned by the tables, and stay valid until xgTablesSetDisc or the end of the
 * program. Every function answers something harmless (0, "", -1) when the
 * disc has no such table.
 *
 * The disc: by default the drive's discs (cd/swap.c: the one in the drive and
 * the other one), disc 1 first, since disc 2 lacks most fields.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Point the tables at another disc image (e.g. a JP disc in a US build);
 * NULL goes back to the drive's discs. Drops what was read. 1 on success. */
int xgTablesSetDisc(const char* path);
/* "US", "JP" or "" (no disc) of the disc the tables read. */
const char* xgTablesRegion(void);
/* That disc (disc.h; the tables own it), NULL if none. */
typedef struct XgDisc XgDisc;
XgDisc* xgTablesDisc(void);

/* ---- names --------------------------------------------------------------------------------- */

/* Fields (maps): n is the field number (the low 12 bits of the game's map
 * number; archive 04/B8+2n). The name is the game's own: the world map's
 * name of the place the field belongs to, found by following the fields'
 * exits (their scripts' change-field ops) at most three exits from the
 * field that place's world map exit enters. "" when the game names none:
 * the debug rooms, fields only scenes reach, fields as near to places of
 * different names (the Yggdrasil's rooms), and every field of a JP disc
 * (its world map names are in the JP font's own codes). */
int xgFieldCount(void);
const char* xgFieldName(int field);
/* The world map place xgFieldName took the name from, -1 if none. */
int xgFieldPlace(int field);

/* The save screen's location line: the story chapter name the menu shows for
 * story progress p (field variable 0), e.g. "Lahan Village", "Path to Aveh"
 * (file 11/01, Shift-JIS: a JP disc gives Japanese, which the extras' game
 * font cannot show; the strings and exports are right). */
int xgStoryCount(void);
const char* xgStoryName(int progress);
/* The first story progress value with this chapter name's line, and the
 * number of chapters (distinct runs of lines). */
int xgStoryChapterCount(void);
const char* xgStoryChapterName(int chapter);
int xgStoryChapterStart(int chapter);

/* The world map's places: names as the world map labels them, and the field
 * each one enters (-1 if none known). */
int xgWorldPlaceCount(void);
const char* xgWorldPlaceName(int place);
int xgWorldPlaceField(int place);

/* How a field got its name, for the Tables extra: the fields its scripts
 * change to (-1 ends the list), and its distance in exits from the field
 * the world map place enters (0 that field, -1 unnamed). */
const int* xgTablesFieldExits(int field);
int xgTablesFieldHops(int field);

/* ---- music --------------------------------------------------------------------------------- */

/* The SPU reverb the sound driver sets (setupReverb): mode (4 = studio
 * large), depth 0..0x7F, delay and feedback. */
typedef struct {
    int mode;
    int depth;
    int delay;
    int feedback;
} XgReverb;

typedef struct {
    int id;          /* field music id (playMusic, field script ops) */
    int smdsFile;    /* file of directory 0x1C holding the score: 0x14 + 2 * id */
    int bank;        /* the selector's sample bank number, -1 for none (0xFF: keep what is loaded) */
    int wdsFile;     /* file of directory 0x1C holding that bank: 0x13 + 2 * bank; 0 for none */
    int flags;       /* the selector's second byte: 1 = the bank needs the second resident
                        bank's room (the game unloads it), 0 = the resident banks stay */
    char name[24];   /* the score's own name (SMDS +0x1E), "" if not a score */
    int tracks;      /* tracks in the score (track 0 the conductor), 0 if not a score */
    int loops;       /* 1 when a track has a loop point (op 0x91), walked with the
                        driver's own note-length and op-length tables */
    XgReverb reverb; /* what the score sets when it starts (its header +0x1A) */
} XgMusicInfo;

/* The selector's entries (the field overlay's table); id 0..count-1. */
int xgMusicCount(void);
/* 1 and *out filled; 0 when id is outside the table. */
int xgMusicGet(int id, XgMusicInfo* out);

/* ---- sound effects ------------------------------------------------------------------------- */

/* A sound effect's name where the game has one (sound id = SEDS id << 16 |
 * effect), else "". */
const char* xgSfxName(int sedsId, int effect);

/* The named effects one by one (i from 0), for the Tables extra: 1 and the
 * effect's SEDS id, number and name; 0 past the last. */
int xgTablesSfxNamed(int i, int* sedsId, int* effect, const char** name);

/* The reverb sound effects play with while field music `musicId` plays:
 * the driver has one reverb for everything, which each score sets when it
 * starts (and its op 0xB8 changes); -1: before any score (the driver's
 * initSoundSystem setting). 1 and *out filled; 0 for an unknown id. */
int xgSfxReverb(int musicId, XgReverb* out);

/* ---- battle effects ------------------------------------------------------------------------- */

/* An ability's effect as abilityPlayAnimationSub1(n) loads it: directory
 * 0x0E, file 0x22 + 2n, an offset bundle whose first three members are a
 * sprite animation bundle's (animations, frames, palettes) and whose others
 * are SEDS banks, WDS banks or more sprite bundles (loadEffectFragmentsAndAudio),
 * and file 0x23 + 2n, its texture uploads. */
typedef struct {
    int id;          /* n */
    int file;        /* 0x22 + 2n; size 0 and no members: a placeholder ("CDMAKE Dummy") */
    int size;
    int textureFile; /* 0x23 + 2n, 0 when it is empty (16 bytes or less: not loaded) */
    int textureSize;
    int members;
    int mode;        /* bits 12-13 of the animation table's first word: 0 the scene stays; 1 the
                        camera drops every entity and the other party actors go; 2-3 the same,
                        but the user stays */
    int animations;  /* the animation table's count (its first word's low 6 bits) */
    int seds;        /* SEDS banks among the other members, and the first one's id */
    int sedsId;
    int wds;         /* WDS banks among them, and the first one's bank */
    int wdsBank;
    int sprites;     /* more sprite bundles among them (uploaded at 0x380,0x100, CLUT row 0x1F4) */
} XgBattleEffect;

int xgBattleEffectCount(void);
/* 1 and *out filled; 0 when n is outside the directory. */
int xgBattleEffectGet(int n, XgBattleEffect* out);

/* ---- playback rates ------------------------------------------------------------------------ */

/* The PS1's vertical blank rate the game counts in (NTSC, non-interlaced). */
double xgVsyncRate(void);

typedef struct {
    double frameRate;      /* the mode's frames per second */
    int vsyncsPerFrame;    /* vblanks a frame lasts at least */
    int catchUp;           /* 1: frames run the animation once per vblank that passed
                              (battle), so it keeps time when frames drop */
    double spriteTickRate; /* sprite bytecode ticks per second (its waits count ticks) */
    double gearStepRate;   /* Gear animation VM steps per second */
} XgRates;

/* The game's timing of animation in a mode (1 field, 2 battle, 3 world map)
 * at slowdown `slow` (the field's op D6 value: 0 = 30 fps, 1 = 20, 2 = 15;
 * ignored elsewhere). 1 and *out filled; 0 for an unknown mode. A sprite
 * actor's time scale (spriteActorSetTimeScale, 0x100 = 1) scales its waits
 * on top (wait x scale / 0x100, at least 1). */
int xgAnimRates(int mode, int slow, XgRates* out);
/* Shorthands at the mode's normal speed. */
double xgSpriteTickRate(int mode);
double xgGearStepRate(int mode);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_TABLES_H */
