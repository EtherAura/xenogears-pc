#ifndef XG_GPU_TEXTURE_H
#define XG_GPU_TEXTURE_H

#include "gpu_internal.h"

/* An ordered patch of logical UVs, before page wrap and texture-window
 * remapping. Width/height 1..256; origins -32768..32767. Cache identity
 * includes the location; content keys deliberately do not. */
typedef struct {
    uint16_t tpage, clut;
    uint32_t window;
    int u, v, w, h;
    /* A stable region (textureRequestSnapStable): texels whose upload stamps lie
     * outside stableFrom..stableTo decode as 0 (clear) and so stay out of the
     * content key. 0, 0 for every other request. */
    uint32_t stableFrom, stableTo;
} TextureRequest;

#define TEXTURE_CACHE_SLOTS 4096 /* a field with a 3D pack seen from a low free camera meets over a thousand a frame */
#define TEXTURE_CACHE_BYTES (64u << 20) /* decoded pixels kept at most */
typedef struct {
    TextureRequest request;
    int depth, paletteSize;
    uint64_t texelKey, paletteKey, revision;
    /* RGBA byte order on every host; alpha 0 transparent, 128 STP, 255 opaque.
     * Indexed requests retain one index byte per pixel and the exact CLUT.
     * Direct pixels retain two little-endian bytes per pixel in texels. */
    const uint8_t* rgba;
    const uint8_t* texels;
    uint16_t palette[256];
} DecodedTexture;

typedef struct {
    uint64_t hits, decodes, evictions, renderedSkips;
    uint64_t paletteOnly; /* re-decodes that only recoloured: the texels were unchanged */
    size_t bytes;
    int entries;
} TextureStats;

void textureCacheReset(void);
/* Snap to a covering unchanged upload when it fits the same logical page.
 * Wrapped/windowed or assembled patches retain their sampled rectangle. */
TextureRequest textureRequestSnap(TextureRequest request);
/* For 3D draws (a field's or an arena's polygons): the same, else the stable
 * region of the page around the patch (one load: a field texture page; see
 * vram.c), else the sampled rectangle. Sprites, HUD and text keep the
 * plain snap, so their keys are what they were. */
TextureRequest textureRequestSnapStable(TextureRequest request);
/* Returned storage remains valid until this slot is evicted/re-decoded or
 * reset. Direct-colour rendered tiles return NULL, even after readback. */
const DecodedTexture* textureCacheGet(TextureRequest request);
/* The texel key textureCacheGet would give the request now, from VRAM alone:
 * no entry is made (texture_pack.c probes a sprite cell's neighbours by it). */
uint64_t textureTexelKey(TextureRequest r);
/* The same keys from any 1024 x 512 VRAM image (the extras' viewers draw from VRAM of their
 * own): the texel key of a request, and the palette key of `size` CLUT entries (0 for none),
 * as DecodedTexture's texelKey and paletteKey. */
uint64_t textureTexelKeyIn(const uint16_t* vram, TextureRequest r);
uint64_t texturePaletteKeyIn(const uint16_t* vram, uint16_t clut, int size);
/* Field sprite poses (Enhanced; the sprite library's poses/ folder, built by
 * tools/field_sprite_pose.py): one whole-figure 8x index map per pose of an
 * actor, drawn in place of its part quads. The image's corners in the
 * reference part's texel frame (gp0.c spritePose): top left (a[0], b[0]),
 * top right (a[1], b[1]), bottom left (a[2], b[2]). */
typedef struct {
    int w, h, scale;          /* native texels, and the map's multiplier */
    float a[3], b[3];
    const uint8_t* indices;   /* w*scale x h*scale palette indices */
    uint64_t revision;        /* unique per loaded image (the GPU keeps it by this) */
} SpritePose;
/* The pose with this key (gp0.c spritePoseKey), or NULL; cached, missing ones too. */
const SpritePose* texturePoseGet(uint64_t key);

/* Enhanced field sprites: one actor's part quads, in one ordering table
 * (pcSpritePosePart, reset with the packets at each DrawOTag); gp0.c draws
 * the actor's pose from the library (texturePoseGet) in place of its parts. */
#define POSE_PARTS 16
typedef struct {
    const void* xy[3];    /* the part's vertices 0, 1 (beside 0), 2 (below 0) in its packet */
    TextureRequest cell;  /* its whole sheet cell */
    int16_t sx[3], sy[3]; /* the same vertices in sprite space (pcSpritePoseSpace) */
    uint8_t space;        /* 1 when sx, sy are set */
} PosePart;
typedef struct {
    const void* actor;
    int n, overflow;
    PosePart part[POSE_PARTS];
    int computed, ref, drawn[2];
    uint64_t key;
    const SpritePose* pose;
} PoseGroup;
PoseGroup* widePoseGroup(int group); /* WidePacket.poseGroup */
unsigned widePoseSerial(void);        /* ordering tables drawn so far (pose hold times) */
void textureCacheStats(TextureStats* out);

/* Borrowed decoded pixels or a cached, patch-clamped 2x edge variant.
 * The GPU copies these into the current frame's immutable storage before
 * another lookup may evict them. w/h are logical texels; packs support
 * integer scales through 8, at most 1 million replacement pixels.
 * keep: a pack image, kept on the GPU once uploaded (vk.c), by revision;
 * indices (one byte a pixel, rgba NULL): an index map the GPU colours with
 * the primitive's live CLUT. */
typedef struct TextureSample {
    const uint8_t* rgba;
    uint64_t revision;
    int u, v, w, h, scale;
    const uint8_t* indices;
    int keep;
} TextureSample;
int textureSampleGet(const DecodedTexture* texture, TextureRequest patch, int filter, TextureSample* out);
void textureSampleReset(void);
/* The 2x/4x variant cache (Edge, Upscaled), since a reset: lookups served,
 * variants made (one malloc each), Scale2x passes run, the bytes made, and
 * each variant's bytes once a frame it is drawn in (the GPU copies those). */
typedef struct {
    uint64_t hits, made, passes, madeBytes, evictions, frameBytes;
    size_t bytes;
    int entries;
} TextureSampleStats;
void textureSampleStats(TextureSampleStats* out);
int textureUpscaleSample(const DecodedTexture* texture, TextureRequest patch, int scale, TextureSample* out);
int textureCelCanvas(TextureRequest canvas, int scale);
int textureCelSample(const DecodedTexture* texture, TextureRequest patch, int scale, TextureSample* out);
void textureCelShutdown(void);

/* Pack images and known misses held (texture_pack.c). */
#define TEXTURE_PACK_SLOTS 8192 /* a room's NPC cels, the party's (up to ~2600 at three) and its pages and misses */
void texturePackShutdown(void);
/* Bounded PNG decoder shared by independent user artwork libraries. */
struct SDL_Surface* texturePackReadPng(const char* path, int w, int h, int* scale, int* found);
/* vk.c: the GPU keeps pack images and colours index maps itself, so whole
 * images go to it as they are (no crops, no recolouring on the CPU). */
void texturePackSetGpu(int on);
int texturePackEnabled(void);
int texturePackChosen(void); /* enabled, and the user's own pack (texture_pack), not the default library */
int texturePackSample(const DecodedTexture* texture, TextureRequest patch, int filter, TextureSample* out);
int textureSpriteCelEnabled(void);
/* A library's image by content key, for pictures drawn from VRAM other than the game's (the
 * extras' viewers, port/src/extras/replace.c): TEXTURE_FIND_PACK, the texture pack (an RGBA
 * image by texel and palette key, else its index map by texel key; limit the palette's size,
 * 0 for 15-bit); TEXTURE_FIND_CEL, the sprite library's index map by texel key. 1 with *out,
 * 0 when the library has none, -1 while it is still loading (ask again). Not withheld by the
 * A/B comparison (textureReplacementsShow): a viewer chooses for itself. The pixels stay the
 * library's: valid until its next look-up. textureSpriteCelKnown: the sprite library holds
 * a file for this key (no decode). */
#define TEXTURE_FIND_PACK 0
#define TEXTURE_FIND_CEL 1
typedef struct {
    const uint8_t* rgba;    /* an RGBA image (alpha 0 clear, 128 STP, 255 opaque), or */
    const uint8_t* indices; /* an index map, coloured with the drawing's CLUT */
    int w, h, scale;        /* native texels and the multiplier */
    uint64_t revision;      /* unique per loaded image */
} TexturePackImage;
int texturePackFind(int library, uint64_t texel, uint64_t palette, int w, int h, int limit, TexturePackImage* out);
int textureSpriteCelKnown(uint64_t key);
int textureSpriteCelSample(const DecodedTexture* texture, TextureRequest patch, int filter, TextureSample* out);
/* Independent portrait lookup; shared by live drawing and motion snapshots. */
int portraitPackSample(const DecodedTexture* texture, TextureRequest patch, TextureSample* out);
int portraitPackClassSample(const DecodedTexture* texture, TextureRequest patch, int kind, TextureSample* out);
uint64_t texturePackGeneration(void);
int texturePackGlyph(uint64_t key, int w, int h, int limit, const uint8_t** indices, int* scale);
int textureHDGlyph(uint64_t key,int w,int h,int limit,const uint8_t** indices,int* scale);

void textureGlyphReset(void);
void textureGlyphUpload(int x, int y, int w, int h, const uint16_t* data);
void textureGlyphDiscover(TextureRequest request);
int textureGlyphSample(const DecodedTexture* texture, TextureSample* out);
/* Compose after the caller has resolved the category's appearance policy. */
int textureGlyphCompose(const DecodedTexture* texture, TextureSample* out);
/* A registered atlas glyph with authored subpixel artwork. Quad halves use
 * this complete canvas rather than their separately rounded native spans. */
int textureGlyphCanvas(TextureRequest canvas);
/* Content-verified retail button art only; the backing VRAM stays original. */
int texturePromptRemap(TextureRequest from, TextureRequest* to);
uint64_t textureGlyphKey(const uint8_t* indices, int w, int h, int depth);

/* Opt-in draw observer, independent of resolution. Unset XG_TEXDUMP: no
 * allocations, decodes, readbacks or files. Dump I/O failures disable it. */
void textureDumpInit(void);
void textureDumpShutdown(void);
int textureDumpEnabled(void);
/* stable: a 3D draw, snapped as textureRequestSnapStable. */
void textureDumpObserve(uint32_t tex, uint32_t window, uint32_t uvMin, uint32_t uvMax, int stable);
void textureDumpVBlank(uint32_t vblank);
void textureDumpMode(int mode);
void textureDumpGlyph(uint64_t key, const uint8_t* indices, int w, int h, int limit);

#endif
