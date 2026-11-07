#ifndef MAIN_MEMORY_H
#define MAIN_MEMORY_H

/* Kernel heap ("Ls" memory manager: LsGetMem / LsFreeMem, MCB log). */

#include "common.h"

/* Memory control block in front of every allocation: 8 bytes on the PS1,
 * 16 on the PC (64-bit pointer). */
typedef struct HeapBlock {
    struct HeapBlock* m0_next;  /* data pointer of the next block */
    u32 m4_caller : 21;         /* caller address >> 2 */
    u32 m4_user : 4;            /* 0 = free, 1 = end marker, else owner id */
    u32 m4_pinned : 1;          /* locked: freeHeap refuses */
    u32 m4_type : 6;            /* content type (0x20 = default) */
} HeapBlock;

/* Header size, and the allocation granule: 4 bytes on the PS1; 16 on the PC
 * so every block stays aligned for the 64-bit pointers stored in it. */
#define HEAP_HEADER ((s32)sizeof(HeapBlock))
#define HEAP_GRAIN 16

void* allocateHeap(s32 size, s32 flags);   /* allocate (Noah: allocateHeap) */
s32 setHeapAllowFailure(s32);
void flagAllocation(void*);
void unflagAllocation(void*);
s32 freeHeap(void* ptr);               /* free: 0, -1 if locked, 1 */
void resetMemoryAllocStats(s32, sptr);
u8* mallocAndDecompress(void* src, s32 flags);    /* allocate and decompress */
void decompress(void*, void*);
void initHeap(HeapBlock* start, HeapBlock* end);
void resetHeap(HeapBlock* start);
u8* getBssEnd(void);                   /* model.c: the heap start */

#endif /* MAIN_MEMORY_H */
