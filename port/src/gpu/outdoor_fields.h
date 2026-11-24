/* The fields that are outdoors, where cloud shadows fall (xg-rgcf.5.17).
 *
 * The game marks no field as indoors or outdoors, so this list was made by
 * looking: every field of the US release started once, standing still, its
 * picture judged by eye (and those that show nothing on disc 1 tried on disc
 * 2). A field whose scenery lies under the open sky by day is on it (a town's
 * streets, a forest, a desert, a ship's deck); a room, a cave, a cockpit, a
 * covered hall, a scene at night, a picture or a view of the sky alone is
 * not. Field numbers are the game's own (the field file's index; the Japanese
 * release's world map leads into the same fields). Correct a field by adding
 * or removing its number here; keep the list sorted.
 */
#ifndef OUTDOOR_FIELDS_H
#define OUTDOOR_FIELDS_H

static const unsigned short outdoorFields[] = {
    1, 15, 17, 22, 24, 35, 39, 46, 47, 48, 49, 50,
    51, 52, 53, 59, 61, 62, 63, 64, 65, 66, 67, 68,
    69, 70, 73, 93, 94, 101, 102, 103, 124, 125, 131, 132,
    133, 134, 161, 175, 178, 180, 181, 182, 183, 184, 185, 186,
    187, 188, 189, 190, 191, 192, 194, 195, 196, 198, 199, 234,
    277, 278, 280, 281, 285, 297, 308, 311, 316, 318, 319, 320,
    326, 333, 335, 402, 411, 421, 423, 424, 456, 473, 474, 483,
    652, 664, 672, 681, 700, 720, 729,
};

/* Whether field `field` is outdoors (a binary search of the list). */
static int fieldOutdoors(int field) {
    int lo = 0, hi = (int)(sizeof(outdoorFields) / sizeof(outdoorFields[0])) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (outdoorFields[mid] == field) return 1;
        if (outdoorFields[mid] < field) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

#endif
