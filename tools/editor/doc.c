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
#include <ctype.h>
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
    ed_doc_normalize(d);
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

/* ---- the tree ---------------------------------------------------------------------
 * `order` is always the tree flattened depth first: each entity is followed
 * by its whole subtree. Sibling order is the order they have in `order`.
 * Every change that touches the tree ends with normalize(), which rebuilds
 * that shape from the Parent components while keeping sibling order. */

static void emit(ed_doc *d, const br_entity *old, uint32_t n, br_entity node, br_entity *out, uint32_t *count, uint8_t *done) {
    uint32_t i;
    for (i = 0; i < n; ++i) if (old[i] == node) { if (done[i]) return; done[i] = 1; break; }
    out[(*count)++] = node;
    for (i = 0; i < n; ++i)                           /* its children, in their current order */
        if (!done[i] && br_parent_of(&d->world, old[i]) == node) emit(d, old, n, old[i], out, count, done);
}

void ed_doc_normalize(ed_doc *d) {
    br_entity *old, *fresh;
    uint8_t *done;
    uint32_t i, n = 0, count = 0;
    if (!d->count) return;
    old = (br_entity *)br_alloc(sizeof(br_entity) * d->count);
    fresh = (br_entity *)br_alloc(sizeof(br_entity) * d->count);
    done = (uint8_t *)br_alloc(d->count);
    if (!old || !fresh || !done) { br_free(old); br_free(fresh); br_free(done); return; }
    for (i = 0; i < d->count; ++i) if (br_entity_alive(&d->world, d->order[i])) old[n++] = d->order[i];
    memset(done, 0, n);
    for (i = 0; i < n; ++i) {                         /* roots (and orphans of entities not in the list) */
        br_entity p = br_parent_of(&d->world, old[i]);
        bool listed = false;
        uint32_t k;
        for (k = 0; p != BR_ENTITY_NULL && k < n && !listed; ++k) listed = old[k] == p;
        if (!done[i] && !listed) emit(d, old, n, old[i], fresh, &count, done);
    }
    memcpy(d->order, fresh, sizeof(br_entity) * count);
    d->count = count;
    if (!br_entity_alive(&d->world, d->selected)) d->selected = BR_ENTITY_NULL;
    br_free(old);
    br_free(fresh);
    br_free(done);
}

/* How many entries of `order`, from `at`, belong to the subtree starting there. */
static uint32_t block_len(const ed_doc *d, uint32_t at) {
    uint32_t n = 1;
    while (at + n < d->count && br_is_ancestor(&d->world, d->order[at], d->order[at + n])) ++n;
    return n;
}

br_entity ed_doc_create(ed_doc *d, const char *name, br_vec2 pos) {
    br_entity e = br_entity_create(&d->world);
    if (e == BR_ENTITY_NULL) return e;
    snprintf(((br_name *)br_add(&d->world, e, d->ids.name, NULL))->text, sizeof(((br_name *)0)->text), "%s", name);
    ((br_transform *)br_add(&d->world, e, d->ids.transform, NULL))->position = pos;
    insert_at(d, d->count, e);
    d->selected = e;
    return e;
}

br_entity ed_doc_create_child(ed_doc *d, br_entity parent, const char *name, br_vec2 local) {
    br_entity e;
    if (!br_entity_alive(&d->world, parent)) return ed_doc_create(d, name, local);
    if ((e = ed_doc_create(d, name, local)) == BR_ENTITY_NULL) return e;
    br_set_parent(&d->world, e, parent, false);       /* `local` is in the parent's space */
    ed_doc_normalize(d);
    return e;
}

br_entity ed_doc_duplicate(ed_doc *d, br_entity e) {
    int at = ed_doc_index(d, e);
    uint32_t n, i, k;
    br_entity *src, *dst;
    br_name *nm;
    br_transform *t;
    if (at < 0) return BR_ENTITY_NULL;
    n = block_len(d, (uint32_t)at);
    src = (br_entity *)br_alloc(sizeof(br_entity) * n);
    dst = (br_entity *)br_alloc(sizeof(br_entity) * n);
    if (!src || !dst || !reserve(d, d->count + n)) { br_free(src); br_free(dst); return BR_ENTITY_NULL; }
    memcpy(src, d->order + at, sizeof(br_entity) * n);
    for (i = 0; i < n; ++i) dst[i] = br_entity_clone(&d->world, src[i]);
    for (i = 1; i < n; ++i) {                         /* copies point at copies */
        br_entity p = br_parent_of(&d->world, src[i]);
        for (k = 0; k < n; ++k) if (src[k] == p) { br_set_parent(&d->world, dst[i], dst[k], false); break; }
    }
    if ((nm = (br_name *)br_get(&d->world, dst[0], d->ids.name)) != NULL) {
        char base[sizeof(nm->text)];
        snprintf(base, sizeof(base), "%s", nm->text);
        snprintf(nm->text, sizeof(nm->text), "%.26s copy", base);
    }
    if ((t = (br_transform *)br_get(&d->world, dst[0], d->ids.transform)) != NULL)
        t->position = br_v2_add(t->position, br_world_to_parent_vector(&d->world, dst[0], br_v2(8, 8)));   /* visibly not on top */
    memmove(d->order + at + n + n, d->order + at + n, (d->count - (uint32_t)at - n) * sizeof(br_entity));
    memcpy(d->order + at + n, dst, sizeof(br_entity) * n);   /* right after the original's subtree */
    d->count += n;
    d->selected = dst[0];
    br_free(src);
    br_free(dst);
    ed_doc_normalize(d);
    return d->selected;
}

void ed_doc_delete(ed_doc *d, br_entity e) {
    int at = ed_doc_index(d, e);
    if (at < 0) return;
    br_entity_destroy(&d->world, e);                  /* and its children (the hierarchy's hook) */
    ed_doc_normalize(d);
    if (d->selected == BR_ENTITY_NULL || d->selected == e)   /* the neighbour, so Delete can be pressed again */
        d->selected = d->count ? d->order[(uint32_t)at < d->count ? (uint32_t)at : d->count - 1] : BR_ENTITY_NULL;
}

bool ed_doc_place(ed_doc *d, br_entity e, br_entity parent, br_entity before) {
    int at = ed_doc_index(d, e), to;
    uint32_t n;
    br_entity *block;
    if (at < 0 || (parent != BR_ENTITY_NULL && (parent == e || br_is_ancestor(&d->world, e, parent)))) return false;
    if (before == e || (before != BR_ENTITY_NULL && br_is_ancestor(&d->world, e, before))) before = BR_ENTITY_NULL;
    if (!br_set_parent(&d->world, e, parent, true)) return false;   /* stays where it is on screen */
    n = block_len(d, (uint32_t)at);
    if (!(block = (br_entity *)br_alloc(sizeof(br_entity) * n))) return false;
    memcpy(block, d->order + at, sizeof(br_entity) * n);
    memmove(d->order + at, d->order + at + n, (d->count - (uint32_t)at - n) * sizeof(br_entity));
    d->count -= n;
    if (before != BR_ENTITY_NULL && (to = ed_doc_index(d, before)) >= 0) {
    } else if (parent != BR_ENTITY_NULL && (to = ed_doc_index(d, parent)) >= 0) {
        to += (int)block_len(d, (uint32_t)to);        /* after the parent's last descendant */
    } else {
        to = (int)d->count;
    }
    memmove(d->order + to + n, d->order + to, (d->count - (uint32_t)to) * sizeof(br_entity));
    memcpy(d->order + to, block, sizeof(br_entity) * n);
    d->count += n;
    br_free(block);
    ed_doc_normalize(d);
    return true;
}

/* Swaps with the previous / next sibling. */
void ed_doc_reorder(ed_doc *d, br_entity e, int delta) {
    br_entity p = br_parent_of(&d->world, e), sib = BR_ENTITY_NULL;
    int at = ed_doc_index(d, e), i;
    if (at < 0) return;
    if (delta < 0) {
        for (i = at - 1; i >= 0; --i) if (br_parent_of(&d->world, d->order[i]) == p) { sib = d->order[i]; break; }
        if (sib != BR_ENTITY_NULL) ed_doc_place(d, e, p, sib);
    } else {
        for (i = at + (int)block_len(d, (uint32_t)at); i < (int)d->count; ++i)
            if (br_parent_of(&d->world, d->order[i]) == p) { sib = d->order[i]; break; }
        if (sib != BR_ENTITY_NULL) {                  /* before the sibling after that one */
            int j = ed_doc_index(d, sib) + (int)block_len(d, (uint32_t)ed_doc_index(d, sib));
            br_entity after = BR_ENTITY_NULL;
            for (; j < (int)d->count; ++j) if (br_parent_of(&d->world, d->order[j]) == p) { after = d->order[j]; break; }
            ed_doc_place(d, e, p, after);
        }
    }
}

void ed_doc_move(ed_doc *d, br_entity e, int slot) {
    br_entity before = slot >= 0 && slot < (int)d->count ? d->order[slot] : BR_ENTITY_NULL;
    br_entity parent = before != BR_ENTITY_NULL ? br_parent_of(&d->world, before) : BR_ENTITY_NULL;
    if (before == e) return;
    ed_doc_place(d, e, parent, before);
}

const char *ed_doc_name(const ed_doc *d, br_entity e, char *buf, size_t size) {
    const br_name *n = (const br_name *)br_get(&d->world, e, d->ids.name);
    if (n && n->text[0]) snprintf(buf, size, "%s", n->text);
    else snprintf(buf, size, "Entity %u", BR_ENTITY_INDEX(e));
    return buf;
}

bool ed_doc_matches(const ed_doc *d, br_entity e, const char *filter) {
    const char *p = filter;
    char name[64];
    ed_doc_name(d, e, name, sizeof(name));
    while (p && *p) {
        char word[64];
        size_t n = 0;
        while (*p == ' ' || *p == ',') ++p;
        while (*p && *p != ' ' && *p != ',' && n < sizeof(word) - 1) word[n++] = *p++;
        word[n] = 0;
        if (!n) break;
        if (word[0] == '#') {                            /* a tag */
            if (word[1] && !br_tag_has(&d->world, e, word + 1)) return false;
        } else {                                         /* part of the name, any case */
            size_t i, j, len = strlen(name);
            bool found = false;
            for (i = 0; !found && i + n <= len; ++i) {
                for (j = 0; j < n && tolower((unsigned char)name[i + j]) == tolower((unsigned char)word[j]); ++j) {}
                found = j == n;
            }
            if (!found) return false;
        }
    }
    return true;
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

int ed_doc_paste(ed_doc *d, const char *text, const br_vec2 *at, br_entity parent) {
    br_entity *made;
    br_vec2 sum = br_v2(0, 0);
    int n, i, placed = 0;
    if (d->playing) { snprintf(d->error, sizeof(d->error), "stop playing first"); return -1; }
    if (parent != BR_ENTITY_NULL && !br_entity_alive(&d->world, parent)) parent = BR_ENTITY_NULL;
    {   /* room for every entity the text could hold */
        const char *p = text;
        uint32_t guess = 1;
        while ((p = strstr(p, "entity")) != NULL) { guess++; p += 6; }
        made = (br_entity *)br_alloc(guess * sizeof(br_entity));
        n = made && reserve(d, d->count + guess)
          ? br_world_from_text(&d->world, text, made, guess, d->error, sizeof(d->error)) : -1;
    }
    if (n < 0) { br_free(made); return -1; }
    for (i = 0; i < n; ++i) {                         /* the roots; children ride along */
        const br_transform *t = (const br_transform *)br_get(&d->world, made[i], d->ids.transform);
        if (t && br_parent_of(&d->world, made[i]) == BR_ENTITY_NULL) { sum = br_v2_add(sum, t->position); ++placed; }
    }
    if (placed && at) {                               /* their average position lands on `at` */
        br_vec2 shift = br_v2_sub(*at, br_v2_scale(sum, 1.0f / (float)placed));
        for (i = 0; i < n; ++i) {
            br_transform *t = (br_transform *)br_get(&d->world, made[i], d->ids.transform);
            if (t && br_parent_of(&d->world, made[i]) == BR_ENTITY_NULL) t->position = br_v2_add(t->position, shift);
        }
    }
    if (parent != BR_ENTITY_NULL)                     /* under a parent, staying where they are */
        for (i = 0; i < n; ++i)
            if (br_parent_of(&d->world, made[i]) == BR_ENTITY_NULL) br_set_parent(&d->world, made[i], parent, true);
    for (i = 0; i < n; ++i) insert_at(d, d->count, made[i]);
    ed_doc_normalize(d);
    if (n > 0) d->selected = made[0];
    br_free(made);
    return n;
}

int ed_doc_instantiate(ed_doc *d, const char *path, br_vec2 at) {
    size_t size;
    char *text;
    int n;
    if (d->playing) { snprintf(d->error, sizeof(d->error), "stop playing first"); return -1; }
    if (!(text = (char *)br_file_read_all(path, &size))) {
        snprintf(d->error, sizeof(d->error), "can't read '%s'", path);
        return -1;
    }
    n = ed_doc_paste(d, text, &at, BR_ENTITY_NULL);
    br_free(text);
    return n;
}

/* The entity and its subtree as scene text, the entity saved as a root where it is. */
char *ed_doc_copy(ed_doc *d, br_entity e) {
    int at = ed_doc_index(d, e);
    br_entity parent;
    br_transform *t, local;
    char *text;
    if (at < 0) { snprintf(d->error, sizeof(d->error), "nothing selected"); return NULL; }
    parent = br_parent_of(&d->world, e);
    t = (br_transform *)br_get(&d->world, e, d->ids.transform);
    if (t) local = *t;
    if (parent != BR_ENTITY_NULL) br_set_parent(&d->world, e, BR_ENTITY_NULL, true);
    text = br_world_to_text(&d->world, d->order + at, block_len(d, (uint32_t)at), NULL);
    if (parent != BR_ENTITY_NULL) br_set_parent(&d->world, e, parent, false);
    if (t) *(br_transform *)br_get(&d->world, e, d->ids.transform) = local;
    if (!text) snprintf(d->error, sizeof(d->error), "out of memory");
    return text;
}

bool ed_doc_save_entity(ed_doc *d, br_entity e, const char *path) {
    char *text = ed_doc_copy(d, e);
    bool ok;
    if (!text) return false;
    ok = br_file_write_all(path, text, strlen(text));
    br_free(text);
    if (!ok) snprintf(d->error, sizeof(d->error), "can't write '%s'", path);
    return ok;
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

bool ed_doc_switch_scene(ed_doc *d, const char *path) {
    size_t size;
    char *text;
    bool ok;
    if (!d->playing) return false;                    /* (Stop still goes back to the edited scene) */
    if (!(text = (char *)br_file_read_all(path, &size))) { snprintf(d->error, sizeof(d->error), "can't read '%s'", path); return false; }
    ok = restore(d, text, -1);
    br_free(text);
    return ok;
}

void ed_doc_step(ed_doc *d, float dt) {
    if (d->playing) br_world_run_phase(&d->world, BR_PHASE_FIXED_UPDATE, dt);
}
