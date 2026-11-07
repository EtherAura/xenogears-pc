#include "common.h"
#include "main/game_state.h"
#include "field/field.h"

/* main: Noah pKernelGameState */
extern GameState* pKernelGameState;

/* opcode 0x8E: jump to u16 at pc+5 if gold < u32 at pc+1, else pc += 7 */
void OP_8E(void) {
    u16 pc = pCurrentFieldScriptActor->mCC_scriptPC;
    u8* p = pc + pCurrentFieldScriptFile;
    u32 value = p[1] + (p[2] << 8) + (p[3] << 16) + (p[4] << 24);

    if (value <= (u32)pKernelGameState->m1924_gold) {
        pCurrentFieldScriptActor->mCC_scriptPC = pc + 7;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(5);
    }
}

void OP_ADD_GOLD(void) {
    s32 gold = pKernelGameState->m1924_gold + getImmediateOrVariableUnsigned(1);

    if (gold > 9999999) {
        gold = 9999999;
    }
    pKernelGameState->m1924_gold = gold;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* opcode 0x90: remove gold (floored at 0) */
void OP_90(void) {
    s32 amount = getImmediateOrVariableUnsigned(1);
    s32 gold = pKernelGameState->m1924_gold;

    gold -= amount;
    if (gold < 0) {
        gold = 0;
    }
    pKernelGameState->m1924_gold = gold;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}
