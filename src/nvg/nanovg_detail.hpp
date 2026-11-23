#pragma once

#ifdef _MSC_VER
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#endif

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "nanovg_context.hpp"

#if defined(NANOVG_NO_FAST_SQRT)
#undef NANOVG_FAST_SQRT
#elif !defined(NANOVG_FAST_SQRT)
#define NANOVG_FAST_SQRT
#endif

#ifndef NVG_INIT_FONTIMAGE_SIZE
#define NVG_INIT_FONTIMAGE_SIZE 512
#define NVG_MAX_FONTIMAGE_SIZE 2048
#define NVG_MAX_FONTIMAGES 4
#endif

#ifndef NVG_INIT_COMMANDS_SIZE
#define NVG_INIT_COMMANDS_SIZE 256
#endif
#ifndef NVG_INIT_POINTS_SIZE
#define NVG_INIT_POINTS_SIZE 128
#endif
#ifndef NVG_INIT_PATHS_SIZE
#define NVG_INIT_PATHS_SIZE 16
#endif
#ifndef NVG_INIT_VERTS_SIZE
#define NVG_INIT_VERTS_SIZE 256
#endif
#ifndef NVG_MAX_STATES
#define NVG_MAX_STATES 32
#endif
#ifndef NVG_KAPPA90
#define NVG_KAPPA90 0.5522847493f
#endif
#ifndef NVG_COUNTOF
#define NVG_COUNTOF(arr) (sizeof(arr) / sizeof(0 [arr]))
#endif

namespace nvg
{

namespace detail
{

// Small helpers used in the tessellation inner loops; defined here so they inline across translation units.
inline float modf(float a, float b)
{
    return ::fmodf(a, b);
}
inline float sinf(float a)
{
    return ::sinf(a);
}
inline float cosf(float a)
{
    return ::cosf(a);
}
inline float tanf(float a)
{
    return ::tanf(a);
}
inline float atan2f(float a, float b)
{
    return ::atan2f(a, b);
}
inline float acosf(float a)
{
    return ::acosf(a);
}
inline int mini(int a, int b)
{
    return a < b ? a : b;
}
inline int maxi(int a, int b)
{
    return a > b ? a : b;
}
inline int clampi(int a, int mn, int mx)
{
    return a < mn ? mn : (a > mx ? mx : a);
}
inline float minf(float a, float b)
{
    return a < b ? a : b;
}
inline float maxf(float a, float b)
{
    return a > b ? a : b;
}
inline float absf(float a)
{
    return a >= 0.0f ? a : -a;
}
inline float signf(float a)
{
    return a >= 0.0f ? 1.0f : -1.0f;
}
inline float clampf(float a, float mn, float mx)
{
    return a < mn ? mn : (a > mx ? mx : a);
}
inline float cross(float dx0, float dy0, float dx1, float dy1)
{
    return dx1 * dy0 - dx0 * dy1;
}

inline float rsqrtf(float x)
{
    float xhalf = 0.5f * x;
    uint32_t i = std::bit_cast<uint32_t>(x);
    i = 0x5f3759df - (i >> 1);
    float y = std::bit_cast<float>(i);
    y = y * (1.5f - (xhalf * y * y));
    return y;
}

inline float sqrtf(float a)
{
#if defined(NANOVG_FAST_SQRT)
    return 1.0f / std::max(std::numeric_limits<float>::epsilon(), rsqrtf(a));
#else
    return std::sqrt(a);
#endif
}

inline float normalize(float& x, float& y)
{
#if defined(NANOVG_FAST_SQRT)
    float id = rsqrtf(x * x + y * y);
    x *= id;
    y *= id;
    return 1.0f / std::max(std::numeric_limits<float>::epsilon(), id);
#else
    float d = sqrtf(x * x + y * y);
    if (d > std::numeric_limits<float>::epsilon()) {
        d = 1.0f / d;
        x *= d;
        y *= d;
    }
    return d;
#endif
}

inline int ptEquals(float x1, float y1, float x2, float y2, float tol)
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    return dx * dx + dy * dy < tol * tol;
}

inline float triarea2(float ax, float ay, float bx, float by, float cx, float cy)
{
    float abx = bx - ax;
    float aby = by - ay;
    float acx = cx - ax;
    float acy = cy - ay;
    return acx * aby - abx * acy;
}

inline void pushVertex(std::vector<Vertex>& vs, float x, float y, float u, float v, float s, float t)
{
    vs.push_back(Vertex{x, y, u, v, s, t});
}

inline float quantize(float a, float d)
{
    return (static_cast<int>(a / d + 0.5f)) * d;
}



std::shared_ptr<PathCache> allocPathCache();
CompositeOperationState compositeOperationState(int op);
float hue(float h, float m1, float m2);
void setPaintColor(Paint& p, const Color& color);
void isectRects(float* dst, float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh);
float distPtSeg(float x, float y, float px, float py, float qx, float qy);

float getAverageScale(const float* t);
float polyArea(Point* pts, int npts);
void polyReverse(Point* pts, int npts);
int curveDivs(float r, float arc, float tol);
void chooseBevel(int bevel, Point* p0, Point* p1, float w, float* x0, float* y0, float* x1, float* y1);
void roundJoin(std::vector<Vertex>& out, Point* p0, Point* p1, float lw, float rw, float lu, float ru, int ncap, float fringe, float t);
void bevelJoin(std::vector<Vertex>& out, Point* p0, Point* p1, float lw, float rw, float lu, float ru, float fringe, float t);
void insertSpacer(std::vector<Vertex>& out, Point* p, float dx, float dy, float w, float u0, float u1, float t);
void buttCapStart(std::vector<Vertex>& out, Point* p, float dx, float dy, float w, float d, float aa, float u0, float u1, float t, int dir);
void buttCapEnd(std::vector<Vertex>& out, Point* p, float dx, float dy, float w, float d, float aa, float u0, float u1, float t, int dir);
void roundCapStart(std::vector<Vertex>& out, Point* p, float dx, float dy, float w, int ncap, float aa, float u0, float u1, float t, int dir);
void roundCapEnd(std::vector<Vertex>& out, Point* p, float dx, float dy, float w, int ncap, float aa, float u0, float u1, float t, int dir);
float getFontScale(const State* state);
int isTransformFlipped(const float* xform);

}  // namespace detail

}  // namespace nvg
