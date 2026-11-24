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
    int shapeType;  // 0 = none, else nvg::ShapeType
    float shapeAA;
    float shapeStrokeHalf;
    mat3 shapeMat;  // device -> shape space
    vec4 shapeRect;
    vec4 shapeGrid;
    vec4 strokeEffect;  // stroke effect (Params::renderStrokeEffect): w > 0 inner shadow, w < 0 Gaussian blur, 0 off
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

// Shape uniforms of the current draw, copied here by main() so the shape code matches the GL shaders.
int shapeType;
float shapeAA;
float shapeStrokeHalf;
vec4 shapeRect;
vec4 shapeGrid;
// Analytic shapes (drawGrid / drawArrow): coverage of the shape at p (shape space).
float shapeSegDist(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float l = dot(ba, ba);
    float h = l > 0.0 ? clamp(dot(pa, ba) / l, 0.0, 1.0) : 0.0;
    return length(pa - ba * h);
}
// Coverage of an axis-aligned box with sharp corners (center c, half extents e).
float shapeBox(vec2 p, vec2 c, vec2 e) {
    vec2 q = clamp((e - abs(p - c)) / shapeAA + 0.5, 0.0, 1.0);
    return q.x * q.y;
}
// Coverage from a signed distance (negative inside).
float shapeFromDist(float d) {
    if (shapeStrokeHalf > 0.0) d = abs(d) - shapeStrokeHalf;
    return clamp(0.5 - d / shapeAA, 0.0, 1.0);
}
// Union of the nearest vertical and horizontal grid bars, each grown by g on every side.
float shapeGridLines(vec2 p, vec2 nearest, float g) {
    vec2 o = shapeRect.xy, sz = shapeRect.zw, c = o + sz * 0.5;
    float t = shapeGrid.z * 0.5 + g;
    float v = shapeBox(p, vec2(nearest.x, c.y), vec2(t, sz.y * 0.5 + t));
    float h = shapeBox(p, vec2(c.x, nearest.y), vec2(sz.x * 0.5 + t, t));
    return max(v, h);
}
float shapeCoverage(vec2 p) {
    float hs = shapeStrokeHalf;
    if (shapeType == 4) {  // Arrow along +x from the origin, symmetric about the x axis.
        vec2 q = vec2(p.x, abs(p.y));
        float len = shapeRect.x, s = shapeRect.y, hw = shapeRect.z, hl = shapeRect.w, b = len - hl;
        float d = min(shapeSegDist(q, vec2(0.0, 0.0), vec2(0.0, s)), shapeSegDist(q, vec2(0.0, s), vec2(b, s)));
        d = min(d, shapeSegDist(q, vec2(b, s), vec2(b, hw)));
        d = min(d, shapeSegDist(q, vec2(b, hw), vec2(len, 0.0)));
        bool inside = (q.x >= 0.0 && q.x <= b && q.y <= s) || (hl > 0.0 && q.x >= b && q.x <= len && q.y * hl <= hw * (len - q.x));
        return shapeFromDist(inside ? -d : d);
    }
    vec2 o = shapeRect.xy, n = shapeGrid.xy, cell = shapeRect.zw / n;
    float size = shapeGrid.z;
    if (shapeType == 1) {  // Grid lines: nearest boundary in each axis.
        vec2 nearest = o + clamp(floor((p - o) / cell + 0.5), vec2(0.0), n) * cell;
        if (hs <= 0.0) return shapeGridLines(p, nearest, 0.0);
        return clamp(shapeGridLines(p, nearest, hs) - shapeGridLines(p, nearest, -hs), 0.0, 1.0);
    }
    // Circles / squares: the element of the cell containing p.
    vec2 c = o + (clamp(floor((p - o) / cell), vec2(0.0), n - 1.0) + 0.5) * cell;
    if (shapeType == 2) return shapeFromDist(length(p - c) - size);
    if (hs <= 0.0) return shapeBox(p, c, vec2(size));
    return clamp(shapeBox(p, c, vec2(size + hs)) - shapeBox(p, c, vec2(size - hs)), 0.0, 1.0);
}

// Stroke - from [0..1] to clipped pyramid, where the slope is 1px.
float strokeMask(FragUniforms f)
{
    return min(1.0, (1.0 - abs(ftcoord.x * 2.0 - 1.0)) * f.strokeMult) * min(1.0, ftcoord.y);
}

// Stroke inner shadow: one minus the coverage of the stroke band moved by strokeEffect.xy and blurred by
// strokeEffect.z, for a band of half width strokeEffect.w (framebuffer px). ftcoord.x runs 0..1 across the
// band, so its screen-space gradient gives the band's normal at this pixel.
// Gaussian-blurred stroke (strokeEffect.w < 0): the vertices carry distance coordinates, ftcoord.x =
// signed distance across the center line, ftcoord.y = distance from the path's start, uv.x * 2 = distance
// to its end. phi is the normal CDF (logistic approximation); the band profile is phi(hw - d) - phi(-hw - d).
float phi(float x)
{
    return 1.0 / (1.0 + exp(-1.702 * x));
}

float strokeBlurCoverage(FragUniforms f)
{
    float hw = f.strokeEffect.x, sig = f.strokeEffect.y;
    float a0 = ftcoord.y, a1 = uv.x * 2.0;
    if (f.strokeEffect.w < -1.5) {  // round ends: distance to the capsule's core segment
        float r = length(vec2(ftcoord.x, max(max(-a0, -a1), 0.0)));
        return phi((hw - r) / sig) - phi((-hw - r) / sig);
    }
    // butt / square ends: a box, blurred across times blurred along
    float d = abs(ftcoord.x);
    float across = phi((hw - d) / sig) - phi((-hw - d) / sig);
    return across * clamp(phi(a0 / sig) + phi(a1 / sig) - 1.0, 0.0, 1.0);
}

// g: screen-space gradient of ftcoord.x, taken in main() before any discard (derivatives after one are
// undefined).
float strokeInnerShadow(FragUniforms f, vec2 g)
{
    float a = ftcoord.x;
    float gl = length(g);
    if (gl < 1e-6) return 0.0;
    float halfW = f.strokeEffect.w;                   // half width including the antialiasing ramp
    float edge = halfW * (1.0 - 0.5 / f.strokeMult);  // nominal half width (the middle of the ramp)
    float p = (a - 0.5) * 2.0 * halfW;                // signed distance from the center line
    float s = dot(f.strokeEffect.xy, g / gl);         // offset across the band
    float b = max(f.strokeEffect.z * 2.0, 1.0);
    return 1.0 - clamp((edge - abs(p - s)) / b + 0.5, 0.0, 1.0);
}

void main(void)
{
    FragUniforms f = frags[pc.uniformIndex];
    vec2 strokeGrad = vec2(dFdx(ftcoord.x), dFdy(ftcoord.x));  // before any discard
    vec4 result = vec4(0.0);
    float scissor = scissorMask(f, fpos);

    float strokeAlpha = (EDGE_AA != 0) ? (f.strokeEffect.w < 0.0 ? strokeBlurCoverage(f) : strokeMask(f)) : 1.0;
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
    } else if (f.type == 4) {  // Gradient through an alpha mask (gradient-filled text)
        vec2 pt = (f.paintMat * vec3(fpos, 1.0)).xy;
        float d = clamp((sdroundrect(pt, f.extent, f.radius) + f.feather * 0.5) / f.feather, 0.0, 1.0);
        result = mix(f.innerCol, f.outerCol, d) * (texture(tex, ftcoord).x * scissor);
    }
    if (f.shapeType != 0) {
        shapeType = f.shapeType;
        shapeAA = f.shapeAA;
        shapeStrokeHalf = f.shapeStrokeHalf;
        shapeRect = f.shapeRect;
        shapeGrid = f.shapeGrid;
        result *= shapeCoverage((f.shapeMat * vec3(fpos, 1.0)).xy);
    }
    if (EDGE_AA != 0 && f.strokeEffect.w > 0.0) result *= strokeInnerShadow(f, strokeGrad);
    outColor = result;
}
