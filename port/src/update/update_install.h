#ifndef PORT_UPDATE_INSTALL_H
#define PORT_UPDATE_INSTALL_H

/* The one-click update (xg-4oq.14.4), in a build with the updater: a copy that
 * installs itself (release.h releaseUpdatesItself: the Linux tarball, the
 * Windows zip and setup; Android's is xg-4oq.14.5) fetches the newer release's
 * own asset, checks it, installs it and starts again in it.
 *
 *   1. checking: the manifest again (update_check.c: its signature, the
 *      release newer than this copy), and in it the asset of this copy's kind
 *      for this platform;
 *   2. downloading: into <pref folder>/update/ (the size the manifest gives,
 *      then its SHA-512; a different file is deleted);
 *   3. unpacking (tarball, zip): every file of the archive written beside the
 *      installed one as <name>.new (update_archive.c), after the archive's own
 *      release.txt is found to name that version and this copy's kind, and the
 *      running executable is found in it; setup: nothing;
 *   4. finishing (the game's thread): each installed file renamed to
 *      <name>.old, each .new to its name; a failure puts every one back and
 *      nothing has changed. Then the game quits and starts again in the new
 *      executable with the same arguments. Setup: the new setup.exe is
 *      started silently ("/S /UPDATE=<executable>": Windows asks for admin),
 *      the game quits, and the setup starts it again when it is done;
 *   5. at the next start (updateInstallCleanup): the .old files and the
 *      download deleted.
 *
 * Steps 1-3 run on a thread of their own, F1 > System > Updates showing their
 * progress; the player can cancel them, and nothing installed has changed
 * before step 4. update/files.txt in the pref folder lists the files of steps
 * 3-5 with the copy's folder, so a start after a crash finishes or undoes
 * them. The settings, memory cards and saves are in the pref folder, which an
 * install never touches; files of the old release that the new one no longer
 * has are left as they are. */

#include <stddef.h>
#include <stdint.h>

#include "update/update_check.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where and what this copy is, and what an install uses. */
typedef struct {
    char root[1024];      /* the copy's folder: what its archive's top folder holds, ending in a separator */
    char exe[256];        /* the running executable, relative to root ("bin/xenogears", "xenogears.exe") */
    char downloads[1024]; /* where the asset goes, ending in a separator */
    ReleaseKind kind;
    char version[16];
    char platform[32];
    char base[512];       /* the manifest's folder (updateBaseUrl) */
    UpdateFetch fetch;
    UpdateDownload download;
    const uint8_t (*keys)[32]; /* NULL: the build's own */
    int keyCount;
} UpdateInstallPlace;

typedef enum {
    INSTALL_IDLE,
    INSTALL_CHECKING,
    INSTALL_DOWNLOADING,
    INSTALL_VERIFYING,
    INSTALL_UNPACKING,
    INSTALL_READY,  /* steps 1-3 done: updateInstallFinish */
    INSTALL_FAILED, /* why says; nothing installed has changed */
} UpdateInstallStage;

typedef struct {
    UpdateInstallStage stage;
    uint64_t done, total; /* the download's bytes */
    char version[16];     /* the release being installed */
    char why[256];
} UpdateInstallStatus;

/* This process's place, or 0 with why: a copy that cannot install itself (a
 * development build, a package manager's, Android's for now), or whose folder
 * cannot be written (a zip unpacked into Program Files). */
int updateInstallPlace(UpdateInstallPlace* place, char* why, size_t whySize);

/* Steps 1-3 on their own thread, for place (copied). 0 when one is under way already. */
int updateInstallStart(const UpdateInstallPlace* place);
/* Stop steps 1-3 (the download within a second): the stage becomes FAILED, "cancelled". */
void updateInstallCancel(void);
/* The progress now (from any thread's point of view, the game's). */
UpdateInstallStatus updateInstallStatus(void);
/* Step 4, while READY: 1 when the files are in place (then updateInstallRestart), 0 with why when
 * nothing changed. Setup: 1 always (nothing to swap). */
int updateInstallFinish(char* why, size_t whySize);
/* After updateInstallFinish: the game quits and starts again in the new executable (setup: the setup
 * starts first, Windows asking for admin). Returns only when that could not start, with why; the old
 * copy then still runs (a swap is undone). */
void updateInstallRestart(char* why, size_t whySize);
/* At the start (main.c): what an earlier install left, finished or undone, then deleted. */
void updateInstallCleanup(void);
/* The game's vblank (gpu/libgpu.c): an install updateInstallStart began and that is READY is
 * finished and the game restarted (outside the menu's frame: the quit shuts the menu down); when
 * that fails, a corner note says why and the game goes on. */
void updateInstallVBlank(void);

/* The steps themselves, blocking, for the tests (and the thread): 1-3 with place, the status kept in st;
 * 4 and 5 on the files update/files.txt in place->downloads lists. */
int updateInstallPrepare(const UpdateInstallPlace* place, UpdateInstallStatus* st);
int updateInstallSwap(const UpdateInstallPlace* place, char* why, size_t whySize);
void updateInstallTidy(const UpdateInstallPlace* place);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UPDATE_INSTALL_H */
