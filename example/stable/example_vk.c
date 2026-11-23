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
// NanoVG demo rendered with the Vulkan backend through the C API (nanovg_vk.h). C counterpart of
// example/example_vk.cpp, using GLFW directly like the other C examples.
//
// Usage: example_stable_vk [--test N]   (same capture/compare mode as example_vk)
// Keys: Esc quit, Space blow up, S screenshot (dump.png), P premultiplied alpha.
//

#define VK_NO_PROTOTYPES
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nanovg.h"
#include "nanovg_vk.h"
#include "demo.h"
#include "perf.h"

#define FRAMES_IN_FLIGHT 2
#define MAX_SWAP_IMAGES 8

// clang-format off
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
    X(AllocateMemory) X(FreeMemory) X(MapMemory) \
    X(CreateRenderPass) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(ResetCommandBuffer) X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdPipelineBarrier) X(CmdCopyImageToBuffer) \
    X(CreateFence) X(DestroyFence) X(WaitForFences) X(ResetFences) X(CreateSemaphore) X(DestroySemaphore)
// clang-format on

static PFN_vkGetInstanceProcAddr getInstanceProcAddr;
static PFN_vkCreateInstance vkCreateInstance;
#define EX_DECLARE(name) static PFN_vk##name vk##name;
EX_INSTANCE_FUNCS(EX_DECLARE)
EX_DEVICE_FUNCS(EX_DECLARE)
#undef EX_DECLARE

typedef struct App {
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice gpu;
    VkPhysicalDeviceMemoryProperties memProps;
    uint32_t queueFamily;
    VkDevice device;
    VkQueue queue;

    VkFormat colorFormat;
    VkFormat stencilFormat;
    VkRenderPass renderPass;

    VkSwapchainKHR swapchain;
    VkExtent2D extent;
    int canReadback;
    uint32_t imageCount;
    VkImage swapImages[MAX_SWAP_IMAGES];
    VkImageView swapViews[MAX_SWAP_IMAGES];
    VkFramebuffer framebuffers[MAX_SWAP_IMAGES];
    VkSemaphore renderFinished[MAX_SWAP_IMAGES];
    VkImage stencilImage;
    VkDeviceMemory stencilMemory;
    VkImageView stencilView;

    VkCommandPool commandPool;
    VkCommandBuffer cmds[FRAMES_IN_FLIGHT];
    VkFence fences[FRAMES_IN_FLIGHT];
    VkSemaphore imageAvailable[FRAMES_IN_FLIGHT];

    VkBuffer readbackBuffer;
    VkDeviceMemory readbackMemory;
    void* readbackMapped;
    VkDeviceSize readbackSize;
} App;

static int vkOk(VkResult res, const char* what)
{
    if (res == VK_SUCCESS) return 1;
    printf("%s failed (VkResult %d)\n", what, (int)res);
    return 0;
}

static int findMemoryType(const App* app, uint32_t bits, VkMemoryPropertyFlags flags, uint32_t* index)
{
    uint32_t i;
    for (i = 0; i < app->memProps.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (app->memProps.memoryTypes[i].propertyFlags & flags) == flags) {
            *index = i;
            return 1;
        }
    }
    return 0;
}

static int createInstance(App* app)
{
    uint32_t extCount = 0;
    const char** exts;
    VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};

    if (!glfwVulkanSupported()) {
        printf("Vulkan loader not available.\n");
        return 0;
    }
    getInstanceProcAddr = (PFN_vkGetInstanceProcAddr)glfwGetInstanceProcAddress(NULL, "vkGetInstanceProcAddr");
    vkCreateInstance = (PFN_vkCreateInstance)getInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    exts = glfwGetRequiredInstanceExtensions(&extCount);
    if (exts == NULL || vkCreateInstance == NULL) return 0;

    ai.pApplicationName = "NanoVG";
    ai.pEngineName = "NanoVG++";
    ai.apiVersion = VK_API_VERSION_1_0;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = extCount;
    ici.ppEnabledExtensionNames = exts;
    if (!vkOk(vkCreateInstance(&ici, NULL, &app->instance), "vkCreateInstance")) return 0;

#define EX_LOAD_INSTANCE(name)                                                                \
    vk##name = (PFN_vk##name)getInstanceProcAddr(app->instance, "vk" #name);                  \
    if (vk##name == NULL) {                                                                   \
        printf("Could not load vk" #name "\n");                                               \
        return 0;                                                                             \
    }
    EX_INSTANCE_FUNCS(EX_LOAD_INSTANCE)
#undef EX_LOAD_INSTANCE
    return 1;
}

static int hasSwapchainExtension(VkPhysicalDevice gpu)
{
    uint32_t n = 0, i;
    int found = 0;
    VkExtensionProperties* props;
    vkEnumerateDeviceExtensionProperties(gpu, NULL, &n, NULL);
    props = (VkExtensionProperties*)malloc(sizeof(VkExtensionProperties) * (n ? n : 1));
    vkEnumerateDeviceExtensionProperties(gpu, NULL, &n, props);
    for (i = 0; i < n; i++)
        if (strcmp(props[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) found = 1;
    free(props);
    return found;
}

static int createDevice(App* app)
{
    uint32_t count = 0, g;
    VkPhysicalDevice gpus[16];
    int bestScore = -1;
    const float priority = 1.0f;
    const char* deviceExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkPhysicalDeviceProperties props;

    vkEnumeratePhysicalDevices(app->instance, &count, NULL);
    if (count > 16) count = 16;
    vkEnumeratePhysicalDevices(app->instance, &count, gpus);

    // Prefer a discrete GPU that can render and present from one queue family.
    for (g = 0; g < count; g++) {
        uint32_t qn = 0, q;
        VkQueueFamilyProperties qprops[32];
        if (!hasSwapchainExtension(gpus[g])) continue;
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[g], &qn, NULL);
        if (qn > 32) qn = 32;
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[g], &qn, qprops);
        for (q = 0; q < qn; q++) {
            VkBool32 present = VK_FALSE;
            int score;
            vkGetPhysicalDeviceSurfaceSupportKHR(gpus[g], q, app->surface, &present);
            if (!(qprops[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            vkGetPhysicalDeviceProperties(gpus[g], &props);
            score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > bestScore) {
                bestScore = score;
                app->gpu = gpus[g];
                app->queueFamily = q;
            }
            break;
        }
    }
    if (app->gpu == VK_NULL_HANDLE) {
        printf("No Vulkan device can present to this window.\n");
        return 0;
    }
    vkGetPhysicalDeviceProperties(app->gpu, &props);
    printf("Vulkan device: %s\n", props.deviceName);
    vkGetPhysicalDeviceMemoryProperties(app->gpu, &app->memProps);

    qci.queueFamilyIndex = app->queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = deviceExts;
    if (!vkOk(vkCreateDevice(app->gpu, &dci, NULL, &app->device), "vkCreateDevice")) return 0;

#define EX_LOAD_DEVICE(name)                                                                  \
    vk##name = (PFN_vk##name)vkGetDeviceProcAddr(app->device, "vk" #name);                    \
    if (vk##name == NULL) {                                                                   \
        printf("Could not load vk" #name "\n");                                               \
        return 0;                                                                             \
    }
    EX_DEVICE_FUNCS(EX_LOAD_DEVICE)
#undef EX_LOAD_DEVICE
    vkGetDeviceQueue(app->device, app->queueFamily, 0, &app->queue);
    return 1;
}

static int chooseFormats(App* app)
{
    uint32_t n = 0, i;
    VkSurfaceFormatKHR formats[64];
    const VkFormat stencilFormats[] = {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT, VK_FORMAT_S8_UINT};

    vkGetPhysicalDeviceSurfaceFormatsKHR(app->gpu, app->surface, &n, NULL);
    if (n > 64) n = 64;
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->gpu, app->surface, &n, formats);
    // UNORM (not SRGB) so blending happens on the same values as the GL backend.
    for (i = 0; i < n; i++) {
        if ((formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            app->colorFormat = formats[i].format;
            break;
        }
    }
    if (app->colorFormat == VK_FORMAT_UNDEFINED && n > 0) app->colorFormat = formats[0].format;

    for (i = 0; i < sizeof(stencilFormats) / sizeof(stencilFormats[0]); i++) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(app->gpu, stencilFormats[i], &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            app->stencilFormat = stencilFormats[i];
            break;
        }
    }
    return app->colorFormat != VK_FORMAT_UNDEFINED && app->stencilFormat != VK_FORMAT_UNDEFINED;
}

static int createRenderPass(App* app)
{
    VkAttachmentDescription atts[2];
    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference stencilRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass;
    VkSubpassDependency deps[2];
    VkRenderPassCreateInfo rpci = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};

    memset(atts, 0, sizeof(atts));
    atts[0].format = app->colorFormat;
    atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    // NanoVG needs a stencil buffer that starts out cleared to zero.
    atts[1].format = app->stencilFormat;
    atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &stencilRef;

    memset(deps, 0, sizeof(deps));
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

    rpci.attachmentCount = 2;
    rpci.pAttachments = atts;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;
    return vkOk(vkCreateRenderPass(app->device, &rpci, NULL, &app->renderPass), "vkCreateRenderPass");
}

static void destroySwapchainResources(App* app)
{
    uint32_t i;
    for (i = 0; i < app->imageCount; i++) {
        if (app->framebuffers[i]) vkDestroyFramebuffer(app->device, app->framebuffers[i], NULL);
        if (app->swapViews[i]) vkDestroyImageView(app->device, app->swapViews[i], NULL);
        if (app->renderFinished[i]) vkDestroySemaphore(app->device, app->renderFinished[i], NULL);
        app->framebuffers[i] = VK_NULL_HANDLE;
        app->swapViews[i] = VK_NULL_HANDLE;
        app->renderFinished[i] = VK_NULL_HANDLE;
    }
    app->imageCount = 0;
    if (app->stencilView) vkDestroyImageView(app->device, app->stencilView, NULL);
    if (app->stencilImage) vkDestroyImage(app->device, app->stencilImage, NULL);
    if (app->stencilMemory) vkFreeMemory(app->device, app->stencilMemory, NULL);
    app->stencilView = VK_NULL_HANDLE;
    app->stencilImage = VK_NULL_HANDLE;
    app->stencilMemory = VK_NULL_HANDLE;
}

static uint32_t clampu(uint32_t v, uint32_t lo, uint32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int createSwapchain(App* app, int fbWidth, int fbHeight)
{
    VkSurfaceCapabilitiesKHR caps;
    uint32_t n = 0, i, imageCount;
    VkPresentModeKHR modes[16];
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkSwapchainCreateInfoKHR sci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    VkSwapchainKHR swapchain;
    VkImageCreateInfo ici = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkMemoryRequirements req;
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    const VkCompositeAlphaFlagBitsKHR alphas[] = {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                                  VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR};

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->gpu, app->surface, &caps);
    if (caps.currentExtent.width != UINT32_MAX) {
        app->extent = caps.currentExtent;
    } else {
        app->extent.width = clampu((uint32_t)fbWidth, caps.minImageExtent.width, caps.maxImageExtent.width);
        app->extent.height = clampu((uint32_t)fbHeight, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (app->extent.width == 0 || app->extent.height == 0) return 0;  // minimized

    // Uncapped frame rate like the GL examples (swap interval 0), falling back to vsync.
    vkGetPhysicalDeviceSurfacePresentModesKHR(app->gpu, app->surface, &n, NULL);
    if (n > 16) n = 16;
    vkGetPhysicalDeviceSurfacePresentModesKHR(app->gpu, app->surface, &n, modes);
    for (i = 0; i < n; i++)
        if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR && presentMode == VK_PRESENT_MODE_FIFO_KHR) presentMode = modes[i];
    for (i = 0; i < n; i++)
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) presentMode = modes[i];

    imageCount = caps.minImageCount + 1 > 3 ? caps.minImageCount + 1 : 3;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;
    app->canReadback = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;

    sci.surface = app->surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = app->colorFormat;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent = app->extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (app->canReadback ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (i = 0; i < 4; i++) {
        if (caps.supportedCompositeAlpha & alphas[i]) {
            sci.compositeAlpha = alphas[i];
            break;
        }
    }
    sci.presentMode = presentMode;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = app->swapchain;
    if (!vkOk(vkCreateSwapchainKHR(app->device, &sci, NULL, &swapchain), "vkCreateSwapchainKHR")) return 0;
    if (app->swapchain) vkDestroySwapchainKHR(app->device, app->swapchain, NULL);
    app->swapchain = swapchain;

    vkGetSwapchainImagesKHR(app->device, app->swapchain, &n, NULL);
    if (n > MAX_SWAP_IMAGES) n = MAX_SWAP_IMAGES;
    vkGetSwapchainImagesKHR(app->device, app->swapchain, &n, app->swapImages);

    // Stencil buffer
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = app->stencilFormat;
    ici.extent.width = app->extent.width;
    ici.extent.height = app->extent.height;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!vkOk(vkCreateImage(app->device, &ici, NULL, &app->stencilImage), "vkCreateImage")) return 0;
    vkGetImageMemoryRequirements(app->device, app->stencilImage, &req);
    mai.allocationSize = req.size;
    if (!findMemoryType(app, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mai.memoryTypeIndex) && !findMemoryType(app, req.memoryTypeBits, 0, &mai.memoryTypeIndex)) return 0;
    if (!vkOk(vkAllocateMemory(app->device, &mai, NULL, &app->stencilMemory), "vkAllocateMemory")) return 0;
    vkBindImageMemory(app->device, app->stencilImage, app->stencilMemory, 0);
    vci.image = app->stencilImage;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = app->stencilFormat;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT | (app->stencilFormat == VK_FORMAT_S8_UINT ? 0u : (uint32_t)VK_IMAGE_ASPECT_DEPTH_BIT);
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    if (!vkOk(vkCreateImageView(app->device, &vci, NULL, &app->stencilView), "vkCreateImageView")) return 0;

    for (i = 0; i < n; i++) {
        VkImageView attachments[2];
        VkFramebufferCreateInfo fci = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        VkSemaphoreCreateInfo semci = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        app->imageCount = i + 1;
        vci.image = app->swapImages[i];
        vci.format = app->colorFormat;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        if (!vkOk(vkCreateImageView(app->device, &vci, NULL, &app->swapViews[i]), "vkCreateImageView")) return 0;
        attachments[0] = app->swapViews[i];
        attachments[1] = app->stencilView;
        fci.renderPass = app->renderPass;
        fci.attachmentCount = 2;
        fci.pAttachments = attachments;
        fci.width = app->extent.width;
        fci.height = app->extent.height;
        fci.layers = 1;
        if (!vkOk(vkCreateFramebuffer(app->device, &fci, NULL, &app->framebuffers[i]), "vkCreateFramebuffer")) return 0;
        if (!vkOk(vkCreateSemaphore(app->device, &semci, NULL, &app->renderFinished[i]), "vkCreateSemaphore")) return 0;
    }
    return 1;
}

static int recreateSwapchain(App* app, GLFWwindow* window)
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    vkDeviceWaitIdle(app->device);
    destroySwapchainResources(app);
    return createSwapchain(app, w, h);
}

static int createFrameResources(App* app)
{
    uint32_t i;
    VkCommandPoolCreateInfo cpci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    VkCommandBufferAllocateInfo cbai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = app->queueFamily;
    if (!vkOk(vkCreateCommandPool(app->device, &cpci, NULL, &app->commandPool), "vkCreateCommandPool")) return 0;
    cbai.commandPool = app->commandPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = FRAMES_IN_FLIGHT;
    if (!vkOk(vkAllocateCommandBuffers(app->device, &cbai, app->cmds), "vkAllocateCommandBuffers")) return 0;
    for (i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkSemaphoreCreateInfo sci = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (!vkOk(vkCreateFence(app->device, &fci, NULL, &app->fences[i]), "vkCreateFence")) return 0;
        if (!vkOk(vkCreateSemaphore(app->device, &sci, NULL, &app->imageAvailable[i]), "vkCreateSemaphore")) return 0;
    }
    return 1;
}

static void destroyReadback(App* app)
{
    if (app->readbackBuffer) vkDestroyBuffer(app->device, app->readbackBuffer, NULL);
    if (app->readbackMemory) vkFreeMemory(app->device, app->readbackMemory, NULL);
    app->readbackBuffer = VK_NULL_HANDLE;
    app->readbackMemory = VK_NULL_HANDLE;
    app->readbackMapped = NULL;
    app->readbackSize = 0;
}

static int ensureReadback(App* app, VkDeviceSize size)
{
    VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkMemoryRequirements req;
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    if (app->readbackSize >= size) return 1;
    destroyReadback(app);
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!vkOk(vkCreateBuffer(app->device, &bci, NULL, &app->readbackBuffer), "vkCreateBuffer")) return 0;
    vkGetBufferMemoryRequirements(app->device, app->readbackBuffer, &req);
    mai.allocationSize = req.size;
    if (!findMemoryType(app, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &mai.memoryTypeIndex)) return 0;
    if (!vkOk(vkAllocateMemory(app->device, &mai, NULL, &app->readbackMemory), "vkAllocateMemory")) return 0;
    vkBindBufferMemory(app->device, app->readbackBuffer, app->readbackMemory, 0);
    if (!vkOk(vkMapMemory(app->device, app->readbackMemory, 0, VK_WHOLE_SIZE, 0, &app->readbackMapped), "vkMapMemory")) return 0;
    app->readbackSize = size;
    return 1;
}

// Records a copy of the presented image into the readback buffer (after the render pass; see example_vk.cpp).
static void recordReadback(App* app, VkCommandBuffer cmd, VkImage image)
{
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    VkBufferMemoryBarrier bb = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    VkBufferImageCopy region;

    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);

    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = app->extent.width;
    region.imageExtent.height = app->extent.height;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, app->readbackBuffer, 1, &region);

    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.dstAccessMask = 0;
    bb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    bb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb.buffer = app->readbackBuffer;
    bb.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 1, &bb, 1, &b);
}

// Copies the readback buffer into top-down RGBA8. Caller frees.
static unsigned char* readbackPixels(const App* app)
{
    const size_t n = (size_t)app->extent.width * app->extent.height * 4;
    unsigned char* px = (unsigned char*)malloc(n);
    size_t i;
    if (px == NULL) return NULL;
    memcpy(px, app->readbackMapped, n);
    if (app->colorFormat == VK_FORMAT_B8G8R8A8_UNORM || app->colorFormat == VK_FORMAT_B8G8R8A8_SRGB) {
        for (i = 0; i < n; i += 4) {
            unsigned char tmp = px[i];
            px[i] = px[i + 2];
            px[i + 2] = tmp;
        }
    }
    return px;
}

static void destroyApp(App* app)
{
    uint32_t i;
    if (app->device) {
        vkDeviceWaitIdle(app->device);
        destroyReadback(app);
        destroySwapchainResources(app);
        if (app->swapchain) vkDestroySwapchainKHR(app->device, app->swapchain, NULL);
        for (i = 0; i < FRAMES_IN_FLIGHT; i++) {
            if (app->fences[i]) vkDestroyFence(app->device, app->fences[i], NULL);
            if (app->imageAvailable[i]) vkDestroySemaphore(app->device, app->imageAvailable[i], NULL);
        }
        if (app->commandPool) vkDestroyCommandPool(app->device, app->commandPool, NULL);
        if (app->renderPass) vkDestroyRenderPass(app->device, app->renderPass, NULL);
        vkDestroyDevice(app->device, NULL);
    }
    if (app->instance) {
        if (app->surface) vkDestroySurfaceKHR(app->instance, app->surface, NULL);
        vkDestroyInstance(app->instance, NULL);
    }
    memset(app, 0, sizeof(*app));
}

void errorcb(int error, const char* desc)
{
    printf("GLFW error %d: %s\n", error, desc);
}

int blowup = 0;
int screenshot = 0;
int premult = 0;

static void key(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    NVG_NOTUSED(scancode);
    NVG_NOTUSED(mods);
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(window, GLFW_TRUE);
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) blowup = !blowup;
    if (key == GLFW_KEY_S && action == GLFW_PRESS) screenshot = 1;
    if (key == GLFW_KEY_P && action == GLFW_PRESS) premult = !premult;
}

static int parseTestCount(const char* s, int* out)
{
    char* endptr = NULL;
    long v = strtol(s, &endptr, 10);
    if (endptr == s || (endptr && *endptr != '\0') || v < 0) return 0;
    *out = (int)v;
    return 1;
}

int main(int argc, char** argv)
{
    static App app;
    GLFWwindow* window;
    DemoData data;
    NVGcontext* vg;
    NVGvkCreateInfo ci;
    GPUtimer gpuTimer;
    PerfGraph fps, cpuGraph, gpuGraph;
    double prevt = 0, cpuTime = 0;
    int testCount = 0, testRemaining, testRequested = 0, success = 1, swapchainOk = 1, i;
    int fbWidth = 0, fbHeight = 0;
    long long frameCounter = 0;
    const int sampleRate = 5;
    uint32_t frame = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--test")) {
            if (i + 1 >= argc || !parseTestCount(argv[i + 1], &testCount)) {
                printf("Invalid --test N value.\n");
                return -1;
            }
            testRequested = 1;
            i++;
        } else if (!strncmp(argv[i], "--test=", 7)) {
            if (!parseTestCount(argv[i] + 7, &testCount)) {
                printf("Invalid --test N value.\n");
                return -1;
            }
            testRequested = 1;
        }
    }
    testRemaining = testCount;

    if (!glfwInit()) {
        printf("Failed to init GLFW.\n");
        return -1;
    }
    glfwSetErrorCallback(errorcb);

    initGraph(&fps, GRAPH_RENDER_FPS, "Frame Time");
    initGraph(&cpuGraph, GRAPH_RENDER_MS, "CPU Time");
    initGraph(&gpuGraph, GRAPH_RENDER_MS, "GPU Time");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(1200, 1000, "NanoVG (Vulkan, C API)", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwSetKeyCallback(window, key);

    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    if (!createInstance(&app) || !vkOk(glfwCreateWindowSurface(app.instance, window, NULL, &app.surface), "glfwCreateWindowSurface") || !createDevice(&app) || !chooseFormats(&app) ||
        !createRenderPass(&app) || !createSwapchain(&app, fbWidth, fbHeight) || !createFrameResources(&app)) {
        printf("Could not init Vulkan.\n");
        destroyApp(&app);
        glfwTerminate();
        return -1;
    }

    memset(&ci, 0, sizeof(ci));
    ci.instance = app.instance;
    ci.physicalDevice = app.gpu;
    ci.device = app.device;
    ci.queue = app.queue;
    ci.queueFamilyIndex = app.queueFamily;
    ci.renderPass = app.renderPass;
    ci.framesInFlight = FRAMES_IN_FLIGHT;
    ci.getInstanceProcAddr = getInstanceProcAddr;
    vg = nvgCreateVk(&ci, NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
    if (vg == NULL) {
        printf("Could not init nanovg.\n");
        destroyApp(&app);
        glfwTerminate();
        return -1;
    }

    if (loadDemoData(vg, &data) == -1) return -1;
    if (testRequested && testCount == 0) glfwSetWindowShouldClose(window, GLFW_TRUE);

    initGPUTimer(&gpuTimer);
    glfwSetTime(0);
    prevt = testRequested ? 0.0 : glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        double mx, my, t, dt;
        int winWidth, winHeight, captureTest, captureKey, capture;
        uint32_t imageIndex = 0;
        float pxRatio;
        VkResult res;
        VkCommandBuffer cmd;
        VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VkClearValue clears[2];
        VkRenderPassBeginInfo rpbi = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};

        glfwPollEvents();
        glfwGetWindowSize(window, &winWidth, &winHeight);
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        if (!swapchainOk || (uint32_t)fbWidth != app.extent.width || (uint32_t)fbHeight != app.extent.height) {
            swapchainOk = recreateSwapchain(&app, window);
            if (!swapchainOk) continue;  // minimized
        }

        // Wait for this slot's previous frame; afterwards its command buffer and NanoVG's resources for it are free.
        vkWaitForFences(app.device, 1, &app.fences[frame], VK_TRUE, UINT64_MAX);
        res = vkAcquireNextImageKHR(app.device, app.swapchain, UINT64_MAX, app.imageAvailable[frame], VK_NULL_HANDLE, &imageIndex);
        if (res == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchainOk = 0;
            continue;
        }
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
            printf("vkAcquireNextImageKHR failed (%d)\n", (int)res);
            success = 0;
            break;
        }
        vkResetFences(app.device, 1, &app.fences[frame]);

        t = testRequested ? frameCounter / 30.0 : glfwGetTime();
        dt = t - prevt;
        prevt = t;
        if (testRequested) {
            double angle = 2.0 * NVG_PI * testRemaining / (double)testCount;
            mx = winWidth / 2.0f + cos(angle) * winWidth / 4.0f;
            my = winHeight / 2.0f + sin(angle) * winHeight / 4.0f;
        } else {
            glfwGetCursorPos(window, &mx, &my);
        }
        pxRatio = (float)app.extent.width / (float)winWidth;

        captureTest = testRemaining > 0 && frameCounter % sampleRate == 0;
        captureKey = screenshot && !testRequested;
        capture = (captureTest || captureKey) && app.canReadback && ensureReadback(&app, (VkDeviceSize)app.extent.width * app.extent.height * 4);

        cmd = app.cmds[frame];
        vkResetCommandBuffer(cmd, 0);
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        memset(clears, 0, sizeof(clears));
        if (!premult) {
            clears[0].color.float32[0] = 0.3f;
            clears[0].color.float32[1] = 0.3f;
            clears[0].color.float32[2] = 0.32f;
            clears[0].color.float32[3] = 1.0f;
        }
        clears[1].depthStencil.depth = 1.0f;
        rpbi.renderPass = app.renderPass;
        rpbi.framebuffer = app.framebuffers[imageIndex];
        rpbi.renderArea.extent = app.extent;
        rpbi.clearValueCount = 2;
        rpbi.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        nvgBeginFrameVk(vg, cmd, frame);
        nvgBeginFrame(vg, (float)winWidth, (float)winHeight, pxRatio);
        renderDemo(vg, (float)mx, (float)my, (float)winWidth, (float)winHeight, (float)t, blowup, &data);
        if (!testRequested) {
            renderGraph(vg, 5, 5, &fps);
            renderGraph(vg, 5 + 200 + 5, 5, &cpuGraph);
            if (gpuTimer.supported) renderGraph(vg, 5 + 200 + 5 + 200 + 5, 5, &gpuGraph);
        }
        nvgEndFrame(vg);

        vkCmdEndRenderPass(cmd);
        if (capture) recordReadback(&app, cmd, app.swapImages[imageIndex]);
        vkEndCommandBuffer(cmd);

        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &app.imageAvailable[frame];
        si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &app.renderFinished[imageIndex];
        if (!vkOk(vkQueueSubmit(app.queue, 1, &si, app.fences[frame]), "vkQueueSubmit")) {
            success = 0;
            break;
        }

        // Measure the CPU time taken excluding presentation (as it may wait for the GPU)
        cpuTime = glfwGetTime() - t;
        updateGraph(&fps, (float)dt);
        updateGraph(&cpuGraph, (float)cpuTime);

        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &app.renderFinished[imageIndex];
        pi.swapchainCount = 1;
        pi.pSwapchains = &app.swapchain;
        pi.pImageIndices = &imageIndex;
        res = vkQueuePresentKHR(app.queue, &pi);
        if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) swapchainOk = 0;

        if (captureTest || captureKey) {
            int ok = capture;
            char fileName[64] = "dump.png";
            if (captureTest) {
                printf("Capturing %d/%d at t = %0.2f sec\n", testCount - testRemaining + 1, testCount, t);
                snprintf(fileName, sizeof(fileName), "screenshot%03d.png", testCount - testRemaining + 1);
            }
            if (ok) {
                unsigned char* px;
                vkWaitForFences(app.device, 1, &app.fences[frame], VK_TRUE, UINT64_MAX);
                px = readbackPixels(&app);
                ok = px != NULL && saveScreenShotRGBA((int)app.extent.width, (int)app.extent.height, captureTest ? 0 : premult, px, fileName, captureTest);
                free(px);
            } else {
                printf("Screenshot not supported: swapchain images cannot be copied on this device.\n");
            }
            success &= ok;
            if (captureTest) {
                testRemaining--;
                if (testRemaining == 0) glfwSetWindowShouldClose(window, GLFW_TRUE);
            } else {
                screenshot = 0;
            }
        }

        frameCounter++;
        frame = (frame + 1) % FRAMES_IN_FLIGHT;
    }

    vkDeviceWaitIdle(app.device);
    freeDemoData(vg, &data);
    nvgDeleteVk(vg);

    if (testRequested) {
        printf(success ? "Test passed!\n" : "Test failed!\n");
    } else {
        printf("Average Frame Time: %.2f ms\n", getGraphAverage(&fps) * 1000.0f);
        printf("          CPU Time: %.2f ms\n", getGraphAverage(&cpuGraph) * 1000.0f);
    }

    destroyApp(&app);
    glfwDestroyWindow(window);
    glfwTerminate();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
