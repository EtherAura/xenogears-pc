#ifndef XG_INTERPOLATION_CORE_H
#define XG_INTERPOLATION_CORE_H

/* Owned draw snapshots. Nothing here reads game memory or advances a clock. */
#include "gpu_internal.h"

#define INTERP_MAX_VERTICES 65536
#define INTERP_MAX_DRAWS 16384
#define INTERP_MAX_OPS 8192
#define INTERP_FREEZES 16
#define INTERP_SETS (INTERP_FRAMES+2+INTERP_FRAMES*INTERP_FREEZES)
#define INTERP_MAX_PIXELS (2u * 1024u * 1024u) /* owned 32-bit storage words */
/* Previous/current presentation endpoints, up to two completed native
 * buffers awaiting display, and the next frame being constructed. */
#define INTERP_FRAMES 5
#define INTERP_SAMPLE_PALETTE (1u << 18)
#define INTERP_SAMPLE_CAPTURE (1u << 19)
#define INTERP_SAMPLE_EVOLVING (1u << 20)
#define INTERP_SAMPLE_UNCACHED (1u << 21) /* owned immutable/ordered source */
#define INTERP_SAMPLE_NATIVE_CACHE (1u << 22)
#define INTERP_SAMPLE_STEP_SOURCE (1u << 23)
/* Mutually exclusive sample formats: captures use bit 23 for a step image;
 * ordinary owned textures use it for two losslessly packed PS1 words. */
#define INTERP_SAMPLE_NATIVE_WORD (1u << 23)
#define INTERP_SAMPLE_OPAQUE_BLACK (1u << 24) /* native words followed by a coverage bit plane */
#define INTERP_SAMPLE_NATIVE_STEP (1u << 16)
enum { INTERP_OP_CACHE, INTERP_OP_COPY, INTERP_OP_FROZEN };
typedef struct {
    uint32_t vertex, kind, data;
    int sx, sy, dx, dy, w, h;
} InterpOp;

typedef struct {
    uint64_t object, face, material;
    uint32_t first, count;
    int kind; /* host coverage provenance, never native draw behaviour */
    int collapse; /* authored wide tile: clamp negative width after lerping */
    int placement;
    float anchor[3];
} InterpDraw;

typedef struct {
    uint64_t scene, serial;
    int x, y, w, h;
    int viewSet, wide, viewX, viewY, viewW, viewH, originX, originY;
    int aspect, aspectW, aspectH, expanded;
    uint32_t background;
    int capture;
    int frozen;
    int frozenCount, frozenDirty, frozenWrites;
    VRect frozenRects[INTERP_FREEZES];
    VRect frozenRect; /* physical source copied into this snapshot, at local (0,0) */
    InterpOp* ops;
    uint32_t opCount, opCapacity;
    HiVertex* vertices;
    InterpDraw* draws;
    uint32_t* pixels;
    uint32_t* lookup; /* object/face -> draw index + 1 */
    uint32_t* anchors; /* object -> placement draw index + 1 */
    uint32_t vertexCount, drawCount, pixelCount;
    uint32_t vertexCapacity, drawCapacity, pixelCapacity;
    int cleared, valid;
    const char* rejection; /* first capture failure; host diagnostics only */
} InterpFrame;

/* Actual 8-bit UV/window texel bounds, in the owned capture image. */
int interpCaptureRect(const HiVertex* v, VRect* rect);
int interpCaptureSteps(const HiVertex* v,const int32_t* rows,VRect box,uint32_t* map,int n,int* overlap);
void interpFrameClear(InterpFrame* f);
void interpFrameFree(InterpFrame* f);
void interpFrameReject(InterpFrame* f, const char* reason);
void interpFramePlacement(InterpFrame* f, float x, float y, float z);
int interpFrameOp(InterpFrame* f, InterpOp op);
int interpFrameWords(InterpFrame* f, const uint32_t* words, uint32_t count, uint32_t* offset);
int interpFrameDraw(InterpFrame* f, const HiVertex* v, int count, uint64_t object, uint64_t face, uint64_t material);
int interpFramePixels(InterpFrame* f, const uint8_t* rgba, size_t count, uint32_t* offset);
/* Lossless owned palette + packed indices when it saves storage, otherwise
 * RGBA words. No palette or indices are borrowed from native resources. */
int interpFrameSample(InterpFrame* f, const uint8_t* rgba, size_t count, uint32_t* offset, uint32_t* format);
int interpSampleEqual(const InterpFrame* f, uint32_t offset, uint32_t format, const uint8_t* rgba, size_t count);
/* Unknown/discrete draws have object zero. Scene/projection discontinuities
 * reject the pair; changing faces retain their earlier authored image while
 * compatible faces move. Sprite poses follow a separate actor anchor.
 * Ordering-table order is held from the earlier endpoint. */
int interpFramesCompatible(const InterpFrame* before, const InterpFrame* after);
/* NULL means compatible. Stable reason tokens are also used by coverage tools. */
const char* interpFramesMismatch(const InterpFrame* before, const InterpFrame* after);
/* Local incompatibilities hold just this authored face/pose. */
const char* interpDrawMismatch(const InterpFrame* before, const InterpFrame* after, uint32_t draw);
int interpFramesMove(const InterpFrame* before, const InterpFrame* after);
uint32_t interpFramesMotionKinds(const InterpFrame* before, const InterpFrame* after);
/* The before frame's draw order and discrete attributes, with identified
 * projected positions between the two endpoints. Returns vertex count. */
int interpFrameVertices(const InterpFrame* before, const InterpFrame* after, double alpha, HiVertex* out, size_t cap);

/* Native vblank timestamps, with a one-native-frame presentation delay. */
typedef struct {
    uint64_t serial;
    double activated, interval;
    int previous, current;
} InterpTimeline;
void interpTimelineReset(InterpTimeline* t);
void interpTimelineActivate(InterpTimeline* t, uint64_t serial, int frame, double vblank);
double interpTimelineAlpha(const InterpTimeline* t, double vblank);

/* Pure host deadline calculation, independent of virtual PS1 events. */
typedef struct { uint64_t anchor, tick; int hz; } InterpPacer;
void interpPacerReset(InterpPacer* p, uint64_t now, int hz);
uint64_t interpPacerDeadline(const InterpPacer* p);
/* Skip missed deadlines instead of creating a burst. */
void interpPacerAdvance(InterpPacer* p, uint64_t now);
/* Preserve the requested output target; bound preparation under native/WSI
 * contention. The adaptive latch belongs to host scheduling, not game state. */
int interpPrepareRate(int requested, int current, double delivered, int* adaptive);

#endif
