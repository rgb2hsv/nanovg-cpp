//
// Stable C API for nvg::Context (see nanovg.hpp). Implements the declarations in stable/nanovg.h.
//

#include "stable/nanovg.h"

#include "nanovg.hpp"

#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>

struct NVGcontext : nvg::Context {
    explicit NVGcontext(const nvg::Params& params) : nvg::Context(params)
    {
    }
    ~NVGcontext() = default;
};

namespace
{
nvg::Params copyParams(const NVGparams& p) noexcept
{
    nvg::Params out{};
    static_assert(sizeof(NVGparams) == sizeof(nvg::Params));
    static_assert(alignof(NVGparams) == alignof(nvg::Params));
    std::memcpy(&out, &p, sizeof(out));
    return out;
}

nvg::Color toCpp(NVGcolor c) noexcept
{
    nvg::Color r;
    std::memcpy(&r, &c, sizeof(r));
    return r;
}

nvg::Paint toCpp(NVGpaint p) noexcept
{
    nvg::Paint r;
    std::memcpy(&r, &p, sizeof(r));
    return r;
}

NVGcolor toC(const nvg::Color& c) noexcept
{
    NVGcolor r;
    std::memcpy(&r, &c, sizeof(r));
    return r;
}

NVGpaint toC(const nvg::Paint& p) noexcept
{
    NVGpaint r;
    std::memcpy(&r, &p, sizeof(r));
    return r;
}

struct NVGscissorBounds toC(const nvg::ScissorBounds& b) noexcept
{
    NVGscissorBounds r;
    std::memcpy(&r, &b, sizeof(r));
    return r;
}

}  // namespace

namespace
{
nvg::Rect toCpp(NVGrect r) noexcept
{
    return nvg::Rect{r.x, r.y, r.w, r.h};
}

nvg::PaintStyle toPaintStyle(int style) noexcept
{
    switch (style) {
        case NVG_PAINT_STROKE:
            return nvg::PaintStyle::Stroke;
        case NVG_PAINT_FILL_AND_STROKE:
            return nvg::PaintStyle::FillAndStroke;
        default:
            return nvg::PaintStyle::Fill;
    }
}

nvg::PointMode toPointMode(int mode) noexcept
{
    switch (mode) {
        case NVG_POINTS_LINES:
            return nvg::PointMode::Lines;
        case NVG_POINTS_POLYGON:
            return nvg::PointMode::Polygon;
        default:
            return nvg::PointMode::Points;
    }
}
}  // namespace

extern "C" {

void nvgBeginFrame(NVGcontext* ctx, float windowWidth, float windowHeight, float devicePixelRatio)
{
    ctx->beginFrame(windowWidth, windowHeight, devicePixelRatio);
}

void nvgCancelFrame(NVGcontext* ctx)
{
    ctx->cancelFrame();
}

void nvgEndFrame(NVGcontext* ctx)
{
    ctx->endFrame();
}

void nvgGlobalCompositeOperation(NVGcontext* ctx, int op)
{
    ctx->globalCompositeOperation(op);
}

void nvgGlobalCompositeBlendFunc(NVGcontext* ctx, int sfactor, int dfactor)
{
    ctx->globalCompositeBlendFunc(sfactor, dfactor);
}

void nvgGlobalCompositeBlendFuncSeparate(NVGcontext* ctx, int srcRGB, int dstRGB, int srcAlpha, int dstAlpha)
{
    ctx->globalCompositeBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
}

NVGcolor nvgRGB(unsigned char r, unsigned char g, unsigned char b)
{
    return toC(nvg::rgb(r, g, b));
}

NVGcolor nvgRGBf(float r, float g, float b)
{
    return toC(nvg::rgbf(r, g, b));
}

NVGcolor nvgRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    return toC(nvg::rgba(r, g, b, a));
}

NVGcolor nvgRGBAf(float r, float g, float b, float a)
{
    return toC(nvg::rgbaf(r, g, b, a));
}

NVGcolor nvgLerpRGBA(NVGcolor c0, NVGcolor c1, float u)
{
    return toC(nvg::lerpRgba(toCpp(c0), toCpp(c1), u));
}

NVGcolor nvgTransRGBA(NVGcolor c0, unsigned char a)
{
    return toC(nvg::transRgba(toCpp(c0), a));
}

NVGcolor nvgTransRGBAf(NVGcolor c0, float a)
{
    return toC(nvg::transRgbaf(toCpp(c0), a));
}

NVGcolor nvgHSL(float h, float s, float l)
{
    return toC(nvg::hsl(h, s, l));
}

NVGcolor nvgHSLA(float h, float s, float l, unsigned char a)
{
    return toC(nvg::hsla(h, s, l, a));
}

void nvgSave(NVGcontext* ctx)
{
    ctx->save();
}

void nvgRestore(NVGcontext* ctx)
{
    ctx->restore();
}

void nvgReset(NVGcontext* ctx)
{
    ctx->reset();
}

NVGscissorBounds nvgCurrentScissor(NVGcontext* ctx)
{
    return toC(ctx->currentScissor());
}

void nvgShapeAntiAlias(NVGcontext* ctx, int enabled)
{
    ctx->shapeAntiAlias(enabled);
}

void nvgStrokeColor(NVGcontext* ctx, NVGcolor color)
{
    ctx->strokeColor(toCpp(color));
}

void nvgStrokePaint(NVGcontext* ctx, NVGpaint paint)
{
    ctx->strokePaint(toCpp(paint));
}

void nvgFillColor(NVGcontext* ctx, NVGcolor color)
{
    ctx->fillColor(toCpp(color));
}

void nvgFillPaint(NVGcontext* ctx, NVGpaint paint)
{
    ctx->fillPaint(toCpp(paint));
}

void nvgMiterLimit(NVGcontext* ctx, float limit)
{
    ctx->miterLimit(limit);
}

void nvgStrokeWidth(NVGcontext* ctx, float size)
{
    ctx->strokeWidth(size);
}

void nvgLineStyle(NVGcontext* ctx, int lineStyle)
{
    ctx->lineStyle(lineStyle);
}

void nvgLineCap(NVGcontext* ctx, int cap)
{
    ctx->lineCap(cap);
}

void nvgLineJoin(NVGcontext* ctx, int join)
{
    ctx->lineJoin(join);
}

void nvgGlobalAlpha(NVGcontext* ctx, float alpha)
{
    ctx->globalAlpha(alpha);
}

void nvgResetTransform(NVGcontext* ctx)
{
    ctx->resetTransform();
}

void nvgTransform(NVGcontext* ctx, float a, float b, float c, float d, float e, float f)
{
    ctx->transform(a, b, c, d, e, f);
}

void nvgTranslate(NVGcontext* ctx, float x, float y)
{
    ctx->translate(x, y);
}

void nvgRotate(NVGcontext* ctx, float angle)
{
    ctx->rotate(angle);
}

void nvgSkewX(NVGcontext* ctx, float angle)
{
    ctx->skewX(angle);
}

void nvgSkewY(NVGcontext* ctx, float angle)
{
    ctx->skewY(angle);
}

void nvgScale(NVGcontext* ctx, float x, float y)
{
    ctx->scale(x, y);
}

void nvgCurrentTransform(NVGcontext* ctx, float* xform)
{
    if (!xform) return;
    ctx->currentTransform(xform);
}

void nvgTransformIdentity(float* dst)
{
    nvg::transformIdentity(dst);
}

void nvgTransformTranslate(float* dst, float tx, float ty)
{
    nvg::transformTranslate(dst, tx, ty);
}

void nvgTransformScale(float* dst, float sx, float sy)
{
    nvg::transformScale(dst, sx, sy);
}

void nvgTransformRotate(float* dst, float a)
{
    nvg::transformRotate(dst, a);
}

void nvgTransformSkewX(float* dst, float a)
{
    nvg::transformSkewX(dst, a);
}

void nvgTransformSkewY(float* dst, float a)
{
    nvg::transformSkewY(dst, a);
}

void nvgTransformMultiply(float* dst, const float* src)
{
    nvg::transformMultiply(dst, src);
}

void nvgTransformPremultiply(float* dst, const float* src)
{
    nvg::transformPremultiply(dst, src);
}

int nvgTransformInverse(float* dst, const float* src)
{
    return nvg::transformInverse(dst, src);
}

void nvgTransformPoint(float* dstx, float* dsty, const float* xform, float srcx, float srcy)
{
    if (!dstx || !dsty) return;
    nvg::transformPoint(*dstx, *dsty, xform, srcx, srcy);
}

float nvgDegToRad(float deg)
{
    return nvg::degToRad(deg);
}

float nvgRadToDeg(float rad)
{
    return nvg::radToDeg(rad);
}

int nvgCreateImage(NVGcontext* ctx, const char* filename, int imageFlags)
{
    return ctx->createImage(filename, imageFlags);
}

int nvgCreateImageMem(NVGcontext* ctx, int imageFlags, unsigned char* data, int ndata)
{
    return ctx->createImageMem(imageFlags, data, ndata);
}

int nvgCreateImageRGBA(NVGcontext* ctx, int w, int h, int imageFlags, const unsigned char* data)
{
    return ctx->createImageRGBA(w, h, imageFlags, data);
}

void nvgUpdateImage(NVGcontext* ctx, int image, const unsigned char* data)
{
    ctx->updateImage(image, data);
}

void nvgImageSize(NVGcontext* ctx, int image, int* w, int* h)
{
    if (!w || !h) return;
    ctx->imageSize(image, *w, *h);
}

void nvgDeleteImage(NVGcontext* ctx, int image)
{
    ctx->deleteImage(image);
}

NVGpaint nvgLinearGradient(NVGcontext* ctx, float sx, float sy, float ex, float ey, NVGcolor icol, NVGcolor ocol)
{
    return toC(ctx->linearGradient(sx, sy, ex, ey, toCpp(icol), toCpp(ocol)));
}

NVGpaint nvgBoxGradient(NVGcontext* ctx, float x, float y, float w, float h, float r, float f, NVGcolor icol, NVGcolor ocol)
{
    return toC(ctx->boxGradient(x, y, w, h, r, f, toCpp(icol), toCpp(ocol)));
}

NVGpaint nvgRadialGradient(NVGcontext* ctx, float cx, float cy, float inr, float outr, NVGcolor icol, NVGcolor ocol)
{
    return toC(ctx->radialGradient(cx, cy, inr, outr, toCpp(icol), toCpp(ocol)));
}

NVGpaint nvgImagePattern(NVGcontext* ctx, float ox, float oy, float ex, float ey, float angle, int image, float alpha)
{
    return toC(ctx->imagePattern(ox, oy, ex, ey, angle, image, alpha));
}

void nvgScissor(NVGcontext* ctx, float x, float y, float w, float h)
{
    ctx->scissor(x, y, w, h);
}

void nvgIntersectScissor(NVGcontext* ctx, float x, float y, float w, float h)
{
    ctx->intersectScissor(x, y, w, h);
}

void nvgResetScissor(NVGcontext* ctx)
{
    ctx->resetScissor();
}

void nvgBeginPath(NVGcontext* ctx)
{
    ctx->beginPath();
}

void nvgMoveTo(NVGcontext* ctx, float x, float y)
{
    ctx->moveTo(x, y);
}

void nvgLineTo(NVGcontext* ctx, float x, float y)
{
    ctx->lineTo(x, y);
}

void nvgBezierTo(NVGcontext* ctx, float c1x, float c1y, float c2x, float c2y, float x, float y)
{
    ctx->bezierTo(c1x, c1y, c2x, c2y, x, y);
}

void nvgQuadTo(NVGcontext* ctx, float cx, float cy, float x, float y)
{
    ctx->quadTo(cx, cy, x, y);
}

void nvgArcTo(NVGcontext* ctx, float x1, float y1, float x2, float y2, float radius)
{
    ctx->arcTo(x1, y1, x2, y2, radius);
}

void nvgClosePath(NVGcontext* ctx)
{
    ctx->closePath();
}

void nvgPathWinding(NVGcontext* ctx, int dir)
{
    ctx->pathWinding(dir);
}

void nvgArc(NVGcontext* ctx, float cx, float cy, float r, float a0, float a1, int dir)
{
    ctx->arc(cx, cy, r, a0, a1, dir);
}

void nvgRect(NVGcontext* ctx, float x, float y, float w, float h)
{
    ctx->rect(x, y, w, h);
}

void nvgRoundedRect(NVGcontext* ctx, float x, float y, float w, float h, float r)
{
    ctx->roundedRect(x, y, w, h, r);
}

void nvgRoundedRectVarying(NVGcontext* ctx, float x, float y, float w, float h, float radTopLeft, float radTopRight, float radBottomRight, float radBottomLeft)
{
    ctx->roundedRectVarying(x, y, w, h, radTopLeft, radTopRight, radBottomRight, radBottomLeft);
}

void nvgEllipse(NVGcontext* ctx, float cx, float cy, float rx, float ry)
{
    ctx->ellipse(cx, cy, rx, ry);
}

void nvgCircle(NVGcontext* ctx, float cx, float cy, float r)
{
    ctx->circle(cx, cy, r);
}

void nvgFill(NVGcontext* ctx)
{
    ctx->fill();
}

void nvgStroke(NVGcontext* ctx)
{
    ctx->stroke();
}

int nvgCreateFont(NVGcontext* ctx, const char* name, const char* filename)
{
    return ctx->createFont(name, filename);
}

int nvgCreateFontAtIndex(NVGcontext* ctx, const char* name, const char* filename, const int fontIndex)
{
    return ctx->createFontAtIndex(name, filename, fontIndex);
}

int nvgCreateFontMem(NVGcontext* ctx, const char* name, unsigned char* data, int ndata, int freeData)
{
    return ctx->createFontMem(name, data, ndata, freeData);
}

int nvgCreateFontMemAtIndex(NVGcontext* ctx, const char* name, unsigned char* data, int ndata, int freeData, const int fontIndex)
{
    return ctx->createFontMemAtIndex(name, data, ndata, freeData, fontIndex);
}

int nvgFindFont(NVGcontext* ctx, const char* name)
{
    return ctx->findFont(name);
}

int nvgAddFallbackFontId(NVGcontext* ctx, int baseFont, int fallbackFont)
{
    return ctx->addFallbackFontId(baseFont, fallbackFont);
}

int nvgAddFallbackFont(NVGcontext* ctx, const char* baseFont, const char* fallbackFont)
{
    return ctx->addFallbackFont(baseFont, fallbackFont);
}

void nvgResetFallbackFontsId(NVGcontext* ctx, int baseFont)
{
    ctx->resetFallbackFontsId(baseFont);
}

void nvgResetFallbackFonts(NVGcontext* ctx, const char* baseFont)
{
    ctx->resetFallbackFonts(baseFont);
}

void nvgFontSize(NVGcontext* ctx, float size)
{
    ctx->fontSize(size);
}

void nvgFontBlur(NVGcontext* ctx, float blur)
{
    ctx->fontBlur(blur);
}

void nvgFontDilate(NVGcontext* ctx, float dilate)
{
    ctx->fontDilate(dilate);
}

void nvgTextLetterSpacing(NVGcontext* ctx, float spacing)
{
    ctx->textLetterSpacing(spacing);
}

void nvgTextLineHeight(NVGcontext* ctx, float lineHeight)
{
    ctx->textLineHeight(lineHeight);
}

void nvgTextAlign(NVGcontext* ctx, int align)
{
    ctx->textAlign(align);
}

void nvgFontFaceId(NVGcontext* ctx, int font)
{
    ctx->fontFaceId(font);
}

void nvgFontFace(NVGcontext* ctx, const char* font)
{
    ctx->fontFace(font);
}

int nvgGetFontFaceId(NVGcontext* ctx)
{
    return ctx->getFontFaceId();
}

float nvgGetFontSize(NVGcontext* ctx)
{
    return ctx->getFontSize();
}

float nvgGetStrokeWidth(NVGcontext* ctx)
{
    return ctx->getStrokeWidth();
}

int nvgGetTextAlign(NVGcontext* ctx)
{
    return ctx->getTextAlign();
}

float nvgText(NVGcontext* ctx, float x, float y, const char* string, const char* end)
{
    return ctx->text(x, y, string, end);
}

void nvgTextBox(NVGcontext* ctx, float x, float y, float breakRowWidth, const char* string, const char* end)
{
    ctx->textBox(x, y, breakRowWidth, string, end);
}

float nvgTextBounds(NVGcontext* ctx, float x, float y, const char* string, const char* end, float* bounds)
{
    return ctx->textBounds(x, y, string, end, bounds);
}

void nvgTextBoxBounds(NVGcontext* ctx, float x, float y, float breakRowWidth, const char* string, const char* end, float* bounds)
{
    ctx->textBoxBounds(x, y, breakRowWidth, string, end, bounds);
}

int nvgTextGlyphPositions(NVGcontext* ctx, float x, float y, const char* string, const char* end, NVGglyphPosition* positions, int maxPositions)
{
    return ctx->textGlyphPositions(x, y, string, end, reinterpret_cast<nvg::GlyphPosition*>(positions), maxPositions);
}

void nvgTextMetrics(NVGcontext* ctx, float* ascender, float* descender, float* lineh)
{
    ctx->textMetrics(ascender, descender, lineh);
}

int nvgTextBreakLines(NVGcontext* ctx, const char* string, const char* end, float breakRowWidth, NVGtextRow* rows, int maxRows, int skipSpaces)
{
    return ctx->textBreakLines(string, end, breakRowWidth, reinterpret_cast<nvg::TextRow*>(rows), maxRows, skipSpaces);
}


static_assert(sizeof(NVGrect) == sizeof(nvg::Rect));
static_assert(sizeof(NVGvertex) == sizeof(nvg::Vertex));
static_assert(static_cast<int>(NVG_PAINT_FILL) == static_cast<int>(nvg::PaintStyle::Fill) && static_cast<int>(NVG_PAINT_FILL_AND_STROKE) == static_cast<int>(nvg::PaintStyle::FillAndStroke));
static_assert(static_cast<int>(NVG_POINTS_POLYGON) == static_cast<int>(nvg::PointMode::Polygon));
static_assert(static_cast<int>(NVG_ANTIALIAS) == static_cast<int>(nvg::CreateFlags::Antialias) && static_cast<int>(NVG_STENCIL_STROKES) == static_cast<int>(nvg::CreateFlags::StencilStrokes) &&
              static_cast<int>(NVG_DEBUG) == static_cast<int>(nvg::CreateFlags::Debug));

void nvgDrawPaint(NVGcontext* ctx, NVGpaint paint)
{
    ctx->drawPaint(toCpp(paint));
}

void nvgDrawColor(NVGcontext* ctx, NVGcolor color)
{
    ctx->drawColor(toCpp(color));
}

void nvgClear(NVGcontext* ctx, NVGcolor color)
{
    ctx->clear(toCpp(color));
}

void nvgDrawLine(NVGcontext* ctx, float x0, float y0, float x1, float y1)
{
    ctx->drawLine(x0, y0, x1, y1);
}

void nvgDrawPoints(NVGcontext* ctx, int mode, const float* pts, int count)
{
    ctx->drawPoints(toPointMode(mode), pts, count);
}

void nvgDrawPoint(NVGcontext* ctx, float x, float y)
{
    ctx->drawPoint(x, y);
}

void nvgDrawPolygon(NVGcontext* ctx, const float* pts, int count, int style)
{
    ctx->drawPolygon(pts, count, toPaintStyle(style));
}

void nvgDrawRect(NVGcontext* ctx, NVGrect r, int style)
{
    ctx->drawRect(toCpp(r), toPaintStyle(style));
}

void nvgDrawRoundRect(NVGcontext* ctx, NVGrect r, float radius, int style)
{
    ctx->drawRoundRect(toCpp(r), radius, toPaintStyle(style));
}

void nvgDrawRoundRectVarying(NVGcontext* ctx, NVGrect r, float radTopLeft, float radTopRight, float radBottomRight, float radBottomLeft, int style)
{
    ctx->drawRoundRectVarying(toCpp(r), radTopLeft, radTopRight, radBottomRight, radBottomLeft, toPaintStyle(style));
}

void nvgDrawDRRect(NVGcontext* ctx, NVGrect outer, float outerRadius, NVGrect inner, float innerRadius)
{
    ctx->drawDRRect(toCpp(outer), outerRadius, toCpp(inner), innerRadius);
}

void nvgDrawCircle(NVGcontext* ctx, float cx, float cy, float r, int style)
{
    ctx->drawCircle(cx, cy, r, toPaintStyle(style));
}

void nvgDrawOval(NVGcontext* ctx, NVGrect oval, int style)
{
    ctx->drawOval(toCpp(oval), toPaintStyle(style));
}

void nvgDrawArc(NVGcontext* ctx, float cx, float cy, float r, float startAngle, float sweepAngle, int useCenter, int style)
{
    ctx->drawArc(cx, cy, r, startAngle, sweepAngle, useCenter != 0, toPaintStyle(style));
}

void nvgDrawImage(NVGcontext* ctx, int image, float x, float y, float alpha)
{
    ctx->drawImage(image, x, y, alpha);
}

void nvgDrawImageRect(NVGcontext* ctx, int image, const NVGrect* src, NVGrect dst, float alpha)
{
    if (src != nullptr)
        ctx->drawImageRect(image, toCpp(*src), toCpp(dst), alpha);
    else
        ctx->drawImageRect(image, toCpp(dst), alpha);
}

void nvgDrawImageNine(NVGcontext* ctx, int image, NVGrect center, NVGrect dst, float alpha)
{
    ctx->drawImageNine(image, toCpp(center), toCpp(dst), alpha);
}

void nvgDrawVertices(NVGcontext* ctx, const NVGvertex* verts, int nverts, int image, float alpha)
{
    ctx->drawVertices(reinterpret_cast<const nvg::Vertex*>(verts), nverts, image, alpha);
}

int nvgGetImageTextureId(NVGcontext* ctx, int handle)
{
    const nvg::Params& p = ctx->internalParams();
    if (!p.renderGetImageTextureId) return 0;
    return p.renderGetImageTextureId(p.userPtr, handle);
}

NVGcontext* nvgCreateInternal(NVGparams* params)
{
    if (!params) return nullptr;
    // C callers cannot catch exceptions; report construction failure (e.g. renderCreate returning 0) as NULL.
    try {
        return new NVGcontext(copyParams(*params));
    } catch (const std::exception&) {
        return nullptr;
    }
}

void nvgDeleteInternal(NVGcontext* ctx)
{
    delete ctx;
}

NVGparams* nvgInternalParams(NVGcontext* ctx)
{
    return reinterpret_cast<NVGparams*>(const_cast<nvg::Params*>(&ctx->internalParams()));
}

void nvgDebugDumpPathCache(NVGcontext* ctx)
{
    ctx->debugDumpPathCache();
}

}  // extern "C"
