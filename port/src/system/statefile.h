#ifndef PORT_SYSTEM_STATEFILE_H
#define PORT_SYSTEM_STATEFILE_H

/*
 * Save state files (statefile.c): the header, the build id, packing, slots.
 *
 *   "XGSTATE\0", u32 format version, u32 header size, then the header
 *   (little-endian, at fixed offsets: a state from another build or format
 *   still shows in the slot list), the thumbnail and the body, each packed
 *   (statecodec.h). The body is the chunks savestate.c writes.
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
} StateHeader;

#define STATE_FLAG_STEPPED 1u /* saved with XG_CLOCK=step */

/* This executable's id: its code and its sections' places (16 bytes). */
const uint8_t* stateBuildId(void);
const char* stateBuildText(void); /* the same in hex, for messages */

/* Packs and writes a state atomically (a .tmp file renamed over path).
 * thumb: RGB888 thumbW x thumbH, or NULL. 0 with why on failure. */
int stateFileWrite(const char* path, const StateHeader* h, const uint8_t* thumb, const uint8_t* body, char* why,
                   size_t whySize);
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

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_STATEFILE_H */
