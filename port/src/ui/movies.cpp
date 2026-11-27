// F1 > Display > Movies from the pack (port/src/gpu/moviepack.h,
// xg-htn.35.10): a texture pack's pictures for the movies' frames, on or off,
// and what the last movie found.

#include <stdio.h>

#include "imgui.h"
#include "l10n.h"
#include "menu.h"
#include "options.h"
#include "skin.h"

extern "C" {
#include "gpu/gpu.h"
#include "gpu/av1video.h"
#include "gpu/moviepack.h"
}

namespace menu {

namespace {
bool offOnCombo(const char* id, bool* value) {
    static const char* const names[] = {N_("Off"), N_("On")};
    bool changed = false;
    if (skin::beginCombo(id, tr(names[*value ? 1 : 0]))) {
        for (int i = 0; i < 2; i++) {
            bool sel = (*value ? 1 : 0) == i;
            if (skin::comboItem(tr(names[i]), sel) && !sel) {
                *value = i == 1;
                changed = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        skin::endCombo();
    }
    return changed;
}
} // namespace

// The Packs section's rows (options.h).
void moviePackRows() {
    MoviePackStats s;
    moviePackStats(&s);
    if (opt::row("Pack movies", "movie_replacements")) {
        bool on = moviePackEnabled() != 0;
        if (offOnCombo("##movie_replacements", &on)) {
            moviePackSetEnabled(on);
        }
        if (s.disc > 0) {
            hint("Last movie: %s%s", s.message, s.active && s.w > 0 ? "" : ".");
            if (s.active && s.w > 0) {
                hint("%s, %dx%d: %llu shown, %llu frames without one, %llu read late (the original or the last picture "
                     "showed); %.1f ms a picture to read, worst %.1f",
                     s.format[0] != '\0' ? s.format : "pictures", s.w, s.h, (unsigned long long)s.shown,
                     (unsigned long long)s.missing, (unsigned long long)s.late, s.decodeMs, s.decodeMsMax);
                if (s.invalid > 0) {
                    hint("%llu unusable pictures.", (unsigned long long)s.invalid);
                }
            } else if (s.format[0] != '\0') {
                hint("%s.", s.format);
            }
        }
        hint("In the pack's %s/movies: d<disc>s<stream>/<frame>.png (PNG frames; uncompressed ones read fastest) or "
             "d<disc>s<stream>.webm (AV1%s). They show in place of the movie frames, frame for frame: the game plays its "
             "own movie (timing, music, voices). The field's movie backgrounds need upscaling. Discovery writes the "
             "original frames there.",
             texturePackVersion(), movieVideoAvailable() ? "" : ": not in this build");
        overrideNote("movie_replacements");
    }
    if (opt::row("Uncompressed discovery PNGs", "dump_png_uncompressed")) {
        bool stored = moviePackDumpStored() != 0;
        if (offOnCombo("##dump_png_uncompressed", &stored)) {
            moviePackSetDumpStored(stored);
        }
        if (s.dumped > 0) {
            hint("%llu movie frames written by discovery.", (unsigned long long)s.dumped);
        }
        hint("Discovery writes uncompressed PNGs for textures and movie frames (faster to read back).");
    }
}

} // namespace menu
