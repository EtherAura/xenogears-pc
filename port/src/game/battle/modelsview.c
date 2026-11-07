/*
 * The model viewer's way into the battle's copy of the Gear code
 * (modelsview.h; PC only, called inside an extras sandbox, never by the game).
 */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/sprite.h"
#include "field_mecha/mecha.h"
#include "battle/battle.h"
#include "pc_convert.h"
#include "modelsview.h"

/* battle (b_800A8B0C.c) */
void mechaInitForBattle(void);
void mechaInitEnvironmentMechaMesh(s32 entryId, u16 flags, sMechaDataTable2* pData2, sMechaDataTable1* pData1, s16 tpageX,
                                   s16 tpageY, s16 clutX, s16 clutY, SVECTOR* pPosition);
void mechaPlayAnimation_battle(u16 index, u16 arg1, s32 animationId);
void renderMechasBattle(MATRIX* pMatrix, MATRIX* pArgs, OTag* ot, s32 oddOrEven);
void initFieldEntitySub4Sub5Sub0(SpriteVramData* vram, void* bundle, Vec2s16 vramLocation, Vec2s16 clut, s32 unused);
s32 uploadTextureToVram(void* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY);
extern MATRIX* battle800d2fc0;
extern sMechaInitVar2 battleMechaInitVar2;
extern s32 allocateJumpAnimationStructVar0;
extern s32 battle800ccc5c;
extern u8 battleMechaExtraAnimFiles[]; /* the extra animation file per op 0x04 operand */
extern SpriteVramData createSavePointMeshData_mode6;
static u32 rigGeneration[32];

void modelsBattleRigCreated(s32 slot) {
    if (slot >= 0 && slot < 32) rigGeneration[slot]++;
}

u32 modelsBattleRigGeneration(s32 slot) {
    return slot >= 0 && slot < 32 ? rigGeneration[slot] : 0;
}

s32 modelsBattleInit(MATRIX* colour, s32 backR, s32 backG, s32 backB) {
    sBattleMechaInitData* cfg = allocateHeap((s32)sizeof(sBattleMechaInitData), 0);

    if (cfg == NULL) {
        return 0;
    }
    memset(cfg, 0, sizeof(*cfg));
    memset(rigGeneration, 0, sizeof(rigGeneration));
    cfg->m344_mechaScale = 0x1000;
    cfg->m348_trackPoolSize = 0x400;
    cfg->m34A_trailPoolSize = 0x20;
    cfg->m474_backR = backR;
    cfg->m475_backG = backG;
    cfg->m476_backB = backB;
    cfg->m478_r = 0x40;
    cfg->m479_g = 0x40;
    cfg->m47A_b = 0x40;
    battleEnvMechaInitData = cfg;
    battle800d2fc0 = colour;
    mechaInitForBattle();
    /* op 0x3E reads battleCurrentDamages[allocateJumpAnimationStructVar0 - 1]:
     * the first, as in a battle's first action. */
    allocateJumpAnimationStructVar0 = 1;
    battle800ccc5c = 0;
    return 1;
}

void* modelsBattleLoad(s32 slot, void* anim, void* model, s16 tpageX, s16 tpageY, s16 clutX, s16 clutY) {
    sLoadedMechas* m;

    mechaInitEnvironmentMechaMesh(slot, 4, anim, model, tpageX, tpageY, clutX, clutY, NULL);
    m = battleMechas[slot];
    if (m != NULL) {
        m->m34_visible = 1;
        modelsBattleRigCreated(slot);
    }
    return m;
}

void modelsBattleExtraFile(s32 operand, s32 param, s32* file) {
    *file = param + battleMechaExtraAnimFiles[operand];
}

/* As op 0x04 then op 0x05 (the PC's branch) leave the mecha, but the
 * sequence: m63 says not loaded, and the driver is off in this world. */
s32 modelsBattleSetExtra(s32 slot, void* file) {
    sLoadedMechas* m = battleMechas[slot];
    sBattleMechaSoundData* pSound;
    sMechaDataTable2_4* pAnim;
    Vec2s16 vramLocation;
    Vec2s16 clut;

    if (m == NULL || file == NULL) {
        return 0;
    }
    m->mC_extraAnim = (sMechaExtraAnim*)pcConvertMechaData2(file);
    pSound = (sBattleMechaSoundData*)((sMechaDataTable2*)m->mC_extraAnim)->m8_sound;
    m->mB4_extraSound = (sMechaDataTable2_8*)pSound;
    if (pSound->m18_texturesEnd != pSound->m14_textures) {
        uploadTextureToVram(pSound->m14_textures, 1, 0x380, 0x100, 1, 0, 0x1D0);
    }
    m->m63_isSeq2Loaded = 0;
    pAnim = ((sMechaDataTable2*)m->mC_extraAnim)->m4_anim;
    m->m18_extraKeyframes = (sMechaDataTable2_4_4_array*)pAnim->m4_keyframes;
    if (pSound->m8_seq != pSound->m4_spriteBundle) {
        vramLocation.vx = 0x380;
        vramLocation.vy = 0x100;
        clut.vx = 0;
        clut.vy = 0x1D0;
        initFieldEntitySub4Sub5Sub0(&createSavePointMeshData_mode6, pSound->m4_spriteBundle, vramLocation, clut, 0);
    }
    m->m23_extraLoadPending = 0;
    return 1;
}

void modelsBattlePlay(s32 slot, s32 animation) {
    /* Its own slot as the target mask (the events' hit-result checks read
     * the target's entry). */
    mechaPlayAnimation_battle(slot, 1 << slot, animation);
}

void modelsBattleRender(MATRIX* view, MATRIX* light, OTag* ot, s32 oddOrEven, s32 step) {
    /* renderMechasBattle adds 1 + battle800ccc5c to its half-step counter and
     * steps the VM per 2: a frame of one vblank adds 1. A step: called twice
     * as two such frames (the first leaves nothing to keep); a pose: adds 0. */
    battle800ccc5c = step ? 0 : -1;
    renderMechasBattle(view, light, ot, oddOrEven);
    battle800ccc5c = 0;
}

void* modelsBattleMecha(s32 slot) {
    return slot >= 0 && slot < 32 ? battleMechas[slot] : NULL;
}

s32 modelsBattleTracksInUse(void) {
    s32 n = 0;
    s32 i;

    for (i = 0; battleMechaInitVar2.m0_tracks != NULL && i < battleMechaInitVar2.m6_count; i++) {
        n += battleMechaInitVar2.m0_tracks[i].m0_inUse != 0;
    }
    return n;
}
