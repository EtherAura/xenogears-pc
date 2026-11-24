/* Save state files (system/statefile.c), disc-free: a header with every field
 * set (the engine id among them: savestate.c's usable() refuses a state from
 * the other engine by it), a thumbnail and a body written and read back; the
 * engine as the file holds it at H_ENGINE (byte 220); a file with another
 * magic and one of another format refused. A portable slot's trailer
 * (StatePortable): written after the body with its flag, read back, unseen by
 * the plain reader, refused when damaged or cut; its strings table sorted and
 * served by address; a schema filed in the "XGSCHEMA" form and read back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "system/statecodec.h"
#include "system/statefile.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

enum { THUMB_W = 16, THUMB_H = 12, BODY_SIZE = 70000 };

static void put(const char* path, const void* data, size_t n) {
    SDL_IOStream* io = SDL_IOFromFile(path, "wb");
    CHECK(io != NULL && SDL_WriteIO(io, data, n) == n);
    if (io != NULL) SDL_CloseIO(io);
}

/* A copy of the good file with 4 bytes at `at` replaced: refused by both readers. */
static void refused(const char* name, const char* path, const uint8_t* file, size_t size, size_t at, const char* with) {
    uint8_t* bad = malloc(size);
    CHECK(bad != NULL);
    if (bad == NULL) return;
    memcpy(bad, file, size);
    memcpy(bad + at, with, 4);
    put(path, bad, size);
    free(bad);

    StateHeader h;
    uint8_t* thumb = (uint8_t*)1;
    uint8_t* body = (uint8_t*)1;
    char why[256] = "";
    int header = stateFileReadHeader(path, &h, &thumb);
    int whole = stateFileRead(path, &h, &body, why, sizeof why);
    if (header || whole) printf("FAIL %s: a file with %s read (header %d, state %d)\n", name, name, header, whole);
    if (header || whole) failures++;
    CHECK(body == NULL);
    CHECK(strcmp(why, "it is no save state of this format") == 0);
    CHECK(SDL_RemovePath(path));
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--image") == 0) { /* for tools/state_schema.py's identity of this file */
        printf("image %s bias %llx build %s path %s\n", stateImageIdText(), (unsigned long long)stateImageBias(),
               stateBuildText(), stateImagePath() ? stateImagePath() : "-");
        return stateImageId() == NULL;
    }
    char base[1024], path[1100], badMagic[1100], badFormat[1100], tmp[1200];
    snprintf(base, sizeof base, "%s/xg-statefiletest-%u", argc > 1 ? argv[1] : SDL_GetCurrentDirectory(),
             (unsigned)SDL_GetTicksNS());
    snprintf(path, sizeof path, "%s.xgs", base);
    snprintf(badMagic, sizeof badMagic, "%s-magic.xgs", base);
    snprintf(badFormat, sizeof badFormat, "%s-format.xgs", base);

    static uint8_t thumb[THUMB_W * THUMB_H * 3], body[BODY_SIZE];
    for (size_t i = 0; i < sizeof thumb; i++) thumb[i] = (uint8_t)(i * 29 + 3);
    for (size_t i = 0; i < sizeof body; i++) body[i] = (uint8_t)(i < 20000 ? i * 7 ^ (i >> 8) : (i / 97) & 3);

    StateHeader h;
    memset(&h, 0, sizeof h);
    for (int i = 0; i < 16; i++) h.buildId[i] = (uint8_t)(0xA0 + i);
    memcpy(h.region, "JP", 3);
    h.disc = 2;
    snprintf(h.serial, sizeof h.serial, "SYNTH_123.45");
    h.sectors = 0x00123457;
    h.vblank = 0x0007654B;
    h.savedAt = 0x0000000165A0BC11ll;
    h.playTime = 98765;
    h.mode = 7;
    h.place = -1;
    snprintf(h.label, sizeof h.label, "statefiletest label: a synthetic place");
    h.flags = STATE_FLAG_STEPPED;
    h.dumpFrames = 0x0000000123456789ull;
    h.thumbW = THUMB_W;
    h.thumbH = THUMB_H;
    h.bodySize = BODY_SIZE;
    h.bodyHash = stateHash(body, BODY_SIZE, 0);
    h.engine = 1;
    snprintf(h.appVersion, sizeof h.appVersion, "9.8.7-synth");

    char why[256] = "";
    CHECK(stateFileWrite(path, &h, thumb, body, why, sizeof why));
    if (why[0]) printf("statefiletest: write: %s\n", why);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    CHECK(!SDL_GetPathInfo(tmp, NULL)); /* renamed over path, not left behind */

    /* The file as written: magic, format, header size, and the engine at byte 220. */
    size_t size = 0;
    uint8_t* file = SDL_LoadFile(path, &size);
    CHECK(file != NULL && size > 256);
    if (file == NULL || size <= 256) {
        printf("statefiletest: %d failures\n", failures + 1);
        return 1;
    }
    CHECK(memcmp(file, "XGSTATE", 8) == 0);
    CHECK(file[8] == STATE_FORMAT && file[9] == 0 && file[10] == 0 && file[11] == 0);
    CHECK(file[12] == 0 && file[13] == 1 && file[14] == 0 && file[15] == 0); /* 256 */
    CHECK(file[220] == 1 && file[221] == 0 && file[222] == 0 && file[223] == 0);
    uint64_t bias = 0; /* the process's load bias at byte 232 (statefile.c H_IMAGE) */
    for (int i = 0; i < 8; i++) bias |= (uint64_t)file[232 + i] << (8 * i);
    CHECK(bias == (uint64_t)stateImageBias());

    /* The image's identity: known, stable, in hex; at the link addresses the build id itself (ELF). */
    const uint8_t* image = stateImageId();
    CHECK(image != NULL && strlen(stateImageIdText()) == 32 && stateImageId() == image);
#ifndef _WIN32
    CHECK(image != NULL && (memcmp(image, stateBuildId(), 16) == 0) == (stateImageBias() == 0));
#endif

    /* The header and the thumbnail. */
    StateHeader r;
    uint8_t* thumbBack = NULL;
    memset(&r, 0x5A, sizeof r);
    CHECK(stateFileReadHeader(path, &r, &thumbBack));
    CHECK(r.engine == 1);
    CHECK(memcmp(r.buildId, h.buildId, 16) == 0);
    CHECK(strcmp(r.region, "JP") == 0);
    CHECK(r.disc == 2);
    CHECK(strcmp(r.serial, h.serial) == 0);
    CHECK(r.sectors == h.sectors);
    CHECK(r.vblank == h.vblank);
    CHECK(r.savedAt == h.savedAt);
    CHECK(r.playTime == h.playTime);
    CHECK(r.mode == h.mode);
    CHECK(r.place == h.place);
    CHECK(strcmp(r.label, h.label) == 0);
    CHECK(r.flags == h.flags);
    CHECK(r.dumpFrames == h.dumpFrames);
    CHECK(r.thumbW == THUMB_W && r.thumbH == THUMB_H);
    CHECK(r.bodySize == BODY_SIZE);
    CHECK(r.bodyHash == h.bodyHash);
    CHECK(strcmp(r.appVersion, "9.8.7-synth") == 0);
    CHECK(r.imageBias == (uint64_t)stateImageBias());
    CHECK(thumbBack != NULL && memcmp(thumbBack, thumb, sizeof thumb) == 0);
    free(thumbBack);

    /* The whole state: the same header, the body intact. */
    uint8_t* bodyBack = NULL;
    memset(&r, 0x5A, sizeof r);
    CHECK(stateFileRead(path, &r, &bodyBack, why, sizeof why));
    CHECK(r.engine == 1);
    CHECK(strcmp(r.region, "JP") == 0 && r.disc == 2);
    CHECK(strcmp(r.label, h.label) == 0 && strcmp(r.appVersion, h.appVersion) == 0);
    CHECK(r.bodySize == BODY_SIZE);
    CHECK(bodyBack != NULL && memcmp(bodyBack, body, BODY_SIZE) == 0);
    free(bodyBack);

    /* Another magic; another format version (STATE_FORMAT + 1). */
    refused("bad magic", badMagic, file, size, 0, "XGSX"); /* "XGSXATE" */
    char format[4] = {(char)(STATE_FORMAT + 1), 0, 0, 0};
    refused("bad format", badFormat, file, size, 8, format);

    /* A portable slot: the same state with a trailer. */
    StatePortable port;
    memset(&port, 0, sizeof port);
    port.schemaHash = 0x0123456789ABCDEFull;
    port.safePoint = 0x10E;
    CHECK(statePortableString(&port, 0x9A0010, (const uint8_t*)"second", 6));
    CHECK(statePortableString(&port, 0x92D000, (const uint8_t*)"first", 5));
    CHECK(statePortableString(&port, 0x9A0010, (const uint8_t*)"again", 5)); /* the same address: the first kept */
    CHECK(statePortableString(&port, 0xF00000, (const uint8_t*)"", 0));      /* an empty string */
    static uint8_t longText[STATE_STRING_MAX + 1];
    CHECK(!statePortableString(&port, 0xF00008, longText, sizeof longText)); /* past the cap: refused */
    statePortableSortStrings(&port);
    CHECK(port.stringCount == 3);
    const uint8_t* text = NULL;
    size_t len = 0;
    CHECK(statePortableStringAt(&port, 0x92D000, &text, &len) && len == 5 && memcmp(text, "first", 5) == 0);
    CHECK(statePortableStringAt(&port, 0x9A0010, &text, &len) && len == 6 && memcmp(text, "second", 6) == 0);
    CHECK(statePortableStringAt(&port, 0xF00000, &text, &len) && len == 0);
    CHECK(!statePortableStringAt(&port, 0x92D001, &text, &len)); /* inside a string: no entry of its own */

    char portPath[1100];
    snprintf(portPath, sizeof portPath, "%s-portable.xgs", base);
    h.flags = STATE_FLAG_STEPPED | STATE_FLAG_PORTABLE; /* (the writer sets the flag by the trailer, whatever h says) */
    CHECK(stateFileWrite(portPath, &h, thumb, body, why, sizeof why));
    StatePortable back;
    CHECK(stateFileReadHeader(portPath, &r, NULL) && r.flags == STATE_FLAG_STEPPED); /* none written: no flag */
    CHECK(!stateFileReadPortable(portPath, &back, why, sizeof why) && strstr(why, "no trailer") != NULL);
    h.flags = STATE_FLAG_STEPPED;
    CHECK(stateFileWritePortable(portPath, &h, thumb, body, &port, why, sizeof why));
    size_t portSize = 0;
    uint8_t* portFile = SDL_LoadFile(portPath, &portSize);
    CHECK(portFile != NULL && portSize > size);
    CHECK(portFile != NULL && memcmp(portFile + 256, file + 256, size - 256) == 0); /* the same thumbnail and body */
    CHECK(portFile != NULL && memcmp(portFile + size, "XGCANON", 8) == 0);           /* then the trailer */
    CHECK(stateFileReadHeader(portPath, &r, NULL) && r.flags == (STATE_FLAG_STEPPED | STATE_FLAG_PORTABLE));
    bodyBack = NULL;
    CHECK(stateFileRead(portPath, &r, &bodyBack, why, sizeof why)); /* the plain reader: the body, the trailer unseen */
    CHECK(bodyBack != NULL && memcmp(bodyBack, body, BODY_SIZE) == 0);
    free(bodyBack);
    CHECK(stateFileReadPortable(portPath, &back, why, sizeof why));
    CHECK(back.schemaHash == port.schemaHash && back.safePoint == 0x10E && back.stringCount == 3);
    CHECK(statePortableStringAt(&back, 0x9A0010, &text, &len) && len == 6 && memcmp(text, "second", 6) == 0);
    CHECK(statePortableStringAt(&back, 0x92D000, &text, &len) && len == 5 && memcmp(text, "first", 5) == 0);
    CHECK(statePortableStringAt(&back, 0xF00000, &text, &len) && len == 0);
    statePortableFree(&back);
    /* a byte of the trailer changed, the trailer cut, a byte past it: refused; the body still reads */
    if (portFile != NULL) {
        for (int k = 0; k < 3; k++) {
            uint8_t* bad = malloc(portSize + 1);
            CHECK(bad != NULL);
            if (bad == NULL) break;
            memcpy(bad, portFile, portSize);
            size_t n = portSize;
            if (k == 0) bad[size + 30] ^= 0x40;  /* the safe point */
            if (k == 1) n = portSize - 5;        /* cut */
            if (k == 2) bad[n++] = 0;            /* one byte too many */
            put(portPath, bad, n);
            free(bad);
            CHECK(!stateFileReadPortable(portPath, &back, why, sizeof why) && strstr(why, "damaged") != NULL);
            CHECK(back.strings == NULL && back.stringCount == 0);
            bodyBack = NULL;
            CHECK(stateFileRead(portPath, &r, &bodyBack, why, sizeof why));
            free(bodyBack);
        }
        SDL_free(portFile);
    }
    CHECK(SDL_RemovePath(portPath));
    statePortableFree(&port);

    /* A schema filed in a folder of its own, as the save folder's schemas/ holds them. */
    char schemaPath[1200];
    snprintf(schemaPath, sizeof schemaPath, "%s-schemas/0123456789abcdef.xgschema", base);
    static const char json[] = "{\"version\": \"us\", \"types\": {}}";
    CHECK(stateSchemaFileWrite(schemaPath, (const uint8_t*)json, strlen(json), why, sizeof why));
    uint8_t* jsonBack = NULL;
    size_t jsonSize = 0;
    CHECK(stateSchemaFileRead(schemaPath, &jsonBack, &jsonSize, why, sizeof why));
    CHECK(jsonBack != NULL && jsonSize == strlen(json) && memcmp(jsonBack, json, jsonSize) == 0);
    CHECK(jsonBack != NULL && jsonBack[jsonSize] == 0);
    free(jsonBack);
    size_t packedSize = 0;
    uint8_t* packed = SDL_LoadFile(schemaPath, &packedSize);
    CHECK(packed != NULL && packedSize > 16 && memcmp(packed, "XGSCHEMA", 8) == 0 && packed[8] == strlen(json));
    if (packed != NULL && packedSize > 20) {
        /* another raw size, then a cut stream: refused (a changed byte of a stored block reads back: the
         * schema's hash, its file name, is checked by its reader, stateportable.c) */
        packed[8] ^= 1;
        put(schemaPath, packed, packedSize);
        CHECK(!stateSchemaFileRead(schemaPath, &jsonBack, &jsonSize, why, sizeof why) && jsonBack == NULL);
        packed[8] ^= 1;
        put(schemaPath, packed, packedSize - 3);
        CHECK(!stateSchemaFileRead(schemaPath, &jsonBack, &jsonSize, why, sizeof why) && jsonBack == NULL);
    }
    SDL_free(packed);
    CHECK(SDL_RemovePath(schemaPath));
    snprintf(schemaPath, sizeof schemaPath, "%s-schemas", base);
    CHECK(SDL_RemovePath(schemaPath));

    SDL_free(file);
    CHECK(SDL_RemovePath(path));
    CHECK(!SDL_GetPathInfo(path, NULL) && !SDL_GetPathInfo(badMagic, NULL) && !SDL_GetPathInfo(badFormat, NULL));

    if (failures) {
        printf("statefiletest: %d failures\n", failures);
        return 1;
    }
    printf("statefiletest: ok (header fields, engine byte 220, the load bias, thumbnail, body; bad magic and format "
           "refused; the image id; a portable trailer and its strings, unseen by the plain reader, damaged ones "
           "refused; a filed schema)\n");
    return 0;
}
