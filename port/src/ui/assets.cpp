// The settings menu's pictures and font from the player's disc (assets.h).

#include "assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <filesystem>
#include <vector>

#include "japanese_codes.h"
#include "jukebox_font.h"

extern "C" {
#include "cd/cd.h"
#include "system/gametext.h"
#include "cd/cd_internal.h"
#include "cd/verify.h"
#include "system/settings.h"
void uiGameFontColors(uint16_t* body, uint16_t* outline); // port/src/system/uidata.c
}

namespace assets {

namespace {

#define USER_OFFSET 24 // Mode 2 Form 1: sync, header, subheader
#define USER_SIZE 2048

// ---- the disc ------------------------------------------------------------------------------

bool readUser(int lba, uint8_t* out, int size, CdDisc* image = nullptr) {
    uint8_t raw[CD_RAW_SECTOR];
    for (int done = 0; done < size; done += USER_SIZE, lba++) {
        if (!(image ? discRead(image, lba, raw) : cdReadRawSector(lba, raw))) {
            return false;
        }
        memcpy(out + done, raw + USER_OFFSET, (size_t)(size - done < USER_SIZE ? size - done : USER_SIZE));
    }
    return true;
}

// File `file` (1-based) of directory `dir` through the disc's file table (16
// sectors from LBA 0x18: u24 LBA, s32 size) and directory index (LBA 0x28:
// one-based u16 first entries), whole sectors (LZSS streams may run into the
// padding). Freed by the caller.
uint8_t* readFile(int dir, int file, int* size, CdDisc* image = nullptr) {
    static uint8_t table[16 * USER_SIZE];
    static uint16_t dirs[USER_SIZE / 2];
    if (dir < 0 || dir >= 0x3c || file < 1 ||
        !readUser(0x18, table, sizeof(table), image) || !readUser(0x28, (uint8_t*)dirs, sizeof(dirs), image)) {
        return nullptr;
    }
    int entry = dirs[dir] - 1 + file - 1;
    if (dirs[dir] == 0 || dirs[dir] == 0xffff || entry < 0 || (entry + 1) * 7 > (int)sizeof(table)) {
        return nullptr;
    }
    const uint8_t* e = table + entry * 7;
    int lba = e[0] | e[1] << 8 | e[2] << 16;
    int32_t bytes;
    memcpy(&bytes, e + 3, 4);
    if (bytes <= 0 || bytes > 0x400000) {
        return nullptr;
    }
    int whole = (bytes + USER_SIZE - 1) / USER_SIZE * USER_SIZE;
    uint8_t* data = (uint8_t*)calloc(1, (size_t)whole);
    if (data == nullptr || !readUser(lba, data, whole, image)) {
        free(data);
        return nullptr;
    }
    *size = whole;
    return data;
}

// The game's LZSS (port/src/game/main/lzss.c): a u32 size, then groups of a
// flag byte (low bit first) and eight items, a literal or a 12-bit distance
// with a 4-bit length - 3. Stops at the end of the input too.
uint8_t* unlzss(const uint8_t* in, int inSize, int* outSize) {
    if (inSize < 4) {
        return nullptr;
    }
    uint32_t size;
    memcpy(&size, in, 4);
    if (size == 0 || size > 0x400000) {
        return nullptr;
    }
    uint8_t* out = (uint8_t*)calloc(1, size + 16);
    if (out == nullptr) {
        return nullptr;
    }
    uint32_t o = 0;
    int i = 4;
    while (o < size && i < inSize) {
        int flags = in[i++];
        for (int bit = 0; bit < 8 && o < size; bit++, flags >>= 1) {
            if (!(flags & 1)) {
                if (i >= inSize) {
                    break;
                }
                out[o++] = in[i++];
                continue;
            }
            if (i + 1 >= inSize) {
                break;
            }
            uint32_t back = in[i] | (in[i + 1] & 0xF) << 8;
            int n = (in[i + 1] >> 4) + 3;
            i += 2;
            for (; n > 0 && o < size; n--, o++) {
                out[o] = back <= o ? out[o - back] : 0;
            }
        }
    }
    *outSize = (int)size;
    return out;
}

// ---- the menu resource file -----------------------------------------------------------------

uint16_t* vram;     // 1024 x 512, the menu file's TIMs uploaded
uint8_t* sprites;   // the sprite table (bundle entry 3)
int spritesSize;
int spriteCount;
Image* spriteCache[2]; // [semi][id]

uint8_t* font;      // the dialogue font, decompressed
int fontSize;
int fontFirst;      // the first one-byte code (dialogFontVar1)
int fontTwoByte;    // codes from here on are the lead of two bytes (dialogFontVar0)
int fontNarrow;     // glyphs from fontFirst on that are 8 pixels wide (dialogFontVar2)
int fontGlyphs;     // offset of the glyphs (the header's u16 at +2)
uint8_t* japaneseFont; // independent JP image, never inserted into the drive
int japaneseFontSize;
bool done, ok;

uint16_t vramAt(int x, int y) {
    return vram[(y & 511) * 1024 + (x & 1023)];
}

void upload(int x, int y, int w, int h, const uint8_t* src) {
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            uint16_t v;
            memcpy(&v, src + (r * w + c) * 2, 2);
            vram[((y + r) & 511) * 1024 + ((x + c) & 1023)] = v;
        }
    }
}

// One TIM (magic 0x10; flags bit 3: a CLUT block first) into VRAM, as LoadImage would.
bool loadTim(const uint8_t* p, int size) {
    uint32_t head[2];
    if (size < 8) {
        return false;
    }
    memcpy(head, p, 8);
    if (head[0] != 0x10) {
        return false;
    }
    int at = 8;
    for (int block = (head[1] & 8) ? 0 : 1; block < 2; block++) {
        if (at + 12 > size) {
            return false;
        }
        uint32_t bytes;
        uint16_t r[4];
        memcpy(&bytes, p + at, 4);
        memcpy(r, p + at + 4, 8);
        if (at + 12 + r[2] * r[3] * 2 > size) {
            return false;
        }
        upload(r[0], r[1], r[2], r[3], p + at + 12);
        at += (int)bytes;
    }
    return true;
}

// The game's offset bundle: u32 count, then count u32 offsets from its start.
const uint8_t* bundleEntry(const uint8_t* b, int size, int n, int* left) {
    uint32_t count, off;
    memcpy(&count, b, 4);
    if ((uint32_t)n >= count || 4 + (n + 1) * 4 > size) {
        return nullptr;
    }
    memcpy(&off, b + 4 + n * 4, 4);
    if (off >= (uint32_t)size) {
        return nullptr;
    }
    *left = size - (int)off;
    return b + off;
}

bool loadMenuFile() {
    int size = 0;
    uint8_t* file = readFile(0x10, 1, &size);
    if (file == nullptr) {
        return false;
    }
    // loadMenuSharedResources: bundle[2] the images (an offset bundle of
    // TIMs, uploaded last to first), bundle[3] the sprite table.
    int left, imgSize = 0;
    const uint8_t* e = bundleEntry(file, size, 1, &left);
    uint8_t* images = e != nullptr ? unlzss(e, left, &imgSize) : nullptr;
    e = bundleEntry(file, size, 2, &left);
    sprites = e != nullptr ? unlzss(e, left, &spritesSize) : nullptr;
    free(file);
    if (images == nullptr || sprites == nullptr || spritesSize < 8) {
        free(images);
        return false;
    }
    vram = (uint16_t*)calloc(1024 * 512, 2);
    uint32_t count;
    memcpy(&count, images, 4);
    bool good = vram != nullptr && count > 0 && count < 256;
    for (int i = (int)count - 1; good && i >= 0; i--) {
        e = bundleEntry(images, imgSize, i, &left);
        good = e != nullptr && loadTim(e, left);
    }
    free(images);
    int32_t n;
    memcpy(&n, sprites, 4);
    spriteCount = n > 0 && 4 + n * 2 <= spritesSize ? n : 0;
    return good && spriteCount > 0;
}

bool loadFont() {
    int size = 0;
    uint8_t* file = readFile(1, 6, &size);
    if (file == nullptr) {
        return false;
    }
    font = unlzss(file, size, &fontSize);
    free(file);
    if (font == nullptr || fontSize < 14) {
        return false;
    }
    // initFont: +2 the glyphs' offset, +4 the first two-byte lead, +8 the
    // narrow glyphs' count, +12 the first one-byte code.
    uint16_t h[7];
    memcpy(h, font, sizeof(h));
    fontGlyphs = h[1];
    fontTwoByte = h[2];
    fontNarrow = h[4];
    fontFirst = h[6];
    return fontGlyphs < fontSize && fontFirst < fontTwoByte && fontTwoByte <= 0x100;
}

std::string pathUtf8(const std::filesystem::path& path) {
    auto bytes = path.u8string();
    return std::string(bytes.begin(), bytes.end()); // C++17 char / C++20 char8_t
}

std::filesystem::path fromUtf8(const char* text) {
#ifdef __cpp_char8_t
    return std::filesystem::path(reinterpret_cast<const char8_t*>(text));
#else
    return std::filesystem::u8path(text);
#endif
}

bool readJapaneseFont(const std::filesystem::path& path) {
    std::string name = pathUtf8(path);
    CdImageInfo info;
    char why[256];
    if (!cdVerifyImage(name.c_str(), &info, why, sizeof(why)) || strcmp(info.region, "JP")) return false;
    CdDisc* image = discOpen(name.c_str());
    if (!image) return false;
    int size = 0, bytes = 0;
    uint8_t* file = readFile(1, 6, &size, image);
    uint8_t* data = file ? unlzss(file, size, &bytes) : nullptr;
    free(file);
    discClose(image);
    uint16_t h[7] = {};
    if (data && bytes >= 14) memcpy(h, data, sizeof(h));
    // These locators describe the native JP dialogue font, not an arbitrary
    // image or a translated font with a different character order.
    if (bytes != 39310 || h[1] != 14 || h[2] != 248 || h[3] != 5104 || h[4] != 63 || h[6] != 16) {
        free(data);
        return false;
    }
    japaneseFont = data;
    japaneseFontSize = bytes;
    fprintf(stderr, "ui: Japanese dialogue font from %s\n", name.c_str());
    return true;
}

void loadJapaneseFont() {
    if (fontNarrow == 63) return; // already playing a Japanese disc
    settingsDescribe("menu_jp_disc", "menu_jp_disc: JP Xenogears image for native Japanese menu text; empty searches beside the game discs, none disables the search");
    const char* configured = settingsGet("menu_jp_disc");
    if (configured && configured[0]) {
        if (strcmp(configured, "none") && !readJapaneseFont(fromUtf8(configured)))
            fprintf(stderr, "ui: no native Japanese font in menu_jp_disc; using the pixel fallback\n");
        return;
    }
    // Read only the image's folder, its parent and neighboring image folders.
    // Sorting keeps discovery deterministic; errors and unrelated images are
    // ignored. Independent handles leave CD playback and the mounted pair alone.
    const char* current = cdSwapPath(0);
    if (!current || !current[0]) return;
    namespace fs = std::filesystem;
    fs::path folder = fromUtf8(current).parent_path();
    if (folder.empty()) folder = ".";
    std::vector<fs::path> folders{folder};
    fs::path parent = folder.parent_path();
    if (!parent.empty() && parent != folder) folders.push_back(parent);
    std::error_code ec;
    for (fs::directory_iterator it(parent.empty() ? folder : parent, ec), end; !ec && it != end; it.increment(ec)) {
        if (folders.size() >= 128) break;
        if (it->is_directory(ec)) folders.push_back(it->path());
    }
    std::sort(folders.begin(), folders.end());
    folders.erase(std::unique(folders.begin(), folders.end()), folders.end());
    std::vector<fs::path> images;
    for (const fs::path& dir : folders) {
        ec.clear();
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (images.size() >= 512) break;
            std::string ext = pathUtf8(it->path().extension());
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c; });
            if ((ext == ".cue" || ext == ".bin") && it->is_regular_file(ec)) images.push_back(it->path());
        }
    }
    std::sort(images.begin(), images.end());
    for (const fs::path& path : images) if (readJapaneseFont(path)) return;
}

// A texel: depth 0 4-bit, 1 8-bit (CLUT at cx, cy), 2 15-bit.
uint16_t texel(int depth, int tpx, int tpy, int u, int v, int cx, int cy) {
    u &= 0xFF;
    v &= 0xFF;
    if (depth == 0) {
        return vramAt(cx + ((vramAt(tpx + u / 4, tpy + v) >> ((u & 3) * 4)) & 15), cy);
    }
    if (depth == 1) {
        return vramAt(cx + ((vramAt(tpx + u / 2, tpy + v) >> ((u & 1) * 8)) & 255), cy);
    }
    return vramAt(tpx + u, tpy + v);
}

// A sprite table entry (Noah sFontGlyph): s16 piece count, u16, then 0x1C-byte
// pieces: u, v, w, h, x, y, 2 unknown, depth, CLUT x, y, page x, y, flip x, y.
struct Piece {
    int u, v, w, h, x, y, depth, cx, cy, tpx, tpy, fx, fy;
};

int pieces(int id, Piece* out, int max) {
    if (id < 0 || id >= spriteCount) {
        return 0;
    }
    uint16_t off;
    memcpy(&off, sprites + 4 + id * 2, 2);
    if (off + 4 > spritesSize) {
        return 0;
    }
    int16_t count;
    memcpy(&count, sprites + off, 2);
    int n = 0;
    for (int i = 0; i < count && n < max && off + 4 + (i + 1) * 0x1C <= spritesSize; i++) {
        const uint8_t* p = sprites + off + 4 + i * 0x1C;
        int16_t f[13];
        memcpy(f, p, 26);
        Piece& c = out[n++];
        c = {(uint16_t)f[0], f[1], f[2], f[3], f[4], f[5], f[8], f[9], f[10], f[11] & 0x3C0, f[12] & 0x100, p[0x1A], p[0x1B]};
    }
    return n;
}

} // namespace

uint8_t* readDiscFile(int dir, int file, int* size) {
    return readFile(dir, file, size);
}

uint32_t rgba(uint16_t c, bool semi) {
    if (c == 0) {
        return 0;
    }
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r = r << 3 | r >> 2;
    g = g << 3 | g >> 2;
    b = b << 3 | b >> 2;
    uint32_t a = semi && (c & 0x8000) ? 128 : 255;
    return r | g << 8 | b << 16 | a << 24;
}

bool load() {
    // Again after a failure: the first opening may have had no disc in the
    // drive (the setup screen before the first image, main.c).
    if (ok) {
        return true;
    }
    done = true;
    bool menu = loadMenuFile();
    bool text = loadFont();
    if (text) loadJapaneseFont();
    ok = menu && text;
    fprintf(stderr, "ui: the menu's pictures %s, its font %s (from the disc)\n", menu ? "read" : "not found",
            text ? "read" : "not found");
    return ok;
}

bool loaded() {
    return ok;
}

Image vramImage(int depth, int tpageX, int tpageY, int clutX, int clutY, int u, int v, int w, int h, bool semi) {
    Image im;
    im.w = w;
    im.h = h;
    im.rgba = (uint32_t*)calloc((size_t)(w * h), 4);
    for (int y = 0; vram != nullptr && im.rgba != nullptr && y < h; y++) {
        for (int x = 0; x < w; x++) {
            im.rgba[y * w + x] = rgba(texel(depth, tpageX, tpageY, u + x, v + y, clutX, clutY), semi);
        }
    }
    return im;
}

const Image* sprite(int id, bool semi) {
    if (!ok || id < 0 || id >= spriteCount) {
        return nullptr;
    }
    Image*& cache = spriteCache[semi ? 1 : 0];
    if (cache == nullptr) {
        cache = new Image[spriteCount];
    }
    Image& im = cache[id];
    if (im.rgba != nullptr) {
        return &im;
    }
    Piece p[16];
    int n = pieces(id, p, 16);
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
    for (int i = 0; i < n; i++) {
        x0 = p[i].x < x0 ? p[i].x : x0;
        y0 = p[i].y < y0 ? p[i].y : y0;
        x1 = p[i].x + p[i].w > x1 ? p[i].x + p[i].w : x1;
        y1 = p[i].y + p[i].h > y1 ? p[i].y + p[i].h : y1;
    }
    if (n == 0 || x1 <= x0 || y1 <= y0 || x1 - x0 > 512 || y1 - y0 > 512) {
        return nullptr;
    }
    im.w = x1 - x0;
    im.h = y1 - y0;
    im.ox = -x0;
    im.oy = -y0;
    im.rgba = (uint32_t*)calloc((size_t)(im.w * im.h), 4);
    // Each piece shows its texels, mirrored where it is flipped (the game
    // steps u or v back one so the mirrored quad samples the same texels).
    for (int i = 0; i < n && im.rgba != nullptr; i++) {
        for (int y = 0; y < p[i].h; y++) {
            for (int x = 0; x < p[i].w; x++) {
                int u = p[i].u + (p[i].fx ? p[i].w - 1 - x : x);
                int v = p[i].v + (p[i].fy ? p[i].h - 1 - y : y);
                uint16_t t = texel(p[i].depth, p[i].tpx, p[i].tpy, u, v, p[i].cx, p[i].cy);
                if (t != 0) {
                    im.rgba[(p[i].y - y0 + y) * im.w + (p[i].x - x0 + x)] = rgba(t, semi);
                }
            }
        }
    }
    return &im;
}

// Characters a font lacks, drawn here in its manner (5 pixels wide, capitals
// from row 1, small letters from row 3, the line's base on row 9): code
// 0x100 + their place in this list. The US font lacks the first ones; the
// Japanese font has fewer Latin letters, and gets the rest too.
const struct {
    uint32_t c;
    const char* rows[11];
} extra[] = {
    {0x2190, {"", "", "", "..#", ".#", "#####", ".#", "..#", "", "", ""}}, // arrows
    {0x2192, {"", "", "", "..#", "...#", "#####", "...#", "..#", "", "", ""}},
    {0x2191, {"", "..#", ".###", "#.#.#", "..#", "..#", "..#", "..#", "..#", "..#", ""}},
    {0x2193, {"", "..#", "..#", "..#", "..#", "..#", "..#", "#.#.#", ".###", "..#", ""}},
    {'_', {"", "", "", "", "", "", "", "", "", "", "#####"}},
    {'\\', {"#", "#", ".#", ".#", "..#", "..#", "..#", "...#", "...#", "....#", "....#"}},
    {'|', {"..#", "..#", "..#", "..#", "..#", "..#", "..#", "..#", "..#", "..#", "..#"}},
    {';', {"", "", "", ".##", ".##", "", "", ".##", ".##", "..#", ".#"}},
    {'*', {"", "", "..#", "#.#.#", ".###", "#.#.#", "..#", "", "", "", ""}},
    {'<', {"", "", "....#", "...#", "..#", ".#", "..#", "...#", "....#", "", ""}},
    {'>', {"", "", "#", ".#", "..#", "...#", "..#", ".#", "#", "", ""}},
    {'^', {"..#", ".#.#", "#...#", "", "", "", "", "", "", "", ""}},
    {'`', {".#", "..#", "", "", "", "", "", "", "", "", ""}},
    {'~', {"", "", "", "", ".#..#", "#.##", "", "", "", "", ""}},
    {'B', {"", "####", "#...#", "#...#", "####", "#...#", "#...#", "#...#", "#...#", "####", ""}},
    {'H', {"", "#...#", "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#", "#...#", ""}},
    {'J', {"", "....#", "....#", "....#", "....#", "....#", "....#", "#...#", "#...#", ".###", ""}},
    {'K', {"", "#...#", "#..#", "#.#", "##", "##", "#.#", "#..#", "#...#", "#...#", ""}},
    {'Q', {"", ".###", "#...#", "#...#", "#...#", "#...#", "#...#", "#.#.#", "#..#", ".##.#", ""}},
    {'W', {"", "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", "##.##", "##.##", "#...#", ""}},
    {'h', {"", "#", "#", "#.##", "##..#", "#...#", "#...#", "#...#", "#...#", "#...#", ""}},
    {'j', {"", "...#", "", "..##", "...#", "...#", "...#", "...#", "...#", "#..#", ".##"}},
    {'q', {"", "", "", ".####", "#...#", "#...#", "#...#", "#..##", ".##.#", "....#", "....#"}},
    {'v', {"", "", "", "#...#", "#...#", "#...#", ".#.#", ".#.#", "..#", "..#", ""}},
    {'z', {"", "", "", "#####", "....#", "...#", "..#", ".#", "#", "#####", ""}},
    {':', {"", "", "", ".##", ".##", "", "", ".##", ".##", "", ""}},
    {'\'', {"", "..#", "..#", ".#", "", "", "", "", "", "", ""}},
    {'!', {"", "..#", "..#", "..#", "..#", "..#", "..#", "..#", "", "..#", ""}},
    {'?', {"", ".###", "#...#", "....#", "...#", "..#", "..#", "..#", "", "..#", ""}},
    {'"', {"", ".#.#", ".#.#", "", "", "", "", "", "", "", ""}},
    {'#', {"", ".#.#", ".#.#", "#####", ".#.#", ".#.#", "#####", ".#.#", ".#.#", "", ""}},
    {'%', {"", ".#..#", "#.#.#", ".#.#", "...#", "..#", ".#", ".#.#", "#.#.#", "#..#", ""}},
    {'&', {"", ".##", "#..#", "#..#", ".##", ".#", "#.#.#", "#..#", "#..##", ".##.#", ""}},
};

// The fonts' narrow one-byte codes from 0x10, in their order; \1 for a code
// that is not a Latin character (the Japanese long vowel mark).
const char usNarrow[] = " +,-./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ[]=abcdefghijklmnopqrstuvwxyz!\"#%&'():?";
const char jpNarrow[] = " ()+,-./0123456789=ACDEFGILMNOPRSTUVXYZ[]\1abcdefgiklmnoprstuwxy";

int fontCode(uint32_t c) {
    // The font's own layout: its first code and narrow glyphs tell the US
    // font (81 from 0x10) from the Japanese one (63); another has no table.
    const char* narrow = nullptr;
    bool us = false;
    if (font != nullptr && fontFirst == 0x10 && fontNarrow == (int)sizeof(usNarrow) - 1) {
        narrow = usNarrow;
        us = true;
    } else if (font != nullptr && fontFirst == 0x10 && fontNarrow == (int)sizeof(jpNarrow) - 1) {
        narrow = jpNarrow;
    }
    if (narrow == nullptr) {
        return -1;
    }
    const char* at = c >= 0x20 && c < 0x7F ? strchr(narrow, (int)c) : nullptr;
    if (at != nullptr) {
        return 0x10 + (int)(at - narrow);
    }
    for (int i = 0; i < (int)(sizeof(japaneseCodes) / sizeof(japaneseCodes[0])); i++) {
        if (c == japaneseCodes[i].unicode) return 0x10000 + i;
    }
    for (int i = 0; i < (int)(sizeof(extra) / sizeof(extra[0])); i++) {
        if (c == extra[i].c) {
            return 0x100 + i;
        }
    }
    // Any character of the game's own fonts (system/gametext.h maps every
    // glyph of both): the JP font's (the running one, or the JP image beside
    // the US game), else the running US font's (its kana and signs).
    int g = gtUnicodeGlyph("JP", c, nullptr);
    if (g >= 0 && (!us || japaneseFont != nullptr)) return 0x20000 + g;
    g = us ? gtUnicodeGlyph("US", c, nullptr) : -1;
    if (g >= 0) return 0x30000 + g;
    if (!us) {
        return -1;
    }
    switch (c) { // the US font's wide signs
    case 0x00D7: return 0x7F; // the multiplication sign
    case 0x2026: return 0x8E; // ...
    case 0x2606: return 0x6D; // star
    case 0x25CB: return 0x6E; // circle, square, triangle
    case 0x25A1: return 0x6F;
    case 0x25B3: return 0x70;
    default: return -1;
    }
}

bool fontGlyph(int code, uint8_t px[13][14], int* advance) {
    memset(px, 0, 13 * 14);
    bool bits[11][12] = {};
    int count = (int)(sizeof(extra) / sizeof(extra[0]));
    if (code >= 0x100 && code < 0x100 + count && font != nullptr) {
        *advance = 8;
        for (int r = 0; r < 11; r++) {
            for (int c = 0; extra[code - 0x100].rows[r][c] != '\0' && c < 12; c++) {
                bits[r][c] = extra[code - 0x100].rows[r][c] == '#';
            }
        }
    } else {
        const uint8_t* data = font;
        int size = fontSize, at = -1;
        const uint16_t* fallback = nullptr;
        int jpCount = (int)(sizeof(japaneseCodes) / sizeof(japaneseCodes[0]));
        if (code >= 0x20000 && code < 0x40000) {
            // A glyph of the JP font (0x20000 + n) or the US font (0x30000 + n).
            bool jp = code < 0x30000;
            int n = code & 0xFFFF;
            *advance = gtGlyphWide(jp ? "JP" : "US", n) ? 12 : 8;
            if (jp && fontNarrow != 63) {
                data = japaneseFont;
                size = japaneseFontSize;
                at = 14 + n * 22;
            } else {
                at = fontGlyphs + n * 22;
            }
        } else if (code >= 0x10000 && code < 0x10000 + jpCount) {
            int i = code - 0x10000;
            const auto& glyph = japaneseCodes[i];
            *advance = 12;
            if (fontNarrow == 63 && glyph.jp >= 0) at = fontGlyphs + glyph.jp * 22;
            else if (glyph.us >= 0 && fontNarrow == 81) at = fontGlyphs + glyph.us * 22;
            else if (japaneseFont && glyph.jp >= 0) {
                data = japaneseFont;
                size = japaneseFontSize;
                at = 14 + glyph.jp * 22;
            } else {
                for (const auto& g : japaneseFallback) if (g.unicode == glyph.unicode) fallback = g.rows;
            }
        } else {
            if (code < fontFirst || code >= fontTwoByte) return false;
            at = fontGlyphs + (code - fontFirst) * 22;
            *advance = code - fontFirst < fontNarrow ? 8 : 12;
        }
        if (!fallback && (!data || at < 0 || at + 22 > size)) return false;
        // printDialogCharacter: row r's 12 pixels are the low byte's bits
        // 7..0, then the high byte's 7..4.
        static const int order[12] = {7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12};
        for (int r = 0; r < 11; r++) {
            uint16_t row;
            if (fallback) row = fallback[r];
            else memcpy(&row, data + at + r * 2, 2);
            for (int c = 0; c < 12; c++) {
                bits[r][c] = (row >> order[c]) & 1;
            }
        }
    }
    // Each pixel is drawn one right and one down of its place, with the
    // outline in the eight around it.
    for (int r = 0; r < 11; r++) {
        for (int c = 0; c < 12; c++) {
            for (int dy = 0; bits[r][c] && dy < 3; dy++) {
                for (int dx = 0; dx < 3; dx++) {
                    px[r + dy][c + dx] |= 2;
                }
            }
        }
    }
    for (int r = 0; r < 11; r++) {
        for (int c = 0; c < 12; c++) {
            if (bits[r][c]) {
                px[r + 1][c + 1] = 1;
            }
        }
    }
    for (int r = 0; r < 13; r++) {
        for (int c = 0; c < 14; c++) {
            px[r][c] = px[r][c] == 1 ? 1 : px[r][c] ? 2 : 0;
        }
    }
    return true;
}

void fontColors(uint16_t* body, uint16_t* outline) {
    uiGameFontColors(body, outline);
}

} // namespace assets
