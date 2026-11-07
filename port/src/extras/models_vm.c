/*
 * The Gear animation VM's bytecode, as the two interpreters read it
 * (models_vm.h): each op's length and what it does, in our words, from
 * field_mecha processMechaAnimData and battle processMechaAnimData_battle.
 *
 * An op is a u16 word: the low byte the opcode, the high byte its parameter;
 * operand words follow. The battle copy reads operands for ops the field copy
 * passes over as one word (0x04, 0x12, 0x1B, 0x2C, 0x2D, 0x3E, 0x52, 0x61,
 * 0x65-0x67, 0x71, 0x75) and knows 0x71-0x75, which the field copy stops at.
 */

#include "models_vm.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned char field, battle; /* words the op takes in each copy, 0: it stops there (unknown) */
    const char* name;
} OpInfo;

/* Ops whose length depends on their operands are marked 0xFF and handled
 * below (0x63: an event stream the op jumps over). */
static const OpInfo ops[0x76] = {
    [0x00] = {1, 1, "end"},
    [0x01] = {2, 2, "wait"},
    [0x02] = {1, 1, "signal"},
    [0x03] = {1, 1, "signal"},
    [0x04] = {1, 2, "load extra file"},
    [0x05] = {1, 1, "set up extra file"},
    [0x06] = {1, 1, "free extra file"},
    [0x07] = {1, 1, "stop CD read"},
    [0x08] = {1, 1, "stop tracks"},
    [0x09] = {1, 1, "free tagged tracks"},
    [0x0A] = {1, 1, "release bone tracks"},
    [0x0B] = {1, 1, "reset pose"},
    [0x0C] = {1, 1, "stop root motion"},
    [0x0D] = {1, 1, "release rotations"},
    [0x0E] = {1, 1, "release translations"},
    [0x0F] = {1, 1, "end event camera"},
    [0x10] = {1, 1, "pose from keyframe"},
    [0x11] = {2, 2, "play keyframe"},
    [0x12] = {1, 2, "play keyframe from its pose"},
    [0x13] = {3, 3, "blend to keyframe"},
    [0x14] = {2, 2, "play on other models"},
    [0x15] = {2, 2, "split off bones"},
    [0x16] = {1, 1, "rejoin bones"},
    [0x17] = {1, 1, "free this model"},
    [0x18] = {2, 2, "keyframe events"},
    [0x19] = {1, 1, "stop events"},
    [0x1A] = {7, 7, "move VRAM rectangle"},
    [0x1B] = {1, 16, "colour animation"},
    [0x1C] = {1, 1, "stop colour animation"},
    [0x1D] = {10, 10, "bone interpolation"},
    [0x1E] = {1, 1, "bone scale mode"},
    [0x1F] = {1, 1, "act on model"},
    [0x20] = {1, 1, "wait for all tracks"},
    [0x21] = {1, 1, "wait for tagged tracks"},
    [0x22] = {2, 2, "wait, loops"},
    [0x23] = {2, 2, "bone visibility"},
    [0x24] = {1, 1, "visibility"},
    [0x25] = {5, 5, "attach models"},
    [0x26] = {1, 1, "detach models"},
    [0x27] = {1, 1, "update matrices"},
    [0x28] = {2, 2, "wait for distance, near"},
    [0x29] = {2, 2, "wait for distance, far"},
    [0x2A] = {1, 1, "wait, target near"},
    [0x2B] = {1, 1, "wait, target far"},
    [0x2C] = {1, 4, "wait, point near"},
    [0x2D] = {1, 4, "wait, point far"},
    [0x2E] = {2, 2, "on reaching target"},
    [0x2F] = {1, 1, "wait for damage"},
    [0x30] = {2, 2, "clear counter"},
    [0x31] = {2, 2, "count loop"},
    [0x32] = {2, 2, "jump"},
    [0x33] = {2, 2, "jump on transition"},
    [0x34] = {2, 2, "jump unless a battle Gear"},
    [0x35] = {2, 2, "jump at random"},
    [0x36] = {3, 3, "delayed branch"},
    [0x37] = {2, 2, "on landing"},
    [0x38] = {2, 2, "look at target"},
    [0x39] = {2, 2, "turn to target"},
    [0x3A] = {1, 1, "wait for damage display"},
    [0x3B] = {2, 2, "jump if hit"},
    [0x3C] = {2, 2, "fade effect volume"},
    [0x3D] = {1, 1, "flush queue on"},
    [0x3E] = {1, 2, "jump on damage type"},
    [0x3F] = {1, 1, "show damage"},
    [0x40] = {4, 4, "rotate to"},
    [0x41] = {4, 4, "rotate by"},
    [0x42] = {1, 1, "face target"},
    [0x43] = {1, 1, "face target, level"},
    [0x44] = {4, 4, "angular velocity"},
    [0x45] = {4, 4, "add angular velocity"},
    [0x46] = {4, 4, "angular acceleration"},
    [0x47] = {4, 4, "add angular acceleration"},
    [0x48] = {1, 1, "ground snap off"},
    [0x49] = {4, 4, "move to"},
    [0x4A] = {1, 1, "move near target"},
    [0x4B] = {4, 4, "velocity"},
    [0x4C] = {4, 4, "add velocity"},
    [0x4D] = {4, 4, "acceleration"},
    [0x4E] = {4, 4, "add acceleration"},
    [0x4F] = {1, 1, "head for target"},
    [0x50] = {4, 4, "target point"},
    [0x51] = {1, 1, "target entity"},
    [0x52] = {1, 5, "target bone"},
    [0x53] = {1, 1, "target to ground"},
    [0x54] = {2, 2, "step distance"},
    [0x55] = {2, 2, "add step distance"},
    [0x56] = {2, 2, "add step"},
    [0x57] = {1, 1, "add collider radius"},
    [0x58] = {1, 1, "target model"},
    [0x59] = {1, 1, "target camera eye"},
    [0x5A] = {1, 1, "target camera point"},
    [0x5B] = {1, 1, "queue mode"},
    [0x5C] = {2, 2, "jump if at target"},
    [0x5D] = {2, 2, "billboard"},
    [0x5E] = {2, 2, "scale"},
    [0x5F] = {2, 2, "flags"},
    [0x60] = {1, 1, "target slot centre"},
    [0x61] = {1, 2, "jump on slot layout"},
    [0x62] = {5, 5, "bone offset"},
    [0x63] = {0xFF, 0xFF, "event stream"},
    [0x64] = {2, 2, "cloth pull"},
    [0x65] = {1, 4, "camera point track"},
    [0x66] = {1, 4, "camera eye track"},
    [0x67] = {1, 5, "camera track"},
    [0x68] = {1, 1, "event camera"},
    [0x69] = {1, 1, "wait for camera"},
    [0x6A] = {1, 1, "camera write-back"},
    [0x6B] = {2, 2, "rotation order"},
    [0x6C] = {1, 1, "wait for CD"},
    [0x6D] = {1, 1, "sync flag"},
    [0x6E] = {2, 2, "wait for sync"},
    [0x6F] = {1, 1, "save target mask"},
    [0x70] = {2, 2, "jump on target mask"},
    [0x71] = {0, 2, "effect id"},
    [0x72] = {0, 1, "wait for effect"},
    [0x73] = {0, 1, "effect fragments"},
    [0x74] = {0, 1, "effect event"},
    [0x75] = {0, 2, "jump if facing target"},
};

/* Ops neither copy knows (the interpreter stops at them): 0x76 and up. */
static const OpInfo* info(int op) {
    return op >= 0 && op < 0x76 && ops[op].name != NULL ? &ops[op] : NULL;
}

int xgMechaOpLength(const uint16_t* pc, int words, int battle) {
    if (words <= 0) {
        return 0;
    }
    int op = pc[0] & 0xFF;
    const OpInfo* i = info(op);
    if (i == NULL) {
        return 0;
    }
    int n = battle ? i->battle : i->field;
    if (n == 0xFF) {
        /* 0x63: the stream follows the op; the op jumps over it by its
         * operand (bytes from the op). */
        if (words < 2) {
            return 0;
        }
        int bytes = (int16_t)pc[1];
        return bytes >= 4 && !(bytes & 1) && bytes / 2 <= words ? bytes / 2 : 0;
    }
    return n <= words ? n : 0;
}

const char* xgMechaOpName(int op, int battle) {
    const OpInfo* i = info(op & 0xFF);
    if (i == NULL || (!battle && i->field == 0)) {
        return NULL;
    }
    return i->name;
}

int xgMechaOpBattleOnly(int op) {
    const OpInfo* i = info(op & 0xFF);
    return i != NULL && (i->field != i->battle || i->field == 0);
}

int xgMechaDisasm(const uint16_t* pc, int words, int battle, char* out, int outSize) {
    if (words <= 0 || pc == NULL) {
        if (outSize > 0) {
            snprintf(out, (size_t)outSize, "(end of data)");
        }
        return 0;
    }
    int n = xgMechaOpLength(pc, words, battle);
    const char* name = words > 0 ? xgMechaOpName(pc[0], battle) : NULL;
    int at = snprintf(out, (size_t)outSize, "%02X %02X %s", pc[0] & 0xFF, pc[0] >> 8, name != NULL ? name : "(stops)");
    for (int k = 1; k < n && k < 6 && at < outSize; k++) {
        at += snprintf(out + at, (size_t)(outSize - at), " %04X", pc[k]);
    }
    if (n > 6 && at < outSize) {
        snprintf(out + at, (size_t)(outSize - at), " ...");
    }
    return n;
}

int xgMechaScan(const uint16_t* program, int words, int battle, xgMechaScanFn fn, void* user) {
    int at = 0, count = 0;
    while (at < words) {
        int n = xgMechaOpLength(program + at, words - at, battle);
        if (n <= 0) {
            break;
        }
        if (fn != NULL && !fn(program + at, at, user)) {
            break;
        }
        count++;
        at += n;
    }
    return count;
}

/* ---- parameters the interpreters index arrays with, unchecked ------------------------------ */

/* An id as getMechaBitfieldForAnim maps it: the special codes (0xF7-0xFF)
 * name mechas the copy keeps in range, except 0xFA (mecha 10), past the
 * field's ten; others are taken as they are. */
static int idOk(int id, int battle) {
    int count = battle ? 32 : 10;
    if (id >= 0xF7) {
        return battle || id != 0xFA;
    }
    return id < count;
}

/* A target as updateMechaTargetPosition takes it (m58): 0 none, 1-0x7F the
 * mecha id + 1, 0xFA mecha 0x1F, 0xFB-0xFF codes. */
static int targetOk(int t, int battle) {
    int count = battle ? 32 : 10;
    if (t == 0 || t >= 0xFB) {
        return 1;
    }
    if (t == 0xFA) {
        return battle;
    }
    return t < 0x80 && t - 1 < count;
}

int xgMechaOpSafe(const uint16_t* pc, int words, int battle, int bones) {
    int n = xgMechaOpLength(pc, words, battle);
    if (n <= 0) {
        return 1; /* stops there anyway */
    }
    int op = pc[0] & 0xFF, hi = pc[0] >> 8;
    int a = n > 1 ? pc[1] : 0;
    switch (op) {
    case 0x14: /* the source mecha: the operand's low byte (0xFF: each target its own) */
        return (a & 0xFF) == 0xFF || idOk(a & 0xFF, battle);
    case 0x1F:
    case 0x57:
    case 0x6E:
        return idOk(hi, battle);
    case 0x52:
    case 0x58:
        return n == 1 && !battle ? 1 : targetOk(hi, battle);
    case 0x65:
    case 0x66:
        if (!battle) {
            return 1;
        }
        if ((a >> 8) == 0xF4 || (a >> 8) == 0xF5) {
            return (pc[2] & 0xFF) < 8; /* battle camera presets */
        }
        return (a >> 8) == 0xF6 || idOk(a >> 8, battle);
    case 0x51: /* battleVisualEntities has thirteen entries, not 32 */
        return !battle || hi == 0xFF || hi == 0xFE || hi == 0xFD || hi == 0xFC || hi == 0xF9 || hi < 13;
    case 0x59:
    case 0x5A:
        return !battle || hi < 8;
    /* bones of its own */
    case 0x15:
        return a < bones;
    case 0x1D:
        return hi < bones;
    case 0x23:
    case 0x5D:
    case 0x62:
    case 0x6B:
        return (int16_t)a >= 0 && (int16_t)a < bones;
    case 0x25:
        /* The bone is retained by the attached model even when the op
         * doesn't immediately compute the offset from its matrix. */
        return (a >> 8) < bones;
    case 0x28:
    case 0x29:
        return (a & 0xFF) < bones;
    default:
        return 1;
    }
}

void xgMechaOpNeutralise(uint16_t* pc, int length) {
    /* Same length, nothing done: op 0x30 (zero its own operand word) per two
     * words, op 0x02 (a signal: the field's empty handler, the battle's empty
     * action queue) for the odd one. */
    int at = 0;
    while (length - at >= 2) {
        pc[at] = 0x30;
        pc[at + 1] = 0;
        at += 2;
    }
    if (at < length) {
        pc[at] = 0x02;
    }
}
