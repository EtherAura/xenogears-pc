/*
 * Heap / memory manager: memory control blocks (MCBs) in front of every
 * allocation, allocateHeap/freeHeap, per-user frees, delayed frees, the MCB
 * dump, and a PC-host symbol file for naming callers.
 *
 * File 0x80031894-0x80032DCC (main executable): hand-written add-to-OT
 * stubs precede it.
 * It owns .rodata 0x80018998-0x80018A7C, .data 0x80050110-0x80050190 and
 * .sdata 0x800591CC-0x800592BC (name tables, short strings, debugPrintSink).
 * allocateHeap, freeHeap and freeHeapDelayed record their caller with
 * GET_RA (include/stack.h). Built with GCC 2.7.2 (PSY-Q), ASPSX < 2.50.
 */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/debug_font.h"
#include "stack.h"
#include "pc_retail_heap.h"
#include "pc_glyph.h"
#include "pc_interpolation.h"

/* ---- dynamic (morphing) vertices: Noah kernel/modelDynamicVertices.cpp ---- */

/* One vertex offset of a morph target. */
typedef struct {
    /* 0x00 */ s16 m0_offset[3];
    /* 0x06 */ s16 m6_index;
} DynVertOffset;

typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ DynVertOffset* m4_positions;
    /* 0x08 */ DynVertOffset* m8_normals;
} ModelDynVertEntry;

/* ModelBlock.m1C: count, count entries, then { index count, s16* indices }. */
typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ ModelDynVertEntry m4_entries[1];
} ModelDynVerts;

/* Noah: sModelBlock (same layout as src/main/model.c) */
typedef struct {
    /* 0x00 */ u16 m0_flags;
    /* 0x02 */ u16 m2_numVertices;
    /* 0x04 */ u16 m4_numPrims;
    /* 0x06 */ u16 m6_numMeshBlocks;
    /* 0x08 */ SVECTOR* m8_vertices;
    /* 0x0C */ SVECTOR* mC_normals;
    /* 0x10 */ u8* m10_meshBlocks;
    /* 0x14 */ u8* m14_displayList;
    /* 0x18 */ u8* m18_instanceData;
    /* 0x1C */ ModelDynVerts* m1C_dynamicVertices;
    /* 0x20 */ s16 m20_boundingBox[8];
    /* 0x30 */ s32 m30_instanceDataSize;
    /* 0x34 */ s32 m34_primBufferSize;
} ModelBlock; /* 0x38 */

/* Delayed-free list node; delayedFreeListHead is the list's head node. */
typedef struct DelayedFree {
    struct DelayedFree* m0_next;
    void* m4_ptr;
    s32 m8_delay;  /* ticks left */
} DelayedFree;

void bootGame(s32) __attribute__((noreturn));  /* fatal error screen */
void traceNextAlloc(s32);
static u16 heapNextAllocType;         /* current content type */
static u16 currentHeapUser;         /* current user */
static HeapBlock* heapFirstBlockData;  /* data pointer of the first block */
static s32 D_80059324;         /* unused */
static s32 D_80059328;         /* unused */
static s32 heapNeedsMerge;         /* free blocks may need merging */
static s32 heapAllowFailure;         /* error handler disabled */
static u8* heapSymbolFileStart;         /* symbol file start */
static u8* heapSymbolFileEnd;         /* symbol file end */
static s32 heapLastRequestSize;         /* size of the last request */
static s32 heapLastRequestCaller;         /* caller of the last request */
#if VERSION_US
static s32 D_80059344;         /* unused (US only: JP's .sbss is 4 bytes shorter) */
#endif
static char** heapUserTypeNames[10];  /* per-user type-name tables */
static DelayedFree delayedFreeListHead;  /* delayed-free list head node */
void PCinit(void);
s32 PCclose(s32);
void traceNextAlloc(s32);
void printHeapDebug();  /* defined below; called with 1 or 2 arguments */
void resetHeapTraceState(void);
void mergeFreeHeapBlocks(void);
void freeAllHeap(void);

/* LibLS.LIB banner (unreferenced), heap.c's first initialized data: in JP
 * the string is the first of this object's .rodata (0x80018968) and the
 * pointer the first of its .sdata (0x80058864); US blanked the text, so " "
 * sits in .sdata (0x800591C4) ahead of the pointer (0x800591C8). */
#if VERSION_JP
char* libLsBanner = "\nPS General Routines(Polygon,3D Graphics Engine,Memory Manager,Message,"
                   "Font,Controler,VSync,Kernel OS,Compress,Debugger&Tools) Library "
                   "'LibLS.LIB' for XENOGEARS Programed by K.Sugimoto@SQUARE\n";
#else
char* libLsBanner = " ";
#endif

/* User (owner) names, indexed by m4_user. */
char* heapUserNames[] = {
    "....", "END ", "HIG ", "KAZM", "MASA", "MIYA",
    "SUGI", "SUZU", "YOSI", "SIMA", "????", "TEST",
};

/* Shared content-type names, indexed by m4_type & 0x1F (types 0x20-0x3F). */
char* heapTypeNames[] = {
    "",            "FREE AREA",   "fake malloc", "fake calloc", "MDL Data",
    "MDL Packet",  "MDL Light",   "CD CHACE",    "MES IMAGE",   "MES WORK",
    "MES CUE",     "MIMe Work",   "MIMe Vertex", "MIMe Normal", "SYMBOL DATA",
    "SOUND",       "MES FONT",    "MES SYSDATA", "LsFONT",      "DelayFree",
};

/* Usable size of an allocation. */
s32 getAllocationSize(HeapBlock* data) {
    return (s32)((u8*)data[-1].m0_next - (u8*)data - sizeof(HeapBlock));
}

/* Owner (user id) of an allocation. */
s32 getAllocationUser(HeapBlock* data) {
    return data[-1].m4_user;
}

/* Address of the code that made the allocation. */
s32 getAllocationCaller(HeapBlock* data) {
    return (data[-1].m4_caller << 2) + 0x80000000;
}

/* Whether an allocation is pinned (freeHeap refuses it). */
s32 isAllocationFlagged(HeapBlock* data) {
    return data[-1].m4_pinned;
}

void func_800318F0(void) {
}

/* Load a symbol file ("SYM1") from the PC host into the heap. */
s32 loadHeapSymbolFile(char* name) {
    s32 fd;
    s32 size;
    s32 chunk;
    u8* p;

    chunk = 0x8000;
    PCinit();
    fd = PCopen(name, 0, 0);
    if (fd == -1) {
        return -1;
    }
    size = PClseek(fd, 0, 2);
    PClseek(fd, 0, 0);
    traceNextAlloc(0x2E);
    p = allocateHeap(size, 0);
    heapSymbolFileStart = p;
    heapSymbolFileEnd = p + size;
    /* goto out of the loop: a break gets the exit test rotated to the bottom */
    for (;;) {
        if (size <= 0) {
            goto done;
        }
        if (size < chunk) {
            chunk = size;
        }
        PCread(fd, p, chunk);
        size -= chunk;
        p += chunk;
    }
done:
    PCclose(fd);
    if (heapSymbolFileStart[0] != 'S' || heapSymbolFileStart[1] != 'Y' || heapSymbolFileStart[2] != 'M' || heapSymbolFileStart[3] != '1') {
        freeHeap(heapSymbolFileStart);
        heapSymbolFileStart = 0;
        heapSymbolFileEnd = 0;
        return 0;
    }
    return 0;
}

void resetHeapTraceState(void) {
    pcHeapOwner(10);
    setDebugFontBuffer(0);
    heapNextAllocType = 0x20;
    currentHeapUser = 10;
    heapSymbolFileStart = 0;
    heapSymbolFileEnd = 0;
}

void initHeap(HeapBlock* start, HeapBlock* end) {
    pcHeapInvalidate(); /* sandbox/unknown arenas do not inherit game mappings */
    start = (HeapBlock*)((uptr)start & ~3);
    end = (HeapBlock*)((uptr)end & ~3);
    heapFirstBlockData = start + 1;
    start->m0_next = end;
    heapNeedsMerge = 0;
    heapNextAllocType = 0x20;
    start->m4_user = 0;
    start->m4_type = 0x21;
    currentHeapUser = 10;
    end[-1].m0_next = end;
    heapSymbolFileStart = 0;
    end[-1].m4_user = 1;
    end[-1].m4_type = 0x20;
    heapSymbolFileEnd = 0;
    delayedFreeListHead.m0_next = NULL;
    resetHeapTraceState();
}

void resetHeap(HeapBlock* start) {
    HeapBlock* old;
    freeAllHeap();
    if (heapNeedsMerge != 0) {
        mergeFreeHeapBlocks();
    }
    start = (HeapBlock*)((uptr)start & ~3);
    old = heapFirstBlockData;
    heapFirstBlockData = start + 1;
    start->m0_next = old[-1].m0_next;
    start->m4_user = 0;
    start->m4_type = 0x21;
    delayedFreeListHead.m0_next = NULL;
}

s32 getHeapUser(void) {
    return currentHeapUser;
}

void setHeapUser(s32 user) {
    pcHeapOwner(user);
    currentHeapUser = user;
}

s32 setHeapAllowFailure(s32 flag) {
    s32 old = heapAllowFailure;
    heapAllowFailure = flag;
    return old;
}

void getLastAllocRequest(s32* caller, s32* size) {
    *caller = heapLastRequestCaller;
    *size = heapLastRequestSize;
}

/* Allocate size bytes; mode 0 first fit, 1 top-down (last fit), 2 best fit. */
/* Enhanced (xg-hbc.19.2, 60 Hz Battling): the heap's used extent, the end
 * marker's data address: everything allocated lies below it, for a snapshot
 * of the heap (port/src/game/battling/between.c). */
void* pcHeapUsedEnd(void) {
    HeapBlock* data = heapFirstBlockData;
    HeapBlock* blk;
    s32 guard = 1 << 20;
    if (data == NULL) return NULL;
    for (;;) {
        blk = data - 1;
        if (blk->m4_user == 1 || blk->m0_next == NULL || --guard == 0) return data;
        data = blk->m0_next;
    }
}

void* allocateHeap(s32 size, s32 mode) {
    u32 ra;
    HeapBlock* blk;
    HeapBlock* data;
    HeapBlock* best;
    HeapBlock* bestData;
    HeapBlock* exact;
    u32 bestSize;
    u32 avail;
    s32 diff;
    s32 none;
    HeapBlock* nb;
    HeapBlock* p;
    s32 nativeSize = size;
    s32 retailSize = pcHeapTakeRequest();

    GET_RA(&ra);
    ra -= 8;
    heapLastRequestCaller = ra;
    ra = (ra << 7) >> 9;
    if (heapNeedsMerge != 0) {
        mergeFreeHeapBlocks();
    }
    none = 1;
    heapLastRequestSize = size;
    size += HEAP_GRAIN - 1;
    size &= ~(HEAP_GRAIN - 1);
    bestData = 0;
    bestSize = 0x800000;
    best = 0;
    data = heapFirstBlockData;
    exact = 0;
    blk = data - 1;
    for (;;) {
    while (blk->m4_user != 0) {
        if (blk->m4_user == 1) {
            goto end;
        }
        data = blk->m0_next;
        blk = data - 1;
    }
    avail = (u32)((uptr)blk->m0_next - (uptr)blk - 2 * HEAP_HEADER);
    diff = avail - size;
    if (diff == 4 || diff == 0) {
        none = 0;
        if (mode == 1) {
            exact = blk;
            goto next;
        }
    alloc_exact:
        blk->m4_user = currentHeapUser;
        blk->m4_type = heapNextAllocType;
        blk->m4_pinned = 0;
        blk->m4_caller = ra;
        heapNextAllocType = 0x20;
        pcHeapAllocated(blk + 1, nativeSize, retailSize, mode);
        return blk + 1;
    }
    if (diff >= 5) {
        none = 0;
        switch (mode) {
        case 2:
            if (avail < bestSize) {
                bestData = data;
                best = blk;
                bestSize = avail;
            }
            break;
        case 1:
            best = blk;
            break;
        default:
            bestData = data;
            best = blk;
            goto split;
        }
    }
next:
    data = blk->m0_next;
    blk = data - 1;
    }
end:
#if VERSION_JP
    if (heapAllowFailure != 0) {
        if (none) {
            pcHeapAllocated(0, nativeSize, retailSize, mode);
            return 0;
        }
    } else if (none) {
        bootGame(0x82);
    }
#else
    if (none) {
        if (heapAllowFailure != 0) {
            pcHeapAllocated(0, nativeSize, retailSize, mode);
            return 0;
        }
        bootGame(0x82);
    }
#endif
    if (mode == 1) {
        if (best < exact) {
            blk = exact;
            goto alloc_exact;
        }
        p = (HeapBlock*)((u8*)best->m0_next - (size + HEAP_HEADER));
        p[-1].m0_next = best->m0_next;
        p[-1].m4_user = currentHeapUser;
        p[-1].m4_type = heapNextAllocType;
        p[-1].m4_pinned = 0;
        p[-1].m4_caller = ra;
        heapNextAllocType = 0x20;
        best->m0_next = p;
        pcHeapAllocated(p, nativeSize, retailSize, mode);
        return p;
    }
split:
    nb = (HeapBlock*)((u8*)bestData + size);
    nb->m0_next = best->m0_next;
    nb->m4_user = best->m4_user;
    nb->m4_type = best->m4_type;
    nb->m4_pinned = best->m4_pinned;
    nb->m4_caller = best->m4_caller;
    best->m4_type = heapNextAllocType;
    best->m0_next = nb + 1;
    best->m4_user = currentHeapUser;
    best->m4_pinned = 0;
    best->m4_caller = ra;
    heapNextAllocType = 0x20;
    pcHeapAllocated(bestData, nativeSize, retailSize, mode);
    return bestData;
}

/* Split block p: carve a free block after the first size bytes. */
HeapBlock* shrinkAllocation(HeapBlock* p, u32 size) {
    HeapBlock* hdr;
    HeapBlock* next;
    HeapBlock* nb;
    pcHeapShrink(p, size);
    next = p[-1].m0_next;
    hdr = p - 1;
    if ((uptr)next - (uptr)hdr - 2 * HEAP_HEADER <= size + 2 * HEAP_HEADER) {
        return 0;
    }
    nb = (HeapBlock*)((u8*)p + size);
    nb->m0_next = next;
    heapNeedsMerge = 1;
    nb->m4_user = 0;
    nb->m4_type = 0x21;
    nb->m4_caller = 0;
    nb->m4_pinned = 0;
    p[-1].m0_next = nb + 1;
    return hdr;
}

void mergeFreeHeapBlocks(void) {
    HeapBlock* blk;
    pcHeapMerge();
    for (blk = heapFirstBlockData - 1; blk->m4_user != 1; blk = blk->m0_next - 1) {
        while (blk->m4_user == 0 && blk->m0_next[-1].m4_user == 0) {
            blk->m0_next = blk->m0_next[-1].m0_next;
        }
    }
    heapNeedsMerge = 0;
}

void flagAllocation(void* p) {
    pcHeapPin(p, 1);
    ((HeapBlock*)p)[-1].m4_pinned = 1;
}

void unflagAllocation(void* p) {
    pcHeapPin(p, 0);
    ((HeapBlock*)p)[-1].m4_pinned = 0;
}

void unpinAllocation(HeapBlock* p) {
    pcHeapPin(p, 0);
    p[-1].m4_pinned = 0;
}

/* Free: marks the block free (type 0x21, user 0); merging is deferred. */
s32 freeHeap(void* p) {
    u32 ra;
    if (p == NULL) {
        if (heapAllowFailure != 0) {
            return 1;
        }
        GET_RA(&ra);
        heapLastRequestSize = 0;
        heapLastRequestCaller = ra - 8;
        bootGame(0x83);
    }
    if (((HeapBlock*)p)[-1].m4_pinned) {
        return -1;
    }
    pcGlyphClearRAM(p, (s32)((uptr)((HeapBlock*)p)[-1].m0_next - (uptr)p) - HEAP_HEADER, 3);
    pcInterpolationHeapFree(p, (s32)((uptr)((HeapBlock*)p)[-1].m0_next - (uptr)p) - HEAP_HEADER);
    pcHeapFreed(p);
    ((HeapBlock*)p)[-1].m4_type = 0x21;
    ((HeapBlock*)p)[-1].m4_user = 0;
    ((HeapBlock*)p)[-1].m4_caller = 0;
    heapNeedsMerge = 1;
    return 0;
}

void freeHeapForUser(u8 user) {
    HeapBlock* blk;
    HeapBlock* cur;
    blk = heapFirstBlockData - 1;
    while (blk->m4_user != 1) {
        if (blk->m4_user == user) {
            cur = blk;
            blk = blk->m0_next - 1;
            freeHeap(cur + 1);
        } else {
            blk = blk->m0_next - 1;
        }
    }
}

void freeAllHeap(void) {
    HeapBlock* blk;
    HeapBlock* cur;
    blk = heapFirstBlockData - 1;
    while (blk->m4_user != 1) {
        cur = blk;
        blk = blk->m0_next - 1;
        freeHeap(cur + 1);
    }
    pcHeapAllFreed();
}

void forceFreeAllHeap(void) {
    HeapBlock* blk;
    HeapBlock* cur;
    blk = heapFirstBlockData - 1;
    while (blk->m4_user != 1) {
        cur = blk;
        blk = blk->m0_next - 1;
        unflagAllocation(cur + 1);
        freeHeap(cur + 1);
    }
}

s32 getTotalFreeHeap(void) {
    HeapBlock* blk;
    s32 total = 0;
    for (blk = heapFirstBlockData - 1; blk->m4_user != 1; blk = blk->m0_next - 1) {
        if (blk->m4_user == 0) {
            total += (sptr)blk->m0_next - (sptr)blk - 2 * HEAP_HEADER;
        }
    }
    return total;
}

s32 walkHeapBlockList(void) {
    HeapBlock* blk;
    for (blk = heapFirstBlockData - 1; blk->m4_user != 1; blk = blk->m0_next - 1) {
    }
    return 0;
}

s32 getLargestFreeHeapBlock(void) {
    HeapBlock* blk;
    u32 size;
    u32 max = 0;
    for (blk = heapFirstBlockData - 1; blk->m4_user != 1; blk = blk->m0_next - 1) {
        if (blk->m4_user == 0) {
            size = (u32)((uptr)blk->m0_next - (uptr)blk - 2 * HEAP_HEADER);
            if (max < size) {
                max = size;
            }
        }
    }
    if (max < HEAP_HEADER) {
        max = HEAP_HEADER;
    }
    return max - HEAP_HEADER;
}

void resetMemoryAllocStats(s32 user, sptr names) {
    pcHeapOwner(user);
    currentHeapUser = user;
    /* The table has 10 entries: user 10 (resetDefaultMemoryAllocStats, just
     * after resetHeap) writes the PS1's next word, the delayed-free list
     * head, which resetHeap has already cleared. */
    if (user == 10) {
        delayedFreeListHead.m0_next = (DelayedFree*)names;
    } else {
        heapUserTypeNames[user] = (char**)names;
    }
    heapAllowFailure = 0;
}

void traceNextAlloc(s32 type) {
    heapNextAllocType = type;
}

/* Find the symbol at or below addr in the loaded symbol file; copy its name to out. */
void findSymbolNameForAddress(u32 addr, char* out) {
    u8* p;
    u8* name;
    s32 len;
    u32 v;

    p = heapSymbolFileStart + 4;
    if (p != 0) {
        while (p < heapSymbolFileEnd) {
            v = *p++;
            v |= *p++ << 8;
            v |= *p++ << 16;
            v |= *p++ << 24;
            if (v >= addr) {
                break;
            }
            name = p;
            len = *p++;
            p += len;
        }
        len = *name++;
        while (--len != -1) {
            *out++ = *name++;
        }
    }
    *out = 0;
}

/* Print one block's columns (flags as for dumpHeapBlocks). */
void printHeapBlockInfo(HeapBlock* blk, void* data, s32 size, s32 flags) {
    char buf[64];
    char* name;

    /* the low address bits, as %06x prints them */
    if (flags & 2) {
        printHeapDebug("%06x ", (u32)(uptr)blk & 0xFFFFFF);
    }
    if (flags & 4) {
        printHeapDebug("%06x ", (u32)(uptr)data & 0xFFFFFF);
    }
    if (flags & 8) {
        printHeapDebug("%6x ", size);
    }
    if (flags & 0x10) {
        printHeapDebug("%s ", heapUserNames[blk->m4_user]);
    }
    if (flags & 0x20) {
        printHeapDebug("%06x ", blk->m4_caller << 2);
    }
    if ((flags & 0x40) && blk->m4_user != 0) {
        findSymbolNameForAddress((blk->m4_caller << 2) - 0x80000000, buf);
        printHeapDebug("%s", buf);
        if ((flags & 0x80) && (blk->m4_type & 0x1F)) {
            printHeapDebug(" / ");
        }
    }
    if ((flags & 0x80) && (blk->m4_type & 0x1F)) {
        if (blk->m4_type & 0x20) {
            name = heapTypeNames[blk->m4_type & 0x1F];
        } else {
            name = heapUserTypeNames[blk->m4_user][blk->m4_type];
        }
        printHeapDebug("%s", name);
    }
    printHeapDebug("\n");
}

/* Dump the block list: mode 2/3 merge runs of blocks with the same
 * user+type / caller; skip rows first, then at most count rows (0 = all). */
void dumpHeapBlocks(s32 mode, s32 skip, s32 count, s32 flags) {
    char buf[64]; /* unused: reserves 0x10-0x4F like retail */
    HeapBlock* blk;
    HeapBlock* data;
    HeapBlock* next;
    s32 size;
    s32 index;
    s32 limited;

    index = 0;
    limited = 0;
    if (flags == 0) {
        flags = 0x808D;
    }
    if (mode != 0) {
        mergeFreeHeapBlocks();
    }
    if (count != 0) {
        limited = 1;
    }
    if (heapSymbolFileStart == 0) {
        flags &= ~0x40;
    }
    if (flags & 1) {
        printHeapDebug("No- ");
    }
    if (flags & 2) {
        printHeapDebug("MCB--- ");
    }
    if (flags & 4) {
        printHeapDebug("ADDR-- ");
    }
    if (flags & 8) {
        printHeapDebug("SIZE-- ");
    }
    if (flags & 0x10) {
        printHeapDebug("USER ");
    }
    if (flags & 0x20) {
        printHeapDebug("GETADD ");
    }
    if (flags & 0x40) {
        printHeapDebug("FUNCTION/");
    }
    if (flags & 0x80) {
        printHeapDebug("CONTENTS");
    }
    size = 0;
    printHeapDebug("\n");
    blk = heapFirstBlockData - 1;
    data = heapFirstBlockData;
    while (blk->m4_user != 1) {
        size += (sptr)blk->m0_next - (sptr)blk - 2 * HEAP_HEADER;
        if (mode == 2 && blk->m4_user == blk->m0_next[-1].m4_user && blk->m4_type == blk->m0_next[-1].m4_type) {
            index++;
            blk = blk->m0_next - 1;
            continue;
        }
        if (mode == 3 && blk->m4_caller == blk->m0_next[-1].m4_caller) {
            index++;
            blk = blk->m0_next - 1;
            continue;
        }
        if (skip != 0) {
            skip--;
        } else {
            if (flags & 1) {
                printHeapDebug("%3d ", index);
            }
            printHeapBlockInfo(blk, data, size, flags);
            count--;
        }
        if (limited && count == 0) {
            break;
        }
        index++;
        next = blk->m0_next;
        size = 0;
        blk = next - 1;
        data = next;
    }
    if (flags & 1) {
        printHeapDebug("--- ");
    }
    if (flags & 2) {
        printHeapDebug("------ ");
    }
    if (flags & 4) {
        printHeapDebug("------ ");
    }
    if (flags & 8) {
        printHeapDebug("------ ");
    }
    if (flags & 0x10) {
        printHeapDebug("---- ");
    }
    if (flags & 0x8000) {
        printHeapDebug("\nFree %6x", getTotalFreeHeap());
    }
    printHeapDebug("\n");
}

/* Allocate a locked block for the sound driver (user 7, type 0x2F). */
void* allocateSoundBlock(s32 size) {
    u16 user = currentHeapUser;
    void* p;
    currentHeapUser = 7;
    heapNextAllocType = 0x2F;
    p = allocateHeap(size, 1);
    flagAllocation(p);
    currentHeapUser = user;
    return p;
}

/* "fake calloc": n * size bytes (user 7, type 0x23), not cleared. */
void* fakeCalloc(s32 n, s32 size) {
    u16 user = currentHeapUser;
    void* p;
    currentHeapUser = 7;
    heapNextAllocType = 0x23;
    p = allocateHeap(size * n, 0);
    currentHeapUser = user;
    return p;
}

/* Unlock and free. */
void unflagAndFreeHeap(void* p) {
    unflagAllocation(p);
    freeHeap(p);
}

/* Debug print sink (dumpHeapToPcFile points it at a file writer). Defined here,
 * after the dump functions, so that it follows their strings in .sdata. */
void (*debugPrintSink)(const char*, ...) = logToScreen;

/* printf through the debug sink. */
void printHeapDebug(char* fmt, s32 arg) {
    char buf[1024];
    sprintf(buf, fmt, arg);
    debugPrintSink(buf);
}

/* Free p after delay ticks (0 = now). */
void freeHeapDelayed(void* p, s32 delay) {
    u32 ra;
    DelayedFree* e;
    if (p == NULL) {
        GET_RA(&ra);
        heapLastRequestSize = 0;
        heapLastRequestCaller = ra - 8;
        bootGame(0x83);
    }
    if (delay == 0) {
        freeHeap(p);
        return;
    }
    traceNextAlloc(0x33);
    e = allocateHeap(PC_HEAP_SIZED(sizeof(DelayedFree), 0xC), 1);
    e->m0_next = delayedFreeListHead.m0_next;
    e->m4_ptr = p;
    e->m8_delay = delay;
    delayedFreeListHead.m0_next = e;
}

/* Count down delayed frees; free the ones that expire. */
void updateDelayedFrees(void) {
    DelayedFree* prev = &delayedFreeListHead;
    DelayedFree* e;
    while ((e = prev->m0_next) != NULL) {
        if (--e->m8_delay == -1) {
            freeHeap(e->m4_ptr);
            prev->m0_next = e->m0_next;
            freeHeap(e);
            if (prev->m0_next == NULL) {
                break;
            }
        } else {
            prev = prev->m0_next;
        }
    }
}

/* Free every pending delayed free now. */
void flushDelayedFrees(void) {
    DelayedFree* head = &delayedFreeListHead;
    DelayedFree* e = head->m0_next;
    DelayedFree* n;
    while (e != NULL) {
        freeHeap(e->m4_ptr);
        head->m0_next = e->m0_next;
        freeHeap(e);
        /* separate n + break: retail tests the reloaded head in $a0, then copies */
        n = head->m0_next;
        if (n == NULL) {
            break;
        }
        e = n;
    }
}
