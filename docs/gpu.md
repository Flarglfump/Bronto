# Showing frames through the GPU

Bronto draws every frame on the CPU, 2D and 3D alike (`render.h`, `render3d.h`), into
a framebuffer the size of the game's screen. A desktop window then has to show that
frame, scaled up to the window with black bars around it. It can do that two ways:

- **through the GPU**: the frame goes into a texture, and the GPU scales it into the
  window (nearest-neighbour, so pixels stay sharp). This is the default.
- **without it ("Software")**: the OS's own 2D drawing does the scaling: GDI's
  `StretchDIBits` on Windows, a Core Animation layer on macOS, `XPutImage` on Linux.

Either way the game draws exactly the same pixels; only how they reach the screen
changes. The graphics APIs are the operating system's and the graphics driver's,
loaded while the program runs, as X11 and ALSA already are, so nothing extra is
needed to build a game or to run one.

| API | Where | How it's used |
|---|---|---|
| **Direct3D 12** | Windows 10 and later | `d3d12.dll` and `dxgi.dll`; a flip-model swapchain; the frame's texture drawn by one triangle with a point sampler (two small HLSL shaders, compiled when it starts by Windows' `d3dcompiler_47.dll`) |
| **Vulkan** | Windows, Linux (with a Vulkan driver) | the Vulkan loader (`vulkan-1.dll`, `libvulkan.so.1`); a FIFO swapchain; the frame copied to an image and blitted into the window with `vkCmdBlitImage` (nearest), so no shaders |
| **Metal** | macOS | `Metal.framework` and a `CAMetalLayer` over the view; one triangle with a nearest sampler (Metal shader source, compiled when it starts) |
| **OpenGL** | Windows (WGL), macOS (`NSOpenGLContext`), Linux (GLX) | OpenGL 1.1 only: one texture drawn as one quad, so any OpenGL works, Mesa's software one included. On macOS it's deprecated by Apple (still there), so it's only used when asked for by name |
| **Software** | everywhere | no graphics API |

Each presents with vsync, and each is used from the engine's own thread, the way the
software paths are.

## Choosing one

The choice is **Automatic** unless you say otherwise. Automatic takes the best API the
computer has: on Windows Direct3D 12, then Vulkan, then OpenGL; on macOS Metal; on
Linux OpenGL, then Vulkan. Whatever is chosen, one that isn't there or doesn't start
falls back to the next in Automatic's order, and finally to Software. The log says
which one was used:

```
[INFO ] platform_x11.c: X11: frames go through OpenGL 4.6, Mesa Intel(R) Graphics (ADL GT2)
```

| For | Where | Setting |
|---|---|---|
| a game | **Build > Build Settings > Graphics** (with Renderer: Window), for the platform chosen there | `gpu = auto` in `project.bronto` (`software`, `opengl`, `vulkan`, `metal`, `d3d12`) |
| one run of a game | the command line | `bronto_player --gpu vulkan`, or `MyGame --gpu software` |
| the editor's own window | **Settings > Graphics** (used from the next start) | `graphics = auto` in the editor's settings file; `editor --gpu software` for one run |
| any Bronto program | the environment | `BRONTO_GPU=software` (or `opengl`, `vulkan`...) wins over all of the above |
| a game in C | `br_engine_config.gpu` | `cfg.gpu = BR_GPU_VULKAN;` (`platform.h`) |

`BRONTO_GPU=software` is the way out if a driver shows a black window or misbehaves:
the game or editor runs as it did before any of this, without the GPU.

Build Settings lists only the APIs the chosen platform has (Metal for macOS, Direct3D
12 for Windows...). A project setting that the computer running the game doesn't
have falls back, as above.

In C: `br_gpu_active()` and `br_gpu_info()` say what a window is using, and
`br_gpu_name()`, `br_gpu_title()` and `br_gpu_parse()` convert between `br_gpu` and the
names above.

## What's tested where

| | Tested |
|---|---|
| OpenGL (GLX) | On Linux under Xvfb with Mesa's OpenGL: the X11 integration test (`make x11-test`) checks the pixels that reach the window (scaling, bars, resizing, fullscreen), through OpenGL and again with Software; the editor runs with it |
| Vulkan | Falls back cleanly where there's no Vulkan driver (checked on Linux). Its declarations are checked against the sizes in the real `vulkan_core.h` (`make gpu-check`). Not yet run with a Vulkan driver |
| Direct3D 12, OpenGL (WGL) | Type-checked against a stand-in `windows.h`, with every Direct3D 12 structure checked against the size the real headers give (`make gpu-check`). Not yet run on Windows |
| Metal, OpenGL (macOS) | Metal's file is type-checked with the Objective-C runtime stood in for (`make gpu-check`). Not yet run on a Mac |

## Later: drawing on the GPU

All of this only *shows* frames through the GPU; the drawing itself is still the CPU's,
which keeps every platform's pictures identical (replays and the golden-image tests
depend on it). Drawing sprites, shapes, text and 3D meshes with the GPU, with the CPU
renderer kept as the reference and the fallback, is the next step.
