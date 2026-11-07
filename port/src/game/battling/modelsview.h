#ifndef PORT_GAME_BATTLING_MODELSVIEW_H
#define PORT_GAME_BATTLING_MODELSVIEW_H

/* PC-only entry points, used only inside the model viewer's sandbox. */
#include "common.h"
#include "psyq.h"
#include "../../extras/models_types.h"

void* modelsBattlingLoad(void* data, s32 size);
s32 modelsBattlingBoneCount(void* root);
s32 modelsBattlingAnimationCount(void* root);
void modelsBattlingPlay(void* root, s32 animation);
void modelsBattlingRepeat(void* root, s32 animation);
s32 modelsBattlingStep(void* root, s32 animation);
void modelsBattlingState(void* root, s32 animation, XgVmState* state);
void modelsBattlingTiming(void* root, s32 animation, XgBattlingTiming* timing);
s32 modelsBattlingParts(void* root, XgScenePart* out, s32 max);
s32 modelsBattlingBones(void* root, XgSceneBone* out, s32 max);
void modelsBattlingRender(void* root, MATRIX* view, MATRIX* light, OTag* ot, s32 odd);

#endif
