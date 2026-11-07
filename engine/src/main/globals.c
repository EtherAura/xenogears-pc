/*
 * Shared menu/debug globals: main's first .sdata (0x80059170-0x80059184, JP
 * 0x80058810-0x80058824; it starts at $gp). Data only.
 *
 * Owner evidence (agent DD): cc1 -G8 writes a file's data ahead of all of its
 * code, so a -G8 file reaches its own .sdata through $gp in every function
 * (one-pass ASPSX sees the definition first). game_init and battle_boot reach
 * these through %hi/%lo (and game_init's own small globals through $gp), so
 * neither defines them; kernel.c is cc1 -G0 (its small initialized data is in
 * .data). Nothing else is linked ahead of task's .sdata, so the owner is an
 * object without code that uses them: this data-only file (-G8).
 */
#include "common.h"

extern s32 runningOnDTL;               /* runningOnDTL, in the boot header */

u8 D_80059170 = 0;                   /* unreferenced */
u8 g_menuOpenArg = 0;
s32 D_80059174 = 1;                  /* unreferenced */
u8 useDebugMenuList = 1;
u8 gearSwitchDisabled = 0;                   /* menu lock: field, world map, battle, menu */
s32* pRunningOnDTL = &runningOnDTL;
u8 victoryFanfarePlayed = 0;                   /* battle, battle_result */
