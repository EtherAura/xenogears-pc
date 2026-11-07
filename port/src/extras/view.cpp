// A viewer's picture (view.h): PS1 primitives drawn by our Vulkan device
// into an image of the view's own, then shown by ImGui.
//
// Each frame's offscreen drawing goes into one command buffer of the
// extras' own (two in flight, each with its fence), handed to the queue just
// before the present pass's (viewsEndFrame): its last barrier makes each
// view's image readable by the present pass's fragment shaders, and its
// first one waits for the previous present pass to be done reading it.

#include "view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>
#include <cmath>

#include "imgui_impl_vulkan.h"
#include "imgui_internal.h"
#include "shell.h"
#include "viewer.h"
#include "bookmarks.h"
#include "maprast.h"
#include "replace.h"

namespace extras {

namespace {

const uint32_t vertSpv[] =
#include "view.vert.spv.inc"
    ;
const uint32_t fragSpv[] =
#include "view.frag.spv.inc"
    ;

// The Enhanced look (replace.h): each pack picture a view draws, on the device once
// (R.images, by its id), kept while views use it, the least recently used freed past
// IMAGE_BYTES; its descriptor set (set 1: the index map, the RGBA image; a 1x1 stand-in
// for the other) bound for the primitives that read it.
#define IMAGE_BYTES ((VkDeviceSize)512 << 20)
#define IMAGE_SETS 1024

// view.frag's mode bits and push-constant flags.
enum { MODE_TEXTURED = 4, MODE_RAW = 8, MODE_SEMI = 16, MODE_FREE_UV = 128 };
enum { FLAG_AREA = 1, FLAG_TEXTURES = 2 };

#define SLOTS 2
#define PIPES 5 // opaque, then semi-transparency modes 0-3
#define TARGET_FORMAT VK_FORMAT_R8G8B8A8_UNORM
#define VRAM_BYTES (XG_VRAM_W * XG_VRAM_H * 2)
#define MAX_VIEWS 64

struct Vtx {
    float x, y, u, v;
    uint8_t rgba[4];
    uint16_t tex[4];
    uint32_t mode;
    int16_t area[4];
    int16_t rep[4]; // a replacement: u, v, scale, kind (view.frag)
};

struct Batch {
    int pipe;
    uint32_t pass, first, count;
    int image; // R.images index, -1 none
};

struct RepImage {
    uint64_t id = 0;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDeviceSize bytes = 0;
    uint64_t used = 0; // R.frame of its last draw
};

#define VK_FUNCS(X)                                                                                                 \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory)        \
    X(vkBindBufferMemory) X(vkMapMemory) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements)         \
    X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler)          \
    X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreateDescriptorPool)                         \
    X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkFreeDescriptorSets) X(vkUpdateDescriptorSets)         \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateShaderModule) X(vkDestroyShaderModule)            \
    X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreateCommandPool) X(vkDestroyCommandPool)                 \
    X(vkAllocateCommandBuffers) X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences)              \
    X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkQueueSubmit) X(vkDeviceWaitIdle)       \
    X(vkCmdPipelineBarrier2) X(vkCmdBeginRendering) X(vkCmdEndRendering) X(vkCmdBindPipeline)                       \
    X(vkCmdBindVertexBuffers) X(vkCmdBindDescriptorSets) X(vkCmdPushConstants) X(vkCmdSetViewport)                   \
    X(vkCmdSetScissor) X(vkCmdDraw) X(vkCmdCopyBufferToImage) X(vkGetPhysicalDeviceMemoryProperties)

struct Fn {
#define DECLARE(name) PFN_##name name;
    VK_FUNCS(DECLARE)
#undef DECLARE
};

struct Buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint8_t* map;
    VkDeviceSize size;
};

struct Slot {
    VkCommandBuffer cmd;
    VkFence fence;
    Buffer vertices, staging;
    VkDeviceSize vertexUsed, stagingUsed;
    VkDeviceSize vertexWanted, stagingWanted; // grow to this at the next begin
    bool recording, submitted;
};

struct Renderer {
    bool ready, broken;
    Fn f;
    VkDevice dev;
    VkQueue queue;
    VkPhysicalDeviceMemoryProperties mem;
    VkCommandPool pool;
    Slot slots[SLOTS];
    int slot;
    bool frameOpen;
    VkDescriptorSetLayout setLayout;
    VkPipelineLayout layout;
    VkDescriptorPool descriptors;
    VkSampler sampler;
    VkPipeline pipes[PIPES];
    std::vector<View::Impl*> views;
    // The Enhanced look's pictures (set 1).
    VkDescriptorSetLayout repLayout;
    VkDescriptorPool repPool;
    RepImage dummyIndex, dummyRgba; // the stand-ins, 1x1
    VkDescriptorSet noneSet;        // both stand-ins: primitives drawn from VRAM
    bool dummiesReady;
    std::vector<RepImage> images;
    VkDeviceSize imageBytes;
    uint64_t frame;
};

Renderer R;

bool check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) {
        fprintf(stderr, "extras: view: %s failed (%d)\n", what, (int)r);
        R.broken = true;
        return false;
    }
    return true;
}

uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < R.mem.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (R.mem.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    for (uint32_t i = 0; i < R.mem.memoryTypeCount; i++) {
        if (bits & (1u << i)) {
            return i;
        }
    }
    return 0;
}

bool makeBuffer(Buffer* b, VkDeviceSize size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!check(R.f.vkCreateBuffer(R.dev, &bi, nullptr, &b->buffer), "vkCreateBuffer")) {
        return false;
    }
    VkMemoryRequirements req;
    R.f.vkGetBufferMemoryRequirements(R.dev, b->buffer, &req);
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!check(R.f.vkAllocateMemory(R.dev, &ai, nullptr, &b->memory), "vkAllocateMemory") ||
        !check(R.f.vkBindBufferMemory(R.dev, b->buffer, b->memory, 0), "vkBindBufferMemory") ||
        !check(R.f.vkMapMemory(R.dev, b->memory, 0, VK_WHOLE_SIZE, 0, (void**)&b->map), "vkMapMemory")) {
        return false;
    }
    b->size = size;
    return true;
}

void freeBuffer(Buffer* b) {
    if (b->buffer != VK_NULL_HANDLE) {
        R.f.vkDestroyBuffer(R.dev, b->buffer, nullptr);
        R.f.vkFreeMemory(R.dev, b->memory, nullptr);
    }
    *b = Buffer();
}

bool makeImage(VkImage* image, VkDeviceMemory* memory, VkImageView* view, VkFormat format, int w, int h,
               VkImageUsageFlags usage) {
    VkImageCreateInfo ii = {};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {(uint32_t)w, (uint32_t)h, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!check(R.f.vkCreateImage(R.dev, &ii, nullptr, image), "vkCreateImage")) {
        return false;
    }
    VkMemoryRequirements req;
    R.f.vkGetImageMemoryRequirements(R.dev, *image, &req);
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!check(R.f.vkAllocateMemory(R.dev, &ai, nullptr, memory), "vkAllocateMemory") ||
        !check(R.f.vkBindImageMemory(R.dev, *image, *memory, 0), "vkBindImageMemory")) {
        return false;
    }
    VkImageViewCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = *image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return check(R.f.vkCreateImageView(R.dev, &vi, nullptr, view), "vkCreateImageView");
}

void barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage,
             VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 b = {};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo di = {};
    di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &b;
    R.f.vkCmdPipelineBarrier2(cmd, &di);
}

VkShaderModule shader(const uint32_t* code, size_t size) {
    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    check(R.f.vkCreateShaderModule(R.dev, &info, nullptr, &m), "vkCreateShaderModule");
    return m;
}

bool makePipelines() {
    VkShaderModule vs = shader(vertSpv, sizeof(vertSpv)), fs = shader(fragSpv, sizeof(fragSpv));
    if (vs == VK_NULL_HANDLE || fs == VK_NULL_HANDLE) {
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";
    VkVertexInputBindingDescription binding = {0, sizeof(Vtx), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[7] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vtx, x)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vtx, u)},
        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Vtx, rgba)},
        {3, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(Vtx, tex)},
        {4, 0, VK_FORMAT_R32_UINT, offsetof(Vtx, mode)},
        {5, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(Vtx, area)},
        {6, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(Vtx, rep)},
    };
    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 7;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkFormat format = TARGET_FORMAT;
    VkPipelineRenderingCreateInfo ri = {};
    ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &format;
    bool ok = true;
    for (int i = 0; i < PIPES && ok; i++) {
        // Opaque; then the PS1's B/2+F/2, B+F, B-F, B+F/4 (the shader gives
        // alpha 0.5 for the first, 0.25 for the last).
        VkPipelineColorBlendAttachmentState blend = {};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
        if (i == 0) {
            blend.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
        } else {
            blend.blendEnable = VK_TRUE;
            int abr = i - 1;
            blend.srcColorBlendFactor = abr == 0 || abr == 3 ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
            blend.dstColorBlendFactor = abr == 0 ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
            blend.colorBlendOp = abr == 2 ? VK_BLEND_OP_REVERSE_SUBTRACT : VK_BLEND_OP_ADD;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo cb = {};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments = &blend;
        VkGraphicsPipelineCreateInfo pi = {};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.pNext = &ri;
        pi.stageCount = 2;
        pi.pStages = stages;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pColorBlendState = &cb;
        pi.pDynamicState = &ds;
        pi.layout = R.layout;
        ok = check(R.f.vkCreateGraphicsPipelines(R.dev, VK_NULL_HANDLE, 1, &pi, nullptr, &R.pipes[i]),
                   "vkCreateGraphicsPipelines");
    }
    R.f.vkDestroyShaderModule(R.dev, vs, nullptr);
    R.f.vkDestroyShaderModule(R.dev, fs, nullptr);
    return ok;
}

bool start() {
    if (R.ready || R.broken) {
        return R.ready;
    }
    const Device* d = device();
    if (d == nullptr) {
        return false;
    }
    R.dev = d->device;
    R.queue = d->queue;
#define LOAD(name)                                     \
    R.f.name = (PFN_##name)vkLoad(#name);              \
    if (R.f.name == nullptr) {                         \
        fprintf(stderr, "extras: view: no " #name "\n"); \
        R.broken = true;                               \
        return false;                                  \
    }
    VK_FUNCS(LOAD)
#undef LOAD
    R.f.vkGetPhysicalDeviceMemoryProperties(d->physical, &R.mem);

    VkCommandPoolCreateInfo pi = {};
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = d->queueFamily;
    if (!check(R.f.vkCreateCommandPool(R.dev, &pi, nullptr, &R.pool), "vkCreateCommandPool")) {
        return false;
    }
    for (Slot& s : R.slots) {
        VkCommandBufferAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = R.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkFenceCreateInfo fi = {};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (!check(R.f.vkAllocateCommandBuffers(R.dev, &ai, &s.cmd), "vkAllocateCommandBuffers") ||
            !check(R.f.vkCreateFence(R.dev, &fi, nullptr, &s.fence), "vkCreateFence") ||
            !makeBuffer(&s.vertices, 4u << 20, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
            !makeBuffer(&s.staging, 4u << 20, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
            return false;
        }
    }

    VkDescriptorSetLayoutBinding b = {};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo li = {};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 1;
    li.pBindings = &b;
    VkDescriptorSetLayoutBinding rb[2] = {};
    for (int i = 0; i < 2; i++) {
        rb[i].binding = (uint32_t)i;
        rb[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        rb[i].descriptorCount = 1;
        rb[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo rli = {};
    rli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    rli.bindingCount = 2;
    rli.pBindings = rb;
    if (!check(R.f.vkCreateDescriptorSetLayout(R.dev, &rli, nullptr, &R.repLayout), "vkCreateDescriptorSetLayout")) {
        return false;
    }
    VkDescriptorPoolSize repSize = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (IMAGE_SETS + 1) * 2};
    VkDescriptorPoolCreateInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    rp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    rp.maxSets = IMAGE_SETS + 1;
    rp.poolSizeCount = 1;
    rp.pPoolSizes = &repSize;
    if (!check(R.f.vkCreateDescriptorPool(R.dev, &rp, nullptr, &R.repPool), "vkCreateDescriptorPool")) {
        return false;
    }
    if (!check(R.f.vkCreateDescriptorSetLayout(R.dev, &li, nullptr, &R.setLayout), "vkCreateDescriptorSetLayout")) {
        return false;
    }
    VkDescriptorSetLayout layouts[2] = {R.setLayout, R.repLayout};
    VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 24};
    VkPipelineLayoutCreateInfo pl = {};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 2;
    pl.pSetLayouts = layouts;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_VIEWS};
    VkDescriptorPoolCreateInfo dp = {};
    dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dp.maxSets = MAX_VIEWS;
    dp.poolSizeCount = 1;
    dp.pPoolSizes = &size;
    VkSamplerCreateInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!check(R.f.vkCreatePipelineLayout(R.dev, &pl, nullptr, &R.layout), "vkCreatePipelineLayout") ||
        !check(R.f.vkCreateDescriptorPool(R.dev, &dp, nullptr, &R.descriptors), "vkCreateDescriptorPool") ||
        !check(R.f.vkCreateSampler(R.dev, &si, nullptr, &R.sampler), "vkCreateSampler") || !makePipelines()) {
        return false;
    }
    R.ready = true;
    return true;
}

Slot& slot() {
    return R.slots[R.slot];
}

// Bytes in this frame's buffer (vertices or staging); nullptr when full (the
// next frame's is made larger).
uint8_t* reserve(bool staging, VkDeviceSize bytes, VkDeviceSize* offset) {
    Slot& s = slot();
    Buffer& b = staging ? s.staging : s.vertices;
    VkDeviceSize& used = staging ? s.stagingUsed : s.vertexUsed;
    VkDeviceSize& wanted = staging ? s.stagingWanted : s.vertexWanted;
    VkDeviceSize at = (used + 15) & ~(VkDeviceSize)15;
    if (at + bytes > b.size) {
        wanted = (at + bytes) * 2 > wanted ? (at + bytes) * 2 : wanted;
        for (Slot& o : R.slots) {
            VkDeviceSize& w = staging ? o.stagingWanted : o.vertexWanted;
            w = wanted > w ? wanted : w;
        }
        return nullptr;
    }
    used = at + bytes;
    *offset = at;
    return b.map + at;
}

// ---- the Enhanced look's pictures ----------------------------------------------------------

void freeRepImage(RepImage& i) {
    if (i.set != VK_NULL_HANDLE) R.f.vkFreeDescriptorSets(R.dev, R.repPool, 1, &i.set);
    if (i.view != VK_NULL_HANDLE) R.f.vkDestroyImageView(R.dev, i.view, nullptr);
    if (i.image != VK_NULL_HANDLE) R.f.vkDestroyImage(R.dev, i.image, nullptr);
    if (i.memory != VK_NULL_HANDLE) R.f.vkFreeMemory(R.dev, i.memory, nullptr);
    R.imageBytes -= i.bytes <= R.imageBytes ? i.bytes : R.imageBytes;
    i = RepImage();
}

// A descriptor set for an index map and an RGBA image (the stand-ins where none).
VkDescriptorSet repSet(VkImageView index, VkImageView rgba) {
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = R.repPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &R.repLayout;
    if (R.f.vkAllocateDescriptorSets(R.dev, &ai, &set) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkDescriptorImageInfo ii[2] = {{R.sampler, index, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                   {R.sampler, rgba, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet w[2] = {};
    for (int k = 0; k < 2; k++) {
        w[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[k].dstSet = set;
        w[k].dstBinding = (uint32_t)k;
        w[k].descriptorCount = 1;
        w[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[k].pImageInfo = &ii[k];
    }
    R.f.vkUpdateDescriptorSets(R.dev, 2, w, 0, nullptr);
    return set;
}

// pixels (w x h, one byte or four a pixel) into a new image, recorded in cmd.
bool repUpload(VkCommandBuffer cmd, RepImage& i, bool indexed, int w, int h, const uint8_t* pixels) {
    VkDeviceSize bytes = (VkDeviceSize)w * h * (indexed ? 1 : 4), at = 0;
    uint8_t* dst = reserve(true, bytes, &at);
    if (dst == nullptr) return false; // the staging buffer grows for the next frame
    if (!makeImage(&i.image, &i.memory, &i.view, indexed ? VK_FORMAT_R8_UINT : VK_FORMAT_R8G8B8A8_UNORM, w, h,
                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        return false;
    }
    memcpy(dst, pixels, (size_t)bytes);
    barrier(cmd, i.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE,
            VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy c = {};
    c.bufferOffset = at;
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
    R.f.vkCmdCopyBufferToImage(cmd, slot().staging.buffer, i.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    barrier(cmd, i.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT);
    i.bytes = bytes;
    return true;
}

// The stand-ins (one zero texel each) and the set of both, made in the first frame that draws.
bool repDummies(VkCommandBuffer cmd) {
    if (R.dummiesReady) return true;
    static const uint8_t zero[4] = {0, 0, 0, 0};
    if (R.dummyIndex.image == VK_NULL_HANDLE && !repUpload(cmd, R.dummyIndex, true, 1, 1, zero)) return false;
    if (R.dummyRgba.image == VK_NULL_HANDLE && !repUpload(cmd, R.dummyRgba, false, 1, 1, zero)) return false;
    R.noneSet = repSet(R.dummyIndex.view, R.dummyRgba.view);
    R.dummiesReady = R.noneSet != VK_NULL_HANDLE;
    return R.dummiesReady;
}

// The picture `id` on the device (uploaded now if new): its R.images index, -1 when it cannot be yet.
int repImage(VkCommandBuffer cmd, uint64_t id) {
    for (size_t k = 0; k < R.images.size(); k++) {
        if (R.images[k].id == id) {
            R.images[k].used = R.frame;
            return (int)k;
        }
    }
    const XgReplaceImage* src = xgReplaceImage(id);
    if (src == nullptr || src->pw < 1 || src->ph < 1 || src->pw > 4096 || src->ph > 4096) return -1;
    if (R.images.size() >= IMAGE_SETS) return -1;
    RepImage i;
    if (!repUpload(cmd, i, src->indexed != 0, src->pw, src->ph, src->pixels)) {
        freeRepImage(i);
        return -1;
    }
    i.set = src->indexed ? repSet(i.view, R.dummyRgba.view) : repSet(R.dummyIndex.view, i.view);
    if (i.set == VK_NULL_HANDLE) {
        freeRepImage(i);
        return -1;
    }
    i.id = id;
    i.used = R.frame;
    R.imageBytes += i.bytes;
    R.images.push_back(i);
    return (int)R.images.size() - 1;
}

// Past the budget: the pictures no frame in flight draws, least recently drawn first.
void repTrim() {
    while (R.imageBytes > IMAGE_BYTES) {
        size_t victim = R.images.size();
        for (size_t k = 0; k < R.images.size(); k++)
            if (R.images[k].used + SLOTS < R.frame && (victim == R.images.size() || R.images[k].used < R.images[victim].used))
                victim = k;
        if (victim == R.images.size()) return;
        freeRepImage(R.images[victim]);
        R.images.erase(R.images.begin() + (long)victim);
    }
}

} // namespace

// ---- a view -----------------------------------------------------------------------------------

struct View::Impl {
    std::vector<XgPrim> prims;
    std::vector<uint32_t> pixels;
    int pixelsW = 0, pixelsH = 0;
    bool pixelsUploaded = false;
    XgVgpu* gpu = nullptr;
    const uint16_t* vram = nullptr;
    unsigned vramVersion = 0, uploadedVersion = 0;
    uint64_t uploadedGpuIdentity = 0;
    const uint16_t* uploadedVram = nullptr;
    bool uploaded = false, vramZeroed = false;
    float scene[4] = {0, 0, 0, 0};
    bool haveScene = false, fitWanted = true, fitOnChange = true, wholeFit = true;
    ImU32 background = IM_COL32(0, 0, 0, 255);
    bool textures = true, wireframe = false, keepInArea = true;
    bool interactive = true; // its own zoom and pan (setInteractive)
    // The Enhanced look (setReplacements).
    unsigned libraries = 0;
    int poseParts = 0;
    XgReplacer* replacer = nullptr;
    std::vector<XgReplace> reps;
    std::vector<int> repSlots; // per primitive: R.images index, -1 none
    int replacedCount = 0, pendingCount = 0;
    float zoom = 1, cx = 0, cy = 0;
    bool hovered = false;
    ImVec2 origin, size;
    // Vulkan
    VkImage vramImage = VK_NULL_HANDLE;
    VkDeviceMemory vramMemory = VK_NULL_HANDLE;
    VkImageView vramView = VK_NULL_HANDLE;
    VkDescriptorSet vramSet = VK_NULL_HANDLE;
    VkImageLayout vramLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage target = VK_NULL_HANDLE;
    VkDeviceMemory targetMemory = VK_NULL_HANDLE;
    VkImageView targetView = VK_NULL_HANDLE;
    VkDescriptorSet imguiSet = VK_NULL_HANDLE;
    VkImageLayout targetLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    int tw = 0, th = 0;

    void freeTarget() {
        if (target == VK_NULL_HANDLE) {
            return;
        }
        if (imguiSet != VK_NULL_HANDLE && ImGui::GetCurrentContext() != nullptr) {
            ImGui_ImplVulkan_RemoveTexture(imguiSet);
        }
        R.f.vkDestroyImageView(R.dev, targetView, nullptr);
        R.f.vkDestroyImage(R.dev, target, nullptr);
        R.f.vkFreeMemory(R.dev, targetMemory, nullptr);
        target = VK_NULL_HANDLE;
        targetView = VK_NULL_HANDLE;
        targetMemory = VK_NULL_HANDLE;
        imguiSet = VK_NULL_HANDLE;
        tw = th = 0;
        pixelsUploaded = false;
    }

    void freeVulkan() {
        if (!R.ready) {
            return;
        }
        freeTarget();
        if (vramImage != VK_NULL_HANDLE) {
            R.f.vkFreeDescriptorSets(R.dev, R.descriptors, 1, &vramSet);
            R.f.vkDestroyImageView(R.dev, vramView, nullptr);
            R.f.vkDestroyImage(R.dev, vramImage, nullptr);
            R.f.vkFreeMemory(R.dev, vramMemory, nullptr);
            vramImage = VK_NULL_HANDLE;
            vramView = VK_NULL_HANDLE;
            vramMemory = VK_NULL_HANDLE;
            vramSet = VK_NULL_HANDLE;
        }
        uploaded = false;
        pixelsUploaded = false;
        vramLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    bool makeVram() {
        if (vramImage != VK_NULL_HANDLE) {
            return true;
        }
        if (!makeImage(&vramImage, &vramMemory, &vramView, VK_FORMAT_R16_UINT, XG_VRAM_W, XG_VRAM_H,
                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
            return false;
        }
        VkDescriptorSetAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = R.descriptors;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &R.setLayout;
        if (!check(R.f.vkAllocateDescriptorSets(R.dev, &ai, &vramSet), "vkAllocateDescriptorSets")) {
            return false;
        }
        VkDescriptorImageInfo ii = {R.sampler, vramView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w = {};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = vramSet;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &ii;
        R.f.vkUpdateDescriptorSets(R.dev, 1, &w, 0, nullptr);
        return true;
    }

    bool makeTarget(int w, int h) {
        if (target != VK_NULL_HANDLE && tw == w && th == h) {
            return true;
        }
        if (target != VK_NULL_HANDLE) {
            R.f.vkDeviceWaitIdle(R.dev); // the present pass may still read the old one
            freeTarget();
        }
        if (!makeImage(&target, &targetMemory, &targetView, TARGET_FORMAT, w, h,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
            return false;
        }
        imguiSet = ImGui_ImplVulkan_AddTexture(targetView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        targetLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        tw = w;
        th = h;
        return imguiSet != VK_NULL_HANDLE;
    }

    // VRAM into the image when it changed (zeros when there is none).
    void upload(VkCommandBuffer cmd) {
        const uint16_t* src = gpu != nullptr ? xgVgpuVram(gpu) : vram;
        unsigned version = gpu != nullptr ? xgVgpuVramVersion(gpu) : vramVersion;
        uint64_t identity = gpu != nullptr ? xgVgpuIdentity(gpu) : 0;
        if (uploaded && (src == nullptr ? vramZeroed : src == uploadedVram && identity == uploadedGpuIdentity &&
                         version == uploadedVersion && !vramZeroed)) {
            return;
        }
        VkDeviceSize at;
        uint8_t* p = reserve(true, VRAM_BYTES, &at);
        if (p == nullptr) {
            return;
        }
        if (src != nullptr) {
            memcpy(p, src, VRAM_BYTES);
        } else {
            memset(p, 0, VRAM_BYTES);
        }
        barrier(cmd, vramImage, vramLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy c = {};
        c.bufferOffset = at;
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {XG_VRAM_W, XG_VRAM_H, 1};
        R.f.vkCmdCopyBufferToImage(cmd, slot().staging.buffer, vramImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        barrier(cmd, vramImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        vramLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        uploaded = true;
        vramZeroed = src == nullptr;
        uploadedVersion = version;
        uploadedGpuIdentity = identity;
        uploadedVram = src;
    }

    const XgPrim* primList(int* count) const {
        if (gpu != nullptr) {
            *count = xgVgpuPrimCount(gpu);
            return xgVgpuPrims(gpu);
        }
        *count = (int)prims.size();
        return prims.data();
    }

    void bounds(float* b) const {
        int n;
        const XgPrim* list = primList(&n);
        b[0] = b[1] = 1e9f;
        b[2] = b[3] = -1e9f;
        for (int i = 0; i < n; i++) {
            for (int k = 0; k < list[i].count; k++) {
                b[0] = fminf(b[0], list[i].x[k]);
                b[1] = fminf(b[1], list[i].y[k]);
                b[2] = fmaxf(b[2], list[i].x[k]);
                b[3] = fmaxf(b[3], list[i].y[k]);
            }
        }
        if (b[0] > b[2]) {
            b[0] = b[1] = 0;
            b[2] = 320;
            b[3] = 240;
        }
    }

    void fitNow() {
        float b[4];
        if (haveScene) {
            memcpy(b, scene, sizeof(b));
        } else {
            bounds(b);
        }
        float w = fmaxf(b[2] - b[0], 1), h = fmaxf(b[3] - b[1], 1);
        zoom = fminf(size.x / w, size.y / h);
        // Whole window pixels per scene pixel when the scene fits at 2 or
        // more (between 1 and 2 the space it would leave is too much: a
        // focused view's picture fills it).
        if (zoom >= 2 && wholeFit) {
            zoom = floorf(zoom);
        }
        cx = (b[0] + b[2]) * 0.5f;
        cy = (b[1] + b[3]) * 0.5f;
        fitWanted = false;
    }
};

namespace {

void addVertex(std::vector<Vtx>& out, const XgPrim& p, int k, uint32_t mode, const uint16_t* tex) {
    Vtx v = {};
    v.x = p.x[k];
    v.y = p.y[k];
    v.u = p.u[k];
    v.v = p.v[k];
    int c = (p.flags & XG_PRIM_GOURAUD) ? k : 0;
    v.rgba[0] = p.r[c];
    v.rgba[1] = p.g[c];
    v.rgba[2] = p.b[c];
    v.rgba[3] = 255;
    memcpy(v.tex, tex, sizeof(v.tex));
    v.mode = mode;
    memcpy(v.area, p.area, sizeof(v.area));
    out.push_back(v);
}

// A line from (x0, y0) to (x1, y1), `width` scene units across, as two triangles.
void addLine(std::vector<Vtx>& out, float x0, float y0, float x1, float y1, float width, const uint8_t* rgba,
             uint32_t mode, const int16_t* area) {
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    float nx = len > 0 ? -dy / len * width * 0.5f : 0, ny = len > 0 ? dx / len * width * 0.5f : width * 0.5f;
    float px[4] = {x0 + nx, x0 - nx, x1 + nx, x1 - nx}, py[4] = {y0 + ny, y0 - ny, y1 + ny, y1 - ny};
    static const int order[6] = {0, 1, 2, 1, 2, 3};
    for (int i : order) {
        Vtx v = {};
        v.x = px[i];
        v.y = py[i];
        memcpy(v.rgba, rgba, 4);
        v.mode = mode;
        memcpy(v.area, area, sizeof(v.area));
        out.push_back(v);
    }
}

void addBatch(std::vector<Batch>& batches, int pipe, uint32_t pass, uint32_t first, uint32_t count, int image = -1) {
    if (!batches.empty()) {
        Batch& b = batches.back();
        if (b.pipe == pipe && b.pass == pass && b.first + b.count == first && b.image == image) {
            b.count += count;
            return;
        }
    }
    batches.push_back({pipe, pass, first, count, image});
}

// The primitives as triangles, in drawing order, and the draws they need:
// semi-transparent ones twice over the same vertices (opaque texels, then
// the blended ones).
void build(const View::Impl& v, const XgPrim* prims, int n, std::vector<Vtx>& out, std::vector<Batch>& batches) {
    static const int tri[3] = {0, 1, 2}, quad[6] = {0, 1, 2, 1, 2, 3};
    bool replacing = v.libraries != 0 && v.reps.size() == (size_t)n && v.repSlots.size() == (size_t)n;
    for (int i = 0; i < n; i++) {
        const XgPrim* source = &prims[i];
        const XgReplace* r = replacing ? &v.reps[(size_t)i] : nullptr;
        int image = replacing ? v.repSlots[(size_t)i] : -1;
        if (r != nullptr && r->kind == XG_REPLACE_SKIP) continue; // a part its pose stands for
        XgPrim posed;
        if (r != nullptr && r->pose && image >= 0) {
            // The actor's pose: one quad through its reference part's frame, u, v over the picture.
            posed = *source;
            posed.count = 4;
            posed.clut = r->clut;
            for (int k = 0; k < 4; k++) {
                posed.x[k] = (int16_t)lroundf(r->x[k]);
                posed.y[k] = (int16_t)lroundf(r->y[k]);
                posed.u[k] = (int16_t)(k & 1 ? r->w : 0);
                posed.v[k] = (int16_t)(k & 2 ? r->h : 0);
            }
            posed.flags |= XG_PRIM_FREE_UV;
            source = &posed;
        } else if (r != nullptr && r->pose) {
            image = -1; // not on the device yet: the parts as they are
        }
        const XgPrim& p = *source;
        if (r != nullptr && (r->kind == XG_REPLACE_NONE || image < 0)) image = -1;
        uint32_t first = (uint32_t)out.size();
        uint32_t mode = 0;
        uint16_t tex[4] = {0, 0, 0, 0};
        if (p.flags & XG_PRIM_TEXTURED) {
            mode |= MODE_TEXTURED | (p.tpage >> 7 & 3);
            mode |= p.flags & XG_PRIM_RAW ? MODE_RAW : 0;
            mode |= p.flags & XG_PRIM_FREE_UV ? MODE_FREE_UV : 0;
            mode |= (p.window & 0xFFFFF) << 8;
            tex[0] = (uint16_t)((p.tpage & 15) * 64);
            tex[1] = (uint16_t)((p.tpage >> 4 & 1) * 256);
            tex[2] = (uint16_t)((p.clut & 63) * 16);
            tex[3] = (uint16_t)(p.clut >> 6 & 511);
        }
        bool semi = (p.flags & XG_PRIM_SEMI) != 0;
        if (semi) {
            mode |= MODE_SEMI | ((uint32_t)(p.tpage >> 5 & 3) << 5);
        }
        if (p.flags & XG_PRIM_LINE) {
            uint8_t rgba[4] = {p.r[0], p.g[0], p.b[0], 255};
            addLine(out, p.x[0] + 0.5f, p.y[0] + 0.5f, p.x[1] + 0.5f, p.y[1] + 0.5f, 1.0f, rgba, mode, p.area);
        } else if (p.count == 3 || p.count == 4) {
            const int* order = p.count == 3 ? tri : quad;
            int count = p.count == 3 ? 3 : 6;
            for (int k = 0; k < count; k++) {
                addVertex(out, p, order[k], mode, tex);
                if (image >= 0) {
                    Vtx& vx = out.back();
                    if (r->pose) {
                        // the corners exactly (addVertex took the rounded ones)
                        vx.x = r->x[order[k]];
                        vx.y = r->y[order[k]];
                    }
                    vx.rep[0] = r->u;
                    vx.rep[1] = r->v;
                    vx.rep[2] = r->scale;
                    vx.rep[3] = r->kind == XG_REPLACE_INDEXED ? 1 : 2;
                }
            }
            if (p.flags & XG_PRIM_FILL) {
                for (uint32_t k = first; k < out.size(); k++) {
                    out[k].area[0] = out[k].area[1] = 0;
                    out[k].area[2] = XG_VRAM_W - 1;
                    out[k].area[3] = XG_VRAM_H - 1;
                }
            }
        } else {
            continue;
        }
        uint32_t count = (uint32_t)out.size() - first;
        if (!semi) {
            addBatch(batches, 0, 0, first, count, image);
            continue;
        }
        if ((p.flags & XG_PRIM_TEXTURED) || !v.textures) {
            addBatch(batches, 0, 0, first, count, image);
        }
        addBatch(batches, 1 + (p.tpage >> 5 & 3), 1, first, count, image);
    }
    if (v.wireframe) {
        // The polygons' edges, one window pixel wide, over everything.
        static const uint8_t white[4] = {230, 230, 240, 255};
        static const int16_t all[4] = {-32768, -32768, 32767, 32767};
        uint32_t first = (uint32_t)out.size();
        float w = 1.0f / fmaxf(v.zoom, 0.01f);
        for (int i = 0; i < n; i++) {
            const XgPrim& p = prims[i];
            if ((p.flags & (XG_PRIM_LINE | XG_PRIM_FILL)) || (p.count != 3 && p.count != 4)) {
                continue;
            }
            static const int edges3[6] = {0, 1, 1, 2, 2, 0}, edges4[8] = {0, 1, 1, 3, 3, 2, 2, 0};
            const int* e = p.count == 3 ? edges3 : edges4;
            for (int k = 0; k < p.count; k++) {
                addLine(out, p.x[e[2 * k]], p.y[e[2 * k]], p.x[e[2 * k + 1]], p.y[e[2 * k + 1]], w, white, 0, all);
            }
        }
        if (out.size() > first) {
            addBatch(batches, 0, 0, first, (uint32_t)out.size() - first);
        }
    }
}

} // namespace

View::View() : p(new Impl) {
    R.views.push_back(p);
}

View::~View() {
    for (size_t i = 0; i < R.views.size(); i++) {
        if (R.views[i] == p) {
            R.views.erase(R.views.begin() + (long)i);
            break;
        }
    }
    if (R.ready && (p->target != VK_NULL_HANDLE || p->vramImage != VK_NULL_HANDLE)) {
        R.f.vkDeviceWaitIdle(R.dev);
        p->freeVulkan();
    }
    xgReplacerDestroy(p->replacer);
    delete p;
}

void View::setPrims(const XgPrim* prims, int count) {
    p->pixels.clear();
    p->gpu = nullptr;
    p->prims.assign(prims, prims + (count > 0 ? count : 0));
}

void View::setVram(const uint16_t* vram, unsigned version) {
    p->gpu = nullptr;
    p->vram = vram;
    p->vramVersion = version;
}

void View::show(XgVgpu* gpu) {
    p->pixels.clear();
    p->gpu = gpu;
    p->prims.clear();
}

void View::setPixels(const std::vector<uint32_t>& rgba, int w, int h) {
    if (w < 1 || h < 1 || w > 1024 || h > 2048 || rgba.size() != (size_t)w * h) return;
    p->pixels = rgba; p->pixelsW = w; p->pixelsH = h; p->pixelsUploaded = false;
}

bool View::thumbnail(std::vector<uint32_t>& rgba) const {
    int n = 0; const XgPrim* list = p->primList(&n);
    if (n <= 0 || p->size.x <= 0 || p->size.y <= 0 || !std::isfinite(p->zoom) || p->zoom <= 0) return false;
    float halfW = p->size.x / p->zoom * 0.5f, halfH = p->size.y / p->zoom * 0.5f;
    // Keep the viewport's shape, with extra background at the sides.
    halfW = fmaxf(halfW, halfH * bookmarkWidth / bookmarkHeight);
    halfH = fmaxf(halfH, halfW * bookmarkHeight / bookmarkWidth);
    rgba.resize(bookmarkWidth * bookmarkHeight);
    const uint16_t* vram = p->gpu ? xgVgpuVram(p->gpu) : p->vram;
    std::vector<XgPrim> unclipped;
    if (!p->keepInArea) {
        unclipped.assign(list, list + n);
        for (auto& prim : unclipped) { prim.area[0] = prim.area[1] = -32768; prim.area[2] = prim.area[3] = 32767; }
        list = unclipped.data();
    }
    xgRasterize(list, n, vram, p->cx - halfW, p->cy - halfH, p->cx + halfW, p->cy + halfH,
                bookmarkWidth, bookmarkHeight, p->background, p->textures, rgba.data());
    return true;
}

void View::setScene(float x0, float y0, float x1, float y1) {
    float s[4] = {x0, y0, x1, y1};
    if (!p->haveScene || memcmp(s, p->scene, sizeof(s)) != 0) {
        memcpy(p->scene, s, sizeof(s));
        p->fitWanted |= p->fitOnChange || !p->haveScene;
        p->haveScene = true;
    }
}

void View::setBackground(ImU32 colour) {
    p->background = colour;
}

void View::setTextures(bool on) {
    p->textures = on;
}

void View::setWireframe(bool on) {
    p->wireframe = on;
}

void View::setKeepInArea(bool on) {
    p->keepInArea = on;
}

void View::setFitOnChange(bool on) {
    p->fitOnChange = on;
}

void View::setWholeFit(bool on) {
    p->wholeFit = on;
}

void View::fit() {
    p->fitWanted = true;
}

void View::setInteractive(bool on) {
    p->interactive = on;
}

void View::setReplacements(unsigned libraries, int poseParts) {
    p->libraries = libraries;
    p->poseParts = poseParts;
    if (libraries == 0) {
        p->replacedCount = p->pendingCount = 0;
    }
}

int View::replaced() const {
    return p->libraries != 0 ? p->replacedCount : 0;
}

int View::replacing() const {
    return p->libraries != 0 ? p->pendingCount : 0;
}

void View::setZoom(float z) {
    p->zoom = fmaxf(0.01f, fminf(z, 256.0f));
}

float View::zoom() const {
    return p->zoom;
}

bool View::hovered() const {
    return p->hovered;
}

ImVec2 View::toScene(ImVec2 s) const {
    return ImVec2((s.x - p->origin.x - p->size.x * 0.5f) / p->zoom + p->cx,
                  (s.y - p->origin.y - p->size.y * 0.5f) / p->zoom + p->cy);
}

void View::draw(ImVec2 size) {
    Impl& v = *p;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    size.x = size.x > 0 ? size.x : avail.x;
    size.y = size.y > 0 ? size.y : avail.y;
    size.x = floorf(fmaxf(size.x, 8));
    size.y = floorf(fmaxf(size.y, 8));
    if (!v.pixels.empty()) {
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(size);
        if (!start() || !R.frameOpen || !v.makeTarget(v.pixelsW, v.pixelsH)) return;
        if (!v.pixelsUploaded) {
            VkDeviceSize offset = 0;
            uint8_t* dst = reserve(true, v.pixels.size() * 4, &offset);
            if (!dst) return;
            // The cache words are RGBA in little-endian channel order.
            for (size_t i = 0; i < v.pixels.size(); ++i)
                for (int c = 0; c < 4; ++c) dst[i * 4 + c] = (uint8_t)(v.pixels[i] >> (c * 8));
            VkCommandBuffer cmd = slot().cmd;
            barrier(cmd, v.target, v.targetLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            VkBufferImageCopy copy = {};
            copy.bufferOffset = offset; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {(uint32_t)v.pixelsW, (uint32_t)v.pixelsH, 1};
            R.f.vkCmdCopyBufferToImage(cmd, slot().staging.buffer, v.target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            barrier(cmd, v.target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT);
            v.targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; v.pixelsUploaded = true;
        }
        ImGui::GetWindowDrawList()->AddImage(ImTextureRef((ImTextureID)(uintptr_t)v.imguiSet), origin,
                                           ImVec2(origin.x + size.x, origin.y + size.y));
        return;
    }
    v.origin = ImGui::GetCursorScreenPos();
    v.origin.x = floorf(v.origin.x);
    v.origin.y = floorf(v.origin.y);
    bool resized = size.x != v.size.x || size.y != v.size.y;
    v.size = size;

    // Input: the view is one item that takes the mouse; the wheel is its own.
    ImGui::PushID(this);
    ImGui::InvisibleButton("view", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                             ImGuiButtonFlags_MouseButtonMiddle);
    ImGui::PopID();
    v.hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    if (v.fitWanted || (resized && v.fitOnChange)) {
        v.fitNow();
    }
    auto zoomAt = [&](ImVec2 at, float factor) {
        ImVec2 before = toScene(at);
        v.zoom = fmaxf(0.01f, fminf(v.zoom * factor, 256.0f));
        ImVec2 after = toScene(at);
        v.cx += before.x - after.x;
        v.cy += before.y - after.y;
    };
    if (v.hovered && v.interactive) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        if (io.MouseWheel != 0) {
            zoomAt(io.MousePos, powf(1.2f, io.MouseWheel));
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            v.fitNow();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
            zoomAt(ImVec2(v.origin.x + size.x / 2, v.origin.y + size.y / 2), 1.25f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
            zoomAt(ImVec2(v.origin.x + size.x / 2, v.origin.y + size.y / 2), 0.8f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_0)) {
            v.fitNow();
        }
    }
    if (v.interactive && ImGui::IsItemActive() && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
        v.cx -= io.MouseDelta.x / v.zoom;
        v.cy -= io.MouseDelta.y / v.zoom;
    }
    // The gamepad: the right stick pans, the triggers zoom.
    float sx = ImGui::GetKeyData(ImGuiKey_GamepadRStickRight)->AnalogValue -
               ImGui::GetKeyData(ImGuiKey_GamepadRStickLeft)->AnalogValue;
    float sy = ImGui::GetKeyData(ImGuiKey_GamepadRStickDown)->AnalogValue -
               ImGui::GetKeyData(ImGuiKey_GamepadRStickUp)->AnalogValue;
    float zt = ImGui::GetKeyData(ImGuiKey_GamepadR2)->AnalogValue - ImGui::GetKeyData(ImGuiKey_GamepadL2)->AnalogValue;
    if (!strcmp(visualFamily(), "sprites")) sx = sy = 0;
    if (!v.interactive || !viewerShortcuts()) {
        sx = sy = zt = 0;
    }
    if (fabsf(sx) > 0.2f || fabsf(sy) > 0.2f) {
        v.cx += sx * 8.0f * unit() / v.zoom;
        v.cy += sy * 8.0f * unit() / v.zoom;
    }
    if (fabsf(zt) > 0.2f) {
        zoomAt(ImVec2(v.origin.x + size.x / 2, v.origin.y + size.y / 2), 1.0f + zt * 0.04f);
    }

    // The picture, drawn now into this frame's command buffer.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 end(v.origin.x + size.x, v.origin.y + size.y);
    if (!start() || !R.frameOpen) {
        dl->AddRectFilled(v.origin, end, v.background);
        return;
    }
    int w = (int)size.x, h = (int)size.y;
    if (!v.makeVram() || !v.makeTarget(w, h)) {
        dl->AddRectFilled(v.origin, end, v.background);
        return;
    }
    VkCommandBuffer cmd = slot().cmd;
    v.upload(cmd);
    int n;
    const XgPrim* prims = v.primList(&n);
    v.replacedCount = v.pendingCount = 0;
    bool repReady = repDummies(cmd); // set 1 is bound for every draw, the stand-ins where nothing is replaced
    if (v.libraries != 0 && n > 0 && repReady) {
        if (v.replacer == nullptr) v.replacer = xgReplacerCreate();
        const uint16_t* src = v.gpu != nullptr ? xgVgpuVram(v.gpu) : v.vram;
        unsigned version = v.gpu != nullptr ? xgVgpuVramVersion(v.gpu) : v.vramVersion;
        uint64_t identity = v.gpu != nullptr ? xgVgpuIdentity(v.gpu) : (uint64_t)(uintptr_t)v.vram;
        v.reps.resize((size_t)n);
        v.repSlots.assign((size_t)n, -1);
        v.replacedCount = xgReplacePrims(v.replacer, v.libraries, v.gpu, src, version, identity, prims, n, v.poseParts,
                                         v.reps.data(), &v.pendingCount);
        for (int i = 0; i < n; i++) {
            const XgReplace& r = v.reps[(size_t)i];
            if (r.kind != XG_REPLACE_INDEXED && r.kind != XG_REPLACE_RGBA) continue;
            v.repSlots[(size_t)i] = repImage(cmd, r.id);
            if (v.repSlots[(size_t)i] < 0) v.pendingCount++;
        }
    } else {
        v.reps.clear();
        v.repSlots.clear();
    }
    std::vector<Vtx> verts;
    std::vector<Batch> batches;
    verts.reserve((size_t)n * 6);
    build(v, prims, n, verts, batches);
    VkDeviceSize at = 0;
    uint8_t* dst = verts.empty() ? nullptr : reserve(false, verts.size() * sizeof(Vtx), &at);
    if (dst != nullptr) {
        memcpy(dst, verts.data(), verts.size() * sizeof(Vtx));
    }

    barrier(cmd, v.target, v.targetLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo att = {};
    att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    att.imageView = v.targetView;
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ImVec4 bg = ImGui::ColorConvertU32ToFloat4(v.background);
    att.clearValue.color = {{bg.x, bg.y, bg.z, 1.0f}};
    VkRenderingInfo ri = {};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea.extent = {(uint32_t)w, (uint32_t)h};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    R.f.vkCmdBeginRendering(cmd, &ri);
    if (dst != nullptr && repReady) {
        VkViewport viewport = {0, 0, (float)w, (float)h, 0, 1};
        VkRect2D scissor = {{0, 0}, {(uint32_t)w, (uint32_t)h}};
        R.f.vkCmdSetViewport(cmd, 0, 1, &viewport);
        R.f.vkCmdSetScissor(cmd, 0, 1, &scissor);
        R.f.vkCmdBindVertexBuffers(cmd, 0, 1, &slot().vertices.buffer, &at);
        R.f.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, R.layout, 0, 1, &v.vramSet, 0, nullptr);
        struct {
            float scale[2], offset[2];
            uint32_t flags, pass;
        } push;
        push.scale[0] = 2.0f * v.zoom / (float)w;
        push.scale[1] = 2.0f * v.zoom / (float)h;
        push.offset[0] = -v.cx * push.scale[0];
        push.offset[1] = -v.cy * push.scale[1];
        push.flags = (v.keepInArea ? FLAG_AREA : 0) | (v.textures ? FLAG_TEXTURES : 0);
        int pipe = -1, image = -2;
        for (const Batch& b : batches) {
            if (b.pipe != pipe) {
                R.f.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, R.pipes[b.pipe]);
                pipe = b.pipe;
            }
            if (b.image != image && R.dummiesReady) {
                VkDescriptorSet set = b.image >= 0 ? R.images[(size_t)b.image].set : R.noneSet;
                R.f.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, R.layout, 1, 1, &set, 0, nullptr);
                image = b.image;
            }
            push.pass = b.pass;
            R.f.vkCmdPushConstants(cmd, R.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(push), &push);
            R.f.vkCmdDraw(cmd, b.count, 1, b.first, 0);
        }
    }
    R.f.vkCmdEndRendering(cmd);
    barrier(cmd, v.target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    v.targetLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dl->AddImage(ImTextureRef((ImTextureID)(uintptr_t)v.imguiSet), v.origin, end);
}

// ---- the shell's calls ------------------------------------------------------------------------

void viewsBeginFrame() {
    if (!start()) {
        return;
    }
    R.slot = (R.slot + 1) % SLOTS;
    Slot& s = slot();
    if (s.submitted) {
        R.f.vkWaitForFences(R.dev, 1, &s.fence, VK_TRUE, UINT64_MAX);
        R.f.vkResetFences(R.dev, 1, &s.fence);
        s.submitted = false;
    }
    R.frame++;
    repTrim();
    // Buffers that were too small last time grow now (nothing uses them).
    if (s.vertexWanted > s.vertices.size) {
        freeBuffer(&s.vertices);
        makeBuffer(&s.vertices, s.vertexWanted, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
    if (s.stagingWanted > s.staging.size) {
        freeBuffer(&s.staging);
        makeBuffer(&s.staging, s.stagingWanted, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    }
    s.vertexUsed = s.stagingUsed = 0;
    R.f.vkResetCommandBuffer(s.cmd, 0);
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    R.frameOpen = s.vertices.buffer != VK_NULL_HANDLE && s.staging.buffer != VK_NULL_HANDLE &&
                  check(R.f.vkBeginCommandBuffer(s.cmd, &bi), "vkBeginCommandBuffer");
}

void viewsEndFrame() {
    if (!R.ready || !R.frameOpen) {
        return;
    }
    R.frameOpen = false;
    Slot& s = slot();
    if (!check(R.f.vkEndCommandBuffer(s.cmd), "vkEndCommandBuffer")) {
        return;
    }
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &s.cmd;
    s.submitted = check(R.f.vkQueueSubmit(R.queue, 1, &si, s.fence), "vkQueueSubmit");
}

void viewsShutdown() {
    if (!R.ready) {
        R = Renderer();
        return;
    }
    if (R.frameOpen) {
        R.f.vkEndCommandBuffer(slot().cmd);
        R.frameOpen = false;
    }
    R.f.vkDeviceWaitIdle(R.dev);
    for (View::Impl* v : R.views) {
        v->freeVulkan();
    }
    for (RepImage& i : R.images) freeRepImage(i);
    R.images.clear();
    freeRepImage(R.dummyIndex);
    freeRepImage(R.dummyRgba);
    if (R.noneSet != VK_NULL_HANDLE) R.f.vkFreeDescriptorSets(R.dev, R.repPool, 1, &R.noneSet);
    R.f.vkDestroyDescriptorPool(R.dev, R.repPool, nullptr);
    R.f.vkDestroyDescriptorSetLayout(R.dev, R.repLayout, nullptr);
    for (Slot& s : R.slots) {
        freeBuffer(&s.vertices);
        freeBuffer(&s.staging);
        R.f.vkDestroyFence(R.dev, s.fence, nullptr);
    }
    for (VkPipeline pipe : R.pipes) {
        R.f.vkDestroyPipeline(R.dev, pipe, nullptr);
    }
    R.f.vkDestroySampler(R.dev, R.sampler, nullptr);
    R.f.vkDestroyDescriptorPool(R.dev, R.descriptors, nullptr);
    R.f.vkDestroyPipelineLayout(R.dev, R.layout, nullptr);
    R.f.vkDestroyDescriptorSetLayout(R.dev, R.setLayout, nullptr);
    R.f.vkDestroyCommandPool(R.dev, R.pool, nullptr);
    std::vector<View::Impl*> views = R.views;
    R = Renderer();
    R.views = views; // views outlive the device: they free nothing more
}

} // namespace extras
