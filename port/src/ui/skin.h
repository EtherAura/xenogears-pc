// The settings menu's look (skin.cpp): the game's own menu, from its assets
// on the player's disc (assets.h). ImGui keeps the input, the layout and the
// widgets; this draws the game's windows and cursors and gives ImGui the
// game's font and colours.
//
// Sizes are in game pixels: one is unit() ImGui units (a whole number of the
// window's pixels, so every sprite and letter stays sharp; menu.cpp chooses
// it from the window's size).

#ifndef PORT_UI_SKIN_H
#define PORT_UI_SKIN_H

#include "imgui.h"

struct ImGuiWindow; // imgui_internal.h

namespace skin {

// Sprites of the menu's sprite table, and the pad's buttons (the pictures
// the table's sprites 0xF9-0xFC show, in another order in each version).
enum {
    SPRITE_CROSS = 0x1000,
    SPRITE_CIRCLE,
    SPRITE_TRIANGLE,
    SPRITE_SQUARE,
    SPRITE_ARROW = 0x108,      // the main menu's red arrow (its tip is its origin)
    SPRITE_BALL = 0x109,       // the main menu's ball, grey
    SPRITE_BALL_RED = 0x116,   // and red: the chosen entry
    SPRITE_TRACK_CAP = 0x105,  // the scroll bar: the track's ends,
    SPRITE_TRACK_EDGE = 0x106, // its middle,
    SPRITE_THUMB = 0x107,      // and the red thumb
    SPRITE_POINTER = 0x15B,    // the list pointer, five frames
    SPRITE_L1 = 0x164,
    SPRITE_R1 = 0x165,
};

// After ImGui's context exists, before its first frame: reads the assets
// (once) and adds the game's font. False: the disc gave nothing, ImGui's own
// look stays (with the game's colours where they can be).
bool init();
bool native();

// A movie's caption (cd/regional.h) over the part of its picture that is
// seen, [min, max] (screen pixels): the game's dialogue font at a whole
// scale, centred near the picture's bottom, wrapped inside it. The game's
// movies (menu.cpp uiRecord) and the extras' movie viewer draw them so.
// False (nothing drawn): no game font, no text, or a picture too small.
bool caption(ImDrawList* dl, const char* text, ImVec2 min, ImVec2 max);
// The game's dialogue font in its own colours (what caption draws with), or
// nullptr without the game's look. Its size: 14 game pixels a line.
ImFont* dialogueFont();

// The unit (ImGui units per game pixel), and the style for it.
void setUnit(float unit);
float unit();

// The largest whole number of window pixels per game pixel that leaves room
// for a 480 x 300 game-pixel panel, with a minimum of 1.
int unitFor(int w, int h);

// The menu's size (settings.ini menu_scale, percent, default 100): the
// base unit times this (menuUnitFor: what menu.cpp uses; unitFor
// stays the whole-pixel unit, as the extras use it). Read each frame.
enum { SCALE_MIN = 50, SCALE_MAX = 200, SCALE_DEFAULT = 100 };
int scalePercent();
void setScalePercent(int percent); // also written to settings.ini
float menuUnitFor(int w, int h);

// The menu's font size (settings.ini menu_font_scale, percent, default 100):
// scales the text independent of the panel's layout unit. Read each frame.
enum { FONT_SCALE_MIN = 75, FONT_SCALE_MAX = 150, FONT_SCALE_DEFAULT = 100 };
int fontScalePercent();
void setFontScalePercent(int percent); // also written to settings.ini
float fontScale();
float rowHeight(); // one option line height in ImGui units (at least 16 * unit)

// Dark windows (settings.ini menu_dark, off by default): box() fills the
// game window's inside with a darker grey; frames, letters, cursors and the
// picture behind stay as they are. Read at each box(), so a change applies
// at once.
bool darkWindows();
void setDarkWindows(bool on); // also written to settings.ini

// Long option descriptions in the message box (settings.ini menu_desc_autoscroll,
// on by default): scroll gently on their own after a pause until the right
// stick or mouse scrolls them by hand.
bool descAutoScroll();
void setDescAutoScroll(bool on);

// Mouse input in the F1 / startup UI (settings.ini menu_mouse, off by default;
// touch input still works regardless).
bool menuMouse();
void setMenuMouse(bool on);

// OS pointer visibility in the same menus, independent of their input.
enum MenuPointer : int { MENU_POINTER_AUTO, MENU_POINTER_SHOW, MENU_POINTER_HIDE, MENU_POINTER_COUNT };
extern const char* const menuPointerNames[MENU_POINTER_COUNT];
MenuPointer menuPointer();
void setMenuPointer(MenuPointer mode);
bool menuPointerVisible(bool mouseEnabled);

// Each frame, right after ImGui::NewFrame.
void beginFrame();

// A game window around [min, max] (its inside): the border pieces reach 8
// game pixels out of it, the grey background is inside.
void box(ImDrawList* dl, ImVec2 min, ImVec2 max);

// The game window around the current ImGui window (a popup, a list): its
// inside is the window, the border's glow reaches out of it. Call right
// after its Begin (it goes behind what the window holds). opens: the window
// opens as the game's message boxes do, from its middle, 32 game pixels
// wider and taller a frame, what it holds shown once it is whole
// (menu.cpp cuts it away meanwhile: hideUnopened).
void frameWindow(bool opens = false);
// For the frame's draw data: a copy of a window's list without what it
// drew after its frame while it opens (copy: original->CloneOutput()).
bool hidesContents(const ImDrawList* list);
void hideUnopened(ImDrawList* original, ImDrawList* copy);

// Field multichoice text: the selected option uses the game's
// (EF, 1E, F0) texture modulation, including its outline, without a band.
void pushOptionText(bool selected);
void popOptionText();
bool selectable(const char* label, bool selected, ImGuiSelectableFlags flags = 0, ImVec2 size = ImVec2(0, 0));
// An item inside an attached dropdown (beginCombo): no backdrop; the focused/hovered
// (or selected) item uses Xenogears purple text.
bool comboItem(const char* label, bool selected);
// A pixel-art checkbox on the game's grid: PS1 metallic bevel box with an outlined red check mark.
bool checkbox(const char* label, bool* v);

// Left and Right adjust a focused slider directly. Keep ImGui's dragging and text input.
bool sliderInt(const char* label, int* value, int min, int max, const char* format = "%d",
               ImGuiSliderFlags flags = 0, int step = 1);
bool sliderFloat(const char* label, float* value, float min, float max, const char* format, float step);

// The purple selection band behind this window's keyboard/gamepad cursor.
// Call after Begin/BeginChild and its background, before its controls.
void focusWindow();
void selection(ImDrawList* dl, ImVec2 min, ImVec2 max);

// A sprite placed by its origin, at `scale` game pixels per texel.
void sprite(ImDrawList* dl, int id, ImVec2 origin, ImU32 tint = IM_COL32_WHITE, int scale = 1);
// The same with its picture's top left at `topLeft`.
void spriteAt(ImDrawList* dl, int id, ImVec2 topLeft, ImU32 tint = IM_COL32_WHITE);
ImVec2 spriteSize(int id); // in game pixels (0 if none)

// Inline native button symbols/keycaps, sharing input's physical bindings.
const char* buttonGlyph(int glyph);
const char* actionGlyph(const char* button); // native PS1 fallback without a pad
const char* inputGlyph(int input); // selected controller profile
const char* psButtonGlyph(const char* button); // fixed PS1 binding-table identity

// The game's list scroll bar (its item and ability lists: initMenuTiles6's
// track at a window's inner left edge, sprites 0x105 and 0x106, and
// setupScrollBarThumb's red thumb, sprite 0x107): the track from `top` down
// `height` (8 game pixels wide), the thumb `thumbTop` below the track's top
// cap, `thumbHeight` long (both in ImGui units).
void scrollBar(ImDrawList* dl, ImVec2 top, float height, float thumbTop, float thumbHeight);

// The list pointer (animated) with its top left at `at`.
void pointer(ImDrawList* dl, ImVec2 at);
// A setting's value: every drop-down (and slider) the same width, as wide as fits up to that.
void setRowAvail(float avail);
float valueWidth();
// ImGui::BeginCombo as an attached single-line dropdown (default width valueWidth()),
// its chosen value drawn right-aligned beside the arrow, and its list flush beneath it.
bool beginCombo(const char* id, const char* preview, ImGuiComboFlags flags = 0, float width = 0.0f);
void endCombo();
// A settings row (options.cpp): the pointer left of its label at x while the cursor is within y0..y1.
void pointerRow(ImGuiWindow* w, float x, float y0, float y1);

// The animated Xenogears dialog/list arrow rotated to point up or down,
// centred at `center` (for off-screen list and description indicators).
void scrollArrow(ImDrawList* dl, ImVec2 center, bool up);

// End of a frame: the pointer beside the item the keyboard or gamepad
// cursor is on (not over the section list: menu.cpp draws its arrow), and
// the animation's step.
void endFrame(ImGuiID skipWindow);

// Text in the game's colours (ImGui tints the game's white letters; skin.cpp
// says where in the game each comes from).
extern const ImVec4 textHint;    // secondary text: the game's grey digits
extern const ImVec4 textWarning; // yellow
extern const ImVec4 textError;   // red
extern const ImVec4 textGood;    // green
extern const ImVec4 textDimmed;  // unavailable (ImGuiCol_TextDisabled)

} // namespace skin

#endif
