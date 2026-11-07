/*
 * Field BSS blocks that the game C reads both as one object and through names
 * inside it (symbol-file or splat names at fixed PS1 addresses). On the PS1
 * those names are addresses inside the block; on the PC the block's layout
 * differs (pointers and primitive tags are 8 bytes), so each name becomes an
 * alias at its member's PC offset. tools/pc_link.py would otherwise give every
 * name its own storage, and the two views would drift apart.
 */
#include "common.h"
#include "psyq.h"
#include "field/field.h"
#include "field/screen_effects.h"

/* The blocks, at their PS1 extents. Defined, not tentative, so they have a
 * section the aliases can refer to. */
FieldBss_800AF880 fieldSceneState = { 0 };
POLY_FT4* debugCameraDigitPolys[2] = { 0 };
FieldQuad fieldCompassVar2[25] = { 0 };
DR_MODE gDialogDrModes[2][16] = { 0 };
FieldScreenEffects fieldScreenEffects = { 0 };
sFieldRenderContext fieldRenderContext[2] = { 0 };

/* The loaded sound-effect sequence (f_8008083C.c, FieldSequence*). The PS1
 * keeps it in op9DVar1's padding (FieldBss_800B2184 + 0x1D8); on the PC it is
 * 8 bytes, which would run over partyToFollowStructMapping, so it gets its
 * own storage instead of an alias into op9DVar1. */
void* D_800B235C = 0;

/* name = (char*)&block + offset. Emitted from a function only because
 * top-level asm takes no operands. */
#define ALIAS_AT(name, block, offset) \
    __asm__(".globl " #name "\n.set " #name ", " #block " + %c0" : : "i"(offset))
#define OFFSET(block, member) __builtin_offsetof(__typeof__(block), member)
#define ALIAS(name, block, member) ALIAS_AT(name, block, OFFSET(block, member))

static void __attribute__((used)) fieldBssAliases(void) {
    /* fieldSceneState: camera, script and walkmesh state. (The field debugger reads the
     * camera vectors' high halves through casts now, no aliases.) */
    ALIAS(op99Var5, fieldSceneState, m104_op99Var5);
    ALIAS(op99Var6, fieldSceneState, m108_op99Var6);
    ALIAS(camera2Tan, fieldSceneState, m10C_camera2Tan);
    ALIAS(cameraTan, fieldSceneState, m10E_cameraTan);
    ALIAS(computeProjectionMatrixAngles, fieldSceneState, m150_projectionAngles);
    ALIAS(op99Var7, fieldSceneState, m158_op99Var7);
    ALIAS(cameraLimits, fieldSceneState, m15C_cameraLimits);
    ALIAS(opA0_var1, fieldSceneState, m170_opA0_var1);
    ALIAS(DollySet, fieldSceneState, m174_dollySet);
    ALIAS(DollyStop, fieldSceneState, m175_dollyStop);
    ALIAS(sceneSCRZ, fieldSceneState, m178_sceneSCRZ);
    ALIAS(sceneDIP, fieldSceneState, m17C_sceneDIP);
    ALIAS(sceneScale, fieldSceneState, m17E_sceneScale);
    ALIAS(opA0_var0, fieldSceneState, m18C_opA0_var0);
    ALIAS(fieldCameraShakeCounter, fieldSceneState, m1AA_cameraShakeCounter);
    ALIAS(D_800AFA54, fieldSceneState, m1D4_projectionAngles); /* the sFieldState view (packed on the PC to match) */
    ALIAS(g_currentProjectionMatrix, fieldSceneState, m1E4_projectionMatrix);
    ALIAS(totalObjects, fieldSceneState, m28C_totalObjects);
    ALIAS(D_800AFB10, fieldSceneState, m290_actorArray);
    ALIAS(fieldActorSetupParams, fieldSceneState, m29C_actorSetupParams);
    ALIAS(walkMeshVar1, fieldSceneState, m2A0_walkMeshVar1);
    ALIAS(walkMeshTriangle, fieldSceneState, m2A4_walkMeshTriangle);
    ALIAS(walkMeshVertices, fieldSceneState, m2B4_walkMeshVertices);
    ALIAS(g_walkMeshNumTrianglePerBlock, fieldSceneState, m2C4_walkMeshNumTrianglePerBlock);

    /* A DR_MODE per frame buffer, linked after the screen distortion strips. */
    ALIAS_AT(D_800B1E18, gDialogDrModes, 3 * sizeof(DR_MODE));
}

/* The sFieldState view must see FieldBss_800AF880's pointers. */
typedef char fieldStateViewCheck[OFFSET(fieldSceneState, m1D4_projectionAngles) +
                                 __builtin_offsetof(sFieldState, mC8_actorSetupParams) ==
                                 OFFSET(fieldSceneState, m29C_actorSetupParams) ? 1 : -1];
