/* The update check (update_check.h, xg-4oq.14.3): the setting, the daily rule,
 * the judgement of a fetched manifest, and the one background thread that
 * fetches it. Everything but the thread's fetch and judgement runs on the game's
 * thread (the vblank, the menu); the thread touches only its own copy of what it
 * needs and its result, handed over through U.done. */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "update_check.h"
#include "update_manifest.h"
#include "system/release.h"
#include "system/settings.h"

/* system/netplay.c, gpu/present.c (weak: the tests have neither). */
int netplayActive(void) __attribute__((weak));
void presentShowNote(const char* text) __attribute__((weak));

#define REPO_TAG_PAGE "https://github.com/EtherAura/xenogears-pc/releases/tag/v"
#define LOOK_VBLANKS 3600 /* the daily rule looked at about once a minute in a long session */

static struct {
    int inited;
    UpdateStatus status;  /* the game thread's */
    UpdateStatus pending; /* the check thread's, until done */
    SDL_Thread* thread;
    SDL_AtomicInt done;
    UpdateFetch fetch;
    char base[512], current[16], skip[16];
    unsigned look; /* vblanks until the daily rule is looked at again */
    char url[512];
} U;
static struct {
    UpdateFetch fetch;
    const uint8_t (*keys)[32];
    int keyCount;
} T; /* updateTestWith */

static void copy(char* dst, size_t size, const char* src) {
    snprintf(dst, size, "%s", src != NULL ? src : "");
}

/* ---- the setting ------------------------------------------------------------------------------ */

UpdateSetting updateSetting(void) {
    const char* v = settingsGet("update_check");
    if (v == NULL || SDL_strcasecmp(v, "ask") == 0) return UPDATE_ASK;
    int on = settingsBool("update_check", -1);
    return on == 1 ? UPDATE_ON : on == 0 ? UPDATE_OFF : UPDATE_ASK;
}

void updateSetSetting(UpdateSetting s) {
    static const char* const WORDS[] = {"ask", "on", "off"};
    settingsDescribe("update_check", "update_check: on checks for a new version of the port once a day (two small "
                                     "files from GitHub, nothing about you sent), off never, ask (default) asks "
                                     "before the game starts");
    settingsSet("update_check", WORDS[s]);
    settingsSave();
}

/* ---- the judgement ---------------------------------------------------------------------------- */

#ifdef XG_UPDATER_TEST
/* A test build of the updater (-DXG_UPDATER_TEST=ON, never a release): the tests' FIXTURE key is
 * trusted too (updatemanifesttest's; its seed is public: SHA-256 of "xenogears-pc update manifest
 * test key: public, trusted by no build"), so an install can be run end to end on files made here. */
static const uint8_t TEST_KEY[1][32] = {{
    0xd3, 0xe4, 0x87, 0x62, 0xaa, 0x9f, 0xec, 0xf4, 0x1f, 0x07, 0xca, 0x7f, 0xd7, 0x29, 0x50, 0x03,
    0x85, 0xc2, 0xc6, 0x2a, 0x7e, 0x9b, 0xf3, 0x6c, 0x46, 0x1b, 0xb0, 0x68, 0x0e, 0xa5, 0x21, 0x96,
}};
#endif

static UpdateOutcome judge(const char* text, size_t n, const char* sig, size_t sigSize, const uint8_t (*keys)[32],
                           int keyCount, const char* current, const char* skip, UpdateStatus* st, UpdateManifest* out) {
    char why[200] = "";
    st->version[0] = st->page[0] = st->why[0] = '\0';
    int trusted = keys != NULL
                      ? updateSignatureCheck((const uint8_t*)text, n, sig, sigSize, keys, keyCount, why, sizeof why)
                      : updateSignatureTrusted((const uint8_t*)text, n, sig, sigSize, why, sizeof why);
#ifdef XG_UPDATER_TEST
    if (!trusted && keys == NULL) {
        trusted = updateSignatureCheck((const uint8_t*)text, n, sig, sigSize, TEST_KEY, 1, why, sizeof why);
    }
#endif
    if (!trusted) {
        snprintf(st->why, sizeof st->why, "the manifest is not trusted: %s", why);
        return st->outcome = UPDATE_UNTRUSTED;
    }
    UpdateManifest m;
    if (!updateManifestParse(text, n, &m, why, sizeof why)) {
        snprintf(st->why, sizeof st->why, "the manifest is not one this build reads: %s", why);
        return st->outcome = UPDATE_BAD_MANIFEST;
    }
    copy(st->version, sizeof st->version, m.version);
    copy(st->page, sizeof st->page, m.page);
    if (out != NULL) *out = m;
    if (!m.updates) {
        snprintf(st->why, sizeof st->why, "the latest release, %s, offers no updates yet", m.version);
        return st->outcome = UPDATE_NOT_OFFERED;
    }
    if (updateVersionCompare(m.version, current) <= 0) {
        snprintf(st->why, sizeof st->why, "%s is the latest release (this is %s)", m.version, current);
        return st->outcome = UPDATE_LATEST;
    }
    if (skip != NULL && strcmp(skip, m.version) == 0) {
        snprintf(st->why, sizeof st->why, "%s is out, and skipped (this is %s)", m.version, current);
        return st->outcome = UPDATE_SKIPPED;
    }
    snprintf(st->why, sizeof st->why, "%s is out (this is %s)", m.version, current);
    return st->outcome = UPDATE_NEWER;
}

UpdateOutcome updateJudge(const char* text, size_t n, const char* sig, size_t sigSize, const uint8_t (*keys)[32],
                          int keyCount, const char* current, const char* skip, UpdateStatus* st) {
    return judge(text, n, sig, sigSize, keys, keyCount, current, skip, st, NULL);
}

/* One file: its size, or the outcome's negative (st's why set). */
static long fetchOne(UpdateFetch fetch, const char* base, const char* name, char* buf, size_t max, UpdateStatus* st) {
    char url[640], why[200] = "";
    snprintf(url, sizeof url, "%s%s", base, name);
    long n = fetch(url, buf, max, why, sizeof why);
    if (n == -2) {
        snprintf(st->why, sizeof st->why, "the latest release has no %s (%.160s: not found)", name, url);
        return -UPDATE_NO_MANIFEST;
    }
    if (n < 0 || (size_t)n > max) {
        snprintf(st->why, sizeof st->why, "%.120s: %.120s", url,
                 n >= 0 ? "longer than asked for" : why[0] ? why : "the request failed");
        return -UPDATE_FAILED;
    }
    buf[n] = '\0';
    return n;
}

UpdateOutcome updateCheckManifest(UpdateFetch fetch, const char* base, const uint8_t (*keys)[32], int keyCount,
                                  const char* current, const char* skip, UpdateStatus* st, UpdateManifest* out) {
    st->version[0] = st->page[0] = '\0';
    if (fetch == NULL) {
        snprintf(st->why, sizeof st->why, "nothing to fetch with");
        return st->outcome = UPDATE_NO_FETCHER;
    }
    char* text = malloc(UPDATE_MANIFEST_MAX + 1);
    char* sig = malloc(UPDATE_SIGNATURE_MAX + 1);
    if (text == NULL || sig == NULL) {
        free(text);
        free(sig);
        snprintf(st->why, sizeof st->why, "out of memory");
        return st->outcome = UPDATE_FAILED;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        long n = fetchOne(fetch, base, UPDATE_MANIFEST_NAME, text, UPDATE_MANIFEST_MAX, st);
        if (n < 0) {
            st->outcome = (UpdateOutcome)-n;
            break;
        }
        long sn = fetchOne(fetch, base, UPDATE_SIGNATURE_NAME, sig, UPDATE_SIGNATURE_MAX, st);
        if (sn < 0) {
            st->outcome = sn == -UPDATE_NO_MANIFEST ? UPDATE_UNTRUSTED : UPDATE_FAILED;
        } else {
            judge(text, (size_t)n, sig, (size_t)sn, keys, keyCount, current, skip, st, out);
        }
        if (st->outcome != UPDATE_UNTRUSTED) break;
        /* A release published between the two requests pairs one release's manifest with the next one's
         * signature: both once more. */
    }
    free(text);
    free(sig);
    return st->outcome;
}

UpdateOutcome updateCheckWith(UpdateFetch fetch, const char* base, const uint8_t (*keys)[32], int keyCount,
                              const char* current, const char* skip, UpdateStatus* st) {
    return updateCheckManifest(fetch, base, keys, keyCount, current, skip, st, NULL);
}

/* ---- the rules -------------------------------------------------------------------------------- */

const char* updateBlocked(void) {
    if (releaseKind() == RELEASE_DEV) return "a development build, which never checks";
    if (SDL_getenv("XG_HEADLESS") != NULL) return "a headless run";
    if (netplayActive != NULL && netplayActive()) return "an online session";
    return NULL;
}

static long long lastCheck(void) {
    const char* v = settingsGet("update_last");
    return v != NULL ? strtoll(v, NULL, 10) : 0;
}

int updateDue(long long now) {
    if (updateSetting() != UPDATE_ON || updateBlocked() != NULL) return 0;
    long long last = lastCheck();
    return last <= 0 || now < last || now - last >= UPDATE_DAY;
}

int updateShouldAsk(void) {
    if (SDL_getenv("XG_UPDATE_ASK") != NULL) return 1; /* the question's screenshots */
    return updateSetting() == UPDATE_ASK && updateBlocked() == NULL &&
           (T.fetch != NULL || updatePlatformFetch(NULL) != NULL);
}

const char* updateBaseUrl(void) {
    if (U.url[0] != '\0') return U.url;
    const char* env = SDL_getenv("XG_UPDATE_URL");
    if (env != NULL && SDL_strncmp(env, "https://", 8) == 0 && strlen(env) + 2 < sizeof U.url) {
        size_t n = strlen(env);
        snprintf(U.url, sizeof U.url, "%s%s", env, env[n - 1] == '/' ? "" : "/");
    } else {
        if (env != NULL) fprintf(stderr, "update: XG_UPDATE_URL is not an https URL; the latest release's instead\n");
        snprintf(U.url, sizeof U.url, "%s", UPDATE_DEFAULT_URL);
    }
    return U.url;
}

/* ---- the state -------------------------------------------------------------------------------- */

static void forgetAvailable(void) {
    U.status.available[0] = U.status.availablePage[0] = '\0';
    if (settingsGet("update_available") != NULL || settingsGet("update_page") != NULL) {
        settingsUnset("update_available");
        settingsUnset("update_page");
    }
}

static void tagPage(char* out, size_t size, const char* version) {
    snprintf(out, size, REPO_TAG_PAGE "%s", version);
}

static void init(void) {
    if (U.inited) return;
    U.inited = 1;
    U.status.checkedAt = lastCheck();
    const char* show = SDL_getenv("XG_UPDATE_SHOW");
    if (show != NULL && releaseVersionValid(show)) {
        copy(U.status.available, sizeof U.status.available, show);
        tagPage(U.status.availablePage, sizeof U.status.availablePage, show);
        return;
    }
    /* What an earlier check found: while this copy is older, and not skipped. */
    const char* known = settingsGet("update_available");
    if (known == NULL) return;
    const char* skip = settingsGet("update_skip");
    const char* page = settingsGet("update_page");
    if (!releaseVersionValid(known) || releaseKind() == RELEASE_DEV ||
        updateVersionCompare(known, releaseVersion()) <= 0 || (skip != NULL && strcmp(skip, known) == 0)) {
        forgetAvailable();
        settingsSave();
        return;
    }
    copy(U.status.available, sizeof U.status.available, known);
    if (page != NULL && SDL_strncmp(page, "https://", 8) == 0) {
        copy(U.status.availablePage, sizeof U.status.availablePage, page);
    } else {
        tagPage(U.status.availablePage, sizeof U.status.availablePage, known);
    }
}

void updateReset(void) {
    if (U.thread != NULL) SDL_WaitThread(U.thread, NULL);
    memset(&U, 0, sizeof U);
}

void updateTestWith(UpdateFetch fetch, const uint8_t (*keys)[32], int keyCount) {
    T.fetch = fetch;
    T.keys = keys;
    T.keyCount = keyCount;
}

/* A finished check taken in: the settings, the log, the note. */
static void apply(const UpdateStatus* r) {
    UpdateStatus* s = &U.status;
    s->outcome = r->outcome;
    s->running = 0;
    s->forced = r->forced;
    s->checkedAt = r->checkedAt;
    copy(s->version, sizeof s->version, r->version);
    copy(s->page, sizeof s->page, r->page);
    copy(s->why, sizeof s->why, r->why);
    switch (r->outcome) {
    case UPDATE_NEWER:
        copy(s->available, sizeof s->available, r->version);
        if (SDL_strncmp(r->page, "https://", 8) == 0) {
            copy(s->availablePage, sizeof s->availablePage, r->page);
        } else {
            tagPage(s->availablePage, sizeof s->availablePage, r->version);
        }
        settingsDescribe("update_available", "update_available, update_page: a newer version the update check "
                                             "found, and its release page (shown in F1 > System > Updates)");
        settingsSet("update_available", s->available);
        settingsSet("update_page", s->availablePage);
        if (!r->forced && presentShowNote != NULL) {
            char note[96];
            snprintf(note, sizeof note, "Xenogears PC port %s is out: F1 > System > Updates", r->version);
            presentShowNote(note);
        }
        break;
    case UPDATE_LATEST:
    case UPDATE_NOT_OFFERED:
    case UPDATE_SKIPPED:
        forgetAvailable();
        break;
    default:
        break; /* a failure: what was known stays */
    }
    settingsSave();
    fprintf(stderr, "update: %s\n", r->why);
}

static void take(void) {
    if (U.thread == NULL || !SDL_GetAtomicInt(&U.done)) return;
    SDL_WaitThread(U.thread, NULL);
    U.thread = NULL;
    apply(&U.pending);
}

static int SDLCALL checkThread(void* unused) {
    (void)unused;
    updateCheckWith(U.fetch, U.base, T.keys, T.keyCount, U.current, U.skip[0] ? U.skip : NULL, &U.pending);
    SDL_SetAtomicInt(&U.done, 1);
    return 0;
}

int updateStart(int force) {
    init();
    take();
    if (U.thread != NULL) return 0;
    long long now = (long long)time(NULL);
    if (force ? updateSetting() != UPDATE_ON || updateBlocked() != NULL : !updateDue(now)) return 0;
    settingsDescribe("update_last", "update_last: when the last update check started (seconds since 1970, UTC); "
                                    "the next is due a day later");
    char when[32];
    snprintf(when, sizeof when, "%lld", now);
    settingsSet("update_last", when);
    settingsSave();
    const char* name = "the tests' fetcher";
    U.fetch = T.fetch != NULL ? T.fetch : updatePlatformFetch(&name);
    memset(&U.pending, 0, sizeof U.pending);
    U.pending.checkedAt = now;
    U.pending.forced = force;
    if (U.fetch == NULL) {
        U.pending.outcome = UPDATE_NO_FETCHER;
        copy(U.pending.why, sizeof U.pending.why, name);
        apply(&U.pending);
        return 0;
    }
    copy(U.base, sizeof U.base, updateBaseUrl());
    copy(U.current, sizeof U.current, releaseVersion());
    copy(U.skip, sizeof U.skip, settingsGet("update_skip"));
    fprintf(stderr, "update: checking %s%s (%s)\n", U.base, UPDATE_MANIFEST_NAME, name);
    SDL_SetAtomicInt(&U.done, 0);
    U.thread = SDL_CreateThread(checkThread, "xg-update", NULL);
    if (U.thread == NULL) {
        U.pending.outcome = UPDATE_FAILED;
        snprintf(U.pending.why, sizeof U.pending.why, "no thread for the check: %s", SDL_GetError());
        apply(&U.pending);
        return 0;
    }
    U.status.running = 1;
    U.status.forced = force;
    U.status.checkedAt = now;
    return 1;
}

const UpdateStatus* updateStatus(void) {
    init();
    take();
    return &U.status;
}

void updateVBlank(void) {
    if (U.thread != NULL) {
        take();
        return;
    }
    if (U.look > 0) {
        U.look--;
        return;
    }
    U.look = LOOK_VBLANKS;
    updateStart(0);
}

void updateShutdown(void) {
    if (U.thread == NULL) return;
    if (updateFetchAbort()) {
        SDL_WaitThread(U.thread, NULL);
    } else {
        SDL_DetachThread(U.thread);
    }
    U.thread = NULL;
}

void updateSkip(const char* version) {
    init();
    if (version == NULL || !releaseVersionValid(version)) return;
    char skipped[16]; /* version may be the status's own, which forgetAvailable clears */
    copy(skipped, sizeof skipped, version);
    settingsDescribe("update_skip", "update_skip: a version the player skipped: the update check does not show it");
    settingsSet("update_skip", skipped);
    if (strcmp(U.status.available, skipped) == 0) forgetAvailable();
    settingsSave();
    fprintf(stderr, "update: %s skipped\n", skipped);
}
