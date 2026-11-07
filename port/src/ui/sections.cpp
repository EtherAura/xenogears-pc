// The settings menu's sections (menu.h): each control reads the setting it
// shows from its module, and a change goes through that module, which
// writes settings.ini at once and applies it where it can.

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
extern "C" int pcFreeCameraEnabled(void); /* port/src/system/freecam.c */
extern "C" void pcFreeCameraSet(int on);

#include <SDL3/SDL.h>
#include <algorithm>
#include <math.h>
#include <string>
#include <vector>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "imgui_internal.h" // FocusItem: the browser's cursor on its first entry
#include "diagnostics.h"
#include "menu.h"
#include "options.h"
#include "sfx.h"
#include "skin.h"
#include "l10n.h"
#include "ui_buildinfo.h"

// port/src/ui/diagnostics.cpp's (diagnostics.h): weak, so the menu links
// while it is not in the build (sections.cpp calls each only when linked).
void diagnosticsPerfRows() __attribute__((weak));
void diagnosticsPacksPage() __attribute__((weak));

extern "C" {
#include "card/card.h"
#include "cd/cd.h"
#include "cd/verify.h"
#include "cd/setup.h"
#include "cd/regional.h"
#include "gpu/gpu.h"
#include "gpu/interpolation.h"
#include "gpu/portrait_pack.h"
#include "pc_wide.h"
#include "spu/output.h"
#include "system/engine.h"
#include "system/pad.h"
#include "system/settings.h"
#include "system/system.h"
#include "system/gameplay.h"
#include "system/hw.h"
#include "system/speedup.h"
#include "system/battlespeed.h"
#include "system/mouse.h"
#include "system/rumble.h"
}

#define CAPTURE_TIMEOUT_NS 8000000000ull // a capture gives up after 8 s

namespace menu {

void gauntletSection(); // gauntlet.cpp: the Gameplay section's Gauntlet block
void arenaSection();    // arena.cpp: the Extras section's Battle arena block

namespace {

// ---- layout helpers ---------------------------------------------------------------------

bool checkbox(const char* label, bool* value) {
    ImGui::PushID(label);
    ImGui::BeginGroup();
    bool changed = ImGui::Checkbox("##value", value);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", tr(label));
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

// A setting's row (options.h): the label on the left, the control after
// (sizes in the game's pixels: menu.h scale()); where the pane is narrow (a
// small window), each label is a line of its own above its controls. False
// when the row is not shown (a collapsed group, the Search page's filter):
// its controls are skipped. keys: the settings it shows (changed markers,
// reset, search); flags: opt::RESTART, opt::ENHANCED.
using opt::row;

// The choices on one line, as the game's menus show them: the chosen one
// magenta, the other available choices white, and the pointer on the cursor.
int listChoice(const char* id, const char* const* names, int count, int value);
int choice(const char* id, const char* const* names, int count, int value) {
    float width = (count - 1) * 14 * scale();
    for (int i = 0; i < count; i++) width += ImGui::CalcTextSize(tr(names[i])).x;
    if (width > ImGui::GetContentRegionAvail().x) return listChoice(id, names, count, value);
    ImGui::PushID(id);
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            ImGui::SameLine(0, 14 * scale());
        }
        ImGui::AlignTextToFramePadding();
        if (skin::selectable(tr(names[i]), value == i, 0, ImGui::CalcTextSize(tr(names[i])))) {
            value = i;
        }
    }
    ImGui::PopID();
    return value;
}

// More choices than a line holds: the chosen one shown, the others in a
// drop-down list (as Aspect's).
int listChoice(const char* id, const char* const* names, int count, int value) {
    int chosen = value;
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * scale(), 6 * scale()));
    bool open = ImGui::BeginCombo(id, value >= 0 && value < count ? tr(names[value]) : tr("Custom"));
    ImGui::PopStyleVar();
    if (open) {
        skin::frameWindow();
        for (int i = 0; i < count; i++) {
            if (ImGui::Selectable(tr(names[i]), value == i)) {
                chosen = i;
            }
            if (value == i) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return chosen;
}

// The next button on this line if it fits, else on the next.
void sameLineFor(const char* label, float extra = 0) {
    float w = ImGui::CalcTextSize(tr(label), NULL, true).x + ImGui::GetStyle().FramePadding.x * 2 + extra;
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < w) {
        ImGui::NewLine();
    }
}

// A wrapped line in the game's gold.
void warning(const char* fmt, ...) IM_FMTARGS(1);
void warning(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textWarning);
    ImGui::TextWrappedV(tr(fmt), ap);
    ImGui::PopStyleColor();
    va_end(ap);
}

// A popup in the game's window, placed and opened as the game's message box
// (menu.h nextModal); its text wraps in it (the width is set).
bool beginModal(const char* name) {
    nextModal();
    bool open = ImGui::BeginPopupModal(name, NULL,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoTitleBar);
    modalStyleEnd();
    if (open) {
        skin::frameWindow(true);
    }
    return open;
}

// A table's header row as plain text: TableHeadersRow's cells take the
// cursor (they sort), which only gets in the way here.
void headers(const char* const* names, int count) {
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, ROW * scale());
    for (int i = 0; i < count; i++) {
        ImGui::TableSetColumnIndex(i);
        ImGui::TextDisabled("%s", tr(names[i]));
    }
}

// A line saying how an action went: green, or red for a failure.
struct Message {
    char text[1200];
    bool error;
    void set(bool failed, const char* fmt, ...) IM_FMTARGS(3) {
        va_list ap;
        va_start(ap, fmt);
        char log[sizeof(text)];
        va_list again;
        va_copy(again, ap);
        vsnprintf(log, sizeof(log), fmt, again); // the log in English (scripts read it)
        va_end(again);
        vsnprintf(text, sizeof(text), tr(fmt), ap); // the screen in the menu's language
        va_end(ap);
        error = failed;
        fprintf(stderr, "ui: %s\n", log);
        if (failed) {
            sfx::play(sfx::BUZZ); // the game's sound for a choice it refuses
        }
    }
    void draw() const {
        if (text[0] != '\0') {
            ImGui::PushStyleColor(ImGuiCol_Text, error ? skin::textError : skin::textGood);
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        }
    }
};

void pathText(const char* path, const char* none) {
    if (path != NULL && path[0] != '\0') {
        ImGui::TextWrapped("%s", path);
    } else {
        ImGui::TextDisabled("%s", tr(none));
    }
}

// ---- file dialogs (SDL3): their answer may come on another thread ---------------------------

struct Dialog {
    SDL_Mutex* lock;
    bool pending;
    bool ready;
    int tag;
    char path[1024];
};
Dialog dialog;

void SDLCALL dialogDone(void* user, const char* const* list, int filter) {
    (void)user;
    (void)filter;
    SDL_LockMutex(dialog.lock);
    dialog.pending = false;
    if (list != NULL && list[0] != NULL) {
        snprintf(dialog.path, sizeof(dialog.path), "%s", list[0]);
        dialog.ready = true;
    } else if (list == NULL) {
        fprintf(stderr, "ui: file dialog: %s\n", SDL_GetError());
    }
    SDL_UnlockMutex(dialog.lock);
}

// ---- the folder browser: the menu's own, so a controller can use it too -----------------

// A modal in the game's window listing a folder (its folders, then its files
// of the asked kinds), opened by Browse: choosing a folder goes into it,
// ".." up (on Windows up from a drive: the drives), choosing a file (file
// mode) or "Use this folder" (folder mode) hands the path over through
// `dialog` (browsed()), Cancel closes it. "System dialog..." opens the
// desktop's own file dialog instead (its answer comes the same way).
struct Browser {
    bool want, open, files, refocus;
    int tag;
    std::string dir;          // "" on Windows: the drives
    std::vector<std::string> folders, matches;
    std::vector<std::string> exts; // the files shown ("*": all), lower case
    const SDL_DialogFileFilter* filters;
    int filterCount;
    std::string error;
};
Browser browser;

bool separator(char c) {
    return c == '/' || c == '\\';
}

std::string joinDir(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    return separator(dir.back()) ? dir + name : dir + "/" + name;
}

SDL_EnumerationResult SDLCALL listedOne(void* user, const char* dirname, const char* fname) {
    Browser* b = (Browser*)user;
    if (fname[0] == '.') return SDL_ENUM_CONTINUE; // hidden ones
    std::string path = joinDir(dirname, fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info)) return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        b->folders.push_back(fname);
    } else if (info.type == SDL_PATHTYPE_FILE) {
        const char* dot = strrchr(fname, '.');
        for (const std::string& e : b->exts) {
            if (e == "*" || (dot != nullptr && SDL_strcasecmp(dot + 1, e.c_str()) == 0)) {
                b->matches.push_back(fname);
                break;
            }
        }
    }
    return SDL_ENUM_CONTINUE;
}

void browserList(const std::string& dir) {
    browser.dir = dir;
    browser.folders.clear();
    browser.matches.clear();
    browser.error.clear();
    browser.refocus = true;
#ifdef _WIN32
    if (dir.empty()) {
        for (char d = 'A'; d <= 'Z'; d++) {
            char root[4] = {d, ':', '\\', 0};
            SDL_PathInfo info;
            if (SDL_GetPathInfo(root, &info)) browser.folders.push_back(root);
        }
        return;
    }
#endif
    if (!SDL_EnumerateDirectory(dir.c_str(), listedOne, &browser)) {
        browser.error = SDL_GetError();
    }
    auto byName = [](const std::string& a, const std::string& b) { return SDL_strcasecmp(a.c_str(), b.c_str()) < 0; };
    std::sort(browser.folders.begin(), browser.folders.end(), byName);
    std::sort(browser.matches.begin(), browser.matches.end(), byName);
}

// The folder above `dir` ("" for the drives on Windows; "/" stays).
std::string parentDir(std::string dir) {
    while (dir.size() > 1 && separator(dir.back())) dir.pop_back();
#ifdef _WIN32
    if (dir.size() <= 2 && dir.size() >= 1 && (dir.size() < 2 || dir[1] == ':')) return "";
#endif
    size_t cut = dir.find_last_of("/\\");
    if (cut == std::string::npos) return dir;
    if (cut == 0) return "/";
#ifdef _WIN32
    if (cut == 2 && dir[1] == ':') return dir.substr(0, 3);
#endif
    return dir.substr(0, cut);
}

// Where to start: the field's folder (or the field, a folder), else home.
std::string startDir(const char* start) {
    SDL_PathInfo info;
    if (start != nullptr && start[0] != '\0') {
        std::string p = start;
        for (int up = 0; up < 8 && !p.empty(); up++) {
            if (SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY) return p;
            std::string q = parentDir(p);
            if (q == p) break;
            p = q;
        }
    }
    const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME);
    return home != nullptr ? home : "/";
}

void openBrowser(int tag, bool files, const SDL_DialogFileFilter* filters, int count, const char* start) {
    browser.want = true;
    browser.files = files;
    browser.tag = tag;
    browser.filters = filters;
    browser.filterCount = count;
    browser.exts.clear();
    for (int i = 0; files && i < count; i++) {
        std::string pattern = filters[i].pattern;
        size_t at = 0;
        while (at <= pattern.size()) {
            size_t end = pattern.find(';', at);
            std::string e = pattern.substr(at, end == std::string::npos ? std::string::npos : end - at);
            if (!e.empty() && e != "*") browser.exts.push_back(e); // ("*": the list keeps to the kinds asked for)
            if (end == std::string::npos) break;
            at = end + 1;
        }
    }
    browserList(startDir(start));
}

void browserDone(const char* path) {
    if (dialog.lock == NULL) dialog.lock = SDL_CreateMutex();
    SDL_LockMutex(dialog.lock);
    dialog.pending = false;
    if (path != nullptr) {
        snprintf(dialog.path, sizeof(dialog.path), "%s", path);
        dialog.ready = true;
    }
    SDL_UnlockMutex(dialog.lock);
}

// Ask for a file for the field `tag`; browsed() hands it over when chosen.
// Open: the menu's browser; save (a name to give): the desktop's dialog.
void browse(int tag, bool save, const SDL_DialogFileFilter* filters, int count, const char* start) {
    if (dialog.lock == NULL) {
        dialog.lock = SDL_CreateMutex();
    }
    SDL_LockMutex(dialog.lock);
    bool busy = dialog.pending;
    if (!busy) {
        dialog.pending = true;
        dialog.ready = false;
        dialog.tag = tag;
    }
    SDL_UnlockMutex(dialog.lock);
    if (busy) {
        return;
    }
    if (save) {
        SDL_ShowSaveFileDialog(dialogDone, NULL, window(), filters, count, start);
    } else {
        openBrowser(tag, true, filters, count, start);
    }
}

void browseFolder(int tag, const char* start) {
    if (dialog.lock == NULL) dialog.lock=SDL_CreateMutex();
    SDL_LockMutex(dialog.lock);
    bool busy=dialog.pending;
    if (!busy) { dialog.pending=true; dialog.ready=false; dialog.tag=tag; }
    SDL_UnlockMutex(dialog.lock);
    if (!busy) openBrowser(tag, false, nullptr, 0, start);
}

// Each frame, in the pane (sections.cpp's drawSection and the setup's).
void drawBrowser() {
    if (browser.want) {
        ImGui::OpenPopup("Browse");
        browser.want = false;
        browser.open = true;
    }
    if (!browser.open) return;
    // Game size: the game's large list box (the item list's, box 3 at 0xC,
    // 0xA, 0x124 x 0x84) grown down over the message box; compact: most
    // of the panel.
    float u = scale();
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImVec2 size, at;
    if (skin::textSize() == skin::TEXT_GAME) {
        size = ImVec2(std::min(0x124 * u, display.x - 16 * u), std::min(0xCC * u, display.y - 16 * u));
    } else {
        size = ImVec2(std::min(480 * u, display.x - 32 * u), std::min(300 * u, display.y - 32 * u));
    }
    at = ImVec2(floorf((display.x - size.x) / 2), floorf((display.y - size.y) / 2));
    ImGui::SetNextWindowPos(at, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * u, 10 * u));
    bool open = ImGui::BeginPopupModal("Browse", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    if (!open) {
        browser.open = false;
        if (dialog.pending && dialog.tag == browser.tag) browserDone(nullptr); // closed some other way
        return;
    }
    skin::frameWindow(true);
    std::string go; // a folder to list next (with goSet: "" is the drives on Windows)
    bool goSet = false;
    const char* done = nullptr;
    bool cancel = false;
    textLines(browser.dir.empty() ? tr("Drives") : browser.dir.c_str());
    float buttons = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 2 + 16 * u;
    ImGui::BeginChild("entries", ImVec2(0, std::max(32 * u, ImGui::GetContentRegionAvail().y - buttons)),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::Indent(16 * u); // room for the list pointer on the left
    int index = 0;
    auto entry = [&](const char* label) {
        bool chosen = skin::selectable(label, false);
        if (browser.refocus && index == 0) {
            ImGui::SetItemDefaultFocus();
            ImGui::FocusItem();
        }
        index++;
        return chosen;
    };
    std::string up = parentDir(browser.dir);
    if (!browser.dir.empty() && up != browser.dir && entry("..")) go = up, goSet = true;
    for (const std::string& f : browser.folders) {
        std::string label = f;
        if (!separator(label.back())) label += "/";
        if (entry(label.c_str())) go = joinDir(browser.dir, f), goSet = true;
    }
    std::string picked;
    for (const std::string& f : browser.matches) {
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textWarning); // files in the game's yellow
        if (entry(f.c_str())) picked = joinDir(browser.dir, f);
        ImGui::PopStyleColor();
    }
    if (index == 0 || (browser.folders.empty() && browser.matches.empty())) {
        hint("%s", browser.error.empty() ? (browser.files ? tr("No folders or files of this kind here.") : tr("No folders here."))
                                         : browser.error.c_str());
    }
    browser.refocus = false;
    ImGui::Unindent(16 * u);
    // Backspace: up a folder.
    if (ImGui::IsWindowFocused() && !browser.dir.empty() && up != browser.dir &&
        ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        go = up;
        goSet = true;
    }
    ImGui::EndChild();
    if (!picked.empty()) done = picked.c_str();
    std::string here = browser.dir;
    if (!browser.files) {
        ImGui::BeginDisabled(browser.dir.empty());
        if (ImGui::Button(tr("Use this folder"))) done = here.c_str();
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (ImGui::Button(tr("System dialog..."))) {
        // The desktop's own (its answer comes through dialogDone).
        ImGui::CloseCurrentPopup();
        browser.open = false;
        if (browser.files) {
            SDL_ShowOpenFileDialog(dialogDone, NULL, window(), browser.filters, browser.filterCount,
                                   here.empty() ? NULL : here.c_str(), false);
        } else {
            SDL_ShowOpenFolderDialog(dialogDone, NULL, window(), here.empty() ? NULL : here.c_str(), false);
        }
        ImGui::EndPopup();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) cancel = true;
    if (done != nullptr || cancel) {
        browserDone(done);
        ImGui::CloseCurrentPopup();
        browser.open = false;
    } else if (goSet) {
        browserList(go);
    }
    ImGui::EndPopup();
}

bool browsed(int tag, char* out, size_t size) {
    if (dialog.lock == NULL) {
        return false;
    }
    SDL_LockMutex(dialog.lock);
    bool got = dialog.ready && dialog.tag == tag;
    if (got) {
        snprintf(out, size, "%s", dialog.path);
        dialog.ready = false;
    }
    SDL_UnlockMutex(dialog.lock);
    return got;
}

bool dialogOpen() {
    return dialog.pending;
}

// wide: the path on a line of its own, the buttons under it (long paths).
bool folderField(const char* id, char* buf, size_t size, int tag, const char* use, bool wide = false) {
    ImGui::PushID(id);
    float buttons=ImGui::CalcTextSize(tr("Browse...")).x+ImGui::CalcTextSize(tr(use)).x+
        ImGui::GetStyle().FramePadding.x*4+ImGui::GetStyle().ItemSpacing.x*2;
    ImGui::SetNextItemWidth(wide ? -1.0f : -buttons);
    bool go=ImGui::InputText("##folder",buf,size,ImGuiInputTextFlags_EnterReturnsTrue);
    if (!wide) ImGui::SameLine();
    ImGui::BeginDisabled(dialogOpen());
    if (ImGui::Button(tr("Browse..."))) browseFolder(tag,buf[0]?buf:NULL);
    ImGui::EndDisabled(); ImGui::SameLine();
    go|=ImGui::Button(tr(use)); go|=browsed(tag,buf,size);
    ImGui::PopID(); return go;
}

// A path to type (keyboard) or pick (Browse: SDL's file dialog), and a button
// that uses it. True when it is to be used: the button, a pick, or (enterUses)
// Enter in the field.
bool pathField(const char* id, char* buf, size_t size, int tag, bool save, const SDL_DialogFileFilter* filters,
               int count, const char* use, bool enterUses = true, bool wide = false) {
    ImGui::PushID(id);
    float buttons = ImGui::CalcTextSize(tr("Browse...")).x + ImGui::CalcTextSize(tr(use)).x +
                    ImGui::GetStyle().FramePadding.x * 4 + ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SetNextItemWidth(wide ? -1.0f : -buttons);
    bool go = ImGui::InputText("##path", buf, size, ImGuiInputTextFlags_EnterReturnsTrue) && enterUses;
    if (!wide) ImGui::SameLine();
    ImGui::BeginDisabled(dialogOpen());
    if (ImGui::Button(tr("Browse..."))) {
        browse(tag, save, filters, count, buf[0] != '\0' ? buf : NULL);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    go |= ImGui::Button(tr(use));
    go |= browsed(tag, buf, size);
    ImGui::PopID();
    return go && buf[0] != '\0';
}

// ---- Display ----------------------------------------------------------------------------

const struct {
    const char* value;
    const char* name;
    const char* about;
} aspects[] = {
    {"4:3tv", N_("4:3 TV"), N_("The picture as a TV showed it: the 4:3 frame, Xenogears' 216 lines at their size and place.")},
    {"4:3fill", N_("4:3 fill"), N_("The picture stretched to fill 4:3 (its 216 lines drawn 11% taller).")},
    {"1:1", N_("1:1 square pixels"), N_("Each PS1 pixel square: 320x216 is 1.48:1.")},
    {"16:9", N_("16:9 wide"), N_("Widescreen: more of the scene at the sides. Movies stay 4:3.")},
    {"16:10", N_("16:10 wide"), N_("Widescreen: more of the scene at the sides. Movies stay 4:3.")},
    {"21:9", N_("21:9 wide"), N_("Widescreen: more of the scene at the sides. Movies stay 4:3.")},
    {"32:9", N_("32:9 wide"), N_("Widescreen: more of the scene at the sides. Movies stay 4:3.")},
};
#define ASPECT_COUNT (int)IM_COUNTOF(aspects)

int customW = 2, customH = 1;
bool customOpen;
Message aspectMsg;
int windowW, windowH; // the size fields, from the window when the menu opens

void readWindowSize() {
    SDL_Window* w = window();
    windowW = settingsInt("window_width", 960);
    windowH = settingsInt("window_height", 720);
    if (w != NULL && !(SDL_GetWindowFlags(w) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED))) {
        SDL_GetWindowSize(w, &windowW, &windowH);
    }
}

// Off and On in a drop-down list (the menu's style for a setting with a few
// named states); true when chosen otherwise.
bool offOnList(const char* id, bool* value, const char* off = N_("Off"), const char* on = N_("On")) {
    const char* names[] = {off, on};
    int v = listChoice(id, names, 2, *value ? 1 : 0);
    if (v == (*value ? 1 : 0) || v < 0) return false;
    *value = v == 1;
    return true;
}

void displaySection() {
    opt::beginPage("display", DISPLAY);

    // ---- what the picture is: its shape, scale, the window
    opt::group(N_("Picture"));
    const char* now = presentAspectText();
    int current = ASPECT_COUNT; // custom
    for (int i = 0; i < ASPECT_COUNT; i++) {
        if (strcmp(now, aspects[i].value) == 0) {
            current = i;
        }
    }
    if (row("Aspect", "aspect")) {
        char customName[48];
        snprintf(customName, sizeof(customName), current == ASPECT_COUNT ? tr("Custom %s") : tr("Custom w:h"), now);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * scale(), 6 * scale()));
        bool listOpen = ImGui::BeginCombo("##aspect", current < ASPECT_COUNT ? tr(aspects[current].name) : customName);
        ImGui::PopStyleVar();
        if (listOpen) {
            skin::frameWindow();
            for (int i = 0; i < ASPECT_COUNT; i++) {
                if (ImGui::Selectable(tr(aspects[i].name), current == i)) {
                    presentSetAspectText(aspects[i].value);
                    customOpen = false;
                    aspectMsg.text[0] = '\0';
                }
                if (current == i) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            if (ImGui::Selectable(customName, current == ASPECT_COUNT)) {
                customOpen = true;
            }
            ImGui::EndCombo();
        }
        if (current < ASPECT_COUNT) {
            hint("%s", tr(aspects[current].about));
        } else {
            hint("Widescreen at %s: more of the scene at the sides. Movies stay 4:3.", now);
        }
        if (customOpen || current == ASPECT_COUNT) {
            if (current == ASPECT_COUNT && !customOpen) {
                sscanf(now, "%d:%d", &customW, &customH);
                customOpen = true;
            }
            ImGui::SetNextItemWidth(68.0f * scale());
            ImGui::InputInt("##cw", &customW, 1, 4);
            ImGui::SameLine();
            ImGui::TextUnformatted(":");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(68.0f * scale());
            ImGui::InputInt("##ch", &customH, 1, 4);
            customW = customW < 1 ? 1 : customW > 100 ? 100 : customW;
            customH = customH < 1 ? 1 : customH > 100 ? 100 : customH;
            sameLineFor("Use");
            if (ImGui::Button(tr("Use##aspect"))) {
                char text[32];
                snprintf(text, sizeof(text), "%d:%d", customW, customH);
                if (presentSetAspectText(text)) {
                    aspectMsg.text[0] = '\0';
                } else {
                    aspectMsg.set(true, "%s is narrower than 4:3: wide shapes only (or 1:1)", text);
                }
            }
            aspectMsg.draw();
        }
        if (presentAspect(NULL, NULL) == PRESENT_ASPECT_WIDE) {
            hint("The paused picture stays as it was drawn; the game draws the wide picture when it goes on.");
        }
        overrideNote("aspect");
    }

    if (row("Vertical framing", "expanded_vertical")) {
        static const char* const framing[] = {"Original", "Expanded"};
        int expanded = presentExpandedVertical();
        int framingChoice = choice("expanded_vertical", framing, 2, expanded);
        if (framingChoice != expanded) presentSetExpandedVertical(framingChoice);
        hint("Expanded draws more above and below fields, battles and the world map, including menu backdrops. The title zooms to fill.");
        overrideNote("expanded_vertical");
    }

    int scaleMode = presentScaleMode();
    if (row("Scale", "scale")) {
        static const char* const scales[] = {"Fit", "Integer", "Stretch"};
        int v = choice("scale", scales, 3, scaleMode);
        if (v != scaleMode) {
            presentSetScale(v);
        }
        hint(v == 0   ? "As large as the window allows, in the aspect's shape."
             : v == 1 ? "Each PS1 pixel a whole number of window pixels: sharp and even."
                      : "The whole window, whatever its shape.");
        overrideNote("scale");
    }

    if (row("Wide fill", "widefill")) {
        static const char* const fills[] = {"Bars", "Zoom", "Blur"};
        int fill = presentFillMode();
        int v = choice("fill", fills, 3, fill);
        if (v != fill) {
            presentSetFill(v);
        }
        hint("Beside a 4:3 picture in a wide frame (movies): black bars, the picture zoomed to the frame's width "
             "(top and bottom cut), or the picture blurred.");
        overrideNote("widefill");
    }

    SDL_Window* win = window();
    bool full = win != NULL && (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN);
    if (row("Fullscreen", "fullscreen")) {
        static const char* const offOn[] = {"Off", "On"};
        if (choice("fullscreen", offOn, 2, full) != (int)full) {
            full = !full;
            presentSetFullscreen(full);
        }
        ImGui::SameLine(0, 14 * scale());
        hint("F11 or Alt+Enter too.");
        overrideNote("fullscreen");
    }

    if (row("Window size", "window_width window_height")) {
        ImGui::BeginDisabled(full);
        ImGui::SetNextItemWidth(84.0f * scale());
        ImGui::InputInt("##ww", &windowW, 16, 160);
        ImGui::SameLine();
        ImGui::TextUnformatted(tr("x"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(84.0f * scale());
        ImGui::InputInt("##wh", &windowH, 16, 160);
        windowW = windowW < 320 ? 320 : windowW > 16384 ? 16384 : windowW;
        windowH = windowH < 240 ? 240 : windowH > 16384 ? 16384 : windowH;
        sameLineFor("Set");
        if (ImGui::Button(tr("Set##size"))) {
            presentSetWindowSize(windowW, windowH);
        }
        static const int presets[][2] = {{960, 720}, {1280, 960}, {1440, 1080}, {1280, 720}, {1920, 1080}};
        for (int i = 0; i < (int)IM_COUNTOF(presets); i++) {
            char label[24];
            snprintf(label, sizeof(label), "%dx%d", presets[i][0], presets[i][1]);
            if (i > 0) {
                sameLineFor(label);
            }
            if (ImGui::Button(label)) {
                windowW = presets[i][0];
                windowH = presets[i][1];
                presentSetWindowSize(windowW, windowH);
            }
        }
        ImGui::EndDisabled();
        int pw = 0, ph = 0, lw = 0, lh = 0;
        if (win != NULL) {
            SDL_GetWindowSizeInPixels(win, &pw, &ph);
            SDL_GetWindowSize(win, &lw, &lh);
        }
        if (pw != lw) {
            hint("Now %dx%d (%dx%d pixels). A tiling desktop may size the window itself.", lw, lh, pw, ph);
        } else {
            hint("Now %dx%d. A tiling desktop may size the window itself.", lw, lh);
        }
        overrideNote("window_width");
        overrideNote("window_height");
    }

    // ---- how sharp: the resolution, colour and the filters
    opt::group(N_("Quality"));
    if (row("Resolution", "upscale")) {
        // The list: exact first, then auto, then 2x..12x (hires.c's values 1, 0, 2..12).
        static const char* const names[] = {"1x (PS1 resolution)", "Auto (from the window)", "2x", "3x", "4x",
                                            "5x", "6x", "7x", "8x", "9x", "10x", "11x", "12x"};
        int up = hiresUpscale();
        int index = up == 1 ? 0 : up == 0 ? 1 : up;
        ImGui::SetNextItemWidth(-1);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * scale(), 6 * scale()));
        bool open = ImGui::BeginCombo("##upscale", tr(names[index]));
        ImGui::PopStyleVar();
        if (open) {
            skin::frameWindow();
            for (int i = 0; i < (int)IM_COUNTOF(names); i++) {
                if (ImGui::Selectable(tr(names[i]), index == i)) {
                    hiresSetUpscale(i == 0 ? 1 : i == 1 ? 0 : i);
                }
                if (index == i) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (up == 1) {
            hint(hiresPrecision() ? "PS1 resolution, with subpixel geometry in the displayed picture."
                                  : "The PS1's picture pixel for pixel, as it always was.");
        } else {
            hint("Drawn at %dx the PS1's resolution in a copy of the picture the game never reads. Movies stay "
                 "as they are.",
                 hiresScaleNow());
            if (up == 0) {
                hint("Auto: as many times as the window's height holds PS1 lines (4x in a 1868x965 window, 9x at "
                     "4K).");
            }
        }
        overrideNote("upscale");
    }

    if (row("Colour", "upscale_color")) {
        static const char* const colours[] = {"True colour", "15-bit", "15-bit fine", "Raw 8-bit"};
        static const char* const about[] = {
            "No dither: smooth shading, as bright as the PS1's dither pattern; blending in 8 bits a colour.",
            "The PS1's 15-bit colour and its dither, each dot as large as a PS1 pixel.",
            "The PS1's 15-bit colour, dithered at the sharper resolution: finer, harder to see.",
            "No dither, all colours in 8 bits: dim glow layers add up, making dark areas brighter than the PS1.",
        };
        int c = hiresColor();
        int nc = listChoice("##upcolor", colours, IM_COUNTOF(colours), c);
        if (nc != c) {
            hiresSetColor(nc);
        }
        hint("%s%s", tr(about[nc]), hiresUpscale() == 1 && !hiresPrecision() ? tr(" (when the resolution is above 1x)") : "");
        overrideNote("upscale_color");
    }

    if (row("Filter", "present_filter")) {
        static const char* const filters[] = {"Nearest", "Linear", "Sharp", "Bicubic", "xBR (edges)"};
        static const char* const filterHints[] = {
            "Sharp pixels, as the PS1 drew them.",
            "Smoothed pixels (bilinear).",
            "Crisp pixels, all the same size at any scale: only where two meet is a window pixel blended.",
            "Smooth (bicubic); made for movies.",
            "The pixel art's edges smoothed (xBR): diagonals and curves without steps.",
        };
        int filter = presentFilter();
        int v = listChoice("##filter", filters, PRESENT_FILTER_COUNT, filter);
        if (v != filter) {
            presentSetFilter(v);
        }
        hint("%s", tr(filterHints[v]));
        if (scaleMode == 1 && (v == PRESENT_FILTER_LINEAR || v == PRESENT_FILTER_BICUBIC)) {
            hint("Integer scale keeps it sharp (nearest).");
        }
        overrideNote("present_filter");
    }

    if (row("Movies", "present_movie_filter")) {
        static const char* const movieFilters[] = {"Same as the filter", "Nearest", "Linear", "Sharp", "Bicubic",
                                                   "xBR (edges)"};
        int movie = presentMovieFilter() - PRESENT_FILTER_SAME;
        int v = listChoice("##moviefilter", movieFilters, PRESENT_FILTER_COUNT + 1, movie);
        if (v != movie) {
            presentSetMovieFilter(v + PRESENT_FILTER_SAME);
        }
        hint("For the movies: the filter above, or one of their own (bicubic suits them).");
        overrideNote("present_movie_filter");
    }

    // ---- each kind of picture's look: native, filtered, upscaled, or a pack's art
    opt::group(N_("Textures and art"));
    /* One appearance control per category: native, filter, upscale or art. */
    auto appearanceRow = [&](const char* label, const char* keys, int kind, int portrait, bool last) {
        if (!row(label, keys)) return;
        ImGui::PushID(kind);
        auto selectedSet=[&]() {return portrait==1?portraitBattlePackSelected():portrait==2?portraitMenuPackSelected():portraitPackSelected();};
        auto selectSet=[&](const char* id) {if(portrait==1)portraitBattlePackSelect(id);else if(portrait==2)portraitMenuPackSelect(id);else portraitPackSelect(id);};
        static const char* names[]={"Original","Linear","Edge","Upscaled 2x","Upscaled 4x","Texture pack","Enhanced text","Cel 2x","Cel 4x","Cel 8x"};
        int mode=portrait<0?textureAppearance(kind):textureFilter(kind);
        const char* currentName=mode>=0 && mode<10?tr(names[mode]):tr("Original");
        bool artwork=portrait>=0 && strcmp(selectedSet(),"original");
        if(artwork) {
            currentName=selectedSet();
            for(int i=1;i<portraitPackCount();i++)if(!strcmp(portraitPackSet(i)->id,selectedSet()))currentName=portraitPackSet(i)->name;
            if(!strcmp(selectedSet(),PORTRAIT_TEXTURE_PACK))currentName=tr("Texture pack");
        }
        ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("##appearance",currentName)) {
            skin::frameWindow();
            auto option=[&](int value) {
                bool selected=!artwork && mode==value;
                if(ImGui::Selectable(tr(names[value]),selected)) {
                    if(portrait>=0) {selectSet("original");textureSetFilter(kind,value);}
                    else textureSetAppearance(kind,value);
                }
                if(selected)ImGui::SetItemDefaultFocus();
            };
            for(int i=0;i<3;i++)option(i);
            if(portrait<0) {
                if(kind==PC_TEX_HUD || kind==PC_TEX_BATTLE_HUD || kind==PC_TEX_TEXT || kind==PC_TEX_ICON) {option(3);option(4);}
                option(5);if(kind==PC_TEX_TEXT)option(6);
                if(kind==PC_TEX_HUD || kind==PC_TEX_BATTLE_HUD || kind==PC_TEX_ICON){option(7);option(8);option(9);}
                if(kind==PC_TEX_SPRITE || kind==PC_TEX_3D)option(9);
            } else {
                for(int i=1;i<portraitPackCount();i++) {
                    const PortraitSet* set=portraitPackSet(i);
                    bool selected=artwork && !strcmp(set->id,selectedSet());
                    if(ImGui::Selectable(set->name,selected)) {selectSet(set->id);textureSetFilter(kind,0);}
                    if(selected)ImGui::SetItemDefaultFocus();
                }
                /* the texture pack's pictures of the faces (a Beetle PSX HW pack's, or the user's pack) */
                bool packSelected=artwork && !strcmp(selectedSet(),PORTRAIT_TEXTURE_PACK);
                if(ImGui::Selectable(tr("Texture pack"),packSelected)) {selectSet(PORTRAIT_TEXTURE_PACK);textureSetFilter(kind,0);}
                if(packSelected)ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        overrideNote(textureFilterKey(kind));
        if (last) {
            hint("Each category uses its selected filter, faithful native upscale or artwork pack. Text controls labels and numbers. Battle portraits also cover results; menu portraits reuse the dialogue artwork.");
            hint("Applies with upscaling or subpixel geometry. Missing pack images use the originals.");
            hint("Cel 2x/4x/8x uses faithful cel redraws of the original HUD and icons. Upscaled 2x/4x retains the original pixel artwork. Sprites -> Cel 8x uses the field sprite cel library (Packs > Sprite packs): redrawn sprites through the game\'s own palettes. 3D textures -> Cel 8x uses the field texture library (Packs > Texture pack): repainted scenery at 8x.");
            overrideNote("portrait_set");overrideNote("menu_portrait_set");overrideNote("battle_portrait_set");
            overrideNote("text_glyphs");
        }
    };
    appearanceRow("3D textures","texture_style_3d",PC_TEX_3D,-1,false);
    appearanceRow("Sprites","texture_style_sprite",PC_TEX_SPRITE,-1,false);
    appearanceRow("HUD","texture_style_hud",PC_TEX_HUD,-1,false);
    appearanceRow("Battle HUD","texture_style_battle_hud",PC_TEX_BATTLE_HUD,-1,false);
    appearanceRow("Text","texture_style_text",PC_TEX_TEXT,-1,false);
    appearanceRow("Battle icons","texture_style_icon",PC_TEX_ICON,-1,false);
    appearanceRow("Dialogue portraits","texture_filter_portrait portrait_set",PC_TEX_PORTRAIT,0,false);
    appearanceRow("Menu portraits","texture_filter_menu_portrait menu_portrait_set",PC_TEX_MENU_PORTRAIT,2,false);
    appearanceRow("Battle portraits","texture_filter_battle_portrait battle_portrait_set",PC_TEX_BATTLE_PORTRAIT,1,true);

    if (row("Text edges", "text_smooth")) {
        bool smoothText = textureGlyphSmooth() != 0;
        ImGui::BeginDisabled(textureAppearance(PC_TEX_TEXT) != 6);
        if (checkbox("Smooth Enhanced text", &smoothText)) textureSetGlyphSmooth(smoothText);
        ImGui::EndDisabled();
        hint("With Text -> Enhanced text: the letters' edges blend into the picture and their outline meets the face softly. Off keeps the hard edges of the 8x glyphs.");
        overrideNote("text_smooth");
    }

    // ---- what it costs: the GPU's limits (and the diagnostics' rows)
    opt::group(N_("Performance"));
    if (row("Auto GPU limit", "upscale_gpu_ms")) {
        ImGui::BeginDisabled(hiresUpscale() != 0);
        int gpuBudget = hiresGpuBudget();
        bool limitGpu = gpuBudget > 0;
        if (checkbox("Limit GPU time", &limitGpu)) {
            gpuBudget = limitGpu ? 12 : 0;
            hiresSetGpuBudget(gpuBudget);
        }
        if (limitGpu) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##gpu_budget", &gpuBudget, 1, 50, "%d ms", ImGuiSliderFlags_AlwaysClamp))
                hiresSetGpuBudget(gpuBudget);
        }
        ImGui::EndDisabled();
        hint("Auto lowers resolution after sustained GPU load and restores it when there is headroom. Off by default on a "
             "discrete GPU; 8 ms on an integrated one (handhelds, laptops), where 12x battles cannot hold the frame rate.");
        hint("The budget covers GPU work per video frame. CPU or display waits can still limit speed.");
        overrideNote("upscale_gpu_ms");
    }
    if (row("Large surfaces", "upscale_large", opt::ENHANCED)) {
        bool coarseLarge = hiresCoarseLarge();
        if (checkbox("Half-resolution large surfaces (Enhanced)", &coarseLarge)) hiresSetCoarseLarge(coarseLarge);
        hint("Arenas, Gears and screen-covering layers drawn at half the upscale; text, the HUD and small things stay at full. Cheaper on handheld GPUs; 2x2 blocks on their edges. Off by default.");
        overrideNote("upscale_large");
    }
    // ===== INTEGRATION POINT: port/src/ui/diagnostics.cpp (diagnostics.h) =====
    // The diagnostics' performance rows, in this group, outside its table; a
    // weak symbol, so the menu links without diagnostics.cpp.
    if (diagnosticsPerfRows && opt::plain()) {
        diagnosticsPerfRows();
    }
    // ===== end of the integration point =====

    // ---- rarely changed
    opt::group(N_("Advanced"), opt::COLLAPSED);
    if (row("GPU ordering", "gpu_ordered_batches")) {
        bool ordered = gpuOrderedFallback();
        if (checkbox("Exact ordered fallback", &ordered)) gpuSetOrderedFallback(ordered);
        hint("May improve speed on GPUs without hardware ordered rendering. Uses 20 MiB of GPU memory. Off by default.");
        hint("%s", gpuOrderedStatus());
        overrideNote("gpu_ordered_batches");
    }

    if (row("VSync wait", "vsync_wait")) {
        static const char* const vsyncWaits[] = {"Sleep between frames", "Spin (steady clock)"};
        int spin = hwSpinWait() ? 1 : 0;
        int spinChoice = listChoice("##vsync_wait", vsyncWaits, 2, spin);
        if (spinChoice != spin && spinChoice >= 0) hwSetSpinWait(spinChoice);
        hint("Sleep rests the game thread until its next event, so the CPU may clock down between frames. Spin keeps it busy-waiting: a steady CPU clock and vblanks delivered on time, with one core always busy and more power used.");
        overrideNote("vsync_wait");
    }

    bool lockOn = settingsInt("display_lock", 0) != 0;
    if (row("Display lock", "display_lock")) {
        if (offOnList("##display_lock", &lockOn, N_("Off"), N_("On: one vblank per refresh"))) {
            settingsDescribe("display_lock", "display_lock: 1 ties the game's vblank to the display's refresh when they "
                                             "are within display_lock_tolerance Hz (FIFO; the game that much faster or "
                                             "slower), 0 off (default)");
            settingsSetInt("display_lock", lockOn ? 1 : 0);
            settingsSave();
        }
        hint("On: when the display refreshes within the tolerance below of the game's 59.83 Hz, the game runs at the "
             "display's rate instead (at most 0.4%% faster or slower, its sound too) and each picture is shown exactly "
             "once, with no repeated or dropped frame. Adds the FIFO queue's latency. Off by default. Applies within "
             "a second.");
        overrideNote("display_lock");
    }
    if (row("Lock tolerance", "display_lock_tolerance")) {
        static const char* const names[] = {"0.01 Hz", "0.02 Hz", "0.05 Hz", "0.10 Hz", "0.15 Hz", "0.20 Hz", "0.24 Hz"};
        static const int hundredths[] = {1, 2, 5, 10, 15, 20, 24};
        const char* t = settingsGet("display_lock_tolerance");
        int nowH = t != NULL && t[0] != '\0' ? (int)lround(atof(t) * 100) : 5, at = -1;
        for (int i = 0; i < 7; i++) if (hundredths[i] == nowH) at = i;
        ImGui::BeginDisabled(!lockOn);
        int v = listChoice("##display_lock_tolerance", names, 7, at);
        ImGui::EndDisabled();
        if (v != at && v >= 0) {
            char text[16];
            snprintf(text, sizeof text, "%d.%02d", hundredths[v] / 100, hundredths[v] % 100);
            settingsSet("display_lock_tolerance", text);
            settingsSave();
        }
        hint("The largest difference between the display's refresh and the game's vblank rate that Display lock "
             "locks. 0.05 Hz by default; a display at exactly 60.00 Hz is 0.17 Hz from the game's rate, so it needs 0.20 "
             "or more.");
        overrideNote("display_lock_tolerance");
    }

    if (row("Screen effects", "upscale_cpu_effects")) {
        bool cpuEffects = hiresCpuEffects();
        if (checkbox("High-resolution pause blur", &cpuEffects)) hiresSetCpuEffects(cpuEffects);
        hint("Keeps rendered detail through the Gear arena pause blur above 1x. Uses the original blur radius and colour steps. Off by default.");
        overrideNote("upscale_cpu_effects");
    }

    if (row("Geometry", "geometry_precision perspective_textures geometry_slivers geometry_seam_guard")) {
        bool precise = hiresPrecision();
        if (checkbox("Subpixel geometry", &precise)) hiresSetPrecision(precise);
        hint("Smooths projected vertices as the camera moves. Enabled by default with upscaling.");
        ImGui::BeginDisabled(!precise);
        bool perspective = hiresPerspective();
        if (checkbox("Perspective textures", &perspective)) hiresSetPerspective(perspective);
        hint("Corrects texture swim on 3D surfaces. Screen-space sprites and menus keep their original mapping.");
        bool slivers = hiresSlivers();
        if (checkbox("Restore thin surfaces", &slivers)) hiresSetSlivers(slivers);
        hint("Keeps thin terrain, shadows and custom surfaces visible when PS1 pixel rounding would remove them. Off by default.");
        bool seamGuard = hiresSeamGuard();
        if (checkbox("Close surface gaps", &seamGuard)) hiresSetSeamGuard(seamGuard);
        ImGui::EndDisabled();
        hint("Slightly overlaps opaque edges that met through PS1 pixel rounding.");
        overrideNote("geometry_precision");
        overrideNote("perspective_textures");
        overrideNote("geometry_seam_guard");
        overrideNote("geometry_slivers");
    }

    shaderPresetRows(); // shaders.cpp: Shader, Movie shader and their parameters

    opt::endPage();
}

// ---- Packs ------------------------------------------------------------------------------

// A pack library's folder kept in settings.ini as it is given ("none": no
// library; unset: the default folder beside the executable), read again with
// the packs (texturePackReload).
void libraryRow(const char* label, const char* key, int tag, const char* defaultName, const char* about) {
    if (!row(label, key)) return;
    static std::string texts[8];
    static bool loaded[8];
    int slot = tag % 8;
    char buf[3072];
    if (!loaded[slot]) {
        const char* v = settingsGet(key);
        texts[slot] = v != NULL ? v : "";
        loaded[slot] = true;
    }
    snprintf(buf, sizeof buf, "%s", texts[slot].c_str());
    ImGui::PushID(key);
    if (folderField(key, buf, sizeof buf, tag, "Use") && buf[0] != '\0') {
        settingsSet(key, buf);
        settingsSave();
        texturePackReload();
    }
    texts[slot] = buf;
    if (ImGui::Button(tr("None"))) {
        settingsSet(key, "none");
        settingsSave();
        texts[slot] = "none";
        texturePackReload();
    }
    ImGui::PopID();
    const char* v = settingsGet(key);
    if (v == NULL) hint("Now: %s (the default).", tr(defaultName));
    else if (SDL_strcasecmp(v, "none") == 0) hint("Now: none, the game's own.");
    else hint("Now: %s", v);
    hint("%s", tr(about));
    overrideNote(key);
}

void packsSection() {
    opt::beginPage("packs", PACKS);
    opt::group(N_("Pack folders"));
    if (row("Texture pack", "texture_pack")) {
        static char packPath[3072];
        static bool texturePathsLoaded;
        if (!texturePathsLoaded) {
            snprintf(packPath,sizeof(packPath),"%s",texturePackRoot());
            texturePathsLoaded=true;
        }
        if (folderField("texture_pack",packPath,sizeof(packPath),901,"Use")) {textureFreezeAppearances();texturePackSetRoot(packPath);}
        if (ImGui::Button(tr("Reload pack"))) texturePackReload();
        ImGui::SameLine();
        if (ImGui::Button(tr("Disable pack"))) { texturePackSetRoot(""); packPath[0]=0; }
        TexturePackStats pack;
        texturePackStats(&pack);
        if (pack.name[0]) ImGui::TextWrapped("%s%s%s",pack.name,pack.author[0]?tr(" by "):"",pack.author);
        hint("%s: %s",texturePackVersion(),pack.message);
        if (pack.enabled) hint("%llu PNG loads, %llu replacement draws, %llu missing keys, %llu invalid files; %.1f MiB cached",
            (unsigned long long)pack.loads,(unsigned long long)pack.draws,(unsigned long long)pack.misses,
            (unsigned long long)pack.invalid,(double)pack.bytes/(1024*1024));
        hint("Choose the folder containing %s. Packs apply with upscaling or subpixel geometry. Reload after editing PNGs. Edge keeps authored detail above 1x. Display > Textures and art chooses where each kind of picture uses it.",texturePackVersion());
        overrideNote("texture_pack");
    }

    /* A Beetle PSX HW replacement folder (the community's HD packs), beside the texture pack. */
    if (row("Beetle PSX pack", "beetle_pack beetle_pack_order")) {
        static char beetlePath[3072];
        static bool beetlePathLoaded;
        if (!beetlePathLoaded) {
            snprintf(beetlePath,sizeof(beetlePath),"%s",texturePackBeetleRoot());
            beetlePathLoaded=true;
        }
        if (folderField("beetle_pack",beetlePath,sizeof(beetlePath),908,"Use")) {textureFreezeAppearances();texturePackBeetleSetRoot(beetlePath);}
        if (ImGui::Button(tr("Reload##beetle"))) texturePackReload();
        ImGui::SameLine();
        if (ImGui::Button(tr("Disable##beetle"))) { texturePackBeetleSetRoot(""); beetlePath[0]=0; }
        static const char* const orders[]={"Our texture pack first","Beetle pack first"};
        ImGui::SetNextItemWidth(-1);
        int first=texturePackBeetleFirst();
        int v=listChoice("##beetle_order",orders,2,first);
        if (v!=first) texturePackBeetleSetFirst(v);
        BeetlePackStats beetle;
        texturePackBeetleStats(&beetle);
        hint("%s",beetle.message);
        if (beetle.enabled) hint("%llu PNG loads, %llu missing, %llu invalid files since the last reload",
            (unsigned long long)beetle.loads,(unsigned long long)beetle.misses,(unsigned long long)beetle.invalid);
        hint("A RetroArch Beetle PSX HW replacement folder (<game>-texture-replacements, its <upload>-<palette>.png files). It applies where Display > Textures and art uses the texture pack (portraits: their Texture pack choice). Textures already in VRAM when it is chosen show it once the game loads them again.");
        overrideNote("beetle_pack");
        overrideNote("beetle_pack_order");
    }

    if (row("HUD packs", "ui_packs")) {
        static char celPath[3072];static bool celPathLoaded;
        if(!celPathLoaded){snprintf(celPath,sizeof(celPath),"%s",textureCelRoot());celPathLoaded=true;}
        if(folderField("ui_packs",celPath,sizeof(celPath),905,"Use"))textureCelSetRoot(celPath);
        if(ImGui::Button(tr("Reload HUD artwork")))textureCelReload();
        hint("%s",textureCelStatus());overrideNote("ui_packs");
    }

    if (row("Portrait packs", "portrait_packs")) {
        static char portraitPath[3072];
        static bool portraitPathLoaded;
        if (!portraitPathLoaded) {
            snprintf(portraitPath,sizeof(portraitPath),"%s",portraitPackRoot());
            portraitPathLoaded=true;
        }
        if (folderField("portrait_packs",portraitPath,sizeof(portraitPath),904,"Use")) portraitPackSetRoot(portraitPath);
        if(ImGui::Button(tr("Reload portrait sets"))) portraitPackReload();
        PortraitPackStats portraits; portraitPackStats(&portraits);
        hint("%s",tr(portraits.message));
        hint("Display > Textures and art selects dialogue, menu and battle artwork independently. Original is the default for each.");
        hint("Menu portraits: %s",tr(portraits.menuMessage));
        hint("Battle portraits: %s",tr(portraits.battleMessage));
        if (portraits.enabled || portraits.menuEnabled || portraits.battleEnabled)
            hint("Since the last reload, replacements drawn / pictures missing: dialogue %llu / %llu, menu %llu / %llu, menu 24 px faces %llu / %llu, battle %llu / %llu",
                 (unsigned long long)portraits.kindDraws[PORTRAIT_DIALOGUE],(unsigned long long)portraits.kindMisses[PORTRAIT_DIALOGUE],
                 (unsigned long long)portraits.kindDraws[PORTRAIT_MENU],(unsigned long long)portraits.kindMisses[PORTRAIT_MENU],
                 (unsigned long long)portraits.kindDraws[PORTRAIT_MENU_THUMB],(unsigned long long)portraits.kindMisses[PORTRAIT_MENU_THUMB],
                 (unsigned long long)portraits.kindDraws[PORTRAIT_BATTLE],(unsigned long long)portraits.kindMisses[PORTRAIT_BATTLE]);
        overrideNote("portrait_packs"); overrideNote("portrait_set"); overrideNote("battle_portrait_set");
    }

    libraryRow("Sprite packs", "sprite_packs", 906, "sprite-packs beside the executable",
               "The field sprite cel library (Display > Textures and art > Sprites -> Cel 8x): a folder holding a "
               "version folder (SLUS-00664 or SLPS-01160). None keeps the game's sprites. Read again at once.");
    libraryRow("Glyph packs", "glyph_packs", 907, "glyph-packs beside the executable",
               "The HD lettering library (Display > Textures and art > Text -> Enhanced text): a folder holding a "
               "version folder. None keeps the game's own letters. Read again at once.");

    moviePackRows(); // movies.cpp: Pack movies

    opt::group(N_("Advanced"), opt::COLLAPSED);
    if (row("Sprite preload", "sprite_preload")) {
        const char* p = settingsGet("sprite_preload");
        bool preload = !(p != NULL && strcmp(p, "0") == 0);
        if (offOnList("##sprite_preload", &preload, N_("At their first draw"), N_("While the room loads"))) {
            settingsSetInt("sprite_preload", preload ? 1 : 0);
            settingsSave();
            texturePackReload();
        }
        hint("A room's sprite pack cels and poses, and the party's poses, decoded in the background as the room "
             "loads (the default), or each when it is first drawn (less memory, a hitch where a new one appears). "
             "Reloads the packs.");
        overrideNote("sprite_preload");
    }
    if (row("Pack GPU memory", "texture_pack_gpu_mb", opt::RESTART)) {
        static const char* const names[] = {"256 MB", "512 MB", "1 GB", "2 GB", "4 GB"};
        static const int mb[] = {256, 512, 1024, 2048, 4096};
        int nowMb = settingsInt("texture_pack_gpu_mb", 512), at = -1;
        for (int i = 0; i < 5; i++) if (mb[i] == nowMb) at = i;
        int v = listChoice("##texture_pack_gpu_mb", names, 5, at);
        if (v != at && v >= 0) {
            settingsSetInt("texture_pack_gpu_mb", mb[v]);
            settingsSave();
        }
        hint("The GPU memory texture pack images may take (at most a quarter of the GPU's). 512 MB by default; "
             "more keeps more of a large pack ready, less leaves room on a small or shared GPU.");
        overrideNote("texture_pack_gpu_mb");
    }
    if (row("Discovery", "texture_dump")) {
        static char dumpPath[3072];
        static bool dumpPathLoaded;
        if (!dumpPathLoaded) {
            snprintf(dumpPath,sizeof(dumpPath),"%s",textureDumpRoot());
            dumpPathLoaded=true;
        }
        if (folderField("texture_dump",dumpPath,sizeof(dumpPath),902,"Start")) textureDumpSetRoot(dumpPath);
        ImGui::BeginDisabled(!textureDumpEnabled());
        if (ImGui::Button(tr("Stop discovery"))) textureDumpSetRoot("");
        ImGui::EndDisabled();
        hint("%s",textureDumpStatus());
        hint("Writes original textures and stable filenames into a version folder for pack editing. RGBA images match one palette; index maps follow the game's live palette.");
        overrideNote("texture_dump");
    }

    // ===== INTEGRATION POINT: port/src/ui/diagnostics.cpp (diagnostics.h) =====
    // The diagnostics' packs page: every library, what it holds, on and off,
    // reload. A weak symbol, so the menu links without diagnostics.cpp.
    if (diagnosticsPacksPage) {
        opt::group(N_("Diagnostics"));
        if (opt::plain()) diagnosticsPacksPage();
    }
    // ===== end of the integration point =====
    opt::endPage();
}

// ---- Audio ------------------------------------------------------------------------------------

char japanesePaths[2][4096];
bool japanesePathsLoaded;
Message regionalMsg;
const SDL_DialogFileFilter japaneseFilters[] = {{"Japanese Xenogears discs", "cue;bin;img"}, {"All files", "*"}};

// A slider of milliseconds kept in settings.ini (the output reads it at once).
void audioSlider(const char* id, const char* key, int def, int lo, int hi) {
    int ms = settingsInt(key, def);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt(id, &ms, lo, hi, "%d ms")) {
        settingsSetInt(key, ms);
        spuOutputApplySettings();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        settingsSave();
    }
}

void audioSection() {
    if (!japanesePathsLoaded) {
        for (int disc = 1; disc <= 2; ++disc)
            snprintf(japanesePaths[disc-1], sizeof japanesePaths[0], "%s", regionalSourcePath(disc));
        japanesePathsLoaded = true;
    }
    opt::beginPage("audio", AUDIO);
    opt::group(N_("Sound"));
    if (row("Volume", "volume")) {
        int volume = settingsInt("volume", 100);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##volume", &volume, 0, 100, "%d%%")) {
            settingsSetInt("volume", volume);
            spuOutputApplySettings();
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            settingsSave();
        }
        hint("The game's sound is paused while this menu is open: a change is heard when it goes on.");
        hint("Sliders: choose one, then ← and →; choose or back again when done.");
        overrideNote("volume");
    }

    if (row("Latency", "audio_latency")) {
        audioSlider("##latency", "audio_latency", 40, 10, 250);
        hint("How far ahead the sound is queued for the device: lower answers sooner, higher is safer if the sound "
             "crackles. 40 ms by default.");
        overrideNote("audio_latency");
    }

    opt::group(N_("Japanese audio"));
    bool battle = settingsBool("japanese_battle_voices", 0);
    bool movies = regionalMovieEnabled();
    if (row("Battle voices", "japanese_battle_voices", opt::RESTART)) {
        if (checkbox("Japanese battle voices", &battle)) regionalSetBattle(battle);
        hint("Uses your Japanese discs. Restart the game after changing this option.");
        if (battle != (regionalBattleEnabled() != 0)) warning("Saved for the next launch.");
        overrideNote("japanese_battle_voices");
    }
    if (row("Cutscene audio", "japanese_cutscene_audio")) {
        if (checkbox("Japanese cutscene audio", &movies)) regionalSetMovie(movies);
        hint("Applies to the next cutscene. Keeps this version's video and text.");
        overrideNote("japanese_cutscene_audio");
    }
    if (row("Subtitles", "cutscene_subtitles")) {
        const char* subtitleTracks[] = {"Off", "US dialogue", "Japanese translation"};
        int captions = regionalSubtitlesEnabled();
        int captionChoice = choice("subtitles", subtitleTracks, 3, captions);
        if (captionChoice != captions) regionalSetSubtitles(captionChoice);
        hint("Independent of audio language. Uses the in-game dialogue font. Text tracks are drafts under review.");
        overrideNote("cutscene_subtitles");
    }
    for (int disc = 1; disc <= 2; ++disc) {
        if (!row(disc == 1 ? "Japanese Disc 1" : "Japanese Disc 2", disc == 1 ? "japanese_disc1" : "japanese_disc2")) {
            continue;
        }
        ImGui::PushID(disc);
        if (pathField("japanese-image", japanesePaths[disc-1], sizeof japanesePaths[0], 60+disc,
                      false, japaneseFilters, 2, "Use")) {
            char why[1200];
            bool ok = regionalSetSource(disc, japanesePaths[disc-1], why, sizeof why);
            regionalMsg.set(!ok, "%s", why);
        }
        if (battle || movies) hint("%s", regionalSourceStatus(disc));
        hint("Your Japanese disc's image, for its battle voices and cutscene audio.");
        if (disc == 2) regionalMsg.draw();
        ImGui::PopID();
    }

    opt::group(N_("Advanced"), opt::COLLAPSED);
    if (row("Latency ceiling", "audio_latency_max")) {
        audioSlider("##latency_max", "audio_latency_max", 150, 40, 500);
        hint("How far the queue may grow by itself after the sound ran dry (an underrun): it grows a little each "
             "time, up to this, then shrinks back to Latency. 150 ms by default; higher for a busy machine or "
             "streaming software.");
        overrideNote("audio_latency_max");
    }
    if (row("Output")) {
        const char* driver = SDL_GetCurrentAudioDriver();
        ImGui::AlignTextToFramePadding();
        ImGui::TextWrapped(tr("SDL audio driver: %s"), driver != NULL ? driver : tr("none"));
        hint("The system's sound driver the game plays through (SDL_AUDIO_DRIVER chooses another).");
    }
    opt::endPage();
}

// ---- Controls ---------------------------------------------------------------------------------

struct Capture {
    bool active;
    bool open;    // the popup is still to be opened (outside the table's ID scope)
    bool gamepad; // a gamepad input, else a key
    int button;
    int slot;
    int profile;
    SDL_JoystickID device;
    uint64_t since;
    char note[96];
};
Capture cap;

const char* keyLabel(SDL_Scancode key) {
    if (key == SDL_SCANCODE_UNKNOWN) {
        return "-";
    }
    const char* name = SDL_GetScancodeName(key);
    return name != NULL && name[0] != '\0' ? name : "?";
}

// A pad input's name in the menu's language (pad.c's English labels are
// "#. runtime" entries of the catalogs: pad.c compares them as they are).
const char* inputLabel(int input) {
    static char label[128];
    if (!skin::native()) return tr(padInputLabel(input));
    snprintf(label,sizeof(label),"%s %s",skin::inputGlyph(input),tr(padInputLabel(input)));
    return label;
}

void startCapture(bool gamepad, int button, int slot) {
    cap.active = true;
    cap.gamepad = gamepad;
    cap.button = button;
    cap.slot = slot;
    cap.profile = padProfilePort();
    cap.device = padProfileDevice(cap.profile);
    cap.since = SDL_GetTicksNS();
    cap.note[0] = '\0';
    cap.open = true;
}

void capturePopup() {
    if (cap.open) {
        ImGui::OpenPopup("Bind");
        cap.open = false;
    }
    if (!beginModal("Bind")) {
        return;
    }
    if (!cap.active) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (cap.gamepad) {
        if (cap.device != 0 && padProfileDevice(cap.profile) != cap.device) {
            cap.active = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::TextWrapped(tr("Press a gamepad button or pull a trigger for %s."), tr(padButtonLabel(cap.button)));
        int count = 0;
        SDL_free(SDL_GetGamepads(&count));
        if (count == 0) {
            warning("No gamepad is connected.");
        }
        hint("Esc cancels (or wait %d s). Guide opens the menu: it cannot be bound.",
             (int)(CAPTURE_TIMEOUT_NS / 1000000000ull));
    } else {
        ImGui::TextWrapped(tr("Press a key for %s (%s)."), tr(padButtonLabel(cap.button)), cap.slot == 0 ? tr("first key") : tr("second key"));
        hint("Esc or any gamepad button cancels (or wait %d s), Delete clears. F1 to F12 are the hotkeys: they "
             "cannot be bound.",
             (int)(CAPTURE_TIMEOUT_NS / 1000000000ull));
    }
    if (SDL_GetTicksNS() - cap.since > CAPTURE_TIMEOUT_NS) {
        cap.active = false;
    }
    if (cap.note[0] != '\0') {
        warning("%s", cap.note);
    }
    if (ImGui::Button(tr("Clear"))) {
        if (cap.gamepad) {
            padSetGamepadInput(cap.button, PAD_INPUT_NONE);
        } else {
            padSetKey(cap.button, cap.slot, SDL_SCANCODE_UNKNOWN);
        }
        cap.active = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel"))) {
        cap.active = false;
    }
    ImGui::EndPopup();
}

// The bindings: a row for each PS1 button, its keys and (or) its gamepad input.
void bindingTable(const char* id, bool keys, bool gamepad) {
    if (ImGui::GetContentRegionAvail().x < 240*scale()) {
        ImGui::PushID(id);
        for (int b = 0; b < PAD_BUTTONS; b++) {
            ImGui::PushID(b);
            markBlock();
            ImGui::TextWrapped("%s %s", skin::psButtonGlyph(padButtonName(b)), tr(padButtonLabel(b)));
            for (int k = 0; keys && k < PAD_KEYS; k++) {
                ImGui::PushID(k);
                if (k) sameLineFor(keyLabel(padKey(b,k)));
                if (ImGui::Button(keyLabel(padKey(b,k)))) startCapture(false,b,k);
                ImGui::PopID();
            }
            if (gamepad && ImGui::Button(inputLabel(padGamepadInput(b)), ImVec2(-1,0))) startCapture(true,b,0);
            ImGui::PopID();
        }
        ImGui::PopID();
        return;
    }
    ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
    int columns = 1 + (keys ? PAD_KEYS : 0) + (gamepad ? 1 : 0);
    if (!ImGui::BeginTable(id, columns, tf)) {
        return;
    }
    ImGui::TableSetupColumn("PS1 button", ImGuiTableColumnFlags_WidthFixed, 86.0f * scale());
    const char* names[4] = {"PS1 button"};
    int n = 1;
    if (keys) {
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Second key", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        names[n++] = N_("Key");
        names[n++] = N_("Second");
    }
    if (gamepad) {
        ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        names[n++] = N_("Gamepad");
    }
    headers(names, n);
    for (int b = 0; b < PAD_BUTTONS; b++) {
        ImGui::PushID(b);
        ImGui::TableNextRow(0, ROW * scale());
        ImGui::TableSetColumnIndex(0);
        markBlock();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s %s", skin::psButtonGlyph(padButtonName(b)), tr(padButtonLabel(b)));
        for (int k = 0; keys && k < PAD_KEYS; k++) {
            ImGui::TableSetColumnIndex(1 + k);
            ImGui::PushID(k);
            if (ImGui::Button(keyLabel(padKey(b, k)), ImVec2(-1, 0))) {
                startCapture(false, b, k);
            }
            ImGui::PopID();
        }
        if (gamepad) {
            ImGui::TableSetColumnIndex(columns - 1);
            if (ImGui::Button(inputLabel(padGamepadInput(b)), ImVec2(-1, 0))) {
                startCapture(true, b, 0);
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void controlsSection() {
    opt::beginPage("controls", CONTROLS, true); // one column: the tables want the width
    opt::group(N_("Buttons"));
    if (row("Button convention", "jp_controls_us us_controls_jp")) {
        bool jp = padRegionalControls(0), us = padRegionalControls(1);
        if (checkbox("Japanese controls in the US version", &jp)) padSetRegionalControls(0, jp);
        hint("Circle confirms, Cross cancels; Triangle and Square follow the Japanese convention too.");
        overrideNote("jp_controls_us");
        if (checkbox("US controls in the Japanese version", &us)) padSetRegionalControls(1, us);
        hint("Cross confirms, Circle cancels. Your keyboard and controller bindings stay independent.");
        overrideNote("us_controls_jp");
        ImGui::Text(tr("%s Choose   %s Back"),skin::actionGlyph(padConfirmButton()),skin::actionGlyph(padCancelButton()));
        hint("Button symbols follow your control convention and the controller you use, including remaps and reconnects.");
    }

    if (row("Controller profile")) {
        int profile = padProfilePort();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##profile", profile < 0 ? tr(padProfileName(profile)) : padProfileName(profile))) {
            if (ImGui::Selectable(tr("Default mapping"), profile < 0)) {
                padSelectProfile(-1);
            }
            for (int p = 0; p < 2; p++) {
                if (padProfileDevice(p) != 0) {
                    ImGui::PushID(p);
                    char label[256];
                    snprintf(label, sizeof(label), tr("Pad %d: %s"), p + 1, padProfileName(p));
                    if (ImGui::Selectable(label, profile == p)) {
                        padSelectProfile(p);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
        }
        hint("Profiles follow your controller. Some identical controllers share a profile. Keyboard settings apply "
             "to all profiles.");
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (count == 0) {
            hint("No gamepad connected. The first one is pad 1 (with the keyboard), the second pad 2.");
        }
        for (int i = 0; i < count; i++) {
            const char* name = SDL_GetGamepadNameForID(ids[i]);
            hint("Pad %d: %s", i + 1, name != NULL ? name : tr("gamepad"));
        }
        SDL_free(ids);
    }

    if (row("Bindings")) {
        ImGui::TextUnformatted(tr("Keyboard"));
        hint("Choose a key to change it, then press the new one. A key drives one button: binding it elsewhere moves it.");
        if (ImGui::GetContentRegionAvail().x >= 340.0f * scale()) {
            bindingTable("keys", true, true);
        } else {
            // A narrow pane (a small window, the game's text size): the keys,
            // then the gamepad, each table with room for its names.
            bindingTable("keys", true, false);
            ImGui::Spacing();
            ImGui::TextUnformatted(tr("Gamepad"));
            bindingTable("pad", false, true);
        }
        markBlock(); // the defaults go with these
        if (ImGui::Button(tr("Default keys"))) {
            padResetKeys();
        }
        const char* resetPad = padProfilePort() < 0 ? tr("Default gamepad") : tr("Reset controller profile");
        sameLineFor(resetPad);
        if (ImGui::Button(resetPad)) {
            padResetGamepad();
        }
        hint("Default keys and Default gamepad (or Reset controller profile) put every binding back.");
    }

    opt::group(N_("Stick and mouse"));
    if (row("Left stick", "pad_stick_dpad pad_stick_deadzone")) {
        bool stick = padStickDpad();
        if (checkbox("Left stick drives the D-pad", &stick)) {
            padSetStickDpad(stick);
        }
        int deadzone = padStickDeadzone();
        ImGui::BeginDisabled(!stick);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##deadzone", &deadzone, 0, 95, tr("Stick dead zone: %d%%"))) {
            padSetStickDeadzone(deadzone);
        }
        ImGui::EndDisabled();
        hint("The stick must move past this threshold to press a direction. Default: 50%%.");
    }

    if (row("Mouse", "mouse_game mouse_point mouse_target mouse_camera")) {
        auto mouseOption = [](const char* key, const char* label, bool value, const char* about) {
            if (strcmp(key, "mouse_game") != 0) {
                markBlock(); // each its own help
            }
            if (checkbox(label, &value)) {
                settingsSetInt(key, value ? 1 : 0);
                settingsSave();
            }
            hint("%s", tr(about));
            overrideNote(key);
        };
        bool mouse = mouseGameEnabled() != 0;
        mouseOption("mouse_game", "Mouse in the game", mouse,
                    "Left click confirms, right click cancels, the wheel moves the cursor, the side buttons are L1 and R1.");
        ImGui::BeginDisabled(!mouse);
        mouseOption("mouse_point", "Click menu entries", mousePointEnabled() != 0,
                    "A click on a main menu command or an item moves the game's cursor there and chooses it (an item: "
                    "click it again to use it).");
        mouseOption("mouse_target", "Click battle targets", mouseTargetEnabled() != 0,
                    "While the game asks for a target, a click on one chooses it. In an attack it aims; a click on "
                    "the aimed target is the confirm button.");
        mouseOption("mouse_camera", "Drag to turn the camera", mouseCameraEnabled() != 0,
                    "Drag sideways with the right button in fields and on the world map, as L1 and R1 turn it.");
        ImGui::EndDisabled();
    }

    opt::group(N_("Camera and vibration"));
    if (row("Free camera", "free_camera", opt::ENHANCED)) {
        bool on = pcFreeCameraEnabled() != 0;
        if (checkbox("Free camera in fields", &on)) pcFreeCameraSet(on);
        hint("%s", tr("Enhanced engine. Turn and tilt the camera around the party with the right stick; hold L2 and move "
                      "the stick up or down to zoom (keyboard: J/L turn, I/K tilt, U/O zoom). R3 or Home returns to the "
                      "game's view. Scenes and rooms with a fixed camera keep the game's."));
        overrideNote("free_camera");
    }

    if (row("Vibration", "vibration vibration_strength")) {
        static const char* const modes[] = {"Off", "The game's", "Enhanced"};
        static const char* const about[] = {
            "No controller vibration.",
            "As on a DualShock. Xenogears itself vibrates only in the gear battling arena, with the arena's "
            "Vibration option on.",
            "The game's own, and the port's: hits taken and dealt, knock-outs, gear impacts, deathblows, and the "
            "camera shakes of battles and fields."};
        int mode = rumbleMode();
        int chosen = choice("vibration", modes, 3, mode);
        if (chosen != mode) {
            rumbleSetMode(chosen);
            settingsSave();
        }
        hint("%s", tr(about[chosen]));
        overrideNote("vibration");
        markBlock();
        SDL_Gamepad* pad = SDL_GetGamepadFromID(padProfileDevice(0));
        bool motors = rumblePadHasMotors(pad) != 0;
        int strength = rumbleStrength();
        ImGui::BeginDisabled(chosen == RUMBLE_OFF);
        float test = ImGui::CalcTextSize(tr("Test")).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth(-(test + ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::SliderInt("##vibration_strength", &strength, 0, 100, tr("Strength: %d%%"))) {
            rumbleSetStrength(strength);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            settingsSave();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!motors);
        if (ImGui::Button(tr("Test"))) {
            rumbleTest(pad);
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (pad == NULL) {
            hint("No controller on pad 1. Vibration needs a controller with motors.");
        } else if (!motors) {
            hint("%s has no vibration motors (or the system does not give access to them).", padProfileName(0));
        } else {
            hint("Both modes. Test plays a short pulse at this strength. Vibration stops while the game is paused.");
        }
        overrideNote("vibration_strength");
    }

    opt::group(N_("Hotkeys"), opt::COLLAPSED);
    if (row("Hotkeys")) {
        const char* choose = skin::native() ? skin::actionGlyph(padConfirmButton()) : padActionLabel(padConfirmButton());
        const char* back = skin::native() ? skin::actionGlyph(padCancelButton()) : padActionLabel(padCancelButton());
        const char* start = skin::native() ? skin::actionGlyph("start") : padActionLabel("start");
        char chooseKeys[96], backKeys[96];
        snprintf(chooseKeys, sizeof(chooseKeys), "%s%sEnter", choose != NULL ? choose : "", choose != NULL ? "/" : "");
        snprintf(backKeys, sizeof(backKeys), "%s%sEsc%s%s", back != NULL ? back : "", back != NULL ? "/" : "",
                 start != NULL ? "/" : "", start != NULL ? start : "");
        const char* const hotkeys[][2] = {
            {"F1", N_("this menu (the game pauses)")},
            {N_("Guide / both stick clicks"), N_("this menu")},
            {backKeys, N_("back; from the list, leave this menu")},
            {chooseKeys, N_("choose")},
            {N_("← at an option's start"), N_("back to the section list")},
            {"L1/R1", N_("the previous and next section")},
            {N_("Square / Delete"), N_("reset the setting the cursor is on (gold: changed)")},
            {"Ctrl+F", N_("search the settings")},
            {"F2", N_("swap discs (when the game asks for one)")},
            {"F3", N_("aspect: 4:3 TV, 4:3 fill, 1:1, 16:9, 16:10, 21:9, 32:9")},
            {"F4", N_("scale: fit, integer, stretch")},
            {"Shift+F4", N_("resolution: exact 1x, or back to the upscaled choice (auto at first)")},
            {"F5", N_("filter: nearest, linear, sharp, bicubic, xbr")},
            {"F6", N_("wide fill: bars, zoom, blur")},
            {"F10", N_("performance overlay (perf_overlay_key)")},
            {"Shift+F10", N_("start or stop the performance log (perf_log_key)")},
            {"F12", N_("replacements A/B: the original art shown in place (replacements_ab_key)")},
            {"F11, Alt+Enter", "fullscreen"},
            {"Start+Select+L1+R1", N_("the game's own soft reset")},
            {"Tab, Select+R2", N_("in a battle: battle speed 1x, 2x, 4x (Gameplay > Battle speed on)")},
        };
        if (ImGui::BeginTable("hotkeys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            for (const auto& h : hotkeys) {
                ImGui::TableNextRow(0, ROW * scale());
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", tr(h[0])); // (wrapped: a narrow pane cuts nothing)
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", tr(h[1]));
            }
            ImGui::EndTable();
        }
        hint("The keys and buttons of this menu and the hotkeys that work while the game runs.");
    }
    opt::endPage();
    if (!opt::indexing()) capturePopup();
}

// ---- Saves: memory cards (the save states: states.cpp) ------------------------------------

const SDL_DialogFileFilter cardFilters[] = {{"Memory card images", "mcd;mcr;mc;srm"}, {"All files", "*"}};
const SDL_DialogFileFilter saveFilters[] = {{"Single saves", "mcs"}, {"All files", "*"}};

int cardSlot;
int cardPick[2] = {-1, -1};
char imagePath[2][1024];
char savePath[1024];
Message cardMsg;
char replaceFrom[1024]; // an import waiting for "replace?"
bool replaceAsk;        // ... its popup is still to be opened (outside the table)
CardBackup backups[CARD_BACKUPS];
int backupCount, backupPick = -1, backupSlot = -1;
char backupImage[1024], restoreName[32], restoreImage[1024];
int restoreSlot;
bool restoreAsk;

void importSave(int slot, const char* path, bool replace) {
    static Card previous;
    char msg[1200];
    if (cardBusy(slot)) {
        cardMsg.set(true, "The game is using this card; try again in a moment.");
        return;
    }
    previous = cards[slot];
    int r = cardImportSave(cards[slot].data, path, replace, msg, sizeof(msg));
    if (r < 0) {
        snprintf(replaceFrom, sizeof(replaceFrom), "%s", path);
        replaceAsk = true;
        cardMsg.text[0] = '\0';
        return;
    }
    if (r > 0) {
        if (!cardChangedByMenu(slot)) {
            cards[slot] = previous;
            cardMsg.set(true, "Cannot write this card or its backup; nothing imported.");
            return;
        }
        backupSlot = -1;
        cardMsg.set(false, "Slot %d: %s", slot + 1, msg);
    } else {
        cardMsg.set(true, "Slot %d: %s", slot + 1, msg);
    }
}

void cardsSection() {
    cardImagesInit(); // read by now unless the game has not reached its card code yet
    static const char* const slots[] = {"Slot 1", "Slot 2"};
    opt::group(N_("Memory cards"));
    if (row("Card in")) {
        int s = choice("slot", slots, 2, cardSlot);
        if (s != cardSlot) {
            cardSlot = s;
            cardMsg.text[0] = '\0';
        }
        hint("The memory card slot the rows below show.");
    }
    Card* c = &cards[cardSlot];
    bool busy = cardBusy(cardSlot);
    char msg[1200];

    if (row("Image", cardSlot == 0 ? "card1" : "card2")) {
    pathText(c->present ? c->path : NULL, "No card in this slot");
    if (imagePath[cardSlot][0] == '\0' && c->present) {
        snprintf(imagePath[cardSlot], sizeof(imagePath[cardSlot]), "%s", c->path);
    }
    ImGui::BeginDisabled(busy);
    if (pathField("image", imagePath[cardSlot], sizeof(imagePath[cardSlot]), 10 + cardSlot, false, cardFilters, 2,
                  "Insert")) {
        if (cardSetSlot(cardSlot, imagePath[cardSlot], msg, sizeof(msg))) {
            cardMsg.set(false, "Inserted: %s", msg);
            cardPick[cardSlot] = -1;
        } else {
            cardMsg.set(true, "%s", msg);
        }
    }
    if (ImGui::Button(tr("Default image"))) {
        if (cardSetSlot(cardSlot, "", msg, sizeof(msg))) {
            cardMsg.set(false, "Inserted: %s", msg);
            snprintf(imagePath[cardSlot], sizeof(imagePath[cardSlot]), "%s", cards[cardSlot].path);
        } else {
            cardMsg.set(true, "%s", msg);
        }
    }
    sameLineFor("No card");
    if (ImGui::Button(tr("No card"))) {
        if (cardSetSlot(cardSlot, "none", msg, sizeof(msg))) {
            cardMsg.set(false, "Slot %d is empty", cardSlot + 1);
        } else {
            cardMsg.set(true, "%s", msg);
        }
    }
    ImGui::EndDisabled();
    hint("A missing file becomes a new formatted card, written when something is saved on it. The game sees "
         "the change as a card taken out and put in.");
    overrideNote(cardSlot == 0 ? "card1" : "card2");
    } // Image

    if (row("Backups")) {
    if (backupSlot != cardSlot || strcmp(backupImage, c->path) != 0) {
        backupSlot = cardSlot;
        snprintf(backupImage, sizeof(backupImage), "%s", c->path);
        backupCount = cardListBackups(cardSlot, backups, CARD_BACKUPS);
        backupPick = -1;
    }
    if (backupCount < 0) {
        warning("Cannot read this card's backup history.");
    } else if (backupCount == 0) {
        ImGui::TextDisabled(tr("No previous versions yet"));
    } else if (ImGui::BeginTable("backups", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 76 * scale());
        ImGui::TableSetupColumn("Saved");
        for (int i = 0; i < backupCount; i++) {
            ImGui::TableNextRow(0, ROW * scale());
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            char label[32];
            snprintf(label, sizeof(label), i == 0 ? tr("Latest") : tr("%d earlier"), i);
            if (skin::selectable(label, backupPick == i, ImGuiSelectableFlags_SpanAllColumns)) {
                backupPick = i;
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            SDL_DateTime date;
            if (SDL_TimeToDateTime(backups[i].modified, &date, true)) {
                ImGui::Text("%04d-%02d-%02d %02d:%02d:%02d", date.year, date.month, date.day,
                            date.hour, date.minute, date.second);
            } else {
                ImGui::TextUnformatted(backups[i].name);
            }
        }
        ImGui::EndTable();
    }
    ImGui::BeginDisabled(busy || !c->present || backupPick < 0);
    if (ImGui::Button(tr("Restore selected backup"))) {
        restoreSlot = cardSlot;
        snprintf(restoreName, sizeof(restoreName), "%s", backups[backupPick].name);
        snprintf(restoreImage, sizeof(restoreImage), "%s", c->path);
        restoreAsk = true;
    }
    ImGui::EndDisabled();
    sameLineFor("Refresh backups");
    if (ImGui::Button(tr("Refresh backups"))) {
        backupSlot = -1;
    }
    hint("Keeps the last ten previous card versions beside its image. Restore replaces the whole card and keeps "
         "the current contents as another backup.");
    } // Backups

    CardSave saves[CARD_BLOCKS];
    int n = c->present ? cardListSaves(c->data, saves, CARD_BLOCKS) : -2;
    if (cardPick[cardSlot] >= n) {
        cardPick[cardSlot] = -1;
    }
    if (row("Saves")) {
    if (n == -2) {
        ImGui::TextDisabled("-");
    } else if (n < 0) {
        ImGui::TextDisabled(tr("Not formatted (the game formats it when it saves)"));
    } else {
        ImGui::Text(tr("%d save%s, %d free block%s"), n, n == 1 ? "" : "s", cardFreeBlocks(c->data),
                    cardFreeBlocks(c->data) == 1 ? "" : "s");
        if (n > 0 && ImGui::BeginTable("saves", 3,
                                       ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                           ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("File");
            ImGui::TableSetupColumn("Blocks", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::CalcTextSize(tr("Blocks")).x + ImGui::GetStyle().CellPadding.x * 2);
            ImGui::TableSetupColumn("Title");
            static const char* const names[] = {"File", "Blocks", "Title"};
            headers(names, 3);
            for (int i = 0; i < n; i++) {
                ImGui::TableNextRow(0, ROW * scale());
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i);
                if (skin::selectable(saves[i].name, cardPick[cardSlot] == i, ImGuiSelectableFlags_SpanAllColumns)) {
                    cardPick[cardSlot] = i;
                    char* pref = SDL_GetPrefPath(NULL, "xenogears");
                    snprintf(savePath, sizeof(savePath), "%s%s.mcs", pref != NULL ? pref : "", saves[i].name);
                    SDL_free(pref);
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", saves[i].blocks);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(saves[i].title);
            }
            ImGui::EndTable();
        }
    }
    hint("The saves on this card: choose one to export it below.");
    } // Saves

    bool pick = n > 0 && cardPick[cardSlot] >= 0;
    if (row("Single save")) {
    ImGui::BeginDisabled(busy || !c->present || n < 0);
    // The field's own button imports from it; Export writes to it (or asks for a file).
    if (pathField("mcs", savePath, sizeof(savePath), 20, false, saveFilters, 1, "Import", false)) {
        importSave(cardSlot, savePath, false);
    }
    ImGui::BeginDisabled(!pick);
    if (ImGui::Button(tr("Export to this file")) && pick) {
        if (cardExportSave(c->data, saves[cardPick[cardSlot]].name, savePath, msg, sizeof(msg))) {
            cardMsg.set(false, "%s", msg);
        } else {
            cardMsg.set(true, "%s", msg);
        }
    }
    sameLineFor("Export to...");
    if (ImGui::Button(tr("Export to...")) && pick) {
        browse(21, true, saveFilters, 1, savePath[0] != '\0' ? savePath : NULL);
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    hint("Choose a save above to export it as an .mcs file; Import puts an .mcs save from the file on this card.");
    if (busy) {
        warning("The game is using this card right now: go back to the game and try again in a moment.");
    }
    cardMsg.draw();
    } // Single save
    // An export chosen in the desktop's dialog (its answer may come later).
    if (opt::indexing()) return;
    if (browsed(21, savePath, sizeof(savePath)) && pick) {
        if (cardExportSave(c->data, saves[cardPick[cardSlot]].name, savePath, msg, sizeof(msg))) {
            cardMsg.set(false, "%s", msg);
        } else {
            cardMsg.set(true, "%s", msg);
        }
    }

    opt::plain(); // (the table closed: the popups outside it)
    if (restoreAsk) {
        ImGui::OpenPopup("Restore card");
        restoreAsk = false;
    }
    if (beginModal("Restore card")) {
        ImGui::TextWrapped(tr("Replace all saves in slot %d with this backup? The current card will be kept in its "
                           "backup history."), restoreSlot + 1);
        bool changed = strcmp(cards[restoreSlot].path, restoreImage) != 0;
        bool unavailable = changed || cardBusy(restoreSlot) || !cards[restoreSlot].present;
        if (unavailable) {
            warning("The card changed or is in use. Cancel and select its backup again.");
        }
        ImGui::BeginDisabled(unavailable);
        if (ImGui::Button(tr("Restore whole card"))) {
            int ok = cardRestoreBackup(restoreSlot, restoreName, msg, sizeof(msg));
            cardMsg.set(!ok, "%s", msg);
            if (ok) {
                cardPick[restoreSlot] = -1;
                backupSlot = -1;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        sameLineFor("Cancel");
        if (ImGui::Button(tr("Cancel"))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (replaceAsk) {
        ImGui::OpenPopup("Replace");
        replaceAsk = false;
    }
    if (beginModal("Replace")) {
        ImGui::TextWrapped(tr("A save of that name is on the card in slot %d already. Replace it with the one in the file?"),
                           cardSlot + 1);
        if (ImGui::Button(tr("Replace"))) {
            importSave(cardSlot, replaceFrom, true);
            ImGui::CloseCurrentPopup();
        }
        sameLineFor("Keep the one on the card");
        if (ImGui::Button(tr("Keep the one on the card"))) {
            cardMsg.set(false, "Nothing imported");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void savesSection() {
    opt::beginPage("saves", SAVES);
    cardsSection();
    statesSection(); // states.cpp: the Save states group and its Advanced group
    opt::endPage();
}

// ---- Gameplay -----------------------------------------------------------------------------------

// A 60 Hz mode's row: the Enhanced engine's switch, or why it is not here.
void hz60Row(const char* label, const char* key, int (*get)(void), void (*set)(int), const char* about,
             const char* original) {
    if (!row(label, key, opt::ENHANCED)) return;
    if (engineThis() == ENGINE_ENHANCED) {
        bool on = get() != 0;
        char id[40];
        snprintf(id, sizeof id, "##%s", key);
        if (offOnList(id, &on, N_("Off: 30 frames per second"), N_("On: 60 frames per second"))) set(on);
        hint("%s", tr(about));
        overrideNote(key);
    } else {
        ImGui::TextDisabled("%s", tr("Enhanced engine only (System > Engine)."));
        hint("%s", tr(original));
    }
}

void gameplaySection() {
    opt::beginPage("gameplay", GAMEPLAY);
    opt::group(N_("General"));
    bool speedup = speedupEnabled() != 0;
    if (row("Speedup", "speedup")) {
        if (offOnList("##speedup", &speedup)) speedupSetEnabled(speedup);
        hint("Reduces card/loading waits and field/battle stalls, and enables smooth field motion at 60 Hz by default. Gameplay, animation events, movies and audio stay at normal speed. Off keeps original timing and frame cadence.");
        overrideNote("speedup");
    }
    if (row("Battle speed", "battle_speed battle_speed_key")) {
        static const char* const battleSpeedNames[] = {N_("Off"), "1x", "2x", "4x"};
        static const int battleSpeeds[] = {0, 1, 2, 4};
        int chosen = battleSpeedChosen(), index = 0;
        for (int i = 1; i < 4; i++) if (chosen == battleSpeeds[i]) index = i;
        int picked = listChoice("##battle_speed", battleSpeedNames, 4, index);
        if (picked != index && picked >= 0) battleSpeedSetChosen(battleSpeeds[picked]);
        hint("Character and Gear battles at 2x or 4x: the game runs that many of its own frames per picture, so "
             "turns, damage and every random roll come as at 1x, only sooner. Music keeps its tempo; sound effects "
             "keep their pitch. Menus, target selection, dialogue and the first moments of each attack name run at "
             "1x. In a battle, Tab (or Select + R2 together on a gamepad) steps 1x, 2x, 4x. Off: the original "
             "speed, and the control does nothing.");
        overrideNote("battle_speed");
    }
    if (row("Original-game bug fixes", "original_bug_fixes")) {
        bool originalFixes = pcGameplayOriginalBugFixes() != 0;
        if (offOnList("##original_bug_fixes", &originalFixes)) pcGameplaySetOriginalBugFixes(originalFixes);
        hint("Fixes Gear-shop Ether amp, Billy's ammo elements on equipment/status refresh, the first Deus camera freeze, "
             "missing/stray portrait pixels on the next portrait load, Def UP, a name's final 's' (US), the shop's "
             "Buy/Sell/Exit arrow (US), MP for EP in the party menu, and field sound loops a menu cut off.");
        overrideNote("original_bug_fixes");
    }

    // ---- every 60 Hz mode together: the Enhanced engine's per-mode switches,
    // and the Original engine's motion smoothing
    opt::group(N_("60 Hz"));
    hz60Row("60 Hz fields", "field60", pcGameplayField60, pcGameplaySetField60,
            N_("Fields run their logic every vblank: input every vblank, the player, sprites and camera moving every "
               "frame at their authored speed, scripts, dialogue, random encounters and effects at their authored "
               "cadence. Fields authored at 20 or 15 frames per second keep that. Takes effect at once."),
            N_("The Original engine runs fields as the PlayStation did, at 30 frames per second."));
    if (row("20 and 15 fps rooms", "field60slow", opt::ENHANCED | opt::RESTART)) {
        bool slow = settingsBool("field60slow", 1) != 0;
        ImGui::BeginDisabled(engineThis() == ENGINE_ENHANCED && !pcGameplayField60());
        if (offOnList("##field60slow", &slow, N_("At their authored rate"), N_("At 60 Hz with the field"))) {
            settingsDescribe("field60slow", "field60slow: 1 (default) runs the fields authored at 20 or 15 frames per "
                                            "second at 60 Hz with 60 Hz fields, 0 keeps their own cadence");
            settingsSetInt("field60slow", slow ? 1 : 0);
            settingsSave();
        }
        ImGui::EndDisabled();
        hint("The few rooms the game draws at 20 or 15 frames per second (with 60 Hz fields): at 60 Hz too, exact "
             "against the Original at every native frame (the default), or at their own rate, for a comparison.");
        overrideNote("field60slow");
    }
    hz60Row("60 Hz world map", "worldmap60", pcGameplayWorldmap60, pcGameplaySetWorldmap60,
            N_("The world map draws every vblank: the party, vehicles, camera, clouds and dust move every frame, "
               "and the game itself (input, random encounters, the rest) runs at its authored cadence, exactly as "
               "on the PlayStation. Story scenes on the world map keep 30 frames per second. Takes effect at once."),
            N_("The Original engine runs the world map as the PlayStation did, at 30 frames per second."));
    hz60Row("60 Hz battles", "battle60", pcGameplayBattle60, pcGameplaySetBattle60,
            N_("Battles move every vblank: the Gears' animation, which the game steps every other frame, and what "
               "follows it, while the battle itself (turns, damage, random rolls) runs exactly as on the PlayStation. "
               "Takes effect at once."),
            N_("The Original engine runs battles as the PlayStation did."));
    bool smoothing = interpolationEnabled();
    if (row("Motion smoothing", "interpolation")) {
        ImGui::BeginDisabled(!speedupEnabled());
        if (checkbox("Smooth motion", &smoothing)) interpolationSetEnabled(smoothing);
        ImGui::EndDisabled();
        hint("Smooths fields and the world map: models, Gears, terrain, character placement, camera motion, particles, clouds and screen effects. Gameplay, animation events and audio keep their original timing.");
        hint("Adds one game frame of display delay (usually 33 ms; up to 67 ms in slower scenes). Animation pictures, text and lighting retain their original timing.");
        hint("Available with Speedup. Supports wide/expanded framing, texture filters, packs and enhanced text. Scene cuts and unsafe state changes use the original picture.");
        overrideNote("interpolation");
    }
    if (row("Motion display rate", "interpolation_hz")) {
        static const char* const motionRates[] = {"Native (59.8 Hz)", "60 Hz", "120 Hz", "144 Hz", "240 Hz"};
        static const int motionHz[] = {0, 60, 120, 144, 240};
        int rate = presentationRate(), rateIndex = 0;
        for (int i = 1; i < 5; i++) if (rate == motionHz[i]) rateIndex = i;
        ImGui::BeginDisabled(!smoothing);
        int rateChoice = choice("interpolation_hz", motionRates, 5, rateIndex);
        if (rateChoice != rateIndex) presentationSetRate(motionHz[rateChoice]);
        ImGui::EndDisabled();
        hint("Speedup defaults to 60 Hz in supported fields. Higher rates add display samples. Shader presets and unsupported scenes use native presentation. Speedup off keeps authentic frame cadence.");
        hint("Rates above your monitor's refresh can be discarded. VRR availability depends on your display and desktop; submission rate does not measure visible refreshes.");
        char rateStatus[384]; presentationRateDescribe(rateStatus, sizeof(rateStatus)); hint("%s", rateStatus);
        overrideNote("interpolation_hz");
    }

    opt::group(N_("Text"));
    if (row("Text speed", "text_speed")) {
        static const char* const speedNames[] = {"Original", "2x", "3x", "4x", "8x", "Instant"};
        static const int speeds[] = {1, 2, 3, 4, 8, 0};
        int textSpeed = pcGameplayTextSpeed();
        int speedChoice = -1;
        for (int i = 0; i < 6; i++) if (textSpeed == speeds[i]) speedChoice = i;
        int speedSelected = listChoice("##text_speed", speedNames, 6, speedChoice);
        if (speedSelected != speedChoice && speedSelected >= 0) pcGameplaySetTextSpeed(speeds[speedSelected]);
        hint("How fast dialogue appears. Pauses stay. Timed and slow text, such as lines that close by themselves "
             "or are cut off, keeps its original pace.");
        overrideNote("text_speed");
    }
    if (row("Hold to show text", "text_hold_fast")) {
        bool holdText = pcGameplayTextHold() != 0;
        if (offOnList("##text_hold_fast", &holdText)) pcGameplaySetTextHold(holdText);
        hint("Holding the confirm button shows the rest of a page at once. Pages still wait for a fresh press.");
        overrideNote("text_hold_fast");
    }
    if (row("Auto-advance text", "text_auto_advance")) {
        static const char* const autoNames[] = {"Off", "1 s", "2 s", "3 s", "5 s", "8 s"};
        static const int autoSeconds[] = {0, 1, 2, 3, 5, 8};
        int autoNow = pcGameplayTextAutoAdvance();
        int autoChoice = -1;
        for (int i = 0; i < 6; i++) if (autoNow == autoSeconds[i]) autoChoice = i;
        int autoSelected = listChoice("##text_auto_advance", autoNames, 6, autoChoice);
        if (autoSelected != autoChoice && autoSelected >= 0) pcGameplaySetTextAutoAdvance(autoSeconds[autoSelected]);
        hint("A finished page continues by itself after this long, in fields and battle. Choices always wait "
             "for you.");
        overrideNote("text_auto_advance");
    }

    opt::group(N_("Battles"));
    if (row("Difficulty", "difficulty")) {
        static const char* const modes[] = {"Normal", "Hard"};
        int hard = pcGameplayHardMode();
        int mode = choice("##difficulty", modes, 2, hard);
        if (mode != hard) pcGameplaySetHardMode(mode);
        hint("Hard usually uses 2x enemy HP and 1.5x attack and ether, with boss, story and support exceptions. Character starts and Gear equipment stay original. "
             "Battle changes apply when the next fight loads. Advanced below has a multiplier for each enemy stat.");
        overrideNote("difficulty");
    }
    if (row("Retry lost battles", "battle_retry")) {
        bool battleRetry = pcGameplayBattleRetry() != 0;
        if (offOnList("##battle_retry", &battleRetry)) pcGameplaySetBattleRetry(battleRetry);
        hint("When the party falls in a battle that would end the game, choose Retry to fight it again from its "
             "start, with the items, HP and Gears you had. Story battles you are meant to lose stay as they are. "
             "Difficulty changed here before you choose applies to the retry.");
        overrideNote("battle_retry");
    }
    if (row("Enemy info", "enemy_info")) {
        static const char* const enemyInfoNames[] = {"Off", "Always", "HP once beaten"};
        int enemyInfoChoice = pcGameplayEnemyInfoMode();
        int enemyInfoSelected = listChoice("##enemy_info", enemyInfoNames, 3, enemyInfoChoice);
        if (enemyInfoSelected != enemyInfoChoice && enemyInfoSelected >= 0) pcGameplaySetEnemyInfoMode(enemyInfoSelected);
        hint("Select, pressed and released on its own, opens and closes a window in the lower right of the battle "
             "with each enemy's name, HP and active statuses, named as the game names them; while you choose a "
             "target, only the enemy under the cursor. Hidden enemies are not listed; weaknesses stay hidden. HP "
             "once beaten: an enemy's HP appears only once you have defeated that kind of enemy before, as the "
             "extras' bestiary records it (kept with your settings, across games: a boss beaten in one game shows "
             "its HP in the next). Select with L1, R1, L2 or R2 is another control (Select + R2: battle speed).");
        overrideNote("enemy_info");
    }
    if (row("Random encounters", "encounter_rate")) {
        int rate = pcGameplayEncounterRate();
        static const char* const names[] = {"Off", "Reduced", "Normal", "Increased"};
        static const int rates[] = {0, 50, 100, 200};
        int preset = 4;
        for (int i = 0; i < 4; i++) if (rate == rates[i]) preset = i;
        int selected = listChoice("##encounter_preset", names, 4, preset);
        if (selected != preset && selected < 4) {
            rate = rates[selected];
            pcGameplaySetEncounterRate(rate);
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##encounter_rate", &rate, 0, 400, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
            pcGameplaySetEncounterRate(rate);
        }
        hint("Applies while walking in fields and on the world map. 0%% turns random encounters off; 100%% is the "
             "original rate. Story battles still occur.");
        overrideNote("encounter_rate");
    }
    if (row("Deathblow learning", "deathblow_learning")) {
        int practice = pcGameplayDeathblowLearning();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##deathblow_learning", &practice, 0, 1000, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
            pcGameplaySetDeathblowLearning(practice);
        }
        hint("Practice each normal attack gives toward new deathblows. 100%% is original; 0%% adds none. "
             "Deathblows are still learned after a battle, at their original levels and in their original order.");
        overrideNote("deathblow_learning");
    }

    // ---- the multipliers behind Difficulty and the rewards
    opt::group(N_("Advanced"), opt::COLLAPSED);
    static const char* const statNames[] = {
        "Enemy HP", "Enemy attack", "Enemy ether", "Enemy defence", "Enemy ether defence", "Enemy agility"
    };
    for (int stat = 0; stat < GAMEPLAY_ENEMY_STAT_COUNT; stat++) {
        if (!row(statNames[stat], pcGameplayEnemyKey(stat))) continue;
        ImGui::PushID(stat);
        int percent = pcGameplayEnemyMultiplier(stat);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##enemy_stat", &percent, 25, 400, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
            pcGameplaySetEnemyMultiplier(stat, percent);
        }
        overrideNote(pcGameplayEnemyKey(stat));
        ImGui::PopID();
        hint("Multipliers apply on top of Normal or Hard, in character and Gear battles. Stats stop at their native "
             "limits; agility stops at 16. Normal with every multiplier at 100%% keeps the original balance.");
    }
    if (row("Reset difficulty")) {
        if (ImGui::Button(tr("Reset difficulty"))) {
            pcGameplaySetHardMode(0);
            for (int stat = 0; stat < GAMEPLAY_ENEMY_STAT_COUNT; stat++) pcGameplaySetEnemyMultiplier(stat, 100);
        }
        hint("Normal, and every enemy multiplier at 100%%.");
    }

    static const char* const rewardNames[] = {"Random EXP", "Random gold", "Scripted EXP", "Scripted gold"};
    for (int reward = 0; reward < GAMEPLAY_REWARD_COUNT; reward++) {
        if (!row(rewardNames[reward], pcGameplayRewardKey(reward))) continue;
        ImGui::PushID(100 + reward);
        int percent = pcGameplayRewardMultiplier(reward);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##reward", &percent, 0, 1000, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
            pcGameplaySetRewardMultiplier(reward, percent);
        }
        overrideNote(pcGameplayRewardKey(reward));
        ImGui::PopID();
        hint("Random battles are the ones the encounter rate controls, while walking in fields and on the world map. "
             "Scripted battles are every other fight: bosses, story battles and enemies you walk into. 100%% is "
             "original, 0%% gives none. EXP includes ether EXP and reserve characters. To make up for fewer "
             "encounters, raise the random-battle rewards: try 200%% with 50%% encounters, or 400%% with 25%%.");
    }
    if (row("Reset rewards")) {
        if (ImGui::Button(tr("Reset rewards"))) {
            for (int reward = 0; reward < GAMEPLAY_REWARD_COUNT; reward++) pcGameplaySetRewardMultiplier(reward, 100);
        }
        hint("Every reward multiplier at 100%%.");
    }

    opt::group(""); // the Gauntlet has its own heading
    if (opt::plain()) gauntletSection(); // gauntlet.cpp
    opt::endPage();
}

// ---- System -------------------------------------------------------------------------------------

// The engine choice (system/engine.h): Original, the game as the PS1 ran it,
// or Enhanced, the port's own engine in xenogears-enhanced. A choice other
// than the running executable takes effect at the next launch, or now from
// the settings menu (the game ends and the other executable starts).
static const char* const engineNames[] = {N_("Original"), N_("Enhanced")};
void engineRow(int* engine, bool live) {
    if (!row("Engine", live ? "engine" : nullptr, live ? opt::RESTART : 0)) return;
    *engine = choice("engine", engineNames, 2, *engine);
    hint("Original: the game exactly as the PlayStation ran it, validated frame for frame. Enhanced: the port's "
         "own engine, which may change timing, pictures and loading on purpose. Saves on the memory cards are "
         "shared; save states and rewinds belong to the engine that made them.");
    if (!live) return;
    ImGui::Text(tr("Running: the %s engine"), tr(engineTitle(engineThis())));
    if (*engine != engineThis()) {
        const char* exe = engineExecutable(*engine);
        if (exe == NULL) {
            warning("The %s engine's executable was not found beside this one (%s).", tr(engineTitle(*engine)),
                    *engine == ENGINE_ENHANCED ? "xenogears-enhanced" : "xenogears");
        } else {
            warning("Saved for the next launch.");
            char label[160];
            snprintf(label, sizeof label, tr("Restart into the %s engine now"), tr(engineTitle(*engine)));
            if (ImGui::Button(label)) {
                engineRestart(*engine);
            }
            hint("Ends this game now: anything not saved to a memory card is lost. Then %s starts with the same "
                 "arguments.", exe);
        }
    }
    overrideNote("engine");
}

const SDL_DialogFileFilter biosFilters[] = {{"BIOS images", "bin;rom"}, {"All files", "*"}};
const SDL_DialogFileFilter discFilters[] = {{"Disc images", "cue;bin"}, {"All files", "*"}};

char biosPath[1024];
char disc2Path[1024];
Message biosMsg, discMsg;

// The setup screen (ui.h uiDiscSetup): its two pages and what they hold.
struct DiscSetup {
    char first[4096], other[4096], region[3];
    char imageDir[4096], biosDir[4096], bios[4096];
    CdImageInfo info[2];
    CdSetupImage found[16];
    int foundCount;
    bool searched, valid;
    Message message, biosMessage;
    bool accepted, noDisc, withoutBios, biosOk, askBios;
    char chosen[4096];
    int engine; // system/engine.h: the engine to start with (Start applies it)
} setup;

// A path into one of the setup's 4096-byte fields (from another of them).
void copyPath(char* dst, const char* src) {
    size_t n = strlen(src);
    n = n < 4095 ? n : 4095;
    memmove(dst, src, n);
    dst[n] = '\0';
}

bool verifySetup() {
    char why[1024];
    char first[4096], other[4096];
    if (!cdSetupPath(setup.first, first, sizeof first) || !cdSetupPath(setup.other, other, sizeof other)) {
        setup.message.set(true, "Cannot resolve the image path. Use a complete path within 4095 bytes.");
        return setup.valid = false;
    }
    snprintf(setup.first, sizeof setup.first, "%s", first);
    snprintf(setup.other, sizeof setup.other, "%s", other);
    bool ok = cdVerifyPair(setup.first, setup.other, setup.region[0] ? setup.region : NULL,
                           setup.info, why, sizeof why) != 0;
    if (ok) setup.message.set(false, "Image identity and file-table checks passed%s", setup.other[0] ? tr(" for both discs") : "");
    else setup.message.set(true, "%s", why);
    return setup.valid = ok;
}

// The image folder searched: the release's starting disc and its other disc.
void searchImages() {
    char dir[4096];
    if (!cdSetupPath(setup.imageDir, dir, sizeof dir)) dir[0] = '\0';
    snprintf(setup.imageDir, sizeof setup.imageDir, "%s", dir);
    setup.foundCount = cdSetupFindImages(dir, setup.found, (int)IM_COUNTOF(setup.found));
    setup.searched = true;
    int first, other;
    if (cdSetupPick(setup.found, setup.foundCount, setup.region[0] ? setup.region : NULL, &first, &other)) {
        copyPath(setup.first, setup.found[first].path);
        copyPath(setup.other, other >= 0 ? setup.found[other].path : "");
        verifySetup();
    } else {
        setup.message.set(true, setup.foundCount > 0 ? "No %s Xenogears image in %s or the folders in it (only "
                                                       "another release's)."
                                                     : "No%s Xenogears image in %s or the folders in it.",
                          setup.region[0] && setup.foundCount > 0 ? setup.region : "", dir[0] ? dir : tr("that folder"));
    }
}

void checkBios() {
    setup.biosOk = cdSetupIsBios(setup.bios);
}

// The BIOS folder searched (or the path given, a BIOS image itself).
void searchBios() {
    char dir[4096], found[4096];
    if (!cdSetupPath(setup.biosDir, dir, sizeof dir)) dir[0] = '\0';
    snprintf(setup.biosDir, sizeof setup.biosDir, "%s", dir);
    if (cdSetupFindBios(dir, found, sizeof found)) {
        snprintf(setup.bios, sizeof setup.bios, "%s", found);
        setup.withoutBios = false;
        setup.biosMessage.set(false, "BIOS image: %s", found);
    } else {
        setup.biosMessage.set(true, "No PS1 BIOS image (scph5501.bin, scph1001.bin... 512 KB) in %s.",
                              dir[0] ? dir : tr("that folder"));
    }
    checkBios();
}

bool acceptSetup() {
    if (!verifySetup()) return false;
    checkBios();
    if (!setup.biosOk && !setup.withoutBios) {
        setup.askBios = true; // the explicit choice to go on without one (drawSetupSection's modal)
        return false;
    }
    if (setup.noDisc) {
        // main.c opens it (nothing is in the drive yet).
        snprintf(setup.chosen, sizeof setup.chosen, "%s", setup.first);
        settingsSet("disc2", setup.other);
    } else {
        if (!cdInsertDisc(setup.first)) {
            setup.message.set(true, "Cannot reopen the starting image. Check its files and try again.");
            return false;
        }
        cdSwapInit(setup.first);
        if (!cdSwapSetOther(setup.other)) {
            setup.message.set(true, "Cannot use the other image. Check its files and try again.");
            return false;
        }
    }
    settingsDescribe("disc", "disc: the starting disc image (XG_DISC overrides for one run)");
    settingsSet("disc", setup.first);
    if (setup.engine != engineThis() || engineSetting() >= 0) engineSetSetting(setup.engine);
    if (!(setup.biosOk ? kromSetBios(setup.bios) : kromSetBios("none"))) {
        setup.biosMessage.set(true, "%s is not a 512 KB PS1 BIOS image.", setup.bios);
        return false;
    }
    if (!settingsSave()) setup.message.set(true, "Starting with these images; settings could not be saved.");
    setup.accepted = true;
    return true;
}

// The menu's and the extras' language (l10n.h), each name in its own
// language (never translated).
void languageRow() {
    if (!row("Language", "ui_language")) return;
    int count = l10nLanguageCount(), now = l10nLanguage();
    ImGui::PushID("ui_language");
    for (int i = 0; i < count; i++) {
        const char* name = l10nLanguageName(i);
        if (i > 0) {
            ImGui::SameLine(0, 14 * scale());
            if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(name).x) ImGui::NewLine();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::PushID(i);
        if (skin::selectable(name, now == i, 0, ImGui::CalcTextSize(name)) && i != now) l10nSetLanguage(i);
        ImGui::PopID();
    }
    ImGui::PopID();
    hint("This menu and the extras. The game's own text stays as the disc has it. Text not yet translated shows in "
         "English.");
    if (now > 0) hint("%s: %d texts translated.", l10nLanguageName(now), l10nEntries(now));
    if (ImGui::Button(tr("Reload translations"))) l10nReload();
    hint("Your own translations: a gettext .po file in the save folder's lang folder (tools/l10n.py makes one). "
         "Reload reads them again after an edit.");
    overrideNote("ui_language");
}

// This menu's own look (skin.h; they were Display's).
void menuLookRows() {
    if (row("Menu text", "menu_text")) {
        static const char* const sizes[] = {"Game size", "Compact"};
        static bool resized; // the menu changed size last frame: this row back in view
        int size = skin::textSize() == skin::TEXT_GAME ? 0 : 1;
        int v = choice("menutext", sizes, 2, size);
        if (resized) {
            ImGui::SetScrollHereY(0.5f);
            resized = false;
        }
        if (v != size) {
            skin::setTextSize(v == 0 ? skin::TEXT_GAME : skin::TEXT_COMPACT);
            resized = true;
        }
        hint(v == 0 ? "This menu's letters as large as the game's own menus: less at once, more to scroll."
                    : "This menu's letters smaller than the game's menus, to show more at once.");
        hint("In whole pixels from the window's size, so they stay sharp (Menu size can make it larger or smaller).");
        overrideNote("menu_text");
    }

    if (row("Menu size", "menu_scale")) {
        // In steps of 10 (a value set in settings.ini shows as it is), and
        // Reset: 100%. A choice made from a list, so the menu does not change
        // size under the mouse while it drags.
        static bool rescaled; // the menu changed size last frame: this row back in view
        int percent = skin::scalePercent();
        char now[16];
        snprintf(now, sizeof(now), "%d%%", percent);
        float reset = ImGui::CalcTextSize(tr("Reset")).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - reset - ImGui::GetStyle().ItemSpacing.x);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * scale(), 6 * scale()));
        bool open = ImGui::BeginCombo("##menuscale", now);
        ImGui::PopStyleVar();
        int chosen = percent;
        if (open) {
            skin::frameWindow();
            for (int p = skin::SCALE_MIN; p <= skin::SCALE_MAX; p += 10) {
                char name[16];
                snprintf(name, sizeof(name), "%d%%", p);
                if (ImGui::Selectable(name, p == percent)) chosen = p;
                if (p == percent) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(percent == skin::SCALE_DEFAULT);
        if (ImGui::Button(tr("Reset"))) chosen = skin::SCALE_DEFAULT;
        ImGui::EndDisabled();
        if (rescaled) {
            ImGui::SetScrollHereY(0.5f);
            rescaled = false;
        }
        if (chosen != percent) {
            skin::setScalePercent(chosen);
            rescaled = true;
        }
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(window(), &pw, &ph);
        float base = (float)skin::unitFor(pw, ph), unit = skin::menuUnitFor(pw, ph);
        hint("The whole menu, its letters, windows and gaps, at %d%% of the size its text size gives "
             "(100%%: whole pixels, the sharpest). Reset goes back to 100%%.", percent);
        if (fabsf(unit - base * percent / 100.0f) > 0.01f) {
            if (unit > base * percent / 100.0f) {
                hint("In this window it goes no smaller than %d%% (a window pixel for each of the game's).",
                     (int)lroundf(unit * 100.0f / base));
            } else {
                hint("In this window it goes no larger than %d%% (the game's 320 x 216 screen filling it).",
                     (int)lroundf(unit * 100.0f / base));
            }
        }
        overrideNote("menu_scale");
    }

    if (row("Dark mode", "menu_dark")) {
        static const char* const offOnDark[] = {"Off", "On"};
        int dark = skin::darkWindows() ? 1 : 0;
        int v = choice("menudark", offOnDark, 2, dark);
        if (v != dark) {
            skin::setDarkWindows(v != 0);
        }
        hint(v ? "This menu's windows darker than the game's grey." : "This menu's windows in the game's own half-clear grey.");
        overrideNote("menu_dark");
    }
}

void aboutRows(); // System's About group (below)

void systemSection() {
    opt::beginPage("system", SYSTEM);
    // ---- one choice for many settings
    opt::group(N_("Presets"));
    opt::presetRows(); // options.cpp

    opt::group(N_("Engine"));
    int engine = engineSetting() >= 0 ? engineSetting() : engineThis();
    int chosenEngine = engine;
    engineRow(&chosenEngine, true);
    if (chosenEngine != engine) engineSetSetting(chosenEngine);

    opt::group(N_("Discs and BIOS"));
    if (row("BIOS (kanji)", "bios")) {
        pathText(kromPath(), "None read: the save titles on the memory card screens and the text that scrolls over some "
                             "scenes are blank");
        if (pathField("bios", biosPath, sizeof(biosPath), 30, false, biosFilters, 2, "Use")) {
            if (kromSetBios(biosPath)) {
                biosMsg.set(false, "Kanji glyphs from %s", biosPath);
            } else {
                biosMsg.set(true, "%s is not a 512 KB PS1 BIOS image", biosPath);
            }
        }
        if (ImGui::Button(tr("Look beside the disc"))) {
            if (kromSetBios("")) {
                biosMsg.set(false, "Kanji glyphs from %s", kromPath());
            } else {
                biosMsg.set(true, "No BIOS image beside the disc or in the save folder");
            }
        }
        sameLineFor("None##bios");
        if (ImGui::Button(tr("None##bios"))) {
            kromSetBios("none");
            biosMsg.set(false, "No BIOS: the kanji glyphs are blank");
        }
        biosMsg.draw();
        hint("Only the font in the BIOS image is used (save titles, some text over movies). The image is read where it "
             "is and never copied.");
        overrideNote("bios");
    }

    if (row("Current disc")) {
        pathText(cdSwapPath(0), "-");
        hint("The disc image in the drive now.");
    }
    if (row("Other disc", "disc2 disc_swap")) {
        pathText(cdSwapPath(1), "None found: set the other disc's image");
        if (pathField("disc2", disc2Path, sizeof(disc2Path), 31, false, discFilters, 2, "Use")) {
            if (cdSwapSetOther(disc2Path)) {
                discMsg.set(false, "Other disc: %s", disc2Path);
            } else {
                CdImageInfo info[2]; char why[1024];
                cdVerifyPair(cdSwapPath(0), disc2Path, NULL, info, why, sizeof why);
                discMsg.set(true, "%s", why);
            }
        }
        if (ImGui::Button(tr("Default##disc2"))) {
            cdSwapSetOther("");
            discMsg.set(false, cdSwapPath(1) != NULL ? "Other disc: the image beside the first one"
                                                     : "No image beside the first one");
        }
        sameLineFor("Swap discs now");
        ImGui::BeginDisabled(cdSwapPath(1) == NULL || !cdSwapWanted());
        if (ImGui::Button(tr("Swap discs now"))) {
            if (cdSwapRequest() > 0) {
                discMsg.set(false, "Swapped: the drive has %s", cdSwapPath(0));
            } else {
                discMsg.set(true, "Could not swap discs");
            }
        }
        ImGui::EndDisabled();
        discMsg.draw();
        bool automatic = cdSwapAutomatic();
        if (checkbox("Swap when the game asks for the other disc", &automatic)) {
            cdSwapSetAutomatic(automatic);
        }
        hint("Swapping works while the game asks for a disc (\"insert disc\"); F2 does the same. The game asks for disc 2 "
             "at the end of disc 1, and for the other disc when a save from it is loaded. A swap at any other time would "
             "break the game, as opening the lid mid-game does on a PS1.");
        overrideNote("disc2");
        overrideNote("disc_swap");
    }

    opt::group(N_("Language and text"));
    languageRow();
    gameTextRows(); // gametext.cpp

    opt::group(N_("This menu"));
    menuLookRows();

    opt::group(N_("Advanced"), opt::COLLAPSED);
    if (row("Pause when minimised", "pause_minimized")) {
        bool pause = settingsBool("pause_minimized", 0) != 0;
        if (offOnList("##pause_minimized", &pause, N_("Off: the game plays on"), N_("On: the game waits"))) {
            settingsDescribe("pause_minimized", "pause_minimized: 1 pauses the game while its window is minimised "
                                                "(0, default: it plays on)");
            settingsSetInt("pause_minimized", pause ? 1 : 0);
            settingsSave();
        }
        hint("While the window is minimised the game waits as while this menu is open: its clock stands still and "
             "the sound fades out and back. A handheld's suspend and an app switch always pause it.");
        overrideNote("pause_minimized");
    }
    if (row("Settings file")) {
        pathText(settingsPath(), "None (XG_SETTINGS=none): nothing is kept");
        hint("Every change here is written to it at once. XG_<KEY> in the environment overrides a key for one run.");
    }

    opt::group(N_("About"), opt::COLLAPSED | opt::STACKED);
    aboutRows();
    opt::endPage();
}

// ---- About (System's About group) ---------------------------------------------------------------

char deviceLine[256] = "-";

// A licence's text as paragraphs: its hard line breaks joined, so it wraps
// to the pane (blank lines and numbered clauses stay).
const char* reflow(const char* text, char* out, size_t size) {
    size_t o = 0;
    for (const char* p = text; *p != '\0' && o + 1 < size; p++) {
        if (*p == '\r') {
            continue;
        }
        if (*p == '\n' && p[1] != '\n' && p[1] != '\0' && !(p[1] >= '0' && p[1] <= '9' && p[2] == '.') &&
            o > 0 && out[o - 1] != '\n') {
            out[o++] = ' ';
            while (p[1] == ' ') {
                p++; /* the next line's indent */
            }
            continue;
        }
        out[o++] = *p;
    }
    out[o] = '\0';
    return out;
}

void aboutRows() {
    if (row("This build")) {
#ifdef VERSION_JP
        const char* game = tr("Japanese version (SLPS-01160)");
#else
        const char* game = tr("US version (SLUS-00664)");
#endif
        ImGui::TextWrapped(tr("Xenogears PC port, %s"), game);
        ImGui::TextWrapped(tr("Build %s, %s, %s"), XG_BUILD_REV, XG_BUILD_DAY, XG_BUILD_TYPE);
        ImGui::TextWrapped(tr("Engine: %s (%s)"), tr(engineTitle(engineThis())),
                           engineThis() == ENGINE_ENHANCED ? "xenogears-enhanced, engine/" : "xenogears, src/");
#if defined(__clang__)
        ImGui::TextWrapped(tr("Compiler: clang %s"), __clang_version__);
#elif defined(__GNUC__)
        ImGui::TextWrapped(tr("Compiler: GCC %s"), __VERSION__);
#endif
        int sdl = SDL_GetVersion();
        ImGui::TextWrapped(tr("SDL %d.%d.%d (built with %d.%d.%d), video %s, audio %s"), SDL_VERSIONNUM_MAJOR(sdl),
                    SDL_VERSIONNUM_MINOR(sdl), SDL_VERSIONNUM_MICRO(sdl), SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
                    SDL_MICRO_VERSION, SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "-",
                    SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "-");
        ImGui::TextWrapped(tr("Vulkan: %s"), deviceLine);
        ImGui::TextWrapped(tr("Dear ImGui %s"), ImGui::GetVersion());
        hint("The game's code is rebuilt from the original as C (a matching decompilation); its data is read from "
             "your own disc. Keep this build for your own use.");
    }
    if (row("Licences")) {
        // The GPL's "Appropriate Legal Notices" (GPLv3 section 0 and 5d): who
        // holds the copyright, that there is no warranty, and where the licence is.
        ImGui::TextWrapped(tr("Xenogears PC port. Copyright (C) 2026 carl. This program is free software under the GNU "
                           "General Public License, version 3 or any later version, and comes with ABSOLUTELY NO "
                           "WARRANTY; see the licence below. The licence does not cover Xenogears itself: the game's "
                           "data comes from your own discs."));
        static char portText[sizeof(xgLicensePort)];
        if (portText[0] == '\0') {
            reflow(xgLicensePort, portText, sizeof(portText));
        }
        markBlock();
        if (ImGui::CollapsingHeader(tr("Xenogears PC port (GNU GPL v3 or later)"))) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(portText);
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
        textLines("Licences of the libraries in this program");
        static char sdlText[sizeof(xgLicenseSdl)], imguiText[sizeof(xgLicenseImgui)], fontText[sizeof(xgLicenseJukeboxFont)];
        if (sdlText[0] == '\0') {
            reflow(xgLicenseSdl, sdlText, sizeof(sdlText));
            reflow(xgLicenseImgui, imguiText, sizeof(imguiText));
            reflow(xgLicenseJukeboxFont, fontText, sizeof(fontText));
        }
        markBlock();
        if (ImGui::CollapsingHeader(tr("SDL 3 (zlib licence)"))) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(sdlText);
            ImGui::PopTextWrapPos();
        }
        markBlock();
        if (ImGui::CollapsingHeader(tr("Dear ImGui (MIT licence)"))) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(imguiText);
            ImGui::PopTextWrapPos();
        }
        markBlock();
#ifdef XG_LICENSE_GLSLANG
        if (ImGui::CollapsingHeader(tr("Khronos glslang (BSD, MIT, Apache 2.0; shader presets)"))) {
            static char glslangText[sizeof(xgLicenseGlslang)];
            if (glslangText[0] == '\0') {
                reflow(xgLicenseGlslang, glslangText, sizeof(glslangText));
            }
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(glslangText);
            ImGui::PopTextWrapPos();
        }
        markBlock();
#endif
#ifdef XG_LICENSE_DAV1D
        if (ImGui::CollapsingHeader(tr("dav1d (BSD 2-clause licence; AV1 movie videos)"))) {
            static char dav1dText[sizeof(xgLicenseDav1d)];
            if (dav1dText[0] == '\0') {
                reflow(xgLicenseDav1d, dav1dText, sizeof(dav1dText));
            }
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(dav1dText);
            ImGui::PopTextWrapPos();
        }
        markBlock();
#endif
        if (ImGui::CollapsingHeader(tr("Japanese font of the OST titles and this menu (SIL Open Font Licence)"))) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(fontText);
            ImGui::PopTextWrapPos();
        }
        hint("Vulkan is reached through the system's own loader. The right stick or the mouse wheel scrolls.");
    }
}

} // namespace

// ---- menu.h ---------------------------------------------------------------------------------

void sectionsOpened() {
    opt::opened();
    readWindowSize();
    cap.active = false;
    backupSlot = -1;
    cardMsg.text[0] = biosMsg.text[0] = discMsg.text[0] = aspectMsg.text[0] = '\0';
}

void discSetupOpened(const char* region, bool noDisc) {
    setup = {};
    setup.noDisc = noDisc;
    setup.engine = engineSetting() >= 0 ? engineSetting() : engineThis();
    const char* first = noDisc ? settingsGet("disc") : cdSwapPath(0);
    snprintf(setup.first, sizeof setup.first, "%s", first ? first : "");
    snprintf(setup.other, sizeof setup.other, "%s", !noDisc && cdSwapPath(1) ? cdSwapPath(1) : "");
    snprintf(setup.region, sizeof setup.region, "%s", region ? region : "");
    // The folders: the starting image's (or its folder's folder: one a disc).
    std::string where = parentDir(setup.first);
    if (where != setup.first && !where.empty()) {
        std::string above = parentDir(where);
        snprintf(setup.imageDir, sizeof setup.imageDir, "%s",
                 (above != where && strstr(where.c_str(), "Disc") != nullptr ? above : where).c_str());
    }
    const char* bios = kromPath() != nullptr ? kromPath() : settingsGet("bios");
    snprintf(setup.bios, sizeof setup.bios, "%s", bios && strcmp(bios, "none") != 0 ? bios : "");
    setup.withoutBios = bios != nullptr && strcmp(bios, "none") == 0;
    std::string biosDir = parentDir(setup.bios[0] ? setup.bios : setup.first);
    snprintf(setup.biosDir, sizeof setup.biosDir, "%s", biosDir.c_str());
    checkBios();
    if (!setup.biosOk && !setup.withoutBios && setup.biosDir[0]) {
        searchBios(); // beside the image, as the game would look
        if (!setup.biosOk) setup.biosMessage.text[0] = '\0';
    }
    if (setup.first[0]) verifySetup();
    else if (setup.imageDir[0]) searchImages();
}
bool discSetupAccepted() { return setup.accepted; }
const char* discSetupChosen() { return setup.chosen; }
bool discSetupCommand(const char* command, const char* argument) {
    if (!strcmp(command, "setup-first") || !strcmp(command, "setup-other")) {
        char* path = !strcmp(command, "setup-first") ? setup.first : setup.other;
        snprintf(path, 4096, "%s", argument);
        verifySetup();
        return false;
    }
    if (!strcmp(command, "setup-folder")) {
        snprintf(setup.imageDir, sizeof setup.imageDir, "%s", argument);
        searchImages();
        return false;
    }
    if (!strcmp(command, "setup-bios")) {
        snprintf(setup.biosDir, sizeof setup.biosDir, "%s", argument);
        searchBios();
        return false;
    }
    if (!strcmp(command, "setup-nobios")) {
        setup.withoutBios = true;
        setup.bios[0] = '\0';
        checkBios();
        return false;
    }
    return !strcmp(command, "setup-start") && acceptSetup();
}

const char* const setupSectionNames[SETUP_SECTIONS] = {N_("Disc images"), "BIOS"};

// What a missing BIOS takes away: what the game draws with the BIOS's own
// font (B(51h) Krom2RawAdd, port/src/system/krom.c): the save titles on the
// memory card screens (renderSaveTitleImage) and the text that scrolls over
// some scenes (the field's EX BEh lines, getScrollingTextGlyph).
const char* const biosFeatures[] = {N_("Save titles on the memory card screens"), N_("Text that scrolls over some scenes")};

void setupDiscs() {
    opt::beginPage("setup-discs", -1, true);
    row("Image folder");
    if (folderField("setup-folder", setup.imageDir, sizeof setup.imageDir, 42, "Search", true)) searchImages();
    hint("The folder your Xenogears images are in: it and the folders directly in it are searched (a folder for "
         "each disc is fine). Browse lists folders here; Search finds the images.");
    if (setup.searched) {
        row("Found");
        if (setup.foundCount == 0) ImGui::TextDisabled(tr("None"));
        for (int i = 0; i < setup.foundCount; i++) {
            const CdSetupImage& f = setup.found[i];
            const char* name = strrchr(f.path, '/');
            char label[600];
            bool wrongRegion = setup.region[0] && strcmp(f.info.region, setup.region) != 0;
            snprintf(label, sizeof label, "%s Disc %d: %s##found%d", f.info.region, f.info.disc, name ? name + 1 : f.path, i);
            ImGui::BeginDisabled(wrongRegion);
            if (skin::selectable(label, !strcmp(f.path, setup.first) || !strcmp(f.path, setup.other))) {
                copyPath(setup.first, f.path);
                setup.other[0] = '\0';
                for (int j = 0; j < setup.foundCount; j++)
                    if (!strcmp(setup.found[j].info.region, f.info.region) && setup.found[j].info.disc != f.info.disc)
                        copyPath(setup.other, setup.found[j].path);
                verifySetup();
            }
            ImGui::EndDisabled();
        }
        hint("Choose the disc to start on; the other disc of the same release goes with it.%s",
             setup.region[0] ? tr(" This build plays its own release's images only.") : "");
    }
    row("Starting image");
    if (pathField("setup-first", setup.first, sizeof setup.first, 40, false, discFilters, 2, "Verify", true, true)) {
        verifySetup();
    }
    hint("Your Disc 1 or Disc 2 .cue sheet (its BINARY files beside it), or a raw .bin image.");
    row("Other image");
    if (pathField("setup-other", setup.other, sizeof setup.other, 41, false, discFilters, 2, "Verify", true, true)) {
        verifySetup();
    }
    hint("Optional: the other disc of the same release. Empty: found beside the starting image when it is there.");
    row("Status");
    setup.message.draw();
    for (int i = 0; i < 2; i++) {
        if (setup.info[i].disc) {
            ImGui::Text(tr("%s Disc %d: %s"), setup.info[i].region, setup.info[i].disc, setup.info[i].boot);
        }
    }
    hint("Start saves these and plays. XG_DISC and XG_DISC2 override them for one run.");
    engineRow(&setup.engine, false);
    opt::endPage();
}

void setupBios() {
    opt::beginPage("setup-bios", -1, true);
    row("BIOS folder");
    if (folderField("setup-biosdir", setup.biosDir, sizeof setup.biosDir, 43, "Search", true)) searchBios();
    hint("A folder with your PS1 BIOS image (scph5501.bin, scph1001.bin and the like: 512 KB). It is read where it "
         "is and never copied.");
    row("BIOS image");
    if (pathField("setup-bios", setup.bios, sizeof setup.bios, 44, false, biosFilters, 2, "Use", true, true)) {
        checkBios();
        setup.withoutBios = false;
        if (setup.biosOk) setup.biosMessage.set(false, "BIOS image: %s", setup.bios);
        else setup.biosMessage.set(true, "%s is not a 512 KB PS1 BIOS image.", setup.bios);
    }
    hint("Or the image itself.");
    row("Status");
    setup.biosMessage.draw();
    if (setup.biosOk) {
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textGood);
        textLines("Everything is available.");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textWarning);
        textLines(setup.withoutBios ? tr("Playing without a BIOS. Not available:") : tr("No BIOS image. Without one, not available:"));
        ImGui::PopStyleColor();
        for (const char* f : biosFeatures) {
            char line[160];
            snprintf(line, sizeof line, tr("- %s (blank)"), tr(f));
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textDimmed);
            textLines(line);
            ImGui::PopStyleColor();
        }
    }
    hint("The game uses its BIOS only for its own font in those two places; everything else is complete without "
         "one. It can be set later in the settings menu (F1, System).");
    row("Without a BIOS");
    static const char* const playWithout[] = {"No", "Yes"};
    int without = setup.withoutBios ? 1 : 0;
    int v = choice("setup-without", playWithout, 2, without);
    if (v != without) {
        setup.withoutBios = v == 1;
        if (setup.withoutBios) setup.bios[0] = '\0';
        checkBios();
    }
    hint("Yes: play without one, and do not ask again (later launches start right away).");
    opt::endPage();
}

void drawSetupSection(int section) {
    if (section == SETUP_DISCS) setupDiscs();
    else setupBios();
    // Start without a BIOS: asked once, explicitly.
    if (setup.askBios) {
        ImGui::OpenPopup("No BIOS");
        setup.askBios = false;
    }
    if (beginModal("No BIOS")) {
        textLines("No BIOS image is set. Everything works without one but the save titles on the memory card "
                  "screens and the text that scrolls over some scenes, which stay blank. A BIOS can be set later in "
                  "the settings menu (F1, System).");
        if (ImGui::Button(tr("Play without a BIOS"))) {
            setup.withoutBios = true;
            ImGui::CloseCurrentPopup();
            if (acceptSetup()) closeMenu();
        }
        sameLineFor("Choose a BIOS");
        if (ImGui::Button(tr("Choose a BIOS")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
            showSetupSection(SETUP_BIOS);
        }
        ImGui::EndPopup();
    }
    drawBrowser();
}

bool setupStart() {
    if (acceptSetup()) return true;
    sfx::play(sfx::BUZZ); // the game's sound for a choice it refuses
    if (!setup.valid) showSetupSection(SETUP_DISCS); // what is wrong, there (the BIOS: its question)
    return false;
}

// The extras (port/src/extras/extras.h; weak: a build may leave them out).
extern "C" void extrasOpenFromMenu(void) __attribute__((weak));
extern "C" int extrasLeftIntoGame(void) __attribute__((weak));
extern "C" int netplayActive(void) __attribute__((weak)); // system/netplay.c

void extrasSection() {
    markBlock();
    ImGui::TextWrapped(tr("The asset browser and the viewers: the game's files, pictures, models and sounds, from your "
                       "own disc."));
    if (extrasOpenFromMenu == nullptr) {
        hint("Not in this build.");
        return;
    }
    bool session = netplayActive != nullptr && netplayActive(); // an online session's window: one side alone
    ImGui::BeginDisabled(session);
    bool open = ImGui::Button(tr("Open the extras"));
    ImGui::EndDisabled();
    if (session) hint("Not during an online session: they could change this side's game alone.");
    if (open) {
        sfx::play(sfx::CHOOSE);
        extrasOpenFromMenu(); // runs until they are left; the game stays paused
        if (extrasLeftIntoGame != nullptr && extrasLeftIntoGame()) {
            closeMenu(); // the gallery's cutscene replay: the game goes on into it
        }
    }
    hint("The game stays paused while they are open. Leaving them comes back here.");
}

namespace {

// A section's page (the Search page draws them all, filtered: no Extras, no
// Search; options.h).
void drawPage(int section) {
    switch (section) {
    case DISPLAY:
        displaySection();
        break;
    case AUDIO:
        audioSection();
        break;
    case CONTROLS:
        controlsSection();
        break;
    case GAMEPLAY:
        gameplaySection();
        break;
    case PACKS:
        packsSection();
        break;
    case SAVES:
        savesSection(); // the memory cards, then states.cpp's save states
        break;
    case SYSTEM:
        systemSection(); // with About in its last group
        break;
    default:
        break;
    }
}

} // namespace

void drawSection(int section) {
    if (section == EXTRAS) {
        extrasSection();
        arenaSection(); // arena.cpp: the Battle arena block
    } else if (section == SEARCH) {
        opt::searchPage(drawPage, SECTION_COUNT);
    } else {
        drawPage(section);
    }
    drawBrowser();
}

bool capturing() {
    return cap.active;
}

bool captureEvent(const SDL_Event* e) {
    if (cap.gamepad && cap.device != 0 && padProfileDevice(cap.profile) != cap.device) {
        cap.active = false;
        return false;
    }
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN: {
        SDL_Scancode k = e->key.scancode;
        if (e->key.repeat) {
            return true;
        }
        if (k == SDL_SCANCODE_ESCAPE) {
            cap.active = false;
        } else if (cap.gamepad) {
            snprintf(cap.note, sizeof(cap.note), "That is a key: press a gamepad button (Esc cancels).");
        } else if (k == SDL_SCANCODE_DELETE) {
            padSetKey(cap.button, cap.slot, SDL_SCANCODE_UNKNOWN);
            cap.active = false;
        } else if (padKeyReserved(k)) {
            snprintf(cap.note, sizeof(cap.note), "%s is a hotkey: choose another key.", SDL_GetScancodeName(k));
        } else {
            padSetKey(cap.button, cap.slot, k);
            cap.active = false;
        }
        return true;
    }
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING:
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (!cap.gamepad) {
            cap.active = false; /* a key was asked for: a gamepad can still back out */
            return true;
        }
        if (cap.device != 0 && e->gbutton.which != cap.device) {
            return true;
        }
        padSelectProfile(cap.profile);
        if (e->gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) {
            snprintf(cap.note, sizeof(cap.note), "Guide opens the menu: choose another button.");
        } else {
            padSetGamepadInput(cap.button, e->gbutton.button);
            cap.active = false;
        }
        return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (cap.device != 0 && e->gaxis.which != cap.device) {
            return true;
        }
        if (cap.gamepad && e->gaxis.value > 16000 &&
            (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
            padSelectProfile(cap.profile);
            padSetGamepadInput(cap.button, e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? PAD_INPUT_LEFT_TRIGGER
                                                                                         : PAD_INPUT_RIGHT_TRIGGER);
            cap.active = false;
        }
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        return true;
    default:
        return false;
    }
}

void sectionsVulkan(void* physical, void* getInstanceProcAddr, void* instance) {
    PFN_vkGetPhysicalDeviceProperties props = (PFN_vkGetPhysicalDeviceProperties)((PFN_vkGetInstanceProcAddr)
                                                                                     getInstanceProcAddr)(
        (VkInstance)instance, "vkGetPhysicalDeviceProperties");
    if (props == NULL) {
        return;
    }
    VkPhysicalDeviceProperties p;
    props((VkPhysicalDevice)physical, &p);
    snprintf(deviceLine, sizeof(deviceLine), "%.200s, Vulkan %u.%u.%u", p.deviceName, VK_API_VERSION_MAJOR(p.apiVersion),
             VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion));
}

} // namespace menu
