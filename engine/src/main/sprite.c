/*
 * Sprite actors (Noah: fieldGraphicObject): creation, sub-sprites, VRAM
 * upload, rendering, and the first animation bytecode interpreter.
 *
 * Provisional file 0x8001D298-0x80021AD8 (main executable). The end is fixed
 * by the read-only data layout (see config/us/main.yaml); the start may move.
 * Built with GCC 2.7.2-cdk.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "pc_wide.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/task.h"
#include "main/sprite.h"
#include "main/sound.h"
#include "main/battle.h"
#include "stack.h"
#include "pc_convert.h"
#include "pc_interpolation.h"
int pcGameplaySpritePrecise(void); /* Enhanced (xg-hbc.19.3), port/src/system/gameplay.c */
#define pcSpritePrecise pcGameplaySpritePrecise()
s32 pcSpriteGravityScaled(s32 gravity, s32 timeScale);

/* m34_primBufferSize of a model block these opcodes attach (main/model.h). */
#define MODEL_BLOCK_PRIM_BUFFER_SIZE(b) pcModelBlockPrimBufferSize(b)

#define SB(p, i) (((s8*)(p))[i])
#define S16A(p) ((p)[0] | (s16)((p)[1] << 8))
#define S16C(p) ((p)[0] | ((s8)(p)[1] << 8))
#define S16R(p) ((s16)((p)[1] << 8) | (p)[0])
#define S16B(p) ((p)[0] | (SB(p, 1) << 8))
#define S24(p) ((SB(p, 2) << 16) + ((p)[1] << 8) + (p)[0])

/* ==== for src/main/sprite.c =============================================== */

/* Objects of actor type 2 (m3C_x0_type == 2). */
typedef struct {
    /* 0x00 */ s16 m0_rotation[3];
    /* 0x06 */ s16 m6_scale[3];
    /* 0x0C */ MATRIX mC_matrix;
    /* 0x2C */ void* m2C_primBuffer0;
    /* 0x30 */ void* m30_primBuffer1;
    /* 0x34 */ void* m34_modelBlock;
    /* 0x38 */ s16 m38_color[3];
} Type2Object;

typedef struct {
    s8 x, y;
} Vec2s8;

#define BATTLE_VISUAL_X(i) battleVisualEntities[i].mA_X
#define BATTLE_VISUAL_Z(i) battleVisualEntities[i].mC_Z

extern s32 fieldDrawEnvsInitialized;
extern s32 spritesCallback2Delay;
extern SpriteActorCore* processBattleAnimationSub0_var1;
extern SpriteActorCore* pSpriteCoreListHead[];
/* Defined here (small, $gp-relative): pending upload for customVramUpload. */
static u8* customVramUploadPtr;
static s16 customVramUploadX;
static s16 customVramUploadY;
void setupOverrideTPage(u16 x, u16 y);
void playSoundEffectInFreeSlot(s32 arg);
s32 vec2dRatan2(Vec2s16 a, Vec2s16 b);
void spriteActorSetDirection(SpriteActorCore* actor, s32 direction);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
void updateSpriteMovement(SpriteActorCore* actor);
void customVramUpload(void);
void registerSpriteCallback2_2(TaskHeader* task);
s32 modulateSpeed(SpriteActorCore* actor, s32 value);
s32 getAngleCos(s32 angle);
s32 getAngleSin(s32 angle);
void setSVector3(SVECTOR* v, s32 x, s32 y, s32 z);
void setVector3(VECTOR* v, s32 x, s32 y, s32 z);
u8* executeSpriteBytecode2Sub3(SpriteActorCore* actor, u8* p);
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animationId);
void initPerSubgroupTransforms(SpriteActorCore* actor);
void setTransparencyMode(SpriteActorCore* actor, s32 mode);
s32 addClamp255(s32 value, s32 delta);
void tintCustomPolyPrimitives(void* a, void* b, void* c, s32 r, s32 g, s32 bl);
void setAnimationBundle(SpriteActorCore* actor, AnimationBundle* bundle);
void relocateModelBlock(u8* block);
void fieldModelRelocation(u8* block);
void initModel1(u8* block, void** a, void** b);
void initModel2(u8* block, void* a, s32 c);
void setGraphicEntityScale(SpriteActorCore* actor, s32 scale);
SpriteActorCore* spriteBytecode2ExtendedE0(SpriteActorCore* actor, u16* anim, SpriteVramData* vram);
void pushByteOnAnimationStack(SpriteActorCore* actor, u8 value);
void computeStepFromMoveSpeed(SpriteActorCore* actor);
void resetSpriteTransfertList(void);
void clearSpriteTransfertList(void);
void renderSpriteActor(SpriteActorCore* actor, OTag* ot);
s32 initFieldEntitySub4Sub4(void* spriteData);
/* Defined here (small, $gp-relative). */
SpriteActorCore* spriteTransfertListHead = NULL;
extern u8 isWorldMapOverlayLoaded;
extern SpriteVramData createSavePointMeshData_mode6;
extern SpriteVramData createSavePointMeshData_mode5;
/* ---- .data (0x8004FAB8) ---- */
/* VRAM position of each sprite texture page (Noah: tpageLocationTable). */
Vec2s16 tpageLocationTable[8] = {
    {0x300, 0}, {0x340, 0}, {0x380, 0}, {0x3C0, 0},
    {0x300, 0x100}, {0x340, 0x100}, {0x380, 0x100}, {0x3C0, 0x100},
};
/* Corners of the shadow quad (renderSpriteSquishedShadow). */
SVECTOR shadowQuadCorners[4] = {0};
/* Visibility bit of each sprite matrix (1 << id), tested against m3C_x8. */
u16 spriteMatrixTable[16] = {
    1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 5, 1 << 6, 1 << 7,
    1 << 8, 1 << 9, 1 << 10, 1 << 11, 1 << 12, 1 << 13, 1 << 14, 1 << 15,
};
/* The same as words (unused in main). */
u32 D_8004FB18[32] = {
    1u << 0, 1u << 1, 1u << 2, 1u << 3, 1u << 4, 1u << 5, 1u << 6, 1u << 7,
    1u << 8, 1u << 9, 1u << 10, 1u << 11, 1u << 12, 1u << 13, 1u << 14, 1u << 15,
    1u << 16, 1u << 17, 1u << 18, 1u << 19, 1u << 20, 1u << 21, 1u << 22, 1u << 23,
    1u << 24, 1u << 25, 1u << 26, 1u << 27, 1u << 28, 1u << 29, 1u << 30, 1u << 31,
};
/* Corners of the sprite being drawn (Noah: currentSpriteCharacterSize). */
SVECTOR currentSpriteCharacterSize[4] = {0};
/* Noah: currentRenderingMatrix (identity until setCurrentRenderingMatrix). */
MATRIX currentRenderingMatrix = {{{0x1000, 0, 0}, {0, 0x1000, 0}, {0, 0, 0x1000}}, {0, 0, 0}};
extern u8* shapeTransfertTableEnd;
extern u8* shapeTransfertTableCurrentEntry;
void executeSpriteBytecode2Sub0Sub0(SpriteActorCore* actor, s32 frameId, SpriteVramData* vram);
void spriteCallback_render2_updateMatrix(SpriteActorCore* actor);
Vec2s16 allocateSpecialEffectSpriteVram(s32 width);
void uploadCharacterSpriteSub1(SpriteActorCore* actor, s32 frameId, SpriteVramData* vram);
void uploadCharacterSprite(SpriteActorCore* actor, s32 frameId, SpriteVramData* vram);
void submitSpriteActorToRendering(SpriteActorCore* actor, OTag* ot);
void renderSpriteSquishedShadow(SpriteActorCore* actor, OTag* ot);
void submitSpriteActorUpperPart(SpriteActorCore* actor, OTag* ot, s32 clip);
void submitSpriteActorLowerPart(SpriteActorCore* actor, OTag* ot, s32 clip);
void addToShapeTransfertTable(void* data, s16 x, s16 y, s16 w, s16 h);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
extern u8 isBattleOverlayLoaded;
void addToSpriteTransferList(SpriteActorCore* actor, s32 frame);
void savePointCallbackC_sub1(SpriteActorCore* actor);
s32 isVramPreBacked(void* spriteData);
/* ---- globals and externs -------------------------------------------------- */

s16 specialEffectSpriteY = 0; /* special-effect sprite VRAM row (0..2) */
s16 specialEffectSpriteX = 0; /* special-effect sprite VRAM x cursor */
/* LZSS image (0x298 bytes unpacked) for uploadBuiltinTexture: game data, assembly. */
extern u8 compressedPauseImage[0xE8]; /* the whole blob: main data labels its tail D_8004FC40 */
void resetPerSubgroupTransforms(SpriteActorCore* actor);
s32 uploadTextureToVram(s32* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY);
void updateAllSubsprites(SpriteActorCore* actor);

/* spriteTransfertListHead = 0 */
void resetSpriteTransfertList(void) {
    spriteTransfertListHead = 0;
}

/* spriteTransfertListHead = 0 */
void clearSpriteTransfertList(void) {
    spriteTransfertListHead = 0;
}

void addToSpriteTransferList(SpriteActorCore* actor, s32 frame) {
    SpriteActorCore* p;
    SpriteVramData* vram;

    if (actor->m3C_x0_type != 1) {
        actor->m34_currentSpriteFrame = 0;
        return;
    }
    if (actor->m40_draw.bits.x14) {
        actor->m40_draw.raw &= ~0x100000;
        if (actor->m20_object->m34_perSubgroupTransform != NULL) {
            resetPerSubgroupTransforms(actor);
        }
    }
    if (actor->m40_draw.bits.x11) {
        for (p = spriteTransfertListHead; p != NULL; p = p->m20_object->m38_pNext) {
            if (p == actor) {
                vram = actor->m24_vramData;
                if (vram != &createSavePointMeshData_mode6 && vram != &createSavePointMeshData_mode5 && !actor->m40_draw.bits.x13) {
                    executeSpriteBytecode2Sub0Sub0(actor, (u16)actor->m34_currentSpriteFrame, vram);
                }
                actor->m34_currentSpriteFrame = frame;
                return;
            }
        }
    }
    actor->m34_currentSpriteFrame = frame;
    actor->m40_draw.raw |= 0x20000;
    actor->m20_object->m38_pNext = spriteTransfertListHead;
    spriteTransfertListHead = actor;
}

void savePointCallbackC_sub1(SpriteActorCore* actor) {
    SpriteActorCore* p = spriteTransfertListHead;
    SpriteActorCore* prev = NULL;

    while (p != NULL) {
        if (p == actor) {
            if (prev != NULL) {
                prev->m20_object->m38_pNext = p->m20_object->m38_pNext;
            } else {
                spriteTransfertListHead = p->m20_object->m38_pNext;
            }
        } else {
            prev = p;
        }
        p = p->m20_object->m38_pNext;
    }
}

void uploadCharacterSprites(void) {
    SpriteActorCore* p = spriteTransfertListHead;

    while (p != NULL) {
        u16 frame = p->m34_currentSpriteFrame;
        if (frame == 0) {
            p->m40_draw.raw &= ~0xFC;
        } else {
            uploadCharacterSprite(p, frame, p->m24_vramData);
        }
        p = p->m20_object->m38_pNext;
    }
    spriteTransfertListHead = NULL;
}

/* ---- task list setters (0x8001CD64) --------------------------------------- */

/* ---- 0x8001D4E8 ------------------------------------------------------------ */

void initPerSubgroupTransforms(SpriteActorCore* actor) {
    if (actor->m20_object->m34_perSubgroupTransform == NULL) {
        actor->m20_object->m34_perSubgroupTransform = allocateHeap(PC_HEAP_SIZED(8 * sizeof(SubgroupTransform), 8 * 0x8), 0);
        resetPerSubgroupTransforms(actor);
    }
}

s32 getAllocationSize(HeapBlock* data); /* heap.c */

/* The sub-sprite buffer for a frame of `count` parts. A frame can have 63
 * (header & 0x3F) but a party character gets 32 (OP_INIT_ENTITY_SCRIPT_sub0Sub3):
 * Bart's frames 62 and 63 have 60 and 58 (field 315's story-145 scene).
 * Retail writes on past the buffer and carries on (a Beetle run of that scene
 * reaches story 146); here that overwrote the next heap block's header (a
 * crash in allocateHeap). Only such a frame gets a larger buffer, so the heap
 * is laid out as retail lays it out whenever retail stays inside it. */
static SubSprite* pcSubSprites(SpriteActorCore* actor, s32 count) {
    SpriteObject* o = actor->m20_object;
    if (o->m2C_subSpriteBuffer != NULL && o->m30_subSprites == o->m2C_subSpriteBuffer &&
        getAllocationSize((HeapBlock*)o->m2C_subSpriteBuffer) < count * (s32)sizeof(SubSprite)) {
        pcHeapUnsupported(); /* native safety growth has no retail allocation */
        freeHeap(o->m2C_subSpriteBuffer);
        o->m2C_subSpriteBuffer = o->m30_subSprites = allocateHeap(PC_HEAP_NATIVE(count * (s32)sizeof(SubSprite)), 0);
    }
    return o->m30_subSprites;
}

void uploadCharacterSpriteSub1(SpriteActorCore* actor, s32 frameId, SpriteVramData* vram) {
    uptr data;
    u8* frame;
    s32 scaled;
    u8 header;
    u16* tileRef;
    u16 texY;
    u16 defaultBlend;
    u32 color;
    s32 count;
    u8 hasExtra;
    u8* p;
    SpriteActorCore* self;
    SubSprite* sub;
    u32 transformId;
    u32 i;
    u8 texX;

    data = vram->m0_spriteData;
    frame = (u8*)(((u16*)data)[frameId] + data);
    scaled = frame[3] * actor->m2C_scale;
    header = *frame;
    tileRef = (u16*)(frame + 4);
    hasExtra = header & 0x80;
    count = header & 0x3F;
    p = frame + (count * 2 + 4);
    self = actor;
    sub = actor->m20_object->m30_subSprites;
    sub = pcSubSprites(actor, count);
    actor->m36_height = scaled / 4096;
    actor->m38_centerHeight = (frame[1] * actor->m2C_scale) / 4096;
    texY = vram->m4_vramLocation.vy & 0xFF;
    defaultBlend = actor->m3C_x5_blendMode;
    color = *(u32*)&actor->m28_r;
    transformId = 4;
    i = 0;
    if (count != 0) {
        do {
            u8* tile;
            u8 b;
            s32 flags;
            s16 blend;
            u8 c;
            u8 clutX;

            sub[i].m9_heightAdjust = 0;
            sub[i].m8_widthAdjust = 0;
            sub[i].m14_attrs.bits.x5_tileFlipY = 0;
        loop:
            {
                b = *p;
                if (!(b & 0x80)) {
                    goto done;
                }
                p++;
                if (b & 0x40) {
                    transformId = b & 7;
                    if (actor->m20_object->m34_perSubgroupTransform == NULL) {
                        actor->m20_object->m34_perSubgroupTransform = allocateHeap(PC_HEAP_SIZED(8 * sizeof(SubgroupTransform), 8 * 0x8), 0);
                        resetPerSubgroupTransforms(actor);
                    }
                    if (b & 0x20) {
                        actor->m20_object->m34_perSubgroupTransform[transformId].m0_translateX = *p++;
                        actor->m20_object->m34_perSubgroupTransform[transformId].m1_translateY = *p++;
                    }
                    if (b & 0x10) {
                        s16 rot = *p++ << 4;
                        actor->m20_object->m34_perSubgroupTransform[transformId].m6_rotateZ = rot;
                    } else {
                        actor->m20_object->m34_perSubgroupTransform[transformId].m6_rotateZ = 0;
                    }
                } else {
                    if (b & 4) {
                        sub[i].m14_attrs.bits.x5_tileFlipY = 1;
                    }
                    if (b & 1) {
                        sub[i].m8_widthAdjust = *p++;
                    }
                    if (b & 2) {
                        sub[i].m9_heightAdjust = *p++;
                    }
                }
                goto loop;
            }
        done:
            tile = (u8*)(*tileRef + data);
            flags = *tile;
            tileRef++;
            if (flags & 1) {
                sub[i].m14_attrs.bits.x3_8bit = 1;
                texX = (u16)(vram->m4_vramLocation.vx & 0x3F) >> 1;
            } else {
                sub[i].m14_attrs.bits.x3_8bit = 0;
                texX = (u16)(vram->m4_vramLocation.vx & 0x3F) >> 2;
            }
            c = *p;
            blend = (*p >> 4) & 3;
            clutX = c & 0xF;
            sub[i].m10_colorAndCode = color;
            if (blend != 0 || (blend = defaultBlend) != 0) {
                blend--;
                ((u8*)&sub[i].m10_colorAndCode)[3] |= 2;
            }
            if ((flags >> 4) & 1) {
                tile++;
                flags |= *tile << 8;
                sub[i].mA_tpage = GetTPage(flags & 1, blend, tpageLocationTable[((u32)flags >> 1) & 7].vx, tpageLocationTable[((u32)flags >> 1) & 7].vy);
                sub[i].mC_clut = GetClut((flags >> 1) & 0xF0, ((flags >> 9) & 0xF) + 0x1CC);
            } else {
                Vec2s16* q;
                if (self->mA8_x0 == 1 && (q = (Vec2s16*)self->m7C_extra->m18_vramLocations) != NULL) {
                    u16 y;
                    u16 x;
                    y = q[((u32)flags >> 1) & 7].vy;
                    x = q[((u32)flags >> 1) & 7].vx;
                    texY = y & 0xFF;
                    texX = (x & 0x3F) >> 2;
                    sub[i].mA_tpage = ((flags & 1) << 7) | ((blend & 3) << 5) | ((s16)(y & 0x100) >> 4)
                                  | ((x & 0x3FF) >> 6) | ((y & 0x200) << 2);
                } else {
                    sub[i].mA_tpage = ((flags & 1) << 7) | ((blend & 3) << 5)
                                  | ((s16)(vram->m4_vramLocation.vy & 0x100) >> 4)
                                  | (((vram->m4_vramLocation.vx + (s32)(((u32)flags >> 1) & 7) * 64) & 0x3FF) >> 6)
                                  | ((vram->m4_vramLocation.vy & 0x200) << 2);
                }
                sub[i].mC_clut = (vram->m8_clut.vy << 6) | (((vram->m8_clut.vx + clutX * 16) >> 4) & 0x3F);
            }
            sub[i].m14_attrs.bits.x0_transformId = transformId;
            sub[i].m4_texcoordX = texX + tile[1];
            sub[i].m5_texcoordY = tile[2] + texY;
            sub[i].m6_width = tile[3];
            sub[i].m7_height = tile[4];
            sub[i].m14_attrs.bits.x4_tileFlipX = *p >> 6;
            if (hasExtra) {
                sub[i].m0_offsetX = p[1] | ((s8)p[2] << 8);
                sub[i].m2_offsetY = p[3] | ((s8)p[4] << 8);
                p += 2;
            } else {
                sub[i].m0_offsetX = (s8)p[1];
                sub[i].m2_offsetY = (s8)p[2];
            }
            i++;
            p += 3;
        } while (i != count);
    }
    actor->m40_draw.bits.x2_subSpriteCount = i;
}

void uploadCharacterSprite(SpriteActorCore* actor, s32 frameId, SpriteVramData* vram) {
    Vec2s16 loc;
    RECT rect;
    uptr data;
    SubSprite* sub;
    u8* frame;
    u8 header;
    u16* tileRef;
    u8* p;
    s32 scaled;
    u32 defaultBlend;
    u32 color;
    s32 base4;
    s32 base2;
    s32 baseY;
    s32 count;
    u8 hasExtra;
    u32 transformId;
    u32 i;

    actor->m40_draw.raw &= ~0x20000;
    actor->m40_draw.raw &= ~0x80000;
    data = vram->m0_spriteData;
    sub = actor->m20_object->m30_subSprites;
    if (frameId < (*(u16*)data & 0x1FF) + 1) {
        if (actor->m3C_x1E_bundleChanged) {
            u16* clut;
            actor->m3C_x1E_bundleChanged = 0;
            clut = (u16*)vram->mC_clutData;
            if (*clut != 0) {
                rect.x = vram->m8_clut.vx;
                rect.y = vram->m8_clut.vy;
                rect.w = *clut * 16;
                rect.h = 1;
                addToShapeTransfertTable((u8*)clut + (*clut * (actor->m3C_x14 << 4) * 2 + 4), vram->m8_clut.vx, vram->m8_clut.vy, *clut * 16, 1);
            }
        }
        if (*(u16*)data & 0x8000) {
            uploadCharacterSpriteSub1(actor, frameId, vram);
            return;
        }
        frame = (u8*)(((u16*)data)[frameId] + data);
        loc = actor->m24_vramData->m4_vramLocation;
        if (actor->m40_draw.bits.xD_mode == 0xE) {
            loc = allocateSpecialEffectSpriteVram(frame[4]);
        }
        scaled = frame[3] * actor->m2C_scale;
        header = *frame;
        tileRef = (u16*)(frame + 6);
        hasExtra = header & 0x80;
        count = header & 0x3F;
        sub = pcSubSprites(actor, count);
        p = frame + (count * 4 + 6);
        actor->m36_height = scaled / 4096;
        actor->m38_centerHeight = (frame[1] * actor->m2C_scale) / 4096;
        defaultBlend = actor->m3C_x5_blendMode;
        base4 = (loc.vx & 0x3F) * 4;
        base2 = (loc.vx & 0x3F) * 2;
        baseY = loc.vy & 0xFF;
        color = *(u32*)&actor->m28_r;
        transformId = 4;
        i = 0;
        if (count != 0) {
            do {
                u8* tile;
                u8 b;
                s32 depth;
                s16 blend;
                u8 c;
                u8 clutX;
                u32 width;
                u8 texX;

                sub[i].m9_heightAdjust = 0;
                sub[i].m8_widthAdjust = 0;
                sub[i].m14_attrs.bits.x5_tileFlipY = 0;
            loop:
                {
                    b = *p;
                    if (!(b & 0x80)) {
                        goto done;
                    }
                    p++;
                    if (b & 0x40) {
                        if (actor->m20_object->m34_perSubgroupTransform == NULL) {
                            actor->m20_object->m34_perSubgroupTransform = allocateHeap(PC_HEAP_SIZED(8 * sizeof(SubgroupTransform), 8 * 0x8), 0);
                            resetPerSubgroupTransforms(actor);
                        }
                        transformId = b & 7;
                        if (b & 0x20) {
                            actor->m20_object->m34_perSubgroupTransform[transformId].m0_translateX = *p++;
                            actor->m20_object->m34_perSubgroupTransform[transformId].m1_translateY = *p++;
                        }
                        if (b & 0x10) {
                            s16 rot = *p++ << 4;
                            actor->m20_object->m34_perSubgroupTransform[transformId].m6_rotateZ = rot;
                        } else {
                            actor->m20_object->m34_perSubgroupTransform[transformId].m6_rotateZ = 0;
                        }
                    } else {
                        if (b & 4) {
                            sub[i].m14_attrs.bits.x5_tileFlipY = 1;
                        }
                        if (b & 1) {
                            sub[i].m8_widthAdjust = *p++;
                        }
                        if (b & 2) {
                            sub[i].m9_heightAdjust = *p++;
                        }
                    }
                    goto loop;
                }
            done:
                tile = (u8*)(tileRef[0] * 4 + data);
                rect.x = tileRef[1] & 0x1F;
                rect.y = (tileRef[1] >> 5) & 0x3F;
                depth = ((u16*)tile)[1];
                tileRef += 2;
                if (depth & 1) {
                    texX = base2 + rect.x * 2;
                    width = tile[0] >> 1;
                    sub[i].m14_attrs.bits.x3_8bit = 1;
                } else {
                    texX = base4 + rect.x * 4;
                    width = tile[0] >> 2;
                    sub[i].m14_attrs.bits.x3_8bit = 0;
                }
                sub[i].m4_texcoordX = texX;
                sub[i].m14_attrs.bits.x0_transformId = transformId;
                sub[i].m5_texcoordY = baseY + rect.y;
                sub[i].m6_width = tile[0];
                sub[i].m7_height = tile[1];
                sub[i].m14_attrs.bits.x4_tileFlipX = *p >> 6;
                c = *p;
                blend = (*p >> 4) & 3;
                clutX = c & 0xF;
                sub[i].m10_colorAndCode = color;
                if (blend != 0 || (blend = defaultBlend) != 0) {
                    blend--;
                    ((u8*)&sub[i].m10_colorAndCode)[3] |= 2;
                }
                sub[i].mA_tpage = getTPage(depth & 1, blend, (u16)loc.vx, loc.vy);
                sub[i].mC_clut = GetClut(vram->m8_clut.vx + clutX * 16, vram->m8_clut.vy);
                addToShapeTransfertTable(tile + 4, loc.vx + rect.x, loc.vy + rect.y, width, tile[1]);
                if (hasExtra) {
                    sub[i].m0_offsetX = p[1] | ((s8)p[2] << 8);
                    sub[i].m2_offsetY = p[3] | ((s8)p[4] << 8);
                    p += 2;
                } else {
                    sub[i].m0_offsetX = (s8)p[1];
                    sub[i].m2_offsetY = (s8)p[2];
                }
                i++;
                p += 3;
            } while (i != count);
        }
        actor->m40_draw.bits.x2_subSpriteCount = i;
        addToShapeTransfertTable(NULL, loc.vx, loc.vy, frame[4], frame[5]);
    }
}

void setupSpriteActorTransform(SpriteActorCore* actor) {
    SVECTOR pos;
    VECTOR out;
    MATRIX* m;
    u32 shift;
    s32 x;
    s32 y;

    if (isBattleOverlayLoaded != 0 || isWorldMapOverlayLoaded != 0) {
        spriteCallback_render2_updateMatrix(actor);
    }
    shift = (actor->m40_draw.raw >> 8) & 0x1F;
    y = actor->m20_object->m3D_anchorY;
    x = actor->m20_object->m3C_anchorX;
    y <<= shift;
    x <<= shift;
    if (actor->mAC_x2_facing) {
        x = -x;
    }
    y = y * actor->m2C_scale / 4096;
    x = x * actor->m2C_scale / 4096;
    pos.vx = actor->m0_position.vx >> 16;
    pos.vy = actor->m0_position.vy >> 16;
    pos.vz = actor->m0_position.vz >> 16;
    ApplyMatrix(&currentRenderingMatrix, &pos, &out);
    m = &actor->m20_object->mC_matrix;
    m->t[0] = currentRenderingMatrix.t[0] + out.vx + x;
    m->t[1] = currentRenderingMatrix.t[1] + out.vy + y;
    m->t[2] = currentRenderingMatrix.t[2] + out.vz;
    SetRotMatrix(m);
    SetTransMatrix(m);
}

void renderSpriteActor(SpriteActorCore* actor, OTag* ot) {
    setupSpriteActorTransform(actor);
    submitSpriteActorToRendering(actor, ot);
    if (actor->m3C_x2) {
        renderSpriteSquishedShadow(actor, ot);
    }
}

void renderSpriteActorUpperPart(SpriteActorCore* actor, OTag* ot, s32 arg2) {
    setupSpriteActorTransform(actor);
    submitSpriteActorUpperPart(actor, ot, arg2);
    if (actor->m3C_x2) {
        renderSpriteSquishedShadow(actor, ot);
    }
}

void renderSpriteActorLowerPart(SpriteActorCore* actor, OTag* ot, s32 arg2) {
    setupSpriteActorTransform(actor);
    submitSpriteActorLowerPart(actor, ot, arg2);
    if (actor->m3C_x2) {
        renderSpriteSquishedShadow(actor, ot);
    }
}

void submitSpriteActorToRendering(SpriteActorCore* actor, OTag* ot) {
    MATRIX mat;
    SVECTOR rot;
    /* Unused: retail's frame (US cdk and JP 2.6.3 alike) has 12 untouched
     * bytes at sp+0x50: an 8-byte struct slot plus a 4-byte one at sp+0x58.
     * A 12-byte array or struct would round up to 16 (depth would move to
     * sp+0x60), and an unused scalar has no slot unless its address is taken. */
    SVECTOR unused;
    s32 unused2;
    s32 depth;
    s32 flag;
    SpriteObject* obj;
    SubSprite* sub;
    u32 i;
    u8 visible;
    s32 spriteWidth;
    s32 spriteHeight;
    s32 matrixId;

    (void)&unused2;
    obj = actor->m20_object;
    spriteHeight = obj->m3D_anchorY;
    spriteWidth = obj->m3C_anchorX;
    sub = obj->m30_subSprites;
    spriteHeight <<= actor->m40_draw.bits.x8;
    spriteWidth <<= actor->m40_draw.bits.x8;
    if (actor->mAC_x2_facing) {
        spriteWidth = -spriteWidth;
    }
    matrixId = -1;
    if (shapeTransfertTableCurrentEntry + actor->m40_draw.bits.x2_subSpriteCount * sizeof(POLY_FT4) < shapeTransfertTableEnd) {
        for (i = 0; i != actor->m40_draw.bits.x2_subSpriteCount; i++) {
            if (matrixId != sub[i].m14_attrs.bits.x0_transformId) {
                matrixId = sub[i].m14_attrs.bits.x0_transformId;
                visible = (spriteMatrixTable[matrixId] & actor->m3C_x8) == 0;
                if (actor->m20_object->m34_perSubgroupTransform != NULL
                    && (*(u16*)&actor->m20_object->m34_perSubgroupTransform[matrixId] != 0
                        || actor->m20_object->m34_perSubgroupTransform[matrixId].m6_rotateZ != 0)) {
                    s32 tx;
                    s32 ty;
                    ty = actor->m20_object->m34_perSubgroupTransform[matrixId].m1_translateY;
                    tx = actor->m20_object->m34_perSubgroupTransform[matrixId].m0_translateX;
                    ty <<= actor->m40_draw.bits.x8;
                    tx <<= actor->m40_draw.bits.x8;
                    if (actor->m3C_x3_flipX) {
                        tx = -tx;
                    }
                    ty = ty * actor->m2C_scale / 4096;
                    tx = tx * actor->m2C_scale / 4096;
                    rot.vx = actor->m20_object->m34_perSubgroupTransform[matrixId].m2_rotateX;
                    rot.vy = actor->m20_object->m34_perSubgroupTransform[matrixId].m4_rotateY;
                    rot.vz = actor->m20_object->m34_perSubgroupTransform[matrixId].m6_rotateZ;
                    if (actor->m3C_x3_flipX) {
                        rot.vz = -rot.vz;
                    }
                    createRotationMatrix(&rot, &mat);
                    mat.t[0] = actor->m20_object->mC_matrix.t[0] + tx;
                    mat.t[1] = actor->m20_object->mC_matrix.t[1] + ty;
                    mat.t[2] = actor->m20_object->mC_matrix.t[2];
                    SetMulMatrix(&actor->m20_object->mC_matrix, &mat);
                    SetTransMatrix(&mat);
                } else {
                    SetRotMatrix(&actor->m20_object->mC_matrix);
                    SetTransMatrix(&actor->m20_object->mC_matrix);
                }
            }
            if (visible) {
                POLY_FT4* p = (POLY_FT4*)shapeTransfertTableCurrentEntry;
                s16 w, h, x, y;
                s8 ow, oh;
                s16 u;
                s16 v;
                s16 uw;
                s16 vh;

                shapeTransfertTableCurrentEntry += sizeof(POLY_FT4);
                setlen(p, 9);
                pcTextureClass(p, PC_TEX_SPRITE); /* a sprite with its own matrix, as the flat ones below */
                pcSpriteInterpolationPrim(actor, p, i);
                pcSpritePosePart(actor, p, &p->x0, &p->x1, &p->x2, sub[i].mA_tpage, sub[i].mC_clut,
                                 sub[i].m4_texcoordX, sub[i].m5_texcoordY, sub[i].m6_width, sub[i].m7_height);
                *(u32*)&p->r0 = sub[i].m10_colorAndCode;
                p->tpage = sub[i].mA_tpage;
                p->clut = sub[i].mC_clut;
                ow = sub[i].m8_widthAdjust;
                oh = sub[i].m9_heightAdjust;
                w = sub[i].m6_width + ow;
                h = sub[i].m7_height + oh;
                w <<= actor->m40_draw.bits.x8;
                h <<= actor->m40_draw.bits.x8;
                x = sub[i].m0_offsetX << actor->m40_draw.bits.x8;
                y = sub[i].m2_offsetY << actor->m40_draw.bits.x8;
                if (actor->m3C_x3_flipX) {
                    w = -w;
                    x = -x;
                }
                if (actor->m3C_x4) {
                    h = -h;
                    y = -y;
                }
                if (!sub[i].m14_attrs.bits.x4_tileFlipX) {
                    currentSpriteCharacterSize[0].vx = x;
                    currentSpriteCharacterSize[1].vx = x + w;
                    currentSpriteCharacterSize[2].vx = x + w;
                    currentSpriteCharacterSize[3].vx = x;
                } else {
                    currentSpriteCharacterSize[0].vx = x + w;
                    currentSpriteCharacterSize[1].vx = x;
                    currentSpriteCharacterSize[2].vx = x;
                    currentSpriteCharacterSize[3].vx = currentSpriteCharacterSize[0].vx;
                }
                if (!sub[i].m14_attrs.bits.x5_tileFlipY) {
                    currentSpriteCharacterSize[0].vy = y;
                    currentSpriteCharacterSize[1].vy = y;
                    currentSpriteCharacterSize[2].vy = y + h;
                    currentSpriteCharacterSize[3].vy = y + h;
                } else {
                    currentSpriteCharacterSize[0].vy = y + h;
                    currentSpriteCharacterSize[1].vy = y + h;
                    currentSpriteCharacterSize[2].vy = y;
                    currentSpriteCharacterSize[3].vy = y;
                }
                currentSpriteCharacterSize[0].vy -= spriteHeight;
                currentSpriteCharacterSize[1].vy -= spriteHeight;
                currentSpriteCharacterSize[2].vy -= spriteHeight;
                currentSpriteCharacterSize[3].vy -= spriteHeight;
                currentSpriteCharacterSize[0].vx -= spriteWidth;
                currentSpriteCharacterSize[1].vx -= spriteWidth;
                currentSpriteCharacterSize[2].vx -= spriteWidth;
                currentSpriteCharacterSize[3].vx -= spriteWidth;
                RotTransPers4(&currentSpriteCharacterSize[0], &currentSpriteCharacterSize[1], &currentSpriteCharacterSize[2], &currentSpriteCharacterSize[3],
                              (s32*)&p->x0, (s32*)&p->x1, (s32*)&p->x3, (s32*)&p->x2, &depth, &flag);
                /* vertices 0, 1 and 2 of the packet are corners 0, 1 and 3 */
                pcSpritePoseSpace(p, currentSpriteCharacterSize[0].vx, currentSpriteCharacterSize[0].vy,
                                  currentSpriteCharacterSize[1].vx, currentSpriteCharacterSize[1].vy,
                                  currentSpriteCharacterSize[3].vx, currentSpriteCharacterSize[3].vy);
                u = sub[i].m4_texcoordX;
                uw = sub[i].m6_width - 1;
                vh = sub[i].m7_height - 1;
                v = sub[i].m5_texcoordY;
                if (p->x3 < p->x0) {
                    if (--u < 0) {
                        u = 0;
                        uw--;
                    }
                }
                p->u0 = u;
                p->v0 = v;
                p->u1 = u + uw;
                p->v1 = v;
                p->u2 = u;
                p->v2 = v + vh;
                p->u3 = u + uw;
                p->v3 = v + vh;
                if (actor->m3C_x1B) {
                    P_TAG* link = (P_TAG*)(ot - matrixId);
                    ((P_TAG*)p)->addr = link->addr;
                    setaddr(link, p);
                } else {
                    ((P_TAG*)p)->addr = ((P_TAG*)ot)->addr;
                    setaddr(ot, p);
                }
            }
        }
    }
}

/* ==== sprite.c ==== */

/* Noah: renderSpriteSquishedShadow (kernel copy) - flattened black shadow quads. */
void renderSpriteSquishedShadow(SpriteActorCore* actor, OTag* ot) {
    MATRIX m;
    SVECTOR pos;
    VECTOR t;
    VECTOR scale;
    s32 p;
    s32 flag;
    u32 count;
    SubSprite* subs;
    u32 i;
    s32 lastTile;
    u8 visible;

    m = currentRenderingMatrix;
    pos.vx = actor->m0_position.vx >> 16;
    pos.vy = actor->m0_position.vy >> 16;
    pos.vz = actor->m0_position.vz >> 16;
    scale.vx = actor->m2C_scale;
    scale.vy = actor->m2C_scale / 2;
    scale.vz = 0;
    ScaleMatrixL(&m, &scale);
    pos.vy = actor->m84_maxY;
    ApplyMatrix(&currentRenderingMatrix, &pos, &t);
    m.t[0] += t.vx;
    m.t[1] += t.vy;
    m.t[2] += t.vz;
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    count = (actor->m40_draw.raw >> 2) & 0x3F;
    subs = actor->m20_object->m30_subSprites;
    lastTile = -1;
    if (shapeTransfertTableCurrentEntry + count * sizeof(POLY_FT4) < shapeTransfertTableEnd) {
        i = 0;
        if (count != 0) {
            do {
                u32 tile = subs[i].m14_attrs.raw & 7;
                if (lastTile != tile) {
                    lastTile = tile;
                    visible = (spriteMatrixTable[lastTile] & actor->m3C_x8) == 0;
                }
                if (visible) {
                    POLY_FT4* prim;
                    s16 w, h, x, y;
                    s8 ow = subs[i].m8_widthAdjust;
                    s8 oh = subs[i].m9_heightAdjust;

                    w = subs[i].m6_width + ow;
                    h = subs[i].m7_height + oh;
                    w <<= actor->m40_draw.bits.x8;
                    h <<= actor->m40_draw.bits.x8;
                    x = subs[i].m0_offsetX << actor->m40_draw.bits.x8;
                    y = subs[i].m2_offsetY << actor->m40_draw.bits.x8;
                    if (actor->m3C_x3_flipX) {
                        w = -w;
                        x = -x;
                    }
                    if (actor->m3C_x4 != ((subs[i].m14_attrs.raw >> 5) & 1)) {
                        h = -h;
                        y = -y;
                    }
                    if (!subs[i].m14_attrs.bits.x4_tileFlipX) {
                        shadowQuadCorners[0].vx = x;
                        shadowQuadCorners[1].vx = x + w;
                        shadowQuadCorners[2].vx = x + w;
                        shadowQuadCorners[3].vx = x;
                    } else {
                        shadowQuadCorners[0].vx = x + w;
                        shadowQuadCorners[1].vx = x;
                        shadowQuadCorners[2].vx = x;
                        shadowQuadCorners[3].vx = x + w;
                    }
                    if (!subs[i].m14_attrs.bits.x5_tileFlipY) {
                        shadowQuadCorners[2].vz = y + h;
                        shadowQuadCorners[3].vz = y + h;
                        shadowQuadCorners[0].vz = y;
                        shadowQuadCorners[1].vz = y;
                    } else {
                        shadowQuadCorners[2].vz = y;
                        shadowQuadCorners[3].vz = y;
                        shadowQuadCorners[0].vz = y + h;
                        shadowQuadCorners[1].vz = y + h;
                    }
                    prim = (POLY_FT4*)shapeTransfertTableCurrentEntry;
                    shapeTransfertTableCurrentEntry = (u8*)(prim + 1);
                    setlen(prim, 9);
                    prim->code = 0x2C;
                    prim->r0 = 0;
                    prim->g0 = 0;
                    prim->b0 = 0;
                    RotAverage4(&shadowQuadCorners[0], &shadowQuadCorners[1], &shadowQuadCorners[2], &shadowQuadCorners[3],
                                &prim->x0, &prim->x1, &prim->x3, &prim->x2, &p, &flag);
                    prim->y0 = prim->y1 = y = (s16)(prim->y0 + prim->y1) / 2;
                    prim->y2 = prim->y3 = y = (s16)(prim->y2 + prim->y3) / 2;
                    prim->tpage = subs[i].mA_tpage;
                    prim->clut = subs[i].mC_clut;
                    prim->u0 = subs[i].m4_texcoordX;
                    prim->v0 = subs[i].m5_texcoordY;
                    prim->u1 = (u8)(subs[i].m4_texcoordX - 1) + subs[i].m6_width;
                    prim->v1 = subs[i].m5_texcoordY;
                    prim->u2 = subs[i].m4_texcoordX;
                    prim->v2 = (u8)(subs[i].m5_texcoordY - 1) + subs[i].m7_height;
                    prim->u3 = (u8)(subs[i].m4_texcoordX - 1) + subs[i].m6_width;
                    prim->v3 = (u8)(subs[i].m5_texcoordY - 1) + subs[i].m7_height;
                    pcTextureClass(prim, PC_TEX_SPRITE);
                    pcInterpolationKeyType(actor, actor->m20_object, 0x100 + i, prim, PC_INTERP_SHADOW);
                    addPrim(ot, prim);
                }
                i++;
            } while (i != actor->m40_draw.bits.x2_subSpriteCount);
        }
    }
}

/* Noah: isVramPreBacked - sprite data header bit 15. */
s32 isVramPreBacked(void* spriteData) {
#if VERSION_JP
    return *(u16*)spriteData >> 15;
#else
    return ((u8*)spriteData)[1] >> 7;
#endif
}

/* Noah: initFieldEntitySub4Sub4 - sub-sprite count, header bits 9..14. */
s32 initFieldEntitySub4Sub4(void* spriteData) {
    if (spriteData == NULL) {
        return 0; /* retail reads low RAM (0x0003 there): no sub-sprites (battle effects using createSavePointMeshData_mode6 unloaded) */
    }
    return (*(u16*)spriteData >> 9) & 0x3F;
}

/* Submit the actor's sub-sprites as POLY_FT4, clipped below `clip` (scaled by m40_draw.bits.x8). */
void submitSpriteActorUpperPart(SpriteActorCore* actor, OTag* ot, s32 clip) {
    u32 count;
    SubSprite* subs;
    u32 i;
    s32 p;
    s32 flag;

    clip <<= actor->m40_draw.bits.x8;
    count = (actor->m40_draw.raw >> 2) & 0x3F;
    subs = actor->m20_object->m30_subSprites;

    if (shapeTransfertTableCurrentEntry + count * sizeof(POLY_FT4) < shapeTransfertTableEnd) {
        i = 0;
        if (count != 0) {
            do {
                POLY_FT4* prim = (POLY_FT4*)shapeTransfertTableCurrentEntry;
                s32 x, y, w, h;
                s32 top, bottom, cut;
                s16 u;
                s32 uw;
                s16 v, vh;

                shapeTransfertTableCurrentEntry = (u8*)(prim + 1);
                setlen(prim, 9);
                *(u32*)&prim->r0 = subs[i].m10_colorAndCode;
                prim->tpage = subs[i].mA_tpage;
                prim->clut = subs[i].mC_clut;
                x = subs[i].m0_offsetX;
                w = subs[i].m6_width + subs[i].m8_widthAdjust;
                y = subs[i].m2_offsetY;
                h = subs[i].m7_height + subs[i].m9_heightAdjust;
                w <<= actor->m40_draw.bits.x8;
                h <<= actor->m40_draw.bits.x8;
                x <<= actor->m40_draw.bits.x8;
                y <<= actor->m40_draw.bits.x8;
                if (actor->m3C_x3_flipX) {
                    w = -w;
                    x = -x;
                }
                if (actor->m3C_x4) {
                    h = -h;
                    y = -y;
                }
                if (!subs[i].m14_attrs.bits.x4_tileFlipX) {
                    currentSpriteCharacterSize[0].vx = x;
                    currentSpriteCharacterSize[1].vx = x + w;
                    currentSpriteCharacterSize[2].vx = x + w;
                    currentSpriteCharacterSize[3].vx = x;
                } else {
                    currentSpriteCharacterSize[0].vx = x + w;
                    currentSpriteCharacterSize[1].vx = x;
                    currentSpriteCharacterSize[2].vx = x;
                    currentSpriteCharacterSize[3].vx = x + w;
                }
                if (h > 0) {
                    top = y;
                    bottom = y + h;
                } else {
                    top = y + h;
                    bottom = y;
                }
                if (clip >= top) {
                    cut = 0;
                    if (clip < bottom) {
                        cut = bottom - clip;
                    }
                    h -= cut;
                    if (h < 0) {
                        y -= cut;
                    }
                    if (!subs[i].m14_attrs.bits.x5_tileFlipY) {
                        currentSpriteCharacterSize[0].vy = y;
                        currentSpriteCharacterSize[1].vy = y;
                        currentSpriteCharacterSize[2].vy = y + h;
                        currentSpriteCharacterSize[3].vy = y + h;
                    } else {
                        currentSpriteCharacterSize[0].vy = y + h;
                        currentSpriteCharacterSize[1].vy = y + h;
                        currentSpriteCharacterSize[2].vy = y;
                        currentSpriteCharacterSize[3].vy = y;
                    }
                    RotAverage4(&currentSpriteCharacterSize[0], &currentSpriteCharacterSize[1], &currentSpriteCharacterSize[2], &currentSpriteCharacterSize[3],
                                &prim->x0, &prim->x1, &prim->x3, &prim->x2, &p, &flag);
                    v = subs[i].m5_texcoordY;
                    cut >>= actor->m40_draw.bits.x8;
                    if (h > 0) {
                        vh = subs[i].m7_height;
                        vh -= cut;
                    } else {
                        vh = subs[i].m7_height;
                        v -= cut;
                        vh -= cut;
                    }
                    u = subs[i].m4_texcoordX;
                    uw = subs[i].m6_width;
                    if (prim->x3 < prim->x0) {
                        if (--u < 0) {
                            u = 0;
                            uw = uw - 1;
                        }
                    }
                    prim->u0 = u;
                    prim->v0 = v;
                    prim->u1 = uw + u;
                    prim->v1 = v;
                    prim->u2 = u;
                    prim->v2 = v + vh;
                    prim->u3 = uw + u;
                    prim->v3 = v + vh;
                    pcTextureClass(prim, PC_TEX_SPRITE);
                    pcSpriteInterpolationPrim(actor, prim, 0x200 + i);
                    addPrim(ot, prim);
                }
                i++;
            } while (i != actor->m40_draw.bits.x2_subSpriteCount);
        }
    }
}

/* Same as submitSpriteActorUpperPart but clipped above `clip`. */
void submitSpriteActorLowerPart(SpriteActorCore* actor, OTag* ot, s32 clip) {
    u32 count;
    SubSprite* subs;
    u32 i;
    s32 p;
    s32 flag;

    clip <<= actor->m40_draw.bits.x8;
    count = (actor->m40_draw.raw >> 2) & 0x3F;
    subs = actor->m20_object->m30_subSprites;

    if (shapeTransfertTableCurrentEntry + count * sizeof(POLY_FT4) < shapeTransfertTableEnd) {
        i = 0;
        if (count != 0) {
            do {
                POLY_FT4* prim = (POLY_FT4*)shapeTransfertTableCurrentEntry;
                s32 x, y, w, h;
                s32 top, bottom, cut;
                s16 u;
                s32 uw;
                s16 v, vh;

                shapeTransfertTableCurrentEntry = (u8*)(prim + 1);
                setlen(prim, 9);
                *(u32*)&prim->r0 = subs[i].m10_colorAndCode;
                prim->tpage = subs[i].mA_tpage;
                prim->clut = subs[i].mC_clut;
                x = subs[i].m0_offsetX;
                w = subs[i].m6_width + subs[i].m8_widthAdjust;
                y = subs[i].m2_offsetY;
                h = subs[i].m7_height + subs[i].m9_heightAdjust;
                w <<= actor->m40_draw.bits.x8;
                h <<= actor->m40_draw.bits.x8;
                x <<= actor->m40_draw.bits.x8;
                y <<= actor->m40_draw.bits.x8;
                if (actor->m3C_x3_flipX) {
                    w = -w;
                    x = -x;
                }
                if (actor->m3C_x4) {
                    h = -h;
                    y = -y;
                }
                if (!subs[i].m14_attrs.bits.x4_tileFlipX) {
                    currentSpriteCharacterSize[0].vx = x;
                    currentSpriteCharacterSize[1].vx = x + w;
                    currentSpriteCharacterSize[2].vx = x + w;
                    currentSpriteCharacterSize[3].vx = x;
                } else {
                    currentSpriteCharacterSize[0].vx = x + w;
                    currentSpriteCharacterSize[1].vx = x;
                    currentSpriteCharacterSize[2].vx = x;
                    currentSpriteCharacterSize[3].vx = x + w;
                }
                if (h > 0) {
                    top = y;
                    bottom = y + h;
                } else {
                    top = y + h;
                    bottom = y;
                }
                if (bottom >= clip) {
                    cut = 0;
                    if (top < clip) {
                        cut = clip - top;
                    }
                    if (h > 0) {
                        y += cut;
                        h -= cut;
                    } else {
                        y += cut;
                        h += cut;
                    }
                    if (!subs[i].m14_attrs.bits.x5_tileFlipY) {
                        currentSpriteCharacterSize[0].vy = y;
                        currentSpriteCharacterSize[1].vy = y;
                        currentSpriteCharacterSize[2].vy = y + h;
                        currentSpriteCharacterSize[3].vy = y + h;
                    } else {
                        currentSpriteCharacterSize[0].vy = y + h;
                        currentSpriteCharacterSize[1].vy = y + h;
                        currentSpriteCharacterSize[2].vy = y;
                        currentSpriteCharacterSize[3].vy = y;
                    }
                    RotAverage4(&currentSpriteCharacterSize[0], &currentSpriteCharacterSize[1], &currentSpriteCharacterSize[2], &currentSpriteCharacterSize[3],
                                &prim->x0, &prim->x1, &prim->x3, &prim->x2, &p, &flag);
                    cut >>= actor->m40_draw.bits.x8;
                    if (h > 0) {
                        v = subs[i].m5_texcoordY;
                        vh = subs[i].m7_height;
                        v += cut;
                    } else {
                        v = subs[i].m5_texcoordY;
                        vh = subs[i].m7_height;
                        v -= cut;
                    }
                    vh -= cut;
                    u = subs[i].m4_texcoordX;
                    uw = subs[i].m6_width;
                    if (prim->x3 < prim->x0) {
                        if (--u < 0) {
                            u = 0;
                            uw = uw - 1;
                        }
                    }
                    prim->u0 = u;
                    prim->v0 = v;
                    prim->u1 = uw + u;
                    prim->v1 = v;
                    prim->u2 = u;
                    prim->v2 = v + vh;
                    prim->u3 = uw + u;
                    prim->v3 = v + vh;
                    pcTextureClass(prim, PC_TEX_SPRITE);
                    pcSpriteInterpolationPrim(actor, prim, 0x300 + i);
                    addPrim(ot, prim);
                }
                i++;
            } while (i != actor->m40_draw.bits.x2_subSpriteCount);
        }
    }
}

/* ---- 0x8001F530 - 0x8001FBE4 --------------------------------------------- */

Vec2s16 allocateSpecialEffectSpriteVram(s32 width) {
    Vec2s16 pos;
    s32 row;

    if (specialEffectSpriteX + width > 0x40) {
        specialEffectSpriteX = 0;
        specialEffectSpriteY++;
        if (specialEffectSpriteY >= 3) {
            specialEffectSpriteY = 0;
        }
    }
    row = specialEffectSpriteY;
    pos.vx = specialEffectSpriteX + 0x300;
    pos.vy = (row << 6) + 0x140;
    specialEffectSpriteX += width;
    return pos;
}

void getBoundingVolumeFromSprite(SpriteActorCore* actor, s32 unused, s32* pX, s32* pY, s32* pZ) {
    SpriteVramData* vram = actor->m24_vramData;
    s32 frame;
    u8* frameData;
    uptr data;

    {
        uptr anim = vram->m10_startOfAnimationContainer;
        u8* p = (u8*)(*(u16*)(anim + 2) + anim);
        frame = p[*(u16*)(p + 4) + 4];
    }
    data = vram->m0_spriteData;
    if (frame != 0) {
        frame--;
    }
    if (((u16*)data)[frame] < frame) {
        frame = 0;
    }
    frameData = (u8*)(((u16*)data)[frame + 1] + data);
    *pY = (frameData[3] * actor->m2C_scale) / 4096;
    *pZ = (frameData[1] * actor->m2C_scale) / 4096;
    *pX = (frameData[2] * actor->m2C_scale) / 4096;
}

void updateAllSubsprites(SpriteActorCore* actor) {
    if (actor->m3C_x0_type == 1) {
        s32 blend = actor->m3C_x5_blendMode;
        Vec2s16 vramLocation;
        u32 colorAndCode;
        SubSprite* sub;
        u32 i;

        if (blend != 0) {
            blend = blend - 1;
        }
        vramLocation = actor->m24_vramData->m4_vramLocation;
        sub = actor->m20_object->m30_subSprites;
        colorAndCode = *(u32*)&actor->m28_r;
        for (i = 0; i != actor->m40_draw.bits.x2_subSpriteCount; i++) {
            sub[i].m10_colorAndCode = colorAndCode;
            sub[i].mA_tpage = (sub[i].mA_tpage & 0xFF9F) | (blend << 5);
        }
    }
}

void executeSpriteBytecode2Sub0Sub0sub0(SpriteActorCore* actor, s32 frameId, SpriteVramData* vram) {
    uptr data = vram->m0_spriteData;
    u8* frame = (u8*)(((u16*)data)[frameId] + data);
    u8 header = *frame;
    u32 i = 0;
    u8 hasExtra = header & 0x80;
    s32 count = header & 0x3F;
    u8* p = frame + (count * 2 + 4);
    u8 b;

    if (count != 0) {
        do {
        loop:
            b = *p;
            if (b & 0x80) {
                p++;
                if (b & 0x40) {
                    s32 idx = b & 7;
                    if (actor->m20_object->m34_perSubgroupTransform == NULL) {
                        actor->m20_object->m34_perSubgroupTransform = allocateHeap(PC_HEAP_SIZED(8 * sizeof(SubgroupTransform), 8 * 0x8), 0);
                        resetPerSubgroupTransforms(actor);
                    }
                    if (b & 0x20) {
                        actor->m20_object->m34_perSubgroupTransform[idx].m0_translateX = *p++;
                        actor->m20_object->m34_perSubgroupTransform[idx].m1_translateY = *p++;
                    }
                    if (b & 0x10) {
                        s16 rot = *p++ << 4;
                        actor->m20_object->m34_perSubgroupTransform[idx].m6_rotateZ = rot;
                    } else {
                        actor->m20_object->m34_perSubgroupTransform[idx].m6_rotateZ = 0;
                    }
                } else {
                    if (b & 1) {
                        p++;
                    }
                    if (b & 2) {
                        p++;
                    }
                }
                goto loop;
            }
            if (hasExtra) {
                p += 2;
            }
            i++;
            p += 3;
        } while (i != count);
    }
}

void executeSpriteBytecode2Sub0Sub0(SpriteActorCore* actor, s32 frameId, SpriteVramData* vram) {
    uptr data = vram->m0_spriteData;
    u8 unused[16]; /* 16 bytes of unused stack in retail */

    if (frameId < (*(u16*)data & 0x1FF) + 1) {
        if (*(u16*)data & 0x8000) {
            executeSpriteBytecode2Sub0Sub0sub0(actor, frameId, vram);
        } else {
            u8* frame = (u8*)(((u16*)data)[frameId] + data);
            u8 header = *frame;
            u32 i = 0;
            u8 hasExtra = header & 0x80;
            s32 count = header & 0x3F;
            u8* p = frame + (count * 4 + 6);

            if (count != 0) {
                do {
                    while (1) {
                        u8 b = *p;
                        if (!(b & 0x80)) {
                            break;
                        }
                        p++;
                        if (b & 0x40) {
                            s32 idx;
                            if (actor->m20_object->m34_perSubgroupTransform == NULL) {
                                actor->m20_object->m34_perSubgroupTransform = allocateHeap(PC_HEAP_SIZED(8 * sizeof(SubgroupTransform), 8 * 0x8), 0);
                                resetPerSubgroupTransforms(actor);
                            }
                            idx = b & 7;
                            if (b & 0x20) {
                                actor->m20_object->m34_perSubgroupTransform[idx].m0_translateX = *p++;
                                actor->m20_object->m34_perSubgroupTransform[idx].m1_translateY = *p++;
                            }
                            if (b & 0x10) {
                                s16 rot = *p++ << 4;
                                actor->m20_object->m34_perSubgroupTransform[idx].m6_rotateZ = rot;
                            } else {
                                actor->m20_object->m34_perSubgroupTransform[idx].m6_rotateZ = 0;
                            }
                        } else {
                            if (b & 1) {
                                p++;
                            }
                            if (b & 2) {
                                p++;
                            }
                        }
                    }
                    if (hasExtra) {
                        p += 2;
                    }
                    i++;
                    p += 3;
                } while (i != count);
            }
        }
    }
}

void uploadBuiltinTexture(s32 x, s32 y) {
    void* image = mallocAndDecompress(compressedPauseImage, 0);
    uploadTextureToVram(image, 1, x, y, 0, 0, 0);
    DrawSync(0);
    freeHeap(image);
}

/* Upload customVramUploadPtr to VRAM (customVramUploadX, customVramUploadY) on a scratch stack. */
void customVramUpload(void) {
    u8* block = allocateHeap(PC_HEAP_BYTES(0x2000), 1);
    STACK_SWITCH(block + 0x1F00);
    uploadTextureToVram((s32*)customVramUploadPtr, 1, customVramUploadX, customVramUploadY, 0, 0, 0);
    STACK_RESTORE();
    freeHeap(block);
}

u8* executeSpriteBytecode2Sub3(SpriteActorCore* actor, u8* p) {
    u8 b = *p;
    if ((b & 0x80) == 0) {
        s32 offset = (s8)b;
        return &actor->m8E_stack[actor->m8C_stackPosition + offset];
    }
    return actor->m88_stack2 + (b & 0x7F);
}

/* Battle scripts can aim at a missing actor (a NULL parent or target): the
 * PS1 then reads low BIOS RAM through NULL. This actor holds what those reads
 * see there: the position words at 0, 4, 8 are 0x00000003, 0x275A0C80,
 * 0x03400008 (PCSX-Redux), the heights at 0x36 and 0x38 are 0. */
static SpriteActorCore pcLowRamActor = {{3, 0x275A0C80, 0x03400008}};
#define LOW_RAM_IF_NULL(a) ((a) != NULL ? (a) : &pcLowRamActor)

/* Sprite bytecode interpreter, extended opcodes 0x8A..0xFC: op is the opcode,
 * p points at its operands. */
void executeSpriteBytecode2Extended(SpriteActorCore* actor, s32 op, u8* p) {
    /* Op 0xBC can leave sv unset (a subgroup position of a target that is
     * not a sprite, of no parent, an index past the table) and still writes
     * it to the actor: Fei's Yamikei on a model enemy. The PS1 then reads its
     * stack slot, which every call from the battle interpreter shares: in
     * Redux (Yamikei on six enemies) 48 of 60 hits read the sv an earlier
     * call computed, the rest registers that battleSpriteEffect_19/_1C saved
     * there. On the PC the slot held host addresses, so the effect moved with
     * the build's layout; keeping sv across calls gives the PS1's common case
     * (xg-02e). */
    static SVECTOR sv;
    VECTOR v;
    SVECTOR rot;
    MATRIX m;
    Vec2s16 from;
    Vec2s16 to;
    s32 flag;
    SpriteActorCore* other;
    u8* unk; /* never initialised: case 0xF7 reads a stale register in retail too */

    switch (op & 0xFF) {
    case 0x8D:
        setupOverrideTPage(actor->m24_vramData->m4_vramLocation.vx, actor->m24_vramData->m4_vramLocation.vy);
        break;
    case 0xC6:
        if (actor->mA8_x0 == 1) {
            actor->m7C_extra->mC_animationSignal = p[0];
        }
        break;
    case 0xC9:
        if (actor->mA8_x0 == 1) {
            actor->m7C_extra->mC_animationSignal = S16C(p);
        }
        break;
    case 0xC5: {
        s32 n = p[0];
        n = n / (fieldDrawEnvsInitialized + 1);
        while (--n != -1) {
            updateSpriteMovement(actor);
        }
        break;
    }
    case 0xB9: {
        u8* snd = actor->m50_soundEffectSeq;
        if (snd != NULL) {
            playSoundEffectInFreeSlot(p[0] | (*(u16*)(snd + 0x14) << 16));
        }
        break;
    }
    case 0xB0: {
        SeqFile* snd = gBattleCurrentSoundEffectSeq;
        if (snd != NULL) {
            playSoundEffectInFreeSlot(p[0] | (snd->u14.m14_id << 16));
        }
        break;
    }
    case 0xCC:
#if VERSION_JP
        actor->m88_stack2 = actor->m64_spriteByteCode + S16A(p);
#else
        actor->m88_stack2 = actor->m64_spriteByteCode + S16R(p);
#endif
        break;
    case 0x8C: {
        s16 dir;
        other = LOW_RAM_IF_NULL(actor->m74_targetEntitySprite);
        from.vx = actor->m0_position.vx >> 16;
        from.vy = actor->m0_position.vz >> 16;
        to.vx = other->m0_position.vx >> 16;
        to.vy = other->m0_position.vz >> 16;
        dir = vec2dRatan2(to, from);
        spriteActorSetDirection(actor, dir);
        setSpriteActorAngle(actor, dir);
        break;
    }
    case 0x94:
        other = LOW_RAM_IF_NULL(actor->m70_parent);
        if (actor->m3C_x0_type == 2) {
            actor->m20_object->m0_rotation[1] = other->m32_direction;
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    case 0xA7: {
        s32 wait;
        if (p[0] & 0x80) {
            wait = (p[0] & 0x7F) + 1;
            spritesCallback2Delay = wait;
        } else {
            wait = ((p[0] + 1) * actor->mAC_x7_timeScale) / 256;
            if (wait == 0) {
                wait = 1;
            }
            actor->m9E_wait += wait;
        }
        break;
    }
    case 0xFC: {
        u8* stack = allocateHeap(PC_HEAP_BYTES(0x2000), 0);
        s32 x, y;
        STACK_SWITCH(stack + 0x1F00);
        customVramUploadPtr = (u8*)(S24(p) + (sptr)p);
        x = actor->m24_vramData->m4_vramLocation.vx;
        y = actor->m24_vramData->m4_vramLocation.vy;
        customVramUploadX = x;
        customVramUploadY = y;
        customVramUpload();
        STACK_RESTORE();
        freeHeap(stack);
        break;
    }
    case 0xBF:
        actor->m36_height = p[0];
        break;
    case 0x96:
        registerSpriteCallback2_2(actor->m6C_owner);
        break;
    case 0xA2:
        actor->m3C_x8 = p[0];
        break;
    case 0xCD: {
        SpriteObject* obj = actor->m20_object;
        if (obj != NULL) {
            u16 arg = S16C(p);
            s32 angle = (arg & 0x1FF) << 3;
            s32 idx = (arg >> 9) & 7;
            if (!((arg >> 12) & 1)) {
                if (idx != 0) {
                    if (obj->m34_perSubgroupTransform != NULL) {
                        obj->m34_perSubgroupTransform[idx].m2_rotateX += angle;
                    }
                    break;
                }
                obj->m0_rotation[0] += angle;
            } else {
                if (idx != 0) {
                    if (obj->m34_perSubgroupTransform != NULL) {
                        obj->m34_perSubgroupTransform[idx].m2_rotateX = angle;
                    }
                    break;
                }
                obj->m0_rotation[0] = angle;
            }
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    }
    case 0xCE: {
        SpriteObject* obj = actor->m20_object;
        u16 arg = S16C(p);
        if (obj != NULL) {
            s32 angle = (arg & 0x1FF) * 8;
            s32 idx = (arg >> 9) & 7;
            if (actor->mAC_x2_facing) {
                angle = -angle;
            }
            if (!((arg >> 12) & 1)) {
                if (idx != 0) {
                    if (obj->m34_perSubgroupTransform != NULL) {
                        obj->m34_perSubgroupTransform[idx].m4_rotateY += angle;
                    }
                    break;
                }
                obj->m0_rotation[1] += angle;
            } else {
                if (idx != 0) {
                    if (obj->m34_perSubgroupTransform != NULL) {
                        obj->m34_perSubgroupTransform[idx].m4_rotateY = angle;
                    }
                    break;
                }
                obj->m0_rotation[1] = angle;
            }
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    }
    case 0xCF: {
        SpriteObject* obj = actor->m20_object;
        u16 arg = S16C(p);
        if (obj != NULL) {
            s32 angle = (arg & 0x1FF) * 8;
            s32 idx = (arg >> 9) & 7;
            if (actor->mAC_x2_facing) {
                angle = -angle;
            }
            if (!((arg >> 12) & 1)) {
                if (idx != 0) {
                    if (obj->m34_perSubgroupTransform != NULL) {
                        obj->m34_perSubgroupTransform[idx].m6_rotateZ += angle;
                    }
                    break;
                }
                obj->m0_rotation[2] += angle;
            } else {
                if (idx != 0) {
                    if (obj->m34_perSubgroupTransform != NULL) {
                        obj->m34_perSubgroupTransform[idx].m6_rotateZ = angle;
                    }
                    break;
                }
                obj->m0_rotation[2] = angle;
            }
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    }
    case 0xC0: {
        s32 dist;
        s32 angle;
        dist = rand() & 0xFF;
        dist *= p[0];
        dist >>= 8;
        dist = (dist * actor->m2C_scale) / 4096;
        angle = rand();
        actor->m0_position.vx += modulateSpeed(actor, getAngleSin(angle)) * dist * 16;
        actor->m0_position.vz -= modulateSpeed(actor, getAngleCos(angle)) * dist * 16;
        break;
    }
    case 0xC1: {
        s32 dist;
        dist = rand() & 0xFF;
        dist *= p[0];
        dist >>= 8;
        dist = (dist * actor->m2C_scale) / 4096;
        setSVector3(&sv, (s16)modulateSpeed(actor, dist), 0, 0);
        {
            /* the PS1 compiler evaluates arguments left to right, the PC's right to left */
            s16 rx = rand();
            s16 ry = rand();
            setSVector3(&rot, rx, ry, 0);
        }
        setVector3(&v, (s16)(actor->m0_position.vx >> 16), (s16)(actor->m0_position.vy >> 16),
                   (s16)(actor->m0_position.vz >> 16));
        TransMatrix(&m, &v);
        SetTransMatrix(&m);
        createRotationMatrix(&rot, &m);
        SetRotMatrix(&m);
        RotTransSV(&sv, &sv, &flag);
        actor->m0_position.vx = sv.vx << 16;
        actor->m0_position.vy = sv.vy << 16;
        actor->m0_position.vz = sv.vz << 16;
        break;
    }
    case 0xBC: {
        u8 type = p[0];
        s32 idx = type & 0x3F;
        if (type & 0x80) {
            u8 applyMatrix = actor->m3C_x18;
            switch (idx) {
            case 38: {
#if VERSION_JP
                sv.vx = BATTLE_VISUAL_X(actor->mAC_x0);
                sv.vz = BATTLE_VISUAL_Z(actor->mAC_x0);
#else
                s32 lo = actor->mA8_x1E;
                sv.vx = BATTLE_VISUAL_X((actor->mAC_x0 << 2) | lo);
                lo = actor->mA8_x1E;
                sv.vz = BATTLE_VISUAL_Z((actor->mAC_x0 << 2) | lo);
#endif
                sv.vy = 0;
                break;
            }
            case 36:
                ((TaskHeader*)actor->m6C_owner)->m14_ownerId.m30 = 1;
                goto self;
            case 37:
                ((TaskHeader*)actor->m6C_owner)->m14_ownerId.m30 = 0;
                goto self;
            case 23:
                ReadGeomOffset(&v.vx, &v.vy);
                sv.vx = (0xA0 - v.vx) * 2;
                sv.vy = (0x70 - v.vy) * 2;
                sv.vz = actor->m0_position.vz >> 16;
                applyMatrix = 0;
                break;
            case 24:
            self:
                sv.vx = actor->m0_position.vx >> 16;
                sv.vy = actor->m0_position.vy >> 16;
                sv.vz = actor->m0_position.vz >> 16;
                applyMatrix = 0;
                break;
            case 22:
                other = LOW_RAM_IF_NULL(actor->m70_parent);
                sv.vx = other->m0_position.vx >> 16;
                sv.vy = other->m0_position.vy >> 16;
                sv.vz = other->m0_position.vz >> 16;
                applyMatrix = 0;
                break;
            case 32:
                other = LOW_RAM_IF_NULL(processBattleAnimationSub0_var1);
                goto l18;
            case 33:
                other = LOW_RAM_IF_NULL(processBattleAnimationSub0_var1);
                goto l19;
            case 34:
                other = LOW_RAM_IF_NULL(processBattleAnimationSub0_var1);
                goto l20;
            case 35:
                other = LOW_RAM_IF_NULL(processBattleAnimationSub0_var1);
                goto l21;
            case 18:
                other = LOW_RAM_IF_NULL(actor->m74_targetEntitySprite);
            l18:
                sv.vx = other->m0_position.vx >> 16;
                sv.vy = other->m0_position.vy >> 16;
                sv.vz = other->m0_position.vz >> 16;
                sv.vy -= (u16)other->m36_height;
                break;
            case 19:
                other = LOW_RAM_IF_NULL(actor->m74_targetEntitySprite);
            l19:
                sv.vx = other->m0_position.vx >> 16;
                sv.vy = other->m0_position.vy >> 16;
                sv.vz = other->m0_position.vz >> 16;
                sv.vy -= (u16)other->m36_height - ((u16)other->m36_height - (u16)other->m38_centerHeight) / 2;
                break;
            case 20:
                other = LOW_RAM_IF_NULL(actor->m74_targetEntitySprite);
            l20:
                sv.vx = other->m0_position.vx >> 16;
                sv.vy = other->m0_position.vy >> 16;
                sv.vz = other->m0_position.vz >> 16;
                sv.vy -= (u16)other->m38_centerHeight;
                break;
            case 21:
                other = LOW_RAM_IF_NULL(actor->m74_targetEntitySprite);
            l21:
                sv.vx = other->m0_position.vx >> 16;
                sv.vy = other->m0_position.vy >> 16;
                sv.vz = other->m0_position.vz >> 16;
                {
                    s32 h = (u16)other->m38_centerHeight;
                    sv.vy -= (u16)other->m38_centerHeight - (h / 2);
                }
                break;
            case 6:
                sv.vx = previousCameraEye2.vx >> 16;
                sv.vy = previousCameraEye2.vy >> 16;
                sv.vz = previousCameraEye2.vz >> 16;
                break;
            case 7:
                sv.vx = previousCameraAt2.vx >> 16;
                sv.vy = previousCameraAt2.vy >> 16;
                sv.vz = previousCameraAt2.vz >> 16;
                break;
            case 1:
                other = LOW_RAM_IF_NULL(processBattleAnimationSub0_var1);
                goto l0;
            case 9:
                idx = 0xB;
                goto target;
            case 8:
                idx = 0xC;
                goto target;
            case 10:
                idx = 0xD;
                goto target;
            case 11:
            case 12:
            case 13:
            case 14:
            case 15:
            case 16:
            case 17:
                other = actor->m70_parent;
                if (other == NULL) {
                    break;
                }
                goto subgroup;
            target:
                other = LOW_RAM_IF_NULL(actor->m74_targetEntitySprite);
            subgroup:
                if (other->m20_object != NULL) {
                    s32 x, y, k;
                    if (other->m3C_x0_type != 1) {
                        break;
                    }
                    k = idx - 10;
                    if (other->m20_object->m34_perSubgroupTransform != NULL) {
                        x = other->m20_object->m34_perSubgroupTransform[k].m0_translateX;
                        y = other->m20_object->m34_perSubgroupTransform[k].m1_translateY;
                    } else {
                        y = x = 0;
                    }
                    if (other->mAC_x2_facing) {
                        x = -x;
                    }
                    x = (x * other->m2C_scale) / 4096;
                    y = (y * other->m2C_scale) / 4096;
                    setSVector3(&sv, (s16)(x + (other->m0_position.vx >> 16)),
                                (s16)(y + (other->m0_position.vy >> 16)), (s16)(other->m0_position.vz >> 16));
                }
                break;
            case 25:
            case 26:
            case 27:
            case 28:
            case 29:
            case 30:
            case 31:
                other = LOW_RAM_IF_NULL(processBattleAnimationSub0_var1);
                idx -= 0xE;
                goto subgroup;
            case 0:
                other = LOW_RAM_IF_NULL(actor->m74_targetEntitySprite);
            l0:
                sv.vx = other->m0_position.vx >> 16;
                sv.vy = other->m0_position.vy >> 16;
                sv.vz = other->m0_position.vz >> 16;
                break;
            case 2: {
                s32 n;
                setVector3(&v, 0, 0, 0);
                for (n = 0; (other = pSpriteCoreListHead[n]) != NULL; n++) {
                    v.vx += other->m0_position.vx;
                    v.vy += other->m0_position.vy;
                    v.vz += other->m0_position.vz;
                }
                v.vx = R3000_DIV(v.vx, n);
                v.vy = R3000_DIV(v.vy, n);
                v.vz = R3000_DIV(v.vz, n);
                sv.vx = v.vx >> 16;
                sv.vy = v.vy >> 16;
                sv.vz = v.vz >> 16;
                break;
            }
            case 3:
                other = LOW_RAM_IF_NULL(processBattleAnimationSub0_var1);
                v.vx = other->m0_position.vx;
                v.vy = other->m0_position.vy;
                v.vz = other->m0_position.vz;
                v.vx += actor->m0_position.vx;
                v.vy += actor->m0_position.vy;
                v.vz += actor->m0_position.vz;
                v.vx /= 2;
                v.vy /= 2;
                v.vz /= 2;
                sv.vx = v.vx >> 16;
                sv.vy = v.vy >> 16;
                sv.vz = v.vz >> 16;
                break;
            case 4: {
                s32 n;
                setVector3(&v, 0, 0, 0);
                for (n = 0; (other = pSpriteCoreListHead[n]) != NULL; n++) {
                    v.vx += other->m0_position.vx;
                    v.vy += other->m0_position.vy;
                    v.vz += other->m0_position.vz;
                }
                n++;
                v.vx += actor->m0_position.vx;
                v.vy += actor->m0_position.vy;
                v.vz += actor->m0_position.vz;
                v.vx /= n;
                v.vy /= n;
                v.vz /= n;
                sv.vx = v.vx >> 16;
                sv.vy = v.vy >> 16;
                sv.vz = v.vz >> 16;
                break;
            }
            case 5: {
                /* n is never set: the PS1 divides 0 by the caller's s0, so
                 * sv is 0 unless that is 0 too; never seen in the battle
                 * sweeps. The PC must not divide by host garbage (or 0). */
                s32 n = 1;
                setVector3(&v, 0, 0, 0);
                v.vx /= n;
                v.vy /= n;
                v.vz /= n;
                sv.vx = v.vx >> 16;
                sv.vy = v.vy >> 16;
                sv.vz = v.vz >> 16;
                break;
            }
            }
            if (applyMatrix) {
                ApplyMatrixSV(&currentRenderingMatrix, &sv, &sv);
                sv.vx += currentRenderingMatrix.t[0];
                sv.vy += currentRenderingMatrix.t[1];
                sv.vz += currentRenderingMatrix.t[2];
            }
            if (type & 0x40) {
                actor->mA0_destination.vx = sv.vx;
                actor->mA0_destination.vy = sv.vy;
                actor->mA0_destination.vz = sv.vz;
                break;
            }
            actor->m0_position.vx = sv.vx << 16;
            actor->m0_position.vy = sv.vy << 16;
            actor->m0_position.vz = sv.vz << 16;
        } else {
            s32 x, y;
            other = actor->m70_parent;
            if (other == NULL) {
                break;
            }
            if (other->m20_object == NULL) {
                break;
            }
            if (other->m3C_x0_type != 1) {
                break;
            }
            if (other->m20_object->m34_perSubgroupTransform != NULL) {
                y = other->m20_object->m34_perSubgroupTransform[type].m1_translateY;
                x = other->m20_object->m34_perSubgroupTransform[type].m0_translateX;
            } else {
                y = x = 0;
            }
            if (other->m3C_x3_flipX) {
                x = -x;
            }
            y = (y * other->m2C_scale) / 4096 << 16;
            x = (x * other->m2C_scale) / 4096 << 16;
            actor->m0_position.vz = other->m0_position.vz;
            actor->m0_position.vx = other->m0_position.vx + x;
            actor->m0_position.vy = other->m0_position.vy + y;
        }
        break;
    }
    case 0xD1: {
        u8* a = executeSpriteBytecode2Sub3(actor, p);
        *a *= *executeSpriteBytecode2Sub3(actor, p + 1);
        break;
    }
    case 0xD2:
    case 0xD5: {
        u8* a = executeSpriteBytecode2Sub3(actor, p);
        *a /= *executeSpriteBytecode2Sub3(actor, p + 1);
        break;
    }
    case 0xE5: {
        u8* a = executeSpriteBytecode2Sub3(actor, p);
        *a = ((rand() & 0xFF) * p[1]) >> 8;
        break;
    }
    case 0xD6:
        *executeSpriteBytecode2Sub3(actor, p) += p[1];
        break;
    case 0xD7:
        *executeSpriteBytecode2Sub3(actor, p) *= SB(p, 1);
        break;
    case 0xD8:
        *executeSpriteBytecode2Sub3(actor, p) /= SB(p, 1);
        break;
    case 0xD9:
        *executeSpriteBytecode2Sub3(actor, p) <<= SB(p, 1);
        break;
    case 0xDA:
        *(s8*)executeSpriteBytecode2Sub3(actor, p) >>= SB(p, 1);
        break;
    case 0xDB: {
        u8* a = executeSpriteBytecode2Sub3(actor, p);
        s32 val = (a[1] << 8) | a[0];
        val <<= SB(p, 1);
        a[0] = val;
        a[1] = val >> 8;
        break;
    }
    case 0xDC: {
        u8* a = executeSpriteBytecode2Sub3(actor, p);
        s32 val = (a[1] << 8) | a[0];
        val >>= SB(p, 1);
        a[0] = val;
        a[1] = val >> 8;
        break;
    }
    case 0xD0:
    case 0xD3:
    case 0xDD:
    case 0xDE: {
        u8* a = executeSpriteBytecode2Sub3(actor, p);
        *a += *executeSpriteBytecode2Sub3(actor, p + 1);
        break;
    }
    case 0xA4:
        spriteActorSetPlayingAnimation(actor->m74_targetEntitySprite, SB(p, 0));
        break;
    case 0xDF:
        *executeSpriteBytecode2Sub3(actor, p) = p[1];
        break;
    case 0xE6: {
        u8* a = executeSpriteBytecode2Sub3(actor, p);
        a[0] = p[1];
        a[1] = 0;
        break;
    }
    case 0x91:
        actor->m2B_code &= ~1;
        updateAllSubsprites(actor);
        break;
    case 0x92:
        actor->m2B_code |= 1;
        updateAllSubsprites(actor);
        break;
    case 0xBB:
        actor->m30_otOffset += (s8)p[0];
        break;
    case 0x93:
        other = actor->m70_parent;
        if (other != NULL && actor->m3C_x0_type != 0) {
            if (isVramPreBacked((void*)actor->m24_vramData->m0_spriteData) == 0) {
                actor->m40_draw.bits.xD_mode = 0xE;
            }
            if (actor->m20_object != NULL &&
                other->m20_object != NULL &&
                other->m20_object->m34_perSubgroupTransform != NULL) {
                s32 i;
                initPerSubgroupTransforms(actor);
                for (i = 0; i != 8; i++) {
                    actor->m20_object->m34_perSubgroupTransform[i] = other->m20_object->m34_perSubgroupTransform[i];
                }
                *(Vec2s8*)&actor->m20_object->m3C_anchorX = *(Vec2s8*)&other->m20_object->m3C_anchorX;
            }
            addToSpriteTransferList(actor, (u16)actor->m34_currentSpriteFrame);
        }
        break;
    case 0xBA:
        setTransparencyMode(actor, p[0]);
        break;
    case 0xF1: {
        Type2Object* obj = (Type2Object*)actor->m20_object;
        actor->m28_r = p[0];
        actor->m29_g = p[1];
        actor->m2A_b = p[2];
        if (actor->m3C_x0_type == 2) {
            obj->m38_color[0] = p[0];
            obj->m38_color[1] = p[1];
            obj->m38_color[2] = p[2];
        }
        if (actor->m3C_x0_type == 1) {
            updateAllSubsprites(actor);
        }
        break;
    }
    case 0xF2: {
        Type2Object* obj = (Type2Object*)actor->m20_object;
        actor->m28_r = addClamp255(actor->m28_r, SB(p, 0));
        actor->m29_g = addClamp255(actor->m29_g, SB(p, 1));
        actor->m2A_b = addClamp255(actor->m2A_b, SB(p, 2));
        if (actor->m3C_x0_type == 2) {
            obj->m38_color[0] += (s8)p[0];
            obj->m38_color[1] += (s8)p[1];
            obj->m38_color[2] += (s8)p[2];
        }
        if (actor->m3C_x0_type == 1) {
            updateAllSubsprites(actor);
        }
        if (actor->m40_draw.bits.xD_mode == 0xF) {
            Type2Object* o2 = (Type2Object*)actor->m20_object;
            if (o2->m34_modelBlock != NULL && !actor->m40_draw.bits.x1) {
                tintCustomPolyPrimitives(o2->m34_modelBlock, o2->m2C_primBuffer0, o2->m30_primBuffer1, obj->m38_color[0], obj->m38_color[1], obj->m38_color[2]);
            }
        }
        break;
    }
    case 0x90: {
        void* cur = actor->m44_currentAnimationBundle;
        void* def = actor->m48_defaultAnimationBundle;
        if (cur == def) {
            setAnimationBundle(actor, actor->m4C_specialAnimation);
            actor->mB0_xA = 1;
        } else {
            setAnimationBundle(actor, def);
            actor->mB0_xA = 0;
        }
        break;
    }
    case 0xF5: {
        u8* data;
        u8* block;
        resetMemoryAllocStats(5, 0);
        data = (u8*)(S24(p) + (sptr)p);
        block = data;
        relocateModelBlock(block);
        block = pcModelBlockOf(data);
        if (((Type2Object*)actor->m20_object)->m2C_primBuffer0 != NULL) {
            freeHeap(((Type2Object*)actor->m20_object)->m2C_primBuffer0);
        }
        initModel1(block, &((Type2Object*)actor->m20_object)->m2C_primBuffer0, &((Type2Object*)actor->m20_object)->m30_primBuffer1);
        initModel2(block, ((Type2Object*)actor->m20_object)->m2C_primBuffer0, 0);
        memcpy(((Type2Object*)actor->m20_object)->m30_primBuffer1, ((Type2Object*)actor->m20_object)->m2C_primBuffer0, MODEL_BLOCK_PRIM_BUFFER_SIZE(block));
        ((Type2Object*)actor->m20_object)->m34_modelBlock = block;
        break;
    }
    case 0xF6: {
        u8* data;
        u8* block;
        resetMemoryAllocStats(5, 0);
        data = (u8*)(S24(p) + (sptr)p);
        fieldModelRelocation(data);
        block = pcModelBlocks(data);
        if (((Type2Object*)actor->m20_object)->m2C_primBuffer0 != NULL) {
            freeHeap(((Type2Object*)actor->m20_object)->m2C_primBuffer0);
        }
        initModel1(block, &((Type2Object*)actor->m20_object)->m2C_primBuffer0, &((Type2Object*)actor->m20_object)->m30_primBuffer1);
        initModel2(block, ((Type2Object*)actor->m20_object)->m2C_primBuffer0, 0);
        memcpy(((Type2Object*)actor->m20_object)->m30_primBuffer1, ((Type2Object*)actor->m20_object)->m2C_primBuffer0, MODEL_BLOCK_PRIM_BUFFER_SIZE(block));
        ((Type2Object*)actor->m20_object)->m34_modelBlock = block;
        break;
    }
    case 0xF7: {
        u8* data;
        u8* block;
        data = (u8*)(S24(p) + (sptr)p);
        fieldModelRelocation(data);
        block = pcModelBlocks(data);
        if (((Type2Object*)actor->m20_object)->m2C_primBuffer0 != NULL) {
            freeHeap(((Type2Object*)actor->m20_object)->m2C_primBuffer0);
        }
        initModel1(block, &((Type2Object*)actor->m20_object)->m2C_primBuffer0, &((Type2Object*)actor->m20_object)->m30_primBuffer1);
        initModel2(block, ((Type2Object*)actor->m20_object)->m2C_primBuffer0, 0);
        memcpy(((Type2Object*)actor->m20_object)->m30_primBuffer1, ((Type2Object*)actor->m20_object)->m2C_primBuffer0, MODEL_BLOCK_PRIM_BUFFER_SIZE(block));
        (*(s32**)(unk + 0x10))[1] = 0;
        ((Type2Object*)actor->m20_object)->m34_modelBlock = block;
        break;
    }
    case 0xB5: {
        u8 b = p[0];
        if (actor->m3C_x0_type != 0) {
            setGraphicEntityScale(actor, (s16)(b << 8));
        }
        break;
    }
    case 0xE7:
        setGraphicEntityScale(actor, actor->m2C_scale + (s16)(S16A(p) * 2));
        break;
    case 0xE9: {
        s32 d = S16A(p) * 2;
        if (actor->m20_object != NULL) {
            actor->m20_object->m6_scale[0] = d + actor->m20_object->m6_scale[0];
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    }
    case 0xEA: {
        s32 d = S16A(p) * 2;
        if (actor->m20_object != NULL) {
            actor->m20_object->m6_scale[1] = d + actor->m20_object->m6_scale[1];
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    }
    case 0xEB: {
        s32 d = S16A(p) * 2;
        if (actor->m20_object != NULL) {
            actor->m20_object->m6_scale[2] = d + actor->m20_object->m6_scale[2];
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    }
    case 0xBD:
        spriteBytecode2ExtendedE0(actor,
                      (u16*)(((u16*)createSavePointMeshData_mode5.m10_startOfAnimationContainer)[p[0] + 1] +
                             createSavePointMeshData_mode5.m10_startOfAnimationContainer),
                      actor->m24_vramData);
        break;
    case 0xE0:
        spriteBytecode2ExtendedE0(actor, (u16*)(p + ((SB(p, 1) << 8) + p[0])), actor->m24_vramData);
        break;
    case 0xAD:
        actor->mA8_x1_bounce = p[0];
        break;
    case 0xB4:
        pushByteOnAnimationStack(actor, p[0]);
        break;
    case 0xB8: {
        s32 d = SB(p, 0);
        actor->m8C_stackPosition -= d;
        break;
    }
    case 0xB3:
        actor->mA8_xB_frame = SB(p, 0);
        break;
    case 0xAE: {
        s32 d = SB(p, 0) << 4;
        if (actor->mAC_x2_facing) {
            d = -d;
        }
        if (actor->m20_object != NULL) {
            actor->m20_object->m0_rotation[2] += d;
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    }
    case 0xB6:
        if (actor->m20_object != NULL) {
            actor->m20_object->m0_rotation[0] += (s8)p[0] << 4;
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    case 0xB7:
        if (actor->m20_object != NULL) {
            actor->m20_object->m0_rotation[1] += (s8)p[0] << 4;
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    case 0xAF: {
        s32 d = SB(p, 0) << 4;
        if (actor->mAC_x2_facing) {
            d = -d;
        }
        if (actor->m20_object != NULL) {
            actor->m20_object->m0_rotation[2] = d;
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    }
    case 0xC4: {
        s32 r = ((rand() & 0xFF) * p[0]) / 256 - (p[0] >> 1);
        r <<= 4;
        setSVector3(&sv, 0, 0, (s16)r);
        createRotationMatrix(&sv, &m);
        ApplyMatrixLV(&m, (VECTOR*)&actor->mC_step, &v);
        actor->mC_step.vx = v.vx;
        actor->mC_step.vy = v.vy;
        actor->mC_step.vz = v.vz;
        break;
    }
    case 0xAC: {
        s32 r = ((rand() & 0xFF) * p[0]) / 256 - (p[0] >> 1);
        r <<= 4;
        spriteActorSetDirection(actor, (s16)(actor->m32_direction + r));
        break;
    }
    case 0xA9: {
        s32 off = modulateSpeed(actor, (SB(p, 0) * actor->m2C_scale) / 4096) << 16;
        if (actor->mAC_x2_facing) {
            off = -off;
        }
        actor->m0_position.vx += off;
        break;
    }
    case 0xAA:
        actor->m0_position.vy += modulateSpeed(actor, (SB(p, 0) * actor->m2C_scale) / 4096) << 16;
        break;
    case 0xAB:
        actor->m0_position.vz += modulateSpeed(actor, (SB(p, 0) * actor->m2C_scale) / 4096) << 16;
        break;
    case 0xA8:
        actor->m32_direction += (s8)p[0] << 4;
        computeStepFromMoveSpeed(actor);
        break;
    case 0x8A:
        actor->mC_step.vx = 0;
        actor->mC_step.vz = 0;
        actor->m18_moveSpeed = 0;
        break;
    case 0xA3:
        if (actor->mA8_x0 == 1 && actor->m7C_extra->m4_gravityOverride != 0) {
            actor->m1C_gravity = actor->m7C_extra->m4_gravityOverride;
        } else {
            s32 g = ((SB(p, 0) << 6) * actor->m82_entityScale) / 4096;
            s32 inv = 0x10000 / actor->mAC_x7_timeScale;
            s32 sq = inv * inv;
            actor->m1C_gravity = g << 5;
            if (pcSpritePrecise) { /* Enhanced: the 60 Hz field's 20/15 fps rooms (sprite_actor.c) */
                actor->m1C_gravity = pcSpriteGravityScaled(actor->m1C_gravity, actor->mAC_x7_timeScale);
            } else {
            actor->m1C_gravity = actor->m1C_gravity * (sq / 256);
            actor->m1C_gravity = actor->m1C_gravity / 256;
            }
            actor->m1C_gravity =
                actor->m1C_gravity * ((fieldDrawEnvsInitialized + 1) * (fieldDrawEnvsInitialized + 1));
        }
        break;
    case 0xA5:
        actor->m18_moveSpeed +=
            (((SB(p, 0) << 4) * (fieldDrawEnvsInitialized + 1) * actor->m82_entityScale) / 4096) << 8;
        computeStepFromMoveSpeed(actor);
        break;
    case 0xA6:
        if (actor->mA8_x0 != 1) {
            actor->mC_step.vy +=
                ((((SB(p, 0) << 4) * (fieldDrawEnvsInitialized + 1) * actor->m82_entityScale) / 4096) << 16) /
                actor->mAC_x7_timeScale;
        }
        break;
    case 0xA0:
        actor->m18_moveSpeed = (((SB(p, 0) << 4) * (fieldDrawEnvsInitialized + 1) * actor->m82_entityScale) / 4096) << 8;
        computeStepFromMoveSpeed(actor);
        break;
    case 0xA1:
        if (actor->mA8_x0 == 1 && actor->m7C_extra->m0 != 0) {
            actor->mC_step.vy = actor->m7C_extra->m0;
        } else {
            actor->mC_step.vy =
                (((SB(p, 0) << 4) * (fieldDrawEnvsInitialized + 1) * actor->m82_entityScale) / 4096) << 8;
        }
        actor->mC_step.vy <<= 8;
        actor->mC_step.vy /= actor->mAC_x7_timeScale;
        break;
    case 0xED:
        actor->m0_position.vx = S16B(p) << 16;
        break;
    case 0xEE:
        actor->m0_position.vy = (actor->m84_maxY + (S16B(p) * actor->m2C_scale) / 4096) << 16;
        break;
    case 0xEF:
        actor->m0_position.vz = S16B(p) << 16;
        break;
    }
}
