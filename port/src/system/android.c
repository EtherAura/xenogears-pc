/*
 * Android-only parts of the platform layer (xg-mrj.3.6, .7). The app itself is
 * port/android: its loader (port/android/loader/xgloader.c) sets the start
 * options and loads this library.
 */

#ifdef __ANDROID__

#define _BSD_SOURCE /* funopen */
#include <errno.h>
#include <jni.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>

#include "android.h"
#include "savestate.h"
#include "settings.h"
#include "statefile.h"

/* The game calls bzero, which glibc has and bionic dropped (as the Windows
 * C runtime lacks it: port/src/system/win32.c); with _BSD_SOURCE its header
 * has an inline one, a macro, for code of the system's. */
#undef bzero
void bzero(void* p, size_t n) {
    memset(p, 0, n);
}

/* ---- images read through the activity's file descriptors ----------------------------
 *
 * The player's discs and BIOS stay where they are: the activity opens them
 * through the Storage Access Framework (port/android XgActivity) and the
 * loader names each descriptor /proc/self/fd/N (XG_DISC, XG_DISC2, XG_BIOS).
 * Opening that name again would ask the storage daemon for the file as this
 * app, which holds no right to it beyond the descriptor; so this library's
 * fopen (-Wl,--wrap=fopen, port/CMakeLists.txt) reads such a name through the
 * descriptor itself: a stream over a dup of it with a position of its own
 * (pread), as the cd code opens one image several times, from two threads. */

typedef struct {
    int fd;
    off_t at;
} FdStream;

static int fdRead(void* cookie, char* buf, int n) {
    FdStream* s = cookie;
    ssize_t got = pread(s->fd, buf, (size_t)n, s->at);
    if (got > 0) s->at += got;
    return (int)got;
}

static fpos_t fdSeek(void* cookie, fpos_t offset, int whence) {
    FdStream* s = cookie;
    off_t base = 0;
    if (whence == SEEK_CUR) {
        base = s->at;
    } else if (whence == SEEK_END) {
        struct stat st;
        if (fstat(s->fd, &st) != 0) return -1;
        base = st.st_size;
    } else if (whence != SEEK_SET) {
        errno = EINVAL;
        return -1;
    }
    if (base + offset < 0) {
        errno = EINVAL;
        return -1;
    }
    s->at = base + offset;
    return s->at;
}

static int fdClose(void* cookie) {
    FdStream* s = cookie;
    int r = close(s->fd);
    free(s);
    return r;
}

FILE* __real_fopen(const char* path, const char* mode);
FILE* __wrap_fopen(const char* path, const char* mode);
FILE* __wrap_fopen(const char* path, const char* mode) {
    static const char prefix[] = "/proc/self/fd/";
    if (path != NULL && strncmp(path, prefix, sizeof prefix - 1) == 0 && mode[0] == 'r' &&
        strchr(mode, '+') == NULL) {
        char* end;
        long n = strtol(path + sizeof prefix - 1, &end, 10);
        if (*end == '\0' && n >= 0 && n < 65536) {
            FdStream* s = malloc(sizeof *s);
            int fd = dup((int)n);
            if (s == NULL || fd < 0) {
                free(s);
                if (fd >= 0) close(fd);
                return NULL;
            }
            s->fd = fd;
            s->at = 0;
            FILE* f = funopen(s, fdRead, NULL, fdSeek, fdClose);
            if (f == NULL) fdClose(s);
            return f;
        }
    }
    return __real_fopen(path, mode);
}

/* ---- the packs folder ------------------------------------------------------------- */

/* The folder that holds texture-packs/, ui-packs/... (engine.c enginePackDir):
 * the app's own folder on the shared storage (Android/data/<package>/files),
 * where the menu copies packs in (XgActivity, PackCopy) and a computer can
 * put them over USB. */
const char* androidPacksRoot(void) {
    const char* files = getenv("XG_ANDROID_FILES");
    return files != NULL ? files : ".";
}

/* ---- the packs copied in: listed, removed ------------------------------------------ */

static const char* const packLibraries[] = {"texture-packs", "ui-packs", "portrait-packs", "sprite-packs",
                                            "glyph-packs"};

typedef struct {
    AndroidPack* out;
    int count, max;
    const char* library;
} PackList;

typedef struct {
    uint64_t bytes;
} PackSize;

static SDL_EnumerationResult SDLCALL sizeOne(void* user, const char* dirname, const char* fname) {
    char path[4096];
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        SDL_EnumerateDirectory(path, sizeOne, user);
    } else if (info.type == SDL_PATHTYPE_FILE) {
        ((PackSize*)user)->bytes += info.size;
    }
    return SDL_ENUM_CONTINUE;
}

static SDL_EnumerationResult SDLCALL listOne(void* user, const char* dirname, const char* fname) {
    PackList* l = user;
    if (fname[0] == '.' || l->count >= l->max) return SDL_ENUM_CONTINUE; /* hidden: a copy under way */
    char path[4096];
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_DIRECTORY) return SDL_ENUM_CONTINUE;
    AndroidPack* p = &l->out[l->count++];
    snprintf(p->library, sizeof p->library, "%s", l->library);
    snprintf(p->name, sizeof p->name, "%s", fname);
    PackSize size = {0};
    SDL_EnumerateDirectory(path, sizeOne, &size);
    p->bytes = size.bytes;
    return SDL_ENUM_CONTINUE;
}

int androidListPacks(AndroidPack* out, int max) {
    PackList l = {out, 0, max, NULL};
    for (size_t i = 0; i < sizeof packLibraries / sizeof packLibraries[0]; i++) {
        char dir[4096];
        snprintf(dir, sizeof dir, "%s/%s", androidPacksRoot(), packLibraries[i]);
        l.library = packLibraries[i];
        SDL_EnumerateDirectory(dir, listOne, &l);
    }
    return l.count;
}

static SDL_EnumerationResult SDLCALL removeOne(void* user, const char* dirname, const char* fname) {
    char path[4096];
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    SDL_PathInfo info;
    /* a link is removed itself, never followed out of the folder */
    struct stat st;
    if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode) && SDL_GetPathInfo(path, &info)) {
        SDL_EnumerateDirectory(path, removeOne, user);
    }
    if (!SDL_RemovePath(path)) ++*(int*)user;
    return SDL_ENUM_CONTINUE;
}

int androidRemovePack(const char* library, const char* name) {
    int known = 0;
    for (size_t i = 0; i < sizeof packLibraries / sizeof packLibraries[0]; i++) {
        known |= strcmp(library, packLibraries[i]) == 0;
    }
    /* only a folder directly in one of the app's pack libraries */
    if (!known || name[0] == '\0' || name[0] == '.' || strchr(name, '/') != NULL) return 0;
    char path[4096];
    snprintf(path, sizeof path, "%s/%s/%s", androidPacksRoot(), library, name);
    struct stat st;
    if (lstat(path, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    int failed = 0;
    SDL_EnumerateDirectory(path, removeOne, &failed);
    if (!SDL_RemovePath(path)) failed++;
    fprintf(stderr, "packs: removed %s/%s%s\n", library, name, failed ? " (some files stayed)" : "");
    return failed == 0;
}

/* ---- an update and the save states ----------------------------------------------------- */

/* A state holds the program's own memory (statefile.c: the build id), so an update of the app
 * leaves the earlier version's states unloadable. They are kept (that version still loads them,
 * should the player go back), marked in the menu ("Version X's"), deletable there, and said
 * once, at the first start of the new version. */
void androidUpdateCheck(SDL_Window* window) {
    const char* now = stateAppVersion();
    if (now[0] == '\0') return;
    /* not "app_version": settings take XG_<KEY> from the environment, and XG_APP_VERSION is the version now */
    const char* saved = settingsGet("last_app_version");
    char before[64];
    snprintf(before, sizeof before, "%s", saved != NULL ? saved : "");
    if (strcmp(before, now) == 0) return;
    settingsDescribe("last_app_version",
                     "last_app_version: the Android app's version at its last start (its update note)");
    settingsSet("last_app_version", now);
    settingsSave();
    int others = 0;
    for (int slot = 0; slot <= STATE_SLOTS; slot++) {
        XgStateInfo info;
        if (stateSlotInfo(slot, &info) && info.otherBuild) others++;
        stateInfoFree(&info);
    }
    fprintf(stderr, "android: version %s (before: %s); %d states of other versions\n", now,
            before[0] ? before : "none", others);
    if (before[0] == '\0' || others == 0) return; /* a first start, or nothing it cannot load */
    char text[700];
    snprintf(text, sizeof text,
             "The app's version changed (%s to %s).\n\n%d save state%s made by the earlier version cannot be loaded "
             "by this one: a state holds the program's own memory, which a new version moves. %s kept (the earlier "
             "version can still load %s); F1 > Saves > Save states can delete %s.\n\nMemory card saves are not "
             "affected.",
             before, now, others, others == 1 ? "" : "s", others == 1 ? "It is" : "They are",
             others == 1 ? "it" : "them", others == 1 ? "it" : "them");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Xenogears", text, window);
}

/* ---- the activity ----------------------------------------------------------------- */

/* activity.<method>(arg) with signature sig (arg: a String, when sig takes one); 0 when it
 * is not there. */
static int activityCall(const char* method, const char* sig, const char* arg, jvalue* out) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (env == NULL || activity == NULL) return 0;
    jclass c = (*env)->GetObjectClass(env, activity);
    jmethodID m = c != NULL ? (*env)->GetMethodID(env, c, method, sig) : NULL;
    int ok = 0;
    if (m != NULL) {
        jstring text = arg != NULL ? (*env)->NewStringUTF(env, arg) : NULL;
        if (sig[strlen(sig) - 1] == 'V') {
            (*env)->CallVoidMethod(env, activity, m, text);
        } else {
            out->l = (*env)->CallObjectMethod(env, activity, m, text);
        }
        ok = !(*env)->ExceptionCheck(env);
        if (text != NULL) (*env)->DeleteLocalRef(env, text);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (c != NULL) (*env)->DeleteLocalRef(env, c);
    (*env)->DeleteLocalRef(env, activity);
    return ok;
}

void androidActivityAction(const char* method) {
    activityCall(method, "()V", NULL, NULL);
}

void androidActivityActionText(const char* method, const char* arg) {
    activityCall(method, "(Ljava/lang/String;)V", arg, NULL);
}

const char* androidActivityText(const char* method, char* out, size_t size) {
    jvalue v = {0};
    out[0] = '\0';
    if (activityCall(method, "()Ljava/lang/String;", NULL, &v) && v.l != NULL) {
        JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
        const char* s = (*env)->GetStringUTFChars(env, (jstring)v.l, NULL);
        if (s != NULL) {
            snprintf(out, size, "%s", s);
            (*env)->ReleaseStringUTFChars(env, (jstring)v.l, s);
        }
        (*env)->DeleteLocalRef(env, v.l);
    }
    return out;
}

#endif
