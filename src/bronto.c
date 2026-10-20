/*
 * Unity build: compiling this single file builds the whole engine.
 *
 *   cc -Iinclude -c src/bronto.c            (then link with your game)
 *   cc -Iinclude src/bronto.c game.c -lm    (one step)
 *
 * This is the simplest way to build on any compiler/OS with no build
 * system at all. The Makefile builds the files separately instead.
 * Link flags: Linux -ldl (glibc < 2.34), macOS -framework Cocoa
 * -framework QuartzCore -framework AudioToolbox -framework GameController,
 * Windows -luser32 -lgdi32
 * (MSVC: automatic).
 */
#if !defined(_WIN32)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#    define _DARWIN_C_SOURCE
#  endif
#endif
#include "core/memory.c"
#include "core/log.c"
#include "core/bmath.c"
#include "core/event.c"
#include "core/input.c"
#include "core/ecs.c"
#include "core/scene.c"
#include "core/image.c"
#include "core/render.c"
#include "core/font8x8.c"
#include "core/audio.c"
#include "core/window.c"
#include "core/engine.c"
#include "platform/platform_headless.c"
#include "platform/platform_native.c"
