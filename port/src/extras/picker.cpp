// The extras' file picker and export folder (picker.h).

#include "picker.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "imgui.h"
#include "ui/l10n.h"
#include "ui/skin.h"
#include "viewer.h"

extern "C" {
#include "system/settings.h"
}

namespace extras {

namespace {

#ifdef _WIN32
const char SEP = '\\';
#else
const char SEP = '/';
#endif

std::string defaultExportDir() {
    char* pref = SDL_GetPrefPath(NULL, "xenogears");
    std::string dir = pref != nullptr ? std::string(pref) + "exports" : std::string("exports");
    SDL_free(pref);
    return dir;
}

struct Entry {
    std::string name;
    bool folder;
};

struct Picker {
    bool open;
    std::string id, title, extensions;
    bool folders;
    std::string folder;
    char typed[1024];
    std::vector<Entry> entries;
    int selected;
    bool focusList;
    std::string chosen;
    bool done;
};

Picker pk;

bool wanted(const std::string& name) {
    if (pk.extensions.empty()) {
        return true;
    }
    size_t start = 0;
    while (start <= pk.extensions.size()) {
        size_t end = pk.extensions.find(';', start);
        std::string ext = pk.extensions.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!ext.empty() && name.size() >= ext.size() &&
            SDL_strcasecmp(name.c_str() + name.size() - ext.size(), ext.c_str()) == 0) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

std::string join(const std::string& dir, const std::string& name) {
    if (dir.empty()) {
        return name;
    }
    return dir.back() == '/' || dir.back() == '\\' ? dir + name : dir + SEP + name;
}

std::string parentOf(const std::string& dir) {
    std::string d = dir;
    while (d.size() > 1 && (d.back() == '/' || d.back() == '\\')) {
        d.pop_back();
    }
    size_t at = d.find_last_of("/\\");
    if (at == std::string::npos) {
        return d;
    }
    return at == 0 ? d.substr(0, 1) : d.substr(0, at + (d[at - 1] == ':' ? 1 : 0));
}

void list(const std::string& folder) {
    pk.folder = folder;
    pk.entries.clear();
    pk.selected = 0;
    snprintf(pk.typed, sizeof(pk.typed), "%s", folder.c_str());
    int count = 0;
    char** names = SDL_GlobDirectory(folder.c_str(), NULL, 0, &count);
    for (int i = 0; names != nullptr && i < count; i++) {
        if (names[i][0] == '.') {
            continue; /* hidden */
        }
        SDL_PathInfo info;
        std::string path = join(folder, names[i]);
        if (!SDL_GetPathInfo(path.c_str(), &info)) {
            continue;
        }
        bool isFolder = info.type == SDL_PATHTYPE_DIRECTORY;
        if (isFolder || (!pk.folders && wanted(names[i]))) {
            pk.entries.push_back({names[i], isFolder});
        }
    }
    SDL_free(names);
    std::sort(pk.entries.begin(), pk.entries.end(), [](const Entry& a, const Entry& b) {
        return a.folder != b.folder ? a.folder : SDL_strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    pk.entries.insert(pk.entries.begin(), Entry{"..", true});
    pk.focusList = true;
}

void choose(const std::string& path) {
    pk.chosen = path;
    pk.done = true;
    pk.open = false;
    sound(2);
}

void draw() {
    float u = unit();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 size(fminf(io.DisplaySize.x - 32 * u, 520 * u), fminf(io.DisplaySize.y - 32 * u, 300 * u));
    ImGui::SetNextWindowPos(ImVec2(floorf((io.DisplaySize.x - size.x) / 2 / u) * u,
                                   floorf((io.DisplaySize.y - size.y) / 2 / u) * u));
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowFocus();
    ImGui::Begin("##picker", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    skin::frameWindow();
    ImGui::TextUnformatted(tr(pk.title.c_str())); // openPicker titles are extracted (tools/l10n.py)
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##path", pk.typed, sizeof(pk.typed), ImGuiInputTextFlags_EnterReturnsTrue)) {
        SDL_PathInfo info;
        if (SDL_GetPathInfo(pk.typed, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
            list(pk.typed);
        } else if (!pk.folders && SDL_GetPathInfo(pk.typed, &info)) {
            choose(pk.typed);
        } else {
            error("No such file or folder: %s", pk.typed);
        }
    }
    float footer = ImGui::GetFrameHeightWithSpacing() + 4 * u;
    ImGui::BeginChild("##entries", ImVec2(0, -footer), ImGuiChildFlags_None);
    for (int i = 0; i < (int)pk.entries.size(); i++) {
        const Entry& e = pk.entries[i];
        std::string label = e.folder ? e.name + (e.name == ".." ? "" : "/") : e.name;
        ImGui::PushID(i);
        if (pk.focusList && i == 0) {
            ImGui::SetKeyboardFocusHere();
            pk.focusList = false;
        }
        bool picked = ImGui::Selectable(label.c_str(), pk.selected == i, ImGuiSelectableFlags_AllowDoubleClick);
        if (ImGui::IsItemFocused()) {
            pk.selected = i;
        }
        ImGui::PopID();
        if (picked) {
            pk.selected = i;
            if (e.folder) {
                list(e.name == ".." ? parentOf(pk.folder) : join(pk.folder, e.name));
                ImGui::EndChild();
                ImGui::End();
                return;
            }
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {
                choose(join(pk.folder, e.name));
            }
        }
    }
    ImGui::EndChild();
    if (pk.folders && ImGui::Button(tr("Use this folder"))) {
        choose(pk.folder);
    }
    if (!pk.folders && pk.selected > 0 && pk.selected < (int)pk.entries.size() && !pk.entries[pk.selected].folder) {
        if (ImGui::Button(tr("Open"))) {
            choose(join(pk.folder, pk.entries[pk.selected].name));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
        pk.open = false;
        sound(3);
    }
    ImGui::End();
}

} // namespace

const char* exportDir() {
    static std::string dir;
    const char* v = settingsGet("extras_export");
    dir = v != nullptr && v[0] != '\0' ? v : defaultExportDir();
    return dir.c_str();
}

void setExportDir(const char* path) {
    settingsDescribe("extras_export", "extras_export: where the extras write their exports (empty: the default)");
    settingsSet("extras_export", path);
    settingsSave();
}

std::string exportPath(const char* name) {
    std::string path = exportDir();
    if (!SDL_CreateDirectory(path.c_str())) {
        return std::string();
    }
    std::string rest = name;
    size_t at;
    while ((at = rest.find('/')) != std::string::npos) {
        path = join(path, rest.substr(0, at));
        rest = rest.substr(at + 1);
        if (!SDL_CreateDirectory(path.c_str())) {
            return std::string();
        }
    }
    return join(path, rest);
}

bool openExportDir() {
    std::string dir = exportDir();
    SDL_CreateDirectory(dir.c_str());
    std::string url = "file://";
#ifdef _WIN32
    url += "/";
#endif
    url += dir;
    return SDL_OpenURL(url.c_str());
}

void openPicker(const char* id, const char* title, const char* startFolder, const char* extensions, bool folders) {
    pk.open = true;
    pk.done = false;
    pk.id = id;
    pk.title = title;
    pk.extensions = extensions != nullptr ? extensions : "";
    pk.folders = folders;
    std::string start = startFolder != nullptr && startFolder[0] != '\0' ? startFolder : "";
    SDL_PathInfo info;
    if (!start.empty() && SDL_GetPathInfo(start.c_str(), &info) && info.type != SDL_PATHTYPE_DIRECTORY) {
        start = parentOf(start);
    }
    if (start.empty() || !SDL_GetPathInfo(start.c_str(), &info)) {
        char* cwd = SDL_GetCurrentDirectory();
        start = cwd != nullptr ? cwd : ".";
        SDL_free(cwd);
    }
    list(start);
}

bool picker(const char* id, std::string* chosen) {
    if (pk.id != id) {
        return false;
    }
    if (pk.open) {
        draw();
    }
    if (pk.done) {
        pk.done = false;
        *chosen = pk.chosen;
        return true;
    }
    return false;
}

bool pickerOpen() {
    return pk.open;
}

} // namespace extras
