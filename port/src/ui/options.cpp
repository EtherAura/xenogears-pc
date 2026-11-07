// The settings menu's rows, groups, search, changed markers and presets
// (options.h).

#include <SDL3/SDL.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "l10n.h"
#include "menu.h"
#include "options.h"
#include "sfx.h"
#include "skin.h"

extern "C" {
#include "cd/cd.h"
#include "cd/regional.h"
#include "gpu/gpu.h"
#include "gpu/interpolation.h"
#include "gpu/moviepack.h"
#include "gpu/portrait_pack.h"
#include "gpu/shaderchain.h"
#include "pc_wide.h"
#include "spu/output.h"
#include "system/engine.h"
#include "system/gameplay.h"
#include "system/hw.h"
#include "system/mouse.h"
#include "system/pad.h"
#include "system/rewind.h"
#include "system/rumble.h"
#include "system/settings.h"
#include "system/speedup.h"
int pcFreeCameraEnabled(void); /* port/src/system/freecam.c */
void pcFreeCameraSet(int on);
int gvkDeviceIntegrated(void); /* port/src/gpu/vk.c: Auto's GPU budget is 8 ms by default there */
}

namespace menu {
namespace opt {

namespace {

// ---- the settings the menu knows ------------------------------------------------------

// A setting: how to read it, change it (through its module, so it applies as
// the menu's own control would) and its default; its values' names for the
// presets' confirmation. Integer-valued (get/set/def, an argument for the
// ones that come in families: a texture kind, an enemy stat), or text-valued.
struct Spec {
    std::string key;
    const char* label = nullptr; // N_(): as a preset's confirmation lists it
    int arg = 0;
    int (*get)(int) = nullptr;
    void (*set)(int, int) = nullptr;
    int (*def)(int) = nullptr;
    int defValue = 0;
    const char* const* names = nullptr; // a name for each of values (or for 0..count-1)
    const int* values = nullptr;
    int count = 0;
    const char* unit = ""; // numbers without names: "%", " ms"
    const char* (*getText)() = nullptr;
    void (*setText)(const char*) = nullptr;
    const char* defText = "";
    bool mine = true; // kept by "Save my current setup" (not a device's own)

    bool text() const { return getText != nullptr; }
    int value() const { return get ? get(arg) : 0; }
    int fallback() const { return def ? def(arg) : defValue; }
    bool isChanged() const {
        if (text()) {
            const char* v = getText();
            return strcmp(v ? v : "", defText) != 0;
        }
        return value() != fallback();
    }
    void restore() const {
        if (text()) setText(defText);
        else if (set) set(arg, fallback());
    }
    std::string show(int v) const {
        for (int i = 0; names != nullptr && i < count; i++) {
            if ((values ? values[i] : i) == v) return tr(names[i]);
        }
        char t[48];
        snprintf(t, sizeof t, "%d%s", v, unit);
        return t;
    }
    std::string showNow() const { return text() ? std::string(getText() ? getText() : "") : show(value()); }
    std::string showDefault() const { return text() ? std::string(defText) : show(fallback()); }
};

const char* const offOn[] = {N_("Off"), N_("On")};
const char* const appearanceNames[] = {N_("Original"), N_("Linear"),        N_("Edge"),   N_("Upscaled 2x"),
                                       N_("Upscaled 4x"), N_("Texture pack"), N_("Enhanced text"), N_("Cel 2x"),
                                       N_("Cel 4x"),   N_("Cel 8x")};
const char* const upscaleNames[] = {N_("Auto (from the window)"), N_("1x (PS1 resolution)"), "2x", "3x", "4x", "5x",
                                    "6x", "7x", "8x", "9x", "10x", "11x", "12x"};
const char* const filterNames[] = {N_("Nearest"), N_("Linear"), N_("Sharp"), N_("Bicubic"), N_("xBR (edges)")};
const char* const movieFilterNames[] = {N_("Same as the filter"), N_("Nearest"), N_("Linear"),
                                        N_("Sharp"), N_("Bicubic"), N_("xBR (edges)")};
const char* const colourNames[] = {N_("True colour"), N_("15-bit"), N_("15-bit fine"), N_("Raw 8-bit")};
const char* const rateNames[] = {N_("Native (59.8 Hz)"), N_("60 Hz"), N_("120 Hz"), N_("144 Hz"), N_("240 Hz")};
const int rateValues[] = {0, 60, 120, 144, 240};
const char* const speedNames[] = {N_("Original"), "2x", "3x", "4x", "8x", N_("Instant")};
const int speedValues[] = {1, 2, 3, 4, 8, 0};
const char* const autoNames[] = {N_("Off"), "1 s", "2 s", "3 s", "5 s", "8 s"};
const int autoValues[] = {0, 1, 2, 3, 5, 8};
const char* const budgetNames[] = {"128 MB", "256 MB", "512 MB", "1 GB"};
const int budgetValues[] = {128, 256, 512, 1024};
const char* const gpuMbNames[] = {"256 MB", "512 MB", "1 GB", "2 GB", "4 GB"};
const int gpuMbValues[] = {256, 512, 1024, 2048, 4096};

// A yes/no or number key the module reads from settings.ini each time.
int keyInt(const char* key, int def) { return settingsInt(key, def); }
void keySet(const char* key, int v) {
    settingsSetInt(key, v);
    settingsSave();
}

std::vector<Spec>& table() {
    static std::vector<Spec> t;
    if (!t.empty()) return t;
    auto add = [&](const char* key, const char* label, int (*get)(int), void (*set)(int, int), int def,
                   const char* const* names = offOn, int count = 2, const int* values = nullptr) -> Spec& {
        Spec s;
        s.key = key;
        s.label = label;
        s.get = get;
        s.set = set;
        s.defValue = def;
        s.names = names;
        s.count = names ? count : 0;
        s.values = values;
        t.push_back(s);
        return t.back();
    };
    auto addText = [&](const char* key, const char* label, const char* (*get)(), void (*set)(const char*),
                       const char* def) -> Spec& {
        Spec s;
        s.key = key;
        s.label = label;
        s.getText = get;
        s.setText = set;
        s.defText = def;
        t.push_back(s);
        return t.back();
    };
    // Display
    addText("aspect", N_("Aspect"), [] { return presentAspectText(); }, [](const char* v) { presentSetAspectText(v); },
            "4:3tv");
    static const char* const framing[] = {N_("Original"), N_("Expanded")};
    add("expanded_vertical", N_("Vertical framing"), [](int) { return presentExpandedVertical(); },
        [](int, int v) { presentSetExpandedVertical(v); }, 0, framing);
    static const char* const scales[] = {N_("Fit"), N_("Integer"), N_("Stretch")};
    add("scale", N_("Scale"), [](int) { return presentScaleMode(); }, [](int, int v) { presentSetScale(v); }, 0, scales, 3);
    static const char* const fills[] = {N_("Bars"), N_("Zoom"), N_("Blur")};
    add("widefill", N_("Wide fill"), [](int) { return presentFillMode(); }, [](int, int v) { presentSetFill(v); }, 0,
        fills, 3);
    add("fullscreen", N_("Fullscreen"),
        [](int) { return presentWindow() != NULL && (SDL_GetWindowFlags(presentWindow()) & SDL_WINDOW_FULLSCREEN) ? 1 : 0; },
        [](int, int v) { presentSetFullscreen(v); }, 0).mine = false;
    add("upscale", N_("Resolution"), [](int) { return hiresUpscale(); }, [](int, int v) { hiresSetUpscale(v); }, 1,
        upscaleNames, 13);
    {
        Spec& s = add("upscale_gpu_ms", N_("Auto GPU limit"), [](int) { return hiresGpuBudget(); },
                      [](int, int v) { hiresSetGpuBudget(v); }, 0, nullptr);
        s.def = [](int) { return gvkDeviceIntegrated() > 0 ? 8 : 0; };
        s.unit = " ms";
        s.mine = false; // the device's
    }
    add("gpu_ordered_batches", N_("GPU ordering"), [](int) { return gpuOrderedFallback(); },
        [](int, int v) { gpuSetOrderedFallback(v); }, 0);
    static const char* const waits[] = {N_("Sleep between frames"), N_("Spin (steady clock)")};
    add("vsync_wait", N_("VSync wait"), [](int) { return hwSpinWait(); }, [](int, int v) { hwSetSpinWait(v); }, 0,
        waits);
    add("display_lock", N_("Display lock"), [](int) { return keyInt("display_lock", 0) != 0 ? 1 : 0; },
        [](int, int v) { keySet("display_lock", v); }, 0);
    {
        // In hundredths of a hertz (settings.ini has it as a decimal).
        Spec& s = add("display_lock_tolerance", N_("Lock tolerance"),
                      [](int) {
                          const char* v = settingsGet("display_lock_tolerance");
                          return v != NULL && v[0] != '\0' ? (int)lround(atof(v) * 100) : 5;
                      },
                      [](int, int v) {
                          char text[16];
                          snprintf(text, sizeof text, "%d.%02d", v / 100, v % 100);
                          settingsSet("display_lock_tolerance", text);
                          settingsSave();
                      },
                      5, nullptr);
        static const char* const names[] = {"0.01 Hz", "0.02 Hz", "0.05 Hz", "0.10 Hz", "0.15 Hz", "0.20 Hz", "0.24 Hz"};
        static const int values[] = {1, 2, 5, 10, 15, 20, 24};
        s.names = names;
        s.values = values;
        s.count = 7;
    }
    add("upscale_color", N_("Colour"), [](int) { return hiresColor(); }, [](int, int v) { hiresSetColor(v); }, 0,
        colourNames, 4);
    add("present_filter", N_("Filter"), [](int) { return presentFilter(); }, [](int, int v) { presentSetFilter(v); }, 0,
        filterNames, 5);
    add("present_movie_filter", N_("Movies"), [](int) { return presentMovieFilter() - PRESENT_FILTER_SAME; },
        [](int, int v) { presentSetMovieFilter(v + PRESENT_FILTER_SAME); }, 0, movieFilterNames, 6);
    add("upscale_cpu_effects", N_("High-resolution pause blur"), [](int) { return hiresCpuEffects(); },
        [](int, int v) { hiresSetCpuEffects(v); }, 0);
    add("upscale_large", N_("Half-resolution large surfaces"), [](int) { return hiresCoarseLarge(); },
        [](int, int v) { hiresSetCoarseLarge(v); }, 0);
    add("geometry_precision", N_("Subpixel geometry"), [](int) { return hiresPrecision(); },
        [](int, int v) { hiresSetPrecision(v); }, 0)
        .def = [](int) { return hiresUpscale() != 1 ? 1 : 0; };
    add("perspective_textures", N_("Perspective textures"), [](int) { return hiresPerspective(); },
        [](int, int v) { hiresSetPerspective(v); }, 1);
    add("geometry_slivers", N_("Restore thin surfaces"), [](int) { return hiresSlivers(); },
        [](int, int v) { hiresSetSlivers(v); }, 0);
    add("geometry_seam_guard", N_("Close surface gaps"), [](int) { return hiresSeamGuard(); },
        [](int, int v) { hiresSetSeamGuard(v); }, 1);
    // The appearance categories (Display > Textures and art).
    static const struct {
        const char* key;
        const char* label;
        int kind;
    } kinds[] = {{"texture_style_3d", N_("3D textures"), PC_TEX_3D},     {"texture_style_sprite", N_("Sprites"), PC_TEX_SPRITE},
                 {"texture_style_hud", N_("HUD"), PC_TEX_HUD},          {"texture_style_battle_hud", N_("Battle HUD"), PC_TEX_BATTLE_HUD},
                 {"texture_style_text", N_("Text"), PC_TEX_TEXT},       {"texture_style_icon", N_("Battle icons"), PC_TEX_ICON}};
    for (const auto& k : kinds) {
        add(k.key, k.label, [](int kind) { return textureAppearance(kind); },
            [](int kind, int v) { textureSetAppearance(kind, v); }, 0, appearanceNames, 10)
            .arg = k.kind;
    }
    static const struct {
        const char* key;
        const char* label;
        int kind;
    } portraits[] = {{"texture_filter_portrait", N_("Dialogue portraits"), PC_TEX_PORTRAIT},
                     {"texture_filter_menu_portrait", N_("Menu portraits"), PC_TEX_MENU_PORTRAIT},
                     {"texture_filter_battle_portrait", N_("Battle portraits"), PC_TEX_BATTLE_PORTRAIT}};
    for (const auto& k : portraits) {
        add(k.key, k.label, [](int kind) { return textureFilter(kind); }, [](int kind, int v) { textureSetFilter(kind, v); },
            0, appearanceNames, 3)
            .arg = k.kind;
    }
    addText("portrait_set", N_("Dialogue portrait art"), [] { return portraitPackSelected(); },
            [](const char* v) { portraitPackSelect(v); }, "original");
    addText("menu_portrait_set", N_("Menu portrait art"), [] { return portraitMenuPackSelected(); },
            [](const char* v) { portraitMenuPackSelect(v); }, "original");
    addText("battle_portrait_set", N_("Battle portrait art"), [] { return portraitBattlePackSelected(); },
            [](const char* v) { portraitBattlePackSelect(v); }, "original");
    add("text_smooth", N_("Text edges"), [](int) { return textureGlyphSmooth() != 0 ? 1 : 0; },
        [](int, int v) { textureSetGlyphSmooth(v); }, 1);
    addText("present_shader", N_("Shader"),
            [] {
                shaderChainEdit(0);
                return shaderChainPath();
            },
            [](const char* v) {
                shaderChainEdit(0);
                shaderChainSetPath(v);
            },
            "");
    static const char* const sizes[] = {N_("Game size"), N_("Compact")};
    add("menu_text", N_("Menu text"), [](int) { return skin::textSize() == skin::TEXT_GAME ? 0 : 1; },
        [](int, int v) { skin::setTextSize(v == 0 ? skin::TEXT_GAME : skin::TEXT_COMPACT); }, 1, sizes);
    add("menu_scale", N_("Menu size"), [](int) { return skin::scalePercent(); },
        [](int, int v) { skin::setScalePercent(v); }, skin::SCALE_DEFAULT, nullptr)
        .unit = "%";
    add("menu_dark", N_("Dark mode"), [](int) { return skin::darkWindows() ? 1 : 0; },
        [](int, int v) { skin::setDarkWindows(v != 0); }, 0);
    // Packs
    add("movie_replacements", N_("Pack movies"), [](int) { return moviePackEnabled(); },
        [](int, int v) { moviePackSetEnabled(v); }, 1);
    add("sprite_preload", N_("Sprite preload"),
        [](int) {
            const char* v = settingsGet("sprite_preload");
            return v != NULL && strcmp(v, "0") == 0 ? 0 : 1;
        },
        [](int, int v) {
            keySet("sprite_preload", v);
            texturePackReload(); // read again with the packs
        },
        1);
    add("texture_pack_gpu_mb", N_("Pack GPU memory"), [](int) { return keyInt("texture_pack_gpu_mb", 512); },
        [](int, int v) { keySet("texture_pack_gpu_mb", v); }, 512, gpuMbNames, 5, gpuMbValues)
        .mine = false;
    // Audio
    add("volume", N_("Volume"), [](int) { return keyInt("volume", 100); },
        [](int, int v) {
            settingsSetInt("volume", v);
            spuOutputApplySettings();
            settingsSave();
        },
        100, nullptr)
        .unit = "%";
    add("audio_latency", N_("Latency"), [](int) { return keyInt("audio_latency", 40); },
        [](int, int v) {
            settingsSetInt("audio_latency", v);
            spuOutputApplySettings();
            settingsSave();
        },
        40, nullptr)
        .unit = " ms";
    add("audio_latency_max", N_("Latency ceiling"), [](int) { return keyInt("audio_latency_max", 150); },
        [](int, int v) {
            settingsSetInt("audio_latency_max", v);
            spuOutputApplySettings();
            settingsSave();
        },
        150, nullptr)
        .unit = " ms";
    add("japanese_battle_voices", N_("Battle voices"), [](int) { return settingsBool("japanese_battle_voices", 0); },
        [](int, int v) { regionalSetBattle(v); }, 0);
    add("japanese_cutscene_audio", N_("Cutscene audio"), [](int) { return regionalMovieEnabled(); },
        [](int, int v) { regionalSetMovie(v); }, 0);
    static const char* const subtitles[] = {N_("Off"), N_("US dialogue"), N_("Japanese translation")};
    add("cutscene_subtitles", N_("Subtitles"), [](int) { return regionalSubtitlesEnabled(); },
        [](int, int v) { regionalSetSubtitles(v); }, 0, subtitles, 3);
    // Controls
    add("jp_controls_us", N_("Japanese controls in the US version"), [](int) { return padRegionalControls(0); },
        [](int, int v) { padSetRegionalControls(0, v); }, 0);
    add("us_controls_jp", N_("US controls in the Japanese version"), [](int) { return padRegionalControls(1); },
        [](int, int v) { padSetRegionalControls(1, v); }, 0);
    add("pad_stick_dpad", N_("Left stick drives the D-pad"), [](int) { return padStickDpad(); },
        [](int, int v) { padSetStickDpad(v); }, 1);
    add("pad_stick_deadzone", N_("Stick dead zone"), [](int) { return padStickDeadzone(); },
        [](int, int v) { padSetStickDeadzone(v); }, 50, nullptr)
        .unit = "%";
    add("mouse_game", N_("Mouse in the game"), [](int) { return mouseGameEnabled(); },
        [](int, int v) { keySet("mouse_game", v); }, 1);
    add("mouse_point", N_("Click menu entries"), [](int) { return mousePointEnabled(); },
        [](int, int v) { keySet("mouse_point", v); }, 1);
    add("mouse_target", N_("Click battle targets"), [](int) { return mouseTargetEnabled(); },
        [](int, int v) { keySet("mouse_target", v); }, 0);
    add("mouse_camera", N_("Drag to turn the camera"), [](int) { return mouseCameraEnabled(); },
        [](int, int v) { keySet("mouse_camera", v); }, 0);
    add("free_camera", N_("Free camera in fields"), [](int) { return pcFreeCameraEnabled(); },
        [](int, int v) { pcFreeCameraSet(v); }, 0);
    static const char* const vibration[] = {N_("Off"), N_("The game's"), N_("Enhanced")};
    add("vibration", N_("Vibration"), [](int) { return rumbleMode(); },
        [](int, int v) {
            rumbleSetMode(v);
            settingsSave();
        },
        RUMBLE_GAME, vibration, 3);
    add("vibration_strength", N_("Vibration strength"), [](int) { return rumbleStrength(); },
        [](int, int v) {
            rumbleSetStrength(v);
            settingsSave();
        },
        100, nullptr)
        .unit = "%";
    // Gameplay
    add("speedup", N_("Speedup"), [](int) { return speedupEnabled() != 0 ? 1 : 0; },
        [](int, int v) { speedupSetEnabled(v); }, 0);
    add("field60", N_("60 Hz fields"), [](int) { return pcGameplayField60(); },
        [](int, int v) { pcGameplaySetField60(v); }, 0)
        .def = [](int) { return engineThis() == ENGINE_ENHANCED ? 1 : 0; };
    add("worldmap60", N_("60 Hz world map"), [](int) { return pcGameplayWorldmap60(); },
        [](int, int v) { pcGameplaySetWorldmap60(v); }, 0)
        .def = [](int) { return engineThis() == ENGINE_ENHANCED ? 1 : 0; };
    add("battle60", N_("60 Hz battles"), [](int) { return pcGameplayBattle60(); },
        [](int, int v) { pcGameplaySetBattle60(v); }, 0)
        .def = [](int) { return engineThis() == ENGINE_ENHANCED ? 1 : 0; };
    add("field60slow", N_("20 and 15 fps rooms"),
        [](int) { return engineThis() == ENGINE_ENHANCED ? settingsBool("field60slow", 1) : 1; },
        [](int, int v) { keySet("field60slow", v); }, 1);
    add("interpolation", N_("Motion smoothing"), [](int) { return interpolationEnabled(); },
        [](int, int v) { interpolationSetEnabled(v); }, 1);
    add("interpolation_hz", N_("Motion display rate"), [](int) { return presentationRate(); },
        [](int, int v) { presentationSetRate(v); }, 60, rateNames, 5, rateValues);
    add("original_bug_fixes", N_("Original-game bug fixes"), [](int) { return pcGameplayOriginalBugFixes(); },
        [](int, int v) { pcGameplaySetOriginalBugFixes(v); }, 0);
    static const char* const difficulty[] = {N_("Normal"), N_("Hard")};
    add("difficulty", N_("Difficulty"), [](int) { return pcGameplayHardMode(); },
        [](int, int v) { pcGameplaySetHardMode(v); }, 0, difficulty);
    static const char* const statNames[] = {N_("Enemy HP"),      N_("Enemy attack"),        N_("Enemy ether"),
                                            N_("Enemy defence"), N_("Enemy ether defence"), N_("Enemy agility")};
    for (int stat = 0; stat < GAMEPLAY_ENEMY_STAT_COUNT && stat < 6; stat++) {
        Spec& s = add(pcGameplayEnemyKey(stat), statNames[stat], [](int a) { return pcGameplayEnemyMultiplier(a); },
                      [](int a, int v) { pcGameplaySetEnemyMultiplier(a, v); }, 100, nullptr);
        s.arg = stat;
        s.unit = "%";
    }
    add("encounter_rate", N_("Random encounters"), [](int) { return pcGameplayEncounterRate(); },
        [](int, int v) { pcGameplaySetEncounterRate(v); }, 100, nullptr)
        .unit = "%";
    static const char* const rewardNames[] = {N_("Random EXP"), N_("Random gold"), N_("Scripted EXP"),
                                              N_("Scripted gold")};
    for (int reward = 0; reward < GAMEPLAY_REWARD_COUNT && reward < 4; reward++) {
        Spec& s = add(pcGameplayRewardKey(reward), rewardNames[reward], [](int a) { return pcGameplayRewardMultiplier(a); },
                      [](int a, int v) { pcGameplaySetRewardMultiplier(a, v); }, 100, nullptr);
        s.arg = reward;
        s.unit = "%";
    }
    add("deathblow_learning", N_("Deathblow learning"), [](int) { return pcGameplayDeathblowLearning(); },
        [](int, int v) { pcGameplaySetDeathblowLearning(v); }, 100, nullptr)
        .unit = "%";
    static const char* const enemyInfo[] = {N_("Off"), N_("Always"), N_("HP once beaten")};
    add("enemy_info", N_("Enemy info"), [](int) { return pcGameplayEnemyInfoMode(); },
        [](int, int v) { pcGameplaySetEnemyInfoMode(v); }, 0, enemyInfo, 3);
    add("battle_retry", N_("Retry lost battles"), [](int) { return pcGameplayBattleRetry(); },
        [](int, int v) { pcGameplaySetBattleRetry(v); }, 0);
    add("text_speed", N_("Text speed"), [](int) { return pcGameplayTextSpeed(); },
        [](int, int v) { pcGameplaySetTextSpeed(v); }, 1, speedNames, 6, speedValues);
    add("text_hold_fast", N_("Hold to show text"), [](int) { return pcGameplayTextHold(); },
        [](int, int v) { pcGameplaySetTextHold(v); }, 0);
    add("text_auto_advance", N_("Auto-advance text"), [](int) { return pcGameplayTextAutoAdvance(); },
        [](int, int v) { pcGameplaySetTextAutoAdvance(v); }, 0, autoNames, 6, autoValues);
    // Saves
    add("rewind", N_("Rewind"),
        [](int) {
            RewindStats s;
            rewindStats(&s);
            return s.on != 0 ? 1 : 0;
        },
        [](int, int v) { rewindSetOn(v); }, 0);
    static const char* const secondNames[] = {"10 s", "30 s", "60 s", "120 s"};
    static const int secondValues[] = {10, 30, 60, 120};
    add("rewind_seconds", N_("How far back"),
        [](int) {
            RewindStats s;
            rewindStats(&s);
            return s.seconds;
        },
        [](int, int v) { rewindSetSeconds(v); }, 30, secondNames, 4, secondValues);
    static const char* const intervalNames[] = {"1", "2", "4"};
    static const int intervalValues[] = {1, 2, 4};
    add("rewind_interval", N_("Capture every N frames"),
        [](int) {
            RewindStats s;
            rewindStats(&s);
            return s.interval;
        },
        [](int, int v) { rewindSetInterval(v); }, 2, intervalNames, 3, intervalValues);
    add("rewind_budget", N_("Rewind memory"),
        [](int) {
            RewindStats s;
            rewindStats(&s);
            return s.budget;
        },
        [](int, int v) { rewindSetBudget(v); }, 256, budgetNames, 4, budgetValues);
    add("rewind_lanes", N_("Compare threads"),
        [](int) {
            RewindStats s;
            rewindStats(&s);
            return s.lanes;
        },
        [](int, int v) { rewindSetLanes(v); }, 4, intervalNames, 3, intervalValues)
        .mine = false;
    add("disc_swap", N_("Automatic disc swap"), [](int) { return cdSwapAutomatic(); },
        [](int, int v) { cdSwapSetAutomatic(v); }, 1);
    // System
    static const char* const engines[] = {N_("Original"), N_("Enhanced")};
    add("engine", N_("Engine"), [](int) { return engineSetting() >= 0 ? engineSetting() : engineThis(); },
        [](int, int v) { engineSetSetting(v); }, 0, engines)
        .def = [](int) { return engineThis(); };
    add("pause_minimized", N_("Pause when minimised"), [](int) { return settingsBool("pause_minimized", 0); },
        [](int, int v) { keySet("pause_minimized", v); }, 0);
    return t;
}

const Spec* findSpec(const char* key, size_t n = std::string::npos) {
    std::string k = n == std::string::npos ? std::string(key) : std::string(key, n);
    for (const Spec& s : table()) {
        if (s.key == k) return &s;
    }
    return nullptr;
}

// The known settings among a row's keys (separated by spaces).
std::vector<const Spec*> specsOf(const char* keys) {
    std::vector<const Spec*> out;
    for (const char* p = keys; p != nullptr && *p != '\0';) {
        while (*p == ' ') p++;
        const char* end = p;
        while (*end != '\0' && *end != ' ') end++;
        if (end > p) {
            const Spec* s = findSpec(p, (size_t)(end - p));
            if (s != nullptr) out.push_back(s);
        }
        p = end;
    }
    return out;
}

bool anyChanged(const std::vector<const Spec*>& specs) {
    for (const Spec* s : specs) {
        if (s->isChanged()) return true;
    }
    return false;
}

void restoreAll(const std::vector<const Spec*>& specs) {
    for (const Spec* s : specs) {
        if (s->isChanged()) {
            fprintf(stderr, "ui: reset %s (%s -> %s)\n", s->key.c_str(), s->showNow().c_str(), s->showDefault().c_str());
            s->restore();
        }
    }
    settingsSave();
}

// ---- pages, groups and rows --------------------------------------------------------------

enum Mode { MODE_NORMAL, MODE_FILTER, MODE_INDEX };
Mode mode = MODE_NORMAL;

struct Page {
    bool active;
    int section;
    bool stackedPage, stacked;
    bool tableOpen;
    int tables;
    std::string group;
    int groupFlags;
    bool groupOpen;
    bool markPending; // the group's heading began a block: its first row joins it
    bool tail;        // the last row's own description lines are still to be written
    int tailFlags;
    int changedInGroup;
    std::string lastHeading; // the Search page: the section and group shown last
} pg;

struct RowRecord {
    float y;
    std::vector<const Spec*> specs;
};
std::vector<RowRecord> rowsNow;
int currentRow = -1; // the row the cursor (or the mouse) was on last frame
bool currentChanged; // and whether it differs from its default (the footer's reset hint)
std::map<std::string, bool> groupOpenState;   // section/group: a collapsed group opened
std::map<std::string, int> groupChangedCount; // section/group: its rows that differ, last drawn

// The Search page's index: each row's words.
struct Entry {
    int section;
    std::string group, label, text, keys;
};
std::vector<Entry> entries;
bool indexReady;
int indexLanguage = -1;

char query[128];
bool changedOnly, searchFocus;
int matches;
struct Target {
    bool set;
    int section;
    std::string group, label;
};
Target firstMatch;
struct {
    bool pending, scroll;
    int section;
    std::string group, label;
} jump;

std::string groupKey(int section, const std::string& group) {
    char s[16];
    snprintf(s, sizeof s, "%d/", section);
    return s + group;
}

void openTable() {
    if (pg.tableOpen) return;
    char id[32];
    snprintf(id, sizeof id, "##rows%d", pg.tables++);
    pg.tableOpen = ImGui::BeginTable(id, pg.stacked ? 1 : 2, ImGuiTableFlags_SizingStretchProp);
    if (!pg.tableOpen) return;
    if (!pg.stacked) ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 104.0f * scale());
    ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
}

void closeTable() {
    if (pg.tableOpen) ImGui::EndTable();
    pg.tableOpen = false;
}

// The last row's own lines, after its controls' (still in its cell and block).
void emitTail() {
    if (!pg.tail) return;
    pg.tail = false;
    if (mode == MODE_INDEX) return;
    if (pg.tailFlags & RESTART) hint("Takes effect the next time the game starts.");
    if ((pg.tailFlags & ENHANCED) && engineThis() != ENGINE_ENHANCED)
        hint("Only the Enhanced engine has this (System > Engine).");
}

bool lower(std::string& s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return true;
}

// Every word of the query in the row's words (ASCII case ignored).
bool matchesQuery(const Entry* e, const char* label) {
    std::string words = std::string(tr(label)) + "\n" + label;
    if (e != nullptr) words += "\n" + e->text + "\n" + e->keys + "\n" + tr(e->group.c_str()) + "\n" + e->group;
    lower(words);
    std::string q = query;
    lower(q);
    size_t at = 0;
    while (at < q.size()) {
        while (at < q.size() && q[at] == ' ') at++;
        size_t end = q.find(' ', at);
        if (end == std::string::npos) end = q.size();
        if (end > at && words.find(q.substr(at, end - at)) == std::string::npos) return false;
        at = end;
    }
    return true;
}

const Entry* entryFor(int section, const std::string& group, const char* label) {
    for (const Entry& e : entries) {
        if (e.section == section && e.label == label && e.group == group) return &e;
    }
    return nullptr;
}

// The Search page: the result's section and group above it, choosing it opens there.
void resultHeading(const char* label) {
    std::string heading = tr(sectionNames[pg.section]);
    if (!pg.group.empty()) heading = heading + " > " + tr(pg.group.c_str());
    if (heading == pg.lastHeading) return;
    closeTable();
    pg.lastHeading = heading;
    ImGui::Dummy(ImVec2(0, 4 * scale()));
    markBlock();
    ImGui::PushID(heading.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    if (skin::selectable(heading.c_str(), false)) {
        jump = {};
        jump.pending = true;
        jump.section = pg.section;
        jump.group = pg.group;
        jump.label = label;
        sfx::play(sfx::CHOOSE);
    }
    ImGui::PopStyleColor();
    ImGui::PopID();
    hint("Choose to open %s at this setting.", tr(sectionNames[pg.section]));
}

} // namespace

// ---- options.h -------------------------------------------------------------------------

bool indexing() { return mode == MODE_INDEX; }
bool filtering() { return mode == MODE_FILTER; }

void beginPage(const char* id, int section, bool stacked) {
    pg.active = true;
    pg.section = section;
    pg.stackedPage = stacked;
    pg.stacked = stacked || ImGui::GetContentRegionAvail().x < 244.0f * scale();
    pg.tableOpen = false;
    pg.tables = 0;
    pg.group.clear();
    pg.groupFlags = 0;
    pg.groupOpen = true;
    pg.markPending = false;
    pg.tail = false;
    pg.changedInGroup = 0;
    ImGui::PushID(id);
}

void endPage() {
    emitTail();
    closeTable();
    if (!pg.group.empty()) groupChangedCount[groupKey(pg.section, pg.group)] = pg.changedInGroup;
    pg.active = false;
    ImGui::PopID();
}

void group(const char* heading, int flags) {
    emitTail();
    closeTable();
    if (!pg.group.empty()) groupChangedCount[groupKey(pg.section, pg.group)] = pg.changedInGroup;
    pg.group = heading;
    pg.groupFlags = flags;
    pg.changedInGroup = 0;
    pg.stacked = pg.stackedPage || (flags & STACKED) || ImGui::GetContentRegionAvail().x < 244.0f * scale();
    std::string key = groupKey(pg.section, heading);
    if ((flags & COLLAPSED) && jump.scroll && jump.section == pg.section && jump.group == heading) {
        groupOpenState[key] = true; // a search result in it
    }
    pg.groupOpen = !(flags & COLLAPSED) || groupOpenState[key];
    pg.markPending = false;
    if (mode != MODE_NORMAL || heading[0] == '\0') return; // (no heading: what follows has its own)
    ImGui::Dummy(ImVec2(0, (pg.tables > 0 || ImGui::GetCursorPosY() > 8 * scale()) ? 6 * scale() : 0));
    if (flags & COLLAPSED) {
        // A line the cursor can choose: "+ Advanced (2 changed)".
        markBlock();
        int changedCount = groupChangedCount[key];
        char text[160];
        if (changedCount > 0) {
            snprintf(text, sizeof text, tr("%s %s (%d changed)"), pg.groupOpen ? "-" : "+", tr(heading), changedCount);
        } else {
            snprintf(text, sizeof text, "%s %s", pg.groupOpen ? "-" : "+", tr(heading));
        }
        ImGui::PushID(heading);
        if (changedCount > 0) ImGui::PushStyleColor(ImGuiCol_Text, skin::textWarning);
        bool chosen = skin::selectable(text, false);
        if (changedCount > 0) ImGui::PopStyleColor();
        ImGui::PopID();
        if (chosen) {
            groupOpenState[key] = !pg.groupOpen;
            pg.groupOpen = !pg.groupOpen;
        }
        hint(pg.groupOpen ? "Choose to hide these settings again." : "Settings most players never need: choose to show them.");
    } else {
        markBlock();
        pg.markPending = true; // the heading shows with its first row
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
        ImGui::SeparatorText(tr(heading));
        ImGui::PopStyleColor();
    }
}

bool row(const char* label, const char* keys, int flags) {
    emitTail();
    if (!pg.active) return true; // outside a page (no table): the caller lays it out
    std::vector<const Spec*> specs = specsOf(keys);
    bool isChanged = mode != MODE_INDEX && anyChanged(specs);
    if (isChanged) pg.changedInGroup++;
    bool shown;
    if (mode == MODE_INDEX) {
        Entry e;
        e.section = pg.section;
        e.group = pg.group;
        e.label = label;
        e.keys = keys != nullptr ? keys : "";
        entries.push_back(e);
        shown = true;
    } else if (mode == MODE_FILTER) {
        shown = (!changedOnly || isChanged) && (query[0] == '\0' || matchesQuery(entryFor(pg.section, pg.group, label), label));
        if (shown) {
            matches++;
            if (!firstMatch.set) firstMatch = {true, pg.section, pg.group, label};
            resultHeading(label);
        }
    } else {
        shown = pg.groupOpen;
    }
    if (!shown) return false;
    openTable();
    if (!pg.tableOpen) return false;
    // A row's lines 16 apart, and the next row's first 16 below its last:
    // 3 under what the row holds (ImGui's tables put nothing between rows).
    if (ImGui::TableGetRowIndex() >= 0) ImGui::Dummy(ImVec2(0, 0));
    ImGui::TableNextRow(0, ROW * scale());
    ImGui::TableSetColumnIndex(0);
    if (!pg.markPending) markBlock();
    pg.markPending = false;
    if (mode != MODE_INDEX) rowsNow.push_back({ImGui::GetCursorScreenPos().y, specs});
    bool target = mode == MODE_NORMAL && jump.scroll && jump.section == pg.section && jump.label == label;
    if (target) {
        ImGui::SetScrollHereY(0.25f);
        jump.scroll = false;
    }
    if (!pg.stacked) ImGui::AlignTextToFramePadding();
    if (isChanged) ImGui::PushStyleColor(ImGuiCol_Text, skin::textWarning);
    textLines(label);
    if (isChanged) ImGui::PopStyleColor();
    // The badges.
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    if (flags & RESTART) textLines("After restart");
    if (flags & ENHANCED) textLines("Enhanced only");
    ImGui::PopStyleColor();
    if (!pg.stacked) ImGui::TableSetColumnIndex(1);
    if (isChanged) {
        // The description's first line: what it was, and how to get it back.
        std::string defaults;
        for (const Spec* s : specs) {
            if (!s->isChanged()) continue;
            if (!defaults.empty()) defaults += ", ";
            std::string d = s->showDefault();
            defaults += d.empty() ? std::string(tr("none")) : d;
        }
        hint("Changed from the default (%s): %s resets it.", defaults.c_str(), resetKeysText());
    }
    if (target) ImGui::SetKeyboardFocusHere(); // the row's control gets the cursor
    pg.tail = true;
    pg.tailFlags = flags;
    return true;
}

bool plain() {
    emitTail();
    closeTable();
    return mode == MODE_NORMAL && pg.groupOpen;
}

void noteHint(const char* text) {
    if (mode == MODE_INDEX && !entries.empty() && entries.back().section == pg.section) {
        if (!entries.back().text.empty()) entries.back().text += "\n";
        entries.back().text += text;
    }
}

bool cursorRowChanged() {
    return currentChanged;
}

bool finishPage(float cursorY, bool resetWanted) {
    bool done = false;
    currentRow = -1;
    for (int i = 0; cursorY != -FLT_MAX && i < (int)rowsNow.size() && rowsNow[i].y <= cursorY; i++) currentRow = i;
    currentChanged = currentRow >= 0 && anyChanged(rowsNow[currentRow].specs);
    if (resetWanted && cursorY != -FLT_MAX) {
        const RowRecord* on = nullptr;
        for (const RowRecord& r : rowsNow) {
            if (r.y > cursorY) break;
            on = &r;
        }
        if (on != nullptr && anyChanged(on->specs)) {
            restoreAll(on->specs);
            sfx::play(sfx::CHOOSE);
            done = true;
        } else if (on != nullptr) {
            sfx::play(sfx::BUZZ); // nothing to reset there
        }
    }
    rowsNow.clear();
    return done;
}

bool takeJump(int* section) {
    if (!jump.pending) return false;
    jump.pending = false;
    jump.scroll = true;
    *section = jump.section;
    return true;
}

void focusSearch() { searchFocus = true; }

void opened() {
    indexReady = false;
    rowsNow.clear();
    jump = {};
}

bool changed(const char* key) {
    const Spec* s = findSpec(key);
    return s != nullptr && s->isChanged();
}

void reset(const char* key) {
    const Spec* s = findSpec(key);
    if (s != nullptr) s->restore();
}

void searchPage(void (*drawPage)(int section), int sections) {
    // The box and the filter: rows of their own, before the results.
    if (searchFocus) {
        ImGui::SetKeyboardFocusHere();
        searchFocus = false;
    }
    markBlock();
    ImGui::SetNextItemWidth(-1);
    bool enter = ImGui::InputTextWithHint("##query", tr("Search the settings"), query, sizeof query,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    hint("Words of a setting's name or description (all of them). Enter opens the first match; choosing a "
         "result's heading opens its section there. Ctrl+F comes here from any section.");
    markBlock();
    ImGui::AlignTextToFramePadding();
    textLines("Show");
    static const char* const shows[] = {N_("Every setting"), N_("Only changed")};
    ImGui::SameLine(0, 14 * scale());
    for (int i = 0; i < 2; i++) {
        if (i > 0) ImGui::SameLine(0, 14 * scale());
        ImGui::PushID(i);
        if (skin::selectable(tr(shows[i]), changedOnly == (i == 1), 0, ImGui::CalcTextSize(tr(shows[i])))) {
            changedOnly = i == 1;
        }
        ImGui::PopID();
    }
    hint("Only changed: the settings that differ from their defaults, wherever they are. "
         "%s on a setting resets it.", resetKeysText());
    if (query[0] == '\0' && !changedOnly) {
        ImGui::Spacing();
        hint("Type in the box (a keyboard), or choose Only changed.");
        return;
    }
    // The index of every row's words, made invisibly (once per opening, and
    // again when the language changes).
    if (query[0] != '\0' && (!indexReady || indexLanguage != l10nLanguage())) {
        float width = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextWindowPos(ImVec2(-30000.0f, -30000.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, 4000.0f), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
        ImGui::Begin("##settings index", nullptr,
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
        entries.clear();
        mode = MODE_INDEX;
        for (int s = 0; s < sections; s++) drawPage(s);
        mode = MODE_NORMAL;
        pg.active = false;
        ImGui::End();
        ImGui::PopStyleVar();
        indexReady = true;
        indexLanguage = l10nLanguage();
        fprintf(stderr, "ui: search index: %d rows\n", (int)entries.size());
    }
    matches = 0;
    firstMatch = {};
    mode = MODE_FILTER;
    for (int s = 0; s < sections; s++) {
        pg.lastHeading.clear();
        drawPage(s);
    }
    mode = MODE_NORMAL;
    pg.active = false;
    if (matches == 0) {
        ImGui::Spacing();
        markBlock();
        textLines(changedOnly && query[0] == '\0' ? "Every setting is at its default." : "No setting matches.");
    }
    if (enter && firstMatch.set) {
        jump = {};
        jump.pending = true;
        jump.section = firstMatch.section;
        jump.group = firstMatch.group;
        jump.label = firstMatch.label;
        sfx::play(sfx::CHOOSE);
    }
}

// ---- presets ---------------------------------------------------------------------------

namespace {

struct PresetValue {
    const char* key;
    int value;
    const char* text; // text-valued settings
};

struct Preset {
    const char* name;  // N_()
    const char* about; // N_()
    std::vector<PresetValue> values;
};

const std::vector<Preset>& presets() {
    static std::vector<Preset> p;
    if (!p.empty()) return p;
    p.push_back({N_("Faithful"),
                 N_("The game as the PlayStation showed it: the Original engine, the 4:3 TV picture in its original "
                    "framing, every texture, sprite, HUD, text and portrait in its original look (no packs, no "
                    "redraws), no pack movies, no shader, no motion smoothing and no Speedup."),
                 {{"engine", ENGINE_ORIGINAL, nullptr},
                  {"aspect", 0, "4:3tv"},
                  {"expanded_vertical", 0, nullptr},
                  {"texture_style_3d", 0, nullptr},
                  {"texture_style_sprite", 0, nullptr},
                  {"texture_style_hud", 0, nullptr},
                  {"texture_style_battle_hud", 0, nullptr},
                  {"texture_style_text", 0, nullptr},
                  {"texture_style_icon", 0, nullptr},
                  {"texture_filter_portrait", 0, nullptr},
                  {"texture_filter_menu_portrait", 0, nullptr},
                  {"texture_filter_battle_portrait", 0, nullptr},
                  {"portrait_set", 0, "original"},
                  {"menu_portrait_set", 0, "original"},
                  {"battle_portrait_set", 0, "original"},
                  {"movie_replacements", 0, nullptr},
                  {"present_shader", 0, ""},
                  {"interpolation", 0, nullptr},
                  {"speedup", 0, nullptr}}});
    p.push_back({N_("Enhanced 60 Hz"),
                 N_("The Enhanced engine with every 60 Hz mode on: fields (with their 20 and 15 fps rooms), the "
                    "world map and battles."),
                 {{"engine", ENGINE_ENHANCED, nullptr},
                  {"field60", 1, nullptr},
                  {"field60slow", 1, nullptr},
                  {"worldmap60", 1, nullptr},
                  {"battle60", 1, nullptr}}});
    p.push_back({N_("Performance"),
                 N_("For integrated GPUs and handhelds: 2x resolution, the large surfaces at half of it, no "
                    "high-resolution pause blur (a CPU effect), no shader, motion shown at 60 Hz, and the game "
                    "thread sleeping between frames."),
                 {{"upscale", 2, nullptr},
                  {"upscale_large", 1, nullptr},
                  {"upscale_cpu_effects", 0, nullptr},
                  {"present_shader", 0, ""},
                  {"interpolation_hz", 60, nullptr},
                  {"vsync_wait", 0, nullptr}}});
    p.push_back({N_("Streaming"),
                 N_("More sound queued ahead (100 ms, growing up to 250 ms), so capture and streaming software "
                    "and busy machines do not make it crackle."),
                 {{"audio_latency", 100, nullptr}, {"audio_latency_max", 250, nullptr}}});
    return p;
}

// "Save my current setup": every known setting's value, key=value;... in settings.ini.
const char* const MINE_KEY = "preset_mine";

std::vector<std::pair<std::string, std::string>> mine() {
    std::vector<std::pair<std::string, std::string>> out;
    const char* v = settingsGet(MINE_KEY);
    if (v == nullptr) return out;
    std::string all = v;
    size_t at = 0;
    while (at < all.size()) {
        size_t end = all.find(';', at);
        if (end == std::string::npos) end = all.size();
        std::string item = all.substr(at, end - at);
        size_t eq = item.find('=');
        if (eq != std::string::npos) out.push_back({item.substr(0, eq), item.substr(eq + 1)});
        at = end + 1;
    }
    return out;
}

int saveMine() {
    std::string all;
    int n = 0;
    for (const Spec& s : table()) {
        if (!s.mine) continue;
        std::string v;
        if (s.text()) {
            v = s.getText() ? s.getText() : "";
            if (v.find(';') != std::string::npos) continue;
        } else {
            v = std::to_string(s.value());
        }
        if (!all.empty()) all += ";";
        all += s.key + "=" + v;
        n++;
    }
    settingsDescribe(MINE_KEY, "preset_mine: F1 > System > Presets' My setup: key=value;... of the settings it sets");
    settingsSet(MINE_KEY, all.c_str());
    settingsSave();
    return n;
}

// A preset's changes: each setting it sets that differs now.
struct Change {
    const Spec* spec;
    int value;
    std::string text;
};

std::vector<Change> changesOf(int preset) {
    std::vector<Change> out;
    auto consider = [&](const Spec* s, int value, const char* text) {
        if (s == nullptr) return;
        if (s->text()) {
            const char* now = s->getText();
            if (strcmp(now ? now : "", text ? text : "") != 0) out.push_back({s, 0, text ? text : ""});
        } else if (s->value() != value) {
            out.push_back({s, value, ""});
        }
    };
    if (preset < (int)presets().size()) {
        for (const PresetValue& v : presets()[preset].values) consider(findSpec(v.key), v.value, v.text);
    } else {
        for (const auto& kv : mine()) {
            const Spec* s = findSpec(kv.first.c_str());
            if (s == nullptr) continue;
            consider(s, atoi(kv.second.c_str()), kv.second.c_str());
        }
    }
    return out;
}

int asking = -1;   // the preset whose confirmation is open
bool askWanted;
char presetMessage[200];
bool presetError;

bool beginModal(const char* name) {
    nextModal();
    bool open = ImGui::BeginPopupModal(name, NULL,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoTitleBar);
    modalStyleEnd();
    if (open) skin::frameWindow(true);
    return open;
}

} // namespace

void presetRows() {
    int count = (int)presets().size();
    bool haveMine = settingsGet(MINE_KEY) != nullptr && settingsGet(MINE_KEY)[0] != '\0';
    if (row("Presets")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * scale(), 6 * scale()));
        bool open = ImGui::BeginCombo("##preset", tr("Choose a preset..."));
        ImGui::PopStyleVar();
        if (open) {
            skin::frameWindow();
            for (int i = 0; i < count + (haveMine ? 1 : 0); i++) {
                if (ImGui::Selectable(tr(i < count ? presets()[i].name : "My setup"), false)) {
                    asking = i;
                    askWanted = true;
                }
            }
            ImGui::EndCombo();
        }
        for (int i = 0; i < count; i++) hint("%s: %s", tr(presets()[i].name), tr(presets()[i].about));
        hint("Each lists what it changes before it applies. Settings it does not name stay as they are.");
        if (presetMessage[0] != '\0') {
            ImGui::PushStyleColor(ImGuiCol_Text, presetError ? skin::textError : skin::textGood);
            textLines(presetMessage);
            ImGui::PopStyleColor();
        }
    }
    if (row("My setup", MINE_KEY)) {
        if (ImGui::Button(tr(haveMine ? "Save my current setup again" : "Save my current setup"))) {
            int n = saveMine();
            snprintf(presetMessage, sizeof presetMessage, tr("My setup saved: %d settings."), n);
            presetError = false;
            fprintf(stderr, "ui: my setup saved (%d settings)\n", n);
            sfx::play(sfx::CHOOSE);
        }
        hint("Keeps every setting the presets know as it is now (in settings.ini, preset_mine): the presets' list "
             "then has My setup, to come back to it.");
    }
    if (indexing()) return;
    closeTable();
    if (askWanted) {
        ImGui::OpenPopup("##presetask");
        askWanted = false;
    }
    if (beginModal("##presetask")) {
        std::vector<Change> changes = changesOf(asking);
        const char* name = tr(asking >= 0 && asking < count ? presets()[asking].name : "My setup");
        char title[160];
        if (changes.empty()) {
            snprintf(title, sizeof title, tr("%s: every setting it names is already so."), name);
        } else {
            snprintf(title, sizeof title, tr("Apply %s? It changes:"), name);
        }
        textLines(title);
        for (const Change& c : changes) {
            std::string to = c.spec->text() ? c.text : c.spec->show(c.value);
            char line[400];
            snprintf(line, sizeof line, "%s: %s -> %s%s", tr(c.spec->label), c.spec->showNow().c_str(),
                     to.empty() ? tr("none") : to.c_str(), c.spec->key == "engine" ? tr(" (at the next launch)") : "");
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
            textLines(line);
            ImGui::PopStyleColor();
        }
        ImGui::BeginDisabled(changes.empty());
        if (ImGui::Button(tr("Apply"))) {
            for (const Change& c : changes) {
                if (c.spec->text()) c.spec->setText(c.text.c_str());
                else c.spec->set(c.spec->arg, c.value);
                fprintf(stderr, "ui: preset %s: %s = %s\n", asking < count ? presets()[asking].name : "mine",
                        c.spec->key.c_str(), c.spec->text() ? c.text.c_str() : std::to_string(c.value).c_str());
            }
            settingsSave();
            bool engineChanged = false;
            for (const Change& c : changes) engineChanged |= c.spec->key == "engine";
            snprintf(presetMessage, sizeof presetMessage,
                     tr(engineChanged ? "%s applied: %d settings changed. The engine changes at the next launch "
                                        "(System > Engine can restart now)."
                                      : "%s applied: %d settings changed."),
                     name, (int)changes.size());
            presetError = false;
            sfx::play(sfx::CHOOSE);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            sfx::play(sfx::BACK);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace opt
} // namespace menu
