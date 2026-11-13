# C code in a project

A project made in the editor can have C code as well as scripts: `.c` and `.h` files in
its **code folder** (`src/` unless Build Settings says otherwise). The code uses the
engine's C API (`#include "bronto/bronto.h"`), the same one the engine's own examples use,
and it's part of the game everywhere:

- **In the editor.** The editor compiles the code into a library and loads it, so the
  components it defines are in the inspector's **Add component** and in the scene files,
  and its systems and behaviours run when you press **Play**. When a `.c` or `.h` file
  changes (saved in the editor's code tab or anywhere else), it's compiled and loaded
  again within a couple of seconds, and the scene is made again with the new code.
  **Build > Compile C Code** (F7) does it at once.
- **In games.** **Build** compiles the code into the game's own copy of the player, with
  the whole engine, for this computer or for the web. A game with C code for another
  system is built on that system (open the project in the editor there, or run
  `editor --build` there).

`examples/orbits` is a small project like this: an **Orbit** component with the system
that moves it, and a **Twinkle** behaviour.

```sh
make editor SCENE=examples/orbits      # edit it (Play runs the C code)
make orbits                            # build the game for this computer
make orbits TARGET=web                 # ... or as a web page
```

## Starting

**+ C File** (under Files, or **New C File** in the files' right-click menu) writes
`src/game.c` from a template, with a component, a system and a behaviour, and opens it in
the code tab. Press F7 and the **GameSpin** component is in **Add component**; give an
entity a GameSpin and press Play to see it turn.

## How the code joins in

The code joins in through **setup functions**. Each file can have any number. They run
once before the first scene (and in the editor each time the code is compiled again):

```c
#include "bronto/bronto.h"

BR_GAME_SETUP(enemies) {
    br_game_on_world(add_enemy_parts, NULL);                 /* components and systems */
    br_script_register_behaviour("Chaser", &k_chaser);      /* behaviours */
}
```

The editor finds them itself: it looks for `BR_GAME_SETUP(` in the code folder's `.c`
files and writes `build/native/setup.c`, which calls each of them, so there is no list to
keep. A setup's name must be unique in the project (it's a C identifier).

### Components and systems

Components are registered per world, and every scene is a world of its own (the editor's
scenes too), so a setup asks to be called for each one with `br_game_on_world`.
`br_builtin_register` calls it after the engine's own components and systems:

```c
typedef struct Health { int hp; float regen; } Health;

static const br_field k_health_fields[] = {
    BR_FIELD(Health, hp, BR_FIELD_INT, 0, 999),
    BR_FIELD(Health, regen, BR_FIELD_FLOAT, 0, 10),
};
static const Health k_health_default = { 100, 0.5f };
static const br_component_desc k_health = BR_COMPONENT_DESC(Health, k_health_fields, &k_health_default);

static void regen_system(br_world *w, float dt, void *user) {
    br_component with[1];
    br_query q;
    (void)user;
    with[0] = br_component_find(w, "Health");
    q = br_query_make(w, with, 1);
    while (br_query_next(&q)) BR_QUERY_GET(&q, 0, Health)->hp += ...;
}

static void add_health(br_world *w, void *user) {
    (void)user;
    br_component_register_desc(w, &k_health);                /* Add component > Health */
    br_system_add(w, "regen", BR_PHASE_FIXED_UPDATE, regen_system, NULL, 0);
}

BR_GAME_SETUP(health) { br_game_on_world(add_health, NULL); }
```

A **described** component (`br_component_desc`, see `ecs.h`) is saved in scene files and
edited in the inspector with no more code: its fields can be ints, floats, bools, vec2s,
vec3s, colours, strings, enums and references to other entities (`BR_FIELD_ENTITY`, which
scene files keep connected).

Systems run in their phase (`BR_PHASE_FIXED_UPDATE`, `UPDATE`, `LATE_UPDATE`, `RENDER`),
in Play and in the game, never while you edit. `w->context` is the engine (`br_engine *`),
for its input (`br_key_down(&engine->input, BR_KEY_SPACE)`), renderer and mixer.

### Behaviours

A behaviour is C code for one entity at a time, like a script: an entity whose **Script**
component's `file` is the behaviour's name runs it. Each entity gets its own state
(`state_size` bytes, zeroed), and any of the callbacks can be left out:

```c
typedef struct chaser { float speed; } chaser;

static void chaser_update(br_world *w, br_entity self, void *state, float dt) { ... }

static const br_behaviour k_chaser = { sizeof(chaser), NULL, chaser_update, NULL, NULL, NULL };
/* start, update, on_collision, on_trigger, on_destroy */
```

Scripts and C code mix freely: a scene can have scripted entities and C components side
by side, and scripts can read and change C components' fields like the engine's.

## Build Settings

**Build > Build Settings** has a **C code** section (saved in `project.bronto`):

| Setting | Key | |
|---|---|---|
| Code folder | `code_dir` | where the `.c` and `.h` files are (default `src`). Its `.c` and `.h` files aren't packed into games: they're compiled into them |
| Compiler | `compiler` | the C compiler for this computer. Empty: the first found (see below) |
| Web compiler | `web_compiler` | clang with WebAssembly, for web builds. Empty: the first found |
| Compiler options | `cflags` | added to every compile: `-DLEVELS=3 -Ivendor` (with cl: `/DLEVELS=3`) |
| Linker options | `ldflags` | libraries and linker options: `-lsqlite3` (with cl: `sqlite3.lib`) |
| Debug build | `debug_build` | `-O0 -g` (cl: `/Od /Zi`) instead of `-O2`: for running the game in a debugger |

The code is compiled as C99 (`-std=c99`; a `-std=` in the compiler options wins), with
`-Wall`, the engine's headers and the code folder on the include path.

### Which compiler

| System | Found first |
|---|---|
| Linux, BSD | `cc`, `gcc`, `clang` |
| macOS | `cc`, `gcc`, `clang` (Apple's command line tools: `xcode-select --install`) |
| Windows, editor built with MSVC (`build.bat`) | `cl` on the PATH, else Visual Studio's, found with `vswhere` (so the editor started from the Start menu finds it too) |
| Windows, editor built with MinGW (`make`) | `gcc`, `clang` |
| Web (any system) | Homebrew's LLVM clang on a Mac, `C:\Program Files\LLVM\bin\clang.exe`, then `clang` |

On Windows, the code the editor loads has to be compiled by the same kind of compiler as
the editor (MSVC for an editor built with `build.bat`, MinGW for one built with `make`),
because it takes the engine's functions from the editor through its import library
(`editor.lib` / `libeditor.a`, beside the editor).

### What goes where

Everything the compiles make is in the project's `build/native/` (which isn't packed):

```
build/native/setup.c             bronto_game_setup(), calling every BR_GAME_SETUP
build/native/editor/game_N.so    the code the editor loaded (.dylib, .dll)
build/native/Linux/              this computer's player with the code in (macOS/, Windows/ elsewhere),
                                 and the engine and player objects, compiled once per project and compiler
build/native/Web/                the web player with the code in, its runtime, and the engine's objects
```

The engine itself is compiled once per project (it takes a few seconds), and again only
when the compiler, the options or the engine change; after that only the game's own code
is compiled.

The engine's sources and headers come with the editor: from `make install`, in `sdk/`
beside it (`Contents/Resources/sdk` in the Mac app); run from the repository's `build/`,
the repository itself.

## Mistakes

The compiler's errors and warnings go to the console, with their file and line: a
double-click opens the file there in the code tab, which marks the line, as does the
file's preview under Files. While the code doesn't compile, the editor keeps running the
last code that did.

If a scene was opened while its C components were unknown (the code had never compiled),
those components were left out of it; the editor won't save over the scene then (that
would lose them). Once the code compiles, the scene is read again from its file, with
them.

## Notes

- The editor loads a new copy of the library each time the code is compiled, and keeps
  the old ones loaded (anything still pointing into them stays safe). Static variables
  start again with each copy; anything that must survive belongs in components.
- Behaviours are registered by name: a behaviour that's gone from the code stays
  registered (as it last was) until the editor restarts.
- On Linux the editor is linked with `-rdynamic` and the whole engine, so the library can
  use any engine function. On macOS it's linked with `-undefined dynamic_lookup`.
- `editor --build` compiles the code too, for the command line and CI:
  `editor --build host my_game` (see [editor-cli.md](editor-cli.md)).
