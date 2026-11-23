#ifndef PORT_UPDATE_ARCHIVE_H
#define PORT_UPDATE_ARCHIVE_H

/* A release's archive, unpacked (xg-4oq.14.4): the Windows .zip and the Linux
 * .tar.gz the updater installs (packaging/windows_release.py and
 * linux_release.py write them), inflated by puff (port/third_party/puff).
 *
 * Both hold one folder (xenogears-pc-<version>-<platform>/) and in it the
 * release's files; what is read is each regular file's path inside that
 * folder, its bytes and, from a tar, its execute bit. Every file's checksum
 * is checked (the zip's CRC-32 of each file, the gzip trailer's of the whole
 * tar), and an archive is refused whole if any entry is not a plain file or
 * folder (a link, a device), names a path outside its folder (an absolute
 * one, "..", a backslash, a drive or a stream), is in another top folder, is
 * named twice, is encrypted, uses zip64 or a method other than stored or
 * deflate, or is larger than a release holds (UPDATE_ARCHIVE_MAX).
 *
 * The updater checks the archive against the signed manifest's SHA-512 before
 * it reads it: these checks are for a damaged file, not a hostile one. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UPDATE_ARCHIVE_MAX (512u << 20) /* bytes, packed or unpacked */
#define UPDATE_ARCHIVE_FILES 4096

typedef struct {
    const char* path;      /* inside the top folder, '/'-separated: "bin/xenogears", "SDL3.dll" */
    const uint8_t* data;   /* valid during the call only */
    size_t size;
    int executable;        /* a tar entry with its owner's execute bit; a zip entry never */
} UpdateArchiveFile;

/* Called for each regular file, in the archive's order: 0 stops the reading,
 * which then fails ("stopped", or the reason the visitor wrote to why). */
typedef int (*UpdateArchiveVisit)(void* user, const UpdateArchiveFile* file, char* why, size_t whySize);

/* An archive in memory (n bytes), a .zip or a .tar.gz by its first bytes:
 * every entry is checked first, then each file is visited. top gets the top
 * folder's name (without '/'). 1 when all went through; 0 with why. */
int updateArchiveRead(const uint8_t* bytes, size_t n, UpdateArchiveVisit visit, void* user, char* top,
                      size_t topSize, char* why, size_t whySize);

/* The same for a file. */
int updateArchiveReadFile(const char* path, UpdateArchiveVisit visit, void* user, char* top, size_t topSize,
                          char* why, size_t whySize);

/* CRC-32 (ISO 3309, as zip and gzip use it): crc of data continuing crc (0 to start). */
uint32_t updateCrc32(uint32_t crc, const uint8_t* data, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UPDATE_ARCHIVE_H */
