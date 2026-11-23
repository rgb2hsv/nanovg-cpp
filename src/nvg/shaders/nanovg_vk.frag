#version 450
// NanoVG Vulkan backend - fragment shader. Mirrors the GL3 shader in nanovg_gl.hpp.
// Regenerate nanovg_vk_frag.spv.inc after editing (CMake target: nanovg_vk_shaders).

layout(constant_id = 0) const int EDGE_AA = 0;

struct FragUniforms {
    mat3 scissorMat;
    mat3 paintMat;
    vec4 innerCol;
    vec4 outerCol;
    vec2 scissorExt;
    vec2 scissorScale;
    vec2 extent;
    float radius;
    float feather;
    float strokeMult;
    float strokeThr;
    int lineStyle;
    int texType;
    int type;
};

layout(std430, set = 0, binding = 0) readonly buffer FragBuffer {
    FragUniforms frags[];
};
layout(set = 1, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform PushConstants {
    vec2 viewSize;
    int uniformIndex;
} pc;

layout(location = 0) in vec2 ftcoord;
layout(location = 1) in vec2 fpos;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec4 outColor;

float sdroundrect(vec2 pt, vec2 ext, float rad)
{
    vec2 ext2 = ext - vec2(rad, rad);
    vec2 d = abs(pt) - ext2;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - rad;
}

float scissorMask(FragUniforms f, vec2 p)
{
    vec2 sc = (abs((f.scissorMat * vec3(p, 1.0)).xy) - f.scissorExt);
    sc = vec2(0.5, 0.5) - sc * f.scissorScale;
    return clamp(sc.x, 0.0, 1.0) * clamp(sc.y, 0.0, 1.0);
}

float glow(vec2 p)
{
    return smoothstep(0.0, 1.0, 1.0 - 2.0 * abs(p.x));
}

float dashed(vec2 p)
{
    float fy = fract(p.y / 4.0);
    float w = step(fy, 0.5);
    fy *= 4.0;
    if (fy >= 1.5) {
        fy -= 1.5;
    } else if (fy <= 0.5) {
        fy = 0.5 - fy;
    } else {
        fy = 0.0;
    }
    w *= smoothstep(0.0, 1.0, 6.0 * (0.25 - (p.x * p.x + fy * fy)));
    return w;
}

float dotted(vec2 p)
{
    float fy = 4.0 * fract(p.y / 4.0) - 0.5;
    return smoothstep(0.0, 1.0, 6.0 * (0.25 - (p.x * p.x + fy * fy)));
}

// Stroke - from [0..1] to clipped pyramid, where the slope is 1px.
float strokeMask(FragUniforms f)
{
    return min(1.0, (1.0 - abs(ftcoord.x * 2.0 - 1.0)) * f.strokeMult) * min(1.0, ftcoord.y);
}

void main(void)
{
    FragUniforms f = frags[pc.uniformIndex];
    vec4 result = vec4(0.0);
    float scissor = scissorMask(f, fpos);

    float strokeAlpha = (EDGE_AA != 0) ? strokeMask(f) : 1.0;
    if (f.lineStyle == 2) strokeAlpha *= dashed(uv);
    if (f.lineStyle == 3) strokeAlpha *= dotted(uv);
    if (f.lineStyle == 4) strokeAlpha *= glow(uv);
    if (EDGE_AA != 0) {
        if (strokeAlpha < f.strokeThr) discard;
    } else {
        if (f.lineStyle > 1 && strokeAlpha < f.strokeThr) discard;
    }

    if (f.type == 0) {  // Gradient
        vec2 pt = (f.paintMat * vec3(fpos, 1.0)).xy;
        float d = clamp((sdroundrect(pt, f.extent, f.radius) + f.feather * 0.5) / f.feather, 0.0, 1.0);
        vec4 color = mix(f.innerCol, f.outerCol, d);
        color *= strokeAlpha * scissor;
        result = color;
    } else if (f.type == 1) {  // Image
        vec2 pt = (f.paintMat * vec3(fpos, 1.0)).xy / f.extent;
        vec4 color = texture(tex, pt);
        if (f.texType == 1) color = vec4(color.xyz * color.w, color.w);
        if (f.texType == 2) color = vec4(color.x);
        color *= f.innerCol;
        color *= strokeAlpha * scissor;
        result = color;
    } else if (f.type == 2) {  // Stencil fill
        result = vec4(1.0);
    } else if (f.type == 3) {  // Textured tris
        vec4 color = texture(tex, ftcoord);
        if (f.texType == 1) color = vec4(color.xyz * color.w, color.w);
        if (f.texType == 2) color = vec4(color.x);
        color *= scissor;
        result = color * f.innerCol;
    }
    outColor = result;
}
