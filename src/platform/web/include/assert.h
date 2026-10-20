/* Bronto web C library: <assert.h> */
#undef assert
#ifdef NDEBUG
#  define assert(x) ((void)0)
#else
#  include "_bw_common.h"
BW_BEGIN
BW_NORETURN void __bw_assert_fail(const char *expr, const char *file, int line);
BW_END
#  define assert(x) ((x) ? (void)0 : __bw_assert_fail(#x, __FILE__, __LINE__))
#endif
