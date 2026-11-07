// The extras' file picker and export folder (picker.cpp), in the game's look.
//
// Exports go to one folder: settings.ini extras_export, by default
// <SDL pref path>/exports (~/.local/share/xenogears/exports on Linux,
// %APPDATA%\xenogears\exports on Windows), made when first used. Viewers
// name their files; exportPath gives the full path, making sub-folders.
//
// The picker is a window over the extras listing a folder (folders first,
// then files with one of the wanted extensions); a path can be typed too.
//
//   if (ImGui::Button("Open image...")) extras::openPicker("disc", "Open a disc image", start, ".cue;.bin", false);
//   std::string path;
//   if (extras::picker("disc", &path)) { ... }   // each frame; true once, when chosen

#ifndef PORT_EXTRAS_PICKER_H
#define PORT_EXTRAS_PICKER_H

#include <string>

namespace extras {

const char* exportDir();
void setExportDir(const char* path); // also written to settings.ini; "" for the default
// The full path of an export: `name` may hold '/' for sub-folders, which are
// made. Empty when the folder cannot be made.
std::string exportPath(const char* name);
bool openExportDir(); // in the system's file manager

// extensions: ';'-separated, e.g. ".cue;.bin" (empty: every file);
// folders: choose a folder instead of a file.
void openPicker(const char* id, const char* title, const char* startFolder, const char* extensions, bool folders);
bool picker(const char* id, std::string* chosen);
bool pickerOpen(); // a picker is showing (it takes Esc)

} // namespace extras

#endif
