/*
 * A netplay session started from the F1 menu (Extras > Battle arena >
 * Online, port/src/ui/arena.cpp): this program again, as a process of its own
 * (`--netplay host|join ...`, netplay.c), in its own window from power-on.
 * Its state comes back on its stdout (XG_NETPLAY_STATUS=1: "XGNP ..." lines);
 * its log goes where this one's goes. The session code goes to it in its
 * environment (XG_NETPLAY_CODE), not on its command line, which other users
 * of the machine can read. The disc is the player's own (settings "disc", else
 * the image in the drive).
 *
 * The session's process gets this one's environment but for what belongs to
 * this run alone (a pad or menu script, a quit vblank, dumps, save-state
 * harness: XG_PAD_SCRIPT, XG_PAD_SCRIPT2, XG_UI_SCRIPT, XG_QUIT_AT,
 * XG_DUMP_FRAMES, XG_DUMP_RANGE, XG_AUDIO_DUMP, XG_STATE_SAVE, XG_STATE_LOAD,
 * XG_STATE_EVERY, XG_STATE_RELOAD); XG_NETPLAY_CHILD_<NAME>=value gives it
 * <NAME>=value (tests).
 *
 * Meanwhile the local game waits (netplayLaunchWait, from the vblank): its
 * clock and sound paused as for the menu, F1 still opens the menu (to see the
 * session or end it), and when the session's process ends the game goes on.
 * Closing this window ends the session's process too.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "main/game_state.h"
#include "cd/cd.h"
#include "hw.h"
#include "net.h"
#include "netplay.h"
#include "settings.h"
#include "spu/output.h"
#include "system.h"
#include "ui/ui.h"

static struct {
    SDL_Process* proc;
    int state;
    char detail[256];
    int delay;
    int paused;
    int lost;
    char line[512];
    size_t used;
} L;

static void copy(char* out, size_t size, const char* text) {
    SDL_strlcpy(out, text, size);
}

static void parse(const char* line) {
    if (SDL_strncmp(line, "XGNP ", 5) != 0) return; /* the game's own output */
    line += 5;
    if (SDL_strncmp(line, "hosting ", 8) == 0) {
        L.state = NETPLAY_HOSTING;
        copy(L.detail, sizeof L.detail, line + 8);
    } else if (SDL_strncmp(line, "joining ", 8) == 0) {
        L.state = NETPLAY_JOINING;
        copy(L.detail, sizeof L.detail, line + 8);
    } else if (SDL_strncmp(line, "connected ", 10) == 0) {
        char peer[64] = "";
        int delay = 0;
        if (sscanf(line + 10, "%63s %d", peer, &delay) >= 1) {
            L.state = NETPLAY_CONNECTED;
            copy(L.detail, sizeof L.detail, peer);
            L.delay = delay;
        }
    } else if (SDL_strcmp(line, "paused") == 0) {
        L.paused = 1;
    } else if (SDL_strcmp(line, "back") == 0) {
        L.paused = 0;
    } else if (SDL_strcmp(line, "lost") == 0) {
        L.lost = 1;
    } else if (SDL_strcmp(line, "resumed") == 0) {
        L.lost = 0;
    } else if (SDL_strncmp(line, "moved ", 6) == 0 && L.state == NETPLAY_CONNECTED) {
        copy(L.detail, sizeof L.detail, line + 6);
    } else if (SDL_strncmp(line, "over ", 5) == 0) {
        L.state = NETPLAY_OVER;
        L.paused = 0;
        L.lost = 0;
        copy(L.detail, sizeof L.detail, line + 5);
    }
}

static void readOutput(void) {
    SDL_IOStream* out = SDL_GetProcessOutput(L.proc);
    char buf[256];
    size_t n;
    while (out != NULL && (n = SDL_ReadIO(out, buf, sizeof buf)) > 0) {
        for (size_t i = 0; i < n; i++) {
            if (buf[i] == '\n' || buf[i] == '\r') {
                L.line[L.used] = '\0';
                if (L.used > 0) parse(L.line);
                L.used = 0;
            } else if (L.used + 1 < sizeof L.line) {
                L.line[L.used++] = buf[i];
            }
        }
    }
}

/* The process's news; when it has ended, its end. */
static void checkProcess(void) {
    int code = 0;
    if (L.proc == NULL) return;
    readOutput();
    if (SDL_WaitProcess(L.proc, false, &code)) {
        readOutput();
        if (L.state != NETPLAY_OVER) {
            L.state = NETPLAY_OVER;
            SDL_snprintf(L.detail, sizeof L.detail, "exit %d", code);
        }
        fprintf(stderr, "netplay: the session's process ended (%s)\n", L.detail);
        SDL_DestroyProcess(L.proc);
        L.proc = NULL;
    }
}

static void stopAtExit(void) {
    if (L.proc != NULL) SDL_KillProcess(L.proc, false); /* it tells the other player itself */
}

int netplayLaunch(const NetplayStart* o) {
    static int atExit;
    char exe[4096], port[16], delay[16], progress[16], versus[32], party[32];
    const char* args[20];
    int n = 0;

    checkProcess();
    if (L.proc != NULL || !netSelfPath(exe, sizeof exe)) return 0;
    args[n++] = exe;
    args[n++] = "--netplay";
    if (o->host) {
        SDL_snprintf(port, sizeof port, "%d", o->port);
        SDL_snprintf(delay, sizeof delay, "%d", o->delay);
        SDL_snprintf(progress, sizeof progress, "%d", o->progress);
        args[n++] = "host";
        args[n++] = "--port";
        args[n++] = port;
        args[n++] = "--delay";
        args[n++] = delay;
        args[n++] = "--progress";
        args[n++] = progress;
        if (o->versus) {
            if (o->joinerPicks) SDL_snprintf(versus, sizeof versus, "joiner");
            else SDL_snprintf(versus, sizeof versus, "%d,%d", o->table, o->config);
            SDL_snprintf(party, sizeof party, "%d,%d,%d", o->party[0], o->party[1], o->party[2]);
            args[n++] = "--versus";
            args[n++] = versus;
            args[n++] = "--party";
            args[n++] = party;
        }
    } else {
        args[n++] = "join";
        args[n++] = o->address;
        if (o->pick) {
            SDL_snprintf(versus, sizeof versus, "%d,%d", o->table, o->config);
            args[n++] = "--formation";
            args[n++] = versus;
        }
    }
    const char* disc = settingsGet("disc");
    if ((disc == NULL || disc[0] == '\0') && cdSwapPath(0) != NULL) args[n++] = cdSwapPath(0);
    args[n] = NULL;

    SDL_Environment* env = SDL_CreateEnvironment(true);
    SDL_PropertiesID props = SDL_CreateProperties();
    if (env == NULL || props == 0) {
        if (env != NULL) SDL_DestroyEnvironment(env);
        return 0;
    }
    static const char* const mine[] = {"XG_PAD_SCRIPT",  "XG_PAD_SCRIPT2", "XG_UI_SCRIPT",  "XG_QUIT_AT",
                                       "XG_DUMP_FRAMES", "XG_DUMP_RANGE",  "XG_AUDIO_DUMP", "XG_STATE_SAVE",
                                       "XG_STATE_LOAD",  "XG_STATE_EVERY", "XG_STATE_RELOAD", "XG_INVARIANT_LOG"};
    for (size_t i = 0; i < sizeof mine / sizeof mine[0]; i++) SDL_UnsetEnvironmentVariable(env, mine[i]);
    char** vars = SDL_GetEnvironmentVariables(env);
    for (int i = 0; vars != NULL && vars[i] != NULL; i++) {
        static const char prefix[] = "XG_NETPLAY_CHILD_";
        char* eq = SDL_strchr(vars[i], '=');
        if (eq != NULL && SDL_strncmp(vars[i], prefix, sizeof prefix - 1) == 0) {
            *eq = '\0';
            SDL_SetEnvironmentVariable(env, vars[i] + sizeof prefix - 1, eq + 1, true);
        }
    }
    SDL_free(vars);
    SDL_SetEnvironmentVariable(env, "XG_NETPLAY_STATUS", "1", true);
    SDL_SetEnvironmentVariable(env, "XG_ENGINE_LAUNCHED", "1", true); /* this engine, whatever the setting says */
    SDL_SetEnvironmentVariable(env, "XG_NETPLAY_CODE", o->code, true);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)args);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_INHERITED);
    L.proc = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);
    memset(L.detail, 0, sizeof L.detail);
    L.used = 0;
    L.paused = 0;
    L.lost = 0;
    L.delay = 0;
    if (L.proc == NULL) {
        L.state = NETPLAY_OVER;
        SDL_snprintf(L.detail, sizeof L.detail, "start %s", SDL_GetError());
        fprintf(stderr, "netplay: the session's process did not start: %s\n", SDL_GetError());
        return 0;
    }
    L.state = NETPLAY_STARTING;
    fprintf(stderr, "netplay: started a session's process (%s)\n", o->host ? "host" : "join");
    if (!atExit) {
        atExit = 1;
        atexit(stopAtExit);
    }
    return 1;
}

int netplaySessionState(void) {
    checkProcess();
    return L.state;
}

const char* netplaySessionDetail(void) {
    return L.detail;
}

int netplaySessionDelay(void) {
    return L.delay;
}

int netplaySessionPaused(void) {
    return L.paused;
}

int netplaySessionLost(void) {
    return L.lost;
}

void netplaySessionStop(void) {
    if (L.proc != NULL) SDL_KillProcess(L.proc, false);
}

int netplaySessionStory(void) {
    int v = gameState.m1930_fieldVarsBackup[0]; /* field variable 0: Battling's unlock levels follow it */
    return v < 0 ? 0 : v > 1000 ? 1000 : v;
}

void netplaySessionParty(int party[3]) {
    for (int i = 0; i < 3; i++) party[i] = gameState.m1D34_currentParty[i];
    if (party[0] >= 16) { /* nobody (Battling, a start without a party): Fei and Citan */
        party[0] = 0;
        party[1] = 2;
        party[2] = 255;
    }
    for (int i = 1; i < 3; i++) {
        if (party[i] >= 16) party[i] = 255;
    }
}

/* From the vblank (netplay.c netplayPace): while a session's process runs,
 * the game here waits, as in the settings menu. */
void netplayLaunchWait(void) {
    checkProcess();
    if (L.proc == NULL) return;
    fprintf(stderr, "netplay: the game waits while the online session runs\n");
    while (L.proc != NULL) {
        hwPause(1);
        spuOutputPause(1);
        padPumpEvents(); /* the window: F1, closing */
        uiVBlank();      /* F1: the menu, with the session's state */
        hwPausedFrame();
        checkProcess();
        SDL_Delay(20);
    }
    spuOutputPause(0);
    hwPause(0);
    fprintf(stderr, "netplay: the session is over (%s); the game goes on\n", L.detail);
}
