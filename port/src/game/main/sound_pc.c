/*
 * The sound driver's disc formats with 4-byte pointer slots, on the PC
 * (include/main/sound.h).
 *
 * WDS: the driver keeps a copy of each loaded bank's header in its arena and
 * links the copies through the slot at 0x2C. The PC copy is the native Wds
 * (an 8-byte link, so the instruments start at 0x38 instead of 0x30).
 *
 * SEDS: the driver links loaded sound-effect sequences through the slot at
 * 0x1C of the data itself, where it was loaded. The PC leaves the data as
 * loaded and keeps the links in a table keyed by the sequence.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "main/sound.h"
#include "main/filesystem.h"
#include "main/memory.h"

#define WDS_DISC_HEADER 0x30 /* the instruments' offset in the disc header */
#define WDS_DISC_LINK 0x2C   /* the link slot: the fields before it are the same on the PC */

void copySoundMemory(void* d, void* s, s32 size);

s32 pcWdsSize(Wds* disc) {
    return (s32)(sptr)&((Wds*)0)->m30_instruments + disc->m10_headerSize1 - WDS_DISC_HEADER;
}

void pcWdsConvert(Wds* copy, Wds* disc) {
    copySoundMemory(copy, disc, WDS_DISC_LINK);
    copy->m2C_next = NULL;
    copySoundMemory(copy->m30_instruments, (u8*)disc + WDS_DISC_HEADER, disc->m10_headerSize1 - WDS_DISC_HEADER);
}

#define MAX_SEQ_LINKS 32

typedef struct {
    SeqFile* seq;
    SeqFile* next;
} SeqLink;

static SeqLink seqLinks[MAX_SEQ_LINKS];

SeqFile* pcSeqNext(SeqFile* seq) {
    s32 i;

    for (i = 0; i < MAX_SEQ_LINKS; i++) {
        if (seqLinks[i].seq == seq) {
            return seqLinks[i].next;
        }
    }
    return NULL;
}

/* Keep only the links of the sequences in the loaded list (clearLoadedSequences
 * drops the list without unlinking). */
static void dropStaleLinks(void) {
    SeqLink kept[MAX_SEQ_LINKS];
    SeqFile* s;
    s32 n = 0;
    s32 i;

    for (s = pLoadedSequencesLinkedList; s != NULL && n < MAX_SEQ_LINKS; s = pcSeqNext(s)) {
        kept[n].seq = s;
        kept[n].next = pcSeqNext(s);
        if (kept[n].next != NULL) {
            n++;
        }
    }
    for (i = 0; i < MAX_SEQ_LINKS; i++) {
        seqLinks[i].seq = i < n ? kept[i].seq : NULL;
        seqLinks[i].next = i < n ? kept[i].next : NULL;
    }
}

void pcSeqSetNext(SeqFile* seq, SeqFile* next) {
    SeqLink* freeSlot = NULL;
    s32 i;

    for (i = 0; i < MAX_SEQ_LINKS; i++) {
        if (seqLinks[i].seq == seq) {
            /* No link is the same as "next = NULL". */
            seqLinks[i].seq = next != NULL ? seq : NULL;
            seqLinks[i].next = next;
            return;
        }
        if (seqLinks[i].seq == NULL && freeSlot == NULL) {
            freeSlot = &seqLinks[i];
        }
    }
    if (next == NULL) {
        return;
    }
    if (freeSlot == NULL) {
        dropStaleLinks();
        for (i = 0; i < MAX_SEQ_LINKS && freeSlot == NULL; i++) {
            if (seqLinks[i].seq == NULL) {
                freeSlot = &seqLinks[i];
            }
        }
        if (freeSlot == NULL) {
            return; /* more loaded sequences than the PS1's sound arena could track */
        }
    }
    freeSlot->seq = seq;
    freeSlot->next = next;
}

/* The driver's volumes (include/main/sound.h). On the PS1 the struct sits in
 * main's BSS among labels the code also uses on their own (sound.c names
 * those as fields on the PC); defined here so it is one object of its full
 * size, not an alias inside a smaller neighbour. */
/* A strong definition, not a common one, and in .bss (game C keeps zero
 * initialisers in .data), so the soft reset clears it. */
__attribute__((nocommon)) SpuVolumes g_spuVolumes;

/* Start a score from a fresh kernel, when an extra mode skips field loading. */
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

int pcStartIsolatedMusic(s32 id, s32 wave, s32 flags) {
    s32 dir, sub;
    Wds* wds;
    int started = 0;

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
        started = 1;
    }
    setCurrentDirectory(dir, sub);
    return started;
}
