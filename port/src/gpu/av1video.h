/*
 * A replacement movie video (av1video.c, xg-htn.35.10): AV1 in WebM,
 * decoded by dav1d (an optional build component, XG_HAVE_DAV1D) into RGBA
 * pictures, frame by frame, seeking from the keyframe before a frame when
 * asked out of order.
 */
#ifndef PORT_GPU_AV1VIDEO_H
#define PORT_GPU_AV1VIDEO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MovieVideo MovieVideo;

/* 1 when this build can decode AV1. */
int movieVideoAvailable(void);
/* Open a WebM file with an AV1 video track; NULL and err when it cannot. */
MovieVideo* movieVideoOpen(const char* path, char* err, size_t errCap);
void movieVideoClose(MovieVideo* v);
/* Its frames, their size, frame i's time from the first frame's (ns). */
int movieVideoFrames(const MovieVideo* v);
void movieVideoSize(const MovieVideo* v, int* w, int* h);
int64_t movieVideoTime(const MovieVideo* v, int i);
/* The frame shown at time t (ns from the first frame): the last one that
 * starts at or before it; -1 before the first. */
int movieVideoFrameAt(const MovieVideo* v, int64_t t);
/* Frame i's picture as RGBA bytes (caller frees), *w x *h; NULL on error
 * (err). In order it decodes one frame; otherwise from the keyframe before. */
uint8_t* movieVideoDecode(MovieVideo* v, int i, int* w, int* h, char* err, size_t errCap);

#ifdef __cplusplus
}
#endif

#endif /* PORT_GPU_AV1VIDEO_H */
