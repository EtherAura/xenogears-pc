#ifndef PORT_GPU_H
#define PORT_GPU_H

/*
 * The GPU layer's entry points for the rest of the platform (port/src/gpu).
 *
 * The game draws through LIBGPU (libgpu.c, prim.c): VRAM is a 1024x512
 * 16-bit image on the Vulkan device, with an exact CPU copy for StoreImage.
 * Ordering tables are walked when the game hands them over and their GP0
 * packets become draws (gp0.c): triangles become rows of pixels, lines
 * become pixels, and the fragment shader does what the PS1 GPU does per pixel
 * (texture pages and CLUTs, modulation, dithering, semi-transparency, mask
 * bits, draw area). test/gpureplay.c compares the result with a recorded
 * trace.
 *
 * Presentation happens at vertical blank: the display area the game last set
 * with PutDispEnv is shown in the window as it is in VRAM at that moment, like
 * the TV picture. The vblank clock belongs to port/src/system: its vblank
 * interrupt calls gpuVBlank(). Until something does, the GPU layer keeps its
 * own 59.826 Hz vblank events through hwScheduleAt (hw.h); the first call to
 * gpuVBlank() turns those off.
 *
 * Environment:
 *   XG_HEADLESS=1          no swapchain (render offscreen); also chosen when
 *                          the window has no Vulkan surface
 *   XG_DUMP_FRAMES=<dir>   write each displayed frame that differs from the
 *                          previous one as <dir>/frame_<vblank>.png (the
 *                          4:3 picture in VRAM); with a wide aspect also the
 *                          wide picture as wide_<vblank>.png and a line in
 *                          <dir>/wide.log (wide.c)
 *   XG_DUMP_RANGE=<list>   only inside these vblank ranges ("lo-hi", separated
 *                          by commas; the first frame of each is always
 *                          written), so long validation runs stay small;
 *                          "lo-hi/n": every n-th vblank from lo, each written
 *   XG_DUMP_VRAM=<list>    also write VRAM (vram_<vblank>.png and .bin) at
 *                          these vblanks ("all", or numbers separated by
 *                          commas); SIGUSR1 asks for one at the next vblank
 *                          (Windows: Ctrl+Break, SIGBREAK), into the
 *                          XG_DUMP_FRAMES directory or the working directory.
 *                          While the game thread is stuck (no vblank comes),
 *                          port/src/system/stall.c serves it from its own
 *                          thread: vram_<vblank>_stuck.png/.bin read through
 *                          a second device queue, wide_<vblank>_stuck.png
 *   XG_VK_VALIDATION=1     enable the Khronos validation layer. Its
 *                          synchronization checks see the draws' VRAM
 *                          accesses (storage images) only with
 *                          VK_KHRONOS_VALIDATION_VALIDATE_SYNC=1 and
 *                          VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC=1
 *   XG_VK_DEVICE=<n|name>  use the n-th Vulkan device, or the first whose
 *                          name has this in it (any case: intel, nvidia)
 *   XG_TEST_INTERPOLATION_ALLOC_FAIL=1/2/3  fail after partial capture allocation
 *                          groups; tests cleanup, retry delay and recovery.
 *   XG_TEST_HI_WIDE_GROW_FAIL=1  force the overlap allocation to fail, testing
 *                          host-staged growth without changing the scale.
 *   XG_VK_NO_INTERLOCK=1   draw without fragment shader interlock (the
 *                          fallback that splits batches on overlap)
 *   XG_VK_NO_MEMORY_BUDGET=1 ignore VK_EXT_memory_budget: the hi-res wide
 *                          canvas's unused rows are trimmed to system memory
 *                          whatever the device heap's fill (else only when it
 *                          is 70% full; XG_TEST_HI_HEAP_PERCENT=n fakes the fill)
 *   XG_VK_FIFO=1           present with FIFO (default: mailbox, else
 *                          immediate, so presenting never holds up the game)
 *   XG_VK_ACQUIRE_US=<n>   how long a vblank may wait for a swapchain image
 *                          (default 1000; a busy swapchain skips the frame)
 *   XG_PRESENT_LOG=1       a line per vblank on stderr: what the game handed
 *                          over since the last one (DrawOTag, DrawPrim,
 *                          PutDispEnv, the display area), the acquire and
 *                          present results, the swapchain and the window
 *   XG_PRESENT_STALL=<n>   the stall guard: a visible window that got no new
 *                          picture for n vblanks in a row (default 120)
 *                          gets a new swapchain (every third time a new
 *                          surface too); said once, counted at exit; 0: off
 *   XG_PRESENT_TEST=leak@<vblank>[/<n>]  at that vblank (and every n
 *                          after it) hold back every swapchain image
 *                          (acquired, never presented), as a compositor that
 *                          keeps them all would: tests the stall guard
 *   XG_GP0_PROBE=x,y       log every GP0 command that may draw VRAM pixel x,y
 *   XG_DUMP_PRESENT=<dir>  write the image presented in the window (the
 *                          display options and the note applied) as
 *                          <dir>/present_<vblank>.png when it changes;
 *                          XG_DUMP_RANGE applies. Without a window
 *                          (XG_HEADLESS) it is drawn offscreen at the
 *                          window's size (XG_WINDOW_WIDTH/HEIGHT)
 *
 * Display options (aspect, scale, present_filter, present_movie_filter,
 * widefill, fullscreen, the window's size and position) come from
 * settings.ini and XG_ASPECT, XG_SCALE, XG_PRESENT_FILTER, XG_WIDEFILL,
 * XG_FULLSCREEN...: see present.c. True widescreen
 * (wide aspects): wide.c, include/pc_wide.h, and its XG_WIDE_MODES,
 * XG_WIDE_GUARD, XG_WIDE_LOG, XG_WIDE_NOSHIFT.
 */

#include <stddef.h>
#include <stdint.h>

/* Show the current display area (vertical blank). Called by the vblank
 * interrupt of port/src/system on the game thread. */
void gpuVBlank(void);

/* Write VRAM now as <path>.png (8-bit RGB) and <path>.bin (raw 16-bit). */
void gpuDumpVram(const char* path);

/* For port/src/system/stall.c, on its own thread while the game thread is
 * stuck: one line on the display and the present path, and SIGUSR1's dump
 * if one is waiting (returns 1 when it wrote it). */
void gpuDescribe(char* out, size_t size);
int gpuServeStalledDump(void);

/* Copy the current VRAM (1024x512 halfwords) to dst, waiting for drawing
 * (NULL: only bring the CPU copy up to date). */
void gpuReadVram(uint16_t* dst);

/* A frame of the settings menu (port/src/ui) while the game is paused: the
 * last vblank's picture shown again with the menu over it. shot: also write
 * it as <shot>.png (XG_DUMP_PRESENT's directory). */
void gpuPresentMenu(const char* shot);

/* Game code run outside the game (the extras' sandbox, port/src/extras):
 * while a capture is set, what the game hands LIBGPU goes to it instead of
 * the GPU: VRAM transfers (LoadImage, StoreImage, MoveImage, ClearImage) and
 * GP0 packets (DrawOTag, DrawOTagEnv, DrawPrim; PutDrawEnv as its DR_ENV
 * words). PutDispEnv is ignored, and no draw-done callback is queued. VRAM,
 * the picture and the wide picture stay as the paused game left them. Rects
 * arrive clamped as LIBGPU clamps them. NULL: back to the GPU. */
typedef struct GpuCapture {
    void* user;
    void (*load)(void* user, int x, int y, int w, int h, const uint16_t* src);
    void (*store)(void* user, int x, int y, int w, int h, uint16_t* dst);
    void (*move)(void* user, int sx, int sy, int dx, int dy, int w, int h);
    void (*fill)(void* user, int x, int y, int w, int h, uint32_t rgb);
    void (*gp0)(void* user, const uint32_t* words, int count);
} GpuCapture;
void gpuSetCapture(const GpuCapture* capture);

/* ---- display options and the window (present.c) ---- */

struct SDL_Window;
union SDL_Event;

/* The game's window, as settings.ini left it (size, position, fullscreen). */
struct SDL_Window* presentCreateWindow(const char* title);

/* Hotkeys (port/src/system/pad.c): each changes an option, writes
 * settings.ini and shows a note in the window for about 2 s. */
void presentCycleAspect(void);      /* F3: 4:3 TV, 4:3 fill, 1:1, 16:9, 16:10, 21:9, 32:9 */
void presentCycleScale(void);       /* F4: fit, integer, stretch */
void presentShowNote(const char* text); /* a corner note, as the hotkeys show */
void presentCycleFilter(void);      /* F5: nearest, linear, sharp, bicubic, xbr */
void presentCycleFill(void);        /* F6: widefill bars, zoom, blur */
void presentToggleFullscreen(void); /* F11, Alt+Enter */

/* The window's events (moved, resized, fullscreen), kept for settings.ini. */
void presentWindowEvent(const union SDL_Event* e);

/* The settings menu (port/src/ui): the options as they are, and setting
 * them (written to settings.ini at once, applied from the next picture). */
const char* presentAspectText(void);       /* "4:3tv", "4:3fill", "1:1", "16:9", ..., or a custom "w:h" */
int presentSetAspectText(const char* text); /* 0: not an aspect (w:h at least 4:3 wide, or 1:1) */
int presentScaleMode(void);                /* 0 fit, 1 integer, 2 stretch */
void presentSetScale(int mode);
int presentFilter(void);                   /* PRESENT_FILTER_* */
void presentSetFilter(int filter);
int presentMovieFilter(void);              /* 24-bit pictures (movies): PRESENT_FILTER_SAME or one */
void presentSetMovieFilter(int filter);
int presentFillMode(void);                 /* widefill: 0 bars, 1 zoom, 2 blur */
void presentSetFill(int mode);
int presentExpandedVertical(void);
void presentSetExpandedVertical(int on);
int presentSetFullscreen(int on);          /* 0 if the window could not change */
void presentSetWindowSize(int w, int h);   /* the window when not fullscreen */
struct SDL_Window* presentWindow(void);
/* A point of the window's target (pixels) in the last picture's display
 * area: 0,0 its top left, its width x height the 4:3 picture; a wide
 * picture's margins below 0 and beyond the width (mouse.c). 0: no picture. */
int presentWindowToDisplay(float x, float y, float* dx, float* dy);

/* The aspect setting (aspect=, XG_ASPECT): its kind, and w:h (4:3, 1:1 or
 * the wide shape asked for, e.g. 16:9). The widescreen renderer reads it. */
#define PRESENT_ASPECT_4_3_TV 0   /* a TV's picture: its size and place in the 4:3 frame */
#define PRESENT_ASPECT_SQUARE 1   /* square pixels */
#define PRESENT_ASPECT_WIDE 2     /* wider than 4:3 */
#define PRESENT_ASPECT_4_3_FILL 3 /* the display area fills 4:3 */
int presentAspect(int* w, int* h);

/* present_filter (present.c, shaders/filters.glsl). */
#define PRESENT_FILTER_SAME (-1)  /* present_movie_filter only: as present_filter */
#define PRESENT_FILTER_NEAREST 0  /* exact: sharp pixels */
#define PRESENT_FILTER_LINEAR 1   /* bilinear */
#define PRESENT_FILTER_SHARP 2    /* area: crisp, even pixels at any scale */
#define PRESENT_FILTER_BICUBIC 3  /* smooth (Catmull-Rom) */
#define PRESENT_FILTER_XBR 4      /* smoothed edges in pixel art */
#define PRESENT_FILTER_COUNT 5

/* The hi-res copy (hires.c): upscale= and upscale_color= in settings.ini,
 * written at once, applied from the next picture. */
int gpuOrderedFallback(void);
void gpuSetOrderedFallback(int on);
const char* gpuOrderedStatus(void);
int hiresUpscale(void);          /* 1 exact (default), 0 auto, 2..12 */
void hiresSetUpscale(int v);
int hiresGpuBudget(void);       /* Auto GPU budget in ms; 0 disabled (default), 1..50 */
void hiresSetGpuBudget(int ms);
void hiresToggleUpscale(void);   /* Shift+F4: exact 1x <-> the upscaled choice (auto at first), with a note */
int hiresColor(void);            /* 0 true colour, 1 15-bit PS1 dither, 2 15-bit fine dither, 3 raw 8-bit */
void hiresSetColor(int c);
int hiresScaleNow(void);         /* the copy's N now; 1: the exact picture */
int hiresSlivers(void);
void hiresSetSlivers(int on);
int hiresPrecision(void);
void hiresSetPrecision(int on);
int hiresPerspective(void);
void hiresSetPerspective(int on);
int hiresCoarseLarge(void);
void hiresSetCoarseLarge(int on);
int hiresCpuEffects(void);
void hiresSetCpuEffects(int on);
int hiresSeamGuard(void);
void hiresSetSeamGuard(int on);

/* Texture filters in the enhanced display copy, independently per class
 * (PC_TEX_* in pc_wide.h). Nearest is the default for every class. */
#define TEXTURE_FILTER_NEAREST 0
#define TEXTURE_FILTER_LINEAR 1
#define TEXTURE_FILTER_EDGE 2
#define TEXTURE_FILTER_COUNT 3
/* Not a choice: the display copy's filter for an Enhanced text overlay with
 * smooth edges (textureGlyphSmooth): its outer edge blends by coverage. */
#define TEXTURE_FILTER_TEXT_SMOOTH 3
int textureFilter(int kind);
void textureSetFilter(int kind, int filter);
const char* textureFilterKey(int kind);
int textureHudUpscale(int battle);
void textureSetHudUpscale(int battle, int scale);
int textureHudScale(int kind);
int textureCelScale(int kind);
const char* textureCelRoot(void);
const char* textureCelStatus(void);
void textureCelSetRoot(const char* root);
void textureCelReload(void);
int textureAppearance(int kind);
int textureUseSpriteCel(int kind); /* Sprites -> Cel 8x: the field sprite cel library */
int textureSetAppearance(int kind, int mode);
void textureFreezeAppearances(void);
int textureUsePack(int kind);
int textureUseGlyphs(int kind);

/* User-owned packs, scoped to the compiled disc version. Counters describe
 * actual lookups/loads since reload, not a directory-wide inventory. */
typedef struct {
    int enabled, entries;
    size_t bytes;
    uint64_t loads, misses, invalid, draws, recolours, evictions;
    /* loads decoded on the game thread (the rest on the loader thread), of them sprite cels;
     * the sprite pack's room preload (pcSpritePackPreload): cels queued, cels drawn before
     * their decode finished (decoded there and then), cels left out (the cache budget) */
    uint64_t gameThreadLoads, celSync, celPreloads, celLate, celSkipped;
    /* sprite poses (texturePoseGet): loaded (any thread), of them on the pose loaders and on the game thread
     * (poseSync; poseLate: drawn before their queued decode finished, decoded there and then); queued by the
     * room preload or at a part's first drawn pose (posePartsSeen: those parts), left out (the pose cache's
     * budget), evicted */
    uint64_t poseLoads, poseLoader, poseSync, poseLate, posePreloads, posePartsSeen, poseSkipped, poseEvictions;
    /* field texture pages (the big replacements, above ASYNC_FROM; xg-hbc.27): installed (any thread), of them
     * decoded on the loader threads and on the game thread (pageSync: texture_pack_sync, or first needed in a
     * room's first frames); queued by the room preloads (rooms.txt, at the field load); drawn while still
     * loading in a room's first frames and waited for or taken over (pageWaits, pageWaitUs in all, the worst
     * room's pageWaitWorstUs; roomsOverBudget: rooms whose waits reached the 50 ms bound), drawn as the
     * original while loading (pageNative: first needed later in the room, or past the bound) */
    uint64_t pageLoads, pageLoader, pageSync, pagePreloads, pageWaits, pageWaitUs, pageWaitWorstUs, roomsOverBudget, pageNative;
    char name[128], author[128], message[256];
} TexturePackStats;
/* The sprite pack's room preload (parts.tsv, npcs.tsv beside its pack.ini; tools/field_sprite_index.py):
 * the cels of field `field`'s NPC slot `npc` (-1: every slot of the field) queued for the loader thread as
 * the field loads (initFieldData), so none is decoded on the game thread at its first draw. texturePackPreloadPending:
 * installs the finished loads (game thread) and gives the cels still decoding. */
void pcSpritePackPreload(int field, int npc);
/* npc -1 (the field load) also starts the texture pack's room pages (rooms.txt) on its loader threads, so they
 * are ready by the room's first frame; a page still loading when a room's first frames draw it is waited for
 * (bounded; xg-hbc.27). texture_pack_sync=1 (XG_TEXTURE_PACK_SYNC=1, a debug switch): every page decodes on
 * the game thread as it is asked for, the picture the background loads must give. */
/* The current party's character ids (0xFF: an empty slot), given just before pcSpritePackPreload(field, -1):
 * that preload also queues the party's field dolls' poses and cels (poses.tsv's member column; parts.tsv). */
void pcSpritePackParty(int a, int b, int c);
/* texturePackPreloadPending: cels and poses still decoding. texturePackPoseCache: the pose cache's bytes now,
 * its peak and its budget (the sprite pack's pack.ini cache_mb). */
int texturePackPreloadPending(void);
void texturePackPoseCache(size_t* bytes, size_t* peak, size_t* limit);
const char* texturePackRoot(void);
const char* texturePackVersion(void);
void texturePackSetRoot(const char* root);
void texturePackReload(void);
void texturePackStats(TexturePackStats* out);
/* The A/B comparison (port/src/ui/diagnostics.cpp's hotkey): 0 withholds every
 * replacement (texture, sprite cel and pose, glyph, Cel HUD, portrait packs)
 * without unloading anything, 1 shows them again (the default). */
void textureReplacementsShow(int on);
int textureReplacementsShown(void);
const char* textureSpritePackRoot(void); /* the sprite library's folder (pack.ini), "" when none */
const char* textureGlyphPackDir(void);   /* the glyph library's version folder, "" when none */
/* The game thread's pack decodes so far (counted while the CPU timing runs): sprite cels,
 * other images, sprite poses: their ms in all, the slowest, those over 1 ms. */
void texturePackLoadTimes(double ms[3], double worst[3], unsigned slow[3]);
/* vk.c: the GPU time per vblank without XG_GPU_TIMING (timestamp queries; 0 when the
 * device has none): collect on or off; the last vblank's total and per kind of pass
 * (gvkGpuTimingName: draw, wide, hires, hiwide, hiutil, copy, present), 0 before one. */
int gvkGpuTimingCollect(int on);
int gvkGpuTimingLast(double* total, double* kinds, int count);
const char* gvkGpuTimingName(int kind);
const char* textureDumpRoot(void);
void textureDumpSetRoot(const char* root);
int textureDumpEnabled(void);
const char* textureDumpStatus(void);

int textureGlyphsEnabled(void);
void textureSetGlyphs(int enabled);
/* Enhanced text with smooth edges (text_smooth, on by default): the overlay's
 * outer edge is blended into the picture by coverage, the face and outline
 * mix where they meet; off keeps the hard 8x edges. */
int textureGlyphSmooth(void);
void textureSetGlyphSmooth(int on);
/* The text category's live choice (texture_filter.c registers it when its
 * settings load), so atlas pieces are kept whenever text uses glyphs. */
void textureGlyphPolicy(int (*textUsesGlyphs)(void));
typedef struct {
    uint64_t overlays, packGlyphs, invalidated;
    int ramRecords, pieces, cached;
    size_t sourceBytes, cacheBytes;
} TextureGlyphStats;
void textureGlyphStats(TextureGlyphStats* out);

#endif /* PORT_GPU_H */
