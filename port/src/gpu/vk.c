/*
 * The Vulkan device behind the GPU layer.
 *
 * VRAM is one 1024x512 R16_UINT storage image in GENERAL layout. Draws render
 * with no attachments: the fragment shader reads and writes VRAM itself, so
 * every PS1 pixel operation (CLUT lookups, blending in 5-bit, mask bits) is
 * exact. With VK_EXT_fragment_shader_interlock those read-modify-writes run in
 * primitive order per pixel; without it gp0.c ends a batch whenever a
 * primitive overlaps one already in it. The optional ordered fallback
 * instead collects bounded per-pixel operations and resolves them in compute,
 * preserving the existing rasterizer and integer pixel arithmetic.
 *
 * Commands are recorded into one of two frame contexts and handed to the
 * queue at vertical blank (with the present pass), at a readback, or when a
 * context's buffers are full. Every transfer or batch boundary is followed by
 * a full memory barrier before VRAM is touched again; at native resolution
 * these are cheap and they keep the ordering exactly the game's.
 *
 * The Vulkan loader comes from SDL (SDL_Vulkan_GetVkGetInstanceProcAddr), so
 * the executable does not link libvulkan.
 */

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "gpu_internal.h"
#include "interpolation.h"
#include "pc_wide.h"
#include "moviepack.h"
#include "shaderchain_vk.h"
#include "texture.h"
#include "ordered.h"
#include "ui/ui.h"
#include "system/cputime.h"
#include "system/settings.h"
#include "system/speedup.h"

#define OSD_WORDS 2048 /* the note, or the performance overlay's panel (present.c) */

static void presentDrain(void);
static void presentCollect(void);
static void presentShutdown(void);
static void cpuResolveFlush(void);
static void endNativeBatch(void);
static int hiScratchFor(int w, int h);
static int hiWideToSystem(void);
static void hiWideRows(int rows);
static void hiWideGrow(int rows);
#define HI_TRIM_HEAP_PERCENT 70 /* device heap fill (percent of budget) from which the wide canvas is trimmed and captures stay out of device memory */
static int deviceHeapPercent(VkDeviceSize extra);

/* Host presentation never owns the virtual clock or native frame count. */
static struct {
    InterpPacer pacer;
    GpuDisplay display;
    uint32_t vblank, nativeShown;
    uint64_t sequence, nativeSequence, polls, nativePolls, nativeFrames, missed, measuredAt, cost, deferred, revision;
    uint32_t measuredShown;
    double achieved;
    const char* status;
    int loaded, rate, realtime, active, quietFrames, log, dump, prepareRate, adaptive;
} hostPresent;

int presentationRate(void) {
    if (!hostPresent.loaded) {
        hostPresent.loaded = 1;
        settingsDescribe("interpolation_hz", "interpolation_hz: host presentation 60 (default)/120/144/240 Hz; 0 native; requires Speedup, Smooth motion, real time and compatible frames; shader presets use native presentation");
        int hz = settingsInt("interpolation_hz", 60);
        hostPresent.rate = hz == 60 || hz == 120 || hz == 144 || hz == 240 ? hz : 0;
        hostPresent.log = settingsBool("host_present_log", 0);
        hostPresent.dump = settingsBool("dump_host", 0);
    }
    return hostPresent.rate;
}
void gpuHostWrite(void) { hostPresent.revision++; }
void gpuHostInvalidate(void) {
    hostPresent.active = 0;
    hostPresent.quietFrames = 0;
    hostPresent.deferred = 0;
    interpPacerReset(&hostPresent.pacer, 0, 0);
}
void gpuHostReset(void) {
    presentDrain(); gpuHostWrite(); gpuHostInvalidate();
    hostPresent.cost = 0;
    hostPresent.prepareRate = hostPresent.adaptive = 0;
    hostPresent.measuredAt = 0;
    interpolationReset();
}
void gpuHostClock(int realtime) { hostPresent.realtime = realtime != 0; gpuHostReset(); }
uint64_t gpuHostDeadline(void) {
    uint64_t deadline = interpPacerDeadline(&hostPresent.pacer);
    return hostPresent.deferred > deadline ? hostPresent.deferred : deadline;
}
void presentationSetRate(int hz) {
    presentationRate();
    hostPresent.rate = hz == 60 || hz == 120 || hz == 144 || hz == 240 ? hz : 0;
    gpuHostReset();
    settingsSetInt("interpolation_hz", hostPresent.rate); settingsSave();
}

/* The settings menu (port/src/ui), drawn over the present pass (weak: the
 * GPU's test programs are built without it). */
extern void uiVulkanReady(const UiVulkan* dev) __attribute__((weak));
extern void uiRecord(void* cmd, int w, int h, int format, const int* movieRect) __attribute__((weak));
extern void uiVulkanShutdown(void) __attribute__((weak));
/* The extras (port/src/extras/extras.h), drawn the same way after the menu. */
extern void extrasVulkanReady(const UiVulkan* dev) __attribute__((weak));
extern void extrasRecord(void* cmd, int w, int h, int format) __attribute__((weak));
extern int extrasIsOpen(void) __attribute__((weak));
extern int uiIsOpen(void) __attribute__((weak));
extern void extrasVulkanShutdown(void) __attribute__((weak));
/* port/src/system/battlespeed.c: this vblank's picture is withheld (a game
 * frame between two shown ones at 2x/4x battle speed; weak: GPU tests). */
extern int battleSpeedHidden(void) __attribute__((weak));

static const uint32_t drawVertSpv[] =
#include "draw.vert.spv.inc"
    ;
static const uint32_t drawFragSpv[] =
#include "draw.frag.spv.inc"
    ;
static const uint32_t drawInterlockFragSpv[] =
#include "draw_interlock.frag.spv.inc"
    ;
static const uint32_t drawWideVertSpv[] =
#include "draw_wide.vert.spv.inc"
    ;
static const uint32_t drawWideFragSpv[] =
#include "draw_wide.frag.spv.inc"
    ;
static const uint32_t drawWideInterlockFragSpv[] =
#include "draw_wide_interlock.frag.spv.inc"
    ;
static const uint32_t presentVertSpv[] =
#include "present.vert.spv.inc"
    ;
static const uint32_t hiresVertSpv[] =
#include "hires.vert.spv.inc"
    ;
static const uint32_t hiresWideVertSpv[] =
#include "hires_wide.vert.spv.inc"
    ;
static const uint32_t hiresFragSpv[] =
#include "hires.frag.spv.inc"
    ;
static const uint32_t hiresInterlockFragSpv[] =
#include "hires_interlock.frag.spv.inc"
    ;
static const uint32_t hiresWideFragSpv[] =
#include "hires_wide.frag.spv.inc"
    ;
static const uint32_t hiresWideInterlockFragSpv[] =
#include "hires_wide_interlock.frag.spv.inc"
    ;
static const uint32_t hiresCoarseVertSpv[] =
#include "hires_coarse.vert.spv.inc"
    ;
static const uint32_t hiresWideCoarseVertSpv[] =
#include "hires_wide_coarse.vert.spv.inc"
    ;
static const uint32_t hiresCoarseFragSpv[] =
#include "hires_coarse.frag.spv.inc"
    ;
static const uint32_t hiresCoarseInterlockFragSpv[] =
#include "hires_coarse_interlock.frag.spv.inc"
    ;
static const uint32_t hiresWideCoarseFragSpv[] =
#include "hires_wide_coarse.frag.spv.inc"
    ;
static const uint32_t hiresWideCoarseInterlockFragSpv[] =
#include "hires_wide_coarse_interlock.frag.spv.inc"
    ;
static const uint32_t captureFragSpv[] =
#include "capture.frag.spv.inc"
    ;
static const uint32_t hiresUtilFragSpv[] =
#include "hires_util.frag.spv.inc"
    ;
static const uint32_t presentFragSpv[] =
#include "present.frag.spv.inc"
    ;
static const uint32_t presentPictureFragSpv[] =
#include "present_picture.frag.spv.inc"
    ;
static const uint32_t rotateFragSpv[] =
#include "rotate.frag.spv.inc"
    ;

static const uint32_t draw_ordered_vertSpv[] =
#include "draw_ordered.vert.spv.inc"
    ;
static const uint32_t draw_ordered_fragSpv[] =
#include "draw_ordered.frag.spv.inc"
    ;
static const uint32_t draw_wide_ordered_vertSpv[] =
#include "draw_wide_ordered.vert.spv.inc"
    ;
static const uint32_t draw_wide_ordered_fragSpv[] =
#include "draw_wide_ordered.frag.spv.inc"
    ;
static const uint32_t hires_ordered_vertSpv[] =
#include "hires_ordered.vert.spv.inc"
    ;
static const uint32_t hires_ordered_fragSpv[] =
#include "hires_ordered.frag.spv.inc"
    ;
static const uint32_t hires_wide_ordered_vertSpv[] =
#include "hires_wide_ordered.vert.spv.inc"
    ;
static const uint32_t hires_wide_ordered_fragSpv[] =
#include "hires_wide_ordered.frag.spv.inc"
    ;
static const uint32_t ordered_compSpv[] =
#include "ordered.comp.spv.inc"
    ;

/* ---- function pointers ---------------------------------------------------- */

#define VK_GLOBAL_FUNCS(X)                  \
    X(vkCreateInstance)                     \
    X(vkEnumerateInstanceExtensionProperties) \
    X(vkEnumerateInstanceLayerProperties)   \
    X(vkEnumerateInstanceVersion)

#define VK_INSTANCE_FUNCS(X)                    \
    X(vkDestroyInstance)                        \
    X(vkEnumeratePhysicalDevices)               \
    X(vkGetPhysicalDeviceProperties)            \
    X(vkGetPhysicalDeviceFeatures2)             \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties)      \
    X(vkGetPhysicalDeviceMemoryProperties2)     \
    X(vkGetPhysicalDeviceFormatProperties)      \
    X(vkEnumerateDeviceExtensionProperties)     \
    X(vkCreateDevice)                           \
    X(vkGetDeviceProcAddr)                      \
    X(vkDestroySurfaceKHR)                      \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)     \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)     \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    X(vkCreateDebugUtilsMessengerEXT)           \
    X(vkDestroyDebugUtilsMessengerEXT)

#define VK_DEVICE_FUNCS(X)              \
    X(vkDestroyDevice)                  \
    X(vkGetDeviceQueue)                 \
    X(vkDeviceWaitIdle)                 \
    X(vkCreateImage)                    \
    X(vkDestroyImage)                   \
    X(vkGetImageMemoryRequirements)     \
    X(vkAllocateMemory)                 \
    X(vkFreeMemory)                     \
    X(vkBindImageMemory)                \
    X(vkCreateBuffer)                   \
    X(vkDestroyBuffer)                  \
    X(vkGetBufferMemoryRequirements)    \
    X(vkBindBufferMemory)               \
    X(vkMapMemory)                      \
    X(vkCreateImageView)                \
    X(vkDestroyImageView)               \
    X(vkCreateShaderModule)             \
    X(vkDestroyShaderModule)            \
    X(vkCreateDescriptorSetLayout)      \
    X(vkDestroyDescriptorSetLayout)     \
    X(vkCreatePipelineLayout)           \
    X(vkDestroyPipelineLayout)          \
    X(vkCreateComputePipelines)         \
    X(vkCmdDispatch)                   \
    X(vkCmdFillBuffer)                 \
    X(vkCmdUpdateBuffer)               \
    X(vkCreateGraphicsPipelines)        \
    X(vkDestroyPipeline)                \
    X(vkCreateDescriptorPool)           \
    X(vkCreateSampler)                  \
    X(vkDestroySampler)                 \
    X(vkDestroyDescriptorPool)          \
    X(vkAllocateDescriptorSets)         \
    X(vkUpdateDescriptorSets)           \
    X(vkCreateCommandPool)              \
    X(vkDestroyCommandPool)             \
    X(vkAllocateCommandBuffers)         \
    X(vkResetCommandBuffer)             \
    X(vkBeginCommandBuffer)             \
    X(vkEndCommandBuffer)               \
    X(vkCreateFence)                    \
    X(vkDestroyFence)                   \
    X(vkGetFenceStatus)                 \
    X(vkWaitForFences)                  \
    X(vkResetFences)                    \
    X(vkCreateSemaphore)                \
    X(vkDestroySemaphore)               \
    X(vkQueueSubmit)                    \
    X(vkCmdPipelineBarrier2)            \
    X(vkCmdBeginRendering)              \
    X(vkCmdEndRendering)                \
    X(vkCmdBindPipeline)                \
    X(vkCmdBindDescriptorSets)          \
    X(vkCmdBindVertexBuffers)           \
    X(vkCmdDraw)                        \
    X(vkCmdSetViewport)                 \
    X(vkCmdSetScissor)                  \
    X(vkCmdPushConstants)               \
    X(vkCmdCopyBufferToImage)           \
    X(vkCmdCopyImageToBuffer)           \
    X(vkCmdCopyImage)                   \
    X(vkCmdBlitImage)                   \
    X(vkCmdCopyBuffer)                  \
    X(vkCmdClearColorImage)             \
    X(vkCreateQueryPool)                \
    X(vkDestroyQueryPool)               \
    X(vkCmdResetQueryPool)              \
    X(vkCmdWriteTimestamp2)             \
    X(vkGetQueryPoolResults)            \
    X(vkCreateSwapchainKHR)             \
    X(vkDestroySwapchainKHR)            \
    X(vkGetSwapchainImagesKHR)          \
    X(vkAcquireNextImageKHR)            \
    X(vkQueuePresentKHR)

#define VK_DECLARE(name) static PFN_##name name;
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
VK_GLOBAL_FUNCS(VK_DECLARE)
VK_INSTANCE_FUNCS(VK_DECLARE)
VK_DEVICE_FUNCS(VK_DECLARE)

/* ---- state ------------------------------------------------------------------- */

#define FRAMES SHADER_CHAIN_FRAMES
#define VERTEX_BYTES (8u << 20)
#define STAGING_BYTES (2u << 20)
#define SPAN_BYTES (8u << 20)
#define VRAM_BYTES (VRAM_W * VRAM_H * 2u)
/* Readbacks: VRAM, or a picture of the wide target (a frame with its margins). */
#define READBACK_BYTES (WIDE_W * VRAM_H * 2u)
#define MAX_SWAPCHAIN_IMAGES 8

/* XG_GPU_TIMING=1: GPU time per kind of pass, from timestamp queries. */
enum { GT_DRAW, GT_WIDE, GT_HIRES, GT_HIWIDE, GT_HIUTIL, GT_COPY, GT_PRESENT, GT_KINDS };
#define GT_QUERIES 4096

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint8_t* map;
} HostBuffer;

typedef struct {
    VkCommandBuffer cmd;
    VkFence fence;
    VkSemaphore acquired;
    HostBuffer vertices;
    HostBuffer interpolationRuns[16];
    size_t interpolationRunUsed[16];
    HostBuffer staging;
    HostBuffer spans;
    VkDescriptorSet set;
    size_t vertexUsed;
    size_t stagingUsed;
    size_t spanUsed;
    /* Decoded texture snapshots share the span arena, never overwritten
     * while this context has queued draws or a fence in flight. */
    struct { uint64_t revision; int u,v,w,h,scale; uint32_t base; } samples[256];
    int sampleCount;
    int recording;
    int submitted;
    uint64_t uses; /* recordings begun (late reads: whether its fence is still theirs) */
    /* XG_GPU_TIMING: a timestamp pair per pass, and each pair's kind. */
    VkQueryPool queries;
    uint32_t queryUsed;
    uint8_t queryKind[GT_QUERIES / 2];
    /* XG_HIRES_STATS=1 (with XG_GPU_TIMING=1): each hi-res pass's triangles
     * and 1x pixel area (all, semi-transparent, textured), printed with its
     * GPU time when its timestamps come back. */
    struct { uint32_t tris, runs; float area, semi, textured, surface, fringe, runMax; uint8_t interlock, coarse; } passStats[GT_QUERIES / 2];
    /* Auto resolution uses two queries for the whole recording, so a busy
     * fallback frame cannot overflow the per-pass diagnostic query pool. */
    VkQueryPool autoQueries;
    uint64_t autoEpoch, autoFrame;
    int autoScale, autoRecorded;
    /* A movie pack's picture for the present pass (moviepack.c): set 1 of
     * the present pipelines, its image (8:8:8 in r32ui, or the 1x1 stand-in),
     * the picture it holds and the upload buffer. */
    VkDescriptorSet movieSet;
    VkDescriptorSet interpolationSet[INTERP_SETS];
    VkDescriptorSet interpolationPicture[INTERP_FRAMES + 1];
    VkImage movieImage;
    VkDeviceMemory movieMemory;
    VkImageView movieView;
    int movieW, movieH;
    uint64_t movieId;
    HostBuffer movieStaging;
    size_t movieStagingBytes;
    /* A 15-bit movie frame's picture into the hi-res copy (gvkHiPicture):
     * its upload buffer, used once a recording. */
    HostBuffer movieHiStaging;
    size_t movieHiStagingBytes;
    int movieHiUsed;
    /* Pack images bound for the GPU's kept copy (resPlace): their upload
     * room and the copies recorded before this context's hi-res draws. */
    HostBuffer resStage;
    size_t resStageUsed;
    VkBufferCopy* resCopies;
    int resCopyCount, resCopyCap;
} FrameCtx;

static struct {
    SDL_Window* window;
    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    VkPhysicalDeviceMemoryProperties memProps;
    VkDevice device;
    uint32_t queueFamily, copyFamily;
    VkQueue queue;
    int interlock;
    VkPhysicalDeviceLimits limits;

    VkImage vram;
    VkDeviceMemory vramMemory;
    VkImageView vramView;
    VkImage scratch;
    VkDeviceMemory scratchMemory;
    /* The wide target (wide.c) and its draws, kept until the batch ends. */
    VkImage wide;
    VkDeviceMemory wideMemory;
    VkImageView wideView;
    VkPipeline wideDrawPipeline;
    uint32_t wideCount;

    /* The 1x VRAM's scratch as a storage image too (the hi-res copy's uploads). */
    VkImageView scratchView;
    int wideExtent; /* the wide target's columns written so far */

    /* The hi-res copy (hires.c): VRAM and the wide target N times, a scratch
     * image for overlapping moves, the StoreImage stash; their draws are kept
     * until the batch ends, like the wide target's. */
    int hiN;
    int presentAcquired;
    int hiColor;
    VkImage hiVram;
    VkDeviceMemory hiVramMemory;
    VkImageView hiVramView;
    VkImage hiWide;
    VkDeviceMemory hiWideMemory;
    VkImageView hiWideView;
    int hiWideW, hiWideH; /* allocated 1x extent; unused rows come from native on growth */
    int hiWideSystem;
    int hiFailN; /* a scale whose wide copy could not grow: later copies stay below it */
    VkImage hiWideCold; VkDeviceMemory hiWideColdMemory; VkImageView hiWideColdView;
    int hiWideColdW, hiWideColdH, hiWideColdY, hiWideRecentH, hiWideTrimClock; /* cold: canvas rows ColdY..ColdH-1 */
    int hiWideColdPxW, hiWideColdPxH; /* the cold image's own extent (hi-res pixels; it may be larger) */
    VkImage hiWideSpare; VkDeviceMemory hiWideSpareMemory; VkImageView hiWideSpareView;
    int hiWideSpareW, hiWideSpareH; /* a cold image whose rows went back, kept for the next trim (hiColdSpare) */
    unsigned hiSetsStale; /* frame contexts whose hi set still names a replaced canvas (hiTrimWideBody) */
    struct { VkImage image; VkDeviceMemory memory; VkImageView view; unsigned pending; } hiRetired[4];
    int hiWideQuiet, hiWideQuietH; /* trim windows in a row with unused rows, and the most they used */
    int hiWideKept; /* unused rows kept resident for want of heap pressure, logged once */
    int memoryBudget; /* VK_EXT_memory_budget: the device heaps' usage and budget (deviceHeapPercent) */
    int frozenLocalLogged; /* the interpolation's frozen captures took device memory: logged once */
    VkImage hiScratch;
    VkDeviceMemory hiScratchMemory;
    VkImageView hiScratchView;
    int hiScratchW, hiScratchH; /* in hi-res pixels */
    VkDescriptorSet effectSet; /* set 1: private source, separate from movie frames */
    VkImage hiEffect;
    VkDeviceMemory hiEffectMemory;
    VkImageView hiEffectView;
    int hiEffectW, hiEffectH;
    int hiEffectWidth[2], hiEffectHeight, hiEffectValid;
    VkImage hiStash;
    VkDeviceMemory hiStashMemory;
    VkImageView hiStashView;
    VkImage dummy32; /* bound where a hi-res image is not made */
    VkDeviceMemory dummy32Memory;
    VkImageView dummy32View;
    VkPipelineLayout hiLayout;
    VkPipeline hiDrawPipeline;
    VkPipeline hiUnorderedPipeline;
    /* upscale_large=half (drawHiBatch): the hi-res passes at N/2 with 2x2 blocks, narrow and wide, interlocked and not. */
    VkPipeline hiCoarsePipeline, hiCoarseUnorderedPipeline, hiWideCoarsePipeline, hiWideCoarseUnorderedPipeline;
    VkPipeline interpolationWidePipeline;
    VkPipeline hiWidePipeline;
    VkPipeline hiUtilPipeline, capturePipeline;
    uint32_t hiCount, hiWideCount;
    /* finishBatch's independence tests: pending vertices already examined
     * and whether one of them depends on native VRAM. Reset with the counts. */
    uint32_t hiWideScanned, wideScanned;
    int hiWideDependent, wideDependent;
    HostBuffer hiReadback;
    size_t hiReadbackBytes;

    VkDescriptorSetLayout setLayout;
    VkPipelineLayout pipelineLayout;
    VkDescriptorPool descriptorPool;
    VkPipeline drawPipeline;
    VkPipeline presentPipeline;
    VkFormat presentFormat;

    VkCommandPool commandPool;
    FrameCtx frames[FRAMES];
    int cur;
    int rendering;
    uint32_t batchFirst;
    int batchQuery; /* XG_GPU_TIMING: the batch's timestamps */
    int needBarrier;
    HostBuffer readback;

    VkSwapchainKHR swapchain;
    VkFormat swapchainFormat;
    VkExtent2D swapchainExtent;
    uint32_t swapchainImageCount;
    VkImage swapchainImages[MAX_SWAPCHAIN_IMAGES];
    VkImageView swapchainViews[MAX_SWAPCHAIN_IMAGES];
    VkSemaphore presented[MAX_SWAPCHAIN_IMAGES];
    VkPresentModeKHR presentMode;
    int swapchainStale;
    uint32_t framesShown;
    uint32_t framesSkipped;
    uint64_t swapchainRetryNs; /* after a failed creation, when to try again */
    int windowW, windowH;      /* the window's pixel size the swapchain was made for */
    int surfaceLost;           /* the window had a surface; it is gone, made again at the retry time */
    /* Pre-rotation (below): quarter turns clockwise from the player's view to the swapchain
     * image; XG_VK_ROTATE's (-1: none); the image the present pass draws into, and the turn. */
    int rotate, rotateForced;
    VkImage rotImage;
    VkDeviceMemory rotMemory;
    VkImageView rotView;
    int rotW, rotH;
    VkFormat rotFormat, rotPipelineFormat;
    VkSampler rotSampler;
    VkDescriptorSetLayout rotSetLayout;
    VkPipelineLayout rotLayout;
    VkDescriptorPool rotPool;
    VkDescriptorSet rotSet;
    VkPipeline rotPipeline;

    /* Presentation health (gvkPresentStats, XG_PRESENT_LOG, the stall guard). */
    int lastAcquire;           /* VkResult of this vblank's acquire; PRESENT_NOT_TRIED */
    int lastPresent;           /* VkResult of the last present */
    uint32_t lastImage;
    double acquireMs;          /* time spent acquiring */
    int noPicture;             /* vblanks in a row the visible window got no new picture */
    int stallLimit;            /* noPicture that counts as a stall; doubles while recoveries fail */
    int stallBase;             /* XG_PRESENT_STALL, 0: no guard */
    uint32_t stallRecoveries;
    uint32_t testLeakAt;       /* XG_PRESENT_TEST=leak@<vblank>[/<every>] */
    uint32_t testLeakEvery;

    /* Readbacks from another thread while the game thread is stuck (SIGUSR1):
     * a second queue, used by nothing else. */
    VkQueue rescueQueue;
    VkQueue presentQueue;
    VkQueue copyQueue;
    int copyPresent,bufferedMode;
    VkCommandPool rescuePool;
    VkCommandBuffer rescueCmd;
    VkFence rescueFence;
    HostBuffer rescueBuffer;

    /* XG_DUMP_PRESENT: the present pass drawn again into an image we can read,
     * in the swapchain's format once there is one (the menu's pipeline has
     * one format), else RGBA. */
    VkImage dumpImage;
    VkDeviceMemory dumpMemory;
    VkImageView dumpView;
    HostBuffer dumpBuffer;
    VkPipeline dumpPipeline;
    VkFormat dumpFormat;
    int dumpW, dumpH;

    /* The shader chain (shaderchain.c): present.frag into its R8G8B8A8
     * picture image. */
    VkPipeline chainPicturePipeline;

    /* Set 1 of vk.pipelineLayout: a movie pack's picture (FrameCtx). */
    VkDescriptorSetLayout movieSetLayout;
    VkDescriptorPool moviePool;
    VkDescriptorPool interpolationPool;
    VkImage interpolationImage[INTERP_FRAMES + 1];
    int interpolationSystem[INTERP_FRAMES];
    VkDeviceMemory interpolationMemory[INTERP_FRAMES + 1];
    VkImageView interpolationView[INTERP_FRAMES + 1];
    int interpolationCanvasW, interpolationCanvasH;
    int interpolationRetry, interpolationFailedN;
    struct {
        int valid, w, h, words, scale, color;
        uint64_t revision;
        VkSwapchainKHR swapchain;
        PresentConstants pc;
        uint32_t bits[OSD_WORDS];
    } held;
    VkImage interpolationFrozen[INTERP_FRAMES*INTERP_FREEZES];
    VkDeviceMemory interpolationFrozenMemory[INTERP_FRAMES*INTERP_FREEZES];
    VkImageView interpolationFrozenView[INTERP_FRAMES*INTERP_FREEZES];
    int interpolationFrozenW[INTERP_FRAMES*INTERP_FREEZES], interpolationFrozenH[INTERP_FRAMES*INTERP_FREEZES], interpolationFrozenN[INTERP_FRAMES*INTERP_FREEZES];
    VkImage interpolationSource;
    VkDeviceMemory interpolationSourceMemory;
    VkImageView interpolationSourceView;
    int interpolationSourceW, interpolationSourceH;
    int interpolationW, interpolationH, interpolationN;
    HiVertex* interpolationVertices;
    int32_t* interpolationSpans;
    uint32_t* interpolationSteps;
    VkImage interpolationCache;
    VkDeviceMemory interpolationCacheMemory;
    VkImageView interpolationCacheView;
    VkDescriptorSet pictureOverride;
    uint64_t interpolatedPictures, intermediatePictures;
    /* gvkHiPicture: the picture (RGBA8) and its size in the copy (RGBA8). */
    VkImage movieHiSrc, movieHiDst;
    VkDeviceMemory movieHiSrcMemory, movieHiDstMemory;
    int movieHiSrcW, movieHiSrcH, movieHiDstW, movieHiDstH;
    uint64_t movieHiSrcId; /* the picture in movieHiSrc */
    int movieHiSrcMask;

    /* Pack images kept on the GPU (resPlace): set 0's binding 8. */
    VkBuffer resBuf, resDummy;
    VkDeviceMemory resMem, resDummyMem;
    uint32_t resCapWords, resTopWords, resLimitWords;
    /* Ordered fallback resources are transient host rendering state. Queued
     * fills/updates reuse them in queue order, never by a host write. */
    int orderedEnabled, orderedState;
    uint32_t orderedCapacity;
    VkDescriptorSetLayout orderedLayout;
    VkPipelineLayout orderedResolveLayout;
    VkDescriptorPool orderedPool;
    VkDescriptorSet orderedSet;
    VkBuffer orderedNodes, orderedHeads;
    VkDeviceMemory orderedNodesMemory, orderedHeadsMemory;
    VkPipeline orderedDraw, orderedWide, orderedHi, orderedHiWide, orderedResolve;
    uint64_t orderedPasses;
    int resState; /* 0 not made yet, 1 made, -1 could not be */
    uint64_t resUploads, resUploadBytes;
    int copySkipped; /* the last collected copy-path picture found no free image */
    /* Diagnostic and test switches (set at all, as getenv tests them), read
     * once at gvkInit: several are checked per barrier, batch or picture.
     * No test changes them while the device is up. */
    struct {
        int fullBarriers, presentSync, directPresent, serialReplay;
        int interpolationOrdered, interpolationLog, allInterlock, immediateResolve, interpolationSpill;
        int hiresStats; /* XG_HIRES_STATS=1: a line per hi-res pass (gtHarvest) */
    } env;
} vk;

#define VK_CHECK(call)                                                       \
    do {                                                                     \
        VkResult r_ = (call);                                                \
        if (r_ != VK_SUCCESS) {                                              \
            gpuLog("vulkan: %s failed (%d) at %s:%d", #call, (int)r_, __FILE__, __LINE__); \
            abort();                                                         \
        }                                                                    \
    } while (0)

/* ---- helpers ---------------------------------------------------------------- */

static int envFlag(const char* name) {
    const char* v = getenv(name);
    return v != NULL && v[0] != '\0' && strcmp(v, "0") != 0;
}

static uint32_t findMemory(uint32_t typeBits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags fallback) {
    for (int pass = 0; pass < 2; pass++) {
        VkMemoryPropertyFlags flags = pass == 0 ? want : fallback;
        for (uint32_t i = 0; i < vk.memProps.memoryTypeCount; i++) {
            if ((typeBits & (1u << i)) && (vk.memProps.memoryTypes[i].propertyFlags & flags) == flags) {
                return i;
            }
        }
    }
    gpuLog("vulkan: no memory type for %08x", (unsigned)want);
    abort();
}

/* 0 (nothing left made) when the heap cannot hold it: for optional storage
 * that degrades instead (hiWideGrow's staging copy). */
static int tryHostBufferMemory(HostBuffer* b, VkDeviceSize size, VkBufferUsageFlags usage, int cached, int system) {
    memset(b, 0, sizeof(*b));
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk.device, &bi, NULL, &b->buffer) != VK_SUCCESS) {
        b->buffer = VK_NULL_HANDLE;
        return 0;
    }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(vk.device, b->buffer, &req);
    VkMemoryPropertyFlags base = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemory(req.memoryTypeBits, cached ? base | VK_MEMORY_PROPERTY_HOST_CACHED_BIT : base, base);
    if (system) {
        /* Prefer ordinary RAM over a host-visible VRAM aperture. A unified
         * device has no separate heap, so its normal host type is retained. */
        for (int pass = 0; pass < 2; pass++) {
            VkMemoryPropertyFlags want = pass == 0 ? base | VK_MEMORY_PROPERTY_HOST_CACHED_BIT : base;
            uint32_t found = UINT32_MAX;
            for (uint32_t i = 0; i < vk.memProps.memoryTypeCount; i++) {
                VkMemoryPropertyFlags flags = vk.memProps.memoryTypes[i].propertyFlags;
                if ((req.memoryTypeBits & (1u << i)) && !(flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
                    (flags & want) == want) { found = i; break; }
            }
            if (found != UINT32_MAX) { ai.memoryTypeIndex = found; break; }
        }
    }
    void* map = NULL;
    if (vkAllocateMemory(vk.device, &ai, NULL, &b->memory) != VK_SUCCESS) {
        vkDestroyBuffer(vk.device, b->buffer, NULL);
        memset(b, 0, sizeof(*b));
        return 0;
    }
    if (vkBindBufferMemory(vk.device, b->buffer, b->memory, 0) != VK_SUCCESS ||
        vkMapMemory(vk.device, b->memory, 0, VK_WHOLE_SIZE, 0, &map) != VK_SUCCESS) {
        vkDestroyBuffer(vk.device, b->buffer, NULL);
        vkFreeMemory(vk.device, b->memory, NULL);
        memset(b, 0, sizeof(*b));
        return 0;
    }
    b->map = map;
    return 1;
}

static void createHostBufferMemory(HostBuffer* b, VkDeviceSize size, VkBufferUsageFlags usage, int cached, int system) {
    if (!tryHostBufferMemory(b, size, usage, cached, system)) {
        gpuLog("vulkan: no memory for a %llu-byte host buffer", (unsigned long long)size);
        abort();
    }
}

static void createHostBuffer(HostBuffer* b, VkDeviceSize size, VkBufferUsageFlags usage, int cached) {
    createHostBufferMemory(b, size, usage, cached, 0);
}

static void destroyHostBuffer(HostBuffer* b) {
    if (b->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk.device, b->buffer, NULL);
        vkFreeMemory(vk.device, b->memory, NULL);
    }
    memset(b, 0, sizeof(*b));
}

static void createImage16(VkImage* image, VkDeviceMemory* memory, VkImageUsageFlags usage, int w, int h) {
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R16_UINT;
    ii.extent.width = (uint32_t)w;
    ii.extent.height = (uint32_t)h;
    ii.extent.depth = 1;
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(vk.device, &ii, NULL, image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk.device, *image, &req);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    VK_CHECK(vkAllocateMemory(vk.device, &ai, NULL, memory));
    VK_CHECK(vkBindImageMemory(vk.device, *image, *memory, 0));
}

static void createVramImage(VkImage* image, VkDeviceMemory* memory, VkImageUsageFlags usage) {
    createImage16(image, memory, usage, VRAM_W, VRAM_H);
}

static VkShaderModule shaderModule(const uint32_t* code, size_t size) {
    VkShaderModuleCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    si.codeSize = size;
    si.pCode = code;
    VkShaderModule m;
    VK_CHECK(vkCreateShaderModule(vk.device, &si, NULL, &m));
    return m;
}

static const VkImageSubresourceRange colorRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
static const VkImageSubresourceLayers colorLayers = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};

/* Everything before is finished and visible to everything after. */
static void fullBarrier(VkCommandBuffer cmd) {
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

/* Native raster/copy dependencies do not touch vertex fetch or the
 * vertex shader. Let those stages prepare the next primitive while image
 * writes finish; all storage, transfer, colour and depth accesses stay
 * ordered exactly as before. */
static void rasterBarrier(VkCommandBuffer cmd) {
    if(vk.env.fullBarriers) { fullBarrier(cmd);return; }
    VkMemoryBarrier2 memory={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    memory.srcStageMask=memory.dstStageMask=VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT |
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    memory.srcAccessMask=VK_ACCESS_2_MEMORY_WRITE_BIT;
    memory.dstAccessMask=VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dep={.sType=VK_STRUCTURE_TYPE_DEPENDENCY_INFO,.memoryBarrierCount=1,.pMemoryBarriers=&memory};
    vkCmdPipelineBarrier2(cmd,&dep);
}

/* A copy into a host buffer, for the host to read once the submission's
 * fence is signalled: the fence alone does not make device writes visible
 * to the host (Vulkan spec, "Fences"), this barrier does. */
static void hostReadBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    mb.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &di);
}

static void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                         VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                         VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 ib = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    ib.srcStageMask = srcStage;
    ib.srcAccessMask = srcAccess;
    ib.dstStageMask = dstStage;
    ib.dstAccessMask = dstAccess;
    ib.oldLayout = from;
    ib.newLayout = to;
    ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = image;
    ib.subresourceRange = colorRange;
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &ib;
    vkCmdPipelineBarrier2(cmd, &di);
}

/* ---- GPU timing (XG_GPU_TIMING) ------------------------------------------------
 *
 * XG_GPU_TIMING=1 writes a line per vblank: the GPU time of the passes whose
 * results came back since the last line (a frame context's are read when it
 * is reused, so a line is about one vblank late), per kind of pass: the
 * exact VRAM draws (draw), their wide copy (wide), the hi-res copy of both
 * (hires, hiwide), the hi-res copy's transfers (hiutil), VRAM copies and
 * uploads (copy) and the present pass (present). At exit, the run's mean and
 * worst vblank. XG_GPU_TIMING=summary: only that. */

static const char* const gtNames[GT_KINDS] = {"draw", "wide", "hires", "hiwide", "hiutil", "copy", "present"};

static struct {
    int on;       /* 1: a line per vblank; 2: the summary only */
    int collect;  /* gvkGpuTimingCollect: the settings menu's overlay and log, without lines */
    double last, lastKind[GT_KINDS]; /* the last vblank's (gvkGpuTimingLast) */
    int lastValid;
    double nsPerTick;
    uint64_t mask; /* the timestamps' valid bits */
    double ms[GT_KINDS]; /* since the last line */
    uint32_t passes[GT_KINDS];
    double runMs[GT_KINDS], runMax[GT_KINDS], runTotal, runTotalMax;
    uint32_t runLines, runBusy;
} gt;

static struct {
    int on, available, have, valid, scale;
    uint64_t epoch, frame, sumFrame;
    double sum, readyMs;
    unsigned readyFrames;
} autoTime;

void gvkGpuTimeReset(void) {
    autoTime.epoch++;
    autoTime.have = autoTime.readyFrames = 0;
    autoTime.sum = autoTime.readyMs = 0;
}

int gvkDeviceIntegrated(void) {
    static int known; /* 0 not asked, else the answer */
    if (vk.physical == VK_NULL_HANDLE) return 0;
    if (known == 0) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(vk.physical, &props);
        known = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1 : -1;
    }
    return known;
}

int gvkGpuTimeEnable(int on) {
    on = on && autoTime.available;
    if (on != autoTime.on) {
        gvkGpuTimeReset();
        autoTime.on = on;
    }
    return on;
}

int gvkGpuTimeTake(double* ms, unsigned* frames, int* scale) {
    if (!autoTime.on || !autoTime.readyFrames) return 0;
    *ms = autoTime.readyMs;
    *frames = autoTime.readyFrames;
    *scale = autoTime.scale;
    autoTime.readyMs = 0;
    autoTime.readyFrames = 0;
    return 1;
}

/* Submissions are harvested in queue order as frame contexts are reused.
 * A new logical frame closes the previous one's sum, including any idle
 * vblanks between them. Never wait here: recording() already owns the fence.
 * Vulkan query rules: https://docs.vulkan.org/spec/latest/chapters/queries.html */
static void autoTimeHarvest(FrameCtx* f) {
    if (!f->autoRecorded) return;
    f->autoRecorded = 0;
    if (!autoTime.on || f->autoEpoch != autoTime.epoch) return;
    if (autoTime.have && f->autoFrame != autoTime.sumFrame) {
        uint64_t frames = f->autoFrame - autoTime.sumFrame;
        if (autoTime.valid && frames > 0 && frames <= 120) {
            autoTime.readyMs += autoTime.sum;
            autoTime.readyFrames += (unsigned)frames;
        }
        autoTime.have = 0;
    }
    if (!autoTime.have) {
        autoTime.have = autoTime.valid = 1;
        autoTime.sumFrame = f->autoFrame;
        autoTime.scale = f->autoScale;
        autoTime.sum = 0;
    }
    uint64_t t[2];
    if (f->autoScale != autoTime.scale ||
        vkGetQueryPoolResults(vk.device, f->autoQueries, 0, 2, sizeof(t), t, sizeof(t[0]),
                              VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
        autoTime.valid = 0;
    } else {
        double total=(double)((t[1] - t[0]) & gt.mask) * gt.nsPerTick * 1e-6;
        autoTime.sum += total;
    }
}

/* A timed pass of `kind` starts: its pair of queries, or -1. */
static int gtStart(VkCommandBuffer cmd, int kind) {
    FrameCtx* f = &vk.frames[vk.cur];
    if (!(gt.on || gt.collect) || f->queries == VK_NULL_HANDLE || f->queryUsed + 2 > GT_QUERIES) {
        return -1;
    }
    uint32_t q = f->queryUsed;
    f->queryKind[q / 2] = (uint8_t)kind;
    f->queryUsed += 2;
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f->queries, q);
    return (int)q;
}

static void gtStop(VkCommandBuffer cmd, int q) {
    if (q >= 0) {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, vk.frames[vk.cur].queries, (uint32_t)q + 1);
    }
}

/* A frame context's results, once the device is done with it. */
static void gtHarvest(FrameCtx* f) {
    static uint64_t t[GT_QUERIES];
    if (f->queryUsed != 0 &&
        vkGetQueryPoolResults(vk.device, f->queries, 0, f->queryUsed, sizeof(uint64_t) * f->queryUsed, t,
                              sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
        for (uint32_t i = 0; i + 1 < f->queryUsed; i += 2) {
            int k = f->queryKind[i / 2];
            double ms = (double)((t[i + 1] - t[i]) & gt.mask) * gt.nsPerTick * 1e-6;
            gt.ms[k] += ms;
            gt.passes[k]++;
            if (vk.env.hiresStats && (k == GT_HIRES || k == GT_HIWIDE)) {
                const __typeof__(f->passStats[0])* ps = &f->passStats[i / 2];
                gpuLog("hires pass: %s %.3f ms tris %u area %.0f semi %.0f textured %.0f %s%s surface %.0f fringe %.0f runs %u runmax %.0f",
                       gtNames[k], ms, ps->tris, ps->area, ps->semi, ps->textured, ps->interlock ? "interlock" : "disjoint",
                       ps->coarse ? " coarse" : "", ps->surface, ps->fringe, ps->runs, ps->runMax);
            }
        }
    }
    f->queryUsed = 0;
}

/* Without XG_GPU_TIMING (the settings menu's performance overlay and log,
 * port/src/ui/diagnostics.cpp): the same timestamps, the last vblank's kept
 * for gvkGpuTimingLast, nothing written. 0 when the device has no
 * timestamps. A frame context's query pool is made at its next use. */
int gvkGpuTimingCollect(int on) {
    if (gt.nsPerTick <= 0) return 0;
    gt.collect = on != 0;
    if (!on) gt.lastValid = 0;
    return 1;
}

int gvkGpuTimingLast(double* total, double* kinds, int count) {
    if (!gt.lastValid) return 0;
    *total = gt.last;
    for (int k = 0; k < count && k < GT_KINDS; k++) kinds[k] = gt.lastKind[k];
    return 1;
}

const char* gvkGpuTimingName(int kind) {
    return kind >= 0 && kind < GT_KINDS ? gtNames[kind] : NULL;
}

static void gtVBlank(uint32_t vblank) {
    if (!gt.on && !gt.collect) {
        return;
    }
    double total = 0;
    char line[512];
    int len = 0;
    for (int k = 0; k < GT_KINDS; k++) total += gt.ms[k];
    gt.last = total;
    memcpy(gt.lastKind, gt.ms, sizeof(gt.lastKind));
    gt.lastValid = 1;
    if (!gt.on) {
        memset(gt.ms, 0, sizeof(gt.ms));
        memset(gt.passes, 0, sizeof(gt.passes));
        return;
    }
    total = 0;
    for (int k = 0; k < GT_KINDS; k++) {
        total += gt.ms[k];
        gt.runMs[k] += gt.ms[k];
        gt.runMax[k] = gt.ms[k] > gt.runMax[k] ? gt.ms[k] : gt.runMax[k];
        if (gt.passes[k] != 0 && len < (int)sizeof(line) - 48) {
            len += snprintf(line + len, sizeof(line) - (size_t)len, " %s %.3f/%u", gtNames[k], gt.ms[k], gt.passes[k]);
        }
    }
    gt.runTotal += total;
    gt.runTotalMax = total > gt.runTotalMax ? total : gt.runTotalMax;
    gt.runLines++;
    gt.runBusy += total > 0;
    if (gt.on == 1) {
        gpuLog("gpu time: vb %u %.3f ms:%s", vblank, total, len > 0 ? line : " -");
    }
    memset(gt.ms, 0, sizeof(gt.ms));
    memset(gt.passes, 0, sizeof(gt.passes));
}

static void gtSummary(void) {
    if (!gt.on || gt.runLines == 0) {
        return;
    }
    double n = (double)gt.runLines;
    char line[512];
    int len = 0;
    for (int k = 0; k < GT_KINDS; k++) {
        if (gt.runMs[k] > 0 && len < (int)sizeof(line) - 48) {
            len += snprintf(line + len, sizeof(line) - (size_t)len, " %s %.3f/%.3f", gtNames[k], gt.runMs[k] / n,
                            gt.runMax[k]);
        }
    }
    gpuLog("gpu time: %u vblanks, mean %.3f ms, worst %.3f ms; per kind mean/worst:%s", gt.runLines, gt.runTotal / n,
           gt.runTotalMax, line);
}

/* ---- instance and device ------------------------------------------------------ */

static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                   VkDebugUtilsMessageTypeFlagsEXT types,
                                                   const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    (void)types;
    (void)user;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        gpuLog("vulkan validation: %s", data->pMessage);
    }
    return VK_FALSE;
}

static int loadLoader(void) {
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
    if (vkGetInstanceProcAddr == NULL && SDL_Vulkan_LoadLibrary(NULL)) {
        vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
    }
    if (vkGetInstanceProcAddr == NULL) {
        /* No video driver with Vulkan (headless): load the loader directly. */
        static const char* const names[] = {"libvulkan.so.1", "libvulkan.so", "vulkan-1.dll", "libvulkan.1.dylib"};
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]) && vkGetInstanceProcAddr == NULL; i++) {
            SDL_SharedObject* lib = SDL_LoadObject(names[i]);
            if (lib != NULL) {
                vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_LoadFunction(lib, "vkGetInstanceProcAddr");
            }
        }
    }
    if (vkGetInstanceProcAddr == NULL) {
        gpuLog("vulkan: no loader (%s)", SDL_GetError());
        return 0;
    }
#define VK_LOAD_GLOBAL(name) name = (PFN_##name)vkGetInstanceProcAddr(VK_NULL_HANDLE, #name);
    VK_GLOBAL_FUNCS(VK_LOAD_GLOBAL)
    return vkCreateInstance != NULL;
}

static int hasLayer(const char* name) {
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, NULL);
    VkLayerProperties* props = calloc(n, sizeof(*props));
    vkEnumerateInstanceLayerProperties(&n, props);
    int found = 0;
    for (uint32_t i = 0; i < n; i++) {
        found |= strcmp(props[i].layerName, name) == 0;
    }
    free(props);
    return found;
}

/* Optional presentation diagnostics. Queue acceptance alone does not
 * prove display progress. Record supported presentation-stage feedback and
 * sampled output hashes only when XG_PRESENT_TIMING_LOG names a file. */
static int timingEnabled;
#if defined(VK_EXT_present_timing) && defined(VK_KHR_present_id2) && !defined(XG_TEST_NO_PRESENT_TIMING)
static VkPresentStageFlagsEXT timingStages;
static uint64_t timingDomain,timingId;
static FILE* timingFile;
static PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR timingCapabilities;
static PFN_vkGetSwapchainTimeDomainPropertiesEXT timingDomains;
static PFN_vkSetSwapchainPresentTimingQueueSizeEXT timingQueueSize;
static PFN_vkGetPastPresentationTimingEXT timingPast;
static struct { double vb;uint64_t ns;int kind;uint64_t hash; } timingMeta[4096];
static void timingCollect(void) {
    if(!timingEnabled || !timingFile || !vk.swapchain)return;
    VkPresentStageTimeEXT stages[64][2];VkPastPresentationTimingEXT reports[64];
    for(int i=0;i<64;i++)reports[i]=(VkPastPresentationTimingEXT){.sType=VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_EXT,.presentStageCount=2,.pPresentStages=stages[i]};
    VkPastPresentationTimingInfoEXT info={.sType=VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_INFO_EXT,.swapchain=vk.swapchain};
    VkPastPresentationTimingPropertiesEXT result={.sType=VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_PROPERTIES_EXT,.presentationTimingCount=64,.pPresentationTimings=reports};
    VkResult r=timingPast(vk.device,&info,&result);
    if(r!=VK_SUCCESS && r!=VK_INCOMPLETE)return;
    for(unsigned i=0;i<result.presentationTimingCount;i++) {
        VkPastPresentationTimingEXT* v=&reports[i];uint64_t out=0,end=0;
        for(unsigned j=0;j<v->presentStageCount;j++) {
            if(stages[i][j].stage==VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT)out=stages[i][j].time;
            if(stages[i][j].stage==VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT)end=stages[i][j].time;
        }
        unsigned m=v->presentId%4096;
        fprintf(timingFile,"%llu %.9f %d %llu %llu %llu %u %llu %u %016llx\n",(unsigned long long)v->presentId,timingMeta[m].vb,timingMeta[m].kind,
            (unsigned long long)timingMeta[m].ns,(unsigned long long)out,(unsigned long long)end,v->timeDomain,(unsigned long long)v->timeDomainId,v->reportComplete,(unsigned long long)__atomic_load_n(&timingMeta[m].hash,__ATOMIC_ACQUIRE));
    }
    fflush(timingFile);
}
static uint64_t timingNote(double vb,int kind) {
    if(!timingEnabled)return 0;
    uint64_t id=++timingId;timingMeta[id%4096].vb=vb;timingMeta[id%4096].kind=kind;timingMeta[id%4096].ns=SDL_GetTicksNS();__atomic_store_n(&timingMeta[id%4096].hash,0,__ATOMIC_RELEASE);return id;
}
static VkResult timingPresent(VkQueue queue,const VkPresentInfoKHR* info,uint64_t id) {
    if(!timingEnabled || !id)return vkQueuePresentKHR(queue,info);
    VkPresentTimingInfoEXT one={.sType=VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT,.timeDomainId=timingDomain,.presentStageQueries=timingStages};
    VkPresentTimingsInfoEXT times={.sType=VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT,.swapchainCount=1,.pTimingInfos=&one};
    VkPresentId2KHR ids={.sType=VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR,.pNext=&times,.swapchainCount=1,.pPresentIds=&id};
    VkPresentInfoKHR tracked=*info;tracked.pNext=&ids;return vkQueuePresentKHR(queue,&tracked);
}

static void timingClose(void) {
    timingCollect();if(timingFile)fclose(timingFile);timingFile=NULL;
    timingEnabled=0;timingId=0;
}

#else
static void timingCollect(void) {}
static void timingClose(void) {}
static uint64_t timingNote(double vb,int kind) { (void)vb;(void)kind;return 0; }
static VkResult timingPresent(VkQueue queue,const VkPresentInfoKHR* info,uint64_t id) { (void)id;return vkQueuePresentKHR(queue,info); }
#endif

static int createInstance(int wantSurface) {
    uint32_t version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion != NULL) {
        vkEnumerateInstanceVersion(&version);
    }
    if (version < VK_API_VERSION_1_3) {
        gpuLog("vulkan: loader %u.%u, need 1.3", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version));
        return 0;
    }
    const char* exts[16];
    uint32_t extCount = 0;
    if (wantSurface) {
        Uint32 n = 0;
        const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&n);
        for (Uint32 i = 0; sdlExts != NULL && i < n && extCount < 12; i++) {
            exts[extCount++] = sdlExts[i];
        }
    }
    if(wantSurface && getenv("XG_PRESENT_TIMING_LOG"))exts[extCount++]=VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
    const char* layers[1];
    uint32_t layerCount = 0;
    int validation = envFlag("XG_VK_VALIDATION");
    if (validation) {
        if (hasLayer("VK_LAYER_KHRONOS_validation")) {
            layers[layerCount++] = "VK_LAYER_KHRONOS_validation";
            exts[extCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        } else {
            gpuLog("vulkan: XG_VK_VALIDATION set but the validation layer is not installed");
            validation = 0;
        }
    }
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Xenogears";
    app.pEngineName = "xenogears-pc";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = extCount;
    ci.ppEnabledExtensionNames = exts;
    ci.enabledLayerCount = layerCount;
    ci.ppEnabledLayerNames = layers;
    VkResult r = vkCreateInstance(&ci, NULL, &vk.instance);
    if (r != VK_SUCCESS) {
        gpuLog("vulkan: vkCreateInstance failed (%d)", (int)r);
        return 0;
    }
#define VK_LOAD_INSTANCE(name) name = (PFN_##name)vkGetInstanceProcAddr(vk.instance, #name);
    VK_INSTANCE_FUNCS(VK_LOAD_INSTANCE)
    if (validation && vkCreateDebugUtilsMessengerEXT != NULL) {
        VkDebugUtilsMessengerCreateInfoEXT mi = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mi.pfnUserCallback = debugMessage;
        vkCreateDebugUtilsMessengerEXT(vk.instance, &mi, NULL, &vk.messenger);
    }
    return 1;
}

static int hasDeviceExtension(VkPhysicalDevice pd, const char* name) {
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(pd, NULL, &n, NULL);
    VkExtensionProperties* props = calloc(n, sizeof(*props));
    vkEnumerateDeviceExtensionProperties(pd, NULL, &n, props);
    int found = 0;
    for (uint32_t i = 0; i < n; i++) {
        found |= strcmp(props[i].extensionName, name) == 0;
    }
    free(props);
    return found;
}

typedef struct {
    int usable;
    int score;
    uint32_t family;
    uint32_t queues; /* in that family */
    int interlock;
} DeviceChoice;

static DeviceChoice rateDevice(VkPhysicalDevice pd) {
    DeviceChoice c = {0};
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pd, &props);
    if (props.apiVersion < VK_API_VERSION_1_3) {
        return c;
    }
    VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT il = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT};
    VkPhysicalDeviceVulkan13Features f13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.pNext = &il;
    VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f13;
    vkGetPhysicalDeviceFeatures2(pd, &f2);
    if (!f2.features.fragmentStoresAndAtomics || !f2.features.shaderStorageImageExtendedFormats ||
        !f13.dynamicRendering || !f13.synchronization2) {
        return c;
    }
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(pd, VK_FORMAT_R16_UINT, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
        return c;
    }
    if (vk.surface != VK_NULL_HANDLE && !hasDeviceExtension(pd, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        return c;
    }
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, NULL);
    VkQueueFamilyProperties* qf = calloc(n, sizeof(*qf));
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, qf);
    for (uint32_t i = 0; i < n && !c.usable; i++) {
        if (!(qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            continue;
        }
        VkBool32 present = VK_TRUE;
        if (vk.surface != VK_NULL_HANDLE) {
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, vk.surface, &present);
        }
        if (present) {
            c.usable = 1;
            c.family = i;
            c.queues = qf[i].queueCount;
        }
    }
    free(qf);
    if (!c.usable) {
        return c;
    }
    c.interlock = il.fragmentShaderPixelInterlock && hasDeviceExtension(pd, VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME);
    c.score = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4
               : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1) * 2 + c.interlock;
    return c;
}

static int createDevice(void) {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(vk.instance, &n, NULL);
    if (n == 0) {
        gpuLog("vulkan: no devices");
        return 0;
    }
    VkPhysicalDevice* pds = calloc(n, sizeof(*pds));
    vkEnumeratePhysicalDevices(vk.instance, &n, pds);
    /* XG_VK_DEVICE: a device's number, or part of its name ("intel",
     * "nvidia"; the first usable one that has it). A name used to count as
     * device 0 (atoi), so "XG_VK_DEVICE=Intel" ran on the NVIDIA GPU here
     * unnoticed (xg-htn.35.74). A device asked for and not found is an error,
     * never another device. */
    const char* pick = getenv("XG_VK_DEVICE");
    if (pick != NULL && pick[0] == '\0') {
        pick = NULL;
    }
    int pickNumber = pick != NULL && strspn(pick, "0123456789") == strlen(pick);
    int best = -1;
    DeviceChoice bestChoice = {0};
    for (uint32_t i = 0; i < n; i++) {
        DeviceChoice c = rateDevice(pds[i]);
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pds[i], &props);
        gpuLog("vulkan: device %u: %s%s%s", i, props.deviceName, c.usable ? "" : " (unusable)",
               c.interlock ? " (interlock)" : "");
        if (!c.usable) {
            continue;
        }
        int picked = pick == NULL ? c.score > bestChoice.score
                     : pickNumber ? (uint32_t)atoi(pick) == i
                                  : best < 0 && SDL_strcasestr(props.deviceName, pick) != NULL;
        if (picked) {
            best = (int)i;
            bestChoice = c;
        }
    }
    if (best < 0) {
        free(pds);
        if (pick != NULL) {
            gpuLog("vulkan: XG_VK_DEVICE=%s: no usable device of that %s", pick, pickNumber ? "number" : "name");
        } else {
            gpuLog("vulkan: no usable device (needs Vulkan 1.3, R16_UINT storage images)");
        }
        return 0;
    }
    vk.physical = pds[best];
    free(pds);
    vk.queueFamily = bestChoice.family;
    hiresDeviceDefaults(); /* the Auto GPU budget's default depends on the device's kind */
    vk.interlock = bestChoice.interlock && !envFlag("XG_VK_NO_INTERLOCK");
    vkGetPhysicalDeviceMemoryProperties(vk.physical, &vk.memProps);
    vk.memoryBudget = vkGetPhysicalDeviceMemoryProperties2 != NULL && !envFlag("XG_VK_NO_MEMORY_BUDGET") &&
                      hasDeviceExtension(vk.physical, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

    /* A second queue where there is one: stall reports read VRAM through it
     * from another thread (gvkRescueRead). */
    float priorities[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = vk.queueFamily;
    qi.queueCount = bestChoice.queues >= 4 ? 4 : bestChoice.queues >= 3 ? 3 : bestChoice.queues >= 2 ? 2 : 1;
    qi.pQueuePriorities = priorities;

    VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT il = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT};
    il.fragmentShaderPixelInterlock = VK_TRUE;
    VkPhysicalDeviceVulkan13Features f13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    f13.pNext = vk.interlock ? &il : NULL;
    VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f13;
    f2.features.fragmentStoresAndAtomics = VK_TRUE;
    f2.features.shaderStorageImageExtendedFormats = VK_TRUE;

#if defined(VK_EXT_present_timing) && defined(VK_KHR_present_id2) && !defined(XG_TEST_NO_PRESENT_TIMING)
    timingEnabled=0;
    VkPhysicalDevicePresentTimingFeaturesEXT tf={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT};
    VkPhysicalDevicePresentId2FeaturesKHR idf={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR,.pNext=&tf};
    if(getenv("XG_PRESENT_TIMING_LOG") && vk.surface && hasDeviceExtension(vk.physical,VK_EXT_PRESENT_TIMING_EXTENSION_NAME) &&
        hasDeviceExtension(vk.physical,VK_KHR_PRESENT_ID_2_EXTENSION_NAME) && hasDeviceExtension(vk.physical,VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)) {
        VkPhysicalDeviceFeatures2 query={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&idf};vkGetPhysicalDeviceFeatures2(vk.physical,&query);
        timingEnabled=tf.presentTiming&&idf.presentId2;
        tf.presentAtAbsoluteTime=tf.presentAtRelativeTime=0;
        if(timingEnabled){tf.pNext=f13.pNext;f13.pNext=&idf;}
    }
#endif
    const char* exts[8];
    uint32_t extCount = 0;
    if (vk.surface != VK_NULL_HANDLE) {
        exts[extCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    }
    if (vk.interlock) {
        exts[extCount++] = VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME;
    }
    if (vk.memoryBudget) {
        exts[extCount++] = VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;
    }
#if defined(VK_EXT_present_timing) && defined(VK_KHR_present_id2) && !defined(XG_TEST_NO_PRESENT_TIMING)
    if(timingEnabled) {
        exts[extCount++]=VK_EXT_PRESENT_TIMING_EXTENSION_NAME;
        exts[extCount++]=VK_KHR_PRESENT_ID_2_EXTENSION_NAME;
        exts[extCount++]=VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME;
    }
#endif
    vk.copyFamily=vk.queueFamily;
    uint32_t familyCount=0;vkGetPhysicalDeviceQueueFamilyProperties(vk.physical,&familyCount,NULL);
    VkQueueFamilyProperties* families=calloc(familyCount,sizeof(*families));
    vkGetPhysicalDeviceQueueFamilyProperties(vk.physical,&familyCount,families);
    for(uint32_t i=0;i<familyCount;i++) if((families[i].queueFlags&VK_QUEUE_TRANSFER_BIT) &&
        !(families[i].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))) { vk.copyFamily=i;break; }
    VkBool32 transferPresent=VK_FALSE;
    if(vk.copyFamily!=vk.queueFamily && vk.surface && families[vk.copyFamily].queueCount>=2)
        vkGetPhysicalDeviceSurfaceSupportKHR(vk.physical,vk.copyFamily,vk.surface,&transferPresent);
    free(families);
    float transferPriorities[2]={1,1};
    VkDeviceQueueCreateInfo queues[2]={qi,{.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=vk.copyFamily,.queueCount=transferPresent?2:1,.pQueuePriorities=transferPriorities}};
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.pNext = &f2;
    di.queueCreateInfoCount = vk.copyFamily==vk.queueFamily?1:2;
    di.pQueueCreateInfos = queues;
    di.enabledExtensionCount = extCount;
    di.ppEnabledExtensionNames = exts;
    VkResult r = vkCreateDevice(vk.physical, &di, NULL, &vk.device);
    if (r != VK_SUCCESS) {
        gpuLog("vulkan: vkCreateDevice failed (%d)", (int)r);
        return 0;
    }
#define VK_LOAD_DEVICE(name) name = (PFN_##name)vkGetDeviceProcAddr(vk.device, #name);
    VK_DEVICE_FUNCS(VK_LOAD_DEVICE)
#if defined(VK_EXT_present_timing) && defined(VK_KHR_present_id2) && !defined(XG_TEST_NO_PRESENT_TIMING)
    timingCapabilities=(PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR)vkGetInstanceProcAddr(vk.instance,"vkGetPhysicalDeviceSurfaceCapabilities2KHR");
    timingDomains=(PFN_vkGetSwapchainTimeDomainPropertiesEXT)vkGetDeviceProcAddr(vk.device,"vkGetSwapchainTimeDomainPropertiesEXT");
    timingQueueSize=(PFN_vkSetSwapchainPresentTimingQueueSizeEXT)vkGetDeviceProcAddr(vk.device,"vkSetSwapchainPresentTimingQueueSizeEXT");
    timingPast=(PFN_vkGetPastPresentationTimingEXT)vkGetDeviceProcAddr(vk.device,"vkGetPastPresentationTimingEXT");
#endif
    vkGetDeviceQueue(vk.device, vk.queueFamily, 0, &vk.queue);
    if (qi.queueCount >= 3) vkGetDeviceQueue(vk.device, vk.queueFamily, 2, &vk.presentQueue);
    if(vk.copyFamily!=vk.queueFamily) {
        vkGetDeviceQueue(vk.device,vk.copyFamily,0,&vk.copyQueue);
        if(transferPresent)vkGetDeviceQueue(vk.device,vk.copyFamily,1,&vk.presentQueue);
    } else if (qi.queueCount >= 4) vkGetDeviceQueue(vk.device, vk.queueFamily, 3, &vk.copyQueue);
    gpuLog("vulkan: graphics family %u (%u queues), copy family %u, transfer presentation %d",vk.queueFamily,qi.queueCount,vk.copyFamily,transferPresent);
    if (qi.queueCount >= 2) {
        vkGetDeviceQueue(vk.device, vk.queueFamily, 1, &vk.rescueQueue);
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vk.physical, &props);
    gpuLog("vulkan: using %s, %s", props.deviceName,
           vk.interlock ? "fragment shader interlock" : "no interlock (batches split on overlap)");
    vk.limits = props.limits;
    const char* timing = getenv("XG_GPU_TIMING");
    { /* Cap timing can be enabled later in F1, independently of diagnostics. */
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(vk.physical, &nq, NULL);
        VkQueueFamilyProperties* qf = calloc(nq, sizeof(*qf));
        vkGetPhysicalDeviceQueueFamilyProperties(vk.physical, &nq, qf);
        uint32_t bits = qf[vk.queueFamily].timestampValidBits;
        free(qf);
        if (bits == 0 || props.limits.timestampPeriod <= 0) {
            if (timing != NULL && timing[0] != '\0' && strcmp(timing, "0") != 0)
                gpuLog("vulkan: GPU timing unavailable on this queue");
        } else {
            if (timing != NULL && timing[0] != '\0' && strcmp(timing, "0") != 0)
                gt.on = strcmp(timing, "summary") == 0 ? 2 : 1;
            gt.nsPerTick = props.limits.timestampPeriod;
            gt.mask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
            autoTime.available = 1;
        }
    }
    return 1;
}

/* ---- pipelines ------------------------------------------------------------------ */

/* The set's bindings: 0 VRAM, 1 the span buffer, 2 the wide target, then the
 * hi-res copy's (hires.c): 3 its VRAM, 4 its wide target, 5 VRAM's scratch
 * image, 6 its own scratch, 7 the StoreImage stash. */
#define SET_BINDINGS 8
/* ... and 8: the texture pack images kept on the GPU (resPlace). */
#define RESIDENT_BINDING 8

static void resWriteSet(FrameCtx* f);
static void resFlush(FrameCtx* f);
static int deviceBuffer(VkBuffer* b, VkDeviceMemory* m, VkDeviceSize size, VkBufferUsageFlags usage);

static void createLayouts(void) {
    VkDescriptorSetLayoutBinding b[SET_BINDINGS + 1] = {{0}};
    b[RESIDENT_BINDING].binding = RESIDENT_BINDING;
    b[RESIDENT_BINDING].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b[RESIDENT_BINDING].descriptorCount = 1;
    b[RESIDENT_BINDING].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    for (int i = 3; i < SET_BINDINGS; i++) {
        b[i].binding = (uint32_t)i;
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | (i<=4?VK_SHADER_STAGE_COMPUTE_BIT:0);
    }
    b[2].binding = 2; /* the wide target */
    b[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[2].descriptorCount = 1;
    b[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    b[0].binding = 0;
    b[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    b[0].descriptorCount = 1;
    b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    b[1].binding = 1;
    b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b[1].descriptorCount = 1;
    b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = SET_BINDINGS + 1;
    li.pBindings = b;
    VK_CHECK(vkCreateDescriptorSetLayout(vk.device, &li, NULL, &vk.setLayout));
    /* Set 1: a movie pack's picture (present.frag's src 4). */
    VkDescriptorSetLayoutBinding mb = {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    li.bindingCount = 1;
    li.pBindings = &mb;
    VK_CHECK(vkCreateDescriptorSetLayout(vk.device, &li, NULL, &vk.movieSetLayout));

    VkDescriptorSetLayoutBinding ob[2] = {
        {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT,NULL},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
    li.bindingCount=2;li.pBindings=ob;
    VK_CHECK(vkCreateDescriptorSetLayout(vk.device,&li,NULL,&vk.orderedLayout));
    VkPushConstantRange pr = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PresentConstants)};
    VkDescriptorSetLayout sets[3] = {vk.setLayout, vk.movieSetLayout, vk.orderedLayout};
    VkPipelineLayoutCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pi.setLayoutCount = 3;
    pi.pSetLayouts = sets;
    pi.pushConstantRangeCount = 1;
    pi.pPushConstantRanges = &pr;
    VK_CHECK(vkCreatePipelineLayout(vk.device, &pi, NULL, &vk.pipelineLayout));
    /* Hi-res passes use the same three layouts and their own constants.
     * Utility set 1 holds the effect source instead of a movie. Keeping it
     * separate stays within the baseline eight fragment storage images. */
    VkPushConstantRange hr = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64};
    pi.setLayoutCount = 3;
    pi.pPushConstantRanges = &hr;
    VK_CHECK(vkCreatePipelineLayout(vk.device, &pi, NULL, &vk.hiLayout));

    VkDescriptorSetLayout resolveSets[2]={vk.setLayout,vk.orderedLayout};
    pi.setLayoutCount=2;pi.pSetLayouts=resolveSets;pi.pushConstantRangeCount=0;
    VK_CHECK(vkCreatePipelineLayout(vk.device,&pi,NULL,&vk.orderedResolveLayout));

    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, (SET_BINDINGS - 1) * FRAMES},
                                  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * FRAMES}};
    VkDescriptorPoolCreateInfo dpi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = FRAMES;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = ps;
    VK_CHECK(vkCreateDescriptorPool(vk.device, &dpi, NULL, &vk.descriptorPool));
    VkDescriptorPoolSize mps = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, FRAMES + 1};
    dpi.maxSets = FRAMES + 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &mps;
    VK_CHECK(vkCreateDescriptorPool(vk.device, &dpi, NULL, &vk.moviePool));
    VkDescriptorSetAllocateInfo effect = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    effect.descriptorPool=vk.moviePool; effect.descriptorSetCount=1; effect.pSetLayouts=&vk.movieSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(vk.device,&effect,&vk.effectSet));
    VkDescriptorPoolSize ips[2] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, SET_BINDINGS * FRAMES * (INTERP_SETS)},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * FRAMES * (INTERP_SETS)}};
    dpi.maxSets = 2 * FRAMES * (INTERP_SETS);
    dpi.poolSizeCount = 2; dpi.pPoolSizes = ips;
    VK_CHECK(vkCreateDescriptorPool(vk.device, &dpi, NULL, &vk.interpolationPool));
}

/* A frame context's set 1: its movie picture, or the 1x1 stand-in. Only
 * when no submitted work of this context is pending and its command buffer
 * has not bound the set since it began. */
static void writeMovieSet(FrameCtx* f) {
    VkDescriptorImageInfo ii = {VK_NULL_HANDLE, f->movieView != VK_NULL_HANDLE ? f->movieView : vk.dummy32View,
                                VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = f->movieSet;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(vk.device, 1, &w, 0, NULL);
}

/* The hi-res copy's bindings of a frame context's set (3-7): its images, or a
 * 1x1 stand-in where one is not made. Only while the device is idle. */
static void writeHiSet(FrameCtx* f) {
    VkImageView views[SET_BINDINGS - 3] = {vk.hiVramView, vk.hiWideView, vk.scratchView, vk.hiScratchView,
                                           vk.hiStashView};
    VkDescriptorImageInfo ii[SET_BINDINGS - 3];
    VkWriteDescriptorSet w[SET_BINDINGS - 3];
    for (int i = 0; i < SET_BINDINGS - 3; i++) {
        ii[i] = (VkDescriptorImageInfo){VK_NULL_HANDLE, views[i] != VK_NULL_HANDLE ? views[i] : vk.dummy32View,
                                        VK_IMAGE_LAYOUT_GENERAL};
        w[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[i].dstSet = f->set;
        w[i].dstBinding = (uint32_t)(3 + i);
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[i].pImageInfo = &ii[i];
    }
    vkUpdateDescriptorSets(vk.device, SET_BINDINGS - 3, w, 0, NULL);

}

/* Each frame context's set: VRAM, that context's span buffer, the wide target. */
static void createSet(FrameCtx* f) {
    VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = vk.descriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &vk.setLayout;
    VK_CHECK(vkAllocateDescriptorSets(vk.device, &ai, &f->set));
    VkDescriptorImageInfo ii = {VK_NULL_HANDLE, vk.vramView, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorBufferInfo bi = {f->spans.buffer, 0, VK_WHOLE_SIZE};
    VkDescriptorImageInfo wi = {VK_NULL_HANDLE, vk.wideView, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w[3] = {{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                 {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                 {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    w[2].dstSet = f->set;
    w[2].dstBinding = 2;
    w[2].descriptorCount = 1;
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[2].pImageInfo = &wi;
    w[0].dstSet = f->set;
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[0].pImageInfo = &ii;
    w[1].dstSet = f->set;
    w[1].dstBinding = 1;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &bi;
    vkUpdateDescriptorSets(vk.device, 3, w, 0, NULL);
    writeHiSet(f);
    resWriteSet(f);
}

static VkPipeline createPipeline(const uint32_t* vs, size_t vsSize, const uint32_t* fs, size_t fsSize, int draw,
                                 VkFormat colorFormat) {
    int optional = draw >= 3;
    if(optional)draw-=2;
    VkShaderModule vm = shaderModule(vs, vsSize);
    VkShaderModule fm = shaderModule(fs, fsSize);
    VkPipelineShaderStageCreateInfo stages[2] = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                 {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vm;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fm;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {0, sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[11] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuVertex, x)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuVertex, u)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, r)},
        {3, 0, VK_FORMAT_R32_UINT, offsetof(GpuVertex, mode)},
        {4, 0, VK_FORMAT_R32_UINT, offsetof(GpuVertex, tex)},
        {5, 0, VK_FORMAT_R32_UINT, offsetof(GpuVertex, texWindow)},
        {6, 0, VK_FORMAT_R32_UINT, offsetof(GpuVertex, clipMin)},
        {7, 0, VK_FORMAT_R32_UINT, offsetof(GpuVertex, clipMax)},
        {8, 0, VK_FORMAT_R32_UINT, offsetof(GpuVertex, span)},
        {9, 0, VK_FORMAT_R32_UINT, offsetof(GpuVertex, wideTex)},
        {10, 0, VK_FORMAT_R32_UINT, offsetof(GpuVertex, wideEdge)},
    };
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (draw) {
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = draw == 2 ? 11 : 9; /* 2: the wide pass (wideTex, wideEdge) */
        vi.pVertexAttributeDescriptions = attrs;
    }
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState att = {0};
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                         VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = draw ? 0 : 1;
    cb.pAttachments = &att;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = draw ? 0 : 1;
    ri.pColorAttachmentFormats = &colorFormat;

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
    gi.layout = vk.pipelineLayout;
    VkPipeline p = VK_NULL_HANDLE;
    VkResult result=vkCreateGraphicsPipelines(vk.device,VK_NULL_HANDLE,1,&gi,NULL,&p);
    if(!optional) { VK_CHECK(result); }
    else if(result!=VK_SUCCESS) {
        if(p!=VK_NULL_HANDLE)vkDestroyPipeline(vk.device,p,NULL);
        p=VK_NULL_HANDLE;
    }
    vkDestroyShaderModule(vk.device, vm, NULL);
    vkDestroyShaderModule(vk.device, fm, NULL);
    return p;
}

/* The hi-res copy's pipelines: its triangles (HiVertex; vertices 1, 2 for
 * the wide target's pass with its wide fields), or one pass of
 * hires_util.frag (vertices 0: no vertex input). */
static VkPipeline createHiPipeline(const uint32_t* vs, size_t vsSize, const uint32_t* fs, size_t fsSize, int vertices) {
    int optional = vertices >= 3;
    if(optional)vertices-=2;
    VkShaderModule vm = shaderModule(vs, vsSize);
    VkShaderModule fm = shaderModule(fs, fsSize);
    VkPipelineShaderStageCreateInfo stages[2] = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                 {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vm;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fm;
    stages[1].pName = "main";
    VkVertexInputBindingDescription binding = {0, sizeof(HiVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[15] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(HiVertex, x)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(HiVertex, u)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(HiVertex, r)},
        {3, 0, VK_FORMAT_R32_UINT, offsetof(HiVertex, mode)},
        {4, 0, VK_FORMAT_R32_UINT, offsetof(HiVertex, tex)},
        {5, 0, VK_FORMAT_R32_UINT, offsetof(HiVertex, texWindow)},
        {6, 0, VK_FORMAT_R32_UINT, offsetof(HiVertex, clipMin)},
        {7, 0, VK_FORMAT_R32_UINT, offsetof(HiVertex, clipMax)},
        {8, 0, VK_FORMAT_R32_UINT, offsetof(HiVertex, uvMin)},
        {9, 0, VK_FORMAT_R32_UINT, offsetof(HiVertex, uvMax)},
        {10, 0, VK_FORMAT_R32_UINT, offsetof(HiVertex, wideTex)},
        {11, 0, VK_FORMAT_R32_UINT, offsetof(HiVertex, wideEdge)},
        {12, 0, VK_FORMAT_R32_SFLOAT, offsetof(HiVertex, w)},
        {13, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(HiVertex, sample)},
        {14, 0, VK_FORMAT_R32_UINT, offsetof(HiVertex, rasterShift)},
    };
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (vertices) {
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = 15;
        vi.pVertexAttributeDescriptions = attrs;
    }
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
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
    gi.layout = vk.hiLayout;
    VkPipeline p = VK_NULL_HANDLE;
    VkResult result=vkCreateGraphicsPipelines(vk.device,VK_NULL_HANDLE,1,&gi,NULL,&p);
    if(!optional) { VK_CHECK(result); }
    else if(result!=VK_SUCCESS) {
        if(p!=VK_NULL_HANDLE)vkDestroyPipeline(vk.device,p,NULL);
        p=VK_NULL_HANDLE;
    }
    vkDestroyShaderModule(vk.device, vm, NULL);
    vkDestroyShaderModule(vk.device, fm, NULL);
    return p;
}

/* ---- swapchain ---------------------------------------------------------------- */

static void destroySwapchainViews(void) {
    for (uint32_t i = 0; i < vk.swapchainImageCount; i++) {
        vkDestroyImageView(vk.device, vk.swapchainViews[i], NULL);
        vkDestroySemaphore(vk.device, vk.presented[i], NULL);
    }
    vk.swapchainImageCount = 0;
}

/* port/src/system/displaylock.c (weak: the GPU tests have no system layer):
 * the vblank locked to the display presents FIFO. */
extern int displayLockFifo(void) __attribute__((weak));

/* displaylock.c turned the lock on or off: a new swapchain at the next picture. */
void gvkPresentModeChanged(void) {
    vk.swapchainStale = 1;
}

static int bufferedPresentation(void);
static VkPresentModeKHR choosePresentMode(void) {
    VkPresentModeKHR modes[8];
    uint32_t n = 8;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &n, modes);
    static int logged;
    if (!logged) {
        logged = 1;
        char list[128] = "";
        for (uint32_t i = 0; i < n; i++) {
            static const char* const names[] = {"immediate", "mailbox", "fifo", "fifo-relaxed"};
            size_t len = strlen(list);
            snprintf(list + len, sizeof(list) - len, "%s%s", i ? " " : "",
                     modes[i] < 4 ? names[modes[i]] : "other");
        }
        gpuLog("vulkan: present modes: %s", list);
    }
    if (bufferedPresentation() || envFlag("XG_VK_FIFO") || (displayLockFifo != NULL && displayLockFifo())) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    /* The game's own vblank paces the frames: presenting must not block it. */
    static const VkPresentModeKHR order[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR};
    for (size_t o = 0; o < 2; o++) {
        for (uint32_t i = 0; i < n; i++) {
            if (modes[i] == order[o]) {
                return order[o];
            }
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static const char* resultName(int r) {
    switch (r) {
    case PRESENT_NOT_TRIED:
        return "-";
    case VK_SUCCESS:
        return "ok";
    case VK_NOT_READY:
        return "not-ready";
    case VK_TIMEOUT:
        return "timeout";
    case VK_SUBOPTIMAL_KHR:
        return "suboptimal";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "out-of-date";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "surface-lost";
    case VK_ERROR_DEVICE_LOST:
        return "device-lost";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "out-of-host-memory";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "out-of-device-memory";
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
        return "fullscreen-exclusive-lost";
    default:
        return "error";
    }
}

/* A failure the present path recovers from by itself: said once per call and
 * result, not at every vblank. */
static void presentFailed(const char* call, int r) {
    static struct {
        const char* call;
        int r;
    } seen[16];
    static int seenCount;
    for (int i = 0; i < seenCount; i++) {
        if (seen[i].call == call && seen[i].r == r) {
            return;
        }
    }
    if (seenCount < 16) {
        seen[seenCount].call = call;
        seen[seenCount].r = r;
        seenCount++;
    }
    gpuLog("vulkan: %s: %s (%d); making the swapchain again", call, resultName(r), r);
}

/* Some WSI drivers wait inside QueuePresent even in MAILBOX mode. Keep
 * that wait off the game/recording thread. The present queue is owned
 * exclusively by this worker. Only final-picture copy jobs run here: the
 * worker then makes every acquire and present of the swapchain, which the
 * spec requires to be host-synchronised, and the game thread drains it
 * before touching the swapchain itself (acquireImage, recreation, timing
 * queries). A direct picture is acquired by the game thread, so it is also
 * presented there, as with Speedup off. Every resource lifetime change
 * drains jobs. */
typedef struct PresentCopy PresentCopy;
static VkResult presentCopyWorker(PresentCopy*,VkSwapchainKHR,uint32_t*,uint64_t);
typedef struct {
    VkSwapchainKHR swapchain;
    uint32_t image;
    VkResult result;
    uint64_t timing;PresentCopy* copy;
} PresentJob;
static struct {
    SDL_Mutex* lock;
    SDL_Condition* changed;
    SDL_Thread* thread;
    PresentJob jobs[MAX_SWAPCHAIN_IMAGES];
    uint64_t queued, started, done, collected;
    int stop, disabled, lost;
} asyncPresent;

static int presentWorker(void* unused) {
    (void)unused;
    SDL_LockMutex(asyncPresent.lock);
    for (;;) {
        while(asyncPresent.started==asyncPresent.queued && !asyncPresent.stop)
            SDL_WaitCondition(asyncPresent.changed,asyncPresent.lock);
        if(asyncPresent.started==asyncPresent.queued && asyncPresent.stop)break;
        unsigned slot=(unsigned)(asyncPresent.started++%MAX_SWAPCHAIN_IMAGES);
        PresentJob job=asyncPresent.jobs[slot];
        SDL_UnlockMutex(asyncPresent.lock);
        VkResult result=presentCopyWorker(job.copy,job.swapchain,&job.image,job.timing);
        timingCollect(); /* the swapchain is this thread's until the job is done */
        SDL_LockMutex(asyncPresent.lock);
        asyncPresent.jobs[slot].result=result;asyncPresent.jobs[slot].image=job.image;++asyncPresent.done;
        SDL_BroadcastCondition(asyncPresent.changed);
    }
    SDL_UnlockMutex(asyncPresent.lock);return 0;
}
static void presentResult(VkResult result,uint32_t image) {
    if(result==VK_TIMEOUT || result==VK_NOT_READY) {
        /* The copy path's acquire found no free image: skip this picture,
         * as acquireImage does, rather than remake a working swapchain.
         * The stall guard still sees it (presentPicture). */
        vk.lastAcquire=result;vk.framesSkipped++;vk.copySkipped=1;vk.held.valid=0;
        return;
    }
    vk.copySkipped=0;
    vk.lastPresent=result;vk.lastImage=image;
    if(result==VK_SUCCESS || result==VK_SUBOPTIMAL_KHR)++vk.framesShown;
    if(result==VK_ERROR_SURFACE_LOST_KHR)asyncPresent.lost=1;
    else if(result!=VK_SUCCESS) {
        vk.swapchainStale=1;
        if(result!=VK_ERROR_OUT_OF_DATE_KHR && result!=VK_SUBOPTIMAL_KHR)
            presentFailed("vkQueuePresentKHR",result);
    }
    if(result!=VK_SUCCESS && result!=VK_SUBOPTIMAL_KHR)vk.held.valid=0;
}
static void presentCollect(void) {
    if(!asyncPresent.thread) { timingCollect();return; }
    int idle=0;
    for (;;) {
        SDL_LockMutex(asyncPresent.lock);
        if(asyncPresent.collected==asyncPresent.done) {
            idle=asyncPresent.done==asyncPresent.queued;SDL_UnlockMutex(asyncPresent.lock);break;
        }
        PresentJob job=asyncPresent.jobs[asyncPresent.collected++%MAX_SWAPCHAIN_IMAGES];
        SDL_UnlockMutex(asyncPresent.lock);presentResult(job.result,job.image);
    }
    /* Only this thread queues jobs: an idle worker stays idle until then. */
    if(idle)timingCollect();
}
static void presentDrain(void) {
    if(!asyncPresent.thread)return;
    SDL_LockMutex(asyncPresent.lock);
    while(asyncPresent.done!=asyncPresent.queued)SDL_WaitCondition(asyncPresent.changed,asyncPresent.lock);
    SDL_UnlockMutex(asyncPresent.lock);presentCollect();
}
static int presentEnqueue(uint64_t timing,PresentCopy* copy) {
    if(!copy || !vk.presentQueue || !hostPresent.realtime || !speedupEnabled() ||
        vk.env.presentSync || asyncPresent.disabled)return 0;
    if(!asyncPresent.thread) {
        asyncPresent.lock=SDL_CreateMutex();asyncPresent.changed=SDL_CreateCondition();
        if(asyncPresent.lock && asyncPresent.changed)
            asyncPresent.thread=SDL_CreateThread(presentWorker,"Vulkan present",NULL);
        if(!asyncPresent.thread) {
            SDL_DestroyCondition(asyncPresent.changed);SDL_DestroyMutex(asyncPresent.lock);
            asyncPresent.lock=NULL;asyncPresent.changed=NULL;asyncPresent.disabled=1;
            gpuWarnOnce("present-thread","vulkan: presentation worker unavailable; using synchronous presentation");return 0;
        }
    }
    presentCollect();SDL_LockMutex(asyncPresent.lock);
    while(asyncPresent.queued-asyncPresent.collected>=MAX_SWAPCHAIN_IMAGES) {
        SDL_UnlockMutex(asyncPresent.lock);presentDrain();SDL_LockMutex(asyncPresent.lock);
    }
    asyncPresent.jobs[asyncPresent.queued++%MAX_SWAPCHAIN_IMAGES]=(PresentJob){vk.swapchain,0,VK_NOT_READY,timing,copy};
    SDL_SignalCondition(asyncPresent.changed);SDL_UnlockMutex(asyncPresent.lock);return 1;
}
static void presentShutdown(void) {
    if(asyncPresent.thread) {
        presentDrain();SDL_LockMutex(asyncPresent.lock);asyncPresent.stop=1;
        SDL_SignalCondition(asyncPresent.changed);SDL_UnlockMutex(asyncPresent.lock);
        SDL_WaitThread(asyncPresent.thread,NULL);
        SDL_DestroyCondition(asyncPresent.changed);SDL_DestroyMutex(asyncPresent.lock);
    }
    memset(&asyncPresent,0,sizeof(asyncPresent));
}

/* The primary queue renders a complete final picture without waiting for
 * an acquired display image. A separate queue waits and copies that picture
 * byte-for-byte into the swapchain. Each slot owns its semaphores and image
 * until its copy fence completes; none of those waits owns a game context. */
struct PresentCopy {
    VkImage image;VkDeviceMemory memory;VkImageView view;
    VkSemaphore acquired, rendered;VkCommandBuffer cmd;VkFence fence;
    int w,h,submitted;VkFormat format;uint64_t ticket;HostBuffer samples;size_t sampleBytes;
};
#define PRESENT_COPY_SLOTS 4
static struct { VkCommandPool pool;PresentCopy slots[PRESENT_COPY_SLOTS];int disabled,limit; } copyPresent;
static void destroyImage(VkImage*,VkDeviceMemory*,VkImageView*);
static int presentCopyWanted(void) {
    return vk.copyPresent && vk.presentQueue && !asyncPresent.disabled && !vk.env.presentSync && vk.swapchain && vk.surface && !vk.surfaceLost && hostPresent.realtime && speedupEnabled() && !copyPresent.disabled &&
        !vk.env.directPresent;
}
static int bufferedPresentation(void) {
    return vk.copyPresent && vk.presentQueue && hostPresent.realtime && speedupEnabled() && interpolationEnabled() &&
        presentationRate()>0 && !asyncPresent.disabled && !copyPresent.disabled && !vk.env.presentSync && !vk.env.directPresent;
}
static PresentCopy* presentCopySlot(void) {
    if(!presentCopyWanted())return NULL;
    if(!copyPresent.pool) {
        VkCommandPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=vk.copyFamily};
        VK_CHECK(vkCreateCommandPool(vk.device,&pool,NULL,&copyPresent.pool));
        VkCommandBuffer commands[PRESENT_COPY_SLOTS];
        VkCommandBufferAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=copyPresent.pool,
            .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=PRESENT_COPY_SLOTS};
        VK_CHECK(vkAllocateCommandBuffers(vk.device,&alloc,commands));
        for(unsigned i=0;i<PRESENT_COPY_SLOTS;i++) {
            PresentCopy* p=&copyPresent.slots[i];p->cmd=commands[i];
            VkFenceCreateInfo fence={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,.flags=VK_FENCE_CREATE_SIGNALED_BIT};
            VK_CHECK(vkCreateFence(vk.device,&fence,NULL,&p->fence));
            VkSemaphoreCreateInfo sem={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VK_CHECK(vkCreateSemaphore(vk.device,&sem,NULL,&p->acquired));VK_CHECK(vkCreateSemaphore(vk.device,&sem,NULL,&p->rendered));
        }
    }
    for(unsigned i=0;i<(unsigned)(copyPresent.limit?copyPresent.limit:PRESENT_COPY_SLOTS);i++) {
        PresentCopy* p=&copyPresent.slots[i];
        if(p->ticket && asyncPresent.collected<p->ticket)continue;
        if(p->submitted && vkGetFenceStatus(vk.device,p->fence)!=VK_SUCCESS)continue;
        p->submitted=0;
        return p;
    }
    return NULL;
}
static int presentCopyImage(PresentCopy* p,int w,int h) {
    if(p->image && p->w==w && p->h==h && p->format==vk.swapchainFormat)return 1;
    destroyImage(&p->image,&p->memory,&p->view);
    VkImageCreateInfo image={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=vk.swapchainFormat,.extent={w,h,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    uint32_t families[2]={vk.queueFamily,vk.copyFamily};
    if(vk.queueFamily!=vk.copyFamily) { image.sharingMode=VK_SHARING_MODE_CONCURRENT;image.queueFamilyIndexCount=2;image.pQueueFamilyIndices=families; }
    const char* fault=getenv("XG_TEST_PRESENT_COPY_ALLOC_FAIL");
    int failAt=fault?atoi(fault):0;
    if((failAt>0 && p-copyPresent.slots>=failAt-1) || vkCreateImage(vk.device,&image,NULL,&p->image)!=VK_SUCCESS)goto failed;
    VkMemoryRequirements req;vkGetImageMemoryRequirements(vk.device,p->image,&req);
    VkMemoryAllocateInfo alloc={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,
        .memoryTypeIndex=findMemory(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0)};
    if(vkAllocateMemory(vk.device,&alloc,NULL,&p->memory)!=VK_SUCCESS)goto failed;
    if(vkBindImageMemory(vk.device,p->image,p->memory,0)!=VK_SUCCESS)goto failed;
    VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=p->image,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=vk.swapchainFormat,.subresourceRange=colorRange};
    if(vkCreateImageView(vk.device,&view,NULL,&p->view)!=VK_SUCCESS)goto failed;
    p->w=w;p->h=h;p->format=vk.swapchainFormat;
    destroyHostBuffer(&p->samples);p->sampleBytes=0;
    if(timingEnabled) {
        p->sampleBytes=(size_t)w*16*(h<2?h:2)*4;
        createHostBuffer(&p->samples,p->sampleBytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,1);
    }
    return 1;
failed:
    destroyImage(&p->image,&p->memory,&p->view);
    int prefix=(int)(p-copyPresent.slots);
    if(prefix>0 && copyPresent.slots[0].image && copyPresent.slots[0].w==w && copyPresent.slots[0].h==h &&
        copyPresent.slots[0].format==vk.swapchainFormat) {
        copyPresent.limit=prefix;
        gpuWarnOnce("present-copy-limit","vulkan: using %d buffered pictures under graphics-memory pressure",prefix);
        return -1;
    }
    /* No picture of this size fits. Retire the old pool before taking the
     * direct path; disabled buffering must not retain its device images. */
    presentDrain();VK_CHECK(vkDeviceWaitIdle(vk.device));
    for(unsigned i=0;i<PRESENT_COPY_SLOTS;i++) {
        PresentCopy* slot=&copyPresent.slots[i];destroyImage(&slot->image,&slot->memory,&slot->view);
        destroyHostBuffer(&slot->samples);slot->sampleBytes=0;
    }
    copyPresent.disabled=1;
    gpuWarnOnce("present-copy-memory","vulkan: final-picture buffering unavailable; using direct presentation");return 0;
}
static void presentCopySubmit(PresentCopy* p,uint32_t index) {
    VK_CHECK(vkResetCommandBuffer(p->cmd,0));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK_CHECK(vkBeginCommandBuffer(p->cmd,&begin));
    imageBarrier(p->cmd,vk.swapchainImages[index],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,0,VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageCopy region={.srcSubresource=colorLayers,.dstSubresource=colorLayers,.extent={p->w,p->h,1}};
    vkCmdCopyImage(p->cmd,p->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,vk.swapchainImages[index],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
    imageBarrier(p->cmd,vk.swapchainImages[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_2_NONE,0);
    if(p->samples.buffer) {
        VkBufferImageCopy regions[16];int rows=p->h<2?p->h:2;
        for(int i=0;i<16;i++)regions[i]=(VkBufferImageCopy){.bufferOffset=(VkDeviceSize)i*p->w*rows*4,
            .imageSubresource=colorLayers,.imageOffset={0,(p->h-rows)*(i+1)/17,0},.imageExtent={p->w,rows,1}};
        vkCmdCopyImageToBuffer(p->cmd,p->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,p->samples.buffer,16,regions);
        hostReadBarrier(p->cmd);
    }
    VK_CHECK(vkEndCommandBuffer(p->cmd));VK_CHECK(vkResetFences(vk.device,1,&p->fence));
    VkSemaphore waits[2]={p->acquired,p->rendered};
    VkPipelineStageFlags stages[2]={VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT};
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=2,.pWaitSemaphores=waits,
        .pWaitDstStageMask=stages,.commandBufferCount=1,.pCommandBuffers=&p->cmd,
        .signalSemaphoreCount=1,.pSignalSemaphores=&vk.presented[index]};
    VK_CHECK(vkQueueSubmit(vk.copyQueue,1,&submit,p->fence));p->submitted=1;
}
static VkResult presentCopyWorker(PresentCopy* p,VkSwapchainKHR swapchain,uint32_t* index,uint64_t timing) {
    VkResult r=vkAcquireNextImageKHR(vk.device,swapchain,UINT64_C(50000000),p->acquired,VK_NULL_HANDLE,index);
    if(r==VK_SUCCESS || r==VK_SUBOPTIMAL_KHR) {
        presentCopySubmit(p,*index);
        /* Present only once the picture and its copy are complete. The
         * binary semaphore still provides device visibility, but its wait
         * cannot occupy the present queue ahead of the rendering that must
         * signal it. */
        VkResult ready=vkWaitForFences(vk.device,1,&p->fence,VK_TRUE,UINT64_MAX);
        if(ready!=VK_SUCCESS)return ready;
#if defined(VK_EXT_present_timing) && defined(VK_KHR_present_id2) && !defined(XG_TEST_NO_PRESENT_TIMING)
        if(timing && p->samples.map) {
            uint64_t hash=UINT64_C(14695981039346656037);
            for(size_t i=0;i<p->sampleBytes;i++){hash^=p->samples.map[i];hash*=UINT64_C(1099511628211);}
            __atomic_store_n(&timingMeta[timing%4096].hash,hash,__ATOMIC_RELEASE);
        }
#endif
        VkPresentInfoKHR info={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.waitSemaphoreCount=1,
            .pWaitSemaphores=&vk.presented[*index],.swapchainCount=1,.pSwapchains=&swapchain,.pImageIndices=index};
        VkResult result=timingPresent(vk.presentQueue,&info,timing);
        return result==VK_SUCCESS && r==VK_SUBOPTIMAL_KHR?r:result;
    }
    VK_CHECK(vkResetFences(vk.device,1,&p->fence));
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo wait={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&p->rendered,.pWaitDstStageMask=&stage};
    VkResult consume=vkQueueSubmit(vk.copyQueue,1,&wait,p->fence);
    return consume==VK_SUCCESS?r:consume;
}
static void presentCopyShutdown(void) {
    for(unsigned i=0;i<PRESENT_COPY_SLOTS;i++) {
        PresentCopy* p=&copyPresent.slots[i];destroyImage(&p->image,&p->memory,&p->view);destroyHostBuffer(&p->samples);
        if(p->fence)vkDestroyFence(vk.device,p->fence,NULL);
        if(p->acquired)vkDestroySemaphore(vk.device,p->acquired,NULL);
        if(p->rendered)vkDestroySemaphore(vk.device,p->rendered,NULL);
    }
    if(copyPresent.pool)vkDestroyCommandPool(vk.device,copyPresent.pool,NULL);
    memset(&copyPresent,0,sizeof(copyPresent));
}

static void destroySwapchain(void) {
    destroySwapchainViews();
    if (vk.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vk.device, vk.swapchain, NULL);
        vk.swapchain = VK_NULL_HANDLE;
    }
}

/* VK_ERROR_SURFACE_LOST_KHR: the window's surface is gone; a new one (and a
 * swapchain) is made at the next retry time. */
static void loseSurface(const char* call) {
    presentFailed(call, VK_ERROR_SURFACE_LOST_KHR);
    presentDrain();
    vkDeviceWaitIdle(vk.device);
    for (int i = 0; i < FRAMES; i++) {
        vk.frames[i].submitted = 0;
    }
    destroySwapchain();
    if (vk.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
        vk.surface = VK_NULL_HANDLE;
    }
    vk.surfaceLost = 1;
    vk.swapchainRetryNs = SDL_GetTicksNS() + 250000000ull;
}

static int remakeSurface(void) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkBool32 ok = VK_FALSE;
    if (!SDL_Vulkan_CreateSurface(vk.window, vk.instance, NULL, &surface)) {
        gpuWarnOnce("surface", "vulkan: no window surface (%s); retrying each second", SDL_GetError());
        vk.swapchainRetryNs = SDL_GetTicksNS() + 1000000000ull;
        return 0;
    }
    vkGetPhysicalDeviceSurfaceSupportKHR(vk.physical, vk.queueFamily, surface, &ok);
    if (!ok) {
        gpuWarnOnce("surface support", "vulkan: the new window surface cannot be presented to; retrying each second");
        vkDestroySurfaceKHR(vk.instance, surface, NULL);
        vk.swapchainRetryNs = SDL_GetTicksNS() + 1000000000ull;
        return 0;
    }
    vk.surface = surface;
    vk.surfaceLost = 0;
    vk.swapchainStale = 1;
    return 1;
}

static int quarterOf(VkSurfaceTransformFlagBitsKHR t);         /* pre-rotation, below */
static void ensureRotation(int w, int h, VkFormat format);
static int createSwapchain(void) {
    VkSurfaceCapabilitiesKHR caps;
    VkResult cr = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, vk.surface, &caps);
    if (cr == VK_ERROR_SURFACE_LOST_KHR) {
        loseSurface("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        return 0;
    }
    if (cr != VK_SUCCESS) {
        gpuWarnOnce("caps", "vulkan: vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed (%d); retrying each second", (int)cr);
        destroySwapchain();
        vk.swapchainRetryNs = SDL_GetTicksNS() + 1000000000ull;
        return 0;
    }
    VkExtent2D extent = caps.currentExtent;
    SDL_GetWindowSizeInPixels(vk.window, &vk.windowW, &vk.windowH);
    if (extent.width == 0xFFFFFFFFu) {
        extent.width = (uint32_t)vk.windowW;
        extent.height = (uint32_t)vk.windowH;
    }
    if (extent.width == 0 || extent.height == 0) {
        /* Minimised: no swapchain until the window has a size again. */
        destroySwapchain();
        vk.swapchainRetryNs = SDL_GetTicksNS() + 250000000ull;
        return 0;
    }
    /* Pre-rotation (ensureRotation): the images in the display's own orientation. The
     * window's size is the player's view; the display's is that turned (Android reports
     * currentExtent in either: the window decides). */
    vk.rotate = vk.rotateForced >= 0 ? vk.rotateForced : quarterOf(caps.currentTransform);
    if (vk.rotate & 1) {
        uint32_t viewW = vk.windowW > 0 ? (uint32_t)vk.windowW : extent.width;
        uint32_t viewH = vk.windowH > 0 ? (uint32_t)vk.windowH : extent.height;
        VkExtent2D turned = {viewH, viewW};
        if (turned.width < caps.minImageExtent.width || turned.width > caps.maxImageExtent.width ||
            turned.height < caps.minImageExtent.height || turned.height > caps.maxImageExtent.height) {
            gpuWarnOnce("rotate", "vulkan: the surface takes no %ux%u images: no pre-rotation", turned.width,
                        turned.height);
            vk.rotate = vk.rotateForced >= 0 ? 0 : vk.rotate; /* the display's own turn stays the compositor's */
        }
        if (vk.rotate & 1) extent = turned;
    }
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &n, NULL);
    VkSurfaceFormatKHR* formats = calloc(n ? n : 1, sizeof(*formats));
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &n, formats);
    VkSurfaceFormatKHR format = formats[0];
    for (uint32_t i = 0; i < n; i++) {
        /* UNORM, not sRGB: the PS1's colours go to the screen as they are. */
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            format = formats[i];
            break;
        }
    }
    free(formats);

    uint32_t images = caps.minImageCount + 1;
    if (images < 3) {
        images = 3;
    }
    if (caps.maxImageCount != 0 && images > caps.maxImageCount) {
        images = caps.maxImageCount;
    }
    VkSwapchainCreateInfoKHR si = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    si.surface = vk.surface;
#if defined(VK_EXT_present_timing) && defined(VK_KHR_present_id2) && !defined(XG_TEST_NO_PRESENT_TIMING)
    if(timingEnabled) {
        VkSurfaceCapabilitiesPresentId2KHR idCaps={.sType=VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_ID_2_KHR};
        VkPresentTimingSurfaceCapabilitiesEXT tc={.sType=VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT,.pNext=&idCaps};
        VkSurfaceCapabilities2KHR caps2={.sType=VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,.pNext=&tc};
        VkPhysicalDeviceSurfaceInfo2KHR surface={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,.surface=vk.surface};
        if(timingCapabilities(vk.physical,&surface,&caps2)==VK_SUCCESS && tc.presentTimingSupported && idCaps.presentId2Supported) {
            timingStages=tc.presentStageQueries&(VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT|VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT);
            si.flags|=VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT|VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR;
        } else timingEnabled=0;
    }
#endif
    si.minImageCount = images;
    si.imageFormat = format.format;
    si.imageColorSpace = format.colorSpace;
    si.imageExtent = extent;
    si.imageArrayLayers = 1;
    si.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    vk.copyPresent=vk.copyQueue && (caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT) && !vk.rotate;
    if(vk.copyPresent)si.imageUsage|=VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    si.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    uint32_t sharingFamilies[2]={vk.queueFamily,vk.copyFamily};
    if(vk.copyPresent && vk.queueFamily!=vk.copyFamily) {
        si.imageSharingMode=VK_SHARING_MODE_CONCURRENT;si.queueFamilyIndexCount=2;si.pQueueFamilyIndices=sharingFamilies;
    }
    si.preTransform = caps.currentTransform;
    si.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                            : (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
    si.presentMode = choosePresentMode();
    vk.bufferedMode=bufferedPresentation();
    si.clipped = VK_TRUE;
    si.oldSwapchain = vk.swapchain;
    VkSwapchainKHR swapchain;
    VkResult r = vkCreateSwapchainKHR(vk.device, &si, NULL, &swapchain);
    destroySwapchain();
    if (r == VK_ERROR_SURFACE_LOST_KHR) {
        loseSurface("vkCreateSwapchainKHR");
        return 0;
    }
    if (r != VK_SUCCESS) {
        gpuWarnOnce("swapchain", "vulkan: vkCreateSwapchainKHR failed (%d); not presenting, retrying each second", (int)r);
        vk.swapchainRetryNs = SDL_GetTicksNS() + 1000000000ull;
        return 0;
    }
    vk.swapchain = swapchain;
#if defined(VK_EXT_present_timing) && defined(VK_KHR_present_id2) && !defined(XG_TEST_NO_PRESENT_TIMING)
    if(timingEnabled) {
        VkTimeDomainKHR domains[16];uint64_t ids[16];VkSwapchainTimeDomainPropertiesEXT td={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT,
            .timeDomainCount=16,.pTimeDomains=domains,.pTimeDomainIds=ids};
        VkResult tr=timingDomains(vk.device,swapchain,&td,NULL);
        if(tr==VK_SUCCESS && td.timeDomainCount && timingQueueSize(vk.device,swapchain,256)==VK_SUCCESS) {
            timingDomain=ids[0];for(unsigned i=0;i<td.timeDomainCount;i++)if(domains[i]==VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR)timingDomain=ids[i];
            if(!timingFile)timingFile=fopen(getenv("XG_PRESENT_TIMING_LOG"),"w");
            if(!timingFile) { timingEnabled=0;gpuWarnOnce("present-timing-file","vulkan: could not open the requested presentation timing log"); }
            else gpuLog("present timing: enabled domain=%llu stages=%x",(unsigned long long)timingDomain,timingStages);
        } else timingEnabled=0;
    }
#endif
    vk.swapchainExtent = extent;
    vk.presentMode = si.presentMode;
    vk.swapchainImageCount = MAX_SWAPCHAIN_IMAGES;
    VK_CHECK(vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &vk.swapchainImageCount, vk.swapchainImages));
    for (uint32_t i = 0; i < vk.swapchainImageCount; i++) {
        VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = vk.swapchainImages[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format.format;
        vi.subresourceRange = colorRange;
        VK_CHECK(vkCreateImageView(vk.device, &vi, NULL, &vk.swapchainViews[i]));
        VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(vk.device, &sci, NULL, &vk.presented[i]));
    }
    if (vk.presentPipeline == VK_NULL_HANDLE || vk.presentFormat != format.format) {
        if (vk.presentPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(vk.device, vk.presentPipeline, NULL);
        }
        vk.presentPipeline = createPipeline(presentVertSpv, sizeof(presentVertSpv), presentFragSpv,
                                            sizeof(presentFragSpv), 0, format.format);
        vk.presentFormat = format.format;
    }
    vk.swapchainFormat = format.format;
    vk.swapchainStale = 0;
    if (vk.rotate) {
        ensureRotation(vk.rotate & 1 ? (int)extent.height : (int)extent.width,
                       vk.rotate & 1 ? (int)extent.width : (int)extent.height, format.format);
    }
    return 1;
}

static void recreateSwapchain(void) {
    presentDrain();
    vkDeviceWaitIdle(vk.device);
    for (int i = 0; i < FRAMES; i++) {
        vk.frames[i].submitted = 0; /* idle: every fence has signalled */
    }
    if (vk.surface == VK_NULL_HANDLE && (!vk.surfaceLost || !remakeSurface())) {
        return;
    }
    createSwapchain();
}

/* ---- command recording ------------------------------------------------------------ */

static FrameCtx* frame(void) {
    return &vk.frames[vk.cur];
}

static void waitFrame(FrameCtx* f) {
    if (f->submitted) {
        CPU_TIME_BEGIN(CPU_GPUWAIT);
        VK_CHECK(vkWaitForFences(vk.device, 1, &f->fence, VK_TRUE, UINT64_MAX));
        CPU_TIME_END(CPU_GPUWAIT);
        f->submitted = 0;
    }
}

/* A canvas the trim replaced without idling the device (hiTrimWideBody):
 * each frame context's set names it until that context records again, and
 * it is freed once every context has finished what it recorded before
 * (each one's wait here), or at the next idle (frame -1). */
static void hiFrameDone(int frame) {
    for (int i = 0; i < (int)(sizeof(vk.hiRetired) / sizeof(vk.hiRetired[0])); i++) {
        if (vk.hiRetired[i].image == VK_NULL_HANDLE) continue;
        vk.hiRetired[i].pending &= frame < 0 ? 0 : ~(1u << frame);
        if (!vk.hiRetired[i].pending) {
            destroyImage(&vk.hiRetired[i].image, &vk.hiRetired[i].memory, &vk.hiRetired[i].view);
        }
    }
    if (frame >= 0 && (vk.hiSetsStale & (1u << frame))) {
        writeHiSet(&vk.frames[frame]);
        vk.hiSetsStale &= ~(1u << frame);
    }
}

static VkCommandBuffer recording(void) {
    FrameCtx* f = frame();
    if (!f->recording) {
        waitFrame(f);
        /* Done with (or idle since a wait that cleared `submitted`). */
        gtHarvest(f);
        autoTimeHarvest(f);
        hiFrameDone((int)(f - vk.frames));
        VK_CHECK(vkResetFences(vk.device, 1, &f->fence));
        VK_CHECK(vkResetCommandBuffer(f->cmd, 0));
        VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(f->cmd, &bi));
        if (f->queries == VK_NULL_HANDLE && gt.collect) {
            VkQueryPoolCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qi.queryCount = GT_QUERIES;
            if (vkCreateQueryPool(vk.device, &qi, NULL, &f->queries) != VK_SUCCESS) f->queries = VK_NULL_HANDLE;
            f->queryUsed = 0;
        }
        if (f->queries != VK_NULL_HANDLE) {
            vkCmdResetQueryPool(f->cmd, f->queries, 0, GT_QUERIES);
        }
        if (autoTime.on) {
            if (f->autoQueries == VK_NULL_HANDLE) {
                VkQueryPoolCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
                qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
                qi.queryCount = 2;
                if (vkCreateQueryPool(vk.device, &qi, NULL, &f->autoQueries) != VK_SUCCESS) {
                    gpuLog("vulkan: Auto GPU timing pool unavailable (using window limits)");
                    autoTime.available = 0;
                    gvkGpuTimeEnable(0);
                }
            }
            if (autoTime.on) {
                vkCmdResetQueryPool(f->cmd, f->autoQueries, 0, 2);
                vkCmdWriteTimestamp2(f->cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f->autoQueries, 0);
                f->autoRecorded = 1;
                f->autoEpoch = autoTime.epoch;
                f->autoFrame = autoTime.frame;
                f->autoScale = vk.hiN > 0 ? vk.hiN : 1;
            }
        }
        f->vertexUsed = 0;
        memset(f->interpolationRunUsed,0,sizeof(f->interpolationRunUsed));
        f->stagingUsed = 0;
        f->movieHiUsed = 0;
        f->resStageUsed = 0;
        f->resCopyCount = 0;
        f->spanUsed = 0;
        f->sampleCount = 0;
        f->recording = 1;
        f->uses++;
    }
    return f->cmd;
}

static void barrierIfNeeded(VkCommandBuffer cmd) {
    if (vk.needBarrier) {
        rasterBarrier(cmd);
        vk.needBarrier = 0;
    }
}

/* The wide target's draws of the current batch (wide.c), drawn after its
 * VRAM draws: the batch never reads what it writes, so their texels are the
 * ones the VRAM draws read. */
#define WIDE_MAX 65536
static GpuVertex wideBuf[WIDE_MAX];

/* The hi-res copy's draws of the current batch (hires.c), drawn after the
 * wide target's in passes of their own. */
#define HI_MAX 65536
static HiVertex hiBuf[HI_MAX];
static HiVertex hiWideBuf[HI_MAX];
static void drawHiBatch(FrameCtx* f, int wide);
static void wideTouch(int x1);

/* Vertex bytes the batch will still add at its end. */
static size_t pendingBytes(void) {
    return (size_t)vk.wideCount * sizeof(GpuVertex) + (size_t)(vk.hiCount + vk.hiWideCount) * sizeof(HiVertex) +
           2 * sizeof(GpuVertex);
}

/* Fragment arrival order is arbitrary. Each bounded pass collects at most
 * 64 triangles, then one compute invocation resolves each pixel in order.
 * Scissors only partition pixels, preserving hardware interpolation. */
static void drawOrdered(FrameCtx* f,const void* src,size_t stride,VkDeviceSize offset,
                        uint32_t first,uint32_t count,int scale,int w,int h,int kind,
                        const void* constants,uint32_t constantBytes) {
    VkCommandBuffer cmd=f->cmd;
    offset+=(VkDeviceSize)first*stride;first=0;
    VkPipeline graphics=kind==0?vk.orderedDraw:kind==1?vk.orderedWide:kind==2?vk.orderedHi:vk.orderedHiWide;
    VkPipelineLayout layout=kind<2?vk.pipelineLayout:vk.hiLayout;
    /* No overlapping invocations: keep the original one-pass shader. This
     * matters for small sprites and individual field faces between texture
     * hazards; collecting/sorting their pixels would add work for no gain. */
    if(orderedIndependent(src,stride,(int)count,kind>=2?offsetof(HiVertex,w):0)) {
        fullBarrier(cmd);
        VkRenderingInfo ri={.sType=VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea.extent=(VkExtent2D){(uint32_t)w,(uint32_t)h};ri.layerCount=1;
        vkCmdBeginRendering(cmd,&ri);
        VkPipeline p=kind==0?vk.drawPipeline:kind==1?vk.wideDrawPipeline:kind==2?vk.hiDrawPipeline:vk.hiWidePipeline;
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,p);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&f->set,0,NULL);
        vkCmdBindVertexBuffers(cmd,0,1,&f->vertices.buffer,&offset);
        VkViewport vp={0,0,(float)w,(float)h,0,1};VkRect2D sc={{0,0},{(uint32_t)w,(uint32_t)h}};
        vkCmdSetViewport(cmd,0,1,&vp);vkCmdSetScissor(cmd,0,1,&sc);
        if(constantBytes)vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,constantBytes,constants);
        vkCmdDraw(cmd,count,1,first,0);vkCmdEndRendering(cmd);fullBarrier(cmd);return;
    }
    for(uint32_t start=0;start<count;start+=3*ORDERED_TRIANGLES) {
        int triangles=(int)((count-start)/3);
        if(triangles>ORDERED_TRIANGLES)triangles=ORDERED_TRIANGLES;
        /* With W<=0, homogeneous clipping can project beyond the input
         * screen-space bounds. Preserve exact clipping via the original
         * shader, serialized per triangle, rather than assume a small box. */
        int clippedPerspective=0;
        if(kind>=2)for(int i=0;i<triangles*3;i++) {
            const HiVertex* v=(const HiVertex*)((const char*)src+(start+i)*stride);
            if(!isfinite(v->w)||v->w<=0)clippedPerspective=1;
        }
        if(clippedPerspective) {
            for(int t=0;t<triangles;t++) {
                size_t step=(start+3*t)*stride;
                drawOrdered(f,(const char*)src+step,stride,offset+step,0,3,scale,w,h,kind,constants,constantBytes);
            }
            continue;
        }
        OrderedBox boxes[ORDERED_TRIANGLES],all={w,h,0,0};
        for(int t=0;t<triangles;t++) {
            OrderedBox b=orderedBounds((const char*)src+(start+3*t)*stride,stride,scale,w,h);
            boxes[t]=b;
            if(b.x0<all.x0)all.x0=b.x0;
            if(b.y0<all.y0)all.y0=b.y0;
            if(b.x1>all.x1)all.x1=b.x1;
            if(b.y1>all.y1)all.y1=b.y1;
        }
        int columnWidth=(int)(vk.orderedCapacity/(unsigned)triangles);
        for(int x=all.x0;x<all.x1;x+=columnWidth) {
            OrderedBox col={x,all.y0,x+columnWidth,all.y1};
            if(col.x1>all.x1)col.x1=all.x1;
            for(int y=col.y0;y<col.y1;) {
                int end=orderedBandEnd(boxes,triangles,col,y,vk.orderedCapacity);
                /* A column is at most capacity/triangleCount wide, so one
                 * row always fits, even for full-screen overdraw. */
                if(end<=y)abort();
                uint32_t width=(uint32_t)(col.x1-x),height=(uint32_t)(end-y);
                struct {
                    uint32_t firstOrder,overflow;int32_t x,y,w,h;uint32_t capacity,kind;
                    uint32_t rect[ORDERED_TRIANGLES][4];
                } header={(first+start)/3,0,x,y,(int32_t)width,(int32_t)height,vk.orderedCapacity,(uint32_t)kind,{{0}}};
                uint32_t base=0;
                for(int t=0;t<triangles;t++) {
                    int x0=boxes[t].x0>x?boxes[t].x0:x,y0=boxes[t].y0>y?boxes[t].y0:y;
                    int x1=boxes[t].x1<col.x1?boxes[t].x1:col.x1,y1=boxes[t].y1<end?boxes[t].y1:end;
                    uint32_t bw=x1>x0?(uint32_t)(x1-x0):0,bh=y1>y0?(uint32_t)(y1-y0):0;
                    header.rect[t][0]=(uint32_t)x0;header.rect[t][1]=(uint32_t)y0;
                    header.rect[t][2]=bw|(bh<<16);header.rect[t][3]=base;base+=bw*bh;
                }
                if(base>vk.orderedCapacity)abort();
                fullBarrier(cmd);
                vkCmdUpdateBuffer(cmd,vk.orderedNodes,0,4,&header.firstOrder);
                vkCmdUpdateBuffer(cmd,vk.orderedNodes,8,sizeof(header)-8,(const char*)&header+8);
                vkCmdFillBuffer(cmd,vk.orderedHeads,0,(VkDeviceSize)width*height*4,UINT32_MAX);
                fullBarrier(cmd);
                VkRenderingInfo ri={.sType=VK_STRUCTURE_TYPE_RENDERING_INFO};
                ri.renderArea.extent=(VkExtent2D){(uint32_t)w,(uint32_t)h};ri.layerCount=1;
                vkCmdBeginRendering(cmd,&ri);
                vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,graphics);
                vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&f->set,0,NULL);
                vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,2,1,&vk.orderedSet,0,NULL);
                vkCmdBindVertexBuffers(cmd,0,1,&f->vertices.buffer,&offset);
                VkViewport vp={0,0,(float)w,(float)h,0,1};
                VkRect2D scissor={{x,y},{width,height}};
                vkCmdSetViewport(cmd,0,1,&vp);vkCmdSetScissor(cmd,0,1,&scissor);
                if(constantBytes)vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,constantBytes,constants);
                vkCmdDraw(cmd,(uint32_t)triangles*3,1,first+start,0);
                vkCmdEndRendering(cmd);
                fullBarrier(cmd);
                vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,vk.orderedResolve);
                VkDescriptorSet sets[2]={f->set,vk.orderedSet};
                vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,vk.orderedResolveLayout,0,2,sets,0,NULL);
                vkCmdDispatch(cmd,(width+7)/8,(height+7)/8,1);
                fullBarrier(cmd);
                vk.orderedPasses++;
                y=end;
            }
        }
    }
}

static void drawWideBatch(FrameCtx* f) {
    rasterBarrier(f->cmd);
    int q = gtStart(f->cmd, GT_WIDE);
    if(vk.orderedEnabled) {
        VkDeviceSize offset=f->vertexUsed;
        memcpy(f->vertices.map+offset,wideBuf,(size_t)vk.wideCount*sizeof(GpuVertex));
        drawOrdered(f,wideBuf,sizeof(GpuVertex),offset,0,vk.wideCount,1,WIDE_W,WIDE_H,1,NULL,0);
        f->vertexUsed+=(size_t)vk.wideCount*sizeof(GpuVertex);vk.wideCount=0;
        vk.wideScanned=0;vk.wideDependent=0;
        gtStop(f->cmd,q);return;
    }
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.extent.width = WIDE_W;
    ri.renderArea.extent.height = WIDE_H;
    ri.layerCount = 1;
    vkCmdBeginRendering(f->cmd, &ri);
    vkCmdBindPipeline(f->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.wideDrawPipeline);
    vkCmdBindDescriptorSets(f->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipelineLayout, 0, 1, &f->set, 0, NULL);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(f->cmd, 0, 1, &f->vertices.buffer, &offset);
    VkViewport viewport = {0.0f, 0.0f, (float)WIDE_W, (float)WIDE_H, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {WIDE_W, WIDE_H}};
    vkCmdSetViewport(f->cmd, 0, 1, &viewport);
    vkCmdSetScissor(f->cmd, 0, 1, &scissor);
    memcpy(f->vertices.map + f->vertexUsed, wideBuf, (size_t)vk.wideCount * sizeof(GpuVertex));
    vkCmdDraw(f->cmd, vk.wideCount, 1, (uint32_t)(f->vertexUsed / sizeof(GpuVertex)), 0);
    f->vertexUsed += (size_t)vk.wideCount * sizeof(GpuVertex);
    vk.wideCount = 0;
    vk.wideScanned = 0; vk.wideDependent = 0;
    vkCmdEndRendering(f->cmd);
    gtStop(f->cmd, q);
}

/* Both tests run at every native barrier while wide draws stay pending.
 * Each is a pure per-vertex test, pending vertices are only appended until
 * their batch is drawn (where the scan state resets with the count), and
 * vk.interlock is fixed per device: examining only the new vertices keeps
 * the exact answer of a full rescan in O(appended) instead of O(pending). */
static int wideIndependentOfNative(void) {
    if (!vk.interlock) return 0;
    for (uint32_t i = vk.hiWideScanned; i < vk.hiWideCount && !vk.hiWideDependent; i++) {
        const HiVertex* v = &hiWideBuf[i];
        if (!(v->mode & DRAW_TEXTURED)) continue;
        /* These reads use only the wide enhanced image. Indexed/native
         * textures, owned sample variants and wrapped pages retain the
         * original full boundary. Transfers of wide sources also do so. */
        if (v->sample[3] || ((v->tex >> 7) & 3) != 2 || !v->wideTex || v->texWindow ||
            (int16_t)v->uvMin < 0 || (int16_t)v->uvMax > 255 ||
            (int)((v->tex & 15)*64)+(int16_t)v->uvMax >= VRAM_W) vk.hiWideDependent = 1;
    }
    vk.hiWideScanned = vk.hiWideCount;
    return !vk.hiWideDependent;
}

static int wideNativeIndependent(void) {
    if (!vk.interlock) return 0;
    for (uint32_t i = vk.wideScanned; i < vk.wideCount && !vk.wideDependent; i++) {
        const GpuVertex* v = &wideBuf[i];
        if (!(v->mode & DRAW_TEXTURED) || (v->mode & DRAW_CACHED)) continue;
        if (((v->tex >> 7) & 3) != 2 || !v->wideTex || v->texWindow ||
            v->u < 0 || v->u > 254 || (v->tex & 15)*64 + ceilf(v->u+1) >= VRAM_W) vk.wideDependent = 1;
    }
    vk.wideScanned = vk.wideCount;
    return !vk.wideDependent;
}

static void finishBatch(int nativeOnly) {
    int keepWide = nativeOnly && wideIndependentOfNative();
    int keepWideNative = nativeOnly && wideNativeIndependent();
    if (!vk.rendering) {
        if (vk.device != VK_NULL_HANDLE) {
            resFlush(frame());
            if (vk.wideCount && !keepWideNative) drawWideBatch(frame());
            if (vk.hiWideCount && !keepWide) drawHiBatch(frame(),1);
        }
        return;
    }
    FrameCtx* f = frame();
    uint32_t count = (uint32_t)(f->vertexUsed / sizeof(GpuVertex)) - vk.batchFirst;
    /* CPU-resolved native draws can still have enhanced/wide work. Do not
     * open an empty native render pass for every such batch. */
    if (count) {
        barrierIfNeeded(f->cmd);
        int query=gtStart(f->cmd,GT_DRAW);
        if(vk.orderedEnabled) {
            drawOrdered(f,f->vertices.map+vk.batchFirst*sizeof(GpuVertex),sizeof(GpuVertex),0,vk.batchFirst,count,1,VRAM_W,VRAM_H,0,NULL,0);
        } else {
            VkRenderingInfo ri={.sType=VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea={{0,0},{VRAM_W,VRAM_H}},.layerCount=1};
            vkCmdBeginRendering(f->cmd,&ri);
            vkCmdBindPipeline(f->cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,vk.drawPipeline);
            vkCmdBindDescriptorSets(f->cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,vk.pipelineLayout,0,1,&f->set,0,NULL);
            VkDeviceSize offset=0;
            vkCmdBindVertexBuffers(f->cmd,0,1,&f->vertices.buffer,&offset);
            VkViewport viewport={0,0,(float)VRAM_W,(float)VRAM_H,0,1};
            VkRect2D scissor={{0,0},{VRAM_W,VRAM_H}};
            vkCmdSetViewport(f->cmd,0,1,&viewport);vkCmdSetScissor(f->cmd,0,1,&scissor);
            vkCmdDraw(f->cmd,count,1,vk.batchFirst,0);
            vkCmdEndRendering(f->cmd);
        }
        gtStop(f->cmd,query);
    }
    vk.rendering=0;vk.needBarrier=1;
    resFlush(f);
    if (vk.wideCount && !keepWideNative) drawWideBatch(f);
    if (vk.hiCount) drawHiBatch(f,0);
    if (vk.hiWideCount && !keepWide) drawHiBatch(f,1);
}

static void endBatch(void) { finishBatch(0); }
static void endNativeBatch(void) { finishBatch(1); }

static void beginBatch(void) {
    recording();
    vk.batchFirst=(uint32_t)(frame()->vertexUsed/sizeof(GpuVertex));
    vk.rendering=1;
}

/* Hand the current context to the queue (signalling its fence) and move on. */
static void submitFrame(const VkSemaphore* wait, const VkSemaphore* signal) {
    if (vk.hiWideCount || vk.wideCount) endBatch();
    FrameCtx* f = frame();
    if (!f->recording) {
        return;
    }
    if (!vk.rendering) {
        resFlush(f);
    }
    if (f->autoRecorded)
        vkCmdWriteTimestamp2(f->cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f->autoQueries, 1);
    VK_CHECK(vkEndCommandBuffer(f->cmd));
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = wait != NULL;
    si.pWaitSemaphores = wait;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f->cmd;
    si.signalSemaphoreCount = signal != NULL;
    si.pSignalSemaphores = signal;
    VK_CHECK(vkQueueSubmit(vk.queue, 1, &si, f->fence));
    f->recording = 0;
    f->submitted = 1;
    vk.cur = (vk.cur + 1) % FRAMES;
}

/* ---- pack images kept on the GPU ----------------------------------------------- */

/* Texture pack images (TextureSample.keep, texture_pack.c) go to a
 * device-local buffer once and stay there, found again by their revision,
 * patch and scale: a field at 4x draws from a few dozen whole pages every
 * frame, which the span arena would have to copy each frame. Index maps are
 * kept as bytes and coloured in hires.frag with the primitive's live CLUT,
 * so palette changes cost nothing. A full buffer starts over (after the
 * device is idle); a picture too big for it goes the span way. The buffer
 * is set 0's binding 8 (a 16-byte stand-in until the first pack image). */
#define RES_ENTRIES 8192
#define RES_HASH 16384
#define RES_STAGE_BYTES (32u << 20) /* upload room in each frame context */
typedef struct {
    uint64_t revision;
    int u, v, w, h, scale, indexed;
    uint32_t offset; /* in words */
} ResEntry;
static ResEntry resEntries[RES_ENTRIES];
static int32_t resBucket[RES_HASH], resChain[RES_ENTRIES]; /* index + 1, 0 end */
static int resCount;

static uint32_t resHash(const TextureSample* s) {
    uint64_t k = s->revision ^ ((uint64_t)(uint16_t)s->u << 16) ^ ((uint64_t)(uint16_t)s->v << 32) ^
                 ((uint64_t)s->w << 44) ^ ((uint64_t)s->h << 54) ^ (uint64_t)s->scale;
    k ^= k >> 31;
    k *= 0x9E3779B97F4A7C15ull;
    return (uint32_t)(k >> 40) & (RES_HASH - 1);
}

static void resWriteSet(FrameCtx* f) {
    VkDescriptorBufferInfo bi = {vk.resBuf != VK_NULL_HANDLE ? vk.resBuf : vk.resDummy, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = f->set;
    w.dstBinding = RESIDENT_BINDING;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(vk.device, 1, &w, 0, NULL);
}

static int deviceBuffer(VkBuffer* b, VkDeviceMemory* m, VkDeviceSize size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk.device, &bi, NULL, b) != VK_SUCCESS) {
        *b = VK_NULL_HANDLE;
        return 0;
    }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(vk.device, *b, &req);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (vkAllocateMemory(vk.device, &ai, NULL, m) != VK_SUCCESS) {
        vkDestroyBuffer(vk.device, *b, NULL);
        *b = VK_NULL_HANDLE;
        *m = VK_NULL_HANDLE;
        return 0;
    }
    VK_CHECK(vkBindBufferMemory(vk.device, *b, *m, 0));
    return 1;
}

/* Hand over this context's work and wait for the device: nothing submitted
 * still uses what is about to change. */
static void resIdle(void) {
    CPU_TIME_BEGIN(CPU_GPUWAIT);
    endBatch();
    submitFrame(NULL, NULL);
    presentDrain();
    vkDeviceWaitIdle(vk.device);
    CPU_TIME_END(CPU_GPUWAIT);
}

static void orderedDestroy(void) {
    VkPipeline* pipelines[5]={&vk.orderedDraw,&vk.orderedWide,&vk.orderedHi,&vk.orderedHiWide,&vk.orderedResolve};
    for(int i=0;i<5;i++) {
        if(*pipelines[i])vkDestroyPipeline(vk.device,*pipelines[i],NULL);
        *pipelines[i]=VK_NULL_HANDLE;
    }
    if(vk.orderedPool)vkDestroyDescriptorPool(vk.device,vk.orderedPool,NULL);
    vk.orderedPool=VK_NULL_HANDLE;vk.orderedSet=VK_NULL_HANDLE;
    if(vk.orderedNodes)vkDestroyBuffer(vk.device,vk.orderedNodes,NULL);
    if(vk.orderedHeads)vkDestroyBuffer(vk.device,vk.orderedHeads,NULL);
    if(vk.orderedNodesMemory)vkFreeMemory(vk.device,vk.orderedNodesMemory,NULL);
    if(vk.orderedHeadsMemory)vkFreeMemory(vk.device,vk.orderedHeadsMemory,NULL);
    vk.orderedNodes=vk.orderedHeads=VK_NULL_HANDLE;
    vk.orderedNodesMemory=vk.orderedHeadsMemory=VK_NULL_HANDLE;
}
static int orderedMake(void) {
    if(vk.orderedState)return vk.orderedState>0;
    vk.orderedState=-1;
    uint32_t n=0;vkGetPhysicalDeviceQueueFamilyProperties(vk.physical,&n,NULL);
    VkQueueFamilyProperties* props=calloc(n,sizeof(*props));
    if(!props)return 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk.physical,&n,props);
    int compute=vk.queueFamily<n&&(props[vk.queueFamily].queueFlags&VK_QUEUE_COMPUTE_BIT)!=0;
    free(props);
    if(!compute||envFlag("XG_ORDERED_TEST_FAIL"))return 0;
    vk.orderedCapacity=ORDERED_CAPACITY;
    /* Small bounded arenas exercise horizontal and vertical partitioning in
     * tests. Production uses 20 MiB, independent of render resolution. */
    const char* cap=getenv("XG_ORDERED_TEST_CAPACITY");
    if(cap) {unsigned long c=strtoul(cap,NULL,10);if(c>=ORDERED_TRIANGLES&&c<=ORDERED_CAPACITY)vk.orderedCapacity=(uint32_t)c;}
    VkDeviceSize bytes=(VkDeviceSize)vk.orderedCapacity*16+32+16*ORDERED_TRIANGLES;
    if(bytes>vk.limits.maxStorageBufferRange)return 0;
    VkBufferUsageFlags usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if(!deviceBuffer(&vk.orderedNodes,&vk.orderedNodesMemory,bytes,usage)||
       !deviceBuffer(&vk.orderedHeads,&vk.orderedHeadsMemory,(VkDeviceSize)vk.orderedCapacity*4,usage))goto fail;
    VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets=1;pi.poolSizeCount=1;pi.pPoolSizes=&size;
    if(vkCreateDescriptorPool(vk.device,&pi,NULL,&vk.orderedPool)!=VK_SUCCESS)goto fail;
    VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool=vk.orderedPool;ai.descriptorSetCount=1;ai.pSetLayouts=&vk.orderedLayout;
    if(vkAllocateDescriptorSets(vk.device,&ai,&vk.orderedSet)!=VK_SUCCESS)goto fail;
    VkDescriptorBufferInfo bi[2]={{vk.orderedNodes,0,bytes},{vk.orderedHeads,0,(VkDeviceSize)vk.orderedCapacity*4}};
    VkWriteDescriptorSet writes[2]={{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    for(int i=0;i<2;i++){writes[i].dstSet=vk.orderedSet;writes[i].dstBinding=(uint32_t)i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].pBufferInfo=&bi[i];}
    vkUpdateDescriptorSets(vk.device,2,writes,0,NULL);
    vk.orderedDraw=createPipeline(draw_ordered_vertSpv,sizeof(draw_ordered_vertSpv),draw_ordered_fragSpv,sizeof(draw_ordered_fragSpv),3,VK_FORMAT_UNDEFINED);
    vk.orderedWide=createPipeline(draw_wide_ordered_vertSpv,sizeof(draw_wide_ordered_vertSpv),draw_wide_ordered_fragSpv,sizeof(draw_wide_ordered_fragSpv),4,VK_FORMAT_UNDEFINED);
    vk.orderedHi=createHiPipeline(hires_ordered_vertSpv,sizeof(hires_ordered_vertSpv),hires_ordered_fragSpv,sizeof(hires_ordered_fragSpv),3);
    vk.orderedHiWide=createHiPipeline(hires_wide_ordered_vertSpv,sizeof(hires_wide_ordered_vertSpv),hires_wide_ordered_fragSpv,sizeof(hires_wide_ordered_fragSpv),4);
    if(!vk.orderedDraw||!vk.orderedWide||!vk.orderedHi||!vk.orderedHiWide)goto fail;
    VkShaderModule module=shaderModule(ordered_compSpv,sizeof(ordered_compSpv));
    VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage=(VkPipelineShaderStageCreateInfo){.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"};
    ci.layout=vk.orderedResolveLayout;
    VkResult result=vkCreateComputePipelines(vk.device,VK_NULL_HANDLE,1,&ci,NULL,&vk.orderedResolve);
    vkDestroyShaderModule(vk.device,module,NULL);
    if(result!=VK_SUCCESS)goto fail;
    VkCommandBuffer commands=recording();
    fullBarrier(commands);vkCmdFillBuffer(commands,vk.orderedNodes,4,4,0);fullBarrier(commands);
    vk.orderedState=1;
    gpuLog("vulkan: exact ordered fallback ready (%u operations, %.1f MiB)",vk.orderedCapacity,(double)(bytes+vk.orderedCapacity*4)/(1<<20));
    return 1;
fail:
    orderedDestroy();return 0;
}
void gvkSetOrderedFallback(int on) {
    if(!vk.device)return;
    endBatch();
    if(on&&!vk.interlock&&vk.orderedState==0) {resIdle();orderedMake();}
    vk.orderedEnabled=on&&!vk.interlock&&vk.orderedState>0;
    if(on&&!vk.interlock&&!vk.orderedEnabled)gpuWarnOnce("ordered-fallback","vulkan: ordered fallback unavailable; using exact split batches");
}
uint32_t gvkOrderedOverflow(void) {
    if(vk.orderedState<=0)return 0;
    endBatch();
    HostBuffer read={0};createHostBuffer(&read,4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,1);
    FrameCtx* f=frame();VkCommandBuffer cmd=recording();fullBarrier(cmd);
    VkBufferCopy copy={4,0,4};vkCmdCopyBuffer(cmd,vk.orderedNodes,read.buffer,1,&copy);
    hostReadBarrier(cmd);submitFrame(NULL,NULL);waitFrame(f);
    uint32_t result;memcpy(&result,read.map,4);destroyHostBuffer(&read);return result;
}
int gvkOrderedWrites(void) {return vk.interlock||vk.orderedEnabled;}
const char* gpuOrderedStatus(void) {
    if(!vk.device)return "Renderer has not started.";
    if(vk.interlock)return "Hardware ordered rendering is active.";
    if(vk.orderedEnabled)return "Exact ordered fallback is active.";
    if(vk.orderedState<0)return "Ordered fallback is unavailable; exact split batches are active.";
    return "Exact split batches are active.";
}

static void resClear(void) {
    memset(resBucket, 0, sizeof(resBucket));
    resCount = 0;
    vk.resTopWords = 0;
}

/* Reserve on demand, up to texture_pack_gpu_mb (default512, bounded by the
 * heap and storage-buffer range). Small packs must not reserve the entire
 * cache at the expense of the active native and replay canvases. */
static int resMake(void) {
    if (vk.resState != 0) {
        return vk.resState > 0;
    }
    vk.resState = -1;
    settingsDescribe("texture_pack_gpu_mb", "texture_pack_gpu_mb: GPU memory for texture pack images, in MB "
                                            "(64 to 4096, default 512; at most a quarter of the GPU's memory)");
    long mb = settingsInt("texture_pack_gpu_mb", 512);
    mb = mb < 64 ? 64 : mb > 4096 ? 4096 : mb;
    VkDeviceSize heap = 0;
    for (uint32_t i = 0; i < vk.memProps.memoryHeapCount; i++) {
        if ((vk.memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) && vk.memProps.memoryHeaps[i].size > heap) {
            heap = vk.memProps.memoryHeaps[i].size;
        }
    }
    if (heap != 0 && (VkDeviceSize)mb << 20 > heap / 4) {
        mb = (long)((heap / 4) >> 20);
    }
    if ((VkDeviceSize)mb << 20 > vk.limits.maxStorageBufferRange) {
        mb = (long)(vk.limits.maxStorageBufferRange >> 20); /* one descriptor sees it all */
    }
    vk.resLimitWords = (uint32_t)(((VkDeviceSize)mb << 20)/4);
    resIdle();
    for (mb = mb < 4 ? mb : 4; mb >= 1; mb /= 2) {
        if (deviceBuffer(&vk.resBuf, &vk.resMem, (VkDeviceSize)mb << 20,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
            vk.resCapWords = (uint32_t)(((VkDeviceSize)mb << 20) / 4);
            vk.resState = 1;
            break;
        }
    }
    for (int i = 0; i < FRAMES; i++) {
        resWriteSet(&vk.frames[i]);
    }
    resClear();
    gpuLog("vulkan: texture pack images on the GPU: %s", vk.resState > 0 ? "ok" : "no memory (copied each frame)");
    if (vk.resState > 0) {
        gpuLog("vulkan: %u MB for texture pack images", (unsigned)((uint64_t)vk.resCapWords * 4 >> 20));
    }
    return vk.resState > 0;
}

/* Growing waits for the GPU to idle and drops what the cache holds (re-uploaded as draws want
 * it), so it grows in few, large steps: at least 64 MB, room for twice what is wanted, by
 * fours. Doubling from 4 MB, the first room of an 8x pack grew three times in one frame (a
 * 145 ms hitch, and the sound's catch-up burst cut;). */
static void resGrow(uint32_t wanted) {
    if (wanted <= vk.resCapWords || vk.resCapWords >= vk.resLimitWords) return;
    uint32_t floorWords = (64u << 20) / 4, capacity = vk.resCapWords;
    if (capacity < floorWords) capacity = floorWords < vk.resLimitWords ? floorWords : vk.resLimitWords;
    uint64_t room = (uint64_t)wanted * 2;
    while (capacity < room && capacity < vk.resLimitWords)
        capacity = capacity > vk.resLimitWords/4 ? vk.resLimitWords : capacity*4;
    resIdle();
    VkBuffer buffer; VkDeviceMemory memory;
    if (deviceBuffer(&buffer,&memory,(VkDeviceSize)capacity*4,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        vkDestroyBuffer(vk.device,vk.resBuf,NULL); vkFreeMemory(vk.device,vk.resMem,NULL);
        vk.resBuf=buffer; vk.resMem=memory; vk.resCapWords=capacity;
        for (int i=0;i<FRAMES;i++) resWriteSet(&vk.frames[i]);
        gpuLog("vulkan: texture pack cache grew to %u MB",capacity/262144);
    }
    /* As at the configured ceiling, eviction is only a cache miss. All old
     * references have drained and future draws re-upload their exact data. */
    resClear();
}

/* The pending uploads of this context, before the draws that read them. */
static void resFlush(FrameCtx* f) {
    if (f->resCopyCount == 0) {
        return;
    }
    vkCmdCopyBuffer(f->cmd, f->resStage.buffer, vk.resBuf, (uint32_t)f->resCopyCount, f->resCopies);
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(f->cmd, &di);
    f->resCopyCount = 0;
}

/* A kept sample's place in the buffer (words), uploading it the first
 * time; -1 when it goes the span way. May hand over the context (vertex
 * reservations follow). */
static int64_t resPlace(const TextureSample* s) {
    if (!resMake()) {
        return -1;
    }
    int indexed = s->indices != NULL;
    uint32_t h = resHash(s);
    for (int32_t i = resBucket[h]; i; i = resChain[i - 1]) {
        const ResEntry* e = &resEntries[i - 1];
        if (e->revision == s->revision && e->u == s->u && e->v == s->v && e->w == s->w && e->h == s->h &&
            e->scale == s->scale && e->indexed == indexed) {
            return e->offset;
        }
    }
    size_t pixels = (size_t)s->w * s->h * s->scale * s->scale;
    size_t bytes = indexed ? (pixels + 3) & ~(size_t)3 : pixels * 4;
    uint32_t words = (uint32_t)(bytes / 4);
    if (bytes > RES_STAGE_BYTES) {
        return -1;
    }
    if (vk.resTopWords + words > vk.resCapWords) resGrow(vk.resTopWords + words);
    if (words > vk.resCapWords) return -1;
    if (vk.resTopWords + words > vk.resCapWords || resCount == RES_ENTRIES) {
        static unsigned resets;
        gpuLog("vulkan: texture pack cache full (%u MB, %d images): started over (%u)",
               (unsigned)((uint64_t)vk.resCapWords * 4 >> 20), resCount, ++resets);
        resIdle(); /* full: start over */
        resClear();
    }
    recording(); /* begun now: a context begun later would drop its pending copies */
    FrameCtx* f = frame();
    if (f->resStageUsed + bytes > RES_STAGE_BYTES) {
        endBatch();
        submitFrame(NULL, NULL);
        recording();
        f = frame();
    }
    if (f->resStage.buffer == VK_NULL_HANDLE) {
        createHostBuffer(&f->resStage, RES_STAGE_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0);
    }
    if (f->resCopyCount == f->resCopyCap) {
        int cap = f->resCopyCap ? f->resCopyCap * 2 : 64;
        VkBufferCopy* n = realloc(f->resCopies, (size_t)cap * sizeof(VkBufferCopy));
        if (n == NULL) {
            return -1;
        }
        f->resCopies = n;
        f->resCopyCap = cap;
    }
    uint8_t* dst = f->resStage.map + f->resStageUsed;
    if (indexed) {
        memcpy(dst, s->indices, pixels); /* little-endian: index i is byte i of the words */
        memset(dst + pixels, 0, bytes - pixels);
    } else {
        uint32_t* d = (uint32_t*)dst;
        for (size_t j = 0; j < pixels; j++) {
            const uint8_t* p = s->rgba + j * 4;
            d[j] = p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        }
    }
    uint32_t offset = vk.resTopWords;
    f->resCopies[f->resCopyCount++] = (VkBufferCopy){f->resStageUsed, (VkDeviceSize)offset * 4, bytes};
    f->resStageUsed += (bytes + 15) & ~(size_t)15;
    vk.resTopWords += words;
    ResEntry* e = &resEntries[resCount];
    e->revision = s->revision;
    e->u = s->u, e->v = s->v, e->w = s->w, e->h = s->h, e->scale = s->scale, e->indexed = indexed;
    e->offset = offset;
    resChain[resCount] = resBucket[h];
    resBucket[h] = ++resCount;
    vk.resUploads++;
    vk.resUploadBytes += bytes;
    return offset;
}

void gvkSubmit(void) {
    cpuResolveFlush();
    if (vk.device == VK_NULL_HANDLE) {
        return;
    }
    endBatch();
    submitFrame(NULL, NULL);
}

void gvkDraw(const GpuVertex* v, int n) {
    vramFlushCpuWrites((VRect){0,0,VRAM_W-1,VRAM_H-1});
    size_t bytes = (size_t)n * sizeof(GpuVertex);
    recording();
    if (frame()->vertexUsed + bytes + pendingBytes() > VERTEX_BYTES) {
        endBatch();
        submitFrame(NULL, NULL);
        recording();
    }
    if (!vk.rendering) {
        beginBatch();
    }
    FrameCtx* f = frame();
    memcpy(f->vertices.map + f->vertexUsed, v, bytes);
    f->vertexUsed += bytes;
}

void gvkDrawSpans(GpuVertex* v, int n, const int32_t* spans, int ints) {
    vramFlushCpuWrites((VRect){0,0,VRAM_W-1,VRAM_H-1});
    size_t bytes = (size_t)n * sizeof(GpuVertex);
    size_t spanBytes = (size_t)ints * 4;
    recording();
    if (frame()->vertexUsed + bytes + pendingBytes() > VERTEX_BYTES || frame()->spanUsed + spanBytes > SPAN_BYTES) {
        endBatch();
        submitFrame(NULL, NULL);
        recording();
    }
    if (!vk.rendering) {
        beginBatch();
    }
    FrameCtx* f = frame();
    uint32_t base = (uint32_t)(f->spanUsed / 16);
    memcpy(f->spans.map + f->spanUsed, spans, spanBytes);
    f->spanUsed += (spanBytes + 15) & ~(size_t)15;
    for (int i = 0; i < n; i++) {
        v[i].span = base;
    }
    memcpy(f->vertices.map + f->vertexUsed, v, bytes);
    f->vertexUsed += bytes;
}

void gvkBarrier(void) {
    endBatch();
}

int gvkNativeBarrier(void) {
    endNativeBatch();
    return vk.hiWideCount != 0 || vk.wideCount != 0;
}

static void uploadImage(VkImage image, VRect r, const uint16_t* src, int pitch);

void gvkUpload(VRect r, const uint16_t* src, int pitch) {
    if(src!=&vramShadow[r.y0][r.x0])vramFlushCpuWrites((VRect){0,0,VRAM_W-1,VRAM_H-1});
    uploadImage(vk.vram, r, src, pitch);
}

static void hiReplicate(int target, int srcKind, VRect r, int sx, int sy);

void gvkUploadWide(VRect r, const uint16_t* src, int pitch) {
    if (vk.device != VK_NULL_HANDLE) {
        wideTouch(r.x1);
        hiWideRows(r.y1+1);
        uploadImage(vk.wide, r, src, pitch);
        if (vk.hiN != 0) {
            hiReplicate(1, 1, r, r.x0, r.y0);
        }
    }
}

int gvkWideUsedWidth(void) {
    return vk.wideExtent;
}

void gvkRestoreWideWidth(int columns) {
    if (vk.device == VK_NULL_HANDLE) return;
    vk.wideExtent = columns < 0 ? 0 : columns > WIDE_W ? WIDE_W : columns;
    if (vk.wideExtent > 0) wideTouch(vk.wideExtent - 1);
}

void gvkRestoreWide(VRect r, const uint16_t* src, int pitch) {
    if (vk.device == VK_NULL_HANDLE) return;
    if (vk.hiWideCold) hiWideGrow(vk.hiWideColdH);
    uploadImage(vk.wide, r, src, pitch);
    /* An allocation from the abandoned timeline can be wider than the
     * restored extent. Refresh it too: a later write there need not grow
     * the image, and must see the restored native pixels. */
    if (vk.hiN != 0 && vk.hiWideW > r.x0 && vk.hiWideH > r.y0) {
        r.x1 = r.x1 < vk.hiWideW ? r.x1 : vk.hiWideW - 1;
        r.y1 = r.y1 < vk.hiWideH ? r.y1 : vk.hiWideH - 1;
        hiReplicate(1, 1, r, r.x0, r.y0);
    }
}

static void uploadImage(VkImage image, VRect r, const uint16_t* src, int pitch) {
    int w = r.x1 - r.x0 + 1;
    int h = r.y1 - r.y0 + 1;
    size_t bytes = ((size_t)w * h * 2 + 15) & ~(size_t)15;
    if (image == vk.vram || image == vk.scratch) endNativeBatch();
    else endBatch();
    recording();
    if (frame()->stagingUsed + bytes > STAGING_BYTES) {
        submitFrame(NULL, NULL);
    }
    VkCommandBuffer cmd = recording();
    FrameCtx* f = frame();
    uint8_t* dst = f->staging.map + f->stagingUsed;
    for (int y = 0; y < h; y++) {
        memcpy(dst + (size_t)y * w * 2, src + (size_t)y * pitch, (size_t)w * 2);
    }
    barrierIfNeeded(cmd);
    int q = gtStart(cmd, GT_COPY);
    VkBufferImageCopy c = {0};
    c.bufferOffset = f->stagingUsed;
    c.imageSubresource = colorLayers;
    c.imageOffset.x = r.x0;
    c.imageOffset.y = r.y0;
    c.imageExtent.width = (uint32_t)w;
    c.imageExtent.height = (uint32_t)h;
    c.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cmd, f->staging.buffer, image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    gtStop(cmd, q);
    f->stagingUsed += bytes;
    vk.needBarrier = 1;
}

/* Room for n more wide vertices (and span data) in this frame context. */
static void wideReserve(int n, size_t spanBytes) {
    size_t bytes = (size_t)n * sizeof(GpuVertex) + pendingBytes();
    if (vk.wideCount + (uint32_t)n > WIDE_MAX || frame()->vertexUsed + bytes > VERTEX_BYTES ||
        frame()->spanUsed + spanBytes > SPAN_BYTES) {
        endBatch();
        if (frame()->vertexUsed + (size_t)n * sizeof(GpuVertex) > VERTEX_BYTES ||
            frame()->spanUsed + spanBytes > SPAN_BYTES) {
            submitFrame(NULL, NULL);
        }
    }
    recording();
    if (!vk.rendering) {
        beginBatch();
    }
}

void gvkWideTouch(int x1) {
    if (vk.device != VK_NULL_HANDLE) {
        wideTouch(x1 < 0 ? 0 : x1 > WIDE_W - 1 ? WIDE_W - 1 : x1);
    }
}

void gvkDrawWide(const GpuVertex* v, int n) {
    wideReserve(n, 0);
    memcpy(&wideBuf[vk.wideCount], v, (size_t)n * sizeof(GpuVertex));
    vk.wideCount += (uint32_t)n;
}

void gvkDrawSpansWide(GpuVertex* v, int n, const int32_t* spans, int ints) {
    size_t spanBytes = (size_t)ints * 4;
    wideReserve(n, spanBytes);
    FrameCtx* f = frame();
    uint32_t base = (uint32_t)(f->spanUsed / 16);
    memcpy(f->spans.map + f->spanUsed, spans, spanBytes);
    f->spanUsed += (spanBytes + 15) & ~(size_t)15;
    for (int i = 0; i < n; i++) {
        v[i].span = base;
    }
    memcpy(&wideBuf[vk.wideCount], v, (size_t)n * sizeof(GpuVertex));
    vk.wideCount += (uint32_t)n;
}

static void copyImage(VkImage from, int fx, int fy, VkImage to, int tx, int ty, int w, int h) {
    if(from==vk.hiVram || to==vk.hiVram)cpuResolveFlush();
    int narrowFrom = from == vk.vram || from == vk.hiVram || from == vk.scratch || from == vk.hiScratch;
    int narrowTo = to == vk.vram || to == vk.hiVram || to == vk.scratch || to == vk.hiScratch;
    if (narrowFrom && narrowTo) endNativeBatch();
    else endBatch();
    VkCommandBuffer cmd = recording();
    barrierIfNeeded(cmd);
    int q = gtStart(cmd, GT_COPY);
    VkImageCopy c = {0};
    c.srcSubresource = colorLayers;
    c.srcOffset.x = fx;
    c.srcOffset.y = fy;
    c.dstSubresource = colorLayers;
    c.dstOffset.x = tx;
    c.dstOffset.y = ty;
    c.extent.width = (uint32_t)w;
    c.extent.height = (uint32_t)h;
    c.extent.depth = 1;
    vkCmdCopyImage(cmd, from, VK_IMAGE_LAYOUT_GENERAL, to, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    gtStop(cmd, q);
    vk.needBarrier = 1;
}

static void toGeneral(VkCommandBuffer cmd, VkImage image);

/* An RGBA8 image for transfers (blit source and destination), device-local. */
static int transferImage(VkImage* image, VkDeviceMemory* memory, int w, int h) {
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = (VkExtent3D){(uint32_t)w, (uint32_t)h, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk.device, &ii, NULL, image) != VK_SUCCESS) {
        *image = VK_NULL_HANDLE;
        return 0;
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk.device, *image, &req);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    VkResult allocationResult = vkAllocateMemory(vk.device, &ai, NULL, memory);
    if (allocationResult != VK_SUCCESS) {
        vkDestroyImage(vk.device, *image, NULL);
        *image = VK_NULL_HANDLE;
        return 0;
    }
    VK_CHECK(vkBindImageMemory(vk.device, *image, *memory, 0));
    return 1;
}

static void freeTransferImage(VkImage* image, VkDeviceMemory* memory) {
    if (*image != VK_NULL_HANDLE) {
        vkDestroyImage(vk.device, *image, NULL);
        vkFreeMemory(vk.device, *memory, NULL);
    }
    *image = VK_NULL_HANDLE;
    *memory = VK_NULL_HANDLE;
}

/* Part of a movie frame's picture into the hi-res copy (moviepack.c): the
 * frame occupies VRAM (fx, fy) to (fx + fw, fy + fh) (1x pixels), the
 * picture (w x h RGBA, `id`) covers it; the part over VRAM (x0, y0) to (x1,
 * y1) (inclusive: a slice just loaded) goes into the copy, stretched to N
 * times with a linear blit, the mask bit set where `mask`. The picture is
 * uploaded once (an RGBA8 image) and blitted into an RGBA8 image of the
 * frame's N-times size, whose part is then copied into the copy (its 8:8:8
 * words and the RGBA8 bytes have the same size; the A byte is the mask
 * bit, 24). Recorded in order with the uploads around it. */
int gvkHiPicture(int x0, int y0, int x1, int y1, const uint8_t* rgba, int w, int h, uint64_t id, int fx, int fy,
                 int fw, int fh, int mask) {
    int n = vk.hiVram != VK_NULL_HANDLE ? vk.hiN : 0;
    int dw = fw * n, dh = fh * n;
    if (n == 0 || w <= 0 || h <= 0 || fw <= 0 || fh <= 0 || x0 < fx || y0 < fy || x1 >= fx + fw || y1 >= fy + fh) {
        return 0;
    }
    endBatch();
    VkCommandBuffer cmd = recording();
    if (vk.movieHiSrcW != w || vk.movieHiSrcH != h || vk.movieHiDstW != dw || vk.movieHiDstH != dh) {
        presentDrain();
        vkDeviceWaitIdle(vk.device); /* nothing submitted still uses them */
        freeTransferImage(&vk.movieHiSrc, &vk.movieHiSrcMemory);
        freeTransferImage(&vk.movieHiDst, &vk.movieHiDstMemory);
        vk.movieHiSrcW = vk.movieHiSrcH = vk.movieHiDstW = vk.movieHiDstH = 0;
        vk.movieHiSrcId = 0;
        if (!transferImage(&vk.movieHiSrc, &vk.movieHiSrcMemory, w, h) ||
            !transferImage(&vk.movieHiDst, &vk.movieHiDstMemory, dw, dh)) {
            freeTransferImage(&vk.movieHiSrc, &vk.movieHiSrcMemory);
            return 0;
        }
        vk.movieHiSrcW = w, vk.movieHiSrcH = h, vk.movieHiDstW = dw, vk.movieHiDstH = dh;
        toGeneral(cmd, vk.movieHiSrc);
        toGeneral(cmd, vk.movieHiDst);
    }
    if (vk.movieHiSrcId != id || vk.movieHiSrcMask != mask) {
        if (frame()->movieHiUsed) {
            submitFrame(NULL, NULL); /* its upload buffer is in this recording already */
            cmd = recording();
        }
        FrameCtx* f = frame();
        size_t bytes = (size_t)w * h * 4;
        if (f->movieHiStagingBytes < bytes) {
            destroyHostBuffer(&f->movieHiStaging); /* this context's work is done (recording() waited) */
            createHostBuffer(&f->movieHiStaging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0);
            f->movieHiStagingBytes = bytes;
        }
        uint8_t* dst = f->movieHiStaging.map;
        uint8_t a = mask ? 1 : 0;
        for (size_t i = 0; i < (size_t)w * h; i++) {
            dst[i * 4] = rgba[i * 4], dst[i * 4 + 1] = rgba[i * 4 + 1], dst[i * 4 + 2] = rgba[i * 4 + 2];
            dst[i * 4 + 3] = a;
        }
        f->movieHiUsed = 1;
        fullBarrier(cmd); /* earlier blits from the image */
        VkBufferImageCopy c = {0};
        c.imageSubresource = colorLayers;
        c.imageExtent = (VkExtent3D){(uint32_t)w, (uint32_t)h, 1};
        vkCmdCopyBufferToImage(cmd, f->movieHiStaging.buffer, vk.movieHiSrc, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        vk.movieHiSrcId = id;
        vk.movieHiSrcMask = mask;
    }
    fullBarrier(cmd); /* the picture, earlier draws into the copy */
    vk.needBarrier = 0;
    int q = gtStart(cmd, GT_COPY);
    /* the part: in the frame's pixels, the picture's, the N-times image's */
    int px0 = x0 - fx, py0 = y0 - fy, px1 = x1 + 1 - fx, py1 = y1 + 1 - fy;
    VkImageBlit b = {0};
    b.srcSubresource = colorLayers;
    b.srcOffsets[0] = (VkOffset3D){(int32_t)((int64_t)px0 * w / fw), (int32_t)((int64_t)py0 * h / fh), 0};
    b.srcOffsets[1] = (VkOffset3D){(int32_t)((int64_t)px1 * w / fw), (int32_t)((int64_t)py1 * h / fh), 1};
    b.dstSubresource = colorLayers;
    b.dstOffsets[0] = (VkOffset3D){px0 * n, py0 * n, 0};
    b.dstOffsets[1] = (VkOffset3D){px1 * n, py1 * n, 1};
    vkCmdBlitImage(cmd, vk.movieHiSrc, VK_IMAGE_LAYOUT_GENERAL, vk.movieHiDst, VK_IMAGE_LAYOUT_GENERAL, 1, &b,
                   VK_FILTER_LINEAR);
    fullBarrier(cmd);
    VkImageCopy ic = {0};
    ic.srcSubresource = colorLayers;
    ic.srcOffset = (VkOffset3D){px0 * n, py0 * n, 0};
    ic.dstSubresource = colorLayers;
    ic.dstOffset = (VkOffset3D){x0 * n, y0 * n, 0};
    ic.extent = (VkExtent3D){(uint32_t)((px1 - px0) * n), (uint32_t)((py1 - py0) * n), 1};
    vkCmdCopyImage(cmd, vk.movieHiDst, VK_IMAGE_LAYOUT_GENERAL, vk.hiVram, VK_IMAGE_LAYOUT_GENERAL, 1, &ic);
    gtStop(cmd, q);
    vk.needBarrier = 1;
    return 1;
}

/* An upward overlapping display copy reads lower rows before they are
 * overwritten. Vulkan forbids a single self-copy with overlapping regions;
 * strips no taller than the Y displacement retain that authored order. */
static void copyDisplayImage(VkImage from, int sx, int sy, VkImage to, int dx, int dy, int w, int h) {
    if (from == to && dy < sy && dy + h > sy && dx < sx + w && sx < dx + w) {
        int step = sy - dy;
        for (int row = 0; row < h; row += step) {
            int rows = h - row < step ? h - row : step;
            copyImage(from,sx,sy+row,to,dx,dy+row,w,rows);
        }
    } else copyImage(from,sx,sy,to,dx,dy,w,h);
}

void gvkCopy(int from, int sx, int sy, int to, int dx, int dy, int w, int h) {
    if(!from || !to)vramFlushCpuWrites((VRect){0,0,VRAM_W-1,VRAM_H-1});
    if (from == 1 && to == 1) interpolationCopy(1,sx,sy,dx,dy,w,h);
    if (vk.device == VK_NULL_HANDLE || w <= 0 || h <= 0) {
        return;
    }
    if (to) {
        wideTouch(dx + w - 1);
        hiWideRows(dy+h);
    }
    if (from) hiWideRows(sy+h);
    copyDisplayImage(from ? vk.wide : vk.vram, sx, sy, to ? vk.wide : vk.vram, dx, dy, w, h);
    int n = vk.hiN;
    if (n != 0) {
        /* The copy's side: the same rectangle N times (a column of the wide
         * target past its copy holds nothing yet). */
        if (from && sx + w > vk.hiWideW) {
            w = vk.hiWideW - sx;
        }
        if (w > 0) {
            copyDisplayImage(from ? vk.hiWide : vk.hiVram, sx * n, sy * n, to ? vk.hiWide : vk.hiVram, dx * n, dy * n, w * n,
                      h * n);
        }
    }
}

void gvkReadWide(uint16_t* dst, int x, int y, int w, int h) {
    if ((size_t)w * h * 2 > READBACK_BYTES) {
        memset(dst, 0, (size_t)w * h * 2);
        return;
    }
    endBatch();
    VkCommandBuffer cmd = recording();
    barrierIfNeeded(cmd);
    VkBufferImageCopy c = {0};
    c.imageSubresource = colorLayers;
    c.imageOffset.x = x;
    c.imageOffset.y = y;
    c.imageExtent.width = (uint32_t)w;
    c.imageExtent.height = (uint32_t)h;
    c.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, vk.wide, VK_IMAGE_LAYOUT_GENERAL, vk.readback.buffer, 1, &c);
    hostReadBarrier(cmd);
    vk.needBarrier = 1;
    FrameCtx* f = frame();
    submitFrame(NULL, NULL);
    waitFrame(f);
    memcpy(dst, vk.readback.map, (size_t)w * h * 2);
}

void gvkCopyToScratch(VRect src, int sx, int sy) {
    vramFlushCpuWrites((VRect){0,0,VRAM_W-1,VRAM_H-1});
    copyImage(vk.vram, src.x0, src.y0, vk.scratch, sx, sy, src.x1 - src.x0 + 1, src.y1 - src.y0 + 1);
}

void gvkCopyFromScratch(int sx, int sy, VRect dst) {
    vramFlushCpuWrites((VRect){0,0,VRAM_W-1,VRAM_H-1});
    copyImage(vk.scratch, sx, sy, vk.vram, dst.x0, dst.y0, dst.x1 - dst.x0 + 1, dst.y1 - dst.y0 + 1);
}

/* ---- a picture between's device-side keep (libgpu.c pcVramKeepDevice, wide.c pcWideFrameKeep) ----
 * The 60 Hz Battling's picture between draws into the buffer on display and
 * takes the picture away again after the wait (between.c). Before it, the
 * buffer's native rectangle, the hi-res copy's N-times picture of it and the
 * wide picture's rows go to images of their own, and after it they come
 * back: device copies both ways, no readback, no wait, and the copy's detail
 * comes back as it was. The scratch image is taken by the between's own
 * moves. The images are made when first needed, as large as the largest
 * rectangle so far (the device idle while one is replaced); when one cannot
 * be made the caller keeps the pixels on the CPU as before. */
static void idleDevice(void);
static struct {
    VkImage vram, hi, wide, hiWide;
    VkDeviceMemory vramMemory, hiMemory, wideMemory, hiWideMemory;
    int vramW, vramH, hiW, hiH, wideW, wideH, hiWideW, hiWideH; /* the images' extents */
    VRect r;                                                    /* the native rectangle kept */
    int n, on;                                                  /* the copy's N at the keep */
    int wx, wy, ww, wh, whw, whh, wn, wideOn;                   /* the wide rows kept, their hi-res part (1x), N */
} keep;

/* An image for the keep, w x h at least, in VRAM's or the hi-res copy's
 * words; what it had is lost when it is made again. 0: the device has no
 * room (the image stays as it was, or stays unmade). */
static int keepImage(VkImage* image, VkDeviceMemory* memory, int* iw, int* ih, int w, int h, VkFormat format) {
    if (*image != VK_NULL_HANDLE && *iw >= w && *ih >= h) return 1;
    if (w <= 0 || h <= 0 || (uint32_t)w > vk.limits.maxImageDimension2D || (uint32_t)h > vk.limits.maxImageDimension2D) {
        return 0;
    }
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = (VkExtent3D){(uint32_t)w, (uint32_t)h, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage made;
    VkDeviceMemory mem;
    if (vkCreateImage(vk.device, &ii, NULL, &made) != VK_SUCCESS) return 0;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk.device, made, &req);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (vkAllocateMemory(vk.device, &ai, NULL, &mem) != VK_SUCCESS || vkBindImageMemory(vk.device, made, mem, 0) != VK_SUCCESS) {
        vkDestroyImage(vk.device, made, NULL);
        return 0;
    }
    if (*image != VK_NULL_HANDLE) {
        idleDevice(); /* the last keep's copies may still be in flight */
        vkDestroyImage(vk.device, *image, NULL);
        vkFreeMemory(vk.device, *memory, NULL);
    }
    *image = made;
    *memory = mem;
    *iw = w;
    *ih = h;
    VkCommandBuffer cmd = recording();
    toGeneral(cmd, made);
    vk.needBarrier = 1;
    return 1;
}

int gvkKeepFrame(VRect r) {
    int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1, n = vk.hiN;
    keep.on = 0;
    if (vk.device == VK_NULL_HANDLE || w <= 0 || h <= 0) return 0;
    if (!keepImage(&keep.vram, &keep.vramMemory, &keep.vramW, &keep.vramH, w, h, VK_FORMAT_R16_UINT)) return 0;
    if (n != 0 && !keepImage(&keep.hi, &keep.hiMemory, &keep.hiW, &keep.hiH, w * n, h * n, VK_FORMAT_R32_UINT)) return 0;
    vramFlushCpuWrites((VRect){0, 0, VRAM_W - 1, VRAM_H - 1}); /* the CPU's pending tiles are VRAM's */
    copyImage(vk.vram, r.x0, r.y0, keep.vram, 0, 0, w, h);
    if (n != 0) copyImage(vk.hiVram, r.x0 * n, r.y0 * n, keep.hi, 0, 0, w * n, h * n);
    keep.r = r;
    keep.n = n;
    keep.on = 1;
    return 1;
}

void gvkKeepFramePutBack(void) {
    if (!keep.on) return;
    keep.on = 0;
    if (vk.device == VK_NULL_HANDLE) return;
    VRect r = keep.r;
    int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1, n = keep.n;
    /* the between's CPU-drawn tiles land first; the kept pixels go over them */
    vramFlushCpuWrites((VRect){0, 0, VRAM_W - 1, VRAM_H - 1});
    copyImage(keep.vram, 0, 0, vk.vram, r.x0, r.y0, w, h);
    if (n != 0 && n == vk.hiN && vk.hiVram != VK_NULL_HANDLE) {
        copyImage(keep.hi, 0, 0, vk.hiVram, r.x0 * n, r.y0 * n, w * n, h * n);
    }
}

int gvkKeepWide(int x, int y, int w, int h) {
    keep.wideOn = 0;
    if (vk.device == VK_NULL_HANDLE || vk.wide == VK_NULL_HANDLE || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > WIDE_W ||
        y + h > WIDE_H) {
        return 0;
    }
    int n = vk.hiN;
    if (n != 0) hiWideRows(y + h); /* the rows resident in the copy, as a wide copy has them */
    n = vk.hiN;                    /* growth may have turned upscaling off */
    int hw = 0, hh = 0;
    if (n != 0 && vk.hiWide != VK_NULL_HANDLE) {
        hw = x + w > vk.hiWideW ? vk.hiWideW - x : w;
        hh = y + h > vk.hiWideH ? vk.hiWideH - y : h;
        if (hw <= 0 || hh <= 0) hw = hh = 0;
    }
    if (!keepImage(&keep.wide, &keep.wideMemory, &keep.wideW, &keep.wideH, w, h, VK_FORMAT_R16_UINT)) return 0;
    if (hw != 0 && !keepImage(&keep.hiWide, &keep.hiWideMemory, &keep.hiWideW, &keep.hiWideH, hw * n, hh * n, VK_FORMAT_R32_UINT)) {
        return 0;
    }
    copyImage(vk.wide, x, y, keep.wide, 0, 0, w, h);
    if (hw != 0) copyImage(vk.hiWide, x * n, y * n, keep.hiWide, 0, 0, hw * n, hh * n);
    keep.wx = x;
    keep.wy = y;
    keep.ww = w;
    keep.wh = h;
    keep.whw = hw;
    keep.whh = hh;
    keep.wn = n;
    keep.wideOn = 1;
    return 1;
}

void gvkKeepWidePutBack(void) {
    if (!keep.wideOn) return;
    keep.wideOn = 0;
    if (vk.device == VK_NULL_HANDLE || vk.wide == VK_NULL_HANDLE) return;
    int n = keep.wn;
    wideTouch(keep.wx + keep.ww - 1);
    copyImage(keep.wide, 0, 0, vk.wide, keep.wx, keep.wy, keep.ww, keep.wh);
    if (keep.whw != 0 && n == vk.hiN && vk.hiWide != VK_NULL_HANDLE) {
        hiWideRows(keep.wy + keep.whh);
        if (n == vk.hiN && keep.wx + keep.whw <= vk.hiWideW && keep.wy + keep.whh <= vk.hiWideH) {
            copyImage(keep.hiWide, 0, 0, vk.hiWide, keep.wx * n, keep.wy * n, keep.whw * n, keep.whh * n);
        }
    }
}

static void keepDestroy(void) {
    VkImageView none = VK_NULL_HANDLE;
    destroyImage(&keep.vram, &keep.vramMemory, &none);
    destroyImage(&keep.hi, &keep.hiMemory, &none);
    destroyImage(&keep.wide, &keep.wideMemory, &none);
    destroyImage(&keep.hiWide, &keep.hiWideMemory, &none);
    memset(&keep, 0, sizeof(keep));
}

/* ---- the hi-res copy (hires.c) -------------------------------------------------------
 *
 * VRAM and the wide target N times, as R32 images (8:8:8 colour and the mask
 * bit 24). Every draw batch gets two more passes (hires.frag: the copy's
 * triangles, rasterised by the device), every transfer its scaled twin, and
 * pictures that only exist at 1x (uploads, the CPU's) are replicated into it
 * (hires_util.frag). The wide target's copy is made only as wide as the
 * columns the wide target has used (wideExtent), and grows with them, by at
 * least 128 more columns at a time. */

typedef struct {
    int32_t n, color;
    int32_t targetW, targetH;
    int32_t wideW, wideH;
    int32_t ditherX, ditherY; /* owned replay origin, in 1x target pixels */
    int32_t coarse; /* 2: each fragment a 2x2 block (hiBatchCoarse); 0 or 1: a pixel */
} HiDrawConstants;

typedef struct {
    int32_t n, op, target, srcKind;
    int32_t dst0[2], src0[2], ref0[2];
    int32_t refKind, hiKind;
    int32_t hi0[2];
    int32_t flags;
} HiUtilConstants;

enum { HI_OP_REPLICATE, HI_OP_LOAD, HI_OP_MASK, HI_OP_BLIT, HI_OP_BLUR };

static void interpolationSets(void) {
    for (int k = 0; k < FRAMES; k++) {
        FrameCtx* f = &vk.frames[k];
        for (int s = 0; s < INTERP_SETS; s++) {
            /* Only the native VRAM/wide target and this context's span buffer are
             * shared. Unused bindings use stable images/buffers, so a
             * native hi-res/pack resource resize cannot leave stale views. */
            VkCopyDescriptorSet copy[2];
            for (int b = 0; b < 2; b++) {
                copy[b] = (VkCopyDescriptorSet){.sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};
                copy[b].srcSet = f->set; copy[b].srcBinding = (uint32_t)b;
                copy[b].dstSet = f->interpolationSet[s]; copy[b].dstBinding = (uint32_t)b;
                copy[b].descriptorCount = 1;
            }
            vkUpdateDescriptorSets(vk.device, 0, NULL, 2, copy);
            VkImageView targetView = s>=INTERP_FRAMES+2 ? vk.interpolationFrozenView[s-INTERP_FRAMES-2] : s==INTERP_FRAMES+1 ? vk.interpolationSourceView : vk.interpolationView[s];
            VkDescriptorImageInfo target = {VK_NULL_HANDLE,targetView?targetView:vk.dummy32View,VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo cache = {VK_NULL_HANDLE, vk.interpolationCacheView?vk.interpolationCacheView:vk.dummy32View, VK_IMAGE_LAYOUT_GENERAL};
            VkImageView sourceView=s==INTERP_FRAMES+1?vk.interpolationView[INTERP_FRAMES]:vk.interpolationSourceView;
            VkDescriptorImageInfo source = {VK_NULL_HANDLE,sourceView?sourceView:vk.dummy32View,VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo unused16 = {VK_NULL_HANDLE, vk.scratchView, VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo move = {VK_NULL_HANDLE,vk.hiScratchView?vk.hiScratchView:vk.dummy32View,VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorImageInfo wide16 = {VK_NULL_HANDLE, vk.wideView, VK_IMAGE_LAYOUT_GENERAL};
            VkDescriptorBufferInfo spans = {f->spans.buffer, 0, VK_WHOLE_SIZE};
            VkWriteDescriptorSet w[8];
            for (int b = 2; b <= 8; b++) {
                w[b - 2] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                w[b - 2].dstSet = f->interpolationSet[s]; w[b - 2].dstBinding = (uint32_t)b;
                w[b - 2].descriptorCount = 1;
                w[b - 2].descriptorType = b == 8 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                w[b - 2].pImageInfo = b == 2 ? &wide16 : b == 4 ? &source : b == 5 ? &unused16 : b == 6 ? &cache : b == 7 ? &move : &target;
                if (b == 8) { w[b - 2].pImageInfo = NULL; w[b - 2].pBufferInfo = &spans; }
            }
            if (s >= INTERP_FRAMES + 1) { vkUpdateDescriptorSets(vk.device,7,w,0,NULL); continue; }
            w[7] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w[7].dstSet = f->interpolationPicture[s]; w[7].descriptorCount = 1;
            w[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[7].pImageInfo = &target;
            vkUpdateDescriptorSets(vk.device, 8, w, 0, NULL);
        }
    }
}

static int interpolationImages(int w, int h, int n, int wide, int capture, int fw, int fh, int mw, int mh);
static void idleDevice(void);
static void interpolationPassClip(VkCommandBuffer cmd, VkDescriptorSet set, VkPipeline pipeline,
                              const void* constants, int bytes, int w, int h, uint32_t first, uint32_t count, const InterpFrame* replay, const VRect* clip) {
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.extent = (VkExtent2D){(uint32_t)w, (uint32_t)h}; ri.layerCount = 1;
    vkCmdBeginRendering(cmd, &ri);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkDescriptorSet sets[2] = {set, vk.effectSet};
    /* The utility shader declares its source in set 1 even for a native
     * replicate pass. Bind it explicitly, rather than inherit an old draw. */
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.hiLayout, 0,
                            pipeline == vk.hiUtilPipeline ? 2 : 1, sets, 0, NULL);
    VkViewport viewport = {0, 0, (float)w, (float)h, 0, 1};
    if (replay && (replay->capture || replay->frozen)) {
        const HiDrawConstants* c = constants;
        viewport.width = (float)c->targetW; viewport.height = (float)c->targetH;
    }
    VkRect2D scissor = {{0, 0}, {(uint32_t)w, (uint32_t)h}};
    if (clip) {
        int n = ((const HiDrawConstants*)constants)->n;
        scissor.offset = (VkOffset2D){clip->x0*n,clip->y0*n};
        scissor.extent = (VkExtent2D){(uint32_t)(clip->x1-clip->x0+1)*n,(uint32_t)(clip->y1-clip->y0+1)*n};
    }
    vkCmdSetViewport(cmd, 0, 1, &viewport); vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdPushConstants(cmd, vk.hiLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, (uint32_t)bytes, constants);
    vkCmdDraw(cmd, count, 1, first, 0);
    vkCmdEndRendering(cmd);
}

static void interpolationPass(VkCommandBuffer cmd, VkDescriptorSet set, VkPipeline pipeline,
                              const void* constants, int bytes, int w, int h, uint32_t first, uint32_t count, const InterpFrame* replay) {
    interpolationPassClip(cmd,set,pipeline,constants,bytes,w,h,first,count,replay,NULL);
}

int gvkHiScale(void) {
    return vk.hiN;
}

int gvkHiWideWidth(void) {
    return vk.hiWideW;
}
int gvkHiWideHeight(void) { return vk.hiWideH; }

void gvkHiLimits(int* maxDim, uint64_t* heapBytes) {
    uint32_t d = vk.limits.maxImageDimension2D;
    uint32_t lim[4] = {vk.limits.maxFramebufferWidth, vk.limits.maxFramebufferHeight, vk.limits.maxViewportDimensions[0],
                       vk.limits.maxViewportDimensions[1]};
    for (int i = 0; i < 4; i++) {
        d = lim[i] < d ? lim[i] : d;
    }
    *maxDim = (int)d;
    uint64_t heap = 0;
    for (uint32_t i = 0; i < vk.memProps.memoryHeapCount; i++) {
        if ((vk.memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) && vk.memProps.memoryHeaps[i].size > heap) {
            heap = vk.memProps.memoryHeaps[i].size;
        }
    }
    *heapBytes = heap;
}

/* An R32_UINT storage image and its view; 0 when the selected heap
 * cannot hold it (nothing is left made). */
static int createImage32Memory(VkImage* image, VkDeviceMemory* memory, VkImageView* view, int w, int h, int system) {
    *image = VK_NULL_HANDLE;
    *memory = VK_NULL_HANDLE;
    *view = VK_NULL_HANDLE;
    if (w <= 0 || h <= 0 || (uint32_t)w > vk.limits.maxImageDimension2D || (uint32_t)h > vk.limits.maxImageDimension2D) {
        return 0;
    }
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R32_UINT;
    ii.extent.width = (uint32_t)w;
    ii.extent.height = (uint32_t)h;
    ii.extent.depth = 1;
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk.device, &ii, NULL, image) != VK_SUCCESS) {
        *image = VK_NULL_HANDLE;
        return 0;
    }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk.device, *image, &req);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (system) {
        /* Cold immutable endpoints are copied once and sampled only at a
         * timeline edge. Keep their exact optimal-tiled image format while
         * leaving scarce device memory to the native and replay canvases.
         * Unified devices simply retain their normal device-local type. */
        for (int pass = 0; pass < 3; pass++) {
            VkMemoryPropertyFlags want = pass == 2 ? 0 : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                (pass ? 0 : VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
            uint32_t found = UINT32_MAX;
            for (uint32_t i = 0; i < vk.memProps.memoryTypeCount; i++) {
                VkMemoryPropertyFlags flags = vk.memProps.memoryTypes[i].propertyFlags;
                if ((req.memoryTypeBits & (1u << i)) && !(flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
                    (flags & want) == want) { found = i; break; }
            }
            if (found != UINT32_MAX) { ai.memoryTypeIndex = found; break; }
        }
        /* Images are never host-mapped. A compatible GPU-only system type
         * is sufficient even when optimal images exclude HOST_VISIBLE. */
    }
    VkResult allocationResult = vkAllocateMemory(vk.device, &ai, NULL, memory);
    if (allocationResult != VK_SUCCESS) {
        vkDestroyImage(vk.device, *image, NULL);
        *image = VK_NULL_HANDLE;
        *memory = VK_NULL_HANDLE;
        return 0;
    }
    VK_CHECK(vkBindImageMemory(vk.device, *image, *memory, 0));
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = *image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R32_UINT;
    vi.subresourceRange = colorRange;
    VK_CHECK(vkCreateImageView(vk.device, &vi, NULL, view));
    return 1;
}

static int createImage32(VkImage* image, VkDeviceMemory* memory, VkImageView* view, int w, int h) {
    return createImage32Memory(image,memory,view,w,h,0);
}

static int createNativeImage32(VkImage* image, VkDeviceMemory* memory, VkImageView* view, int w, int h) {
    if (!envFlag("XG_TEST_NATIVE_IMAGE_SYSTEM") && createImage32(image,memory,view,w,h)) return 1;
    if (!createImage32Memory(image,memory,view,w,h,1)) return 0;
    gpuLog("hires: %dx%d native storage in compatible system memory; retaining every enhanced pixel",w,h);
    return 2;
}

static void destroyImage(VkImage* image, VkDeviceMemory* memory, VkImageView* view) {
    if (*view != VK_NULL_HANDLE) {
        vkDestroyImageView(vk.device, *view, NULL);
    }
    if (*image != VK_NULL_HANDLE) {
        vkDestroyImage(vk.device, *image, NULL);
    }
    if (*memory != VK_NULL_HANDLE) {
        vkFreeMemory(vk.device, *memory, NULL);
    }
    *view = VK_NULL_HANDLE;
    *image = VK_NULL_HANDLE;
    *memory = VK_NULL_HANDLE;
}

int gvkInterpolationReady(void) {
    int n = vk.hiN ? vk.hiN : 1;
    if (vk.interpolationRetry && vk.interpolationFailedN == n) {
        --vk.interpolationRetry;
        return 0;
    }
    vk.interpolationRetry = 0;
    return 1;
}

static int interpolationImages(int w, int h, int n, int wide, int capture, int fw, int fh, int mw, int mh) {
    int cw = capture ? (wide ? (vk.hiN ? vk.hiWideW : WIDE_W) : VRAM_W) : w;
    /* Keep the authored raster coordinates, but allocate only the two field
     * slots. The scratch strips stay beside them, below row512. */
    int ch = capture ? (wide ? VRAM_H+256 : VRAM_H) : fw ? VRAM_H : h;
    /* Wide replay owns a compact strip atlas; native x=960 scratch does
     * not require a full VRAM-width enhanced image. */
    if (cw < fw) cw = fw;
    if (ch < fh) ch = fh;
    /* A wide ordered step reads one wrapped 8-bit texture window at a time.
     * Rebase its owned copy to (0,0), rather than retaining empty framebuffer
     * rows around it. Frozen captures already use local coordinates. Native
     * cache-prefix replay still requires the complete source canvas. */
    int sw = wide ? (fw > 256 ? fw : 256) : cw;
    int sh = wide ? (fh > 256 ? fh : 256) : ch;
    int spill = vk.env.interpolationSpill && vk.hiWide && !vk.hiWideSystem;
    /* A larger move needs only a larger scratch image, whose contents never
     * outlive one replay: grow it alone (or rebuild everything when even
     * that fails) and keep the captured endpoints. */
    if (!spill && vk.interpolationW == w && vk.interpolationH == h && vk.interpolationN == n && vk.interpolationCanvasW >= cw && vk.interpolationCanvasH >= ch && vk.interpolationSourceW >= sw && vk.interpolationSourceH >= sh && hiScratchFor(mw*n,mh*n)) return 1;
    idleDevice();
    int changed = vk.interpolationW != 0;
    for (int i = 0; i <= INTERP_FRAMES; i++)
        destroyImage(&vk.interpolationImage[i], &vk.interpolationMemory[i], &vk.interpolationView[i]);
    destroyImage(&vk.interpolationCache, &vk.interpolationCacheMemory, &vk.interpolationCacheView);
    destroyImage(&vk.interpolationSource, &vk.interpolationSourceMemory, &vk.interpolationSourceView);
    vk.interpolationW = vk.interpolationH = vk.interpolationN = 0;
    vk.interpolationCanvasW = cw; vk.interpolationCanvasH = ch;
    vk.interpolationSourceW = sw; vk.interpolationSourceH = sh;
    const char* failText = getenv("XG_TEST_INTERPOLATION_ALLOC_FAIL");
    int failStage = failText ? atoi(failText) : 0;
    if (spill) goto failed;
    if (!createImage32(&vk.interpolationCache, &vk.interpolationCacheMemory, &vk.interpolationCacheView,5,257) ||
        failStage == 1 ||
        !hiScratchFor(mw*n,mh*n) ||
        !createImage32(&vk.interpolationImage[INTERP_FRAMES], &vk.interpolationMemory[INTERP_FRAMES],
                       &vk.interpolationView[INTERP_FRAMES], cw*n, ch*n)) {
        goto failed;
    }
    /* Ordered self-feedback reads the source repeatedly during replay.
     * Keep it with the output and copy scratch on the device; immutable
     * endpoint and frozen-version storage below can use system memory. */
    if (!createImage32(&vk.interpolationSource,&vk.interpolationSourceMemory,&vk.interpolationSourceView,sw*n,sh*n)) goto failed;
    if (failStage == 2) goto failed;
    for (int i = 0; i < INTERP_FRAMES; i++) {
        vk.interpolationSystem[i] = 0;
        if (failStage == 3 && i == 3) goto failed;
        if (!createImage32(&vk.interpolationImage[i], &vk.interpolationMemory[i], &vk.interpolationView[i], w*n, h*n)) {
            vk.interpolationSystem[i] = 1;
            if (!createImage32Memory(&vk.interpolationImage[i], &vk.interpolationMemory[i], &vk.interpolationView[i], w*n, h*n, 1)) goto failed;
        }
    }
    VkCommandBuffer cmd = recording();
    for (int i = 0; i <= INTERP_FRAMES; i++)
        imageBarrier(cmd, vk.interpolationImage[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                     VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                     VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    imageBarrier(cmd, vk.interpolationCache, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                 VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    imageBarrier(cmd, vk.interpolationSource, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                 VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    vk.interpolationRetry = 0;
    vk.interpolationW = w; vk.interpolationH = h; vk.interpolationN = n;
    interpolationSets();
    if (changed) interpolationReset();
    return 1;
failed:
    for (int i = 0; i <= INTERP_FRAMES; i++)
        destroyImage(&vk.interpolationImage[i], &vk.interpolationMemory[i], &vk.interpolationView[i]);
    destroyImage(&vk.interpolationCache, &vk.interpolationCacheMemory, &vk.interpolationCacheView);
    destroyImage(&vk.interpolationSource, &vk.interpolationSourceMemory, &vk.interpolationSourceView);
    /* Reserve room for the replay canvases before abandoning smoothing.
     * A complete lossless native image can use a compatible system heap.
     * The residency flag limits this retry to one spill, never a loop. */
    if (!failStage && hiWideToSystem()) {
        if (changed) interpolationReset();
        return interpolationImages(w,h,n,wide,capture,fw,fh,mw,mh);
    }
    interpolationReset();
    vk.interpolationRetry = 60; vk.interpolationFailedN = n;
    gpuWarnOnce("interpolation-memory", "interpolation: capture workspace unavailable at %dx; retrying after 60 field frames", n);
    return 0;
}

void gvkInterpolationCapture(int slot, const InterpFrame* f) {
    if (vk.device == VK_NULL_HANDLE || slot < 0 || slot >= INTERP_FRAMES) return;
    cpuResolveFlush();
    vramFlushCpuWrites((VRect){0,0,VRAM_W-1,VRAM_H-1});
    /* Grow first: under memory pressure growth can turn upscaling off. */
    if (f->wide) hiWideRows(f->viewY+f->viewH);
    int n = vk.hiN ? vk.hiN : 1;
    int fw = f->frozen ? f->frozenRect.x1-f->frozenRect.x0+1 : 0;
    int fh = f->frozen ? f->frozenRect.y1-f->frozenRect.y0+1 : 0;
    /* Copy scratch uses local coordinates. Only its largest actual move
     * needs storage; it does not need the full capture atlas. */
    int mw = 1, mh = 1;
    for (uint32_t i = 0; i < f->opCount; i++) if (f->ops[i].kind == INTERP_OP_COPY) {
        if (f->ops[i].w > mw) mw = f->ops[i].w;
        if (f->ops[i].h > mh) mh = f->ops[i].h;
    }
    /* capture.frag also uses the move image for native cache samples,
     * addressed by destination pixel rather than by copy-local coordinates.
     * A strong wave can reach below every copy, and frozen transitions may
     * have no copies at all. Cover their clipped native destinations too;
     * the allocation is scaled, while these temporary samples are not. */
    if (!f->wide && (f->capture || f->frozenWrites)) {
        for (uint32_t i = 0; i < f->vertexCount; i++) {
            const HiVertex* v = &f->vertices[i];
            if (!(v->sample[3] & INTERP_SAMPLE_CAPTURE)) continue;
            int w = ((int)(v->clipMax & 65535) + f->viewX + n) / n;
            int h = ((int)(v->clipMax >> 16) + f->viewY + n) / n;
            if (w > mw) mw = w;
            if (h > mh) mh = h;
        }
    }
    if (!interpolationImages(f->viewW, f->viewH, n, f->wide, f->capture, fw, fh, mw, mh)) return;
    endBatch();
    if (vk.hiN) {
        if (f->wide && (vk.hiWide == VK_NULL_HANDLE || f->viewX + f->viewW > vk.hiWideW)) {
            interpolationReset(); return;
        }
        copyImage(f->wide ? vk.hiWide : vk.hiVram, f->viewX * n, f->viewY * n,
                  vk.interpolationImage[slot], 0, 0, f->viewW * n, f->viewH * n);
    } else {
        VkCommandBuffer cmd = recording();
        fullBarrier(cmd);
        HiUtilConstants c = {0}; c.n = 1; c.op = HI_OP_REPLICATE;
        c.srcKind = f->wide;
        c.src0[0] = f->viewX; c.src0[1] = f->viewY;
        interpolationPass(cmd, frame()->interpolationSet[slot], vk.hiUtilPipeline, &c, sizeof(c), f->viewW, f->viewH, 0, 3, NULL);
        vk.needBarrier = 1;
    }
}

int gvkInterpolationFreeze(int slot, int version, VRect rect, int wide) {
    if (vk.device==VK_NULL_HANDLE || slot<0 || slot>=INTERP_FRAMES || version<0 || version>=INTERP_FREEZES) return 0;
    if (wide) hiWideRows(rect.y1+1); /* first: growth can turn upscaling off */
    int k=slot*INTERP_FREEZES+version,n=vk.hiN?vk.hiN:1,w=rect.x1-rect.x0+1,h=rect.y1-rect.y0+1;
    if (vk.interpolationFrozenW[k]!=w || vk.interpolationFrozenH[k]!=h || vk.interpolationFrozenN[k]!=n) {
        idleDevice();
        destroyImage(&vk.interpolationFrozen[k],&vk.interpolationFrozenMemory[k],&vk.interpolationFrozenView[k]);
        /* System memory keeps scarce device memory for the canvases, but a
         * fade-in freezes a 64 MB capture per native frame at 12x: copying
         * them out over the bus and sampling them back held the GPU at 11 ms
         * a vblank for the fade (xg-lla). While the device heap has room
         * (deviceHeapPercent, where the budget is known) they stay there. */
        VkDeviceSize bytes=(VkDeviceSize)w*n*h*n*4;
        int percent=deviceHeapPercent(bytes), local=percent>=0 && percent<HI_TRIM_HEAP_PERCENT;
        if (!(local && createImage32(&vk.interpolationFrozen[k],&vk.interpolationFrozenMemory[k],&vk.interpolationFrozenView[k],w*n,h*n)) &&
            !createImage32Memory(&vk.interpolationFrozen[k],&vk.interpolationFrozenMemory[k],&vk.interpolationFrozenView[k],w*n,h*n,1)) return 0;
        if (local && !vk.frozenLocalLogged) { vk.frozenLocalLogged=1; gpuLog("interpolation: frozen captures in device memory (heap at %d%% of its budget with one)",percent); }
        vk.interpolationFrozenW[k]=w; vk.interpolationFrozenH[k]=h; vk.interpolationFrozenN[k]=n;
        imageBarrier(recording(),vk.interpolationFrozen[k],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_NONE,0,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,VK_ACCESS_2_MEMORY_READ_BIT|VK_ACCESS_2_MEMORY_WRITE_BIT);
        interpolationSets();
    }
    endBatch();
    if (vk.hiN) copyImage(wide?vk.hiWide:vk.hiVram,rect.x0*n,rect.y0*n,vk.interpolationFrozen[k],0,0,w*n,h*n);
    else {
        VkCommandBuffer cmd=recording(); fullBarrier(cmd);
        HiUtilConstants c={0}; c.n=1; c.op=HI_OP_REPLICATE; c.srcKind=wide; c.src0[0]=rect.x0; c.src0[1]=rect.y0;
        interpolationPass(cmd,frame()->interpolationSet[INTERP_FRAMES+2+k],vk.hiUtilPipeline,&c,sizeof(c),w,h,0,3,NULL);
        vk.needBarrier=1;
    }
    return 1;
}

typedef struct {
    int32_t n, action, first, seed;
    int32_t source[2];
    uint32_t mode, window;
    int32_t feedback, frozen;
    int32_t readOffset[2];
} CaptureConstants;

static int interpolationFeedback(const HiVertex* v, VRect box) {
    int u0 = (int)fminf(v[0].u,fminf(v[1].u,v[2].u));
    int u1 = (int)fmaxf(v[0].u,fmaxf(v[1].u,v[2].u));
    int v0 = (int)fminf(v[0].v,fminf(v[1].v,v[2].v));
    int v1 = (int)fmaxf(v[0].v,fmaxf(v[1].v,v[2].v));
    if (v->texWindow || u0 < 0 || u1 > 255 || v0 < 0 || v1 > 255) { u0=v0=0; u1=v1=255; }
    else { u0=u0>0?u0-1:0; u1=u1<255?u1+1:255; v0=v0>0?v0-1:0; v1=v1<255?v1+1:255; }
    VPiece pieces[4];
    int n = vramWrap((int16_t)v->sample[0]+u0,(int16_t)(v->sample[0]>>16)+v0,u1-u0+1,v1-v0+1,pieces);
    for (int i = 0; i < n; i++) {
        VRect r = pieces[i].r;
        if (r.x0 <= box.x1 && box.x0 <= r.x1 && r.y0 <= box.y1 && box.y0 <= r.y1) return 1;
    }
    return 0;
}

/* The same native 128-halfword burst dependencies as gvkHiMove. Wide moves
 * already have snapshot semantics; both paths use only private images. */
/* Private replay images are touched only by storage-image shaders and
 * transfers. Keep vertex fetch and unrelated colour output outside these
 * dependencies; neither reads nor writes the replay workspace. */
static void interpolationBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier2 mb={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask=mb.dstStageMask=VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    mb.srcAccessMask=VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstAccessMask=VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
        VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    VkDependencyInfo dep={.sType=VK_STRUCTURE_TYPE_DEPENDENCY_INFO,.memoryBarrierCount=1,.pMemoryBarriers=&mb};
    vkCmdPipelineBarrier2(cmd,&dep);
}
static void interpolationCopyImage(VkImage from,int fx,int fy,VkImage to,int tx,int ty,int w,int h) {
    VkCommandBuffer cmd=recording();interpolationBarrier(cmd);int q=gtStart(cmd,GT_COPY);
    VkImageCopy r={.srcSubresource=colorLayers,.srcOffset={fx,fy,0},.dstSubresource=colorLayers,
        .dstOffset={tx,ty,0},.extent={w,h,1}};
    vkCmdCopyImage(cmd,from,VK_IMAGE_LAYOUT_GENERAL,to,VK_IMAGE_LAYOUT_GENERAL,1,&r);gtStop(cmd,q);
    vk.needBarrier=1;
}

static void interpolationCopyReplay(VkCommandBuffer cmd, VkImage target, const InterpOp* op, int n, int wide) {
    if (wide && (op->sx+op->w<=op->dx || op->dx+op->w<=op->sx ||
                 op->sy+op->h<=op->dy || op->dy+op->h<=op->sy)) {
        /* Vulkan permits disjoint regions of the same image. Saved wave
         * strips are outside the display and need no intermediate copy. */
        interpolationCopyImage(target,op->sx*n,op->sy*n,target,op->dx*n,op->dy*n,op->w*n,op->h*n);
    } else if (wide) {
        interpolationCopyImage(target,op->sx*n,op->sy*n,vk.hiScratch,0,0,op->w*n,op->h*n);
        interpolationCopyImage(vk.hiScratch,0,0,target,op->dx*n,op->dy*n,op->w*n,op->h*n);
    } else {
        uint16_t step[VRAM_H*(VRAM_W/COPY_PIECE)];
        VRect rects[VRAM_H*(VRAM_W/COPY_PIECE)];
        int steps = vramCopySteps(op->sx,op->sy,op->dx,op->dy,op->w,op->h,step);
        for (int s = 0; s < steps; s++) {
            int nr = vramCopyStepRects(step,op->w,op->h,s,rects);
            for (int k = 0; k < nr; k++) {
                VRect r = rects[k]; VPiece p[4];
                int count = vramWrap(op->sx+r.x0,op->sy+r.y0,r.x1-r.x0+1,r.y1-r.y0+1,p);
                for (int i = 0; i < count; i++) interpolationCopyImage(target,p[i].r.x0*n,p[i].r.y0*n,vk.hiScratch,
                    (r.x0+p[i].dx)*n,(r.y0+p[i].dy)*n,(p[i].r.x1-p[i].r.x0+1)*n,(p[i].r.y1-p[i].r.y0+1)*n);
            }
            for (int k = 0; k < nr; k++) {
                VRect r = rects[k]; VPiece p[4];
                int count = vramWrap(op->dx+r.x0,op->dy+r.y0,r.x1-r.x0+1,r.y1-r.y0+1,p);
                for (int i = 0; i < count; i++) interpolationCopyImage(vk.hiScratch,(r.x0+p[i].dx)*n,(r.y0+p[i].dy)*n,target,
                    p[i].r.x0*n,p[i].r.y0*n,(p[i].r.x1-p[i].r.x0+1)*n,(p[i].r.y1-p[i].r.y0+1)*n);
            }
        }
    }
    interpolationBarrier(cmd);
}

static int interpolationRunVertices(const HiVertex* vertices, unsigned count, VkBuffer* outBuffer, VkDeviceSize* outOffset) {
    FrameCtx* ctx=frame();size_t bytes=count*sizeof(HiVertex);int chunk=0;
    while(chunk<16 && ctx->interpolationRunUsed[chunk]+bytes>VERTEX_BYTES)chunk++;
    if(chunk==16)return 0;
    HostBuffer* buffer=&ctx->interpolationRuns[chunk];
    if(buffer->buffer==VK_NULL_HANDLE)createHostBuffer(buffer,VERTEX_BYTES,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,0);
    VkDeviceSize offset=ctx->interpolationRunUsed[chunk];
    memcpy(buffer->map+offset,vertices,bytes);ctx->interpolationRunUsed[chunk]+=bytes;
    *outBuffer=buffer->buffer;*outOffset=offset;return 1;
}
static void interpolationLocalSource(HiVertex* v, VRect source) {
    int x = (int16_t)v->sample[0] - source.x0;
    int y = (int16_t)(v->sample[0] >> 16) - source.y0;
    v->sample[0] = (uint32_t)(uint16_t)x | (uint32_t)(uint16_t)y << 16;
    v->sample[3] |= INTERP_SAMPLE_STEP_SOURCE;
}
static int interpolationRun(const HiVertex* v,const float* plane,VRect source,int x0,int y0,int x1,int y1,
                            VkBuffer* outBuffer, VkDeviceSize* outOffset) {
    HiVertex q[6];float x[4]={(float)x0,(float)x1,(float)x0,(float)x1},y[4]={(float)y0,(float)y0,(float)y1,(float)y1};
    for(int k=0;k<4;k++) {
        q[k]=*v;q[k].x=x[k];q[k].y=y[k];
        q[k].u=plane[0]*x[k]+plane[1]*y[k]+plane[2];
        q[k].v=plane[3]*x[k]+plane[4]*y[k]+plane[5];
        interpolationLocalSource(&q[k],source);
    }
    q[5]=q[3];q[3]=q[1];q[4]=q[2];
    return interpolationRunVertices(q,6,outBuffer,outOffset);
}
static void interpolationPlane(const HiVertex* v, int axis, float* p) {
    float x1=v[1].x-v[0].x,y1=v[1].y-v[0].y,x2=v[2].x-v[0].x,y2=v[2].y-v[0].y;
    float a=axis?v[0].v:v[0].u,b=axis?v[1].v:v[1].u,c=axis?v[2].v:v[2].u;
    float det=x1*y2-y1*x2;
    p[0]=((b-a)*y2-(c-a)*y1)/det;
    p[1]=((c-a)*x1-(b-a)*x2)/det;
    p[2]=a-p[0]*v[0].x-p[1]*v[0].y;
}
static int hiBatchDisjoint(const HiVertex* v,uint32_t count);
static int hiTrianglesDisjoint(const HiVertex*,const HiVertex*);
static int replayRectsMeet(VRect a,VRect b) {
    return a.x0<=b.x1 && b.x0<=a.x1 && a.y0<=b.y1 && b.y0<=a.y1;
}

/* Adjacent wave primitives can share a pass when none reads a destination
 * written by the group. Sources stay in place; no extra snapshot/copy is
 * needed. Conservative enhanced bounds also cover fractional edge samples. */
static uint32_t interpolationWideParallel(const HiVertex* vertices,uint32_t first,uint32_t limit) {
    if(vk.env.serialReplay)return first;
    VRect sources[64],destinations[64];unsigned used=0;
    uint32_t end=first;
    while(end<limit && used<64) {
        const HiVertex* v=vertices+end;VRect source;
        if(!(v->sample[3]&INTERP_SAMPLE_EVOLVING) || !interpCaptureRect(v,&source))break;
        VRect dest={(int)floorf(fminf(v[0].x,fminf(v[1].x,v[2].x)))-1,
            (int)floorf(fminf(v[0].y,fminf(v[1].y,v[2].y)))-1,
            (int)ceilf(fmaxf(v[0].x,fmaxf(v[1].x,v[2].x)))+1,
            (int)ceilf(fmaxf(v[0].y,fmaxf(v[1].y,v[2].y)))+1};
        if(replayRectsMeet(source,dest))break;
        int conflict=0;
        for(unsigned i=0;i<used;i++)
            if(replayRectsMeet(source,destinations[i]) || replayRectsMeet(dest,sources[i]) ||
               (!vk.interlock && !hiTrianglesDisjoint(v,vertices+first+i*3))) { conflict=1;break; }
        if(conflict)break;
        sources[used]=source;destinations[used++]=dest;end+=3;
    }
    return used>1?end:first;
}

static int interpolationWideReplay(VkCommandBuffer cmd, VkDescriptorSet set, const HiDrawConstants* c,
                                    int w, int h, uint32_t first, const HiVertex* v, const InterpFrame* f) {
    int32_t rows[8+8*VRAM_H]; VRect box;
    if (!gp0InterpolationSpans(v,rows,&box)) {
        /* A fractional intermediate can cover enhanced samples while its
         * integer triangle is empty. Own the source before drawing it. */
        VRect source; if (!interpCaptureRect(v,&source)) return 0;
        interpolationCopyImage(vk.interpolationImage[INTERP_FRAMES],source.x0*c->n,source.y0*c->n,vk.interpolationSource,
            0,0,(source.x1-source.x0+1)*c->n,(source.y1-source.y0+1)*c->n);
        interpolationBarrier(cmd);
        HiVertex q[3]; memcpy(q,v,sizeof(q));
        for (int i=0;i<3;i++) interpolationLocalSource(&q[i],source);
        VkBuffer buffer;VkDeviceSize offset;
        if (!interpolationRunVertices(q,3,&buffer,&offset)) return 0;
        vkCmdBindVertexBuffers(cmd,0,1,&buffer,&offset);
        interpolationPass(cmd,set,vk.interpolationWidePipeline,c,sizeof(*c),w,h,0,3,f); return 1;
    }
    if (!vk.interpolationSteps) vk.interpolationSteps=malloc(WIDE_W*VRAM_H*sizeof(uint32_t));
    if(!vk.interpolationSteps)return 0;
    int overlap=0,last=interpCaptureSteps(v,rows,box,vk.interpolationSteps,c->n,&overlap);
    uint32_t* map=vk.interpolationSteps;int bw=box.x1-box.x0+1;
    if(last<0)return 0;
    if(!last&&!(c->n>1&&overlap)){interpolationPass(cmd,set,vk.interpolationWidePipeline,c,sizeof(*c),w,h,first,3,f);return 1;}
    float uvPlanes[6]; interpolationPlane(v,0,uvPlanes); interpolationPlane(v,1,uvPlanes+3);
    for (int step=0; step<=last; step++) {
        VRect source; interpCaptureRect(v,&source);
        interpolationCopyImage(vk.interpolationImage[INTERP_FRAMES],source.x0*c->n,source.y0*c->n,vk.interpolationSource,
            0,0,(source.x1-source.x0+1)*c->n,(source.y1-source.y0+1)*c->n);
        interpolationBarrier(cmd);
        VkBuffer runBuffer=VK_NULL_HANDLE;VkDeviceSize runOffset=0;uint32_t runCount=0;
        for (int y=box.y0; y<=box.y1; y++) {
            const int32_t* r=rows+8+(y-rows[0])*8;
            for (int x=r[0]; x<=r[1];) {
                if (map[(y-box.y0)*bw+x-box.x0]!=(uint32_t)step) { x++; continue; }
                int x0=x++;
                while (x<=r[1] && map[(y-box.y0)*bw+x-box.x0]==(uint32_t)step) x++;
                VkBuffer nextBuffer;VkDeviceSize nextOffset;
                if(!interpolationRun(v,uvPlanes,source,x0,y,x,y+1,&nextBuffer,&nextOffset))return 0;
                if(runCount && nextBuffer!=runBuffer) {
                    vkCmdBindVertexBuffers(cmd,0,1,&runBuffer,&runOffset);
                    interpolationPass(cmd,set,vk.interpolationWidePipeline,c,sizeof(*c),w,h,0,runCount,f);interpolationBarrier(cmd);runCount=0;
                }
                if(!runCount){runBuffer=nextBuffer;runOffset=nextOffset;}
                runCount+=6;
            }
        }
        if(runCount) {
            vkCmdBindVertexBuffers(cmd,0,1,&runBuffer,&runOffset);
            interpolationPass(cmd,set,vk.interpolationWidePipeline,c,sizeof(*c),w,h,0,runCount,f);
        }
        interpolationBarrier(cmd);
    }
    return 1;
}

/* A cold cache and sources untouched since its reset make cached reads
 * identical to an immutable copy. Prove that for the complete draw order;
 * strong/self-overlapping waves retain the serialized cache replay. */
static int interpolationNativePrefix(const InterpFrame* f, const HiVertex* vertices, uint32_t count) {
    if (f->wide || !f->capture || f->frozen || vk.env.interpolationOrdered) return 0;
    VRect written[INTERP_MAX_VERTICES/3];
    uint32_t operation=0, used=0;
    int cold=0;
    for (uint32_t first=0; first<count; first+=3) {
        while (operation<f->opCount && f->ops[operation].vertex==first) {
            const InterpOp* op=&f->ops[operation++];
            if (op->kind==INTERP_OP_CACHE) { cold=!op->w; used=0; }
            else if (op->kind==INTERP_OP_COPY) {
                /* Authored copies follow their cache reset, before reads. */
                if (used) return 0;
            } else return 0;
        }
        const HiVertex* v=vertices+first;
        VRect box; int32_t rows[8+8*VRAM_H];
        if (!gp0InterpolationSpans(v,rows,&box)) continue;
        if (v->sample[3]&INTERP_SAMPLE_EVOLVING) {
            VRect source;
            if (!cold || !interpCaptureRect(v,&source)) return 0;
            for (uint32_t i=0; i<=used; i++) {
                VRect r=i==used?box:written[i];
                if (source.x0<=r.x1 && r.x0<=source.x1 && source.y0<=r.y1 && r.y0<=source.y1) return 0;
            }
        }
        if (cold) written[used++]=box;
    }
    return 1;
}

static int interpolationRender(const GpuDisplay* d, double vblank, PresentConstants* pc, int sample) {
    if (!interpolationEnabled()) return 0;
    int before, after; double alpha;
    const InterpFrame* f = sample ? interpolationSample(d, vblank, &before, &after, &alpha) :
        interpolationPicture(d, vblank, &before, &after, &alpha);
    if (!f) return 0;
    const InterpFrame* current = interpolationFrame(after);
    if (pc->src != current->wide || pc->disp[0] < current->viewX || pc->disp[1] < current->viewY ||
        pc->disp[0] + pc->disp[2] > current->viewX + current->viewW ||
        pc->disp[1] + pc->disp[3] > current->viewY + current->viewH) {
        interpolationReport("native", "display-viewport-change", vblank, -1); return 0;
    }
    if (!vk.interpolationW || f->viewW != vk.interpolationW || f->viewH != vk.interpolationH) {
        interpolationReport("native", "replay-target-size", vblank, -1); return 0;
    }
    int slot;
    uint32_t motionKinds = interpFramesMotionKinds(f, interpolationFrame(after));
    if (alpha <= 0 || !motionKinds) slot = before;
    else if (alpha >= 1) slot = after;
    else {
        slot = INTERP_FRAMES;
        if (!vk.interpolationVertices) vk.interpolationVertices = malloc(INTERP_MAX_VERTICES * sizeof(HiVertex));
        int count = interpFrameVertices(f, interpolationFrame(after), alpha, vk.interpolationVertices, INTERP_MAX_VERTICES);
        if (!count) { interpolationReport("native", "replay-vertices", vblank, alpha); return 0; }
        for (int i = 0; i < count; i += 3) {
            HiVertex* v = &vk.interpolationVertices[i];
            if (!(v->sample[3] & INTERP_SAMPLE_CAPTURE)) continue;
            gp0InterpolationTransitionBounds(v);
            VRect sampled;
            if (!interpCaptureRect(v,&sampled)) { interpolationReport("native","capture-uv-bounds",vblank,alpha); return 0; }
            int w=vk.interpolationCanvasW,h=vk.interpolationCanvasH;
            if (!(v->sample[3]&INTERP_SAMPLE_EVOLVING)) {
                VRect rect=f->frozenRects[v->sample[3]>>24]; w=rect.x1-rect.x0+1; h=rect.y1-rect.y0+1;
            }
            if (sampled.x0<0 || sampled.y0<0 || sampled.x1>=w || sampled.y1>=h) {
                interpolationReport("native","capture-source-bounds",vblank,alpha); return 0;
            }
        }
        if (!vk.interpolationSpans) vk.interpolationSpans = malloc(SPAN_BYTES);
        if (!vk.interpolationSpans) return 0;
        uint32_t spanCount = 0;
        if (f->frozenWrites && !f->wide) for (int i=0;i<count;i++)
            if ((vk.interpolationVertices[i].sample[3]&INTERP_SAMPLE_CAPTURE) && !(vk.interpolationVertices[i].sample[3]&INTERP_SAMPLE_EVOLVING))
                vk.interpolationVertices[i].sample[3] |= INTERP_SAMPLE_NATIVE_CACHE;
        if (f->capture || f->frozen) for (int i = 0; i < count; i++) {
            HiVertex* v = &vk.interpolationVertices[i];
            v->x += f->viewX; v->y += f->viewY;
            v->clipMin += (uint32_t)f->viewX | (uint32_t)f->viewY << 16;
            v->clipMax += (uint32_t)f->viewX | (uint32_t)f->viewY << 16;
        }
        int nativePrefix=interpolationNativePrefix(f,vk.interpolationVertices,(uint32_t)count);
        if (nativePrefix) for (int i=0;i<count;i++)
            if (vk.interpolationVertices[i].sample[3]&INTERP_SAMPLE_EVOLVING)
                vk.interpolationVertices[i].sample[3] |= INTERP_SAMPLE_UNCACHED|INTERP_SAMPLE_STEP_SOURCE;
        for (int i = 0; i < count; i += 3) {
            HiVertex* v = &vk.interpolationVertices[i];
            if (!(v->sample[3] & (INTERP_SAMPLE_EVOLVING|INTERP_SAMPLE_NATIVE_CACHE))) continue;
            int32_t rows[8+8*VRAM_H]; VRect box;
            int ints = gp0InterpolationSpans(v,rows,&box);
            if ((size_t)(spanCount+ints+f->pixelCount)*4 > SPAN_BYTES) {
                interpolationReport("native","replay-span-capacity",vblank,alpha); return 0;
            }
            for (int k = 0; k < 3; k++) { v[k].sample[1] = spanCount; v[k].sample[2] = ints != 0; }
            if (ints) memcpy(vk.interpolationSpans+spanCount,rows,(size_t)ints*4);
            spanCount += ints;
        }
        int nativeBatch=!vk.hiN && !f->wide && f->capture && !nativePrefix;
        uint32_t batchBase=spanCount;
        if (nativeBatch) spanCount+=(uint32_t)count/3*5;
        size_t vb = (size_t)count*sizeof(HiVertex), pb = (size_t)f->pixelCount*4, sb = (size_t)spanCount*4;
        endBatch();
        if (vb > VERTEX_BYTES || pb+sb > SPAN_BYTES) return 0;
        if (frame()->vertexUsed+vb > VERTEX_BYTES || frame()->spanUsed+pb+sb > SPAN_BYTES) {
            if(vk.presentAcquired)return 0;
            submitFrame(NULL,NULL);
        }
        VkCommandBuffer cmd = recording(); FrameCtx* ctx = frame();
        uint32_t base = (uint32_t)(ctx->spanUsed/4);
        if (nativeBatch) for (int i=0;i<count;i+=3) {
            const HiVertex* v=vk.interpolationVertices+i;
            uint32_t* entry=(uint32_t*)(vk.interpolationSpans+batchBase+i/3*5);
            entry[0]=v->sample[2]?base+f->pixelCount+v->sample[1]:UINT32_MAX;
            entry[1]=v->mode;entry[2]=v->texWindow;entry[3]=v->sample[0];
            VRect box;int32_t rows[8+8*VRAM_H];
            entry[4]=gp0InterpolationSpans(v,rows,&box)?(uint32_t)interpolationFeedback(v,box):0;
        }
        if (pb) memcpy(ctx->spans.map+ctx->spanUsed,f->pixels,pb);
        if (sb) memcpy(ctx->spans.map+ctx->spanUsed+pb,vk.interpolationSpans,sb);
        ctx->spanUsed += pb+sb;
        for (int i = 0; i < count; i++) {
            HiVertex* v = &vk.interpolationVertices[i];
            if (v->sample[3] && !(v->sample[3]&INTERP_SAMPLE_CAPTURE)) v->sample[0] += base;
            else if (v->sample[3]&(INTERP_SAMPLE_EVOLVING|INTERP_SAMPLE_NATIVE_CACHE)) v->sample[1] += base+f->pixelCount;
        }
        memcpy(ctx->vertices.map+ctx->vertexUsed,vk.interpolationVertices,vb);
        VkDeviceSize offset = ctx->vertexUsed;
        ctx->vertexUsed += vb;
        ctx->vertexUsed = (ctx->vertexUsed+sizeof(GpuVertex)-1)/sizeof(GpuVertex)*sizeof(GpuVertex);
        interpolationBarrier(cmd);
        uint32_t rgb = f->background;
        uint32_t r = (rgb&255u)>>3, g = (rgb>>8&255u)>>3, b = (rgb>>16&255u)>>3;
        VkClearColorValue clear = {.uint32={(r<<3|r>>2)|(g<<3|g>>2)<<8|(b<<3|b>>2)<<16,0,0,0}};
        vkCmdClearColorImage(cmd,vk.interpolationImage[slot],VK_IMAGE_LAYOUT_GENERAL,&clear,1,&colorRange);
        interpolationBarrier(cmd);
        vkCmdBindVertexBuffers(cmd,0,1,&ctx->vertices.buffer,&offset);
        HiDrawConstants c = {vk.interpolationN,vk.hiN?vk.hiColor:HIRES_COLOR_DITHER_1X,
                            vk.interpolationCanvasW*vk.interpolationN,vk.interpolationCanvasH*vk.interpolationN,0,0,f->viewX,f->viewY,1};
        int canvasW = c.targetW, canvasH = c.targetH;
        if (f->capture || f->frozen) {
            c.targetW = (f->wide?(vk.hiN?vk.hiWideW:WIDE_W):VRAM_W)*vk.interpolationN;
            c.targetH = (f->wide?WIDE_H:VRAM_H)*vk.interpolationN;
            c.ditherX = c.ditherY = 0;
        }
        uint32_t operation = 0, first = 0;
        while (first < (uint32_t)count || operation < f->opCount) {
            int sourceChanged=0;
            while (operation < f->opCount && f->ops[operation].vertex == first) {
                const InterpOp* op = &f->ops[operation++];
                if (op->kind == INTERP_OP_CACHE) {
                    if (nativePrefix) { sourceChanged=1; continue; }
                    CaptureConstants cc = {.n=vk.interpolationN,.seed=op->w?(int)(base+op->data):-1};
                    interpolationPass(cmd,ctx->interpolationSet[slot],vk.capturePipeline,&cc,sizeof(cc),1,1,0,3,NULL);
                    interpolationBarrier(cmd);
                } else if (op->kind==INTERP_OP_FROZEN) {
                    VRect r=f->frozenRects[op->data];
                    interpolationCopyImage(vk.interpolationFrozen[before*INTERP_FREEZES+op->data],0,0,vk.interpolationSource,0,0,
                        (r.x1-r.x0+1)*vk.interpolationN,(r.y1-r.y0+1)*vk.interpolationN);
                    interpolationBarrier(cmd);
                } else {
                    interpolationCopyReplay(cmd,vk.interpolationImage[slot],op,vk.interpolationN,f->wide);
                    sourceChanged=1;
                }
            }
            if (nativePrefix && sourceChanged) {
                interpolationCopyImage(vk.interpolationImage[slot],0,0,vk.interpolationSource,0,0,canvasW,canvasH);
                interpolationBarrier(cmd);
            }
            if (first >= (uint32_t)count) break;
            const HiVertex* v = &vk.interpolationVertices[first];
            if (nativeBatch && (v->sample[3]&INTERP_SAMPLE_EVOLVING)) {
                uint32_t end=first+3,limit=operation<f->opCount?f->ops[operation].vertex:(uint32_t)count;
                while (end<limit && (vk.interpolationVertices[end].sample[3]&INTERP_SAMPLE_EVOLVING)) end+=3;
                CaptureConstants cc={.n=1,.action=3,.first=(int)(base+f->pixelCount+batchBase+first/3*5),.seed=(int)((end-first)/3)};
                interpolationPass(cmd,ctx->interpolationSet[slot],vk.capturePipeline,&cc,sizeof(cc),1,1,0,3,NULL);
                interpolationBarrier(cmd);first=end;continue;
            }
            if (!nativePrefix && (v->sample[3]&(INTERP_SAMPLE_EVOLVING|INTERP_SAMPLE_NATIVE_CACHE))) {
                if (!f->wide) {
                    if (v->sample[2]) {
                        VRect box; int32_t rows[8+8*VRAM_H]; gp0InterpolationSpans(v,rows,&box);
                        CaptureConstants cc = {.n=vk.interpolationN,.action=1,.first=(int)v->sample[1],
                            .source={(int16_t)v->sample[0],(int16_t)(v->sample[0]>>16)},.mode=v->mode,.window=v->texWindow,
                            .feedback=interpolationFeedback(v,box)};
                        if (v->sample[3]&INTERP_SAMPLE_NATIVE_CACHE) {
                            VRect r=f->frozenRects[v->sample[3]>>24];
                            cc.frozen=1; cc.source[0]=(v->tex&15)*64; cc.source[1]=((v->tex>>4)&1)*256;
                            cc.readOffset[0]=-r.x0; cc.readOffset[1]=-r.y0; cc.feedback=0;
                        }
                        interpolationPass(cmd,ctx->interpolationSet[slot],vk.capturePipeline,&cc,sizeof(cc),1,1,0,3,NULL);
                        interpolationBarrier(cmd);
                    } else {
                        CaptureConstants cc = {.action=2};
                        interpolationPass(cmd,ctx->interpolationSet[slot],vk.capturePipeline,&cc,sizeof(cc),1,1,0,3,NULL);
                        interpolationBarrier(cmd);
                    }
                    interpolationPass(cmd,ctx->interpolationSet[slot],vk.hiDrawPipeline,&c,sizeof(c),canvasW,canvasH,first,3,f);
                } else {
                    uint32_t limit=operation<f->opCount?f->ops[operation].vertex:(uint32_t)count;
                    uint32_t parallel=interpolationWideParallel(vk.interpolationVertices,first,limit);
                    if(parallel>first) {
                        VkPipeline pipeline=hiBatchDisjoint(v,parallel-first)?vk.interpolationWidePipeline:vk.hiWidePipeline;
                        interpolationPass(cmd,ctx->interpolationSet[INTERP_FRAMES+1],pipeline,&c,sizeof(c),canvasW,canvasH,first,parallel-first,f);
                        interpolationBarrier(cmd);first=parallel;continue;
                    }
                    if(!interpolationWideReplay(cmd,ctx->interpolationSet[INTERP_FRAMES+1],&c,canvasW,canvasH,first,v,f)) {
                        interpolationBarrier(cmd);vk.needBarrier=1;interpolationReport("native","replay-run-capacity",vblank,alpha);return 0;
                    }
                    vkCmdBindVertexBuffers(cmd,0,1,&ctx->vertices.buffer,&offset);
                }
                interpolationBarrier(cmd); first += 3;
            } else {
                uint32_t end = first+3, limit = operation < f->opCount ? f->ops[operation].vertex : (uint32_t)count;
                if (vk.interlock) while (end < limit && (nativePrefix || !(vk.interpolationVertices[end].sample[3]&(INTERP_SAMPLE_EVOLVING|INTERP_SAMPLE_NATIVE_CACHE)))) end += 3;
                interpolationPass(cmd,ctx->interpolationSet[f->wide?INTERP_FRAMES+1:slot],f->wide?vk.hiWidePipeline:vk.hiDrawPipeline,&c,sizeof(c),canvasW,canvasH,first,end-first,f);
                interpolationBarrier(cmd); first = end;
            }
        }
        vk.needBarrier = 1;
        vk.intermediatePictures++;
        if (vk.env.interpolationLog) gpuLog("interpolation: intermediate at %.3f alpha %.6f", vblank, alpha);
    }
    pc->src = 4; pc->rgb24 = 0;
    /* Endpoints are local images. The prior game frame may live in the
     * other native framebuffer; use the current frame's display crop. */
    pc->disp[0] = -(pc->disp[0] - current->viewX + (slot == INTERP_FRAMES && (f->capture || f->frozen) ? f->viewX : 0)) * vk.interpolationN;
    pc->disp[1] = -(pc->disp[1] - current->viewY + (slot == INTERP_FRAMES && (f->capture || f->frozen) ? f->viewY : 0)) * vk.interpolationN;
    pc->disp[2] *= vk.interpolationN; pc->disp[3] *= vk.interpolationN;
    vk.pictureOverride = frame()->interpolationPicture[slot];
    vk.interpolatedPictures++;
    interpolationReport(slot == INTERP_FRAMES ? "intermediate" : "endpoint", "compatible", vblank, alpha);
    if (slot == INTERP_FRAMES) {
        const char* const names[] = {"motion-model", "motion-mecha", "motion-particle", "motion-sprite", "motion-shadow", "motion-transition", "motion-wave", "motion-terrain"};
        for (unsigned k = 0; k < sizeof(names) / sizeof(names[0]); k++)
            if (motionKinds & (1u << k)) interpolationReport(names[k], "compatible", vblank, alpha);
    }
    return 1;
}

/* Everything recorded handed over and finished (before images change). */
static void idleDevice(void) {
    endBatch();
    submitFrame(NULL, NULL);
    presentDrain();
    vkDeviceWaitIdle(vk.device);
    for (int i = 0; i < FRAMES; i++) {
        vk.frames[i].submitted = 0;
        if (vk.hiSetsStale & (1u << i)) writeHiSet(&vk.frames[i]);
    }
    vk.hiSetsStale = 0;
    hiFrameDone(-1); /* a replaced canvas: nothing uses it any more */
}

/* Only while idle: all recorded references have finished before replacing
 * this descriptor's source image, just like the display-copy descriptors. */
static void writeEffectSet(void) {
    VkDescriptorImageInfo image = {VK_NULL_HANDLE, vk.hiEffectView ? vk.hiEffectView : vk.dummy32View,
                                  VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = vk.effectSet; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w.pImageInfo = &image;
    vkUpdateDescriptorSets(vk.device, 1, &w, 0, NULL);
}
static void writeHiSets(void) {
    for (int i = 0; i < FRAMES; i++) {
        writeHiSet(&vk.frames[i]);
    }
    vk.hiSetsStale = 0;
    writeEffectSet();
    interpolationSets();
}

/* A new image to GENERAL before its first use. */
static void toGeneral(VkCommandBuffer cmd, VkImage image) {
    if (image != VK_NULL_HANDLE) {
        imageBarrier(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_NONE, 0,
                     VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    }
}

/* One pass of hires_util.frag over (x, y, w, h), hi-res pixels of the
 * target (u->target: 0 the copy of VRAM, 1 of the wide target). */
static void hiUtil(const HiUtilConstants* u, int x, int y, int w, int h) {
    cpuResolveFlush();
    int tw = u->target ? vk.hiWideW * vk.hiN : VRAM_W * vk.hiN;
    int th = u->target ? vk.hiWideH * vk.hiN : VRAM_H * vk.hiN;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    w = x + w > tw ? tw - x : w;
    h = y + h > th ? th - y : h;
    if (w <= 0 || h <= 0) {
        return;
    }
    endBatch();
    VkCommandBuffer cmd = recording();
    barrierIfNeeded(cmd);
    int q = gtStart(cmd, GT_HIUTIL);
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.offset.x = x;
    ri.renderArea.offset.y = y;
    ri.renderArea.extent.width = (uint32_t)w;
    ri.renderArea.extent.height = (uint32_t)h;
    ri.layerCount = 1;
    vkCmdBeginRendering(cmd, &ri);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.hiUtilPipeline);
    VkDescriptorSet sets[2] = {frame()->set, vk.effectSet};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.hiLayout, 0, 2, sets, 0, NULL);
    VkViewport viewport = {0.0f, 0.0f, (float)tw, (float)th, 0.0f, 1.0f};
    VkRect2D scissor = {{x, y}, {(uint32_t)w, (uint32_t)h}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdPushConstants(cmd, vk.hiLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(*u), u);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
    gtStop(cmd, q);
    vk.needBarrier = 1;
}

/* 1x rectangle r of the copy (target 0 VRAM, 1 the wide target) as N x N
 * blocks of a 1x image (srcKind 0 VRAM, 1 the wide target, 2 the scratch
 * image) from (sx, sy). */
static void hiReplicate(int target, int srcKind, VRect r, int sx, int sy) {
    int n = vk.hiN;
    if (n == 0 || (target == 1 && vk.hiWide == VK_NULL_HANDLE)) {
        return;
    }
    HiUtilConstants u = {0};
    u.n = n;
    u.op = HI_OP_REPLICATE;
    u.target = target;
    u.srcKind = srcKind;
    u.dst0[0] = r.x0;
    u.dst0[1] = r.y0;
    u.src0[0] = sx;
    u.src0[1] = sy;
    hiUtil(&u, r.x0 * n, r.y0 * n, (r.x1 - r.x0 + 1) * n, (r.y1 - r.y0 + 1) * n);
}

/* The copy of the wide target as wide as the columns used so far: made or
 * made wider (what it had is kept, the new columns come from the 1x one). */
static void interpolationColdSystem(void) {
    /* Native growth has priority over endpoint residency. Drain readers and
     * preserve each complete immutable image, including hidden enhanced bits.
     * New workspaces can prefer device memory again when there is room. */
    idleDevice();
    for (int i=0;i<INTERP_FRAMES;i++) {
        if (vk.interpolationSystem[i] || !vk.interpolationImage[i]) continue;
        VkImage image; VkDeviceMemory memory; VkImageView view;
        int w=vk.interpolationW*vk.interpolationN,h=vk.interpolationH*vk.interpolationN;
        if (!createImage32Memory(&image,&memory,&view,w,h,1)) continue;
        toGeneral(recording(),image);
        copyImage(vk.interpolationImage[i],0,0,image,0,0,w,h);
        idleDevice();
        destroyImage(&vk.interpolationImage[i],&vk.interpolationMemory[i],&vk.interpolationView[i]);
        vk.interpolationImage[i]=image; vk.interpolationMemory[i]=memory; vk.interpolationView[i]=view;
        vk.interpolationSystem[i]=1;
    }
    interpolationSets();
}

static int hiConfigure(int n, int color);
/* The largest scale allowed once the wide copy could not grow at
 * vk.hiFailN: about half its bytes per failure, so a heap that keeps
 * failing reaches the exact picture in a few steps. A 1x copy is the
 * precise-geometry mode, never an automatic stand-in for an upscale. */
static int hiScaleAllowed(int n) {
    int want = n;
    while (vk.hiFailN && n > 0 && 2 * n * n > vk.hiFailN * vk.hiFailN) n--;
    return want >= 2 && n < 2 ? 0 : n;
}

/* Neither heap holds the grown wide copy. Memory pressure is not a
 * programmer error: degrade like an over-budget scale instead of aborting.
 * Upscaling is off for the rest of this vblank (callers recheck vk.hiN
 * after any growth), which hires.c sees as a new copy, so the next vblank
 * rebuilds it from the exact 1x picture at hiScaleAllowed(). */
static void hiWideExhausted(int n, int w, int h) {
    vk.hiFailN = n;
    if (vk.hiWide == VK_NULL_HANDLE) {
        /* Released for staging: no descriptor may name it while the
         * reconfiguration below flushes pending CPU resolves. */
        vk.hiWideW = vk.hiWideH = 0;
        writeHiSets();
    }
    int next = hiScaleAllowed(n);
    if (next) gpuLog("hires: no room for the wide copy at %dx (%dx%d) in any heap; upscaling off, then at most %dx", n, w * n, h * n, next);
    else gpuLog("hires: no room for the wide copy at %dx (%dx%d) in any heap; upscaling off", n, w * n, h * n);
    hiConfigure(0, vk.hiColor);
}

/* The fullest device-local heap's use plus `extra` bytes, in percent of its
 * budget (VK_EXT_memory_budget: every process's use), or -1 without the
 * extension. XG_TEST_HI_HEAP_PERCENT=n fakes the fill for tests. */
static int deviceHeapPercent(VkDeviceSize extra) {
    const char* test = getenv("XG_TEST_HI_HEAP_PERCENT");
    if (test) return atoi(test);
    if (!vk.memoryBudget) return -1;
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 props = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, .pNext = &budget};
    vkGetPhysicalDeviceMemoryProperties2(vk.physical, &props);
    int worst = 0;
    for (uint32_t i = 0; i < props.memoryProperties.memoryHeapCount; i++) {
        if (!(props.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) || budget.heapBudget[i] == 0) continue;
        int p = (int)((budget.heapUsage[i] + extra) * 100 / budget.heapBudget[i]);
        if (p > worst) worst = p;
    }
    return worst;
}

/* The latest burst of canvas work that stalled the game thread (a trim, a
 * growth): hw.c asks whether a gap in its clock was one (gpuStallSince). */
static struct { uint64_t ns, end; } stall;
static void stallEnd(uint64_t began) {
    uint64_t now = SDL_GetTicksNS();
    if (began - stall.end > UINT64_C(1000000000)) stall.ns = 0;
    stall.ns += now - began;
    stall.end = now;
}
double gpuStallSince(uint64_t ns) { return stall.end >= ns ? (double)stall.ns * 1e-9 : 0; }

/* The cold image outlives its rows (xg-2fy.1). Allocating its ~0.4 GB of
 * system memory was most of a 12x trim (64-72 ms of 110 on an RTX 3070 Ti)
 * and waiting for its last copy and freeing it most of the regrowth after
 * (about 65 of 80 ms), three times that on a GPU shared with a stream
 * encoder, and the two recur minutes apart. With its rows back in the
 * canvas it waits as the spare, which the next trim takes when it is large
 * enough. It costs system memory only, never the device heap a trim relieves.
 * The copy that gave its rows back may still be running: the trim's barrier
 * orders its own copy into the spare after it, and a spare freed idles first. */
static void hiColdSpare(void) {
    if (vk.hiWideSpare != VK_NULL_HANDLE) { /* (not expected: one cold image at a time) */
        idleDevice();
        destroyImage(&vk.hiWideSpare, &vk.hiWideSpareMemory, &vk.hiWideSpareView);
    }
    vk.hiWideSpare = vk.hiWideCold; vk.hiWideSpareMemory = vk.hiWideColdMemory; vk.hiWideSpareView = vk.hiWideColdView;
    vk.hiWideSpareW = vk.hiWideColdPxW; vk.hiWideSpareH = vk.hiWideColdPxH;
    vk.hiWideCold = VK_NULL_HANDLE; vk.hiWideColdMemory = VK_NULL_HANDLE; vk.hiWideColdView = VK_NULL_HANDLE;
    vk.hiWideColdW = vk.hiWideColdH = vk.hiWideColdY = vk.hiWideColdPxW = vk.hiWideColdPxH = 0;
}

/* The first spare, made on a thread of its own while the rows are quiet
 * under heap pressure (the window count, about nine seconds before the trim):
 * the trim then allocates nothing. Only the image, its memory and its view are
 * made there (Vulkan's creation calls need no external synchronisation with
 * the game thread's). */
static struct {
    SDL_Thread* thread;
    int w, h, made;
    VkImage image; VkDeviceMemory memory; VkImageView view;
} spareAhead;
static int SDLCALL spareAheadRun(void* arg) {
    (void)arg;
    spareAhead.made = createImage32Memory(&spareAhead.image, &spareAhead.memory, &spareAhead.view,
                                          spareAhead.w, spareAhead.h, 1);
    return 0;
}
static void hiSpareAhead(int w, int h) {
    if (spareAhead.thread || vk.hiWideSpare != VK_NULL_HANDLE || vk.hiWideCold != VK_NULL_HANDLE || w <= 0 || h <= 0) return;
    spareAhead.w = w; spareAhead.h = h; spareAhead.made = 0;
    spareAhead.thread = SDL_CreateThread(spareAheadRun, "hires spare", NULL);
}
/* Its result as the spare (a spare already there wins: nothing is using
 * the one made ahead, which is freed). */
static void hiSpareCollect(void) {
    if (!spareAhead.thread) return;
    SDL_WaitThread(spareAhead.thread, NULL);
    spareAhead.thread = NULL;
    if (!spareAhead.made) return;
    if (vk.hiWideSpare != VK_NULL_HANDLE) {
        destroyImage(&spareAhead.image, &spareAhead.memory, &spareAhead.view);
        return;
    }
    vk.hiWideSpare = spareAhead.image; vk.hiWideSpareMemory = spareAhead.memory; vk.hiWideSpareView = spareAhead.view;
    vk.hiWideSpareW = spareAhead.w; vk.hiWideSpareH = spareAhead.h;
    spareAhead.image = VK_NULL_HANDLE; spareAhead.memory = VK_NULL_HANDLE; spareAhead.view = VK_NULL_HANDLE;
    gpuLog("hires: system memory for inactive wide rows made ahead (%dx%d)", vk.hiWideSpareW, vk.hiWideSpareH);
}

/* No spare left, the one being made ahead neither (a scale change, shutdown:
 * the device is idle). */
static void hiSpareFree(void) {
    if (spareAhead.thread) {
        SDL_WaitThread(spareAhead.thread, NULL);
        spareAhead.thread = NULL;
        destroyImage(&spareAhead.image, &spareAhead.memory, &spareAhead.view);
    }
    destroyImage(&vk.hiWideSpare, &vk.hiWideSpareMemory, &vk.hiWideSpareView);
    vk.hiWideSpareW = vk.hiWideSpareH = 0;
}

/* The spare as a cold image of at least *w x *h hi-res pixels (*w, *h: its
 * own extent then); one too small is freed. 0: none to take. */
static int hiSpareTake(VkImage* image, VkDeviceMemory* memory, VkImageView* view, int* w, int* h) {
    hiSpareCollect();
    if (vk.hiWideSpare == VK_NULL_HANDLE) return 0;
    if (vk.hiWideSpareW < *w || vk.hiWideSpareH < *h) {
        idleDevice();
        destroyImage(&vk.hiWideSpare, &vk.hiWideSpareMemory, &vk.hiWideSpareView);
        vk.hiWideSpareW = vk.hiWideSpareH = 0;
        return 0;
    }
    *image = vk.hiWideSpare; *memory = vk.hiWideSpareMemory; *view = vk.hiWideSpareView;
    *w = vk.hiWideSpareW; *h = vk.hiWideSpareH;
    vk.hiWideSpare = VK_NULL_HANDLE; vk.hiWideSpareMemory = VK_NULL_HANDLE; vk.hiWideSpareView = VK_NULL_HANDLE;
    vk.hiWideSpareW = vk.hiWideSpareH = 0;
    return 1;
}

/* A canvas replaced without idling the device, freed once no frame context
 * can use it (hiFrameDone). With every slot taken the device idles, which
 * frees them all. */
static void hiRetire(VkImage* image, VkDeviceMemory* memory, VkImageView* view) {
    int slots = (int)(sizeof(vk.hiRetired) / sizeof(vk.hiRetired[0])), i = 0;
    while (i < slots && vk.hiRetired[i].image != VK_NULL_HANDLE) i++;
    if (i == slots) {
        idleDevice();
        i = 0;
    }
    vk.hiRetired[i].image = *image; vk.hiRetired[i].memory = *memory; vk.hiRetired[i].view = *view;
    vk.hiRetired[i].pending = (1u << FRAMES) - 1;
    *image = VK_NULL_HANDLE; *memory = VK_NULL_HANDLE; *view = VK_NULL_HANDLE;
}

static void hiWideGrowBody(int rows);
static void hiWideGrow(int rows) {
    uint64_t began = SDL_GetTicksNS();
    hiWideGrowBody(rows);
    stallEnd(began);
}
static void hiWideGrowBody(int rows) {
    int n = vk.hiN;
    int w = (vk.wideExtent + 63) & ~63;
    /* Growing reallocates the whole copy (about 0.6 GB at 12x) with the device idle: when the
     * used columns outgrow it, take 128 more for what comes next. A battle's start grew it
     * twice in one frame, 448 -> 512 -> 576 columns (a 60 ms stall,). */
    if (vk.hiWideW && w > vk.hiWideW) w += 128;
    /* Made at boot (the title), with room for a battle's wider pictures (576 columns at 16:9):
     * growing there is free, in a battle's first frame it is not. */
    else if (!vk.hiWideW && w < 640) w = 640;
    w = w > WIDE_W ? WIDE_W : w;
    if (w < vk.hiWideW) w = vk.hiWideW;
    int h = (rows+63)&~63;
    if (h < vk.hiWideH) h = vk.hiWideH;
    if (h < VRAM_H) h = VRAM_H;
    if (h > WIDE_H) h = WIDE_H;
    if (n == 0 || (w <= vk.hiWideW && h <= vk.hiWideH)) {
        return;
    }
    idleDevice();
    int old = vk.hiWideW, oldH = vk.hiWideH;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    HostBuffer saved = {0};
    VkBufferImageCopy copy = {0};
    copy.imageSubresource = colorLayers;
    copy.imageExtent = (VkExtent3D){(uint32_t)(old * n), (uint32_t)(oldH * n), 1};
    /* Deterministic pressure tests fail only the overlapping allocation
     * (1, or 2 through system memory); 3 also fails the allocation after
     * the old canvas is released, 4 its host staging copy. */
    const char* test = getenv("XG_TEST_HI_WIDE_GROW_FAIL");
    int fail = old && test ? atoi(test) : 0;
    if (fail < 3 && (vk.hiWideSystem || envFlag("XG_TEST_NATIVE_IMAGE_SYSTEM"))) fail = 2;
    int system = 0;
    if (fail || !createImage32(&image, &memory, &view, w * n, h * n)) {
        if (!vk.hiWideSystem) interpolationColdSystem();
        /* Preserve every enhanced bit, including hidden capture rows.
         * Unlike another device image this cached host buffer does not
         * need the old and new canvases resident in VRAM together. */
        if (old && fail != 4 &&
            tryHostBufferMemory(&saved, (VkDeviceSize)old * oldH * n * n * 4,
                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1, 1)) {
            VkCommandBuffer cmd = recording();
            fullBarrier(cmd);
            vkCmdCopyImageToBuffer(cmd, vk.hiWide, VK_IMAGE_LAYOUT_GENERAL, saved.buffer, 1, &copy);
            vk.needBarrier = 1;
            idleDevice();
            destroyImage(&vk.hiWide, &vk.hiWideMemory, &vk.hiWideView);
        }
        int made = old && !saved.buffer ? 0 : fail >= 3 ? 0 :
                   fail == 2 ? createImage32Memory(&image,&memory,&view,w*n,h*n,1) :
                               createNativeImage32(&image,&memory,&view,w*n,h*n);
        system = fail == 2 || made == 2;
        if (!made) {
            destroyHostBuffer(&saved);
            hiWideExhausted(n, w, h);
            return;
        }
        gpuLog("hires: wide copy growth via host staging, keeping %dx (%d -> %d columns)", n, old, w);
    }
    VkCommandBuffer cmd = recording();
    toGeneral(cmd, image);
    fullBarrier(cmd);
    if (saved.buffer != VK_NULL_HANDLE) {
        vkCmdCopyBufferToImage(cmd, saved.buffer, image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    } else if (vk.hiWide != VK_NULL_HANDLE) {
        VkImageCopy c = {0};
        c.srcSubresource = colorLayers;
        c.dstSubresource = colorLayers;
        c.extent = copy.imageExtent;
        vkCmdCopyImage(cmd, vk.hiWide, VK_IMAGE_LAYOUT_GENERAL, image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    }
    vk.needBarrier = 1;
    idleDevice();
    destroyImage(&vk.hiWide, &vk.hiWideMemory, &vk.hiWideView);
    destroyHostBuffer(&saved);
    vk.hiWide = image;
    vk.hiWideMemory = memory;
    vk.hiWideView = view;
    vk.hiWideW = w; vk.hiWideH = h;
    vk.hiWideSystem = system;
    writeHiSets();
    if (w > old) hiReplicate(1,1,(VRect){old,0,w-1,h-1},old,0);
    if (h > oldH && old) hiReplicate(1,1,(VRect){0,oldH,old-1,h-1},0,oldH);
    if (vk.hiWideCold) {
        int rows = h < vk.hiWideColdH ? h : vk.hiWideColdH;
        int cols = w < vk.hiWideColdW ? w : vk.hiWideColdW;
        if (rows > oldH && oldH >= vk.hiWideColdY && cols) copyImage(vk.hiWideCold,0,(oldH-vk.hiWideColdY)*n,vk.hiWide,0,oldH*n,cols*n,(rows-oldH)*n);
        if (h >= vk.hiWideColdH) hiColdSpare(); /* every row back: kept for the next trim */
    }
    gpuLog("hires: wide target's copy %dx%d (%dx%d)",w,h,w*n,h*n);
}

static int hiWideToSystem(void) {
    if (!vk.hiWide || vk.hiWideSystem) return 0;
    VkImage image; VkDeviceMemory memory; VkImageView view;
    int w=vk.hiWideW*vk.hiN,h=vk.hiWideH*vk.hiN;
    if (!createImage32Memory(&image,&memory,&view,w,h,1)) return 0;
    idleDevice();
    toGeneral(recording(),image);
    fullBarrier(recording());
    copyImage(vk.hiWide,0,0,image,0,0,w,h);
    idleDevice();
    destroyImage(&vk.hiWide,&vk.hiWideMemory,&vk.hiWideView);
    vk.hiWide=image; vk.hiWideMemory=memory; vk.hiWideView=view; vk.hiWideSystem=1;
    writeHiSets();
    gpuLog("hires: native wide canvas moved to system memory, keeping %dx and all enhanced pixels",vk.hiN);
    return 1;
}

/* Column x1 of the wide target is about to be written. */
static void wideTouch(int x1) {
    if (x1 >= vk.wideExtent) {
        vk.wideExtent = x1 + 1 > WIDE_W ? WIDE_W : x1 + 1;
    }
    if (vk.hiN != 0 && vk.wideExtent > vk.hiWideW) {
        hiWideGrow(vk.hiWideH);
    }
}

/* Rows never written at enhanced resolution contain only their native
 * replica. Materialize them before a write or read can need them. Existing
 * enhanced rows survive growth; restored native hidden rows initialize new
 * storage without becoming part of the saved GPU metadata. */
static void hiWideRows(int rows) {
    if (rows > vk.hiWideRecentH) vk.hiWideRecentH=rows;
    if (vk.hiN && vk.hiWideW && rows > vk.hiWideH) hiWideGrow(rows);
}

/* Keep inactive enhanced rows in an exact system-memory image. Their
 * native replica is insufficient: screen captures can carry coverage and
 * colour detail absent at 1x. At most one cold image is retained. Materialize
 * its remaining rows before replacing it, so repeated trims cannot lose an
 * earlier hidden region. All readers drain before image/view replacement. */
#define HI_TRIM_WINDOWS 10 /* about ten seconds of native vblanks */
static void hiTrimWideBody(int rows);
void gvkHiTrimWide(int rows) {
    uint64_t began = SDL_GetTicksNS();
    hiTrimWideBody(rows);
    stallEnd(began);
}
/* Whether the inactive rows are worth their trim: the fullest device-local
 * heap at HI_TRIM_HEAP_PERCENT of its budget (deviceHeapPercent). Without
 * VK_EXT_memory_budget the answer is always yes, as before. percent: that
 * heap's, or -1 unknown. A heap with room keeps the rows resident: a trim
 * and the regrowth after it still cost the game thread a few milliseconds
 * each, and the GPU its copies (xg-2fy.1). */
static int hiWidePressure(int* percent) {
    *percent = deviceHeapPercent(0);
    return *percent < 0 || *percent >= HI_TRIM_HEAP_PERCENT;
}
static void hiTrimWideBody(int rows) {
    int h=(rows+63)&~63;
    if (h<VRAM_H) h=VRAM_H;
    if (!vk.hiN || !vk.hiWide || h+256>vk.hiWideH) return;
    if (vk.hiWideCold) hiWideGrow(vk.hiWideColdH);
    if (!vk.hiN) return; /* that growth turned upscaling off */
    int n=vk.hiN,w=vk.hiWideW,oldH=vk.hiWideH;
    VkImage cold,image; VkDeviceMemory coldMemory,memory; VkImageView coldView,view;
    /* Only the inactive rows go cold, in one device-to-host copy. The whole
     * canvas is 462 MB at 12x: copying all of it to system memory and the
     * kept rows back from there took 0.4-0.5 s of the game thread (xg-ody).
     * The image is the spare when there is one (hiColdSpare); a new one has
     * rows enough for any later trim of this canvas (they keep VRAM_H). */
    int coldW=w*n,coldH=(oldH-h)*n;
    if (!hiSpareTake(&cold,&coldMemory,&coldView,&coldW,&coldH)) {
        coldH=(oldH-VRAM_H)*n;
        if (!createImage32Memory(&cold,&coldMemory,&coldView,coldW,coldH,1)) {
            coldH=(oldH-h)*n;
            if (!createImage32Memory(&cold,&coldMemory,&coldView,coldW,coldH,1)) return;
        }
    }
    /* The canvas's pending draws, then its copies, in the current frame
     * context. */
    endBatch();
    toGeneral(recording(),cold);fullBarrier(recording());
    copyImage(vk.hiWide,0,h*n,cold,0,0,w*n,(oldH-h)*n);
    /* The kept rows: device to device beside the old canvas while there is
     * room; under pressure staged through system memory (a non-overlapping
     * retry), the stage serving as the canvas if the retry fails too.
     * Beside the old canvas nothing is waited for (xg-2fy.1): that context
     * goes to the queue with the copies and the canvas changes from the next
     * one on; the old one is retired (hiRetire) and each context's set is
     * rewritten when it records again (hiFrameDone). Waiting for the copies
     * and freeing the old canvas took 40 ms of the game thread at 12x on an
     * RTX 3070 Ti. */
    int made=createNativeImage32(&image,&memory,&view,w*n,h*n);
    int beside=made!=0;
    if (made) {
        toGeneral(recording(),image);fullBarrier(recording());
        copyImage(vk.hiWide,0,0,image,0,0,w*n,h*n);
        submitFrame(NULL,NULL);
        hiRetire(&vk.hiWide,&vk.hiWideMemory,&vk.hiWideView);
    } else {
        VkImage kept; VkDeviceMemory keptMemory; VkImageView keptView;
        if (!createImage32Memory(&kept,&keptMemory,&keptView,w*n,h*n,1)) {
            idleDevice();destroyImage(&cold,&coldMemory,&coldView);return;
        }
        toGeneral(recording(),kept);fullBarrier(recording());
        copyImage(vk.hiWide,0,0,kept,0,0,w*n,h*n);idleDevice();
        destroyImage(&vk.hiWide,&vk.hiWideMemory,&vk.hiWideView);
        made=createNativeImage32(&image,&memory,&view,w*n,h*n);
        if (made) {
            toGeneral(recording(),image);fullBarrier(recording());
            copyImage(kept,0,0,image,0,0,w*n,h*n);idleDevice();
            destroyImage(&kept,&keptMemory,&keptView);
        } else { image=kept;memory=keptMemory;view=keptView;made=2; }
    }
    vk.hiWide=image;vk.hiWideMemory=memory;vk.hiWideView=view;vk.hiWideH=h;vk.hiWideSystem=made==2;
    vk.hiWideCold=cold;vk.hiWideColdMemory=coldMemory;vk.hiWideColdView=coldView;
    vk.hiWideColdW=w;vk.hiWideColdH=oldH;vk.hiWideColdY=h;vk.hiWideColdPxW=coldW;vk.hiWideColdPxH=coldH;
    if (beside) {
        vk.hiSetsStale=(1u<<FRAMES)-1;
        vk.needBarrier=1; /* the next context's first use comes after the copies */
    } else writeHiSets();
    gpuLog("hires: cached inactive wide rows %d..%d in system memory; retaining %dx detail",h,oldH-1,n);
}

/* The scratch image for overlapping moves, at least w x h hi-res pixels. */
static int hiScratchFor(int w, int h) {
    if (w <= vk.hiScratchW && h <= vk.hiScratchH) {
        return 1;
    }
    int n = vk.hiN;
    w = w > vk.hiScratchW ? w : vk.hiScratchW;
    h = h > vk.hiScratchH ? h : vk.hiScratchH;
    w = w > 320 * n ? w : 320 * n;
    h = h > 256 * n ? h : 256 * n;
    idleDevice();
    destroyImage(&vk.hiScratch, &vk.hiScratchMemory, &vk.hiScratchView);
    vk.hiScratchW = vk.hiScratchH = 0;
    int ok = createNativeImage32(&vk.hiScratch, &vk.hiScratchMemory, &vk.hiScratchView, w, h);
    if (ok) {
        vk.hiScratchW = w;
        vk.hiScratchH = h;
        toGeneral(recording(), vk.hiScratch);
        vk.needBarrier = 1;
    } else {
        gpuWarnOnce("hiscratch", "hires: no room for a %dx%d scratch image: overlapping moves stay 1x", w, h);
    }
    writeHiSets();
    return ok;
}

void gvkHiEffectReset(void) { vk.hiEffectValid = 0; }

/* Retain the narrow and wide captures side by side. Lazy, bounded storage:
 * at most (320 + 896) x 218 x N^2 RGBA pixels, no readback or state chunks. */
int gvkHiEffectCapture(VRect narrow, const VRect* wide) {
    int n=vk.hiN, w=narrow.x1-narrow.x0+1, h=narrow.y1-narrow.y0+1;
    int ww=wide ? wide->x1-wide->x0+1 : 0;
    vk.hiEffectValid=0;
    if (n<=1 || w!=320 || h!=218 || ww<0 || ww>896 ||
        (wide && wide->y1-wide->y0+1!=h)) return 0;
    if ((w+ww)*n>vk.hiEffectW || h*n>vk.hiEffectH) {
        idleDevice();
        destroyImage(&vk.hiEffect,&vk.hiEffectMemory,&vk.hiEffectView);
        vk.hiEffectW=vk.hiEffectH=0;
        if (!createImage32(&vk.hiEffect,&vk.hiEffectMemory,&vk.hiEffectView,(w+ww)*n,h*n)) {
            writeHiSets();
            gpuWarnOnce("hieffect","hires: no room for the pause effect; using native pixels");
            return 0;
        }
        vk.hiEffectW=(w+ww)*n; vk.hiEffectH=h*n;
        toGeneral(recording(),vk.hiEffect); vk.needBarrier=1;
        writeHiSets();
    }
    copyImage(vk.hiVram,narrow.x0*n,narrow.y0*n,vk.hiEffect,0,0,w*n,h*n);
    if (wide) hiWideRows(wide->y1+1);
    if (vk.hiN!=n) return 0; /* growth under memory pressure turned upscaling off */
    if (wide) copyImage(vk.hiWide,wide->x0*n,wide->y0*n,vk.hiEffect,w*n,0,ww*n,h*n);
    vk.hiEffectWidth[0]=w; vk.hiEffectWidth[1]=ww; vk.hiEffectHeight=h;
    vk.hiEffectValid=1;
    return 1;
}

int gvkHiEffectBlur(int target,VRect dst,int narrowCentre) {
    int n=vk.hiN, w=dst.x1-dst.x0+1, h=dst.y1-dst.y0+1;
    if (!vk.hiEffectValid || target<0 || target>1 || w!=vk.hiEffectWidth[target] ||
        h!=vk.hiEffectHeight || n<=1) return 0;
    if (target) hiWideRows(dst.y1+1);
    if (vk.hiN!=n || !vk.hiEffectValid) return 0; /* growth turned upscaling off */
    int sx=target ? vk.hiEffectWidth[0]*n : 0;
    HiUtilConstants u={0}; u.n=n; u.op=HI_OP_BLUR; u.target=target;
    u.dst0[0]=dst.x0; u.dst0[1]=dst.y0; u.src0[0]=sx;
    u.ref0[0]=w; u.ref0[1]=h;
    u.flags=target && narrowCentre>=0; u.hi0[0]=narrowCentre*n;
    hiUtil(&u,dst.x0*n,dst.y0*n,w*n,h*n);
    copyImage(target ? vk.hiWide : vk.hiVram,dst.x0*n,dst.y0*n,vk.hiEffect,sx,0,w*n,h*n);
    return 1;
}

int gvkHiConfigure(int n, int color) {
    /* An explicit switch to the exact picture (Shift+F4, a 1x choice, a
     * smaller window) retries every scale; anything else stays below one
     * whose wide copy could not grow (hiWideExhausted). */
    if (n == 0 && vk.hiN != 0) vk.hiFailN = 0;
    return hiConfigure(hiScaleAllowed(n), color);
}

static int hiConfigure(int n, int color) {
    cpuResolveFlush();
    if (vk.hiColor != color) gvkHiEffectReset();
    vk.hiColor = color;
    if (vk.device == VK_NULL_HANDLE || n == vk.hiN) {
        return vk.hiN;
    }
    vramFlushCpuWrites((VRect){0,0,VRAM_W-1,VRAM_H-1});
    gvkGpuTimeReset();
    gvkHiEffectReset();
    idleDevice();
    destroyImage(&vk.hiEffect,&vk.hiEffectMemory,&vk.hiEffectView);
    vk.hiEffectW=vk.hiEffectH=0;
    destroyImage(&vk.hiVram, &vk.hiVramMemory, &vk.hiVramView);
    destroyImage(&vk.hiWide, &vk.hiWideMemory, &vk.hiWideView);
    destroyImage(&vk.hiWideCold,&vk.hiWideColdMemory,&vk.hiWideColdView);
    hiSpareFree();
    vk.hiWideColdW=vk.hiWideColdH=vk.hiWideColdY=vk.hiWideRecentH=vk.hiWideTrimClock=0;
    vk.hiWideColdPxW=vk.hiWideColdPxH=vk.hiWideSpareW=vk.hiWideSpareH=0;
    vk.hiWideQuiet=vk.hiWideQuietH=vk.hiWideKept=0;
    destroyImage(&vk.hiScratch, &vk.hiScratchMemory, &vk.hiScratchView);
    destroyImage(&vk.hiStash, &vk.hiStashMemory, &vk.hiStashView);
    vk.hiN = 0;
    vk.hiWideW = vk.hiWideH = 0;
    vk.hiWideSystem = 0;
    vk.hiScratchW = vk.hiScratchH = 0;
    vk.hiCount = vk.hiWideCount = 0;
    vk.hiWideScanned = 0; vk.hiWideDependent = 0;
    if (n > 0) {
        int ww = (vk.wideExtent + 63) & ~63;
        ww = ww > WIDE_W ? WIDE_W : ww;
        int ok = createNativeImage32(&vk.hiVram, &vk.hiVramMemory, &vk.hiVramView, VRAM_W * n, VRAM_H * n) &&
                 createImage32Memory(&vk.hiStash, &vk.hiStashMemory, &vk.hiStashView, HI_STASH_W * n,
                                     HI_STASH_SLOTS * HI_STASH_H * n,1);
        int made = !ww ? 1 : ok ? createNativeImage32(&vk.hiWide,&vk.hiWideMemory,&vk.hiWideView,ww*n,VRAM_H*n) : 0;
        ok = ok && made;
        vk.hiWideSystem = made == 2;
        if (!ok) {
            gpuLog("hires: no room for the copy at %dx; upscaling off", n);
            destroyImage(&vk.hiVram, &vk.hiVramMemory, &vk.hiVramView);
            destroyImage(&vk.hiWide, &vk.hiWideMemory, &vk.hiWideView);
            destroyImage(&vk.hiStash, &vk.hiStashMemory, &vk.hiStashView);
            n = 0;
        } else {
            vk.hiWideW = vk.hiWide != VK_NULL_HANDLE ? ww : 0;
            vk.hiWideH = vk.hiWideW ? VRAM_H : 0;
        }
    }
    vk.hiN = n;
    writeHiSets();
    if (n > 0) {
        VkCommandBuffer cmd = recording();
        toGeneral(cmd, vk.hiVram);
        toGeneral(cmd, vk.hiStash);
        toGeneral(cmd, vk.hiWide);
        vk.needBarrier = 1;
        hiReplicate(0, 0, (VRect){0, 0, VRAM_W - 1, VRAM_H - 1}, 0, 0);
        if (vk.hiWideW > 0) {
            hiReplicate(1, 1, (VRect){0, 0, vk.hiWideW - 1, vk.hiWideH - 1}, 0, 0);
        }
    }
    double mb = ((double)VRAM_W * VRAM_H + (double)HI_STASH_W * HI_STASH_SLOTS * HI_STASH_H +
                 (double)vk.hiWideW * vk.hiWideH) * n * n * 4 / 1048576.0;
    if (n == 0) {
        gpuLog("hires: off (the exact 1x picture)");
    } else {
        gpuLog("hires: %dx (VRAM %dx%d, wide target %d columns: %.0f MB), colour %d", n, VRAM_W * n, VRAM_H * n,
               vk.hiWideW, mb, color);
    }
    return n;
}

/* Room for n more of the copy's vertices in this frame context. */
static void hiReserve(int n, int wide) {
    uint32_t count = wide ? vk.hiWideCount : vk.hiCount;
    size_t bytes = (size_t)n * sizeof(HiVertex) + pendingBytes();
    if (count + (uint32_t)n > HI_MAX || frame()->vertexUsed + bytes > VERTEX_BYTES) {
        endBatch();
        if (frame()->vertexUsed + (size_t)n * sizeof(HiVertex) + pendingBytes() > VERTEX_BYTES) {
            submitFrame(NULL, NULL);
        }
    }
    recording();
    if (!vk.rendering) {
        beginBatch();
    }
}

/* Consecutive resolved native boxes are opaque replacements. Keep their
 * final words and exact union until a draw/transfer actually needs the narrow
 * enhanced image. Wide framebuffer draws can continue independently. */
static struct CpuResolve {
    uint16_t words[VRAM_H][VRAM_W];
    uint64_t mask[VRAM_H][VRAM_W/64];
    int pending,flushing,y0,y1;
} *cpuResolve;

static int hiNeedsNative(const HiVertex* v,int count,int wide) {
    if(!wide)return 1;
    for(int i=0;i<count;i++) {
        if(!(v[i].mode&DRAW_TEXTURED))continue;
        if(v[i].sample[3]) {
            /* Native feedback row runs read their owned hiScratch image
             * (binding 6), not the narrow enhanced framebuffer. */
            if((v[i].sample[3]&INTERP_SAMPLE_CAPTURE) &&
                !(v[i].sample[3]&INTERP_SAMPLE_NATIVE_STEP))return 1;
            continue;
        }
        if(((v[i].tex>>7)&3)<2)continue;
        if(!v[i].wideTex || v[i].texWindow || (int16_t)v[i].uvMin<0 ||
            (int16_t)v[i].uvMax>255 || (int)((v[i].tex&15)*64)+(int16_t)v[i].uvMax>=1024)return 1;
    }
    return 0;
}

/* Reserve first: vertex pressure or snapshot pressure can roll over to a
 * different context. Bind offsets only after both reservations succeed. */
static void hiDrawTexture(const HiVertex* v, int n, int wide, const TextureSample* sample, int filter,int captured) {
    if (vk.hiN == 0 || n <= 0 || (wide && vk.hiWide == VK_NULL_HANDLE)) return;
    if (wide) {
        int rows = 0;
        for (int i=0;i<n;i++) {
            const HiVertex* a=&v[i];
            int bottom=(int)(a->clipMax>>16)+1;
            int edge=isfinite(a->y) ? (int)ceilf(fminf(fmaxf(a->y+1,0),WIDE_H)) : WIDE_H;
            if (!(a->mode&DRAW_FILL) && bottom<edge) edge=bottom;
            if (edge>rows) rows=edge;
            /* All possible wrapped UVs, including texture-window remapping.
             * Pack/cache samples have private storage and need no native rows. */
            if (!sample && (!captured || !a->sample[3]) && (a->mode&DRAW_TEXTURED) &&
                ((a->tex>>7)&3)>=2 && a->wideTex) {
                int start=(((a->tex>>4)&1)*256+((int32_t)(a->wideTex<<1)>>17))&(WIDE_H-1);
                int end=start+256;
                if (end>WIDE_H) end=WIDE_H;
                if (end>rows) rows=end;
            }
        }
        hiWideRows(rows);
        if (vk.hiN == 0) return; /* growth under memory pressure turned upscaling off */
    }
    if(hiNeedsNative(v,n,wide))cpuResolveFlush();
    hiReserve(n, wide);
    uint32_t info[4] = {0};
    int64_t kept = -1;
    if (sample && sample->keep) {
        FrameCtx* before = frame();
        kept = resPlace(sample);
        if (frame() != before || !vk.rendering) {
            hiReserve(n, wide); /* handed over: reserve again */
        }
        if (kept >= 0) {
            info[0] = (uint32_t)kept;
            info[1] = ((uint32_t)sample->u & 0xffffu) | ((uint32_t)sample->v & 0xffffu) << 16;
            info[2] = (uint32_t)sample->w | (uint32_t)sample->h << 16;
            info[3] = (uint32_t)(filter + 1) | (uint32_t)sample->scale << 8 | 1u << 16 |
                      (sample->indices != NULL ? 1u << 17 : 0u);
        }
    }
    if (sample && kept < 0) {
        FrameCtx* f = frame();
        int i;
        for (i = 0; i < f->sampleCount; ++i) {
            if (f->samples[i].revision == sample->revision && f->samples[i].u == sample->u &&
                f->samples[i].v == sample->v && f->samples[i].w == sample->w &&
                f->samples[i].h == sample->h && f->samples[i].scale == sample->scale) break;
        }
        if (i == f->sampleCount) {
            size_t pixels = (size_t)sample->w * sample->h * sample->scale * sample->scale;
            size_t bytes = ((sample->indices != NULL ? pixels : pixels * 4) + 15) & ~(size_t)15;
            if (f->spanUsed + bytes > SPAN_BYTES || f->sampleCount == 256) {
                endBatch(); submitFrame(NULL,NULL); hiReserve(n,wide);
                f = frame(); i = 0;
            }
            uint32_t base = (uint32_t)(f->spanUsed / 4);
            uint32_t* dst = (uint32_t*)(f->spans.map + f->spanUsed);
            if (sample->indices != NULL) {
                memcpy(dst, sample->indices, pixels); /* an index map: a byte a pixel */
            } else {
                for (size_t j = 0; j < pixels; ++j) {
                    const uint8_t* p = sample->rgba + j*4;
                    dst[j] = p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
                }
            }
            f->spanUsed += bytes;
            f->samples[i].revision = sample->revision;
            f->samples[i].u = sample->u; f->samples[i].v = sample->v;
            f->samples[i].w = sample->w; f->samples[i].h = sample->h;
            f->samples[i].scale = sample->scale; f->samples[i].base = base;
            ++f->sampleCount;
        }
        info[0] = f->samples[i].base;
        info[1] = ((uint32_t)sample->u & 0xffffu) | ((uint32_t)sample->v & 0xffffu)<<16;
        info[2] = (uint32_t)sample->w | (uint32_t)sample->h<<16;
        info[3] = (uint32_t)(filter + 1) | (uint32_t)sample->scale << 8 | (sample->indices != NULL ? 1u << 17 : 0u);
    }
    HiVertex* dst = wide ? &hiWideBuf[vk.hiWideCount] : &hiBuf[vk.hiCount];
    memcpy(dst,v,(size_t)n*sizeof(*v));
    if(!captured)for (int k = 0; k < n; ++k) memcpy(dst[k].sample,info,sizeof(info));
    if (wide) vk.hiWideCount += (uint32_t)n; else vk.hiCount += (uint32_t)n;
}
void gvkHiDrawTexture(const HiVertex* v,int n,int wide,const TextureSample* sample,int filter) {hiDrawTexture(v,n,wide,sample,filter,0);}
void gvkHiDrawCaptured(const HiVertex* v,int n,int wide) {hiDrawTexture(v,n,wide,NULL,0,1);}
void gvkHiDraw(const HiVertex* v, int n) { gvkHiDrawTexture(v,n,0,NULL,0); }
void gvkHiDrawWide(const HiVertex* v, int n) { gvkHiDrawTexture(v,n,1,NULL,0); }

/* A vertex's raster shift (HiVertex.rasterShift), in 1x pixels. */
static int rasterShiftX(const HiVertex* v) { return (int16_t)(v->rasterShift & 0xFFFFu); }
static int rasterShiftY(const HiVertex* v) { return (int16_t)(v->rasterShift >> 16); }

/* hires.frag accepts pixels only inside each primitive's native clip
 * rectangle. Scissor their union before rasterization: at large scales a
 * projected floor/effect can otherwise shade far outside the display area.
 * Fills ignore that shader clip, so retain the full target for those batches.
 * Inspect every vertex to stay conservative even with mixed flat metadata.
 * tw, th: the render area (the target, or where the VRAM copy's shifted
 * triangles rasterise, HiVertex.rasterShift). */
static VkRect2D hiBatchScissor(const HiVertex* vertices, uint32_t count, int n, int tw, int th) {
    int x0 = tw / n, y0 = th / n, x1 = -1, y1 = -1;
    uint32_t previousMin = UINT32_MAX, previousMax = UINT32_MAX, previousShift = UINT32_MAX;
    for (uint32_t i = 0; i < count; i++) {
        const HiVertex* v = &vertices[i];
        if (v->mode & 0x100u) return (VkRect2D){{0, 0}, {(uint32_t)tw, (uint32_t)th}};
        if (v->clipMin == previousMin && v->clipMax == previousMax && v->rasterShift == previousShift) continue;
        previousMin = v->clipMin; previousMax = v->clipMax; previousShift = v->rasterShift;
        int sx = rasterShiftX(v), sy = rasterShiftY(v);
        int left = (int)(v->clipMin & 65535) + sx, top = (int)(v->clipMin >> 16) + sy;
        int right = (int)(v->clipMax & 65535) + sx, bottom = (int)(v->clipMax >> 16) + sy;
        if (left > right || top > bottom) continue;
        if (left < x0) x0 = left;
        if (top < y0) y0 = top;
        if (right > x1) x1 = right;
        if (bottom > y1) y1 = bottom;
    }
    if (x1 < x0 || y1 < y0 || x0 >= tw/n || y0 >= th/n) return (VkRect2D){{0, 0}, {0, 0}};
    if (x1 >= tw/n) x1 = tw/n-1;
    if (y1 >= th/n) y1 = th/n-1;
    return (VkRect2D){{x0*n, y0*n}, {(uint32_t)(x1-x0+1)*n, (uint32_t)(y1-y0+1)*n}};
}

/* A single triangle has only one invocation per destination sample. Two
 * triangles sharing an edge are also disjoint when their other corners
 * lie on opposite sides of that edge. The identical shared endpoints go
 * through the same vertex transform and Vulkan's top-left coverage rule.
 * Every other batch keeps ordered fragment interlock. */
static int hiTrianglesDisjoint(const HiVertex* a,const HiVertex* b) {
    float ax0=fminf(a[0].x,fminf(a[1].x,a[2].x)),ax1=fmaxf(a[0].x,fmaxf(a[1].x,a[2].x));
    float ay0=fminf(a[0].y,fminf(a[1].y,a[2].y)),ay1=fmaxf(a[0].y,fmaxf(a[1].y,a[2].y));
    float bx0=fminf(b[0].x,fminf(b[1].x,b[2].x)),bx1=fmaxf(b[0].x,fmaxf(b[1].x,b[2].x));
    float by0=fminf(b[0].y,fminf(b[1].y,b[2].y)),by1=fmaxf(b[0].y,fmaxf(b[1].y,b[2].y));
    int unit=1;for(int i=0;i<3;i++)if(a[i].w!=1 || b[i].w!=1)unit=0;
    /* Identical axis-aligned boundaries at W=1 receive identical vertex
     * transforms and complementary top-left raster coverage. For other
     * projections retain a whole native pixel of conservative separation. */
    float pad=unit?0:1;
    if(ax1+pad<=bx0 || bx1+pad<=ax0 || ay1+pad<=by0 || by1+pad<=ay0)return 1;
    int shared[2],used=0,otherA=-1,otherB=-1;
    for(int i=0;i<3;i++) {
        int match=-1;
        for(int j=0;j<3;j++)if(a[i].x==b[j].x && a[i].y==b[j].y && a[i].w==b[j].w) { match=j;break; }
        if(match<0)otherA=i;
        else { if(used==2)return 0;shared[used++]=i; }
    }
    if(used!=2 || otherA<0)return 0;
    for(int j=0;j<3;j++)
        if((b[j].x!=a[shared[0]].x || b[j].y!=a[shared[0]].y) &&
           (b[j].x!=a[shared[1]].x || b[j].y!=a[shared[1]].y))otherB=j;
    if(otherB<0)return 0;
    double dx=(double)a[shared[1]].x-a[shared[0]].x,dy=(double)a[shared[1]].y-a[shared[0]].y;
    double ca=dx*((double)a[otherA].y-a[shared[0]].y)-dy*((double)a[otherA].x-a[shared[0]].x);
    double cb=dx*((double)b[otherB].y-a[shared[0]].y)-dy*((double)b[otherB].x-a[shared[0]].x);
    double margin=sqrt(dx*dx+dy*dy);
    return (ca<=-margin && cb>=margin) || (ca>=margin && cb<=-margin);
}
/* Pairwise, so quadratic in triangles; the 192-vertex cap bounds it at 64
 * triangles, at most 2016 hiTrianglesDisjoint calls per batch or wave group
 * (each a bounding-box test, rarely the shared-edge one). Larger batches
 * keep the interlocked pipeline. A sort-and-sweep over the boxes would be
 * needed before raising the cap. */
static int hiBatchDisjoint(const HiVertex* v,uint32_t count) {
    if(count==3)return 1;
    if(count>192)return 0;
    for(uint32_t i=0;i<count;i+=3)for(uint32_t j=0;j<i;j+=3)
        if(!hiTrianglesDisjoint(v+i,v+j))return 0;
    return 1;
}

/* upscale_large=half (hires.c): runs of triangles covering at least half a
 * screen are rasterised at N/2, each fragment writing its 2x2 block (the
 * COARSE shaders): a quarter of the invocations for the arena, the Gears
 * and the screen-covering layers, whose overdraw is most of the copy's GPU
 * time on an integrated GPU (xg-hbc.12). Small things (text, the HUD,
 * field characters) stay at N, as does everything that indexes pixels
 * (hiTriangleCoarseEligible), and everything while smooth motion captures
 * (its replay draws at N). A batch mixing both is drawn as several passes
 * (the fragment interlock orders fragments of one rasterisation grid
 * only). Measured on the Intel UHD 770: 58 ns per 1x pixel of a coarse
 * pass against 103 at N (the stores per pixel stay). */
#define HI_COARSE_MIN_AREA 35000.0f /* 1x pixels: half of 320x224 */
static int hiTriangleCoarseEligible(const HiVertex* a) {
    /* Not fills and resolved words (pixel-indexed), not captured, replayed
     * or pack-sampled triangles (sample words: hiDrawTexture). A 1x texel
     * sprite comes out the same: its texel blocks are whole 1x pixels, so
     * no 2x2 block straddles a texel. */
    return (a->mode & (DRAW_FILL | DRAW_RESOLVED)) == 0 && a->sample[3] == 0;
}
static float hiTriangleArea(const HiVertex* a) {
    return fabsf((a[1].x - a[0].x) * (a[2].y - a[0].y) - (a[2].x - a[0].x) * (a[1].y - a[0].y)) * 0.5f;
}
/* Marks each triangle coarse (1) or not: the runs of eligible triangles
 * whose area reaches HI_COARSE_MIN_AREA. Returns whether any is. */
static int hiBatchCoarseRuns(const HiVertex* v, uint32_t count, uint8_t* mark) {
    uint32_t tris = count / 3, i = 0;
    int any = 0;
    while (i < tris) {
        uint32_t j = i;
        float area = 0;
        while (j < tris && hiTriangleCoarseEligible(&v[j * 3])) {
            area += hiTriangleArea(&v[j * 3]);
            j++;
        }
        if (j == i) {
            mark[i++] = 0;
            continue;
        }
        uint8_t coarse = area >= HI_COARSE_MIN_AREA;
        any |= coarse;
        while (i < j) mark[i++] = coarse;
    }
    return any;
}

/* The batch's triangles into the copy (after its 1x passes): one pass, or
 * one per run of coarse/full triangles (hiBatchCoarseRuns). */
/* The power of two at or above a hi-res target's extent, as far as the device
 * allows (else the extent itself). */
static int hiViewportSize(int extent) {
    int p = 1;
    while (p < extent) p <<= 1;
    return (uint32_t)p <= vk.limits.maxViewportDimensions[0] && (uint32_t)p <= vk.limits.maxViewportDimensions[1] ? p : extent;
}

static void drawHiBatch(FrameCtx* f, int wide) {
    static uint8_t coarseMark[HI_MAX / 3];
    uint32_t count = wide ? vk.hiWideCount : vk.hiCount;
    const HiVertex* src = wide ? hiWideBuf : hiBuf;
    int n = vk.hiN;
    int tw = (wide ? vk.hiWideW : VRAM_W) * n, th = (wide ? WIDE_H : VRAM_H) * n;
    /* The VRAM copy's triangles rasterise where the wide pass rasterises them
     * (HiVertex.rasterShift): its render area reaches as far as the wide
     * target's, within the device's limit (beyond it the shift is dropped:
     * the copies then differ at edges as they did before). */
    int rw = tw, rh = th;
    if (!wide && vk.hiWide != VK_NULL_HANDLE) {
        rw = vk.hiWideW * n > tw ? vk.hiWideW * n : tw;
        rh = (VRAM_H + 256) * n;
        if ((uint32_t)rw > vk.limits.maxFramebufferWidth) rw = tw;
        if ((uint32_t)rh > vk.limits.maxFramebufferHeight) rh = th;
    }
    if (wide) {
        vk.hiWideCount = 0;
        vk.hiWideScanned = 0; vk.hiWideDependent = 0;
    } else {
        vk.hiCount = 0;
    }
    if (n == 0 || tw == 0 || count == 0) {
        return;
    }
    /* This pass reads the native VRAM and the copies written by the
     * preceding passes, including when there was no wide draw. */
    rasterBarrier(f->cmd);
    if(vk.orderedEnabled) {
        int q = gtStart(f->cmd, wide ? GT_HIWIDE : GT_HIRES);
        VkDeviceSize offset=f->vertexUsed;
        for (uint32_t i = 0; i < count; i++) ((HiVertex*)src)[i].rasterShift = 0; /* the ordered list rasterises in place */
        memcpy(f->vertices.map+offset,src,(size_t)count*sizeof(HiVertex));
        HiDrawConstants c={n,vk.hiColor,tw,th,vk.hiWideW*n,WIDE_H*n,0,0,1};
        drawOrdered(f,src,sizeof(HiVertex),offset,0,count,n,tw,th,wide?3:2,&c,sizeof(c));
        f->vertexUsed+=(size_t)count*sizeof(HiVertex);
        f->vertexUsed=(f->vertexUsed+sizeof(GpuVertex)-1)/sizeof(GpuVertex)*sizeof(GpuVertex);
        gtStop(f->cmd,q);return;
    }
    int anyCoarse = n >= 4 && (n & 1) == 0 && hiresCoarseLarge() && !interpolationCapturing() &&
                    hiBatchCoarseRuns(src, count, coarseMark);
    if (vk.env.hiresStats) { /* what ends the coarse runs: the batch's triangles by class */
        uint32_t cls[8] = {0}, runs = 0, coarseRuns = 0; uint8_t prev = 2;
        for (uint32_t i = 0; i + 2 < count; i += 3) {
            const HiVertex* a = &src[i];
            int k = (a->mode & DRAW_FILL) ? 1 : (a->mode & DRAW_RESOLVED) ? 2 : (a->sample[3] & 0x80000u) ? 3 :
                    (a->sample[3] & (0x100000u | 0x400000u)) ? 4 : a->sample[3] != 0 ? 5 : (a->mode & DRAW_FRINGE) ? 6 : (a->mode & DRAW_SURFACE) ? 7 : 0;
            cls[k]++;
            if (anyCoarse && coarseMark[i / 3] != prev) { runs++; coarseRuns += coarseMark[i / 3]; prev = coarseMark[i / 3]; }
        }
        gpuLog("hires batch: %s tris %u runs %u coarse %u | plain %u fill %u resolved %u captured %u replay %u sampled %u fringe %u surface %u",
               wide ? "wide" : "narrow", count / 3, runs, coarseRuns, cls[0], cls[1], cls[2], cls[3], cls[4], cls[5], cls[6], cls[7]);
    }
    if (rw == tw && rh == th && !wide) {
        for (uint32_t i = 0; i < count; i++) ((HiVertex*)src)[i].rasterShift = 0; /* no room to shift: as before */
    }
    VkDeviceSize offset = f->vertexUsed;
    memcpy(f->vertices.map + f->vertexUsed, src, (size_t)count * sizeof(HiVertex));
    vkCmdBindVertexBuffers(f->cmd, 0, 1, &f->vertices.buffer, &offset);
    for (uint32_t first = 0; first < count;) {
        int cs = anyCoarse && coarseMark[first / 3] ? 2 : 1;
        uint32_t end = first + 3;
        while (end < count && (!anyCoarse || coarseMark[end / 3] == coarseMark[first / 3])) end += 3;
        uint32_t run = end - first;
        if (first > 0) rasterBarrier(f->cmd); /* the earlier run's pixels, in order */
        int q = gtStart(f->cmd, wide ? GT_HIWIDE : GT_HIRES);
        VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea.extent.width = (uint32_t)rw;
        ri.renderArea.extent.height = (uint32_t)rh;
        ri.layerCount = 1;
        vkCmdBeginRendering(f->cmd, &ri);
        int disjoint = hiBatchDisjoint(src + first, run) && !vk.env.allInterlock;
        VkPipeline pipeline;
        if (cs > 1) pipeline = wide ? (disjoint ? vk.hiWideCoarseUnorderedPipeline : vk.hiWideCoarsePipeline)
                                    : (disjoint ? vk.hiCoarseUnorderedPipeline : vk.hiCoarsePipeline);
        else pipeline = wide ? (disjoint ? vk.interpolationWidePipeline : vk.hiWidePipeline)
                             : (disjoint ? vk.hiUnorderedPipeline : vk.hiDrawPipeline);
        if (vk.env.hiresStats && q >= 0) {
            /* The pass's 1x pixel area: the sum of its triangles' areas (a
             * fill-rate measure; the scissor is the clip rectangle, not the
             * coverage). */
            __typeof__(f->passStats[0])* ps = &f->passStats[q / 2];
            ps->tris = run / 3; ps->area = ps->semi = ps->textured = ps->surface = ps->fringe = ps->runMax = 0; ps->runs = 0;
            ps->interlock = (uint8_t)!disjoint; ps->coarse = (uint8_t)(cs > 1);
            float runArea = 0; int inRun = 0;
            for (uint32_t i = first; i + 2 < end; i += 3) {
                const HiVertex* a = &src[i];
                float area = hiTriangleArea(a);
                ps->area += area;
                if (a->mode & DRAW_SEMI) ps->semi += area;
                if (a->mode & DRAW_TEXTURED) ps->textured += area;
                if (a->mode & DRAW_SURFACE) ps->surface += area;
                if (a->mode & DRAW_FRINGE) ps->fringe += area;
                if (hiTriangleCoarseEligible(a)) { if (!inRun) { inRun = 1; ps->runs++; runArea = 0; } runArea += area; if (runArea > ps->runMax) ps->runMax = runArea; }
                else inRun = 0;
            }
        }
        vkCmdBindPipeline(f->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdBindDescriptorSets(f->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.hiLayout, 0, 1, &f->set, 0, NULL);
        /* One viewport for both copies, a power of two: the vertex shader's
         * NDC and the device's viewport transform are then exact, and with
         * the clip-space position (NDC x w) and the division identical the
         * window position is too, to the last bit (perspective textures:
         * w != 1), so the VRAM copy and the wide target rasterise a primitive
         * identically (hires.vert). The scissor keeps the pixels inside the
         * target. */
        int vmax = rw > vk.hiWideW * n ? rw : vk.hiWideW * n, hmax = rh > WIDE_H * n ? rh : WIDE_H * n;
        int vw = hiViewportSize(vmax), vh = hiViewportSize(hmax);
        VkViewport viewport = {0.0f, 0.0f, (float)(vw / cs), (float)(vh / cs), 0.0f, 1.0f};
        VkRect2D scissor = hiBatchScissor(src + first, run, n, rw, rh);
        if (cs > 1) { /* the clip rectangle's blocks (cs divides n: blocks never straddle a 1x pixel) */
            scissor.offset.x /= cs; scissor.offset.y /= cs;
            scissor.extent.width = (scissor.extent.width + (uint32_t)cs - 1) / (uint32_t)cs;
            scissor.extent.height = (scissor.extent.height + (uint32_t)cs - 1) / (uint32_t)cs;
        }
        vkCmdSetViewport(f->cmd, 0, 1, &viewport);
        vkCmdSetScissor(f->cmd, 0, 1, &scissor);
        HiDrawConstants c = {n, vk.hiColor, vw, vh, vk.hiWideW * n, WIDE_H * n, 0, 0, cs};
        vkCmdPushConstants(f->cmd, vk.hiLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(c), &c);
        vkCmdDraw(f->cmd, run, 1, first, 0);
        vkCmdEndRendering(f->cmd);
        gtStop(f->cmd, q);
        first = end;
    }
    /* Back on the 1x passes' vertex grid (their draws index by vertex). */
    f->vertexUsed += (size_t)count * sizeof(HiVertex);
    f->vertexUsed = (f->vertexUsed + sizeof(GpuVertex) - 1) / sizeof(GpuVertex) * sizeof(GpuVertex);
}

/* A resolved native box used to end every draw batch, upload 1x pixels,
 * then replicate them in a utility pass. Own its words in this context and
 * let the normal ordered enhanced batch write exactly the same RGB/mask. */
static void hiResolveCpuNow(VRect r,const uint16_t* data,int pitch) {
    if(!vk.hiN)return;
    int w=r.x1-r.x0+1,h=r.y1-r.y0+1;
    size_t words=((size_t)w*h+1)/2,bytes=words*4;
    hiReserve(6,0);
    if(frame()->spanUsed+bytes>SPAN_BYTES) { endBatch();submitFrame(NULL,NULL);hiReserve(6,0); }
    FrameCtx* f=frame();uint32_t offset=(uint32_t)(f->spanUsed/4);
    uint32_t* out=(uint32_t*)(f->spans.map+f->spanUsed);memset(out,0,bytes);
    for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
        size_t at=(size_t)y*w+x;out[at/2]|=(uint32_t)data[(size_t)y*pitch+x]<<((at&1)*16);
    }
    f->spanUsed+=bytes;
    HiVertex v[6]={0};int xs[6]={0,1,0,1,0,1},ys[6]={0,0,1,0,1,1};
    for(int k=0;k<6;k++) {
        v[k].x=xs[k]?r.x1+1:r.x0;v[k].y=ys[k]?r.y1+1:r.y0;v[k].w=1;
        v[k].mode=DRAW_RESOLVED;v[k].clipMin=(uint32_t)r.x0|(uint32_t)r.y0<<16;
        v[k].clipMax=(uint32_t)r.x1|(uint32_t)r.y1<<16;
        v[k].sample[0]=offset;v[k].sample[1]=(uint32_t)r.x0|(uint32_t)r.y0<<16;v[k].sample[2]=(uint32_t)w;
    }
    hiDrawTexture(v,6,0,NULL,0,1);
}

static void cpuResolveFlush(void) {
    if(!cpuResolve || !cpuResolve->pending || cpuResolve->flushing)return;
    cpuResolve->flushing=1;cpuResolve->pending=0;
    endNativeBatch();
    for(int y=cpuResolve->y0;y<=cpuResolve->y1;) {
        const uint64_t* row=cpuResolve->mask[y];
        int end=y+1;
        while(end<=cpuResolve->y1 && !memcmp(row,cpuResolve->mask[end],sizeof(cpuResolve->mask[0])))end++;
        for(int x=0;x<VRAM_W;) {
            if(!(row[x/64]&(UINT64_C(1)<<(x&63)))) { x++;continue; }
            int x0=x++;
            while(x<VRAM_W && (row[x/64]&(UINT64_C(1)<<(x&63))))x++;
            hiResolveCpuNow((VRect){x0,y,x-1,end-1},&cpuResolve->words[y][x0],VRAM_W);
        }
        y=end;
    }
    memset(cpuResolve->mask,0,sizeof(cpuResolve->mask));
    endNativeBatch();cpuResolve->flushing=0;
}

void gvkHiResolveCpu(VRect r,const uint16_t* data,int pitch) {
    if(!vk.hiN)return;
    if(!cpuResolve)cpuResolve=calloc(1,sizeof(*cpuResolve));
    if(!cpuResolve || vk.env.immediateResolve) { hiResolveCpuNow(r,data,pitch);return; }
    /* An earlier enhanced draw precedes this opaque replacement. */
    if(vk.hiCount)endNativeBatch();
    if(!cpuResolve->pending) { cpuResolve->y0=r.y0;cpuResolve->y1=r.y1; }
    if(r.y0<cpuResolve->y0)cpuResolve->y0=r.y0;
    if(r.y1>cpuResolve->y1)cpuResolve->y1=r.y1;
    cpuResolve->pending=1;
    for(int y=r.y0;y<=r.y1;y++) {
        memcpy(&cpuResolve->words[y][r.x0],data+(size_t)(y-r.y0)*pitch,(size_t)(r.x1-r.x0+1)*2);
        for(int w=r.x0/64;w<=r.x1/64;w++) {
            uint64_t bits=UINT64_MAX;
            if(w==r.x0/64)bits&=UINT64_MAX<<(r.x0&63);
            if(w==r.x1/64)bits&=UINT64_MAX>>(63-(r.x1&63));
            cpuResolve->mask[y][w]|=bits;
        }
    }
}

void gvkHiLoad(VRect r, const uint16_t* data, int pitch, int how, int slot, const uint16_t* ref, int refPitch) {
    vramFlushCpuWrites(r);
    int n = vk.hiN;
    if (n == 0) {
        return;
    }
    int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1;
    HiUtilConstants u = {0};
    u.n = n;
    u.target = 0;
    u.dst0[0] = r.x0;
    u.dst0[1] = r.y0;
    if (how == HI_LOAD_STASH && w <= HI_STASH_W && h <= HI_STASH_H && slot >= 0 && slot < HI_STASH_SLOTS) {
        /* The upload at (0, 0) of the scratch image, the stash's 1x picture below. */
        uploadImage(vk.scratch, (VRect){0, 0, w - 1, h - 1}, data, pitch);
        uploadImage(vk.scratch, (VRect){0, HI_STASH_H, w - 1, HI_STASH_H + h - 1}, ref, refPitch);
        u.op = HI_OP_LOAD;
        u.refKind = 1;
        u.ref0[1] = HI_STASH_H;
        u.hiKind = 1;
        u.hi0[1] = slot * HI_STASH_H * n;
    } else {
        uploadImage(vk.scratch, r, data, pitch);
        u.op = how == HI_LOAD_KEEP ? HI_OP_LOAD : HI_OP_REPLICATE;
        u.srcKind = 2;
        u.src0[0] = r.x0;
        u.src0[1] = r.y0;
    }
    hiUtil(&u, r.x0 * n, r.y0 * n, w * n, h * n);
}

int gvkHiFeedbackSource(VRect source) {
    int n=vk.hiN,w=source.x1-source.x0+1,h=source.y1-source.y0+1;
    if(!n||!hiScratchFor(w*n,h*n))return 0;
    hiWideRows(source.y1+1);
    if(vk.hiN!=n)return 0; /* growth turned upscaling off */
    copyImage(vk.hiWide,source.x0*n,source.y0*n,vk.hiScratch,0,0,w*n,h*n);return 1;
}

void gvkHiStash(VRect r, int slot) {
    int n = vk.hiN;
    int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1;
    if (n == 0 || slot < 0 || slot >= HI_STASH_SLOTS || w > HI_STASH_W || h > HI_STASH_H) {
        return;
    }
    copyImage(vk.hiVram, r.x0 * n, r.y0 * n, vk.hiStash, 0, slot * HI_STASH_H * n, w * n, h * n);
}

static int rectsMeet(VRect a, VRect b) {
    return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1;
}

/* A move inside the copy of VRAM: a copy, or with the mask bits a pass. */
static void hiBlit(VkImage from, int srcKind, int sx, int sy, VRect d, int setMask, int checkMask) {
    int n = vk.hiN;
    int w = (d.x1 - d.x0 + 1) * n, h = (d.y1 - d.y0 + 1) * n;
    if (!setMask && !checkMask) {
        copyImage(from, sx, sy, vk.hiVram, d.x0 * n, d.y0 * n, w, h);
        return;
    }
    HiUtilConstants u = {0};
    u.n = n;
    u.op = HI_OP_BLIT;
    u.srcKind = srcKind;
    u.dst0[0] = d.x0;
    u.dst0[1] = d.y0;
    u.src0[0] = sx;
    u.src0[1] = sy;
    u.flags = (setMask ? 1 : 0) | (checkMask ? 2 : 0);
    hiUtil(&u, d.x0 * n, d.y0 * n, w, h);
}

void gvkHiMove(const VPiece* src, int ns, const VPiece* dst, int nd, int w, int h, int setMask, int checkMask) {
    int n = vk.hiN;
    if (n == 0) {
        return;
    }
    if (ns == 1 && nd == 1 && !rectsMeet(src[0].r, dst[0].r)) {
        hiBlit(vk.hiVram, 0, src[0].r.x0 * n, src[0].r.y0 * n, dst[0].r, setMask, checkMask);
        return;
    }
    /* Overlapping, or wrapping round VRAM: through the scratch image; where
     * source and destination meet, in the 1x copy's steps (vram.c
     * vramCopySteps, libgpu.c moveCopy), so both copies agree. */
    if (!hiScratchFor(w * n, h * n)) {
        return;
    }
    static uint16_t step[VRAM_H * (VRAM_W / COPY_PIECE)];
    static VRect rects[VRAM_H * (VRAM_W / COPY_PIECE)];
    int meet = vramCopyMeets(src, ns, dst, nd);
    /* The first piece of vramWrap starts at the copy's corner. */
    int sx = src[0].r.x0, sy = src[0].r.y0, dx = dst[0].r.x0, dy = dst[0].r.y0;
    int steps = meet ? vramCopySteps(sx, sy, dx, dy, w, h, step) : 1;
    for (int s = 0; s < steps; s++) {
        int nr = 1;
        rects[0] = (VRect){0, 0, w - 1, h - 1};
        if (meet) {
            nr = vramCopyStepRects(step, w, h, s, rects);
        }
        for (int pass = 0; pass < 2; pass++) {
            for (int k = 0; k < nr; k++) {
                VRect r = rects[k];
                VPiece p[4];
                int np = vramWrap((pass ? dx : sx) + r.x0, (pass ? dy : sy) + r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1, p);
                for (int i = 0; i < np; i++) {
                    int ox = (r.x0 + p[i].dx) * n, oy = (r.y0 + p[i].dy) * n;
                    if (pass == 0) {
                        copyImage(vk.hiVram, p[i].r.x0 * n, p[i].r.y0 * n, vk.hiScratch, ox, oy,
                                  (p[i].r.x1 - p[i].r.x0 + 1) * n, (p[i].r.y1 - p[i].r.y0 + 1) * n);
                    } else {
                        hiBlit(vk.hiScratch, 1, ox, oy, p[i].r, setMask, checkMask);
                    }
                }
            }
        }
    }
}

void gvkUploadWideMasked(VRect r, const uint16_t* src, int pitch) {
    if (vk.device == VK_NULL_HANDLE) {
        return;
    }
    wideTouch(r.x1);
    hiWideRows(r.y1+1);
    uploadImage(vk.wide, r, src, pitch);
    int n = vk.hiN;
    if (n != 0 && vk.hiWide != VK_NULL_HANDLE) {
        HiUtilConstants u = {0};
        u.n = n;
        u.op = HI_OP_MASK;
        u.target = 1;
        hiUtil(&u, r.x0 * n, r.y0 * n, (r.x1 - r.x0 + 1) * n, (r.y1 - r.y0 + 1) * n);
    }
}

static int readImage32(VkImage image, uint32_t* dst, int x, int y, int w, int h, int tw, int th) {
    if (image == VK_NULL_HANDLE || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > tw || y + h > th) {
        return 0;
    }
    size_t bytes = (size_t)w * h * 4;
    if (bytes > vk.hiReadbackBytes) {
        destroyHostBuffer(&vk.hiReadback);
        createHostBuffer(&vk.hiReadback, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1);
        vk.hiReadbackBytes = bytes;
    }
    endBatch();
    VkCommandBuffer cmd = recording();
    barrierIfNeeded(cmd);
    VkBufferImageCopy c = {0};
    c.imageSubresource = colorLayers;
    c.imageOffset.x = x;
    c.imageOffset.y = y;
    c.imageExtent.width = (uint32_t)w;
    c.imageExtent.height = (uint32_t)h;
    c.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_GENERAL, vk.hiReadback.buffer, 1, &c);
    hostReadBarrier(cmd);
    vk.needBarrier = 1;
    FrameCtx* f = frame();
    submitFrame(NULL, NULL);
    waitFrame(f);
    memcpy(dst, vk.hiReadback.map, bytes);
    return 1;
}

int gvkHiRead(int wide, uint32_t* dst, int x, int y, int w, int h) {
    if(!wide)cpuResolveFlush();
    if (!vk.hiN) return 0;
    if (wide && y>=0 && h>0 && y+h<=WIDE_H*vk.hiN) hiWideRows((y+h+vk.hiN-1)/vk.hiN);
    return readImage32(wide?vk.hiWide:vk.hiVram,dst,x,y,w,h,(wide?vk.hiWideW:VRAM_W)*vk.hiN,(wide?vk.hiWideH:VRAM_H)*vk.hiN);
}
int gvkInterpolationRead(uint32_t* dst, int x, int y, int w, int h) {
    return readImage32(vk.interpolationImage[INTERP_FRAMES],dst,x,y,w,h,
        vk.interpolationCanvasW*vk.interpolationN,vk.interpolationCanvasH*vk.interpolationN);
}


void gvkReadback(VRect r, uint16_t* dst, int pitch) {
    vramFlushCpuWrites((VRect){0,0,VRAM_W-1,VRAM_H-1});
    endBatch();
    VkCommandBuffer cmd = recording();
    barrierIfNeeded(cmd);
    VkBufferImageCopy c = {0};
    c.imageSubresource = colorLayers;
    int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1;
    c.imageOffset.x = r.x0;
    c.imageOffset.y = r.y0;
    c.imageExtent.width = (uint32_t)w;
    c.imageExtent.height = (uint32_t)h;
    c.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, vk.vram, VK_IMAGE_LAYOUT_GENERAL, vk.readback.buffer, 1, &c);
    hostReadBarrier(cmd);
    vk.needBarrier = 1;
    FrameCtx* f = frame();
    submitFrame(NULL, NULL);
    waitFrame(f);
    for (int y = 0; y < h; y++)
        memcpy(dst + (size_t)y * pitch, vk.readback.map + (size_t)y * w * 2, (size_t)w * 2);
}

/* Late reads (save states' rewind captures): copies queued behind the work
 * recorded so far, into a buffer of their own, without waiting; by the next
 * capture the frame that carried them has normally long been done. */
#define LATE_BYTES ((size_t)VRAM_W * VRAM_H * 2 + (size_t)WIDE_W * WIDE_VIEW_Y * 2 + (64u << 10))
static struct {
    HostBuffer bufs[2]; /* by turns: rewind's pack worker may still be comparing the last list's */
    HostBuffer* buf;
    size_t used;
    int queued, frame;
    uint64_t use;
} late;

ptrdiff_t gvkLateRead(int wide, VRect r) {
    if(!wide)vramFlushCpuWrites(r);
    int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1;
    size_t bytes = (size_t)w * h * 2;
    if (vk.device == VK_NULL_HANDLE || (wide && vk.wide == VK_NULL_HANDLE) || w <= 0 || h <= 0) {
        return -1;
    }
    if (!late.queued) {
        late.used = 0;
        late.buf = &late.bufs[late.buf == &late.bufs[0]];
    }
    if (late.used + bytes > LATE_BYTES) {
        return -1;
    }
    if (late.buf->buffer == VK_NULL_HANDLE) {
        createHostBuffer(late.buf, LATE_BYTES, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1);
    }
    endBatch();
    VkCommandBuffer cmd = recording();
    barrierIfNeeded(cmd);
    VkBufferImageCopy c = {0};
    c.bufferOffset = late.used;
    c.imageSubresource = colorLayers;
    c.imageOffset.x = r.x0;
    c.imageOffset.y = r.y0;
    c.imageExtent.width = (uint32_t)w;
    c.imageExtent.height = (uint32_t)h;
    c.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, wide ? vk.wide : vk.vram, VK_IMAGE_LAYOUT_GENERAL, late.buf->buffer, 1, &c);
    hostReadBarrier(cmd);
    vk.needBarrier = 1;
    late.queued = 1;
    late.frame = vk.cur;
    late.use = frame()->uses;
    ptrdiff_t at = (ptrdiff_t)(late.used / 2);
    late.used += (bytes + 15) & ~(size_t)15;
    return at;
}

const uint16_t* gvkLateWait(void) {
    if (late.queued) {
        FrameCtx* f = &vk.frames[late.frame];
        if (f->uses == late.use) { /* else used again since: its fence was waited for first */
            if (f->recording) {
                endBatch();
                submitFrame(NULL, NULL); /* not handed over yet (no frame since) */
            }
            waitFrame(f);
        }
        late.queued = 0;
    }
    return late.buf != NULL ? (const uint16_t*)late.buf->map : NULL;
}

int gvkHasInterlock(void) {
    return vk.interlock;
}

/* Another thread's readback (a stall report while the game thread spins in a
 * loop that reaches no safe point): the second queue with its own pool,
 * fence and buffer, so nothing the game thread holds is touched. What the
 * game recorded since its last submission is not in the picture. */
int gvkRescueRead(int wide, uint16_t* dst, int x, int y, int w, int h) {
    if (vk.device == VK_NULL_HANDLE || vk.rescueQueue == VK_NULL_HANDLE || w <= 0 || h <= 0 ||
        (size_t)w * h * 2 > READBACK_BYTES) {
        return 0;
    }
    if (vk.rescuePool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pi.queueFamilyIndex = vk.queueFamily;
        if (vkCreateCommandPool(vk.device, &pi, NULL, &vk.rescuePool) != VK_SUCCESS) {
            return 0;
        }
        VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = vk.rescuePool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (vkAllocateCommandBuffers(vk.device, &ai, &vk.rescueCmd) != VK_SUCCESS ||
            vkCreateFence(vk.device, &fi, NULL, &vk.rescueFence) != VK_SUCCESS) {
            vkDestroyCommandPool(vk.device, vk.rescuePool, NULL);
            vk.rescuePool = VK_NULL_HANDLE;
            return 0;
        }
        createHostBuffer(&vk.rescueBuffer, READBACK_BYTES, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1);
    }
    /* The game thread's submitted work first (its writes become available). */
    for (int i = 0; i < FRAMES; i++) {
        if (vk.frames[i].submitted &&
            vkWaitForFences(vk.device, 1, &vk.frames[i].fence, VK_TRUE, 500000000ull) != VK_SUCCESS) {
            return 0; /* the device is still busy with it, or lost */
        }
    }
    VkCommandBuffer cmd = vk.rescueCmd;
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkResetCommandBuffer(cmd, 0) != VK_SUCCESS || vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS) {
        return 0;
    }
    fullBarrier(cmd);
    VkBufferImageCopy c = {0};
    c.imageSubresource = colorLayers;
    c.imageOffset.x = x;
    c.imageOffset.y = y;
    c.imageExtent.width = (uint32_t)w;
    c.imageExtent.height = (uint32_t)h;
    c.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, wide ? vk.wide : vk.vram, VK_IMAGE_LAYOUT_GENERAL, vk.rescueBuffer.buffer, 1, &c);
    hostReadBarrier(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        return 0;
    }
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkResetFences(vk.device, 1, &vk.rescueFence);
    if (vkQueueSubmit(vk.rescueQueue, 1, &si, vk.rescueFence) != VK_SUCCESS ||
        vkWaitForFences(vk.device, 1, &vk.rescueFence, VK_TRUE, 2000000000ull) != VK_SUCCESS) {
        vk.rescueQueue = VK_NULL_HANDLE; /* its command buffer may still be pending: not again */
        return 0;
    }
    memcpy(dst, vk.rescueBuffer.map, (size_t)w * h * 2);
    return 1;
}

/* How long vblank may wait for a swapchain image (XG_VK_ACQUIRE_US, default
 * 1 ms). Not zero: with a zero timeout Wayland drivers never read the
 * compositor's buffer releases, and every image stays busy. */
static uint64_t acquireTimeout(void) {
    const char* e = getenv("XG_VK_ACQUIRE_US");
    return (e != NULL && e[0] != '\0' ? (uint64_t)atoll(e) : 1000) * 1000;
}

/* XG_PRESENT_LOG=1: a line per vblank (libgpu.c) and every stall recovery. */
int presentLogging(void) {
    static int value = -1;
    if (value < 0) {
        value = envFlag("XG_PRESENT_LOG");
    }
    return value;
}

/* The window's pixel size changed (a resize, fullscreen): a new swapchain.
 * Wayland reports no size of its own (currentExtent 0xFFFFFFFF) and never
 * says the swapchain is out of date, so the window is the only witness. */
static void checkWindowSize(void) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(vk.window, &w, &h);
    if (w != vk.windowW || h != vk.windowH) {
        vk.windowW = w;
        vk.windowH = h;
        vk.swapchainStale = 1;
    }
}

/* XG_PRESENT_TEST=leak@<vblank>[/<every>]: at that vblank (and every
 * <every> after it) every swapchain image the presentation engine gives back
 * is acquired and never presented, as if the compositor kept them all: the
 * stall guard's test with a real swapchain. */
static void testLeakImages(uint32_t vblank) {
    VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    int n = 0;
    VK_CHECK(vkCreateFence(vk.device, &fi, NULL, &fence));
    for (int i = 0; i < MAX_SWAPCHAIN_IMAGES; i++) {
        uint32_t index;
        VkResult r = vkAcquireNextImageKHR(vk.device, vk.swapchain, 20000000ull, VK_NULL_HANDLE, fence, &index);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            break;
        }
        vkWaitForFences(vk.device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(vk.device, 1, &fence);
        n++;
    }
    vkDestroyFence(vk.device, fence, NULL);
    gpuLog("vulkan: XG_PRESENT_TEST: vblank %u: %d swapchain images held back", vblank, n);
}

/* A swapchain image for this vblank, or 0 (none free, minimised, no window). */
static int acquireImage(VkSemaphore acquired, uint32_t* index, uint32_t vblank) {
    /* The swapchain needs host synchronisation: a copy job still on the
     * present worker (copy slots busy, Speedup just turned off) owns it
     * until done. This picture's present then also stays on this thread. */
    presentDrain();
    vk.lastAcquire = PRESENT_NOT_TRIED;
    vk.acquireMs = 0;
    if (vk.surface == VK_NULL_HANDLE && !vk.surfaceLost) {
        return 0;
    }
    checkWindowSize();
    if (vk.swapchain == VK_NULL_HANDLE || vk.swapchainStale) {
        if (vk.swapchain == VK_NULL_HANDLE && SDL_GetTicksNS() < vk.swapchainRetryNs) {
            return 0;
        }
        recreateSwapchain();
        if (vk.swapchain == VK_NULL_HANDLE) {
            return 0;
        }
    }
    if (vk.testLeakAt != 0 && vblank >= vk.testLeakAt) {
        vk.testLeakAt = vk.testLeakEvery != 0 ? vblank + vk.testLeakEvery : 0;
        testLeakImages(vblank);
    }
    uint64_t t0 = SDL_GetTicksNS();
    VkResult r = vkAcquireNextImageKHR(vk.device, vk.swapchain, acquireTimeout(), acquired, VK_NULL_HANDLE, index);
    vk.acquireMs = (double)(SDL_GetTicksNS() - t0) * 1e-6;
    vk.lastAcquire = r;
    switch (r) {
    case VK_SUCCESS:
        return 1;
    case VK_SUBOPTIMAL_KHR:
        vk.swapchainStale = 1; /* the image is ours: show it, then make a better swapchain */
        return 1;
    case VK_TIMEOUT:
    case VK_NOT_READY:
        /* No image free yet: skip this vblank's picture rather than hold up
         * the game (the stall guard steps in if it lasts). */
        vk.framesSkipped++;
        return 0;
    case VK_ERROR_OUT_OF_DATE_KHR:
        vk.swapchainStale = 1;
        return 0;
    case VK_ERROR_SURFACE_LOST_KHR:
        loseSurface("vkAcquireNextImageKHR");
        return 0;
    default:
        presentFailed("vkAcquireNextImageKHR", r);
        vk.swapchainStale = 1;
        return 0;
    }
}

/* The stall guard: a visible window that gets no new picture for stallLimit
 * vblanks in a row (every acquire timing out, a swapchain that cannot be
 * made, a present that keeps failing) gets a new swapchain; every third time
 * in a row a new surface as well. Said once; XG_PRESENT_LOG says each. */
static void watchPresent(int presented, uint32_t vblank) {
    if (presented || vk.stallBase <= 0) {
        vk.noPicture = 0;
        vk.stallLimit = vk.stallBase;
        return;
    }
    if (vk.window == NULL || (vk.surface == VK_NULL_HANDLE && !vk.surfaceLost) ||
        (SDL_GetWindowFlags(vk.window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED))) {
        vk.noPicture = 0; /* nobody can see it: nothing is wrong */
        return;
    }
    if (++vk.noPicture < vk.stallLimit) {
        return;
    }
    vk.stallRecoveries++;
    int surface = vk.stallRecoveries % 3 == 0 && vk.surface != VK_NULL_HANDLE;
    if (vk.stallRecoveries == 1 || presentLogging()) {
        gpuLog("vulkan: vblank %u: no picture shown for %d vblanks (last acquire %s, last present %s, %ux%u, %u images)%s; "
               "making the swapchain again%s",
               vblank, vk.noPicture, resultName(vk.lastAcquire), resultName(vk.lastPresent),
               vk.swapchainExtent.width, vk.swapchainExtent.height, vk.swapchainImageCount,
               vk.swapchain == VK_NULL_HANDLE ? " (no swapchain)" : "", surface ? " and the surface" : "");
    }
    vk.noPicture = 0;
    if (vk.stallLimit < vk.stallBase * 16) {
        vk.stallLimit *= 2; /* recoveries that do not help: try less often */
    }
    presentDrain();
    vkDeviceWaitIdle(vk.device);
    for (int i = 0; i < FRAMES; i++) {
        vk.frames[i].submitted = 0;
    }
    if (surface) {
        destroySwapchain();
        vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
        vk.surface = VK_NULL_HANDLE;
        vk.surfaceLost = 1;
        vk.swapchainRetryNs = 0;
    } else {
        vk.swapchainStale = 1;
        vk.swapchainRetryNs = 0;
    }
}

void gvkPresentStats(GvkPresentStats* out) {
    presentCollect();
    memset(out, 0, sizeof(*out));
    out->pending=asyncPresent.queued-asyncPresent.collected;
    out->nativeFrames = hostPresent.nativeFrames;
    out->hostFrames = hostPresent.sequence;
    out->missedHostSlots = hostPresent.missed;
    out->autoFrames = autoTime.frame;
    out->hostTarget = hostPresent.rate;
    out->hostActive = hostPresent.active;
    out->shown = vk.framesShown;
    out->skipped = vk.framesSkipped;
    out->recoveries = vk.stallRecoveries;
    out->lastAcquire = vk.lastAcquire;
    out->lastPresent = vk.lastPresent;
    out->acquireName = resultName(vk.lastAcquire);
    out->presentName = resultName(vk.lastPresent);
    out->image = vk.lastImage;
    out->acquireMs = vk.acquireMs;
    out->noPicture = vk.noPicture;
    out->width = (int)vk.swapchainExtent.width;
    out->height = (int)vk.swapchainExtent.height;
    out->images = (int)vk.swapchainImageCount;
    out->windowW = vk.windowW;
    out->windowH = vk.windowH;
    out->haveSwapchain = vk.swapchain != VK_NULL_HANDLE;
    out->stale = vk.swapchainStale;
    out->surface = vk.surface != VK_NULL_HANDLE ? 1 : vk.surfaceLost ? -1 : 0;
    static const char* const modes[] = {"immediate", "mailbox", "fifo", "fifo-relaxed"};
    out->mode = vk.swapchain == VK_NULL_HANDLE ? "-" : (unsigned)vk.presentMode < 4 ? modes[vk.presentMode] : "other";
}

static void destroyDumpTarget(void) {
    if (vk.dumpImage != VK_NULL_HANDLE) {
        vkDestroyImageView(vk.device, vk.dumpView, NULL);
        vkDestroyImage(vk.device, vk.dumpImage, NULL);
        vkFreeMemory(vk.device, vk.dumpMemory, NULL);
        destroyHostBuffer(&vk.dumpBuffer);
        vk.dumpImage = VK_NULL_HANDLE;
    }
}

static void ensureDumpTarget(int w, int h) {
    VkFormat format = vk.presentFormat != VK_FORMAT_UNDEFINED ? vk.presentFormat : VK_FORMAT_R8G8B8A8_UNORM;
    if (vk.dumpImage != VK_NULL_HANDLE && vk.dumpW == w && vk.dumpH == h && vk.dumpFormat == format) {
        return;
    }
    if (vk.dumpImage != VK_NULL_HANDLE) {
        presentDrain();
        vkDeviceWaitIdle(vk.device);
        for (int i = 0; i < FRAMES; i++) {
            vk.frames[i].submitted = 0;
        }
        destroyDumpTarget();
    }
    if (vk.dumpPipeline != VK_NULL_HANDLE && vk.dumpFormat != format) {
        vkDestroyPipeline(vk.device, vk.dumpPipeline, NULL);
        vk.dumpPipeline = VK_NULL_HANDLE;
    }
    vk.dumpFormat = format;
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent.width = (uint32_t)w;
    ii.extent.height = (uint32_t)h;
    ii.extent.depth = 1;
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(vk.device, &ii, NULL, &vk.dumpImage));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk.device, vk.dumpImage, &req);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    VK_CHECK(vkAllocateMemory(vk.device, &ai, NULL, &vk.dumpMemory));
    VK_CHECK(vkBindImageMemory(vk.device, vk.dumpImage, vk.dumpMemory, 0));
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = vk.dumpImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = colorRange;
    VK_CHECK(vkCreateImageView(vk.device, &vi, NULL, &vk.dumpView));
    createHostBuffer(&vk.dumpBuffer, (VkDeviceSize)w * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1);
    if (vk.dumpPipeline == VK_NULL_HANDLE) {
        vk.dumpPipeline =
            createPipeline(presentVertSpv, sizeof(presentVertSpv), presentFragSpv, sizeof(presentFragSpv), 0, format);
    }
    vk.dumpW = w;
    vk.dumpH = h;
}

/* The present pipelines' sets: the frame context's own and its movie set. */
static void bindPresentSets(VkCommandBuffer cmd, FrameCtx* f) {
    VkDescriptorSet sets[2] = {f->set, vk.pictureOverride != VK_NULL_HANDLE ? vk.pictureOverride : f->movieSet};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipelineLayout, 0, 2, sets, 0, NULL);
}

/* The present pass into a w x h target, the settings menu over it. With a
 * shader preset (chain) its last pass draws the picture instead, and the
 * present pass only the note, over black, inside the note's rectangle. */
static void recordPresentPass(VkCommandBuffer cmd, VkImageView view, VkPipeline pipeline, VkFormat format, int w,
                              int h, const PresentConstants* pc, int chain) {
    VkRenderingAttachmentInfo att = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    att.imageView = view;
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.extent.width = (uint32_t)w;
    ri.renderArea.extent.height = (uint32_t)h;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    vkCmdBeginRendering(cmd, &ri);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    bindPresentSets(cmd, frame());
    VkViewport viewport = {0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {(uint32_t)w, (uint32_t)h}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    if (chain) {
        shaderChainFinal(cmd, format, pc->dest, pc->clip);
        if (pc->osd[2] > 0) {
            PresentConstants note = *pc;
            note.enabled = 0;
            VkRect2D box = {{pc->osd[0], pc->osd[1]}, {(uint32_t)pc->osd[2], (uint32_t)pc->osd[3]}};
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            bindPresentSets(cmd, frame());
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &box);
            vkCmdPushConstants(cmd, vk.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(note), &note);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
        }
    } else {
        vkCmdPushConstants(cmd, vk.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(*pc), pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    if (uiRecord != NULL) {
        uiRecord(cmd, w, h, (int)format, pc->rgb24 && pc->enabled ? pc->dest : NULL);
    }
    if (extrasRecord != NULL) {
        extrasRecord(cmd, w, h, (int)format);
    }
    vkCmdEndRendering(cmd);
}

/* ---- pre-rotation (xg-mrj.3.9) -------------------------------------------------------------
 * A phone shows its landscape picture on a display whose own orientation is
 * portrait, and its surface says so (currentTransform ROTATE_90 or
 * ROTATE_270). The swapchain then takes images in the display's orientation,
 * marked as already turned (preTransform = currentTransform), and the present
 * pass draws as everywhere else, into an image of the window's size (the
 * player's view), which one more draw turns into the swapchain image
 * (shaders/rotate.frag): the compositor shows it as it is, without turning it
 * itself (what Android calls pre-rotation). XG_VK_ROTATE=90|180|270 turns the
 * picture so wherever the surface allows the turned size (a test: the window
 * shows it turned; XG_DUMP_PRESENT's dumps are turned as well, headless too). */

static int quarterOf(VkSurfaceTransformFlagBitsKHR t) {
    switch (t) {
    case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR: return 1;
    case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR: return 2;
    case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR: return 3;
    default: return 0;
    }
}

static void destroyRotationTarget(void) {
    if (vk.rotImage != VK_NULL_HANDLE) {
        vkDestroyImageView(vk.device, vk.rotView, NULL);
        vkDestroyImage(vk.device, vk.rotImage, NULL);
        vkFreeMemory(vk.device, vk.rotMemory, NULL);
        vk.rotImage = VK_NULL_HANDLE;
        vk.rotView = VK_NULL_HANDLE;
        vk.rotMemory = VK_NULL_HANDLE;
    }
    vk.rotW = vk.rotH = 0;
}

static void destroyRotation(void) {
    destroyRotationTarget();
    if (vk.rotPipeline != VK_NULL_HANDLE) vkDestroyPipeline(vk.device, vk.rotPipeline, NULL);
    if (vk.rotLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(vk.device, vk.rotLayout, NULL);
    if (vk.rotSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(vk.device, vk.rotSetLayout, NULL);
    if (vk.rotPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(vk.device, vk.rotPool, NULL);
    if (vk.rotSampler != VK_NULL_HANDLE) vkDestroySampler(vk.device, vk.rotSampler, NULL);
    vk.rotPipeline = VK_NULL_HANDLE;
    vk.rotLayout = VK_NULL_HANDLE;
    vk.rotSetLayout = VK_NULL_HANDLE;
    vk.rotPool = VK_NULL_HANDLE;
    vk.rotSampler = VK_NULL_HANDLE;
}

/* The w x h image the present pass draws into (the player's view), and the draw that turns it. */
static void ensureRotation(int w, int h, VkFormat format) {
    if (vk.rotSetLayout == VK_NULL_HANDLE) {
        VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = VK_FILTER_NEAREST;
        si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(vk.device, &si, NULL, &vk.rotSampler));
        VkDescriptorSetLayoutBinding b = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                                          &vk.rotSampler};
        VkDescriptorSetLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = 1;
        li.pBindings = &b;
        VK_CHECK(vkCreateDescriptorSetLayout(vk.device, &li, NULL, &vk.rotSetLayout));
        VkPushConstantRange pr = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int32_t)};
        VkPipelineLayoutCreateInfo pli = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &vk.rotSetLayout;
        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges = &pr;
        VK_CHECK(vkCreatePipelineLayout(vk.device, &pli, NULL, &vk.rotLayout));
        VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo dpi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dpi.maxSets = 1;
        dpi.poolSizeCount = 1;
        dpi.pPoolSizes = &ps;
        VK_CHECK(vkCreateDescriptorPool(vk.device, &dpi, NULL, &vk.rotPool));
        VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = vk.rotPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &vk.rotSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(vk.device, &ai, &vk.rotSet));
    }
    if (vk.rotPipeline == VK_NULL_HANDLE || vk.rotPipelineFormat != format) {
        if (vk.rotPipeline != VK_NULL_HANDLE) vkDestroyPipeline(vk.device, vk.rotPipeline, NULL);
        /* createPipeline builds with vk.pipelineLayout: this draw's own for the call. */
        VkPipelineLayout keep = vk.pipelineLayout;
        vk.pipelineLayout = vk.rotLayout;
        vk.rotPipeline = createPipeline(presentVertSpv, sizeof(presentVertSpv), rotateFragSpv, sizeof(rotateFragSpv), 0,
                                        format);
        vk.pipelineLayout = keep;
        vk.rotPipelineFormat = format;
    }
    if (vk.rotImage != VK_NULL_HANDLE && vk.rotW == w && vk.rotH == h && vk.rotFormat == format) return;
    if (vk.rotImage != VK_NULL_HANDLE) {
        presentDrain();
        vkDeviceWaitIdle(vk.device);
        for (int i = 0; i < FRAMES; i++) {
            vk.frames[i].submitted = 0;
        }
        destroyRotationTarget();
    }
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent.width = (uint32_t)w;
    ii.extent.height = (uint32_t)h;
    ii.extent.depth = 1;
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(vk.device, &ii, NULL, &vk.rotImage));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk.device, vk.rotImage, &req);
    VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    VK_CHECK(vkAllocateMemory(vk.device, &mai, NULL, &vk.rotMemory));
    VK_CHECK(vkBindImageMemory(vk.device, vk.rotImage, vk.rotMemory, 0));
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = vk.rotImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = colorRange;
    VK_CHECK(vkCreateImageView(vk.device, &vi, NULL, &vk.rotView));
    VkDescriptorImageInfo di = {vk.rotSampler, vk.rotView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr.dstSet = vk.rotSet;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &di;
    vkUpdateDescriptorSets(vk.device, 1, &wr, 0, NULL);
    vk.rotW = w;
    vk.rotH = h;
    vk.rotFormat = format;
    gpuLog("vulkan: pre-rotation: %dx%d turned %d degrees", w, h, vk.rotate * 90);
}

/* The rotation image, ready for the present pass (its last picture already read). */
static void rotationTargetBarrier(VkCommandBuffer cmd) {
    imageBarrier(cmd, vk.rotImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

/* The rotation image, drawn, turned into `target` (tw x th, ready for colour writes). */
static void recordRotation(VkCommandBuffer cmd, VkImageView target, int tw, int th) {
    imageBarrier(cmd, vk.rotImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    VkRenderingAttachmentInfo att = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    att.imageView = target;
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; /* every pixel is written */
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.extent.width = (uint32_t)tw;
    ri.renderArea.extent.height = (uint32_t)th;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    vkCmdBeginRendering(cmd, &ri);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.rotPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.rotLayout, 0, 1, &vk.rotSet, 0, NULL);
    VkViewport viewport = {0.0f, 0.0f, (float)tw, (float)th, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {(uint32_t)tw, (uint32_t)th}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    int32_t quarter = vk.rotate;
    vkCmdPushConstants(cmd, vk.rotLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(quarter), &quarter);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
}

/* XG_DUMP_PRESENT: the same pass into the dump image, copied out for the host; w x h the
 * player's view (the dump image is that turned, with pre-rotation). */
static void recordDumpPass(VkCommandBuffer cmd, const PresentConstants* pc, int chain, int w, int h) {
    imageBarrier(cmd, vk.dumpImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    int q = gtStart(cmd, GT_PRESENT); /* timed too: headless runs have only this one */
    if (vk.rotate) {
        ensureRotation(w, h, vk.dumpFormat);
        rotationTargetBarrier(cmd);
        recordPresentPass(cmd, vk.rotView, vk.dumpPipeline, vk.dumpFormat, w, h, pc, chain);
        recordRotation(cmd, vk.dumpView, vk.dumpW, vk.dumpH);
    } else {
        recordPresentPass(cmd, vk.dumpView, vk.dumpPipeline, vk.dumpFormat, vk.dumpW, vk.dumpH, pc, chain);
    }
    gtStop(cmd, q);
    imageBarrier(cmd, vk.dumpImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy c = {0};
    c.imageSubresource = colorLayers;
    c.imageExtent.width = (uint32_t)vk.dumpW;
    c.imageExtent.height = (uint32_t)vk.dumpH;
    c.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, vk.dumpImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.dumpBuffer.buffer, 1, &c);
    hostReadBarrier(cmd);
}

/* The dump image as RGBA (a BGRA swapchain's format swapped back). */
static const uint8_t* dumpPixels(void) {
    uint8_t* p = vk.dumpBuffer.map;
    if (vk.dumpFormat == VK_FORMAT_B8G8R8A8_UNORM) {
        for (size_t i = 0; i < (size_t)vk.dumpW * vk.dumpH; i++) {
            uint8_t b = p[i * 4];
            p[i * 4] = p[i * 4 + 2];
            p[i * 4 + 2] = b;
        }
    }
    return p;
}


/* The picture prepass, for the filters other than nearest
 * (shaders/filters.glsl): a pass of its own before the present pass,
 * present.frag built with PICTURE_PREPASS and drawn over the picture's size
 * into a stand-in target, writes into the frame's span buffer
 *   - xbr's codes for each picture pixel's corners, when that filter scales
 *     up: found once per picture pixel instead of once per window pixel;
 *   - on an integrated GPU, each picture pixel once as 8:8:8: the filters
 *     read it many times, and read it faster there (the span buffer is host
 *     memory, the device's own only on an integrated GPU).
 * The present pass reads them there. Without room for them, or for a picture
 * larger than the stand-in, it finds and reads everything itself. */
#define PICTURE_TARGET_W WIDE_FRAME_W
#define PICTURE_TARGET_H VRAM_H

static struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkPipeline pipeline;
    int integrated; /* 1: the device is an integrated GPU, -1 not, 0 not asked yet */
} prepass;

static void recordPicturePrepass(VkCommandBuffer cmd, FrameCtx* f, PresentConstants* pc) {
    pc->picture = pc->codes = -1;
    if (pc->filterMode == PRESENT_FILTER_NEAREST || !pc->enabled || pc->disp[2] > PICTURE_TARGET_W ||
        pc->disp[3] > PICTURE_TARGET_H) {
        return;
    }
    if (prepass.integrated == 0) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(vk.physical, &props);
        prepass.integrated = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1 : -1;
    }
    int codes = pc->filterMode == PRESENT_FILTER_XBR && pc->dest[2] > pc->disp[2] && pc->dest[3] > pc->disp[3];
    int pixels = prepass.integrated > 0;
    size_t plane = ((size_t)pc->disp[2] * (size_t)pc->disp[3] * 4 + 15) & ~(size_t)15;
    if ((!codes && !pixels) || f->spanUsed + plane * (size_t)(codes + pixels) > SPAN_BYTES) {
        return;
    }
    if (prepass.pipeline == VK_NULL_HANDLE) {
        createImage16(&prepass.image, &prepass.memory, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, PICTURE_TARGET_W,
                      PICTURE_TARGET_H);
        VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = prepass.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R16_UINT;
        vi.subresourceRange = colorRange;
        VK_CHECK(vkCreateImageView(vk.device, &vi, NULL, &prepass.view));
        prepass.pipeline = createPipeline(presentVertSpv, sizeof(presentVertSpv), presentPictureFragSpv,
                                          sizeof(presentPictureFragSpv), 0, VK_FORMAT_R16_UINT);
    }
    if (pixels) {
        pc->picture = (int32_t)(f->spanUsed / 4);
        f->spanUsed += plane;
    }
    if (codes) {
        pc->codes = (int32_t)(f->spanUsed / 4);
        f->spanUsed += plane;
    }
    int q = gtStart(cmd, GT_PRESENT);
    imageBarrier(cmd, prepass.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo att = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    att.imageView = prepass.view;
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.extent.width = (uint32_t)pc->disp[2];
    ri.renderArea.extent.height = (uint32_t)pc->disp[3];
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    vkCmdBeginRendering(cmd, &ri);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prepass.pipeline);
    bindPresentSets(cmd, f);
    VkViewport viewport = {0.0f, 0.0f, (float)pc->disp[2], (float)pc->disp[3], 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {(uint32_t)pc->disp[2], (uint32_t)pc->disp[3]}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdPushConstants(cmd, vk.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(*pc), pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
    fullBarrier(cmd); /* its words, for the present pass */
    gtStop(cmd, q);
}

static void destroyPicturePrepass(void) {
    if (prepass.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk.device, prepass.pipeline, NULL);
        vkDestroyImageView(vk.device, prepass.view, NULL);
        vkDestroyImage(vk.device, prepass.image, NULL);
        vkFreeMemory(vk.device, prepass.memory, NULL);
    }
    memset(&prepass, 0, sizeof(prepass));
}

/* The shader chain's input: the picture as present.frag shows it, 1:1 at its
 * own size (the hi-res copy's, the wide picture's), no filter, no note. */
static int recordChainPicture(VkCommandBuffer cmd, const PresentConstants* pc) {
    int pw = pc->disp[2], ph = pc->disp[3];
    VkImageView view = shaderChainBegin(cmd, pw, ph, pc->dest[2], pc->dest[3]);
    if (view == VK_NULL_HANDLE) {
        return 0;
    }
    if (vk.chainPicturePipeline == VK_NULL_HANDLE) {
        vk.chainPicturePipeline = createPipeline(presentVertSpv, sizeof(presentVertSpv), presentFragSpv,
                                                 sizeof(presentFragSpv), 0, VK_FORMAT_R8G8B8A8_UNORM);
    }
    PresentConstants q = *pc;
    q.dest[0] = q.dest[1] = q.clip[0] = q.clip[1] = 0;
    q.dest[2] = q.clip[2] = pw;
    q.dest[3] = q.clip[3] = ph;
    q.osd[2] = 0;
    q.filterMode = PRESENT_FILTER_NEAREST;
    q.fill = 0;
    q.picture = q.codes = -1;
    int timing = gtStart(cmd, GT_PRESENT); /* XG_GPU_TIMING: the chain's passes count as the present's */
    VkRenderingAttachmentInfo att = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    att.imageView = view;
    att.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.extent.width = (uint32_t)pw;
    ri.renderArea.extent.height = (uint32_t)ph;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    vkCmdBeginRendering(cmd, &ri);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.chainPicturePipeline);
    bindPresentSets(cmd, frame());
    VkViewport viewport = {0.0f, 0.0f, (float)pw, (float)ph, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {(uint32_t)pw, (uint32_t)ph}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdPushConstants(cmd, vk.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(q), &q);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
    shaderChainRun(cmd, vk.cur);
    gtStop(cmd, timing);
    return 1;
}

/* A movie pack's picture (moviepack.c) for a 24-bit display: uploaded into
 * this context's image when it is another one, and the present constants
 * switched to it (src 4: disp is then in the picture's pixels, scaled as the
 * movie frame is, its origin where the frame sits in the display area; the
 * rest of the area shows black). Nearest becomes linear: the picture is not
 * the game's pixels. 1 when it shows. */
static int moviePicture(VkCommandBuffer cmd, FrameCtx* f, const GpuDisplay* d, PresentConstants* pc) {
    MoviePicture m;
    if (!pc->rgb24 || pc->src != 0 || !pc->enabled || !moviePackPicture(d->x, d->y, d->w, d->h, &m)) {
        return 0;
    }
    if (f->movieW != m.w || f->movieH != m.h) {
        /* this context's earlier work is done (its fence): the image is free */
        destroyImage(&f->movieImage, &f->movieMemory, &f->movieView);
        f->movieW = f->movieH = 0;
        f->movieId = 0;
        int made = createImage32(&f->movieImage, &f->movieMemory, &f->movieView, m.w, m.h);
        writeMovieSet(f);
        if (!made) {
            return 0;
        }
        toGeneral(cmd, f->movieImage);
        f->movieW = m.w;
        f->movieH = m.h;
    }
    if (f->movieId != m.id) {
        size_t bytes = (size_t)m.w * m.h * 4;
        if (f->movieStagingBytes < bytes) {
            destroyHostBuffer(&f->movieStaging);
            createHostBuffer(&f->movieStaging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0);
            f->movieStagingBytes = bytes;
        }
        f->movieId = 0;
        if (!moviePackCopy(m.id, f->movieStaging.map, (size_t)m.w * 4)) {
            return 0;
        }
        VkBufferImageCopy c = {0};
        c.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = (VkExtent3D){(uint32_t)m.w, (uint32_t)m.h, 1};
        vkCmdCopyBufferToImage(cmd, f->movieStaging.buffer, f->movieImage, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        imageBarrier(cmd, f->movieImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                     VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        f->movieId = m.id;
    }
    int pw = (int)((int64_t)pc->disp[2] * m.w / m.rect[2]), ph = (int)((int64_t)pc->disp[3] * m.h / m.rect[3]);
    pc->disp[0] = (int)((int64_t)m.rect[0] * m.w / m.rect[2]);
    pc->disp[1] = (int)((int64_t)m.rect[1] * m.h / m.rect[3]);
    pc->disp[2] = pw > 0 ? pw : 1;
    pc->disp[3] = ph > 0 ? ph : 1;
    pc->src = 4;
    if (pc->filterMode == PRESENT_FILTER_NEAREST) {
        pc->filterMode = PRESENT_FILTER_LINEAR;
    }
    return 1;
}

/* A vblank's picture, or (menu) a settings menu frame: the same picture
 * again with the menu over it, dumped only as the named shot. */
/* kind: 0 ordinary native/menu, 1 host deadline, 2 native dump only. */
static void presentPicture(const GpuDisplay* d, uint32_t vblank, int menu, const char* shot, double stamp, int kind, int smooth) {
    presentCollect();
    const WideView* wideView=wideCurrentView();
    if (wideView && wideView->wide) hiWideRows(wideView->y+wideView->h);
    if(asyncPresent.lost) { presentDrain();asyncPresent.lost=0;loseSurface("vkQueuePresentKHR"); }
    endBatch();
    int dump = menu ? shot != NULL : dumpPresentWanted(vblank) && (kind != 1 || hostPresent.dump);
    if (vk.surface == VK_NULL_HANDLE && !vk.surfaceLost && !dump) {
        submitFrame(NULL, NULL);
        return;
    }
    if(vk.swapchain && bufferedPresentation()!=vk.bufferedMode)vk.swapchainStale=1;
    if(vk.copyQueue && hostPresent.realtime && speedupEnabled()) {
        checkWindowSize();
        if(vk.swapchainStale || (vk.swapchain==VK_NULL_HANDLE && SDL_GetTicksNS()>=vk.swapchainRetryNs))recreateSwapchain();
    }
    if(vk.testLeakAt != 0 && vblank >= vk.testLeakAt && presentCopyWanted()) {
        /* XG_PRESENT_TEST on the copy path: the worker's acquires time out. */
        presentDrain();
        vk.testLeakAt = vk.testLeakEvery != 0 ? vblank + vk.testLeakEvery : 0;
        testLeakImages(vblank);
    }
    PresentCopy* copy=presentCopySlot();
    if(presentCopyWanted() && !copy && !dump) {
        submitFrame(NULL,NULL);if(!kind)watchPresent(0,vblank);return;
    }
    if(copy) {
        int ready=presentCopyImage(copy,(int)vk.swapchainExtent.width,(int)vk.swapchainExtent.height);
        if(ready<0) { submitFrame(NULL,NULL);if(!kind)watchPresent(0,vblank);return; }
        if(!ready)copy=NULL;
    }
    int early=0;uint32_t earlyImage=0;
    if(kind==1 && !copy && !dump && vk.surface && !shaderChainUse(d->rgb24)) {
        /* Do not replay a high-resolution picture that has nowhere to go.
         * An empty recording context has room for every accepted snapshot;
         * replay's capacity guard preserves this acquire semaphore. */
        if(frame()->recording)submitFrame(NULL,NULL);
        FrameCtx* target=frame();recording();
        if(!acquireImage(copy?copy->acquired:target->acquired,&earlyImage,vblank)) { submitFrame(NULL,NULL);return; }
        early=1;vk.presentAcquired=1;
    }
    /* Prepare before acquiring: snapshot replay may roll command buffers
     * to make room, and must never move an acquired semaphore to another
     * frame context. Source selection is independent of the window size. */
    vk.pictureOverride = VK_NULL_HANDLE;
    PresentConstants interpolatedPc;
    presentConstants(d, 960, 720, &interpolatedPc, NULL, 0);
    int interpolated = !menu && (smooth || dump) && interpolationRender(d, stamp, &interpolatedPc, kind != 0);
    vk.presentAcquired=0;
    if (menu) interpolationReport("native", "menu", (double)vblank, -1);
    /* The compositor already holds this exact built-in picture. No image
     * write, layout/note change, interpolation or overlay needs a new one.
     * Animated shader presets and menus retain every presentation call. */
    int cacheable = !menu && !kind && !interpolated && !d->rgb24 && !shaderChainUse(d->rgb24) &&
        !(uiIsOpen && uiIsOpen()) && !(extrasIsOpen && extrasIsOpen());
    PresentConstants heldPc; uint32_t heldBits[OSD_WORDS]; int heldWords=0, heldW=0, heldH=0;
    if (cacheable && vk.window) {
        SDL_GetWindowSizeInPixels(vk.window,&heldW,&heldH);
        heldWords=presentConstants(d,heldW,heldH,&heldPc,heldBits,OSD_WORDS);
        int same = vk.held.valid && vk.held.revision==hostPresent.revision && vk.held.w==heldW && vk.held.h==heldH &&
            vk.held.swapchain==vk.swapchain && !vk.surfaceLost && !vk.swapchainStale &&
            (vk.lastPresent==VK_SUCCESS || (asyncPresent.thread && asyncPresent.queued>asyncPresent.collected)) &&
            vk.held.scale==vk.hiN && vk.held.color==vk.hiColor &&
            heldW==(int)vk.swapchainExtent.width && heldH==(int)vk.swapchainExtent.height &&
            vk.held.words==heldWords && !memcmp(&vk.held.pc,&heldPc,sizeof(heldPc)) &&
            (!heldWords || !memcmp(vk.held.bits,heldBits,(size_t)heldWords*4));
        if (same && !dump) {
            submitFrame(NULL,NULL); watchPresent(1,vblank); return;
        }
    }
    vk.held.valid=0;
    VkCommandBuffer cmd = recording();
    FrameCtx* f = frame();
    uint32_t index = early?earlyImage:0;
    int shown = kind==2?0:copy?1:early?1:acquireImage(f->acquired,&index,vblank);
    int w, h;
    if (shown) {
        w = (int)vk.swapchainExtent.width;
        h = (int)vk.swapchainExtent.height;
        if (vk.rotate & 1) { /* the player's view: the swapchain's turned back */
            int t = w;
            w = h;
            h = t;
        }
    } else if (dump) {
        /* No image to show: the dump still gets the window's picture. */
        w = 960, h = 720;
        if (vk.window != NULL) {
            SDL_GetWindowSizeInPixels(vk.window, &w, &h);
        }
        if (w <= 0 || h <= 0) {
            dump = 0;
        }
    }
    if (!shown && !dump) {
        submitFrame(NULL, NULL);
        if (!kind) watchPresent(0, vblank);
        return;
    }

    /* The note's bitmap rides in this frame's span buffer, after the spans. */
    PresentConstants pc;
    uint32_t bits[OSD_WORDS];
    int words = presentConstants(d, w, h, &pc, bits, OSD_WORDS);
    if (interpolated) {
        memcpy(pc.disp, interpolatedPc.disp, sizeof(pc.disp));
        pc.src = interpolatedPc.src; pc.rgb24 = 0;
        /* Keep the selected presentation filter at exact endpoints and
         * between them. Nearest must not average authored text/HUD edges. */
    }
    if (!interpolated && vk.hiN != 0 && pc.rgb24 == 0 && (pc.src == 0 || pc.disp[0] + pc.disp[2] <= vk.hiWideW)) {
        /* The hi-res copy shows instead (hires.c; movies stay 1x). */
        pc.src += 2;
        for (int i = 0; i < 4; i++) {
            pc.disp[i] *= vk.hiN;
        }
        /* Honor Nearest when reducing this copy too. Sharp is available
         * explicitly when coverage averaging is desired. */
    }
    if (!interpolated) moviePicture(cmd, f, d, &pc);
    if (words > 0 && f->spanUsed + (size_t)words * 4 <= SPAN_BYTES) {
        memcpy(f->spans.map + f->spanUsed, bits, (size_t)words * 4);
        pc.osdBase = (int32_t)(f->spanUsed / 4);
        f->spanUsed += ((size_t)words * 4 + 15) & ~(size_t)15;
    } else {
        pc.osd[2] = 0;
    }

    barrierIfNeeded(cmd);
    int chain = shaderChainUse(pc.rgb24) && recordChainPicture(cmd, &pc);
    if (!chain) {
        recordPicturePrepass(cmd, f, &pc);
    }
    if (dump) {
        ensureDumpTarget(vk.rotate & 1 ? h : w, vk.rotate & 1 ? w : h);
        recordDumpPass(cmd, &pc, chain, w, h);
    }
    if (shown) {
        VkImage output=copy?copy->image:vk.swapchainImages[index];
        imageBarrier(cmd, output, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        int q = gtStart(cmd, GT_PRESENT);
        if (vk.rotate && !copy) {
            /* pre-rotation: the pass into the player's view, turned into the image */
            ensureRotation(w, h, vk.swapchainFormat);
            rotationTargetBarrier(cmd);
            recordPresentPass(cmd, vk.rotView, vk.presentPipeline, vk.swapchainFormat, w, h, &pc, chain);
            recordRotation(cmd, vk.swapchainViews[index], (int)vk.swapchainExtent.width,
                           (int)vk.swapchainExtent.height);
        } else {
            recordPresentPass(cmd, copy?copy->view:vk.swapchainViews[index], vk.presentPipeline, vk.swapchainFormat, w, h, &pc, chain);
        }
        gtStop(cmd, q);
        imageBarrier(cmd, output, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     copy?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
    }
    vk.needBarrier = 1; /* later draws must not overtake the reads above */
    if (chain) {
        shaderChainEnd();
    }
    int queued=0;
    if (!shown) {
        submitFrame(NULL, NULL);
    } else {
        if(copy) { submitFrame(NULL,&copy->rendered);copy->submitted=1; }
        else submitFrame(&f->acquired, &vk.presented[index]);
        VkPresentInfoKHR pi = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &vk.presented[index];
        pi.swapchainCount = 1;
        pi.pSwapchains = &vk.swapchain;
        pi.pImageIndices = &index;
        uint64_t timing=timingNote(stamp,kind);
        queued=presentEnqueue(timing,copy);
        if(copy && queued)copy->ticket=asyncPresent.queued;
        VkResult r=queued?VK_SUCCESS:copy?presentCopyWorker(copy,vk.swapchain,&index,timing):timingPresent(vk.queue,&pi,timing);
        if(!queued)presentResult(r,index);
        if(r==VK_SUCCESS || r==VK_SUBOPTIMAL_KHR) {
            if (cacheable && w==heldW && h==heldH) {
                vk.held.valid=1; vk.held.scale=vk.hiN; vk.held.color=vk.hiColor; vk.held.w=w; vk.held.h=h; vk.held.words=heldWords;
                vk.held.revision=hostPresent.revision; vk.held.swapchain=vk.swapchain; vk.held.pc=heldPc;
                if(heldWords) memcpy(vk.held.bits,heldBits,(size_t)heldWords*4);
            }
        } else shown=0;
    }
    /* A queued copy's outcome arrives a picture later: the stall guard
     * counts the worker's skipped acquires too. */
    if (!kind) watchPresent(shown && !(queued && vk.copySkipped), vblank);
    vk.pictureOverride = VK_NULL_HANDLE;
    if (dump) {
        waitFrame(f);
        if (menu || kind == 1) {
            dumpPresentNamed(shot, vk.dumpW, vk.dumpH, dumpPixels());
        } else {
            dumpPresent(vblank, vk.dumpW, vk.dumpH, dumpPixels());
        }
    }
}

void gvkPresent(const GpuDisplay* d, uint32_t vblank) {
    cpuResolveFlush();
    if (vk.device == VK_NULL_HANDLE) return;
    vramFlushCpuWrites((VRect){0,0,VRAM_W-1,VRAM_H-1});
    endBatch();
    hostPresent.nativeFrames++;
    /* Battle speed: the game frames between two shown ones are drawn (into
     * VRAM, as every frame) and submitted, but no swapchain image is
     * acquired or presented for them, so the game thread never waits on the
     * display for them; native dumps keep their vblanks (kind 2). The
     * corner note ages with the shown pictures (it stays up as long). */
    int hidden = battleSpeedHidden != NULL && battleSpeedHidden();
    if (!hidden) presentVBlank();
    gtVBlank(vblank);
    int before, after; double alpha;
    int eligible = !hidden && hostPresent.realtime && presentationRate() && interpolationEnabled() &&
        !gpuCapturing() && !shaderChainUse(d->rgb24);
    int ready = eligible && interpolationPicture(d, (double)vblank, &before, &after, &alpha) != NULL;
    /* Field drawing and idle waits can fall in different native frames.
     * Keep the pair eligible across both, but provide native pictures when
     * game work leaves no idle slot for two consecutive native intervals. */
    if (hostPresent.active && hostPresent.polls == hostPresent.nativePolls) hostPresent.quietFrames++;
    else hostPresent.quietFrames = 0;
    int starved = hostPresent.quietFrames >= 2;
    int active = ready && !starved && hostPresent.cost <= UINT64_C(12000000);
    hostPresent.status = active ? "Host scheduling" : !presentationRate() ? "Native rate" :
        !hostPresent.realtime ? "Native rate during deterministic playback" : !interpolationEnabled() ?
        "Native rate: Smooth motion is off" : starved ? "Native rate: no idle display time" : ready ? "Native rate: display work is over budget" : !eligible ? "Native rate: shader preset or capture active" :
        "Native rate: waiting for supported field frames";
    if (hidden) {
        gpuHostInvalidate();
        if (dumpPresentWanted(vblank)) presentPicture(d, vblank, 0, NULL, (double)vblank, 2, 1);
        else { endBatch(); submitFrame(NULL, NULL); }
    } else if (active) {
        if (!hostPresent.active) {
            uint64_t now = SDL_GetTicksNS();
            /* Put the first deadline between native vblanks. At 60 Hz,
             * one full period lands just before the 59.8 Hz vblank and
             * cannot fit even a cheap display sample. */
            if (!hostPresent.prepareRate) {
                hostPresent.prepareRate = hostPresent.rate;
                hostPresent.measuredAt = now;
                hostPresent.measuredShown = vk.framesShown;
            }
            uint64_t phase=UINT64_C(500000000)/(unsigned)hostPresent.prepareRate;
            interpPacerReset(&hostPresent.pacer, now>phase?now-phase:0, hostPresent.prepareRate);
        }
        hostPresent.active = 1;
        hostPresent.display = *d; hostPresent.vblank = vblank;
        /* Native dumps retain their vblank names and integer timeline.
         * They never acquire/present an extra swapchain image. */
        if (dumpPresentWanted(vblank)) presentPicture(d, vblank, 0, NULL, (double)vblank, 2, 1);
        else { endBatch(); submitFrame(NULL, NULL); }
        watchPresent(vk.framesShown != hostPresent.nativeShown, vblank);
    } else {
        gpuHostInvalidate();
        uint64_t began = SDL_GetTicksNS();
        presentPicture(d, vblank, 0, NULL, (double)vblank, 0, 1);
        if (ready) hostPresent.cost = (hostPresent.cost + SDL_GetTicksNS() - began) / 2;
    }
    hostPresent.nativeShown = vk.framesShown;
    hostPresent.nativeSequence = hostPresent.sequence;
    hostPresent.nativePolls = hostPresent.polls;
    autoTime.frame++;
    int w = (int)vk.swapchainExtent.width, h = (int)vk.swapchainExtent.height;
    if (vk.swapchain == VK_NULL_HANDLE) {
        w = 960; h = 720;
        if (vk.window) SDL_GetWindowSizeInPixels(vk.window, &w, &h);
    }
    hiresVBlank(d, w, h);
    if (++vk.hiWideTrimClock>=60) {
        int rows=vk.hiWideRecentH;
        vk.hiWideRecentH=vk.hiWideTrimClock=0;
        /* A unified heap gains no residency space from a second copy. */
        int separate=0;
        for (uint32_t i=0;i<vk.memProps.memoryTypeCount;i++)
            if (!(vk.memProps.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) separate=1;
        /* Hysteresis: a field's captures and wave strips recur seconds
         * apart (burning Lahan, field 14: one trim per second was regrown
         * within 1-2 s, in up to seven 64-row steps of a device idle and a
         * whole-canvas copy each). Trim only rows that HI_TRIM_WINDOWS whole
         * windows in a row left unused, down to the most any of them used.
         * Trimming keeps every enhanced bit, so pictures never depend on it. */
        int h=(rows+63)&~63;
        if (h<VRAM_H) h=VRAM_H;
        if (!separate || !vk.hiN || !vk.hiWide || h+256>vk.hiWideH) vk.hiWideQuiet=vk.hiWideQuietH=vk.hiWideKept=0;
        else {
            if (rows>vk.hiWideQuietH) vk.hiWideQuietH=rows;
            int percent;
            if (++vk.hiWideQuiet==1 && hiWidePressure(&percent))
                hiSpareAhead(vk.hiWideW*vk.hiN,(vk.hiWideH-VRAM_H)*vk.hiN); /* what the trim's new cold image would be */
            if (vk.hiWideQuiet>=HI_TRIM_WINDOWS) {
                if (hiWidePressure(&percent)) {
                    rows=vk.hiWideQuietH;
                    vk.hiWideQuiet=vk.hiWideQuietH=vk.hiWideKept=0;
                    gvkHiTrimWide(rows);
                } else {
                    /* Still quiet: the next window asks the heap again. */
                    vk.hiWideQuiet=HI_TRIM_WINDOWS-1;
                    if (!vk.hiWideKept) {
                        vk.hiWideKept=1;
                        gpuLog("hires: wide rows %d..%d unused but kept resident: device heap at %d%% of its budget (trimmed from %d%%)",
                               h,vk.hiWideH-1,percent,HI_TRIM_HEAP_PERCENT);
                    }
                }
            }
        }
    }
    uint64_t measured = SDL_GetTicksNS();
    if (!hostPresent.measuredAt) { hostPresent.measuredAt = measured; hostPresent.measuredShown = vk.framesShown; }
    else if (measured - hostPresent.measuredAt >= UINT64_C(1000000000)) {
        hostPresent.achieved = (vk.framesShown - hostPresent.measuredShown) * 1e9 / (double)(measured - hostPresent.measuredAt);
        hostPresent.measuredAt = measured; hostPresent.measuredShown = vk.framesShown;
        /* A 60 Hz preparation clock offers only one chance per native
         * interval. Native work or GPU/WSI backpressure can consume it.
         * Preserve the requested delivery target while adapting preparation
         * within the already-supported 60..120 Hz range. Owned output images
         * and the native-event budget continue to bound queued work. */
        if (hostPresent.active && vk.surface && vk.swapchain && hostPresent.rate == 60) {
            int rate = hostPresent.prepareRate;
            int next = interpPrepareRate(hostPresent.rate,rate,hostPresent.achieved,&hostPresent.adaptive);
            if (next != rate) {
                hostPresent.prepareRate = next;
                uint64_t phase = UINT64_C(500000000)/(unsigned)next;
                interpPacerReset(&hostPresent.pacer,measured>phase?measured-phase:0,next);
                hostPresent.deferred = 0;
            }
        }
    }
    if (hostPresent.log) gpuLog("host-present: native vb=%u native=%llu auto=%llu active=%d target=%d shown=%u cost=%.3f quiet=%d prepare=%d",
        vblank, (unsigned long long)hostPresent.nativeFrames, (unsigned long long)autoTime.frame,
        hostPresent.active, hostPresent.rate, vk.framesShown,hostPresent.cost*1e-6,hostPresent.quietFrames,hostPresent.prepareRate);
}

void gpuHostPoll(uint64_t now, double vblank, uint64_t available) {
    if (!hostPresent.active) return;
    hostPresent.polls++;
    if (now < hostPresent.pacer.anchor) {
        gpuHostReset(); return;
    }
    if (now < gpuHostDeadline()) return;
    /* A busy rendering context is backpressure, not CPU presentation cost.
     * Retry from the idle loop without waiting on its GPU fence; otherwise
     * the wait consumes the game's next native interval and poisons the
     * presentation budget for several more frames. */
    unsigned index=vk.cur;
    if(frame()->recording)index=(index+1)%FRAMES;
    FrameCtx* next=&vk.frames[index];
    if(next->submitted && vkGetFenceStatus(vk.device,next->fence)==VK_NOT_READY) {
        hostPresent.deferred=now+UINT64_C(100000);return;
    }
    uint64_t tick = hostPresent.pacer.tick;
    uint64_t budget = hostPresent.cost + hostPresent.cost / 4 + UINT64_C(250000);
    if (budget < UINT64_C(1000000)) budget = UINT64_C(1000000);
    if (available < budget) {
        /* Give the imminent native event priority, then retry one current
         * sample. Advancing after it skips old slots without a burst. */
        hostPresent.deferred = now + available + 1000;
        return;
    }
    hostPresent.deferred = 0;
    hostPresent.sequence++;
    char shot[96];
    snprintf(shot, sizeof(shot), "host_%010llu_%016llu", (unsigned long long)hostPresent.sequence, (unsigned long long)now);
    CPU_TIME_BEGIN(CPU_PRESENT);
    presentPicture(&hostPresent.display, hostPresent.vblank, 0, shot, vblank, 1, 1);
    CPU_TIME_END(CPU_PRESENT);
    uint64_t finished = SDL_GetTicksNS();
    uint64_t cost = finished > now ? finished - now : 0;
    hostPresent.cost = cost > hostPresent.cost ? cost : (hostPresent.cost * 3 + cost) / 4;
    if (hostPresent.active) {
        interpPacerAdvance(&hostPresent.pacer, finished > now ? finished : now);
        if (hostPresent.pacer.tick > tick + 1) hostPresent.missed += hostPresent.pacer.tick - tick - 1;
    }
    if (hostPresent.log) gpuLog("host-present: host seq=%llu ns=%llu vb=%.6f target=%d shown=%u missed=%llu",
        (unsigned long long)hostPresent.sequence, (unsigned long long)now, vblank, hostPresent.rate,
        vk.framesShown, (unsigned long long)hostPresent.missed);
}

void gvkPresentMenu(const GpuDisplay* d, uint32_t vblank, const char* shot) {
    cpuResolveFlush();
    if (vk.device == VK_NULL_HANDLE) return;
    gpuHostInvalidate();
    hiresGpuTimePause();
    endBatch();
    presentVBlank();
    presentPicture(d, vblank, 1, shot, (double)vblank, 0, 0);
}

void presentationRateDescribe(char* out, size_t size) {
    presentationRate();
    SDL_DisplayID id = vk.window ? SDL_GetDisplayForWindow(vk.window) : 0;
    const SDL_DisplayMode* m = id ? SDL_GetCurrentDisplayMode(id) : NULL;
    double hz = m ? (m->refresh_rate_denominator > 0 ? (double)m->refresh_rate_numerator / m->refresh_rate_denominator : m->refresh_rate) : 0;
    const char* mode = vk.swapchain == VK_NULL_HANDLE ? "offscreen" : vk.presentMode == VK_PRESENT_MODE_FIFO_KHR ? "FIFO" : vk.presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? "Mailbox" : "Immediate";
    char monitor[64];
    if (hz > 0) snprintf(monitor, sizeof(monitor), "monitor %.2f Hz", hz);
    else snprintf(monitor, sizeof(monitor), "monitor refresh unknown");
    snprintf(out, size, "%s. %.1f recent submissions/s; %s; %s, %s.",
        hostPresent.status ? hostPresent.status : "Waiting for gameplay", hostPresent.achieved, monitor, mode,
        vk.window && (SDL_GetWindowFlags(vk.window) & SDL_WINDOW_FULLSCREEN) ? "fullscreen" : "windowed");
}

/* ---- setup -------------------------------------------------------------------------- */

static void createFrames(void) {
    VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = vk.queueFamily;
    VK_CHECK(vkCreateCommandPool(vk.device, &pi, NULL, &vk.commandPool));
    for (int i = 0; i < FRAMES; i++) {
        FrameCtx* f = &vk.frames[i];
        VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = vk.commandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(vk.device, &ai, &f->cmd));
        VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VK_CHECK(vkCreateFence(vk.device, &fi, NULL, &f->fence));
        VkSemaphoreCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(vk.device, &si, NULL, &f->acquired));
        createHostBuffer(&f->vertices, VERTEX_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0);
        createHostBuffer(&f->staging, STAGING_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0);
        createHostBuffer(&f->spans, SPAN_BYTES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0);
        createSet(f);
        VkDescriptorSetAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        mai.descriptorPool = vk.moviePool;
        mai.descriptorSetCount = 1;
        mai.pSetLayouts = &vk.movieSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(vk.device, &mai, &f->movieSet));
        writeMovieSet(f);
        mai.descriptorPool = vk.interpolationPool;
        for (int k = 0; k <= INTERP_FRAMES; k++) {
            mai.pSetLayouts = &vk.setLayout;
            VK_CHECK(vkAllocateDescriptorSets(vk.device, &mai, &f->interpolationSet[k]));
            mai.pSetLayouts = &vk.movieSetLayout;
            VK_CHECK(vkAllocateDescriptorSets(vk.device, &mai, &f->interpolationPicture[k]));
        }
        mai.pSetLayouts = &vk.setLayout;
        for (int k = INTERP_FRAMES+1; k < INTERP_SETS; k++)
            VK_CHECK(vkAllocateDescriptorSets(vk.device, &mai, &f->interpolationSet[k]));
        if (gt.on) {
            VkQueryPoolCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qi.queryCount = GT_QUERIES;
            VK_CHECK(vkCreateQueryPool(vk.device, &qi, NULL, &f->queries));
        }
    }
    createHostBuffer(&vk.readback, READBACK_BYTES, VK_BUFFER_USAGE_TRANSFER_DST_BIT, 1);
}

/* The shader chain's one-off work (its LUT uploads): this frame context's
 * commands, then handed over and waited for. */
static VkCommandBuffer chainCommands(void* user) {
    (void)user;
    endBatch();
    return recording();
}

static void chainSubmit(void* user) {
    (void)user;
    FrameCtx* f = frame();
    submitFrame(NULL, NULL);
    waitFrame(f);
}

static void chainWaitIdle(void* user) {
    (void)user;
    /* Keep the caller's recording context alive. Only previously submitted
     * shader resources are being retired here. */
    presentDrain();VK_CHECK(vkDeviceWaitIdle(vk.device));
}

int gvkInit(void* sdlWindow) {
    memset(&vk, 0, sizeof(vk));
    vk.window = sdlWindow;
    vk.env.fullBarriers = getenv("XG_TEST_FULL_BARRIERS") != NULL;
    vk.env.presentSync = getenv("XG_TEST_PRESENT_SYNC") != NULL;
    vk.env.directPresent = getenv("XG_TEST_DIRECT_PRESENT") != NULL;
    vk.env.serialReplay = getenv("XG_TEST_SERIAL_REPLAY") != NULL;
    vk.env.interpolationOrdered = getenv("XG_INTERPOLATION_ORDERED") != NULL;
    vk.env.interpolationLog = getenv("XG_INTERPOLATION_LOG") != NULL;
    vk.env.allInterlock = getenv("XG_TEST_ALL_INTERLOCK") != NULL;
    vk.env.hiresStats = getenv("XG_HIRES_STATS") != NULL && atoi(getenv("XG_HIRES_STATS")) > 0;
    vk.env.immediateResolve = getenv("XG_TEST_IMMEDIATE_RESOLVE") != NULL;
    vk.env.interpolationSpill = envFlag("XG_TEST_INTERPOLATION_SPILL");
    int headless = envFlag("XG_HEADLESS") || sdlWindow == NULL;
    {
        const char* turn = getenv("XG_VK_ROTATE"); /* pre-rotation's test: degrees clockwise */
        vk.rotateForced = turn != NULL && turn[0] != '\0' ? (atoi(turn) / 90) & 3 : -1;
        vk.rotate = vk.rotateForced > 0 ? vk.rotateForced : 0;
    }
    if (!loadLoader()) {
        return 0;
    }
    if (!createInstance(!headless)) {
        if (headless || !createInstance(0)) {
            return 0;
        }
        headless = 1;
    }
    if (!headless && !SDL_Vulkan_CreateSurface(vk.window, vk.instance, NULL, &vk.surface)) {
        gpuLog("vulkan: no window surface (%s); rendering offscreen", SDL_GetError());
        vk.surface = VK_NULL_HANDLE;
    }
    if (!createDevice()) {
        return 0;
    }

    createVramImage(&vk.vram, &vk.vramMemory,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    createVramImage(&vk.scratch, &vk.scratchMemory,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = vk.vram;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R16_UINT;
    vi.subresourceRange = colorRange;
    VK_CHECK(vkCreateImageView(vk.device, &vi, NULL, &vk.vramView));
    vi.image = vk.scratch;
    VK_CHECK(vkCreateImageView(vk.device, &vi, NULL, &vk.scratchView));
    if (!createImage32(&vk.dummy32, &vk.dummy32Memory, &vk.dummy32View, 1, 1) ||
        !deviceBuffer(&vk.resDummy, &vk.resDummyMem, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        gpuLog("vulkan: no memory for a 1x1 image");
        return 0;
    }
    createImage16(&vk.wide, &vk.wideMemory,
                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, WIDE_W,
                  WIDE_H);
    vi.image = vk.wide;
    VK_CHECK(vkCreateImageView(vk.device, &vi, NULL, &vk.wideView));
    createLayouts();
    writeEffectSet();
    if (vk.interlock) {
        vk.drawPipeline = createPipeline(drawVertSpv, sizeof(drawVertSpv), drawInterlockFragSpv,
                                         sizeof(drawInterlockFragSpv), 1, VK_FORMAT_UNDEFINED);
        vk.wideDrawPipeline = createPipeline(drawWideVertSpv, sizeof(drawWideVertSpv), drawWideInterlockFragSpv,
                                             sizeof(drawWideInterlockFragSpv), 2, VK_FORMAT_UNDEFINED);
    } else {
        vk.drawPipeline = createPipeline(drawVertSpv, sizeof(drawVertSpv), drawFragSpv, sizeof(drawFragSpv), 1,
                                         VK_FORMAT_UNDEFINED);
        vk.wideDrawPipeline = createPipeline(drawWideVertSpv, sizeof(drawWideVertSpv), drawWideFragSpv,
                                             sizeof(drawWideFragSpv), 2, VK_FORMAT_UNDEFINED);
    }
    vk.hiDrawPipeline = createHiPipeline(hiresVertSpv, sizeof(hiresVertSpv),
                                         vk.interlock ? hiresInterlockFragSpv : hiresFragSpv,
                                         vk.interlock ? sizeof(hiresInterlockFragSpv) : sizeof(hiresFragSpv), 1);
    vk.hiWidePipeline = createHiPipeline(hiresWideVertSpv, sizeof(hiresWideVertSpv),
                                         vk.interlock ? hiresWideInterlockFragSpv : hiresWideFragSpv,
                                         vk.interlock ? sizeof(hiresWideInterlockFragSpv) : sizeof(hiresWideFragSpv), 2);
    /* Replay submits individual triangles or proved disjoint batches. Their
     * destinations cannot race, so per-pixel fragment locks add no ordering. */
    vk.interpolationWidePipeline=createHiPipeline(hiresWideVertSpv,sizeof(hiresWideVertSpv),hiresWideFragSpv,sizeof(hiresWideFragSpv),2);
    vk.hiUnorderedPipeline=createHiPipeline(hiresVertSpv,sizeof(hiresVertSpv),hiresFragSpv,sizeof(hiresFragSpv),1);
    vk.hiCoarsePipeline = createHiPipeline(hiresCoarseVertSpv, sizeof(hiresCoarseVertSpv),
                                           vk.interlock ? hiresCoarseInterlockFragSpv : hiresCoarseFragSpv,
                                           vk.interlock ? sizeof(hiresCoarseInterlockFragSpv) : sizeof(hiresCoarseFragSpv), 1);
    vk.hiCoarseUnorderedPipeline = createHiPipeline(hiresCoarseVertSpv, sizeof(hiresCoarseVertSpv), hiresCoarseFragSpv, sizeof(hiresCoarseFragSpv), 1);
    vk.hiWideCoarsePipeline = createHiPipeline(hiresWideCoarseVertSpv, sizeof(hiresWideCoarseVertSpv),
                                               vk.interlock ? hiresWideCoarseInterlockFragSpv : hiresWideCoarseFragSpv,
                                               vk.interlock ? sizeof(hiresWideCoarseInterlockFragSpv) : sizeof(hiresWideCoarseFragSpv), 2);
    vk.hiWideCoarseUnorderedPipeline = createHiPipeline(hiresWideCoarseVertSpv, sizeof(hiresWideCoarseVertSpv), hiresWideCoarseFragSpv, sizeof(hiresWideCoarseFragSpv), 2);
    vk.capturePipeline = createHiPipeline(presentVertSpv,sizeof(presentVertSpv),captureFragSpv,sizeof(captureFragSpv),0);
    vk.hiUtilPipeline =
        createHiPipeline(presentVertSpv, sizeof(presentVertSpv), hiresUtilFragSpv, sizeof(hiresUtilFragSpv), 0);
    createFrames();

    /* VRAM starts out zero, like the CPU copy. */
    VkCommandBuffer cmd = recording();
    toGeneral(cmd, vk.dummy32);
    VkImage images[3] = {vk.vram, vk.scratch, vk.wide};
    for (int i = 0; i < 3; i++) {
        imageBarrier(cmd, images[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_NONE, 0,
                     VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkClearColorValue zero = {{0}};
        vkCmdClearColorImage(cmd, images[i], VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &colorRange);
    }
    vk.needBarrier = 1;
    vk.lastAcquire = vk.lastPresent = PRESENT_NOT_TRIED;
    const char* stall = getenv("XG_PRESENT_STALL");
    vk.stallBase = stall != NULL && stall[0] != '\0' ? atoi(stall) : 120;
    vk.stallLimit = vk.stallBase;
    const char* test = getenv("XG_PRESENT_TEST");
    if (test != NULL && strncmp(test, "leak@", 5) == 0) {
        char* end;
        vk.testLeakAt = (uint32_t)strtoul(test + 5, &end, 10);
        vk.testLeakEvery = *end == '/' ? (uint32_t)strtoul(end + 1, NULL, 10) : 0;
    }
    if (vk.surface != VK_NULL_HANDLE) {
        createSwapchain();
    }
    gvkSubmit();
    gvkSetOrderedFallback(gpuOrderedFallback());
    UiVulkan dev = {(void*)vkGetInstanceProcAddr, vk.instance, vk.physical, vk.device, vk.queueFamily, vk.queue};
    if (uiVulkanReady != NULL) {
        uiVulkanReady(&dev);
    }
    if (extrasVulkanReady != NULL) {
        extrasVulkanReady(&dev);
    }
    ShaderChainDevice chain = {(void*)vkGetInstanceProcAddr, vk.instance, vk.physical, vk.device, NULL,
                               chainCommands, chainSubmit, chainWaitIdle};
    shaderChainVulkanReady(&chain);
    texturePackSetGpu(1); /* whole pack images to the GPU, kept there (resPlace) */
    return 1;
}

void gvkShutdown(void) {
    cpuResolveFlush();
    gpuHostInvalidate();
    if (vk.device == VK_NULL_HANDLE) {
        if (vk.instance != VK_NULL_HANDLE) {
            vkDestroyInstance(vk.instance, NULL);
        }
        memset(&vk, 0, sizeof(vk));
        return;
    }
    presentShutdown();
    timingClose();
    vkDeviceWaitIdle(vk.device);
    presentCopyShutdown();
    if(gvkOrderedOverflow()!=0) {
        gpuLog("vulkan: ordered fallback exceeded its proven operation bound");abort();
    }
    shaderChainVulkanShutdown();
    moviePackShutdown();
    if (vk.chainPicturePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk.device, vk.chainPicturePipeline, NULL);
    }
    if (extrasVulkanShutdown != NULL) {
        extrasVulkanShutdown(); /* first: it leaves the menu's ImGui context current for the menu's */
    }
    if (uiVulkanShutdown != NULL) {
        uiVulkanShutdown();
    }
    if (vk.surface != VK_NULL_HANDLE || vk.surfaceLost) {
        gpuLog("vulkan: %u frames presented, %u skipped (no swapchain image free)", vk.framesShown, vk.framesSkipped);
        if (vk.stallRecoveries != 0) {
            gpuLog("vulkan: %u present stalls recovered (swapchain made again)", vk.stallRecoveries);
        }
    }
    destroySwapchain();
    if (vk.rescuePool != VK_NULL_HANDLE) {
        vkDestroyFence(vk.device, vk.rescueFence, NULL);
        vkDestroyCommandPool(vk.device, vk.rescuePool, NULL);
        destroyHostBuffer(&vk.rescueBuffer);
    }
    for (int i = 0; i < FRAMES; i++) {
        FrameCtx* f = &vk.frames[i];
        if (f->queries != VK_NULL_HANDLE) {
            gtHarvest(f);
            vkDestroyQueryPool(vk.device, f->queries, NULL);
        }
        if (f->autoQueries != VK_NULL_HANDLE)
            vkDestroyQueryPool(vk.device, f->autoQueries, NULL);
        destroyHostBuffer(&f->vertices);
        for(int j=0;j<16;j++)destroyHostBuffer(&f->interpolationRuns[j]);
        destroyHostBuffer(&f->staging);
        destroyHostBuffer(&f->spans);
        destroyHostBuffer(&f->movieStaging);
        destroyHostBuffer(&f->movieHiStaging);
        destroyHostBuffer(&f->resStage);
        free(f->resCopies);
        destroyImage(&f->movieImage, &f->movieMemory, &f->movieView);
        vkDestroyFence(vk.device, f->fence, NULL);
        vkDestroySemaphore(vk.device, f->acquired, NULL);
    }
    if(vk.orderedPasses)gpuLog("vulkan: ordered fallback %llu bounded passes",(unsigned long long)vk.orderedPasses);
    orderedDestroy();
    gtSummary();
    if (interpolationEnabled()) gpuLog("interpolation: %llu pictures, %llu intermediate geometry pictures",
                                      (unsigned long long)vk.interpolatedPictures, (unsigned long long)vk.intermediatePictures);
    free(vk.interpolationVertices); free(vk.interpolationSpans); free(vk.interpolationSteps);
    for (int i = 0; i <= INTERP_FRAMES; i++)
        destroyImage(&vk.interpolationImage[i], &vk.interpolationMemory[i], &vk.interpolationView[i]);
    for (int i = 0; i < INTERP_FRAMES*INTERP_FREEZES; i++)
        destroyImage(&vk.interpolationFrozen[i], &vk.interpolationFrozenMemory[i], &vk.interpolationFrozenView[i]);
    destroyImage(&vk.interpolationCache, &vk.interpolationCacheMemory, &vk.interpolationCacheView);
    destroyImage(&vk.interpolationSource, &vk.interpolationSourceMemory, &vk.interpolationSourceView);
    vkDestroyDescriptorPool(vk.device, vk.interpolationPool, NULL);
    memset(&gt, 0, sizeof(gt));
    memset(&autoTime, 0, sizeof(autoTime));
    hiresSummary();
    destroyHostBuffer(&vk.readback);
    destroyHostBuffer(&late.bufs[0]);
    destroyHostBuffer(&late.bufs[1]);
    memset(&late, 0, sizeof(late));
    destroyDumpTarget();
    destroyRotation();
    destroyPicturePrepass();
    if (vk.dumpPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk.device, vk.dumpPipeline, NULL);
    }
    vkDestroyCommandPool(vk.device, vk.commandPool, NULL);
    vkDestroyPipeline(vk.device, vk.drawPipeline, NULL);
    if (vk.presentPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vk.device, vk.presentPipeline, NULL);
    }
    vkDestroyDescriptorPool(vk.device, vk.descriptorPool, NULL);
    vkDestroyDescriptorPool(vk.device, vk.moviePool, NULL);
    if (vk.resUploads != 0) {
        gpuLog("vulkan: %llu texture pack images kept on the GPU (%.1f MB uploaded)", (unsigned long long)vk.resUploads,
               (double)vk.resUploadBytes / (1024.0 * 1024.0));
    }
    if (vk.resBuf != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk.device, vk.resBuf, NULL);
        vkFreeMemory(vk.device, vk.resMem, NULL);
    }
    vkDestroyBuffer(vk.device, vk.resDummy, NULL);
    vkFreeMemory(vk.device, vk.resDummyMem, NULL);
    freeTransferImage(&vk.movieHiSrc, &vk.movieHiSrcMemory);
    freeTransferImage(&vk.movieHiDst, &vk.movieHiDstMemory);
    vkDestroyPipelineLayout(vk.device,vk.orderedResolveLayout,NULL);
    vkDestroyDescriptorSetLayout(vk.device,vk.orderedLayout,NULL);
    vkDestroyPipelineLayout(vk.device, vk.pipelineLayout, NULL);
    vkDestroyDescriptorSetLayout(vk.device, vk.setLayout, NULL);
    vkDestroyDescriptorSetLayout(vk.device, vk.movieSetLayout, NULL);
    vkDestroyImageView(vk.device, vk.vramView, NULL);
    vkDestroyImage(vk.device, vk.vram, NULL);
    vkFreeMemory(vk.device, vk.vramMemory, NULL);
    vkDestroyImage(vk.device, vk.scratch, NULL);
    vkFreeMemory(vk.device, vk.scratchMemory, NULL);
    vkDestroyPipeline(vk.device, vk.wideDrawPipeline, NULL);
    vkDestroyPipeline(vk.device, vk.hiDrawPipeline, NULL);
    vkDestroyPipeline(vk.device, vk.hiWidePipeline, NULL);
    vkDestroyPipeline(vk.device, vk.interpolationWidePipeline, NULL);
    vkDestroyPipeline(vk.device, vk.hiUnorderedPipeline, NULL);
    vkDestroyPipeline(vk.device, vk.hiCoarsePipeline, NULL);
    vkDestroyPipeline(vk.device, vk.hiCoarseUnorderedPipeline, NULL);
    vkDestroyPipeline(vk.device, vk.hiWideCoarsePipeline, NULL);
    vkDestroyPipeline(vk.device, vk.hiWideCoarseUnorderedPipeline, NULL);
    vkDestroyPipeline(vk.device, vk.hiUtilPipeline, NULL);
    vkDestroyPipeline(vk.device, vk.capturePipeline, NULL);
    vkDestroyPipelineLayout(vk.device, vk.hiLayout, NULL);
    destroyImage(&vk.hiVram, &vk.hiVramMemory, &vk.hiVramView);
    destroyImage(&vk.hiWide, &vk.hiWideMemory, &vk.hiWideView);
    destroyImage(&vk.hiWideCold,&vk.hiWideColdMemory,&vk.hiWideColdView);
    hiSpareFree();
    vk.hiWideColdW=vk.hiWideColdH=vk.hiWideColdY=vk.hiWideRecentH=vk.hiWideTrimClock=0;
    vk.hiWideColdPxW=vk.hiWideColdPxH=vk.hiWideSpareW=vk.hiWideSpareH=0;
    hiFrameDone(-1); /* canvases a trim retired */
    vk.hiWideQuiet=vk.hiWideQuietH=vk.hiWideKept=0;
    destroyImage(&vk.hiScratch, &vk.hiScratchMemory, &vk.hiScratchView);
    destroyImage(&vk.hiStash, &vk.hiStashMemory, &vk.hiStashView);
    destroyImage(&vk.hiEffect,&vk.hiEffectMemory,&vk.hiEffectView);
    keepDestroy();
    destroyImage(&vk.dummy32, &vk.dummy32Memory, &vk.dummy32View);
    destroyHostBuffer(&vk.hiReadback);
    vkDestroyImageView(vk.device, vk.scratchView, NULL);
    vkDestroyImageView(vk.device, vk.wideView, NULL);
    vkDestroyImage(vk.device, vk.wide, NULL);
    vkFreeMemory(vk.device, vk.wideMemory, NULL);
    vkDestroyDevice(vk.device, NULL);
    if (vk.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
    }
    if (vk.messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(vk.instance, vk.messenger, NULL);
    }
    vkDestroyInstance(vk.instance, NULL);
    free(cpuResolve);cpuResolve=NULL;
    memset(&vk, 0, sizeof(vk));
}

/* ---- save states (system/savestate.h) ---- */

/* The hi-res copies of VRAM and of the wide target made again from their 1x
 * pictures (after a load put those back), as when the copy is first made. */
void gvkHiRefill(void) {
    cpuResolveFlush();
    if (vk.device == VK_NULL_HANDLE || vk.hiN == 0) {
        return;
    }
    endBatch();
    if (vk.hiWideCold) hiColdSpare(); /* its rows abandoned: kept for the next trim */
    recording();
    vk.needBarrier = 1;
    hiReplicate(0, 0, (VRect){0, 0, VRAM_W - 1, VRAM_H - 1}, 0, 0);
    if (vk.hiWideW > 0) {
        hiReplicate(1, 1, (VRect){0, 0, vk.hiWideW - 1, vk.hiWideH - 1}, 0, 0);
    }
}
