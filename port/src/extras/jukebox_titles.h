#ifndef PORT_EXTRAS_JUKEBOX_TITLES_H
#define PORT_EXTRAS_JUKEBOX_TITLES_H

#include <stdint.h>

namespace extras {
struct JukeboxTitle {
    uint64_t hash;
    int bytes, disc, track; // OST disc/track, independent of the game's music ID
    const char *english, *japanese;
};
// nullptr for unverified/non-OST cues, including modified copies of a score.
const JukeboxTitle* jukeboxTitle(uint64_t hash, int bytes);
}

#endif
