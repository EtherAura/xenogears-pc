// The settings menu's diagnostics (diagnostics.h, xg-htn.35.134):
//
//   the performance overlay  a panel in the window's top-left corner, drawn by
//                            the present pass with the hotkeys' note font
//                            (port/src/gpu/present.c) while this menu is
//                            closed: the last second's frame time (mean and
//                            worst, the rate), the game thread's CPU time and
//                            its parts (port/src/system/cputime.h), the GPU's
//                            time and the wait for it, the audio queue and its
//                            target, underruns, and the pack decodes on the
//                            game thread. Never in VRAM or the game's frames.
//   the performance log      every vblank's numbers (XG_CPU_TIMING's line, the
//                            GPU time, the audio queue, the field and screen
//                            kind) in perf-<date>-<time>.log beside
//                            settings.ini, and a summary at its end (also
//                            shown in the menu)
//   the audio meter          the output's queue, target and underruns
//                            (port/src/spu/output.c)
//   the packs page           every pack library (texture-packs, sprite-packs,
//                            glyph-packs, portrait-packs, ui-packs, beside the
//                            executable unless settings.ini names another
//                            folder): its pack.ini, what it holds, on and
//                            off (its settings key), reload
//   the A/B comparison       every replacement withheld and shown again in
//                            place (textureReplacementsShow): nothing is
//                            unloaded, so the picture comes back exactly
//
// None of it needs an environment variable. Settings (settings.ini):
//   perf_overlay          0 (default) or 1: the overlay
//   perf_overlay_key      its hotkey (default F10)
//   perf_log_key          the log's start and stop (default Shift+F10)
//   replacements_ab_key   the A/B comparison (default F12)
// A key is an SDL key name with "Shift+", "Ctrl+" or "Alt+" before it, or
// "none". The hotkeys work while the game runs (port/src/system/pad.c).

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"
#include "diagnostics.h"
#include "l10n.h"
#include "menu.h"
#include "options.h"
#include "skin.h"
#include "ui.h"

extern "C" {
#include "gpu/gpu.h"
#include "gpu/portrait_pack.h"
#include "pc_wide.h"
#include "spu/output.h"
#include "system/cputime.h"
#include "system/engine.h"
#include "system/hw.h"
#include "system/settings.h"
// port/src/gpu/texture.h (the GPU layer's internal header): a library read again from its settings.
void texturePackShutdown(void);
void textureCelShutdown(void);
// What the game shows now; weak: none before the game's code runs.
int gpuTextureField(void) __attribute__((weak));
}

using namespace menu;

namespace {

constexpr int SPU_HZ = 44100;
constexpr int RING = 512; // vblanks kept (the overlay's second at up to ~500 Hz)
const char* const modeNames[PC_WIDE_KINDS] = {"kernel", "field", "battle", "worldmap", "battling", "menu", "movie"};

// ---- the hotkeys ------------------------------------------------------------------------

enum { MOD_SHIFT = 1, MOD_CTRL = 2, MOD_ALT = 4 };
struct Hotkey {
    SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
    int mods = 0;
    char text[48] = "";
};

Hotkey parseKey(const char* s, const char* def) {
    Hotkey h;
    if (s == nullptr || s[0] == '\0') s = def;
    char buf[64];
    SDL_strlcpy(buf, s, sizeof(buf));
    char* name = buf;
    for (;;) {
        char* plus = strchr(name, '+');
        if (plus == nullptr || plus[1] == '\0') break; // "Shift++"? a '+' key name stays
        *plus = '\0';
        if (!SDL_strcasecmp(name, "shift")) h.mods |= MOD_SHIFT;
        else if (!SDL_strcasecmp(name, "ctrl") || !SDL_strcasecmp(name, "control")) h.mods |= MOD_CTRL;
        else if (!SDL_strcasecmp(name, "alt")) h.mods |= MOD_ALT;
        name = plus + 1;
    }
    if (SDL_strcasecmp(name, "none") != 0) h.key = SDL_GetScancodeFromName(name);
    if (h.key == SDL_SCANCODE_UNKNOWN && SDL_strcasecmp(name, "none") != 0 && s != def) return parseKey(def, def);
    if (h.key == SDL_SCANCODE_UNKNOWN) {
        SDL_strlcpy(h.text, "none", sizeof(h.text));
    } else {
        snprintf(h.text, sizeof(h.text), "%s%s%s%s", h.mods & MOD_CTRL ? "Ctrl+" : "", h.mods & MOD_ALT ? "Alt+" : "",
                 h.mods & MOD_SHIFT ? "Shift+" : "", SDL_GetScancodeName(h.key));
    }
    return h;
}

bool hotkeyMatches(const Hotkey& h, int scancode, int mod) {
    if (h.key == SDL_SCANCODE_UNKNOWN || scancode != (int)h.key) return false;
    int mods = (mod & SDL_KMOD_SHIFT ? MOD_SHIFT : 0) | (mod & SDL_KMOD_CTRL ? MOD_CTRL : 0) |
               (mod & SDL_KMOD_ALT ? MOD_ALT : 0);
    return mods == h.mods;
}

// ---- the state ----------------------------------------------------------------------------

struct Sample {
    CpuTimingFrame f;
    double gpu;
    bool gpuValid;
    uint64_t gameLoads, celLate; // the pack's counts so far (TexturePackStats)
};

struct Column {
    double sum = 0, worst = 0;
    uint32_t worstVb = 0;
    void add(double v, uint32_t vb) {
        sum += v;
        if (v > worst) worst = v, worstVb = vb;
    }
};

struct State {
    bool init = false;
    unsigned keysRevision = ~0u;
    Hotkey overlayKey, logKey, abKey;
    bool overlay = false;
    bool timing = false;      // cpuTimingRequest held
    bool gpuCollect = false;  // gvkGpuTimingCollect on
    Sample ring[RING];
    int ringPos = 0, ringCount = 0;
    // the log
    FILE* log = nullptr;
    std::string logPath;
    uint64_t logStartNs = 0;
    uint32_t logVblanks = 0, logLate1 = 0, logLateFrame = 0;
    int logStarved = 0, logDry = 0, logCut = 0;
    double logQueueSum = 0, logQueueMin = 0;
    Column cols[CPU_KINDS + 7]; // the kinds, then wall cpu game runq idle late gpu
    uint32_t gpuN = 0;
    std::vector<std::string> summary; // the last log's, for the menu
    std::string logError;
};
State s;

enum { COL_WALL = CPU_KINDS, COL_CPU, COL_GAME, COL_RUNQ, COL_IDLE, COL_LATE, COL_GPU };

void readKeys() {
    if (s.keysRevision == settingsRevision()) return;
    s.keysRevision = settingsRevision();
    s.overlayKey = parseKey(settingsGet("perf_overlay_key"), "F10");
    s.logKey = parseKey(settingsGet("perf_log_key"), "Shift+F10");
    s.abKey = parseKey(settingsGet("replacements_ab_key"), "F12");
}

void applyTiming() {
    bool want = s.overlay || s.log != nullptr;
    if (want != s.timing) {
        cpuTimingRequest(want ? 1 : 0);
        s.timing = want;
    }
    if (!want && s.gpuCollect) {
        gvkGpuTimingCollect(0);
        s.gpuCollect = false;
    }
}

void writeLine(const Sample& x);

// cputime.c, at each vblank while its timing runs.
void onFrame(const CpuTimingFrame* f) {
    if ((s.overlay || s.log != nullptr) && !s.gpuCollect) s.gpuCollect = gvkGpuTimingCollect(1) != 0;
    Sample& x = s.ring[s.ringPos];
    x.f = *f;
    double kinds[8];
    x.gpuValid = s.gpuCollect && gvkGpuTimingLast(&x.gpu, kinds, 8);
    if (!x.gpuValid) x.gpu = 0;
    TexturePackStats ts;
    texturePackStats(&ts);
    x.gameLoads = ts.gameThreadLoads + ts.poseSync; // images and sprite poses decoded on the game thread
    // ... preloads drawn before their decode finished: cels and poses (decoded there), field pages (waited for
    // in a room's first frames, or drawn as the original)
    x.celLate = ts.celLate + ts.poseLate + ts.pageWaits + ts.pageNative;
    s.ringPos = (s.ringPos + 1) % RING;
    if (s.ringCount < RING) s.ringCount++;
    if (s.log != nullptr) writeLine(x);
}

void init() {
    if (s.init) return;
    s.init = true;
    settingsDescribe("perf_overlay", "perf_overlay: 1 shows the performance panel over the game (F1 menu closed); 0 (default) not");
    settingsDescribe("perf_overlay_key", "perf_overlay_key: the performance panel's hotkey (default F10; Shift+, Ctrl+, Alt+ or none)");
    settingsDescribe("perf_log_key", "perf_log_key: starts and stops a performance log beside settings.ini (default Shift+F10)");
    settingsDescribe("replacements_ab_key", "replacements_ab_key: hides and shows every texture, sprite and glyph replacement in place (default F12)");
    s.overlay = settingsBool("perf_overlay", 0) != 0;
    cpuTimingSetListener(onFrame);
    readKeys();
    applyTiming();
}

void setOverlay(bool on) {
    s.overlay = on;
    settingsSetInt("perf_overlay", on ? 1 : 0);
    settingsSave();
    applyTiming();
}

void setReplacements(bool shown) {
    textureReplacementsShow(shown ? 1 : 0);
    presentShowNote(shown ? "Replacements shown" : "Replacements hidden (A/B)");
}

// ---- the last second ------------------------------------------------------------------------

struct Window {
    int n = 0;
    double wall = 0, wallWorst = 0, cpu = 0, cpuWorst = 0, game = 0, idle = 0, gpu = 0, kinds[CPU_KINDS] = {0};
    double loadWorst = 0, span = 0;
    int gpuN = 0, late = 0, starved = 0, dry = 0;
    uint64_t gameLoads = 0, celLate = 0;
};

Window lastSecond() {
    Window w;
    const Sample* newest = nullptr;
    const Sample* oldest = nullptr;
    for (int i = 0; i < s.ringCount; i++) {
        const Sample& x = s.ring[(s.ringPos - 1 - i + RING) % RING];
        if (i > 0 && w.span + x.f.wall > 1000.0) break;
        if (newest == nullptr) newest = &x;
        oldest = &x;
        w.n++;
        w.span += x.f.wall;
        w.wall += x.f.wall;
        w.wallWorst = std::max(w.wallWorst, x.f.wall);
        w.cpu += x.f.cpu;
        w.cpuWorst = std::max(w.cpuWorst, x.f.cpu);
        w.game += x.f.game;
        w.idle += x.f.idle;
        for (int k = 0; k < CPU_KINDS; k++) w.kinds[k] += x.f.ms[k];
        w.loadWorst = std::max(w.loadWorst, x.f.ms[CPU_LOAD]);
        if (x.gpuValid) w.gpu += x.gpu, w.gpuN++;
        w.late += x.f.late > 1.0;
        w.starved += x.f.starved;
        w.dry += x.f.dry;
    }
    if (w.n == 0) return w;
    w.wall /= w.n;
    w.cpu /= w.n;
    w.game /= w.n;
    w.idle /= w.n;
    for (double& k : w.kinds) k /= w.n;
    if (w.gpuN) w.gpu /= w.gpuN;
    w.gameLoads = newest->gameLoads - oldest->gameLoads;
    w.celLate = newest->celLate - oldest->celLate;
    return w;
}

double framesMs(double frames) {
    return frames * 1000.0 / SPU_HZ;
}

// ---- the log ----------------------------------------------------------------------------------

std::string logDir() {
    const char* path = settingsPath();
    if (path != nullptr) {
        std::string p = path;
        size_t slash = p.find_last_of("/\\");
        return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
    }
    char* pref = SDL_GetPrefPath(NULL, "xenogears");
    std::string p = pref != nullptr ? pref : ".";
    SDL_free(pref);
    while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    return p;
}

void writeLine(const Sample& x) {
    const CpuTimingFrame& f = x.f;
    SpuOutputLevels a;
    spuOutputLevels(&a);
    int field = gpuTextureField != nullptr ? gpuTextureField() : -1;
    int mode = pcWideGetMode();
    fprintf(s.log, "%u %.3f %.3f %.3f", f.vblank, f.wall, f.cpu, f.game);
    for (int k = 0; k < CPU_KINDS; k++) fprintf(s.log, " %.3f", f.ms[k]);
    fprintf(s.log, " %.3f %.3f %.3f", f.runq, f.idle, f.late);
    if (x.gpuValid) fprintf(s.log, " %.3f", x.gpu);
    else fputs(" -", s.log);
    fprintf(s.log, " %.1f %.1f %d %d %d %d %s\n", framesMs(a.queued), framesMs(a.target), f.dry, f.cut, f.starved, field,
            mode >= 0 && mode < PC_WIDE_KINDS ? modeNames[mode] : "?");
    s.logVblanks++;
    for (int k = 0; k < CPU_KINDS; k++) s.cols[k].add(f.ms[k], f.vblank);
    s.cols[COL_WALL].add(f.wall, f.vblank);
    s.cols[COL_CPU].add(f.cpu, f.vblank);
    s.cols[COL_GAME].add(f.game, f.vblank);
    s.cols[COL_RUNQ].add(f.runq, f.vblank);
    s.cols[COL_IDLE].add(f.idle, f.vblank);
    s.cols[COL_LATE].add(f.late, f.vblank);
    if (x.gpuValid) s.cols[COL_GPU].add(x.gpu, f.vblank), s.gpuN++;
    s.logLate1 += f.late > 1.0;
    s.logLateFrame += f.late > 1000.0 / 59.94;
    s.logStarved += f.starved;
    s.logDry += f.dry;
    s.logCut += f.cut;
    double q = framesMs(a.queued);
    s.logQueueSum += q;
    if (s.logVblanks == 1 || q < s.logQueueMin) s.logQueueMin = q;
}

void startLog() {
    if (s.log != nullptr) return;
    time_t now = time(nullptr);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char name[64], stamp[32];
    strftime(name, sizeof(name), "perf-%Y-%m-%d-%H%M%S.log", &tmv);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv);
    s.logPath = logDir() + "/" + name;
    s.log = fopen(s.logPath.c_str(), "w");
    if (s.log == nullptr) {
        s.logError = "Cannot write " + s.logPath;
        presentShowNote("Performance log: cannot write it");
        return;
    }
    s.logError.clear();
    s.summary.clear();
    for (Column& c : s.cols) c = Column();
    s.gpuN = 0;
    s.logVblanks = s.logLate1 = s.logLateFrame = 0;
    s.logStarved = s.logDry = s.logCut = 0;
    s.logQueueSum = s.logQueueMin = 0;
    s.logStartNs = SDL_GetTicksNS();
    const char* settings = settingsPath();
    fprintf(s.log, "# Xenogears performance log, %s: the %s engine, %s; settings %s\n", stamp, engineName(engineThis()),
            texturePackVersion(), settings != nullptr ? settings : "none");
    fprintf(s.log, "# A line per vblank. Times in ms since the vblank before (XG_CPU_TIMING's line, port/src/system/cputime.c):\n"
                   "# wall, cpu (the game thread's), game (cpu less the timed parts), the timed parts (each exclusive),\n"
                   "# runq (waiting for a core), idle, late (the vblank's delivery after its due time); gpu: the GPU time of\n"
                   "# the passes whose results came back (about a vblank late; '-' without timestamps); queue, target: the\n"
                   "# audio output's queue and target in ms; dry, cut, starved: audio events since the vblank before; field:\n"
                   "# the game's field id; mode: what the game shows. The summary follows the last line.\n");
    fputs("vb wall cpu game", s.log);
    for (int k = 0; k < CPU_KINDS; k++) fprintf(s.log, " %s", cpuTimingKindName(k));
    fputs(" runq idle late gpu queue target dry cut starved field mode\n", s.log);
    applyTiming();
    char note[96];
    snprintf(note, sizeof(note), "Performance log: %s", name);
    presentShowNote(note);
}

void stopLog() {
    if (s.log == nullptr) return;
    double seconds = (double)(SDL_GetTicksNS() - s.logStartNs) * 1e-9;
    std::vector<std::string>& out = s.summary;
    out.clear();
    char line[1024];
    uint32_t n = s.logVblanks;
    snprintf(line, sizeof(line), "%u vblanks in %.1f s (%.2f a second)", n, seconds, seconds > 0 ? n / seconds : 0.0);
    out.push_back(line);
    if (n > 0) {
        auto col = [&](const char* name, const Column& c, uint32_t count) {
            char part[96];
            snprintf(part, sizeof(part), " %s %.3f/%.3f", name, count ? c.sum / count : 0.0, c.worst);
            return std::string(part);
        };
        std::string a = "mean/worst ms:";
        a += col("wall", s.cols[COL_WALL], n) + col("cpu", s.cols[COL_CPU], n) + col("game", s.cols[COL_GAME], n);
        if (s.gpuN) a += col("gpu", s.cols[COL_GPU], s.gpuN);
        a += col("idle", s.cols[COL_IDLE], n) + col("runq", s.cols[COL_RUNQ], n);
        out.push_back(a);
        std::string b = "parts:";
        for (int k = 0; k < CPU_KINDS; k++)
            if (s.cols[k].worst > 0) b += col(cpuTimingKindName(k), s.cols[k], n);
        out.push_back(b);
        snprintf(line, sizeof(line), "slowest vblank %u (wall %.3f ms); late vblanks: %u by more than 1 ms, %u by more "
                 "than a frame (worst %.3f ms at vblank %u)", s.cols[COL_WALL].worstVb, s.cols[COL_WALL].worst,
                 s.logLate1, s.logLateFrame, s.cols[COL_LATE].worst, s.cols[COL_LATE].worstVb);
        out.push_back(line);
        snprintf(line, sizeof(line), "audio: %d underruns, %d dry pushes, %d cuts; queue mean %.1f ms, least %.1f ms", s.logStarved,
                 s.logDry, s.logCut, s.logQueueSum / n, s.logQueueMin);
        out.push_back(line);
    }
    for (const std::string& l : out) fprintf(s.log, "# summary: %s\n", l.c_str());
    fclose(s.log);
    s.log = nullptr;
    applyTiming();
    fprintf(stderr, "diagnostics: performance log %s: %u vblanks\n", s.logPath.c_str(), n);
    presentShowNote("Performance log stopped");
}

// ---- the menu's rows ----------------------------------------------------------------------

int dropdown(const char* id, const char* const* names, int count, int value) {
    int chosen = value;
    ImGui::SetNextItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * scale(), 6 * scale()));
    bool open = ImGui::BeginCombo(id, value >= 0 && value < count ? tr(names[value]) : "");
    ImGui::PopStyleVar();
    if (open) {
        skin::frameWindow();
        for (int i = 0; i < count; i++) {
            if (ImGui::Selectable(tr(names[i]), value == i)) chosen = i;
            if (value == i) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return chosen;
}

void replacementsRow() {
    if (!opt::row(N_("Compare replacements"), "replacements_ab_key")) return;
    static const char* const names[] = {N_("Shown"), N_("Hidden: the original pictures")};
    int v = textureReplacementsShown() ? 0 : 1;
    int c = dropdown("##diag_ab", names, 2, v);
    if (c != v) setReplacements(c == 0);
    hint("Every texture, sprite, lettering, HUD and portrait replacement off and on again in place (nothing is "
         "reloaded), to compare with the original pictures. Hotkey: %s (replacements_ab_key).", s.abKey.text);
}

void audioRows() {
    SpuOutputLevels a;
    spuOutputLevels(&a);
    if (opt::row(N_("Audio queue"))) {
    if (!a.open) {
        hint("No sound output yet.");
    } else {
        char text[96];
        snprintf(text, sizeof(text), "%.1f ms", framesMs(a.queued));
        float fraction = a.max > 0 ? std::min(1.0f, (float)a.queued / (float)a.max) : 0.0f;
        ImGui::ProgressBar(fraction, ImVec2(-1, 0), text);
        hint("%d frames now, %.1f ms on average%s; the bar's end is the ceiling.", a.queued, framesMs(a.average),
             a.prefilling ? " (filling its reserve)" : "");
    }
    }
    if (opt::row(N_("Audio target"), "audio_latency audio_latency_max") && a.open) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%.0f ms", framesMs(a.target));
        int grown = a.target - a.base;
        if (grown > 0)
            hint("audio_latency %.0f ms, grown %.0f ms by underruns (shrinks again after half a minute without one), "
                 "up to audio_latency_max %.0f ms.", framesMs(a.base), framesMs(grown), framesMs(a.max));
        else
            hint("audio_latency %.0f ms; underruns grow it up to audio_latency_max %.0f ms.", framesMs(a.base),
                 framesMs(a.max));
    }
    if (opt::row(N_("Underruns"))) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%d", spuOutputStarved());
        hint("Since the game started: the device found the queue empty (a gap). %d pushes found it dry, %d lossy cuts.",
             spuOutputDryPushes(), spuOutputCuts());
    }
}

} // namespace

// ---- the C side ------------------------------------------------------------------------------

// The extras (port/src/extras/shell.cpp, their own event loop): the A/B key is a viewer's
// Stock / Enhanced there. 1 when the key is replacements_ab_key.
extern "C" int diagnosticsReplacementsKey(int scancode, int mod) {
    init();
    readKeys();
    return hotkeyMatches(s.abKey, scancode, mod) ? 1 : 0;
}

extern "C" int diagnosticsHotkey(int scancode, int mod) {
    init();
    readKeys();
    if (hotkeyMatches(s.overlayKey, scancode, mod)) {
        setOverlay(!s.overlay);
        presentShowNote(s.overlay ? "Performance overlay" : "Performance overlay off");
        return 1;
    }
    if (hotkeyMatches(s.logKey, scancode, mod)) {
        if (s.log != nullptr) stopLog();
        else startLog();
        return 1;
    }
    if (hotkeyMatches(s.abKey, scancode, mod)) {
        setReplacements(!textureReplacementsShown());
        return 1;
    }
    return 0;
}

extern "C" int diagnosticsOverlayText(char* out, int size) {
    init();
    if (size <= 0) return 0;
    out[0] = '\0';
    bool hidden = !textureReplacementsShown();
    if ((!s.overlay && !hidden && s.log == nullptr) || uiIsOpen()) return 0;
    int at = 0;
#define put(...) (void)(at < size ? at += snprintf(out + at, (size_t)(size - at), __VA_ARGS__) : 0)
    if (s.overlay) {
        Window w = lastSecond();
        if (w.n == 0) {
            put("PERF: STARTING\n");
        } else {
            put("FRAME %.2f MS WORST %.2f (%.1f/S)\n", w.wall, w.wallWorst, w.span > 0 ? w.n * 1000.0 / w.span : 0.0);
            put("CPU %.2f WORST %.2f GAME %.2f\n", w.cpu, w.cpuWorst, w.game);
            put("GP0 %.2f SPANS %.2f CACHE %.2f\n", w.kinds[CPU_GP0], w.kinds[CPU_SPANS], w.kinds[CPU_CACHE]);
            double between = w.kinds[CPU_BKEEP] + w.kinds[CPU_BMEM] + w.kinds[CPU_BDRAW] + w.kinds[CPU_BPUT];
            put("SPU %.2f PRESENT %.2f STATE %.2f", w.kinds[CPU_SPU], w.kinds[CPU_PRESENT], w.kinds[CPU_STATE]);
            if (between > 0) put(" BETWEEN %.2f", between);
            put("\n");
            if (w.gpuN) put("GPU %.2f WAIT %.2f IDLE %.2f\n", w.gpu, w.kinds[CPU_GPUWAIT], w.idle);
            else put("GPU - WAIT %.2f IDLE %.2f\n", w.kinds[CPU_GPUWAIT], w.idle);
            SpuOutputLevels a;
            spuOutputLevels(&a);
            if (a.open) put("AUDIO %.0f/%.0f MS UNDERRUNS %d\n", framesMs(a.queued), framesMs(a.target), spuOutputStarved());
            else put("AUDIO - UNDERRUNS %d\n", spuOutputStarved());
            put("LATE %d STARVED %d DRY %d\n", w.late, w.starved, w.dry);
            put("PACK LOAD %.2f WORST %.2f SYNC %llu LATE %llu\n", w.kinds[CPU_LOAD], w.loadWorst,
                (unsigned long long)w.gameLoads, (unsigned long long)w.celLate);
        }
    }
    if (hidden) put("REPLACEMENTS HIDDEN (A/B)\n");
    if (s.log != nullptr) put("PERF LOG %u VBLANKS\n", s.logVblanks);
#undef put
    if (at >= size) at = size - 1;
    while (at > 0 && out[at - 1] == '\n') out[--at] = '\0';
    return out[0] != '\0';
}

// ---- the menu --------------------------------------------------------------------------------

void diagnosticsPerfRows() {
    init();
    readKeys();
    if (opt::row(N_("Performance overlay"), "perf_overlay perf_overlay_key")) {
        static const char* const names[] = {N_("Off"), N_("On: a panel in the corner")};
        int v = s.overlay ? 1 : 0;
        int c = dropdown("##diag_overlay", names, 2, v);
        if (c != v) setOverlay(c == 1);
        hint("Over the game while this menu is closed: the last second's frame time, the CPU's time and its parts, "
             "the GPU's time and the wait for it, the audio queue, underruns and the pack decodes. Hotkey: %s "
             "(perf_overlay_key).", s.overlayKey.text);
        overrideNote("perf_overlay");
    }
    if (opt::row(N_("Performance log"), "perf_log_key")) {
        char label[96];
        snprintf(label, sizeof(label), "%s###diag_log", s.log != nullptr ? tr("Stop recording") : tr("Start recording"));
        if (!opt::indexing() && ImGui::Button(label)) {
            if (s.log != nullptr) stopLog();
            else startLog();
        }
        if (s.log != nullptr) {
            hint("Recording %u vblanks to %s", s.logVblanks, s.logPath.c_str());
        } else if (!s.summary.empty()) {
            hint("Written to %s:", s.logPath.c_str());
            for (const std::string& l : s.summary) hint("%s", l.c_str());
        } else if (!s.logError.empty()) {
            hint("%s", s.logError.c_str());
        }
        hint("Every vblank's timing, GPU time, audio queue, field and screen in perf-<date>.log beside settings.ini, "
             "with a summary when it stops. Hotkey: %s (perf_log_key).", s.logKey.text);
    }
    audioRows();
    replacementsRow();
}

// ---- the packs page --------------------------------------------------------------------------

namespace {

struct Counts {
    long png = 0, pose = 0, other = 0;
};
struct Walk {
    Counts* c;
    int depth;
};

SDL_EnumerationResult SDLCALL countEntry(void* user, const char* dirname, const char* name) {
    Walk* w = (Walk*)user;
    size_t n = strlen(name);
    auto ends = [&](const char* suffix) {
        size_t k = strlen(suffix);
        return n >= k && SDL_strcasecmp(name + n - k, suffix) == 0;
    };
    if (ends(".png")) w->c->png++;
    else if (ends(".pose")) w->c->pose++;
    else if (strchr(name, '.') == nullptr && w->depth < 3) {
        std::string path = std::string(dirname) + name;
        SDL_PathInfo info;
        if (SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
            Walk deeper = {w->c, w->depth + 1};
            SDL_EnumerateDirectory(path.c_str(), countEntry, &deeper);
        } else {
            w->c->other++;
        }
    } else {
        w->c->other++;
    }
    return SDL_ENUM_CONTINUE;
}

Counts countFiles(const std::string& dir) {
    Counts c;
    Walk w = {&c, 0};
    if (!dir.empty()) SDL_EnumerateDirectory(dir.c_str(), countEntry, &w);
    return c;
}

std::string readText(const std::string& path, size_t limit) {
    size_t n = 0;
    void* data = SDL_LoadFile(path.c_str(), &n);
    if (data == nullptr) return std::string();
    std::string text((const char*)data, std::min(n, limit));
    SDL_free(data);
    return text;
}

bool isDir(const std::string& path) {
    SDL_PathInfo info;
    return !path.empty() && SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

std::string fileDate(const std::string& path) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info)) return std::string();
    time_t t = (time_t)(info.modify_time / 1000000000);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char out[32];
    strftime(out, sizeof(out), "%Y-%m-%d %H:%M", &tmv);
    return out;
}

long countOf(const std::string& text, const char* needle) {
    long n = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) n++;
    return n;
}

// The sum of the numbers after each "key": in a JSON text (merge.json's poses and cel_maps).
long sumOf(const std::string& text, const char* key) {
    long sum = 0;
    for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + 1)) {
        size_t colon = text.find(':', at);
        if (colon != std::string::npos) sum += strtol(text.c_str() + colon + 1, nullptr, 10);
    }
    return sum;
}

struct Ini {
    std::string name, author, version, path;
};
Ini readIni(const std::vector<std::string>& candidates) {
    Ini ini;
    for (const std::string& p : candidates) {
        std::string text = readText(p, 8192);
        if (text.empty()) continue;
        ini.path = p;
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            at = end == std::string::npos ? text.size() : end + 1;
            size_t eq = line.find('=');
            if (eq == std::string::npos || line[0] == '#' || line[0] == ';') continue;
            auto trim = [](std::string v) {
                while (!v.empty() && (v.back() == '\r' || v.back() == ' ' || v.back() == '\t')) v.pop_back();
                size_t b = v.find_first_not_of(" \t");
                return b == std::string::npos ? std::string() : v.substr(b);
            };
            std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
            if (k == "name") ini.name = v;
            else if (k == "author") ini.author = v;
            else if (k == "version") ini.version = v;
        }
        break;
    }
    return ini;
}

enum Library { LIB_TEXTURES, LIB_SPRITES, LIB_GLYPHS, LIB_PORTRAITS, LIB_UI, LIBS };
const char* const libTitles[LIBS] = {N_("Field textures"), N_("Field sprites"), N_("Lettering"), N_("Portraits"),
                                     N_("Cel HUD and icons")};
const char* const libFolders[LIBS] = {"texture-packs", "sprite-packs", "glyph-packs", "portrait-packs", "ui-packs"};
const char* const libKeys[LIBS] = {"texture_pack", "sprite_packs", "glyph_packs", "portrait_packs", "ui_packs"};

struct PackInfo {
    bool scanned = false;
    bool on = false;
    std::string root, defaultRoot, status;
    Ini ini;
    std::vector<std::string> facts;
};
PackInfo libs[LIBS];

std::string version() {
    return texturePackVersion();
}

void scan(int lib) {
    PackInfo& p = libs[lib];
    p = PackInfo();
    p.scanned = true;
    char def[4096];
    enginePackDir(libFolders[lib], def, sizeof(def));
    p.defaultRoot = def;
    char line[512];
    switch (lib) {
    case LIB_TEXTURES: {
        TexturePackStats ts;
        texturePackStats(&ts);
        p.root = texturePackRoot();
        p.on = ts.enabled != 0;
        p.status = ts.message;
        std::string dir = p.root + "/" + version();
        p.ini = readIni({dir + "/pack.ini", p.root + "/pack.ini"});
        if (!isDir(dir)) break;
        Counts c = countFiles(dir);
        std::string rooms = readText(dir + "/rooms.txt", 1u << 24);
        std::vector<int> fields;
        long pages = 0;
        for (size_t at = 0; at < rooms.size();) {
            size_t end = rooms.find('\n', at);
            int field;
            if (sscanf(rooms.c_str() + at, "%d", &field) == 1) {
                pages++;
                if (std::find(fields.begin(), fields.end(), field) == fields.end()) fields.push_back(field);
            }
            at = end == std::string::npos ? rooms.size() : end + 1;
        }
        snprintf(line, sizeof(line), "%ld PNG images; rooms.txt: %ld pages for %zu fields", c.png, pages, fields.size());
        p.facts.push_back(line);
        if (!rooms.empty()) p.facts.push_back("rooms.txt " + fileDate(dir + "/rooms.txt"));
        break;
    }
    case LIB_SPRITES: {
        p.root = textureSpritePackRoot();
        p.on = !p.root.empty();
        p.status = p.on ? "Ready" : "Off, or no " + version() + " folder";
        if (!p.on) break;
        p.ini = readIni({p.root + "/pack.ini"});
        std::string parts = readText(p.root + "/parts.tsv", 1u << 26);
        long cels = 0, distinct = 0;
        std::string last;
        for (size_t at = 0; at < parts.size();) {
            size_t end = parts.find('\n', at);
            std::string row = parts.substr(at, end == std::string::npos ? std::string::npos : end - at);
            at = end == std::string::npos ? parts.size() : end + 1;
            if (row.empty() || row[0] == '#') continue;
            std::string part = row.substr(0, row.find_first_of("\t "));
            cels++;
            if (part != last) distinct++, last = part;
        }
        std::string dir = p.root + "/" + version();
        Counts c = countFiles(dir);
        snprintf(line, sizeof(line), "parts.tsv: %ld parts, %ld cels; %ld cel maps, %ld poses", distinct, cels,
                 c.png - c.pose, c.pose);
        p.facts.push_back(line);
        std::string merge = readText(p.root + "/merge.json", 1u << 26);
        if (!merge.empty()) {
            snprintf(line, sizeof(line), "merge.json (%s): %ld merged sets, %ld poses, %ld cel maps",
                     fileDate(p.root + "/merge.json").c_str(), countOf(merge, "\"pack\""), sumOf(merge, "\"poses\""),
                     sumOf(merge, "\"cel_maps\""));
            p.facts.push_back(line);
        }
        std::string prov = readText(p.root + "/provenance.json", 1u << 26);
        if (!prov.empty()) {
            snprintf(line, sizeof(line), "provenance.json (%s): %ld replacements", fileDate(p.root + "/provenance.json").c_str(),
                     countOf(prov, "\"key\""));
            p.facts.push_back(line);
        }
        break;
    }
    case LIB_GLYPHS: {
        std::string dir = textureGlyphPackDir();
        p.on = !dir.empty();
        const char* set = settingsGet("glyph_packs");
        p.root = p.on ? dir.substr(0, dir.find_last_of("/\\")) : set != nullptr ? set : p.defaultRoot;
        p.status = p.on ? (textureUseGlyphs(PC_TEX_TEXT) ? "Ready" : "Ready; Text's appearance is not Glyphs")
                        : "Off, or no " + version() + " folder";
        p.ini = readIni({p.root + "/pack.ini", dir + "/pack.ini"});
        if (!p.on) break;
        Counts c = countFiles(dir);
        snprintf(line, sizeof(line), "%ld glyph maps", c.png);
        p.facts.push_back(line);
        static const char* const provenance[] = {"provenance.json", "provenance-sheets.json", "provenance-bios.json"};
        for (const char* f : provenance) {
            std::string text = readText(p.root + "/" + f, 1u << 26);
            if (text.empty()) continue;
            snprintf(line, sizeof(line), "%s (%s): %ld glyphs", f, fileDate(p.root + "/" + f).c_str(), countOf(text, "\"key\""));
            p.facts.push_back(line);
        }
        break;
    }
    case LIB_PORTRAITS: {
        p.root = portraitPackRoot();
        PortraitPackStats ps;
        portraitPackStats(&ps);
        p.on = ps.enabled || ps.menuEnabled || ps.battleEnabled;
        p.status = ps.message;
        for (int i = 1; i < portraitPackCount(); i++) {
            const PortraitSet* set = portraitPackSet(i);
            Counts c = countFiles(p.root + "/" + set->id + "/" + version());
            snprintf(line, sizeof(line), "%s: %s%s%s, %ld pictures", set->id, set->name, set->author[0] ? " by " : "",
                     set->author, c.png);
            p.facts.push_back(line);
        }
        break;
    }
    case LIB_UI: {
        p.root = textureCelRoot();
        p.status = textureCelStatus();
        p.ini = readIni({p.root + "/pack.ini"});
        static const int scales[] = {2, 4, 8};
        for (int sc : scales) {
            char path[64];
            snprintf(path, sizeof(path), "/cel-%dx/", sc);
            std::string list = readText(p.root + path + version() + "/sprites.txt", 1u << 16);
            if (list.empty()) continue;
            p.on = true;
            long n = 0;
            for (size_t at = 0; at < list.size();) {
                size_t end = list.find_first_of("\r\n", at);
                std::string row = list.substr(at, end == std::string::npos ? std::string::npos : end - at);
                at = end == std::string::npos ? list.size() : end + 1;
                size_t b = row.find_first_not_of(" \t");
                if (b != std::string::npos && row[b] != '#') n++;
            }
            snprintf(line, sizeof(line), "cel %dx: %ld sprites", sc, n);
            p.facts.push_back(line);
        }
        break;
    }
    }
}

// On: the library beside the executable (its key out of settings.ini); off: none.
void setLibrary(int lib, bool on) {
    switch (lib) {
    case LIB_TEXTURES:
        textureFreezeAppearances(); // every category keeps its look (as the Display section's pack folder does)
        if (on) {
            settingsUnset("texture_pack");
            settingsSave();
            texturePackShutdown(); // read again from its settings: the default library, not a chosen pack
            texturePackReload();
        } else {
            texturePackSetRoot("");
        }
        break;
    case LIB_SPRITES:
    case LIB_GLYPHS:
        if (on) settingsUnset(libKeys[lib]);
        else settingsSet(libKeys[lib], "none");
        settingsSave();
        texturePackReload();
        break;
    case LIB_UI:
        if (on) {
            settingsUnset("ui_packs");
            settingsSave();
            textureCelShutdown();
            textureCelReload();
        } else {
            textureCelSetRoot("none");
        }
        break;
    default:
        break;
    }
    for (PackInfo& p : libs) p.scanned = false; // one reload reads several libraries again
}

void reloadLibrary(int lib) {
    if (lib == LIB_PORTRAITS) portraitPackReload();
    else if (lib == LIB_UI) textureCelReload();
    else texturePackReload();
    for (PackInfo& p : libs) p.scanned = false;
}

void portraitChoice(const char* label, const char* key, const char* id, const char* selected, int (*select)(const char*)) {
    if (!opt::row(label, key)) return;
    std::vector<const char*> names;
    int value = 0;
    for (int i = 0; i < portraitPackCount(); i++) {
        const PortraitSet* set = portraitPackSet(i);
        if (!strcmp(set->id, selected)) value = i;
        names.push_back(i == 0 ? N_("Original") : set->id);
    }
    int c = dropdown(id, names.data(), (int)names.size(), value);
    if (c != value) {
        select(portraitPackSet(c)->id);
        libs[LIB_PORTRAITS].scanned = false;
    }
}

void library(int lib) {
    PackInfo& p = libs[lib];
    if (!p.scanned) scan(lib);
    opt::group(libTitles[lib]);
    if (opt::row(N_("Folder"))) {
        ImGui::AlignTextToFramePadding();
        textLines(p.root.empty() ? tr("none") : p.root.c_str());
        if (p.root != p.defaultRoot) hint("Beside the executable: %s (%s in settings.ini names another).", p.defaultRoot.c_str(), libKeys[lib]);
    }
    if (!p.ini.path.empty() && opt::row(N_("Pack"))) {
        ImGui::AlignTextToFramePadding();
        textLines(p.ini.name.empty() ? "-" : p.ini.name.c_str());
        std::string by = p.ini.author;
        if (!p.ini.version.empty()) by += (by.empty() ? "version " : ", version ") + p.ini.version;
        if (!by.empty()) textLines(by.c_str());
        hint("From its pack.ini (%s).", fileDate(p.ini.path).c_str());
    }
    if (!p.facts.empty() && opt::row(N_("Contents"))) {
        ImGui::AlignTextToFramePadding();
        for (const std::string& f : p.facts) textLines(f.c_str());
        hint("Counted from the pack's files when this page first showed, and after each reload.");
    }
    if (lib == LIB_PORTRAITS) {
        portraitChoice(N_("Dialogue"), "portrait_set", "##diag_dialogue", portraitPackSelected(), portraitPackSelect);
        portraitChoice(N_("Menu"), "menu_portrait_set", "##diag_menu", portraitMenuPackSelected(), portraitMenuPackSelect);
        portraitChoice(N_("Battle"), "battle_portrait_set", "##diag_battle", portraitBattlePackSelected(), portraitBattlePackSelect);
        if (!p.status.empty() && opt::plain()) hint("%s", p.status.c_str());
    } else if (opt::row(N_("Use"), libKeys[lib])) {
        static const char* const names[] = {N_("On"), N_("Off")};
        int v = p.on ? 0 : 1;
        char id[32];
        snprintf(id, sizeof(id), "##diag_use%d", lib);
        int c = dropdown(id, names, 2, v);
        if (c != v) setLibrary(lib, c == 0);
        overrideNote(libKeys[lib]);
        if (!p.status.empty()) hint("%s", p.status.c_str());
    }
    if (opt::row(N_("Reload"))) {
        char label[96];
        snprintf(label, sizeof(label), "%s##diag_reload%d", tr("Read the files again"), lib);
        if (!opt::indexing() && ImGui::Button(label)) reloadLibrary(lib);
        if (lib == LIB_TEXTURES || lib == LIB_SPRITES || lib == LIB_GLYPHS)
            hint("Field textures, sprites and lettering are read again together.");
    }
}

} // namespace

void diagnosticsPacksPage() {
    init();
    readKeys();
    if (opt::plain())
        textLines(tr("The replacement libraries this game finds (beside the executable unless settings.ini names "
                     "another folder). Each draws only where its category's appearance uses it (Display)."));
    replacementsRow();
    for (int lib = 0; lib < LIBS; lib++) library(lib);
}
