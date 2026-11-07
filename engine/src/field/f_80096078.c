#include "common.h"
#include "field/field.h"

void jumpIfMask(u16 mask) {
    if (readU16FromScript(1) & mask) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 5;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(3);
    }
}

/* jumpIfMask's equality twin: pc += 5 if u16 at pc+1 == value, else jump */
void jumpIfEqual(u16 value) {
    if (readU16FromScript(1) == value) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 5;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(3);
    }
}

/* opcode 0xE2 */
void OP_E2(void) {
    jumpIfEqual(padButtonForScripts);
}

/* opcode 0xE3 */
void OP_E3(void) {
    jumpIfEqual(padButtonForScripts2);
}

void OP_JUMP_IF_PAD_MASK(void) {
    jumpIfMask(padButtonForScripts);
}

void OP_JUMP_IF_ACCUMULATED_INPUT_MASK(void) {
    jumpIfMask(padButtonForScripts2);
}

void OP_RESET_ACCUMULATED_INPUT_MASK(void) {
    padButtonForScripts2 = 0;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}
