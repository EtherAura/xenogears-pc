// Line breaking for the menus' CJK text: textwrap.h.
#include "textwrap.h"

namespace {

// One UTF-8 character at s (end bounds it): its code point and its length; a byte
// that starts no valid sequence is taken as itself, one byte long.
unsigned decode(const char* s, const char* end, int* length) {
    unsigned char b = (unsigned char)s[0];
    int n = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 0;
    if (n == 0 || end - s < n) {
        *length = 1;
        return b;
    }
    unsigned c = n == 1 ? b : n == 2 ? (b & 0x1Fu) : n == 3 ? (b & 0x0Fu) : (b & 0x07u);
    for (int i = 1; i < n; i++) {
        unsigned char t = (unsigned char)s[i];
        if ((t >> 6) != 2) {
            *length = 1;
            return b;
        }
        c = (c << 6) | (t & 0x3Fu);
    }
    *length = n;
    return c;
}

bool isCjk(unsigned c) {
    return (c >= 0x2E80 && c <= 0x9FFF) ||  // radicals, CJK symbols and punctuation, kana, ideographs
           (c >= 0xF900 && c <= 0xFAFF) ||  // compatibility ideographs
           (c >= 0xFE30 && c <= 0xFE4F) ||  // compatibility forms
           (c >= 0xFF00 && c <= 0xFFEF) ||  // full-width and half-width forms
           (c >= 0x20000 && c <= 0x3FFFF);  // the supplementary ideographs
}

// May not begin a line: closing brackets and quotes, the stops, the middle dot,
// iteration marks, the long-vowel mark, small kana (JIS X 4051's classes 2-6, 9).
bool noStart(unsigned c) {
    switch (c) {
    case 0x3001: case 0x3002: case 0x30FB: case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B: case 0xFF1F:
    case 0xFF01: case 0xFF09: case 0xFF3D: case 0xFF5D: case 0xFF60: case 0xFF61: case 0xFF63: case 0xFF64:
    case 0xFF65: case 0x3009: case 0x300B: case 0x300D: case 0x300F: case 0x3011: case 0x3015: case 0x3017:
    case 0x3019: case 0x301B: case 0x301E: case 0x301F: case 0x2019: case 0x201D: case 0x30FC: case 0xFF70:
    case 0x3005: case 0x303B: case 0x309D: case 0x309E: case 0x30FD: case 0x30FE: case 0x309B: case 0x309C:
    case 0x301C: case 0xFF5E: case 0x2010: case 0x2013: case 0x2026: case 0x2025:
    case ',': case '.': case ':': case ';': case '!': case '?': case ')': case ']': case '}':
        return true;
    default:
        break;
    }
    static const unsigned small[] = {0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087,
                                     0x308E, 0x3095, 0x3096, 0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3,
                                     0x30E3, 0x30E5, 0x30E7, 0x30EE, 0x30F5, 0x30F6};
    for (unsigned k : small) {
        if (c == k) return true;
    }
    return (c >= 0x31F0 && c <= 0x31FF) || (c >= 0xFF67 && c <= 0xFF6F); // more small kana, half-width ones
}

// May not end a line: opening brackets and quotes (class 1).
bool noEnd(unsigned c) {
    switch (c) {
    case 0x3008: case 0x300A: case 0x300C: case 0x300E: case 0x3010: case 0x3014: case 0x3016: case 0x3018:
    case 0x301A: case 0x301D: case 0xFF08: case 0xFF3B: case 0xFF5B: case 0xFF5F: case 0xFF62: case 0x2018:
    case 0x201C: case '(': case '[': case '{':
        return true;
    default:
        return false;
    }
}

bool isSpace(unsigned c) {
    return c == ' ' || c == '\t'; // (the ideographic space is CJK: a line may end before or after it)
}

} // namespace

bool textHasCjk(const char* text, const char* end) {
    for (const char* s = text; s < end;) {
        if ((unsigned char)*s < 0x80) {
            s++;
            continue;
        }
        int n;
        if (isCjk(decode(s, end, &n))) return true;
        s += n;
    }
    return false;
}

const char* textWrapCut(const char* text, const char* end, float width, float (*advance)(unsigned c, void* ctx),
                        void* ctx) {
    const char* breakAt = nullptr; // the last place the line may end
    const char* spaceRun = nullptr; // where the spaces before s began
    float used = 0.0f;
    unsigned prev = 0;
    for (const char* s = text; s < end;) {
        int n;
        unsigned c = decode(s, end, &n);
        if (c == '\n') return s;
        if (isSpace(c)) {
            if (spaceRun == nullptr) spaceRun = s;
            used += advance(c, ctx); // trailing spaces may go past the edge: they are not drawn
            prev = c;
            s += n;
            continue;
        }
        if (s > text) {
            if (spaceRun != nullptr) {
                breakAt = spaceRun; // after a word: the caller skips the spaces
            } else if ((isCjk(prev) || isCjk(c)) && !noStart(c) && !noEnd(prev)) {
                breakAt = s;
            }
        }
        spaceRun = nullptr;
        float w = advance(c, ctx);
        if (used + w > width && s > text) {
            return breakAt != nullptr ? breakAt : s;
        }
        used += w;
        prev = c;
        s += n;
    }
    return end;
}
