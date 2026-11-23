#pragma once

// Vulkan hooks of the example window layer. Only available when the window backend is
// compiled with NANOVG_VULKAN; the window must be created with NVGWIN_PROFILE_VULKAN.

#include <vulkan/vulkan.h>

#include "nvg_window.hpp"

// vkGetInstanceProcAddr from the Vulkan loader the window system opened (no link-time dependency on libvulkan).
PFN_vkGetInstanceProcAddr nvgwin_vk_get_instance_proc_addr(void);

// Instance extensions needed to present to windows of this window system.
const char* const* nvgwin_vk_instance_extensions(uint32_t* count);

// Returns 0 on success.
int nvgwin_vk_create_surface(NvgWindow* win, VkInstance instance, VkSurfaceKHR* surface);
