/*
 * Stored-deflate PNGs (pngfast.h): written without compression, read
 * without inflating. A PNG is a signature, then chunks (length, type, data,
 * CRC-32); IHDR gives the size and pixel format, the IDATs together a zlib
 * stream (two header bytes, deflate blocks, an Adler-32), whose blocks here
 * are all stored ones (BTYPE 00: LEN, NLEN, LEN bytes). The raw data is one
 * filter-type byte and the filtered bytes of each row (filters 0 to 4).
 */

#include "pngfast.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system/settings.h"

static int dumpStoredRead, dumpStored;

int pngDumpStored(void) {
    if (!dumpStoredRead) {
        dumpStoredRead = 1;
        settingsDescribe("dump_png_uncompressed", "dump_png_uncompressed: discovery writes its PNGs (textures, movie "
                                                  "frames) uncompressed: faster to write and to read back");
        dumpStored = settingsBool("dump_png_uncompressed", 0);
    }
    return dumpStored;
}

void pngSetDumpStored(int on) {
    pngDumpStored();
    dumpStored = on != 0;
    settingsSet("dump_png_uncompressed", dumpStored ? "1" : "0");
}

static uint32_t crcTable[256];

static uint32_t crc32(uint32_t crc, const uint8_t* p, size_t n) {
    if (crcTable[1] == 0) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            crcTable[i] = c;
        }
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc = crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

static void be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int writeChunk(SDL_IOStream* io, const char* type, const uint8_t* data, size_t n) {
    uint8_t head[8], tail[4];
    be32(head, (uint32_t)n);
    memcpy(head + 4, type, 4);
    be32(tail, crc32(crc32(0, (const uint8_t*)type, 4), data, n));
    return SDL_WriteIO(io, head, 8) == 8 && (n == 0 || SDL_WriteIO(io, data, n) == n) && SDL_WriteIO(io, tail, 4) == 4;
}

int pngWriteStored(const char* path, int w, int h, int channels, const uint8_t* px, size_t pitch) {
    if (w <= 0 || h <= 0 || (channels != 3 && channels != 4)) {
        return 0;
    }
    size_t row = (size_t)w * channels + 1, raw = row * (size_t)h;
    size_t blocks = (raw + 65534) / 65535;
    size_t zlen = 2 + raw + blocks * 5 + 4;
    uint8_t* z = malloc(zlen);
    if (z == NULL) {
        return 0;
    }
    /* zlib header: deflate, 32 K window, no dictionary, fastest (FCHECK makes it a multiple of 31) */
    z[0] = 0x78;
    z[1] = 0x01;
    size_t o = 2, done = 0;
    uint32_t a = 1, b = 0;
    int y = 0;
    size_t inRow = 0; /* bytes of row y already written */
    while (done < raw) {
        size_t n = raw - done < 65535 ? raw - done : 65535;
        z[o++] = done + n == raw ? 1 : 0; /* BFINAL, BTYPE 00 */
        z[o++] = (uint8_t)n, z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n, z[o++] = (uint8_t)(~n >> 8);
        size_t left = n;
        while (left > 0) {
            const uint8_t* src;
            size_t take;
            static const uint8_t none = 0; /* filter type 0 */
            if (inRow == 0) {
                src = &none;
                take = 1;
            } else {
                src = px + (size_t)y * pitch + (inRow - 1);
                take = row - inRow < left ? row - inRow : left;
            }
            memcpy(z + o, src, take);
            for (size_t i = 0; i < take; i++) {
                a += src[i];
                if (a >= 65521) a -= 65521;
                b += a;
                if (b >= 65521) b -= 65521;
            }
            o += take;
            left -= take;
            inRow += take;
            if (inRow == row) {
                inRow = 0;
                y++;
            }
        }
        done += n;
    }
    be32(z + o, b << 16 | a);
    o += 4;
    SDL_IOStream* io = SDL_IOFromFile(path, "wb");
    if (io == NULL) {
        free(z);
        return 0;
    }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;
    ihdr[9] = channels == 4 ? 6 : 2;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    int ok = SDL_WriteIO(io, sig, 8) == 8 && writeChunk(io, "IHDR", ihdr, 13) && writeChunk(io, "IDAT", z, o) &&
             writeChunk(io, "IEND", NULL, 0);
    ok &= SDL_CloseIO(io);
    free(z);
    return ok;
}

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* gray 0: 8-bit RGB or RGBA out as RGBA; gray 1: 8-bit greyscale out as one byte a pixel. */
static uint8_t* decodeStored(const uint8_t* f, size_t size, int* w, int* h, int maxSide, uint64_t maxPixels,
                             int* other, int gray) {
    *other = 0;
    if (size < 8 + 25 || memcmp(f, "\x89PNG\r\n\x1a\n", 8) != 0 || get32(f + 8) != 13 ||
        memcmp(f + 12, "IHDR", 4) != 0) {
        return NULL;
    }
    uint32_t pw = get32(f + 16), ph = get32(f + 20);
    int depth = f[24], type = f[25], interlace = f[28];
    if (pw == 0 || ph == 0 || pw > (uint32_t)maxSide || ph > (uint32_t)maxSide || (uint64_t)pw * ph > maxPixels) {
        return NULL;
    }
    if (depth != 8 || (gray ? type != 0 : (type != 2 && type != 6)) || interlace != 0 || f[26] != 0 || f[27] != 0) {
        *other = 1; /* a PNG, of a kind for the general decoder */
        return NULL;
    }
    int ch = gray ? 1 : type == 6 ? 4 : 3;
    size_t row = (size_t)pw * ch + 1, rawLen = row * ph;
    uint8_t* raw = malloc(rawLen);
    if (raw == NULL) {
        return NULL;
    }
    /* Walk the IDATs as one zlib stream: its header, then stored blocks. */
    size_t pos = 8, got = 0;
    int zhead = 0, inBlock = 0, last = 0, ok = 1;
    size_t blockLeft = 0;
    uint8_t hdr[5];
    int hdrHave = 0;
    while (ok && pos + 12 <= size) {
        uint32_t n = get32(f + pos);
        const uint8_t* type4 = f + pos + 4;
        const uint8_t* d = f + pos + 8;
        if ((size_t)n > size - pos - 12) {
            ok = 0;
            break;
        }
        if (memcmp(type4, "IEND", 4) == 0) {
            break;
        }
        if (memcmp(type4, "IDAT", 4) == 0) {
            size_t i = 0;
            while (ok && i < n) {
                if (zhead < 2) {
                    hdr[zhead++] = d[i++];
                    if (zhead == 2 && ((hdr[0] & 0x0F) != 8 || (hdr[0] << 8 | hdr[1]) % 31 != 0 || (hdr[1] & 0x20))) {
                        ok = 0;
                    }
                    continue;
                }
                if (!inBlock) {
                    if (last) {
                        break; /* the Adler-32 follows */
                    }
                    hdr[hdrHave++] = d[i++];
                    if (hdrHave == 1 && (hdr[0] & 0x06) != 0) {
                        *other = 1; /* a compressed block: inflate needed */
                        ok = 0;
                        break;
                    }
                    if (hdrHave == 5) {
                        size_t len = hdr[1] | (size_t)hdr[2] << 8, nlen = hdr[3] | (size_t)hdr[4] << 8;
                        if ((len ^ 0xFFFF) != nlen) {
                            ok = 0;
                            break;
                        }
                        last = hdr[0] & 1;
                        blockLeft = len;
                        inBlock = len > 0;
                        hdrHave = 0;
                    }
                    continue;
                }
                size_t take = n - i < blockLeft ? n - i : blockLeft;
                if (take > rawLen - got) {
                    ok = 0;
                    break;
                }
                memcpy(raw + got, d + i, take);
                got += take;
                i += take;
                blockLeft -= take;
                if (blockLeft == 0) {
                    inBlock = 0;
                }
            }
        }
        pos += 12 + (size_t)n;
    }
    if (!ok || got != rawLen) {
        free(raw);
        return NULL;
    }
    /* Unfilter into RGBA (or the grey bytes). */
    uint8_t* out = malloc((size_t)pw * ph * (gray ? 1 : 4));
    uint8_t* prev = calloc(row, 1);
    uint8_t* cur = malloc(row);
    if (out == NULL || prev == NULL || cur == NULL) {
        free(out), free(prev), free(cur), free(raw);
        return NULL;
    }
    size_t bytes = row - 1;
    for (uint32_t y = 0; y < ph && ok; y++) {
        const uint8_t* r = raw + (size_t)y * row;
        int filter = r[0];
        memcpy(cur, r + 1, bytes);
        switch (filter) {
        case 0:
            break;
        case 1:
            for (size_t x = (size_t)ch; x < bytes; x++) cur[x] = (uint8_t)(cur[x] + cur[x - ch]);
            break;
        case 2:
            for (size_t x = 0; x < bytes; x++) cur[x] = (uint8_t)(cur[x] + prev[x]);
            break;
        case 3:
            for (size_t x = 0; x < bytes; x++)
                cur[x] = (uint8_t)(cur[x] + (((x >= (size_t)ch ? cur[x - ch] : 0) + prev[x]) >> 1));
            break;
        case 4:
            for (size_t x = 0; x < bytes; x++)
                cur[x] = (uint8_t)(cur[x] + paeth(x >= (size_t)ch ? cur[x - ch] : 0, prev[x],
                                                  x >= (size_t)ch ? prev[x - ch] : 0));
            break;
        default:
            ok = 0;
            break;
        }
        uint8_t* o = out + (size_t)y * pw * (gray ? 1 : 4);
        if (gray || ch == 4) {
            memcpy(o, cur, bytes);
        } else {
            for (uint32_t x = 0; x < pw; x++) {
                o[x * 4] = cur[x * 3], o[x * 4 + 1] = cur[x * 3 + 1], o[x * 4 + 2] = cur[x * 3 + 2], o[x * 4 + 3] = 255;
            }
        }
        uint8_t* t = prev;
        prev = cur;
        cur = t;
    }
    free(prev), free(cur), free(raw);
    if (!ok) {
        free(out);
        return NULL;
    }
    *w = (int)pw;
    *h = (int)ph;
    return out;
}

uint8_t* pngDecodeStored(const uint8_t* f, size_t size, int* w, int* h, int maxSide, uint64_t maxPixels, int* other) {
    return decodeStored(f, size, w, h, maxSide, maxPixels, other, 0);
}
uint8_t* pngDecodeStoredGray(const uint8_t* f, size_t size, int* w, int* h, int maxSide, uint64_t maxPixels,
                             int* other) {
    return decodeStored(f, size, w, h, maxSide, maxPixels, other, 1);
}

uint8_t* pngReadStored(const char* path, int* w, int* h, int maxSide, uint64_t maxPixels, int* other) {
    size_t n = 0;
    uint8_t* f = SDL_LoadFile(path, &n);
    *other = 0;
    if (f == NULL) {
        return NULL;
    }
    uint8_t* px = pngDecodeStored(f, n, w, h, maxSide, maxPixels, other);
    SDL_free(f);
    return px;
}
