/*
 * Shader presets after the game picture (xg-htn.35.28): RetroArch's .slang
 * shaders and .slangp presets, compiled at run time with Khronos glslang
 * (an optional build component: XG_GLSLANG), drawn by the present pass in
 * place of its own scaling (shaderchain.c has the supported subset).
 *
 * The setting present_shader names a .slangp preset or a single .slang
 * shader; empty, the default, is the ordinary picture. present_shader_params
 * keeps the parameters the player changed ("name=value;...").
 */
#ifndef PORT_GPU_SHADERCHAIN_H
#define PORT_GPU_SHADERCHAIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the preset (slangp.c: files only, no device) -------------------------- */

#define SLANG_MAX_PASSES 64 /* as RetroArch (Mega Bezel presets have up to 51) */
#define SLANG_MAX_TEXTURES 64
#define SLANG_MAX_PARAMS 1024
#define SLANG_NAME 64

enum { SLANG_SCALE_SOURCE, SLANG_SCALE_VIEWPORT, SLANG_SCALE_ABSOLUTE, SLANG_SCALE_ORIGINAL };
enum { SLANG_WRAP_BORDER, SLANG_WRAP_EDGE, SLANG_WRAP_REPEAT, SLANG_WRAP_MIRROR };

typedef struct {
    char name[SLANG_NAME];
    char desc[128];
    float value, def, min, max, step;
} SlangParam;

typedef struct {
    char path[1024];       /* the .slang file */
    char alias[SLANG_NAME]; /* aliasN, or the shader's #pragma name */
    int filter;            /* filter_linearN: -1 unset, 0 nearest, 1 linear (its input) */
    int wrap;              /* wrap_modeN (its input) */
    int mipmap;            /* mipmap_inputN */
    int floatFb, srgbFb;   /* float_framebufferN, srgb_framebufferN */
    int scaleSet;          /* scale_type given (else: source 1x, or the viewport for the last pass) */
    int scaleType[2];      /* SLANG_SCALE_*, x and y */
    float scale[2];
    int frameCountMod;
    char format[48];       /* #pragma format, "" for the default */
    /* the shader's two stages, includes expanded, #pragma lines removed */
    char* vertex;
    char* fragment;
} SlangPass;

typedef struct {
    char name[SLANG_NAME];
    char path[1024];
    int linear, wrap, mipmap;
} SlangTexture;

typedef struct {
    int passes;
    SlangPass pass[SLANG_MAX_PASSES];
    int textures;
    SlangTexture texture[SLANG_MAX_TEXTURES];
    int params;
    SlangParam param[SLANG_MAX_PARAMS];
    char error[512];       /* why loading failed (and the file) */
} SlangPreset;

/* Read a .slangp preset (with its #reference chain) or one .slang shader
 * into p: every pass's source split into its stages and every parameter.
 * 1 when it can be used, else 0 with p->error saying why. */
int slangLoad(SlangPreset* p, const char* path);
void slangFree(SlangPreset* p);
/* The preset's parameter values from "name=value;..." (present_shader_params). */
void slangApplyParams(SlangPreset* p, const char* text);
/* The parameters that differ from their defaults, as "name=value;..." (cap bytes). */
void slangParamsText(const SlangPreset* p, char* out, size_t cap);

/* ---- the settings menu's side -------------------------------------------------- */

/* 1 when this build has the shader compiler. */
int shaderChainAvailable(void);
/* Which chain the functions below set: 0 the picture's (present_shader), 1
 * the movies' (present_movie_shader, 24-bit pictures; its path may also be
 * "same", the picture's chain, or "none"). 0 until chosen. */
void shaderChainEdit(int movie);
/* The movies' chain: 0 the picture's, 1 none, 2 a preset of their own. */
int shaderChainMovieMode(void);
/* The preset in use ("" when off) and the last load's result: "" when it
 * loaded (or none is set), else the error. */
const char* shaderChainPath(void);
const char* shaderChainError(void);
/* Use this preset ("" or NULL: none); saved in the settings. Loading and
 * compiling happen at once; 1 when it is in use. */
int shaderChainSetPath(const char* path);
/* Its passes (0: none in use), its parameters. */
int shaderChainPasses(void);
int shaderChainParamCount(void);
const SlangParam* shaderChainParam(int i);
void shaderChainSetParam(int i, float value);
void shaderChainResetParams(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_GPU_SHADERCHAIN_H */
