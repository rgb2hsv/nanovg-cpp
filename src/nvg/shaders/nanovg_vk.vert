#version 450
// NanoVG Vulkan backend - vertex shader.
// Regenerate nanovg_vk_vert.spv.inc after editing (CMake target: nanovg_vk_shaders).

layout(push_constant) uniform PushConstants {
    vec2 viewSize;
    int uniformIndex;
} pc;

layout(location = 0) in vec2 vertex;
layout(location = 1) in vec4 tcoord;

layout(location = 0) out vec2 ftcoord;
layout(location = 1) out vec2 fpos;
layout(location = 2) out vec2 uv;

void main(void)
{
    ftcoord = tcoord.xy;
    uv = 0.5 * tcoord.zw;
    fpos = vertex;
    // Vulkan clip space has +y pointing down, which matches NanoVG's coordinate system.
    gl_Position = vec4(2.0 * vertex.x / pc.viewSize.x - 1.0, 2.0 * vertex.y / pc.viewSize.y - 1.0, 0.0, 1.0);
}
