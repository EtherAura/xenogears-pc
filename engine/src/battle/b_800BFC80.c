/*
 * Running battle animations and effect tasks (Noah battle.cpp:
 * updateRunningAnimations). 0x800BFC80-0x800C11CC; gcc-2.7.2-cdk, ASPSX 2.56.
 */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/task.h"
#include "main/sprite.h"
#include "battle/battle.h"
#include "pollhost.h"
#include "gte.h"

s32 processBattleAnimationSub0(void);
void waitForBattleAnimationsToSettle(void);
void evaluateCurvePoint(s32 a, s32 b, SVECTOR* v, VECTOR* out);

/* main */
extern TaskHeader* spriteCallback2Var2;
extern u8 spriteBytecode2ExtendedE0_Var0;
extern SpriteVramData createSavePointMeshData_mode5;
SpriteActorCore* spriteBytecode2ExtendedE0(SpriteActorCore* actor, u16* anim, SpriteVramData* vram);
void unloadSequence(void* data);
void loadSequence(void* data);
void shrinkAllocation(void* p, s32 size);
void initFieldEntitySub4Sub5Sub0(SpriteVramData* vram, void* bundle, Vec2s16 vramLocation, Vec2s16 clut, s32 unused);
extern s32 runningOnDTL;
extern SpriteVramData createSavePointMeshData_mode6;

/* battle */
extern Wds* loadWdsDataNeeded;
extern u8 battleAnimationSoundLoaded;
extern u16 jumpAnimationActiveActorBF;
extern SpriteActorCore* processBattleAnimationSub0_var1;
extern u16 processBattleAnimationSub0_var0;
extern s32 (*pCurveSegmentWeights)[4];
extern s32 spriteTrailSegmentIndex;
void performMechaPlayAnimation(s32 a, s32 b, s32 c);
void updateBattleAnimationDataLoading(void);
void battleRender(void);
void clearAnimationsEndOfTurnSub(void);
extern u16 statusToAnimationTable[10]; /* by getEntityStatusAnimationIndex (0..8) */
void battleAnimationCallback_5(s32 entity);

/* Entity index of a battle sprite: a 4-bit field straddling 0xA8/0xAC (cdk
 * packs bitfields across words). */
#if VERSION_JP
#define SPRITE_ENTITY(a) ((a)->mAC_x0)
#else
#define SPRITE_ENTITY(a) ({ s32 lo_ = (a)->mA8_x1E; ((a)->mAC_x0 << 2) | lo_; })
#endif
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animationId);
extern u8 startJumpAnimationVar3;

SpriteActorCore* findOrDeleteStatusEffectSprite(SpriteActorCore* actor, s32 anim, s32 mode);
SpriteActorCore* findOrDeleteStatusEffectSprite(SpriteActorCore* actor, s32 anim, s32 mode) {
    TaskHeader* owner = actor->m6C_owner;
    TaskHeader* t;

    for (t = spriteCallback2Var2; t != NULL; t = t->m18_pNext) {
        if (t->m0_owner == owner && t->m14_ownerId.m0_id == owner->m10_id.m0_id && t->m14_ownerId.m29) {
            SpriteActorCore* a = (SpriteActorCore*)t->m4_data;
            if (a->m24_vramData == &createSavePointMeshData_mode5) {
                if (mode != 2) {
                    if ((s8)a->mAF_animationId != anim) {
                        continue;
                    }
                    if (mode == 0) {
                        return a;
                    }
                }
                ((TaskHeader*)a->m6C_owner)->mC_deleteCallback(a->m6C_owner);
            }
        }
    }
    return NULL;
}

SpriteActorCore* deleteStatusEffectSprite(SpriteActorCore* actor, s32 anim);
SpriteActorCore* deleteStatusEffectSprite(SpriteActorCore* actor, s32 anim) {
    return findOrDeleteStatusEffectSprite(actor, anim, 1);
}

void spawnStatusEffectSprite(SpriteActorCore* actor, s32 anim);
void spawnStatusEffectSprite(SpriteActorCore* actor, s32 anim) {
    if (actor->m48_defaultAnimationBundle != NULL && findOrDeleteStatusEffectSprite(actor, anim, 0) == NULL) {
        u16* a = (u16*)(((u16*)createSavePointMeshData_mode5.m10_startOfAnimationContainer)[anim + 1] + createSavePointMeshData_mode5.m10_startOfAnimationContainer);
        u8 saved = spriteBytecode2ExtendedE0_Var0;
        SpriteActorCore* child;
        spriteBytecode2ExtendedE0_Var0 = 0;
        child = spriteBytecode2ExtendedE0(actor, a, &createSavePointMeshData_mode5);
        child->mAF_animationId = anim;
        child->m74_targetEntitySprite = actor;
        spriteBytecode2ExtendedE0_Var0 = saved;
        child->mB0_x8 = 1;
    }
}

void updateRunningAnimations(void);
void updateRunningAnimations(void) {
    s32 i;
    s32 k;
    SpriteActorCore* a;
    u16 v;
    u16 old;
    u16 added;
    u16 removed;
    s32 anim;

    for (i = 0; i != 11; i++) {
        if (battleBssBlock.m4_visualEntities[i].m4_isGear) {
            continue;
        }
        a = battleBssBlock.m8C8C_spriteActorCores[i];
        if (a == NULL) {
            continue;
        }
        if (getEntityStatusAnimationIndex(i) == 8) {
            continue;
        }
        switch ((s8)a->mAF_animationId) {
        case 5:
        case 7:
        case 14:
        case 15:
        case 21:
            if ((s8)a->mAF_animationId != statusToAnimationTable[getEntityStatusAnimationIndex(i)]) {
                spriteActorSetPlayingAnimation(a, 0x10);
                processBattleAnimationSub0_var0 &= ~(1 << SPRITE_ENTITY(a));
            }
            break;
        }
    }
    waitForBattleAnimationsToSettle();
    for (i = 0; i != 11; i++) {
        if (battleBssBlock.m4_visualEntities[i].m4_isGear) {
            continue;
        }
        a = battleBssBlock.m8C8C_spriteActorCores[i];
        if (a == NULL) {
            continue;
        }
        if (getEntityStatusAnimationIndex(i) == 8) {
            continue;
        }
        if ((s8)a->mAF_animationId == 0x15) {
            continue;
        }
        {
            s32 d = (s16)(a->m0_position.vx >> 16) - battleBssBlock.m4_visualEntities[i].mA_X;
            s32 c = (d >= 0 ? d : battleBssBlock.m4_visualEntities[i].mA_X - (s16)(a->m0_position.vx >> 16)) < 9;
            if (c) {
                d = (s16)(a->m0_position.vz >> 16) - battleBssBlock.m4_visualEntities[i].mC_Z;
                c = (d >= 0 ? d : battleBssBlock.m4_visualEntities[i].mC_Z - (s16)(a->m0_position.vz >> 16)) < 9;
                if (c) {
                    continue;
                }
            }
        }
        a->mAC_x6 = 1;
        a->mA0_destination.vx = battleBssBlock.m4_visualEntities[i].mA_X;
        a->mA0_destination.vz = battleBssBlock.m4_visualEntities[i].mC_Z;
        a->mA0_destination.vy = 0;
        spriteActorSetPlayingAnimation(a, 3);
    }
    waitForBattleAnimationsToSettle();
    for (i = 0; i != 11; i++) {
        SpriteActorCore* c = battleBssBlock.m8C8C_spriteActorCores[i];
        if (c != NULL) {
            c->mAC_x6 = 0;
        }
    }
    for (i = 0; i != 11; i++) {
        if (battleBssBlock.m4_visualEntities[i].m4_isGear) {
            continue;
        }
        /* One-shot loop (like processBattleAnimationSub0's): its loop notes end GCC 2.6.3's
         * CSE block, which JP needs (m4_isGear gets its own pointer there). */
        for (;;) {
            a = battleBssBlock.m8C8C_spriteActorCores[i];
            break;
        }
        if (a == NULL) {
            continue;
        }
        if ((s8)a->mAF_animationId != 0x15) {
            battleAnimationCallback_5(i);
        }
        processBattleAnimationSub0();
        if (getEntityStatusAnimationIndex(i) != 8) {
            anim = statusToAnimationTable[getEntityStatusAnimationIndex(i)];
            if (battleBssBlock.m4_visualEntities[i].m4_isGear == 0) {
                if (anim == 0x15 && !((jumpAnimationActiveActorBF >> SPRITE_ENTITY(a)) & 1)) {
                    goto skip;
                }
                if (anim == 1) {
                    anim = a->mB0_x0_animationId;
                }
                if (battleBssBlock.m4_visualEntities[i].m3_hidden == 0 && (s8)a->mAF_animationId != anim) {
                    spriteActorSetPlayingAnimation(a, anim);
                }
            }
        }
    skip:
        v = getEntityStatusEffectBitmask(i);
        old = a->m7C_extra->mC_animationSignal;
        a->m7C_extra->mC_animationSignal = v;
        removed = old & ~v;
        added = v & ~old;
        for (k = 0; k != 16; k++, added >>= 1) {
            if (added & 1) {
                switch (k) {
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:
                case 10:
                case 11:
                case 12:
                    break;
                case 13:
                    spawnStatusEffectSprite(a, 9);
                    break;
                case 14:
                    spawnStatusEffectSprite(a, 10);
                    break;
                case 15:
                    spawnStatusEffectSprite(a, 11);
                    break;
                }
            }
        }
        added = removed;
        for (k = 0; k != 16; k++, added >>= 1) {
            if (added & 1) {
                switch (k) {
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:
                case 10:
                case 11:
                case 12:
                    break;
                case 13:
                    deleteStatusEffectSprite(a, 9);
                    break;
                case 14:
                    deleteStatusEffectSprite(a, 10);
                    break;
                case 15:
                    deleteStatusEffectSprite(a, 11);
                    break;
                }
            }
        }
    }
}

s32 processBattleAnimationSub0(void) {
    SpriteActorCore* list[12];
    s32 count = 0;
    u8 flag = 0;
    s32 i;
    u8 busy;

    i = countBattleActorCoresActiveForBitmask(battleBssBlock.mA38_performAttackSub3_var0 & (battleBssBlock.mA38_performAttackSub3_var0 ^ processBattleAnimationSub0_var0), list, processBattleAnimationSub0_var1);
    if (i != 0) {
        for (i = i - 1; i >= 0; i--) {
            SpriteActorCore* a = list[i];
#if VERSION_JP
            if (!((jumpAnimationActiveActorBF >> SPRITE_ENTITY(a)) & 1)) {
#else
            if ((jumpAnimationActiveActorBF >> SPRITE_ENTITY(a)) & 1) {
                list[i] = NULL;
            } else {
#endif
                a->m9E_wait = 0;
                if (battleBssBlock.m4_visualEntities[SPRITE_ENTITY(a)].m4_isGear) {
                    flag = 1;
                    battleMechas[SPRITE_ENTITY(a)]->m38_syncFlag = 1;
                    performMechaPlayAnimation(SPRITE_ENTITY(a), SPRITE_ENTITY(a), 0x15);
                } else if ((s8)a->mAF_animationId != 0x15) {
                    spriteActorSetPlayingAnimation(a, 0x15);
                }
                count++;
                processBattleAnimationSub0_var0 |= 1 << SPRITE_ENTITY(a);
            }
        }
    }
    while (1) {
        busy = 0;
        for (i = 0; i != count; i++) {
#if VERSION_JP
            if (list[i]->m48_defaultAnimationBundle && list[i]->m9E_wait) {
#else
            if (list[i] && list[i]->m48_defaultAnimationBundle && list[i]->m9E_wait) {
#endif
                busy = 1;
            }
        }
        if (!busy) {
            break;
        }
        /* retail re-tests count before each call: two one-shot loops */
        for (i = 0; i != count; i++) {
            updateBattleAnimationDataLoading();
            break;
        }
        for (i = 0; i != count; i++) {
            battleRender();
            break;
        }
    }
    if (flag) {
        clearAnimationsEndOfTurnSub();
    }
    return count;
}

void waitForBattleAnimationsToSettle(void) {
    u8 again;
    s32 i;

    do {
        again = 0;
        for (i = 0; i != 11; i++) {
            SpriteActorCore* a = battleBssBlock.m8C8C_spriteActorCores[i];
            if (a == NULL) {
                continue;
            }
            if (battleBssBlock.m4_visualEntities[i].m4_isGear) {
                if (a->m64_spriteByteCode) {
                    again = 1;
                }
                continue;
            }
            if (getEntityStatusAnimationIndex(i) == 8) {
                continue;
            }
            switch ((s8)a->mAF_animationId) {
            case 0:
            case 1:
            case 5:
            case 7:
            case 14:
            case 15:
            case 20:
            case 21:
            case 22:
            case 24:
                continue;
            }
            if (battleBssBlock.m4_visualEntities[i].m3_hidden) {
                continue;
            }
            if ((s8)a->mAF_animationId == statusToAnimationTable[getEntityStatusAnimationIndex(i)]) {
                continue;
            }
            if ((s8)a->mAF_animationId == a->mB0_x0_animationId) {
                continue;
            }
            again = 1;
        }
        if (again) {
            battleRender();
        }
    } while (again);
}

s32 computeVectorDistance(VECTOR* a, VECTOR* b) {
    VECTOR d;
    d.vx = a->vx - b->vx;
    d.vy = a->vy - b->vy;
    d.vz = a->vz - b->vz;
    Square0(&d, &d);
    return SquareRoot0(d.vy + d.vz + d.vx);
}

s32 computeSVectorDistance(SVECTOR* a, SVECTOR* b) {
    VECTOR d;
    d.vx = a->vx - b->vx;
    d.vy = a->vy - b->vy;
    d.vz = a->vz - b->vz;
    Square0(&d, &d);
    return SquareRoot0(d.vy + d.vz + d.vx);
}

s32 computerJumpDistanceToTarget(Vec2s16 a, Vec2s16 b) {
    VECTOR d;
    d.vx = a.vx - b.vx;
    d.vz = a.vy - b.vy;
    Square0(&d, &d);
    return SquareRoot0(d.vx + d.vz);
}

void computeDirectionAngles(SVECTOR* a, SVECTOR* b, SVECTOR* out) {
    MATRIX unused;
    VECTOR d;
    VECTOR sq;
    s32 len;
    d.vx = a->vx - b->vx;
    d.vy = a->vy - b->vy;
    d.vz = a->vz - b->vz;
    Square0(&d, &sq);
    len = SquareRoot0(sq.vx + sq.vz);
    out->vy = ratan2(d.vz, d.vx);
    out->vz = ratan2(d.vy, len);
    out->vx = 0;
}

#define CURVE_SEGMENT(type)                                        \
    evaluateCurvePoint(type, 0, &pts[i], &p0);                          \
    draw(&p1, &p0);                                                \
    for (j = 1; j != 8; j++) {                                     \
        if (j & 1) {                                               \
            evaluateCurvePoint(type, j, &pts[i], &p1);                  \
            draw(&p0, &p1);                                        \
        } else {                                                   \
            evaluateCurvePoint(type, j, &pts[i], &p0);                  \
            draw(&p1, &p0);                                        \
        }                                                          \
    }

void drawCurveThroughPoints(s32 n, SVECTOR* pts, void (*draw)(VECTOR*, VECTOR*));
void drawCurveThroughPoints(s32 n, SVECTOR* pts, void (*draw)(VECTOR*, VECTOR*)) {
    VECTOR p0;
    VECTOR p1;
    SVECTOR unused;
    s32 i, j;

    spriteTrailSegmentIndex = 0;
    if (n < 5) {
        if (n < 3) {
            if (n >= 2) {
                p0.vx = pts[0].vx;
                p0.vy = pts[0].vy;
                p0.vz = pts[0].vz;
                p1.vx = pts[1].vx;
                p1.vy = pts[1].vy;
                p1.vz = pts[1].vz;
                draw(&p0, &p1);
            }
            return;
        }
        switch (n) {
        case 3:
            pts[3].vx = pts[2].vx;
            pts[3].vy = pts[2].vy;
            pts[3].vz = pts[2].vz;
            pts[4].vx = pts[2].vx;
            pts[4].vy = pts[2].vy;
            pts[4].vz = pts[2].vz;
            break;
        case 4:
            pts[4].vx = pts[3].vx;
            pts[4].vy = pts[3].vy;
            pts[4].vz = pts[3].vz;
            break;
        }
        n = 5;
    }
    i = 0;
    evaluateCurvePoint(0, 0, &pts[i], &p0);
    for (j = 1; j != 8; j++) {
        if (j & 1) {
            evaluateCurvePoint(0, j, &pts[i], &p1);
            draw(&p0, &p1);
        } else {
            evaluateCurvePoint(0, j, &pts[i], &p0);
            draw(&p1, &p0);
        }
    }
    if (n >= 6) {
        i = 1;
        CURVE_SEGMENT(1);
    }
    for (i = 2; i < n - 5; i++) {
        CURVE_SEGMENT(2);
    }
    if (n >= 7) {
        i = n - 5;
        CURVE_SEGMENT(3);
    }
    i = n - 4;
    CURVE_SEGMENT(4);
    if (n >= 5) {
        p0.vx = pts[n - 1].vx;
        p0.vy = pts[n - 1].vy;
        p0.vz = pts[n - 1].vz;
        draw(&p1, &p0);
    }
}

void evaluateCurvePoint(s32 a, s32 b, SVECTOR* v, VECTOR* out) {
    VECTOR t;

    gte_lddp(pCurveSegmentWeights[a * 8 + b][0]);
    t.vx = v[0].vx;
    t.vy = v[0].vy;
    t.vz = v[0].vz;
    gte_ldlvl(&t);
    gte_gpf12();
    gte_stlvl(out);
    gte_lddp(pCurveSegmentWeights[a * 8 + b][1]);
    t.vx = v[1].vx;
    t.vy = v[1].vy;
    t.vz = v[1].vz;
    gte_ldlvl(&t);
    gte_gpf12();
    gte_stlvl(&t);
    out->vx += t.vx;
    out->vy += t.vy;
    out->vz += t.vz;
    gte_lddp(pCurveSegmentWeights[a * 8 + b][2]);
    t.vx = v[2].vx;
    t.vy = v[2].vy;
    t.vz = v[2].vz;
    gte_ldlvl(&t);
    gte_gpf12();
    gte_stlvl(&t);
    out->vx += t.vx;
    out->vy += t.vy;
    out->vz += t.vz;
    gte_lddp(pCurveSegmentWeights[a * 8 + b][3]);
    t.vx = v[3].vx;
    t.vy = v[3].vy;
    t.vz = v[3].vz;
    gte_ldlvl(&t);
    gte_gpf12();
    gte_stlvl(&t);
    out->vx += t.vx;
    out->vy += t.vy;
    out->vz += t.vz;
    out->vx >>= 2;
    out->vy >>= 2;
    out->vz >>= 2;
}

void loadWdsDataIfNeeded(void);
void loadWdsDataIfNeeded(void) {
    if (loadWdsDataNeeded) {
        unloadWds(loadWdsDataNeeded);
    }
    loadWdsDataNeeded = 0;
}

/* Loads the sound/sprite bundles of a battle data file: 'sedes' sequences,
 * 'wds ' wave banks (US waits for the transfer, polling the SN debugger) and
 * sprite bundles uploaded to VRAM (0x380,0x100). Returns the last sequence. */
void* loadEffectFragmentsAndAudio(s32* data) {
    void* seq = NULL;
    s32* p = data;
    s32 n = *p - 3;

    for (p += 4; n > 0; n--, p++) {
#if VERSION_JP
        s32* q = (s32*)((sptr)data + *p);
#else
        s32* q = (s32*)(*p + (sptr)data);
#endif
        switch (*q) {
        case 0x73646573:
            seq = q;
            loadSequence(seq);
            break;
        case 0x20736477:
            loadWdsDataIfNeeded();
            battleAnimationSoundLoaded = 0;
            startJumpAnimationVar3 = 0;
            /* the cast makes GCC address loadWdsDataNeeded before the call, so its
             * %hi is hoisted into a saved register */
            loadWdsDataNeeded = (void*)loadWds((Wds*)q, 0);
#if VERSION_JP
            waitForMusic(0x10);
#else
            while ((s16)waitForMusic(0)) {
                if (BATTLE_DTL_WORD != -1) {
                    POLLHOST();
                }
                PORT_YIELD(); /* no SDK call here: let the SPU transfer interrupts in */
            }
#endif
            if (n == 1) {
                shrinkAllocation(data, *p);
            }
            break;
        default:
            {
                Vec2s16 loc;
                Vec2s16 clut;

                loc.vx = 0x380;
                loc.vy = 0x100;
                clut.vx = 0;
                clut.vy = 0x1F4;
                initFieldEntitySub4Sub5Sub0(&createSavePointMeshData_mode6, q, loc, clut, 0);
            }
            break;
        }
    }
    return seq;
}

void clearFragmentSeq(s32* data);
void clearFragmentSeq(s32* data) {
    s32* p = data;
    s32 n = *p - 3;
    p += 4;
    for (; n > 0; n--, p++) {
        s32* q = (s32*)(*p + (sptr)data);
        if (*q == 0x73646573) {
            unloadSequence(q);
        }
    }
}
