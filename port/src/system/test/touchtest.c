/* The touch pad (touch.c) on a window of SDL's dummy video driver: both
 * layouts on screens of several shapes and at several sizes (every control on
 * the screen, none over another), then fingers: the left stick's directions,
 * the right stick and the camera's buttons, and what shows with which of the
 * camera's features; last, display cutouts (XG_TOUCH_CUTOUT): no control on
 * one, the layout's rules kept, nothing moved by one that covers nothing.
 * Settings stay in memory (XG_SETTINGS=none). */
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "system/settings.h"
#include "system/touch.h"
#include "test/env.h"

static int features, toggles;
int padCameraFeatures(void) { return features; }
void padFirstPerson(void) { toggles++; }

static int checks, failures;
static char context[160];
#define CHECK_CONTEXT ((void)(context[0] != '\0' && fprintf(stderr, "%s: ", context)))
#include "test/check.h"

#define B_UP 0x1000
#define B_RIGHT 0x2000
#define B_DOWN 0x4000
#define B_LEFT 0x8000
#define B_CROSS 0x0040

static SDL_Window* window;
static const float W = 2400, H = 1080;

static void finger(Uint32 type, int id, float x, float y) {
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    e.tfinger.windowID = SDL_GetWindowID(window);
    e.tfinger.fingerID = (SDL_FingerID)(id + 1);
    e.tfinger.x = x / W;
    e.tfinger.y = y / H;
    touchEvent(&e);
}

static int isRound(int kind) {
    return (kind >= TOUCH_CROSS && kind <= TOUCH_TRIANGLE) || kind == TOUCH_MENU || kind == TOUCH_HOTKEYS ||
           kind == TOUCH_STICK_LEFT || kind == TOUCH_STICK_RIGHT || kind == TOUCH_VIEW || kind == TOUCH_RECENTRE;
}

/* Whether two controls' figures share a point. */
static int overlap(const TouchControl* a, const TouchControl* b) {
    if (isRound(a->kind) && isRound(b->kind)) {
        float dx = a->x - b->x, dy = a->y - b->y, r = a->w / 2 + b->w / 2;
        return dx * dx + dy * dy < r * r;
    }
    if (isRound(b->kind)) {
        const TouchControl* t = a;
        a = b;
        b = t;
    }
    if (isRound(a->kind)) { /* a circle and a box: the box's nearest point */
        float nx = fmaxf(b->x - b->w / 2, fminf(a->x, b->x + b->w / 2));
        float ny = fmaxf(b->y - b->h / 2, fminf(a->y, b->y + b->h / 2));
        float dx = a->x - nx, dy = a->y - ny;
        return dx * dx + dy * dy < a->w / 2 * a->w / 2;
    }
    return fabsf(a->x - b->x) < (a->w + b->w) / 2 && fabsf(a->y - b->y) < (a->h + b->h) / 2;
}

/* Whether a control's figure (a round one's circle, the D-pad's plus, a box) meets a rectangle. */
static int onRect(const TouchControl* c, float x0, float y0, float x1, float y1) {
    if (isRound(c->kind)) {
        float nx = fmaxf(x0, fminf(c->x, x1)), ny = fmaxf(y0, fminf(c->y, y1));
        return (c->x - nx) * (c->x - nx) + (c->y - ny) * (c->y - ny) < c->w / 2 * c->w / 2;
    }
    if (c->kind == TOUCH_DPAD) {
        float arm = c->w * 0.175f, half = c->w / 2;
        return (c->x + arm > x0 && c->x - arm < x1 && c->y + half > y0 && c->y - half < y1) ||
               (c->x + half > x0 && c->x - half < x1 && c->y + arm > y0 && c->y - arm < y1);
    }
    return c->x + c->w / 2 > x0 && c->x - c->w / 2 < x1 && c->y + c->h / 2 > y0 && c->y - c->h / 2 < y1;
}

static const TouchControl* find(const TouchControl* cs, int n, int kind) {
    for (int i = 0; i < n; i++)
        if (cs[i].kind == kind) return &cs[i];
    return NULL;
}

/* Every control of this layout on a w x h screen: on it, and clear of the others. */
static void layoutChecks(float w, float h) {
    TouchControl cs[32];
    int n = touchControls(cs, 32, w, h);
    EXPECT(n >= 13 && n <= 32);
    for (int i = 0; i < n; i++) {
        const TouchControl* c = &cs[i];
        EXPECTF(c->x - c->w / 2 >= 0 && c->x + c->w / 2 <= w && c->y - c->h / 2 >= 0 && c->y + c->h / 2 <= h,
                "control %d (kind %d) at %.0f,%.0f size %.0f x %.0f", i, c->kind, c->x, c->y, c->w, c->h);
        for (int k = i + 1; k < n; k++)
            EXPECTF(!overlap(c, &cs[k]), "kinds %d and %d", c->kind, cs[k].kind);
    }
}

int main(void) {
    testSetEnv("XG_SETTINGS", "none");
    testSetEnv("SDL_VIDEO_DRIVER", "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("touchtest", (int)W, (int)H, SDL_WINDOW_HIDDEN);
    if (window == NULL) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    touchSetMode(1);
    EXPECT(touchMode() == 1 && touchShown());
    EXPECT(touchLayout() == 0);

    /* ---- the layouts, on phones, tablets and a square ---- */
    static const float screens[][2] = {{2400, 1080}, {1920, 1080}, {1280, 800}, {1024, 768},
                                       {2208, 1840}, {1080, 1080}, {3120, 1440}, {800, 480}};
    static const int sizes[] = {60, 100, 140};
    for (int layout = 0; layout < 2; layout++) {
        touchSetLayout(layout);
        EXPECT(touchLayout() == layout);
        for (features = 0; features < 4; features++) {
            for (unsigned z = 0; z < sizeof sizes / sizeof sizes[0]; z++) {
                touchSetSizePercent(sizes[z]);
                for (unsigned s = 0; s < sizeof screens / sizeof screens[0]; s++) {
                    snprintf(context, sizeof context, "layout %d features %d size %d on %.0f x %.0f", layout, features,
                             sizes[z], screens[s][0], screens[s][1]);
                    layoutChecks(screens[s][0], screens[s][1]);
                    int fitted = touchFittedSizePercent(screens[s][0], screens[s][1]);
                    EXPECT(fitted > 0 && fitted <= sizes[z]);
                }
            }
        }
    }
    context[0] = '\0';
    /* a size that fits is the size drawn; one that does not is brought down */
    touchSetLayout(1);
    touchSetSizePercent(100);
    EXPECT(touchFittedSizePercent(2400, 1080) == 100);
    touchSetSizePercent(140);
    EXPECT(touchFittedSizePercent(1024, 768) < 140);
    touchSetSizePercent(100);

    /* ---- what the sticks' layout shows ---- */
    TouchControl cs[32];
    int n;
    touchSetLayout(0);
    features = 3;
    n = touchControls(cs, 32, W, H);
    EXPECT(find(cs, n, TOUCH_STICK_LEFT) == NULL && find(cs, n, TOUCH_STICK_RIGHT) == NULL);
    EXPECT(find(cs, n, TOUCH_VIEW) == NULL && find(cs, n, TOUCH_RECENTRE) == NULL);
    touchSetLayout(1);
    features = 0; /* nothing for a right stick to turn: the left one alone */
    n = touchControls(cs, 32, W, H);
    EXPECT(find(cs, n, TOUCH_STICK_LEFT) != NULL && find(cs, n, TOUCH_DPAD) != NULL);
    EXPECT(find(cs, n, TOUCH_STICK_RIGHT) == NULL && find(cs, n, TOUCH_VIEW) == NULL && find(cs, n, TOUCH_RECENTRE) == NULL);
    features = 1; /* the free camera: the stick and the way back */
    n = touchControls(cs, 32, W, H);
    EXPECT(find(cs, n, TOUCH_STICK_RIGHT) != NULL && find(cs, n, TOUCH_RECENTRE) != NULL && find(cs, n, TOUCH_VIEW) == NULL);
    features = 2; /* the first-person view allowed: the stick and the eye */
    n = touchControls(cs, 32, W, H);
    EXPECT(find(cs, n, TOUCH_STICK_RIGHT) != NULL && find(cs, n, TOUCH_RECENTRE) == NULL && find(cs, n, TOUCH_VIEW) != NULL);
    EXPECT(find(cs, n, TOUCH_VIEW) != NULL && !find(cs, n, TOUCH_VIEW)->on);
    features = 2 | 4;
    n = touchControls(cs, 32, W, H);
    EXPECT(find(cs, n, TOUCH_VIEW) != NULL && find(cs, n, TOUCH_VIEW)->on);

    /* ---- the left stick: the directions, eight ways, none near its centre ---- */
    features = 3;
    n = touchControls(cs, 32, W, H);
    const TouchControl* left = find(cs, n, TOUCH_STICK_LEFT);
    const TouchControl* right = find(cs, n, TOUCH_STICK_RIGHT);
    const TouchControl* dpad = find(cs, n, TOUCH_DPAD);
    CHECK(left != NULL && right != NULL && dpad != NULL);
    float lx = left->x, ly = left->y, reach = touchStickTravel(left);
    float rx = right->x, ry = right->y;
    CHECK(reach > 0 && reach < left->w / 2);
    EXPECT(touchVBlank() == 0);
    finger(SDL_EVENT_FINGER_DOWN, 0, lx + reach, ly);
    EXPECT(touchVBlank() == B_RIGHT);
    finger(SDL_EVENT_FINGER_MOTION, 0, lx + reach * 0.7f, ly - reach * 0.7f);
    EXPECT(touchVBlank() == (B_RIGHT | B_UP));
    finger(SDL_EVENT_FINGER_MOTION, 0, lx, ly - reach);
    EXPECT(touchVBlank() == B_UP);
    finger(SDL_EVENT_FINGER_MOTION, 0, lx - reach * 0.7f, ly + reach * 0.7f);
    EXPECT(touchVBlank() == (B_LEFT | B_DOWN));
    finger(SDL_EVENT_FINGER_MOTION, 0, lx + reach * 0.2f, ly + reach * 0.1f);
    EXPECT(touchVBlank() == 0);
    /* past its reach the finger keeps the stick, and the cap stops at the reach */
    finger(SDL_EVENT_FINGER_MOTION, 0, lx - reach * 5, ly);
    EXPECT(touchVBlank() == B_LEFT);
    n = touchControls(cs, 32, W, H);
    left = find(cs, n, TOUCH_STICK_LEFT);
    CHECK(left != NULL);
    EXPECT(left->held && fabsf(left->sx + 1) < 0.001f && fabsf(left->sy) < 0.001f);
    float cx, cy;
    int back;
    EXPECT(!touchCamera(&cx, &cy, &back) && cx == 0 && cy == 0 && !back); /* not the camera's */
    finger(SDL_EVENT_FINGER_UP, 0, lx - reach * 5, ly);
    EXPECT(touchVBlank() == 0);

    /* the D-pad is still a D-pad, with Cross under a second finger; where its margin
     * reaches under the stick, the stick has the touch */
    n = touchControls(cs, 32, W, H);
    const TouchControl* cross = find(cs, n, TOUCH_CROSS);
    CHECK(cross != NULL);
    finger(SDL_EVENT_FINGER_DOWN, 0, dpad->x, dpad->y + dpad->h * 0.4f);
    finger(SDL_EVENT_FINGER_DOWN, 1, cross->x, cross->y);
    EXPECT(touchVBlank() == (B_DOWN | B_CROSS));
    finger(SDL_EVENT_FINGER_UP, 0, dpad->x, dpad->y + dpad->h * 0.4f);
    finger(SDL_EVENT_FINGER_UP, 1, cross->x, cross->y);
    EXPECT(touchVBlank() == 0);
    finger(SDL_EVENT_FINGER_DOWN, 0, lx - left->w * 0.5f, ly - left->w * 0.2f);
    n = touchControls(cs, 32, W, H);
    EXPECT(find(cs, n, TOUCH_STICK_LEFT)->held && find(cs, n, TOUCH_DPAD)->pressed == 0);
    finger(SDL_EVENT_FINGER_UP, 0, lx - left->w * 0.5f, ly - left->w * 0.2f);

    /* ---- the right stick: the camera's, no button of the game's ---- */
    finger(SDL_EVENT_FINGER_DOWN, 0, rx + reach * 0.5f, ry);
    EXPECT(touchVBlank() == 0);
    EXPECT(touchCamera(&cx, &cy, &back) && fabsf(cx - 0.5f) < 0.001f && fabsf(cy) < 0.001f && !back);
    finger(SDL_EVENT_FINGER_MOTION, 0, rx, ry - reach * 3);
    EXPECT(touchCamera(&cx, &cy, &back) && fabsf(cx) < 0.001f && fabsf(cy + 1) < 0.001f && !back);
    /* both sticks at once */
    finger(SDL_EVENT_FINGER_DOWN, 1, lx, ly + reach);
    EXPECT(touchVBlank() == B_DOWN);
    EXPECT(touchCamera(&cx, &cy, &back) && fabsf(cy + 1) < 0.001f);
    finger(SDL_EVENT_FINGER_UP, 0, rx, ry - reach * 3);
    finger(SDL_EVENT_FINGER_UP, 1, lx, ly + reach);
    EXPECT(!touchCamera(&cx, &cy, &back) && cx == 0 && cy == 0);

    /* ---- the camera's buttons ---- */
    n = touchControls(cs, 32, W, H);
    const TouchControl* recentre = find(cs, n, TOUCH_RECENTRE);
    const TouchControl* view = find(cs, n, TOUCH_VIEW);
    CHECK(recentre != NULL && view != NULL);
    float bx = recentre->x, by = recentre->y, vx = view->x, vy = view->y;
    finger(SDL_EVENT_FINGER_DOWN, 0, bx, by);
    EXPECT(touchCamera(&cx, &cy, &back) && back && cx == 0 && cy == 0);
    EXPECT(touchCamera(&cx, &cy, &back) && back); /* while held */
    EXPECT(touchVBlank() == 0);
    finger(SDL_EVENT_FINGER_UP, 0, bx, by);
    EXPECT(!touchCamera(&cx, &cy, &back) && !back);
    EXPECT(toggles == 0);
    finger(SDL_EVENT_FINGER_DOWN, 0, vx, vy);
    EXPECT(toggles == 1); /* at the touch */
    finger(SDL_EVENT_FINGER_MOTION, 0, vx + 2, vy);
    finger(SDL_EVENT_FINGER_UP, 0, vx + 2, vy);
    EXPECT(toggles == 1 && touchVBlank() == 0);

    /* with nothing for them to drive they are not there to touch */
    features = 0;
    finger(SDL_EVENT_FINGER_DOWN, 0, rx, ry - reach);
    finger(SDL_EVENT_FINGER_DOWN, 1, vx, vy);
    EXPECT(!touchCamera(&cx, &cy, &back) && toggles == 1 && touchVBlank() == 0);
    finger(SDL_EVENT_FINGER_UP, 0, rx, ry - reach);
    finger(SDL_EVENT_FINGER_UP, 1, vx, vy);

    /* ---- the hotkeys' row fits between the shoulders on a narrow screen ---- */
    features = 3;
    n = touchControls(cs, 32, W, H);
    const TouchControl* row = find(cs, n, TOUCH_HOTKEYS);
    CHECK(row != NULL);
    finger(SDL_EVENT_FINGER_DOWN, 0, row->x, row->y);
    finger(SDL_EVENT_FINGER_UP, 0, row->x, row->y);
    n = touchControls(cs, 32, W, H);
    EXPECT(find(cs, n, TOUCH_HOTKEY) != NULL);
    for (int layout = 0; layout < 2; layout++) {
        touchSetLayout(layout);
        for (unsigned s = 0; s < sizeof screens / sizeof screens[0]; s++) {
            snprintf(context, sizeof context, "row open, layout %d on %.0f x %.0f", layout, screens[s][0], screens[s][1]);
            layoutChecks(screens[s][0], screens[s][1]);
        }
    }
    context[0] = '\0';

    /* ---- display cutouts: a camera hole at the middle of a short edge (as the Pixel 10 Pro XL's
     * in landscape, either way round), a notch at the top middle ---- */
    finger(SDL_EVENT_FINGER_DOWN, 0, row->x, row->y); /* the hotkeys' row closed again */
    finger(SDL_EVENT_FINGER_UP, 0, row->x, row->y);
    static const char* const holes[] = {"0.010,0.46,0.034,0.08", "0.956,0.46,0.034,0.08", "0.44,0,0.12,0.06",
                                        "0.010,0.46,0.034,0.08;0.956,0.46,0.034,0.08"};
    static const float boxes[][2][4] = {{{0.010f, 0.46f, 0.044f, 0.54f}}, {{0.956f, 0.46f, 0.990f, 0.54f}},
                                        {{0.44f, 0.0f, 0.56f, 0.06f}},
                                        {{0.010f, 0.46f, 0.044f, 0.54f}, {0.956f, 0.46f, 0.990f, 0.54f}}};
    TouchControl plain[32];
    for (unsigned k = 0; k < sizeof holes / sizeof holes[0]; k++) {
        testSetEnv("XG_TOUCH_CUTOUT", holes[k]);
        SDL_Event changed;
        SDL_zero(changed);
        changed.type = SDL_EVENT_WINDOW_SAFE_AREA_CHANGED;
        touchInputSeen(&changed);
        touchVBlank(); /* reads them */
        for (int layout = 0; layout < 2; layout++) {
            touchSetLayout(layout);
            for (unsigned z = 0; z < sizeof sizes / sizeof sizes[0]; z++) {
                touchSetSizePercent(sizes[z]);
                for (unsigned sc = 0; sc < sizeof screens / sizeof screens[0]; sc++) {
                    float sw = screens[sc][0], sh = screens[sc][1];
                    snprintf(context, sizeof context, "cutout %s, layout %d size %d on %.0f x %.0f", holes[k], layout,
                             sizes[z], sw, sh);
                    layoutChecks(sw, sh);
                    n = touchControls(cs, 32, sw, sh);
                    for (int i = 0; i < n; i++)
                        for (int b = 0; b < 2 && boxes[k][b][2] > 0; b++) {
                            const float* c = boxes[k][b];
                            EXPECTF(!onRect(&cs[i], c[0] * sw, c[1] * sh, c[2] * sw, c[3] * sh),
                                    "kind %d at %.0f,%.0f on the cutout", cs[i].kind, cs[i].x, cs[i].y);
                        }
                }
            }
        }
    }
    context[0] = '\0';
    /* the Pixel's hole, a little lower, on the stick layout's D-pad arm: it moves (so the checks above
     * are not met by chance) and clears it */
    {
        testSetEnv("XG_TOUCH_CUTOUT", "0.010,0.50,0.034,0.08");
        SDL_Event changed;
        SDL_zero(changed);
        changed.type = SDL_EVENT_WINDOW_SAFE_AREA_CHANGED;
        touchInputSeen(&changed);
        touchVBlank();
        touchSetLayout(1);
        touchSetSizePercent(100);
        n = touchControls(cs, 32, 2992, 1344);
        const TouchControl* d = find(cs, n, TOUCH_DPAD);
        testSetEnv("XG_TOUCH_CUTOUT", "");
        touchInputSeen(&changed);
        touchVBlank();
        int m = touchControls(plain, 32, 2992, 1344);
        const TouchControl* before = find(plain, m, TOUCH_DPAD);
        CHECK(d != NULL && before != NULL);
        EXPECT(onRect(before, 0.010f * 2992, 0.50f * 1344, 0.044f * 2992, 0.58f * 1344));
        EXPECT(d->x > before->x && !onRect(d, 0.010f * 2992, 0.50f * 1344, 0.044f * 2992, 0.58f * 1344));
        EXPECT(d->x - before->x < 0.06f * 2992); /* clear of it, not across the screen */
    }
    /* a cutout that covers no control moves none */
    touchSetSizePercent(100);
    touchSetLayout(0);
    testSetEnv("XG_TOUCH_CUTOUT", "0.01,0.01,0.001,0.001");
    {
        SDL_Event changed;
        SDL_zero(changed);
        changed.type = SDL_EVENT_WINDOW_SAFE_AREA_CHANGED;
        touchInputSeen(&changed);
        touchVBlank();
        n = touchControls(cs, 32, W, H);
        testSetEnv("XG_TOUCH_CUTOUT", "");
        touchInputSeen(&changed);
        touchVBlank();
        int m = touchControls(plain, 32, W, H);
        EXPECT(n == m);
        for (int i = 0; i < n && i < m; i++) EXPECT(cs[i].x == plain[i].x && cs[i].y == plain[i].y);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    if (failures) {
        fprintf(stderr, "touchtest: %d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("touchtest: %d checks passed\n", checks);
    return 0;
}
