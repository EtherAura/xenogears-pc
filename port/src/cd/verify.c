#include "verify.h"
#include "cd_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static unsigned u16(const uint8_t* p) {
    return p[0] | (unsigned)p[1] << 8;
}
static uint32_t u32(const uint8_t* p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int error(char* why, size_t size, const char* text) {
    if (size)
        snprintf(why, size, "%s", text);
    return 0;
}
static int user(CdDisc* d, int lba, uint8_t* out) {
    uint8_t raw[CD_RAW_SECTOR];
    if (lba < 0 || lba >= discSectorCount(d) || !discRead(d, lba, raw) || raw[15] != 2 || (raw[18] & 0x20))
        return 0;
    memcpy(out, raw + 24, 2048);
    return 1;
}
static int boot(CdDisc* d, char out[32]) {
    uint8_t p[2048], dir[2048];
    if (!user(d, 16, p) || p[0] != 1 || memcmp(p + 1, "CD001", 5))
        return 0;
    uint32_t lba = u32(p + 158), bytes = u32(p + 166);
    if (lba >= (unsigned)discSectorCount(d) || bytes == 0 || bytes > 128 * 1024)
        return 0;
    for (uint32_t done = 0; done < bytes; done += 2048) {
        if (!user(d, (int)lba + (int)(done / 2048), dir))
            return 0;
        unsigned limit = bytes - done < 2048 ? bytes - done : 2048;
        for (unsigned at = 0; at < limit && dir[at]; at += dir[at]) {
            unsigned n = dir[at];
            if (n < 34 || n > limit - at || dir[at + 32] > n - 33)
                return 0;
            if (dir[at + 32] != 12 || memcmp(dir + at + 33, "SYSTEM.CNF;1", 12))
                continue;
            uint32_t file = u32(dir + at + 2), length = u32(dir + at + 10);
            if (file >= (unsigned)discSectorCount(d) || length == 0 || length > 2048 || !user(d, (int)file, p))
                return 0;
            char cnf[2049];
            memcpy(cnf, p, length);
            cnf[length] = 0;
            char* name = strstr(cnf, "cdrom:");
            if (!name)
                return 0;
            name += 6;
            while (*name == '\\' || *name == '/')
                name++;
            unsigned k = 0;
            while (name[k] && name[k] != ';' && name[k] != '\r' && name[k] != '\n' && k < 31) {
                out[k] = name[k];
                k++;
            }
            out[k] = 0;
            return k != 0;
        }
    }
    return 0;
}
int discIsJapanese(CdDisc* d) {
    char name[32];
    return d && boot(d,name) && !strncmp(name,"SLPS_011.",9);
}
int cdVerifyImage(const char* path, CdImageInfo* info, char* why, size_t size) {
    if (size)
        why[0] = 0;
    memset(info, 0, sizeof(*info));
    if (!path || !*path)
        return error(why, size, "Choose a Xenogears .cue image (or a raw 2352-byte .bin).");
    CdDisc* d = discOpen(path);
    if (!d)
        return error(why, size, discLastError());
    CdImageInfo found = {0};
    uint8_t id[2048], dirs[2048], table[32768];
    const char* problem = NULL;
    found.sectors = discSectorCount(d);
    if (!user(d, 23, id) || memcmp(id, "DS0", 3) || (id[3] != '1' && id[3] != '2') || memcmp(id + 4, "_XENOGEARS", 10))
        problem = "No Xenogears disc identity at sector 23. Choose a complete raw disc image.";
    else {
        found.disc = id[3] - '0';
        if (!boot(d, found.boot))
            problem = "Cannot read SYSTEM.CNF from the ISO directory. The image is incomplete or malformed.";
        else {
            const char* expectedUS = found.disc == 1 ? "SLUS_006.64" : "SLUS_006.69";
            const char* expectedJP = found.disc == 1 ? "SLPS_011.60" : "SLPS_011.61";
            if (!strcmp(found.boot, expectedUS))
                strcpy(found.region, "US");
            else if (!strcmp(found.boot, expectedJP))
                strcpy(found.region, "JP");
            else
                problem = "The boot executable does not match a supported US/JP Xenogears disc identity.";
        }
    }
    for (int i = 0; !problem && i < 16; i++)
        if (!user(d, 24 + i, table + 2048 * i))
            problem = "Cannot read the complete Xenogears file table.";
    if (!problem && !user(d, 40, dirs))
        problem = "Cannot read the Xenogears directory table.";
    if (!problem) {
        int count = 0;
        for (; count < 32768 / 7; count++) {
            const uint8_t* e = table + count * 7;
            unsigned sector = e[0] | (unsigned)e[1] << 8 | (unsigned)e[2] << 16;
            if (sector == 0xFFFFFF)
                break;
            int32_t bytes = (int32_t)u32(e + 3);
            if (bytes > 0 && (sector >= (unsigned)found.sectors ||
                              ((uint32_t)bytes + 2047u) / 2048u > (unsigned)found.sectors - sector)) {
                problem = "A file-table entry extends beyond the image. Check that all .cue BINARY files are complete.";
                break;
            }
        }
        found.entries = count;
        if (!problem &&
            (count == 0 || count == 32768 / 7 || u16(dirs) == 0xFFFF || u16(dirs + 120) != (unsigned)found.disc))
            problem = "Invalid Xenogears file/directory table or disc-number mismatch.";
        for (int i = 0; !problem && i < 60; i++) {
            unsigned entry = u16(dirs + 2 * i);
            if (entry != 0xFFFF && (entry == 0 || entry > (unsigned)count))
                problem = "A directory points outside the Xenogears file table.";
        }
    }
    discClose(d);
    if (problem)
        return error(why, size, problem);
    *info = found;
    return 1;
}
int cdVerifyPair(const char* first, const char* other, const char* region, CdImageInfo info[2], char* why,
                 size_t size) {
    memset(info, 0, 2 * sizeof(*info));
    if (!cdVerifyImage(first, &info[0], why, size))
        return 0;
    if (region && strcmp(region, info[0].region)) {
        if (size)
            snprintf(why, size, "This is a %s disc; this game build requires %s images.", info[0].region, region);
        return 0;
    }
    if (!other || !*other)
        return 1;
    if (!cdVerifyImage(other, &info[1], why, size))
        return 0;
    if (strcmp(info[0].region, info[1].region))
        return error(why, size, "The two images have different regions. Choose both discs from the same release.");
    if (info[0].disc == info[1].disc)
        return error(why, size, "Both images are the same disc number. Choose Disc 1 and Disc 2.");
    return 1;
}
int cdImageSibling(const char* path, char* out, size_t size) {
    size_t n = strlen(path);
    if (n + 1 > size)
        return 0;
    memcpy(out, path, n + 1);
    int changed = 0;
    for (char* p = out; (p = strstr(p, "Disc ")) != NULL; p += 5) {
        if (p[5] == '1' || p[5] == '2') {
            p[5] = p[5] == '1' ? '2' : '1';
            changed = 1;
        }
    }
    return changed;
}
