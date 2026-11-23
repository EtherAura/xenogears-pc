/* The one-click update (update_install.h, xg-4oq.14.4): steps 1-3 on a thread
 * of their own, step 4 and the restart on the game's, step 5 at the next start.
 * update/files.txt (in the pref folder) is the record of steps 3-5:
 *
 *   root=<the copy's folder>
 *   state=unpacking | unpacked | swapping | swapped
 *   file=<a path inside it>          (one line per file of the archive)
 *
 * so whatever a crash interrupts is finished or undone at the next start:
 * unpacking/unpacked, the .new files deleted (nothing had changed); swapping,
 * the swap finished; swapped, the .old files deleted. */

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/stat.h>
#endif

#include "update_install.h"
#include "update_archive.h"
#include "monocypher-ed25519.h"
#include "system/engine.h"
#include "system/release.h"
#ifdef __ANDROID__
#include "system/android.h"
#endif

/* system/quit.c, gpu/present.c (weak: the tests have no game to quit, no picture). */
void quitNow(void) __attribute__((weak));
void quitAndStart(const char* exe) __attribute__((weak));
void presentShowNote(const char* text) __attribute__((weak));

#define LIST_NAME "files.txt"
#define RELEASE_FILE_MAX 4096 /* a release file is two lines (release.c's own limit) */
#define REL_MAX 400

static void say(char* why, size_t size, const char* fmt, ...) {
    if (why == NULL || size == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, size, fmt, ap);
    va_end(ap);
}

static int exists(const char* path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info);
}

/* ---- update/files.txt -------------------------------------------------------------------------- */

typedef struct {
    char root[1024];
    char state[16];
    int count, cap;
    char (*rel)[REL_MAX];
} FileList;

static void listFree(FileList* l) {
    free(l->rel);
    memset(l, 0, sizeof *l);
}

static int listAdd(FileList* l, const char* rel) {
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 32;
        char (*grown)[REL_MAX] = realloc(l->rel, (size_t)cap * sizeof *grown);
        if (grown == NULL) return 0;
        l->rel = grown;
        l->cap = cap;
    }
    snprintf(l->rel[l->count++], REL_MAX, "%s", rel);
    return 1;
}

static void listPath(const char* downloads, char* out, size_t size) {
    snprintf(out, size, "%s%s", downloads, LIST_NAME);
}

/* Written whole to files.txt.tmp, then renamed over files.txt. */
static int listWrite(const char* downloads, const FileList* l) {
    char path[1100], tmp[1110];
    listPath(downloads, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    SDL_IOStream* io = SDL_IOFromFile(tmp, "wb");
    if (io == NULL) return 0;
    int ok = SDL_IOprintf(io, "# what an update installs (port/src/update/update_install.c)\nroot=%s\nstate=%s\n",
                          l->root, l->state) > 0;
    for (int i = 0; ok && i < l->count; i++) ok = SDL_IOprintf(io, "file=%s\n", l->rel[i]) > 0;
    ok = SDL_CloseIO(io) && ok;
    return ok && SDL_RenamePath(tmp, path);
}

static int listRead(const char* downloads, FileList* l) {
    char path[1100];
    listPath(downloads, path, sizeof path);
    memset(l, 0, sizeof *l);
    size_t n = 0;
    char* text = (char*)SDL_LoadFile(path, &n);
    if (text == NULL) return 0;
    for (char *line = text, *next; line != NULL; line = next) {
        next = strchr(line, '\n');
        if (next != NULL) *next++ = '\0';
        if (strncmp(line, "root=", 5) == 0) snprintf(l->root, sizeof l->root, "%s", line + 5);
        else if (strncmp(line, "state=", 6) == 0) snprintf(l->state, sizeof l->state, "%s", line + 6);
        else if (strncmp(line, "file=", 5) == 0 && line[5] != '\0' && strstr(line, "..") == NULL) listAdd(l, line + 5);
    }
    SDL_free(text);
    return l->root[0] != '\0' && l->state[0] != '\0';
}

static void listRemove(const char* downloads) {
    char path[1100];
    listPath(downloads, path, sizeof path);
    SDL_RemovePath(path);
}

static void join(char* out, size_t size, const char* root, const char* rel, const char* suffix) {
    snprintf(out, size, "%s%s%s", root, rel, suffix);
}

/* ---- the place --------------------------------------------------------------------------------- */

/* A file made and removed in folder: it can be written. */
static int writable(const char* folder, char* why, size_t whySize) {
    char probe[1200];
    snprintf(probe, sizeof probe, "%s.xenogears-update-probe", folder);
    SDL_IOStream* io = SDL_IOFromFile(probe, "wb");
    if (io == NULL) {
        say(why, whySize, "the game's folder cannot be written (%s): %s", folder, SDL_GetError());
        return 0;
    }
    SDL_CloseIO(io);
    SDL_RemovePath(probe);
    return 1;
}

int updateInstallPlace(UpdateInstallPlace* p, char* why, size_t whySize) {
    memset(p, 0, sizeof *p);
    p->kind = releaseKind();
    if (p->kind == RELEASE_DEV) {
        say(why, whySize, "a development build never updates itself");
        return 0;
    }
    if (!releaseUpdatesItself(p->kind)) {
        say(why, whySize, "a package manager updates this copy");
        return 0;
    }
#ifndef __ANDROID__
    if (p->kind == RELEASE_ANDROID) {
        say(why, whySize, "an Android release's file outside Android");
        return 0;
    }
#else
    if (p->kind != RELEASE_ANDROID) {
        say(why, whySize, "a desktop release's file on Android");
        return 0;
    }
#endif
    const char* base = p->kind == RELEASE_ANDROID ? "./" : SDL_GetBasePath(); /* Android: no folder of ours to write */
    if (base == NULL) {
        say(why, whySize, "the game's folder is not known");
        return 0;
    }
#ifdef _WIN32
    const char* ext = ".exe";
#else
    const char* ext = "";
#endif
    const char* name = engineThis() == ENGINE_ENHANCED ? "xenogears-enhanced" : "xenogears";
    if (p->kind == RELEASE_TARBALL) {
        snprintf(p->root, sizeof p->root, "%s..%c", base, base[strlen(base) - 1]); /* bin/.. */
        snprintf(p->exe, sizeof p->exe, "bin/%s%s", name, ext);
    } else {
        snprintf(p->root, sizeof p->root, "%s", base);
        snprintf(p->exe, sizeof p->exe, "%s%s", name, ext);
    }
    char* pref = SDL_GetPrefPath(NULL, "xenogears");
    if (pref == NULL) {
        say(why, whySize, "no folder for the download: %s", SDL_GetError());
        return 0;
    }
    snprintf(p->downloads, sizeof p->downloads, "%supdate%c", pref, pref[strlen(pref) - 1]);
    SDL_free(pref);
    snprintf(p->version, sizeof p->version, "%s", releaseVersion());
    snprintf(p->platform, sizeof p->platform, "%s", updatePlatform());
    snprintf(p->base, sizeof p->base, "%s", updateBaseUrl());
    p->fetch = updatePlatformFetch(NULL);
    p->download = updatePlatformDownload();
    if (p->fetch == NULL || p->download == NULL) {
        say(why, whySize, "no way to download on this system");
        return 0;
    }
    /* The setup writes Program Files, with Windows's permission; Android installs the APK itself. */
    if (p->kind == RELEASE_SETUP || p->kind == RELEASE_ANDROID) return 1;
    if (!writable(p->root, why, whySize)) return 0;
    if (p->kind == RELEASE_TARBALL) {
        char bin[1100];
        snprintf(bin, sizeof bin, "%sbin/", p->root);
        if (!writable(bin, why, whySize)) return 0;
    }
    return 1;
}

/* ---- steps 1-3 --------------------------------------------------------------------------------- */

static struct {
    SDL_Thread* thread;
    SDL_AtomicInt stage, done, total, cancel;
    int wanted; /* updateInstallStart: finished and restarted at the vblank once READY */
    UpdateInstallStatus st; /* the thread's: version and why, complete before stage is set to READY or FAILED */
    UpdateInstallPlace place;
    char setup[1300];       /* setup, android: the downloaded setup.exe or APK, run or installed as it is */
    int watch, ticks;       /* android: Android's answer looked at (XgActivity.updateInstallState) */
} I;

static void stage(UpdateInstallStatus* st, UpdateInstallStage s) {
    st->stage = s;
    SDL_SetAtomicInt(&I.stage, (int)s);
}

static int fail(UpdateInstallStatus* st, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(st->why, sizeof st->why, fmt, ap);
    va_end(ap);
    stage(st, INSTALL_FAILED);
    fprintf(stderr, "update: install: %s\n", st->why);
    return 0;
}

static int cancelled(void) {
    return SDL_GetAtomicInt(&I.cancel) != 0;
}

static int onProgress(void* user, uint64_t done, uint64_t total) {
    UpdateInstallStatus* st = (UpdateInstallStatus*)user;
    st->done = done;
    st->total = total;
    SDL_SetAtomicInt(&I.done, (int)done);
    SDL_SetAtomicInt(&I.total, (int)total);
    return !cancelled();
}

static int sha512File(const char* path, uint8_t out[64]) {
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (io == NULL) return 0;
    crypto_sha512_ctx ctx;
    crypto_sha512_init(&ctx);
    static uint8_t buf[1 << 16]; /* the one thread that hashes */
    size_t n;
    while ((n = SDL_ReadIO(io, buf, sizeof buf)) > 0) crypto_sha512_update(&ctx, buf, n);
    int ok = SDL_GetIOStatus(io) == SDL_IO_STATUS_EOF;
    SDL_CloseIO(io);
    crypto_sha512_final(&ctx, out);
    return ok;
}

typedef struct {
    const UpdateInstallPlace* place;
    FileList* list;
    const char* version;
    const char* releaseRel;
    int release, exe;
} Unpack;

static void removeNew(const FileList* l) {
    char path[1500];
    for (int i = 0; i < l->count; i++) {
        join(path, sizeof path, l->root, l->rel[i], ".new");
        SDL_RemovePath(path);
    }
}

static int writeNew(const char* path, const UpdateArchiveFile* f, char* why, size_t whySize) {
    char folder[1500];
    snprintf(folder, sizeof folder, "%s", path);
    char* slash = strrchr(folder, '/');
#ifdef _WIN32
    char* back = strrchr(folder, '\\');
    if (back != NULL && (slash == NULL || back > slash)) slash = back;
#endif
    if (slash != NULL) {
        *slash = '\0';
        if (!exists(folder) && !SDL_CreateDirectory(folder)) {
            say(why, whySize, "cannot make %s: %s", folder, SDL_GetError());
            return 0;
        }
    }
    SDL_IOStream* io = SDL_IOFromFile(path, "wb");
    if (io == NULL) {
        say(why, whySize, "cannot write %s: %s", path, SDL_GetError());
        return 0;
    }
    int ok = SDL_WriteIO(io, f->data, f->size) == f->size && SDL_FlushIO(io);
    ok = SDL_CloseIO(io) && ok;
    if (!ok) {
        say(why, whySize, "cannot write %s: %s", path, SDL_GetError());
        SDL_RemovePath(path);
        return 0;
    }
#ifndef _WIN32
    chmod(path, f->executable ? 0755 : 0644);
#endif
    return 1;
}

static int unpackVisit(void* user, const UpdateArchiveFile* f, char* why, size_t whySize) {
    Unpack* u = (Unpack*)user;
    if (cancelled()) {
        say(why, whySize, "cancelled");
        return 0;
    }
    if (strcmp(f->path, u->releaseRel) == 0) {
        char text[RELEASE_FILE_MAX], version[16];
        ReleaseKind kind = RELEASE_DEV;
        if (f->size >= sizeof text) {
            say(why, whySize, "its %s is not a release file", f->path);
            return 0;
        }
        memcpy(text, f->data, f->size);
        text[f->size] = '\0';
        if (!releaseParse(text, version, sizeof version, &kind) || strcmp(version, u->version) != 0 ||
            kind != u->place->kind) {
            say(why, whySize, "its %s names %s (%s), not %s (%s)", f->path, version[0] ? version : "no release",
                releaseKindName(kind), u->version, releaseKindName(u->place->kind));
            return 0;
        }
        u->release = 1;
    }
    if (strcmp(f->path, u->place->exe) == 0) u->exe = 1;
    char path[1500];
    join(path, sizeof path, u->place->root, f->path, ".new");
    if (!listAdd(u->list, f->path) || !listWrite(u->place->downloads, u->list)) {
        say(why, whySize, "cannot keep the list of files (%s%s)", u->place->downloads, LIST_NAME);
        return 0;
    }
    return writeNew(path, f, why, whySize);
}

int updateInstallPrepare(const UpdateInstallPlace* p, UpdateInstallStatus* st) {
    memset(st, 0, sizeof *st);
    /* 1: the manifest, and this copy's asset in it */
    stage(st, INSTALL_CHECKING);
    UpdateStatus check;
    memset(&check, 0, sizeof check);
    static UpdateManifest m; /* the one install thread's */
    UpdateOutcome o = updateCheckManifest(p->fetch, p->base, p->keys, p->keyCount, p->version, NULL, &check, &m);
    if (o != UPDATE_NEWER) {
        return fail(st, "%s", o == UPDATE_LATEST        ? "this copy is the latest release"
                              : o == UPDATE_NOT_OFFERED ? "the latest release offers no updates yet"
                                                        : check.why);
    }
    snprintf(st->version, sizeof st->version, "%s", m.version);
    const UpdateAsset* a = updateManifestAsset(&m, p->kind, p->platform);
    if (a == NULL) return fail(st, "release %s has no %s for %s", m.version, releaseKindName(p->kind), p->platform);
    if (a->size == 0 || a->size > UPDATE_ARCHIVE_MAX) return fail(st, "release %s's %s is too large", m.version, a->name);
    if (cancelled()) return fail(st, "cancelled");

    /* 2: the asset, its size and SHA-512 the manifest's */
    stage(st, INSTALL_DOWNLOADING);
    st->total = a->size;
    SDL_SetAtomicInt(&I.total, (int)a->size);
    char path[1300], part[1310], url[700], why[256] = "";
    if (!exists(p->downloads) && !SDL_CreateDirectory(p->downloads)) {
        return fail(st, "cannot make %s: %s", p->downloads, SDL_GetError());
    }
    snprintf(path, sizeof path, "%s%s", p->downloads, a->name);
    snprintf(part, sizeof part, "%s.part", path);
    snprintf(url, sizeof url, "%s%s", m.download, a->name);
    fprintf(stderr, "update: install: downloading %s (%llu bytes)\n", url, (unsigned long long)a->size);
    if (!p->download(url, part, a->size, onProgress, st, why, sizeof why)) {
        SDL_RemovePath(part);
        return fail(st, "%s", why);
    }
    stage(st, INSTALL_VERIFYING);
    uint8_t sum[64];
    if (!sha512File(part, sum) || memcmp(sum, a->sha512, 64) != 0) {
        SDL_RemovePath(part);
        return fail(st, "the download is not the file the signed manifest names (SHA-512)");
    }
    SDL_RemovePath(path);
    if (!SDL_RenamePath(part, path)) return fail(st, "cannot rename %s: %s", part, SDL_GetError());
    if (p->kind == RELEASE_SETUP || p->kind == RELEASE_ANDROID) {
        snprintf(I.setup, sizeof I.setup, "%s", path);
        stage(st, INSTALL_READY);
        return 1;
    }

    /* 3: unpacked beside the installed files */
    stage(st, INSTALL_UNPACKING);
    FileList list;
    memset(&list, 0, sizeof list);
    snprintf(list.root, sizeof list.root, "%s", p->root);
    snprintf(list.state, sizeof list.state, "unpacking");
    if (!listWrite(p->downloads, &list)) {
        return fail(st, "cannot keep the list of files (%s%s): %s", p->downloads, LIST_NAME, SDL_GetError());
    }
    Unpack u = {p, &list, m.version, p->kind == RELEASE_TARBALL ? "share/xenogears-pc/release.txt" : "release.txt", 0,
                0};
    char top[256];
    int ok = updateArchiveReadFile(path, unpackVisit, &u, top, sizeof top, why, sizeof why);
    if (ok && (!u.release || !u.exe)) {
        say(why, sizeof why, "the archive has no %s", !u.release ? u.releaseRel : p->exe);
        ok = 0;
    }
    if (ok) {
        snprintf(list.state, sizeof list.state, "unpacked");
        ok = listWrite(p->downloads, &list);
        if (!ok) say(why, sizeof why, "cannot keep the list of files: %s", SDL_GetError());
    }
    if (!ok) {
        removeNew(&list);
        listRemove(p->downloads);
        listFree(&list);
        return fail(st, "%s", why);
    }
    fprintf(stderr, "update: install: %d files of %s ready beside %s\n", list.count, m.version, p->root);
    listFree(&list);
    stage(st, INSTALL_READY);
    return 1;
}

static int SDLCALL installThread(void* unused) {
    (void)unused;
    UpdateInstallStatus st;
    updateInstallPrepare(&I.place, &st);
    I.st = st; /* then the stage, which the game thread reads first */
    SDL_SetAtomicInt(&I.stage, (int)st.stage);
    return 0;
}

int updateInstallStart(const UpdateInstallPlace* place) {
    if (I.thread != NULL) {
        int s = SDL_GetAtomicInt(&I.stage);
        if (s != INSTALL_READY && s != INSTALL_FAILED) return 0;
        SDL_WaitThread(I.thread, NULL);
        I.thread = NULL;
    }
    I.place = *place;
    I.wanted = 1;
    memset(&I.st, 0, sizeof I.st);
    SDL_SetAtomicInt(&I.cancel, 0);
    SDL_SetAtomicInt(&I.done, 0);
    SDL_SetAtomicInt(&I.total, 0);
    SDL_SetAtomicInt(&I.stage, INSTALL_CHECKING);
    I.thread = SDL_CreateThread(installThread, "xg-update-install", NULL);
    if (I.thread == NULL) {
        snprintf(I.st.why, sizeof I.st.why, "no thread for the install: %s", SDL_GetError());
        SDL_SetAtomicInt(&I.stage, INSTALL_FAILED);
        return 0;
    }
    return 1;
}

void updateInstallCancel(void) {
    SDL_SetAtomicInt(&I.cancel, 1);
}

UpdateInstallStatus updateInstallStatus(void) {
    UpdateInstallStatus s;
    memset(&s, 0, sizeof s);
    s.stage = (UpdateInstallStage)SDL_GetAtomicInt(&I.stage);
    s.done = (uint64_t)(unsigned)SDL_GetAtomicInt(&I.done);
    s.total = (uint64_t)(unsigned)SDL_GetAtomicInt(&I.total);
    if (s.stage == INSTALL_READY || s.stage == INSTALL_FAILED) {
        if (I.thread != NULL) { /* finished: its status is complete */
            SDL_WaitThread(I.thread, NULL);
            I.thread = NULL;
        }
        snprintf(s.version, sizeof s.version, "%s", I.st.version);
        snprintf(s.why, sizeof s.why, "%s", I.st.why);
    }
    return s;
}

/* ---- step 4 ------------------------------------------------------------------------------------ */

/* A swapped file back as it was: the new one back to .new, its .old (if the old release had it) to its name. */
static void undoOne(const char* root, const char* rel) {
    char target[1500], fresh[1500], old[1500];
    join(target, sizeof target, root, rel, "");
    join(fresh, sizeof fresh, root, rel, ".new");
    join(old, sizeof old, root, rel, ".old");
    if (exists(target) && !exists(fresh)) SDL_RenamePath(target, fresh);
    if (exists(old)) SDL_RenamePath(old, target);
}

int updateInstallSwap(const UpdateInstallPlace* p, char* why, size_t whySize) {
    FileList l;
    if (!listRead(p->downloads, &l) || strcmp(l.state, "unpacked") != 0) {
        listFree(&l);
        say(why, whySize, "nothing is ready to install");
        return 0;
    }
    snprintf(l.state, sizeof l.state, "swapping");
    if (!listWrite(p->downloads, &l)) {
        say(why, whySize, "cannot keep the list of files: %s", SDL_GetError());
        listFree(&l);
        return 0;
    }
    char target[1500], fresh[1500], old[1500];
    int i;
    for (i = 0; i < l.count; i++) {
        join(target, sizeof target, l.root, l.rel[i], "");
        join(fresh, sizeof fresh, l.root, l.rel[i], ".new");
        join(old, sizeof old, l.root, l.rel[i], ".old");
        if (exists(old) && !SDL_RemovePath(old)) {
            say(why, whySize, "cannot remove %s (still in use?): %s", old, SDL_GetError());
            break;
        }
        if (exists(target) && !SDL_RenamePath(target, old)) {
            say(why, whySize, "cannot move %s aside: %s", target, SDL_GetError());
            break;
        }
        if (!SDL_RenamePath(fresh, target)) {
            say(why, whySize, "cannot put %s in place: %s", target, SDL_GetError());
            if (exists(old)) SDL_RenamePath(old, target);
            break;
        }
    }
    if (i < l.count) { /* each file before this one back as it was: nothing changed */
        while (i-- > 0) undoOne(l.root, l.rel[i]);
        snprintf(l.state, sizeof l.state, "unpacked");
        listWrite(p->downloads, &l);
        fprintf(stderr, "update: install: %s; every file put back\n", why);
        listFree(&l);
        return 0;
    }
    snprintf(l.state, sizeof l.state, "swapped");
    listWrite(p->downloads, &l);
    fprintf(stderr, "update: install: %d files in place in %s\n", l.count, l.root);
    listFree(&l);
    return 1;
}

int updateInstallFinish(char* why, size_t whySize) {
    UpdateInstallStatus s = updateInstallStatus();
    if (s.stage != INSTALL_READY) {
        say(why, whySize, "nothing is ready to install");
        return 0;
    }
    if (I.place.kind == RELEASE_SETUP || I.place.kind == RELEASE_ANDROID) return 1;
    return updateInstallSwap(&I.place, why, whySize);
}

void updateInstallRestart(char* why, size_t whySize) {
    if (I.place.kind == RELEASE_SETUP) {
#ifdef _WIN32
        /* "/UPDATE=<executable>": the setup waits for the game to end, installs, and starts that
         * executable again as the player (packaging/xenogears-pc-installer.nsi.in); "/D=": into this
         * copy's folder (NSIS's own option: last, unquoted, no trailing separator). */
        wchar_t file[1300], params[1400];
        char args[1400], dir[1100];
        snprintf(dir, sizeof dir, "%s", I.place.root);
        size_t n = strlen(dir);
        while (n > 3 && (dir[n - 1] == '\\' || dir[n - 1] == '/')) dir[--n] = '\0';
        snprintf(args, sizeof args, "/S /UPDATE=%s /D=%s", I.place.exe, dir);
        if (MultiByteToWideChar(CP_UTF8, 0, I.setup, -1, file, 1300) == 0 ||
            MultiByteToWideChar(CP_UTF8, 0, args, -1, params, 1400) == 0) {
            say(why, whySize, "the setup's path is too long");
            return;
        }
        SHELLEXECUTEINFOW info;
        memset(&info, 0, sizeof info);
        info.cbSize = sizeof info;
        info.fMask = SEE_MASK_NOASYNC;
        info.lpVerb = L"open"; /* the setup asks for admin itself (its manifest): Windows asks the player */
        info.lpFile = file;
        info.lpParameters = params;
        info.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info)) {
            DWORD e = GetLastError();
            say(why, whySize, e == ERROR_CANCELLED ? "Windows did not let the setup run" : "cannot start the setup "
                                                                                          "(error %lu)", (unsigned long)e);
            return;
        }
        fprintf(stderr, "update: install: %s started; the game quits for it\n", I.setup);
        if (quitNow != NULL) quitNow();
#endif
        say(why, whySize, "the setup can only run on Windows");
        return;
    }
    char exe[1500];
    join(exe, sizeof exe, I.place.root, I.place.exe, "");
    fprintf(stderr, "update: install: starting %s\n", exe);
    if (quitAndStart != NULL) quitAndStart(exe);
    say(why, whySize, "cannot start %s", exe);
}

int updateInstallAllowed(char* why, size_t whySize) {
#ifdef __ANDROID__
    char can[8];
    if (strcmp(androidActivityText("updateCanInstall", can, sizeof can), "no") == 0) {
        androidActivityAction("updateAllowInstalls");
        say(why, whySize, "Android asks to allow installs from this app first: allow it in the settings it "
                          "opened, then choose Install and restart again");
        return 0;
    }
#endif
    (void)why, (void)whySize;
    return 1;
}

void updateInstallVBlank(void) {
#ifdef XG_UPDATER_TEST
    /* A test build (XG_UPDATER_TEST): XG_UPDATE_TEST_INSTALL=1 starts an install at the first vblank,
     * as the menu's Install and restart would (the device tests, which have no menu to drive). */
    static int tested;
    if (!tested) {
        tested = 1;
        const char* go = SDL_getenv("XG_UPDATE_TEST_INSTALL");
        UpdateInstallPlace place;
        char why[256] = "";
        if (go != NULL && go[0] == '1') {
            if (updateInstallPlace(&place, why, sizeof why) && updateInstallAllowed(why, sizeof why)) {
                updateInstallStart(&place);
            } else {
                fprintf(stderr, "update: install: test: %s\n", why);
            }
        }
    }
#endif
#ifdef __ANDROID__
    if (I.watch && ++I.ticks % 30 == 0) { /* Android's answer, twice a second */
        char state[300], note[340];
        androidActivityText("updateInstallState", state, sizeof state);
        if (strncmp(state, "failed:", 7) == 0) {
            I.watch = 0;
            const char* said = state + 7;
            fprintf(stderr, "update: install: Android did not install it: %s\n", said);
            /* Android's codes in words (PackageInstaller's status messages): the two a player meets. */
            const char* words = strstr(said, "INSTALL_FAILED_UPDATE_INCOMPATIBLE") != NULL
                                    ? "it is signed with another key than this app"
                                : strstr(said, "INSTALL_FAILED_ABORTED") != NULL ? "it was cancelled"
                                                                                 : said;
            snprintf(note, sizeof note, "Android did not install the update: %s", words);
            if (presentShowNote != NULL) presentShowNote(note);
            snprintf(I.st.why, sizeof I.st.why, "Android did not install it: %s", words);
            SDL_SetAtomicInt(&I.stage, INSTALL_FAILED); /* the menu offers it again */
        }
    }
#endif
    if (!I.wanted || SDL_GetAtomicInt(&I.stage) != INSTALL_READY) return;
    I.wanted = 0;
    char why[256] = "", note[300];
#ifdef __ANDROID__
    if (I.place.kind == RELEASE_ANDROID) { /* Android installs it, asking the player; this process then ends */
        fprintf(stderr, "update: install: %s handed to Android\n", I.setup);
        androidActivityActionText("updateInstall", I.setup);
        I.watch = 1;
        I.ticks = 0;
        return;
    }
#endif
    if (!updateInstallFinish(why, sizeof why)) {
        snprintf(note, sizeof note, "The update stopped: %s", why);
    } else {
        updateInstallRestart(why, sizeof why); /* returns only when that could not start */
        fprintf(stderr, "update: install: %s\n", why);
        if (I.place.kind == RELEASE_SETUP) {
            snprintf(note, sizeof note, "The setup did not run: %s", why);
        } else {
            snprintf(note, sizeof note, "Xenogears PC port %s is installed: start the game again", I.st.version);
        }
    }
    if (presentShowNote != NULL) presentShowNote(note);
}

/* ---- step 5 ------------------------------------------------------------------------------------ */

static void removeDownloads(const char* downloads) {
    char** names = SDL_GlobDirectory(downloads, "*", 0, NULL);
    if (names == NULL) return;
    char path[1300];
    for (char** n = names; *n != NULL; n++) {
        snprintf(path, sizeof path, "%s%s", downloads, *n);
        SDL_RemovePath(path);
    }
    SDL_free(names);
}

void updateInstallTidy(const UpdateInstallPlace* p) {
    FileList l;
    if (!listRead(p->downloads, &l)) {
        listFree(&l);
        if (exists(p->downloads)) removeDownloads(p->downloads); /* a download that went no further */
        return;
    }
    char target[1500], fresh[1500], old[1500];
    if (strcmp(l.state, "swapping") == 0) { /* interrupted while swapping: finished */
        for (int i = 0; i < l.count; i++) {
            join(target, sizeof target, l.root, l.rel[i], "");
            join(fresh, sizeof fresh, l.root, l.rel[i], ".new");
            join(old, sizeof old, l.root, l.rel[i], ".old");
            if (!exists(fresh)) continue;
            if (exists(target)) {
                SDL_RemovePath(old);
                SDL_RenamePath(target, old);
            }
            SDL_RenamePath(fresh, target);
        }
        snprintf(l.state, sizeof l.state, "swapped");
        listWrite(p->downloads, &l);
        fprintf(stderr, "update: an interrupted install finished in %s\n", l.root);
    }
    int left = 0;
    for (int i = 0; i < l.count; i++) {
        const char* suffix = strcmp(l.state, "swapped") == 0 ? ".old" : ".new";
        join(target, sizeof target, l.root, l.rel[i], suffix);
        if (exists(target) && !SDL_RemovePath(target)) left++; /* the old process still ending (Windows) */
    }
    if (left == 0) {
        removeDownloads(p->downloads);
        fprintf(stderr, "update: %s install tidied (%d files)\n", strcmp(l.state, "swapped") == 0 ? "the last" : "an "
                "unfinished", l.count);
    }
    listFree(&l);
}

void updateInstallCleanup(void) {
    UpdateInstallPlace p;
    memset(&p, 0, sizeof p);
    char* pref = SDL_GetPrefPath(NULL, "xenogears");
    if (pref == NULL) return;
    snprintf(p.downloads, sizeof p.downloads, "%supdate%c", pref, pref[strlen(pref) - 1]);
    SDL_free(pref);
    if (exists(p.downloads)) updateInstallTidy(&p);
}
