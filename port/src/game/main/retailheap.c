/* Address-only retail allocation metadata. Original implementation from this
 * project's matched heap.c, validated by black-box US/JP MCB traces. It never
 * owns game memory or substitutes a host address for a retail one. Explicit
 * annotations admit only proven byte sizes and conversion-only allocations.
 * Unknown requests stop new predictions until a proven whole-heap reset;
 * already-proven live allocation addresses remain valid until freed/reused.
 *
 * Part of game_main: these fixed arrays, pending request and validity flags
 * are saved with the module's data/BSS, including a suspended allocation. */
#include "common.h"
#include "pc_retail_heap.h"

#define BLOCK_LIMIT 4096
#define RETAIL_HEADER 8u
#define RETAIL_END 0x801FC000u

typedef struct {
    u32 data, next;
    s32 owner, pinned;
} RetailBlock;
typedef struct {
    const void* native;
    u32 address, retailSize;
    s32 nativeSize;
    u32 reserved; /* explicit zero instead of indeterminate saved padding */
} NativeMapping;
static struct {
    RetailBlock blocks[BLOCK_LIMIT];
    NativeMapping mappings[BLOCK_LIMIT];
    s32 count, owner, dirty, synced, pending, unsupported;
    const void* unknownPins[BLOCK_LIMIT];
    s32 unknownPinCount, allFreed;
} retailHeap;

void pcHeapUnsupported(void) {
    retailHeap.synced = 0;
    retailHeap.unsupported = 2; /* size/lifetime/error uncertainty cannot recover */
}

static void unclassified(void) {
    retailHeap.synced = 0;
    if (retailHeap.unsupported < 2) retailHeap.unsupported = 1;
}

void pcHeapInvalidate(void) {
    s32 i;
    retailHeap.count = 0;
    retailHeap.synced = 0;
    retailHeap.pending = PC_HEAP_UNKNOWN;
    retailHeap.unsupported = 0;
    retailHeap.unknownPinCount = retailHeap.allFreed = 0;
    for (i = 0; i < BLOCK_LIMIT; i++) {
        retailHeap.mappings[i].native = 0;
    }
}

void pcHeapStartRetail(u32 start) {
    pcHeapInvalidate();
    /* The player's boot header supplies the BSS boundary, independently of
     * native object addresses and enlarged host allocation sizes. */
    if (start < 0x80000000u || start > RETAIL_END - 2 * RETAIL_HEADER || (start & 3)) return;
    retailHeap.blocks[0].data = start + RETAIL_HEADER;
    retailHeap.blocks[0].next = RETAIL_END;
    retailHeap.blocks[0].owner = retailHeap.blocks[0].pinned = 0;
    retailHeap.blocks[1].data = retailHeap.blocks[1].next = RETAIL_END;
    retailHeap.blocks[1].owner = 1;
    retailHeap.blocks[1].pinned = 0;
    retailHeap.count = 2;
    retailHeap.owner = 10;
    retailHeap.dirty = 0;
    retailHeap.synced = 1;
}

s32 pcHeapRequest(s32 nativeSize, s32 retailSize) {
    retailHeap.pending = retailSize;
    return nativeSize;
}

s32 pcHeapRequestBytes(s32 size) {
    return pcHeapRequest(size, size);
}

s32 pcHeapTakeRequest(void) {
    s32 size = retailHeap.pending;
    retailHeap.allFreed = 0;
    retailHeap.pending = PC_HEAP_UNKNOWN;
    /* Reject before the native allocation can fail or unwind bootGame. */
    if (size == PC_HEAP_UNKNOWN) unclassified();
    else if (size < 0 && size != PC_HEAP_NATIVE_ONLY) pcHeapUnsupported();
    return size;
}

void pcHeapOwner(s32 owner) {
    retailHeap.owner = owner;
}

static s32 blockAt(u32 address) {
    s32 i;
    for (i = 0; i < retailHeap.count - 1; i++) {
        if (retailHeap.blocks[i].data == address) return i;
    }
    return -1;
}

static NativeMapping* mappingAt(const void* pointer) {
    s32 i;
    for (i = 0; i < BLOCK_LIMIT; i++) {
        if (pointer && retailHeap.mappings[i].native == pointer) return &retailHeap.mappings[i];
    }
    return 0;
}

static void merge(void) {
    s32 i = 0, j;
    while (i < retailHeap.count - 1) {
        if (!retailHeap.blocks[i].owner && !retailHeap.blocks[i + 1].owner) {
            retailHeap.blocks[i].next = retailHeap.blocks[i + 1].next;
            for (j = i + 1; j < retailHeap.count - 1; j++) retailHeap.blocks[j] = retailHeap.blocks[j + 1];
            retailHeap.count--;
        } else i++;
    }
    retailHeap.dirty = 0;
}

void pcHeapMerge(void) {
    if (retailHeap.synced) merge();
}

static s32 insert(s32 i, RetailBlock block) {
    s32 j;
    if (retailHeap.count == BLOCK_LIMIT) {
        pcHeapUnsupported();
        return 0;
    }
    for (j = retailHeap.count; j > i; j--) retailHeap.blocks[j] = retailHeap.blocks[j - 1];
    retailHeap.blocks[i] = block;
    retailHeap.count++;
    return 1;
}

static u32 allocateRetail(u32 size, s32 mode) {
    s32 i, best = -1, exact = -1;
    u32 bestSize = 0x800000, available, diff, address;
    RetailBlock* b;
    RetailBlock after;
    if (!retailHeap.synced) return 0;
    if (size > 0x7FFFFFFCu || retailHeap.owner < 2 || retailHeap.owner > 15 || mode < 0 || mode > 2) {
        pcHeapUnsupported();
        return 0;
    }
    if (retailHeap.dirty) merge();
    size = (size + 3) & ~3u;
    for (i = 0; i < retailHeap.count - 1; i++) {
        b = &retailHeap.blocks[i];
        if (b->owner) continue;
        available = b->next - b->data - RETAIL_HEADER;
        if (available < size) continue;
        diff = available - size;
        if (diff == 0 || diff == 4) {
            if (mode == 1) exact = i;
            else {
                b->owner = retailHeap.owner; b->pinned = 0;
                return b->data;
            }
        } else if (diff >= 5) {
            if (mode == 0) { best = i; break; }
            if (mode == 1 || available < bestSize) { best = i; bestSize = available; }
        }
    }
    if (best < 0 && exact < 0) return 0;
    if (mode == 1 && exact > best) {
        b = &retailHeap.blocks[exact]; b->owner = retailHeap.owner; b->pinned = 0;
        return b->data;
    }
    b = &retailHeap.blocks[best];
    if (mode == 1) {
        address = b->next - size - RETAIL_HEADER;
        after = (RetailBlock){ address, b->next, retailHeap.owner, 0 };
        if (!insert(best + 1, after)) return 0;
        retailHeap.blocks[best].next = address;
    } else {
        address = b->data;
        after = (RetailBlock){ address + size + RETAIL_HEADER, b->next, 0, 0 };
        if (!insert(best + 1, after)) return 0;
        b = &retailHeap.blocks[best];
        b->next = after.data; b->owner = retailHeap.owner; b->pinned = 0;
    }
    return address;
}

void pcHeapAllocated(void* pointer, s32 nativeSize, s32 retailSize, s32 mode) {
    s32 i;
    u32 address;
    NativeMapping* m = mappingAt(pointer);
    retailHeap.allFreed = 0;
    if (m) {
        /* Reuse without an observed free is an unsupported lifetime, rather
         * than permission to predict another retail allocation. */
        m->native = 0;
        pcHeapUnsupported();
    }
    if (nativeSize < 0) { pcHeapUnsupported(); return; }
    if (retailSize == PC_HEAP_NATIVE_ONLY) {
        /* Keep conversion-only lifetimes too: pinning one must not turn it
         * into an unknown retail survivor at the next whole-heap reset. */
        if (!pointer) return;
        address = 0;
    } else {
        if (retailSize == PC_HEAP_UNKNOWN && nativeSize >= 0) { unclassified(); return; }
        if (retailSize < 0 || nativeSize < 0) { pcHeapUnsupported(); return; }
        if (!retailHeap.synced) return;
        address = allocateRetail((u32)retailSize, mode);
        if ((!pointer) != (!address)) { pcHeapUnsupported(); return; }
        if (!address) return;
    }
    for (i = 0; i < BLOCK_LIMIT; i++) {
        if (!retailHeap.mappings[i].native) {
            retailHeap.mappings[i] = (NativeMapping){ pointer, address, (u32)retailSize, nativeSize, 0 };
            return;
        }
    }
    pcHeapUnsupported();
}

void pcHeapFreed(void* pointer) {
    NativeMapping* m = mappingAt(pointer);
    s32 i;
    if (!m) return; /* no proven lifetime for this native block */
    if (!m->address) { m->native = 0; return; }
    i = blockAt(m->address);
    if (i < 0 || retailHeap.blocks[i].pinned) pcHeapUnsupported();
    else { retailHeap.blocks[i].owner = 0; retailHeap.dirty = 1; }
    m->native = 0;
}

void pcHeapPin(void* pointer, s32 pinned) {
    NativeMapping* m = mappingAt(pointer);
    s32 i;
    retailHeap.allFreed = 0;
    if (!m) {
        for (i = 0; i < retailHeap.unknownPinCount; i++) if (retailHeap.unknownPins[i] == pointer) break;
        if (pinned && i == retailHeap.unknownPinCount) {
            if (i == BLOCK_LIMIT) { pcHeapUnsupported(); return; }
            retailHeap.unknownPins[retailHeap.unknownPinCount++] = pointer;
        } else if (!pinned && i < retailHeap.unknownPinCount) {
            retailHeap.unknownPins[i] = retailHeap.unknownPins[--retailHeap.unknownPinCount];
        }
        return;
    }
    if (!m->address) return; /* proven conversion-only pin */
    i = blockAt(m->address);
    if (i < 0) pcHeapUnsupported();
    else retailHeap.blocks[i].pinned = pinned != 0;
}

void pcHeapAllFreed(void) { retailHeap.allFreed = 1; }

void pcHeapResetRetail(u32 start) {
    s32 i;
    if (!retailHeap.synced) {
        /* A mode's actual freeAllHeap removes every transient allocation.
         * Only known pinned allocations may survive; their addresses and
         * extents remain facts even while transient sizes were unknown. */
        if (retailHeap.unsupported != 1 || !retailHeap.allFreed || retailHeap.unknownPinCount) return;
        for (i = 0; i < BLOCK_LIMIT; i++) if (retailHeap.mappings[i].native && retailHeap.mappings[i].address) {
            s32 b = blockAt(retailHeap.mappings[i].address);
            if (b < 0 || !retailHeap.blocks[b].pinned) return;
        }
        retailHeap.synced = 1;
        retailHeap.unsupported = 0;
    }
    retailHeap.allFreed = 0;
    for (i = 0; i < retailHeap.count - 1; i++) {
        if (!retailHeap.blocks[i].pinned) retailHeap.blocks[i].owner = 0;
    }
    merge();
    start = (start & ~3u) + RETAIL_HEADER;
    if (retailHeap.blocks[0].owner || start < 0x80000008u || start > retailHeap.blocks[0].next - RETAIL_HEADER) {
        pcHeapUnsupported();
        return;
    }
    retailHeap.blocks[0].data = start;
    for (i = 0; i < BLOCK_LIMIT; i++) {
        NativeMapping* m = &retailHeap.mappings[i];
        s32 b = m->native ? blockAt(m->address) : -1;
        if (m->address && (b < 0 || !retailHeap.blocks[b].owner)) m->native = 0;
    }
}

void pcHeapShrink(void* pointer, u32 size) {
    NativeMapping* m = mappingAt(pointer);
    s32 i;
    RetailBlock* b;
    RetailBlock after;
    if (!m) return;
    if (!m->address) return;
    if (!retailHeap.synced) { pcHeapUnsupported(); return; }
    /* Only byte-identical allocations are currently classified for shrink. */
    if (size & 3 || (u32)m->nativeSize != m->retailSize) {
        m->native = 0; /* its new extent has not been translated */
        pcHeapUnsupported();
        return;
    }
    i = blockAt(m->address);
    if (i < 0) { pcHeapUnsupported(); return; }
    b = &retailHeap.blocks[i];
    if (b->next - b->data - RETAIL_HEADER <= (u64)size + 2 * RETAIL_HEADER) return;
    after = (RetailBlock){ b->data + size + RETAIL_HEADER, b->next, 0, 0 };
    if (!insert(i + 1, after)) return;
    retailHeap.blocks[i].next = after.data;
    retailHeap.dirty = 1;
    m->retailSize = size;
    m->nativeSize = size;
}

u32 pcHeapAddress(const void* pointer) {
    NativeMapping* m = mappingAt(pointer);
    return m ? m->address : 0;
}

/* Original usable extent includes the retail allocator's four-byte slack,
 * independently of the host allocator's larger grain and header. */
s32 pcHeapSize(const void* pointer) {
    NativeMapping* m = mappingAt(pointer);
    s32 i = m ? blockAt(m->address) : -1;
    if (!retailHeap.synced || i < 0 || !retailHeap.blocks[i].owner) return PC_HEAP_UNKNOWN;
    return (s32)(retailHeap.blocks[i].next - m->address - RETAIL_HEADER);
}

/* Linked-in overlays replace retail padding blocks with native tokens.
 * Reproduce the original top-down request from its proven marker address;
 * destination is the overlay's documented link address, never a field ID. */
s32 pcHeapReservation(const void* top, u32 destination) {
    u32 address = pcHeapAddress(top);
    if (!address || destination < 0x80000000u || destination > RETAIL_END - RETAIL_HEADER || (destination & 3) ||
        address < destination + RETAIL_HEADER || address >= RETAIL_END) return PC_HEAP_UNKNOWN;
    return (s32)(address - destination - RETAIL_HEADER);
}

u32 pcHeapElement(const void* base, const void* element, s32 count, s32 nativeStride, s32 retailStride) {
    NativeMapping* m = mappingAt(base);
    uptr offset;
    u64 end;
    if (!m || !m->address || count <= 0 || nativeStride <= 0 || retailStride <= 0 || (uptr)element < (uptr)base) return 0;
    offset = (uptr)element - (uptr)base;
    if (offset % nativeStride || offset / nativeStride >= (u32)count ||
        (u64)(u32)count * nativeStride > (u32)m->nativeSize ||
        (u64)(u32)count * retailStride > m->retailSize) return 0;
    end = (u64)m->address + offset / nativeStride * retailStride;
    return end < RETAIL_END ? (u32)end : 0;
}
