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
// Vulkan backend for NanoVG++. Header-only in the style of nanovg_gl.hpp: include it
// everywhere you need the declarations, and in exactly one translation unit
//
//     #define NANOVG_VK_IMPLEMENTATION
//     #include "nanovg_vk.hpp"
//
// The backend resolves every Vulkan entry point it uses through the
// vkGetInstanceProcAddr you pass in VkCreateInfo, so it never needs to link against
// the Vulkan loader and works with VK_NO_PROTOTYPES.
//
// Frame protocol (per frame-in-flight slot `i`, see VkCreateInfo::framesInFlight):
//   1. Wait until the GPU has finished the work you last submitted for slot `i`
//      (the usual per-frame fence wait).
//   2. Begin your command buffer and the render pass passed at creation.
//   3. nvg::beginFrameVk(ctx, cmd, i);
//   4. ctx.beginFrame(...); draw ...; ctx.endFrame();   // draws are recorded into cmd
//   5. End the render pass / command buffer and submit it on VkCreateInfo::queue.
//
// Texture uploads are recorded into a command buffer owned by the backend and submitted
// on the same queue before your frame's command buffer, so textures created or updated
// at any point before ctx.endFrame() are visible to that frame's draws. Resources the
// GPU may still be reading (deleted images, outgrown buffers) are released when their
// frame slot comes around again.
//
// The render target needs a stencil attachment (cleared to 0 at the start of the pass).
//
#pragma once

#include "nanovg.hpp"

#include <cstdint>
#include <memory>

#include <vulkan/vulkan.h>

namespace nvg
{

struct VkCreateInfo {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    // Queue used for texture uploads. Must be the queue the frame command buffers are submitted to.
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;
    // Render pass (and subpass) the NanoVG draws are recorded in. Needs a color and a stencil attachment.
    VkRenderPass renderPass = VK_NULL_HANDLE;
    uint32_t subpass = 0;
    VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    // Number of frames the application keeps in flight; frameIndex passed to beginFrameVk is in [0, framesInFlight).
    uint32_t framesInFlight = 2;
    // Required. Used to resolve all instance and device level functions.
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    const VkAllocationCallbacks* allocator = nullptr;
};

// Creates a NanoVG context rendering through Vulkan. flags is a combination of nvg::CreateFlags.
// Returns nullptr on failure.
std::unique_ptr<Context> createVk(const VkCreateInfo& createInfo, int flags);
void deleteVk(std::unique_ptr<Context>& ctx);

// Selects the command buffer that the next ctx.endFrame() records into and the frame-in-flight slot it belongs to.
// Call once per frame, after waiting for the previous use of frameIndex and before ctx.beginFrame().
void beginFrameVk(Context& ctx, VkCommandBuffer commandBuffer, uint32_t frameIndex);

// Wraps an application owned image view so it can be used with imagePattern() etc. NanoVG never destroys the
// view or sampler. The image must be in `layout` whenever NanoVG draws with it. Pass VK_NULL_HANDLE as sampler
// to use NanoVG's sampler matching imageFlags.
int createImageFromHandleVk(Context& ctx, VkImageView view, VkSampler sampler, VkImageLayout layout, int w, int h, int imageFlags);
VkImageView imageHandleVk(Context& ctx, int image);

}  // namespace nvg

#ifdef NANOVG_VK_IMPLEMENTATION

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

namespace nvg
{
namespace vkdetail
{

// clang-format off
#define NVGVK_INSTANCE_FUNCS(X) \
    X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceFormatProperties) \
    X(GetDeviceProcAddr)

#define NVGVK_DEVICE_FUNCS(X) \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) \
    X(AllocateMemory) X(FreeMemory) X(MapMemory) X(UnmapMemory) \
    X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) \
    X(CreateImageView) X(DestroyImageView) X(CreateSampler) X(DestroySampler) \
    X(CreateShaderModule) X(DestroyShaderModule) \
    X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
    X(CreatePipelineCache) X(DestroyPipelineCache) X(CreateGraphicsPipelines) X(DestroyPipeline) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(FreeDescriptorSets) \
    X(UpdateDescriptorSets) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(FreeCommandBuffers) \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(ResetCommandBuffer) \
    X(CreateFence) X(DestroyFence) X(WaitForFences) X(ResetFences) X(QueueSubmit) X(DeviceWaitIdle) \
    X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdBindVertexBuffers) X(CmdPushConstants) X(CmdDraw) \
    X(CmdSetViewport) X(CmdSetScissor) X(CmdPipelineBarrier) X(CmdCopyBufferToImage) X(CmdClearColorImage) \
    X(CmdBlitImage)
// clang-format on

struct Functions {
#define NVGVK_DECLARE(name) PFN_vk##name name = nullptr;
    NVGVK_INSTANCE_FUNCS(NVGVK_DECLARE)
    NVGVK_DEVICE_FUNCS(NVGVK_DECLARE)
#undef NVGVK_DECLARE
};

enum ShaderType { ShaderFillGrad, ShaderFillImg, ShaderSimple, ShaderImg };

enum CallType { CallNone = 0, CallFill, CallConvexFill, CallStroke, CallTriangles };

// How a draw interacts with the stencil buffer. Mirrors the GL state sequences in nanovg_gl.hpp.
enum StencilMode : uint32_t {
    StencilNone = 0,     // no stencil test
    StencilFillShapes,   // ALWAYS, front INCR_WRAP / back DECR_WRAP, no color, no culling
    StencilEqualZero,    // EQUAL 0, KEEP (fill AA fringes, stroke AA pass)
    StencilFillCover,    // NOTEQUAL 0, ZERO
    StencilStrokeBase,   // EQUAL 0, INCR_AND_CLAMP
    StencilStrokeClear,  // ALWAYS, ZERO, no color
};

// Must match FragUniforms in shaders/nanovg_vk.frag (std430).
struct FragUniforms {
    float scissorMat[12];  // matrices are actually 3 vec4s
    float paintMat[12];
    Color innerCol;
    Color outerCol;
    float scissorExt[2];
    float scissorScale[2];
    float extent[2];
    float radius;
    float feather;
    float strokeMult;
    float strokeThr;
    int lineStyle;
    int texType;
    int type;
    int shapeType;  // ShapeType; 0 = none
    float shapeAA;
    float shapeStrokeHalf;
    float shapeMat[12];  // device -> shape space (3 vec4 columns)
    float shapeRect[4];  // Shape::params[0..3]
    float shapeGrid[4];  // Shape::params[4..7]
};
static_assert(sizeof(FragUniforms) == 272, "FragUniforms must match the std430 array stride of the shader struct");
static_assert(offsetof(FragUniforms, shapeMat) == 192, "mat3 in std430 is 16-byte aligned");

struct PushConstants {
    float viewSize[2];
    int32_t uniformIndex;
    int32_t pad;
};

struct Blend {
    VkBlendFactor srcRGB;
    VkBlendFactor dstRGB;
    VkBlendFactor srcAlpha;
    VkBlendFactor dstAlpha;
};

struct Call {
    int type;
    int image;
    int pathOffset;
    int pathCount;
    int triangleOffset;
    int triangleCount;
    int uniformOffset;  // in FragUniforms elements
    Blend blend;
};

struct DrawPath {
    int fillOffset;
    int fillCount;
    int strokeOffset;
    int strokeCount;
};

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};

struct TextureVk {
    int id = 0;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;  // owned by the sampler cache or by the application
    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorPool setPool = VK_NULL_HANDLE;
    int width = 0, height = 0;
    int type = 0;
    int flags = 0;
    uint32_t mipLevels = 1;
    bool external = false;
};

// Objects that may still be referenced by work in flight; released once their frame slot is reused.
struct Garbage {
    std::vector<VkBuffer> buffers;
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkDeviceMemory> memory;
    std::vector<std::pair<VkDescriptorPool, VkDescriptorSet>> sets;
};

struct FrameData {
    VkCommandBuffer cmd = VK_NULL_HANDLE;  // application command buffer for this frame

    Buffer vertices;
    VkDeviceSize verticesUsed = 0;
    Buffer uniforms;
    VkDeviceSize uniformsUsed = 0;
    VkDescriptorSet uniformSet = VK_NULL_HANDLE;
    VkDescriptorPool uniformSetPool = VK_NULL_HANDLE;

    VkCommandBuffer uploadCmd = VK_NULL_HANDLE;
    VkFence uploadFence = VK_NULL_HANDLE;
    bool uploadRecording = false;
    bool uploadInFlight = false;
    Buffer staging;
    VkDeviceSize stagingUsed = 0;

    Garbage garbage;
};

struct VkContext {
    VkCreateInfo info{};
    int flags = 0;
    Functions vk;
    VkPhysicalDeviceProperties props{};
    VkPhysicalDeviceMemoryProperties memProps{};
    bool canGenerateMips[2] = {false, false};  // [Alpha, Rgba]

    VkShaderModule vertShader = VK_NULL_HANDLE;
    VkShaderModule fragShader = VK_NULL_HANDLE;
    VkDescriptorSetLayout uniformSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout textureSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    std::map<uint64_t, VkPipeline> pipelines;
    std::map<int, VkSampler> samplers;
    std::vector<VkDescriptorPool> descriptorPools;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    std::vector<FrameData> frames;
    uint32_t frameIndex = 0;

    std::vector<TextureVk> textures;
    int textureId = 0;
    int dummyTex = 0;

    float view[2] = {0.0f, 0.0f};
    float devicePixelRatio = 1.0f;

    // Per frame buffers
    std::vector<Call> calls;
    std::vector<DrawPath> paths;
    std::vector<Vertex> verts;
    std::vector<FragUniforms> uniforms;

    // Bind state during flush
    VkPipeline boundPipeline = VK_NULL_HANDLE;
    VkDescriptorSet boundTextureSet = VK_NULL_HANDLE;
    int32_t boundUniformBase = 0;
};

static const uint32_t kVertShaderCode[] = {
#include "shaders/nanovg_vk_vert.spv.inc"
};

static const uint32_t kFragShaderCode[] = {
#include "shaders/nanovg_vk_frag.spv.inc"
};

constexpr uint32_t kSetsPerPool = 256;

static bool check(VkContext* vk, VkResult res, const char* what)
{
    if (res == VK_SUCCESS) return true;
    if (vk->flags & CreateFlags::Debug) printf("nanovg vulkan: %s failed (VkResult %d)\n", what, static_cast<int>(res));
    return false;
}

static FrameData& currentFrame(VkContext* vk)
{
    return vk->frames[vk->frameIndex];
}

static bool loadFunctions(VkContext* vk)
{
    PFN_vkGetInstanceProcAddr gipa = vk->info.getInstanceProcAddr;
#define NVGVK_LOAD_INSTANCE(name)                                                                            \
    vk->vk.name = reinterpret_cast<PFN_vk##name>(gipa(vk->info.instance, "vk" #name));                       \
    if (vk->vk.name == nullptr) {                                                                            \
        printf("nanovg vulkan: could not load vk" #name "\n");                                               \
        return false;                                                                                        \
    }
    NVGVK_INSTANCE_FUNCS(NVGVK_LOAD_INSTANCE)
#undef NVGVK_LOAD_INSTANCE
#define NVGVK_LOAD_DEVICE(name)                                                                              \
    vk->vk.name = reinterpret_cast<PFN_vk##name>(vk->vk.GetDeviceProcAddr(vk->info.device, "vk" #name));     \
    if (vk->vk.name == nullptr) {                                                                            \
        printf("nanovg vulkan: could not load vk" #name "\n");                                               \
        return false;                                                                                        \
    }
    NVGVK_DEVICE_FUNCS(NVGVK_LOAD_DEVICE)
#undef NVGVK_LOAD_DEVICE
    return true;
}

static bool findMemoryType(VkContext* vk, uint32_t typeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, uint32_t* index)
{
    for (int pass = 0; pass < 2; pass++) {
        const VkMemoryPropertyFlags want = pass == 0 ? (required | preferred) : required;
        for (uint32_t i = 0; i < vk->memProps.memoryTypeCount; i++) {
            if ((typeBits & (1u << i)) && (vk->memProps.memoryTypes[i].propertyFlags & want) == want) {
                *index = i;
                return true;
            }
        }
    }
    return false;
}

//
// Deferred destruction
//

static void releaseGarbage(VkContext* vk, Garbage& g)
{
    const VkDevice dev = vk->info.device;
    const VkAllocationCallbacks* alloc = vk->info.allocator;
    for (auto& [pool, set] : g.sets) vk->vk.FreeDescriptorSets(dev, pool, 1, &set);
    for (VkImageView v : g.views) vk->vk.DestroyImageView(dev, v, alloc);
    for (VkImage i : g.images) vk->vk.DestroyImage(dev, i, alloc);
    for (VkBuffer b : g.buffers) vk->vk.DestroyBuffer(dev, b, alloc);
    for (VkDeviceMemory m : g.memory) vk->vk.FreeMemory(dev, m, alloc);
    g = Garbage{};
}

static void retireBuffer(VkContext* vk, Buffer& buf)
{
    Garbage& g = currentFrame(vk).garbage;
    if (buf.buffer != VK_NULL_HANDLE) g.buffers.push_back(buf.buffer);
    if (buf.memory != VK_NULL_HANDLE) g.memory.push_back(buf.memory);  // unmapped implicitly by vkFreeMemory
    buf = Buffer{};
}

static void destroyBufferNow(VkContext* vk, Buffer& buf)
{
    if (buf.buffer != VK_NULL_HANDLE) vk->vk.DestroyBuffer(vk->info.device, buf.buffer, vk->info.allocator);
    if (buf.memory != VK_NULL_HANDLE) vk->vk.FreeMemory(vk->info.device, buf.memory, vk->info.allocator);
    buf = Buffer{};
}

//
// Buffers and descriptor sets
//

static bool createHostBuffer(VkContext* vk, VkDeviceSize size, VkBufferUsageFlags usage, Buffer* out)
{
    const VkDevice dev = vk->info.device;
    Buffer buf;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!check(vk, vk->vk.CreateBuffer(dev, &bci, vk->info.allocator, &buf.buffer), "vkCreateBuffer")) return false;

    VkMemoryRequirements req;
    vk->vk.GetBufferMemoryRequirements(dev, buf.buffer, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    // Coherent host memory; prefer device local (ReBAR / UMA) when available.
    const VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const VkMemoryPropertyFlags preferred = (usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ? 0 : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (!findMemoryType(vk, req.memoryTypeBits, required, preferred, &mai.memoryTypeIndex) || !check(vk, vk->vk.AllocateMemory(dev, &mai, vk->info.allocator, &buf.memory), "vkAllocateMemory")) {
        destroyBufferNow(vk, buf);
        return false;
    }
    if (!check(vk, vk->vk.BindBufferMemory(dev, buf.buffer, buf.memory, 0), "vkBindBufferMemory") || !check(vk, vk->vk.MapMemory(dev, buf.memory, 0, VK_WHOLE_SIZE, 0, &buf.mapped), "vkMapMemory")) {
        destroyBufferNow(vk, buf);
        return false;
    }
    buf.size = size;
    *out = buf;
    return true;
}

static bool allocateDescriptorSet(VkContext* vk, VkDescriptorSetLayout layout, VkDescriptorSet* set, VkDescriptorPool* pool)
{
    const VkDevice dev = vk->info.device;
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;
    if (!vk->descriptorPools.empty()) {
        ai.descriptorPool = vk->descriptorPools.back();
        if (vk->vk.AllocateDescriptorSets(dev, &ai, set) == VK_SUCCESS) {
            *pool = ai.descriptorPool;
            return true;
        }
    }
    // Pools fill up or fragment; just start another one.
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSetsPerPool},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kSetsPerPool / 4},
    };
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pci.maxSets = kSetsPerPool;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = sizes;
    VkDescriptorPool newPool;
    if (!check(vk, vk->vk.CreateDescriptorPool(dev, &pci, vk->info.allocator, &newPool), "vkCreateDescriptorPool")) return false;
    vk->descriptorPools.push_back(newPool);
    ai.descriptorPool = newPool;
    if (!check(vk, vk->vk.AllocateDescriptorSets(dev, &ai, set), "vkAllocateDescriptorSets")) return false;
    *pool = newPool;
    return true;
}

// Makes sure buf has `bytes` free past `used`. A buffer that is too small is retired (frames already recorded
// this slot may still read it) and replaced by a larger one, so offsets handed out earlier stay valid.
static bool reserveFrameBuffer(VkContext* vk, Buffer& buf, VkDeviceSize& used, VkDeviceSize bytes, VkBufferUsageFlags usage, bool* replaced)
{
    *replaced = false;
    if (buf.buffer != VK_NULL_HANDLE && used + bytes <= buf.size) return true;
    VkDeviceSize size = std::max<VkDeviceSize>(64 * 1024, buf.size * 2);
    while (size < bytes) size *= 2;
    retireBuffer(vk, buf);
    used = 0;
    if (!createHostBuffer(vk, size, usage, &buf)) return false;
    *replaced = true;
    return true;
}

//
// Uploads
//

static bool beginUploads(VkContext* vk)
{
    FrameData& f = currentFrame(vk);
    const VkDevice dev = vk->info.device;
    if (f.uploadRecording) return true;
    if (f.uploadInFlight) {
        // Only happens when uploading again after this slot's uploads were submitted within the same frame.
        vk->vk.WaitForFences(dev, 1, &f.uploadFence, VK_TRUE, UINT64_MAX);
        f.uploadInFlight = false;
        f.stagingUsed = 0;
    }
    if (f.uploadCmd == VK_NULL_HANDLE) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = vk->commandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (!check(vk, vk->vk.AllocateCommandBuffers(dev, &ai, &f.uploadCmd), "vkAllocateCommandBuffers")) return false;
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (!check(vk, vk->vk.CreateFence(dev, &fci, vk->info.allocator, &f.uploadFence), "vkCreateFence")) return false;
    } else {
        vk->vk.ResetCommandBuffer(f.uploadCmd, 0);
    }
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(vk, vk->vk.BeginCommandBuffer(f.uploadCmd, &bi), "vkBeginCommandBuffer")) return false;
    f.uploadRecording = true;
    return true;
}

static void submitUploads(VkContext* vk)
{
    FrameData& f = currentFrame(vk);
    if (!f.uploadRecording) return;
    f.uploadRecording = false;
    vk->vk.EndCommandBuffer(f.uploadCmd);
    vk->vk.ResetFences(vk->info.device, 1, &f.uploadFence);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.uploadCmd;
    if (check(vk, vk->vk.QueueSubmit(vk->info.queue, 1, &si, f.uploadFence), "vkQueueSubmit")) f.uploadInFlight = true;
}

static void imageBarrier(VkContext* vk, VkCommandBuffer cmd, VkImage image, uint32_t baseMip, uint32_t mipCount, VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags srcAccess,
                         VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, 0, 1};
    vk->vk.CmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// Records an update of the rectangle (x, y, w, h) of tex. `data` points at the full image (row stride
// tex.width), as in the NanoVG render API. A null `data` clears the whole image to zero. When `initial` is set the
// previous contents are undefined. Mip levels are regenerated after the write.
static bool recordTextureWrite(VkContext* vk, TextureVk& tex, int x, int y, int w, int h, const unsigned char* data, bool initial)
{
    if (!beginUploads(vk)) return false;
    FrameData& f = currentFrame(vk);
    const VkCommandBuffer cmd = f.uploadCmd;
    const VkDeviceSize comp = tex.type == static_cast<int>(Texture::Rgba) ? 4 : 1;

    const VkImageLayout oldLayout = initial ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkPipelineStageFlags oldStage = initial ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    imageBarrier(vk, cmd, tex.image, 0, tex.mipLevels, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT, oldStage, VK_PIPELINE_STAGE_TRANSFER_BIT);

    if (data == nullptr) {
        const VkClearColorValue zero{};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, tex.mipLevels, 0, 1};
        vk->vk.CmdClearColorImage(cmd, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    } else {
        const VkDeviceSize rowBytes = static_cast<VkDeviceSize>(w) * comp;
        const VkDeviceSize bytes = rowBytes * static_cast<VkDeviceSize>(h);
        VkDeviceSize offset = (f.stagingUsed + 15) & ~VkDeviceSize(15);
        if (f.staging.buffer == VK_NULL_HANDLE || offset + bytes > f.staging.size) {
            // The upload command buffer may already reference the old staging buffer; retire it instead of freeing it.
            VkDeviceSize size = std::max<VkDeviceSize>(256 * 1024, f.staging.size * 2);
            while (size < bytes) size *= 2;
            retireBuffer(vk, f.staging);
            if (!createHostBuffer(vk, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &f.staging)) return false;
            offset = 0;
        }
        unsigned char* dst = static_cast<unsigned char*>(f.staging.mapped) + offset;
        for (int row = 0; row < h; row++) {
            const unsigned char* src = data + ((static_cast<VkDeviceSize>(y + row) * tex.width) + x) * comp;
            std::memcpy(dst + row * rowBytes, src, rowBytes);
        }
        f.stagingUsed = offset + bytes;

        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {x, y, 0};
        region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        vk->vk.CmdCopyBufferToImage(cmd, f.staging.buffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }

    if (tex.mipLevels > 1 && data != nullptr) {
        int32_t mw = tex.width, mh = tex.height;
        for (uint32_t level = 1; level < tex.mipLevels; level++) {
            imageBarrier(vk, cmd, tex.image, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            const int32_t nw = std::max(1, mw / 2), nh = std::max(1, mh / 2);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
            blit.srcOffsets[1] = {mw, mh, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
            blit.dstOffsets[1] = {nw, nh, 1};
            vk->vk.CmdBlitImage(cmd, tex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
            mw = nw;
            mh = nh;
        }
        imageBarrier(vk, cmd, tex.image, 0, tex.mipLevels - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        imageBarrier(vk, cmd, tex.image, tex.mipLevels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    } else {
        imageBarrier(vk, cmd, tex.image, 0, tex.mipLevels, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    return true;
}

//
// Textures
//

static TextureVk* findTexture(VkContext* vk, int id)
{
    for (TextureVk& t : vk->textures)
        if (t.id == id && id != 0) return &t;
    return nullptr;
}

static TextureVk* allocTexture(VkContext* vk)
{
    TextureVk* tex = nullptr;
    for (TextureVk& t : vk->textures) {
        if (t.id == 0) {
            tex = &t;
            break;
        }
    }
    if (tex == nullptr) {
        vk->textures.emplace_back();
        tex = &vk->textures.back();
    }
    *tex = TextureVk{};
    tex->id = ++vk->textureId;
    return tex;
}

static VkSampler getSampler(VkContext* vk, int imageFlags, uint32_t mipLevels)
{
    const int key = (imageFlags & (to_underlying(ImageFlags::Nearest) | to_underlying(ImageFlags::RepeatX) | to_underlying(ImageFlags::RepeatY))) | (mipLevels > 1 ? 1 << 30 : 0);
    auto it = vk->samplers.find(key);
    if (it != vk->samplers.end()) return it->second;

    const bool nearest = (imageFlags & ImageFlags::Nearest) != 0;
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    sci.minFilter = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    sci.mipmapMode = nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = (imageFlags & ImageFlags::RepeatX) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = (imageFlags & ImageFlags::RepeatY) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxAnisotropy = 1.0f;
    sci.compareOp = VK_COMPARE_OP_NEVER;
    sci.minLod = 0.0f;
    sci.maxLod = mipLevels > 1 ? VK_LOD_CLAMP_NONE : 0.0f;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    VkSampler sampler = VK_NULL_HANDLE;
    if (!check(vk, vk->vk.CreateSampler(vk->info.device, &sci, vk->info.allocator, &sampler), "vkCreateSampler")) return VK_NULL_HANDLE;
    vk->samplers[key] = sampler;
    return sampler;
}

static bool writeTextureSet(VkContext* vk, TextureVk& tex)
{
    if (!allocateDescriptorSet(vk, vk->textureSetLayout, &tex.set, &tex.setPool)) return false;
    VkDescriptorImageInfo ii{};
    ii.sampler = tex.sampler;
    ii.imageView = tex.view;
    ii.imageLayout = tex.layout;
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = tex.set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vk->vk.UpdateDescriptorSets(vk->info.device, 1, &w, 0, nullptr);
    return true;
}

static void retireTexture(VkContext* vk, TextureVk& tex)
{
    Garbage& g = currentFrame(vk).garbage;
    if (tex.set != VK_NULL_HANDLE) g.sets.emplace_back(tex.setPool, tex.set);
    if (!tex.external) {
        if (tex.view != VK_NULL_HANDLE) g.views.push_back(tex.view);
        if (tex.image != VK_NULL_HANDLE) g.images.push_back(tex.image);
        if (tex.memory != VK_NULL_HANDLE) g.memory.push_back(tex.memory);
    }
    tex = TextureVk{};
}

static int renderCreateTexture(void* uptr, int type, int w, int h, int imageFlags, const unsigned char* data)
{
    VkContext* vk = static_cast<VkContext*>(uptr);
    const VkDevice dev = vk->info.device;
    const VkAllocationCallbacks* alloc = vk->info.allocator;
    if (w <= 0 || h <= 0) return 0;

    TextureVk* tex = allocTexture(vk);
    tex->width = w;
    tex->height = h;
    tex->type = type;
    tex->flags = imageFlags;
    const bool rgba = type == static_cast<int>(Texture::Rgba);
    if ((imageFlags & ImageFlags::GenerateMipmaps) && vk->canGenerateMips[rgba ? 1 : 0]) {
        tex->mipLevels = static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(w, h))))) + 1;
    }

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = rgba ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8_UNORM;
    ici.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    ici.mipLevels = tex->mipLevels;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | (tex->mipLevels > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool ok = check(vk, vk->vk.CreateImage(dev, &ici, alloc, &tex->image), "vkCreateImage");

    if (ok) {
        VkMemoryRequirements req;
        vk->vk.GetImageMemoryRequirements(dev, tex->image, &req);
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = req.size;
        ok = findMemoryType(vk, req.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mai.memoryTypeIndex) && check(vk, vk->vk.AllocateMemory(dev, &mai, alloc, &tex->memory), "vkAllocateMemory") &&
             check(vk, vk->vk.BindImageMemory(dev, tex->image, tex->memory, 0), "vkBindImageMemory");
    }
    if (ok) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = tex->image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = ici.format;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, tex->mipLevels, 0, 1};
        ok = check(vk, vk->vk.CreateImageView(dev, &vci, alloc, &tex->view), "vkCreateImageView");
    }
    if (ok) {
        tex->sampler = getSampler(vk, imageFlags, tex->mipLevels);
        ok = tex->sampler != VK_NULL_HANDLE && writeTextureSet(vk, *tex) && recordTextureWrite(vk, *tex, 0, 0, w, h, data, true);
    }
    if (!ok) {
        // Nothing can reference the image yet, but keep the release path uniform.
        retireTexture(vk, *tex);
        return 0;
    }
    return tex->id;
}

static int renderDeleteTexture(void* uptr, int image)
{
    VkContext* vk = static_cast<VkContext*>(uptr);
    TextureVk* tex = findTexture(vk, image);
    if (tex == nullptr) return 0;
    retireTexture(vk, *tex);
    return 1;
}

static int renderUpdateTexture(void* uptr, int image, int x, int y, int w, int h, const unsigned char* data)
{
    VkContext* vk = static_cast<VkContext*>(uptr);
    TextureVk* tex = findTexture(vk, image);
    if (tex == nullptr || tex->external || data == nullptr) return 0;
    x = std::clamp(x, 0, tex->width);
    y = std::clamp(y, 0, tex->height);
    w = std::min(w, tex->width - x);
    h = std::min(h, tex->height - y);
    if (w <= 0 || h <= 0) return 1;
    return recordTextureWrite(vk, *tex, x, y, w, h, data, false) ? 1 : 0;
}

static int renderGetTextureSize(void* uptr, int image, int* w, int* h)
{
    VkContext* vk = static_cast<VkContext*>(uptr);
    TextureVk* tex = findTexture(vk, image);
    if (tex == nullptr) return 0;
    *w = tex->width;
    *h = tex->height;
    return 1;
}

static int renderGetImageTextureId(void* uptr, int handle)
{
    // Vulkan handles do not fit in an int; use nvg::imageHandleVk() instead.
    VkContext* vk = static_cast<VkContext*>(uptr);
    return findTexture(vk, handle) != nullptr ? handle : -1;
}

//
// Pipelines
//

static uint64_t pipelineKey(VkPrimitiveTopology topology, StencilMode mode, const Blend& blend)
{
    return static_cast<uint64_t>(topology) | (static_cast<uint64_t>(mode) << 4) | (static_cast<uint64_t>(blend.srcRGB) << 8) | (static_cast<uint64_t>(blend.dstRGB) << 16) |
           (static_cast<uint64_t>(blend.srcAlpha) << 24) | (static_cast<uint64_t>(blend.dstAlpha) << 32);
}

static VkStencilOpState stencilOp(VkCompareOp compare, VkStencilOp pass, VkStencilOp fail)
{
    VkStencilOpState s{};
    s.failOp = fail;
    s.passOp = pass;
    s.depthFailOp = fail;
    s.compareOp = compare;
    s.compareMask = 0xff;
    s.writeMask = 0xff;
    s.reference = 0;
    return s;
}

static VkPipeline createPipeline(VkContext* vk, VkPrimitiveTopology topology, StencilMode mode, const Blend& blend)
{
    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, x)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, u)},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = topology;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = mode == StencilFillShapes ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = vk->info.rasterizationSamples;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    ds.stencilTestEnable = mode != StencilNone ? VK_TRUE : VK_FALSE;
    switch (mode) {
        case StencilNone:
            ds.front = ds.back = stencilOp(VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP);
            break;
        case StencilFillShapes:
            ds.front = stencilOp(VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_INCREMENT_AND_WRAP, VK_STENCIL_OP_KEEP);
            ds.back = stencilOp(VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_DECREMENT_AND_WRAP, VK_STENCIL_OP_KEEP);
            break;
        case StencilEqualZero:
            ds.front = ds.back = stencilOp(VK_COMPARE_OP_EQUAL, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP);
            break;
        case StencilFillCover:
            ds.front = ds.back = stencilOp(VK_COMPARE_OP_NOT_EQUAL, VK_STENCIL_OP_ZERO, VK_STENCIL_OP_ZERO);
            break;
        case StencilStrokeBase:
            ds.front = ds.back = stencilOp(VK_COMPARE_OP_EQUAL, VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_STENCIL_OP_KEEP);
            break;
        case StencilStrokeClear:
            ds.front = ds.back = stencilOp(VK_COMPARE_OP_ALWAYS, VK_STENCIL_OP_ZERO, VK_STENCIL_OP_ZERO);
            break;
    }

    VkPipelineColorBlendAttachmentState att{};
    att.blendEnable = VK_TRUE;
    att.srcColorBlendFactor = blend.srcRGB;
    att.dstColorBlendFactor = blend.dstRGB;
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = blend.srcAlpha;
    att.dstAlphaBlendFactor = blend.dstAlpha;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
    const bool writesColor = mode != StencilFillShapes && mode != StencilStrokeClear;
    att.colorWriteMask = writesColor ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT) : 0;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &att;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynamicStates;

    const int32_t edgeAA = (vk->flags & CreateFlags::Antialias) ? 1 : 0;
    VkSpecializationMapEntry entry{0, 0, sizeof(edgeAA)};
    VkSpecializationInfo spec{1, &entry, sizeof(edgeAA), &edgeAA};

    VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vk->vertShader;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = vk->fragShader;
    stages[1].pName = "main";
    stages[1].pSpecializationInfo = &spec;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dyn;
    pci.layout = vk->pipelineLayout;
    pci.renderPass = vk->info.renderPass;
    pci.subpass = vk->info.subpass;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (!check(vk, vk->vk.CreateGraphicsPipelines(vk->info.device, vk->pipelineCache, 1, &pci, vk->info.allocator, &pipeline), "vkCreateGraphicsPipelines")) return VK_NULL_HANDLE;
    return pipeline;
}

static bool bindPipeline(VkContext* vk, VkCommandBuffer cmd, VkPrimitiveTopology topology, StencilMode mode, const Blend& blend)
{
    const uint64_t key = pipelineKey(topology, mode, blend);
    VkPipeline pipeline;
    auto it = vk->pipelines.find(key);
    if (it != vk->pipelines.end()) {
        pipeline = it->second;
    } else {
        pipeline = createPipeline(vk, topology, mode, blend);
        if (pipeline == VK_NULL_HANDLE) return false;
        vk->pipelines[key] = pipeline;
    }
    if (pipeline != vk->boundPipeline) {
        vk->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vk->boundPipeline = pipeline;
    }
    return true;
}

//
// Render API
//

static int renderCreate(void* uptr)
{
    VkContext* vk = static_cast<VkContext*>(uptr);
    const VkDevice dev = vk->info.device;
    const VkAllocationCallbacks* alloc = vk->info.allocator;

    if (!loadFunctions(vk)) return 0;
    vk->vk.GetPhysicalDeviceProperties(vk->info.physicalDevice, &vk->props);
    vk->vk.GetPhysicalDeviceMemoryProperties(vk->info.physicalDevice, &vk->memProps);
    const VkFormat formats[2] = {VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    for (int i = 0; i < 2; i++) {
        VkFormatProperties fp;
        vk->vk.GetPhysicalDeviceFormatProperties(vk->info.physicalDevice, formats[i], &fp);
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        vk->canGenerateMips[i] = (fp.optimalTilingFeatures & need) == need;
    }

    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = sizeof(kVertShaderCode);
    smci.pCode = kVertShaderCode;
    if (!check(vk, vk->vk.CreateShaderModule(dev, &smci, alloc, &vk->vertShader), "vkCreateShaderModule")) return 0;
    smci.codeSize = sizeof(kFragShaderCode);
    smci.pCode = kFragShaderCode;
    if (!check(vk, vk->vk.CreateShaderModule(dev, &smci, alloc, &vk->fragShader), "vkCreateShaderModule")) return 0;

    VkDescriptorSetLayoutBinding uniformBinding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 1;
    dslci.pBindings = &uniformBinding;
    if (!check(vk, vk->vk.CreateDescriptorSetLayout(dev, &dslci, alloc, &vk->uniformSetLayout), "vkCreateDescriptorSetLayout")) return 0;
    VkDescriptorSetLayoutBinding textureBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    dslci.pBindings = &textureBinding;
    if (!check(vk, vk->vk.CreateDescriptorSetLayout(dev, &dslci, alloc, &vk->textureSetLayout), "vkCreateDescriptorSetLayout")) return 0;

    const VkDescriptorSetLayout setLayouts[2] = {vk->uniformSetLayout, vk->textureSetLayout};
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 2;
    plci.pSetLayouts = setLayouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (!check(vk, vk->vk.CreatePipelineLayout(dev, &plci, alloc, &vk->pipelineLayout), "vkCreatePipelineLayout")) return 0;

    VkPipelineCacheCreateInfo pcci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    if (!check(vk, vk->vk.CreatePipelineCache(dev, &pcci, alloc, &vk->pipelineCache), "vkCreatePipelineCache")) return 0;

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = vk->info.queueFamilyIndex;
    if (!check(vk, vk->vk.CreateCommandPool(dev, &cpci, alloc, &vk->commandPool), "vkCreateCommandPool")) return 0;

    vk->frames.resize(std::max(1u, vk->info.framesInFlight));

    // Some drivers dislike sampling unbound descriptors; draws without an image sample this one.
    vk->dummyTex = renderCreateTexture(vk, static_cast<int>(Texture::Alpha), 1, 1, 0, nullptr);
    return vk->dummyTex != 0 ? 1 : 0;
}

static void renderViewport(void* uptr, float width, float height, float devicePixelRatio)
{
    VkContext* vk = static_cast<VkContext*>(uptr);
    vk->view[0] = width;
    vk->view[1] = height;
    vk->devicePixelRatio = devicePixelRatio;
}

static void resetCalls(VkContext* vk)
{
    vk->calls.clear();
    vk->paths.clear();
    vk->verts.clear();
    vk->uniforms.clear();
}

static void renderCancel(void* uptr)
{
    resetCalls(static_cast<VkContext*>(uptr));
}

static void setUniforms(VkContext* vk, VkCommandBuffer cmd, int uniformOffset, int image)
{
    PushConstants pc{};
    pc.viewSize[0] = vk->view[0];
    pc.viewSize[1] = vk->view[1];
    pc.uniformIndex = vk->boundUniformBase + uniformOffset;
    vk->vk.CmdPushConstants(cmd, vk->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    TextureVk* tex = image != 0 ? findTexture(vk, image) : nullptr;
    if (tex == nullptr) tex = findTexture(vk, vk->dummyTex);
    if (tex != nullptr && tex->set != vk->boundTextureSet) {
        vk->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipelineLayout, 1, 1, &tex->set, 0, nullptr);
        vk->boundTextureSet = tex->set;
    }
}

static void drawFill(VkContext* vk, VkCommandBuffer cmd, const Call& call)
{
    const DrawPath* paths = &vk->paths[call.pathOffset];
    const Blend& blend = call.blend;

    // Draw shapes into the stencil buffer
    if (!bindPipeline(vk, cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, StencilFillShapes, blend)) return;
    setUniforms(vk, cmd, call.uniformOffset, 0);
    for (int i = 0; i < call.pathCount; i++) vk->vk.CmdDraw(cmd, paths[i].fillCount, 1, paths[i].fillOffset, 0);

    setUniforms(vk, cmd, call.uniformOffset + 1, call.image);
    if (vk->flags & CreateFlags::Antialias) {
        // Draw anti-aliased fringes
        if (!bindPipeline(vk, cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, StencilEqualZero, blend)) return;
        for (int i = 0; i < call.pathCount; i++) vk->vk.CmdDraw(cmd, paths[i].strokeCount, 1, paths[i].strokeOffset, 0);
    }

    // Draw fill, clearing the stencil as we go
    if (!bindPipeline(vk, cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, StencilFillCover, blend)) return;
    vk->vk.CmdDraw(cmd, call.triangleCount, 1, call.triangleOffset, 0);
}

static void drawConvexFill(VkContext* vk, VkCommandBuffer cmd, const Call& call)
{
    const DrawPath* paths = &vk->paths[call.pathOffset];
    if (!bindPipeline(vk, cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, StencilNone, call.blend)) return;
    setUniforms(vk, cmd, call.uniformOffset, call.image);
    for (int i = 0; i < call.pathCount; i++) vk->vk.CmdDraw(cmd, paths[i].fillCount, 1, paths[i].fillOffset, 0);

    // Draw fringes
    if (!bindPipeline(vk, cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, StencilNone, call.blend)) return;
    for (int i = 0; i < call.pathCount; i++)
        if (paths[i].strokeCount > 0) vk->vk.CmdDraw(cmd, paths[i].strokeCount, 1, paths[i].strokeOffset, 0);
}

static void drawStroke(VkContext* vk, VkCommandBuffer cmd, const Call& call)
{
    const DrawPath* paths = &vk->paths[call.pathOffset];
    const VkPrimitiveTopology strip = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    if (vk->flags & CreateFlags::StencilStrokes) {
        // Fill the stroke base without overlap
        if (!bindPipeline(vk, cmd, strip, StencilStrokeBase, call.blend)) return;
        setUniforms(vk, cmd, call.uniformOffset + 1, call.image);
        for (int i = 0; i < call.pathCount; i++) vk->vk.CmdDraw(cmd, paths[i].strokeCount, 1, paths[i].strokeOffset, 0);

        // Draw anti-aliased pixels.
        if (!bindPipeline(vk, cmd, strip, StencilEqualZero, call.blend)) return;
        setUniforms(vk, cmd, call.uniformOffset, call.image);
        for (int i = 0; i < call.pathCount; i++) vk->vk.CmdDraw(cmd, paths[i].strokeCount, 1, paths[i].strokeOffset, 0);

        // Clear stencil buffer.
        if (!bindPipeline(vk, cmd, strip, StencilStrokeClear, call.blend)) return;
        for (int i = 0; i < call.pathCount; i++) vk->vk.CmdDraw(cmd, paths[i].strokeCount, 1, paths[i].strokeOffset, 0);
    } else {
        if (!bindPipeline(vk, cmd, strip, StencilNone, call.blend)) return;
        setUniforms(vk, cmd, call.uniformOffset, call.image);
        for (int i = 0; i < call.pathCount; i++) vk->vk.CmdDraw(cmd, paths[i].strokeCount, 1, paths[i].strokeOffset, 0);
    }
}

static void drawTriangles(VkContext* vk, VkCommandBuffer cmd, const Call& call)
{
    if (call.triangleCount == 0) return;
    if (!bindPipeline(vk, cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, StencilNone, call.blend)) return;
    setUniforms(vk, cmd, call.uniformOffset, call.image);
    vk->vk.CmdDraw(cmd, call.triangleCount, 1, call.triangleOffset, 0);
}

static void renderFlush(void* uptr)
{
    VkContext* vk = static_cast<VkContext*>(uptr);
    FrameData& f = currentFrame(vk);

    // TextureVk writes recorded so far must land before this frame's command buffer executes.
    submitUploads(vk);

    if (vk->calls.empty()) {
        resetCalls(vk);
        return;
    }
    const VkCommandBuffer cmd = f.cmd;
    if (cmd == VK_NULL_HANDLE) {
        printf("nanovg vulkan: endFrame() without beginFrameVk(); dropping %zu draw calls\n", vk->calls.size());
        resetCalls(vk);
        return;
    }

    // Append this flush's data to the slot's buffers; earlier flushes in the same frame keep their ranges.
    const VkDeviceSize vertBytes = vk->verts.size() * sizeof(Vertex);
    const VkDeviceSize uniBytes = vk->uniforms.size() * sizeof(FragUniforms);
    bool replaced = false;
    if (!reserveFrameBuffer(vk, f.vertices, f.verticesUsed, vertBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &replaced)) {
        resetCalls(vk);
        return;
    }
    if (!reserveFrameBuffer(vk, f.uniforms, f.uniformsUsed, uniBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &replaced)) {
        resetCalls(vk);
        return;
    }
    if (replaced || f.uniformSet == VK_NULL_HANDLE) {
        // A descriptor set may be in use by recorded draws, so each uniform buffer gets its own set.
        if (f.uniformSet != VK_NULL_HANDLE) f.garbage.sets.emplace_back(f.uniformSetPool, f.uniformSet);
        f.uniformSet = VK_NULL_HANDLE;
        if (!allocateDescriptorSet(vk, vk->uniformSetLayout, &f.uniformSet, &f.uniformSetPool)) {
            resetCalls(vk);
            return;
        }
        VkDescriptorBufferInfo bi{f.uniforms.buffer, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = f.uniformSet;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w.pBufferInfo = &bi;
        vk->vk.UpdateDescriptorSets(vk->info.device, 1, &w, 0, nullptr);
    }
    const VkDeviceSize vertOffset = f.verticesUsed;
    const VkDeviceSize uniOffset = f.uniformsUsed;
    std::memcpy(static_cast<unsigned char*>(f.vertices.mapped) + vertOffset, vk->verts.data(), vertBytes);
    std::memcpy(static_cast<unsigned char*>(f.uniforms.mapped) + uniOffset, vk->uniforms.data(), uniBytes);
    f.verticesUsed += vertBytes;
    f.uniformsUsed += uniBytes;

    vk->boundPipeline = VK_NULL_HANDLE;
    vk->boundTextureSet = VK_NULL_HANDLE;
    vk->boundUniformBase = static_cast<int32_t>(uniOffset / sizeof(FragUniforms));

    VkViewport viewport{};
    viewport.width = vk->view[0] * vk->devicePixelRatio;
    viewport.height = vk->view[1] * vk->devicePixelRatio;
    viewport.maxDepth = 1.0f;
    vk->vk.CmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, {static_cast<uint32_t>(std::lround(viewport.width)), static_cast<uint32_t>(std::lround(viewport.height))}};
    vk->vk.CmdSetScissor(cmd, 0, 1, &scissor);
    vk->vk.CmdBindVertexBuffers(cmd, 0, 1, &f.vertices.buffer, &vertOffset);
    vk->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->pipelineLayout, 0, 1, &f.uniformSet, 0, nullptr);

    for (const Call& call : vk->calls) {
        switch (call.type) {
            case CallFill:
                drawFill(vk, cmd, call);
                break;
            case CallConvexFill:
                drawConvexFill(vk, cmd, call);
                break;
            case CallStroke:
                drawStroke(vk, cmd, call);
                break;
            case CallTriangles:
                drawTriangles(vk, cmd, call);
                break;
            default:
                break;
        }
    }
    resetCalls(vk);
}

static void xformToMat3x4(float* m3, const float* t)
{
    m3[0] = t[0];
    m3[1] = t[1];
    m3[2] = 0.0f;
    m3[3] = 0.0f;
    m3[4] = t[2];
    m3[5] = t[3];
    m3[6] = 0.0f;
    m3[7] = 0.0f;
    m3[8] = t[4];
    m3[9] = t[5];
    m3[10] = 1.0f;
    m3[11] = 0.0f;
}

static Color premulColor(Color c)
{
    c.r *= c.a;
    c.g *= c.a;
    c.b *= c.a;
    return c;
}

static bool convertPaint(VkContext* vk, FragUniforms* frag, const Paint* paint, const Scissor* scissor, float width, float fringe, float strokeThr, int lineStyle)
{
    std::array<float, 6> invxform{};
    *frag = FragUniforms{};
    frag->innerCol = premulColor(paint->innerColor);
    frag->outerCol = premulColor(paint->outerColor);
    frag->lineStyle = lineStyle;

    if (scissor->extent[0] < -0.5f || scissor->extent[1] < -0.5f) {
        frag->scissorExt[0] = 1.0f;
        frag->scissorExt[1] = 1.0f;
        frag->scissorScale[0] = 1.0f;
        frag->scissorScale[1] = 1.0f;
    } else {
        transformInverse(invxform.data(), scissor->xform);
        xformToMat3x4(frag->scissorMat, invxform.data());
        frag->scissorExt[0] = scissor->extent[0];
        frag->scissorExt[1] = scissor->extent[1];
        frag->scissorScale[0] = std::sqrt(scissor->xform[0] * scissor->xform[0] + scissor->xform[2] * scissor->xform[2]) / fringe;
        frag->scissorScale[1] = std::sqrt(scissor->xform[1] * scissor->xform[1] + scissor->xform[3] * scissor->xform[3]) / fringe;
    }

    std::memcpy(frag->extent, paint->extent, sizeof(frag->extent));
    frag->strokeMult = (width * 0.5f + fringe * 0.5f) / fringe;
    frag->strokeThr = strokeThr;

    if (paint->image != 0) {
        const TextureVk* tex = findTexture(vk, paint->image);
        if (tex == nullptr) return false;
        if ((tex->flags & ImageFlags::Flipy) != 0) {
            std::array<float, 6> m1{};
            std::array<float, 6> m2{};
            transformTranslate(m1.data(), 0.0f, frag->extent[1] * 0.5f);
            transformMultiply(m1.data(), paint->xform);
            transformScale(m2.data(), 1.0f, -1.0f);
            transformMultiply(m2.data(), m1.data());
            transformTranslate(m1.data(), 0.0f, -frag->extent[1] * 0.5f);
            transformMultiply(m1.data(), m2.data());
            transformInverse(invxform.data(), m1.data());
        } else {
            transformInverse(invxform.data(), paint->xform);
        }
        frag->type = ShaderFillImg;
        if (tex->type == static_cast<int>(Texture::Rgba))
            frag->texType = (tex->flags & ImageFlags::Premultiplied) ? 0 : 1;
        else
            frag->texType = 2;
    } else {
        frag->type = ShaderFillGrad;
        frag->radius = paint->radius;
        frag->feather = paint->feather;
        transformInverse(invxform.data(), paint->xform);
    }
    xformToMat3x4(frag->paintMat, invxform.data());
    return true;
}

static VkBlendFactor convertBlendFactor(int factor)
{
    switch (static_cast<BlendFactor>(factor)) {
        case BlendFactor::Zero:
            return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:
            return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor:
            return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::SrcAlphaSaturate:
            return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    }
    return VK_BLEND_FACTOR_MAX_ENUM;
}

static Blend blendCompositeOperation(CompositeOperationState op)
{
    Blend blend{convertBlendFactor(op.srcRGB), convertBlendFactor(op.dstRGB), convertBlendFactor(op.srcAlpha), convertBlendFactor(op.dstAlpha)};
    if (blend.srcRGB == VK_BLEND_FACTOR_MAX_ENUM || blend.dstRGB == VK_BLEND_FACTOR_MAX_ENUM || blend.srcAlpha == VK_BLEND_FACTOR_MAX_ENUM || blend.dstAlpha == VK_BLEND_FACTOR_MAX_ENUM) {
        blend = Blend{VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA};
    }
    return blend;
}

static int allocUniforms(VkContext* vk, int n)
{
    const int offset = static_cast<int>(vk->uniforms.size());
    vk->uniforms.resize(vk->uniforms.size() + n);
    return offset;
}

static int allocVerts(VkContext* vk, int n)
{
    const int offset = static_cast<int>(vk->verts.size());
    vk->verts.resize(vk->verts.size() + n);
    return offset;
}

static void renderFill(void* uptr, Paint* paint, CompositeOperationState compositeOperation, Scissor* scissor, float fringe, const float* bounds, const Path* paths, int npaths)
{
    VkContext* vk = static_cast<VkContext*>(uptr);
    Call call{};
    call.type = CallFill;
    call.triangleCount = 4;
    call.pathOffset = static_cast<int>(vk->paths.size());
    call.pathCount = npaths;
    call.image = paint->image;
    call.blend = blendCompositeOperation(compositeOperation);
    if (npaths == 1 && paths[0].convex) {
        call.type = CallConvexFill;
        call.triangleCount = 0;  // Bounding box fill quad not needed for convex fill
    }

    // Fans are expanded into triangle lists: TRIANGLE_FAN is optional on some Vulkan implementations.
    int maxverts = call.triangleCount;
    for (int i = 0; i < npaths; i++) maxverts += std::max(0, paths[i].nfill - 2) * 3 + paths[i].nstroke;
    int offset = allocVerts(vk, maxverts);

    for (int i = 0; i < npaths; i++) {
        DrawPath copy{};
        const Path* path = &paths[i];
        if (path->nfill > 2) {
            copy.fillOffset = offset;
            copy.fillCount = (path->nfill - 2) * 3;
            for (int j = 0; j < path->nfill - 2; j++) {
                vk->verts[offset++] = path->fill[0];
                vk->verts[offset++] = path->fill[j + 1];
                vk->verts[offset++] = path->fill[j + 2];
            }
        }
        if (path->nstroke > 0) {
            copy.strokeOffset = offset;
            copy.strokeCount = path->nstroke;
            std::memcpy(&vk->verts[offset], path->stroke, sizeof(Vertex) * path->nstroke);
            offset += path->nstroke;
        }
        vk->paths.push_back(copy);
    }

    bool ok;
    if (call.type == CallFill) {
        // Quad
        call.triangleOffset = offset;
        Vertex* quad = &vk->verts[call.triangleOffset];
        quad[0] = Vertex{bounds[2], bounds[3], 0.5f, 1.0f, 0.0f, 0.0f};
        quad[1] = Vertex{bounds[2], bounds[1], 0.5f, 1.0f, 0.0f, 0.0f};
        quad[2] = Vertex{bounds[0], bounds[3], 0.5f, 1.0f, 0.0f, 0.0f};
        quad[3] = Vertex{bounds[0], bounds[1], 0.5f, 1.0f, 0.0f, 0.0f};

        call.uniformOffset = allocUniforms(vk, 2);
        // Simple shader for stencil
        FragUniforms& simple = vk->uniforms[call.uniformOffset];
        simple = FragUniforms{};
        simple.strokeThr = -1.0f;
        simple.type = ShaderSimple;
        // Fill shader
        ok = convertPaint(vk, &vk->uniforms[call.uniformOffset + 1], paint, scissor, fringe, fringe, -1.0f, 0);
    } else {
        call.uniformOffset = allocUniforms(vk, 1);
        ok = convertPaint(vk, &vk->uniforms[call.uniformOffset], paint, scissor, fringe, fringe, -1.0f, 0);
    }
    if (ok) vk->calls.push_back(call);
}

static void renderStroke(void* uptr, Paint* paint, CompositeOperationState compositeOperation, Scissor* scissor, float fringe, float strokeWidth, int lineStyle, const Path* paths, int npaths)
{
    VkContext* vk = static_cast<VkContext*>(uptr);
    Call call{};
    call.type = CallStroke;
    call.pathOffset = static_cast<int>(vk->paths.size());
    call.pathCount = npaths;
    call.image = paint->image;
    call.blend = blendCompositeOperation(compositeOperation);

    int maxverts = 0;
    for (int i = 0; i < npaths; i++) maxverts += paths[i].nstroke;
    int offset = allocVerts(vk, maxverts);
    for (int i = 0; i < npaths; i++) {
        DrawPath copy{};
        const Path* path = &paths[i];
        if (path->nstroke > 0) {
            copy.strokeOffset = offset;
            copy.strokeCount = path->nstroke;
            std::memcpy(&vk->verts[offset], path->stroke, sizeof(Vertex) * path->nstroke);
            offset += path->nstroke;
        }
        vk->paths.push_back(copy);
    }

    bool ok;
    if (vk->flags & CreateFlags::StencilStrokes) {
        call.uniformOffset = allocUniforms(vk, 2);
        ok = convertPaint(vk, &vk->uniforms[call.uniformOffset], paint, scissor, strokeWidth, fringe, -1.0f, lineStyle) &&
             convertPaint(vk, &vk->uniforms[call.uniformOffset + 1], paint, scissor, strokeWidth, fringe, 1.0f - 0.5f / 255.0f, lineStyle);
    } else {
        call.uniformOffset = allocUniforms(vk, 1);
        ok = convertPaint(vk, &vk->uniforms[call.uniformOffset], paint, scissor, strokeWidth, fringe, -1.0f, lineStyle);
    }
    if (ok) vk->calls.push_back(call);
}

static void renderTriangles(void* uptr, Paint* paint, CompositeOperationState compositeOperation, Scissor* scissor, const Vertex* verts, int nverts, float fringe)
{
    VkContext* vk = static_cast<VkContext*>(uptr);
    Call call{};
    call.type = CallTriangles;
    call.image = paint->image;
    call.blend = blendCompositeOperation(compositeOperation);
    call.triangleOffset = allocVerts(vk, nverts);
    call.triangleCount = nverts;
    std::memcpy(&vk->verts[call.triangleOffset], verts, sizeof(Vertex) * nverts);

    call.uniformOffset = allocUniforms(vk, 1);
    FragUniforms* frag = &vk->uniforms[call.uniformOffset];
    if (!convertPaint(vk, frag, paint, scissor, 1.0f, fringe, -1.0f, 0)) return;
    frag->type = ShaderImg;
    vk->calls.push_back(call);
}

static void renderShape(void* uptr, Paint* paint, CompositeOperationState compositeOperation, Scissor* scissor, float fringe, const Shape* shape, const Vertex* verts, int nverts)
{
    VkContext* vk = static_cast<VkContext*>(uptr);
    std::array<float, 6> inv{};
    if (transformInverse(inv.data(), shape->xform) == 0) return;

    Call call{};
    call.type = CallTriangles;
    call.image = paint->image;
    call.blend = blendCompositeOperation(compositeOperation);
    call.triangleOffset = allocVerts(vk, nverts);
    call.triangleCount = nverts;
    std::memcpy(&vk->verts[call.triangleOffset], verts, sizeof(Vertex) * nverts);

    // A regular paint (gradient or image) whose result the shader scales by the shape's coverage.
    call.uniformOffset = allocUniforms(vk, 1);
    FragUniforms* frag = &vk->uniforms[call.uniformOffset];
    if (!convertPaint(vk, frag, paint, scissor, fringe, fringe, -1.0f, 0)) return;
    xformToMat3x4(frag->shapeMat, inv.data());
    frag->shapeType = shape->type;
    frag->shapeAA = shape->aaWidth;
    frag->shapeStrokeHalf = shape->strokeHalfWidth;
    std::memcpy(frag->shapeRect, shape->params, sizeof(frag->shapeRect));
    std::memcpy(frag->shapeGrid, shape->params + 4, sizeof(frag->shapeGrid));
    vk->calls.push_back(call);
}

static void renderDelete(void* uptr)
{
    VkContext* vk = static_cast<VkContext*>(uptr);
    if (vk == nullptr) return;
    const VkDevice dev = vk->info.device;
    const VkAllocationCallbacks* alloc = vk->info.allocator;
    if (vk->frames.empty()) vk->frames.resize(1);  // renderCreate failed early; retire* needs a slot

    if (vk->vk.DeviceWaitIdle != nullptr) {
        vk->vk.DeviceWaitIdle(dev);

        for (TextureVk& t : vk->textures)
            if (t.id != 0) retireTexture(vk, t);
        for (FrameData& f : vk->frames) {
            releaseGarbage(vk, f.garbage);
            destroyBufferNow(vk, f.vertices);
            destroyBufferNow(vk, f.uniforms);
            destroyBufferNow(vk, f.staging);
            if (f.uploadFence != VK_NULL_HANDLE) vk->vk.DestroyFence(dev, f.uploadFence, alloc);
        }
        for (auto& [key, pipeline] : vk->pipelines) vk->vk.DestroyPipeline(dev, pipeline, alloc);
        for (auto& [key, sampler] : vk->samplers) vk->vk.DestroySampler(dev, sampler, alloc);
        // Destroying the pools frees every descriptor set and command buffer allocated from them.
        for (VkDescriptorPool pool : vk->descriptorPools) vk->vk.DestroyDescriptorPool(dev, pool, alloc);
        if (vk->commandPool != VK_NULL_HANDLE) vk->vk.DestroyCommandPool(dev, vk->commandPool, alloc);
        if (vk->pipelineCache != VK_NULL_HANDLE) vk->vk.DestroyPipelineCache(dev, vk->pipelineCache, alloc);
        if (vk->pipelineLayout != VK_NULL_HANDLE) vk->vk.DestroyPipelineLayout(dev, vk->pipelineLayout, alloc);
        if (vk->uniformSetLayout != VK_NULL_HANDLE) vk->vk.DestroyDescriptorSetLayout(dev, vk->uniformSetLayout, alloc);
        if (vk->textureSetLayout != VK_NULL_HANDLE) vk->vk.DestroyDescriptorSetLayout(dev, vk->textureSetLayout, alloc);
        if (vk->vertShader != VK_NULL_HANDLE) vk->vk.DestroyShaderModule(dev, vk->vertShader, alloc);
        if (vk->fragShader != VK_NULL_HANDLE) vk->vk.DestroyShaderModule(dev, vk->fragShader, alloc);
    }
    delete vk;
}

static VkContext* contextOf(Context& ctx)
{
    return static_cast<VkContext*>(ctx.internalParams().userPtr);
}

// Allocates the backend state and fills the render callbacks. The returned object is owned by `params`
// (released through params->renderDelete). Shared by createVk and the C API (nvgCreateVk).
static VkContext* newContext(const VkCreateInfo& createInfo, int flags, Params* params)
{
    if (createInfo.getInstanceProcAddr == nullptr || createInfo.device == VK_NULL_HANDLE || createInfo.physicalDevice == VK_NULL_HANDLE || createInfo.queue == VK_NULL_HANDLE ||
        createInfo.renderPass == VK_NULL_HANDLE) {
        printf("nanovg vulkan: incomplete VkCreateInfo\n");
        return nullptr;
    }
    VkContext* vk = new VkContext{};
    vk->info = createInfo;
    vk->flags = flags;

    *params = Params{};
    params->renderCreate = renderCreate;
    params->renderCreateTexture = renderCreateTexture;
    params->renderDeleteTexture = renderDeleteTexture;
    params->renderUpdateTexture = renderUpdateTexture;
    params->renderGetTextureSize = renderGetTextureSize;
    params->renderGetImageTextureId = renderGetImageTextureId;
    params->renderViewport = renderViewport;
    params->renderCancel = renderCancel;
    params->renderFlush = renderFlush;
    params->renderFill = renderFill;
    params->renderStroke = renderStroke;
    params->renderTriangles = renderTriangles;
    params->renderShape = renderShape;
    params->renderDelete = renderDelete;
    params->userPtr = vk;
    params->edgeAntiAlias = (flags & CreateFlags::Antialias) ? 1 : 0;
    return vk;
}

static void beginFrame(VkContext* vk, VkCommandBuffer commandBuffer, uint32_t frameIndex)
{
    // Uploads recorded since the last flush belong to the slot that is ending.
    submitUploads(vk);

    vk->frameIndex = frameIndex % static_cast<uint32_t>(vk->frames.size());
    FrameData& f = currentFrame(vk);
    if (f.uploadInFlight) {
        vk->vk.WaitForFences(vk->info.device, 1, &f.uploadFence, VK_TRUE, UINT64_MAX);
        f.uploadInFlight = false;
    }
    // The application has waited for this slot's previous frame, so nothing references the garbage anymore.
    releaseGarbage(vk, f.garbage);
    f.cmd = commandBuffer;
    f.verticesUsed = 0;
    f.uniformsUsed = 0;
    f.stagingUsed = 0;
}

static int createImageFromHandle(VkContext* vk, VkImageView view, VkSampler sampler, VkImageLayout layout, int w, int h, int imageFlags)
{
    if (view == VK_NULL_HANDLE) return 0;
    TextureVk* tex = allocTexture(vk);
    tex->external = true;
    tex->view = view;
    tex->layout = layout;
    tex->width = w;
    tex->height = h;
    tex->type = static_cast<int>(Texture::Rgba);
    tex->flags = imageFlags;
    tex->sampler = sampler != VK_NULL_HANDLE ? sampler : getSampler(vk, imageFlags, 1);
    if (tex->sampler == VK_NULL_HANDLE || !writeTextureSet(vk, *tex)) {
        retireTexture(vk, *tex);
        return 0;
    }
    return tex->id;
}

static VkImageView imageHandle(VkContext* vk, int image)
{
    TextureVk* tex = findTexture(vk, image);
    return tex != nullptr ? tex->view : VK_NULL_HANDLE;
}

}  // namespace vkdetail

std::unique_ptr<Context> createVk(const VkCreateInfo& createInfo, int flags)
{
    Params params{};
    vkdetail::VkContext* vk = vkdetail::newContext(createInfo, flags, &params);
    if (vk == nullptr) return nullptr;
    try {
        return std::make_unique<Context>(params);
    } catch (const std::exception& e) {
        // The context never finished construction, so it did not call renderDelete.
        printf("nanovg vulkan: %s\n", e.what());
        vkdetail::renderDelete(vk);
        return nullptr;
    }
}

void deleteVk(std::unique_ptr<Context>& ctx)
{
    ctx.reset();
}

void beginFrameVk(Context& ctx, VkCommandBuffer commandBuffer, uint32_t frameIndex)
{
    vkdetail::beginFrame(vkdetail::contextOf(ctx), commandBuffer, frameIndex);
}

int createImageFromHandleVk(Context& ctx, VkImageView view, VkSampler sampler, VkImageLayout layout, int w, int h, int imageFlags)
{
    return vkdetail::createImageFromHandle(vkdetail::contextOf(ctx), view, sampler, layout, w, h, imageFlags);
}

VkImageView imageHandleVk(Context& ctx, int image)
{
    return vkdetail::imageHandle(vkdetail::contextOf(ctx), image);
}

}  // namespace nvg

#endif  // NANOVG_VK_IMPLEMENTATION
