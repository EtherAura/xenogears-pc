#ifndef FIELD_MECHA_MECHA_H
#define FIELD_MECHA_MECHA_H

/*
 * Gear (mecha) overlay types (disc 10/0C, loaded at 0x801DC000; Noah:
 * field/mecha/mechaOverlay.h/.cpp). Names follow Noah where it has them,
 * otherwise the most descriptive name the matching agents gave; fields are
 * offset-tagged. Layouts agree with the partial sMechaBone / sLoadedMechas /
 * ModelBlock views in include/field/field.h.
 */

#include "common.h"
#include "psyq.h"
#include "main/sprite.h"
#include "main/model.h"

/* ---- libgpu additions (not in psyq.h yet) ---- */
void SetPolyGT3(void* p);

/* ---- 3D model data (main's model code) ---- */

/* Noah: sModelBlock, sModel: main's model types (include/main/model.h). */
typedef ModelBlock sModelBlock;
typedef Model sModel;

/* ---- mecha data files ---- */

/* Noah: sMechaDataTable1_C, one bone hierarchy entry; the list ends at the
 * first geometry id >= the block count that is not 0xFFFF. (Noah: s16; the
 * code compares both halves unsigned.) */
typedef struct {
    /* 0x00 */ u16 m0_geometryId; /* 0xFFFF: no geometry */
    /* 0x02 */ u16 m2_parentBone; /* 0xFFFF: root */
} sMechaDataTable1_C;

/* Noah: sMechaDataTable1_10_4: the gear's parameter header, then the m10D
 * cloth records (u16 each). */
typedef struct {
    /* 0x00 */ u16 m0;
    /* 0x02 */ s16 m2_dimensions[3];
    /* 0x08 */ s16 m8_scale;
    /* 0x0A */ u8 mA_defaultKeyframe;
    /* 0x0B */ u8 mB;
    /* 0x0C */ u16 mC_flags;
    /* 0x0E */ u8 mE_count110;
    /* 0x0F */ u8 mF;
    /* 0x10 */ u8 m10_count118;
    /* 0x11 */ u8 m11;
    /* 0x12 */ u8 m12_count114;
    /* 0x13 */ u8 m13;
    /* 0x14 */ u16 m14_data[1];
} sMechaDataTable1_10_4;

/* Noah: sMechaDataTable1_10. */
typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ sMechaDataTable1_10_4* m4_params;
    /* 0x08 */ void* m8_clothData[1]; /* one per m114 entry */
} sMechaDataTable1_10;

/* Noah: sMechaDataTable1 (relocated file: count, then offsets). */
typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ void* m4_textures;
    /* 0x08 */ u8* m8_modelBlocks;
    /* 0x0C */ u8* mC_bones; /* sMechaDataTable1_C entries; ends the model data */
    /* 0x10 */ sMechaDataTable1_10* m10_gearInfo;
} sMechaDataTable1;

/* Noah: sMechaDataTable2_4_8, one word of an animation bytecode program. */
typedef u16 sMechaDataTable2_4_8;

/* Noah: sMechaDataTable2_4_4, an animation keyframe (raw file layout). After
 * the header: when m6 == 0, (mC_numRotations + 1) per-bone track headers of
 * three u16 (rotation data offset, translation data offset, two type bytes);
 * then the pose: rotations (x,y,z) unless m4_flags & 1, translations (x,y,z)
 * unless m4_flags & 2; then the track data. */
typedef struct sMechaDataTable2_4_4 {
    /* 0x00 */ u16 m0;
    /* 0x02 */ u16 m2_keyDuration;
    /* 0x04 */ u16 m4_flags; /* 1: no rotations, 2: no translations */
    /* 0x06 */ u16 m6_isStaticPose;       /* nonzero: static pose (no track headers) */
    /* 0x08 */ u16 m8;
    /* 0x0A */ u16 mA;
    /* 0x0C */ u16 mC_numRotations;
    /* 0x0E */ u16 mE_numTranslations;
    /* 0x10 */ s16 m10_stepDistance;
    /* 0x12 */ u16 m12_numEvents;      /* event count (mechaOP_11_b) */
    /* 0x14 */ u32 m14_eventsOffset;      /* offset of the event stream from this header */
    /* 0x18 */ u8 m18_data[1];
} sMechaDataTable2_4_4;

/* Noah: sMechaDataTable2_4::sMechaDataTable2_4_4_array: a count, then
 * keyframe pointers (loadedMechas m14: ids 0..0x3F, m18: ids 0x40..). */
typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ sMechaDataTable2_4_4* m4_keyframes[1];
} sMechaDataTable2_4_4_array;

/* Noah: sMechaDataTable2_4: keyframes, then the bytecode programs. */
typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ sMechaDataTable2_4_4_array* m4_keyframes;
    /* 0x08 */ sMechaDataTable2_4_8* m8_bytecode[1];
} sMechaDataTable2_4;

/* Noah: sMechaDataTable2_8: the gear's sound sequence. */
typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ void* m4_spriteBundle;
    /* 0x08 */ u8* m8_seq; /* sequence header; u16 at +0x14 is the sound id */
    /* 0x0C */ u8* mC_seqEnd;
} sMechaDataTable2_8;

/* Noah: sMechaDataTable2 (relocated file: count, then offsets). */
typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ sMechaDataTable2_4* m4_anim;
    /* 0x08 */ sMechaDataTable2_8* m8_sound;
} sMechaDataTable2;

/* A battle-loaded extra anim file (sLoadedMechas::mC). m4 is a relocated
 * sMechaDataTable2_4 seen as a pointer table: [0] count, [1] keyframes,
 * [2..] bytecode programs (animation ids 0x50..). */
typedef struct {
    /* 0x00 */ s32 m0;
    /* 0x04 */ sMechaDataTable2_4_8** m4_anim;
} sMechaExtraAnim;

/* One event of a keyframe's event stream (loadedMechas mA0/mA4; type in m2,
 * size depends on the type). Field meanings by type in updateMechAnimSub4. */
typedef struct {
    /* 0x00 */ s16 m0_frame;
    /* 0x02 */ u8 m2_type;
    /* 0x03 */ u8 m3_index;
    /* 0x04 */ u8 m4;
    /* 0x05 */ u8 m5;
    /* 0x06 */ u8 m6_tileMode;
    /* 0x07 */ u8 m7_stepFunctionId;
    /* 0x08 */ s16 m8_x;
    /* 0x0A */ s16 mA_y;
    /* 0x0C */ s16 mC_blendSource0;
    /* 0x0E */ s16 mE_blendSource1;
    /* 0x10 */ s16 m10_blendSource2;
    /* 0x12 */ u8 m12_sourceFlags;
    /* 0x13 */ u8 m13_width;
    /* 0x14 */ u8 m14_height;
    /* 0x15 */ u8 m15;
    /* 0x16 */ s16 m16_speed;
    /* 0x18 */ s16 m18_stepDivisor;
    /* 0x1A */ s16 m1A_stepBase;
} sMechaAnimEvent; /* type 9 is 0x1C bytes */

/* Event type 2 (light setup): bytes 7..9 are colours. */
typedef struct {
    /* 0x00 */ s16 m0_frame;
    /* 0x02 */ u8 m2_type;
    /* 0x03 */ u8 m3_lightIndex;
    /* 0x04 */ u8 m4_enable;
    /* 0x05 */ u8 m5_isAbsolute;
    /* 0x06 */ u8 m6_boneIndex;
    /* 0x07 */ u8 m7_r;
    /* 0x08 */ u8 m8_g;
    /* 0x09 */ u8 m9_b;
    /* 0x0A */ s16 mA_x;
    /* 0x0C */ s16 mC_y;
    /* 0x0E */ s16 mE_z;
    /* 0x10 */ s16 m10_isActive;
} sMechaAnimEventLight; /* 0x12 bytes */

/* ---- runtime ---- */

/* Noah: sMechaInitVar2Sub, one bone animation track (pool entry). Types
 * (m2_type & 0xF): 0 absolute s16 stream, 1 s8 delta stream (-0x80: an s16
 * absolute value follows), 2 s16 delta stream (translation: scaled and
 * rotated by the bone matrix), 3 linear interpolation, 4 approach target,
 * 5 accelerate, 7/8 turn toward a target (rotation only). 0x10/0x20/0x40
 * skip the x/y/z component of a stream. (Noah: m2/m3 s8; the code compares
 * both unsigned.) */
typedef struct sMechaInitVar2Sub {
    /* 0x00 */ u8 m0_inUse;
    /* 0x01 */ u8 m1_isLooping;
    /* 0x02 */ u8 m2_type;
    /* 0x03 */ u8 m3_tag; /* owner tag; 0xFF: locked, 0xFE: set by code */
    union {
        struct {
            /* 0x04 */ s8* m4_start;
            /* 0x08 */ s8* m8_cursor;
        } stream; /* types 0-2 (Noah: m4 / m8) */
        struct {
            /* 0x04 */ s16 m4_x; /* start / current value (types 7/8: distance + 1) */
            /* 0x06 */ s16 m6_y;
            /* 0x08 */ s16 m8_z;
            /* 0x0A */ s16 mA_x; /* delta / target */
            /* 0x0C */ s16 mC_y;
            /* 0x0E */ s16 mE_z;
        } v; /* types 3-8 (Noah: m4_s16, m6_s16, m8_s16, mA_s16, mC, mE) */
    } m4;
    /* 0x10 */ s16 m10_currentTime;
    /* 0x12 */ s16 m12_maxTime;
} sMechaInitVar2Sub; /* 0x14 */

/* Noah: sMechaInitVar2 (mechaInitVar2 at 0x801E86A8): the track pool. */
typedef struct sMechaInitVar2 {
    /* 0x00 */ sMechaInitVar2Sub* m0_tracks;
    /* 0x04 */ u16 m4_firstFree;
    /* 0x06 */ u16 m6_count;
} sMechaInitVar2;

/* Noah: sMechaInitVar3Sub, a fading quad (trail segment) with a
 * double-buffered POLY_FT4. The four corners are SVECTOR-shaped; their pad
 * halves hold m6/mE/m16/m1E. */
typedef struct sMechaInitVar3Sub {
    /* 0x00 */ s16 m0_x0; /* corner 0 */
    /* 0x02 */ s16 m2_y0;
    /* 0x04 */ s16 m4_z0;
    /* 0x06 */ s16 m6;
    /* 0x08 */ s16 m8_x1; /* corner 1 */
    /* 0x0A */ s16 mA_y1;
    /* 0x0C */ s16 mC_z1;
    /* 0x0E */ s16 mE_is3D; /* 0: the corners are screen coordinates */
    /* 0x10 */ s16 m10_x2; /* corner 2 */
    /* 0x12 */ s16 m12_y2;
    /* 0x14 */ s16 m14_z2;
    /* 0x16 */ s16 m16_age; /* age; -1: free */
    /* 0x18 */ s16 m18_x3; /* corner 3 */
    /* 0x1A */ s16 m1A_y3;
    /* 0x1C */ s16 m1C_z3;
    /* 0x1E */ s16 m1E_lifetime; /* lifetime */
    /* 0x20 */ u16 m20_rgb[3];     /* colour, 10.6 fixed point */
    /* 0x26 */ s16 m26_rgbStep[3]; /* colour change per tick */
    /* 0x2C */ POLY_FT4 m2C_polys[2];
} sMechaInitVar3Sub; /* 0x7C */

/* Noah: sMechaInitVar3 (mechaInitVar3 at 0x801E86A0): pool of m4_count
 * segments plus a spare one at [m4_count] (returned when the pool is full). */
typedef struct sMechaInitVar3 {
    /* 0x00 */ sMechaInitVar3Sub* m0_entries;
    /* 0x04 */ s16 m4_count;
    /* 0x06 */ s16 m6_next; /* first free */
} sMechaInitVar3;

/* Noah: sMechaInitVar4 (mechaInitVar4[8] at 0x801E85F4): model block list. */
typedef struct {
    /* 0x00 */ sModelBlock** m0_blocks;
    /* 0x04 */ u32 m4_count;
} sMechaInitVar4;

/* Noah: sMechaInitVar5 (mechaInitVar5[2] at 0x801E8648): the two extra
 * light positions set by animation event type 2, each a point attached to a
 * mecha bone; refreshAttachedMechaTransforms writes its world position to m0. */
typedef struct sMechaInitVar5 {
    /* 0x00 */ s16 m0_worldPosition[3];  /* world position (output) */
    /* 0x06 */ s16 m6_isActive;     /* active */
    /* 0x08 */ SVECTOR m8_localPosition; /* position in bone space (absolute if m10 < 0) */
    /* 0x10 */ s16 m10_mechaIndex;    /* loadedMechas index; -1: m8 is absolute */
    /* 0x12 */ s16 m12_boneIndex;    /* bone index (bones[m12 + 1]) */
} sMechaInitVar5; /* 0x14 */

/* Noah: sMechaBone. Element 0 is a root whose mA_numBones is the count
 * (including itself); the others hold their 1-based index there. (Noah:
 * m8/mA s16; the code compares m8 with 0xFFFF and indexes with it
 * unsigned.) */
typedef struct sMechaBone {
    /* 0x00 */ struct sMechaBone* m0_parentBone;
    /* 0x04 */ u8 m4_needTranslationUpdate;
    /* 0x05 */ u8 m5_needRotationUpdate;
    /* 0x06 */ u8 m6_useYXZRotation; /* nonzero: RotMatrixYXZ, else createRotationMatrix */
    /* 0x07 */ u8 m7_isEnabled;
    /* 0x08 */ u16 m8_geometryId; /* 0xFFFF: none */
    /* 0x0A */ u16 mA_numBones;
    /* 0x0C */ MATRIX mC_localMatrix;
    /* 0x2C */ MATRIX m2C_boneFinalMatrix;
    /* 0x4C */ s16 m4C_scale[3];
    /* 0x52 */ s16 m52_billboardMode;
    /* 0x54 */ SVECTOR m54_rotationAngles;
    /* 0x5C */ s32 m5C_translation[3];
    /* 0x68 */ u8* m68_prims[2]; /* double-buffered primitive buffers (Noah: m68) */
    /* 0x70 */ sMechaInitVar2Sub* m70_animTracks[3]; /* rotation, translation, scale */
} sMechaBone; /* 0x7C */

/* Noah: sLoadedMechas::sLoadedMechas_110, a bone-attached trail emitter
 * feeding sMechaInitVar3 segments. */
typedef struct sLoadedMechas_110 {
    /* 0x00 */ s16 m0_boneIndex; /* < 0 (-1): unused */
    /* 0x02 */ u8 m2_mode;       /* 0: screen space (rtps), else world space (rt) */
    /* 0x03 */ u8 m3_semiTrans;            /* semi-transparency of the segments */
    /* 0x04 */ sMechaInitVar3* m4_pool;
    /* 0x08 */ sMechaInitVar3Sub* m8_segment; /* current segment (Noah: u32 m8) */
    /* 0x0C */ SVECTOR mC_p0; /* the two edge points in bone space */
    /* 0x14 */ SVECTOR m14_p1;
    /* 0x1C */ union {
        s16 xy[8][2]; /* screen-space ring (8 entries) */
        VECTOR v[2];  /* world space: current / previous */
    } m1C_hist0, m3C_hist1;
    /* 0x5C */ s16 m5C_head;
    /* 0x5E */ s16 m5E_count;
    /* 0x60 */ s16 m60_maxCount;
    /* 0x62 */ s16 m62_lifetime;             /* segment lifetime (-> sMechaInitVar3Sub m1E) */
    /* 0x64 */ s16 m64_rgb[3];      /* segment colour (-> m20_rgb) */
    /* 0x6A */ s16 m6A_rgbStep[3];  /* (-> m26_rgbStep) */
} sLoadedMechas_110; /* 0x70 */

/* Soft-body (cloth/cape) vertex. sLoadedMechas_114::m1C_chains point into
 * one array of these (m1C_chains[0] is the whole vertex array); each chain
 * ends at a vertex with m0_weight == 0. */
typedef struct {
    /* 0x00 */ s16 m0_weight;       /* 0 terminates a chain */
    /* 0x02 */ s16 m2_gravity;      /* added to the y pull */
    /* 0x04 */ SVECTOR m4_position; /* .pad: face count for the normal average */
    /* 0x0C */ s32 mC_normal[3];
} sMechaClothVertex; /* 0x18 */

/* Sphere collider for the cloth vertices, attached to a bone. */
typedef struct {
    /* 0x00 */ SVECTOR m0_pos; /* in bone space; .pad: bone index */
    /* 0x08 */ s16 m8_x;       /* world position (submitMechaForRendering) */
    /* 0x0A */ s16 mA_y;
    /* 0x0C */ s16 mC_z;
    /* 0x0E */ s16 mE_radius;
} sMechaClothCollider; /* 0x10 */

/* Cloth triangle: three vertex indices and a double-buffered POLY_GT3. */
typedef struct {
    /* 0x00 */ s16 m0_vertex[3];
    /* 0x06 */ s16 m6;
    /* 0x08 */ POLY_GT3 m8_poly[2];
} sMechaClothTriangle; /* 0x58 */

/* Noah: sLoadedMechas::sLoadedMechas_114, a cloth/soft-body part (Noah only
 * has the size). Built by initMechaCloth, simulated and drawn by
 * updateAndRenderMechaCloth. */
typedef struct sLoadedMechas_114 {
    /* 0x00 */ s16 m0_boneIndex; /* < 0: unused */
    /* 0x02 */ s16 m2;
    /* 0x04 */ s16 m4_numChains;
    /* 0x06 */ s16 m6_numTriangles;
    /* 0x08 */ s16 m8_numVertices;
    /* 0x0A */ s16 mA_numColliders;
    /* 0x0C */ u8 mC_frontColor[3];
    /* 0x0F */ u8 mF_backColor[3];
    /* 0x12 */ s16 m12;
    /* 0x14 */ SVECTOR* m14_vertices; /* per chain: its root in bone space; NULL: part unused */
    /* 0x18 */ sMechaClothCollider* m18_colliders;
    /* 0x1C */ sMechaClothVertex** m1C_chains;
    /* 0x20 */ sMechaClothTriangle* m20_triangles;
} sLoadedMechas_114; /* 0x24 */

/* Rows of sLoadedMechas_118::m1C (6 bytes, indexed [row].m0[column]); in
 * modes 4/5 this is the colour matrix. */
typedef struct sLoadedMechas_118_1C {
    /* 0x00 */ u16 m0_values[3];
} sLoadedMechas_118_1C;

/* Noah: sLoadedMechas::sLoadedMechas_118, a CLUT (modes 0/1) or colour
 * matrix (modes 4/5) animation. m24 computes the current step from
 * m14/m20/m22; m10 selects how the target (m28 rect in VRAM, or m1C rows)
 * is rebuilt. */
typedef struct sLoadedMechas_118 {
    /* 0x00 */ struct sLoadedMechas_118* m0_parent; /* parent/overlapped entry */
    /* 0x04 */ u16* m4_baseColors;  /* source buffer */
    /* 0x08 */ u16* m8_blendColors;
    /* 0x0C */ u16* mC_outputColors;
    /* 0x10 */ u8 m10_mode;   /* mode */
    /* 0x11 */ u8 m11_dirty;
    /* 0x12 */ s16 m12_numColors;
    /* 0x14 */ u16 m14_time;
    /* 0x16 */ u16 m16_speed;
    /* 0x18 */ u16 m18_level;
    /* 0x1A */ u16 m1A_isActive;  /* active (Noah: s16) */
    /* 0x1C */ sLoadedMechas_118_1C* m1C_colorMatrix;
    /* 0x20 */ s16 m20_stepDivisor;
    /* 0x22 */ s16 m22_stepBase;
    /* 0x24 */ s16 (*m24)();
    /* 0x28 */ RECT m28_rect;
} sLoadedMechas_118; /* 0x30 */

/* Noah: sLoadedMechas (loadedMechas[10] at 0x801E8670). */
typedef struct sLoadedMechas {
    /* 0x000 */ sMechaInitVar4* m0_model;
    /* 0x004 */ sMechaBone* m4_bones;
    /* 0x008 */ sMechaDataTable2_4_8** m8_bytecodeTable;
    /* 0x00C */ sMechaExtraAnim* mC_extraAnim;
    /* 0x010 */ sMechaDataTable2_4_8* m10_bytecode0;
    /* 0x014 */ sMechaDataTable2_4_4_array* m14_keyframes;
    /* 0x018 */ sMechaDataTable2_4_4_array* m18_extraKeyframes;
    /* 0x01C */ s16 m1C_scale;
    /* 0x01E */ s16 m1E_terrainTriangle;
    /* 0x020 */ u8 m20_mechaEntryId;
    /* 0x021 */ u8 m21_sourceEntryId;
    /* 0x022 */ u8 m22_isBattleGear;
    /* 0x023 */ u8 m23_extraLoadPending;
    /* 0x024 */ s16 m24_dimensions[3]; /* m24[1] / m24[2]: shadow half size x / z */
    /* 0x02A */ u8 m2A_animQueue[10]; /* [0] keyframe override, [1] queue count, [2..5] queued mecha ids, [6..9] anim ids */
    /* 0x034 */ u8 m34_visible;
    /* 0x035 */ u8 m35_isEventAnimation;
    /* 0x036 */ u8 m36_disableGroundSnap;
    /* 0x037 */ u8 m37_useBoneScale;
    /* 0x038 */ u8 m38_syncFlag;
    /* 0x039 */ u8 m39_shadowShade;
    /* 0x03A */ s16 m3A_savedAnimMask;
    /* 0x03C */ u16 m3C_trackTag; /* track owner id (Noah: s16; loaded with lhu) */
    /* 0x03E */ s16 m3E_clothPullY;
    /* 0x040 */ s16 m40_waitTimer;
    /* 0x042 */ u16 m42_loopCount; /* Noah: s16 (lhu in processMechaAnimData) */
    /* 0x044 */ u16 m44_delayTimer; /* Noah: s16 (sltu against m46) */
    /* 0x046 */ u16 m46_delay; /* Noah: s16 */
    /* 0x048 */ s16 m48_triggerDistance;
    /* 0x04A */ u16 m4A_flags; /* 1: no shadow, 4: no texture override, 8, 0x10: keep m34, 0x200 */
    /* 0x04C */ sMechaDataTable2_4_8* m4C_bytecode3;
    /* 0x050 */ sMechaDataTable2_4_8* m50_bytecode2;
    /* 0x054 */ sMechaDataTable2_4_8* m54_bytecode1;
    /* 0x058 */ s16 m58_targetMecha;
    /* 0x05A */ s16 m5A_targetBone;
    /* 0x05C */ u8 m5C_attachMecha; /* attached-to mecha, 0xFF: none (Noah: s8) */
    /* 0x05D */ u8 m5D_attachRotation; /* Noah: s8 */
    /* 0x05E */ s16 m5E_attachBone;
    /* 0x060 */ s16 m60_groundY;
    /* 0x062 */ u8 m62_isSeqLoaded;
    /* 0x063 */ u8 m63_isSeq2Loaded;
    /* 0x064 */ s16 m64_targetOffsetX; /* m64..m6A: target offset (loaded as one SVECTOR in updateMechaTargetPosition) */
    /* 0x066 */ s16 m66_targetOffsetY;
    /* 0x068 */ s16 m68_targetOffsetZ;
    /* 0x06A */ s16 m6A_attachOffset[3]; /* attach offset */
    /* 0x070 */ s16 m70_angularVelocity[3];
    /* 0x076 */ s16 m76_angularAcceleration[3];
    /* 0x07C */ s16 m7C_velocity[3];
    /* 0x082 */ s16 m82_acceleration[3];
    /* 0x088 */ s16 m88_targetPosition[3]; /* target position */
    /* 0x08E */ s16 m8E_stepDistance;
    /* 0x090 */ s16 m90_vramCoords[3]; /* tpage x, tpage y, clut x (m90[0] < 0: none) */
    /* 0x096 */ s16 m96_clutY;    /* clut y */
    /* 0x098 */ s16 m98_eventFrame;    /* event frame; < 0: no event stream */
    /* 0x09A */ s16 m9A_eventLoopLength;    /* loop length; < 0: no loop */
    /* 0x09C */ s16 m9C_eventIndex;    /* events done */
    /* 0x09E */ s16 m9E_numEvents;    /* event count */
    /* 0x0A0 */ u8* mA0_eventCursor;    /* event cursor */
    /* 0x0A4 */ u8* mA4_eventStream;    /* event stream start */
    /* 0x0A8 */ sModel* mA8_model;
    /* 0x0AC */ sMechaDataTable2* mAC_animData;
    /* 0x0B0 */ sMechaDataTable2_8* mB0_sound;
    /* 0x0B4 */ sMechaDataTable2_8* mB4_extraSound;
    /* 0x0B8 */ POLY_FT4 mB8_Polys[2]; /* shadow quad */
    /* 0x108 */ s16 m108;
    /* 0x10A */ u16 m10A_targetMask; /* mask of mechas (Noah: s16) */
    /* 0x10C */ u8 m10C_num110;
    /* 0x10D */ u8 m10D_num114;
    /* 0x10E */ u8 m10E_num118;
    /* 0x10F */ u8 m10F;
    /* 0x110 */ sLoadedMechas_110* m110_trails;
    /* 0x114 */ sLoadedMechas_114* m114_cloths;
    /* 0x118 */ sLoadedMechas_118* m118_colorAnims;
    /* 0x11C */ s32 m11C_previousTranslation[3];
    /* 0x128 */ s32 m128_deltaTranslation[3];
} sLoadedMechas; /* 0x134 */

extern sLoadedMechas* loadedMechas[10];

/* Data a sprite effect keeps after its actor (extraSize 0x18 of
 * spawnEffectSprite) when spawnMechaBoneAttachedSprite attaches it to a mecha bone. */
typedef struct {
    /* 0x00 */ s32 m0;
    /* 0x04 */ void (*m4_updateCallback)(void*); /* the task's previous update function */
    /* 0x08 */ sLoadedMechas* m8_mecha;
    /* 0x0C */ s16 mC_boneIndex;
    /* 0x0E */ s16 mE_snapToMechaAltitude;
    /* 0x10 */ SVECTOR m10_offset; /* in bone space */
} sMechaBoneAttachment; /* 0x18 */

/* Attachment parameters read by spawnMechaBoneAttachedSprite (layout partly known). */
typedef struct {
    /* 0x00 */ u8 m0[5];
    /* 0x05 */ u8 m5_boneIndex;
    /* 0x06 */ s16 m6_offsetX;
    /* 0x08 */ s16 m8_offsetY;
    /* 0x0A */ s16 mA_offsetZ;
    /* 0x0C */ u8 mC_snapToMechaAltitude;
    /* 0x0D */ u8 mD[6];
    /* 0x13 */ u8 m13_attach;
} sMechaBoneAttachmentConfig;

/* Scratchpad matrices (Noah: matrixSP_1f800000 / _1f800020 / _1f800040) */
#define SCRATCH_MATRIX_00 ((MATRIX*)PSX_SCRATCH(0x1F800000))
#define SCRATCH_MATRIX_20 ((MATRIX*)PSX_SCRATCH(0x1F800020))
#define SCRATCH_MATRIX_40 ((MATRIX*)PSX_SCRATCH(0x1F800040))

/* ---- functions of this overlay (Noah names where known) ---- */

sMechaInitVar4* sMechaModel_init(sModel* model, sMechaInitVar4* out); /* sMechaModel_init */
sMechaBone* processMechaMesh(sMechaInitVar4* model, sMechaDataTable1_C* entries, s32 mode, s32 useTpageAndClut,
                          s16 tpageX, s16 tpageY, s16 clutX, s16 clutY); /* processMechaMesh */
s32 updateMechAnimSub1(sMechaBone* bones, s32 scale); /* updateMechAnimSub1 */
s32 updateMechAnimSub2(sMechaBone* bones, s32 scale); /* updateMechAnimSub2 */
void func_801DCC34(void);
void submitAllMechaBonesForRendering(sMechaInitVar4* model, sMechaBone* bones, MATRIX* m0, MATRIX* m1, s32 renderMode, OTag* ot,
                   s32 oddOrEven); /* submit all bones (no enable check) */
void freeMechaBones(sMechaBone* bones); /* free a bone array */
void freeMechaModelBlocks(sMechaInitVar4* p, s32 freeBlocks); /* freeMechaModelBlocks */
void submitMechaForRendering(sLoadedMechas* mecha, MATRIX* m1, MATRIX* m2, s32 renderMode, s32 arg4, OTag* ot,
                   s32 oddOrEven); /* submitMechaForRendering */
u32 updateMechAnimSub3(sMechaInitVar2* pool, sMechaBone* bones, s32 id, s32 speed); /* updateMechAnimSub3 */
s32 mechaSetPoseFromKeyframe(sMechaBone* bones, s16* keyframe); /* mechaSetPoseFromKeyframe */
s32 mechaOP_13(sMechaInitVar2* pool, sMechaBone* bones, sMechaDataTable2_4_4* kf, s32 len, u32 interpType,
                  u32 isLooping, s32 tag); /* mechaOP_13: tween to a keyframe */
void mechaOP_A(sMechaInitVar2* pool, sMechaBone* bones, s32 boneIndex, s32 mask); /* mechaGetKeyframe */
sMechaInitVar2* initMechaInitVar2(sMechaInitVar2* pool, s32 count); /* initMechaInitVar2 */
void freeMechaInitVar2(sMechaInitVar2* pool); /* freeMechaInitVar2 */
void initMechaInitVar2Sub0(sMechaInitVar2* pool); /* initMechaInitVar2Sub0 */
sMechaInitVar2Sub* allocateAnimTrack(sMechaInitVar2* pool); /* allocateAnimTrack */
s32 releaseMechaAnimTrack(sMechaInitVar2* pool, sMechaInitVar2Sub* track); /* releaseMechaAnimTrack */
s32 mechaOP_11(sMechaInitVar2* pool, sMechaBone* bones, sMechaDataTable2_4_4* kf, u32 isLooping,
                  s32 tag); /* mechaOP_11 */
s32 mechaSetupKeyframeTracksWithPose(sMechaInitVar2* pool, sMechaBone* bones, sMechaDataTable2_4_4* kf, u32 isLooping,
                  s32 tag); /* mechaOP_11 variant that also applies the first key as the pose */
void mechaOP_8(sMechaInitVar2* pool, sMechaBone* bones); /* mechaOP_A: release unlocked tracks */
void freeAnimTracks2(sMechaInitVar2* pool, sMechaBone* bones, u8 tag); /* freeAnimTracks2 */
sMechaInitVar3* initMechaInitVar3(sMechaInitVar3* pool, s32 count); /* initMechaInitVar3 */
void freeMechaInitVar3(sMechaInitVar3* pool); /* freeMechaInitVar3 */
void initMechaInitVar3Sub(sMechaInitVar3* pool); /* initMechaInitVar3Sub */
sMechaInitVar3Sub* allocateMechaTrailSegment(sMechaInitVar3* pool, s16 semiTrans); /* allocate a segment */
s32 releaseMechaTrailSegment(sMechaInitVar3* pool, sMechaInitVar3Sub* p); /* release a segment */
void drawMechaWheelTrail(sMechaInitVar3* pool, MATRIX* m, s32 speed, OTag* ot, s32 oddOrEven); /* draw the segments */
s32 initMechaTrailEmitter(sLoadedMechas_110* p, void* pool, s16 boneIndex, u8 mode, s16 maxCount, s16 lifetime, u8 r0,
                  u8 g0, u8 b0, u8 r1, u8 g1, u8 b1, s16 x0, s16 y0, s16 z0, s16 x1, s16 y1, s16 z1,
                  s16 semiTrans); /* init a trail emitter */
void stopMechaTrailEmitter(sLoadedMechas_110* p, s32 oddOrEven); /* stop a trail emitter */
s16 envTileStepSine(s16 t, s16 div, s16 base); /* sLoadedMechas_118 step functions (getEnvTileStepFunction) */
s32 envTileStepRampUp(s16 t, s16 div, s16 base);
s16 envTileStepRampDown(s16 t, s16 div, s16 base);
s16 envTileStepRampDownClamped(s16 t, s16 div, s16 base);
sLoadedMechas_118* initAnimatedEnvTextureTile(sLoadedMechas_118* p, sLoadedMechas_118* parent, u16 mode, u16 flags,
                                 sLoadedMechas_118_1C* src, s16 a5, s16 a6, s16 a7, s16 a8, s16 a9, s16 a10, s16 x,
                                 s16 y, s16 w, s16 h, s16 speed, s16 arg16, s16 arg17,
                                 s16 (*fn)()); /* init a CLUT / colour animation */
s16 updateAnimatedEnvTextureTile(sLoadedMechas_118* p, s32 arg1); /* update it */
void freeAnimatedEnvTextureTile(sLoadedMechas_118* p); /* free it */
void fadeEnvTileColorMatrix(sLoadedMechas_118* p, s16 level); /* m1C = m4 * level / 32 */
void lerpEnvTileColorMatrix(sLoadedMechas_118* p, s16 level); /* m1C = m8 + (m4 - m8) * level / 32 */
void refreshAttachedMechaTransforms(sLoadedMechas** mechas); /* update the mechaInitVar5 points */
/* initMechaCloth (build a cloth part): the cloth file (fm_801E1A14.c) defines
 * it with int gravity/scale/numColliders, the mecha loader (fm_801E34BC.c)
 * calls it through a short-parameter prototype; on the PS1 each declares its own. */
void initMechaCloth(sLoadedMechas_114* obj, u16* data, s32 gravity, s32 scale, s16 x, s16 y, s16 z, s32 numColliders,
                   s16 tpx, s16 tpy, s16 uSpan, s16 vSpan, s16 clutX, s16 clutY, u8 frontR, u8 frontG, u8 frontB,
                   u8 backR, u8 backG, u8 backB);
void updateAndRenderMechaCloth(sLoadedMechas_114* p, SVECTOR* pull, MATRIX* m, OTag* ot, s32 oddOrEven, s32 scale,
                   s16 floorY); /* simulate and draw a cloth part */
void freeMechaCloth(sLoadedMechas_114* p); /* free a cloth part */
void* getEnvTileStepFunction(s32 id); /* sLoadedMechas_118 step function by id */
void initMechaTransforms1(sLoadedMechas* mecha, sMechaInitVar2* pool, sMechaDataTable2_4_8** bytecode,
                   sMechaDataTable2_4_4_array* keyframes); /* initMechaTransforms1 */
void initMechaAnimation(sLoadedMechas* mecha, sLoadedMechas* source, sMechaInitVar2* pool,
                   s32 animationId); /* initMechaAnimation */
s32 updateMechAnim(sLoadedMechas* mecha, sMechaInitVar2* pool, s32 iterations, s32 oddOrEven,
                  s32 arg4); /* updateMechAnim */
void updateMechasBaseMatrixIsAttached(sLoadedMechas* mecha); /* updateMechasBaseMatrixIsAttached */
void processMechaAnimData(sLoadedMechas* mecha, sMechaInitVar2* pool, s32 flags, s32 iterations,
                   s32 arg4); /* animation bytecode interpreter */
void setupBoneRotationTrack(sMechaInitVar2* pool, sMechaBone* bone, s32 duration, s32 x, s32 y, s32 z); /* rotation track */
void setupBoneLookAtTrack(sMechaInitVar2* pool, sMechaBone* bone, s32 type, s32 arg3, s32 arg4, s32 duration, s32 x,
                   s32 y, s32 z); /* turn-toward track */
void mechaOP_11_b(sLoadedMechas* mecha, sMechaDataTable2_4_4* key, s32 loop); /* mechaOP_11_b */
s32 getMechaSoundEffectIdBase(sLoadedMechas* mecha, s32 which); /* sound id of source 0/1/2 */
void updateMechAnimSub4(sLoadedMechas* mecha, sMechaInitVar2* pool, s32 oddOrEven); /* updateMechAnimSub4 */
void mechaOP_19(sLoadedMechas* mecha); /* mechaOP_19 */
s32 computeMechaDistanceToTarget(sLoadedMechas* mecha); /* computeMechaDistanceToTarget */
void updateMechaTargetPosition(sLoadedMechas* mecha); /* target from another mecha's bone */
void detachMechaBoneSubtree(sMechaInitVar2* pool, s32 index, sMechaBone* bones, sMechaBone* bones2);
void reattachMechaBones(sMechaBone* bones, sMechaBone* bones2);
s16 projectOnPlaneNormalScaled(VECTOR* dir, VECTOR* a, VECTOR* b, s32 scale);
s32 getFirstAnimTargetMecha(void);
u8 getMechaBitfieldForAnim(sLoadedMechas* mecha, u8 id, u16* mask); /* battleGetMechaBitfieldForAnim */
sMechaDataTable2_4_4* mechaGetKeyframe(sLoadedMechas* mecha, u8 id, s32* flag); /* mechaGetKeyframe */
void setupBoneInterpolationTrack(sLoadedMechas* mecha, sMechaInitVar2* pool, sMechaBone* bone, u8 flags, u8 mode, u8 tag,
                   u8 loop, s16 x, s16 y, s16 z, s16 tx, s16 ty, s16 tz, s16 duration);
void setBoneVisibility(sLoadedMechas* mecha, sMechaBone* bone, s32 flags); /* setBoneVisibility */
void spawnMechaBoneAttachedSprite(SpriteVramData* vram, s32 animationId, s16* position, s16 direction, s16 scale,
                   sMechaBoneAttachmentConfig* config, sLoadedMechas* mecha);
void updateMechaBoneAttachedSprite(SpriteTask* task);
void mechaOp_62(sLoadedMechas* mecha, sMechaBone* bone, u8 flags, s16 x, s16 y, s16 z); /* mechaOp_62 */
void updateMechAnimSetupRootAltitude(sLoadedMechas* mecha); /* updateMechAnimSetupRootAltitude */
void getMechaBoneMatrix(MATRIX* out, void* unused, s32 mechaIndex, s32 boneIndex); /* getMechaBoneMatrix */
void setMechaBytecodeToggle(s32 arg0);
void mechaInitOverlay(s32 count); /* mechaInitOverlay */
void mechaInitNewMecha(s32 entryId, u16 flags, sMechaDataTable2* data2, sMechaDataTable1* data1, s16 tpageX, s16 tpageY,
                   s16 clutX, s16 clutY, SVECTOR* position); /* mechaInitNewMecha */
void renderMechas(MATRIX* m, MATRIX* args, OTag* ot, s32 oddOrEven, s32 flag); /* renderMechas */
void freeMechaModule(void); /* freeMechaModule */
void freeMecha(s32 index); /* freeMecha */
void mechaPlayAnimation(u16 index, u16 mask, s32 animationId); /* mechaPlayAnimation */
void mechaPlayAnimationFromSource(sLoadedMechas* other, u16 index, u16 mask, s32 animationId);
s32 initMechaForBattleSub0(s32 index);
s32 getMechaColliderRadius(s32 index);
void initMechSub110(sLoadedMechas* mecha); /* initMechSub110 */

/* The gear files as loaded keep their [count][offsets] tables
 * (include/pc_convert.h; port/src/game/field_mecha/convert.c). File 1
 * (sMechaDataTable1 and its gear info table) converts into one heap block that
 * mechaInitNewMecha frees; file 2 (sMechaDataTable2 with its sound, animation
 * and keyframe tables) into one block that owns the file: pcFreeMechaData2
 * frees both. */
sMechaDataTable1* pcConvertMechaData1(void* data);
sMechaDataTable2* pcConvertMechaData2(void* data);
void pcFreeMechaData2(sMechaDataTable2* table);
/* The PS1 stack words under the field's wall-slide edge buffer (the field's
 * src/field/f_8008083C.c); renderMechas leaves some of its own there. */
extern u32 pcSlideEdgeStack[4];

#endif /* FIELD_MECHA_MECHA_H */
