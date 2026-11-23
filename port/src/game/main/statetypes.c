/*
 * What the game heap's blocks hold, and the typed ranges in memory that has
 * no C type, for canonical save states (include/pc_state.h). Part of
 * game_main: the pending type and the ranges are saved with the module's
 * BSS, so a state written by one build says where its pointers are, and a
 * load in another build finds them (tools/statecanon.py reads pcStateRanges
 * by name).
 */
#include "common.h"
#include "pc_state.h"

#define RANGE_LIMIT 4096

typedef struct {
    const void* at;
    u32 type;  /* pcStateTypeId of the name */
    s32 count;
} StateRange;

static u32 pendingType;
static struct {
    s32 count;
    s32 overflow; /* a range was not recorded: such a state is not canonical */
    StateRange ranges[RANGE_LIMIT];
} pcStateRanges;

u32 pcStateTypeId(const char* name) {
    /* FNV-1a (32 bits) of the name without its blanks: tools/state_schema.py
     * hashes the same text from the source */
    u32 h = 0x811C9DC5u;
    for (; *name; name++) {
        if (*name != ' ' && *name != '\t' && *name != '\n') h = (h ^ (u8)*name) * 0x01000193u;
    }
    return h ? h : 1;
}

s32 pcHeapRequestType(const char* name, s32 size) {
    pendingType = pcStateTypeId(name);
    return size;
}

u32 pcHeapTakeType(void) {
    u32 type = pendingType;
    pendingType = 0;
    return type;
}

void pcStateAt(const void* at, const char* name, s32 count) {
    u32 type = pcStateTypeId(name);
    s32 i;
    for (i = 0; i < pcStateRanges.count; i++) {
        if (pcStateRanges.ranges[i].at == at) {
            pcStateRanges.ranges[i].type = type;
            pcStateRanges.ranges[i].count = count;
            return;
        }
    }
    if (pcStateRanges.count == RANGE_LIMIT) {
        pcStateRanges.overflow = 1;
        return;
    }
    pcStateRanges.ranges[pcStateRanges.count].at = at;
    pcStateRanges.ranges[pcStateRanges.count].type = type;
    pcStateRanges.ranges[pcStateRanges.count].count = count;
    pcStateRanges.count++;
}

void pcStateForget(const void* start, const void* end) {
    s32 i;
    s32 n = 0;
    for (i = 0; i < pcStateRanges.count; i++) {
        const u8* at = pcStateRanges.ranges[i].at;
        if (at >= (const u8*)start && at < (const u8*)end) continue;
        pcStateRanges.ranges[n++] = pcStateRanges.ranges[i];
    }
    /* the unused entries stay zero, so states of the same moment are alike */
    for (i = n; i < pcStateRanges.count; i++) {
        pcStateRanges.ranges[i].at = 0;
        pcStateRanges.ranges[i].type = 0;
        pcStateRanges.ranges[i].count = 0;
    }
    pcStateRanges.count = n;
}

void pcStateHeapFree(const void* start, s32 bytes) {
    if (pcStateRanges.count) pcStateForget(start, (const u8*)start + bytes);
}
