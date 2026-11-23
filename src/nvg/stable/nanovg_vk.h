//
// Copyright (c) 2009-2013 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//
// C API for the Vulkan backend; the counterpart of nvg::createVk & co. in nanovg_vk.hpp.
// The implementation is compiled into the `nanovg_vk` library (src/nvg/stable/nanovg_vk.cpp), so C programs
// link `nanovg_vk` in addition to `nanovg`. See nanovg_vk.hpp for the frame protocol: wait for frame slot i,
// begin your render pass, nvgBeginFrameVk(ctx, cmd, i), nvgBeginFrame ... nvgEndFrame, end the pass, submit.
//
#ifndef NANOVG_VK_H
#define NANOVG_VK_H

#include <stdint.h>
#include <vulkan/vulkan.h>

#include "nanovg.h"

#ifdef __cplusplus
extern "C" {
#endif

// Mirrors nvg::VkCreateInfo. Zero-initialize, then fill in; rasterizationSamples 0 means 1 sample and
// framesInFlight 0 means 2.
struct NVGvkCreateInfo {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    // Queue used for texture uploads. Must be the queue the frame command buffers are submitted to.
    VkQueue queue;
    uint32_t queueFamilyIndex;
    // Render pass (and subpass) the NanoVG draws are recorded in. Needs a color and a stencil attachment.
    VkRenderPass renderPass;
    uint32_t subpass;
    VkSampleCountFlagBits rasterizationSamples;
    // Number of frames the application keeps in flight; frameIndex passed to nvgBeginFrameVk is in [0, framesInFlight).
    uint32_t framesInFlight;
    // Required. Used to resolve all instance and device level functions.
    PFN_vkGetInstanceProcAddr getInstanceProcAddr;
    const VkAllocationCallbacks* allocator;
};
typedef struct NVGvkCreateInfo NVGvkCreateInfo;

// Creates a NanoVG context rendering through Vulkan. flags is a combination of NVGcreateFlags. Returns NULL on failure.
NVGcontext* nvgCreateVk(const NVGvkCreateInfo* createInfo, int flags);
void nvgDeleteVk(NVGcontext* ctx);

// Selects the command buffer that the next nvgEndFrame() records into and the frame-in-flight slot it belongs to.
// Call once per frame, after waiting for the previous use of frameIndex and before nvgBeginFrame().
void nvgBeginFrameVk(NVGcontext* ctx, VkCommandBuffer commandBuffer, uint32_t frameIndex);

// Wraps an application owned image view (see nvg::createImageFromHandleVk). sampler may be VK_NULL_HANDLE.
int nvgCreateImageFromHandleVk(NVGcontext* ctx, VkImageView view, VkSampler sampler, VkImageLayout layout, int w, int h, int imageFlags);
VkImageView nvgImageHandleVk(NVGcontext* ctx, int image);

#ifdef __cplusplus
}
#endif

#endif  // NANOVG_VK_H
