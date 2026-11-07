// The settings menu's rows (options.cpp): every section's settings as
// groups of rows, and what the menu knows about each setting beside them.
//
// A section's page is beginPage() ... endPage(); in it each group() starts
// a heading with its own two-column table, and each row() a label with its
// controls after it. row() returns false when the row is not shown (its
// group is collapsed, or the Search page's filter leaves it out): its
// controls are then skipped:
//
//     opt::beginPage("display", DISPLAY);
//     opt::group("Picture");
//     if (opt::row("Scale", "scale")) { ...controls...; hint("..."); }
//     opt::group("Advanced", opt::COLLAPSED);
//     if (opt::row("VSync wait", "vsync_wait", opt::RESTART)) { ... }
//     if (opt::plain()) { ...anything that is not a setting's row... }
//     opt::endPage();
//
// A row names the settings it shows (settings.ini keys, separated by
// spaces): those the menu knows (the table in options.cpp: the getter and
// setter of each, and its default) make the row's label gold when one
// differs from its default, with a Reset beside it (the mouse) and the
// description saying how to reset it from the keys or a gamepad (menu.cpp:
// the PS1 Square button or Delete on the row the cursor is on). Badges under
// the label: RESTART (applies at the next launch), ENHANCED (the Enhanced
// engine only).
//
// The Search page draws every section's page with a filter: a row shows when
// its label, its description (gathered from its hint() lines by an indexing
// pass over the sections, drawn into an invisible window) or one of its keys
// holds the words typed, and with "Only changed" when it differs from the
// default. Choosing a result's heading, or Enter in the search box, opens the
// row's section at it (takeJump).
//
// Presets (System > Presets): one-click sets of values for the settings the
// table knows, applied after a confirmation listing what changes; "Save my
// current setup" keeps every known setting's value in settings.ini
// (preset_mine) as one more preset.

#ifndef PORT_UI_OPTIONS_H
#define PORT_UI_OPTIONS_H

namespace menu {
namespace opt {

enum RowFlags {
    RESTART = 1,  // applies when the game starts again (badge "After restart")
    ENHANCED = 2, // the Enhanced engine only (badge "Enhanced only")
};
enum GroupFlags {
    COLLAPSED = 1, // closed until chosen (an "Advanced" group)
    STACKED = 2,   // one column: each label a line above its controls
};

// A section's page (id: an ImGui id; section: menu.h's Section).
void beginPage(const char* id, int section, bool stacked = false);
void endPage();
// A group's heading: everything after it until the next group (or the page's
// end) belongs to it. A collapsed group's heading is a line the cursor can
// choose to open and close it.
void group(const char* heading, int flags = 0);
// A setting's row: false when it is not shown (skip its controls).
bool row(const char* label, const char* keys = nullptr, int flags = 0);
// Content of a page that is not a setting's row: its table closed; true when
// it is to be drawn (never in the Search page's results, nor while indexing,
// nor in a collapsed group).
bool plain();
// hint() lines (menu.cpp): while indexing, the words of the row they describe.
void noteHint(const char* text);
// The page is being indexed for the Search page (invisibly): no popups, no
// side effects beyond drawing.
bool indexing();
// The Search page shows this page's rows (filtered).
bool filtering();

// The page's rows this frame, for menu.cpp: the row the cursor is on (by its
// screen y) reset to its defaults when `reset` (its key or button pressed).
// True when something was reset.
bool finishPage(float cursorY, bool reset);
// Whether the row the cursor was on last frame differs from its default: the footer then names the reset keys.
bool cursorRowChanged();
// A search result chosen (or Enter in the box): the section to open; the
// row is then scrolled to and its control given the cursor.
bool takeJump(int* section);

// The Search page (sections.cpp passes how to draw a section's page).
void searchPage(void (*drawPage)(int section), int sections);
// The Search page's box takes the keyboard when it is opened from a key
// (Ctrl+F, /): menu.cpp asks for it.
void focusSearch();
// The menu opened (the index is made again when a search needs it).
void opened();

// Settings the menu knows (options.cpp's table): changed from the default?
bool changed(const char* key);
void reset(const char* key);

// System > Presets: its rows.
void presetRows();

} // namespace opt
} // namespace menu

#endif
