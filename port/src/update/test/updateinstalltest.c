/* The one-click update (update/update_install.c, update_archive.c; xg-4oq.14.4),
 * disc-free and with no network, in a folder of its own (updateinstalltest-work):
 *
 *   - archives made here (zip and tar.gz, their deflate in stored blocks, which
 *     puff inflates as any other) read back: names, bytes, execute bits, the
 *     top folder, a pax path; refused: .., an absolute path, a backslash, two
 *     top folders, a name twice (in any case), a link, an encrypted file, a
 *     changed byte (CRC-32), a cut archive, something else;
 *   - an install of a tarball copy and a zip copy, with a fake fetcher (a
 *     manifest signed with the FIXTURE key of updatemanifesttest) and a fake
 *     download: unpacked beside the copy as .new, nothing installed changed;
 *     swapped, .old beside; tidied at the next start; a player's own file in
 *     the folder untouched;
 *   - refused, nothing changed: a download that is not the manifest's file, an
 *     archive whose release.txt names another version or kind, one without the
 *     running executable, a release that is not newer; a swap that fails
 *     half-way undone (POSIX: a folder made read-only); a swap a crash
 *     interrupted, finished at the next start; a setup only downloaded; the
 *     thread's progress, and a cancelled download.
 *
 *     updateinstalltest                  the checks above
 *     updateinstalltest --unpack ARCHIVE each file as the updater reads it: "path size crc32 x", then "top NAME"
 *                                        (tools/test_update_install.py compares Python's own archives) */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <SDL3/SDL.h>

#include "monocypher-ed25519.h"
#include "update/update_archive.h"
#include "update/update_install.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* ---- archives made here -------------------------------------------------------------------------- */

typedef struct {
    uint8_t* b;
    size_t n, cap;
} Buf;

static void put(Buf* o, const void* data, size_t n) {
    if (o->n + n > o->cap) {
        o->cap = (o->n + n) * 2 + 1024;
        o->b = realloc(o->b, o->cap);
    }
    memcpy(o->b + o->n, data, n);
    o->n += n;
}

static void put16(Buf* o, uint32_t v) {
    uint8_t p[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    put(o, p, 2);
}

static void put32(Buf* o, uint32_t v) {
    put16(o, v & 0xffff);
    put16(o, v >> 16);
}

/* Deflate data of stored blocks (RFC 1951 3.2.4): BFINAL on the last, LEN and NLEN, the bytes. */
static void deflateStored(Buf* o, const uint8_t* data, size_t n) {
    size_t at = 0;
    do {
        size_t len = n - at > 65535 ? 65535 : n - at;
        uint8_t head = at + len == n ? 1 : 0;
        put(o, &head, 1);
        put16(o, (uint32_t)len);
        put16(o, (uint32_t)~len & 0xffff);
        put(o, data + at, len);
        at += len;
    } while (at < n);
}

typedef struct {
    const char* name; /* the whole name, top folder and all */
    const char* text;
    int executable;
    char type; /* tar: '0' a file (default), '5' a folder, '2' a link */
} Item;

static void tarHeader(Buf* o, const char* name, size_t size, int mode, char type) {
    uint8_t h[512];
    memset(h, 0, sizeof h);
    snprintf((char*)h, 100, "%s", name);
    snprintf((char*)h + 100, 8, "%07o", mode);
    snprintf((char*)h + 108, 8, "%07o", 0);
    snprintf((char*)h + 116, 8, "%07o", 0);
    snprintf((char*)h + 124, 12, "%011o", (unsigned)size);
    snprintf((char*)h + 136, 12, "%011o", 0);
    h[156] = (uint8_t)type;
    memcpy(h + 257, "ustar", 6);
    memcpy(h + 263, "00", 2);
    memset(h + 148, ' ', 8);
    unsigned sum = 0;
    for (int i = 0; i < 512; i++) sum += h[i];
    snprintf((char*)h + 148, 8, "%06o", sum);
    put(o, h, 512);
}

static void tarPad(Buf* o, size_t size) {
    static const uint8_t zero[512];
    if (size % 512) put(o, zero, 512 - size % 512);
}

/* A tar of items (paxLong: one more file whose name only a pax header holds), in a gzip member. */
static Buf targz(const Item* items, int count, const char* paxLong) {
    Buf t = {0};
    for (int i = 0; i < count; i++) {
        size_t size = items[i].text ? strlen(items[i].text) : 0;
        char type = items[i].type ? items[i].type : '0';
        tarHeader(&t, items[i].name, type == '0' ? size : 0, items[i].executable ? 0755 : 0644, type);
        if (type == '0') {
            put(&t, items[i].text, size);
            tarPad(&t, size);
        }
    }
    if (paxLong != NULL) {
        char rec[600];
        int body = (int)strlen(paxLong) + 7, len = body + 1; /* " path=...\n" and the length's own digits */
        while (snprintf(NULL, 0, "%d", len) + body != len) len = snprintf(NULL, 0, "%d", len) + body;
        snprintf(rec, sizeof rec, "%d path=%s\n", len, paxLong);
        tarHeader(&t, "././@PaxHeader", strlen(rec), 0644, 'x');
        put(&t, rec, strlen(rec));
        tarPad(&t, strlen(rec));
        tarHeader(&t, "short-name-replaced", 4, 0644, '0');
        put(&t, "long", 4);
        tarPad(&t, 4);
    }
    static const uint8_t zero[1024];
    put(&t, zero, 1024);
    Buf g = {0};
    static const uint8_t head[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 255};
    put(&g, head, 10);
    deflateStored(&g, t.b, t.n);
    put32(&g, updateCrc32(0, t.b, t.n));
    put32(&g, (uint32_t)t.n);
    free(t.b);
    return g;
}

/* A zip of items (method 8, stored blocks), flags on every entry (1: encrypted). */
static Buf zip(const Item* items, int count, uint32_t flags) {
    Buf z = {0}, cd = {0};
    for (int i = 0; i < count; i++) {
        const uint8_t* data = (const uint8_t*)(items[i].text ? items[i].text : "");
        size_t size = strlen((const char*)data), nameLen = strlen(items[i].name);
        Buf d = {0};
        deflateStored(&d, data, size);
        uint32_t crc = updateCrc32(0, data, size), local = (uint32_t)z.n;
        put32(&z, 0x04034b50);
        put16(&z, 20), put16(&z, flags), put16(&z, 8), put16(&z, 0), put16(&z, 0);
        put32(&z, crc), put32(&z, (uint32_t)d.n), put32(&z, (uint32_t)size);
        put16(&z, (uint32_t)nameLen), put16(&z, 0);
        put(&z, items[i].name, nameLen);
        put(&z, d.b, d.n);
        put32(&cd, 0x02014b50);
        put16(&cd, 20), put16(&cd, 20), put16(&cd, flags), put16(&cd, 8), put16(&cd, 0), put16(&cd, 0);
        put32(&cd, crc), put32(&cd, (uint32_t)d.n), put32(&cd, (uint32_t)size);
        put16(&cd, (uint32_t)nameLen), put16(&cd, 0), put16(&cd, 0), put16(&cd, 0), put16(&cd, 0);
        put32(&cd, 0), put32(&cd, local);
        put(&cd, items[i].name, nameLen);
        free(d.b);
    }
    uint32_t at = (uint32_t)z.n;
    put(&z, cd.b, cd.n);
    put32(&z, 0x06054b50);
    put16(&z, 0), put16(&z, 0), put16(&z, (uint32_t)count), put16(&z, (uint32_t)count);
    put32(&z, (uint32_t)cd.n), put32(&z, at), put16(&z, 0);
    free(cd.b);
    return z;
}

/* ---- reading them back ----------------------------------------------------------------------------- */

typedef struct {
    int count;
    char paths[16][128];
    char data[16][128];
    int executable[16];
} Seen;

static int seeFile(void* user, const UpdateArchiveFile* f, char* why, size_t whySize) {
    (void)why, (void)whySize;
    Seen* s = (Seen*)user;
    if (s->count < 16) {
        snprintf(s->paths[s->count], 128, "%s", f->path);
        snprintf(s->data[s->count], 128, "%.*s", (int)f->size, (const char*)f->data);
        s->executable[s->count] = f->executable;
    }
    s->count++;
    return 1;
}

static int reads(Buf b, Seen* s, char* top, char* why) {
    memset(s, 0, sizeof *s);
    int ok = updateArchiveRead(b.b, b.n, seeFile, s, top, 128, why, 200);
    free(b.b);
    return ok;
}

static int refused(Buf b, const char* needle) {
    Seen s;
    char top[128], why[200] = "";
    int ok = reads(b, &s, top, why);
    if (ok || strstr(why, needle) == NULL) printf("  refusal: %s (wanted \"%s\")\n", ok ? "none" : why, needle);
    return !ok && s.count == 0 && strstr(why, needle) != NULL;
}

static void archiveTests(void) {
    static const Item files[] = {
        {"xg-1/", NULL, 0, '5'},
        {"xg-1/bin/xenogears", "new game", 1, 0},
        {"xg-1/share/xenogears-pc/release.txt", "version=2026.12.3\nkind=tarball\n", 0, 0},
        {"xg-1/empty.txt", "", 0, 0},
    };
    Seen s;
    char top[128], why[200] = "";
    CHECK(reads(targz(files, 4, "xg-1/share/doc/xenogears-pc/a-name-longer-than-the-hundred-bytes-a-ustar-name-"
                                "holds-without-its-prefix-field-xxxxxxxxxxxxxxxxxx.txt"),
                &s, top, why));
    CHECK(strcmp(top, "xg-1") == 0 && s.count == 4);
    CHECK(strcmp(s.paths[0], "bin/xenogears") == 0 && strcmp(s.data[0], "new game") == 0 && s.executable[0]);
    CHECK(strcmp(s.paths[1], "share/xenogears-pc/release.txt") == 0 && !s.executable[1]);
    CHECK(strcmp(s.paths[2], "empty.txt") == 0 && s.data[2][0] == '\0');
    CHECK(strncmp(s.paths[3], "share/doc/xenogears-pc/a-name-longer", 36) == 0 && strcmp(s.data[3], "long") == 0);
    CHECK(reads(zip(files + 1, 3, 0), &s, top, why));
    CHECK(strcmp(top, "xg-1") == 0 && s.count == 3 && strcmp(s.data[1], "version=2026.12.3\nkind=tarball\n") == 0);
    CHECK(!s.executable[0]); /* a zip's files are never executable here */
    {
        static uint8_t big[200000]; /* more than one stored block */
        for (size_t i = 0; i < sizeof big; i++) big[i] = (uint8_t)(i * 7);
        char* text = malloc(sizeof big + 1);
        memcpy(text, big, sizeof big);
        for (size_t i = 0; i < sizeof big; i++) text[i] = (char)(text[i] ? text[i] : 1);
        text[sizeof big] = '\0';
        Item one = {"top/big.bin", text, 0, 0};
        CHECK(reads(zip(&one, 1, 0), &s, top, why) && s.count == 1);
        CHECK(reads(targz(&one, 1, NULL), &s, top, why) && s.count == 1);
        free(text);
    }

    Item bad[2] = {{"xg-1/ok.txt", "x", 0, 0}, {NULL, "y", 0, 0}};
    static const char* const BAD_NAMES[][2] = {
        {"xg-1/../escape.txt", ". or .."},       {"/etc/passwd", "absolute"},
        {"xg-1/sub\\file.txt", "character"},    {"xg-2/other.txt", "more than one top folder"},
        {"xg-1/OK.TXT", "twice"},               {"xg-1/c:stream", "character"},
        {"xg-1//double.txt", ". or .."},
    };
    for (size_t i = 0; i < sizeof BAD_NAMES / sizeof BAD_NAMES[0]; i++) {
        bad[1].name = BAD_NAMES[i][0];
        CHECK(refused(zip(bad, 2, 0), BAD_NAMES[i][1]));
        CHECK(refused(targz(bad, 2, NULL), BAD_NAMES[i][1]));
    }
    Item link[2] = {{"xg-1/ok.txt", "x", 0, 0}, {"xg-1/link", NULL, 0, '2'}};
    CHECK(refused(targz(link, 2, NULL), "not a plain file"));
    CHECK(refused(zip(files + 1, 3, 1), "encrypted"));
    {
        Buf z = zip(files + 1, 3, 0);
        z.b[60] ^= 1; /* a byte of the first file's data */
        CHECK(refused(z, "CRC-32"));
        Buf g = targz(files, 4, NULL);
        g.b[g.n - 300] ^= 1; /* a byte of the tar */
        CHECK(refused(g, "CRC-32"));
        Buf cut = targz(files, 4, NULL);
        cut.n -= 40;
        CHECK(refused(cut, "gzip"));
        Buf zcut = zip(files + 1, 3, 0);
        zcut.n -= 5;
        CHECK(refused(zcut, "end record"));
        Buf other = {0};
        put(&other, "not an archive at all", 21);
        CHECK(refused(other, "neither"));
    }
}

/* ---- an install -------------------------------------------------------------------------------------- */

static const char* const FIXTURE_SEED = "aab593a535cad866a4ee538be640cb87ea2ef32f54bdb19c54cb73ca5ca19f76";
static uint8_t secretKey[64], publicKey[32];
static uint8_t trusted[1][32];

static void keys(void) {
    uint8_t seed[32];
    for (int i = 0; i < 32; i++) {
        unsigned v;
        sscanf(FIXTURE_SEED + 2 * i, "%2x", &v);
        seed[i] = (uint8_t)v;
    }
    crypto_ed25519_key_pair(secretKey, publicKey, seed);
    memcpy(trusted[0], publicKey, 32);
}

static void base64(const uint8_t* in, size_t n, char* out) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)in[i] << 16 | (i + 1 < n ? (unsigned)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = T[v >> 18 & 63];
        out[o++] = T[v >> 12 & 63];
        out[o++] = i + 1 < n ? T[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[o] = '\0';
}

/* What the fake fetcher and download serve. */
static struct {
    char manifest[2048], sig[256];
    Buf asset;
    int slow; /* the download in small pieces, a pause between */
} S;

static void serve(const char* version, const char* kind, const char* platform, const char* name, Buf asset) {
    free(S.asset.b);
    S.asset = asset;
    uint8_t sum[64];
    crypto_sha512(sum, asset.b, asset.n);
    char hex[129];
    for (int i = 0; i < 64; i++) snprintf(hex + 2 * i, 3, "%02x", sum[i]);
    snprintf(S.manifest, sizeof S.manifest,
             "format=1\nversion=%s\npage=https://github.com/EtherAura/xenogears-pc/releases/tag/v%s\n"
             "download=https://example.invalid/download/v%s/\nupdates=on\nasset=%s %s %zu %s %s\n",
             version, version, version, kind, platform, asset.n, hex, name);
    uint8_t sig[64];
    char b64[96];
    crypto_ed25519_sign(sig, secretKey, (const uint8_t*)S.manifest, strlen(S.manifest));
    base64(sig, 64, b64);
    snprintf(S.sig, sizeof S.sig, "ed25519 %02x%02x%02x%02x%02x%02x%02x%02x %s\n", publicKey[0], publicKey[1],
             publicKey[2], publicKey[3], publicKey[4], publicKey[5], publicKey[6], publicKey[7], b64);
}

static long fakeFetch(const char* url, char* buf, size_t max, char* why, size_t whySize) {
    (void)why, (void)whySize;
    const char* body = strstr(url, ".sig") != NULL ? S.sig : S.manifest;
    size_t n = strlen(body);
    if (n > max) return -1;
    memcpy(buf, body, n);
    return (long)n;
}

static int fakeDownload(const char* url, const char* path, uint64_t size, UpdateProgress progress, void* user,
                        char* why, size_t whySize) {
    (void)url;
    SDL_IOStream* io = SDL_IOFromFile(path, "wb");
    if (io == NULL) return 0;
    size_t piece = S.slow ? 128 : S.asset.n;
    for (size_t at = 0; at < S.asset.n; at += piece) {
        size_t n = S.asset.n - at < piece ? S.asset.n - at : piece;
        SDL_WriteIO(io, S.asset.b + at, n);
        if (S.slow) SDL_Delay(20);
        if (!progress(user, at + n, size)) {
            SDL_CloseIO(io);
            snprintf(why, whySize, "cancelled");
            return 0;
        }
    }
    SDL_CloseIO(io);
    if (S.asset.n != size) {
        snprintf(why, whySize, "the download is %zu bytes, not %llu", S.asset.n, (unsigned long long)size);
        return 0;
    }
    return 1;
}

static char work[512];

static void writeText(const char* rel, const char* text) {
    char path[1024], folder[1024];
    snprintf(path, sizeof path, "%s%s", work, rel);
    snprintf(folder, sizeof folder, "%s", path);
    char* slash = strrchr(folder, '/');
    if (slash != NULL) {
        *slash = '\0';
        SDL_CreateDirectory(folder);
    }
    SDL_IOStream* io = SDL_IOFromFile(path, "wb");
    if (io != NULL) {
        SDL_WriteIO(io, text, strlen(text));
        SDL_CloseIO(io);
    }
}

static int textIs(const char* rel, const char* want) {
    char path[1024];
    snprintf(path, sizeof path, "%s%s", work, rel);
    size_t n = 0;
    char* t = (char*)SDL_LoadFile(path, &n);
    int same = want == NULL ? t == NULL : t != NULL && n == strlen(want) && memcmp(t, want, n) == 0;
    SDL_free(t);
    return same;
}

static int there(const char* rel) {
    char path[1024];
    snprintf(path, sizeof path, "%s%s", work, rel);
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info);
}

static void removeTree(const char* path) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        char** names = SDL_GlobDirectory(path, "*", 0, NULL);
        for (char** n = names; n != NULL && *n != NULL; n++) {
            char child[1024];
            snprintf(child, sizeof child, "%s/%s", path, *n);
            removeTree(child);
        }
        SDL_free(names);
    }
    SDL_RemovePath(path);
}

/* A tarball copy of 2026.11.27 in work/copy/, its pref folder work/pref/. */
static void tarballCopy(UpdateInstallPlace* p) {
    removeTree(work);
    SDL_CreateDirectory(work);
    writeText("copy/bin/xenogears", "old game");
    writeText("copy/share/xenogears-pc/release.txt", "version=2026.11.27\nkind=tarball\n");
    writeText("copy/share/doc/xenogears-pc/COPYING", "old licence");
    writeText("copy/disc.cue", "the player's own");
    memset(p, 0, sizeof *p);
    snprintf(p->root, sizeof p->root, "%scopy/", work);
    snprintf(p->exe, sizeof p->exe, "bin/xenogears");
    snprintf(p->downloads, sizeof p->downloads, "%spref/update/", work);
    p->kind = RELEASE_TARBALL;
    snprintf(p->version, sizeof p->version, "2026.11.27");
    snprintf(p->platform, sizeof p->platform, "linux-x86_64");
    snprintf(p->base, sizeof p->base, "https://example.invalid/latest/");
    p->fetch = fakeFetch;
    p->download = fakeDownload;
    p->keys = (const uint8_t (*)[32])trusted;
    p->keyCount = 1;
}

static const Item NEW_TARBALL[] = {
    {"xenogears-pc-2026.12.3-linux-x86_64/", NULL, 0, '5'},
    {"xenogears-pc-2026.12.3-linux-x86_64/bin/xenogears", "new game", 1, 0},
    {"xenogears-pc-2026.12.3-linux-x86_64/bin/xenogears-enhanced", "new enhanced", 1, 0},
    {"xenogears-pc-2026.12.3-linux-x86_64/share/xenogears-pc/release.txt", "version=2026.12.3\nkind=tarball\n", 0, 0},
    {"xenogears-pc-2026.12.3-linux-x86_64/share/doc/xenogears-pc/COPYING", "new licence", 0, 0},
};
#define TARBALL_NAME "xenogears-pc-2026.12.3-linux-x86_64.tar.gz"

static int unchanged(void) {
    return textIs("copy/bin/xenogears", "old game") && textIs("copy/share/doc/xenogears-pc/COPYING", "old licence") &&
           !there("copy/bin/xenogears.new") && !there("copy/bin/xenogears-enhanced") && !there("pref/update/files.txt") &&
           textIs("copy/disc.cue", "the player's own");
}

static void installTests(void) {
    UpdateInstallPlace p;
    UpdateInstallStatus st;
    char why[256] = "";

    /* A tarball copy updated: prepared, swapped, tidied. */
    tarballCopy(&p);
    serve("2026.12.3", "tarball", "linux-x86_64", TARBALL_NAME, targz(NEW_TARBALL, 5, NULL));
    CHECK(updateInstallPrepare(&p, &st) && st.stage == INSTALL_READY && strcmp(st.version, "2026.12.3") == 0);
    CHECK(st.done == st.total && st.total == S.asset.n);
    CHECK(textIs("copy/bin/xenogears", "old game") && textIs("copy/bin/xenogears.new", "new game"));
    CHECK(textIs("copy/bin/xenogears-enhanced.new", "new enhanced") && there("pref/update/" TARBALL_NAME));
#ifndef _WIN32
    {
        char path[1024];
        struct stat sb;
        snprintf(path, sizeof path, "%scopy/bin/xenogears.new", work);
        CHECK(stat(path, &sb) == 0 && (sb.st_mode & 0777) == 0755);
        snprintf(path, sizeof path, "%scopy/share/doc/xenogears-pc/COPYING.new", work);
        CHECK(stat(path, &sb) == 0 && (sb.st_mode & 0777) == 0644);
    }
#endif
    CHECK(updateInstallSwap(&p, why, sizeof why));
    CHECK(textIs("copy/bin/xenogears", "new game") && textIs("copy/bin/xenogears.old", "old game"));
    CHECK(textIs("copy/bin/xenogears-enhanced", "new enhanced") && !there("copy/bin/xenogears-enhanced.old"));
    CHECK(textIs("copy/share/xenogears-pc/release.txt", "version=2026.12.3\nkind=tarball\n"));
    CHECK(textIs("copy/disc.cue", "the player's own") && !there("copy/bin/xenogears.new"));
    CHECK(!updateInstallSwap(&p, why, sizeof why) && strstr(why, "nothing is ready") != NULL); /* once only */
    updateInstallTidy(&p);
    CHECK(!there("copy/bin/xenogears.old") && !there("copy/share/doc/xenogears-pc/COPYING.old"));
    CHECK(!there("pref/update/files.txt") && !there("pref/update/" TARBALL_NAME));
    CHECK(textIs("copy/bin/xenogears", "new game"));

    /* Refused, and nothing changed. */
    tarballCopy(&p);
    serve("2026.12.3", "tarball", "linux-x86_64", TARBALL_NAME, targz(NEW_TARBALL, 5, NULL));
    S.asset.b[S.asset.n / 2] ^= 1; /* the download is not the manifest's file */
    CHECK(!updateInstallPrepare(&p, &st) && st.stage == INSTALL_FAILED && strstr(st.why, "SHA-512") != NULL);
    CHECK(unchanged() && !there("pref/update/" TARBALL_NAME) && !there("pref/update/" TARBALL_NAME ".part"));
    {
        Item other[5];
        memcpy(other, NEW_TARBALL, sizeof other);
        other[3].text = "version=2026.12.3\nkind=deb\n"; /* the .deb's */
        serve("2026.12.3", "tarball", "linux-x86_64", TARBALL_NAME, targz(other, 5, NULL));
        CHECK(!updateInstallPrepare(&p, &st) && strstr(st.why, "names 2026.12.3 (deb)") != NULL && unchanged());
        other[3].text = "version=2026.12.2\nkind=tarball\n";
        serve("2026.12.3", "tarball", "linux-x86_64", TARBALL_NAME, targz(other, 5, NULL));
        CHECK(!updateInstallPrepare(&p, &st) && strstr(st.why, "names 2026.12.2") != NULL && unchanged());
        memcpy(other, NEW_TARBALL, sizeof other);
        other[1].name = "xenogears-pc-2026.12.3-linux-x86_64/bin/something-else";
        serve("2026.12.3", "tarball", "linux-x86_64", TARBALL_NAME, targz(other, 5, NULL));
        CHECK(!updateInstallPrepare(&p, &st) && strstr(st.why, "has no bin/xenogears") != NULL && unchanged());
    }
    serve("2026.11.27", "tarball", "linux-x86_64", TARBALL_NAME, targz(NEW_TARBALL, 5, NULL));
    CHECK(!updateInstallPrepare(&p, &st) && strstr(st.why, "latest release") != NULL && unchanged());
    serve("2026.12.3", "zip", "windows-x64", "x.zip", targz(NEW_TARBALL, 5, NULL));
    CHECK(!updateInstallPrepare(&p, &st) && strstr(st.why, "has no tarball for linux-x86_64") != NULL && unchanged());

#ifndef _WIN32
    /* A swap that fails half-way (share/doc read-only): every file back as it was. */
    serve("2026.12.3", "tarball", "linux-x86_64", TARBALL_NAME, targz(NEW_TARBALL, 5, NULL));
    CHECK(updateInstallPrepare(&p, &st));
    char doc[1024];
    snprintf(doc, sizeof doc, "%scopy/share/doc/xenogears-pc", work);
    chmod(doc, 0555);
    int swapped = updateInstallSwap(&p, why, sizeof why);
    chmod(doc, 0755);
    if (geteuid() != 0) { /* root writes a read-only folder anyway */
        CHECK(!swapped && strstr(why, "COPYING") != NULL);
        CHECK(textIs("copy/bin/xenogears", "old game") && textIs("copy/bin/xenogears.new", "new game"));
        CHECK(!there("copy/bin/xenogears.old") && !there("copy/bin/xenogears-enhanced"));
        CHECK(textIs("copy/share/xenogears-pc/release.txt", "version=2026.11.27\nkind=tarball\n"));
        CHECK(updateInstallSwap(&p, why, sizeof why) && textIs("copy/bin/xenogears", "new game")); /* again: done */
    }
#endif

    /* A swap a crash interrupted: finished at the next start. A prepare a crash interrupted: undone. */
    tarballCopy(&p);
    serve("2026.12.3", "tarball", "linux-x86_64", TARBALL_NAME, targz(NEW_TARBALL, 5, NULL));
    CHECK(updateInstallPrepare(&p, &st));
    {
        char list[1024], text[4096] = "";
        snprintf(list, sizeof list, "%sfiles.txt", p.downloads);
        size_t n = 0;
        char* t = (char*)SDL_LoadFile(list, &n);
        CHECK(t != NULL && strstr(t, "state=unpacked\n") != NULL);
        if (t != NULL) {
            snprintf(text, sizeof text, "%s", t);
            char* s = strstr(text, "state=unpacked");
            if (s != NULL) memcpy(s, "state=swapping", 14);
        }
        SDL_free(t);
        writeText("pref/update/files.txt", text); /* as if the swap had begun */
        char from[1024], to[1024]; /* the first file done, the crash came then */
        snprintf(from, sizeof from, "%scopy/bin/xenogears", work);
        snprintf(to, sizeof to, "%scopy/bin/xenogears.old", work);
        SDL_RenamePath(from, to);
        snprintf(to, sizeof to, "%scopy/bin/xenogears.new", work);
        SDL_RenamePath(to, from);
        updateInstallTidy(&p);
        CHECK(textIs("copy/bin/xenogears", "new game") && textIs("copy/bin/xenogears-enhanced", "new enhanced"));
        CHECK(textIs("copy/share/doc/xenogears-pc/COPYING", "new licence") && !there("copy/bin/xenogears.old"));
        CHECK(!there("pref/update/files.txt"));
    }
    tarballCopy(&p);
    CHECK(updateInstallPrepare(&p, &st));
    updateInstallTidy(&p); /* the game started again without finishing */
    CHECK(unchanged() && !there("pref/update/" TARBALL_NAME));

    /* A zip copy (Windows's layout, on any system). */
    {
        removeTree(work);
        SDL_CreateDirectory(work);
        writeText("copy/xenogears.exe", "old exe");
        writeText("copy/SDL3.dll", "old dll");
        writeText("copy/release.txt", "version=2026.11.27\nkind=zip\n");
        snprintf(p.root, sizeof p.root, "%scopy/", work);
        snprintf(p.exe, sizeof p.exe, "xenogears.exe");
        p.kind = RELEASE_ZIP;
        snprintf(p.platform, sizeof p.platform, "windows-x64");
        static const Item NEW_ZIP[] = {
            {"xenogears-pc-2026.12.3-windows-x64/xenogears.exe", "new exe", 0, 0},
            {"xenogears-pc-2026.12.3-windows-x64/SDL3.dll", "new dll", 0, 0},
            {"xenogears-pc-2026.12.3-windows-x64/release.txt", "version=2026.12.3\nkind=zip\n", 0, 0},
        };
        serve("2026.12.3", "zip", "windows-x64", "xenogears-pc-2026.12.3-windows-x64.zip", zip(NEW_ZIP, 3, 0));
        CHECK(updateInstallPrepare(&p, &st) && updateInstallSwap(&p, why, sizeof why));
        CHECK(textIs("copy/xenogears.exe", "new exe") && textIs("copy/xenogears.exe.old", "old exe"));
        CHECK(textIs("copy/SDL3.dll", "new dll") && textIs("copy/release.txt", "version=2026.12.3\nkind=zip\n"));
        updateInstallTidy(&p);
        CHECK(!there("copy/xenogears.exe.old") && !there("copy/SDL3.dll.old"));
    }

    /* A setup copy: the setup only downloaded and checked. */
    {
        tarballCopy(&p);
        p.kind = RELEASE_SETUP;
        snprintf(p.platform, sizeof p.platform, "windows-x64");
        Buf setup = {0};
        put(&setup, "MZ a setup", 10);
        serve("2026.12.3", "setup", "windows-x64", "xenogears-pc-2026.12.3-windows-x64-setup.exe", setup);
        CHECK(updateInstallPrepare(&p, &st) && st.stage == INSTALL_READY);
        CHECK(textIs("pref/update/xenogears-pc-2026.12.3-windows-x64-setup.exe", "MZ a setup"));
        CHECK(textIs("copy/bin/xenogears", "old game") && !there("pref/update/files.txt"));
    }

    /* On its thread: the progress, and a cancel. */
    tarballCopy(&p);
    serve("2026.12.3", "tarball", "linux-x86_64", TARBALL_NAME, targz(NEW_TARBALL, 5, NULL));
    CHECK(updateInstallStart(&p));
    UpdateInstallStatus now = updateInstallStatus();
    for (int i = 0; i < 500 && now.stage != INSTALL_READY && now.stage != INSTALL_FAILED; i++) {
        SDL_Delay(10);
        now = updateInstallStatus();
    }
    CHECK(now.stage == INSTALL_READY && strcmp(now.version, "2026.12.3") == 0 && now.done == now.total);
    CHECK(updateInstallFinish(why, sizeof why) && textIs("copy/bin/xenogears", "new game"));
    tarballCopy(&p);
    S.slow = 1;
    CHECK(updateInstallStart(&p));
    for (int i = 0; i < 200 && updateInstallStatus().done == 0; i++) SDL_Delay(5);
    CHECK(updateInstallStatus().stage == INSTALL_DOWNLOADING);
    updateInstallCancel();
    now = updateInstallStatus();
    for (int i = 0; i < 500 && now.stage != INSTALL_FAILED; i++) {
        SDL_Delay(10);
        now = updateInstallStatus();
    }
    CHECK(now.stage == INSTALL_FAILED && strstr(now.why, "cancelled") != NULL && unchanged());
    CHECK(!updateInstallFinish(why, sizeof why));
    S.slow = 0;
    removeTree(work);
}

/* ---- --unpack ---------------------------------------------------------------------------------------- */

static int listFile(void* user, const UpdateArchiveFile* f, char* why, size_t whySize) {
    (void)user, (void)why, (void)whySize;
    printf("%s %zu %08x %d\n", f->path, f->size, updateCrc32(0, f->data, f->size), f->executable);
    return 1;
}

int main(int argc, char** argv) {
    if (argc == 3 && strcmp(argv[1], "--unpack") == 0) {
        char top[256], why[256] = "";
        if (!updateArchiveReadFile(argv[2], listFile, NULL, top, sizeof top, why, sizeof why)) {
            printf("refused: %s\n", why);
            return 1;
        }
        printf("top %s\n", top);
        return 0;
    }
    if (argc != 1) {
        printf("usage: updateinstalltest [--unpack ARCHIVE]\n");
        return 2;
    }
    char* here = SDL_GetCurrentDirectory();
    snprintf(work, sizeof work, "%supdateinstalltest-work/", here != NULL ? here : "");
    SDL_free(here);
    keys();
    archiveTests();
    installTests();
    free(S.asset.b);
    if (failures) {
        printf("updateinstalltest: %d failure(s)\n", failures);
        return 1;
    }
    printf("updateinstalltest: ok\n");
    return 0;
}
