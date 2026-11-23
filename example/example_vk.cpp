//
// Copyright (c) 2013 Mikko Mononen memon@inside.org
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
// NanoVG++ demo rendered with the Vulkan backend (nanovg_vk.hpp).
//
// Usage: example_vk [--test N]
//   --test N  Render a fixed animation and capture N screenshots (screenshotNNN.png in the working directory),
//             comparing each against an existing file of the same name. Exit code is non-zero on mismatch.
//
// Keys: Esc quit, Space blow up, S screenshot (dump.png), P premultiplied alpha.
// Set VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation to run under the validation layer.
//

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "nvg_window.hpp"
#include "nvg_window_vk.hpp"
#include "nvg_input.hpp"
#include "nanovg.hpp"
#define NANOVG_VK_IMPLEMENTATION
#include "nanovg_vk.hpp"
#include "demo.hpp"
#include "perf.hpp"

namespace
{

// clang-format off
#define EX_GLOBAL_FUNCS(X) X(CreateInstance)
#define EX_INSTANCE_FUNCS(X) \
    X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceQueueFamilyProperties) \
    X(GetPhysicalDeviceFormatProperties) X(GetPhysicalDeviceMemoryProperties) X(EnumerateDeviceExtensionProperties) \
    X(GetPhysicalDeviceSurfaceSupportKHR) X(GetPhysicalDeviceSurfaceCapabilitiesKHR) X(GetPhysicalDeviceSurfaceFormatsKHR) \
    X(GetPhysicalDeviceSurfacePresentModesKHR) X(DestroySurfaceKHR) X(CreateDevice) X(GetDeviceProcAddr)
#define EX_DEVICE_FUNCS(X) \
    X(DestroyDevice) X(GetDeviceQueue) X(DeviceWaitIdle) X(QueueSubmit) \
    X(CreateSwapchainKHR) X(DestroySwapchainKHR) X(GetSwapchainImagesKHR) X(AcquireNextImageKHR) X(QueuePresentKHR) \
    X(CreateImage) X(DestroyImage) X(CreateImageView) X(DestroyImageView) X(GetImageMemoryRequirements) X(BindImageMemory) \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) \
    X(AllocateMemory) X(FreeMemory) X(MapMemory) X(UnmapMemory) \
    X(CreateRenderPass) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(ResetCommandBuffer) X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdPipelineBarrier) X(CmdCopyImageToBuffer) \
    X(CreateFence) X(DestroyFence) X(WaitForFences) X(ResetFences) X(CreateSemaphore) X(DestroySemaphore)
// clang-format on

#define EX_DECLARE(name) PFN_vk##name vk##name = nullptr;
EX_GLOBAL_FUNCS(EX_DECLARE)
EX_INSTANCE_FUNCS(EX_DECLARE)
EX_DEVICE_FUNCS(EX_DECLARE)
#undef EX_DECLARE

constexpr uint32_t kFramesInFlight = 2;

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct Readback {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

struct App {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memProps{};
    uint32_t queueFamily = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;

    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat stencilFormat = VK_FORMAT_UNDEFINED;
    VkRenderPass renderPass = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent{};
    bool canReadback = false;
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkSemaphore> renderFinished;  // per swapchain image
    Image stencil;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kFramesInFlight> cmds{};
    std::array<VkFence, kFramesInFlight> fences{};
    std::array<VkSemaphore, kFramesInFlight> imageAvailable{};

    Readback readback;
};

bool vkOk(VkResult res, const char* what)
{
    if (res == VK_SUCCESS) return true;
    printf("%s failed (VkResult %d)\n", what, static_cast<int>(res));
    return false;
}

bool findMemoryType(const App& app, uint32_t bits, VkMemoryPropertyFlags flags, uint32_t* index)
{
    for (uint32_t i = 0; i < app.memProps.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (app.memProps.memoryTypes[i].propertyFlags & flags) == flags) {
            *index = i;
            return true;
        }
    }
    return false;
}

bool createInstance(App& app)
{
    app.getInstanceProcAddr = nvgwin_vk_get_instance_proc_addr();
    if (app.getInstanceProcAddr == nullptr) {
        printf("Vulkan loader not available.\n");
        return false;
    }
#define EX_LOAD_GLOBAL(name) vk##name = reinterpret_cast<PFN_vk##name>(app.getInstanceProcAddr(VK_NULL_HANDLE, "vk" #name));
    EX_GLOBAL_FUNCS(EX_LOAD_GLOBAL)
#undef EX_LOAD_GLOBAL

    uint32_t extCount = 0;
    const char* const* exts = nvgwin_vk_instance_extensions(&extCount);
    if (exts == nullptr) {
        printf("Window system cannot present with Vulkan.\n");
        return false;
    }

    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.pApplicationName = "NanoVG";
    ai.pEngineName = "NanoVG++";
    ai.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = extCount;
    ici.ppEnabledExtensionNames = exts;
    if (!vkOk(vkCreateInstance(&ici, nullptr, &app.instance), "vkCreateInstance")) return false;

#define EX_LOAD_INSTANCE(name)                                                                              \
    vk##name = reinterpret_cast<PFN_vk##name>(app.getInstanceProcAddr(app.instance, "vk" #name));           \
    if (vk##name == nullptr) {                                                                              \
        printf("Could not load vk" #name "\n");                                                             \
        return false;                                                                                       \
    }
    EX_INSTANCE_FUNCS(EX_LOAD_INSTANCE)
#undef EX_LOAD_INSTANCE
    return true;
}

bool hasExtension(VkPhysicalDevice gpu, const char* name)
{
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> props(n);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &n, props.data());
    for (const auto& p : props)
        if (strcmp(p.extensionName, name) == 0) return true;
    return false;
}

bool createDevice(App& app)
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(app.instance, &count, nullptr);
    std::vector<VkPhysicalDevice> gpus(count);
    vkEnumeratePhysicalDevices(app.instance, &count, gpus.data());

    // Prefer a discrete GPU that can render and present from one queue family.
    int bestScore = -1;
    for (VkPhysicalDevice gpu : gpus) {
        if (!hasExtension(gpu, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, qprops.data());
        for (uint32_t q = 0; q < qn; q++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(gpu, q, app.surface, &present);
            if (!(qprops[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(gpu, &props);
            const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > bestScore) {
                bestScore = score;
                app.gpu = gpu;
                app.queueFamily = q;
            }
            break;
        }
    }
    if (app.gpu == VK_NULL_HANDLE) {
        printf("No Vulkan device can present to this window.\n");
        return false;
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(app.gpu, &props);
    printf("Vulkan device: %s\n", props.deviceName);
    vkGetPhysicalDeviceMemoryProperties(app.gpu, &app.memProps);

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = app.queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    const char* deviceExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = deviceExts;
    if (!vkOk(vkCreateDevice(app.gpu, &dci, nullptr, &app.device), "vkCreateDevice")) return false;

#define EX_LOAD_DEVICE(name)                                                                                \
    vk##name = reinterpret_cast<PFN_vk##name>(vkGetDeviceProcAddr(app.device, "vk" #name));                 \
    if (vk##name == nullptr) {                                                                              \
        printf("Could not load vk" #name "\n");                                                             \
        return false;                                                                                       \
    }
    EX_DEVICE_FUNCS(EX_LOAD_DEVICE)
#undef EX_LOAD_DEVICE
    vkGetDeviceQueue(app.device, app.queueFamily, 0, &app.queue);
    return true;
}

bool chooseFormats(App& app)
{
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(app.gpu, app.surface, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(app.gpu, app.surface, &n, formats.data());
    // UNORM (not SRGB) so blending happens on the same values as the GL backend.
    for (const auto& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            app.colorFormat = f.format;
            break;
        }
    }
    if (app.colorFormat == VK_FORMAT_UNDEFINED && n > 0) app.colorFormat = formats[0].format;

    for (VkFormat f : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT, VK_FORMAT_S8_UINT}) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(app.gpu, f, &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            app.stencilFormat = f;
            break;
        }
    }
    if (app.stencilFormat == VK_FORMAT_UNDEFINED) {
        printf("No stencil format available.\n");
        return false;
    }
    return app.colorFormat != VK_FORMAT_UNDEFINED;
}

bool createRenderPass(App& app)
{
    VkAttachmentDescription atts[2]{};
    atts[0].format = app.colorFormat;
    atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    // NanoVG needs a stencil buffer that starts out cleared to zero.
    atts[1].format = app.stencilFormat;
    atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference stencilRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &stencilRef;

    VkSubpassDependency deps[2]{};
    // Wait for the swapchain image (acquire semaphore) and for the previous frame's use of the shared stencil image.
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    // Make the rendered image available to the optional screenshot copy.
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 2;
    rpci.pAttachments = atts;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;
    return vkOk(vkCreateRenderPass(app.device, &rpci, nullptr, &app.renderPass), "vkCreateRenderPass");
}

void destroySwapchainResources(App& app)
{
    for (VkFramebuffer fb : app.framebuffers) vkDestroyFramebuffer(app.device, fb, nullptr);
    for (VkImageView v : app.swapViews) vkDestroyImageView(app.device, v, nullptr);
    for (VkSemaphore s : app.renderFinished) vkDestroySemaphore(app.device, s, nullptr);
    app.framebuffers.clear();
    app.swapViews.clear();
    app.renderFinished.clear();
    app.swapImages.clear();
    if (app.stencil.view) vkDestroyImageView(app.device, app.stencil.view, nullptr);
    if (app.stencil.image) vkDestroyImage(app.device, app.stencil.image, nullptr);
    if (app.stencil.memory) vkFreeMemory(app.device, app.stencil.memory, nullptr);
    app.stencil = Image{};
}

bool createSwapchain(App& app, int fbWidth, int fbHeight)
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app.gpu, app.surface, &caps);
    if (caps.currentExtent.width != UINT32_MAX) {
        app.extent = caps.currentExtent;
    } else {
        app.extent.width = std::clamp(static_cast<uint32_t>(fbWidth), caps.minImageExtent.width, caps.maxImageExtent.width);
        app.extent.height = std::clamp(static_cast<uint32_t>(fbHeight), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (app.extent.width == 0 || app.extent.height == 0) return false;  // minimized

    // Uncapped frame rate like the GL examples (swap interval 0), falling back to vsync.
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(app.gpu, app.surface, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(app.gpu, app.surface, &n, modes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (VkPresentModeKHR m : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
        if (std::find(modes.begin(), modes.end(), m) != modes.end()) {
            presentMode = m;
            break;
        }
    }

    uint32_t imageCount = std::max(caps.minImageCount + 1, 3u);
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);
    app.canReadback = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = app.surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = app.colorFormat;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent = app.extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (app.canReadback ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (VkCompositeAlphaFlagBitsKHR a : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
        if (caps.supportedCompositeAlpha & a) {
            sci.compositeAlpha = a;
            break;
        }
    }
    sci.presentMode = presentMode;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = app.swapchain;
    VkSwapchainKHR swapchain;
    if (!vkOk(vkCreateSwapchainKHR(app.device, &sci, nullptr, &swapchain), "vkCreateSwapchainKHR")) return false;
    if (app.swapchain) vkDestroySwapchainKHR(app.device, app.swapchain, nullptr);
    app.swapchain = swapchain;

    vkGetSwapchainImagesKHR(app.device, app.swapchain, &n, nullptr);
    app.swapImages.resize(n);
    vkGetSwapchainImagesKHR(app.device, app.swapchain, &n, app.swapImages.data());

    // Stencil buffer
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = app.stencilFormat;
    ici.extent = {app.extent.width, app.extent.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!vkOk(vkCreateImage(app.device, &ici, nullptr, &app.stencil.image), "vkCreateImage")) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(app.device, app.stencil.image, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    if (!findMemoryType(app, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mai.memoryTypeIndex) &&
        !findMemoryType(app, req.memoryTypeBits, 0, &mai.memoryTypeIndex))
        return false;
    if (!vkOk(vkAllocateMemory(app.device, &mai, nullptr, &app.stencil.memory), "vkAllocateMemory")) return false;
    vkBindImageMemory(app.device, app.stencil.image, app.stencil.memory, 0);
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = app.stencil.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = app.stencilFormat;
    vci.subresourceRange = {VK_IMAGE_ASPECT_STENCIL_BIT | (app.stencilFormat == VK_FORMAT_S8_UINT ? 0u : VK_IMAGE_ASPECT_DEPTH_BIT), 0, 1, 0, 1};
    if (!vkOk(vkCreateImageView(app.device, &vci, nullptr, &app.stencil.view), "vkCreateImageView")) return false;

    for (VkImage image : app.swapImages) {
        vci.image = image;
        vci.format = app.colorFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view;
        if (!vkOk(vkCreateImageView(app.device, &vci, nullptr, &view), "vkCreateImageView")) return false;
        app.swapViews.push_back(view);

        const VkImageView attachments[2] = {view, app.stencil.view};
        VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fci.renderPass = app.renderPass;
        fci.attachmentCount = 2;
        fci.pAttachments = attachments;
        fci.width = app.extent.width;
        fci.height = app.extent.height;
        fci.layers = 1;
        VkFramebuffer fb;
        if (!vkOk(vkCreateFramebuffer(app.device, &fci, nullptr, &fb), "vkCreateFramebuffer")) return false;
        app.framebuffers.push_back(fb);

        VkSemaphoreCreateInfo semci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore sem;
        if (!vkOk(vkCreateSemaphore(app.device, &semci, nullptr, &sem), "vkCreateSemaphore")) return false;
        app.renderFinished.push_back(sem);
    }
    return true;
}

bool recreateSwapchain(App& app, NvgWindow* window)
{
    int w = 0, h = 0;
    nvgwin_get_framebuffer_size(window, &w, &h);
    vkDeviceWaitIdle(app.device);
    destroySwapchainResources(app);
    return createSwapchain(app, w, h);
}

bool createFrameResources(App& app)
{
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = app.queueFamily;
    if (!vkOk(vkCreateCommandPool(app.device, &cpci, nullptr, &app.commandPool), "vkCreateCommandPool")) return false;
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = app.commandPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = kFramesInFlight;
    if (!vkOk(vkAllocateCommandBuffers(app.device, &cbai, app.cmds.data()), "vkAllocateCommandBuffers")) return false;
    for (uint32_t i = 0; i < kFramesInFlight; i++) {
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (!vkOk(vkCreateFence(app.device, &fci, nullptr, &app.fences[i]), "vkCreateFence")) return false;
        if (!vkOk(vkCreateSemaphore(app.device, &sci, nullptr, &app.imageAvailable[i]), "vkCreateSemaphore")) return false;
    }
    return true;
}

void destroyReadback(App& app)
{
    if (app.readback.buffer) vkDestroyBuffer(app.device, app.readback.buffer, nullptr);
    if (app.readback.memory) vkFreeMemory(app.device, app.readback.memory, nullptr);
    app.readback = Readback{};
}

bool ensureReadback(App& app, VkDeviceSize size)
{
    if (app.readback.size >= size) return true;
    destroyReadback(app);
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!vkOk(vkCreateBuffer(app.device, &bci, nullptr, &app.readback.buffer), "vkCreateBuffer")) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(app.device, app.readback.buffer, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    if (!findMemoryType(app, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &mai.memoryTypeIndex)) return false;
    if (!vkOk(vkAllocateMemory(app.device, &mai, nullptr, &app.readback.memory), "vkAllocateMemory")) return false;
    vkBindBufferMemory(app.device, app.readback.buffer, app.readback.memory, 0);
    if (!vkOk(vkMapMemory(app.device, app.readback.memory, 0, VK_WHOLE_SIZE, 0, &app.readback.mapped), "vkMapMemory")) return false;
    app.readback.size = size;
    return true;
}

// Records a copy of the presented image into the readback buffer. Runs after the render pass, whose final
// layout is PRESENT_SRC and whose outgoing dependency makes the color writes visible to transfers.
void recordReadback(App& app, VkCommandBuffer cmd, VkImage image)
{
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {app.extent.width, app.extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, app.readback.buffer, 1, &region);

    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.srcAccessMask = 0;
    b.dstAccessMask = 0;
    VkBufferMemoryBarrier bb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    bb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    bb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb.buffer = app.readback.buffer;
    bb.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 1, &bb, 1, &b);
}

// Converts the readback buffer to top-down RGBA8.
std::vector<unsigned char> readbackPixels(const App& app)
{
    const size_t n = static_cast<size_t>(app.extent.width) * app.extent.height * 4;
    std::vector<unsigned char> px(n);
    std::memcpy(px.data(), app.readback.mapped, n);
    if (app.colorFormat == VK_FORMAT_B8G8R8A8_UNORM || app.colorFormat == VK_FORMAT_B8G8R8A8_SRGB) {
        for (size_t i = 0; i < n; i += 4) std::swap(px[i], px[i + 2]);
    }
    return px;
}

void destroyApp(App& app)
{
    if (app.device) {
        vkDeviceWaitIdle(app.device);
        destroyReadback(app);
        destroySwapchainResources(app);
        if (app.swapchain) vkDestroySwapchainKHR(app.device, app.swapchain, nullptr);
        for (uint32_t i = 0; i < kFramesInFlight; i++) {
            if (app.fences[i]) vkDestroyFence(app.device, app.fences[i], nullptr);
            if (app.imageAvailable[i]) vkDestroySemaphore(app.device, app.imageAvailable[i], nullptr);
        }
        if (app.commandPool) vkDestroyCommandPool(app.device, app.commandPool, nullptr);
        if (app.renderPass) vkDestroyRenderPass(app.device, app.renderPass, nullptr);
        vkDestroyDevice(app.device, nullptr);
    }
    if (app.instance) {
        if (app.surface) vkDestroySurfaceKHR(app.instance, app.surface, nullptr);
        vkDestroyInstance(app.instance, nullptr);
    }
    app = App{};
}

}  // namespace

void errorcb(int error, const char* desc)
{
    printf("Window error %d: %s\n", error, desc);
}

int blowup = 0;
int screenshot = 0;
int premult = 0;

static void key(NvgWindow* window, int key, int scancode, int action, int mods, void* user)
{
    UNUSED(user);
    UNUSED(scancode);
    UNUSED(mods);
    if (key == NVG_KEY_ESCAPE && action == NVG_PRESS) nvgwin_set_should_close(window, 1);
    if (key == NVG_KEY_SPACE && action == NVG_PRESS) blowup = !blowup;
    if (key == NVG_KEY_S && action == NVG_PRESS) screenshot = 1;
    if (key == NVG_KEY_P && action == NVG_PRESS) premult = !premult;
}

static bool parseTestCount(const char* s, int* out)
{
    char* endptr = nullptr;
    long v = strtol(s, &endptr, 10);
    if (endptr == s || (endptr && *endptr != '\0') || v < 0) return false;
    *out = static_cast<int>(v);
    return true;
}

int main(int argc, char** argv)
{
    DemoData data;
    GPUtimer gpuTimer;
    PerfGraph fps, cpuGraph, gpuGraph;
    double prevt = 0, cpuTime = 0;
    int testCount = 0;
    bool testRequested = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--test")) {
            if (i + 1 >= argc || !parseTestCount(argv[i + 1], &testCount)) {
                printf("Invalid --test N value.\n");
                return -1;
            }
            testRequested = true;
            i++;
        } else if (!strncmp(argv[i], "--test=", 7)) {
            if (!parseTestCount(argv[i] + 7, &testCount)) {
                printf("Invalid --test N value.\n");
                return -1;
            }
            testRequested = true;
        }
    }
    int testRemaining = testCount;

    if (nvgwin_init() != 0) {
        printf("Failed to init window system.\n");
        return -1;
    }
    nvgwin_set_error_callback(errorcb);

    initGraph(fps, GRAPH_RENDER_FPS, "Frame Time");
    initGraph(cpuGraph, GRAPH_RENDER_MS, "CPU Time");
    initGraph(gpuGraph, GRAPH_RENDER_MS, "GPU Time");

    NvgWindow* window = nvgwin_create(1200, 1000, "NanoVG (Vulkan)", NVGWIN_PROFILE_VULKAN, 0, 0);
    if (!window) {
        nvgwin_shutdown();
        return -1;
    }
    nvgwin_set_key_callback(window, key, nullptr);

    App app;
    int fbWidth = 0, fbHeight = 0;
    nvgwin_get_framebuffer_size(window, &fbWidth, &fbHeight);
    if (!createInstance(app) || nvgwin_vk_create_surface(window, app.instance, &app.surface) != 0 || !createDevice(app) || !chooseFormats(app) || !createRenderPass(app) ||
        !createSwapchain(app, fbWidth, fbHeight) || !createFrameResources(app)) {
        printf("Could not init Vulkan.\n");
        destroyApp(app);
        nvgwin_destroy(window);
        nvgwin_shutdown();
        return -1;
    }

    nvg::VkCreateInfo ci;
    ci.instance = app.instance;
    ci.physicalDevice = app.gpu;
    ci.device = app.device;
    ci.queue = app.queue;
    ci.queueFamilyIndex = app.queueFamily;
    ci.renderPass = app.renderPass;
    ci.framesInFlight = kFramesInFlight;
    ci.getInstanceProcAddr = app.getInstanceProcAddr;
    auto vgOwner = nvg::createVk(ci, static_cast<int>(nvg::CreateFlags::Antialias | nvg::CreateFlags::StencilStrokes | nvg::CreateFlags::Debug));
    if (!vgOwner) {
        printf("Could not init nanovg.\n");
        destroyApp(app);
        nvgwin_destroy(window);
        nvgwin_shutdown();
        return -1;
    }
    nvg::Context& vg = *vgOwner;

    if (loadDemoData(vg, data) == -1) {
        printf("Could not load demo data.\n");
        return -1;
    }

    if (testRequested && testCount == 0) nvgwin_set_should_close(window, 1);

    initGPUTimer(&gpuTimer);

    nvgwin_set_time(0);
    long long frameCounter = 0;
    const int sampleRate = 5;
    bool success = true;
    uint32_t frame = 0;
    bool swapchainOk = true;
    prevt = testRequested ? 0.0 : nvgwin_get_time();

    while (!nvgwin_should_close(window)) {
        double mx, my, t, dt;
        int winWidth, winHeight;

        nvgwin_poll_events();
        nvgwin_get_window_size(window, &winWidth, &winHeight);
        nvgwin_get_framebuffer_size(window, &fbWidth, &fbHeight);
        if (!swapchainOk || static_cast<uint32_t>(fbWidth) != app.extent.width || static_cast<uint32_t>(fbHeight) != app.extent.height) {
            swapchainOk = recreateSwapchain(app, window);
            if (!swapchainOk) continue;  // minimized
        }

        // Wait for this slot's previous frame; afterwards its command buffer and NanoVG's resources for it are free.
        vkWaitForFences(app.device, 1, &app.fences[frame], VK_TRUE, UINT64_MAX);
        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(app.device, app.swapchain, UINT64_MAX, app.imageAvailable[frame], VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchainOk = false;
            continue;
        }
        if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
            printf("vkAcquireNextImageKHR failed (%d)\n", static_cast<int>(acq));
            success = false;
            break;
        }
        vkResetFences(app.device, 1, &app.fences[frame]);

        t = testRequested ? frameCounter / 30.0 : nvgwin_get_time();
        dt = t - prevt;
        prevt = t;

        if (testRequested) {
            double angle = 2.0 * nvg::nvgPi * testRemaining / (double)testCount;
            mx = winWidth / 2.0f + cos(angle) * winWidth / 4.0f;
            my = winHeight / 2.0f + sin(angle) * winHeight / 4.0f;
        } else {
            nvgwin_get_cursor_pos(window, &mx, &my);
        }
        const float pxRatio = (float)app.extent.width / (float)winWidth;

        const bool captureTest = testRemaining > 0 && frameCounter % sampleRate == 0;
        const bool captureKey = screenshot && !testRequested;
        const bool capture = (captureTest || captureKey) && app.canReadback && ensureReadback(app, VkDeviceSize(app.extent.width) * app.extent.height * 4);

        VkCommandBuffer cmd = app.cmds[frame];
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        VkClearValue clears[2]{};
        if (premult) {
            clears[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        } else {
            clears[0].color = {{0.3f, 0.3f, 0.32f, 1.0f}};
        }
        clears[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpbi.renderPass = app.renderPass;
        rpbi.framebuffer = app.framebuffers[imageIndex];
        rpbi.renderArea = {{0, 0}, app.extent};
        rpbi.clearValueCount = 2;
        rpbi.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        nvg::beginFrameVk(vg, cmd, frame);
        vg.beginFrame((float)winWidth, (float)winHeight, pxRatio);
        renderDemo(vg, (float)mx, (float)my, (float)winWidth, (float)winHeight, (float)t, blowup, data);
        if (!testRequested) {
            renderGraph(vg, 5, 5, fps);
            renderGraph(vg, 5 + 200 + 5, 5, cpuGraph);
            if (gpuTimer.supported) renderGraph(vg, 5 + 200 + 5 + 200 + 5, 5, gpuGraph);
        }
        vg.endFrame();

        vkCmdEndRenderPass(cmd);
        if (capture) recordReadback(app, cmd, app.swapImages[imageIndex]);
        vkEndCommandBuffer(cmd);

        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &app.imageAvailable[frame];
        si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &app.renderFinished[imageIndex];
        if (!vkOk(vkQueueSubmit(app.queue, 1, &si, app.fences[frame]), "vkQueueSubmit")) {
            success = false;
            break;
        }

        // Measure the CPU time taken excluding presentation (as it may wait for the GPU)
        cpuTime = nvgwin_get_time() - t;
        updateGraph(fps, (float)dt);
        updateGraph(cpuGraph, (float)cpuTime);

        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &app.renderFinished[imageIndex];
        pi.swapchainCount = 1;
        pi.pSwapchains = &app.swapchain;
        pi.pImageIndices = &imageIndex;
        VkResult pres = vkQueuePresentKHR(app.queue, &pi);
        if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) swapchainOk = false;

        if (captureTest || captureKey) {
            bool ok = capture;
            std::string fileName = "dump.png";
            if (captureTest) {
                std::array<char, 256> name{};
                printf("Capturing %d/%d at t = %0.2f sec\n", testCount - testRemaining + 1, testCount, t);
                snprintf(name.data(), name.size(), "screenshot%03d.png", testCount - testRemaining + 1);
                fileName = name.data();
            }
            if (ok) {
                vkWaitForFences(app.device, 1, &app.fences[frame], VK_TRUE, UINT64_MAX);
                std::vector<unsigned char> px = readbackPixels(app);
                ok = saveScreenShotRGBA((int)app.extent.width, (int)app.extent.height, captureTest ? 0 : premult, px.data(), fileName, captureTest);
            } else {
                printf("Screenshot not supported: swapchain images cannot be copied on this device.\n");
            }
            success &= ok;
            if (captureTest) {
                testRemaining--;
                if (testRemaining == 0) nvgwin_set_should_close(window, 1);
            } else {
                screenshot = 0;
            }
        }

        frameCounter++;
        frame = (frame + 1) % kFramesInFlight;
    }

    vkDeviceWaitIdle(app.device);
    freeDemoData(vg, data);
    nvg::deleteVk(vgOwner);

    if (!testRequested) {
        printf("Average Frame Time: %.2f ms\n", getGraphAverage(fps) * 1000.0f);
        printf("          CPU Time: %.2f ms\n", getGraphAverage(cpuGraph) * 1000.0f);
    } else {
        printf(success ? "Test passed!\n" : "Test failed!\n");
    }

    destroyApp(app);
    nvgwin_destroy(window);
    nvgwin_shutdown();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
