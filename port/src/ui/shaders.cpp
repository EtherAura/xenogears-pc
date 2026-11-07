// F1 > Display > Shader and Movie shader (port/src/gpu/shaderchain.h,
// xg-htn.35.28 and 35.10): a RetroArch .slangp preset or .slang shader over
// the picture, and one for the movies (the picture's, none, or their own),
// each typed or picked, its state or error, and its parameters' sliders.

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "imgui.h"
#include "menu.h"
#include "options.h"
#include "skin.h"

extern "C" {
#include "gpu/shaderchain.h"
}

namespace menu {

namespace {

// Per chain (0 the picture's, 1 the movies'): the path typed, and SDL's file
// dialog's answer (on its own thread: the pick waits here).
struct PathField {
    char path[1024];
    bool read;
    char picked[1024];
    bool pickedReady, dialogOpen;
};
PathField fields[2];
SDL_Mutex* pickLock;

void SDLCALL onPick(void* user, const char* const* files, int filter) {
    (void)filter;
    PathField* f = (PathField*)user;
    SDL_LockMutex(pickLock);
    if (files != NULL && files[0] != NULL) {
        snprintf(f->picked, sizeof(f->picked), "%s", files[0]);
        f->pickedReady = true;
    }
    f->dialogOpen = false;
    SDL_UnlockMutex(pickLock);
}

const SDL_DialogFileFilter shaderFilters[] = {{"RetroArch shader presets", "slangp;slang"}, {"All files", "*"}};

// The path, Browse..., Use and (off) buttons of chain `which`; the path to
// use when one was chosen (NULL: none).
const char* pathRow(int which, const char* offLabel) {
    PathField* f = &fields[which];
    shaderChainEdit(which);
    if (!f->read) {
        const char* p = shaderChainPath();
        snprintf(f->path, sizeof(f->path), "%s", p);
        f->read = true;
    }
    float buttons = ImGui::CalcTextSize("Browse...").x + ImGui::CalcTextSize("Use").x +
                    ImGui::CalcTextSize(offLabel).x + ImGui::GetStyle().FramePadding.x * 6 +
                    ImGui::GetStyle().ItemSpacing.x * 3;
    ImGui::SetNextItemWidth(-buttons);
    bool use = ImGui::InputText("##path", f->path, sizeof(f->path), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    SDL_LockMutex(pickLock);
    bool open = f->dialogOpen;
    if (f->pickedReady) {
        memcpy(f->path, f->picked, sizeof(f->path)); /* same size, terminated */
        f->pickedReady = false;
        use = true;
    }
    SDL_UnlockMutex(pickLock);
    ImGui::BeginDisabled(open);
    if (ImGui::Button("Browse...")) {
        f->dialogOpen = true;
        SDL_ShowOpenFileDialog(onPick, f, window(), shaderFilters, 2, f->path[0] != '\0' ? f->path : NULL, false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    use |= ImGui::Button("Use");
    ImGui::SameLine();
    if (ImGui::Button(offLabel)) {
        f->path[0] = '\0';
        use = true;
    }
    return use ? f->path : NULL;
}

// The state of chain `which` (shaderChainEdit chosen), and its parameters' row.
void stateAndParams(int which, bool rowShown) {
    shaderChainEdit(which);
    if (!rowShown) {
        // (its state belongs to the chain's row, not shown)
    } else if (shaderChainError()[0] != '\0') {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        textLines(shaderChainError());
        ImGui::PopStyleColor();
    } else if (shaderChainPasses() > 0) {
        hint("%d pass%s. RetroArch .slangp presets and .slang shaders (not .glslp or .cgp).", shaderChainPasses(),
             shaderChainPasses() == 1 ? "" : "es");
    }
    int n = shaderChainParamCount();
    if (n == 0) {
        return;
    }
    if (!opt::row(which ? "Movie parameters" : "Parameters", which ? "present_movie_shader_params" : "present_shader_params")) {
        return;
    }
    ImGui::PushID(which ? "movieparams" : "shaderparams");
    for (int i = 0; i < n; i++) {
        const SlangParam* p = shaderChainParam(i);
        if (p == NULL || p->min >= p->max) {
            continue; /* a heading: RetroArch presets use 0-range ones as labels */
        }
        ImGui::PushID(i);
        float v = p->value;
        ImGui::SetNextItemWidth(-1);
        char format[160];
        snprintf(format, sizeof(format), "%s: %%.3g", p->desc[0] != '\0' ? p->desc : p->name);
        if (ImGui::SliderFloat("##p", &v, p->min, p->max, format)) {
            if (p->step > 0) {
                v = p->min + p->step * (float)(int)((v - p->min) / p->step + 0.5f);
            }
            shaderChainSetParam(i, v);
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Defaults")) {
        shaderChainResetParams();
    }
    ImGui::PopID();
}

} // namespace

// The Display section's rows (options.h).
void shaderPresetRows() {
    if (pickLock == NULL) {
        pickLock = SDL_CreateMutex();
    }
    bool available = shaderChainAvailable() != 0;

    bool shaderShown = opt::row("Shader", "present_shader");
    if (shaderShown) {
    ImGui::PushID("shader");
    ImGui::BeginDisabled(!available);
    const char* chosen = pathRow(0, "Off");
    if (chosen != NULL) {
        shaderChainEdit(0);
        shaderChainSetPath(chosen);
    }
    ImGui::EndDisabled();
    if (!available) {
        hint("This build has no shader compiler (Khronos glslang): presets cannot be used.");
    } else {
        shaderChainEdit(0);
        if (shaderChainPasses() == 0 && shaderChainError()[0] == '\0') {
            hint("A RetroArch .slangp preset or .slang shader over the picture (CRT, scalers, sharpening). Off: the "
                 "picture as it is.");
        }
    }
    ImGui::PopID();
    } // Shader
    if (available) {
        stateAndParams(0, shaderShown);
    }

    static bool ownChosen; // "Its own" picked, its preset not chosen yet
    static const char builtin[] = "builtin:movie-smooth.slangp";
    int shown = 0;
    bool movieShown = opt::row("Movie shader", "present_movie_shader");
    if (movieShown) {
    ImGui::PushID("movieshader");
    ImGui::BeginDisabled(!available);
    shaderChainEdit(1);
    int mode = shaderChainMovieMode();
    bool smooth = mode == 2 && strcmp(shaderChainPath(), builtin) == 0;
    shown = smooth ? 3 : mode == 2 || ownChosen ? 2 : mode;
    static const char* const modes[4] = {"As the picture", "None", "Its own", "Smooth (built in)"};
    static const int order[4] = {0, 1, 3, 2};
    for (int k = 0; k < 4; k++) {
        int i = order[k];
        if (k > 0) {
            ImGui::SameLine();
        }
        if (ImGui::RadioButton(modes[i], shown == i) && shown != i) {
            shaderChainEdit(1);
            ownChosen = i == 2;
            if (i < 2) {
                shaderChainSetPath(i == 0 ? "same" : "none");
            } else if (i == 3) {
                shaderChainSetPath(builtin);
            } else if (fields[1].path[0] != '\0' && strcmp(fields[1].path, builtin) != 0) {
                shaderChainSetPath(fields[1].path);
            } else {
                fields[1].path[0] = '\0';
                fields[1].read = true;
            }
            shown = i;
        }
    }
    if (shown == 2) {
        const char* movie = pathRow(1, "Clear");
        if (movie != NULL) {
            shaderChainEdit(1);
            shaderChainSetPath(movie[0] != '\0' ? movie : "none");
        }
    }
    ImGui::EndDisabled();
    hint("The movies (24-bit pictures; a pack's movie pictures too): the picture's shader, none, a preset of their "
         "own, or the built-in smoothing (the MDEC's block edges softened, a sharp Catmull-Rom upscale).");
    ImGui::PopID();
    } // Movie shader
    shaderChainEdit(1);
    if (available && shaderChainMovieMode() == 2) {
        stateAndParams(1, movieShown);
    }
}

} // namespace menu
