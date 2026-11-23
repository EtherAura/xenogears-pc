#ifndef PORT_SYSTEM_RELEASE_H
#define PORT_SYSTEM_RELEASE_H

/* Which release this copy of the port is, and how it was installed: what the
 * in-app updater may do with it (xg-4oq.14).
 *
 * The executables do not carry it: the Linux tarball holds the .deb's own
 * executable, and a release rebuilt from its sources must give the same bytes
 * whatever it is packaged as. Each package writes a small file instead,
 * release.txt, beside what it installs:
 *
 *   Linux and the BSDs  <prefix>/share/xenogears-pc/release.txt (bin/.. of the executable)
 *   Windows             release.txt beside xenogears.exe
 *   Android             none: the app's versionName (XG_APP_VERSION, from the loader)
 *
 *   version=2026.11.26
 *   kind=tarball
 *
 * Lines are key=value; # starts a comment; other keys are left for later
 * versions. A copy without the file, or with one that names no release (the
 * version is not the release tag's YYYY.MM.N, or the kind is unknown), is a
 * development build: it is never offered an update. XG_RELEASE_FILE names
 * another file to read (tests), or "none". */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RELEASE_DEV,     /* a development build, or a copy without its release file */
    RELEASE_TARBALL, /* the Linux tarball, unpacked anywhere: replaces its own files */
    RELEASE_ZIP,     /* the Windows .zip: replaces its own files */
    RELEASE_SETUP,   /* the Windows installer: runs the next one */
    RELEASE_ANDROID, /* the APK: hands the next APK to Android's installer */
    RELEASE_DEB,     /* the Ubuntu package: apt updates it */
    RELEASE_ARCH,    /* the PKGBUILD's package: the player's AUR helper or makepkg updates it */
    RELEASE_GENTOO,  /* the ebuild's package: emerge updates it */
    RELEASE_KINDS
} ReleaseKind;

/* The release's version ("2026.11.26"), or "" for a development build. */
const char* releaseVersion(void);
ReleaseKind releaseKind(void);
/* The file read (or looked for), for messages; "" on Android. */
const char* releaseFilePath(void);
/* The word release.txt spells it with ("tarball"; "dev" for a development build). The menu has the
 * titles for people (port/src/ui/sections.cpp, translated). */
const char* releaseKindName(ReleaseKind kind);
/* 1 when a release of this kind installs the next one itself; 0 when a package manager does, or never. */
int releaseUpdatesItself(ReleaseKind kind);
/* Writes one line to stderr: the release and the file it came from. */
void releaseLog(void);

/* 1 when v is a release version: YYYY.MM.N as the release tags spell it (no v, month 01-12, N a whole
 * number from 0 without leading zeros). */
int releaseVersionValid(const char* v);
/* A release file's text: 1 with *version (at most size - 1 characters) and *kind when it names a release;
 * 0, with "" and RELEASE_DEV, when it does not. */
int releaseParse(const char* text, char* version, size_t size, ReleaseKind* kind);

#ifdef __cplusplus
}
#endif
#endif
