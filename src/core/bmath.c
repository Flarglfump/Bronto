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
