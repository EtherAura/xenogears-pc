// The asset browser (xg-htn.1): both discs' file tables, and for a file its
// size, kind and place in the file map (docs/ASSETS.md), a hex view, LZSS
// decompression by the game's own decoder, offset bundles (the members,
// relocated by the game's own code as a check), TIM pictures in every CLUT
// (drawn by the view as the GPU would, exported to PNG from our own decoder),
// model archives set up and drawn by the game's own model code, and sound
// banks played on an SPU of their own. Raw exports of whatever is shown.
//
// What is shown is a stack: the file, then what was opened from it (its
// LZSS stream decompressed, a bundle's member, ...); the line at the top
// goes back up it.
//
// Script commands (XG_EXTRAS_SCRIPT "do ..."):
//   disc <n>              show disc n (1, 2) or the n-th image opened
//   image <path>          open another disc image
//   section <n>           decode a field archive section (0-8)
//   select <dir> <file>   open file <file> of directory <dir> (hex)
//   member <i>            open member i of the bundle shown
//   lzss                  decompress the LZSS stream shown
//   up                    back up the stack
//   tab <name>            info hex bundle picture model sound
//   clut <n>              the picture's CLUT
//   model <yaw> <pitch>   the model's rotation (4096 = 360 degrees)
//   play <n>              play sound n (a WDS preset, a SEDS effect)
//   export <what>         raw, png, pngall, wav
//   check                 compare the game's lookups with the table (log)
//   info                  the shown item on stderr

#include <SDL3/SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "picker.h"
#include "ui/l10n.h"
#include "ui/skin.h"
#include "view.h"
#include "viewer.h"
#include "replace.h"
#include "export_provenance.h"
#include "bookmark_source.h"

extern "C" {
#include "cd/cd.h"
#include "disc.h"
#include "files.h"
#include "field_assets.h"
#include "modelprev.h"
#include "preview.h"
#include "writers.h"
}

namespace {

using namespace extras;

// The packs a file's picture can show (the Enhanced look): the texture pack and the sprite
// library's cels, by content (a TIM the game draws as one texture page or one cel).
const unsigned browserLibraries = XG_REPLACE_PACK | XG_REPLACE_CELS;

#define PADDING 64        // zero bytes after every blob (readers that run a little past the end)
#define MAX_READ (24 << 20) // files larger than this are not read whole (movie streams)

struct Blob {
    std::vector<uint8_t> data; // size + PADDING
    int size = 0;
    std::string crumb;         // the line at the top: "04/0B8", "LZSS", "member 3"
    std::string name;          // for exports: "us1/04_0b8_lzss_m3"
    XgKind kind = XG_KIND_UNKNOWN;
    XgTim tim = {};
    XgBundle bundle = {};
    int lzss = -1; // decompressed size
    int clut = 0;
    int member = -1;
    std::vector<int> mAt, mSize; // members: a bundle's, or a picture pack's TIMs
    unsigned id = 0;             // each item shown gets its own (the picture and model follow it)
    int field = -1, section = -1, sourceOffset = -1;
    bool fieldArchive = false;
    XgFieldArchive archive = {};
};

struct Disc {
    XgDisc* disc = nullptr;
    std::string path;
    int check = -2; // the game's lookups vs the table: -2 not run, -1 failed, else differences
};

enum Tab { TAB_INFO, TAB_HEX, TAB_BUNDLE, TAB_PICTURE, TAB_MODEL, TAB_SOUND, TAB_COUNT };
const char* const tabNames[TAB_COUNT] = {"info", "hex", "bundle", "picture", "model", "sound"};

struct Row {
    int dir, file; // file 0: the directory's own row
};

struct State {
    std::vector<Disc> discs;
    int disc = 0;
    bool expanded[XG_DISC_DIRS] = {};
    std::vector<Row> rows;
    bool rowsDirty = true;
    int selDir = -1, selFile = -1;
    std::vector<Blob> stack;
    int tab = TAB_INFO, wantTab = -1;
    std::unique_ptr<View> picture;
    unsigned pictureFor = 0; // the item the picture shows
    int pictureClut = -1;
    bool zeroClear = true;
    uint16_t* pictureVram = nullptr;
    unsigned pictureVersion = 0;
    std::unique_ptr<View> modelView;
    XgModelPreview* model = nullptr;
    unsigned modelFor = 0;
    int yaw = 512, pitch = -256, distance = 0;
    bool spin = false, textures = true, wireframe = false;
    std::string modelWhy;
    int sound = 0;
    std::vector<int16_t> pcm;
    int scrollTo = -1;
    // The focused view (viewer.h): the file chosen, its tabs across the area.
    bool focused = false, focusWanted = false;
    int focusDir = -1, focusFile = -1; // the file it opened on (Back: unchanged, the list's scroll stays)
};

State* S;

const char* kindName(const Blob& b) {
    if (b.fieldArchive) return "field archive";
    if (b.section >= 0 && b.kind == XG_KIND_UNKNOWN) return xgFieldSectionName(b.section);
    return xgKindName(b.kind);
}

// ---- discs -----------------------------------------------------------------------------------

int addDisc(const char* path, bool quiet) {
    for (int i = 0; i < (int)S->discs.size(); i++) {
        if (S->discs[i].path == path) {
            return i;
        }
    }
    const char* why = nullptr;
    XgDisc* x = xgDiscOpen(path, &why);
    if (x == nullptr) {
        if (!quiet) {
            error("%s: %s", path, why);
        }
        return -1;
    }
    Disc d;
    d.disc = x;
    d.path = path;
    S->discs.push_back(d);
    return (int)S->discs.size() - 1;
}

void selectDisc(int i) {
    if (i < 0 || i >= (int)S->discs.size()) {
        return;
    }
    S->disc = i;
    S->scrollTo = 0;
    memset(S->expanded, 0, sizeof(S->expanded));
    S->rowsDirty = true;
    S->selDir = S->selFile = -1;
    S->stack.clear();
}

XgDisc* disc() {
    return S->discs.empty() ? nullptr : S->discs[S->disc].disc;
}

std::string discTag() {
    XgDisc* d = disc();
    if (d == nullptr) {
        return "disc";
    }
    std::string r = xgDiscRegion(d);
    for (char& c : r) {
        c = (char)SDL_tolower(c);
    }
    return (r == "?" ? std::string("xg") : r) + std::to_string(xgDiscNumber(d));
}

void buildRows() {
    S->rows.clear();
    XgDisc* d = disc();
    for (int dir = 0; d != nullptr && dir < XG_DISC_DIRS; dir++) {
        int files = xgDiscDirFiles(d, dir);
        if (files == 0) {
            continue;
        }
        S->rows.push_back({dir, 0});
        for (int f = 1; S->expanded[dir] && f <= files; f++) {
            S->rows.push_back({dir, f});
        }
    }
    S->rowsDirty = false;
}

// ---- the stack ---------------------------------------------------------------------------------

void classify(Blob& b) {
    b.kind = xgIdentify(b.data.data(), b.size);
    b.lzss = -1;
    int used;
    if (b.kind == XG_KIND_LZSS) {
        b.lzss = xgLzssCheck(b.data.data(), b.size, &used);
    }
    if (b.kind != XG_KIND_TIM || !xgTimParse(b.data.data(), b.size, &b.tim)) {
        b.tim = XgTim();
    }
    if (!xgBundleParse(b.data.data(), b.size, &b.bundle)) {
        b.bundle = XgBundle();
    }
    b.mAt.clear();
    b.mSize.clear();
    b.fieldArchive = b.field >= 0 && b.section < 0 && xgFieldArchiveParse(b.data.data(), b.size, &b.archive);
    if (b.fieldArchive) {
        for (const XgFieldSection& s : b.archive.section) {
            b.mAt.push_back(s.offset > 0 ? s.offset : -1);
            b.mSize.push_back(s.bytes);
        }
    } else if (b.bundle.count > 0) {
        for (int i = 0; i < b.bundle.count; i++) {
            int size;
            int at = xgBundleMember(b.data.data(), b.size, &b.bundle, i, &size);
            b.mAt.push_back(at);
            b.mSize.push_back(at >= 0 ? size : 0);
        }
    } else if (b.kind == XG_KIND_TIMS) {
        int n = xgTimRun(b.data.data(), b.size, nullptr, 0);
        b.mAt.resize((size_t)n);
        xgTimRun(b.data.data(), b.size, b.mAt.data(), n);
        for (int i = 0; i < n; i++) {
            b.mSize.push_back((i + 1 < n ? b.mAt[(size_t)i + 1] : b.size) - b.mAt[(size_t)i]);
        }
    }
}

void push(Blob b) {
    static unsigned ids;
    classify(b);
    b.id = ++ids;
    S->stack.push_back(std::move(b));
    Blob& top = S->stack.back();
    S->wantTab = top.kind == XG_KIND_TIM                                ? TAB_PICTURE
                 : !top.mAt.empty() || top.kind == XG_KIND_TEXTURES      ? TAB_BUNDLE
                 : top.kind == XG_KIND_MODEL                            ? TAB_MODEL
                 : top.kind == XG_KIND_WDS || top.kind == XG_KIND_SEDS  ? TAB_SOUND
                                                                        : TAB_INFO;
    S->sound = 0;
}

void pop() {
    if (S->stack.size() > 1) {
        S->stack.pop_back();
        S->wantTab = S->stack.back().member >= 0 ? TAB_BUNDLE : TAB_INFO;
    }
}

Blob makeBlob(const uint8_t* data, int size, const std::string& crumb, const std::string& name) {
    Blob b;
    b.data.assign(data, data + size);
    b.data.resize((size_t)size + PADDING, 0);
    b.size = size;
    b.crumb = crumb;
    b.name = name;
    return b;
}

bool openFile(int dir, int file) {
    XgDisc* d = disc();
    int index = d != nullptr ? xgDiscFileEntry(d, dir, file) : -1;
    if (index < 0) {
        return false;
    }
    S->selDir = dir;
    S->selFile = file;
    S->stack.clear();
    XgEntry e = xgDiscEntry(d, index);
    char crumb[32], name[64];
    snprintf(crumb, sizeof(crumb), "%02X/%03X", dir, file);
    snprintf(name, sizeof(name), "%s/%02x_%03x", discTag().c_str(), dir, file);
    int size = 0;
    uint8_t* data = nullptr;
    if (e.size > MAX_READ) {
        status("%s: %d bytes, too large to show whole (a movie stream?)", crumb, e.size);
    } else if (e.size != 0) {
        data = xgDiscReadEntry(d, index, &size);
    }
    if (data == nullptr && e.size != 0 && e.size <= MAX_READ) {
        error("%s: cannot read it from the image", crumb);
        return false;
    }
    Blob b = makeBlob(data != nullptr ? data : (const uint8_t*)"", data != nullptr ? size : 0, crumb, name);
    if (dir == 4 && file >= 0xB8 && file < 0xB8 + 2 * 730 && (file - 0xB8) % 2 == 0)
        b.field = (file - 0xB8) / 2;
    free(data);
    push(std::move(b));
    S->tab = S->wantTab;
    return true;
}

bool openSection(int k) {
    Blob& b = S->stack.back();
    if (!b.fieldArchive || k < 0 || k >= XG_FIELD_SECTIONS) return false;
    const XgFieldSection& s = b.archive.section[k];
    if (s.offset <= 0 || s.decoded <= 0) return false;
    /* Each section owns its decoder padding; the following stream is not
     * part of this one, even when the final group reads beyond its end. */
    std::vector<uint8_t> stream(b.data.begin() + s.offset, b.data.begin() + s.offset + s.bytes);
    stream.resize((size_t)s.bytes + PADDING, 0);
    int size = 0;
    uint8_t* data = xgLzssDecompress(stream.data(), s.bytes, &size);
    if (data == nullptr || size != s.decoded) { free(data); return false; }
    char crumb[96], suffix[16];
    snprintf(crumb, sizeof(crumb), "section %d: %s", k, xgFieldSectionName(k));
    snprintf(suffix, sizeof(suffix), "_s%d", k);
    Blob section = makeBlob(data, size, crumb, b.name + suffix);
    free(data);
    section.field = b.field;
    section.section = k;
    section.sourceOffset = s.offset;
    b.member = k;
    push(std::move(section));
    return true;
}

bool openMember(int i) {
    Blob& b = S->stack.back();
    if (b.fieldArchive) return openSection(i);
    if (i < 0 || i >= (int)b.mAt.size() || b.mAt[(size_t)i] < 0) {
        return false;
    }
    int at = b.mAt[(size_t)i], size = b.mSize[(size_t)i];
    b.member = i;
    char crumb[32], suffix[16];
    snprintf(crumb, sizeof(crumb), b.kind == XG_KIND_TIMS ? "picture %d" : "member %d", i);
    snprintf(suffix, sizeof(suffix), "_m%d", i);
    Blob m = makeBlob(b.data.data() + at, size, crumb, b.name + suffix);
    m.field = b.field;
    m.section = b.section;
    m.sourceOffset = b.sourceOffset;
    push(std::move(m));
    return true;
}

void decompressTop() {
    Blob& b = S->stack.back();
    int size;
    uint8_t* out = xgLzssDecompress(b.data.data(), b.size, &size);
    if (out == nullptr) {
        error("Not an LZSS stream the game's decoder would take");
        return;
    }
    Blob m = makeBlob(out, size, "LZSS", b.name + "_lzss");
    m.field = b.field;
    m.section = b.section;
    m.sourceOffset = b.sourceOffset;
    free(out);
    push(std::move(m));
    status("Decompressed by the game's decoder: %d bytes", size);
}

// ---- exports -----------------------------------------------------------------------------------

std::vector<ExportSource> exportSources() {
    std::vector<int> members;
    int section = -1;
    for (size_t i = 1; i < S->stack.size(); ++i) {
        const auto& parent = S->stack[i - 1]; const auto& child = S->stack[i];
        if (child.crumb == "LZSS") continue;
        if (parent.fieldArchive) section = parent.member; else members.push_back(parent.member);
    }
    return {exportSource(disc(), S->selDir, S->selFile, "asset", S->stack.back().field, section, members)};
}

void exportRaw() {
    const Blob& b = S->stack.back();
    std::string path = exportPath((b.name + ".bin").c_str());
    FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "wb");
    if (f == nullptr || fwrite(b.data.data(), 1, (size_t)b.size, f) != (size_t)b.size) {
        error("Cannot write %s", path.c_str());
    } else {
        if (recordExport(path, "browser", exportSources())) status("Exported %s", path.c_str());
    }
    if (f != nullptr) {
        fclose(f);
    }
}

bool exportPicture(int clut, ExportManifest* batch = nullptr) {
    const Blob& b = S->stack.back();
    if (b.tim.bpp == 0) {
        return false;
    }
    std::vector<uint32_t> rgba((size_t)b.tim.width * b.tim.imgH);
    xgTimDecode(&b.tim, clut, S->zeroClear, rgba.data());
    char suffix[32] = "";
    if (xgTimClutCount(&b.tim) > 1) {
        snprintf(suffix, sizeof(suffix), "_clut%d", clut);
    }
    std::string path = exportPath((b.name + suffix + ".png").c_str());
    if (path.empty() || !xgPngWrite(path.c_str(), rgba.data(), b.tim.width, b.tim.imgH)) {
        error("Cannot write %s: %s", path.c_str(), xgWriterError());
        return false;
    }
    if (!recordExport(path, "browser", exportSources(), {{"clut", std::to_string(clut)}, {"zero_clear", S->zeroClear ? "1" : "0"}}, batch)) return false;
    status("Exported %s", path.c_str());
    return true;
}

void exportSound() {
    if (S->pcm.empty()) {
        error("Play a sound first");
        return;
    }
    const Blob& b = S->stack.back();
    char suffix[32];
    snprintf(suffix, sizeof(suffix), "_%d.wav", S->sound);
    std::string path = exportPath((b.name + suffix).c_str());
    if (path.empty() || !xgWavWrite(path.c_str(), S->pcm.data(), (int)S->pcm.size() / 2, 2, XG_PREVIEW_RATE)) {
        error("Cannot write %s: %s", path.c_str(), xgWriterError());
        return;
    }
    if (recordExport(path, "browser", exportSources(), {{"sound", std::to_string(S->sound)}})) status("Exported %s", path.c_str());
}

// ---- tabs --------------------------------------------------------------------------------------

void hint(const char* fmt, ...) IM_FMTARGS(1);
void hint(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    ImGui::TextWrappedV(tr(fmt), ap); // in the menu's language (ui/l10n.h)
    ImGui::PopStyleColor();
    va_end(ap);
}

void infoTab() {
    Blob& b = S->stack.back();
    XgDisc* d = disc();
    if (d != nullptr) {
        ImGui::Text(tr("Source: %s disc %d (%s)"), xgDiscRegion(d), xgDiscNumber(d), xgDiscBootName(d));
        ImGui::TextWrapped("%s", xgDiscPath(d));
        ImGui::Separator();
    }
    if (b.field >= 0) {
        ImGui::Text(tr("Field %d"), b.field);
        if (b.section >= 0) ImGui::TextWrapped(tr("Section %d: %s (archive offset %X)"), b.section,
                                      xgFieldSectionName(b.section), b.sourceOffset);
        if (b.fieldArchive) ImGui::TextUnformatted(tr("Field archive: 9 compressed sections"));
    }
    if (S->stack.size() == 1 && d != nullptr) {
        int index = xgDiscFileEntry(d, S->selDir, S->selFile);
        XgEntry e = xgDiscEntry(d, index);
        char where[128];
        xgKnownLocation(xgDiscNumber(d), S->selDir, S->selFile, where, sizeof(where));
        ImGui::TextWrapped(tr("Directory %02X, file %03X: table entry %d"), S->selDir, S->selFile, index);
        if (e.size < 0) {
            ImGui::TextWrapped(tr("A group of %d entries from LBA %d, read as one"), -e.size, e.lba);
        } else {
            ImGui::TextWrapped(tr("LBA %d, %d bytes"), e.lba, e.size);
        }
        int lba, size, group;
        if (xgDiscGameLookup(d, S->selDir, S->selFile, &lba, &size, &group)) {
            hint("The game's own lookup (setCurrentDirectory 0x%02X, getFileStartSector, getFileSize): sector %d, "
                 "size %d%s",
                 S->selDir, lba, size, lba == e.lba && size == e.size ? tr(", the same") : tr(", DIFFERENT"));
        }
        if (where[0] != '\0') {
            ImGui::TextColored(skin::textWarning, tr("File map: %s"), where);
        }
    } else {
        ImGui::TextWrapped(tr("%s: %d bytes"), b.crumb.c_str(), b.size);
    }
    ImGui::TextWrapped(tr("Kind: %s"), kindName(b));
    switch (b.kind) {
    case XG_KIND_LZSS:
        ImGui::TextWrapped(tr("An LZSS stream of %d bytes"), b.lzss);
        if (ImGui::Button(tr("Decompress (the game's decoder)"))) {
            decompressTop();
        }
        break;
    case XG_KIND_TIM:
        ImGui::TextWrapped(tr("TIM, %d-bit, %d x %d at VRAM (%d, %d)"), b.tim.bpp, b.tim.width, b.tim.imgH, b.tim.imgX,
                    b.tim.imgY);
        if (b.tim.hasClut) {
            ImGui::TextWrapped(tr("%d CLUTs at (%d, %d), %d x %d"), xgTimClutCount(&b.tim), b.tim.clutX, b.tim.clutY,
                        b.tim.clutW, b.tim.clutH);
        }
        break;
    case XG_KIND_BUNDLE:
        ImGui::TextWrapped(tr("Offset bundle of %d members%s"), b.bundle.count, b.bundle.hasEnd ? tr(" (with its end offset)") : "");
        break;
    case XG_KIND_TIMS:
        ImGui::TextWrapped(tr("%d TIM pictures back to back"), (int)b.mAt.size());
        break;
    case XG_KIND_TEXTURES:
        ImGui::TextWrapped(tr("Packed VRAM uploads: %d records"), xgTexturesCheck(b.data.data(), b.size));
        break;
    case XG_KIND_MODEL:
        ImGui::TextWrapped(tr("Model archive of %d parts"), xgModelCheck(b.data.data(), b.size));
        break;
    case XG_KIND_WDS:
        ImGui::TextWrapped(tr("Sample bank %d: %d presets"), xgWdsBank(b.data.data(), b.size), xgWdsPresets(b.data.data(), b.size));
        break;
    case XG_KIND_SEDS: {
        int bank;
        int n = xgSedsEffects(b.data.data(), b.size, &bank);
        ImGui::TextWrapped(tr("%d sound effects, sample bank %d"), n, bank);
        break;
    }
    case XG_KIND_SMDS: {
        char name[64];
        ImGui::TextWrapped(tr("Music score \"%s\""), xgSmdsName(b.data.data(), b.size, name, sizeof(name)));
        hint("The jukebox (coming) plays scores with the game's own sound driver.");
        break;
    }
    default:
        break;
    }
    if (b.kind != XG_KIND_LZSS) {
        int used;
        int lz = xgLzssCheck(b.data.data(), b.size, &used);
        if (lz > 0) {
            ImGui::TextWrapped(tr("Also reads as an LZSS stream of %d bytes"), lz);
            if (ImGui::Button(tr("Decompress (the game's decoder)"))) {
                decompressTop();
            }
        }
    }
}

void hexTab() {
    Blob& b = S->stack.back();
    // As many bytes a row as fit: offset, hex and text take 9 + 4n characters.
    float chars = ImGui::GetContentRegionAvail().x / ImGui::CalcTextSize("0").x - ImGui::GetStyle().ScrollbarSize;
    int perRow = chars >= 9 + 4 * 16 ? 16 : chars >= 9 + 4 * 8 ? 8 : 4;
    int rows = (b.size + perRow - 1) / perRow;
    ImGuiListClipper clip;
    clip.Begin(rows);
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
            char line[160];
            int n = snprintf(line, sizeof(line), "%06X ", r * perRow);
            for (int k = 0; k < perRow; k++) {
                int i = r * perRow + k;
                n += i < b.size ? snprintf(line + n, sizeof(line) - (size_t)n, " %02X", b.data[(size_t)i])
                                : snprintf(line + n, sizeof(line) - (size_t)n, "   ");
            }
            n += snprintf(line + n, sizeof(line) - (size_t)n, "  ");
            for (int k = 0; k < perRow && r * perRow + k < b.size; k++) {
                uint8_t c = b.data[(size_t)(r * perRow + k)];
                line[n++] = c >= 0x20 && c < 0x7F ? (char)c : '.';
            }
            line[n] = '\0';
            ImGui::TextUnformatted(line);
        }
    }
}

void bundleTab() {
    Blob& b = S->stack.back();
    if (b.fieldArchive) {
        for (int k = 0; k < XG_FIELD_SECTIONS; k++) {
            const XgFieldSection& s = b.archive.section[k];
            char label[128];
            snprintf(label, sizeof(label), "%d  %-18s  %d bytes", k, xgFieldSectionName(k), s.decoded);
            if (listItem(label, b.member == k, s.decoded == 0)) {
                if (!openSection(k)) error("Cannot decode field section %d", k);
                break; // pushing a section can move this Blob
            }
        }
        return;
    }
    if (b.kind == XG_KIND_TEXTURES) {
        hint("Packed VRAM uploads (uploadTextureToVram): a model bundle's textures. The Model tab of a model beside "
             "them uploads them.");
    }
    if (b.mAt.empty()) {
        hint("No members.");
        return;
    }
    static int checked = -1;
    if (b.bundle.count > 0) {
        if (ImGui::Button(tr("Check the game's relocation"))) {
            checked = xgBundleCheckRelocation(b.data.data(), b.size);
        }
        if (checked >= 0) {
            ImGui::SameLine();
            ImGui::TextColored(checked ? skin::textGood : skin::textError,
                               checked ? tr("pcRelocTable gives every member's address") : tr("the relocation differs"));
        }
    } else {
        hint("TIM pictures back to back.");
    }
    for (int i = 0; i < (int)b.mAt.size(); i++) {
        int at = b.mAt[(size_t)i], size = b.mSize[(size_t)i];
        XgKind k = at >= 0 ? xgIdentify(b.data.data() + at, size) : XG_KIND_EMPTY;
        char label[160];
        snprintf(label, sizeof(label), "%3d  @%06X  %7d  %s", i, at, size, xgKindName(k));
        ImGui::PushID(i);
        if (listItem(label, b.member == i, size == 0)) {
            if (size > 0) {
                openMember(i);
                ImGui::PopID();
                return; // pushing a member can move this Blob
            } else {
                sound(4);
            }
        }
        ImGui::PopID();
    }
}

// The picture: the TIM in the view's own VRAM where it says, drawn as one
// primitive through the chosen CLUT (FREE_UV: the whole picture at once).
void pictureTab() {
    Blob& b = S->stack.back();
    if (b.tim.bpp == 0) {
        hint("Not a TIM picture.");
        return;
    }
    int cluts = xgTimClutCount(&b.tim);
    if (cluts > 1) {
        ImGui::SetNextItemWidth(160 * unit());
        ImGui::SliderInt(tr("CLUT"), &b.clut, 0, cluts - 1);
        ImGui::SameLine();
    }
    ImGui::Checkbox(tr("Texel 0 clear"), &S->zeroClear);
    ImGui::SameLine();
    if (ImGui::Button(tr("Export PNG"))) {
        exportPicture(b.clut);
    }
    if (cluts > 1) {
        ImGui::SameLine();
        if (ImGui::Button(tr("All CLUTs"))) {
            ExportManifest manifest;
            for (int c = 0; c < cluts; c++) {
                exportPicture(c, &manifest);
            }
            writeManifest(manifest, exportPath((b.name + "_pictures.manifest.json").c_str()));
        }
    }
    if (S->picture == nullptr) {
        S->picture.reset(new View);
        S->pictureVram = (uint16_t*)calloc(XG_VRAM_W * XG_VRAM_H, 2);
    }
    if (S->pictureFor != b.id || S->pictureClut != b.clut) {
        S->pictureFor = b.id;
        S->pictureClut = b.clut;
        memset(S->pictureVram, 0, XG_VRAM_W * XG_VRAM_H * 2);
        const XgTim& t = b.tim;
        XgPrim p = {};
        p.count = 4;
        p.flags = XG_PRIM_TEXTURED | XG_PRIM_RAW | XG_PRIM_FREE_UV;
        int ppw = t.bpp == 4 ? 4 : t.bpp == 8 ? 2 : 1;
        int x = t.imgX, y = t.imgY;
        if (t.bpp == 24) {
            // 24-bit: shown as 15-bit (the export keeps 24).
            std::vector<uint32_t> rgba((size_t)t.width * t.imgH);
            xgTimDecode(&t, 0, 0, rgba.data());
            for (int r = 0; r < t.imgH && r < XG_VRAM_H; r++) {
                for (int c = 0; c < t.width && c < XG_VRAM_W; c++) {
                    uint32_t v = rgba[(size_t)r * t.width + c];
                    uint16_t h = (uint16_t)((v >> 3 & 31) | (v >> 11 & 31) << 5 | (v >> 19 & 31) << 10);
                    S->pictureVram[r * XG_VRAM_W + c] = h != 0 ? h : 0x8000;
                }
            }
            x = y = 0;
            ppw = 1;
        } else {
            for (int r = 0; r < t.imgH; r++) {
                for (int c = 0; c < t.imgW; c++) {
                    uint16_t v;
                    memcpy(&v, t.pixels + ((size_t)r * t.imgW + c) * 2, 2);
                    S->pictureVram[((y + r) & 511) * XG_VRAM_W + ((x + c) & 1023)] = v;
                }
            }
            for (int r = 0; t.hasClut && r < t.clutH; r++) {
                for (int c = 0; c < t.clutW; c++) {
                    S->pictureVram[((t.clutY + r) & 511) * XG_VRAM_W + ((t.clutX + c) & 1023)] =
                        t.clut[r * t.clutW + c];
                }
            }
        }
        p.tpage = (uint16_t)((x / 64) | (y / 256) << 4 | (t.bpp == 4 ? 0 : t.bpp == 8 ? 1 : 2) << 7);
        p.clut = xgTimClutWord(&t, b.clut);
        int u0 = (x % 64) * ppw, v0 = y % 256;
        for (int k = 0; k < 4; k++) {
            p.x[k] = (int16_t)(k & 1 ? t.width : 0);
            p.y[k] = (int16_t)(k & 2 ? t.imgH : 0);
            p.u[k] = (int16_t)(u0 + (k & 1 ? t.width : 0));
            p.v[k] = (int16_t)(v0 + (k & 2 ? t.imgH : 0));
            p.r[k] = p.g[k] = p.b[k] = 128;
        }
        p.area[2] = (int16_t)(t.width - 1);
        p.area[3] = (int16_t)(t.imgH - 1);
        S->picture->setPrims(&p, 1);
        S->picture->setVram(S->pictureVram, ++S->pictureVersion);
        S->picture->setScene(0, 0, (float)t.width, (float)t.imgH);
        S->picture->setBackground(IM_COL32(40, 40, 52, 255));
    }
    if (!S->zeroClear) {
        // Texel 0 drawn black: a background of black under the picture.
        S->picture->setBackground(IM_COL32(0, 0, 0, 255));
    } else {
        S->picture->setBackground(IM_COL32(40, 40, 52, 255));
    }
    ImGui::Text(tr("%d x %d, %d-bit, zoom %.2f"), b.tim.width, b.tim.imgH, b.tim.bpp, S->picture->zoom());
    lookOptions(browserLibraries, lookCovered(S->picture.get(), browserLibraries));
    S->picture->setReplacements(lookLibraries(browserLibraries));
    S->picture->draw();
}

// Packed texture uploads next to a model in its bundle (a Gear's member 0).
std::vector<std::pair<const uint8_t*, int>> siblingTextures() {
    std::vector<std::pair<const uint8_t*, int>> out;
    if (S->stack.size() < 2) {
        return out;
    }
    Blob& parent = S->stack[S->stack.size() - 2];
    for (size_t i = 0; i < parent.mAt.size(); i++) {
        int at = parent.mAt[i], size = parent.mSize[i];
        if (at >= 0 && xgTexturesCheck(parent.data.data() + at, size) > 0) {
            out.push_back({parent.data.data() + at, size});
        }
    }
    return out;
}

void modelTab() {
    Blob& b = S->stack.back();
    if (b.kind != XG_KIND_MODEL) {
        hint("Not a model archive.");
        return;
    }
    if (S->modelFor != b.id) {
        xgModelPreviewDestroy(S->model);
        S->model = nullptr;
        S->modelFor = b.id;
        auto tex = siblingTextures();
        std::vector<const uint8_t*> ptrs;
        std::vector<int> sizes;
        for (auto& t : tex) {
            ptrs.push_back(t.first);
            sizes.push_back(t.second);
        }
        char why[128] = "";
        S->model = xgModelPreviewCreate(b.data.data(), b.size, ptrs.data(), sizes.data(), (int)ptrs.size(), why,
                                        sizeof(why));
        S->modelWhy = why;
        if (S->model != nullptr) {
            S->distance = xgModelPreviewRadius(S->model) * 3;
            status("Model: %d parts, %d polygons, %d texture files (the game's model code in a sandbox)",
                   xgModelPreviewParts(S->model), xgModelPreviewPrims(S->model), (int)ptrs.size());
        }
        if (S->modelView == nullptr) {
            S->modelView.reset(new View);
        }
        S->modelView->fit();
    }
    if (S->model == nullptr) {
        ImGui::TextColored(skin::textError, "%s", S->modelWhy.c_str());
        return;
    }
    float u = unit();
    ImGui::SetNextItemWidth(110 * u);
    ImGui::SliderInt(tr("Yaw"), &S->yaw, -2048, 2048);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110 * u);
    ImGui::SliderInt(tr("Pitch"), &S->pitch, -1024, 1024);
    ImGui::Checkbox(tr("Turn"), &S->spin);
    ImGui::SameLine();
    ImGui::Checkbox(tr("Textures"), &S->textures);
    ImGui::SameLine();
    ImGui::Checkbox(tr("Edges"), &S->wireframe);
    if (S->spin) {
        S->yaw = (S->yaw + 2048 + 16) % 4096 - 2048;
    }
    const int w = 640, h = 480;
    int n = xgModelPreviewDraw(S->model, S->yaw, S->pitch, S->distance, w, h);
    S->modelView->show(xgModelPreviewGpu(S->model));
    S->modelView->setFitOnChange(false); // fits the model as first drawn (fit() when it was made)
    S->modelView->setTextures(S->textures);
    S->modelView->setWireframe(S->wireframe);
    S->modelView->setBackground(IM_COL32(24, 26, 40, 255));
    ImGui::Text(tr("%d primitives drawn"), n);
    lookOptions(XG_REPLACE_PACK, lookCovered(S->modelView.get(), XG_REPLACE_PACK), false, N_("No texture pack installed"));
    S->modelView->setReplacements(lookLibraries(XG_REPLACE_PACK));
    S->modelView->draw();
}

// The WDS for a SEDS bank: the resident banks (01/02-05), else directory
// 1C's banks with that number.
std::vector<uint8_t> findWds(int bank) {
    XgDisc* d = disc();
    std::vector<uint8_t> out;
    for (int pass = 0; d != nullptr && pass < 2 && out.empty(); pass++) {
        int dir = pass == 0 ? 1 : 0x1C;
        int files = xgDiscDirFiles(d, dir);
        for (int f = pass == 0 ? 2 : 1; f <= (pass == 0 ? 5 : files) && out.empty(); f++) {
            int index = xgDiscFileEntry(d, dir, f);
            XgEntry e = xgDiscEntry(d, index);
            if (e.size <= 0x30 || e.size > MAX_READ) {
                continue;
            }
            uint8_t* head = xgDiscReadSectors(d, e.lba, 1);
            bool match = head != nullptr && memcmp(head, "wds ", 4) == 0 && xgWdsBank(head, 2048) == bank;
            free(head);
            if (match) {
                int size;
                uint8_t* all = xgDiscReadEntry(d, index, &size);
                if (all != nullptr) {
                    out.assign(all, all + e.size);
                }
                free(all);
            }
        }
    }
    return out;
}

void play(int n) {
    Blob& b = S->stack.back();
    S->sound = n;
    S->pcm.assign((size_t)XG_PREVIEW_RATE * 2 * 8, 0);
    int frames = 0;
    if (b.kind == XG_KIND_WDS) {
        frames = xgPreviewSample(b.data.data(), b.size, n, 60, XG_PREVIEW_RATE, S->pcm.data(), XG_PREVIEW_RATE * 8);
    } else if (b.kind == XG_KIND_SEDS) {
        int bank;
        xgSedsEffects(b.data.data(), b.size, &bank);
        std::vector<uint8_t> wds = findWds(bank);
        if (wds.empty()) {
            error("No sample bank %d on this disc", bank);
        } else {
            frames = xgPreviewEffect(b.data.data(), b.size, wds.data(), (int)wds.size(), n, S->pcm.data(),
                                     XG_PREVIEW_RATE * 8);
        }
    }
    S->pcm.resize((size_t)frames * 2);
    if (frames > 0) {
        xgPreviewPlay(S->pcm.data(), frames);
        status("Sound %d: %.2f s (on an SPU of its own)", n, frames / (double)XG_PREVIEW_RATE);
    } else {
        error("Sound %d: nothing to play (or a driver opcode the preview does not follow)", n);
    }
}

void soundTab() {
    Blob& b = S->stack.back();
    int count = 0;
    if (b.kind == XG_KIND_WDS) {
        count = xgWdsPresets(b.data.data(), b.size);
        hint("Each preset played as a note at its own pitch, held one second.");
    } else if (b.kind == XG_KIND_SEDS) {
        int bank;
        count = xgSedsEffects(b.data.data(), b.size, &bank);
        hint("Each effect as the game's sound driver plays it, with sample bank %d.", bank);
    } else {
        hint("Not a sample bank or a sound effect bank.");
        return;
    }
    if (ImGui::Button(tr("Stop"))) {
        xgPreviewStop();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Export WAV"))) {
        exportSound();
    }
    for (int i = 0; i < count; i++) {
        char label[32];
        snprintf(label, sizeof(label), "%s %d", b.kind == XG_KIND_WDS ? tr("Preset") : tr("Effect"), i);
        ImGui::PushID(i);
        if (listItem(label, S->sound == i)) {
            play(i);
        }
        ImGui::PopID();
    }
}

// ---- the panes ---------------------------------------------------------------------------------

void filesPane(ImVec2 min, ImVec2 max) {
    float u = unit();
    if (beginPane("##files", min, max, nullptr, true)) {
        for (int i = 0; i < (int)S->discs.size(); i++) {
            XgDisc* d = S->discs[i].disc;
            char label[48];
            snprintf(label, sizeof(label), "%s %d##disc%d", xgDiscRegion(d), xgDiscNumber(d), i);
            if (i > 0) {
                ImGui::SameLine();
            }
            if (ImGui::RadioButton(label, S->disc == i)) {
                selectDisc(i);
            }
        }
        if (!S->discs.empty()) {
            ImGui::SameLine();
        }
        if (ImGui::Button(tr("Image..."))) {
            openPicker("browser-image", "Open a Xenogears disc image", S->discs.empty() ? "" : S->discs[0].path.c_str(),
                       ".cue;.bin;.img", false);
        }
        ImGui::Separator();
        if (S->rowsDirty) {
            buildRows();
        }
        XgDisc* d = disc();
        ImGui::BeginChild("##rows", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
        ImGuiListClipper clip;
        clip.Begin((int)S->rows.size());
        if (S->scrollTo >= 0) {
            clip.IncludeItemByIndex(S->scrollTo);
        }
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                Row row = S->rows[(size_t)r];
                char label[160];
                ImGui::PushID(r);
                if (row.file == 0) {
                    snprintf(label, sizeof(label), tr("%s %02X  (%d files)"), S->expanded[row.dir] ? "-" : "+", row.dir,
                             xgDiscDirFiles(d, row.dir));
                    if (listItem(label, false)) {
                        S->expanded[row.dir] = !S->expanded[row.dir];
                        S->rowsDirty = true;
                        sound(2);
                    }
                } else {
                    XgEntry e = xgDiscEntry(d, xgDiscFileEntry(d, row.dir, row.file));
                    char where[96], size[24];
                    xgKnownLocation(xgDiscNumber(d), row.dir, row.file, where, sizeof(where));
                    if (e.size < 0) {
                        snprintf(size, sizeof(size), tr("group %d"), -e.size);
                    } else if (e.size == 0) {
                        snprintf(size, sizeof(size), "%s", tr("empty"));
                    } else if (e.size >= 10240) {
                        snprintf(size, sizeof(size), "%d K", (e.size + 1023) / 1024);
                    } else {
                        snprintf(size, sizeof(size), "%d", e.size);
                    }
                    snprintf(label, sizeof(label), "  %03X %7s %s", row.file, size, where);
                    bool sel = row.dir == S->selDir && row.file == S->selFile;
                    if (listItem(label, sel, e.size == 0)) {
                        if (e.size == 0) {
                            sound(4);
                        } else {
                            if (openFile(row.dir, row.file)) S->focusWanted = true; // across the area next
                            sound(2);
                        }
                    }
                    if (sel && S->scrollTo == r) {
                        ImGui::SetScrollHereY(0.3f);
                        ImGui::SetItemDefaultFocus();
                        ImGui::SetKeyboardFocusHere(-1);
                    }
                }
                ImGui::PopID();
            }
        }
        if (S->scrollTo >= 0 && !S->rowsDirty) {
            S->scrollTo = -1;
        }
        ImGui::EndChild();
        (void)u;
    }
    endPane();
    std::string path;
    if (picker("browser-image", &path)) {
        int i = addDisc(path.c_str(), false);
        if (i >= 0) {
            selectDisc(i);
            status("Opened %s", path.c_str());
        }
    }
}

void detailPane(ImVec2 min, ImVec2 max, bool focused = false) {
    if (S->stack.empty()) {
        if (beginPane("##detail", min, max, "No file", true)) {
            XgDisc* d = disc();
            if (d == nullptr) {
                hint("No Xenogears disc image was found. Open one with Image...");
            } else {
                ImGui::Text(tr("%s, disc %d (%s)"), xgDiscRegion(d), xgDiscNumber(d), xgDiscBootName(d));
                ImGui::TextWrapped("%s", xgDiscPath(d));
                ImGui::Text(tr("%d table entries, %d sectors"), xgDiscEntryCount(d), xgDiscSectors(d));
                Disc& slot = S->discs[S->disc];
                if (slot.check == -2) {
                    slot.check = xgDiscCheckGameLookups(d);
                }
                ImGui::PushStyleColor(ImGuiCol_Text, slot.check == 0 ? skin::textGood : skin::textError);
                if (slot.check == 0) {
                    ImGui::TextWrapped(tr("The game's own file lookups (filesystem.c, in a sandbox) agree with the table."));
                } else {
                    ImGui::TextWrapped(tr("The game's own lookups differ in %d files."), slot.check);
                }
                ImGui::PopStyleColor();
                hint("Open a directory and choose a file. Files are read from the image as the game's file "
                     "table places them.");
            }
        }
        endPane();
        return;
    }
    if (beginPane("##detail", min, max, nullptr, false)) {
        // The stack, as a line of steps back up.
        for (size_t i = 0; i < S->stack.size(); i++) {
            if (i > 0) {
                ImGui::SameLine();
                ImGui::TextUnformatted(">");
                ImGui::SameLine();
            }
            ImGui::PushID((int)i);
            bool last = i + 1 == S->stack.size();
            if (last) {
                ImGui::TextColored(skin::textWarning, "%s", S->stack[i].crumb.c_str());
            } else if (ImGui::SmallButton(S->stack[i].crumb.c_str())) {
                S->stack.resize(i + 1);
                S->wantTab = TAB_BUNDLE;
            }
            ImGui::PopID();
        }
        if (!focused) { // in the focused view's bar instead
            if (ImGui::SmallButton(tr("Export raw"))) {
                exportRaw();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("Bookmark"))) bookmarkSelection();
            ImGui::SameLine();
        }
        ImGui::TextColored(skin::textHint, tr("%s, %d bytes"), kindName(S->stack.back()), S->stack.back().size);
        if (ImGui::BeginTabBar("##tabs")) {
            static const char* const labels[TAB_COUNT] = {N_("Info"), N_("Hex"), N_("Members"), N_("Picture"), N_("Model"),
                                                          N_("Sound")};
            const Blob& b = S->stack.back();
            bool show[TAB_COUNT] = {true, true, !b.mAt.empty() || b.kind == XG_KIND_TEXTURES, b.tim.bpp != 0,
                                    b.kind == XG_KIND_MODEL, b.kind == XG_KIND_WDS || b.kind == XG_KIND_SEDS};
            bool sections = b.fieldArchive;
            for (int t = 0; t < TAB_COUNT; t++) {
                if (!show[t]) {
                    continue;
                }
                ImGuiTabItemFlags f = S->wantTab == t ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(t == TAB_BUNDLE && sections ? tr("Sections") : tr(labels[t]), nullptr, f)) {
                    S->tab = t;
                    ImGui::BeginChild("##tab", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
                    switch (t) {
                    case TAB_INFO:
                        infoTab();
                        break;
                    case TAB_HEX:
                        hexTab();
                        break;
                    case TAB_BUNDLE:
                        bundleTab();
                        break;
                    case TAB_PICTURE:
                        pictureTab();
                        break;
                    case TAB_MODEL:
                        modelTab();
                        break;
                    case TAB_SOUND:
                        soundTab();
                        break;
                    default:
                        break;
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            S->wantTab = -1;
            ImGui::EndTabBar();
        }
    }
    endPane();
}

// ---- the viewer --------------------------------------------------------------------------------

void opened() {
    S = new State;
    // The discs: the one in the drive and its other disc (cd/swap.c), first.
    for (int i = 0; i < 2; i++) {
        const char* p = cdSwapPath(i);
        if (p != nullptr && p[0] != '\0') {
            addDisc(p, true);
        }
    }
    std::sort(S->discs.begin(), S->discs.end(),
              [](const Disc& a, const Disc& b) { return xgDiscNumber(a.disc) < xgDiscNumber(b.disc); });
    if (S->discs.empty()) {
        error("No Xenogears disc image found: open one with Image...");
    }
}

void closed() {
    xgPreviewStop();
    for (Disc& d : S->discs) {
        xgDiscClose(d.disc);
    }
    xgModelPreviewDestroy(S->model);
    free(S->pictureVram);
    delete S; // its views go with it
    S = nullptr;
}

// ---- the focused view: the file's tabs across the area, its controls in a bar ---------------------

void setFocused(bool on) {
    if (on == S->focused || (on && S->stack.empty())) return;
    S->focused = on;
    if (S->picture) S->picture->fit(); // the new space
    if (S->modelView) S->modelView->fit();
    if (on) {
        S->focusDir = S->selDir;
        S->focusFile = S->selFile;
        focusEnter();
        return;
    }
    if (S->selDir != S->focusDir || S->selFile != S->focusFile) { // another file meanwhile: into view
        if (S->rowsDirty) buildRows();
        for (int r = 0; r < (int)S->rows.size(); r++)
            if (S->rows[(size_t)r].dir == S->selDir && S->rows[(size_t)r].file == S->selFile) S->scrollTo = r;
    }
    focusLeave();
}

// The list's previous or next file (directories and empty files skipped).
void stepFile(int delta) {
    if (S->rowsDirty) buildRows();
    XgDisc* d = disc();
    int n = (int)S->rows.size(), at = -1;
    for (int r = 0; r < n; r++)
        if (S->rows[(size_t)r].dir == S->selDir && S->rows[(size_t)r].file == S->selFile) at = r;
    for (int k = 1; d != nullptr && k <= n; k++) {
        const Row& row = S->rows[(size_t)(((at < 0 ? 0 : at) + delta * k) % n + n) % n];
        if (row.file == 0 || xgDiscEntry(d, xgDiscFileEntry(d, row.dir, row.file)).size == 0) continue;
        if (openFile(row.dir, row.file)) sound(1);
        return;
    }
}

void focusedView(const Area& a) {
    if (S->stack.empty()) { setFocused(false); return; }
    FocusLayout f = focusLayout(a, 1);
    detailPane(f.min, f.max, true);
    if (beginFocusBar(f)) setFocused(false);
    if (focusButton("Previous")) stepFile(-1);
    if (focusButton("Next")) stepFile(1);
    if (S->tab == TAB_PICTURE && S->picture) focusZoom(S->picture.get(), 1); // 1:1: one VRAM pixel per window pixel
    if (S->tab == TAB_MODEL && S->modelView) focusZoom(S->modelView.get(), 0);
    if (S->tab == TAB_PICTURE && S->picture) focusLook(browserLibraries, lookCovered(S->picture.get(), browserLibraries));
    if (S->tab == TAB_MODEL && S->modelView) focusLook(XG_REPLACE_PACK, lookCovered(S->modelView.get(), XG_REPLACE_PACK), N_("No texture pack installed"));
    if (focusButton("Export raw")) exportRaw();
    if (focusButton("Bookmark")) bookmarkSelection();
    endFocusBar();
}

void draw(const Area& a) {
    if (S->focused) {
        focusedView(a);
        return;
    }
    float u = a.unit;
    float split = floorf(fminf(300 * u, (a.max.x - a.min.x) * 0.45f) / u) * u;
    filesPane(a.min, ImVec2(a.min.x + split, a.max.y));
    detailPane(ImVec2(a.min.x + split + 16 * u, a.min.y), a.max);
    if (S->focusWanted) {
        S->focusWanted = false;
        setFocused(true);
    }
}

bool back() {
    if (S->stack.size() > 1) {
        pop();
        return true;
    }
    if (S->focused) {
        setFocused(false);
        return true;
    }
    return false;
}

bool command(const char* cmd, const char* arg) {
    if (strcmp(cmd, "focus") == 0) {
        bool on = strcmp(arg, "0") != 0;
        setFocused(on);
        return S->focused == on;
    }
    if (strcmp(cmd, "disc") == 0) {
        int n = atoi(arg);
        for (int i = 0; i < (int)S->discs.size(); i++) {
            if (xgDiscNumber(S->discs[i].disc) == n && (n <= 2)) {
                selectDisc(i);
                return true;
            }
        }
        if (n < 1 || n > (int)S->discs.size()) return false;
        selectDisc(n - 1);
        return true;
    }
    if (strcmp(cmd, "image") == 0) {
        int i = addDisc(arg, false);
        if (i < 0) return false;
        selectDisc(i);
        return true;
    }
    if (strcmp(cmd, "select") == 0) {
        unsigned dir = 0, file = 0;
        if (sscanf(arg, "%x %x", &dir, &file) != 2 || dir >= XG_DISC_DIRS ||
            file < 1 || file > (unsigned)xgDiscEntryCount(disc())) {
            return false;
        }
        S->expanded[dir] = true;
        buildRows();
        for (int r = 0; r < (int)S->rows.size(); r++) {
            if (S->rows[(size_t)r].dir == (int)dir && S->rows[(size_t)r].file == (int)file) {
                S->scrollTo = r;
            }
        }
        return openFile((int)dir, (int)file);
    }
    if (strcmp(cmd, "check") == 0) {
        int n = xgDiscCheckGameLookups(disc());
        fprintf(stderr, "extras: browser: game lookups vs table on %s: %d differences\n", xgDiscPath(disc()), n);
        return true;
    }
    if (S->stack.empty()) {
        return false;
    }
    if (strcmp(cmd, "section") == 0) return openSection(atoi(arg));
    if (strcmp(cmd, "member") == 0) return openMember(atoi(arg));
    if (strcmp(cmd, "lzss") == 0) {
        size_t depth = S->stack.size();
        decompressTop();
        return S->stack.size() > depth;
    }
    if (strcmp(cmd, "up") == 0) {
        pop();
        return true;
    }
    if (strcmp(cmd, "tab") == 0) {
        for (int t = 0; t < TAB_COUNT; t++) {
            if (strcmp(arg, tabNames[t]) == 0) {
                S->wantTab = t;
                return true;
            }
        }
        return false;
    }
    if (strcmp(cmd, "clut") == 0) {
        S->stack.back().clut = atoi(arg);
        return true;
    }
    if (strcmp(cmd, "model") == 0) {
        sscanf(arg, "%d %d", &S->yaw, &S->pitch);
        return true;
    }
    if (strcmp(cmd, "play") == 0) {
        play(atoi(arg));
        return true;
    }
    if (strcmp(cmd, "export") == 0) {
        if (strcmp(arg, "raw") == 0) {
            exportRaw();
        } else if (strcmp(arg, "png") == 0) {
            exportPicture(S->stack.back().clut);
        } else if (strcmp(arg, "pngall") == 0) {
            ExportManifest manifest;
            for (int c = 0; c < (xgTimClutCount(&S->stack.back().tim) > 0 ? xgTimClutCount(&S->stack.back().tim) : 1); c++) {
                exportPicture(c, &manifest);
            }
            writeManifest(manifest, exportPath((S->stack.back().name + "_pictures.manifest.json").c_str()));
        } else if (strcmp(arg, "wav") == 0) {
            exportSound();
        } else {
            return false;
        }
        return true;
    }
    if (strcmp(cmd, "info") == 0) {
        const Blob& b = S->stack.back();
        fprintf(stderr, "extras: browser: source %s disc %d: %s; file %02X/%03X\n",
                xgDiscRegion(disc()), xgDiscNumber(disc()), xgDiscPath(disc()), S->selDir, S->selFile);
        if (b.field >= 0) fprintf(stderr, "extras: browser: field %d section %d archive offset %X\n",
                                  b.field, b.section, b.sourceOffset);
        fprintf(stderr, "extras: browser: %s (%s): %s, %d bytes", b.crumb.c_str(), b.name.c_str(), kindName(b),
                b.size);
        if (b.tim.bpp != 0) {
            fprintf(stderr, ", TIM %d-bit %dx%d, %d CLUTs", b.tim.bpp, b.tim.width, b.tim.imgH, xgTimClutCount(&b.tim));
        }
        if (b.bundle.count != 0) {
            fprintf(stderr, ", bundle of %d", b.bundle.count);
        }
        fprintf(stderr, "\n");
        return true;
    }
    return false;
}

bool bookmark(Bookmark& b) {
    if (S->stack.empty() || !bookmarkSource(b, disc(), "browser", S->stack.back().name)) return false;
    char arg[64]; snprintf(arg, sizeof(arg), "%X %X", S->selDir, S->selFile);
    b.target.commands.push_back({"select", arg});
    for (size_t i = 1; i < S->stack.size(); ++i) {
        const Blob& parent = S->stack[i - 1]; const Blob& child = S->stack[i];
        if (child.crumb == "LZSS") b.target.commands.push_back({"lzss", ""});
        else b.target.commands.push_back({parent.fieldArchive ? "section" : "member", std::to_string(parent.member)});
    }
    b.target.commands.push_back({"clut", std::to_string(S->stack.back().clut)});
    b.target.commands.push_back({"model", std::to_string(S->yaw) + " " + std::to_string(S->pitch)});
    b.target.commands.push_back({"tab", tabNames[S->tab]});
    return true;
}
View* picture() {
    if (S->stack.empty()) return nullptr;
    unsigned id = S->stack.back().id;
    return S->tab == TAB_PICTURE && S->pictureFor == id ? S->picture.get() :
           S->tab == TAB_MODEL && S->modelFor == id ? S->modelView.get() : nullptr;
}

const Viewer viewer = {"browser", "Asset browser",
                       "Both discs' files: kinds, hex, LZSS, offset bundles, TIM pictures, models, sound banks; export",
                       10, opened, closed, draw, back, command, nullptr, nullptr, bookmark, picture};

} // namespace

XG_EXTRAS_VIEWER(viewer);
