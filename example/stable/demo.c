#include "demo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef NANOVG_VULKAN
#ifdef NANOVG_GLEW
#include <GL/glew.h>
#endif
#include <GLFW/glfw3.h>
#endif
#include "nanovg.h"
// stb_image's implementation lives in the nanovg library.
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244) /* float/double/int conversions in layout math */
#define snprintf _snprintf
#elif !defined(__MINGW32__)
#include <iconv.h>
#endif

#define ICON_SEARCH 0x1F50D
#define ICON_CIRCLED_CROSS 0x2716
#define ICON_CHEVRON_RIGHT 0xE75E
#define ICON_CHECK 0x2713
#define ICON_LOGIN 0xE740
#define ICON_TRASH 0xE729

// static float minf(float a, float b) { return a < b ? a : b; }
// static float maxf(float a, float b) { return a > b ? a : b; }
// static float absf(float a) { return a >= 0.0f ? a : -a; }
static float clampf(float a, float mn, float mx)
{
    return a < mn ? mn : (a > mx ? mx : a);
}

// Returns 1 if col.rgba is 0.0f,0.0f,0.0f,0.0f, 0 otherwise
int isBlack(NVGcolor col)
{
    if (col.r == 0.0f && col.g == 0.0f && col.b == 0.0f && col.a == 0.0f) {
        return 1;
    }
    return 0;
}

static char* cpToUTF8(int cp, char* str)
{
    int n = 0;
    if (cp < 0x80)
        n = 1;
    else if (cp < 0x800)
        n = 2;
    else if (cp < 0x10000)
        n = 3;
    else if (cp < 0x200000)
        n = 4;
    else if (cp < 0x4000000)
        n = 5;
    else if (cp <= 0x7fffffff)
        n = 6;
    str[n] = '\0';
    switch (n) {
        case 6:
            str[5] = 0x80 | (cp & 0x3f);
            cp = cp >> 6;
            cp |= 0x4000000;
        case 5:
            str[4] = 0x80 | (cp & 0x3f);
            cp = cp >> 6;
            cp |= 0x200000;
        case 4:
            str[3] = 0x80 | (cp & 0x3f);
            cp = cp >> 6;
            cp |= 0x10000;
        case 3:
            str[2] = 0x80 | (cp & 0x3f);
            cp = cp >> 6;
            cp |= 0x800;
        case 2:
            str[1] = 0x80 | (cp & 0x3f);
            cp = cp >> 6;
            cp |= 0xc0;
        case 1:
            str[0] = (char)cp;
    }
    return str;
}

void drawWindow(NVGcontext* vg, const char* title, float x, float y, float w, float h)
{
    float cornerRadius = 3.0f;
    NVGpaint shadowPaint;
    NVGpaint headerPaint;

    nvgSave(vg);
    //	nvgClearState(vg);

    // Window
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, cornerRadius);
    nvgFillColor(vg, nvgRGBA(28, 30, 34, 192));
    //	nvgFillColor(vg, nvgRGBA(0,0,0,128));
    nvgFill(vg);

    // Drop shadow
    shadowPaint = nvgBoxGradient(vg, x, y + 2, w, h, cornerRadius * 2, 10, nvgRGBA(0, 0, 0, 128), nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, x - 10, y - 10, w + 20, h + 30);
    nvgRoundedRect(vg, x, y, w, h, cornerRadius);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, shadowPaint);
    nvgFill(vg);

    // Header
    headerPaint = nvgLinearGradient(vg, x, y, x, y + 15, nvgRGBA(255, 255, 255, 8), nvgRGBA(0, 0, 0, 16));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 1, y + 1, w - 2, 30, cornerRadius - 1);
    nvgFillPaint(vg, headerPaint);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, x + 0.5f, y + 0.5f + 30);
    nvgLineTo(vg, x + 0.5f + w - 1, y + 0.5f + 30);
    nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 32));
    nvgStroke(vg);

    nvgFontSize(vg, 15.0f);
    nvgFontFace(vg, "sans-bold");
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    nvgFontBlur(vg, 2);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 128));
    nvgText(vg, x + w / 2, y + 16 + 1, title, NULL);

    nvgFontBlur(vg, 0);
    nvgFillColor(vg, nvgRGBA(220, 220, 220, 160));
    nvgText(vg, x + w / 2, y + 16, title, NULL);

    nvgRestore(vg);
}

void drawSearchBox(NVGcontext* vg, const char* text, float x, float y, float w, float h)
{
    NVGpaint bg;
    char icon[8];
    float cornerRadius = h / 2 - 1;

    // Edit
    bg = nvgBoxGradient(vg, x, y + 1.5f, w, h, h / 2, 5, nvgRGBA(0, 0, 0, 16), nvgRGBA(0, 0, 0, 92));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, cornerRadius);
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    /*	nvgBeginPath(vg);
            nvgRoundedRect(vg, x+0.5f,y+0.5f, w-1,h-1, cornerRadius-0.5f);
            nvgStrokeColor(vg, nvgRGBA(0,0,0,48));
            nvgStroke(vg);*/

    nvgFontSize(vg, h * 1.3f);
    nvgFontFace(vg, "icons");
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 64));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + h * 0.55f, y + h * 0.55f, cpToUTF8(ICON_SEARCH, icon), NULL);

    nvgFontSize(vg, 17.0f);
    nvgFontFace(vg, "sans");
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 32));

    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + h * 1.05f, y + h * 0.5f, text, NULL);

    nvgFontSize(vg, h * 1.3f);
    nvgFontFace(vg, "icons");
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 32));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + w - h * 0.55f, y + h * 0.55f, cpToUTF8(ICON_CIRCLED_CROSS, icon), NULL);
}

void drawDropDown(NVGcontext* vg, const char* text, float x, float y, float w, float h)
{
    NVGpaint bg;
    char icon[8];
    float cornerRadius = 4.0f;

    bg = nvgLinearGradient(vg, x, y, x, y + h, nvgRGBA(255, 255, 255, 16), nvgRGBA(0, 0, 0, 16));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 1, y + 1, w - 2, h - 2, cornerRadius - 1);
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 0.5f, y + 0.5f, w - 1, h - 1, cornerRadius - 0.5f);
    nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 48));
    nvgStroke(vg);

    nvgFontSize(vg, 17.0f);
    nvgFontFace(vg, "sans");
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 160));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + h * 0.3f, y + h * 0.5f, text, NULL);

    nvgFontSize(vg, h * 1.3f);
    nvgFontFace(vg, "icons");
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 64));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + w - h * 0.5f, y + h * 0.5f, cpToUTF8(ICON_CHEVRON_RIGHT, icon), NULL);
}

void drawLabel(NVGcontext* vg, const char* text, float x, float y, float w, float h)
{
    NVG_NOTUSED(w);

    nvgFontSize(vg, 15.0f);
    nvgFontFace(vg, "sans");
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 128));

    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, x, y + h * 0.5f, text, NULL);
}

void drawEditBoxBase(NVGcontext* vg, float x, float y, float w, float h)
{
    NVGpaint bg;
    // Edit
    bg = nvgBoxGradient(vg, x + 1, y + 1 + 1.5f, w - 2, h - 2, 3, 4, nvgRGBA(255, 255, 255, 32), nvgRGBA(32, 32, 32, 32));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 1, y + 1, w - 2, h - 2, 4 - 1);
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 0.5f, y + 0.5f, w - 1, h - 1, 4 - 0.5f);
    nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 48));
    nvgStroke(vg);
}

void drawEditBox(NVGcontext* vg, const char* text, float x, float y, float w, float h)
{
    drawEditBoxBase(vg, x, y, w, h);

    nvgFontSize(vg, 17.0f);
    nvgFontFace(vg, "sans");
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 64));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + h * 0.3f, y + h * 0.5f, text, NULL);
}

void drawEditBoxNum(NVGcontext* vg, const char* text, const char* units, float x, float y, float w, float h)
{
    float uw;

    drawEditBoxBase(vg, x, y, w, h);

    uw = nvgTextBounds(vg, 0, 0, units, NULL, NULL);

    nvgFontSize(vg, 15.0f);
    nvgFontFace(vg, "sans");
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 64));
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + w - h * 0.3f, y + h * 0.5f, units, NULL);

    nvgFontSize(vg, 17.0f);
    nvgFontFace(vg, "sans");
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 128));
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + w - uw - h * 0.5f, y + h * 0.5f, text, NULL);
}

void drawCheckBox(NVGcontext* vg, const char* text, float x, float y, float w, float h)
{
    NVGpaint bg;
    char icon[8];
    NVG_NOTUSED(w);

    nvgFontSize(vg, 15.0f);
    nvgFontFace(vg, "sans");
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 160));

    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + 28, y + h * 0.5f, text, NULL);

    bg = nvgBoxGradient(vg, x + 1, y + (int)(h * 0.5f) - 9 + 1, 18, 18, 3, 3, nvgRGBA(0, 0, 0, 32), nvgRGBA(0, 0, 0, 92));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 1, y + (int)(h * 0.5f) - 9, 18, 18, 3);
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    nvgFontSize(vg, 33);
    nvgFontFace(vg, "icons");
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 128));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + 9 + 2, y + h * 0.5f, cpToUTF8(ICON_CHECK, icon), NULL);
}

void drawButton(NVGcontext* vg, int preicon, const char* text, float x, float y, float w, float h, NVGcolor col)
{
    NVGpaint bg;
    char icon[8];
    float cornerRadius = 4.0f;
    float tw = 0, iw = 0;

    bg = nvgLinearGradient(vg, x, y, x, y + h, nvgRGBA(255, 255, 255, isBlack(col) ? 16 : 32), nvgRGBA(0, 0, 0, isBlack(col) ? 16 : 32));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 1, y + 1, w - 2, h - 2, cornerRadius - 1);
    if (!isBlack(col)) {
        nvgFillColor(vg, col);
        nvgFill(vg);
    }
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 0.5f, y + 0.5f, w - 1, h - 1, cornerRadius - 0.5f);
    nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 48));
    nvgStroke(vg);

    nvgFontSize(vg, 17.0f);
    nvgFontFace(vg, "sans-bold");
    tw = nvgTextBounds(vg, 0, 0, text, NULL, NULL);
    if (preicon != 0) {
        nvgFontSize(vg, h * 1.3f);
        nvgFontFace(vg, "icons");
        iw = nvgTextBounds(vg, 0, 0, cpToUTF8(preicon, icon), NULL, NULL);
        iw += h * 0.15f;
    }

    if (preicon != 0) {
        nvgFontSize(vg, h * 1.3f);
        nvgFontFace(vg, "icons");
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 96));
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, x + w * 0.5f - tw * 0.5f - iw * 0.75f, y + h * 0.5f, cpToUTF8(preicon, icon), NULL);
    }

    nvgFontSize(vg, 17.0f);
    nvgFontFace(vg, "sans-bold");
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 160));
    nvgText(vg, x + w * 0.5f - tw * 0.5f + iw * 0.25f, y + h * 0.5f - 1, text, NULL);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 160));
    nvgText(vg, x + w * 0.5f - tw * 0.5f + iw * 0.25f, y + h * 0.5f, text, NULL);
}

void drawSlider(NVGcontext* vg, float pos, float x, float y, float w, float h)
{
    NVGpaint bg, knob;
    float cy = y + (float)((int)(h * 0.5f));
    float kr = (float)((int)(h * 0.25f));

    nvgSave(vg);
    //	nvgClearState(vg);

    // Slot
    bg = nvgBoxGradient(vg, x, cy - 2 + 1, w, 4, 2, 2, nvgRGBA(0, 0, 0, 32), nvgRGBA(0, 0, 0, 128));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, cy - 2, w, 4, 2);
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    // Knob Shadow
    bg = nvgRadialGradient(vg, x + (int)(pos * w), cy + 1, kr - 3, kr + 3, nvgRGBA(0, 0, 0, 64), nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, x + (int)(pos * w) - kr - 5, cy - kr - 5, kr * 2 + 5 + 5, kr * 2 + 5 + 5 + 3);
    nvgCircle(vg, x + (int)(pos * w), cy, kr);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    // Knob
    knob = nvgLinearGradient(vg, x, cy - kr, x, cy + kr, nvgRGBA(255, 255, 255, 16), nvgRGBA(0, 0, 0, 16));
    nvgBeginPath(vg);
    nvgCircle(vg, x + (int)(pos * w), cy, kr - 1);
    nvgFillColor(vg, nvgRGBA(40, 43, 48, 255));
    nvgFill(vg);
    nvgFillPaint(vg, knob);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgCircle(vg, x + (int)(pos * w), cy, kr - 0.5f);
    nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 92));
    nvgStroke(vg);

    nvgRestore(vg);
}

void drawFancyText(NVGcontext* vg, float x, float y)
{
    nvgFontSize(vg, 30.0f);
    nvgFontFace(vg, "sans-bold");
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    nvgFontBlur(vg, 10);
    nvgFillColor(vg, nvgRGB(255, 0, 102));
    nvgText(vg, x, y, "Blur", NULL);
    nvgFontBlur(vg, 0);
    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgText(vg, x, y, "Blur", NULL);

    nvgFontDilate(vg, 2);
    nvgFillColor(vg, nvgRGB(255, 0, 102));
    nvgText(vg, x, y + 40, "Outline", NULL);
    nvgFontDilate(vg, 0);
    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgText(vg, x, y + 40, "Outline", NULL);

    nvgFontDilate(vg, 3);  // Dilate will always be applied before blur
    nvgFontBlur(vg, 2);
    nvgFillColor(vg, nvgRGB(255, 0, 102));
    nvgText(vg, x, y + 80, "Blur Outline", NULL);
    nvgFontDilate(vg, 0);
    nvgFontBlur(vg, 0);
    nvgFillColor(vg, nvgRGB(255, 255, 255));
    nvgText(vg, x, y + 80, "Blur Outline", NULL);
}

void drawEyes(NVGcontext* vg, float x, float y, float w, float h, float mx, float my, float t)
{
    NVGpaint gloss, bg;
    float ex = w * 0.23f;
    float ey = h * 0.5f;
    float lx = x + ex;
    float ly = y + ey;
    float rx = x + w - ex;
    float ry = y + ey;
    float dx, dy, d;
    float br = (ex < ey ? ex : ey) * 0.5f;
    float blink = 1.0f - powf(sinf(t * 0.5f), 200.0f) * 0.8f;

    bg = nvgLinearGradient(vg, x, y + h * 0.5f, x + w * 0.1f, y + h, nvgRGBA(0, 0, 0, 32), nvgRGBA(0, 0, 0, 16));
    nvgBeginPath(vg);
    nvgEllipse(vg, lx + 3.0f, ly + 16.0f, ex, ey);
    nvgEllipse(vg, rx + 3.0f, ry + 16.0f, ex, ey);
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    bg = nvgLinearGradient(vg, x, y + h * 0.25f, x + w * 0.1f, y + h, nvgRGBA(220, 220, 220, 255), nvgRGBA(128, 128, 128, 255));
    nvgBeginPath(vg);
    nvgEllipse(vg, lx, ly, ex, ey);
    nvgEllipse(vg, rx, ry, ex, ey);
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    dx = (mx - rx) / (ex * 10);
    dy = (my - ry) / (ey * 10);
    d = sqrtf(dx * dx + dy * dy);
    if (d > 1.0f) {
        dx /= d;
        dy /= d;
    }
    dx *= ex * 0.4f;
    dy *= ey * 0.5f;
    nvgBeginPath(vg);
    nvgEllipse(vg, lx + dx, ly + dy + ey * 0.25f * (1 - blink), br, br * blink);
    nvgFillColor(vg, nvgRGBA(32, 32, 32, 255));
    nvgFill(vg);

    dx = (mx - rx) / (ex * 10);
    dy = (my - ry) / (ey * 10);
    d = sqrtf(dx * dx + dy * dy);
    if (d > 1.0f) {
        dx /= d;
        dy /= d;
    }
    dx *= ex * 0.4f;
    dy *= ey * 0.5f;
    nvgBeginPath(vg);
    nvgEllipse(vg, rx + dx, ry + dy + ey * 0.25f * (1 - blink), br, br * blink);
    nvgFillColor(vg, nvgRGBA(32, 32, 32, 255));
    nvgFill(vg);

    gloss = nvgRadialGradient(vg, lx - ex * 0.25f, ly - ey * 0.5f, ex * 0.1f, ex * 0.75f, nvgRGBA(255, 255, 255, 128), nvgRGBA(255, 255, 255, 0));
    nvgBeginPath(vg);
    nvgEllipse(vg, lx, ly, ex, ey);
    nvgFillPaint(vg, gloss);
    nvgFill(vg);

    gloss = nvgRadialGradient(vg, rx - ex * 0.25f, ry - ey * 0.5f, ex * 0.1f, ex * 0.75f, nvgRGBA(255, 255, 255, 128), nvgRGBA(255, 255, 255, 0));
    nvgBeginPath(vg);
    nvgEllipse(vg, rx, ry, ex, ey);
    nvgFillPaint(vg, gloss);
    nvgFill(vg);
}

void drawGraph(NVGcontext* vg, float x, float y, float w, float h, float t)
{
    NVGpaint bg;
    float samples[6];
    float sx[6], sy[6];
    float dx = w / 5.0f;
    int i;

    samples[0] = (1 + sinf(t * 1.2345f + cosf(t * 0.33457f) * 0.44f)) * 0.5f;
    samples[1] = (1 + sinf(t * 0.68363f + cosf(t * 1.3f) * 1.55f)) * 0.5f;
    samples[2] = (1 + sinf(t * 1.1642f + cosf(t * 0.33457) * 1.24f)) * 0.5f;
    samples[3] = (1 + sinf(t * 0.56345f + cosf(t * 1.63f) * 0.14f)) * 0.5f;
    samples[4] = (1 + sinf(t * 1.6245f + cosf(t * 0.254f) * 0.3f)) * 0.5f;
    samples[5] = (1 + sinf(t * 0.345f + cosf(t * 0.03f) * 0.6f)) * 0.5f;

    for (i = 0; i < 6; i++) {
        sx[i] = x + i * dx;
        sy[i] = y + h * samples[i] * 0.8f;
    }

    // Graph background
    bg = nvgLinearGradient(vg, x, y, x, y + h, nvgRGBA(0, 160, 192, 0), nvgRGBA(0, 160, 192, 64));
    nvgBeginPath(vg);
    nvgMoveTo(vg, sx[0], sy[0]);
    for (i = 1; i < 6; i++) nvgBezierTo(vg, sx[i - 1] + dx * 0.5f, sy[i - 1], sx[i] - dx * 0.5f, sy[i], sx[i], sy[i]);
    nvgLineTo(vg, x + w, y + h);
    nvgLineTo(vg, x, y + h);
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    // Graph line
    nvgBeginPath(vg);
    nvgMoveTo(vg, sx[0], sy[0] + 2);
    for (i = 1; i < 6; i++) nvgBezierTo(vg, sx[i - 1] + dx * 0.5f, sy[i - 1] + 2, sx[i] - dx * 0.5f, sy[i] + 2, sx[i], sy[i] + 2);
    nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 32));
    nvgStrokeWidth(vg, 3.0f);
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgMoveTo(vg, sx[0], sy[0]);
    for (i = 1; i < 6; i++) nvgBezierTo(vg, sx[i - 1] + dx * 0.5f, sy[i - 1], sx[i] - dx * 0.5f, sy[i], sx[i], sy[i]);
    nvgStrokeColor(vg, nvgRGBA(0, 160, 192, 255));
    nvgStrokeWidth(vg, 3.0f);
    nvgStroke(vg);

    // Graph sample pos
    for (i = 0; i < 6; i++) {
        bg = nvgRadialGradient(vg, sx[i], sy[i] + 2, 3.0f, 8.0f, nvgRGBA(0, 0, 0, 32), nvgRGBA(0, 0, 0, 0));
        nvgBeginPath(vg);
        nvgRect(vg, sx[i] - 10, sy[i] - 10 + 2, 20, 20);
        nvgFillPaint(vg, bg);
        nvgFill(vg);
    }

    nvgBeginPath(vg);
    for (i = 0; i < 6; i++) nvgCircle(vg, sx[i], sy[i], 4.0f);
    nvgFillColor(vg, nvgRGBA(0, 160, 192, 255));
    nvgFill(vg);
    nvgBeginPath(vg);
    for (i = 0; i < 6; i++) nvgCircle(vg, sx[i], sy[i], 2.0f);
    nvgFillColor(vg, nvgRGBA(220, 220, 220, 255));
    nvgFill(vg);

    nvgStrokeWidth(vg, 1.0f);
}

void drawSpinner(NVGcontext* vg, float cx, float cy, float r, float t)
{
    float a0 = 0.0f + t * 6;
    float a1 = NVG_PI + t * 6;
    float r0 = r;
    float r1 = r * 0.75f;
    float ax, ay, bx, by;
    NVGpaint paint;

    nvgSave(vg);

    nvgBeginPath(vg);
    nvgArc(vg, cx, cy, r0, a0, a1, NVG_CW);
    nvgArc(vg, cx, cy, r1, a1, a0, NVG_CCW);
    nvgClosePath(vg);
    ax = cx + cosf(a0) * (r0 + r1) * 0.5f;
    ay = cy + sinf(a0) * (r0 + r1) * 0.5f;
    bx = cx + cosf(a1) * (r0 + r1) * 0.5f;
    by = cy + sinf(a1) * (r0 + r1) * 0.5f;
    paint = nvgLinearGradient(vg, ax, ay, bx, by, nvgRGBA(0, 0, 0, 0), nvgRGBA(0, 0, 0, 128));
    nvgFillPaint(vg, paint);
    nvgFill(vg);

    nvgRestore(vg);
}

void drawThumbnails(NVGcontext* vg, float x, float y, float w, float h, const int* images, int nimages, float t)
{
    float cornerRadius = 3.0f;
    NVGpaint shadowPaint, imgPaint, fadePaint;
    float ix, iy, iw, ih;
    float thumb = 60.0f;
    float arry = 30.5f;
    int imgw, imgh;
    float stackh = (nimages / 2) * (thumb + 10) + 10;
    int i;
    float u = (1 + cosf(t * 0.5f)) * 0.5f;
    float u2 = (1 - cosf(t * 0.2f)) * 0.5f;
    float scrollh, dv;

    nvgSave(vg);
    //	nvgClearState(vg);

    // Drop shadow
    shadowPaint = nvgBoxGradient(vg, x, y + 4, w, h, cornerRadius * 2, 20, nvgRGBA(0, 0, 0, 128), nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, x - 10, y - 10, w + 20, h + 30);
    nvgRoundedRect(vg, x, y, w, h, cornerRadius);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, shadowPaint);
    nvgFill(vg);

    // Window
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, cornerRadius);
    nvgMoveTo(vg, x - 10, y + arry);
    nvgLineTo(vg, x + 1, y + arry - 11);
    nvgLineTo(vg, x + 1, y + arry + 11);
    nvgFillColor(vg, nvgRGBA(200, 200, 200, 255));
    nvgFill(vg);

    nvgSave(vg);
    nvgScissor(vg, x, y, w, h);
    nvgTranslate(vg, 0, -(stackh - h) * u);

    dv = 1.0f / (float)(nimages - 1);

    for (i = 0; i < nimages; i++) {
        float tx, ty, v, a;
        tx = x + 10;
        ty = y + 10;
        tx += (i % 2) * (thumb + 10);
        ty += (i / 2) * (thumb + 10);
        nvgImageSize(vg, images[i], &imgw, &imgh);
        if (imgw < imgh) {
            iw = thumb;
            ih = iw * (float)imgh / (float)imgw;
            ix = 0;
            iy = -(ih - thumb) * 0.5f;
        } else {
            ih = thumb;
            iw = ih * (float)imgw / (float)imgh;
            ix = -(iw - thumb) * 0.5f;
            iy = 0;
        }

        v = i * dv;
        a = clampf((u2 - v) / dv, 0, 1);

        if (a < 1.0f) drawSpinner(vg, tx + thumb / 2, ty + thumb / 2, thumb * 0.25f, t);

        imgPaint = nvgImagePattern(vg, tx + ix, ty + iy, iw, ih, 0.0f / 180.0f * NVG_PI, images[i], a);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, tx, ty, thumb, thumb, 5);
        nvgFillPaint(vg, imgPaint);
        nvgFill(vg);

        shadowPaint = nvgBoxGradient(vg, tx - 1, ty, thumb + 2, thumb + 2, 5, 3, nvgRGBA(0, 0, 0, 128), nvgRGBA(0, 0, 0, 0));
        nvgBeginPath(vg);
        nvgRect(vg, tx - 5, ty - 5, thumb + 10, thumb + 10);
        nvgRoundedRect(vg, tx, ty, thumb, thumb, 6);
        nvgPathWinding(vg, NVG_HOLE);
        nvgFillPaint(vg, shadowPaint);
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, tx + 0.5f, ty + 0.5f, thumb - 1, thumb - 1, 4 - 0.5f);
        nvgStrokeWidth(vg, 1.0f);
        nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 192));
        nvgStroke(vg);
    }
    nvgRestore(vg);

    // Hide fades
    fadePaint = nvgLinearGradient(vg, x, y, x, y + 6, nvgRGBA(200, 200, 200, 255), nvgRGBA(200, 200, 200, 0));
    nvgBeginPath(vg);
    nvgRect(vg, x + 4, y, w - 8, 6);
    nvgFillPaint(vg, fadePaint);
    nvgFill(vg);

    fadePaint = nvgLinearGradient(vg, x, y + h, x, y + h - 6, nvgRGBA(200, 200, 200, 255), nvgRGBA(200, 200, 200, 0));
    nvgBeginPath(vg);
    nvgRect(vg, x + 4, y + h - 6, w - 8, 6);
    nvgFillPaint(vg, fadePaint);
    nvgFill(vg);

    // Scroll bar
    shadowPaint = nvgBoxGradient(vg, x + w - 12 + 1, y + 4 + 1, 8, h - 8, 3, 4, nvgRGBA(0, 0, 0, 32), nvgRGBA(0, 0, 0, 92));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + w - 12, y + 4, 8, h - 8, 3);
    nvgFillPaint(vg, shadowPaint);
    //	nvgFillColor(vg, nvgRGBA(255,0,0,128));
    nvgFill(vg);

    scrollh = (h / stackh) * (h - 8);
    shadowPaint = nvgBoxGradient(vg, x + w - 12 - 1, y + 4 + (h - 8 - scrollh) * u - 1, 8, scrollh, 3, 4, nvgRGBA(220, 220, 220, 255), nvgRGBA(128, 128, 128, 255));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + w - 12 + 1, y + 4 + 1 + (h - 8 - scrollh) * u, 8 - 2, scrollh - 2, 2);
    nvgFillPaint(vg, shadowPaint);
    //	nvgFillColor(vg, nvgRGBA(0,0,0,128));
    nvgFill(vg);

    nvgRestore(vg);
}

void drawColorwheel(NVGcontext* vg, float x, float y, float w, float h, float t)
{
    int i;
    float r0, r1, ax, ay, bx, by, cx, cy, aeps, r;
    float hue = sinf(t * 0.12f);
    NVGpaint paint;

    nvgSave(vg);

    /*	nvgBeginPath(vg);
            nvgRect(vg, x,y,w,h);
            nvgFillColor(vg, nvgRGBA(255,0,0,128));
            nvgFill(vg);*/

    cx = x + w * 0.5f;
    cy = y + h * 0.5f;
    r1 = (w < h ? w : h) * 0.5f - 5.0f;
    r0 = r1 - 20.0f;
    aeps = 0.5f / r1;  // half a pixel arc length in radians (2pi cancels out).

    for (i = 0; i < 6; i++) {
        float a0 = (float)i / 6.0f * NVG_PI * 2.0f - aeps;
        float a1 = (float)(i + 1.0f) / 6.0f * NVG_PI * 2.0f + aeps;
        nvgBeginPath(vg);
        nvgArc(vg, cx, cy, r0, a0, a1, NVG_CW);
        nvgArc(vg, cx, cy, r1, a1, a0, NVG_CCW);
        nvgClosePath(vg);
        ax = cx + cosf(a0) * (r0 + r1) * 0.5f;
        ay = cy + sinf(a0) * (r0 + r1) * 0.5f;
        bx = cx + cosf(a1) * (r0 + r1) * 0.5f;
        by = cy + sinf(a1) * (r0 + r1) * 0.5f;
        paint = nvgLinearGradient(vg, ax, ay, bx, by, nvgHSLA(a0 / (NVG_PI * 2), 1.0f, 0.55f, 255), nvgHSLA(a1 / (NVG_PI * 2), 1.0f, 0.55f, 255));
        nvgFillPaint(vg, paint);
        nvgFill(vg);
    }

    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r0 - 0.5f);
    nvgCircle(vg, cx, cy, r1 + 0.5f);
    nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 64));
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    // Selector
    nvgSave(vg);
    nvgTranslate(vg, cx, cy);
    nvgRotate(vg, hue * NVG_PI * 2);

    // Marker on
    nvgStrokeWidth(vg, 2.0f);
    nvgBeginPath(vg);
    nvgRect(vg, r0 - 1, -3, r1 - r0 + 2, 6);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 192));
    nvgStroke(vg);

    paint = nvgBoxGradient(vg, r0 - 3, -5, r1 - r0 + 6, 10, 2, 4, nvgRGBA(0, 0, 0, 128), nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, r0 - 2 - 10, -4 - 10, r1 - r0 + 4 + 20, 8 + 20);
    nvgRect(vg, r0 - 2, -4, r1 - r0 + 4, 8);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, paint);
    nvgFill(vg);

    // Center triangle
    r = r0 - 6;
    ax = cosf(120.0f / 180.0f * NVG_PI) * r;
    ay = sinf(120.0f / 180.0f * NVG_PI) * r;
    bx = cosf(-120.0f / 180.0f * NVG_PI) * r;
    by = sinf(-120.0f / 180.0f * NVG_PI) * r;
    nvgBeginPath(vg);
    nvgMoveTo(vg, r, 0);
    nvgLineTo(vg, ax, ay);
    nvgLineTo(vg, bx, by);
    nvgClosePath(vg);
    paint = nvgLinearGradient(vg, r, 0, ax, ay, nvgHSLA(hue, 1.0f, 0.5f, 255), nvgRGBA(255, 255, 255, 255));
    nvgFillPaint(vg, paint);
    nvgFill(vg);
    paint = nvgLinearGradient(vg, (r + ax) * 0.5f, (0 + ay) * 0.5f, bx, by, nvgRGBA(0, 0, 0, 0), nvgRGBA(0, 0, 0, 255));
    nvgFillPaint(vg, paint);
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 64));
    nvgStroke(vg);

    // Select circle on triangle
    ax = cosf(120.0f / 180.0f * NVG_PI) * r * 0.3f;
    ay = sinf(120.0f / 180.0f * NVG_PI) * r * 0.4f;
    nvgStrokeWidth(vg, 2.0f);
    nvgBeginPath(vg);
    nvgCircle(vg, ax, ay, 5);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 192));
    nvgStroke(vg);

    paint = nvgRadialGradient(vg, ax, ay, 7, 9, nvgRGBA(0, 0, 0, 64), nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, ax - 20, ay - 20, 40, 40);
    nvgCircle(vg, ax, ay, 7);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, paint);
    nvgFill(vg);

    nvgRestore(vg);

    // Render hue label
    const float tw = 50;
    const float th = 25;
    r1 += 0.5f * sqrtf(tw * tw + th * th);
    nvgBeginPath(vg);
    nvgFillColor(vg, nvgRGB(32, 32, 32));
    ax = cx + r1 * cosf(hue * NVG_PI * 2);
    ay = cy + r1 * sinf(hue * NVG_PI * 2);
    nvgRoundedRect(vg, ax - tw * 0.5f, ay - th * 0.5f, tw, th, 5.0f);
    nvgFill(vg);

    nvgSave(vg);
    nvgTranslate(vg, ax, ay);
    nvgScale(vg, 2.0f, 2.0f);  // Check that local transforms work with text
    nvgFontSize(vg, 0.5f * th);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgFontFace(vg, "sans");
    nvgFillColor(vg, nvgRGB(255, 255, 255));
    char str[128];
    sprintf(str, "%d%%", (int)(100.0f * (hue + 1.0f)) % 100);
    nvgBeginPath(vg);
    nvgText(vg, 0.0f, -0.2f * th, str, 0);
    nvgFill(vg);

    nvgRestore(vg);
    nvgRestore(vg);
}

void drawStylizedLines(NVGcontext* vg, float x, float y, float w, float h)
{
    nvgLineJoin(vg, NVG_ROUND);
    nvgLineStyle(vg, NVG_LINE_DASHED);
    nvgStrokeColor(vg, nvgRGBAf(0.6f, 0.6f, 1.0f, 1.0f));
    nvgStrokeWidth(vg, 5.0f);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgStroke(vg);
    nvgLineStyle(vg, NVG_LINE_SOLID);
}

void drawLines(NVGcontext* vg, float x, float y, float w, float h, float strokeWidth, NVGcolor color, float t)
{
    int i, j;
    float pad = 5.0f, s = w / 9.0f - pad * 2;
    float pts[4 * 2], fx, fy;
    int joins[3] = {NVG_MITER, NVG_ROUND, NVG_BEVEL};
    int caps[3] = {NVG_BUTT, NVG_ROUND, NVG_SQUARE};
    NVG_NOTUSED(h);

    nvgSave(vg);
    pts[0] = -s * 0.25f + cosf(t * 0.3f) * s * 0.5f;
    pts[1] = sinf(t * 0.3f) * s * 0.5f;
    pts[2] = -s * 0.25f;
    pts[3] = 0.0f;
    pts[4] = s * 0.25f;
    pts[5] = 0;
    pts[6] = s * 0.25f + cosf(-t * 0.3f) * s * 0.5f;
    pts[7] = sinf(-t * 0.3f) * s * 0.5f;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            fx = x + s * 0.5f + (i * 3 + j) / 9.0f * w + pad;
            fy = y - s * 0.5f + pad;

            nvgLineCap(vg, caps[i]);
            nvgLineJoin(vg, joins[j]);

            nvgStrokeWidth(vg, strokeWidth);
            nvgStrokeColor(vg, color);
            nvgBeginPath(vg);
            nvgMoveTo(vg, fx + pts[0], fy + pts[1]);
            nvgLineTo(vg, fx + pts[2], fy + pts[3]);
            nvgLineTo(vg, fx + pts[4], fy + pts[5]);
            nvgLineTo(vg, fx + pts[6], fy + pts[7]);
            nvgStroke(vg);

            nvgLineCap(vg, NVG_BUTT);
            nvgLineJoin(vg, NVG_BEVEL);

            nvgStrokeWidth(vg, 1.0f);
            nvgStrokeColor(vg, nvgRGBA(0, 192, 255, 255));
            nvgBeginPath(vg);
            nvgMoveTo(vg, fx + pts[0], fy + pts[1]);
            nvgLineTo(vg, fx + pts[2], fy + pts[3]);
            nvgLineTo(vg, fx + pts[4], fy + pts[5]);
            nvgLineTo(vg, fx + pts[6], fy + pts[7]);
            nvgStroke(vg);
        }
    }

    nvgRestore(vg);
}

// Finds example/<name> in the working directory or up to three of its parents, like the C++ demo.
static int findFile(const char* name, char* out, size_t outSize)
{
    char prefix[16] = "";
    int i;
    for (i = 0; i < 4; i++) {
        FILE* f;
        snprintf(out, outSize, "%sexample/%s", prefix, name);
        f = fopen(out, "rb");
        if (f != NULL) {
            fclose(f);
            return 1;
        }
        strcat(prefix, "../");
    }
    printf("Could not find file: %s\n", name);
    return 0;
}

// Builds a 48x48 "panel" texture for the nine-patch demo: rounded 16px corners, a 2px light border,
// a darker inner fill and a diagonal stripe pattern so stretching of edges/center is easy to see.
static int createNinePatchImage(NVGcontext* vg)
{
    enum { size = 48 };
    const float corner = 16.0f;
    unsigned char px[size * size * 4];
    int x, y;
    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            // Signed distance to a rounded rect covering the whole image.
            const float cx = x + 0.5f, cy = y + 0.5f;
            const float qx = fabsf(cx - size * 0.5f) - (size * 0.5f - corner);
            const float qy = fabsf(cy - size * 0.5f) - (size * 0.5f - corner);
            const float mx = qx > 0.0f ? qx : 0.0f, my = qy > 0.0f ? qy : 0.0f;
            const float inside = qx > qy ? qx : qy;
            const float dist = sqrtf(mx * mx + my * my) + (inside < 0.0f ? inside : 0.0f) - corner;
            const float coverage = clampf(0.5f - dist, 0.0f, 1.0f);
            const int border = dist > -2.5f;
            const int stripe = ((x + y) / 4) % 2 == 0;
            unsigned char* p = &px[(y * size + x) * 4];
            p[0] = border ? 230 : (stripe ? 60 : 48);
            p[1] = border ? 200 : (stripe ? 110 : 90);
            p[2] = border ? 120 : (stripe ? 170 : 140);
            p[3] = (unsigned char)(coverage * 255.0f);
        }
    }
    return nvgCreateImageRGBA(vg, size, size, 0, px);
}

int loadDemoData(NVGcontext* vg, DemoData* data)
{
    int i;
    char file[512];

    if (vg == NULL) return -1;

    for (i = 0; i < 12; i++) {
        char name[64];
        snprintf(name, sizeof(name), "images/image%d.jpg", i + 1);
        if (!findFile(name, file, sizeof(file))) return -1;
        data->images[i] = nvgCreateImage(vg, file, 0);
        if (data->images[i] == 0) {
            printf("Could not load %s.\n", file);
            return -1;
        }
    }

    if (!findFile("entypo.ttf", file, sizeof(file))) return -1;
    data->fontIcons = nvgCreateFont(vg, "icons", file);
    if (data->fontIcons == -1) {
        printf("Could not add font icons.\n");
        return -1;
    }
    if (!findFile("Roboto-Regular.ttf", file, sizeof(file))) return -1;
    data->fontNormal = nvgCreateFont(vg, "sans", file);
    if (data->fontNormal == -1) {
        printf("Could not add font italic.\n");
        return -1;
    }
    if (!findFile("Roboto-Bold.ttf", file, sizeof(file))) return -1;
    data->fontBold = nvgCreateFont(vg, "sans-bold", file);
    if (data->fontBold == -1) {
        printf("Could not add font bold.\n");
        return -1;
    }
    if (!findFile("NotoEmoji-Regular.ttf", file, sizeof(file))) return -1;
    data->fontEmoji = nvgCreateFont(vg, "emoji", file);
    if (data->fontEmoji == -1) {
        printf("Could not add font emoji.\n");
        return -1;
    }
    nvgAddFallbackFontId(vg, data->fontNormal, data->fontEmoji);
    nvgAddFallbackFontId(vg, data->fontBold, data->fontEmoji);

    data->ninePatch = createNinePatchImage(vg);
    if (data->ninePatch == 0) {
        printf("Could not create nine-patch image.\n");
        return -1;
    }

    return 0;
}

void freeDemoData(NVGcontext* vg, DemoData* data)
{
    int i;

    if (vg == NULL) return;

    for (i = 0; i < 12; i++) nvgDeleteImage(vg, data->images[i]);
    if (data->ninePatch != 0) nvgDeleteImage(vg, data->ninePatch);
}

void drawParagraph(NVGcontext* vg, float x, float y, float width, float height, float mx, float my)
{
    NVGtextRow rows[3];
    NVGglyphPosition glyphs[100];
    const char* text =
        "This is longer chunk of text.\n  \n  Would have used lorem ipsum but she    was busy jumping over the lazy dog with the fox and all the men who came to the aid of the party.🎉";
    const char* start;
    const char* end;
    int nrows, i, nglyphs, j, lnum = 0;
    float lineh;
    float caretx, px;
    float bounds[4];
    float a;
    const char* hoverText = "Hover your mouse over the text to see calculated caret position.";
    float gx = 0.0f, gy = 0.0f;
    int gutter = 0;
    NVG_NOTUSED(height);

    nvgSave(vg);

    nvgFontSize(vg, 15.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextMetrics(vg, NULL, NULL, &lineh);

    // The text break API can be used to fill a large buffer of rows,
    // or to iterate over the text just few lines (or just one) at a time.
    // The "next" variable of the last returned item tells where to continue.
    start = text;
    end = text + strlen(text);
    while ((nrows = nvgTextBreakLines(vg, start, end, width, rows, 3, 0))) {
        for (i = 0; i < nrows; i++) {
            NVGtextRow* row = &rows[i];
            int hit = mx > x && mx < (x + width) && my >= y && my < (y + lineh);

            nvgBeginPath(vg);
            nvgFillColor(vg, nvgRGBA(255, 255, 255, hit ? 64 : 16));
            nvgRect(vg, x + row->minx, y, row->maxx - row->minx, lineh);
            nvgFill(vg);

            nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
            nvgText(vg, x, y, row->start, row->end);

            if (hit) {
                caretx = (mx < x + row->width / 2) ? x : x + row->width;
                px = x;
                nglyphs = nvgTextGlyphPositions(vg, x, y, row->start, row->end, glyphs, 100);
                for (j = 0; j < nglyphs; j++) {
                    float x0 = glyphs[j].x;
                    float x1 = (j + 1 < nglyphs) ? glyphs[j + 1].x : x + row->width;
                    float glyphMidX = x0 * 0.3f + x1 * 0.7f;
                    if (mx >= px && mx < glyphMidX) caretx = glyphs[j].x;
                    px = glyphMidX;
                }
                nvgBeginPath(vg);
                nvgFillColor(vg, nvgRGBA(255, 192, 0, 255));
                nvgRect(vg, caretx, y, 1, lineh);
                nvgFill(vg);

                gutter = lnum + 1;
                gx = x - 10;
                gy = y + lineh / 2;
            }
            lnum++;
            y += lineh;
        }
        // Keep going...
        start = rows[nrows - 1].next;
    }

    if (gutter) {
        char txt[16];
        snprintf(txt, sizeof(txt), "%d", gutter);
        nvgFontSize(vg, 12.0f);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);

        nvgTextBounds(vg, gx, gy, txt, NULL, bounds);

        nvgBeginPath(vg);
        nvgFillColor(vg, nvgRGBA(255, 192, 0, 255));
        nvgRoundedRect(vg, (int)bounds[0] - 4, (int)bounds[1] - 2, (int)(bounds[2] - bounds[0]) + 8, (int)(bounds[3] - bounds[1]) + 4, ((int)(bounds[3] - bounds[1]) + 4) / 2 - 1);
        nvgFill(vg);

        nvgFillColor(vg, nvgRGBA(32, 32, 32, 255));
        nvgText(vg, gx, gy, txt, NULL);
    }

    y += 20.0f;

    nvgFontSize(vg, 11.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, 1.2f);

    nvgTextBoxBounds(vg, x, y, 150, hoverText, NULL, bounds);

    // Fade the tooltip out when close to it.
    gx = clampf(mx, bounds[0], bounds[2]) - mx;
    gy = clampf(my, bounds[1], bounds[3]) - my;
    a = sqrtf(gx * gx + gy * gy) / 30.0f;
    a = clampf(a, 0, 1);
    nvgGlobalAlpha(vg, a);

    nvgBeginPath(vg);
    nvgFillColor(vg, nvgRGBA(220, 220, 220, 255));
    nvgRoundedRect(vg, bounds[0] - 2, bounds[1] - 2, (int)(bounds[2] - bounds[0]) + 4, (int)(bounds[3] - bounds[1]) + 4, 3);
    px = (bounds[2] + bounds[0]) * 0.5f;
    nvgMoveTo(vg, px, bounds[1] - 10);
    nvgLineTo(vg, px + 7, bounds[1] + 1);
    nvgLineTo(vg, px - 7, bounds[1] + 1);
    nvgFill(vg);

    nvgFillColor(vg, nvgRGBA(0, 0, 0, 220));
    nvgTextBox(vg, x, y, 150, hoverText, NULL);

    nvgRestore(vg);
}

void drawWidths(NVGcontext* vg, float x, float y, float width)
{
    int i;

    nvgSave(vg);

    nvgStrokeColor(vg, nvgRGBA(230, 230, 240, 255));

    for (i = 0; i < 20; i++) {
        float w = (i + 0.5f) * 0.1f;
        nvgStrokeWidth(vg, w);
        nvgBeginPath(vg);
        nvgMoveTo(vg, x, y);
        nvgLineTo(vg, x + width, y + width * 0.3f);
        nvgStroke(vg);
        y += 10;
    }

    nvgRestore(vg);
}

void drawCaps(NVGcontext* vg, float x, float y, float width)
{
    int i;
    int caps[3] = {NVG_BUTT, NVG_ROUND, NVG_SQUARE};
    float lineWidth = 8.0f;

    nvgSave(vg);

    nvgBeginPath(vg);
    nvgRect(vg, x - lineWidth / 2, y, width + lineWidth, 40);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 32));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, 40);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 32));
    nvgFill(vg);

    nvgStrokeWidth(vg, lineWidth);
    for (i = 0; i < 3; i++) {
        nvgLineCap(vg, caps[i]);
        nvgStrokeColor(vg, nvgRGBA(230, 230, 240, 255));
        nvgBeginPath(vg);
        nvgMoveTo(vg, x, y + i * 10 + 5);
        nvgLineTo(vg, x + width, y + i * 10 + 5);
        nvgStroke(vg);
    }

    nvgRestore(vg);
}

void drawScissor(NVGcontext* vg, float x, float y, float t)
{
    nvgSave(vg);

    // Draw first rect and set scissor to it's area.
    nvgTranslate(vg, x, y);
    nvgRotate(vg, nvgDegToRad(5));
    nvgBeginPath(vg);
    nvgRect(vg, -20, -20, 60, 40);
    nvgFillColor(vg, nvgRGBA(255, 0, 0, 255));
    nvgFill(vg);
    nvgScissor(vg, -20, -20, 60, 40);

    // Draw second rectangle with offset and rotation.
    nvgTranslate(vg, 40, 0);
    nvgRotate(vg, t);

    // Draw the intended second rectangle without any scissoring.
    nvgSave(vg);
    nvgResetScissor(vg);
    nvgBeginPath(vg);
    nvgRect(vg, -20, -10, 60, 30);
    nvgFillColor(vg, nvgRGBA(255, 128, 0, 64));
    nvgFill(vg);
    nvgRestore(vg);

    // Draw second rectangle with combined scissoring.
    nvgIntersectScissor(vg, -20, -10, 60, 30);
    nvgBeginPath(vg);
    nvgRect(vg, -20, -10, 60, 30);
    nvgFillColor(vg, nvgRGBA(255, 128, 0, 255));
    nvgFill(vg);

    nvgRestore(vg);
}

void drawBezierCurve(NVGcontext* vg, float x0, float y0, float radius, float t)
{
    float x1 = x0 + radius * cosf(2.0f * NVG_PI * t / 5.0f);
    float y1 = y0 + radius * sinf(2.0f * NVG_PI * t / 5.0f);

    float cx0 = x0;
    float cy0 = y0 + ((y1 - y0) * 0.75f);
    float cx1 = x1;
    float cy1 = y0 + ((y1 - y0) * 0.75f);

    nvgBeginPath(vg);
    nvgMoveTo(vg, x0, y0);
    nvgLineTo(vg, cx0, cy0);
    nvgLineTo(vg, cx1, cy1);
    nvgLineTo(vg, x1, y1);
    nvgStrokeColor(vg, nvgRGBA(200, 200, 200, 255));
    nvgStrokeWidth(vg, 2.0f);
    nvgStroke(vg);

    nvgLineCap(vg, NVG_ROUND);
    nvgStrokeWidth(vg, 5);
    nvgLineJoin(vg, NVG_ROUND);

    nvgBeginPath(vg);
    nvgMoveTo(vg, x0, y0);
    nvgBezierTo(vg, cx0, cy0, cx1, cy1, x1, y1);
    nvgLineStyle(vg, NVG_LINE_SOLID);
    nvgStrokeColor(vg, nvgRGBA(40, 53, 147, 255));
    nvgStroke(vg);

    nvgLineStyle(vg, NVG_LINE_DASHED);
    nvgStrokeColor(vg, nvgRGBA(255, 195, 0, 255));
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgCircle(vg, x0, y0, 5.0f);
    nvgCircle(vg, cx0, cy0, 5.0f);
    nvgCircle(vg, cx1, cy1, 5.0f);
    nvgCircle(vg, x1, y1, 5.0f);
    nvgLineStyle(vg, NVG_LINE_SOLID);
    nvgFillColor(vg, nvgRGBA(64, 192, 64, 255));
    nvgFill(vg);
}

void drawScaledText(NVGcontext* vg, float x0, float y0, float t)
{
    nvgSave(vg);
    const float scale = (cosf(2.0f * NVG_PI * t * 0.25f) + 1.0f) + 0.1f;
    nvgTranslate(vg, x0, y0);
    nvgScale(vg, scale, scale);
    nvgFontSize(vg, 24.0f);
    nvgFontFace(vg, "sans-bold");
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgBeginPath(vg);
    nvgText(vg, 0.0f, 0.0f, "NanoVG", NULL);
    nvgFill(vg);
    nvgRestore(vg);
}

static NVGrect rect(float x, float y, float w, float h);
static float drawCallsCell(NVGcontext* vg, NVGrect cell, const char* title, const char* calls);

// One tile of the classic demo: its place in the panel grid, the size of the design space its
// content is drawn in (scaled down to fit the tile), and the drawing (mx, my: the mouse in the tile's
// design space).
typedef void (*ClassicTileFn)(NVGcontext* vg, float mx, float my, float t, int blowup, DemoData* data);
typedef struct ClassicTile {
    const char* title;
    const char* calls;
    int col, row, colSpan, rowSpan;
    float designW, designH;
    ClassicTileFn draw;
} ClassicTile;

static void classicWidgets(NVGcontext* vg, float mx, float my, float t, int blowup, DemoData* data)
{
    float x = 10, y = 45, popy;
    NVG_NOTUSED(mx);
    NVG_NOTUSED(my);
    if (blowup) {
        nvgRotate(vg, sinf(t * 0.3f) * 5.0f / 180.0f * NVG_PI);
        nvgScale(vg, 2.0f, 2.0f);
    }
    drawWindow(vg, "Widgets `n Stuff", 0, 0, 300, 400);
    drawSearchBox(vg, "Search", x, y, 280, 25);
    y += 40;
    drawDropDown(vg, "Effects", x, y, 280, 28);
    popy = y + 14;
    y += 45;

    // Form
    drawLabel(vg, "Login", x, y, 280, 20);
    y += 25;
    drawEditBox(vg, "Email", x, y, 280, 28);
    y += 35;
    drawEditBox(vg, "Password", x, y, 280, 28);
    y += 38;
    drawCheckBox(vg, "Remember me", x, y, 140, 28);
    drawButton(vg, ICON_LOGIN, "Sign in", x + 138, y, 140, 28, nvgRGBA(0, 96, 128, 255));
    y += 45;

    // Slider
    drawLabel(vg, "Diameter", x, y, 280, 20);
    y += 25;
    drawEditBoxNum(vg, "123.00", "px", x + 180, y, 100, 28);
    drawSlider(vg, 0.4f, x, y, 170, 28);
    y += 55;

    drawButton(vg, ICON_TRASH, "Delete", x, y, 160, 28, nvgRGBA(128, 16, 8, 255));
    drawButton(vg, 0, "Cancel", x + 170, y, 110, 28, nvgRGBA(0, 0, 0, 0));

    // Thumbnails popup, pointing at the drop-down.
    drawThumbnails(vg, 315, popy - 30, 160, 300, data->images, 12, t);
}

static void classicEyes(NVGcontext* vg, float mx, float my, float t, int blowup, DemoData* data)
{
    NVG_NOTUSED(blowup);
    NVG_NOTUSED(data);
    drawEyes(vg, 20, 20, 150, 100, mx, my, t);
    drawStylizedLines(vg, 5, 5, 180, 130);
}

static void classicColorwheel(NVGcontext* vg, float mx, float my, float t, int blowup, DemoData* data)
{
    NVG_NOTUSED(mx);
    NVG_NOTUSED(my);
    NVG_NOTUSED(blowup);
    NVG_NOTUSED(data);
    drawColorwheel(vg, 0, 0, 250.0f, 250.0f, t);
}

static void classicGraph(NVGcontext* vg, float mx, float my, float t, int blowup, DemoData* data)
{
    NVG_NOTUSED(mx);
    NVG_NOTUSED(my);
    NVG_NOTUSED(blowup);
    NVG_NOTUSED(data);
    drawGraph(vg, 10, 5, 400, 140, t);
}

static void classicParagraph(NVGcontext* vg, float mx, float my, float t, int blowup, DemoData* data)
{
    NVG_NOTUSED(t);
    NVG_NOTUSED(blowup);
    NVG_NOTUSED(data);
    drawParagraph(vg, 10, 10, 150, 100, mx, my);
}

static void classicBezier(NVGcontext* vg, float mx, float my, float t, int blowup, DemoData* data)
{
    NVG_NOTUSED(mx);
    NVG_NOTUSED(my);
    NVG_NOTUSED(blowup);
    NVG_NOTUSED(data);
    drawBezierCurve(vg, 70, 70, 60, t);
}

static void classicLines(NVGcontext* vg, float mx, float my, float t, int blowup, DemoData* data)
{
    static const int styles[3] = {NVG_LINE_DASHED, NVG_LINE_DOTTED, NVG_LINE_GLOW};
    NVG_NOTUSED(mx);
    NVG_NOTUSED(my);
    NVG_NOTUSED(blowup);
    NVG_NOTUSED(data);
    // The line style cycles every 5 seconds.
    nvgLineStyle(vg, styles[(int)(t / 5.0f) % 3]);
    drawLines(vg, 10, 60, 600, 60, 10.0f, nvgRGBA(255, 153, 0, 255), t * 3);
    nvgLineStyle(vg, NVG_LINE_SOLID);
    drawLines(vg, 10, 140, 600, 50, 17.0f, nvgRGBA(220, 220, 235, 160), t);
}

static void classicScissor(NVGcontext* vg, float mx, float my, float t, int blowup, DemoData* data)
{
    NVG_NOTUSED(mx);
    NVG_NOTUSED(my);
    NVG_NOTUSED(blowup);
    NVG_NOTUSED(data);
    drawScissor(vg, 25, 60, t);
}

static void classicWidthsCaps(NVGcontext* vg, float mx, float my, float t, int blowup, DemoData* data)
{
    NVG_NOTUSED(mx);
    NVG_NOTUSED(my);
    NVG_NOTUSED(t);
    NVG_NOTUSED(blowup);
    NVG_NOTUSED(data);
    drawWidths(vg, 10, 10, 60);
    drawCaps(vg, 90, 10, 60);
}

static void classicScaledText(NVGcontext* vg, float mx, float my, float t, int blowup, DemoData* data)
{
    NVG_NOTUSED(mx);
    NVG_NOTUSED(my);
    NVG_NOTUSED(blowup);
    NVG_NOTUSED(data);
    drawScaledText(vg, 110, 45, t);
}

void renderClassicDemo(NVGcontext* vg, float x, float y, float width, float height, float mx, float my, float t, int blowup, DemoData* data)
{
    static const ClassicTile tiles[] = {
        {"Widgets & thumbnail popup", "roundedRect, boxGradient drop shadows, linear / radial gradients, icon-font text, imagePattern thumbnails clipped by scissor, arc spinners",
         0, 0, 2, 3, 480, 410, classicWidgets},
        {"Eyes", "ellipse with radial and linear gradients; LineStyle::Dashed frame", 2, 0, 1, 1, 190, 140, classicEyes},
        {"Color wheel", "arc segments with linear gradients, rotate, boxGradient marker", 3, 0, 1, 1, 290, 250, classicColorwheel},
        {"Graph", "bezierTo curve, linearGradient area fill, shadowed stroke, radialGradient points", 4, 0, 2, 1, 420, 150, classicGraph},
        {"Text paragraph", "textBreakLines, textGlyphPositions caret hit-testing (hover), textBoxBounds, emoji fallback font, textBox tooltip", 2, 1, 1, 2, 170, 225,
         classicParagraph},
        {"Bezier curve", "bezierTo, stroked Solid then LineStyle::Dashed", 3, 1, 1, 1, 140, 140, classicBezier},
        {"Line joins & caps", "Miter / Round / Bevel joins x Butt / Round / Square caps; LineStyle Dashed / Dotted / Glow, cycling", 4, 1, 2, 1, 620, 155, classicLines},
        {"Scissor", "scissor + intersectScissor under rotate", 3, 2, 1, 1, 130, 115, classicScissor},
        {"Widths & caps", "strokeWidth 0.05 - 1.95 (hairlines), lineCap", 4, 2, 1, 1, 160, 225, classicWidthsCaps},
        {"Scaled text", "text() under an animated scale", 5, 2, 1, 1, 220, 90, classicScaledText},
    };
    enum { cols = 6, rows = 3 };
    const float pad = 10.0f, top = 10.0f;
    const float cw = (width - pad * (cols + 1)) / cols;
    const float ch = (height - top - pad * rows) / rows;
    size_t i;

    // Same panel background as the draw-calls panel below.
    nvgFillColor(vg, nvgRGBA(36, 38, 46, 255));
    nvgDrawRect(vg, rect(x, y, width, height), NVG_PAINT_FILL);
    if (cw <= 0.0f || ch <= 0.0f) return;

    for (i = 0; i < sizeof(tiles) / sizeof(tiles[0]); i++) {
        const ClassicTile* tile = &tiles[i];
        NVGrect c;
        float header, areaH, s;
        c.x = x + pad + tile->col * (cw + pad);
        c.y = y + top + tile->row * (ch + pad);
        c.w = cw * tile->colSpan + pad * (tile->colSpan - 1);
        c.h = ch * tile->rowSpan + pad * (tile->rowSpan - 1);
        nvgSave(vg);
        header = drawCallsCell(vg, c, tile->title, tile->calls);
        // Scale the design space down (never up) to fit under the header, centered.
        areaH = c.h - header - 4.0f;
        s = 1.0f;
        if ((c.w - 8.0f) / tile->designW < s) s = (c.w - 8.0f) / tile->designW;
        if (areaH / tile->designH < s) s = areaH / tile->designH;
        if (s > 0.0f) {
            const float ox = c.x + (c.w - tile->designW * s) * 0.5f, oy = c.y + header + (areaH - tile->designH * s) * 0.5f;
            nvgTranslate(vg, ox, oy);
            nvgScale(vg, s, s);
            tile->draw(vg, (mx - ox) / s, (my - oy) / s, t, blowup, data);
        }
        nvgRestore(vg);
    }
}

void renderDemo(NVGcontext* vg, float mx, float my, float width, float height, float t, int blowup, DemoData* data)
{
    // The classic demo uses the top 53% of the window; the Skia-style draw-call showcase fills the rest.
    const float drawCallsHeight = floorf(height * 0.47f);
    renderClassicDemo(vg, 0, 0, width, height - drawCallsHeight, mx, my, t, blowup, data);
    renderDrawCallsDemo(vg, 0, height - drawCallsHeight, width, drawCallsHeight, t, data);
}

// ---------------------------------------------------------------------------------------------
// Draw-calls demo page: one cell per Skia-style immediate draw call (C port of demo.cpp).

static NVGrect rect(float x, float y, float w, float h)
{
    NVGrect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

// Draws a demo tile: background, title and, when given, a line or two naming the calls it exercises.
// Returns the height of that header (the tile's content goes below it).
static float drawCallsCell(NVGcontext* vg, NVGrect cell, const char* title, const char* calls)
{
    const float maxW = cell.w - 16.0f;
    float titleW, b[4];
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 12));
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 40));
    nvgStrokeWidth(vg, 1.0f);
    nvgDrawRoundRect(vg, cell, 6.0f, NVG_PAINT_FILL_AND_STROKE);

    nvgFontSize(vg, 13.0f);
    nvgFontFace(vg, "sans-bold");
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    // Shrink long titles so they stay inside narrow cells.
    titleW = nvgTextBounds(vg, 0, 0, title, NULL, NULL);
    if (titleW > maxW && maxW > 0.0f) nvgFontSize(vg, 13.0f * maxW / titleW);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 180));
    nvgText(vg, cell.x + 8, cell.y + 6, title, NULL);
    if (calls == NULL || maxW <= 0.0f) return 0.0f;

    nvgFontSize(vg, 11.0f);
    nvgFontFace(vg, "sans");
    nvgTextLineHeight(vg, 1.0f);
    nvgFillColor(vg, nvgRGBA(160, 200, 255, 170));
    nvgTextBoxBounds(vg, cell.x + 8, cell.y + 19, maxW, calls, NULL, b);
    nvgTextBox(vg, cell.x + 8, cell.y + 19, maxW, calls, NULL);
    nvgTextLineHeight(vg, 1.0f);
    return b[3] - cell.y + 2.0f;
}

static void demoRects(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    const float y = c.y + 40;
    const float w = (c.w - 40) / 3.0f;
    const float r = 20.0f + 15.0f * sinf(t);
    NVG_NOTUSED(data);
    nvgFillColor(vg, nvgRGBA(255, 160, 0, 255));
    nvgStrokeColor(vg, nvgRGBA(80, 200, 255, 255));
    nvgStrokeWidth(vg, 3.0f);
    nvgDrawRect(vg, rect(c.x + 10, y, w, 50), NVG_PAINT_FILL);
    nvgDrawRect(vg, rect(c.x + 20 + w, y, w, 50), NVG_PAINT_STROKE);
    nvgDrawRoundRect(vg, rect(c.x + 30 + w * 2, y, w, 50), 6.0f + 6.0f * sinf(t * 2.0f), NVG_PAINT_FILL_AND_STROKE);
    nvgDrawRoundRectVarying(vg, rect(c.x + 10, y + 64, c.w - 20, 50), r, 4, r, 4, NVG_PAINT_FILL_AND_STROKE);
}

static void demoDRRect(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    const float inset = 14.0f + 10.0f * sinf(t * 1.5f);
    const NVGrect outer = rect(c.x + 20, c.y + 36, c.w - 40, c.h - 50);
    const NVGrect inner = rect(outer.x + inset, outer.y + inset, outer.w - inset * 2, outer.h - inset * 2);
    NVG_NOTUSED(data);
    nvgFillPaint(vg, nvgLinearGradient(vg, outer.x, outer.y, outer.x + outer.w, outer.y + outer.h, nvgRGBA(255, 90, 120, 255), nvgRGBA(120, 80, 255, 255)));
    nvgDrawDRRect(vg, outer, 24.0f, inner, 24.0f - inset * 0.8f);
}

static void demoCircles(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    const float cy = c.y + c.h * 0.5f + 12;
    const float rx = 30 + 12 * sinf(t), ry = 45 - 12 * sinf(t);
    NVG_NOTUSED(data);
    nvgFillColor(vg, nvgRGBA(120, 220, 120, 255));
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 220));
    nvgStrokeWidth(vg, 3.0f);
    nvgDrawCircle(vg, c.x + 45, cy, 28, NVG_PAINT_FILL);
    nvgDrawCircle(vg, c.x + 110, cy, 22 + 6 * sinf(t * 3.0f), NVG_PAINT_STROKE);
    nvgDrawOval(vg, rect(c.x + c.w - 50 - rx, cy - ry, rx * 2, ry * 2), NVG_PAINT_FILL_AND_STROKE);
}

static void demoArcs(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    const float cy = c.y + c.h * 0.5f + 12;
    const float sweep = NVG_PI * (1.0f + 0.9f * sinf(t));
    const float start = t * 0.7f;
    NVG_NOTUSED(data);
    nvgFillColor(vg, nvgRGBA(255, 200, 40, 255));
    nvgStrokeColor(vg, nvgRGBA(80, 200, 255, 255));
    nvgStrokeWidth(vg, 3.0f);
    nvgDrawArc(vg, c.x + 42, cy, 30, start, sweep, 1, NVG_PAINT_FILL);                    // wedge
    nvgDrawArc(vg, c.x + 117, cy, 30, start, sweep, 0, NVG_PAINT_FILL_AND_STROKE);        // chord + open arc
    nvgLineCap(vg, NVG_ROUND);
    nvgStrokeWidth(vg, 6.0f);
    nvgDrawArc(vg, c.x + 192, cy, 30, -NVG_PI * 0.5f, -sweep, 0, NVG_PAINT_STROKE);       // counter-clockwise
    nvgLineCap(vg, NVG_BUTT);
}

static void demoLines(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    const float cx = c.x + c.w * 0.5f, cy = c.y + c.h * 0.5f + 12;
    int i;
    NVG_NOTUSED(data);
    for (i = 0; i < 12; i++) {
        const float a = t * 0.5f + i * NVG_PI / 6.0f;
        nvgStrokeColor(vg, nvgHSLA(i / 12.0f, 0.7f, 0.6f, 255));
        nvgStrokeWidth(vg, 1.0f + i * 0.4f);
        nvgDrawLine(vg, cx + cosf(a) * 12, cy + sinf(a) * 12, cx + cosf(a) * 55, cy + sinf(a) * 55);
    }
}

static void demoArrows(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    // Left: a fan of filled arrows whose head width, head length and stalk width grow together.
    const float ox = c.x + 22, oy = c.y + c.h - 18;
    const float cx = c.x + c.w - 62, cy = c.y + 75;
    const float a = t * 0.7f;
    const float headW = 22.0f + 10.0f * sinf(t * 2.0f);
    int i;
    NVG_NOTUSED(data);
    for (i = 0; i < 4; i++) {
        const float ai = -NVG_PI * 0.5f + (i + 0.5f) * NVG_PI * 0.125f + sinf(t + i) * 0.08f;
        const float len = 95.0f + 8.0f * sinf(t * 1.3f + i);
        nvgFillColor(vg, nvgHSLA(0.55f + i * 0.08f, 0.7f, 0.6f, 255));
        nvgDrawArrow(vg, ox, oy, ox + cosf(ai) * len, oy + sinf(ai) * len, 2.0f + i * 2.0f, 10.0f + i * 5.0f, 10.0f + i * 4.0f, NVG_PAINT_FILL);
    }

    // Right: a rotating outlined arrow with a breathing head, and a stalk-less head below it.
    nvgFillColor(vg, nvgRGBA(255, 190, 60, 255));
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 230));
    nvgStrokeWidth(vg, 2.0f);
    nvgLineJoin(vg, NVG_ROUND);
    nvgDrawArrow(vg, cx - cosf(a) * 40, cy - sinf(a) * 40, cx + cosf(a) * 40, cy + sinf(a) * 40, 8.0f, headW, 24.0f, NVG_PAINT_FILL_AND_STROKE);
    nvgLineJoin(vg, NVG_MITER);
    nvgStrokeColor(vg, nvgRGBA(120, 230, 140, 255));
    nvgStrokeWidth(vg, 1.5f);
    nvgDrawArrow(vg, cx - 45, c.y + c.h - 18, cx + 45, c.y + c.h - 18, 0.0f, 14.0f, 14.0f, NVG_PAINT_STROKE);
}

static void demoDashes(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    static const float dots[] = {0.0f, 10.0f};
    NVG_NOTUSED(data);
    nvgSave(vg);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 220));
    nvgStrokeWidth(vg, 3.0f);
    nvgStrokeDashArrayString(vg, "12 5");  // plain dashes
    nvgDrawLine(vg, c.x + 15, c.y + 42, c.x + c.w - 15, c.y + 42);

    nvgStrokeColor(vg, nvgRGBA(80, 200, 255, 255));
    nvgStrokeWidth(vg, 5.0f);
    nvgLineCap(vg, NVG_ROUND);
    nvgStrokeDashArray(vg, dots, 2);  // zero-length dashes + round caps = dots
    nvgDrawLine(vg, c.x + 17, c.y + 62, c.x + c.w - 15, c.y + 62);

    nvgStrokeColor(vg, nvgRGBA(255, 160, 0, 255));
    nvgStrokeWidth(vg, 3.0f);
    nvgLineCap(vg, NVG_BUTT);
    nvgStrokeDashArrayString(vg, "14, 4, 3, 4");  // dash-dot
    nvgDrawLine(vg, c.x + 15, c.y + 82, c.x + c.w - 15, c.y + 82);

    // Marching ants: an odd-length array ("8 4 2" -> "8 4 2 8 4 2") with an animated offset, on a curve.
    nvgStrokeColor(vg, nvgRGBA(120, 230, 140, 255));
    nvgStrokeWidth(vg, 2.5f);
    nvgStrokeDashArrayString(vg, "8 4 2");
    nvgStrokeDashOffset(vg, -t * 20.0f);
    nvgBeginPath(vg);
    nvgMoveTo(vg, c.x + 15, c.y + 130);
    nvgBezierTo(vg, c.x + 70, c.y + 80, c.x + 120, c.y + 180, c.x + c.w - 15, c.y + 110);
    nvgStroke(vg);
    nvgRestore(vg);
}

static void demoGrid(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    // Three 4x5 grids side by side, each with its thickness / radius animating.
    const float gw = (c.w - 40) / 3.0f, gy = c.y + 42, gh = c.h - 56;
    NVG_NOTUSED(data);
    nvgFillColor(vg, nvgRGBA(80, 200, 255, 200));
    nvgDrawGrid(vg, rect(c.x + 10, gy, gw, gh), 4, 5, NVG_GRID_LINES, 2.0f + 1.5f * sinf(t * 1.5f), NVG_PAINT_FILL);
    nvgFillColor(vg, nvgRGBA(255, 120, 160, 255));
    nvgDrawGrid(vg, rect(c.x + 20 + gw, gy, gw, gh), 4, 5, NVG_GRID_CIRCLES, 4.0f + 2.0f * sinf(t * 1.5f + 1.0f), NVG_PAINT_FILL);
    nvgFillColor(vg, nvgRGBA(255, 200, 60, 255));
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 200));
    nvgStrokeWidth(vg, 1.0f);
    nvgDrawGrid(vg, rect(c.x + 30 + gw * 2, gy, gw, gh), 4, 5, NVG_GRID_SQUARES, 4.0f + 2.0f * sinf(t * 1.5f + 2.0f), NVG_PAINT_FILL_AND_STROKE);
}

static void ringMarker(NVGcontext* vg, void* uptr)
{
    NVG_NOTUSED(uptr);
    nvgBeginPath(vg);
    nvgCircle(vg, 0.0f, 0.0f, 3.0f);
    nvgStroke(vg);
}

static void demoMarkers(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    NVGmarker head = nvgMarker(NVG_MARKER_ARROW, 4.0f);
    NVGmarker ring = nvgMarker(NVG_MARKER_CUSTOM, 1.0f);
    int i;
    NVG_NOTUSED(data);
    nvgSave(vg);
    // A zigzag with a dot at the start, diamonds at the corners and an arrowhead at the end.
    nvgStrokeColor(vg, nvgRGBA(80, 200, 255, 255));
    nvgStrokeWidth(vg, 2.0f);
    nvgLineJoin(vg, NVG_ROUND);
    nvgMarkerStart(vg, nvgMarker(NVG_MARKER_CIRCLE, 3.0f));
    nvgMarkerMid(vg, nvgMarker(NVG_MARKER_DIAMOND, 3.5f));
    nvgMarkerEnd(vg, nvgMarker(NVG_MARKER_ARROW, 5.0f));
    nvgBeginPath(vg);
    for (i = 0; i < 5; i++) {
        const float x = c.x + 20 + i * (c.w - 50) / 4.0f;
        const float y = c.y + 62 + ((i & 1) ? 18.0f : -12.0f) + 5.0f * sinf(t * 2.0f + i);
        if (i == 0)
            nvgMoveTo(vg, x, y);
        else
            nvgLineTo(vg, x, y);
    }
    nvgStroke(vg);

    // A double-headed curve: auto-start-reverse turns the start arrow around.
    nvgMarkerMid(vg, nvgMarker(NVG_MARKER_NONE, 0.0f));
    head.orient = NVG_MARKER_ORIENT_AUTO_START_REVERSE;
    nvgMarkerStart(vg, head);
    nvgMarkerEnd(vg, head);
    nvgStrokeColor(vg, nvgRGBA(255, 160, 0, 255));
    nvgBeginPath(vg);
    nvgMoveTo(vg, c.x + 25, c.y + 125);
    nvgBezierTo(vg, c.x + 80, c.y + 95 + 25 * sinf(t), c.x + 160, c.y + 155 - 25 * sinf(t), c.x + c.w - 25, c.y + 118);
    nvgStroke(vg);

    // A ruler: tick bars at the inner vertices and custom ring markers at the ends.
    nvgStrokeColor(vg, nvgRGBA(120, 230, 140, 255));
    nvgStrokeWidth(vg, 1.5f);
    nvgMarkerMid(vg, nvgMarker(NVG_MARKER_BAR, 6.0f));
    ring.draw = ringMarker;
    nvgMarkerStart(vg, ring);
    nvgMarkerEnd(vg, ring);
    nvgBeginPath(vg);
    nvgMoveTo(vg, c.x + 25, c.y + 160);
    for (i = 1; i <= 8; i++) nvgLineTo(vg, c.x + 25 + i * (c.w - 50) / 8.0f, c.y + 160);
    nvgStroke(vg);
    nvgRestore(vg);
}

static void demoEllipticalArcs(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    // The four SVG arcs between the same two points (largeArc x sweep), on a rotating ellipse.
    const float x0 = c.x + 55, y0 = c.y + 110, x1 = c.x + 105, y1 = c.y + 85;
    const float rot = 0.4f * sinf(t * 0.8f);
    const float cx = c.x + c.w - 50, cy = c.y + 100;
    NVGcolor colors[4];
    int i;
    NVG_NOTUSED(data);
    colors[0] = nvgRGBA(80, 200, 255, 255);
    colors[1] = nvgRGBA(255, 160, 0, 255);
    colors[2] = nvgRGBA(255, 90, 120, 255);
    colors[3] = nvgRGBA(120, 230, 140, 255);
    nvgStrokeWidth(vg, 2.5f);
    for (i = 0; i < 4; i++) {
        nvgStrokeColor(vg, colors[i]);
        nvgBeginPath(vg);
        nvgMoveTo(vg, x0, y0);
        nvgEllipticalArcTo(vg, 42.0f, 24.0f, rot, (i & 2) != 0, (i & 1) != 0, x1, y1);
        nvgStroke(vg);
    }
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgDrawCircle(vg, x0, y0, 3.0f, NVG_PAINT_FILL);
    nvgDrawCircle(vg, x1, y1, 3.0f, NVG_PAINT_FILL);

    // A wedge cut from a rotated ellipse with the center form.
    nvgFillColor(vg, nvgRGBA(255, 200, 60, 230));
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx, cy);
    nvgEllipticalArc(vg, cx, cy, 38.0f, 22.0f, t * 0.6f, 0.3f, 0.3f + NVG_PI * (1.2f + 0.5f * sinf(t)), NVG_CW);
    nvgClosePath(vg);
    nvgFill(vg);
}

static void demoEllipticalCorners(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    // rx and ry trade places over time: wide, flat corners turn into tall, narrow ones.
    const float k = 0.5f + 0.5f * sinf(t * 1.3f);
    NVG_NOTUSED(data);
    nvgFillPaint(vg, nvgLinearGradient(vg, c.x, c.y + 40, c.x + c.w, c.y + c.h, nvgRGBA(120, 80, 255, 255), nvgRGBA(255, 90, 120, 255)));
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 220));
    nvgStrokeWidth(vg, 2.0f);
    nvgDrawRoundRectElliptic(vg, rect(c.x + 12, c.y + 42, c.w * 0.5f - 18, c.h - 56), 8.0f + 40.0f * k, 48.0f - 40.0f * k, NVG_PAINT_FILL_AND_STROKE);
    // Per-corner elliptical radii.
    nvgFillColor(vg, nvgRGBA(80, 200, 255, 255));
    nvgBeginPath(vg);
    nvgRoundedRectEllipticVarying(vg, c.x + c.w * 0.5f + 6, c.y + 42, c.w * 0.5f - 18, c.h - 56, 50, 20, 4, 4, 30, 60 * k + 5, 10, 30);
    nvgFill(vg);
    nvgStroke(vg);
}

static void demoTextOuterShadow(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    NVG_NOTUSED(data);
    nvgSave(vg);
    nvgFontFace(vg, "sans-bold");
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 28.0f);
    // An outer shadow whose light source moves around.
    nvgTextOuterShadow(vg, 3.0f * cosf(t), 3.0f + 1.5f * sinf(t), 3.0f, nvgRGBA(0, 0, 0, 220));
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgText(vg, c.x + c.w * 0.18f, c.y + 62, "Outer", NULL);
    nvgTextOuterShadow(vg, 0.0f, 0.0f, 0.0f, nvgRGBA(0, 0, 0, 0));
    // An inner shadow: the offset, blurred copy shades the opposite inner edges.
    nvgTextInnerShadow(vg, 1.5f * cosf(t), 1.5f + 0.5f * sinf(t), 1.5f, nvgRGBA(120, 40, 0, 255));
    nvgFillColor(vg, nvgRGBA(255, 200, 60, 255));
    nvgText(vg, c.x + c.w * 0.5f, c.y + 62, "Inner", NULL);
    nvgTextInnerShadow(vg, 0.0f, 0.0f, 0.0f, nvgRGBA(0, 0, 0, 0));
    // No offset and a colored outer shadow make a glow.
    nvgTextOuterShadow(vg, 0.0f, 0.0f, 5.0f, nvgRGBA(80, 200, 255, 255));
    nvgFillColor(vg, nvgRGBA(230, 250, 255, 255));
    nvgText(vg, c.x + c.w * 0.82f, c.y + 62, "Glow", NULL);
    nvgTextOuterShadow(vg, 0.0f, 0.0f, 0.0f, nvgRGBA(0, 0, 0, 0));
    // The classic fontBlur / fontDilate effects, scaled down to fit under them.
    nvgTranslate(vg, c.x + c.w * 0.5f, c.y + 103);
    nvgScale(vg, 0.6f, 0.6f);
    drawFancyText(vg, 0.0f, 0.0f);
    nvgRestore(vg);
}

static void demoTextOnPath(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    float a, cy, r;
    NVG_NOTUSED(data);
    nvgSave(vg);
    nvgFontFace(vg, "sans-bold");
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 50));
    nvgStrokeWidth(vg, 1.0f);
    // A wave whose bends change over time: the glyphs turn to follow it (method="align").
    a = 22.0f * sinf(t * 1.3f);
    nvgBeginPath(vg);
    nvgMoveTo(vg, c.x + 8, c.y + 72);
    nvgBezierTo(vg, c.x + c.w * 0.3f, c.y + 72 - a * 2.0f, c.x + c.w * 0.7f, c.y + 72 + a * 2.0f, c.x + c.w - 8, c.y + 72);
    nvgStroke(vg);
    nvgFontSize(vg, 19.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
    nvgFillColor(vg, nvgRGBA(255, 200, 60, 255));
    nvgTextOnPath(vg, "Text that follows the curve", NULL, 50.0f, 0.0f, NVG_TEXTPATH_PERCENT);
    // Two circles. Left: text bent glyph by glyph (method="stretch") on the outside; the start offset
    // moves and wraps since the path is one closed subpath.
    cy = c.y + 130;
    r = 30.0f;
    nvgBeginPath(vg);
    nvgCircle(vg, c.x + c.w * 0.27f, cy, r);
    nvgStroke(vg);
    nvgFontSize(vg, 14.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgFillColor(vg, nvgRGBA(80, 200, 255, 255));
    nvgTextOnPath(vg, "round and round we go ", NULL, t * 30.0f, -4.0f, NVG_TEXTPATH_STRETCH);
    // Right: side="right" runs the text the other way round, and Middle centers it on the stroke.
    nvgBeginPath(vg);
    nvgCircle(vg, c.x + c.w * 0.73f, cy, r);
    nvgStrokeColor(vg, nvgRGBA(255, 120, 160, 90));
    nvgStrokeWidth(vg, 16.0f);
    nvgStroke(vg);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgTextOnPath(vg, "side right, middle * ", NULL, t * 30.0f, 0.0f, NVG_TEXTPATH_RIGHT);
    nvgRestore(vg);
}

static void demoTextFill(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    float b[4], shift, r;
    NVG_NOTUSED(data);
    nvgSave(vg);
    nvgFontFace(vg, "sans-bold");
    nvgTextFill(vg, NVG_TEXT_FILL_PAINT);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    // A linear gradient across the word's bounds, sliding sideways: the glyphs show the gradient as a
    // rect filled under them would.
    nvgFontSize(vg, 46.0f);
    nvgTextBounds(vg, c.x + c.w * 0.5f, c.y + 68, "Gradient", NULL, b);
    shift = (b[2] - b[0]) * 0.5f * sinf(t);
    nvgFillPaint(vg, nvgLinearGradient(vg, b[0] + shift, 0, b[2] + shift, 0, nvgRGBA(255, 60, 120, 255), nvgRGBA(80, 200, 255, 255)));
    nvgText(vg, c.x + c.w * 0.5f, c.y + 68, "Gradient", NULL);
    // A radial gradient pulsing out from the middle of the second line, and a vertical one below it.
    nvgFontSize(vg, 30.0f);
    r = 20.0f + 30.0f * (0.5f + 0.5f * sinf(t * 1.7f));
    nvgFillPaint(vg, nvgRadialGradient(vg, c.x + c.w * 0.5f, c.y + 115, 4, r, nvgRGBA(255, 240, 120, 255), nvgRGBA(255, 90, 0, 255)));
    nvgText(vg, c.x + c.w * 0.5f, c.y + 115, "radial fill", NULL);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0, c.y + 145, 0, c.y + 165, nvgRGBA(255, 255, 255, 255), nvgRGBA(90, 220, 120, 255)));
    nvgFontSize(vg, 22.0f);
    nvgText(vg, c.x + c.w * 0.5f, c.y + 155, "vertical fade", NULL);
    nvgRestore(vg);
}

static void strokeEffectsWave(NVGcontext* vg, float x, float y, float w)
{
    nvgBeginPath(vg);
    nvgMoveTo(vg, x, y);
    nvgBezierTo(vg, x + w * 0.33f, y - 22, x + w * 0.66f, y + 22, x + w, y);
}

static void strokeEffectsLabel(NVGcontext* vg, float x, float y, const char* s)
{
    nvgFontSize(vg, 12.0f);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 150));
    nvgText(vg, x, y, s, NULL);
}

static void demoStrokeEffects(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    // Four strokes of the same wave, one effect each, with labels.
    const float w = c.w * 0.5f - 30;
    NVG_NOTUSED(data);
    nvgSave(vg);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgStrokeWidth(vg, 9.0f);

    // Outer shadow whose light source moves around (butt caps: the shadow's ends are a blurred box).
    nvgSave(vg);
    nvgLineCap(vg, NVG_BUTT);
    nvgStrokeOuterShadow(vg, 4.0f * cosf(t), 4.0f + 2.0f * sinf(t), 4.0f, nvgRGBA(0, 0, 0, 230));
    nvgStrokeColor(vg, nvgRGBA(255, 200, 60, 255));
    strokeEffectsWave(vg, c.x + 15, c.y + 62, w);
    nvgStroke(vg);
    nvgRestore(vg);
    strokeEffectsLabel(vg, c.x + 15 + w * 0.5f, c.y + 86, "outer shadow");

    // Glow: an unshifted, colored outer shadow, pulsing.
    nvgSave(vg);
    nvgStrokeOuterShadow(vg, 0.0f, 0.0f, 10.0f + 4.0f * sinf(t * 2.0f), nvgRGBA(80, 200, 255, 255));
    nvgStrokeColor(vg, nvgRGBA(230, 250, 255, 255));
    nvgStrokeWidth(vg, 5.0f);
    strokeEffectsWave(vg, c.x + c.w * 0.5f + 15, c.y + 62, w);
    nvgStroke(vg);
    nvgRestore(vg);
    strokeEffectsLabel(vg, c.x + c.w * 0.5f + 15 + w * 0.5f, c.y + 86, "glow");

    // Inner shadow on a thick stroke: the edges facing away from the offset are shaded.
    nvgSave(vg);
    nvgStrokeWidth(vg, 16.0f);
    nvgStrokeInnerShadow(vg, 2.5f * cosf(t), 2.5f * sinf(t), 2.0f, nvgRGBA(90, 20, 60, 255));
    nvgStrokeColor(vg, nvgRGBA(255, 120, 170, 255));
    strokeEffectsWave(vg, c.x + 15, c.y + 130, w);
    nvgStroke(vg);
    nvgRestore(vg);
    strokeEffectsLabel(vg, c.x + 15 + w * 0.5f, c.y + 154, "inner shadow");

    // Outline under the stroke, with a soft blur on both.
    nvgSave(vg);
    nvgStrokeOutline(vg, 2.5f, nvgRGBA(255, 255, 255, 255));
    nvgStrokeBlur(vg, 0.6f + 0.6f * sinf(t * 1.5f));
    nvgStrokeColor(vg, nvgRGBA(90, 220, 120, 255));
    nvgStrokeWidth(vg, 7.0f);
    strokeEffectsWave(vg, c.x + c.w * 0.5f + 15, c.y + 130, w);
    nvgStroke(vg);
    nvgRestore(vg);
    strokeEffectsLabel(vg, c.x + c.w * 0.5f + 15 + w * 0.5f, c.y + 154, "outline + blur");
    nvgRestore(vg);
}

static void demoPoints(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    float wave[2 * 16];
    float ticks[2 * 10];
    int i;
    NVG_NOTUSED(data);
    for (i = 0; i < 16; i++) {
        wave[i * 2] = c.x + 15 + i * (c.w - 30) / 15.0f;
        wave[i * 2 + 1] = c.y + 60 + sinf(t * 2.0f + i * 0.6f) * 18.0f;
    }
    nvgStrokeColor(vg, nvgRGBA(80, 200, 255, 255));
    nvgStrokeWidth(vg, 2.0f);
    nvgDrawPoints(vg, NVG_POINTS_POLYGON, wave, 16);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgStrokeWidth(vg, 7.0f);
    nvgLineCap(vg, NVG_ROUND);
    nvgDrawPoints(vg, NVG_POINTS_POINTS, wave, 16);  // round dots

    for (i = 0; i < 5; i++) {
        const float x = c.x + 20 + i * (c.w - 40) / 4.0f;
        ticks[i * 4 + 0] = x;
        ticks[i * 4 + 1] = c.y + c.h - 40;
        ticks[i * 4 + 2] = x + 10 * sinf(t + i);
        ticks[i * 4 + 3] = c.y + c.h - 12;
    }
    nvgLineCap(vg, NVG_BUTT);
    nvgStrokeColor(vg, nvgRGBA(255, 160, 0, 255));
    nvgStrokeWidth(vg, 3.0f);
    nvgDrawPoints(vg, NVG_POINTS_LINES, ticks, 10);  // independent segments
    nvgStrokeWidth(vg, 8.0f);
    nvgDrawPoint(vg, c.x + c.w - 16, c.y + 22);  // square dot (butt cap)
}

static void demoPolygon(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    const float cx = c.x + c.w * 0.5f, cy = c.y + c.h * 0.5f + 12;
    float star[2 * 10];
    int i;
    NVG_NOTUSED(data);
    for (i = 0; i < 10; i++) {
        const float a = t * 0.6f + i * NVG_PI / 5.0f - NVG_PI * 0.5f;
        const float r = (i % 2 == 0) ? 55.0f : 24.0f;
        star[i * 2] = cx + cosf(a) * r;
        star[i * 2 + 1] = cy + sinf(a) * r;
    }
    nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy, 5, 55, nvgRGBA(255, 240, 120, 255), nvgRGBA(255, 120, 0, 255)));
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 230));
    nvgStrokeWidth(vg, 2.0f);
    nvgLineJoin(vg, NVG_ROUND);
    nvgDrawPolygon(vg, star, 10, NVG_PAINT_FILL_AND_STROKE);
    nvgLineJoin(vg, NVG_MITER);
}

static void demoPaintAndColor(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    const float shift = c.w * 0.5f * sinf(t);
    NVG_NOTUSED(data);
    // nvgDrawPaint / nvgDrawColor cover the whole viewport; scissor them down to the cell.
    nvgSave(vg);
    nvgScissor(vg, c.x + 10, c.y + 32, c.w - 20, (c.h - 42) * 0.5f - 4);
    nvgDrawPaint(vg, nvgLinearGradient(vg, c.x + shift, 0, c.x + c.w + shift, 0, nvgRGBA(255, 60, 60, 255), nvgRGBA(60, 60, 255, 255)));
    nvgRestore(vg);

    nvgSave(vg);
    nvgScissor(vg, c.x + 10, c.y + 32 + (c.h - 42) * 0.5f, c.w - 20, (c.h - 42) * 0.5f);
    nvgDrawColor(vg, nvgHSLA(fmodf(t * 0.1f, 1.0f), 0.6f, 0.5f, 255));
    nvgRestore(vg);
}

static void demoImages(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    const int image = data->images[4];
    int iw = 0, ih = 0;
    float sx, sy;
    NVGrect src, dst;
    nvgImageSize(vg, image, &iw, &ih);
    // Full image at its natural size, scaled down via the transform.
    nvgSave(vg);
    nvgTranslate(vg, c.x + 10, c.y + 34);
    nvgScale(vg, 0.8f, 0.8f);
    nvgDrawImage(vg, image, 0, 0, 1.0f);
    nvgRestore(vg);
    // A 50x50 window panning across the image, magnified into the right half.
    sx = (iw - 50) * (0.5f + 0.5f * sinf(t));
    sy = (ih - 50) * (0.5f + 0.5f * cosf(t * 0.7f));
    src = rect(sx, sy, 50, 50);
    dst = rect(c.x + c.w - 10 - 100, c.y + 34, 100, 100);
    nvgDrawImageRect(vg, image, &src, dst, 1.0f);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 160));
    nvgStrokeWidth(vg, 1.0f);
    nvgDrawRect(vg, rect(c.x + 10 + sx * 0.8f, c.y + 34 + sy * 0.8f, 40, 40), NVG_PAINT_STROKE);
    nvgDrawRect(vg, dst, NVG_PAINT_STROKE);
}

static void demoNinePatch(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    const float w = 60 + (c.w - 80) * (0.5f + 0.5f * sinf(t * 0.8f));
    const float h = 40 + (c.h - 82) * (0.5f + 0.5f * cosf(t * 0.6f));
    nvgDrawImageNine(vg, data->ninePatch, rect(16, 16, 16, 16), rect(c.x + (c.w - w) * 0.5f, c.y + 32 + (c.h - 42 - h) * 0.5f, w, h), 1.0f);
}

static NVGvertex meshVertex(int i, int j, float x0, float y0, float gw, float gh, int nx, int ny, float t)
{
    NVGvertex v;
    v.x = x0 + i * gw + sinf(t * 2.0f + j * 0.9f) * 6.0f;
    v.y = y0 + j * gh + cosf(t * 1.7f + i * 0.8f) * 5.0f;
    v.u = (float)i / nx;
    v.v = (float)j / ny;
    v.s = 0.0f;
    v.t = 0.0f;
    return v;
}

static void demoVertices(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    // A waving 6x4 textured mesh: two triangles per quad.
    enum { nx = 6, ny = 4 };
    const float x0 = c.x + 20, y0 = c.y + 40;
    const float gw = (c.w - 40) / nx, gh = (c.h - 60) / ny;
    NVGvertex verts[nx * ny * 6];
    int i, j, n = 0;
    for (j = 0; j < ny; j++) {
        for (i = 0; i < nx; i++) {
            verts[n++] = meshVertex(i, j, x0, y0, gw, gh, nx, ny, t);
            verts[n++] = meshVertex(i + 1, j, x0, y0, gw, gh, nx, ny, t);
            verts[n++] = meshVertex(i, j + 1, x0, y0, gw, gh, nx, ny, t);
            verts[n++] = meshVertex(i + 1, j, x0, y0, gw, gh, nx, ny, t);
            verts[n++] = meshVertex(i + 1, j + 1, x0, y0, gw, gh, nx, ny, t);
            verts[n++] = meshVertex(i, j + 1, x0, y0, gw, gh, nx, ny, t);
        }
    }
    nvgDrawVertices(vg, verts, n, data->images[2], 1.0f);
}

static void demoClear(NVGcontext* vg, NVGrect c, float t, const DemoData* data)
{
    // Checkerboard background, then nvgClear() a moving window: it replaces the pixels outright,
    // ignoring the 25% global alpha that would otherwise make a fill translucent.
    const NVGrect area = rect(c.x + 10, c.y + 32, c.w - 20, c.h - 42);
    const float x = area.x + (area.w * 0.5f - 10) * (0.5f + 0.5f * sinf(t));
    int i, j;
    NVG_NOTUSED(data);
    for (j = 0; j < 6; j++)
        for (i = 0; i < 12; i++) {
            nvgFillColor(vg, (i + j) % 2 ? nvgRGBA(200, 200, 200, 255) : nvgRGBA(90, 90, 90, 255));
            nvgDrawRect(vg, rect(area.x + i * area.w / 12, area.y + j * area.h / 6, area.w / 12, area.h / 6), NVG_PAINT_FILL);
        }
    nvgSave(vg);
    nvgGlobalAlpha(vg, 0.25f);
    nvgFillColor(vg, nvgRGBA(255, 80, 80, 255));
    nvgDrawRect(vg, rect(x, area.y + 8, area.w * 0.5f, area.h * 0.35f), NVG_PAINT_FILL);  // translucent fill
    nvgScissor(vg, x, area.y + area.h * 0.55f, area.w * 0.5f, area.h * 0.35f);
    nvgClear(vg, nvgRGBA(255, 80, 80, 255));  // opaque despite globalAlpha
    nvgRestore(vg);
}

typedef void (*DrawCallsDemoFn)(NVGcontext* vg, NVGrect c, float t, const DemoData* data);

void renderDrawCallsDemo(NVGcontext* vg, float x, float y, float width, float height, float t, DemoData* data)
{
    enum { cols = 8, rows = 3, count = 22 };
    // Each cell's content is authored in a fixed design space and scaled to fit the cell.
    const float designW = 240.0f, designH = 175.0f;
    const float pad = 10.0f, top = 10.0f;
    const float cw = (width - pad * (cols + 1)) / cols;
    const float ch = (height - top - pad * rows) / rows;
    static const char* titles[count] = {
        "drawRect / drawRoundRect", "drawDRRect", "drawCircle / drawOval", "drawArc", "drawLine", "drawArrow", "strokeDashArray", "markerStart / Mid / End",
        "ellipticalArcTo / Arc", "drawRoundRect(rx, ry)", "drawPoints / drawPoint", "drawPolygon", "drawPaint / drawColor", "drawImage / drawImageRect", "drawImageNine",
        "drawVertices", "clear", "drawGrid", "textOuterShadow / Inner", "textOnPath", "textFill(Paint): gradient text", "strokeOuterShadow / Inner / Outline",
    };
    static const DrawCallsDemoFn draws[count] = {
        demoRects, demoDRRect, demoCircles, demoArcs, demoLines, demoArrows, demoDashes, demoMarkers, demoEllipticalArcs, demoEllipticalCorners, demoPoints, demoPolygon,
        demoPaintAndColor, demoImages, demoNinePatch, demoVertices, demoClear, demoGrid, demoTextOuterShadow, demoTextOnPath, demoTextFill, demoStrokeEffects,
    };
    float s;
    int i;

    // Panel background and top edge.
    nvgFillColor(vg, nvgRGBA(36, 38, 46, 255));
    nvgDrawRect(vg, rect(x, y, width, height), NVG_PAINT_FILL);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 30));
    nvgStrokeWidth(vg, 1.0f);
    nvgDrawLine(vg, x, y + 0.5f, x + width, y + 0.5f);

    if (cw <= 0.0f || ch <= 0.0f) return;
    s = cw / designW < ch / designH ? cw / designW : ch / designH;

    for (i = 0; i < count; i++) {
        const NVGrect c = rect(x + pad + (i % cols) * (cw + pad), y + top + (i / cols) * (ch + pad), cw, ch);
        nvgSave(vg);
        drawCallsCell(vg, c, titles[i], NULL);
        // Center the scaled design-space box in the cell.
        nvgTranslate(vg, c.x + (cw - designW * s) * 0.5f, c.y + (ch - designH * s) * 0.5f);
        nvgScale(vg, s, s);
        draws[i](vg, rect(0, 0, designW, designH), t, data);
        nvgRestore(vg);
    }
}

static int mini(int a, int b)
{
    return a < b ? a : b;
}

static void unpremultiplyAlpha(unsigned char* image, int w, int h, int stride)
{
    int x, y;

    // Unpremultiply
    for (y = 0; y < h; y++) {
        unsigned char* row = &image[y * stride];
        for (x = 0; x < w; x++) {
            int r = row[0], g = row[1], b = row[2], a = row[3];
            if (a != 0) {
                row[0] = (unsigned char)mini(r * 255 / a, 255);
                row[1] = (unsigned char)mini(g * 255 / a, 255);
                row[2] = (unsigned char)mini(b * 255 / a, 255);
            }
            row += 4;
        }
    }

    // Defringe
    for (y = 0; y < h; y++) {
        unsigned char* row = &image[y * stride];
        for (x = 0; x < w; x++) {
            int r = 0, g = 0, b = 0, a = row[3], n = 0;
            if (a == 0) {
                if (x - 1 > 0 && row[-1] != 0) {
                    r += row[-4];
                    g += row[-3];
                    b += row[-2];
                    n++;
                }
                if (x + 1 < w && row[7] != 0) {
                    r += row[4];
                    g += row[5];
                    b += row[6];
                    n++;
                }
                if (y - 1 > 0 && row[-stride + 3] != 0) {
                    r += row[-stride];
                    g += row[-stride + 1];
                    b += row[-stride + 2];
                    n++;
                }
                if (y + 1 < h && row[stride + 3] != 0) {
                    r += row[stride];
                    g += row[stride + 1];
                    b += row[stride + 2];
                    n++;
                }
                if (n > 0) {
                    row[0] = (unsigned char)(r / n);
                    row[1] = (unsigned char)(g / n);
                    row[2] = (unsigned char)(b / n);
                }
            }
            row += 4;
        }
    }
}

static void setAlpha(unsigned char* image, int w, int h, int stride, unsigned char a)
{
    int x, y;
    for (y = 0; y < h; y++) {
        unsigned char* row = &image[y * stride];
        for (x = 0; x < w; x++) row[x * 4 + 3] = a;
    }
}

static void flipHorizontal(unsigned char* image, int w, int h, int stride)
{
    int i = 0, j = h - 1, k;
    while (i < j) {
        unsigned char* ri = &image[i * stride];
        unsigned char* rj = &image[j * stride];
        for (k = 0; k < w * 4; k++) {
            unsigned char t = ri[k];
            ri[k] = rj[k];
            rj[k] = t;
        }
        i++;
        j--;
    }
}

// Compares against an existing PNG of the same name; writes <name>-diff.png on mismatch. Returns 1 when they match
// (or there is nothing to compare with).
static int comparePreviousScreenShot(int nw, int nh, int premult, const unsigned char* nimg, const char* name)
{
    int w, h, n, ret = 1;
    size_t idx, size;
    unsigned char *img, *diff;
    const int threshold = 2;
    stbi_set_unpremultiply_on_load(premult);
    stbi_convert_iphone_png_to_rgb(1);
    img = stbi_load(name, &w, &h, &n, 4);
    if (img == NULL) {
        printf("Skipped screenshot comparison since there is no previous screenshot to compare to.\n");
        return 1;
    }
    if (w != nw || h != nh) {
        printf("Screenshot does not match previous screenshot size %d x %d vs %d x %d.\n", w, h, nw, nh);
        stbi_image_free(img);
        return 0;
    }
    size = (size_t)w * h * 4;
    diff = (unsigned char*)malloc(size);
    for (idx = 0; idx < size; idx += 4) {
        int rd = abs(img[idx] - nimg[idx]), gd = abs(img[idx + 1] - nimg[idx + 1]);
        int bd = abs(img[idx + 2] - nimg[idx + 2]), ad = abs(img[idx + 3] - nimg[idx + 3]);
        diff[idx] = (unsigned char)mini(rd * 4, 255);
        diff[idx + 1] = (unsigned char)mini(gd * 4, 255);
        diff[idx + 2] = (unsigned char)mini(bd * 4, 255);
        diff[idx + 3] = 255;
        if (rd > threshold || gd > threshold || bd > threshold || ad > threshold) ret = 0;
    }
    stbi_image_free(img);
    if (!ret) {
        char diffName[512];
        const char* dot = strrchr(name, '.');
        int stem = dot ? (int)(dot - name) : (int)strlen(name);
        snprintf(diffName, sizeof(diffName), "%.*s-diff%s", stem, name, dot ? dot : "");
        printf("Saving screenshot diff to '%s'\n", diffName);
        stbi_write_png(diffName, w, h, 4, diff, w * 4);
    }
    free(diff);
    return ret;
}

int saveScreenShotRGBA(int w, int h, int premult, unsigned char* image, const char* name, int compare)
{
    char fileName[512];
    int ret = 1;
    if (premult)
        unpremultiplyAlpha(image, w, h, w * 4);
    else
        setAlpha(image, w, h, w * 4, 255);
    snprintf(fileName, sizeof(fileName), "%s", name);
    if (compare) {
        ret = comparePreviousScreenShot(w, h, premult, image, name);
        if (!ret) {
            const char* dot = strrchr(name, '.');
            int stem = dot ? (int)(dot - name) : (int)strlen(name);
            snprintf(fileName, sizeof(fileName), "%.*s-failed%s", stem, name, dot ? dot : "");
        }
    }
    printf("Saving screenshot to '%s'\n", fileName);
    stbi_write_png(fileName, w, h, 4, image, w * 4);
    return ret;
}

#ifndef NANOVG_VULKAN
int saveScreenShot(int w, int h, int premult, const char* name, int compare)
{
    int ret;
    unsigned char* image = (unsigned char*)malloc(w * h * 4);
    if (image == NULL) return 0;
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, image);
    flipHorizontal(image, w, h, w * 4);
    ret = saveScreenShotRGBA(w, h, premult, image, name, compare);
    free(image);
    return ret;
}
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif
