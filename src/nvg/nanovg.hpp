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

#pragma once
#include <functional>
#include <type_traits>
#include <memory>

#ifndef UNUSED
#define UNUSED(v) (void)(v)
#endif

namespace nvg
{

struct ContextImpl;

template <typename E>
    requires std::is_enum_v<E>
[[nodiscard]] constexpr std::underlying_type_t<E> to_underlying(E e) noexcept
{
    return static_cast<std::underlying_type_t<E>>(e);
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4201)  // nonstandard extension used : nameless struct/union
#endif

struct Color {
    union {
        float rgba[4];
        struct {
            float r, g, b, a;
        };
    };
};

constexpr float nvgPi = 3.14159265358979323846264338327f;
// Maximum number of values accepted by Context::strokeDashArray (an odd-length array is doubled, as in SVG).
constexpr int maxStrokeDashes = 16;

struct Paint {
    float xform[6];
    float extent[2];
    float radius;
    float feather;
    Color innerColor;
    Color outerColor;
    int image;
};

enum class Winding : int {
    CCW = 1,  // Winding for solid shapes
    CW = 2,   // Winding for holes
};

enum class Solidity : int {
    Solid = 1,  // CCW
    Hole = 2,   // CW
};

enum class LineStyle : int { Solid = 1, Dashed = 2, Dotted = 3, Glow = 4 };

enum class LineCap : int {
    Butt = 0,
    Round = 1,
    Square = 2,
    Bevel = 3,
    Miter = 4,
};

enum class Align : int {
    Left = 1 << 0,
    Center = 1 << 1,
    Right = 1 << 2,
    Top = 1 << 3,
    Middle = 1 << 4,
    MiddleAscent = 1 << 5,
    Bottom = 1 << 6,
    Baseline = 1 << 7,
};

inline constexpr Align operator|(Align a, Align b) noexcept
{
    return static_cast<Align>(to_underlying(a) | to_underlying(b));
}

inline constexpr int operator&(int a, Align b) noexcept
{
    return a & to_underlying(b);
}

enum class BlendFactor : int {
    Zero = 1 << 0,
    One = 1 << 1,
    SrcColor = 1 << 2,
    OneMinusSrcColor = 1 << 3,
    DstColor = 1 << 4,
    OneMinusDstColor = 1 << 5,
    SrcAlpha = 1 << 6,
    OneMinusSrcAlpha = 1 << 7,
    DstAlpha = 1 << 8,
    OneMinusDstAlpha = 1 << 9,
    SrcAlphaSaturate = 1 << 10,
};

enum class CompositeOperation : int {
    SourceOver,
    SourceIn,
    SourceOut,
    Atop,
    DestinationOver,
    DestinationIn,
    DestinationOut,
    DestinationAtop,
    Lighter,
    Copy,
    Xor,
};

struct CompositeOperationState {
    int srcRGB;
    int dstRGB;
    int srcAlpha;
    int dstAlpha;
};

// Axis-aligned rectangle in local (pre-transform) coordinates.
struct Rect {
    float x, y, w, h;
};

// How a draw* call renders its shape, analogous to SkPaint::Style.
// Fill uses the current fill paint, Stroke uses the current stroke paint/width/caps/joins.
enum class PaintStyle : int {
    Fill = 0,
    Stroke = 1,
    FillAndStroke = 2,
};

// Interpretation of the point list passed to Context::drawPoints, analogous to SkCanvas::PointMode.
enum class PointMode : int {
    Points = 0,   // Each point is drawn as a dot of strokeWidth diameter (round dot for LineCap::Round, else square).
    Lines = 1,    // Each consecutive pair of points is drawn as an independent line segment.
    Polygon = 2,  // The points are drawn as one connected, open polyline.
};

// Element drawn by Context::drawGrid.
enum class GridShape : int {
    Lines = 0,    // The (cols + 1) vertical and (rows + 1) horizontal lines bounding the cells; size is the line thickness.
    Circles = 1,  // One circle centered in each cell; size is the radius.
    Squares = 2,  // One axis-aligned square centered in each cell; size is the half side length (a "radius").
};

// Built-in shapes for stroke markers (see Context::markerStart). Each is defined in a unit box centered
// on the path vertex, +x pointing along the marker's orientation, and scaled by Marker::size.
enum class MarkerShape : int {
    None = 0,     // No marker.
    Arrow = 1,    // Triangle from x = -0.5 (base, 1 unit wide) to x = +0.5 (tip): points along the path.
    Circle = 2,   // Circle of diameter 1.
    Square = 3,   // Square of side 1.
    Diamond = 4,  // Square of diagonal 1, rotated 45 degrees.
    Bar = 5,      // Tick across the path: 1 unit long and one stroke width thick.
    Custom = 6,   // Drawn by Marker::draw.
};

// How a marker is rotated at its vertex, like the SVG marker orient attribute.
enum class MarkerOrient : int {
    Auto = 0,              // Along the path direction (the bisector of the incoming and outgoing segments).
    AutoStartReverse = 1,  // Like Auto, but the start marker points backwards (double-headed arrows).
    Fixed = 2,             // Marker::angle radians, in the local coordinate system of the stroked path.
};

class Context;
struct TextPathGeometry;  // flattened path used internally by Context::textOnPath

// A stroke marker, drawn at path vertices by Context::stroke (SVG marker-start / marker-mid /
// marker-end with markerUnits="strokeWidth"). The marker frame has its origin on the vertex and +x along
// the orientation, scaled so that 1 unit = size x the stroke width.
struct Marker {
    MarkerShape shape = MarkerShape::None;
    float size = 3.0f;
    MarkerOrient orient = MarkerOrient::Auto;
    float angle = 0.0f;  // Used with MarkerOrient::Fixed.
    // For MarkerShape::Custom: called with the context set up in the marker frame, the fill paint set to
    // the stroke paint and the stroke width set to the stroked path's width. It may build and fill/stroke
    // any paths; the caller's path and state are restored afterwards. Markers do not nest.
    std::function<void(Context& ctx)> draw;
};

// How text is colored by the fill paint (Context::textFill).
enum class TextFill : int {
    Color = 0,  // Glyphs take the fill paint's inner color only, as in classic NanoVG (the default).
    Paint = 1,  // Glyphs show the whole fill paint: a linear, radial or box gradient is evaluated across
                // them as if a rect under the text were filled with it, in the same coordinates as shapes.
};

// Which side of the path text is drawn on by Context::textOnPath, like the SVG textPath side attribute.
enum class TextPathSide : int {
    Left = 0,   // Along the path's direction, glyphs upright on its left (the default).
    Right = 1,  // The path is followed in reverse, so text runs the other way on the opposite side.
};

// How glyphs follow the path in Context::textOnPath, like the SVG textPath method attribute.
enum class TextPathMethod : int {
    Align = 0,    // Each glyph stays rigid and is rotated to the path's direction at its midpoint.
    Stretch = 1,  // Each glyph is cut into thin slices that bend with the path, so it warps along curves.
};

// Layout of Context::textOnPath, mirroring the SVG <textPath> attributes.
struct TextPathOptions {
    // Distance along the path where the text's anchor point sits (SVG startOffset), in local units, or
    // a percentage of the path's length (0..100) when startOffsetPercent is set.
    float startOffset = 0.0f;
    bool startOffsetPercent = false;
    TextPathSide side = TextPathSide::Left;
    TextPathMethod method = TextPathMethod::Align;
    // Moves the glyphs away from the path along its normal, in local units (positive = towards the
    // glyphs' "down", like SVG dy / a negative baseline-shift).
    float baselineShift = 0.0f;
};

struct GlyphPosition {
    const char* str;   // Position of the glyph in the input string.
    float x;           // The x-coordinate of the logical glyph position.
    float minx, maxx;  // The bounds of the glyph shape.
};

struct TextRow {
    const char* start;  // Pointer to the input text where the row starts.
    const char* end;    // Pointer to the input text where the row ends (one past the last character).
    const char* next;   // Pointer to the beginning of the next row.
    float width;        // Logical width of the row.
    float minx, maxx;   // Actual bounds of the row. Logical with and bounds can differ because of kerning and some parts over extending.
};

enum class ImageFlags : int {
    GenerateMipmaps = 1 << 0,
    RepeatX = 1 << 1,
    RepeatY = 1 << 2,
    Flipy = 1 << 3,
    Premultiplied = 1 << 4,
    Nearest = 1 << 5,
};

inline constexpr ImageFlags operator|(ImageFlags a, ImageFlags b) noexcept
{
    return static_cast<ImageFlags>(to_underlying(a) | to_underlying(b));
}

inline constexpr int operator&(int a, ImageFlags b) noexcept
{
    return a & to_underlying(b);
}

// Flags accepted by the backend constructors (nvg::createGL, nvg::createVk).
enum class CreateFlags : int {
    Antialias = 1 << 0,       // Geometry based anti-aliasing (may not be needed when using MSAA).
    StencilStrokes = 1 << 1,  // Render strokes through the stencil buffer so overlapping parts blend only once.
    Debug = 1 << 2,           // Extra error checking in the backend.
};

inline constexpr CreateFlags operator|(CreateFlags a, CreateFlags b) noexcept
{
    return static_cast<CreateFlags>(to_underlying(a) | to_underlying(b));
}

inline constexpr int operator&(int a, CreateFlags b) noexcept
{
    return a & to_underlying(b);
}

// Internal Render API
//
enum class Texture : int {
    Alpha = 0x01,
    Rgba = 0x02,
};

struct Scissor {
    float xform[6];
    float extent[2];
};

struct ScissorBounds {
    float x;
    float y;
    float w;
    float h;
};

struct Vertex {
    float x, y, u, v, s, t;
};

struct Path {
    int first;
    int count;
    int reversed;
    unsigned char closed;
    int nbevel;
    Vertex* fill;
    int nfill;
    Vertex* stroke;
    int nstroke;
    int winding;
    int convex;
};

// Analytic shape handed to Params::renderShape: the backend draws the given quad and computes the
// shape's coverage per pixel in the fragment shader (instead of tessellating it), multiplying it into
// the paint. Mirrored by NVGshape in the stable C API.
enum class ShapeType : int {
    None = 0,
    GridLines = 1,    // params: x, y, w, h, cols, rows, line thickness
    GridCircles = 2,  // params: x, y, w, h, cols, rows, radius
    GridSquares = 3,  // params: x, y, w, h, cols, rows, half side
    Arrow = 4,        // params: length, stalk half width, head half width, head length (tail at the origin, pointing along +x)
};

struct Shape {
    int type;               // ShapeType
    float strokeHalfWidth;  // 0 fills the shape; > 0 covers a band of this half width centered on its outline
    float aaWidth;          // antialiasing ramp width in shape units (a tiny value gives hard edges)
    float xform[6];         // shape space -> device space
    float params[8];        // per ShapeType, see above
};

struct Params {
    void* userPtr;
    int edgeAntiAlias;
    int (*renderCreate)(void* uptr);
    int (*renderCreateTexture)(void* uptr, int type, int w, int h, int imageFlags, const unsigned char* data);
    int (*renderDeleteTexture)(void* uptr, int image);
    int (*renderUpdateTexture)(void* uptr, int image, int x, int y, int w, int h, const unsigned char* data);
    int (*renderGetTextureSize)(void* uptr, int image, int* w, int* h);
    int (*renderGetImageTextureId)(void* uptr, int handle);
    void (*renderViewport)(void* uptr, float width, float height, float devicePixelRatio);
    void (*renderCancel)(void* uptr);
    void (*renderFlush)(void* uptr);
    void (*renderFill)(void* uptr, Paint* paint, CompositeOperationState compositeOperation, Scissor* scissor, float fringe, const float* bounds, const Path* paths, int npaths);
    void (*renderStroke)(void* uptr, Paint* paint, CompositeOperationState compositeOperation, Scissor* scissor, float fringe, float strokeWidth, int lineStyle, const Path* paths, int npaths);
    void (*renderTriangles)(void* uptr, Paint* paint, CompositeOperationState compositeOperation, Scissor* scissor, const Vertex* verts, int nverts, float fringe);
    void (*renderDelete)(void* uptr);
    // Optional (may be null): draws verts (device-space triangles) with paint, scaled by the coverage of
    // shape. Context falls back to tessellated geometry when it is not set.
    void (*renderShape)(void* uptr, Paint* paint, CompositeOperationState compositeOperation, Scissor* scissor, float fringe, const Shape* shape, const Vertex* verts, int nverts);
    // Optional (may be null): draws verts (device-space triangles) colored by paint's gradient (paint->image
    // is 0), evaluated per pixel as in renderFill, and scaled by the coverage in the alpha texture mask
    // sampled at the vertices' u, v. Used for gradient-filled text (TextFill::Paint); without it such text
    // falls back to the paint's inner color.
    void (*renderMaskedPaint)(void* uptr, Paint* paint, int mask, CompositeOperationState compositeOperation, Scissor* scissor, const Vertex* verts, int nverts, float fringe);
    // Optional (may be null): draws paths' stroke geometry like renderStroke (solid line style) with a
    // per-pixel stroke effect selected by effect[3]:
    //  > 0  Inner shadow: the paint is scaled by one minus the coverage of the stroke band moved by
    //       (effect[0], effect[1]) and blurred by effect[2], effect[3] being the band's half width
    //       including the antialiasing ramp, all in framebuffer pixels (the vertices are as from
    //       renderStroke; ftcoord.x runs 0..1 across the band).
    //  < 0  Gaussian-blurred stroke: coverage is a Gaussian blur (sigma effect[1]) of the stroke of half
    //       width effect[0], in vertex units. The vertices carry distance coordinates instead of the usual
    //       ones: u = signed distance across the center line, v = distance along from the path's start,
    //       s = distance along to its end (very large on closed paths). effect[3] = -1: butt / square
    //       ends (a box: blurred across times blurred along), -2: round ends (a capsule).
    // Context falls back to plain strokes (blur as a linear antialiasing ramp, no inner shadow) without it.
    void (*renderStrokeEffect)(void* uptr, Paint* paint, CompositeOperationState compositeOperation, Scissor* scissor, float fringe, float strokeWidth, const float* effect, const Path* paths,
                               int npaths);
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

class Context
{
public:
    explicit Context(const Params& params);
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

    ContextImpl& operator*() noexcept;
    const ContextImpl& operator*() const noexcept;
    ContextImpl* operator->() noexcept;
    const ContextImpl* operator->() const noexcept;

    void beginFrame(float windowWidth, float windowHeight, float devicePixelRatio);
    void cancelFrame();
    void endFrame();
    void globalCompositeOperation(int op);
    void globalCompositeBlendFunc(int sfactor, int dfactor);
    void globalCompositeBlendFuncSeparate(int srcRGB, int dstRGB, int srcAlpha, int dstAlpha);
    void save();
    void restore();
    void reset();
    const ScissorBounds& currentScissor() const;
    void shapeAntiAlias(int enabled);
    void strokeColor(const Color& color);
    void strokePaint(const Paint& paint);
    void fillColor(const Color& color);
    void fillPaint(const Paint& paint);
    void miterLimit(float limit);
    void strokeWidth(float size);
    void lineStyle(int lineStyle);
    // SVG-style dashing for every stroke (stroke-dasharray / stroke-dashoffset). dashes alternates dash
    // and gap lengths in local units, starting with a dash; an odd count is repeated to make it even, as
    // in SVG. nullptr, count <= 0, a negative or non-finite value, or an all-zero array turns dashing
    // off (solid strokes). Values past maxStrokeDashes are ignored. The pattern restarts on each subpath,
    // and line caps are applied to both ends of every dash (so "0 8" with round caps draws dots).
    void strokeDashArray(const float* dashes, int count);
    // Same, parsed from an SVG stroke-dasharray value: lengths separated by commas and/or whitespace,
    // each optionally suffixed with "px", e.g. "6 3", "4,2,1,2" or "none". An invalid string turns dashing off.
    void strokeDashArray(const char* dashArray);
    // Shifts the start of the dash pattern along each subpath, like SVG stroke-dashoffset (may be negative).
    void strokeDashOffset(float offset);
    // Markers drawn by every stroke() after the stroke itself, like SVG marker-start / marker-mid /
    // marker-end: start at the path's first vertex, end at its last, mid at every vertex in between
    // (including the ends of inner subpaths). Vertices are the end points of the path commands: an arc
    // or ellipse counts as one segment, curves are not split at their tessellation points, and
    // zero-length segments add no vertex. Markers are filled with the stroke paint by default and saved
    // and restored with the rest of the state. Pass Marker{} to remove one. The Skia-style draw calls
    // that stroke a path (drawLine, drawRect, ...) draw markers too; drawArrow and drawGrid do not.
    void markerStart(const Marker& marker);
    void markerMid(const Marker& marker);
    void markerEnd(const Marker& marker);
    // Sets all three, like the SVG marker shorthand.
    void markers(const Marker& marker);
    // Stroke effects matching the text ones, applied by every stroke() (and the Skia-style calls that
    // stroke) in local units, so they follow the transform. All are saved and restored with the state.
    // Soft shadow under the stroke, like textOuterShadow: the stroke again in color, offset by
    // (offsetX, offsetY) and blurred by blur. No offset and a bright color make a glow. Zero alpha = off.
    void strokeOuterShadow(float offsetX, float offsetY, float blur, const Color& color);
    // Inner shadow over the stroke, like textInnerShadow: color inside the stroke wherever a copy of it,
    // offset by (offsetX, offsetY) and blurred by blur, does not cover it, so an offset down-right shades
    // the band's top-left edges. Needs Params::renderStrokeEffect (the bundled GL and Vulkan
    // backends; GLES2 also needs GL_OES_standard_derivatives). Zero alpha = off.
    void strokeInnerShadow(float offsetX, float offsetY, float blur, const Color& color);
    // An outline of the given width around the stroke, drawn under it in color (the stroke grown by
    // width on each side), like an outlined text style. Zero width or alpha = off.
    void strokeOutline(float width, const Color& color);
    // Blurs the stroke's own edges (and its outline) by blur, like fontBlur. 0 = sharp (the default).
    // Blurs (here and in the outer shadow) are Gaussian, measured from the true distance to the stroke,
    // so round caps and joins stay round, through Params::renderStrokeEffect; without it they fall back
    // to a linear ramp. They need an antialiased context (CreateFlags::Antialias).
    void strokeBlur(float blur);
    void lineCap(int cap);
    void lineJoin(int join);
    void globalAlpha(float alpha);
    void resetTransform();
    void transform(float a, float b, float c, float d, float e, float f);
    void translate(float x, float y);
    void rotate(float angle);
    void skewX(float angle);
    void skewY(float angle);
    void scale(float x, float y);
    void currentTransform(float* xform) const;
    int createImage(const char* filename, int imageFlags);
    int createImageMem(int imageFlags, unsigned char* data, int ndata);
    int createImageRGBA(int w, int h, int imageFlags, const unsigned char* data);
    void updateImage(int image, const unsigned char* data);
    void imageSize(int image, int& w, int& h) const;
    void deleteImage(int image);
    Paint linearGradient(float sx, float sy, float ex, float ey, const Color& icol, const Color& ocol) const;
    Paint boxGradient(float x, float y, float w, float h, float r, float f, const Color& icol, const Color& ocol) const;
    Paint radialGradient(float cx, float cy, float inr, float outr, const Color& icol, const Color& ocol) const;
    Paint imagePattern(float ox, float oy, float ex, float ey, float angle, int image, float alpha) const;
    void scissor(float x, float y, float w, float h);
    void intersectScissor(float x, float y, float w, float h);
    void resetScissor();
    void beginPath();
    void moveTo(float x, float y);
    void lineTo(float x, float y);
    void bezierTo(float c1x, float c1y, float c2x, float c2y, float x, float y);
    void quadTo(float cx, float cy, float x, float y);
    void arcTo(float x1, float y1, float x2, float y2, float radius);
    void closePath();
    void pathWinding(int dir);
    void arc(float cx, float cy, float r, float a0, float a1, int dir);
    // Elliptical arc from its center: radii rx, ry, the ellipse rotated by rotation (radians), from
    // parametric angle a0 to a1 in direction dir (like arc, and like the canvas ellipse() angles).
    // Connects to the current path with a line, or starts a new subpath if the path is empty.
    void ellipticalArc(float cx, float cy, float rx, float ry, float rotation, float a0, float a1, int dir);
    // Elliptical arc from the current point to (x, y), with the parameters of the SVG path "A" command:
    // radii rx, ry, the ellipse's x axis rotated by xAxisRotation (radians, not degrees as in SVG), and
    // the largeArc / sweep flags choosing one of the four candidate arcs (sweep = clockwise on a y-down
    // screen). Radii too small to span the endpoints are scaled up, a zero radius draws a straight line,
    // and an endpoint equal to the current point draws nothing, all as in SVG. Starts a new subpath at
    // (x, y) if the path is empty.
    void ellipticalArcTo(float rx, float ry, float xAxisRotation, bool largeArc, bool sweep, float x, float y);
    void rect(float x, float y, float w, float h);
    void roundedRect(float x, float y, float w, float h, float r);
    // Rounded rect with elliptical corners of radii rx (horizontal) and ry (vertical), like an SVG
    // <rect rx ry>. Each radius is clamped to half the rect's width / height.
    void roundedRect(float x, float y, float w, float h, float rx, float ry);
    void roundedRectVarying(float x, float y, float w, float h, float radTopLeft, float radTopRight, float radBottomRight, float radBottomLeft);
    // Per-corner elliptical radii (horizontal, vertical), clockwise from the top left.
    void roundedRectVarying(float x, float y, float w, float h, float rxTopLeft, float ryTopLeft, float rxTopRight, float ryTopRight, float rxBottomRight, float ryBottomRight,
                            float rxBottomLeft, float ryBottomLeft);
    void ellipse(float cx, float cy, float rx, float ry);
    void circle(float cx, float cy, float r);
    void fill();
    void stroke();

    // Immediate-mode draw calls modeled after SkCanvas. Each call builds its own path and renders it
    // with the current state (paints, stroke width, caps, joins, transform, scissor, alpha, composite op).
    // Note: they replace the current path, so call them outside of a beginPath()/fill() sequence.

    // Fills the whole viewport (respecting scissor) with a paint or color, like SkCanvas::drawPaint/drawColor.
    void drawPaint(const Paint& paint);
    void drawColor(const Color& color);
    // Replaces the pixels in the viewport (respecting scissor) with color, ignoring blending, like SkCanvas::clear.
    void clear(const Color& color);
    void drawLine(float x0, float y0, float x1, float y1);
    // Arrow from the tail (x0, y0) to the tip (x1, y1), built as one closed outline: a stalk of
    // stalkWidth topped by a triangular head headLength long (along the arrow) and headWidth wide
    // (across it). headLength is clamped to the arrow's length and headWidth to at least stalkWidth.
    // A stalkWidth of 0 gives a bare triangular head on a zero-width stalk (stroke it to see the line).
    // With a backend that implements Params::renderShape the arrow is one quad whose coverage the
    // fragment shader computes; strokes take that path when undashed and with round joins.
    void drawArrow(float x0, float y0, float x1, float y1, float stalkWidth, float headWidth, float headLength, PaintStyle style = PaintStyle::Fill);
    // pts is an array of count (x, y) pairs, i.e. 2 * count floats.
    // A cols x rows grid laid out over r. Lines are the union of bars of thickness size centered on the
    // cell boundaries (Stroke outlines that union), so crossings do not double-blend. With a backend that
    // implements Params::renderShape the grid is one quad whose coverage the fragment shader computes, so
    // the cost does not grow with cols x rows; otherwise it falls back to a tessellated path. Strokes
    // take the shader path for plain (undashed, LineStyle::Solid) strokes with miter joins for lines and
    // squares; circles and squares also need to stay inside their cell.
    void drawGrid(const Rect& r, int cols, int rows, GridShape shape, float size, PaintStyle style = PaintStyle::Fill);
    void drawPoints(PointMode mode, const float* pts, int count);
    void drawPoint(float x, float y);
    void drawPolygon(const float* pts, int count, PaintStyle style = PaintStyle::Fill);
    void drawRect(const Rect& r, PaintStyle style = PaintStyle::Fill);
    void drawRoundRect(const Rect& r, float radius, PaintStyle style = PaintStyle::Fill);
    // Rounded rect with elliptical corners (rx horizontal, ry vertical), like SkCanvas::drawRoundRect.
    void drawRoundRect(const Rect& r, float rx, float ry, PaintStyle style = PaintStyle::Fill);
    void drawRoundRectVarying(const Rect& r, float radTopLeft, float radTopRight, float radBottomRight, float radBottomLeft, PaintStyle style = PaintStyle::Fill);
    // Fills the area between two rounded rects (outer minus inner), like SkCanvas::drawDRRect.
    void drawDRRect(const Rect& outer, float outerRadius, const Rect& inner, float innerRadius);
    void drawCircle(float cx, float cy, float r, PaintStyle style = PaintStyle::Fill);
    // Ellipse inscribed in the rect, like SkCanvas::drawOval.
    void drawOval(const Rect& oval, PaintStyle style = PaintStyle::Fill);
    // Circular arc, like SkCanvas::drawArc. Angles in radians; positive sweep is clockwise (y-down).
    // With useCenter the arc is closed through the center (a wedge); otherwise Fill draws the chord segment.
    void drawArc(float cx, float cy, float r, float startAngle, float sweepAngle, bool useCenter, PaintStyle style = PaintStyle::Fill);
    // Draws the full image with its top-left corner at (x, y), like SkCanvas::drawImage.
    void drawImage(int image, float x, float y, float alpha = 1.0f);
    // Draws the src sub-rect of the image (in image pixels) scaled into dst, like SkCanvas::drawImageRect.
    void drawImageRect(int image, const Rect& src, const Rect& dst, float alpha = 1.0f);
    void drawImageRect(int image, const Rect& dst, float alpha = 1.0f);
    // Nine-patch: corners outside center (image pixels) keep their size, edges stretch along one axis,
    // and the center stretches in both, like SkCanvas::drawImageNine.
    void drawImageNine(int image, const Rect& center, const Rect& dst, float alpha = 1.0f);
    // Draws textured triangles (3 vertices per triangle). Vertex x/y are local coordinates and u/v are
    // normalized texture coordinates in [0, 1]; s/t are ignored. Like SkCanvas::drawVertices in triangle mode.
    void drawVertices(const Vertex* verts, int nverts, int image, float alpha = 1.0f);

private:
    // Appends an elliptical arc as cubic segments (at most 90 degrees each) from parametric angle a0
    // through the signed sweep da. With connect the first point is joined by a line (or starts the
    // path); otherwise the arc must start at the current point.
    void appendEllipticalArc(float cx, float cy, float rx, float ry, float rotation, float a0, float da, bool connect);
    void strokePath(bool withMarkers);
    // One stroke pass of the current path: paint, the stroke widened by extraWidth on each side, edges
    // blurred by blur, offset by (dx, dy) (local units); with innerShadow (device px: offset x, y, blur)
    // it goes through Params::renderStrokeEffect (the pass fills in the band's half width).
    void strokePass(const Paint& paint, float extraWidth, float blur, float dx, float dy, const float* innerShadow);
    void textInnerShadowPass(float x, float y, const char* string, const char* end);
    float textOnPathPass(const TextPathGeometry& path, const char* string, const char* end, const TextPathOptions& options, float shiftX, float shiftY);
    void drawMarkers();
    // Draws one shader-evaluated shape pass (fill or stroke) through Params::renderShape. Returns false,
    // drawing nothing, when the backend has no renderShape or the shader cannot match the geometry path.
    bool renderShapePass(ShapeType type, const float* params, const float* frame, const float* bounds, bool stroke);

public:
    int createFont(const char* name, const char* filename);
    int createFontAtIndex(const char* name, const char* filename, const int fontIndex);
    int createFontMem(const char* name, unsigned char* data, int ndata, int freeData);
    int createFontMemAtIndex(const char* name, unsigned char* data, int ndata, int freeData, const int fontIndex);
    int findFont(const char* name) const;
    int addFallbackFontId(int baseFont, int fallbackFont);
    int addFallbackFont(const char* baseFont, const char* fallbackFont);
    void resetFallbackFontsId(int baseFont);
    void resetFallbackFonts(const char* baseFont);
    void fontSize(float size);
    void fontBlur(float blur);
    // Soft outer shadow drawn under all text from text() and textBox(), like CSS text-shadow: the glyphs
    // again in color, offset by (offsetX, offsetY) and blurred by blur (both in local units, so they
    // follow the transform and scale with it). The blur is the font atlas blur, so it adds to fontBlur and
    // is capped at 20 device pixels; each distinct blur value caches its own glyphs, so avoid animating it
    // continuously. A color with zero alpha turns the shadow off (the default). Saved with the state.
    void textOuterShadow(float offsetX, float offsetY, float blur, const Color& color);
    // Soft inner shadow drawn over all text from text() and textBox(), like an inset shadow in design
    // tools: color inside the glyphs wherever a copy of them, offset by (offsetX, offsetY) and blurred by
    // blur, does not cover them, so an offset down-right shades the top-left inner edges. The offset copy
    // is of the whole string, so it may shade neighbouring letters too. Offset and blur are in local units
    // (the blur matches textOuterShadow's but takes fractional values and is capped at 64 device pixels).
    // Each call builds the mask on the CPU and draws it as one textured quad, so it works on any backend;
    // the cost grows with the text's pixel area. Zero alpha turns it off (the default).
    void textInnerShadow(float offsetX, float offsetY, float blur, const Color& color);
    void fontDilate(float dilate);
    // Selects how text() / textBox() / textOnPath() glyphs are colored by the fill paint. With
    // TextFill::Paint a gradient fill (linearGradient, radialGradient, boxGradient) runs across the glyphs
    // exactly as it would across a rect filled under them, so build it from the text's bounds (textBounds)
    // to span the string. Image patterns keep the classic behaviour (their tint), as do backends without
    // Params::renderMaskedPaint. Saved with the state; the default is TextFill::Color.
    void textFill(TextFill mode);
    void textLetterSpacing(float spacing);
    void textLineHeight(float lineHeight);
    void textAlign(int align);
    void fontFaceId(int font);
    void fontFace(const char* font);
    int getFontFaceId() const;
    float getFontSize() const;
    float getStrokeWidth() const;
    int getTextAlign() const;
    float text(float x, float y, const char* string, const char* end);
    void textBox(float x, float y, float breakRowWidth, const char* string, const char* end);
    // Draws text along the current path, like SVG <textPath>: every glyph is placed at its advance
    // midpoint's distance along the path and rotated to follow the path's direction there, so the text
    // reorients as the curve turns. The path is used as built (beginPath, moveTo, bezierTo, arc, ...),
    // is not drawn, and stays the current path, so it can still be filled or stroked afterwards.
    // The text is laid out as by text() from the path's start: textAlign's horizontal part is the SVG
    // text-anchor around startOffset (Left = start, Center = middle, Right = end) and its vertical part
    // places the glyphs relative to the path (Baseline puts the baseline on it, Middle centers them).
    // Font, size, letter spacing, blur, dilate, fill paint and textOuterShadow apply as for text();
    // textInnerShadow does not. Subpaths are followed one after the other (a moveTo jumps without adding
    // length). As in SVG, glyphs whose midpoint falls off either end of the path are not drawn, except
    // on a path made of one closed subpath, where positions wrap around. Returns the distance along the
    // path, in local units, just past the last glyph's advance.
    float textOnPath(const char* string, const char* end, const TextPathOptions& options = {});
    float textBounds(float x, float y, const char* string, const char* end, float* bounds);
    void textBoxBounds(float x, float y, float breakRowWidth, const char* string, const char* end, float* bounds);
    int textGlyphPositions(float x, float y, const char* string, const char* end, GlyphPosition* positions, int maxPositions);
    void textMetrics(float* ascender, float* descender, float* lineh);
    int textBreakLines(const char* string, const char* end, float breakRowWidth, TextRow* rows, int maxRows, int skipSpaces);
    const Params& internalParams() const;
    void debugDumpPathCache() const;

protected:
    std::unique_ptr<ContextImpl> mImpl;
};

// Frame drawing, paths, paints, text, images, and state: use Context members, e.g. ctx.beginFrame(...)
// or ctx->fill() when using a pointer or smart pointer to Context.

//
// Color utils
//
// Colors in NanoVG are stored as unsigned ints in ABGR format.

// Returns a color value from red, green, blue values. Alpha will be set to 255 (1.0f).
Color rgb(unsigned char r, unsigned char g, unsigned char b);

// Returns a color value from red, green, blue values. Alpha will be set to 1.0f.
Color rgbf(float r, float g, float b);

// Returns a color value from red, green, blue and alpha values.
Color rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a);

// Returns a color value from red, green, blue and alpha values.
Color rgbaf(float r, float g, float b, float a);

// Linearly interpolates from color c0 to c1, and returns resulting color value.
Color lerpRgba(Color c0, Color c1, float u);

// Sets transparency of a color value.
Color transRgba(Color c0, unsigned char a);

// Sets transparency of a color value.
Color transRgbaf(Color c0, float a);

// Returns color value specified by hue, saturation and lightness.
// HSL values are all in range [0..1], alpha will be set to 255.
Color hsl(float h, float s, float l);

// Returns color value specified by hue, saturation and lightness and alpha.
// HSL values are all in range [0..1], alpha in range [0..255]
Color hsla(float h, float s, float l, unsigned char a);

//
// Transforms (matrix utilities; not tied to Context)
// A 2x3 matrix is represented as float[6].

// Sets the transform to identity matrix.
void transformIdentity(float* dst);

// Sets the transform to translation matrix matrix.
void transformTranslate(float* dst, float tx, float ty);

// Sets the transform to scale matrix.
void transformScale(float* dst, float sx, float sy);

// Sets the transform to rotate matrix. Angle is specified in radians.
void transformRotate(float* dst, float a);

// Sets the transform to skew-x matrix. Angle is specified in radians.
void transformSkewX(float* dst, float a);

// Sets the transform to skew-y matrix. Angle is specified in radians.
void transformSkewY(float* dst, float a);

// Sets the transform to the result of multiplication of two transforms, of A = A*B.
void transformMultiply(float* dst, const float* src);

// Sets the transform to the result of multiplication of two transforms, of A = B*A.
void transformPremultiply(float* dst, const float* src);

// Sets the destination to inverse of specified transform.
// Returns 1 if the inverse could be calculated, else 0.
int transformInverse(float* dst, const float* src);

// Transform a point by given transform.
void transformPoint(float& dstx, float& dsty, const float* xform, float srcx, float srcy);

// Converts degrees to radians and vice versa.
float degToRad(float deg);
float radToDeg(float rad);

}  // namespace nvg
