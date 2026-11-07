/*
 * The model viewer's way into the Gear overlay (modelsview.h; PC only, called
 * inside an extras sandbox, never by the game).
 */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/sound.h"
#include "field_mecha/mecha.h"
#include "modelsview.h"

extern SoundInstance* pSoundEffectsInstances;
extern s32 numMaxSoundEffectInstances;
extern sMechaInitVar2 mechaInitVar2;

s32 modelsViewSoundStubs(void) {
    s32 size = (s32)(sizeof(SoundInstance) + 2 * sizeof(SoundTrack));
    SoundInstance* inst = allocateHeap(size, 0);
    SeqFile* seq = allocateHeap((s32)sizeof(SeqFile), 0);

    if (inst == NULL || seq == NULL) {
        return 0;
    }
    memset(inst, 0, size);
    memset(seq, 0, sizeof(SeqFile));
    pSoundEffectsInstances = inst;
    numMaxSoundEffectInstances = 2; /* the driver keeps them in pairs */
    gBattleCurrentSoundEffectSeq = seq;
    return 1;
}

s32 modelsViewTracksInUse(void) {
    s32 n = 0;
    s32 i;

    for (i = 0; mechaInitVar2.m0_tracks != NULL && i < mechaInitVar2.m6_count; i++) {
        n += mechaInitVar2.m0_tracks[i].m0_inUse != 0;
    }
    return n;
}
