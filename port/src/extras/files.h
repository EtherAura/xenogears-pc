#ifndef PORT_EXTRAS_FILES_H
#define PORT_EXTRAS_FILES_H

/*
 * What a file from the disc is, and the formats the asset browser opens
 * (files.c). docs/ASSETS.md has the formats and the file map.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    XG_KIND_UNKNOWN,
    XG_KIND_EMPTY,
    XG_KIND_TIM,      /* a TIM picture */
    XG_KIND_LZSS,     /* an LZSS stream (the decoder's walk checks out) */
    XG_KIND_BUNDLE,   /* an offset bundle (doPointerRelocation's table) */
    XG_KIND_TEXTURES, /* packed VRAM uploads (uploadTextureToVram) */
    XG_KIND_MODEL,    /* a model archive (fieldModelRelocation) */
    XG_KIND_WDS,      /* sample bank */
    XG_KIND_SMDS,     /* music score */
    XG_KIND_SEDS,     /* sound effect bank */
    XG_KIND_EXE,      /* a PS-X EXE */
    XG_KIND_TIMS,     /* TIM pictures back to back (a picture pack) */
    XG_KIND_COUNT
} XgKind;

const char* xgKindName(XgKind k);

/* The kind from the bytes alone. */
XgKind xgIdentify(const uint8_t* data, int size);

/* What docs/ASSETS.md's file map says is at directory dir, file file (from
 * 1) of a disc (1 or 2), in `out`; "" if it says nothing. */
const char* xgKnownLocation(int disc, int dir, int file, char* out, int outSize);

/* ---- LZSS ---------------------------------------------------------------------------- */

/* The stream's decompressed size if `data` is one, checked by walking it
 * without writing (every reference inside the output, the input not run
 * past its end by more than the padding): -1 if not. *used: input bytes. */
int xgLzssCheck(const uint8_t* data, int size, int* used);

/* Decompress with the game's own decoder (lzss.c decompress, in a sandbox:
 * sandbox.h) after xgLzssCheck; malloc'd, NULL if it is no stream. */
uint8_t* xgLzssDecompress(const uint8_t* data, int size, int* outSize);

/* ---- offset bundles ---------------------------------------------------------------------- */

/* A bundle: u32 count, count u32 offsets from its start (and a u32 end
 * offset after them when offset[0] == 4 * count + 8). Members run from their
 * offset to the next one's (the last to the end offset or the data's end).
 * 0 if `data` is none. */
typedef struct {
    int count;
    int hasEnd;
    int end;
} XgBundle;
int xgBundleParse(const uint8_t* data, int size, XgBundle* out);
int xgBundleMember(const uint8_t* data, int size, const XgBundle* b, int i, int* memberSize); /* offset, -1 */

/* The game's relocation of a bundle's table (pc_convert.h pcRelocTable, the
 * PC's doPointerRelocation) in a sandbox: 1 when each native pointer is the
 * table's offset from the bundle's start. */
int xgBundleCheckRelocation(const uint8_t* data, int size);

/* ---- TIM ---------------------------------------------------------------------------------- */

typedef struct {
    int bpp;                 /* 4, 8, 16 or 24 */
    int hasClut;
    int clutX, clutY, clutW, clutH; /* VRAM halfwords */
    const uint16_t* clut;
    int imgX, imgY, imgW, imgH;     /* VRAM halfwords */
    int width;               /* pixels */
    const uint8_t* pixels;
    int size;                /* bytes the TIM takes */
} XgTim;

int xgTimParse(const uint8_t* data, int size, XgTim* out); /* 0 if not a TIM */
/* TIMs back to back from the start: how many, their offsets into at[] (up
 * to max of them). */
int xgTimRun(const uint8_t* data, int size, int* at, int max);
int xgTimClutCount(const XgTim* t);                        /* CLUTs in its CLUT block (0: none) */
/* The picture through CLUT `clut` as RGBA (width x imgH): 5-bit channels
 * widened as c << 3 | c >> 2; texel value 0 transparent (alpha 0) when
 * zeroClear, as the GPU draws it; without a CLUT, 4/8-bit as grey. */
void xgTimDecode(const XgTim* t, int clut, int zeroClear, uint32_t* rgba);
/* The CLUT word (GP0's) of CLUT `clut` at the TIM's own VRAM place. */
uint16_t xgTimClutWord(const XgTim* t, int clut);

/* ---- packed VRAM uploads and models ----------------------------------------------------------- */

/* uploadTextureToVram's format: count, offsets, then {tag 0x1100/0x1101, x0,
 * y0, x1, y1, w, h, pixels} records. Returns the record count, 0 if not. */
int xgTexturesCheck(const uint8_t* data, int size);

/* A model archive: u32 part count, u32 flags (bit 0 relocated), 8 zero
 * bytes, 0x38-byte parts whose offsets stay inside. Returns the parts, 0. */
int xgModelCheck(const uint8_t* data, int size);

/* ---- sound -------------------------------------------------------------------------------- */

/* A WDS's presets (sample start, loop, ADSR...), 0 if not a WDS. */
int xgWdsPresets(const uint8_t* data, int size);
int xgWdsBank(const uint8_t* data, int size);     /* +0x20 */
/* A SEDS's effects, its default WDS bank (+0x16); 0 if not one. */
int xgSedsEffects(const uint8_t* data, int size, int* bank);
/* An SMDS's name (+0x1E offset), "" if none. */
const char* xgSmdsName(const uint8_t* data, int size, char* out, int outSize);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_FILES_H */
