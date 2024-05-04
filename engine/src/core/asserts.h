#pragma once

#include "defines.h"

// Comment out to diable assertions
#define B_ASSERTIONS_ENABLED

#ifdef B_ASSERTIONS_ENABLED
#if _MSC_VER
    #include <intrin.h>
    #define debugBreak() __debugbreak()
#else
    #define debugBreak() __builtin_trap()
#endif

B_API void report_assertion_failure(const char * expression, const char * message, const char * file, i32 line);

#define B_ASSERT(expr)                                               \
    {                                                                \
        if (expr) {                                                  \
        } else {                                                     \
            report_assertion_failure(#expr, "", __FILE__, __LINE__); \
            debugBreak();                                            \
        }                                                            \
    }                                                                

#define B_ASSERT_MSG(expr, message)                                       \
    {                                                                     \
        if (expr) {                                                       \
        } else {                                                          \
            report_assertion_failure(#expr, message, __FILE__, __LINE__); \
            debugBreak();                                                 \
        }                                                                 \
    }                                                                     

#ifdef _DEBUG
#define B_ASSERT_DEBUG(expr)                                         \
    {                                                                \
        if (expr) {                                                  \
        } else {                                                     \
            report_assertion_failure(#expr, "", __FILE__, __LINE__); \
            debugBreak();                                            \
        }                                                            \
    }   

#else
#define B_ASSERT_DEBUG(expr) // Does nothing
#endif
#else
// Assertions disabled
#define B_ASSERT(expr) // Does nothing
#define B_ASSERT_MSG(expr, message) // Does nothing
#define B_ASSERT_DEBUG(expr) // Does nothing

#endif // End Assertions

