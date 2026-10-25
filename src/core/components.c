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
#include <stdio.h>
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

static const char *const k_blend_modes[] = { "alpha", "none", "add", "multiply" };   /* br_blend_mode order */
static const char *const k_shape_kinds[] = { "rect", "ellipse", "triangle" };
static const br_field k_shape_fields[] = {
    BR_FIELD_ENUM_OF(br_shape, kind, k_shape_kinds),
    BR_FIELD(br_shape, size, BR_FIELD_VEC2, 0, 4096),
    BR_FIELD(br_shape, color, BR_FIELD_COLOR, 0, 0),
    BR_FIELD(br_shape, outline, BR_FIELD_COLOR, 0, 0),
    BR_FIELD(br_shape, layer, BR_FIELD_INT, -100, 100),
    BR_FIELD_ENUM_OF(br_shape, blend, k_blend_modes),
};
static const br_shape k_shape_default = { BR_SHAPE_RECT, { 16, 16 }, { 74, 163, 255, 255 }, { 0, 0, 0, 0 }, 0, BR_BLEND_ALPHA };

static const br_field k_sprite_fields[] = {
    BR_FIELD(br_sprite, image, BR_FIELD_STRING, 0, 0),
    BR_FIELD(br_sprite, tint, BR_FIELD_COLOR, 0, 0),
    BR_FIELD(br_sprite, layer, BR_FIELD_INT, -100, 100),
    BR_FIELD(br_sprite, frame_size, BR_FIELD_VEC2, 0, 4096),
    BR_FIELD(br_sprite, frame, BR_FIELD_INT, 0, 4095),
    BR_FIELD(br_sprite, flip_x, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_sprite, flip_y, BR_FIELD_BOOL, 0, 0),
    BR_FIELD_ENUM_OF(br_sprite, blend, k_blend_modes),
};
static const br_sprite k_sprite_default = { "", { 255, 255, 255, 255 }, 0, { 0, 0 }, 0, false, false, BR_BLEND_ALPHA };

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

static const br_field k_animation_fields[] = {
    BR_FIELD(br_animation, frames, BR_FIELD_STRING, 0, 0),
    BR_FIELD(br_animation, fps, BR_FIELD_FLOAT, 0, 120),
    BR_FIELD(br_animation, loop, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_animation, playing, BR_FIELD_BOOL, 0, 0),
};
static const br_animation k_animation_default = { "0-3", 8, true, true, 0, "" };

static const br_field k_camera_fields[] = {
    BR_FIELD(br_camera, current, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_camera, zoom, BR_FIELD_FLOAT, 0.05f, 32),
    BR_FIELD(br_camera, follow, BR_FIELD_ENTITY, 0, 0),
    BR_FIELD(br_camera, follow_offset, BR_FIELD_VEC2, 0, 0),
    BR_FIELD(br_camera, deadzone, BR_FIELD_VEC2, 0, 4096),
    BR_FIELD(br_camera, smoothing, BR_FIELD_FLOAT, 0, 5),
    BR_FIELD(br_camera, limit, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(br_camera, limit_min, BR_FIELD_VEC2, 0, 0),
    BR_FIELD(br_camera, limit_max, BR_FIELD_VEC2, 0, 0),
    BR_FIELD(br_camera, ui_layer, BR_FIELD_INT, -100, 101),
    BR_FIELD(br_camera, pixel_snap, BR_FIELD_BOOL, 0, 0),
};
static const br_camera k_camera_default = {
    true, 1, BR_ENTITY_NULL, { 0, 0 }, { 0, 0 }, 0, false, { 0, 0 }, { 320, 180 }, 50, true, 0, 0, 0, { 0, 0 }, 0, false
};

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
    DESC("Animation", br_animation, k_animation_fields, &k_animation_default),
    DESC("Camera", br_camera, k_camera_fields, &k_camera_default),
};
static const br_component_desc *const k_desc_list[] = {
    &k_descs[0], &k_descs[1], &k_descs[2], &k_descs[3], &k_descs[4], &k_descs[5], &k_descs[6], &k_descs[7],
    &k_descs[8], &k_descs[9], &k_descs[10], &k_descs[11],
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
    ids->animation = br_component_find(w, "Animation");
    ids->camera = br_component_find(w, "Camera");
    return ids->tags != BR_COMPONENT_INVALID && ids->animation != BR_COMPONENT_INVALID && ids->camera != BR_COMPONENT_INVALID && ids->transform != BR_COMPONENT_INVALID && ids->shape != BR_COMPONENT_INVALID &&
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
        if (img && sp->frame_size.x >= 1 && sp->frame_size.y >= 1) { iw = sp->frame_size.x; ih = sp->frame_size.y; }
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

/* what: 0 shape, 1 sprite, 2 label, 3 tilemap, 4 a script's draw(); parents under their children */
typedef struct draw_item { br_entity e; int layer; int what; uint32_t depth; } draw_item;

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

static br_blend_mode blend_of(int b) { return b >= BR_BLEND_ALPHA && b <= BR_BLEND_MULTIPLY ? (br_blend_mode)b : BR_BLEND_ALPHA; }

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
    br_set_blend(r, blend_of(s->blend));
    draw_polygon(r, pts, n, s->color, s->outline);
    br_set_blend(r, BR_BLEND_ALPHA);
}

/* The part of a sprite's image it shows: the whole image, or its frame. */
static br_recti sprite_source(const br_image *img, const br_sprite *s) {
    int fw = (int)s->frame_size.x, fh = (int)s->frame_size.y, per_row, frame;
    if (fw < 1 || fh < 1 || fw > img->width || fh > img->height) return br_recti_make(0, 0, img->width, img->height);
    per_row = img->width / fw;
    frame = s->frame < 0 ? 0 : s->frame;
    if (frame >= per_row * (img->height / fh)) frame = per_row * (img->height / fh) - 1;   /* past the last: the last */
    return br_recti_make((frame % per_row) * fw, (frame / per_row) * fh, fw, fh);
}

static void draw_sprite(br_renderer *r, const br_transform *t, const br_sprite *s) {
    const br_image *img = br_asset_image(s->image);
    if (img) {
        br_recti src = sprite_source(img, s);
        br_vec2 scale = t->scale;
        if (s->flip_x) scale.x = -scale.x;
        if (s->flip_y) scale.y = -scale.y;
        br_set_blend(r, blend_of(s->blend));
        br_draw_sprite(r, img, &src, t->position, br_v2((float)src.w / 2, (float)src.h / 2),
                       scale, t->rotation * (BR_PI / 180.0f), s->tint);
        br_set_blend(r, BR_BLEND_ALPHA);
    } else {                      /* missing image: a magenta box with a cross */
        br_vec2 p[4];
        p[0] = to_world(t, br_v2(-8, -8)); p[1] = to_world(t, br_v2(8, -8));
        p[2] = to_world(t, br_v2(8, 8));   p[3] = to_world(t, br_v2(-8, 8));
        draw_polygon(r, p, 4, br_rgba(255, 0, 255, 120), br_rgb(255, 0, 255));
        br_draw_line(r, p[0], p[2], br_rgb(255, 0, 255));
        br_draw_line(r, p[1], p[3], br_rgb(255, 0, 255));
    }
}

/* Which cells of an unrotated map can show inside the renderer's clip (the
 * others are skipped): columns c0..c1 and rows r0..r1. False: draw them all. */
static bool visible_cells(const br_renderer *r, const br_transform *t, const br_tilemap *m, int *c0, int *c1, int *r0, int *r1) {
    br_vec2 a, b, lo, hi;
    float ts = (float)m->tile_size, left, top;
    if (t->rotation != 0 || br_absf(r->view.b) > 1e-6f || br_absf(r->view.c) > 1e-6f) return false;
    if (br_absf(t->scale.x) < 1e-6f || br_absf(t->scale.y) < 1e-6f) return false;
    a = br_screen_to_world(r, br_v2((float)r->clip.x, (float)r->clip.y));
    b = br_screen_to_world(r, br_v2((float)(r->clip.x + r->clip.w), (float)(r->clip.y + r->clip.h)));
    lo = br_v2((BR_MIN(a.x, b.x) - t->position.x) / t->scale.x, (BR_MIN(a.y, b.y) - t->position.y) / t->scale.y);
    hi = br_v2((BR_MAX(a.x, b.x) - t->position.x) / t->scale.x, (BR_MAX(a.y, b.y) - t->position.y) / t->scale.y);
    if (lo.x > hi.x) { float k = lo.x; lo.x = hi.x; hi.x = k; }
    if (lo.y > hi.y) { float k = lo.y; lo.y = hi.y; hi.y = k; }
    left = -(float)m->columns * ts / 2;
    top = -(float)m->rows * ts / 2;
    *c0 = (int)floorf((lo.x - left) / ts) - 1;
    *c1 = (int)floorf((hi.x - left) / ts) + 1;
    *r0 = (int)floorf((lo.y - top) / ts) - 1;
    *r1 = (int)floorf((hi.y - top) / ts) + 1;
    return true;
}

static void draw_tilemap(br_renderer *r, const br_transform *t, const br_tilemap *m) {
    const br_image *img = br_asset_image(m->tileset);
    int ts = m->tile_size, per_row, x, y, c0 = 0, c1, r0 = 0, r1;
    size_t count;
    float left, top;
    if (!img || ts <= 0 || (per_row = img->width / ts) <= 0) return;
    count = strlen(m->tiles);
    left = -(float)(m->columns * ts) / 2;
    top = -(float)(m->rows * ts) / 2;
    c1 = m->columns - 1;
    r1 = m->rows - 1;
    if (visible_cells(r, t, m, &c0, &c1, &r0, &r1)) {
        if (c0 < 0) c0 = 0;
        if (r0 < 0) r0 = 0;
        if (c1 > m->columns - 1) c1 = m->columns - 1;
        if (r1 > m->rows - 1) r1 = m->rows - 1;
    }
    for (y = r0; y <= r1; ++y) {
        for (x = c0; x <= c1; ++x) {
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

bool br_tilemap_solid_box(const br_world *w, br_entity map, br_vec2 center, br_vec2 size) {
    const br_tilemap *m = tilemap_of(w, map);
    br_transform t;
    float ts, left, top, x0, y0, x1, y1, sx, sy;
    int c0, c1, r0, r1, cx, cy;
    size_t len;
    if (!m || m->tile_size <= 0 || !m->solid[0]) return false;
    t = br_world_transform(w, map);
    if (t.rotation != 0 || br_absf(t.scale.x) < 1e-6f || br_absf(t.scale.y) < 1e-6f) {   /* a turned map: the corners and the middle */
        float hx = size.x / 2, hy = size.y / 2;
        return br_tilemap_solid(w, map, center) || br_tilemap_solid(w, map, br_v2(center.x - hx, center.y - hy)) ||
               br_tilemap_solid(w, map, br_v2(center.x + hx, center.y - hy)) || br_tilemap_solid(w, map, br_v2(center.x - hx, center.y + hy)) ||
               br_tilemap_solid(w, map, br_v2(center.x + hx, center.y + hy));
    }
    ts = (float)m->tile_size;
    sx = t.scale.x; sy = t.scale.y;
    left = -(float)m->columns * ts / 2;
    top = -(float)m->rows * ts / 2;
    x0 = (center.x - size.x / 2 - t.position.x) / sx; x1 = (center.x + size.x / 2 - t.position.x) / sx;
    y0 = (center.y - size.y / 2 - t.position.y) / sy; y1 = (center.y + size.y / 2 - t.position.y) / sy;
    if (x0 > x1) { float k = x0; x0 = x1; x1 = k; }
    if (y0 > y1) { float k = y0; y0 = y1; y1 = k; }
    c0 = (int)floorf((x0 - left) / ts);
    c1 = (int)ceilf((x1 - left) / ts) - 1;                  /* an edge on a cell's edge doesn't reach into it */
    r0 = (int)floorf((y0 - top) / ts);
    r1 = (int)ceilf((y1 - top) / ts) - 1;
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 > m->columns - 1) c1 = m->columns - 1;
    if (r1 > m->rows - 1) r1 = m->rows - 1;
    len = strlen(m->tiles);
    for (cy = r0; cy <= r1; ++cy)
        for (cx = c0; cx <= c1; ++cx) {
            size_t at = (size_t)cy * (size_t)m->columns + (size_t)cx;
            if (at < len && m->tiles[at] && strchr(m->solid, m->tiles[at])) return true;
        }
    return false;
}

char br_tilemap_cell(const br_world *w, br_entity map, int col, int row) {
    const br_tilemap *m = tilemap_of(w, map);
    size_t at;
    if (!m || col < 0 || row < 0 || col >= m->columns || row >= m->rows) return 0;
    at = (size_t)row * (size_t)m->columns + (size_t)col;
    return at < strlen(m->tiles) ? m->tiles[at] : 0;
}

bool br_tilemap_set_cell(br_world *w, br_entity map, int col, int row, char c) {
    br_tilemap *m = tilemap_of(w, map);
    size_t at, len;
    if (!m || !c || col < 0 || row < 0 || col >= m->columns || row >= m->rows) return false;
    at = (size_t)row * (size_t)m->columns + (size_t)col;
    if (at >= BR_TILEMAP_MAX) return false;
    len = strlen(m->tiles);
    while (len < at) m->tiles[len++] = ' ';
    m->tiles[at] = c;
    if (at == len) m->tiles[at + 1] = 0;
    return true;
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

static bool use_camera(br_renderer *r, const br_camera2d *cam, bool *began, const br_camera2d *saved, bool had) {
    if (cam) { br_begin_camera(r, cam); *began = true; }
    else if (*began) {                          /* back to whatever space the caller had */
        if (had) br_begin_camera(r, saved); else br_end_camera(r);
        *began = false;
    }
    return true;
}

void br_builtin_draw_layers(const br_world *w, br_renderer *r, const br_camera2d *world_cam,
                            const br_camera2d *screen_cam, int ui_layer) {
    br_builtin_ids id;
    uint32_t n = 0, i, k;
    br_component kinds[5];
    br_component script = br_component_find(w, "Script");
    bool had = r->camera_active, began = false;
    br_camera2d saved = r->camera;
    int space = -1;                               /* 0: the world's, 1: the screen's */
    if (!br_builtin_ids_of(w, &id)) return;
    kinds[0] = id.shape;
    kinds[1] = id.sprite;
    kinds[2] = id.label;
    kinds[3] = id.tilemap;
    kinds[4] = script;
    for (k = 0; k < 5; ++k) {                     /* everything visible that has a Transform */
        const br_component_pool *p;
        if (kinds[k] == BR_COMPONENT_INVALID) continue;
        p = &w->pools[kinds[k]];
        for (i = 0; i < p->count; ++i) {
            br_entity e = p->entities[i];
            const void *data = p->data + (size_t)i * p->stride;
            int layer = 0;
            if (k == 4) { if (!br_script_draw_layer(w, e, &layer)) continue; }   /* a script with draw() */
            else if (!br_has(w, e, id.transform)) continue;
            else layer = k == 0 ? ((const br_shape *)data)->layer
                       : k == 1 ? ((const br_sprite *)data)->layer
                       : k == 2 ? ((const br_label *)data)->layer : ((const br_tilemap *)data)->layer;
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
            g_items[n].layer = layer;
            ++n;
        }
    }
    qsort(g_items, n, sizeof(draw_item), compare_items);
    for (i = 0; i < n; ++i) {
        const draw_item *it = &g_items[i];
        int want = it->layer >= ui_layer ? 1 : 0;
        if (want != space) {                       /* the world's layers, then the screen's */
            space = want;
            use_camera(r, want ? screen_cam : world_cam, &began, &saved, had);
        }
        if (it->what == 4) { br_script_draw((br_world *)w, it->e, r); continue; }
        {
            br_transform wt = br_world_transform(w, it->e);   /* children follow their parents */
            const br_transform *t = &wt;
            if (it->what == 0) draw_shape(r, t, (const br_shape *)br_get(w, it->e, id.shape));
            else if (it->what == 1) draw_sprite(r, t, (const br_sprite *)br_get(w, it->e, id.sprite));
            else if (it->what == 2) draw_label(r, t, (const br_label *)br_get(w, it->e, id.label));
            else draw_tilemap(r, t, (const br_tilemap *)br_get(w, it->e, id.tilemap));
        }
    }
    use_camera(r, NULL, &began, &saved, had);
}

void br_builtin_draw(const br_world *w, br_renderer *r) {
    br_builtin_draw_layers(w, r, NULL, NULL, 1 << 30);
}

/* ---- animation ------------------------------------------------------------------ */

int br_animation_parse(const char *frames, int *out, int max) {
    const char *p = frames;
    int count = 0;
    for (;;) {
        long a, b, step;
        char *end;
        while (*p == ' ' || *p == ',' || *p == '\t') ++p;
        if (!*p) return count;
        if (*p < '0' || *p > '9') return -1;
        a = strtol(p, &end, 10);
        p = end;
        while (*p == ' ') ++p;
        b = a;
        if (*p == '-') {
            ++p;
            while (*p == ' ') ++p;
            if (*p < '0' || *p > '9') return -1;
            b = strtol(p, &end, 10);
            p = end;
        }
        if (a > 4095 || b > 4095) return -1;
        step = b >= a ? 1 : -1;
        for (;; a += step) {
            if (out && count < max) out[count] = (int)a;
            if (++count > 4096) return -1;
            if (a == b) break;
        }
        while (*p == ' ') ++p;
        if (*p && *p != ',') return -1;
    }
}

void br_animation_step(br_world *w, float dt) {
    br_builtin_ids id;
    br_component with[2];
    br_query q;
    int frames[256];
    if (!br_builtin_ids_of(w, &id)) return;
    with[0] = id.animation;
    with[1] = id.sprite;
    q = br_query_make(w, with, 2);
    while (br_query_next(&q)) {
        br_animation *a = BR_QUERY_GET(&q, 0, br_animation);
        br_sprite *sp = BR_QUERY_GET(&q, 1, br_sprite);
        int n = br_animation_parse(a->frames, frames, 256), at;
        if (n > 256) n = 256;
        if (strcmp(a->started, a->frames) != 0) {      /* a new list: from its first frame */
            snprintf(a->started, sizeof(a->started), "%s", a->frames);
            a->time = 0;
            if (n > 0) sp->frame = frames[0];
        }
        if (n <= 0 || !a->playing) continue;
        if (a->fps > 0) a->time += dt;
        at = a->fps > 0 ? (int)floorf(a->time * a->fps + 1e-4f) : 0;
        if (at >= n) {
            if (a->loop) { at %= n; a->time = fmodf(a->time, (float)n / a->fps); }
            else { at = n - 1; a->playing = false; }
        }
        sp->frame = frames[at];
    }
}

static void animation_added(br_world *w, br_entity e, void *data, void *user) {
    br_animation *a = (br_animation *)data;
    BR_UNUSED(w); BR_UNUSED(e); BR_UNUSED(user);
    a->time = 0;
    a->started[0] = 0;
}

/* ---- cameras ---------------------------------------------------------------------- */

br_entity br_camera_current(const br_world *w) {
    br_builtin_ids id;
    const br_component_pool *p;
    uint32_t i;
    if (!br_builtin_ids_of(w, &id)) return BR_ENTITY_NULL;
    p = &w->pools[id.camera];
    for (i = 0; i < p->count; ++i) {
        const br_camera *c = (const br_camera *)(p->data + (size_t)i * p->stride);
        if (c->current && br_has(w, p->entities[i], id.transform)) return p->entities[i];
    }
    return BR_ENTITY_NULL;
}

/* Keeps a centre inside the limits for a view of half-size h (an area smaller than the view: its middle). */
static float limit_axis(float c, float h, float lo, float hi) {
    if (hi - lo <= 2 * h) return (lo + hi) / 2;
    if (c - h < lo) return lo + h;
    if (c + h > hi) return hi - h;
    return c;
}

static float cam_zoom(const br_camera *c) { return c->zoom > 0.01f ? c->zoom : 1; }

bool br_camera_view(const br_world *w, int width, int height, br_camera2d *out, int *ui_layer) {
    br_entity e = br_camera_current(w);
    br_builtin_ids id;
    const br_camera *c;
    br_transform t;
    br_vec2 target;
    float z;
    *out = br_camera2d_make(br_v2((float)width / 2, (float)height / 2), br_v2((float)width / 2, (float)height / 2));
    *ui_layer = 1 << 30;
    if (e == BR_ENTITY_NULL || !br_builtin_ids_of(w, &id)) return false;
    c = (const br_camera *)br_get(w, e, id.camera);
    t = br_world_transform(w, e);
    z = cam_zoom(c);
    target = br_v2_add(t.position, c->shake_offset);
    if (c->pixel_snap) {                                  /* whole screen pixels */
        target.x = floorf(target.x * z + 0.5f) / z;
        target.y = floorf(target.y * z + 0.5f) / z;
    }
    out->target = target;
    out->zoom = z;
    out->rotation = t.rotation * (BR_PI / 180.0f);
    *ui_layer = c->ui_layer;
    return true;
}

static int g_view_w = 320, g_view_h = 180;              /* the screen size cameras last drew for */

/* Where a camera wants to be, from where it is. */
static br_vec2 camera_goal(const br_world *w, const br_builtin_ids *id, br_entity e, const br_camera *c, br_vec2 pos) {
    float z = cam_zoom(c), hw = (float)g_view_w / 2 / z, hh = (float)g_view_h / 2 / z;
    br_vec2 goal = pos;
    if (c->follow != BR_ENTITY_NULL && br_entity_alive(w, c->follow) && br_has(w, c->follow, id->transform)) {
        br_vec2 aim = br_v2_add(br_world_transform(w, c->follow).position, c->follow_offset);
        float dx = aim.x - pos.x, dy = aim.y - pos.y;
        if (dx > c->deadzone.x) goal.x = aim.x - c->deadzone.x; else if (dx < -c->deadzone.x) goal.x = aim.x + c->deadzone.x;
        if (dy > c->deadzone.y) goal.y = aim.y - c->deadzone.y; else if (dy < -c->deadzone.y) goal.y = aim.y + c->deadzone.y;
    }
    BR_UNUSED(e);
    if (c->limit) {
        goal.x = limit_axis(goal.x, hw, BR_MIN(c->limit_min.x, c->limit_max.x), BR_MAX(c->limit_min.x, c->limit_max.x));
        goal.y = limit_axis(goal.y, hh, BR_MIN(c->limit_min.y, c->limit_max.y), BR_MAX(c->limit_min.y, c->limit_max.y));
    }
    return goal;
}

static void camera_move(br_world *w, float dt, bool snap) {
    br_builtin_ids id;
    br_component_pool *p;
    uint32_t i;
    if (!br_builtin_ids_of(w, &id)) return;
    if (w->context) { g_view_w = ((br_engine *)w->context)->framebuffer.width; g_view_h = ((br_engine *)w->context)->framebuffer.height; }
    p = &w->pools[id.camera];
    for (i = 0; i < p->count; ++i) {
        br_entity e = p->entities[i];
        br_camera *c = (br_camera *)(p->data + (size_t)i * p->stride);
        br_transform wt;
        br_vec2 goal, pos;
        if (!br_has(w, e, id.transform)) continue;
        wt = br_world_transform(w, e);
        goal = camera_goal(w, &id, e, c, wt.position);
        pos = goal;
        if (!snap && c->placed && c->smoothing > 0 && dt > 0) {
            float k = 1 - expf(-dt * 3.0f / c->smoothing);       /* about 95% of the way in `smoothing` seconds */
            pos = br_v2_add(wt.position, br_v2_scale(br_v2_sub(goal, wt.position), k));
        }
        c->placed = true;
        if (pos.x != wt.position.x || pos.y != wt.position.y) br_set_world_position(w, e, pos);
        if (c->shake_left > 0 && dt > 0) {                         /* shaking: a new offset each step, fading */
            float f;
            c->shake_left -= dt;
            f = c->shake_time > 0 ? BR_MAX(c->shake_left, 0) / c->shake_time : 0;
            c->shake_seed = c->shake_seed * 1664525u + 1013904223u;
            c->shake_offset.x = (((float)((c->shake_seed >> 8) & 0xFFFF) / 65535.0f) * 2 - 1) * c->shake * f;
            c->shake_seed = c->shake_seed * 1664525u + 1013904223u;
            c->shake_offset.y = (((float)((c->shake_seed >> 8) & 0xFFFF) / 65535.0f) * 2 - 1) * c->shake * f;
            if (c->shake_left <= 0) c->shake_offset = br_v2(0, 0);
        }
    }
}

void br_camera_step(br_world *w, float dt) { camera_move(w, dt, false); }
void br_camera_snap(br_world *w) { camera_move(w, 0, true); }

void br_camera_shake(br_world *w, float strength, float seconds) {
    br_builtin_ids id;
    br_entity e = br_camera_current(w);
    br_camera *c;
    if (e == BR_ENTITY_NULL || !br_builtin_ids_of(w, &id)) return;
    c = (br_camera *)br_get(w, e, id.camera);
    if (strength <= 0 || seconds <= 0) { c->shake_left = 0; c->shake_offset = br_v2(0, 0); return; }
    if (c->shake_left > 0 && c->shake * (c->shake_left / BR_MAX(c->shake_time, 1e-3f)) > strength) return;   /* a bigger one is going */
    c->shake = strength;
    c->shake_time = c->shake_left = seconds;
    if (!c->shake_seed) c->shake_seed = 0x9E3779B9u ^ (uint32_t)e;
}

static void camera_added(br_world *w, br_entity e, void *data, void *user) {
    br_camera *c = (br_camera *)data;
    BR_UNUSED(w); BR_UNUSED(e); BR_UNUSED(user);
    c->shake = c->shake_time = c->shake_left = 0;
    c->shake_offset = br_v2(0, 0);
    c->shake_seed = 0;
    c->placed = false;
}

void br_builtin_release(void) {
    br_script_release();
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

static void sys_animation(br_world *w, float dt, void *user) { BR_UNUSED(user); br_animation_step(w, dt); }
static void sys_camera(br_world *w, float dt, void *user) { BR_UNUSED(user); br_camera_step(w, dt); }

static void sys_draw(br_world *w, float dt, void *user) {
    br_engine *e = (br_engine *)w->context;
    br_camera2d cam;
    int ui_layer;
    BR_UNUSED(dt);
    BR_UNUSED(user);
    if (!e) return;
    g_view_w = e->framebuffer.width;
    g_view_h = e->framebuffer.height;
    if (br_camera_view(w, e->framebuffer.width, e->framebuffer.height, &cam, &ui_layer))
        br_builtin_draw_layers(w, &e->renderer, &cam, NULL, ui_layer);     /* the world through the camera, the HUD on the screen */
    else br_builtin_draw(w, &e->renderer);
}

void br_builtin_register(br_world *w, br_builtin_ids *ids) {
    int i;
    for (i = 0; i < (int)BR_ARRAY_COUNT(k_descs); ++i) br_component_register_desc(w, &k_descs[i]);
    br_system_add(w, "builtin_motion", BR_PHASE_FIXED_UPDATE, sys_motion, NULL, 0);
    br_system_add(w, "builtin_audio", BR_PHASE_FIXED_UPDATE, sys_audio, NULL, 0);
    br_system_add(w, "builtin_animation", BR_PHASE_FIXED_UPDATE, sys_animation, NULL, 25);
    br_system_add(w, "builtin_camera", BR_PHASE_FIXED_UPDATE, sys_camera, NULL, 40);
    br_component_set_hooks(w, br_component_find(w, "Sound"), sound_added, sound_removed, NULL);
    br_component_set_hooks(w, br_component_find(w, "Animation"), animation_added, NULL, NULL);
    br_component_set_hooks(w, br_component_find(w, "Camera"), camera_added, NULL, NULL);
    br_hierarchy_register(w);
    br_physics_register(w);
    br_script_register(w);
    br_system_add(w, "builtin_draw", BR_PHASE_RENDER, sys_draw, NULL, 0);
    if (ids) br_builtin_ids_of(w, ids);
}
