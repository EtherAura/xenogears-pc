/* Field script variable getters and arithmetic opcodes. gcc-2.7.2-psx -G0,
 * ASPSX 2.34. */
#include "common.h"
#include "psyq.h"
#include "field/field.h"

extern sFieldScriptEntity* pCurrentFieldScriptActor;
extern u8* pCurrentFieldScriptFile;

u16 readU16FromScript(s32);
s16 readS16FromScript(s32);
s32 getImmediateOrVariableUnsigned(s32);
s32 getVariable(s32 varIndex);
void setVar(s32 varIndex, s32 value);
s32 rand(void);

void OP_D1(void) {
}

/* Noah: getVar80 .. getVar01: a script operand that is an immediate s16 when
 * its flag bit is set, else a variable index. */
s32 getVar80(s32 offset, s32 flags) {
    s32 result;
    if (flags & 0x80) {
        result = readS16FromScript(offset);
    } else {
        result = getVariable(readU16FromScript(offset));
    }
    return result;
}

s32 getVar40(s32 offset, s32 flags) {
    s32 result;
    if (flags & 0x40) {
        result = readS16FromScript(offset);
    } else {
        result = getVariable(readU16FromScript(offset));
    }
    return result;
}

s32 getVar20(s32 offset, s32 flags) {
    s32 result;
    if (flags & 0x20) {
        result = readS16FromScript(offset);
    } else {
        result = getVariable(readU16FromScript(offset));
    }
    return result;
}

s32 getVar10(s32 offset, s32 flags) {
    s32 result;
    if (flags & 0x10) {
        result = readS16FromScript(offset);
    } else {
        result = getVariable(readU16FromScript(offset));
    }
    return result;
}

s32 getVar08(s32 offset, s32 flags) {
    s32 result;
    if (flags & 0x8) {
        result = readS16FromScript(offset);
    } else {
        result = getVariable(readU16FromScript(offset));
    }
    return result;
}

s32 getVar04(s32 offset, s32 flags) {
    s32 result;
    if (flags & 0x4) {
        result = readS16FromScript(offset);
    } else {
        result = getVariable(readU16FromScript(offset));
    }
    return result;
}

s32 getVar02(s32 offset, s32 flags) {
    s32 result;
    if (flags & 0x2) {
        result = readS16FromScript(offset);
    } else {
        result = getVariable(readU16FromScript(offset));
    }
    return result;
}

s32 getVar01(s32 offset, s32 flags) {
    s32 result;
    if (flags & 0x1) {
        result = readS16FromScript(offset);
    } else {
        result = getVariable(readU16FromScript(offset));
    }
    return result;
}

#define SCRIPT_BYTE(n) pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + (n)]

void OP_SET_VAR_RANDOM(void) {
    setVar(readU16FromScript(1), rand());
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_A8(void) {
    s32 r;
    r = rand() * (getImmediateOrVariableUnsigned(3) + 1) >> 15;
    setVar(readU16FromScript(1), r);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_SET_VAR_SHIFTRIGHT(void) {
    s32 value;
    value = getVariable(readU16FromScript(1));
    value >>= getImmediateOrVariableUnsigned(3);
    setVar(readU16FromScript(1), value);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_SET_VAR_SHIFTLEFT(void) {
    s32 value;
    value = getVariable(readU16FromScript(1));
    value <<= getImmediateOrVariableUnsigned(3);
    setVar(readU16FromScript(1), value);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_SET_VAR_INC(void) {
    s32 value;
    value = getVariable(readU16FromScript(1)) + 1;
    setVar(readU16FromScript(1), value);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_SET_VAR_DEC(void) {
    s32 value;
    value = getVariable(readU16FromScript(1)) - 1;
    setVar(readU16FromScript(1), value);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_SET_VARBIT_FALSE(void) {
    s32 value;
    value = getVariable(readU16FromScript(1));
    value &= ~(1 << getVar40(3, SCRIPT_BYTE(5)));
    setVar(readU16FromScript(1), value);
    pCurrentFieldScriptActor->mCC_scriptPC += 6;
}

void OP_SET_VAR_XOR(void) {
    s32 value;
    value = getVariable(readU16FromScript(1));
    value ^= getVar40(3, SCRIPT_BYTE(5));
    setVar(readU16FromScript(1), value);
    pCurrentFieldScriptActor->mCC_scriptPC += 6;
}

void OP_SET_VAR_OR(void) {
    s32 value;
    value = getVariable(readU16FromScript(1));
    value |= getVar40(3, SCRIPT_BYTE(5));
    setVar(readU16FromScript(1), value);
    pCurrentFieldScriptActor->mCC_scriptPC += 6;
}

void OP_SET_VAR_AND(void) {
    s32 value;
    value = getVariable(readU16FromScript(1));
    value &= getVar40(3, SCRIPT_BYTE(5));
    setVar(readU16FromScript(1), value);
    pCurrentFieldScriptActor->mCC_scriptPC += 6;
}

void OP_SET_VARBIT_TRUE(void) {
    s32 value;
    value = getVariable(readU16FromScript(1));
    value |= 1 << getVar40(3, SCRIPT_BYTE(5));
    setVar(readU16FromScript(1), value);
    pCurrentFieldScriptActor->mCC_scriptPC += 6;
}

void OP_DE(void) {
    s32 value;
    value = getVariable(readU16FromScript(1));
    value *= getVar40(3, SCRIPT_BYTE(5));
    setVar(readU16FromScript(1), value);
    pCurrentFieldScriptActor->mCC_scriptPC += 6;
}

void OP_DF(void) {
    s32 value;
    s32 divisor;
    value = getVariable(readU16FromScript(1));
    divisor = getVar40(3, SCRIPT_BYTE(5));
    if (divisor == 0) {
        divisor = 1;
    }
    value /= divisor;
    setVar(readU16FromScript(1), value);
    pCurrentFieldScriptActor->mCC_scriptPC += 6;
}

void OP_SET_VAR_SUBTRACT(void) {
    s32 value;
    value = getVariable(readU16FromScript(1));
    value -= getVar40(3, SCRIPT_BYTE(5));
    setVar(readU16FromScript(1), value);
    pCurrentFieldScriptActor->mCC_scriptPC += 6;
}

void OP_SET_VAR_ADD(void) {
    s32 value;
    value = getVariable(readU16FromScript(1));
    value += getVar40(3, SCRIPT_BYTE(5));
    setVar(readU16FromScript(1), value);
    pCurrentFieldScriptActor->mCC_scriptPC += 6;
}

void OP_SET_VAR_FALSE(void) {
    setVar(readU16FromScript(1), 0);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_SET_VAR_TRUE(void) {
    setVar(readU16FromScript(1), 1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_SET_VAR_FROM_S16(void) {
    s32 varIndex;
    varIndex = readU16FromScript(1);
    setVar(varIndex, getVar40(3, SCRIPT_BYTE(5)));
    pCurrentFieldScriptActor->mCC_scriptPC += 6;
}
