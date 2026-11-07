/*
 * JV3 STATUS (round 21, JP variants). US: preset (gcc-2.7.2-cdk, ASPSX 2.56).
 * JP: gcc-2.6.3-psx, ASPSX 2.34, -G0, no divide checks (2.6.0 matches every
 * function except advanceCustomPolyPacket; 2.7.2-psx/cdk do not). Check with
 * work/agents/JV3/t.sh <file> gcc-2.6.3-psx (US via --as, JP explicit flags,
 * -I work/agents/JV3/include, JP symbols from jp_extra_symbols.txt).
 * Shared body now (was a JP fallback), OK us/jp:
 *   selectCustomPolySubBuffer getCustomPolySubBufferSize scaleCustomPolyVertices addCustomPolyTPagePrim updateBattleCameraShake
 *   deleteBattleCameraShake getOrCreateBattleCameraShake battleFader_update battleFader_delete battleFader_draw
 *   battleCreateFader2 battleCreateFader getBattleFaderAbe sBattleSpriteEffect_3A_sub_entity_delete sBattleSpriteEffect_3A_sub_entity_draw
 *   battleSpriteEffect_3A_sub
 * OK us/jp with inner #if VERSION_JP: copyCustomPolySubBuffer, tintCustomPolyPrimitives (JP tests
 *   packet byte 2 with an if instead of ((b ^ 1) & 1) << 8)
 *   advanceCustomPolyPacket (JP reads the length byte via a pointer to the cursor inside
 *   a do/while(0) block: the only form found that gives two loads of
 *   customPolyPacketCursor with the first block unscheduled)
 * Every fallback in this file is done.
 * JP symbols needed: see jp_extra_symbols.txt.
 */
/*
 * Custom poly models (Noah customPolyBuffer.cpp), screen shake, battle fader
 * (battleFader.cpp) and volume ramp tasks. 0x800B15D8-0x800B3E04;
 * gcc-2.7.2-cdk, ASPSX 2.56.
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "pc_geometry.h"
#include "stack.h"
#include "main/memory.h"
#include "battle/battle.h"

void registerSpriteCallback2(TaskHeader* owner, TaskHeader* task);
void registerSpriteCallback2Sub0(TaskHeader* task);
void allocateSavePointMeshDataSub0(TaskHeader* owner, TaskHeader* task);
void allocateSavePointMeshDataSub0_callback(TaskHeader* task);
TaskHeader* allocateUpdateTask(TaskHeader* owner, s32 size);
TaskHeader* allocateTaskPair(s32 size, TaskHeader* owner, void (*update)(TaskHeader*), void (*draw)(TaskHeader*), void (*del)(TaskHeader*));
void initBattleEnvColorEffects(void);
void setBattleEnvColorEffect(s32, s32, s32, s32, s32, s32);
void freeBattleEnvColorEffects(void);
s32 addClamp255(s32 value, s32 delta);
extern u8* shapeTransfertTableEnd;
extern u8* shapeTransfertTableCurrentEntry;
extern OTag* characterRenderingOT;
extern u8 spriteBytecode2ExtendedE0_Var0;
extern s32 allocateSavePointMeshDataSub0_var0;
extern DR_TPAGE customPolyTPage;
extern BattleShake* battleCameraShakeTask;
extern sBattleFader* pBattleFader2;
extern sBattleFader* pBattleFader1;
extern u8 battleFaderMode;
extern BattleRamp* battleSpriteEffect_3A_sub_entity;
extern sBattleFader battleFader2;
extern sBattleFader battleFader1;
extern u8 disableBattleFader;
void updateBattleCameraShake(BattleShake* p);
void battleFader_update(sBattleFader* p);
void battleFader_draw(TaskHeader* task);
void battleFader_delete(sBattleFader* fader);
void sBattleSpriteEffect_3A_sub_entity_update(TaskHeader* task);

/* Byte / halfword at a byte offset (packets and primitives vary by type). */
#define B(p, o) (*(u8*)((u8*)(p) + (o)))
#define H(p, o) (*(u16*)((u8*)(p) + (o)))

extern sCustomPolySubBuffer currentCustomPolySubBuffer; /* current sub buffer (a relocated copy) */
extern u8* customPolyPacketCursor;                  /* its packet cursor */
extern s32 customPolyPacketIndex;                  /* its packet index */

/* Selects sub buffer `index` of `buffer` as the current one (a copy with the
 * offsets made pointers, unless the buffer is already relocated) and returns
 * its packet count. */
s32 selectCustomPolySubBuffer(sCustomPolyBuffer* buffer, s32 index);
s32 selectCustomPolySubBuffer(sCustomPolyBuffer* buffer, s32 index) {
    sCustomPolySubBuffer* sub = &buffer->mC_buffers[index];

    currentCustomPolySubBuffer = *sub;
    /* The copy keeps its offsets (32-bit fields); only the packet cursor is
     * read as a pointer. A buffer flagged as relocated cannot hold host
     * pointers in those fields. */
    if (buffer->m4_flags & 1) {
        PORT_TODO("custom poly buffer whose offsets were made pointers");
    }
    customPolyPacketIndex = 0;
    customPolyPacketCursor = (u8*)sub + currentCustomPolySubBuffer.m10_offsetToPackets;
    return sub->m14_count;
}

sCustomPolySubBuffer* getCustomPolySubBuffer(sCustomPolyBuffer* buffer, s32 index) {
    return &buffer->mC_buffers[index];
}

s32 getCustomPolySubBufferSize(sCustomPolySubBuffer* sub);
s32 getCustomPolySubBufferSize(sCustomPolySubBuffer* sub) {
    u32 i;
    s32 size;
    u8* p;
    u32 n;

    i = 0;
    size = 0;
    p = (u8*)(sub->m10_offsetToPackets + (sptr)sub);
    n = sub->m14_count;
    for (; i != n; i++) {
        size += (p[0] + 1) * 4 + PRIM_OFS(0); /* the PC primitive tag is 4 bytes longer */
        p += (p[1] + 1) * 4;
    }
    return size;
}

/* Advances the current sub buffer's packet cursor (selectCustomPolySubBuffer). */
void advanceCustomPolyPacket(void);
void advanceCustomPolyPacket(void) {
#if VERSION_JP
    /* JP reads the packet length through a pointer to the cursor in its own
     * block (two loads of customPolyPacketCursor, the first one unscheduled). */
    u8 n;
    u8** pp = &customPolyPacketCursor;

    do {
        n = (*pp)[1];
    } while (0);
    customPolyPacketCursor += (n + 1) * 4;
#else
    customPolyPacketCursor += (customPolyPacketCursor[1] + 1) * 4;
#endif
    customPolyPacketIndex++;
}

/* Noah: copyCustomPolySubBuffer. Initialises one GPU primitive per packet of
 * `buf` in `dst`: size/code, texture/semi-transparency flags, colours, UVs,
 * CLUT and TPAGE (the TPAGE's blend bits are patched in the packet itself). */
void copyCustomPolySubBuffer(sCustomPolySubBuffer* buf, u8* dst, s32 semiTrans, u8 shadeTex) {
    u8* src;
    s32 count;
    s32 i;
    s32 abr;
    s32 type;

    src = (u8*)(buf->m10_offsetToPackets + (sptr)buf);
    count = buf->m14_count;
    if (semiTrans != 0) {
        abr = (semiTrans - 1) & 3;
        abr <<= 5;
    }
    for (i = 0; i != count; i++) {
        B(dst, PRIM_OFS(3)) = B(src, 0);
        B(dst, PRIM_OFS(7)) = B(src, 3);
        SetShadeTex(dst, shadeTex);
        if (semiTrans != 0) {
            SetSemiTrans(dst, semiTrans != 0);
        }
        /* F3 0x00, FT3 0x04, F4 0x08, FT4 0x0C, G3 0x10, GT3 0x14, G4 0x18,
         * GT4 0x1C; +0x100 when packet byte 2 is 0 (no colours stored). */
        type = B(src, 3) & 0x1C;
#if VERSION_JP
        if (!(B(src, 2) & 1)) {
            type |= 0x100;
        }
#else
        type |= ((B(src, 2) ^ 1) & 1) << 8;
#endif
        switch (type) {
        case 0x10:
        case 0x110:
            B(dst, PRIM_OFS(0x4)) = B(src, 0x4);
            B(dst, PRIM_OFS(0x5)) = B(src, 0x5);
            B(dst, PRIM_OFS(0x6)) = B(src, 0x6);
            B(dst, PRIM_OFS(0xC)) = B(src, 0x8);
            B(dst, PRIM_OFS(0xD)) = B(src, 0x9);
            B(dst, PRIM_OFS(0xE)) = B(src, 0xA);
            B(dst, PRIM_OFS(0x14)) = B(src, 0xC);
            B(dst, PRIM_OFS(0x15)) = B(src, 0xD);
            B(dst, PRIM_OFS(0x16)) = B(src, 0xE);
            break;
        case 0x18:
        case 0x118:
            B(dst, PRIM_OFS(0x4)) = B(src, 0x4);
            B(dst, PRIM_OFS(0x5)) = B(src, 0x5);
            B(dst, PRIM_OFS(0x6)) = B(src, 0x6);
            B(dst, PRIM_OFS(0xC)) = B(src, 0x8);
            B(dst, PRIM_OFS(0xD)) = B(src, 0x9);
            B(dst, PRIM_OFS(0xE)) = B(src, 0xA);
            B(dst, PRIM_OFS(0x14)) = B(src, 0xC);
            B(dst, PRIM_OFS(0x15)) = B(src, 0xD);
            B(dst, PRIM_OFS(0x16)) = B(src, 0xE);
            B(dst, PRIM_OFS(0x1C)) = B(src, 0x10);
            B(dst, PRIM_OFS(0x1D)) = B(src, 0x11);
            B(dst, PRIM_OFS(0x1E)) = B(src, 0x12);
            break;
        case 0x00:
        case 0x08:
        case 0x100:
        case 0x108:
            B(dst, PRIM_OFS(4)) = B(src, 4);
            B(dst, PRIM_OFS(5)) = B(src, 5);
            B(dst, PRIM_OFS(6)) = B(src, 6);
            break;
        case 0x104:
            if (semiTrans != 0) {
                H(src, 0xA) = (H(src, 0xA) & 0xFF9F) | abr;
            }
            B(dst, PRIM_OFS(4)) = 0x80;
            B(dst, PRIM_OFS(5)) = 0x80;
            B(dst, PRIM_OFS(6)) = 0x80;
            H(dst, PRIM_OFS(0x16)) = H(src, 0xA);
            H(dst, PRIM_OFS(0xE)) = H(src, 0x6);
            B(dst, PRIM_OFS(0xC)) = B(src, 0x4);
            B(dst, PRIM_OFS(0xD)) = B(src, 0x5);
            B(dst, PRIM_OFS(0x14)) = B(src, 0x8);
            B(dst, PRIM_OFS(0x15)) = B(src, 0x9);
            B(dst, PRIM_OFS(0x1C)) = B(src, 0xC);
            B(dst, PRIM_OFS(0x1D)) = B(src, 0xD);
            break;
        case 0x04:
            if (semiTrans != 0) {
                H(src, 0xA) = (H(src, 0xA) & 0xFF9F) | abr;
            }
            B(dst, PRIM_OFS(4)) = B(src, 0x10);
            B(dst, PRIM_OFS(5)) = B(src, 0x11);
            B(dst, PRIM_OFS(6)) = B(src, 0x12);
            H(dst, PRIM_OFS(0x16)) = H(src, 0xA);
            H(dst, PRIM_OFS(0xE)) = H(src, 0x6);
            B(dst, PRIM_OFS(0xC)) = B(src, 0x4);
            B(dst, PRIM_OFS(0xD)) = B(src, 0x5);
            B(dst, PRIM_OFS(0x14)) = B(src, 0x8);
            B(dst, PRIM_OFS(0x15)) = B(src, 0x9);
            B(dst, PRIM_OFS(0x1C)) = B(src, 0xC);
            B(dst, PRIM_OFS(0x1D)) = B(src, 0xD);
            break;
        case 0x114:
            if (semiTrans != 0) {
                H(src, 0xA) = (H(src, 0xA) & 0xFF9F) | abr;
            }
            B(dst, PRIM_OFS(0x4)) = 0x80;
            B(dst, PRIM_OFS(0x5)) = 0x80;
            B(dst, PRIM_OFS(0x6)) = 0x80;
            B(dst, PRIM_OFS(0x10)) = 0x80;
            B(dst, PRIM_OFS(0x11)) = 0x80;
            B(dst, PRIM_OFS(0x12)) = 0x80;
            B(dst, PRIM_OFS(0x1C)) = 0x80;
            B(dst, PRIM_OFS(0x1D)) = 0x80;
            B(dst, PRIM_OFS(0x1E)) = 0x80;
            H(dst, PRIM_OFS(0x1A)) = H(src, 0xA);
            H(dst, PRIM_OFS(0xE)) = H(src, 0x6);
            B(dst, PRIM_OFS(0xC)) = B(src, 0x4);
            B(dst, PRIM_OFS(0xD)) = B(src, 0x5);
            B(dst, PRIM_OFS(0x18)) = B(src, 0x8);
            B(dst, PRIM_OFS(0x19)) = B(src, 0x9);
            B(dst, PRIM_OFS(0x24)) = B(src, 0xC);
            B(dst, PRIM_OFS(0x25)) = B(src, 0xD);
            break;
        case 0x14:
            if (semiTrans != 0) {
                H(src, 0xA) = (H(src, 0xA) & 0xFF9F) | abr;
            }
            B(dst, PRIM_OFS(0x4)) = B(src, 0x10);
            B(dst, PRIM_OFS(0x5)) = B(src, 0x11);
            B(dst, PRIM_OFS(0x6)) = B(src, 0x12);
            B(dst, PRIM_OFS(0x10)) = B(src, 0x14);
            B(dst, PRIM_OFS(0x11)) = B(src, 0x15);
            B(dst, PRIM_OFS(0x12)) = B(src, 0x16);
            B(dst, PRIM_OFS(0x1C)) = B(src, 0x18);
            B(dst, PRIM_OFS(0x1D)) = B(src, 0x19);
            B(dst, PRIM_OFS(0x1E)) = B(src, 0x1A);
            H(dst, PRIM_OFS(0x1A)) = H(src, 0xA);
            H(dst, PRIM_OFS(0xE)) = H(src, 0x6);
            B(dst, PRIM_OFS(0xC)) = B(src, 0x4);
            B(dst, PRIM_OFS(0xD)) = B(src, 0x5);
            B(dst, PRIM_OFS(0x18)) = B(src, 0x8);
            B(dst, PRIM_OFS(0x19)) = B(src, 0x9);
            B(dst, PRIM_OFS(0x24)) = B(src, 0xC);
            B(dst, PRIM_OFS(0x25)) = B(src, 0xD);
            break;
        case 0x10C:
            if (semiTrans != 0) {
                H(src, 0xA) = (H(src, 0xA) & 0xFF9F) | abr;
            }
            B(dst, PRIM_OFS(4)) = 0x80;
            B(dst, PRIM_OFS(5)) = 0x80;
            B(dst, PRIM_OFS(6)) = 0x80;
            H(dst, PRIM_OFS(0x16)) = H(src, 0xA);
            H(dst, PRIM_OFS(0xE)) = H(src, 0x6);
            B(dst, PRIM_OFS(0xC)) = B(src, 0x4);
            B(dst, PRIM_OFS(0xD)) = B(src, 0x5);
            B(dst, PRIM_OFS(0x14)) = B(src, 0x8);
            B(dst, PRIM_OFS(0x15)) = B(src, 0x9);
            B(dst, PRIM_OFS(0x1C)) = B(src, 0xC);
            B(dst, PRIM_OFS(0x1D)) = B(src, 0xD);
            B(dst, PRIM_OFS(0x24)) = B(src, 0x10);
            B(dst, PRIM_OFS(0x25)) = B(src, 0x11);
            break;
        case 0x0C:
            if (semiTrans != 0) {
                H(src, 0xA) = (H(src, 0xA) & 0xFF9F) | abr;
            }
            B(dst, PRIM_OFS(4)) = B(src, 0x14);
            B(dst, PRIM_OFS(5)) = B(src, 0x15);
            B(dst, PRIM_OFS(6)) = B(src, 0x16);
            H(dst, PRIM_OFS(0x16)) = H(src, 0xA);
            H(dst, PRIM_OFS(0xE)) = H(src, 0x6);
            B(dst, PRIM_OFS(0xC)) = B(src, 0x4);
            B(dst, PRIM_OFS(0xD)) = B(src, 0x5);
            B(dst, PRIM_OFS(0x14)) = B(src, 0x8);
            B(dst, PRIM_OFS(0x15)) = B(src, 0x9);
            B(dst, PRIM_OFS(0x1C)) = B(src, 0xC);
            B(dst, PRIM_OFS(0x1D)) = B(src, 0xD);
            B(dst, PRIM_OFS(0x24)) = B(src, 0x10);
            B(dst, PRIM_OFS(0x25)) = B(src, 0x11);
            break;
        case 0x11C:
            if (semiTrans != 0) {
                H(src, 0xA) = (H(src, 0xA) & 0xFF9F) | abr;
            }
            B(dst, PRIM_OFS(0x4)) = 0x80;
            B(dst, PRIM_OFS(0x5)) = 0x80;
            B(dst, PRIM_OFS(0x6)) = 0x80;
            B(dst, PRIM_OFS(0x10)) = 0x80;
            B(dst, PRIM_OFS(0x11)) = 0x80;
            B(dst, PRIM_OFS(0x12)) = 0x80;
            B(dst, PRIM_OFS(0x1C)) = 0x80;
            B(dst, PRIM_OFS(0x1D)) = 0x80;
            B(dst, PRIM_OFS(0x1E)) = 0x80;
            B(dst, PRIM_OFS(0x28)) = 0x80;
            B(dst, PRIM_OFS(0x29)) = 0x80;
            B(dst, PRIM_OFS(0x2A)) = 0x80;
            H(dst, PRIM_OFS(0x1A)) = H(src, 0xA);
            H(dst, PRIM_OFS(0xE)) = H(src, 0x6);
            B(dst, PRIM_OFS(0xC)) = B(src, 0x4);
            B(dst, PRIM_OFS(0xD)) = B(src, 0x5);
            B(dst, PRIM_OFS(0x18)) = B(src, 0x8);
            B(dst, PRIM_OFS(0x19)) = B(src, 0x9);
            B(dst, PRIM_OFS(0x24)) = B(src, 0xC);
            B(dst, PRIM_OFS(0x25)) = B(src, 0xD);
            B(dst, PRIM_OFS(0x30)) = B(src, 0x10);
            B(dst, PRIM_OFS(0x31)) = B(src, 0x11);
            break;
        case 0x1C:
            if (semiTrans != 0) {
                H(src, 0xA) = (H(src, 0xA) & 0xFF9F) | abr;
            }
            B(dst, PRIM_OFS(0x4)) = B(src, 0x14);
            B(dst, PRIM_OFS(0x5)) = B(src, 0x15);
            B(dst, PRIM_OFS(0x6)) = B(src, 0x16);
            B(dst, PRIM_OFS(0x10)) = B(src, 0x18);
            B(dst, PRIM_OFS(0x11)) = B(src, 0x19);
            B(dst, PRIM_OFS(0x12)) = B(src, 0x1A);
            B(dst, PRIM_OFS(0x1C)) = B(src, 0x1C);
            B(dst, PRIM_OFS(0x1D)) = B(src, 0x1D);
            B(dst, PRIM_OFS(0x1E)) = B(src, 0x1E);
            B(dst, PRIM_OFS(0x28)) = B(src, 0x20);
            B(dst, PRIM_OFS(0x29)) = B(src, 0x21);
            B(dst, PRIM_OFS(0x2A)) = B(src, 0x22);
            H(dst, PRIM_OFS(0x1A)) = H(src, 0xA);
            H(dst, PRIM_OFS(0xE)) = H(src, 0x6);
            B(dst, PRIM_OFS(0xC)) = B(src, 0x4);
            B(dst, PRIM_OFS(0xD)) = B(src, 0x5);
            B(dst, PRIM_OFS(0x18)) = B(src, 0x8);
            B(dst, PRIM_OFS(0x19)) = B(src, 0x9);
            B(dst, PRIM_OFS(0x24)) = B(src, 0xC);
            B(dst, PRIM_OFS(0x25)) = B(src, 0xD);
            B(dst, PRIM_OFS(0x30)) = B(src, 0x10);
            B(dst, PRIM_OFS(0x31)) = B(src, 0x11);
            break;
        }
        dst += (B(src, 0) + 1) * 4 + PRIM_OFS(0);
        src += (B(src, 1) + 1) * 4;
    }
}

void scaleCustomPolyVertices(sCustomPolySubBuffer* p, s32 shift);
void scaleCustomPolyVertices(sCustomPolySubBuffer* p, s32 shift) {
    SVECTOR* v;
    s32 i;
    s32 n;

    if (!(p->m18_flags & 0x8000)) {
        p->m18_flags |= 0x8000;
        v = (SVECTOR*)(p->m0_offsetToVertices + (sptr)p);
        n = p->m4_numVertices;
        for (i = 0; i != n; i++) {
            v[i].vx <<= shift;
            v[i].vy <<= shift;
            v[i].vz <<= shift;
        }
    }
}

void addCustomPolyTPagePrim(OTag* ot);
void addCustomPolyTPagePrim(OTag* ot) {
    DR_TPAGE* p = (DR_TPAGE*)shapeTransfertTableCurrentEntry;

    if ((u8*)(p + 1) < shapeTransfertTableEnd) {
        shapeTransfertTableCurrentEntry = (u8*)(p + 1);
        *p = customPolyTPage;
        AddPrim(ot, p);
    }
}

/* Project, shade and link custom polygon packets. US and JP use different
 * compiler versions and projection sequences, so keep their bodies separate.
 * The cursor first holds the output buffer, then the input packet stream. The
 * four single-pass scopes around its first assignment preserve the old GCC
 * register priority for these real pointer roles. Without them, combining the
 * header/payload stride coalesces the cursor with dst and changes the code.
 * Both regional bodies are verified against every retail instruction. */
#if VERSION_JP
s32 RotAverageNclip4(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* v3, s32* sxy0, s32* sxy1, s32* sxy2,
                     s32* sxy3, s32* p, s32* otz, s32* flag);
void addCustomPolyTPagePrim(OTag* ot);
extern s32 gDepthDivider;
extern DR_TPAGE customPolyTPage;


#include "pc_custom_geometry.h"

#define VTX(base, o) ((SVECTOR*)(H(cursor.m0_bytes, o) * 8 + (sptr)(base)))
static inline SVECTOR* decode_vertex(SVECTOR* base, u16 index) { return (SVECTOR*)(index * 8 + (sptr)base); }
static inline void shadeOne(SVECTOR* normal, u8* primitive) {
    u8 saved = primitive[PRIM_OFS(7)];
    NormalColor(normal, (CVECTOR*)(primitive + PRIM_OFS(4)));
    primitive[PRIM_OFS(7)] = saved;
}
static inline void shadeThree(SVECTOR* n0, SVECTOR* n1, SVECTOR* n2, u8* primitive) {
    u8 saved = primitive[PRIM_OFS(7)];
    NormalColor3(n0, n1, n2, (CVECTOR*)(primitive + PRIM_OFS(4)), (CVECTOR*)(primitive + PRIM_OFS(0x10)), (CVECTOR*)(primitive + PRIM_OFS(0x1C)));
    primitive[PRIM_OFS(7)] = saved;
}

/* Noah: renderCustomPolyBuffer. */
void renderCustomPolyBuffer(sCustomPolySubBuffer* buf, u8* src, OTag* ot, s32 unused, s32 depthBias, s32 semiTrans) {
    u8* dst;
    struct { u8* m0_bytes; } cursor;
    s32 type;
    SVECTOR* verts;
    SVECTOR* normals;
    sptr v0;
    SVECTOR* v1;
    SVECTOR* v2;
    SVECTOR* v3;
    s32 count;
    s32 i;
    s32 opz;
    s32 otz;
    s32 p;
    s32 flag;
    u8 code;
    u8* geometryDst;
    GteState geometrySaved;
    int geometryRescue;
    v3 = 0; /* Only quad cases use v3; every quad case assigns it below. */

    if (semiTrans != 0) {
        SetDrawTPage(&customPolyTPage, 0, 0, ((semiTrans - 1) & 3) << 5);
    }
    do {
        do {
            do {
                do {
                    cursor.m0_bytes = src;
                } while (0);
            } while (0);
        } while (0);
    } while (0);
    dst = cursor.m0_bytes;
    i = 0;
    cursor.m0_bytes = (u8*)(buf->m10_offsetToPackets + (sptr)buf);
    verts = (SVECTOR*)(buf->m0_offsetToVertices + (sptr)buf);
    count = buf->m14_count;
    normals = (SVECTOR*)(buf->m8_offsetToNormals + (sptr)buf);
    for (; i != count; i++) {
        type = B(cursor.m0_bytes, 3) & 0x1C;
#if VERSION_JP
        if (!(B(cursor.m0_bytes, 2) & 1)) {
            type |= 0x100;
        }
#else
        type |= ((B(cursor.m0_bytes, 2) ^ 1) & 1) << 8;
#endif
        switch (type) {
        case 0x00:
            v0 = (sptr)VTX(verts, 0x8);
            v1 = VTX(verts, 0xA);
            v2 = VTX(verts, 0xC);
            break;
        case 0x10:
            v0 = (sptr)VTX(verts, 0x10);
            v1 = VTX(verts, 0x12);
            v2 = VTX(verts, 0x14);
            break;
        case 0x18:
            v0 = (sptr)VTX(verts, 0x14);
            v1 = VTX(verts, 0x16);
            v2 = VTX(verts, 0x18);
            v3 = VTX(verts, 0x1A);
            break;
        case 0x08:
            v0 = (sptr)VTX(verts, 0x8);
            v1 = VTX(verts, 0xA);
            v2 = VTX(verts, 0xC);
            v3 = VTX(verts, 0xE);
            break;
        case 0x04:
            v0 = (sptr)VTX(verts, 0x14);
            v1 = VTX(verts, 0x16);
            v2 = VTX(verts, 0x18);
            break;
        case 0x14:
            v0 = (sptr)VTX(verts, 0x1C);
            v1 = VTX(verts, 0x1E);
            v2 = VTX(verts, 0x20);
            break;
        case 0x0C:
            v0 = (sptr)VTX(verts, 0x18);
            v1 = VTX(verts, 0x1A);
            v2 = VTX(verts, 0x1C);
            v3 = VTX(verts, 0x1E);
            break;
        case 0x1C:
            v0 = (sptr)VTX(verts, 0x24);
            v1 = VTX(verts, 0x26);
            v2 = VTX(verts, 0x28);
            v3 = VTX(verts, 0x2A);
            break;
        case 0x100:
            shadeOne(VTX(normals, 0x8), dst);
            v0 = (sptr)verts; v0 = (sptr)decode_vertex((SVECTOR*)v0, H(cursor.m0_bytes, 0xA));
            v1 = VTX(verts, 0xC);
            v2 = VTX(verts, 0xE);
            break;
        case 0x110:
            v0 = (sptr)verts; v0 = (sptr)decode_vertex((SVECTOR*)v0, H(cursor.m0_bytes, 0x10));
            v1 = VTX(verts, 0x16);
            v2 = VTX(verts, 0x1A);
            break;
        case 0x108:
            shadeOne(VTX(normals, 0x8), dst);
            v0 = (sptr)VTX(verts, 0xA);
            v1 = VTX(verts, 0xC);
            v2 = VTX(verts, 0xE);
            v3 = VTX(verts, 0x10);
            break;
        case 0x104:
            shadeOne(VTX(normals, 0x10), dst);
            v0 = (sptr)verts; v0 = (sptr)decode_vertex((SVECTOR*)v0, H(cursor.m0_bytes, 0x12));
            v1 = VTX(verts, 0x14);
            v2 = VTX(verts, 0x16);
            break;
        case 0x114:
            shadeThree(VTX(normals, 0x10), VTX(normals, 0x14), VTX(normals, 0x18), dst);
            v0 = (sptr)verts; v0 = (sptr)decode_vertex((SVECTOR*)v0, H(cursor.m0_bytes, 0x12));
            v1 = VTX(verts, 0x16);
            v2 = VTX(verts, 0x1A);
            break;
        case 0x10C:
            v0 = (sptr)VTX(verts, 0x16);
            v1 = VTX(verts, 0x18);
            v2 = VTX(verts, 0x1A);
            v3 = VTX(verts, 0x1C);
            break;
        case 0x118:
        case 0x11C:
            v0 = (sptr)VTX(verts, 0x16);
            v1 = VTX(verts, 0x1A);
            v2 = VTX(verts, 0x1E);
            v3 = VTX(verts, 0x22);
            break;
        }
        geometryDst = dst;
        geometryRescue = 0;
        switch (B(cursor.m0_bytes, 3) & 0x1C) {
        case 0x00:
            TRI(0xC, 0x10, TPAGE_TAIL)
        case 0x10:
            TRI(0x10, 0x18, TPAGE_TAIL)
        case 0x04:
            TRI(0x10, 0x18, )
        case 0x14:
            TRI(0x14, 0x20, )
        case 0x08:
            QUAD(0xC, 0x10, 0x14, TPAGE_TAIL)
        case 0x18:
            QUAD(0x10, 0x18, 0x20, TPAGE_TAIL)
        case 0x0C:
            QUAD(0x10, 0x18, 0x20, )
        case 0x1C:
            QUAD(0x14, 0x20, 0x2C, )
        }
        if (geometryRescue) { gteLoad(&geometrySaved); dst = geometryDst; }
        dst += (B(cursor.m0_bytes, 0) + 1) * 4 + PRIM_OFS(0);
        cursor.m0_bytes += (B(cursor.m0_bytes, 1) + 1) * 4;
    }
}


#else
s32 RotAverageNclip4(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* v3, s32* sxy0, s32* sxy1, s32* sxy2,
                     s32* sxy3, s32* p, s32* otz, s32* flag);
void addCustomPolyTPagePrim(OTag* ot);
extern s32 gDepthDivider;
extern DR_TPAGE customPolyTPage;

#include "pc_custom_geometry.h"

#define VTX(base, o) ((SVECTOR*)(H(cursor.m0_packet, o) * 8 + (sptr)(base)))
static __inline__ SVECTOR* decode_vertex(SVECTOR* base, u16 index) {
    return (SVECTOR*)(index * 8 + (sptr)base);
}
static inline void shadeThree(SVECTOR* n0, SVECTOR* n1, SVECTOR* n2, u8* primitive) { u8 saved=primitive[PRIM_OFS(7)]; NormalColor3(n0,n1,n2,(CVECTOR*)(primitive + PRIM_OFS(4)),(CVECTOR*)(primitive + PRIM_OFS(0x10)),(CVECTOR*)(primitive + PRIM_OFS(0x1C))); primitive[PRIM_OFS(7)]=saved; }

static inline void shadeOne(SVECTOR* normal, u8* primitive) { u8 saved=primitive[PRIM_OFS(7)]; NormalColor(normal,(CVECTOR*)(primitive + PRIM_OFS(4))); primitive[PRIM_OFS(7)]=saved; }

/* Noah: renderCustomPolyBuffer. */
void renderCustomPolyBuffer(sCustomPolySubBuffer* buf, u8* src, OTag* ot, s32 unused, s32 depthBias, s32 semiTrans) {
    u8* dst;
    struct {u8* m0_packet;} cursor;
    s32 type;
    SVECTOR* verts;
    SVECTOR* normals;
    sptr v0;
    u8 code;
    SVECTOR* v1;
    SVECTOR* v2;
    SVECTOR* v3;
    s32 count;
    s32 i;
    s32 opz;
    s32 otz;
    s32 p;
    s32 flag;
    u8* geometryDst;
    GteState geometrySaved;
    int geometryRescue;
    v3 = 0; /* Only quad cases use v3; every quad case assigns it below. */

    if (semiTrans != 0) {
        SetDrawTPage(&customPolyTPage, 0, 0, ((semiTrans - 1) & 3) << 5);
    }
    do {
        do {
            do {
                do {
                    cursor.m0_packet = src;
                } while (0);
            } while (0);
        } while (0);
    } while (0);
    dst = cursor.m0_packet;
    i = 0;
    cursor.m0_packet = (u8*)(buf->m10_offsetToPackets+(sptr)buf);
    verts = (SVECTOR*)(buf->m0_offsetToVertices + (sptr)buf);
    count = buf->m14_count;
    normals = (SVECTOR*)(buf->m8_offsetToNormals + (sptr)buf);
    for (; i != count; i++) {
        type = B(cursor.m0_packet, 3) & 0x1C;
#if VERSION_JP
        if (!(B(cursor.m0_packet, 2) & 1)) {
            type |= 0x100;
        }
#else
        type |= ((B(cursor.m0_packet, 2) ^ 1) & 1) << 8;
#endif
        switch (type) {
        case 0x00:
            v0 = H(cursor.m0_packet, 0x8); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0xA);
            v2 = VTX(verts, 0xC);
            break;
        case 0x10:
            v0 = H(cursor.m0_packet, 0x10); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0x12);
            v2 = VTX(verts, 0x14);
            break;
        case 0x18:
            v0 = H(cursor.m0_packet, 0x14); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0x16);
            v2 = VTX(verts, 0x18);
            v3 = VTX(verts, 0x1A);
            break;
        case 0x08:
            v0 = H(cursor.m0_packet, 0x8); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0xA);
            v2 = VTX(verts, 0xC);
            v3 = VTX(verts, 0xE);
            break;
        case 0x04:
            v0 = H(cursor.m0_packet, 0x14); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0x16);
            v2 = VTX(verts, 0x18);
            break;
        case 0x14:
            v0 = H(cursor.m0_packet, 0x1C); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0x1E);
            v2 = VTX(verts, 0x20);
            break;
        case 0x0C:
            v0 = H(cursor.m0_packet, 0x18); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0x1A);
            v2 = VTX(verts, 0x1C);
            v3 = VTX(verts, 0x1E);
            break;
        case 0x1C:
            v0 = H(cursor.m0_packet, 0x24); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0x26);
            v2 = VTX(verts, 0x28);
            v3 = VTX(verts, 0x2A);
            break;
        case 0x100:
            shadeOne(VTX(normals, 0x8), dst);
            v0 = H(cursor.m0_packet, 0xA); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0xC);
            v2 = VTX(verts, 0xE);
            break;
        case 0x110:
            v0 = H(cursor.m0_packet, 0x10); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0x16);
            v2 = VTX(verts, 0x1A);
            break;
        case 0x108:
            shadeOne(VTX(normals, 0x8), dst);
            v0 = H(cursor.m0_packet, 0xA); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0xC);
            v2 = VTX(verts, 0xE);
            v3 = VTX(verts, 0x10);
            break;
        case 0x104:
            shadeOne(VTX(normals, 0x10), dst);
            v0 = H(cursor.m0_packet, 0x12); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0x14);
            v2 = VTX(verts, 0x16);
            break;
        case 0x114:
            shadeThree(VTX(normals, 0x10), VTX(normals, 0x14), VTX(normals, 0x18), dst);
            v0 = H(cursor.m0_packet, 0x12); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0x16);
            v2 = VTX(verts, 0x1A);
            break;
        case 0x10C:
            v0 = H(cursor.m0_packet, 0x16); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0x18);
            v2 = VTX(verts, 0x1A);
            v3 = VTX(verts, 0x1C);
            break;
        case 0x118:
        case 0x11C:
            v0 = H(cursor.m0_packet, 0x16); v0 = (sptr)decode_vertex(verts, v0);
            v1 = VTX(verts, 0x1A);
            v2 = VTX(verts, 0x1E);
            v3 = VTX(verts, 0x22);
            break;
        }
        geometryDst = dst;
        geometryRescue = 0;
        type = B(cursor.m0_packet, 3) & 0x1C;
        switch (type) {
        case 0x00:
            TRI(0xC, 0x10, TPAGE_TAIL)
        case 0x10:
            TRI(0x10, 0x18, TPAGE_TAIL)
        case 0x04:
            TRI(0x10, 0x18, )
        case 0x14:
            TRI(0x14, 0x20, )
        case 0x08:
            QUAD(0xC, 0x10, 0x14, TPAGE_TAIL)
        case 0x18:
            QUAD(0x10, 0x18, 0x20, TPAGE_TAIL)
        case 0x0C:
            QUAD(0x10, 0x18, 0x20, )
        case 0x1C:
            QUAD(0x14, 0x20, 0x2C, )
        }
        if (geometryRescue) { gteLoad(&geometrySaved); dst = geometryDst; }
        dst += (B(cursor.m0_packet, 0) + 1) * 4 + PRIM_OFS(0);
        cursor.m0_packet += (B(cursor.m0_packet, 1) + 1) * 4;
    }
}

#endif
#undef TRI
#undef QUAD
#undef TPAGE_TAIL
#undef VTX
#undef CUSTOM_RESCUE_BEGIN


/* Colour triple at prim offset d = packet colour at s plus (dr, dg, db). */
#define C3(d, s)                                   \
    B(dst0, PRIM_OFS((d) + 0)) = addClamp255(B(src, (s) + 0), dr); \
    B(dst0, PRIM_OFS((d) + 1)) = addClamp255(B(src, (s) + 1), dg); \
    B(dst0, PRIM_OFS((d) + 2)) = addClamp255(B(src, (s) + 2), db);
/* Same from white (0x80) for packets without colours. */
#define W3(d)                                      \
    B(dst0, PRIM_OFS((d) + 0)) = addClamp255(0x80, dr);      \
    B(dst0, PRIM_OFS((d) + 1)) = addClamp255(0x80, dg);      \
    B(dst0, PRIM_OFS((d) + 2)) = addClamp255(0x80, db);
/* Copy a colour triple to the second buffer. */
#define K3(d)                                      \
    B(dst1, PRIM_OFS((d) + 0)) = B(dst0, PRIM_OFS((d) + 0));           \
    B(dst1, PRIM_OFS((d) + 1)) = B(dst0, PRIM_OFS((d) + 1));           \
    B(dst1, PRIM_OFS((d) + 2)) = B(dst0, PRIM_OFS((d) + 2));

/* Tints both primitive buffers of `buf` (packet colour + delta, clamped). */
void tintCustomPolyPrimitives(sCustomPolySubBuffer* buf, u8* dst0, u8* dst1, s32 dr, s32 dg, s32 db);
void tintCustomPolyPrimitives(sCustomPolySubBuffer* buf, u8* dst0, u8* dst1, s32 dr, s32 dg, s32 db) {
    u8* src;
    s32 count;
    s32 i;
    s32 type;

    i = 0;
    src = (u8*)(buf->m10_offsetToPackets + (sptr)buf);
    count = buf->m14_count;
    for (; i != count; i++) {
        type = B(src, 3) & 0x1C;
#if VERSION_JP
        if (!(B(src, 2) & 1)) {
            type |= 0x100;
        }
#else
        type |= ((B(src, 2) ^ 1) & 1) << 8;
#endif
        switch (type) {
        case 0x00:
            C3(4, 4)
            K3(4)
            break;
        case 0x10:
            C3(4, 4)
            C3(0xC, 8)
            C3(0x14, 0xC)
            K3(4)
            K3(0xC)
            K3(0x14)
            break;
        case 0x18:
            C3(4, 4)
            C3(0xC, 8)
            C3(0x14, 0xC)
            C3(0x1C, 0x10)
            K3(4)
            K3(0xC)
            K3(0x14)
            K3(0x1C)
            break;
        case 0x08:
            C3(4, 4)
            K3(4)
            break;
        case 0x104:
            W3(4)
            K3(4)
            break;
        case 0x04:
            C3(4, 0x10)
            K3(4)
            break;
        case 0x114:
            W3(4)
            W3(0x10)
            W3(0x1C)
            K3(4)
            K3(0x10)
            K3(0x1C)
            break;
        case 0x14:
            C3(4, 0x10)
            C3(0x10, 0x14)
            C3(0x1C, 0x18)
            K3(4)
            K3(0x10)
            K3(0x1C)
            break;
        case 0x10C:
            W3(4)
            K3(4)
            break;
        case 0x0C:
            C3(4, 0x14)
            K3(4)
            break;
        case 0x11C:
            W3(4)
            W3(0x10)
            W3(0x1C)
            W3(0x28)
            K3(4)
            K3(0x10)
            K3(0x1C)
            K3(0x28)
            break;
        case 0x1C:
            C3(4, 0x14)
            C3(0x10, 0x18)
            C3(0x1C, 0x1C)
            C3(0x28, 0x20)
            K3(4)
            K3(0x10)
            K3(0x1C)
            K3(0x28)
            break;
        }
        dst0 += (B(src, 0) + 1) * 4 + PRIM_OFS(0);
        dst1 += (B(src, 0) + 1) * 4 + PRIM_OFS(0);
        src += (B(src, 1) + 1) * 4;
    }
}

/* (empty, already C) */
void func_800B3348(void) {
}

/* (empty, already C) */
void func_800B3350(void) {
}

/* (GTE macros) */
void updateBattleCameraShake(BattleShake* p) {
    VECTOR d;

    if (p->m38_remaining == 0) {
        p->m1C_current.vx = p->m2C_target.vx;
        p->m1C_current.vy = p->m2C_target.vy;
        p->m1C_current.vz = p->m2C_target.vz;
    } else {
        p->m38_remaining--;
        d.vx = p->m2C_target.vx - p->m24_start.vx;
        d.vy = p->m2C_target.vy - p->m24_start.vy;
        d.vz = p->m2C_target.vz - p->m24_start.vz;
        gte_ldIR0(p->m38_remaining >> 1);
        gte_ldlvl(&d);
        gte_gpf0();
        gte_stlvl(&d);
        d.vx = R3000_DIV(d.vx, p->m3C_duration >> 1);
        d.vy = R3000_DIV(d.vy, p->m3C_duration >> 1);
        d.vz = R3000_DIV(d.vz, p->m3C_duration >> 1);
        p->m1C_current.vx = p->m2C_target.vx - d.vx;
        p->m1C_current.vy = p->m2C_target.vy - d.vy;
        p->m1C_current.vz = p->m2C_target.vz - d.vz;
    }
    p->m34_frame++;
    if (p->m34_frame & 2) {
        battleSceneRenderingMatrixOffset.vx = -p->m1C_current.vx;
    } else {
        battleSceneRenderingMatrixOffset.vx = p->m1C_current.vx;
    }
    if (p->m34_frame & 2) {
        battleSceneRenderingMatrixOffset.vy = -p->m1C_current.vy;
    } else {
        battleSceneRenderingMatrixOffset.vy = p->m1C_current.vy;
    }
    if (p->m34_frame & 2) {
        battleSceneRenderingMatrixOffset.vz = -p->m1C_current.vz;
    } else {
        battleSceneRenderingMatrixOffset.vz = p->m1C_current.vz;
    }
    if (*(s32*)&p->m1C_current == 0 && p->m1C_current.vz == 0 && p->m38_remaining == 0) {
        p->m0_task.mC_deleteCallback(&p->m0_task);
    }
}

void deleteBattleCameraShake(TaskHeader* task);
void deleteBattleCameraShake(TaskHeader* task) {
    allocateSavePointMeshDataSub0_callback(task);
    freeHeap(task);
    battleCameraShakeTask = 0;
}

BattleShake* getOrCreateBattleCameraShake(void);
BattleShake* getOrCreateBattleCameraShake(void) {
    BattleShake* p;

    if (battleCameraShakeTask == 0) {
        p = (BattleShake*)allocateUpdateTask(0, (s32)sizeof(BattleShake) - (s32)sizeof(TaskHeader));
        setTaskUpdateFunction(&p->m0_task, (void (*)(TaskHeader*))updateBattleCameraShake);
        setTaskDeleteFunction(&p->m0_task, deleteBattleCameraShake);
        p->m24_start.vx = 0;
        p->m24_start.vy = 0;
        p->m24_start.vz = 0;
        /* m34_frame is never set: the PS1 starts from the heap's residue,
         * which only matters as frame & 2, the shake's sign phase. In the
         * opening gear battle it is a word-aligned heap address (0x801B0BDC,
         * 0x801B0DDC in Redux), the phase of 0; the PC's residue made the
         * shake's phase differ between builds (xg-htn.15.6). */
        p->m34_frame = 0;
        battleCameraShakeTask = p;
    } else {
        p = battleCameraShakeTask;
        p->m24_start.vx = p->m1C_current.vx;
        p->m24_start.vy = p->m1C_current.vy;
        p->m24_start.vz = p->m1C_current.vz;
    }
    return p;
}

void startBattleCameraShake(SVECTOR* target, s32 duration) {
    BattleShake* p = getOrCreateBattleCameraShake();

    p->m2C_target.vx = target->vx;
    p->m2C_target.vy = target->vy;
    p->m2C_target.vz = target->vz;
    p->m38_remaining = duration * 2;
    p->m3C_duration = duration * 2;
    updateBattleCameraShake(p);
}

/* (GTE macros) Noah battleFader_update */
void battleFader_update(sBattleFader* task) {
    sBattleFader* p = task;
    VECTOR d;

    if (p->m3C_remaining == 0) {
        p->m48_currentColor[0] = p->m42_targetColor[0];
        p->m48_currentColor[1] = p->m42_targetColor[1];
        p->m48_currentColor[2] = p->m42_targetColor[2];
        if ((p->m48_currentColor[0] | p->m48_currentColor[1] | p->m48_currentColor[2]) == 0) {
            pBattleFader1->m0_updateTask.mC_deleteCallback(&pBattleFader1->m0_updateTask);
        }
    } else {
        p->m3C_remaining--;
        d.vx = p->m42_targetColor[0] - p->m45_startColor[0];
        d.vy = p->m42_targetColor[1] - p->m45_startColor[1];
        d.vz = p->m42_targetColor[2] - p->m45_startColor[2];
        gte_ldIR0(p->m3C_remaining >> 1);
        gte_ldlvl(&d);
        gte_gpf0();
        gte_stlvl(&d);
        d.vx = R3000_DIV(d.vx, p->m38_duration >> 1);
        d.vy = R3000_DIV(d.vy, p->m38_duration >> 1);
        d.vz = R3000_DIV(d.vz, p->m38_duration >> 1);
        p->m48_currentColor[0] = p->m42_targetColor[0] - d.vx;
        p->m48_currentColor[1] = p->m42_targetColor[1] - d.vy;
        p->m48_currentColor[2] = p->m42_targetColor[2] - d.vz;
    }
}

/* Noah battleFader_delete */
void battleFader_delete(sBattleFader* fader) {
    registerSpriteCallback2Sub0(&fader->m1C_drawTask);
    allocateSavePointMeshDataSub0_callback(&fader->m0_updateTask);
    pBattleFader1 = 0;
}

int pcGameplayBattle60(void);
/* Noah battleFader_draw */
void battleFader_draw(TaskHeader* task) {
    POLY_F4* p = (POLY_F4*)shapeTransfertTableCurrentEntry;
    sBattleFader* fader = (sBattleFader*)task->m4_data;
    DR_MODE* mode;

    if (shapeTransfertTableCurrentEntry + 0x50 < shapeTransfertTableEnd) {
        mode = (DR_MODE*)(p + 1);
        shapeTransfertTableCurrentEntry = (u8*)(mode + 1);
        SetPolyF4(p);
        SetSemiTrans(p, 1);
        p->r0 = fader->m48_currentColor[0];
        p->g0 = fader->m48_currentColor[1];
        p->b0 = fader->m48_currentColor[2];
        if (pcGameplayBattle60() && (fader->m3C_remaining & 1) && fader->m38_duration > 1) {
            /* Enhanced, the 60 Hz battle: the fade steps every other frame
             * (remaining >> 1 over duration >> 1, battleFader_update); on the
             * frame it holds it is drawn halfway between its two levels */
            s32 k;
            u8* c = &p->r0;
            for (k = 0; k < 3; k++) {
                s32 d = fader->m42_targetColor[k] - fader->m45_startColor[k];
                s32 a = d * ((fader->m3C_remaining + 1) >> 1);
                s32 b = d * ((fader->m3C_remaining - 1) >> 1);
                a = a > 0x7FFF ? 0x7FFF : a < -0x8000 ? -0x8000 : a; /* the GTE's saturation */
                b = b > 0x7FFF ? 0x7FFF : b < -0x8000 ? -0x8000 : b;
                a = fader->m42_targetColor[k] - a / (fader->m38_duration >> 1);
                b = fader->m42_targetColor[k] - b / (fader->m38_duration >> 1);
                c[k] = (u8)((a + b) / 2);
            }
        }
        p->x0 = -32;
        p->y0 = -32;
        p->x1 = 320;
        p->y1 = -32;
        p->x2 = -32;
        p->y2 = 240;
        p->x3 = 320;
        p->y3 = 240;
        SetDrawMode(mode, 0, 0, GetTPage(0, fader->m41_tpageABE, 0, 0), 0);
        AddPrim(&characterRenderingOT[2], p);
        AddPrim(&characterRenderingOT[2], mode);
    }
}

int pcGameplayBattle60(void);
void battleCreateFader2(s32 duration, s32 abe, u8 r, u8 g, u8 b);
void battleCreateFader2(s32 duration, s32 abe, u8 r, u8 g, u8 b) {
    battleFaderMode = 1;
    battleCreateFader(duration, abe, r, g, b);
    battleFaderMode = 0;
}

/* Noah battleCreateFader */
void battleCreateFader(s32 duration, s32 abe, u8 r, u8 g, u8 b) {
    sBattleFader* p;

    if (disableBattleFader != 0) {
        return;
    }
    duration *= 2;
    if (battleFaderMode != 0) {
        if (pBattleFader1 == 0) {
            p = &battleFader2;
            pBattleFader2 = p;
        } else {
            p = pBattleFader2;
            goto update;
        }
    } else if (pBattleFader1 == 0) {
        p = &battleFader1;
        pBattleFader1 = p;
    } else {
        p = pBattleFader1;
        goto update;
    }
    allocateSavePointMeshDataSub0(0, &p->m0_updateTask);
    registerSpriteCallback2(&p->m0_updateTask, &p->m1C_drawTask);
    p->m0_updateTask.m14_ownerId.m31 = 0;
    if (spriteBytecode2ExtendedE0_Var0 != 0) {
        allocateSavePointMeshDataSub0_var0--;
    }
    setTaskUpdateFunction(&p->m0_updateTask, (void (*)(TaskHeader*))battleFader_update);
    setTaskDrawFunction(&p->m1C_drawTask, battleFader_draw);
    setTaskDeleteFunction(&p->m0_updateTask, (void (*)(TaskHeader*))battleFader_delete);
    p->m0_updateTask.m4_data = p;
    p->m1C_drawTask.m4_data = p;
    p->m40 = 0;
    p->m45_startColor[0] = 0;
    p->m45_startColor[1] = 0;
    p->m45_startColor[2] = 0;
    goto set;
update:
    p->m45_startColor[0] = p->m48_currentColor[0];
    p->m45_startColor[1] = p->m48_currentColor[1];
    p->m45_startColor[2] = p->m48_currentColor[2];
set:
    p->m41_tpageABE = abe;
    p->m42_targetColor[0] = r;
    p->m42_targetColor[1] = g;
    p->m42_targetColor[2] = b;
    p->m38_duration = duration;
    p->m3C_remaining = duration;
    battleFader_update(p);
}

s32 getBattleFaderAbe(void);
s32 getBattleFaderAbe(void) {
    if (pBattleFader1 != 0) {
        return pBattleFader1->m41_tpageABE;
    }
    return 1;
}

/* Volume ramp step. The dead stores (t = 0 before p = task, p = 0 after the
 * delete call) only steer CSE: they keep the entry copy p = task from being
 * swapped with the parameter copy and make p the canonical register for the
 * test and the ramp update while the else path keeps using task (retail
 * copies a0 to a1 at entry and uses a0 for the else branch). task then
 * carries the remaining count, a 32-bit number. */
void sBattleSpriteEffect_3A_sub_entity_update(TaskHeader* task) {
    BattleRamp* p;
    s32 target;
    s32 t;

    t = 0;
    p = (BattleRamp*)task;
    if (p->m3C_remaining != 0) {
        task = (TaskHeader*)(sptr)(p->m3C_remaining - 1);
        target = p->m42_target;
        t = target - p->m40_start;
        t *= R3000_DIV((s32)(sptr)task << 5, p->m38_duration);
        p->m3C_remaining = (s32)(sptr)task;
        t /= 32;
        t = target - t;
        p->m46_current = t;
    } else if (((BattleRamp*)task)->m46_current == 0) {
        task->mC_deleteCallback(task);
        p = 0;
    }
}

void sBattleSpriteEffect_3A_sub_entity_delete(TaskHeaderPair* task);
void sBattleSpriteEffect_3A_sub_entity_delete(TaskHeaderPair* task) {
    registerSpriteCallback2Sub0(&task->m1C_drawTask);
    allocateSavePointMeshDataSub0_callback(&task->m0_updateTask);
    freeHeap(task);
    battleSpriteEffect_3A_sub_entity = 0;
    freeBattleEnvColorEffects();
}

void sBattleSpriteEffect_3A_sub_entity_draw(TaskHeader* task);
void sBattleSpriteEffect_3A_sub_entity_draw(TaskHeader* task) {
    BattleRamp* p = (BattleRamp*)task->m4_data;

    if (p->m44_applied != p->m46_current) {
        p->m44_applied = p->m46_current;
        setBattleEnvColorEffect(0, p->m48_mode, 0x20 - p->m46_current, p->m4A_rOffset, p->m4C_gOffset, p->m4E_bOffset);
    }
}

/* (STACK_SWITCH) */
void battleSpriteEffect_3A_sub(s32 target, s32 duration, s32 arg2, s16 arg3, s16 arg4, s16 arg5);
void battleSpriteEffect_3A_sub(s32 target, s32 duration, s32 arg2, s16 arg3, s16 arg4, s16 arg5) {
    BattleRamp* p;
    u8* stack;

    if (battleSpriteEffect_3A_sub_entity == 0) {
        p = (BattleRamp*)allocateTaskPair((s32)sizeof(BattleRamp), 0, (void (*)(TaskHeader*))sBattleSpriteEffect_3A_sub_entity_update, sBattleSpriteEffect_3A_sub_entity_draw, (void (*)(TaskHeader*))sBattleSpriteEffect_3A_sub_entity_delete);
        battleSpriteEffect_3A_sub_entity = p;
        stack = allocateHeap(0x1000, 1);
        STACK_SWITCH(stack + 0xC00);
        initBattleEnvColorEffects();
        STACK_RESTORE();
        freeHeap(stack);
        p->m40_start = 0;
        p->m44_applied = 0;
        p->m46_current = 0;
    } else {
        p = battleSpriteEffect_3A_sub_entity;
        p->m40_start = p->m46_current;
    }
    p->m42_target = target;
    p->m38_duration = duration * 2;
    p->m3C_remaining = duration * 2;
    p->m4A_rOffset = arg3;
    p->m4C_gOffset = arg4;
    p->m4E_bOffset = arg5;
    p->m48_mode = arg2;
    sBattleSpriteEffect_3A_sub_entity_update(&p->m0_updateTask);
}
