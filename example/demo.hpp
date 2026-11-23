#pragma once
#include "nanovg.hpp"
#include <array>
#include <string>

struct DemoData {
    int fontNormal, fontBold, fontIcons, fontEmoji;
    std::array<int, 12> images{};
    int ninePatch = 0;  // Procedurally generated panel image used by the nine-patch demo.
};

int loadDemoData(nvg::Context& vg, DemoData& data);
void freeDemoData(nvg::Context& vg, DemoData& data);
void renderDemo(nvg::Context& vg, float mx, float my, float width, float height, float t, int blowup, DemoData& data);
// Showcase of the Skia-style immediate draw calls (drawRect, drawArc, drawImageNine, ...) inside the given rect.
// renderDemo() calls this for the bottom part of the window.
void renderDrawCallsDemo(nvg::Context& vg, float x, float y, float width, float height, float t, DemoData& data);

bool saveScreenShot(int w, int h, int premult, const std::string& name, bool compare = false);
