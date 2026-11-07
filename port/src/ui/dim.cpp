// The darkened game picture behind the settings menu (dim.h), as the
// game's own menu darkens it: a full-screen grey of 0x80 drawn subtractively
// (initPrimitives_348_cursor: POLY_F4 0x80 with a B-F draw mode), so the
// picture loses 128 of 255 in each channel and its darker half goes black.
// ImGui's pipeline only blends by alpha, so this is a pipeline of its own,
// recorded into the present pass before ImGui's.
//
// Its two shaders, compiled with glslc --target-env=vulkan1.3 -O -mfmt=num:
//
// dim.vert
//   #version 450
//   // One triangle over the whole target.
//   void main() {
//       vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
//       gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
//   }
//
// dim.frag
//   #version 450
//   // The colour taken off the picture (the pipeline's blend subtracts it).
//   layout(push_constant) uniform Dim {
//       vec4 color;
//   } pc;
//   layout(location = 0) out vec4 outColor;
//   void main() {
//       outColor = pc.color;
//   }

#include "dim.h"

#include <stdio.h>

namespace dim {

namespace {

const uint32_t vertCode[] = {
    0x07230203, 0x00010600, 0x000d000b, 0x0000002c, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000000,
    0x00000004, 0x6e69616d, 0x00000000, 0x0000000c, 0x0000001d, 0x00040047,
    0x0000000c, 0x0000000b, 0x0000002a, 0x00030047, 0x0000001b, 0x00000002,
    0x00050048, 0x0000001b, 0x00000000, 0x0000000b, 0x00000000, 0x00050048,
    0x0000001b, 0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x0000001b,
    0x00000002, 0x0000000b, 0x00000003, 0x00050048, 0x0000001b, 0x00000003,
    0x0000000b, 0x00000004, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
    0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007,
    0x00000006, 0x00000002, 0x00040015, 0x0000000a, 0x00000020, 0x00000001,
    0x00040020, 0x0000000b, 0x00000001, 0x0000000a, 0x0004003b, 0x0000000b,
    0x0000000c, 0x00000001, 0x0004002b, 0x0000000a, 0x0000000e, 0x00000001,
    0x0004002b, 0x0000000a, 0x00000010, 0x00000002, 0x00040017, 0x00000017,
    0x00000006, 0x00000004, 0x00040015, 0x00000018, 0x00000020, 0x00000000,
    0x0004002b, 0x00000018, 0x00000019, 0x00000001, 0x0004001c, 0x0000001a,
    0x00000006, 0x00000019, 0x0006001e, 0x0000001b, 0x00000017, 0x00000006,
    0x0000001a, 0x0000001a, 0x00040020, 0x0000001c, 0x00000003, 0x0000001b,
    0x0004003b, 0x0000001c, 0x0000001d, 0x00000003, 0x0004002b, 0x0000000a,
    0x0000001e, 0x00000000, 0x0004002b, 0x00000006, 0x00000020, 0x40000000,
    0x0004002b, 0x00000006, 0x00000022, 0x3f800000, 0x0004002b, 0x00000006,
    0x00000025, 0x00000000, 0x00040020, 0x00000029, 0x00000003, 0x00000017,
    0x0005002c, 0x00000007, 0x0000002b, 0x00000022, 0x00000022, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
    0x0004003d, 0x0000000a, 0x0000000d, 0x0000000c, 0x000500c4, 0x0000000a,
    0x0000000f, 0x0000000d, 0x0000000e, 0x000500c7, 0x0000000a, 0x00000011,
    0x0000000f, 0x00000010, 0x0004006f, 0x00000006, 0x00000012, 0x00000011,
    0x000500c7, 0x0000000a, 0x00000014, 0x0000000d, 0x00000010, 0x0004006f,
    0x00000006, 0x00000015, 0x00000014, 0x00050050, 0x00000007, 0x00000016,
    0x00000012, 0x00000015, 0x0005008e, 0x00000007, 0x00000021, 0x00000016,
    0x00000020, 0x00050083, 0x00000007, 0x00000024, 0x00000021, 0x0000002b,
    0x00050051, 0x00000006, 0x00000026, 0x00000024, 0x00000000, 0x00050051,
    0x00000006, 0x00000027, 0x00000024, 0x00000001, 0x00070050, 0x00000017,
    0x00000028, 0x00000026, 0x00000027, 0x00000025, 0x00000022, 0x00050041,
    0x00000029, 0x0000002a, 0x0000001d, 0x0000001e, 0x0003003e, 0x0000002a,
    0x00000028, 0x000100fd, 0x00010038,
};

const uint32_t fragCode[] = {
    0x07230203, 0x00010600, 0x000d000b, 0x00000012, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000004,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000c, 0x00030010,
    0x00000004, 0x00000007, 0x00040047, 0x00000009, 0x0000001e, 0x00000000,
    0x00030047, 0x0000000a, 0x00000002, 0x00050048, 0x0000000a, 0x00000000,
    0x00000023, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
    0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007,
    0x00000006, 0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007,
    0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x0003001e, 0x0000000a,
    0x00000007, 0x00040020, 0x0000000b, 0x00000009, 0x0000000a, 0x0004003b,
    0x0000000b, 0x0000000c, 0x00000009, 0x00040015, 0x0000000d, 0x00000020,
    0x00000001, 0x0004002b, 0x0000000d, 0x0000000e, 0x00000000, 0x00040020,
    0x0000000f, 0x00000009, 0x00000007, 0x00050036, 0x00000002, 0x00000004,
    0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x00050041, 0x0000000f,
    0x00000010, 0x0000000c, 0x0000000e, 0x0004003d, 0x00000007, 0x00000011,
    0x00000010, 0x0003003e, 0x00000009, 0x00000011, 0x000100fd, 0x00010038,
};

struct State {
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkCreateShaderModule createShaderModule;
    PFN_vkDestroyShaderModule destroyShaderModule;
    PFN_vkCreatePipelineLayout createPipelineLayout;
    PFN_vkDestroyPipelineLayout destroyPipelineLayout;
    PFN_vkCreateGraphicsPipelines createGraphicsPipelines;
    PFN_vkDestroyPipeline destroyPipeline;
    PFN_vkDeviceWaitIdle deviceWaitIdle;
    PFN_vkCmdBindPipeline cmdBindPipeline;
    PFN_vkCmdSetViewport cmdSetViewport;
    PFN_vkCmdSetScissor cmdSetScissor;
    PFN_vkCmdPushConstants cmdPushConstants;
    PFN_vkCmdDraw cmdDraw;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool broken;
};

State st;

VkShaderModule module(const uint32_t* code, size_t size) {
    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    return st.createShaderModule(st.device, &info, nullptr, &m) == VK_SUCCESS ? m : VK_NULL_HANDLE;
}

// The pipeline for targets of `format`: dst - src on colour, dst's alpha kept.
bool build(VkFormat format) {
    if (st.pipeline != VK_NULL_HANDLE) {
        st.deviceWaitIdle(st.device);
        st.destroyPipeline(st.device, st.pipeline, nullptr);
        st.pipeline = VK_NULL_HANDLE;
    }
    if (st.layout == VK_NULL_HANDLE) {
        VkPushConstantRange range = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
        VkPipelineLayoutCreateInfo li = {};
        li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &range;
        if (st.createPipelineLayout(st.device, &li, nullptr, &st.layout) != VK_SUCCESS) {
            return false;
        }
    }
    VkShaderModule vert = module(vertCode, sizeof(vertCode)), frag = module(fragCode, sizeof(fragCode));
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend = {};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri = {};
    ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &format;
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
    pi.layout = st.layout;
    VkResult r = vert != VK_NULL_HANDLE && frag != VK_NULL_HANDLE
                     ? st.createGraphicsPipelines(st.device, VK_NULL_HANDLE, 1, &pi, nullptr, &st.pipeline)
                     : VK_ERROR_INITIALIZATION_FAILED;
    if (vert != VK_NULL_HANDLE) {
        st.destroyShaderModule(st.device, vert, nullptr);
    }
    if (frag != VK_NULL_HANDLE) {
        st.destroyShaderModule(st.device, frag, nullptr);
    }
    st.format = format;
    return r == VK_SUCCESS;
}

} // namespace

void init(VkDevice device, PFN_vkVoidFunction (*load)(const char* name, void* user), void* user) {
    st.device = device;
#define LOAD(name, fn) st.name = (PFN_##fn)load(#fn, user)
    LOAD(createShaderModule, vkCreateShaderModule);
    LOAD(destroyShaderModule, vkDestroyShaderModule);
    LOAD(createPipelineLayout, vkCreatePipelineLayout);
    LOAD(destroyPipelineLayout, vkDestroyPipelineLayout);
    LOAD(createGraphicsPipelines, vkCreateGraphicsPipelines);
    LOAD(destroyPipeline, vkDestroyPipeline);
    LOAD(deviceWaitIdle, vkDeviceWaitIdle);
    LOAD(cmdBindPipeline, vkCmdBindPipeline);
    LOAD(cmdSetViewport, vkCmdSetViewport);
    LOAD(cmdSetScissor, vkCmdSetScissor);
    LOAD(cmdPushConstants, vkCmdPushConstants);
    LOAD(cmdDraw, vkCmdDraw);
#undef LOAD
    st.broken = st.createShaderModule == nullptr || st.createGraphicsPipelines == nullptr || st.cmdDraw == nullptr;
}

void record(VkCommandBuffer cmd, int w, int h, VkFormat format, float amount) {
    if (st.broken || st.device == VK_NULL_HANDLE) {
        return;
    }
    if (format != st.format && !build(format)) {
        fprintf(stderr, "ui: no pipeline for the darkened picture\n");
        st.broken = true;
        return;
    }
    VkViewport viewport = {0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {(uint32_t)w, (uint32_t)h}};
    float color[4] = {amount, amount, amount, 0.0f};
    st.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, st.pipeline);
    st.cmdSetViewport(cmd, 0, 1, &viewport);
    st.cmdSetScissor(cmd, 0, 1, &scissor);
    st.cmdPushConstants(cmd, st.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(color), color);
    st.cmdDraw(cmd, 3, 1, 0, 0);
}

void shutdown() {
    if (st.device == VK_NULL_HANDLE) {
        return;
    }
    if (st.pipeline != VK_NULL_HANDLE || st.layout != VK_NULL_HANDLE) {
        st.deviceWaitIdle(st.device);
    }
    if (st.pipeline != VK_NULL_HANDLE) {
        st.destroyPipeline(st.device, st.pipeline, nullptr);
    }
    if (st.layout != VK_NULL_HANDLE) {
        st.destroyPipelineLayout(st.device, st.layout, nullptr);
    }
    st = State();
}

} // namespace dim
