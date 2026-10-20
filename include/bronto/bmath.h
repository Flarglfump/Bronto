/*
 * Bronto Engine - math library.
 *
 * Vectors (2/3/4), 4x4 matrices (column-major, right-handed, OpenGL-style
 * clip space) and quaternions. 2D games use vec2 + br_mat4 with z = 0, so
 * the same transform pipeline extends naturally to 3D later.
 *
 * Named bmath.h (not math.h) so it can never shadow the C library header.
 */
#ifndef BRONTO_BMATH_H
#define BRONTO_BMATH_H

#include "types.h"
#include <math.h>

#define BR_PI      3.14159265358979323846f
#define BR_TAU     6.28318530717958647692f
#define BR_DEG2RAD (BR_PI / 180.0f)
#define BR_RAD2DEG (180.0f / BR_PI)
#define BR_EPSILON 1e-6f

typedef struct br_vec2 { float x, y; } br_vec2;
typedef struct br_vec3 { float x, y, z; } br_vec3;
typedef struct br_vec4 { float x, y, z, w; } br_vec4;
typedef struct br_quat { float x, y, z, w; } br_quat;
/* Column-major: m[col * 4 + row]. */
typedef struct br_mat4 { float m[16]; } br_mat4;

typedef struct br_rect  { float x, y, w, h; } br_rect;
typedef struct br_recti { int x, y, w, h; } br_recti;

/* ---- scalar ---------------------------------------------------------- */
BR_INLINE float br_lerpf(float a, float b, float t) { return a + (b - a) * t; }
BR_INLINE float br_clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
BR_INLINE float br_absf(float x) { return x < 0 ? -x : x; }
BR_INLINE bool  br_nearf(float a, float b, float eps) { return br_absf(a - b) <= eps; }
BR_INLINE float br_signf(float x) { return (float)((x > 0) - (x < 0)); }

/* ---- vec2 ------------------------------------------------------------ */
BR_INLINE br_vec2 br_v2(float x, float y) { br_vec2 r; r.x = x; r.y = y; return r; }
BR_INLINE br_vec2 br_v2_add(br_vec2 a, br_vec2 b) { return br_v2(a.x + b.x, a.y + b.y); }
BR_INLINE br_vec2 br_v2_sub(br_vec2 a, br_vec2 b) { return br_v2(a.x - b.x, a.y - b.y); }
BR_INLINE br_vec2 br_v2_mul(br_vec2 a, br_vec2 b) { return br_v2(a.x * b.x, a.y * b.y); }
BR_INLINE br_vec2 br_v2_scale(br_vec2 a, float s) { return br_v2(a.x * s, a.y * s); }
BR_INLINE float   br_v2_dot(br_vec2 a, br_vec2 b) { return a.x * b.x + a.y * b.y; }
BR_INLINE float   br_v2_cross(br_vec2 a, br_vec2 b) { return a.x * b.y - a.y * b.x; }
BR_INLINE float   br_v2_len2(br_vec2 a) { return br_v2_dot(a, a); }
BR_INLINE float   br_v2_len(br_vec2 a) { return sqrtf(br_v2_len2(a)); }
BR_INLINE float   br_v2_dist(br_vec2 a, br_vec2 b) { return br_v2_len(br_v2_sub(a, b)); }
BR_INLINE br_vec2 br_v2_norm(br_vec2 a) {
    float l = br_v2_len(a);
    return l > BR_EPSILON ? br_v2_scale(a, 1.0f / l) : br_v2(0, 0);
}
BR_INLINE br_vec2 br_v2_lerp(br_vec2 a, br_vec2 b, float t) {
    return br_v2(br_lerpf(a.x, b.x, t), br_lerpf(a.y, b.y, t));
}
BR_INLINE br_vec2 br_v2_rotate(br_vec2 v, float radians) {
    float c = cosf(radians), s = sinf(radians);
    return br_v2(v.x * c - v.y * s, v.x * s + v.y * c);
}
BR_INLINE br_vec2 br_v2_perp(br_vec2 v) { return br_v2(-v.y, v.x); }

/* ---- vec3 ------------------------------------------------------------ */
BR_INLINE br_vec3 br_v3(float x, float y, float z) { br_vec3 r; r.x = x; r.y = y; r.z = z; return r; }
BR_INLINE br_vec3 br_v3_add(br_vec3 a, br_vec3 b) { return br_v3(a.x + b.x, a.y + b.y, a.z + b.z); }
BR_INLINE br_vec3 br_v3_sub(br_vec3 a, br_vec3 b) { return br_v3(a.x - b.x, a.y - b.y, a.z - b.z); }
BR_INLINE br_vec3 br_v3_mul(br_vec3 a, br_vec3 b) { return br_v3(a.x * b.x, a.y * b.y, a.z * b.z); }
BR_INLINE br_vec3 br_v3_scale(br_vec3 a, float s) { return br_v3(a.x * s, a.y * s, a.z * s); }
BR_INLINE float   br_v3_dot(br_vec3 a, br_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
BR_INLINE br_vec3 br_v3_cross(br_vec3 a, br_vec3 b) {
    return br_v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
BR_INLINE float   br_v3_len(br_vec3 a) { return sqrtf(br_v3_dot(a, a)); }
BR_INLINE br_vec3 br_v3_norm(br_vec3 a) {
    float l = br_v3_len(a);
    return l > BR_EPSILON ? br_v3_scale(a, 1.0f / l) : br_v3(0, 0, 0);
}
BR_INLINE br_vec3 br_v3_lerp(br_vec3 a, br_vec3 b, float t) {
    return br_v3(br_lerpf(a.x, b.x, t), br_lerpf(a.y, b.y, t), br_lerpf(a.z, b.z, t));
}

/* ---- vec4 ------------------------------------------------------------ */
BR_INLINE br_vec4 br_v4(float x, float y, float z, float w) {
    br_vec4 r; r.x = x; r.y = y; r.z = z; r.w = w; return r;
}
BR_INLINE br_vec4 br_v4_add(br_vec4 a, br_vec4 b) { return br_v4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
BR_INLINE br_vec4 br_v4_scale(br_vec4 a, float s) { return br_v4(a.x * s, a.y * s, a.z * s, a.w * s); }
BR_INLINE float   br_v4_dot(br_vec4 a, br_vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

/* ---- mat4 ------------------------------------------------------------ */
BR_INLINE br_mat4 br_m4_identity(void) {
    br_mat4 r = {{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
    return r;
}
BR_INLINE br_mat4 br_m4_mul(br_mat4 a, br_mat4 b) {
    br_mat4 r;
    int c, row, k;
    for (c = 0; c < 4; ++c)
        for (row = 0; row < 4; ++row) {
            float s = 0;
            for (k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}
BR_INLINE br_vec4 br_m4_mul_v4(br_mat4 a, br_vec4 v) {
    return br_v4(a.m[0] * v.x + a.m[4] * v.y + a.m[8]  * v.z + a.m[12] * v.w,
                 a.m[1] * v.x + a.m[5] * v.y + a.m[9]  * v.z + a.m[13] * v.w,
                 a.m[2] * v.x + a.m[6] * v.y + a.m[10] * v.z + a.m[14] * v.w,
                 a.m[3] * v.x + a.m[7] * v.y + a.m[11] * v.z + a.m[15] * v.w);
}
BR_INLINE br_vec3 br_m4_mul_point(br_mat4 a, br_vec3 p) {
    br_vec4 r = br_m4_mul_v4(a, br_v4(p.x, p.y, p.z, 1.0f));
    if (r.w != 0.0f && r.w != 1.0f) return br_v3(r.x / r.w, r.y / r.w, r.z / r.w);
    return br_v3(r.x, r.y, r.z);
}
BR_INLINE br_vec2 br_m4_mul_point2(br_mat4 a, br_vec2 p) {
    return br_v2(a.m[0] * p.x + a.m[4] * p.y + a.m[12], a.m[1] * p.x + a.m[5] * p.y + a.m[13]);
}
BR_INLINE br_mat4 br_m4_translate(br_vec3 t) {
    br_mat4 r = br_m4_identity();
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}
BR_INLINE br_mat4 br_m4_scale(br_vec3 s) {
    br_mat4 r = br_m4_identity();
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
}
BR_INLINE br_mat4 br_m4_rotate_z(float radians) {
    br_mat4 r = br_m4_identity();
    float c = cosf(radians), s = sinf(radians);
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
    return r;
}
BR_INLINE br_mat4 br_m4_rotate(br_vec3 axis, float radians) {
    br_mat4 r = br_m4_identity();
    br_vec3 a = br_v3_norm(axis);
    float c = cosf(radians), s = sinf(radians), t = 1.0f - c;
    r.m[0] = t * a.x * a.x + c;       r.m[4] = t * a.x * a.y - s * a.z; r.m[8]  = t * a.x * a.z + s * a.y;
    r.m[1] = t * a.x * a.y + s * a.z; r.m[5] = t * a.y * a.y + c;       r.m[9]  = t * a.y * a.z - s * a.x;
    r.m[2] = t * a.x * a.z - s * a.y; r.m[6] = t * a.y * a.z + s * a.x; r.m[10] = t * a.z * a.z + c;
    return r;
}
BR_INLINE br_mat4 br_m4_ortho(float l, float r_, float b, float t, float n, float f) {
    br_mat4 r = br_m4_identity();
    r.m[0] = 2.0f / (r_ - l);
    r.m[5] = 2.0f / (t - b);
    r.m[10] = -2.0f / (f - n);
    r.m[12] = -(r_ + l) / (r_ - l);
    r.m[13] = -(t + b) / (t - b);
    r.m[14] = -(f + n) / (f - n);
    return r;
}
BR_INLINE br_mat4 br_m4_perspective(float fovy_radians, float aspect, float n, float f) {
    br_mat4 r;
    float t = 1.0f / tanf(fovy_radians * 0.5f);
    int i;
    for (i = 0; i < 16; ++i) r.m[i] = 0;
    r.m[0] = t / aspect;
    r.m[5] = t;
    r.m[10] = (f + n) / (n - f);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * f * n) / (n - f);
    return r;
}
BR_INLINE br_mat4 br_m4_look_at(br_vec3 eye, br_vec3 target, br_vec3 up) {
    br_vec3 f = br_v3_norm(br_v3_sub(target, eye));
    br_vec3 s = br_v3_norm(br_v3_cross(f, up));
    br_vec3 u = br_v3_cross(s, f);
    br_mat4 r = br_m4_identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -br_v3_dot(s, eye);
    r.m[13] = -br_v3_dot(u, eye);
    r.m[14] = br_v3_dot(f, eye);
    return r;
}
/* General inverse. Returns identity if the matrix is singular. */
br_mat4 br_m4_inverse(br_mat4 a);
br_mat4 br_m4_transpose(br_mat4 a);

/* ---- quaternion ------------------------------------------------------- */
BR_INLINE br_quat br_quat_identity(void) { br_quat q = {0, 0, 0, 1}; return q; }
BR_INLINE br_quat br_quat_axis_angle(br_vec3 axis, float radians) {
    br_vec3 a = br_v3_norm(axis);
    float s = sinf(radians * 0.5f);
    br_quat q;
    q.x = a.x * s; q.y = a.y * s; q.z = a.z * s; q.w = cosf(radians * 0.5f);
    return q;
}
BR_INLINE br_quat br_quat_mul(br_quat a, br_quat b) {
    br_quat r;
    r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    return r;
}
br_mat4 br_quat_to_m4(br_quat q);

/* ---- rect ------------------------------------------------------------ */
BR_INLINE br_rect br_rect_make(float x, float y, float w, float h) { br_rect r; r.x = x; r.y = y; r.w = w; r.h = h; return r; }
BR_INLINE bool br_rect_contains(br_rect r, br_vec2 p) {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h;
}
BR_INLINE bool br_rect_overlaps(br_rect a, br_rect b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
BR_INLINE br_recti br_recti_make(int x, int y, int w, int h) { br_recti r; r.x = x; r.y = y; r.w = w; r.h = h; return r; }
br_recti br_recti_intersect(br_recti a, br_recti b);

/* ---- deterministic PRNG (xorshift64*) -------------------------------- */
typedef struct br_rng { uint64_t state; } br_rng;
void     br_rng_seed(br_rng *r, uint64_t seed);
uint32_t br_rng_u32(br_rng *r);
float    br_rng_float(br_rng *r);                    /* [0, 1)  */
float    br_rng_range(br_rng *r, float lo, float hi);/* [lo,hi) */
int      br_rng_int(br_rng *r, int lo, int hi);      /* [lo,hi] */

#endif /* BRONTO_BMATH_H */
