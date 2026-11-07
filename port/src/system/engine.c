/*
 * The engine choice (engine.h). The two executables are the same platform
 * around two game trees, so the choice is a setting both read at start:
 *
 *   engine=original|enhanced   settings.ini (XG_ENGINE=... for one run); unset
 *                              means "this executable", so a build run on its
 *                              own never hands over
 *   XG_ENGINE_LAUNCHED=1       set in the process started by a hand-over, so
 *                              two executables never pass a start back and forth
 *
 * The other executable is looked for beside this one (SDL_GetBasePath: a
 * distributed folder holds both), then, for a build run from the repository,
 * in the sibling build directory (build/pc/release and build/pc/enhanced,
 * -jp for the JP builds). POSIX replaces the process (execv: same pid, same
 * standard streams, the harness's timeouts and signals still land); Windows
 * starts the other executable with the same arguments and inherited streams,
 * waits for it and exits with its code.
 */
#define _POSIX_C_SOURCE 200809L
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "engine.h"
#include "settings.h"

#ifndef XG_ENGINE_ENHANCED
#define XG_ENGINE_ENHANCED 0
#endif

static int argcSaved;
static char** argvSaved;
static char note[1024];
static char found[4096];

int engineThis(void) {
    return XG_ENGINE_ENHANCED ? ENGINE_ENHANCED : ENGINE_ORIGINAL;
}

const char* engineName(int e) {
    return e == ENGINE_ENHANCED ? "enhanced" : "original";
}

const char* engineTitle(int e) {
    return e == ENGINE_ENHANCED ? "Enhanced" : "Original";
}

int engineSetting(void) {
    const char* s = settingsGet("engine");
    if (s == NULL || s[0] == '\0') return -1;
    if (SDL_strcasecmp(s, "enhanced") == 0) return ENGINE_ENHANCED;
    if (SDL_strcasecmp(s, "original") == 0) return ENGINE_ORIGINAL;
    return -1;
}

void engineSetSetting(int e) {
    settingsDescribe("engine", "engine: original (the game as the PS1 ran it) or enhanced (the Enhanced engine, "
                               "xenogears-enhanced); XG_ENGINE overrides for one run");
    settingsSet("engine", engineName(e));
}

static int exists(const char* path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

const char* engineExecutable(int e) {
    const char* name = e == ENGINE_ENHANCED ? "xenogears-enhanced" : "xenogears";
#ifdef _WIN32
    const char* ext = ".exe";
#else
    const char* ext = "";
#endif
    const char* base = SDL_GetBasePath();
    if (base != NULL) {
        snprintf(found, sizeof found, "%s%s%s", base, name, ext);
        if (exists(found)) return found;
    }
#ifdef XG_ROOT
    /* Run from the repository: the sibling build directory. */
#ifdef VERSION_JP
    const char* dir = e == ENGINE_ENHANCED ? "enhanced-jp" : "release-jp";
#else
    const char* dir = e == ENGINE_ENHANCED ? "enhanced" : "release";
#endif
    snprintf(found, sizeof found, "%s/build/pc/%s/%s%s", XG_ROOT, dir, name, ext);
    if (exists(found)) return found;
#endif
    return NULL;
}

static int isDir(const char* path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

char* enginePackDir(const char* name, char* out, unsigned long size) {
    const char* base = SDL_GetBasePath();
    snprintf(out, size, "%s%s", base != NULL ? base : "", name);
    if (isDir(out)) return out;
    /* beside the other engine's executable */
    const char* other = engineExecutable(engineThis() == ENGINE_ENHANCED ? ENGINE_ORIGINAL : ENGINE_ENHANCED);
    if (other != NULL) {
        const char* slash = strrchr(other, '/');
#ifdef _WIN32
        const char* bslash = strrchr(other, '\\');
        if (bslash != NULL && (slash == NULL || bslash > slash)) slash = bslash;
#endif
        if (slash != NULL) {
            char candidate[4096];
            snprintf(candidate, sizeof candidate, "%.*s%s", (int)(slash + 1 - other), other, name);
            if (isDir(candidate)) {
                snprintf(out, size, "%s", candidate);
                return out;
            }
        }
    }
    snprintf(out, size, "%s%s", base != NULL ? base : "", name);
    return out;
}

void engineInit(int argc, char** argv) {
    argcSaved = argc;
    argvSaved = argv;
}

const char* engineNote(void) {
    return note;
}

/* Start the executable in this process's place. Returns only on failure. */
static void handOver(const char* exe) {
    fflush(stdout);
    fflush(stderr);
#ifdef _WIN32
    const char** args = calloc((size_t)argcSaved + 1, sizeof *args);
    if (args == NULL) return;
    args[0] = exe;
    for (int i = 1; i < argcSaved; i++) args[i] = argvSaved[i];
    SDL_Environment* env = SDL_CreateEnvironment(true);
    SDL_PropertiesID props = SDL_CreateProperties();
    if (env == NULL || props == 0) {
        free(args);
        return;
    }
    SDL_SetEnvironmentVariable(env, "XG_ENGINE_LAUNCHED", "1", true);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)args);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_INHERITED);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_INHERITED);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_INHERITED);
    SDL_Process* p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    free(args);
    if (p == NULL) return;
    int code = 1;
    SDL_WaitProcess(p, true, &code);
    SDL_DestroyProcess(p);
    exit(code);
#else
    char** args = calloc((size_t)argcSaved + 1, sizeof *args);
    if (args == NULL) return;
    args[0] = (char*)exe;
    for (int i = 1; i < argcSaved; i++) args[i] = argvSaved[i];
    setenv("XG_ENGINE_LAUNCHED", "1", 1);
    execv(exe, args);
    unsetenv("XG_ENGINE_LAUNCHED");
    free(args);
#endif
}

void engineRedirect(void) {
    note[0] = '\0';
    int want = engineSetting();
    if (want < 0 || want == engineThis()) return;
    if (SDL_getenv("XG_ENGINE_LAUNCHED") != NULL) {
        snprintf(note, sizeof note, "Started by the %s engine: staying here", engineTitle(engineThis()));
        fprintf(stderr, "engine: %s\n", note);
        return;
    }
    const char* exe = engineExecutable(want);
    if (exe == NULL) {
        snprintf(note, sizeof note, "The %s engine's executable (%s) was not found beside this one",
                 engineTitle(want), want == ENGINE_ENHANCED ? "xenogears-enhanced" : "xenogears");
        fprintf(stderr, "engine: %s; running the %s engine\n", note, engineTitle(engineThis()));
        return;
    }
    fprintf(stderr, "engine: starting the %s engine, %s\n", engineTitle(want), exe);
    handOver(exe);
    snprintf(note, sizeof note, "Could not start %s", exe);
    fprintf(stderr, "engine: %s; running the %s engine\n", note, engineTitle(engineThis()));
}

int engineRestart(int e) {
    const char* exe = engineExecutable(e);
    if (exe == NULL) return 0;
    engineSetSetting(e);
    settingsSave();
    fprintf(stderr, "engine: restarting into the %s engine, %s\n", engineTitle(e), exe);
    handOver(exe);
    return 0;
}
