
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "pc_gameplay.h"
#include "main/memory.h"
#include "field/field.h"
#include "pc_interpolation.h"
extern FieldBss_800AF880 fieldSceneState;

extern u8 activeParticleEffectsTable[0x40];
extern s16 activeParticleEffectActors[0x40];
extern sParticleEffect* fieldParticleEffects[0x40];
extern s32 particleEditorConfigIndex;
extern s32 currentParticleCreatorId;
extern u8 cameraToParticleMapping[8];
extern s16 cameraTan;
extern s32 g_frameOddOrEven;
extern s32 gDepthDivider;
extern sFieldRenderContext* pCurrentFieldRenderingContext;
extern s32 cameraDeltaTan;
extern s32 isBackBufferRamCopyEnabled;
extern s32 fieldDebugDisable;
extern MATRIX g_currentProjectionMatrix;
void func_80281B00(const char* name);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
void RotTrans(SVECTOR* v0, VECTOR* v1, s32* flag);
VECTOR* ApplyRotMatrix(SVECTOR* v0, VECTOR* v1);
VECTOR* ApplyRotMatrixLV(VECTOR* v0, VECTOR* v1);
MATRIX* RotMatrixZYX(SVECTOR* r, MATRIX* m);
s32 interpolateParticleColor(s32 value, s32 step);
void projectAndRenderParticle(sParticle* p, MATRIX* proj, s16 angle, s32 sort, VECTOR* scale, s32 attachMode);
void copyMatrixRotation(MATRIX* dst, MATRIX* src); /* copy the rotation part */
s32 getAngleSin(s32 angle);
s32 getAngleCos(s32 angle);
s32 evalParticleRange(s32 range);
void initParticlesPoly(sParticle* particle, s32 shape, s32 abr);
/* LIBGTE setVector (libgte.h macro) */

void flushFrame(void);
void deleteParticleEffect(s32 index);
void updateParticle(sParticleConfig* cfg, sParticle* p, MATRIX* proj);
void spawnParticle(sParticleConfig* cfg, sParticle* p, s32* wait);

void resetParticleEffectsTable(void) {
    s32 i;

    for (i = 0; i < 0x40; i++) {
        activeParticleEffectsTable[i] = 0;
        activeParticleEffectActors[i] = -1;
    }
}

void deleteParticleEffect(s32 index) {
    s32 i;
    sParticleConfig* c;

    if (activeParticleEffectsTable[index] == 1) {
        c = fieldParticleEffects[index]->m0_configs;
        for (i = 0; i < 8; i++, c++) {
            if (c->m6_maxParticles) {
                freeHeap(c->m2C_particles);
            }
        }
        freeHeap(fieldParticleEffects[index]);
    }
    activeParticleEffectsTable[index] = 0;
    activeParticleEffectActors[index] = -1;
}

void stopActiveEffects(s32 index) {
    s32 i;
    sParticleConfig* c;

    if (activeParticleEffectsTable[index] == 1) {
        c = fieldParticleEffects[index]->m0_configs;
        for (i = 0; i < 8; i++, c++) {
            if (c->m6_maxParticles != 0) {
                c->m4_eWait = 0;
            }
        }
    }
}

void stopActiveEffect2(s32 index) {
    s32 i;
    s32 j;
    sParticleConfig* c;

    if (activeParticleEffectsTable[index] == 1) {
        c = fieldParticleEffects[index]->m0_configs;
        for (i = 0; i < 8; i++, c++) {
            if (c->m6_maxParticles) {
                c->m4_eWait = 0;
                for (j = 0; j < c->m6_maxParticles; j++) {
                    sParticle* p = c->m2C_particles;
                    p += j;
                    p->m4_life = 1;
                }
            }
        }
    }
}

void deleteAllParticleEffects(void) {
    s32 i;

    for (i = 0; i < 0x40; i++) {
        deleteParticleEffect(i);
    }
    flushFrame();
}

void resetParticleCreationSetup(s32 slot) {
    s32 i;
    s32 j;

    particleEditorConfigIndex = 0;
    for (i = 0; i < 8; i++) {
        particleCreationSetup.m0_configs[i].m52_actorId = slot;
        particleCreationSetup.m0_configs[i].m0 = 0;
        particleCreationSetup.m0_configs[i].m2_sWait = 0;
        particleCreationSetup.m0_configs[i].m4_eWait = 0x80;
        particleCreationSetup.m0_configs[i].m6_maxParticles = 0;
        setVector(&particleCreationSetup.m0_configs[i].mC_sPos, 0, 0, 0);
        setVector(&particleCreationSetup.m0_configs[i].m14_ePos, 0, -1000, 0);
        particleCreationSetup.m0_configs[i].m8_speed = 0x8000;
        particleCreationSetup.m0_configs[i].m50_attachScale = 0x800;
        particleCreationSetup.m0_configs[i].m24_speedMultiplier = 1;
        setVector(&particleCreationSetup.m0_configs[i].m1C_gravity, 0, 0, 0);
        particleCreationSetup.m0_configs[i].m28_eRange = 0x100;
        particleCreationSetup.m0_configs[i].m58_peWait = 0x1C;
        particleCreationSetup.m0_configs[i].m26_sRange = 0;
        particleCreationSetup.m0_configs[i].m2A_flags = 0;
        particleCreationSetup.m0_configs[i].m76_rotationAngle = 0;
        particleCreationSetup.m0_configs[i].m56_psWait = 1;
        particleCreationSetup.m0_configs[i].m54_shape = 0;
        setVector(&particleCreationSetup.m0_configs[i].m5A_scale, 0x1C8, 0x1C8, 0x1C8);
        setVector(&particleCreationSetup.m0_configs[i].m62_scaleOffset, 0x20, 0x20, 0x20);
        particleCreationSetup.m0_configs[i].m6A_color[0] = 0x80;
        particleCreationSetup.m0_configs[i].m6A_color[1] = 0x20;
        particleCreationSetup.m0_configs[i].m6A_color[2] = 0;
        particleCreationSetup.m0_configs[i].m6E_colorOffset[0] = -4;
        particleCreationSetup.m0_configs[i].m6E_colorOffset[1] = -1;
        particleCreationSetup.m0_configs[i].m6E_colorOffset[2] = 0;
        for (j = 0; j < 8; j++) {
            particleCreationSetup.m0_configs[i].m30_angleOffsets[j].vx = 0;
            particleCreationSetup.m0_configs[i].m30_angleOffsets[j].vy = 0;
        }
    }
}

void updateParticles(void) {
    MATRIX proj;
    s32 wait;
    s32 i;
    s32 j;
    s32 k;
    s32 alive;
    sParticleConfig* c;

    if (isBackBufferRamCopyEnabled != 0) {
        return;
    }
    proj = g_currentProjectionMatrix;
    for (i = 0; i < 0x40; i++) {
        alive = 0;
        if (activeParticleEffectsTable[i] == 1) {
            c = fieldParticleEffects[i]->m0_configs;
            for (j = 0; j < 8; c++, j++) {
                wait = 0;
                if (c->m6_maxParticles != 0) {
                    /* Enhanced (xg-hbc.11 phase 2): emitters spawn (a random
                     * draw each) and count their waits on the field's 30 Hz
                     * ticks only, so the scripts' RNG sequence stays the
                     * Original's; live particles still move and draw every
                     * frame (their steps are phase 3). */
                    s32 tick = !pcFieldOffTick();
                    if (c->m2_sWait == 0) {
                        for (k = 0; k < c->m6_maxParticles; k++) {
                            if (c->m2C_particles[k].m0_isActive == 0) {
                                if (c->m4_eWait != 0 && tick) {
                                    spawnParticle(c, &c->m2C_particles[k], &wait);
                                    updateParticle(c, &c->m2C_particles[k], &proj);
                                    alive = 1;
                                } else if (c->m4_eWait != 0) {
                                    alive = 1;
                                }
                            } else {
                                updateParticle(c, &c->m2C_particles[k], &proj);
                                alive = 1;
                            }
                        }
                        if (c->m4_eWait != 0) {
                            if (c->m4_eWait != 0x7FFF && tick) {
                                c->m4_eWait--;
                            }
                            alive = 1;
                        }
                    } else {
                        alive = 1;
                        if (tick) c->m2_sWait--;
                    }
                }
            }
            if (!alive) {
                deleteParticleEffect(i);
            }
        }
    }
    if (fieldDebugDisable == 0) {
        func_80281B00("PARTICLE  ");
    }
    if (0) {
        func_80281B00(""); /* retail keeps this unreferenced "" after the name */
    }
}

s32 evalParticleRange(s32 range) {
    return (rand() * range + 1) >> 15;
}

s32 findFreeParticleEffectSlot(void) {
    s32 i;

    for (i = 0; i < 0x40; i++) {
        if (activeParticleEffectsTable[i] == 0) {
            return i;
        }
    }
    return -1;
}

void stopParticleEffect(s32 actorId, s32 mode) {
    s32 i;

    for (i = 0; i < 0x40; i++) {
        if (activeParticleEffectActors[i] == actorId) {
            if (mode == 0) {
                fieldParticleEffects[i]->m0_configs[0].m4_eWait = 0;
                fieldParticleEffects[i]->m0_configs[0].m2_sWait = 0;
                stopActiveEffects(i);
            } else {
                fieldParticleEffects[i]->m0_configs[0].m4_eWait = 0;
                fieldParticleEffects[i]->m0_configs[0].m2_sWait = 0;
                stopActiveEffect2(i);
            }
        }
    }
}

s32 createParticleEffect(s32 actorId) {
    s32 i;
    s32 j;
    sParticleEffect* e;
    sParticleConfig* c;

    i = findFreeParticleEffectSlot();
    if (i == -1) {
        return -1;
    }
    resetMemoryAllocStats(8, 0);
    currentParticleCreatorId = actorId;
    activeParticleEffectsTable[i] = 1;
    activeParticleEffectActors[i] = actorId;
    e = allocateHeap(PC_HEAP_SIZED(sizeof(sParticleEffect), 0x3c0), 0);
    fieldParticleEffects[i] = e;
    *e = particleCreationSetup;
    c = e->m0_configs;
    for (i = 0; i < 8; i++, c++) {
        if (c->m6_maxParticles) {
            c->m2C_particles = allocateHeap(PC_HEAP_SIZED(c->m6_maxParticles * sizeof(sParticle), c->m6_maxParticles * 0xc0), 0);
            for (j = 0; j < c->m6_maxParticles; j++) {
                c->m2C_particles[j].m0_isActive = 0;
                initParticlesPoly(&c->m2C_particles[j], c->m54_shape, ((c->m2A_flags << 16) >> 24) + 1 & 3);
            }
        }
    }
    return 1;
}

s32 interpolateParticleColor(s32 value, s32 step) {
    if (step < 0) {
        value += step;
        if (value < 0) {
            value = 0;
        }
    } else {
        value += step;
        if (value > 0xFF) {
            value = 0xFF;
        }
    }
    return value;
}

void pcFieldPicParticle(const void* p, s32 x, s32 y, s32 z); /* f_80072140.c, the oracle */
void projectAndRenderParticle(sParticle* p, MATRIX* proj, s16 angle, s32 sort, VECTOR* scale, s32 attachMode) {
    MATRIX m;
    MATRIX local;
    MATRIX tmp;
    SVECTOR rot;
    VECTOR v;
    s32 depth;
    s32 z;

    rot.vx = 0;
    rot.vy = 0;
    rot.vz = angle;
    createRotationMatrix(&rot, &local);
    local.t[0] = p->m8_position.vx >> 12;
    local.t[1] = p->m8_position.vy >> 12;
    local.t[2] = p->m8_position.vz >> 12;
    if (attachMode == 3) {
        tmp = *proj;
        ScaleMatrix(&tmp, scale);
        CompMatrix(&tmp, &local, &m);
        copyMatrixRotation(&m, &local);
        v.vx = p->m38_scale.vx;
        v.vy = p->m38_scale.vy;
        v.vz = p->m38_scale.vz;
        ScaleMatrix(&m, &v);
        (p->m50_poly + g_frameOddOrEven)->r0 = p->m48_color[0];
        (p->m50_poly + g_frameOddOrEven)->g0 = p->m48_color[1];
        (p->m50_poly + g_frameOddOrEven)->b0 = p->m48_color[2];
        SetTransMatrix(&m);
        ScaleMatrix(&m, scale);
    } else {
        CompMatrix(proj, &local, &m);
        copyMatrixRotation(&m, &local);
        v.vx = p->m38_scale.vx;
        v.vy = p->m38_scale.vy;
        v.vz = p->m38_scale.vz;
        ScaleMatrix(&m, &v);
        (p->m50_poly + g_frameOddOrEven)->r0 = p->m48_color[0];
        (p->m50_poly + g_frameOddOrEven)->g0 = p->m48_color[1];
        (p->m50_poly + g_frameOddOrEven)->b0 = p->m48_color[2];
        SetTransMatrix(&m);
    }
    pcFieldPicParticle(p, local.t[0], local.t[1], local.t[2]); /* Enhanced oracle: where it is drawn (world) */
    SetRotMatrix(&m);
    z = RotAverage4(&p->mA0_worldspaceVertices[0], &p->mA0_worldspaceVertices[1], &p->mA0_worldspaceVertices[2],
                    &p->mA0_worldspaceVertices[3], &p->m50_poly[g_frameOddOrEven].x0, &p->m50_poly[g_frameOddOrEven].x1,
                    &p->m50_poly[g_frameOddOrEven].x2, &p->m50_poly[g_frameOddOrEven].x3, &depth, &depth) >> gDepthDivider;
    switch (sort) {
    case 0:
        depth = 1;
        break;
    case 1:
        depth = z - 16;
        break;
    case 2:
        depth = z;
        break;
    case 3:
        depth = z + 16;
        break;
    }
    if ((u32)(depth - 1) < 0xFFF) {
        pcInterpolationObjectType(p, p, PC_INTERP_PARTICLE);
        pcInterpolationModel(&p->m50_poly[g_frameOddOrEven]);
        pcInterpolationPrim(&p->m50_poly[g_frameOddOrEven]);
        pcInterpolationObject(0, 0);
        addPrim(&pCurrentFieldRenderingContext->mCC_OT[depth], &p->m50_poly[g_frameOddOrEven]);
    }
}

/* Enhanced (xg-hbc.19.3): a particle moves, scales and fades on the tick frames
 * (phase 3); the tick's picture shows it halfway through that step, the frame
 * between where it went (pcFieldDrawn, f_80072140.c). The step is undone for
 * the drawing only (the position and scale steps exactly, the colour steps as
 * far as their clamp allows) and put back. */
s32 pcFieldSubFrames(void);
s32 pcFieldSubIndex(void);
static u8 pcParticleBack(s32 c, s32 step, s32 k, s32 n) {
    s32 prev = c - step;
    prev = prev < 0 ? 0 : prev > 0xFF ? 0xFF : prev;
    return (u8)(prev + (c - prev) * k / n);
}
/* Which particles the tick stepped (and drew): the frames between draw those
 * through the step, not one the tick only placed (its pre-spawn delay ended)
 * and has not moved yet. Keyed by the particle, stamped with the tick. */
s32 pcFieldTickCount(void);
#define PC_PARTICLE_MARKS 4096
static struct { const sParticle* p; s32 tick; } pcParticleMarks[PC_PARTICLE_MARKS];
static u32 pcParticleSlot(const sParticle* p) {
    return (u32)(((uptr)p >> 4) * 2654435761u) & (PC_PARTICLE_MARKS - 1);
}
static void pcParticleMark(const sParticle* p) {
    u32 k = pcParticleSlot(p);
    s32 i;
    for (i = 0; i < 16; i++, k = (k + 1) & (PC_PARTICLE_MARKS - 1)) {
        if (pcParticleMarks[k].p == p || pcParticleMarks[k].tick != pcFieldTickCount()) break;
    }
    pcParticleMarks[k].p = p;
    pcParticleMarks[k].tick = pcFieldTickCount();
}
static s32 pcParticleMarked(const sParticle* p) {
    u32 k = pcParticleSlot(p);
    s32 i;
    for (i = 0; i < 16; i++, k = (k + 1) & (PC_PARTICLE_MARKS - 1)) {
        if (pcParticleMarks[k].p == p) return pcParticleMarks[k].tick == pcFieldTickCount();
    }
    return 0;
}
static void pcParticleDrawHalfway(sParticle* p, MATRIX* proj, sParticleConfig* cfg, VECTOR* scale) {
    VECTOR pos = p->m8_position;
    SVECTOR sc = p->m38_scale;
    u8 col[3];
    s32 n = pcFieldSubFrames();
    s32 k = pcFieldSubIndex() + 1;
    s32 i;

    for (i = 0; i < 3; i++) col[i] = p->m48_color[i];
    p->m8_position.vx -= (s32)(((long long)p->m18_positionStep.vx * (n - k)) / n);
    p->m8_position.vy -= (s32)(((long long)p->m18_positionStep.vy * (n - k)) / n);
    p->m8_position.vz -= (s32)(((long long)p->m18_positionStep.vz * (n - k)) / n);
    p->m38_scale.vx -= p->m40_scaleStep.vx * (n - k) / n;
    p->m38_scale.vy -= p->m40_scaleStep.vy * (n - k) / n;
    p->m38_scale.vz -= p->m40_scaleStep.vz * (n - k) / n;
    for (i = 0; i < 3; i++) p->m48_color[i] = pcParticleBack(col[i], p->m4C_colorStep[i], k, n);
    projectAndRenderParticle(p, proj, p->m6_rotationAngle, (cfg->m2A_flags >> 1) & 3, scale, (cfg->m2A_flags >> 4) & 3);
    p->m8_position = pos;
    p->m38_scale = sc;
    for (i = 0; i < 3; i++) p->m48_color[i] = col[i];
}

void updateParticle(sParticleConfig* cfg, sParticle* p, MATRIX* proj) {
    VECTOR v;
    SVECTOR rot;
    MATRIX mat;
    MATRIX mat2;
    VECTOR attach;
    VECTOR v2;
    VECTOR v3;
    VECTOR scale;
    s32 flag;
    s32 scaled;

    if (p->m2_life2 != 0) {
        if (pcFieldOffTick()) return; /* Enhanced phase 3: the pre-spawn delay counts ticks */
        if (--p->m2_life2 != 0) {
            return;
        }
        mat.t[2] = 0;
        mat.t[1] = 0;
        mat.t[0] = 0;
        scaled = 0;
        switch ((cfg->m2A_flags >> 4) & 3) {
        case 3:
            rot.vx = 0;
            rot.vy = fieldSceneState.m290_actorArray[cfg->m52_actorId].m4C_scriptEntity->m108_rotation3;
            rot.vz = 0;
            createRotationMatrix(&rot, &mat);
            attach.vx = fieldSceneState.m290_actorArray[cfg->m52_actorId].m4C_scriptEntity->m20_position.vx >> 16;
            attach.vy = fieldSceneState.m290_actorArray[cfg->m52_actorId].m4C_scriptEntity->m20_position.vy >> 16;
            attach.vz = fieldSceneState.m290_actorArray[cfg->m52_actorId].m4C_scriptEntity->m20_position.vz >> 16;
            cfg->m50_attachScale = fieldSceneState.m290_actorArray[cfg->m52_actorId].m4C_scriptEntity->mF4_scale3d[0];
            scaled = 1;
            break;
        case 0:
            rot.vx = 0;
            rot.vy = fieldSceneState.m290_actorArray[cfg->m52_actorId].m4C_scriptEntity->m108_rotation3;
            rot.vz = 0;
            createRotationMatrix(&rot, &mat);
            attach.vx = fieldSceneState.m290_actorArray[cfg->m52_actorId].m4C_scriptEntity->m20_position.vx >> 16;
            attach.vy = fieldSceneState.m290_actorArray[cfg->m52_actorId].m4C_scriptEntity->m20_position.vy >> 16;
            attach.vz = fieldSceneState.m290_actorArray[cfg->m52_actorId].m4C_scriptEntity->m20_position.vz >> 16;
            cfg->m50_attachScale = 0x1000;
            break;
        case 1:
            /* An empty gear slot leaves mat untouched (see OPX_AF): identity
             * instead of the stack's leftovers. */
            mat.m[0][0] = mat.m[1][1] = mat.m[2][2] = 0x1000;
            mat.m[0][1] = mat.m[0][2] = mat.m[1][0] = mat.m[1][2] = mat.m[2][0] = mat.m[2][1] = 0;
            mat.t[0] = mat.t[1] = mat.t[2] = 0;
            getMechaBoneMatrix(&mat, &mat2, cfg->m72_mechaIndex, cfg->m74_boneIndex);
            SetRotMatrix(&mat);
            SetTransMatrix(&mat);
            rot.vx = cfg->mC_sPos.vx;
            rot.vy = cfg->mC_sPos.vy;
            rot.vz = cfg->mC_sPos.vz;
            RotTrans(&rot, &attach, &flag);
            cfg->m50_attachScale = 0x1000;
            break;
        case 2:
            mat = fieldSceneState.m290_actorArray[cfg->m52_actorId].m2C_matrixBackup;
            SetRotMatrix(&mat);
            SetTransMatrix(&mat);
            rot.vx = cfg->mC_sPos.vx;
            rot.vy = cfg->mC_sPos.vy;
            rot.vz = cfg->mC_sPos.vz;
            RotTrans(&rot, &attach, &flag);
            cfg->m50_attachScale = 0x1000;
            break;
        }
        mat.t[2] = 0;
        mat.t[1] = 0;
        mat.t[0] = 0;
        SetRotMatrix(&mat);
        SetTransMatrix(&mat);
        rot.vx = p->m18_positionStep.vx;
        rot.vy = p->m18_positionStep.vy;
        rot.vz = p->m18_positionStep.vz;
        ApplyRotMatrix(&rot, &v);
        VectorNormal(&v, &p->m18_positionStep);
        p->m18_positionStep.vx = ((p->m18_positionStep.vx * cfg->m8_speed) >> 12) * cfg->m24_speedMultiplier;
        p->m18_positionStep.vy = ((p->m18_positionStep.vy * cfg->m8_speed) >> 12) * cfg->m24_speedMultiplier;
        p->m18_positionStep.vz = ((p->m18_positionStep.vz * cfg->m8_speed) >> 12) * cfg->m24_speedMultiplier;
        if (scaled == 1) {
            p->m8_position.vx = (p->m8_position.vx * cfg->m50_attachScale) >> 12;
            p->m8_position.vy = (p->m8_position.vy * cfg->m50_attachScale) >> 12;
            p->m8_position.vz = (p->m8_position.vz * cfg->m50_attachScale) >> 12;
        }
        SetRotMatrix(&mat);
        SetTransMatrix(&mat);
        rot.vx = p->m8_position.vx;
        rot.vy = p->m8_position.vy;
        rot.vz = p->m8_position.vz;
        RotTrans(&rot, &v, &flag);
        if (scaled == 1) {
            rot.vz = 0;
            rot.vx = cameraDeltaTan - 0x400;
            rot.vy = -cameraTan;
            RotMatrixZYX(&rot, &mat2);
            SetRotMatrix(&mat2);
            SetTransMatrix(&mat2);
            v2.vx = 0;
            v2.vz = 0;
            v2.vy = v.vy;
            ApplyRotMatrixLV(&v2, &v3);
            v.vx += v3.vx;
            v.vz += v3.vz;
            v.vy = v3.vy;
            p->m8_position.vx = (attach.vx + v.vx) * R3000_DIV(0x1000000, cfg->m50_attachScale);
            p->m8_position.vy = (attach.vy + v.vy) * R3000_DIV(0x1000000, cfg->m50_attachScale);
            p->m8_position.vz = (attach.vz + v.vz) * R3000_DIV(0x1000000, cfg->m50_attachScale);
        } else {
            p->m8_position.vx = (attach.vx + v.vx) << 12;
            p->m8_position.vy = (attach.vy + v.vy) << 12;
            p->m8_position.vz = (attach.vz + v.vz) << 12;
        }
    } else {
        if (!pcFieldOffTick()) { /* Enhanced phase 3: a particle moves, scales and fades on the tick frames */
        p->m18_positionStep.vx += p->m28_positionStep2.vx;
        p->m18_positionStep.vy += p->m28_positionStep2.vy;
        p->m18_positionStep.vz += p->m28_positionStep2.vz;
        p->m8_position.vx += p->m18_positionStep.vx;
        p->m8_position.vy += p->m18_positionStep.vy;
        p->m8_position.vz += p->m18_positionStep.vz;
        p->m38_scale.vx += p->m40_scaleStep.vx;
        p->m38_scale.vy += p->m40_scaleStep.vy;
        p->m38_scale.vz += p->m40_scaleStep.vz;
        p->m48_color[0] = interpolateParticleColor(p->m48_color[0], p->m4C_colorStep[0]);
        p->m48_color[1] = interpolateParticleColor(p->m48_color[1], p->m4C_colorStep[1]);
        p->m48_color[2] = interpolateParticleColor(p->m48_color[2], p->m4C_colorStep[2]);
        }
        scale.vx = cfg->m50_attachScale;
        scale.vy = cfg->m50_attachScale;
        scale.vz = cfg->m50_attachScale;
        if (p->m4_life != 1) {
            if (pcFieldSubFrames() > 1 && (!pcFieldOffTick() ? (pcParticleMark(p), 1) : pcParticleMarked(p))) {
                pcParticleDrawHalfway(p, proj, cfg, &scale);
            } else {
                projectAndRenderParticle(p, proj, p->m6_rotationAngle, (cfg->m2A_flags >> 1) & 3, &scale, (cfg->m2A_flags >> 4) & 3);
            }
        } else if (pcFieldOffTick() && cfg->m58_peWait != 1) {
            /* Enhanced (xg-hbc.19.3): the last tick drew it for the last time (its life 2 -> 1); the
             * native frame shows it its two vblanks, so the frame between draws it where it went */
            projectAndRenderParticle(p, proj, p->m6_rotationAngle, (cfg->m2A_flags >> 1) & 3, &scale, (cfg->m2A_flags >> 4) & 3);
        }
        if (!pcFieldOffTick() && --p->m4_life == 0) { /* Enhanced: life counts ticks */
            p->m0_isActive = 0;
        }
    }
}

void spawnParticle(sParticleConfig* cfg, sParticle* p, s32* wait) {
    VECTOR unused; /* reserves retail's 16-byte stack slot */
    VECTOR start;
    VECTOR end;
    s32 range;
    s32 angle;
    s32 idx;

    pcInterpolationLifetime(p);
    p->m0_isActive = 1;
    p->m2_life2 = cfg->m56_psWait + *wait;
    *wait += cfg->m56_psWait;
    p->m4_life = cfg->m58_peWait;
    if (cfg->m2A_flags & 1) {
        p->m6_rotationAngle = rand() & 0xFFF;
    } else {
        p->m6_rotationAngle = cfg->m76_rotationAngle;
    }
    if (!(cfg->m2A_flags & 0x80)) {
        range = evalParticleRange(cfg->m26_sRange);
    } else {
        range = cfg->m26_sRange;
    }
    angle = evalParticleRange(0xFFF);
    start.vx = (getAngleSin(angle) * range) >> 12;
    if (!(cfg->m2A_flags & 0x40)) {
        start.vz = (getAngleCos(angle) * range) >> 12;
    } else {
        start.vz = 0;
    }
    idx = cameraToParticleMapping[((cameraTan + fieldSceneState.m290_actorArray[cfg->m52_actorId].m4C_scriptEntity->m108_rotation3) & 0xFFFU) >> 9];
    start.vx += cfg->mC_sPos.vx + cfg->m30_angleOffsets[idx].vx;
    start.vz += cfg->mC_sPos.vz + cfg->m30_angleOffsets[idx].vy;
    start.vy = cfg->mC_sPos.vy;
    p->m8_position.vx = start.vx;
    p->m8_position.vz = start.vz;
    p->m8_position.vy = start.vy;
    range = evalParticleRange(cfg->m28_eRange);
    end.vx = cfg->m14_ePos.vx + ((getAngleSin(angle) * range) >> 12);
    end.vz = cfg->m14_ePos.vz + ((getAngleCos(angle) * range) >> 12);
    end.vy = cfg->m14_ePos.vy;
    p->m18_positionStep.vx = end.vx - start.vx;
    p->m18_positionStep.vy = end.vy - start.vy;
    p->m18_positionStep.vz = end.vz - start.vz;
    p->m28_positionStep2.vx = cfg->m1C_gravity.vx;
    p->m28_positionStep2.vy = cfg->m1C_gravity.vy;
    p->m28_positionStep2.vz = cfg->m1C_gravity.vz;
    p->m38_scale.vx = cfg->m5A_scale.vx;
    p->m38_scale.vy = cfg->m5A_scale.vy;
    p->m38_scale.vz = cfg->m5A_scale.vz;
    p->m40_scaleStep.vx = cfg->m62_scaleOffset.vx;
    p->m40_scaleStep.vy = cfg->m62_scaleOffset.vy;
    p->m40_scaleStep.vz = cfg->m62_scaleOffset.vz;
    p->m48_color[0] = cfg->m6A_color[0];
    p->m48_color[1] = cfg->m6A_color[1];
    p->m48_color[2] = cfg->m6A_color[2];
    p->m4C_colorStep[0] = cfg->m6E_colorOffset[0];
    p->m4C_colorStep[1] = cfg->m6E_colorOffset[1];
    p->m4C_colorStep[2] = cfg->m6E_colorOffset[2];
}
