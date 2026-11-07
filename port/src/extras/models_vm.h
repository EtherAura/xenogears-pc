#ifndef PORT_EXTRAS_MODELS_VM_H
#define PORT_EXTRAS_MODELS_VM_H

/*
 * The Gear animation VM's bytecode (models_vm.c): op lengths and names as the
 * field's copy (field_mecha processMechaAnimData) and the battle's copy
 * (processMechaAnimData_battle) read them; for the animation viewer's
 * listing and for scanning a program before it plays.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The words the op at pc takes (op and operands; 0x63 its event stream
 * too), within `words` left; 0 if that copy of the VM stops at it (an op it
 * does not know) or it runs past the end. */
int xgMechaOpLength(const uint16_t* pc, int words, int battle);
/* What it does, NULL if that copy does not know it. */
const char* xgMechaOpName(int op, int battle);
/* The battle's copy reads it differently, or only it knows it. */
int xgMechaOpBattleOnly(int op);
/* "op param name operands" into out; returns the op's length (0: stops). */
int xgMechaDisasm(const uint16_t* pc, int words, int battle, char* out, int outSize);

/* Whether the op's mecha ids and bone numbers stay inside the arrays the
 * interpreter indexes with them unchecked (battleMechas's 32 mechas,
 * loadedMechas' 10, the model's `bones`): programs that name others are
 * ones the game never runs (data read as a program). */
int xgMechaOpSafe(const uint16_t* pc, int words, int battle, int bones);
/* Overwrite an op of `length` words with ops that do nothing. */
void xgMechaOpNeutralise(uint16_t* pc, int length);

/* Walk a program's ops in order from its start (until one the copy stops at,
 * or `words`): fn(op, word offset, user) for each, stop when it returns 0.
 * Returns the ops walked. */
typedef int (*xgMechaScanFn)(const uint16_t* op, int at, void* user);
int xgMechaScan(const uint16_t* program, int words, int battle, xgMechaScanFn fn, void* user);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_MODELS_VM_H */
