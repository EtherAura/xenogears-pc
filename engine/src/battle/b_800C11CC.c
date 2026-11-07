/*
 * Battle build of the kernel sprite-bytecode VM (Noah
 * executeSpriteBytecode2_battle): src/main/sprite_bytecode.c with the same
 * case order and the battle-only opcodes inserted at the top of the switch.
 * 0x800C11CC; gcc-2.7.2-cdk, ASPSX 2.56. Its .rodata is exactly the two jump
 * tables (opcodes 0x80-0xFB, and 0xF8's inner switch).
 * 0x9A/0x9C repeat their tail (cross-jumping merges it from the
 * createRotationMatrix call on); 0xF8 case 4 needs an s16 damage type;
 * 0xF8 case 7 loads numActiveSpriteCores into cond before the call.
 */
#include "common.h"
#include "psyq.h"
#include "main/sprite.h"
#include "main/task.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "battle/battle.h"

extern u8 sizePerBytecodeTable[];
extern s32 spritesCallback2Delay;
extern s16 battleCameraVar1;
extern SVECTOR battleCameraEye;
extern SVECTOR battleCameraAt;
extern SVECTOR battleCameraSpriteOrientation;            /* Noah: battleCameraSpriteOrientation (battleCameraBlock.m14) */
extern s32 battleCameraSpriteDistance;                /* Noah: battleCameraSpriteDistance (battleCameraBlock.m1C) */
extern u8 startJumpAnimationVar1;
extern s32 allocateJumpAnimationStructVar0;
extern SpriteActorCore* processBattleAnimationSub0_var1;
extern s16 numActiveSpriteCores;
void executeSpriteBytecode2_battle(SpriteActorCore* actor);
void addToSpriteTransferList(SpriteActorCore* actor, s32 frame);
void executeSpriteBytecode2Sub1(SpriteActorCore* actor);
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animationId);
u8* executeSpriteBytecode2Sub3(SpriteActorCore* actor, u8* p);
void executeSpriteBytecode2Extended(SpriteActorCore* actor, s32 op, u8* p);
u8 popByteFromAnimationStack(SpriteActorCore* actor);
s32 popU24FromAnimationStack(SpriteActorCore* actor);
void pushByteOnAnimationStack(SpriteActorCore* actor, u8 value);
void pushU24OnAnimationStack(SpriteActorCore* actor, s32 value);
void setSVector3(SVECTOR* v, s32 x, s32 y, s32 z);
void spriteActorSetMoveSpeed(SpriteActorCore* actor, s32 speed);
void spriteActorSetDirection(SpriteActorCore* actor, s32 direction);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
s32 vec2dRatan2(Vec2s16 a, Vec2s16 b);
void clearShapeTransferEntry(void* p);
void mechaPlayAnimationPreserveState_battle(u16 index, u16 mask, s32 animationId);
void* getCustomPolySubBuffer(void* data, s32 index);
s32 getCustomPolySubBufferSize(void* buf);
void scaleCustomPolyVertices(void* buf, s32 shift);
void startBattleCameraShake(SVECTOR* target, s32 duration);
s32 getBattleFaderAbe(void);
void battleSpriteEffect(SpriteActorCore* actor, s32 id, u8* args);
void* battleSpriteOpcode_FB(SpriteActorCore* actor, s32 minDistance, u8* bytecode);
void computeSpriteJumpArc(SpriteActorCore* actor);
void initSpriteJumpToTarget(SpriteActorCore* actor);
void createAllDamageDisplays(void);
s32 findSpriteInTargetList(SpriteActorCore* actor);
void retargetToNextSprite(SpriteActorCore* actor);

/* Little-endian signed 16-bit bytecode argument at p[i] (JP builds it
 * without the outer s16 cast, as in main's sprite VM). */
#if VERSION_JP
#define S16ARG(p, i) ((p)[i] | (s16)((s8)(p)[(i) + 1] << 8))
#else
#define S16ARG(p, i) ((s16)((p)[i] | ((s8)(p)[(i) + 1] << 8)))
#endif

/* Noah executeSpriteBytecode2_battle: the kernel sprite VM (main/sprite_bytecode.c) plus battle opcodes */
void executeSpriteBytecode2_battle(SpriteActorCore* actor) {
    SVECTOR sv;
    MATRIX m;
    VECTOR out;
    Vec2s16 pos;
    Vec2s16 tpos;
    SVECTOR* rot;
    s32 n;
    u8* p;
    u8 op;

loop:
    if (actor->m9E_wait != 0) {
        return;
    }
    op = *actor->m64_spriteByteCode;
    p = actor->m64_spriteByteCode + 1;
    if (op < 0x80) {
        actor->m64_spriteByteCode = p;
        if (op < 0x10) {
            addToSpriteTransferList(actor, (u16)actor->m34_currentSpriteFrame + 1);
            n = (op & 0xF) + 1;
        } else if (op < 0x20) {
            actor->mA8_xB_frame++;
            executeSpriteBytecode2Sub1(actor);
            n = (op & 0xF) + 1;
        } else if (op < 0x30) {
            addToSpriteTransferList(actor, (u16)actor->m34_currentSpriteFrame - 1);
            n = (op & 0xF) + 1;
        }
        if (op < 0x40) {
            n = (op & 0xF) + 1;
        }
        n = (n * (s32)actor->mAC_x7_timeScale) / 256;
        if (n == 0) {
            n = 1;
        }
        actor->m9E_wait += n;
        if (++actor->mA8_x16_bytecodeStep == 0) {
            actor->mA8_x16_bytecodeStep--;
        }
        return;
    }
    switch (op) {
    case 0xE8:
        battleSpriteEffect(actor, *p, executeSpriteBytecode2Sub3(actor, p + 1));
        break;
    case 0xCA: {
        u8* q = p + ((((s8*)p)[1] << 8) | p[0]);
        s32 x;
        if (q[4] != 0) {
            x = q[4] - 1;
        } else {
            x = getBattleFaderAbe();
        }
        battleCreateFader(q[3] >> 1, x, q[0], q[1], q[2]);
        break;
    }
    case 0xCB: {
        u8* q = p + ((((s8*)p)[1] << 8) | p[0]);
        setSVector3(&sv, q[0], q[1], q[2]);
        startBattleCameraShake(&sv, q[3]);
        break;
    }
    case 0xE3: {
        SpriteActorCore* target = actor->m74_targetEntitySprite;
        u8* anim = actor->m64_spriteByteCode + S16ARG(p, 0);
        target->mAF_animationId = 0x3F;
        setCurrentAnimationPtr(target, (u16*)anim);
        break;
    }
    case 0xFB:
        battleSpriteOpcode_FB(actor, p[2] << 1, actor->m64_spriteByteCode + S16ARG(p, 0));
        break;
    case 0xC3:
    case 0xEC:
    case 0xF9:
        battleSpriteEffect(actor, *p, p + 1);
        break;
    case 0x9D:
        if (actor->m40_draw.bits.xD_mode == 10) {
            battleCameraEye.vx = actor->m0_position.vx >> 16;
            battleCameraEye.vy = actor->m0_position.vy >> 16;
            battleCameraEye.vz = actor->m0_position.vz >> 16;
        } else {
            battleCameraAt.vx = actor->m0_position.vx >> 16;
            battleCameraAt.vy = actor->m0_position.vy >> 16;
            battleCameraAt.vz = actor->m0_position.vz >> 16;
        }
        break;
    case 0x99:
        actor->m20_object->m0_rotation[0] = battleCameraSpriteOrientation.vx;
        actor->m20_object->m0_rotation[1] = battleCameraSpriteOrientation.vy;
        actor->m20_object->m0_rotation[2] = battleCameraSpriteOrientation.vz;
        break;
    case 0x9A:
        rot = (SVECTOR*)actor->m20_object->m0_rotation;
        sv.vx = battleCameraSpriteDistance;
        sv.vy = 0;
        sv.vz = 0;
        createRotationMatrix(rot, &m);
        ApplyMatrix(&m, &sv, &out);
        if (actor->m40_draw.bits.xD_mode == 10) {
            actor->m0_position.vx = previousCameraAt2.vx - (out.vx << 16);
            actor->m0_position.vy = previousCameraAt2.vy - (out.vy << 16);
            actor->m0_position.vz = previousCameraAt2.vz - (out.vz << 16);
        }
        if (actor->m40_draw.bits.xD_mode == 11) {
            actor->m0_position.vx = previousCameraEye2.vx + (out.vx << 16);
            actor->m0_position.vy = previousCameraEye2.vy + (out.vy << 16);
            actor->m0_position.vz = previousCameraEye2.vz + (out.vz << 16);
        }
        break;
    case 0x9B:
        actor->m2C_scale = R3000_DIV(battleCameraVar1 << 12, battleCameraSpriteDistance);
        break;
    case 0x9C:
        sv.vx = R3000_DIV(battleCameraVar1 << 12, actor->m2C_scale);
        sv.vy = 0;
        sv.vz = 0;
        createRotationMatrix(&battleCameraSpriteOrientation, &m);
        ApplyMatrix(&m, &sv, &out);
        if (actor->m40_draw.bits.xD_mode == 10) {
            actor->m0_position.vx = previousCameraAt2.vx - (out.vx << 16);
            actor->m0_position.vy = previousCameraAt2.vy - (out.vy << 16);
            actor->m0_position.vz = previousCameraAt2.vz - (out.vz << 16);
        }
        if (actor->m40_draw.bits.xD_mode == 11) {
            actor->m0_position.vx = previousCameraEye2.vx + (out.vx << 16);
            actor->m0_position.vy = previousCameraEye2.vy + (out.vy << 16);
            actor->m0_position.vz = previousCameraEye2.vz + (out.vz << 16);
        }
        break;
    case 0xC2: {
        s32 d;
        SpriteActorCore* target;
        actor->m0_position.vy = (actor->m84_maxY - 1) << 16;
        target = actor->m74_targetEntitySprite;
        d = ((s8)*p * actor->m2C_scale) / 4096;
        if (actor->mAC_x2_facing == 0) {
            d = -d;
        }
        actor->mA0_destination.vx = (target->m0_position.vx >> 16) + d;
        actor->mA0_destination.vz = target->m0_position.vz >> 16;
        actor->mA0_destination.vy = 0;
        initSpriteJumpToTarget(actor);
        break;
    }
    case 0x97: {
        SpriteActorCore* target;
        if ((actor->m0_position.vy >> 16) < actor->m84_maxY) {
            actor->m9E_wait = 1;
            return;
        }
        target = actor->m74_targetEntitySprite;
        spriteActorSetMoveSpeed(actor, 0);
        pos.vx = actor->m0_position.vx >> 16;
        pos.vy = actor->m0_position.vz >> 16;
        tpos.vx = target->m0_position.vx >> 16;
        tpos.vy = target->m0_position.vz >> 16;
        spriteActorSetDirection(actor, (s16)vec2dRatan2(tpos, pos));
        setSpriteActorAngle(actor, vec2dRatan2(tpos, pos));
        spriteActorSetDirection(target, (s16)vec2dRatan2(pos, tpos));
        setSpriteActorAngle(target, vec2dRatan2(pos, tpos));
        break;
    }
    case 0xA4: {
        SpriteActorCore* target = actor->m74_targetEntitySprite;
        s32 anim = (s8)*p;
        if (target->m48_defaultAnimationBundle == NULL) {
            mechaPlayAnimationPreserveState_battle(BATTLE_SLOT(target), 1 << BATTLE_SLOT(actor), anim);
        } else {
            spriteActorSetPlayingAnimation(target, anim);
        }
        break;
    }
    case 0x9E:
        actor->m9E_wait = 1;
        if (!(battleBssBlock.m8C58_pad.m0_held & 0x100)) {
            return;
        }
        break;
    case 0x95:
        actor->m9E_wait = 1;
        if (isCDBusy() != 0) {
            return;
        }
        break;
    case 0x89:
        initSpriteJumpToTarget(actor);
        break;
    case 0x88:
        computeSpriteJumpArc(actor);
        break;
    case 0x8F:
        startJumpAnimationVar1 = 1;
        actor->m64_spriteByteCode = NULL;
        actor->mA8_x1C = 0;
        return;
    case 0xF8: {
        s16 damageType = battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0 - 1].m18_damageType[BATTLE_SLOT(actor->m74_targetEntitySprite)];
        s32 cond;
        switch (p[2] & 0x7F) {
        case 8:
            cond = battleBssBlock.mA78_startPlayingAttackAnimationVar0 != 0;
            break;
        case 9:
            cond = actor->m74_targetEntitySprite == processBattleAnimationSub0_var1;
            break;
        case 0:
            cond = damageType == 0;
            break;
        case 1:
            cond = damageType == 1;
            break;
        case 2:
            cond = damageType == 2;
            break;
        case 3:
            cond = damageType == 3;
            break;
        case 4:
            switch (damageType) {
            case 4:
            case 7:
                cond = 1;
                break;
            default:
                cond = 0;
                break;
            }
            break;
        case 5:
            cond = damageType == 5;
            break;
        case 6:
            cond = damageType == 6;
            break;
        case 7:
            cond = numActiveSpriteCores;
            cond = findSpriteInTargetList(actor->m74_targetEntitySprite) + 1 < cond;
            retargetToNextSprite(actor);
            break;
        default:
            cond = 0;
            break;
        }
        if (p[2] & 0x80) {
            cond = !cond;
        }
        if (!cond) {
            break;
        }
#if VERSION_JP
        actor->m64_spriteByteCode += S16ARG(p, 0);
        goto loop;
#else
        goto jump;
#endif
    }
    case 0xF3: {
        u8* data;
        s32 size;
        u8* buf;
        s32 off;
        if (actor->m20_object->m2C_subSpriteBuffer != NULL) {
            clearShapeTransferEntry(actor->m20_object->m2C_subSpriteBuffer);
        }
        off = (((s8*)p)[2] << 16) + (p[1] << 8) + p[0];
        if (off != 0) {
            data = (u8*)(off + (sptr)p);
            size = getCustomPolySubBufferSize(getCustomPolySubBuffer(data, 0));
            buf = allocateHeap(size * 2, 0);
            if ((u16)actor->m3A_scaleModifier != 0) {
                scaleCustomPolyVertices(getCustomPolySubBuffer(data, 0), 3);
            }
            copyCustomPolySubBuffer(getCustomPolySubBuffer(data, 0), buf, actor->m3C_x5_blendMode, actor->m2B_code & 1);
            memcpy(buf + size, buf, size);
            actor->m20_object->m2C_subSpriteBuffer = (SubSprite*)buf;
            actor->m20_object->m30_subSprites = (SubSprite*)(buf + size);
            actor->m20_object->m34_perSubgroupTransform = getCustomPolySubBuffer(data, 0);
        } else {
            actor->m20_object->m2C_subSpriteBuffer = NULL;
            actor->m20_object->m34_perSubgroupTransform = NULL;
        }
        break;
    }
    case 0x8B:
        createAllDamageDisplays();
        break;
    case 0xBE: {
        s32 arg = p[0] | (((s8*)p)[1] << 8);
        s32 wait = ((arg >> 11) & 0xF) + 1;
        s32 frame = arg & 0x1FF;
        wait = (wait * (s32)actor->mAC_x7_timeScale) / 256;
        if (wait == 0) {
            wait = 1;
        }
        if (actor->m3C_x0_type != 1) {
            actor->m34_currentSpriteFrame = frame;
            actor->m9E_wait += wait;
            actor->m64_spriteByteCode += 3;
            return;
        }
        if (arg < 0 && frame != 0) {
            frame = actor->m60_endOfAnimationContainer[frame - 1];
        }
        actor->mAC_x3 = arg >> 9;
        actor->m3C_x3_flipX = actor->mAC_x3 ^ actor->mAC_x2_facing;
        if ((arg >> 10) & 1) {
            actor->m3C_x4 = 1;
        } else {
            actor->m3C_x4 = 0;
        }
        addToSpriteTransferList(actor, frame);
        actor->m9E_wait += wait;
        actor->m64_spriteByteCode += 3;
        return;
    }
    case 0x8E:
        actor->m64_spriteByteCode = NULL;
        return;
    case 0xE2: {
        s16 off = p[0] + ((s8)p[1] << 8);
        pushU24OnAnimationStack(actor, (s32)(uptr)(actor->m64_spriteByteCode + 3));
        actor->m64_spriteByteCode += off;
        goto loop;
    }
    case 0x85:
#if VERSION_JP
        actor->m64_spriteByteCode = pcU24Address(actor->m64_spriteByteCode, popU24FromAnimationStack(actor));
        goto loop;
    case 0xE1:
        actor->m64_spriteByteCode += S16ARG(p, 0);
        goto loop;
#else
        actor->m64_spriteByteCode = pcU24Address(actor->m64_spriteByteCode, popU24FromAnimationStack(actor));
        goto loop;
#endif
    case 0xFA:
        if (*executeSpriteBytecode2Sub3(actor, p) != 0) {
            actor->m64_spriteByteCode += S16ARG(p, 1);
            goto loop;
        }
        break;
    case 0xD4:
        actor->m64_spriteByteCode += S16ARG(p, 0);
        if (actor->m68 != NULL) {
            actor->m68(actor);
        }
        goto loop;
    case 0x86:
        if (actor->mC_step.vy < 0) {
            actor->m9E_wait = 1;
            return;
        }
        break;
    case 0x87:
        if ((actor->m0_position.vy >> 16) < actor->m84_maxY) {
            actor->m9E_wait = 1;
            return;
        }
        break;
    case 0x80:
    end_animation:
        actor->mA8_x1C = 0;
        if (actor->m68 != NULL) {
            actor->m68(actor);
            return;
        }
        if (actor->mB0_x0_animationId >= 0) {
            spriteActorSetPlayingAnimation(actor, actor->mB0_x0_animationId);
        }
        actor->mA8_x1C = 0;
        return;
    case 0x98: {
        SpriteActorCore* other = actor->m70_parent;
        if (other != NULL) {
            actor->m9E_wait = 1;
            if ((s8)other->mAF_animationId == (s8)actor->m8D_parentAnimationId && other->mA8_x1C == 2) {
                return;
            }
        }
        break;
    }
    case 0x82: {
        s32 vy;
        if (actor->m68 != NULL) {
            actor->m68(actor);
        }
        vy = actor->mC_step.vy;
        spriteActorSetPlayingAnimation(actor, (s8)actor->mAF_animationId);
        actor->mC_step.vy = vy;
        actor->m9E_wait = 0;
        executeSpriteBytecode2_battle(actor);
        return;
    }
    case 0x81:
        if ((s8)actor->mAF_animationId == 0x3F) {
            goto end_animation;
        }
        actor->m9E_wait = 0;
        if (actor->m68 != NULL) {
            actor->m68(actor);
        }
        actor->mA8_x1C = 1;
        return;
    case 0xE4: {
        u8 count = popByteFromAnimationStack(actor);
        if (count == 0) {
            break;
        }
        count--;
        pushByteOnAnimationStack(actor, count);
#if VERSION_JP
        actor->m64_spriteByteCode += (s16)(p[0] | ((s8)p[1] << 8));
        goto loop;
#endif
    }
#if !VERSION_JP
    case 0xE1:
    jump:
        actor->m64_spriteByteCode += S16ARG(p, 0);
        goto loop;
#endif
    case 0xA7:
        actor->m64_spriteByteCode += sizePerBytecodeTable[op - 0x80];
        if (*p & 0x80) {
            s32 d = (*p & 0x7F) + 1;
            spritesCallback2Delay = d;
            actor->m9E_wait++;
            return;
        }
        {
            s32 wait = ((*p + 2) * (s32)actor->mAC_x7_timeScale) / 256;
            if (wait == 0) {
                wait = 1;
            }
            actor->m9E_wait += wait;
        }
        return;
    case 0xC8:
        executeSpriteBytecode2Extended(actor, *p, executeSpriteBytecode2Sub3(actor, p + 1));
        break;
    case 0x9F:
        break;
    default:
        executeSpriteBytecode2Extended(actor, op, p);
        break;
    }
    actor->m64_spriteByteCode += sizePerBytecodeTable[op - 0x80];
    goto loop;
}
