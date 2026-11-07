/*
 * Menu text images on the PC (include/menu/menu.h ALLOC_TEXT_IMAGE).
 *
 * xg-htn.27: the menus allocate one 13-halfword column less than the rect
 * they LoadImage. The PC allocates the whole image and zeroes that column
 * (on the PS1: the heap's slack, the next block's header and its first
 * bytes, which nothing shows).
 *
 * xg-htn.31: most sites render into the new block without clearing it, so a
 * texel no glyph covers keeps what the heap had there, and box open and
 * close animations sample some of those texels (the status screen's
 * equipment names, the shop's buy list, the 0x3A-wide message lines). In
 * PCSX-Redux (title Continue, status, shop and name entry) a block that held
 * an image before still holds exactly the image last uploaded from it; any
 * other block holds older RAM (game data, 15-27% zero bytes) that only the
 * PS1's own heap history decides. The PC gives a new block the same: the
 * small images uploaded from that memory before, word by word where the
 * heap still holds them, and zero elsewhere. So no host heap bytes reach
 * VRAM, and a reused block shows the text the PS1 shows.
 */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "pc_glyph.h"
#include "pc_retail_heap.h"

#define TEXT_IMAGE_COLUMN (13 * 2)
#define MAX_IMAGE 0x800 /* the menus' images: up to 0x3C x 13 (the shop's description) */
#define RECORDS 64

u8* portHeapStart(void);
u8* portHeapEnd(void);

typedef struct {
    u8* addr;    /* a block ALLOC_TEXT_IMAGE returned, or a small image uploaded from the heap (NULL: unused) */
    s32 size;    /* its bytes (a text image's with the column) */
    s32 shadow;  /* bytes of its last upload kept in data (0: none yet) */
    u32 order;   /* when that upload came */
    u8 data[MAX_IMAGE];
} TextRecord;

static TextRecord records[RECORDS];
static u32 uploads;
static u8 kept[MAX_IMAGE];

/* Which record to replace first: a free one, then the oldest upload. */
static u32 recordAge(const TextRecord* r) {
    if (r->addr == NULL) {
        return 0xFFFFFFFF;
    }
    return r->order == 0 ? 0 : 0xFFFFFFFE - r->order;
}

/* The record of an address: its own, else a free one, else the one with the
 * oldest upload (a text image not uploaded yet may be in use). */
static TextRecord* recordFor(const u8* addr) {
    TextRecord* slot = NULL;
    s32 i;

    for (i = 0; i < RECORDS; i++) {
        TextRecord* r = &records[i];
        if (r->addr == addr) {
            return r;
        }
        if (slot == NULL || recordAge(r) > recordAge(slot)) {
            slot = r;
        }
    }
    return slot;
}

/* port/src/gpu/libgpu.c: every LoadImage. An image's upload is what its block
 * holds once it is freed. */
void pcTextImageUploaded(const void* data, s32 bytes) {
    TextRecord* r = recordFor(data);

    if (r->addr != data) {
        if ((u8*)data < portHeapStart() || (u8*)data >= portHeapEnd() || bytes > MAX_IMAGE) {
            return;
        }
        r->addr = (u8*)data;
        r->size = 0;
    }
    if (bytes > r->size) {
        r->size = bytes < MAX_IMAGE ? bytes : MAX_IMAGE;
    }
    r->shadow = bytes < r->size ? bytes : r->size;
    memcpy(r->data, data, r->shadow);
    r->order = ++uploads;
}

/* The earlier uploads (oldest first) still in the heap at [image, image + n). */
static void keepEarlierImages(u8* image, s32 n) {
    u32 after = 0;
    s32 i;

    bzero(kept, n);
    for (;;) {
        TextRecord* next = NULL;
        for (i = 0; i < RECORDS; i++) {
            TextRecord* r = &records[i];
            if (r->addr != NULL && r->shadow > 0 && r->order > after && (next == NULL || r->order < next->order)) {
                next = r;
            }
        }
        if (next == NULL) {
            return;
        }
        after = next->order;
        {
            u8* lo = next->addr > image ? next->addr : image;
            u8* hi = next->addr + next->shadow < image + n ? next->addr + next->shadow : image + n;
            u8* p;
            /* Blocks are word aligned: a word that differs was overwritten since. */
            for (p = lo; p + 4 <= hi; p += 4) {
                if (*(u32*)p == *(u32*)(next->data + (p - next->addr))) {
                    *(u32*)(kept + (p - image)) = *(u32*)p;
                }
            }
        }
    }
}

/* A new image block of n bytes; past `keep` bytes (the missing column) zero. */
static u8* newImage(s32 n, s32 keep) {
    u8* image = allocateHeap(PC_HEAP_SIZED(n, keep), 0);
    pcGlyphClearRAM(image, n, 3);
    TextRecord* r;

    if (n > MAX_IMAGE) {
        bzero(image, n);
        return image;
    }
    keepEarlierImages(image, n);
    memcpy(image, kept, keep);
    bzero(image + keep, n - keep);
    r = recordFor(image);
    r->addr = image;
    r->size = n;
    r->shadow = 0;
    r->order = 0;
    return image;
}

void* pcAllocTextImage(s32 size) {
    return newImage(size + TEXT_IMAGE_COLUMN, size);
}

/* The menus' label pair image (setupMenuContext4E0): a whole 0x38E block for
 * a 0x1C x 13 image. Never sampled past its glyphs (every menu tour gives the
 * same frames with zero and 0xFF in it), but uploaded whole. */
void* pcAllocLabelImage(s32 size) {
    return newImage(size, size);
}
