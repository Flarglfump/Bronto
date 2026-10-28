/*
 * Bronto Engine - glTF 2.0 models, skins and animations (see model.h).
 *
 * A small JSON reader, then the glTF pieces: buffers (base64 data URIs, files
 * beside the model, or a .glb's binary chunk), views and accessors, images and
 * materials, meshes, nodes, skins and animations.
 */
#include "bronto/model.h"
#include "bronto/memory.h"
#include "bronto/fs.h"
#include "bronto/log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- JSON ------------------------------------------------------------------------------- */

enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };
typedef struct jnode { int type; double num; int str, key; int first, next, count; } jnode;   /* str / key: offsets in the pool (-1: none) */

typedef struct jdoc {
    jnode *n;
    int count, cap;
    char *pool;
    size_t pool_n, pool_cap;
    const char *p, *end;
    bool bad;
} jdoc;

static int j_new(jdoc *d, int type) {
    if (d->count == d->cap) {
        int cap = d->cap ? d->cap * 2 : 256;
        jnode *g = (jnode *)br_realloc(d->n, sizeof(jnode) * (size_t)cap);
        if (!g) { d->bad = true; return -1; }
        d->n = g;
        d->cap = cap;
    }
    memset(&d->n[d->count], 0, sizeof(jnode));
    d->n[d->count].type = type;
    d->n[d->count].str = d->n[d->count].key = d->n[d->count].first = d->n[d->count].next = -1;
    return d->count++;
}

static void j_pool_put(jdoc *d, char c) {
    if (d->pool_n == d->pool_cap) {
        size_t cap = d->pool_cap ? d->pool_cap * 2 : 1024;
        char *g = (char *)br_realloc(d->pool, cap);
        if (!g) { d->bad = true; return; }
        d->pool = g;
        d->pool_cap = cap;
    }
    d->pool[d->pool_n++] = c;
}

static void j_ws(jdoc *d) { while (d->p < d->end && (*d->p == ' ' || *d->p == '\t' || *d->p == '\n' || *d->p == '\r')) ++d->p; }

static int j_string(jdoc *d) {                                  /* at the opening quote: the pool offset */
    int at = (int)d->pool_n;
    ++d->p;
    while (d->p < d->end && *d->p != '"') {
        char c = *d->p++;
        if (c == '\\' && d->p < d->end) {
            c = *d->p++;
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c == 'r') c = '\r';
            else if (c == 'b') c = '\b';
            else if (c == 'f') c = '\f';
            else if (c == 'u') {                                 /* \uXXXX: as UTF-8 (pairs not joined: names are plain) */
                unsigned v = 0;
                int k;
                for (k = 0; k < 4 && d->p < d->end; ++k, ++d->p) {
                    char h = *d->p;
                    v = v * 16 + (unsigned)(h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
                }
                if (v < 0x80) j_pool_put(d, (char)v);
                else if (v < 0x800) { j_pool_put(d, (char)(0xC0 | (v >> 6))); j_pool_put(d, (char)(0x80 | (v & 63))); }
                else { j_pool_put(d, (char)(0xE0 | (v >> 12))); j_pool_put(d, (char)(0x80 | ((v >> 6) & 63))); j_pool_put(d, (char)(0x80 | (v & 63))); }
                continue;
            }
        }
        j_pool_put(d, c);
    }
    if (d->p >= d->end) d->bad = true;
    else ++d->p;
    j_pool_put(d, 0);
    return at;
}

static int j_value(jdoc *d, int depth) {
    int at;
    j_ws(d);
    if (d->p >= d->end || depth > 64) { d->bad = true; return -1; }
    if (*d->p == '{' || *d->p == '[') {
        bool obj = *d->p == '{';
        int last = -1;
        at = j_new(d, obj ? J_OBJ : J_ARR);
        if (at < 0) return -1;
        ++d->p;
        j_ws(d);
        if (d->p < d->end && *d->p == (obj ? '}' : ']')) { ++d->p; return at; }
        for (;;) {
            int key = -1, v;
            j_ws(d);
            if (obj) {
                if (d->p >= d->end || *d->p != '"') { d->bad = true; return at; }
                key = j_string(d);
                j_ws(d);
                if (d->p >= d->end || *d->p != ':') { d->bad = true; return at; }
                ++d->p;
            }
            v = j_value(d, depth + 1);
            if (v < 0 || d->bad) { d->bad = true; return at; }
            d->n[v].key = key;
            if (last < 0) d->n[at].first = v; else d->n[last].next = v;
            last = v;
            d->n[at].count++;
            j_ws(d);
            if (d->p < d->end && *d->p == ',') { ++d->p; continue; }
            if (d->p < d->end && *d->p == (obj ? '}' : ']')) { ++d->p; return at; }
            d->bad = true;
            return at;
        }
    }
    if (*d->p == '"') { at = j_new(d, J_STR); if (at >= 0) d->n[at].str = j_string(d); return at; }
    if (d->end - d->p >= 4 && strncmp(d->p, "true", 4) == 0) { d->p += 4; at = j_new(d, J_BOOL); if (at >= 0) d->n[at].num = 1; return at; }
    if (d->end - d->p >= 5 && strncmp(d->p, "false", 5) == 0) { d->p += 5; return j_new(d, J_BOOL); }
    if (d->end - d->p >= 4 && strncmp(d->p, "null", 4) == 0) { d->p += 4; return j_new(d, J_NULL); }
    {
        char buf[64], *e;
        size_t k = 0;
        while (d->p < d->end && k < sizeof(buf) - 1 && strchr("+-0123456789.eE", *d->p)) buf[k++] = *d->p++;
        buf[k] = 0;
        if (!k) { d->bad = true; return -1; }
        at = j_new(d, J_NUM);
        if (at >= 0) d->n[at].num = strtod(buf, &e);
        return at;
    }
}

static int j_get(const jdoc *d, int obj, const char *key) {
    int c;
    if (obj < 0 || d->n[obj].type != J_OBJ) return -1;
    for (c = d->n[obj].first; c >= 0; c = d->n[c].next)
        if (d->n[c].key >= 0 && strcmp(d->pool + d->n[c].key, key) == 0) return c;
    return -1;
}
static int j_at(const jdoc *d, int arr, int i) {
    int c, k = 0;
    if (arr < 0 || d->n[arr].type != J_ARR) return -1;
    for (c = d->n[arr].first; c >= 0; c = d->n[c].next, ++k) if (k == i) return c;
    return -1;
}
static int j_len(const jdoc *d, int arr) { return arr >= 0 && d->n[arr].type == J_ARR ? d->n[arr].count : 0; }
static double j_num(const jdoc *d, int v, double def) { return v >= 0 && (d->n[v].type == J_NUM || d->n[v].type == J_BOOL) ? d->n[v].num : def; }
static int j_int(const jdoc *d, int obj, const char *key, int def) { return (int)j_num(d, j_get(d, obj, key), def); }
static const char *j_str(const jdoc *d, int v) { return v >= 0 && d->n[v].type == J_STR ? d->pool + d->n[v].str : NULL; }

static void j_free(jdoc *d) { br_free(d->n); br_free(d->pool); }

/* ---- glTF ----------------------------------------------------------------------------- */

typedef struct gl_buf { unsigned char *data; size_t size; bool owned; } gl_buf;

typedef struct gl_ctx {
    jdoc j;
    int root;
    gl_buf *bufs;
    int buf_count;
    const unsigned char *glb_bin;
    size_t glb_bin_size;
    char dir[BR_PATH_MAX];
    char *err;
    size_t err_size;
    bool failed;
} gl_ctx;

static void gl_fail(gl_ctx *g, const char *msg, const char *what) {
    if (!g->failed && g->err && g->err_size) snprintf(g->err, g->err_size, msg, what ? what : "");
    g->failed = true;
}

static int b64v(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

static unsigned char *b64_decode(const char *s, size_t *out_n) {
    size_t n = strlen(s), k = 0, i;
    unsigned char *out = (unsigned char *)br_alloc(n / 4 * 3 + 4);
    uint32_t acc = 0;
    int bits = 0;
    if (!out) return NULL;
    for (i = 0; i < n; ++i) {
        int v = b64v((unsigned char)s[i]);
        if (v < 0) continue;                                    /* (padding, line breaks) */
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out[k++] = (unsigned char)(acc >> bits); }
    }
    *out_n = k;
    return out;
}

/* The bytes a URI names: data:...;base64,..., or a file beside the model. */
static unsigned char *gl_uri(gl_ctx *g, const char *uri, size_t *n) {
    if (strncmp(uri, "data:", 5) == 0) {
        const char *comma = strchr(uri, ',');
        if (!comma || !strstr(uri, ";base64")) { gl_fail(g, "only base64 data URIs are read", NULL); return NULL; }
        return b64_decode(comma + 1, n);
    }
    if (!g->dir[0] && !strchr(uri, '/')) {
        /* (no folder known: a bare name relative to the working folder) */
    }
    {
        char full[BR_PATH_MAX * 2], name[BR_PATH_MAX];
        size_t i, k = 0;
        for (i = 0; uri[i] && k + 1 < sizeof(name); ++i) {          /* %20 and friends */
            if (uri[i] == '%' && uri[i + 1] && uri[i + 2]) {
                char hex[3] = { uri[i + 1], uri[i + 2], 0 };
                name[k++] = (char)strtol(hex, NULL, 16);
                i += 2;
            } else name[k++] = uri[i];
        }
        name[k] = 0;
        if (strstr(name, "..")) { gl_fail(g, "a file outside the model's folder: %s", uri); return NULL; }
        snprintf(full, sizeof(full), "%s%s%s", g->dir, g->dir[0] ? "/" : "", name);
        {
            unsigned char *data = (unsigned char *)br_file_read_all(full, n);
            if (!data) gl_fail(g, "can't read %s", full);
            return data;
        }
    }
}

static bool gl_buffers(gl_ctx *g) {
    int arr = j_get(&g->j, g->root, "buffers"), i, n = j_len(&g->j, arr);
    g->bufs = (gl_buf *)br_calloc((size_t)(n ? n : 1), sizeof(gl_buf));
    if (!g->bufs) return false;
    g->buf_count = n;
    for (i = 0; i < n; ++i) {
        int b = j_at(&g->j, arr, i);
        const char *uri = j_str(&g->j, j_get(&g->j, b, "uri"));
        size_t len = (size_t)j_int(&g->j, b, "byteLength", 0);
        if (!uri) {                                              /* the .glb's own binary chunk */
            if (!g->glb_bin || g->glb_bin_size < len) { gl_fail(g, "buffer %s has no data", ""); return false; }
            g->bufs[i].data = (unsigned char *)g->glb_bin;
            g->bufs[i].size = g->glb_bin_size;
        } else {
            size_t got = 0;
            g->bufs[i].data = gl_uri(g, uri, &got);
            if (!g->bufs[i].data) return false;
            g->bufs[i].size = got;
            g->bufs[i].owned = true;
            if (got < len) { gl_fail(g, "a buffer is shorter than it says", NULL); return false; }
        }
    }
    return true;
}

/* A bufferView's bytes (and stride). */
static const unsigned char *gl_view(gl_ctx *g, int view, size_t *len, int *stride) {
    int v = j_at(&g->j, j_get(&g->j, g->root, "bufferViews"), view), buf;
    size_t off, n;
    if (v < 0) { gl_fail(g, "a missing buffer view", NULL); return NULL; }
    buf = j_int(&g->j, v, "buffer", 0);
    off = (size_t)j_int(&g->j, v, "byteOffset", 0);
    n = (size_t)j_int(&g->j, v, "byteLength", 0);
    if (buf < 0 || buf >= g->buf_count || off + n > g->bufs[buf].size) { gl_fail(g, "a buffer view outside its buffer", NULL); return NULL; }
    if (len) *len = n;
    if (stride) *stride = j_int(&g->j, v, "byteStride", 0);
    return g->bufs[buf].data + off;
}

typedef struct gl_acc { const unsigned char *p; int count, comps, ctype, stride; bool norm; } gl_acc;

static int gl_comps(const char *type) {
    if (!type) return 0;
    if (strcmp(type, "SCALAR") == 0) return 1;
    if (strcmp(type, "VEC2") == 0) return 2;
    if (strcmp(type, "VEC3") == 0) return 3;
    if (strcmp(type, "VEC4") == 0) return 4;
    if (strcmp(type, "MAT4") == 0) return 16;
    return 0;
}
static int gl_csize(int ctype) { return ctype == 5120 || ctype == 5121 ? 1 : ctype == 5122 || ctype == 5123 ? 2 : 4; }

static bool gl_accessor(gl_ctx *g, int index, gl_acc *a) {
    int acc = j_at(&g->j, j_get(&g->j, g->root, "accessors"), index), view;
    size_t len = 0, need;
    int stride = 0;
    memset(a, 0, sizeof(*a));
    if (acc < 0) { gl_fail(g, "a missing accessor", NULL); return false; }
    if (j_get(&g->j, acc, "sparse") >= 0) { gl_fail(g, "sparse accessors aren't read", NULL); return false; }
    a->count = j_int(&g->j, acc, "count", 0);
    a->comps = gl_comps(j_str(&g->j, j_get(&g->j, acc, "type")));
    a->ctype = j_int(&g->j, acc, "componentType", 5126);
    a->norm = j_num(&g->j, j_get(&g->j, acc, "normalized"), 0) != 0;
    if (!a->comps || a->count < 0) { gl_fail(g, "an accessor of an unknown type", NULL); return false; }
    view = j_int(&g->j, acc, "bufferView", -1);
    if (view < 0) { gl_fail(g, "an accessor without data", NULL); return false; }
    a->p = gl_view(g, view, &len, &stride);
    if (!a->p) return false;
    a->p += j_int(&g->j, acc, "byteOffset", 0);
    a->stride = stride ? stride : a->comps * gl_csize(a->ctype);
    need = (size_t)j_int(&g->j, acc, "byteOffset", 0) + (a->count ? (size_t)(a->count - 1) * (size_t)a->stride + (size_t)(a->comps * gl_csize(a->ctype)) : 0);
    if (need > len) { gl_fail(g, "an accessor reaches past its view", NULL); return false; }
    return true;
}

static float gl_get(const gl_acc *a, int i, int c) {
    const unsigned char *p = a->p + (size_t)i * (size_t)a->stride + (size_t)c * (size_t)gl_csize(a->ctype);
    switch (a->ctype) {
    case 5120: { int8_t v = (int8_t)p[0]; return a->norm ? BR_MAX((float)v / 127.0f, -1.0f) : (float)v; }
    case 5121: return a->norm ? (float)p[0] / 255.0f : (float)p[0];
    case 5122: { int16_t v = (int16_t)(p[0] | (p[1] << 8)); return a->norm ? BR_MAX((float)v / 32767.0f, -1.0f) : (float)v; }
    case 5123: { uint16_t v = (uint16_t)(p[0] | (p[1] << 8)); return a->norm ? (float)v / 65535.0f : (float)v; }
    case 5125: return (float)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    default: { float f; memcpy(&f, p, 4); return f; }
    }
}
static uint32_t gl_geti(const gl_acc *a, int i) {
    const unsigned char *p = a->p + (size_t)i * (size_t)a->stride;
    if (a->ctype == 5121) return p[0];
    if (a->ctype == 5123) return (uint32_t)(p[0] | (p[1] << 8));
    if (a->ctype == 5125) return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return (uint32_t)gl_get(a, i, 0);
}

static void gl_images(gl_ctx *g, br_model *m) {
    int arr = j_get(&g->j, g->root, "images"), i, n = j_len(&g->j, arr);
    if (!n) return;
    m->images = (br_image *)br_calloc((size_t)n, sizeof(br_image));
    if (!m->images) return;
    m->image_count = n;
    for (i = 0; i < n; ++i) {
        int im = j_at(&g->j, arr, i), view = j_int(&g->j, im, "bufferView", -1);
        const char *uri = j_str(&g->j, j_get(&g->j, im, "uri"));
        bool ok = false;
        if (view >= 0) {
            size_t len = 0;
            const unsigned char *p = gl_view(g, view, &len, NULL);
            ok = p && br_image_load_png_mem(&m->images[i], p, len);
        } else if (uri && strncmp(uri, "data:", 5) == 0) {
            size_t len = 0;
            unsigned char *p = gl_uri(g, uri, &len);
            ok = p && br_image_load_png_mem(&m->images[i], p, len);
            br_free(p);
        } else if (uri) {
            char full[BR_PATH_MAX * 2];
            snprintf(full, sizeof(full), "%s%s%s", g->dir, g->dir[0] ? "/" : "", uri);
            ok = !strstr(uri, "..") && br_image_load(&m->images[i], full);
        }
        g->failed = false;                                       /* (a missing picture isn't the end of the model) */
        if (!ok) BR_WARN("model: image %d couldn't be read (only PNG, BMP and TGA are)", i);
    }
}

static br_image *gl_material_texture(gl_ctx *g, br_model *m, int material, br_color *color) {
    int mat = j_at(&g->j, j_get(&g->j, g->root, "materials"), material), pbr, f, tex, src;
    *color = br_rgba(255, 255, 255, 255);
    if (mat < 0) return NULL;
    pbr = j_get(&g->j, mat, "pbrMetallicRoughness");
    if ((f = j_get(&g->j, pbr, "baseColorFactor")) >= 0 && j_len(&g->j, f) >= 3) {
        int k;
        uint8_t ch[4] = { 255, 255, 255, 255 };
        for (k = 0; k < 4 && k < j_len(&g->j, f); ++k) {
            double v = j_num(&g->j, j_at(&g->j, f, k), 1);
            if (k < 3) v = v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1 / 2.4) - 0.055;   /* (glTF colours are linear) */
            ch[k] = (uint8_t)(v < 0 ? 0 : v > 1 ? 255 : v * 255 + 0.5);
        }
        *color = br_rgba(ch[0], ch[1], ch[2], ch[3]);
    }
    tex = j_int(&g->j, j_get(&g->j, pbr, "baseColorTexture"), "index", -1);
    if (tex < 0) return NULL;
    src = j_int(&g->j, j_at(&g->j, j_get(&g->j, g->root, "textures"), tex), "source", -1);
    if (src < 0 || src >= m->image_count || !m->images[src].pixels) return NULL;
    return &m->images[src];
}

static bool gl_meshes(gl_ctx *g, br_model *m) {
    int nodes = j_get(&g->j, g->root, "nodes"), meshes = j_get(&g->j, g->root, "meshes"), ni, total = 0;
    for (ni = 0; ni < m->node_count; ++ni) {                      /* how many parts */
        int mi = j_int(&g->j, j_at(&g->j, nodes, ni), "mesh", -1);
        if (mi >= 0) total += j_len(&g->j, j_get(&g->j, j_at(&g->j, meshes, mi), "primitives"));
    }
    if (!total) { gl_fail(g, "there are no meshes in it", NULL); return false; }
    m->parts = (br_model_part *)br_calloc((size_t)total, sizeof(br_model_part));
    if (!m->parts) return false;
    for (ni = 0; ni < m->node_count && !g->failed; ++ni) {
        int node = j_at(&g->j, nodes, ni), mi = j_int(&g->j, node, "mesh", -1), skin = j_int(&g->j, node, "skin", -1);
        int prims = j_get(&g->j, j_at(&g->j, meshes, mi), "primitives"), pi;
        if (mi < 0) continue;
        for (pi = 0; pi < j_len(&g->j, prims) && !g->failed; ++pi) {
            int prim = j_at(&g->j, prims, pi), attrs = j_get(&g->j, prim, "attributes"), mode = j_int(&g->j, prim, "mode", 4);
            int ia = j_int(&g->j, attrs, "POSITION", -1), in = j_int(&g->j, attrs, "NORMAL", -1), it = j_int(&g->j, attrs, "TEXCOORD_0", -1);
            int ij = j_int(&g->j, attrs, "JOINTS_0", -1), iw = j_int(&g->j, attrs, "WEIGHTS_0", -1), ii = j_int(&g->j, prim, "indices", -1);
            gl_acc pos, nor, uv, jo, we, idx;
            br_model_part *part = &m->parts[m->part_count];
            uint32_t v, nidx;
            if (mode != 4) { BR_WARN("model: a primitive that isn't triangles was left out"); continue; }
            if (ia < 0 || !gl_accessor(g, ia, &pos) || pos.comps != 3) { gl_fail(g, "a mesh without positions", NULL); break; }
            if (in >= 0 && !gl_accessor(g, in, &nor)) break;
            if (it >= 0 && !gl_accessor(g, it, &uv)) break;
            if (ii >= 0 && !gl_accessor(g, ii, &idx)) break;
            nidx = ii >= 0 ? (uint32_t)idx.count : (uint32_t)pos.count;
            if (nidx % 3) { gl_fail(g, "a triangle list whose length isn't a multiple of three", NULL); break; }
            if (!br_mesh_init(&part->mesh, (uint32_t)pos.count, nidx)) { gl_fail(g, "out of memory", NULL); break; }
            for (v = 0; v < (uint32_t)pos.count; ++v) {
                br_vertex3d *x = &part->mesh.verts[v];
                x->pos = br_v3(gl_get(&pos, (int)v, 0), gl_get(&pos, (int)v, 1), gl_get(&pos, (int)v, 2));
                if (in >= 0 && (int)v < nor.count) x->normal = br_v3(gl_get(&nor, (int)v, 0), gl_get(&nor, (int)v, 1), gl_get(&nor, (int)v, 2));
                if (it >= 0 && (int)v < uv.count) x->uv = br_v2(gl_get(&uv, (int)v, 0), 1.0f - gl_get(&uv, (int)v, 1));   /* (glTF's v goes down) */
            }
            for (v = 0; v < nidx; ++v) {
                uint32_t k = ii >= 0 ? gl_geti(&idx, (int)v) : v;
                if (k >= (uint32_t)pos.count) { gl_fail(g, "an index past the vertices", NULL); break; }
                part->mesh.indices[v] = k;
            }
            if (in < 0) {                                           /* no normals: each face's own */
                for (v = 0; v + 2 < nidx; v += 3) {
                    br_vertex3d *a = &part->mesh.verts[part->mesh.indices[v]], *b = &part->mesh.verts[part->mesh.indices[v + 1]], *c = &part->mesh.verts[part->mesh.indices[v + 2]];
                    br_vec3 n = br_v3_norm(br_v3_cross(br_v3_sub(b->pos, a->pos), br_v3_sub(c->pos, a->pos)));
                    a->normal = b->normal = c->normal = n;
                }
            }
            part->node = ni;
            part->skin = -1;
            if (skin >= 0 && ij >= 0 && iw >= 0 && gl_accessor(g, ij, &jo) && gl_accessor(g, iw, &we)) {
                part->joints = (uint8_t (*)[4])br_calloc((size_t)pos.count, 4);
                part->weights = (float (*)[4])br_calloc((size_t)pos.count, sizeof(float) * 4);
                if (!part->joints || !part->weights) { gl_fail(g, "out of memory", NULL); break; }
                for (v = 0; v < (uint32_t)pos.count && (int)v < jo.count && (int)v < we.count; ++v) {
                    int k;
                    float sum = 0;
                    for (k = 0; k < 4; ++k) {
                        part->joints[v][k] = (uint8_t)gl_get(&jo, (int)v, k);
                        part->weights[v][k] = gl_get(&we, (int)v, k);
                        sum += part->weights[v][k];
                    }
                    if (sum > 1e-6f) for (k = 0; k < 4; ++k) part->weights[v][k] /= sum;
                    else part->weights[v][0] = 1;
                }
                part->skin = skin;
            }
            part->texture = gl_material_texture(g, m, j_int(&g->j, prim, "material", -1), &part->color);
            br_mesh_bounds(&part->mesh);
            m->part_count++;
        }
    }
    return !g->failed;
}

static bool gl_nodes(gl_ctx *g, br_model *m) {
    int arr = j_get(&g->j, g->root, "nodes"), n = j_len(&g->j, arr), i, k, done = 0;
    bool *placed;
    m->nodes = (br_model_node *)br_calloc((size_t)(n ? n : 1), sizeof(br_model_node));
    m->order = (int *)br_calloc((size_t)(n ? n : 1), sizeof(int));
    if (!m->nodes || !m->order) return false;
    m->node_count = n;
    for (i = 0; i < n; ++i) { m->nodes[i].parent = -1; m->nodes[i].s = br_v3(1, 1, 1); m->nodes[i].r = br_quat_identity(); }
    for (i = 0; i < n; ++i) {
        int node = j_at(&g->j, arr, i), kids = j_get(&g->j, node, "children"), f;
        const char *name = j_str(&g->j, j_get(&g->j, node, "name"));
        br_model_node *nd = &m->nodes[i];
        snprintf(nd->name, sizeof(nd->name), "%s", name ? name : "");
        if ((f = j_get(&g->j, node, "translation")) >= 0) nd->t = br_v3((float)j_num(&g->j, j_at(&g->j, f, 0), 0), (float)j_num(&g->j, j_at(&g->j, f, 1), 0), (float)j_num(&g->j, j_at(&g->j, f, 2), 0));
        if ((f = j_get(&g->j, node, "scale")) >= 0) nd->s = br_v3((float)j_num(&g->j, j_at(&g->j, f, 0), 1), (float)j_num(&g->j, j_at(&g->j, f, 1), 1), (float)j_num(&g->j, j_at(&g->j, f, 2), 1));
        if ((f = j_get(&g->j, node, "rotation")) >= 0) {
            br_quat q;
            q.x = (float)j_num(&g->j, j_at(&g->j, f, 0), 0); q.y = (float)j_num(&g->j, j_at(&g->j, f, 1), 0);
            q.z = (float)j_num(&g->j, j_at(&g->j, f, 2), 0); q.w = (float)j_num(&g->j, j_at(&g->j, f, 3), 1);
            nd->r = br_quat_norm(q);
        }
        if ((f = j_get(&g->j, node, "matrix")) >= 0 && j_len(&g->j, f) == 16) {
            br_mat4 mm;
            for (k = 0; k < 16; ++k) mm.m[k] = (float)j_num(&g->j, j_at(&g->j, f, k), 0);   /* (column-major, as ours) */
            br_m4_decompose(mm, &nd->t, &nd->r, &nd->s);
        }
        for (k = 0; k < j_len(&g->j, kids); ++k) {
            int c = (int)j_num(&g->j, j_at(&g->j, kids, k), -1);
            if (c >= 0 && c < n && c != i) m->nodes[c].parent = i;
        }
    }
    placed = (bool *)br_calloc((size_t)(n ? n : 1), sizeof(bool));      /* parents before children */
    if (!placed) return false;
    while (done < n) {
        int before = done;
        for (i = 0; i < n; ++i)
            if (!placed[i] && (m->nodes[i].parent < 0 || placed[m->nodes[i].parent])) { placed[i] = true; m->order[done++] = i; }
        if (done == before) { br_free(placed); gl_fail(g, "the nodes loop round on themselves", NULL); return false; }
    }
    br_free(placed);
    return true;
}

static bool gl_skins(gl_ctx *g, br_model *m) {
    int arr = j_get(&g->j, g->root, "skins"), n = j_len(&g->j, arr), i, k;
    if (!n) return true;
    m->skins = (br_model_skin *)br_calloc((size_t)n, sizeof(br_model_skin));
    if (!m->skins) return false;
    m->skin_count = n;
    for (i = 0; i < n; ++i) {
        int skin = j_at(&g->j, arr, i), joints = j_get(&g->j, skin, "joints"), ib = j_int(&g->j, skin, "inverseBindMatrices", -1);
        br_model_skin *s = &m->skins[i];
        gl_acc acc;
        s->joint_count = j_len(&g->j, joints);
        if (s->joint_count > 256) { gl_fail(g, "a skin with more than 256 joints", NULL); return false; }
        s->joints = (int *)br_calloc((size_t)(s->joint_count ? s->joint_count : 1), sizeof(int));
        s->inverse_bind = (br_mat4 *)br_calloc((size_t)(s->joint_count ? s->joint_count : 1), sizeof(br_mat4));
        if (!s->joints || !s->inverse_bind) return false;
        for (k = 0; k < s->joint_count; ++k) {
            s->joints[k] = (int)j_num(&g->j, j_at(&g->j, joints, k), 0);
            if (s->joints[k] < 0 || s->joints[k] >= m->node_count) { gl_fail(g, "a skin joint that isn't a node", NULL); return false; }
            s->inverse_bind[k] = br_m4_identity();
        }
        if (ib >= 0 && gl_accessor(g, ib, &acc) && acc.comps == 16)
            for (k = 0; k < s->joint_count && k < acc.count; ++k) {
                int c;
                for (c = 0; c < 16; ++c) s->inverse_bind[k].m[c] = gl_get(&acc, k, c);
            }
    }
    return !g->failed;
}

static bool gl_animations(gl_ctx *g, br_model *m) {
    int arr = j_get(&g->j, g->root, "animations"), n = j_len(&g->j, arr), i, k;
    if (!n) return true;
    m->clips = (br_model_clip *)br_calloc((size_t)n, sizeof(br_model_clip));
    if (!m->clips) return false;
    m->clip_count = n;
    for (i = 0; i < n; ++i) {
        int an = j_at(&g->j, arr, i), chans = j_get(&g->j, an, "channels"), samps = j_get(&g->j, an, "samplers");
        const char *name = j_str(&g->j, j_get(&g->j, an, "name"));
        br_model_clip *c = &m->clips[i];
        snprintf(c->name, sizeof(c->name), "%s", name && name[0] ? name : "");
        if (!c->name[0]) snprintf(c->name, sizeof(c->name), "clip%d", i);
        c->channels = (br_model_channel *)br_calloc((size_t)(j_len(&g->j, chans) ? j_len(&g->j, chans) : 1), sizeof(br_model_channel));
        if (!c->channels) return false;
        for (k = 0; k < j_len(&g->j, chans); ++k) {
            int ch = j_at(&g->j, chans, k), target = j_get(&g->j, ch, "target"), sm = j_at(&g->j, samps, j_int(&g->j, ch, "sampler", -1));
            const char *path = j_str(&g->j, j_get(&g->j, target, "path")), *interp = j_str(&g->j, j_get(&g->j, sm, "interpolation"));
            br_model_channel *out = &c->channels[c->channel_count];
            gl_acc tin, vout;
            int comps, key, node = j_int(&g->j, target, "node", -1);
            bool cubic = interp && strcmp(interp, "CUBICSPLINE") == 0;
            if (!path || node < 0 || node >= m->node_count || sm < 0) continue;
            if (strcmp(path, "translation") == 0) out->path = BR_MODEL_T;
            else if (strcmp(path, "rotation") == 0) out->path = BR_MODEL_R;
            else if (strcmp(path, "scale") == 0) out->path = BR_MODEL_S;
            else continue;                                       /* (morph weights: not read) */
            if (!gl_accessor(g, j_int(&g->j, sm, "input", -1), &tin) || !gl_accessor(g, j_int(&g->j, sm, "output", -1), &vout)) return false;
            comps = out->path == BR_MODEL_R ? 4 : 3;
            if (vout.comps != comps || vout.count < tin.count * (cubic ? 3 : 1)) { gl_fail(g, "an animation's keys don't match", NULL); return false; }
            out->node = node;
            out->step = interp && strcmp(interp, "STEP") == 0;
            out->count = tin.count;
            out->times = (float *)br_alloc(sizeof(float) * (size_t)(tin.count ? tin.count : 1));
            out->values = (float *)br_alloc(sizeof(float) * (size_t)comps * (size_t)(tin.count ? tin.count : 1));
            if (!out->times || !out->values) return false;
            for (key = 0; key < tin.count; ++key) {
                int cc, from = cubic ? key * 3 + 1 : key;          /* (cubic splines: the values between the tangents) */
                out->times[key] = gl_get(&tin, key, 0);
                for (cc = 0; cc < comps; ++cc) out->values[key * comps + cc] = gl_get(&vout, from, cc);
                if (out->times[key] > c->duration) c->duration = out->times[key];
            }
            c->channel_count++;
        }
    }
    return true;
}

static void gl_merge(br_model *m) {                               /* every part at rest, in the model's space */
    br_model_pose pose;
    br_mat4 *nodes;
    uint32_t verts = 0, idx = 0, v;
    int i;
    for (i = 0; i < m->part_count; ++i) { verts += m->parts[i].mesh.vert_count; idx += m->parts[i].mesh.index_count; }
    nodes = (br_mat4 *)br_alloc(sizeof(br_mat4) * (size_t)(m->node_count ? m->node_count : 1));
    if (!nodes || !br_model_pose_init(&pose, m) || !br_mesh_init(&m->merged, verts, idx)) { br_free(nodes); br_model_pose_destroy(&pose); return; }
    br_model_node_matrices(m, &pose, nodes);
    verts = idx = 0;
    for (i = 0; i < m->part_count; ++i) {
        br_mesh posed;
        memset(&posed, 0, sizeof(posed));
        if (br_model_part_posed(m, i, nodes, &posed)) {
            memcpy(m->merged.verts + verts, posed.verts, sizeof(br_vertex3d) * posed.vert_count);
            for (v = 0; v < posed.index_count; ++v) m->merged.indices[idx + v] = posed.indices[v] + verts;
            verts += posed.vert_count;
            idx += posed.index_count;
        }
        br_mesh_destroy(&posed);
    }
    m->merged.vert_count = verts;
    m->merged.index_count = idx;
    br_mesh_bounds(&m->merged);
    br_free(nodes);
    br_model_pose_destroy(&pose);
}

bool br_model_is_file(const char *path) { return path && (br_path_has_ext(path, ".gltf") || br_path_has_ext(path, ".glb")); }

void br_model_destroy(br_model *m) {
    int i, k;
    for (i = 0; i < m->part_count; ++i) {
        br_mesh_destroy(&m->parts[i].mesh);
        br_free(m->parts[i].joints);
        br_free(m->parts[i].weights);
    }
    br_free(m->parts);
    br_free(m->nodes);
    br_free(m->order);
    for (i = 0; i < m->skin_count; ++i) { br_free(m->skins[i].joints); br_free(m->skins[i].inverse_bind); }
    br_free(m->skins);
    for (i = 0; i < m->clip_count; ++i) {
        for (k = 0; k < m->clips[i].channel_count; ++k) { br_free(m->clips[i].channels[k].times); br_free(m->clips[i].channels[k].values); }
        br_free(m->clips[i].channels);
    }
    br_free(m->clips);
    for (i = 0; i < m->image_count; ++i) br_image_destroy(&m->images[i]);
    br_free(m->images);
    br_mesh_destroy(&m->merged);
    memset(m, 0, sizeof(*m));
}

bool br_model_load_mem(br_model *m, const void *data, size_t size, const char *dir, char *err, size_t err_size) {
    gl_ctx g;
    const unsigned char *bytes = (const unsigned char *)data;
    const char *json = (const char *)data;
    size_t json_size = size;
    int i;
    bool ok;
    memset(m, 0, sizeof(*m));
    memset(&g, 0, sizeof(g));
    g.err = err;
    g.err_size = err_size;
    if (err && err_size) err[0] = 0;
    snprintf(g.dir, sizeof(g.dir), "%s", dir ? dir : "");
    if (size >= 20 && memcmp(bytes, "glTF", 4) == 0) {          /* .glb: a header, the JSON chunk, the binary chunk */
        uint32_t jlen = (uint32_t)bytes[12] | ((uint32_t)bytes[13] << 8) | ((uint32_t)bytes[14] << 16) | ((uint32_t)bytes[15] << 24);
        if (memcmp(bytes + 16, "JSON", 4) != 0 || 20 + (size_t)jlen > size) { gl_fail(&g, "a damaged .glb", NULL); return false; }
        json = (const char *)bytes + 20;
        json_size = jlen;
        if (20 + (size_t)jlen + 8 <= size && memcmp(bytes + 20 + jlen + 4, "BIN", 3) == 0) {
            const unsigned char *b = bytes + 20 + jlen;
            uint32_t blen = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
            if (20 + (size_t)jlen + 8 + blen > size) { gl_fail(&g, "a damaged .glb", NULL); return false; }
            g.glb_bin = b + 8;
            g.glb_bin_size = blen;
        }
    }
    g.j.p = json;
    g.j.end = json + json_size;
    g.root = j_value(&g.j, 0);
    if (g.j.bad || g.root < 0 || g.j.n[g.root].type != J_OBJ) { j_free(&g.j); gl_fail(&g, "it isn't glTF (or its JSON is damaged)", NULL); return false; }
    {
        const char *version = j_str(&g.j, j_get(&g.j, j_get(&g.j, g.root, "asset"), "version"));
        if (!version || version[0] != '2') { gl_fail(&g, "only glTF 2.0 is read (this is %s)", version ? version : "?"); j_free(&g.j); return false; }
    }
    ok = gl_buffers(&g) && gl_nodes(&g, m);
    if (ok) gl_images(&g, m);
    ok = ok && gl_meshes(&g, m) && gl_skins(&g, m) && gl_animations(&g, m);
    for (i = 0; i < g.buf_count; ++i) if (g.bufs[i].owned) br_free(g.bufs[i].data);
    br_free(g.bufs);
    j_free(&g.j);
    if (!ok) { if (!g.failed && err && err_size) snprintf(err, err_size, "out of memory"); br_model_destroy(m); return false; }
    for (i = 0; i < m->part_count; ++i)                          /* (a skin's joints must fit its list) */
        if (m->parts[i].skin >= m->skin_count) m->parts[i].skin = -1;
    gl_merge(m);
    return true;
}

bool br_model_load(br_model *m, const char *path, char *err, size_t err_size) {
    size_t size = 0;
    char dir[BR_PATH_MAX];
    void *data = br_file_read_all(path, &size);
    bool ok;
    if (!data) { if (err && err_size) snprintf(err, err_size, "can't read %s", path); memset(m, 0, sizeof(*m)); return false; }
    br_path_parent(dir, sizeof(dir), path);
    ok = br_model_load_mem(m, data, size, dir, err, err_size);
    br_free(data);
    return ok;
}

int br_model_find_clip(const br_model *m, const char *name) {
    int i;
    if (!name) return -1;
    for (i = 0; i < m->clip_count; ++i) if (strcmp(m->clips[i].name, name) == 0) return i;
    return -1;
}

/* ---- poses ------------------------------------------------------------------------------ */

bool br_model_pose_init(br_model_pose *p, const br_model *m) {
    int n = m->node_count ? m->node_count : 1;
    memset(p, 0, sizeof(*p));
    p->t = (br_vec3 *)br_alloc(sizeof(br_vec3) * (size_t)n);
    p->s = (br_vec3 *)br_alloc(sizeof(br_vec3) * (size_t)n);
    p->r = (br_quat *)br_alloc(sizeof(br_quat) * (size_t)n);
    if (!p->t || !p->s || !p->r) { br_model_pose_destroy(p); return false; }
    p->count = m->node_count;
    br_model_pose_rest(p, m);
    return true;
}

void br_model_pose_destroy(br_model_pose *p) {
    br_free(p->t); br_free(p->s); br_free(p->r);
    memset(p, 0, sizeof(*p));
}

void br_model_pose_rest(br_model_pose *p, const br_model *m) {
    int i;
    for (i = 0; i < p->count && i < m->node_count; ++i) { p->t[i] = m->nodes[i].t; p->r[i] = m->nodes[i].r; p->s[i] = m->nodes[i].s; }
}

void br_model_pose_sample(br_model_pose *p, const br_model *m, int clip, float time, bool loop) {
    const br_model_clip *c;
    int k;
    br_model_pose_rest(p, m);
    if (clip < 0 || clip >= m->clip_count) return;
    c = &m->clips[clip];
    if (c->duration > 0) {
        if (loop) { time = fmodf(time, c->duration); if (time < 0) time += c->duration; }
        else if (time > c->duration) time = c->duration;
    }
    for (k = 0; k < c->channel_count; ++k) {
        const br_model_channel *ch = &c->channels[k];
        int comps = ch->path == BR_MODEL_R ? 4 : 3, a = 0, b, lo, hi;
        float t = 0, v[4];
        if (!ch->count || ch->node >= p->count) continue;
        if (time <= ch->times[0]) { a = b = 0; }
        else if (time >= ch->times[ch->count - 1]) { a = b = ch->count - 1; }
        else {
            lo = 0; hi = ch->count - 1;                           /* the keys either side */
            while (hi - lo > 1) { int mid = (lo + hi) / 2; if (ch->times[mid] <= time) lo = mid; else hi = mid; }
            a = lo; b = hi;
            if (!ch->step && ch->times[b] > ch->times[a]) t = (time - ch->times[a]) / (ch->times[b] - ch->times[a]);
        }
        if (ch->path == BR_MODEL_R) {
            br_quat qa, qb;
            qa.x = ch->values[a * 4]; qa.y = ch->values[a * 4 + 1]; qa.z = ch->values[a * 4 + 2]; qa.w = ch->values[a * 4 + 3];
            qb.x = ch->values[b * 4]; qb.y = ch->values[b * 4 + 1]; qb.z = ch->values[b * 4 + 2]; qb.w = ch->values[b * 4 + 3];
            p->r[ch->node] = br_quat_norm(t > 0 ? br_quat_slerp(qa, qb, t) : qa);
            continue;
        }
        for (lo = 0; lo < comps; ++lo) v[lo] = ch->values[a * comps + lo] + (ch->values[b * comps + lo] - ch->values[a * comps + lo]) * t;
        if (ch->path == BR_MODEL_T) p->t[ch->node] = br_v3(v[0], v[1], v[2]);
        else p->s[ch->node] = br_v3(v[0], v[1], v[2]);
    }
}

void br_model_pose_blend(br_model_pose *a, const br_model_pose *b, float t) {
    int i;
    if (t <= 0) return;
    if (t > 1) t = 1;
    for (i = 0; i < a->count && i < b->count; ++i) {
        a->t[i] = br_v3_lerp(a->t[i], b->t[i], t);
        a->s[i] = br_v3_lerp(a->s[i], b->s[i], t);
        a->r[i] = br_quat_norm(br_quat_slerp(a->r[i], b->r[i], t));
    }
}

void br_model_node_matrices(const br_model *m, const br_model_pose *p, br_mat4 *out) {
    int k;
    for (k = 0; k < m->node_count; ++k) {
        int i = m->order[k];
        br_mat4 local = br_m4_trs(p->t[i], p->r[i], p->s[i]);
        out[i] = m->nodes[i].parent >= 0 ? br_m4_mul(out[m->nodes[i].parent], local) : local;
    }
}

bool br_model_part_posed(const br_model *m, int pi, const br_mat4 *nodes, br_mesh *out) {
    const br_model_part *part;
    uint32_t v;
    if (pi < 0 || pi >= m->part_count) return false;
    part = &m->parts[pi];
    if (out->vert_count != part->mesh.vert_count || out->index_count != part->mesh.index_count || !out->verts) {
        br_mesh_destroy(out);
        if (!br_mesh_init(out, part->mesh.vert_count, part->mesh.index_count)) return false;
        if (part->mesh.index_count) memcpy(out->indices, part->mesh.indices, sizeof(uint32_t) * part->mesh.index_count);
    }
    if (part->skin >= 0 && part->joints) {                        /* skinned: each vertex by its joints' matrices, weighted */
        const br_model_skin *s = &m->skins[part->skin];
        br_mat4 joint[256];
        int j;
        for (j = 0; j < s->joint_count; ++j) joint[j] = br_m4_mul(nodes[s->joints[j]], s->inverse_bind[j]);
        for (v = 0; v < part->mesh.vert_count; ++v) {
            const br_vertex3d *src = &part->mesh.verts[v];
            br_vec3 p = br_v3(0, 0, 0), n = br_v3(0, 0, 0);
            int k;
            for (k = 0; k < 4; ++k) {
                float w = part->weights[v][k];
                int ji = part->joints[v][k];
                if (w <= 0 || ji >= s->joint_count) continue;
                p = br_v3_add(p, br_v3_scale(br_m4_mul_point(joint[ji], src->pos), w));
                n = br_v3_add(n, br_v3_scale(br_m4_mul_dir(joint[ji], src->normal), w));
            }
            out->verts[v].pos = p;
            out->verts[v].normal = br_v3_norm(n);
            out->verts[v].uv = src->uv;
        }
    } else {                                                     /* rigid: its node's matrix */
        br_mat4 nm = part->node >= 0 ? nodes[part->node] : br_m4_identity(), nn = br_m4_transpose(br_m4_inverse(nm));
        for (v = 0; v < part->mesh.vert_count; ++v) {
            out->verts[v].pos = br_m4_mul_point(nm, part->mesh.verts[v].pos);
            out->verts[v].normal = br_v3_norm(br_m4_mul_dir(nn, part->mesh.verts[v].normal));
            out->verts[v].uv = part->mesh.verts[v].uv;
        }
    }
    br_mesh_bounds(out);
    return true;
}
