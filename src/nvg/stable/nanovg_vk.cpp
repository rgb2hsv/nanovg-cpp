//
// C API for the Vulkan backend (declared in stable/nanovg_vk.h). Built into the `nanovg_vk` library.
// C++ code should use nanovg_vk.hpp directly instead of linking this library: both define the backend.
//

#define NANOVG_VK_IMPLEMENTATION
#include "nanovg_vk.hpp"

#include "stable/nanovg_vk.h"

static_assert(sizeof(NVGparams) == sizeof(nvg::Params), "NVGparams must mirror nvg::Params");

namespace
{
nvg::vkdetail::VkContext* contextOf(NVGcontext* ctx)
{
    return static_cast<nvg::vkdetail::VkContext*>(nvgInternalParams(ctx)->userPtr);
}
}  // namespace

extern "C" {

NVGcontext* nvgCreateVk(const NVGvkCreateInfo* createInfo, int flags)
{
    if (createInfo == nullptr) return nullptr;
    nvg::VkCreateInfo info;
    info.instance = createInfo->instance;
    info.physicalDevice = createInfo->physicalDevice;
    info.device = createInfo->device;
    info.queue = createInfo->queue;
    info.queueFamilyIndex = createInfo->queueFamilyIndex;
    info.renderPass = createInfo->renderPass;
    info.subpass = createInfo->subpass;
    if (createInfo->rasterizationSamples != 0) info.rasterizationSamples = createInfo->rasterizationSamples;
    if (createInfo->framesInFlight != 0) info.framesInFlight = createInfo->framesInFlight;
    info.getInstanceProcAddr = createInfo->getInstanceProcAddr;
    info.allocator = createInfo->allocator;

    nvg::Params params;
    nvg::vkdetail::VkContext* vk = nvg::vkdetail::newContext(info, flags, &params);
    if (vk == nullptr) return nullptr;
    NVGcontext* ctx = nvgCreateInternal(reinterpret_cast<NVGparams*>(&params));
    // On failure the context never took ownership of the backend state.
    if (ctx == nullptr) nvg::vkdetail::renderDelete(vk);
    return ctx;
}

void nvgDeleteVk(NVGcontext* ctx)
{
    nvgDeleteInternal(ctx);
}

void nvgBeginFrameVk(NVGcontext* ctx, VkCommandBuffer commandBuffer, uint32_t frameIndex)
{
    nvg::vkdetail::beginFrame(contextOf(ctx), commandBuffer, frameIndex);
}

int nvgCreateImageFromHandleVk(NVGcontext* ctx, VkImageView view, VkSampler sampler, VkImageLayout layout, int w, int h, int imageFlags)
{
    return nvg::vkdetail::createImageFromHandle(contextOf(ctx), view, sampler, layout, w, h, imageFlags);
}

VkImageView nvgImageHandleVk(NVGcontext* ctx, int image)
{
    return nvg::vkdetail::imageHandle(contextOf(ctx), image);
}

}  // extern "C"
