// The shell's internals for the extras' own files (shell.cpp): the Vulkan
// device the extras draw with, and their ImGui context.

#ifndef PORT_EXTRAS_SHELL_H
#define PORT_EXTRAS_SHELL_H

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include "imgui.h"

namespace extras {

// The device (vk.c's, given through extrasVulkanReady), nullptr functions
// before it exists.
struct Device {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    unsigned queueFamily;
    VkQueue queue;
};
const Device* device();
PFN_vkVoidFunction vkLoad(const char* name);

// The extras' ImGui context (made the first time they open).
ImGuiContext* context();

// The script's "shot" files go here (XG_DUMP_PRESENT, else XG_DUMP_FRAMES,
// else the working directory).
const char* shotDir();

} // namespace extras

#endif
