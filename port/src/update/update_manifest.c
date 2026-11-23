/*
 * A release's update manifest and its signature (update_manifest.h).
 */
#include "update/update_manifest.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "monocypher-ed25519.h"

/* The release key's public half (packaging/update_manifest.py public): the
 * one key whose signature makes a manifest trusted. None yet: until the key
 * is made (xg-4oq.14.2) no manifest is trusted, so nothing is ever offered. */
static const uint8_t TRUSTED_KEYS[][32] = {{0}};
static const int TRUSTED_COUNT = 0;

int updateTrustedKeyCount(void) {
    return TRUSTED_COUNT;
}

__attribute__((format(printf, 3, 4))) static void say(char* why, size_t size, const char* fmt, ...) {
    if (why == NULL || size == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, size, fmt, ap);
    va_end(ap);
}

/* ---- the manifest ---------------------------------------------------------------------------- */

static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1; /* lower case only, as the tool writes it */
}

static int hexBytes(const char* text, size_t digits, uint8_t* out) {
    for (size_t i = 0; i < digits / 2; i++) {
        int hi = hexDigit(text[2 * i]), lo = hexDigit(text[2 * i + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (uint8_t)(hi * 16 + lo);
    }
    return 1;
}

static int nameOk(const char* s) {
    if (s[0] == '\0' || s[0] == '.') return 0;
    for (const char* p = s; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
              c == '-' || c == '+'))
            return 0;
    }
    return 1;
}

static int urlOk(const char* s) {
    if (strncmp(s, "https://", 8) != 0 || s[8] == '\0') return 0;
    for (const char* p = s; *p; p++)
        if ((unsigned char)*p <= ' ' || (unsigned char)*p >= 0x7F || *p == '"' || *p == '\\') return 0;
    return 1;
}

/* "kind platform size sha512 name" */
static int parseAsset(char* value, UpdateAsset* a, char* why, size_t whySize) {
    char* field[5];
    int n = 0;
    for (char* p = strtok(value, " "); p != NULL && n < 6; p = strtok(NULL, " ")) {
        if (n == 5) {
            say(why, whySize, "an asset line has more than five fields");
            return 0;
        }
        field[n++] = p;
    }
    if (n != 5) {
        say(why, whySize, "an asset line has %s fields, not five", n == 0 ? "no" : "too few");
        return 0;
    }
    a->kind = RELEASE_DEV;
    for (int k = RELEASE_DEV + 1; k < RELEASE_KINDS; k++)
        if (strcmp(field[0], releaseKindName((ReleaseKind)k)) == 0) a->kind = (ReleaseKind)k;
    if (a->kind == RELEASE_DEV) {
        say(why, whySize, "an asset of an unknown kind (%s)", field[0]);
        return 0;
    }
    if (strlen(field[1]) >= sizeof a->platform || !nameOk(field[1])) {
        say(why, whySize, "an asset's platform is not a name (%.40s)", field[1]);
        return 0;
    }
    snprintf(a->platform, sizeof a->platform, "%s", field[1]);
    size_t digits = strspn(field[2], "0123456789");
    if (digits == 0 || digits > 15 || field[2][digits] != '\0' || (digits > 1 && field[2][0] == '0')) {
        say(why, whySize, "an asset's size is not a number of bytes (%.20s)", field[2]);
        return 0;
    }
    a->size = strtoull(field[2], NULL, 10);
    if (strlen(field[3]) != 128 || !hexBytes(field[3], 128, a->sha512)) {
        say(why, whySize, "an asset's SHA-512 is not 128 lower-case hex digits");
        return 0;
    }
    if (strlen(field[4]) >= sizeof a->name || !nameOk(field[4])) {
        say(why, whySize, "an asset's file name is not one (%.60s)", field[4]);
        return 0;
    }
    snprintf(a->name, sizeof a->name, "%s", field[4]);
    return 1;
}

int updateManifestParse(const char* text, size_t n, UpdateManifest* m, char* why, size_t whySize) {
    memset(m, 0, sizeof *m);
    m->format = -1;
    m->updates = -1;
    if (text == NULL || n == 0 || n > UPDATE_MANIFEST_MAX) {
        say(why, whySize, "empty, or longer than a manifest");
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == 0 || c >= 0x80 || (c < ' ' && c != '\n' && c != '\r' && c != '\t')) {
            say(why, whySize, "not a text file (byte %s)", c == 0 ? "0" : "out of range");
            return 0;
        }
    }
    char* copy = malloc(n + 1);
    if (copy == NULL) {
        say(why, whySize, "out of memory");
        return 0;
    }
    memcpy(copy, text, n);
    copy[n] = '\0';
    int ok = 1;
    for (char *line = copy, *next; ok && line != NULL; line = next) {
        next = strchr(line, '\n');
        if (next != NULL) *next++ = '\0';
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';
        if (len == 0 || line[0] == '#') continue;
        char* eq = strchr(line, '=');
        if (eq == NULL) {
            say(why, whySize, "a line is not key=value (%.40s)", line);
            ok = 0;
            break;
        }
        *eq = '\0';
        const char* key = line;
        char* value = eq + 1;
        if (strcmp(key, "format") == 0) {
            if (m->format != -1 || strcmp(value, "1") != 0) {
                say(why, whySize, "format %.20s, or twice: this build reads format 1", value);
                ok = 0;
            } else {
                m->format = 1;
            }
        } else if (strcmp(key, "version") == 0) {
            if (m->version[0] != '\0' || !releaseVersionValid(value)) {
                say(why, whySize, "version %.20s is not a release's, or given twice", value);
                ok = 0;
            } else {
                snprintf(m->version, sizeof m->version, "%s", value);
            }
        } else if (strcmp(key, "page") == 0 || strcmp(key, "download") == 0) {
            char* dest = key[0] == 'p' ? m->page : m->download;
            if (dest[0] != '\0' || strlen(value) >= sizeof m->page || !urlOk(value)) {
                say(why, whySize, "%s is not an https address, or given twice", key);
                ok = 0;
            } else {
                snprintf(dest, sizeof m->page, "%s", value);
            }
        } else if (strcmp(key, "updates") == 0) {
            if (m->updates != -1 || (strcmp(value, "on") != 0 && strcmp(value, "off") != 0)) {
                say(why, whySize, "updates is %.10s, not on or off, or given twice", value);
                ok = 0;
            } else {
                m->updates = strcmp(value, "on") == 0;
            }
        } else if (strcmp(key, "asset") == 0) {
            if (m->assets == UPDATE_ASSETS_MAX) {
                say(why, whySize, "more assets than a manifest holds");
                ok = 0;
            } else if (parseAsset(value, &m->asset[m->assets], why, whySize)) {
                m->assets++;
            } else {
                ok = 0;
            }
        } /* other keys: a later format's */
    }
    free(copy);
    if (!ok) return 0;
    if (m->format != 1 || m->version[0] == '\0' || m->page[0] == '\0' || m->updates == -1 ||
        (m->assets > 0 && m->download[0] == '\0')) {
        say(why, whySize, "no %s line", m->format != 1 ? "format" : m->version[0] == '\0' ? "version"
                                         : m->page[0] == '\0'  ? "page" : m->updates == -1 ? "updates" : "download");
        return 0;
    }
    for (int i = 0; i < m->assets; i++)
        for (int j = 0; j < i; j++)
            if (strcmp(m->asset[i].name, m->asset[j].name) == 0 ||
                (m->asset[i].kind == m->asset[j].kind && strcmp(m->asset[i].platform, m->asset[j].platform) == 0)) {
                say(why, whySize, "two assets of one name, or of one kind and platform (%s)", m->asset[i].name);
                return 0;
            }
    return 1;
}

const UpdateAsset* updateManifestAsset(const UpdateManifest* m, ReleaseKind kind, const char* platform) {
    for (int i = 0; i < m->assets; i++)
        if (m->asset[i].kind == kind && strcmp(m->asset[i].platform, platform) == 0) return &m->asset[i];
    return NULL;
}

/* ---- the signature --------------------------------------------------------------------------- */

static int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* A 64-byte signature is 88 base64 characters, the last two "==". */
static int signatureBytes(const char* b64, uint8_t out[64]) {
    if (strlen(b64) != 88 || b64[86] != '=' || b64[87] != '=') return 0;
    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    for (int i = 0; i < 86; i++) {
        int v = base64Value(b64[i]);
        if (v < 0) return 0;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o < 64) out[o] = (uint8_t)(acc >> bits);
            o++;
        }
    }
    /* 86 characters are 516 bits: 64 bytes and 4 bits that must be zero */
    return o == 64 && (acc & ((1u << bits) - 1)) == 0;
}

int updateSignatureCheck(const uint8_t* text, size_t n, const char* sig, size_t sigSize,
                         const uint8_t (*keys)[32], int keyCount, char* why, size_t whySize) {
    if (sig == NULL || sigSize == 0 || sigSize > UPDATE_SIGNATURE_MAX || memchr(sig, '\0', sigSize) != NULL) {
        say(why, whySize, "the signature file is empty, too long or binary");
        return 0;
    }
    if (keyCount <= 0) {
        say(why, whySize, "this build trusts no release key yet");
        return 0;
    }
    char copy[UPDATE_SIGNATURE_MAX + 1];
    memcpy(copy, sig, sigSize);
    copy[sigSize] = '\0';
    int lines = 0, known = 0;
    for (char *line = copy, *next; line != NULL; line = next) {
        next = strchr(line, '\n');
        if (next != NULL) *next++ = '\0';
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';
        if (len == 0) continue;
        lines++;
        char scheme[16], id[32], b64[128];
        uint8_t idBytes[8], signature[64];
        if (sscanf(line, "%15s %31s %127s", scheme, id, b64) != 3 || strcmp(scheme, "ed25519") != 0 ||
            strlen(id) != 16 || !hexBytes(id, 16, idBytes) || !signatureBytes(b64, signature))
            continue; /* a line of another scheme or form: a later build's */
        for (int k = 0; k < keyCount; k++) {
            if (memcmp(keys[k], idBytes, 8) != 0) continue;
            known++;
            if (crypto_ed25519_check(signature, keys[k], text, n) == 0) return 1;
        }
    }
    if (lines == 0) {
        say(why, whySize, "the signature file holds no signature");
    } else if (known == 0) {
        say(why, whySize, "signed by no key this build trusts");
    } else {
        say(why, whySize, "the signature does not match the manifest");
    }
    return 0;
}

int updateSignatureTrusted(const uint8_t* text, size_t n, const char* sig, size_t sigSize, char* why,
                           size_t whySize) {
    return updateSignatureCheck(text, n, sig, sigSize, TRUSTED_KEYS, TRUSTED_COUNT, why, whySize);
}

/* ---- versions and platforms ------------------------------------------------------------------ */

const char* updatePlatform(void) {
#if defined(__ANDROID__) && defined(__aarch64__)
    return "android-arm64-v8a";
#elif defined(__ANDROID__) && defined(__x86_64__)
    return "android-x86_64";
#elif defined(_WIN32) && defined(__x86_64__)
    return "windows-x64";
#elif defined(__linux__) && defined(__x86_64__)
    return "linux-x86_64";
#else
    return "";
#endif
}

static void versionParts(const char* v, unsigned long part[3]) {
    char* end;
    part[0] = strtoul(v, &end, 10);
    part[1] = strtoul(end + 1, &end, 10);
    part[2] = strtoul(end + 1, NULL, 10);
}

int updateVersionCompare(const char* a, const char* b) {
    unsigned long x[3], y[3];
    versionParts(a, x);
    versionParts(b, y);
    for (int i = 0; i < 3; i++)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
