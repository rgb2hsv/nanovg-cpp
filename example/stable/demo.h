#ifndef DEMO_H
#define DEMO_H

#include "nanovg.h"

#ifdef __cplusplus
extern "C" {
#endif

struct DemoData {
    int fontNormal, fontBold, fontIcons, fontEmoji;
    int images[12];
    int ninePatch;  // Procedurally generated panel image used by the nine-patch demo.
};
typedef struct DemoData DemoData;

int loadDemoData(NVGcontext* vg, DemoData* data);
void freeDemoData(NVGcontext* vg, DemoData* data);
void renderDemo(NVGcontext* vg, float mx, float my, float width, float height, float t, int blowup, DemoData* data);

// Showcase of the Skia-style immediate draw calls (nvgDrawRect, nvgDrawArc, nvgDrawImageNine, ...) inside the given rect.
// renderDemo() calls this for the bottom part of the window.
void renderDrawCallsDemo(NVGcontext* vg, float x, float y, float width, float height, float t, DemoData* data);

// Reads back the current GL framebuffer (GL examples only). With compare, diffs against an existing file of the
// same name. Returns 0 on mismatch.
int saveScreenShot(int w, int h, int premult, const char* name, int compare);
// Saves top-down RGBA8 pixels (modified in place), optionally comparing like saveScreenShot.
int saveScreenShotRGBA(int w, int h, int premult, unsigned char* image, const char* name, int compare);

#ifdef __cplusplus
}
#endif

#endif  // DEMO_H
