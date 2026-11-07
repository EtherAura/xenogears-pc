/* Replay real retail observations and exercise fail-closed native mappings. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../retailheap.c"

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)
static void* host(u32 address) { return (void*)((uptr)address + ((uptr)1 << 32)); }

static void unit(void) {
    void* a = host(0x1234), *b = host(0x4321);
    u32 address;
    s32 size = 3;
    pcHeapStartRetail(0x8006FAF0u);
    address = retailHeap.blocks[0].data;
    CHECK(pcHeapTakeRequest() == PC_HEAP_UNKNOWN);
    CHECK(!retailHeap.synced); /* a non-returning unknown request already stops */
    pcHeapStartRetail(0x8006FAF0u);
    CHECK(PC_HEAP_BYTES(size++) == 3 && size == 4);
    CHECK(pcHeapTakeRequest() == 3);
    CHECK(pcHeapRequestBytes(7) == 7);
    CHECK(pcHeapTakeRequest() == 7);
    CHECK(pcHeapTakeRequest() == PC_HEAP_UNKNOWN);
    pcHeapStartRetail(0x8006FAF0u);
    pcHeapAllocated(a, 112 * 3, 92 * 3, 0);
    CHECK(pcHeapAddress(a) == address);
    CHECK(pcHeapAddress(a) != (u32)(uptr)a);
    CHECK(pcHeapElement(a, (u8*)a + 112, 3, 112, 92) == address + 92);
    CHECK(!pcHeapElement(a, (u8*)a + 1, 3, 112, 92));
    CHECK(!pcHeapElement(a, (u8*)a + 336, 3, 112, 92));
    CHECK(!pcHeapElement(a, a, 4, 112, 92));
    CHECK(!pcHeapElement(a, a, 3, 0, 92));
    pcHeapAllocated(b, 96, PC_HEAP_NATIVE_ONLY, 1);
    CHECK(retailHeap.synced && !pcHeapAddress(b));
    CHECK(!pcHeapElement(b, (u8*)b + 16, 6, 16, 8));
    pcHeapPin(b, 1);
    CHECK(!retailHeap.unknownPinCount);
    pcHeapPin(b, 0); pcHeapFreed(b);
    pcHeapAllocated(b, 96, PC_HEAP_UNKNOWN, 1);
    CHECK(!retailHeap.synced && retailHeap.unsupported == 1);
    CHECK(pcHeapAddress(a) == address); /* earlier live addresses are facts */
    pcHeapResetRetail(address + 4096);
    CHECK(!retailHeap.synced); /* a reset cannot repair an unknown history */
    pcHeapFreed(a);
    CHECK(!pcHeapAddress(a));
    pcHeapAllocated(a, 16, 16, 0);
    CHECK(!pcHeapAddress(a));
    pcHeapStartRetail(0x8006FAF0u);
    CHECK(retailHeap.synced);
    pcHeapAllocated(a, 4, 4, 1);
    CHECK(pcHeapSize(a) == 4);
    CHECK(pcHeapReservation(a, RETAIL_END - 512) == 492);
    CHECK(pcHeapReservation(a, RETAIL_END - 512 - 8) == 500); /* battle loader's original top - link request */
    pcHeapAllocated(b, 4, pcHeapReservation(a, RETAIL_END - 512), 1);
    CHECK(retailHeap.synced && pcHeapAddress(b) == RETAIL_END - 512);
    CHECK(pcHeapSize(b) == 492 && mappingAt(b)->nativeSize == 4);
    CHECK(pcHeapReservation(NULL, RETAIL_END - 512) == PC_HEAP_UNKNOWN);
    CHECK(pcHeapReservation(a, RETAIL_END) == PC_HEAP_UNKNOWN);
    CHECK(pcHeapReservation(a, RETAIL_END - 511) == PC_HEAP_UNKNOWN);
    pcHeapFreed(a); CHECK(pcHeapSize(a) == PC_HEAP_UNKNOWN);
    pcHeapStartRetail(0x8006FAF0u);
    pcHeapAllocated(a, 64, 64, 1);
    address = pcHeapAddress(a);
    CHECK(address == RETAIL_END - 72);
    pcHeapPin(a, 1);
    pcHeapResetRetail(retailHeap.blocks[0].data + 0x100);
    CHECK(pcHeapAddress(a) == address);
    pcHeapPin(a, 0); pcHeapFreed(a);
    CHECK(!pcHeapAddress(a));
    pcHeapMerge();
    CHECK(retailHeap.count == 2);
    pcHeapAllocated(a, 64, 64, 0);
    pcHeapShrink(a, 16);
    CHECK(retailHeap.synced && retailHeap.count == 4);
    CHECK(retailHeap.blocks[0].next == retailHeap.blocks[0].data + 24);
    pcHeapInvalidate();
    CHECK(!pcHeapAddress(a) && !retailHeap.synced);
    pcHeapStartRetail(0x8006FAF0u);
    pcHeapAllocated(a, 16, 16, 0);
    pcHeapAllocated(a, 32, 32, 0);
    CHECK(!retailHeap.synced && !pcHeapAddress(a));
    pcHeapStartRetail(0x8006FAF0u);
    pcHeapAllocated(a, 224, 184, 0);
    pcHeapShrink(a, 112);
    CHECK(!retailHeap.synced && !pcHeapAddress(a));
    /* Unknown transient modes may rejoin only after the native free-all
     * completes, with all surviving allocations proven pinned. */
    pcHeapStartRetail(0x8006FAF0u);
    pcHeapAllocated(a, 64, 64, 1); address = pcHeapAddress(a);
    pcHeapPin(a, 1);
    pcHeapAllocated(b, 128, PC_HEAP_UNKNOWN, 0);
    pcHeapResetRetail(0x80070000u);
    CHECK(!retailHeap.synced); /* no observed native free-all */
    pcHeapAllFreed(); pcHeapResetRetail(0x80070000u);
    CHECK(retailHeap.synced && pcHeapAddress(a) == address);
    CHECK(retailHeap.blocks[0].data == 0x80070008u);
    pcHeapAllocated(b, 128, PC_HEAP_UNKNOWN, 0); pcHeapPin(b, 1);
    pcHeapAllFreed(); pcHeapResetRetail(0x80070000u);
    CHECK(!retailHeap.synced); /* unclassified pinned survivor */
    pcHeapPin(b, 0); pcHeapResetRetail(0x80070000u);
    CHECK(!retailHeap.synced); /* unpin does not prove it was freed */
    pcHeapAllFreed(); pcHeapResetRetail(0x80070000u);
    CHECK(retailHeap.synced && pcHeapAddress(a) == address);
    pcHeapAllocated(b, 128, PC_HEAP_UNKNOWN, 0);
    pcHeapPin(a, 0); pcHeapFreed(a); pcHeapAllFreed();
    pcHeapResetRetail(0x80070000u);
    CHECK(retailHeap.synced && !pcHeapAddress(a));
    CHECK(retailHeap.count == 2); /* no formerly pinned ghost block */
    pcHeapUnsupported(); pcHeapAllFreed(); pcHeapResetRetail(0x80070000u);
    CHECK(!retailHeap.synced); /* hard disagreement never recovers */
    pcHeapStartRetail(0x8006FAF0u);
    pcHeapAllocated(a, 128, PC_HEAP_UNKNOWN, 0);
    pcHeapAllocated(b, 96, PC_HEAP_NATIVE_ONLY, 0); pcHeapPin(b, 1);
    pcHeapAllFreed(); pcHeapResetRetail(0x80070000u);
    CHECK(retailHeap.synced && !retailHeap.unknownPinCount);
    CHECK(mappingAt(b) && !pcHeapAddress(b));
    pcHeapShrink(b, 16);
    CHECK(retailHeap.synced);
    pcHeapPin(b, 0); pcHeapFreed(b);
    CHECK(retailHeap.synced && !mappingAt(b));
}

static void trace(const char* path) {
    FILE* f = fopen(path, "r");
    char line[512], name[80];
    unsigned frame, a, b, ra, expected;
    int events = 0, excluded = 0, returns = 0;
    unsigned char saved[sizeof(retailHeap)];
    CHECK(f != NULL);
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        s32 i;
        int n = sscanf(line, "%79s %u %x %x %x %x", name, &frame, &a, &b, &ra, &expected);
        if (n < 5 || name[0] == '#') continue;
        events++;
        /* Restore all metadata before each event, including host mappings. */
        memcpy(saved, &retailHeap, sizeof(retailHeap));
        memset(&retailHeap, 0, sizeof(retailHeap));
        memcpy(&retailHeap, saved, sizeof(retailHeap));
        if (!strcmp(name, "initHeap")) {
            pcHeapStartRetail(0x8006FAF0u);
            retailHeap.blocks[0].data = (a & ~3u) + 8;
            CHECK(b == RETAIL_END);
        } else if (!strcmp(name, "resetHeapBegin")) {
            /* Its nested free events precede rebasing at resetHeapEnd. */
        } else if (!strcmp(name, "resetHeapEnd")) pcHeapResetRetail(a);
        else if (!strcmp(name, "resetHeapTraceState")) pcHeapOwner(10);
        else if (!strcmp(name, "setHeapUser") || !strcmp(name, "resetMemoryAllocStats")) pcHeapOwner(a);
        else if (!strcmp(name, "mergeFreeHeapBlocks")) pcHeapMerge();
        else if (!strcmp(name, "allocateHeap")) {
            pcHeapAllocated(expected ? host(expected) : NULL, a, a, b);
            CHECK(retailHeap.synced);
            CHECK(pcHeapAddress(host(expected)) == expected);
            returns++;
        } else if (!strcmp(name, "flagAllocation") || !strcmp(name, "unflagAllocation") || !strcmp(name, "unpinAllocation")) {
            pcHeapPin(host(a), !strcmp(name, "flagAllocation"));
        } else if (!strcmp(name, "freeHeap")) {
            i = blockAt(a);
            if (i < 0) { excluded++; continue; } /* stale unlinked header */
            CHECK((retailHeap.blocks[i].pinned ? 0xFFFFFFFFu : 0) == expected);
            if (!retailHeap.blocks[i].pinned) {
                NativeMapping* m = mappingAt(host(a));
                if (m) pcHeapFreed(host(a));
                else { retailHeap.blocks[i].owner = 0; retailHeap.dirty = 1; }
            }
            returns++;
        } else if (!strcmp(name, "shrinkAllocation")) {
            u32 result;
            i = blockAt(a);
            if (i < 0) { excluded++; continue; } /* embedded payload header */
            result = retailHeap.blocks[i].next - a - 8 <= (u64)b + 16 ? 0 : a - 8;
            pcHeapShrink(host(a), b);
            CHECK(result == expected);
            CHECK(retailHeap.synced);
            returns++;
        } else CHECK(0);
    }
    fclose(f);
    printf("%s: %d events, %d modeled returns, %d explicitly excluded header/payload events\n", path, events, returns, excluded);
}

int main(int argc, char** argv) {
    int i;
    unit();
    for (i = 1; i < argc; i++) trace(argv[i]);
    printf("retailheaptest: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
