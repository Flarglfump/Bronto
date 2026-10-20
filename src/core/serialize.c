/*
 * Bronto Engine - scene files (see serialize.h for the format).
 */
#include "bronto/serialize.h"
#include "bronto/memory.h"
#include "bronto/log.h"
#include "bronto/bmath.h"
#include "bronto/render.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VALUE_MAX 1100      /* longest formatted value (strings up to 512 chars) */
#define LINE_MAX_LEN 1200

/* ---- a growable string ----------------------------------------------------- */

typedef struct strbuf { char *data; size_t len, cap; bool failed; } strbuf;

static void sb_append(strbuf *b, const char *s, size_t n) {
    if (b->failed) return;
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 1024;
        char *grown;
        while (b->len + n + 1 > cap) cap *= 2;
        grown = (char *)br_realloc(b->data, cap);
        if (!grown) { b->failed = true; return; }
        b->data = grown;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void sb_puts(strbuf *b, const char *s) { sb_append(b, s, strlen(s)); }

/* ---- field values ------------------------------------------------------------- */

static size_t bounded_len(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) ++n;
    return n;
}

void br_field_format(const br_field *f, const void *component, char *out, size_t size) {
    const unsigned char *p = (const unsigned char *)component + f->offset;
    if (size == 0) return;
    out[0] = 0;
    switch (f->type) {
    case BR_FIELD_INT: { int v; memcpy(&v, p, sizeof(v)); snprintf(out, size, "%d", v); break; }
    case BR_FIELD_FLOAT: { float v; memcpy(&v, p, sizeof(v)); snprintf(out, size, "%.9g", (double)v); break; }
    case BR_FIELD_BOOL: { bool v; memcpy(&v, p, sizeof(v)); snprintf(out, size, "%s", v ? "true" : "false"); break; }
    case BR_FIELD_VEC2: {
        br_vec2 v;
        memcpy(&v, p, sizeof(v));
        snprintf(out, size, "%.9g %.9g", (double)v.x, (double)v.y);
        break;
    }
    case BR_FIELD_COLOR: {
        br_color c;
        memcpy(&c, p, sizeof(c));
        snprintf(out, size, "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
        break;
    }
    case BR_FIELD_ENUM: {
        int v;
        memcpy(&v, p, sizeof(v));
        if (f->enum_names && v >= 0 && v < f->enum_count) snprintf(out, size, "%s", f->enum_names[v]);
        else snprintf(out, size, "%d", v);
        break;
    }
    case BR_FIELD_ENTITY: {             /* on its own, a raw handle; scene files write file positions */
        br_entity e;
        memcpy(&e, p, sizeof(e));
        if (e == BR_ENTITY_NULL) snprintf(out, size, "none");
        else snprintf(out, size, "@%u", (unsigned)e);
        break;
    }
    case BR_FIELD_STRING: {
        const char *s = (const char *)p;
        size_t n = bounded_len(s, f->size), i, o = 0;
        out[o++] = '"';
        for (i = 0; i < n && o + 3 < size; ++i) {
            char ch = s[i];
            if (ch == '"' || ch == '\\') { out[o++] = '\\'; out[o++] = ch; }
            else if (ch == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
            else if (ch == '\t') { out[o++] = '\\'; out[o++] = 't'; }
            else out[o++] = ch;
        }
        out[o++] = '"';
        out[o] = 0;
        break;
    }
    }
}

static const char *skip_space(const char *s) { while (*s == ' ' || *s == '\t') ++s; return s; }
static bool at_end(const char *s) { return *skip_space(s) == 0; }

static bool parse_float(const char **s, float *out) {
    char *end;
    double v = strtod(*s, &end);
    if (end == *s || !isfinite(v)) return false;
    *out = (float)v;
    *s = end;
    return true;
}

static bool parse_int(const char *s, int *out) {
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s || !at_end(end) || v < -2147483647L - 1 || v > 2147483647L) return false;
    *out = (int)v;
    return true;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_color(const char *s, br_color *out) {
    int v[8], n = 0, i;
    if (*s == '#') {
        for (++s; *s && !isspace((unsigned char)*s) && n < 8; ++s, ++n)
            if ((v[n] = hex_digit(*s)) < 0) return false;
        if (!at_end(s) || (n != 6 && n != 8)) return false;
        out->r = (uint8_t)(v[0] * 16 + v[1]);
        out->g = (uint8_t)(v[2] * 16 + v[3]);
        out->b = (uint8_t)(v[4] * 16 + v[5]);
        out->a = n == 8 ? (uint8_t)(v[6] * 16 + v[7]) : 255;
        return true;
    }
    for (i = 0; i < 4; ++i) {               /* or "r g b a" */
        char *end;
        long c = strtol(s, &end, 10);
        if (end == s || c < 0 || c > 255) return false;
        v[i] = (int)c;
        s = end;
    }
    if (!at_end(s)) return false;
    *out = br_rgba((uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3]);
    return true;
}

static bool parse_string(const char *s, char *out, size_t size) {
    size_t o = 0;
    if (*s++ != '"') return false;
    while (*s && *s != '"') {
        char ch = *s++;
        if (ch == '\\') {
            ch = *s++;
            if (ch == 'n') ch = '\n';
            else if (ch == 't') ch = '\t';
            else if (ch != '"' && ch != '\\') return false;
        }
        if (o + 1 < size) out[o++] = ch;   /* longer than the field: truncated */
    }
    if (*s != '"' || !at_end(s + 1)) return false;
    out[o] = 0;
    return true;
}

bool br_field_parse(const br_field *f, void *component, const char *text) {
    unsigned char *p = (unsigned char *)component + f->offset;
    const char *s = skip_space(text);
    switch (f->type) {
    case BR_FIELD_INT: { int v; if (!parse_int(s, &v)) return false; memcpy(p, &v, sizeof(v)); return true; }
    case BR_FIELD_FLOAT: {
        float v;
        if (!parse_float(&s, &v) || !at_end(s)) return false;
        memcpy(p, &v, sizeof(v));
        return true;
    }
    case BR_FIELD_BOOL: {
        bool v;
        if (strncmp(s, "true", 4) == 0 && at_end(s + 4)) v = true;
        else if (strncmp(s, "false", 5) == 0 && at_end(s + 5)) v = false;
        else if ((*s == '0' || *s == '1') && at_end(s + 1)) v = *s == '1';
        else return false;
        memcpy(p, &v, sizeof(v));
        return true;
    }
    case BR_FIELD_VEC2: {
        br_vec2 v;
        if (!parse_float(&s, &v.x) || !parse_float(&s, &v.y) || !at_end(s)) return false;
        memcpy(p, &v, sizeof(v));
        return true;
    }
    case BR_FIELD_COLOR: { br_color c; if (!parse_color(s, &c)) return false; memcpy(p, &c, sizeof(c)); return true; }
    case BR_FIELD_ENUM: {
        int i, v;
        size_t n = 0;
        while (s[n] && !isspace((unsigned char)s[n])) ++n;
        for (i = 0; f->enum_names && i < f->enum_count; ++i)
            if (strlen(f->enum_names[i]) == n && strncmp(f->enum_names[i], s, n) == 0 && at_end(s + n)) {
                memcpy(p, &i, sizeof(i));
                return true;
            }
        if (!parse_int(s, &v)) return false;     /* a number also works */
        memcpy(p, &v, sizeof(v));
        return true;
    }
    case BR_FIELD_ENTITY: {
        long v;
        char *end;
        br_entity e = BR_ENTITY_NULL;
        if (strncmp(s, "none", 4) == 0 && at_end(s + 4)) { memcpy(p, &e, sizeof(e)); return true; }
        if (*s != '@') return false;
        v = strtol(s + 1, &end, 10);
        if (end == s + 1 || !at_end(end) || v < 0) return false;
        e = (br_entity)v;
        memcpy(p, &e, sizeof(e));
        return true;
    }
    case BR_FIELD_STRING: {
        char buf[VALUE_MAX];
        if (f->size == 0 || !parse_string(s, buf, f->size < sizeof(buf) ? f->size : sizeof(buf))) return false;
        memcpy(p, buf, strlen(buf) + 1);
        return true;
    }
    }
    return false;
}

/* ---- writing --------------------------------------------------------------------- */

/* Entity references are written as the referenced entity's position among
 * those being saved (0 = the first), found through a sorted handle list. */
typedef struct ref_slot { br_entity e; uint32_t index; } ref_slot;
static int compare_ref_slots(const void *a, const void *b) {
    br_entity x = ((const ref_slot *)a)->e, y = ((const ref_slot *)b)->e;
    return x < y ? -1 : x > y ? 1 : 0;
}
typedef struct ref_map { ref_slot *slots; uint32_t count; } ref_map;

static long ref_index(const ref_map *m, br_entity e) {
    uint32_t lo = 0, hi = m->count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (m->slots[mid].e < e) lo = mid + 1;
        else hi = mid;
    }
    return lo < m->count && m->slots[lo].e == e ? (long)m->slots[lo].index : -1;
}

static void write_entity(strbuf *b, const br_world *w, br_entity e, const ref_map *refs) {
    uint32_t c;
    int i;
    char value[VALUE_MAX];
    sb_puts(b, "\nentity\n");
    for (c = 0; c < w->pool_count; ++c) {
        const br_component_desc *d = w->pools[c].desc;
        const void *data;
        if (!d || !br_has(w, e, c)) continue;
        data = br_get(w, e, c);
        sb_puts(b, "  ");
        sb_puts(b, d->name);
        sb_puts(b, "\n");
        for (i = 0; i < d->field_count; ++i) {
            if (d->fields[i].type == BR_FIELD_ENTITY) {
                br_entity ref;
                long at;
                memcpy(&ref, (const unsigned char *)data + d->fields[i].offset, sizeof(ref));
                at = ref == BR_ENTITY_NULL || !br_entity_alive(w, ref) ? -1 : ref_index(refs, ref);
                if (at < 0) snprintf(value, sizeof(value), "none");     /* not saved with it: dropped */
                else snprintf(value, sizeof(value), "%ld", at);
            } else {
                br_field_format(&d->fields[i], data, value, sizeof(value));
            }
            sb_puts(b, "    ");
            sb_puts(b, d->fields[i].name);
            sb_puts(b, " = ");
            sb_puts(b, value);
            sb_puts(b, "\n");
        }
    }
}

char *br_world_to_text(const br_world *w, const br_entity *order, uint32_t count, size_t *out_len) {
    strbuf b;
    char header[64];
    uint32_t i, n = 0;
    ref_map refs;
    br_entity *list = (br_entity *)br_alloc(sizeof(br_entity) * (order ? count : w->high_water) + 1);
    memset(&b, 0, sizeof(b));
    memset(&refs, 0, sizeof(refs));
    if (!list) return NULL;
    if (order) {
        for (i = 0; i < count; ++i) if (br_entity_alive(w, order[i])) list[n++] = order[i];
    } else {
        for (i = 0; i < w->high_water; ++i)
            if (w->alive[i]) list[n++] = (br_entity)(((uint32_t)w->generations[i] << BR_ENTITY_INDEX_BITS) | i);
    }
    refs.slots = (ref_slot *)br_alloc(sizeof(ref_slot) * n + 1);
    if (!refs.slots) { br_free(list); return NULL; }
    for (i = 0; i < n; ++i) { refs.slots[i].e = list[i]; refs.slots[i].index = i; }
    refs.count = n;
    if (n > 1) qsort(refs.slots, n, sizeof(ref_slot), compare_ref_slots);
    snprintf(header, sizeof(header), "# Bronto scene\nbronto_scene %d\n", BR_SCENE_FORMAT_VERSION);
    sb_puts(&b, header);
    for (i = 0; i < n; ++i) write_entity(&b, w, list[i], &refs);
    br_free(refs.slots);
    br_free(list);
    if (b.failed) { br_free(b.data); return NULL; }
    if (out_len) *out_len = b.len;
    return b.data;
}

bool br_world_save_file(const br_world *w, const br_entity *order, uint32_t count, const char *path) {
    size_t len;
    char *text = br_world_to_text(w, order, count, &len);
    bool ok = text && br_file_write_all(path, text, len);
    br_free(text);
    if (!ok) BR_ERROR("could not write scene '%s'", path);
    return ok;
}

/* ---- reading ------------------------------------------------------------------------ */

typedef struct scene_reader {
    br_world *w;
    br_entity *made;
    uint32_t made_count, made_cap;
    char *err;
    size_t err_size;
    int line;
} scene_reader;

static bool fail(scene_reader *r, const char *fmt, ...) {
    va_list ap;
    if (r->err && r->err_size) {
        int n = snprintf(r->err, r->err_size, "line %d: ", r->line);
        va_start(ap, fmt);
        if (n >= 0 && (size_t)n < r->err_size) vsnprintf(r->err + n, r->err_size - (size_t)n, fmt, ap);
        va_end(ap);
    }
    return false;
}

static bool remember(scene_reader *r, br_entity e) {
    if (r->made_count == r->made_cap) {
        uint32_t cap = r->made_cap ? r->made_cap * 2 : 64;
        br_entity *grown = (br_entity *)br_realloc(r->made, cap * sizeof(br_entity));
        if (!grown) return false;
        r->made = grown;
        r->made_cap = cap;
    }
    r->made[r->made_count++] = e;
    return true;
}

static void trim(char *s) {
    size_t n = strlen(s), start = 0;
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    while (s[start] == ' ' || s[start] == '\t') ++start;
    if (start) memmove(s, s + start, n - start + 1);
}

static const br_field *find_field(const br_component_desc *d, const char *name) {
    int i;
    for (i = 0; i < d->field_count; ++i)
        if (strcmp(d->fields[i].name, name) == 0) return &d->fields[i];
    return NULL;
}

/* One pass over the text; returns false on the first error. */
static bool read_all(scene_reader *r, const char *p) {
    char line[LINE_MAX_LEN];
    bool header = false, skipping = false;
    br_entity cur = BR_ENTITY_NULL;
    br_component comp = BR_COMPONENT_INVALID;
    const br_component_desc *desc = NULL;
    while (*p) {
        size_t n = 0;
        char *eq;
        while (p[n] && p[n] != '\n') ++n;
        r->line++;
        if (n >= sizeof(line)) return fail(r, "line too long");
        memcpy(line, p, n);
        line[n] = 0;
        p += n + (p[n] == '\n');
        trim(line);
        if (!line[0] || line[0] == '#') continue;

        if (!header) {
            int version;
            if (strncmp(line, "bronto_scene", 12) != 0 || !parse_int(line + 12, &version))
                return fail(r, "not a Bronto scene (expected 'bronto_scene %d')", BR_SCENE_FORMAT_VERSION);
            if (version > BR_SCENE_FORMAT_VERSION) return fail(r, "scene format %d is newer than this engine (%d)", version, BR_SCENE_FORMAT_VERSION);
            header = true;
            continue;
        }
        if (strcmp(line, "entity") == 0) {
            cur = br_entity_create(r->w);
            if (cur == BR_ENTITY_NULL || !remember(r, cur)) return fail(r, "out of memory");
            comp = BR_COMPONENT_INVALID;
            skipping = false;
            continue;
        }
        if ((eq = strchr(line, '=')) != NULL) {                 /* field = value */
            const br_field *f;
            void *data;
            *eq = 0;
            trim(line);
            if (cur == BR_ENTITY_NULL) return fail(r, "'%s' is outside an entity", line);
            if (skipping) continue;
            if (comp == BR_COMPONENT_INVALID) return fail(r, "field '%s' comes before any component", line);
            f = find_field(desc, line);
            if (!f) { BR_WARN("scene line %d: %s has no field '%s' (ignored)", r->line, desc->name, line); continue; }
            data = br_get(r->w, cur, comp);
            if (f->type == BR_FIELD_ENTITY) {                    /* a position in this file, connected at the end */
                const char *v = skip_space(eq + 1);
                int at;
                br_entity slot = BR_ENTITY_NULL;
                if (!(strncmp(v, "none", 4) == 0 && at_end(v + 4))) {
                    if (!parse_int(v, &at) || at < 0) return fail(r, "bad value for %s.%s: %s (expected an entity number or none)", desc->name, f->name, v);
                    slot = (br_entity)at + 1;
                }
                memcpy((unsigned char *)data + f->offset, &slot, sizeof(slot));
                continue;
            }
            if (!br_field_parse(f, data, eq + 1)) return fail(r, "bad value for %s.%s: %s", desc->name, f->name, skip_space(eq + 1));
            continue;
        }
        if (cur == BR_ENTITY_NULL) return fail(r, "component '%s' is outside an entity", line);
        comp = br_component_find(r->w, line);                     /* a component */
        desc = br_component_get_desc(r->w, comp);
        if (comp == BR_COMPONENT_INVALID || !desc) {
            BR_WARN("scene line %d: unknown component '%s' (skipped)", r->line, line);
            comp = BR_COMPONENT_INVALID;
            skipping = true;
            continue;
        }
        skipping = false;
        if (!br_add(r->w, cur, comp, NULL)) return fail(r, "could not add %s", line);
    }
    if (!header) return fail(r, "not a Bronto scene (empty)");
    return true;
}

int br_world_from_text(br_world *w, const char *text, br_entity *out, uint32_t cap, char *err, size_t err_size) {
    scene_reader r;
    uint32_t i;
    int count;
    memset(&r, 0, sizeof(r));
    r.w = w;
    r.err = err;
    r.err_size = err_size;
    if (err && err_size) err[0] = 0;
    if (!read_all(&r, text ? text : "")) {
        for (i = 0; i < r.made_count; ++i) br_entity_destroy(w, r.made[i]);   /* all or nothing */
        br_free(r.made);
        return -1;
    }
    for (i = 0; i < r.made_count; ++i) {                       /* entity references: positions -> handles */
        uint32_t c;
        for (c = 0; c < w->pool_count; ++c) {
            const br_component_desc *d = w->pools[c].desc;
            unsigned char *data;
            int k;
            if (!d || !(data = (unsigned char *)br_get(w, r.made[i], c))) continue;
            for (k = 0; k < d->field_count; ++k) {
                br_entity ref;
                if (d->fields[k].type != BR_FIELD_ENTITY) continue;
                memcpy(&ref, data + d->fields[k].offset, sizeof(ref));
                ref = ref >= 1 && ref <= r.made_count ? r.made[ref - 1] : BR_ENTITY_NULL;
                memcpy(data + d->fields[k].offset, &ref, sizeof(ref));
            }
        }
    }
    for (i = 0; out && i < r.made_count && i < cap; ++i) out[i] = r.made[i];
    count = (int)r.made_count;
    br_free(r.made);
    return count;
}

int br_world_load_file(br_world *w, const char *path, br_entity *out, uint32_t cap, char *err, size_t err_size) {
    size_t size;
    char *text = (char *)br_file_read_all(path, &size);
    int n;
    if (!text) {
        if (err && err_size) snprintf(err, err_size, "can't read '%s'", path);
        return -1;
    }
    n = br_world_from_text(w, text, out, cap, err, err_size);
    br_free(text);
    return n;
}
