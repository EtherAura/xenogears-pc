/* A release's archive, unpacked (update_archive.h): the zip and the tar (in
 * gzip) read here, written from their specifications (PKWARE's APPNOTE.TXT:
 * the end of central directory, central and local headers; POSIX pax and
 * ustar headers; RFC 1952's gzip member), the deflate data inflated by puff.
 * Everything is checked before the first file is visited, so a damaged
 * archive is refused whole. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "update_archive.h"
#include "puff.h"

#define PATH_MAX_LEN 400

static void say(char* why, size_t size, const char* fmt, ...) {
    if (why == NULL || size == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, size, fmt, ap);
    va_end(ap);
}

uint32_t updateCrc32(uint32_t crc, const uint8_t* data, size_t n) {
    static uint32_t table[256];
    if (table[1] == 0) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static uint32_t le16(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t le32(const uint8_t* p) {
    return le16(p) | le16(p + 2) << 16;
}

/* ---- the entries -------------------------------------------------------------------------------- */

typedef struct {
    char path[PATH_MAX_LEN];
    const uint8_t* packed; /* zip: the stored or deflate data; tar: the file's bytes */
    size_t packedSize;
    size_t size;
    int method; /* 0 stored (and every tar file), 8 deflate */
    uint32_t crc;
    int executable;
    uint8_t* data; /* zip deflate: inflated */
} Entry;

typedef struct {
    Entry* e;
    int count;
    char top[PATH_MAX_LEN];
} List;

static void freeList(List* l) {
    for (int i = 0; i < l->count; i++) free(l->e[i].data);
    free(l->e);
    l->e = NULL;
    l->count = 0;
}

/* An entry's name (n bytes): in the one top folder, relative, no "." or ".." part, no backslash, colon or
 * control character. rel: the path inside the top folder ("" for the folder itself); *folder: it names one. */
static int checkName(const char* name, size_t n, List* l, char* rel, int* folder, char* why, size_t whySize) {
    char s[PATH_MAX_LEN];
    if (n == 0 || n >= sizeof s) {
        say(why, whySize, "an entry's name is empty or too long");
        return 0;
    }
    memcpy(s, name, n);
    s[n] = '\0';
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7f || c == '\\' || c == ':') {
            say(why, whySize, "an entry's name has a character no release file has (%.60s)", s);
            return 0;
        }
    }
    *folder = s[n - 1] == '/';
    if (*folder) s[--n] = '\0';
    if (s[0] == '/') {
        say(why, whySize, "an entry's name is absolute (%.60s)", s);
        return 0;
    }
    for (char *part = s, *end; part != NULL; part = end != NULL ? end + 1 : NULL) {
        end = strchr(part, '/');
        size_t len = end != NULL ? (size_t)(end - part) : strlen(part);
        if (len == 0 || (len == 1 && part[0] == '.') || (len == 2 && part[0] == '.' && part[1] == '.')) {
            say(why, whySize, "an entry's name has an empty, . or .. part (%.60s)", name);
            return 0;
        }
    }
    char* slash = strchr(s, '/');
    size_t topLen = slash != NULL ? (size_t)(slash - s) : strlen(s);
    if (l->top[0] == '\0') {
        memcpy(l->top, s, topLen);
        l->top[topLen] = '\0';
    } else if (strlen(l->top) != topLen || memcmp(l->top, s, topLen) != 0) {
        say(why, whySize, "the archive has more than one top folder (%.40s, %.40s)", l->top, s);
        return 0;
    }
    snprintf(rel, PATH_MAX_LEN, "%s", slash != NULL ? slash + 1 : "");
    if (rel[0] == '\0') *folder = 1; /* the top folder itself */
    return 1;
}

static int sameName(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (SDL_tolower((unsigned char)*a) != SDL_tolower((unsigned char)*b)) return 0;
    }
    return *a == *b;
}

static Entry* add(List* l, const char* rel, char* why, size_t whySize) {
    if (l->count >= UPDATE_ARCHIVE_FILES) {
        say(why, whySize, "more files than a release has");
        return NULL;
    }
    for (int i = 0; i < l->count; i++) {
        if (sameName(l->e[i].path, rel)) { /* names that differ in case only are one file on Windows */
            say(why, whySize, "%.60s is in the archive twice", rel);
            return NULL;
        }
    }
    if (l->count % 64 == 0) {
        Entry* grown = realloc(l->e, (size_t)(l->count + 64) * sizeof *grown);
        if (grown == NULL) {
            say(why, whySize, "out of memory");
            return NULL;
        }
        l->e = grown;
    }
    Entry* e = &l->e[l->count++];
    memset(e, 0, sizeof *e);
    snprintf(e->path, sizeof e->path, "%s", rel);
    return e;
}

/* ---- tar, in a gzip member ------------------------------------------------------------------------ */

/* An octal header field ("0000755 ", "00000004022\0"): its value, or -1. */
static long long octal(const uint8_t* p, size_t n) {
    long long v = 0;
    size_t i = 0;
    while (i < n && p[i] == ' ') i++;
    if (i == n || p[i] < '0' || p[i] > '7') return -1;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++) {
        v = v * 8 + (p[i] - '0');
        if (v > (long long)UPDATE_ARCHIVE_MAX * 8) return -1;
    }
    for (; i < n; i++) {
        if (p[i] != ' ' && p[i] != '\0') return -1;
    }
    return v;
}

static int zeros(const uint8_t* p) {
    for (int i = 0; i < 512; i++) {
        if (p[i] != 0) return 0;
    }
    return 1;
}

/* A pax extended header's records ("<length> <key>=<value>\n"): path and size taken. */
static int paxRecords(const uint8_t* p, size_t n, char* path, size_t pathSize, long long* size, char* why,
                      size_t whySize) {
    size_t at = 0;
    while (at < n) {
        size_t len = 0, i = at;
        while (i < n && p[i] >= '0' && p[i] <= '9' && len < 100000) len = len * 10 + (size_t)(p[i++] - '0');
        if (i >= n || p[i] != ' ' || len == 0 || at + len > n || p[at + len - 1] != '\n') {
            say(why, whySize, "a pax header is damaged");
            return 0;
        }
        const char* key = (const char*)p + i + 1;
        const char* end = (const char*)p + at + len - 1;
        const char* eq = memchr(key, '=', (size_t)(end - key));
        if (eq == NULL) {
            say(why, whySize, "a pax record has no =");
            return 0;
        }
        size_t keyLen = (size_t)(eq - key), valueLen = (size_t)(end - eq - 1);
        if (keyLen == 4 && memcmp(key, "path", 4) == 0) {
            if (valueLen == 0 || valueLen >= pathSize) {
                say(why, whySize, "a pax path is empty or too long");
                return 0;
            }
            memcpy(path, eq + 1, valueLen);
            path[valueLen] = '\0';
        } else if (keyLen == 4 && memcmp(key, "size", 4) == 0) {
            long long v = 0;
            for (size_t k = 0; k < valueLen; k++) {
                char c = eq[1 + k];
                if (c < '0' || c > '9' || v > (long long)UPDATE_ARCHIVE_MAX) {
                    v = -1;
                    break;
                }
                v = v * 10 + (c - '0');
            }
            *size = valueLen > 0 ? v : -1;
            if (*size < 0) {
                say(why, whySize, "a pax size is not a size");
                return 0;
            }
        }
        at += len;
    }
    return 1;
}

static int readTar(const uint8_t* t, size_t n, List* l, char* why, size_t whySize) {
    char paxPath[PATH_MAX_LEN] = "";
    long long paxSize = -1;
    size_t at = 0;
    int ended = 0;
    while (at + 512 <= n) {
        const uint8_t* h = t + at;
        if (zeros(h)) {
            ended = 1;
            break;
        }
        long long sum = octal(h + 148, 8), mine = 0;
        for (int i = 0; i < 512; i++) mine += i >= 148 && i < 156 ? ' ' : h[i];
        if (sum != mine || memcmp(h + 257, "ustar", 5) != 0) {
            say(why, whySize, "a tar header is damaged (at byte %zu)", at);
            return 0;
        }
        long long size = octal(h + 124, 12), mode = octal(h + 100, 8);
        char type = (char)h[156];
        if (type != 'x' && paxSize >= 0) size = paxSize;
        if (size < 0 || mode < 0 || (unsigned long long)size > n - at - 512) {
            say(why, whySize, "a tar entry's size or mode is damaged (at byte %zu)", at);
            return 0;
        }
        const uint8_t* data = h + 512;
        size_t next = at + 512 + (((size_t)size + 511) & ~(size_t)511);
        if (next > n) {
            say(why, whySize, "the tar ends inside an entry");
            return 0;
        }
        if (type == 'x') {
            if (!paxRecords(data, (size_t)size, paxPath, sizeof paxPath, &paxSize, why, whySize)) return 0;
            at = next;
            continue;
        }
        if (type == 'g') { /* global pax records: nothing a release's files need */
            at = next;
            continue;
        }
        char name[PATH_MAX_LEN];
        if (paxPath[0] != '\0') {
            snprintf(name, sizeof name, "%s", paxPath);
        } else {
            char base[101], prefix[156];
            memcpy(base, h, 100);
            base[100] = '\0';
            memcpy(prefix, h + 345, 155);
            prefix[155] = '\0';
            snprintf(name, sizeof name, "%s%s%s", prefix, prefix[0] ? "/" : "", base);
        }
        paxPath[0] = '\0';
        paxSize = -1;
        char rel[PATH_MAX_LEN];
        int folder = 0;
        if (!checkName(name, strlen(name), l, rel, &folder, why, whySize)) return 0;
        if (type == '5') {
            at = next;
            continue;
        }
        if ((type != '0' && type != '\0') || folder) {
            say(why, whySize, "%.60s is not a plain file or folder (type %c)", name, type ? type : '0');
            return 0;
        }
        Entry* e = add(l, rel, why, whySize);
        if (e == NULL) return 0;
        e->packed = data;
        e->packedSize = e->size = (size_t)size;
        e->executable = (mode & 0100) != 0;
        at = next;
    }
    if (!ended) {
        say(why, whySize, "the tar has no end");
        return 0;
    }
    return 1;
}

/* A gzip member (RFC 1952) holding a tar: inflated into *out (the caller frees it). */
/* The gzip header's end: where the deflate data starts, or 0. */
static size_t gzipHeader(const uint8_t* b, size_t n) {
    size_t at = 10;
    uint8_t flags = b[3];
    if (flags & 4) { /* FEXTRA */
        if (at + 2 > n - 8) return 0;
        at += 2 + le16(b + at);
    }
    for (int bit = 8; bit <= 16; bit <<= 1) { /* FNAME, FCOMMENT: zero-terminated */
        if (!(flags & bit)) continue;
        while (at < n - 8 && b[at] != 0) at++;
        at++;
    }
    if (flags & 2) at += 2; /* FHCRC */
    return at < n - 8 ? at : 0;
}

static int gunzip(const uint8_t* b, size_t n, uint8_t** out, size_t* outSize, char* why, size_t whySize) {
    if (n < 18 || b[0] != 0x1f || b[1] != 0x8b || b[2] != 8 || (b[3] & 0xe0) != 0) {
        say(why, whySize, "not a gzip file");
        return 0;
    }
    size_t at = gzipHeader(b, n);
    if (at == 0) {
        say(why, whySize, "the gzip header is damaged");
        return 0;
    }
    uint32_t crc = le32(b + n - 8), size = le32(b + n - 4);
    if (size > UPDATE_ARCHIVE_MAX) {
        say(why, whySize, "the tar would be larger than a release");
        return 0;
    }
    uint8_t* t = malloc(size ? size : 1);
    if (t == NULL) {
        say(why, whySize, "out of memory");
        return 0;
    }
    unsigned long tLen = size, inLen = (unsigned long)(n - 8 - at);
    int r = puff(t, &tLen, b + at, &inLen);
    if (r != 0 || tLen != size || at + inLen != n - 8 || updateCrc32(0, t, tLen) != crc) {
        free(t);
        say(why, whySize, r != 0 ? "the gzip data is damaged (inflate error %d)" : "the gzip file's size or CRC-32 "
                                                                                 "does not match its data", r);
        return 0;
    }
    *out = t;
    *outSize = tLen;
    return 1;
}

/* ---- zip ------------------------------------------------------------------------------------------ */

static int readZip(const uint8_t* b, size_t n, List* l, char* why, size_t whySize) {
    size_t eocd = 0;
    int found = 0;
    if (n >= 22) { /* the end record, with its comment (at most 65535 bytes) after it */
        size_t lowest = n - 22 > 65535 ? n - 22 - 65535 : 0;
        for (size_t i = n - 22 + 1; !found && i-- > lowest;) {
            if (le32(b + i) == 0x06054b50 && i + 22 + le16(b + i + 20) == n) {
                eocd = i;
                found = 1;
            }
        }
    }
    if (!found) {
        say(why, whySize, "the zip has no end record");
        return 0;
    }
    const uint8_t* e = b + eocd;
    uint32_t count = le16(e + 10), cdSize = le32(e + 12), cdAt = le32(e + 16);
    if (le16(e + 4) != 0 || le16(e + 6) != 0 || le16(e + 8) != count || count == 0xFFFF || cdSize == 0xFFFFFFFFu ||
        cdAt == 0xFFFFFFFFu || (size_t)cdAt + cdSize > eocd) {
        say(why, whySize, "the zip's directory is damaged, split or zip64");
        return 0;
    }
    size_t at = cdAt;
    for (uint32_t k = 0; k < count; k++) {
        if (at + 46 > (size_t)cdAt + cdSize || le32(b + at) != 0x02014b50) {
            say(why, whySize, "the zip's directory is damaged (entry %u)", k);
            return 0;
        }
        const uint8_t* c = b + at;
        uint32_t flags = le16(c + 8), method = le16(c + 10), crc = le32(c + 16), packed = le32(c + 20),
                 size = le32(c + 24), nameLen = le16(c + 28), extraLen = le16(c + 30), commentLen = le16(c + 32),
                 local = le32(c + 42);
        if (at + 46 + nameLen + extraLen + commentLen > (size_t)cdAt + cdSize) {
            say(why, whySize, "the zip's directory is damaged (entry %u)", k);
            return 0;
        }
        char rel[PATH_MAX_LEN];
        int folder = 0;
        if (!checkName((const char*)c + 46, nameLen, l, rel, &folder, why, whySize)) return 0;
        at += 46 + nameLen + extraLen + commentLen;
        if (folder) continue;
        if (flags & 0x41) {
            say(why, whySize, "%.60s is encrypted", rel);
            return 0;
        }
        if ((method != 0 && method != 8) || packed == 0xFFFFFFFFu || size == 0xFFFFFFFFu ||
            size > UPDATE_ARCHIVE_MAX || (method == 0 && packed != size)) {
            say(why, whySize, "%.60s is zip64 or packed in a way no release is (method %u)", rel, method);
            return 0;
        }
        if ((size_t)local + 30 > cdAt || le32(b + local) != 0x04034b50) {
            say(why, whySize, "%.60s's local header is damaged", rel);
            return 0;
        }
        size_t data = (size_t)local + 30 + le16(b + local + 26) + le16(b + local + 28);
        if (data + packed > cdAt) {
            say(why, whySize, "%.60s's data runs past the directory", rel);
            return 0;
        }
        Entry* ent = add(l, rel, why, whySize);
        if (ent == NULL) return 0;
        ent->packed = b + data;
        ent->packedSize = packed;
        ent->size = size;
        ent->method = (int)method;
        ent->crc = crc;
    }
    /* Each file inflated and its CRC-32 checked, before any is visited. */
    for (int i = 0; i < l->count; i++) {
        Entry* ent = &l->e[i];
        if (ent->method == 8) {
            ent->data = malloc(ent->size ? ent->size : 1);
            if (ent->data == NULL) {
                say(why, whySize, "out of memory");
                return 0;
            }
            unsigned long outLen = (unsigned long)ent->size, inLen = (unsigned long)ent->packedSize;
            int r = puff(ent->data, &outLen, ent->packed, &inLen);
            if (r != 0 || outLen != ent->size) {
                say(why, whySize, "%.60s's deflate data is damaged (inflate error %d)", ent->path, r);
                return 0;
            }
        }
        const uint8_t* bytes = ent->data != NULL ? ent->data : ent->packed;
        if (updateCrc32(0, bytes, ent->size) != ent->crc) {
            say(why, whySize, "%.60s's CRC-32 does not match its data", ent->path);
            return 0;
        }
    }
    return 1;
}

/* ---- reading ------------------------------------------------------------------------------------- */

int updateArchiveRead(const uint8_t* bytes, size_t n, UpdateArchiveVisit visit, void* user, char* top,
                      size_t topSize, char* why, size_t whySize) {
    List l;
    memset(&l, 0, sizeof l);
    uint8_t* tar = NULL;
    size_t tarSize = 0;
    int ok = 0;
    if (n > UPDATE_ARCHIVE_MAX) {
        say(why, whySize, "larger than a release");
    } else if (n >= 4 && le32(bytes) == 0x04034b50) {
        ok = readZip(bytes, n, &l, why, whySize);
    } else if (n >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b) {
        ok = gunzip(bytes, n, &tar, &tarSize, why, whySize) && readTar(tar, tarSize, &l, why, whySize);
    } else {
        say(why, whySize, "neither a zip nor a gzip file");
    }
    if (ok && l.count == 0) {
        say(why, whySize, "the archive holds no file");
        ok = 0;
    }
    if (ok && top != NULL) snprintf(top, topSize, "%s", l.top);
    for (int i = 0; ok && i < l.count; i++) {
        UpdateArchiveFile f = {l.e[i].path, l.e[i].data != NULL ? l.e[i].data : l.e[i].packed, l.e[i].size,
                               l.e[i].executable};
        if (!visit(user, &f, why, whySize)) {
            if (why != NULL && why[0] == '\0') say(why, whySize, "stopped");
            ok = 0;
        }
    }
    freeList(&l);
    free(tar);
    return ok;
}

int updateArchiveReadFile(const char* path, UpdateArchiveVisit visit, void* user, char* top, size_t topSize,
                          char* why, size_t whySize) {
    size_t n = 0;
    uint8_t* bytes = (uint8_t*)SDL_LoadFile(path, &n);
    if (bytes == NULL) {
        say(why, whySize, "cannot read %s: %s", path, SDL_GetError());
        return 0;
    }
    if (why != NULL && whySize > 0) why[0] = '\0';
    int ok = updateArchiveRead(bytes, n, visit, user, top, topSize, why, whySize);
    SDL_free(bytes);
    return ok;
}
