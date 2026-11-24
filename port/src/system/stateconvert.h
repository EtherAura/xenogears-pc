#ifndef PORT_SYSTEM_STATECONVERT_H
#define PORT_SYSTEM_STATECONVERT_H

/*
 * A save state of one build of the port as another build's
 * (stateconvert.c; docs/CANONICAL_STATES.md §16, milestone 3): the C port of
 * tools/statecanon.py convert, whose output it equals byte for byte. The
 * writer's raw body is read through its schema into the game's data by stable
 * identity, and written into the reader's layout: the reader's template body
 * (its own running state, as t.xgs is for statecanon) gives the chunk order
 * and the parts the writer did not carry.
 */

#include <stddef.h>
#include <stdint.h>

#include "stateschema.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A build's strings in its read-only data, which a state's pointers reach
 * (statecanon Schema.string_at and string_addr). The writer's side needs `at`
 * only; the reader's needs both. */
typedef struct StateStrings {
    void* user;
    /* The NUL-terminated string at addr (without its NUL) in *text and *len;
     * 0 when addr is not read-only data or no NUL follows within 4096 bytes. */
    int (*at)(void* user, uint64_t addr, const uint8_t** text, size_t* len);
    /* The address of the first string equal to text, scanning the read-only
     * ranges in the schema's order, each from its start (only strings that
     * begin after a NUL or at a range's start); 0 if none. */
    int (*find)(void* user, const uint8_t* text, size_t len, uint64_t* addr);
} StateStrings;

/* Strings over byte images of the read-only ranges: the executable's file
 * (statecanon reads it) or the running build's own memory. */
typedef struct {
    uint64_t lo, hi;      /* the range's addresses */
    const uint8_t* bytes; /* the byte at lo, and what follows */
    size_t size;          /* how many can be read from lo: at least hi - lo; the file's next 4096 bytes let a
                           * string run past hi as statecanon's search in the file does */
} StateRoSegment;

typedef struct StateRoStrings StateRoStrings;
/* The segments are kept by reference (not copied); NULL when out of memory. */
StateRoStrings* stateRoStringsNew(const StateRoSegment* segments, int32_t count);
void stateRoStringsFree(StateRoStrings* r);
StateStrings stateRoStrings(StateRoStrings* r);

/* One build's side of a conversion: its raw body (unpacked), schema and strings. */
typedef struct {
    const uint8_t* body;
    size_t size;
    const StateSchema* schema;
    StateStrings strings;
} StateConvertSide;

typedef struct {
    char* text; /* as statecanon words it */
    int64_t count;
} StateConvertWarning;

typedef struct {
    uint8_t* body; /* the reader's raw body */
    size_t size;
    int64_t safePoint; /* statecanon safe_point_of: the safe point the writer saved at (0: none) */
    /* the warnings of statecanon's Encoder and Decoder, in the order each was first counted */
    StateConvertWarning* encodeWarnings;
    int32_t encodeWarningCount;
    StateConvertWarning* decodeWarnings;
    int32_t decodeWarningCount;
} StateConvertResult;

/* The writer's state as the reader's: statecanon decode(encode(writer), reader, template, stackless).
 * reader->body is the template. stackless: without the game's frames (statecanon convert always
 * converts so: the load starts the game again at the safe point it was saved at). 1 on success; 0 with
 * a message in err where statecanon would fail too (another shape of value than the type takes, a state
 * that is not at a safe point for a stackless one) or memory ran out. */
int stateConvert(const StateConvertSide* writer, const StateConvertSide* reader, int stackless,
                 StateConvertResult* out, char* err, size_t errSize);
void stateConvertResultFree(StateConvertResult* r);

/* stateConvert's encoding of the writer's state alone, nothing decoded: out gets the safe point and
 * the encode warnings (no body). writer->strings.at is asked for every string the state's pointers
 * reach, as a conversion will ask the writer's table: a portable slot's table is made so (savestate.c,
 * statefile.h StatePortable). 1 on success; 0 with a message in err. */
int stateConvertEncode(const StateConvertSide* writer, StateConvertResult* out, char* err, size_t errSize);

/* A template for stateConvert from the reader's own state at another moment than the writer's (the
 * running game's at a load; statecanon's template comes from the same moment): its chunks in their
 * order, each as it is but for those the reader keeps as bytes (not "mem." and no variable of its
 * schema whose type fills the chunk: vram's captures, the arrays a pointer points at, ...), which take
 * the size the writer's chunk of that tag has (zeros; the
 * n-th of a tag the writer's n-th), as a template of the writer's moment would: the same data takes
 * the same room. malloc'd into *out; 0
 * with a message in err when a body is damaged or memory ran out. */
int stateConvertTemplate(const uint8_t* reader, size_t readerSize, const StateSchema* readerSchema,
                         const uint8_t* writer, size_t writerSize, uint8_t** out, size_t* outSize, char* err,
                         size_t errSize);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_STATECONVERT_H */
