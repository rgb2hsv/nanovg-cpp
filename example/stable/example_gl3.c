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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef NANOVG_GLEW
#include <GL/glew.h>
#endif
#ifdef __APPLE__
#define GLFW_INCLUDE_GLCOREARB
#endif
#define GLFW_INCLUDE_GLEXT
#include <GLFW/glfw3.h>
#include "nanovg.h"
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"
#include "demo.h"
#include "perf.h"

void errorcb(int error, const char* desc)
{
    printf("GLFW error %d: %s\n", error, desc);
}

int blowup = 0;
int screenshot = 0;
int premult = 0;

static void key(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    NVG_NOTUSED(scancode);
    NVG_NOTUSED(mods);
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(window, GL_TRUE);
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) blowup = !blowup;
    if (key == GLFW_KEY_S && action == GLFW_PRESS) screenshot = 1;
    if (key == GLFW_KEY_P && action == GLFW_PRESS) premult = !premult;
}

static int parseTestCount(const char* s, int* out)
{
    char* endptr = NULL;
    long v = strtol(s, &endptr, 10);
    if (endptr == s || (endptr && *endptr != '\0') || v < 0) return 0;
    *out = (int)v;
    return 1;
}

// Usage: example_stable_gl3 [--test N]  (same capture/compare mode as the C++ example_gl3)
int main(int argc, char** argv)
{
    GLFWwindow* window;
    DemoData data;
    NVGcontext* vg = NULL;
    GPUtimer gpuTimer;
    PerfGraph fps, cpuGraph, gpuGraph;
    double prevt = 0, cpuTime = 0;
    int testCount = 0, testRemaining, testRequested = 0, success = 1, i;
    long long frameCounter = 0;
    const int sampleRate = 5;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--test")) {
            if (i + 1 >= argc || !parseTestCount(argv[i + 1], &testCount)) {
                printf("Invalid --test N value.\n");
                return -1;
            }
            testRequested = 1;
            i++;
        } else if (!strncmp(argv[i], "--test=", 7)) {
            if (!parseTestCount(argv[i] + 7, &testCount)) {
                printf("Invalid --test N value.\n");
                return -1;
            }
            testRequested = 1;
        }
    }
    testRemaining = testCount;

    if (!glfwInit()) {
        printf("Failed to init GLFW.");
        return -1;
    }

    initGraph(&fps, GRAPH_RENDER_FPS, "Frame Time");
    initGraph(&cpuGraph, GRAPH_RENDER_MS, "CPU Time");
    initGraph(&gpuGraph, GRAPH_RENDER_MS, "GPU Time");

    glfwSetErrorCallback(errorcb);
#ifndef _WIN32  // don't require this on win32, and works with more cards
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, 1);

#ifdef DEMO_MSAA
    glfwWindowHint(GLFW_SAMPLES, 4);
#endif
    window = glfwCreateWindow(1200, 1000, "NanoVG", NULL, NULL);
    //	window = glfwCreateWindow(1200, 1000, "NanoVG", glfwGetPrimaryMonitor(), NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwSetKeyCallback(window, key);

    glfwMakeContextCurrent(window);
#ifdef NANOVG_GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        printf("Could not init glew.\n");
        return -1;
    }
    // GLEW generates GL error because it calls glGetString(GL_EXTENSIONS), we'll consume it here.
    glGetError();
#endif

#ifdef DEMO_MSAA
    vg = nvgCreateGL3(NVG_STENCIL_STROKES | NVG_DEBUG);
#else
    vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
#endif
    if (vg == NULL) {
        printf("Could not init nanovg.\n");
        return -1;
    }

    if (loadDemoData(vg, &data) == -1) return -1;

    if (testRequested && testCount == 0) glfwSetWindowShouldClose(window, GL_TRUE);

    glfwSwapInterval(0);

    initGPUTimer(&gpuTimer);

    glfwSetTime(0);
    prevt = testRequested ? 0.0 : glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        double mx, my, t, dt;
        int winWidth, winHeight;
        int fbWidth, fbHeight;
        float pxRatio;
        float gpuTimes[3];
        int n;

        t = testRequested ? frameCounter / 30.0 : glfwGetTime();
        dt = t - prevt;
        prevt = t;

        startGPUTimer(&gpuTimer);

        glfwGetWindowSize(window, &winWidth, &winHeight);
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        if (testRequested) {
            double angle = 2.0 * NVG_PI * testRemaining / (double)testCount;
            mx = winWidth / 2.0f + cos(angle) * winWidth / 4.0f;
            my = winHeight / 2.0f + sin(angle) * winHeight / 4.0f;
        } else {
            glfwGetCursorPos(window, &mx, &my);
        }
        // Calculate pixel ration for hi-dpi devices.
        pxRatio = (float)fbWidth / (float)winWidth;

        // Update and render
        glViewport(0, 0, fbWidth, fbHeight);
        if (premult)
            glClearColor(0, 0, 0, 0);
        else
            glClearColor(0.3f, 0.3f, 0.32f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        nvgBeginFrame(vg, (float)winWidth, (float)winHeight, pxRatio);

        renderDemo(vg, (float)mx, (float)my, (float)winWidth, (float)winHeight, (float)t, blowup, &data);

        if (!testRequested) {
            renderGraph(vg, 5, 5, &fps);
            renderGraph(vg, 5 + 200 + 5, 5, &cpuGraph);
            if (gpuTimer.supported) renderGraph(vg, 5 + 200 + 5 + 200 + 5, 5, &gpuGraph);
        }

        nvgEndFrame(vg);

        // Measure the CPU time taken excluding swap buffers (as the swap may wait for GPU)
        cpuTime = glfwGetTime() - t;

        updateGraph(&fps, (float)dt);
        updateGraph(&cpuGraph, (float)cpuTime);

        // We may get multiple results.
        n = stopGPUTimer(&gpuTimer, gpuTimes, 3);
        for (i = 0; i < n; i++) updateGraph(&gpuGraph, gpuTimes[i]);

        if (testRemaining > 0 && frameCounter % sampleRate == 0) {
            char fileName[64];
            printf("Capturing %d/%d at t = %0.2f sec\n", testCount - testRemaining + 1, testCount, t);
            snprintf(fileName, sizeof(fileName), "screenshot%03d.png", testCount - testRemaining + 1);
            success &= saveScreenShot(fbWidth, fbHeight, 0, fileName, 1);
            testRemaining--;
            if (testRemaining == 0) glfwSetWindowShouldClose(window, GL_TRUE);
        } else if (screenshot && !testRequested) {
            screenshot = 0;
            success &= saveScreenShot(fbWidth, fbHeight, premult, "dump.png", 0);
        }
        frameCounter++;

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    freeDemoData(vg, &data);

    nvgDeleteGL3(vg);

    if (testRequested) {
        printf(success ? "Test passed!\n" : "Test failed!\n");
    } else {
        printf("Average Frame Time: %.2f ms\n", getGraphAverage(&fps) * 1000.0f);
        printf("          CPU Time: %.2f ms\n", getGraphAverage(&cpuGraph) * 1000.0f);
        printf("          GPU Time: %.2f ms\n", getGraphAverage(&gpuGraph) * 1000.0f);
    }

    glfwTerminate();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
