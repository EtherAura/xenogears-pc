/*
 * JV3 STATUS (round 21, JP variants). US: preset (gcc-2.7.2-cdk, ASPSX 2.56).
 * JP: gcc-2.6.3-psx, ASPSX 2.34, -G0, no divide checks (2.6.0 matches every
 * function except advanceCustomPolyPacket; 2.7.2-psx/cdk do not). Check with
 * work/agents/JV3/t.sh <file> gcc-2.6.3-psx (US via --as, JP explicit flags,
 * -I work/agents/JV3/include, JP symbols from jp_extra_symbols.txt).
 * Shared body now (was a JP fallback), OK us/jp:
 *   battleSpriteEffect_6B computeSpriteSubgroupMatrix battleSpriteEffect_1_draw battleSpriteEffect_1 battleSpriteOpcode_FB_taskUpdate
 *   battleSpriteOpcode_FB getSpriteSubgroupOffset battleSpriteEffect_3_update battleSpriteEffect_3 battleSpriteEffect_20_update
 *   battleSpriteEffect_5 battleSpriteEffect_1E battleSpriteEffect_46 battleSpriteEffect_51 battleSpriteEffect_38
 *   battleSpriteEffect_2C battleSpriteEffect_26 battleSpriteEffect_1C battleSpriteEffect_1D battleSpriteEffect_18
 *   battleSpriteEffect_C battleSpriteEffect_8 battleSpriteEffect_D
 *   battleSpriteEffect_20 (new shared body: owner read before the store)
 *   battleSpriteEffect_47 (needs the JP BATTLE_SLOT in include/battle/battle.h copy)
 * OK us/jp with inner #if VERSION_JP: battleSpriteEffect_3D (JP reads tpage/clut
 *   twice: a 16-bit narrowed read and a widened local); battleSpriteEffect_5_draw,
 *   battleSpriteEffect_1E_draw (JP tests the blend bitfield in place and builds the
 *   DR_TPAGE code before bumping the packet cursor)
 *   battleSpriteEffect_1_update: new shared body (statement order: actor, args, m2E,
 *   width, blend mode), OK us/jp.
 * Every fallback in this file is done.
 * JP symbols needed: see jp_extra_symbols.txt.
 */
/*
 * Battle extension of the sprite bytecode: the opcode dispatcher
 * (battleSpriteEffect) and its handlers. 0x800B3E04-0x800B6F0C; gcc-2.7.2-cdk,
 * ASPSX 2.56.
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "stack.h"
#include "main/memory.h"
#include "main/sprite.h"
#include "battle/battle.h"

/* Bytecode argument: a signed 16-bit offset from the argument itself. */
#define ARG_PTR(p) ((p) + (((s8)(p)[1] << 8) | (p)[0]))
/* Little-endian signed 16-bit bytecode value. */
#define ARG_S16(p) (((s8)(p)[1] << 8) | (p)[0])

TaskHeader* allocateUpdateTask(TaskHeader* owner, s32 size);
TaskHeader* allocateTaskPair(s32 size, TaskHeader* owner, void (*update)(TaskHeader*), void (*draw)(TaskHeader*), void (*del)(TaskHeader*));
void spriteCallback_renderBillboardModel(TaskHeader* task);
void setupSpriteActorTransform(SpriteActorCore* actor);
s32 vec2dRatan2(Vec2s16 a, Vec2s16 b);
s32 getAngleCos(s32 angle);
s32 getAngleSin(s32 angle);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
void spriteActorSetDirection(void* actor, s32 direction);
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animationId);
s32 uploadTextureToVram(s32* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY);
void createEffectMechaLoadingFileRequests(s32);
void freeMecha_battle(s32);
void battleSpriteEffect_3A_sub(s32 target, s32 duration, s32 arg2, s16 arg3, s16 arg4, s16 arg5);
void performMechaPlayAnimation(s32, s32, s32);
void drawCurveThroughPoints(s32, SVECTOR*, void (*)(VECTOR*, VECTOR*));
s32 modulateSpeed(SpriteActorCore* actor, s32 value);
void OP_INIT_ENTITY_SCRIPT_sub0Sub9(SpriteActorCore* actor);
void setSVector3(SVECTOR* v, s32 x, s32 y, s32 z);
void createBattleStartEffectTask(void);
void pcWideShatterCapture(s32 margin);
void createFxModelShatter(void*, void*, MATRIX*, s32, s32, s32, s32, s32);
void createFxSpriteWobble(SpriteActorCore*, s32, s32, s32, s32, s32, s32);
void createFxSpriteBand(SpriteActorCore*);
void createFxTiledSprite(SpriteActorCore*);
void playFxScreenRipple(void);
void setBattleMechasVisible(s32);
void initSpriteJumpToTarget(SpriteActorCore*);
void sampleBattleTerrainHeight(SpriteActorCore*);
void createDamageDisplayIfValid(s32);
void setNeedFxFragments(SpriteActorCore*);
void retargetToNextSprite(SpriteActorCore*);
void playSoundEffectInFreeSlot(s32);
void playSoundEffectOnChannel(s32 id, s32 ch);
void stopSoundEffectById(s32 key);
void playSoundEffect(s32);
void setAnimationBundle(SpriteActorCore* actor, void* bundle);
void initPerSubgroupTransforms(SpriteActorCore* actor);
u8* executeSpriteBytecode2Sub3(SpriteActorCore* actor, u8* p);
void registerSpriteCallback2_2(TaskHeader* task);
TaskHeader* findMatchingGraphicEntity(TaskHeader* owner, void (*callback)(TaskHeader*));
TaskHeader* findTaskByCallback(void (*callback)(TaskHeader*));
extern MATRIX currentRenderingMatrix;
extern s32 gDepthDivider;   /* OT depth shift */
extern u8* shapeTransfertTableEnd;
extern u8* shapeTransfertTableCurrentEntry;
extern OTag* characterRenderingOT;
extern MATRIX battleIdentityMatrix;
extern s16 battleMechaPartyTextureLocations[6];
extern s32 runningOnDTL;               /* kernel: PC link flag */
extern s16 battleCameraVar1;
extern s32 fieldDrawEnvsInitialized;
extern u8 battleDeathBlowMusicOverride;
extern u8 D_800C3564;
extern SpriteActorCore* debugModelEditorActor;
extern u8 battleTextureReloadPending;
extern u8 startJumpAnimationVar3;
extern u16 attackInProgress;
extern u8 battleSpritesDisabled;
extern u8 battleCameraGearHeightDisabled;
extern u8 renderBattleSceneDisabled;
extern u8 savedBattleBgEnabled;
extern u8 savedBattleBgColor[3];
extern u8 drawBattleMode1Disabled;
extern s16 effectMechaEventCount;
extern u8 disableBattleFader;
extern SVECTOR battleCameraEye;
extern SVECTOR battleCameraAt;
extern u8 spriteTrailSubgroupIds[5]; /* subgroups drawn by the sprite trail */
extern RECT partyGearTextureMoveRect;
/* sword trail: the previous segment's far corners (x2, y2, x3, y3); four
 * separate halfwords (retail addresses each with its own %hi/%lo, and JP
 * spaces them 4 bytes apart) */
extern s16 spriteTrailPrevX2;
extern s16 spriteTrailPrevY2;
extern s16 spriteTrailPrevX3;
extern s16 spriteTrailPrevY3;
extern s32 spriteTrailSegmentIndex;     /* sword trail: segment counter */
extern s16 spriteTrailBlendMode;
extern SpriteActorCore* processBattleAnimationSub0_var1;
extern s16 spriteTrailWidth;
extern u8* spriteTrailFrontFlags;
extern s16 spriteTrailOtIndex;
extern s16 pendingEffectMechaEvents;
void getSpriteTrailSubgroupOffsets(SpriteActorCore* actor, SVECTOR* out);
void drawSpriteTrailSegment(VECTOR* from, VECTOR* to);
void battleSpriteEffect_1_draw(TaskHeader* task);
void battleSpriteEffect_1_update(TaskHeader* task);
s32 battleSpriteOpcode_FB_sub0(SpriteActorCore* actor);
void battleSpriteOpcode_FB_taskUpdate(TaskHeader* task);
void battleSpriteEffect_3_update(BattleSpriteAttachTask* p);
void battleSpriteEffect_20_update(TaskHeader* task);
void battleSpriteEffect_5_draw(TaskHeader* task);
void battleSpriteEffect_1E_draw(TaskHeader* task);
void battleSpriteEffect_3D(SpriteActorCore* actor);
void battleSpriteEffect_1(SpriteActorCore* actor, u8* args);
BattleSpriteAttachTask* battleSpriteEffect_3(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_20(SpriteActorCore* actor);
void battleSpriteEffect_5(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_1E(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_46(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_51(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_47(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_3A(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_38(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_2C(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_27(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_26(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_23(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_40(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_1C(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_2D(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_2E(SVECTOR* v, u8* args);
void battleSpriteEffect_5D(SVECTOR* v, u8* args);
void battleSpriteEffect_1B(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_1D(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_19(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_18(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_A(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_B(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_C(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_8(SpriteActorCore* actor, u8* args);
void battleSpriteEffect_D(SpriteActorCore* actor, s8* args);

void battleSpriteEffect_6B(void);
void battleSpriteEffect_6B(void) {
    u8* stack = allocateHeap(0x1000, 0);

    STACK_SWITCH(stack + 0xF00);
    partyGearTextureMoveRect.x = 0x280;
    partyGearTextureMoveRect.y = 0x100;
    partyGearTextureMoveRect.w = 0x40;
    partyGearTextureMoveRect.h = 0x100;
    MoveImage(&partyGearTextureMoveRect, battleMechaPartyTextureLocations[0], battleMechaPartyTextureLocations[1]);
    partyGearTextureMoveRect.x = 0x240;
    partyGearTextureMoveRect.y = 0x100;
    partyGearTextureMoveRect.w = 0x40;
    partyGearTextureMoveRect.h = 0x100;
    MoveImage(&partyGearTextureMoveRect, battleMechaPartyTextureLocations[2], battleMechaPartyTextureLocations[3]);
    partyGearTextureMoveRect.x = 0x200;
    partyGearTextureMoveRect.y = 0x100;
    partyGearTextureMoveRect.w = 0x40;
    partyGearTextureMoveRect.h = 0x100;
    MoveImage(&partyGearTextureMoveRect, battleMechaPartyTextureLocations[4], battleMechaPartyTextureLocations[5]);
    STACK_RESTORE();
    freeHeap(stack);
}

/* Battle sprite-bytecode extension: opcode `op` (1..0x6B) of `actor` with its
 * argument bytes at `args` (the battle counterpart of Noah's
 * executeSpriteBytecode2Extended). */
void battleSpriteEffect(SpriteActorCore* actor, s32 op, u8* args) {
    SVECTOR unusedRot; /* unused, keeps retail's 0x28-byte frame gap */
    MATRIX unusedMat;
    VECTOR d1;
    VECTOR d2;
    TaskHeader* task;
    sBattleVisualEntity* slot;
    SpriteActorCore* target;
    s32 x;
    s32 t;
    s32 id;

    /* Diagnostics are active only inside the dedicated effects sandbox. */
    {
        extern s32 battleFxViewCommand(SpriteActorCore*, s32, u8*);
        if (battleFxViewCommand(actor, op, args)) return;
    }
    switch (op) {
    case 0x6B:
        battleSpriteEffect_6B();
        break;
    case 0x6A:
        drawBattleMode1Disabled = 1;
        battleDeathBlowMusicOverride = 0;
        playBattleMusic(pCurrentBattleMusic, 0, 0x78);
        break;
    case 0x69:
        if (actor->mB0_xA) {
            setAnimationBundle(actor, actor->m4C_specialAnimation);
            actor->mB0_xA = 1;
        } else {
            setAnimationBundle(actor, actor->m48_defaultAnimationBundle);
            actor->mB0_xA = 0;
        }
        break;
    case 0x68:
        if ((s8)actor->mAF_animationId < 0) {
            setAnimationBundle(actor, actor->m4C_specialAnimation);
            actor->mB0_xA = 1;
        } else {
            setAnimationBundle(actor, actor->m48_defaultAnimationBundle);
            actor->mB0_xA = 0;
        }
        break;
    case 0x66:
        battleCameraGearHeightDisabled = 1;
        break;
    case 0x64:
        actor->mB0_x9 = 1;
    case 0x63:
        if (battleCameraVar1 > 0x200) {
            actor->m3A_scaleModifier = battleCameraVar1;
            actor->m40_draw.bits.x8 = 3;
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    case 0x62:
        createDamageDisplayIfValid(BATTLE_SLOT(actor));
        break;
    case 0x61:
        actor->m0_position.vy = (actor->m84_maxY - 1) << 16;
        target = actor->m74_targetEntitySprite;
        x = ((s8)args[0] * actor->m2C_scale) / 4096;
        if ((s16)(target->m0_position.vx >> 16) == battleBssBlock.m4_visualEntities[BATTLE_SLOT(target)].mA_X) {
            if (!actor->mAC_x2_facing) {
                x = -x;
            }
        } else if ((s16)(target->m0_position.vx >> 16) < battleBssBlock.m4_visualEntities[BATTLE_SLOT(target)].mA_X) {
            x = -x;
        }
        actor->mA0_destination.vx = (target->m0_position.vx >> 16) + x;
        actor->mA0_destination.vz = target->m0_position.vz >> 16;
        actor->mA0_destination.vy = 0;
        initSpriteJumpToTarget(actor);
        break;
    case 0x5F:
        D_800C3564 = 1;
        break;
    case 0x60:
        D_800C3564 = BATTLE_SLOT(actor) + 2;
        break;
    case 0x59:
        disableBattleFader = 0;
        break;
    case 0x5A:
        disableBattleFader = 1;
        break;
    case 0x57:
        setBattleMechasVisible(1);
        break;
    case 0x58:
        setBattleMechasVisible(0);
        break;
    case 0x56:
        if (startJumpAnimationVar3 != 0) {
#if VERSION_JP
            id = BATTLE_SLOT(actor);
#else
            {
                s32 lo = actor->mA8_x1E; /* low bits first, into a temporary */
                id = (actor->mAC_x0 << 2) | lo;
            }
#endif
            t = battleBssBlock.m4_visualEntities[id].m2_characterId;
            if (t == 9) {
                t = 2;
            }
            if (t == 10) {
                t = 6;
            }
            if (t == 8) {
                t = 0xF;
            }
            if (!((attackInProgress >> id) & 1)) {
                attackInProgress |= 1 << id;
                playSoundEffectInFreeSlot((t + 0x52) | (gBattleCurrentSoundEffectSeq->u14.m14_id << 16));
            }
        }
        break;
    case 0x54:
        playFxScreenRipple();
        break;
    case 0x4F:
        if (actor->m50_soundEffectSeq != 0) {
            playSoundEffect(args[0] | (*(u16*)(actor->m50_soundEffectSeq + 0x14) << 16));
        }
        break;
    case 0x52:
        if (actor->m50_soundEffectSeq != 0) {
            playSoundEffectOnChannel(args[0] | (*(u16*)(actor->m50_soundEffectSeq + 0x14) << 16), args[1]);
        }
        break;
    case 0x4D:
        if (actor->m50_soundEffectSeq != 0) {
            stopSoundEffectById(args[0] | (*(u16*)(actor->m50_soundEffectSeq + 0x14) << 16));
        }
        break;
    case 0x4E:
        if (gBattleCurrentSoundEffectSeq != 0) {
            stopSoundEffectById(args[0] | (gBattleCurrentSoundEffectSeq->u14.m14_id << 16));
        }
        break;
    case 0x4C:
        actor->m40_draw.bits.x1 = 1;
        break;
    case 0x48:
        actor->m40_draw.bits.x0 = 1;
        break;
    case 0x67:
        actor->m9E_wait++;
        d1.vx = battleCameraBlock.m4_eyeTarget.vx - battleCameraEye.vx;
        d1.vy = battleCameraBlock.m4_eyeTarget.vy - battleCameraEye.vy;
        d1.vz = battleCameraBlock.m4_eyeTarget.vz - battleCameraEye.vz;
        Square0(&d1, &d1);
        d2.vx = battleCameraBlock.mC_atTarget.vx - battleCameraAt.vx;
        d2.vy = battleCameraBlock.mC_atTarget.vy - battleCameraAt.vy;
        d2.vz = battleCameraBlock.mC_atTarget.vz - battleCameraAt.vz;
        Square0(&d2, &d2);
        if (SquareRoot0(d1.vx + d1.vy + d1.vz) < 4 && SquareRoot0(d2.vx + d2.vy + d2.vz) < 4) {
            break;
        }
        actor->m64_spriteByteCode -= 2;
        break;
    case 0x50:
        actor->m9E_wait++;
        if (pendingEffectMechaEvents != 0) {
            pendingEffectMechaEvents--;
            break;
        }
        actor->m64_spriteByteCode -= 2;
        break;
    case 0x46:
        battleSpriteEffect_46(actor, args);
        effectMechaEventCount = 0;
        break;
    case 0x51:
        battleSpriteEffect_51(actor, args);
        break;
    case 0x47:
        battleSpriteEffect_47(actor, args);
        break;
    case 0x44:
        if (BATTLE_DTL_WORD != -1) {
            debugModelEditorActor = actor;
        }
        break;
    case 0x45:
        if (BATTLE_DTL_WORD != -1) {
            debugModelEditorActor = 0;
        }
        break;
    case 0x43:
        actor->m3C_x1F = 1;
        break;
    case 0x3E:
        retargetToNextSprite(actor);
        break;
    case 0x3D:
        battleSpriteEffect_3D(actor);
        break;
    case 0x3A:
        battleSpriteEffect_3A(actor, args);
        break;
    case 0x42:
        actor->mAC_x5 = 0;
        break;
    case 0x38:
        battleSpriteEffect_38(actor, args);
        break;
    case 0x37:
        battleTextureReloadPending = 1;
        break;
    case 0x34:
        actor->m1C_gravity = ((((s8)args[0] << 1) * actor->m82_entityScale) / 4096) << 5;
        actor->m1C_gravity *= (fieldDrawEnvsInitialized + 1) * (fieldDrawEnvsInitialized + 1);
        break;
    case 0x33:
        if (actor->m48_defaultAnimationBundle != 0) {
            spriteActorSetPlayingAnimation(actor, args[0]);
        }
        break;
    case 0x31:
        SetGeomOffset(0xA0, 0x70);
        break;
    case 0x32:
        SetGeomOffset(0xA0, 0xA4);
        break;
    case 0x30:
        if (actor->m20_object != 0 && actor->m3C_x0_type == 1 && actor->m20_object->m34_perSubgroupTransform != 0) {
            actor->m20_object->m3D_anchorY = actor->m20_object->m34_perSubgroupTransform[args[0]].m1_translateY;
            actor->m20_object->m3C_anchorX = actor->m20_object->m34_perSubgroupTransform[args[0]].m0_translateX;
        }
        break;
    case 0x2F:
        setNeedFxFragments(actor);
        break;
    case 0x2C:
        battleSpriteEffect_2C(actor, args);
        break;
    case 0x27:
        battleSpriteEffect_27(actor, args);
        break;
    case 0x26:
        battleSpriteEffect_26(actor, args);
        break;
    case 0x28:
        actor->mC_step.vx = ((((s8)args[0] << 4) * actor->m82_entityScale) / 4096) << 8;
        break;
    case 0x29:
        actor->mC_step.vx += ((((s8)args[0] << 4) * actor->m82_entityScale) / 4096) << 8;
        break;
    case 0x21:
        actor->mC_step.vz = ((((s8)args[0] << 4) * actor->m82_entityScale) / 4096) << 12;
        break;
    case 0x22:
        actor->mC_step.vz += ((((s8)args[0] << 4) * actor->m82_entityScale) / 4096) << 8;
        break;
    case 0x2A:
        actor->mC_step.vy = ((((s8)args[0] << 4) * actor->m82_entityScale) / 4096) << 12;
        break;
    case 0x2B:
        actor->mC_step.vy += ((((s8)args[0] << 4) * actor->m82_entityScale) / 4096) << 8;
        break;
    case 0x1F:
        actor->m3C_x1A = 0;
        sampleBattleTerrainHeight(actor);
        break;
    case 0x3F:
        actor->m3C_x1A = 1;
        break;
    case 0x3B:
        actor->m3C_x1D = 1;
        break;
    case 0x3C:
        actor->m3C_x1D = 0;
        break;
    case 0x35:
        actor->m3C_x1B = 1;
        break;
    case 0x36:
        actor->m3C_x1B = 0;
        break;
    case 0x23:
        battleSpriteEffect_23(actor, args);
        break;
    case 0x40:
        battleSpriteEffect_40(actor, args);
        break;
    case 0x1C:
        battleSpriteEffect_1C(actor, args);
        break;
    case 0x1B:
        battleSpriteEffect_1B(actor, args);
        break;
    case 0x1A:
        registerSpriteCallback2_2((TaskHeader*)actor->m6C_owner);
        break;
    case 0x2D:
        battleSpriteEffect_2D(actor, args);
        break;
    case 0x2E:
        battleSpriteEffect_2E(&actor->mA0_destination, args);
        break;
    case 0x5B:
        battleSpriteEffect_2E(&((ModelObject*)actor->m20_object)->m44_lightRotation, args);
        break;
    case 0x5C:
        battleSpriteEffect_2E((SVECTOR*)((ModelObject*)actor->m20_object)->m4C_lightColor, args);
        break;
    case 0x5D:
        battleSpriteEffect_5D(&((ModelObject*)actor->m20_object)->m44_lightRotation, args);
        break;
    case 0x5E:
        battleSpriteEffect_5D((SVECTOR*)((ModelObject*)actor->m20_object)->m4C_lightColor, args);
        break;
    case 0x1D:
        battleSpriteEffect_1D(actor, args);
        break;
    case 0x19:
        battleSpriteEffect_19(actor, args);
        break;
    case 0x18:
        battleSpriteEffect_18(actor, args);
        break;
    case 0x17:
        {
            s32 k = (s8)args[0] << 2;
            actor->mC_step.vx = (actor->mC_step.vx * k) / 256;
            actor->mC_step.vy = (actor->mC_step.vy * k) / 256;
            actor->mC_step.vz = (actor->mC_step.vz * k) / 256;
        }
        break;
    case 0x16:
        actor->m3C_x19 = 1;
        break;
    case 0x15:
        actor->m3C_x18 = 1;
        break;
    case 0x11:
        battleSpritesDisabled = 1;
        break;
    case 0x12:
        {
            u8 a;
            u8 b;
            u8 c;
            u8 d;

            renderBattleSceneDisabled = 1;
            a = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.isbg;
            b = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.r0;
            c = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.g0;
            d = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.b0;
            battleBssBlock.mB70_renderStructs[1].m0_drawEnv.isbg = 1;
            battleBssBlock.mB70_renderStructs[0].m0_drawEnv.isbg = 1;
            battleBssBlock.mB70_renderStructs[0].m0_drawEnv.r0 = 0;
            battleBssBlock.mB70_renderStructs[1].m0_drawEnv.r0 = 0;
            battleBssBlock.mB70_renderStructs[0].m0_drawEnv.g0 = 0;
            battleBssBlock.mB70_renderStructs[1].m0_drawEnv.g0 = 0;
            battleBssBlock.mB70_renderStructs[0].m0_drawEnv.b0 = 0;
            battleBssBlock.mB70_renderStructs[1].m0_drawEnv.b0 = 0;
            savedBattleBgEnabled = a;
            savedBattleBgColor[0] = b;
            savedBattleBgColor[1] = c;
            savedBattleBgColor[2] = d;
        }
        break;
    case 0x13:
        battleSpritesDisabled = 0;
        break;
    case 0x14:
        renderBattleSceneDisabled = 0;
        battleBssBlock.mB70_renderStructs[0].m0_drawEnv.isbg = battleBssBlock.mB70_renderStructs[1].m0_drawEnv.isbg = savedBattleBgEnabled;
        battleBssBlock.mB70_renderStructs[1].m0_drawEnv.r0 = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.r0 = savedBattleBgColor[0];
        battleBssBlock.mB70_renderStructs[1].m0_drawEnv.g0 = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.g0 = savedBattleBgColor[1];
        battleBssBlock.mB70_renderStructs[1].m0_drawEnv.b0 = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.b0 = savedBattleBgColor[2];
        break;
    case 0x10:
        actor->m0_position.vx = battleCameraAt.vx << 16;
        actor->m0_position.vy = battleCameraAt.vy << 16;
        actor->m0_position.vz = battleCameraAt.vz << 16;
        break;
    case 0x53:
        actor->mAC_x3 = 0;
        actor->mAC_x2_facing = 0;
        actor->m3C_x3_flipX = 0;
        actor->m3C_x4 = 0;
        break;
    case 0x0F:
        actor->mAC_x3 = 0;
        actor->mAC_x2_facing = 0;
        actor->m3C_x3_flipX = 0;
        actor->m3C_x4 = 0;
        spriteActorSetDirection(actor, 0);
        break;
    case 0x0E:
        spriteActorSetDirection(actor, (s8)args[0] << 4);
        break;
    case 0x0D:
        battleSpriteEffect_D(actor, (s8*)args);
        break;
    case 0x0A:
        battleSpriteEffect_A(actor, args);
        break;
    case 0x0B:
        battleSpriteEffect_B(actor, args);
        break;
    case 0x49:
        if (actor->m20_object != 0) {
            actor->m20_object->m0_rotation[0] += (s8)args[0];
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    case 0x4A:
        if (actor->m20_object != 0) {
            actor->m20_object->m0_rotation[1] += (s8)args[0];
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    case 0x4B:
        if (actor->m20_object != 0) {
            actor->m20_object->m0_rotation[2] += (s8)args[0];
            actor->m3C_x1C_matrixDirty = 1;
        }
        break;
    case 0x0C:
        battleSpriteEffect_C(actor, args);
        break;
    case 0x09:
        actor->m32_direction += 0x800;
        actor->mC_step.vx = -actor->mC_step.vx;
        actor->mC_step.vy = -actor->mC_step.vy;
        actor->mC_step.vz = -actor->mC_step.vz;
        break;
    case 0x01:
        initPerSubgroupTransforms(actor);
        battleSpriteEffect_1(actor, executeSpriteBytecode2Sub3(actor, args));
        break;
    case 0x24:
        createFxTiledSprite(actor);
        break;
    case 0x25:
        createFxSpriteBand(actor);
        break;
    case 0x02:
        task = findMatchingGraphicEntity((TaskHeader*)actor->m6C_owner, battleSpriteEffect_1_update);
        if (task != 0) {
            task->mC_deleteCallback(task);
        }
        break;
    case 0x03:
        battleSpriteEffect_3(actor, args);
        break;
    case 0x04:
        while ((task = findTaskByCallback((void (*)(TaskHeader*))battleSpriteEffect_3_update)) != 0) {
            task->mC_deleteCallback(task);
        }
        break;
    case 0x1E:
        battleSpriteEffect_1E(actor, args);
        break;
    case 0x20:
        battleSpriteEffect_20(actor);
        break;
    case 0x05:
        battleSpriteEffect_5(actor, args);
        break;
    case 0x06:
        setCameraVisibleEntities(1 << BATTLE_SLOT(processBattleAnimationSub0_var1));
        break;
    case 0x55:
        setCameraVisibleEntities((1 << BATTLE_SLOT(processBattleAnimationSub0_var1)) | currentJumpAnimationBitMask);
        break;
    case 0x07:
        setCameraVisibleEntities(1 << BATTLE_SLOT(actor->m74_targetEntitySprite));
        break;
    case 0x65:
        setCameraVisibleEntities(currentJumpAnimationBitMask);
        break;
    case 0x41:
        {
            SpriteActorCore** pp = &processBattleAnimationSub0_var1;
            setCameraVisibleEntities((1 << BATTLE_SLOT(actor->m74_targetEntitySprite)) | (1 << BATTLE_SLOT(*pp)));
        }

        break;
    case 0x08:
        actor->m40_draw.bits.x13 = 1;
        battleSpriteEffect_8(actor, args);
        break;
    }
}

void battleSpriteEffect_3D(SpriteActorCore* actor) {
    RECT rect;
    SubSprite* sub = actor->m20_object->m30_subSprites;
#if VERSION_JP
    /* JP reads each field twice: once narrowed to 16 bits, once widened */
    s32 tpage = sub->mA_tpage;
    u32 clut;
    s16 x;

    rect.x = (sub->m4_texcoordX >> 2) + ((sub->mA_tpage & 0xF) << 6);
#else
    u16 tpage = sub->mA_tpage;
    u16 clut;

    rect.x = (sub->m4_texcoordX >> 2) + ((tpage & 0xF) << 6);
#endif
    rect.y = sub->m5_texcoordY + ((tpage << 4) & 0x100);
    rect.w = 8;
    rect.h = 8;
    MoveImage(&rect, 0x3F0, 0x1F0);
#if VERSION_JP
    x = sub->mC_clut & 0x3F;
    clut = sub->mC_clut;
    rect.x = x;
#else
    clut = sub->mC_clut;
    rect.x = clut & 0x3F;
#endif
    rect.y = (clut >> 6) & 0x1FF;
    rect.w = 0x10;
    rect.h = 1;
    MoveImage(&rect, 0x3F0, 0x1EE);
}

void computeSpriteSubgroupMatrix(SpriteActorCore* actor, s32 subgroup, MATRIX* m);
void computeSpriteSubgroupMatrix(SpriteActorCore* actor, s32 subgroup, MATRIX* m) {
    SVECTOR rot;
    s32 x;
    s32 y;

    if (actor->m20_object != 0) {
        y = actor->m20_object->m34_perSubgroupTransform[subgroup].m1_translateY;
        x = actor->m20_object->m34_perSubgroupTransform[subgroup].m0_translateX;
        if (actor->mAC_x2_facing) {
            x = -x;
        }
        y = (y * actor->m2C_scale) / 4096;
        x = (x * actor->m2C_scale) / 4096;
        rot.vx = actor->m20_object->m34_perSubgroupTransform[subgroup].m2_rotateX;
        rot.vy = actor->m20_object->m34_perSubgroupTransform[subgroup].m4_rotateY;
        rot.vz = actor->m20_object->m34_perSubgroupTransform[subgroup].m6_rotateZ;
        createRotationMatrix(&rot, m);
        m->t[0] = actor->m20_object->mC_matrix.t[0] + x;
        m->t[1] = actor->m20_object->mC_matrix.t[1] + y;
        m->t[2] = actor->m20_object->mC_matrix.t[2];
        SetMulMatrix(m, &actor->m20_object->mC_matrix);
    }
}

/* Scaled 2D positions of the 5 trail subgroups (spriteTrailSubgroupIds) of a sprite actor,
 * mirrored when it faces the other way; z = 0. */
void getSpriteTrailSubgroupOffsets(SpriteActorCore* actor, SVECTOR* out) {
    SubgroupTransform* t;
    s32 i;
    s32 x;
    s32 y;

    if (actor->m3C_x0_type == 1 && actor->m20_object != 0 && actor->m20_object->m34_perSubgroupTransform != 0) {
        i = 0;
        do {
            /* index first: retail adds (index * 8) + base */
            t = (SubgroupTransform*)((spriteTrailSubgroupIds[i] << 3) + (sptr)actor->m20_object->m34_perSubgroupTransform);
            x = t->m0_translateX;
            y = t->m1_translateY;
            if (actor->mAC_x2_facing) {
                x = -x;
            }
            x = (x * actor->m2C_scale) / 0x2000;
            y = (y * actor->m2C_scale) / 0x2000;
            i++;
            out->vx = x;
            out->vy = y;
            out->vz = 0;
            out++;
        } while (i != 5);
    }
}

/* Sword-trail segment from `from` to `to`: a flat line plus a textured quad
 * whose far corners are kept (spriteTrailPrevX2..spriteTrailPrevY3) for the next segment. */
void drawSpriteTrailSegment(VECTOR* from, VECTOR* to) {
    SVECTOR a;
    SVECTOR b;
    s32 tmp;
    s32 flag;
    u8* prim;
    u8* prim2;
    LINE_F2* line;
    POLY_FT4* poly;
    s32 blend;
    s32 z;
    s32 angle;
    s32 width;
    s32 dx;
    s32 dy;

    if (from->vx != to->vx || from->vy != to->vy || from->vz != to->vz) {
        a.vx = from->vx;
        a.vy = from->vy;
        a.vz = from->vz;
        b.vx = to->vx;
        b.vy = to->vy;
        b.vz = to->vz;
        prim = shapeTransfertTableCurrentEntry;
        /* sizeof: the PC's primitives are larger (8-byte tag), and the quad
         * after the line would overwrite its second point */
        if (prim + sizeof(LINE_F2) < shapeTransfertTableEnd) {
            shapeTransfertTableCurrentEntry = prim + sizeof(LINE_F2);
            line = (LINE_F2*)prim;
            SetLineF2(line);
            blend = spriteTrailBlendMode;
            if (blend) {
                blend -= 1;
                SetSemiTrans(line, 1);
            } else {
                SetSemiTrans(line, 0);
            }
            line->r0 = 0xF0;
            line->g0 = 0xF0;
            line->b0 = 0xF0;
            /* A trail can draw before any trail's update has set the flags
             * (a battle world's first trail: the extras' battle preview):
             * the PS1 reads low RAM's garbage through NULL; the PC draws
             * that trail behind the actor. */
            if (spriteTrailFrontFlags != NULL && spriteTrailFrontFlags[spriteTrailSegmentIndex / 8] != 0) {
                z = 2;
            } else {
                z = -2;
            }
            z += spriteTrailOtIndex;
            RotTransPers3(&a, &b, &a, (s32*)&line->x0, (s32*)&line->x1, &tmp, &tmp, &flag);
            if ((u32)(z - 1) < 0xFFF) {
                width = spriteTrailWidth;
                angle = line->x1 - line->x0;
                angle = ratan2(line->y1 - line->y0, angle) + 0x400;
                dx = getAngleSin(angle);
                dx = dx * width / 0x2000;
                dy = getAngleCos(angle);
                dy = dy * width / 0x2000;
                prim2 = shapeTransfertTableCurrentEntry;
                if (prim2 + sizeof(POLY_FT4) < shapeTransfertTableEnd) {
                    shapeTransfertTableCurrentEntry = prim2 + sizeof(POLY_FT4);
                    poly = (POLY_FT4*)prim2;
                    SetPolyFT4(poly);
                    SetShadeTex(poly, 1);
                    poly->u0 = (spriteTrailSegmentIndex & 7) - 0x40;
                    poly->v0 = 0xF0;
                    poly->u1 = (spriteTrailSegmentIndex & 7) - 0x3F;
                    poly->v1 = 0xF0;
                    poly->u2 = (spriteTrailSegmentIndex & 7) - 0x40;
                    poly->v2 = 0xF7;
                    poly->u3 = (spriteTrailSegmentIndex & 7) - 0x3F;
                    poly->v3 = 0xF7;
                    poly->tpage = GetTPage(0, blend, 0x3F0, 0x1F0);
                    poly->clut = GetClut(0x3F0, 0x1EE);
                    poly->r0 = 0xF0;
                    poly->g0 = 0xF0;
                    poly->b0 = 0xF0;
                    if (spriteTrailSegmentIndex == 0) {
                        poly->x0 = line->x0 - dx;
                        poly->y0 = line->y0 - dy;
                        poly->x1 = line->x0 + dx;
                        poly->y1 = line->y0 + dy;
                    } else {
                        poly->x0 = spriteTrailPrevX2;
                        poly->y0 = spriteTrailPrevY2;
                        poly->x1 = spriteTrailPrevX3;
                        poly->y1 = spriteTrailPrevY3;
                    }
                    spriteTrailPrevX2 = poly->x2 = line->x1 - dx;
                    spriteTrailPrevY2 = poly->y2 = line->y1 - dy;
                    spriteTrailPrevX3 = poly->x3 = line->x1 + dx;
                    spriteTrailPrevY3 = poly->y3 = line->y1 + dy;
                    AddPrim(&characterRenderingOT[z], poly);
                    spriteTrailSegmentIndex++;
                }
            }
        }
    }
}

void battleSpriteEffect_1_update(TaskHeader* task) {
    BattleSpriteTrail* p = (BattleSpriteTrail*)task->m4_data;
    SpriteActorCore* actor;
    s32 i;
    u16 frame;

    actor = p->m38_actor;
    spriteTrailFrontFlags = p->m4C_args;
    spriteTrailOtIndex = actor->m2E_otIndex;
    spriteTrailWidth = p->m48_width;
    spriteTrailBlendMode = p->m44_blendMode;
    frame = actor->m34_currentSpriteFrame;
    if (frame != p->m3C_frame) {
        p->m3C_frame = frame;
        getSpriteTrailSubgroupOffsets(actor, p->m50_target);
    }
    for (i = 1; i != 5; i++) {
        p->m78_current[i].vx += (p->m50_target[i].vx - p->m78_current[i].vx) / 2;
        p->m78_current[i].vy += (p->m50_target[i].vy - p->m78_current[i].vy) / 2;
        p->m78_current[i].vz += (p->m50_target[i].vz - p->m78_current[i].vz) / 2;
    }
    p->m78_current[0].vx = p->m50_target[0].vx;
    p->m78_current[0].vy = p->m50_target[0].vy;
    p->m78_current[0].vz = p->m50_target[0].vz;
    if (p->m40_animationId != (s8)p->m38_actor->mAF_animationId || p->m38_actor->mA8_x1C == 0 ||
        p->m38_actor->mA8_x1C == 1) {
        p->m0_updateTask.mC_deleteCallback(&p->m0_updateTask);
    }
}

void battleSpriteEffect_1_draw(TaskHeader* task) {
    BattleSpriteTrail* p = (BattleSpriteTrail*)task->m4_data;

    setupSpriteActorTransform(p->m38_actor);
    drawCurveThroughPoints(5, p->m78_current, drawSpriteTrailSegment);
}

void battleSpriteEffect_1(SpriteActorCore* actor, u8* args) {
    BattleSpriteTrail* p;

    p = (BattleSpriteTrail*)allocateTaskPair((s32)sizeof(BattleSpriteTrail), actor->m6C_owner, battleSpriteEffect_1_update, battleSpriteEffect_1_draw, 0);
    p->m38_actor = actor;
    p->m3C_frame = (u16)actor->m34_currentSpriteFrame;
    p->m40_animationId = (s8)actor->mAF_animationId;
    p->m4C_args = args;
    p->m44_blendMode = actor->m3C_x5_blendMode;
    if (args[0] != 0) {
        p->m48_width = args[0];
    } else {
        p->m48_width = 4;
    }
    getSpriteTrailSubgroupOffsets(actor, p->m78_current);
    getSpriteTrailSubgroupOffsets(actor, p->m50_target);
}

s32 battleSpriteOpcode_FB_sub0(SpriteActorCore* actor) {
    VECTOR d;
    VECTOR sq;

    d.vx = actor->mA0_destination.vx - (s16)(actor->m0_position.vx >> 16);
    d.vy = actor->mA0_destination.vy - (s16)(actor->m0_position.vy >> 16);
    d.vz = actor->mA0_destination.vz - (s16)(actor->m0_position.vz >> 16);
    Square0(&d, &sq);
    return SquareRoot0(sq.vx + sq.vz + sq.vy);
}

void battleSpriteOpcode_FB_taskUpdate(TaskHeader* task) {
    BattleSpriteDistanceTask* p = (BattleSpriteDistanceTask*)task;
    SpriteActorCore* actor = p->m1C_actor;
    s32 old = p->m28_distance;
    s32 dist;
    u8 done = 0;

    dist = battleSpriteOpcode_FB_sub0(actor);
    p->m28_distance = dist;
    if (old < dist || dist < p->m2C_minDistance) {
        done = 1;
        actor->m64_spriteByteCode = p->m30_bytecode;
        actor->m9E_wait = 1;
    }
    if (actor->m9E_wait == 0) {
        done = 1;
    }
    if ((s8)actor->mAF_animationId != p->m24_animationId) {
        done = 1;
    }
    if (done) {
        p->m0_task.mC_deleteCallback(&p->m0_task);
    }
}

BattleSpriteDistanceTask* battleSpriteOpcode_FB(SpriteActorCore* actor, s32 minDistance, u8* bytecode);
BattleSpriteDistanceTask* battleSpriteOpcode_FB(SpriteActorCore* actor, s32 minDistance, u8* bytecode) {
    BattleSpriteDistanceTask* p = (BattleSpriteDistanceTask*)allocateUpdateTask(actor->m6C_owner, (s32)sizeof(BattleSpriteDistanceTask) - (s32)sizeof(TaskHeader));

    setTaskUpdateFunction(&p->m0_task, battleSpriteOpcode_FB_taskUpdate);
    p->m1C_actor = actor;
    p->m28_distance = battleSpriteOpcode_FB_sub0(actor);
    p->m2C_minDistance = minDistance;
    p->m30_bytecode = bytecode;
    p->m24_animationId = (s8)actor->mAF_animationId;
    actor->mAC_x5 = 1;
    return p;
}

Vec2s16 getSpriteSubgroupOffset(SpriteActorCore* actor, s32 subgroup);
Vec2s16 getSpriteSubgroupOffset(SpriteActorCore* actor, s32 subgroup) {
    Vec2s16 v;

    if (actor->m20_object != 0 && actor->m3C_x0_type == 1 && actor->m20_object->m34_perSubgroupTransform != 0) {
        v.vy = actor->m20_object->m34_perSubgroupTransform[subgroup].m1_translateY;
        v.vx = actor->m20_object->m34_perSubgroupTransform[subgroup].m0_translateX;
        if (actor->mAC_x2_facing) {
            v.vx = -v.vx;
        }
        v.vy = (v.vy * actor->m2C_scale) / 4096;
        v.vx = (v.vx * actor->m2C_scale) / 4096;
        return v;
    }
    /* No per-subgroup transform: the PS1 leaves the caller's buffer as it
     * was, stack garbage (in battleSpriteEffect_3_update's second call, the
     * first call's result). The PC gives no offset. */
    FALLOFF_RETURN(((Vec2s16){ 0, 0 }));
}

Vec2s16 getSpriteSubgroupPosition(SpriteActorCore* actor, s32 subgroup) {
    Vec2s16 v = getSpriteSubgroupOffset(actor, subgroup);

    v.vx += (s16)(actor->m0_position.vx >> 16);
    v.vy += (s16)(actor->m0_position.vy >> 16);
    return v;
}

void battleSpriteEffect_3_update(BattleSpriteAttachTask* p) {
    SpriteActorCore* target = p->m1C_actor->m74_targetEntitySprite;
    Vec2s16 a;
    Vec2s16 b;
    Vec2s16 d;
    SpriteActorCore* actor;

    a = getSpriteSubgroupPosition(p->m1C_actor, p->m24_actorSubgroup);
    b = getSpriteSubgroupPosition(target, p->m28_targetSubgroup);
    d.vx = a.vx - b.vx;
    d.vy = a.vy - b.vy;
    target->m0_position.vx += d.vx << 16;
    target->m0_position.vy += d.vy << 16;
    actor = p->m1C_actor;
    if (p->m30_animationId != (s8)actor->mAF_animationId || actor->mA8_x1C == 0 || actor->mA8_x1C == 1) {
        p->m0_task.mC_deleteCallback(&p->m0_task);
    }
}

BattleSpriteAttachTask* battleSpriteEffect_3(SpriteActorCore* actor, u8* args) {
    BattleSpriteAttachTask* p = (BattleSpriteAttachTask*)allocateUpdateTask(actor->m6C_owner, (s32)sizeof(BattleSpriteAttachTask) - (s32)sizeof(TaskHeader));

    setTaskUpdateFunction(&p->m0_task, (void (*)(TaskHeader*))battleSpriteEffect_3_update);
    p->m1C_actor = actor;
    p->m20_target = actor->m74_targetEntitySprite;
    p->m2C_frame = (u16)actor->m34_currentSpriteFrame;
    p->m30_animationId = (s8)actor->mAF_animationId;
    p->m24_actorSubgroup = args[0] & 0xF;
    p->m28_targetSubgroup = args[0] >> 4;
    battleSpriteEffect_3_update(p);
    return p;
}

void battleSpriteEffect_20_update(TaskHeader* task) {
    SpriteActorCore* actor = (SpriteActorCore*)task->m4_data;
    MATRIX m;
    SVECTOR v;
    SVECTOR rot;
    VECTOR out;

    OP_INIT_ENTITY_SCRIPT_sub0Sub9(actor);
    rot.vy = actor->mC_step.vy >> 13;
    rot.vz = actor->mC_step.vz >> 13;
    rot.vx = 0;
    v.vx = modulateSpeed(actor, actor->mC_step.vx >> 13);
    v.vy = 0;
    v.vz = 0;
    createRotationMatrix(&rot, &m);
    ApplyMatrix(&m, &v, &out);
    out.vx += actor->mA0_destination.vx;
    out.vy += actor->mA0_destination.vy;
    out.vz += actor->mA0_destination.vz;
    actor->m0_position.vx = out.vx << 16;
    actor->m0_position.vy = out.vy << 16;
    actor->m0_position.vz = out.vz << 16;
    if (actor->m64_spriteByteCode == 0) {
        task->mC_deleteCallback(task);
    }
}

void battleSpriteEffect_20(SpriteActorCore* actor) {
    void* owner = actor->m6C_owner;

    actor->m34_currentSpriteFrame = 1;
    setTaskUpdateFunction(owner, battleSpriteEffect_20_update);
}

void battleSpriteEffect_5_draw(TaskHeader* task) {
    SpriteActorCore* actor = (SpriteActorCore*)task->m4_data;
    SVECTOR v;
    s32 tmp;
    u8* prim;
    LINE_F2* line;
    DR_TPAGE* tpage;
    s32 z;
    s32 shift;
#if !VERSION_JP
    u8 blend;
#endif

    if ((u16)actor->m34_currentSpriteFrame == 0) {
        prim = shapeTransfertTableCurrentEntry;
        /* sizeof: on the PC the DR_TPAGE after the line would overwrite its
         * second point (long streaks across the screen) */
        if (prim + sizeof(LINE_F2) < shapeTransfertTableEnd) {
            v.vx = actor->m0_position.vx >> 16;
            v.vy = actor->m0_position.vy >> 16;
            v.vz = actor->m0_position.vz >> 16;
            shapeTransfertTableCurrentEntry = prim + sizeof(LINE_F2);
            line = (LINE_F2*)prim;
            if (actor->m3C_x18) {
                SetRotMatrix(&battleIdentityMatrix);
                SetTransMatrix(&battleIdentityMatrix);
            } else {
                SetRotMatrix(&currentRenderingMatrix);
                SetTransMatrix(&currentRenderingMatrix);
            }
            z = RotTransPers(&v, (s32*)&line->x0, &tmp, &tmp) >> gDepthDivider;
            shift = (u16)actor->m36_height + 8;
            actor->m2E_otIndex = z;
            v.vx -= actor->mC_step.vx >> shift;
            v.vy -= actor->mC_step.vy >> shift;
            v.vz -= actor->mC_step.vz >> shift;
            RotTransPers(&v, (s32*)&line->x1, &tmp, &tmp);
            setlen(line, 3);
            *(u32*)&line->r0 = *(u32*)&actor->m28_r;
            AddPrim(&characterRenderingOT[z], line);
            tpage = (DR_TPAGE*)shapeTransfertTableCurrentEntry;
            if ((u8*)(tpage + 1) < shapeTransfertTableEnd) {
#if VERSION_JP
                if (actor->m3C_x5_blendMode != 0) {
                    tpage->code[0] = (((actor->m3C_x5_blendMode - 1) & 3) << 5) | 0xE1000000;
                    shapeTransfertTableCurrentEntry = (u8*)(tpage + 1);
                    setlen(tpage, 1);
                    AddPrim(&characterRenderingOT[z], tpage);
                }
#else
                blend = actor->m3C_x5_blendMode;
                if (blend != 0) {
                    shapeTransfertTableCurrentEntry = (u8*)(tpage + 1);
                    setlen(tpage, 1);
                    tpage->code[0] = (((blend - 1) & 3) << 5) | 0xE1000000;
                    AddPrim(&characterRenderingOT[z], tpage);
                }
#endif
            }
        }
    }
}

void battleSpriteEffect_5(SpriteActorCore* actor, u8* args) {
    actor->m34_currentSpriteFrame = 1;
    setTaskDrawFunction(&((SpriteTask*)actor->m6C_owner)->m1C_drawTask, battleSpriteEffect_5_draw);
    actor->m2B_code = 0x40;
}

void battleSpriteEffect_1E_draw(TaskHeader* task) {
    SpriteActorCore* actor = (SpriteActorCore*)task->m4_data;
    SpriteActorCore* target;
    SVECTOR v;
    s32 tmp;
    LINE_F2* line;
    DR_TPAGE* tpage;
    s32 z;
#if !VERSION_JP
    u8 blend;
#endif

    if ((u16)actor->m34_currentSpriteFrame == 0) {
        line = (LINE_F2*)shapeTransfertTableCurrentEntry;
        target = actor->m70_parent;
        if ((u8*)(line + 1) < shapeTransfertTableEnd) {
            v.vx = actor->m0_position.vx >> 16;
            v.vy = actor->m0_position.vy >> 16;
            v.vz = actor->m0_position.vz >> 16;
            shapeTransfertTableCurrentEntry = (u8*)(line + 1);
            SetRotMatrix(&currentRenderingMatrix);
            SetTransMatrix(&currentRenderingMatrix);
            z = RotTransPers(&v, (s32*)&line->x0, &tmp, &tmp) >> gDepthDivider;
            actor->m2E_otIndex = z;
            v.vx = actor->m0_position.vx >> 16;
            v.vy = actor->m0_position.vy >> 16;
            v.vz = actor->m0_position.vz >> 16;
            v.vx = target->m0_position.vx >> 16;
            v.vy = target->m0_position.vy >> 16;
            v.vz = target->m0_position.vz >> 16;
            RotTransPers(&v, (s32*)&line->x1, &tmp, &tmp);
            setlen(line, 3);
            *(u32*)&line->r0 = *(u32*)&actor->m28_r;
            AddPrim(&characterRenderingOT[z], line);
            tpage = (DR_TPAGE*)shapeTransfertTableCurrentEntry;
            if ((u8*)(tpage + 1) < shapeTransfertTableEnd) {
#if VERSION_JP
                if (actor->m3C_x5_blendMode != 0) {
                    tpage->code[0] = (((actor->m3C_x5_blendMode - 1) & 3) << 5) | 0xE1000000;
                    shapeTransfertTableCurrentEntry = (u8*)(tpage + 1);
                    setlen(tpage, 1);
                    AddPrim(&characterRenderingOT[z], tpage);
                }
#else
                blend = actor->m3C_x5_blendMode;
                if (blend != 0) {
                    shapeTransfertTableCurrentEntry = (u8*)(tpage + 1);
                    setlen(tpage, 1);
                    tpage->code[0] = (((blend - 1) & 3) << 5) | 0xE1000000;
                    AddPrim(&characterRenderingOT[z], tpage);
                }
#endif
            }
        }
    }
}

void battleSpriteEffect_1E(SpriteActorCore* actor, u8* args) {
    actor->m34_currentSpriteFrame = 1;
    setTaskDrawFunction(&((SpriteTask*)actor->m6C_owner)->m1C_drawTask, battleSpriteEffect_1E_draw);
    actor->m2B_code = 0x40;
}

void battleSpriteEffect_46(SpriteActorCore* actor, u8* args) {
    u8* stack = allocateHeap(0x4000, 1);

    STACK_SWITCH(stack + 0x3E00);
    pendingEffectMechaEvents = 0;
    createEffectMechaLoadingFileRequests(args[0]);
    STACK_RESTORE();
    freeHeap(stack);
}

void battleSpriteEffect_51(SpriteActorCore* actor, u8* args) {
    u8* stack = allocateHeap(0x4000, 1);

    STACK_SWITCH(stack + 0x3E00);
    freeMecha_battle(0xB);
    STACK_RESTORE();
    freeHeap(stack);
}

void battleSpriteEffect_47(SpriteActorCore* actor, u8* args) {
    u8* stack;

    stack = allocateHeap(0x4000, 1);
    STACK_SWITCH(stack + 0x3E00);
    if (args[0] == 2) {
        finalizeBattleMechaLoadingCommands(0xB, 0x300, 0x100, 0, 0x1DB);
        performMechaPlayAnimation(0xB, 1 << BATTLE_SLOT(processBattleAnimationSub0_var1), args[0]);
    } else {
        performMechaPlayAnimation(0xB, currentJumpAnimationBitMask, args[0]);
    }
    STACK_RESTORE();
    freeHeap(stack);
}

void battleSpriteEffect_3A(SpriteActorCore* actor, u8* args) {
    u8* p = ARG_PTR(args);

    battleSpriteEffect_3A_sub(p[5], p[3], p[4], (s8)p[0], (s8)p[1], (s8)p[2]);
}

void battleSpriteEffect_38(SpriteActorCore* actor, u8* args) {
    uploadTextureToVram((s32*)ARG_PTR(args), 0, 0, 0, 0, 0, 0);
}

void battleSpriteEffect_2C(SpriteActorCore* actor, u8* args) {
    setTaskDrawFunction(&((SpriteTask*)actor->m6C_owner)->m1C_drawTask, spriteCallback_renderBillboardModel);
}

/* Shifts the CLUT of every sub-sprite of `actor` by (args[0], args[1]) (x in
 * 16-pixel units, y in lines). */
void battleSpriteEffect_27(SpriteActorCore* actor, u8* args) {
    s16 i;
    s16 n = actor->m40_draw.bits.x2_subSpriteCount;
#if VERSION_JP
    SubSprite* sub = actor->m20_object->m2C_subSpriteBuffer; /* JP reads the other sub-sprite pointer */
#else
    SubSprite* sub = actor->m20_object->m30_subSprites;
#endif
    s32 lo;
    s32 hi;

    for (i = 0; i != n; i++) {
        lo = sub->mC_clut & 0x3F;
        hi = (sub->mC_clut >> 6) & 0x1FF;
        lo += args[0];
        hi += args[1];
        sub->mC_clut = lo | (hi << 6);
        sub++;
    }
}

void battleSpriteEffect_26(SpriteActorCore* actor, u8* args) {
    spriteActorSetPlayingAnimation(battleBssBlock.m8C8C_spriteActorCores[args[1]], args[0]);
}

void battleSpriteEffect_23(SpriteActorCore* actor, u8* args) {
    Vec2s16 pos;
    Vec2s16 dest;
    s16 dir;

    pos.vx = actor->m0_position.vx >> 16;
    pos.vy = actor->m0_position.vz >> 16;
    dest.vx = actor->mA0_destination.vx;
    dest.vy = actor->mA0_destination.vz;
    dir = vec2dRatan2(dest, pos);
    spriteActorSetDirection(actor, dir);
    setSpriteActorAngle(actor, dir);
}

/* Turns the actor's step vector towards mA0 by at most args[0] * 4 angle units
 * per axis (yaw vy, pitch vz; 12-bit wrapped differences), keeping its speed. */
void battleSpriteEffect_40(SpriteActorCore* actor, u8* args) {
    SVECTOR aim;
    SVECTOR cur;
    VECTOR d;
    VECTOR sq;
    VECTOR s;
    SVECTOR v;
    MATRIX m;
    VECTOR out;
    s32 len;
    s32 speed;
    s32 limit;
    s32 len2;
    s16 delta;
    s16 turn;

    d.vx = actor->mA0_destination.vx - (actor->m0_position.vx >> 16);
    d.vy = actor->mA0_destination.vy - (actor->m0_position.vy >> 16);
    d.vz = actor->mA0_destination.vz - (actor->m0_position.vz >> 16);
    Square0(&d, &sq);
    len = SquareRoot0(sq.vx + sq.vz);
    aim.vy = -ratan2(d.vz, d.vx);
    aim.vz = ratan2(d.vy, (s16)len);
    aim.vx = 0;
    s.vx = actor->mC_step.vx >> 7;
    s.vy = actor->mC_step.vy >> 7;
    s.vz = actor->mC_step.vz >> 7;
    Square0(&s, &sq);
    speed = SquareRoot0(sq.vy + sq.vx + sq.vz);
    len2 = SquareRoot0(sq.vx + sq.vz);
    cur.vy = -ratan2(s.vz, s.vx);
    cur.vz = ratan2(s.vy, (s16)len2);
    cur.vx = 0;
    limit = args[0] * 4;
    delta = (s16)((aim.vy << 4) - (cur.vy << 4)) >> 4;
    turn = delta;
    if (limit < abs(delta)) {
        turn = limit;
        if (delta < 0) {
            turn = -limit;
        }
    }
    cur.vy += turn;
    delta = (s16)((aim.vz << 4) - (cur.vz << 4)) >> 4;
    turn = delta;
    if (limit < abs(delta)) {
        if (delta < 0) {
            turn = -limit;
        } else {
            turn = limit;
        }
    }
    cur.vz += turn;
    setSVector3(&v, (s16)speed, 0, 0);
    createRotationMatrix(&cur, &m);
    ApplyMatrix(&m, &v, &out);
    actor->mC_step.vx = out.vx << 7;
    actor->mC_step.vy = out.vy << 7;
    actor->mC_step.vz = out.vz << 7;
}

void battleSpriteEffect_1C(SpriteActorCore* actor, u8* args) {
    SVECTOR rot;
    VECTOR d;
    VECTOR sq;
    SVECTOR v;
    MATRIX m;
    VECTOR out;
    s32 len;

    d.vx = actor->mA0_destination.vx - (actor->m0_position.vx >> 16);
    d.vy = actor->mA0_destination.vy - (actor->m0_position.vy >> 16);
    d.vz = actor->mA0_destination.vz - (actor->m0_position.vz >> 16);
    Square0(&d, &sq);
    len = SquareRoot0(sq.vx + sq.vz);
    rot.vy = -ratan2(d.vz, d.vx);
    rot.vz = ratan2(d.vy, len);
    rot.vx = 0;
    actor->m32_direction = rot.vy;
    setSVector3(&v, (actor->m18_moveSpeed << 9) >> 16, 0, 0);
    createRotationMatrix(&rot, &m);
    ApplyMatrix(&m, &v, &out);
    actor->mC_step.vx = out.vx << 7;
    actor->mC_step.vy = out.vy << 7;
    actor->mC_step.vz = out.vz << 7;
}

void battleSpriteEffect_2D(SpriteActorCore* actor, u8* args) {
    u8* p = ARG_PTR(args);

    actor->m0_position.vx = ARG_S16(p) << 16;
    actor->m0_position.vy = ARG_S16(p + 2) << 16;
    actor->m0_position.vz = ARG_S16(p + 4) << 16;
}

void battleSpriteEffect_2E(SVECTOR* v, u8* args) {
    u8* p = ARG_PTR(args);
    s32 t;

    t = ARG_S16(p);
    v->vx = t;
    t = ARG_S16(p + 2);
    v->vy = t;
    t = ARG_S16(p + 4);
    v->vz = t;
}

void battleSpriteEffect_5D(SVECTOR* v, u8* args) {
    u8* p = ARG_PTR(args);
    s32 t;

    t = ARG_S16(p);
    v->vx += t;
    t = ARG_S16(p + 2);
    v->vy += t;
    t = ARG_S16(p + 4);
    v->vz += t;
}

void battleSpriteEffect_1B(SpriteActorCore* actor, u8* args) {
    SpriteActorCore* other = actor->m70_parent;

    actor->mC_step.vx = other->mC_step.vx;
    actor->mC_step.vy = other->mC_step.vy;
    actor->mC_step.vz = other->mC_step.vz;
}

void battleSpriteEffect_1D(SpriteActorCore* actor, u8* args) {
    u8* p = ARG_PTR(args);
    s32 a;
    s32 b;
    s32 c;
    ModelObject* model;

    if ((u16)actor->m3A_scaleModifier != 0) {
        a = modulateSpeed(actor, ((s8)p[1] << 3) | p[0]) << 8;
        b = modulateSpeed(actor, p[2]) << 16;
        c = modulateSpeed(actor, p[3]) << 16;
    } else {
        a = (((s8)p[1] << 3) | p[0]) << 5;
        b = p[2] << 13;
        c = p[3] << 13;
    }
    model = (ModelObject*)actor->m20_object;
    createFxModelShatter(model->m34_modelBlock, model->m2C_primBuffers[0], &model->mC_matrix, a, b, c, p[4] << 4, p[5] << 2);
    ((ModelObject*)actor->m20_object)->m34_modelBlock = 0;
    ((ModelObject*)actor->m20_object)->m2C_primBuffers[0] = 0;
    ((ModelObject*)actor->m20_object)->m2C_primBuffers[1] = 0;
}

void battleSpriteEffect_19(SpriteActorCore* actor, u8* args) {
    u8* p = ARG_PTR(args);

    createFxSpriteWobble(actor, p[0] << 4, p[1], (s8)p[2] << 3, p[3] << 3, (s8)p[4] << 3, p[5]);
}

void battleSpriteEffect_18(SpriteActorCore* actor, u8* args) {
    RECT rect;

    rect.x = 0;
    rect.y = 0;
    rect.w = 0x140;
    rect.h = 0xE0;
    MoveImage(&rect, 0x2C0, 0x100);
    /* The wide picture's shatter (b_800B6F0C.c) needs the capture's margins. */
    pcWideShatterCapture(pcWideCapture(&rect, 0x2C0, 0x100));
    createBattleStartEffectTask();
}

void battleSpriteEffect_A(SpriteActorCore* actor, u8* args) {
    actor->m20_object->m0_rotation[2] = ratan2(actor->mC_step.vy >> 8, actor->mC_step.vx >> 8);
    actor->m3C_x1C_matrixDirty = 1;
}

void battleSpriteEffect_B(SpriteActorCore* actor, u8* args) {
    actor->m20_object->m0_rotation[0] = ratan2(actor->mC_step.vz >> 8, actor->mC_step.vx >> 8);
    actor->m3C_x1C_matrixDirty = 1;
}

void battleSpriteEffect_C(SpriteActorCore* actor, u8* args) {
    VECTOR v;
    VECTOR sq;
    s32 len;

    v.vx = actor->mC_step.vx >> 8;
    v.vy = actor->mC_step.vy >> 8;
    v.vz = actor->mC_step.vz >> 8;
    if (v.vz == 0) {
        v.vz = 4;
    }
    Square0(&v, &sq);
    len = SquareRoot0(sq.vx + sq.vz);
    actor->m20_object->m0_rotation[1] = -ratan2(v.vz, v.vx);
    actor->m20_object->m0_rotation[2] = ratan2(v.vy, len);
    actor->m20_object->m0_rotation[0] = 0;
    actor->m3C_x1C_matrixDirty = 1;
}

void battleSpriteEffect_8(SpriteActorCore* actor, u8* args) {
    s32 i;

    if (actor->m20_object != 0 && actor->m20_object->m34_perSubgroupTransform != 0) {
        for (i = 0; i != 8; i++) {
            actor->m20_object->m34_perSubgroupTransform[i].m0_translateX = 0;
            actor->m20_object->m34_perSubgroupTransform[i].m1_translateY = 0;
            actor->m20_object->m34_perSubgroupTransform[i].m2_rotateX = 0;
            actor->m20_object->m34_perSubgroupTransform[i].m4_rotateY = 0;
            actor->m20_object->m34_perSubgroupTransform[i].m6_rotateZ = 0;
        }
        actor->m20_object->m3C_anchorX = 0;
        actor->m20_object->m3D_anchorY = 0;
    }
}

void battleSpriteEffect_D(SpriteActorCore* actor, s8* args) {
    SVECTOR rot;
    MATRIX m;
    VECTOR out;

    setSVector3(&rot, 0, 0, args[0] << 4);
    createRotationMatrix(&rot, &m);
    ApplyMatrixLV(&m, (VECTOR*)&actor->mC_step, &out);
    actor->mC_step.vx = out.vx;
    actor->mC_step.vy = out.vy;
    actor->mC_step.vz = out.vz;
}
