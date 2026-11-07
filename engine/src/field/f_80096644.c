#include "common.h"
#include "field/field.h"

void OP_IF_GAMEPROGRESS_LESS(void) {
    s32 value = getImmediateOrVariableUnsigned(1);

    if (getVariable(0) < value) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 5;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(3);
    }
}

void OP_IF_GAMEPROGRESS_GREATER(void) {
    s32 value = getImmediateOrVariableUnsigned(1);

    if (value < getVariable(0)) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 5;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(3);
    }
}

void OP_IF_GAMEPROGRESS_EQUAL(void) {
    s32 value = getImmediateOrVariableUnsigned(1);

    if (getVariable(0) == value) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 5;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(3);
    }
}

void OP_SET_GAMEPROGRESS(void) {
    fieldExectuteMaxCycles += 0x20;
    setVar(0, getImmediateOrVariableUnsigned(1));
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* opcode 0x88: var[u16 at pc+1] = game progress (var 0) */
void OP_88(void) {
    setVar(readU16FromScript(1), getVariable(0));
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}
