/*
 * Replacement movie frames (moviepack.c, xg-htn.35.10): a texture pack's
 * pictures for the game's movies, shown in place of the decoded frames, frame
 * for frame. The game still plays its own movie (the stream, the MDEC, the XA
 * audio and its timing, the Japanese audio when chosen), so a replacement
 * keeps every timing; a frame with no picture ready shows the original.
 *
 * Pack layout, under the texture pack's version folder (gpu.h,
 * texturePackRoot()/texturePackVersion()), one of:
 *   movies/d<disc>s<stream>/<frame>.png   e.g. movies/d1s03/00042.png
 *   movies/d<disc>s<stream>.webm          AV1 (dav1d, an optional component)
 * disc 1 or 2, stream the movie's stream (directory 0x18/1, file stream + 2;
 * two digits), frame its number in the STR headers (from 1; five digits;
 * every stream on the four discs counts from 1 at its first sector, none
 * starts again). A video's frame for frame N is the one shown at (N - 1) /
 * 15 s, so a video of the whole stream at any frame rate lines up.
 * PNGs: compressed, or uncompressed (stored deflate blocks: read without
 * inflating, pngfast.c). Any size up to 4096 x 4096 (16 M pixels), best
 * one size for a stream; the picture covers the frame's rectangle
 * (320 x 224 on the discs; the movie display shows 216 lines of it).
 * 24-bit movies are shown in the display's place; the field's 15-bit movie
 * backgrounds (drawn into, under the field's sprites) go into the hi-res
 * copy of VRAM at the frame's upload, so they need upscaling on.
 * With texture discovery on (texture_dump), each frame the game decodes is
 * written there under the same name, at its own size, for upscaling;
 * dump_png_uncompressed writes stored PNGs.
 */
#ifndef PORT_GPU_MOVIEPACK_H
#define PORT_GPU_MOVIEPACK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The movie player (src/video_player, PLATFORM_PC hooks): a stream starts
 * (disc 1 or 2, stream number, the first frame it plays); a frame is
 * complete in VRAM (x0, y0 to x1, y1 exclusive, in VRAM halfwords; 24-bit
 * frames are 3/2 halfwords a pixel); the player stops. */
void moviePackBegin(int disc, int stream, int first);
void moviePackFrame(int frame, int x0, int y0, int x1, int y1, int rgb24);
/* A slice of a frame just loaded (x0, y0 to x1, y1 exclusive) into the frame
 * at (fx0, fy0) to (fx1, fy1): a 15-bit frame's picture goes into the hi-res
 * copy slice by slice (the field's one movie buffer is loaded over while it
 * is drawn from). */
void moviePackSlice(int frame, int x0, int y0, int x1, int y1, int fx0, int fy0, int fx1, int fy1, int rgb24);
void moviePackEnd(void);
/* The device goes (gvkShutdown): the reader thread stops, pictures freed. */
void moviePackShutdown(void);

/* Settings and the menu: movie_replacements (on by default; it matters only
 * with a pack holding movies). */
int moviePackEnabled(void);
void moviePackSetEnabled(int on);
typedef struct {
    int active;        /* this stream has pictures in the pack */
    int disc, stream;  /* the last stream started */
    int w, h;          /* its pictures' size (0 before the first) */
    char format[48];   /* PNG (stored / compressed counts), AV1 WebM */
    uint64_t storedPngs, compressedPngs;
    double decodeMs, decodeMsMax; /* a picture's reading time: mean, worst */
    /* Retained RGBA allocations across all slots, including old sessions;
     * excludes decoder working memory and files currently being decoded. */
    uint64_t cacheBytes;
    unsigned cacheFrames, loadingFrames;
    /* pictures shown; frames with no picture; frames whose picture was not
     * read in time (the last one stayed); unusable files; frames written by
     * discovery */
    uint64_t shown, missing, late, invalid, dumped;
    char message[256];
} MoviePackStats;
void moviePackStats(MoviePackStats* out);

/* The present pass (vk.c): for a 24-bit display of VRAM (x, y in halfwords;
 * w, h in pixels), the replacement of the movie frame it shows, if one is
 * ready: 1 and *out. The frame counts only while its VRAM is as the player
 * left it (nothing drawn or loaded over it since). */
typedef struct {
    uint64_t id;  /* another picture, another id */
    int w, h;     /* the picture's size */
    int rect[4];  /* the movie frame in display pixels: x, y, width, height */
} MoviePicture;
int moviePackPicture(int x, int y, int w, int h, MoviePicture* out);
/* Picture id's pixels (bytes R, G, B, A) into dst, pitch bytes a row; 0 if
 * it is no longer held. */
int moviePackCopy(uint64_t id, uint8_t* dst, size_t pitch);

/* vk.c: part of a movie frame's picture into the hi-res copy (gvkHiScale()
 * times VRAM): the frame at VRAM (fx, fy), fw x fh 1x pixels, its picture w x
 * h RGBA (id: the same picture again is not uploaded again), the part over
 * VRAM (x0, y0) to (x1, y1) (inclusive); mask: the PS1 mask bit. 1 when
 * recorded. */
int gvkHiPicture(int x0, int y0, int x1, int y1, const uint8_t* rgba, int w, int h, uint64_t id, int fx, int fy,
                 int fw, int fh, int mask);

/* dump_png_uncompressed: discovery's PNGs (movie frames, texture dumps)
 * written uncompressed (stored deflate blocks): faster to write and read. */
int moviePackDumpStored(void);
void moviePackSetDumpStored(int on);

#ifdef __cplusplus
}
#endif

#endif /* PORT_GPU_MOVIEPACK_H */
