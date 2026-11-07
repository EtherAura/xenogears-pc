#ifndef PORT_EXTRAS_H
#define PORT_EXTRAS_H

/*
 * The extras (port/src/extras): an asset browser and viewers that work from
 * the player's own disc, inside the port's executable, in the game's own look
 * (port/src/ui/skin.h: the menu's windows, font, cursors and sounds).
 *
 *   xenogears --extras [disc]   starts straight into them; leaving quits
 *   F1 menu, Extras             opens them over the paused game; leaving
 *                               returns to the menu, the game still paused
 *
 * While they are open the game does not run: from the menu they run inside
 * its pause (the vblank that opened the menu has not returned), so the
 * game's frames and sound are the same as in a run that never opened them.
 * They are drawn by the present pass over the presented picture, never into
 * VRAM (extrasRecord).
 *
 * Pieces (each file's header says more):
 *   shell.cpp      the loop, the home screen, the viewer framework (viewer.h),
 *                  status messages, XG_EXTRAS_SCRIPT
 *   browser.cpp    the asset browser: both discs' file tables, kinds, hex,
 *                  LZSS, offset bundles, TIM preview and export
 *   view.cpp       a 2D/3D view: PS1 primitives drawn with our Vulkan device
 *                  into a viewer-owned image, with zoom and pan (view.h)
 *   sandbox.c      game code run outside the game: a world of its own
 *                  (modules' data, heap arena, GTE, captured GPU) (sandbox.h)
 *   vgpu.c         the sandbox's GPU: VRAM and the primitives drawn (vgpu.h)
 *   disc.c         both discs' images and file tables (disc.h)
 *   files.c        kinds, LZSS checks, bundles, TIMs (files.h)
 *   writers.c      PNG, WAV and glTF/GLB writers (writers.h)
 *   preview.c      sound previews on a sandboxed SPU (preview.h)
 *   picker.cpp     the file picker and the export folder (picker.h)
 *
 * Environment:
 *   XG_EXTRAS_SCRIPT=file   drive the extras from a script (shell.cpp)
 *   XG_EXTRAS_OPEN=id       open this viewer at once (e.g. browser)
 * settings.ini:
 *   extras_export           the export folder (default: <pref path>/exports)
 * Files beside settings.ini:
 *   settings.ini.bookmarks  the bookmarks (bookmarks.h)
 *   extras.log              one line per event: openings, viewers, library
 *                           sections, errors, the gallery's tabs and row
 *                           counts (viewer.h logEvent; extras.log.1 the one
 *                           before, past 64 KB)
 */

#include "ui/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* port/src/main.c */
void extrasInit(void); /* after portModulesInit: keeps the modules' data as loaded */
/* xenogears --extras: runs them until they are left; 1 when they were left
 * into the game (viewer.h leaveIntoGame): main.c then starts it. */
int extrasMain(void);

/* port/src/ui (the F1 menu's Extras section): runs them over the paused game
 * until they are left; the menu's ImGui context is current again after. */
void extrasOpenFromMenu(void);
int extrasIsOpen(void);
/* Private jukebox player, pumped by whichever paused menu owns the loop.
 * End before resuming the game's SPU, or shutting down the audio device. */
void extrasJukeboxTick(void);
void extrasJukeboxEnd(void);
/* After extrasOpenFromMenu: 1 once when they were left into the game (the
 * menu then closes too). */
int extrasLeftIntoGame(void);

/* port/src/gpu/vk.c, as for the settings menu (ui/ui.h): the device, the
 * extras' part of the present pass, and their end before the device's. */
void extrasVulkanReady(const UiVulkan* dev);
void extrasRecord(void* cmd, int w, int h, int format);
void extrasVulkanShutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_H */
