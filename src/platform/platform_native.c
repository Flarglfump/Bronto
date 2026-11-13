/*
 * Native backend selector: compiles in the backend for the target OS.
 *
 *   Windows        platform_win32.c  Win32 + GDI
 *   macOS          platform_macos.c  Cocoa (from C via the Objective-C runtime) + Core Animation
 *   Linux / BSD    platform_x11.c    X11, loaded at run time with dlopen
 *   Web            platform_web.c    a <canvas> in the page, with tools/web/bronto.js
 *
 * and the terminal backend (platform_terminal.c), which draws in the terminal
 * and borrows the OS backend's sound and gamepad code, so it comes after it.
 *
 * Define BR_NO_NATIVE_PLATFORM to build without any (headless only).
 */
#if !defined(_WIN32) && !defined(__wasm__)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L   /* clock_gettime, nanosleep, dlopen */
#  endif
#  if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#    define _DARWIN_C_SOURCE
#  endif
#endif

#include "bronto/platform.h"
#include "bronto/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the GPU choice (platform.h), shared by every backend ------------------------------- */

static const char *const k_gpu_names[BR_GPU_COUNT] = { "auto", "software", "opengl", "vulkan", "metal", "d3d12" };
static const char *const k_gpu_titles[BR_GPU_COUNT] = { "Automatic", "Software", "OpenGL", "Vulkan", "Metal", "Direct3D 12" };
static br_gpu g_gpu_active = BR_GPU_SOFTWARE;
static char g_gpu_info[160];

const char *br_gpu_name(br_gpu gpu) { return (unsigned)gpu < BR_GPU_COUNT ? k_gpu_names[gpu] : "auto"; }
const char *br_gpu_title(br_gpu gpu) { return (unsigned)gpu < BR_GPU_COUNT ? k_gpu_titles[gpu] : "Automatic"; }

static bool gpu_same(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b) {
        char x = (char)(*a >= 'A' && *a <= 'Z' ? *a + 32 : *a), y = (char)(*b >= 'A' && *b <= 'Z' ? *b + 32 : *b);
        if (x != y) return false;
    }
    return *a == *b;
}

bool br_gpu_parse(const char *name, br_gpu *gpu) {
    static const struct { const char *name; br_gpu gpu; } more[] = {
        { "gl", BR_GPU_OPENGL }, { "dx12", BR_GPU_D3D12 }, { "d3d", BR_GPU_D3D12 }, { "direct3d", BR_GPU_D3D12 },
        { "directx", BR_GPU_D3D12 }, { "vk", BR_GPU_VULKAN }, { "none", BR_GPU_SOFTWARE }, { "cpu", BR_GPU_SOFTWARE },
    };
    size_t i;
    if (!name) return false;
    for (i = 0; i < BR_GPU_COUNT; ++i) if (gpu_same(name, k_gpu_names[i])) { *gpu = (br_gpu)i; return true; }
    for (i = 0; i < sizeof(more) / sizeof(more[0]); ++i) if (gpu_same(name, more[i].name)) { *gpu = more[i].gpu; return true; }
    return false;
}

bool br_gpu_exists_on(br_gpu gpu, const char *os) {
    bool win = os && gpu_same(os, "windows"), mac = os && gpu_same(os, "macos"), lin = os && (gpu_same(os, "linux") || gpu_same(os, "bsd"));
    switch (gpu) {
    case BR_GPU_AUTO: case BR_GPU_SOFTWARE: return win || mac || lin;
    case BR_GPU_OPENGL: return win || mac || lin;
    case BR_GPU_VULKAN: return win || lin;
    case BR_GPU_METAL: return mac;
    case BR_GPU_D3D12: return win;
    default: return false;
    }
}

br_gpu br_gpu_active(void) { return g_gpu_active; }
const char *br_gpu_info(void) { return g_gpu_info; }

void br_gpu_set_active(br_gpu gpu, const char *info) {
    g_gpu_active = gpu;
    snprintf(g_gpu_info, sizeof(g_gpu_info), "%.150s", info ? info : "");
}

int br_gpu_order(br_gpu requested, const br_gpu *auto_order, int auto_count, br_gpu *out, int max) {
    const char *env = getenv("BRONTO_GPU");
    br_gpu want = requested;
    int n = 0, i, k;
    if (env && env[0]) {
        if (br_gpu_parse(env, &want)) BR_INFO("BRONTO_GPU=%s: presenting with %s", env, br_gpu_title(want));
        else BR_WARN("BRONTO_GPU=%s isn't one of auto, software, opengl, vulkan, metal, d3d12 (ignored)", env);
        if (!br_gpu_parse(env, &want)) want = requested;
    }
    if ((unsigned)want >= BR_GPU_COUNT) want = BR_GPU_AUTO;
    if (want == BR_GPU_SOFTWARE) { if (max > 0) out[n++] = BR_GPU_SOFTWARE; return n; }
    if (want != BR_GPU_AUTO && n < max) out[n++] = want;            /* the one asked for first ... */
    for (i = 0; i < auto_count && n < max; ++i) {                 /* ... then the rest, in this system's order */
        bool dup = false;
        for (k = 0; k < n; ++k) if (out[k] == auto_order[i]) dup = true;
        if (!dup) out[n++] = auto_order[i];
    }
    if (n < max) out[n++] = BR_GPU_SOFTWARE;
    return n;
}

#if defined(BR_NO_NATIVE_PLATFORM)
const br_platform *br_platform_native(void) { return NULL; }
#elif defined(__wasm__)
#  include "platform_web.c"
#elif defined(_WIN32)
#  include "platform_win32.c"
#elif defined(__APPLE__)
#  include "platform_macos.c"
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#  include "platform_x11.c"
#else
const br_platform *br_platform_native(void) { return NULL; }
#endif

#include "platform_terminal.c"
