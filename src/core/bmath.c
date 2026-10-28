#include "bronto/bmath.h"

br_mat4 br_m4_transpose(br_mat4 a) {
    br_mat4 r;
    int c, row;
    for (c = 0; c < 4; ++c)
        for (row = 0; row < 4; ++row) r.m[c * 4 + row] = a.m[row * 4 + c];
    return r;
}

br_mat4 br_m4_inverse(br_mat4 a) {
    const float *m = a.m;
    float inv[16], det;
    br_mat4 r;
    int i;

    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (br_absf(det) < 1e-12f) return br_m4_identity();
    det = 1.0f / det;
    for (i = 0; i < 16; ++i) r.m[i] = inv[i] * det;
    return r;
}

br_mat4 br_quat_to_m4(br_quat q) {
    br_mat4 r = br_m4_identity();
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    r.m[0] = 1 - 2 * (yy + zz); r.m[4] = 2 * (xy - wz);     r.m[8]  = 2 * (xz + wy);
    r.m[1] = 2 * (xy + wz);     r.m[5] = 1 - 2 * (xx + zz); r.m[9]  = 2 * (yz - wx);
    r.m[2] = 2 * (xz - wy);     r.m[6] = 2 * (yz + wx);     r.m[10] = 1 - 2 * (xx + yy);
    return r;
}

br_quat br_quat_norm(br_quat q) {
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-12f) return br_quat_identity();
    l = 1.0f / l;
    q.x *= l; q.y *= l; q.z *= l; q.w *= l;
    return q;
}

br_vec3 br_quat_rotate(br_quat q, br_vec3 v) {
    br_vec3 u = br_v3(q.x, q.y, q.z);
    br_vec3 t = br_v3_scale(br_v3_cross(u, v), 2.0f);
    return br_v3_add(br_v3_add(v, br_v3_scale(t, q.w)), br_v3_cross(u, t));
}

br_quat br_quat_slerp(br_quat a, br_quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w, th, s, wa, wb;
    br_quat r;
    if (d < 0) { d = -d; b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; }
    if (d > 0.9995f) {
        r.x = a.x + (b.x - a.x) * t; r.y = a.y + (b.y - a.y) * t; r.z = a.z + (b.z - a.z) * t; r.w = a.w + (b.w - a.w) * t;
        return br_quat_norm(r);
    }
    th = acosf(d);
    s = sinf(th);
    wa = sinf((1 - t) * th) / s;
    wb = sinf(t * th) / s;
    r.x = a.x * wa + b.x * wb; r.y = a.y * wa + b.y * wb; r.z = a.z * wa + b.z * wb; r.w = a.w * wa + b.w * wb;
    return r;
}

br_quat br_quat_from_euler(br_vec3 deg) {
    br_quat qx = br_quat_axis_angle(br_v3(1, 0, 0), deg.x * BR_DEG2RAD);
    br_quat qy = br_quat_axis_angle(br_v3(0, 1, 0), deg.y * BR_DEG2RAD);
    br_quat qz = br_quat_axis_angle(br_v3(0, 0, 1), deg.z * BR_DEG2RAD);
    return br_quat_norm(br_quat_mul(br_quat_mul(qy, qx), qz));
}

/* R = Ry Rx Rz: m12 = -sin x, m02 / m22 give y, m10 / m11 give z (row, column). */
br_vec3 br_quat_to_euler(br_quat q) {
    br_mat4 m = br_quat_to_m4(br_quat_norm(q));
    float m12 = m.m[9], m02 = m.m[8], m22 = m.m[10], m10 = m.m[1], m11 = m.m[5], m00 = m.m[0], m20 = m.m[2];
    float sx = br_clampf(-m12, -1, 1), x = asinf(sx), y, z;
    if (fabsf(sx) < 0.99999f) {
        y = atan2f(m02, m22);
        z = atan2f(m10, m11);
    } else {                                            /* (straight up or down: roll folds into yaw) */
        y = atan2f(-m20, m00);
        z = 0;
    }
    return br_v3(x * BR_RAD2DEG + 0.0f, y * BR_RAD2DEG + 0.0f, z * BR_RAD2DEG + 0.0f);   /* (+ 0: no -0) */
}

br_quat br_quat_from_m4(br_mat4 a) {
    const float *m = a.m;
    float tr = m[0] + m[5] + m[10], s;
    br_quat q;
    if (tr > 0) {
        s = sqrtf(tr + 1.0f) * 2;
        q.w = 0.25f * s; q.x = (m[6] - m[9]) / s; q.y = (m[8] - m[2]) / s; q.z = (m[1] - m[4]) / s;
    } else if (m[0] > m[5] && m[0] > m[10]) {
        s = sqrtf(1.0f + m[0] - m[5] - m[10]) * 2;
        q.w = (m[6] - m[9]) / s; q.x = 0.25f * s; q.y = (m[4] + m[1]) / s; q.z = (m[8] + m[2]) / s;
    } else if (m[5] > m[10]) {
        s = sqrtf(1.0f + m[5] - m[0] - m[10]) * 2;
        q.w = (m[8] - m[2]) / s; q.x = (m[4] + m[1]) / s; q.y = 0.25f * s; q.z = (m[9] + m[6]) / s;
    } else {
        s = sqrtf(1.0f + m[10] - m[0] - m[5]) * 2;
        q.w = (m[1] - m[4]) / s; q.x = (m[8] + m[2]) / s; q.y = (m[9] + m[6]) / s; q.z = 0.25f * s;
    }
    return br_quat_norm(q);
}

br_quat br_quat_look(br_vec3 dir, br_vec3 up) {
    br_vec3 f = br_v3_norm(dir), r, u;
    br_mat4 m = br_m4_identity();
    if (br_v3_len2(f) < 1e-12f) return br_quat_identity();
    r = br_v3_cross(f, up);
    if (br_v3_len2(r) < 1e-8f) r = br_v3_cross(f, fabsf(f.y) < 0.9f ? br_v3(0, 1, 0) : br_v3(1, 0, 0));
    r = br_v3_norm(r);
    u = br_v3_cross(r, f);
    m.m[0] = r.x; m.m[1] = r.y; m.m[2] = r.z;           /* columns: right, up, back (-forward) */
    m.m[4] = u.x; m.m[5] = u.y; m.m[6] = u.z;
    m.m[8] = -f.x; m.m[9] = -f.y; m.m[10] = -f.z;
    return br_quat_from_m4(m);
}

br_mat4 br_m4_trs(br_vec3 t, br_quat q, br_vec3 s) {
    br_mat4 m = br_quat_to_m4(q);
    int i;
    for (i = 0; i < 3; ++i) { m.m[i] *= s.x; m.m[4 + i] *= s.y; m.m[8 + i] *= s.z; }
    m.m[12] = t.x; m.m[13] = t.y; m.m[14] = t.z;
    return m;
}

void br_m4_decompose(br_mat4 m, br_vec3 *t, br_quat *r, br_vec3 *s) {
    br_vec3 sc = br_v3(sqrtf(m.m[0] * m.m[0] + m.m[1] * m.m[1] + m.m[2] * m.m[2]),
                       sqrtf(m.m[4] * m.m[4] + m.m[5] * m.m[5] + m.m[6] * m.m[6]),
                       sqrtf(m.m[8] * m.m[8] + m.m[9] * m.m[9] + m.m[10] * m.m[10]));
    br_vec3 x = br_v3(m.m[0], m.m[1], m.m[2]), y = br_v3(m.m[4], m.m[5], m.m[6]), z = br_v3(m.m[8], m.m[9], m.m[10]);
    br_mat4 rot = br_m4_identity();
    int i;
    if (br_v3_dot(br_v3_cross(x, y), z) < 0) sc.x = -sc.x;   /* a mirror: one negative scale */
    if (t) *t = br_v3(m.m[12], m.m[13], m.m[14]);
    if (s) *s = sc;
    for (i = 0; i < 3; ++i) {
        rot.m[i] = sc.x != 0 ? m.m[i] / sc.x : (i == 0);
        rot.m[4 + i] = sc.y != 0 ? m.m[4 + i] / sc.y : (i == 1);
        rot.m[8 + i] = sc.z != 0 ? m.m[8 + i] / sc.z : (i == 2);
    }
    if (r) *r = br_quat_from_m4(rot);
}

br_recti br_recti_intersect(br_recti a, br_recti b) {
    int x0 = BR_MAX(a.x, b.x), y0 = BR_MAX(a.y, b.y);
    int x1 = BR_MIN(a.x + a.w, b.x + b.w), y1 = BR_MIN(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0) return br_recti_make(x0, y0, 0, 0);
    return br_recti_make(x0, y0, x1 - x0, y1 - y0);
}

void br_rng_seed(br_rng *r, uint64_t seed) {
    /* splitmix64 to spread poor seeds; state must be non-zero. */
    uint64_t z = seed + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    r->state = z ? z : 0x2545F4914F6CDD1Dull;
}

uint32_t br_rng_u32(br_rng *r) {
    uint64_t x = r->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    r->state = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1Dull) >> 32);
}

float br_rng_float(br_rng *r) { return (float)(br_rng_u32(r) >> 8) * (1.0f / 16777216.0f); }
float br_rng_range(br_rng *r, float lo, float hi) { return lo + (hi - lo) * br_rng_float(r); }
int br_rng_int(br_rng *r, int lo, int hi) {
    uint32_t span;
    if (hi <= lo) return lo;
    span = (uint32_t)(hi - lo) + 1u;
    return lo + (int)(br_rng_u32(r) % span);
}
