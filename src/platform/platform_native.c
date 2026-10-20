/*
 * Native backend selector: compiles in the backend for the target OS.
 *
 *   Windows        platform_win32.c  Win32 + GDI
 *   macOS          platform_macos.c  Cocoa (from C via the Objective-C runtime) + Core Animation
 *   Linux / BSD    platform_x11.c    X11, loaded at run time with dlopen
 *
 * Define BR_NO_NATIVE_PLATFORM to build without any (headless only).
 */
#if !defined(_WIN32)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L   /* clock_gettime, nanosleep, dlopen */
#  endif
#  if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#    define _DARWIN_C_SOURCE
#  endif
#endif

#include "bronto/platform.h"

#if defined(BR_NO_NATIVE_PLATFORM)
const br_platform *br_platform_native(void) { return NULL; }
#elif defined(_WIN32)
#  include "platform_win32.c"
#elif defined(__APPLE__)
#  include "platform_macos.c"
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#  include "platform_x11.c"
#else
const br_platform *br_platform_native(void) { return NULL; }
#endif
