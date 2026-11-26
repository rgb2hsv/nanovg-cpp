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

#include "nanovg.hpp"

#ifdef _MSC_VER
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <vector>
#include <algorithm>
#include <array>
#include <memory>
#include <exception>
#include <stdexcept>
#include <ranges>
#include <concepts>
#include <span>
#include <bit>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstring>

#define Expects(condition)                                                                                     \
    do {                                                                                                       \
        if (!(condition)) {                                                                                    \
            printf("Precondition failed: %s\n  at %s:%d in %s()\n", #condition, __FILE__, __LINE__, __func__); \
            std::terminate();                                                                                  \
        }                                                                                                      \
    } while (0)

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100)  // unreferenced formal parameter
#pragma warning(disable : 4127)  // conditional expression is constant
#pragma warning(disable : 4204)  // nonstandard extension used : non-constant aggregate initializer
#pragma warning(disable : 4706)  // assignment within conditional expression
#endif
#define FONTSTASH_IMPLEMENTATION
#include "../thirdParty/fontstash.h"

#ifndef NVG_NO_STB
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "nanovg_detail.hpp"

namespace nvg
{

Context::Context(const Params& params) : mImpl(std::make_unique<ContextImpl>(params))
{
    // Like nvgCreateInternal in C NanoVG: start with a valid default state so text can be
    // measured before the first beginFrame(). Otherwise the state has a zero transform,
    // font scale is 0 and nvgTextBounds returns NaN.
    save();
    reset();
}

Context::~Context() = default;

ContextImpl& Context::operator*() noexcept
{
    return *mImpl;
}

const ContextImpl& Context::operator*() const noexcept
{
    return *mImpl;
}

ContextImpl* Context::operator->() noexcept
{
    return mImpl.get();
}

const ContextImpl* Context::operator->() const noexcept
{
    return mImpl.get();
}

ContextImpl::~ContextImpl()
{
    for (size_t i = 0; i < fontImages.size(); ++i) {
        if (fontImages[i] != 0) {
            params.renderDeleteTexture(params.userPtr, fontImages[i]);
            fontImages[i] = 0;
        }
    }
    for (ShadowImage& s : shadowImages)
        if (s.image != 0) params.renderDeleteTexture(params.userPtr, s.image);

    if (params.renderDelete != nullptr) params.renderDelete(params.userPtr);
}
ContextImpl::ContextImpl(const Params& params) : params(params)
{
    FONSparams fontParams{};
    fontImages.resize(NVG_MAX_FONTIMAGES, 0);

    cache = detail::allocPathCache();

    setDevicePixelRatio(1.0f);

    if (params.renderCreate(params.userPtr) == 0) throw std::runtime_error("Could not init render context.");

    // Init font rendering
    fontParams.width = NVG_INIT_FONTIMAGE_SIZE;
    fontParams.height = NVG_INIT_FONTIMAGE_SIZE;
    fontParams.flags = FONS_ZERO_TOPLEFT;
    fontParams.renderCreate = nullptr;
    fontParams.renderUpdate = nullptr;
    fontParams.renderDraw = nullptr;
    fontParams.renderDelete = nullptr;
    fontParams.userPtr = nullptr;
    FONScontext* rawFs = fonsCreateInternal(&fontParams);
    if (rawFs == nullptr) throw std::runtime_error("Could not create font context");
    fs = std::shared_ptr<FONScontext>(rawFs, [](FONScontext* p) { fonsDeleteInternal(p); });

    // Create font texture
    fontImages[0] = params.renderCreateTexture(params.userPtr, static_cast<int>(Texture::Alpha), fontParams.width, fontParams.height, 0, nullptr);
    if (fontImages[0] == 0) throw std::runtime_error("Could not create font texture");
    fontImageIdx = 0;
    scissor = ScissorBounds{0.0f, 0.0f, -1.0f, -1.0f};
}

Color rgb(unsigned char r, unsigned char g, unsigned char b)
{
    return rgba(r, g, b, 255);
}

Color rgbf(float r, float g, float b)
{
    return rgbaf(r, g, b, 1.0f);
}

Color rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    Color color;
    // Use longer initialization to suppress warning.
    color.r = r / 255.0f;
    color.g = g / 255.0f;
    color.b = b / 255.0f;
    color.a = a / 255.0f;
    return color;
}

Color rgbaf(float r, float g, float b, float a)
{
    Color color;
    // Use longer initialization to suppress warning.
    color.r = r;
    color.g = g;
    color.b = b;
    color.a = a;
    return color;
}

Color transRgba(Color c, unsigned char a)
{
    c.a = a / 255.0f;
    return c;
}

Color transRgbaf(Color c, float a)
{
    c.a = a;
    return c;
}

Color lerpRgba(Color c0, Color c1, float u)
{
    int i;
    float oneminu;
    Color cint = {{{0}}};

    u = detail::clampf(u, 0.0f, 1.0f);
    oneminu = 1.0f - u;
    for (i = 0; i < 4; i++) {
        cint.rgba[i] = c0.rgba[i] * oneminu + c1.rgba[i] * u;
    }

    return cint;
}

Color hsl(float h, float s, float l)
{
    return hsla(h, s, l, 255);
}

Color hsla(float h, float s, float l, unsigned char a)
{
    float m1, m2;
    Color col;
    h = detail::modf(h, 1.0f);
    if (h < 0.0f) h += 1.0f;
    s = detail::clampf(s, 0.0f, 1.0f);
    l = detail::clampf(l, 0.0f, 1.0f);
    m2 = l <= 0.5f ? (l * (1 + s)) : (l + s - l * s);
    m1 = 2 * l - m2;
    col.r = detail::clampf(detail::hue(h + 1.0f / 3.0f, m1, m2), 0.0f, 1.0f);
    col.g = detail::clampf(detail::hue(h, m1, m2), 0.0f, 1.0f);
    col.b = detail::clampf(detail::hue(h - 1.0f / 3.0f, m1, m2), 0.0f, 1.0f);
    col.a = a / 255.0f;
    return col;
}

void transformIdentity(float* t)
{
    t[0] = 1.0f;
    t[1] = 0.0f;
    t[2] = 0.0f;
    t[3] = 1.0f;
    t[4] = 0.0f;
    t[5] = 0.0f;
}

void transformTranslate(float* t, float tx, float ty)
{
    t[0] = 1.0f;
    t[1] = 0.0f;
    t[2] = 0.0f;
    t[3] = 1.0f;
    t[4] = tx;
    t[5] = ty;
}

void transformScale(float* t, float sx, float sy)
{
    t[0] = sx;
    t[1] = 0.0f;
    t[2] = 0.0f;
    t[3] = sy;
    t[4] = 0.0f;
    t[5] = 0.0f;
}

void transformRotate(float* t, float a)
{
    float cs = cosf(a), sn = sinf(a);
    t[0] = cs;
    t[1] = sn;
    t[2] = -sn;
    t[3] = cs;
    t[4] = 0.0f;
    t[5] = 0.0f;
}

void transformSkewX(float* t, float a)
{
    t[0] = 1.0f;
    t[1] = 0.0f;
    t[2] = tanf(a);
    t[3] = 1.0f;
    t[4] = 0.0f;
    t[5] = 0.0f;
}

void transformSkewY(float* t, float a)
{
    t[0] = 1.0f;
    t[1] = tanf(a);
    t[2] = 0.0f;
    t[3] = 1.0f;
    t[4] = 0.0f;
    t[5] = 0.0f;
}

void transformMultiply(float* t, const float* s)
{
    float t0 = t[0] * s[0] + t[1] * s[2];
    float t2 = t[2] * s[0] + t[3] * s[2];
    float t4 = t[4] * s[0] + t[5] * s[2] + s[4];
    t[1] = t[0] * s[1] + t[1] * s[3];
    t[3] = t[2] * s[1] + t[3] * s[3];
    t[5] = t[4] * s[1] + t[5] * s[3] + s[5];
    t[0] = t0;
    t[2] = t2;
    t[4] = t4;
}

void transformPremultiply(float* t, const float* s)
{
    std::array<float, 6> s2{};
    memcpy(s2.data(), s, sizeof(float) * 6);
    transformMultiply(s2.data(), t);
    memcpy(t, s2.data(), sizeof(float) * 6);
}

int transformInverse(float* inv, const float* t)
{
    double invdet, det = (double)t[0] * t[3] - (double)t[2] * t[1];
    if (det > -1e-6 && det < 1e-6) {
        transformIdentity(inv);
        return 0;
    }
    invdet = 1.0 / det;
    inv[0] = (float)(t[3] * invdet);
    inv[2] = (float)(-t[2] * invdet);
    inv[4] = (float)(((double)t[2] * t[5] - (double)t[3] * t[4]) * invdet);
    inv[1] = (float)(-t[1] * invdet);
    inv[3] = (float)(t[0] * invdet);
    inv[5] = (float)(((double)t[1] * t[4] - (double)t[0] * t[5]) * invdet);
    return 1;
}

void transformPoint(float& dx, float& dy, const float* t, float sx, float sy)
{
    dx = sx * t[0] + sy * t[2] + t[4];
    dy = sx * t[1] + sy * t[3] + t[5];
}

float degToRad(float deg)
{
    return deg / 180.0f * (float)M_PI;
}

float radToDeg(float rad)
{
    return rad / (float)M_PI * 180.0f;
}

void Context::beginFrame(float windowWidth, float windowHeight, float devicePixelRatio)
{
    mImpl->states.clear();
    save();
    reset();

    mImpl->setDevicePixelRatio(devicePixelRatio);
    mImpl->viewWidth = windowWidth;
    mImpl->viewHeight = windowHeight;

    mImpl->params.renderViewport(mImpl->params.userPtr, windowWidth, windowHeight, devicePixelRatio);

    mImpl->shadowImageIdx = 0;
    mImpl->drawCallCount = 0;
    mImpl->fillTriCount = 0;
    mImpl->strokeTriCount = 0;
    mImpl->textTriCount = 0;
}

void Context::cancelFrame()
{
    mImpl->params.renderCancel(mImpl->params.userPtr);
}

void Context::endFrame()
{
    mImpl->params.renderFlush(mImpl->params.userPtr);
    if (mImpl->fontImageIdx != 0) {
        int fontImage = mImpl->fontImages[mImpl->fontImageIdx];
        mImpl->fontImages[mImpl->fontImageIdx] = 0;
        int iw, ih;
        if (fontImage == 0) return;
        imageSize(fontImage, iw, ih);
        size_t j = 0;
        for (size_t i = 0; i < mImpl->fontImageIdx; i++) {
            if (mImpl->fontImages[i] != 0) {
                int nw, nh;
                int image = mImpl->fontImages[i];
                mImpl->fontImages[i] = 0;
                imageSize(image, nw, nh);
                if (nw < iw || nh < ih)
                    deleteImage(image);
                else
                    mImpl->fontImages[j++] = image;
            }
        }
        mImpl->fontImages[j] = mImpl->fontImages[0];
        mImpl->fontImages[0] = fontImage;
        mImpl->fontImageIdx = 0;
    }
}

void Context::save()
{
    if (!mImpl->states.empty())
        mImpl->states.push_back(mImpl->states.back());
    else
        mImpl->states.push_back({});
}

void Context::restore()
{
    if (mImpl->states.size() <= 1) return;

    mImpl->states.pop_back();
}

void Context::reset()
{
    State& state = mImpl->topState();
    detail::setPaintColor(state.fill, rgba(255, 255, 255, 255));
    detail::setPaintColor(state.stroke, rgba(0, 0, 0, 255));
    state.compositeOperation = detail::compositeOperationState(static_cast<int>(CompositeOperation::SourceOver));
    state.shapeAntiAlias = 1;
    state.strokeWidth = 1.0f;
    state.miterLimit = 10.0f;
    state.lineCap = static_cast<int>(LineCap::Butt);
    state.lineJoin = static_cast<int>(LineCap::Miter);
    state.lineStyle = static_cast<int>(LineStyle::Solid);
    state.ndashes = 0;
    state.dashOffset = 0.0f;
    state.markers = {};
    state.strokeOuterShadowX = state.strokeOuterShadowY = state.strokeOuterShadowBlur = 0.0f;
    state.strokeOuterShadowColor = Color{};
    state.strokeInnerShadowX = state.strokeInnerShadowY = state.strokeInnerShadowBlur = 0.0f;
    state.strokeInnerShadowColor = Color{};
    state.strokeOutlineWidth = 0.0f;
    state.strokeOutlineColor = Color{};
    state.strokeBlur = 0.0f;
    state.alpha = 1.0f;
    transformIdentity(state.xform.data());

    state.scissor.extent[0] = -1.0f;
    state.scissor.extent[1] = -1.0f;

    state.fontSize = 16.0f;
    state.letterSpacing = 0.0f;
    state.lineHeight = 1.0f;
    state.fontBlur = 0.0f;
    state.textOuterShadowX = state.textOuterShadowY = state.textOuterShadowBlur = 0.0f;
    state.textOuterShadowColor = Color{};
    state.textInnerShadowX = state.textInnerShadowY = state.textInnerShadowBlur = 0.0f;
    state.textInnerShadowColor = Color{};
    state.fontDilate = 0.0f;
    state.textAlign = static_cast<int>(Align::Left | Align::Baseline);
    state.fontId = 0;
    state.textFill = TextFill::Color;
}

// State setting
const ScissorBounds& Context::currentScissor() const
{
    return mImpl->scissor;
}

void Context::shapeAntiAlias(int enabled)
{
    State& state = mImpl->topState();
    state.shapeAntiAlias = enabled;
}

void Context::strokeWidth(float width)
{
    State& state = mImpl->topState();
    state.strokeWidth = width;
}

float Context::getStrokeWidth() const
{
    const State& state = (*this)->topState();
    return state.strokeWidth;
}

int Context::getTextAlign() const
{
    const State& state = (*this)->topState();
    return state.textAlign;
}

float Context::getFontSize() const
{
    const State& state = (*this)->topState();
    return state.fontSize;
}
int Context::getFontFaceId() const
{
    const State& state = (*this)->topState();
    return state.fontId;
}

void Context::miterLimit(float limit)
{
    State& state = mImpl->topState();
    state.miterLimit = limit;
}

void Context::lineStyle(int lineStyle)
{
    State& state = mImpl->topState();
    state.lineStyle = lineStyle;
}

void Context::strokeDashArray(const float* dashes, int count)
{
    State& state = mImpl->topState();
    state.ndashes = 0;
    if (dashes == nullptr || count <= 0) return;
    count = std::min(count, maxStrokeDashes);
    float total = 0.0f;
    for (int i = 0; i < count; i++) {
        if (!std::isfinite(dashes[i]) || dashes[i] < 0.0f) return;  // invalid, as in SVG: render solid
        total += dashes[i];
    }
    if (!(total > 0.0f)) return;
    // An odd-length list is repeated to make it even, so dashes and gaps keep alternating.
    const int n = (count % 2) ? count * 2 : count;
    for (int i = 0; i < n; i++) state.dashes[static_cast<size_t>(i)] = dashes[i % count];
    state.ndashes = n;
}

void Context::strokeDashArray(const char* dashArray)
{
    std::array<float, maxStrokeDashes> values{};
    int count = 0;
    const char* p = dashArray;
    auto skipSeparators = [&] {
        while (p != nullptr && (*p == ',' || std::isspace(static_cast<unsigned char>(*p)))) p++;
    };
    skipSeparators();
    if (p == nullptr || *p == '\0' || std::strncmp(p, "none", 4) == 0) {
        strokeDashArray(nullptr, 0);
        return;
    }
    while (*p != '\0') {
        char* end = nullptr;
        const float v = std::strtof(p, &end);
        if (end == p) {
            strokeDashArray(nullptr, 0);  // not a number: invalid value, render solid
            return;
        }
        p = end;
        if (p[0] == 'p' && p[1] == 'x') p += 2;
        if (*p != '\0' && *p != ',' && !std::isspace(static_cast<unsigned char>(*p))) {
            strokeDashArray(nullptr, 0);
            return;
        }
        if (count < maxStrokeDashes) values[static_cast<size_t>(count++)] = v;
        skipSeparators();
    }
    strokeDashArray(values.data(), count);
}

void Context::strokeDashOffset(float offset)
{
    State& state = mImpl->topState();
    state.dashOffset = std::isfinite(offset) ? offset : 0.0f;
}

void Context::markerStart(const Marker& marker)
{
    mImpl->topState().markers[0] = marker;
}

void Context::markerMid(const Marker& marker)
{
    mImpl->topState().markers[1] = marker;
}

void Context::markerEnd(const Marker& marker)
{
    mImpl->topState().markers[2] = marker;
}

void Context::markers(const Marker& marker)
{
    auto& m = mImpl->topState().markers;
    m[0] = m[1] = m[2] = marker;
}

namespace
{
float finiteOr0(float v)
{
    return std::isfinite(v) ? v : 0.0f;
}
float nonNegative(float v)
{
    return std::isfinite(v) ? std::max(v, 0.0f) : 0.0f;
}
}  // namespace

void Context::strokeOuterShadow(float offsetX, float offsetY, float blur, const Color& color)
{
    State& state = mImpl->topState();
    state.strokeOuterShadowX = finiteOr0(offsetX);
    state.strokeOuterShadowY = finiteOr0(offsetY);
    state.strokeOuterShadowBlur = nonNegative(blur);
    state.strokeOuterShadowColor = color;
}

void Context::strokeInnerShadow(float offsetX, float offsetY, float blur, const Color& color)
{
    State& state = mImpl->topState();
    state.strokeInnerShadowX = finiteOr0(offsetX);
    state.strokeInnerShadowY = finiteOr0(offsetY);
    state.strokeInnerShadowBlur = nonNegative(blur);
    state.strokeInnerShadowColor = color;
}

void Context::strokeOutline(float width, const Color& color)
{
    State& state = mImpl->topState();
    state.strokeOutlineWidth = nonNegative(width);
    state.strokeOutlineColor = color;
}

void Context::strokeBlur(float blur)
{
    mImpl->topState().strokeBlur = nonNegative(blur);
}

void Context::lineCap(int cap)
{
    State& state = mImpl->topState();
    state.lineCap = cap;
}

void Context::lineJoin(int join)
{
    State& state = mImpl->topState();
    state.lineJoin = join;
}

void Context::globalAlpha(float alpha)
{
    State& state = mImpl->topState();
    state.alpha = alpha;
}

void Context::transform(float a, float b, float c, float d, float e, float f)
{
    State& state = mImpl->topState();
    const std::array<float, 6> t = {a, b, c, d, e, f};
    transformPremultiply(state.xform.data(), t.data());
}

void Context::resetTransform()
{
    State& state = mImpl->topState();
    transformIdentity(state.xform.data());
}

void Context::translate(float x, float y)
{
    State& state = mImpl->topState();
    std::array<float, 6> t{};
    transformTranslate(t.data(), x, y);
    transformPremultiply(state.xform.data(), t.data());
}

void Context::rotate(float angle)
{
    State& state = mImpl->topState();
    std::array<float, 6> t{};
    transformRotate(t.data(), angle);
    transformPremultiply(state.xform.data(), t.data());
}

void Context::skewX(float angle)
{
    State& state = mImpl->topState();
    std::array<float, 6> t{};
    transformSkewX(t.data(), angle);
    transformPremultiply(state.xform.data(), t.data());
}

void Context::skewY(float angle)
{
    State& state = mImpl->topState();
    std::array<float, 6> t{};
    transformSkewY(t.data(), angle);
    transformPremultiply(state.xform.data(), t.data());
}

void Context::scale(float x, float y)
{
    State& state = mImpl->topState();
    std::array<float, 6> t{};
    transformScale(t.data(), x, y);
    transformPremultiply(state.xform.data(), t.data());
}

void Context::currentTransform(float* xform) const
{
    const State& state = (*this)->topState();
    if (xform == NULL) return;
    memcpy(xform, state.xform.data(), sizeof(float) * 6);
}

void Context::strokeColor(const Color& color)
{
    State& state = mImpl->topState();
    detail::setPaintColor(state.stroke, color);
}

void Context::strokePaint(const Paint& paint)
{
    State& state = mImpl->topState();
    state.stroke = paint;
    transformMultiply(state.stroke.xform, state.xform.data());
}

void Context::fillColor(const Color& color)
{
    State& state = mImpl->topState();
    detail::setPaintColor(state.fill, color);
}

void Context::fillPaint(const Paint& paint)
{
    State& state = mImpl->topState();
    state.fill = paint;
    transformMultiply(state.fill.xform, state.xform.data());
}

#ifndef NVG_NO_STB
int Context::createImage(const char* filename, int imageFlags)
{
    int w, h, n, image;
    unsigned char* img;
    stbi_set_unpremultiply_on_load(1);
    stbi_convert_iphone_png_to_rgb(1);
    img = stbi_load(filename, &w, &h, &n, 4);
    if (img == NULL) {
        //		printf("Failed to load %s - %s\n", filename, stbi_failure_reason());
        return 0;
    }
    image = createImageRGBA(w, h, imageFlags, img);
    stbi_image_free(img);
    return image;
}

int Context::createImageMem(int imageFlags, unsigned char* data, int ndata)
{
    int w, h, n, image;
    stbi_set_unpremultiply_on_load(1);
    stbi_convert_iphone_png_to_rgb(1);
    unsigned char* img = stbi_load_from_memory(data, ndata, &w, &h, &n, 4);
    if (img == NULL) {
        //		printf("Failed to load %s - %s\n", filename, stbi_failure_reason());
        return 0;
    }
    image = createImageRGBA(w, h, imageFlags, img);
    stbi_image_free(img);
    return image;
}
#endif

int Context::createImageRGBA(int w, int h, int imageFlags, const unsigned char* data)
{
    return mImpl->params.renderCreateTexture(mImpl->params.userPtr, static_cast<int>(Texture::Rgba), w, h, imageFlags, data);
}

void Context::updateImage(int image, const unsigned char* data)
{
    int w, h;
    mImpl->params.renderGetTextureSize(mImpl->params.userPtr, image, &w, &h);
    mImpl->params.renderUpdateTexture(mImpl->params.userPtr, image, 0, 0, w, h, data);
}

void Context::imageSize(int image, int& w, int& h) const
{
    (*this)->params.renderGetTextureSize((*this)->params.userPtr, image, &w, &h);
}

void Context::deleteImage(int image)
{
    mImpl->params.renderDeleteTexture(mImpl->params.userPtr, image);
}

Paint Context::linearGradient(float sx, float sy, float ex, float ey, const Color& icol, const Color& ocol) const
{
    Paint p{};
    float dx, dy, d;
    const float large = 1e5;

    // Calculate transform aligned to the line
    dx = ex - sx;
    dy = ey - sy;
    d = sqrtf(dx * dx + dy * dy);
    if (d > 0.0001f) {
        dx /= d;
        dy /= d;
    } else {
        dx = 0;
        dy = 1;
    }

    p.xform[0] = dy;
    p.xform[1] = -dx;
    p.xform[2] = dx;
    p.xform[3] = dy;
    p.xform[4] = sx - dx * large;
    p.xform[5] = sy - dy * large;

    p.extent[0] = large;
    p.extent[1] = large + d * 0.5f;

    p.radius = 0.0f;

    p.feather = detail::maxf(1.0f, d);

    p.innerColor = icol;
    p.outerColor = ocol;

    return p;
}

Paint Context::radialGradient(float cx, float cy, float inr, float outr, const Color& icol, const Color& ocol) const
{
    Paint p{};
    float r = (inr + outr) * 0.5f;
    float f = (outr - inr);

    transformIdentity(p.xform);
    p.xform[4] = cx;
    p.xform[5] = cy;

    p.extent[0] = r;
    p.extent[1] = r;

    p.radius = r;

    p.feather = detail::maxf(1.0f, f);

    p.innerColor = icol;
    p.outerColor = ocol;

    return p;
}

Paint Context::boxGradient(float x, float y, float w, float h, float r, float f, const Color& icol, const Color& ocol) const
{
    Paint p{};

    transformIdentity(p.xform);
    p.xform[4] = x + w * 0.5f;
    p.xform[5] = y + h * 0.5f;

    p.extent[0] = w * 0.5f;
    p.extent[1] = h * 0.5f;

    p.radius = r;

    p.feather = detail::maxf(1.0f, f);

    p.innerColor = icol;
    p.outerColor = ocol;

    return p;
}

Paint Context::imagePattern(float cx, float cy, float w, float h, float angle, int image, float alpha) const
{
    Paint p{};

    transformRotate(p.xform, angle);
    p.xform[4] = cx;
    p.xform[5] = cy;

    p.extent[0] = w;
    p.extent[1] = h;

    p.image = image;

    p.innerColor = p.outerColor = rgbaf(1, 1, 1, alpha);

    return p;
}

// Scissoring
void Context::scissor(float x, float y, float w, float h)
{
    State& state = mImpl->topState();
    mImpl->scissor = ScissorBounds{x, y, w, h};
    w = detail::maxf(0.0f, w);
    h = detail::maxf(0.0f, h);

    transformIdentity(state.scissor.xform);
    state.scissor.xform[4] = x + w * 0.5f;
    state.scissor.xform[5] = y + h * 0.5f;
    transformMultiply(state.scissor.xform, state.xform.data());

    state.scissor.extent[0] = w * 0.5f;
    state.scissor.extent[1] = h * 0.5f;
}

void Context::intersectScissor(float x, float y, float w, float h)
{
    State& state = mImpl->topState();
    std::array<float, 6> pxform{};
    std::array<float, 6> invxorm{};
    std::array<float, 4> rect{};
    float ex, ey, tex, tey;

    // If no previous scissor has been set, set the scissor as current scissor.
    if (state.scissor.extent[0] < 0) {
        scissor(x, y, w, h);
        return;
    }

    // Transform the current scissor rect into current transform space.
    // If there is difference in rotation, this will be approximation.
    memcpy(pxform.data(), state.scissor.xform, sizeof(float) * 6);
    ex = state.scissor.extent[0];
    ey = state.scissor.extent[1];
    transformInverse(invxorm.data(), state.xform.data());
    transformMultiply(pxform.data(), invxorm.data());
    tex = ex * detail::absf(pxform[0]) + ey * detail::absf(pxform[2]);
    tey = ex * detail::absf(pxform[1]) + ey * detail::absf(pxform[3]);

    // Intersect rects.
    detail::isectRects(rect.data(), pxform[4] - tex, pxform[5] - tey, tex * 2, tey * 2, x, y, w, h);

    scissor(rect[0], rect[1], rect[2], rect[3]);
}

void Context::resetScissor()
{
    State& state = mImpl->topState();
    state.scissor = Scissor{};
    state.scissor.extent[0] = -1.0f;
    state.scissor.extent[1] = -1.0f;
}

// Global composite operation.
void Context::globalCompositeOperation(int op)
{
    State& state = mImpl->topState();
    state.compositeOperation = detail::compositeOperationState(op);
}

void Context::globalCompositeBlendFunc(int sfactor, int dfactor)
{
    globalCompositeBlendFuncSeparate(sfactor, dfactor, sfactor, dfactor);
}

void Context::globalCompositeBlendFuncSeparate(int srcRGB, int dstRGB, int srcAlpha, int dstAlpha)
{
    CompositeOperationState op;
    op.srcRGB = srcRGB;
    op.dstRGB = dstRGB;
    op.srcAlpha = srcAlpha;
    op.dstAlpha = dstAlpha;

    State& state = mImpl->topState();
    state.compositeOperation = op;
}

// Draw
void Context::beginPath()
{
    mImpl->commands.clear();
    mImpl->clearPathCache();
}

void Context::moveTo(float x, float y)
{
    std::array<float, 3> vals = {static_cast<float>(to_underlying(PathCommand::MoveTo)), x, y};
    mImpl->appendCommands(vals);
}

void Context::lineTo(float x, float y)
{
    std::array<float, 3> vals = {static_cast<float>(to_underlying(PathCommand::LineTo)), x, y};
    mImpl->appendCommands(vals);
}

void Context::bezierTo(float c1x, float c1y, float c2x, float c2y, float x, float y)
{
    std::array<float, 7> vals = {static_cast<float>(to_underlying(PathCommand::BezierTo)), c1x, c1y, c2x, c2y, x, y};
    mImpl->appendCommands(vals);
}

void Context::quadTo(float cx, float cy, float x, float y)
{
    float x0 = mImpl->commandx;
    float y0 = mImpl->commandy;
    std::array<float, 7> vals = {
        static_cast<float>(to_underlying(PathCommand::BezierTo)), x0 + 2.0f / 3.0f * (cx - x0), y0 + 2.0f / 3.0f * (cy - y0), x + 2.0f / 3.0f * (cx - x), y + 2.0f / 3.0f * (cy - y), x, y};
    mImpl->appendCommands(vals);
}

void Context::arcTo(float x1, float y1, float x2, float y2, float radius)
{
    float x0 = mImpl->commandx;
    float y0 = mImpl->commandy;
    float dx0, dy0, dx1, dy1, a, d, cx, cy, a0, a1;
    int dir;

    if (mImpl->commands.empty()) {
        return;
    }

    // Handle degenerate cases.
    if (detail::ptEquals(x0, y0, x1, y1, mImpl->distTol) || detail::ptEquals(x1, y1, x2, y2, mImpl->distTol) || detail::distPtSeg(x1, y1, x0, y0, x2, y2) < mImpl->distTol * mImpl->distTol ||
        radius < mImpl->distTol) {
        lineTo(x1, y1);
        return;
    }

    // Calculate tangential circle to lines (x0,y0)-(x1,y1) and (x1,y1)-(x2,y2).
    dx0 = x0 - x1;
    dy0 = y0 - y1;
    dx1 = x2 - x1;
    dy1 = y2 - y1;
    detail::normalize(dx0, dy0);
    detail::normalize(dx1, dy1);
    a = acosf(dx0 * dx1 + dy0 * dy1);
    d = radius / tanf(a / 2.0f);

    //	printf("a=%f° d=%f\n", a/M_PI*180.0f, d);

    if (d > 10000.0f) {
        lineTo(x1, y1);
        return;
    }

    if (detail::cross(dx0, dy0, dx1, dy1) > 0.0f) {
        cx = x1 + dx0 * d + dy0 * radius;
        cy = y1 + dy0 * d + -dx0 * radius;
        a0 = atan2f(dx0, -dy0);
        a1 = atan2f(-dx1, dy1);
        dir = static_cast<int>(Winding::CW);
        //		printf("CW c=(%f, %f) a0=%f° a1=%f°\n", cx, cy, a0/M_PI*180.0f, a1/M_PI*180.0f);
    } else {
        cx = x1 + dx0 * d + -dy0 * radius;
        cy = y1 + dy0 * d + dx0 * radius;
        a0 = atan2f(-dx0, dy0);
        a1 = atan2f(dx1, -dy1);
        dir = static_cast<int>(Winding::CCW);
        //		printf("CCW c=(%f, %f) a0=%f° a1=%f°\n", cx, cy, a0/M_PI*180.0f, a1/M_PI*180.0f);
    }

    arc(cx, cy, radius, a0, a1, dir);
}

void Context::closePath()
{
    std::array<float, 1> vals = {static_cast<float>(to_underlying(PathCommand::Close))};
    mImpl->appendCommands(vals);
}

void Context::pathWinding(int dir)
{
    std::array<float, 2> vals = {static_cast<float>(to_underlying(PathCommand::Winding)), (float)dir};
    mImpl->appendCommands(vals);
}

namespace
{
// Turns [a0, a1] into a signed sweep in direction dir, as arc() always has: at most one full turn.
float clampSweep(float a0, float a1, int dir)
{
    const float twoPi = (float)M_PI * 2.0f;
    float da = a1 - a0;
    if (dir == static_cast<int>(Winding::CW)) {
        if (detail::absf(da) >= twoPi) {
            da = twoPi;
        } else {
            while (da < 0.0f) da += twoPi;
        }
    } else {
        if (detail::absf(da) >= twoPi) {
            da = -twoPi;
        } else {
            while (da > 0.0f) da -= twoPi;
        }
    }
    return da;
}
}  // namespace

void Context::appendEllipticalArc(float cx, float cy, float rx, float ry, float rotation, float a0, float da, bool connect)
{
    const float pi = (float)M_PI;
    const float cr = cosf(rotation), sr = sinf(rotation);
    std::array<float, 3 + 5 * 7> vals{};
    size_t nvals = 0;

    // Split the arc into max 90 degree segments.
    const int ndivs = detail::maxi(1, detail::mini((int)(detail::absf(da) / (pi * 0.5f) + 0.5f), 5));
    const float hda = (da / (float)ndivs) / 2.0f;
    float kappa = detail::absf(hda) > 1e-6f ? detail::absf(4.0f / 3.0f * (1.0f - cosf(hda)) / sinf(hda)) : 0.0f;
    if (da < 0.0f) kappa = -kappa;

    float px = 0, py = 0, ptanx = 0, ptany = 0;
    for (int i = 0; i <= ndivs; i++) {
        const float a = a0 + da * (i / (float)ndivs);
        const float ca = cosf(a), sa = sinf(a);
        // Point and (kappa-scaled) tangent on the axis-aligned ellipse, then rotated.
        const float lx = ca * rx, ly = sa * ry;
        const float ltx = -sa * rx * kappa, lty = ca * ry * kappa;
        const float x = cx + cr * lx - sr * ly, y = cy + sr * lx + cr * ly;
        const float tanx = cr * ltx - sr * lty, tany = sr * ltx + cr * lty;

        if (i == 0) {
            if (connect) {
                const PathCommand move = !mImpl->commands.empty() ? PathCommand::LineTo : PathCommand::MoveTo;
                vals[nvals++] = static_cast<float>(to_underlying(move));
                vals[nvals++] = x;
                vals[nvals++] = y;
            }
        } else {
            // Later pieces continue the first: the arc is one segment for stroke markers.
            vals[nvals++] = static_cast<float>(to_underlying(i == 1 ? PathCommand::BezierTo : PathCommand::BezierContinue));
            vals[nvals++] = px + ptanx;
            vals[nvals++] = py + ptany;
            vals[nvals++] = x - tanx;
            vals[nvals++] = y - tany;
            vals[nvals++] = x;
            vals[nvals++] = y;
        }
        px = x;
        py = y;
        ptanx = tanx;
        ptany = tany;
    }

    if (nvals == 0) return;
    std::span<float> span(vals.data(), nvals);
    mImpl->appendCommands(span);
}

void Context::arc(float cx, float cy, float r, float a0, float a1, int dir)
{
    appendEllipticalArc(cx, cy, r, r, 0.0f, a0, clampSweep(a0, a1, dir), true);
}

void Context::ellipticalArc(float cx, float cy, float rx, float ry, float rotation, float a0, float a1, int dir)
{
    appendEllipticalArc(cx, cy, rx, ry, rotation, a0, clampSweep(a0, a1, dir), true);
}

void Context::ellipticalArcTo(float rx, float ry, float xAxisRotation, bool largeArc, bool sweep, float x, float y)
{
    if (mImpl->commands.empty()) {
        moveTo(x, y);
        return;
    }
    // The current point (commandx / commandy) is kept in local coordinates.
    const float x1 = mImpl->commandx, y1 = mImpl->commandy;
    // Same endpoints: SVG omits the arc. distTol is in device pixels, so scale it to local units.
    const float scale = detail::getAverageScale(mImpl->topState().xform.data());
    if (detail::ptEquals(x1, y1, x, y, scale > 0.0f ? mImpl->distTol / scale : mImpl->distTol)) return;
    rx = detail::absf(rx);
    ry = detail::absf(ry);
    if (!(rx > 0.0f) || !(ry > 0.0f) || !std::isfinite(rx) || !std::isfinite(ry)) {
        lineTo(x, y);
        return;
    }

    // Endpoint to center parameterization (SVG 1.1 implementation notes, F.6.5 and F.6.6).
    const double cphi = cos((double)xAxisRotation), sphi = sin((double)xAxisRotation);
    const double hx = (x1 - (double)x) * 0.5, hy = (y1 - (double)y) * 0.5;
    const double x1p = cphi * hx + sphi * hy;
    const double y1p = -sphi * hx + cphi * hy;
    double rxd = rx, ryd = ry;
    const double lambda = (x1p * x1p) / (rxd * rxd) + (y1p * y1p) / (ryd * ryd);
    if (lambda > 1.0) {
        const double k = sqrt(lambda);
        rxd *= k;
        ryd *= k;
    }
    const double rx2 = rxd * rxd, ry2 = ryd * ryd;
    const double den = rx2 * y1p * y1p + ry2 * x1p * x1p;
    double coef = den > 0.0 ? sqrt(std::max(0.0, (rx2 * ry2 - den) / den)) : 0.0;
    if (largeArc == sweep) coef = -coef;
    const double cxp = coef * rxd * y1p / ryd;
    const double cyp = -coef * ryd * x1p / rxd;
    const double cx = cphi * cxp - sphi * cyp + (x1 + (double)x) * 0.5;
    const double cy = sphi * cxp + cphi * cyp + (y1 + (double)y) * 0.5;

    auto angle = [](double ux, double uy, double vx, double vy) { return atan2(ux * vy - uy * vx, ux * vx + uy * vy); };
    const double ux = (x1p - cxp) / rxd, uy = (y1p - cyp) / ryd;
    const double vx = (-x1p - cxp) / rxd, vy = (-y1p - cyp) / ryd;
    const double theta1 = angle(1.0, 0.0, ux, uy);
    double dtheta = angle(ux, uy, vx, vy);
    if (!sweep && dtheta > 0.0) dtheta -= 2.0 * M_PI;
    if (sweep && dtheta < 0.0) dtheta += 2.0 * M_PI;

    appendEllipticalArc((float)cx, (float)cy, (float)rxd, (float)ryd, xAxisRotation, (float)theta1, (float)dtheta, false);
}

void Context::rect(float x, float y, float w, float h)
{
    std::array<float, 13> vals = {static_cast<float>(to_underlying(PathCommand::MoveTo)), x,     y,     static_cast<float>(to_underlying(PathCommand::LineTo)), x,     y + h,
                                  static_cast<float>(to_underlying(PathCommand::LineTo)), x + w, y + h, static_cast<float>(to_underlying(PathCommand::LineTo)), x + w, y,
                                  static_cast<float>(to_underlying(PathCommand::Close))};
    mImpl->appendCommands(vals);
}

void Context::roundedRect(float x, float y, float w, float h, float r)
{
    roundedRectVarying(x, y, w, h, r, r, r, r);
}

void Context::roundedRect(float x, float y, float w, float h, float rx, float ry)
{
    roundedRectVarying(x, y, w, h, rx, ry, rx, ry, rx, ry, rx, ry);
}

void Context::roundedRectVarying(float x, float y, float w, float h, float radTopLeft, float radTopRight, float radBottomRight, float radBottomLeft)
{
    roundedRectVarying(x, y, w, h, radTopLeft, radTopLeft, radTopRight, radTopRight, radBottomRight, radBottomRight, radBottomLeft, radBottomLeft);
}

void Context::roundedRectVarying(float x, float y, float w, float h, float rxTopLeft, float ryTopLeft, float rxTopRight, float ryTopRight, float rxBottomRight, float ryBottomRight,
                                 float rxBottomLeft, float ryBottomLeft)
{
    // A corner is square unless both of its radii are at least 0.1 (as for circular corners).
    auto square = [](float rx, float ry) { return !(rx >= 0.1f) || !(ry >= 0.1f); };
    const bool sqTL = square(rxTopLeft, ryTopLeft), sqTR = square(rxTopRight, ryTopRight);
    const bool sqBR = square(rxBottomRight, ryBottomRight), sqBL = square(rxBottomLeft, ryBottomLeft);
    if (sqTL && sqTR && sqBR && sqBL) {
        rect(x, y, w, h);
        return;
    } else {
        float halfw = detail::absf(w) * 0.5f;
        float halfh = detail::absf(h) * 0.5f;
        auto cx = [&](bool sq, float r) { return sq ? 0.0f : detail::minf(r, halfw) * detail::signf(w); };
        auto cy = [&](bool sq, float r) { return sq ? 0.0f : detail::minf(r, halfh) * detail::signf(h); };
        float rxBL = cx(sqBL, rxBottomLeft), ryBL = cy(sqBL, ryBottomLeft);
        float rxBR = cx(sqBR, rxBottomRight), ryBR = cy(sqBR, ryBottomRight);
        float rxTR = cx(sqTR, rxTopRight), ryTR = cy(sqTR, ryTopRight);
        float rxTL = cx(sqTL, rxTopLeft), ryTL = cy(sqTL, ryTopLeft);
        std::array<float, 44> vals = {static_cast<float>(to_underlying(PathCommand::MoveTo)),
                                      x,
                                      y + ryTL,
                                      static_cast<float>(to_underlying(PathCommand::LineTo)),
                                      x,
                                      y + h - ryBL,
                                      static_cast<float>(to_underlying(PathCommand::BezierTo)),
                                      x,
                                      y + h - ryBL * (1 - NVG_KAPPA90),
                                      x + rxBL * (1 - NVG_KAPPA90),
                                      y + h,
                                      x + rxBL,
                                      y + h,
                                      static_cast<float>(to_underlying(PathCommand::LineTo)),
                                      x + w - rxBR,
                                      y + h,
                                      static_cast<float>(to_underlying(PathCommand::BezierTo)),
                                      x + w - rxBR * (1 - NVG_KAPPA90),
                                      y + h,
                                      x + w,
                                      y + h - ryBR * (1 - NVG_KAPPA90),
                                      x + w,
                                      y + h - ryBR,
                                      static_cast<float>(to_underlying(PathCommand::LineTo)),
                                      x + w,
                                      y + ryTR,
                                      static_cast<float>(to_underlying(PathCommand::BezierTo)),
                                      x + w,
                                      y + ryTR * (1 - NVG_KAPPA90),
                                      x + w - rxTR * (1 - NVG_KAPPA90),
                                      y,
                                      x + w - rxTR,
                                      y,
                                      static_cast<float>(to_underlying(PathCommand::LineTo)),
                                      x + rxTL,
                                      y,
                                      static_cast<float>(to_underlying(PathCommand::BezierTo)),
                                      x + rxTL * (1 - NVG_KAPPA90),
                                      y,
                                      x,
                                      y + ryTL * (1 - NVG_KAPPA90),
                                      x,
                                      y + ryTL,
                                      static_cast<float>(to_underlying(PathCommand::Close))};
        mImpl->appendCommands(vals);
    }
}

void Context::ellipse(float cx, float cy, float rx, float ry)
{
    std::array<float, 32> vals = {static_cast<float>(to_underlying(PathCommand::MoveTo)),
                                  cx - rx,
                                  cy,
                                  static_cast<float>(to_underlying(PathCommand::BezierTo)),
                                  cx - rx,
                                  cy + ry * NVG_KAPPA90,
                                  cx - rx * NVG_KAPPA90,
                                  cy + ry,
                                  cx,
                                  cy + ry,
                                  static_cast<float>(to_underlying(PathCommand::BezierContinue)),
                                  cx + rx * NVG_KAPPA90,
                                  cy + ry,
                                  cx + rx,
                                  cy + ry * NVG_KAPPA90,
                                  cx + rx,
                                  cy,
                                  static_cast<float>(to_underlying(PathCommand::BezierContinue)),
                                  cx + rx,
                                  cy - ry * NVG_KAPPA90,
                                  cx + rx * NVG_KAPPA90,
                                  cy - ry,
                                  cx,
                                  cy - ry,
                                  static_cast<float>(to_underlying(PathCommand::BezierContinue)),
                                  cx - rx * NVG_KAPPA90,
                                  cy - ry,
                                  cx - rx,
                                  cy - ry * NVG_KAPPA90,
                                  cx - rx,
                                  cy,
                                  static_cast<float>(to_underlying(PathCommand::Close))};
    mImpl->appendCommands(vals);
}

void Context::circle(float cx, float cy, float r)
{
    ellipse(cx, cy, r, r);
}

void Context::debugDumpPathCache() const
{
    const Path* path;
    int i, j;

    printf("Dumping %zu cached paths\n", (*this)->cache->paths.size());
    for (i = 0; i < static_cast<int>((*this)->cache->paths.size()); i++) {
        path = &(*this)->cache->paths[i];
        printf(" - Path %d\n", i);
        if (path->nfill) {
            printf("   - fill: %d\n", path->nfill);
            for (j = 0; j < path->nfill; j++) printf("%f\t%f\n", path->fill[j].x, path->fill[j].y);
        }
        if (path->nstroke) {
            printf("   - stroke: %d\n", path->nstroke);
            for (j = 0; j < path->nstroke; j++) printf("%f\t%f\n", path->stroke[j].x, path->stroke[j].y);
        }
    }
}

void Context::fill()
{
    State& state = mImpl->topState();
    const Path* path;
    Paint fillPaint = state.fill;
    int i;

    mImpl->flattenPaths();
    if (mImpl->params.edgeAntiAlias && state.shapeAntiAlias)
        mImpl->expandFill(mImpl->fringeWidth, static_cast<int>(LineCap::Miter), 2.4f);
    else
        mImpl->expandFill(0.0f, static_cast<int>(LineCap::Miter), 2.4f);

    // Apply global alpha
    fillPaint.innerColor.a *= state.alpha;
    fillPaint.outerColor.a *= state.alpha;

    mImpl->params.renderFill(mImpl->params.userPtr, &fillPaint, state.compositeOperation, &state.scissor, mImpl->fringeWidth, mImpl->cache->bounds.data(), mImpl->cache->paths.data(),
                             static_cast<int>(mImpl->cache->paths.size()));

    // Count triangles
    for (i = 0; i < static_cast<int>(mImpl->cache->paths.size()); i++) {
        path = &mImpl->cache->paths[i];
        mImpl->fillTriCount += path->nfill - 2;
        mImpl->fillTriCount += path->nstroke - 2;
        mImpl->drawCallCount += 2;
    }
}

void Context::stroke()
{
    strokePath(true);
}

void Context::strokePath(bool withMarkers)
{
    const State& state = mImpl->topState();
    // Copies: the passes below may not keep references into the state stack.
    const Paint stroke = state.stroke;
    const float blur = state.strokeBlur;
    if (state.strokeOuterShadowColor.a > 0.0f) {
        Paint shadow{};
        detail::setPaintColor(shadow, state.strokeOuterShadowColor);
        const float outline = state.strokeOutlineColor.a > 0.0f ? state.strokeOutlineWidth : 0.0f;
        strokePass(shadow, outline, state.strokeOuterShadowBlur, state.strokeOuterShadowX, state.strokeOuterShadowY, nullptr);
    }
    if (state.strokeOutlineWidth > 0.0f && state.strokeOutlineColor.a > 0.0f) {
        Paint outline{};
        detail::setPaintColor(outline, state.strokeOutlineColor);
        strokePass(outline, state.strokeOutlineWidth, blur, 0.0f, 0.0f, nullptr);
    }
    strokePass(stroke, 0.0f, blur, 0.0f, 0.0f, nullptr);
    const State& st = mImpl->topState();
    if (st.strokeInnerShadowColor.a > 0.0f && mImpl->params.renderStrokeEffect && mImpl->params.edgeAntiAlias && st.shapeAntiAlias) {
        // Offset and blur in framebuffer pixels: the shader measures the band with screen derivatives.
        const float* xf = st.xform.data();
        const float px = mImpl->devicePxRatio;
        const float scale = detail::getAverageScale(xf);
        const float shadow[4] = {(xf[0] * st.strokeInnerShadowX + xf[2] * st.strokeInnerShadowY) * px, (xf[1] * st.strokeInnerShadowX + xf[3] * st.strokeInnerShadowY) * px,
                                 st.strokeInnerShadowBlur * scale * px, 0.0f};
        Paint inner{};
        detail::setPaintColor(inner, st.strokeInnerShadowColor);
        strokePass(inner, 0.0f, blur, 0.0f, 0.0f, shadow);
    }
    if (withMarkers) drawMarkers();
}

void Context::strokePass(const Paint& paint, float extraWidth, float blur, float dx, float dy, const float* innerShadow)
{
    State& state = mImpl->topState();
    const float scale = detail::getAverageScale(state.xform.data());
    float strokeWidth = detail::clampf((state.strokeWidth + extraWidth * 2.0f) * scale, 0.0f, 1000.0f);
    Paint strokePaint = paint;
    const bool aa = mImpl->params.edgeAntiAlias && state.shapeAntiAlias;
    // A blurred stroke is drawn with a Gaussian measured from the true distance to the stroke (the same
    // sigma as the font atlas blur), through renderStrokeEffect; without it, as a wide linear
    // antialiasing ramp centered on the edge.
    const bool blurred = aa && blur > 0.0f && innerShadow == nullptr;
    const bool gaussian = blurred && mImpl->params.renderStrokeEffect != nullptr;
    const float sigma = std::max(blur * scale * 0.57735f, mImpl->fringeWidth * 0.4f);
    float fringe = mImpl->fringeWidth;
    if (gaussian)
        fringe = 2.0f * (std::min(3.0f * sigma, 300.0f) + mImpl->fringeWidth);  // geometry reaches 3 sigma past the edge
    else if (blurred)
        fringe = std::max(mImpl->fringeWidth, std::min(blur * scale * 2.0f, 200.0f));

    if (!gaussian && strokeWidth < mImpl->fringeWidth) {
        // If the stroke width is less than pixel size, use alpha to emulate coverage.
        // Since coverage is area, scale by alpha*alpha.
        float alpha = detail::clampf(strokeWidth / mImpl->fringeWidth, 0.0f, 1.0f);
        strokePaint.innerColor.a *= alpha * alpha;
        strokePaint.outerColor.a *= alpha * alpha;
        strokeWidth = mImpl->fringeWidth;
    }

    // Apply global alpha
    strokePaint.innerColor.a *= state.alpha;
    strokePaint.outerColor.a *= state.alpha;

    // An offset pass (shadows) strokes a moved copy of the path; the commands are in device space,
    // so move them by the offset run through the transform's linear part.
    std::vector<float> savedCommands;
    const bool moved = dx != 0.0f || dy != 0.0f;
    if (moved) {
        const float* xf = state.xform.data();
        const float ddx = xf[0] * dx + xf[2] * dy, ddy = xf[1] * dx + xf[3] * dy;
        savedCommands = mImpl->commands;
        std::vector<float>& cmds = mImpl->commands;
        size_t i = 0;
        while (i < cmds.size()) {
            const int cmd = static_cast<int>(cmds[i]);
            const size_t npts = cmd == static_cast<int>(PathCommand::MoveTo) || cmd == static_cast<int>(PathCommand::LineTo)                 ? 1
                                : cmd == static_cast<int>(PathCommand::BezierTo) || cmd == static_cast<int>(PathCommand::BezierContinue) ? 3
                                                                                                                                         : 0;
            if (i + 1 + npts * 2 > cmds.size()) break;
            for (size_t k = 0; k < npts; k++) {
                cmds[i + 1 + k * 2] += ddx;
                cmds[i + 2 + k * 2] += ddy;
            }
            i += cmd == static_cast<int>(PathCommand::Winding) ? 2 : 1 + npts * 2;
        }
        mImpl->clearPathCache();
    }

    mImpl->flattenPaths();

    // Dashing replaces the flattened outline in the cache with one open path per dash. Dash lengths
    // are in local units, so scale them to device space like the stroke width.
    const bool dashed = state.ndashes > 0;
    if (dashed) {
        std::array<float, maxStrokeDashes * 2> deviceDashes{};
        for (int k = 0; k < state.ndashes; k++) deviceDashes[static_cast<size_t>(k)] = state.dashes[static_cast<size_t>(k)] * scale;
        mImpl->dashPaths(deviceDashes.data(), state.ndashes, state.dashOffset * scale, state.lineCap);
    }

    // The effect passes rewrite or read the vertices' coordinates, so they use plain (solid) geometry.
    const int lineStyle = (innerShadow || gaussian) ? static_cast<int>(LineStyle::Solid) : state.lineStyle;
    if (aa)
        mImpl->expandStroke(strokeWidth * 0.5f, fringe, state.lineCap, state.lineJoin, lineStyle, state.miterLimit);
    else
        mImpl->expandStroke(strokeWidth * 0.5f, 0.0f, state.lineCap, state.lineJoin, lineStyle, state.miterLimit);

    const Params& params = mImpl->params;
    Path* paths = mImpl->cache->paths.data();
    const int npaths = static_cast<int>(mImpl->cache->paths.size());
    if (gaussian) {
        mImpl->strokeDistanceCoords(state.lineCap, strokeWidth * 0.5f);
        const float effect[4] = {strokeWidth * 0.5f, sigma, 0.0f, state.lineCap == static_cast<int>(LineCap::Round) ? -2.0f : -1.0f};
        // The regular fringe: the backend also derives the scissor's antialiasing from it.
        params.renderStrokeEffect(params.userPtr, &strokePaint, state.compositeOperation, &state.scissor, mImpl->fringeWidth, strokeWidth, effect, paths, npaths);
    } else if (innerShadow) {
        const float effect[4] = {innerShadow[0], innerShadow[1], innerShadow[2], (strokeWidth * 0.5f + fringe * 0.5f) * mImpl->devicePxRatio};
        params.renderStrokeEffect(params.userPtr, &strokePaint, state.compositeOperation, &state.scissor, fringe, strokeWidth, effect, paths, npaths);
    } else {
        params.renderStroke(params.userPtr, &strokePaint, state.compositeOperation, &state.scissor, fringe, strokeWidth, lineStyle, paths, npaths);
    }

    // Count triangles
    for (const Path& path : mImpl->cache->paths) {
        mImpl->strokeTriCount += path.nstroke - 2;
        mImpl->drawCallCount++;
    }
    // The cache now holds dashes (or a moved copy) rather than the path's outline; make a later fill()
    // or stroke() of the same path flatten it again.
    if (moved) {
        mImpl->commands = std::move(savedCommands);
        mImpl->clearPathCache();
    } else if (dashed) {
        mImpl->clearPathCache();
    }
}

namespace
{
// A path vertex for markers, in device space, with the directions of the segments meeting there.
struct MarkerVertex {
    float x, y;
    float inx = 0.0f, iny = 0.0f, outx = 0.0f, outy = 0.0f;
    bool hasIn = false, hasOut = false;
};

// Collects the marker vertices of a command list (SVG path vertex rules, zero-length segments skipped).
std::vector<MarkerVertex> markerVertices(const std::vector<float>& cmds, float tol)
{
    std::vector<MarkerVertex> vs;
    size_t subStart = SIZE_MAX;   // vertex that started the current subpath
    bool lastFromSegment = false;  // the last vertex ends a segment that a BezierContinue may extend
    float curx = 0.0f, cury = 0.0f;
    auto longer = [tol](float dx, float dy) { return dx * dx + dy * dy > tol * tol; };
    auto moveTo = [&](float x, float y) {
        vs.push_back(MarkerVertex{x, y});
        subStart = vs.size() - 1;
        lastFromSegment = false;
        curx = x;
        cury = y;
    };
    auto segment = [&](float sdx, float sdy, float edx, float edy, float x, float y, bool cont) {
        if (vs.empty()) moveTo(curx, cury);
        if (cont && lastFromSegment) {
            MarkerVertex& v = vs.back();
            v.x = x;
            v.y = y;
            v.inx = edx;
            v.iny = edy;
        } else {
            MarkerVertex& prev = vs.back();
            if (!prev.hasOut) {
                prev.outx = sdx;
                prev.outy = sdy;
                prev.hasOut = true;
            }
            MarkerVertex v{x, y};
            v.inx = edx;
            v.iny = edy;
            v.hasIn = true;
            vs.push_back(v);
            lastFromSegment = true;
        }
        curx = x;
        cury = y;
    };

    size_t i = 0;
    while (i < cmds.size()) {
        const int cmd = static_cast<int>(cmds[i]);
        switch (cmd) {
            case static_cast<int>(PathCommand::MoveTo):
                if (i + 3 > cmds.size()) return vs;
                moveTo(cmds[i + 1], cmds[i + 2]);
                i += 3;
                break;
            case static_cast<int>(PathCommand::LineTo): {
                if (i + 3 > cmds.size()) return vs;
                const float x = cmds[i + 1], y = cmds[i + 2];
                const float dx = x - curx, dy = y - cury;
                if (longer(dx, dy)) segment(dx, dy, dx, dy, x, y, false);
                i += 3;
                break;
            }
            case static_cast<int>(PathCommand::BezierTo):
            case static_cast<int>(PathCommand::BezierContinue): {
                if (i + 7 > cmds.size()) return vs;
                const float c1x = cmds[i + 1], c1y = cmds[i + 2], c2x = cmds[i + 3], c2y = cmds[i + 4], x = cmds[i + 5], y = cmds[i + 6];
                // Tangents at the ends: towards the nearest distinct control point.
                const float s[3][2] = {{c1x - curx, c1y - cury}, {c2x - curx, c2y - cury}, {x - curx, y - cury}};
                const float e[3][2] = {{x - c2x, y - c2y}, {x - c1x, y - c1y}, {x - curx, y - cury}};
                int si = 0, ei = 0;
                while (si < 3 && !longer(s[si][0], s[si][1])) si++;
                while (ei < 3 && !longer(e[ei][0], e[ei][1])) ei++;
                if (si < 3 && ei < 3) segment(s[si][0], s[si][1], e[ei][0], e[ei][1], x, y, cmd == static_cast<int>(PathCommand::BezierContinue));
                i += 7;
                break;
            }
            case static_cast<int>(PathCommand::Close):
                if (subStart != SIZE_MAX) {
                    const float sx = vs[subStart].x, sy = vs[subStart].y;
                    const float dx = sx - curx, dy = sy - cury;
                    if (longer(dx, dy)) segment(dx, dy, dx, dy, sx, sy, false);
                    // A closed subpath joins up: its start vertex comes in along the closing segment,
                    // and its closing vertex leaves along the first segment.
                    if (vs.size() - 1 != subStart) {
                        MarkerVertex& first = vs[subStart];
                        MarkerVertex& last = vs.back();
                        if (first.hasOut) {
                            last.outx = first.outx;
                            last.outy = first.outy;
                            last.hasOut = true;
                        }
                        if (last.hasIn) {
                            first.inx = last.inx;
                            first.iny = last.iny;
                            first.hasIn = true;
                        }
                    }
                    curx = sx;
                    cury = sy;
                }
                lastFromSegment = false;
                i++;
                break;
            case static_cast<int>(PathCommand::Winding):
                i += 2;
                break;
            default:
                i++;
        }
    }
    return vs;
}

// SVG orient="auto": the bisector of the incoming and outgoing directions.
float markerAngle(const MarkerVertex& v)
{
    if (v.hasIn && v.hasOut) {
        const float a1 = atan2f(v.iny, v.inx), a2 = atan2f(v.outy, v.outx);
        float d = a2 - a1;
        const float pi = (float)M_PI;
        while (d > pi) d -= 2.0f * pi;
        while (d <= -pi) d += 2.0f * pi;
        return a1 + d * 0.5f;
    }
    if (v.hasIn) return atan2f(v.iny, v.inx);
    if (v.hasOut) return atan2f(v.outy, v.outx);
    return 0.0f;
}
}  // namespace

void Context::drawMarkers()
{
    const State& state = mImpl->topState();
    auto active = [](const Marker& m) { return m.shape != MarkerShape::None && m.size > 0.0f && (m.shape != MarkerShape::Custom || m.draw); };
    if (!active(state.markers[0]) && !active(state.markers[1]) && !active(state.markers[2])) return;
    const float strokeWidth = state.strokeWidth * detail::getAverageScale(state.xform.data());
    if (!(strokeWidth > 0.0f)) return;

    const std::vector<MarkerVertex> verts = markerVertices(mImpl->commands, mImpl->distTol);
    if (verts.size() < 2) return;
    const std::array<Marker, 3> markers = state.markers;
    const float xformAngle = atan2f(state.xform[1], state.xform[0]);

    // Marker drawing builds its own paths; keep the caller's so it can still fill() it afterwards.
    std::vector<float> commands = mImpl->commands;
    const float commandx = mImpl->commandx, commandy = mImpl->commandy;

    save();
    {
        State& st = mImpl->topState();
        st.markers = {};
        st.ndashes = 0;
        st.lineStyle = static_cast<int>(LineStyle::Solid);
        // The stroke's effects (shadows, outline, blur) are not applied to its markers.
        st.strokeOuterShadowColor = st.strokeInnerShadowColor = st.strokeOutlineColor = Color{};
        st.strokeBlur = 0.0f;
        // Both paints are already in device space, so the marker frame does not move them.
        st.fill = st.stroke;
    }
    for (size_t i = 0; i < verts.size(); i++) {
        const size_t role = i == 0 ? 0 : i + 1 == verts.size() ? 2 : 1;
        const Marker& m = markers[role];
        if (!active(m)) continue;
        float a = 0.0f;
        if (m.orient == MarkerOrient::Fixed) {
            a = m.angle + xformAngle;
        } else {
            a = markerAngle(verts[i]);
            if (role == 0 && m.orient == MarkerOrient::AutoStartReverse) a += (float)M_PI;
        }
        const float unit = m.size * strokeWidth;

        save();
        resetTransform();
        translate(verts[i].x, verts[i].y);
        rotate(a);
        scale(unit, unit);
        mImpl->topState().strokeWidth = 1.0f / m.size;
        beginPath();
        switch (m.shape) {
            case MarkerShape::Arrow:
                moveTo(0.5f, 0.0f);
                lineTo(-0.5f, 0.5f);
                lineTo(-0.5f, -0.5f);
                closePath();
                fill();
                break;
            case MarkerShape::Circle:
                circle(0.0f, 0.0f, 0.5f);
                fill();
                break;
            case MarkerShape::Square:
                rect(-0.5f, -0.5f, 1.0f, 1.0f);
                fill();
                break;
            case MarkerShape::Diamond:
                moveTo(0.5f, 0.0f);
                lineTo(0.0f, 0.5f);
                lineTo(-0.5f, 0.0f);
                lineTo(0.0f, -0.5f);
                closePath();
                fill();
                break;
            case MarkerShape::Bar: {
                const float t = 0.5f / m.size;
                rect(-t, -0.5f, 2.0f * t, 1.0f);
                fill();
                break;
            }
            case MarkerShape::Custom:
                m.draw(*this);
                break;
            case MarkerShape::None:
                break;
        }
        restore();
    }
    restore();

    mImpl->commands = std::move(commands);
    mImpl->commandx = commandx;
    mImpl->commandy = commandy;
    mImpl->clearPathCache();
}

// Skia-style immediate draw calls
namespace
{
void renderWithStyle(Context& ctx, PaintStyle style)
{
    if (style != PaintStyle::Stroke) ctx.fill();
    if (style != PaintStyle::Fill) ctx.stroke();
}
}  // namespace

// Adds a device-space path covering everything that can currently be drawn: the scissor quad if a
// scissor is set, otherwise the whole viewport. Must be called with an identity transform.
// Covering only the scissor matters for blend modes like Copy: the scissor is applied as a coverage
// mask in the shader, so a viewport-sized Copy fill would wipe the pixels outside the scissor.
static void addVisibleAreaPath(Context& ctx, const Scissor& sc, float viewWidth, float viewHeight)
{
    if (sc.extent[0] < -0.5f || sc.extent[1] < -0.5f) {
        ctx.rect(0.0f, 0.0f, viewWidth, viewHeight);
        return;
    }
    const float ex = sc.extent[0], ey = sc.extent[1];
    const float corners[4][2] = {{-ex, -ey}, {-ex, ey}, {ex, ey}, {ex, -ey}};
    for (int i = 0; i < 4; i++) {
        float x = 0.0f, y = 0.0f;
        transformPoint(x, y, sc.xform, corners[i][0], corners[i][1]);
        if (i == 0)
            ctx.moveTo(x, y);
        else
            ctx.lineTo(x, y);
    }
    ctx.closePath();
}

void Context::drawPaint(const Paint& paint)
{
    save();
    // Bind the paint in the current local space first, then cover the visible area in device space.
    fillPaint(paint);
    resetTransform();
    beginPath();
    addVisibleAreaPath(*this, mImpl->topState().scissor, mImpl->viewWidth, mImpl->viewHeight);
    fill();
    restore();
}

void Context::drawColor(const Color& color)
{
    save();
    fillColor(color);
    resetTransform();
    beginPath();
    addVisibleAreaPath(*this, mImpl->topState().scissor, mImpl->viewWidth, mImpl->viewHeight);
    fill();
    restore();
}

void Context::clear(const Color& color)
{
    save();
    globalAlpha(1.0f);
    globalCompositeOperation(static_cast<int>(CompositeOperation::Copy));
    // With Copy, antialiased edges would write partially transparent pixels.
    shapeAntiAlias(0);
    drawColor(color);
    restore();
}

void Context::drawLine(float x0, float y0, float x1, float y1)
{
    beginPath();
    moveTo(x0, y0);
    lineTo(x1, y1);
    stroke();
}

bool Context::renderShapePass(ShapeType type, const float* params, const float* frame, const float* bounds, bool stroke)
{
    if (mImpl->params.renderShape == nullptr) return false;
    const State& state = mImpl->topState();

    // Shape space -> device space: the shape's frame first, then the current transform.
    std::array<float, 6> m{};
    std::memcpy(m.data(), frame, sizeof(float) * 6);
    transformMultiply(m.data(), state.xform.data());
    const float scale = detail::getAverageScale(m.data());
    if (!(scale > 0.0f)) return false;

    Shape shape{};
    shape.type = static_cast<int>(type);
    const bool aa = mImpl->params.edgeAntiAlias && state.shapeAntiAlias;
    // Without antialiasing use a ramp far below a pixel, which the shader turns into a hard edge.
    shape.aaWidth = (aa ? mImpl->fringeWidth : mImpl->fringeWidth * 1e-3f) / scale;
    std::memcpy(shape.xform, m.data(), sizeof(shape.xform));
    std::memcpy(shape.params, params, sizeof(shape.params));

    Paint paint = stroke ? state.stroke : state.fill;
    if (stroke) {
        // Only plain strokes wider than a pixel match the shader's outline band exactly; dashes, the
        // lineStyle effects and the thin-stroke alpha emulation need the tessellated path.
        if (state.ndashes > 0 || state.lineStyle != static_cast<int>(LineStyle::Solid)) return false;
        const float w = detail::clampf(state.strokeWidth * scale, 0.0f, 1000.0f);
        if (w < mImpl->fringeWidth) return false;
        shape.strokeHalfWidth = w * 0.5f / scale;
    }
    paint.innerColor.a *= state.alpha;
    paint.outerColor.a *= state.alpha;

    // Bounding quad of the shape, grown by the stroke band and the antialiasing ramp.
    const float grow = shape.strokeHalfWidth + shape.aaWidth * 2.0f;
    const float corners[4][2] = {{bounds[0] - grow, bounds[1] - grow}, {bounds[2] + grow, bounds[1] - grow}, {bounds[2] + grow, bounds[3] + grow}, {bounds[0] - grow, bounds[3] + grow}};
    std::array<Vertex, 6> verts{};
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) {
        float x = 0.0f, y = 0.0f;
        transformPoint(x, y, m.data(), corners[order[i]][0], corners[order[i]][1]);
        // u = 0.5, v = 1 is the fully covered interior of nanovg's antialiasing fringe.
        verts[static_cast<size_t>(i)] = Vertex{x, y, 0.5f, 1.0f, 0.0f, 0.0f};
    }
    // The GL backend culls back faces; give both triangles the winding it draws (see drawVertices),
    // whatever orientation the transform left them in.
    if (detail::cross(verts[1].x - verts[0].x, verts[1].y - verts[0].y, verts[2].x - verts[0].x, verts[2].y - verts[0].y) < 0.0f) {
        std::swap(verts[1], verts[2]);
        std::swap(verts[4], verts[5]);
    }

    Scissor scissor = state.scissor;
    mImpl->params.renderShape(mImpl->params.userPtr, &paint, state.compositeOperation, &scissor, mImpl->fringeWidth, &shape, verts.data(), static_cast<int>(verts.size()));
    mImpl->fillTriCount += 2;
    mImpl->drawCallCount++;
    return true;
}

void Context::drawArrow(float x0, float y0, float x1, float y1, float stalkWidth, float headWidth, float headLength, PaintStyle style)
{
    beginPath();
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = sqrtf(dx * dx + dy * dy);
    if (len <= 1e-6f) return;
    // Unit direction along the arrow and its left-hand normal.
    const float ux = dx / len, uy = dy / len;
    const float nx = -uy, ny = ux;

    const float sw = std::max(stalkWidth, 0.0f) * 0.5f;
    const float hw = std::max(headWidth * 0.5f, sw);
    const float hl = std::min(std::max(headLength, 0.0f), len);

    // Shader path: one quad in the arrow's own frame (tail at the origin, +x towards the tip).
    // Its outline band has round corners, so strokes only take it with round joins.
    const float frame[6] = {ux, uy, -uy, ux, x0, y0};
    const float params[8] = {len, sw, hw, hl, 0.0f, 0.0f, 0.0f, 0.0f};
    const float bounds[4] = {0.0f, -hw, len, hw};
    const bool strokeOk = mImpl->topState().lineJoin == static_cast<int>(LineCap::Round);

    bool built = false;
    auto build = [&] {
        if (built) return;
        built = true;
        // Base of the head, where the stalk ends.
        const float bx = x1 - ux * hl, by = y1 - uy * hl;
        moveTo(x0 + nx * sw, y0 + ny * sw);
        lineTo(bx + nx * sw, by + ny * sw);
        lineTo(bx + nx * hw, by + ny * hw);
        lineTo(x1, y1);
        lineTo(bx - nx * hw, by - ny * hw);
        lineTo(bx - nx * sw, by - ny * sw);
        lineTo(x0 - nx * sw, y0 - ny * sw);
        closePath();
    };
    if (style != PaintStyle::Stroke && !renderShapePass(ShapeType::Arrow, params, frame, bounds, false)) {
        build();
        fill();
    }
    if (style != PaintStyle::Fill && !(strokeOk && renderShapePass(ShapeType::Arrow, params, frame, bounds, true))) {
        build();
        strokePath(false);
    }
}

void Context::drawGrid(const Rect& r, int cols, int rows, GridShape shape, float size, PaintStyle style)
{
    beginPath();
    if (cols <= 0 || rows <= 0 || size <= 0.0f || r.w <= 0.0f || r.h <= 0.0f) return;
    const float cw = r.w / cols, ch = r.h / rows;
    const State& state = mImpl->topState();

    // Shader path: one quad covering the grid; the shader evaluates the nearest line / element.
    // Lines are the union of the bars either way, so both paths fill and outline the same shape.
    const float identity[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    const float params[8] = {r.x, r.y, r.w, r.h, static_cast<float>(cols), static_cast<float>(rows), size, 0.0f};
    const float pad = shape == GridShape::Lines ? size * 0.5f : 0.0f;
    const float bounds[4] = {r.x - pad, r.y - pad, r.x + r.w + pad, r.y + r.h + pad};
    const ShapeType type = shape == GridShape::Lines ? ShapeType::GridLines : shape == GridShape::Circles ? ShapeType::GridCircles : ShapeType::GridSquares;
    // Circles and squares are evaluated against their own cell only, so they must not reach into a
    // neighbour (strokes and the antialiasing ramp included). Square outlines have sharp corners,
    // which is what a miter join gives a right angle.
    const float scale = detail::getAverageScale(state.xform.data());
    const float aaLocal = scale > 0.0f ? mImpl->fringeWidth / scale : 0.0f;
    const float halfCell = 0.5f * std::min(cw, ch);
    auto fits = [&](float strokeHalf) { return shape == GridShape::Lines || size + strokeHalf + aaLocal <= halfCell; };
    const bool sharpJoins = state.lineJoin == static_cast<int>(LineCap::Miter) && state.miterLimit >= 1.42f;
    const bool strokeOk = fits(state.strokeWidth * 0.5f) && (shape == GridShape::Circles || sharpJoins);

    bool built = false;
    auto build = [&] {
        if (built) return;
        built = true;
        switch (shape) {
            case GridShape::Lines: {
                // The lines' union is the outer border rect with one hole per cell, so fill and stroke
                // both follow its true outline (crossings neither double-blend nor show seams).
                const float h = size * 0.5f;
                rect(r.x - h, r.y - h, r.w + size, r.h + size);
                if (cw > size && ch > size) {
                    for (int j = 0; j < rows; j++) {
                        for (int i = 0; i < cols; i++) {
                            rect(r.x + i * cw + h, r.y + j * ch + h, cw - size, ch - size);
                            pathWinding(static_cast<int>(Solidity::Hole));
                        }
                    }
                }
                break;
            }
            case GridShape::Circles:
                for (int j = 0; j < rows; j++)
                    for (int i = 0; i < cols; i++) circle(r.x + (i + 0.5f) * cw, r.y + (j + 0.5f) * ch, size);
                break;
            case GridShape::Squares:
                for (int j = 0; j < rows; j++)
                    for (int i = 0; i < cols; i++) rect(r.x + (i + 0.5f) * cw - size, r.y + (j + 0.5f) * ch - size, size * 2.0f, size * 2.0f);
                break;
        }
    };
    if (style != PaintStyle::Stroke && !(fits(0.0f) && renderShapePass(type, params, identity, bounds, false))) {
        build();
        fill();
    }
    if (style != PaintStyle::Fill && !(strokeOk && renderShapePass(type, params, identity, bounds, true))) {
        build();
        strokePath(false);
    }
}

void Context::drawPoints(PointMode mode, const float* pts, int count)
{
    if (pts == nullptr || count <= 0) return;

    switch (mode) {
        case PointMode::Points: {
            State& state = mImpl->topState();
            const float hw = state.strokeWidth * 0.5f;
            const bool round = state.lineCap == static_cast<int>(LineCap::Round);
            save();
            // Dots are filled with the stroke paint, matching Skia's use of the paint's stroke settings.
            mImpl->topState().fill = mImpl->topState().stroke;
            beginPath();
            for (int i = 0; i < count; i++) {
                const float x = pts[i * 2];
                const float y = pts[i * 2 + 1];
                if (round)
                    circle(x, y, hw);
                else
                    rect(x - hw, y - hw, hw * 2.0f, hw * 2.0f);
            }
            fill();
            restore();
            break;
        }
        case PointMode::Lines:
            beginPath();
            for (int i = 0; i + 1 < count; i += 2) {
                moveTo(pts[i * 2], pts[i * 2 + 1]);
                lineTo(pts[i * 2 + 2], pts[i * 2 + 3]);
            }
            stroke();
            break;
        case PointMode::Polygon:
            if (count < 2) return;
            beginPath();
            moveTo(pts[0], pts[1]);
            for (int i = 1; i < count; i++) lineTo(pts[i * 2], pts[i * 2 + 1]);
            stroke();
            break;
    }
}

void Context::drawPoint(float x, float y)
{
    const float pt[2] = {x, y};
    drawPoints(PointMode::Points, pt, 1);
}

void Context::drawPolygon(const float* pts, int count, PaintStyle style)
{
    if (pts == nullptr || count < 2) return;
    beginPath();
    moveTo(pts[0], pts[1]);
    for (int i = 1; i < count; i++) lineTo(pts[i * 2], pts[i * 2 + 1]);
    closePath();
    renderWithStyle(*this, style);
}

void Context::drawRect(const Rect& r, PaintStyle style)
{
    beginPath();
    rect(r.x, r.y, r.w, r.h);
    renderWithStyle(*this, style);
}

void Context::drawRoundRect(const Rect& r, float radius, PaintStyle style)
{
    beginPath();
    roundedRect(r.x, r.y, r.w, r.h, radius);
    renderWithStyle(*this, style);
}

void Context::drawRoundRect(const Rect& r, float rx, float ry, PaintStyle style)
{
    beginPath();
    roundedRect(r.x, r.y, r.w, r.h, rx, ry);
    renderWithStyle(*this, style);
}

void Context::drawRoundRectVarying(const Rect& r, float radTopLeft, float radTopRight, float radBottomRight, float radBottomLeft, PaintStyle style)
{
    beginPath();
    roundedRectVarying(r.x, r.y, r.w, r.h, radTopLeft, radTopRight, radBottomRight, radBottomLeft);
    renderWithStyle(*this, style);
}

void Context::drawDRRect(const Rect& outer, float outerRadius, const Rect& inner, float innerRadius)
{
    beginPath();
    roundedRect(outer.x, outer.y, outer.w, outer.h, outerRadius);
    roundedRect(inner.x, inner.y, inner.w, inner.h, innerRadius);
    pathWinding(static_cast<int>(Solidity::Hole));
    fill();
}

void Context::drawCircle(float cx, float cy, float r, PaintStyle style)
{
    beginPath();
    circle(cx, cy, r);
    renderWithStyle(*this, style);
}

void Context::drawOval(const Rect& oval, PaintStyle style)
{
    beginPath();
    ellipse(oval.x + oval.w * 0.5f, oval.y + oval.h * 0.5f, detail::absf(oval.w) * 0.5f, detail::absf(oval.h) * 0.5f);
    renderWithStyle(*this, style);
}

void Context::drawArc(float cx, float cy, float r, float startAngle, float sweepAngle, bool useCenter, PaintStyle style)
{
    if (sweepAngle == 0.0f || r <= 0.0f) return;
    const int dir = sweepAngle > 0.0f ? static_cast<int>(Winding::CW) : static_cast<int>(Winding::CCW);
    const float endAngle = startAngle + sweepAngle;

    auto buildPath = [&](bool closed) {
        beginPath();
        if (useCenter) moveTo(cx, cy);
        arc(cx, cy, r, startAngle, endAngle, dir);
        if (closed) closePath();
    };

    if (style != PaintStyle::Stroke) {
        buildPath(true);
        fill();
    }
    if (style != PaintStyle::Fill) {
        // A wedge is stroked as a closed outline; a bare arc is stroked open, as in Skia.
        buildPath(useCenter);
        stroke();
    }
}

void Context::drawImage(int image, float x, float y, float alpha)
{
    int w = 0, h = 0;
    imageSize(image, w, h);
    if (w <= 0 || h <= 0) return;
    const Rect r{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
    drawImageRect(image, r, Rect{x, y, r.w, r.h}, alpha);
}

void Context::drawImageRect(int image, const Rect& src, const Rect& dst, float alpha)
{
    if (src.w == 0.0f || src.h == 0.0f || dst.w == 0.0f || dst.h == 0.0f) return;
    int iw = 0, ih = 0;
    imageSize(image, iw, ih);
    if (iw <= 0 || ih <= 0) return;

    const float sx = dst.w / src.w;
    const float sy = dst.h / src.h;
    const Paint pattern = imagePattern(dst.x - src.x * sx, dst.y - src.y * sy, static_cast<float>(iw) * sx, static_cast<float>(ih) * sy, 0.0f, image, alpha);

    save();
    fillPaint(pattern);
    beginPath();
    rect(dst.x, dst.y, dst.w, dst.h);
    fill();
    restore();
}

void Context::drawImageRect(int image, const Rect& dst, float alpha)
{
    int iw = 0, ih = 0;
    imageSize(image, iw, ih);
    drawImageRect(image, Rect{0.0f, 0.0f, static_cast<float>(iw), static_cast<float>(ih)}, dst, alpha);
}

void Context::drawImageNine(int image, const Rect& center, const Rect& dst, float alpha)
{
    int iwi = 0, ihi = 0;
    imageSize(image, iwi, ihi);
    if (iwi <= 0 || ihi <= 0 || dst.w <= 0.0f || dst.h <= 0.0f) return;
    const float iw = static_cast<float>(iwi);
    const float ih = static_cast<float>(ihi);

    // Clamp the center rect to the image.
    const float cx0 = detail::clampf(center.x, 0.0f, iw);
    const float cy0 = detail::clampf(center.y, 0.0f, ih);
    const float cx1 = detail::clampf(center.x + center.w, cx0, iw);
    const float cy1 = detail::clampf(center.y + center.h, cy0, ih);

    // Fixed border sizes; shrink proportionally when the destination is too small for them.
    float left = cx0, right = iw - cx1, top = cy0, bottom = ih - cy1;
    if (left + right > dst.w) {
        const float s = dst.w / (left + right);
        left *= s;
        right *= s;
    }
    if (top + bottom > dst.h) {
        const float s = dst.h / (top + bottom);
        top *= s;
        bottom *= s;
    }

    const std::array<float, 4> srcX = {0.0f, cx0, cx1, iw};
    const std::array<float, 4> srcY = {0.0f, cy0, cy1, ih};
    const std::array<float, 4> dstX = {dst.x, dst.x + left, dst.x + dst.w - right, dst.x + dst.w};
    const std::array<float, 4> dstY = {dst.y, dst.y + top, dst.y + dst.h - bottom, dst.y + dst.h};

    save();
    // Adjacent patches share edges; antialiasing them would leave visible seams.
    shapeAntiAlias(0);
    for (size_t j = 0; j < 3; j++) {
        for (size_t i = 0; i < 3; i++) {
            const Rect s{srcX[i], srcY[j], srcX[i + 1] - srcX[i], srcY[j + 1] - srcY[j]};
            const Rect d{dstX[i], dstY[j], dstX[i + 1] - dstX[i], dstY[j + 1] - dstY[j]};
            if (s.w <= 0.0f || s.h <= 0.0f || d.w <= 0.0f || d.h <= 0.0f) continue;
            drawImageRect(image, s, d, alpha);
        }
    }
    restore();
}

void Context::drawVertices(const Vertex* verts, int nverts, int image, float alpha)
{
    if (verts == nullptr || nverts < 3 || image == 0) return;
    State& state = mImpl->topState();
    nverts -= nverts % 3;

    std::vector<Vertex> xformed(verts, verts + nverts);
    for (Vertex& v : xformed) {
        transformPoint(v.x, v.y, state.xform.data(), v.x, v.y);
        v.s = 0.0f;
        v.t = 0.0f;
    }
    // The GL backend culls back faces; normalize each triangle to the winding the backend draws
    // (same as text quads) so callers can supply either orientation, as with SkCanvas::drawVertices.
    for (size_t i = 0; i + 2 < xformed.size(); i += 3) {
        const Vertex& a = xformed[i];
        const Vertex& b = xformed[i + 1];
        const Vertex& c = xformed[i + 2];
        // detail::cross returns dx1*dy0 - dx0*dy1, so a negative value means (a, b, c) is clockwise in y-down space.
        if (detail::cross(b.x - a.x, b.y - a.y, c.x - a.x, c.y - a.y) < 0.0f) std::swap(xformed[i + 1], xformed[i + 2]);
    }

    Paint paint{};
    transformIdentity(paint.xform);
    paint.innerColor = paint.outerColor = rgbaf(1.0f, 1.0f, 1.0f, alpha * state.alpha);
    paint.image = image;

    mImpl->params.renderTriangles(mImpl->params.userPtr, &paint, state.compositeOperation, &state.scissor, xformed.data(), nverts, mImpl->fringeWidth);
    mImpl->drawCallCount++;
    mImpl->fillTriCount += static_cast<size_t>(nverts / 3);
}

// Add fonts
int Context::createFont(const char* name, const char* filename)
{
    return fonsAddFont(mImpl->fs.get(), name, filename, 0);
}

int Context::createFontAtIndex(const char* name, const char* filename, const int fontIndex)
{
    return fonsAddFont(mImpl->fs.get(), name, filename, fontIndex);
}

int Context::createFontMem(const char* name, unsigned char* data, int ndata, int freeData)
{
    return fonsAddFontMem(mImpl->fs.get(), name, data, ndata, freeData, 0);
}

int Context::createFontMemAtIndex(const char* name, unsigned char* data, int ndata, int freeData, const int fontIndex)
{
    return fonsAddFontMem(mImpl->fs.get(), name, data, ndata, freeData, fontIndex);
}

int Context::findFont(const char* name) const
{
    if (name == NULL) return -1;
    return fonsGetFontByName((*this)->fs.get(), name);
}

int Context::addFallbackFontId(int baseFont, int fallbackFont)
{
    if (baseFont == -1 || fallbackFont == -1) return 0;
    return fonsAddFallbackFont(mImpl->fs.get(), baseFont, fallbackFont);
}

int Context::addFallbackFont(const char* baseFont, const char* fallbackFont)
{
    return addFallbackFontId(findFont(baseFont), findFont(fallbackFont));
}

void Context::resetFallbackFontsId(int baseFont)
{
    fonsResetFallbackFont(mImpl->fs.get(), baseFont);
}

void Context::resetFallbackFonts(const char* baseFont)
{
    resetFallbackFontsId(findFont(baseFont));
}

// State setting
void Context::fontSize(float size)
{
    State& state = mImpl->topState();
    state.fontSize = size;
}

void Context::fontBlur(float blur)
{
    State& state = mImpl->topState();
    state.fontBlur = blur;
}

void Context::textOuterShadow(float offsetX, float offsetY, float blur, const Color& color)
{
    State& state = mImpl->topState();
    state.textOuterShadowX = std::isfinite(offsetX) ? offsetX : 0.0f;
    state.textOuterShadowY = std::isfinite(offsetY) ? offsetY : 0.0f;
    state.textOuterShadowBlur = std::isfinite(blur) ? std::max(blur, 0.0f) : 0.0f;
    state.textOuterShadowColor = color;
}

void Context::textInnerShadow(float offsetX, float offsetY, float blur, const Color& color)
{
    State& state = mImpl->topState();
    state.textInnerShadowX = std::isfinite(offsetX) ? offsetX : 0.0f;
    state.textInnerShadowY = std::isfinite(offsetY) ? offsetY : 0.0f;
    state.textInnerShadowBlur = std::isfinite(blur) ? std::max(blur, 0.0f) : 0.0f;
    state.textInnerShadowColor = color;
}

namespace
{
// fontstash's glyph blur (a recursive exponential filter run twice along each axis, with zero borders),
// on floats and with a fractional radius, so inner shadows match outer ones and animate smoothly.
void blurMask(float* p, int w, int h, float blur)
{
    if (!(blur > 0.0f) || w < 2 || h < 2) return;
    const float sigma = blur * 0.57735f;
    const float a = 1.0f - expf(-2.3f / (sigma + 1.0f));
    auto line = [a](float* d, int n, int stride) {
        float z = 0.0f;
        for (int i = 1; i < n; i++) d[i * stride] = z += a * (d[i * stride] - z);
        d[(n - 1) * stride] = 0.0f;
        z = 0.0f;
        for (int i = n - 2; i >= 0; i--) d[i * stride] = z += a * (d[i * stride] - z);
        d[0] = 0.0f;
    };
    for (int pass = 0; pass < 2; pass++) {
        for (int x = 0; x < w; x++) line(p + x, h, w);
        for (int y = 0; y < h; y++) line(p + (size_t)y * w, w, 1);
    }
}

// Bilinear sample of a w x h mask at a position in pixel units (texel centers at i + 0.5), 0 outside.
float sampleMask(const float* m, int w, int h, float x, float y)
{
    x -= 0.5f;
    y -= 0.5f;
    const float fx = floorf(x), fy = floorf(y);
    const int ix = (int)fx, iy = (int)fy;
    const float tx = x - fx, ty = y - fy;
    auto at = [&](int cx, int cy) { return cx < 0 || cy < 0 || cx >= w || cy >= h ? 0.0f : m[(size_t)cy * w + cx]; };
    return (at(ix, iy) * (1 - tx) + at(ix + 1, iy) * tx) * (1 - ty) + (at(ix, iy + 1) * (1 - tx) + at(ix + 1, iy + 1) * tx) * ty;
}
}  // namespace

void Context::textInnerShadowPass(float x, float y, const char* string, const char* end)
{
    ContextImpl& im = *mImpl;
    State& state = im.topState();
    FONScontext* fs = im.fs.get();
    // Glyph quads come in "font pixels": device pixels when the transform is a plain scale.
    const float scale = detail::getFontScale(&state) * im.devicePxRatio;
    const float invscale = 1.0f / scale;
    const float blur = detail::clampf(state.textInnerShadowBlur * scale, 0.0f, 64.0f);
    const float ox = state.textInnerShadowX * scale, oy = state.textInnerShadowY * scale;

    // 1. Collect the glyphs as drawn: their quads and a copy of their atlas bitmaps (copied right away, as
    //    a full atlas is reset to make room).
    struct Glyph {
        FONSquad q;
        int w, h;
        size_t offset;
    };
    std::vector<Glyph> glyphs;
    std::vector<unsigned char> bitmaps;
    fonsSetSize(fs, state.fontSize * scale);
    fonsSetSpacing(fs, state.letterSpacing * scale);
    fonsSetBlur(fs, state.fontBlur * scale);
    fonsSetDilate(fs, state.fontDilate * scale);
    fonsSetAlign(fs, state.textAlign);
    fonsSetFont(fs, state.fontId);
    FONStextIter iter, prevIter;
    FONSquad q;
    fonsTextIterInit(fs, &iter, 0, 0, string, end, FONS_GLYPH_BITMAP_REQUIRED);
    prevIter = iter;
    float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
    while (fonsTextIterNext(fs, &iter, &q)) {
        if (iter.prevGlyphIndex == -1) {  // atlas full: make room and retry, as text() does
            if (!im.allocTextAtlas()) break;
            iter = prevIter;
            if (!fonsTextIterNext(fs, &iter, &q) || iter.prevGlyphIndex == -1) break;
        }
        prevIter = iter;
        if (!(q.x1 > q.x0) || !(q.y1 > q.y0)) continue;
        int aw = 0, ah = 0;
        const unsigned char* atlas = fonsGetTextureData(fs, &aw, &ah);
        const int sx0 = detail::maxi(0, (int)lroundf(q.s0 * aw)), sy0 = detail::maxi(0, (int)lroundf(q.t0 * ah));
        const int sx1 = detail::mini(aw, (int)lroundf(q.s1 * aw)), sy1 = detail::mini(ah, (int)lroundf(q.t1 * ah));
        if (sx1 <= sx0 || sy1 <= sy0) continue;
        Glyph g{q, sx1 - sx0, sy1 - sy0, bitmaps.size()};
        for (int r = sy0; r < sy1; r++) bitmaps.insert(bitmaps.end(), atlas + (size_t)r * aw + sx0, atlas + (size_t)r * aw + sx1);
        glyphs.push_back(g);
        minx = std::min(minx, q.x0);
        miny = std::min(miny, q.y0);
        maxx = std::max(maxx, q.x1);
        maxy = std::max(maxy, q.y1);
    }
    if (glyphs.empty()) return;

    // 2. Rasterize the text's coverage into a pixel grid around it, sampling the bitmaps the way the GPU
    //    samples the atlas, so the shadow's edges line up with the drawn glyphs.
    const int pad = (int)ceilf(blur) + 2;
    const int gx0 = (int)floorf(minx) - pad, gy0 = (int)floorf(miny) - pad;
    const int gw = (int)ceilf(maxx) + pad - gx0, gh = (int)ceilf(maxy) + pad - gy0;
    if (gw <= 0 || gh <= 0 || gw > 4096 || gh > 4096) return;
    std::vector<float>& cover = im.shadowCoverage;
    cover.assign((size_t)gw * gh, 0.0f);
    for (const Glyph& g : glyphs) {
        const float kx = g.w / (g.q.x1 - g.q.x0), ky = g.h / (g.q.y1 - g.q.y0);
        const int px0 = (int)floorf(g.q.x0), py0 = (int)floorf(g.q.y0), px1 = (int)ceilf(g.q.x1), py1 = (int)ceilf(g.q.y1);
        for (int py = py0; py < py1; py++) {
            for (int px = px0; px < px1; px++) {
                // Texel coordinates in the glyph's bitmap, texel centers at i + 0.5.
                const float u = (px + 0.5f - g.q.x0) * kx - 0.5f, v = (py + 0.5f - g.q.y0) * ky - 0.5f;
                const float fu = floorf(u), fv = floorf(v);
                const int iu = (int)fu, iv = (int)fv;
                const float tu = u - fu, tv = v - fv;
                auto at = [&](int cu, int cv) { return cu < 0 || cv < 0 || cu >= g.w || cv >= g.h ? 0.0f : bitmaps[g.offset + (size_t)cv * g.w + cu] / 255.0f; };
                const float a = (at(iu, iv) * (1 - tu) + at(iu + 1, iv) * tu) * (1 - tv) + (at(iu, iv + 1) * (1 - tu) + at(iu + 1, iv + 1) * tu) * tv;
                if (a <= 0.0f) continue;
                float& c = cover[(size_t)(py - gy0) * gw + (px - gx0)];
                c = 1.0f - (1.0f - c) * (1.0f - a);  // glyphs overlap like src-over coverage
            }
        }
    }

    // 3. Shadow = coverage x (1 - the blurred coverage moved by the offset).
    std::vector<float>& blurred = im.shadowBlur;
    blurred = cover;
    blurMask(blurred.data(), gw, gh, blur);

    // 4. Upload into this frame's next mask texture (one extra zero row and column so linear filtering
    //    at the quad's edges never reads stale pixels).
    if (im.shadowImageIdx >= im.shadowImages.size()) im.shadowImages.emplace_back();
    ContextImpl::ShadowImage& slot = im.shadowImages[im.shadowImageIdx++];
    if (slot.image == 0 || slot.width < gw + 1 || slot.height < gh + 1) {
        if (slot.image != 0) im.params.renderDeleteTexture(im.params.userPtr, slot.image);
        slot.width = std::max(slot.width, (gw + 1 + 63) & ~63);
        slot.height = std::max(slot.height, (gh + 1 + 63) & ~63);
        slot.image = im.params.renderCreateTexture(im.params.userPtr, static_cast<int>(Texture::Alpha), slot.width, slot.height, 0, nullptr);
        if (slot.image == 0) {
            slot.width = slot.height = 0;
            return;
        }
    }
    std::vector<unsigned char>& pixels = im.shadowPixels;
    pixels.assign((size_t)slot.width * (gh + 1), 0);
    for (int j = 0; j < gh; j++) {
        for (int i = 0; i < gw; i++) {
            const float c = cover[(size_t)j * gw + i];
            if (c <= 0.0f) continue;
            const float b = sampleMask(blurred.data(), gw, gh, i + 0.5f - ox, j + 0.5f - oy);
            pixels[(size_t)j * slot.width + i] = (unsigned char)lroundf(detail::clampf(c * (1.0f - b), 0.0f, 1.0f) * 255.0f);
        }
    }
    im.params.renderUpdateTexture(im.params.userPtr, slot.image, 0, 0, gw + 1, gh + 1, pixels.data());

    // 5. Draw the mask as one quad over the text, tinted with the shadow color (like glyphs).
    const bool isFlipped = detail::isTransformFlipped(state.xform.data()) != 0;
    float qy0 = (float)gy0, qy1 = (float)(gy0 + gh);
    if (isFlipped) std::swap(qy0, qy1);  // keeps the triangles' winding, as text() does
    auto vert = [&](float px, float py) {
        Vertex v{};
        transformPoint(v.x, v.y, state.xform.data(), px * invscale + x, py * invscale + y);
        v.u = (px - gx0) / slot.width;
        v.v = (py - gy0) / slot.height;
        return v;
    };
    const float qx0 = (float)gx0, qx1 = (float)(gx0 + gw);
    const Vertex a = vert(qx0, qy0), b = vert(qx1, qy0), c = vert(qx1, qy1), d = vert(qx0, qy1);
    const std::array<Vertex, 6> verts = {a, c, b, a, d, c};
    Paint paint{};
    detail::setPaintColor(paint, state.textInnerShadowColor);
    paint.innerColor.a *= state.alpha;
    paint.outerColor.a *= state.alpha;
    paint.image = slot.image;
    Scissor scissor = state.scissor;
    im.params.renderTriangles(im.params.userPtr, &paint, state.compositeOperation, &scissor, verts.data(), static_cast<int>(verts.size()), im.fringeWidth);
    im.drawCallCount++;
    im.textTriCount += 2;
}

void Context::fontDilate(float dilate)
{
    State& state = mImpl->topState();
    state.fontDilate = dilate;
}

void Context::textFill(TextFill mode)
{
    mImpl->topState().textFill = mode == TextFill::Paint ? TextFill::Paint : TextFill::Color;
}

void Context::textLetterSpacing(float spacing)
{
    State& state = mImpl->topState();
    state.letterSpacing = spacing;
}

void Context::textLineHeight(float lineHeight)
{
    State& state = mImpl->topState();
    state.lineHeight = lineHeight;
}

void Context::textAlign(int align)
{
    State& state = mImpl->topState();
    state.textAlign = align;
}

void Context::fontFaceId(int font)
{
    State& state = mImpl->topState();
    state.fontId = font;
}

void Context::fontFace(const char* font)
{
    State& state = mImpl->topState();
    state.fontId = fonsGetFontByName(mImpl->fs.get(), font);
}

float Context::text(float x, float y, const char* string, const char* end)
{
    if (end == NULL) end = string + strlen(string);
    if (mImpl->topState().fontId == FONS_INVALID) return x;

    // Shadow pass first: the same glyphs, blurred, shifted and in the shadow color.
    if (mImpl->topState().textOuterShadowColor.a > 0.0f) {
        save();
        State& shadow = mImpl->topState();
        detail::setPaintColor(shadow.fill, shadow.textOuterShadowColor);
        shadow.fontBlur += shadow.textOuterShadowBlur;
        shadow.textOuterShadowColor.a = 0.0f;
        shadow.textInnerShadowColor.a = 0.0f;
        translate(shadow.textOuterShadowX, shadow.textOuterShadowY);
        text(x, y, string, end);
        restore();
    }

    State& state = mImpl->topState();
    FONStextIter iter, prevIter;
    FONSquad q;
    std::vector<Vertex>& buf = mImpl->cache->verts;
    float scale = detail::getFontScale(&state) * mImpl->devicePxRatio;
    float invscale = 1.0f / scale;
    int cverts = 0;
    int isFlipped = detail::isTransformFlipped(state.xform.data());

    fonsSetSize(mImpl->fs.get(), state.fontSize * scale);
    fonsSetSpacing(mImpl->fs.get(), state.letterSpacing * scale);
    fonsSetBlur(mImpl->fs.get(), state.fontBlur * scale);
    fonsSetDilate(mImpl->fs.get(), state.fontDilate * scale);
    fonsSetAlign(mImpl->fs.get(), state.textAlign);
    fonsSetFont(mImpl->fs.get(), state.fontId);

    cverts = detail::maxi(2, (int)(end - string)) * 6;  // conservative estimate.
    mImpl->prepareTempVerts(cverts);

    fonsTextIterInit(mImpl->fs.get(), &iter, 0, 0, string, end, FONS_GLYPH_BITMAP_REQUIRED);
    prevIter = iter;
    while (fonsTextIterNext(mImpl->fs.get(), &iter, &q)) {
        std::array<float, 8> c{};
        if (iter.prevGlyphIndex == -1) {  // can not retrieve glyph?
            if (!buf.empty()) {
                mImpl->renderText(buf.data(), static_cast<int>(buf.size()));
                buf.clear();
            }
            if (!mImpl->allocTextAtlas()) break;  // no memory :(
            iter = prevIter;
            fonsTextIterNext(mImpl->fs.get(), &iter, &q);  // try again
            if (iter.prevGlyphIndex == -1)                 // still can not find glyph?
                break;
        }
        prevIter = iter;
        if (isFlipped) {
            float tmp;

            tmp = q.y0;
            q.y0 = q.y1;
            q.y1 = tmp;
            tmp = q.t0;
            q.t0 = q.t1;
            q.t1 = tmp;
        }
        // Transform corners.
        transformPoint(c[0], c[1], state.xform.data(), q.x0 * invscale + x, q.y0 * invscale + y);
        transformPoint(c[2], c[3], state.xform.data(), q.x1 * invscale + x, q.y0 * invscale + y);
        transformPoint(c[4], c[5], state.xform.data(), q.x1 * invscale + x, q.y1 * invscale + y);
        transformPoint(c[6], c[7], state.xform.data(), q.x0 * invscale + x, q.y1 * invscale + y);
        // Create triangles
        if (buf.size() + 6 <= buf.capacity()) {
            detail::pushVertex(buf, c[0], c[1], q.s0, q.t0, 0, 0);
            detail::pushVertex(buf, c[4], c[5], q.s1, q.t1, 0, 0);
            detail::pushVertex(buf, c[2], c[3], q.s1, q.t0, 0, 0);
            detail::pushVertex(buf, c[0], c[1], q.s0, q.t0, 0, 0);
            detail::pushVertex(buf, c[6], c[7], q.s0, q.t1, 0, 0);
            detail::pushVertex(buf, c[4], c[5], q.s1, q.t1, 0, 0);
        }
    }

    // TODO: add back-end bit to do this just once per frame.
    mImpl->flushTextTexture();

    mImpl->renderText(buf.data(), static_cast<int>(buf.size()));
    const float advance = iter.nextx * invscale + x;
    if (state.textInnerShadowColor.a > 0.0f) textInnerShadowPass(x, y, string, end);
    return advance;
}

void Context::textBox(float x, float y, float breakRowWidth, const char* string, const char* end)
{
    State& state = mImpl->topState();
    std::array<TextRow, 2> rows{};
    int nrows = 0, i;
    int oldAlign = state.textAlign;
    int halign = state.textAlign & static_cast<int>(Align::Left | Align::Center | Align::Right);
    int valign = state.textAlign & static_cast<int>(Align::Top | Align::Middle | Align::MiddleAscent | Align::Bottom | Align::Baseline);
    float lineh = 0;

    if (state.fontId == FONS_INVALID) return;

    textMetrics(NULL, NULL, &lineh);

    state.textAlign = static_cast<int>(Align::Left) | valign;

    while ((nrows = textBreakLines(string, end, breakRowWidth, rows.data(), (int)rows.size(), 0))) {
        for (i = 0; i < nrows; i++) {
            TextRow* row = &rows[(size_t)i];
            if (halign & Align::Left)
                text(x, y, row->start, row->end);
            else if (halign & Align::Center)
                text(x + breakRowWidth * 0.5f - row->width * 0.5f, y, row->start, row->end);
            else if (halign & Align::Right)
                text(x + breakRowWidth - row->width, y, row->start, row->end);
            y += lineh * state.lineHeight;
        }
        string = rows[(size_t)nrows - 1].next;
    }

    state.textAlign = oldAlign;
}

// Text along a path (SVG <textPath>)

// The current path flattened into local-space segments laid end to end, with the distance along the
// path at the start of each. Tangents are blended across smooth joints (the flattened curves), so glyphs
// turn gradually along curves but keep the segment direction on straight edges up to a corner.
struct TextPathGeometry {
    struct Seg {
        float x, y;      // start point
        float dx, dy;    // unit direction
        float len, s;    // length, distance along the path at the start
        float t0x, t0y;  // unit tangents at the start and end (equal to the direction unless smooth)
        float t1x, t1y;
    };
    std::vector<Seg> segs;
    float length = 0.0f;
    bool wrap = false;  // one closed subpath: positions wrap around

    // Point and unit tangent at distance s; extrapolates along the end segments. False when s is off
    // the path (and the path does not wrap).
    bool at(float s, float& x, float& y, float& tx, float& ty) const
    {
        bool on = true;
        if (wrap && length > 0.0f) {
            s = std::fmod(s, length);
            if (s < 0.0f) s += length;
        } else if (s < 0.0f || s > length) {
            on = false;
        }
        auto it = std::upper_bound(segs.begin(), segs.end(), s, [](float v, const Seg& g) { return v < g.s; });
        const Seg& g = it == segs.begin() ? segs.front() : *(it - 1);
        const float d = s - g.s;
        x = g.x + g.dx * d;
        y = g.y + g.dy * d;
        const float u = detail::clampf(d / g.len, 0.0f, 1.0f);
        tx = g.t0x + (g.t1x - g.t0x) * u;
        ty = g.t0y + (g.t1y - g.t0y) * u;
        const float tl = sqrtf(tx * tx + ty * ty);
        if (tl > 1e-6f) {
            tx /= tl;
            ty /= tl;
        } else {
            tx = g.dx;
            ty = g.dy;
        }
        return on;
    }
};

namespace
{
struct TextPathPoint {
    float x, y;
};
struct TextPathPolyline {
    std::vector<TextPathPoint> pts;
    bool closed = false;
};

void flattenTextPathBezier(std::vector<TextPathPoint>& out, float tol, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, int level)
{
    if (level > 10) return;
    const float dx = x4 - x1, dy = y4 - y1;
    const float d2 = std::fabs((x2 - x4) * dy - (y2 - y4) * dx);
    const float d3 = std::fabs((x3 - x4) * dy - (y3 - y4) * dx);
    if ((d2 + d3) * (d2 + d3) < tol * (dx * dx + dy * dy)) {
        out.push_back({x4, y4});
        return;
    }
    const float x12 = (x1 + x2) * 0.5f, y12 = (y1 + y2) * 0.5f;
    const float x23 = (x2 + x3) * 0.5f, y23 = (y2 + y3) * 0.5f;
    const float x34 = (x3 + x4) * 0.5f, y34 = (y3 + y4) * 0.5f;
    const float x123 = (x12 + x23) * 0.5f, y123 = (y12 + y23) * 0.5f;
    const float x234 = (x23 + x34) * 0.5f, y234 = (y23 + y34) * 0.5f;
    const float x1234 = (x123 + x234) * 0.5f, y1234 = (y123 + y234) * 0.5f;
    flattenTextPathBezier(out, tol, x1, y1, x12, y12, x123, y123, x1234, y1234, level + 1);
    flattenTextPathBezier(out, tol, x1234, y1234, x234, y234, x34, y34, x4, y4, level + 1);
}

// Flattens a (device-space) command list into polylines in the local space given by invXform. Unlike
// ContextImpl::flattenPaths it keeps every subpath's direction (no winding is enforced).
std::vector<TextPathPolyline> flattenTextPath(const std::vector<float>& cmds, const float* invXform, float tessTol)
{
    std::vector<TextPathPolyline> subs;
    float curx = 0.0f, cury = 0.0f;
    auto current = [&]() -> std::vector<TextPathPoint>& {
        if (subs.empty() || subs.back().closed) {
            // Drawing on after closePath (or with no moveTo) starts a new subpath at the current point.
            subs.emplace_back();
            subs.back().pts.push_back({curx, cury});
        }
        return subs.back().pts;
    };
    size_t i = 0;
    while (i < cmds.size()) {
        const int cmd = static_cast<int>(cmds[i]);
        switch (cmd) {
            case static_cast<int>(PathCommand::MoveTo):
                if (i + 3 > cmds.size()) i = cmds.size();
                else {
                    curx = cmds[i + 1];
                    cury = cmds[i + 2];
                    subs.emplace_back();
                    subs.back().pts.push_back({curx, cury});
                    i += 3;
                }
                break;
            case static_cast<int>(PathCommand::LineTo):
                if (i + 3 > cmds.size()) i = cmds.size();
                else {
                    current().push_back({cmds[i + 1], cmds[i + 2]});
                    curx = cmds[i + 1];
                    cury = cmds[i + 2];
                    i += 3;
                }
                break;
            case static_cast<int>(PathCommand::BezierTo):
            case static_cast<int>(PathCommand::BezierContinue):
                if (i + 7 > cmds.size()) i = cmds.size();
                else {
                    std::vector<TextPathPoint>& pts = current();
                    flattenTextPathBezier(pts, tessTol, curx, cury, cmds[i + 1], cmds[i + 2], cmds[i + 3], cmds[i + 4], cmds[i + 5], cmds[i + 6], 0);
                    curx = cmds[i + 5];
                    cury = cmds[i + 6];
                    i += 7;
                }
                break;
            case static_cast<int>(PathCommand::Close):
                if (!subs.empty() && !subs.back().closed) {
                    TextPathPolyline& sp = subs.back();
                    sp.closed = true;
                    sp.pts.push_back(sp.pts.front());  // the closing segment
                    curx = sp.pts.front().x;
                    cury = sp.pts.front().y;
                }
                i++;
                break;
            case static_cast<int>(PathCommand::Winding):
                i += 2;
                break;
            default:
                i++;
        }
    }
    for (TextPathPolyline& sp : subs)
        for (TextPathPoint& p : sp.pts) transformPoint(p.x, p.y, invXform, p.x, p.y);
    return subs;
}

TextPathGeometry buildTextPathGeometry(std::vector<TextPathPolyline> subs, bool reverse)
{
    TextPathGeometry geo;
    if (reverse) {
        std::reverse(subs.begin(), subs.end());
        for (TextPathPolyline& sp : subs) std::reverse(sp.pts.begin(), sp.pts.end());
    }
    // Joints bending less than this are treated as smooth (curve flattening), sharper ones as corners.
    const float smoothCos = 0.96f;  // about 16 degrees
    int nonEmpty = 0;
    bool lastClosed = false;
    for (const TextPathPolyline& sp : subs) {
        const size_t first = geo.segs.size();
        for (size_t k = 0; k + 1 < sp.pts.size(); k++) {
            float dx = sp.pts[k + 1].x - sp.pts[k].x, dy = sp.pts[k + 1].y - sp.pts[k].y;
            const float len = sqrtf(dx * dx + dy * dy);
            if (!(len > 1e-5f)) continue;
            dx /= len;
            dy /= len;
            geo.segs.push_back({sp.pts[k].x, sp.pts[k].y, dx, dy, len, geo.length, dx, dy, dx, dy});
            geo.length += len;
        }
        const size_t n = geo.segs.size() - first;
        if (n == 0) continue;
        nonEmpty++;
        lastClosed = sp.closed;
        // Blend tangents across smooth joints (including the closing joint of a closed subpath).
        auto blend = [&](TextPathGeometry::Seg& a, TextPathGeometry::Seg& b) {
            if (a.dx * b.dx + a.dy * b.dy < smoothCos) return;
            float tx = a.dx + b.dx, ty = a.dy + b.dy;
            const float tl = sqrtf(tx * tx + ty * ty);
            if (!(tl > 1e-6f)) return;
            tx /= tl;
            ty /= tl;
            a.t1x = b.t0x = tx;
            a.t1y = b.t0y = ty;
        };
        for (size_t k = first; k + 1 < geo.segs.size(); k++) blend(geo.segs[k], geo.segs[k + 1]);
        if (sp.closed && n > 1) blend(geo.segs.back(), geo.segs[first]);
    }
    geo.wrap = nonEmpty == 1 && lastClosed;
    return geo;
}
}  // namespace

float Context::textOnPath(const char* string, const char* end, const TextPathOptions& options)
{
    if (string == nullptr) return 0.0f;
    if (end == nullptr) end = string + strlen(string);
    State& state = mImpl->topState();
    if (state.fontId == FONS_INVALID) return 0.0f;

    // The path's commands are stored in device space; lay the text out in the current local space.
    std::array<float, 6> inv{};
    if (!transformInverse(inv.data(), state.xform.data())) return 0.0f;
    const TextPathGeometry geo = buildTextPathGeometry(flattenTextPath(mImpl->commands, inv.data(), mImpl->tessTol), options.side == TextPathSide::Right);
    if (geo.segs.empty()) return 0.0f;

    if (state.textOuterShadowColor.a > 0.0f) {
        save();
        State& shadow = mImpl->topState();
        detail::setPaintColor(shadow.fill, shadow.textOuterShadowColor);
        shadow.fontBlur += shadow.textOuterShadowBlur;
        textOnPathPass(geo, string, end, options, shadow.textOuterShadowX, shadow.textOuterShadowY);
        restore();
    }
    return textOnPathPass(geo, string, end, options, 0.0f, 0.0f);
}

float Context::textOnPathPass(const TextPathGeometry& geo, const char* string, const char* end, const TextPathOptions& options, float shiftX, float shiftY)
{
    State& state = mImpl->topState();
    FONScontext* fs = mImpl->fs.get();
    std::vector<Vertex>& buf = mImpl->cache->verts;
    const float scale = detail::getFontScale(&state) * mImpl->devicePxRatio;
    const float invscale = 1.0f / scale;
    const bool isFlipped = detail::isTransformFlipped(state.xform.data()) != 0;
    const bool stretch = options.method == TextPathMethod::Stretch;
    const float start = options.startOffsetPercent ? options.startOffset * 0.01f * geo.length : options.startOffset;
    const float shift = options.baselineShift;
    constexpr int maxSlices = 24;

    fonsSetSize(fs, state.fontSize * scale);
    fonsSetSpacing(fs, state.letterSpacing * scale);
    fonsSetBlur(fs, state.fontBlur * scale);
    fonsSetDilate(fs, state.fontDilate * scale);
    fonsSetAlign(fs, state.textAlign);  // horizontal align = text-anchor, vertical = baseline placement
    fonsSetFont(fs, state.fontId);

    mImpl->prepareTempVerts(detail::maxi(2, (int)(end - string)) * 6 * (stretch ? maxSlices : 1));

    auto emit = [&](float lx, float ly, float s, float t) {
        float vx, vy;
        transformPoint(vx, vy, state.xform.data(), lx + shiftX, ly + shiftY);
        detail::pushVertex(buf, vx, vy, s, t, 0, 0);
    };
    // Two triangles for a quad given as top-left, top-right, bottom-right, bottom-left (as text() does).
    auto quad = [&](const float* c, float s0, float t0, float s1, float t1) {
        if (buf.size() + 6 > buf.capacity()) return;
        emit(c[0], c[1], s0, t0);
        emit(c[4], c[5], s1, t1);
        emit(c[2], c[3], s1, t0);
        emit(c[0], c[1], s0, t0);
        emit(c[6], c[7], s0, t1);
        emit(c[4], c[5], s1, t1);
    };

    FONStextIter iter, prevIter;
    FONSquad q;
    fonsTextIterInit(fs, &iter, 0, 0, string, end, FONS_GLYPH_BITMAP_REQUIRED);
    prevIter = iter;
    while (fonsTextIterNext(fs, &iter, &q)) {
        if (iter.prevGlyphIndex == -1) {  // atlas full: draw what we have, make room and retry
            if (!buf.empty()) {
                mImpl->renderText(buf.data(), static_cast<int>(buf.size()));
                buf.clear();
            }
            if (!mImpl->allocTextAtlas()) break;
            iter = prevIter;
            fonsTextIterNext(fs, &iter, &q);
            if (iter.prevGlyphIndex == -1) break;
        }
        prevIter = iter;
        if (!(q.x1 > q.x0)) continue;
        if (isFlipped) {
            std::swap(q.y0, q.y1);
            std::swap(q.t0, q.t1);
        }
        // The glyph is positioned by the midpoint of its advance, as SVG does.
        const float mid = (iter.x + iter.nextx) * 0.5f * invscale;
        float px, py, tx, ty;
        if (!geo.at(start + mid, px, py, tx, ty)) continue;
        const float gx0 = q.x0 * invscale, gx1 = q.x1 * invscale;
        const float gy0 = q.y0 * invscale + shift, gy1 = q.y1 * invscale + shift;
        if (!stretch) {
            // Rigid glyph in the frame (tangent, normal) at the midpoint; the normal (-ty, tx) is the
            // glyph's +y, so the text sits upright on the left of the path's direction.
            auto map = [&](float gx, float gy, float* o) {
                const float u = gx - mid;
                o[0] = px + tx * u - ty * gy;
                o[1] = py + ty * u + tx * gy;
            };
            std::array<float, 8> c{};
            map(gx0, gy0, &c[0]);
            map(gx1, gy0, &c[2]);
            map(gx1, gy1, &c[4]);
            map(gx0, gy1, &c[6]);
            quad(c.data(), q.s0, q.t0, q.s1, q.t1);
        } else {
            // Slices across the glyph, each edge placed and oriented by the path at its own distance.
            const int n = detail::clampi(static_cast<int>(ceilf((q.x1 - q.x0) / 3.0f)), 1, maxSlices);
            std::array<float, 4> prev{};
            for (int k = 0; k <= n; k++) {
                const float u = static_cast<float>(k) / n;
                float ex, ey, etx, ety;
                geo.at(start + gx0 + (gx1 - gx0) * u, ex, ey, etx, ety);
                const std::array<float, 4> edge = {ex - ety * gy0, ey + etx * gy0, ex - ety * gy1, ey + etx * gy1};
                if (k > 0) {
                    const std::array<float, 8> c = {prev[0], prev[1], edge[0], edge[1], edge[2], edge[3], prev[2], prev[3]};
                    const float sa = q.s0 + (q.s1 - q.s0) * (static_cast<float>(k - 1) / n), sb = q.s0 + (q.s1 - q.s0) * u;
                    quad(c.data(), sa, q.t0, sb, q.t1);
                }
                prev = edge;
            }
        }
    }

    mImpl->flushTextTexture();
    mImpl->renderText(buf.data(), static_cast<int>(buf.size()));
    return start + iter.nextx * invscale;
}

int Context::textGlyphPositions(float x, float y, const char* string, const char* end, GlyphPosition* positions, int maxPositions)
{
    UNUSED(y);
    State& state = mImpl->topState();
    float scale = detail::getFontScale(&state) * mImpl->devicePxRatio;
    float invscale = 1.0f / scale;
    FONStextIter iter, prevIter;
    FONSquad q;
    int npos = 0;

    if (state.fontId == FONS_INVALID) return 0;

    if (end == NULL) end = string + strlen(string);

    if (string == end) return 0;

    fonsSetSize(mImpl->fs.get(), state.fontSize * scale);
    fonsSetSpacing(mImpl->fs.get(), state.letterSpacing * scale);
    fonsSetBlur(mImpl->fs.get(), state.fontBlur * scale);
    fonsSetAlign(mImpl->fs.get(), state.textAlign);
    fonsSetFont(mImpl->fs.get(), state.fontId);

    fonsTextIterInit(mImpl->fs.get(), &iter, 0, 0, string, end, FONS_GLYPH_BITMAP_OPTIONAL);
    prevIter = iter;
    while (fonsTextIterNext(mImpl->fs.get(), &iter, &q)) {
        if (iter.prevGlyphIndex < 0 && mImpl->allocTextAtlas()) {  // can not retrieve glyph?
            iter = prevIter;
            fonsTextIterNext(mImpl->fs.get(), &iter, &q);  // try again
        }
        prevIter = iter;
        positions[npos].str = iter.str;
        positions[npos].x = iter.x * invscale + x;
        positions[npos].minx = detail::minf(iter.x, q.x0) * invscale + x;
        positions[npos].maxx = detail::maxf(iter.nextx, q.x1) * invscale + x;
        npos++;
        if (npos >= maxPositions) break;
    }

    return npos;
}

enum class CodepointType : int {
    Space,
    Newline,
    Char,
    CjkChar,
};

int Context::textBreakLines(const char* string, const char* end, float breakRowWidth, TextRow* rows, int maxRows, int skipSpaces)
{
    State& state = mImpl->topState();
    float scale = detail::getFontScale(&state) * mImpl->devicePxRatio;
    float invscale = 1.0f / scale;
    FONStextIter iter, prevIter;
    FONSquad q;
    int nrows = 0;
    float rowStartX = 0;
    float rowWidth = 0;
    float rowMinX = 0;
    float rowMaxX = 0;
    const char* rowStart = NULL;
    const char* rowEnd = NULL;
    const char* wordStart = NULL;
    float wordStartX = 0;
    float wordMinX = 0;
    const char* breakEnd = NULL;
    float breakWidth = 0;
    float breakMaxX = 0;
    CodepointType type = CodepointType::Space, ptype = CodepointType::Space;
    unsigned int pcodepoint = 0;

    if (maxRows == 0) return 0;
    if (state.fontId == FONS_INVALID) return 0;

    if (end == NULL) end = string + strlen(string);

    if (string == end) return 0;

    fonsSetSize(mImpl->fs.get(), state.fontSize * scale);
    fonsSetSpacing(mImpl->fs.get(), state.letterSpacing * scale);
    fonsSetBlur(mImpl->fs.get(), state.fontBlur * scale);
    fonsSetDilate(mImpl->fs.get(), state.fontDilate * scale);
    fonsSetAlign(mImpl->fs.get(), state.textAlign);
    fonsSetFont(mImpl->fs.get(), state.fontId);

    breakRowWidth *= scale;

    fonsTextIterInit(mImpl->fs.get(), &iter, 0, 0, string, end, FONS_GLYPH_BITMAP_OPTIONAL);
    prevIter = iter;
    while (fonsTextIterNext(mImpl->fs.get(), &iter, &q)) {
        if (iter.prevGlyphIndex < 0 && mImpl->allocTextAtlas()) {  // can not retrieve glyph?
            iter = prevIter;
            fonsTextIterNext(mImpl->fs.get(), &iter, &q);  // try again
        }
        prevIter = iter;
        switch (iter.codepoint) {
            case 9:       // \t
            case 11:      // \v
            case 12:      // \f
            case 32:      // space
            case 0x00a0:  // NBSP
                type = CodepointType::Space;
                break;
            case 10:  // \n
                type = pcodepoint == 13 ? CodepointType::Space : CodepointType::Newline;
                break;
            case 13:  // \r
                type = pcodepoint == 10 ? CodepointType::Space : CodepointType::Newline;
                break;
            case 0x0085:  // NEL
                type = CodepointType::Newline;
                break;
            default:
                if ((iter.codepoint >= 0x4E00 && iter.codepoint <= 0x9FFF) || (iter.codepoint >= 0x3000 && iter.codepoint <= 0x30FF) || (iter.codepoint >= 0xFF00 && iter.codepoint <= 0xFFEF) ||
                    (iter.codepoint >= 0x1100 && iter.codepoint <= 0x11FF) || (iter.codepoint >= 0x3130 && iter.codepoint <= 0x318F) || (iter.codepoint >= 0xAC00 && iter.codepoint <= 0xD7AF))
                    type = CodepointType::CjkChar;
                else
                    type = CodepointType::Char;
                break;
        }

        if (type == CodepointType::Newline) {
            // Always handle new lines.
            rows[nrows].start = rowStart != NULL ? rowStart : iter.str;
            rows[nrows].end = rowEnd != NULL ? rowEnd : iter.str;
            rows[nrows].width = rowWidth * invscale;
            rows[nrows].minx = rowMinX * invscale;
            rows[nrows].maxx = rowMaxX * invscale;
            rows[nrows].next = iter.next;
            nrows++;
            if (nrows >= maxRows) return nrows;
            // Set null break point
            breakEnd = rowStart;
            breakWidth = 0.0;
            breakMaxX = 0.0;
            // Indicate to skip the white space at the beginning of the row.
            rowStart = NULL;
            rowEnd = NULL;
            rowWidth = 0;
            rowMinX = rowMaxX = 0;
        } else {
            if (rowStart == NULL) {
                // Skip white space until the beginning of the line
                if (type == CodepointType::Char || type == CodepointType::CjkChar) {
                    // The current char is the row so far
                    rowStartX = iter.x;
                    rowStart = iter.str;
                    rowEnd = iter.next;
                    rowWidth = iter.nextx - rowStartX;
                    rowMinX = q.x0 - rowStartX;
                    rowMaxX = q.x1 - rowStartX;
                    wordStart = iter.str;
                    wordStartX = iter.x;
                    wordMinX = q.x0 - rowStartX;
                    // Set null break point
                    breakEnd = rowStart;
                    breakWidth = 0.0;
                    breakMaxX = 0.0;
                }
            } else {
                float nextWidth = iter.nextx - rowStartX;

                // track last non-white space character
                if (type == CodepointType::Char || type == CodepointType::CjkChar) {
                    rowEnd = iter.next;
                    rowWidth = iter.nextx - rowStartX;
                    rowMaxX = q.x1 - rowStartX;
                }
                // track last end of a word
                if (((ptype == CodepointType::Char || ptype == CodepointType::CjkChar) && type == CodepointType::Space) || type == CodepointType::CjkChar) {
                    breakEnd = iter.str;
                    breakWidth = rowWidth;
                    breakMaxX = rowMaxX;
                }
                // track last beginning of a word
                if ((ptype == CodepointType::Space && (type == CodepointType::Char || type == CodepointType::CjkChar)) || type == CodepointType::CjkChar) {
                    wordStart = iter.str;
                    wordStartX = iter.x;
                    wordMinX = q.x0;
                }

                // Break to new line when a character is beyond break width.
                if ((type == CodepointType::Char || type == CodepointType::CjkChar) && nextWidth > breakRowWidth) {
                    // The run length is too long, need to break to new line.
                    if (breakEnd == rowStart) {
                        // The current word is longer than the row length, just break it from here.
                        rows[nrows].start = rowStart;
                        rows[nrows].end = iter.str;
                        rows[nrows].width = rowWidth * invscale;
                        rows[nrows].minx = rowMinX * invscale;
                        rows[nrows].maxx = rowMaxX * invscale;
                        rows[nrows].next = iter.str;
                        nrows++;
                        if (nrows >= maxRows) return nrows;
                        rowStartX = iter.x;
                        rowStart = iter.str;
                        rowEnd = iter.next;
                        rowWidth = iter.nextx - rowStartX;
                        rowMinX = q.x0 - rowStartX;
                        rowMaxX = q.x1 - rowStartX;
                        wordStart = iter.str;
                        wordStartX = iter.x;
                        wordMinX = q.x0 - rowStartX;
                    } else {
                        // Break the line from the end of the last word, and start new line from the beginning of the new.
                        rows[nrows].start = rowStart;
                        rows[nrows].end = breakEnd;
                        rows[nrows].width = breakWidth * invscale;
                        rows[nrows].minx = rowMinX * invscale;
                        rows[nrows].maxx = breakMaxX * invscale;
                        rows[nrows].next = wordStart;
                        nrows++;
                        if (nrows >= maxRows) return nrows;
                        // Update row
                        rowStartX = wordStartX;
                        rowStart = wordStart;
                        rowEnd = iter.next;
                        rowWidth = iter.nextx - rowStartX;
                        rowMinX = wordMinX - rowStartX;
                        rowMaxX = q.x1 - rowStartX;
                    }
                    // Set null break point
                    breakEnd = rowStart;
                    breakWidth = 0.0;
                    breakMaxX = 0.0;
                }
            }
        }

        pcodepoint = iter.codepoint;
        ptype = type;
    }

    // Break the line from the end of the last word, and start new line from the beginning of the new.
    if (rowStart != NULL) {
        rows[nrows].start = rowStart;
        rows[nrows].end = rowEnd;
        rows[nrows].width = rowWidth * invscale;
        rows[nrows].minx = rowMinX * invscale;
        rows[nrows].maxx = rowMaxX * invscale;
        rows[nrows].next = end;
        nrows++;
    } else if (!skipSpaces) {
        rows[nrows].start = end;
        rows[nrows].end = end;
        rows[nrows].width = rowWidth * invscale;
        rows[nrows].minx = rowMinX * invscale;
        rows[nrows].maxx = rowMaxX * invscale;
        rows[nrows].next = end;
        nrows++;
    }

    return nrows;
}

float Context::textBounds(float x, float y, const char* string, const char* end, float* bounds)
{
    State& state = mImpl->topState();
    float scale = detail::getFontScale(&state) * mImpl->devicePxRatio;
    float invscale = 1.0f / scale;
    float width;

    if (state.fontId == FONS_INVALID) return 0;

    fonsSetSize(mImpl->fs.get(), state.fontSize * scale);
    fonsSetSpacing(mImpl->fs.get(), state.letterSpacing * scale);
    fonsSetBlur(mImpl->fs.get(), state.fontBlur * scale);
    fonsSetDilate(mImpl->fs.get(), state.fontDilate * scale);
    fonsSetAlign(mImpl->fs.get(), state.textAlign);
    fonsSetFont(mImpl->fs.get(), state.fontId);

    width = fonsTextBounds(mImpl->fs.get(), 0, 0, string, end, bounds);
    if (bounds != NULL) {
        // Use line bounds for height.
        fonsLineBounds(mImpl->fs.get(), 0, &bounds[1], &bounds[3]);
        bounds[0] = bounds[0] * invscale + x;
        bounds[1] = bounds[1] * invscale + y;
        bounds[2] = bounds[2] * invscale + x;
        bounds[3] = bounds[3] * invscale + y;
    }
    return width * invscale;
}

void Context::textBoxBounds(float x, float y, float breakRowWidth, const char* string, const char* end, float* bounds)
{
    State& state = mImpl->topState();
    std::array<TextRow, 2> rows{};
    float scale = detail::getFontScale(&state) * mImpl->devicePxRatio;
    float invscale = 1.0f / scale;
    float yoff = 0;
    int nrows = 0, i;
    int oldAlign = state.textAlign;
    int halign = state.textAlign & static_cast<int>(Align::Left | Align::Center | Align::Right);
    int valign = state.textAlign & static_cast<int>(Align::Top | Align::Middle | Align::MiddleAscent | Align::Bottom | Align::Baseline);
    float lineh = 0, rminy = 0, rmaxy = 0;
    float minx, miny, maxx, maxy;

    if (state.fontId == FONS_INVALID) {
        if (bounds != NULL) bounds[0] = bounds[1] = bounds[2] = bounds[3] = 0.0f;
        return;
    }

    textMetrics(NULL, NULL, &lineh);

    state.textAlign = static_cast<int>(Align::Left) | valign;

    minx = maxx = 0;
    miny = maxy = 0;

    fonsSetSize(mImpl->fs.get(), state.fontSize * scale);
    fonsSetSpacing(mImpl->fs.get(), state.letterSpacing * scale);
    fonsSetBlur(mImpl->fs.get(), state.fontBlur * scale);
    fonsSetDilate(mImpl->fs.get(), state.fontDilate * scale);
    fonsSetAlign(mImpl->fs.get(), state.textAlign);
    fonsSetFont(mImpl->fs.get(), state.fontId);
    fonsLineBounds(mImpl->fs.get(), 0, &rminy, &rmaxy);
    rminy *= invscale;
    rmaxy *= invscale;

    while ((nrows = textBreakLines(string, end, breakRowWidth, rows.data(), (int)rows.size(), 0))) {
        for (i = 0; i < nrows; i++) {
            TextRow* row = &rows[(size_t)i];
            float rminx, rmaxx, dx = 0;
            // Horizontal bounds
            if (halign & Align::Left)
                dx = 0;
            else if (halign & Align::Center)
                dx = breakRowWidth * 0.5f - row->width * 0.5f;
            else if (halign & Align::Right)
                dx = breakRowWidth - row->width;
            rminx = row->minx + dx;
            rmaxx = row->maxx + dx;
            minx = detail::minf(minx, rminx);
            maxx = detail::maxf(maxx, rmaxx);
            // Vertical bounds.
            miny = detail::minf(miny, yoff + rminy);
            maxy = detail::maxf(maxy, yoff + rmaxy);

            yoff += lineh * state.lineHeight;
        }
        string = rows[(size_t)nrows - 1].next;
    }

    state.textAlign = oldAlign;

    if (bounds != NULL) {
        bounds[0] = minx + x;
        bounds[1] = miny + y;
        bounds[2] = maxx + x;
        bounds[3] = maxy + y;
    }
}

void Context::textMetrics(float* ascender, float* descender, float* lineh)
{
    State& state = mImpl->topState();
    float scale = detail::getFontScale(&state) * mImpl->devicePxRatio;
    float invscale = 1.0f / scale;

    if (state.fontId == FONS_INVALID) return;

    fonsSetSize(mImpl->fs.get(), state.fontSize * scale);
    fonsSetSpacing(mImpl->fs.get(), state.letterSpacing * scale);
    fonsSetBlur(mImpl->fs.get(), state.fontBlur * scale);
    fonsSetDilate(mImpl->fs.get(), state.fontDilate * scale);
    fonsSetAlign(mImpl->fs.get(), state.textAlign);
    fonsSetFont(mImpl->fs.get(), state.fontId);

    fonsVertMetrics(mImpl->fs.get(), ascender, descender, lineh);
    if (ascender != NULL) *ascender *= invscale;
    if (descender != NULL) *descender *= invscale;
    if (lineh != NULL) *lineh *= invscale;
}
const Params& Context::internalParams() const
{
    return (*this)->params;
}

}  // namespace nvg
