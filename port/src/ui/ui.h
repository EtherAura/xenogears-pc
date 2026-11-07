#ifndef PORT_UI_H
#define PORT_UI_H


/*
 * The settings menu (port/src/ui, C++ with Dear ImGui; port/third_party/imgui).
 *
 * It looks like the game's own menu, from the game's own assets on the
 * player's disc, read when it first opens (nothing of the game is in this
 * program's files; without them it falls back to ImGui's own look):
 *   - windows: the menu's frame pieces and half-clear grey (skin.cpp, from
 *     the menu file's pictures, directory 0x10 file 1; assets.cpp);
 *   - text: the dialogue font (directory 1 file 6) in the game's text colours
 *     (fontPalettes), at a whole number of window pixels per game pixel;
 *   - cursors: the list pointer, the main menu's balls and red arrow, the
 *     pad's buttons (the menu file's sprite table);
 *   - the picture behind it darkened as the game's menu darkens it (dim.cpp);
 *   - sounds: the game's menu sounds for a move, a choice and going back,
 *     rendered from the field's sound effect bank (directory 4 file 0xA8) and
 *     its samples (directory 1 file 2) by the SPU's voices on a copy of their
 *     state (spu/menusfx.c), played on an audio stream of their own (sfx.cpp):
 *     the game's sound and its audio dump are untouched.
 * ImGui keeps the input, the layout and the widgets (menu.cpp, sections.cpp).
 *
 * Its own settings (Display section; settings.ini, XG_<KEY> overrides):
 *   menu_text   game: the game's own menu screen: letters as large as its
 *               (its 320 x 216 picture filling the window, in whole
 *               pixels), the commands, boxes and lines at its places;
 *               compact (default): smaller, at least 480 x 300 game
 *               pixels, the port's panel with the keys' legend
 *   menu_dark   1: its windows a darker half-clear grey; 0 (default): the
 *               game's own grey
 * Both apply at once.
 *
 * Opening and closing:
 *   F1                      opens it; F1 or Esc closes it (Esc when nothing in
 *                           the menu takes it: a list or a key capture)
 *   gamepad Guide (Home, PS) or L3+R3 (both sticks clicked) open and close it;
 *                           Start or the back button (from the section list)
 *                           close it too
 *
 * In it the PS1 buttons work as in the game, through their keys and gamepad
 * inputs (Controls; key_<button>, pad_<button>): the D-pad moves, the game's
 * confirm button chooses (US Cross, JP Circle), its cancel button goes back
 * (US Circle, JP Cross), L1 and R1 change the section, Start leaves; Enter,
 * Esc and the arrow keys too. The arrows alone go everywhere: Up and Down
 * move between options (and sections in the list), Right goes from the list
 * into its section, Left on an option with nothing left of it goes back to
 * the list; Left and Right inside a row move between its choices, and a
 * value changes only when chosen (a slider takes Left and Right once
 * chosen). The help line shows the buttons and their keys as bound now.
 *
 * The sections' rows are in groups, the rarely needed ones in a collapsed
 * Advanced group (options.h). A setting that differs from its default has a
 * gold label and a Reset beside it; Square (its key, or Delete) on the row
 * the cursor is on resets it too. Search (the last section; Ctrl+F from any)
 * filters every section's rows by the words typed, or shows only the changed
 * ones; System > Presets applies a set of values after listing them.
 *
 * While it is open the game is paused: the vblank that opened it does not
 * return until it closes, so no game code runs and the virtual clock stands
 * still (hw.c hwPause: the time spent in the menu is taken out of it; with
 * XG_CLOCK=step the clock does not move at all). The sound fades out and comes
 * back (spu/output.c). The menu is drawn by the present pass over the
 * presented picture (vk.c), never into VRAM or the wide picture, so the
 * game's frames (XG_DUMP_FRAMES) are the same whether it was opened or not.
 *
 * Each change goes to settings.ini at once (port/src/system/settings.h) and
 * applies at once where it can. A key set from the environment (XG_<KEY>) is
 * shown as such; changing it in the menu replaces it for the rest of the run.
 *
 * XG_UI_SCRIPT=file: the menu driven from a script, for headless runs (opened
 * by XG_PAD_SCRIPT's "<vsync> key F1"). Each line is "<frame> <command>",
 * <frame> counting the frames the menu has been open (all openings together):
 *   key <SDL key name>      press and release a key (Down, Return, Escape...)
 *   hold <key> <frames>     press a key and let it go that many frames later
 *   text <text>             type text (into the field being edited)
 *   pad <button>            press and release a gamepad button as ImGui sees
 *                           it: up down left right a b x y l1 r1 start
 *   section <name>          show a section: display audio controls gameplay
 *                           packs saves system extras search (the old
 *                           names: cards and states open Saves, about System)
 *   shot <name>             write the presented picture (the menu on it) as
 *                           <XG_DUMP_PRESENT or XG_DUMP_FRAMES dir>/<name>.png
 *   close                   close the menu
 *   background <ms>         the system's "to the background" (SDL's app
 *                           lifecycle events), and "back" ms later
 *   setup-folder <dir>      the setup screen: search a folder for images
 *   setup-bios <dir|file>   ... a BIOS image; setup-nobios: play without one
 *   setup-first <path>      select/verify the starting image in --setup
 *   setup-other <path>      select/verify the other image in --setup
 *   setup-start             save valid selections and continue
 *   mouse <x> <y>           move the mouse there (window coordinates, as SDL
 *                           reports them)
 *   click <x> <y>           and press and release the left button there
 *   rclick <x> <y>          the right button there
 *   rdown <x> <y>, rup      the right button pressed there, released (a drag:
 *                           "mouse" lines between)
 *   wheel <notches>         the mouse wheel (positive: up)
 *   vpad <button>           press and release a button of a virtual SDL
 *                           gamepad (attached when the script uses vpad): the
 *                           same path as a real one (an SDL gamepad button
 *                           name: a b x y dpup dpdown start guide leftstick...)
 * Lines "v<vblank> <command>" run at that vblank while the game runs (key,
 * vpad: "v900 vpad guide" opens the menu from the gamepad).
 *
 * XG_UI_SOUND_DUMP=dir: the menu's sounds as rendered when it first opens,
 * dir/menu_sound_<n>.raw (s16 stereo, 44.1 kHz; n as in sfx.h).
 * XG_UI_SOUND_LOG=1: a line on stderr for each sound played ("ui: sound 0x5b").
 * XG_UI_SUSPEND_LOG=1: the real time between the 30 vblanks after a pause
 * for the background, or after a second without vblanks (the system slept).
 *
 * In the background (SDL's app lifecycle events: a handheld's suspend, an
 * app switch; with pause_minimized=1 in settings.ini also a minimised
 * window) the game waits in its vblank as while this menu is open: its
 * clock stands still, the sound fades out and back, nothing is drawn.
 */

#ifdef __cplusplus
extern "C" {
#endif

union SDL_Event;

/* Before kernelMain: the setup screen (sections.cpp): the disc images (a
 * folder searched, or the images themselves) and the BIOS image (a folder
 * searched, a file, or none). Returns 1 when the player started with valid
 * images (saved to settings.ini, and the BIOS applied), 0 when they quit
 * (saved selections unchanged). region NULL allows either region (extras).
 *   UI_SETUP_NO_DISC   no image is open yet (main.c opens the chosen one:
 *                      `chosen`); the game's look is read from an image, so
 *                      without one the screen has a plain look
 *   UI_SETUP_BIOS      it is shown because no BIOS image was found: it
 *                      opens at the BIOS page */
enum { UI_SETUP_NO_DISC = 1, UI_SETUP_BIOS = 2 };
int uiDiscSetup(const char* region, int flags, char* chosen, unsigned long size);

/* port/src/system/pad.c */
void uiRequestToggle(void);             /* F1, gamepad Guide or L3+R3 */
int uiEvent(const union SDL_Event* e);  /* every event the pump takes: 1 when the menu used it */
int uiVBlank(void);                     /* after the vblank's pump: runs the menu if it was asked
                                         * for, until it closes; 1 if it ran */
int uiIsOpen(void);

/* port/src/gpu/vk.c: the device the menu draws with (given once it exists),
 * the menu's part of the present pass (cmd a VkCommandBuffer inside the
 * pass's rendering, w x h the target, format its VkFormat; nothing while the
 * menu is closed), and its end, before the device goes. */
typedef struct {
    void* getInstanceProcAddr; /* PFN_vkGetInstanceProcAddr */
    void* instance;            /* VkInstance */
    void* physical;            /* VkPhysicalDevice */
    void* device;              /* VkDevice */
    unsigned queueFamily;
    void* queue;               /* VkQueue */
} UiVulkan;
void uiVulkanReady(const UiVulkan* dev);
void uiRecord(void* cmd, int w, int h, int format, const int* movieRect);
void uiVulkanShutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_H */
