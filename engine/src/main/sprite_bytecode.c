/*
 * executeSpriteBytecode2: the sprite animation bytecode interpreter.
 *
 * File 0x800248D4-0x80024F20 (main executable): its jump table starts a new
 * object's read-only data, and shape_transfer.c follows. Built with GCC
 * 2.7.2-cdk.
 */
#include "common.h"
#include "main/sprite.h"

/* ======== src/main/sprite_bytecode.c ======== */

extern u8 isBattleOverlayLoaded;
/* Length in bytes of each opcode 0x80-0xFF, operands included (Noah:
 * sizePerBytecodeTable, which also lists the 0x80 bytes in front of it: the
 * tail of sprite.c's LZSS image). Indexed with op - 0x80. */
u8 sizePerBytecodeTable[0x80] = {
    /* 0x80 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0x90 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 0xA0 */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    /* 0xB0 */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    /* 0xC0 */ 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
    /* 0xD0 */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    /* 0xE0 */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    /* 0xF0 */ 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
};
extern s32 spritesCallback2Delay;
/* Noah: spritesCallback2Delay */
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

void executeSpriteBytecode2(SpriteActorCore* actor) {
    s32 n = PC_SPRITE_UNSET_COUNT; /* ops 0x40-0x7F: the PS1's $s3 (main/sprite.h) */
    u8* p;
    u8 op;

    if (isBattleOverlayLoaded) {
        executeSpriteBytecode2_battle(actor);
        return;
    }
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
        actor->m64_spriteByteCode += p[0] | (s16)((s8)p[1] << 8);
        goto loop;
    case 0xFA:
        if (*executeSpriteBytecode2Sub3(actor, p) != 0) {
            actor->m64_spriteByteCode += p[1] | (s16)((s8)p[2] << 8);
            goto loop;
        }
        break;
    case 0xD4:
        actor->m64_spriteByteCode += p[0] | (s16)((s8)p[1] << 8);
#else
        actor->m64_spriteByteCode = pcU24Address(actor->m64_spriteByteCode, popU24FromAnimationStack(actor));
        goto loop;
    case 0xFA:
        if (*executeSpriteBytecode2Sub3(actor, p) != 0) {
            actor->m64_spriteByteCode += (s16)(p[1] | ((s8)p[2] << 8));
            goto loop;
        }
        break;
    case 0xD4:
        actor->m64_spriteByteCode += (s16)(p[0] | ((s8)p[1] << 8));
#endif
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
        executeSpriteBytecode2(actor);
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
        actor->m64_spriteByteCode += (s16)(p[0] | ((s8)p[1] << 8));
        goto loop;
#endif
    case 0xA7:
        actor->m64_spriteByteCode += sizePerBytecodeTable[op - 0x80];
        if (*p & 0x80) {
            spritesCallback2Delay = (*p & 0x7F) + 1;
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
    default:
        executeSpriteBytecode2Extended(actor, op, p);
        break;
    }
    actor->m64_spriteByteCode += sizePerBytecodeTable[op - 0x80];
    goto loop;
}
