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

#ifndef NANOVG_H
#define NANOVG_H

#ifdef __cplusplus
extern "C" {
#endif

#define NVG_PI 3.14159265358979323846264338327f

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4201)  // nonstandard extension used : nameless struct/union
#endif

typedef struct NVGcontext NVGcontext;

struct NVGcolor {
    union {
        float rgba[4];
        struct {
            float r, g, b, a;
        };
    };
};
typedef struct NVGcolor NVGcolor;
typedef struct NVGcolor NVGcolor;

struct NVGpaint {
    float xform[6];
    float extent[2];
    float radius;
    float feather;
    NVGcolor innerColor;
    NVGcolor outerColor;
    int image;
};
typedef struct NVGpaint NVGpaint;

enum NVGwinding {
    NVG_CCW = 1,  // Winding for solid shapes
    NVG_CW = 2,   // Winding for holes
};

enum NVGsolidity {
    NVG_SOLID = 1,  // CCW
    NVG_HOLE = 2,   // CW
};

enum NVGlineStyle { NVG_LINE_SOLID = 1, NVG_LINE_DASHED = 2, NVG_LINE_DOTTED = 3, NVG_LINE_GLOW = 4 };

enum NVGlineCap {
    NVG_BUTT,
    NVG_ROUND,
    NVG_SQUARE,
    NVG_BEVEL,
    NVG_MITER,
};

enum NVGalign {
    // Horizontal align
    NVG_ALIGN_LEFT = 1 << 0,    // Default, align text horizontally to left.
    NVG_ALIGN_CENTER = 1 << 1,  // Align text horizontally to center.
    NVG_ALIGN_RIGHT = 1 << 2,   // Align text horizontally to right.
    // Vertical align
    NVG_ALIGN_TOP = 1 << 3,            // Align text vertically to top.
    NVG_ALIGN_MIDDLE = 1 << 4,         // Align text vertically to middle.
    NVG_ALIGN_MIDDLE_ASCENT = 1 << 5,  // Align text vertically to middle of ascent.
    NVG_ALIGN_BOTTOM = 1 << 6,         // Align text vertically to bottom.
    NVG_ALIGN_BASELINE = 1 << 7        // Default, align text vertically to baseline.
};

enum NVGblendFactor {
    NVG_ZERO = 1 << 0,
    NVG_ONE = 1 << 1,
    NVG_SRC_COLOR = 1 << 2,
    NVG_ONE_MINUS_SRC_COLOR = 1 << 3,
    NVG_DST_COLOR = 1 << 4,
    NVG_ONE_MINUS_DST_COLOR = 1 << 5,
    NVG_SRC_ALPHA = 1 << 6,
    NVG_ONE_MINUS_SRC_ALPHA = 1 << 7,
    NVG_DST_ALPHA = 1 << 8,
    NVG_ONE_MINUS_DST_ALPHA = 1 << 9,
    NVG_SRC_ALPHA_SATURATE = 1 << 10,
};

enum NVGcompositeOperation {
    NVG_SOURCE_OVER,
    NVG_SOURCE_IN,
    NVG_SOURCE_OUT,
    NVG_ATOP,
    NVG_DESTINATION_OVER,
    NVG_DESTINATION_IN,
    NVG_DESTINATION_OUT,
    NVG_DESTINATION_ATOP,
    NVG_LIGHTER,
    NVG_COPY,
    NVG_XOR,
};

struct NVGcompositeOperationState {
    int srcRGB;
    int dstRGB;
    int srcAlpha;
    int dstAlpha;
};
typedef struct NVGcompositeOperationState NVGcompositeOperationState;

struct NVGglyphPosition {
    const char* str;   // Position of the glyph in the input string.
    float x;           // The x-coordinate of the logical glyph position.
    float minx, maxx;  // The bounds of the glyph shape.
};
typedef struct NVGglyphPosition NVGglyphPosition;

struct NVGtextRow {
    const char* start;  // Pointer to the input text where the row starts.
    const char* end;    // Pointer to the input text where the row ends (one past the last character).
    const char* next;   // Pointer to the beginning of the next row.
    float width;        // Logical width of the row.
    float minx, maxx;   // Actual bounds of the row. Logical with and bounds can differ because of kerning and some parts over extending.
};
typedef struct NVGtextRow NVGtextRow;

// Axis-aligned rectangle in local (pre-transform) coordinates. Mirrors nvg::Rect.
struct NVGrect {
    float x, y, w, h;
};
typedef struct NVGrect NVGrect;

// How an nvgDraw* call renders its shape, analogous to SkPaint::Style. Mirrors nvg::PaintStyle.
// NVG_PAINT_FILL uses the current fill paint, NVG_PAINT_STROKE the current stroke paint/width/caps/joins.
enum NVGpaintStyle {
    NVG_PAINT_FILL = 0,
    NVG_PAINT_STROKE = 1,
    NVG_PAINT_FILL_AND_STROKE = 2,
};

// Interpretation of the point list passed to nvgDrawPoints, analogous to SkCanvas::PointMode. Mirrors nvg::PointMode.
enum NVGpointMode {
    NVG_POINTS_POINTS = 0,   // Each point is drawn as a dot of strokeWidth diameter (round dot for NVG_ROUND caps, else square).
    NVG_POINTS_LINES = 1,    // Each consecutive pair of points is drawn as an independent line segment.
    NVG_POINTS_POLYGON = 2,  // The points are drawn as one connected, open polyline.
};

// Element drawn by nvgDrawGrid. Mirrors nvg::GridShape.
enum NVGgridShape {
    NVG_GRID_LINES = 0,    // The (cols + 1) vertical and (rows + 1) horizontal lines bounding the cells; size is the line thickness.
    NVG_GRID_CIRCLES = 1,  // One circle centered in each cell; size is the radius.
    NVG_GRID_SQUARES = 2,  // One axis-aligned square centered in each cell; size is the half side length (a "radius").
};

// Built-in stroke marker shapes. Mirrors nvg::MarkerShape: each is defined in a unit box centered on the
// vertex, +x along the marker's orientation, scaled by NVGmarker.size.
enum NVGmarkerShape {
    NVG_MARKER_NONE = 0,
    NVG_MARKER_ARROW = 1,    // Triangle from x = -0.5 (base, 1 unit wide) to x = +0.5 (tip).
    NVG_MARKER_CIRCLE = 2,   // Circle of diameter 1.
    NVG_MARKER_SQUARE = 3,   // Square of side 1.
    NVG_MARKER_DIAMOND = 4,  // Square of diagonal 1, rotated 45 degrees.
    NVG_MARKER_BAR = 5,      // Tick across the path: 1 unit long and one stroke width thick.
    NVG_MARKER_CUSTOM = 6,   // Drawn by NVGmarker.draw.
};

// How a marker is rotated at its vertex, like the SVG marker orient attribute. Mirrors nvg::MarkerOrient.
enum NVGmarkerOrient {
    NVG_MARKER_ORIENT_AUTO = 0,                // Along the path (bisector of the incoming and outgoing segments).
    NVG_MARKER_ORIENT_AUTO_START_REVERSE = 1,  // Like AUTO, but the start marker points backwards.
    NVG_MARKER_ORIENT_FIXED = 2,               // NVGmarker.angle radians, in the path's local coordinates.
};

// A stroke marker (SVG marker-start / marker-mid / marker-end, markerUnits="strokeWidth"). The marker
// frame has its origin on the vertex and +x along the orientation, with 1 unit = size x the stroke width.
// For NVG_MARKER_CUSTOM, draw(ctx, uptr) is called in that frame with the fill paint set to the stroke
// paint; the caller's path and state are restored afterwards. Use nvgMarker() for the defaults.
typedef struct NVGmarker {
    int shape;    // NVGmarkerShape
    float size;   // default 3
    int orient;   // NVGmarkerOrient
    float angle;  // for NVG_MARKER_ORIENT_FIXED
    void (*draw)(NVGcontext* ctx, void* uptr);
    void* uptr;
} NVGmarker;

// Flags accepted by the backend constructors (nvgCreateGL*, nvgCreateVk). Mirrors nvg::CreateFlags.
enum NVGcreateFlags {
    // Flag indicating if geometry based anti-aliasing is used (may not be needed when using MSAA).
    NVG_ANTIALIAS = 1 << 0,
    // Flag indicating if strokes should be drawn using stencil buffer. The rendering will be a little
    // slower, but path overlaps (i.e. self-intersecting or sharp turns) will be drawn just once.
    NVG_STENCIL_STROKES = 1 << 1,
    // Flag indicating that additional debug checks are done.
    NVG_DEBUG = 1 << 2,
};

struct NVGvertex;

enum NVGimageFlags {
    NVG_IMAGE_GENERATE_MIPMAPS = 1 << 0,  // Generate mipmaps during creation of the image.
    NVG_IMAGE_REPEATX = 1 << 1,           // Repeat image in X direction.
    NVG_IMAGE_REPEATY = 1 << 2,           // Repeat image in Y direction.
    NVG_IMAGE_FLIPY = 1 << 3,             // Flips (inverses) image in Y direction when rendered.
    NVG_IMAGE_PREMULTIPLIED = 1 << 4,     // Image data has premultiplied alpha.
    NVG_IMAGE_NEAREST = 1 << 5,           // Image interpolation is Nearest instead Linear
};

// Begin drawing a new frame
// Calls to nanovg drawing API should be wrapped in nvgBeginFrame() & nvgEndFrame()
// nvgBeginFrame() defines the size of the window to render to in relation currently
// set viewport (i.e. glViewport on GL backends). Device pixel ration allows to
// control the rendering on Hi-DPI devices.
// For example, GLFW returns two dimension for an opened window: window size and
// frame buffer size. In that case you would set windowWidth/Height to the window size
// devicePixelRatio to: frameBufferWidth / windowWidth.
void nvgBeginFrame(NVGcontext* ctx, float windowWidth, float windowHeight, float devicePixelRatio);

// Cancels drawing the current frame.
void nvgCancelFrame(NVGcontext* ctx);

// Ends drawing flushing remaining render state.
void nvgEndFrame(NVGcontext* ctx);

//
// Composite operation
//
// The composite operations in NanoVG are modeled after HTML Canvas API, and
// the blend func is based on OpenGL (see corresponding manuals for more info).
// The colors in the blending state have premultiplied alpha.

// Sets the composite operation. The op parameter should be one of NVGcompositeOperation.
void nvgGlobalCompositeOperation(NVGcontext* ctx, int op);

// Sets the composite operation with custom pixel arithmetic. The parameters should be one of NVGblendFactor.
void nvgGlobalCompositeBlendFunc(NVGcontext* ctx, int sfactor, int dfactor);

// Sets the composite operation with custom pixel arithmetic for RGB and alpha components separately. The parameters should be one of NVGblendFactor.
void nvgGlobalCompositeBlendFuncSeparate(NVGcontext* ctx, int srcRGB, int dstRGB, int srcAlpha, int dstAlpha);

//
// Color utils
//
// Colors in NanoVG are stored as unsigned ints in ABGR format.

// Returns a color value from red, green, blue values. Alpha will be set to 255 (1.0f).
NVGcolor nvgRGB(unsigned char r, unsigned char g, unsigned char b);

// Returns a color value from red, green, blue values. Alpha will be set to 1.0f.
NVGcolor nvgRGBf(float r, float g, float b);

// Returns a color value from red, green, blue and alpha values.
NVGcolor nvgRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a);

// Returns a color value from red, green, blue and alpha values.
NVGcolor nvgRGBAf(float r, float g, float b, float a);

// Linearly interpolates from color c0 to c1, and returns resulting color value.
NVGcolor nvgLerpRGBA(NVGcolor c0, NVGcolor c1, float u);

// Sets transparency of a color value.
NVGcolor nvgTransRGBA(NVGcolor c0, unsigned char a);

// Sets transparency of a color value.
NVGcolor nvgTransRGBAf(NVGcolor c0, float a);

// Returns color value specified by hue, saturation and lightness.
// HSL values are all in range [0..1], alpha will be set to 255.
NVGcolor nvgHSL(float h, float s, float l);

// Returns color value specified by hue, saturation and lightness and alpha.
// HSL values are all in range [0..1], alpha in range [0..255]
NVGcolor nvgHSLA(float h, float s, float l, unsigned char a);

//
// State Handling
//
// NanoVG contains state which represents how paths will be rendered.
// The state contains transform, fill and stroke styles, text and font styles,
// and scissor clipping.

// Pushes and saves the current render state into a state stack.
// A matching nvgRestore() must be used to restore the state.
void nvgSave(NVGcontext* ctx);

// Pops and restores current render state.
void nvgRestore(NVGcontext* ctx);

// Resets current render state to default values. Does not affect the render state stack.
void nvgReset(NVGcontext* ctx);

// Gets the current scissor bounds
struct NVGscissorBounds nvgCurrentScissor(NVGcontext* ctx);

//
// Render styles
//
// Fill and stroke render style can be either a solid color or a paint which is a gradient or a pattern.
// Solid color is simply defined as a color value, different kinds of paints can be created
// using nvgLinearGradient(), nvgBoxGradient(), nvgRadialGradient() and nvgImagePattern().
//
// Current render style can be saved and restored using nvgSave() and nvgRestore().

// Sets whether to draw antialias for nvgStroke() and nvgFill(). It's enabled by default.
void nvgShapeAntiAlias(NVGcontext* ctx, int enabled);

// Sets current stroke style to a solid color.
void nvgStrokeColor(NVGcontext* ctx, NVGcolor color);

// Sets current stroke style to a paint, which can be a one of the gradients or a pattern.
void nvgStrokePaint(NVGcontext* ctx, NVGpaint paint);

// Sets current fill style to a solid color.
void nvgFillColor(NVGcontext* ctx, NVGcolor color);

// Sets current fill style to a paint, which can be a one of the gradients or a pattern.
void nvgFillPaint(NVGcontext* ctx, NVGpaint paint);

// Sets the miter limit of the stroke style.
// Miter limit controls when a sharp corner is beveled.
void nvgMiterLimit(NVGcontext* ctx, float limit);

// Sets the stroke width of the stroke style.
void nvgStrokeWidth(NVGcontext* ctx, float size);

// Sets how line is drawn.
// Can be one of NVG_LINE_SOLID (default), NVG_LINE_GLOW, NVG_LINE_DASHED, NVG LINE_DOTTED
void nvgLineStyle(NVGcontext* ctx, int lineStyle);

#define NVG_MAX_STROKE_DASHES 16

// SVG-style dashing for every stroke (stroke-dasharray). dashes alternates dash and gap lengths in local
// units, starting with a dash; an odd count is repeated to make it even, as in SVG. NULL, count <= 0, a
// negative value or an all-zero array turns dashing off. Values past NVG_MAX_STROKE_DASHES are ignored.
// The pattern restarts on each subpath and line caps apply to both ends of every dash.
void nvgStrokeDashArray(NVGcontext* ctx, const float* dashes, int count);

// Same as nvgStrokeDashArray, parsed from an SVG stroke-dasharray value such as "6 3", "4,2,1,2" or "none".
void nvgStrokeDashArrayString(NVGcontext* ctx, const char* dashArray);

// Shifts the start of the dash pattern along each subpath, like SVG stroke-dashoffset (may be negative).
void nvgStrokeDashOffset(NVGcontext* ctx, float offset);

// A marker of the given NVGmarkerShape and size with automatic orientation and no custom callback.
NVGmarker nvgMarker(int shape, float size);

// Markers drawn by every stroke after the stroke itself, at the path's first vertex (start), last vertex
// (end) and every vertex in between (mid). Vertices are the end points of path commands (an arc or ellipse
// is one segment; zero-length segments add none). Pass nvgMarker(NVG_MARKER_NONE, 0) to remove one.
// nvgDrawArrow and nvgDrawGrid do not draw markers. See nvg::Context::markerStart.
void nvgMarkerStart(NVGcontext* ctx, NVGmarker marker);
void nvgMarkerMid(NVGcontext* ctx, NVGmarker marker);
void nvgMarkerEnd(NVGcontext* ctx, NVGmarker marker);
// Sets start, mid and end at once, like the SVG marker shorthand.
void nvgMarkers(NVGcontext* ctx, NVGmarker marker);

// Sets how the end of the line (cap) is drawn,
// Can be one of: NVG_BUTT (default), NVG_ROUND, NVG_SQUARE.
void nvgLineCap(NVGcontext* ctx, int cap);

// Sets how sharp path corners are drawn.
// Can be one of NVG_MITER (default), NVG_ROUND, NVG_BEVEL.
void nvgLineJoin(NVGcontext* ctx, int join);

// Sets the transparency applied to all rendered shapes.
// Already transparent paths will get proportionally more transparent as well.
void nvgGlobalAlpha(NVGcontext* ctx, float alpha);

//
// Transforms
//
// The paths, gradients, patterns and scissor region are transformed by an transformation
// matrix at the time when they are passed to the API.
// The current transformation matrix is a affine matrix:
//   [sx kx tx]
//   [ky sy ty]
//   [ 0  0  1]
// Where: sx,sy define scaling, kx,ky skewing, and tx,ty translation.
// The last row is assumed to be 0,0,1 and is not stored.
//
// Apart from nvgResetTransform(), each transformation function first creates
// specific transformation matrix and pre-multiplies the current transformation by it.
//
// Current coordinate system (transformation) can be saved and restored using nvgSave() and nvgRestore().

// Resets current transform to a identity matrix.
void nvgResetTransform(NVGcontext* ctx);

// Premultiplies current coordinate system by specified matrix.
// The parameters are interpreted as matrix as follows:
//   [a c e]
//   [b d f]
//   [0 0 1]
void nvgTransform(NVGcontext* ctx, float a, float b, float c, float d, float e, float f);

// Translates current coordinate system.
void nvgTranslate(NVGcontext* ctx, float x, float y);

// Rotates current coordinate system. Angle is specified in radians.
void nvgRotate(NVGcontext* ctx, float angle);

// Skews the current coordinate system along X axis. Angle is specified in radians.
void nvgSkewX(NVGcontext* ctx, float angle);

// Skews the current coordinate system along Y axis. Angle is specified in radians.
void nvgSkewY(NVGcontext* ctx, float angle);

// Scales the current coordinate system.
void nvgScale(NVGcontext* ctx, float x, float y);

// Stores the top part (a-f) of the current transformation matrix in to the specified buffer.
//   [a c e]
//   [b d f]
//   [0 0 1]
// There should be space for 6 floats in the return buffer for the values a-f.
void nvgCurrentTransform(NVGcontext* ctx, float* xform);

// The following functions can be used to make calculations on 2x3 transformation matrices.
// A 2x3 matrix is represented as float[6].

// Sets the transform to identity matrix.
void nvgTransformIdentity(float* dst);

// Sets the transform to translation matrix matrix.
void nvgTransformTranslate(float* dst, float tx, float ty);

// Sets the transform to scale matrix.
void nvgTransformScale(float* dst, float sx, float sy);

// Sets the transform to rotate matrix. Angle is specified in radians.
void nvgTransformRotate(float* dst, float a);

// Sets the transform to skew-x matrix. Angle is specified in radians.
void nvgTransformSkewX(float* dst, float a);

// Sets the transform to skew-y matrix. Angle is specified in radians.
void nvgTransformSkewY(float* dst, float a);

// Sets the transform to the result of multiplication of two transforms, of A = A*B.
void nvgTransformMultiply(float* dst, const float* src);

// Sets the transform to the result of multiplication of two transforms, of A = B*A.
void nvgTransformPremultiply(float* dst, const float* src);

// Sets the destination to inverse of specified transform.
// Returns 1 if the inverse could be calculated, else 0.
int nvgTransformInverse(float* dst, const float* src);

// Transform a point by given transform.
void nvgTransformPoint(float* dstx, float* dsty, const float* xform, float srcx, float srcy);

// Converts degrees to radians and vice versa.
float nvgDegToRad(float deg);
float nvgRadToDeg(float rad);

//
// Images
//
// NanoVG allows you to load jpg, png, psd, tga, pic and gif files to be used for rendering.
// In addition you can upload your own image. The image loading is provided by stb_image.
// The parameter imageFlags is combination of flags defined in NVGimageFlags.

// Creates image by loading it from the disk from specified file name.
// Returns handle to the image.
int nvgCreateImage(NVGcontext* ctx, const char* filename, int imageFlags);

// Creates image by loading it from the specified chunk of memory.
// Returns handle to the image.
int nvgCreateImageMem(NVGcontext* ctx, int imageFlags, unsigned char* data, int ndata);

// Creates image from specified image data.
// Returns handle to the image.
int nvgCreateImageRGBA(NVGcontext* ctx, int w, int h, int imageFlags, const unsigned char* data);

// Updates image data specified by image handle.
void nvgUpdateImage(NVGcontext* ctx, int image, const unsigned char* data);

// Returns the dimensions of a created image.
void nvgImageSize(NVGcontext* ctx, int image, int* w, int* h);

// Deletes created image.
void nvgDeleteImage(NVGcontext* ctx, int image);

//
// Paints
//
// NanoVG supports four types of paints: linear gradient, box gradient, radial gradient and image pattern.
// These can be used as paints for strokes and fills.

// Creates and returns a linear gradient. Parameters (sx,sy)-(ex,ey) specify the start and end coordinates
// of the linear gradient, icol specifies the start color and ocol the end color.
// The gradient is transformed by the current transform when it is passed to nvgFillPaint() or nvgStrokePaint().
NVGpaint nvgLinearGradient(NVGcontext* ctx, float sx, float sy, float ex, float ey, NVGcolor icol, NVGcolor ocol);

// Creates and returns a box gradient. Box gradient is a feathered rounded rectangle, it is useful for rendering
// drop shadows or highlights for boxes. Parameters (x,y) define the top-left corner of the rectangle,
// (w,h) define the size of the rectangle, r defines the corner radius, and f feather. Feather defines how blurry
// the border of the rectangle is. Parameter icol specifies the inner color and ocol the outer color of the gradient.
// The gradient is transformed by the current transform when it is passed to nvgFillPaint() or nvgStrokePaint().
NVGpaint nvgBoxGradient(NVGcontext* ctx, float x, float y, float w, float h, float r, float f, NVGcolor icol, NVGcolor ocol);

// Creates and returns a radial gradient. Parameters (cx,cy) specify the center, inr and outr specify
// the inner and outer radius of the gradient, icol specifies the start color and ocol the end color.
// The gradient is transformed by the current transform when it is passed to nvgFillPaint() or nvgStrokePaint().
NVGpaint nvgRadialGradient(NVGcontext* ctx, float cx, float cy, float inr, float outr, NVGcolor icol, NVGcolor ocol);

// Creates and returns an image pattern. Parameters (ox,oy) specify the left-top location of the image pattern,
// (ex,ey) the size of one image, angle rotation around the top-left corner, image is handle to the image to render.
// The gradient is transformed by the current transform when it is passed to nvgFillPaint() or nvgStrokePaint().
NVGpaint nvgImagePattern(NVGcontext* ctx, float ox, float oy, float ex, float ey, float angle, int image, float alpha);

//
// Scissoring
//
// Scissoring allows you to clip the rendering into a rectangle. This is useful for various
// user interface cases like rendering a text edit or a timeline.

// Sets the current scissor rectangle.
// The scissor rectangle is transformed by the current transform.
void nvgScissor(NVGcontext* ctx, float x, float y, float w, float h);

// Intersects current scissor rectangle with the specified rectangle.
// The scissor rectangle is transformed by the current transform.
// Note: in case the rotation of previous scissor rect differs from
// the current one, the intersection will be done between the specified
// rectangle and the previous scissor rectangle transformed in the current
// transform space. The resulting shape is always rectangle.
void nvgIntersectScissor(NVGcontext* ctx, float x, float y, float w, float h);

// Reset and disables scissoring.
void nvgResetScissor(NVGcontext* ctx);

//
// Paths
//
// Drawing a new shape starts with nvgBeginPath(), it clears all the currently defined paths.
// Then you define one or more paths and sub-paths which describe the shape. The are functions
// to draw common shapes like rectangles and circles, and lower level step-by-step functions,
// which allow to define a path curve by curve.
//
// NanoVG uses even-odd fill rule to draw the shapes. Solid shapes should have counter clockwise
// winding and holes should have counter clockwise order. To specify winding of a path you can
// call nvgPathWinding(). This is useful especially for the common shapes, which are drawn CCW.
//
// Finally you can fill the path using current fill style by calling nvgFill(), and stroke it
// with current stroke style by calling nvgStroke().
//
// The curve segments and sub-paths are transformed by the current transform.

// Clears the current path and sub-paths.
void nvgBeginPath(NVGcontext* ctx);

// Starts new sub-path with specified point as first point.
void nvgMoveTo(NVGcontext* ctx, float x, float y);

// Adds line segment from the last point in the path to the specified point.
void nvgLineTo(NVGcontext* ctx, float x, float y);

// Adds cubic bezier segment from last point in the path via two control points to the specified point.
void nvgBezierTo(NVGcontext* ctx, float c1x, float c1y, float c2x, float c2y, float x, float y);

// Adds quadratic bezier segment from last point in the path via a control point to the specified point.
void nvgQuadTo(NVGcontext* ctx, float cx, float cy, float x, float y);

// Adds an arc segment at the corner defined by the last path point, and two specified points.
void nvgArcTo(NVGcontext* ctx, float x1, float y1, float x2, float y2, float radius);

// Closes current sub-path with a line segment.
void nvgClosePath(NVGcontext* ctx);

// Sets the current sub-path winding, see NVGwinding and NVGsolidity.
void nvgPathWinding(NVGcontext* ctx, int dir);

// Creates new circle arc shaped sub-path. The arc center is at cx,cy, the arc radius is r,
// and the arc is drawn from angle a0 to a1, and swept in direction dir (NVG_CCW, or NVG_CW).
// Angles are specified in radians.
void nvgArc(NVGcontext* ctx, float cx, float cy, float r, float a0, float a1, int dir);

// Elliptical arc from its center: radii rx, ry, the ellipse rotated by rotation radians, from parametric
// angle a0 to a1 in direction dir (NVG_CCW or NVG_CW). Connects to the current path like nvgArc.
void nvgEllipticalArc(NVGcontext* ctx, float cx, float cy, float rx, float ry, float rotation, float a0, float a1, int dir);

// Elliptical arc from the current point to x,y with the parameters of the SVG path "A" command (the
// rotation is in radians). largeArc and sweep are booleans; sweep = clockwise on a y-down screen. Radii
// too small to reach the endpoint are scaled up, and a zero radius draws a line, as in SVG.
void nvgEllipticalArcTo(NVGcontext* ctx, float rx, float ry, float xAxisRotation, int largeArc, int sweep, float x, float y);

// Creates new rectangle shaped sub-path.
void nvgRect(NVGcontext* ctx, float x, float y, float w, float h);

// Creates new rounded rectangle shaped sub-path.
void nvgRoundedRect(NVGcontext* ctx, float x, float y, float w, float h, float r);

// Creates new rounded rectangle shaped sub-path with varying radii for each corner.
void nvgRoundedRectVarying(NVGcontext* ctx, float x, float y, float w, float h, float radTopLeft, float radTopRight, float radBottomRight, float radBottomLeft);

// Rounded rectangle with elliptical corners: rx horizontal and ry vertical radius (SVG <rect rx ry>).
void nvgRoundedRectElliptic(NVGcontext* ctx, float x, float y, float w, float h, float rx, float ry);

// Per-corner elliptical radii (horizontal, vertical), clockwise from the top left.
void nvgRoundedRectEllipticVarying(NVGcontext* ctx, float x, float y, float w, float h, float rxTopLeft, float ryTopLeft, float rxTopRight, float ryTopRight, float rxBottomRight,
                                   float ryBottomRight, float rxBottomLeft, float ryBottomLeft);

// Creates new ellipse shaped sub-path.
void nvgEllipse(NVGcontext* ctx, float cx, float cy, float rx, float ry);

// Creates new circle shaped sub-path.
void nvgCircle(NVGcontext* ctx, float cx, float cy, float r);

// Fills the current path with current fill style.
void nvgFill(NVGcontext* ctx);

// Fills the current path with current stroke style.
void nvgStroke(NVGcontext* ctx);

//
// Immediate draw calls (Skia style)
//
// One-call draw functions modeled after SkCanvas; the C counterparts of the nvg::Context::draw* methods.
// Each call builds its own path and renders it with the current state (paints, stroke width, caps, joins,
// transform, scissor, alpha, composite operation). They replace the current path, so call them outside of
// an nvgBeginPath()/nvgFill() sequence. `style` is one of NVGpaintStyle.

// Fills the whole viewport (respecting scissor) with a paint or color, like SkCanvas::drawPaint/drawColor.
void nvgDrawPaint(NVGcontext* ctx, NVGpaint paint);
void nvgDrawColor(NVGcontext* ctx, NVGcolor color);

// Replaces the pixels in the viewport (respecting scissor) with color, ignoring blending, like SkCanvas::clear.
void nvgClear(NVGcontext* ctx, NVGcolor color);

// Strokes a single line segment with the current stroke style.
void nvgDrawLine(NVGcontext* ctx, float x0, float y0, float x1, float y1);

// pts is an array of count (x, y) pairs, i.e. 2 * count floats. mode is one of NVGpointMode.
void nvgDrawPoints(NVGcontext* ctx, int mode, const float* pts, int count);
void nvgDrawPoint(NVGcontext* ctx, float x, float y);

// Arrow from the tail (x0, y0) to the tip (x1, y1): a stalk of stalkWidth topped by a triangular head
// headLength long (along the arrow) and headWidth wide (across it). headLength is clamped to the
// arrow's length and headWidth to at least stalkWidth.
void nvgDrawArrow(NVGcontext* ctx, float x0, float y0, float x1, float y1, float stalkWidth, float headWidth, float headLength, int style);

// A cols x rows grid laid out over r. Drawn as one shader-evaluated quad when the backend implements
// NVGparams.renderShape (see nvg::Context::drawGrid for when it falls back to tessellated geometry). shape is an NVGgridShape; size is the line
// thickness for NVG_GRID_LINES and the radius / half side for NVG_GRID_CIRCLES / NVG_GRID_SQUARES.
void nvgDrawGrid(NVGcontext* ctx, NVGrect r, int cols, int rows, int shape, float size, int style);

// Closed polygon through count (x, y) pairs.
void nvgDrawPolygon(NVGcontext* ctx, const float* pts, int count, int style);
void nvgDrawRect(NVGcontext* ctx, NVGrect r, int style);
void nvgDrawRoundRect(NVGcontext* ctx, NVGrect r, float radius, int style);
// Rounded rect with elliptical corners (rx horizontal, ry vertical).
void nvgDrawRoundRectElliptic(NVGcontext* ctx, NVGrect r, float rx, float ry, int style);
void nvgDrawRoundRectVarying(NVGcontext* ctx, NVGrect r, float radTopLeft, float radTopRight, float radBottomRight, float radBottomLeft, int style);

// Fills the area between two rounded rects (outer minus inner), like SkCanvas::drawDRRect.
void nvgDrawDRRect(NVGcontext* ctx, NVGrect outer, float outerRadius, NVGrect inner, float innerRadius);
void nvgDrawCircle(NVGcontext* ctx, float cx, float cy, float r, int style);

// Ellipse inscribed in the rect, like SkCanvas::drawOval.
void nvgDrawOval(NVGcontext* ctx, NVGrect oval, int style);

// Circular arc, like SkCanvas::drawArc. Angles in radians; positive sweep is clockwise (y-down).
// With useCenter non-zero the arc is closed through the center (a wedge); otherwise fill draws the chord segment.
void nvgDrawArc(NVGcontext* ctx, float cx, float cy, float r, float startAngle, float sweepAngle, int useCenter, int style);

// Draws the full image with its top-left corner at (x, y), like SkCanvas::drawImage.
void nvgDrawImage(NVGcontext* ctx, int image, float x, float y, float alpha);

// Draws the src sub-rect of the image (in image pixels) scaled into dst, like SkCanvas::drawImageRect.
// Pass src = NULL to draw the whole image.
void nvgDrawImageRect(NVGcontext* ctx, int image, const NVGrect* src, NVGrect dst, float alpha);

// Nine-patch: corners outside center (image pixels) keep their size, edges stretch along one axis,
// and the center stretches in both, like SkCanvas::drawImageNine.
void nvgDrawImageNine(NVGcontext* ctx, int image, NVGrect center, NVGrect dst, float alpha);

// Draws textured triangles (3 vertices per triangle). Vertex x/y are local coordinates and u/v are
// normalized texture coordinates in [0, 1]; s/t are ignored. Like SkCanvas::drawVertices in triangle mode.
void nvgDrawVertices(NVGcontext* ctx, const struct NVGvertex* verts, int nverts, int image, float alpha);

//
// Text
//
// NanoVG allows you to load .ttf files and use the font to render text.
//
// The appearance of the text can be defined by setting the current text style
// and by specifying the fill color. Common text and font settings such as
// font size, letter spacing and text align are supported. Font blur allows you
// to create simple text effects such as drop shadows.
//
// At render time the font face can be set based on the font handles or name.
//
// Font measure functions return values in local space, the calculations are
// carried in the same resolution as the final rendering. This is done because
// the text glyph positions are snapped to the nearest pixels sharp rendering.
//
// The local space means that values are not rotated or scale as per the current
// transformation. For example if you set font size to 12, which would mean that
// line height is 16, then regardless of the current scaling and rotation, the
// returned line height is always 16. Some measures may vary because of the scaling
// since aforementioned pixel snapping.
//
// While this may sound a little odd, the setup allows you to always render the
// same way regardless of scaling. I.e. following works regardless of scaling:
//
//		const char* txt = "Text me up.";
//		nvgTextBounds(vg, x,y, txt, NULL, bounds);
//		nvgBeginPath(vg);
//		nvgRect(vg, bounds[0],bounds[1], bounds[2]-bounds[0], bounds[3]-bounds[1]);
//		nvgFill(vg);
//
// Note: currently only solid color fill is supported for text.

// Creates font by loading it from the disk from specified file name.
// Returns handle to the font.
int nvgCreateFont(NVGcontext* ctx, const char* name, const char* filename);

// fontIndex specifies which font face to load from a .ttf/.ttc file.
int nvgCreateFontAtIndex(NVGcontext* ctx, const char* name, const char* filename, const int fontIndex);

// Creates font by loading it from the specified memory chunk.
// Returns handle to the font.
int nvgCreateFontMem(NVGcontext* ctx, const char* name, unsigned char* data, int ndata, int freeData);

// fontIndex specifies which font face to load from a .ttf/.ttc file.
int nvgCreateFontMemAtIndex(NVGcontext* ctx, const char* name, unsigned char* data, int ndata, int freeData, const int fontIndex);

// Finds a loaded font of specified name, and returns handle to it, or -1 if the font is not found.
int nvgFindFont(NVGcontext* ctx, const char* name);

// Adds a fallback font by handle.
int nvgAddFallbackFontId(NVGcontext* ctx, int baseFont, int fallbackFont);

// Adds a fallback font by name.
int nvgAddFallbackFont(NVGcontext* ctx, const char* baseFont, const char* fallbackFont);

// Resets fallback fonts by handle.
void nvgResetFallbackFontsId(NVGcontext* ctx, int baseFont);

// Resets fallback fonts by name.
void nvgResetFallbackFonts(NVGcontext* ctx, const char* baseFont);

// Sets the font size of current text style.
void nvgFontSize(NVGcontext* ctx, float size);

// Sets the blur of current text style.
void nvgFontBlur(NVGcontext* ctx, float blur);

// Soft outer shadow under all text, like CSS text-shadow: offset and blur in local units, and the
// shadow color (zero alpha = off, the default). The blur adds to the font blur and is capped at 20
// device pixels. See nvg::Context::textOuterShadow.
void nvgTextOuterShadow(NVGcontext* ctx, float offsetX, float offsetY, float blur, NVGcolor color);

// Soft inner shadow over all text: color inside the glyphs wherever an offset, blurred copy of them does
// not cover them (offset down-right shades the top-left inner edges). Same units as nvgTextOuterShadow;
// zero alpha = off. The mask is built on the CPU and drawn as one quad. See nvg::Context::textInnerShadow.
void nvgTextInnerShadow(NVGcontext* ctx, float offsetX, float offsetY, float blur, NVGcolor color);

// Sets the dilation of current text style.
void nvgFontDilate(NVGcontext* ctx, float dilate);

// Sets the letter spacing of current text style.
void nvgTextLetterSpacing(NVGcontext* ctx, float spacing);

// Sets the proportional line height of current text style. The line height is specified as multiple of font size.
void nvgTextLineHeight(NVGcontext* ctx, float lineHeight);

// Sets the text align of current text style, see NVGalign for options.
void nvgTextAlign(NVGcontext* ctx, int align);

// Sets the font face based on specified id of current text style.
void nvgFontFaceId(NVGcontext* ctx, int font);

// Sets the font face based on specified name of current text style.
void nvgFontFace(NVGcontext* ctx, const char* font);

// Gets the font size of current text style.
int nvgGetFontFaceId(NVGcontext* ctx);

// Get the font size
float nvgGetFontSize(NVGcontext* ctx);

// Get Stroke width
float nvgGetStrokeWidth(NVGcontext* ctx);

// Get text alignment
int nvgGetTextAlign(NVGcontext* ctx);

// Draws text string at specified location. If end is specified only the sub-string up to the end is drawn.
float nvgText(NVGcontext* ctx, float x, float y, const char* string, const char* end);

// Draws multi-line text string at specified location wrapped at the specified width. If end is specified only the sub-string up to the end is drawn.
// White space is stripped at the beginning of the rows, the text is split at word boundaries or when new-line characters are encountered.
// Words longer than the max width are slit at nearest character (i.e. no hyphenation).
void nvgTextBox(NVGcontext* ctx, float x, float y, float breakRowWidth, const char* string, const char* end);

// Flags for nvgTextOnPath, mirroring the SVG <textPath> attributes (see nvg::TextPathOptions).
enum NVGtextPathFlags {
    NVG_TEXTPATH_PERCENT = 1 << 0,  // startOffset is a percentage (0..100) of the path's length.
    NVG_TEXTPATH_RIGHT = 1 << 1,    // side="right": follow the path in reverse, text on the other side.
    NVG_TEXTPATH_STRETCH = 1 << 2,  // method="stretch": bend each glyph along the path instead of rotating it rigidly.
};

// Draws text along the current path, like SVG <textPath>: each glyph is placed at its advance midpoint's
// distance along the path and rotated to follow the path there. startOffset is the distance (local
// units, or a percentage with NVG_TEXTPATH_PERCENT) of the text anchor set by the horizontal text align;
// baselineShift moves the glyphs along the path's normal (positive = down). The path is not drawn and
// stays current. Returns the distance along the path just past the last glyph. See nvg::Context::textOnPath.
float nvgTextOnPath(NVGcontext* ctx, const char* string, const char* end, float startOffset, float baselineShift, int flags);

// Measures the specified text string. Parameter bounds should be a pointer to float[4],
// if the bounding box of the text should be returned. The bounds value are [xmin,ymin, xmax,ymax]
// Returns the horizontal advance of the measured text (i.e. where the next character should drawn).
// Measured values are returned in local coordinate space.
float nvgTextBounds(NVGcontext* ctx, float x, float y, const char* string, const char* end, float* bounds);

// Measures the specified multi-text string. Parameter bounds should be a pointer to float[4],
// if the bounding box of the text should be returned. The bounds value are [xmin,ymin, xmax,ymax]
// Measured values are returned in local coordinate space.
void nvgTextBoxBounds(NVGcontext* ctx, float x, float y, float breakRowWidth, const char* string, const char* end, float* bounds);

// Calculates the glyph x positions of the specified text. If end is specified only the sub-string will be used.
// Measured values are returned in local coordinate space.
int nvgTextGlyphPositions(NVGcontext* ctx, float x, float y, const char* string, const char* end, NVGglyphPosition* positions, int maxPositions);

// Returns the vertical metrics based on the current text style.
// Measured values are returned in local coordinate space.
void nvgTextMetrics(NVGcontext* ctx, float* ascender, float* descender, float* lineh);

// Breaks the specified text into lines. If end is specified only the sub-string will be used.
// White space is stripped at the beginning of the rows, the text is split at word boundaries or when new-line characters are encountered.
// Words longer than the max width are slit at nearest character (i.e. no hyphenation).
int nvgTextBreakLines(NVGcontext* ctx, const char* string, const char* end, float breakRowWidth, NVGtextRow* rows, int maxRows, int skipSpaces);

// Get image texture Id
int nvgGetImageTextureId(NVGcontext* ctx, int handle);

//
// Internal Render API
//
enum NVGtexture {
    NVG_TEXTURE_ALPHA = 0x01,
    NVG_TEXTURE_RGBA = 0x02,
};

struct NVGscissor {
    float xform[6];
    float extent[2];
};
typedef struct NVGscissor NVGscissor;

struct NVGscissorBounds {
    float x;
    float y;
    float w;
    float h;
};
typedef struct NVGscissorBounds NVGscissorBounds;

struct NVGvertex {
    float x, y, u, v, s, t;
};
typedef struct NVGvertex NVGvertex;

struct NVGpath {
    int first;
    int count;
    int reversed;
    unsigned char closed;
    int nbevel;
    NVGvertex* fill;
    int nfill;
    NVGvertex* stroke;
    int nstroke;
    int winding;
    int convex;
};
typedef struct NVGpath NVGpath;

// Analytic shape passed to NVGparams.renderShape. Mirrors nvg::Shape; see nanovg.hpp for the meaning
// of params per type.
enum NVGshapeType {
    NVG_SHAPE_NONE = 0,
    NVG_SHAPE_GRID_LINES = 1,
    NVG_SHAPE_GRID_CIRCLES = 2,
    NVG_SHAPE_GRID_SQUARES = 3,
    NVG_SHAPE_ARROW = 4,
};

struct NVGshape {
    int type;
    float strokeHalfWidth;
    float aaWidth;
    float xform[6];
    float params[8];
};
typedef struct NVGshape NVGshape;

struct NVGparams {
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
    void (*renderFill)(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation, NVGscissor* scissor, float fringe, const float* bounds, const NVGpath* paths, int npaths);
    void (*renderStroke)(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation, NVGscissor* scissor, float fringe, float strokeWidth, int lineStyle, const NVGpath* paths,
                         int npaths);
    void (*renderTriangles)(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation, NVGscissor* scissor, const NVGvertex* verts, int nverts, float fringe);
    void (*renderDelete)(void* uptr);
    // Optional (may be NULL): draws verts (device-space triangles) with paint, scaled by the per-pixel
    // coverage of shape. nvgDrawGrid / nvgDrawArrow fall back to tessellated geometry without it.
    void (*renderShape)(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation, NVGscissor* scissor, float fringe, const NVGshape* shape, const NVGvertex* verts, int nverts);
};
typedef struct NVGparams NVGparams;

// Constructor and destructor, called by the render back-end.
NVGcontext* nvgCreateInternal(NVGparams* params);
void nvgDeleteInternal(NVGcontext* ctx);

NVGparams* nvgInternalParams(NVGcontext* ctx);

// Debug function to dump cached path data.
void nvgDebugDumpPathCache(NVGcontext* ctx);

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef _MSC_VER
#define NVG_NOTUSED(v) ((void)(v))
#else
#define NVG_NOTUSED(v)                     \
    for (;;) {                             \
        (void)(1 ? (void)0 : ((void)(v))); \
        break;                             \
    }
#endif

#ifdef __cplusplus
}
#endif

#endif  // NANOVG_H
