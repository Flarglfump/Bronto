#include "bronto/scene.h"
#include "bronto/memory.h"
#include "bronto/log.h"

#include <string.h>

void br_scenes_init(br_scene_manager *m, struct br_engine *engine, br_events *events) {
    memset(m, 0, sizeof(*m));
    m->engine = engine;
    m->events = events;
}

static void enqueue(br_scene_manager *m, br_scene_op_kind kind, const br_scene_desc *desc) {
    if (m->op_count >= BR_MAX_SCENE_OPS) {
        BR_ERROR("too many pending scene operations");
        return;
    }
    m->ops[m->op_count].kind = kind;
    m->ops[m->op_count].desc = desc;
    m->op_count++;
}

void br_scene_push(br_scene_manager *m, const br_scene_desc *d)   { enqueue(m, BR_SCENE_OP_PUSH, d); }
void br_scene_pop(br_scene_manager *m)                            { enqueue(m, BR_SCENE_OP_POP, NULL); }
void br_scene_switch(br_scene_manager *m, const br_scene_desc *d) { enqueue(m, BR_SCENE_OP_SWITCH, d); }
void br_scene_clear(br_scene_manager *m)                          { enqueue(m, BR_SCENE_OP_CLEAR, NULL); }

br_scene *br_scenes_top(const br_scene_manager *m) { return m->depth ? m->stack[m->depth - 1] : NULL; }
int br_scenes_depth(const br_scene_manager *m) { return m->depth; }

static void destroy_scene(br_scene *s) {
    br_world_destroy(&s->world);
    br_free(s->data);
    br_free(s);
}

static void do_pop(br_scene_manager *m, bool resume_below) {
    br_scene *s;
    if (m->depth == 0) return;
    s = m->stack[--m->depth];
    if (s->desc->on_exit) s->desc->on_exit(s);
    destroy_scene(s);
    if (resume_below && m->depth > 0) {
        br_scene *top = m->stack[m->depth - 1];
        if (top->desc->on_resume) top->desc->on_resume(top);
    }
}

static bool do_push(br_scene_manager *m, const br_scene_desc *desc, bool pause_below) {
    br_scene *s;
    if (!desc) return false;
    if (m->depth >= BR_MAX_SCENE_STACK) {
        BR_ERROR("scene stack overflow pushing '%s'", desc->name ? desc->name : "?");
        return false;
    }
    s = BR_NEW(br_scene);
    if (!s) return false;
    s->desc = desc;
    s->engine = m->engine;
    if (!br_world_init(&s->world)) { br_free(s); return false; }
    s->world.context = m->engine;
    s->world.user = s;
    if (desc->data_size) {
        s->data = br_calloc(1, desc->data_size);
        if (!s->data) { destroy_scene(s); return false; }
    }
    /* On a switch the scene below is already paused (by the scene being
     * replaced), so it is not paused a second time. */
    if (pause_below && m->depth > 0) {
        br_scene *below = m->stack[m->depth - 1];
        if (below->desc->on_pause) below->desc->on_pause(below);
    }
    m->stack[m->depth++] = s;
    if (desc->on_enter && !desc->on_enter(s)) {
        BR_WARN("scene '%s' refused to enter", desc->name ? desc->name : "?");
        m->depth--;
        destroy_scene(s);
        if (m->depth > 0) {
            br_scene *below = m->stack[m->depth - 1];
            if (below->desc->on_resume) below->desc->on_resume(below);
        }
        return false;
    }
    return true;
}

bool br_scenes_apply(br_scene_manager *m) {
    int i, n = m->op_count;
    bool changed = false;
    br_scene_op ops[BR_MAX_SCENE_OPS];
    if (n == 0) return false;
    /* Copy first: on_enter/on_exit may queue further operations, which are
     * applied on the next call. */
    memcpy(ops, m->ops, sizeof(br_scene_op) * (size_t)n);
    m->op_count = 0;
    for (i = 0; i < n; ++i) {
        switch (ops[i].kind) {
        case BR_SCENE_OP_PUSH:
            changed |= do_push(m, ops[i].desc, true);
            break;
        case BR_SCENE_OP_POP:
            if (m->depth) { do_pop(m, true); changed = true; }
            break;
        case BR_SCENE_OP_SWITCH:
            if (m->depth) do_pop(m, false);
            do_push(m, ops[i].desc, false);
            changed = true;
            break;
        case BR_SCENE_OP_CLEAR:
            while (m->depth) do_pop(m, false);
            changed = true;
            break;
        }
    }
    if (changed && m->events) {
        br_event e = br_event_make(BR_EVENT_SCENE_CHANGED);
        br_events_post(m->events, &e);
    }
    return changed;
}

void br_scenes_shutdown(br_scene_manager *m) {
    m->op_count = 0;
    while (m->depth) do_pop(m, false);
}

/* Index of the lowest scene that should update (walk down while the scene
 * above lets updates through). */
static int lowest_updating(const br_scene_manager *m) {
    int i = m->depth - 1;
    while (i > 0 && m->stack[i]->desc->update_below) --i;
    return i;
}

static int lowest_visible(const br_scene_manager *m) {
    int i = m->depth - 1;
    while (i > 0 && m->stack[i]->desc->transparent) --i;
    return i;
}

void br_scenes_fixed_update(br_scene_manager *m, float dt) {
    int i;
    if (!m->depth) return;
    for (i = lowest_updating(m); i < m->depth; ++i) {
        br_scene *s = m->stack[i];
        if (s->desc->fixed_update) s->desc->fixed_update(s, dt);
        br_world_run_phase(&s->world, BR_PHASE_FIXED_UPDATE, dt);
    }
}

void br_scenes_update(br_scene_manager *m, float dt) {
    int i;
    if (!m->depth) return;
    for (i = lowest_updating(m); i < m->depth; ++i) {
        br_scene *s = m->stack[i];
        if (s->desc->update) s->desc->update(s, dt);
        br_world_run_phase(&s->world, BR_PHASE_UPDATE, dt);
        br_world_run_phase(&s->world, BR_PHASE_LATE_UPDATE, dt);
        s->time += dt;
        s->frames++;
    }
}

void br_scenes_render(br_scene_manager *m) {
    int i;
    if (!m->depth) return;
    for (i = lowest_visible(m); i < m->depth; ++i) {
        br_scene *s = m->stack[i];
        br_world_run_phase(&s->world, BR_PHASE_RENDER, 0.0f);
        if (s->desc->render) s->desc->render(s);
    }
}

bool br_scenes_event(br_scene_manager *m, const br_event *e) {
    int i;
    for (i = m->depth - 1; i >= 0; --i) {
        br_scene *s = m->stack[i];
        if (s->desc->on_event && s->desc->on_event(s, e)) return true;
        if (!s->desc->update_below) break;
    }
    return false;
}
