#ifndef PORT_CD_VERIFY_H
#define PORT_CD_VERIFY_H
#include <stddef.h>
typedef struct {
    int disc, sectors, entries;
    char region[3], boot[32];
    int atelierFrench; /* recognized text-source layout */
    int perfectWorks;  /* Perfect Works 0.11.4's full presets as a text source (slots past the image masked) */
} CdImageInfo;
/* Read-only identity and file-table checks on an independently opened image.
 * Empty second paths are allowed. Neither function changes the drive/settings. */
int cdVerifyImage(const char* path, CdImageInfo* info, char* why, size_t size);
int cdVerifyPair(const char* first, const char* other, const char* region, CdImageInfo info[2], char* why, size_t size);
int cdImageSibling(const char* path, char* out, size_t size);
/* A .cue of `region`'s Disc `disc` beside `image`: its folder, the folder
 * above and the folders in that (a folder per disc), the first in name
 * order that verifies. 1 and its path in `out`, else 0. */
int cdFindImageBeside(const char* image, const char* region, int disc, char* out, size_t size);
#endif
