// The darkened game picture behind the settings menu (dim.cpp).

#ifndef PORT_UI_DIM_H
#define PORT_UI_DIM_H

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

namespace dim {

// The device, and how to reach its functions.
void init(VkDevice device, PFN_vkVoidFunction (*load)(const char* name, void* user), void* user);

// Inside the present pass's rendering: the w x h target (of `format`) loses
// `amount` (0-1) in each channel.
void record(VkCommandBuffer cmd, int w, int h, VkFormat format, float amount);

void shutdown();

} // namespace dim

#endif
