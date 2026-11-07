/* Caller isolation and partial scratch ownership, including a 64-bit host
 * file-source pointer whose low word is zero. Native call/value evidence is
 * collected separately by tools/wallslide_probe.py against Beetle. */
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "pc_slide.h"
#include "pc_retail_heap.h"
#include "../../../test/check.h"

u32 pcSlideEdgeStack[4];
sptr cdromFnd;
static int checks;

int main(void) {
    const u32 authored[4] = { 0x0076FF6F, 0x0000FDD1, 0x0076FF2C, 0x8005FDC0 };
    u32 loaded[4];

    memcpy(pcSlideEdgeStack, authored, sizeof(authored));
    pcSlideEdgePartyInit(PC_SLIDE_PARTY_FIRST, 0);
    pcSlideEdgePartyInit(23, 0);
    pcSlideEdgeMusicRead(0);
    CHECK(memcmp(pcSlideEdgeStack, authored, sizeof(authored)) == 0);
    pcSlideEdgeAnimation(0, 0, 0, 0, 0);
    CHECK(memcmp(pcSlideEdgeStack, authored, sizeof(authored)) == 0);
    pcSlideEdgeAnimationScope(1);
    pcSlideEdgeAnimation(0, 0, 0, 0, 0);
    CHECK(pcSlideEdgeStack[0] == authored[0]);
    CHECK(pcSlideEdgeStack[1] == authored[1]);
    CHECK(pcSlideEdgeStack[2] == 0);
    CHECK(pcSlideEdgeStack[3] == authored[3]); /* no host heap pointer guess */
    {
        unsigned char array[224];
        u32 before[4];
        memcpy(before, pcSlideEdgeStack, sizeof(before));
        u32 address;
        pcHeapStartRetail(0x8006FAF0);
        pcHeapAllocated(array, sizeof(array), 184, 0);
        address = pcHeapAddress(array);
        pcSlideEdgeAnimation(7, array, array + 112, 2, 112);
        CHECK(pcSlideEdgeStack[3] == address + 92);
        pcHeapFreed(array);
        pcSlideEdgeStack[3] = 123;
        pcSlideEdgeAnimation(7, array, array + 112, 2, 112);
        CHECK(pcSlideEdgeStack[3] == 123); /* no stale mapping */
        memcpy(pcSlideEdgeStack, before, sizeof(before));
    }
    pcSlideEdgeAnimationScope(0);
    pcSlideEdgeAnimation(7, 0, 0, 0, 0);
    CHECK(pcSlideEdgeStack[2] == 0);
    memcpy(pcSlideEdgeStack, authored, sizeof(authored));

    pcSlideEdgeLoader(PC_SLIDE_LOADER_INIT);
    pcSlideEdgePartyInit(PC_SLIDE_PARTY_FIRST, 71);
    CHECK(pcSlideEdgeStack[3] == 71);
#if VERSION_JP
    CHECK(pcSlideEdgeStack[1] == 0x800AEFE4);
    CHECK(pcSlideEdgeStack[2] == 0x800AF558);
#else
    CHECK(pcSlideEdgeStack[1] == 0x800AFB10);
    CHECK(pcSlideEdgeStack[2] == 0x800B0084);
#endif
    pcSlideEdgePartyInit(23, 0);
    CHECK(pcSlideEdgeStack[1] == 23);
    CHECK(pcSlideEdgeStack[0] == authored[0]);
    CHECK(pcSlideEdgeStack[2] == authored[2]);
    CHECK(pcSlideEdgeStack[3] == 71); /* unknown actor address retains the count */
    pcSlideEdgeStack[2] = 17;
    pcSlideEdgePartyInit(23, 0x801202D4);
    CHECK(pcSlideEdgeStack[2] == 17); /* fallback restoration happens only once */
    CHECK(pcSlideEdgeStack[3] == 0x801202D4);
    {
        unsigned char sprite[472];
        u32 address;
        pcHeapStartRetail(0x8006FAF0);
        pcHeapAllocated(sprite, sizeof(sprite), 0x164, 0);
        address = pcHeapAddress(sprite);
        pcSlideEdgePartySprite(sprite);
        CHECK(pcSlideEdgeStack[2] == address);
        pcHeapFreed(sprite);
        pcSlideEdgeStack[2] = 17;
        pcSlideEdgePartySprite(sprite);
        CHECK(pcSlideEdgeStack[2] == 17);
        pcHeapAllocated(sprite, sizeof(sprite), 0x164, 0);
        pcSlideEdgeLoader(PC_SLIDE_LOADER_OFF);
        pcSlideEdgePartySprite(sprite);
        CHECK(pcSlideEdgeStack[2] == 17);
        pcSlideEdgeLoader(PC_SLIDE_LOADER_INIT);
        pcHeapFreed(sprite);
    }
    memcpy(loaded, pcSlideEdgeStack, sizeof(loaded));
    pcSlideEdgeMusicRead(0); /* same callee, a different stack context */
    CHECK(memcmp(pcSlideEdgeStack, loaded, sizeof(loaded)) == 0);

    pcSlideEdgeLoader(PC_SLIDE_LOADER_MUSIC);
    pcSlideEdgePartyInit(5, 0);
    pcSlideEdgeMusicRead(-3);
    pcSlideEdgeMusicRead(1);
    CHECK(memcmp(pcSlideEdgeStack, loaded, sizeof(loaded)) == 0);
    cdromFnd = (sptr)((uptr)1 << 32); /* the PC file path doesn't call CD_cw */
    pcSlideEdgeMusicRead(0);
    CHECK(memcmp(pcSlideEdgeStack, loaded, sizeof(loaded)) == 0);
    cdromFnd = 0;
    pcSlideEdgeMusicRead(0);
    CHECK(pcSlideEdgeStack[0] == loaded[0]);
    CHECK(pcSlideEdgeStack[1] == loaded[1]);
    CHECK(pcSlideEdgeStack[2] == 3);
#if VERSION_JP
    CHECK(pcSlideEdgeStack[3] == 0x800595AC);
#else
    CHECK(pcSlideEdgeStack[3] == 0x80059F10);
#endif
    pcSlideEdgeLoader(PC_SLIDE_LOADER_OFF);
    memcpy(pcSlideEdgeStack, authored, sizeof(authored));
    pcSlideEdgePartyInit(5, 0);
    pcSlideEdgeMusicRead(0);
    CHECK(memcmp(pcSlideEdgeStack, authored, sizeof(authored)) == 0);
    pcSlideEdgeLoader(PC_SLIDE_LOADER_INIT);
    pcSlideEdgePartyInit(PC_SLIDE_PARTY_FIRST, 104);
    pcSlideEdgeLoader(PC_SLIDE_LOADER_OFF);
    pcSlideEdgeLoader(PC_SLIDE_LOADER_INIT);
    pcSlideEdgeStack[2] = 123;
    pcSlideEdgePartyInit(5, 0);
    CHECK(pcSlideEdgeStack[2] == 123); /* no pending first call across scopes */
    pcSlideEdgePartyInit(PC_SLIDE_PARTY_FIRST, 104);
    pcSlideEdgePartyInit(5, 0);
    CHECK(pcSlideEdgeStack[2] == 123); /* each loader takes its own fallback */
    puts("slidestacktest: passed");
    printf("%d checks\n", checks);
    return 0;
}
