// The "Tables" extra (xg-htn.9): the tables tables.h recovers from the disc
// and the game's code, page by page, in the game's look; each page exports
// as tab-separated text, and a disc of the other region can be compared.
//
// Script commands (XG_EXTRAS_SCRIPT "do ..."):
//   page <name>       fields story places music sounds rates compare
//   select <row>      the row shown first on the page
//   export <name>     a page, or "all", to <export folder>/<region><disc>/tables/<name>.tsv
//   compare <path>    compare with another disc image (the "compare" page)
//   disc <path>       read the tables from this image ("" the drive's again)
//   dump <name>       a page's rows on stderr

#include <SDL3/SDL.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "picker.h"
#include "ui/skin.h"
#include "ui/l10n.h"
#include "viewer.h"

extern "C" {
#include "disc.h"
#include "tables.h"
}

namespace {

using namespace extras;

enum Page { P_FIELDS, P_STORY, P_PLACES, P_MUSIC, P_SOUNDS, P_EFFECTS, P_RATES, P_COMPARE, P_COUNT };

struct PageInfo {
    const char* id;
    const char* title;
    const char* about; // a line over the rows
};

const PageInfo pages[P_COUNT] = {
    {"fields", N_("Field names"),
     N_("Each field's name: the world map place whose exit enters it, or the nearest such field along the "
     "fields' own exits.")},
    {"story", N_("Story chapters"),
     N_("The save screen's location line for each story progress value (field variable 0): file 11/01.")},
    {"places", N_("World places"), N_("The names the world map shows at its exits, and the field each one enters.")},
    {"music", N_("Music selector"),
     N_("The field's music table: score (1C/14+2n), sample bank (1C/13+2b; none: keep the loaded one), flags, "
     "and each score's name, reverb and loop point.")},
    {"sounds", N_("Sound effects"),
     N_("Effects the game's code plays at fixed places, named after what that code does (the game has no "
     "names for them). Effects play with the reverb of the score playing.")},
    {"effects", N_("Battle effects"),
     N_("Each ability effect's file (0E/22+2n): a sprite bundle, then sound banks and more sprites; the mode says "
     "who stays on screen (0 all, 1 none of the party, 2-3 the user).")},
    {"rates", N_("Playback rates"), N_("How often the game steps sprite and Gear animation, from its own frame code.")},
    {"compare", N_("US and JP"), N_("This disc's tables against another disc's (choose one below).")},
};

typedef std::vector<std::string> Row;

struct Table {
    std::vector<std::string> head;
    std::vector<Row> rows;
    mutable std::vector<float> widths; // rendered pixels, shared by headers and rows
    mutable int language = -1;
    mutable const ImFont* font = nullptr;
    mutable float fontSize = 0;
    size_t shown = 99;          // columns shown (the export has them all)
};

struct State {
    int page = P_FIELDS;
    int sel[P_COUNT] = {};
    bool focusRows = false;
    bool focused = false, focusWanted = false; // the focused view (viewer.h): the table across the area
    int wantFocusPages = 1; // frames until the cursor goes to the page list (0: no)
    bool inRows = false; // the cursor is on a row (back goes to the pages)
    int focusedPage = -1; // the page entry the cursor was on last frame
    int scrollTo = -1;
    Table tables[P_COUNT];
    bool built[P_COUNT] = {};
    std::string other; // the compared disc
    std::string region;
};

State* S;

std::string fmt(const char* f, ...) IM_FMTARGS(1);
std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

// ---- the pages' rows ------------------------------------------------------------------------------

Table buildFields() {
    Table t;
    t.head = {N_("Field"), N_("Name"), N_("Exits away"), N_("Exits to")};
    t.shown = 3; // the exits: on the selected field's line
    for (int f = 0; f < xgFieldCount(); f++) {
        std::string exits;
        for (const int* e = xgTablesFieldExits(f); *e >= 0; e++) {
            exits += fmt("%s%d", exits.empty() ? "" : " ", *e);
        }
        int hops = xgTablesFieldHops(f);
        t.rows.push_back({fmt("%d", f), xgFieldName(f), hops < 0 ? "" : fmt("%d", hops), exits});
    }
    return t;
}

Table buildStory() {
    Table t;
    t.head = {N_("Progress"), N_("Chapter")};
    for (int c = 0; c < xgStoryChapterCount(); c++) {
        int start = xgStoryChapterStart(c);
        int end = c + 1 < xgStoryChapterCount() ? xgStoryChapterStart(c + 1) - 1 : xgStoryCount() - 1;
        t.rows.push_back({start == end ? fmt("%d", start) : fmt("%d-%d", start, end), xgStoryChapterName(c)});
    }
    return t;
}

Table buildPlaces() {
    Table t;
    t.head = {N_("Place"), N_("Name"), N_("Field")};
    for (int p = 0; p < xgWorldPlaceCount(); p++) {
        t.rows.push_back({fmt("%d", p), xgWorldPlaceName(p), fmt("%d", xgWorldPlaceField(p))});
    }
    return t;
}

Table buildMusic() {
    Table t;
    t.head = {N_("Id"), N_("Name"), N_("Bank"), N_("Flags"), N_("Score"), N_("Bank file"), N_("Tracks"), N_("Reverb"), N_("Loops")};
    t.shown = 4;
    for (int i = 0; i < xgMusicCount(); i++) {
        XgMusicInfo m;
        if (!xgMusicGet(i, &m)) {
            continue;
        }
        t.rows.push_back({fmt("%d", m.id), m.name, m.bank < 0 ? std::string("none") : fmt("%02X", m.bank),
                          fmt("%d", m.flags), fmt("1C/%02X", m.smdsFile),
                          m.bank < 0 ? std::string("none") : fmt("1C/%02X", m.wdsFile), fmt("%d", m.tracks),
                          fmt("mode %d, depth %d, delay %d, feedback %d", m.reverb.mode, m.reverb.depth, m.reverb.delay,
                              m.reverb.feedback),
                          m.loops ? "yes" : "no"});
    }
    return t;
}

Table buildSounds() {
    Table t;
    t.head = {N_("Sound id"), N_("Name"), N_("SEDS"), N_("Effect")};
    t.shown = 2;
    int seds, effect;
    const char* name;
    for (int i = 0; xgTablesSfxNamed(i, &seds, &effect, &name); i++) {
        t.rows.push_back({seds < 0 ? std::string("?") : fmt("%06X", (unsigned)(seds << 16 | effect)), name,
                          seds < 0 ? std::string("?") : fmt("%04X", seds), fmt("%02X", effect)});
    }
    XgReverb r;
    if (xgSfxReverb(-1, &r)) {
        t.rows.push_back({"", fmt("Reverb before any score: mode %d, depth %d", r.mode, r.depth), "", ""});
    }
    return t;
}

Table buildEffects() {
    Table t;
    t.head = {N_("Effect"), N_("File"), N_("Holds"), N_("Mode"), N_("Size"), N_("Animations"), N_("SEDS"), N_("WDS"), N_("Texture")};
    t.shown = 4;
    for (int i = 0; i < xgBattleEffectCount(); i++) {
        XgBattleEffect e;
        if (!xgBattleEffectGet(i, &e)) {
            continue;
        }
        if (e.members == 0) {
            t.rows.push_back({fmt("%d", e.id), fmt("0E/%03X", e.file), "placeholder", "", "", "", "", "", ""});
            continue;
        }
        std::string holds = "sprite";
        for (int k = 0; k < e.sprites; k++) {
            holds += "+sprite";
        }
        for (int k = 0; k < e.seds; k++) {
            holds += "+seds";
        }
        for (int k = 0; k < e.wds; k++) {
            holds += "+wds";
        }
        t.rows.push_back({fmt("%d", e.id), fmt("0E/%03X", e.file), holds, fmt("%d", e.mode), fmt("%d", e.size),
                          fmt("%d", e.animations), e.seds ? fmt("%d, id %04X", e.seds, e.sedsId) : std::string(""),
                          e.wds ? fmt("%d, bank %02X", e.wds, e.wdsBank) : std::string(""),
                          e.textureFile ? fmt("0E/%03X, %d bytes", e.textureFile, e.textureSize) : std::string("none")});
    }
    return t;
}

Table buildRates() {
    Table t;
    t.head = {N_("Mode"), N_("Sprite/s"), N_("Gear/s"), N_("Frames/s"), N_("Vblanks a frame"), N_("Keeps time")};
    t.shown = 3;
    struct {
        const char* name;
        int mode, slow;
    } modes[] = {{"Field", 1, 0}, {"Field, D6 1", 1, 1}, {"Field, D6 2", 1, 2}, {"World map", 3, 0}, {"Battle", 2, 0}};
    for (auto& m : modes) {
        XgRates r;
        if (xgAnimRates(m.mode, m.slow, &r)) {
            t.rows.push_back({m.name, fmt("%.2f", r.spriteTickRate), fmt("%.2f", r.gearStepRate), fmt("%.3f", r.frameRate),
                              fmt("%d", r.vsyncsPerFrame), r.catchUp ? "yes: a frame runs once per vblank passed" : "no"});
        }
    }
    t.rows.push_back({"Vblank", "", "", fmt("%.4f", xgVsyncRate()), "", ""});
    return t;
}

Table buildPage(int page) {
    switch (page) {
    case P_FIELDS: return buildFields();
    case P_STORY: return buildStory();
    case P_PLACES: return buildPlaces();
    case P_MUSIC: return buildMusic();
    case P_SOUNDS: return buildSounds();
    case P_EFFECTS: return buildEffects();
    case P_RATES: return buildRates();
    default: return S->tables[P_COMPARE];
    }
}

void measure(Table* t) {
    t->language = -1;
}

const Table& table(int page) {
    if (!S->built[page] && page != P_COMPARE) {
        S->tables[page] = buildPage(page);
        measure(&S->tables[page]);
        S->built[page] = true;
    }
    return S->tables[page];
}

void rebuild() {
    for (int p = 0; p < P_COUNT; p++) {
        if (p != P_COMPARE) {
            S->built[p] = false;
        }
    }
    S->region = xgTablesRegion();
}

// ---- comparing two discs -------------------------------------------------------------------------

// Rows that differ between two versions of a page, matched by position.
std::string diffRows(const Table& a, const Table& b, int* differing) {
    int n = (int)std::max(a.rows.size(), b.rows.size());
    int diff = 0;
    std::string first;
    for (int i = 0; i < n; i++) {
        bool same = i < (int)a.rows.size() && i < (int)b.rows.size() && a.rows[(size_t)i] == b.rows[(size_t)i];
        if (!same) {
            if (diff < 6) {
                first += fmt("%s%d", first.empty() ? "" : " ", i);
            }
            diff++;
        }
    }
    *differing = diff;
    return first;
}

// The file tables directory by directory: entries whose sizes differ.
void compareFiles(XgDisc* a, XgDisc* b, Table* out) {
    for (int dir = 0; dir < XG_DISC_DIRS; dir++) {
        int na = xgDiscDirFiles(a, dir), nb = xgDiscDirFiles(b, dir);
        if (na <= 0 && nb <= 0) {
            continue;
        }
        int diff = 0, n = std::max(na, nb);
        for (int f = 1; f <= n; f++) {
            int ea = f <= na ? xgDiscFileEntry(a, dir, f) : -1, eb = f <= nb ? xgDiscFileEntry(b, dir, f) : -1;
            int sa = ea >= 0 ? xgDiscEntry(a, ea).size : 0, sb = eb >= 0 ? xgDiscEntry(b, eb).size : 0;
            diff += sa != sb;
        }
        out->rows.push_back({fmt("Directory %02X", dir),
                             diff == 0 ? std::string("same sizes") : fmt("%d sizes differ", diff),
                             fmt("%d / %d files", na, nb)});
    }
}

void compareWith(const std::string& path) {
    Table* out = &S->tables[P_COMPARE];
    out->head = {N_("Table"), N_("Differences"), N_("Here / there")};
    out->shown = 2;
    out->rows.clear();
    const char* why = nullptr;
    XgDisc* other = xgDiscOpen(path.c_str(), &why);
    if (other == nullptr) {
        error("%s: %s", path.c_str(), why);
        return;
    }
    std::string here = xgTablesRegion();
    Table mine[P_RATES], theirs[P_RATES];
    for (int p = 0; p < P_RATES; p++) {
        mine[p] = table(p);
    }
    // The tables read one disc at a time: read the other's, then come back.
    xgTablesSetDisc(path.c_str());
    std::string there = xgTablesRegion();
    for (int p = 0; p < P_RATES; p++) {
        theirs[p] = buildPage(p);
    }
    xgTablesSetDisc(nullptr);
    rebuild();
    for (int p = 0; p < P_RATES; p++) {
        int differing;
        std::string first = diffRows(mine[p], theirs[p], &differing);
        out->rows.push_back({pages[p].title,
                             differing == 0 ? std::string("same") : fmt("%d rows differ (first: %s)", differing, first.c_str()),
                             fmt("%d / %d rows", (int)mine[p].rows.size(), (int)theirs[p].rows.size())});
    }
    // The fields' exits on their own (the names need the US font's letters).
    {
        Table a = mine[P_FIELDS], b = theirs[P_FIELDS];
        for (Row& r : a.rows) {
            r = {r[0], r.size() > 3 ? r[3] : ""};
        }
        for (Row& r : b.rows) {
            r = {r[0], r.size() > 3 ? r[3] : ""};
        }
        int differing;
        std::string first = diffRows(a, b, &differing);
        out->rows.push_back({"Field exits",
                             differing == 0 ? std::string("same") : fmt("%d fields differ (first: %s)", differing, first.c_str()),
                             fmt("%d / %d fields", (int)a.rows.size(), (int)b.rows.size())});
    }
    out->rows.push_back({"Playback rates", "the same code in both versions", ""});
    out->rows.push_back({"Discs", "", fmt("%s / %s %d", here.c_str(), there.c_str(), xgDiscNumber(other))});
    XgDisc* ours = xgTablesDisc();
    if (ours != nullptr) {
        compareFiles(ours, other, out);
    }
    xgDiscClose(other);
    measure(out);
    S->other = path;
    status("Compared with %s", path.c_str());
}

// ---- export -------------------------------------------------------------------------------------

bool exportPage(int page) {
    const Table& t = page == P_COMPARE ? S->tables[P_COMPARE] : table(page);
    std::string region = S->region.empty() ? std::string("disc") : S->region;
    for (char& c : region) {
        c = (char)tolower((unsigned char)c);
    }
    std::string path = exportPath(fmt("%s1/tables/%s.tsv", region.c_str(), pages[page].id).c_str());
    FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "wb");
    if (f == nullptr) {
        error("Cannot write %s", path.empty() ? pages[page].id : path.c_str());
        return false;
    }
    for (size_t i = 0; i < t.head.size(); i++) {
        fprintf(f, "%s%s", i ? "\t" : "", t.head[i].c_str());
    }
    fprintf(f, "\n");
    for (const Row& r : t.rows) {
        for (size_t i = 0; i < r.size(); i++) {
            fprintf(f, "%s%s", i ? "\t" : "", r[i].c_str());
        }
        fprintf(f, "\n");
    }
    fclose(f);
    status("Exported %s", path.c_str());
    return true;
}

// ---- drawing --------------------------------------------------------------------------------------

struct Columns {
    std::vector<float> x, width;
};

Columns columns(const Table& t, float available) {
    if (t.language != l10nSerial() || t.font != ImGui::GetFont() || t.fontSize != ImGui::GetFontSize()) {
        t.widths.assign(t.head.size(), 0);
        float cap = ImGui::CalcTextSize("M").x * 40;
        for (size_t i = 0; i < t.head.size(); i++)
            t.widths[i] = ImGui::CalcTextSize(tr(t.head[i].c_str())).x;
        for (const Row& row : t.rows)
            for (size_t i = 0; i < row.size() && i < t.widths.size(); i++)
                t.widths[i] = std::max(t.widths[i], std::min(cap, ImGui::CalcTextSize(row[i].c_str()).x));
        t.language = l10nSerial(); t.font = ImGui::GetFont(); t.fontSize = ImGui::GetFontSize();
    }
    Columns c;
    size_t n = std::min(t.shown, t.head.size());
    c.x.resize(n); c.width.assign(n, -1);
    float gap = ImGui::CalcTextSize("  ").x;
    float left = std::max(0.0f, available - gap * (n ? n - 1 : 0));
    size_t remaining = n;
    /* Keep short columns whole and share the rest among long ones. */
    while (remaining) {
        float share = left / remaining;
        bool fixed = false;
        for (size_t i = 0; i < n; i++) if (c.width[i] < 0 && t.widths[i] <= share) {
            c.width[i] = t.widths[i]; left -= c.width[i]; remaining--; fixed = true;
        }
        if (!fixed) {
            for (float& w : c.width) if (w < 0) w = share;
            break;
        }
    }
    float x = 0;
    for (size_t i = 0; i < n; i++) { c.x[i] = x; x += c.width[i] + gap; }
    return c;
}

void drawColumns(const Row& row, const Columns& c, ImVec2 at, bool header, bool hint = false) {
    if (hint) ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < row.size() && i < c.x.size(); i++) {
        if (c.width[i] <= 0) continue;
        const char* text = header ? tr(row[i].c_str()) : row[i].c_str();
        ImVec2 lo(at.x + c.x[i], at.y), hi(lo.x + c.width[i], lo.y + ImGui::GetTextLineHeight());
        dl->PushClipRect(lo, hi, true);
        ImGui::RenderTextEllipsis(dl, lo, hi, hi.x, text, text + strlen(text), nullptr);
        dl->PopClipRect();
    }
    if (hint) ImGui::PopStyleColor();
}

void pagesPane(ImVec2 min, ImVec2 max) {
    if (beginPane("##tablepages", min, max, "Tables", false)) {
        for (int p = 0; p < P_COUNT; p++) {
            ImGui::PushID(p);
            if (listItem(tr(pages[p].title), S->page == p)) {
                if (S->page != p) {
                    S->page = p;
                    S->scrollTo = S->sel[p];
                }
                S->focusRows = true;
                S->focusWanted = true; // the table across the area once this frame is drawn
                sound(2);
            }
            if (ImGui::IsItemFocused()) {
                S->inRows = false;
                if (S->focusedPage != p && S->page != p) {
                    S->page = p; // the rows follow the cursor
                    S->scrollTo = S->sel[p];
                }
                S->focusedPage = p;
            }
            if (S->wantFocusPages == 1 && S->page == p) {
                // The cursor straight onto this entry (SetKeyboardFocusHere
                // loses to the Left that brought us here).
                ImGui::SetItemDefaultFocus();
                ImGui::FocusWindow(ImGui::GetCurrentWindow());
                ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
                ImGui::SetNavCursorVisible(true);
            }
            ImGui::PopID();
        }
        // back() asks a frame ahead: a Left that brought it here is still
        // moving the cursor that frame, and would take the request with it.
        if (S->wantFocusPages > 0) {
            S->wantFocusPages--;
        }
        ImGui::Dummy(ImVec2(0, 8 * unit()));
        if (ImGui::Button(tr("Export this"))) {
            if (exportPage(S->page)) {
                sound(2);
            }
        }
        if (ImGui::Button(tr("Export all"))) {
            bool ok = true;
            for (int p = 0; p < P_COUNT; p++) {
                if (p != P_COMPARE || !S->tables[P_COMPARE].rows.empty()) {
                    ok = exportPage(p) && ok;
                }
            }
            if (ok) {
                status("Exported every table to %s", exportDir());
                sound(2);
            }
        }
        if (ImGui::Button(tr("Export folder"))) {
            openExportDir();
        }
        // Left from the rows lands on whatever is beside them: take it to
        // the page's entry instead.
        if (S->inRows && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && S->wantFocusPages == 0) {
            S->inRows = false;
            S->wantFocusPages = 1;
        }
        ImGui::Dummy(ImVec2(0, 8 * unit()));
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
        ImGui::TextWrapped(tr("Disc: %s"), S->region.empty() ? "none" : S->region.c_str());
        ImGui::PopStyleColor();
    }
    endPane();
}

void rowsPane(ImVec2 min, ImVec2 max) {
    const PageInfo& pi = pages[S->page];
    if (beginPane("##tablerows", min, max, pi.title, false)) {
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
        ImGui::TextWrapped("%s", tr(pi.about));
        ImGui::PopStyleColor();
        if (S->page == P_COMPARE) {
            if (ImGui::Button(tr("Compare with a disc image..."))) {
                openPicker("tables-compare", "Compare with a Xenogears disc image", S->other.c_str(), ".cue;.bin;.img",
                           false);
            }
            std::string path;
            if (picker("tables-compare", &path)) {
                compareWith(path);
            }
        }
        const Table& t = table(S->page);
        // The selected row's columns the rows leave out.
        int sel = S->sel[S->page];
        if (t.shown < t.head.size() && sel >= 0 && sel < (int)t.rows.size()) {
            const Row& r = t.rows[(size_t)sel];
            std::string more;
            for (size_t i = t.shown; i < r.size() && i < t.head.size(); i++) {
                if (!r[i].empty()) {
                    more += fmt("%s%s: %s", more.empty() ? "" : "; ", tr(t.head[i].c_str()), r[i].c_str());
                }
            }
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
            ImGui::TextWrapped("%s %s: %s", tr(t.head[0].c_str()), r[0].c_str(), more.empty() ? "-" : more.c_str());
            ImGui::PopStyleColor();
        }
        Columns cols = columns(t, ImGui::GetContentRegionAvail().x - 22 * unit() - ImGui::GetStyle().ScrollbarSize);
        ImVec2 headAt = ImGui::GetCursorScreenPos(); headAt.x += 11 * unit();
        drawColumns(t.head, cols, headAt, true);
        ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight()));
        ImGui::Separator();
        // Room on the left for the list pointer, as beginPane leaves.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(11 * unit(), 0));
        ImGui::BeginChild("##rowlist", ImVec2(0, 0), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        ImGui::PushID(S->page);
        if (t.rows.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
            ImGui::TextUnformatted(S->page == P_COMPARE ? tr("Nothing compared yet.") : tr("The disc has no such table."));
            ImGui::PopStyleColor();
        }
        ImGuiListClipper clip;
        clip.Begin((int)t.rows.size());
        if (S->scrollTo >= 0 && S->scrollTo < (int)t.rows.size()) {
            clip.IncludeItemByIndex(S->scrollTo);
        }
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                ImGui::PushID(r);
                const Row& row = t.rows[(size_t)r];
                std::string text;
                for (const std::string& cell : row) { if (!text.empty()) text += " | "; text += cell; }
                text += "###table-row";
                bool sel = S->sel[S->page] == r;
                bool unnamed = S->page == P_FIELDS && t.rows[(size_t)r].size() > 1 && t.rows[(size_t)r][1].empty();
                ImVec2 rowAt = ImGui::GetCursorScreenPos();
                if (listItem(text.c_str(), sel, unnamed, false)) {
                    S->sel[S->page] = r;
                    sound(1);
                }
                drawColumns(row, cols, rowAt, false, unnamed);
                if (ImGui::IsItemFocused()) {
                    S->sel[S->page] = r;
                    S->inRows = true;
                }
                if (S->focusRows && sel) {
                    ImGui::SetKeyboardFocusHere(-1);
                    S->focusRows = false;
                }
                if (S->scrollTo == r) {
                    ImGui::SetScrollHereY(0.3f);
                    S->scrollTo = -1;
                }
                ImGui::PopID();
            }
        }
        if (S->focusRows && !t.rows.empty() && S->sel[S->page] >= (int)t.rows.size()) {
            S->sel[S->page] = 0;
        }
        ImGui::PopID();
        ImGui::EndChild();
    }
    endPane();
}

// ---- the viewer --------------------------------------------------------------------------------

void opened() {
    S = new State;
    S->region = xgTablesRegion();
    if (S->region.empty()) {
        error("No Xenogears disc image found: the tables need one");
    }
}

void closed() {
    delete S;
    S = nullptr;
}

// ---- the focused view: the table across the area (every column there is room for) ------------------

void setFocused(bool on) {
    if (on == S->focused) return;
    S->focused = on;
    if (on) {
        S->focusRows = true; // the cursor on the selected row
        focusEnter(false);
        return;
    }
    S->inRows = false;
    focusLeave(); // the page's entry takes the cursor back
}

void focusedView(const Area& a) {
    FocusLayout f = focusLayout(a, 1);
    rowsPane(f.min, f.max);
    if (beginFocusBar(f)) setFocused(false);
    int step = focusButton("Previous") ? -1 : 0;
    if (focusButton("Next")) step = 1;
    if (step) {
        S->page = (S->page + step + P_COUNT) % P_COUNT;
        S->scrollTo = S->sel[S->page];
        S->focusRows = true;
        sound(1);
    }
    if (focusButton("Export this") && exportPage(S->page)) sound(2);
    endFocusBar();
}

void draw(const Area& a) {
    if (S->focused) {
        focusedView(a);
        return;
    }
    float u = a.unit;
    float split = floorf(fminf(150 * u, (a.max.x - a.min.x) * 0.3f) / u) * u;
    pagesPane(a.min, ImVec2(a.min.x + split, a.max.y));
    rowsPane(ImVec2(a.min.x + split + 16 * u, a.min.y), a.max);
    if (S->focusWanted) {
        S->focusWanted = false;
        setFocused(true);
    }
}

bool back() {
    if (S->focused) {
        setFocused(false);
        return true;
    }
    // From the rows, back to the pages; from the pages, home.
    if (S->inRows) {
        S->inRows = false;
        S->wantFocusPages = 2;
        return true;
    }
    return false;
}

int pageByName(const char* name) {
    for (int p = 0; p < P_COUNT; p++) {
        if (strcmp(name, pages[p].id) == 0) {
            return p;
        }
    }
    return -1;
}

bool command(const char* cmd, const char* arg) {
    if (strcmp(cmd, "focus") == 0) {
        setFocused(strcmp(arg, "0") != 0);
        return true;
    }
    if (strcmp(cmd, "page") == 0) {
        int p = pageByName(arg);
        if (p < 0) {
            return false;
        }
        S->page = p;
        S->scrollTo = S->sel[p];
        S->wantFocusPages = 1; // the cursor onto its entry
        return true;
    }
    if (strcmp(cmd, "select") == 0) {
        S->sel[S->page] = atoi(arg);
        S->scrollTo = S->sel[S->page];
        return true;
    }
    if (strcmp(cmd, "export") == 0) {
        if (strcmp(arg, "all") == 0) {
            for (int p = 0; p < P_COUNT; p++) {
                if (p != P_COMPARE || !S->tables[P_COMPARE].rows.empty()) {
                    exportPage(p);
                }
            }
            return true;
        }
        int p = pageByName(arg);
        return p >= 0 && exportPage(p);
    }
    if (strcmp(cmd, "compare") == 0) {
        S->page = P_COMPARE;
        S->wantFocusPages = 1;
        compareWith(arg);
        return true;
    }
    if (strcmp(cmd, "disc") == 0) {
        if (!xgTablesSetDisc(arg[0] != '\0' ? arg : nullptr)) {
            error("%s: not a Xenogears disc image", arg);
        }
        rebuild();
        return true;
    }
    if (strcmp(cmd, "dump") == 0) {
        int p = pageByName(arg);
        if (p < 0) {
            return false;
        }
        const Table& t = table(p);
        for (const Row& r : t.rows) {
            std::string line;
            for (size_t i = 0; i < r.size(); i++) {
                line += (i ? "\t" : "") + r[i];
            }
            fprintf(stderr, "tables: %s: %s\n", pages[p].id, line.c_str());
        }
        return true;
    }
    return false;
}

const Viewer viewer = {"tables", "Game tables",
                       "Names, music, sounds and rates recovered from the disc and the game's code", 80,
                       opened, closed, draw, back, command};

} // namespace

XG_EXTRAS_VIEWER(viewer);
