/*
 * Bronto Engine - scene files (see serialize.h for the format).
 */
#include "bronto/serialize.h"
#include "bronto/memory.h"
#include "bronto/log.h"
#include "bronto/bmath.h"
#include "bronto/render.h"
#include "bronto/hierarchy.h"
#include "bronto/assets.h"
#include "bronto/components.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VALUE_MAX 4400      /* longest formatted value (a Tilemap's 2048 cells, escaped) */
#define LINE_MAX_LEN 4500

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
    case BR_FIELD_VEC3: {
        br_vec3 v;
        memcpy(&v, p, sizeof(v));
        snprintf(out, size, "%.9g %.9g %.9g", (double)v.x, (double)v.y, (double)v.z);
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
    if (end == *s || !br_finite(v)) return false;
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
    case BR_FIELD_VEC3: {
        br_vec3 v;
        if (!parse_float(&s, &v.x) || !parse_float(&s, &v.y) || !parse_float(&s, &v.z) || !at_end(s)) return false;
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

/* ---- prefab instances: shared by writing and reading --------------------------------------
 *
 * An entity with a Prefab component is an instance of another scene file: its
 * entities are made from that file when the scene loads (so a changed prefab
 * changes every instance), and the scene only says what's different. Its parts
 * are found by their names from the instance's root ("Arm/Hand"; "Leaf#2" is
 * the second child called Leaf).
 */

static int g_prefab_depth;                 /* (prefabs inside prefabs, but not round in a circle) */
static char g_prefab_stack[16][256];

static const char *sr_name(const br_world *w, br_entity e) {
    br_component c = br_component_find(w, "Name");
    const void *n = c != BR_COMPONENT_INVALID ? br_get(w, e, c) : NULL;
    return n ? (const char *)n : "";                             /* (Name is a char[32] at offset 0) */
}

/* The children of e in `rank` order (creation or list order), into out; returns how many. */
static uint32_t sr_children(const br_world *w, br_entity e, const uint32_t *rank, br_entity *out, uint32_t cap) {
    uint32_t n = br_children(w, e, out, cap), i, k;
    if (n > cap) n = cap;
    for (i = 1; i < n; ++i) {                                    /* (few children: insertion sort) */
        br_entity x = out[i];
        uint32_t rx = rank ? rank[BR_ENTITY_INDEX(x)] : BR_ENTITY_INDEX(x);
        k = i;
        while (k > 0 && (rank ? rank[BR_ENTITY_INDEX(out[k - 1])] : BR_ENTITY_INDEX(out[k - 1])) > rx) { out[k] = out[k - 1]; --k; }
        out[k] = x;
    }
    return n;
}

/* The part of an instance at `path` below `root` (BR_ENTITY_NULL: none). */
static br_entity sr_find_path(const br_world *w, br_entity root, const char *path, const uint32_t *rank) {
    br_entity cur = root, kids[256];
    while (*path && cur != BR_ENTITY_NULL) {
        char seg[80];
        size_t n = 0;
        int want = 1, seen = 0;
        uint32_t count, i;
        const char *hash;
        while (path[n] && path[n] != '/' && n + 1 < sizeof(seg)) { seg[n] = path[n]; ++n; }
        seg[n] = 0;
        path += n;
        if (*path == '/') ++path;
        if ((hash = strrchr(seg, '#')) != NULL && hash[1] >= '0' && hash[1] <= '9') { want = atoi(hash + 1); seg[hash - seg] = 0; }
        count = sr_children(w, cur, rank, kids, 256);
        cur = BR_ENTITY_NULL;
        for (i = 0; i < count; ++i)
            if (strcmp(sr_name(w, kids[i]), seg) == 0 && ++seen == want) { cur = kids[i]; break; }
    }
    return cur;
}

/* The path of `e` below `root` (false if e isn't below it). */
static bool sr_path_of(const br_world *w, br_entity root, br_entity e, const uint32_t *rank, char *out, size_t size) {
    char tmp[512];
    br_entity kids[256];
    out[0] = 0;
    while (e != root) {
        br_entity parent = br_parent_of(w, e);
        uint32_t count, i;
        int same = 0, mine = 0;
        char seg[96];
        if (parent == BR_ENTITY_NULL) return false;
        count = sr_children(w, parent, rank, kids, 256);
        for (i = 0; i < count; ++i)
            if (strcmp(sr_name(w, kids[i]), sr_name(w, e)) == 0) { ++same; if (kids[i] == e) mine = same; }
        if (mine > 1) snprintf(seg, sizeof(seg), "%s#%d", sr_name(w, e), mine);
        else snprintf(seg, sizeof(seg), "%s", sr_name(w, e));
        snprintf(tmp, sizeof(tmp), "%s%s%s", seg, out[0] ? "/" : "", out);
        snprintf(out, size, "%s", tmp);
        e = parent;
    }
    return true;
}

/* A prefab file's text (relative paths: from the project's folder). */
static char *sr_read_prefab(const char *file) {
    char full[1024];
    size_t size;
    const char *root = br_assets_root();
    if (file[0] == '/' || file[0] == '\\' || (file[0] && file[1] == ':')) snprintf(full, sizeof(full), "%s", file);
    else snprintf(full, sizeof(full), "%s%s", root ? root : "", file);
    return (char *)br_file_read_all(full, &size);
}

static const char *sr_prefab_file(const br_world *w, br_entity e) {
    br_component c = br_component_find(w, "Prefab");
    const br_prefab *p = c != BR_COMPONENT_INVALID ? (const br_prefab *)br_get(w, e, c) : NULL;
    return p && p->file[0] ? p->file : NULL;
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

static void sort_refs(ref_map *m) { if (m->count > 1) qsort(m->slots, m->count, sizeof(ref_slot), compare_ref_slots); }

/* One instance being written: its prefab, made again in a world of its own to compare with. */
/* A prefab file, made once per save however many instances it has. */
typedef struct wr_prefab {
    char file[128];
    br_world pw;                    /* the prefab, as its file makes it */
    br_entity proot;
    br_entity *pmade;               /* the prefab's entities, made in this order */
    uint32_t pcount;
    uint32_t *prank;                /* ... their places in that order, by entity index */
    uint32_t prank_n;
    bool ok;
} wr_prefab;

typedef struct wr_inst {
    br_entity root;                 /* in the scene */
    wr_prefab *pf;
    ref_map to_prefab;              /* scene part -> its prefab entity */
    ref_map from_prefab;            /* prefab entity -> scene part */
    bool ok;
} wr_inst;

typedef struct wr_ctx {
    const br_world *w;
    br_entity *list;
    uint32_t n;
    uint32_t *rank;                 /* places in `list`, by entity index */
    uint32_t rank_n;
    uint8_t *kind;                  /* per list entry: 0 plain, 1 instance root, 2 part (written with its root) */
    int *inst_of;                   /* per list entry: its instance (roots and parts) */
    wr_inst *inst;
    int inst_count;
    wr_prefab *prefabs;             /* (one each: as many as instances, at most) */
    int prefab_count;
    ref_map pos;                    /* entity -> its block's position in the file */
    uint8_t *referenced;            /* per list entry: something refers to it */
} wr_ctx;

static long list_index(const wr_ctx *c, br_entity e) {
    uint32_t ix = BR_ENTITY_INDEX(e);
    if (e == BR_ENTITY_NULL || ix >= c->rank_n || c->rank[ix] == UINT32_MAX || c->list[c->rank[ix]] != e) return -1;
    return (long)c->rank[ix];
}

static void wr_ref_value(const wr_ctx *c, br_entity ref, char *out, size_t size) {
    long at = ref == BR_ENTITY_NULL || !br_entity_alive(c->w, ref) ? -1 : ref_index(&c->pos, ref);
    if (at < 0) snprintf(out, size, "none");                       /* not saved with it: dropped */
    else snprintf(out, size, "%ld", at);
}

/* Writes a component's fields: all of them, or (prefab != NULL) only those that differ from it. */
static bool wr_fields(strbuf *b, const wr_ctx *c, const br_component_desc *d, const void *data,
                      const wr_inst *in, const void *prefab, bool header_written) {
    char value[VALUE_MAX], other[VALUE_MAX];
    int i;
    bool header = header_written;
    for (i = 0; i < d->field_count; ++i) {
        const br_field *f = &d->fields[i];
        if (f->type == BR_FIELD_ENTITY) {
            br_entity ref, pref;
            memcpy(&ref, (const unsigned char *)data + f->offset, sizeof(ref));
            if (prefab) {                                        /* the same as the prefab's? (its entity, mapped) */
                long k;
                memcpy(&pref, (const unsigned char *)prefab + f->offset, sizeof(pref));
                k = pref == BR_ENTITY_NULL ? -1 : ref_index(&in->from_prefab, pref);
                if ((pref == BR_ENTITY_NULL && ref == BR_ENTITY_NULL) || (k >= 0 && in->from_prefab.slots && (br_entity)k == ref)) continue;
            }
            wr_ref_value(c, ref, value, sizeof(value));
        } else {
            br_field_format(f, data, value, sizeof(value));
            if (prefab) {
                br_field_format(f, prefab, other, sizeof(other));
                if (strcmp(value, other) == 0) continue;
            }
        }
        if (!header) { sb_puts(b, "  "); sb_puts(b, d->name); sb_puts(b, "\n"); header = true; }
        sb_puts(b, "    ");
        sb_puts(b, f->name);
        sb_puts(b, " = ");
        sb_puts(b, value);
        sb_puts(b, "\n");
    }
    return header;
}

static void write_entity(strbuf *b, const wr_ctx *c, br_entity e) {
    uint32_t k;
    sb_puts(b, "\nentity\n");
    for (k = 0; k < c->w->pool_count; ++k) {
        const br_component_desc *d = c->w->pools[k].desc;
        if (!d || !br_has(c->w, e, k)) continue;
        sb_puts(b, "  ");
        sb_puts(b, d->name);
        sb_puts(b, "\n");
        wr_fields(b, c, d, br_get(c->w, e, k), NULL, NULL, true);
    }
}

/* An instance's root or part: what differs from the prefab's entity `pe` (in in->pf->pw).
 * Returns whether anything was (with `force`, the block is written regardless). */
static bool write_difference(strbuf *b, const wr_ctx *c, const wr_inst *in, br_entity e, br_entity pe,
                             const char *path, long root_pos, bool is_root, bool force) {
    strbuf body;
    uint32_t k;
    bool any = false;
    memset(&body, 0, sizeof(body));
    for (k = 0; k < c->w->pool_count; ++k) {
        const br_component_desc *d = c->w->pools[k].desc;
        br_component pk;
        bool mine, theirs;
        if (!d) continue;
        if (is_root && (strcmp(d->name, "Prefab") == 0 || strcmp(d->name, "Parent") == 0)) {   /* the scene's own: whole */
            if (!br_has(c->w, e, k)) continue;
            sb_puts(&body, "  "); sb_puts(&body, d->name); sb_puts(&body, "\n");
            wr_fields(&body, c, d, br_get(c->w, e, k), NULL, NULL, true);
            any = true;
            continue;
        }
        pk = br_component_find(&in->pf->pw, d->name);
        mine = br_has(c->w, e, k);
        theirs = pk != BR_COMPONENT_INVALID && br_has(&in->pf->pw, pe, pk);
        if (mine && !theirs) {                                   /* added here: all of it */
            sb_puts(&body, "  "); sb_puts(&body, d->name); sb_puts(&body, "\n");
            wr_fields(&body, c, d, br_get(c->w, e, k), NULL, NULL, true);
            any = true;
        } else if (!mine && theirs) {                            /* taken off here */
            sb_puts(&body, "  remove "); sb_puts(&body, d->name); sb_puts(&body, "\n");
            any = true;
        } else if (mine && theirs) {
            if (wr_fields(&body, c, d, br_get(c->w, e, k), in, br_get(&in->pf->pw, pe, pk), false)) any = true;
        }
    }
    if (any || force) {
        char line[700];
        sb_puts(b, "\nentity\n");
        if (!is_root) {                                          /* which part of which instance */
            snprintf(line, sizeof(line), "  PrefabPart\n    instance = %ld\n    path = \"", root_pos);
            sb_puts(b, line);
            {
                const char *p;
                for (p = path; *p; ++p) { if (*p == '"' || *p == '\\') sb_append(b, "\\", 1); sb_append(b, p, 1); }
            }
            sb_puts(b, "\"\n");
        }
        if (body.data) sb_puts(b, body.data);
    }
    br_free(body.data);
    return any;
}

static void wr_inst_free(wr_inst *in) {
    br_free(in->to_prefab.slots);
    br_free(in->from_prefab.slots);
}

static void wr_prefab_free(wr_prefab *pf) {
    if (pf->pmade) br_world_destroy(&pf->pw);
    br_free(pf->pmade);
    br_free(pf->prank);
}

/* The prefab file made in a world of its own (or the one made already). */
static wr_prefab *wr_prefab_get(wr_ctx *c, const char *file) {
    wr_prefab *pf;
    char *text;
    uint32_t i, k;
    int n;
    for (i = 0; i < (uint32_t)c->prefab_count; ++i) if (strcmp(c->prefabs[i].file, file) == 0) return c->prefabs[i].ok ? &c->prefabs[i] : NULL;
    pf = &c->prefabs[c->prefab_count++];
    snprintf(pf->file, sizeof(pf->file), "%s", file);
    if (!(text = sr_read_prefab(file))) { BR_WARN("prefab '%s' can't be read: its instance is saved whole", file); return NULL; }
    br_world_init(&pf->pw);
    for (k = 0; k < c->w->pool_count; ++k)                       /* the same components, described the same */
        if (c->w->pools[k].desc) br_component_register_desc(&pf->pw, c->w->pools[k].desc);
    n = br_world_from_text_list(&pf->pw, text, &pf->pmade, NULL, 0);
    br_free(text);
    if (n < 0 || !pf->pmade) { br_world_destroy(&pf->pw); br_free(pf->pmade); pf->pmade = NULL; return NULL; }
    pf->pcount = (uint32_t)n;
    pf->prank_n = pf->pw.high_water + 1;
    pf->prank = (uint32_t *)br_alloc(sizeof(uint32_t) * pf->prank_n);
    if (!pf->prank) return NULL;
    for (i = 0; i < pf->prank_n; ++i) pf->prank[i] = UINT32_MAX;
    for (i = 0; i < pf->pcount; ++i) pf->prank[BR_ENTITY_INDEX(pf->pmade[i])] = i;
    pf->proot = BR_ENTITY_NULL;
    for (i = 0; i < pf->pcount && pf->proot == BR_ENTITY_NULL; ++i)
        if (br_parent_of(&pf->pw, pf->pmade[i]) == BR_ENTITY_NULL) pf->proot = pf->pmade[i];
    pf->ok = pf->proot != BR_ENTITY_NULL;
    return pf->ok ? pf : NULL;
}

/* Makes the instance's prefab in a world of its own and matches its entities to the scene's. */
static void wr_inst_make(wr_ctx *c, wr_inst *in, uint32_t list_at) {
    const char *file = sr_prefab_file(c->w, in->root);
    char path[512];
    br_entity *stack;
    uint32_t i, sp = 0;
    if (!file || !(in->pf = wr_prefab_get(c, file))) return;
    in->to_prefab.slots = (ref_slot *)br_alloc(sizeof(ref_slot) * (c->n + 1));
    in->from_prefab.slots = (ref_slot *)br_alloc(sizeof(ref_slot) * (c->n + 1));
    stack = (br_entity *)br_alloc(sizeof(br_entity) * (c->n + 1));
    if (!in->to_prefab.slots || !in->from_prefab.slots || !stack) { br_free(stack); return; }
    in->ok = true;
    in->to_prefab.slots[0].e = in->root; in->to_prefab.slots[0].index = in->pf->proot;   /* (index: the handle, here) */
    in->from_prefab.slots[0].e = in->pf->proot; in->from_prefab.slots[0].index = in->root;
    in->to_prefab.count = in->from_prefab.count = 1;
    c->kind[list_at] = 1;
    c->inst_of[list_at] = (int)(in - c->inst);
    stack[sp++] = in->root;                                      /* the scene's side of it: every descendant */
    while (sp) {
        br_entity cur = stack[--sp], kids[256];
        uint32_t count = sr_children(c->w, cur, c->rank, kids, 256);
        for (i = 0; i < count; ++i) {
            long at = list_index(c, kids[i]);
            br_entity pe;
            if (at < 0) continue;
            if (!sr_path_of(c->w, in->root, kids[i], c->rank, path, sizeof(path))) continue;
            pe = sr_find_path(&in->pf->pw, in->pf->proot, path, in->pf->prank);
            if (pe != BR_ENTITY_NULL && ref_index(&in->from_prefab, pe) < 0) {   /* a part */
                in->to_prefab.slots[in->to_prefab.count].e = kids[i];
                in->to_prefab.slots[in->to_prefab.count++].index = pe;
                in->from_prefab.slots[in->from_prefab.count].e = pe;
                in->from_prefab.slots[in->from_prefab.count++].index = kids[i];
                sort_refs(&in->from_prefab);
                c->kind[at] = 2;
                c->inst_of[at] = (int)(in - c->inst);
                if (count && sp < c->n) stack[sp++] = kids[i];
            }                                                    /* (else: added in the scene - written as it is) */
        }
    }
    sort_refs(&in->to_prefab);
    sort_refs(&in->from_prefab);
    br_free(stack);
}

static bool wr_has_prefab_ancestor(const wr_ctx *c, br_entity e) {
    br_entity p = br_parent_of(c->w, e);
    int guard = 0;
    while (p != BR_ENTITY_NULL && guard++ < 1024) {
        if (sr_prefab_file(c->w, p) && list_index(c, p) >= 0) return true;
        p = br_parent_of(c->w, p);
    }
    return false;
}

char *br_world_to_text(const br_world *w, const br_entity *order, uint32_t count, size_t *out_len) {
    strbuf b;
    char header[64];
    uint32_t i, k, n = 0, written = 0;
    wr_ctx c;
    br_entity *list = (br_entity *)br_alloc(sizeof(br_entity) * (order ? count : w->high_water) + 1);
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));
    if (!list) return NULL;
    if (order) {
        for (i = 0; i < count; ++i) if (br_entity_alive(w, order[i])) list[n++] = order[i];
    } else {
        for (i = 0; i < w->high_water; ++i)
            if (w->alive[i]) list[n++] = (br_entity)(((uint32_t)w->generations[i] << BR_ENTITY_INDEX_BITS) | i);
    }
    c.w = w;
    c.list = list;
    c.n = n;
    c.rank_n = w->high_water + 1;
    c.rank = (uint32_t *)br_alloc(sizeof(uint32_t) * c.rank_n);
    c.kind = (uint8_t *)br_calloc(n + 1, 1);
    c.referenced = (uint8_t *)br_calloc(n + 1, 1);
    c.inst_of = (int *)br_alloc(sizeof(int) * (n + 1));
    c.pos.slots = (ref_slot *)br_alloc(sizeof(ref_slot) * (n + 1));
    if (!c.rank || !c.kind || !c.referenced || !c.inst_of || !c.pos.slots) goto done;
    for (i = 0; i < c.rank_n; ++i) c.rank[i] = UINT32_MAX;
    for (i = 0; i < n; ++i) { c.rank[BR_ENTITY_INDEX(list[i])] = i; c.inst_of[i] = -1; }
    /* the instances (outermost ones: an instance inside another is a part of it) */
    for (i = 0; i < n; ++i)
        if (sr_prefab_file(w, list[i]) && !wr_has_prefab_ancestor(&c, list[i])) ++c.inst_count;
    if (c.inst_count) {
        int at = 0;
        c.inst = (wr_inst *)br_calloc((size_t)c.inst_count, sizeof(wr_inst));
        c.prefabs = (wr_prefab *)br_calloc((size_t)c.inst_count, sizeof(wr_prefab));
        if (!c.inst || !c.prefabs) goto done;
        for (i = 0; i < n; ++i)
            if (sr_prefab_file(w, list[i]) && !wr_has_prefab_ancestor(&c, list[i])) {
                c.inst[at].root = list[i];
                wr_inst_make(&c, &c.inst[at], i);
                ++at;
            }
    }
    for (i = 0; i < n; ++i) {                                    /* what's referred to by what will be written (it needs a place in the file) */
        const wr_inst *in = c.kind[i] ? &c.inst[c.inst_of[i]] : NULL;
        br_entity pe = BR_ENTITY_NULL;
        if (in) { long m = ref_index(&in->to_prefab, list[i]); pe = m >= 0 ? (br_entity)m : BR_ENTITY_NULL; }
        for (k = 0; k < w->pool_count; ++k) {
            const br_component_desc *d = w->pools[k].desc;
            const unsigned char *data, *pdata = NULL;
            int f;
            if (!d || !(data = (const unsigned char *)br_get(w, list[i], k))) continue;
            if (in && pe != BR_ENTITY_NULL && !(c.kind[i] == 1 && (strcmp(d->name, "Prefab") == 0 || strcmp(d->name, "Parent") == 0))) {
                br_component pk = br_component_find(&in->pf->pw, d->name);
                pdata = pk != BR_COMPONENT_INVALID ? (const unsigned char *)br_get(&in->pf->pw, pe, pk) : NULL;
            }
            for (f = 0; f < d->field_count; ++f) {
                br_entity ref;
                long at;
                if (d->fields[f].type != BR_FIELD_ENTITY) continue;
                memcpy(&ref, data + d->fields[f].offset, sizeof(ref));
                if (pdata) {                                     /* the prefab's own reference: not written */
                    br_entity pref;
                    long mk;
                    memcpy(&pref, pdata + d->fields[f].offset, sizeof(pref));
                    mk = pref == BR_ENTITY_NULL ? -1 : ref_index(&in->from_prefab, pref);
                    if ((pref == BR_ENTITY_NULL && ref == BR_ENTITY_NULL) || (mk >= 0 && (br_entity)mk == ref)) continue;
                }
                if ((at = list_index(&c, ref)) >= 0) c.referenced[at] = 1;
            }
        }
    }
    /* the blocks' places: plain entities and instance roots in order, each root followed by its parts that need saying */
    {
        bool *needs = (bool *)br_calloc(n + 1, sizeof(bool));
        strbuf parts;
        if (!needs) goto done;
        for (i = 0; i < n; ++i) {
            if (c.kind[i] == 2) {                                 /* a part: does it differ, or is it referred to? */
                wr_inst *in = &c.inst[c.inst_of[i]];
                long pe = ref_index(&in->to_prefab, list[i]);
                strbuf scratch;
                memset(&scratch, 0, sizeof(scratch));
                needs[i] = c.referenced[i] || (pe >= 0 && write_difference(&scratch, &c, in, list[i], (br_entity)pe, "", 0, false, false));
                br_free(scratch.data);
            }
        }
        for (i = 0; i < n; ++i) {
            uint32_t j;
            if (c.kind[i] == 2) continue;
            c.pos.slots[c.pos.count].e = list[i];
            c.pos.slots[c.pos.count++].index = written++;
            if (c.kind[i] != 1) continue;
            for (j = 0; j < n; ++j)
                if (c.kind[j] == 2 && c.inst_of[j] == c.inst_of[i] && needs[j]) {
                    c.pos.slots[c.pos.count].e = list[j];
                    c.pos.slots[c.pos.count++].index = written++;
                }
        }
        sort_refs(&c.pos);
        snprintf(header, sizeof(header), "# Bronto scene\nbronto_scene %d\n", BR_SCENE_FORMAT_VERSION);
        sb_puts(&b, header);
        for (i = 0; i < n; ++i) {
            uint32_t j;
            wr_inst *in;
            long root_pos;
            if (c.kind[i] == 2) continue;
            if (c.kind[i] == 0) { write_entity(&b, &c, list[i]); continue; }
            in = &c.inst[c.inst_of[i]];
            root_pos = ref_index(&c.pos, list[i]);
            write_difference(&b, &c, in, list[i], in->pf->proot, "", root_pos, true, true);
            memset(&parts, 0, sizeof(parts));
            for (j = 0; j < n; ++j) {
                char path[512];
                long pe;
                if (c.kind[j] != 2 || c.inst_of[j] != c.inst_of[i] || !needs[j]) continue;
                pe = ref_index(&in->to_prefab, list[j]);
                sr_path_of(w, list[i], list[j], c.rank, path, sizeof(path));
                write_difference(&b, &c, in, list[j], (br_entity)pe, path, root_pos, false, true);
            }
            for (j = 0; j < in->pf->pcount; ++j) {                   /* the prefab's parts deleted here */
                br_entity pe = in->pf->pmade[j];
                char path[512];
                if (pe == in->pf->proot || ref_index(&in->from_prefab, pe) >= 0) continue;
                if (!sr_path_of(&in->pf->pw, in->pf->proot, pe, in->pf->prank, path, sizeof(path))) continue;
                {   /* (only the topmost of a deleted subtree) */
                    br_entity pp = br_parent_of(&in->pf->pw, pe);
                    if (pp != in->pf->proot && ref_index(&in->from_prefab, pp) < 0) continue;
                }
                {
                    char line[700];
                    const char *p;
                    snprintf(line, sizeof(line), "\nentity\n  PrefabPart\n    instance = %ld\n    path = \"", root_pos);
                    sb_puts(&b, line);
                    for (p = path; *p; ++p) { if (*p == '"' || *p == '\\') sb_append(&b, "\\", 1); sb_append(&b, p, 1); }
                    sb_puts(&b, "\"\n  remove entity\n");
                }
            }
        }
        br_free(needs);
    }
done:
    for (i = 0; c.inst && i < (uint32_t)c.inst_count; ++i) wr_inst_free(&c.inst[i]);
    for (i = 0; c.prefabs && i < (uint32_t)c.prefab_count; ++i) wr_prefab_free(&c.prefabs[i]);
    br_free(c.inst);
    br_free(c.prefabs);
    br_free(c.rank);
    br_free(c.kind);
    br_free(c.referenced);
    br_free(c.inst_of);
    br_free(c.pos.slots);
    br_free(list);
    if (b.failed || !b.data) { br_free(b.data); return NULL; }
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

typedef struct pending_ref { br_entity e; br_component c; uint16_t offset; int32_t at; } pending_ref;

typedef struct scene_reader {
    br_world *w;
    br_entity *made;                /* every entity made (prefabs' too), in order */
    uint32_t made_count, made_cap;
    br_entity *block;               /* per block in this file: its entity (an instance's root, a part, or its own) */
    uint8_t *block_inst;            /* ... 1: an instance root */
    uint32_t block_count, block_cap, inst_cap;
    pending_ref *refs;              /* entity fields from this file: positions, connected at the end */
    uint32_t ref_count, ref_cap;
    uint32_t *rank;                 /* places in `made`, by entity index (for finding parts) */
    uint32_t rank_cap;
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

static bool grow_to(void **p, uint32_t *cap, uint32_t need, size_t item) {
    if (need <= *cap) return true;
    {
        uint32_t c = *cap ? *cap : 64;
        void *g;
        while (c < need) c *= 2;
        if (!(g = br_realloc(*p, (size_t)c * item))) return false;
        *p = g;
        *cap = c;
    }
    return true;
}

static bool remember(scene_reader *r, br_entity e) {
    uint32_t ix = BR_ENTITY_INDEX(e);
    if (!grow_to((void **)&r->made, &r->made_cap, r->made_count + 1, sizeof(br_entity))) return false;
    if (ix >= r->rank_cap) {
        uint32_t old = r->rank_cap, i;
        if (!grow_to((void **)&r->rank, &r->rank_cap, ix + 1, sizeof(uint32_t))) return false;
        for (i = old; i < r->rank_cap; ++i) r->rank[i] = UINT32_MAX;
    }
    r->rank[ix] = r->made_count;
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

/* One line of the file, trimmed (false at the end). */
static bool next_line(const char **pp, char *line, size_t size, bool *too_long) {
    const char *p = *pp;
    size_t n = 0;
    *too_long = false;
    if (!*p) return false;
    while (p[n] && p[n] != '\n') ++n;
    if (n >= size) *too_long = true;
    else { memcpy(line, p, n); line[n] = 0; trim(line); }
    *pp = p + n + (p[n] == '\n');
    return true;
}

static int from_text(br_world *w, const char *text, scene_reader *outer, char *err, size_t err_size, br_entity **list);

/* An entity block's first lines: is it an instance (Prefab file = ...) or a part (PrefabPart ...)? */
typedef struct block_head { bool instance, part, removed; char file[256]; int inst; char path[512]; } block_head;

static void read_head(const char *p, block_head *h) {
    char line[LINE_MAX_LEN];
    bool too_long, in_prefab = false, in_part = false;
    memset(h, 0, sizeof(*h));
    h->inst = -1;
    while (next_line(&p, line, sizeof(line), &too_long)) {
        char *eq;
        if (too_long || !line[0] || line[0] == '#') continue;
        if (strcmp(line, "entity") == 0) break;
        if (strcmp(line, "remove entity") == 0) { h->removed = true; continue; }
        if ((eq = strchr(line, '=')) == NULL) { in_prefab = strcmp(line, "Prefab") == 0; in_part = strcmp(line, "PrefabPart") == 0; continue; }
        *eq = 0;
        trim(line);
        if (in_prefab && strcmp(line, "file") == 0 && parse_string(skip_space(eq + 1), h->file, sizeof(h->file)) && h->file[0]) h->instance = true;
        if (in_part && strcmp(line, "instance") == 0) { h->part = true; parse_int(skip_space(eq + 1), &h->inst); }
        if (in_part && strcmp(line, "path") == 0) parse_string(skip_space(eq + 1), h->path, sizeof(h->path));
    }
}

/* Makes a prefab's entities (its file, read and expanded); returns its root, or BR_ENTITY_NULL. */
static br_entity make_instance(scene_reader *r, const char *file) {
    char *text;
    uint32_t before = r->made_count, i;
    int n, k;
    char err[200];
    br_entity root = BR_ENTITY_NULL;
    for (k = 0; k < g_prefab_depth; ++k)
        if (strcmp(g_prefab_stack[k], file) == 0) { BR_WARN("scene line %d: prefab '%s' contains itself", r->line, file); return BR_ENTITY_NULL; }
    if (g_prefab_depth >= 16) { BR_WARN("scene line %d: prefabs nested too deep", r->line); return BR_ENTITY_NULL; }
    if (!(text = sr_read_prefab(file))) { BR_WARN("scene line %d: can't read prefab '%s' (its instance has only what this scene says)", r->line, file); return BR_ENTITY_NULL; }
    snprintf(g_prefab_stack[g_prefab_depth++], sizeof(g_prefab_stack[0]), "%s", file);
    n = from_text(r->w, text, r, err, sizeof(err), NULL);
    --g_prefab_depth;
    br_free(text);
    if (n < 0) { BR_WARN("scene line %d: prefab '%s': %s", r->line, file, err); return BR_ENTITY_NULL; }
    for (i = before; i < r->made_count && root == BR_ENTITY_NULL; ++i)
        if (br_parent_of(r->w, r->made[i]) == BR_ENTITY_NULL) root = r->made[i];
    return root;
}

/* One pass over the text; returns false on the first error. */
static bool read_all(scene_reader *r, const char *p) {
    char line[LINE_MAX_LEN];
    bool header = false, skipping = false, too_long;
    br_entity cur = BR_ENTITY_NULL;
    br_component comp = BR_COMPONENT_INVALID;
    const br_component_desc *desc = NULL;
    while (next_line(&p, line, sizeof(line), &too_long)) {
        char *eq;
        r->line++;
        if (too_long) return fail(r, "line too long");
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
            block_head h;
            read_head(p, &h);
            cur = BR_ENTITY_NULL;
            if (!grow_to((void **)&r->block, &r->block_cap, r->block_count + 1, sizeof(br_entity)) ||
                !grow_to((void **)&r->block_inst, &r->inst_cap, r->block_count + 1, 1))
                return fail(r, "out of memory");
            if (h.part) {                                        /* a part of an instance earlier in the file */
                if (h.inst >= 0 && (uint32_t)h.inst < r->block_count && r->block_inst[h.inst] && r->block[h.inst] != BR_ENTITY_NULL)
                    cur = sr_find_path(r->w, r->block[h.inst], h.path, r->rank);
                if (cur != BR_ENTITY_NULL && h.removed) {
                    br_entity_destroy(r->w, cur);                /* (and its children: hierarchy.h) */
                    cur = BR_ENTITY_NULL;
                    r->block[r->block_count] = BR_ENTITY_NULL;
                    r->block_inst[r->block_count++] = 0;
                    skipping = true;
                    comp = BR_COMPONENT_INVALID;
                    continue;
                }
                if (cur == BR_ENTITY_NULL && !h.removed)
                    BR_WARN("scene line %d: the prefab has no part '%s' any more (kept as an entity of its own)", r->line, h.path);
            }
            if (h.instance) cur = make_instance(r, h.file);
            if (cur == BR_ENTITY_NULL && h.removed) {            /* (a part that's gone anyway) */
                r->block[r->block_count] = BR_ENTITY_NULL;
                r->block_inst[r->block_count++] = 0;
                skipping = true;
                comp = BR_COMPONENT_INVALID;
                continue;
            }
            if (cur == BR_ENTITY_NULL) {
                cur = br_entity_create(r->w);
                if (cur == BR_ENTITY_NULL || !remember(r, cur)) return fail(r, "out of memory");
            }
            r->block[r->block_count] = cur;
            r->block_inst[r->block_count++] = h.instance ? 1 : 0;
            comp = BR_COMPONENT_INVALID;
            skipping = false;
            continue;
        }
        if (cur == BR_ENTITY_NULL) {
            if (skipping) continue;
            return fail(r, "'%s' is outside an entity", line);
        }
        if (strncmp(line, "remove ", 7) == 0) {                  /* a prefab's component taken off here */
            br_component rc = br_component_find(r->w, skip_space(line + 7));
            if (rc != BR_COMPONENT_INVALID) br_remove(r->w, cur, rc);
            comp = BR_COMPONENT_INVALID;
            skipping = true;
            continue;
        }
        if ((eq = strchr(line, '=')) != NULL) {                 /* field = value */
            const br_field *f;
            void *data;
            *eq = 0;
            trim(line);
            if (skipping) continue;
            if (comp == BR_COMPONENT_INVALID) return fail(r, "field '%s' comes before any component", line);
            f = find_field(desc, line);
            if (!f) { BR_WARN("scene line %d: %s has no field '%s' (ignored)", r->line, desc->name, line); continue; }
            data = br_get(r->w, cur, comp);
            if (f->type == BR_FIELD_ENTITY) {                    /* a position in this file, connected at the end */
                const char *v = skip_space(eq + 1);
                int at = -1;
                br_entity none = BR_ENTITY_NULL;
                if (!(strncmp(v, "none", 4) == 0 && at_end(v + 4))) {
                    if (!parse_int(v, &at) || at < 0) return fail(r, "bad value for %s.%s: %s (expected an entity number or none)", desc->name, f->name, v);
                }
                memcpy((unsigned char *)data + f->offset, &none, sizeof(none));
                if (at >= 0) {
                    if (!grow_to((void **)&r->refs, &r->ref_cap, r->ref_count + 1, sizeof(pending_ref))) return fail(r, "out of memory");
                    r->refs[r->ref_count].e = cur;
                    r->refs[r->ref_count].c = comp;
                    r->refs[r->ref_count].offset = (uint16_t)f->offset;
                    r->refs[r->ref_count++].at = at;
                }
                continue;
            }
            if (!br_field_parse(f, data, eq + 1)) return fail(r, "bad value for %s.%s: %s", desc->name, f->name, skip_space(eq + 1));
            continue;
        }
        if (strcmp(line, "PrefabPart") == 0) { comp = BR_COMPONENT_INVALID; skipping = true; continue; }   /* (read already) */
        comp = br_component_find(r->w, line);                     /* a component */
        desc = br_component_get_desc(r->w, comp);
        if (comp == BR_COMPONENT_INVALID || !desc) {
            BR_WARN("scene line %d: unknown component '%s' (skipped)", r->line, line);
            comp = BR_COMPONENT_INVALID;
            skipping = true;
            continue;
        }
        skipping = false;
        if (!br_has(r->w, cur, comp) && !br_add(r->w, cur, comp, NULL)) return fail(r, "could not add %s", line);
    }
    if (!header) return fail(r, "not a Bronto scene (empty)");
    return true;
}

/* Reads `text` into the world; with `outer`, the entities it makes are added to
 * the outer reader's list (a prefab being expanded inside a scene). */
static int from_text(br_world *w, const char *text, scene_reader *outer, char *err, size_t err_size, br_entity **list) {
    scene_reader r;
    uint32_t i;
    memset(&r, 0, sizeof(r));
    r.w = w;
    r.err = err;
    r.err_size = err_size;
    if (err && err_size) err[0] = 0;
    if (outer) {                                                 /* (shares the outer list: one list of everything made) */
        r.made = outer->made; r.made_count = outer->made_count; r.made_cap = outer->made_cap;
        r.rank = outer->rank; r.rank_cap = outer->rank_cap;
    }
    if (!read_all(&r, text ? text : "")) {
        uint32_t from = outer ? outer->made_count : 0;
        for (i = from; i < r.made_count; ++i) if (br_entity_alive(w, r.made[i])) br_entity_destroy(w, r.made[i]);   /* all or nothing */
        if (outer) { outer->made = r.made; outer->made_cap = r.made_cap; outer->rank = r.rank; outer->rank_cap = r.rank_cap; }
        else { br_free(r.made); br_free(r.rank); }
        br_free(r.block); br_free(r.block_inst); br_free(r.refs);
        return -1;
    }
    for (i = 0; i < r.ref_count; ++i) {                          /* entity references: positions -> handles */
        const pending_ref *pr = &r.refs[i];
        br_entity to = pr->at >= 0 && (uint32_t)pr->at < r.block_count ? r.block[pr->at] : BR_ENTITY_NULL;
        unsigned char *data = br_entity_alive(w, pr->e) ? (unsigned char *)br_get(w, pr->e, pr->c) : NULL;
        if (to != BR_ENTITY_NULL && !br_entity_alive(w, to)) to = BR_ENTITY_NULL;
        if (data) memcpy(data + pr->offset, &to, sizeof(to));
    }
    {
        int n = (int)(r.made_count - (outer ? outer->made_count : 0));
        if (outer) { outer->made = r.made; outer->made_count = r.made_count; outer->made_cap = r.made_cap; outer->rank = r.rank; outer->rank_cap = r.rank_cap; }
        else { br_free(r.rank); }
        br_free(r.block); br_free(r.block_inst); br_free(r.refs);
        if (!outer) {                                            /* hand the list over (the live ones: parts can be removed) */
            uint32_t k = 0;
            for (i = 0; i < r.made_count; ++i) if (br_entity_alive(w, r.made[i])) r.made[k++] = r.made[i];
            r.made_count = k;
            if (list) *list = r.made;
            else br_free(r.made);
            return (int)k;
        }
        return n;
    }
}

int br_world_from_text_list(br_world *w, const char *text, br_entity **out, char *err, size_t err_size) {
    int n;
    if (out) *out = NULL;
    n = from_text(w, text, NULL, err, err_size, out);
    return n;
}

int br_world_from_text(br_world *w, const char *text, br_entity *out, uint32_t cap, char *err, size_t err_size) {
    br_entity *all = NULL;
    int n = br_world_from_text_list(w, text, &all, err, err_size), i;
    for (i = 0; out && i < n && (uint32_t)i < cap; ++i) out[i] = all[i];
    br_free(all);
    return n;
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
