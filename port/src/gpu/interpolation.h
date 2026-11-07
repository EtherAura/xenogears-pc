#ifndef XG_INTERPOLATION_H
#define XG_INTERPOLATION_H
#include "interpolation_core.h"
#include "texture.h"

int interpolationEnabled(void);
int interpolationCapturing(void);
void interpolationSetEnabled(int enabled);
void interpolationReset(void);
void interpolationShutdown(void);
void interpolationPacket(const void* packet);
int interpolationGeometryPacket(void);
int interpolationWavePacket(void);
int interpolationWaveFrame(void);
int interpolationCaptureSample(void);
void interpolationPacketsDone(void);
/* Select exactly one authored draw path; expanded snapshots use the wide
 * renderer's coordinates, clipping and HUD treatment. */
int interpolationRecording(int wide);
void interpolationDraw(const HiVertex* vertices, int count, int texClass,
                       int wide, int shiftX, int shiftY, unsigned piece);
/* A field sprite's pose (gp0.c spritePose): this sample (coloured with the
 * palette of `palette`) in place of a lookup, one face per actor. */
void interpolationPoseDraw(const HiVertex* vertices, int count, int wide, int shiftX, int shiftY,
                           const DecodedTexture* palette, const TextureSample* sample);
void interpolationClear(VRect rect, uint32_t rgb);
void interpolationTransfer(VRect rect);
void interpolationCopy(int wide, int sx, int sy, int dx, int dy, int w, int h);
void interpolationCacheClear(void);
void interpolationSourceChanging(VRect rect);
void gp0InterpolationCache(uint32_t* words);
/* Private scratch: the native sampler, with no native register/cache writes. */
int gp0InterpolationSpans(const HiVertex* v, int32_t* rows, VRect* box);
void interpolationUnsupported(void);
void interpolationReject(const char* reason);
void interpolationReport(const char* event, const char* reason, double vblank, double alpha);
/* Select the completed snapshot for the displayed framebuffer. */
const InterpFrame* interpolationPicture(const GpuDisplay* d, double vblank, int* before, int* after, double* alpha);
/* Host deadlines query the last native activation; they never activate a
 * newly built back buffer between native vblanks. */
const InterpFrame* interpolationSample(const GpuDisplay* d, double vblank, int* before, int* after, double* alpha);
int presentationRate(void);
void presentationSetRate(int hz);
void presentationRateDescribe(char* out, size_t size);
/* hw.c presents only in idle waits, outside native interrupt delivery.
 * available is the real time before the next native event, in nanoseconds. */
void gpuHostClock(int realtime);
uint64_t gpuHostDeadline(void);
void gpuHostPoll(uint64_t now, double vblank, uint64_t available);
void gpuHostReset(void);
void gpuHostInvalidate(void);
const InterpFrame* interpolationFrame(int index);
const char* interpolationStatus(void);

/* gp0.c: exclude feedback and a stale native cached CLUT. */
int gp0InterpolationTextureSafe(const DecodedTexture* decoded);
const char* gp0InterpolationTextureReason(const DecodedTexture* decoded);
int gp0InterpolationCaptureSafe(void);
void gp0InterpolationTransitionBounds(HiVertex* triangle);

/* vk.c: all writes are to private images, never native/hi-res feedback. */
int gvkInterpolationFreeze(int slot, int version, VRect rect, int wide);
int gvkInterpolationRead(uint32_t* dst, int x, int y, int w, int h);
void gvkInterpolationCapture(int slot, const InterpFrame* frame);
int gvkInterpolationReady(void);

#endif
