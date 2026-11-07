#include "battlefx_decode.h"

static uint32_t rd32(const uint8_t* p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int rd16(const uint8_t* p) {
    return p[0] | p[1] << 8;
}

int xgBattleFxModelSafe(const uint8_t* data, int size, int pc) {
    if (!data || size < 4 || pc < 0 || pc > size - 4 || data[pc] != 0xF3) return 0;
    int off = data[pc + 1] | data[pc + 2] << 8 | (int8_t)data[pc + 3] * 65536;
    if (!off) return 1; /* release the current model */
    int64_t at = (int64_t)pc + 1 + off;
    if (at < 0 || at > size - 40) return 0;
    const uint8_t* b = data + at;
    if (rd32(b + 4) & 1 || rd32(b + 8) < 1 || rd32(b + 8) > 256) return 0;
    b += 12; at += 12;
    uint32_t vertices = rd32(b + 4), count = rd32(b + 20);
    int64_t v = at + rd32(b), normal = at + rd32(b + 8), packet = at + rd32(b + 16);
    if (!vertices || vertices > 8192 || count > 8192 || v < 0 || v > size - 8LL * vertices ||
        normal < 0 || normal > size || packet < 0 || packet > size) return 0;
    /* Packet layouts read by renderCustomPolyBuffer; indices select
     * eight-byte SVECTORs in the source model. */
    static const uint8_t vi[16][4] = {
        {8,10,12,0}, {20,22,24,0}, {8,10,12,14}, {24,26,28,30},
        {16,18,20,0}, {28,30,32,0}, {20,22,24,26}, {36,38,40,42},
        {10,12,14,0}, {18,20,22,0}, {10,12,14,16}, {22,24,26,28},
        {18,22,26,0}, {18,22,26,0}, {22,26,30,34}, {22,26,30,34}
    };
    static const uint8_t words[8] = {4,7,5,9,6,9,8,12};
    for (uint32_t i = 0; i < count; i++) {
        if (packet > size - 4) return 0;
        const uint8_t* p = data + packet;
        int type = (p[3] & 0x1C) >> 2, lit = !(p[2] & 1), n = (type & 2) ? 4 : 3;
        int length = (p[1] + 1) * 4;
        if ((p[3] & 0xE0) != 0x20 || p[0] != words[type] || length < vi[type + 8 * lit][n - 1] + 2 ||
            packet > size - length) return 0;
        for (int j = 0; j < n; j++) {
            unsigned index = rd16(p + vi[type + 8 * lit][j]);
            if (index >= vertices) return 0;
        }
        if (lit) {
            int start = (type & 1) ? 16 : 8;
            int normals = (type & 4) ? n : 1;
            for (int j = 0; j < normals; j++) {
                int pos = start + j * 4;
                if (pos + 2 > length || normal + 8LL * rd16(p + pos) > size - 8) return 0;
            }
        }
        packet += length;
    }
    return 1;
}

int xgBattleFxBundle(const uint8_t* data, int size) {
    if (!data || size < 32) return 0;
    uint32_t count = rd32(data), a = rd32(data + 4), f = rd32(data + 8), c = rd32(data + 12);
    if (count < 3 || count > 64 || 4 + 4 * count > (uint32_t)size || a < 4 + 4 * count ||
        a > (uint32_t)size - 4 || f <= a || f > (uint32_t)size - 4) return 0;
    int animations = rd16(data + a) & 63, frames = rd16(data + f) & 511;
    if (!animations || a + 2 + 2 * (uint32_t)animations > f) return 0;
    /* Palette-free controller banks contain no root frames: children use
     * the separately loaded texture/CLUT file. Bound their table by the
     * next real member instead of treating offset zero as a palette. */
    if (!c) {
        uint32_t end = size;
        if (frames || !(rd16(data + f) & 0x8000)) return 0;
        for (uint32_t i = 3; i < count; i++) {
            uint32_t next = rd32(data + 4 + 4 * i);
            if (next > f && next < end) end = next;
        }
        return f + 4 <= end;
    }
    if (c <= f || c > (uint32_t)size - 4 || f + 2 + 2 * (uint32_t)frames > c) return 0;
    int colours = rd16(data + c) * 16, palettes = rd16(data + c + 2) + 1;
    return colours <= 1024 && (colours == 0 || (uint32_t)(size - c - 4) / (2u * colours) >= (uint32_t)palettes);
}

int xgBattleFxCommandSafe(const uint8_t* data, int size, int pc) {
    if (data == 0 || size < 2 || pc < 0 || pc > size - 2)
        return 0;
    int op = data[pc], id = data[pc + 1], length;
    if (op == 0xCA || op == 0xCB) {
        if (pc > size - 3) return 0;
        int64_t at = (int64_t)pc + 1 + (int16_t)rd16(data + pc + 1);
        return at >= 0 && at <= size - (op == 0xCA ? 5 : 4);
    }
    if (op == 0xFB) {
        if (pc > size - 4) return 0;
        int64_t at = (int64_t)pc + (int16_t)rd16(data + pc + 1);
        return at >= 0 && at < size;
    }
    if (op == 0xC3)
        length = 2;
    else if (op == 0xEC || op == 0xE8)
        length = 3;
    else if (op == 0xF9)
        length = 4;
    else
        return 0;
    if (pc > size - length)
        return 0;
    int args = op == 0xE8 ? 1 : length - 2; /* runtime checks the resolved stack/parameter byte */
    switch (id) {
    case 0x02:
    case 0x05:
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0F:
    case 0x10:
    case 0x11: case 0x12: case 0x13: case 0x14:
    case 0x15: case 0x16:
    case 0x18:
    case 0x1B:
    case 0x1C:
    case 0x1E:
    case 0x3C:
    case 0x3F:
    case 0x42:
    case 0x43:
    case 0x48:
    case 0x4C:
    case 0x53:
    case 0x20: case 0x23: case 0x24: case 0x25: case 0x2C:
    case 0x31: case 0x32: case 0x35: case 0x36: case 0x37:
    case 0x50: case 0x51: case 0x54: case 0x57: case 0x58: case 0x59: case 0x5A:
        return 1;
    case 0x0D:
    case 0x0E:
    case 0x17:
    case 0x21:
    case 0x22:
    case 0x28:
    case 0x29:
    case 0x2A:
    case 0x2B:
    case 0x34:
    case 0x49:
    case 0x4A:
    case 0x4B:
    case 0x40:
    case 0x46: case 0x47:
        return args >= 1;
    case 0x27:
        return args >= 2; /* literal CLUT column/row deltas */
    case 0x19: case 0x1D: case 0x2D: case 0x2E: case 0x3A:
    case 0x5B: case 0x5C: case 0x5D: case 0x5E: {
        if (args < 2)
            return 0;
        int64_t at = (int64_t)pc + 2 + (int16_t)rd16(data + pc + 2);
        return at >= 0 && at <= size - 6;
    }
    default:
        return 0;
    }
}
