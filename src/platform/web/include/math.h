/* Bronto web C library: <math.h>. The simple operations are WebAssembly
 * instructions; the transcendental functions come from the browser's Math. */
#ifndef BW_MATH_H
#define BW_MATH_H
#include "_bw_common.h"
BW_BEGIN
typedef float float_t;
typedef double double_t;
#define HUGE_VAL  __builtin_huge_val()
#define HUGE_VALF __builtin_huge_valf()
#define HUGE_VALL __builtin_huge_vall()
#define INFINITY  __builtin_inff()
#define NAN       __builtin_nanf("")
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define MATH_ERRNO 1
#define MATH_ERREXCEPT 2
#define math_errhandling 0
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#define isnan(x)      __builtin_isnan(x)
#define isinf(x)      __builtin_isinf(x)
#define isfinite(x)   __builtin_isfinite(x)
#define isnormal(x)   __builtin_isnormal(x)
#define signbit(x)    __builtin_signbit(x)
#define isgreater(a, b)      __builtin_isgreater(a, b)
#define isgreaterequal(a, b) __builtin_isgreaterequal(a, b)
#define isless(a, b)         __builtin_isless(a, b)
#define islessequal(a, b)    __builtin_islessequal(a, b)
#define islessgreater(a, b)  __builtin_islessgreater(a, b)
#define isunordered(a, b)    __builtin_isunordered(a, b)

#define BW_MATH2(name) double name(double x); float name##f(float x);
#define BW_MATH2_2(name) double name(double x, double y); float name##f(float x, float y);
BW_MATH2(fabs) BW_MATH2(sqrt) BW_MATH2(floor) BW_MATH2(ceil) BW_MATH2(trunc) BW_MATH2(round)
BW_MATH2(rint) BW_MATH2(nearbyint)
BW_MATH2(sin) BW_MATH2(cos) BW_MATH2(tan) BW_MATH2(asin) BW_MATH2(acos) BW_MATH2(atan)
BW_MATH2(sinh) BW_MATH2(cosh) BW_MATH2(tanh) BW_MATH2(asinh) BW_MATH2(acosh) BW_MATH2(atanh)
BW_MATH2(exp) BW_MATH2(exp2) BW_MATH2(expm1) BW_MATH2(log) BW_MATH2(log2) BW_MATH2(log10) BW_MATH2(log1p)
BW_MATH2(cbrt) BW_MATH2(logb) BW_MATH2(erf) BW_MATH2(erfc) BW_MATH2(tgamma) BW_MATH2(lgamma)
BW_MATH2_2(atan2) BW_MATH2_2(pow) BW_MATH2_2(fmod) BW_MATH2_2(remainder) BW_MATH2_2(hypot)
BW_MATH2_2(fmin) BW_MATH2_2(fmax) BW_MATH2_2(fdim) BW_MATH2_2(copysign) BW_MATH2_2(nextafter)
#undef BW_MATH2
#undef BW_MATH2_2
double fma(double x, double y, double z); float fmaf(float x, float y, float z);
long lround(double x); long lroundf(float x); long long llround(double x); long long llroundf(float x);
long lrint(double x); long lrintf(float x); long long llrint(double x); long long llrintf(float x);
double modf(double x, double *ip); float modff(float x, float *ip);
double frexp(double x, int *e); float frexpf(float x, int *e);
double ldexp(double x, int e); float ldexpf(float x, int e);
double scalbn(double x, int e); float scalbnf(float x, int e);
int ilogb(double x); int ilogbf(float x);
double nan(const char *tag); float nanf(const char *tag);
void sincos(double x, double *s, double *c); void sincosf(float x, float *s, float *c);
BW_END
#endif
