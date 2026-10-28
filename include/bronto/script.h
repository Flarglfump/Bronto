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
 * on_collision_end(other), on_trigger(other), on_trigger_exit(other),
 * draw() when the scene is drawn (in the layer named by the script's
 * `draw_layer` variable), and on_pause() / on_resume() when another scene
 * is pushed over this one and when it is the top scene again.
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
#include "render.h"

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
/* A vec2 variable, at full precision (false if it isn't one). */
bool br_script_get_vec2(br_world *w, br_entity e, const char *var, double *x, double *y);
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
/* push_scene(path, transparent, update_below) and pop_scene(): for the game
 * (bronto_player, the editor's Play) to act on, like load_scene. */
bool br_script_push_request(br_world *w, char *path, size_t size, bool *transparent, bool *update_below);
bool br_script_pop_request(br_world *w);
/* Calls a function with no arguments (such as "on_pause" or "on_resume") in
 * every script of the world that has it. Returns how many did. */
int br_script_notify(br_world *w, const char *function);

/* ---- drawing from scripts --------------------------------------------------
 * A script with a draw() function is drawn among the scene's sprites, in the
 * layer its `draw_layer` variable gives (0 without one). br_builtin_draw asks
 * these for each Script; draw() runs with `r` as the target of draw_rect(),
 * draw_text() and the rest. */
bool br_script_draw_layer(const br_world *w, br_entity e, int *layer);
void br_script_draw(br_world *w, br_entity e, br_renderer *r);

/* ---- state that outlives a scene -------------------------------------------
 * set_global() / global() in scripts keep values (as text) while the game
 * runs, across load_scene and push_scene. play_music() keeps playing too.
 * br_script_reset_session() forgets the globals and stops the music and the
 * tones play_tone() made (the editor does this when Play starts and stops);
 * br_script_release() also frees them (the engine's shutdown does this). */
void br_script_reset_session(void);
void br_script_release(void);
/* The music play_music() is playing ("" for none). */
const char *br_script_music(void);
bool br_script_strict(void);

/* The most recent script error ("player.bs:12: unknown function 'jmup'"),
 * "" if none; and how many there have been in this world. */
const char *br_script_last_error(const br_world *w);
uint32_t br_script_error_count(const br_world *w);

/* ---- the debugger ----
 * A host (the editor) attaches a hook; when a script reaches a breakpoint (or a
 * step, or a pause asked for), the hook is called right there, in the middle of
 * the script, and the game waits until it returns. While it runs it can look at
 * the call stack and variables; what it returns says how to go on.
 *
 *   CONTINUE   run on to the next breakpoint
 *   STEP_INTO  stop at the next statement, inside a call it makes too
 *   STEP_OVER  stop at the next statement of this function (or after it returns)
 *   STEP_OUT   stop once this function has returned
 *   DETACH     run on, ignoring breakpoints until the next attach or resume
 *
 * Breakpoints are by file and line; a file matches a script's path when one ends
 * with the other ("scripts/player.bs" finds "/home/me/game/scripts/player.bs").
 * With no hook attached, scripts run at full speed: nothing is checked. */
typedef enum br_debug_action {
    BR_DEBUG_CONTINUE = 0, BR_DEBUG_STEP_INTO, BR_DEBUG_STEP_OVER, BR_DEBUG_STEP_OUT, BR_DEBUG_DETACH
} br_debug_action;
typedef struct br_debug_stop {
    br_world *world;
    br_entity entity;               /* whose script */
    const char *file;               /* its path */
    int line;
    const char *reason;             /* "breakpoint", "step" or "paused" */
} br_debug_stop;
typedef struct br_debug_var { char name[32]; char value[120]; } br_debug_var;
typedef br_debug_action (*br_debug_hook)(const br_debug_stop *stop, void *user);

void br_script_debug_attach(br_debug_hook hook, void *user);      /* NULL: detach */
bool br_script_debug_set_breakpoint(const char *file, int line, bool on);
bool br_script_debug_has_breakpoint(const char *file, int line);
int  br_script_debug_breakpoint(int index, const char **file, int *line);   /* returns how many */
void br_script_debug_clear_breakpoints(void);
void br_script_debug_pause(void);     /* stop at the next statement any script runs */
void br_script_debug_resume(void);    /* forget a pause or a step; breakpoints count again */
/* Only inside the hook: */
bool br_script_debug_stopped(void);
int  br_script_debug_frame_count(void);
bool br_script_debug_frame(int index, const char **function, const char **file, int *line);   /* 0: innermost */
int  br_script_debug_locals(int frame, br_debug_var *out, int max);
int  br_script_debug_globals(int frame, br_debug_var *out, int max);

/* ---- a game's own components ----
 * Scripts can declare components; the scene files save them, the editor's
 * inspector edits them, and scripts use them like the engine's (self.Health.hp,
 * has(e, "Health"), add(e, "Health"), find_with("Health")):
 *
 *   component Health {
 *       var hp: int = 3
 *       var regen: number = 0.5
 *       var tint: color = "#ff4040"
 *       var team: enum(red, blue) = blue
 *       var target: entity
 *   }
 *
 * Field types: int, number, bool, string (up to 63 bytes), vec2, vec3, color,
 * entity (starts as none) and enum(...). br_script_components_from_project reads
 * every .bs file of a project (its pack, if one is mounted) and keeps the set;
 * worlds made afterwards get them (br_builtin_register). The player and the editor
 * do this when a project opens. Returns how many, or -1 if a declaration was wrong
 * (the message in err; the good ones are kept). */
int  br_script_components_from_project(const char *root, char *err, size_t err_size);
int  br_script_scan_components(const char *source, const char *file, char *err, size_t err_size);
void br_script_components_clear(void);
void br_script_register_components(br_world *w);
int  br_script_component_count(void);
const br_component_desc *br_script_component(int index);
uint32_t br_script_components_signature(void);   /* changes when the declarations do */
void br_script_components_release(void);

/* Where print() goes (default: the engine log). */
void br_script_set_print(void (*fn)(const char *text, void *user), void *user);

/* Forget loaded script files so they are read again (the runtime also
 * notices changed files by itself). */
void br_script_reload(br_world *w);

#endif /* BRONTO_SCRIPT_H */
