#ifndef PORT_MOUSE_H
#define PORT_MOUSE_H

/* The mouse as the game's pad (mouse.c): buttons and wheel as short presses
 * of the PS1 buttons on port 1, through pad.c; pointing in the game's menus
 * (mouse_menu.c) and at battle targets (mouse_battle.c); camera drags. */

#ifdef __cplusplus
extern "C" {
#endif

union SDL_Event;

int mouseGameEnabled(void);              /* settings.ini mouse_game (default 1) */
int mousePointEnabled(void);             /* mouse_point (default 1) */
int mouseTargetEnabled(void);            /* mouse_target (default 0) */
int mouseCameraEnabled(void);            /* mouse_camera (default 0) */
void mouseEvent(const union SDL_Event* e); /* pad.c's pump: what the settings menu did not take */
unsigned short mouseVBlank(void);        /* pad.c, each vblank of live input: the buttons held now */
void mouseReset(void);                   /* forget what waits */
void mouseLog(const char* fmt, ...);     /* with XG_MOUSE_LOG=1 */

/* A click's plan: D-pad presses, then (confirm) the confirm button, which
 * goes only if the game's cursor is then at checkArg (check: what kind of
 * cursor). */
#define MOUSE_PLAN_MAX 12
enum { MOUSE_CHECK_NONE, MOUSE_CHECK_MENU_ENTRY, MOUSE_CHECK_ITEM, MOUSE_CHECK_TARGET, MOUSE_CHECK_FIELD_TURN };
typedef struct {
    unsigned short bits[MOUSE_PLAN_MAX];
    int count;
    int confirm;
    int check, checkArg;
} MousePlan;

/* x, y: the click in the game's display area (gpu.h presentWindowToDisplay).
 * 1 and a plan when the click is on something the game is asking for; -1
 * when the game asks there but the click is beside it (it does nothing); 0
 * elsewhere (the click is the confirm button). */
int mouseMenuPoint(float x, float y, MousePlan* plan);
int mouseMenuCheck(int kind, int arg);
int mouseBattlePoint(float x, float y, MousePlan* plan);
int mouseBattleCheck(int target);
int mouseFieldTurnReady(void); /* mouse_camera.c: no field camera turn running */
/* (and pcMouseBattleSprite, mouse_battle.c: the battle's sprite renderer
 * hands over each actor it draws, src/battle/b_800B7C28.c) */

#ifdef __cplusplus
}
#endif

#endif
