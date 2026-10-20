/*
 * Bronto Engine - base types and utility macros.
 *
 * Bronto targets C99 and uses only the C standard library in its core.
 * Everything OS-specific lives behind the platform layer (platform.h).
 */
#ifndef BRONTO_TYPES_H
#define BRONTO_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define BRONTO_VERSION_MAJOR 0
#define BRONTO_VERSION_MINOR 1
#define BRONTO_VERSION_PATCH 0

/* ---- Compiler / platform detection ------------------------------------ */
#if defined(_WIN32)
#  define BR_OS_WINDOWS 1
#elif defined(__wasm__) || defined(__EMSCRIPTEN__)
#  define BR_OS_WEB 1        /* WebAssembly in a browser (src/platform/web) */
#elif defined(__APPLE__)
#  define BR_OS_MACOS 1
#elif defined(__linux__)
#  define BR_OS_LINUX 1
#else
#  define BR_OS_UNKNOWN 1
#endif

#if defined(_MSC_VER)
#  define BR_INLINE static __inline
#else
#  define BR_INLINE static inline
#endif

/* ---- Utility macros -------------------------------------------------- */
#define BR_UNUSED(x)        ((void)(x))
#define BR_ARRAY_COUNT(a)   (sizeof(a) / sizeof((a)[0]))
#define BR_MIN(a, b)        ((a) < (b) ? (a) : (b))
#define BR_MAX(a, b)        ((a) > (b) ? (a) : (b))
#define BR_CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define BR_KB(n)            ((size_t)(n) * 1024u)
#define BR_MB(n)            (BR_KB(n) * 1024u)

/* Compile-time assertion that works in C99. */
#define BR_STATIC_ASSERT_JOIN2(a, b) a##b
#define BR_STATIC_ASSERT_JOIN(a, b)  BR_STATIC_ASSERT_JOIN2(a, b)
#define BR_STATIC_ASSERT(cond, msg) \
    typedef char BR_STATIC_ASSERT_JOIN(br_static_assert_, __LINE__)[(cond) ? 1 : -1]

/* Runtime assertion. Compiled out when BR_NO_ASSERT is defined. */
#ifndef BR_NO_ASSERT
void br_assert_fail(const char *expr, const char *file, int line);
#  define BR_ASSERT(cond) \
      do { if (!(cond)) br_assert_fail(#cond, __FILE__, __LINE__); } while (0)
#else
#  define BR_ASSERT(cond) ((void)0)
#endif

#endif /* BRONTO_TYPES_H */
