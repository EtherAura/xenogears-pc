// The gallery (xg-htn.6): the game's movies played by its own video player,
// the dialogue portraits and event pictures, and cutscene replay: the game
// started at a scene of the story.
//
// Everything is read from the player's discs when the gallery opens
// (gallery_scan.c): the movies from the movie streams' own frame headers,
// where the game plays each one from the field scripts, and the scenes from
// the scripts' story progress ops. Movies play in gallery_movie.c (the
// video_player overlay in a sandbox and a world of its own: drive, MDEC,
// SPU). A scene starts the game (system/options.c portRequestStart): the
// field, its entrance and the story progress before the scene, as
// XG_FIELD/XG_GAMESTATE do; leaving the extras then runs it, and F1, Extras
// opens a fresh Extras menu.
//
// Script commands (XG_EXTRAS_SCRIPT "do ..."):
//   tab <movies|pictures|scenes>
//   page <-1|1>            the previous or next tab (L1/R1, visuals.cpp)
//   wait                   finish the scan now (blocks)
//   movie <n>              select movie n of the list (and play it)
//   pause, stop, seek <frame>
//   frame                  export the frame shown (PNG)
//   wav                    export the movie's audio so far (WAV)
//   picture <n>            select picture n of the list
//   png, pngall            export it, or every picture of its kind (PNG)
//   scene <n>              select scene n
//   override <0|1>         explicitly allow an unverified/custom start
//   party <a> <b> <c>      custom character IDs (255 empty)
//   image <path>         select one exact image
//   media <field> <pc>    open a reachable literal movie/picture request
//   clip <stream> <offset> <frame> / hold <frame>   restore an exact movie frame
//   clut <n> / zero <0|1>  picture palette and transparency
//   portraitset <id> / portraitview <original|upscaled|compare>
//   variable <index> <v>   custom supplemental field variable
//   list                   the movies, uses and scenes on stderr

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "picker.h"
#include "ui/l10n.h"
#include "ui/skin.h"
#include "viewer.h"
#include "bookmark_source.h"
#include "entity_names.h"
#include "export_provenance.h"

extern "C" {
#include "cd/cd.h"
#include "disc.h"
#include "files.h"
#include "gallery.h"
#include "gallery_scene.h"
#include "writers.h"
#include "gpu/portrait_pack.h"
#include "gpu/gpu.h"
}

// tables.c (xg-htn.9) is another agent's: reached through weak references,
// so the gallery builds and runs without it.
extern "C" __attribute__((weak)) const char* xgFieldName(int field);
extern "C" __attribute__((weak)) const char* xgStoryName(int progress);

namespace {

using namespace extras;

#define TEX_W 640
#define TEX_H 480

enum Tab { TAB_MOVIES, TAB_PICTURES, TAB_SCENES, TAB_COUNT };
const char* const tabNames[TAB_COUNT] = {"movies", "pictures", "scenes"};
const char* const tabLabels[TAB_COUNT] = {N_("Movies"), N_("Pictures"), N_("Scenes")}; /* tr() where shown */

// The pictures: the game's numbering (src/field: portrait n is file 0x46 + n
// of directory 4, event picture n file 0x7FB + n), and the logo.
struct PicKind {
    const char* name;
    int first, count; // files of directory 4
};
const PicKind picKinds[] = {{N_("Portraits"), 0x46, 91}, {N_("Event pictures"), 0x7FB, 88}, {N_("Logo"), 0xAC, 1}};

struct Pic {
    int kind, n, file;
};

struct State {
    XgDisc* discs[2] = {nullptr, nullptr};
    int sourceDisc = 0;
    XgGalleryScan* scan = nullptr;
    bool scanned = false;
    int tab = TAB_MOVIES, wantTab = -1;
    // movies
    bool hasMedia = false;
    XgFieldMedia media = {};
    int clipFirst = 1, clipLast = 0;
    int movieSel = -1;
    XgMovie* player = nullptr;
    int playerDisc = 0;
    int playing = -1; // the movie the player has
    bool paused = false;
    int frame = 0, frameW = 0, frameH = 0;
    int seekTo = 0;
    std::vector<uint32_t> rgba;
    // pictures
    std::vector<Pic> pics;
    int picSel = -1, picCursor = -1;
    std::vector<uint8_t> picData;
    XgTim tim = {};
    int clut = 0;
    bool zeroClear = true;
    int picW = 0, picH = 0;
    std::vector<uint32_t> picRgba;
    int portraitView = 0; // Original, Upscaled, Compare
    // The gallery's Stock / Enhanced (viewer.h) is its portraits' view: Stock the original,
    // Enhanced the portrait set's picture, Compare both (in the options only). The look last
    // seen (-2: not yet: the first view follows the portrait set chosen in the settings).
    int lookSeen = -2;
    uint64_t portraitGeneration = 0, portraitRevision = 0;
    int portraitSize = 0;
    bool portraitChecked = false; // this picture against this generation of the sets
    // scenes
    int sceneSel = -1;
    PortStartRequest start = {};
    bool confirm = false;
    bool overrideStart = false;
    XgDisc* namesDisc = nullptr;
    std::string partyNames[11];
    // the picture shown (texture contents)
    int shown = -1; // 0 movie, 1 picture
    // what logShown last wrote to the extras' log
    int loggedTab = -1, loggedRows = -2;
    // The cursor goes to the shown list's selected row (else its first) when
    // that list is next drawn: after an opening, a resume or a tab change, so
    // a gamepad starts in the list, not on the header's Library button
    // (xg-htn.35.93).
    bool focusList = true;
    // The focused view (viewer.h): the movie, picture or scene chosen across
    // the area; a picture's zoom (0: fitted) and pan.
    bool focused = false, focusWanted = false;
    float zoom = 0;
    ImVec2 pan = ImVec2(0, 0);
};

State* S;

// ---- the picture: an RGBA texture of ImGui's (kept for the program) -------------------------------

ImTextureData* texture() {
    static ImTextureData* tex;
    if (tex == nullptr) {
        tex = IM_NEW(ImTextureData)();
        tex->Create(ImTextureFormat_RGBA32, TEX_W, TEX_H);
        memset(tex->GetPixels(), 0, (size_t)tex->GetSizeInBytes());
        ImGui::RegisterUserTexture(tex);
    }
    return tex;
}

ImTextureData* portraitTexture() {
    static ImTextureData* tex;
    if (!tex) {
        tex=IM_NEW(ImTextureData)(); tex->Create(ImTextureFormat_RGBA32,512,512);
        memset(tex->GetPixels(),0,(size_t)tex->GetSizeInBytes()); ImGui::RegisterUserTexture(tex);
    }
    return tex;
}

// Looked up once per picture and set generation (a reload or another set
// starts a new one), with or without a replacement: not every frame
// (xg-j85.39).
// The gallery's look (viewer.h) and its portraits' view go together: the look chosen
// (the bar, F12, Select, the options) sets the view; the view chosen (its combo, a
// script) sets the look. The first view: the settings' portrait set (viewer.h).
void syncLook() {
    int l = look(LOOK_PORTRAITS);
    if (l != S->lookSeen) {
        S->portraitView = l;
        S->lookSeen = l;
    }
}

void portraitLook() {
    setLook(S->portraitView);
    S->lookSeen = S->portraitView;
}

// What the portrait shows of the sets (viewer.h lookCovered's answers).
int portraitCovered() {
    bool sets = false;
    for (int i = 0; i < portraitPackCount(); ++i)
        if (strcmp(portraitPackSet(i)->id, "original") != 0) sets = true;
    if (!sets) return LOOK_NO_PACK;
    return S->portraitSize ? LOOK_COVERED : LOOK_NOT_HERE;
}

// The pictures' options (a submenu: the list's Options, the focused bar's Options): the
// portraits' look, Compare (original | set) included.
void pictureOptions(const char* id) {
    if (!commandPopup(id)) return;
    lookOptions(LOOK_PORTRAITS, portraitCovered(), true, N_("No portrait pack installed"));
    ImGui::EndPopup();
}

// A movie pack (gpu/moviepack.h): movies/ in the texture pack's version folder.
bool moviePackPresent() {
    const char* root = texturePackRoot();
    if (root == nullptr || !*root) return false;
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s/movies", root, texturePackVersion());
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

void updatePortrait() {
    PortraitPackStats stats; portraitPackStats(&stats);
    if (S->portraitChecked && S->portraitGeneration==stats.generation) return;
    S->portraitChecked=true;
    S->portraitGeneration=stats.generation; S->portraitRevision=0; S->portraitSize=0;
    PortraitImage image;
    if (!portraitPackTim(S->picData.data(),S->picData.size(),&image)) return;
    ImTextureData* tex=portraitTexture();
    for(int y=0;y<image.size;++y) memcpy(tex->GetPixelsAt(0,y),image.rgba+(size_t)y*image.size*4,(size_t)image.size*4);
    if(tex->Status==ImTextureStatus_OK || tex->Status==ImTextureStatus_WantUpdates)
        ImTextureDataQueueUpload(tex,0,0,image.size,image.size);
    S->portraitSize=image.size; S->portraitRevision=image.revision;
}

void drawPortrait(ImVec2 size,bool upscale) {
    float scale=fminf(size.x/64,size.y/64); if(scale>=2) scale=floorf(scale);
    ImVec2 at=ImGui::GetCursorScreenPos(), dim(64*scale,64*scale);
    ImGui::SetCursorScreenPos(ImVec2(floorf(at.x+(size.x-dim.x)/2),at.y));
    if(upscale && S->portraitSize)
        ImGui::Image(portraitTexture()->GetTexRef(),dim,ImVec2(0,0),ImVec2(S->portraitSize/512.f,S->portraitSize/512.f));
    else ImGui::Image(texture()->GetTexRef(),dim,ImVec2(0,0),ImVec2(S->picW/(float)TEX_W,S->picH/(float)TEX_H));
}

void upload(const uint32_t* rgba, int w, int h) {
    ImTextureData* tex = texture();
    w = w < TEX_W ? w : TEX_W;
    h = h < TEX_H ? h : TEX_H;
    for (int y = 0; y < h; y++) {
        memcpy(tex->GetPixelsAt(0, y), rgba + (size_t)y * w, (size_t)w * 4);
    }
    if (tex->Status == ImTextureStatus_OK || tex->Status == ImTextureStatus_WantUpdates) {
        ImTextureDataQueueUpload(tex, 0, 0, w, h);
    }
}

// The picture centred in `size`: whole multiples of its pixels when it fits,
// else scaled down to fit.
void drawPicture(int w, int h, ImVec2 size) {
    if (w <= 0 || h <= 0) {
        return;
    }
    float s = fminf(size.x / w, size.y / h);
    if (s >= 2.0f) {
        s = floorf(s); /* whole pixels when it can */
    }
    ImVec2 at = ImGui::GetCursorScreenPos();
    ImVec2 dim(w * s, h * s);
    ImGui::SetCursorScreenPos(ImVec2(floorf(at.x + (size.x - dim.x) / 2), at.y));
    ImGui::Image(texture()->GetTexRef(), dim, ImVec2(0, 0), ImVec2((float)w / TEX_W, (float)h / TEX_H));
}

void hint(const char* fmt, ...) IM_FMTARGS(1);
void hint(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    ImGui::TextWrappedV(tr(fmt), ap); // in the menu's language (ui/l10n.h)
    ImGui::PopStyleColor();
    va_end(ap);
}

// On screen in the menu's language; `english` for the logs (status keeps them English).
std::string fieldName(int field, bool english = false) {
    const char* n = xgFieldName != nullptr ? xgFieldName(field) : nullptr;
    char buf[160];
    if (n != nullptr && n[0] != '\0') {
        snprintf(buf, sizeof(buf), english ? "field %d, %s" : tr("field %d, %s"), field, n);
    } else {
        snprintf(buf, sizeof(buf), english ? "field %d" : tr("field %d"), field);
    }
    return buf;
}

std::string regionTag(int disc) {
    XgDisc* d = S->discs[disc - 1];
    std::string r = d != nullptr ? xgDiscRegion(d) : "xg";
    for (char& c : r) {
        c = (char)SDL_tolower(c);
    }
    return (r == "?" ? std::string("xg") : r) + std::to_string(disc);
}

// ---- the scan -----------------------------------------------------------------------------------

void stepScan(int budget) {
    if (S->scan == nullptr || S->scanned) {
        return;
    }
    if (xgScanStep(S->scan, budget)) {
        S->scanned = true;
        status("Gallery: %d movies, %d places the game plays them, %d scenes", xgScanMovieCount(S->scan),
               xgScanUseCount(S->scan), xgScanSceneCount(S->scan));
        logEvent("gallery: scan done: %d movies, %d uses, %d scenes", xgScanMovieCount(S->scan),
                 xgScanUseCount(S->scan), xgScanSceneCount(S->scan));
    }
}

// The extras' log (viewer.h logEvent): the tab shown with its rows, or why the
// list has none yet, when either changes (xg-htn.35.91: a reported empty
// gallery that could not be reproduced).
void logShown() {
    int rows = S->tab == TAB_PICTURES ? (int)S->pics.size()
               : !S->scanned          ? -1
               : S->tab == TAB_MOVIES ? xgScanMovieCount(S->scan)
                                      : xgScanSceneCount(S->scan);
    if (S->tab == S->loggedTab && rows == S->loggedRows) {
        return;
    }
    S->loggedTab = S->tab;
    S->loggedRows = rows;
    if (rows >= 0) {
        logEvent("gallery: %s tab, %d rows", tabNames[S->tab], rows);
    } else {
        logEvent("gallery: %s tab, %s", tabNames[S->tab], S->scan != nullptr ? "reading the discs" : "no disc image");
    }
}

void showTab(int t) {
    if (S->tab != t) {
        sound(1);
    }
    S->tab = t;
    S->focusList = true;
}

// In a list, after the row it drew: the cursor to it if the list wants focus
// and this is its row (focusList).
void listFocus(int row, int selected) {
    if (S->focusList && row == (selected >= 0 ? selected : 0)) {
        ImGui::SetKeyboardFocusHere(-1);
        ImGui::SetScrollHereY(0.5f);
        S->focusList = false;
    }
}

bool scanPane(ImVec2 min, ImVec2 max, const char* title) {
    if (S->scanned) {
        return false;
    }
    if (beginPane("##gscan", min, max, title, false)) {
        if (S->scan == nullptr) {
            hint("No Xenogears disc image was found.");
        } else {
            ImGui::TextWrapped(tr("Reading the discs: movie streams, field scripts and battle events."));
            ImGui::ProgressBar((float)xgScanProgress(S->scan), ImVec2(-1, 0), xgScanDoing(S->scan));
        }
    }
    endPane();
    return true;
}

// ---- movies -------------------------------------------------------------------------------------

// The movie's place in the list: "13" or "6.2" (the second movie of stream 6).
std::string movieName(int i) {
    const XgMovieEntry* m = xgScanMovie(S->scan, i);
    int part = 1, parts = 0;
    for (int k = 0; k < xgScanMovieCount(S->scan); k++) {
        const XgMovieEntry* o = xgScanMovie(S->scan, k);
        if (o->disc == m->disc && o->stream == m->stream) {
            parts++;
            if (k < i) {
                part++;
            }
        }
    }
    char buf[32];
    if (parts > 1) {
        snprintf(buf, sizeof(buf), "%d.%d", m->stream, part);
    } else {
        snprintf(buf, sizeof(buf), "%d", m->stream);
    }
    return buf;
}

std::string useText(const XgMovieUse* u) {
    char buf[200];
    if (u->field == -2) {
        snprintf(buf, sizeof(buf), tr("Battle event %d: frames %d-%d"), u->event, u->first, u->last);
    } else if (u->field < 0) {
        snprintf(buf, sizeof(buf), tr("The opening (the kernel's movie mode): frames %d-%d"), u->first, u->last);
    } else {
        const char* how = u->op == 0xA0 ? "op A0" : u->op == 0x60 ? tr("op 60, full screen") : tr("op 67, a background");
        snprintf(buf, sizeof(buf), tr("In %s at PC %04X: frames %d-%d (%s)"), fieldName(u->field).c_str(), u->pc, u->first, u->last, how);
    }
    return buf;
}

void stopMovie() {
    if (S->player != nullptr) {
        xgMovieStop(S->player);
    }
    xgMovieAudioStop();
    S->paused = false;
}

bool playMovie(int i, int frame, int last = 0) {
    if (S->hasMedia && S->media.kind != XG_MEDIA_MOVIE) S->hasMedia = false;
    if (S->hasMedia && frame < S->media.first) return false;
    if (last == 0) last = S->hasMedia && S->media.kind == XG_MEDIA_MOVIE && S->playing == i ? S->clipLast : 0;
    const XgMovieEntry* m = xgScanMovie(S->scan, i);
    if (m == nullptr || frame < 1 || frame > m->frames || (last && (last < frame || last > m->frames))) {
        return false;
    }
    if (S->player == nullptr || S->playerDisc != m->disc) {
        xgMovieDestroy(S->player);
        char why[128] = "";
        S->player = xgMovieCreate(S->discs[m->disc - 1], why, sizeof(why));
        S->playerDisc = m->disc;
        if (S->player == nullptr) {
            error("Movies: %s", why);
            return false;
        }
    }
    int offset = m->offset, first = 1;
    if (frame > 1) {
        int at = xgScanSeek(S->scan, i, frame);
        if (at < 0) return false;
        if (at >= 0) {
            offset = at;
            first = frame;
        }
    }
    xgMovieAudioStop();
    if (!xgMovieStart(S->player, m->stream, offset, first, last ? last : m->frames, m->width, m->height)) {
        error("Movie %s: the player did not start", movieName(i).c_str());
        S->playing = -1;
        return false;
    }
    xgMovieRecord(S->player, 1);
    S->playing = i;
    S->movieSel = i;
    S->frame = 0; S->frameW = S->frameH = 0; S->rgba.clear(); S->shown = -1;
    S->paused = false;
    S->seekTo = first;
    S->clipFirst = S->hasMedia ? S->media.first : 1;
    S->clipLast = last ? last : m->frames;
    return true;
}

// Each frame: the player runs a vertical blank (the extras' frames come at
// the game's vblank rate), more or less to keep the audio queued.
void runMovie() {
    if (S->player == nullptr || S->playing < 0 || S->paused || !xgMoviePlaying(S->player)) {
        return;
    }
    if (S->tab != TAB_MOVIES) {
        /* Another tab shows its own picture: the movie waits. */
        S->paused = true;
        xgMovieAudioStop();
        return;
    }
    int vsyncs = 1;
    double queued = xgMovieAudioQueued();
    if (getenv("XG_EXTRAS_SCRIPT") == nullptr && queued >= 0) {
        /* The audio device's clock keeps the pace (the frames' may differ). */
        vsyncs = queued < 0.05 ? 2 : queued > 0.25 ? 0 : 1;
    }
    xgMovieRun(S->player, vsyncs, 1);
    S->rgba.resize(320 * 240);
    int w, h;
    int f = xgMovieFrame(S->player, S->rgba.data(), &w, &h);
    if (f != 0 && f != S->frame) {
        S->frame = f;
        S->frameW = w;
        S->frameH = h;
        upload(S->rgba.data(), w, h);
        S->shown = 0;
    }
}

void exportFrame() {
    if (S->frame == 0 || S->playing < 0) {
        error("No frame to export");
        return;
    }
    const XgMovieEntry* m = xgScanMovie(S->scan, S->playing);
    char name[128];
    snprintf(name, sizeof(name), "%s/movie_%s_f%04d.png", regionTag(m->disc).c_str(), movieName(S->playing).c_str(),
             S->frame);
    std::string path = exportPath(name);
    if (path.empty() || !xgPngWrite(path.c_str(), S->rgba.data(), S->frameW, S->frameH)) {
        error("Cannot write %s: %s", path.c_str(), xgWriterError());
        return;
    }
    bool linked = S->hasMedia && S->media.kind == XG_MEDIA_MOVIE && S->movieSel == S->playing;
    if (!recordExport(path, "gallery", {exportSource(S->discs[m->disc - 1], 0x19, m->stream + 2, "movie", linked ? S->media.field : -1)},
        {{"stream", std::to_string(m->stream)}, {"movie_start_sector", std::to_string(m->offset)}, {"request_sector", linked ? std::to_string(S->media.offset) : ""}, {"frame", std::to_string(S->frame)},
         {"script_pc", linked ? std::to_string(S->media.pc) : ""}, {"first", std::to_string(S->clipFirst)}, {"last", std::to_string(S->clipLast)}})) return;
    status("Exported %s", path.c_str());
}

void exportAudio() {
    int frames = 0;
    const int16_t* pcm = S->player != nullptr ? xgMovieRecorded(S->player, &frames) : nullptr;
    if (pcm == nullptr || frames == 0 || S->playing < 0) {
        error("Play a movie first");
        return;
    }
    const XgMovieEntry* m = xgScanMovie(S->scan, S->playing);
    char name[128];
    snprintf(name, sizeof(name), "%s/movie_%s.wav", regionTag(m->disc).c_str(), movieName(S->playing).c_str());
    std::string path = exportPath(name);
    if (path.empty() || !xgWavWrite(path.c_str(), pcm, frames, 2, XG_MOVIE_RATE)) {
        error("Cannot write %s: %s", path.c_str(), xgWriterError());
        return;
    }
    bool linked = S->hasMedia && S->media.kind == XG_MEDIA_MOVIE && S->movieSel == S->playing;
    if (!recordExport(path, "gallery", {exportSource(S->discs[m->disc - 1], 0x19, m->stream + 2, "movie", linked ? S->media.field : -1)},
        {{"stream", std::to_string(m->stream)}, {"movie_start_sector", std::to_string(m->offset)}, {"request_sector", linked ? std::to_string(S->media.offset) : ""}, {"frame", std::to_string(S->frame)},
         {"script_pc", linked ? std::to_string(S->media.pc) : ""}, {"first", std::to_string(S->clipFirst)}, {"last", std::to_string(S->clipLast)}})) return;
    status("Exported %s (%.1f s)", path.c_str(), frames / (double)XG_MOVIE_RATE);
}

std::string clock(int frames);

void movieList(ImVec2 min, ImVec2 max) {
    if (beginPane("##gmovies", min, max, nullptr, true)) {
        int disc = 0;
        for (int i = 0; i < xgScanMovieCount(S->scan); i++) {
            const XgMovieEntry* m = xgScanMovie(S->scan, i);
            if (m->disc != disc) {
                disc = m->disc;
                ImGui::SeparatorText(disc == 1 ? tr("Disc 1") : tr("Disc 2"));
            }
            const XgMovieUse* first = nullptr;
            for (int u = 0; u < xgScanUseCount(S->scan) && first == nullptr; u++) {
                if (xgScanUse(S->scan, u)->movie == i) {
                    first = xgScanUse(S->scan, u);
                }
            }
            char label[160];
            char event[64];
            snprintf(event, sizeof(event), tr("battle event %d"), first != nullptr ? first->event : 0);
            std::string where = first == nullptr       ? std::string("")
                                : first->field == -2  ? std::string("  ") + event
                                : first->field < 0     ? std::string("  ") + tr("the opening")
                                                       : "  " + fieldName(first->field);
            snprintf(label, sizeof(label), "%-3s %5s%s", movieName(i).c_str(), clock(m->frames).c_str(), where.c_str());
            ImGui::PushID(i);
            if (listItem(label, S->movieSel == i)) {
                S->hasMedia = false; S->movieSel = i;
                sound(2);
                playMovie(i, 1);
                S->focusWanted = true; // across the area once this frame is drawn
            }
            listFocus(i, S->movieSel);
            ImGui::PopID();
        }
    }
    endPane();
}

std::string clock(int frames) {
    int secs = frames / 15;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", secs / 60, secs % 60);
    return buf;
}

void moviePane(ImVec2 min, ImVec2 max) {
    char title[96];
    snprintf(title, sizeof(title), "%s", tr("Movies"));
    int i = S->movieSel;
    const XgMovieEntry* m = i >= 0 ? xgScanMovie(S->scan, i) : nullptr;
    if (m != nullptr) {
        snprintf(title, sizeof(title), tr("Movie %s, disc %d"), movieName(i).c_str(), m->disc);
    }
    if (beginPane("##gmovie", min, max, title, false)) {
        if (m == nullptr) {
            hint("Choose a movie. It plays through the game's own movie player (its video_player overlay), with its "
                 "XA sound, from the disc it is on.");
            endPane();
            return;
        }
        if (S->hasMedia && S->media.kind == XG_MEDIA_MOVIE) {
            ImGui::TextWrapped(tr("%s disc %d - field %d, script PC %04X, frames %d-%d"), xgDiscRegion(S->discs[m->disc - 1]),
                m->disc, S->media.field, S->media.pc, S->media.first, S->media.last);
            ImGui::TextWrapped("%s", xgDiscPath(S->discs[m->disc - 1]));
        }
        float u = unit();
        bool linked = S->hasMedia && S->media.kind == XG_MEDIA_MOVIE;
        int first = linked ? S->clipFirst : 1, last = linked ? S->clipLast : m->frames;
        bool here = S->playing == i;
        bool running = here && S->player != nullptr && xgMoviePlaying(S->player);
        int frame = here && S->frame > 0 ? S->frame : 1;
        if (ImGui::Button(running && !S->paused ? tr("Pause") : running ? tr("Go on") : tr("Play"))) {
            if (running) {
                S->paused = !S->paused;
                if (S->paused) {
                    xgMovieAudioStop();
                }
            } else {
                playMovie(i, first);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Stop"))) {
            stopMovie();
        }
        ImGui::SameLine();
        if (ImGui::Button("-10s")) {
            playMovie(i, frame - 150 > first ? frame - 150 : first);
        }
        ImGui::SameLine();
        if (ImGui::Button("+10s")) {
            playMovie(i, frame + 150 < last ? frame + 150 : last);
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("PNG"))) {
            exportFrame();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("WAV"))) {
            exportAudio();
        }
        // The seek bar: a drag (or a click) plays from the frame let go at.
        if (ImGui::Button(tr("Bookmark"))) bookmarkSelection();
        ImGui::SetNextItemWidth(-1);
        int f = frame;
        char shown[48];
        snprintf(shown, sizeof(shown), tr("%s / %s   frame %d"), clock(frame).c_str(), clock(m->frames).c_str(), frame);
        ImGui::SliderInt("##seek", &f, first, last, shown, ImGuiSliderFlags_NoInput);
        if (ImGui::IsItemActive()) {
            S->seekTo = f;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            playMovie(i, S->seekTo);
        }
        // The picture, then where the game plays it (scrolls when long).
        int uses = 0;
        for (int k = 0; k < xgScanUseCount(S->scan); k++) {
            uses += xgScanUse(S->scan, k)->movie == i;
        }
        float line = ImGui::GetTextLineHeightWithSpacing();
        float infoH = line * (1.2f + (uses < 1 ? 1 : uses > 3 ? 3 : uses));
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 area(avail.x, fmaxf(avail.y - infoH, 40 * u));
        if (here && S->frame > 0 && S->shown != 0) {
            upload(S->rgba.data(), S->frameW, S->frameH); /* back from another tab */
            S->shown = 0;
        }
        if (here && S->frame > 0 && S->shown == 0) {
            ImVec2 at = ImGui::GetCursorScreenPos();
            drawPicture(S->frameW, S->frameH, area);
            ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + area.y));
        } else {
            ImGui::Dummy(area);
        }
        ImGui::TextColored(skin::textHint, tr("Stream %d (sector %d), %dx%d, %d frames at 15 a second"), m->stream, m->lba,
                           m->width, m->height, m->frames);
        ImGui::BeginChild("##uses", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
        for (int k = 0; k < xgScanUseCount(S->scan); k++) {
            const XgMovieUse* use = xgScanUse(S->scan, k);
            if (use->movie == i) {
                ImGui::TextWrapped("%s", useText(use).c_str());
            }
        }
        if (uses == 0) {
            hint("No verified script reference was found on the supplied discs.");
        }
        ImGui::EndChild();
    }
    endPane();
}

// ---- pictures -----------------------------------------------------------------------------------

XgDisc* pictureDisc() {
    if (S->sourceDisc && S->discs[S->sourceDisc - 1]) return S->discs[S->sourceDisc - 1];
    return S->discs[0] != nullptr ? S->discs[0] : S->discs[1];
}

void buildPictures() {
    S->pics.clear();
    for (int k = 0; k < (int)IM_COUNTOF(picKinds); k++) {
        for (int n = 0; n < picKinds[k].count; n++) {
            S->pics.push_back({k, n, picKinds[k].first + n});
        }
    }
}

bool showPicture(int i) {
    if (i < 0 || i >= (int)S->pics.size()) return false;
    S->hasMedia = false;
    S->picSel = i;
    S->picData.clear();
    S->portraitChecked = false;
    S->tim = XgTim();
    S->clut = 0;
    XgDisc* d = pictureDisc();
    const Pic& p = S->pics[(size_t)i];
    int index = d != nullptr ? xgDiscFileEntry(d, 4, p.file) : -1;
    int size = 0;
    uint8_t* data = index >= 0 ? xgDiscReadEntry(d, index, &size) : nullptr;
    if (data == nullptr) {
        error("Picture %d: cannot read file 04/%03X", p.n, p.file);
        return false;
    }
    S->picData.assign(data, data + size);
    S->picData.resize((size_t)size + 64, 0);
    free(data);
    if (!xgTimParse(S->picData.data(), size, &S->tim)) {
        error("File 04/%03X is not a TIM picture", p.file);
        S->tim = XgTim();
        return false;
    }
    S->shown = -1;
    S->picRgba.clear();
    S->portraitGeneration=0; S->portraitRevision=0; S->portraitSize=0; S->portraitChecked=false;
    return true;
}

void decodePicture() {
    S->picRgba.resize((size_t)S->tim.width * S->tim.imgH);
    auto& rgba = S->picRgba;
    xgTimDecode(&S->tim, S->clut, S->zeroClear, rgba.data());
    S->picW = S->tim.width;
    S->picH = S->tim.imgH;
    upload(rgba.data(), S->picW, S->picH);
    S->shown = 1;
}

bool exportPicture(int i, int clut, ExportManifest* batch = nullptr) {
    if (S->picSel != i) {
        showPicture(i);
    }
    if (S->tim.bpp == 0) {
        return false;
    }
    std::vector<uint32_t> rgba((size_t)S->tim.width * S->tim.imgH);
    xgTimDecode(&S->tim, clut, S->zeroClear, rgba.data());
    const Pic& p = S->pics[(size_t)i];
    static const char* const prefix[] = {"portrait", "picture", "logo"};
    char name[128];
    snprintf(name, sizeof(name), "%s/%s_%02d%s.png", regionTag(xgDiscNumber(pictureDisc())).c_str(), prefix[p.kind],
             p.n, xgTimClutCount(&S->tim) > 1 ? ("_clut" + std::to_string(clut)).c_str() : "");
    std::string path = exportPath(name);
    if (path.empty() || !xgPngWrite(path.c_str(), rgba.data(), S->tim.width, S->tim.imgH)) {
        error("Cannot write %s: %s", path.c_str(), xgWriterError());
        return false;
    }
    int field = S->hasMedia && S->media.kind == XG_MEDIA_PICTURE ? S->media.field : -1;
    if (!recordExport(path, "gallery", {exportSource(pictureDisc(), 4, p.file, "picture", field)},
        {{"picture", std::to_string(p.n)}, {"clut", std::to_string(clut)}, {"zero_clear", S->zeroClear ? "1" : "0"},
         {"script_pc", field >= 0 ? std::to_string(S->media.pc) : ""}}, batch)) return false;
    status("Exported %s", path.c_str());
    return true;
}

// Every picture of the shown one's kind (each CLUT of the multi-CLUT ones).
void exportKind() {
    ExportManifest manifest;
    bool hadMedia = S->hasMedia; XgFieldMedia media = S->media; int clut = S->clut;
    int kind = S->pics[(size_t)S->picSel].kind, keep = S->picSel, done = 0;
    for (int i = 0; i < (int)S->pics.size(); i++) {
        if (S->pics[(size_t)i].kind != kind) {
            continue;
        }
        showPicture(i);
        if (i == keep) { S->hasMedia = hadMedia; S->media = media; }
        int cluts = xgTimClutCount(&S->tim);
        for (int c = 0; c < (cluts > 1 ? cluts : 1); c++) {
            done += exportPicture(i, c, &manifest);
        }
    }
    showPicture(keep); S->hasMedia = hadMedia; S->media = media; S->clut = clut;
    if (!writeManifest(manifest, exportPath((regionTag(xgDiscNumber(pictureDisc())) + "/" + std::string(kind == 0 ? "portraits" : kind == 1 ? "pictures" : "logo") + ".manifest.json").c_str()))) return;
    status("Exported %d pictures to %s", done, exportDir());
}

void pictureList(ImVec2 min, ImVec2 max) {
    if (beginPane("##gpics", min, max, nullptr, true)) {
        int kind = -1;
        for (int i = 0; i < (int)S->pics.size(); i++) {
            const Pic& p = S->pics[(size_t)i];
            if (p.kind != kind) {
                kind = p.kind;
                ImGui::SeparatorText(tr(picKinds[kind].name));
            }
            char label[160];
            int field = p.kind == 1 && S->scanned ? xgScanPictureField(S->scan, p.n) : -1;
            snprintf(label, sizeof(label), "%s %d  (04/%03X)%s%s", kind == 0 ? tr("Portrait") : kind == 1 ? tr("Picture") : tr("Logo"),
                     p.n, p.file, field >= 0 ? "  " : "", field >= 0 ? fieldName(field).c_str() : "");
            ImGui::PushID(i);
            if (listItem(label, S->picSel == i)) {
                sound(2);
                showPicture(i);
                S->focusWanted = true;
            }
            if (S->picSel == i && S->picCursor != i) { ImGui::SetScrollHereY(0.5f); S->picCursor = i; }
            listFocus(i, S->picSel);
            ImGui::PopID();
        }
    }
    endPane();
}

void picturePane(ImVec2 min, ImVec2 max) {
    char title[96];
    snprintf(title, sizeof(title), "%s", tr("Pictures"));
    if (S->picSel >= 0) {
        const Pic& p = S->pics[(size_t)S->picSel];
        snprintf(title, sizeof(title), "%s %d", p.kind == 0 ? tr("Portrait") : p.kind == 1 ? tr("Event picture") : tr("Logo"), p.n);
    }
    if (beginPane("##gpic", min, max, title, false)) {
        if (S->tim.bpp == 0) {
            hint("Choose a picture: the dialogue portraits, the event pictures the field scripts show (and the town "
                 "maps), the logo. Exported to PNG as the game's GPU would draw them.");
            endPane();
            return;
        }
        if (S->hasMedia && S->media.kind == XG_MEDIA_PICTURE) {
            ImGui::TextWrapped(tr("%s disc %d - field %d, script PC %04X - 04/%03X"), xgDiscRegion(pictureDisc()),
                xgDiscNumber(pictureDisc()), S->media.field, S->media.pc, 0x7fb + S->media.picture);
            ImGui::TextWrapped("%s", xgDiscPath(pictureDisc()));
        }
        float u = unit();
        bool portrait=S->picSel>=0 && S->pics[(size_t)S->picSel].kind==0;
        int cluts = xgTimClutCount(&S->tim);
        int clut = S->clut;
        if (cluts > 1) {
            ImGui::SetNextItemWidth(140 * u);
            ImGui::SliderInt(tr("CLUT"), &clut, 0, cluts - 1);
            ImGui::SameLine();
        }
        bool zero = S->zeroClear;
        ImGui::Checkbox(tr(portrait?"Original: texel 0 clear":"Texel 0 clear"), &zero);
        if(!portrait) ImGui::SameLine();
        if (ImGui::Button(tr(portrait?"Export original PNG":"Export PNG"))) {
            exportPicture(S->picSel, S->clut);
        }
        if (ImGui::Button(tr("Export all of these"))) {
            exportKind();
        }
        if(!portrait) ImGui::SameLine();
        if (ImGui::Button(tr("Bookmark"))) bookmarkSelection();
        if (clut != S->clut || zero != S->zeroClear || S->shown != 1) {
            S->clut = clut;
            S->zeroClear = zero;
            decodePicture();
        }
        if(portrait) {
            const char* selected=portraitPackSelected();
            const char* name=selected;
            for(int i=0;i<portraitPackCount();++i) if(!strcmp(portraitPackSet(i)->id,selected)) name=!strcmp(selected,"original")?tr("Original"):portraitPackSet(i)->name;
            ImGui::TextUnformatted(tr("Set")); ImGui::SameLine();
            float reloadWidth=ImGui::CalcTextSize(tr("Reload")).x+ImGui::GetStyle().FramePadding.x*2;
            ImGui::SetNextItemWidth(fmaxf(60*u,ImGui::GetContentRegionAvail().x-reloadWidth-ImGui::GetStyle().ItemSpacing.x));
            if(ImGui::BeginCombo("##portrait_set",name)) {
                for(int i=0;i<portraitPackCount();++i) {
                    const PortraitSet* set=portraitPackSet(i);
                    if(ImGui::Selectable(!strcmp(set->id,"original")?tr("Original"):set->name,!strcmp(set->id,portraitPackSelected()))) { portraitPackSelect(set->id); break; }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine(); if(ImGui::Button(tr("Reload"))) portraitPackReload();
            ImGui::TextUnformatted(tr("View")); ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            const char* views[]={tr("Original"),tr("Upscaled"),tr("Compare")};
            if(ImGui::BeginCombo("##portrait_view",views[S->portraitView])) { // Compare: in Options
                for(int v=0;v<2;++v) if(ImGui::Selectable(views[v],S->portraitView==v)) { S->portraitView=v; portraitLook(); }
                ImGui::EndCombo();
            }
            if(ImGui::Button(tr("Options"))) ImGui::OpenPopup("pictureoptions");
            pictureOptions("pictureoptions");
            updatePortrait();
            if(!S->portraitSize) hint("No replacement in this set; showing the original.");
            hint("Also applies in dialogue.");
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",tr("Add sets in F1, Display, Portrait packs. Dialogue replacements require enhanced resolution or subpixel geometry."));
        }
        ImVec2 avail = ImGui::GetContentRegionAvail();
        avail.y-=ImGui::GetTextLineHeightWithSpacing()*2.5f;
        if(portrait && S->portraitView==2) {
            if(ImGui::BeginTable("##portrait_compare",2,ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn(); ImGui::TextUnformatted(tr("Original"));
                drawPortrait(ImVec2((avail.x-16*u)/2,avail.y-ImGui::GetTextLineHeightWithSpacing()),false);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(tr(S->portraitSize?"Upscaled":"Original (fallback)"));
                drawPortrait(ImVec2((avail.x-16*u)/2,avail.y-ImGui::GetTextLineHeightWithSpacing()),true);
                ImGui::EndTable();
            }
        } else if(portrait) drawPortrait(avail,S->portraitView==1);
        else drawPicture(S->picW,S->picH,avail);
        char cl[64] = "";
        if (cluts > 0) snprintf(cl, sizeof(cl), cluts > 1 ? tr(", %d CLUTs") : tr(", %d CLUT"), cluts);
        ImGui::TextColored(skin::textHint, tr("%d x %d, %d-bit%s"), S->tim.width, S->tim.imgH, S->tim.bpp, cl);
        const Pic& p = S->pics[(size_t)S->picSel];
        int field = p.kind == 1 && S->scanned ? xgScanPictureField(S->scan, p.n) : -1;
        if (field >= 0) {
            ImGui::TextColored(skin::textHint, tr("Shown in %s"), fieldName(field).c_str());
        }
    }
    endPane();
}

// ---- scenes -------------------------------------------------------------------------------------

void selectScene(int i) {
    const XgScene* sc = xgScanScene(S->scan, i);
    if (sc == nullptr) {
        return;
    }
    S->sceneSel = i;
    XgDisc* disc = S->discs[sc->disc - 1];
    if (disc != nullptr && disc != S->namesDisc) {
        EntityNames names(disc);
        for (int id = 0; id < 11; id++) S->partyNames[id] = names.party(id);
        S->namesDisc = disc;
    }
    xgSceneStart(sc, disc != nullptr ? xgDiscRegion(disc) : nullptr, &S->start);
    S->start.disc = S->discs[sc->disc - 1] != nullptr ? sc->disc : 1;
    S->confirm = false;
    S->overrideStart = false;
    status("Scene %d selected", sc->setsTo);
}

const XgScenePreset* selectedScenePreset() {
    const XgScene* sc = xgScanScene(S->scan, S->sceneSel);
    XgDisc* disc = sc != nullptr ? S->discs[sc->disc - 1] : nullptr;
    return xgScenePreset(sc, disc != nullptr ? xgDiscRegion(disc) : nullptr);
}

void startScene() {
    XgDisc* d = S->discs[S->start.disc - 1];
    if (d == nullptr) {
        error("No disc %d image", S->start.disc);
        return;
    }
    if (!xgScanFieldReal(S->scan, S->start.disc, S->start.field)) {
        error("Disc %d does not hold field %d", S->start.disc, S->start.field);
        return;
    }
    const char* problem = xgSceneStartProtection(selectedScenePreset(), &S->start, S->overrideStart);
    if (problem != nullptr) { error("%s", problem); return; }
    if (!portRequestStart(&S->start)) {
        error("Invalid start: check party IDs and field variables");
        return;
    }
    sound(2);
    status("Starting %s at entrance %d, story %d", fieldName(S->start.field, true).c_str(), S->start.entry, S->start.progress);
    leaveIntoGame();
}

void sceneList(ImVec2 min, ImVec2 max) {
    if (beginPane("##gscenes", min, max, nullptr, true)) {
        std::string chapter;
        for (int i = 0; i < xgScanSceneCount(S->scan); i++) {
            const XgScene* sc = xgScanScene(S->scan, i);
            const char* ch = xgStoryName != nullptr ? xgStoryName(sc->setsTo) : nullptr;
            if (ch != nullptr && ch[0] != '\0' && chapter != ch) {
                chapter = ch;
                ImGui::SeparatorText(ch);
            }
            char label[200];
            XgDisc* d = S->discs[sc->disc - 1];
            bool verified = d != nullptr && xgScenePreset(sc, xgDiscRegion(d)) != nullptr;
            snprintf(label, sizeof(label), "%4d%s  %s%s", sc->setsTo, verified ? tr(" [Verified]") : "",
                     fieldName(sc->field).c_str(), sc->use >= 0 ? tr("  (movie)") : "");
            ImGui::PushID(i);
            if (listItem(label, S->sceneSel == i)) {
                sound(2);
                selectScene(i);
                S->focusWanted = true;
            }
            listFocus(i, S->sceneSel);
            ImGui::PopID();
        }
    }
    endPane();
}

void scenePane(ImVec2 min, ImVec2 max) {
    const XgScene* sc = S->sceneSel >= 0 ? xgScanScene(S->scan, S->sceneSel) : nullptr;
    char title[96];
    snprintf(title, sizeof(title), "%s", tr("Scenes"));
    if (sc != nullptr) {
        snprintf(title, sizeof(title), tr("Scene: story %d"), sc->setsTo);
    }
    if (beginPane("##gscene", min, max, title, true)) {
        if (sc == nullptr) {
            hint("Choose a scene to start the game there. Verified presets reached their story transition in US and JP "
                 "replays. Other starts are inferred from scripts and may need party, flags or player interaction.");
            endPane();
            return;
        }
        float u = unit();
        ImGui::TextWrapped("%s", fieldName(sc->field).c_str());
        const char* ch = xgStoryName != nullptr ? xgStoryName(sc->progress) : nullptr;
        if (ch != nullptr && ch[0] != '\0') {
            ImGui::TextWrapped(tr("Story: %s"), ch);
        }
        ImGui::TextWrapped(tr("Its script sets the story progress to %d."), sc->setsTo);
        XgDisc* presetDisc = S->discs[sc->disc - 1];
        const XgScenePreset* preset = xgScenePreset(sc, presetDisc != nullptr ? xgDiscRegion(presetDisc) : nullptr);
        if (xgScenePresetMatches(preset, &S->start)) {
            ImGui::TextUnformatted(tr("Verified preset (US + JP)"));
            hint("%s", preset->instructions);
        } else if (preset != nullptr) {
            ImGui::TextColored(skin::textWarning, tr("Custom start - unverified"));
            if (ImGui::Button(tr("Restore verified preset"))) {
                S->start = preset->start; S->confirm = false; S->overrideStart = false;
                status("Verified scene preset restored");
            }
        } else {
            ImGui::TextColored(skin::textWarning, tr("Inferred start - unverified"));
            hint(sc->slot == 2 ? "This scene is in an actor's interaction routine. Talking to the actor may be required."
                              : "The indexed script may need player interaction or story flags that this start does not supply.");
        }
        if (sc->use >= 0) {
            ImGui::TextWrapped(tr("It plays a movie: %s"), useText(xgScanUse(S->scan, sc->use)).c_str());
        }
        ImGui::Dummy(ImVec2(0, 4 * u));
        ImGui::SeparatorText(tr("Start the game here"));
        if (ImGui::Checkbox(tr("Override unverified start"), &S->overrideStart)) {
            S->confirm = false;
            if (!S->overrideStart) selectScene(S->sceneSel);
        }
        hint(S->overrideStart ? "Custom party and flags may change how this scene plays."
                              : "Verified presets protect the scene's party and starting state. Enable the override to edit.");
        std::string party;
        for (int id : S->start.party) {
            if (!party.empty()) party += ", ";
            party += (id == 255 ? tr("Empty") : id >= 0 && id < 11 ? S->partyNames[id] : tr("Invalid")) +
                     std::string(" (") + std::to_string(id) + ")";
        }
        ImGui::TextWrapped(tr("Party: %s"), party.c_str());
        ImGui::BeginDisabled(!S->overrideStart);
        ImGui::SetNextItemWidth(120 * u);
        ImGui::InputInt(tr("Field"), &S->start.field);
        ImGui::SetNextItemWidth(120 * u);
        ImGui::InputInt(tr("Entrance"), &S->start.entry);
        ImGui::SetNextItemWidth(120 * u);
        ImGui::InputInt(tr("Story progress"), &S->start.progress);
        int maxField = xgScanFieldCount(S->scan, 1) > 0 ? xgScanFieldCount(S->scan, 1) - 1 : 729;
        S->start.field = S->start.field < 0 ? 0 : S->start.field > maxField ? maxField : S->start.field;
        S->start.entry = S->start.entry < 0 ? 0 : S->start.entry > 255 ? 255 : S->start.entry;
        S->start.progress = S->start.progress < 0 ? 0 : S->start.progress > 0x7FFF ? 0x7FFF : S->start.progress;
        if (S->discs[1] != nullptr) {
            ImGui::RadioButton(tr("Disc 1"), &S->start.disc, 1);
            ImGui::SameLine();
            ImGui::RadioButton(tr("Disc 2"), &S->start.disc, 2);
        }
        if (ImGui::CollapsingHeader(tr("Party and story flags"))) {
            ImGui::InputInt3(tr("Party IDs"), S->start.party);
            hint("Character IDs 0-10; 255 is an empty slot. Custom parties require the override.");
            ImGui::SetNextItemWidth(120 * u);
            int oldCount = S->start.variableCount;
            ImGui::SliderInt(tr("Field variables"), &S->start.variableCount, 0, PORT_START_VARIABLES);
            for (int i = oldCount; i < S->start.variableCount; i++) S->start.variables[i] = {2 + i, 0};
            hint("Advanced: variable slot 2-511 and a 16-bit value. Slot 82 is reserved for the disc.");
            for (int i = 0; i < S->start.variableCount; i++) {
                ImGui::PushID(i);
                ImGui::SetNextItemWidth(120 * u);
                ImGui::InputInt(tr("Variable slot"), &S->start.variables[i].index);
                ImGui::SetNextItemWidth(120 * u);
                ImGui::InputInt(tr("Value"), &S->start.variables[i].value);
                ImGui::PopID();
            }
        }
        ImGui::EndDisabled();
        const char* problem = xgSceneStartProtection(preset, &S->start, S->overrideStart);
        bool canStart = problem == nullptr && xgScanFieldReal(S->scan, S->start.disc, S->start.field);
        if (problem != nullptr) {
            hint("%s", problem);
        }
        if (!xgScanFieldReal(S->scan, S->start.disc, S->start.field)) {
            ImGui::TextColored(skin::textError, tr("Disc %d does not hold field %d."), S->start.disc, S->start.field);
        }
        ImGui::Dummy(ImVec2(0, 4 * u));
        ImGui::BeginDisabled(!canStart);
        if (!S->confirm) {
            if (ImGui::Button(xgScenePresetMatches(preset, &S->start) ? tr("Play this scene") : tr("Try this start"))) {
                S->confirm = true;
                sound(2);
            }
        } else {
            ImGui::TextColored(skin::textWarning, fromGame() ? tr("The game restarts there: what is not saved is lost.")
                                                                  : tr("The game starts there."));
            if (ImGui::Button(tr("Start"))) {
                startScene();
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel"))) {
                S->confirm = false;
                sound(3);
            }
        }
        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(0, 4 * u));
        hint("F1, then Extras, comes back to the gallery from the game.");
    }
    endPane();
}

// ---- the viewer ---------------------------------------------------------------------------------

// What the gallery read stays for the next opening (the way back from a
// replayed scene opens it again): the discs and the scan, while the drive's
// discs are the same; and where it was.
struct Kept {
    std::string paths[2];
    XgDisc* discs[2] = {nullptr, nullptr};
    XgGalleryScan* scan = nullptr;
    bool scanned = false;
};
Kept kept;

void opened() {
    S = new State;
    std::string paths[2];
    for (int i = 0; i < 2; i++) {
        const char* p = cdSwapPath(i);
        paths[i] = p != nullptr ? p : "";
    }
    bool same = (paths[0] == kept.paths[0] && paths[1] == kept.paths[1]) ||
                (paths[0] == kept.paths[1] && paths[1] == kept.paths[0]);
    if (!same) {
        xgScanFree(kept.scan);
        for (XgDisc*& d : kept.discs) {
            xgDiscClose(d);
            d = nullptr;
        }
        kept = Kept();
        kept.paths[0] = paths[0];
        kept.paths[1] = paths[1];
        for (int i = 0; i < 2; i++) {
            XgDisc* d = paths[i].empty() ? nullptr : xgDiscOpen(paths[i].c_str(), nullptr);
            int n = d != nullptr ? xgDiscNumber(d) : 0;
            if (n >= 1 && n <= 2 && kept.discs[n - 1] == nullptr) {
                kept.discs[n - 1] = d;
            } else {
                xgDiscClose(d);
            }
        }
        if (kept.discs[0] != nullptr || kept.discs[1] != nullptr) {
            kept.scan = xgScanBegin(kept.discs[0], kept.discs[1]);
        }
    }
    S->discs[0] = kept.discs[0];
    S->discs[1] = kept.discs[1];
    S->scan = kept.scan;
    S->scanned = kept.scanned;
    logEvent("gallery: opened; disc 1 %s; disc 2 %s; scan %s", S->discs[0] != nullptr ? xgDiscPath(S->discs[0]) : "none",
             S->discs[1] != nullptr ? xgDiscPath(S->discs[1]) : "none",
             S->scan == nullptr ? "none" : !same ? "started" : S->scanned ? "kept (done)" : "kept (going on)");
    if (S->discs[0] == nullptr && S->discs[1] == nullptr) {
        error("No Xenogears disc image found");
    }
    buildPictures();
}

void closed() {
    stopMovie();
    xgMovieDestroy(S->player);
    kept.scanned = S->scanned;
    delete S; // the discs and the scan are kept's
    S = nullptr;
}

// ---- the focused view: the movie, picture or scene alone across the area ---------------------------

bool focusable() {
    if (S->tab == TAB_MOVIES) return S->scanned && S->movieSel >= 0 && xgScanMovie(S->scan, S->movieSel) != nullptr;
    if (S->tab == TAB_PICTURES) return S->picSel >= 0 && S->tim.bpp != 0;
    return S->scanned && S->sceneSel >= 0 && xgScanScene(S->scan, S->sceneSel) != nullptr;
}

void setFocused(bool on) {
    if (on == S->focused || (on && !focusable())) return;
    S->focused = on;
    S->zoom = 0;
    S->pan = ImVec2(0, 0);
    if (on) focusEnter();
    else focusLeave();
}

// A picture in `size` (the cursor's place) at the focused view's zoom: fitted
// (whole multiples when it can) until zoomed; the wheel and the triggers zoom,
// a drag pans, a double click fits again.
void zoomedImage(ImTextureRef tex, ImVec2 uv1, float w, float h, ImVec2 size) {
    if (w <= 0 || h <= 0 || size.x < 1 || size.y < 1) return;
    ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##picture", size);
    ImGuiIO& io = ImGui::GetIO();
    float fit = fminf(size.x / w, size.y / h);
    if (fit >= 2) fit = floorf(fit);
    float s = S->zoom > 0 ? S->zoom : fit;
    float factor = 1;
    if (ImGui::IsItemHovered()) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        if (io.MouseWheel != 0) factor = powf(1.2f, io.MouseWheel);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) S->zoom = 0, S->pan = ImVec2(0, 0), s = fit;
    }
    float zt = ImGui::GetKeyData(ImGuiKey_GamepadR2)->AnalogValue - ImGui::GetKeyData(ImGuiKey_GamepadL2)->AnalogValue;
    if (viewerShortcuts() && fabsf(zt) > .2f) factor = 1 + zt * .04f;
    if (factor != 1) S->zoom = s = fminf(64.f, fmaxf(.05f, s * factor));
    if (ImGui::IsItemActive() && S->zoom > 0) S->pan = ImVec2(S->pan.x + io.MouseDelta.x, S->pan.y + io.MouseDelta.y);
    if (S->zoom <= 0) S->pan = ImVec2(0, 0);
    ImVec2 dim(w * s, h * s);
    ImVec2 p0(floorf(at.x + (size.x - dim.x) / 2 + S->pan.x), floorf(at.y + (size.y - dim.y) / 2 + S->pan.y));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(at, ImVec2(at.x + size.x, at.y + size.y), true);
    dl->AddImage(tex, p0, ImVec2(p0.x + dim.x, p0.y + dim.y), ImVec2(0, 0), uv1);
    dl->PopClipRect();
}

// The bar's zoom buttons for a picture (1:1: one of its pixels per window pixel).
void pictureZoom() {
    switch (focusZoomButtons()) {
    case 1: S->zoom = 0; S->pan = ImVec2(0, 0); break;
    case 2: if (S->zoom > 0) S->zoom = fmaxf(.05f, S->zoom * .8f); else S->zoom = -.8f; break;
    case 3: if (S->zoom > 0) S->zoom = fminf(64.f, S->zoom * 1.25f); else S->zoom = -1.25f; break;
    case 4: S->zoom = 1; break;
    default: break;
    }
}

// A zoom step asked while fitted (negative: the factor) becomes absolute here,
// where the fitted scale is known.
void resolveZoom(float w, float h, ImVec2 size) {
    if (S->zoom >= 0 || w <= 0 || h <= 0) return;
    float fit = fminf(size.x / w, size.y / h);
    if (fit >= 2) fit = floorf(fit);
    S->zoom = fit * -S->zoom;
}

void focusedMovie(const FocusLayout& f) {
    int i = S->movieSel;
    const XgMovieEntry* m = xgScanMovie(S->scan, i);
    bool linked = S->hasMedia && S->media.kind == XG_MEDIA_MOVIE;
    int first = linked ? S->clipFirst : 1, last = linked ? S->clipLast : m->frames;
    bool here = S->playing == i;
    bool running = here && S->player != nullptr && xgMoviePlaying(S->player);
    int frame = here && S->frame > 0 ? S->frame : 1;
    if (beginPane("##gmoviefocus", f.min, f.max, nullptr, false)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (here && S->frame > 0) {
            if (S->shown != 0) { // back from another tab
                upload(S->rgba.data(), S->frameW, S->frameH);
                S->shown = 0;
            }
            resolveZoom((float)S->frameW, (float)S->frameH, avail);
            zoomedImage(texture()->GetTexRef(), ImVec2((float)S->frameW / TEX_W, (float)S->frameH / TEX_H),
                        (float)S->frameW, (float)S->frameH, avail);
        } else ImGui::InvisibleButton("##picture", avail);
        char title[96];
        snprintf(title, sizeof(title), tr("Movie %s, disc %d"), movieName(i).c_str(), m->disc);
        focusPicture(title);
    }
    endPane();
    if (beginFocusBar(f)) setFocused(false);
    int n = xgScanMovieCount(S->scan);
    if (focusButton("Previous") && n > 0) { S->hasMedia = false; S->movieSel = (i + n - 1) % n; playMovie(S->movieSel, 1); }
    if (focusButton("Next") && n > 0) { S->hasMedia = false; S->movieSel = (i + 1) % n; playMovie(S->movieSel, 1); }
    if (focusButton(running && !S->paused ? "Pause" : running ? "Go on" : "Play")) {
        if (running) {
            S->paused = !S->paused;
            if (S->paused) xgMovieAudioStop();
        } else playMovie(i, first);
    }
    if (focusButton("Stop")) stopMovie();
    if (focusButton("-10s")) playMovie(i, frame - 150 > first ? frame - 150 : first);
    if (focusButton("+10s")) playMovie(i, frame + 150 < last ? frame + 150 : last);
    focusText("%s / %s", clock(frame).c_str(), clock(m->frames).c_str());
    pictureZoom();
    if (focusButton("PNG")) exportFrame();
    if (focusButton("WAV")) exportAudio();
    focusLook(LOOK_MOVIES, LOOK_NO_PACK, moviePackPresent() ? N_("Movie packs show in the game; here the disc's movie")
                                                            : N_("No movie pack installed"));
    endFocusBar();
}

void focusedPicture(const FocusLayout& f) {
    const Pic& p = S->pics[(size_t)S->picSel];
    bool portrait = p.kind == 0;
    if (S->shown != 1) decodePicture();
    if (portrait) updatePortrait();
    if (beginPane("##gpicfocus", f.min, f.max, nullptr, false)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        char title[96];
        snprintf(title, sizeof(title), "%s %d", portrait ? tr("Portrait") : p.kind == 1 ? tr("Event picture") : tr("Logo"), p.n);
        if (portrait && S->portraitView == 2) {
            // Compare: the original and the set's picture side by side, fitted.
            float half = floorf((avail.x - 16 * unit()) / 2);
            ImVec2 at = ImGui::GetCursorScreenPos();
            drawPortrait(ImVec2(half, avail.y), false);
            ImGui::SetCursorScreenPos(ImVec2(at.x + half + 16 * unit(), at.y));
            drawPortrait(ImVec2(half, avail.y), true);
            ImGui::SetCursorScreenPos(at);
            ImGui::InvisibleButton("##picture", avail);
        } else if (portrait && S->portraitView == 1 && S->portraitSize) {
            resolveZoom(64, 64, avail);
            zoomedImage(portraitTexture()->GetTexRef(), ImVec2(S->portraitSize / 512.f, S->portraitSize / 512.f), 64, 64, avail);
        } else {
            resolveZoom((float)S->picW, (float)S->picH, avail);
            zoomedImage(texture()->GetTexRef(), ImVec2((float)S->picW / TEX_W, (float)S->picH / TEX_H),
                        (float)S->picW, (float)S->picH, avail);
        }
        focusPicture(title);
    }
    endPane();
    if (beginFocusBar(f)) setFocused(false);
    int n = (int)S->pics.size();
    if (focusButton("Previous") && n > 0) showPicture((S->picSel + n - 1) % n);
    if (focusButton("Next") && n > 0) showPicture((S->picSel + 1) % n);
    int cluts = xgTimClutCount(&S->tim);
    if (cluts > 1) {
        char label[48];
        snprintf(label, sizeof(label), tr("CLUT %d/%d"), S->clut + 1, cluts);
        if (focusButton(label)) { S->clut = (S->clut + 1) % cluts; decodePicture(); }
    }
    bool zero = S->zeroClear;
    if (focusCheckbox(portrait ? "Original: texel 0 clear" : "Texel 0 clear", &zero)) { S->zeroClear = zero; decodePicture(); }
    if (portrait) focusLook(LOOK_PORTRAITS, portraitCovered(), N_("No portrait pack installed"));
    else focusLook(LOOK_PORTRAITS, LOOK_NO_PACK, N_("No pack covers these pictures"));
    if (!(portrait && S->portraitView == 2)) pictureZoom();
    if (focusButton(portrait ? "Export original PNG" : "Export PNG")) exportPicture(S->picSel, S->clut);
    if (portrait && portraitCovered() != LOOK_NO_PACK) {
        if (focusButton("Options")) ImGui::OpenPopup("pictureoptions");
        pictureOptions("pictureoptions");
    }
    endFocusBar();
}

void focusedScene(const FocusLayout& f) {
    scenePane(f.min, f.max);
    if (beginFocusBar(f)) setFocused(false);
    int n = xgScanSceneCount(S->scan);
    if (focusButton("Previous") && n > 0) selectScene((S->sceneSel + n - 1) % n);
    if (focusButton("Next") && n > 0) selectScene((S->sceneSel + 1) % n);
    endFocusBar();
}

void focusedView(const Area& a) {
    if (!focusable()) { setFocused(false); return; }
    FocusLayout f = focusLayout(a, 1);
    if (S->tab == TAB_MOVIES) focusedMovie(f);
    else if (S->tab == TAB_PICTURES) focusedPicture(f);
    else focusedScene(f);
}

void draw(const Area& a) {
    float u = a.unit;
    syncLook();
    stepScan(12);
    runMovie();
    if (S->focused) {
        if (S->wantTab >= 0) { // L1/R1 (page) or a script's tab: that tab's chosen asset, else its list
            showTab(S->wantTab);
            S->wantTab = -1;
        }
        focusedView(a);
        return;
    }
    float split = floorf(fminf(260 * u, (a.max.x - a.min.x) * 0.42f) / u) * u;
    ImVec2 listMin = a.min, listMax(a.min.x + split, a.max.y);
    ImVec2 paneMin(a.min.x + split + 16 * u, a.min.y), paneMax = a.max;

    // The tabs above the list, as list entries in a row; L1/R1 change them
    // too (command "page"). Room between them for the list pointer, and never
    // the cursor's first place when the gallery opens: that is the list.
    ImGui::SetCursorScreenPos(ImVec2(listMin.x + 4 * u, listMin.y));
    ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
    for (int t = 0; t < TAB_COUNT; t++) {
        if (t > 0) {
            ImGui::SameLine(0, 14 * u);
        }
        ImGui::PushID(t);
        ImVec2 size(ImGui::CalcTextSize(tr(tabLabels[t])).x + 4 * u, 0);
        if (skin::selectable(tr(tabLabels[t]), S->tab == t, 0, size) || S->wantTab == t) {
            showTab(t);
        }
        ImGui::PopID();
    }
    ImGui::PopItemFlag();
    S->wantTab = -1;
    logShown();
    listMin.y += ImGui::GetTextLineHeightWithSpacing() + 12 * u;

    if (S->tab == TAB_PICTURES) {
        pictureList(listMin, listMax);
        picturePane(paneMin, paneMax);
        if (S->focusWanted) S->focusWanted = false, setFocused(true);
        return;
    }
    if (scanPane(listMin, listMax, S->tab == TAB_MOVIES ? N_("Movies") : N_("Scenes"))) {
        if (beginPane("##gwait", paneMin, paneMax, nullptr, false)) {
            hint("The lists are recovered from the disc when the gallery opens; the pictures are ready now.");
        }
        endPane();
        return;
    }
    if (S->tab == TAB_MOVIES) {
        movieList(listMin, listMax);
        moviePane(paneMin, paneMax);
    } else {
        sceneList(listMin, listMax);
        scenePane(paneMin, paneMax);
    }
    if (S->focusWanted) S->focusWanted = false, setFocused(true);
}

bool back() {
    if (S->confirm) {
        S->confirm = false;
        return true;
    }
    if (S->focused) { // back to the list (a movie plays on beside it)
        setFocused(false);
        return true;
    }
    if (S->player != nullptr && S->playing >= 0 && xgMoviePlaying(S->player)) {
        stopMovie();
        return true;
    }
    return false;
}

void listAll() {
    if (S->player != nullptr) {
        fprintf(stderr, "gallery: playback movie %d frame %d playing %d paused %d\n", S->playing, S->frame,
                xgMoviePlaying(S->player), S->paused);
    }
    for (int i = 0; S->scan != nullptr && i < xgScanMovieCount(S->scan); i++) {
        const XgMovieEntry* m = xgScanMovie(S->scan, i);
        fprintf(stderr, "gallery: movie %d: disc %d stream %d lba %d offset %d sectors %d frames %d %dx%d\n", i, m->disc,
                m->stream, m->lba, m->offset, m->sectors, m->frames, m->width, m->height);
    }
    for (int i = 0; S->scan != nullptr && i < xgScanUseCount(S->scan); i++) {
        const XgMovieUse* u = xgScanUse(S->scan, i);
        fprintf(stderr, "gallery: use %d: disc %d movie %d stream %d offset %d frames %d-%d field %d op %02X mode %d event %d pc %d\n",
                i, u->disc, u->movie, u->stream, u->offset, u->first, u->last, u->field, u->op, u->mode, u->event, u->pc);
    }
    for (int n = 0; S->scan != nullptr && n < 256; n++) {
        if (xgScanPictureField(S->scan, n) >= 0) {
            fprintf(stderr, "gallery: event picture %d: field %d\n", n, xgScanPictureField(S->scan, n));
        }
    }
    for (int i = 0; S->scan != nullptr && i < xgScanSceneCount(S->scan); i++) {
        const XgScene* s = xgScanScene(S->scan, i);
        fprintf(stderr, "gallery: scene %d: field %d sets %d from %d entry %d disc %d use %d actor %d slot %d start %d pc %d\n",
                i, s->field, s->setsTo, s->progress, s->entry, s->disc, s->use, s->actor, s->slot, s->start, s->pc);
        XgDisc* d = S->discs[s->disc - 1];
        const XgScenePreset* p = xgScenePreset(s, d != nullptr ? xgDiscRegion(d) : nullptr);
        if (p != nullptr) fprintf(stderr, "gallery: verified scene %d: field %d from %d entry %d party %d,%d,%d (US+JP)\n",
                                 i, p->start.field, p->start.progress, p->start.entry,
                                 p->start.party[0], p->start.party[1], p->start.party[2]);
    }
}

bool selectImage(const char* path) {
    XgDisc* disc = xgDiscOpen(path, nullptr);
    int n = disc ? xgDiscNumber(disc) : 0;
    if (n < 1 || n > 2) { xgDiscClose(disc); return false; }
    stopMovie(); xgMovieDestroy(S->player); S->player = nullptr; S->playerDisc = 0;
    xgScanFree(kept.scan);
    for (auto* d : kept.discs) xgDiscClose(d);
    kept = Kept(); kept.paths[0] = path; kept.discs[n - 1] = disc;
    for (int i = 0; i < 2; ++i) {
        const char* other = cdSwapPath(i);
        if (!other || !*other || !strcmp(other, path)) continue;
        XgDisc* d = xgDiscOpen(other, nullptr);
        int number = d ? xgDiscNumber(d) : 0;
        if (number >= 1 && number <= 2 && !kept.discs[number - 1] && !strcmp(xgDiscRegion(d), xgDiscRegion(disc))) {
            kept.discs[number - 1] = d; kept.paths[1] = other;
        } else xgDiscClose(d);
    }
    S->sourceDisc = n;
    S->namesDisc = nullptr; S->overrideStart = false; S->confirm = false;
    kept.scan = xgScanBegin(kept.discs[0], kept.discs[1]);
    S->discs[0] = kept.discs[0]; S->discs[1] = kept.discs[1]; S->scan = kept.scan; S->scanned = false;
    S->movieSel = S->playing = S->picSel = S->sceneSel = -1; S->hasMedia = false;
    S->frame = 0; S->shown = -1; S->rgba.clear(); S->picData.clear(); S->picRgba.clear(); S->tim = {};
    S->tab = S->wantTab = TAB_MOVIES; S->confirm = false;
    return S->scan != nullptr;
}

bool selectMedia(int field, int pc) {
    XgDisc* disc = pictureDisc();
    XgFieldMedia* refs = nullptr;
    int count = xgFieldMediaRead(disc, field, &refs);
    XgFieldMedia r = {}; bool found = false;
    for (int k = 0; k < count; ++k) if (refs[k].pc == pc) { r = refs[k]; found = true; break; }
    free(refs);
    if (!found || r.problem) return false;
    if (r.kind == XG_MEDIA_PICTURE) {
        int i = -1;
        for (int k = 0; k < (int)S->pics.size(); ++k) if (S->pics[k].kind == 1 && S->pics[k].n == r.picture) i = k;
        if (i < 0 || !showPicture(i)) return false;
        decodePicture(); S->tab = S->wantTab = TAB_PICTURES;
    } else {
        XgMovieEntry resolved;
        if (!xgFieldMediaMovie(disc, &r, &resolved)) return false;
        while (S->scan && !S->scanned) stepScan(1000);
        int i = -1;
        for (int k = 0; k < xgScanMovieCount(S->scan); ++k) {
            const auto* m = xgScanMovie(S->scan, k);
            if (m->disc == resolved.disc && m->stream == resolved.stream && m->offset == resolved.offset) { i = k; break; }
        }
        if (i < 0) return false;
        S->hasMedia = false;
        if (!playMovie(i, r.first, r.last)) return false;
        S->tab = S->wantTab = TAB_MOVIES;
    }
    S->hasMedia = true; S->media = r;
    if (r.kind == XG_MEDIA_MOVIE) { S->clipFirst = r.first; S->clipLast = r.last; }
    status("Gallery: %s disc %d field %d script PC %04X", xgDiscRegion(disc), xgDiscNumber(disc), field, pc);
    return true;
}

// Decode one exact frame silently for a bookmark, then hold it. No clock or
// audio from the paused game is advanced by the movie's private sandbox.
bool holdFrame(int frame) {
    if (S->movieSel < 0 || (S->hasMedia && (frame < S->clipFirst || frame > S->clipLast))) return false;
    if (!playMovie(S->movieSel, frame)) return false;
    S->rgba.resize(320 * 240);
    for (int tick = 0; tick < 120; ++tick) {
        xgMovieRun(S->player, 1, 0);
        int w = 0, h = 0, f = xgMovieFrame(S->player, S->rgba.data(), &w, &h);
        if (f == frame) {
            S->frame = f; S->frameW = w; S->frameH = h; S->paused = true;
            upload(S->rgba.data(), w, h); S->shown = 0; xgMovieAudioStop(); return true;
        }
        if (f > frame) break;
    }
    stopMovie(); return false;
}

bool bookmark(Bookmark& b) {
    bool movie = S->tab == TAB_MOVIES && S->playing == S->movieSel && S->frame > 0;
    bool picture = S->tab == TAB_PICTURES && S->picSel >= 0 && S->tim.bpp;
    if (!movie && !picture) return false;
    const auto* m = movie ? xgScanMovie(S->scan, S->movieSel) : nullptr;
    XgDisc* d = movie ? S->discs[m->disc - 1] : pictureDisc();
    char pictureLabel[40];
    snprintf(pictureLabel, sizeof(pictureLabel), "Picture 04/%03X", picture ? S->pics[S->picSel].file : 0);
    if (!bookmarkSource(b, d, "gallery", movie ? "Movie " + movieName(S->movieSel) + " frame " + std::to_string(S->frame)
        : std::string(pictureLabel))) return false;
    bool linked = S->hasMedia && S->media.kind == (movie ? XG_MEDIA_MOVIE : XG_MEDIA_PICTURE);
    if (linked) b.target.commands.push_back({"media", std::to_string(S->media.field) + " " + std::to_string(S->media.pc)});
    if (movie) {
        if (!linked) b.target.commands.push_back({"clip", std::to_string(m->stream) + " " + std::to_string(m->offset) + " " + std::to_string(S->frame)});
        b.target.commands.push_back({"hold", std::to_string(S->frame)});
    } else {
        if (!linked) b.target.commands.push_back({"picture", std::to_string(S->picSel)});
        b.target.commands.push_back({"clut", std::to_string(S->clut)});
        b.target.commands.push_back({"zero", S->zeroClear ? "1" : "0"});
    }
    return true;
}

bool thumbnail(std::vector<uint32_t>& pixels) {
    const auto& rgba = S->tab == TAB_MOVIES ? S->rgba : S->picRgba;
    int w = S->tab == TAB_MOVIES ? S->frameW : S->picW, h = S->tab == TAB_MOVIES ? S->frameH : S->picH;
    if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * h) return false;
    pixels.assign(bookmarkWidth * bookmarkHeight, 0xff000000);
    double scale = fmin((double)bookmarkWidth / w, (double)bookmarkHeight / h);
    int dw = (int)(w * scale), dh = (int)(h * scale);
    for (int y = 0; y < dh; ++y) for (int x = 0; x < dw; ++x)
        pixels[(y + (bookmarkHeight - dh) / 2) * bookmarkWidth + x + (bookmarkWidth - dw) / 2] = rgba[(size_t)(y * h / dh) * w + x * w / dw];
    return true;
}
void suspended() { xgMovieAudioStop(); }
void resumed() { S->wantTab = S->tab; S->shown = -1; }

// L1/R1 (visuals.cpp): the previous or next tab, wrapping, the cursor to its list.
bool page(int delta) {
    S->confirm = false;
    showTab((S->tab + delta + TAB_COUNT) % TAB_COUNT);
    return true;
}

bool command(const char* cmd, const char* arg) {
    if (!strcmp(cmd, "image")) return selectImage(arg);
    if (!strcmp(cmd, "focus")) {
        bool on = strcmp(arg, "0") != 0;
        setFocused(on);
        return S->focused == on;
    }
    if (!strcmp(cmd, "media")) {
        int field, pc; char tail;
        return sscanf(arg, "%d %d %c", &field, &pc, &tail) == 2 && field >= 0 && field <= 729 && pc >= 0 && selectMedia(field, pc);
    }
    if (!strcmp(cmd, "clip")) {
        int stream, offset, frame; char tail;
        if (sscanf(arg, "%d %d %d %c", &stream, &offset, &frame, &tail) != 3 || stream < 0 || offset < 0 || frame < 1) return false;
        while (S->scan && !S->scanned) stepScan(1000);
        for (int i = 0; S->scan && i < xgScanMovieCount(S->scan); ++i) {
            const auto* m = xgScanMovie(S->scan, i);
            if ((!S->sourceDisc || m->disc == S->sourceDisc) && m->stream == stream && m->offset == offset) {
                S->hasMedia = false; S->tab = S->wantTab = TAB_MOVIES; return playMovie(i, frame);
            }
        }
        return false;
    }
    if (!strcmp(cmd, "hold")) return holdFrame(atoi(arg));
    if (!strcmp(cmd,"portraitset")) {
        if(!portraitPackSelect(arg)) return false;
        logEvent("gallery: portrait set %s",portraitPackSelected()); return true;
    }
    if (!strcmp(cmd,"portraitview")) {
        const char* modes[]={"original","upscaled","compare"};
        for(int i=0;i<3;++i) if(!strcmp(arg,modes[i])) { S->portraitView=i; portraitLook(); logEvent("gallery: portrait view %s",arg); return true; }
        return false;
    }
    if (!strcmp(cmd, "clut")) {
        int v = atoi(arg); if (!S->tim.bpp || v < 0 || v >= (xgTimClutCount(&S->tim) > 0 ? xgTimClutCount(&S->tim) : 1)) return false;
        S->clut = v; decodePicture(); return true;
    }
    if (!strcmp(cmd, "zero")) {
        if (strcmp(arg, "0") && strcmp(arg, "1")) return false;
        if (!S->tim.bpp) return false;
        S->zeroClear = !strcmp(arg, "1"); decodePicture(); return true;
    }
    if (!strcmp(cmd, "page")) {
        int delta = atoi(arg);
        return (delta == 1 || delta == -1) && page(delta);
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
    if (strcmp(cmd, "wait") == 0) {
        while (S->scan != nullptr && !S->scanned) {
            stepScan(1000);
        }
        return true;
    }
    if (strcmp(cmd, "info") == 0) {
        const auto* movie = S->tab == TAB_MOVIES ? xgScanMovie(S->scan, S->movieSel) : nullptr;
        XgDisc* d = movie ? S->discs[movie->disc - 1] : pictureDisc();
        fprintf(stderr, "gallery: selection %s disc %d image %s movie %d frame %d picture %d field %d pc %d\n",
            d ? xgDiscRegion(d) : "?", d ? xgDiscNumber(d) : 0, d ? xgDiscPath(d) : "", S->movieSel, S->frame, S->picSel,
            S->hasMedia ? S->media.field : -1, S->hasMedia ? S->media.pc : -1);
        fprintf(stderr, "gallery: tab %s paused %d\n", tabNames[S->tab], S->paused);
        if (S->sceneSel >= 0) fprintf(stderr, "gallery: start scene %d field %d entrance %d story %d party %d,%d,%d verified %d override %d\n",
            S->sceneSel, S->start.field, S->start.entry, S->start.progress,
            S->start.party[0], S->start.party[1], S->start.party[2],
            xgScenePresetMatches(selectedScenePreset(), &S->start), S->overrideStart);
        return true;
    }
    if (strcmp(cmd, "list") == 0) {
        listAll();
        return true;
    }
    if (strcmp(cmd, "movie") == 0 && S->scanned) {
        S->hasMedia = false; return playMovie(atoi(arg), 1);
    }
    if (!strcmp(cmd, "toggle")) {
        if (S->tab == TAB_MOVIES && S->movieSel >= 0) {
            if (S->player && xgMoviePlaying(S->player)) { S->paused = !S->paused; if (S->paused) xgMovieAudioStop(); }
            else playMovie(S->movieSel, S->hasMedia ? S->clipFirst : 1);
        }
        return true;
    }
    if (!strcmp(cmd, "step")) {
        if (S->tab == TAB_MOVIES && S->movieSel >= 0) holdFrame(S->frame + 1);
        return true;
    }
    if (strcmp(cmd, "pause") == 0) {
        S->paused = !S->paused;
        if (S->paused) xgMovieAudioStop();
        return true;
    }
    if (strcmp(cmd, "stop") == 0) {
        stopMovie();
        return true;
    }
    if (strcmp(cmd, "seek") == 0 && S->movieSel >= 0) {
        return playMovie(S->movieSel, atoi(arg));
    }
    if (strcmp(cmd, "frame") == 0) {
        exportFrame();
        return true;
    }
    if (strcmp(cmd, "wav") == 0) {
        exportAudio();
        return true;
    }
    if (strcmp(cmd, "picture") == 0) {
        int n = atoi(arg);
        if (n < 0 || n >= (int)S->pics.size()) {
            return false;
        }
        bool ok = showPicture(n); if (ok) { S->tab = S->wantTab = TAB_PICTURES; decodePicture(); } return ok;
    }
    if (strcmp(cmd, "png") == 0 && S->picSel >= 0) {
        return exportPicture(S->picSel, S->clut);
    }
    if (strcmp(cmd, "pngall") == 0 && S->picSel >= 0) {
        exportKind();
        return true;
    }
    if (strcmp(cmd, "scene") == 0 && S->scanned) {
        selectScene(atoi(arg));
        return true;
    }
    if (!strcmp(cmd, "override") && S->sceneSel >= 0) {
        if (strcmp(arg, "0") && strcmp(arg, "1")) return false;
        S->overrideStart = !strcmp(arg, "1"); S->confirm = false;
        if (!S->overrideStart) selectScene(S->sceneSel);
        return true;
    }
    if (strcmp(cmd, "party") == 0 && S->sceneSel >= 0) {
        if (!S->overrideStart) { error("Enable the scene override before changing its party."); return false; }
        int a, b, c;
        if (sscanf(arg, "%d %d %d", &a, &b, &c) != 3) return false;
        S->start.party[0] = a; S->start.party[1] = b; S->start.party[2] = c;
        return true;
    }
    if (strcmp(cmd, "variable") == 0 && S->sceneSel >= 0) {
        if (!S->overrideStart) { error("Enable the scene override before changing its flags."); return false; }
        int index, value;
        if (sscanf(arg, "%d %d", &index, &value) != 2 || S->start.variableCount >= PORT_START_VARIABLES) return false;
        S->start.variables[S->start.variableCount++] = {index, value};
        return true;
    }
    if (strcmp(cmd, "start") == 0 && S->sceneSel >= 0) {
        startScene();
        return true;
    }
    return false;
}

const Viewer viewer = {"gallery", "Gallery", "Movies, portraits, event pictures and cutscenes", 60,
                       opened, closed, draw, back, command, suspended, resumed, bookmark, nullptr, thumbnail};

} // namespace

XG_EXTRAS_VIEWER(viewer);
