/*
 * Orbits - the game's C code (docs/c-code.md).
 *
 * Two components and a behaviour, all usable in the editor once it compiles this:
 *
 *   Orbit    circles another entity (`center`) at `radius`, `speed` degrees a second;
 *            Space held makes every orbit go three times as fast
 *   Twinkle  a behaviour: an entity whose Script component's file is "Twinkle" grows
 *            and shrinks its Shape a little, each at its own pace
 *
 * The scene (main.scene) is ordinary: the sun, planets and a moon are entities with
 * a Shape, and the planets have an Orbit, edited in the inspector like any component.
 */
#include "bronto/bronto.h"
#include "orbits.h"
#include <math.h>

/* ---- Orbit ---------------------------------------------------------------------- */

static const br_field k_orbit_fields[] = {
    BR_FIELD(Orbit, center, BR_FIELD_ENTITY, 0, 0),
    BR_FIELD(Orbit, radius, BR_FIELD_FLOAT, 0, 400),
    BR_FIELD(Orbit, speed, BR_FIELD_FLOAT, -720, 720),
    BR_FIELD(Orbit, angle, BR_FIELD_FLOAT, 0, 360),
};
static const Orbit k_orbit_default = { BR_ENTITY_NULL, 40, 45, 0 };
static const br_component_desc k_orbit = BR_COMPONENT_DESC(Orbit, k_orbit_fields, &k_orbit_default);

static void orbit_system(br_world *w, float dt, void *user) {
    br_component with[2];
    br_component transform = br_component_find(w, "Transform");
    br_engine *engine = (br_engine *)w->context;             /* (the engine, for its input) */
    float boost = engine && br_key_down(&engine->input, BR_KEY_SPACE) ? ORBIT_BOOST : 1.0f;
    br_query q;
    (void)user;
    with[0] = transform;
    with[1] = br_component_find(w, "Orbit");
    q = br_query_make(w, with, 2);
    while (br_query_next(&q)) {
        br_transform *t = BR_QUERY_GET(&q, 0, br_transform);
        Orbit *o = BR_QUERY_GET(&q, 1, Orbit);
        const br_transform *c = o->center != BR_ENTITY_NULL ? (const br_transform *)br_get(w, o->center, transform) : NULL;
        br_vec2 at = c ? c->position : br_v2(0, 0);
        float rad;
        o->angle = fmodf(o->angle + o->speed * boost * dt, 360.0f);
        rad = o->angle * 3.14159265f / 180.0f;
        t->position = br_v2(at.x + cosf(rad) * o->radius, at.y + sinf(rad) * o->radius);
    }
}

static void orbits_world(br_world *w, void *user) {
    (void)user;
    br_component_register_desc(w, &k_orbit);
    br_system_add(w, "orbits", BR_PHASE_FIXED_UPDATE, orbit_system, NULL, 0);
}

/* ---- Twinkle -------------------------------------------------------------------- */

typedef struct twinkle { float time, base_w, base_h; } twinkle;

static void twinkle_start(br_world *w, br_entity self, void *state) {
    twinkle *tw = (twinkle *)state;
    const br_shape *s = (const br_shape *)br_get(w, self, br_component_find(w, "Shape"));
    if (s) { tw->base_w = s->size.x; tw->base_h = s->size.y; }
    tw->time = (float)(self % 17) * 0.37f;                     /* each one at its own point */
}

static void twinkle_update(br_world *w, br_entity self, void *state, float dt) {
    twinkle *tw = (twinkle *)state;
    br_shape *s = (br_shape *)br_get(w, self, br_component_find(w, "Shape"));
    float k;
    tw->time += dt;
    k = 1.0f + 0.08f * sinf(tw->time * 4.0f);
    if (s) s->size = br_v2(tw->base_w * k, tw->base_h * k);
}

static const br_behaviour k_twinkle = { sizeof(twinkle), twinkle_start, twinkle_update, NULL, NULL, NULL };

/* ---- setup ------------------------------------------------------------------------ */

BR_GAME_SETUP(orbits) {
    br_game_on_world(orbits_world, NULL);
    br_script_register_behaviour("Twinkle", &k_twinkle);
}
