/*
 * BRONTO EDITOR - the document: the scene being edited.
 *
 * Everything here is plain data manipulation on an ECS world, with no
 * drawing and no input, so it is unit tested (tests/test_editor_doc.c).
 *
 * Undo, redo and play/stop all work the same way: the scene is saved to
 * text (the engine's scene format) and loaded back. That is simple and
 * exact, and scenes are small enough for it to be instant.
 */
#include "editor.h"
#include <stdio.h>
#include <string.h>

/* ---- the hierarchy order ------------------------------------------------- */

static bool reserve(ed_doc *d, uint32_t n) {
    if (n <= d->cap) return true;
    {
        uint32_t cap = d->cap ? d->cap : 64;
        br_entity *grown;
        while (cap < n) cap *= 2;
        grown = (br_entity *)br_realloc(d->order, cap * sizeof(br_entity));
        if (!grown) return false;
        d->order = grown;
        d->cap = cap;
    }
    return true;
}

static void insert_at(ed_doc *d, uint32_t at, br_entity e) {
    if (!reserve(d, d->count + 1)) return;
    memmove(d->order + at + 1, d->order + at, (d->count - at) * sizeof(br_entity));
    d->order[at] = e;
    d->count++;
}

int ed_doc_index(const ed_doc *d, br_entity e) {
    uint32_t i;
    for (i = 0; i < d->count; ++i) if (d->order[i] == e) return (int)i;
    return -1;
}

/* ---- snapshots -------------------------------------------------------------- */

static char *snapshot(const ed_doc *d) { return br_world_to_text(&d->world, d->order, d->count, NULL); }

static void set_text(char **slot, char *text) { br_free(*slot); *slot = text; }

static void clear_stack(char **stack, int *count) {
    while (*count > 0) br_free(stack[--*count]);
}

static void push(char **stack, int *count, char *text) {
    if (*count == ED_UNDO_MAX) {                 /* full: forget the oldest step */
        br_free(stack[0]);
        memmove(stack, stack + 1, (ED_UNDO_MAX - 1) * sizeof(char *));
        --*count;
    }
    stack[(*count)++] = text;
}

/* Replace the world's contents with `text`; keeps the selection by position. */
static bool restore(ed_doc *d, const char *text, int select_index) {
    const char *p = text;
    uint32_t guess = 1;
    int n;
    while ((p = strstr(p, "entity")) != NULL) { guess++; p += 6; }   /* at most this many */
    if (!reserve(d, guess)) return false;
    br_world_clear(&d->world);
    n = br_world_from_text(&d->world, text, d->order, d->cap, d->error, sizeof(d->error));
    d->count = n > 0 ? (uint32_t)n : 0;
    d->selected = select_index >= 0 && (uint32_t)select_index < d->count ? d->order[select_index] : BR_ENTITY_NULL;
    return n >= 0;
}

/* ---- lifetime ------------------------------------------------------------------ */

void ed_doc_init(ed_doc *d) {
    memset(d, 0, sizeof(*d));
    br_world_init(&d->world);
    br_builtin_register(&d->world, &d->ids);
    ed_doc_new(d);
}

static void forget_history(ed_doc *d) {
    clear_stack(d->undo, &d->undo_count);
    clear_stack(d->redo, &d->redo_count);
    set_text(&d->play_snapshot, NULL);
    d->playing = false;
}

void ed_doc_destroy(ed_doc *d) {
    forget_history(d);
    set_text(&d->state, NULL);
    set_text(&d->saved, NULL);
    br_free(d->order);
    br_world_destroy(&d->world);
    memset(d, 0, sizeof(*d));
}

void ed_doc_new(ed_doc *d) {
    forget_history(d);
    br_world_clear(&d->world);
    d->count = 0;
    d->selected = BR_ENTITY_NULL;
    d->path[0] = 0;
    set_text(&d->state, snapshot(d));
    set_text(&d->saved, br_strdup(d->state));
}

void ed_doc_mark_clean(ed_doc *d) {
    forget_history(d);
    set_text(&d->state, snapshot(d));
    set_text(&d->saved, br_strdup(d->state));
}

/* Asset paths are relative to the project folder. */
static void set_asset_root(const ed_doc *d, const char *scene_path) {
    char dir[ED_PATH_MAX];
    if (d->has_root) snprintf(dir, sizeof(dir), "%s", d->root);
    else br_assets_find_root(scene_path, dir, sizeof(dir));
    br_assets_set_root(dir);
}

void ed_doc_set_root(ed_doc *d, const char *root) {
    snprintf(d->root, sizeof(d->root), "%s", root ? root : "");
    d->has_root = true;
    br_assets_set_root(d->root);
}

bool ed_doc_load(ed_doc *d, const char *path) {
    size_t size;
    char *text = (char *)br_file_read_all(path, &size);
    if (!text) { snprintf(d->error, sizeof(d->error), "can't read '%s'", path); return false; }
    {   /* check it parses before touching the open scene */
        br_world probe;
        int n;
        br_world_init(&probe);
        br_builtin_register(&probe, NULL);
        n = br_world_from_text(&probe, text, NULL, 0, d->error, sizeof(d->error));
        br_world_destroy(&probe);
        if (n < 0) { br_free(text); return false; }
    }
    forget_history(d);
    br_assets_clear();                             /* reload what the new scene uses */
    set_asset_root(d, path);
    restore(d, text, 0);
    br_free(text);
    snprintf(d->path, sizeof(d->path), "%s", path);
    set_text(&d->state, snapshot(d));
    set_text(&d->saved, br_strdup(d->state));
    return true;
}

bool ed_doc_save(ed_doc *d, const char *path) {
    char *text;
    size_t len;
    if (d->playing) { snprintf(d->error, sizeof(d->error), "stop playing before saving"); return false; }
    ed_doc_checkpoint(d);
    text = br_world_to_text(&d->world, d->order, d->count, &len);
    if (!text || !br_file_write_all(path, text, len)) {
        snprintf(d->error, sizeof(d->error), "can't write '%s'", path);
        br_free(text);
        return false;
    }
    snprintf(d->path, sizeof(d->path), "%s", path);
    set_asset_root(d, path);
    set_text(&d->saved, text);
    return true;
}

bool ed_doc_dirty(const ed_doc *d) { return d->state && d->saved && strcmp(d->state, d->saved) != 0; }

/* ---- editing ------------------------------------------------------------------------ */

br_entity ed_doc_create(ed_doc *d, const char *name, br_vec2 pos) {
    br_entity e = br_entity_create(&d->world);
    if (e == BR_ENTITY_NULL) return e;
    snprintf(((br_name *)br_add(&d->world, e, d->ids.name, NULL))->text, sizeof(((br_name *)0)->text), "%s", name);
    ((br_transform *)br_add(&d->world, e, d->ids.transform, NULL))->position = pos;
    insert_at(d, d->count, e);
    d->selected = e;
    return e;
}

br_entity ed_doc_duplicate(ed_doc *d, br_entity e) {
    int at = ed_doc_index(d, e);
    br_entity c;
    br_name *n;
    br_transform *t;
    if (at < 0 || (c = br_entity_clone(&d->world, e)) == BR_ENTITY_NULL) return BR_ENTITY_NULL;
    if ((n = (br_name *)br_get(&d->world, c, d->ids.name)) != NULL) {
        char base[sizeof(n->text)];
        snprintf(base, sizeof(base), "%s", n->text);
        snprintf(n->text, sizeof(n->text), "%.26s copy", base);
    }
    if ((t = (br_transform *)br_get(&d->world, c, d->ids.transform)) != NULL)
        t->position = br_v2_add(t->position, br_v2(8, 8));       /* visibly not on top */
    insert_at(d, (uint32_t)at + 1, c);
    d->selected = c;
    return c;
}

void ed_doc_delete(ed_doc *d, br_entity e) {
    int at = ed_doc_index(d, e);
    if (at < 0) return;
    memmove(d->order + at, d->order + at + 1, (d->count - (uint32_t)at - 1) * sizeof(br_entity));
    d->count--;
    br_entity_destroy(&d->world, e);
    if (d->selected == e)   /* select the neighbour, so Delete can be pressed repeatedly */
        d->selected = d->count ? d->order[(uint32_t)at < d->count ? (uint32_t)at : d->count - 1] : BR_ENTITY_NULL;
}

void ed_doc_reorder(ed_doc *d, br_entity e, int delta) {
    int at = ed_doc_index(d, e), to = at + delta;
    if (at < 0 || to < 0 || to >= (int)d->count) return;
    d->order[at] = d->order[to];
    d->order[to] = e;
}

void ed_doc_move(ed_doc *d, br_entity e, int slot) {
    int at = ed_doc_index(d, e);
    if (at < 0 || slot < 0 || slot > (int)d->count) return;
    if (slot > at) slot--;                       /* the gap it leaves shifts the rest up */
    memmove(d->order + at, d->order + at + 1, (d->count - (uint32_t)at - 1) * sizeof(br_entity));
    memmove(d->order + slot + 1, d->order + slot, (d->count - 1 - (uint32_t)slot) * sizeof(br_entity));
    d->order[slot] = e;
}

const char *ed_doc_name(const ed_doc *d, br_entity e, char *buf, size_t size) {
    const br_name *n = (const br_name *)br_get(&d->world, e, d->ids.name);
    if (n && n->text[0]) snprintf(buf, size, "%s", n->text);
    else snprintf(buf, size, "Entity %u", BR_ENTITY_INDEX(e));
    return buf;
}

/* The same order as drawing: highest layer, then the latest entity. */
br_entity ed_doc_pick(const ed_doc *d, br_vec2 world) {
    br_entity best = BR_ENTITY_NULL;
    int best_layer = -1000000;
    uint32_t i;
    for (i = 0; i < d->count; ++i) {
        br_entity e = d->order[i];
        const br_shape *s = (const br_shape *)br_get(&d->world, e, d->ids.shape);
        const br_sprite *sp = (const br_sprite *)br_get(&d->world, e, d->ids.sprite);
        const br_label *l = (const br_label *)br_get(&d->world, e, d->ids.label);
        int layer = -1000000;
        if (!br_builtin_hit(&d->world, e, world)) continue;
        if (s) layer = BR_MAX(layer, s->layer);
        if (sp) layer = BR_MAX(layer, sp->layer);
        if (l) layer = BR_MAX(layer, l->layer);
        if (!s && !sp && !l) layer = 1000;                     /* invisible: its handle is on top */
        if (layer > best_layer || (layer == best_layer && BR_ENTITY_INDEX(e) > BR_ENTITY_INDEX(best))) {
            best = e;
            best_layer = layer;
        }
    }
    return best;
}

/* ---- prefabs ------------------------------------------------------------------------ */

int ed_doc_count_entities(const char *path) {
    br_world probe;
    int n;
    br_world_init(&probe);
    br_builtin_register(&probe, NULL);
    n = br_world_load_file(&probe, path, NULL, 0, NULL, 0);
    br_world_destroy(&probe);
    return n;
}

int ed_doc_instantiate(ed_doc *d, const char *path, br_vec2 at) {
    size_t size;
    char *text;
    br_entity *made;
    br_vec2 sum = br_v2(0, 0);
    int n, i, placed = 0;
    if (d->playing) { snprintf(d->error, sizeof(d->error), "stop playing first"); return -1; }
    if (!(text = (char *)br_file_read_all(path, &size))) {
        snprintf(d->error, sizeof(d->error), "can't read '%s'", path);
        return -1;
    }
    {   /* room for every entity the file could hold */
        const char *p = text;
        uint32_t guess = 1;
        while ((p = strstr(p, "entity")) != NULL) { guess++; p += 6; }
        made = (br_entity *)br_alloc(guess * sizeof(br_entity));
        n = made && reserve(d, d->count + guess)
          ? br_world_from_text(&d->world, text, made, guess, d->error, sizeof(d->error)) : -1;
    }
    br_free(text);
    if (n < 0) { br_free(made); return -1; }
    for (i = 0; i < n; ++i) {
        const br_transform *t = (const br_transform *)br_get(&d->world, made[i], d->ids.transform);
        if (t) { sum = br_v2_add(sum, t->position); ++placed; }
    }
    if (placed) {
        br_vec2 shift = br_v2_sub(at, br_v2_scale(sum, 1.0f / (float)placed));
        for (i = 0; i < n; ++i) {
            br_transform *t = (br_transform *)br_get(&d->world, made[i], d->ids.transform);
            if (t) t->position = br_v2_add(t->position, shift);
        }
    }
    for (i = 0; i < n; ++i) insert_at(d, d->count, made[i]);
    if (n > 0) d->selected = made[0];
    br_free(made);
    return n;
}

bool ed_doc_save_entity(ed_doc *d, br_entity e, const char *path) {
    if (!br_entity_alive(&d->world, e)) { snprintf(d->error, sizeof(d->error), "nothing selected"); return false; }
    if (!br_world_save_file(&d->world, &e, 1, path)) {
        snprintf(d->error, sizeof(d->error), "can't write '%s'", path);
        return false;
    }
    return true;
}

/* ---- undo / redo --------------------------------------------------------------------- */

void ed_doc_checkpoint(ed_doc *d) {
    char *now;
    if (d->playing) return;
    now = snapshot(d);
    if (!now) return;
    if (d->state && strcmp(now, d->state) == 0) { br_free(now); return; }
    if (d->state) push(d->undo, &d->undo_count, d->state);
    d->state = now;
    clear_stack(d->redo, &d->redo_count);
}

bool ed_doc_undo(ed_doc *d) {
    if (d->playing || d->undo_count == 0) return false;
    push(d->redo, &d->redo_count, d->state);
    d->state = d->undo[--d->undo_count];
    return restore(d, d->state, ed_doc_index(d, d->selected));
}

bool ed_doc_redo(ed_doc *d) {
    if (d->playing || d->redo_count == 0) return false;
    push(d->undo, &d->undo_count, d->state);
    d->state = d->redo[--d->redo_count];
    return restore(d, d->state, ed_doc_index(d, d->selected));
}

/* ---- play mode ------------------------------------------------------------------------- */

void ed_doc_play(ed_doc *d) {
    if (d->playing) return;
    ed_doc_checkpoint(d);
    set_text(&d->play_snapshot, snapshot(d));
    d->play_selected = ed_doc_index(d, d->selected);
    br_script_reload(&d->world);                     /* pick up scripts edited since the last run */
    d->playing = d->play_snapshot != NULL;
}

void ed_doc_stop(ed_doc *d) {
    if (!d->playing) return;
    restore(d, d->play_snapshot, d->play_selected);
    set_text(&d->play_snapshot, NULL);
    d->playing = false;
}

void ed_doc_step(ed_doc *d, float dt) {
    if (d->playing) br_world_run_phase(&d->world, BR_PHASE_FIXED_UPDATE, dt);
}
