#include "sprites_catalogue.h"
#include <cstdio>
#include <vector>

static int checks, failures;
#include "../../test/check.h" /* no port/src on this test's include path */
static void put16(std::vector<uint8_t>& b, int at, unsigned n) { b[at] = uint8_t(n); b[at + 1] = uint8_t(n >> 8); }
static void put32(std::vector<uint8_t>& b, int at, unsigned n) { put16(b, at, n); put16(b, at + 2, n >> 16); }
static std::vector<uint8_t> bundle() {
    std::vector<uint8_t> b(128);
    put32(b, 0, 3); put32(b, 4, 20); put32(b, 8, 32); put32(b, 12, 80);
    put16(b, 20, 1); put16(b, 22, 4); b[24] = 1;
    put16(b, 32, 0x8202); put16(b, 34, 8); put16(b, 36, 16);
    b[40] = b[48] = 1; put16(b, 44, 28); put16(b, 52, 33);
    b[61] = 16; b[62] = 20; b[63] = b[64] = 4;
    b[66] = 32; b[67] = 40; b[68] = b[69] = 4;
    put16(b, 80, 1); put16(b, 84, 0x7fff);
    return b;
}
static std::vector<uint16_t> texture(int pageX, int pageY) {
    std::vector<uint16_t> v(1024 * 512);
    for (int y = 0; y < 256; ++y) for (int x = 0; x < 64; ++x)
        v[(pageY + y) * 1024 + pageX + x] = uint16_t(0x1234 ^ (x * 4 + y));
    return v;
}
int main() {
    auto b = bundle(); auto a = texture(640, 256), v = texture(768, 0);
    auto key = extras::npcIdentity(b.data(), (int)b.size(), a.data(), 640, 256);
    EXPECT(!key.empty());
    EXPECT(key == extras::npcIdentity(b.data(), (int)b.size(), v.data(), 768, 0)); // relocated field sheet
    std::vector<uint16_t> shifted(1024 * 512);
    for (int y = 0; y < 256; ++y) for (int x = 0; x < 64; ++x)
        shifted[((y + 12) & 255) * 1024 + 768 + x] = a[(256 + y) * 1024 + 640 + x];
    EXPECT(key == extras::npcIdentity(b.data(), (int)b.size(), shifted.data(), 768, 12)); // sheet within page
    v[511 * 1024] ^= 1;
    EXPECT(key == extras::npcIdentity(b.data(), (int)b.size(), v.data(), 768, 0)); // unrelated art
    v[20 * 1024 + 768 + 5] ^= 1;
    v[24 * 1024 + 768 + 4] ^= 1;
    EXPECT(key == extras::npcIdentity(b.data(), (int)b.size(), v.data(), 768, 0)); // just outside tile's UVs
    v[40 * 1024 + 768 + 8] ^= 1;
    EXPECT(key != extras::npcIdentity(b.data(), (int)b.size(), v.data(), 768, 0)); // later frame differs
    v = texture(768, 0); v[20 * 1024 + 768 + 3] ^= 0x1000;
    EXPECT(key != extras::npcIdentity(b.data(), (int)b.size(), v.data(), 768, 0)); // mirrored left seam
    auto other = b; other[24] = 2;
    EXPECT(key != extras::npcIdentity(other.data(), (int)other.size(), a.data(), 640, 256)); // animation
    other = b; other[84] ^= 1;
    EXPECT(key != extras::npcIdentity(other.data(), (int)other.size(), a.data(), 640, 256)); // palette
    other = b; other[60] |= 1; other[65] |= 1;
    v = texture(768, 0);
    auto eight = extras::npcIdentity(other.data(), (int)other.size(), a.data(), 640, 256);
    EXPECT(!eight.empty());
    EXPECT(eight == extras::npcIdentity(other.data(), (int)other.size(), v.data(), 768, 0));
    v[40 * 1024 + 768 + 16] ^= 0x100;
    EXPECT(eight != extras::npcIdentity(other.data(), (int)other.size(), v.data(), 768, 0));
    other = b; other[60] |= 16;
    EXPECT(extras::npcIdentity(other.data(), (int)other.size(), a.data(), 640, 256).empty()); // global CLUT
    other = b; other[61] = 0; other[63] = 1;
    v = texture(768, 0);
    auto wrap = extras::npcIdentity(other.data(), 128, v.data(), 768, 0);
    EXPECT(!wrap.empty());
    v[20 * 1024 + 768 + 50] ^= 1;
    EXPECT(wrap != extras::npcIdentity(other.data(), 128, v.data(), 768, 0)); // mirrored endpoint wraps to 255
    other = b; put16(other, 80, 0);
    EXPECT(extras::npcIdentity(other.data(), (int)other.size(), a.data(), 640, 256).empty()); // contextual palette
    EXPECT(extras::npcIdentity(b.data(), (int)b.size(), nullptr, 0, 0).empty());
    EXPECT(extras::npcIdentity(nullptr, 128, a.data(), 0, 0).empty());
    for (int size = 0; size < 84; ++size) EXPECT(extras::npcIdentity(b.data(), size, a.data(), 0, 0).empty());
    for (int offset : {8, 12}) {
        other = b; put32(other, offset, 0xffffffff);
        EXPECT(extras::npcIdentity(other.data(), (int)other.size(), a.data(), 0, 0).empty());
    }
    for (int offset : {34, 36, 44, 52}) {
        other = b; put16(other, offset, 0xffff);
        EXPECT(extras::npcIdentity(other.data(), (int)other.size(), a.data(), 0, 0).empty());
    }
    other = b; other[40] = 63;
    EXPECT(extras::npcIdentity(other.data(), 128, a.data(), 0, 0).empty());
    other = b; put16(other, 32, 0x202); // embedded pieces depend only on their full bundle
    EXPECT(!extras::npcIdentity(other.data(), 128, nullptr, 0, 0).empty());
    // xg-htn.35.68: what the field gives an NPC for its sheet.
    std::vector<uint8_t> s4(64);
    put32(s4, 0, 2); put32(s4, 4, 12); put32(s4, 8, 0); // NPC 0's sheet at 12; NPC 1 none
    put32(s4, 12, 1); put32(s4, 16, 8);                 // one page, 8 bytes on
    put16(s4, 20, 2); put16(s4, 22, 3);                 // 2 x 3 words
    auto sheet = extras::npcSheet(s4.data(), (int)s4.size(), 0, 640, 256, false);
    EXPECT(sheet.kind == extras::NpcSheet::OWN && sheet.pages.size() == 1);
    EXPECT(sheet.pages[0].x == 640 && sheet.pages[0].y == 256 && sheet.pages[0].w == 2 && sheet.pages[0].h == 3);
    EXPECT(extras::npcSheet(s4.data(), (int)s4.size(), 0, 640, 256, true).kind == extras::NpcSheet::TEXTURE);
    EXPECT(extras::npcSheet(s4.data(), (int)s4.size(), 1, 640, 256, false).kind == extras::NpcSheet::NONE);
    EXPECT(extras::npcSheet(s4.data(), (int)s4.size(), 2, 640, 256, false).kind == extras::NpcSheet::NONE);
    EXPECT(extras::npcSheet(s4.data(), 30, 0, 640, 256, false).kind == extras::NpcSheet::NONE); // page past the end
    EXPECT(extras::npcSheet(nullptr, 0, 0, 0, 0, false).kind == extras::NpcSheet::NONE);
    std::vector<uint16_t> before(1024 * 512), after(1024 * 512);
    after[255 * 1024 + 641] = 1; // outside the page
    EXPECT(!extras::npcSheetReplaced(sheet, before.data(), after.data()));
    after[258 * 1024 + 641] = 1; // its last texel
    EXPECT(extras::npcSheetReplaced(sheet, before.data(), after.data()));
    std::vector<uint16_t> empty(1024 * 512);
    EXPECT(extras::npcBlank(b.data(), (int)b.size(), empty.data(), 640, 256)); // only index 0 sampled
    EXPECT(!extras::npcBlank(b.data(), (int)b.size(), a.data(), 640, 256));
    other = b; put16(other, 32, 0x202);
    EXPECT(!extras::npcBlank(other.data(), 128, empty.data(), 640, 256)); // embedded pieces: not judged
    std::printf("NPC catalogue: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
