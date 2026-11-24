#ifndef PC_SCENE_H
#define PC_SCENE_H

/* PC port: a drawn scene's depth and view, for display effects in the hidden
 * display copy (port/src/gpu/scene.c; depth of field and scene lighting in
 * fields, Enhanced engine). Nothing here changes the PS1 picture or anything
 * the game can read. */

#ifdef PLATFORM_PC

/* The scene the next DrawOTag draws: its ordering tables (mainOt of
 * mainCount entries, groundOt of groundCount or NULL), each slot zPerSlot
 * view depth units apart; uiTag, the first tag the walk reaches after the
 * scene (the effect runs there, before anything linked from it is drawn);
 * focusZ, the view depth to keep sharp (PC_SCENE_FOCUS_CENTRE: wherever the
 * picture's centre is, measured by the port; 0 or less otherwise: none,
 * nothing is blurred); pictureSlot, a slot of groundOt whose packets are a flat
 * picture kept sharp, or -1. Call it right before that DrawOTag, each frame
 * the scene is drawn. */
#define PC_SCENE_FOCUS_CENTRE (-1)
/* Where the next scene is (pcScenePlace, before pcSceneDepth; a scene
 * registered without it is a field's): the effects are tuned per place. */
#define PC_SCENE_FIELD 0
#define PC_SCENE_BATTLE 1
#define PC_SCENE_WORLDMAP 2
#define PC_SCENE_BATTLING 3 /* Battling's arena: a battle's, without the ambient particles */
void pcScenePlace(int place);
void pcSceneDepth(const void* mainOt, int mainCount, const void* groundOt, int groundCount, int zPerSlot,
                  const void* uiTag, int focusZ, int pictureSlot);

/* The view that scene is drawn with, for effects that need a pixel's place in
 * the room (scene lighting and shadows): rotation and translation, the
 * room-to-view matrix the scenery is projected with, as the GTE takes it
 * (view = rotation * room / 4096 + translation; rows x, y, z of view space;
 * the field's rotation is a rotation times 3, which the port takes out);
 * screenZ, the projection distance (the GTE's H); centerX and centerY, the
 * projection centre in the frame (the GTE's OFX and OFY); room, the field's
 * number (the shadows' sun is set from the view when it changes). Call it
 * before pcSceneDepth, each frame the scene is drawn; without it the effects
 * that need it are not drawn. */
void pcSceneView(const short rotation[3][3], const int translation[3], int screenZ, int centerX, int centerY, int room);

/* An actor's ground point in room coordinates, where the game lays its
 * shadow under it: the shadows lay the actor's sprite on the ground from
 * there. Call it for each such actor in the frame, before pcSceneDepth. */
void pcSceneFigure(int x, int y, int z);

/* While a dialogue is open, the view depth of the actor it belongs to (the
 * speaker): the depth of field moves its focus there and narrows its sharp
 * range, easing in and out (xg-rgcf.5.18). 0 or less: no speaker. Call it
 * before pcSceneDepth, each frame the scene is drawn; a scene registered
 * without it has none. */
void pcSceneSpeaker(int focusZ);

#endif /* PLATFORM_PC */

#endif /* PC_SCENE_H */
