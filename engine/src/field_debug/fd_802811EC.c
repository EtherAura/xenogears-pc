/* The dev-kit field debugger: 13 debug pages (scene, memory, events, CPU
 * time, script variables, particle editor, items, encounters, fog, poly
 * counts), debug bounding-box lines, a frame timing log, a camera tweak
 * and print helpers. gcc-2.7.2-psx, ASPSX 2.34. */
#include "common.h"
#include "psyq.h"
#include "main/game_state.h"
#include "main/debug_font.h"
#include "field/field.h"
#include "field_debug/field_debug.h"

extern s32 fieldDebugDisable;
extern s32 fieldMouseState[5]; /* main: mouse state (getMouseState): [2] buttons, [3] dx, [4] dy */
extern sFieldRenderContext* pCurrentFieldRenderingContext;
extern s32 g_frameOddOrEven;
extern s32 fieldProfileTimestamp;
extern u16 padButtonForScripts;                  /* Noah: padButtonForScripts[0].m0_buttons */
extern u16 padButtonForScriptsPad1;
extern s32 EntityMoveCheck0Var0;
extern s32 updateEntityEventCode3Var1;
extern s32 op99Var5;
extern s32 op99Var6;
extern s32 opA0_var1;
extern s16 sceneDIP;
extern s16 sceneScale;
extern u16 padButtonForField2;
extern MATRIX g_currentProjectionMatrix;

extern FieldBss_800AF880 fieldSceneState;
extern FieldBss_800B2184 op9DVar1;
extern s32 totalObjects;
extern s32 particleEditorCursorLine;                  /* particle editor: cursor line */
extern s32 particleEditorCursorComponent;                  /* particle editor: cursor component */
extern s32 fieldDebugPage;                  /* debug page (1-13) */
extern s32 fieldDebugPageToggle;
extern u16 g_padButtonForField;                  /* g_padButtonForField (held) */
extern s16 isFogSetup;
extern s32 fieldCpuTime;                  /* CPU time */
extern s32 fieldGpuTime;                  /* GPU time */
extern s32 fieldPolyCount;                  /* poly count */
extern s32 fieldPolyCount2;
extern s32 g_playerControlledActor;                  /* player actor index */
extern u32 debugFaderMode;
extern s32 debugFaderR;
extern s32 debugFaderG;
extern s32 debugFaderB;
extern s16 updateCharacterVar1;
extern s32 sceneSCRZ;
extern s32 fieldMapNumber;
extern u8 DollySet;
extern u8 DollyStop;
extern s32 currentlyLoadedWave;
extern s32 currentlyLoadedMusic;
extern s32 g_totalActors;
extern GameState* pKernelGameState;
extern s32 particleVar1;                  /* Noah: currentParticleCreatorId */
extern s32 particleEditorConfigIndex;                  /* particle editor: current config (0-7) */
extern u8 encounterProbabilityWeight[];
extern s32 fieldDebugPageCursor;
extern s32 D_80285980;
extern s32 fieldDebugLinesVisible;
extern s32 fieldDebugEncounterShown;
extern s32 fieldDebugEncounterTimer;
extern s32 fieldDebugEncounterCount;
extern s32 fieldDebugLastEncounter;
extern s16 fieldDebugTimeEntryCount;
extern FieldDebugTimeEntry fieldDebugTimeEntries[47]; /* to fieldDebugEncounterHistogram */
extern s16 fieldDebugEncounterHistogram[16];
extern FieldDebugLine fieldDebugLines[16];

void setDebugFontColor(s32 r, s32 g, s32 b); /* debug font colour */
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
void dumpHeapBlocks(s32, s32, s32, s32);
s32 getTotalFreeHeap(void);
void func_80071D08(s32 index, s32 steps, s32 r, s32 g, s32 b, s32 mode); /* field setupRGBCalcMode */
void func_80073E38(void);               /* field setupObjectRenderModes */
void func_8008E718(void);               /* field setInitialEncounterData */
u16 func_8009744C(void);                /* field getPlayerCharacterDir */
u16 func_8009A514(void);                /* field getCameraDirection */
s32 func_800A3018(s32 index);           /* field getVariable */
void func_800A3F4C(void);               /* field saveFieldStateToSaveBlock */
void func_800A98E8(s32, s32);           /* field stopParticleEffect */
void func_800A99A8(s32);                /* field createParticleEffect */
void updateParticleEditorPage(void);
void printDebugComponentCursor(s32 line, s32 cursor, s32 active);
s32 printDebugLineCursor(s32 line, s32 cursor, s32* selected);
void editParticleEditorValue(s32 comp, s32 line);
void printWdsBankName(s32 bank);
s32 adjustDebugValue(s32 value, s32 min, s32 max);
void setDebugS16Component(s16* v, s32 index, s32 value);
s32 getDebugS16Component(s16* v, s32 index);
void setDebugU8Component(u8* v, s32 index, s32 value);
s32 getDebugU8Component(u8* v, s32 index);
void setDebugS8Component(s8* v, s32 index, s32 value);
s32 getDebugS8Component(s8* v, s32 index);
s32 updateFieldDebugPages(OTag* ot);
void renderFieldDebugLine(OTag* ot, FieldDebugLine* line, MATRIX* m, s32 odd);

void resetFieldDebugPageCursor(void) {
    D_80285980 = 0;
    fieldDebugPageCursor = 0;
}

void logFieldDebugEncounter(s32 encounter) {
    fieldDebugEncounterShown = 1;
    fieldDebugEncounterTimer = 0x3C;
    fieldDebugLastEncounter = encounter;
    fieldDebugEncounterHistogram[encounter]++;
    fieldDebugEncounterCount++;
}

void resetFieldDebugEncounterStats(void) {
    s32 i;

    fieldDebugTimeEntryCount = 0;
    fieldDebugEncounterShown = 0;
    fieldDebugEncounterTimer = 0x1E;
    fieldDebugEncounterCount = 0;
    for (i = 0; i < 16; i++) {
        fieldDebugEncounterHistogram[i] = 0;
    }
}

void initFieldDebugLines(void) {
    s32 i;

    for (i = 0; i < 16; i++) {
        setVector(&fieldDebugLines[i].m20_rotation, 0, 0, 0);
        setVector(&fieldDebugLines[i].m28_position, 0, 0, 0);
        SetLineG2(&fieldDebugLines[i].m40_line[0]);
        setRGB0(&fieldDebugLines[i].m40_line[0], 0x80, 0x80, 0x80);
        setRGB1(&fieldDebugLines[i].m40_line[0], 0xFF, 0xFF, 0xFF);
        fieldDebugLines[i].m40_line[1] = fieldDebugLines[i].m40_line[0];
    }
    setRGB0(&fieldDebugLines[1].m40_line[0], 0x80, 0, 0);
    setRGB1(&fieldDebugLines[1].m40_line[0], 0xFF, 0, 0);
    setRGB0(&fieldDebugLines[1].m40_line[1], 0x80, 0, 0);
    setRGB1(&fieldDebugLines[1].m40_line[1], 0xFF, 0, 0);
}

void renderFieldDebugText(void) {
    fieldDebugLinesVisible = 0;
    if (updateFieldDebugPages(&pCurrentFieldRenderingContext->mCC_OT[1])) {
        printDebugTextOnScreen(&pCurrentFieldRenderingContext->mCC_OT[1]);
    }
    fieldDebugTimeEntryCount = 0;
}

void renderFieldDebugLines(void) {
    s32 i;

    if (fieldDebugDisable == 0 && fieldDebugLinesVisible != 0) {
        for (i = 0; i < 12; i++) {
            renderFieldDebugLine(pCurrentFieldRenderingContext->mCC_OT, &fieldDebugLines[i], &fieldDebugLines[i].m0_matrix, g_frameOddOrEven);
        }
    }
}

void renderFieldDebugLine(OTag* ot, FieldDebugLine* line, MATRIX* m, s32 odd) {
    SVECTOR v;
    s32 sxy;
    s32 p;
    s32 flag;
    LINE_G2* prim;

    prim = &line->m40_line[odd];
    PushMatrix();
    SetRotMatrix(m);
    SetTransMatrix(m);
    RotTransPers3(&line->m30_start, &line->m38_end, &v, (s32*)&prim->x0, (s32*)&prim->x1, &sxy, &p, &flag);
    addPrim(&ot[1], prim);
    PopMatrix();
}

void updateFieldDebugLineMatrices(void) {
    s32 i;
    s32 flag;

    if (fieldDebugDisable == 0) {
        for (i = 0; i < 16; i++) {
            createRotationMatrix(&fieldDebugLines[i].m20_rotation, &fieldDebugLines[i].m0_matrix);
            PushMatrix();
            MulMatrix2(&g_currentProjectionMatrix, &fieldDebugLines[i].m0_matrix);
            PopMatrix();
            RotTrans(&fieldDebugLines[i].m28_position, (VECTOR*)fieldDebugLines[i].m0_matrix.t, &flag);
        }
    }
}

void setupFieldDebugBoundingBox(sFieldScriptEntity* e) {
    s32 i;

    if (fieldDebugDisable == 0) {
        for (i = 0; i < 12; i++) {
            fieldDebugLines[i].m28_position.vx = e->m20_position.vx >> 16;
            fieldDebugLines[i].m28_position.vy = e->m20_position.vy >> 16;
            fieldDebugLines[i].m28_position.vz = e->m20_position.vz >> 16;
            fieldDebugLines[i].m30_start.vy = 0;
            fieldDebugLines[i].m38_end.vy = 0;
        }
        fieldDebugLines[0].m30_start.vx = -e->m18_boundingVolume[0];
        fieldDebugLines[0].m30_start.vz = -e->m18_boundingVolume[2];
        fieldDebugLines[0].m38_end.vx = e->m18_boundingVolume[0];
        fieldDebugLines[0].m38_end.vz = -e->m18_boundingVolume[2];

        fieldDebugLines[1].m30_start.vx = e->m18_boundingVolume[0];
        fieldDebugLines[1].m30_start.vz = -e->m18_boundingVolume[2];
        fieldDebugLines[1].m38_end.vx = e->m18_boundingVolume[0];
        fieldDebugLines[1].m38_end.vz = e->m18_boundingVolume[2];

        fieldDebugLines[2].m30_start.vx = e->m18_boundingVolume[0];
        fieldDebugLines[2].m30_start.vz = e->m18_boundingVolume[2];
        fieldDebugLines[2].m38_end.vx = -e->m18_boundingVolume[0];
        fieldDebugLines[2].m38_end.vz = e->m18_boundingVolume[2];

        fieldDebugLines[3].m30_start.vx = -e->m18_boundingVolume[0];
        fieldDebugLines[3].m30_start.vz = e->m18_boundingVolume[2];
        fieldDebugLines[3].m38_end.vx = -e->m18_boundingVolume[0];
        fieldDebugLines[3].m38_end.vz = -e->m18_boundingVolume[2];

        fieldDebugLines[4].m30_start.vx = -e->m18_boundingVolume[0];
        fieldDebugLines[4].m30_start.vz = -e->m18_boundingVolume[2];
        fieldDebugLines[4].m38_end.vx = e->m18_boundingVolume[0];
        fieldDebugLines[4].m38_end.vz = -e->m18_boundingVolume[2];
        fieldDebugLines[4].m30_start.vy = -e->m18_boundingVolume[1];
        fieldDebugLines[4].m38_end.vy = -e->m18_boundingVolume[1];

        fieldDebugLines[5].m30_start.vx = e->m18_boundingVolume[0];
        fieldDebugLines[5].m30_start.vz = -e->m18_boundingVolume[2];
        fieldDebugLines[5].m38_end.vx = e->m18_boundingVolume[0];
        fieldDebugLines[5].m38_end.vz = e->m18_boundingVolume[2];
        fieldDebugLines[5].m30_start.vy = -e->m18_boundingVolume[1];
        fieldDebugLines[5].m38_end.vy = -e->m18_boundingVolume[1];

        fieldDebugLines[6].m30_start.vx = e->m18_boundingVolume[0];
        fieldDebugLines[6].m30_start.vz = e->m18_boundingVolume[2];
        fieldDebugLines[6].m38_end.vx = -e->m18_boundingVolume[0];
        fieldDebugLines[6].m38_end.vz = e->m18_boundingVolume[2];
        fieldDebugLines[6].m30_start.vy = -e->m18_boundingVolume[1];
        fieldDebugLines[6].m38_end.vy = -e->m18_boundingVolume[1];

        fieldDebugLines[7].m30_start.vx = -e->m18_boundingVolume[0];
        fieldDebugLines[7].m30_start.vz = e->m18_boundingVolume[2];
        fieldDebugLines[7].m38_end.vx = -e->m18_boundingVolume[0];
        fieldDebugLines[7].m38_end.vz = -e->m18_boundingVolume[2];
        fieldDebugLines[7].m30_start.vy = -e->m18_boundingVolume[1];
        fieldDebugLines[7].m38_end.vy = -e->m18_boundingVolume[1];
    }
}

void printFieldDebugRect(RECT* r) {
    if (fieldDebugDisable != 1) {
        sprintf_screen("REC %07d %07d %07d %07d\n", r->x, r->y, r->w, r->h);
    }
}

/* JP prints the matrix in hex. */
#if VERSION_JP
#define FIELD_DEBUG_MTX_ROW0 "MTX %05x %05x %05x %05x\n"
#define FIELD_DEBUG_MTX_ROW "    %05x %05x %05x %05x\n"
#else
#define FIELD_DEBUG_MTX_ROW0 "MTX %06d %06d %06d %06d\n"
#define FIELD_DEBUG_MTX_ROW "    %06d %06d %06d %06d\n"
#endif

void printFieldDebugMatrix(MATRIX* m) {
    if (fieldDebugDisable != 1) {
        sprintf_screen(FIELD_DEBUG_MTX_ROW0, m->m[0][0], m->m[0][1], m->m[0][2], m->t[0]);
        sprintf_screen(FIELD_DEBUG_MTX_ROW, m->m[1][0], m->m[1][1], m->m[1][2], m->t[1]);
        sprintf_screen(FIELD_DEBUG_MTX_ROW, m->m[2][0], m->m[2][1], m->m[2][2], m->t[2]);
    }
}

void printFieldDebugVector(VECTOR* v) {
    if (fieldDebugDisable != 1) {
        sprintf_screen("VEC  %d %d %d\n", v->vx, v->vy, v->vz);
    }
}

void printFieldDebugSVector(SVECTOR* v) {
    if (fieldDebugDisable != 1) {
        sprintf_screen("SVEC %d %d %d\n", v->vx, v->vy, v->vz);
    }
}

void logFieldRenderingEvent(const char* name) {
    s32 t;

    if (fieldDebugDisable == 0) {
        t = VSync(1);
        fieldDebugTimeEntries[fieldDebugTimeEntryCount].m4_name = name;
        fieldDebugTimeEntries[fieldDebugTimeEntryCount].m0_time = t - fieldProfileTimestamp;
        fieldDebugTimeEntryCount++;
        fieldProfileTimestamp = VSync(1);
    }
}

s32 updateFieldDebugPages(OTag* ot) {
    s32 i;
    s32 j;
    s32 delta;
    s32 flags;
    Wds* wds;
    SeqFile* seq;
    s32* p;
    s16* q;
    u8* pb;

    if (fieldDebugDisable == 1) {
        return 0;
    }
    for (i = 0; i < totalObjects; i++) {
        if ((fieldSceneState.m290_actorArray[i].m58_flags & 0x2000) && fieldSceneState.m290_actorArray[i].m0_model->m14_dynamicVertices == NULL) {
            sprintf_screen("MIME ERROR %d\n", i);
        }
    }
    wds = pLoadedWdsLinkedList;
    i = 0;
loop1:
    if (wds != NULL) {
#if VERSION_JP
        sprintf_screen("%d W=%x   ", i++, wds);
        printWdsBankName(wds->m20_bankNumber);
        sprintf_screen("\n");
#endif
        wds = wds->m2C_next;
        goto loop1;
    }
    seq = pLoadedSequencesLinkedList;
    i = 0;
loop2:
    if (seq != NULL) {
#if VERSION_JP
        sprintf_screen("%d E=%x\n", i, seq);
#endif
        seq = SEQ_NEXT(seq);
        i++;
        goto loop2;
    }
    if (fieldDebugPage >= 14) {
        fieldDebugPage = 0;
    }
    if (padButtonForField2 & 0x800) {
        if (fieldDebugPageToggle & 1) {
            fieldDebugPage = 7;
        } else {
            fieldDebugPage = 0;
        }
        fieldDebugPageToggle = (fieldDebugPageToggle + 1) | 0x8000;
    }
    if ((g_padButtonForField & 0x800) && (padButtonForScripts & 0x40)) {
        if ((u32)(fieldDebugPage++ - 7) < 2) {
            fieldDebugPage = 10;
        }
        if ((u32)(fieldDebugPage - 11) < 2) {
            if (fieldDebugPage == 11) {
                isFogSetup = 1;
            } else {
                isFogSetup = 0;
            }
            func_80073E38();
        }
        fieldDebugPageCursor = 0;
        D_80285980 = 0;
        if (fieldDebugPage >= 14) {
            fieldDebugPage = 0;
        }
    }
    switch (fieldDebugPage) {
    case 12:
        sprintf_screen("\nCPU=%04d GPU=%04d\n", fieldCpuTime, fieldGpuTime);
        sprintf_screen("PolyCount %d / %d\n", fieldPolyCount, fieldPolyCount2);
        sprintf_screen("Pos X%6d Z=%6d Y=%6d\n", fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m20_position.vx >> 16,
                      fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m20_position.vz >> 16,
                      fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m20_position.vy >> 16);
        goto free_size;
    case 13:
        sprintf_screen("RGB CALC\n\n");
        if (g_padButtonForField & 2) {
            if (--fieldDebugPageCursor < 0) {
                fieldDebugPageCursor = 3;
            }
        }
        if (g_padButtonForField & 1) {
            if (++fieldDebugPageCursor >= 4) {
                fieldDebugPageCursor = 0;
            }
        }
        delta = 0;
        if (fieldMouseState[2] == 8) {
            delta = fieldMouseState[4];
        }
        if (fieldDebugPageCursor == 0) {
            sprintf_screen(">MODE %d\n", ((debugFaderMode += delta) >> 4) & 3);
        } else {
            sprintf_screen(" MODE %d\n", (debugFaderMode >> 4) & 3);
        }
        if (fieldDebugPageCursor == 1) {
            sprintf_screen(">R %d\n", debugFaderR = (debugFaderR + delta) & 0xFF);
        } else {
            sprintf_screen(" R %d\n", debugFaderR);
        }
        if (fieldDebugPageCursor == 2) {
            sprintf_screen(">G %d\n", debugFaderG = (debugFaderG + delta) & 0xFF);
        } else {
            sprintf_screen(" G %d\n", debugFaderG);
        }
        if (fieldDebugPageCursor == 3) {
            sprintf_screen(">B %d\n", debugFaderB = (debugFaderB + delta) & 0xFF);
        } else {
            sprintf_screen(" B %d\n", debugFaderB);
        }
        func_80071D08(1, 1, debugFaderR, debugFaderG, debugFaderB, (debugFaderMode >> 4) & 3);
        break;
    case 11:
        if (g_padButtonForField & 2) {
            if (--fieldDebugPageCursor < 0) {
                fieldDebugPageCursor = 7;
            }
        }
        if (g_padButtonForField & 1) {
            if (++fieldDebugPageCursor >= 8) {
                fieldDebugPageCursor = 0;
            }
        }
        delta = 0;
        if (fieldMouseState[2] == 8) {
            delta = fieldMouseState[4];
        }
        if (fieldDebugPageCursor == 0) {
            sprintf_screen(">NearColor R=%d\n", op9DVar1.mC_fogNearColor[0] += delta);
        } else {
            sprintf_screen(" NearColor R=%d\n", op9DVar1.mC_fogNearColor[0]);
        }
        if (fieldDebugPageCursor == 1) {
            sprintf_screen(">          G=%d\n", op9DVar1.mC_fogNearColor[1] += delta);
        } else {
            sprintf_screen("           G=%d\n", op9DVar1.mC_fogNearColor[1]);
        }
        if (fieldDebugPageCursor == 2) {
            sprintf_screen(">          B=%d\n", op9DVar1.mC_fogNearColor[2] += delta);
        } else {
            sprintf_screen("           B=%d\n", op9DVar1.mC_fogNearColor[2]);
        }
        if (fieldDebugPageCursor == 3) {
            sprintf_screen(">FarColor  R=%d\n", op9DVar1.m10_fogFarColor[0] += delta);
        } else {
            sprintf_screen(" FarColor  R=%d\n", op9DVar1.m10_fogFarColor[0]);
        }
        if (fieldDebugPageCursor == 4) {
            sprintf_screen(">          G=%d\n", op9DVar1.m10_fogFarColor[1] += delta);
        } else {
            sprintf_screen("           G=%d\n", op9DVar1.m10_fogFarColor[1]);
        }
        if (fieldDebugPageCursor == 5) {
            sprintf_screen(">          B=%d\n", op9DVar1.m10_fogFarColor[2] += delta);
        } else {
            sprintf_screen("           B=%d\n", op9DVar1.m10_fogFarColor[2]);
        }
        if (fieldDebugPageCursor == 6) {
            sprintf_screen(">Near        %d\n", op9DVar1.m14_fogNear += delta * 10);
        } else {
            sprintf_screen(" Near        %d\n", op9DVar1.m14_fogNear);
        }
        if (fieldDebugPageCursor == 7) {
            sprintf_screen(">Far         %d\n", op9DVar1.m16_fogFar += delta * 10);
        } else {
            sprintf_screen(" Far         %d\n", op9DVar1.m16_fogFar);
        }
        break;
    case 1:
        sprintf_screen("---------- Player Info -----\n");
        sprintf_screen("Pos X%6d Z%6d Y%6d\n", fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m20_position.vx >> 16,
                      fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m20_position.vz >> 16,
                      fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m20_position.vy >> 16);
        sprintf_screen("Pol=%d Pri=%d ID=%x:%x\n",
                      fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m8_currentWalkMeshTriangle[fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m10_walkmeshId],
                      fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m10_walkmeshId,
                      walkMeshTriangle[fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m10_walkmeshId]
                                [fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m8_currentWalkMeshTriangle[fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m10_walkmeshId]]
                                    .mC_indexInWalkmeshData1,
                      fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m14_currentTriangleFlag);
        sprintf_screen("P0=%d P1=%d P2=%d C=%d\n", fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m8_currentWalkMeshTriangle[0],
                      fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m8_currentWalkMeshTriangle[1],
                      fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m8_currentWalkMeshTriangle[2], updateCharacterVar1);
        sprintf_screen("MFflag=%x MFlag2=%x N=%d\n", fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m0_fieldScriptFlags,
                      fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m4_flags,
                      fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m74_touchedActor);
        sprintf_screen("\n---------- Scene Info ------\n");
        sprintf_screen("SCRZ=%d DIP=%d Scale=%d\n", sceneSCRZ, sceneDIP, sceneScale);
        sprintf_screen("CamDIR=%d ChrDIR=%d MapNum=%d\n\n", func_8009A514(), func_8009744C(), fieldMapNumber & 0x3FFF);
        sprintf_screen("Cam AT   X%6d Z%6d Y%6d\n", ((s16*)&fieldSceneState.m10_cameraAt.vx)[1], ((s16*)&fieldSceneState.m10_cameraAt.vz)[1], ((s16*)&fieldSceneState.m10_cameraAt.vy)[1]);
        sprintf_screen("Cam EYE  X%6d Z%6d Y%6d\n", ((s16*)&fieldSceneState.m0_cameraEye.vx)[1], ((s16*)&fieldSceneState.m0_cameraEye.vz)[1], ((s16*)&fieldSceneState.m0_cameraEye.vy)[1]);
        sprintf_screen("Cam AT2  X%6d Z%6d Y%6d\n", ((s16*)&fieldSceneState.m40_cameraAt2.vx)[1], ((s16*)&fieldSceneState.m40_cameraAt2.vz)[1], ((s16*)&fieldSceneState.m40_cameraAt2.vy)[1]);
        sprintf_screen("Cam EYE2 X%6d Z%6d Y%6d\n", ((s16*)&fieldSceneState.m30_cameraEye2.vx)[1], ((s16*)&fieldSceneState.m30_cameraEye2.vz)[1], ((s16*)&fieldSceneState.m30_cameraEye2.vy)[1]);
        sprintf_screen("DollySet=%02x DollyStop=%02x\n", DollySet, DollyStop);
        sprintf_screen("Angle=%d\n", fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy);
        sprintf_screen("Length=%d (%d)\n", (sceneSCRZ * sceneScale) >> 12, (sceneSCRZ * sceneScale * 2) >> 12);
        sprintf_screen("Wave=%02x Music=%02x\n", currentlyLoadedWave, currentlyLoadedMusic);
        sprintf_screen("Total Aactor =%d\n", g_totalActors);
        sprintf_screen("Total Object =%d\n", totalObjects);
        break;
    case 2:
        sprintf_screen("---------- Memory Info -----\n");
        dumpHeapBlocks(0, fieldDebugPageCursor, 0xF, 0xDC);
        if (g_padButtonForField & 1) {
            fieldDebugPageCursor += 4;
        }
        if (g_padButtonForField & 2) {
            fieldDebugPageCursor -= 4;
        }
    free_size:
        sprintf_screen("Free Size=%x\n", getTotalFreeHeap());
        break;
    case 3:
        sprintf_screen("---------- Event Info ------\n");
        sprintf_screen("Event  Time=%d:%d\n", func_800A3018(10) >> 8, func_800A3018(10) & 0xFF);
        sprintf_screen("System Time=%d:%d:%d\n", func_800A3018(14), func_800A3018(12) >> 8, func_800A3018(12) & 0xFF);
        for (i = 0; i < 11; i++) {
            sprintf_screen("Num=%x HP=%3d MP=%2d\n", i, pKernelGameState->m26C_party[i].m4C_HP, pKernelGameState->m26C_party[i].m50_MP);
        }
        sprintf_screen("Gold=%d\n", pKernelGameState->m1924_gold);
        sprintf_screen("SinarioFlag=%d\n", (u16)fieldVars[0]);
        sprintf_screen("Party=%d %d %d\n", currentParty[0], currentParty[1], currentParty[2]);
        j = pKernelGameState->m1D30_partyMemberBitField;
        sprintf_screen("Member ");
        for (i = 0; i < 11; i++) {
            if (j & 1) {
                sprintf_screen("%d ", i);
            }
            j >>= 1;
        }
        sprintf_screen("\n");
        j = pKernelGameState->m1D32_partyFrameMask;
        sprintf_screen("FrMask ");
        for (i = 0; i < 11; i++) {
            if (!(j & 1)) {
                sprintf_screen("%d ", i);
            }
            j >>= 1;
        }
        sprintf_screen("\n");
        j = pKernelGameState->m2318_partyFrameLock;
        sprintf_screen("FrLock ");
        for (i = 0; i < 11; i++) {
            if (j & 1) {
                sprintf_screen("%d ", i);
            }
            j >>= 1;
        }
        sprintf_screen("\n");
        sprintf_screen("GearRide=%d %d %d\n", pKernelGameState->m22B1_isOnGear[0], pKernelGameState->m22B1_isOnGear[1], pKernelGameState->m22B1_isOnGear[2]);
        sprintf_screen("GearNum=");
        for (i = 0; i < 3; i++) {
            if (currentParty[i] == 0xFF) {
                break;
            }
            sprintf_screen(" %d", pKernelGameState->m26C_party[currentParty[i]].mA0_gearNum);
        }
        sprintf_screen("\nTYPE=");
        for (i = 0; i < 3; i++) {
            if (currentParty[i] == 0xFF) {
                break;
            }
            if (partyFieldActorIds[i] == 0xFF) {
                break;
            }
            switch ((fieldSceneState.m290_actorArray[partyFieldActorIds[i]].m4C_scriptEntity->m0_fieldScriptFlags >> 8) & 7) {
            case 1:
                sprintf_screen("People ");
                break;
            case 2:
                sprintf_screen("Robo ");
                break;
            case 4:
                sprintf_screen("Play ");
                break;
            default:
                sprintf_screen("?%d ", (fieldSceneState.m290_actorArray[partyFieldActorIds[i]].m4C_scriptEntity->m0_fieldScriptFlags >> 8) & 7);
                break;
            }
        }
        sprintf_screen(" ID=");
        flags = fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m14_currentTriangleFlag;
        if (!(flags & 0x80)) {
            sprintf_screen("C");
        } else {
            sprintf_screen("-");
        }
        if (!(flags & 0x40)) {
            sprintf_screen("G");
        } else {
            sprintf_screen("-");
        }
        if (!(flags & 0x20)) {
            sprintf_screen("P");
        } else {
            sprintf_screen("-");
        }
        break;
    case 4:
        sprintf_screen("---------- Event DEBUG -----\n");
        for (i = fieldDebugPageCursor, j = 0; i < g_totalActors; i++, j++) {
            sprintf_screen("ActNum=%3d RUN=%04x\n", i,
                          fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m8C_scriptSlots[fieldSceneState.m290_actorArray[i].m4C_scriptEntity->mCE_currentScriptSlot].m0_scriptPC);
            sprintf_screen("P0=%d P1=%d P2=%d P=%d I=%x:%x\n", fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m8_currentWalkMeshTriangle[0],
                          fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m8_currentWalkMeshTriangle[1],
                          fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m8_currentWalkMeshTriangle[2],
                          fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m10_walkmeshId,
                          walkMeshTriangle[fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m10_walkmeshId]
                                    [fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m8_currentWalkMeshTriangle[fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m10_walkmeshId]]
                                        .mC_indexInWalkmeshData1,
                          fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m14_currentTriangleFlag);
            sprintf_screen("Pos X%6d Z%6d Y%6d\n", fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m20_position.vx >> 16,
                          fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m20_position.vz >> 16,
                          fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m20_position.vy >> 16);
            sprintf_screen("M1=%x M2=%x", fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m0_fieldScriptFlags,
                          fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m4_flags);
            if (!(fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m4_flags & 0x4000000)) {
                sprintf_screen("\n\n");
            } else {
                sprintf_screen(" TALK OFF\n\n");
            }
            if (j >= 6) {
                break;
            }
        }
        if (g_padButtonForField & 1) {
            fieldDebugPageCursor++;
        }
        if (g_padButtonForField & 2) {
            fieldDebugPageCursor--;
        }
        break;
    case 5:
        sprintf_screen("---------- CPU Time --------\n");
        for (i = 0; i < fieldDebugTimeEntryCount; i++) {
            sprintf_screen("%s = %6d\n", fieldDebugTimeEntries[i].m4_name, fieldDebugTimeEntries[i].m0_time);
        }
        sprintf_screen("\nCPU=%6d GPU=%6d\n", fieldCpuTime, fieldGpuTime);
        sprintf_screen("PolyCount %d / %d\n", fieldPolyCount, fieldPolyCount2);
        break;
    case 6:
        sprintf_screen("---------- RAM MAP ---------\n");
        for (i = fieldDebugPageCursor, j = 0; i < 0x400; i++, j++) {
            sprintf_screen("ADD %04x:%08x %06d\n", i * 2, func_800A3018(i * 2), func_800A3018(i * 2));
            if (j >= 16) {
                break;
            }
        }
        if (g_padButtonForField & 1) {
            fieldDebugPageCursor += 4;
        }
        if (g_padButtonForField & 2) {
            fieldDebugPageCursor -= 4;
        }
        break;
    case 7:
        sprintf_screen("---------- PARTICLE -----------\n");
        if (padButtonForField2 & 0x100) {
            func_800A98E8(op9DVar1.mE8_playerControlledActor, 1);
            for (i = 0; i < 8; i++) {
                if (particleVar1 == 0xFF) {
                    particleCreationSetup.m0_configs[i].m52_actorId = op9DVar1.mE8_playerControlledActor;
                } else {
                    particleCreationSetup.m0_configs[i].m52_actorId = particleVar1;
                }
            }
            func_800A99A8(op9DVar1.mE8_playerControlledActor);
        }
        updateParticleEditorPage();
        break;
    case 8:
        sprintf_screen("---------- ITEM -------------\n");
        for (i = fieldDebugPageCursor, j = 0; i < 0x96; i += 4, j++) {
            sprintf_screen("%03d=%03d %03d=%03d %03d=%03d %03d=%03d\n",
                          pKernelGameState->m2026_inventory0Ids[i], pKernelGameState->m1F90_inventory0Counts[i],
                          pKernelGameState->m2026_inventory0Ids[i + 1], pKernelGameState->m1F90_inventory0Counts[i + 1],
                          pKernelGameState->m2026_inventory0Ids[i + 2], pKernelGameState->m1F90_inventory0Counts[i + 2],
                          pKernelGameState->m2026_inventory0Ids[i + 3], pKernelGameState->m1F90_inventory0Counts[i + 3]);
            if (j >= 16) {
                break;
            }
        }
        if (g_padButtonForField & 1) {
            fieldDebugPageCursor += 4;
        }
        if (g_padButtonForField & 2) {
            fieldDebugPageCursor -= 4;
        }
        break;
    case 9:
        sprintf_screen("---------- ACC --------------\n");
        for (i = fieldDebugPageCursor, j = 0; i < 0xC8; i += 4, j++) {
            sprintf_screen("%03d=%03d %03d=%03d %03d=%03d %03d=%03d\n",
                          pKernelGameState->m1EC8_inventory2Ids[i], pKernelGameState->m1E00_inventory2Counts[i],
                          pKernelGameState->m1EC8_inventory2Ids[i + 1], pKernelGameState->m1E00_inventory2Counts[i + 1],
                          pKernelGameState->m1EC8_inventory2Ids[i + 2], pKernelGameState->m1E00_inventory2Counts[i + 2],
                          pKernelGameState->m1EC8_inventory2Ids[i + 3], pKernelGameState->m1E00_inventory2Counts[i + 3]);
            if (j >= 16) {
                break;
            }
        }
        if (g_padButtonForField & 1) {
            fieldDebugPageCursor += 4;
        }
        if (g_padButtonForField & 2) {
            fieldDebugPageCursor -= 4;
        }
        break;
    case 10:
        sprintf_screen("---------- ENCOUNT -------------\n");
        if (g_padButtonForField & 1) {
            fieldDebugPageCursor++;
        }
        if (g_padButtonForField & 2) {
            fieldDebugPageCursor--;
        }
        for (i = 0, q = fieldDebugEncounterHistogram, pb = encounterProbabilityWeight; i < 16; i++) {
            sprintf_screen("%2d %3d %d\n", i, *pb, *q);
            q++;
            pb++;
        }
        switch (fieldDebugPageCursor & 3) {
        case 0:
            sprintf_screen(">TIME   =%d\n", op9DVar1.m114_encounterTimer);
            sprintf_screen(" ENCOUNT=%d\n", op9DVar1.m118_encounterCount);
            sprintf_screen(" SET");
            if (g_padButtonForField & 4) {
                op9DVar1.m114_encounterTimer++;
            }
            if (g_padButtonForField & 8) {
                op9DVar1.m114_encounterTimer--;
            }
            break;
        case 1:
            sprintf_screen(" TIME   =%d\n", op9DVar1.m114_encounterTimer);
            sprintf_screen(">ENCOUNT=%d\n", op9DVar1.m118_encounterCount);
            sprintf_screen(" SET");
            if (g_padButtonForField & 4) {
                op9DVar1.m118_encounterCount++;
            }
            if (g_padButtonForField & 8) {
                op9DVar1.m118_encounterCount--;
            }
            op9DVar1.m118_encounterCount &= 0x1F;
            break;
        case 2:
            sprintf_screen(" TIME   =%d\n", op9DVar1.m114_encounterTimer);
            sprintf_screen(" ENCOUNT=%d\n", op9DVar1.m118_encounterCount);
            sprintf_screen(">SET");
            func_8008E718();
            break;
        }
        if (fieldDebugEncounterShown != 0) {
            if (--fieldDebugEncounterTimer == 0) {
                fieldDebugEncounterShown = 0;
            }
            sprintf_screen("COUNT=%d NUM=%d\n", fieldDebugEncounterCount, fieldDebugLastEncounter);
        } else {
            fieldDebugEncounterTimer = 60;
        }
        func_800A3F4C();
        break;
    }
    if (fieldDebugPageCursor < 0) {
        fieldDebugPageCursor = 0;
    }
    return fieldDebugPage;
}

void updateParticleEditorPage(void) {
    s32 line;
    s32 comp;
    s32 n;
    s32 sel;
    s32 i;
    const char* str;

    line = particleEditorCursorLine;
    comp = particleEditorCursorComponent;
    if (line < 22) {
        n = 0;
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("BANK    = %d\n", particleEditorConfigIndex);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("MAX     = %d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m6_maxParticles);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("SWAIT   = %d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m2_sWait);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("EWAIT   = %d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m4_eWait);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("SPOS    =");
        printDebugComponentCursor(0, comp, sel);
        sprintf_screen("%d", particleCreationSetup.m0_configs[particleEditorConfigIndex].mC_sPos.vx);
        printDebugComponentCursor(1, comp, sel);
        sprintf_screen("%d", particleCreationSetup.m0_configs[particleEditorConfigIndex].mC_sPos.vy);
        printDebugComponentCursor(2, comp, sel);
        sprintf_screen("%d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].mC_sPos.vz);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("EPOS    =");
        printDebugComponentCursor(0, comp, sel);
        sprintf_screen("%d", particleCreationSetup.m0_configs[particleEditorConfigIndex].m14_ePos.vx);
        printDebugComponentCursor(1, comp, sel);
        sprintf_screen("%d", particleCreationSetup.m0_configs[particleEditorConfigIndex].m14_ePos.vy);
        printDebugComponentCursor(2, comp, sel);
        sprintf_screen("%d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m14_ePos.vz);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("SPEED   = ");
        printDebugComponentCursor(0, comp, sel);
        sprintf_screen("%d * ", particleCreationSetup.m0_configs[particleEditorConfigIndex].m8_speed);
        printDebugComponentCursor(1, comp, sel);
        sprintf_screen("%d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m24_speedMultiplier);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("GRAVITE =");
        printDebugComponentCursor(0, comp, sel);
        sprintf_screen("%d", particleCreationSetup.m0_configs[particleEditorConfigIndex].m1C_gravity.vx);
        printDebugComponentCursor(1, comp, sel);
        sprintf_screen("%d", particleCreationSetup.m0_configs[particleEditorConfigIndex].m1C_gravity.vy);
        printDebugComponentCursor(2, comp, sel);
        sprintf_screen("%d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m1C_gravity.vz);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("SRANGE  = %d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m26_sRange);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("ERANGE  = %d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m28_eRange);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("PSWAIT  = %d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m56_psWait);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("PEWAIT  = %d\n", (u16)particleCreationSetup.m0_configs[particleEditorConfigIndex].m58_peWait);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("SHAPE   = %d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m54_shape);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("SCALE   =");
        printDebugComponentCursor(0, comp, sel);
        sprintf_screen("%d", particleCreationSetup.m0_configs[particleEditorConfigIndex].m5A_scale.vx);
        printDebugComponentCursor(1, comp, sel);
        sprintf_screen("%d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m5A_scale.vy);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("SCALEOFS=");
        printDebugComponentCursor(0, comp, sel);
        sprintf_screen("%d", particleCreationSetup.m0_configs[particleEditorConfigIndex].m62_scaleOffset.vx);
        printDebugComponentCursor(1, comp, sel);
        sprintf_screen("%d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m62_scaleOffset.vy);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("COLOR   =");
        printDebugComponentCursor(0, comp, sel);
        sprintf_screen("%d", particleCreationSetup.m0_configs[particleEditorConfigIndex].m6A_color[0]);
        printDebugComponentCursor(1, comp, sel);
        sprintf_screen("%d", particleCreationSetup.m0_configs[particleEditorConfigIndex].m6A_color[1]);
        printDebugComponentCursor(2, comp, sel);
        sprintf_screen("%d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m6A_color[2]);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("COLOROFS=");
        printDebugComponentCursor(0, comp, sel);
        sprintf_screen("%d", particleCreationSetup.m0_configs[particleEditorConfigIndex].m6E_colorOffset[0]);
        printDebugComponentCursor(1, comp, sel);
        sprintf_screen("%d", particleCreationSetup.m0_configs[particleEditorConfigIndex].m6E_colorOffset[1]);
        printDebugComponentCursor(2, comp, sel);
        sprintf_screen("%d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m6E_colorOffset[2]);
        n = printDebugLineCursor(n, line, &sel);
        if (!(particleCreationSetup.m0_configs[particleEditorConfigIndex].m2A_flags & 1)) {
            sprintf_screen("RANDROT = OFF\n");
        } else {
            sprintf_screen("RANDROT = ON\n");
        }
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("SORT    = ");
        switch ((particleCreationSetup.m0_configs[particleEditorConfigIndex].m2A_flags >> 1) & 3) {
        case 0:
            sprintf_screen("TOP\n");
            break;
        case 1:
            sprintf_screen("MID\n");
            break;
        case 2:
            sprintf_screen("NORMAL\n");
            break;
        case 3:
            sprintf_screen("BACK\n");
            break;
        }
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("COLMODE = ");
        switch ((particleCreationSetup.m0_configs[particleEditorConfigIndex].m2A_flags >> 8) & 3) {
        case 0:
            sprintf_screen("1.0*Bk + 1.0*Fw\n");
            break;
        case 1:
            sprintf_screen("1.0*Bk - 1.0*Fw\n");
            break;
        case 2:
            sprintf_screen("1.0*Bk + 0.25*Fw\n");
            break;
        case 3:
            sprintf_screen("0.5*Bk + 0.5*Fw\n");
            break;
        }
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("ROTANGLE= %d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m76_rotationAngle);
        n = printDebugLineCursor(n, line, &sel);
        sprintf_screen("RANGEMOD= ");
        switch ((particleCreationSetup.m0_configs[particleEditorConfigIndex].m2A_flags >> 6) & 3) {
        case 0:
            sprintf_screen("RANDUM (0)");
            break;
        case 2:
            sprintf_screen("CIRCLE (1)");
            break;
        case 1:
            sprintf_screen("LINE (2)");
            break;
        }
        sprintf_screen("ROTANGLE= %d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m76_rotationAngle);
    } else {
        n = 22;
        for (i = 0; i < 8; i++) {
            n = printDebugLineCursor(n, line, &sel);
            sprintf_screen("ANGOFFS%d=", i);
            printDebugComponentCursor(0, comp, sel);
            sprintf_screen("%d", particleCreationSetup.m0_configs[particleEditorConfigIndex].m30_angleOffsets[i].vx);
            printDebugComponentCursor(1, comp, sel);
            sprintf_screen("%d\n", particleCreationSetup.m0_configs[particleEditorConfigIndex].m30_angleOffsets[i].vy);
        }
    }
    setDebugFontColor(0xFF, 0xFF, 0xFF);
    if (padButtonForField2 & 0x4000) {
        comp = 0;
        if (line < 29) {
            line++;
        }
    }
    if (padButtonForField2 & 0x1000) {
        comp = 0;
        if (line > 0) {
            line--;
        }
    }
    if (padButtonForField2 & 0x2000) {
        if ((u32)(line - 13) < 2) {
            if (comp <= 0) {
                comp++;
            }
        } else if (comp < 2) {
            comp++;
        }
    }
    if (padButtonForField2 & 0x8000) {
        if (comp > 0) {
            comp--;
        }
    }
    editParticleEditorValue(comp, line);
    particleEditorCursorLine = line;
    particleEditorCursorComponent = comp;
}

void printDebugComponentCursor(s32 line, s32 cursor, s32 active) {
    if (cursor == line && active == 1) {
        sprintf_screen(">");
    } else {
        sprintf_screen(" ");
    }
}

s32 printDebugLineCursor(s32 line, s32 cursor, s32* selected) {
    if (cursor == line) {
        setDebugFontColor(0, 0xFF, 0xFF);
        sprintf_screen(">");
        *selected = 1;
    } else {
        setDebugFontColor(0x40, 0x40, 0x40);
        sprintf_screen(" ");
        *selected = 0;
    }
    line++;
    return line;
}

s32 adjustDebugValue(s32 value, s32 min, s32 max) {
    s32 step;

    step = 1;
    if (padButtonForScriptsPad1 & 4) {
        step = 10;
    }
    if (padButtonForScriptsPad1 & 1) {
        step = 100;
    }
    if (padButtonForScriptsPad1 & 2) {
        step = 1000;
    }
    if (padButtonForField2 & 0x80) {
        value -= step;
        if (value < min) {
            value = min;
        }
    }
    if (padButtonForField2 & 0x20) {
        value += step;
        if (max < value) {
            value = max;
        }
    }
    return value;
}

void setDebugS16Component(s16* v, s32 index, s32 value) {
    switch (index) {
    case 0:
        v[0] = value;
        break;
    case 1:
        v[1] = value;
        break;
    case 2:
        v[2] = value;
        break;
    }
}

s32 getDebugS16Component(s16* v, s32 index) {
    switch (index) {
    case 0:
        return v[0];
    case 1:
        return v[1];
    case 2:
        return v[2];
    }
    return 0;
}

void setDebugU8Component(u8* v, s32 index, s32 value) {
    switch (index) {
    case 0:
        v[0] = value;
        break;
    case 1:
        v[1] = value;
        break;
    case 2:
        v[2] = value;
        break;
    }
}

s32 getDebugU8Component(u8* v, s32 index) {
    switch (index) {
    case 0:
        return v[0];
    case 1:
        return v[1];
    case 2:
        return v[2];
    }
    return 0;
}

void setDebugS8Component(s8* v, s32 index, s32 value) {
    switch (index) {
    case 0:
        v[0] = value;
        break;
    case 1:
        v[1] = value;
        break;
    case 2:
        v[2] = value;
        break;
    }
}

s32 getDebugS8Component(s8* v, s32 index) {
    switch (index) {
    case 0:
        return v[0];
    case 1:
        return v[1];
    case 2:
        return v[2];
    }
    return 0;
}

void editParticleEditorValue(s32 comp, s32 line) {
    s32 f;
    s32 a;

    switch (line) {
    case 0:
        particleEditorConfigIndex = adjustDebugValue(particleEditorConfigIndex, 0, 7);
        break;
    case 1:
        particleCreationSetup.m0_configs[particleEditorConfigIndex].m6_maxParticles = adjustDebugValue(particleCreationSetup.m0_configs[particleEditorConfigIndex].m6_maxParticles, 0, 0xFF);
        break;
    case 2:
        particleCreationSetup.m0_configs[particleEditorConfigIndex].m2_sWait = adjustDebugValue(particleCreationSetup.m0_configs[particleEditorConfigIndex].m2_sWait, 0, 0x7FFF);
        break;
    case 3:
        particleCreationSetup.m0_configs[particleEditorConfigIndex].m4_eWait = adjustDebugValue(particleCreationSetup.m0_configs[particleEditorConfigIndex].m4_eWait, 1, 0x7FFF);
        break;
    case 4:
        setDebugS16Component(&particleCreationSetup.m0_configs[particleEditorConfigIndex].mC_sPos.vx, comp,
                      adjustDebugValue(getDebugS16Component(&particleCreationSetup.m0_configs[particleEditorConfigIndex].mC_sPos.vx, comp), -0x8000, 0x7FFF));
        break;
    case 5:
        setDebugS16Component(&particleCreationSetup.m0_configs[particleEditorConfigIndex].m14_ePos.vx, comp,
                      adjustDebugValue(getDebugS16Component(&particleCreationSetup.m0_configs[particleEditorConfigIndex].m14_ePos.vx, comp), -0x8000, 0x7FFF));
        break;
    case 6:
        if (comp == 0) {
            particleCreationSetup.m0_configs[particleEditorConfigIndex].m8_speed = adjustDebugValue(particleCreationSetup.m0_configs[particleEditorConfigIndex].m8_speed, -0x8000, 0x7FFF);
        } else {
            particleCreationSetup.m0_configs[particleEditorConfigIndex].m24_speedMultiplier = adjustDebugValue(particleCreationSetup.m0_configs[particleEditorConfigIndex].m24_speedMultiplier, 1, 0x7FFF);
        }
        break;
    case 7:
        setDebugS16Component(&particleCreationSetup.m0_configs[particleEditorConfigIndex].m1C_gravity.vx, comp,
                      adjustDebugValue(getDebugS16Component(&particleCreationSetup.m0_configs[particleEditorConfigIndex].m1C_gravity.vx, comp), -0x8000, 0x7FFF));
        break;
    case 8:
        particleCreationSetup.m0_configs[particleEditorConfigIndex].m26_sRange = adjustDebugValue(particleCreationSetup.m0_configs[particleEditorConfigIndex].m26_sRange, 0, 0xFFFF);
        break;
    case 9:
        particleCreationSetup.m0_configs[particleEditorConfigIndex].m28_eRange = adjustDebugValue(particleCreationSetup.m0_configs[particleEditorConfigIndex].m28_eRange, 0, 0xFFFF);
        break;
    case 10:
        particleCreationSetup.m0_configs[particleEditorConfigIndex].m56_psWait = adjustDebugValue(particleCreationSetup.m0_configs[particleEditorConfigIndex].m56_psWait, 1, 0x7FFF);
        break;
    case 11:
        particleCreationSetup.m0_configs[particleEditorConfigIndex].m58_peWait = adjustDebugValue((u16)particleCreationSetup.m0_configs[particleEditorConfigIndex].m58_peWait, 1, 0x7FFF);
        break;
    case 12:
        particleCreationSetup.m0_configs[particleEditorConfigIndex].m54_shape = adjustDebugValue(particleCreationSetup.m0_configs[particleEditorConfigIndex].m54_shape, 0, 0x7FFF);
        break;
    case 13:
        setDebugS16Component(&particleCreationSetup.m0_configs[particleEditorConfigIndex].m5A_scale.vx, comp,
                      adjustDebugValue(getDebugS16Component(&particleCreationSetup.m0_configs[particleEditorConfigIndex].m5A_scale.vx, comp), -0x8000, 0x7FFF));
        break;
    case 14:
        setDebugS16Component(&particleCreationSetup.m0_configs[particleEditorConfigIndex].m62_scaleOffset.vx, comp,
                      adjustDebugValue(getDebugS16Component(&particleCreationSetup.m0_configs[particleEditorConfigIndex].m62_scaleOffset.vx, comp), -0x8000, 0x7FFF));
        break;
    case 15:
        setDebugU8Component(particleCreationSetup.m0_configs[particleEditorConfigIndex].m6A_color, comp,
                      adjustDebugValue(getDebugU8Component(particleCreationSetup.m0_configs[particleEditorConfigIndex].m6A_color, comp), 0, 0xFF));
        break;
    case 16:
        setDebugS8Component(particleCreationSetup.m0_configs[particleEditorConfigIndex].m6E_colorOffset, comp,
                      adjustDebugValue(getDebugS8Component(particleCreationSetup.m0_configs[particleEditorConfigIndex].m6E_colorOffset, comp), -0x80, 0x7F));
        break;
    case 17:
        f = (s16)particleCreationSetup.m0_configs[particleEditorConfigIndex].m2A_flags;
        a = f & 1;
        f &= 0xFFFE;
        f |= adjustDebugValue(a, 0, 1);
        particleCreationSetup.m0_configs[particleEditorConfigIndex].m2A_flags = f;
        break;
    case 18:
        f = (s16)particleCreationSetup.m0_configs[particleEditorConfigIndex].m2A_flags;
        a = (f >> 1) & 3;
        f &= 0xFFF9;
        f |= adjustDebugValue(a, 0, 3) << 1;
        particleCreationSetup.m0_configs[particleEditorConfigIndex].m2A_flags = f;
        break;
    case 19:
        f = (s16)particleCreationSetup.m0_configs[particleEditorConfigIndex].m2A_flags;
        a = (f >> 8) & 3;
        f &= 0xFCFF;
        f |= adjustDebugValue(a, 0, 3) << 8;
        particleCreationSetup.m0_configs[particleEditorConfigIndex].m2A_flags = f;
        break;
    case 20:
        particleCreationSetup.m0_configs[particleEditorConfigIndex].m76_rotationAngle = adjustDebugValue(particleCreationSetup.m0_configs[particleEditorConfigIndex].m76_rotationAngle, 0, 0xFFF);
        break;
    case 21:
        f = (s16)particleCreationSetup.m0_configs[particleEditorConfigIndex].m2A_flags;
        a = (f >> 6) & 3;
        f &= 0xFF3F;
        f |= adjustDebugValue(a, 0, 2) << 6;
        particleCreationSetup.m0_configs[particleEditorConfigIndex].m2A_flags = f;
        break;
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
        if (comp == 0) {
            particleCreationSetup.m0_configs[particleEditorConfigIndex].m30_angleOffsets[line - 22].vx = adjustDebugValue(particleCreationSetup.m0_configs[particleEditorConfigIndex].m30_angleOffsets[line - 22].vx, -0x8000, 0x7FFF);
        } else {
            particleCreationSetup.m0_configs[particleEditorConfigIndex].m30_angleOffsets[line - 22].vy = adjustDebugValue(particleCreationSetup.m0_configs[particleEditorConfigIndex].m30_angleOffsets[line - 22].vy, -0x8000, 0x7FFF);
        }
        break;
    }
}

void updateFieldDebugMouseCamera(void) {
    s32 t;
    if ((padButtonForScripts & 1) && (padButtonForScripts & 0x40)) {
        if (fieldMouseState[2] == 4) {
            updateEntityEventCode3Var1 = 1;
            EntityMoveCheck0Var0 += fieldMouseState[4];
        } else if (fieldMouseState[2] == 8) {
            t = sceneDIP;
            t += ((u32)fieldMouseState[4] << 4) >> 5; /* retail: unsigned shift */
            op99Var5 = 1;
            op99Var6 = 1;
            sceneDIP = t;
        } else {
            op99Var5 = 1;
            op99Var6 = 1;
            sceneScale += fieldMouseState[4] << 4;
            opA0_var1 += fieldMouseState[3] << 18;
            fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy = opA0_var1 >> 16;
        }
    }
}

s32 fieldDebugUploadTextureToVram(s32* data, s16 modeA, s16 xA, s16 yA, s16 modeB, s16 xB, s16 yB) {
    RECT r;
    s32 i;
    s32 count;
    u16* p;
    s32 tag;

    count = data[0];
    p = (u16*)&data[count + 1];
    for (i = 0; i < count; i++) {
        tag = *(s32*)p;
        p += 2;
        if (tag == 0x1100) {
            switch (modeA) {
            case 1:
                r.x = xA + p[2];
                r.y = yA + p[3];
                break;
            case 2:
                r.x = xA + p[0] + p[2];
                r.y = yA + p[1] + p[3];
                break;
            default:
                r.x = p[0] + p[2];
                r.y = p[1] + p[3];
                break;
            }
        } else if (tag != 0x1101) {
            return 1;
        } else {
            switch (modeB) {
            case 1:
                r.x = xB + p[2];
                r.y = yB + p[3];
                break;
            case 2:
                r.x = xB + p[0] + p[2];
                r.y = yB + p[1] + p[3];
                break;
            default:
                r.x = p[0] + p[2];
                r.y = p[1] + p[3];
                break;
            }
        }
        p += 4;
        r.w = *p++;
        r.h = *p++;
        LoadImage(&r, p);
        /* do/while (0) (a macro in the original?): its loop notes keep sched2
         * from moving the i++ into the mult/mflo stall slots. */
        do {
            p += r.w * r.h;
        } while (0);
    }
    return 0;
}

void printWdsBankName(s32 bank) {
    switch (bank) {
    case 0x0:
        sprintf_screen("main_se");
        break;
    case 0x1:
        sprintf_screen("bat_se");
        break;
    case 0x2:
        sprintf_screen("gear_se");
        break;
    case 0x3:
        sprintf_screen("ambi");
        break;
    case 0x4:
        sprintf_screen("ambi2");
        break;
    case 0x5:
        sprintf_screen("ambi3");
        break;
    case 0x6:
        sprintf_screen("ambi4");
        break;
    case 0x20:
        sprintf_screen("minato");
        break;
    case 0x21:
        sprintf_screen("lahan");
        break;
    case 0x22:
        sprintf_screen("jyukai");
        break;
    case 0x23:
        sprintf_screen("shitan");
        break;
    case 0x24:
        sprintf_screen("musi");
        break;
    case 0x25:
        sprintf_screen("church");
        break;
    case 0x26:
        sprintf_screen("battle2");
        break;
    case 0x27:
        sprintf_screen("chuchu");
        break;
    case 0x28:
        sprintf_screen("over");
        break;
    case 0x29:
        sprintf_screen("orgel");
        break;
    case 0x2A:
        sprintf_screen("battle3");
        break;
    case 0x2B:
        sprintf_screen("ajito");
        break;
    case 0x2C:
        sprintf_screen("emerada");
        break;
    case 0x2D:
        sprintf_screen("ellie");
        break;
    case 0x2E:
        sprintf_screen("world");
        break;
    case 0x2F:
        sprintf_screen("sad");
        break;
    case 0x30:
        sprintf_screen("ave");
        break;
    case 0x31:
        sprintf_screen("ellie2");
        break;
    case 0x32:
        sprintf_screen("balto");
        break;
    case 0x33:
        sprintf_screen("dajil");
        break;
    case 0x34:
        sprintf_screen("maria1");
        break;
    case 0x35:
        sprintf_screen("maria2");
        break;
    case 0x36:
        sprintf_screen("heshu");
        break;
    case 0x37:
        sprintf_screen("kaisou");
        break;
    case 0x38:
        sprintf_screen("pinch");
        break;
    case 0x39:
        sprintf_screen("porgan");
        break;
    case 0x3A:
        sprintf_screen("babel");
        break;
    case 0x3B:
        sprintf_screen("solachu");
        break;
    case 0x3C:
        sprintf_screen("shinnyu");
        break;
    case 0x3D:
        sprintf_screen("inbou");
        break;
    case 0x3E:
        sprintf_screen("ido");
        break;
    case 0x3F:
        sprintf_screen("takeoff");
        break;
    case 0x40:
        sprintf_screen("glaerf");
        break;
    case 0x41:
        sprintf_screen("last");
        break;
    case 0x42:
        sprintf_screen("shebat");
        break;
    case 0x43:
        sprintf_screen("dungeon");
        break;
    case 0x44:
        sprintf_screen("lastbat");
        break;
    case 0x45:
        sprintf_screen("solaris");
        break;
    case 0xB5:
        sprintf_screen("vomaria");
        break;
    case 0xB6:
        sprintf_screen("melmv");
        break;
    case 0xB7:
        sprintf_screen("yugumv");
        break;
    case 0xB8:
        sprintf_screen("zoharumv");
        break;
    case 0xB9:
        sprintf_screen("vomagic5");
        break;
    case 0xBA:
        sprintf_screen("vomagic4");
        break;
    case 0xBB:
        sprintf_screen("vomagic3");
        break;
    case 0xBC:
        sprintf_screen("voivent3");
        break;
    case 0xBD:
        sprintf_screen("voivent2");
        break;
    case 0xBE:
        sprintf_screen("vobossm");
        break;
    case 0xBF:
        sprintf_screen("vobossl");
        break;
    case 0xC0:
        sprintf_screen("vochu6");
        break;
    case 0xC1:
        sprintf_screen("vomagic2");
        break;
    case 0xC2:
        sprintf_screen("vomagic1");
        break;
    case 0x7:
        sprintf_screen("movie14");
        break;
    case 0xC3:
        sprintf_screen("movie15");
        break;
    case 0xC4:
        sprintf_screen("movie16");
        break;
    case 0xC5:
        sprintf_screen("movie18");
        break;
    case 0xC6:
        sprintf_screen("voivent");
        break;
    case 0xC7:
        sprintf_screen("damage");
        break;
    case 0xC8:
        sprintf_screen("vofei");
        break;
    case 0xC9:
        sprintf_screen("vofei1");
        break;
    case 0xCA:
        sprintf_screen("vofei2");
        break;
    case 0xCB:
        sprintf_screen("vofei3");
        break;
    case 0xCC:
        sprintf_screen("vofei4");
        break;
    case 0xCD:
        sprintf_screen("vofei5");
        break;
    case 0xCE:
        sprintf_screen("vofei6");
        break;
    case 0xCF:
        sprintf_screen("voellie");
        break;
    case 0xD0:
        sprintf_screen("voellie1");
        break;
    case 0xD1:
        sprintf_screen("voellie2");
        break;
    case 0xD2:
        sprintf_screen("voellie3");
        break;
    case 0xD3:
        sprintf_screen("voellie4");
        break;
    case 0xD4:
        sprintf_screen("voellie5");
        break;
    case 0xD5:
        sprintf_screen("voellie6");
        break;
    case 0xD6:
        sprintf_screen("voellie7");
        break;
    case 0xD7:
        sprintf_screen("voellie8");
        break;
    case 0xD8:
        sprintf_screen("voshita");
        break;
    case 0xD9:
        sprintf_screen("voshita1");
        break;
    case 0xDA:
        sprintf_screen("voshita2");
        break;
    case 0xDB:
        sprintf_screen("voshita3");
        break;
    case 0xDC:
        sprintf_screen("voshita4");
        break;
    case 0xDD:
        sprintf_screen("voshita5");
        break;
    case 0xDE:
        sprintf_screen("voshita6");
        break;
    case 0xDF:
        sprintf_screen("vobaluto");
        break;
    case 0xE0:
        sprintf_screen("vobalu1");
        break;
    case 0xE1:
        sprintf_screen("vobalu2");
        break;
    case 0xE2:
        sprintf_screen("vobalu3");
        break;
    case 0xE3:
        sprintf_screen("vobalu4");
        break;
    case 0xE4:
        sprintf_screen("vobalu5");
        break;
    case 0xE5:
        sprintf_screen("vobalu6");
        break;
    case 0xE6:
        sprintf_screen("vobalu7");
        break;
    case 0xE7:
        sprintf_screen("vorico");
        break;
    case 0xE8:
        sprintf_screen("vorico1");
        break;
    case 0xE9:
        sprintf_screen("vorico2");
        break;
    case 0xEA:
        sprintf_screen("vorico3");
        break;
    case 0xEB:
        sprintf_screen("vorico4");
        break;
    case 0xEC:
        sprintf_screen("vorico5");
        break;
    case 0xED:
        sprintf_screen("vobilly");
        break;
    case 0xEE:
        sprintf_screen("vobilly1");
        break;
    case 0xEF:
        sprintf_screen("vobilly2");
        break;
    case 0xF0:
        sprintf_screen("vobilly3");
        break;
    case 0xF1:
        sprintf_screen("vobilly4");
        break;
    case 0xF2:
        sprintf_screen("vobilly5");
        break;
    case 0xF3:
        sprintf_screen("voeme");
        break;
    case 0xF4:
        sprintf_screen("voeme1");
        break;
    case 0xF5:
        sprintf_screen("voeme2");
        break;
    case 0xF6:
        sprintf_screen("voeme3");
        break;
    case 0xF7:
        sprintf_screen("voeme4");
        break;
    case 0xF8:
        sprintf_screen("voeme5");
        break;
    case 0xF9:
        sprintf_screen("vochu");
        break;
    case 0xFA:
        sprintf_screen("vochu1");
        break;
    case 0xFB:
        sprintf_screen("vochu2");
        break;
    case 0xFC:
        sprintf_screen("vochu3");
        break;
    case 0xFD:
        sprintf_screen("vochu4");
        break;
    case 0xFE:
        sprintf_screen("vochu5");
        break;
    }
}
