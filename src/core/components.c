/*
 * Bronto Engine - built-in components (see components.h).
 */
#include "bronto/components.h"
#include "bronto/assets.h"
#include "bronto/engine.h"
#include "bronto/memory.h"
#include "bronto/physics.h"
#include "bronto/hierarchy.h"
#include "bronto/script.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- descriptions -------------------------------------------------------- */

static const br_field k_name_fields[] = { BR_FIELD(br_name, text, BR_FIELD_STRING, 0, 0) };
static const br_name k_name_default = { "Entity" };

static const br_field k_transform_fields[] = {
    BR_FIELD(br_transform, position, BR_FIELD_VEC2, 0, 0),
    BR_FIELD(br_transform, rotation, BR_FIELD_FLOAT, -360, 360),
    BR_FIELD(br_transform, scale, BR_FIELD_VEC2, 0, 0),
};
static const br_transform k_transform_default = { { 0, 0 }, 0, { 1, 1 } };

static const char *const k_shape_kinds[] = { "rect", "ellipse", "triangle" };
static const br_field k_shape_fields[] = {
    BR_FIELD_ENUM_OF(br_shape, kind, k_shape_kinds),
    BR_FIELD(br_shape, size, BR_FIELD_VEC2, 0, 4096),
    BR_FIELD(br_shape, color, BR_FIELD_COLOR, 0, 0),
    BR_FIELD(br_shape, outline, BR_FIELD_COLOR, 0, 0),
    BR_FIELD(br_shape, layer, BR_FIELD_INT, -100, 100),
};
static const br_shape k_shape_default = { BR_SHAPE_RECT, { 16, 16 }, { 74, 163, 255, 255 }, { 0, 0, 0, 0 }, 0 };

static const br_field k_sprite_fields[] = {
    BR_FIELD(br_sprite, image, BR_FIELD_STRING, 0, 0),
    BR_FIELD(br_sprite, tint, BR_FIELD_COLOR, 0, 0),
    BR_FIELD(br_sprite, layer, BR_FIELD_INT, -100, 100),
};
static const br_sprite k_sprite_default = { "", { 255, 255, 255, 255 }, 0 };

static const br_field k_label_fields[] = {
    BR_FIELD(br_label, text, BR_FIELD_STRING, 0, 0),
    BR_FIELD(br_label, size, BR_FIELD_FLOAT, 0.25f, 16),
    BR_FIELD(br_label, color, BR_FIELD_COLOR, 0, 0),
    BR_FIELD(br_label, layer, BR_FIELD_INT, -100, 100),
};
static const br_label k_label_default = { "Text", 1, { 255, 255, 255, 255 }, 1 };

static const br_field k_motion_fields[] = {
    BR_FIELD(br_motion, velocity, BR_FIELD_VEC2, 0, 0),
    BR_FIELD(br_motion, spin, BR_FIELD_FLOAT, 0, 0),
};
static const br_motion k_motion_default = { { 40, 0 }, 0 };

static const br_field k_bounds_fields[] = {
    BR_FIELD(br_bounds, min, BR_FIELD_VEC2, 0, 0),
    BR_FIELD(br_bounds, max, BR_FIELD_VEC2, 0, 0),
    BR_FIELD(br_bounds, wrap, BR_FIELD_BOOL, 0, 0),
};
static const br_bounds k_bounds_default = { { 0, 0 }, { 320, 180 }, false };

static const char *const k_sound_buses[] = { "sfx", "music", "ui", "ambient" };   /* BR_BUS_* order */
static const br_field k_sound_fields[] = {
    BR_FIELD(br_sound_src, file, BR_FIELD_STRING, 0, 0),
    BR_FIELD(br_sound_src, volume, BR_FIELD_FLOAT, 0, 2),
    BR_FIELD(br_sound_src, loop, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_sound_src, play_on_start, BR_FIELD_BOOL, 0, 0),
    BR_FIELD_ENUM_OF(br_sound_src, bus, k_sound_buses),
};
static const br_sound_src k_sound_default = { "", 1, false, true, BR_BUS_SFX, 0, false };

static const br_field k_tilemap_fields[] = {
    BR_FIELD(br_tilemap, tileset, BR_FIELD_STRING, 0, 0),
    BR_FIELD(br_tilemap, tile_size, BR_FIELD_INT, 1, 256),
    BR_FIELD(br_tilemap, columns, BR_FIELD_INT, 1, 256),
    BR_FIELD(br_tilemap, rows, BR_FIELD_INT, 1, 256),
    BR_FIELD(br_tilemap, legend, BR_FIELD_STRING, 0, 0),
    BR_FIELD(br_tilemap, solid, BR_FIELD_STRING, 0, 0),
    BR_FIELD(br_tilemap, tiles, BR_FIELD_STRING, 0, 0),
    BR_FIELD(br_tilemap, layer, BR_FIELD_INT, -100, 100),
    BR_FIELD(br_tilemap, tint, BR_FIELD_COLOR, 0, 0),
};
static const br_tilemap k_tilemap_default = { "", 16, 16, 11, "", "", "", -10, { 255, 255, 255, 255 } };

static const br_field k_tags_fields[] = { BR_FIELD(br_tags, list, BR_FIELD_STRING, 0, 0) };
static const br_tags k_tags_default = { "" };

#define DESC(name, T, fields, def) { name, sizeof(T), fields, (int)BR_ARRAY_COUNT(fields), def }
static const br_component_desc k_descs[] = {
    DESC("Name", br_name, k_name_fields, &k_name_default),
    DESC("Transform", br_transform, k_transform_fields, &k_transform_default),
    DESC("Shape", br_shape, k_shape_fields, &k_shape_default),
    DESC("Sprite", br_sprite, k_sprite_fields, &k_sprite_default),
    DESC("Label", br_label, k_label_fields, &k_label_default),
    DESC("Motion", br_motion, k_motion_fields, &k_motion_default),
    DESC("Bounds", br_bounds, k_bounds_fields, &k_bounds_default),
    DESC("Sound", br_sound_src, k_sound_fields, &k_sound_default),
    DESC("Tilemap", br_tilemap, k_tilemap_fields, &k_tilemap_default),
    DESC("Tags", br_tags, k_tags_fields, &k_tags_default),
};
static const br_component_desc *const k_desc_list[] = {
    &k_descs[0], &k_descs[1], &k_descs[2], &k_descs[3], &k_descs[4], &k_descs[5], &k_descs[6], &k_descs[7],
    &k_descs[8], &k_descs[9],
};

const br_component_desc *const *br_builtin_descs(int *count) {
    *count = (int)BR_ARRAY_COUNT(k_desc_list);
    return k_desc_list;
}

bool br_builtin_ids_of(const br_world *w, br_builtin_ids *ids) {
    ids->name = br_component_find(w, "Name");
    ids->transform = br_component_find(w, "Transform");
    ids->shape = br_component_find(w, "Shape");
    ids->sprite = br_component_find(w, "Sprite");
    ids->label = br_component_find(w, "Label");
    ids->motion = br_component_find(w, "Motion");
    ids->bounds = br_component_find(w, "Bounds");
    ids->sound = br_component_find(w, "Sound");
    ids->tilemap = br_component_find(w, "Tilemap");
    ids->tags = br_component_find(w, "Tags");
    return ids->tags != BR_COMPONENT_INVALID && ids->transform != BR_COMPONENT_INVALID && ids->shape != BR_COMPONENT_INVALID &&
           ids->sprite != BR_COMPONENT_INVALID && ids->label != BR_COMPONENT_INVALID &&
           ids->motion != BR_COMPONENT_INVALID && ids->bounds != BR_COMPONENT_INVALID &&
           ids->name != BR_COMPONENT_INVALID && ids->sound != BR_COMPONENT_INVALID &&
           ids->tilemap != BR_COMPONENT_INVALID;
}

/* ---- tags ------------------------------------------------------------------ */

static bool tag_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-' || c == '.' || c == ':' || c == '/';
}
static bool tag_sep(char c) { return c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool br_tag_valid(const char *tag) {
    size_t i;
    if (!tag || !tag[0]) return false;
    for (i = 0; tag[i]; ++i) if (!tag_char(tag[i]) || i >= BR_TAG_MAX - 1) return false;
    return true;
}

/* The next tag in a list from *p: its start and length (0 at the end). */
static size_t next_tag(const char **p, const char **start) {
    const char *q = *p;
    size_t n = 0;
    while (*q && tag_sep(*q)) ++q;
    *start = q;
    while (q[n] && !tag_sep(q[n])) ++n;
    *p = q + n;
    return n;
}

static const br_tags *tags_of(const br_world *w, br_entity e) {
    br_component c = br_component_find(w, "Tags");
    return c == BR_COMPONENT_INVALID ? NULL : (const br_tags *)br_get(w, e, c);
}

static bool list_has(const char *list, const char *tag, size_t len) {
    const char *p = list, *s;
    size_t n;
    while ((n = next_tag(&p, &s)) > 0)
        if (n == len && strncmp(s, tag, len) == 0) return true;
    return false;
}

bool br_tag_has(const br_world *w, br_entity e, const char *tag) {
    const br_tags *t = tags_of(w, e);
    return t && tag && tag[0] && list_has(t->list, tag, strlen(tag));
}

int br_tag_count(const br_world *w, br_entity e) {
    const br_tags *t = tags_of(w, e);
    const char *p, *s;
    int count = 0;
    if (!t) return 0;
    for (p = t->list; next_tag(&p, &s) > 0;) ++count;
    return count;
}

bool br_tag_at(const br_world *w, br_entity e, int index, char *out, size_t size) {
    const br_tags *t = tags_of(w, e);
    const char *p, *s;
    size_t n;
    int i = 0;
    if (!t || size == 0) return false;
    for (p = t->list; (n = next_tag(&p, &s)) > 0; ++i) {
        if (i == index) {
            if (n >= size) n = size - 1;
            memcpy(out, s, n);
            out[n] = 0;
            return true;
        }
    }
    return false;
}

bool br_tag_add(br_world *w, br_entity e, const char *tag) {
    br_component c = br_component_find(w, "Tags");
    br_tags *t;
    size_t len, used;
    if (c == BR_COMPONENT_INVALID || !br_entity_alive(w, e) || !br_tag_valid(tag)) return false;
    t = (br_tags *)br_get(w, e, c);
    if (!t) t = (br_tags *)br_add(w, e, c, NULL);
    if (!t) return false;
    len = strlen(tag);
    if (list_has(t->list, tag, len)) return true;
    used = strlen(t->list);
    while (used > 0 && tag_sep(t->list[used - 1])) t->list[--used] = 0;   /* (no trailing ", ") */
    if (used + (used ? 2 : 0) + len >= sizeof(t->list)) return false;
    if (used) { memcpy(t->list + used, ", ", 2); used += 2; }
    memcpy(t->list + used, tag, len + 1);
    return true;
}

bool br_tag_remove(br_world *w, br_entity e, const char *tag) {
    br_tags *t = (br_tags *)tags_of(w, e);
    char out[BR_TAGS_MAX];
    const char *p, *s;
    size_t n, len, used = 0;
    bool had = false;
    if (!t || !tag || !tag[0]) return false;
    len = strlen(tag);
    for (p = t->list; (n = next_tag(&p, &s)) > 0;) {      /* the list again, without it, tidily */
        if (n == len && strncmp(s, tag, len) == 0) { had = true; continue; }
        if (used + 2 + n >= sizeof(out)) break;           /* (can't happen with lists the component holds) */
        if (used) { memcpy(out + used, ", ", 2); used += 2; }
        memcpy(out + used, s, n);
        used += n;
    }
    out[used] = 0;
    if (had) memcpy(t->list, out, used + 1);
    return had;
}

uint32_t br_find_tagged(const br_world *w, const char *const *tags, int tag_count, br_entity *out, uint32_t max) {
    br_component c = br_component_find(w, "Tags");
    const br_component_pool *pool;
    uint32_t i, found = 0;
    int k;
    if (c == BR_COMPONENT_INVALID || tag_count <= 0) return 0;
    pool = &w->pools[c];
    for (i = 0; i < pool->count; ++i) {
        const br_tags *t = (const br_tags *)(pool->data + (size_t)i * pool->stride);
        for (k = 0; k < tag_count; ++k)
            if (!tags[k] || !tags[k][0] || !list_has(t->list, tags[k], strlen(tags[k]))) break;
        if (k < tag_count) continue;
        if (out && found < max) out[found] = pool->entities[i];
        ++found;
    }
    return found;
}

/* ---- motion ---------------------------------------------------------------- */

static void keep_inside(float *pos, float *vel, float lo, float hi, bool wrap) {
    float span = hi - lo;
    if (span <= 0) return;
    if (wrap) {
        if (*pos < lo || *pos >= hi) *pos = lo + fmodf(fmodf(*pos - lo, span) + span, span);
    } else if (*pos < lo) {
        *pos = lo;
        *vel = fabsf(*vel);
    } else if (*pos > hi) {
        *pos = hi;
        *vel = -fabsf(*vel);
    }
}

void br_builtin_step(br_world *w, float dt) {
    br_builtin_ids id;
    br_component with[2], body = br_component_find(w, "RigidBody");
    br_query q;
    if (!br_builtin_ids_of(w, &id)) return;
    with[0] = id.motion;
    with[1] = id.transform;
    q = br_query_make(w, with, 2);
    while (br_query_next(&q)) {
        br_motion *m = BR_QUERY_GET(&q, 0, br_motion);
        br_transform *t = BR_QUERY_GET(&q, 1, br_transform);
        if (body != BR_COMPONENT_INVALID && br_has(w, q.entity, body)) continue;   /* physics moves it */
        const br_bounds *b = (const br_bounds *)br_get(w, q.entity, id.bounds);
        t->position = br_v2_add(t->position, br_v2_scale(m->velocity, dt));
        t->rotation = fmodf(t->rotation + m->spin * dt, 360.0f);
        if (b) {
            keep_inside(&t->position.x, &m->velocity.x, b->min.x, b->max.x, b->wrap);
            keep_inside(&t->position.y, &m->velocity.y, b->min.y, b->max.y, b->wrap);
        }
    }
}

/* ---- sound ------------------------------------------------------------------ */

static br_mixer *world_mixer(const br_world *w) {
    return w->context ? &((br_engine *)w->context)->mixer : NULL;
}

static bool sound_start(br_world *w, br_sound_src *s) {
    br_mixer *m = world_mixer(w);
    const br_sound *snd;
    br_play_params pp;
    s->started = true;
    if (!m || !(snd = br_asset_sound(s->file))) return false;
    if (s->voice) br_voice_stop(m, s->voice);
    pp = br_play_defaults();
    pp.volume = s->volume;
    pp.loop = s->loop;
    pp.bus = s->bus >= 0 && s->bus < BR_AUDIO_BUS_COUNT ? s->bus : BR_BUS_SFX;
    s->voice = br_play(m, snd, &pp);
    return s->voice != 0;
}

bool br_builtin_sound_play(br_world *w, br_entity e) {
    br_builtin_ids id;
    br_sound_src *s;
    if (!br_builtin_ids_of(w, &id) || !(s = (br_sound_src *)br_get(w, e, id.sound))) return false;
    return sound_start(w, s);
}

void br_builtin_sound_stop(br_world *w, br_entity e) {
    br_builtin_ids id;
    br_sound_src *s;
    br_mixer *m = world_mixer(w);
    if (!br_builtin_ids_of(w, &id) || !(s = (br_sound_src *)br_get(w, e, id.sound))) return;
    if (m && s->voice) br_voice_stop(m, s->voice);
    s->voice = 0;
}

static void sound_added(br_world *w, br_entity e, void *data, void *user) {
    br_sound_src *s = (br_sound_src *)data;
    BR_UNUSED(w); BR_UNUSED(e); BR_UNUSED(user);
    s->voice = 0;                 /* a clone or a loaded copy doesn't own a voice */
    s->started = false;
}

static void sound_removed(br_world *w, br_entity e, void *data, void *user) {
    br_sound_src *s = (br_sound_src *)data;
    br_mixer *m = world_mixer(w);
    BR_UNUSED(e); BR_UNUSED(user);
    if (m && s->voice) br_voice_stop(m, s->voice);
    s->voice = 0;
}

static void audio_step(br_world *w) {
    br_builtin_ids id;
    br_component_pool *p;
    uint32_t i;
    if (!w->context || !br_builtin_ids_of(w, &id)) return;
    p = &w->pools[id.sound];
    for (i = 0; i < p->count; ++i) {
        br_sound_src *s = (br_sound_src *)(p->data + (size_t)i * p->stride);
        if (s->play_on_start && !s->started) sound_start(w, s);
    }
}

/* ---- geometry ---------------------------------------------------------------- */

/* Half-size of everything the entity shows, in its local (unscaled) space. */
static br_vec2 local_half_extent(const br_world *w, const br_builtin_ids *id, br_entity e) {
    br_vec2 h = br_v2(0, 0);
    const br_shape *s = (const br_shape *)br_get(w, e, id->shape);
    const br_sprite *sp = (const br_sprite *)br_get(w, e, id->sprite);
    const br_label *l = (const br_label *)br_get(w, e, id->label);
    const br_tilemap *tm = (const br_tilemap *)br_get(w, e, id->tilemap);
    if (tm) {
        h.x = BR_MAX(h.x, (float)(tm->columns * tm->tile_size) / 2);
        h.y = BR_MAX(h.y, (float)(tm->rows * tm->tile_size) / 2);
    }
    if (s) { h.x = BR_MAX(h.x, br_absf(s->size.x) / 2); h.y = BR_MAX(h.y, br_absf(s->size.y) / 2); }
    if (sp) {
        const br_image *img = br_asset_image(sp->image);
        float iw = img ? (float)img->width : 16.0f, ih = img ? (float)img->height : 16.0f;
        h.x = BR_MAX(h.x, iw / 2);
        h.y = BR_MAX(h.y, ih / 2);
    }
    if (l) {
        br_vec2 m = br_measure_text(l->text, l->size);
        h.x = BR_MAX(h.x, m.x / 2);
        h.y = BR_MAX(h.y, m.y / 2);
    }
    if (h.x <= 0 && h.y <= 0) h = br_v2(4, 4);        /* invisible: a small grab handle */
    return h;
}

/* local -> world: scale, rotate (degrees, clockwise on screen), move. */
static br_vec2 to_world(const br_transform *t, br_vec2 p) {
    float a = t->rotation * (BR_PI / 180.0f), c = cosf(a), s = sinf(a);
    br_vec2 q = br_v2(p.x * t->scale.x, p.y * t->scale.y);
    return br_v2(t->position.x + q.x * c - q.y * s, t->position.y + q.x * s + q.y * c);
}

bool br_builtin_box(const br_world *w, br_entity e, br_vec2 corners[4]) {
    br_builtin_ids id;
    br_transform wt;
    const br_transform *t = &wt;
    br_vec2 h;
    if (!br_builtin_ids_of(w, &id) || !br_has(w, e, id.transform)) return false;
    wt = br_world_transform(w, e);                         /* where it is, after its parents */
    h = local_half_extent(w, &id, e);
    corners[0] = to_world(t, br_v2(-h.x, -h.y));
    corners[1] = to_world(t, br_v2(h.x, -h.y));
    corners[2] = to_world(t, br_v2(h.x, h.y));
    corners[3] = to_world(t, br_v2(-h.x, h.y));
    return true;
}

bool br_builtin_hit(const br_world *w, br_entity e, br_vec2 p) {
    br_builtin_ids id;
    br_transform wt;
    const br_transform *t = &wt;
    br_vec2 h, d, l;
    float a, c, s, sx, sy;
    if (!br_builtin_ids_of(w, &id) || !br_has(w, e, id.transform)) return false;
    wt = br_world_transform(w, e);
    h = local_half_extent(w, &id, e);
    a = -t->rotation * (BR_PI / 180.0f);
    c = cosf(a);
    s = sinf(a);
    d = br_v2_sub(p, t->position);
    sx = br_absf(t->scale.x) > 1e-6f ? t->scale.x : 1e-6f;
    sy = br_absf(t->scale.y) > 1e-6f ? t->scale.y : 1e-6f;
    l = br_v2((d.x * c - d.y * s) / sx, (d.x * s + d.y * c) / sy);
    return br_absf(l.x) <= h.x && br_absf(l.y) <= h.y;
}

/* ---- drawing ------------------------------------------------------------------ */

typedef struct draw_item { br_entity e; int layer; int what; uint32_t depth; } draw_item;   /* parents under their children */   /* what: 0 shape, 1 sprite, 2 label, 3 tilemap */

static draw_item *g_items;
static uint32_t g_item_cap;

static int compare_items(const void *a, const void *b) {
    const draw_item *x = (const draw_item *)a, *y = (const draw_item *)b;
    if (x->layer != y->layer) return x->layer < y->layer ? -1 : 1;
    if (x->depth != y->depth) return x->depth < y->depth ? -1 : 1;
    if (x->e != y->e) return BR_ENTITY_INDEX(x->e) < BR_ENTITY_INDEX(y->e) ? -1 : 1;
    return x->what - y->what;
}

static void draw_polygon(br_renderer *r, const br_vec2 *p, int n, br_color fill, br_color outline) {
    int i;
    if (fill.a) for (i = 1; i + 1 < n; ++i) br_fill_triangle(r, p[0], p[i], p[i + 1], fill);
    if (outline.a) for (i = 0; i < n; ++i) br_draw_line(r, p[i], p[(i + 1) % n], outline);
}

static void draw_shape(br_renderer *r, const br_transform *t, const br_shape *s) {
    br_vec2 pts[32];
    float hx = s->size.x / 2, hy = s->size.y / 2;
    int i, n = 0;
    if (s->kind == BR_SHAPE_ELLIPSE) {
        n = 32;
        for (i = 0; i < n; ++i) {
            float a = (float)i * BR_TAU / (float)n;
            pts[i] = to_world(t, br_v2(cosf(a) * hx, sinf(a) * hy));
        }
    } else if (s->kind == BR_SHAPE_TRIANGLE) {
        pts[n++] = to_world(t, br_v2(0, -hy));
        pts[n++] = to_world(t, br_v2(hx, hy));
        pts[n++] = to_world(t, br_v2(-hx, hy));
    } else {
        pts[n++] = to_world(t, br_v2(-hx, -hy));
        pts[n++] = to_world(t, br_v2(hx, -hy));
        pts[n++] = to_world(t, br_v2(hx, hy));
        pts[n++] = to_world(t, br_v2(-hx, hy));
    }
    draw_polygon(r, pts, n, s->color, s->outline);
}

static void draw_sprite(br_renderer *r, const br_transform *t, const br_sprite *s) {
    const br_image *img = br_asset_image(s->image);
    if (img) {
        br_draw_sprite(r, img, NULL, t->position, br_v2((float)img->width / 2, (float)img->height / 2),
                       t->scale, t->rotation * (BR_PI / 180.0f), s->tint);
    } else {                      /* missing image: a magenta box with a cross */
        br_vec2 p[4];
        p[0] = to_world(t, br_v2(-8, -8)); p[1] = to_world(t, br_v2(8, -8));
        p[2] = to_world(t, br_v2(8, 8));   p[3] = to_world(t, br_v2(-8, 8));
        draw_polygon(r, p, 4, br_rgba(255, 0, 255, 120), br_rgb(255, 0, 255));
        br_draw_line(r, p[0], p[2], br_rgb(255, 0, 255));
        br_draw_line(r, p[1], p[3], br_rgb(255, 0, 255));
    }
}

static void draw_tilemap(br_renderer *r, const br_transform *t, const br_tilemap *m) {
    const br_image *img = br_asset_image(m->tileset);
    int ts = m->tile_size, per_row, x, y;
    size_t count;
    float left, top;
    if (!img || ts <= 0 || (per_row = img->width / ts) <= 0) return;
    count = strlen(m->tiles);
    left = -(float)(m->columns * ts) / 2;
    top = -(float)(m->rows * ts) / 2;
    for (y = 0; y < m->rows; ++y) {
        for (x = 0; x < m->columns; ++x) {
            size_t at = (size_t)y * (size_t)m->columns + (size_t)x;
            const char *found;
            int index;
            br_recti src;
            if (at >= count) return;
            if (!m->tiles[at] || !(found = strchr(m->legend, m->tiles[at]))) continue;
            index = (int)(found - m->legend);
            src = br_recti_make((index % per_row) * ts, (index / per_row) * ts, ts, ts);
            if (src.y + ts > img->height) continue;
            br_draw_sprite(r, img, &src, to_world(t, br_v2(left + (float)(x * ts) + (float)ts / 2, top + (float)(y * ts) + (float)ts / 2)),
                           br_v2((float)ts / 2, (float)ts / 2), t->scale, t->rotation * (BR_PI / 180.0f), m->tint);
        }
    }
}

/* The cell under a world point (false: outside the map). */
static bool tilemap_cell(const br_world *w, br_entity e, const br_tilemap *m, br_vec2 p, int *col, int *row) {
    br_transform t = br_world_transform(w, e);
    float a = -t.rotation * (BR_PI / 180.0f), c = cosf(a), s = sinf(a), lx, ly;
    br_vec2 d = br_v2_sub(p, t.position);
    float sx = br_absf(t.scale.x) > 1e-6f ? t.scale.x : 1e-6f, sy = br_absf(t.scale.y) > 1e-6f ? t.scale.y : 1e-6f;
    int cx, cy;
    if (m->tile_size <= 0) return false;
    lx = (d.x * c - d.y * s) / sx + (float)(m->columns * m->tile_size) / 2;
    ly = (d.x * s + d.y * c) / sy + (float)(m->rows * m->tile_size) / 2;
    cx = (int)floorf(lx / (float)m->tile_size);
    cy = (int)floorf(ly / (float)m->tile_size);
    if (col) *col = cx;
    if (row) *row = cy;
    return cx >= 0 && cy >= 0 && cx < m->columns && cy < m->rows;
}

static br_tilemap *tilemap_of(const br_world *w, br_entity e) {
    br_component id = br_component_find(w, "Tilemap");
    return id == BR_COMPONENT_INVALID ? NULL : (br_tilemap *)br_get(w, e, id);
}

char br_tilemap_get(const br_world *w, br_entity map, br_vec2 point, int *col, int *row) {
    const br_tilemap *m = tilemap_of(w, map);
    int cx = -1, cy = -1;
    size_t at;
    bool inside = m && tilemap_cell(w, map, m, point, &cx, &cy);
    if (col) *col = cx;
    if (row) *row = cy;
    if (!inside) return 0;
    at = (size_t)cy * (size_t)m->columns + (size_t)cx;
    return at < strlen(m->tiles) ? m->tiles[at] : 0;
}

bool br_tilemap_set(br_world *w, br_entity map, br_vec2 point, char c) {
    br_tilemap *m = tilemap_of(w, map);
    int cx, cy;
    size_t at, len;
    if (!m || !c || !tilemap_cell(w, map, m, point, &cx, &cy)) return false;
    at = (size_t)cy * (size_t)m->columns + (size_t)cx;
    if (at >= BR_TILEMAP_MAX) return false;
    len = strlen(m->tiles);
    while (len < at) m->tiles[len++] = ' ';               /* a short map grows, empty cells between */
    m->tiles[at] = c;
    if (at == len) m->tiles[at + 1] = 0;
    return true;
}

bool br_tilemap_solid(const br_world *w, br_entity map, br_vec2 point) {
    const br_tilemap *m = tilemap_of(w, map);
    char c = m ? br_tilemap_get(w, map, point, NULL, NULL) : 0;
    return c && strchr(m->solid, c) != NULL;
}

bool br_tilemap_cell_center(const br_world *w, br_entity map, int col, int row, br_vec2 *out) {
    const br_tilemap *m = tilemap_of(w, map);
    br_transform t;
    float ts;
    if (!m) return false;
    t = br_world_transform(w, map);
    ts = (float)m->tile_size;
    *out = to_world(&t, br_v2(-(float)m->columns * ts / 2 + ((float)col + 0.5f) * ts, -(float)m->rows * ts / 2 + ((float)row + 0.5f) * ts));
    return true;
}

static void draw_label(br_renderer *r, const br_transform *t, const br_label *l) {
    float size = l->size * br_absf(t->scale.x);
    br_vec2 m = br_measure_text(l->text, size);
    br_draw_text(r, l->text, t->position.x - m.x / 2, t->position.y - m.y / 2, size, l->color);
}

void br_builtin_draw(const br_world *w, br_renderer *r) {
    br_builtin_ids id;
    uint32_t n = 0, i, k;
    br_component kinds[4];
    if (!br_builtin_ids_of(w, &id)) return;
    kinds[0] = id.shape;
    kinds[1] = id.sprite;
    kinds[2] = id.label;
    kinds[3] = id.tilemap;
    for (k = 0; k < 4; ++k) {                     /* everything visible that has a Transform */
        const br_component_pool *p = &w->pools[kinds[k]];
        for (i = 0; i < p->count; ++i) {
            br_entity e = p->entities[i];
            const void *data = p->data + (size_t)i * p->stride;
            if (!br_has(w, e, id.transform)) continue;
            if (n == g_item_cap) {
                uint32_t cap = g_item_cap ? g_item_cap * 2 : 256;
                draw_item *grown = (draw_item *)br_realloc(g_items, cap * sizeof(draw_item));
                if (!grown) break;
                g_items = grown;
                g_item_cap = cap;
            }
            g_items[n].e = e;
            g_items[n].what = (int)k;
            g_items[n].depth = br_depth(w, e);
            g_items[n].layer = k == 0 ? ((const br_shape *)data)->layer
                             : k == 1 ? ((const br_sprite *)data)->layer
                             : k == 2 ? ((const br_label *)data)->layer : ((const br_tilemap *)data)->layer;
            ++n;
        }
    }
    qsort(g_items, n, sizeof(draw_item), compare_items);
    for (i = 0; i < n; ++i) {
        const draw_item *it = &g_items[i];
        br_transform wt = br_world_transform(w, it->e);   /* children follow their parents */
        const br_transform *t = &wt;
        if (it->what == 0) draw_shape(r, t, (const br_shape *)br_get(w, it->e, id.shape));
        else if (it->what == 1) draw_sprite(r, t, (const br_sprite *)br_get(w, it->e, id.sprite));
        else if (it->what == 2) draw_label(r, t, (const br_label *)br_get(w, it->e, id.label));
        else draw_tilemap(r, t, (const br_tilemap *)br_get(w, it->e, id.tilemap));
    }
}

void br_builtin_release(void) {
    br_free(g_items);
    g_items = NULL;
    g_item_cap = 0;
}

/* ---- systems -------------------------------------------------------------------- */

static void sys_motion(br_world *w, float dt, void *user) {
    BR_UNUSED(user);
    br_builtin_step(w, dt);
}

static void sys_audio(br_world *w, float dt, void *user) {
    BR_UNUSED(dt);
    BR_UNUSED(user);
    audio_step(w);
}

static void sys_draw(br_world *w, float dt, void *user) {
    BR_UNUSED(dt);
    BR_UNUSED(user);
    if (w->context) br_builtin_draw(w, &((br_engine *)w->context)->renderer);
}

void br_builtin_register(br_world *w, br_builtin_ids *ids) {
    int i;
    for (i = 0; i < (int)BR_ARRAY_COUNT(k_descs); ++i) br_component_register_desc(w, &k_descs[i]);
    br_system_add(w, "builtin_motion", BR_PHASE_FIXED_UPDATE, sys_motion, NULL, 0);
    br_system_add(w, "builtin_audio", BR_PHASE_FIXED_UPDATE, sys_audio, NULL, 0);
    br_component_set_hooks(w, br_component_find(w, "Sound"), sound_added, sound_removed, NULL);
    br_hierarchy_register(w);
    br_physics_register(w);
    br_script_register(w);
    br_system_add(w, "builtin_draw", BR_PHASE_RENDER, sys_draw, NULL, 0);
    if (ids) br_builtin_ids_of(w, ids);
}
