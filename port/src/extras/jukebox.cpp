// The jukebox (xg-htn.7): the game's music and sound effects, played by the
// game's own sound driver on our SPU (jukebox.h), from the player's disc.
//
// Music: every score (SMDS) of the music directory (1C), named by the name
// each score carries, once each (a score at several music IDs is listed at
// the first, with the others). The "Movie dammy music" scores (the game's
// own name for the movies' silent stand-ins) are left out: movie music is XA
// audio inside the movie streams (the gallery plays it). For each: its sample
// banks (its default bank and the ones its tracks switch to), its reverb, and
// its loop as the driver plays it (op 0x91 marks the loop start, op 0x90 goes
// back to it), timed by running the driver alone.
//
// Sound effects: every effect bank (SEDS) on the discs, once each (files and
// offset bundles' members: the battle effects' files carry their own), and
// its effects, played as the menus play theirs, with the names the tables
// recover where the game's code plays a fixed one (tables.h xgSfxName).
//
// Movie music: the movie streams' XA audio (directory 0x19, file 1 channel
// 1), decoded by the port's XA decoder into the engine's SPU CD input as the
// drive sends it, mixed at the CD volume the driver set. A stream may hold
// several of the game's movies (the gallery plays them with their pictures).
//
// Sample banks: the driver starts with the kernel's resident banks (01/02-05)
// and the field's effect bank (04/A8), as in a field. A score's or effect
// bank's other banks are found by number among the discs' WDS files (first
// the one just before it, then its own directory, then 1C, then any) and
// loaded where the game loads them; a resident bank they overlap is unloaded,
// as the field does for the large music banks.
//
// Controls: play and stop; play once, next track, repeat track or repeat
// list (a looping score fades after its first loop except in repeat-track
// mode); optional music throughout Extras/F1; volume and track mute/solo.
// Exports (WAV, to the export folder): a score with a set number of loops and
// a fade (the driver's own fade), or played through once; an effect until it
// ends; all scores, or all effects of a bank, in a row.
//
// MIDI export is left out: the driver's notes are sample-bank presets
// (custom ADPCM samples, each with its own ADSR) shaped every 1/240 s by
// pitch slides, portamento, three LFOs and volume ramps; a MIDI file would be
// a loose transcription, not the music. The WAV is the faithful export.
//
// Script commands (XG_EXTRAS_SCRIPT "do ..."):
//   image <path>         use scores and banks from this exact image alone
//   play <what>          play a score: its name, #<music ID>, or @<n> (the n-th listed)
//   effect <bank> <n>   play effect n of effect bank <bank> (its index here)
//   stop                 stop
//   loop on|off          loop
//   playback once|next|repeat|all   completion policy, saved with settings
//   menu-music on|off    keep the player while browsing Extras and F1
//   list-kind music|effects|movies  displayed library
//   next                 play the next listed track, wrapping at the end
//   mute <mask>          channels muted (hex, bit n = track n)
//   solo <track>         that track alone (-1: none)
//   export [loops] [fade] the playing score to WAV now (blocking)
//   effectwav <bank> <n> an effect to WAV now (blocking)
//   movie <n>            play the n-th movie stream's audio
//   moviewav <n>         its audio to WAV now (blocking)
//   render <secs> <file> the playing score from where it is, secs of it,
//                        to a WAV file (blocking; for A/B tests)
//   measure              time the selected score now (blocking), log it
//   list                 the scores and effect banks on stderr
//   info                 what plays, on stderr
//   titles en|ja         select and save the OST title language

#include <SDL3/SDL.h>
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
#include "ui/l10n.h"
#include "ui/skin.h"
#include "viewer.h"
#include "bookmark_source.h"
#include "export_provenance.h"
#include "jukebox_titles.h"

extern "C" {
#include "cd/cd.h"
#include "disc.h"
#include "files.h"
#include "jukebox.h"
#include "system/settings.h"
#include "tables.h"
#include "writers.h"
}

namespace {

using namespace extras;

#define MUSIC_DIR 0x1C
#define FIRST_SCORE 0x14   // music ID n is the score at 1C/14+2n
#define QUEUE_AHEAD 0.15   // seconds of sound queued ahead of the device
#define FADE_ONCE 4.0      // once/next/list: fade after the first native loop
#define MAX_EXPORT (20 * 60) // seconds: a score's export never runs longer
#define MAX_EFFECT 30      // seconds: an effect's export at most
#define BUDGET_NS 12000000 // a frame's share for timing and exports

// A place on the discs: disc (index into S->discs), directory, file, and
// the member of an offset bundle (-1: the whole file).
struct Place {
    int disc = -1, dir = 0, file = 0, member = -1;
};

struct Wds {
    int bank;
    Place at;
    uint32_t addr, bytes;
};

struct Song {
    std::string name;
    Place at;
    std::vector<int> ids; // its music IDs
    int size = 0;
    uint64_t hash = 0;
    int tracks = 0, reverbMode = 0, reverbDepth = 0;
    std::vector<int> banks;
    // The field's music selector (tables.h xgMusicGet) for its first music
    // ID: the WDS file the field loads with it (member -1: none or unknown)
    // and whether it unloads the second resident bank for it.
    Place selector;
    int selectorFlags = -1;
    // Timing, by the driver alone: 0 not yet, 1 being timed, 2 known.
    int timed = 0;
    bool loops = false;
    double loopStart = 0, loopLength = 0, length = 0;
};

struct EffectBank {
    Place at;
    std::vector<Place> also;  // the same bank elsewhere on this disc
    std::string label;        // what the file map says is there
    bool named = false;       // its effects have the tables' names (xgSfxName)
    int id = 0, count = 0, size = 0;
    uint64_t hash = 0;
    std::vector<int> banks;
};

// A movie stream (directory 0x19, the game's movie directory 0x18/1): its
// XA audio, file 1 channel 1, one sector in eight.
struct Movie {
    int disc = 0;   // index into S->discs
    int file = 0;   // 2 + the game's movie number
    int lba = 0, sectors = 0;
};

// Feeding a movie's audio to an engine's CD input as it plays.
struct MovieFeed {
    int movie = -1;
    int sector = 0;    // the next sector of the stream to read
    int64_t frames = 0; // output frames rendered since it started
    bool done() const { return movie < 0; }
};

// A WAV export in the making, a frame's budget at a time.
struct Export {
    bool on = false;
    XgJbEngine* engine = nullptr;
    int song = -1;           // a score, or
    int bank = -1, effect = -1; // an effect, or
    int movie = -1;          // a movie's audio
    MovieFeed feed;
    int phase = 0;           // 0 the loops, 1 the fade, 2 the tail
    int loops = 2;
    double fade = 10;
    std::vector<int16_t> pcm;
    int64_t phaseEnd = 0;    // frames
    int64_t tailFrom = 0;    // frames: where the tail began
    int silent = 0;          // frames of silence at the end so far
    std::string path;
    std::vector<int> queue;  // more to export after this one (songs, or effects of `bank`)
    int done = 0, total = 0;
    ExportManifest manifest;
    std::string manifestPath;
    std::vector<ExportSource> sources;
};

struct State {
    bool captureExportSources = false;
    std::vector<XgDisc*> discs;
    std::vector<Wds> wds;
    std::vector<Song> songs;
    int placeholders = 0;
    std::vector<EffectBank> effectBanks;
    std::vector<Movie> movies;
    std::string region = "xg";

    XgJbEngine* player = nullptr;
    XgJbEngine* meter = nullptr;
    int metering = -1; // the song being timed
    XgJbMarks marks = {};
    double meterTime = 0;

    SDL_AudioStream* stream = nullptr;
    bool streamTried = false;
    bool active = false; // the player renders (something plays, or a tail)
    int tailFrames = 0, silentFrames = 0; // rendered samples after the sequencer ends
    int song = -1;       // the score playing (or last chosen)
    MovieFeed movie;     // a movie's audio playing
    int bank = -1, effect = -1; // the effect bank open, the effect last played
    // Once, next until the list ends, repeat this score, repeat the list.
    int playback = 2;
    bool keepMenu = false, musicRun = false, fadingOnce = false;
    int volume = 80;
    uint32_t mute = 0, solo = 0;
    XgJbStatus st = {};
    std::vector<int16_t> pcm;

    int select = -1;     // the song shown (list cursor's choice)
    bool showEffects = false; // the list shows the open bank's effects
    int listKind = 0; // music, effects, movies
    int showMovie = -1;       // the detail pane shows this movie
    int scrollTo = -1;
    int effectBanksFor = -1;      // the effect effectBankText is of
    std::string effectBankText;    bool focusList = false; // the list changed: the cursor goes to scrollTo
    // The panes: ImGui's cursor does not cross from one game window to the
    // other, so Right in the list goes to the detail pane's first control
    // and Left with nowhere to go there (the shell's back) returns.
    int navIn = 0;          // 1 the list, 2 the detail pane
    // The focused view (viewer.h): the score or movie chosen, its details and
    // controls across the area.
    bool focused = false, focusWanted = false;
    ImGuiID rightFrom = 0;  // Right pressed on this list item
    bool focusDetail = false;
    Export job;
    int exportLoops = 2;
    float exportFade = 10;
};

State* S;

void loadPlayback() {
    S->playback = std::max(0, std::min(3, settingsInt("jukebox_playback", 2)));
    S->keepMenu = settingsInt("jukebox_menu_music", 0) != 0;
}

void savePlayback() {
    settingsSetInt("jukebox_playback", S->playback);
    settingsSetInt("jukebox_menu_music", S->keepMenu);
    settingsSave();
}

bool japaneseTitles() {
    const char* language = settingsGet("jukebox_titles");
    return language != nullptr && strcmp(language, "ja") == 0;
}

bool setTitleLanguage(const char* language) {
    if (strcmp(language, "en") != 0 && strcmp(language, "ja") != 0) return false;
    settingsDescribe("jukebox_titles", "jukebox_titles: OST title language (en or ja); internal names stay visible");
    settingsSet("jukebox_titles", language);
    settingsSave();
    return true;
}

const char* songTitle(const Song& song) {
    const JukeboxTitle* title = jukeboxTitle(song.hash, song.size);
    return title ? (japaneseTitles() ? title->japanese : title->english) : song.name.c_str();
}

uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint64_t fnv(const uint8_t* p, int n) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < n; i++) {
        h = (h ^ p[i]) * 1099511628211ull;
    }
    return h;
}

// A wrapped line in a colour.
void line(const ImVec4& colour, const char* fmt, ...) IM_FMTARGS(2);
void line(const ImVec4& colour, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextWrappedV(tr(fmt), ap); // in the menu's language (ui/l10n.h)
    ImGui::PopStyleColor();
    va_end(ap);
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

std::string clock(double s, bool tenths = true) {
    char b[32];
    if (s < 0) {
        s = 0;
    }
    int m = (int)(s / 60);
    if (tenths) {
        snprintf(b, sizeof(b), "%d:%04.1f", m, s - m * 60);
    } else {
        snprintf(b, sizeof(b), "%d:%02d", m, (int)(s - m * 60));
    }
    return b;
}

// "1C/014", a bundle's member "0E/03C:3"; on the second disc image "2:..."
std::string placeName(const Place& p) {
    char b[48], d[16] = "";
    if (p.disc > 0) {
        snprintf(d, sizeof(d), "%d:", p.disc + 1);
    }
    if (p.member >= 0) {
        snprintf(b, sizeof(b), "%s%02X/%03X:%d", d, p.dir, p.file, p.member);
    } else {
        snprintf(b, sizeof(b), "%s%02X/%03X", d, p.dir, p.file);
    }
    return b;
}

const char* reverbName(int mode) {
    static const char* const names[10] = {N_("off"),  N_("room"), N_("studio small"), N_("studio medium"),
                                          N_("studio large"), N_("hall"), N_("space echo"), N_("echo"),
                                          N_("delay"), N_("pipe")};
    return mode >= 0 && mode < 10 ? tr(names[mode]) : "?";
}

// ---- the discs ---------------------------------------------------------------------------------------

std::vector<uint8_t> readFile(const Place& p) {
    std::vector<uint8_t> out;
    if (p.disc < 0 || p.disc >= (int)S->discs.size()) {
        return out;
    }
    XgDisc* d = S->discs[(size_t)p.disc];
    int index = xgDiscFileEntry(d, p.dir, p.file);
    if (index < 0) {
        return out;
    }
    int size = 0;
    uint8_t* data = xgDiscReadEntry(d, index, &size);
    if (data == nullptr) {
        return out;
    }
    int from = 0, to = size;
    if (p.member >= 0) {
        // A bundle's member: from its offset to the next one's (or the end).
        int count = size >= 4 ? (int)rd32(data) : 0;
        if (p.member >= count || 4 + 4 * count > size) {
            free(data);
            return out;
        }
        from = (int)rd32(data + 4 + 4 * p.member);
        to = p.member + 1 < count ? (int)rd32(data + 8 + 4 * p.member) : size;
        if (from < 0 || from > to || to > size) {
            free(data);
            return out;
        }
    }
    out.assign(data + from, data + to);
    if (S->captureExportSources && !out.empty()) S->job.sources.push_back(exportSource(d, p.dir, p.file, "export_read", -1, -1,
        p.member >= 0 ? std::vector<int>{p.member} : std::vector<int>{}));
    free(data);
    return out;
}

// A sample bank or an effect bank found at `at` (its first bytes in head).
void found(const uint8_t* head, int headSize, const Place& at) {
    if (headSize >= 0x30 && memcmp(head, "wds ", 4) == 0) {
        S->wds.push_back({rd16(head + 0x20), at, rd32(head + 0x28), rd32(head + 0x14)});
    } else if (headSize >= 0x20 && memcmp(head, "seds", 4) == 0) {
        std::vector<uint8_t> all = readFile(at);
        if (all.size() < 0x20) {
            return;
        }
        uint64_t h = fnv(all.data(), (int)all.size());
        for (EffectBank& b : S->effectBanks) {
            if (b.hash == h && b.size == (int)all.size()) {
                if (at.disc == b.at.disc) {
                    b.also.push_back(at); // the other disc's copies are not news
                }
                return;
            }
        }
        EffectBank b;
        b.at = at;
        char where[96] = "";
        xgKnownLocation(xgDiscNumber(S->discs[(size_t)at.disc]), at.dir, at.file, where, sizeof(where));
        b.label = where;
        b.id = rd16(all.data() + 0x14);
        b.count = rd16(all.data() + 0x12);
        b.size = (int)all.size();
        b.hash = h;
        int banks[64];
        int n = xgJbScoreBanks(all.data(), b.size, banks, 64);
        b.banks.assign(banks, banks + n);
        S->effectBanks.push_back(b);
    }
}

// An offset bundle's members (doPointerRelocation's table: u32 count, u32
// offsets): the sample and effect banks among them.
void scanBundle(XgDisc* d, const XgEntry& e, const uint8_t* head, const Place& file) {
    uint32_t count = rd32(head);
    if (count == 0 || count > 64 || 4 + 4 * count > 2048) {
        return;
    }
    uint32_t prev = 4 + 4 * count;
    for (uint32_t m = 0; m < count; m++) {
        uint32_t off = rd32(head + 4 + 4 * m);
        if (off < prev || off >= (uint32_t)e.size) {
            return; // not a bundle
        }
        prev = off;
    }
    for (uint32_t m = 0; m < count; m++) {
        uint32_t off = rd32(head + 4 + 4 * m);
        uint8_t* sec = xgDiscReadSectors(d, e.lba + (int)(off / 2048), 2);
        if (sec != nullptr) {
            Place at = file;
            at.member = (int)m;
            found(sec + off % 2048, 4096 - (int)(off % 2048), at);
            free(sec);
        }
    }
}

// A score of the music directory, music ID `id`: once each (the other
// disc's copy and the other IDs of the same score are noted).
void addSong(const Place& at, int id, std::vector<uint64_t>& songHashes) {
    std::vector<uint8_t> all = readFile(at);
    if (all.size() < 0x24) {
        return;
    }
    uint64_t h = fnv(all.data(), (int)all.size());
    for (Song& s : S->songs) {
        if (s.hash == h && s.size == (int)all.size()) {
            if (std::find(s.ids.begin(), s.ids.end(), id) == s.ids.end()) {
                s.ids.push_back(id);
            }
            return;
        }
    }
    if (std::find(songHashes.begin(), songHashes.end(), h) != songHashes.end()) {
        return; // a placeholder seen before, or the other disc's copy
    }
    songHashes.push_back(h);
    int nameAt = rd16(all.data() + 0x1E);
    std::string name;
    for (int i = nameAt; i > 0 && i < (int)all.size() && all[(size_t)i] >= 0x20 && all[(size_t)i] < 0x7F; i++) {
        name += (char)all[(size_t)i];
    }
    if (name.rfind("Movie dammy", 0) == 0) {
        S->placeholders++;
        return;
    }
    Song s;
    s.name = name.empty() ? "(no name)" : name;
    s.at = at;
    s.ids.push_back(id);
    s.size = (int)all.size();
    s.hash = h;
    s.tracks = all[0x14];
    s.reverbMode = all[0x1A];
    s.reverbDepth = all[0x1B];
    int banks[64];
    int n = xgJbScoreBanks(all.data(), s.size, banks, 64);
    s.banks.assign(banks, banks + n);
    S->songs.push_back(s);
}

// Every WDS and SEDS on the discs (a sector each), with those inside offset
// bundles, and the scores of the music directory.
void scan() {
    std::vector<uint64_t> songHashes;
    for (int di = 0; di < (int)S->discs.size(); di++) {
        XgDisc* d = S->discs[(size_t)di];
        for (int dir = 0; dir < XG_DISC_DIRS; dir++) {
            int files = xgDiscDirFiles(d, dir);
            for (int f = 1; f <= files; f++) {
                XgEntry e = xgDiscEntry(d, xgDiscFileEntry(d, dir, f));
                if (e.size < 0x30 || e.size > (8 << 20)) {
                    continue;
                }
                uint8_t* head = xgDiscReadSectors(d, e.lba, 1);
                if (head == nullptr) {
                    continue;
                }
                Place at{di, dir, f, -1};
                if (memcmp(head, "wds ", 4) == 0 || memcmp(head, "seds", 4) == 0) {
                    found(head, 2048, at);
                } else if (memcmp(head, "smds", 4) == 0 && dir == MUSIC_DIR && f >= FIRST_SCORE &&
                           (f - FIRST_SCORE) % 2 == 0) {
                    addSong(at, (f - FIRST_SCORE) / 2, songHashes);
                } else if (memcmp(head, "smds", 4) != 0) {
                    scanBundle(d, e, head, at);
                }
                free(head);
            }
        }
    }
}

#define MOVIE_DIR 0x19        /* the game's directory 0x18/1 */
#define MODE2_SECTOR 2336     /* a stream's size counts whole Mode 2 sectors */

// The movie streams of each disc.
void scanMovies() {
    for (int di = 0; di < (int)S->discs.size(); di++) {
        XgDisc* d = S->discs[(size_t)di];
        int files = xgDiscDirFiles(d, MOVIE_DIR);
        for (int f = 2; f <= files; f++) {
            XgEntry e = xgDiscEntry(d, xgDiscFileEntry(d, MOVIE_DIR, f));
            if (e.size <= 0 || e.size % MODE2_SECTOR != 0) {
                continue;
            }
            Movie m;
            m.disc = di;
            m.file = f;
            m.lba = e.lba;
            m.sectors = e.size / MODE2_SECTOR;
            S->movies.push_back(m);
        }
    }
}

double movieSeconds(const Movie& m) {
    return m.sectors / 8.0 * 2016 / 37800; // an XA sector in eight, 2016 frames at 37.8 kHz each
}

// Read on in a movie's stream and give its audio sectors (file 1, channel
// 1) to the engine's CD input until about `ahead` input frames wait there.
// Ends the feed (movie -1) when the stream is read and its audio played.
void feedMovie(XgJbEngine* e, MovieFeed& f, int ahead) {
    if (f.movie < 0) {
        return;
    }
    const Movie& m = S->movies[(size_t)f.movie];
    XgDisc* d = S->discs[(size_t)m.disc];
    int queued = xgJbCdQueued(e);
    std::vector<uint8_t> batch;
    uint8_t raw[2352];
    while (queued < ahead && f.sector < m.sectors) {
        if (!xgDiscReadRaw(d, m.lba + f.sector, raw)) {
            f.sector = m.sectors;
            break;
        }
        f.sector++;
        // XA subheader: file, channel, submode (bit 2 audio), coding.
        if ((raw[18] & 0x04) && raw[16] == 1 && raw[17] == 1) {
            batch.insert(batch.end(), raw, raw + sizeof(raw));
            queued += (raw[19] & 1) ? 2016 : 4032; // stereo or mono frames at 37.8 kHz
        }
    }
    if (!batch.empty()) {
        xgJbXa(e, batch.data(), (int)(batch.size() / 2352));
    }
    if (f.sector >= m.sectors && xgJbCdQueued(e) == 0) {
        f.movie = -1;
    }
}

// The WDS file for a bank, from a score or effect bank at `near`: a member
// of its own bundle (the game loads them together), then the resident banks
// (01/02-05), then the one just before it, then its own directory (the
// nearest before it), then the music directory, then any.
const Wds* findBank(int bank, const Place& near) {
    const Wds* best = nullptr;
    int bestRank = 1 << 30;
    for (const Wds& w : S->wds) {
        if (w.bank != bank) {
            continue;
        }
        int rank;
        if (w.at.disc == near.disc && w.at.dir == near.dir && w.at.file == near.file) {
            rank = 0; // a bundle's own member
        } else if (w.at.disc == 0 && w.at.dir == 1 && w.at.file >= 2 && w.at.file <= 5 && w.at.member < 0) {
            rank = 1; // resident
        } else if (w.at.disc == near.disc && w.at.dir == near.dir) {
            int gap = near.file - w.at.file;
            rank = gap > 0 ? 1 + gap : 1000 - gap;
        } else if (w.at.dir == MUSIC_DIR) {
            rank = 100000 + w.at.file;
        } else {
            rank = 200000 + w.at.dir * 1000 + w.at.file;
        }
        rank += w.at.disc * 1000000;
        if (rank < bestRank) {
            bestRank = rank;
            best = &w;
        }
    }
    return best;
}

// The kernel's resident banks and the field's effect bank: what the driver
// has loaded in a field before its music starts.
bool loadBase(XgJbEngine* e) {
    if (S->discs.empty()) {
        return false;
    }
    bool ok = true;
    for (int f = 2; f <= 5; f++) {
        std::vector<uint8_t> w = readFile(Place{0, 1, f});
        ok &= !w.empty() && xgJbLoadBank(e, w.data(), (int)w.size());
    }
    return ok;
}

bool baseEffects(XgJbEngine* e) {
    std::vector<uint8_t> s = readFile(Place{0, 4, 0xA8});
    return !s.empty() && xgJbLoadEffects(e, s.data(), (int)s.size());
}

// Whether the engine has every one of these banks loaded, as `near` would
// have them.
bool haveBanks(XgJbEngine* e, const std::vector<int>& banks, const Place& near) {
    for (int b : banks) {
        const Wds* w = findBank(b, near);
        if (w != nullptr && !xgJbHasBank(e, b, w->addr, w->bytes)) {
            return false;
        }
    }
    return true;
}

// The banks a score or effect bank needs, on top of the base ones. Nothing
// may play meanwhile: a bank unloaded under a playing track would leave it
// reading freed instruments.
bool loadBanks(XgJbEngine* e, const std::vector<int>& banks, const Place& near, bool base = true) {
    if (base) {
        loadBase(e);
    }
    bool ok = true;
    for (int b : banks) {
        const Wds* w = findBank(b, near);
        if (w != nullptr && w->at.disc == 0 && w->at.dir == 1 && w->at.member < 0) {
            continue; // resident: loadBase has it
        }
        if (w == nullptr) {
            ok = false;
            continue;
        }
        std::vector<uint8_t> data = readFile(w->at);
        ok &= !data.empty() && xgJbLoadBank(e, data.data(), (int)data.size());
    }
    return ok;
}

std::vector<int> effectBanks(const std::vector<uint8_t>& seds, int n) {
    int banks[64];
    int k = seds.empty() ? 0 : xgJbEffectBanks(seds.data(), (int)seds.size(), n, banks, 64);
    return std::vector<int>(banks, banks + k);
}

// A score's banks: its own bank from the file the field's selector names
// (when the tables know it and it holds that bank), the others by number.
bool loadSongBanks(XgJbEngine* e, const Song& s) {
    bool ok = loadBase(e);
    std::vector<int> rest = s.banks;
    if (s.selector.disc >= 0 && !s.banks.empty()) {
        for (const Wds& w : S->wds) {
            if (w.at.disc == s.selector.disc && w.at.dir == s.selector.dir && w.at.file == s.selector.file &&
                w.at.member < 0 && w.bank == s.banks[0]) {
                if (!xgJbHasBank(e, w.bank, w.addr, w.bytes)) {
                    std::vector<uint8_t> data = readFile(w.at);
                    ok &= !data.empty() && xgJbLoadBank(e, data.data(), (int)data.size());
                }
                rest.erase(rest.begin());
                break;
            }
        }
    }
    return loadBanks(e, rest, s.at, false) && ok;
}

XgJbEngine* makeEngine() {
    XgJbEngine* e = xgJbCreate();
    if (e != nullptr) {
        loadBase(e);
        baseEffects(e);
    }
    return e;
}

// ---- playing -----------------------------------------------------------------------------------------

SDL_AudioStream* stream() {
    if (!S->streamTried) {
        S->streamTried = true;
        SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, XG_JB_RATE};
        if (SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            S->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        }
        if (S->stream != nullptr) {
            SDL_ResumeAudioStreamDevice(S->stream);
        } else {
            fprintf(stderr, "extras: jukebox: no audio device: %s\n", SDL_GetError());
        }
    }
    return S->stream;
}

void setGain() {
    if (S->stream != nullptr) {
        int v = settingsInt("volume", 100);
        v = v < 0 ? 0 : v > 100 ? 100 : v;
        float g = (float)(v * v) / 10000.0f * (float)(S->volume * S->volume) / 10000.0f;
        SDL_SetAudioStreamGain(S->stream, g);
    }
}

uint32_t muteMask() {
    uint32_t m = S->mute;
    if (S->solo != 0) {
        m |= ~S->solo & ~1u; // the conductor (track 0) keeps time
    }
    return m;
}

bool playSong(int i) {
    if (i < 0 || i >= (int)S->songs.size() || S->player == nullptr) {
        return false;
    }
    Song& s = S->songs[(size_t)i];
    std::vector<uint8_t> data = readFile(s.at);
    if (data.empty()) {
        error("%s: cannot read %s", s.name.c_str(), placeName(s.at).c_str());
        return false;
    }
    xgJbStop(S->player);
    if (!loadSongBanks(S->player, s)) {
        error("%s: a sample bank it uses is not on the discs", s.name.c_str());
        return false;
    }
    if (!xgJbPlay(S->player, data.data(), (int)data.size())) {
        error("%s: the driver did not start it", s.name.c_str());
        return false;
    }
    S->song = i;
    S->select = i;
    S->effect = -1;
    S->movie = MovieFeed();
    S->showMovie = -1;
    S->fadingOnce = false;
    S->musicRun = true;
    xgJbMute(S->player, muteMask());
    S->active = true;
    S->tailFrames = S->silentFrames = 0;
    if (stream() != nullptr) {
        SDL_ClearAudioStream(S->stream);
    }
    status("Playing %s (%s, #%02d, %s)", songTitle(s), s.name.c_str(), s.ids[0], placeName(s.at).c_str());
    return true;
}

void changePlayback(int mode) {
    S->playback = mode;
    // Restoring repeat while a one-pass fade is underway needs a fresh
    // instance: the native fade has already changed its volume ramp.
    if (mode == 2 && S->fadingOnce && S->musicRun) playSong(S->song);
    savePlayback();
}

bool playEffect(int bank, int n) {
    if (bank < 0 || bank >= (int)S->effectBanks.size() || S->player == nullptr) {
        return false;
    }
    EffectBank& b = S->effectBanks[(size_t)bank];
    if (n < 0 || n >= b.count) {
        return false;
    }
    std::vector<uint8_t> data = readFile(b.at);
    if (data.empty()) {
        error("Cannot read %s", placeName(b.at).c_str());
        return false;
    }
    std::vector<int> banks = effectBanks(data, n);
    if (!haveBanks(S->player, banks, b.at)) {
        xgJbStop(S->player); // the music too: its banks may go
        S->musicRun = false;
        if (!loadBanks(S->player, banks, b.at)) {
            error("Effect bank %s: a sample bank it uses is not on the discs", placeName(b.at).c_str());
        }
    }
    if (!xgJbEffect(S->player, data.data(), (int)data.size(), n)) {
        error("Effect %d of %s: the driver did not play it", n, placeName(b.at).c_str());
        return false;
    }
    S->effect = n;
    S->active = true;
    S->tailFrames = S->silentFrames = 0;
    stream();
    return true;
}

bool playMovie(int i) {
    if (i < 0 || i >= (int)S->movies.size() || S->player == nullptr) {
        return false;
    }
    xgJbStop(S->player);
    S->movie = MovieFeed();
    S->movie.movie = i;
    S->song = -1;
    S->musicRun = false;
    S->effect = -1;
    S->active = true;
    S->tailFrames = S->silentFrames = 0;
    if (stream() != nullptr) {
        SDL_ClearAudioStream(S->stream);
    }
    const Movie& m = S->movies[(size_t)i];
    status("Playing movie %d's audio (disc %d, %02X/%03X)", m.file - 2, xgDiscNumber(S->discs[(size_t)m.disc]),
           MOVIE_DIR, m.file);
    return true;
}

void stop() {
    if (S->player != nullptr) {
        xgJbStop(S->player);
    }
    S->movie = MovieFeed(); // what is queued in the CD input plays out
    S->fadingOnce = false;
    S->musicRun = false;
}

// A voice can retain an envelope after its sample has stopped. Use actual
// output silence, with the same bounded reverb tail as the WAV exporter.
void observeOutput(const int16_t* pcm, int frames) {
    xgJbStatus(S->player, &S->st);
    if (S->st.music || S->st.effects || !S->movie.done()) {
        S->tailFrames = S->silentFrames = 0;
        return;
    }
    S->tailFrames += frames;
    for (int i = 0; i < frames; i++)
        S->silentFrames = abs(pcm[2 * i]) <= 1 && abs(pcm[2 * i + 1]) <= 1 ? S->silentFrames + 1 : 0;
}

// Keep the device's queue fed: the driver runs as far ahead as it plays.
void pump() {
    if (S->player == nullptr) {
        return;
    }
    if (S->active && stream() != nullptr) {
        int queued = SDL_GetAudioStreamQueued(S->stream) / 4;
        int target = (int)(QUEUE_AHEAD * XG_JB_RATE);
        if (queued < target) {
            int n = std::min(target - queued, XG_JB_RATE / 4);
            S->pcm.resize((size_t)n * 2);
            feedMovie(S->player, S->movie, 12000);
            xgJbRender(S->player, S->pcm.data(), n, 0, 0);
            if (!S->movie.done()) {
                S->movie.frames += n;
            }
            observeOutput(S->pcm.data(), n);
            SDL_PutAudioStreamData(S->stream, S->pcm.data(), n * 4);
        }
    }
    setGain();
    xgJbStatus(S->player, &S->st);
    if (S->active && S->musicRun && S->song >= 0 && S->st.music && S->playback != 2 && !S->fadingOnce && S->st.loops >= 1) {
        xgJbFade(S->player, FADE_ONCE);
        S->fadingOnce = true;
    }
    if (S->fadingOnce && S->st.music && S->st.volume == 0) {
        xgJbStop(S->player);
    }
    if (S->active && (S->silentFrames >= XG_JB_RATE / 4 || S->tailFrames >= 10 * XG_JB_RATE)) {
        S->active = false; // what was queued plays out; nothing more to mix
        if (S->musicRun) {
            S->musicRun = false;
            int next = S->song + 1;
            if (S->playback == 2) next = S->song; // a non-looping score
            if (S->playback == 3 && next == (int)S->songs.size()) next = 0;
            if (S->playback != 0 && next < (int)S->songs.size()) playSong(next);
        }
    }
}

// ---- timing a score (the driver alone) ---------------------------------------------------------------

void startTiming(int i) {
    if (S->metering >= 0 || i < 0 || S->songs[(size_t)i].timed != 0) {
        return;
    }
    if (S->meter == nullptr && (S->meter = makeEngine()) == nullptr) {
        return;
    }
    Song& s = S->songs[(size_t)i];
    std::vector<uint8_t> data = readFile(s.at);
    loadSongBanks(S->meter, s);
    if (data.empty() || !xgJbPlay(S->meter, data.data(), (int)data.size())) {
        s.timed = 2;
        s.length = 0;
        return;
    }
    s.timed = 1;
    S->metering = i;
    S->marks = {{-1, -1}, -1};
    S->meterTime = 0;
}

// A frame's share (or all of it): true when the score's timing is known.
bool stepTiming(bool all) {
    if (S->metering < 0) {
        return true;
    }
    Song& s = S->songs[(size_t)S->metering];
    uint64_t start = SDL_GetTicksNS();
    bool done = false;
    while (!done && (all || SDL_GetTicksNS() - start < BUDGET_NS / 2)) {
        done = xgJbMeasure(S->meter, 5.0, &S->marks) != 0;
        S->meterTime += 5.0;
        if (S->meterTime > MAX_EXPORT) {
            done = true;
        }
    }
    if (!done) {
        return false;
    }
    if (S->marks.loop[1] >= 0) {
        s.loops = true;
        s.loopLength = S->marks.loop[1] - S->marks.loop[0];
        s.loopStart = S->marks.loop[0] - s.loopLength;
        s.length = S->marks.loop[0];
    } else {
        s.loops = false;
        s.length = S->marks.end >= 0 ? S->marks.end : S->meterTime;
    }
    s.timed = 2;
    xgJbStop(S->meter);
    S->metering = -1;
    return true;
}

// ---- exports -----------------------------------------------------------------------------------------

std::string fileSafe(const std::string& s) {
    std::string out;
    for (char c : s) {
        out += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    }
    return out;
}

std::string songFile(int i) {
    const Song& s = S->songs[(size_t)i];
    char b[96];
    snprintf(b, sizeof(b), "%s/music/%02d_%s.wav", S->region.c_str(), s.ids[0], fileSafe(s.name).c_str());
    return b;
}

std::string movieFile(int i) {
    const Movie& m = S->movies[(size_t)i];
    char b[96];
    snprintf(b, sizeof(b), "%s/movies/disc%d_movie%02d.wav", S->region.c_str(), xgDiscNumber(S->discs[(size_t)m.disc]),
             m.file - 2);
    return b;
}

std::string effectFile(int bank, int n) {
    const EffectBank& b = S->effectBanks[(size_t)bank];
    char buf[96];
    if (b.at.member >= 0) {
        snprintf(buf, sizeof(buf), "%s/effects/%02x_%03x_m%d/%03d.wav", S->region.c_str(), b.at.dir, b.at.file,
                 b.at.member, n);
    } else {
        snprintf(buf, sizeof(buf), "%s/effects/%02x_%03x/%03d.wav", S->region.c_str(), b.at.dir, b.at.file, n);
    }
    return buf;
}

bool beginExport() {
    Export& j = S->job;
    if (j.engine == nullptr && (j.engine = makeEngine()) == nullptr) {
        return false;
    }
    xgJbStop(j.engine);
    xgJbRender(j.engine, nullptr, XG_JB_RATE / 4, 0, 0); // what was sounding dies away
    j.pcm.clear();
    j.phase = 0;
    j.silent = 0;
    j.tailFrom = 0;
    j.feed = MovieFeed();
    j.sources.clear();
    S->captureExportSources = true;
    struct Capture { ~Capture() { S->captureExportSources = false; } } capture;
    if (j.movie >= 0) {
        j.feed.movie = j.movie;
        j.path = movieFile(j.movie);
    } else if (j.song >= 0) {
        Song& s = S->songs[(size_t)j.song];
        std::vector<uint8_t> data = readFile(s.at);
        loadSongBanks(j.engine, s);
        if (data.empty() || !xgJbPlay(j.engine, data.data(), (int)data.size())) {
            return false;
        }
        j.path = songFile(j.song);
    } else {
        EffectBank& b = S->effectBanks[(size_t)j.bank];
        std::vector<uint8_t> data = readFile(b.at);
        loadBanks(j.engine, effectBanks(data, j.effect), b.at);
        if (data.empty() || !xgJbEffect(j.engine, data.data(), (int)data.size(), j.effect)) {
            return false;
        }
        j.path = effectFile(j.bank, j.effect);
    }
    if (j.done == 0) {
        j.manifest = ExportManifest();
        j.manifestPath = j.total > 1 ? exportPath((S->region + (j.bank >= 0 && j.song < 0 ? "/effects_manifest.json" : "/music_manifest.json")).c_str()) : "";
    }
    j.on = true;
    return true;
}

std::vector<ExportSource> exportSources() {
    const auto& j = S->job; std::vector<ExportSource> refs = j.sources;
    auto add = [&](const Place& p, const char* role) {
        if (p.disc >= 0 && p.disc < (int)S->discs.size()) refs.push_back(exportSource(S->discs[p.disc], p.dir, p.file, role, -1, -1,
            p.member >= 0 ? std::vector<int>{p.member} : std::vector<int>{}));
    };
    if (j.movie >= 0) { const auto& m = S->movies[j.movie]; add({m.disc, 0x19, m.file, -1}, "XA"); }
    else if (j.song >= 0) add(S->songs[j.song].at, "score");
    else if (j.bank >= 0) add(S->effectBanks[j.bank].at, "effects");
    return refs;
}

void finishExport() {
    Export& j = S->job;
    int frames = (int)j.pcm.size() / 2;
    // The silence at the end goes (a little is kept).
    int keep = frames - j.silent + XG_JB_RATE / 20;
    frames = std::min(frames, std::max(keep, 0));
    std::string path = exportPath(j.path.c_str());
    if (frames <= 0) {
        error("%s: nothing was played", j.path.c_str());
    } else if (path.empty() || !xgWavWrite(path.c_str(), j.pcm.data(), frames, 2, XG_JB_RATE)) {
        error("Cannot write %s: %s", path.c_str(), xgWriterError());
    } else {
        if (recordExport(path, "jukebox", exportSources(), {{"song", std::to_string(j.song)}, {"movie", std::to_string(j.movie)},
            {"effect", std::to_string(j.effect)}, {"loops", std::to_string(j.loops)}, {"fade", std::to_string(j.fade)},
            {"pcm_frames", std::to_string(frames)}}, j.total > 1 ? &j.manifest : nullptr)) status("Exported %s (%s)", path.c_str(), clock((double)frames / XG_JB_RATE).c_str());
    }
    j.on = false;
    j.pcm.clear();
    j.pcm.shrink_to_fit();
    j.done++;
}

// Some of the export: true when it is written.
bool stepExport(bool all) {
    Export& j = S->job;
    if (!j.on) {
        return true;
    }
    uint64_t start = SDL_GetTicksNS();
    const int chunk = XG_JB_RATE / 10;
    while (all || SDL_GetTicksNS() - start < BUDGET_NS) {
        size_t at = j.pcm.size();
        int64_t before = (int64_t)at / 2;
        j.pcm.resize(at + (size_t)chunk * 2);
        int16_t* out = j.pcm.data() + at;
        int got = 0;
        int phase = j.phase;
        XgJbStatus st;
        if (phase == 0 && j.movie >= 0) {
            feedMovie(j.engine, j.feed, 12000);
            got = xgJbRender(j.engine, out, chunk, 0, 0);
            if (j.feed.done()) {
                j.phase = 2;
            }
        } else if (phase == 0 && j.song >= 0) {
            got = xgJbRender(j.engine, out, chunk, j.loops, 1);
            xgJbStatus(j.engine, &st);
            if (!st.music) {
                j.phase = 2; // it ended by itself: no loop
            } else if (st.loops >= j.loops) {
                if (j.fade > 0) {
                    xgJbFade(j.engine, j.fade);
                    j.phase = 1;
                    j.phaseEnd = before + got + (int64_t)(j.fade * XG_JB_RATE);
                } else {
                    xgJbStop(j.engine);
                    j.phase = 2;
                }
            }
        } else if (phase == 0) {
            got = xgJbRender(j.engine, out, chunk, 0, 0);
            xgJbStatus(j.engine, &st);
            if (!st.effects || before + got >= (int64_t)MAX_EFFECT * XG_JB_RATE) {
                xgJbStop(j.engine);
                j.phase = 2;
            }
        } else {
            got = xgJbRender(j.engine, out, chunk, 0, 0);
            if (phase == 1 && before + got >= j.phaseEnd) {
                xgJbStop(j.engine);
                j.phase = 2;
            }
        }
        j.pcm.resize(at + (size_t)got * 2);
        if (j.phase == 2 && phase != 2) {
            j.tailFrom = before + got;
        }
        // Silence at the end: every sample within 1 of zero.
        for (int i = 0; i < got; i++) {
            const int16_t* f = j.pcm.data() + at + (size_t)i * 2;
            j.silent = (abs(f[0]) <= 1 && abs(f[1]) <= 1) ? j.silent + 1 : 0;
        }
        int64_t frames = (int64_t)j.pcm.size() / 2;
        bool tailDone = j.phase == 2 && (j.silent >= XG_JB_RATE / 4 || frames - j.tailFrom >= 10 * XG_JB_RATE);
        if (tailDone || got == 0 || frames >= (int64_t)MAX_EXPORT * XG_JB_RATE) {
            finishExport();
            return true;
        }
    }
    return false;
}

void exportMovie(int i) {
    Export& j = S->job;
    j.movie = i;
    j.song = j.bank = j.effect = -1;
    if (!beginExport()) {
        error("Movie %d: cannot export its audio", i);
    }
}

void exportSong(int i) {
    Export& j = S->job;
    j.song = i;
    j.movie = -1;
    j.bank = j.effect = -1;
    j.loops = S->exportLoops;
    j.fade = S->exportFade;
    if (!beginExport()) {
        error("%s: cannot export it", S->songs[(size_t)i].name.c_str());
    }
}

void exportEffect(int bank, int n) {
    Export& j = S->job;
    j.song = -1;
    j.movie = -1;
    j.bank = bank;
    j.effect = n;
    if (!beginExport()) {
        error("Effect %d: cannot export it", n);
    }
}

// The queue: the next one once one is written.
void runJobs() {
    if (S->job.on) {
        stepExport(false);
        return;
    }
    if (!S->job.queue.empty()) {
        int next = S->job.queue.front();
        S->job.queue.erase(S->job.queue.begin());
        if (S->job.bank >= 0 && S->job.song < 0) {
            exportEffect(S->job.bank, next);
        } else {
            exportSong(next);
        }
        return;
    }
    if (!S->job.manifestPath.empty()) {
        if (!S->job.manifest.empty()) writeManifest(S->job.manifest, S->job.manifestPath);
        S->job.manifestPath.clear();
    }
    stepTiming(false);
    if (S->metering < 0 && S->select >= 0 && S->songs[(size_t)S->select].timed == 0) {
        startTiming(S->select);
    }
}

// ---- drawing ---------------------------------------------------------------------------------------------

// The keyboard/gamepad cursor on the item just drawn (SetKeyboardFocusHere
// would also press it).
void focusLast() {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGui::SetFocusID(g.LastItemData.ID, g.CurrentWindow);
    ImGui::SetNavCursorVisible(true);
}

// After the detail pane's first control: the cursor comes here (Right from
// the list).
void takeFocus() {
    if (S->focusDetail) {
        focusLast();
        S->focusDetail = false;
    }
}

bool songRow(int i, bool selected) {
    const Song& s = S->songs[(size_t)i];
    char id[16];
    const JukeboxTitle* ost = jukeboxTitle(s.hash, s.size);
    if (ost) snprintf(id, sizeof(id), "%d-%02d  ", ost->disc, ost->track);
    else snprintf(id, sizeof(id), "#%02d  ", s.ids[0]);
    std::string title = std::string(id) + songTitle(s);
    char code[64];
    snprintf(code, sizeof(code), "%s  (#%02d)", s.name.c_str(), s.ids[0]);
    const char* secondary = code;
    ImVec2 at = ImGui::GetCursorScreenPos();
    float line = ImGui::GetTextLineHeight(), width = ImGui::GetContentRegionAvail().x;
    bool pressed = ImGui::Selectable("##song", selected, 0, ImVec2(width, 2 * line));
    // Reserve two lines and clip/ellipsize each explicitly. Separate text
    // keeps long titles and glyph overhang inside the row while the whole
    // row remains the keyboard/gamepad navigation item.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 end(at.x + width, at.y + 2 * line);
    dl->PushClipRect(at, end, true);
    ImGui::RenderTextEllipsis(dl, at, ImVec2(end.x, at.y + line), end.x, title.c_str(), nullptr, nullptr);
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    ImVec2 sub(at.x + ImGui::CalcTextSize(id).x, at.y + line);
    ImGui::RenderTextEllipsis(dl, sub, end, end.x, secondary, nullptr, nullptr);
    ImGui::PopStyleColor();
    dl->PopClipRect();
    return pressed;
}

void listPane(ImVec2 min, ImVec2 max) {
    if (S->showEffects && S->bank >= 0) {
        EffectBank& b = S->effectBanks[(size_t)S->bank];
        char title[64];
        snprintf(title, sizeof(title), tr("Effects of %s"), placeName(b.at).c_str());
        if (beginPane("##jbeffects", min, max, title, true)) {
            for (int n = 0; n < b.count; n++) {
                char label[96];
                const char* name = b.named ? xgSfxName(b.id, n) : "";
                snprintf(label, sizeof(label), "%3d  %s", n, name[0] != '\0' ? name : "");
                ImGui::PushID(n);
                if (listItem(label, S->effect == n)) {
                    if (playEffect(S->bank, n)) {
                        sound(2);
                    }
                }
                if (S->focusList && n == std::max(S->scrollTo, 0)) {
                    focusLast();
                    S->focusList = false;
                }
                if (S->scrollTo == n) {
                    ImGui::SetScrollHereY(0.3f);
                    S->scrollTo = -1;
                }
                ImGui::PopID();
            }
        }
        endPane();
        return;
    }
    if (beginPane("##jblist", min, max, nullptr, true)) {
        if (S->listKind == 0) {
        bool extra = false;
        for (int i = 0; i < (int)S->songs.size(); i++) {
            if (!extra && !jukeboxTitle(S->songs[i].hash, S->songs[i].size)) {
                ImGui::Spacing();
                ImGui::TextUnformatted(tr("Other game cues"));
                extra = true;
            }
            ImGui::PushID(i);
            bool selected = S->song == i || (S->song < 0 && S->select == i);
            if (songRow(i, selected)) {
                if (playSong(i)) {
                    sound(2);
                    S->focusWanted = true; // across the area once this frame is drawn
                }
            }
            if (selected && focusReturning()) focusRestoreHere();
            if (S->focusList && S->scrollTo == i) {
                focusLast();
                S->focusList = false;
            }
            if (ImGui::IsItemFocused() && S->select != i && S->song != i) {
                S->select = i;
            }
            if (S->scrollTo == i) {
                ImGui::SetScrollHereY(0.3f);
                S->scrollTo = -1;
            }
            ImGui::PopID();
        }
        }
        if (S->listKind == 2) {
        for (int i = 0; i < (int)S->movies.size(); i++) {
            const Movie& m = S->movies[(size_t)i];
            char label[64];
            snprintf(label, sizeof(label), tr("movie %d, disc %d  %s##movie%d"), m.file - 2,
                     xgDiscNumber(S->discs[(size_t)m.disc]), clock(movieSeconds(m), false).c_str(), i);
            if (listItem(label, S->movie.movie == i || S->showMovie == i)) {
                S->showMovie = i;
                if (playMovie(i)) {
                    sound(2);
                }
                S->focusWanted = true;
            }
            if (S->focusList && S->scrollTo == -1000 - i) {
                focusLast();
                ImGui::SetScrollHereY(0.3f);
                S->focusList = false;
                S->scrollTo = -1;
            }
        }
        }
        if (S->listKind == 1) {
        hint("Franz on the Yggdrasil plays the field sound bank. Choose a bank, then an effect.");
        for (int i = 0; i < (int)S->effectBanks.size(); i++) {
            const EffectBank& b = S->effectBanks[(size_t)i];
            char label[96];
            if (b.at.dir == 4 && b.at.file == 0xA8 && b.at.member == -1)
                snprintf(label, sizeof(label), tr("Franz / field sounds (%d)##bank%d"), b.count, i);
            else snprintf(label, sizeof(label), "%-10s %3d  %s##bank%d", placeName(b.at).c_str(), b.count, b.label.c_str(), i);
            if (listItem(label, S->bank == i)) {
                S->bank = i;
                S->showEffects = true;
                S->effect = -1;
                S->scrollTo = 0;
                S->focusList = true;
                sound(2);
            } else if (S->focusList && S->scrollTo == -2 - i) {
                focusLast();
                ImGui::SetScrollHereY(0.3f);
                S->focusList = false;
                S->scrollTo = -1;
            }
        }
        }
    }
    endPane();
}

void meter(float level, float width) {
    float u = unit();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a(p.x, p.y + h * 0.25f), b(p.x + width, p.y + h * 0.75f);
    dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 120));
    float f = level < 0 ? 0 : level > 1 ? 1 : level;
    if (f > 0) {
        dl->AddRectFilled(a, ImVec2(a.x + width * f, b.y), ImGui::GetColorU32(skin::textGood));
    }
    dl->AddRect(a, b, IM_COL32(255, 255, 255, 60), 0, 0, u);
    ImGui::Dummy(ImVec2(width, h));
}

// A small on/off button (mute, solo): lit when on.
bool toggle(const char* label, bool on) {
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textWarning);
    }
    bool pressed = ImGui::SmallButton(label);
    if (on) {
        ImGui::PopStyleColor(2);
    }
    return pressed;
}

void songDetail(int i) {
    Song& s = S->songs[(size_t)i];
    bool playing = S->song == i && S->st.music;
    float u = unit();
    ImGui::TextWrapped("%s", songTitle(s));
    const JukeboxTitle* title = jukeboxTitle(s.hash, s.size);
    if (title) hint("Internal name: %s  /  OST %d-%02d", s.name.c_str(), title->disc, title->track);
    else hint("Internal name: %s  /  No verified OST title (ambience or unmapped variant)", s.name.c_str());
    // Where it is, what it uses, how it loops.
    std::string ids;
    for (size_t k = 0; k < s.ids.size(); k++) {
        char b[8];
        snprintf(b, sizeof(b), "%s#%02d", k ? " " : "", s.ids[k]);
        ids += b;
    }
    std::string banks;
    for (int b : s.banks) {
        const Wds* w = findBank(b, s.at);
        char t[48];
        snprintf(t, sizeof(t), "%s%02X%s", banks.empty() ? "" : " ", b, w ? "" : "?");
        banks += t;
    }
    line(skin::textHint, "Music %s  %s  %d tracks", ids.c_str(), placeName(s.at).c_str(), s.tracks);
    line(skin::textHint, "Banks %s  reverb %s %d%%", banks.c_str(), reverbName(s.reverbMode),
         (s.reverbDepth * 100 + 63) / 127);
    if (s.timed == 2) {
        if (s.loops) {
            ImGui::Text(tr("Loops from %s, %s long"), clock(s.loopStart).c_str(), clock(s.loopLength).c_str());
        } else {
            ImGui::Text(tr("Plays once, %s"), clock(s.length).c_str());
        }
    } else {
        ImGui::TextColored(skin::textHint, tr("Timing its loop..."));
    }
    // Controls.
    if (ImGui::Button(playing ? tr("Again") : tr("Play"))) {
        playSong(i);
    }
    takeFocus();
    ImGui::SameLine();
    if (ImGui::Button(tr("Stop"))) {
        stop();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Next"))) playSong((i + 1) % (int)S->songs.size());
    ImGui::SetNextItemWidth(fminf(150 * u, ImGui::GetContentRegionAvail().x));
    ImGui::SliderInt("##volume", &S->volume, 0, 100, tr("Vol %d%%"));
    // Where the driver is.
    if (S->song == i && (S->st.music || S->active)) {
        line(ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s  bar %d.%d  tempo %d  loop %d%s", clock(S->st.time).c_str(),
             S->st.measure, S->st.beat, S->st.tempo, S->st.loops, S->fadingOnce ? tr("  fading") : "");
    } else {
        ImGui::TextColored(skin::textHint, tr("Stopped"));
    }
    // Channels: as many columns as fit.
    ImGui::SeparatorText(tr("Channels"));
    int n = s.tracks < XG_JB_TRACKS ? s.tracks : XG_JB_TRACKS;
    float cell = 150 * u;
    int columns = std::max(1, std::min(3, (int)(ImGui::GetContentRegionAvail().x / cell)));
    if (ImGui::BeginTable("##tracks", columns, ImGuiTableFlags_SizingStretchSame)) {
        for (int t = 1; t < n; t++) {
            ImGui::TableNextColumn();
            ImGui::PushID(t);
            bool here = S->song == i && S->st.tracks > t;
            bool on = here && S->st.track[t].active;
            ImGui::AlignTextToFramePadding();
            if (on) {
                ImGui::Text("%2d", t);
            } else {
                ImGui::TextColored(skin::textHint, "%2d", t);
            }
            ImGui::SameLine();
            bool m = (S->mute >> t) & 1, so = (S->solo >> t) & 1;
            if (toggle("M", m)) {
                S->mute ^= 1u << t;
                xgJbMute(S->player, muteMask());
                sound(2);
            }
            ImGui::SameLine(0, 2 * u);
            if (toggle("S", so)) {
                S->solo ^= 1u << t;
                xgJbMute(S->player, muteMask());
                sound(2);
            }
            ImGui::SameLine();
            if (on) {
                ImGui::Text("%02X:%02d", S->st.track[t].bank, S->st.track[t].instrument);
            } else {
                ImGui::TextColored(skin::textHint, "--:--");
            }
            ImGui::SameLine();
            float w = ImGui::GetContentRegionAvail().x - 6 * u;
            meter(here && S->st.track[t].heard ? S->st.track[t].level / 32767.0f : 0, w > 8 * u ? w : 8 * u);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void exportControls() {
    ImGui::Spacing();
    ImGui::SeparatorText(tr("Export"));
    if (S->job.on || !S->job.queue.empty()) {
        Export& j = S->job;
        double secs = (double)j.pcm.size() / 2 / XG_JB_RATE;
        if (j.total > 1) {
            ImGui::Text(tr("Exporting %d of %d: %s"), j.done + 1, j.total, j.path.c_str());
        } else {
            ImGui::Text(tr("Exporting %s"), j.path.c_str());
        }
        ImGui::Text(tr("%s rendered%s"), clock(secs).c_str(), j.phase == 1 ? tr(", fading") : j.phase == 2 ? tr(", ending") : "");
        if (ImGui::Button(tr("Cancel"))) {
            j.queue.clear();
            j.on = false;
            j.pcm.clear();
            status("Export cancelled");
        }
        return;
    }
}

void songExport(int i) {
    exportControls();
    if (S->job.on || !S->job.queue.empty()) {
        return;
    }
    ImGui::SetNextItemWidth(80 * unit());
    ImGui::SliderInt("##loops", &S->exportLoops, 1, 8, S->exportLoops == 1 ? tr("%d loop") : tr("%d loops"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90 * unit());
    ImGui::SliderFloat("##fade", &S->exportFade, 0, 20, tr("fade %.0f s"));
    if (ImGui::Button(tr("Export WAV"))) {
        S->job.total = 1;
        S->job.done = 0;
        exportSong(i);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Export all music"))) {
        S->job.queue.clear();
        for (int k = 0; k < (int)S->songs.size(); k++) {
            S->job.queue.push_back(k);
        }
        S->job.bank = -1;
        S->job.song = 0;
        S->job.total = (int)S->songs.size();
        S->job.done = 0;
    }
    hint("WAV, 44.1 kHz stereo, as the driver plays it: up to the loop count, then the fade (a score that does "
         "not loop plays through). To %s",
         exportDir());
}

std::string bankList(const std::vector<int>& banks, const Place& near) {
    std::string out;
    for (int k : banks) {
        const Wds* w = findBank(k, near);
        char t[48];
        snprintf(t, sizeof(t), "%s%02X (%s)", out.empty() ? "" : ", ", k, w ? placeName(w->at).c_str() : tr("missing"));
        out += t;
    }
    return out;
}

void effectDetail() {
    EffectBank& b = S->effectBanks[(size_t)S->bank];
    line(skin::textHint, "%s%s%s", placeName(b.at).c_str(), b.label.empty() ? "" : ": ", b.label.c_str());
    line(skin::textHint, "Bank id %d, %d effects, %d bytes; %d sample banks%s", b.id, b.count, b.size,
         (int)b.banks.size(), b.also.empty() ? "" : "");
    if (!b.also.empty()) {
        line(skin::textHint, "The same bank is at %d more places on this disc", (int)b.also.size());
    }
    ImGui::Spacing();
    if (S->effect >= 0 && S->effect < b.count) {
        const char* name = b.named ? xgSfxName(b.id, S->effect) : "";
        line(ImGui::GetStyleColorVec4(ImGuiCol_Text), "Effect %d%s%s%s", S->effect, name[0] ? ": " : "", name,
             S->st.effects ? tr("  (playing)") : "");
        if (S->effectBanksFor != S->bank * 4096 + S->effect) {
            S->effectBanksFor = S->bank * 4096 + S->effect;
            S->effectBankText = bankList(effectBanks(readFile(b.at), S->effect), b.at);
        }
        line(skin::textHint, "Sample banks %s", S->effectBankText.c_str());
    } else {
        hint("Choose an effect to play it. Effects play with the reverb the last score set, as in the game.");
    }
    if (ImGui::Button(tr("Stop"))) {
        stop();
    }
    takeFocus();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90 * unit());
    ImGui::SliderInt("##volume", &S->volume, 0, 100, tr("Vol %d%%"));
    exportControls();
    if (S->job.on || !S->job.queue.empty()) {
        return;
    }
    if (S->effect >= 0 && ImGui::Button(tr("Export WAV"))) {
        S->job.total = 1;
        S->job.done = 0;
        exportEffect(S->bank, S->effect);
    }
    if (S->effect >= 0) {
        ImGui::SameLine();
    }
    if (ImGui::Button(tr("Export the bank"))) {
        S->job.queue.clear();
        for (int k = 0; k < b.count; k++) {
            S->job.queue.push_back(k);
        }
        S->job.song = -1;
        S->job.bank = S->bank;
        S->job.total = b.count;
        S->job.done = 0;
    }
}

void movieDetail(int i) {
    const Movie& m = S->movies[(size_t)i];
    line(skin::textHint, "Movie %d of disc %d: %02X/%03X, %d sectors", m.file - 2,
         xgDiscNumber(S->discs[(size_t)m.disc]), MOVIE_DIR, m.file, m.sectors);
    line(skin::textHint, "XA audio (file 1, channel 1), about %s: decoded by the port's XA decoder into the SPU's CD "
         "input, as the drive sends it; the stream may hold several of the game's movies.",
         clock(movieSeconds(m), false).c_str());
    bool playing = S->movie.movie == i;
    if (ImGui::Button(playing ? tr("Again") : tr("Play"))) {
        playMovie(i);
    }
    takeFocus();
    ImGui::SameLine();
    if (ImGui::Button(tr("Stop"))) {
        stop();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(84 * unit());
    ImGui::SliderInt("##volume", &S->volume, 0, 100, tr("Vol %d%%"));
    if (playing) {
        ImGui::Text(tr("%s of %s"), clock((double)S->movie.frames / XG_JB_RATE).c_str(),
                    clock(movieSeconds(m)).c_str());
    } else {
        ImGui::TextColored(skin::textHint, tr("Stopped"));
    }
    exportControls();
    if (S->job.on || !S->job.queue.empty()) {
        return;
    }
    if (ImGui::Button(tr("Export WAV"))) {
        S->job.total = 1;
        S->job.done = 0;
        exportMovie(i);
    }
}

void detailPane(ImVec2 min, ImVec2 max) {
    if (S->showEffects && S->bank >= 0) {
        char title[64];
        snprintf(title, sizeof(title), "%s", tr("Sound effects"));
        if (beginPane("##jbdetail", min, max, title, true)) {
            effectDetail();
        }
        endPane();
        return;
    }
    if (S->showMovie >= 0) {
        if (beginPane("##jbdetail", min, max, "Movie music", true)) {
            movieDetail(S->showMovie);
        }
        endPane();
        return;
    }
    int i = S->song >= 0 ? S->song : S->select;
    if (beginPane("##jbdetail", min, max, "Jukebox", true)) {
        ImGui::SetNextItemWidth(fminf(150 * unit(), ImGui::GetContentRegionAvail().x));
        const char* modes[] = {tr("Play once"), tr("Next track"), tr("Repeat track"), tr("Repeat list")};
        if (ImGui::Combo(tr("Playback"), &S->playback, modes, 4)) changePlayback(S->playback);
        if (ImGui::Checkbox(tr("Keep music in menus"), &S->keepMenu)) savePlayback();
        hint("Continues in Extras and F1; stops when the game resumes.");
        int language = japaneseTitles() ? 1 : 0;
        ImGui::SetNextItemWidth(fminf(130 * unit(), ImGui::GetContentRegionAvail().x * 0.65f));
        const char* titles[] = {tr("English"), tr("Japanese")};
        if (ImGui::Combo(tr("Titles"), &language, titles, 2)) setTitleLanguage(language ? "ja" : "en");
        ImGui::Spacing();
        if (S->discs.empty()) {
            hint("No Xenogears disc image was found.");
        } else if (i < 0) {
            ImGui::Text(tr("%d scores, %d effect banks"), (int)S->songs.size(), (int)S->effectBanks.size());
            hint("Each score and effect is played by the game's own sound driver, as the game plays it, on an SPU "
                 "of its own. Choose one to play it.");
            if (S->placeholders > 0) {
                hint("%d scores named \"Movie dammy music\" by the game are left out: the movies' music is XA "
                     "audio in the movie streams.",
                     S->placeholders);
            }
        } else {
            const Place& at = S->songs[(size_t)i].at;
            XgDisc* source = S->discs[(size_t)at.disc];
            ImGui::TextColored(skin::textHint, tr("%s disc %d, %s"), xgDiscRegion(source), xgDiscNumber(source),
                               placeName(at).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", xgDiscPath(source));
            songDetail(i);
            songExport(i);
            if (ImGui::Button(tr("Bookmark"))) bookmarkSelection();
        }
    }
    endPane();
}

// ---- the viewer -------------------------------------------------------------------------------------------

int findSong(const char* arg) {
    if (arg[0] == '@') {
        int i = atoi(arg + 1);
        return i >= 0 && i < (int)S->songs.size() ? i : -1;
    }
    if (arg[0] == '#') {
        int id = atoi(arg + 1);
        for (int i = 0; i < (int)S->songs.size(); i++) {
            for (int k : S->songs[(size_t)i].ids) {
                if (k == id) {
                    return i;
                }
            }
        }
        return -1;
    }
    for (int i = 0; i < (int)S->songs.size(); i++) {
        const Song& song = S->songs[(size_t)i];
        const JukeboxTitle* title = jukeboxTitle(song.hash, song.size);
        if (song.name == arg || (title && (strcmp(title->english, arg) == 0 || strcmp(title->japanese, arg) == 0))) {
            return i;
        }
    }
    return -1;
}

void buildLibrary(XgDisc* only) {
    uint64_t t0 = SDL_GetTicksNS();
    if (only != nullptr) S->discs.push_back(only);
    for (int i = 0; only == nullptr && i < 2; i++) {
        const char* p = cdSwapPath(i);
        if (p != nullptr && p[0] != '\0') {
            const char* why = nullptr;
            XgDisc* d = xgDiscOpen(p, &why);
            if (d != nullptr) {
                S->discs.push_back(d);
            }
        }
    }
    if (S->discs.empty()) {
        error("No Xenogears disc image found");
        return;
    }
    std::string r = xgDiscRegion(S->discs[0]);
    for (char& c : r) {
        c = (char)tolower((unsigned char)c);
    }
    S->region = (r == "?" ? std::string("xg") : r) + std::to_string(xgDiscNumber(S->discs[0]));
    scan();
    scanMovies();
    // The tables name effects by bank id: only for an id one bank has here
    // (several different banks share id 7, for one).
    for (EffectBank& b : S->effectBanks) {
        int same = 0;
        for (const EffectBank& o : S->effectBanks) {
            same += o.id == b.id;
        }
        b.named = same == 1;
    }
    // The field's own choice of sample bank for each score (its selector),
    // where the tables found it; the bank number it loads must be the
    // score's own (its header's), which is what the jukebox loads otherwise.
    int disagree = 0;
    for (Song& s : S->songs) {
        XgMusicInfo mi;
        if (xgMusicGet(s.ids[0], &mi) && mi.wdsFile > 0) {
            s.selector = Place{0, MUSIC_DIR, mi.wdsFile, -1};
            s.selectorFlags = mi.flags;
            const Wds* w = nullptr;
            for (const Wds& x : S->wds) {
                if (x.at.disc == 0 && x.at.dir == MUSIC_DIR && x.at.file == mi.wdsFile && x.at.member < 0) {
                    w = &x;
                }
            }
            if (w == nullptr || s.banks.empty() || w->bank != s.banks[0]) {
                fprintf(stderr, "extras: jukebox: %s (#%02d): the selector's bank file 1C/%03X holds bank %02X, the "
                                "score's own is %02X\n",
                        s.name.c_str(), s.ids[0], mi.wdsFile, w ? w->bank : -1, s.banks.empty() ? -1 : s.banks[0]);
                disagree++;
            }
        }
    }
    std::stable_sort(S->songs.begin(), S->songs.end(), [](const Song& a, const Song& b) {
        const auto* x = jukeboxTitle(a.hash, a.size);
        const auto* y = jukeboxTitle(b.hash, b.size);
        if (!x || !y) return x && !y;
        return x->disc != y->disc ? x->disc < y->disc : x->track < y->track;
    });
    // The resident field effects are the most useful starting point.
    std::stable_partition(S->effectBanks.begin(), S->effectBanks.end(), [](const EffectBank& b) {
        return b.at.dir == 4 && b.at.file == 0xA8 && b.at.member == -1;
    });
    S->player = makeEngine();
    if (S->player == nullptr) {
        error("The sound driver could not be set up");
    }
    fprintf(stderr,
            "extras: jukebox: %d scores (%d movie placeholders left out), %d effect banks, %d sample banks; the "
            "field's selector disagrees on %d; %.0f ms\n",
            (int)S->songs.size(), S->placeholders, (int)S->effectBanks.size(), (int)S->wds.size(), disagree,
            (SDL_GetTicksNS() - t0) / 1e6);
}

void opened() {
    if (S) return; // menu music retains its player and the selected score
    S = new State;
    loadPlayback();
    buildLibrary(nullptr);
}

void releaseResources() {
    if (S == nullptr) return;
    if (!S->job.manifestPath.empty()) {
        if (!S->job.manifest.empty()) writeManifest(S->job.manifest, S->job.manifestPath);
        S->job.manifestPath.clear();
    }
    if (S->stream != nullptr) {
        SDL_DestroyAudioStream(S->stream);
    }
    xgJbDestroy(S->player);
    xgJbDestroy(S->meter);
    xgJbDestroy(S->job.engine);
    for (XgDisc* d : S->discs) {
        xgDiscClose(d);
    }
}

void closed() {
    if (S && S->keepMenu && S->musicRun) return;
    releaseResources();
    delete S;
    S = nullptr;
}

// Which pane ImGui's cursor is in (0 none).
int navPane(float splitX) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if (g.NavId == 0 || g.NavWindow == nullptr || !g.NavCursorVisible) {
        return 0;
    }
    ImRect r = ImGui::WindowRectRelToAbs(g.NavWindow, g.NavWindow->NavRectRel[g.NavLayer]);
    return r.GetCenter().x < splitX ? 1 : 2;
}

// ---- the focused view: the score or movie across the area, its controls in a bar -------------------

bool focusable() {
    if (S->showEffects) return false; // effects stay a list: each one plays from it
    if (S->showMovie >= 0) return S->showMovie < (int)S->movies.size();
    return S->song >= 0 || S->select >= 0;
}

void setFocused(bool on) {
    if (on == S->focused || (on && !focusable())) return;
    S->focused = on;
    if (on) focusEnter();
    else focusLeave();
}

void focusedView(const Area& a) {
    if (!focusable()) { setFocused(false); return; }
    FocusLayout f = focusLayout(a, 1);
    detailPane(f.min, f.max);
    if (beginFocusBar(f)) setFocused(false);
    if (S->showMovie >= 0) {
        int n = (int)S->movies.size();
        int step = focusButton("Previous") ? -1 : 0;
        if (focusButton("Next")) step = 1;
        if (step && n > 0) {
            S->showMovie = (S->showMovie + step + n) % n;
            if (playMovie(S->showMovie)) sound(1);
        }
    } else {
        int n = (int)S->songs.size(), i = S->song >= 0 ? S->song : S->select;
        int step = focusButton("Previous") ? -1 : 0;
        if (focusButton("Next")) step = 1;
        if (step && n > 0) {
            S->select = (i + step + n) % n;
            if (playSong(S->select)) sound(1);
        }
    }
    if (focusButton("Stop")) stop();
    endFocusBar();
}

void draw(const Area& a) {
    float u = a.unit;
    if (S->focused) {
        focusedView(a);
        return;
    }
    ImGui::SetCursorScreenPos(a.min);
    const char* lists[] = {N_("Music"), N_("Sound effects"), N_("Movies")};
    for (int k = 0; k < 3; k++) {
        if (k) ImGui::SameLine(0, 10 * u);
        ImGui::PushID(k);
        bool chosen = skin::selectable(tr(lists[k]), S->listKind == k);
        ImGui::PopID();
        if (chosen) {
            S->listKind = k; S->showEffects = false; S->showMovie = -1;
            S->scrollTo = -1; S->focusList = false;
        }
    }
    Area body = a;
    body.min.y += 20 * u;
    float split = floorf(fminf(220 * u, (a.max.x - a.min.x) * 0.38f) / u) * u;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    S->navIn = navPane(a.min.x + split + 8 * u);
    if (S->rightFrom != 0) {
        S->focusDetail = g.NavId == S->rightFrom; // the cursor had nowhere to go
        S->rightFrom = 0;
    }
    if (S->navIn == 1 && !ImGui::IsAnyItemActive() &&
        (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight))) {
        S->rightFrom = g.NavId;
    }
    listPane(body.min, ImVec2(a.min.x + split, a.max.y));
    detailPane(ImVec2(a.min.x + split + 16 * u, body.min.y), a.max);
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
    if (S->navIn == 2) {
        // Back to the list, on what the detail pane shows.
        S->focusList = true;
        if (S->showEffects) {
            S->scrollTo = std::max(S->effect, 0);
        } else if (S->showMovie >= 0) {
            S->scrollTo = -1000 - S->showMovie;
        } else {
            S->scrollTo = S->song >= 0 ? S->song : std::max(S->select, 0);
        }
        return true;
    }
    if (S->showEffects) {
        S->showEffects = false;
        S->scrollTo = -2 - S->bank; // the cursor back on the bank
        S->focusList = true;
        return true;
    }
    return false;
}

bool writeWav(const char* path, const std::vector<int16_t>& pcm) {
    return xgWavWrite(path, pcm.data(), (int)pcm.size() / 2, 2, XG_JB_RATE) != 0;
}

bool command(const char* cmd, const char* arg) {
    if (strcmp(cmd, "titles") == 0) return setTitleLanguage(arg);
    if (strcmp(cmd, "focus") == 0) {
        bool on = strcmp(arg, "0") != 0;
        setFocused(on);
        return S->focused == on;
    }
    if (strcmp(cmd, "image") == 0) {
        if (S->discs.size() == 1 && strcmp(xgDiscPath(S->discs[0]), arg) == 0) return S->player != nullptr;
        const char* why = nullptr;
        XgDisc* d = xgDiscOpen(arg, &why);
        if (d == nullptr) { error("%s: %s", arg, why); return false; }
        // Build a library from this one image: deduplicated scores and
        // resident banks must all belong to the linked field's source.
        releaseResources();
        *S = State();
        loadPlayback();
        buildLibrary(d);
        return S->player != nullptr;
    }
    if (strcmp(cmd, "play") == 0) {
        int i = findSong(arg);
        if (i < 0) {
            error("No score %s", arg);
            return false;
        }
        S->listKind = 0;
        S->showEffects = false;
        S->scrollTo = i;
        S->focusList = true;
        return playSong(i);
    }
    if (strcmp(cmd, "effect") == 0) {
        int bank = 0, n = 0;
        if (sscanf(arg, "%d %d", &bank, &n) != 2) {
            return false;
        }
        if (bank < 0 || bank >= (int)S->effectBanks.size() || n < 0 || n >= S->effectBanks[bank].count) return false;
        S->listKind = 1;
        S->bank = bank;
        S->showEffects = true;
        S->scrollTo = n;
        S->focusList = true;
        return playEffect(bank, n);
    }
    if (strcmp(cmd, "movie") == 0) {
        int i = atoi(arg);
        S->listKind = 2;
        S->showEffects = false;
        S->showMovie = i;
        playMovie(i);
        return true;
    }
    if (strcmp(cmd, "moviewav") == 0) {
        exportMovie(atoi(arg));
        stepExport(true);
        return true;
    }
    if (strcmp(cmd, "stop") == 0) {
        stop();
        return true;
    }
    if (strcmp(cmd, "loop") == 0) {
        changePlayback(strcmp(arg, "off") != 0 ? 2 : 0);
        return true;
    }
    if (strcmp(cmd, "playback") == 0) {
        const char* modes[] = {"once", "next", "repeat", "all"};
        for (int i = 0; i < 4; i++) if (!strcmp(arg, modes[i])) {
            changePlayback(i); return true;
        }
        return false;
    }
    if (strcmp(cmd, "menu-music") == 0) {
        S->keepMenu = !strcmp(arg, "on"); savePlayback(); return true;
    }
    if (strcmp(cmd, "list-kind") == 0) {
        const char* kinds[] = {"music", "effects", "movies"};
        for (int i = 0; i < 3; i++) if (!strcmp(arg, kinds[i])) {
            S->listKind = i; S->showEffects = false; S->showMovie = -1; return true;
        }
        return false;
    }
    if (strcmp(cmd, "next") == 0) return !S->songs.empty() && playSong((S->song + 1) % (int)S->songs.size());
    if (strcmp(cmd, "mute") == 0) {
        S->mute = (uint32_t)strtoul(arg, nullptr, 16);
        xgJbMute(S->player, muteMask());
        return true;
    }
    if (strcmp(cmd, "solo") == 0) {
        int t = atoi(arg);
        S->solo = t > 0 && t < 32 ? 1u << t : 0;
        xgJbMute(S->player, muteMask());
        return true;
    }
    if (strcmp(cmd, "export") == 0 || strcmp(cmd, "effectwav") == 0) {
        if (strcmp(cmd, "export") == 0) {
            int loops = 0;
            float fade = -1;
            sscanf(arg, "%d %f", &loops, &fade);
            if (loops > 0) {
                S->exportLoops = loops;
            }
            if (fade >= 0) {
                S->exportFade = fade;
            }
            int i = S->song >= 0 ? S->song : S->select;
            if (i < 0) {
                return false;
            }
            exportSong(i);
        } else {
            int bank = 0, n = 0;
            if (sscanf(arg, "%d %d", &bank, &n) != 2) {
                return false;
            }
            exportEffect(bank, n);
        }
        stepExport(true);
        return true;
    }
    if (strcmp(cmd, "render") == 0) {
        double secs = 0;
        char path[512] = "";
        if (sscanf(arg, "%lf %511s", &secs, path) != 2 || secs <= 0) {
            return false;
        }
        std::vector<int16_t> pcm((size_t)(secs * XG_JB_RATE) * 2);
        for (int at = 0, frames = (int)pcm.size() / 2; at < frames;) {
            int n = std::min(frames - at, XG_JB_RATE / 10);
            xgJbRender(S->player, pcm.data() + 2 * at, n, 0, 0);
            observeOutput(pcm.data() + 2 * at, n);
            at += n;
        }
        if (!writeWav(path, pcm)) {
            error("Cannot write %s", path);
        } else {
            status("Rendered %s", path);
        }
        return true;
    }
    if (strcmp(cmd, "measure") == 0) {
        int i = S->song >= 0 ? S->song : S->select;
        if (i < 0) {
            return false;
        }
        S->songs[(size_t)i].timed = 0;
        if (S->metering >= 0) {
            stepTiming(true);
        }
        startTiming(i);
        stepTiming(true);
        const Song& s = S->songs[(size_t)i];
        fprintf(stderr, "extras: jukebox: %s: %s %.3f s, loop %.3f s from %.3f s\n", s.name.c_str(),
                s.loops ? "loops" : "once", s.length, s.loopLength, s.loopStart);
        return true;
    }
    if (strcmp(cmd, "list") == 0) {
        for (int i = 0; i < (int)S->songs.size(); i++) {
            const Song& s = S->songs[(size_t)i];
            std::string banks;
            for (int b : s.banks) {
                const Wds* w = findBank(b, s.at);
                char t[48];
                snprintf(t, sizeof(t), " %02X@%s", b, w ? placeName(w->at).c_str() : "missing");
                banks += t;
            }
            fprintf(stderr, "jukebox: score %2d #%02d %-12s %s %5d bytes %2d tracks reverb %d/%3d banks%s\n", i,
                    s.ids[0], s.name.c_str(), placeName(s.at).c_str(), s.size, s.tracks, s.reverbMode, s.reverbDepth,
                    banks.c_str());
            const JukeboxTitle* title = jukeboxTitle(s.hash, s.size);
            fprintf(stderr, "jukebox: title #%02d %s | %s | OST %d-%02d\n", s.ids[0], songTitle(s),
                    s.name.c_str(), title ? title->disc : 0, title ? title->track : 0);
        }
        for (int i = 0; i < (int)S->effectBanks.size(); i++) {
            const EffectBank& b = S->effectBanks[(size_t)i];
            std::string banks;
            for (int k : b.banks) {
                const Wds* w = findBank(k, b.at);
                char t[48];
                snprintf(t, sizeof(t), " %02X@%s", k, w ? placeName(w->at).c_str() : "missing");
                banks += t;
            }
            fprintf(stderr, "jukebox: effects %2d %s id %04X %3d effects %5d bytes (%d copies) banks%s\n", i,
                    placeName(b.at).c_str(), b.id, b.count, b.size, (int)b.also.size() + 1, banks.c_str());
        }
        return true;
    }
    if (strcmp(cmd, "info") == 0) {
        if (S->song >= 0) {
            const Place& at = S->songs[S->song].at;
            XgDisc* d = S->discs[at.disc];
            fprintf(stderr, "extras: jukebox: source %s disc %d: %s; file %02X/%03X\n",
                    xgDiscRegion(d), xgDiscNumber(d), xgDiscPath(d), at.dir, at.file);
            const Song& song = S->songs[S->song];
            fprintf(stderr, "extras: jukebox: title %s | %s | #%02d\n", songTitle(song), song.name.c_str(), song.ids[0]);
        }
        const XgJbStatus& st = S->st;
        fprintf(stderr, "extras: jukebox: music %d effects %d time %.2f loops %d measure %d.%d tempo %d vol %d error %d\n",
                st.music, st.effects, st.time, st.loops, st.measure, st.beat, st.tempo, st.volume, st.error);
        return true;
    }
    return false;
}

bool bookmark(Bookmark& b) {
    int index = S->song >= 0 ? S->song : S->select;
    if (index < 0 || index >= (int)S->songs.size()) return false;
    const Song& song = S->songs[(size_t)index];
    std::string label = std::string(songTitle(song)) + " (" + song.name + ")";
    if (song.ids.empty() || !bookmarkSource(b, S->discs[(size_t)song.at.disc], "jukebox", label)) return false;
    b.target.commands.push_back({"play", "#" + std::to_string(song.ids[0])});
    if (!S->active) b.target.commands.push_back({"stop", ""});
    return true;
}

const Viewer viewer = {"jukebox", "Jukebox", "Music, sound effects and movie music, played by the game's own sound driver", 70,
                       opened,    closed,    draw,
                       back,      command, nullptr, nullptr, bookmark};

} // namespace

XG_EXTRAS_VIEWER(viewer);

extern "C" void extrasJukeboxTick(void) {
    if (S) { pump(); runJobs(); }
}
extern "C" void extrasJukeboxEnd(void) {
    if (!S) return;
    releaseResources(); delete S; S = nullptr;
}
