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

// The unit (ImGui units per game pixel), and the style for it.
void setUnit(float unit);
float unit();

// The menu's text size (settings.ini menu_text): TEXT_GAME, letters as large
// as the game's own menus (the game's 320 x 216 picture filling the window),
// or TEXT_COMPACT, smaller to show more (at least 480 x 300 game pixels).
// unitFor: the whole number of window pixels per game pixel that size gives
// a window of w x h pixels (at least 1). menu.cpp uses it each frame, so a
// change applies at once.
enum TextSize { TEXT_GAME, TEXT_COMPACT };
TextSize textSize();
void setTextSize(TextSize size); // also written to settings.ini
int unitFor(int w, int h);

// The menu's size (settings.ini menu_scale, percent, default 100): the
// text size's unit times this (menuUnitFor: what menu.cpp uses; unitFor
// stays the whole-pixel unit, as the extras use it). Read each frame.
enum { SCALE_MIN = 50, SCALE_MAX = 200, SCALE_DEFAULT = 100 };
int scalePercent();
void setScalePercent(int percent); // also written to settings.ini
float menuUnitFor(int w, int h);

// Dark windows (settings.ini menu_dark, off by default): box() fills the
// game window's inside with a darker grey; frames, letters, cursors and the
// picture behind stay as they are. Read at each box(), so a change applies
// at once.
bool darkWindows();
void setDarkWindows(bool on); // also written to settings.ini

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
bool selectable(const char* label, bool selected, ImGuiSelectableFlags flags = 0, ImVec2 size = ImVec2(0, 0));

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
