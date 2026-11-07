/*
 * beetle_front: our own minimal libretro frontend for running the Beetle PSX
 * core (mednafen_psx_libretro.so) as a black box from tools/beetle.py.
 *
 * Only the public libretro API is used: the core is loaded with dlopen, given
 * a disc image, a system directory (the BIOS images stay where the user keeps
 * them) and core options, and run one frame at a time. The frontend sees what
 * any libretro frontend sees: the video frames, the audio samples, the
 * memory regions the core exports (retro_get_memory_data), save states
 * (retro_serialize) and the option and controller lists the core declares.
 * The API's constants and structures are declared here from the libretro
 * API's documented values; no emulator source is used.
 *
 * Two instances of the core can run side by side (the second is loaded into
 * its own link-map namespace with dlmopen, so it shares nothing with the
 * first): bf_select picks the one the other calls act on. libretro callbacks
 * carry no context, so they go to the selected instance; the cores call them
 * only from inside their retro_* functions, on this thread.
 *
 * Hardware rendering (bf_hw_enable before bf_open): a core that asks for a
 * Vulkan context (SET_HW_RENDER, the Beetle PSX HW core with its Vulkan
 * renderer) gets a headless one, made here with the libretro Vulkan
 * interface (libretro_vulkan.h's documented structures: the frontend owns
 * the instance, the core may create the device through its negotiation
 * interface, and hands each finished frame over with set_image). The frame
 * is copied back to the CPU, so bf_frame/bf_frame_png work as with the
 * software renderer. Vulkan is loaded with dlopen (libvulkan.so.1) only
 * when a core asks for it; without <vulkan/vulkan.h> at build time the
 * frontend simply declines hardware rendering.
 *
 *   cc -O2 -shared -fPIC -o beetle_front.so tools/beetle_front.c -ldl -lz
 *
 * tools/beetle.py builds it on demand (into ~/.cache/xg-beetle) and drives it
 * through ctypes; tools/setup.sh --beetle builds it too.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <link.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#if defined(__has_include)
#if __has_include(<vulkan/vulkan.h>)
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#define HAVE_VULKAN 1
#endif
#endif

/* ---- the libretro API (public interface) ---- */

#define ENV_EXPERIMENTAL 0x10000
#define ENV_GET_OVERSCAN 2
#define ENV_GET_CAN_DUPE 3
#define ENV_SET_MESSAGE 6
#define ENV_SET_PERFORMANCE_LEVEL 8
#define ENV_GET_SYSTEM_DIRECTORY 9
#define ENV_SET_PIXEL_FORMAT 10
#define ENV_SET_INPUT_DESCRIPTORS 11
#define ENV_SET_DISK_CONTROL_INTERFACE 13
#define ENV_GET_VARIABLE 15
#define ENV_SET_VARIABLES 16
#define ENV_GET_VARIABLE_UPDATE 17
#define ENV_GET_LOG_INTERFACE 27
#define ENV_GET_SAVE_DIRECTORY 31
#define ENV_SET_SYSTEM_AV_INFO 32
#define ENV_SET_CONTROLLER_INFO 35
#define ENV_SET_GEOMETRY 37
#define ENV_GET_AUDIO_VIDEO_ENABLE 47
#define ENV_GET_FASTFORWARDING 49
#define ENV_GET_INPUT_BITMASKS 51
#define ENV_GET_CORE_OPTIONS_VERSION 52
#define ENV_SET_CORE_OPTIONS_DISPLAY 55
#define ENV_SET_HW_RENDER 14
#define ENV_GET_HW_RENDER_INTERFACE (41 | ENV_EXPERIMENTAL)
#define ENV_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE (43 | ENV_EXPERIMENTAL)
#define ENV_GET_PREFERRED_HW_RENDER 56

#define HW_CONTEXT_VULKAN 6
#define HW_FRAME_BUFFER_VALID ((const void*)(intptr_t)-1)

#define MEMORY_SYSTEM_RAM 2
#define DEVICE_JOYPAD 1
#define DEVICE_ID_JOYPAD_MASK 256

struct retro_variable {
    const char* key;
    const char* value;
};

struct retro_game_info {
    const char* path;
    const void* data;
    size_t size;
    const char* meta;
};

struct retro_system_info {
    const char* library_name;
    const char* library_version;
    const char* valid_extensions;
    bool need_fullpath;
    bool block_extract;
};

struct retro_game_geometry {
    unsigned base_width;
    unsigned base_height;
    unsigned max_width;
    unsigned max_height;
    float aspect_ratio;
};

struct retro_system_timing {
    double fps;
    double sample_rate;
};

struct retro_system_av_info {
    struct retro_game_geometry geometry;
    struct retro_system_timing timing;
};

struct retro_controller_description {
    const char* desc;
    unsigned id;
};

struct retro_controller_info {
    const struct retro_controller_description* types;
    unsigned num_types;
};

struct retro_message {
    const char* msg;
    unsigned frames;
};

typedef void (*retro_log_printf_t)(int level, const char* fmt, ...);
struct retro_log_callback {
    retro_log_printf_t log;
};

typedef void (*hw_reset_t)(void);
struct retro_hw_render_callback {
    unsigned context_type;
    hw_reset_t context_reset;
    uintptr_t (*get_current_framebuffer)(void);
    void* (*get_proc_address)(const char* sym);
    bool depth;
    bool stencil;
    bool bottom_left_origin;
    unsigned version_major;
    unsigned version_minor;
    bool cache_context;
    hw_reset_t context_destroy;
    bool debug_context;
};

typedef bool (*env_t)(unsigned cmd, void* data);
typedef void (*video_t)(const void* data, unsigned width, unsigned height, size_t pitch);
typedef void (*audio_t)(int16_t left, int16_t right);
typedef size_t (*audio_batch_t)(const int16_t* data, size_t frames);
typedef void (*poll_t)(void);
typedef int16_t (*input_t)(unsigned port, unsigned device, unsigned index, unsigned id);

/* ---- one core instance ---- */

#define MAX_OPTIONS 256
#define MAX_EXCLUDE 32

typedef struct {
    struct {
        void* lib;
        void (*init)(void);
        void (*deinit)(void);
        void (*set_environment)(env_t);
        void (*set_video_refresh)(video_t);
        void (*set_audio_sample)(audio_t);
        void (*set_audio_sample_batch)(audio_batch_t);
        void (*set_input_poll)(poll_t);
        void (*set_input_state)(input_t);
        void (*set_controller_port_device)(unsigned, unsigned);
        void (*get_system_info)(struct retro_system_info*);
        void (*get_system_av_info)(struct retro_system_av_info*);
        bool (*load_game)(const struct retro_game_info*);
        void (*run)(void);
        void (*reset)(void);
        size_t (*serialize_size)(void);
        bool (*serialize)(void*, size_t);
        bool (*unserialize)(const void*, size_t);
        void* (*get_memory_data)(unsigned);
        size_t (*get_memory_size)(unsigned);
        void (*unload_game)(void);
    } core;
    char sysdir[1024], savedir[1024];
    int verbose;
    FILE* optLog; /* what the core declares and asks for */
    uint16_t buttons[2];
    char optKey[MAX_OPTIONS][128], optValue[MAX_OPTIONS][128];
    int optCount;
    /* the last video frame as 8-bit RGB */
    uint8_t* frame;
    unsigned frameW, frameH, pixelFormat; /* 0 0RGB1555, 1 XRGB8888, 2 RGB565 */
    long frames, dupes;                   /* video callbacks, of which repeats (NULL data) */
    int lastDupe;
    /* audio */
    FILE* audioFile;
    uint64_t audioFrames;
    uint64_t audioHash; /* FNV-1a of the samples since bf_audio_hash */
    int audioMuted;     /* nothing reaches the file, the counters or the hash */
    /* RAM hash exclusions */
    size_t excludeLo[MAX_EXCLUDE], excludeHi[MAX_EXCLUDE];
    int excludeCount;
    /* RAM change map */
    uint8_t* touchPrev;
    uint8_t* touchMap;
    size_t touchSize;
    /* hardware rendering: allowed by bf_hw_enable, asked for by the core */
    int hwAllowed, hwRequested, hwReady;
    int varUpdate; /* GET_VARIABLE_UPDATE's next answer */
    struct retro_hw_render_callback hwcb;
    const void* negotiation;
} Inst;

#define FNV_START 1469598103934665603ull

static Inst inst[2];
static Inst* I = &inst[0];

/* Later calls act on instance i (0 or 1). */
void bf_select(int i) {
    I = &inst[i & 1];
}

static uint64_t fnv(uint64_t h, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 1099511628211ull;
    }
    return h;
}

static void logPrintf(int level, const char* fmt, ...) {
    va_list ap;
    if (I->optLog != NULL && level >= 1) {
        va_start(ap, fmt);
        fprintf(I->optLog, "log %d: ", level);
        vfprintf(I->optLog, fmt, ap);
        va_end(ap);
    }
    if (!I->verbose && level < 2) {
        return;
    }
    va_start(ap, fmt);
    fprintf(stderr, "[core %d] ", level);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static const char* findOption(const char* key) {
    for (int i = 0; i < I->optCount; i++) {
        if (strcmp(I->optKey[i], key) == 0) {
            return I->optValue[i];
        }
    }
    return NULL;
}

static const void* hwInterface(void);

static bool environment(unsigned cmd, void* data) {
    FILE* log = I->optLog;
    switch (cmd) {
    case ENV_SET_HW_RENDER: {
        struct retro_hw_render_callback* cb = data;
        if (log) fprintf(log, "hw render: context %u version %u.%u\n", cb->context_type, cb->version_major,
                         cb->version_minor);
#ifdef HAVE_VULKAN
        if (I->hwAllowed && cb->context_type == HW_CONTEXT_VULKAN) {
            cb->get_current_framebuffer = NULL;
            cb->get_proc_address = NULL;
            I->hwcb = *cb;
            I->hwRequested = 1;
            return true;
        }
#endif
        return false;
    }
    case ENV_GET_PREFERRED_HW_RENDER:
        if (!I->hwAllowed) return false;
        *(unsigned*)data = HW_CONTEXT_VULKAN;
        return true;
    case ENV_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
        if (!I->hwAllowed) return false;
        I->negotiation = data;
        if (log) fprintf(log, "hw negotiation interface given\n");
        return true;
    case ENV_GET_HW_RENDER_INTERFACE:
        if (!I->hwReady) return false;
        *(const void**)data = hwInterface();
        return true;
    case ENV_GET_CAN_DUPE:
        *(bool*)data = true;
        return true;
    case ENV_GET_OVERSCAN:
        *(bool*)data = true;
        return true;
    case ENV_GET_SYSTEM_DIRECTORY:
        *(const char**)data = I->sysdir;
        return true;
    case ENV_GET_SAVE_DIRECTORY:
        *(const char**)data = I->savedir;
        return true;
    case ENV_SET_PIXEL_FORMAT:
        I->pixelFormat = *(const unsigned*)data;
        if (log) fprintf(log, "pixel_format %u\n", I->pixelFormat);
        return true;
    case ENV_GET_LOG_INTERFACE:
        ((struct retro_log_callback*)data)->log = logPrintf;
        return true;
    case ENV_SET_VARIABLES:
        /* The legacy option list: "key" and "Description; value1|value2|...". */
        for (const struct retro_variable* v = data; v != NULL && v->key != NULL; v++) {
            if (log) fprintf(log, "declare %s = %s\n", v->key, v->value);
        }
        return true;
    case ENV_GET_VARIABLE: {
        struct retro_variable* v = data;
        const char* value = findOption(v->key);
        if (log) fprintf(log, "get %s -> %s\n", v->key, value ? value : "(default)");
        v->value = value;
        return value != NULL;
    }
    case ENV_GET_VARIABLE_UPDATE:
        /* hardware rendering: one update at the first retro_run (the HW core
         * reads its renderer's options, texture dumping and replacement among
         * them, only then) */
        *(bool*)data = I->varUpdate != 0;
        I->varUpdate = 0;
        return true;
    case ENV_GET_CORE_OPTIONS_VERSION:
        *(unsigned*)data = 0; /* legacy SET_VARIABLES, so the whole list is logged */
        return true;
    case ENV_SET_CONTROLLER_INFO: {
        const struct retro_controller_info* p = data;
        for (int port = 0; p != NULL && p[port].types != NULL; port++) {
            for (unsigned i = 0; i < p[port].num_types; i++) {
                if (log) fprintf(log, "controller port %d: %u %s\n", port + 1, p[port].types[i].id,
                                 p[port].types[i].desc ? p[port].types[i].desc : "(none)");
            }
        }
        return true;
    }
    case ENV_SET_INPUT_DESCRIPTORS:
    case ENV_SET_PERFORMANCE_LEVEL:
    case ENV_SET_DISK_CONTROL_INTERFACE:
    case ENV_SET_CORE_OPTIONS_DISPLAY:
        return true;
    case ENV_SET_GEOMETRY:
    case ENV_SET_SYSTEM_AV_INFO:
        if (log) fprintf(log, "set geometry/av (cmd %u)\n", cmd);
        return true;
    case ENV_SET_MESSAGE: {
        const struct retro_message* m = data;
        if (log && m && m->msg) fprintf(log, "message: %s\n", m->msg);
        if (I->verbose && m && m->msg) fprintf(stderr, "[core msg] %s\n", m->msg);
        return true;
    }
    case ENV_GET_INPUT_BITMASKS | ENV_EXPERIMENTAL:
    case ENV_GET_INPUT_BITMASKS:
        return true;
    case ENV_GET_AUDIO_VIDEO_ENABLE | ENV_EXPERIMENTAL:
        if (data) *(int*)data = 3; /* video and audio, every frame */
        return true;
    case ENV_GET_FASTFORWARDING | ENV_EXPERIMENTAL:
        if (data) *(bool*)data = false;
        return true;
    default:
        if (log) fprintf(log, "unhandled environment %u (0x%X)\n", cmd, cmd);
        return false;
    }
}

static void hwFrame(unsigned width, unsigned height);

static void frameSize(unsigned width, unsigned height) {
    if (width != I->frameW || height != I->frameH || I->frame == NULL) {
        I->frame = realloc(I->frame, (size_t)width * height * 3);
        I->frameW = width;
        I->frameH = height;
    }
}

static void videoRefresh(const void* data, unsigned width, unsigned height, size_t pitch) {
    I->frames++;
    if (data == NULL) {
        I->dupes++;
        I->lastDupe = 1;
        return;
    }
    I->lastDupe = 0;
    if (data == HW_FRAME_BUFFER_VALID) {
        hwFrame(width, height);
        return;
    }
    frameSize(width, height);
    unsigned fmt = I->pixelFormat;
    for (unsigned y = 0; y < height; y++) {
        const uint8_t* row = (const uint8_t*)data + y * pitch;
        uint8_t* o = I->frame + (size_t)y * width * 3;
        for (unsigned x = 0; x < width; x++, o += 3) {
            if (fmt == 1) {
                uint32_t p = ((const uint32_t*)row)[x];
                o[0] = (uint8_t)(p >> 16);
                o[1] = (uint8_t)(p >> 8);
                o[2] = (uint8_t)p;
            } else if (fmt == 2) {
                uint16_t p = ((const uint16_t*)row)[x];
                o[0] = (uint8_t)((p >> 11) << 3);
                o[1] = (uint8_t)(((p >> 5) & 63) << 2);
                o[2] = (uint8_t)((p & 31) << 3);
            } else {
                uint16_t p = ((const uint16_t*)row)[x];
                o[0] = (uint8_t)(((p >> 10) & 31) << 3);
                o[1] = (uint8_t)(((p >> 5) & 31) << 3);
                o[2] = (uint8_t)((p & 31) << 3);
            }
        }
    }
}

static void audioWrite(const int16_t* data, size_t n) {
    if (I->audioMuted) {
        return;
    }
    I->audioFrames += n;
    I->audioHash = fnv(I->audioHash, (const uint8_t*)data, n * 4);
    if (I->audioFile != NULL) {
        fwrite(data, 4, n, I->audioFile);
    }
}

static void audioSample(int16_t left, int16_t right) {
    int16_t s[2] = {left, right};
    audioWrite(s, 1);
}

static size_t audioBatch(const int16_t* data, size_t n) {
    audioWrite(data, n);
    return n;
}

static void inputPoll(void) {
}

static int16_t inputState(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port > 1 || (device & 0xFF) != DEVICE_JOYPAD || index != 0) {
        return 0;
    }
    if (id == DEVICE_ID_JOYPAD_MASK) {
        return (int16_t)I->buttons[port];
    }
    return (int16_t)((I->buttons[port] >> id) & 1);
}

/* ---- hardware rendering: a headless Vulkan context ----
 *
 * The structures below are libretro_vulkan.h's public interface (interface
 * version 5, negotiation interface version 1). One instance at a time can
 * render in hardware (the state is shared). Everything is synchronous: each
 * frame's submission is waited for before retro_run returns, so the core's
 * per-frame resources are always free again (two sync indices). */

#ifdef HAVE_VULKAN

struct retro_vulkan_image {
    VkImageView image_view;
    VkImageLayout image_layout;
    VkImageViewCreateInfo create_info;
};

struct retro_hw_render_interface_vulkan {
    unsigned interface_type; /* 0: Vulkan */
    unsigned interface_version;
    void* handle;
    VkInstance instance;
    VkPhysicalDevice gpu;
    VkDevice device;
    PFN_vkGetDeviceProcAddr get_device_proc_addr;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
    VkQueue queue;
    unsigned queue_index;
    void (*set_image)(void* handle, const struct retro_vulkan_image* image, uint32_t num_semaphores,
                      const VkSemaphore* semaphores, uint32_t src_queue_family);
    uint32_t (*get_sync_index)(void* handle);
    uint32_t (*get_sync_index_mask)(void* handle);
    void (*set_command_buffers)(void* handle, uint32_t num_cmd, const VkCommandBuffer* cmd);
    void (*wait_sync_index)(void* handle);
    void (*lock_queue)(void* handle);
    void (*unlock_queue)(void* handle);
    void (*set_signal_semaphore)(void* handle, VkSemaphore semaphore);
};

struct retro_vulkan_context {
    VkPhysicalDevice gpu;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family_index;
    VkQueue presentation_queue;
    uint32_t presentation_queue_family_index;
};

struct retro_hw_render_context_negotiation_interface_vulkan {
    unsigned interface_type; /* 0: Vulkan */
    unsigned interface_version;
    const VkApplicationInfo* (*get_application_info)(void);
    bool (*create_device)(struct retro_vulkan_context* context, VkInstance instance, VkPhysicalDevice gpu,
                          VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                          const char** required_device_extensions, unsigned num_required_device_extensions,
                          const char** required_device_layers, unsigned num_required_device_layers,
                          const VkPhysicalDeviceFeatures* required_features);
    void (*destroy_device)(void);
};

#define MAX_SEMAPHORES 16
#define MAX_CORE_CMDS 16

static struct {
    void* lib;
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkGetDeviceProcAddr gdpa;
    VkInstance instance;
    VkPhysicalDevice gpu;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    int coreMadeDevice;
    struct retro_hw_render_interface_vulkan iface;
    /* the frame the core handed over */
    struct retro_vulkan_image image;
    int haveImage;
    VkSemaphore waits[MAX_SEMAPHORES];
    uint32_t waitCount;
    VkCommandBuffer coreCmds[MAX_CORE_CMDS];
    uint32_t coreCmdCount;
    VkSemaphore signal;
    uint32_t syncIndex;
    /* the readback */
    int readback;
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize bufferSize;
    void* mapped;
    long unsupportedFormat;
    /* entry points */
    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceFeatures GetPhysicalDeviceFeatures;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
    PFN_vkCreateDevice CreateDevice;
    PFN_vkDestroyDevice DestroyDevice;
    PFN_vkGetDeviceQueue GetDeviceQueue;
    PFN_vkDeviceWaitIdle DeviceWaitIdle;
    PFN_vkQueueWaitIdle QueueWaitIdle;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkCreateCommandPool CreateCommandPool;
    PFN_vkDestroyCommandPool DestroyCommandPool;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkBeginCommandBuffer BeginCommandBuffer;
    PFN_vkEndCommandBuffer EndCommandBuffer;
    PFN_vkResetCommandBuffer ResetCommandBuffer;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer;
    PFN_vkCreateFence CreateFence;
    PFN_vkDestroyFence DestroyFence;
    PFN_vkWaitForFences WaitForFences;
    PFN_vkResetFences ResetFences;
    PFN_vkCreateBuffer CreateBuffer;
    PFN_vkDestroyBuffer DestroyBuffer;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
    PFN_vkAllocateMemory AllocateMemory;
    PFN_vkFreeMemory FreeMemory;
    PFN_vkBindBufferMemory BindBufferMemory;
    PFN_vkMapMemory MapMemory;
} vk;

static void vkSetImage(void* handle, const struct retro_vulkan_image* image, uint32_t n, const VkSemaphore* sems,
                       uint32_t srcFamily) {
    (void)handle;
    (void)srcFamily; /* one queue family: no ownership transfer */
    vk.image = *image;
    vk.haveImage = 1;
    vk.waitCount = 0;
    for (uint32_t i = 0; i < n && i < MAX_SEMAPHORES; i++) {
        vk.waits[vk.waitCount++] = sems[i];
    }
}

static uint32_t vkGetSyncIndex(void* handle) {
    (void)handle;
    return vk.syncIndex;
}

static uint32_t vkGetSyncIndexMask(void* handle) {
    (void)handle;
    return 3;
}

static void vkSetCommandBuffers(void* handle, uint32_t n, const VkCommandBuffer* cmds) {
    (void)handle;
    for (uint32_t i = 0; i < n && vk.coreCmdCount < MAX_CORE_CMDS; i++) {
        vk.coreCmds[vk.coreCmdCount++] = cmds[i];
    }
}

static void vkWaitSyncIndex(void* handle) {
    (void)handle;
    vk.QueueWaitIdle(vk.queue);
}

static void vkLockQueue(void* handle) {
    (void)handle;
}

static void vkUnlockQueue(void* handle) {
    (void)handle;
}

static void vkSetSignalSemaphore(void* handle, VkSemaphore s) {
    (void)handle;
    vk.signal = s;
}

static const void* hwInterface(void) {
    return &vk.iface;
}

static uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vk.GetPhysicalDeviceMemoryProperties(vk.gpu, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    return UINT32_MAX;
}

static int readbackBuffer(VkDeviceSize size) {
    if (vk.buffer != VK_NULL_HANDLE && vk.bufferSize >= size) {
        return 1;
    }
    if (vk.buffer != VK_NULL_HANDLE) {
        vk.DestroyBuffer(vk.device, vk.buffer, NULL);
        vk.FreeMemory(vk.device, vk.memory, NULL);
        vk.buffer = VK_NULL_HANDLE;
    }
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vk.CreateBuffer(vk.device, &bi, NULL, &vk.buffer) != VK_SUCCESS) {
        return 0;
    }
    VkMemoryRequirements req;
    vk.GetBufferMemoryRequirements(vk.device, vk.buffer, &req);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX || vk.AllocateMemory(vk.device, &ai, NULL, &vk.memory) != VK_SUCCESS) {
        return 0;
    }
    vk.BindBufferMemory(vk.device, vk.buffer, vk.memory, 0);
    vk.MapMemory(vk.device, vk.memory, 0, VK_WHOLE_SIZE, 0, &vk.mapped);
    vk.bufferSize = size;
    return 1;
}

/* Bytes per pixel of the formats a readback understands (0: none). */
static int formatBytes(VkFormat f) {
    switch (f) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        return 4;
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
    case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
        return 2;
    default:
        return 0;
    }
}

static void toRgb(VkFormat f, const uint8_t* src, uint8_t* o, size_t n) {
    for (size_t i = 0; i < n; i++, o += 3) {
        uint32_t p;
        switch (f) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            o[0] = src[i * 4 + 2], o[1] = src[i * 4 + 1], o[2] = src[i * 4];
            break;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            memcpy(&p, src + i * 4, 4);
            o[0] = (uint8_t)((p >> 2) & 255), o[1] = (uint8_t)((p >> 12) & 255), o[2] = (uint8_t)((p >> 22) & 255);
            break;
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
            memcpy(&p, src + i * 4, 4);
            o[2] = (uint8_t)((p >> 2) & 255), o[1] = (uint8_t)((p >> 12) & 255), o[0] = (uint8_t)((p >> 22) & 255);
            break;
        case VK_FORMAT_R5G6B5_UNORM_PACK16:
            p = (uint32_t)src[i * 2] | (uint32_t)src[i * 2 + 1] << 8;
            o[0] = (uint8_t)((p >> 11) << 3), o[1] = (uint8_t)(((p >> 5) & 63) << 2), o[2] = (uint8_t)((p & 31) << 3);
            break;
        case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
            p = (uint32_t)src[i * 2] | (uint32_t)src[i * 2 + 1] << 8;
            o[0] = (uint8_t)(((p >> 10) & 31) << 3), o[1] = (uint8_t)(((p >> 5) & 31) << 3),
            o[2] = (uint8_t)((p & 31) << 3);
            break;
        case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
            p = (uint32_t)src[i * 2] | (uint32_t)src[i * 2 + 1] << 8;
            o[0] = (uint8_t)((p >> 11) << 3), o[1] = (uint8_t)(((p >> 6) & 31) << 3),
            o[2] = (uint8_t)(((p >> 1) & 31) << 3);
            break;
        default: /* R8G8B8A8, A8B8G8R8 */
            o[0] = src[i * 4], o[1] = src[i * 4 + 1], o[2] = src[i * 4 + 2];
            break;
        }
    }
}

/* The core's frame is done: run what it gave us, wait for its semaphores,
 * copy the image back (when wanted) and wait for all of it. */
static void hwFrame(unsigned width, unsigned height) {
    if (!I->hwReady) {
        return;
    }
    VkSubmitInfo submits[2];
    uint32_t count = 0;
    VkPipelineStageFlags stages[MAX_SEMAPHORES];
    for (uint32_t i = 0; i < MAX_SEMAPHORES; i++) {
        stages[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    if (vk.coreCmdCount) {
        VkSubmitInfo s = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        s.commandBufferCount = vk.coreCmdCount;
        s.pCommandBuffers = vk.coreCmds;
        submits[count++] = s;
    }
    VkSubmitInfo s = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    s.waitSemaphoreCount = vk.waitCount;
    s.pWaitSemaphores = vk.waits;
    s.pWaitDstStageMask = stages;
    if (vk.signal != VK_NULL_HANDLE) {
        s.signalSemaphoreCount = 1;
        s.pSignalSemaphores = &vk.signal;
    }
    int bytes = vk.haveImage ? formatBytes(vk.image.create_info.format) : 0;
    int copy = vk.readback && vk.haveImage && bytes && width && height &&
               readbackBuffer((VkDeviceSize)width * height * (VkDeviceSize)bytes);
    if (vk.readback && vk.haveImage && !bytes && vk.unsupportedFormat++ == 0) {
        fprintf(stderr, "beetle_front: frame format %d not read back\n", (int)vk.image.create_info.format);
    }
    if (copy) {
        VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk.ResetCommandBuffer(vk.cmd, 0);
        vk.BeginCommandBuffer(vk.cmd, &bi);
        VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.oldLayout = vk.image.image_layout;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = vk.image.create_info.image;
        b.subresourceRange = vk.image.create_info.subresourceRange;
        vk.CmdPipelineBarrier(vk.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL,
                              0, NULL, 1, &b);
        VkBufferImageCopy r;
        memset(&r, 0, sizeof(r));
        r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        r.imageSubresource.mipLevel = vk.image.create_info.subresourceRange.baseMipLevel;
        r.imageSubresource.baseArrayLayer = vk.image.create_info.subresourceRange.baseArrayLayer;
        r.imageSubresource.layerCount = 1;
        r.imageExtent.width = width;
        r.imageExtent.height = height;
        r.imageExtent.depth = 1;
        vk.CmdCopyImageToBuffer(vk.cmd, b.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.buffer, 1, &r);
        b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.newLayout = vk.image.image_layout;
        vk.CmdPipelineBarrier(vk.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL,
                              0, NULL, 1, &b);
        vk.EndCommandBuffer(vk.cmd);
        s.commandBufferCount = 1;
        s.pCommandBuffers = &vk.cmd;
    }
    submits[count++] = s;
    vk.ResetFences(vk.device, 1, &vk.fence);
    vk.QueueSubmit(vk.queue, count, submits, vk.fence);
    vk.WaitForFences(vk.device, 1, &vk.fence, VK_TRUE, UINT64_MAX);
    vk.coreCmdCount = 0;
    vk.waitCount = 0;
    vk.signal = VK_NULL_HANDLE;
    vk.syncIndex = (vk.syncIndex + 1) & 1;
    if (copy) {
        frameSize(width, height);
        toRgb(vk.image.create_info.format, vk.mapped, I->frame, (size_t)width * height);
    }
}

static int pickDevice(void) {
    uint32_t n = 0;
    vk.EnumeratePhysicalDevices(vk.instance, &n, NULL);
    if (n == 0) {
        return 0;
    }
    VkPhysicalDevice list[16];
    if (n > 16) n = 16;
    vk.EnumeratePhysicalDevices(vk.instance, &n, list);
    /* XG_BEETLE_VK_DEVICE: a part of the device's name (default Intel: the
     * user may be playing on the discrete GPU), else the first device */
    const char* want = getenv("XG_BEETLE_VK_DEVICE");
    if (want == NULL || want[0] == '\0') want = "Intel";
    vk.gpu = list[0];
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p;
        vk.GetPhysicalDeviceProperties(list[i], &p);
        if (strcasestr(p.deviceName, want) != NULL) {
            vk.gpu = list[i];
            break;
        }
    }
    VkPhysicalDeviceProperties p;
    vk.GetPhysicalDeviceProperties(vk.gpu, &p);
    if (I->optLog) fprintf(I->optLog, "hw device %s\n", p.deviceName);
    if (I->verbose) fprintf(stderr, "beetle_front: Vulkan device %s\n", p.deviceName);
    uint32_t qn = 0;
    vk.GetPhysicalDeviceQueueFamilyProperties(vk.gpu, &qn, NULL);
    VkQueueFamilyProperties q[16];
    if (qn > 16) qn = 16;
    vk.GetPhysicalDeviceQueueFamilyProperties(vk.gpu, &qn, q);
    for (uint32_t i = 0; i < qn; i++) {
        VkQueueFlags need = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((q[i].queueFlags & need) == need) {
            vk.family = i;
            return 1;
        }
    }
    return 0;
}

#define VKI(name) vk.name = (PFN_vk##name)vk.gipa(vk.instance, "vk" #name)
#define VKD(name) vk.name = (PFN_vk##name)vk.gdpa(vk.device, "vk" #name)

static int hwCreate(void) {
    vk.lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (vk.lib == NULL) {
        fprintf(stderr, "beetle_front: no libvulkan.so.1 (%s)\n", dlerror());
        return 0;
    }
    vk.gipa = (PFN_vkGetInstanceProcAddr)dlsym(vk.lib, "vkGetInstanceProcAddr");
    const struct retro_hw_render_context_negotiation_interface_vulkan* neg = I->negotiation;
    if (neg != NULL && neg->interface_type != 0) {
        neg = NULL;
    }
    const VkApplicationInfo* app = neg && neg->get_application_info ? neg->get_application_info() : NULL;
    VkApplicationInfo fallback = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    fallback.pApplicationName = "beetle_front";
    fallback.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = app ? app : &fallback;
    PFN_vkCreateInstance createInstance = (PFN_vkCreateInstance)vk.gipa(NULL, "vkCreateInstance");
    if (createInstance(&ci, NULL, &vk.instance) != VK_SUCCESS) {
        fprintf(stderr, "beetle_front: vkCreateInstance failed\n");
        return 0;
    }
    VKI(DestroyInstance);
    VKI(EnumeratePhysicalDevices);
    VKI(GetPhysicalDeviceProperties);
    VKI(GetPhysicalDeviceQueueFamilyProperties);
    VKI(GetPhysicalDeviceFeatures);
    VKI(GetPhysicalDeviceMemoryProperties);
    VKI(CreateDevice);
    vk.gdpa = (PFN_vkGetDeviceProcAddr)vk.gipa(vk.instance, "vkGetDeviceProcAddr");
    if (!pickDevice()) {
        fprintf(stderr, "beetle_front: no Vulkan device with a graphics and compute queue\n");
        return 0;
    }
    if (neg && neg->create_device) {
        struct retro_vulkan_context ctx;
        memset(&ctx, 0, sizeof(ctx));
        static const char* noNames[1] = {NULL};
        VkPhysicalDeviceFeatures none;
        memset(&none, 0, sizeof(none));
        if (I->verbose) fprintf(stderr, "beetle_front: the core creates the device\n");
        if (!neg->create_device(&ctx, vk.instance, vk.gpu, VK_NULL_HANDLE, vk.gipa, noNames, 0, noNames, 0, &none)) {
            fprintf(stderr, "beetle_front: the core's create_device failed\n");
            return 0;
        }
        vk.gpu = ctx.gpu ? ctx.gpu : vk.gpu;
        vk.device = ctx.device;
        vk.queue = ctx.queue;
        vk.family = ctx.queue_family_index;
        vk.coreMadeDevice = 1;
        if (I->optLog) fprintf(I->optLog, "hw device made by the core (queue family %u)\n", vk.family);
    } else {
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qi = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex = vk.family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &prio;
        VkPhysicalDeviceFeatures features;
        vk.GetPhysicalDeviceFeatures(vk.gpu, &features);
        VkDeviceCreateInfo di = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;
        di.pEnabledFeatures = &features;
        if (vk.CreateDevice(vk.gpu, &di, NULL, &vk.device) != VK_SUCCESS) {
            fprintf(stderr, "beetle_front: vkCreateDevice failed\n");
            return 0;
        }
        VKI(GetDeviceQueue);
        vk.GetDeviceQueue(vk.device, vk.family, 0, &vk.queue);
    }
    VKD(DestroyDevice);
    VKD(DeviceWaitIdle);
    VKD(QueueWaitIdle);
    VKD(QueueSubmit);
    VKD(CreateCommandPool);
    VKD(DestroyCommandPool);
    VKD(AllocateCommandBuffers);
    VKD(BeginCommandBuffer);
    VKD(EndCommandBuffer);
    VKD(ResetCommandBuffer);
    VKD(CmdPipelineBarrier);
    VKD(CmdCopyImageToBuffer);
    VKD(CreateFence);
    VKD(DestroyFence);
    VKD(WaitForFences);
    VKD(ResetFences);
    VKD(CreateBuffer);
    VKD(DestroyBuffer);
    VKD(GetBufferMemoryRequirements);
    VKD(AllocateMemory);
    VKD(FreeMemory);
    VKD(BindBufferMemory);
    VKD(MapMemory);
    VkCommandPoolCreateInfo pi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = vk.family;
    vk.CreateCommandPool(vk.device, &pi, NULL, &vk.pool);
    VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = vk.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vk.AllocateCommandBuffers(vk.device, &ai, &vk.cmd);
    VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vk.CreateFence(vk.device, &fi, NULL, &vk.fence);

    struct retro_hw_render_interface_vulkan* f = &vk.iface;
    f->interface_type = 0;
    f->interface_version = 5;
    f->handle = I;
    f->instance = vk.instance;
    f->gpu = vk.gpu;
    f->device = vk.device;
    f->get_device_proc_addr = vk.gdpa;
    f->get_instance_proc_addr = vk.gipa;
    f->queue = vk.queue;
    f->queue_index = vk.family;
    f->set_image = vkSetImage;
    f->get_sync_index = vkGetSyncIndex;
    f->get_sync_index_mask = vkGetSyncIndexMask;
    f->set_command_buffers = vkSetCommandBuffers;
    f->wait_sync_index = vkWaitSyncIndex;
    f->lock_queue = vkLockQueue;
    f->unlock_queue = vkUnlockQueue;
    f->set_signal_semaphore = vkSetSignalSemaphore;
    I->hwReady = 1;
    if (I->verbose) fprintf(stderr, "beetle_front: context_reset\n");
    if (I->hwcb.context_reset) {
        I->hwcb.context_reset();
    }
    return 1;
}

static void hwDestroy(void) {
    if (!I->hwReady) {
        return;
    }
    vk.DeviceWaitIdle(vk.device);
    if (I->hwcb.context_destroy) {
        I->hwcb.context_destroy();
    }
    const struct retro_hw_render_context_negotiation_interface_vulkan* neg = I->negotiation;
    if (vk.coreMadeDevice && neg && neg->destroy_device) {
        neg->destroy_device();
    }
    if (vk.buffer) {
        vk.DestroyBuffer(vk.device, vk.buffer, NULL);
        vk.FreeMemory(vk.device, vk.memory, NULL);
    }
    vk.DestroyFence(vk.device, vk.fence, NULL);
    vk.DestroyCommandPool(vk.device, vk.pool, NULL);
    vk.DestroyDevice(vk.device, NULL);
    vk.DestroyInstance(vk.instance, NULL);
    I->hwReady = 0;
    int readback = vk.readback;
    memset(&vk, 0, sizeof(vk));
    vk.readback = readback;
}

#else /* no Vulkan headers at build time: hardware rendering is declined */

static const void* hwInterface(void) {
    return NULL;
}
static void hwFrame(unsigned width, unsigned height) {
    (void)width;
    (void)height;
}
static int hwCreate(void) {
    return 0;
}
static void hwDestroy(void) {
}

#endif

/* Before bf_open: let the core render in hardware (a Vulkan context), and
 * whether each frame is read back for bf_frame (on by default). */
int bf_hw_enable(int on, int readback) {
    I->hwAllowed = on;
#ifdef HAVE_VULKAN
    vk.readback = readback;
    return 1;
#else
    (void)readback;
    return 0;
#endif
}

/* 1 when the core renders through the Vulkan context. */
int bf_hw_active(void) {
    return I->hwReady;
}

#define SYM(name)                                                                                                     \
    do {                                                                                                              \
        *(void**)&I->core.name = dlsym(I->core.lib, "retro_" #name);                                                  \
        if (!I->core.name) {                                                                                          \
            fprintf(stderr, "beetle_front: missing retro_" #name "\n");                                              \
            return 0;                                                                                                 \
        }                                                                                                             \
    } while (0)

int bf_option(const char* key, const char* value) {
    for (int i = 0; i < I->optCount; i++) {
        if (strcmp(I->optKey[i], key) == 0) {
            snprintf(I->optValue[i], sizeof(I->optValue[i]), "%s", value);
            return 1;
        }
    }
    if (I->optCount == MAX_OPTIONS) {
        return 0;
    }
    snprintf(I->optKey[I->optCount], sizeof(I->optKey[0]), "%s", key);
    snprintf(I->optValue[I->optCount], sizeof(I->optValue[0]), "%s", value);
    I->optCount++;
    return 1;
}

/* Load the core into the selected instance and give it the disc.
 * optionLog: a file for what the core declares and asks for (NULL or "": none). */
int bf_open(const char* corePath, const char* systemDir, const char* saveDir, const char* content, int beVerbose,
            const char* optionLog) {
    I->verbose = beVerbose;
    I->audioHash = FNV_START;
    snprintf(I->sysdir, sizeof(I->sysdir), "%s", systemDir);
    snprintf(I->savedir, sizeof(I->savedir), "%s", saveDir);
    if (optionLog != NULL && optionLog[0] != '\0') {
        I->optLog = fopen(optionLog, "w");
    }
    /* The second instance in a namespace of its own: separate globals. */
    I->core.lib = I == &inst[0] ? dlopen(corePath, RTLD_NOW | RTLD_LOCAL)
                                : dlmopen(LM_ID_NEWLM, corePath, RTLD_NOW | RTLD_LOCAL);
    if (!I->core.lib) {
        fprintf(stderr, "beetle_front: %s\n", dlerror());
        return 0;
    }
    SYM(init);
    SYM(deinit);
    SYM(set_environment);
    SYM(set_video_refresh);
    SYM(set_audio_sample);
    SYM(set_audio_sample_batch);
    SYM(set_input_poll);
    SYM(set_input_state);
    SYM(set_controller_port_device);
    SYM(get_system_info);
    SYM(get_system_av_info);
    SYM(load_game);
    SYM(run);
    SYM(reset);
    SYM(serialize_size);
    SYM(serialize);
    SYM(unserialize);
    SYM(get_memory_data);
    SYM(get_memory_size);
    SYM(unload_game);
    I->core.set_environment(environment);
    I->core.init();
    I->core.set_video_refresh(videoRefresh);
    I->core.set_audio_sample(audioSample);
    I->core.set_audio_sample_batch(audioBatch);
    I->core.set_input_poll(inputPoll);
    I->core.set_input_state(inputState);
    struct retro_system_info info;
    memset(&info, 0, sizeof(info));
    I->core.get_system_info(&info);
    if (I->optLog) {
        fprintf(I->optLog, "core %s %s (%s) need_fullpath=%d\n", info.library_name, info.library_version,
                info.valid_extensions, info.need_fullpath);
    }
    if (I->verbose) {
        fprintf(stderr, "beetle_front: %s %s\n", info.library_name, info.library_version);
    }
    struct retro_game_info game = {content, NULL, 0, NULL};
    void* bytes = NULL;
    if (!info.need_fullpath) {
        FILE* f = fopen(content, "rb");
        if (!f) {
            return 0;
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        bytes = malloc((size_t)n);
        if (fread(bytes, 1, (size_t)n, f) != (size_t)n) {
            n = 0;
        }
        fclose(f);
        game.data = bytes;
        game.size = (size_t)n;
    }
    if (!I->core.load_game(&game)) {
        fprintf(stderr, "beetle_front: retro_load_game failed\n");
        return 0;
    }
    if (I->hwRequested && !hwCreate()) {
        fprintf(stderr, "beetle_front: no Vulkan context for the core\n");
        return 0;
    }
    I->varUpdate = I->hwReady;
    /* again after the game is in: the HW core takes its video callback for
     * the hardware renderer only from a call made after retro_load_game */
    I->core.set_video_refresh(videoRefresh);
    I->core.set_audio_sample(audioSample);
    I->core.set_audio_sample_batch(audioBatch);
    I->core.set_input_poll(inputPoll);
    I->core.set_input_state(inputState);
    if (I->optLog) {
        fflush(I->optLog);
    }
    return 1;
}

/* RetroPad device for a port (1 = RETRO_DEVICE_JOYPAD, 0 = none, or one of
 * the subclasses the core lists in SET_CONTROLLER_INFO). */
void bf_port_device(int port, unsigned device) {
    I->core.set_controller_port_device((unsigned)port, device);
}

void bf_run(int n) {
    for (int i = 0; i < n; i++) {
        I->core.run();
    }
}

void bf_reset(void) {
    I->core.reset();
}

void* bf_memory(unsigned id) {
    return I->core.get_memory_data(id);
}

size_t bf_memory_size(unsigned id) {
    return I->core.get_memory_size(id);
}

/* Buttons in RetroPad bit order (bit n = RETRO_DEVICE_ID_JOYPAD n). */
void bf_buttons(int port, int mask) {
    if (port >= 0 && port < 2) {
        I->buttons[port] = (uint16_t)mask;
    }
}

/* fps, sample rate, base width, base height, max width, max height, aspect. */
void bf_av_info(double* out) {
    struct retro_system_av_info av;
    memset(&av, 0, sizeof(av));
    I->core.get_system_av_info(&av);
    out[0] = av.timing.fps;
    out[1] = av.timing.sample_rate;
    out[2] = av.geometry.base_width;
    out[3] = av.geometry.base_height;
    out[4] = av.geometry.max_width;
    out[5] = av.geometry.max_height;
    out[6] = av.geometry.aspect_ratio;
}

/* The last frame (8-bit RGB rows): its pointer; dims gets width, height,
 * pixel format, whether the last video callback repeated it, and the counts
 * of video callbacks and repeats. */
const uint8_t* bf_frame(long* dims) {
    dims[0] = I->frameW;
    dims[1] = I->frameH;
    dims[2] = I->pixelFormat;
    dims[3] = I->lastDupe;
    dims[4] = I->frames;
    dims[5] = I->dupes;
    return I->frame;
}

/* ---- hashes, for comparing runs frame by frame ---- */

/* The last frame's pixels (with its size). */
uint64_t bf_frame_hash(void) {
    uint32_t wh[2] = {I->frameW, I->frameH};
    uint64_t h = fnv(FNV_START, (const uint8_t*)wh, sizeof(wh));
    return I->frame ? fnv(h, I->frame, (size_t)I->frameW * I->frameH * 3) : h;
}

/* The audio since the last call. */
uint64_t bf_audio_hash(void) {
    uint64_t h = I->audioHash;
    I->audioHash = FNV_START;
    return h;
}

/* Leave RAM bytes [lo, hi) out of bf_ram_hash (the hooks' own words). */
void bf_hash_exclude(size_t lo, size_t hi) {
    if (I->excludeCount < MAX_EXCLUDE) {
        I->excludeLo[I->excludeCount] = lo;
        I->excludeHi[I->excludeCount++] = hi;
    }
}

static uint64_t ramHash(size_t at) {
    const uint8_t* ram = I->core.get_memory_data(MEMORY_SYSTEM_RAM);
    size_t n = I->core.get_memory_size(MEMORY_SYSTEM_RAM);
    uint64_t h = FNV_START;
    while (at < n) {
        size_t next = n;
        int skip = -1;
        for (int i = 0; i < I->excludeCount; i++) {
            if (I->excludeLo[i] <= at && at < I->excludeHi[i]) {
                skip = i;
            } else if (I->excludeLo[i] > at && I->excludeLo[i] < next) {
                next = I->excludeLo[i];
            }
        }
        if (skip >= 0) {
            at = I->excludeHi[skip];
            continue;
        }
        h = fnv(h, ram + at, next - at);
        at = next;
    }
    return h;
}

uint64_t bf_ram_hash(void) {
    return ramHash(0);
}

/* User RAM only (from 0x10000: the kernel's first 64 KB left out). */
uint64_t bf_user_ram_hash(void) {
    return ramHash(0x10000);
}

/* ---- PNG (8-bit RGB) ---- */

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void chunk(FILE* f, const char* tag, const uint8_t* data, uint32_t n) {
    uint8_t head[8];
    put32(head, n);
    memcpy(head + 4, tag, 4);
    fwrite(head, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    uLong crc = crc32(0, (const Bytef*)tag, 4);
    if (n) crc = crc32(crc, data, n);
    uint8_t c[4];
    put32(c, (uint32_t)crc);
    fwrite(c, 1, 4, f);
}

/* Write a w*h RGB image (rows of w*3 bytes, stride bytes apart) as a PNG. */
int bf_write_png(const char* path, const uint8_t* rgb, int w, int h, int stride) {
    size_t rawSize = (size_t)h * (w * 3 + 1);
    uint8_t* raw = malloc(rawSize);
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * (w * 3 + 1)] = 0;
        memcpy(raw + (size_t)y * (w * 3 + 1) + 1, rgb + (size_t)y * stride, (size_t)w * 3);
    }
    uLongf zSize = compressBound(rawSize);
    uint8_t* z = malloc(zSize);
    compress2(z, &zSize, raw, rawSize, 1);
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        free(raw);
        free(z);
        return 0;
    }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)zSize);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 1;
}

/* The last frame, or its rectangle x,y,w,h (w <= 0: all of it), as a PNG. */
int bf_frame_png(const char* path, int x, int y, int w, int h) {
    if (I->frame == NULL) {
        return 0;
    }
    if (w <= 0 || h <= 0) {
        x = y = 0;
        w = (int)I->frameW;
        h = (int)I->frameH;
    }
    if (x < 0 || y < 0 || x + w > (int)I->frameW || y + h > (int)I->frameH) {
        return 0;
    }
    return bf_write_png(path, I->frame + ((size_t)y * I->frameW + x) * 3, w, h, (int)I->frameW * 3);
}

/* ---- audio: interleaved stereo int16 little-endian, appended to a file ---- */

int bf_audio_file(const char* path) {
    if (I->audioFile != NULL) {
        fclose(I->audioFile);
        I->audioFile = NULL;
    }
    if (path != NULL && path[0] != '\0') {
        I->audioFile = fopen(path, "wb");
        return I->audioFile != NULL;
    }
    return 1;
}

uint64_t bf_audio_frames(void) {
    return I->audioFrames;
}

void bf_audio_mute(int on) {
    I->audioMuted = on;
}

/* ---- save states (retro_serialize) ---- */

int bf_state_save(const char* path) {
    size_t n = I->core.serialize_size();
    void* buf = malloc(n);
    if (buf == NULL || !I->core.serialize(buf, n)) {
        free(buf);
        return 0;
    }
    FILE* f = fopen(path, "wb");
    int ok = f != NULL && fwrite(buf, 1, n, f) == n;
    if (f) fclose(f);
    free(buf);
    return ok;
}

int bf_state_load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void* buf = malloc((size_t)n);
    int ok = fread(buf, 1, (size_t)n, f) == (size_t)n && I->core.unserialize(buf, (size_t)n);
    fclose(f);
    free(buf);
    return ok;
}

/* In-memory snapshot, shared by the instances: one saves the selected
 * instance's state, the other (or the same one) loads it. */
static void* snap;
static size_t snapSize;

int bf_snap_save(void) {
    size_t n = I->core.serialize_size();
    if (n != snapSize) {
        snap = realloc(snap, n);
        snapSize = n;
    }
    return snap != NULL && I->core.serialize(snap, n);
}

int bf_snap_load(void) {
    return snap != NULL && I->core.unserialize(snap, snapSize);
}

/* ---- RAM change map: which 32-bit words of main RAM ever changed between
 * two frames (for finding RAM the game never uses) ---- */

int bf_touch_reset(void) {
    uint8_t* ram = I->core.get_memory_data(MEMORY_SYSTEM_RAM);
    size_t n = I->core.get_memory_size(MEMORY_SYSTEM_RAM);
    if (ram == NULL) return 0;
    I->touchSize = n;
    I->touchPrev = realloc(I->touchPrev, n);
    I->touchMap = realloc(I->touchMap, n / 4);
    memcpy(I->touchPrev, ram, n);
    memset(I->touchMap, 0, n / 4);
    return 1;
}

void bf_touch_update(void) {
    const uint32_t* ram = I->core.get_memory_data(MEMORY_SYSTEM_RAM);
    uint32_t* prev = (uint32_t*)I->touchPrev;
    for (size_t i = 0; i < I->touchSize / 4; i++) {
        if (ram[i] != prev[i]) {
            I->touchMap[i] = 1;
            prev[i] = ram[i];
        }
    }
}

const uint8_t* bf_touch_map(void) {
    return I->touchMap;
}

void bf_close(void) {
    bf_audio_file(NULL);
    if (I->core.lib) {
        I->core.unload_game();
        hwDestroy();
        I->core.deinit();
        I->core.lib = NULL;
    }
    if (I->optLog) {
        fclose(I->optLog);
        I->optLog = NULL;
    }
}
