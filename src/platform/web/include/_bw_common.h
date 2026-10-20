/* Bronto web C library: shared definitions (internal). */
#ifndef BW_COMMON_H
#define BW_COMMON_H
#define __need_size_t
#define __need_NULL
#define __need_wchar_t
#include <stddef.h>
#ifdef __cplusplus
#  define BW_BEGIN extern "C" {
#  define BW_END }
#else
#  define BW_BEGIN
#  define BW_END
#endif
#define BW_NORETURN __attribute__((noreturn))
#endif
