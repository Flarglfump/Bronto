/*
 * Bronto web runtime - <math.h>.
 *
 * Square roots, rounding and the like are WebAssembly instructions; sin,
 * pow, log and the other transcendental functions call the browser's Math
 * (fast, and accurate to the last bit or so, like a desktop libm). The
 * float versions work in double and round once.
 */
#include "bw.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define M1(name, op) \
    double name(double x) { return bw_js_math1(op, x); } \
    float name##f(float x) { return (float)bw_js_math1(op, x); }
#define M2(name, op) \
    double name(double x, double y) { return bw_js_math2(op, x, y); } \
    float name##f(float x, float y) { return (float)bw_js_math2(op, x, y); }

M1(sin, BW_M_SIN) M1(cos, BW_M_COS) M1(tan, BW_M_TAN)
M1(asin, BW_M_ASIN) M1(acos, BW_M_ACOS) M1(atan, BW_M_ATAN)
M1(sinh, BW_M_SINH) M1(cosh, BW_M_COSH) M1(tanh, BW_M_TANH)
M1(asinh, BW_M_ASINH) M1(acosh, BW_M_ACOSH) M1(atanh, BW_M_ATANH)
M1(exp, BW_M_EXP) M1(expm1, BW_M_EXPM1) M1(log, BW_M_LOG) M1(log2, BW_M_LOG2)
M1(log10, BW_M_LOG10) M1(log1p, BW_M_LOG1P) M1(cbrt, BW_M_CBRT)
M2(atan2, BW_M_ATAN2) M2(pow, BW_M_POW) M2(fmod, BW_M_FMOD) M2(hypot, BW_M_HYPOT)

/* (Calling the browser directly: written as pow(2, x) or sin + cos, the
 * compiler would turn these back into calls to exp2 and sincos.) */
void sincos(double x, double *s, double *c) { *s = bw_js_math1(BW_M_SIN, x); *c = bw_js_math1(BW_M_COS, x); }
void sincosf(float x, float *s, float *c) { *s = (float)bw_js_math1(BW_M_SIN, x); *c = (float)bw_js_math1(BW_M_COS, x); }
double exp2(double x) { return bw_js_math2(BW_M_POW, 2.0, x); }
float exp2f(float x) { return (float)bw_js_math2(BW_M_POW, 2.0, x); }

/* ---- instructions ---- */
double fabs(double x) { return __builtin_fabs(x); }
float fabsf(float x) { return __builtin_fabsf(x); }
double sqrt(double x) { return __builtin_sqrt(x); }
float sqrtf(float x) { return __builtin_sqrtf(x); }
double floor(double x) { return __builtin_floor(x); }
float floorf(float x) { return __builtin_floorf(x); }
double ceil(double x) { return __builtin_ceil(x); }
float ceilf(float x) { return __builtin_ceilf(x); }
double trunc(double x) { return __builtin_trunc(x); }
float truncf(float x) { return __builtin_truncf(x); }
double rint(double x) { return __builtin_rint(x); }             /* nearest, ties to even */
float rintf(float x) { return __builtin_rintf(x); }
double nearbyint(double x) { return __builtin_rint(x); }
float nearbyintf(float x) { return __builtin_rintf(x); }
double fmin(double x, double y) { return x != x ? y : y != y ? x : (x < y ? x : y); }
float fminf(float x, float y) { return x != x ? y : y != y ? x : (x < y ? x : y); }
double fmax(double x, double y) { return x != x ? y : y != y ? x : (x > y ? x : y); }
float fmaxf(float x, float y) { return x != x ? y : y != y ? x : (x > y ? x : y); }
double copysign(double x, double y) { return __builtin_copysign(x, y); }
float copysignf(float x, float y) { return __builtin_copysignf(x, y); }
double fdim(double x, double y) { return x > y ? x - y : 0; }
float fdimf(float x, float y) { return x > y ? x - y : 0; }

/* round: halves away from zero (unlike the instruction's ties-to-even). */
double round(double x) {
    double t = __builtin_trunc(x);
    if (__builtin_fabs(x - t) >= 0.5) t += x < 0 ? -1.0 : 1.0;
    return t;
}
float roundf(float x) {
    float t = __builtin_truncf(x);
    if (__builtin_fabsf(x - t) >= 0.5f) t += x < 0 ? -1.0f : 1.0f;
    return t;
}
long lround(double x) { return (long)round(x); }
long lroundf(float x) { return (long)roundf(x); }
long long llround(double x) { return (long long)round(x); }
long long llroundf(float x) { return (long long)roundf(x); }
long lrint(double x) { return (long)rint(x); }
long lrintf(float x) { return (long)rintf(x); }
long long llrint(double x) { return (long long)rint(x); }
long long llrintf(float x) { return (long long)rintf(x); }

double fma(double x, double y, double z) { return x * y + z; }   /* (not fused: there's no instruction) */
float fmaf(float x, float y, float z) { return (float)((double)x * y + z); }   /* exact product in double */

double remainder(double x, double y) {
    double q = rint(x / y);
    return x - q * y;
}
float remainderf(float x, float y) { return (float)remainder(x, y); }

double modf(double x, double *ip) {
    double t = __builtin_trunc(x);
    *ip = t;
    return isinf(x) ? copysign(0.0, x) : x - t;
}
float modff(float x, float *ip) {
    float t = __builtin_truncf(x);
    *ip = t;
    return isinf(x) ? copysignf(0.0f, x) : x - t;
}

/* ---- the exponent of a double ---- */
static uint64_t bits_of(double x) { uint64_t u; memcpy(&u, &x, 8); return u; }
static double from_bits(uint64_t u) { double x; memcpy(&x, &u, 8); return x; }

double frexp(double x, int *e) {
    uint64_t u = bits_of(x);
    int ex = (int)((u >> 52) & 0x7ff);
    if (ex == 0) {
        if (x == 0) { *e = 0; return x; }
        x = frexp(x * 18446744073709551616.0, e);   /* subnormal: scale by 2^64 first */
        *e -= 64;
        return x;
    }
    if (ex == 0x7ff) { *e = 0; return x; }
    *e = ex - 1022;
    return from_bits((u & 0x800fffffffffffffULL) | 0x3fe0000000000000ULL);
}
float frexpf(float x, int *e) { return (float)frexp(x, e); }

double scalbn(double x, int n) {
    /* in steps, so huge and tiny factors don't overflow on the way */
    while (n > 1023) { x *= 8.98846567431158e307; n -= 1023; if (isinf(x)) return x; }
    while (n < -1022) { x *= 2.2250738585072014e-308 * 9007199254740992.0; n += 1022 - 53; if (x == 0) return x; }
    return x * from_bits((uint64_t)(0x3ff + n) << 52);
}
float scalbnf(float x, int n) { return (float)scalbn(x, n); }
double ldexp(double x, int n) { return scalbn(x, n); }
float ldexpf(float x, int n) { return (float)scalbn(x, n); }
int ilogb(double x) {
    int e;
    if (x == 0) return -2147483647 - 1;
    if (!isfinite(x)) return 2147483647;
    frexp(x, &e);
    return e - 1;
}
int ilogbf(float x) { return ilogb(x); }
double logb(double x) {
    if (x == 0) return -HUGE_VAL;
    if (!isfinite(x)) return x * x;
    return (double)ilogb(x);
}
float logbf(float x) { return (float)logb(x); }
double nextafter(double x, double y) {
    uint64_t u;
    if (x != x || y != y) return x + y;
    if (x == y) return y;
    if (x == 0) return y > 0 ? from_bits(1) : -from_bits(1);
    u = bits_of(x);
    if ((x < y) == (x > 0)) ++u; else --u;
    return from_bits(u);
}
float nextafterf(float x, float y) {
    uint32_t u;
    if (x != x || y != y) return x + y;
    if (x == y) return y;
    if (x == 0) { u = 1; memcpy(&x, &u, 4); return y > 0 ? x : -x; }
    memcpy(&u, &x, 4);
    if ((x < y) == (x > 0)) ++u; else --u;
    memcpy(&x, &u, 4);
    return x;
}
double nan(const char *tag) { (void)tag; return (double)NAN; }
float nanf(const char *tag) { (void)tag; return NAN; }

/* ---- the error and gamma functions (rarely needed in games) ---- */
double erf(double x) {           /* Abramowitz & Stegun 7.1.26, |error| < 1.5e-7 */
    double t = 1.0 / (1.0 + 0.3275911 * fabs(x));
    double y = 1.0 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * exp(-x * x);
    return x < 0 ? -y : y;
}
float erff(float x) { return (float)erf(x); }
double erfc(double x) { return 1.0 - erf(x); }
float erfcf(float x) { return (float)erfc(x); }
double lgamma(double x) {        /* Lanczos approximation */
    static const double g[] = { 0.99999999999980993, 676.5203681218851, -1259.1392167224028, 771.32342877765313,
                                -176.61503916999185, 12.507343278686905, -0.13857109526572012,
                                9.9843695780195716e-6, 1.5056327351493116e-7 };
    double a, t;
    int i;
    if (x < 0.5) return log(3.14159265358979323846 / fabs(sin(3.14159265358979323846 * x))) - lgamma(1 - x);
    x -= 1;
    a = g[0];
    t = x + 7.5;
    for (i = 1; i < 9; ++i) a += g[i] / (x + i);
    return 0.5 * log(2 * 3.14159265358979323846) + (x + 0.5) * log(t) - t + log(a);
}
float lgammaf(float x) { return (float)lgamma(x); }
double tgamma(double x) {
    if (x < 0.5) return 3.14159265358979323846 / (sin(3.14159265358979323846 * x) * tgamma(1 - x));
    return exp(lgamma(x));
}
float tgammaf(float x) { return (float)tgamma(x); }

/* ---- long double ----
 * long double is 128 bits on wasm32, done in software. Bronto doesn't use
 * it, but printf("%Lf") and strtold convert to and from double, which
 * needs these two compiler helpers. */
typedef union { long double f; struct { uint64_t lo, hi; } u; } bw_quad;
long double __extenddftf2(double d);
double __trunctfdf2(long double q);

long double __extenddftf2(double d) {
    uint64_t b = bits_of(d), sign = b >> 63, ex = (b >> 52) & 0x7ff, man = b & 0xfffffffffffffULL;
    bw_quad q;
    if (ex == 0 && man) {                                  /* subnormal: normalise */
        int shift = 0;
        while (!(man & (1ULL << 52))) { man <<= 1; ++shift; }
        man &= 0xfffffffffffffULL;
        ex = (uint64_t)(1 - shift);
        ex = (uint64_t)((int64_t)ex - 1023 + 16383);
    } else if (ex == 0x7ff) ex = 0x7fff;
    else if (ex) ex = ex - 1023 + 16383;
    q.u.hi = (sign << 63) | (ex << 48) | (man >> 4);
    q.u.lo = man << 60;
    return q.f;
}

double __trunctfdf2(long double f) {
    bw_quad q;
    uint64_t sign, man, rest;
    int64_t ex;
    q.f = f;
    sign = q.u.hi >> 63;
    ex = (int64_t)((q.u.hi >> 48) & 0x7fff);
    man = ((q.u.hi & 0xffffffffffffULL) << 4) | (q.u.lo >> 60);   /* the top 52 bits */
    rest = q.u.lo << 4;                                    /* the bits that don't fit */
    if (ex == 0x7fff) return from_bits((sign << 63) | (0x7ffULL << 52) | (man ? (man | (1ULL << 51)) : 0));
    if (ex == 0 && !man && !rest) return from_bits(sign << 63);
    ex = ex - 16383 + 1023;
    if (ex >= 0x7ff) return from_bits((sign << 63) | (0x7ffULL << 52));
    if (ex <= 0) return from_bits(sign << 63) + (sign ? -0.0 : 0.0);   /* (too small for a double: zero) */
    if ((rest >> 63) && ((rest << 1) || (man & 1))) {      /* round to nearest, ties to even */
        if (++man >> 52) { man = 0; ++ex; if (ex >= 0x7ff) return from_bits((sign << 63) | (0x7ffULL << 52)); }
    }
    return from_bits((sign << 63) | ((uint64_t)ex << 52) | man);
}
