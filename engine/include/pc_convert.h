#ifndef PC_CONVERT_H
#define PC_CONVERT_H

/*
 * PC only: disc data that holds 32-bit pointer slots, converted into native
 * structures after it is loaded (port/src/game/<module>/convert*.c).
 *
 * On the PS1 the game turns offsets in the loaded data into pointers in place
 * (doPointerRelocation) and reads them back through pointer-typed fields. On
 * the PC a pointer is 8 bytes, so each format is converted where the game
 * installs it: the loaded bytes stay where they are, unrelocated, and the
 * game's pointer reads go to a native copy of the pointer part. Converted
 * structures live in the game heap, owned like the data they describe, so the
 * game's own frees and heap resets cover them.
 */

#include "common.h"

/* A relocation table as doPointerRelocation takes it ([u32 n] then n u32
 * offsets from the table's start) as native pointers: a new heap block
 * [n][pointer 1]...[pointer n] in 8-byte slots, so the game's (u8**) views
 * index it unchanged. Allocated top-down for the current heap user. */
void** pcRelocTable(u32* data);
/* The same into a buffer of data[0] + 1 slots; returns it. */
void** pcRelocTableInto(void** out, u32* data);
/* Entry i (1..n) of such a table as loaded, where the PS1 reads the
 * relocated pointer once: no native table needed. */
#define PC_RELOC_ENTRY(table, i) ((void*)((u8*)(table) + ((u32*)(table))[i]))

/* 3D models (main/model.h Model): a 0x10-byte header, then m0_numBlocks
 * 0x38-byte blocks whose pointer fields hold offsets from the model's start.
 * pcConvertModel builds the native Model (header and blocks, with the
 * dynamic-vertex tables) in a heap block and keeps its address in the loaded
 * header's m8/mC words, which the game never uses; the native blocks point
 * into the loaded bytes, and m34_primBufferSize is the PC primitive buffer
 * size. pcModelBlocks gives the native blocks of a converted model,
 * pcFreeModel frees its native copy. undoFieldModelRelocation leaves the
 * native copy attached, as the PS1 block keeps its flags and instance data:
 * converting the bytes (or a copy of them) again moves its pointers. */
void pcConvertModel(void* model);
void* pcModelBlocks(void* model);
void pcFreeModel(void* model);
/* m34_primBufferSize of a native block (for code with byte views of it). */
s32 pcModelBlockPrimBufferSize(void* block);
/* main/model.c: the size of the primitive buffer initModel2 fills for these
 * mesh blocks, in the PC layout. */
s32 pcModelPrimBufferSize(u8* meshBlocks, s32 numMeshBlocks);
s32 pcModelRetailPrimBufferSize(u8* meshBlocks, s32 numMeshBlocks);
/* main/model.c: where in such a PC primitive buffer (prims) the byte lies that
 * sits ps1Offset bytes into the PS1 buffer of the same mesh blocks (the PS1's
 * primitives are smaller: 4-byte tags), for retail stores that land in a
 * primitive buffer by address; NULL outside the buffer. */
u8* pcModelPrimBufferAt(u8* meshBlocks, s32 numMeshBlocks, u8* prims, s32 ps1Offset);
/* A lone model block (relocateModelBlock: offsets from the block itself):
 * pcConvertModelBlock builds its native ModelBlock once and keeps its
 * address in the loaded block's m8/mC words; pcModelBlockOf gives it. */
void pcConvertModelBlock(void* raw);
void* pcModelBlockOf(void* raw);

#endif /* PC_CONVERT_H */
