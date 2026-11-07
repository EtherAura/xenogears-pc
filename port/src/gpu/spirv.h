/*
 * What the shader chain needs to know about a compiled shader stage
 * (spirv.c): its uniform block and push constants (member names, offsets,
 * types) and its combined image samplers (names, bindings). Read from the
 * SPIR-V module's names and decorations (the SPIR-V specification's
 * instruction and decoration numbers).
 */
#ifndef PORT_GPU_SPIRV_H
#define PORT_GPU_SPIRV_H

#include <stddef.h>
#include <stdint.h>

enum { SPV_MEMBER_OTHER, SPV_MEMBER_FLOAT, SPV_MEMBER_INT, SPV_MEMBER_UINT, SPV_MEMBER_VEC2, SPV_MEMBER_VEC3,
       SPV_MEMBER_VEC4, SPV_MEMBER_MAT4 };

#define SPV_MAX_MEMBERS 128
#define SPV_MAX_SAMPLERS 64

typedef struct {
    char name[64];
    uint32_t offset, size;
    int kind; /* SPV_MEMBER_* */
} SpvMember;

typedef struct {
    int present;
    uint32_t set, binding; /* the uniform block's (push constants have none) */
    uint32_t size;         /* bytes up to the end of its last member */
    int members;
    SpvMember member[SPV_MAX_MEMBERS];
} SpvBlock;

typedef struct {
    char name[64];
    uint32_t set, binding;
} SpvSampler;

typedef struct {
    SpvBlock ubo, push;
    int samplers;
    SpvSampler sampler[SPV_MAX_SAMPLERS];
} SpvReflection;

/* 1 when the module could be read (err: why not). */
int spvReflect(const uint32_t* words, size_t count, SpvReflection* out, char* err, size_t errCap);

#endif /* PORT_GPU_SPIRV_H */
