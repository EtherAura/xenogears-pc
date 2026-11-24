#ifndef PORT_SYSTEM_STATEFILE_H
#define PORT_SYSTEM_STATEFILE_H

/*
 * Save state files (statefile.c): the header, the build id, packing, slots.
 *
 *   "XGSTATE\0", u32 format version, u32 header size, then the header
 *   (little-endian, at fixed offsets: a state from another build or format
 *   still shows in the slot list), the thumbnail and the body, each packed
 *   (statecodec.h). The body is the chunks savestate.c writes.
 *
 * A slot saved at a safe point (STATE_FLAG_PORTABLE) has a trailer after the
 * packed body, which readers of the body alone never reach (older builds
 * read the header, the thumbnail and the body by their sizes):
 *   "XGCANON\0", u32 version (1), u32 0, u64 the bytes that follow,
 *   u64 the writer's schema hash, s32 its safe point, u32 the string count,
 *   each string: u64 address, u32 length, the bytes (no NUL), by address;
 *   u64 stateHash of the trailer before it, from its magic on.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STATE_FORMAT 1
#define STATE_THUMB_W 160
#define STATE_THUMB_H 120

typedef struct {
    uint8_t buildId[16];
    char region[4];      /* "US", "JP" */
    uint32_t disc;       /* 1, 2 */
    char serial[16];     /* the disc's boot file: SLUS_006.64... */
    uint32_t sectors;
    uint32_t vblank;
    int64_t savedAt;     /* Unix seconds */
    uint32_t playTime;   /* the game's, seconds */
    int32_t mode;        /* the game mode bootGame started (kernel.c gameModes) */
    int32_t place;       /* the field's id in the field, else -1 */
    char label[96];
    uint32_t flags;      /* STATE_FLAG_* */
    uint64_t dumpFrames; /* XG_AUDIO_DUMP's length at the save */
    uint32_t thumbW, thumbH;
    uint64_t bodySize;   /* raw */
    uint64_t bodyHash;   /* stateHash of the raw body */
    uint32_t engine;     /* system/engine.h: 0 Original, 1 Enhanced (older files: 0) */
    char appVersion[16]; /* the app's version that saved it (stateAppVersion; "" if none) */
    uint64_t imageBias;  /* read: the writer's load bias (stateImageBias when it saved); a write records this
                          * process's own */
} StateHeader;

#define STATE_FLAG_STEPPED 1u  /* saved with XG_CLOCK=step */
#define STATE_FLAG_PORTABLE 2u /* a trailer follows the body (StatePortable): another build converts it */

/* One string of a trailer's table: the text at addr in the writer's read-only data, to its NUL. */
typedef struct {
    uint64_t addr;
    uint32_t len; /* at most STATE_STRING_MAX */
    uint32_t off; /* its bytes in the table's pool */
} StateString;

#define STATE_STRING_MAX 4096

/* What a slot saved at a safe point carries for another build to load it
 * (docs: canonical states §16): the schema of the build that wrote it, by
 * its hash (the schema itself is filed in the save folder:
 * stateSchemaFilePath), the safe point it was saved at, and the text of the
 * strings in the writer's read-only data that the state's pointers reach,
 * which the converter needs from the writer (stateconvert.h StateStrings). */
typedef struct {
    uint64_t schemaHash;  /* stateHash of the writer's state_schema.json */
    int32_t safePoint;    /* statetypes.c safePointId at the save */
    uint32_t stringCount;
    StateString* strings; /* by address, one per address (statePortableSortStrings) */
    uint8_t* pool;
    size_t poolSize, poolCap;
    uint32_t stringCap;
} StatePortable;

/* Adds a string (its text copied); 0 when out of memory or too long. */
int statePortableString(StatePortable* p, uint64_t addr, const uint8_t* text, size_t len);
/* The strings by address, each address once (the first added kept). */
void statePortableSortStrings(StatePortable* p);
/* StateStrings.at over a sorted table (user: the StatePortable). */
int statePortableStringAt(void* user, uint64_t addr, const uint8_t** text, size_t* len);
void statePortableFree(StatePortable* p);

/* This executable's id: its code and its sections' places (16 bytes). */
const uint8_t* stateBuildId(void);
const char* stateBuildText(void); /* the same in hex, for messages */
/* The app's version as players know it (XG_APP_VERSION: the Android app's versionName,
 * set by its loader; elsewhere the release file's version, release.h), at most 15
 * characters; "" for a development build. States record it, so a state another build
 * refuses can say which version made it. */
const char* stateAppVersion(void);
/* Where this build's image sits against its link addresses (its ELF load
 * bias: 0 for a non-PIE executable and on Windows), as states record it for
 * tools (statecheck.py turns symbol addresses into a state's addresses). */
uintptr_t stateImageBias(void);
/* Whether [lo, hi), link addresses, is in a loaded readable segment of this
 * image (0 where that cannot be told: not ELF). */
int stateImageReadable(uint64_t lo, uint64_t hi);
/* The file this image was loaded from ("/proc/self/exe" for the executable;
 * NULL where unknown). */
const char* stateImagePath(void);

/* Packs and writes a state atomically (a .tmp file renamed over path).
 * thumb: RGB888 thumbW x thumbH, or NULL. 0 with why on failure. */
int stateFileWrite(const char* path, const StateHeader* h, const uint8_t* thumb, const uint8_t* body, char* why,
                   size_t whySize);
/* The same with a trailer (portable: a sorted table; NULL writes none). The
 * header's STATE_FLAG_PORTABLE says which, whatever h->flags holds. */
int stateFileWritePortable(const char* path, const StateHeader* h, const uint8_t* thumb, const uint8_t* body,
                           const StatePortable* portable, char* why, size_t whySize);
/* A state's trailer, sorted, into *out (statePortableFree); 0 with why when
 * the file has none (no STATE_FLAG_PORTABLE) or it is damaged. */
int stateFileReadPortable(const char* path, StatePortable* out, char* why, size_t whySize);
/* Reads a state's header (and its thumbnail, malloc'd, when thumb is not
 * NULL); 0 if it is no state file. */
int stateFileReadHeader(const char* path, StateHeader* h, uint8_t** thumb);
/* Reads a whole state: the body (malloc'd, h->bodySize bytes) checked
 * against its hash. 0 with why on failure. */
int stateFileRead(const char* path, StateHeader* h, uint8_t** body, char* why, size_t whySize);

/* The slots' folder (XG_STATE_DIR, else beside settings.ini: states/<US|JP>),
 * created on first use; NULL with XG_SETTINGS=none and no XG_STATE_DIR. */
const char* stateDir(void);
/* A slot's file: slot1.xgs..slot10.xgs, undo.xgs (STATE_UNDO). */
int stateSlotPath(int slot, char* out, size_t size);

/* The schemas portable slots were saved with, one per build, filed in the
 * slots' folder: schemas/<hash, 16 hex digits>.xgschema, "XGSCHEMA", u64 the
 * JSON's size, the JSON packed (statecodec.h): stateschema.h's
 * stateSchemaLoad reads it too. */
int stateSchemaFilePath(uint64_t hash, char* out, size_t size); /* 0: no slots' folder */
/* Writes it atomically (a .tmp renamed over path, the folder made). 0 with why. */
int stateSchemaFileWrite(const char* path, const uint8_t* json, size_t n, char* why, size_t whySize);
/* Its JSON back (malloc'd, NUL after it); 0 with why when absent or damaged. */
int stateSchemaFileRead(const char* path, uint8_t** json, size_t* n, char* why, size_t whySize);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_STATEFILE_H */
