/*
 * The mouse's camera drags (mouse.c, settings.ini mouse_camera): when a
 * field's camera may take the next 45-degree turn. The field turns its
 * camera on L1 or R1 held while no turn runs (updateCameraDolly: a turn takes
 * 8 of its frames, fieldSceneState.m176_cameraDollyVar0 counting them down),
 * so each turn a drag asks for waits for the one before it.
 */

#include "common.h"
#include "field/field.h"
#include "mouse.h"

extern FieldBss_800AF880 fieldSceneState;

int mouseFieldTurnReady(void) {
    return fieldSceneState.m176_cameraDollyVar0 == 0;
}
