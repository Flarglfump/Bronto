/*
 * Bronto Engine - a game's own C code.
 *
 * A project made in the editor can have C source and header files as well as
 * scripts: everything in its code folder (src/ unless Build Settings says
 * otherwise). The editor compiles them with the engine when it builds the
 * game, and also while you work, so the code's components, systems and
 * behaviours are in the editor's scenes, its inspector and its Play
 * (docs/c-code.md).
 *
 * The code joins in through setup functions. Each file can have any number;
 * they run once, before the first scene (and in the editor again each time
 * the code is compiled anew):
 *
 *   #include "bronto/bronto.h"
 *
 *   typedef struct Spin { float speed; } Spin;
 *   static const br_field k_spin_fields[] = { BR_FIELD(Spin, speed, BR_FIELD_FLOAT, -720, 720) };
 *   static const Spin k_spin_default = { 90 };
 *   static const br_component_desc k_spin = BR_COMPONENT_DESC(Spin, k_spin_fields, &k_spin_default);
 *
 *   static void spin_system(br_world *w, float dt, void *user) { ... }
 *
 *   static void add_spin(br_world *w, void *user) {          // every scene's world
 *       br_component_register_desc(w, &k_spin);              // in the inspector's Add Component
 *       br_system_add(w, "spin", BR_PHASE_FIXED_UPDATE, spin_system, NULL, 0);
 *   }
 *
 *   BR_GAME_SETUP(spin) {
 *       br_game_on_world(add_spin, NULL);
 *       br_script_register_behaviour("Wobble", &k_wobble);   // a Script whose file is "Wobble" runs it
 *   }
 *
 * The editor finds the setups itself (it looks for "BR_GAME_SETUP(" in the
 * code folder's .c files) and writes the bronto_game_setup() that calls them
 * all, so there is no list of them to keep. Setup names must be unique in the
 * project; they're C identifiers.
 */
#ifndef BRONTO_GAME_H
#define BRONTO_GAME_H

#include "ecs.h"

/* A setup function: BR_GAME_SETUP(name) { ... }. */
#define BR_GAME_SETUP(name) void br_game_setup_##name(void); void br_game_setup_##name(void)

/* What the editor writes into the game (or the code it loads while you work). */
#if defined(_WIN32)
#  define BR_GAME_EXPORT __declspec(dllexport)
#else
#  define BR_GAME_EXPORT
#endif
BR_GAME_EXPORT void bronto_game_setup(void);

/* Called for every world br_builtin_register sets up (each scene a game plays,
 * each scene the editor opens), after the engine's own components and systems:
 * register the game's components and systems here. At most 64. */
typedef void (*br_world_hook)(br_world *w, void *user);
bool br_game_on_world(br_world_hook fn, void *user);

/* Forgets every hook (the editor, before it loads new code). */
void br_game_clear(void);
int  br_game_hook_count(void);

/* What br_builtin_register does with them. */
void br_game_run_world_hooks(br_world *w);

#endif /* BRONTO_GAME_H */
