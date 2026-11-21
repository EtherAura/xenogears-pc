#ifndef PC_CAMERA_H
#define PC_CAMERA_H
/* PC-only free field camera (port/src/system/freecam.c): the Enhanced engine's
 * updateAllEntities hands it the eye and target the game chose for this frame, after
 * its interpolation and shake, just before the camera matrix is built. */
#include "common.h"
#include "psyq.h"
void pcFieldCamera(VECTOR* eye, VECTOR* at);
/* Enhanced first-person view. Only navigation changes the heading used for movement. */
int pcFirstPersonEnabled(void);
void pcFirstPersonSet(int on);
int pcFirstPersonNavigation(void);
void pcFirstPersonNavigationSet(int on);
s16 pcFirstPersonMovementHeading(s16 game);
void pcFirstPersonWorldFrame(int leader, s16 facing, int moving, int followBearing);
void pcFirstPersonWorldAngles(SVECTOR* view);
int pcFirstPersonToggle(void); /* returns whether the shortcut was handled */
void pcFirstPersonEnd(void);
int pcFirstPersonWanted(void);
int pcFirstPersonActive(void);
void pcFirstPersonMouse(float dx, float dy);
int pcFirstPersonView(VECTOR* eye, VECTOR* at, s16* spriteHeading, int playerControl);
int pcFreeCameraEnabled(void);  /* settings.ini free_camera (or XG_FREECAM) */
void pcFreeCameraSet(int on);
/* Whether the free camera shows this frame (its blend with the game's camera above zero). */
int pcFreeCameraActive(void);
/* The world map's free camera (port/src/system/freecam.c): input once a frame, the turn to add to
 * the world camera's target heading, and the drawn pitch and distance from the game's. */
void pcWorldCameraFrame(void);
int pcWorldCameraTurn(void);
int pcWorldCameraActive(void);
s16 pcWorldCameraPitch(s16 game);
s32 pcWorldCameraDistance(s32 game);
/* Phase 2b, the scenery between the free camera and the party, drawn half transparent:
 * renderObjects sets pcSceneryScope while it submits the room's meshes (the main ordering
 * table, not the ground's) so port/src/game/main/render.c records their primitives; then
 * pcSceneryFade fades those overlapping the tracked actor's screen box (feet and head as
 * projected, the feet's OT index) and nearer the camera than it. */
extern int pcSceneryScope;
void pcSceneryReset(void);
void pcSceneryFade(int feetX, int feetY, int headX, int headY, int ot);
#endif
