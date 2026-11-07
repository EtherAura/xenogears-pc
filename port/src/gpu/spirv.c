/*
 * A SPIR-V stage's uniform block, push constants and samplers (spirv.h),
 * from its OpName/OpMemberName strings and its Binding, DescriptorSet and
 * Offset decorations. Instruction numbers from the SPIR-V specification
 * (section 3, "Binary Form").
 */

#include "spirv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    OP_NAME = 5, OP_MEMBER_NAME = 6, OP_TYPE_INT = 21, OP_TYPE_FLOAT = 22, OP_TYPE_VECTOR = 23,
    OP_TYPE_MATRIX = 24, OP_TYPE_IMAGE = 25, OP_TYPE_SAMPLED_IMAGE = 27, OP_TYPE_ARRAY = 28,
    OP_TYPE_STRUCT = 30, OP_TYPE_POINTER = 32, OP_VARIABLE = 59, OP_DECORATE = 71, OP_MEMBER_DECORATE = 72,
};
enum { DEC_BLOCK = 2, DEC_BINDING = 33, DEC_SET = 34, DEC_OFFSET = 35 };
enum { SC_UNIFORM_CONSTANT = 0, SC_UNIFORM = 2, SC_PUSH_CONSTANT = 9 };

typedef struct {
    uint32_t op;        /* the defining type instruction, 0 none */
    uint32_t a, b;      /* operands: component type/width, count/signedness */
    uint32_t* members;  /* struct member types */
    uint32_t memberCount;
    const char* name;
    uint32_t binding, set;
    int hasBinding;
    uint32_t storage;   /* variables: storage class; pointers: storage class */
    uint32_t type;      /* variables and pointers: the type pointed to */
} Id;

typedef struct {
    uint32_t type, member;
    const char* name;
    uint32_t offset;
    int hasOffset;
} MemberInfo;

/* The string at word i (null-terminated, padded to words). */
static const char* str(const uint32_t* w, size_t i, size_t end) {
    if (i >= end) {
        return "";
    }
    const char* s = (const char*)&w[i];
    size_t max = (end - i) * 4;
    return memchr(s, '\0', max) != NULL ? s : "";
}

static int memberKind(const Id* ids, uint32_t bound, uint32_t t, uint32_t* size) {
    if (t >= bound) {
        *size = 0;
        return SPV_MEMBER_OTHER;
    }
    const Id* e = &ids[t];
    switch (e->op) {
    case OP_TYPE_FLOAT:
        *size = 4;
        return SPV_MEMBER_FLOAT;
    case OP_TYPE_INT:
        *size = 4;
        return e->b ? SPV_MEMBER_INT : SPV_MEMBER_UINT;
    case OP_TYPE_VECTOR:
        *size = 4 * e->b;
        return e->b == 2 ? SPV_MEMBER_VEC2 : e->b == 3 ? SPV_MEMBER_VEC3 : e->b == 4 ? SPV_MEMBER_VEC4
                                                                                     : SPV_MEMBER_OTHER;
    case OP_TYPE_MATRIX:
        *size = 16 * e->b; /* columns of vec4 (std140 and std430 alike for vec4 columns) */
        return e->b == 4 ? SPV_MEMBER_MAT4 : SPV_MEMBER_OTHER;
    default:
        *size = 16;
        return SPV_MEMBER_OTHER;
    }
}

static void fillBlock(SpvBlock* b, const Id* ids, uint32_t bound, uint32_t structId, const MemberInfo* mi, int miCount) {
    const Id* st = &ids[structId];
    b->present = 1;
    b->members = 0;
    b->size = 0;
    for (uint32_t m = 0; m < st->memberCount && b->members < SPV_MAX_MEMBERS; m++) {
        SpvMember* out = &b->member[b->members];
        memset(out, 0, sizeof(*out));
        for (int i = 0; i < miCount; i++) {
            if (mi[i].type == structId && mi[i].member == m) {
                if (mi[i].name != NULL) {
                    snprintf(out->name, sizeof(out->name), "%s", mi[i].name);
                }
                if (mi[i].hasOffset) {
                    out->offset = mi[i].offset;
                }
            }
        }
        out->kind = memberKind(ids, bound, st->members[m], &out->size);
        if (out->offset + out->size > b->size) {
            b->size = out->offset + out->size;
        }
        b->members++;
    }
}

int spvReflect(const uint32_t* w, size_t count, SpvReflection* out, char* err, size_t errCap) {
    memset(out, 0, sizeof(*out));
    if (count < 5 || w[0] != 0x07230203u) {
        snprintf(err, errCap, "not a SPIR-V module");
        return 0;
    }
    uint32_t bound = w[3];
    if (bound == 0 || bound > (1u << 22)) {
        snprintf(err, errCap, "SPIR-V id bound %u out of range", bound);
        return 0;
    }
    Id* ids = calloc(bound, sizeof(Id));
    MemberInfo* mi = NULL;
    int miCount = 0, miCap = 0;
    int ok = 1;
    for (size_t i = 5; i < count;) {
        uint32_t n = w[i] >> 16, op = w[i] & 0xFFFF;
        if (n == 0 || i + n > count) {
            snprintf(err, errCap, "SPIR-V instruction at word %zu runs past the end", i);
            ok = 0;
            break;
        }
        const uint32_t* a = &w[i + 1];
        size_t end = i + n;
#define ID(x) ((x) < bound ? &ids[(x)] : NULL)
        switch (op) {
        case OP_NAME:
            if (ID(a[0])) {
                ids[a[0]].name = str(w, i + 2, end);
            }
            break;
        case OP_MEMBER_NAME:
        case OP_MEMBER_DECORATE: {
            MemberInfo* e = NULL;
            for (int k = 0; k < miCount; k++) {
                if (mi[k].type == a[0] && mi[k].member == a[1]) {
                    e = &mi[k];
                }
            }
            if (e == NULL) {
                if (miCount == miCap) {
                    miCap = miCap ? miCap * 2 : 128;
                    mi = realloc(mi, (size_t)miCap * sizeof(*mi));
                }
                e = &mi[miCount++];
                memset(e, 0, sizeof(*e));
                e->type = a[0];
                e->member = a[1];
            }
            if (op == OP_MEMBER_NAME) {
                e->name = str(w, i + 3, end);
            } else if (n >= 5 && a[2] == DEC_OFFSET) {
                e->offset = a[3];
                e->hasOffset = 1;
            }
            break;
        }
        case OP_DECORATE:
            if (ID(a[0]) && n >= 4) {
                if (a[1] == DEC_BINDING) {
                    ids[a[0]].binding = a[2];
                    ids[a[0]].hasBinding = 1;
                } else if (a[1] == DEC_SET) {
                    ids[a[0]].set = a[2];
                }
            }
            break;
        case OP_TYPE_INT:
        case OP_TYPE_FLOAT:
        case OP_TYPE_VECTOR:
        case OP_TYPE_MATRIX:
        case OP_TYPE_IMAGE:
        case OP_TYPE_SAMPLED_IMAGE:
        case OP_TYPE_ARRAY:
            if (ID(a[0])) {
                ids[a[0]].op = op;
                ids[a[0]].a = n > 2 ? a[1] : 0;
                ids[a[0]].b = n > 3 ? a[2] : 0;
            }
            break;
        case OP_TYPE_STRUCT:
            if (ID(a[0])) {
                ids[a[0]].op = op;
                ids[a[0]].memberCount = n - 2;
                ids[a[0]].members = (uint32_t*)&a[1]; /* points into the module */
            }
            break;
        case OP_TYPE_POINTER:
            if (ID(a[0])) {
                ids[a[0]].op = op;
                ids[a[0]].storage = a[1];
                ids[a[0]].type = a[2];
            }
            break;
        case OP_VARIABLE:
            if (ID(a[1])) {
                ids[a[1]].op = op;
                ids[a[1]].type = a[0];
                ids[a[1]].storage = a[2];
            }
            break;
        default:
            break;
        }
#undef ID
        i = end;
    }
    for (uint32_t v = 0; ok && v < bound; v++) {
        if (ids[v].op != OP_VARIABLE || ids[v].type >= bound) {
            continue;
        }
        const Id* ptr = &ids[ids[v].type];
        uint32_t pointee = ptr->type < bound ? ptr->type : 0;
        if (ids[v].storage == SC_UNIFORM && ids[pointee].op == OP_TYPE_STRUCT) {
            if (out->ubo.present) {
                snprintf(err, errCap, "more than one uniform block");
                ok = 0;
                break;
            }
            fillBlock(&out->ubo, ids, bound, pointee, mi, miCount);
            out->ubo.set = ids[v].set;
            out->ubo.binding = ids[v].binding;
        } else if (ids[v].storage == SC_PUSH_CONSTANT && ids[pointee].op == OP_TYPE_STRUCT) {
            fillBlock(&out->push, ids, bound, pointee, mi, miCount);
        } else if (ids[v].storage == SC_UNIFORM_CONSTANT && ids[pointee].op == OP_TYPE_SAMPLED_IMAGE) {
            if (out->samplers == SPV_MAX_SAMPLERS) {
                snprintf(err, errCap, "more than %d samplers", SPV_MAX_SAMPLERS);
                ok = 0;
                break;
            }
            SpvSampler* s = &out->sampler[out->samplers++];
            snprintf(s->name, sizeof(s->name), "%s", ids[v].name != NULL ? ids[v].name : "");
            s->set = ids[v].set;
            s->binding = ids[v].binding;
        }
    }
    free(mi);
    free(ids);
    return ok;
}
