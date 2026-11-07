/* Reachable field media references, from our field VM handlers. No game execution. */
#include "gallery.h"
#include <stdlib.h>
#include <string.h>
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
/* ---- the field VM's instruction lengths -------------------------------------------------------- */

/* Bytes from the opcode for each primary op, and from the extended opcode
 * (after 0xFE) for each extended one; 0 for ops whose handler does nothing
 * (never reached in a script), -1 for those that depend on an argument. */
static const int8_t primSize[256] = {
    1,  3,  8,  4,  1,  3,  5,  3,  3,  3,  4,  3,  1,  1,  1,  1,  /* 00 */
    -1, -1, 9,  1,  1,  1,  3,  18, 5,  6,  2,  7,  4,  7,  1,  2,  /* 10 */
    3,  3,  1,  1,  2,  2,  3,  2,  2,  2,  1,  1,  2,  8,  3,  3,  /* 20 */
    3,  5,  5,  1,  5,  6,  3,  3,  6,  6,  6,  6,  3,  3,  6,  6,  /* 30 */
    6,  5,  5,  3,  5,  8,  1,  6,  7,  8,  6,  8,  8,  10, 6,  8,  /* 40 */
    8,  10, 4,  4,  5,  7,  10, -1, 4,  1,  1,  1,  3,  2,  1,  2,  /* 50 */
    1,  8,  2,  8,  1,  8,  2,  4,  4,  3,  3,  3,  3,  8,  8,  2,  /* 60 */
    2,  3,  3,  -1, 3,  3,  1,  1,  4,  1,  1,  4,  4,  4,  4,  3,  /* 70 */
    5,  5,  5,  5,  5,  5,  5,  3,  3,  6,  4,  5,  3,  3,  7,  3,  /* 80 */
    3,  4,  1,  3,  5,  2,  1,  3,  5,  1,  3,  5,  1,  4,  1,  1,  /* 90 */
    7,  3,  2,  8,  4,  3,  3,  1,  5,  2,  2,  1,  4,  7,  7,  4,  /* A0 */
    4,  4,  2,  3,  3,  5,  5,  1,  1,  4,  2,  2,  1,  3,  3,  3,  /* B0 */
    3,  3,  3,  1,  2,  2,  1,  3,  3,  4,  8,  4,  4,  1,  1,  5,  /* C0 */
    11, 0,  4,  4,  5,  3,  3,  3,  3,  3,  17, 5,  5,  6,  6,  6,  /* D0 */
    7,  14, 5,  5,  0,  17, 9,  7,  7,  7,  6,  20, 15, 8,  3,  3,  /* E0 */
    7,  11, 9,  7,  2,  4,  2,  5,  4,  2,  5,  5,  6,  1,  0,  1,  /* F0 */
};
static const int8_t extSize[256] = {
    0,  1,  4,  3,  3,  6,  6,  2,  7,  3,  3,  3,  13, 3,  5,  6,  /* 00 */
    5,  6,  3,  5,  5,  5,  1,  3,  2,  2,  1,  5,  8,  8,  2,  1,  /* 10 */
    2,  3,  3,  20, 1,  2,  15, -1, 3,  3,  3,  3,  3,  3,  3,  3,  /* 20 */
    5,  5,  5,  5,  6,  6,  6,  6,  5,  3,  3,  3,  5,  10, 10, 7,  /* 30 */
    7,  3,  3,  1,  1,  2,  2,  3,  8,  1,  3,  1,  2,  2,  1,  1,  /* 40 */
    1,  1,  1,  1,  1,  1,  3,  1,  3,  3,  3,  3,  -1, 7,  3,  8,  /* 50 */
    9,  1,  5,  5,  3,  5,  9,  19, 6,  5,  3,  5,  1,  1,  4,  8,  /* 60 */
    3,  3,  10, 12, 3,  4,  16, -1, 0,  0,  0,  0,  0,  0,  0,  1,  /* 70 */
    15, 8,  25, 3,  9,  3,  2,  1,  18, 11, 3,  3,  7,  3,  5,  8,  /* 80 */
    9,  14, 14, 11, 10, 14, 1,  2,  3,  2,  9,  3,  3,  3,  9,  4,  /* 90 */
    12, 5,  1,  2,  1,  7,  5,  9,  7,  7,  2,  4,  4,  4,  7,  18, /* A0 */
    -1, 1,  4,  4,  4,  1,  2,  3,  4,  9,  10, 3,  4,  7,  1,  13, /* B0 */
    3,  7,  9,  1,  2,  5,  3,  5,  18, 18, 2,  1,  1,  3,  3,  5,  /* C0 */
    5,  1,  3,  17, -1, 5,  5,  6,  2,  2,  1,  3,  5,  -1, 5,  3,  /* D0 */
    2,  5,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* E0 */
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  /* F0 */
};

/* Ops with a script address argument (where a branch, jump or call goes), as
 * the byte offset of the address from the opcode (extended: from 0xFE). */
int xgFieldJumpOffset(int op, int ext) {
    if (!ext) {
        switch (op) {
        case 0x01: /* JUMP */
        case 0x05: /* CALL */
        case 0x06: /* a call with a 2-byte argument */
            return 1;
        case 0x02: /* JUMP_IF */
            return 6;
        case 0x91: /* IF_CHARACTER_IN_PARTY */
        case 0xB9: /* IF_CHARACTER_IN_AVAILABLE_PARTY */
        case 0xCB: /* IF_PLAYER_IN_TRIGGER */
            return 2;
        case 0x31: /* jumpIfMask */
        case 0x32:
        case 0x84: /* IF_GAMEPROGRESS_LESS, _GREATER, _EQUAL */
        case 0x85:
        case 0x86:
        case 0x8B: /* an item in the inventory */
        case 0xE2: /* jumpIfEqual */
        case 0xE3:
        case 0xFB: /* a variable's bit */
            return 3;
        case 0x8E: /* enough gold */
            return 5;
        default:
            return 0;
        }
    }
    switch (op) {
    case 0x02: /* IF_CHARACTER_IN_VIEW */
        return 1 + 2;
    case 0x30: /* jumpIfMask2 */
    case 0x31:
    case 0x32:
    case 0x33:
        return 1 + 3;
    case 0x34: /* jumpIfMask3 */
    case 0x35:
    case 0x36:
    case 0x37:
        return 1 + 4;
    default:
        return 0;
    }
}

/* Ops after which the next byte is not reached by falling through: STOP,
 * JUMP, RETURN and the two that end the actor's script. */
int xgFieldEndsFlow(int op) {
    return op == 0x00 || op == 0x01 || op == 0x0C || op == 0x0D || op == 0x5B;
}

/* The length of the instruction at pc (0: not an instruction). */
int xgFieldOpLength(const uint8_t* c, int size, int pc) {
    if (!c || pc < 0 || size <= 0 || size > 8 * 1024 * 1024 || pc >= size) return 0;
    if (pc + 2 > size) {
        return pc < size && c[pc] != 0xFE && primSize[c[pc]] == 1 ? 1 : 0;
    }
    int op = c[pc], b = c[pc + 1];
    if (op == 0xFE) {
        int e = b, arg = pc + 2 < size ? c[pc + 2] : 0;
        int n = extSize[e];
        if (n == -1) {
            switch (e) {
            case 0x27: /* screen distortion fade: 0 has a value */
                n = arg == 0 ? 4 : arg <= 3 ? 2 : 0;
                break;
            case 0x5C: /* LOAD_NEW_MECHA: 2 has a value */
                n = arg <= 1 ? 2 : arg == 2 ? 4 : 0;
                break;
            case 0x77: /* an event picture: 1 has its place */
                n = arg == 1 ? 11 : 2;
                break;
            case 0xB0: /* a sample bank: 1 waits for it */
                n = arg == 1 ? 2 : 6;
                break;
            case 0xD4: /* script markers: 1 and 3 have four values */
                n = arg == 0 || arg == 2 ? 2 : arg == 1 || arg == 3 ? 10 : 0;
                break;
            case 0xDD: /* palette fades: 0 and 1 have two values */
                n = arg <= 1 ? 6 : arg <= 3 ? 2 : 0;
                break;
            default:
                n = 0;
                break;
            }
        }
        return n > 0 && pc + 1 + n <= size ? 1 + n : 0;
    }
    int n = primSize[op];
    if (n == -1) {
        switch (op) {
        case 0x10: /* a walk: its set-up, then the step that repeats */
            n = b == 0 ? 9 : 2;
            break;
        case 0x11: /* the same with a step count */
            n = b == 0 ? 9 : 4;
            break;
        case 0x57: /* a jump: modes 0-2 set it up, 3 steps it */
            n = (b & 3) == 3 ? 2 : 11;
            break;
        case 0x73: /* a particle effect: 1 has three values */
            n = b == 0 ? 2 : b == 1 ? 8 : 0;
            break;
        default:
            n = 0;
            break;
        }
    }
    return n > 0 && pc + n <= size ? n : 0;
}

/* Every instruction reachable from the routine table: marks[pc] = its length. */
uint8_t* xgFieldScriptWalk(const XgFieldScript* s) {
    if (!s || !s->code || !s->routines || s->codeSize <= 0 || s->codeSize > 8 * 1024 * 1024 ||
        s->actors <= 0 || s->actors > 256) return NULL;
    int size = s->codeSize;
    uint8_t* marks = calloc((size_t)size + 1, 1);
    int* todo = malloc(sizeof(int) * (size_t)(s->actors * 32 + size + 1));
    uint8_t* queued = calloc((size_t)size, 1);
    int count = 0;
    if (marks == NULL || todo == NULL || queued == NULL) {
        free(marks);
        free(todo);
        free(queued);
        return NULL;
    }
    for (int a = 0; a < s->actors; a++) {
        for (int k = 0; k < 32; k++) {
            if (s->routines[a][k] < size && !queued[s->routines[a][k]]) {
                queued[s->routines[a][k]] = 1;
                todo[count++] = s->routines[a][k];
            }
        }
    }
    while (count > 0) {
        int pc = todo[--count];
        while (pc >= 0 && pc < size && marks[pc] == 0) {
            int n = xgFieldOpLength(s->code, size, pc);
            if (n == 0) {
                break;
            }
            marks[pc] = (uint8_t)n;
            int ext = s->code[pc] == 0xFE;
            int op = ext ? s->code[pc + 1] : s->code[pc];
            int at = xgFieldJumpOffset(op, ext);
            if (at != 0 && pc + at + 2 <= size) {
                int t = rd16(s->code + pc + at);
                if (t < size && marks[t] == 0 && !queued[t]) {
                    queued[t] = 1;
                    todo[count++] = t;
                }
            }
            if (!ext && op == 0xA6) {
                /* A computed jump into the table of JUMPs that follows it. */
                for (int q = pc + 3; q + 3 <= size && s->code[q] == 0x01 && marks[q] == 0; q += 3) {
                    marks[q] = 3;
                    int t = rd16(s->code + q + 1);
                    if (t < size && marks[t] == 0 && !queued[t]) {
                        queued[t] = 1;
                        todo[count++] = t;
                    }
                }
            }
            if (!ext && xgFieldEndsFlow(op)) {
                break;
            }
            pc += n;
        }
    }
    free(todo);
    free(queued);
    return marks;
}


static int immediate(const uint8_t* p) {
    int v = rd16(p); return v & 0x8000 ? v & 0x7fff : -1;
}
int xgFieldMediaDecode(const XgFieldScript* fs, int field, XgFieldMedia** out) {
    *out = NULL;
    uint8_t* marks = xgFieldScriptWalk(fs);
    if (!marks) return -1;
    int count = 0;
    for (int pc = 0; pc < fs->codeSize; ++pc) {
        const uint8_t* c = fs->code + pc;
        if (marks[pc] < 3 || c[0] != 0xfe) continue;
        int op = c[1];
        if (op != 0xa0 && op != 0x60 && op != 0x67 && !(op == 0x77 && c[2] == 0)) continue;
        XgFieldMedia r = {.field = field, .pc = pc, .op = op, .picture = -1};
        if (op == 0x77) {
            r.kind = XG_MEDIA_PICTURE;
            /* Mode 0 reads its number/flag from the following mode-1 upload.
             * Check that entire instruction, not arbitrary look-ahead bytes. */
            if (pc + 15 > fs->codeSize || marks[pc + 3] != 12 || c[3] != 0xfe || c[4] != 0x77 || c[5] != 1)
                r.problem = XG_MEDIA_INVALID;
            else if (!(c[14] & 0x80)) r.problem = XG_MEDIA_DYNAMIC;
            else { r.picture = (int16_t)rd16(c + 6); if (r.picture < 0 || r.picture > 255) r.problem = XG_MEDIA_INVALID; }
        } else {
            r.kind = XG_MEDIA_MOVIE;
            const uint8_t* a = c + 1;
            if (op == 0xa0) {
                r.stream = rd16(a + 1); r.offset = rd16(a + 3); r.first = rd16(a + 5);
                r.last = rd16(a + 7); r.mode = rd16(a + 9);
                if ((a[11] & 0xf8) != 0xf8) r.problem = XG_MEDIA_DYNAMIC;
            } else {
                r.stream = immediate(a + 1); r.offset = immediate(a + 3);
                r.first = op == 0x60 ? 1 : immediate(a + 5);
                r.last = immediate(a + (op == 0x60 ? 5 : 7)); r.mode = immediate(a + (op == 0x60 ? 7 : 9));
                for (int k = 1; k < marks[pc] - 2; k += 2) if (immediate(a + k) < 0) r.problem = XG_MEDIA_DYNAMIC;
            }
            if (!r.problem && (r.first < 1 || r.last < r.first)) r.problem = XG_MEDIA_INVALID;
        }
        XgFieldMedia* next = realloc(*out, sizeof(**out) * (size_t)(count + 1));
        if (!next) { free(*out); *out = NULL; free(marks); return -1; }
        *out = next; next[count++] = r;
    }
    free(marks); return count;
}
