/* Known bootField loader writes to the later warm-up wall-slide scratch.
 *
 * initFieldData -> doPCCollisionCheckAfterLoading -> EntityMoveCheck1 saves
 * initFieldData's &D_800AFB10, then the party loop's actor index, into word 1.
 * Saved $s3 is first the object count, then that party actor's address.
 * Saved $s2 initially points at OPX_80Params.m0_OPX_80Params[2]; the party
 * loop replaces it with a SpriteActor pointer. Only a proven live retail
 * mapping replaces the pre-loader fallback for that case.
 * playMusic -> loadMusicPhase0 -> readFile -> startFileRead -> CdControlF
 * -> CD_cw saves the retry count and cdReadLocation into words 2 and 3.
 * Redux write watches discover the writers; Beetle US/JP argument probes
 * confirm their values (work/bd-loader). No field-id guesses or
 * host pointer bits. Proven initial actor-array addresses come from the
 * separate retail allocation metadata, including sprite and bone classes.
 *
 * This is part of the field module: its context is reset with the overlay
 * and saved with the other game state, including a read suspended in MUSIC.
 */
#include "common.h"
#include "pc_slide.h"
#include "pc_retail_heap.h"

extern u32 pcSlideEdgeStack[4];
extern sptr cdromFnd;
static s32 pcSlideEdgeLoaderContext;
static s32 pcSlideEdgeAnimationContext;
static u32 pcSlideEdgePartyFallback;
static s32 pcSlideEdgePartyFirst;

void pcSlideEdgeLoader(int phase) {
    pcSlideEdgeLoaderContext = phase;
    pcSlideEdgePartyFirst = 0;
}

void pcSlideEdgePartyInit(int actor, unsigned int savedActorOrCount) {
    if (pcSlideEdgeLoaderContext == PC_SLIDE_LOADER_INIT) {
        if (actor == PC_SLIDE_PARTY_FIRST) {
            pcSlideEdgePartyFallback = pcSlideEdgeStack[2];
            pcSlideEdgePartyFirst = 1;
#if VERSION_JP
            pcSlideEdgeStack[1] = 0x800AEFE4; /* &D_800AFB10 */
            pcSlideEdgeStack[2] = 0x800AF558; /* &OPX_80Params.m0_OPX_80Params[2] */
#else
            pcSlideEdgeStack[1] = 0x800AFB10;
            pcSlideEdgeStack[2] = 0x800B0084;
#endif
            pcSlideEdgeStack[3] = savedActorOrCount;
        } else {
            /* This caller's saved $s2 is a sprite pointer. Discard the
             * first-call BSS prediction; the caller supplies a live retail
             * mapping when available, otherwise retain the old fallback. */
            if (pcSlideEdgePartyFirst) {
                pcSlideEdgeStack[2] = pcSlideEdgePartyFallback;
                pcSlideEdgePartyFirst = 0;
            }
            pcSlideEdgeStack[1] = actor;
            if (savedActorOrCount) pcSlideEdgeStack[3] = savedActorOrCount;
        }
    }
}

void pcSlideEdgeMusicRead(int result) {
    if (pcSlideEdgeLoaderContext == PC_SLIDE_LOADER_MUSIC && result == 0 && cdromFnd == 0) {
        pcSlideEdgeStack[2] = 3;
#if VERSION_JP
        pcSlideEdgeStack[3] = 0x800595AC; /* &cdReadLocation */
#else
        pcSlideEdgeStack[3] = 0x80059F10;
#endif
    }
}

void pcSlideEdgePartySprite(const void* sprite) {
    if (pcSlideEdgeLoaderContext == PC_SLIDE_LOADER_INIT) {
        u32 address = pcHeapAddress(sprite);
        if (address) pcSlideEdgeStack[2] = address;
    }
}

void pcSlideEdgeAnimationScope(int active) {
    pcSlideEdgeAnimationContext = active;
}

void pcSlideEdgeAnimation(int animation, const void* array, const void* actor, int count, int stride) {
    if (pcSlideEdgeAnimationContext) {
        u32 address = pcHeapElement(array, actor, count, stride, 0x5C);
        /* The scalar saved $s0 and the caller's saved actor-array $s1.
         * The callee already normalized 0xFF to 0. Only a proven live
         * allocation supplies the address; unsupported histories keep the
         * previous residue, without using host pointer bits. */
        pcSlideEdgeStack[2] = animation;
        if (address) pcSlideEdgeStack[3] = address;
    }
}
