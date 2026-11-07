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

static bool environment(unsigned cmd, void* data) {
    FILE* log = I->optLog;
    switch (cmd) {
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
        *(bool*)data = false;
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

static void videoRefresh(const void* data, unsigned width, unsigned height, size_t pitch) {
    I->frames++;
    if (data == NULL) {
        I->dupes++;
        I->lastDupe = 1;
        return;
    }
    I->lastDupe = 0;
    if (width != I->frameW || height != I->frameH || I->frame == NULL) {
        I->frame = realloc(I->frame, (size_t)width * height * 3);
        I->frameW = width;
        I->frameH = height;
    }
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
        I->core.deinit();
        I->core.lib = NULL;
    }
    if (I->optLog) {
        fclose(I->optLog);
        I->optLog = NULL;
    }
}
