// The settings menu's internals (port/src/ui): menu.cpp runs it (the loop,
// input, the Vulkan side, the script), sections.cpp draws its sections,
// skin.cpp and assets.cpp give it the game's look, dim.cpp darkens the
// picture behind it, sfx.cpp plays its sounds.

#ifndef PORT_UI_MENU_H
#define PORT_UI_MENU_H

#include <SDL3/SDL.h>

#include "imgui.h"

namespace menu {

// The sections on the commands' arc (user: consolidated; Memory
// cards and Save states are Saves, About is in System, the packs' rows from
// Display are Packs; Search finds a row in any of them).
enum Section { DISPLAY, AUDIO, CONTROLS, GAMEPLAY, PACKS, SAVES, SYSTEM, EXTRAS, SEARCH, SECTION_COUNT };
extern const char* const sectionNames[SECTION_COUNT];

// sections.cpp
void sectionsOpened();           // the menu opened: read what may have changed since
void drawSection(int section);   // inside the content pane
// The setup screen (ui.h uiDiscSetup): two pages on the commands' arc, and
// Start and Quit.
enum SetupSection { SETUP_DISCS, SETUP_BIOS, SETUP_SECTIONS };
extern const char* const setupSectionNames[SETUP_SECTIONS];
void discSetupOpened(const char* region, bool noDisc);
void drawSetupSection(int section); // inside the content pane
bool setupStart();               // Start: true when the images are valid and saved (and the BIOS settled)
bool discSetupCommand(const char* command, const char* argument);
bool discSetupAccepted();
const char* discSetupChosen();   // without a disc open: the starting image chosen
bool capturing();                // waiting for a key or button to bind (Esc and B are taken)
bool captureEvent(const SDL_Event* e); // while capturing: every input event; true when used
void sectionsVulkan(void* physical, void* getInstanceProcAddr, void* instance); // About's device line

// menu.cpp
float scale();                   // window pixels per game pixel (sizes are in game pixels times this)
void markBlock();                // an option's block starts here (at the cursor): scrolling shows it whole
SDL_Window* window();
void closeMenu();                // leave the menu at the end of this frame
void showSetupSection(int section); // the setup screen: that page, the cursor in it
// A setting's line under its control: the environment overrides it (XG_<KEY>),
// or a note in the hint colour.
void overrideNote(const char* key);
void hint(const char* fmt, ...) IM_FMTARGS(1);
// Text wrapped to the space left, its lines 16 game pixels apart as the
// game's (ImGui's own wrapped text has them a font height, 13, apart).
void textLines(const char* text);
// What resets the row the cursor is on, for the way of playing in use (the
// PS1 Square button's glyph or key, and Delete): the descriptions say it.
const char* resetKeysText();
// shaders.cpp: the Display section's shader preset rows (options.h rows).
void shaderPresetRows();
// movies.cpp: the Packs section's movie pack row.
void moviePackRows();
// gametext.cpp: System's game text sources.
void gameTextRows();
// states.cpp: the Saves section's save states group (system/savestate.h).
void statesSection();
// A modal popup on the game's message box (game mode) or in the middle:
// call right before its BeginPopupModal, then modalStyleEnd() after its
// EndPopup (or after the Begin when it is not open); and skin::frameWindow(true)
// right after the Begin, so it opens as the game's message box does.
void nextModal();
void modalStyleEnd();
// A table row's least height: a line of the game's lists (13 of text, 16 apart).
constexpr float ROW = 16;

} // namespace menu

#endif
