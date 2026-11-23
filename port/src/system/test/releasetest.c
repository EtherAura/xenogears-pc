/* Which release a copy is (system/release.c), disc-free: release versions as the
 * tags spell them and nothing else; release files with comments, CRLF, spaces
 * and keys of later versions; files that name no release (no kind, an unknown
 * one, dev, a bad version) read as a development build; then this process's own
 * release from a file named by XG_RELEASE_FILE, and the save state header's
 * version taken from it (statefile.c, with no XG_APP_VERSION). */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* setenv, unsetenv under a strict -std= */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "system/release.h"
#include "system/statefile.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

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
}

static int parses(const char* text, const char* version, ReleaseKind kind) {
    char got[16] = "x";
    ReleaseKind k = RELEASE_SETUP;
    int ok = releaseParse(text, got, sizeof got, &k);
    if (version == NULL) return ok == 0 && got[0] == '\0' && k == RELEASE_DEV;
    return ok == 1 && strcmp(got, version) == 0 && k == kind;
}

int main(void) {
    /* Versions: the tag's YYYY.MM.N without its v. */
    CHECK(releaseVersionValid("2026.11.26"));
    CHECK(releaseVersionValid("2026.01.0"));
    CHECK(releaseVersionValid("2099.12.9"));
    CHECK(releaseVersionValid("2026.11.1234567")); /* 15 characters: a state header's room */
    CHECK(!releaseVersionValid("2026.11.12345678"));
    CHECK(!releaseVersionValid("v2026.11.26"));
    CHECK(!releaseVersionValid("2026.1.0"));
    CHECK(!releaseVersionValid("2026.13.0"));
    CHECK(!releaseVersionValid("2026.00.1"));
    CHECK(!releaseVersionValid("2026.11.01"));
    CHECK(!releaseVersionValid("2026.11."));
    CHECK(!releaseVersionValid("2026.11.26+abc1234")); /* an Android development build's versionName */
    CHECK(!releaseVersionValid("1999.11.2"));
    CHECK(!releaseVersionValid("2026.11.26 "));
    CHECK(!releaseVersionValid(""));
    CHECK(!releaseVersionValid(NULL));

    /* Release files. */
    CHECK(parses("version=2026.11.26\nkind=tarball\n", "2026.11.26", RELEASE_TARBALL));
    CHECK(parses("kind=setup\r\nversion=2026.11.3\r\n", "2026.11.3", RELEASE_SETUP));
    CHECK(parses("# a comment\n\n  version = 2026.12.0 \n\tkind=deb\nchannel=stable\n", "2026.12.0", RELEASE_DEB));
    CHECK(parses("version=2026.11.26\nkind=zip", "2026.11.26", RELEASE_ZIP)); /* no last newline */
    CHECK(parses("version=2026.11.26\nkind=arch\n", "2026.11.26", RELEASE_ARCH));
    CHECK(parses("version=2026.11.26\nkind=gentoo\n", "2026.11.26", RELEASE_GENTOO));
    CHECK(parses("version=2026.11.25\nversion=2026.11.26\nkind=zip\n", "2026.11.26", RELEASE_ZIP)); /* the last one */
    CHECK(parses("version=2026.11.26\n", NULL, RELEASE_DEV));
    CHECK(parses("kind=tarball\n", NULL, RELEASE_DEV));
    CHECK(parses("version=2026.11.26\nkind=dev\n", NULL, RELEASE_DEV));
    CHECK(parses("version=2026.11.26\nkind=flatpak\n", NULL, RELEASE_DEV));
    CHECK(parses("version=2026.11.26\nkind=Tarball\n", NULL, RELEASE_DEV));
    CHECK(parses("version=v2026.11.26\nkind=tarball\n", NULL, RELEASE_DEV));
    CHECK(parses("version=2026.11.26.changes\nkind=tarball\n", NULL, RELEASE_DEV));
    CHECK(parses("#version=2026.11.26\nkind=tarball\n", NULL, RELEASE_DEV));
    CHECK(parses("", NULL, RELEASE_DEV));
    CHECK(parses(NULL, NULL, RELEASE_DEV));
    {
        char small[6] = "x";
        ReleaseKind k = RELEASE_ZIP;
        CHECK(releaseParse("version=2026.11.26\nkind=zip\n", small, sizeof small, &k) == 0);
        CHECK(small[0] == '\0' && k == RELEASE_DEV);
    }

    /* Kinds. */
    CHECK(strcmp(releaseKindName(RELEASE_DEV), "dev") == 0);
    CHECK(strcmp(releaseKindName(RELEASE_ANDROID), "android") == 0);
    CHECK(strcmp(releaseKindName(RELEASE_GENTOO), "gentoo") == 0);
    CHECK(strcmp(releaseKindName((ReleaseKind)RELEASE_KINDS), "dev") == 0);
    CHECK(releaseUpdatesItself(RELEASE_TARBALL) && releaseUpdatesItself(RELEASE_ZIP));
    CHECK(releaseUpdatesItself(RELEASE_SETUP) && releaseUpdatesItself(RELEASE_ANDROID));
    CHECK(!releaseUpdatesItself(RELEASE_DEB) && !releaseUpdatesItself(RELEASE_ARCH));
    CHECK(!releaseUpdatesItself(RELEASE_GENTOO) && !releaseUpdatesItself(RELEASE_DEV));

    /* This process's release: the file XG_RELEASE_FILE names (the working folder is the test's own). */
    const char* path = "releasetest-release.txt";
    const char* text = "# xenogears-pc\nversion=2026.11.26\nkind=tarball\n";
    SDL_IOStream* io = SDL_IOFromFile(path, "wb");
    CHECK(io != NULL && SDL_WriteIO(io, text, strlen(text)) == strlen(text));
    if (io != NULL) SDL_CloseIO(io);
    setVar("XG_RELEASE_FILE", path);
    setVar("XG_APP_VERSION", NULL);
    CHECK(strcmp(releaseVersion(), "2026.11.26") == 0);
    CHECK(releaseKind() == RELEASE_TARBALL);
    CHECK(strcmp(releaseFilePath(), path) == 0);
    CHECK(strcmp(stateAppVersion(), "2026.11.26") == 0);
    releaseLog();
    remove(path);

    if (failures) {
        printf("releasetest: %d failure(s)\n", failures);
        return 1;
    }
    printf("releasetest: ok\n");
    return 0;
}
