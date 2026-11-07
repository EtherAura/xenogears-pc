/* Scene expansion: preserve native pixels, reveal clipped geometry, isolate
 * adjacent buffers, retain capture framing, and restore the original view. */
#include "common.h"
#include "gpu/gpu_internal.h"
#include "psyq.h"
#include "system/settings.h"
#include "test/env.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int gpuInit(SDL_Window *);
void gpuShutdown(void);
#include "../../../../src/main/background_render.c"
s32 fieldMapNumber;
static int checks;
#include "test/check.h"
static uint16_t native[512 * 1024], expanded[512 * 1024], picture[2048 * 256], captured[2048 * 256];

static void display(int y, int height) {
    DISPENV d;
    SetDefDispEnv(&d, 0, y, 320, height);
    d.screen.y = 10;
    d.screen.h = 216;
    PutDispEnv(&d);
    SetDispMask(1);
    gpuVBlank();
}
static void scene(int y, int height, int green) {
    DRAWENV d;
    SetDefDrawEnv(&d, 0, y, 320, height);
    PutDrawEnv(&d);
    RECT r = {0, y, 320, height};
    ClearImage(&r, 0, 0, 64);
    POLY_G4 q;
    SetPolyG4(&q);
    q.x0 = q.x2 = 70;
    q.x1 = q.x3 = 240;
    q.y0 = q.y1 = -10;
    q.y2 = q.y3 = 230;
    setRGB0(&q, green ? 0 : 248, green ? 248 : 0, 0);
    setRGB1(&q, green ? 0 : 248, green ? 248 : 0, 0);
    setRGB2(&q, 0, 0, 248);
    setRGB3(&q, 0, 0, 248);
    DrawPrim(&q);
    DrawSync(0);
}
static int backdrop(void) {
    presentSetAspectText("16:9");presentSetExpandedVertical(1);pcWideSetMode(PC_WIDE_BATTLE);
    display(0,216);scene(0,224,0);gpuVBlank();
    RECT grab={0,0,320,224};MoveImage(&grab,512,0);DrawSync(0);
    CHECK(pcWideCapturedScene(512,0,320,224));
    CHECK(!pcWideCapturedScene(512,0,319,224));
    pcWideSetMode(PC_WIDE_FIELD);display(256,216);
    DRAWENV env;SetDefDrawEnv(&env,0,256,320,224);PutDrawEnv(&env);
    RECT clear={0,256,320,224};ClearImage(&clear,0,0,0);
    BackgroundPoly p={0};p.m328_textureWidth=320;p.m32C_textureHeight=224;
    p.m334_vramX=512;p.m338_colorMode=2;
    for(int i=0;i<16;i++){SetPolyFT4(&p.m0_ft4[i]);SetShadeTex(&p.m0_ft4[i],1);}
    OTag ot;TermPrim(&ot);computeBackgroundPoly(&p,0,224,0,&ot,0);DrawOTag(&ot);DrawSync(0);gpuVBlank();
    const WideView* v=wideCurrentView();CHECK(v->wide && v->clean && v->h==240);
    int width=v->w;gvkReadWide(picture,v->x,v->y,width,240);
    /* Captured scene margins and extra rows keep the coloured original
     * scene; wrapping its opposite edge or showing black would fail. */
    CHECK(picture[0]!=0 && picture[width-1]!=0);
    CHECK(picture[239*width]!=0 && picture[240*width-1]!=0);
    gpuReadVram(native);
    /* Original 4:3 draws the very same native packets and pixels. */
    presentSetExpandedVertical(0);presentSetAspectText("4:3tv");display(256,216);
    PutDrawEnv(&env);ClearImage(&clear,0,0,0);TermPrim(&ot);
    computeBackgroundPoly(&p,0,224,0,&ot,0);DrawOTag(&ot);DrawSync(0);gpuReadVram(expanded);
    for(int y=256;y<480;y++)CHECK(!memcmp(native+y*1024,expanded+y*1024,320*2));
    /* Uploading artwork invalidates the captured-scene identity. */
    static u16 art[320*224];LoadImage(&(RECT){512,0,320,224},art);
    presentSetAspectText("16:9");gpuVBlank();CHECK(!pcWideCapturedScene(512,0,320,224));
    return 0;
}
static int narrowZoom(void) {
    wideReset();presentSetAspectText("16:9");presentSetExpandedVertical(1);
    presentSetFill(0);pcWideSetMode(PC_WIDE_FIELD);display(0,216);
    CHECK(wideCurrentView()->wide);
    presentSetFill(1);gpuVBlank();
    CHECK(!wideCurrentView()->wide); /* the VRAM handover still uses Zoom */
    RECT r={704,256,320,224};
    for(int y=0;y<224;y++)for(int x=0;x<320;x++)
        captured[y*320+x]=(uint16_t)(1+x%30) | (uint16_t)(1+y%30)<<5 | 12<<10;
    LoadImage(&r,(u32*)captured);pcWideCaptureVram(&r);gpuReadVram(native);
    presentSetFill(0);pcWideCaptureZoom(704,256);
    CHECK(!pcWideCapturedScene(704,256,320,224));
    presentSetFill(1);pcWideCaptureZoom(704,256);
    CHECK(pcWideCapturedScene(704,256,320,224));
    int margin,top,bottom;
    CHECK(pcWideCaptureReadFrame(&r,picture,sizeof(picture)/sizeof(*picture),&margin,&top,&bottom));
    CHECK(margin==56 && top==10 && bottom==6);
    int width=320+2*margin;
    for(int y=0;y<240;y++)for(int x=0;x<width;x++) {
        /* At rest, the 16:9 display shows the entire movie width and the
         * centred 180 of its 216 lines, exactly the preceding Zoom view. */
        int sx=(int)floor(160+(x-margin+0.5-160)*0.75);
        int sy=(int)floor(108+(y-120+0.5)*0.75);
        CHECK(picture[y*width+x]==(sx>=0 && sx<320 ? captured[sy*320+sx] : 0));
    }
    /* Preparing or preparing again cannot change the native capture. */
    pcWideCaptureZoom(704,256);gpuReadVram(expanded);
    CHECK(!memcmp(native,expanded,sizeof(native)));
    presentSetFill(0);
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "write") == 0) {
        testSetEnv("XG_SETTINGS", argv[2]);
        presentSetExpandedVertical(1);
        CHECK(settingsSave());
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "read") == 0) {
        testSetEnv("XG_SETTINGS", argv[2]);
        CHECK(presentExpandedVertical());
        return 0;
    }
    testSetEnv("XG_SETTINGS", "none");
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    CHECK(gpuInit(NULL));
    CHECK(!presentExpandedVertical());
    static const char *aspects[] = {"4:3tv", "4:3fill", "1:1", "16:9", "16:10", "21:9", "32:9"};
    for (int aspect = 0; aspect < 7; aspect++)
        for (int mode = 0; mode < 4; mode++) {
            CHECK(presentSetAspectText(aspects[aspect]));
            pcWideSetMode(mode == 1 ? PC_WIDE_BATTLE : mode == 2 ? PC_WIDE_MENU : mode == 3 ? PC_WIDE_WORLDMAP : PC_WIDE_FIELD);
            int height = mode == 1 || mode == 3 ? 216 : 224;
            int second = mode == 1 || mode == 3 ? 216 : mode == 2 ? 224 : 256;
            presentSetExpandedVertical(0);
            display(0, height);
            scene(0, height, 0);
            gpuReadVram(native);
            presentSetExpandedVertical(1);
            display(0, height);
            scene(0, height, 0);
            gpuVBlank();
            const WideView *v = wideCurrentView();
            CHECK(v->wide && v->top == 10 && v->bottom == 14 && v->h == 240);
            CHECK(v->y == 2 && v->w == 320 + 2 * v->m);
            int m = v->m, width = v->w;
            gvkReadWide(picture, v->x, v->y, width, 240);
            CHECK((picture[m + 100] & 31) > 20); /* real geometry above native y=0 */
            CHECK((picture[239 * width + m + 100] >> 10 & 31) > 20);
            for (int y = 0; y < 216; y++) {
                if (memcmp(picture + (y + 10) * width + m, native + y * 1024, 320 * 2)) {
                    for (int x = 0; x < 320; x++)
                        if (picture[(y + 10) * width + m + x] != native[y * 1024 + x]) {
                            fprintf(stderr, "aspect %d mode %d y %d x %d native %04x expanded %04x\n", aspect, mode,
                                    y, x, native[y * 1024 + x], picture[(y + 10) * width + m + x]);
                            break;
                        }
                }
                CHECK(memcmp(picture + (y + 10) * width + m, native + y * 1024, 320 * 2) == 0);
            }
            gpuReadVram(expanded);
            CHECK(memcmp(native, expanded, sizeof(native)) == 0);
            int n = gvkHiScale();
            if (n) {
                uint32_t originalPixel, extraPixel;
                CHECK(gvkHiRead(0, &originalPixel, 100 * n + n / 2, 100 * n + n / 2, 1, 1));
                CHECK(gvkHiRead(1, &extraPixel, (v->x + m + 100) * n + n / 2, (v->y + 110) * n + n / 2, 1, 1));
                CHECK(originalPixel == extraPixel);
                CHECK(gvkHiRead(1, &extraPixel, (v->x + m + 100) * n + n / 2, v->y * n + n / 2, 1, 1));
                CHECK((extraPixel & 255) > 200);
            }
            PresentConstants pc;
            uint32_t bits[4096];
            for (int scale = 0; scale < 3; scale++) {
                presentSetScale(scale);
                presentConstants(gpuDisplay(), 960, 720, &pc, bits, 4096);
                CHECK(pc.src == 1 && pc.disp[3] == 240);
                if (aspect == 0 && scale == 0)
                    CHECK(pc.dest[1] == 0 && pc.dest[3] == 720);
            }
            presentSetScale(0);
            /* The retail pause uploads a small label into the shown buffer,
             * then waits across many vblanks without drawing another frame.
             * Keep every surrounding expanded pixel, including both bands. */
            memcpy(captured, picture, (size_t)width * 240 * 2);
            uint16_t label[48 * 12];
            for (int i = 0; i < 48 * 12; i++) label[i] = 0x7FFF;
            RECT pause = {136, 100, 48, 12};
            LoadImage(&pause, (u32*)label);
            DrawSync(0);
            for (int paused = 0; paused < 3; paused++) {
                gpuVBlank();
                v = wideCurrentView();
                CHECK(v->wide && v->top == 10 && v->h == 240);
                gvkReadWide(picture, v->x, v->y, width, 240);
                for (int y = 0; y < 240; y++) {
                    int inLabel = y >= 110 && y < 122;
                    if (inLabel) {
                        CHECK(memcmp(picture + y * width, captured + y * width, (m + 136) * 2) == 0);
                        CHECK(memcmp(picture + y * width + m + 184, captured + y * width + m + 184,
                                     (width - m - 184) * 2) == 0);
                        CHECK(picture[y * width + m + 140] == 0x7FFF);
                    } else CHECK(memcmp(picture + y * width, captured + y * width, width * 2) == 0);
                }
            }
            /* Restore only the patched pixels, like any subsequent CPU edit. */
            for (int y = 0; y < 12; y++) memcpy(label + y * 48, native + (100 + y) * 1024 + 136, 48 * 2);
            LoadImage(&pause, (u32*)label);
            DrawSync(0);
            display(second, height);
            scene(second, height, 1);
            gpuVBlank();
            v = wideCurrentView();
            CHECK(v->y == 258 && v->h == 240);
            display(0, height);
            v = wideCurrentView();
            gvkReadWide(picture, v->x, v->y, v->w, 240);
            CHECK((picture[v->m + 100] & 31) > 20 && (picture[v->m + 100] >> 5 & 31) == 0);
            /* A full screen capture is read at its native coordinates. */
            RECT r = {0, 0, 320, 216};
            MoveImage(&r, 640, 0);
            DrawSync(0);
            static uint16_t capture[320 * 216];
            r.x = 640;
            StoreImage(&r, (u32 *)capture);
            for (int y = 0; y < 216; y++)
                CHECK(memcmp(capture + y * 320, native + y * 1024, 640) == 0);
            /* MoveImage back and CPU mask processing retain all captured lines. */
            int cm, ct, cb;
            CHECK(pcWideCaptureReadFrame(&r, captured, 2048 * 256, &cm, &ct, &cb));
            CHECK(cm == m && ct == 10 && cb == 14);
            MoveImage(&r, 0, second);
            DrawSync(0);
            display(second, height);
            v = wideCurrentView();
            CHECK(v->top == 10 && v->h == 240);
            gvkReadWide(picture, v->x, v->y, v->w, v->h);
            CHECK(memcmp(picture, captured, (size_t)width * 240 * 2) == 0);
            r.x = 640;
            LoadImage(&r, (u32 *)capture);
            DrawSync(0);
            pcWideCaptureLoadFrame(&r, captured, cm, ct, cb, 0);
            DRAWENV d;
            SetDefDrawEnv(&d, 0, 0, 320, height);
            PutDrawEnv(&d);
            RECT clear = {0, 0, 320, height};
            ClearImage(&clear, 0, 0, 0);
            /* Independent pieces sample negative Y without 8-bit UV wrapping. */
            for (int y = -10; y < 230; y += 16)
                for (int x = -m; x < 320 + m; x += 64) {
                    POLY_FT4 q;
                    SetPolyFT4(&q);
                    setRGB0(&q, 128, 128, 128);
                    setXYWH(&q, x, y, 64, 16);
                    setUVWH(&q, 0, 0, 64, 16);
                    q.tpage = GetTPage(2, 0, 640, 0);
                    pcWidePrimTex2D(&q, 0, PC_WIDE_ONLY | PC_WIDE_HANDLED, x, y);
                    DrawPrim(&q);
                }
            gpuVBlank();
            v = wideCurrentView();
            CHECK(v->top == 10 && v->h == 240);
            gvkReadWide(picture, v->x, v->y, v->w, v->h);
            CHECK(memcmp(picture, captured, (size_t)width * 240 * 2) == 0);
            /* Mask bits also apply to the extra lines at horizontal margin zero. */
            r.x = 0;
            pcWideCaptureMask(&r, 640, 0);
            r.x = 640;
            CHECK(pcWideCaptureReadFrame(&r, captured, 2048 * 256, &cm, &ct, &cb));
            for (int i = 0; i < width * 240; i++)
                CHECK(captured[i] & 0x8000);
            /* Palette texels over a capture are no picture of the screen (the
             * field's opening credits draw sprites whose texture page a capture
             * overwrote): the frame stays wide. The capture as a 15-bit picture
             * without a wide version of the effect makes it 4:3. */
            display(0, height);
            for (int depth = 0; depth <= 2; depth++) {
                scene(0, height, 0);
                /* not the pieces' packet above: DrawPrim keeps their tags */
                static POLY_FT4 probe;
                SetPolyFT4(&probe);
                setRGB0(&probe, 128, 128, 128);
                setXYWH(&probe, 40, 40, 32, 32);
                setUVWH(&probe, 0, 0, 32, 32);
                probe.tpage = GetTPage(depth, 0, 640, 0);
                probe.clut = GetClut(0, 500);
                DrawPrim(&probe);
                DrawSync(0);
                gpuVBlank();
                CHECK(wideCurrentView()->clean == (depth != 2));
            }
            presentSetExpandedVertical(0);
            scene(0, height, 0);
            gpuVBlank();
            CHECK(wideCurrentView()->top == 0);
        }
    /* On the field -> map boundary, the new back buffer overlaps only
     * hidden field rows. The still displayed expanded front picture must
     * survive until its replacement is ready. */
    presentSetExpandedVertical(1);
    presentSetAspectText("16:9");
    pcWideSetMode(PC_WIDE_FIELD);
    display(0, 216);
    scene(0, 224, 0);
    gpuVBlank();
    const WideView* front = wideCurrentView();
    int frontWidth = front->w;
    CHECK(front->wide && front->top == 10 && front->bottom == 14);
    gvkReadWide(captured, front->x, front->y, frontWidth, 240);
    gpuReadVram(native);
    pcWideSetMode(PC_WIDE_WORLDMAP);
    scene(216, 216, 1);
    gpuVBlank();
    front = wideCurrentView();
    CHECK(front->wide && front->top == 10 && front->bottom == 14 && front->h == 240);
    gvkReadWide(picture, front->x, front->y, frontWidth, 240);
    CHECK(memcmp(picture, captured, frontWidth * 240 * 2) == 0);
    gpuReadVram(expanded);
    CHECK(memcmp(native, expanded, 216 * 1024 * 2) == 0);
    display(216, 216);
    CHECK(wideCurrentView()->top == 10 && wideCurrentView()->h == 240);
    scene(0, 216, 1);
    display(0, 216);
    CHECK(wideCurrentView()->top == 10 && wideCurrentView()->h == 240);
    /* A 216-row world-map capture becomes the menu's 224-row backdrop.
     * Repeated restores must retain all 240 rows without changing its size. */
    presentSetExpandedVertical(1);
    presentSetAspectText("4:3tv");
    pcWideSetMode(PC_WIDE_WORLDMAP);
    display(0, 216);
    scene(0, 216, 0);
    gpuVBlank();
    RECT worldGrab = {0, 0, 320, 216};
    MoveImage(&worldGrab, 640, 0);
    DrawSync(0);
    worldGrab.x = 640;
    int grabMargin, grabTop, grabBottom;
    CHECK(pcWideCaptureReadFrame(&worldGrab, captured, 2048 * 256, &grabMargin, &grabTop, &grabBottom));
    pcWideSetMode(PC_WIDE_MENU);
    scene(224, 224, 1);
    display(224, 224);
    RECT menuGrab = worldGrab;
    menuGrab.h = 224;
    for (int restore = 0; restore < 2; restore++) {
        MoveImage(&menuGrab, 0, 224);
        DrawSync(0);
        gpuVBlank();
        const WideView *menuView = wideCurrentView();
        CHECK(menuView->wide && menuView->top == 10 && menuView->h == 240);
        gvkReadWide(picture, menuView->x, menuView->y, 320, 240);
        CHECK(memcmp(picture, captured, 320 * 240 * 2) == 0);
    }
    CHECK(pcWideCaptureReadFrame(&worldGrab, captured, 2048 * 256, &grabMargin, &grabTop, &grabBottom));
    CHECK(grabTop == 10 && grabBottom == 14);
    presentSetExpandedVertical(1);
    pcWideSetMode(PC_WIDE_FIELD);
    presentSetAspectText("16:9");
    display(0, 224);
    scene(0, 224, 0);
    gpuVBlank();
    /* Fades reach the corners of a simultaneously wide and tall picture. */
    DRAWENV d;
    SetDefDrawEnv(&d, 0, 0, 320, 224);
    PutDrawEnv(&d);
    POLY_F4 fade;
    SetPolyF4(&fade);
    setRGB0(&fade, 128, 128, 128);
    fade.x0 = fade.x2 = 0;
    fade.x1 = fade.x3 = 320;
    fade.y0 = fade.y1 = 0;
    fade.y2 = fade.y3 = 224;
    DrawPrim(&fade);
    gpuVBlank();
    const WideView *v = wideCurrentView();
    gvkReadWide(picture, v->x, v->y, v->w, v->h);
    CHECK(picture[0] == 0x4210 && picture[v->w - 1] == 0x4210);
    CHECK(picture[239 * v->w] == 0x4210 && picture[240 * v->w - 1] == 0x4210);
    /* Switching aspect while paused must not squeeze old side margins. */
    presentSetAspectText("4:3tv");
    gpuVBlank();
    v = wideCurrentView();
    CHECK(v->wide && v->w == 320 && v->m == 0 && v->x > 0);
    /* A native packet with a separate display replacement must neither
     * leak its old picture into the copy nor invalidate the extra rows. */
    pcWideSetMode(PC_WIDE_FIELD);
    display(0, 224);
    scene(0, 224, 0);
    POLY_F4 source;
    SetPolyF4(&source);
    setXYWH(&source, 100, 100, 20, 20);
    setRGB0(&source, 248, 0, 0);
    pcWidePrim(&source, 0, PC_WIDE_NATIVE_ONLY);
    DrawPrim(&source);
    POLY_F4 replacement = source;
    setRGB0(&replacement, 0, 248, 0);
    pcWideOnly(&replacement);
    DrawPrim(&replacement);
    gpuVBlank();
    v = wideCurrentView();
    CHECK(v->wide && v->top == 10 && v->h == 240);
    gpuReadVram(native);
    CHECK(native[110 * 1024 + 110] == 31);
    gvkReadWide(picture, v->x, v->y, v->w, v->h);
    CHECK(picture[120 * v->w + v->m + 110] == (31 << 5));
    for (int mode = 0; mode < PC_WIDE_KINDS; mode++) {
        if (mode == PC_WIDE_FIELD || mode == PC_WIDE_BATTLE || mode == PC_WIDE_MENU || mode == PC_WIDE_WORLDMAP)
            continue;
        pcWideSetMode(mode);
        display(0, 224);
        scene(0, 224, 0);
        gpuVBlank();
        CHECK(wideCurrentView()->top == 0);
    }
    CHECK(!backdrop());
    CHECK(!narrowZoom());
    gpuShutdown();
    SDL_Quit();
    printf("Vertical expansion: %d checks passed\n", checks);
    return 0;
}
