/* PC projection macros shared by both retail compiler shapes. Included in
 * each regional branch deliberately; the native definitions stay untouched.
 * All shading and its safe points precede these macros and still run once.
 * A rescued quad completes the SDK helper's early-returned projection here,
 * with no additional scheduler yield, then restores the exact early state. */
#define CUSTOM_RESCUE_BEGIN() do { \
    if (!pcGeometryRescue()) break; \
    u8* extra = pcGeometryAlloc(dst, (((P_TAG*)dst)->len + 1) * 4 + PRIM_OFS(0)); \
    if (!extra) break; \
    gteSave(&geometrySaved); \
    geometryRescue = 1; dst = extra; \
} while (0)

#define TRI(sxy1, sxy2, TAIL) { \
    s32 flag; \
    gte_ldv3((SVECTOR*)v0, v1, v2); \
    gte_rtpt(); gte_stflg(&flag); gte_nclip(); \
    if (flag & 0x4000) break; \
    gte_stopz(&opz); gte_avsz3(); \
    if (opz <= 0) { CUSTOM_RESCUE_BEGIN(); if (!geometryRescue) break; } \
    gte_stotz(&otz); \
    otz = (otz >> gDepthDivider) + depthBias; \
    if (otz < 5) otz = 5; \
    if (otz >= 0x1000) break; \
    gte_stsxy3(dst + PRIM_OFS(8), dst + PRIM_OFS(sxy1), dst + PRIM_OFS(sxy2)); \
    addPrim(&ot[otz], dst); TAIL break; \
}

#define QUAD(sxy1, sxy2, sxy3, TAIL) \
    opz = RotAverageNclip4((SVECTOR*)v0, v1, v2, v3, \
        (s32*)(dst + PRIM_OFS(8)), (s32*)(dst + PRIM_OFS(sxy1)), \
        (s32*)(dst + PRIM_OFS(sxy2)), (s32*)(dst + PRIM_OFS(sxy3)), &p, &otz, &flag); \
    if (opz <= 0) { \
        CUSTOM_RESCUE_BEGIN(); if (!geometryRescue) break; \
        gte_stsxy3(dst + PRIM_OFS(8), dst + PRIM_OFS(sxy1), dst + PRIM_OFS(sxy2)); \
        gte_ldv0(v3); gte_rtps(); \
        gte_stsxy(dst + PRIM_OFS(sxy3)); \
        flag |= gteCfc2(31); gte_avsz4(); gte_stotz(&otz); \
    } \
    otz = (otz >> gDepthDivider) + depthBias; \
    if (otz < 5) otz = 5; \
    if ((flag & 0x8000) || otz >= 0x1000) break; \
    addPrim(&ot[otz], dst); TAIL break;

#define TPAGE_TAIL if (semiTrans != 0) { \
    if (geometryRescue) pcGeometryBlend(dst, semiTrans - 1); \
    else addCustomPolyTPagePrim(&ot[otz]); \
}
