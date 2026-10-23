/*
 * Bronto Engine - scripts.
 *
 * A Script component runs a script on its entity. A script is either a
 * BrontoScript file (.bs) in the project, interpreted by the engine and
 * reloaded whenever the file changes, or a behaviour written in C and
 * registered by name with br_script_register_behaviour().
 *
 *   Script.file  "scripts/player.bs" (relative to the asset root), or the
 *                name of a registered C behaviour
 *   Script.vars  starting values for the script's variables, overriding
 *                its defaults: "speed = 120, name = \"Rex\", wrap = true"
 *
 * The language, in one example (docs/scripting.md has the full reference):
 *
 *   # player.bs - move with the arrow keys, jump with space
 *   var speed = 100          # one copy per entity that runs this script
 *   var jump = 260
 *
 *   func start() {
 *       print("hello from " + self.name)
 *   }
 *
 *   func update(dt) {
 *       var dir = 0
 *       if key_down("left")  { dir -= 1 }
 *       if key_down("right") { dir += 1 }
 *       self.velocity.x = dir * speed
 *       if key_pressed("space") and on_ground() { self.velocity.y = -jump }
 *   }
 *
 *   func on_ground() {
 *       return raycast(self.position, self.position + vec2(0, 10), self) != nil
 *   }
 *
 *   func on_collision(other) {
 *       if other.name == "Coin" { destroy(other) }
 *   }
 *
 * Callbacks the engine calls when a script defines them: start(),
 * update(dt) every fixed step, on_collision(other, normal, speed),
 * on_collision_end(other), on_trigger(other), on_trigger_exit(other).
 *
 * The "builtin_scripts" system runs before physics in the fixed update,
 * and "builtin_script_events" after it, delivering the contacts.
 * Both do nothing while world->context is NULL (the engine sets it), so the
 * editor only runs scripts while playing.
 *
 * Errors (a typo, calling something that doesn't exist) stop that one
 * script and are reported once with the file and line; the rest of the
 * game keeps running. br_script_last_error() returns the latest.
 */
#ifndef BRONTO_SCRIPT_H
#define BRONTO_SCRIPT_H

#include "ecs.h"

typedef struct br_script {
    char file[96];
    char vars[256];
    /* runtime only (not saved): the running instance */
    void *instance;
} br_script;

/* ---- C behaviours ------------------------------------------------------------
 * Any callback may be NULL. `state` starts zeroed, `state_size` bytes, one
 * per entity, freed when the entity goes away. */
typedef struct br_behaviour {
    size_t state_size;
    void (*start)(br_world *w, br_entity self, void *state);
    void (*update)(br_world *w, br_entity self, void *state, float dt);
    void (*on_collision)(br_world *w, br_entity self, void *state, br_entity other, bool began);
    void (*on_trigger)(br_world *w, br_entity self, void *state, br_entity other, bool entered);
    void (*on_destroy)(br_world *w, br_entity self, void *state);
} br_behaviour;

/* Registers a behaviour for every world (`b` must outlive them). A Script
 * whose file is exactly `name` runs it. */
bool br_script_register_behaviour(const char *name, const br_behaviour *b);

/* Registers the Script component and its systems (br_builtin_register does). */
void br_script_register(br_world *w);
const br_component_desc *br_script_desc(void);

/* What the systems do: start and update every script / deliver contacts. */
void br_script_step(br_world *w, float dt);
void br_script_deliver_contacts(br_world *w);

/* Calls a script function on an entity with number arguments. Returns
 * false if it has no running script with that function. */
bool br_script_call(br_world *w, br_entity e, const char *function, const double *args, int arg_count);

/* An entity's script variable as a number / string (false if it has none). */
bool br_script_get_number(br_world *w, br_entity e, const char *var, double *out);
bool br_script_set_number(br_world *w, br_entity e, const char *var, double value);
bool br_script_get_string(br_world *w, br_entity e, const char *var, char *out, size_t size);
/* An entity variable (false if it isn't one, or the entity is gone). */
bool br_script_get_entity(br_world *w, br_entity e, const char *var, br_entity *out);

/* The language's built-in functions, by name (for tools such as editors). */
int br_script_builtin_count(void);
const char *br_script_builtin_name(int index);
bool br_script_builtin_arity(int index, int *min_args, int *max_args);   /* false for an index out of range */

/* Checks a script file without running it: true if it parses. */
bool br_script_check(const char *source, const char *name, char *err, size_t err_size);
/* Strict typing for every script compiled from now on (as if each started
 * with @strict): every var, parameter and function result needs a type.
 * Games and the editor set it from the project's strict_typing setting. */
void br_script_set_strict(bool strict);
/* What scripts asked for during the last steps: load_scene("levels/2.scene")
 * and quit(). The game (the player, the editor's Play) acts on them; each
 * request is returned once. */
bool br_script_scene_request(br_world *w, char *path, size_t size);
bool br_script_quit_request(br_world *w);
bool br_script_strict(void);

/* The most recent script error ("player.bs:12: unknown function 'jmup'"),
 * "" if none; and how many there have been in this world. */
const char *br_script_last_error(const br_world *w);
uint32_t br_script_error_count(const br_world *w);

/* Where print() goes (default: the engine log). */
void br_script_set_print(void (*fn)(const char *text, void *user), void *user);

/* Forget loaded script files so they are read again (the runtime also
 * notices changed files by itself). */
void br_script_reload(br_world *w);

#endif /* BRONTO_SCRIPT_H */
