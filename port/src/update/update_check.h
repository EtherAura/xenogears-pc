#ifndef PORT_UPDATE_CHECK_H
#define PORT_UPDATE_CHECK_H

/* The update check (xg-4oq.14.3), in a build made with XG_UPDATER: at most once
 * a day, when the player chose it, the latest release's signed manifest
 * (update_manifest.h) is fetched in the background and checked; a newer
 * version shows as a corner note and in F1 > System > Updates (ui/updates.cpp),
 * with its release page and "skip this version". Nothing about the player is
 * sent: two GETs of public files, with no version in them.
 *
 * The question (ui.h uiAskUpdates): asked once, before the game starts (right
 * after the disc setup's Start on a first run), while update_check is ask.
 *
 * settings.ini:
 *   update_check      ask (default): the question at the next start; on: once
 *                     a day; off: never (no request at all)
 *   update_last       when the last check started, in seconds since 1970 (UTC):
 *                     the next is due a day later (or at once if the clock went
 *                     back)
 *   update_available  a newer version a verified manifest named, shown until
 *                     this copy is that version or newer; update_page its
 *                     release page
 *   update_skip       a version the player skipped: no note and no notice for it
 * XG_UPDATE_URL=https://.../  where the two files are fetched from (a folder,
 *                     its trailing / added) instead of the latest release's
 * XG_UPDATE_SHOW=<version>  the notice as if a check had found that version
 *                     (the menu's screenshots); it makes no request
 * XG_UPDATE_ASK=1    the question even where it is never asked (a development
 *                     build, a headless run: its screenshots); the answer is kept
 *
 * Never in a development build (release.h: RELEASE_DEV), a headless run
 * (XG_HEADLESS) or a netplay session. Network errors are written to the log
 * (stderr, "update: ...") and kept from the game: the menu's status line says
 * what happened, nothing pops up.
 *
 * The fetchers are the system's (update_fetch.c): WinHTTP on Windows, Java's
 * HttpURLConnection on Android (the activity, XgActivity.updateFetch), libcurl
 * loaded at run time elsewhere (libcurl.so.4; libcurl.4.dylib on macOS):
 * without it no check can run, and the menu says so. https only, GitHub's
 * redirect to its download host followed. */

#include <stddef.h>
#include <stdint.h>

#include "update/update_manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UPDATE_DEFAULT_URL "https://github.com/EtherAura/xenogears-pc/releases/latest/download/"
#define UPDATE_DAY (24 * 60 * 60)

typedef enum { UPDATE_ASK, UPDATE_ON, UPDATE_OFF } UpdateSetting;

typedef enum {
    UPDATE_UNCHECKED,   /* no check this run */
    UPDATE_NEWER,       /* a newer version, from a verified manifest */
    UPDATE_LATEST,      /* this copy is the latest release, or newer */
    UPDATE_SKIPPED,     /* a newer version the player skipped */
    UPDATE_NOT_OFFERED, /* the manifest's switch is off: nothing offered yet */
    UPDATE_NO_MANIFEST, /* the latest release carries no manifest (HTTP 404) */
    UPDATE_UNTRUSTED,   /* the signature does not verify with a key this build trusts */
    UPDATE_BAD_MANIFEST,/* not a manifest this build reads */
    UPDATE_FAILED,      /* no connection, a timeout, an HTTP error */
    UPDATE_NO_FETCHER,  /* nothing to fetch with (no libcurl) */
    UPDATE_OUTCOMES
} UpdateOutcome;

typedef struct {
    /* The last check: this run's, or none. */
    UpdateOutcome outcome;
    int running;         /* a check is under way */
    int forced;          /* started by the menu's Check now */
    long long checkedAt; /* when the last check started (seconds since 1970; update_last), 0: never */
    char version[16];    /* the latest release's version, when its manifest was read ("" otherwise) */
    char page[256];      /* its release page */
    char why[256];       /* what happened, for the log and the menu */
    /* A newer version known: this run's check found it, or an earlier one did
     * (update_available); a skipped one is not. A check that fails keeps it. */
    char available[16];
    char availablePage[256];
} UpdateStatus;

/* A GET of url (https) into buf, at most max bytes: the size read, or -1 with
 * the reason in why (-2: HTTP 404, the file is not there). */
typedef long (*UpdateFetch)(const char* url, char* buf, size_t max, char* why, size_t whySize);

/* A download's progress: done of total bytes; 0 cancels it. */
typedef int (*UpdateProgress)(void* user, uint64_t done, uint64_t total);

/* A GET of url (https) into the file path (made or replaced), which must come
 * to exactly size bytes (more stops it at once), progress after each piece: 1,
 * or 0 with the reason in why ("cancelled" when progress said so). */
typedef int (*UpdateDownload)(const char* url, const char* path, uint64_t size, UpdateProgress progress, void* user,
                              char* why, size_t whySize);

/* The setting (update_check) and its change (written at once). */
UpdateSetting updateSetting(void);
void updateSetSetting(UpdateSetting s);

/* What a manifest and its signature mean to a copy of version current, with
 * the version skip skipped (NULL: none): keys are the ones trusted (NULL: the
 * build's own, update_manifest.c; the tests give theirs). Fills st's outcome,
 * version, page and why. */
UpdateOutcome updateJudge(const char* text, size_t n, const char* sig, size_t sigSize, const uint8_t (*keys)[32],
                          int keyCount, const char* current, const char* skip, UpdateStatus* st);

/* One whole check, blocking: both files fetched from base (a URL ending in
 * '/'), then judged; a signature that does not match is fetched once more
 * with its manifest (a release published between the two GETs). */
UpdateOutcome updateCheckWith(UpdateFetch fetch, const char* base, const uint8_t (*keys)[32], int keyCount,
                              const char* current, const char* skip, UpdateStatus* st);
/* The same, keeping the manifest in out when one was trusted and read (NEWER,
 * LATEST, SKIPPED, NOT_OFFERED): what an install downloads (update_install.c). */
UpdateOutcome updateCheckManifest(UpdateFetch fetch, const char* base, const uint8_t (*keys)[32], int keyCount,
                                  const char* current, const char* skip, UpdateStatus* st, UpdateManifest* out);

/* Why no check may run in this process, or NULL: a development build, a
 * headless run, a netplay session (the setting is not looked at). */
const char* updateBlocked(void);
/* The daily check is due at now (seconds since 1970): on, not blocked, and a
 * day since update_last. */
int updateDue(long long now);
/* The question is to be asked now: ask, not blocked. */
int updateShouldAsk(void);

/* The check in the background: when due (force 0), or now (force 1: the
 * menu's Check now, still only while on and not blocked). 1 when it started. */
int updateStart(int force);
/* The status now: a check that finished meanwhile is taken in first (the
 * settings written, the log line, the note when it found a version). */
const UpdateStatus* updateStatus(void);
/* The game's vblank (gpu/libgpu.c): a finished check taken in, and the daily
 * check started in a long session. */
void updateVBlank(void);
/* Skip the version a check found: no note and no notice for it. */
void updateSkip(const char* version);

/* This platform's fetcher, or NULL (no libcurl); name: what it is, for the
 * log and the menu ("WinHTTP", "libcurl/8.11.1 ..."), or why there is none. */
UpdateFetch updatePlatformFetch(const char** name);
/* This platform's download (update_fetch.c), or NULL where there is no fetcher. */
UpdateDownload updatePlatformDownload(void);

/* The process is ending (system/quit.c, before SDL_Quit): a check under way
 * is stopped and waited for where its fetcher can stop (libcurl, within a
 * second: exit() would run its TLS library's exit handlers under it), else
 * left to the process's end. updateFetchAbort: the fetch under way gives up
 * soon (1), or cannot be stopped (0). */
void updateShutdown(void);
int updateFetchAbort(void);

/* The base URL in use (XG_UPDATE_URL or the latest release's). */
const char* updateBaseUrl(void);

/* For the tests: the process's state as at its start (no check, nothing taken
 * in), and the fetcher and keys updateStart's check uses instead of the
 * platform's and the build's (NULL: theirs again). */
void updateReset(void);
void updateTestWith(UpdateFetch fetch, const uint8_t (*keys)[32], int keyCount);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UPDATE_CHECK_H */
