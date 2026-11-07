// Read-only scripts and player dialogue history. "do" commands:
// image <path>; field <id>; actor <id>; routine <0-31>; tab scripts|variables|exits|trace|history
// watch <hex byte address>; trace <0|1>; history <0|1>; clear trace|history; info
#include "ui/l10n.h"
#include "viewer.h"
#include "view.h"
#include "ui/assets.h"
#include "gallery.h"
#include "mapfield.h"
#include "observe.h"
#include "script_names.h"
extern "C" {
#include "cd/cd.h"
}
#include <SDL3/SDL.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
using namespace extras;
struct State {
    XgDisc* disc = nullptr;
    XgField* field = nullptr;
    XgFieldScript script = {};
    XgFieldVM vm = {};
    int vars[1024] = {};
    std::vector<int> pcs, watches = {0, 4, 0xA, 0x50};
    std::unique_ptr<View> picture = std::make_unique<View>();
    int id = 120, actor = 0, routine = 0, tab = 0, requestedTab = -1, history = -1;
    uint64_t shown = 0;
    int pictureW = 0, pictureH = 0, historyZoom = 2;
    bool snapshot = false, fromState = false, live = false;
    std::string why;
    char watch[16] = "0000";
};
std::unique_ptr<State> S;
std::vector<int> savedWatches = {0, 4, 0xA, 0x50};
const char* tabs[] = {N_("Scripts"), N_("Variables"), N_("Exits"), N_("Trace"), N_("Dialogue history")}; /* tr() where shown */
const char* tabIds[] = {"scripts", "variables", "exits", "trace", "history"};
int rd16(const uint8_t* p) { return p[0] | p[1] << 8; }
void routine() {
    S->pcs.clear(); S->snapshot = false;
    if (!S->script.code || S->actor < 0 || S->actor >= S->script.actors) return;
    uint16_t roots[1][32];
    for (auto& pc : roots[0]) pc = S->script.routines[S->actor][S->routine];
    XgFieldScript selected = S->script; selected.actors = 1; selected.routines = roots;
    uint8_t* marks = xgFieldScriptWalk(&selected);
    if (marks) {
        for (int i = 0; i < selected.codeSize; i++) if (marks[i]) S->pcs.push_back(i);
        free(marks);
    }
    if (S->live && fromGame()) {
        int field = -1;
        S->snapshot = xgFieldPausedVM(S->actor, &field, nullptr, &S->vm, S->vars) && field == S->id;
    } else S->snapshot = xgFieldVM(S->field, S->actor, &S->vm, S->vars) != 0;
}
void load() {
    xgFieldDestroy(S->field); S->field = nullptr;
    xgFieldScriptFree(&S->script); S->pcs.clear(); S->snapshot = false;
    S->why.clear();
    if (!S->disc || !xgFieldScriptRead(S->disc, S->id, &S->script)) {
        S->why = N_("No readable field script on this image."); return;
    }
    if (S->actor < 0 || S->actor >= S->script.actors) S->actor = 0;
    char why[256] = {};
    std::vector<uint8_t> state((size_t)xgFieldGameStateSize());
    bool use = S->fromState && fromGame() && xgFieldGameState(state.data(), (int)state.size());
    XgFieldStart start = {use, use ? state.data() : nullptr, -1, -1};
    S->field = xgFieldLoad(S->disc, S->id, &start, why, sizeof(why));
    if (!S->field) S->why = why;
    routine();
}
bool image(const char* path) {
    const char* why = nullptr;
    XgDisc* d = xgDiscOpen(path, &why);
    if (!d || xgDiscNumber(d) != 1) { xgDiscClose(d); S->why = why ? why : N_("Choose disc 1."); return false; }
    // A sandbox executes this executable's regional game code.
    const char* driveWhy = nullptr;
    XgDisc* drive = cdSwapPath(0) ? xgDiscOpen(cdSwapPath(0), &driveWhy) : nullptr;
    bool match = drive && !strcmp(xgDiscRegion(d), xgDiscRegion(drive));
    xgDiscClose(drive);
    if (!match) { xgDiscClose(d); S->why = N_("Choose an image matching the running game's region."); return false; }
    xgFieldDestroy(S->field); S->field = nullptr;
    xgDiscClose(S->disc); S->disc = d; load(); return true;
}
void opened() {
    S = std::make_unique<State>();
    S->watches = savedWatches;
    int id = -1;
    if (fromGame() && xgFieldPausedVM(0, &id, nullptr, &S->vm, S->vars)) { S->id = id; S->live = true; }
    for (int i = 0; i < 2 && !S->disc; i++) if (cdSwapPath(i)) image(cdSwapPath(i));
}
void closed() {
    savedWatches = S->watches;
    xgFieldDestroy(S->field); xgFieldScriptFree(&S->script); xgDiscClose(S->disc); S.reset();
}
void instruction(int pc) {
    const auto& s = S->script;
    int n = xgFieldOpLength(s.code, s.codeSize, pc);
    if (!n) return;
    const uint8_t* c = s.code + pc;
    int ext = c[0] == 0xFE ? c[1] : -1;
    std::string bytes;
    for (int i = 0; i < n; i++) { char b[8]; snprintf(b, sizeof(b), "%02X ", c[i]); bytes += b; }
    int at = xgFieldJumpOffset(ext < 0 ? c[0] : ext, ext >= 0);
    std::string annotation;
    if (at && at + 2 <= n) { char b[40]; snprintf(b, sizeof(b), " -> %04X", rd16(c + at)); annotation = b; }
    if (ext < 0 && c[0] == 0x78 && n == 4) annotation += " exit field " + std::to_string(rd16(c + 2)) + ", mode " + std::to_string(c[1]) + ", entry var 004";
    if (ext < 0 && c[0] == 0x87 && n == 3) annotation += " progress " + std::to_string(rd16(c + 1));
    if (ext < 0 && (c[0] == 0xD2 || c[0] == 0xD3 || c[0] == 0x03) && n == 4)
        annotation += " dialogue " + std::to_string(rd16(c + 1));
    ImGui::Text("%04X  %s%s  [%s]", pc, xgScriptName(c[0], ext), annotation.c_str(), bytes.c_str());
}
void scripts() {
    if (!S->script.code) { ImGui::TextWrapped("%s", tr(S->why.c_str())); return; }
    int actor = S->actor, slot = S->routine;
    ImGui::SetNextItemWidth(100 * unit());
    if (ImGui::InputInt(tr("Actor"), &actor) && actor >= 0 && actor < S->script.actors) { S->actor = actor; routine(); }
    ImGui::SetNextItemWidth(100 * unit());
    if (ImGui::SliderInt(tr("Routine"), &slot, 0, 31)) { S->routine = slot; routine(); }
    ImGui::Text(tr("%d actors; %d code bytes; routine entry %04X"), S->script.actors, S->script.codeSize, S->script.routines[S->actor][S->routine]);
    ImGui::TextWrapped(tr("Reachable instructions, including calls and conditional branches. Hex bytes retain operands for unnamed operations."));
    if (S->snapshot) ImGui::Text(tr("%s PC %04X, slot %d, routine %d"), S->live ? tr("Paused game") : tr("Sandbox"), S->vm.pc, S->vm.slot, S->vm.routine);
    if (ImGui::TreeNode(tr("All actor routine entries"))) {
        for (int i = 0; i < 32; i++) { ImGui::PushID(i); char b[40]; snprintf(b, sizeof(b), "%02d: %04X", i, S->script.routines[S->actor][i]);
            if (ImGui::Selectable(b, i == S->routine)) { S->routine = i; routine(); } ImGui::PopID(); }
        ImGui::TreePop();
    }
    ImGui::BeginChild("##instructions", ImVec2(0,0), 0, ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clip; clip.Begin((int)S->pcs.size());
    while (clip.Step()) for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) instruction(S->pcs[i]);
    ImGui::EndChild();
}
void variables() {
    ImGui::TextWrapped(tr("Read-only values from the paused field or after sandbox setup. Addresses are even byte offsets 000-7FE; signedness comes from this field's script header."));
    if (!S->snapshot) { ImGui::TextWrapped(tr("No actor snapshot: %s"), tr(S->why.c_str())); return; }
    ImGui::BeginChild("##variables");
    ImGui::Text(tr("Actor %d: PC %04X, running slot %d"), S->actor, S->vm.pc, S->vm.slot);
    for (int i = 0; i < 8; i++) ImGui::Text(tr("Slot %d: routine %3d, PC %04X, priority %d, delay %d"), i, S->vm.slotRoutine[i], S->vm.slotPC[i], S->vm.priority[i], S->vm.delay[i]);
    ImGui::Separator();
    ImGui::SetNextItemWidth(80 * unit()); ImGui::InputText(tr("Hex variable"), S->watch, sizeof(S->watch), ImGuiInputTextFlags_CharsHexadecimal);
    if (ImGui::Button(tr("Add watch"))) { char* end; long v = strtol(S->watch, &end, 16); if (*S->watch && !*end && v >= 0 && v < 0x800 && !(v & 1) && std::find(S->watches.begin(), S->watches.end(), v) == S->watches.end()) S->watches.push_back((int)v); }
    for (size_t i = 0; i < S->watches.size();) {
        int v = S->watches[i]; ImGui::PushID(v); ImGui::Text("%03X = %d  (0x%04X)", v, S->vars[v/2], (unsigned)S->vars[v/2] & 0xffff); ImGui::SameLine();
        if (ImGui::SmallButton(tr("Remove"))) S->watches.erase(S->watches.begin() + (ptrdiff_t)i); else i++; ImGui::PopID();
    }
    ImGui::EndChild();
}
void exits() {
    ImGui::TextWrapped(tr("Static requests in the selected routine are annotated in Scripts. Probe triggers below in a separate sandbox; the paused game stays unchanged."));
    if (!S->field) return;
    if (ImGui::Button(tr("Probe field exits"))) { xgFieldFindExits(S->field); routine(); }
    const XgFieldInfo* in = xgFieldInfo(S->field);
    for (int i = 0; i < in->exits; i++) {
        const auto& e = in->exit[i];
        ImGui::Text(tr("Actor %d, trigger %d -> %s %d, entry %d"), e.actor, e.trigger, e.field >= 0x1000 ? tr("world place") : tr("field"), e.field & 0xfff, e.entry);
    }
}
void traces() {
    bool on = xgTraceEnabled(); if (ImGui::Checkbox(tr("Record execution trace"), &on)) xgTraceEnable(on);
    ImGui::SameLine(); if (ImGui::Button(tr("Clear trace"))) xgTraceClear();
    ImGui::TextWrapped(tr("Latest %d / %d operations from the running game, oldest first. Recording takes effect on resume; inspection never executes a script or advances paused time."), xgTraceCount(), XG_TRACE_CAPACITY);
    ImGui::BeginChild("##trace", ImVec2(0,0), 0, ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clip; clip.Begin(xgTraceCount());
    while (clip.Step()) for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) { XgTrace t; if (!xgTraceGet(i, &t)) continue;
        ImGui::Text(tr("%llu v%llu field %d actor %d slot %d routine %d PC %04X %02X %s"), (unsigned long long)t.sequence, (unsigned long long)t.vblank, t.field, t.actor, t.slot, t.routine, t.pc, t.ext >= 0 ? t.ext : t.op, xgScriptName(t.op, t.ext)); }
    ImGui::EndChild();
}
void history() {
    bool on = xgHistoryEnabled(); if (ImGui::Checkbox(tr("Keep dialogue history"), &on)) xgHistoryEnable(on);
    ImGui::SameLine(); if (ImGui::Button(tr("Clear history"))) { xgHistoryClear(); S->shown = 0; }
    ImGui::TextWrapped(tr("The last %d dialogue entries as printed by the game. Only revealed text is retained, including Japanese glyphs, names and number substitutions. History lasts for this run."), XG_HISTORY_CAPACITY);
    int count = xgHistoryCount();
    ImGui::SetNextItemWidth(160 * unit());
    if (ImGui::BeginCombo(tr("Entry"), S->history < 0 ? tr("Newest") : std::to_string(S->history + 1).c_str())) {
        if (ImGui::Selectable(tr("Newest"), S->history < 0)) S->history = -1;
        for (int i = 0; i < count; i++) { XgHistory h; xgHistoryGet(i, &h); char b[80]; snprintf(b, sizeof(b), tr("%d: field %d, actor %d, dialogue %d"), i+1, h.field, h.actor, h.dialog); if (ImGui::Selectable(b, S->history == i)) S->history = i; }
        ImGui::EndCombo();
    }
    XgHistory h;
    if (!xgHistoryGet(S->history < 0 ? count-1 : S->history, &h)) { ImGui::TextUnformatted(tr("No dialogue recorded yet.")); return; }
    ImGui::Text(tr("Field %d, actor %d, dialogue %d; %d lines%s"), h.field, h.actor, h.dialog, h.lines, h.truncated ? tr(" (limit reached)") : "");
    if (h.sequence != S->shown) {
        S->pictureW = h.width; S->pictureH = h.lines * 14;
        std::vector<uint32_t> pixels((size_t)h.width * S->pictureH);
        for (int y = 0; y < S->pictureH; y++) for (int x = 0; x < h.width; x++) {
            uint8_t pixel = h.pixels[y * XG_HISTORY_WIDTH + x];
            pixels[y * h.width + x] = pixel ? assets::rgba(pixel & 1 ? h.body : h.outline, false) | 0xff000000 : 0;
        }
        S->picture->setPixels(pixels, S->pictureW, S->pictureH); S->shown = h.sequence;
    }
    ImGui::SetNextItemWidth(100 * unit()); ImGui::SliderInt(tr("Text scale"), &S->historyZoom, 1, 4);
    ImGui::BeginChild("##historyPixels", ImVec2(0,0), 0, ImGuiWindowFlags_HorizontalScrollbar);
    S->picture->draw(ImVec2((float)S->pictureW * S->historyZoom, (float)S->pictureH * S->historyZoom));
    ImGui::EndChild();
}
void draw(const Area& a) {
    if (beginPane("##scripts", a.min, a.max, "Scripts and dialogue", false)) {
        ImGui::SetNextItemWidth(90 * unit()); int field = S->id;
        if (ImGui::InputInt(tr("Field"), &field) && field >= 0 && field < 730) { S->id = field; S->live = false; load(); }
        ImGui::SameLine(); if (ImGui::Checkbox(tr("Sandbox from paused state"), &S->fromState)) { S->live = false; load(); }
        if (fromGame() && ImGui::Button(tr("Inspect paused field"))) {
            int id = -1, actors = 0;
            if (xgFieldPausedVM(0, &id, &actors, &S->vm, S->vars)) { S->id = id; S->actor = 0; S->live = true; load(); }
            else error("The paused game is not in field mode.");
        }
        if (S->live) { ImGui::SameLine(); if (ImGui::Button(tr("Inspect sandbox"))) { S->live = false; routine(); } }
        if (S->disc) ImGui::TextDisabled(tr("%s disc %d: %s"), xgDiscRegion(S->disc), xgDiscNumber(S->disc), xgDiscPath(S->disc));
        if (!S->why.empty()) ImGui::TextWrapped("%s", tr(S->why.c_str()));
        if (ImGui::BeginTabBar("##scriptTabs")) {
            for (int i = 0; i < 5; i++) if (ImGui::BeginTabItem(tr(tabs[i]), nullptr, S->requestedTab == i ? ImGuiTabItemFlags_SetSelected : 0)) {
                S->tab = i;
                if (i == 0) scripts(); else if (i == 1) variables(); else if (i == 2) exits(); else if (i == 3) traces(); else history();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        S->requestedTab = -1;
    }
    endPane();
}
bool command(const char* cmd, const char* arg) {
    if (!strcmp(cmd,"paused")) {
        int id = -1;
        if (fromGame() && xgFieldPausedVM(0, &id, nullptr, &S->vm, S->vars)) { S->id = id; S->actor = 0; S->live = true; load(); }
        return true;
    }
    if (!strcmp(cmd,"image")) { image(arg); return true; }
    if (!strcmp(cmd,"field")) { int n = atoi(arg); if (n >= 0 && n < 730) { S->id = n; S->live = false; load(); } return true; }
    if (!strcmp(cmd,"actor")) { int n = atoi(arg); if (n >= 0 && n < S->script.actors) { S->actor = n; routine(); } return true; }
    if (!strcmp(cmd,"routine")) { int n = atoi(arg); if (n >= 0 && n < 32) { S->routine = n; routine(); } return true; }
    if (!strcmp(cmd,"tab")) { for (int i = 0; i < 5; i++) if (!strcmp(arg, tabIds[i])) S->requestedTab = i; return true; }
    if (!strcmp(cmd,"trace")) { xgTraceEnable(atoi(arg)); return true; }
    if (!strcmp(cmd,"history")) { xgHistoryEnable(atoi(arg)); return true; }
    if (!strcmp(cmd,"clear")) { if (!strcmp(arg,"trace")) xgTraceClear(); if (!strcmp(arg,"history")) xgHistoryClear(); S->shown = 0; return true; }
    if (!strcmp(cmd,"watch")) { char* end; long n = strtol(arg, &end, 16); if (*arg && !*end && n >= 0 && n < 0x800 && !(n&1) && std::find(S->watches.begin(), S->watches.end(), n) == S->watches.end()) S->watches.push_back((int)n); return true; }
    if (!strcmp(cmd,"exits")) { if (S->field) { xgFieldFindExits(S->field); routine(); } return true; }
    if (!strcmp(cmd,"info")) {
        fprintf(stderr,"[scripts] field=%d actor=%d routine=%d entry=%04X instructions=%zu snapshot=%d pc=%04X var000=%d var004=%d trace=%d enabled=%d history=%d vblank=%llu live=%d\n", S->id,S->actor,S->routine,S->script.code ? S->script.routines[S->actor][S->routine] : 0,S->pcs.size(),S->snapshot,S->vm.pc,S->vars[0],S->vars[2],xgTraceCount(),xgTraceEnabled(),xgHistoryCount(),(unsigned long long)xgObserveVblank(),S->live);
        for (int i=0;i<xgHistoryCount();i++) { XgHistory h; xgHistoryGet(i,&h); uint64_t hash=14695981039346656037ull; for(int p=0;p<XG_HISTORY_WIDTH*h.lines*14;p++) hash=(hash^h.pixels[p])*1099511628211ull;
            fprintf(stderr,"[history] field=%d actor=%d dialog=%d lines=%d hash=%016llx\n",h.field,h.actor,h.dialog,h.lines,(unsigned long long)hash); }
        return true;
    }
    return false;
}
const Viewer viewer = {"scripts", "Scripts and dialogue", "Actor routines, variables, execution traces and dialogue history", 90, opened, closed, draw, nullptr, command};
}
XG_EXTRAS_VIEWER(viewer);
