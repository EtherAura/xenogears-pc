/*
 * A WebM (Matroska) file's video track (webm.c, xg-htn.35.10): its codec,
 * size, and an index of every frame (where its data is, its time, whether
 * it is a keyframe), for the movie pack's replacement videos. Written from
 * the EBML (RFC 8794) and Matroska (RFC 9559) specifications; only what a
 * video track needs: no lacing, no encryption or compression of blocks.
 */
#ifndef PORT_GPU_WEBM_H
#define PORT_GPU_WEBM_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t offset; /* of the frame's data in the file */
    uint32_t size;
    int key;
    int64_t timeNs; /* from the segment's start */
} WebmFrame;

typedef struct {
    void* io; /* SDL_IOStream */
    char codec[32];       /* CodecID, e.g. V_AV1 */
    int track, width, height;
    uint8_t* codecPrivate; /* e.g. the av1C record */
    size_t codecPrivateSize;
    int64_t defaultDurationNs; /* 0 when not given */
    WebmFrame* frame;
    int frames;
} Webm;

/* Open path and index its first video track's frames (in file order, which
 * for AV1 is display order); 0 and err when it cannot. */
int webmOpen(Webm* w, const char* path, char* err, size_t errCap);
/* Frame i's data into buf (grown as needed; *cap its size); its size, 0 on error. */
size_t webmRead(Webm* w, int i, uint8_t** buf, size_t* cap);
void webmClose(Webm* w);

#endif /* PORT_GPU_WEBM_H */
