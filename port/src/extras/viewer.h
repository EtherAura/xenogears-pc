// The extras' viewer framework (shell.cpp): what a viewer is, and what the
// shell gives it. A viewer is one file of its own here (plus .c files for game
// code it runs); it registers itself and nothing else changes:
//
//   #include "viewer.h"
//   namespace {
//   void draw(const extras::Area& a) { ... ImGui and extras:: helpers ... }
//   const extras::Viewer viewer = {
//       "sprites", "Sprite viewer", "Field and battle sprites, frame by frame", 20,
//       opened, closed, draw, back, command};
//   }
//   XG_EXTRAS_VIEWER(viewer);
//
// The home screen lists the viewers by `order`; the planned ones that are not
// built yet (shell.cpp's list: browser 10, sprites 20, models 30, animations
// 40, maps 50, gallery 60, jukebox 70, tables 80) show as coming until a
// viewer registers their id.
//
// Each frame the shell begins the ImGui frame, draws its header and footer
// (the game's windows), and calls the open viewer's draw with the area left
// between them. Everything is in the game's look (port/src/ui/skin.h): game
// windows (pane), the dialogue font, the list pointer at the keyboard or
// gamepad cursor, the game's menu sounds (sound). Sizes are in game pixels
// times unit() (a whole number of window pixels per game pixel).
//
// Input: ImGui has the mouse, keyboard and gamepad (navigation on). Esc, B
// (gamepad east) or the footer's Back call the viewer's back(): true when it
// closed something of its own; otherwise the shell returns to the home
// screen. F1 leaves the extras.
//
// A 2D/3D picture: view.h (extras::View), PS1 primitives drawn by our Vulkan
// device into an image of the view's own, with zoom and pan. Game code: the
// C side's sandbox.h, from a .c file in this directory (the C files are part
// of the platform layer, so the game functions they call are linked).

#ifndef PORT_EXTRAS_VIEWER_H
#define PORT_EXTRAS_VIEWER_H

#include "imgui.h"
#include <cstdint>
#include <vector>
#include "navigation.h"

namespace extras {
struct Bookmark;
class View;

// The part of the window a viewer draws in this frame (ImGui coordinates:
// window pixels).
struct Area {
    ImVec2 min, max;
    float unit; // window pixels per game pixel
    ImVec2 size() const { return ImVec2(max.x - min.x, max.y - min.y); }
};

struct Viewer {
    const char* id;    // short and unique ("browser"); XG_EXTRAS_OPEN and the script's "open" use it
    const char* name;  // the home screen's entry and the header's title
    const char* blurb; // one line about it on the home screen
    int order;         // place on the home screen
    void (*opened)();  // entered (may be null)
    void (*closed)();  // left: free what it holds (may be null)
    void (*draw)(const Area& area);
    bool (*back)();    // Esc / B: true when it used it (may be null)
    // XG_EXTRAS_SCRIPT's "do <cmd> <arg>" while it is open: true when known
    // (may be null). For headless tests: select things, export, take views.
    bool (*command)(const char* cmd, const char* arg);
    // Optional audio/timer hooks for linked navigation. Suspended viewers
    // retain their state until Back, Home or leaving the extras.
    void (*suspended)() = nullptr;
    void (*resumed)() = nullptr;
    // Owned selectors for the asset shown; false when nothing is selected.
    bool (*bookmark)(Bookmark&) = nullptr;
    // The selected asset's sandbox picture (optional, no game GPU access).
    View* (*picture)() = nullptr;
    // Optional decoded RGBA cache for viewers without a primitive View.
    bool (*thumbnail)(std::vector<uint32_t>&) = nullptr;
};

struct ViewerRegistration {
    explicit ViewerRegistration(const Viewer& v);
};
// Registered engines used by the visual library. Public navigation still
// goes through follow(); ordinary section entry creates a fresh instance.
const Viewer* registeredViewer(const char* id);
bool viewerShortcuts(); // no picker, popup, active widget or text entry
bool visualReturn();
bool visualFollow(const ViewerTarget& target);
const char* visualFamily(); // current visual-library engine, or empty when closed
const char* visualTitle(); // current section's heading
void resetViewerPosition(); // fresh focus and scroll when entering a menu/viewer
#define XG_EXTRAS_VIEWER(v) static ::extras::ViewerRegistration xgExtrasViewer_##v(v)

// ---- what the shell gives viewers -------------------------------------------------------------

float unit(); // window pixels per game pixel

// A message in the footer for a few seconds (and on stderr): plain, or an
// error (red, with the menu's refusal sound).
void status(const char* fmt, ...) IM_FMTARGS(1);
void error(const char* fmt, ...) IM_FMTARGS(1);
// A line in extras.log beside settings.ini (shell.cpp): what was opened and
// what it showed, so a report that cannot be reproduced can be checked
// afterwards. For events only (an opening, a tab shown, its row counts),
// never each frame; the line before, repeated within 2 seconds, is dropped.
void logEvent(const char* fmt, ...) IM_FMTARGS(1);
// Queue the current asset and view for the shell's existing bookmark store.
void bookmarkSelection();

// A game window over [min, max] (ImGui screen coordinates) holding a child
// region (scrolls if asked); a title is drawn in its top line. endPane after.
bool beginPane(const char* id, ImVec2 min, ImVec2 max, const char* title = nullptr, bool scroll = true);
void endPane();

// A list entry as the game's menus draw them (a Selectable with the pointer
// at the cursor); hint: dimmed (unavailable). True when chosen.
bool listItem(const char* label, bool selected, bool hint = false, bool drawLabel = true);
// The main menu's unboxed command, with a ball and the red arrow.
bool commandItem(const char* label, bool selected, float width);
bool commandPopup(const char* id); // native frame; pair with ImGui::EndPopup

// The game's menu sounds (port/src/ui/sfx.h numbers: 1 move, 2 choose,
// 3 back, 4 refused).
void sound(int s);

// The extras' own label sizes: a line of the game's text, in window pixels.
float lineHeight();

// The scripted run's current frame (XG_EXTRAS_SCRIPT), for viewers that
// animate deterministically in tests; counts frames since the extras opened.
unsigned long frameNumber();

// The extras were opened over the paused game (the F1 menu), not with
// --extras.
bool fromGame();

// Queue a linked asset for the next frame, outside the caller's callback.
// Back resumes the caller with its complete state (camera, selection, zoom,
// sandbox). Targets and their command strings are copied. Cycles are refused.
bool follow(const ViewerTarget& target);
bool returnToSource();

// Leave the extras into the game (the gallery's cutscene replay, after it
// asked system/options.c for a start): with --extras the game then starts;
// from the menu, the menu closes too and the game goes on (and resets into
// the start). The viewer is open again the next time the extras open.
void leaveIntoGame();

// ---- the focused view ---------------------------------------------------------------------------
//
// A viewer lists its assets with the selected one previewed beside the list.
// Choosing one (Enter, A, a click) opens the focused view: the asset alone
// across the whole area, centred and fitted, its controls in a compact bar
// along the bottom. Back (Esc, B, a right click, the bar's Back) returns to
// the list as it was left: the same row selected, the same scroll, the cursor
// on that row (listItem puts it back; other lists call focusRestoreHere).
// Script commands are the viewer's own: "do focus" / "do focus 0" by
// convention, the asset commands unchanged.
//
//   if (S->focused) {
//       FocusLayout f = focusLayout(a, 1);
//       ... the asset in [f.min, f.max], then focusPicture(title) ...
//       if (beginFocusBar(f)) unfocus();     // true: its Back was chosen
//       if (focusButton(S->playing ? "Pause" : "Play")) ...;
//       focusZoom(view, oneToOne);
//       endFocusBar();
//       return;
//   }
struct FocusLayout {
    ImVec2 min, max;       // the asset's window
    ImVec2 barMin, barMax; // the bar under it
};
// The area split into the asset's window and a bar of `lines` rows of controls.
FocusLayout focusLayout(const Area& a, int lines = 1);
// The viewer opened its focused view (the cursor goes to the asset, else the
// bar's Back; `placeCursor` false: the viewer puts it somewhere itself), and
// closed it (the list's selected row takes the cursor back, unscrolled).
void focusEnter(bool placeCursor = true);
void focusLeave();
// The bar's game window, its Back button first: true when Back was chosen.
// Always pair with endFocusBar.
bool beginFocusBar(const FocusLayout& f);
void endFocusBar();
// The bar's controls flow along it and wrap (labels translated, ui/l10n.h).
bool focusButton(const char* label, bool enabled = true);
bool focusCheckbox(const char* label, bool* v);
void focusText(const char* fmt, ...) IM_FMTARGS(1);
void focusSpace(float width); // reserve `width` window pixels for the next item
// Fit, zoom out, zoom in and 1:1: 0 when none was chosen, else 1 fit, 2 out,
// 3 in, 4 one picture pixel per window pixel (oneToOne: false, no 1:1).
int focusZoomButtons(bool oneToOne = true);
// The same, applied to a View: `actual` is its zoom at one picture pixel per
// window pixel (0: no 1:1 button). True when the zoom changed.
bool focusZoom(View* v, float actual);
// After the asset's main item (its picture): its name over the picture's top
// left, and the cursor on it the frame the focused view opened.
void focusPicture(const char* caption);
// The list is coming back from a focused view this frame: a list that does
// not use listItem calls focusRestoreHere() right after its selected row.
bool focusReturning();
void focusRestoreHere();
// A viewer drew its focused view this frame (the footer's hints follow it).
bool focusShown();

// ---- Stock / Enhanced ----------------------------------------------------------------------------
//
// Each viewer shows its assets as the game draws them with the installed
// replacement packs (Enhanced: texture pack, sprite cels and poses, portrait
// sets) or as they are on the disc (Stock), chosen per viewer (the visual
// library's pages each on their own) for this session, never written to
// settings.ini. The first view follows the player's settings for that kind of
// asset (replace.h xgReplaceChosen: 3D textures or sprites as Cel 8x, a
// texture pack of their own; a portrait set other than Original; movie
// replacements): Enhanced where the game shows replacements, else Stock. A
// scripted run (XG_EXTRAS_SCRIPT) starts every viewer Stock, so its pictures
// stay as they were ("do look stock|enhanced|compare" chooses). Exports write
// the stock asset. The choice: the focused view's bar (focusLook: Stock /
// Enhanced), F12 (the game's replacements A/B key, replacements_ab_key) and
// Select while the open viewer offers one (the game's own A/B is not touched);
// Compare (stock | enhanced side by side) only in a viewer's options
// (lookOptions). A View draws the Enhanced look from
// view->setReplacements(lookLibraries(XG_REPLACE_...)) (replace.h: the game's
// look-ups by content key, from the view's VRAM): the packs the settings draw,
// or every installed one when the settings draw none.
enum { LOOK_STOCK = 0, LOOK_ENHANCED = 1, LOOK_COMPARE = 2, LOOK_SETTINGS = -1 /* setLook: the first view's again */ };
// What a viewer's look covers: XG_REPLACE_* (replace.h, the low byte) and these.
#define XG_LOOK_LIBRARIES 0xFFu
#define LOOK_PORTRAITS 0x100u /* the portrait sets (port/src/gpu/portrait_pack.h) */
#define LOOK_MOVIES 0x200u    /* the movie replacements (port/src/gpu/moviepack.h) */
int look(unsigned wanted);    // the open viewer's: its choice, else its first view's (above)
void setLook(int look);
// The libraries a View draws from now (none when Stock).
unsigned lookLibraries(unsigned wanted);
bool lookInstalled(unsigned wanted);
// What a picture shows of the packs (lookCovered, the controls' note).
enum { LOOK_NO_PACK = 0, LOOK_UNKNOWN = 1, LOOK_NOT_HERE = 2, LOOK_COVERED = 3 };
int lookCovered(const View* v, unsigned wanted);
// The bar's Stock / Enhanced button, or (no pack) the note `none` (nullptr: "No
// replacement pack installed"); with a note when Enhanced shows nothing replaced.
void focusLook(unsigned wanted, int covered, const char* none = nullptr);
// The same as a row of a viewer's options (a combo; Compare offered when `compare`).
void lookOptions(unsigned wanted, int covered, bool compare = false, const char* none = nullptr);
bool lookOffered(); // the open viewer showed the choice this frame or the last
// Compare: the name of the last item's half ("Stock", "Enhanced") over its bottom left.
void lookLabel(const char* label);

} // namespace extras

#endif
