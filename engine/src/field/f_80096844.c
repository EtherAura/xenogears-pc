#include "common.h"
#include "main/game_state.h"
#include "field/field.h"

/* main: Noah pKernelGameState */
extern GameState* pKernelGameState;
/* party-slot bit masks by selector (all, slot 0, slot 1, slot 2): {7, 1, 2, 4};
 * also used by f_80086D8C, so not owned here */
extern s16 partySlotSelectionMasks[4];
/* set together by opcode EX 0xAE; an array (or struct): retail's pc load is
 * not scheduled above the stores, so they are in-struct MEMs */
extern FieldBss_800B2184 op9DVar1;

s32 getVar80(s32 offset, s32 flags);

void damagePartyHP(s32 slot, s32 amount);
void addPartyMP(s32 slot, s32 amount);
void drainPartyMP(s32 slot, s32 amount);

/* add HP to party slot, capped at max HP (no caller in retail) */
void addPartyHP(s32 slot, s32 amount) {
    pKernelGameState->m26C_party[currentParty[slot]].m4C_HP += amount;
    if (pKernelGameState->m26C_party[currentParty[slot]].m4E_MaxHP < pKernelGameState->m26C_party[currentParty[slot]].m4C_HP) {
        pKernelGameState->m26C_party[currentParty[slot]].m4C_HP = pKernelGameState->m26C_party[currentParty[slot]].m4E_MaxHP;
    }
}

/* remove HP from party slot, leaving at least 1 */
void damagePartyHP(s32 slot, s32 amount) {
    s32 hp = pKernelGameState->m26C_party[currentParty[slot]].m4C_HP - amount;

    if (hp <= 0) {
        hp = 1;
    }
    pKernelGameState->m26C_party[currentParty[slot]].m4C_HP = hp;
}

/* add MP to party slot, capped at max MP */
void addPartyMP(s32 slot, s32 amount) {
    pKernelGameState->m26C_party[currentParty[slot]].m50_MP += amount;
    if (pKernelGameState->m26C_party[currentParty[slot]].m52_MaxMP < pKernelGameState->m26C_party[currentParty[slot]].m50_MP) {
        pKernelGameState->m26C_party[currentParty[slot]].m50_MP = pKernelGameState->m26C_party[currentParty[slot]].m52_MaxMP;
    }
}

/* remove MP from party slot, leaving at least 1 */
void drainPartyMP(s32 slot, s32 amount) {
    s32 mp = pKernelGameState->m26C_party[currentParty[slot]].m50_MP - amount;

    if (mp <= 0) {
        mp = 1;
    }
    pKernelGameState->m26C_party[currentParty[slot]].m50_MP = mp;
}

/* opcode 0x7B: damage HP of the selected party slots */
void OP_7B(void) {
    s32 amount = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]);
    s32 mask = partySlotSelectionMasks[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3] & 3];
    s32 i;

    for (i = 0; i < 3; i++) {
        if (currentParty[i] != 0xFF && (mask & 1)) {
            damagePartyHP(i, amount);
        }
        mask >>= 1;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

/* opcode EX 0xAE */
void OPX_AE(void) {
    op9DVar1.m1C0_customJumpMode = getImmediateOrVariableUnsigned(1);
    op9DVar1.m1C2_jumpAnimationId = getImmediateOrVariableUnsigned(3);
    op9DVar1.m1BC_jumpCooldownReset = getImmediateOrVariableUnsigned(5);
    op9DVar1.m1BE_jumpCooldown = 0;
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

/* opcode EX 0xAD: var[u16 at pc+1] = HP of party slot (byte at pc+3) */
void OPX_AD(void) {
    if (currentParty[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]] != 0xFF) {
        setVar(readU16FromScript(1),
                      pKernelGameState->m26C_party[currentParty[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]]].m4C_HP);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

/* opcode EX 0xB4: var[u16 at pc+1] = MP of party slot (byte at pc+3) */
void OPX_B4(void) {
    if (currentParty[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]] != 0xFF) {
        setVar(readU16FromScript(1),
                      pKernelGameState->m26C_party[currentParty[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]]].m50_MP);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

/* opcode EX 0xB2: set HP of party slot (byte at pc+1), capped at max HP;
 * retail tests the slot byte at pc+3 (the value's high byte) */
void OPX_B2(void) {
    if (currentParty[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]] != 0xFF) {
        s32 value = getImmediateOrVariableUnsigned(2);
        s32 max = pKernelGameState->m26C_party[currentParty[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]]].m4E_MaxHP;

        if (max < value) {
            value = max;
        }
        pKernelGameState->m26C_party[currentParty[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]]].m4C_HP = value;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

/* opcode EX 0xB3: set MP of party slot (byte at pc+1), capped at max MP */
void OPX_B3(void) {
    if (currentParty[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]] != 0xFF) {
        s32 value = getImmediateOrVariableUnsigned(2);
        s32 max = pKernelGameState->m26C_party[currentParty[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]]].m52_MaxMP;

        if (max < value) {
            value = max;
        }
        pKernelGameState->m26C_party[currentParty[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]]].m50_MP = value;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

/* opcode 0x7C: restore MP of the selected party slots (calls the MP adder,
 * as retail does) */
void OP_7C(void) {
    s32 amount = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]);
    s32 mask = partySlotSelectionMasks[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3] & 3];
    s32 i;

    for (i = 0; i < 3; i++) {
        if (currentParty[i] != 0xFF && (mask & 1)) {
            addPartyMP(i, amount);
        }
        mask >>= 1;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

/* opcode 0x7D: drain MP of the selected party slots */
void OP_7D(void) {
    s32 amount = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]);
    s32 mask = partySlotSelectionMasks[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3] & 3];
    s32 i;

    for (i = 0; i < 3; i++) {
        if (currentParty[i] != 0xFF && (mask & 1)) {
            drainPartyMP(i, amount);
        }
        mask >>= 1;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

/* opcode 0x7E: restore MP of the selected party slots */
void OP_7E(void) {
    s32 amount = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]);
    s32 mask = partySlotSelectionMasks[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3] & 3];
    s32 i;

    for (i = 0; i < 3; i++) {
        if (currentParty[i] != 0xFF && (mask & 1)) {
            addPartyMP(i, amount);
        }
        mask >>= 1;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

/* opcode EX 0xDB: fully restore one character's HP and MP */
void OPX_DB(void) {
    s32 i = getImmediateOrVariableUnsigned(1);

    pKernelGameState->m26C_party[i].m4C_HP = pKernelGameState->m26C_party[i].m4E_MaxHP;
    pKernelGameState->m26C_party[i].m50_MP = pKernelGameState->m26C_party[i].m52_MaxMP;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_RESTORE_HP(void) {
    s32 i;

    for (i = 0; i < 11; i++) {
        pKernelGameState->m26C_party[i].m4C_HP = pKernelGameState->m26C_party[i].m4E_MaxHP;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OP_RESTORE_MP(void) {
    s32 i;

    for (i = 0; i < 11; i++) {
        pKernelGameState->m26C_party[i].m50_MP = pKernelGameState->m26C_party[i].m52_MaxMP;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}
