#ifndef PORT_CD_VERIFY_H
#define PORT_CD_VERIFY_H
#include <stddef.h>
typedef struct {
    int disc, sectors, entries;
    char region[3], boot[32];
} CdImageInfo;
/* Read-only identity and file-table checks on an independently opened image.
 * Empty second paths are allowed. Neither function changes the drive/settings. */
int cdVerifyImage(const char* path, CdImageInfo* info, char* why, size_t size);
int cdVerifyPair(const char* first, const char* other, const char* region, CdImageInfo info[2], char* why, size_t size);
int cdImageSibling(const char* path, char* out, size_t size);
#endif
