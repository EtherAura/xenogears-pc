/*
 * Player/gear/Yggdrasil controls, cameras, fader, exits and exit dialogs, ground
 * height/material queries (Noah: worldmapPlayer.cpp, worldmapPlayerGear.cpp,
 * worldmapTaskCamera.cpp, worldmapTaskWorldCamera.cpp, worldmapTaskFader.cpp,
 * worldmapExit.cpp, worldmapTaskYggdrasil.cpp).
 * Provisional file (bounds from the split). gcc-2.7.2-psx -O2 -G0, ASPSX 2.56
 * --expand-div (the preset); US and JP build from this C.
 */
#include "common.h"
#include "psyq.h"
#include "pc_camera.h"
#include "main/dialog.h"
#include "worldmap/worldmap.h"

extern s32 worldmapFadeRunning; /* Fade transition active flag. */

extern s32 worldmapFadePolyStep; /* Brightness change per frame. */


extern sWorldmapState* gWorldmapState;
extern s32 worldMapGearMode;
extern s32 worldmapGeometryOffsetY;                      /* eased value 0x78..0x8C (task worldmap_taskMinimapY_init/worldmap_taskMinimapY_update) */
extern u16 worldmapInput1_0;
extern u16 worldmapInput2_0;
extern u8 adjustLocationAfterCollisionVar0;
extern s32 worldmapExitVar2;
extern s32 worldmapGearToggleHeld;
extern s32 worldmapGearTogglePrevHeld;
extern s32 worldmapGearTogglePressed;
extern DialogWindow18 worldmapExitDialogWindow;
extern DialogWindow18 worldmapInfoDialogWindow;
extern s16 worldmapVehicleGroundTypeTable[];                    /* Noah: SHORT_ARRAY_worldmap__8009bac8 [8][8] */
extern s32 worldmapSizeX;
extern s32 worldmapSizeY;
extern u8* worldmapChunks[256];
#ifdef PLATFORM_PC
/* A chunk that is not loaded (NULL): the PS1 reads low RAM through it; the PC
 * reads a zero chunk instead. */
static u8 pcNoWorldmapChunk[0x710];
#define WM_CHUNK(id) (worldmapChunks[id] != NULL ? worldmapChunks[id] : pcNoWorldmapChunk)
#else
#define WM_CHUNK(id) worldmapChunks[id]
#endif
extern VECTOR worldmapRadarPosition;
extern s16 worldmapPlayerDirection;                      /* Noah: worldmapVar_8009d52c */
extern SVECTOR worldmapRotation;
extern VECTOR worldmapMaterialSplitOrigins[16];                 /* Noah: VECTOR_ARRAY_worldmap__8009b464 [16] */
extern VECTOR worldmapMaterialSplitNormals[16];                 /* Noah: VECTOR_ARRAY_worldmap__8009b364 [16] */
extern VECTOR worldmapMaterialWallDirections[16];                 /* Noah: VECTOR_ARRAY_worldmap__8009b264 [16] */
extern VECTOR worldmapCellAntiDiagonalNormal;                   /* Noah: VECTOR_worldmap__8009b244 {0xB50,0,-0xB50} */
extern VECTOR worldmapCellDiagonalNormal;                   /* Noah: VECTOR_worldmap__8009b254 {0xB50,0,0xB50} */
extern s32 worldmapWaterWave;
extern s32 worldmapWaterWave2;
extern s32 worldmapCameraVar0;                      /* Noah: worldmapCameraVar0 (camera distance) */
extern s16 worldmapCameraPitchesGround[];                    /* Noah: SHORT_ARRAY_worldmap__8009b224 [4] */
extern s16 worldmapCameraPitchesFlying[];                    /* Noah: SHORT_ARRAY_worldmap__8009b22c [4] */
extern s16 worldmapCameraHeightsGround[];                    /* Noah: SHORT_ARRAY_worldmap__8009b234 [4] */
extern s16 worldmapCameraHeightsFlying[];                    /* Noah: SHORT_ARRAY_worldmap__8009b23c [4] */
extern s32 worldmapCameraStepDistances[];                    /* Noah: INT_ARRAY_worldmap__8009b214 [4] */
extern s32 worldmapMinimapScale;                      /* projection distance (SetGeomScreen) */
extern sWorldmapCameraState worldmapCameraVector;
extern VECTOR worldmapRadarPosition3;
extern VECTOR worldmapGridInputPosition;
extern s32 worldmapMatrixMode;
extern s16 worldmapExitVar0;
extern u8* worldmapFile1Buffer_1C;                      /* Noah: worldmapFile1Buffer_1C (text bundle) */
extern sWorldmapRenderingStruct* pCurrentWorldmapRenderingStruct;
extern s32 worldmapOddOrEven;
extern s16 worldmapExitVar1;
extern sWorldmapExitDef** worldmapExitsDefs; /* exit tables (4) */
extern sWorldmapExitDef* worldmapCurrentExit;
extern s32 worldmapFadePolyAbr;
extern DR_TPAGE worldmapFadePolyDrawTpage;
extern POLY_G4 worldmapFadePoly[2];
extern sWorldmapModel* worldmapModels;
extern POLY_FT4 worldmapInfoDialogBackground[2];              /* background quad of the second dialog */

s32 getAngleCos(s32 angle);
s32 getAngleSin(s32 angle);
s16 getWorldmapMaterial(VECTOR* p);
u8* getWorldChunkForPosition(s32 x, s32 z);
s32 getWorldmapMaterial2(VECTOR* p);
void checkWorldmapPositionSub0(VECTOR* p);
s16 getWorldmapGroundType(VECTOR* position);
s16 checkWorldmapPositionSub1_0_1(s16 a, s16 b);
void interpolateWorldPosition(VECTOR* out, s32 x, s32 z);
void averageVector(VECTOR* a, VECTOR* b, VECTOR* n);
void computeCameraVector(sWorldmapCameraState* cam, VECTOR* target, s32 dist, SVECTOR* rot);
s32 worldmap_taskCamera_updateSub0(s32 current, s16* pitch, s16* heights);
void checkWorldmapGearToggleInput(void);
void computeWormapSpritePosition(VECTOR* p);
s32 changeWorldmapEntityState(s32 index, s32 state);

/* model-rotation task update: spins worldmap models 2 and 3 (task states 1 spin up, 2 spin down) */
s32 worldmap_taskYggdrasilSubModels_update(s32 index) {
    sWorldmapScratch* sp = (sWorldmapScratch*)PSX_SCRATCH(0x1F800000);
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    sWorldmapModel* m2 = &worldmapModels[2];
    sWorldmapModel* m3 = &worldmapModels[3];

    if (p->m4_request == 9) {
        p->m4_request = 0;
        p->m20_subState = 1;
    } else if (p->m4_request == 10) {
        p->m4_request = 0;
        p->m20_subState = 2;
    }
    switch (p->m20_subState) {
    case 0:
        p->m58 = 0;
        p->m5C = 0;
        break;
    case 1:
        p->m58 += 4;
        if (p->m58 > 0x10) {
            p->m5C += 4;
        }
        if (p->m58 >= 0x80) {
            p->m58 = 0x80;
        }
        if (p->m5C >= 0x80) {
            p->m5C = 0x80;
        }
        if ((p->m58 >= 0x80) & (p->m5C >= 0x80)) {
            p->m20_subState = 3;
        }
        break;
    case 2:
        p->m58 -= 4;
        if (p->m58 < 0x70) {
            p->m5C -= 4;
        }
        if (p->m58 < 0) {
            p->m58 = 0;
        }
        if (p->m5C < 0) {
            p->m5C = 0;
        }
        if ((p->m58 == 0) & (p->m5C == 0)) {
            p->m20_subState = 0;
        }
        break;
    }
    p->m50 += p->m58;
    p->m54 -= p->m5C;
    sp->mA8.vx = 0;
    sp->mA0.vx = 0;
    sp->mA0.vy = p->m50;
    sp->mA8.vy = p->m54;
    sp->mA0.vz = m2->m18_rotation.vz;
    sp->mA8.vz = m3->m18_rotation.vz;
    RotMatrixZYX(&sp->mA0, &m2->m20_rotationMatrix);
    RotMatrixZYX(&sp->mA8, &m3->m20_rotationMatrix);
    return 1;
}

/* input edge helper */
void checkWorldmapGearToggleInput(void) {
    if ((worldmapInput1_0 & 1) && (worldmapInput1_0 & 2)) {
        worldmapGearToggleHeld = 1;
    } else {
        worldmapGearToggleHeld = 0;
    }
    worldmapGearTogglePressed = (worldmapGearToggleHeld ^ worldmapGearTogglePrevHeld) & worldmapGearToggleHeld;
    worldmapGearTogglePrevHeld = worldmapGearToggleHeld;
}

/* Noah: worldmapUpdatePlayerControls (worldmapPlayer.cpp); jump table jtbl_80070B54 */
s32 worldmapUpdatePlayerControls(sWorldmapStateEntry* p) {
    switch (worldmapInput1_0 >> 12) {
    case 1:
        p->m48_direction = worldmapRotation.vy;
        break;
    case 2:
        p->m48_direction = (worldmapRotation.vy + 0x400) & 0xFFF;
        break;
    case 3:
        p->m48_direction = (worldmapRotation.vy + 0x200) & 0xFFF;
        break;
    case 4:
        p->m48_direction = (worldmapRotation.vy + 0x800) & 0xFFF;
        break;
    case 6:
        p->m48_direction = (worldmapRotation.vy + 0x600) & 0xFFF;
        break;
    case 8:
        p->m48_direction = (worldmapRotation.vy - 0x400) & 0xFFF;
        break;
    case 9:
        p->m48_direction = (worldmapRotation.vy - 0x200) & 0xFFF;
        break;
    case 12:
        p->m48_direction = (worldmapRotation.vy - 0x600) & 0xFFF;
        break;
    }
    if (worldmapInput1_0 & 0xF000) {
        p->m38_step.vx = getAngleCos(p->m48_direction);
        p->m38_step.vz = -getAngleSin(p->m48_direction);
    }
    if (worldmapInput2_0 & 0x20) {
        if (adjustLocationAfterCollisionVar0 != 0) {
            return 3;
        }
        if (worldmapExitVar0 != -1) {
            return 1;
        }
    } else if (worldmapExitVar0 != -1 && worldmapCurrentExit->mE_type == 1) {
        return 1;
    }
    if ((worldmapInput2_0 & 0x10) && worldmapExitVar1 == -1 && worldmapExitVar0 == -1) {
        worldmapExitVar2 = 1;
    }
    checkWorldmapGearToggleInput();
    return 0;
}

/* Noah: worldmapUpdatePlayerGearsControls (worldmapPlayerGear.cpp); jump table jtbl_80070B84 */
s32 worldmapUpdatePlayerGearsControls(sWorldmapStateEntry* p) {
    switch (worldmapInput1_0 >> 12) {
    case 1:
        p->m48_direction = worldmapRotation.vy;
        break;
    case 2:
        p->m48_direction = (worldmapRotation.vy + 0x400) & 0xFFF;
        break;
    case 3:
        p->m48_direction = (worldmapRotation.vy + 0x200) & 0xFFF;
        break;
    case 4:
        p->m48_direction = (worldmapRotation.vy + 0x800) & 0xFFF;
        break;
    case 6:
        p->m48_direction = (worldmapRotation.vy + 0x600) & 0xFFF;
        break;
    case 8:
        p->m48_direction = (worldmapRotation.vy - 0x400) & 0xFFF;
        break;
    case 9:
        p->m48_direction = (worldmapRotation.vy - 0x200) & 0xFFF;
        break;
    case 12:
        p->m48_direction = (worldmapRotation.vy - 0x600) & 0xFFF;
        break;
    }
    if (worldmapInput1_0 & 0xF000) {
        p->m38_step.vx = getAngleCos(p->m48_direction);
        p->m38_step.vz = -getAngleSin(p->m48_direction);
    }
    if (worldmapInput2_0 & 0x20) {
        if (adjustLocationAfterCollisionVar0 != 0) {
            return 3;
        }
        if (worldmapExitVar0 != -1) {
            return 1;
        }
    }
    if ((worldmapInput2_0 & 0x10) && worldmapExitVar1 == -1 && worldmapExitVar0 == -1) {
        worldmapExitVar2 = 1;
    }
    checkWorldmapGearToggleInput();
    return 0;
}

/* Noah: processYggdrasilInputs_mode4 (worldmapTaskYggdrasil.cpp); jump table jtbl_80070BB4 */
s32 processYggdrasilInputs_mode4(sWorldmapStateEntry* p) {
    switch (worldmapInput1_0 >> 12) {
    case 1:
        p->m48_direction = worldmapRotation.vy;
        break;
    case 2:
        p->m48_direction = (worldmapRotation.vy + 0x400) & 0xFFF;
        break;
    case 3:
        p->m48_direction = (worldmapRotation.vy + 0x200) & 0xFFF;
        break;
    case 4:
        p->m48_direction = (worldmapRotation.vy + 0x800) & 0xFFF;
        break;
    case 6:
        p->m48_direction = (worldmapRotation.vy + 0x600) & 0xFFF;
        break;
    case 8:
        p->m48_direction = (worldmapRotation.vy - 0x400) & 0xFFF;
        break;
    case 9:
        p->m48_direction = (worldmapRotation.vy - 0x200) & 0xFFF;
        break;
    case 12:
        p->m48_direction = (worldmapRotation.vy - 0x600) & 0xFFF;
        break;
    }
    if (worldmapInput1_0 & 0xF000) {
        p->m38_step.vx = getAngleCos(p->m48_direction);
        p->m38_step.vz = -getAngleSin(p->m48_direction);
    }
    if ((worldmapInput2_0 & 0x20) && worldmapExitVar0 != -1) {
        return 1;
    }
    if (worldmapInput2_0 & 0x40) {
        return 4;
    }
    if ((worldmapInput2_0 & 0x10) && worldmapExitVar1 == -1 && worldmapExitVar0 == -1) {
        worldmapExitVar2 = 1;
    }
    return 0;
}

/* Noah: processYggdrasilInputs (worldmapTaskYggdrasil.cpp) */
s32 processYggdrasilInputs(sWorldmapStateEntry* p) {
    s32 target;
    s32 speed;
    s32 step;
    s32 accel;
    s32 d;
    s32 d2;
    s32 v;

    switch (p->m64) {
    case 0:
        p->m64 = 1;
        p->m5C = 0;
        p->m58 = p->m48_direction << 12;
        break;
    case 1:
        speed = 0;
        target = 0;
        step = 8;
        accel = 0x1200;
        if (worldmapInput1_0 & 0x8000) {
            target = -0x60;
            speed = -0x10000;
        }
        if (worldmapInput1_0 & 0x2000) {
            target += 0x60;
            speed += 0x10000;
        }
        if (worldmapInput1_0 & 4) {
            target -= 0x60;
            speed -= 0x10000;
        }
        if (worldmapInput1_0 & 8) {
            target += 0x60;
            speed += 0x10000;
        }
        if (worldmapInput1_0 & 0xA00C) {
            accel = 0x1000;
            step = abs(worldmapRotation.vz - target) >> 4;
        }
        if (worldmapRotation.vz != target) {
            d = abs(worldmapRotation.vz - target);
            if (d < step) {
                step = d;
            }
            if (worldmapRotation.vz < target) {
                worldmapRotation.vz = worldmapRotation.vz + step;
            } else {
                worldmapRotation.vz = worldmapRotation.vz - step;
            }
        }
        if (p->m5C != speed) {
            d2 = abs(p->m5C - speed);
            if (d2 < accel) {
                accel = d2;
            }
            if (p->m5C < speed) {
                p->m5C = p->m5C + accel;
            } else {
                p->m5C = p->m5C - accel;
            }
        }
        break;
    }
    p->m58 += p->m5C;
    p->m48_direction = (p->m58 >> 12) & 0xFFF;
    worldmapRotation.vy = p->m48_direction;
    switch (p->m6C) {
    case 0:
        p->m6C = 1;
        p->m70 = 0;
        break;
    case 1:
        if (p->m70 != 0) {
            if (p->m70 < 0) {
                p->m70 += 0x2000;
            } else {
                p->m70 -= 0x2000;
            }
        }
        if (worldmapInput1_0 & 0x4000) {
            p->m6C = 2;
        }
        if (worldmapInput1_0 & 0x1000) {
            p->m6C = 3;
        }
        break;
    case 2:
        if (worldmapInput1_0 & 0x4000) {
            p->m70 -= 0x4000;
            if (p->m70 < -0x80000) {
                p->m70 = -0x80000;
            }
        } else {
            p->m6C = 1;
        }
        break;
    case 3:
        if (worldmapInput1_0 & 0x1000) {
            p->m70 += 0x4000;
            if (p->m70 > 0x80000) {
                p->m70 = 0x80000;
            }
        } else {
            p->m6C = 1;
        }
        break;
    }
    if (worldmapInput1_0 & 0x80) {
        if (worldmapInput1_0 & 2) {
            p->m60 -= 0x1000;
        } else {
            p->m60 += 0x1000;
        }
    }
    if (worldmapInput1_0 & 0x80) {
        if (p->m60 < -p->m4A_stepScale << 12) {
            p->m60 = -p->m4A_stepScale << 12;
        }
        if (p->m60 > p->m4A_stepScale << 12) {
            p->m60 = p->m4A_stepScale << 12;
        }
    } else if (p->m60 != 0) {
        if (p->m60 < 0) {
            p->m60 += 0x1000;
        } else {
            p->m60 -= 0x1000;
        }
    }
    p->m38_step.vx = getAngleCos(worldmapRotation.vy);
    p->m38_step.vy = getAngleCos(p->m70 >> 12);
    p->m38_step.vz = -getAngleSin(worldmapRotation.vy);
    VectorNormal(&p->m38_step, &p->m38_step);
    if ((worldmapInput2_0 & 0x20) && worldmapExitVar0 != -1) {
        return 1;
    }
    if ((worldmapInput2_0 & 0x40) && ((p->m60 == 0) & (worldmapRotation.vz == 0))) {
        return 4;
    }
    if ((worldmapInput2_0 & 0x10) && worldmapExitVar1 == -1 && worldmapExitVar0 == -1) {
        worldmapExitVar2 = 1;
    }
    return 0;
}

/* worldmap_taskWorldCamera_init (worldmapTaskWorldCamera.cpp) */
s32 worldmap_taskWorldCamera_init(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];

    p->m28_position = worldmapRadarPosition;
    worldmapRotation.vy = worldmapPlayerDirection;
    worldmapRotation.vz = 0;
    p->m50 = worldmapPlayerDirection;
    p->m58 = worldmapRotation.vy << 12;
    switch (worldMapGearMode) {
    case 6:
    case 7:
        p->m20_subState = 3;
        break;
    }
    return 1;
}

/* Noah: worldmap_taskWorldCamera_update (worldmapTaskWorldCamera.cpp); jump tables jtbl_80070BE4, jtbl_80070C0C */
s32 worldmap_taskWorldCamera_update(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    VECTOR d;
    VECTOR d8;
    s32 a;

#ifdef PLATFORM_PC
    pcWorldCameraFrame(); /* the free camera's input (port/src/system/freecam.c) */
#endif
    switch (p->m4_request) {
    case 9:
        p->m4_request = 0;
        p->m20_subState = 2;
        p->m50 = worldmapPlayerDirection;
        p->m58 = worldmapRotation.vy << 12;
        break;
    case 10:
        p->m4_request = 0;
        p->m20_subState = 0;
        p->m50 = worldmapRotation.vy;
        break;
    case 15:
        p->m20_subState = 0x10;
        p->m4_request = 0;
        p->m50 = 0x800;
        break;
    case 16:
        p->m20_subState = 0x10;
        p->m4_request = 0;
        p->m50 = 0xA00;
        break;
    case 17:
        p->m20_subState = 0x10;
        p->m4_request = 0;
        p->m50 = 0xC00;
        break;
    }
    switch (p->m20_subState) {
    case 0:
#ifdef PLATFORM_PC
        p->m50 = (p->m50 + pcWorldCameraTurn()) & 0xFFF; /* the right stick turns as L1/R1 do */
#endif
        switch ((worldmapInput1_0 >> 2) & 3) {
        case 1:
        case 3:
            p->m20_subState = 1;
            p->m54 = -0x40;
            p->m5C = (p->m50 - 0x200) & 0xFFF;
            break;
        case 2:
            p->m20_subState = 1;
            p->m54 = 0x40;
            p->m5C = (p->m50 + 0x200) & 0xFFF;
            break;
        }
    rotate:
        if (worldmapRotation.vy != p->m50) {
            a = (s32)p->m50 - worldmapRotation.vy;
            if (abs(a) > 0xC00) {
                if (a < 0) {
                    a += 0x1000;
                } else {
                    a -= 0x1000;
                }
            }
            p->m58 = (p->m58 + ((a << 12) >> 3)) & 0xFFFFFF;
            worldmapRotation.vy = p->m58 >> 12;
        } else {
            p->m58 = worldmapRotation.vy << 12;
        }
        break;
    case 1:
        p->m50 = (p->m50 + p->m54) & 0xFFF;
        if (p->m50 == p->m5C) {
            p->m20_subState = 0;
        }
        goto rotate;
    case 2:
        a = (s32)p->m50 - worldmapRotation.vy;
        if (abs(a) > 0x800) {
            if (a < 0) {
                a += 0x1000;
            } else {
                a -= 0x1000;
            }
        }
        if (abs(a) > 0x180) {
            if (a < 0) {
                a = -0x180;
            } else {
                a = 0x180;
            }
        }
        p->m58 = (p->m58 + ((a << 12) >> 3)) & 0xFFFFFF;
        worldmapRotation.vy = p->m58 >> 12;
        if ((worldmapRotation.vy == p->m50) & (p->m60 == 0)) {
            p->m20_subState = 3;
            changeWorldmapEntityState(7, 0xB);
        }
        break;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
        break;
    case 16:
        a = (s32)p->m50 - worldmapRotation.vy;
        if (abs(a) > 0x800) {
            if (a < 0) {
                a += 0x1000;
            } else {
                a -= 0x1000;
            }
        }
        if (abs(a) > 0x180) {
            if (a < 0) {
                a = -0x180;
            } else {
                a = 0x180;
            }
        }
        p->m58 = (p->m58 + ((a << 12) >> 5)) & 0xFFFFFF;
        worldmapRotation.vy = p->m58 >> 12;
        if ((worldmapRotation.vy == p->m50) & (p->m60 == 0)) {
            p->m20_subState = 3;
        }
        break;
    }
    switch (p->m20_subState) {
    case 0:
    case 1:
    case 2:
    case 16:
        if ((p->m28_position.vx != worldmapRadarPosition.vx) | (p->m28_position.vy != worldmapRadarPosition.vy) | (p->m28_position.vz != worldmapRadarPosition.vz)) {
            d.vx = worldmapRadarPosition.vx - p->m28_position.vx;
            d.vy = worldmapRadarPosition.vy - p->m28_position.vy;
            d.vz = worldmapRadarPosition.vz - p->m28_position.vz;
            computeWormapSpritePosition(&d);
            d8.vx = d.vx >> 3;
            d8.vy = d.vy >> 3;
            d8.vz = d.vz >> 3;
            if ((abs(d8.vx) < 0x40) & (abs(d8.vz) < 0x40)) {
                p->m60 = 0;
                worldmapGridInputPosition.vx += d.vx;
                worldmapGridInputPosition.vz += d.vz;
                p->m28_position.vx = worldmapRadarPosition.vx;
                p->m28_position.vz = worldmapRadarPosition.vz;
            } else {
                p->m60 = 1;
                p->m28_position.vx += d8.vx;
                p->m28_position.vz += d8.vz;
                worldmapGridInputPosition.vx += d8.vx;
                worldmapGridInputPosition.vz += d8.vz;
            }
            p->m28_position.vy += d8.vy;
        }
        break;
    case 3:
        if ((p->m28_position.vx != worldmapRadarPosition.vx) | (p->m28_position.vy != worldmapRadarPosition.vy) | (p->m28_position.vz != worldmapRadarPosition.vz)) {
            d.vx = worldmapRadarPosition.vx - p->m28_position.vx;
            d.vy = worldmapRadarPosition.vy - p->m28_position.vy;
            d.vz = worldmapRadarPosition.vz - p->m28_position.vz;
            computeWormapSpritePosition(&d);
            p->m28_position.vy += d.vy >> 4;
            worldmapGridInputPosition.vx += d.vx;
            worldmapGridInputPosition.vz += d.vz;
            p->m28_position.vx = worldmapRadarPosition.vx;
            p->m28_position.vz = worldmapRadarPosition.vz;
        }
        break;
    }
    checkWorldmapPositionSub0(&p->m28_position);
    worldmapRadarPosition3 = p->m28_position;
    return 1;
}

s32 worldmap_taskCamera_init(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];

    switch (worldMapGearMode) {
    case 6:
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
        p->m50 = 3;
        worldmapCameraVar0 = 0x460000;
        worldmapRotation.vx = -0x260;
        p->m64 = (sptr)worldmapCameraPitchesGround;
        p->m68 = (sptr)worldmapCameraHeightsGround;
        break;
    case 7:
        p->m50 = 3;
        worldmapCameraVar0 = 0x280000;
        worldmapRotation.vx = -0x260;
        p->m64 = (sptr)worldmapCameraPitchesFlying;
        p->m68 = (sptr)worldmapCameraHeightsFlying;
        break;
    }
    return 1;
}

s32 worldmap_taskCamera_update(s32 index) {
    s16 ret = 1;
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    s16* pitch;
    s32 r;
    s32 target;
    s32 diff;

    switch (p->m4_request) {
    case 14:
        p->m4_request = 0;
        p->m20_subState = 0;
        worldmapMatrixMode = 0;
        break;
    case 17:
        worldmapCameraVar0 = 0x400000;
        p->m20_subState = 3;
        p->m4_request = 0;
        p->m58 = -0x100;
        break;
    case 9:
        p->m64 = (sptr)worldmapCameraPitchesFlying;
        p->m4_request = 0;
        p->m68 = (sptr)worldmapCameraHeightsFlying;
        if (p->m50 == 0) {
            p->m50 = 1;
        }
        break;
    case 10:
        p->m64 = (sptr)worldmapCameraPitchesGround;
        p->m4_request = 0;
        p->m68 = (sptr)worldmapCameraHeightsGround;
        if (p->m50 == 0) {
            p->m50 = 1;
        }
        break;
    }
    pitch = (s16*)p->m64;
    switch (p->m20_subState) {
    case 0:
        r = worldmap_taskCamera_updateSub0((s32)p->m50, pitch, (s16*)p->m68);
        if (p->m50 != r) {
            p->m20_subState = 1;
            p->m22_timer = 0;
            p->m50 = r;
            p->m54 = worldmapCameraStepDistances[r];
            p->m58 = pitch[p->m50];
            p->m60 = worldmapRotation.vx << 12;
        }
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
        break;
    case 1:
        if (p->m22_timer >= 5) {
            r = worldmap_taskCamera_updateSub0((s32)p->m50, pitch, (s16*)p->m68);
            if (p->m50 != r) {
                p->m50 = r;
                p->m54 = worldmapCameraStepDistances[r];
                p->m58 = pitch[p->m50];
            }
            p->m22_timer = 0;
        }
        target = (s32)p->m54;
        diff = target - worldmapCameraVar0;
        if (target != worldmapCameraVar0) {
            if (diff > 0) {
                if (diff > 0x40000) {
                    diff = 0x40000;
                }
                diff >>= 3;
                if (diff < 0x40) {
                    worldmapCameraVar0 = target;
                } else {
                    worldmapCameraVar0 += diff;
                }
            } else {
                worldmapCameraVar0 -= 0x2000;
            }
        }
        if (p->m58 != worldmapRotation.vx) {
            diff = ((s32)p->m58 - worldmapRotation.vx) << 12;
            if (diff < 0) {
                p->m60 = p->m60 + (diff >> 5);
            } else if (p->m50 == 0) {
                p->m60 = p->m60 + 0xF32;
            } else {
                p->m60 = p->m60 + 0x799;
            }
            worldmapRotation.vx = p->m60 >> 12;
        }
        if ((p->m54 == worldmapCameraVar0) & (p->m58 == worldmapRotation.vx)) {
            p->m20_subState = 0;
        }
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
        break;
    case 2:
        worldmapCameraVector.m8_target.vx = 0;
        worldmapCameraVector.m8_target.vz = 0;
        worldmapCameraVector.m8_target.vy = worldmapRadarPosition3.vy >> 12;
        break;
    case 3:
        if (p->m58 != worldmapRotation.vx) {
            worldmapRotation.vx += 2;
        }
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, worldmapCameraVar0, &worldmapRotation);
        break;
    }
#ifdef PLATFORM_PC
    if (p->m20_subState != 2 && pcWorldCameraActive()) {
        /* the free camera's tilt and zoom: the eye drawn from, not the game's angles (freecam.c) */
        SVECTOR view = worldmapRotation;
        view.vx = pcWorldCameraPitch(view.vx);
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, pcWorldCameraDistance(worldmapCameraVar0), &view);
    }
#endif
    p->m22_timer++;
    return ret;
}

/* Noah: worldmap_taskCamera_updateSub0 (worldmapTaskCamera.cpp): picks the camera pitch step whose
 * view clears the terrain; `h` is deliberately reused (z start, height, clearance). */
s32 worldmap_taskCamera_updateSub0(s32 current, s16* pitch, s16* heights) {
    sWorldmapScratch* sp;
    s32 i;
    s32 base;
    s32 minH;
    s32 x0;
    s32 xi;
    s32 zi;
    s32 h;
    s32 d;
    s32* dist;

    sp = (sWorldmapScratch*)PSX_SCRATCH(0x1F800000);
    dist = worldmapCameraStepDistances;
    i = 0;
    sp->mA8.vy = worldmapRotation.vy;
    base = worldmapRadarPosition.vy >> 12;
    sp->mA8.vz = worldmapRotation.vz;
    for (; i < 3; i++, pitch++, dist++, heights++) {
        sp->mA8.vx = *pitch;
        computeCameraVector(&worldmapCameraVector, &worldmapRadarPosition3, *dist - ((worldmapMinimapScale / 2) << 12), &sp->mA8);
        minH = 0;
        x0 = worldmapRadarPosition3.vx + ((worldmapCameraVector.m0_eye.vx - 0x180) << 12);
        h = worldmapRadarPosition3.vz - ((worldmapCameraVector.m0_eye.vz + 0x180) << 12);
        x0 &= 0xFFF80000;
        h &= 0xFFF80000;
        sp->m0.vz = h;
        for (zi = 0; zi < 6; zi++) {
            sp->m0.vx = x0;
            for (xi = 0; xi < 6; xi++) {
                checkWorldmapPositionSub0(&sp->m0);
                h = (s8)*getWorldChunkForPosition(sp->m0.vx, sp->m0.vz);
                sp->m0.vy = h;
                if (h < minH) {
                    minH = h;
                }
                sp->m0.vx += 0x80000;
            }
            sp->m0.vz += 0x80000;
        }
        if (*heights + base + 0x50 < minH * 8) {
            break;
        }
    }
    if (i < current) {
        h = minH * 8 - 0x50;
        d = worldmapCameraHeightsGround[current] + base - h;
        if (abs(d) < 0x41) {
            i = current;
        }
    }
    return i;
}

/* task init paired with worldmap_taskMinimapY_update (worldmapGeometryOffsetY = 0x8C or 0x78 by gear mode) */
s32 worldmap_taskMinimapY_init(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];

    switch (worldMapGearMode) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
        worldmapGeometryOffsetY = 0x8C;
        break;
    case 6:
    case 7:
        worldmapGeometryOffsetY = 0x78;
        p->m20_subState = 1;
        break;
    }
    p->m50 = worldmapGeometryOffsetY << 12;
    return 1;
}

/* task update paired with worldmap_taskMinimapY_init (eases worldmapGeometryOffsetY between 0x78 and 0x8C) */
s32 worldmap_taskMinimapY_update(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    s32 ret = 1;

    if (p->m4_request == 9) {
        p->m4_request = 0;
        p->m20_subState = 1;
    } else if (p->m4_request == 10) {
        p->m4_request = 0;
        p->m20_subState = 2;
    }
    switch (p->m20_subState) {
    case 0:
        ret = 3;
        break;
    case 1:
        p->m50 -= 0x1000;
        worldmapGeometryOffsetY = (s32)p->m50 >> 12;
        if (worldmapGeometryOffsetY < 0x78) {
            worldmapGeometryOffsetY = 0x78;
            p->m50 = 0x78000;
            p->m20_subState = 0;
        }
        break;
    case 2:
        p->m50 += 0x1000;
        worldmapGeometryOffsetY = (s32)p->m50 >> 12;
        if (worldmapGeometryOffsetY >= 0x8C) {
            worldmapGeometryOffsetY = 0x8C;
            p->m50 = 0x8C000;
            p->m20_subState = 0;
        }
        break;
    }
    return ret;
}

/* Noah: worldmap_taskFader_init (worldmapTaskFader.cpp) */
s32 worldmap_taskFader_init(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];

    p->m50 = 0xF8;
    SetDrawTPage(&worldmapFadePolyDrawTpage, 1, 0, GetTPage(0, worldmapFadePolyAbr, 0x380, 0x100));
    setlen(&worldmapFadePoly[0], 8);
    setcode(&worldmapFadePoly[0], 0x38);
    worldmapFadePoly[0].r0 = p->m50;
    worldmapFadePoly[0].g0 = p->m50;
    worldmapFadePoly[0].b0 = p->m50;
    worldmapFadePoly[0].r1 = p->m50;
    worldmapFadePoly[0].g1 = p->m50;
    worldmapFadePoly[0].b1 = p->m50;
    worldmapFadePoly[0].r2 = p->m50;
    worldmapFadePoly[0].g2 = p->m50;
    worldmapFadePoly[0].b2 = p->m50;
    worldmapFadePoly[0].r3 = p->m50;
    worldmapFadePoly[0].g3 = p->m50;
    worldmapFadePoly[0].b3 = p->m50;
    SetSemiTrans(&worldmapFadePoly[0], 1);
    worldmapFadePoly[1] = worldmapFadePoly[0];
    worldmapFadePoly[0].x0 = 0;
    worldmapFadePoly[0].y0 = 0;
    worldmapFadePoly[0].x1 = 0x140;
    worldmapFadePoly[0].y1 = 0;
    worldmapFadePoly[0].x2 = 0;
    worldmapFadePoly[0].y2 = 0xD8;
    worldmapFadePoly[0].x3 = 0x140;
    worldmapFadePoly[0].y3 = 0xD8;
    worldmapFadePoly[1].x0 = 0;
    worldmapFadePoly[1].y0 = 0;
    worldmapFadePoly[1].x1 = 0x140;
    worldmapFadePoly[1].y1 = 0;
    worldmapFadePoly[1].x2 = 0;
    worldmapFadePoly[1].y2 = 0xD8;
    worldmapFadePoly[1].x3 = 0x140;
    worldmapFadePoly[1].y3 = 0xD8;
    return 1;
}

/* Noah: worldmap fader update */
s32 worldmap_taskFader_update(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];
    s32 ret = 1;
    /* Reserved local storage preserves the retail 0xE0-byte stack frame. */
    u8 unused[0x60];
    POLY_G4* poly0;
    POLY_G4* poly;
    s32 idx;
    POLY_G4* base0;
    POLY_G4* base;
    OTag tag;
    POLY_G4* cq0;
    POLY_G4* cq1;
    POLY_G4* cq2;
    POLY_G4* cq3;
    POLY_G4* cq4;
    POLY_G4* cq5;
    POLY_G4* cq6;
    POLY_G4* cq7;
    POLY_G4* cq8;
    POLY_G4* cq9;
    POLY_G4* cq10;
    POLY_G4* cq11;
    s32 ci;

    switch (p->m4_request) {
    case 13:
        p->m4_request = 0;
        p->m20_subState = 1;
        worldmapFadeRunning = 1;
        SetDrawTPage(&worldmapFadePolyDrawTpage, 1, 0, GetTPage(0, worldmapFadePolyAbr, 0x380, 0x100));
        break;
    case 12:
        p->m4_request = 0;
        p->m20_subState = 0;
        worldmapFadeRunning = 1;
        SetDrawTPage(&worldmapFadePolyDrawTpage, 1, 0, GetTPage(0, worldmapFadePolyAbr, 0x380, 0x100));
        break;
    }
    switch (p->m20_subState) {
    case 0:
        /* Reuse the scalar for the buffer index, then the fade brightness. */
        ci = worldmapOddOrEven;
        cq0 = &worldmapFadePoly[ci];
        ci = (s32)p->m50;
        cq0->r0 = ci;
        ci = worldmapOddOrEven;
        cq1 = &worldmapFadePoly[ci];
        ci = (s32)p->m50;
        cq1->g0 = ci;
        ci = worldmapOddOrEven;
        cq2 = &worldmapFadePoly[ci];
        ci = (s32)p->m50;
        cq2->b0 = ci;
        ci = worldmapOddOrEven;
        cq3 = &worldmapFadePoly[ci];
        ci = (s32)p->m50;
        cq3->r1 = ci;
        ci = worldmapOddOrEven;
        cq4 = &worldmapFadePoly[ci];
        ci = (s32)p->m50;
        cq4->g1 = ci;
        ci = worldmapOddOrEven;
        cq5 = &worldmapFadePoly[ci];
        ci = (s32)p->m50;
        cq5->b1 = ci;
        ci = worldmapOddOrEven;
        cq6 = &worldmapFadePoly[ci];
        ci = (s32)p->m50;
        cq6->r2 = ci;
        ci = worldmapOddOrEven;
        cq7 = &worldmapFadePoly[ci];
        ci = (s32)p->m50;
        cq7->g2 = ci;
        ci = worldmapOddOrEven;
        cq8 = &worldmapFadePoly[ci];
        ci = (s32)p->m50;
        cq8->b2 = ci;
        ci = worldmapOddOrEven;
        cq9 = &worldmapFadePoly[ci];
        ci = (s32)p->m50;
        cq9->r3 = ci;
        ci = worldmapOddOrEven;
        cq10 = &worldmapFadePoly[ci];
        ci = (s32)p->m50;
        cq10->g3 = ci;
        ci = worldmapOddOrEven;
        cq11 = &worldmapFadePoly[ci];
        ci = (s32)p->m50;
        cq11->b3 = ci;
        base0 = worldmapFadePoly;
        idx = worldmapOddOrEven;
        poly0 = &base0[idx];
        tag = poly0->tag & ~OT_LINK_MASK;
        tag |= *pCurrentWorldmapRenderingStruct->m70_OT & OT_LINK_MASK;
        poly0->tag = tag;
        setaddr(pCurrentWorldmapRenderingStruct->m70_OT, poly0);
        addPrim(pCurrentWorldmapRenderingStruct->m70_OT, &worldmapFadePolyDrawTpage);
        p->m50 -= worldmapFadePolyStep;
        if (p->m50 < 0) {
            ret = 3;
            p->m50 = 0;
            goto done;
        }
        break;
    case 1:
        worldmapFadePoly[worldmapOddOrEven].r0 = p->m50;
        worldmapFadePoly[worldmapOddOrEven].g0 = p->m50;
        worldmapFadePoly[worldmapOddOrEven].b0 = p->m50;
        worldmapFadePoly[worldmapOddOrEven].r1 = p->m50;
        worldmapFadePoly[worldmapOddOrEven].g1 = p->m50;
        worldmapFadePoly[worldmapOddOrEven].b1 = p->m50;
        worldmapFadePoly[worldmapOddOrEven].r2 = p->m50;
        worldmapFadePoly[worldmapOddOrEven].g2 = p->m50;
        worldmapFadePoly[worldmapOddOrEven].b2 = p->m50;
        worldmapFadePoly[worldmapOddOrEven].r3 = p->m50;
        worldmapFadePoly[worldmapOddOrEven].g3 = p->m50;
        worldmapFadePoly[worldmapOddOrEven].b3 = p->m50;
        base = worldmapFadePoly;
        idx = worldmapOddOrEven;
        poly = &base[idx];
        tag = poly->tag & ~OT_LINK_MASK;
        tag |= *pCurrentWorldmapRenderingStruct->m70_OT & OT_LINK_MASK;
        poly->tag = tag;
        setaddr(pCurrentWorldmapRenderingStruct->m70_OT, poly);
        addPrim(pCurrentWorldmapRenderingStruct->m70_OT, &worldmapFadePolyDrawTpage);
        p->m50 += worldmapFadePolyStep;
        if (p->m50 >= 0xFF) {
            p->m50 = 0xFF;
        done:
            worldmapFadeRunning = 0;
        }
        break;
    default:
        goto end;
    }
    p->m22_timer++;
end:
    return ret;
}

/* dialog task init (window worldmapExitDialogWindow) */
s32 worldmap_taskExit_init(s32 index) {
    worldmapExitVar0 = -1;
    setupWindowSize2(&worldmapExitDialogWindow, 0x3C0, 0x180, 0xA0, 0x78, 0x20, 1);
    worldmapExitDialogWindow.m68_charsPerStep = 8;
    worldmapExitDialogWindow.m10_flags |= 2;
    resetDialogWindow18(&worldmapExitDialogWindow);
    return 1;
}

/* dialog task update: shows text worldmapExitVar0 of the text bundle */
s32 worldmap_taskExit_update(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];

    switch (p->m20_subState) {
    case 0:
        if (worldmapExitVar0 != -1) {
            resetDialogWindow18(&worldmapExitDialogWindow);
            addDialogWindowsToOTSub2(&worldmapExitDialogWindow, getDialogParamPointer(worldmapFile1Buffer_1C, worldmapExitVar0));
            p->m20_subState = 1;
            p->m50 = worldmapExitVar0;
        }
        break;
    case 1:
        if (worldmapExitVar0 == -1) {
            resetDialogWindow18(&worldmapExitDialogWindow);
            p->m20_subState = 0;
        } else if (worldmapExitVar0 != p->m50) {
            resetDialogWindow18(&worldmapExitDialogWindow);
            addDialogWindowsToOTSub2(&worldmapExitDialogWindow, getDialogParamPointer(worldmapFile1Buffer_1C, worldmapExitVar0));
            p->m50 = worldmapExitVar0;
        }
        break;
    }
    updateAndRenderTextForDialogWindow(&worldmapExitDialogWindow, pCurrentWorldmapRenderingStruct->m70_OT, worldmapOddOrEven);
    return 1;
}

/* dialog window free */
void freeWorldmapExitDialog(void) {
    dealocateDialogWindow18Memory(&worldmapExitDialogWindow);
}

/* second dialog task init (window worldmapInfoDialogWindow + background POLY_FT4 worldmapInfoDialogBackground[2]) */
s32 worldmap_taskInfoDialog_init(s32 index) {
    POLY_FT4* p;

    worldmapExitVar1 = -1;
    setupWindowSize2(&worldmapInfoDialogWindow, 0x3C0, 0x18D, 0xA0, 0x78, 0x20, 4);
    worldmapInfoDialogWindow.m68_charsPerStep = 8;
    worldmapExitDialogWindow.m10_flags |= 2;
    resetDialogWindow18(&worldmapInfoDialogWindow);
    p = &worldmapInfoDialogBackground[0];
    setlen(p, 9);
    setcode(p, 0x2C);
    p->x0 = 0x98;
    p->y0 = 0x70;
    p->x1 = 0x128;
    p->y1 = 0x70;
    p->x2 = 0x98;
    p->y2 = 0xB4;
    p->x3 = 0x128;
    p->y3 = 0xB4;
    p->u0 = 0x80;
    p->v0 = 0;
    p->u1 = 0xFF;
    p->v1 = 0;
    p->u2 = 0x80;
    p->v2 = 0x3F;
    p->u3 = 0xFF;
    p->v3 = 0x3F;
    p->r0 = 0x80;
    p->g0 = 0x80;
    p->b0 = 0x80;
    p->tpage = GetTPage(0, 0, 0x380, 0x100);
    p->clut = GetClut(0x130, 0x1E4);
    SetSemiTrans(p, 1);
    p[1] = p[0];
    return 1;
}

/* second dialog task update (window worldmapInfoDialogWindow + background quad) */
s32 worldmap_taskInfoDialog_update(s32 index) {
    sWorldmapStateEntry* p = &gWorldmapState->m0_entries[index];

    switch (p->m20_subState) {
    case 0:
        if (worldmapExitVar1 != -1) {
            resetDialogWindow18(&worldmapInfoDialogWindow);
            addDialogWindowsToOTSub2(&worldmapInfoDialogWindow, getDialogParamPointer(worldmapFile1Buffer_1C, worldmapExitVar1));
            p->m20_subState = 1;
            p->m50 = worldmapExitVar1;
        }
        break;
    case 1:
        if (worldmapExitVar1 == -1) {
            resetDialogWindow18(&worldmapInfoDialogWindow);
            p->m20_subState = 0;
        } else if (worldmapExitVar1 != p->m50) {
            resetDialogWindow18(&worldmapInfoDialogWindow);
            addDialogWindowsToOTSub2(&worldmapInfoDialogWindow, getDialogParamPointer(worldmapFile1Buffer_1C, worldmapExitVar1));
            p->m50 = worldmapExitVar1;
        }
        break;
    }
    updateAndRenderTextForDialogWindow(&worldmapInfoDialogWindow, pCurrentWorldmapRenderingStruct->m70_OT, worldmapOddOrEven);
    if (p->m20_subState == 1) {
        addPrim(pCurrentWorldmapRenderingStruct->m70_OT, &worldmapInfoDialogBackground[worldmapOddOrEven]);
    }
    return 1;
}

/* dialog window free */
void freeWorldmapInfoDialog(void) {
    dealocateDialogWindow18Memory(&worldmapInfoDialogWindow);
}

void tweakWorldmapTexture(u16* src, u16* dst, s32 steps, u8* color) {
    u32 alpha;
    u32 red;
    u32 green;
    u32 blue;
    s32 r;
    s32 g;
    s32 b;
    s32 i;
    s32 j;
    u32 t;
    u16* s;
    u8 c[3];

    r = color[0];
    g = color[1];
    b = color[2];
    for (i = 0; i < steps; i++) {
        t = (i << 12) / steps;
        s = src;
        j = 0;
        while (j < 0x100) {
            if (*s == 0) {
                *dst = *s;
            } else {
                c[0] = (*s & 0x1F) << 3;
                c[1] = (*s >> 2) & 0xF8;
                c[2] = (*s >> 7) & 0xF8;
                alpha = *s & 0x8000;
                red = (c[0] * (0x1000 - t) + r * t) >> 15;
                green = c[1] * (0x1000 - t) + g * t;
                blue = c[2] * (0x1000 - t) + b * t;
                *dst = alpha | red | ((green >> 15) << 5) | ((blue >> 15) << 10);
            }
            j++;
            s++;
            dst++;
        }
    }
}

/* Noah: checkWorldmapPositionSub0 (wrap a position into the world) */
void checkWorldmapPositionSub0(VECTOR* p) {
    if (p->vx >= worldmapSizeX << 23) {
        p->vx -= worldmapSizeX << 23;
    }
    if (p->vx < 0) {
        p->vx += worldmapSizeX << 23;
    }
    if (p->vz >= worldmapSizeY << 23) {
        p->vz -= worldmapSizeY << 23;
    }
    if (p->vz < 0) {
        p->vz += worldmapSizeY << 23;
    }
}

/* wrap, <<11 variant */
void wrapWorldmapIntPosition(VECTOR* p) {
    if (p->vx >= worldmapSizeX << 11) {
        p->vx -= worldmapSizeX << 11;
    }
    if (p->vx < 0) {
        p->vx += worldmapSizeX << 11;
    }
    if (p->vz >= worldmapSizeY << 11) {
        p->vz -= worldmapSizeY << 11;
    }
    if (p->vz < 0) {
        p->vz += worldmapSizeY << 11;
    }
}

/* wrap to nearest, <<23 */
void computeWormapSpritePosition(VECTOR* p) {
    if (p->vx < -0x4000000) {
        p->vx += worldmapSizeX << 23;
    } else if (p->vx > 0x4000000) {
        p->vx -= worldmapSizeX << 23;
    }
    if (p->vz < -0x4000000) {
        p->vz += worldmapSizeY << 23;
    } else if (p->vz > 0x4000000) {
        p->vz -= worldmapSizeY << 23;
    }
}

/* wrap to nearest, <<11 */
void adjustWorldModelLocation(VECTOR* p) {
    if (p->vx < -0x4000) {
        p->vx += worldmapSizeX << 11;
    } else if (p->vx > 0x4000) {
        p->vx -= worldmapSizeX << 11;
    }
    if (p->vz < -0x4000) {
        p->vz += worldmapSizeY << 11;
    } else if (p->vz > 0x4000) {
        p->vz -= worldmapSizeY << 11;
    }
}

void averageVector(VECTOR* a, VECTOR* b, VECTOR* n) {
    s32 t;

    t = (-(n->vx * (a->vx - b->vx)) - n->vz * (a->vz - b->vz)) / n->vy;
    a->vy = t;
    a->vy = t + b->vy;
}

u8* getWorldChunkForPosition(s32 x, s32 z) {
    s32 tileX;
    s32 tileZ;
    s16 quadrant;
    s32 tileId;
    s32 sub;

    tileId = ((z >> 20) / 8) * worldmapSizeX + (x >> 20) / 8;
    x = (x >> 12) & 0x7FF;
    z = (z >> 12) & 0x7FF;
    quadrant = 0;
    if (x >= 0x400) {
        quadrant = 1;
        x -= 0x400;
    }
    if (z >= 0x400) {
        quadrant |= 2;
        z -= 0x400;
    }
    sub = ((z >> 4) / 8) * 9 + (x >> 4) / 8;
    return WM_CHUNK((s16)tileId) + quadrant * 0x144 + (s16)sub * 4;
}

/* Noah: interpolateWorldPosition (ground normal at x,z; scratchpad vectors at 0x1F800000) */
void interpolateWorldPosition(VECTOR* out, s32 x, s32 z) {
    sWorldmapScratch* sp;
    u8* c;

    sp = (sWorldmapScratch*)PSX_SCRATCH(0x1F800000);
    c = getWorldChunkForPosition(x, z);
    x = (x / 8) & 0xFFFF;
    z = (z / 8) & 0xFFFF;
    if (c[1] & 0x80) {
        if ((x * worldmapCellDiagonalNormal.vx + -z * worldmapCellDiagonalNormal.vz) < 0) {
            sp->m0.vx = 0x10;
            sp->m0.vz = -0x10;
            sp->m10.vx = 0;
            sp->m10.vz = -0x10;
            sp->m0.vy = (s8)c[0x28] - (s8)c[0];
            sp->m10.vy = (s8)c[0x24] - (s8)c[0];
        } else {
            sp->m0.vx = 0x10;
            sp->m10.vx = 0x10;
            sp->m0.vz = 0;
            sp->m10.vz = -0x10;
            sp->m0.vy = (s8)c[4] - (s8)c[0];
            sp->m10.vy = (s8)c[0x28] - (s8)c[0];
        }
    } else {
        if (((x - 0x10000) * worldmapCellAntiDiagonalNormal.vx + -z * worldmapCellAntiDiagonalNormal.vz) < 0) {
            sp->m0.vx = -0x10;
            sp->m0.vz = -0x10;
            sp->m10.vx = -0x10;
            sp->m10.vz = 0;
            sp->m0.vy = (s8)c[0x24] - (s8)c[4];
            sp->m10.vy = (s8)c[0] - (s8)c[4];
        } else {
            sp->m0.vx = 0;
            sp->m0.vz = -0x10;
            sp->m10.vx = -0x10;
            sp->m10.vz = -0x10;
            sp->m0.vy = (s8)c[0x28] - (s8)c[4];
            sp->m10.vy = (s8)c[0x24] - (s8)c[4];
        }
    }
    OuterProduct0(&sp->m10, &sp->m0, &sp->m20);
    VectorNormal(&sp->m20, out);
}

s32 worldmapGetAltitudeFor2dPoint(s32 x, s32 z) {
    VECTOR pos;
    VECTOR base;
    VECTOR normal;
    u8* c;

    c = getWorldChunkForPosition(x, z);
    pos.vx = (x / 8) & 0xFFFF;
    pos.vz = -((z / 8) & 0xFFFF);
    if (c[1] & 0x80) {
        base.vx = 0;
        base.vz = 0;
        base.vy = (s8)c[0] << 12;
    } else {
        base.vx = 0x10000;
        base.vz = 0;
        base.vy = (s8)c[4] << 12;
    }
    interpolateWorldPosition(&normal, x, z);
    averageVector(&pos, &base, &normal);
    return pos.vy << 3;
}

/* Noah: getWorldmapElevationWithWater (worldmapTaskYggdrasil.cpp; ground height with the water wave) */
s32 getWorldmapElevationWithWater(s32 x, s32 z) {
    sWorldmapScratch* sp;
    u8* c;
    u32 cx;
    u32 cz;
    s32 x0;
    s32 z0;
    s32 x1;
    s32 z1;
    s32 t;
    s32 zc;
    s32 ux;
    s32 uz;

    c = getWorldChunkForPosition(x, z);
    sp = (sWorldmapScratch*)PSX_SCRATCH(0x1F800000);
    cx = ((u32)x >> 19) & 7;
    x0 = cx * 0x200;
    cz = ((u32)z >> 19) & 7;
    z0 = cz * 0x200;
    t = worldmapWaterWave + x0;
    zc = (getAngleCos(worldmapWaterWave2 + z0) << 4) >> 3;
    sp->mA0.vy = (getAngleCos(t) * zc >> 20) + (s8)c[0] * 8;
    x1 = (cx + 1) * 0x200;
    t = worldmapWaterWave + x1;
    zc = (getAngleCos(worldmapWaterWave2 + z0) << 4) >> 3;
    sp->mA8.vy = (getAngleCos(t) * zc >> 20) + (s8)c[4] * 8;
    z1 = (cz + 1) * 0x200;
    t = worldmapWaterWave + x0;
    zc = (getAngleCos(worldmapWaterWave2 + z1) << 4) >> 3;
    sp->mB0_quadCorner2.vy = (getAngleCos(t) * zc >> 20) + (s8)c[0x24] * 8;
    t = worldmapWaterWave + x1;
    zc = (getAngleCos(worldmapWaterWave2 + z1) << 4) >> 3;
    sp->mB8_quadCorner3.vy = (getAngleCos(t) * zc >> 20) + (s8)c[0x28] * 8;
    ux = (x / 8) & 0xFFFF;
    uz = (z / 8) & 0xFFFF;
    if (c[1] & 0x80) {
        if (ux * worldmapCellDiagonalNormal.vx + -uz * worldmapCellDiagonalNormal.vz < 0) {
            sp->m0.vx = 0x80;
            sp->m0.vz = -0x80;
            sp->m10.vx = 0;
            sp->m10.vz = -0x80;
            sp->m0.vy = sp->mB8_quadCorner3.vy - sp->mA0.vy;
            sp->m10.vy = sp->mB0_quadCorner2.vy - sp->mA0.vy;
        } else {
            sp->m0.vx = 0x80;
            sp->m10.vx = 0x80;
            sp->m0.vz = 0;
            sp->m10.vz = -0x80;
            sp->m0.vy = sp->mA8.vy - sp->mA0.vy;
            sp->m10.vy = sp->mB8_quadCorner3.vy - sp->mA0.vy;
        }
    } else {
        if ((ux - 0x10000) * worldmapCellAntiDiagonalNormal.vx + -uz * worldmapCellAntiDiagonalNormal.vz < 0) {
            sp->m0.vx = -0x80;
            sp->m0.vz = -0x80;
            sp->m10.vx = -0x80;
            sp->m10.vz = 0;
            sp->m0.vy = sp->mB0_quadCorner2.vy - sp->mA8.vy;
            sp->m10.vy = sp->mA0.vy - sp->mA8.vy;
        } else {
            sp->m0.vx = 0;
            sp->m0.vz = -0x80;
            sp->m10.vx = -0x80;
            sp->m10.vz = -0x80;
            sp->m0.vy = sp->mB8_quadCorner3.vy - sp->mA8.vy;
            sp->m10.vy = sp->mB0_quadCorner2.vy - sp->mA8.vy;
        }
    }
    OuterProduct0(&sp->m10, &sp->m0, &sp->m20);
    VectorNormal(&sp->m20, &sp->m0);
    sp->m10.vx = (x >> 12) & 0x7F;
    sp->m10.vz = -((z >> 12) & 0x7F);
    if (c[1] & 0x80) {
        sp->m20.vx = 0;
        sp->m20.vz = 0;
        sp->m20.vy = sp->mA0.vy;
    } else {
        sp->m20.vx = 0x80;
        sp->m20.vz = 0;
        sp->m20.vy = sp->mA8.vy;
    }
    averageVector(&sp->m10, &sp->m20, &sp->m0);
    return sp->m10.vy << 12;
}

s16 getWorldmapMaterial(VECTOR* p) {
    s16* m = (s16*)(WM_CHUNK((s16)(((p->vz >> 20) / 8) * worldmapSizeX + (p->vx >> 20) / 8)) + 0x510);
    return m[((p->vz & 0x7FF000) >> 19) << 4 | (p->vx & 0x7FF000) >> 19];
}

s16 getWorldmapGroundType(VECTOR* p) {
    s16 m;
    s32 i;
    s32 dx;
    s32 dz;

    m = getWorldmapMaterial(p);
    i = m & 0xF;
    dx = (u16)(p->vx / 8) - worldmapMaterialSplitOrigins[i].vx;
    dz = (u16)(p->vz / 8) - worldmapMaterialSplitOrigins[i].vz;
    if ((dx * worldmapMaterialSplitNormals[i].vx >> 12) + (dz * worldmapMaterialSplitNormals[i].vz >> 12) > 0) {
        return ((u16)m >> 4) & 7;
    }
    return ((u16)m >> 7) & 7;
}

s32 getWorldmapMaterial2(VECTOR* p) {
    return getWorldmapMaterial(p) & 0xF;
}

/* material bits 10-15 */
s32 func_80094004(VECTOR* p) {
    return (u32)(getWorldmapMaterial(p) << 16) >> 26;
}

/* chunk byte 3 bits 2-5 */
s32 getWorldmapEncounterZone(VECTOR* p) {
    return (getWorldChunkForPosition(p->vx, p->vz)[3] >> 2) & 0xF;
}

s16 checkWorldmapPositionSub1_0_1(s16 a, s16 b) {
    return ((s16(*)[8])worldmapVehicleGroundTypeTable)[a][b];
}

s32 checkWorldmapPositionSub1_1(VECTOR* collision, VECTOR* step, VECTOR* out) {
    s32 i;
    s32 dot;

    i = (s16)getWorldmapMaterial2(collision);
    dot = step->vx * worldmapMaterialWallDirections[i].vx + step->vz * worldmapMaterialWallDirections[i].vz;
    if (dot == 0) {
        out->vx = step->vx;
        out->vz = step->vz;
        return 0;
    }
    if (dot < 0) {
        out->vx = -worldmapMaterialWallDirections[i].vx;
        out->vz = -worldmapMaterialWallDirections[i].vz;
    } else {
        out->vx = worldmapMaterialWallDirections[i].vx;
        out->vz = worldmapMaterialWallDirections[i].vz;
    }
    return 1;
}

/* 2D distance */
s32 computeWorldmapDistance2D(VECTOR* a, VECTOR* b) {
    s32 dx;
    s32 dz;

    dx = abs(a->vx - b->vx) >> 12;
    dz = abs(a->vz - b->vz) >> 12;
    return SquareRoot0(dx * dx + dz * dz);
}

/* angle + direction between two points */
void selectYggdrasilExitDirection(VECTOR* a, VECTOR* b, VECTOR* out, s16* angle) {
    s32 dx = b->vx - a->vx;
    s32 dz = b->vz - a->vz;
    *angle = (ratan2(dz, dx) + 0x400) & 0xFFF;
    out->vx = getAngleCos(*angle);
    out->vz = -getAngleSin(*angle);
}

/* Noah: setupWorldmapExits (worldmapExit.cpp) */
s32 setupWorldmapExits(VECTOR* pos, s32 set) {
    u16 x;
    u16 z;
    sWorldmapExitDef* e;

    x = pos->vx >> 12;
    e = worldmapExitsDefs[set];
    z = pos->vz >> 12;
    while (e->m8_destinationField != -1) {
        if ((e->m0_X <= x) & (x <= e->m0_X + e->m4_width) & (e->m2_Y <= z) & (z <= e->m2_Y + e->m6_height)) {
            if (e->mE_type == 4) {
                worldmapCurrentExit = (sWorldmapExitDef*)-1;
                worldmapExitVar0 = -1;
                worldmapExitVar1 = e->mC_newWorldmapMode;
            } else {
                worldmapCurrentExit = e;
                worldmapExitVar1 = -1;
                worldmapExitVar0 = e->mC_newWorldmapMode;
            }
            return 1;
        }
        e++;
    }
    worldmapCurrentExit = (sWorldmapExitDef*)-1;
    worldmapExitVar0 = -1;
    worldmapExitVar1 = -1;
    return 0;
}

/* exit of the given type under pos (worldmapExit.cpp style; no Noah name) */
s32 findWorldmapExitOfType(VECTOR* pos, s32 set, s32 type) {
    u16 x;
    u16 z;
    sWorldmapExitDef* e;

    x = pos->vx >> 12;
    e = worldmapExitsDefs[set];
    z = pos->vz >> 12;
    while (e->m8_destinationField != -1) {
        if ((e->m0_X <= x) & (x <= e->m0_X + e->m4_width) & (e->m2_Y <= z) & (z <= e->m2_Y + e->m6_height)) {
            if (e->mE_type == type) {
                worldmapCurrentExit = e;
                return 1;
            }
        }
        e++;
    }
    return 0;
}

/* empty; was C already */
void func_80094434(void) {
}

/* Noah: checkWorldmapPositionSub1_0_3 (crossing a cell edge in +x) */
s32 checkWorldmapPositionSub1_0_3(VECTOR* pos, VECTOR* step, VECTOR* out, s16 type) {
    s32 slope;

    slope = (step->vz << 12) / step->vx;
    out[0].vx = (out[1].vx & 0xFFF80000) - pos->vx;
    out[2].vx = out[0].vx - 1;
    out[3].vx = out[0].vx;
    out[2].vz = (slope * out[2].vx >> 12) + pos->vz;
    out[3].vz = (slope * out[3].vx >> 12) + pos->vz;
    out[2].vx += pos->vx;
    out[3].vx += pos->vx;
    checkWorldmapPositionSub0(&out[2]);
    if (checkWorldmapPositionSub1_0_1(type, getWorldmapGroundType(&out[2])) == 0) {
        out[0] = out[2];
        return 1;
    }
    checkWorldmapPositionSub0(&out[3]);
    return -(checkWorldmapPositionSub1_0_1(type, getWorldmapGroundType(&out[3])) == 0) & 3;
}

/* Noah: checkWorldmapPositionSub1_0_4 (crossing a cell edge in -x) */
s32 checkWorldmapPositionSub1_0_4(VECTOR* pos, VECTOR* step, VECTOR* out, s16 type) {
    s32 slope;

    slope = (step->vz << 12) / step->vx;
    out[0].vx = (pos->vx & 0xFFF80000) - pos->vx;
    out[2].vx = out[0].vx;
    out[3].vx = out[0].vx - 1;
    out[2].vz = (slope * out[2].vx >> 12) + pos->vz;
    out[3].vz = (slope * out[3].vx >> 12) + pos->vz;
    out[2].vx += pos->vx;
    out[3].vx += pos->vx;
    checkWorldmapPositionSub0(&out[2]);
    if (checkWorldmapPositionSub1_0_1(type, getWorldmapGroundType(&out[2])) == 0) {
        out[0] = out[2];
        return 1;
    }
    checkWorldmapPositionSub0(&out[3]);
    return -(checkWorldmapPositionSub1_0_1(type, getWorldmapGroundType(&out[3])) == 0) & 3;
}

/* Noah: checkWorldmapPositionSub1_0_5 (crossing a cell edge in +z) */
s32 checkWorldmapPositionSub1_0_5(VECTOR* pos, VECTOR* step, VECTOR* out, s16 type) {
    s32 slope;

    slope = (step->vx << 12) / step->vz;
    out[0].vz = (out[1].vz & 0xFFF80000) - pos->vz;
    out[2].vz = out[0].vz - 1;
    out[3].vz = out[0].vz;
    out[2].vx = (slope * out[2].vz >> 12) + pos->vx;
    out[3].vx = (slope * out[3].vz >> 12) + pos->vx;
    out[2].vz += pos->vz;
    out[3].vz += pos->vz;
    checkWorldmapPositionSub0(&out[2]);
    if (checkWorldmapPositionSub1_0_1(type, getWorldmapGroundType(&out[2])) == 0) {
        out[0] = out[2];
        return 1;
    }
    checkWorldmapPositionSub0(&out[3]);
    return (checkWorldmapPositionSub1_0_1(type, getWorldmapGroundType(&out[3])) == 0) << 1;
}

/* Noah: checkWorldmapPositionSub1_0_2 (crossing a cell edge in -z) */
s32 checkWorldmapPositionSub1_0_2(VECTOR* pos, VECTOR* step, VECTOR* out, s16 type) {
    s32 slope;

    slope = (step->vx << 12) / step->vz;
    out[0].vz = (pos->vz & 0xFFF80000) - pos->vz;
    out[2].vz = out[0].vz;
    out[3].vz = out[0].vz - 1;
    out[2].vx = (slope * out[2].vz >> 12) + pos->vx;
    out[3].vx = (slope * out[3].vz >> 12) + pos->vx;
    out[2].vz += pos->vz;
    out[3].vz += pos->vz;
    checkWorldmapPositionSub0(&out[2]);
    if (checkWorldmapPositionSub1_0_1(type, getWorldmapGroundType(&out[2])) == 0) {
        out[0] = out[2];
        return 1;
    }
    checkWorldmapPositionSub0(&out[3]);
    return (checkWorldmapPositionSub1_0_1(type, getWorldmapGroundType(&out[3])) == 0) << 1;
}
