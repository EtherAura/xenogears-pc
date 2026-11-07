/*
 * Shader presets after the game picture (shaderchain.h, xg-htn.35.28).
 *
 * The present pass (vk.c) draws the game picture, as present.frag shows it
 * (15- or 24-bit, the hi-res copy, the wide picture), 1:1 into this file's
 * Original image; the preset's passes run on it; the last one draws into the
 * window's destination rectangle (present.c's layout), where the menu and the
 * note follow as usual. Off (no preset), the present pass is unchanged.
 *
 * The RetroArch slang format, as libretro documents it, written here without
 * RetroArch's code:
 *   - passes with scale_type source / viewport / absolute / original, scale,
 *     filter_linear, wrap_mode, mipmap_input, float_framebuffer,
 *     srgb_framebuffer, frame_count_mod, alias (and #pragma name, #pragma
 *     format); the last pass draws at the window's size unless it has a scale
 *     of its own (then it is stretched to the window with its filter);
 *   - textures: Original, Source, OriginalHistory#, PassOutput#,
 *     PassFeedback#, the aliases (and <alias>Feedback), the preset's LUTs
 *     (PNG, BMP, or JPEG with our own decoder, jpeg.c);
 *   - uniforms (block or push constants): MVP, OutputSize, OriginalSize,
 *     SourceSize, FinalViewportSize, FrameCount, FrameDirection,
 *     FrameTimeDelta, Rotation (0), OriginalAspect, OriginalFPS,
 *     CurrentSubFrame and TotalSubFrames (1), every texture's <name>Size, and
 *     the #pragma parameter values;
 *   - #reference presets, #include in shaders.
 * Not supported: .glslp/.cgp presets, shaders in the Slang shading language
 * (a .slang file without #version: refused with a note; no Slang compiler
 * yet), shaders with more than one uniform block, sampler arrays, HDR output
 * and sub-frames; an unknown uniform reads 0 (logged once), an unknown
 * texture is an error.
 *
 * The compiler is Khronos glslang (XG_HAVE_GLSLANG, an optional build
 * component); without it a preset cannot be used and the menu says so.
 */

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "shaderchain.h"
#include "shaderchain_vk.h"
#include "jpeg.h"
#include "spirv.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system/settings.h"

#ifdef XG_HAVE_GLSLANG
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#endif

#define FRAMES SHADER_CHAIN_FRAMES
#define MAX_FINALS 4

/* ---- instance and device functions ---- */

#define SC_INSTANCE_FUNCS(X)              \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceFormatProperties)

#define SC_FUNCS(X)                  \
    X(vkCreateImage)                 \
    X(vkDestroyImage)                \
    X(vkGetImageMemoryRequirements)  \
    X(vkAllocateMemory)              \
    X(vkFreeMemory)                  \
    X(vkBindImageMemory)             \
    X(vkCreateBuffer)                \
    X(vkDestroyBuffer)               \
    X(vkGetBufferMemoryRequirements) \
    X(vkBindBufferMemory)            \
    X(vkMapMemory)                   \
    X(vkCreateImageView)             \
    X(vkDestroyImageView)            \
    X(vkCreateSampler)               \
    X(vkDestroySampler)              \
    X(vkCreateShaderModule)          \
    X(vkDestroyShaderModule)         \
    X(vkCreateDescriptorSetLayout)   \
    X(vkDestroyDescriptorSetLayout)  \
    X(vkCreatePipelineLayout)        \
    X(vkDestroyPipelineLayout)       \
    X(vkCreateGraphicsPipelines)     \
    X(vkDestroyPipeline)             \
    X(vkCreateDescriptorPool)        \
    X(vkDestroyDescriptorPool)       \
    X(vkAllocateDescriptorSets)      \
    X(vkUpdateDescriptorSets)        \
    X(vkDeviceWaitIdle)              \
    X(vkCmdPipelineBarrier2)         \
    X(vkCmdBeginRendering)           \
    X(vkCmdEndRendering)             \
    X(vkCmdBindPipeline)             \
    X(vkCmdBindDescriptorSets)       \
    X(vkCmdBindVertexBuffers)        \
    X(vkCmdDraw)                     \
    X(vkCmdSetViewport)              \
    X(vkCmdSetScissor)               \
    X(vkCmdPushConstants)            \
    X(vkCmdCopyBufferToImage)        \
    X(vkCmdClearColorImage)          \
    X(vkCmdBlitImage)

#define SC_DECLARE(name) static PFN_##name name;
SC_INSTANCE_FUNCS(SC_DECLARE)
SC_FUNCS(SC_DECLARE)

/* ---- state ---- */

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;   /* every level: sampling */
    VkImageView attach; /* level 0: drawing */
    int w, h, levels;
    VkFormat format;
} Tex;

enum { TEX_ORIGINAL, TEX_SOURCE, TEX_HISTORY, TEX_OUTPUT, TEX_FEEDBACK, TEX_LUT };
enum {
    SEM_UNKNOWN, SEM_MVP, SEM_OUTPUT_SIZE, SEM_FINAL_VIEWPORT, SEM_FRAME_COUNT, SEM_FRAME_DIRECTION,
    SEM_FRAME_TIME_DELTA, SEM_ROTATION, SEM_ORIGINAL_ASPECT, SEM_ORIGINAL_FPS, SEM_SUBFRAME, SEM_TOTAL_SUBFRAMES,
    SEM_TEX_SIZE, SEM_PARAM
};

typedef struct {
    int sem, texKind, index; /* SEM_TEX_SIZE: a TEX_* and its index; SEM_PARAM: the parameter */
    uint32_t offset, size;
    int kind; /* SPV_MEMBER_* */
} Member;

typedef struct {
    char name[64];
    uint32_t binding;
    int kind, index;
    VkShaderStageFlags stages;
} Binding;

typedef struct {
    VkShaderModule vs, fs;
    VkDescriptorSetLayout setLayout;
    VkPipelineLayout layout;
    VkPipeline pipeline; /* into its own output */
    VkFormat format;
    struct { VkFormat format; VkPipeline pipeline; } finals[MAX_FINALS];
    int finalCount;
    int hasUbo;
    uint32_t uboBinding, uboSize;
    VkShaderStageFlags uboStages, pushStages;
    uint32_t pushSize;
    int members, pushMembers;
    Member member[SPV_MAX_MEMBERS];     /* the uniform block's */
    Member pushMember[SPV_MAX_MEMBERS]; /* the push constants' */
    int bindings;
    Binding binding[SPV_MAX_SAMPLERS];
    VkDescriptorSet sets[FRAMES];
    VkBuffer ubuf[FRAMES];
    VkDeviceMemory umem[FRAMES];
    uint8_t* umap[FRAMES];
    Tex out[2]; /* out[cur]: this frame's; out[cur ^ 1] the last frame's (feedback) */
    int cur;
    int feedback; /* another pass (or itself) reads PassFeedback of it */
    int mipmapped; /* its output is read with mipmaps (the next pass's mipmap_input) */
    int w, h;
    int direct; /* the last pass, drawn straight into the window */
} Pass;

/* A chain: the picture's (present_shader) and the movies' (24-bit pictures,
 * present_movie_shader: the picture's, none, or a preset of their own). */
typedef struct {
    const char* key;       /* its settings: the preset's path */
    const char* paramsKey; /* and its changed parameters */
    int ready;
    ShaderChainDevice dev;
    VkPhysicalDeviceMemoryProperties memProps;

    char path[1024];
    char error[512];
    SlangPreset preset;
    int loaded; /* preset holds the in-use chain, pipelines made */
    Pass pass[SLANG_MAX_PASSES + 1]; /* + the stretch pass, when the last one has its own scale */
    int passes; /* the preset's, plus the stretch */
    int stretch;
    Tex lut[SLANG_MAX_TEXTURES];
    int luts;
    int historyMax; /* the highest OriginalHistory# read */
    Tex history[16];
    int historyCount;
    int frame;
    uint64_t lastNs;
    uint32_t frameDelta;
    VkDescriptorPool pool;
    VkBuffer vbuf;
    VkDeviceMemory vmem;
    VkSampler samplers[2][4][2]; /* [linear][wrap][mipmap] */
    int picW, picH, vpW, vpH;
    int unknownLogged;
    uint8_t finalPush[256]; /* the last pass's constants, drawn in shaderChainFinal */
    int finalCtx;
} Chain;

static Chain chains[2];
/* The chain drawing (shaderChainUse) or being set (the menu's functions). */
static Chain* cur = &chains[0];
static Chain* edit = &chains[0];
#define sc (*cur)
/* present_movie_shader: "same" (or unset) the picture's chain, "none" no preset */
static int movieMode; /* 0 same, 1 none, 2 a preset of their own */

static void scError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sc.error, sizeof(sc.error), fmt, ap);
    va_end(ap);
    fprintf(stderr, "shader: %s\n", sc.error);
}

/* ---- compile ---- */

#ifdef XG_HAVE_GLSLANG
static int glslangReady;

static uint32_t* compile(const char* source, int fragment, const char* path, size_t* words) {
    if (!glslangReady) {
        glslang_initialize_process();
        glslangReady = 1;
    }
    glslang_input_t in;
    memset(&in, 0, sizeof(in));
    in.language = GLSLANG_SOURCE_GLSL;
    in.stage = fragment ? GLSLANG_STAGE_FRAGMENT : GLSLANG_STAGE_VERTEX;
    in.client = GLSLANG_CLIENT_VULKAN;
    in.client_version = GLSLANG_TARGET_VULKAN_1_1;
    in.target_language = GLSLANG_TARGET_SPV;
    in.target_language_version = GLSLANG_TARGET_SPV_1_3;
    in.code = source;
    in.default_version = 450;
    in.default_profile = GLSLANG_NO_PROFILE;
    in.messages = GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT;
    in.resource = glslang_default_resource();
    glslang_shader_t* shader = glslang_shader_create(&in);
    uint32_t* out = NULL;
    if (!glslang_shader_preprocess(shader, &in) || !glslang_shader_parse(shader, &in)) {
        scError("%s (%s): %s", path, fragment ? "fragment" : "vertex", glslang_shader_get_info_log(shader));
        glslang_shader_delete(shader);
        return NULL;
    }
    glslang_program_t* program = glslang_program_create();
    glslang_program_add_shader(program, shader);
    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        scError("%s: %s", path, glslang_program_get_info_log(program));
    } else {
        glslang_program_SPIRV_generate(program, in.stage);
        *words = glslang_program_SPIRV_get_size(program);
        out = malloc(*words * sizeof(uint32_t));
        glslang_program_SPIRV_get(program, out);
    }
    glslang_program_delete(program);
    glslang_shader_delete(shader);
    return out;
}
#else
static uint32_t* compile(const char* source, int fragment, const char* path, size_t* words) {
    (void)source;
    (void)fragment;
    (void)words;
    scError("%s: this build has no shader compiler (glslang)", path);
    return NULL;
}
#endif

int shaderChainAvailable(void) {
#ifdef XG_HAVE_GLSLANG
    return 1;
#else
    return 0;
#endif
}

/* ---- Vulkan helpers ---- */

static uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < sc.memProps.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (sc.memProps.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    return UINT32_MAX;
}

static void barrier(VkCommandBuffer cmd) {
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &di);
}

static void toGeneral(VkCommandBuffer cmd, VkImage image, int levels) {
    VkImageMemoryBarrier2 ib = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    ib.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    ib.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    ib.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = image;
    ib.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, (uint32_t)levels, 0, 1};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &ib;
    vkCmdPipelineBarrier2(cmd, &di);
}

static int levelsFor(int w, int h) {
    int n = 1;
    while ((w > 1 || h > 1) && n < 16) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        n++;
    }
    return n;
}

static void texFree(Tex* t) {
    VkDevice d = sc.dev.device;
    if (t->attach != VK_NULL_HANDLE && t->attach != t->view) {
        vkDestroyImageView(d, t->attach, NULL);
    }
    if (t->view != VK_NULL_HANDLE) {
        vkDestroyImageView(d, t->view, NULL);
    }
    if (t->image != VK_NULL_HANDLE) {
        vkDestroyImage(d, t->image, NULL);
    }
    if (t->memory != VK_NULL_HANDLE) {
        vkFreeMemory(d, t->memory, NULL);
    }
    memset(t, 0, sizeof(*t));
}

/* An image in GENERAL layout (recorded into cmd), cleared to black. */
static int texMake(Tex* t, VkCommandBuffer cmd, int w, int h, VkFormat format, int mipmaps) {
    texFree(t);
    VkDevice d = sc.dev.device;
    w = w < 1 ? 1 : w > 16384 ? 16384 : w;
    h = h < 1 ? 1 : h > 16384 ? 16384 : h;
    t->w = w;
    t->h = h;
    t->format = format;
    t->levels = mipmaps ? levelsFor(w, h) : 1;
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = (VkExtent3D){(uint32_t)w, (uint32_t)h, 1};
    ii.mipLevels = (uint32_t)t->levels;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(d, &ii, NULL, &t->image) != VK_SUCCESS) {
        t->image = VK_NULL_HANDLE;
        return 0;
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(d, t->image, &req);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(d, &ai, NULL, &t->memory) != VK_SUCCESS) {
        t->memory = VK_NULL_HANDLE;
        texFree(t);
        return 0;
    }
    vkBindImageMemory(d, t->image, t->memory, 0);
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, (uint32_t)t->levels, 0, 1};
    vkCreateImageView(d, &vi, NULL, &t->view);
    if (t->levels > 1) {
        vi.subresourceRange.levelCount = 1;
        vkCreateImageView(d, &vi, NULL, &t->attach);
    } else {
        t->attach = t->view;
    }
    toGeneral(cmd, t->image, t->levels);
    VkClearColorValue black = {{0}};
    VkImageSubresourceRange all = {VK_IMAGE_ASPECT_COLOR_BIT, 0, (uint32_t)t->levels, 0, 1};
    vkCmdClearColorImage(cmd, t->image, VK_IMAGE_LAYOUT_GENERAL, &black, 1, &all);
    barrier(cmd);
    return 1;
}

/* Level 0 down the mipmap chain (linear blits). */
static void texMipmaps(VkCommandBuffer cmd, Tex* t) {
    int w = t->w, h = t->h;
    for (int l = 1; l < t->levels; l++) {
        int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        VkImageBlit b = {0};
        b.srcSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)l - 1, 0, 1};
        b.srcOffsets[1] = (VkOffset3D){w, h, 1};
        b.dstSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)l, 0, 1};
        b.dstOffsets[1] = (VkOffset3D){nw, nh, 1};
        vkCmdBlitImage(cmd, t->image, VK_IMAGE_LAYOUT_GENERAL, t->image, VK_IMAGE_LAYOUT_GENERAL, 1, &b,
                       VK_FILTER_LINEAR);
        barrier(cmd);
        w = nw;
        h = nh;
    }
}

static VkSampler sampler(int linear, int wrap, int mipmap) {
    VkSampler* s = &sc.samplers[linear != 0][wrap & 3][mipmap != 0];
    if (*s == VK_NULL_HANDLE) {
        static const VkSamplerAddressMode modes[4] = {
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT};
        VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        si.mipmapMode = linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = modes[wrap & 3];
        si.maxLod = mipmap ? VK_LOD_CLAMP_NONE : 0.0f;
        si.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        vkCreateSampler(sc.dev.device, &si, NULL, s);
    }
    return *s;
}

/* ---- textures from files (the preset's LUTs) ---- */

static int loadLut(VkCommandBuffer cmd, Tex* t, const SlangTexture* src, VkBuffer* staging, VkDeviceMemory* stagingMem) {
    SDL_Surface* raw = NULL;
    size_t n = strlen(src->path);
    size_t size = 0;
    uint8_t* file = SDL_LoadFile(src->path, &size);
    uint8_t* jpegPixels = NULL;
    if (file != NULL && jpegIs(file, size)) {
        /* our own decoder (jpeg.c) */
        char err[256];
        int jw = 0, jh = 0;
        jpegPixels = jpegDecode(file, size, &jw, &jh, 16384, 1u << 26, err, sizeof(err));
        SDL_free(file);
        if (jpegPixels == NULL) {
            scError("texture %s (%s): %s", src->name, src->path, err);
            return 0;
        }
        raw = SDL_CreateSurfaceFrom(jw, jh, SDL_PIXELFORMAT_RGBA32, jpegPixels, jw * 4);
    } else if (file != NULL) {
        SDL_IOStream* io = SDL_IOFromConstMem(file, size);
        raw = n > 4 && SDL_strcasecmp(src->path + n - 4, ".bmp") == 0 ? SDL_LoadBMP_IO(io, true) : SDL_LoadPNG_IO(io, true);
        SDL_free(file);
    }
    if (raw == NULL) {
        free(jpegPixels);
        scError("texture %s (%s): %s", src->name, src->path, SDL_GetError());
        return 0;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_ABGR8888); /* bytes R, G, B, A */
    SDL_DestroySurface(raw);
    free(jpegPixels);
    if (rgba == NULL) {
        scError("texture %s: %s", src->name, SDL_GetError());
        return 0;
    }
    int w = rgba->w, h = rgba->h;
    if (!texMake(t, cmd, w, h, VK_FORMAT_R8G8B8A8_UNORM, src->mipmap)) {
        SDL_DestroySurface(rgba);
        scError("texture %s: no device memory for %dx%d", src->name, w, h);
        return 0;
    }
    VkDevice d = sc.dev.device;
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = (VkDeviceSize)w * h * 4;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    vkCreateBuffer(d, &bi, NULL, staging);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(d, *staging, &req);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(d, &ai, NULL, stagingMem);
    vkBindBufferMemory(d, *staging, *stagingMem, 0);
    uint8_t* map;
    vkMapMemory(d, *stagingMem, 0, VK_WHOLE_SIZE, 0, (void**)&map);
    for (int y = 0; y < h; y++) {
        memcpy(map + (size_t)y * w * 4, (const uint8_t*)rgba->pixels + (size_t)y * rgba->pitch, (size_t)w * 4);
    }
    SDL_DestroySurface(rgba);
    VkBufferImageCopy c = {0};
    c.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = (VkExtent3D){(uint32_t)w, (uint32_t)h, 1};
    vkCmdCopyBufferToImage(cmd, *staging, t->image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    barrier(cmd);
    if (t->levels > 1) {
        texMipmaps(cmd, t);
    }
    return 1;
}

/* ---- names ---- */

static int numberAfter(const char* name, const char* prefix, int* n) {
    size_t k = strlen(prefix);
    if (strncmp(name, prefix, k) != 0 || name[k] < '0' || name[k] > '9') {
        return 0;
    }
    char* end;
    long v = strtol(name + k, &end, 10);
    if (*end != '\0') {
        return 0;
    }
    *n = (int)v;
    return 1;
}

/* A texture name as pass `self` sees it: *kind and *index; 0 if unknown. */
static int textureName(const char* name, int self, int* kind, int* index) {
    int n;
    *index = 0;
    if (strcmp(name, "Original") == 0) {
        *kind = TEX_ORIGINAL;
        return 1;
    }
    if (strcmp(name, "Source") == 0) {
        *kind = TEX_SOURCE;
        return 1;
    }
    if (numberAfter(name, "OriginalHistory", &n)) {
        *kind = n == 0 ? TEX_ORIGINAL : TEX_HISTORY;
        *index = n;
        return n < 16;
    }
    if (numberAfter(name, "PassOutput", &n)) {
        *kind = TEX_OUTPUT;
        *index = n;
        return n < self;
    }
    if (numberAfter(name, "PassFeedback", &n)) {
        *kind = TEX_FEEDBACK;
        *index = n;
        return n < sc.preset.passes;
    }
    for (int i = 0; i < sc.preset.passes; i++) {
        const char* alias = sc.preset.pass[i].alias;
        size_t k = strlen(alias);
        if (k == 0) {
            continue;
        }
        if (strcmp(name, alias) == 0) {
            *kind = TEX_OUTPUT;
            *index = i;
            return i < self;
        }
        if (strncmp(name, alias, k) == 0 && strcmp(name + k, "Feedback") == 0) {
            *kind = TEX_FEEDBACK;
            *index = i;
            return 1;
        }
    }
    for (int i = 0; i < sc.preset.textures; i++) {
        if (strcmp(name, sc.preset.texture[i].name) == 0) {
            *kind = TEX_LUT;
            *index = i;
            return 1;
        }
    }
    return 0;
}

static void memberSemantic(Member* m, const char* name, int self) {
    static const struct { const char* name; int sem; } fixed[] = {
        {"MVP", SEM_MVP},
        {"OutputSize", SEM_OUTPUT_SIZE},
        {"FinalViewportSize", SEM_FINAL_VIEWPORT},
        {"FrameCount", SEM_FRAME_COUNT},
        {"FrameDirection", SEM_FRAME_DIRECTION},
        {"FrameTimeDelta", SEM_FRAME_TIME_DELTA},
        {"Rotation", SEM_ROTATION},
        {"OriginalAspect", SEM_ORIGINAL_ASPECT},
        {"OriginalAspectRotated", SEM_ORIGINAL_ASPECT},
        {"OriginalFPS", SEM_ORIGINAL_FPS},
        {"CurrentSubFrame", SEM_SUBFRAME},
        {"TotalSubFrames", SEM_TOTAL_SUBFRAMES},
    };
    m->sem = SEM_UNKNOWN;
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
        if (strcmp(name, fixed[i].name) == 0) {
            m->sem = fixed[i].sem;
            return;
        }
    }
    for (int i = 0; i < sc.preset.params; i++) {
        if (strcmp(name, sc.preset.param[i].name) == 0) {
            m->sem = SEM_PARAM;
            m->index = i;
            return;
        }
    }
    size_t n = strlen(name);
    if (n > 4 && strcmp(name + n - 4, "Size") == 0) {
        char base[64];
        snprintf(base, sizeof(base), "%.*s", (int)(n - 4), name);
        if (textureName(base, self, &m->texKind, &m->index) || (strcmp(base, "Original") == 0)) {
            m->sem = SEM_TEX_SIZE;
            return;
        }
        /* a size of a later pass's output is known too (sizes are set before drawing) */
        int k;
        if (numberAfter(base, "PassOutput", &k) && k < sc.preset.passes) {
            m->sem = SEM_TEX_SIZE;
            m->texKind = TEX_OUTPUT;
            m->index = k;
            return;
        }
    }
    if (!sc.unknownLogged) {
        sc.unknownLogged = 1;
        fprintf(stderr, "shader: uniform %s is not known: it reads 0\n", name);
    }
}

/* ---- the chain's passes ---- */

static VkFormat passFormat(const SlangPass* p) {
    static const struct { const char* name; VkFormat format; } names[] = {
        {"R8_UNORM", VK_FORMAT_R8_UNORM},
        {"R8_UINT", VK_FORMAT_R8_UINT},
        {"R8_SINT", VK_FORMAT_R8_SINT},
        {"R8G8_UNORM", VK_FORMAT_R8G8_UNORM},
        {"R8G8_UINT", VK_FORMAT_R8G8_UINT},
        {"R8G8_SINT", VK_FORMAT_R8G8_SINT},
        {"R8G8B8A8_UNORM", VK_FORMAT_R8G8B8A8_UNORM},
        {"R8G8B8A8_UINT", VK_FORMAT_R8G8B8A8_UINT},
        {"R8G8B8A8_SINT", VK_FORMAT_R8G8B8A8_SINT},
        {"R8G8B8A8_SRGB", VK_FORMAT_R8G8B8A8_SRGB},
        {"A2B10G10R10_UNORM_PACK32", VK_FORMAT_A2B10G10R10_UNORM_PACK32},
        {"A2B10G10R10_UINT_PACK32", VK_FORMAT_A2B10G10R10_UINT_PACK32},
        {"R16_UINT", VK_FORMAT_R16_UINT},
        {"R16_SINT", VK_FORMAT_R16_SINT},
        {"R16_SFLOAT", VK_FORMAT_R16_SFLOAT},
        {"R16G16_UINT", VK_FORMAT_R16G16_UINT},
        {"R16G16_SINT", VK_FORMAT_R16G16_SINT},
        {"R16G16_SFLOAT", VK_FORMAT_R16G16_SFLOAT},
        {"R16G16B16A16_UINT", VK_FORMAT_R16G16B16A16_UINT},
        {"R16G16B16A16_SINT", VK_FORMAT_R16G16B16A16_SINT},
        {"R16G16B16A16_SFLOAT", VK_FORMAT_R16G16B16A16_SFLOAT},
        {"R32_UINT", VK_FORMAT_R32_UINT},
        {"R32_SINT", VK_FORMAT_R32_SINT},
        {"R32_SFLOAT", VK_FORMAT_R32_SFLOAT},
        {"R32G32_UINT", VK_FORMAT_R32G32_UINT},
        {"R32G32_SINT", VK_FORMAT_R32G32_SINT},
        {"R32G32_SFLOAT", VK_FORMAT_R32G32_SFLOAT},
        {"R32G32B32A32_UINT", VK_FORMAT_R32G32B32A32_UINT},
        {"R32G32B32A32_SINT", VK_FORMAT_R32G32B32A32_SINT},
        {"R32G32B32A32_SFLOAT", VK_FORMAT_R32G32B32A32_SFLOAT},
    };
    if (p->format[0] != '\0') {
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            if (strcmp(p->format, names[i].name) == 0) {
                return names[i].format;
            }
        }
        fprintf(stderr, "shader: %s: #pragma format %s is not known: R8G8B8A8_UNORM\n", p->path, p->format);
    }
    if (p->floatFb) {
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    return p->srgbFb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
}

static VkPipeline makePipeline(Pass* p, VkFormat format) {
    VkPipelineShaderStageCreateInfo stages[2] = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                 {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = p->vs;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = p->fs;
    stages[1].pName = "main";
    /* RetroArch's vertex layout: vec4 Position (location 0), vec2 TexCoord (1). */
    VkVertexInputBindingDescription binding = {0, 6 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2] = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                                                  {1, 0, VK_FORMAT_R32G32_SFLOAT, 4 * sizeof(float)}};
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState att = {0};
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                         VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo gi = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gi.pNext = &ri;
    gi.stageCount = 2;
    gi.pStages = stages;
    gi.pVertexInputState = &vi;
    gi.pInputAssemblyState = &ia;
    gi.pViewportState = &vp;
    gi.pRasterizationState = &rs;
    gi.pMultisampleState = &ms;
    gi.pColorBlendState = &cb;
    gi.pDynamicState = &ds;
    gi.layout = p->layout;
    VkPipeline pipe = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(sc.dev.device, VK_NULL_HANDLE, 1, &gi, NULL, &pipe) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return pipe;
}

static void passFree(Pass* p) {
    VkDevice d = sc.dev.device;
    if (p->pipeline) vkDestroyPipeline(d, p->pipeline, NULL);
    for (int i = 0; i < p->finalCount; i++) vkDestroyPipeline(d, p->finals[i].pipeline, NULL);
    if (p->layout) vkDestroyPipelineLayout(d, p->layout, NULL);
    if (p->setLayout) vkDestroyDescriptorSetLayout(d, p->setLayout, NULL);
    if (p->vs) vkDestroyShaderModule(d, p->vs, NULL);
    if (p->fs) vkDestroyShaderModule(d, p->fs, NULL);
    for (int f = 0; f < FRAMES; f++) {
        if (p->ubuf[f]) vkDestroyBuffer(d, p->ubuf[f], NULL);
        if (p->umem[f]) vkFreeMemory(d, p->umem[f], NULL);
    }
    texFree(&p->out[0]);
    texFree(&p->out[1]);
    memset(p, 0, sizeof(*p));
}

static void members(Pass* p, Member* out, int* count, const SpvBlock* a, const SpvBlock* b, int self) {
    *count = 0;
    const SpvBlock* blocks[2] = {a, b};
    for (int k = 0; k < 2; k++) {
        if (!blocks[k]->present) {
            continue;
        }
        for (int i = 0; i < blocks[k]->members; i++) {
            const SpvMember* s = &blocks[k]->member[i];
            int dup = 0;
            for (int j = 0; j < *count; j++) {
                dup |= out[j].offset == s->offset;
            }
            if (dup || *count == SPV_MAX_MEMBERS) {
                continue;
            }
            Member* m = &out[(*count)++];
            memset(m, 0, sizeof(*m));
            m->offset = s->offset;
            m->size = s->size;
            m->kind = s->kind;
            memberSemantic(m, s->name, self);
        }
    }
    (void)p;
}

/* Compile, reflect and make the pipeline objects of pass i. */
static int passMake(int i, const char* vsrc, const char* fsrc, const char* path) {
    Pass* p = &sc.pass[i];
    size_t vn = 0, fn = 0;
    uint32_t* vw = compile(vsrc, 0, path, &vn);
    uint32_t* fw = vw != NULL ? compile(fsrc, 1, path, &fn) : NULL;
    if (fw == NULL) {
        free(vw);
        return 0;
    }
    SpvReflection rv, rf;
    char err[256];
    if (!spvReflect(vw, vn, &rv, err, sizeof(err)) || !spvReflect(fw, fn, &rf, err, sizeof(err))) {
        scError("%s: %s", path, err);
        free(vw);
        free(fw);
        return 0;
    }
    VkDevice d = sc.dev.device;
    VkShaderModuleCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    si.codeSize = vn * 4;
    si.pCode = vw;
    vkCreateShaderModule(d, &si, NULL, &p->vs);
    si.codeSize = fn * 4;
    si.pCode = fw;
    vkCreateShaderModule(d, &si, NULL, &p->fs);
    free(vw);
    free(fw);
    /* the uniform block and push constants, both stages' members together */
    if (rv.ubo.present || rf.ubo.present) {
        p->hasUbo = 1;
        p->uboBinding = rv.ubo.present ? rv.ubo.binding : rf.ubo.binding;
        p->uboSize = rv.ubo.size > rf.ubo.size ? rv.ubo.size : rf.ubo.size;
        p->uboStages = (rv.ubo.present ? VK_SHADER_STAGE_VERTEX_BIT : 0) | (rf.ubo.present ? VK_SHADER_STAGE_FRAGMENT_BIT : 0);
        members(p, p->member, &p->members, &rv.ubo, &rf.ubo, i);
    }
    if (rv.push.present || rf.push.present) {
        p->pushSize = rv.push.size > rf.push.size ? rv.push.size : rf.push.size;
        p->pushStages = (rv.push.present ? VK_SHADER_STAGE_VERTEX_BIT : 0) | (rf.push.present ? VK_SHADER_STAGE_FRAGMENT_BIT : 0);
        members(p, p->pushMember, &p->pushMembers, &rv.push, &rf.push, i);
    }
    /* the samplers */
    const SpvReflection* stages[2] = {&rv, &rf};
    for (int k = 0; k < 2; k++) {
        for (int s = 0; s < stages[k]->samplers; s++) {
            const SpvSampler* sp = &stages[k]->sampler[s];
            Binding* b = NULL;
            for (int j = 0; j < p->bindings; j++) {
                if (p->binding[j].binding == sp->binding) {
                    b = &p->binding[j];
                }
            }
            if (b == NULL) {
                b = &p->binding[p->bindings++];
                memset(b, 0, sizeof(*b));
                snprintf(b->name, sizeof(b->name), "%s", sp->name);
                b->binding = sp->binding;
                if (!textureName(sp->name, i, &b->kind, &b->index)) {
                    scError("%s: texture %s is not known (or is a later pass's)", path, sp->name);
                    return 0;
                }
                if (b->kind == TEX_HISTORY && b->index > sc.historyMax) {
                    sc.historyMax = b->index;
                }
                if (b->kind == TEX_FEEDBACK) {
                    sc.pass[b->index].feedback = 1;
                }
            }
            b->stages |= k == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        }
    }
    VkDescriptorSetLayoutBinding lb[SPV_MAX_SAMPLERS + 1];
    int nb = 0;
    if (p->hasUbo) {
        lb[nb++] = (VkDescriptorSetLayoutBinding){p->uboBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, p->uboStages, NULL};
    }
    for (int j = 0; j < p->bindings; j++) {
        lb[nb++] = (VkDescriptorSetLayoutBinding){p->binding[j].binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                                  p->binding[j].stages, NULL};
    }
    VkDescriptorSetLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = (uint32_t)nb;
    li.pBindings = lb;
    vkCreateDescriptorSetLayout(d, &li, NULL, &p->setLayout);
    VkPushConstantRange pr = {p->pushStages, 0, p->pushSize};
    VkPipelineLayoutCreateInfo pli = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &p->setLayout;
    pli.pushConstantRangeCount = p->pushSize > 0 ? 1 : 0;
    pli.pPushConstantRanges = &pr;
    vkCreatePipelineLayout(d, &pli, NULL, &p->layout);
    for (int f = 0; f < FRAMES && p->hasUbo; f++) {
        VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = p->uboSize > 0 ? p->uboSize : 16;
        bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        vkCreateBuffer(d, &bi, NULL, &p->ubuf[f]);
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(d, p->ubuf[f], &req);
        VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex =
            memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(d, &ai, NULL, &p->umem[f]);
        vkBindBufferMemory(d, p->ubuf[f], p->umem[f], 0);
        vkMapMemory(d, p->umem[f], 0, VK_WHOLE_SIZE, 0, (void**)&p->umap[f]);
    }
    return 1;
}

/* The pass that stretches a last pass with its own scale to the window. */
static const char stretchSource[] =
    "#version 450\n"
    "layout(push_constant) uniform Push { mat4 MVP; } params;\n"
    "layout(set = 0, binding = 0) uniform sampler2D Source;\n";

static void waitDevice(void) {
    if(sc.dev.waitIdle)sc.dev.waitIdle(sc.dev.user);
    else vkDeviceWaitIdle(sc.dev.device);
}

static void unload(void) {
    if (sc.dev.device == VK_NULL_HANDLE) {
        return;
    }
    waitDevice();
    for (int i = 0; i < SLANG_MAX_PASSES + 1; i++) {
        passFree(&sc.pass[i]);
    }
    for (int i = 0; i < SLANG_MAX_TEXTURES; i++) {
        texFree(&sc.lut[i]);
    }
    for (int i = 0; i < 16; i++) {
        texFree(&sc.history[i]);
    }
    if (sc.pool) {
        vkDestroyDescriptorPool(sc.dev.device, sc.pool, NULL);
        sc.pool = VK_NULL_HANDLE;
    }
    slangFree(&sc.preset);
    sc.loaded = 0;
    sc.passes = sc.luts = sc.historyMax = sc.historyCount = 0;
    sc.stretch = 0;
    sc.picW = sc.picH = sc.vpW = sc.vpH = 0;
    sc.unknownLogged = 0;
}

/* Load sc.path (compile, make pipelines and LUTs) with a one-shot command
 * buffer of the caller's (the LUT uploads); 1 when it is in use. */
static int load(VkCommandBuffer cmd) {
    unload();
    sc.error[0] = '\0';
    if (sc.path[0] == '\0') {
        return 0;
    }
    if (!slangLoad(&sc.preset, sc.path)) {
        scError("%s", sc.preset.error);
        return 0;
    }
    slangApplyParams(&sc.preset, settingsGet(sc.paramsKey));
    int n = sc.preset.passes;
    for (int i = 0; i < n; i++) {
        if (!passMake(i, sc.preset.pass[i].vertex, sc.preset.pass[i].fragment, sc.preset.pass[i].path)) {
            unload();
            return 0;
        }
        sc.pass[i].format = passFormat(&sc.preset.pass[i]);
    }
    for (int i = 0; i < n; i++) {
        /* the next pass's mipmap_input wants this pass's output mipmapped */
        if (i + 1 < n && sc.preset.pass[i + 1].mipmap) {
            sc.pass[i].mipmapped = 1;
        }
    }
    const SlangPass* last = &sc.preset.pass[n - 1];
    sc.passes = n;
    if (last->scaleSet || sc.pass[n - 1].feedback) {
        /* drawn into its own image, then stretched to the window */
        char vsrc[1024], fsrc[1024];
        snprintf(vsrc, sizeof(vsrc),
                 "%slayout(location = 0) in vec4 Position;\nlayout(location = 1) in vec2 TexCoord;\n"
                 "layout(location = 0) out vec2 uv;\nvoid main() { gl_Position = params.MVP * Position; uv = TexCoord; }\n",
                 stretchSource);
        snprintf(fsrc, sizeof(fsrc),
                 "%slayout(location = 0) in vec2 uv;\nlayout(location = 0) out vec4 color;\n"
                 "void main() { color = texture(Source, uv); }\n",
                 stretchSource);
        if (!passMake(n, vsrc, fsrc, "(stretch)")) {
            unload();
            return 0;
        }
        sc.stretch = 1;
        sc.passes = n + 1;
    }
    sc.pass[sc.passes - 1].direct = 1;
    sc.historyCount = sc.historyMax > 0 ? sc.historyMax + 1 : 1;
    /* descriptor sets: each pass, each frame context */
    uint32_t samplers = 0;
    for (int i = 0; i < sc.passes; i++) {
        samplers += (uint32_t)sc.pass[i].bindings;
    }
    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (samplers ? samplers : 1) * FRAMES},
                                  {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)sc.passes * FRAMES}};
    VkDescriptorPoolCreateInfo dpi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = (uint32_t)sc.passes * FRAMES;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = ps;
    vkCreateDescriptorPool(sc.dev.device, &dpi, NULL, &sc.pool);
    for (int i = 0; i < sc.passes; i++) {
        for (int f = 0; f < FRAMES; f++) {
            VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = sc.pool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &sc.pass[i].setLayout;
            vkAllocateDescriptorSets(sc.dev.device, &ai, &sc.pass[i].sets[f]);
        }
        if (!sc.pass[i].direct) {
            sc.pass[i].pipeline = makePipeline(&sc.pass[i], sc.pass[i].format);
            if (sc.pass[i].pipeline == VK_NULL_HANDLE) {
                scError("%s: the device would not make its pipeline", sc.preset.pass[i < n ? i : n - 1].path);
                unload();
                return 0;
            }
        }
    }
    /* the LUTs (staging buffers live until the device is idle again) */
    VkBuffer staging[SLANG_MAX_TEXTURES] = {0};
    VkDeviceMemory stagingMem[SLANG_MAX_TEXTURES] = {0};
    int ok = 1;
    for (int i = 0; i < sc.preset.textures && ok; i++) {
        ok = loadLut(cmd, &sc.lut[i], &sc.preset.texture[i], &staging[i], &stagingMem[i]);
        sc.luts = i + 1;
    }
    sc.dev.submitAndWait(sc.dev.user);
    for (int i = 0; i < SLANG_MAX_TEXTURES; i++) {
        if (staging[i]) vkDestroyBuffer(sc.dev.device, staging[i], NULL);
        if (stagingMem[i]) vkFreeMemory(sc.dev.device, stagingMem[i], NULL);
    }
    if (!ok) {
        unload();
        return 0;
    }
    sc.loaded = 1;
    sc.frame = 0;
    fprintf(stderr, "shader: %s: %d pass%s, %d texture%s, %d parameter%s\n", sc.path, n, n == 1 ? "" : "es",
            sc.preset.textures, sc.preset.textures == 1 ? "" : "s", sc.preset.params,
            sc.preset.params == 1 ? "" : "s");
    return 1;
}

/* ---- the device side (shaderchain_vk.h) ---- */

/* present_movie_shader's text: the mode, and the preset's path for chain 1. */
static void readMovieSetting(const char* v) {
    chains[1].path[0] = '\0';
    if (v == NULL || v[0] == '\0' || SDL_strcasecmp(v, "same") == 0) {
        movieMode = 0;
    } else if (SDL_strcasecmp(v, "none") == 0) {
        movieMode = 1;
    } else {
        movieMode = 2;
        snprintf(chains[1].path, sizeof(chains[1].path), "%s", v);
    }
}

void shaderChainVulkanReady(const ShaderChainDevice* dev) {
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dev->getInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)gipa(dev->instance, "vkGetDeviceProcAddr");
#define SC_LOAD_INSTANCE(name) name = (PFN_##name)gipa(dev->instance, #name);
    SC_INSTANCE_FUNCS(SC_LOAD_INSTANCE)
#undef SC_LOAD_INSTANCE
#define SC_LOAD(name)                                                    \
    name = (PFN_##name)gdpa(dev->device, #name);                         \
    if (name == NULL) name = (PFN_##name)gipa(dev->instance, #name);
    SC_FUNCS(SC_LOAD)
    settingsDescribe("present_shader", "present_shader: a RetroArch .slangp preset or .slang shader drawn over the "
                                       "picture (empty: none)");
    settingsDescribe("present_shader_params", "present_shader_params: the preset's parameters you changed, "
                                              "name=value;...");
    settingsDescribe("present_movie_shader", "present_movie_shader: for the movies (24-bit pictures): same (as "
                                             "present_shader), none, or a preset of their own");
    settingsDescribe("present_movie_shader_params", "present_movie_shader_params: its parameters you changed");
    static const char* const keys[2][2] = {{"present_shader", "present_shader_params"},
                                           {"present_movie_shader", "present_movie_shader_params"}};
    for (int i = 0; i < 2; i++) {
        cur = &chains[i];
        memset(&sc, 0, sizeof(sc));
        sc.key = keys[i][0];
        sc.paramsKey = keys[i][1];
        sc.dev = *dev;
        vkGetPhysicalDeviceMemoryProperties(dev->physical, &sc.memProps);
        static const float quad[4][6] = {{0, 0, 0, 1, 0, 0}, {1, 0, 0, 1, 1, 0}, {0, 1, 0, 1, 0, 1}, {1, 1, 0, 1, 1, 1}};
        VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = sizeof(quad);
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vkCreateBuffer(dev->device, &bi, NULL, &sc.vbuf);
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(dev->device, sc.vbuf, &req);
        VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex =
            memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(dev->device, &ai, NULL, &sc.vmem);
        vkBindBufferMemory(dev->device, sc.vbuf, sc.vmem, 0);
        void* map;
        vkMapMemory(dev->device, sc.vmem, 0, VK_WHOLE_SIZE, 0, &map);
        memcpy(map, quad, sizeof(quad));
        sc.ready = 1;
        const char* path = settingsGet(sc.key);
        if (i == 0) {
            snprintf(sc.path, sizeof(sc.path), "%s", path != NULL ? path : "");
        } else {
            readMovieSetting(path);
        }
        if (sc.path[0] != '\0') {
            load(dev->beginOneShot(dev->user));
        }
    }
    cur = edit = &chains[0];
}

void shaderChainVulkanShutdown(void) {
    for (int i = 0; i < 2; i++) {
        cur = &chains[i];
        if (!sc.ready) {
            continue;
        }
        unload();
        VkDevice d = sc.dev.device;
        for (int a = 0; a < 2; a++)
            for (int b = 0; b < 4; b++)
                for (int c = 0; c < 2; c++)
                    if (sc.samplers[a][b][c]) vkDestroySampler(d, sc.samplers[a][b][c], NULL);
        if (sc.vbuf) vkDestroyBuffer(d, sc.vbuf, NULL);
        if (sc.vmem) vkFreeMemory(d, sc.vmem, NULL);
        memset(&sc, 0, sizeof(sc));
    }
    cur = edit = &chains[0];
#ifdef XG_HAVE_GLSLANG
    if (glslangReady) {
        glslang_finalize_process();
        glslangReady = 0;
    }
#endif
}

int shaderChainUse(int movie) {
    cur = &chains[0];
    if (movie && movieMode == 1) {
        return 0;
    }
    if (movie && movieMode == 2) {
        cur = &chains[1];
    }
    return shaderChainActive();
}

int shaderChainActive(void) {
    return sc.ready && sc.loaded;
}

static void outputSize(int i, int srcW, int srcH, int* w, int* h) {
    const SlangPass* sp = i < sc.preset.passes ? &sc.preset.pass[i] : NULL;
    if (sc.pass[i].direct || sp == NULL) {
        *w = sc.vpW;
        *h = sc.vpH;
        return;
    }
    if (!sp->scaleSet) {
        *w = srcW;
        *h = srcH;
        return;
    }
    int ref[2][2] = {{srcW, srcH}, {sc.vpW, sc.vpH}};
    int dims[2];
    for (int k = 0; k < 2; k++) {
        float s = sp->scale[k];
        switch (sp->scaleType[k]) {
        case SLANG_SCALE_VIEWPORT:
            dims[k] = (int)lroundf((float)ref[1][k] * s);
            break;
        case SLANG_SCALE_ABSOLUTE:
            dims[k] = (int)lroundf(s);
            break;
        case SLANG_SCALE_ORIGINAL:
            dims[k] = (int)lroundf((float)(k ? sc.picH : sc.picW) * s);
            break;
        default:
            dims[k] = (int)lroundf((float)ref[0][k] * s);
            break;
        }
    }
    *w = dims[0] < 1 ? 1 : dims[0];
    *h = dims[1] < 1 ? 1 : dims[1];
}

VkImageView shaderChainBegin(VkCommandBuffer cmd, int picW, int picH, int vpW, int vpH) {
    if (!shaderChainActive()) {
        return VK_NULL_HANDLE;
    }
    uint64_t now = SDL_GetTicksNS();
    sc.frameDelta = sc.lastNs ? (uint32_t)((now - sc.lastNs) / 1000) : 0;
    sc.lastNs = now;
    int resize = picW != sc.picW || picH != sc.picH || vpW != sc.vpW || vpH != sc.vpH;
    if (resize) {
        waitDevice(); /* nothing submitted may still use what is replaced */
        sc.picW = picW;
        sc.picH = picH;
        sc.vpW = vpW;
        sc.vpH = vpH;
        for (int i = 0; i < sc.historyCount; i++) {
            texMake(&sc.history[i], cmd, picW, picH, VK_FORMAT_R8G8B8A8_UNORM, sc.preset.pass[0].mipmap);
        }
        int w = picW, h = picH;
        for (int i = 0; i < sc.passes; i++) {
            Pass* p = &sc.pass[i];
            outputSize(i, w, h, &p->w, &p->h);
            if (!p->direct) {
                texMake(&p->out[0], cmd, p->w, p->h, p->format, p->mipmapped);
                if (p->feedback) {
                    texMake(&p->out[1], cmd, p->w, p->h, p->format, p->mipmapped);
                }
            }
            w = p->w;
            h = p->h;
        }
    }
    return sc.history[sc.frame % sc.historyCount].attach;
}

static const Tex* texFor(int self, int kind, int index) {
    switch (kind) {
    case TEX_ORIGINAL:
        return &sc.history[sc.frame % sc.historyCount];
    case TEX_SOURCE:
        return self == 0 ? &sc.history[sc.frame % sc.historyCount] : &sc.pass[self - 1].out[sc.pass[self - 1].cur];
    case TEX_HISTORY:
        return &sc.history[((sc.frame - index) % sc.historyCount + sc.historyCount) % sc.historyCount];
    case TEX_OUTPUT:
        return &sc.pass[index].out[sc.pass[index].cur];
    case TEX_FEEDBACK:
        return &sc.pass[index].out[sc.pass[index].feedback ? sc.pass[index].cur ^ 1 : sc.pass[index].cur];
    default:
        return &sc.lut[index];
    }
}

/* The filter and wrap a texture is read with by pass self. */
static void samplingFor(int self, int kind, int index, int* linear, int* wrap, int* mip) {
    int reader = self; /* whose filter_linear / wrap_mode applies */
    if (kind == TEX_LUT) {
        *linear = sc.preset.texture[index].linear;
        *wrap = sc.preset.texture[index].wrap;
        *mip = sc.preset.texture[index].mipmap;
        return;
    }
    if (kind == TEX_ORIGINAL || kind == TEX_HISTORY) {
        reader = 0;
    } else if (kind == TEX_OUTPUT || kind == TEX_FEEDBACK) {
        reader = index + 1 < sc.preset.passes ? index + 1 : self;
    }
    const SlangPass* sp = reader < sc.preset.passes ? &sc.preset.pass[reader] : &sc.preset.pass[sc.preset.passes - 1];
    if (self >= sc.preset.passes) {
        /* the stretch pass: the last pass's own filter for the window */
        sp = &sc.preset.pass[sc.preset.passes - 1];
    }
    *linear = sp->filter > 0;
    *wrap = sp->wrap;
    *mip = sp->mipmap;
}

static void writeValue(uint8_t* dst, const Member* m, int self, int outW, int outH) {
    float v4[16] = {0};
    uint32_t u = 0;
    int isUint = m->kind == SPV_MEMBER_UINT || m->kind == SPV_MEMBER_INT;
    switch (m->sem) {
    case SEM_MVP: {
        static const float mvp[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
        memcpy(v4, mvp, sizeof(mvp));
        break;
    }
    case SEM_OUTPUT_SIZE:
        v4[0] = (float)outW, v4[1] = (float)outH, v4[2] = 1.0f / outW, v4[3] = 1.0f / outH;
        break;
    case SEM_FINAL_VIEWPORT:
        v4[0] = (float)sc.vpW, v4[1] = (float)sc.vpH, v4[2] = 1.0f / sc.vpW, v4[3] = 1.0f / sc.vpH;
        break;
    case SEM_FRAME_COUNT: {
        int mod = self < sc.preset.passes ? sc.preset.pass[self].frameCountMod : 0;
        u = mod > 0 ? (uint32_t)sc.frame % (uint32_t)mod : (uint32_t)sc.frame;
        v4[0] = (float)u;
        break;
    }
    case SEM_FRAME_DIRECTION:
        u = 1;
        v4[0] = 1;
        break;
    case SEM_FRAME_TIME_DELTA:
        u = sc.frameDelta;
        v4[0] = (float)u;
        break;
    case SEM_ORIGINAL_ASPECT:
        v4[0] = (float)sc.picW / (float)sc.picH;
        break;
    case SEM_ORIGINAL_FPS:
        v4[0] = 59.94f;
        break;
    case SEM_SUBFRAME:
    case SEM_TOTAL_SUBFRAMES:
        u = 1;
        v4[0] = 1;
        break;
    case SEM_TEX_SIZE: {
        int w, h;
        if (m->texKind == TEX_OUTPUT && sc.pass[m->index].direct) {
            w = sc.vpW, h = sc.vpH;
        } else {
            const Tex* t = texFor(self, m->texKind, m->index);
            w = t->w, h = t->h;
        }
        w = w > 0 ? w : 1;
        h = h > 0 ? h : 1;
        v4[0] = (float)w, v4[1] = (float)h, v4[2] = 1.0f / w, v4[3] = 1.0f / h;
        break;
    }
    case SEM_PARAM:
        v4[0] = sc.preset.param[m->index].value;
        break;
    default:
        break;
    }
    if (isUint) {
        memcpy(dst + m->offset, &u, 4);
    } else {
        memcpy(dst + m->offset, v4, m->size <= sizeof(v4) ? m->size : sizeof(v4));
    }
}

/* Pass i's uniforms and descriptors for this frame context. */
static void passPrepare(int i, int ctx, uint8_t* push) {
    Pass* p = &sc.pass[i];
    int outW = p->w, outH = p->h;
    if (p->hasUbo) {
        memset(p->umap[ctx], 0, p->uboSize);
        for (int m = 0; m < p->members; m++) {
            writeValue(p->umap[ctx], &p->member[m], i, outW, outH);
        }
    }
    memset(push, 0, 256);
    for (int m = 0; m < p->pushMembers; m++) {
        if (p->pushMember[m].offset + p->pushMember[m].size <= 256) {
            writeValue(push, &p->pushMember[m], i, outW, outH);
        }
    }
    VkWriteDescriptorSet w[SPV_MAX_SAMPLERS + 1];
    VkDescriptorImageInfo ii[SPV_MAX_SAMPLERS];
    VkDescriptorBufferInfo bi = {p->ubuf[ctx], 0, VK_WHOLE_SIZE};
    int n = 0;
    if (p->hasUbo) {
        w[n] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[n].dstSet = p->sets[ctx];
        w[n].dstBinding = p->uboBinding;
        w[n].descriptorCount = 1;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[n].pBufferInfo = &bi;
        n++;
    }
    for (int b = 0; b < p->bindings; b++) {
        const Binding* bd = &p->binding[b];
        int kind = bd->kind, index = bd->index;
        if (i >= sc.preset.passes) {
            kind = TEX_OUTPUT; /* the stretch pass reads the last pass */
            index = sc.preset.passes - 1;
        }
        int linear, wrap, mip;
        samplingFor(i, kind, index, &linear, &wrap, &mip);
        const Tex* t = texFor(i, kind, index);
        ii[b] = (VkDescriptorImageInfo){sampler(linear, wrap, mip && t->levels > 1), t->view, VK_IMAGE_LAYOUT_GENERAL};
        w[n] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[n].dstSet = p->sets[ctx];
        w[n].dstBinding = bd->binding;
        w[n].descriptorCount = 1;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[n].pImageInfo = &ii[b];
        n++;
    }
    vkUpdateDescriptorSets(sc.dev.device, (uint32_t)n, w, 0, NULL);
}


static void passDraw(VkCommandBuffer cmd, int i, int ctx, const uint8_t* push, VkPipeline pipe, int x, int y, int w,
                     int h, const int* scissor) {
    Pass* p = &sc.pass[i];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p->layout, 0, 1, &p->sets[ctx], 0, NULL);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &sc.vbuf, &offset);
    VkViewport vp = {(float)x, (float)y, (float)w, (float)h, 0.0f, 1.0f};
    VkRect2D sr = {{scissor[0], scissor[1]}, {(uint32_t)scissor[2], (uint32_t)scissor[3]}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sr);
    if (p->pushSize > 0) {
        vkCmdPushConstants(cmd, p->layout, p->pushStages, 0, p->pushSize, push);
    }
    vkCmdDraw(cmd, 4, 1, 0, 0);
}

void shaderChainRun(VkCommandBuffer cmd, int ctx) {
    if (!shaderChainActive()) {
        return;
    }
    barrier(cmd); /* the picture, drawn */
    Tex* original = &sc.history[sc.frame % sc.historyCount];
    if (original->levels > 1) {
        texMipmaps(cmd, original);
    }
    uint8_t push[256];
    for (int i = 0; i < sc.passes; i++) {
        Pass* p = &sc.pass[i];
        if (p->direct) {
            passPrepare(i, ctx, sc.finalPush);
            sc.finalCtx = ctx;
            break;
        }
        passPrepare(i, ctx, push);
        VkRenderingAttachmentInfo att = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        att.imageView = p->out[p->cur].attach;
        att.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea.extent = (VkExtent2D){(uint32_t)p->w, (uint32_t)p->h};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &att;
        vkCmdBeginRendering(cmd, &ri);
        int scissor[4] = {0, 0, p->w, p->h};
        passDraw(cmd, i, ctx, push, p->pipeline, 0, 0, p->w, p->h, scissor);
        vkCmdEndRendering(cmd);
        barrier(cmd);
        if (p->mipmapped) {
            texMipmaps(cmd, &p->out[p->cur]);
        }
    }
}

void shaderChainFinal(VkCommandBuffer cmd, VkFormat format, const int dest[4], const int clip[4]) {
    if (!shaderChainActive()) {
        return;
    }
    int i = sc.passes - 1;
    Pass* p = &sc.pass[i];
    VkPipeline pipe = VK_NULL_HANDLE;
    for (int k = 0; k < p->finalCount; k++) {
        if (p->finals[k].format == format) {
            pipe = p->finals[k].pipeline;
        }
    }
    if (pipe == VK_NULL_HANDLE) {
        if (p->finalCount == MAX_FINALS) {
            return;
        }
        pipe = makePipeline(p, format);
        if (pipe == VK_NULL_HANDLE) {
            return;
        }
        p->finals[p->finalCount].format = format;
        p->finals[p->finalCount++].pipeline = pipe;
    }
    /* the picture's part of the window: the destination inside the clip */
    int x0 = dest[0] > clip[0] ? dest[0] : clip[0], y0 = dest[1] > clip[1] ? dest[1] : clip[1];
    int x1 = dest[0] + dest[2] < clip[0] + clip[2] ? dest[0] + dest[2] : clip[0] + clip[2];
    int y1 = dest[1] + dest[3] < clip[1] + clip[3] ? dest[1] + dest[3] : clip[1] + clip[3];
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    int scissor[4] = {x0, y0, x1 - x0, y1 - y0};
    passDraw(cmd, i, sc.finalCtx, sc.finalPush, pipe, dest[0], dest[1], dest[2], dest[3], scissor);
}

void shaderChainEnd(void) {
    if (!shaderChainActive()) {
        return;
    }
    sc.frame++;
    for (int i = 0; i < sc.passes; i++) {
        if (sc.pass[i].feedback) {
            sc.pass[i].cur ^= 1;
        }
    }
}

/* ---- the settings menu's side: the chain shaderChainEdit() chose ---- */

#undef sc
#define sc (*edit)

void shaderChainEdit(int movie) {
    edit = &chains[movie ? 1 : 0];
}

int shaderChainMovieMode(void) {
    return movieMode;
}

const char* shaderChainPath(void) {
    return sc.path;
}

const char* shaderChainError(void) {
    return sc.error;
}

int shaderChainSetPath(const char* path) {
    const char* p = path != NULL ? path : "";
    if (edit == &chains[1]) {
        readMovieSetting(p);
        settingsSet(sc.key ? sc.key : "present_movie_shader", movieMode == 0 ? "same" : movieMode == 1 ? "none" : sc.path);
    } else {
        snprintf(sc.path, sizeof(sc.path), "%s", p);
        settingsSet("present_shader", sc.path);
    }
    if (!sc.ready) {
        return 0;
    }
    Chain* saved = cur;
    cur = edit;
    int ok = 0;
    if (sc.path[0] == '\0') {
        unload();
        sc.error[0] = '\0';
    } else {
        ok = load(sc.dev.beginOneShot(sc.dev.user));
    }
    cur = saved;
    return ok;
}

int shaderChainPasses(void) {
    return sc.loaded ? sc.preset.passes : 0;
}

int shaderChainParamCount(void) {
    return sc.loaded ? sc.preset.params : 0;
}

const SlangParam* shaderChainParam(int i) {
    return sc.loaded && i >= 0 && i < sc.preset.params ? &sc.preset.param[i] : NULL;
}

static void saveParams(void) {
    char text[16384]; /* every changed parameter, name=value; */
    slangParamsText(&sc.preset, text, sizeof(text));
    settingsSet(sc.paramsKey, text);
}

void shaderChainSetParam(int i, float value) {
    if (!sc.loaded || i < 0 || i >= sc.preset.params) {
        return;
    }
    SlangParam* p = &sc.preset.param[i];
    p->value = value < p->min ? p->min : value > p->max ? p->max : value;
    saveParams();
}

void shaderChainResetParams(void) {
    for (int i = 0; sc.loaded && i < sc.preset.params; i++) {
        sc.preset.param[i].value = sc.preset.param[i].def;
    }
    if (sc.paramsKey != NULL) {
        settingsSet(sc.paramsKey, "");
    }
}
