#ifndef PC_STATE_H
#define PC_STATE_H

/*
 * What a canonical save state needs to know from the C beyond its types
 * (tools/state_schema.py, tools/statecanon.py): what each heap block holds,
 * and where the PC keeps a pointer in memory that has no C type. A state
 * written by one build loads in another because every pointer is saved by
 * what it points at; these say where the pointers are. Nothing on the PS1:
 * the macros give back the size they wrap.
 *
 * PC_HEAP_OF(T, size): the next allocateHeap holds T, as many as fit, or one
 *   T whose last member is a counted array (PC_STATE_COUNTED). size is the
 *   request as before (PC_HEAP_NATIVE(n) and so on). T is a type's name, a
 *   pointer type (Node*), or PC_PRIMS (GPU primitives: each a tag linking it
 *   into an ordering table, then its words). The block's header keeps the
 *   name's hash (HeapBlock mC_pcType); the schema maps it back, from these
 *   macros in the source.
 *
 * PC_HEAP_NAMED(name, size): the same with the type chosen at run time, name
 *   from a table of PC_STATE_NAME(T) (a type's name, checked, as a constant).
 *
 * PC_STATE_AT(at, T, n): n T at `at`, in memory that has no C type (a disc
 *   file's bytes, where the PC keeps a native pointer) or after the T of a
 *   PC_HEAP_OF block (a converted model's tables). Forgotten when its heap
 *   block is freed; recording one again replaces it.
 *
 * PC_STATE_COUNTED(T, array, count, extra), next to T's definition (no
 *   semicolon: it brings its own): T's last member `array` holds T's member
 *   `count` plus `extra` elements.
 *
 * PC_STATE_TAIL(T, array), the same way: T's last member `array` fills the
 *   rest of its block (a header and as many entries as were allocated).
 *
 * PC_STATE_UNION(T, member, selector, (view0, view1, ...)), the same way:
 *   T's union `member` holds the view its member `selector` names, view0 for
 *   0, view1 for 1, and so on (other values: every view, the most typed last).
 *
 * The names are written out (no macros as T), one use per line: the schema
 * reads them from the source.
 */
#ifdef PLATFORM_PC
#include "common.h"

/* GPU primitives linked into an ordering table (PC_HEAP_OF(PC_PRIMS, ...)). */
typedef struct PC_PRIMS PC_PRIMS;

u32 pcStateTypeId(const char* name);
s32 pcHeapRequestType(const char* name, s32 size);
u32 pcHeapTakeType(void);
void pcStateAt(const void* at, const char* name, s32 count);
void pcStateHeapFree(const void* start, s32 bytes);
void pcStateForget(const void* start, const void* end);

#define PC_STATE_NAME(T) (#T + 0 * sizeof(T*))
#define PC_HEAP_OF(T, size) pcHeapRequestType(PC_STATE_NAME(T), (size))
#define PC_HEAP_NAMED(name, size) pcHeapRequestType((name), (size))
#define PC_STATE_AT(at, T, n) pcStateAt((at), PC_STATE_NAME(T), (n))
#define PC_STATE_COUNTED(T, array, count, extra) \
    _Static_assert(sizeof(((T*)0)->array[0]) > 0 && sizeof(((T*)0)->count) > 0 && (extra) >= 0, \
                   "PC_STATE_COUNTED: " #T);
#define PC_STATE_TAIL(T, array) _Static_assert(sizeof(((T*)0)->array[0]) > 0, "PC_STATE_TAIL: " #T);
#define PC_STATE_UNION(T, member, selector, views) \
    _Static_assert(sizeof(((T*)0)->member) > 0 && sizeof(((T*)0)->selector) > 0, "PC_STATE_UNION: " #T);
#else
#define PC_HEAP_OF(T, size) (size)
#define PC_HEAP_NAMED(name, size) (size)
#define PC_STATE_AT(at, T, n)
#define PC_STATE_COUNTED(T, array, count, extra)
#define PC_STATE_TAIL(T, array)
#define PC_STATE_UNION(T, member, selector, views)
#endif

#endif
