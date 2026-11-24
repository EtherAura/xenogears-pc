#ifndef PORT_SYSTEM_STATEPORTABLE_H
#define PORT_SYSTEM_STATEPORTABLE_H

/*
 * Portable slots (stateportable.c; docs/CANONICAL_STATES.md §16, milestone 4):
 * a slot saved at a safe point carries a trailer (statefile.h StatePortable)
 * and its build's schema is filed in the save folder, so another build of the
 * same engine and region loads it by converting (stateconvert.h).
 *
 * This build's schema is tools/state_schema.py's state_schema.json beside the
 * executable (XG_STATE_SCHEMA names another file, or "none"). Without one
 * (Release builds today) portable saving is off: slots save as before and load
 * only in this build. It is read at the first portable save or conversion
 * (about 50 ms) and kept, after checking that it describes this executable
 * (its sections, regions and a few functions where they are now, and not older
 * than the executable). Addresses in a schema are link addresses: both builds
 * must run at them (no load bias; position-independent builds are refused).
 */

#include <stddef.h>
#include <stdint.h>

#include "stateconvert.h"
#include "statefile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Whether a slot save waits for a safe point to be portable: this build has
 * a schema file and no load bias. A file's presence only (cheap); call it
 * on the main thread before a save's worker runs. */
int statePortableOn(void);

/* A slot's trailer, made from its body (on the save's worker thread): the
 * converter's encoding of the body with this build's schema names every
 * string the state's pointers reach (read from this build's own memory),
 * and the schema is filed in the save folder once. 0 with why: the slot is
 * then saved without one, for this build only. */
int statePortableMake(const uint8_t* body, size_t size, StatePortable* out, char* why, size_t whySize);

/* Whether another build's state (h, in the file at path) can be converted
 * here, without reading its body: its trailer, the writer's schema filed in
 * the save folder, this build's schema, no load bias. 0 with why (a
 * sentence part: "saved by ..."). */
int statePortableCanLoad(const StateHeader* h, const char* path, char* why, size_t whySize);

/* The conversion: another build's state (h, body, from path) as this
 * build's, stackless (the game starts again at its safe point), `now` (this
 * build's running state) giving its chunks. Warnings go to the log. 0 with
 * why. */
int statePortableConvert(const StateHeader* h, const uint8_t* body, const char* path, const uint8_t* now,
                         size_t nowSize, StateConvertResult* out, char* why, size_t whySize);

/* How a state's writer is named in messages: "version 1.2" or "another build
 * of the port". */
void statePortableWriter(const StateHeader* h, char* out, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_STATEPORTABLE_H */
