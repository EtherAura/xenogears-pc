// F1 > Display > Movies from the pack (port/src/gpu/moviepack.h,
// xg-htn.35.10): a texture pack's pictures for the movies' frames, on or off,
// and what the last movie found.

#include <stdio.h>

#include "imgui.h"
#include "menu.h"
#include "options.h"

extern "C" {
#include "gpu/gpu.h"
#include "gpu/av1video.h"
#include "gpu/moviepack.h"
}

namespace menu {

// The Packs section's row (options.h).
void moviePackRows() {
    if (!opt::row("Pack movies", "movie_replacements dump_png_uncompressed")) return;
    ImGui::PushID("moviepack");
    bool on = moviePackEnabled() != 0;
    if (ImGui::Checkbox("Show the pack's movie pictures", &on)) {
        moviePackSetEnabled(on);
    }
    MoviePackStats s;
    moviePackStats(&s);
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
    bool stored = moviePackDumpStored() != 0;
    if (ImGui::Checkbox("Discovery writes uncompressed PNGs (textures and movie frames)", &stored)) {
        moviePackSetDumpStored(stored);
    }
    if (s.dumped > 0) {
        hint("%llu movie frames written by discovery.", (unsigned long long)s.dumped);
    }
    hint("In the pack's %s/movies: d<disc>s<stream>/<frame>.png (PNG frames; uncompressed ones read fastest) or "
         "d<disc>s<stream>.webm (AV1%s). They show in place of the movie frames, frame for frame: the game plays its "
         "own movie (timing, music, voices). The field's movie backgrounds need upscaling. Discovery writes the "
         "original frames there.",
         texturePackVersion(), movieVideoAvailable() ? "" : ": not in this build");
    ImGui::PopID();
    overrideNote("movie_replacements");
}

} // namespace menu
