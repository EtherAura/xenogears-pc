/*
 * Which release this copy is, and how it was installed (release.h).
 */
#include "release.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RELEASE_FILE_MAX 4096 /* a release file is two lines; anything longer is not one */

static const char* const KIND_NAMES[RELEASE_KINDS] = {
    [RELEASE_DEV] = "dev",         [RELEASE_TARBALL] = "tarball", [RELEASE_ZIP] = "zip",
    [RELEASE_SETUP] = "setup",     [RELEASE_ANDROID] = "android", [RELEASE_DEB] = "deb",
    [RELEASE_ARCH] = "arch",       [RELEASE_GENTOO] = "gentoo",
};

static struct {
    int known;
    char version[16];
    ReleaseKind kind;
    char path[1024];
    char why[96]; /* a development build's reason, for the log */
} rel;

const char* releaseKindName(ReleaseKind kind) {
    return (int)kind >= 0 && (int)kind < RELEASE_KINDS ? KIND_NAMES[kind] : KIND_NAMES[RELEASE_DEV];
}

int releaseUpdatesItself(ReleaseKind kind) {
    return kind == RELEASE_TARBALL || kind == RELEASE_ZIP || kind == RELEASE_SETUP || kind == RELEASE_ANDROID;
}

int releaseVersionValid(const char* v) {
    /* 20YY */
    if (v == NULL || v[0] != '2' || v[1] != '0') return 0;
    for (int i = 2; i < 4; i++)
        if (v[i] < '0' || v[i] > '9') return 0;
    if (v[4] != '.') return 0;
    /* MM, 01-12 */
    if (v[5] < '0' || v[5] > '1' || v[6] < '0' || v[6] > '9') return 0;
    int month = (v[5] - '0') * 10 + (v[6] - '0');
    if (month < 1 || month > 12 || v[7] != '.') return 0;
    /* N: 0, or digits without a leading zero; the whole fits a state header's 15 characters */
    const char* n = v + 8;
    size_t digits = strspn(n, "0123456789");
    if (digits == 0 || n[digits] != '\0' || (digits > 1 && n[0] == '0')) return 0;
    return 8 + digits <= 15;
}

static ReleaseKind kindNamed(const char* word) {
    for (int k = RELEASE_DEV + 1; k < RELEASE_KINDS; k++)
        if (strcmp(word, KIND_NAMES[k]) == 0) return (ReleaseKind)k;
    return RELEASE_DEV;
}

/* One line's key and value, spaces around each trimmed; 0 for a blank line or a comment. */
static int splitLine(char* line, char** key, char** value) {
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '#') return 0;
    char* eq = strchr(line, '=');
    if (eq == NULL) return 0;
    char* end = eq;
    while (end > line && (end[-1] == ' ' || end[-1] == '\t')) end--;
    *end = '\0';
    char* v = eq + 1;
    while (*v == ' ' || *v == '\t') v++;
    char* vend = v + strlen(v);
    while (vend > v && (vend[-1] == ' ' || vend[-1] == '\t' || vend[-1] == '\r')) vend--;
    *vend = '\0';
    *key = line;
    *value = v;
    return 1;
}

int releaseParse(const char* text, char* version, size_t size, ReleaseKind* kind) {
    if (size > 0) version[0] = '\0';
    *kind = RELEASE_DEV;
    if (text == NULL || size == 0) return 0;
    size_t length = strlen(text);
    if (length >= RELEASE_FILE_MAX) return 0;
    char copy[RELEASE_FILE_MAX];
    memcpy(copy, text, length + 1);
    char found[16] = "";
    ReleaseKind named = RELEASE_DEV;
    for (char* line = copy; line != NULL;) {
        char* next = strchr(line, '\n');
        if (next != NULL) *next++ = '\0';
        char *key, *value;
        if (splitLine(line, &key, &value)) {
            if (strcmp(key, "version") == 0) {
                snprintf(found, sizeof found, "%s", strlen(value) < sizeof found ? value : "");
            } else if (strcmp(key, "kind") == 0) {
                named = kindNamed(value);
            }
        }
        line = next;
    }
    if (!releaseVersionValid(found) || named == RELEASE_DEV || strlen(found) >= size) return 0;
    snprintf(version, size, "%s", found);
    *kind = named;
    return 1;
}

#ifndef __ANDROID__
/* Where this copy's release file is: XG_RELEASE_FILE, or beside the executable as release.h says. */
static void filePath(char* out, size_t size) {
    const char* given = getenv("XG_RELEASE_FILE");
    if (given != NULL && given[0] != '\0') {
        snprintf(out, size, "%s", given);
        return;
    }
    const char* base = SDL_GetBasePath(); /* the executable's folder, with its separator */
    if (base == NULL) {
        out[0] = '\0';
        return;
    }
#ifdef _WIN32
    snprintf(out, size, "%srelease.txt", base);
#else
    /* <prefix>/bin/ -> <prefix>/share/xenogears-pc/release.txt */
    char prefix[1024];
    snprintf(prefix, sizeof prefix, "%s", base);
    size_t n = strlen(prefix);
    while (n > 1 && prefix[n - 1] == '/') prefix[--n] = '\0';
    char* slash = strrchr(prefix, '/');
    if (slash != NULL) *slash = '\0';
    snprintf(out, size, "%s/share/xenogears-pc/release.txt", prefix);
#endif
}
#endif

static void load(void) {
    if (rel.known) return;
    rel.known = 1;
    rel.kind = RELEASE_DEV;
#ifdef __ANDROID__
    /* The app's versionName: a release's is its tag's version; a development build's is
     * <tag>+<hash>[.changes] (port/android/app/build.gradle), which is no release version. */
    const char* name = getenv("XG_APP_VERSION");
    if (name != NULL && releaseVersionValid(name)) {
        snprintf(rel.version, sizeof rel.version, "%s", name);
        rel.kind = RELEASE_ANDROID;
    } else {
        snprintf(rel.why, sizeof rel.why, "the app's version %s is not a release's",
                 name != NULL && name[0] != '\0' ? name : "(none)");
    }
#else
    filePath(rel.path, sizeof rel.path);
    if (strcmp(rel.path, "none") == 0 || rel.path[0] == '\0') {
        snprintf(rel.why, sizeof rel.why, "%s", rel.path[0] ? "XG_RELEASE_FILE=none" : "no executable folder");
        return;
    }
    size_t size = 0;
    char* text = (char*)SDL_LoadFile(rel.path, &size);
    if (text == NULL) {
        snprintf(rel.why, sizeof rel.why, "no release file");
        return;
    }
    if (size >= RELEASE_FILE_MAX || memchr(text, '\0', size) != NULL ||
        !releaseParse(text, rel.version, sizeof rel.version, &rel.kind)) {
        snprintf(rel.why, sizeof rel.why, "its release file names no release");
    }
    SDL_free(text);
#endif
}

const char* releaseVersion(void) {
    load();
    return rel.version;
}

ReleaseKind releaseKind(void) {
    load();
    return rel.kind;
}

const char* releaseFilePath(void) {
    load();
    return rel.path;
}

void releaseLog(void) {
    load();
    if (rel.kind != RELEASE_DEV) {
        fprintf(stderr, "release: %s (%s)%s%s\n", rel.version, releaseKindName(rel.kind), rel.path[0] ? ", " : "",
                rel.path);
    } else {
        fprintf(stderr, "release: none, a development build (%s%s%s)\n", rel.why, rel.path[0] ? ": " : "", rel.path);
    }
}
