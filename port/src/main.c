/*
 * PC entry point: window, the platform subsystems, then the game's own entry
 * (kernelMain).
 *
 *   xenogears [disc.cue]     (default: $XG_DISC, saved disc, else the repository's
 *                             bin/Xenogears (USA) (Disc 1)/... for the build's version;
 *                             on Windows first beside xenogears.exe)
 *   xenogears --extras [disc.cue]
 *                            the extras (port/src/extras) without the game:
 *                            leaving them quits
 *   xenogears --state FILE [disc.cue]
 *                            the game from a save state (system/savestate.h)
 *   xenogears --setup [disc.cue]       verify and save the player's disc paths
 *   xenogears --verify-discs FIRST [OTHER]   read-only checks, no window/GPU
 *   xenogears --netplay host|join ... [disc.cue]
 *                            two players over the network (system/netplay.c)
 */

#include <SDL3/SDL.h>
#ifdef _WIN32
/* MinGW: a longjmp that restores registers only, as on Linux. Its default
 * unwinds the stack to setjmp's frame, which fails when startKernel runs on
 * the game's own stack or a Battling one (system/gamestack.h). */
#define __USE_MINGW_SETJMP_NON_SEH
#endif
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "cd/setup.h"
#include "cd/cd.h"
#include "cd/regional.h"
#include "system/gamedata.h"
#include "system/gamestack.h"
#include "system/savestate.h"
#include "system/rewind.h"
#include "system/settings.h"
#include "system/engine.h"
#include "system/system.h"
#include "ui/ui.h"
#ifdef VERSION_JP
#define BUILD_REGION "JP"
#else
#define BUILD_REGION "US"
#endif

void kernelMain(void);
void portModulesInit(void);
int portScratchpadInit(void);
int hwInit(void);
void hwGameStart(void);
int cdOpenDisc(const char* cuePath);
int gpuInit(SDL_Window* window);
SDL_Window* presentCreateWindow(const char* title);
void gpuShutdown(void);
void portSoftReset(void);
int cardCommand(int argc, char** argv);
void kromInit(const char* discPath);
/* port/src/extras (weak: a build may leave them out). */
void extrasInit(void) __attribute__((weak));
int extrasMain(void) __attribute__((weak));
void portStartPrepare(void); /* system/options.c: a start the extras asked for */
#include "system/netplay.h"

static jmp_buf kernelEntry;

/* The PS1 executable's entry point (0x80019524, hand-written): it clears
 * main's BSS, resets the stack and enters kernelMain. The game calls it again
 * for its soft reset (softReboot: Start+Select+L1+R1); the PC unwinds to
 * main() and does the same there (port/src/system/reset.c). */
void startKernel(void) {
    gameStackAbandon();
    longjmp(kernelEntry, 1);
}

static const char* defaultDisc(void) {
    const char* env = settingsGet("disc");
    if (env != NULL && env[0] != '\0') {
        return env;
    }
#ifdef _WIN32
    {
        /* A distributed build: the sheet beside the executable, or in a
         * bin folder there laid out like the repository's. */
#ifdef VERSION_JP
#define XG_DISC1 "Xenogears (Japan) (Disc 1)"
#else
#define XG_DISC1 "Xenogears (USA) (Disc 1)"
#endif
        static const char* const where[] = {"%s" XG_DISC1 ".cue", "%sbin\\" XG_DISC1 "\\" XG_DISC1 ".cue"};
        static char path[2048];
        const char* base = SDL_GetBasePath();
        for (int i = 0; base != NULL && i < 2; i++) {
            snprintf(path, sizeof path, where[i], base);
            FILE* f = fopen(path, "rb");
            if (f != NULL) {
                fclose(f);
                return path;
            }
        }
    }
#endif
#ifdef VERSION_JP
    return XG_ROOT "/bin/Xenogears (Japan) (Disc 1)/Xenogears (Japan) (Disc 1).cue";
#else
    return XG_ROOT "/bin/Xenogears (USA) (Disc 1)/Xenogears (USA) (Disc 1).cue";
#endif
}

/* The modules' data from the player's disc (system/gamedata.h), before
 * anything keeps or reads it. The game starts from an image of the build's
 * release (disc setup checked the region); the extras may start from either
 * release's, so for them the other configured images are tried too. */
static int loadGameData(SDL_Window* window, const char* image, int extras) {
    char why[1024], other[1024];
    if (portDataLoad(image, why, sizeof why)) {
        return 1;
    }
    const char* more[] = {extras ? cdSwapPath(1) : NULL, extras ? settingsGet("disc") : NULL,
                          extras ? defaultDisc() : NULL};
    for (int i = 0; i < 3; i++) {
        if (more[i] != NULL && more[i][0] != '\0' && SDL_strcmp(more[i], image) != 0 &&
            portDataLoad(more[i], other, sizeof other)) {
            return 1;
        }
    }
    fprintf(stderr, "xenogears: the game's data: %s\n", why);
    if (SDL_getenv("XG_HEADLESS") == NULL) {
        char message[1400];
        snprintf(message, sizeof message,
                 "%s\n\nThe game's own data is read from your %s disc image at every start. Choose that image "
                 "(xenogears --setup).",
                 why, portDataRegionName());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Xenogears", message, window);
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && SDL_strcmp(argv[1], "--verify-discs") == 0)
        return cdVerifyCommand(argc - 2, argv + 2, BUILD_REGION);
    if (argc > 1 && SDL_strcmp(argv[1], "--help") == 0) {
        puts("Usage: xenogears [--setup] [--extras] [--state FILE] [disc.cue]\n"
             "       xenogears --verify-discs FIRST.cue [OTHER.cue]\n"
             "--setup: choose, verify and save the starting and other disc\n"
             "--state FILE: start the game from a save state (F1 > Save states)\n"
             "No image argument: XG_DISC, saved disc, then the local build default");
        return 0;
    }
    /* xenogears --card ...: the memory card tool (port/src/card/mcs.c). */
    if (argc > 1 && SDL_strcmp(argv[1], "--card") == 0) {
        return cardCommand(argc - 2, argv + 2);
    }
    /* xenogears --netplay host|join ...: Battling with a player elsewhere
     * (port/src/system/netplay.c); its own arguments come off here. */
    int netplay = netplayArgs(&argc, &argv);
    if (netplay < 0) {
        return 2;
    }
    /* xenogears --extras [disc]: the extras alone (port/src/extras/extras.h). */
    int extras = 0, setup = 0;
    static const char* state; /* static: still right after the soft reset's longjmp */
    while (argc > 1 && (SDL_strcmp(argv[1], "--extras") == 0 || SDL_strcmp(argv[1], "--setup") == 0 ||
                        (SDL_strcmp(argv[1], "--state") == 0 && argc > 2))) {
        extras |= SDL_strcmp(argv[1], "--extras") == 0;
        setup |= SDL_strcmp(argv[1], "--setup") == 0;
        if (SDL_strcmp(argv[1], "--state") == 0) {
            state = argv[2];
            argc--;
            argv++;
        }
        argc--;
        argv++;
    }
    if (argc > 2 || (argc > 1 && argv[1][0] == '-')) {
        fprintf(stderr, "xenogears: unknown arguments; use --help\n"); return 2;
    }
    /* The scratchpad's fixed address before SDL and the Vulkan driver map
     * anything, including settings/pref-path allocations. */
    if (!portScratchpadInit()) {
        return 1;
    }
    /* The engine the player asked for (system/engine.h): the other executable
     * takes over here, with these arguments, when it is wanted and found. */
    engineInit(argc, argv);
    engineRedirect();
    const char* savedDisc = settingsGet("disc");
    setup |= argc == 1 && (savedDisc == NULL || savedDisc[0] == 0) && cdSetupInteractive();
    const char* disc = argc > 1 ? argv[1] : defaultDisc();
    if (netplay && !netplayConnect()) { /* the handshake; the session's options before hwInit reads them */
        return 1;
    }

    /* Gamepads through the kernel's drivers, not libusb: reading some devices'
     * descriptors through libusb blocks SDL_Init indefinitely. */
    SDL_SetHint(SDL_HINT_HIDAPI_LIBUSB, "0");
    /* Headless runs (tests, sweeps) take no gamepads: they would read the
     * controller of whoever is playing on the same machine (scripted input
     * comes from XG_PAD_SCRIPT and the UI/extras scripts instead). */
    SDL_InitFlags gamepad = SDL_getenv("XG_HEADLESS") != NULL ? 0 : SDL_INIT_GAMEPAD;
    /* The window's application id (Wayland app_id, X11 class): a compositor's
     * rules and tiling scripts can tell the game apart from SDL's default
     * "SDL_App" (Krohnkite tiled it into a third of the screen, xg-kma). */
    SDL_SetAppMetadata("Xenogears", NULL, "xenogears");
    if (!SDL_Init(SDL_INIT_VIDEO | gamepad | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    /* Size, position and fullscreen from settings.ini (port/src/gpu/present.c). */
    SDL_Window* window = presentCreateWindow("Xenogears");
    if (window == NULL) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* The starting image (cd/setup.h): valid, or the setup screen (from an
     * image found elsewhere, or with none: then before the disc opens). */
    char selected[4096];
    const char* region = extras ? NULL : BUILD_REGION;
    int first = cdSetupFirst(window, disc, region, selected);
    int setupFlags = 0;
    setup |= first == CD_SETUP_FOUND;
    if (first == 0 || !hwInit()) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (first == CD_SETUP_NONE) {
        if (!gpuInit(window)) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Xenogears",
                                     "No Xenogears disc image was found, and the setup screen cannot start "
                                     "(no Vulkan device). Run xenogears with your Disc 1 .cue file.", window);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        if (!uiDiscSetup(region, UI_SETUP_NO_DISC, selected, sizeof selected)) {
            gpuShutdown();
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 0;
        }
        setup = 0;
        engineRedirect(); /* the setup screen chose the other engine */
    }
    if (!cdOpenDisc(selected) || !loadGameData(window, selected, extras) ||
        (first != CD_SETUP_NONE && !gpuInit(window))) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    regionalInit();
    portModulesInit();
    kromInit(selected); /* the BIOS image's kanji font (system/krom.c, XG_BIOS) */
    /* No BIOS image and none declined (bios=none): the setup screen, at its
     * BIOS page, when someone is there to answer it. */
    const char* bios = settingsGet("bios");
    if (kromPath() == NULL && (bios == NULL || SDL_strcmp(bios, "none") != 0) && SDL_getenv("XG_HEADLESS") == NULL) {
        setup = 1;
        setupFlags |= UI_SETUP_BIOS;
    }
    if (setup && !uiDiscSetup(region, setupFlags, NULL, 0)) {
        gpuShutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
    /* The setup screen chose the other engine: it starts the game instead. */
    engineRedirect();
    disc = cdSwapPath(0);
    if (kromPath() == NULL && SDL_strcmp(disc, selected) != 0) {
        kromInit(disc); /* the starting image changed: its folder's BIOS */
    }
    if (extrasInit != NULL) {
        extrasInit(); /* the modules' data as loaded, for the extras' sandbox */
    }
    if (extras) {
        int game = 0;
        if (extrasMain == NULL) {
            fprintf(stderr, "xenogears: this build has no extras\n");
        } else {
            game = extrasMain(); /* 1: left into the game (a scene to replay) */
        }
        if (game != 1) {
            gpuShutdown();
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 0;
        }
        portStartPrepare();
    }

    if (setjmp(kernelEntry) != 0) {
        portSoftReset();
    }
    if (state != NULL) {
        stateRequestLoadFile(state); /* at the game's first VSync, once the boot has set the machine up */
        state = NULL;
    }
    fprintf(stderr, "xenogears: entering kernelMain\n");
    hwGameStart(); /* the boot is out of the clock: no vblank backlog at the start */
    gameStackRun(kernelMain); /* on a stack of its own (system/gamestack.h: save states) */
    fprintf(stderr, "xenogears: kernelMain returned\n");

    rewindShutdown();
    gpuShutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
