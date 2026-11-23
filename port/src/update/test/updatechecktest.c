/* The update check (update/update_check.c, xg-4oq.14.3), disc-free and with no
 * network: a fake fetcher serves manifests made and signed here with a FIXTURE
 * key (the seed of updatemanifesttest's: public, trusted by no build).
 *
 *   - the setting: ask (default), on, off and the words settings.ini takes;
 *   - a manifest judged: newer, the same, older, skipped, its switch off, a
 *     changed byte, another key's signature, the build's own keys (which do
 *     not trust the fixture), a signed text that is no manifest;
 *   - a whole check: both files from the base URL; a missing manifest or
 *     signature, a failure, a release published between the two requests;
 *   - the rules: no check in a development build, a headless run, while ask
 *     or off; once a day; the clock gone back; the question only while ask;
 *   - the background check: off and ask make no request at all; on makes two
 *     and keeps what it found (update_available) for the next start, where an
 *     older or skipped one is forgotten; Check now; skip this version;
 *   - the base URL: the latest release's, XG_UPDATE_URL (https only);
 *   - the process ending with a check under way.
 *
 *     updatechecktest                  the checks above
 *     updatechecktest --fetch URL      one GET through this system's fetcher (update_fetch.c)
 *     updatechecktest --check VERSION  the whole check of the latest release, as a copy of VERSION,
 *                                      with this build's keys (the network) */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* setenv, unsetenv under a strict -std= */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "monocypher-ed25519.h"
#include "system/release.h"
#include "system/settings.h"
#include "update/update_check.h"
#include "update/update_manifest.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* The C library's environment (getenv: settings.c, release.c) and SDL's copy of it (SDL_getenv:
 * update_check.c), which SDL takes once and never reads again. */
static void setVar(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value != NULL ? value : "");
#else
    if (value != NULL) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
    if (value != NULL) {
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value, true);
    } else {
        SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), name);
    }
}

static void hex(const char* s, uint8_t* out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

/* The FIXTURE key: SHA-256 of "xenogears-pc update manifest test key: public, trusted by no build"
 * (updatemanifesttest.c's), and a second one for "another key". */
static const char* const FIXTURE_SEED = "aab593a535cad866a4ee538be640cb87ea2ef32f54bdb19c54cb73ca5ca19f76";
static const char* const FIXTURE_KEY = "d3e48762aa9fecf41f07ca7fd729500385c2c62a7e9bf36c461bb0680ea52196";
static uint8_t secretKey[64], publicKey[32], otherSecret[64], otherPublic[32];
static uint8_t trusted[1][32];

static void keys(void) {
    uint8_t seed[32];
    hex(FIXTURE_SEED, seed, 32);
    crypto_ed25519_key_pair(secretKey, publicKey, seed);
    memset(seed, 0x5a, sizeof seed);
    crypto_ed25519_key_pair(otherSecret, otherPublic, seed);
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

/* A manifest as packaging/update_manifest.py writes one (one stand-in asset), and its signature line. */
static void manifest(char* out, size_t size, const char* version, int updates) {
    snprintf(out, size,
             "# xenogears-pc's update manifest (a test's)\n"
             "format=1\n"
             "version=%s\n"
             "page=https://github.com/EtherAura/xenogears-pc/releases/tag/v%s\n"
             "download=https://github.com/EtherAura/xenogears-pc/releases/download/v%s/\n"
             "updates=%s\n"
             "asset=tarball linux-x86_64 53 "
             "154628ca365a54c235257b1a6291d7e0d64fc4746d9e3901e27549dd4ffb488f00743347567dfa3d11eac4e92427b71e1b11254beeffae49909f8dfa795608f4"
             " xenogears-pc-%s-linux-x86_64.tar.gz\n",
             version, version, version, updates ? "on" : "off", version);
}

static void sign(const char* text, const uint8_t* sk, const uint8_t* pk, char* out, size_t size) {
    uint8_t sig[64];
    char b64[96];
    crypto_ed25519_sign(sig, sk, (const uint8_t*)text, strlen(text));
    base64(sig, 64, b64);
    snprintf(out, size, "ed25519 %02x%02x%02x%02x%02x%02x%02x%02x %s\n", pk[0], pk[1], pk[2], pk[3], pk[4], pk[5],
             pk[6], pk[7], b64);
}

/* ---- the fake fetcher ------------------------------------------------------------------------ */

static struct {
    int calls;
    char urls[8][640];
    char text[4096], sig[512];
    int textStatus, sigStatus; /* 0: served; -1: a failure; -2: not found */
    char badSigs;              /* signatures served wrong before the right one (a release between) */
    char wrongSig[512];
} F;

static long fake(const char* url, char* buf, size_t max, char* why, size_t whySize) {
    if (F.calls < 8) snprintf(F.urls[F.calls], sizeof F.urls[0], "%s", url);
    F.calls++;
    int isSig = strlen(url) > 4 && strcmp(url + strlen(url) - 4, ".sig") == 0;
    int status = isSig ? F.sigStatus : F.textStatus;
    if (status != 0) {
        snprintf(why, whySize, status == -2 ? "HTTP 404" : "Could not resolve host: example.invalid");
        return status;
    }
    const char* body = isSig ? F.sig : F.text;
    if (isSig && F.badSigs > 0) {
        F.badSigs--;
        body = F.wrongSig;
    }
    size_t n = strlen(body);
    if (n > max) return -1;
    memcpy(buf, body, n);
    return (long)n;
}

/* A request that takes a while (the end of the process meets it under way). */
static long slow(const char* url, char* buf, size_t max, char* why, size_t whySize) {
    SDL_Delay(150);
    return fake(url, buf, max, why, whySize);
}

static void serve(const char* version, int updates) {
    memset(&F, 0, sizeof F);
    manifest(F.text, sizeof F.text, version, updates);
    sign(F.text, secretKey, publicKey, F.sig, sizeof F.sig);
}

/* ---- the background check -------------------------------------------------------------------- */

static const UpdateStatus* finish(void) {
    const UpdateStatus* s = updateStatus();
    for (int i = 0; i < 500 && s->running; i++) {
        SDL_Delay(10);
        s = updateStatus();
    }
    return s;
}

static void useRelease(const char* path, const char* text) {
    if (text != NULL) {
        SDL_IOStream* io = SDL_IOFromFile(path, "wb");
        CHECK(io != NULL && SDL_WriteIO(io, text, strlen(text)) == strlen(text));
        if (io != NULL) SDL_CloseIO(io);
        setVar("XG_RELEASE_FILE", path);
    } else {
        setVar("XG_RELEASE_FILE", "none");
    }
    releaseForget();
    updateReset();
    updateTestWith(fake, NULL, 0); /* the build's own keys */
}

static int manual(int argc, char** argv) {
    const char* name = NULL;
    UpdateFetch fetch = updatePlatformFetch(&name);
    printf("fetcher: %s\n", name);
    if (fetch == NULL) return 1;
    if (strcmp(argv[1], "--fetch") == 0) {
        static char buf[UPDATE_MANIFEST_MAX + 1];
        char why[256] = "";
        long n = fetch(argv[2], buf, UPDATE_MANIFEST_MAX, why, sizeof why);
        if (n < 0) {
            printf("%s: %s%s\n", argv[2], why, n == -2 ? " (not found)" : "");
            return 1;
        }
        printf("%s: %ld bytes\n", argv[2], n);
        fwrite(buf, 1, (size_t)n, stdout);
        return 0;
    }
    UpdateStatus st;
    memset(&st, 0, sizeof st);
    UpdateOutcome o = updateCheckWith(fetch, updateBaseUrl(), NULL, 0, argv[2], NULL, &st);
    printf("check as %s, from %s: outcome %d: %s\n", argv[2], updateBaseUrl(), (int)o, st.why);
    (void)argc;
    return o == UPDATE_NEWER || o == UPDATE_LATEST || o == UPDATE_NOT_OFFERED ? 0 : 1;
}

int main(int argc, char** argv) {
    /* Nothing of the player's: no settings file, none of their XG_UPDATE_* overrides. */
    setVar("XG_SETTINGS", "none");
    static const char* const OURS[] = {"XG_UPDATE_CHECK", "XG_UPDATE_LAST", "XG_UPDATE_AVAILABLE", "XG_UPDATE_PAGE",
                                       "XG_UPDATE_SKIP", "XG_UPDATE_URL", "XG_UPDATE_SHOW", "XG_HEADLESS"};
    for (size_t i = 0; i < sizeof OURS / sizeof OURS[0]; i++) setVar(OURS[i], NULL);
    if (argc == 3 && (strcmp(argv[1], "--fetch") == 0 || strcmp(argv[1], "--check") == 0)) return manual(argc, argv);
    if (argc != 1) {
        printf("usage: updatechecktest [--fetch URL | --check VERSION]\n");
        return 2;
    }
    keys();
    {
        uint8_t want[32];
        hex(FIXTURE_KEY, want, 32);
        CHECK(memcmp(publicKey, want, 32) == 0); /* the seed is updatemanifesttest's key's */
    }

    /* The setting. */
    CHECK(updateSetting() == UPDATE_ASK);
    static const struct { const char* word; UpdateSetting s; } WORDS[] = {
        {"on", UPDATE_ON}, {"1", UPDATE_ON}, {"yes", UPDATE_ON}, {"off", UPDATE_OFF}, {"0", UPDATE_OFF},
        {"no", UPDATE_OFF}, {"ask", UPDATE_ASK}, {"Ask", UPDATE_ASK}, {"weekly", UPDATE_ASK},
    };
    for (size_t i = 0; i < sizeof WORDS / sizeof WORDS[0]; i++) {
        settingsSet("update_check", WORDS[i].word);
        CHECK(updateSetting() == WORDS[i].s);
    }
    updateSetSetting(UPDATE_OFF);
    CHECK(strcmp(settingsGet("update_check"), "off") == 0);
    updateSetSetting(UPDATE_ON);
    CHECK(strcmp(settingsGet("update_check"), "on") == 0);
    settingsUnset("update_check");

    /* A manifest judged. */
    char text[4096], sig[512], other[512];
    UpdateStatus st;
    memset(&st, 0, sizeof st);
    manifest(text, sizeof text, "2026.12.3", 1);
    sign(text, secretKey, publicKey, sig, sizeof sig);
    size_t n = strlen(text), sn = strlen(sig);
    CHECK(updateJudge(text, n, sig, sn, trusted, 1, "2026.11.27", NULL, &st) == UPDATE_NEWER);
    CHECK(strcmp(st.version, "2026.12.3") == 0);
    CHECK(strcmp(st.page, "https://github.com/EtherAura/xenogears-pc/releases/tag/v2026.12.3") == 0);
    CHECK(updateJudge(text, n, sig, sn, trusted, 1, "2026.12.3", NULL, &st) == UPDATE_LATEST);
    CHECK(updateJudge(text, n, sig, sn, trusted, 1, "2027.01.0", NULL, &st) == UPDATE_LATEST);
    CHECK(updateJudge(text, n, sig, sn, trusted, 1, "2026.11.27", "2026.12.3", &st) == UPDATE_SKIPPED);
    CHECK(updateJudge(text, n, sig, sn, trusted, 1, "2026.11.27", "2026.12.2", &st) == UPDATE_NEWER);
    CHECK(updateJudge(text, n, sig, sn, NULL, 0, "2026.11.27", NULL, &st) == UPDATE_UNTRUSTED); /* the build's keys */
    CHECK(strstr(st.why, "no key this build trusts") != NULL && st.version[0] == '\0');
    sign(text, otherSecret, otherPublic, other, sizeof other);
    CHECK(updateJudge(text, n, other, strlen(other), trusted, 1, "2026.11.27", NULL, &st) == UPDATE_UNTRUSTED);
    text[n - 3] ^= 1; /* a byte of the asset's name */
    CHECK(updateJudge(text, n, sig, sn, trusted, 1, "2026.11.27", NULL, &st) == UPDATE_UNTRUSTED);
    CHECK(strstr(st.why, "does not match") != NULL);
    manifest(text, sizeof text, "2026.12.3", 0);
    sign(text, secretKey, publicKey, sig, sizeof sig);
    CHECK(updateJudge(text, strlen(text), sig, strlen(sig), trusted, 1, "2026.11.27", NULL, &st) == UPDATE_NOT_OFFERED);
    CHECK(strcmp(st.version, "2026.12.3") == 0);
    snprintf(text, sizeof text, "format=2\nversion=2026.12.3\n");
    sign(text, secretKey, publicKey, sig, sizeof sig);
    CHECK(updateJudge(text, strlen(text), sig, strlen(sig), trusted, 1, "2026.11.27", NULL, &st) == UPDATE_BAD_MANIFEST);

    /* A whole check. */
    const char* base = "https://example.invalid/latest/";
    serve("2026.12.3", 1);
    CHECK(updateCheckWith(fake, base, trusted, 1, "2026.11.27", NULL, &st) == UPDATE_NEWER);
    CHECK(F.calls == 2);
    CHECK(strcmp(F.urls[0], "https://example.invalid/latest/xenogears-pc-update.txt") == 0);
    CHECK(strcmp(F.urls[1], "https://example.invalid/latest/xenogears-pc-update.txt.sig") == 0);
    serve("2026.12.3", 1);
    F.textStatus = -2;
    CHECK(updateCheckWith(fake, base, trusted, 1, "2026.11.27", NULL, &st) == UPDATE_NO_MANIFEST && F.calls == 1);
    serve("2026.12.3", 1);
    F.textStatus = -1;
    CHECK(updateCheckWith(fake, base, trusted, 1, "2026.11.27", NULL, &st) == UPDATE_FAILED && F.calls == 1);
    CHECK(strstr(st.why, "Could not resolve host") != NULL);
    serve("2026.12.3", 1);
    F.sigStatus = -2;
    CHECK(updateCheckWith(fake, base, trusted, 1, "2026.11.27", NULL, &st) == UPDATE_UNTRUSTED && F.calls == 4);
    serve("2026.12.3", 1);
    F.sigStatus = -1;
    CHECK(updateCheckWith(fake, base, trusted, 1, "2026.11.27", NULL, &st) == UPDATE_FAILED && F.calls == 2);
    serve("2026.12.3", 1);
    F.badSigs = 1; /* the next release's signature with this one's manifest, once */
    sign("another release's manifest", secretKey, publicKey, F.wrongSig, sizeof F.wrongSig);
    CHECK(updateCheckWith(fake, base, trusted, 1, "2026.11.27", NULL, &st) == UPDATE_NEWER && F.calls == 4);
    serve("2026.12.3", 1);
    F.badSigs = 2;
    sign("another release's manifest", secretKey, publicKey, F.wrongSig, sizeof F.wrongSig);
    CHECK(updateCheckWith(fake, base, trusted, 1, "2026.11.27", NULL, &st) == UPDATE_UNTRUSTED && F.calls == 4);
    CHECK(updateCheckWith(NULL, base, trusted, 1, "2026.11.27", NULL, &st) == UPDATE_NO_FETCHER);

    /* The rules, in a release (a tarball). */
    const char* path = "updatechecktest-release.txt";
    useRelease(path, "version=2026.11.27\nkind=tarball\n");
    long long now = (long long)time(NULL);
    char when[32];
    CHECK(updateBlocked() == NULL);
    CHECK(updateShouldAsk() && !updateDue(now)); /* ask: the question, no check */
    settingsSet("update_check", "off");
    CHECK(!updateShouldAsk() && !updateDue(now));
    settingsSet("update_check", "on");
    CHECK(!updateShouldAsk() && updateDue(now)); /* never checked */
    snprintf(when, sizeof when, "%lld", now - 100);
    settingsSet("update_last", when);
    CHECK(!updateDue(now));
    snprintf(when, sizeof when, "%lld", now - UPDATE_DAY);
    settingsSet("update_last", when);
    CHECK(updateDue(now));
    snprintf(when, sizeof when, "%lld", now + 1000); /* the clock went back */
    settingsSet("update_last", when);
    CHECK(updateDue(now));
    setVar("XG_HEADLESS", "1");
    CHECK(updateBlocked() != NULL && !updateDue(now));
    settingsSet("update_check", "ask");
    CHECK(!updateShouldAsk());
    setVar("XG_HEADLESS", NULL);
    useRelease(path, NULL); /* a development build */
    CHECK(strstr(updateBlocked(), "development") != NULL && !updateShouldAsk());
    settingsSet("update_check", "on");
    settingsUnset("update_last");
    CHECK(!updateDue(now));
    serve("2026.12.3", 1);
    CHECK(!updateStart(0) && !updateStart(1) && F.calls == 0);

    /* The background check: no request while off or ask. The fake's manifest is signed by the
     * fixture key, which the build does not trust: these checks end untrusted, which is enough to
     * count requests and see what is kept; what is found is judged above. */
    useRelease(path, "version=2026.11.27\nkind=tarball\n");
    serve("2026.12.3", 1);
    settingsUnset("update_last");
    settingsSet("update_check", "off");
    CHECK(!updateStart(0) && !updateStart(1));
    settingsSet("update_check", "ask");
    CHECK(!updateStart(0) && !updateStart(1));
    for (int v = 0; v < 2 * 3600 + 10; v++) updateVBlank(); /* the vblank's minute look, twice */
    CHECK(F.calls == 0 && settingsGet("update_last") == NULL);
    CHECK(updateStatus()->outcome == UPDATE_UNCHECKED);
    settingsSet("update_check", "on");
    CHECK(updateStart(0));
    const UpdateStatus* s = finish();
    CHECK(!s->running && s->outcome == UPDATE_UNTRUSTED && F.calls == 4); /* both, and once more */
    CHECK(settingsGet("update_last") != NULL && s->checkedAt > 0 && s->available[0] == '\0');
    CHECK(!updateStart(0) && F.calls == 4); /* a day to the next */
    for (int v = 0; v < 3600 + 10; v++) updateVBlank();
    CHECK(F.calls == 4);
    CHECK(updateStart(1)); /* Check now */
    s = finish();
    CHECK(F.calls == 8 && s->forced);
    serve("2026.12.3", 1);
    F.textStatus = -2;
    CHECK(updateStart(1));
    s = finish();
    CHECK(s->outcome == UPDATE_NO_MANIFEST && F.calls == 1);

    /* The background check finding a version, with the fixture key trusted: kept for the next start
     * (update_available, update_page); a later check that finds this copy the latest, the switch off
     * or the version skipped forgets it. */
    updateTestWith(fake, (const uint8_t (*)[32])trusted, 1);
    serve("2026.12.3", 1);
    CHECK(updateStart(1));
    s = finish();
    CHECK(s->outcome == UPDATE_NEWER && F.calls == 2 && strcmp(s->available, "2026.12.3") == 0);
    CHECK(strcmp(s->availablePage, "https://github.com/EtherAura/xenogears-pc/releases/tag/v2026.12.3") == 0);
    CHECK(strcmp(settingsGet("update_available"), "2026.12.3") == 0);
    CHECK(strcmp(settingsGet("update_page"), s->availablePage) == 0);
    serve("2026.11.27", 1);
    CHECK(updateStart(1));
    s = finish();
    CHECK(s->outcome == UPDATE_LATEST && s->available[0] == '\0' && settingsGet("update_available") == NULL);
    serve("2026.12.3", 1);
    CHECK(updateStart(1) && finish()->available[0] != '\0');
    serve("2026.12.3", 0);
    CHECK(updateStart(1));
    s = finish();
    CHECK(s->outcome == UPDATE_NOT_OFFERED && s->available[0] == '\0' && settingsGet("update_page") == NULL);
    settingsSet("update_skip", "2026.12.3");
    serve("2026.12.3", 1);
    CHECK(updateStart(1));
    s = finish();
    CHECK(s->outcome == UPDATE_SKIPPED && s->available[0] == '\0');
    settingsUnset("update_skip");
    updateTestWith(fake, NULL, 0);

    /* What an earlier check found, at the next start: kept while newer, forgotten when not, or skipped. */
    settingsSet("update_available", "2026.12.3");
    settingsSet("update_page", "https://github.com/EtherAura/xenogears-pc/releases/tag/v2026.12.3");
    updateReset();
    s = updateStatus();
    CHECK(strcmp(s->available, "2026.12.3") == 0 && strstr(s->availablePage, "/tag/v2026.12.3") != NULL);
    serve("2026.12.3", 1);
    F.textStatus = -1;
    CHECK(updateStart(1));
    s = finish();
    CHECK(s->outcome == UPDATE_FAILED && strcmp(s->available, "2026.12.3") == 0); /* a failure keeps it */
    updateSkip(updateStatus()->available); /* as the menu calls it: the status's own string */
    CHECK(updateStatus()->available[0] == '\0' && settingsGet("update_available") == NULL);
    CHECK(strcmp(settingsGet("update_skip"), "2026.12.3") == 0);
    settingsSet("update_available", "2026.12.3");
    updateReset();
    CHECK(updateStatus()->available[0] == '\0' && settingsGet("update_available") == NULL); /* skipped */
    settingsUnset("update_skip");
    settingsSet("update_available", "2026.11.27"); /* this version: installed meanwhile */
    updateReset();
    CHECK(updateStatus()->available[0] == '\0' && settingsGet("update_available") == NULL);
    settingsSet("update_available", "2026.12.4");
    settingsSet("update_page", "http://example.invalid/"); /* not https: the tag's page instead */
    updateReset();
    CHECK(strcmp(updateStatus()->availablePage, "https://github.com/EtherAura/xenogears-pc/releases/tag/v2026.12.4") == 0);
    setVar("XG_UPDATE_SHOW", "2027.01.0");
    updateReset();
    CHECK(strcmp(updateStatus()->available, "2027.01.0") == 0);
    setVar("XG_UPDATE_SHOW", NULL);

    /* The base URL. */
    updateReset();
    CHECK(strcmp(updateBaseUrl(), UPDATE_DEFAULT_URL) == 0);
    CHECK(strcmp(UPDATE_DEFAULT_URL, "https://github.com/EtherAura/xenogears-pc/releases/latest/download/") == 0);
    setVar("XG_UPDATE_URL", "https://example.invalid/u");
    updateReset();
    CHECK(strcmp(updateBaseUrl(), "https://example.invalid/u/") == 0);
    setVar("XG_UPDATE_URL", "http://example.invalid/u/");
    updateReset();
    CHECK(strcmp(updateBaseUrl(), UPDATE_DEFAULT_URL) == 0);
    setVar("XG_UPDATE_URL", NULL);

    /* The process ending with a check under way (quit.c): stopped and waited for, or let go. */
    updateReset();
    updateTestWith(slow, (const uint8_t (*)[32])trusted, 1);
    serve("2026.12.3", 1);
    settingsSet("update_check", "on");
    CHECK(updateStart(1));
    Uint64 from = SDL_GetTicks();
    updateShutdown();
    CHECK(SDL_GetTicks() - from < 2000);
    SDL_Delay(500); /* a thread let go (Windows) finishes before the process ends */
    updateTestWith(NULL, NULL, 0);

    remove(path);
    if (failures) {
        printf("updatechecktest: %d failure(s)\n", failures);
        return 1;
    }
    printf("updatechecktest: ok\n");
    return 0;
}
