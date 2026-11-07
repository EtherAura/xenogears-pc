/*
 * The shader chain's device side (shaderchain.c), for vk.c: the present
 * pass draws the game picture into the image shaderChainBegin gives, runs
 * the passes (shaderChainRun) before the window's pass, draws the last one
 * inside it (shaderChainFinal) and ends the frame (shaderChainEnd).
 */
#ifndef PORT_GPU_SHADERCHAIN_VK_H
#define PORT_GPU_SHADERCHAIN_VK_H

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#define SHADER_CHAIN_FRAMES 3 /* vk.c's frame contexts */

typedef struct {
    void* getInstanceProcAddr; /* PFN_vkGetInstanceProcAddr */
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    void* user;
    /* vk.c's command buffer being recorded (begun if it was not) */
    VkCommandBuffer (*beginOneShot)(void* user);
    /* hand it to the queue and wait for it */
    void (*submitAndWait)(void* user);
    /* Synchronize host presentation queue calls before a device-wide wait. */
    void (*waitIdle)(void* user);
} ShaderChainDevice;

void shaderChainVulkanReady(const ShaderChainDevice* dev);
void shaderChainVulkanShutdown(void);
/* A preset is loaded and ready to draw. */
int shaderChainActive(void);
/* Choose the chain for this frame's picture (movie: a 24-bit one) for the
 * calls below: 1 when one is in use for it. */
int shaderChainUse(int movie);
/* This frame's picture (picW x picH) for a window rectangle of vpW x vpH:
 * the image view (R8G8B8A8_UNORM, GENERAL layout) to draw the picture into. */
VkImageView shaderChainBegin(VkCommandBuffer cmd, int picW, int picH, int vpW, int vpH);
/* Every pass but the last (ctx: vk.c's frame context, 0..SHADER_CHAIN_FRAMES-1). */
void shaderChainRun(VkCommandBuffer cmd, int ctx);
/* The last pass, inside the window's rendering (target format), into dest,
 * shown inside clip (both x, y, width, height). */
void shaderChainFinal(VkCommandBuffer cmd, VkFormat format, const int dest[4], const int clip[4]);
void shaderChainEnd(void);

#endif /* PORT_GPU_SHADERCHAIN_VK_H */
