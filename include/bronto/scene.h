/*
 * Bronto Engine - scenes.
 *
 * A scene is a stack entry with its own ECS world and optional per-instance
 * data block. The manager keeps a stack so overlays (pause menu, dialog)
 * can sit on top of gameplay:
 *
 *   transparent  - scenes underneath are still rendered
 *   update_below - scenes underneath keep updating and receive events the
 *                  top scene did not consume
 *
 * Transitions (push/pop/switch) requested during a frame are deferred and
 * applied at a safe point (br_scenes_apply), never mid-update.
 */
#ifndef BRONTO_SCENE_H
#define BRONTO_SCENE_H

#include "types.h"
#include "ecs.h"
#include "event.h"

struct br_engine;
typedef struct br_scene br_scene;

typedef struct br_scene_desc {
    const char *name;
    size_t data_size;        /* bytes allocated (zeroed) as scene->data */
    bool transparent;
    bool update_below;

    /* All callbacks are optional. */
    bool (*on_enter)(br_scene *s);        /* return false to abort the push */
    void (*on_exit)(br_scene *s);
    void (*on_pause)(br_scene *s);        /* another scene pushed on top   */
    void (*on_resume)(br_scene *s);       /* became top again              */
    void (*fixed_update)(br_scene *s, float dt);
    void (*update)(br_scene *s, float dt);
    void (*render)(br_scene *s);          /* after ECS render systems      */
    bool (*on_event)(br_scene *s, const br_event *e); /* true = consumed  */
} br_scene_desc;

struct br_scene {
    const br_scene_desc *desc;
    br_world world;
    void *data;
    struct br_engine *engine;
    double time;          /* seconds this scene has been updated */
    uint64_t frames;
};

#define BR_MAX_SCENE_STACK 16
#define BR_MAX_SCENE_OPS   16

typedef enum br_scene_op_kind { BR_SCENE_OP_PUSH, BR_SCENE_OP_POP, BR_SCENE_OP_SWITCH, BR_SCENE_OP_CLEAR } br_scene_op_kind;
typedef struct br_scene_op { br_scene_op_kind kind; const br_scene_desc *desc; } br_scene_op;

typedef struct br_scene_manager {
    br_scene *stack[BR_MAX_SCENE_STACK];
    int depth;
    br_scene_op ops[BR_MAX_SCENE_OPS];
    int op_count;
    struct br_engine *engine;   /* passed into each scene              */
    br_events *events;          /* optional: receives SCENE_CHANGED    */
} br_scene_manager;

void br_scenes_init(br_scene_manager *m, struct br_engine *engine, br_events *events);
void br_scenes_shutdown(br_scene_manager *m);  /* exits all scenes */

/* Deferred requests. */
void br_scene_push(br_scene_manager *m, const br_scene_desc *desc);
void br_scene_pop(br_scene_manager *m);
void br_scene_switch(br_scene_manager *m, const br_scene_desc *desc); /* replace top */
void br_scene_clear(br_scene_manager *m);                             /* pop all     */
/* Apply pending requests now. Returns true if the stack changed. */
bool br_scenes_apply(br_scene_manager *m);

br_scene *br_scenes_top(const br_scene_manager *m);
int br_scenes_depth(const br_scene_manager *m);

void br_scenes_fixed_update(br_scene_manager *m, float dt);
void br_scenes_update(br_scene_manager *m, float dt);
void br_scenes_render(br_scene_manager *m);
bool br_scenes_event(br_scene_manager *m, const br_event *e);

#endif /* BRONTO_SCENE_H */
