/*
 * Bronto Engine - a game's own components, declared in its scripts (script.h).
 *
 *   component Health {
 *       var hp: int = 3
 *       var regen: number = 0.5
 *       var shield: bool
 *       var team: enum(red, blue) = blue
 *   }
 *
 * The declarations are read from every .bs file of the project (in its pack,
 * or its folder) into a list of component descriptions shared by every world
 * made after that (br_builtin_register adds them), so scene files save them,
 * the editor's inspector edits them and scripts use them like the built-in
 * ones (self.Health.hp). Old descriptions are kept until the engine shuts
 * down: worlds made earlier may still use them.
 */
#include "bronto/script.h"
#include "bronto/serialize.h"
#include "bronto/pack.h"
#include "bronto/fs.h"
#include "bronto/assets.h"
#include "bronto/memory.h"
#include "bronto/log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GC_MAX 64                     /* game components at once */
#define GC_FIELDS 32
#define GC_STRING 64                  /* bytes of a string field */

typedef struct gc_comp {
    br_component_desc desc;
    br_field fields[GC_FIELDS];
    char name[32];
    char field_names[GC_FIELDS][32];
    const char **enum_names[GC_FIELDS];
    unsigned char *defaults;
    char file[128];
    int line;
} gc_comp;

static struct {
    gc_comp *live[GC_MAX];            /* the current set */
    int count;
    gc_comp **old;                    /* replaced ones, kept for worlds that still use them */
    int old_count, old_cap;
    uint32_t signature;
} GCOMP;

static void gc_retire(gc_comp *c) {
    if (GCOMP.old_count == GCOMP.old_cap) {
        int cap = GCOMP.old_cap ? GCOMP.old_cap * 2 : 16;
        gc_comp **g = (gc_comp **)br_realloc(GCOMP.old, sizeof(gc_comp *) * (size_t)cap);
        if (!g) return;                                            /* (kept on: a small leak, not a crash) */
        GCOMP.old = g;
        GCOMP.old_cap = cap;
    }
    GCOMP.old[GCOMP.old_count++] = c;
}

static void gc_free(gc_comp *c) {
    int i, k;
    for (i = 0; i < GC_FIELDS; ++i) {
        if (!c->enum_names[i]) continue;
        for (k = 0; c->enum_names[i][k]; ++k) br_free((void *)c->enum_names[i][k]);
        br_free(c->enum_names[i]);
    }
    br_free(c->defaults);
    br_free(c);
}

void br_script_components_clear(void) {
    int i;
    for (i = 0; i < GCOMP.count; ++i) gc_retire(GCOMP.live[i]);
    GCOMP.count = 0;
    GCOMP.signature = 0;
}

void br_script_components_release(void) {
    int i;
    for (i = 0; i < GCOMP.count; ++i) gc_free(GCOMP.live[i]);
    for (i = 0; i < GCOMP.old_count; ++i) gc_free(GCOMP.old[i]);
    br_free(GCOMP.old);
    memset(&GCOMP, 0, sizeof(GCOMP));
}

int br_script_component_count(void) { return GCOMP.count; }
const br_component_desc *br_script_component(int i) { return i >= 0 && i < GCOMP.count ? &GCOMP.live[i]->desc : NULL; }
uint32_t br_script_components_signature(void) { return GCOMP.signature; }

static uint32_t gc_hash(uint32_t h, const char *s) {
    for (; *s; ++s) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

/* ---- reading declarations ---- */

typedef struct gc_reader { const char *file; char *err; size_t err_size; int errors; } gc_reader;

static void gc_error(gc_reader *r, int line, const char *msg, const char *what) {
    char text[460], body[300];
    snprintf(body, sizeof(body), msg, what ? what : "");
    snprintf(text, sizeof(text), "%.120s:%d: %.300s", r->file, line, body);
    BR_ERROR("component: %s", text);
    if (r->err && r->err_size && !r->errors) snprintf(r->err, r->err_size, "%s", text);
    r->errors++;
}

static const char *gc_skip(const char *p) { while (*p == ' ' || *p == '\t') ++p; return p; }
static bool gc_word(const char **pp, char *out, size_t size) {
    const char *p = gc_skip(*pp);
    size_t n = 0;
    if (!(isalpha((unsigned char)*p) || *p == '_')) return false;
    while ((isalnum((unsigned char)*p) || *p == '_') && n + 1 < size) out[n++] = *p++;
    out[n] = 0;
    *pp = p;
    return true;
}

/* A default written as script would have it, as scene-file text for its field. */
static bool gc_default_text(const char *v, br_field_type type, char *out, size_t size) {
    char tmp[256];
    size_t n = strlen(v);
    while (n && (v[n - 1] == ' ' || v[n - 1] == '\t' || v[n - 1] == '\r')) --n;
    if (n >= sizeof(tmp)) return false;
    memcpy(tmp, v, n);
    tmp[n] = 0;
    if ((type == BR_FIELD_VEC2 && strncmp(tmp, "vec2(", 5) == 0) || (type == BR_FIELD_VEC3 && strncmp(tmp, "vec3(", 5) == 0) ||
        (type == BR_FIELD_COLOR && strncmp(tmp, "rgb(", 4) == 0)) {
        char *open = strchr(tmp, '('), *close = strrchr(tmp, ')'), *q;
        double nums[4] = { 0, 0, 0, 255 };
        int k = 0;
        if (!open || !close) return false;
        *close = 0;
        for (q = open + 1; *q && k < 4; ++k) {
            char *end;
            nums[k] = strtod(q, &end);
            if (end == q) return false;
            q = end;
            while (*q == ' ' || *q == ',') ++q;
        }
        if (type == BR_FIELD_VEC2) snprintf(out, size, "%g %g", nums[0], nums[1]);
        else if (type == BR_FIELD_VEC3) snprintf(out, size, "%g %g %g", nums[0], nums[1], nums[2]);
        else snprintf(out, size, "#%02x%02x%02x%02x", (int)nums[0] & 255, (int)nums[1] & 255, (int)nums[2] & 255, (int)nums[3] & 255);
        return true;
    }
    if (type == BR_FIELD_COLOR && tmp[0] == '"') {                    /* "#rrggbb" */
        size_t len = strlen(tmp);
        if (len < 2 || tmp[len - 1] != '"') return false;
        tmp[len - 1] = 0;
        if (strlen(tmp + 1) + 3 > size) return false;
        strcpy(out, tmp + 1);
        if (strlen(out) == 7) strcat(out, "ff");
        return true;
    }
    if (type == BR_FIELD_ENUM && tmp[0] == '"') {
        size_t len = strlen(tmp);
        if (len < 2) return false;
        tmp[len - 1] = 0;
        snprintf(out, size, "%s", tmp + 1);
        return true;
    }
    snprintf(out, size, "%s", tmp);
    return true;
}

/* `component Name { ... }` blocks in one script's text: each added (or replacing one of the same name). */
int br_script_scan_components(const char *source, const char *file, char *err, size_t err_size) {
    gc_reader r;
    const char *p = source;
    int line = 0, made = 0;
    memset(&r, 0, sizeof(r));
    r.file = file ? file : "script";
    r.err = err;
    r.err_size = err_size;
    if (err && err_size) err[0] = 0;
    while (p && *p) {
        char text[512], word[64];
        const char *q;
        size_t n = 0;
        gc_comp *c;
        int start_line;
        while (p[n] && p[n] != '\n') ++n;
        ++line;
        if (n >= sizeof(text)) n = sizeof(text) - 1;
        memcpy(text, p, n);
        text[n] = 0;
        p += n + (p[n] == '\n');
        q = text;
        if (!gc_word(&q, word, sizeof(word)) || strcmp(word, "component") != 0) continue;
        start_line = line;
        if (!gc_word(&q, word, sizeof(word))) { gc_error(&r, line, "component needs a name: component Health {", NULL); continue; }
        if (!isupper((unsigned char)word[0])) { gc_error(&r, line, "a component's name starts with a capital letter ('%s')", word); continue; }
        if (strlen(word) >= sizeof(((gc_comp *)0)->name)) { gc_error(&r, line, "'%s' is too long a name", word); continue; }
        if (!(c = (gc_comp *)br_calloc(1, sizeof(gc_comp)))) return -1;
        snprintf(c->name, sizeof(c->name), "%s", word);
        snprintf(c->file, sizeof(c->file), "%s", r.file);
        c->line = start_line;
        q = gc_skip(q);
        if (*q != '{') {                                         /* ({ on the next line) */
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') { if (*p == '\n') ++line; ++p; }
            if (*p != '{') { gc_error(&r, start_line, "component %s: '{' expected", c->name); gc_free(c); continue; }
            ++p;
        }
        {   /* the fields, one per line, up to '}' */
            uint32_t offset = 0;
            bool closed = false, bad = false;
            char defaults_text[GC_FIELDS][256];
            int i;
            while (*p && !closed) {
                char ln[512], fname[32], tname[32];
                const char *s;
                br_field *f;
                n = 0;
                while (p[n] && p[n] != '\n') ++n;
                ++line;
                if (n >= sizeof(ln)) n = sizeof(ln) - 1;
                memcpy(ln, p, n);
                ln[n] = 0;
                p += n + (p[n] == '\n');
                {   /* (a comment ends the line, outside quotes) */
                    char *h;
                    bool quoted = false;
                    for (h = ln; *h; ++h) {
                        if (*h == '"') quoted = !quoted;
                        else if (*h == '#' && !quoted) { *h = 0; break; }
                    }
                }
                s = gc_skip(ln);
                if (!*s || *s == '\r') continue;
                if (*s == '}') { closed = true; break; }
                if (!gc_word(&s, fname, sizeof(fname)) || strcmp(fname, "var") != 0) { gc_error(&r, line, "in a component: var name: type = value", NULL); bad = true; continue; }
                if (!gc_word(&s, fname, sizeof(fname))) { gc_error(&r, line, "var needs a name", NULL); bad = true; continue; }
                s = gc_skip(s);
                if (*s != ':') { gc_error(&r, line, "'%s' needs a type (var hp: int = 3): component fields are saved in scenes", fname); bad = true; continue; }
                ++s;
                if (!gc_word(&s, tname, sizeof(tname))) { gc_error(&r, line, "'%s' needs a type", fname); bad = true; continue; }
                if (c->desc.field_count == GC_FIELDS) { gc_error(&r, line, "too many fields (32 at most)", NULL); bad = true; break; }
                i = c->desc.field_count;
                f = &c->fields[i];
                snprintf(c->field_names[i], sizeof(c->field_names[i]), "%s", fname);
                f->name = c->field_names[i];
                if (strcmp(tname, "int") == 0) { f->type = BR_FIELD_INT; f->size = 4; }
                else if (strcmp(tname, "number") == 0 || strcmp(tname, "float") == 0) { f->type = BR_FIELD_FLOAT; f->size = 4; }
                else if (strcmp(tname, "bool") == 0) { f->type = BR_FIELD_BOOL; f->size = sizeof(bool); }
                else if (strcmp(tname, "string") == 0) { f->type = BR_FIELD_STRING; f->size = GC_STRING; }
                else if (strcmp(tname, "vec2") == 0) { f->type = BR_FIELD_VEC2; f->size = sizeof(br_vec2); }
                else if (strcmp(tname, "vec3") == 0) { f->type = BR_FIELD_VEC3; f->size = sizeof(br_vec3); }
                else if (strcmp(tname, "color") == 0) { f->type = BR_FIELD_COLOR; f->size = 4; }
                else if (strcmp(tname, "entity") == 0) { f->type = BR_FIELD_ENTITY; f->size = sizeof(br_entity); }
                else if (strcmp(tname, "enum") == 0) {                /* enum(a, b, c) */
                    const char **names;
                    int k = 0;
                    s = gc_skip(s);
                    if (*s != '(') { gc_error(&r, line, "enum lists its choices: enum(idle, chase, flee)", NULL); bad = true; continue; }
                    ++s;
                    names = (const char **)br_calloc(33, sizeof(char *));
                    if (!names) return -1;
                    while (k < 32) {
                        char choice[48];
                        if (!gc_word(&s, choice, sizeof(choice))) break;
                        names[k++] = br_strdup(choice);
                        s = gc_skip(s);
                        if (*s == ',') ++s;
                    }
                    s = gc_skip(s);
                    if (*s != ')' || k == 0) { gc_error(&r, line, "enum(...) needs its choices, then ')'", NULL); for (i = 0; names[i]; ++i) br_free((void *)names[i]); br_free(names); bad = true; continue; }
                    ++s;
                    c->enum_names[c->desc.field_count] = names;
                    f->type = BR_FIELD_ENUM;
                    f->size = 4;
                    f->enum_names = names;
                    f->enum_count = k;
                } else { gc_error(&r, line, "a component field can't be a '%s' (int, number, bool, string, vec2, vec3, color, entity or enum(...))", tname); bad = true; continue; }
                offset = (offset + 3u) & ~3u;
                f->offset = offset;
                offset += (f->size + 3u) & ~3u;
                s = gc_skip(s);
                defaults_text[c->desc.field_count][0] = 0;
                if (*s == '=') {
                    if (f->type == BR_FIELD_ENTITY) { gc_error(&r, line, "an entity field starts as none: it can't have a value here", NULL); bad = true; continue; }
                    if (!gc_default_text(gc_skip(s + 1), f->type, defaults_text[c->desc.field_count], sizeof(defaults_text[0]))) {
                        gc_error(&r, line, "can't read the value of '%s'", fname); bad = true; continue;
                    }
                } else if (*s && *s != '\r') { gc_error(&r, line, "after the type: = value (or nothing)", NULL); bad = true; continue; }
                c->desc.field_count++;
            }
            if (!closed) { gc_error(&r, start_line, "component %s is never closed with '}'", c->name); bad = true; }
            if (bad) { gc_free(c); continue; }
            c->desc.name = c->name;
            c->desc.fields = c->fields;
            c->desc.size = offset ? offset : 4;
            if (!(c->defaults = (unsigned char *)br_calloc(1, c->desc.size))) { gc_free(c); return -1; }
            for (i = 0; i < c->desc.field_count; ++i)
                if (defaults_text[i][0] && !br_field_parse(&c->fields[i], c->defaults, defaults_text[i])) {
                    gc_error(&r, c->line, "the starting value of '%s' doesn't fit its type", c->fields[i].name);
                    bad = true;
                }
            if (bad) { gc_free(c); continue; }
            c->desc.defaults = c->defaults;
        }
        {   /* in the set (a component of the same name is replaced) */
            int k;
            for (k = 0; k < GCOMP.count; ++k)
                if (strcmp(GCOMP.live[k]->name, c->name) == 0) {
                    if (strcmp(GCOMP.live[k]->file, c->file) != 0) BR_WARN("component %s is declared in %s and in %s: the second is used", c->name, GCOMP.live[k]->file, c->file);
                    gc_retire(GCOMP.live[k]);
                    GCOMP.live[k] = GCOMP.live[--GCOMP.count];
                    break;
                }
            if (GCOMP.count == GC_MAX) { gc_error(&r, c->line, "too many components (64 at most)", NULL); gc_free(c); continue; }
            GCOMP.live[GCOMP.count++] = c;
            ++made;
        }
        {   /* what they are, as one number (the editor notices a change) */
            int k, i;
            uint32_t h = 2166136261u;
            for (k = 0; k < GCOMP.count; ++k) {
                h = gc_hash(h, GCOMP.live[k]->name);
                for (i = 0; i < GCOMP.live[k]->desc.field_count; ++i) {
                    char d[256];
                    h = gc_hash(h, GCOMP.live[k]->fields[i].name);
                    h = (h ^ (uint32_t)GCOMP.live[k]->fields[i].type) * 16777619u;
                    br_field_format(&GCOMP.live[k]->fields[i], GCOMP.live[k]->defaults, d, sizeof(d));
                    h = gc_hash(h, d);
                }
            }
            GCOMP.signature = h;
        }
    }
    return r.errors ? -1 : made;
}

/* ---- finding them in a project ---- */

static int gc_walk(const char *dir, const char *rel, int depth, const br_project *proj, char *err, size_t err_size, int *errors) {
    br_dir_list l;
    int i, made = 0;
    if (depth > 12 || !br_fs_list(dir, false, &l)) return 0;
    for (i = 0; i < l.count; ++i) {
        const br_dir_entry *e = &l.entries[i];
        char full[BR_PATH_MAX], r2[BR_PATH_MAX];
        if (e->name[0] == '.') continue;
        br_path_join(full, sizeof(full), dir, e->name);
        if (rel[0]) snprintf(r2, sizeof(r2), "%s/%s", rel, e->name);
        else snprintf(r2, sizeof(r2), "%s", e->name);
        if (proj && br_project_excludes(proj, r2)) continue;
        if (e->is_dir) made += gc_walk(full, r2, depth + 1, proj, err, err_size, errors);
        else if (br_path_has_ext(e->name, ".bs")) {
            size_t size;
            char *text = (char *)br_file_read_all(full, &size);
            if (text && strstr(text, "component")) {
                int n = br_script_scan_components(text, r2, *errors ? NULL : err, err_size);
                if (n < 0) ++*errors; else made += n;
            }
            br_free(text);
        }
    }
    br_dir_list_free(&l);
    return made;
}

int br_script_components_from_project(const char *root, char *err, size_t err_size) {
    int made = 0, errors = 0;
    uint32_t i;
    br_script_components_clear();
    if (err && err_size) err[0] = 0;
    if (br_pack_mounted()) {                                      /* a built game: its pack */
        for (i = 0; i < br_pack_file_count(); ++i) {
            const char *name = br_pack_file_name(i);
            size_t size;
            char *text;
            if (!br_path_has_ext(name, ".bs") || !(text = (char *)br_pack_read(name, &size))) continue;
            if (strstr(text, "component")) {
                int n = br_script_scan_components(text, name, errors ? NULL : err, err_size);
                if (n < 0) ++errors; else made += n;
            }
            br_free(text);
        }
        return errors ? -1 : made;
    }
    if (root && root[0]) {
        br_project proj;
        bool have = br_project_load(root, &proj);
        made = gc_walk(root, "", 0, have ? &proj : NULL, err, err_size, &errors);
    }
    return errors ? -1 : made;
}

void br_script_register_components(br_world *w) {
    int i;
    for (i = 0; i < GCOMP.count; ++i) {
        if (br_component_find(w, GCOMP.live[i]->name) != BR_COMPONENT_INVALID) {   /* (a built-in one has this name) */
            BR_WARN("component %s (%s) has the name of one the engine has: left out", GCOMP.live[i]->name, GCOMP.live[i]->file);
            continue;
        }
        br_component_register_desc(w, &GCOMP.live[i]->desc);
    }
}
