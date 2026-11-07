#include "common.h"
#include "main/game_state.h"
#include "field/field.h"

/* main: Noah pKernelGameState */
extern GameState* pKernelGameState;

/* Inventory helpers (f_80091944): item id = category << 8 | index */
/* item-id array of the item's category */
u8* getInventoryItemIds(s32 item);
/* count array of the item's category */
u8* getInventoryItemCounts(s32 item);
/* slot holding the item, -1 if none */
s32 findInventoryItemSlot(s32 item);
/* first free slot of the item's category, -1 if none */
s32 findFreeInventorySlot(s32 item);

s32 addItem(s32 item);

/* opcode 0x34: var[u16 at pc+3] = count of item */
void OP_34(void) {
    s32 item = getImmediateOrVariableUnsigned(1);
    s32 slot = findInventoryItemSlot(item);
    u8* counts = getInventoryItemCounts(item);

    getInventoryItemIds(item);
    if (slot != -1) {
        setVar(readU16FromScript(3), counts[slot]);
    } else {
        setVar(readU16FromScript(3), 0);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

/* opcode 0x8B: jump to u16 at pc+3 unless the item is owned */
void OP_8B(void) {
    if (findInventoryItemSlot(getImmediateOrVariableUnsigned(1)) != -1) {
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC + 5;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(3);
    }
}

/* opcode 0x8C: add one item */
void OP_8C(void) {
    addItem(getImmediateOrVariableUnsigned(1));
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* add one of item to the inventory (count capped at 99).
 * Declared int but returns nothing (K&R implicit int): $v0 stays live at the
 * end, which keeps reorg from hoisting `count + 1` into the first branch's
 * delay slot. $v0 holds the new count, 0 if the count was already 99, or -1
 * if the inventory is full (OP_8C ignores it). */
s32 addItem(s32 item) {
    s32 slot = findInventoryItemSlot(item);
    u8* counts = getInventoryItemCounts(item);
    u8* ids = getInventoryItemIds(item);

    if (slot != -1) {
        if (counts[slot] < 99) {
            counts[slot] = counts[slot] + 1;
        } else {
            IMPLICIT_RETURN(0);
        }
    } else {
        slot = findFreeInventorySlot(item);
        if (slot != -1) {
            ids[slot] = item;
            counts[slot] = 1;
        }
    }
    FALLOFF_RETURN((slot == -1) ? -1 : counts[slot]);
}

/* opcode 0x8D: remove one item */
void OP_8D(void) {
    s32 item = getImmediateOrVariableUnsigned(1);
    s32 slot = findInventoryItemSlot(item);

    if (slot != -1) {
        u8* ids = getInventoryItemIds(item);
        u8* counts = getInventoryItemCounts(item);

        if (--counts[slot] == 0) {
            ids[slot] = 0xFF;
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_IF_CHARACTER_IN_PARTY(void) {
    s32 i;

    for (i = 0; i < 3; i++) {
        if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] == currentParty[i]) {
            pCurrentFieldScriptActor->mCC_scriptPC += 4;
            return;
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(2);
}

void OP_IF_CHARACTER_IN_AVAILABLE_PARTY(void) {
    if ((pKernelGameState->m1D30_partyMemberBitField >> pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) & 1) {
        pCurrentFieldScriptActor->mCC_scriptPC += 4;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(2);
    }
}

void OP_ADD_TO_AVAILABLE_PARTY(void) {
    pKernelGameState->m1D30_partyMemberBitField |= 1 << pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_REMOVE_FROM_AVAILABLE_PARTY(void) {
    pKernelGameState->m1D30_partyMemberBitField &= ~(1 << pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]);
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}
