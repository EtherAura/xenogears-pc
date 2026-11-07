/* Battle loader: battle sprite VRAM and sprite actors (Noah
 * battleSpriteLoader.cpp). 0x801E62E0-0x801E70E8; gcc-2.7.2-cdk, ASPSX 2.56. */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/task.h"
#include "main/filesystem.h"
#include "main/sprite.h"
#include "stack.h"
/* field_mecha/mecha.h (via battle/battle.h) declares the gear overlay's own
 * mainBattleSpriteCallback_phase3; this overlay has a different function at that address. */
#define mainBattleSpriteCallback_phase3 fieldMecha_func_801E6E48
#include "battle/battle.h"
#undef mainBattleSpriteCallback_phase3
#include "battle_loader/battle_loader.h"

/* main */
extern u8 battleScriptLoaderTaskCreationInProgress;                   /* task allocation heap flag (Noah: battleScriptLoaderTaskCreationInProgress) */
TaskHeader* allocateUpdateTask(TaskHeader* owner, s32 size);
void freeUpdateTask(TaskHeader* task);
void registerSpriteCallback2Sub0(TaskHeader* task);
void allocateSavePointMeshDataSub0_callback(TaskHeader* task);
void clearShapeTransferEntry(void* data);
s32 addClamp255(s32 value, s32 delta);
void resetSpriteCallbacks(void);
void checkSoftReboot(void);
void startBattleLoadingDuringEffect2(void);
s32 waitReadCompletion(s32 mode);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
s32 getAngleCos(s32 angle);           /* cos */
s32 getAngleSin(s32 angle);           /* sin */
s32 uploadTextureToVram(s32* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY);
extern OTag* characterRenderingOT;
extern SpriteVramData createSavePointMeshData_mode5;
void initFieldEntitySub4Sub5Sub0(SpriteVramData* vram, void* bundle, Vec2s16 vramLocation, Vec2s16 clut, s32 unused);
void loadSequence(void* data);
u32 doPointerRelocation(u32* data);
s32 getSpriteNumVerticalStrides(s32* p);
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animation);
void spriteActorSetDirection(SpriteActorCore* actor, s32 direction);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
void setFixedXZ(FP_VEC3* v, s32 x, s32 z);

/* battle */
extern s32 mainBattleSpriteCallback_phase5Var;
void createMechaLoadingTask(s32 entity);
void setBattleMechaVar2(void);
void battleLoaderTick(u8 step);            /* battle loader: Noah battleLoaderTick */
extern void* battleSpriteLoadTempBuffer;                /* battle sprite data (copy of config file 3's sprite data) */
void mechaInitEnvironmentMechaMesh(s32 entryId, u16 flags, void* pData2, void* pData1, s16 tpageX, s16 tpageY, s16 clutX, s16 clutY, SVECTOR* pPosition);
void initMechaForBattle(u32 entity);
extern s16 battleEnvTextureRectX;                  /* battle env texture rect x (Noah: battleEnvTextureRect*) */
extern s16 battleEnvTextureRectY;                  /* ... y */
extern s16 battleEnvTextureRectW;                  /* ... w */
extern s16 battleEnvTextureRectH;                  /* ... h */
void sampleBattleTerrainHeight(SpriteActorCore* core);
extern u8 currentBattleLoaderTransitionEffect;

/* this overlay */
extern s32* battleLoaderTexture;
extern OTag* battleTileEffectOT;                /* tile transition OT */
extern OTag* battleWaveEffectOT;                /* wave transition OT */
extern u8 isBattleWaveEffectMode3;
extern SVECTOR battleWaveTriVertsTopLeft[3];           /* wave triangle, first half */
extern SVECTOR battleWaveTriVertsBottomRight[3];           /* wave triangle, second half */
extern s32 battlePartyFileMapping[14][2];
extern u8 isBattleTileEffectMode4;
extern SVECTOR battleTileShardVertsTopLeft[3];           /* tile triangle, first half */
extern SVECTOR battleTileShardVertsBottomRight[3];           /* tile triangle, second half */
extern s16 battleSpriteX;
extern u8 battleSpriteWidthPerCharacter[12];

void loadBattleSpriteVram(sBattleSpriteConfigs* config);
void createEnemiesBattleSprites(void* config);
void createBattleSpriteActor(u32 entity, s32 visualBufferIndex, s32 animationId);
void setupPlayerSpriteLoadingCommands(LoadingBatchCommand* commands);
void startPlayableCharactersJumpToPosition(void);
void createBattlePlayerSpriteActors(void);
void mainBattleSpriteCallback_phase6(TaskHeader* task);
void mainBattleSpriteCallback_phase5(TaskHeader* task);
void mainBattleSpriteCallback_phase4(TaskHeader* task);
void uploadBattleLoaderTexture(void);
void mainBattleSpriteCallback_phase3(TaskHeader* task);
void mainBattleSpriteCallback_phase2(TaskHeader* task);
void mainBattleSpriteCallback_phase1(TaskHeader* task);
void createBattleSpriteLoadingTaskSub(void* config);
void renderBattleTileEffect(TaskHeader* task);
sBattleTileEffect* initBattleTileEffect(sBattleTileEffect* e);
void battleLoaderUpdateMode1(void);
sBattleTileEffect* createBattleTileEffect(void);
void freeBattleTileEffect(void* p);
void updateBattleTileEffect(TaskHeader* task);
sBattleWaveEffect* initBattleWaveEffect(sBattleWaveEffect* e);

void createBattleSpriteLoadingTask(void* config);
void createBattleSpriteLoadingTask(void* config) {
    battleScriptLoaderTaskCreationInProgress = 1;
    createBattleSpriteLoadingTaskSub(config);
    battleScriptLoaderTaskCreationInProgress = 0;
}

void loadBattleSpriteVram(sBattleSpriteConfigs* config) {
    struct {
        u16 m0[16];
        s32 m20[14];
    } ids;
    sBattleSpriteConfigEntry* entry;
    s32 n;
    s32 k;
    s32 currentSpriteSlot;
    s32 currentSlot;
    sptr base;
    sBattleSpriteConfigEntry* other;
    s32 i;
    s32 j;
    s32 headerSize;
    s32 currentSpriteX;
    u32 spriteSlot;
    s32 numUsedEntries;
    s32 searchedValue;
    s32 iDest;
    s32 slot;
    s32 tmp;

    currentSpriteSlot = 0;
    currentSlot = 3;
    /* the counter shares currentSpriteX's variable (retail keeps both in s4) */
    for (currentSpriteX = 0; currentSpriteX != 11; currentSpriteX++) {
        ids.m20[currentSpriteX] = battleBssBlock.m4_visualEntities[currentSpriteX].m2_characterId;
    }
    n = config->m0_numEntities;
    currentSpriteX = config->m1_vramColumn * 0x40 + 0x140;
    headerSize = n * 0xC + 8;
    battleSpriteLoadTempBuffer = allocateHeap(config->m4_size - headerSize, 0);
    memcpy(battleSpriteLoadTempBuffer, (u8*)config + headerSize, config->m4_size - headerSize);
    entry = config->m8_entries;
    base = (sptr)battleSpriteLoadTempBuffer - headerSize;
    for (k = 0; k != n; k++) {
        if (entry->m8_isMecha != 0) {
            iDest = 0;
            if (entry->m4_spriteDataOffset < 8) {
                continue;
            }
            searchedValue = currentSlot - 3;
            battleBssBlock.m8D24_visualBuffers[currentSlot].m0_spriteData = NULL;
            currentSpriteX += 0x40;
            for (slot = 3, numUsedEntries = 0; slot != 11; slot++) {
                if (searchedValue == ids.m20[slot]) {
                    numUsedEntries++;
                }
            }
            for (slot = k + 1, other = entry + 1; slot != n; slot++, other++) {
                if (other->m8_isMecha && other->m4_spriteDataOffset == k) {
                    for (j = 3; j != 11; j++) {
                        if (slot == ids.m20[j]) {
                            ids.m20[j] = searchedValue;
                            numUsedEntries++;
                        }
                    }
                }
            }
            for (i = 3; i != 11; i++) {
                s32 e;
                if (ids.m20[i] < 8 && ids.m20[i] == searchedValue && (e = i) != 0) {
                    /* flags live in the loop counter `slot`: its unknown upper bits keep
                     * the u16 argument's andi 0xFFFF */
                    slot = -((numUsedEntries < 2) ^ 1) & 2;
                    if (iDest != 0) {
                        slot = 7;
                        if (numUsedEntries == iDest + 1) {
                            slot = 5;
                        }
                    }
                    slot |= 0x80;
                    tmp = iDest - 0x1C0;
                    mechaInitEnvironmentMechaMesh(e, slot, (void*)(entry->m0_spriteControlOffset + base), (void*)(entry->m4_spriteDataOffset + (sptr)config),
                                  currentSpriteX - 0x40, 0x100, 0, e - tmp, NULL);
                    iDest++;
                    initMechaForBattle(e);
                }
            }
        } else {
            if (entry->m4_spriteDataOffset < 8) {
                spriteSlot = entry->m4_spriteDataOffset;
            } else {
                uploadNpcSpriteSheet((s32*)(entry->m4_spriteDataOffset + (sptr)config), currentSpriteX, 0x100);
                spriteSlot = currentSpriteSlot++;
                ids.m0[spriteSlot] = currentSpriteX;
                currentSpriteX += getSpriteNumVerticalStrides((s32*)(entry->m4_spriteDataOffset + (sptr)config)) << 6;
                if (currentSpriteX > 0x2C0) {
                    currentSpriteX = 0;
                }
            }
            battleBssBlock.m8D24_visualBuffers[currentSlot].m0_spriteData = (void*)(entry->m0_spriteControlOffset + base);
            battleBssBlock.m8D24_visualBuffers[currentSlot].m6_vramY = 0x100;
            battleBssBlock.m8D24_visualBuffers[currentSlot].m4_vramX = ids.m0[spriteSlot];
            battleBssBlock.m8D24_visualBuffers[currentSlot].m8_vramParam = entry->mB_vramParam;
        }
        entry++;
        currentSlot++;
    }
}

void createEnemiesBattleSprites(void* config) {
    s32 i;

    for (i = 3; i != 11; i++) {
        s32 index = battleBssBlock.m4_visualEntities[i].m2_characterId;
        if (index < 8) {
            index += 3;
            if (battleBssBlock.m8D24_visualBuffers[index].m0_spriteData != NULL) {
                createBattleSpriteActor(i, index, 1);
            }
        }
    }
}

void createBattleSpriteActor(u32 entity, s32 visualBufferIndex, s32 animationId) {
    sBattleSpriteActor* sprite;
    SpriteActorCore* core;
    s16 direction;

    sprite = allocateBattleSpriteActor(battleBssBlock.m8D24_visualBuffers[visualBufferIndex].m0_spriteData, 0, entity + 0x1C0,
                           battleBssBlock.m8D24_visualBuffers[visualBufferIndex].m4_vramX, battleBssBlock.m8D24_visualBuffers[visualBufferIndex].m6_vramY,
                           0x20, 0, 0, 0, animationId, 0, 0, 0, battleBssBlock.m8D24_visualBuffers[visualBufferIndex].m8_vramParam);
    core = sprite->m0_updateTask.m4_data;
    core->m24_vramData->m4_vramLocation.vy = battleBssBlock.m8D24_visualBuffers[visualBufferIndex].m6_vramY;
    core->m24_vramData->m4_vramLocation.vx = battleBssBlock.m8D24_visualBuffers[visualBufferIndex].m4_vramX;
    core->m7C_extra->mE_vramLocation = core->m24_vramData->m4_vramLocation;
    battleBssBlock.m8C8C_spriteActorCores[entity] = sprite->m0_updateTask.m4_data;
    battleBssBlock.m8CB8_spriteActors[entity] = sprite;
#if VERSION_JP
    core->mAC_x0 = entity;              /* JP: the id is one 4-bit field */
#else
    core->mA8_x1E = entity;
    core->mAC_x0 = entity >> 2;
#endif
    setFixedXZ(&core->m0_position, (s16)battleBssBlock.m4_visualEntities[entity].mA_X, (s16)battleBssBlock.m4_visualEntities[entity].mC_Z);
    direction = (battleBssBlock.m4_visualEntities[entity].m6_direction != 0) << 11;
    setSpriteActorAngle(core, direction);
    spriteActorSetDirection(core, direction);
    if (battleBssBlock.m4_visualEntities[entity].m3_hidden != 0) {
        core->m9E_wait = 0;
    }
}

void setupPlayerSpriteLoadingCommands(LoadingBatchCommand* commands) {
    s32 i;
    s32 n;

    setCurrentDirectory(0x2C, 1);
    i = 0;
    n = 0;
    for (; i != 3; i++) {
        s32 id = battleBssBlock.m4_visualEntities[i].m2_characterId;
        if (id < 0x11 && battleBssBlock.m4_visualEntities[i].m4_isGear == 0) {
            s32 fileIndex = battlePartyFileMapping[id][0];
            commands[n].m0_fileIndex = fileIndex;
            commands[n].m4_loadPtr = allocateHeap(getFileSizeAligned(fileIndex), 0);
            battleBssBlock.m8D24_visualBuffers[i].m0_spriteData = commands[n].m4_loadPtr;
            battleBssBlock.m8D24_visualBuffers[i].m8_vramParam = 0;
            n++;
        }
    }
    commands[n].m0_fileIndex = 0;
    commands[n].m4_loadPtr = NULL;
    batchStartLoadingFiles(commands, 0, 0);
}

void startPlayableCharactersJumpToPosition(void) {
    s32 i;

    for (i = 0; i != 3; i++) {
        s32 id = battleBssBlock.m4_visualEntities[i].m2_characterId;
        if (id < 0x11 && battleBssBlock.m4_visualEntities[i].m4_isGear != 0) {
            createMechaLoadingTask(i);
        }
    }
}

void createBattlePlayerSpriteActors(void) {
    s32 i;
    SpriteActorCore* core;

    for (i = 0; i != 3; i++) {
        s32 id = battleBssBlock.m4_visualEntities[i].m2_characterId;
        if (id < 0x11 && battleBssBlock.m4_visualEntities[i].m4_isGear == 0) {
            battleBssBlock.m8D24_visualBuffers[i].m6_vramY = 0x1C0;
            battleBssBlock.m8D24_visualBuffers[i].m4_vramX = battleSpriteX + 0x100;
            battleSpriteX += battleSpriteWidthPerCharacter[id];
            createBattleSpriteActor(i, i, 1);
            core = battleBssBlock.m8C8C_spriteActorCores[i];
            core->m7C_extra->m0 = battlePartyFileMapping[id][1];
            freeHeap(core->m20_object->m2C_subSpriteBuffer);
            core->m20_object->m2C_subSpriteBuffer = allocateHeap(0x300, 0);
        }
    }
    if (currentBattleLoaderTransitionEffect == 0) {
        for (i = 0; i != 3; i++) {
            core = battleBssBlock.m8C8C_spriteActorCores[i];
            if (core != NULL) {
                sampleBattleTerrainHeight(core);
                core->mA0_destination.vx = core->m0_position.vx >> 16;
                core->mA0_destination.vy = core->m0_position.vy >> 16;
                core->mA0_destination.vz = core->m0_position.vz >> 16;
                spriteActorSetPlayingAnimation(core, 0x17);
            }
        }
    }
}

void mainBattleSpriteCallback_phase6(TaskHeader* task) {
    sBattleSpriteLoaderTask* p = (sBattleSpriteLoaderTask*)task;
    s32 i;

    if (p->m90_frameToDeleteCount == 0) {
        for (i = 0; i != 3; i++) {
            if (battleBssBlock.m4_visualEntities[i].m4_isGear == 0) {
                SpriteActorCore* core = battleBssBlock.m8C8C_spriteActorCores[i];
                if (core != NULL && (core->m0_position.vy >> 16) != core->m84_maxY) {
                    return;
                }
            }
        }
        battleBssBlock.m8DA8_timeEnabled = 1;
        freeUpdateTask(task);
    } else {
        p->m90_frameToDeleteCount--;
    }
}

void mainBattleSpriteCallback_phase5(TaskHeader* task) {
    if (mainBattleSpriteCallback_phase5Var == 0) {
        ((sBattleSpriteLoaderTask*)task)->m90_frameToDeleteCount = 0x10;
        setTaskUpdateFunction(task, mainBattleSpriteCallback_phase6);
    }
}

void mainBattleSpriteCallback_phase4(TaskHeader* task) {
    if ((s16)waitForMusic(0) == 0) {
        freeHeap(((sBattleSpriteLoaderTask*)task)->m28_texture);
        startPlayableCharactersJumpToPosition();
        setTaskUpdateFunction(task, mainBattleSpriteCallback_phase5);
    }
}

/* Uploads the battle texture battleLoaderTexture on a scratch stack (Noah: MissingCode in phase3) */
void uploadBattleLoaderTexture(void) {
    u8* stack = allocateHeap(0x2000, 1);
    STACK_SWITCH(stack + 0x1F00);
    uploadTextureToVram(battleLoaderTexture, 0, 0, 0, 0, 0, 0);
    DrawSync(0);
    STACK_RESTORE();
    freeHeap(stack);
}

void mainBattleSpriteCallback_phase3(TaskHeader* task) {
    sBattleSpriteLoaderTask* p = (sBattleSpriteLoaderTask*)task;
    Vec2s16 vramLocation;
    Vec2s16 clut;

    if (isCDBusy() == 0) {
        battleLoaderTexture = p->m28_texture;
        uploadBattleLoaderTexture();
        vramLocation.vx = 0x380;
        vramLocation.vy = 0;
        clut.vx = 0;
        clut.vy = 0x1D1;
        initFieldEntitySub4Sub5Sub0(&createSavePointMeshData_mode5, p->m24_spriteBundle, vramLocation, clut, 0);
        loadSequence(p->m2C_seq);
        gBattleCurrentSoundEffectSeq = p->m2C_seq;
        setTaskUpdateFunction(task, mainBattleSpriteCallback_phase4);
        setBattleMechaVar2();
    }
}

void mainBattleSpriteCallback_phase2(TaskHeader* task) {
    sBattleSpriteLoaderTask* p = (sBattleSpriteLoaderTask*)task;
    LoadingBatchCommand* c;
    s32 busy = isCDBusy();

    setCurrentDirectory(0x2C, 0);
    if (busy == 0) {
        createBattlePlayerSpriteActors();
        c = p->m50_loadingCommandsPhase2;
        p->m28_texture = c[0].m4_loadPtr = allocateHeap(getFileSizeAligned(1), 1);
        c[0].m0_fileIndex = 1;
        battleSpriteBundle = p->m24_spriteBundle = c[1].m4_loadPtr = allocateHeap(getFileSizeAligned(2), 0);
        c[1].m0_fileIndex = 2;
        p->m2C_seq = c[2].m4_loadPtr = allocateHeap(getFileSizeAligned(3), 0);
        c[2].m0_fileIndex = 3;
        c[3].m4_loadPtr = NULL;
        c[3].m0_fileIndex = 0;
        batchStartLoadingFiles(c, 0, 0);
        setTaskUpdateFunction(task, mainBattleSpriteCallback_phase3);
    }
}

void mainBattleSpriteCallback_phase1(TaskHeader* task) {
    sBattleSpriteLoaderTask* p = (sBattleSpriteLoaderTask*)task;

    if (isCDBusy() == 0) {
        u8* stack = allocateHeap(0x4000, 1);
        STACK_SWITCH(stack + 0x3FFC);
        loadBattleSpriteVram(p->m20_spriteConfig);
        createEnemiesBattleSprites(p->m20_spriteConfig);
        STACK_RESTORE();
        freeHeap(stack);
        DrawSync(0);
        freeHeap(p->m20_spriteConfig);
        setTaskUpdateFunction(task, mainBattleSpriteCallback_phase2);
        setupPlayerSpriteLoadingCommands(p->m30_loadingCommands);
    }
}

void createBattleSpriteLoadingTaskSub(void* config) {
    sBattleSpriteLoaderTask* task = (sBattleSpriteLoaderTask*)allocateUpdateTask(NULL, (s32)sizeof(sBattleSpriteLoaderTask) - (s32)sizeof(TaskHeader));
    setTaskUpdateFunction(&task->m0_task, mainBattleSpriteCallback_phase1);
    task->m20_spriteConfig = config;
}
