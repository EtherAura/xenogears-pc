#ifndef PC_RETAIL_HEAP_H
#define PC_RETAIL_HEAP_H

/* Explicit allocation provenance for address-only retail metadata. Unknown
 * requests stop new predictions; neither the host heap nor its sizes change. */
#include "common.h"
enum { PC_HEAP_UNKNOWN = -1, PC_HEAP_NATIVE_ONLY = -2 };
s32 pcHeapRequest(s32 nativeSize, s32 retailSize);
s32 pcHeapRequestBytes(s32 size);
s32 pcHeapTakeRequest(void);
void pcHeapInvalidate(void);
void pcHeapUnsupported(void);
void pcHeapStartRetail(u32 start);
u32 portRetailHeapStart(s32 mode);
void pcHeapResetRetail(u32 start);
void pcHeapMerge(void);
void pcHeapAllFreed(void);
void pcHeapOwner(s32 owner);
void pcHeapAllocated(void* pointer, s32 nativeSize, s32 retailSize, s32 mode);
void pcHeapFreed(void* pointer);
void pcHeapPin(void* pointer, s32 pinned);
void pcHeapShrink(void* pointer, u32 size);
u32 pcHeapAddress(const void* pointer);
s32 pcHeapSize(const void* pointer);
s32 pcHeapReservation(const void* top, u32 destination);
u32 pcHeapElement(const void* base, const void* element, s32 count, s32 nativeStride, s32 retailStride);
#define PC_HEAP_BYTES(size) pcHeapRequestBytes(size)
#define PC_HEAP_NATIVE(size) pcHeapRequest((size), PC_HEAP_NATIVE_ONLY)
#define PC_HEAP_SIZED(nativeSize, retailSize) pcHeapRequest((nativeSize), (retailSize))
#endif
