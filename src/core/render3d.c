/* 3D rendering in software: meshes, a depth buffer, lights (render3d.h). */
#include "bronto/render3d.h"
#include "bronto/memory.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- meshes ---------------------------------------------------------------------- */

void br_mesh_destroy(br_mesh *m) {
    br_mesh_drop_bvh(m);
    br_free(m->verts);
    br_free(m->indices);
    memset(m, 0, sizeof(*m));
}

bool br_mesh_init(br_mesh *m, uint32_t verts, uint32_t indices) {
    memset(m, 0, sizeof(*m));
    m->verts = verts ? (br_vertex3d *)br_calloc(verts, sizeof(br_vertex3d)) : NULL;
    m->indices = indices ? (uint32_t *)br_calloc(indices, sizeof(uint32_t)) : NULL;
    if ((verts && !m->verts) || (indices && !m->indices)) { br_mesh_destroy(m); return false; }
    m->vert_count = verts;
    m->index_count = indices;
    return true;
}

void br_mesh_bounds(br_mesh *m) {
    uint32_t i;
    br_mesh_drop_bvh(m);                                        /* (the triangles may have moved) */
    if (!m->vert_count) { m->min = m->max = br_v3(0, 0, 0); return; }
    m->min = m->max = m->verts[0].pos;
    for (i = 1; i < m->vert_count; ++i) {
        m->min = br_v3_min(m->min, m->verts[i].pos);
        m->max = br_v3_max(m->max, m->verts[i].pos);
    }
}

static br_vertex3d vtx(br_vec3 p, br_vec3 n, float u, float v) {
    br_vertex3d x;
    x.pos = p; x.normal = n; x.uv = br_v2(u, v);
    return x;
}

/* Every triangle wound counter-clockwise as seen from where its normals point. */
static void face_out(br_mesh *m) {
    uint32_t i;
    for (i = 0; i + 2 < m->index_count; i += 3) {
        const br_vertex3d *a = &m->verts[m->indices[i]], *b = &m->verts[m->indices[i + 1]], *c = &m->verts[m->indices[i + 2]];
        br_vec3 n = br_v3_cross(br_v3_sub(b->pos, a->pos), br_v3_sub(c->pos, a->pos));
        br_vec3 want = br_v3_add(br_v3_add(a->normal, b->normal), c->normal);
        if (br_v3_dot(n, want) < 0) { uint32_t t = m->indices[i + 1]; m->indices[i + 1] = m->indices[i + 2]; m->indices[i + 2] = t; }
    }
}

bool br_mesh_cube(br_mesh *m) {
    static const float faces[6][9] = {      /* normal, u axis, v axis (u x v = normal) */
        { 1, 0, 0,  0, 0, -1,  0, 1, 0 }, { -1, 0, 0,  0, 0, 1,  0, 1, 0 },
        { 0, 1, 0,  1, 0, 0,  0, 0, -1 }, { 0, -1, 0,  1, 0, 0,  0, 0, 1 },
        { 0, 0, 1,  1, 0, 0,  0, 1, 0 },  { 0, 0, -1,  -1, 0, 0,  0, 1, 0 },
    };
    static const float corner[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    int f, k;
    if (!br_mesh_init(m, 24, 36)) return false;
    for (f = 0; f < 6; ++f) {
        br_vec3 n = br_v3(faces[f][0], faces[f][1], faces[f][2]);
        br_vec3 u = br_v3(faces[f][3], faces[f][4], faces[f][5]), v = br_v3(faces[f][6], faces[f][7], faces[f][8]);
        for (k = 0; k < 4; ++k) {
            br_vec3 p = br_v3_add(br_v3_scale(n, 0.5f), br_v3_add(br_v3_scale(u, 0.5f * corner[k][0]), br_v3_scale(v, 0.5f * corner[k][1])));
            m->verts[f * 4 + k] = vtx(p, n, (corner[k][0] + 1) * 0.5f, (corner[k][1] + 1) * 0.5f);
        }
        m->indices[f * 6 + 0] = (uint32_t)(f * 4); m->indices[f * 6 + 1] = (uint32_t)(f * 4 + 1); m->indices[f * 6 + 2] = (uint32_t)(f * 4 + 2);
        m->indices[f * 6 + 3] = (uint32_t)(f * 4); m->indices[f * 6 + 4] = (uint32_t)(f * 4 + 2); m->indices[f * 6 + 5] = (uint32_t)(f * 4 + 3);
    }
    br_mesh_bounds(m);
    return true;
}

bool br_mesh_sphere(br_mesh *m, int segs, int rings) {
    int r, s;
    uint32_t n = 0;
    if (segs < 3) segs = 3;
    if (rings < 2) rings = 2;
    if (!br_mesh_init(m, (uint32_t)((segs + 1) * (rings + 1)), (uint32_t)(segs * rings * 6))) return false;
    for (r = 0; r <= rings; ++r) {
        float th = BR_PI * (float)r / (float)rings;
        for (s = 0; s <= segs; ++s) {
            float ph = BR_TAU * (float)s / (float)segs;
            br_vec3 d = br_v3(sinf(th) * cosf(ph), cosf(th), -sinf(th) * sinf(ph));
            m->verts[r * (segs + 1) + s] = vtx(br_v3_scale(d, 0.5f), d, (float)s / (float)segs, 1.0f - (float)r / (float)rings);
        }
    }
    for (r = 0; r < rings; ++r)
        for (s = 0; s < segs; ++s) {
            uint32_t a = (uint32_t)(r * (segs + 1) + s), b = a + 1, c = a + (uint32_t)(segs + 1), d = c + 1;
            m->indices[n++] = a; m->indices[n++] = c; m->indices[n++] = b;
            m->indices[n++] = b; m->indices[n++] = c; m->indices[n++] = d;
        }
    face_out(m);
    br_mesh_bounds(m);
    return true;
}

/* A sphere pulled apart at its middle: two half spheres and the tube between. */
bool br_mesh_capsule(br_mesh *m, int segs, int rings) {
    int r, s, rows;
    uint32_t n = 0;
    if (segs < 3) segs = 3;
    if (rings < 2) rings = 2;
    rings += rings & 1;                                /* (even: a row at the equator, for each half) */
    rows = rings + 2;
    if (!br_mesh_init(m, (uint32_t)((segs + 1) * rows), (uint32_t)(segs * (rows - 1) * 6))) return false;
    for (r = 0; r < rows; ++r) {
        int ring = r <= rings / 2 ? r : r - 1;          /* (the equator twice) */
        float th = BR_PI * (float)ring / (float)rings, lift = r <= rings / 2 ? 0.5f : -0.5f;
        for (s = 0; s <= segs; ++s) {
            float ph = BR_TAU * (float)s / (float)segs;
            br_vec3 d = br_v3(sinf(th) * cosf(ph), cosf(th), -sinf(th) * sinf(ph)), p = br_v3_scale(d, 0.5f);
            p.y += lift;
            m->verts[r * (segs + 1) + s] = vtx(p, d, (float)s / (float)segs, (p.y + 1.0f) * 0.5f);
        }
    }
    for (r = 0; r + 1 < rows; ++r)
        for (s = 0; s < segs; ++s) {
            uint32_t a = (uint32_t)(r * (segs + 1) + s), b = a + 1, c = a + (uint32_t)(segs + 1), d = c + 1;
            m->indices[n++] = a; m->indices[n++] = c; m->indices[n++] = b;
            m->indices[n++] = b; m->indices[n++] = c; m->indices[n++] = d;
        }
    face_out(m);
    br_mesh_bounds(m);
    return true;
}

bool br_mesh_plane(br_mesh *m) {
    if (!br_mesh_init(m, 4, 6)) return false;
    m->verts[0] = vtx(br_v3(-0.5f, 0, 0.5f), br_v3(0, 1, 0), 0, 0);
    m->verts[1] = vtx(br_v3(0.5f, 0, 0.5f), br_v3(0, 1, 0), 1, 0);
    m->verts[2] = vtx(br_v3(0.5f, 0, -0.5f), br_v3(0, 1, 0), 1, 1);
    m->verts[3] = vtx(br_v3(-0.5f, 0, -0.5f), br_v3(0, 1, 0), 0, 1);
    m->indices[0] = 0; m->indices[1] = 1; m->indices[2] = 2;
    m->indices[3] = 0; m->indices[4] = 2; m->indices[5] = 3;
    face_out(m);
    br_mesh_bounds(m);
    return true;
}

/* A cylinder (top radius 0.5) or a cone (top radius 0). */
static bool lathe(br_mesh *m, int segs, bool cone) {
    int s;
    uint32_t v = 0, n = 0, side, cap;
    if (segs < 3) segs = 3;
    side = (uint32_t)(segs + 1) * 2;
    cap = (uint32_t)(segs + 2);
    if (!br_mesh_init(m, side + cap * (cone ? 1 : 2), (uint32_t)segs * 3 * (cone ? 2 : 4))) return false;
    for (s = 0; s <= segs; ++s) {                     /* the side: bottom and top rings */
        float ph = BR_TAU * (float)s / (float)segs, c = cosf(ph), sn = -sinf(ph);
        br_vec3 nrm = cone ? br_v3_norm(br_v3(c, 0.5f, sn)) : br_v3(c, 0, sn);
        m->verts[v++] = vtx(br_v3(0.5f * c, -0.5f, 0.5f * sn), nrm, (float)s / (float)segs, 0);
        m->verts[v++] = vtx(cone ? br_v3(0, 0.5f, 0) : br_v3(0.5f * c, 0.5f, 0.5f * sn), nrm, (float)s / (float)segs, 1);
    }
    for (s = 0; s < segs; ++s) {
        uint32_t a = (uint32_t)s * 2, b = a + 1, c = a + 2, d = a + 3;
        m->indices[n++] = a; m->indices[n++] = c; m->indices[n++] = b;
        if (!cone) { m->indices[n++] = b; m->indices[n++] = c; m->indices[n++] = d; }
    }
    {
        int k;
        for (k = 0; k < (cone ? 1 : 2); ++k) {           /* caps: the bottom (and the top) */
            float y = k ? 0.5f : -0.5f;
            br_vec3 nrm = br_v3(0, k ? 1.0f : -1.0f, 0);
            uint32_t centre = v;
            m->verts[v++] = vtx(br_v3(0, y, 0), nrm, 0.5f, 0.5f);
            for (s = 0; s <= segs; ++s) {
                float ph = BR_TAU * (float)s / (float)segs, c = cosf(ph), sn = -sinf(ph);
                m->verts[v++] = vtx(br_v3(0.5f * c, y, 0.5f * sn), nrm, 0.5f + 0.5f * c, 0.5f + 0.5f * sn);
            }
            for (s = 0; s < segs; ++s) {
                m->indices[n++] = centre; m->indices[n++] = centre + 1 + (uint32_t)s; m->indices[n++] = centre + 2 + (uint32_t)s;
            }
        }
    }
    m->vert_count = v;
    m->index_count = n;
    face_out(m);
    br_mesh_bounds(m);
    return true;
}
bool br_mesh_cylinder(br_mesh *m, int segments) { return lathe(m, segments, false); }
bool br_mesh_cone(br_mesh *m, int segments) { return lathe(m, segments, true); }

/* ---- .obj ---------------------------------------------------------------------------- */

typedef struct obj_list { float *v; uint32_t n, cap; } obj_list;
static bool obj_push(obj_list *l, const float *x, int k) {
    if (l->n + (uint32_t)k > l->cap) {
        uint32_t cap = l->cap ? l->cap * 2 : 256;
        float *g;
        while (cap < l->n + (uint32_t)k) cap *= 2;
        if (!(g = (float *)br_realloc(l->v, sizeof(float) * cap))) return false;
        l->v = g;
        l->cap = cap;
    }
    memcpy(l->v + l->n, x, sizeof(float) * (size_t)k);
    l->n += (uint32_t)k;
    return true;
}

/* "7/3/2", "7//2", "7": positions, uvs and normals (0 = none), negative = from the end. */
static bool obj_corner(const char *s, uint32_t counts[3], long out[3]) {
    int part = 0;
    out[0] = out[1] = out[2] = 0;
    while (*s && part < 3) {
        char *end;
        if (*s == '/') { ++part; ++s; continue; }
        out[part] = strtol(s, &end, 10);
        if (end == s) return false;
        if (out[part] < 0) out[part] = (long)counts[part] + out[part] + 1;
        if (out[part] < 1 || out[part] > (long)counts[part]) return false;
        s = end;
    }
    return out[0] > 0;
}

bool br_mesh_load_obj_mem(br_mesh *m, const char *text, size_t size, char *err, size_t err_size) {
    obj_list pos = { 0 }, uv = { 0 }, nrm = { 0 };
    br_vertex3d *verts = NULL;
    uint32_t *idx = NULL, vn = 0, vcap = 0, in = 0, icap = 0;
    const char *p = text, *end = text + size;
    int line = 0;
    bool ok = true;
    memset(m, 0, sizeof(*m));
    if (err && err_size) err[0] = 0;
    while (p < end && ok) {
        char buf[1024];
        size_t len = 0;
        ++line;
        while (p < end && *p != '\n' && len < sizeof(buf) - 1) buf[len++] = *p++;
        while (p < end && *p != '\n') ++p;
        if (p < end) ++p;
        buf[len] = 0;
        if (buf[0] == 'v' && (buf[1] == ' ' || buf[1] == '\t')) {
            float x[3] = { 0, 0, 0 };
            if (sscanf(buf + 2, "%f %f %f", &x[0], &x[1], &x[2]) < 3) ok = false; else ok = obj_push(&pos, x, 3);
        } else if (buf[0] == 'v' && buf[1] == 't') {
            float x[2] = { 0, 0 };
            if (sscanf(buf + 3, "%f %f", &x[0], &x[1]) < 1) ok = false; else ok = obj_push(&uv, x, 2);
        } else if (buf[0] == 'v' && buf[1] == 'n') {
            float x[3] = { 0, 0, 0 };
            if (sscanf(buf + 3, "%f %f %f", &x[0], &x[1], &x[2]) < 3) ok = false; else ok = obj_push(&nrm, x, 3);
        } else if (buf[0] == 'f' && (buf[1] == ' ' || buf[1] == '\t')) {
            uint32_t counts[3], first = vn, k = 0;
            char *q = buf + 2;
            bool any_normal = true;
            counts[0] = pos.n / 3; counts[1] = uv.n / 2; counts[2] = nrm.n / 3;
            for (;; ++k) {                                       /* the corners, one word each */
                char tok[64];
                size_t tl = 0;
                long c[3];
                br_vertex3d v;
                while (*q == ' ' || *q == '\t' || *q == '\r') ++q;
                if (!*q) break;
                while (*q && *q != ' ' && *q != '\t' && *q != '\r' && tl < sizeof(tok) - 1) tok[tl++] = *q++;
                tok[tl] = 0;
                if (!obj_corner(tok, counts, c)) { ok = false; break; }
                memset(&v, 0, sizeof(v));
                v.pos = br_v3(pos.v[(c[0] - 1) * 3], pos.v[(c[0] - 1) * 3 + 1], pos.v[(c[0] - 1) * 3 + 2]);
                if (c[1]) v.uv = br_v2(uv.v[(c[1] - 1) * 2], uv.v[(c[1] - 1) * 2 + 1]);
                if (c[2]) v.normal = br_v3(nrm.v[(c[2] - 1) * 3], nrm.v[(c[2] - 1) * 3 + 1], nrm.v[(c[2] - 1) * 3 + 2]);
                else any_normal = false;
                if (vn == vcap) {
                    br_vertex3d *g;
                    vcap = vcap ? vcap * 2 : 256;
                    if (!(g = (br_vertex3d *)br_realloc(verts, sizeof(*verts) * vcap))) { ok = false; break; }
                    verts = g;
                }
                verts[vn++] = v;
                if (k >= 2) {                                    /* a fan from the first corner */
                    if (in + 3 > icap) {
                        uint32_t *g;
                        icap = icap ? icap * 2 : 768;
                        if (!(g = (uint32_t *)br_realloc(idx, sizeof(*idx) * icap))) { ok = false; break; }
                        idx = g;
                    }
                    idx[in++] = first; idx[in++] = vn - 2; idx[in++] = vn - 1;
                }
            }
            if (ok && k < 3) ok = false;
            if (ok && !any_normal && vn - first >= 3) {          /* the face's own normal */
                br_vec3 n = br_v3_norm(br_v3_cross(br_v3_sub(verts[first + 1].pos, verts[first].pos),
                                                   br_v3_sub(verts[first + 2].pos, verts[first].pos)));
                uint32_t j;
                for (j = first; j < vn; ++j) verts[j].normal = n;
            }
        }
        if (!ok && err && err_size) snprintf(err, err_size, "line %d isn't something a .obj file can have", line);
    }
    br_free(pos.v); br_free(uv.v); br_free(nrm.v);
    if (ok && in == 0) {
        ok = false;
        if (err && err_size) snprintf(err, err_size, "it has no faces");
    }
    if (!ok) { br_free(verts); br_free(idx); return false; }
    m->verts = verts; m->vert_count = vn;
    m->indices = idx; m->index_count = in;
    br_mesh_bounds(m);
    return true;
}

bool br_mesh_load_obj(br_mesh *m, const char *path, char *err, size_t err_size) {
    size_t size;
    char *text = (char *)br_file_read_all(path, &size);
    bool ok;
    if (!text) {
        if (err && err_size) snprintf(err, err_size, "can't read %s", path);
        memset(m, 0, sizeof(*m));
        return false;
    }
    ok = br_mesh_load_obj_mem(m, text, size, err, err_size);
    br_free(text);
    return ok;
}

/* ---- the BVH ------------------------------------------------------------------------- */

typedef struct bvh_node { br_vec3 lo, hi; uint32_t start, count, right; } bvh_node;   /* count 0: inner (children: this + 1, right) */
struct br_mesh_bvh { bvh_node *nodes; uint32_t node_count; uint32_t *tris; uint32_t tri_count; };

#define BVH_LEAF 4

static br_vec3 tri_lo(const br_mesh *m, uint32_t t) {
    const uint32_t *ix = m->indices + t * 3;
    return br_v3_min(m->verts[ix[0]].pos, br_v3_min(m->verts[ix[1]].pos, m->verts[ix[2]].pos));
}
static br_vec3 tri_hi(const br_mesh *m, uint32_t t) {
    const uint32_t *ix = m->indices + t * 3;
    return br_v3_max(m->verts[ix[0]].pos, br_v3_max(m->verts[ix[1]].pos, m->verts[ix[2]].pos));
}
static float tri_mid(const br_mesh *m, uint32_t t, int axis) {
    br_vec3 a = tri_lo(m, t), b = tri_hi(m, t);
    return axis == 0 ? a.x + b.x : axis == 1 ? a.y + b.y : a.z + b.z;
}

static uint32_t bvh_make(struct br_mesh_bvh *b, const br_mesh *m, uint32_t start, uint32_t count, int depth) {
    uint32_t at = b->node_count++, i;
    bvh_node *n = &b->nodes[at];
    br_vec3 lo = tri_lo(m, b->tris[start]), hi = tri_hi(m, b->tris[start]), clo, chi;
    clo = chi = br_v3_scale(br_v3_add(lo, hi), 0.5f);
    for (i = start + 1; i < start + count; ++i) {
        br_vec3 a = tri_lo(m, b->tris[i]), c = tri_hi(m, b->tris[i]), mid = br_v3_scale(br_v3_add(a, c), 0.5f);
        lo = br_v3_min(lo, a);
        hi = br_v3_max(hi, c);
        clo = br_v3_min(clo, mid);
        chi = br_v3_max(chi, mid);
    }
    n->lo = lo;
    n->hi = hi;
    n->start = start;
    n->count = count;
    n->right = 0;
    if (count > BVH_LEAF && depth < 48) {
        br_vec3 ext = br_v3_sub(chi, clo);
        int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
        float split = axis == 0 ? clo.x + chi.x : axis == 1 ? clo.y + chi.y : clo.z + chi.z;   /* (sums: tri_mid is twice the centre) */
        uint32_t lo_i = start, hi_i = start + count;
        while (lo_i < hi_i) {                                   /* partition about the middle of the centres */
            if (tri_mid(m, b->tris[lo_i], axis) < split) ++lo_i;
            else { uint32_t t = b->tris[lo_i]; b->tris[lo_i] = b->tris[--hi_i]; b->tris[hi_i] = t; }
        }
        if (lo_i == start || lo_i == start + count) lo_i = start + count / 2;   /* (all alike: halves) */
        b->nodes[at].count = 0;
        bvh_make(b, m, start, lo_i - start, depth + 1);
        b->nodes[at].right = bvh_make(b, m, lo_i, start + count - lo_i, depth + 1);
    }
    return at;
}

void br_mesh_drop_bvh(br_mesh *m) {
    if (!m->bvh) return;
    br_free(m->bvh->nodes);
    br_free(m->bvh->tris);
    br_free(m->bvh);
    m->bvh = NULL;
}

bool br_mesh_build_bvh(br_mesh *m) {
    struct br_mesh_bvh *b;
    uint32_t n = m->index_count / 3, i;
    br_mesh_drop_bvh(m);
    if (!n) return false;
    if (!(b = (struct br_mesh_bvh *)br_calloc(1, sizeof(*b)))) return false;
    b->tris = (uint32_t *)br_alloc(sizeof(uint32_t) * n);
    b->nodes = (bvh_node *)br_alloc(sizeof(bvh_node) * 2 * n);
    if (!b->tris || !b->nodes) { br_free(b->tris); br_free(b->nodes); br_free(b); return false; }
    for (i = 0; i < n; ++i) b->tris[i] = i;
    b->tri_count = n;
    bvh_make(b, m, 0, n, 0);
    m->bvh = b;
    return true;
}

static bool box_touch(br_vec3 alo, br_vec3 ahi, br_vec3 blo, br_vec3 bhi) {
    return !(alo.x > bhi.x || ahi.x < blo.x || alo.y > bhi.y || ahi.y < blo.y || alo.z > bhi.z || ahi.z < blo.z);
}

uint32_t br_mesh_triangles_in(const br_mesh *cm, br_vec3 lo, br_vec3 hi, uint32_t *out, uint32_t max) {
    br_mesh *m = (br_mesh *)cm;
    uint32_t stack[96], sp = 0, found = 0, i;
    if (!m->bvh && m->index_count >= 3) br_mesh_build_bvh(m);
    if (!m->bvh) return 0;
    stack[sp++] = 0;
    while (sp) {
        const bvh_node *n = &m->bvh->nodes[stack[--sp]];
        uint32_t at = (uint32_t)(n - m->bvh->nodes);
        if (!box_touch(n->lo, n->hi, lo, hi)) continue;
        if (n->count) {
            for (i = n->start; i < n->start + n->count; ++i) {
                uint32_t t = m->bvh->tris[i];
                if (!box_touch(tri_lo(m, t), tri_hi(m, t), lo, hi)) continue;
                if (found < max) out[found] = t;
                ++found;
            }
        } else if (sp + 2 <= 96) {
            stack[sp++] = n->right;
            stack[sp++] = at + 1;
        }
    }
    return found;
}

/* Where a ray enters a box (or false), within 0..best. */
static bool ray_box_t(br_vec3 o, br_vec3 inv, br_vec3 lo, br_vec3 hi, float best) {
    float t0 = 0, t1 = best, a, b, k;
    int ax;
    for (ax = 0; ax < 3; ++ax) {
        float oo = ax == 0 ? o.x : ax == 1 ? o.y : o.z, ii = ax == 0 ? inv.x : ax == 1 ? inv.y : inv.z;
        float l = ax == 0 ? lo.x : ax == 1 ? lo.y : lo.z, h = ax == 0 ? hi.x : ax == 1 ? hi.y : hi.z;
        a = (l - oo) * ii;
        b = (h - oo) * ii;
        if (a > b) { k = a; a = b; b = k; }
        if (a != a) a = -1e30f;                                  /* (0 * inf on a face) */
        if (b != b) b = 1e30f;
        if (a > t0) t0 = a;
        if (b < t1) t1 = b;
        if (t0 > t1) return false;
    }
    return true;
}

static bool tri_hit(const br_mesh *m, uint32_t tri, br_vec3 o, br_vec3 d, float *t_out, br_vec3 *n_out) {
    br_vec3 a = m->verts[m->indices[tri * 3]].pos, b = m->verts[m->indices[tri * 3 + 1]].pos, c = m->verts[m->indices[tri * 3 + 2]].pos;
    br_vec3 e1 = br_v3_sub(b, a), e2 = br_v3_sub(c, a), pv = br_v3_cross(d, e2), tv, qv;   /* Moller-Trumbore */
    float det = br_v3_dot(e1, pv), inv, u, v, t;
    if (fabsf(det) < 1e-12f) return false;
    inv = 1.0f / det;
    tv = br_v3_sub(o, a);
    u = br_v3_dot(tv, pv) * inv;
    if (u < 0 || u > 1) return false;
    qv = br_v3_cross(tv, e1);
    v = br_v3_dot(d, qv) * inv;
    if (v < 0 || u + v > 1) return false;
    t = br_v3_dot(e2, qv) * inv;
    if (t < 0) return false;
    *t_out = t;
    *n_out = br_v3_cross(e1, e2);
    return true;
}

bool br_mesh_raycast(const br_mesh *cm, br_vec3 o, br_vec3 d, float *t_out, br_vec3 *n_out) {
    br_mesh *m = (br_mesh *)cm;
    float best = 1e30f, t;
    br_vec3 bn = br_v3(0, 0, 0), n;
    uint32_t i, tris = m->index_count / 3, best_tri = 0xffffffffu;
    if (!m->bvh && tris > 32) br_mesh_build_bvh(m);              /* (worth it: made once) */
    if (m->bvh) {
        br_vec3 inv = br_v3(1.0f / d.x, 1.0f / d.y, 1.0f / d.z);
        uint32_t stack[96], sp = 0;
        stack[sp++] = 0;
        while (sp) {
            uint32_t at = stack[--sp];
            const bvh_node *nd = &m->bvh->nodes[at];
            if (!ray_box_t(o, inv, nd->lo, nd->hi, best)) continue;
            if (nd->count) {
                for (i = nd->start; i < nd->start + nd->count; ++i) {
                    uint32_t tri = m->bvh->tris[i];
                    if (tri_hit(m, tri, o, d, &t, &n) && (t < best || (t == best && tri < best_tri))) { best = t; bn = n; best_tri = tri; }
                }
            } else if (sp + 2 <= 96) {
                stack[sp++] = nd->right;
                stack[sp++] = at + 1;
            }
        }
    } else {
        for (i = 0; i < tris; ++i)
            if (tri_hit(m, i, o, d, &t, &n) && t < best) { best = t; bn = n; }   /* (the first, as with the BVH) */
    }
    if (best >= 1e30f) return false;
    if (t_out) *t_out = best;
    if (n_out) *n_out = br_v3_norm(bn);
    return true;
}

/* ---- camera, material --------------------------------------------------------------- */

br_camera3d br_camera3d_make(br_vec3 position, br_vec3 target) {
    br_camera3d c;
    memset(&c, 0, sizeof(c));
    c.position = position;
    c.rotation = br_quat_look(br_v3_sub(target, position), br_v3(0, 1, 0));
    c.fov = 60;
    c.size = 5;
    c.near_clip = 0.1f;
    c.far_clip = 1000;
    return c;
}

br_mat4 br_camera3d_view(const br_camera3d *c) {
    return br_m4_mul(br_quat_to_m4(br_quat_conj(br_quat_norm(c->rotation))), br_m4_translate(br_v3_neg(c->position)));
}

br_mat4 br_camera3d_projection(const br_camera3d *c, float aspect) {
    float n = c->near_clip > 1e-4f ? c->near_clip : 1e-4f, f = c->far_clip > n * 1.001f ? c->far_clip : n * 1000;
    if (aspect <= 0) aspect = 1;
    if (c->orthographic) {
        float h = c->size > 1e-4f ? c->size : 1e-4f;
        return br_m4_ortho(-h * aspect, h * aspect, -h, h, n, f);
    }
    return br_m4_perspective(br_clampf(c->fov, 1, 179) * BR_DEG2RAD, aspect, n, f);
}

br_material br_material_make(br_color color) {
    br_material m;
    m.color = color;
    m.texture = NULL;
    m.lit = true;
    m.cull = true;
    m.uv_scale = br_v2(1, 1);
    return m;
}

/* ---- the pass ------------------------------------------------------------------------- */

void br_r3d_init(br_r3d *r3) {
    memset(r3, 0, sizeof(*r3));
    r3->ambient = br_rgb(56, 60, 72);
}

void br_r3d_destroy(br_r3d *r3) {
    br_free(r3->depth);
    br_free(r3->shadow_depth);
    memset(r3, 0, sizeof(*r3));
}

bool br_r3d_begin(br_r3d *r3, br_renderer *r, const br_camera3d *camera, br_recti viewport) {
    int y;
    br_recti vp;
    if (!r || !r->target || !r->target->pixels) return false;
    if (r3->depth_w != r->target->width || r3->depth_h != r->target->height || !r3->depth) {
        float *d = (float *)br_realloc(r3->depth, sizeof(float) * (size_t)r->target->width * (size_t)r->target->height);
        if (!d) return false;
        r3->depth = d;
        r3->depth_w = r->target->width;
        r3->depth_h = r->target->height;
    }
    r3->r = r;
    r3->camera = *camera;
    r3->viewport = viewport;
    r3->view = br_camera3d_view(camera);
    r3->proj = br_camera3d_projection(camera, viewport.h > 0 ? (float)viewport.w / (float)viewport.h : 1);
    r3->view_proj = br_m4_mul(r3->proj, r3->view);
    r3->light_count = 0;
    r3->shadows = false;
    memset(&r3->stats, 0, sizeof(r3->stats));
    vp = br_recti_intersect(viewport, br_recti_make(0, 0, r3->depth_w, r3->depth_h));
    for (y = vp.y; y < vp.y + vp.h; ++y) {
        float *row = r3->depth + (size_t)y * (size_t)r3->depth_w + (size_t)vp.x;
        int x;
        for (x = 0; x < vp.w; ++x) row[x] = 1.0f;
    }
    return true;
}

bool br_r3d_add_light(br_r3d *r3, const br_light3d *l) {
    if (r3->light_count >= BR_R3D_MAX_LIGHTS) return false;
    r3->lights[r3->light_count++] = *l;
    return true;
}

/* A vertex on its way to the screen: clip space, then the things to interpolate. */
#define NA 12                                     /* u, v, light r, g, b, fog; the shadow light's r, g, b; light-space x, y, z */
#define NA_PLAIN 6                                /* (without shadows, only the first six) */
typedef struct cvert { br_vec4 c; float a[NA]; } cvert;
typedef struct svert { float x, y, z, iw, a[NA]; } svert;

static float br_maxf0(float v) { return v > 0 ? v : 0; }

static void light_vertex(const br_r3d *r3, br_vec3 p, br_vec3 n, float out[3], float caster[3]) {
    int i;
    caster[0] = caster[1] = caster[2] = 0;
    out[0] = r3->ambient.r / 255.0f; out[1] = r3->ambient.g / 255.0f; out[2] = r3->ambient.b / 255.0f;
    for (i = 0; i < r3->light_count; ++i) {
        const br_light3d *l = &r3->lights[i];
        float k = 0;
        if (l->kind == BR_LIGHT_AMBIENT) k = l->intensity;
        else if (l->kind == BR_LIGHT_DIRECTIONAL) k = l->intensity * br_maxf0(-br_v3_dot(n, br_v3_norm(l->direction)));
        else {
            br_vec3 to = br_v3_sub(l->position, p);
            float d = br_v3_len(to), fall;
            if (d >= l->range || d < 1e-6f) continue;
            fall = 1.0f - d / l->range;
            k = l->intensity * fall * fall * br_maxf0(br_v3_dot(n, br_v3_scale(to, 1.0f / d)));
        }
        out[0] += k * l->color.r / 255.0f;
        out[1] += k * l->color.g / 255.0f;
        out[2] += k * l->color.b / 255.0f;
        if (r3->shadows && i == r3->shadow_light) {     /* what a shadow takes away */
            caster[0] = k * l->color.r / 255.0f;
            caster[1] = k * l->color.g / 255.0f;
            caster[2] = k * l->color.b / 255.0f;
        }
    }
}

/* ---- shadow maps ---- */

bool br_r3d_shadow_begin(br_r3d *r3, int light, br_vec3 centre, float radius, int size) {
    br_vec3 dir, up, eye, lc;
    br_mat4 view, proj, inv;
    float texel;
    size_t n, i;
    if (light < 0 || light >= r3->light_count || r3->lights[light].kind != BR_LIGHT_DIRECTIONAL || radius <= 0) return false;
    if (size < 64) size = 64;
    if (size > 4096) size = 4096;
    n = (size_t)size * (size_t)size;
    if (r3->shadow_size != size || !r3->shadow_depth) {
        float *d = (float *)br_realloc(r3->shadow_depth, sizeof(float) * n);
        if (!d) return false;
        r3->shadow_depth = d;
        r3->shadow_size = size;
    }
    for (i = 0; i < n; ++i) r3->shadow_depth[i] = 1.0f;
    dir = br_v3_norm(r3->lights[light].direction);
    up = fabsf(dir.y) > 0.99f ? br_v3(0, 0, 1) : br_v3(0, 1, 0);
    view = br_m4_look_at(br_v3(0, 0, 0), dir, up);
    texel = 2 * radius / (float)size;
    lc = br_m4_mul_point(view, centre);                          /* steady: the centre moves a whole texel at a time */
    lc.x = floorf(lc.x / texel) * texel;
    lc.y = floorf(lc.y / texel) * texel;
    inv = br_m4_inverse(view);
    centre = br_m4_mul_point(inv, lc);
    eye = br_v3_sub(centre, br_v3_scale(dir, radius * 3));
    view = br_m4_look_at(eye, centre, up);
    proj = br_m4_ortho(-radius, radius, -radius, radius, 0.01f, radius * 6);
    r3->shadow_vp = br_m4_mul(proj, view);
    r3->shadow_light = light;
    r3->shadow_texel = texel;
    if (r3->shadow_strength <= 0) r3->shadow_strength = 0.85f;
    r3->shadows = false;
    return true;
}

/* Whether a mesh's bounding box is wholly off the shadow map (to a side, or past its far
 * end); things between the light and the map still cast their shadows on to it. */
static bool mesh_off_shadow_map(const br_mesh *m, br_mat4 mvp) {
    int k, out_l = 0, out_r = 0, out_b = 0, out_t = 0, out_f = 0;
    for (k = 0; k < 8; ++k) {
        br_vec3 p = br_v3((k & 1) ? m->max.x : m->min.x, (k & 2) ? m->max.y : m->min.y, (k & 4) ? m->max.z : m->min.z);
        br_vec3 c = br_m4_mul_point(mvp, p);
        out_l += c.x < -1; out_r += c.x > 1;
        out_b += c.y < -1; out_t += c.y > 1;
        out_f += c.z > 1;
    }
    return out_l == 8 || out_r == 8 || out_b == 8 || out_t == 8 || out_f == 8;
}

void br_r3d_shadow_mesh(br_r3d *r3, const br_mesh *m, br_mat4 model) {
    br_mat4 mvp;
    uint32_t i;
    int size = r3->shadow_size;
    if (!r3->shadow_depth || !m || !m->index_count) return;
    mvp = br_m4_mul(r3->shadow_vp, model);
    if (mesh_off_shadow_map(m, mvp)) return;
    for (i = 0; i + 2 < m->index_count; i += 3) {
        float sx[3], sy[3], sz[3], minx, maxx, miny, maxy, area, inv;
        int k, x0, x1, y0, y1, x, y;
        bool out = true;
        for (k = 0; k < 3; ++k) {                                 /* (orthographic: w is 1) */
            br_vec3 p = br_m4_mul_point(mvp, m->verts[m->indices[i + (uint32_t)k]].pos);
            sx[k] = (p.x * 0.5f + 0.5f) * (float)size;
            sy[k] = (0.5f - p.y * 0.5f) * (float)size;
            sz[k] = p.z * 0.5f + 0.5f;
            if (sz[k] <= 1) out = false;
        }
        if (out) continue;
        area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (fabsf(area) < 1e-9f) continue;
        inv = 1.0f / area;
        minx = BR_MIN(sx[0], BR_MIN(sx[1], sx[2])); maxx = BR_MAX(sx[0], BR_MAX(sx[1], sx[2]));
        miny = BR_MIN(sy[0], BR_MIN(sy[1], sy[2])); maxy = BR_MAX(sy[0], BR_MAX(sy[1], sy[2]));
        x0 = BR_MAX((int)floorf(minx), 0); x1 = BR_MIN((int)ceilf(maxx), size);
        y0 = BR_MAX((int)floorf(miny), 0); y1 = BR_MIN((int)ceilf(maxy), size);
        for (y = y0; y < y1; ++y) {
            float py = (float)y + 0.5f, *row = r3->shadow_depth + (size_t)y * (size_t)size;
            for (x = x0; x < x1; ++x) {
                float px = (float)x + 0.5f;
                float b1 = ((px - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (py - sy[0])) * inv;
                float b2 = ((sx[1] - sx[0]) * (py - sy[0]) - (px - sx[0]) * (sy[1] - sy[0])) * inv;
                float b0 = 1 - b1 - b2, z;
                if (b0 < 0 || b1 < 0 || b2 < 0) continue;
                z = sz[0] * b0 + sz[1] * b1 + sz[2] * b2;
                if (z < 0) z = 0;
                if (z < row[x]) row[x] = z;
            }
        }
    }
}

void br_r3d_shadow_end(br_r3d *r3) { r3->shadows = r3->shadow_depth != NULL; }

/* How much of the shadow light reaches a point (light-space x, y, z): 0..1, from four texels. */
static float shadow_lit(const br_r3d *r3, float lx, float ly, float lz) {
    int size = r3->shadow_size, k, lit = 0;
    float fx0 = (lx * 0.5f + 0.5f) * (float)size - 0.5f, fy0 = (0.5f - ly * 0.5f) * (float)size - 0.5f;
    float z = lz * 0.5f + 0.5f, bias = 0.0015f + 2.0f / (float)size;
    int ix = (int)floorf(fx0), iy = (int)floorf(fy0);
    if (z > 1) return 1;
    for (k = 0; k < 4; ++k) {
        int x = ix + (k & 1), y = iy + (k >> 1);
        if (x < 0 || y < 0 || x >= size || y >= size) { ++lit; continue; }   /* (outside the map: lit) */
        if (z <= r3->shadow_depth[(size_t)y * (size_t)size + (size_t)x] + bias) ++lit;
    }
    return (float)lit * 0.25f;
}

static cvert lerp_cv(const cvert *a, const cvert *b, float t) {
    cvert r;
    int i;
    r.c = br_v4(br_lerpf(a->c.x, b->c.x, t), br_lerpf(a->c.y, b->c.y, t), br_lerpf(a->c.z, b->c.z, t), br_lerpf(a->c.w, b->c.w, t));
    for (i = 0; i < NA; ++i) r.a[i] = br_lerpf(a->a[i], b->a[i], t);
    return r;
}

/* Keeps the part of a polygon in front of the near plane (z >= -w). */
static int clip_near(const cvert *in, int n, cvert *out) {
    int i, k = 0;
    for (i = 0; i < n; ++i) {
        const cvert *a = &in[i], *b = &in[(i + 1) % n];
        float da = a->c.z + a->c.w, db = b->c.z + b->c.w;
        if (da >= 0) out[k++] = *a;
        if ((da >= 0) != (db >= 0)) out[k++] = lerp_cv(a, b, da / (da - db));
    }
    return k;
}

static svert to_screen(const br_r3d *r3, const cvert *v) {
    svert s;
    float iw = 1.0f / (fabsf(v->c.w) > 1e-9f ? v->c.w : 1e-9f);
    int i;
    s.x = (float)r3->viewport.x + (v->c.x * iw * 0.5f + 0.5f) * (float)r3->viewport.w;
    s.y = (float)r3->viewport.y + (0.5f - v->c.y * iw * 0.5f) * (float)r3->viewport.h;
    s.z = v->c.z * iw * 0.5f + 0.5f;
    s.iw = iw;
    for (i = 0; i < NA; ++i) s.a[i] = v->a[i] * iw;
    return s;
}

#define SUB 256
typedef long long f64i;
static f64i fx(float v) { return (f64i)floor((double)v * SUB + 0.5); }
static f64i edge3(f64i ax, f64i ay, f64i bx, f64i by, f64i px, f64i py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }
static bool owns(f64i ax, f64i ay, f64i bx, f64i by) { f64i dy = by - ay, dx = bx - ax; return dy < 0 || (dy == 0 && dx > 0); }

static uint8_t clamp255(float v) { return v <= 0 ? 0 : v >= 255 ? 255 : (uint8_t)(v + 0.5f); }

static void raster(br_r3d *r3, svert v0, svert v1, svert v2, const br_material *mat, bool cull, int na) {
    br_renderer *r = r3->r;
    br_image *img = r->target;
    const br_image *tex = mat->texture && mat->texture->pixels ? mat->texture : NULL;
    f64i x0 = fx(v0.x), y0 = fx(v0.y), x1 = fx(v1.x), y1 = fx(v1.y), x2 = fx(v2.x), y2 = fx(v2.y);
    f64i area, minx, maxx, miny, maxy;
    br_recti box = br_recti_intersect(r->clip, r3->viewport);
    int ix0, iy0, ix1, iy1, x, y;
    bool o0, o1, o2;
    float inv;
    if (!(v0.x == v0.x && v1.x == v1.x && v2.x == v2.x)) return;
    if (fabsf(v0.x) > 1e7f || fabsf(v0.y) > 1e7f || fabsf(v1.x) > 1e7f || fabsf(v1.y) > 1e7f || fabsf(v2.x) > 1e7f || fabsf(v2.y) > 1e7f) return;
    area = edge3(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    if (area > 0 && cull) { r3->stats.culled++; return; }   /* (front faces come out with area < 0: y points down) */
    if (area < 0) {                                    /* put it the other way round, so inside is w >= 0 */
        svert t = v1; v1 = v2; v2 = t;
        x1 = fx(v1.x); y1 = fx(v1.y); x2 = fx(v2.x); y2 = fx(v2.y);
        area = -area;
    }
    inv = 1.0f / (float)area;
    minx = BR_MIN(x0, BR_MIN(x1, x2)); maxx = BR_MAX(x0, BR_MAX(x1, x2));
    miny = BR_MIN(y0, BR_MIN(y1, y2)); maxy = BR_MAX(y0, BR_MAX(y1, y2));
    ix0 = BR_MAX((int)floor((double)(minx - SUB / 2) / SUB), box.x);
    iy0 = BR_MAX((int)floor((double)(miny - SUB / 2) / SUB), box.y);
    ix1 = BR_MIN((int)floor((double)(maxx - SUB / 2) / SUB) + 1, box.x + box.w);
    iy1 = BR_MIN((int)floor((double)(maxy - SUB / 2) / SUB) + 1, box.y + box.h);
    o0 = owns(x1, y1, x2, y2); o1 = owns(x2, y2, x0, y0); o2 = owns(x0, y0, x1, y1);
    r3->stats.triangles++;
    for (y = iy0; y < iy1; ++y) {
        br_color *row = img->pixels + (size_t)y * (size_t)img->width;
        float *drow = r3->depth + (size_t)y * (size_t)r3->depth_w;
        f64i py = (f64i)y * SUB + SUB / 2;
        for (x = ix0; x < ix1; ++x) {
            f64i px = (f64i)x * SUB + SUB / 2;
            f64i w0 = edge3(x1, y1, x2, y2, px, py), w1 = edge3(x2, y2, x0, y0, px, py), w2 = edge3(x0, y0, x1, y1, px, py);
            float b0, b1, b2, z, iw, a[NA], cr, cg, cb, ca;
            int i;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            if ((w0 == 0 && !o0) || (w1 == 0 && !o1) || (w2 == 0 && !o2)) continue;
            b0 = (float)w0 * inv; b1 = (float)w1 * inv; b2 = (float)w2 * inv;
            z = v0.z * b0 + v1.z * b1 + v2.z * b2;
            if (z < 0 || z > 1 || z >= drow[x]) continue;
            iw = v0.iw * b0 + v1.iw * b1 + v2.iw * b2;
            if (iw <= 0) continue;
            for (i = 0; i < na; ++i) a[i] = (v0.a[i] * b0 + v1.a[i] * b1 + v2.a[i] * b2) / iw;
            cr = mat->color.r; cg = mat->color.g; cb = mat->color.b; ca = mat->color.a;
            if (tex) {
                float u = a[0] * mat->uv_scale.x, v = a[1] * mat->uv_scale.y;
                int tx, ty;
                br_color t;
                u -= floorf(u); v -= floorf(v);
                tx = (int)(u * (float)tex->width); ty = (int)((1.0f - v) * (float)tex->height);
                if (tx >= tex->width) tx = tex->width - 1;
                if (ty >= tex->height) ty = tex->height - 1;
                if (ty < 0) ty = 0;
                t = tex->pixels[(size_t)ty * (size_t)tex->width + (size_t)tx];
                cr = cr * t.r / 255.0f; cg = cg * t.g / 255.0f; cb = cb * t.b / 255.0f; ca = ca * t.a / 255.0f;
            }
            if (ca < 0.5f) continue;
            if (na > NA_PLAIN && (a[6] > 0 || a[7] > 0 || a[8] > 0)) {   /* in the shadow light's shadow: less of its light */
                float off = (1 - shadow_lit(r3, a[9], a[10], a[11])) * r3->shadow_strength;
                a[2] -= a[6] * off; a[3] -= a[7] * off; a[4] -= a[8] * off;
            }
            cr *= a[2]; cg *= a[3]; cb *= a[4];
            if (r3->fog) {
                float f = a[5] < 0 ? 0 : a[5] > 1 ? 1 : a[5];
                cr += (r3->fog_color.r - cr) * f; cg += (r3->fog_color.g - cg) * f; cb += (r3->fog_color.b - cb) * f;
            }
            if (ca >= 254.5f) {
                row[x] = br_rgba(clamp255(cr), clamp255(cg), clamp255(cb), 255);
                drow[x] = z;
            } else {                                     /* see-through: blended, depth left alone */
                br_color *d = &row[x];
                float k = ca / 255.0f;
                d->r = clamp255(cr * k + d->r * (1 - k));
                d->g = clamp255(cg * k + d->g * (1 - k));
                d->b = clamp255(cb * k + d->b * (1 - k));
                d->a = clamp255(ca + d->a * (1 - k));
            }
            r3->stats.pixels++;
        }
    }
}

/* Whether a mesh's bounding box is wholly outside the view (beyond one side of the
 * frustum, near and far planes included): then none of its triangles need to be
 * looked at. */
static bool mesh_outside_view(const br_mesh *m, br_mat4 mvp) {
    int k, out_l = 0, out_r = 0, out_b = 0, out_t = 0, out_n = 0, out_f = 0;
    for (k = 0; k < 8; ++k) {
        br_vec3 p = br_v3((k & 1) ? m->max.x : m->min.x, (k & 2) ? m->max.y : m->min.y, (k & 4) ? m->max.z : m->min.z);
        br_vec4 c = br_m4_mul_v4(mvp, br_v4(p.x, p.y, p.z, 1));
        out_l += c.x < -c.w; out_r += c.x > c.w;
        out_b += c.y < -c.w; out_t += c.y > c.w;
        out_n += c.z < -c.w; out_f += c.z > c.w;
    }
    return out_l == 8 || out_r == 8 || out_b == 8 || out_t == 8 || out_n == 8 || out_f == 8;
}

void br_r3d_draw_mesh(br_r3d *r3, const br_mesh *m, br_mat4 model, const br_material *mat) {
    br_mat4 mvp, nm;
    uint32_t i;
    bool mirrored, shadowed = r3->shadows && mat->lit;
    int na = shadowed ? NA : NA_PLAIN;
    if (!r3->r || !m || !m->index_count || !r3->depth) return;
    mvp = br_m4_mul(r3->view_proj, model);
    if (mesh_outside_view(m, mvp)) { r3->stats.culled_meshes++; return; }
    nm = br_m4_transpose(br_m4_inverse(model));        /* normals: the inverse transpose */
    mirrored = br_v3_dot(br_v3_cross(br_m4_mul_dir(model, br_v3(1, 0, 0)), br_m4_mul_dir(model, br_v3(0, 1, 0))),
                         br_m4_mul_dir(model, br_v3(0, 0, 1))) < 0;
    r3->stats.meshes++;
    for (i = 0; i + 2 < m->index_count; i += 3) {
        cvert in[3], clipped[8];
        int k, n;
        bool all_out = true;
        for (k = 0; k < 3; ++k) {
            const br_vertex3d *v = &m->verts[m->indices[i + (uint32_t)(mirrored ? 2 - k : k)]];
            float light[3] = { 1, 1, 1 }, caster[3] = { 0, 0, 0 };
            in[k].c = br_m4_mul_v4(mvp, br_v4(v->pos.x, v->pos.y, v->pos.z, 1));
            if (mat->lit) {
                br_vec3 wp = br_m4_mul_point(model, v->pos), wn = br_v3_norm(br_m4_mul_dir(nm, v->normal));
                light_vertex(r3, wp, wn, light, caster);
                if (shadowed) {
                    br_vec3 lp = br_m4_mul_point(r3->shadow_vp, wp);
                    in[k].a[9] = lp.x; in[k].a[10] = lp.y; in[k].a[11] = lp.z;
                }
            }
            in[k].a[6] = caster[0]; in[k].a[7] = caster[1]; in[k].a[8] = caster[2];
            if (!shadowed || !mat->lit) in[k].a[9] = in[k].a[10] = in[k].a[11] = 0;
            in[k].a[0] = v->uv.x; in[k].a[1] = v->uv.y;
            in[k].a[2] = light[0]; in[k].a[3] = light[1]; in[k].a[4] = light[2];
            in[k].a[5] = 0;
            if (r3->fog) {                             /* (how far into the fog, by distance from the camera) */
                float d = br_v3_dist(br_m4_mul_point(model, v->pos), r3->camera.position), span = r3->fog_end - r3->fog_start;
                in[k].a[5] = span > 1e-6f ? (d - r3->fog_start) / span : (d >= r3->fog_end ? 1.0f : 0.0f);
            }
            if (in[k].c.z + in[k].c.w >= 0) all_out = false;
        }
        if (all_out) continue;
        n = clip_near(in, 3, clipped);
        if (n < 3) continue;
        {
            svert s0 = to_screen(r3, &clipped[0]);
            for (k = 1; k + 1 < n; ++k) raster(r3, s0, to_screen(r3, &clipped[k]), to_screen(r3, &clipped[k + 1]), mat, mat->cull, na);
        }
    }
}

void br_r3d_line(br_r3d *r3, br_vec3 a, br_vec3 b, br_color c, bool depth_test) {
    br_vec4 ca, cb;
    float da, db, t;
    svert sa, sb;
    cvert va, vb;
    int steps, i;
    br_recti box;
    if (!r3->r || !r3->depth) return;
    ca = br_m4_mul_v4(r3->view_proj, br_v4(a.x, a.y, a.z, 1));
    cb = br_m4_mul_v4(r3->view_proj, br_v4(b.x, b.y, b.z, 1));
    da = ca.z + ca.w; db = cb.z + cb.w;
    if (da < 0 && db < 0) return;
    memset(&va, 0, sizeof(va)); memset(&vb, 0, sizeof(vb));
    va.c = ca; vb.c = cb;
    if (da < 0) va = lerp_cv(&va, &vb, da / (da - db));
    else if (db < 0) vb = lerp_cv(&va, &vb, da / (da - db));
    sa = to_screen(r3, &va); sb = to_screen(r3, &vb);
    if (fabsf(sa.x) > 1e6f || fabsf(sa.y) > 1e6f || fabsf(sb.x) > 1e6f || fabsf(sb.y) > 1e6f) return;
    box = br_recti_intersect(r3->r->clip, r3->viewport);
    {   /* cut the segment to the box first (a long line, clipped at the near plane, can reach far off screen) */
        float t0 = 0, t1 = 1, dx = sb.x - sa.x, dy = sb.y - sa.y, dz = sb.z - sa.z;
        float p4[4], q4[4];
        int k;
        p4[0] = -dx; q4[0] = sa.x - ((float)box.x - 1);
        p4[1] = dx;  q4[1] = ((float)(box.x + box.w) + 1) - sa.x;
        p4[2] = -dy; q4[2] = sa.y - ((float)box.y - 1);
        p4[3] = dy;  q4[3] = ((float)(box.y + box.h) + 1) - sa.y;
        for (k = 0; k < 4; ++k) {
            if (p4[k] == 0) { if (q4[k] < 0) return; continue; }
            t = q4[k] / p4[k];
            if (p4[k] < 0) { if (t > t1) return; if (t > t0) t0 = t; }
            else { if (t < t0) return; if (t < t1) t1 = t; }
        }
        sb.x = sa.x + dx * t1; sb.y = sa.y + dy * t1; sb.z = sa.z + dz * t1;
        sa.x = sa.x + dx * t0; sa.y = sa.y + dy * t0; sa.z = sa.z + dz * t0;
    }
    steps = (int)ceilf(BR_MAX(fabsf(sb.x - sa.x), fabsf(sb.y - sa.y)));
    if (steps < 1) steps = 1;
    if (steps > 8192) steps = 8192;
    for (i = 0; i <= steps; ++i) {
        float x, y, z;
        int px, py;
        t = (float)i / (float)steps;
        x = br_lerpf(sa.x, sb.x, t); y = br_lerpf(sa.y, sb.y, t); z = br_lerpf(sa.z, sb.z, t);
        px = (int)floorf(x); py = (int)floorf(y);
        if (px < box.x || py < box.y || px >= box.x + box.w || py >= box.y + box.h) continue;
        if (depth_test && (z < 0 || z > r3->depth[(size_t)py * (size_t)r3->depth_w + (size_t)px] + 2e-4f)) continue;
        {
            br_color *d = &r3->r->target->pixels[(size_t)py * (size_t)r3->r->target->width + (size_t)px];
            if (c.a == 255) *d = c;
            else {
                float k = c.a / 255.0f;
                d->r = clamp255(c.r * k + d->r * (1 - k)); d->g = clamp255(c.g * k + d->g * (1 - k)); d->b = clamp255(c.b * k + d->b * (1 - k));
            }
        }
    }
}

bool br_r3d_project(const br_r3d *r3, br_vec3 p, br_vec3 *screen) {
    br_vec4 c = br_m4_mul_v4(r3->view_proj, br_v4(p.x, p.y, p.z, 1));
    cvert v;
    svert s;
    if (c.w <= 1e-6f) return false;
    memset(&v, 0, sizeof(v));
    v.c = c;
    s = to_screen(r3, &v);
    if (screen) *screen = br_v3(s.x, s.y, s.z);
    return true;
}

void br_r3d_ray(const br_r3d *r3, br_vec2 sp, br_vec3 *origin, br_vec3 *dir) {
    br_mat4 inv = br_m4_inverse(r3->view_proj);
    float nx = r3->viewport.w > 0 ? ((sp.x - (float)r3->viewport.x) / (float)r3->viewport.w) * 2 - 1 : 0;
    float ny = r3->viewport.h > 0 ? 1 - ((sp.y - (float)r3->viewport.y) / (float)r3->viewport.h) * 2 : 0;
    br_vec4 a = br_m4_mul_v4(inv, br_v4(nx, ny, -1, 1)), b = br_m4_mul_v4(inv, br_v4(nx, ny, 1, 1));
    br_vec3 pa = br_v3(a.x / a.w, a.y / a.w, a.z / a.w), pb = br_v3(b.x / b.w, b.y / b.w, b.z / b.w);
    if (origin) *origin = pa;
    if (dir) *dir = br_v3_norm(br_v3_sub(pb, pa));
}
