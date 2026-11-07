/*
 * Damage display (Noah damageDisplay.cpp, damageDisplay2.cpp) and the battle
 * render / pad sampling loop. 0x800BD1FC-0x800BFC80 (JP 0x800BC8D4-0x800BF458); US gcc-2.7.2-cdk,
 * ASPSX 2.56; JP gcc-2.6.3-psx, ASPSX 2.34.
 */
#include "common.h"
#include "main/debug_font.h"
#include "pc_wide.h"
#include "pc_speedup.h"
#include "psyq.h"
#include "main/sprite.h"
#include "main/task.h"
#include "main/memory.h"
#include "battle/battle.h"

void battlePreviewBeginFrame(void);
void battlePreviewEndFrame(void);

extern s32 allocateJumpAnimationStructVar0;
extern sJumpAnimationControlStruct* jumpAnimationControlStruct;
extern s32 damageDisplayVar0;
extern sDamageDisplayPoly* pDamageDisplayPolysTask2;
extern s32 damageDisplayVar1;
extern SpriteActorCore* processBattleAnimationSub0_var1;
void createDamageDisplay(SpriteActorCore* actor, s32 damageValue, s32 damageType);
void deleteDamageDisplayTask(TaskHeader* task);
void drawDamageDisplayCharacter(sDamageDisplaySub0* glyph, u32 color);
void setupDamageDisplayMatrix(SVECTOR* pos, VECTOR* out);
void drawDamageDisplayTask(TaskHeader* task);
void damageMissUpdate(TaskHeader* task);
void updateDamageDisplayTask_FadeOut(TaskHeader* task);
void updateDamageDisplayTask2(TaskHeader* task);
void updateDamageDisplayTask(TaskHeader* task);
void createDamageDisplayPolysTask2(void);
void updateDamageDisplayPolys(void);
void formatDamageString(u32 value, sDamageString* str, s32 digits, u8 leadingZeros, s32 base);
extern sDamageDisplayTask* damageTaskDisplayListHead;
extern s16 damageDysplayOffsetBySize[5];
extern void* battleFont;
TaskHeader* allocateTaskPair(s32 size, TaskHeader* owner, void (*update)(TaskHeader*), void (*draw)(TaskHeader*), void (*del)(TaskHeader*));
void freeTaskPair(TaskHeader* task);
void registerSpriteCallback2(TaskHeader* owner, TaskHeader* task);
void registerSpriteCallback2Sub0(TaskHeader* task);
void allocateSavePointMeshDataSub0(TaskHeader* owner, TaskHeader* task);
void allocateSavePointMeshDataSub0_callback(TaskHeader* task);
s32 setupDamagePoly(void* font, s32 glyphIndex, sDamageDisplaySub0* out, s16 x, s16 y);
/* Enhanced: damage digits step 8 units, as the party HP readout (HP_positionTable
 * steps 8 over the same digit cells) and as the game's own centring table assumes
 * (damageDysplayOffsetBySize: -4 per digit). Retail steps 10 (the number sits n units
 * right of centre and its 8-10 wide cells never overlap: 0-4 units between outlines),
 * which shows as gaps once the digits are drawn sharp (user report). Cost:
 * at 8 neighbouring outlines overlap by about a unit, so the additive fade-out
 * (updateDamageDisplayTask2) brightens those spots slightly; 9 halves that. */
#define DAMAGE_DIGIT_ADVANCE 8
s32 addClamp255(s32 value, s32 delta);

extern u8* shapeTransfertTableEnd;
extern u8* shapeTransfertTableCurrentEntry;
extern OTag* characterRenderingOT;
extern SVECTOR currentSpriteCharacterSize[4];
extern MATRIX damageDisplayMatrix;

extern sDamageDisplayPoly damageDisplayPolyTask2;
extern u8 damageDisplayPolysTask2Disabled;
extern s32 createDamageDisplayPolysTask2Var0;
extern s32 initBattleUnkData0Var1;
extern u8 strHexDigits[16];             /* hex digits */
extern u32 damageDecimals[];
extern s32 allocateSavePointMeshDataSub0_var0;
extern u8 spriteBytecode2ExtendedE0_Var0;
void setupTotalDamageDisplayPoly(void* font, s32 glyphIndex, s32 x, s32 y, OTag* ot);
void copySVector3(SVECTOR* dst, SVECTOR* src);
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animationId);
void createDamageDisplayTask3(s32 value);
void updateDamageDisplayTask3(TaskHeader* task);
void drawDamageDisplayTask3(TaskHeader* task);
void battleRender(void);
s32 mecha_battle_op3(void);
void portBattleSweepFrame(void);
void portBattleSweepPicture(const s32* extra, s32 count);
void pcBattle60SceneBegin(void);
void pcBattle60SceneEnd(void);
extern SVECTOR pcBattle60DrawnEye;
extern SVECTOR pcBattle60DrawnAt;
void portBattleWideTest(void);

/* battleRender (battleRender) */
#include "stack.h"
#include "pollhost.h"
#include "main/filesystem.h"
extern s32 battleRenderCount;
extern s16 spriteCallback2Var4;
extern s32 fieldDrawEnvsInitialized;
extern u16 isFxFragmentsSetup;
extern u8 finalizeBattleMechaLoadingVar0;
void checkSoftReboot(void);
void execSpritesCallbacksList1(void);
void execSpritesCallbacksList2(void);
void uploadCharacterSprites(void);
void clearShapeTransfertTableEntry(s32 oddOrEven);
void setCurrentRenderingMatrix(MATRIX* m);
void setCharacterRenderingOT(OTag* ot);
void shapeTransfert(void);
void updateBattleCamera(void);
void renderBattleScene(void);
void renderMechasBattle(MATRIX* m, MATRIX* m2, OTag* ot, s32 oddOrEven);
void drawBattleFromModes(void);
void battleUpdateInputs(s32 arg0);
void fxFragmentSetup(s32 arg0);
void clearAnimationsEndOfTurnSub(void);
void updateBattleDebugger(void);             /* debug overlay */
void battleLoadFileFrom0xC(void);
void pollBattlePadDebug(void);
void updateBattlePadState(void);
extern u8 initFieldVar4;
extern u8 currentFileFrom0xC;
extern u8 initFieldVar5;
void battleIdleDuringLoading(void);
void EnterCriticalSection(void);
void FlushCache(void);
void ExitCriticalSection(void);
s32 readPadButtons(s32 port);          /* pad read */
s32 vec2dRatan2(Vec2s16 a, Vec2s16 b);
void jumpUpdatePositionSub1(SpriteActorCore* actor, s32 anim);
void jumpUpdatePositionSub0(SpriteActorCore* actor, SpriteActorCore* target);
s16 getRatan2ToDest(SpriteActorCore* actor);
s32 computerJumpDistanceToTarget(Vec2s16 a, Vec2s16 b);
void spriteActorSetDirection(SpriteActorCore* actor, s32 direction);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
void clearBattleAnimationData(void);
extern void* battleAnimationLoadingDest;
extern s32 startReadingBattleJumpAnimationVar0;
extern u8 fxFragmentLoaded;
u8* loadEffectFragmentsAndAudio(void* data);
void clearFragmentSeq(void* data);
extern s16 numActiveSpriteCores;
extern SpriteActorCore* pSpriteCoreListHead[];
void battleAnimationCallback_2(SpriteActorCore* actor, SpriteActorCore* target);
void jumpUpdatePosition(SpriteActorCore* actor);
extern s32 abilityPlayAnimationVar0;
extern SpriteActorCore* needFxFragments;
void abilityPlayAnimationSub1(s32 value);
void abilityPlayAnimationSub0(void);
s32 isDamageDisplayPolysTask2Running(void);
void updateSpriteDistanceWatch(TaskHeader* task);
s32 battleSpriteOpcode_FB_sub0(SpriteActorCore* actor);
TaskHeader* allocateUpdateTask(TaskHeader* owner, s32 size);
void createAllDamageDisplays(void);
extern s16 effectMechaEventCount;
extern s16 pendingEffectMechaEvents;
void freeMecha_battle(s32 index);
extern u8 battleTextureReloadPending;
s32 uploadTextureToVram(s32* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY);
extern u8 battleAnimGearUnloadMode;
extern s32 mainBattleSpriteCallback_phase5Var;
void waitBattleAnimationSpriteLoading(void);
void createMechaLoadingTask(s32 entity);
extern u8 battleAnimationSoundLoaded;
extern u8 startJumpAnimationVar3;
extern Wds* loadWdsDataNeeded;
Wds* findInWdsLinkedList(s32 id);
void loadWdsDataIfNeeded(void);
extern s32 JumpAnimationStuffVar0;
extern u16 processBattleAnimationSub0_var0;
void allocateJumpAnimationStructCallback(sJumpAnimationControlStruct* jump);
void setupJumpAnimationStruct1C(s32 mode);

/* createDamageDisplayForEntity-like: show the current attack's damage on entity arg0 (Noah) */
void pcRumbleBattleDamage(int entity, int type, int value);
void createDamageDisplayIfValid(s32 entity);
void createDamageDisplayIfValid(s32 entity) {
    SpriteActorCore* actor;

    if (battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0 - 1].m18_damageType[entity] != 0xFF
        && battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0 - 1].m0_damageValue[entity] != 0xFFFF) {
        actor = battleBssBlock.m8C8C_spriteActorCores[entity];
        if (actor != NULL) {
            s32 type = battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0 - 1].m18_damageType[entity];
            s32 total = battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0 - 1].m24_totalDamages[entity];
            s32 color = battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0 - 1].m3C_totalDamageColors[entity];
            s32 value = battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0 - 1].m0_damageValue[entity];
            damageDisplayVar0 = total;
            damageDisplayVar1 = color;
            pcRumbleBattleDamage(entity, type, value); /* vibration=enhanced (port/src/system/rumble_game.c) */
            createDamageDisplay(actor, value, type);
            battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0 - 1].m0_damageValue[entity] = 0xFFFF;
        }
    }
}

/* Noah createAllDamageDisplays (sprite opcode 0x8B) */
void createAllDamageDisplays(void) {
    s32 i;
    u8 flag = 0;

    for (i = 0; i != 11; i++) {
        if (battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0 - 1].m18_damageType[i] == 7) {
            flag = 1;
        }
        if (battleBssBlock.m8C8C_spriteActorCores[i] != NULL
            && (processBattleAnimationSub0_var1 != battleBssBlock.m8C8C_spriteActorCores[i] || !flag)) {
            createDamageDisplayIfValid(i);
        }
    }
}

/* Noah createDamageDisplay (switch, jtbl 0x80070ADC; unused 0x30-byte local keeps retail's frame) */
void createDamageDisplay(SpriteActorCore* actor, s32 damageValue, s32 damageType) {
    sDamageDisplayTask* p;
    sDamageDisplayTask* t;
    sDamageString str;
    u8 unused[0x30]; /* unused: reserves retail's 0x68-byte frame */
    s32 offset;
    s32 i;

    if (jumpAnimationControlStruct == NULL) {
        return;
    }
    createDamageDisplayPolysTask2();
    battleBssBlock.mA79 = 0;
    for (p = damageTaskDisplayListHead; p != NULL; p = p->m38_pNext) {
        if (p->m78_actor == actor) {
            p->m0_updateTask.mC_deleteCallback(&p->m0_updateTask);
        }
    }
    t = (sDamageDisplayTask*)allocateTaskPair((s32)sizeof(sDamageDisplayTask), NULL, (void*)updateDamageDisplayTask, (void*)drawDamageDisplayTask, (void*)deleteDamageDisplayTask);
    t->m78_actor = actor;
    t->m88_timer = 8;
    t->m38_pNext = damageTaskDisplayListHead;
    damageTaskDisplayListHead = t;
    t->m48_position.vx = actor->m0_position.vx;
    t->m48_position.vy = actor->m0_position.vy;
    t->m48_position.vz = actor->m0_position.vz;
    t->m58_scale.vx = 0x2000;
    t->m58_scale.vy = 0x2000;
    t->m58_scale.vz = 0x2000;
    t->m80_colorAndCode.m3_code = 0x2D;
    t->m40_rotation.vx = 0;
    t->m40_rotation.vy = 0;
    t->m40_rotation.vz = 0;
    t->m80_colorAndCode.m0_r = 0x80;
    t->m80_colorAndCode.m1_g = 0x80;
    t->m80_colorAndCode.m2_b = 0x80;
    t->m7C_direction = actor->mAC_x2_facing;
    t->m8C_damageStringLength = 0;
    if (damageType != 4) {
        formatDamageString(damageValue, &str, 5, 0, 0);
        offset = damageDysplayOffsetBySize[str.m0_length - 1];
    }
    switch (damageType) {
    case 4:
        t->m88_timer = 0x40;
        t->m8C_damageStringLength = setupDamagePoly(battleFont, 0x7C, &t->m90_glyphs[0], -20, -16);
        t->m80_colorAndCode.m3_code = (t->m80_colorAndCode.m3_code & 0xFE) | 2;
        setTaskUpdateFunction(&t->m0_updateTask, (void*)damageMissUpdate);
        break;
    case 5:
        t->m80_colorAndCode.m0_r = 0x80;
        t->m80_colorAndCode.m1_g = 0;
        t->m80_colorAndCode.m2_b = 0;
        t->m80_colorAndCode.m3_code &= 0xFE;
        break;
    case 10:
        t->m8C_damageStringLength += setupDamagePoly(battleFont, 0x7F, &t->m90_glyphs[t->m8C_damageStringLength], offset, 0);
        break;
    case 11:
        t->m8C_damageStringLength += setupDamagePoly(battleFont, 0x91, &t->m90_glyphs[t->m8C_damageStringLength], offset, 0);
        t->m80_colorAndCode.m0_r = 0;
        t->m80_colorAndCode.m1_g = 0;
        t->m80_colorAndCode.m2_b = 0x80;
        t->m80_colorAndCode.m3_code &= 0xFE;
        break;
    case 1:
        t->m8C_damageStringLength += setupDamagePoly(battleFont, 0x80, &t->m90_glyphs[t->m8C_damageStringLength], offset, -16);
        offset += 0x20;
        break;
    case 3:
        t->m8C_damageStringLength += setupDamagePoly(battleFont, 0x80, &t->m90_glyphs[t->m8C_damageStringLength], offset, -16);
        t->m80_colorAndCode.m0_r = 0x80;
        t->m80_colorAndCode.m1_g = 0;
        t->m80_colorAndCode.m2_b = 0x80;
        t->m80_colorAndCode.m3_code &= 0xFE;
        offset += 0x20;
        break;
    case 2:
        t->m80_colorAndCode.m0_r = 0;
        t->m80_colorAndCode.m1_g = 0x80;
        t->m80_colorAndCode.m2_b = 0;
        t->m80_colorAndCode.m3_code &= 0xFE;
        break;
    }
    if (damageType != 4) {
        i = 0;
        while (i != str.m0_length) {
            t->m8C_damageStringLength += setupDamagePoly(battleFont, str.m1_string[i] + 0x72, &t->m90_glyphs[t->m8C_damageStringLength], offset, -16);
            i++;
            offset += DAMAGE_DIGIT_ADVANCE;
        }
    }
}

/* Noah deleteDamageDisplayTask */
void deleteDamageDisplayTask(TaskHeader* task) {
    sDamageDisplayTask* t = (sDamageDisplayTask*)task;
    sDamageDisplayTask* p = damageTaskDisplayListHead;
    sDamageDisplayTask* prev = NULL;

    if (p != NULL) {
        do {
            if (p == t) {
                if (prev != NULL) {
                    prev->m38_pNext = p->m38_pNext;
                } else {
                    damageTaskDisplayListHead = p->m38_pNext;
                }
                break;
            }
            prev = p;
            p = p->m38_pNext;
        } while (p != NULL);
    }
    freeTaskPair(task);
}

/* Noah drawDamageDisplayCharacter (colour passed as a u32 word) */
void drawDamageDisplayCharacter(sDamageDisplaySub0* glyph, u32 color) {
    POLY_FT4* poly = (POLY_FT4*)shapeTransfertTableCurrentEntry;
    s32 p;
    s32 flag;
    s16 x, y;
    s32 w, h;
    s16 u, v, uw, vh;

    if (shapeTransfertTableCurrentEntry + sizeof(POLY_FT4) < shapeTransfertTableEnd) {
        shapeTransfertTableCurrentEntry += sizeof(POLY_FT4);
        setlen(poly, 9);
        *(u32*)&poly->r0 = color;
        poly->tpage = glyph->mA_tpage;
        poly->clut = glyph->mC_clut;
        w = glyph->m6_width;
        h = glyph->m7_height;
        x = glyph->m0_screenPosition[0];
        y = glyph->m0_screenPosition[1];
        currentSpriteCharacterSize[0].vx = x;
        currentSpriteCharacterSize[1].vx = x + w;
        currentSpriteCharacterSize[2].vx = x + w;
        currentSpriteCharacterSize[3].vx = x;
        currentSpriteCharacterSize[0].vy = y;
        currentSpriteCharacterSize[1].vy = y;
        currentSpriteCharacterSize[2].vy = y + h;
        currentSpriteCharacterSize[3].vy = y + h;
        RotTransPers4(&currentSpriteCharacterSize[0], &currentSpriteCharacterSize[1], &currentSpriteCharacterSize[2], &currentSpriteCharacterSize[3], (s32*)&poly->x0, (s32*)&poly->x1,
                      (s32*)&poly->x3, (s32*)&poly->x2, &p, &flag);
        u = glyph->m4_U;
        v = glyph->m5_V;
        uw = glyph->m6_width;
        vh = glyph->m7_height;
        poly->u0 = u;
        poly->v0 = v;
        poly->u1 = u + uw;
        poly->v1 = v;
        poly->u2 = u;
        poly->v2 = v + vh;
        poly->u3 = u + uw;
        poly->v3 = v + vh;
        pcTextureClass(poly, PC_TEX_TEXT);
        addPrim(characterRenderingOT, poly);
    }
}

/* Noah setupDamageDisplayMatrix (unused 8-byte local keeps retail's frame) */
void setupDamageDisplayMatrix(SVECTOR* pos, VECTOR* out) {
    s32 unused[2];
    Vec2s16 sxy;
    s32 p;
    s32 flag;
    s32 ofx;
    s32 ofy;

    SetRotMatrix(&battleCameraBlock.m20_renderingMatrix);
    SetTransMatrix(&battleCameraBlock.m20_renderingMatrix);
    RotTransPers(pos, (s32*)&sxy, &p, &flag);
    ReadGeomOffset(&ofx, &ofy);
    out->vx = (sxy.vx - ofx) * 2;
    out->vy = (sxy.vy - ofy) * 2;
    out->vz = ReadGeomScreen();
}

/* Noah drawDamageDisplayTask */
void drawDamageDisplayTask(TaskHeader* task) {
    MATRIX m;
    SVECTOR pos;
    VECTOR screen;
    sDamageDisplayTask* owner = (sDamageDisplayTask*)task->m4_data;
    sDamageDisplaySub0* glyph;
    s32 i;

    damageDisplayMatrix.t[2] = ReadGeomScreen();
    pos.vx = owner->m48_position.vx >> 16;
    pos.vy = owner->m48_position.vy >> 16;
    pos.vz = owner->m48_position.vz >> 16;
    setupDamageDisplayMatrix(&pos, &screen);
    createRotationMatrix(&owner->m40_rotation, &m);
    TransMatrix(&m, &screen);
    CompMatrix(&damageDisplayMatrix, &m, &m);
    ScaleMatrix(&m, &owner->m58_scale);
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    glyph = owner->m90_glyphs;
    for (i = 0; i != owner->m8C_damageStringLength; i++, glyph++) {
        drawDamageDisplayCharacter(glyph, *(u32*)&owner->m80_colorAndCode);
    }
}

/* Noah damageMissUpdate */
void damageMissUpdate(TaskHeader* task) {
    sDamageDisplayTask* t = (sDamageDisplayTask*)task;

    updateDamageDisplayPolys();
    t->m80_colorAndCode.m0_r -= 2;
    t->m80_colorAndCode.m1_g -= 2;
    t->m80_colorAndCode.m2_b -= 2;
    if (--t->m88_timer < 0) {
        t->m0_updateTask.mC_deleteCallback(&t->m0_updateTask);
    }
}

/* Noah updateDamageDisplayTask_FadeOut */
void updateDamageDisplayTask_FadeOut(TaskHeader* task) {
    sDamageDisplayTask* t = (sDamageDisplayTask*)task;

    updateDamageDisplayPolys();
    t->m80_colorAndCode.m0_r = addClamp255(t->m80_colorAndCode.m0_r, -8);
    t->m80_colorAndCode.m1_g = addClamp255(t->m80_colorAndCode.m0_r, -8);
    t->m80_colorAndCode.m2_b = addClamp255(t->m80_colorAndCode.m0_r, -8);
    if (--t->m88_timer < 0 || (t->m80_colorAndCode.m0_r | t->m80_colorAndCode.m1_g | t->m80_colorAndCode.m2_b) == 0) {
        t->m0_updateTask.mC_deleteCallback(&t->m0_updateTask);
    }
}

/* Noah updateDamageDisplayTask2 */
void updateDamageDisplayTask2(TaskHeader* task) {
    sDamageDisplayTask* t = (sDamageDisplayTask*)task;

    updateDamageDisplayPolys();
    if (--t->m88_timer < 0) {
        t->m88_timer = 0x10;
        t->m80_colorAndCode.m3_code = (t->m80_colorAndCode.m3_code | 2) & 0xFE;
        setTaskUpdateFunction(task, updateDamageDisplayTask_FadeOut);
    }
}

/* Noah updateDamageDisplayTask */
void updateDamageDisplayTask(TaskHeader* task) {
    updateDamageDisplayPolys();
    if (((sDamageDisplayTask*)task)->m7C_direction != 0) {
        ((sDamageDisplayTask*)task)->m48_position.vx += 0xC0000;
    } else {
        ((sDamageDisplayTask*)task)->m48_position.vx -= 0xC0000;
    }
    if (--((sDamageDisplayTask*)task)->m88_timer < 0) {
        ((sDamageDisplayTask*)task)->m88_timer = 0x10;
        setTaskUpdateFunction(task, updateDamageDisplayTask2);
    }
}

/* Noah freeDamageDisplayPolysTask2-like delete callback of the counter task (clears pDamageDisplayPolysTask2) */
void deleteDamageDisplayPolysTask2(TaskHeader* task);
void deleteDamageDisplayPolysTask2(TaskHeader* task) {
    pDamageDisplayPolysTask2 = NULL;
    registerSpriteCallback2Sub0(task + 1);
    allocateSavePointMeshDataSub0_callback(task);
}

/* empty draw callback of the total-damage counter task */
void updateDamageDisplayPolysTask2(void) {
}

/* Noah drawDamageDisplayPolysTask2 */
void drawDamageDisplayPolysTask2(TaskHeader* task);
void drawDamageDisplayPolysTask2(TaskHeader* task) {
    MATRIX m;
    SVECTOR unused;
    VECTOR v;
    s32 ofs[2];
    sDamageDisplayPoly* p = (sDamageDisplayPoly*)task->m4_data;
    sDamageDisplaySub0* glyph;
    s32 i;

    ReadGeomOffset(&ofs[0], &ofs[1]);
    setupTotalDamageDisplayPoly(battleFont, 0x81, p->m50_x, p->m52_y, characterRenderingOT);
    v.vx = (0xA0 - ofs[0]) * 2;
    v.vy = (0x46 - ofs[1]) * 2;
    v.vz = ReadGeomScreen();
    damageDisplayMatrix.t[2] = ReadGeomScreen();
    createRotationMatrix(&p->m38_rotation, &m);
    TransMatrix(&m, &v);
    CompMatrix(&damageDisplayMatrix, &m, &m);
    ScaleMatrix(&m, &p->m40_scale);
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    glyph = p->m68_glyphs;
    for (i = 0; i != p->m64_count; i++, glyph++) {
        drawDamageDisplayCharacter(glyph, *(u32*)&p->m60_colorAndCode);
    }
}

/* Noah createDamageDisplayPolysTask2 */
void createDamageDisplayPolysTask2(void) {
    if (pDamageDisplayPolysTask2 == NULL && !damageDisplayPolysTask2Disabled) {
        pDamageDisplayPolysTask2 = &damageDisplayPolyTask2;
        allocateSavePointMeshDataSub0(NULL, &damageDisplayPolyTask2.m0_updateTask);
        registerSpriteCallback2(&damageDisplayPolyTask2.m0_updateTask, &damageDisplayPolyTask2.m1C_drawTask);
        setTaskUpdateFunction(&damageDisplayPolyTask2.m0_updateTask, (void*)updateDamageDisplayPolysTask2);
        setTaskDrawFunction(&damageDisplayPolyTask2.m1C_drawTask, drawDamageDisplayPolysTask2);
        setTaskDeleteFunction(&damageDisplayPolyTask2.m0_updateTask, deleteDamageDisplayPolysTask2);
        damageDisplayPolyTask2.m50_x = 0x90;
        damageDisplayPolyTask2.m52_y = 0x2A;
        damageDisplayPolyTask2.m0_updateTask.m4_data = &damageDisplayPolyTask2;
        damageDisplayPolyTask2.m1C_drawTask.m4_data = &damageDisplayPolyTask2;
        damageDisplayPolyTask2.m64_count = 0;
        createDamageDisplayPolysTask2Var0 = -1;
    }
}

/* Noah updateDamageDisplayPolys */
void updateDamageDisplayPolys(void) {
    sDamageDisplayPoly* p;
    s32 value = damageDisplayVar0;
    sDamageString str;
    s32 x;
    s32 i;

    if (pDamageDisplayPolysTask2 == NULL) {
        return;
    }
    if (damageDisplayPolysTask2Disabled) {
        return;
    }
    p = pDamageDisplayPolysTask2;
    if (value == createDamageDisplayPolysTask2Var0) {
        return;
    }
    createDamageDisplayPolysTask2Var0 = value;
    createDamageDisplayTask3(value);
    p->m60_colorAndCode.m3_code = 0x2D;
    p->m40_scale.vx = 0x2000;
    p->m40_scale.vy = 0x2000;
    p->m40_scale.vz = 0x2000;
    p->m60_colorAndCode.m3_code &= 0xFE;
    p->m38_rotation.vx = 0;
    p->m38_rotation.vy = 0;
    p->m38_rotation.vz = 0;
    switch (damageDisplayVar1) {
    case 11:
        p->m60_colorAndCode.m0_r = 0;
        p->m60_colorAndCode.m1_g = 0;
        p->m60_colorAndCode.m2_b = 0x80;
        break;
    case 3:
        p->m60_colorAndCode.m0_r = 0x80;
        p->m60_colorAndCode.m1_g = 0;
        p->m60_colorAndCode.m2_b = 0x80;
        break;
    case 2:
        p->m60_colorAndCode.m0_r = 0;
        p->m60_colorAndCode.m1_g = 0x80;
        p->m60_colorAndCode.m2_b = 0;
        break;
    default:
        p->m60_colorAndCode.m0_r = 0x80;
        p->m60_colorAndCode.m1_g = 0x80;
        p->m60_colorAndCode.m2_b = 0x80;
        break;
    }
    formatDamageString(value, &str, 5, 0, 0);
    x = damageDysplayOffsetBySize[str.m0_length - 1];
    p->m64_count = 0;
    i = 0;
    while (i != str.m0_length) {
        p->m64_count += setupDamagePoly(battleFont, str.m1_string[i] + 0x72, &p->m68_glyphs[p->m64_count], x, -8);
        i++;
        x += DAMAGE_DIGIT_ADVANCE;
    }
}

/* Noah freeDamageDisplayPolysTask2 */
void freeDamageDisplayPolysTask2(void);
void freeDamageDisplayPolysTask2(void) {
    if (pDamageDisplayPolysTask2 != NULL) {
        deleteDamageDisplayPolysTask2(&pDamageDisplayPolysTask2->m0_updateTask);
    }
}

/* Noah initBattleUnkData0 */
void initBattleUnkData0(void);
void initBattleUnkData0(void) {
    pDamageDisplayPolysTask2 = NULL;
    initBattleUnkData0Var1 = 0;
}

/* update callback of the floating total-damage particle (Noah createDamageDisplayTask3, missing there) */
void updateDamageDisplayTask3(TaskHeader* task) {
    sDamageParticleTask* t = (sDamageParticleTask*)task;

    t->m38_rotation.vz += t->m64_rotationStep;
    t->m40_scale.vx += 0x330;
    t->m40_scale.vy += 0x330;
    t->m40_scale.vz += 0x330;
    t->m68_colorAndCode.m0_r = addClamp255(t->m68_colorAndCode.m0_r, -4);
    t->m68_colorAndCode.m1_g = addClamp255(t->m68_colorAndCode.m1_g, -4);
    t->m68_colorAndCode.m2_b = addClamp255(t->m68_colorAndCode.m2_b, -4);
    if (--t->m60_timer == 0) {
        task->mC_deleteCallback(task);
    }
}

/* draw callback of the floating total-damage particle task: 6 fading copies */
void drawDamageDisplayTask3(TaskHeader* task) {
    MATRIX m;
    SVECTOR unused;
    SVECTOR rot;
    VECTOR v;
    VECTOR scale;
    s32 ofs[2];
    sDamageParticleTask* p = (sDamageParticleTask*)task->m4_data;
    sDamageDisplaySub0* glyph;
    s32 i;
    s32 j;

    ReadGeomOffset(&ofs[0], &ofs[1]);
    v.vx = (0xA0 - ofs[0]) * 2;
    v.vy = (0x46 - ofs[1]) * 2;
    v.vz = ReadGeomScreen();
    damageDisplayMatrix.t[2] = ReadGeomScreen();
    copySVector3(&rot, &p->m38_rotation);
    scale.vx = p->m40_scale.vx;
    scale.vy = p->m40_scale.vy;
    scale.vz = p->m40_scale.vz;
    for (i = 0; i != 6; i++) {
        createRotationMatrix(&rot, &m);
        TransMatrix(&m, &v);
        CompMatrix(&damageDisplayMatrix, &m, &m);
        ScaleMatrix(&m, &scale);
        SetRotMatrix(&m);
        SetTransMatrix(&m);
        for (j = 0, glyph = p->m70_glyphs; j != p->m6C_count; j++, glyph++) {
            drawDamageDisplayCharacter(glyph, *(u32*)&p->m68_colorAndCode);
        }
        rot.vz -= 5;
        scale.vx -= 0x330;
        scale.vy -= 0x330;
        scale.vz -= 0x330;
    }
}

/* Noah createDamageDisplayTask3 (MissingCode there): the floating total-damage particle */
void createDamageDisplayTask3(s32 value) {
    sDamageParticleTask* t;
    sDamageString str;
    s32 x;
    s32 i;

    t = (sDamageParticleTask*)allocateTaskPair((s32)offsetof(sDamageParticleTask, m70_glyphs[8]), NULL, updateDamageDisplayTask3, drawDamageDisplayTask3, NULL);
    t->m64_rotationStep = -(((rand() & 3) - 2) * 8);
    if (t->m64_rotationStep == 0) {
        t->m64_rotationStep = 6;
    }
    t->m60_timer = 0x20;
    t->m68_colorAndCode.m3_code = 0x2E;
    t->m40_scale.vx = 0x2000;
    t->m40_scale.vy = 0x2000;
    t->m40_scale.vz = 0x2000;
    t->m6C_count = 0;
    t->m38_rotation.vx = 0;
    t->m38_rotation.vy = 0;
    t->m38_rotation.vz = 0;
    switch (damageDisplayVar1) {
    case 11:
        t->m68_colorAndCode.m0_r = 0;
        t->m68_colorAndCode.m1_g = 0;
        t->m68_colorAndCode.m2_b = 0x80;
        break;
    case 3:
        t->m68_colorAndCode.m0_r = 0x80;
        t->m68_colorAndCode.m1_g = 0;
        t->m68_colorAndCode.m2_b = 0x80;
        break;
    case 2:
        t->m68_colorAndCode.m0_r = 0;
        t->m68_colorAndCode.m1_g = 0x80;
        t->m68_colorAndCode.m2_b = 0;
        break;
    default:
        t->m68_colorAndCode.m0_r = 0x80;
        t->m68_colorAndCode.m1_g = 0x80;
        t->m68_colorAndCode.m2_b = 0x80;
        break;
    }
    formatDamageString(value, &str, 5, 0, 0);
    x = damageDysplayOffsetBySize[str.m0_length - 1];
    t->m6C_count = 0;
    i = 0;
    while (i != str.m0_length) {
        t->m6C_count += setupDamagePoly(battleFont, str.m1_string[i] + 0x72, &t->m70_glyphs[t->m6C_count], x, -8);
        i++;
        x += DAMAGE_DIGIT_ADVANCE;
    }
    for (i = 0; i != t->m6C_count; i++) {
        t->m70_glyphs[i].m6_width--;
        t->m70_glyphs[i].m7_height--;
    }
}

/* show up to three damage values on entity's sprite and run battle frames until they are gone */
void displayPassiveModifiers(s32 entity, s32 damage, s32 mpDamage, s32 fuelDamage);
void displayPassiveModifiers(s32 entity, s32 damage, s32 mpDamage, s32 fuelDamage) {
    SpriteActorCore* actor;
    s32 animation;
    sJumpAnimationControlStruct* saved;

    allocateSavePointMeshDataSub0_var0 = 0;
    spriteBytecode2ExtendedE0_Var0 = 1;
    damageDisplayPolysTask2Disabled = 1;
    actor = battleBssBlock.m8C8C_spriteActorCores[entity];
    if (actor != NULL) {
        animation = (s8)actor->mAF_animationId;
        spriteActorSetPlayingAnimation(actor, 10);
        saved = jumpAnimationControlStruct;
        jumpAnimationControlStruct = (sJumpAnimationControlStruct*)1;
        if (damage != 0) {
            createDamageDisplay(actor, damage, 0);
        }
        if (mpDamage != 0) {
            createDamageDisplay(actor, mpDamage, 1);
        }
        if (fuelDamage != 0) {
            createDamageDisplay(actor, fuelDamage, 10);
        }
        jumpAnimationControlStruct = saved;
        while (mecha_battle_op3()) {
            battleRender();
        }
        while ((s8)actor->mAF_animationId == 10) {
            battleRender();
        }
        spriteActorSetPlayingAnimation(actor, animation);
    }
    damageDisplayPolysTask2Disabled = 0;
    freeDamageDisplayPolysTask2();
    allocateSavePointMeshDataSub0_var0 = 0;
    spriteBytecode2ExtendedE0_Var0 = 0;
}

/* format value as `digits` hex digits (glyph base added); Noah: none (hex variant of formatDamageString) */
void formatDamageStringHex(s32 value, sDamageString* str, s32 digits, s32 base);
void formatDamageStringHex(s32 value, sDamageString* str, s32 digits, s32 base) {
    s32 i;
    s32 last;

    for (i = 0; i != digits; i++) {
        last = digits - 1;
        *(str->m1_string + i) = strHexDigits[(value >> ((last - i) * 4)) & 0xF] + base;
    }
    str->m0_length = digits;
}

/* Noah formatDamageString */
void formatDamageString(u32 value, sDamageString* str, s32 digits, u8 leadingZeros, s32 base) {
    s32 length = 0;
    u8* out = str->m1_string;

    if ((s32)value < 0) {
        length = 1;
        str->m1_string[0] = '-';
        out = str->m1_string + 1;
        value = -value;
        digits--;
    }
    while (value %= damageDecimals[digits], digits != 0) {
        digits--;
        if ((value / damageDecimals[digits] & 0xFF) != 0) {
            leadingZeros = 1;
        }
        if (leadingZeros != 0) {
            *out = value / damageDecimals[digits] + base;
            out++;
            length++;
        }
    }
    *out = value + base;
    str->m0_length = length + 1;
}

/* Noah battleRender: one battle frame (double-buffered OT, sprites, camera, input, VSync pacing) */
void battleRender(void) {
    s32 i;
    s32 old;

    battlePreviewBeginFrame();
    portBattleSweepFrame(); /* XG_SWEEP_LOG (port/src/game/battle/sweep.c) */
    {
        /* XG_SWEEP_LOG BR: the last frame's picture camera (Enhanced, 60 Hz oracle) */
        s32 extra[6];
        extra[0] = pcBattle60DrawnEye.vx;
        extra[1] = pcBattle60DrawnEye.vy;
        extra[2] = pcBattle60DrawnEye.vz;
        extra[3] = pcBattle60DrawnAt.vx;
        extra[4] = pcBattle60DrawnAt.vy;
        extra[5] = pcBattle60DrawnAt.vz;
        portBattleSweepPicture(extra, 6);
    }
    portBattleWideTest(); /* XG_BATTLE_WIDETEST (port/src/game/battle/widetest.c) */
    battleRenderCount++;
    checkSoftReboot();
    battleCameraBlock.m48_startOfBattleTime = VSync(-1);
    battleBssBlock.m8C50_currentRenderStruct = (battleBssBlock.m8C50_currentRenderStruct == &battleBssBlock.mB70_renderStructs[0]) ? &battleBssBlock.mB70_renderStructs[1] : &battleBssBlock.mB70_renderStructs[0];
    battleBssBlock.m8C54_currentOT = battleBssBlock.m8C50_currentRenderStruct->m70_ot;
    ClearOTagR(battleBssBlock.m8C54_currentOT, 0x1000);
    battleBssBlock.m8C84_oddOrEven = 1 - battleBssBlock.m8C84_oddOrEven;
    /* runningOnDTL read through its constant address (runningOnDTL): GCC keeps 0x80010000 in a
     * saved register for both tests, as retail does in both versions */
    if (BATTLE_DTL_WORD != -1) {
        pollBattlePadDebug();
        POLLHOST();
        updateBattleDebugger();
    }
    clearShapeTransfertTableEntry(battleBssBlock.m8C84_oddOrEven);
    updateBattleCamera();
    pcBattle60SceneBegin(); /* Enhanced, 60 Hz (b_800B7C28.c) */
    renderBattleScene();
    setCurrentRenderingMatrix(&battleCameraBlock.m20_renderingMatrix);
    setCharacterRenderingOT(battleBssBlock.m8C54_currentOT);
    if (BATTLE_DTL_WORD != -1) {
        printDebugTextOnScreen(battleBssBlock.m8C54_currentOT);
    }
    renderMechasBattle(&battleCameraBlock.m20_renderingMatrix, &battleBssBlock.m8CE4_matrix800CCB94, characterRenderingOT, battleBssBlock.m8C84_oddOrEven);
    STACK_SWITCH(0x1F8003FC);
    uploadCharacterSprites();
    execSpritesCallbacksList1();
    execSpritesCallbacksList2();
    i = spriteCallback2Var4;
    while (--i != -1) {
        updateBattleCamera();
        execSpritesCallbacksList2();
    }
    STACK_RESTORE();
    drawBattleFromModes();
    pcBattle60SceneEnd();
    battleUpdateInputs(0);
    while (--spriteCallback2Var4 != -1) {
        battleUpdateInputs(1);
    }
    battleCameraBlock.m40_drawTiming0 = VSync(1);
    DrawSync(0);
    battleCameraBlock.m44_drawTiming1 = VSync(1);
    spriteCallback2Var4 = VSync(-1) - (u16)battleCameraBlock.m48_startOfBattleTime - (u16)fieldDrawEnvsInitialized;
    if (spriteCallback2Var4 < 0) {
        spriteCallback2Var4 = 0;
    }
    if (spriteCallback2Var4 > 4) {
        spriteCallback2Var4 = 4;
    }
    battleBssBlock.m8DAC_frameRate = spriteCallback2Var4 + fieldDrawEnvsInitialized;
    if (fieldDrawEnvsInitialized == 0) {
        pcBattleVSync();
    } else {
        VSync(fieldDrawEnvsInitialized + 1);
    }
    PutDispEnv(&battleBssBlock.m8C50_currentRenderStruct->m5C_dispEnv);
    PutDrawEnv(&battleBssBlock.m8C50_currentRenderStruct->m0_drawEnv);
    shapeTransfert();
    DrawOTag(&battleBssBlock.m8C50_currentRenderStruct->m70_ot[0xFFF]);
    battlePreviewEndFrame();
    battleLoadFileFrom0xC();
    if (battleRenderCount == 1) {
        if (jumpAnimationControlStruct != NULL) {
            jumpAnimationControlStruct->m8(jumpAnimationControlStruct);
        }
        if (isFxFragmentsSetup != 0) {
            old = isFxFragmentsSetup;
            isFxFragmentsSetup = 0;
            fxFragmentSetup((u16)old);
        }
        if (finalizeBattleMechaLoadingVar0 != 0 && isCDBusy() == 0) {
            finalizeBattleMechaLoadingVar0 = 0;
            clearAnimationsEndOfTurnSub();
        }
    }
    battleRenderCount--;
}

/* Noah battleLoadFileFrom0xC: load the FX fragment file (dir 0xC, file 2 + initFieldVar5) when it changed */
void battleLoadFileFrom0xC(void) {
    s32 directory;
    s32 offset;
    s32 file;

    if (currentFileFrom0xC != initFieldVar5) {
        file = currentFileFrom0xC = initFieldVar5;
        battleIdleDuringLoading();
        getCurrentDirectory(&directory, &offset);
        setCurrentDirectory(0xC, 2);
        if (portOverlayCd()) { /* XG_OVERLAY_CD: the PS1's read, discarded */
            readFile(file + 2, portOverlayScratch(0, getFileSizeAligned(file + 2)), 0, 0x80);
        }
        /* The FX helpers are linked in; disc files 0E/04-08 are five of them,
         * every other slot holds a copy of 0E/02. */
        switch (file + 2) {
        case 4:
            portResetModule("battle_fx_sprite_wobble");
            break;
        case 5:
            portResetModule("battle_fx_model_shatter");
            break;
        case 6:
            portResetModule("battle_fx_tiled_sprite");
            break;
        case 7:
            portResetModule("battle_fx_sprite_band");
            break;
        case 8:
            portResetModule("battle_fx_screen_ripple");
            break;
        default:
            portResetModule("battle_fx_circle_warp");
            break;
        }
        battleIdleDuringLoading();
        setCurrentDirectory(directory, offset);
        DrawSync(0);
        VSync(0);
        EnterCriticalSection();
        FlushCache();
        ExitCriticalSection();
    }
    initFieldVar4 = 1;
}

/* debug-build pad poll (called when running on the DTL; holding 0x100 slows the frame) */
void pollBattlePadDebug(void) {
    updateBattlePadState();
    if (battleBssBlock.m8C58_pad.m0_held & 0x100) {
        VSync(8);
        spriteCallback2Var4 = 0;
    }
}

/* battle pad sampler: held/pressed/released for both pads plus a 4-entry change history */
void updateBattlePadState(void) {
    s32 pad;

    pad = (u16)readPadButtons(0);
    battleBssBlock.m8C58_pad.m4_pressed = ~battleBssBlock.m8C58_pad.m0_held & pad;
    battleBssBlock.m8C58_pad.m8_released = ~pad & battleBssBlock.m8C58_pad.m0_held;
    battleBssBlock.m8C58_pad.m0_held = pad;
    pad = readPadButtons(1);
    battleBssBlock.m8C58_pad.m6_pressed2 = ~battleBssBlock.m8C58_pad.m2_held2 & pad;
    battleBssBlock.m8C58_pad.m2_held2 = pad;
    battleBssBlock.m8C58_pad.mA_released2 = ~pad & battleBssBlock.m8C58_pad.m0_held;
    if (battleBssBlock.m8C58_pad.m0_held != battleBssBlock.m8C58_pad.mC_history[0].m0_held) {
        battleBssBlock.m8C58_pad.mC_history[3] = battleBssBlock.m8C58_pad.mC_history[2];
        battleBssBlock.m8C58_pad.mC_history[2] = battleBssBlock.m8C58_pad.mC_history[1];
        battleBssBlock.m8C58_pad.mC_history[1] = battleBssBlock.m8C58_pad.mC_history[0];
        battleBssBlock.m8C58_pad.mC_history[0].m0_held = battleBssBlock.m8C58_pad.m0_held;
        battleBssBlock.m8C58_pad.mC_history[0].m2_pressed = battleBssBlock.m8C58_pad.m4_pressed;
        battleBssBlock.m8C58_pad.mC_history[0].m4_released = battleBssBlock.m8C58_pad.m8_released;
        battleBssBlock.m8C58_pad.mC_history[0].m6_time = battleCameraBlock.m48_startOfBattleTime;
    }
}

/* Noah initJumpAnimationStuff */
void initJumpAnimationStuff(void);
void initJumpAnimationStuff(void) {
    JumpAnimationStuffVar0 = 0;
    jumpAnimationControlStruct = NULL;
    processBattleAnimationSub0_var0 = 0;
}

/* Noah allocateJumpAnimationStruct */
sJumpAnimationControlStruct* allocateJumpAnimationStruct(void);
sJumpAnimationControlStruct* allocateJumpAnimationStruct(void) {
    jumpAnimationControlStruct = allocateHeap(sizeof(sJumpAnimationControlStruct), 0);
    jumpAnimationControlStruct->m8 = allocateJumpAnimationStructCallback;
    allocateJumpAnimationStructVar0 = 0;
    jumpAnimationControlStruct->m4A = 0;
    jumpAnimationControlStruct->m30 = 0;
    jumpAnimationControlStruct->m48_isStepDone = 1;
    jumpAnimationControlStruct->m2C_jumpPointIndex = 1;
    jumpAnimationControlStruct->m4_spriteCore = NULL;
    jumpAnimationControlStruct->m49_completedMode = 0;
    jumpAnimationControlStruct->m34_pendingSteps = 0;
    setupJumpAnimationStruct1C(0);
    allocateSavePointMeshDataSub0_var0 = 0;
    spriteBytecode2ExtendedE0_Var0 = 1;
    return jumpAnimationControlStruct;
}

/* Noah deleteJumpAnimationControlStruct */
void deleteJumpAnimationControlStruct(void);
void deleteJumpAnimationControlStruct(void) {
    freeHeap(jumpAnimationControlStruct);
    jumpAnimationControlStruct = NULL;
    allocateSavePointMeshDataSub0_var0 = 0;
    spriteBytecode2ExtendedE0_Var0 = 0;
}

/* run mechaPlayAnimation_battle(a, b, c) on a 4 KB heap-allocated stack */
void performMechaPlayAnimation(s32 a, s32 b, s32 c);
void performMechaPlayAnimation(s32 a, s32 b, s32 c) {
    u8* stack = allocateHeap(0x1000, 1);

    STACK_SWITCH(stack + 0xF9C);
    mechaPlayAnimation_battle((u16)a, (u16)b, c);
    STACK_RESTORE();
    freeHeap(stack);
}

/* Noah countBattleActorCoresActiveForBitmask: list the sprite cores in mask, point them at target */
s32 countBattleActorCoresActiveForBitmask(u16 mask, SpriteActorCore** list, SpriteActorCore* target) {
    s32 i;
    s32 count;
    SpriteActorCore* a;

    i = 0;
    count = 0;
    for (; i != 11; i++, mask >>= 1) {
        if (mask & 1) {
            a = battleBssBlock.m8C8C_spriteActorCores[i];
            if (a != NULL) {
                a->m74_targetEntitySprite = target;
                list[count] = a;
                count++;
            }
        }
    }
    list[count] = NULL;
    return count;
}

/* Noah spriteActorCoreRatan2: direction from actor a to actor b */
s16 spriteActorCoreRatan2(SpriteActorCore* a, SpriteActorCore* b) {
    Vec2s16 from;
    Vec2s16 to;

    from.vx = a->m0_position.vx >> 16;
    from.vy = a->m0_position.vz >> 16;
    to.vx = b->m0_position.vx >> 16;
    to.vy = b->m0_position.vz >> 16;
    return vec2dRatan2(to, from);
}

/* Noah getRatan2ToDest: direction from the actor to its destination mA0 */
s16 getRatan2ToDest(SpriteActorCore* actor) {
    Vec2s16 pos;
    Vec2s16 dest;

    pos.vx = actor->m0_position.vx >> 16;
    pos.vy = actor->m0_position.vz >> 16;
    dest.vx = actor->mA0_destination.vx;
    dest.vy = actor->mA0_destination.vz;
    return vec2dRatan2(dest, pos);
}

/* Noah battleAnimationCallback_5-like: switch the jump struct's current actor to entity; returns
 * it (retail callers take it from v0) */
SpriteActorCore* setupBattleAnimationSpriteCore(s32 entity);
SpriteActorCore* setupBattleAnimationSpriteCore(s32 entity) {
    /* one untyped temporary for the control struct and the new actor: JP's register choice in the
     * tail needs the new actor in a pseudo that also lives in the first block */
    void* p = jumpAnimationControlStruct;
    SpriteActorCore* actor = ((sJumpAnimationControlStruct*)p)->m4_spriteCore;

    if (actor != NULL && ((sJumpAnimationControlStruct*)p)->m24_entity != entity) {
        if (battleBssBlock.m4_visualEntities[BATTLE_SLOT(actor)].m3_hidden == 0) {
            spriteActorSetPlayingAnimation(actor, actor->mB0_x0_animationId);
        }
    }
    jumpAnimationControlStruct->m24_entity = entity;
    p = battleBssBlock.m8C8C_spriteActorCores[entity];
    jumpAnimationControlStruct->m4_spriteCore = p;
    return p;
}

/* Noah setupJumpAnimationStruct1C */
void setupJumpAnimationStruct1C(s32 mode) {
    jumpAnimationControlStruct->m1C_mode = mode;
}

/* Noah jumpUpdatePosition: head for the next jump point, or next to the target when the path is done */
void jumpUpdatePosition(SpriteActorCore* actor) {
    s32 anim;

    if (battleBssBlock.mA3C_jumpPoints[jumpAnimationControlStruct->m2C_jumpPointIndex].m0_x == 0xFFFF
        && battleBssBlock.mA3C_jumpPoints[jumpAnimationControlStruct->m2C_jumpPointIndex].m2_z == 0xFFFF) {
        actor->mA0_destination.vx = actor->m0_position.vx >> 16;
        actor->mA0_destination.vy = 0;
        actor->mA0_destination.vz = actor->m0_position.vz >> 16;
        jumpUpdatePositionSub0(actor, actor->m74_targetEntitySprite);
    } else {
        actor->mA0_destination.vx = battleBssBlock.mA3C_jumpPoints[jumpAnimationControlStruct->m2C_jumpPointIndex].m0_x;
        actor->mA0_destination.vz = battleBssBlock.mA3C_jumpPoints[jumpAnimationControlStruct->m2C_jumpPointIndex].m2_z;
        actor->mA0_destination.vy = 0;
        anim = 2;
        if (battleBssBlock.mA3C_jumpPoints[jumpAnimationControlStruct->m2C_jumpPointIndex].m4_flags != 0) {
            anim = 3;
        }
        jumpUpdatePositionSub1(actor, anim);
        jumpAnimationControlStruct->m2C_jumpPointIndex++;
    }
}

/* Noah jumpUpdatePositionSub1: face the destination and start moving with anim */
void jumpUpdatePositionSub1(SpriteActorCore* actor, s32 anim) {
    Vec2s16 pos;
    Vec2s16 dest;

    pos.vx = actor->m0_position.vx >> 16;
    pos.vy = actor->m0_position.vz >> 16;
    dest.vx = actor->mA0_destination.vx;
    dest.vy = actor->mA0_destination.vz;
    jumpAnimationControlStruct->m44_distanceToTarget = computerJumpDistanceToTarget(pos, dest);
    spriteActorSetDirection(actor, getRatan2ToDest(actor));
    setSpriteActorAngle(actor, getRatan2ToDest(actor));
    spriteActorSetPlayingAnimation(actor, anim);
    setupJumpAnimationStruct1C(6);
}

/* Noah startReadingBattleJumpAnimation */
void startReadingBattleJumpAnimation(SpriteActorCore* actor);
void startReadingBattleJumpAnimation(SpriteActorCore* actor) {
    s32 file;
    void* buf;

    clearBattleAnimationData();
    setCurrentDirectory(0x2C, 1);
    file = actor->m7C_extra->m0;
    buf = allocateHeap(getFileSizeAligned(file), 1);
    readFile(file, buf, 0, 0x80);
    battleAnimationLoadingDest = buf;
    startReadingBattleJumpAnimationVar0 = BATTLE_SLOT(actor);
}

/* relocate the loaded battle animation (battleAnimationLoadingDest) once; returns loadEffectFragmentsAndAudio's result (unset if already done) */
u8* loadFragmentIsNotLoadedYet(void);
u8* loadFragmentIsNotLoadedYet(void) {
    u8* r;

    if (fxFragmentLoaded == 0) {
        r = loadEffectFragmentsAndAudio(battleAnimationLoadingDest);
        fxFragmentLoaded = 1;
    }
    return r;
}

/* undo loadFragmentIsNotLoadedYet */
void clearFxFragment(void);
void clearFxFragment(void) {
    if (fxFragmentLoaded != 0) {
        clearFragmentSeq(battleAnimationLoadingDest);
        fxFragmentLoaded = 0;
    }
}

/* Noah setupBattleAnimationSpriteCore */
void getSpriteCoreForAnimation(SpriteActorCore* actor);
void getSpriteCoreForAnimation(SpriteActorCore* actor) {
    SpriteActorCore* target;
    s32 slot;

    currentJumpAnimationBitMask = battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m16_targetBitMask;
    numActiveSpriteCores = countBattleActorCoresActiveForBitmask(battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m16_targetBitMask, pSpriteCoreListHead, actor);
    if (numActiveSpriteCores == 0) {
        pSpriteCoreListHead[0] = actor;
    }
    target = pSpriteCoreListHead[0];
    actor->m74_targetEntitySprite = target;
    target->m74_targetEntitySprite = actor;
    slot = BATTLE_SLOT(target);
    jumpAnimationControlStruct->m4C_target = pSpriteCoreListHead[0];
    jumpAnimationControlStruct->m28_targetSlot = slot;
    setSpriteActorAngle(actor, spriteActorCoreRatan2(actor, actor->m74_targetEntitySprite));
    if ((s8)target->mAF_animationId != 0x15) {
        setSpriteActorAngle(target, spriteActorCoreRatan2(actor->m74_targetEntitySprite, actor));
    }
}

/* Noah jumpUpdatePositionSub0: at the end of the jump path, move next to the target (or attack if already there) */
void jumpUpdatePositionSub0(SpriteActorCore* actor, SpriteActorCore* target) {

    if (battleBssBlock.mA3C_jumpPoints[jumpAnimationControlStruct->m2C_jumpPointIndex].m0_x == 0xFFFF
        && battleBssBlock.mA3C_jumpPoints[jumpAnimationControlStruct->m2C_jumpPointIndex].m2_z == 0xFFFF) {
        actor->m0_position.vx = actor->mA0_destination.vx << 16;
        actor->m0_position.vz = actor->mA0_destination.vz << 16;
        if ((target->m0_position.vx >> 16) > (actor->m0_position.vx >> 16)) {
            actor->mA0_destination.vx = (target->m0_position.vx >> 16) - 0x50;
        } else {
            actor->mA0_destination.vx = (target->m0_position.vx >> 16) + 0x50;
        }
        actor->mA0_destination.vz = target->m0_position.vz >> 16;
        actor->mA0_destination.vy = 0;
        if (actor->mA0_destination.vx == (actor->m0_position.vx >> 16) && actor->mA0_destination.vz == (actor->m0_position.vz >> 16)) {
            battleAnimationCallback_2(actor, target);
        } else {
            jumpUpdatePositionSub1(actor, 3);
            setupJumpAnimationStruct1C(2);
        }
    } else {
        jumpUpdatePosition(actor);
    }
}

/* sprite callback: count finished animations */
void abilityPlayAnimationSub0(void) {
    abilityPlayAnimationVar0++;
}

/* play the actor's ability animation (mAF) while running abilityPlayAnimationSub1(value), waiting for it to finish */
void abilityPlayAnimation(s32 value, SpriteActorCore* actor);
void abilityPlayAnimation(s32 value, SpriteActorCore* actor) {
    if (actor->m48_defaultAnimationBundle == NULL) {
        abilityPlayAnimationSub1(value);
        return;
    }
    abilityPlayAnimationVar0 = 0;
    if ((s8)actor->mAF_animationId != 0) {
        spriteActorSetCallback(actor, (void (*)(SpriteActorCore*))abilityPlayAnimationSub0);
        spriteActorSetPlayingAnimation(actor, (s8)actor->mAF_animationId);
    }
    abilityPlayAnimationSub1(value);
    if ((s8)actor->mAF_animationId != 0) {
        while (abilityPlayAnimationVar0 == 0) {
            battleRender();
        }
        spriteActorSetCallback(actor, NULL);
    }
}

/* Noah: create all damage displays while a jump animation is running */
void createAllDamageDisplaysIfAnimating(void) {
    if (jumpAnimationControlStruct != NULL) {
        createAllDamageDisplays();
    }
}

/* Noah isDamageDisplayPolysTask2Running-based counter: allocateSavePointMeshDataSub0_var0 - (damage polys task running) */
s32 mecha_battle_op3(void) {
    return allocateSavePointMeshDataSub0_var0 - isDamageDisplayPolysTask2Running();
}

/* Noah isDamageDisplayPolysTask2Running */
s32 isDamageDisplayPolysTask2Running(void) {
    return pDamageDisplayPolysTask2 != NULL;
}

/* Noah: set needFxFragments */
void setNeedFxFragments(SpriteActorCore* actor);
void setNeedFxFragments(SpriteActorCore* actor) {
    needFxFragments = actor;
}

/* distance watch task update: fire the callback when the distance grows or drops below the minimum */
void updateSpriteDistanceWatch(TaskHeader* task) {
    BattleDistanceWatchTask* t = (BattleDistanceWatchTask*)task;
    s32 old = t->m24_distance;

    t->m24_distance = battleSpriteOpcode_FB_sub0(t->m1C_actor);
    if (old < t->m24_distance || t->m24_distance < t->m28_minDistance) {
        t->m2C_callback(t->m1C_actor);
        t->m0_task.mC_deleteCallback(&t->m0_task);
    }
}

/* create the distance watch task for actor */
void startSpriteDistanceWatch(SpriteActorCore* actor, s32 minDistance, void (*callback)(SpriteActorCore*));
void startSpriteDistanceWatch(SpriteActorCore* actor, s32 minDistance, void (*callback)(SpriteActorCore*)) {
    BattleDistanceWatchTask* t = (BattleDistanceWatchTask*)allocateUpdateTask(actor->m6C_owner, (s32)sizeof(BattleDistanceWatchTask) - (s32)sizeof(TaskHeader));

    setTaskUpdateFunction(&t->m0_task, updateSpriteDistanceWatch);
    t->m2C_callback = callback;
    t->m1C_actor = actor;
    t->m20_animationId = (s8)actor->mAF_animationId;
    t->m24_distance = battleSpriteOpcode_FB_sub0(actor);
    t->m28_minDistance = minDistance;
    actor->mAC_x5 = 1;
}

/* make entity's sprite the current attacker aimed at target's sprite (single-entry target list) */
void setupMechaForEventSub1(s32 entity, s32 target);
void setupMechaForEventSub1(s32 entity, s32 target) {
    SpriteActorCore* actor = battleBssBlock.m8C8C_spriteActorCores[entity];

    if (actor != NULL) {
        processBattleAnimationSub0_var1 = actor;
        actor->m74_targetEntitySprite = battleBssBlock.m8C8C_spriteActorCores[target];
        currentJumpAnimationBitMask = 1 << target;
        pSpriteCoreListHead[0] = battleBssBlock.m8C8C_spriteActorCores[target];
        pSpriteCoreListHead[1] = NULL;
    }
}

/* retarget the actor to the next sprite in the target list (first one if its target is not listed) */
void retargetToNextSprite(SpriteActorCore* actor);
void retargetToNextSprite(SpriteActorCore* actor) {
    s32 i;

    for (i = 0; i != numActiveSpriteCores; i++) {
        if (pSpriteCoreListHead[i] == actor->m74_targetEntitySprite) {
            break;
        }
    }
    if (i >= numActiveSpriteCores) {
        actor->m74_targetEntitySprite = pSpriteCoreListHead[0];
    } else {
        actor->m74_targetEntitySprite = pSpriteCoreListHead[i + 1];
    }
}

/* index of actor in the target list (numActiveSpriteCores if absent) */
s32 findSpriteInTargetList(SpriteActorCore* actor);
s32 findSpriteInTargetList(SpriteActorCore* actor) {
    s32 i;

    for (i = 0; i != numActiveSpriteCores; i++) {
        if (pSpriteCoreListHead[i] == actor) {
            break;
        }
    }
    return i;
}

/* count a hit; on the second one start sound 0xB */
void signalEffectMechaEvent(void);
void signalEffectMechaEvent(void) {
    effectMechaEventCount++;
    pendingEffectMechaEvents++;
    if (effectMechaEventCount == 2) {
        freeMecha_battle(0xB);
    }
}

/* reload battle texture file (dir 0x2C, file 1) into VRAM when battleTextureReloadPending is set */
void updateBattleAnimationDataLoading(void);
void updateBattleAnimationDataLoading(void) {
    s32* data;

    if (battleTextureReloadPending != 0) {
        battleIdleDuringLoading();
        setCurrentDirectory(0x2C, 0);
        data = allocateHeap(getFileSizeAligned(1), 0);
        readFile(1, data, 0, 0x80);
        battleIdleDuringLoading();
        uploadTextureToVram(data, 0, 0, 0, 0, 0, 0);
        battleRender();
        freeHeap(data);
        battleTextureReloadPending = 0;
    }
}

/* restart the party's gear sprites (all, or all but the current actor's when battleAnimGearUnloadMode == 2) and wait */
void battleRestartReservedMechaLoads(void);
void battleRestartReservedMechaLoads(void) {
    s32 i;
    s32 slot;

    if (battleAnimGearUnloadMode != 0) {
        waitBattleAnimationSpriteLoading();
        battleRender();
        battleRender();
        slot = BATTLE_SLOT(processBattleAnimationSub0_var1);
        for (i = 0; i != 3; i++) {
            if (battleBssBlock.m4_visualEntities[i].m4_isGear != 0 && (battleAnimGearUnloadMode != 2 || i != slot)
                && battleBssBlock.m4_visualEntities[i].m2_characterId < 0x11) {
                createMechaLoadingTask(i);
            }
        }
        while (mainBattleSpriteCallback_phase5Var != 0) {
            battleRender();
        }
        battleAnimGearUnloadMode = 0;
    }
}

/* load the battle sound bank (dir 0x2C, file 5) unless already loaded */
void waitBattleAnimationSoundLoaded(void);
void waitBattleAnimationSoundLoaded(void) {
    u8* data;

    if (battleAnimationSoundLoaded == 0) {
        battleIdleDuringLoading();
        setCurrentDirectory(0x2C, 0);
        data = allocateHeap(getFileSizeAligned(5), 0);
        readFile(5, data, 0, 0x80);
        battleIdleDuringLoading();
        if (findInWdsLinkedList(*(u16*)(data + 0x20)) == 0) {
            loadWdsDataIfNeeded();
            loadWdsDataNeeded = loadWds((Wds*)data, 0);
            while ((s16)waitForMusic(0) != 0) {
                battleRender();
            }
            battleAnimationSoundLoaded = 1;
            startJumpAnimationVar3 = 0;
        }
        freeHeap(data);
    }
}
