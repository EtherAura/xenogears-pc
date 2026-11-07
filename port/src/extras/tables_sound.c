/*
 * The recovered sound tables (tables.h): the field's music selector, sound
 * effect names, and the reverb effects play with.
 *
 * Music selector (playMusic, updateMusicState2 in the field overlay): field
 * music id n plays the score in file 0x14 + 2n of directory 0x1C, with the
 * sample bank the selector's pair (bank, flags) names: bank b is file
 * 0x13 + 2b of that directory, 0xFF keeps the bank already loaded; flags 1
 * makes room for it by unloading the second resident bank (playMusicSub),
 * 0 reloads that bank if it is gone. The selector is in the field overlay
 * (01/0E, LZSS), read here from the disc at its link address (US
 * 0x800ADFCC, the overlay at 0x8006FAF0; JP 0x800AD4A4 and 0x8006F180);
 * its length is the number of score slots the directory holds.
 *
 * Reverb (setupReverb): one setting for the whole SPU. initSoundSystem sets
 * mode 4 with depth 0; a score sets its header's (+0x1A mode, +0x1B depth,
 * +0x1C delay, +0x1D feedback) when it starts, and its op 0xB8 changes the
 * depth, delay and feedback; sound effects only turn it on per voice (their
 * ops 0xBA/0xBB). So an effect has the reverb of the score playing.
 *
 * Sound effect names: the game has no name strings for them; these are the
 * places its code plays a fixed effect, named after what that code does.
 */

#include "tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "tables_int.h"

/* main's sound driver data (src/main/sound_data.c) */
extern unsigned char opcodeSizes[0x80];         /* an op's length with its operands */
extern unsigned char noteDeltaTimeTable[12 * 19]; /* a note's length; 0: a byte follows */

#define MUSIC_DIR 0x1C
#define MUSIC_FIRST_SCORE 0x14
#define MUSIC_MAX 128

static struct {
    int loaded;
    int count;
    XgMusicInfo music[MUSIC_MAX];
} M;

/* A score's tracks walked as the sequencer reads them (executeSequenceEvents:
 * a byte below 0x80 is a note's velocity, then its length selector, then an
 * explicit length when the selector's is 0; an op takes opcodeSizes bytes);
 * 1 when one of them has op 0x91, the loop point. */
static int scoreLoops(const uint8_t* s, int size, int tracks) {
    for (int t = 0; t < tracks && 0x22 + 2 * t + 2 <= size; t++) {
        int at = tablesRd16(s + 0x22 + 2 * t);
        if (at == 0) {
            continue;
        }
        while (at < size) {
            int op = s[at];
            if (op < 0x80) {
                if (at + 1 >= size || s[at + 1] >= sizeof(noteDeltaTimeTable)) {
                    break;
                }
                at += noteDeltaTimeTable[s[at + 1]] == 0 ? 3 : 2;
                continue;
            }
            if (op == 0x91) {
                return 1;
            }
            if (op == 0x90 || opcodeSizes[op - 0x80] == 0) {
                break; /* the end, or an op the driver does not have */
            }
            at += opcodeSizes[op - 0x80];
        }
    }
    return 0;
}

static void readScore(XgDisc* d, XgMusicInfo* m) {
    int size;
    uint8_t* s = tablesRead(d, MUSIC_DIR, m->smdsFile, &size);
    if (s != NULL && size >= 0x24 && memcmp(s, "smds", 4) == 0) {
        xgSmdsName(s, size, m->name, sizeof(m->name));
        m->tracks = s[0x14];
        m->reverb.mode = s[0x1A];
        m->reverb.depth = s[0x1B];
        m->reverb.delay = s[0x1C];
        m->reverb.feedback = s[0x1D];
        m->loops = scoreLoops(s, size, m->tracks);
    }
    free(s);
}

static void loadMusic(void) {
    if (M.loaded) {
        return;
    }
    M.loaded = 1;
    XgDisc* d = tablesDisc(1);
    int region = tablesRegionOf(d);
    if (region == 0) {
        return;
    }
    uint32_t vram = region == 'J' ? 0x8006F180 : 0x8006FAF0;
    uint32_t table = region == 'J' ? 0x800AD4A4 : 0x800ADFCC;
    int size;
    uint8_t* overlay = tablesReadLzss(d, 1, 0x0E, &size);
    if (overlay == NULL || table - vram + 2 * MUSIC_MAX > (uint32_t)size) {
        free(overlay);
        return;
    }
    const uint8_t* sel = overlay + (table - vram);
    /* The score slots: files 0x14 + 2n that hold one. */
    int slots = 0;
    while (slots < MUSIC_MAX) {
        int entry = xgDiscFileEntry(d, MUSIC_DIR, MUSIC_FIRST_SCORE + 2 * slots);
        uint8_t* head = NULL;
        if (entry >= 0 && xgDiscEntry(d, entry).size > 0) {
            head = xgDiscReadSectors(d, xgDiscEntry(d, entry).lba, 1);
        }
        int score = head != NULL && memcmp(head, "smds", 4) == 0;
        free(head);
        if (!score) {
            break;
        }
        slots++;
    }
    for (int n = 0; n < slots; n++) {
        XgMusicInfo* m = &M.music[n];
        memset(m, 0, sizeof(*m));
        m->id = n;
        m->smdsFile = MUSIC_FIRST_SCORE + 2 * n;
        m->bank = sel[2 * n] == 0xFF ? -1 : sel[2 * n];
        m->wdsFile = m->bank < 0 ? 0 : 0x13 + 2 * m->bank;
        m->flags = sel[2 * n + 1];
        readScore(d, m);
    }
    M.count = slots;
    free(overlay);
}

int xgMusicCount(void) {
    loadMusic();
    return M.count;
}

int xgMusicGet(int id, XgMusicInfo* out) {
    loadMusic();
    if (id < 0 || id >= M.count) {
        return 0;
    }
    *out = M.music[id];
    return 1;
}

int xgSfxReverb(int musicId, XgReverb* out) {
    if (musicId == -1) {
        /* initSoundSystem: setupReverb(4, 0, 0, 0) */
        out->mode = 4;
        out->depth = 0;
        out->delay = 0;
        out->feedback = 0;
        return 1;
    }
    loadMusic();
    if (musicId < 0 || musicId >= M.count) {
        return 0;
    }
    *out = M.music[musicId].reverb;
    return 1;
}

/* ---- sound effect names --------------------------------------------------------------------- */

/* Where the code plays a fixed effect: the bank (by the file the game loads
 * it from; its SEDS id is read from that file), the effect, and what the
 * code does there. */
typedef struct {
    int dir, file; /* the SEDS file */
    int effect;
    const char* name;
} SfxFact;

static const SfxFact sfxFacts[] = {
    /* The field's bank 04/A8, which the menu plays its sounds from too
     * (gMenuContext->m2E4_musicSequence is the field's fieldSeq). */
    {4, 0xA8, 0x01, "Menu: cursor"},                /* playMenuSoundEffect(1), 24 places */
    {4, 0xA8, 0x02, "Menu: choose"},                /* 15 places */
    {4, 0xA8, 0x03, "Menu: back"},
    {4, 0xA8, 0x04, "Menu: refused"},
    {4, 0xA8, 0x08, "Field: door opens"},           /* OP_OPEN_DOOR */
    {4, 0xA8, 0x34, "Menu: memory card done"},      /* after a load or save succeeds */
    {4, 0xA8, 0x37, "Menu: item used"},             /* useItemOnParty, useEtherOnParty */
    {4, 0xA8, 0x5B, "Menu: turns in"},              /* updateMenuTransforms state 3 */
    {4, 0xA8, 0x5C, "Menu: turns away"},            /* state 4 */
    {4, 0xA8, 0x5D, "Menu: info cards in"},
    {4, 0xA8, 0x5E, "Menu: opens"},                 /* setupMenu0 */
    {4, 0xA8, 0xD1, "Shop: gold changes hands"},    /* completeShopPurchase, the Gear shop's sale */
    /* The battle's bank 2C/03 (battle loader: gBattleCurrentSoundEffectSeq). */
    {0x2C, 3, 0x4C, "Battle: direction pressed"},   /* battleHandleInput */
    {0x2C, 3, 0x4D, "Battle: Circle, Square or Triangle"},
    {0x2C, 3, 0x4E, "Battle: Cross"},
    {0x2C, 3, 0x4F, "Battle: not enough points"},
};

/* The battle start bank 0C/02 (bootGame's battleStartSeq): each transition
 * plays up to three of its effects (battleTransitionSoundEffects). */
#define BATTLE_START_DIR 0x0C
#define BATTLE_START_FILE 2

static struct {
    int loaded;
    int ids[sizeof(sfxFacts) / sizeof(sfxFacts[0])];
    int battleStartId;
} X;

static int sedsIdOf(XgDisc* d, int dir, int file) {
    int entry = d != NULL ? xgDiscFileEntry(d, dir, file) : -1;
    if (entry < 0 || xgDiscEntry(d, entry).size <= 0) {
        return -1;
    }
    uint8_t* head = xgDiscReadSectors(d, xgDiscEntry(d, entry).lba, 1);
    int id = head != NULL && memcmp(head, "seds", 4) == 0 ? tablesRd16(head + 0x14) : -1;
    free(head);
    return id;
}

static void loadSfx(void) {
    if (X.loaded) {
        return;
    }
    X.loaded = 1;
    XgDisc* d = tablesDisc(1);
    for (size_t i = 0; i < sizeof(sfxFacts) / sizeof(sfxFacts[0]); i++) {
        X.ids[i] = sedsIdOf(d, sfxFacts[i].dir, sfxFacts[i].file);
    }
    X.battleStartId = sedsIdOf(d, BATTLE_START_DIR, BATTLE_START_FILE);
}

const char* xgSfxName(int sedsId, int effect) {
    loadSfx();
    for (size_t i = 0; i < sizeof(sfxFacts) / sizeof(sfxFacts[0]); i++) {
        if (X.ids[i] == sedsId && sfxFacts[i].effect == effect) {
            return sfxFacts[i].name;
        }
    }
    if (sedsId >= 0 && sedsId == X.battleStartId && effect >= 1 && effect <= 10) {
        static char names[11][32];
        snprintf(names[effect], sizeof(names[effect]), "Battle start: sound %d", effect);
        return names[effect];
    }
    return "";
}

/* For the Tables extra: the named effects, in order. */
int xgTablesSfxNamed(int i, int* sedsId, int* effect, const char** name) {
    loadSfx();
    int facts = (int)(sizeof(sfxFacts) / sizeof(sfxFacts[0]));
    if (i < 0 || i >= facts + 10) {
        return 0;
    }
    if (i < facts) {
        *sedsId = X.ids[i];
        *effect = sfxFacts[i].effect;
    } else {
        *sedsId = X.battleStartId;
        *effect = i - facts + 1;
    }
    *name = xgSfxName(*sedsId, *effect);
    return 1;
}

void tablesSoundReset(void) {
    memset(&M, 0, sizeof(M));
    memset(&X, 0, sizeof(X));
}
