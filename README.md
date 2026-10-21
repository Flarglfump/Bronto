# Bronto

A small 2D game engine in C99 with **no dependencies beyond the C standard library and
the operating system itself**. OS-specific code is kept in a separate platform layer,
and the math and render APIs are designed so that 3D can be added later.

> **Note:** much of the code in this project was written with Claude Opus 5.5, an AI
> model made by Anthropic.

Status (v0.1):
- **Done:** the core systems; native windows, sound output and gamepads on Windows,
  macOS and Linux; web builds (WebAssembly, with no Emscripten); two sample games, Snake
  and Pop Defense; and a visual scene editor.
- **Next:** see the roadmap below.

## Quick start

```sh
make snake         # build and play Snake
make towerdefense  # build and play Pop Defense (tower defense)
make editor        # build and run the scene editor (make editor SCENE=level.scene)
make tools         # the editor, bscheck and bronto_player (which the editor's Build menu needs)
make game GAME=examples/pantryquest   # pack a project into a game: build/games/pantryquest/<system>/
                   #   (TARGET=web|linux|macos|windows; default: this computer)
make pantryquest   # the same for Pantry Quest
make test          # build libbronto.a and run the test suite
make examples      # build examples/ into build/examples/
make sanitize      # tests under AddressSanitizer + UndefinedBehaviorSanitizer
make web          # web builds: build/web/<example>/index.html, and the web player (needs LLVM's clang)
make web-test      # the test suite as WebAssembly in Node, and web-specific tests
make x11-test      # Linux: drives a real X11 window (falls back to Xvfb)
make audio-test    # Linux: plays a tone through ALSA and checks what reached the device
make evdev-test    # Linux: feeds kernel gamepad events through the evdev backend
```

On Windows with MSVC, run `build.bat` from a Developer Command Prompt; it builds
`build\snake.exe`, `build\towerdefense.exe` and `build\editor.exe`. With MinGW, use `make`.
`build.bat web` makes the web builds (it needs LLVM's clang as well).

With no build system at all, the unity build compiles everything from one file:

```sh
cc -std=c99 -Iinclude src/bronto.c mygame.c -lm -ldl                         # Linux
cc -std=c99 -Iinclude src/bronto.c mygame.c -framework Cocoa \
   -framework QuartzCore -framework AudioToolbox -framework GameController        # macOS
gcc -std=c99 -Iinclude src/bronto.c mygame.c -luser32 -lgdi32                # MinGW
```

## Snake

`examples/snake` is a complete game and a tour of the engine. It is laid out like Pop
Defense:

- **Rules, one part per file:** `snake_body.c` (the snake on the grid), `snake_food.c`
  (apples and the bonus), `snake_game.c` (one tick of play and steering) and
  `snake_autopilot.c` (a computer player). The rules never call the engine. They are
  deterministic and unit-tested.
- **Events:** the rules record what happened (`ATE`, `ATE_BONUS`, `DIED`, `WON`).
  `main.c` forwards those to the message bus as `snake.event`. The effects, the sound
  and the game-over overlay subscribe to them, so none of them is hard-wired into the
  rules.
- **Scenes:** title, play, and pause and game-over overlays.
- **ECS:** particles and score pop-ups are entities with components (`view_fx.c`).
- **Audio:** every sound effect and the music loop is synthesised in code (`sfx.c`), so
  there are no asset files.
- **Window:** integer scaling keeps the pixels crisp, and F11 / Alt+Enter toggles
  fullscreen.

Controls: arrows / WASD / gamepad d-pad or left stick to move, P / Esc / Start to pause,
Enter or A to start, M to mute the music, F11 for fullscreen. Controllers rumble on a
bonus and on death (Windows and Linux). The high score is kept with the game's
[saved data](#saved-data). `snake --autopilot` lets the AI play. `snake --headless` plays a game with no
window and writes screenshots and a WAV file. See
[examples/snake/README.md](examples/snake/README.md) for the code tour.

## Pop Defense

`examples/towerdefense` is a tower defense game in the style of Bloons TD 5 and 6: one map,
30 rounds, five towers with two upgrade paths each, and layered balloons that can be camo,
lead, ceramic or a blimp. It is built to be easy to follow:

- **One file per part of the rules:** data tables, map, balloons, towers, projectiles,
  rounds, and `td_game.c`, which holds the game loop and the player's commands. The rules
  never call the engine. They are deterministic and covered by unit tests, including a
  full game played by a scripted autopilot.
- **One-way data flow:** input becomes rule commands. The rules record what happened as
  events, and `main.c` forwards those to the engine's message bus. The ECS effects and
  the sound module subscribe to them.
- **Upgrades are tiny functions** that change a tower's stats, so a new upgrade is a
  three-line change.

Mouse, keyboard and gamepad are all supported. `--autopilot` lets the scripted player play,
and `--headless` plays all 30 rounds at triple speed with no window. See
[examples/towerdefense/README.md](examples/towerdefense/README.md) for the code tour and
the rules.

## Scene editor

`tools/editor` is a visual editor for scenes, built on the engine itself:

- **Projects:** **Load Project** opens a folder with the system's folder dialog (Windows
  and macOS; on Linux zenity or kdialog if installed, else a picker drawn by the editor).
  Its `main.scene` opens if it has one. `editor <folder>` does the same.
- **Hierarchy:** every entity in the scene. Add, copy and delete them, and drag rows to
  reorder.
- **Viewport:** the scene exactly as a game draws it, with a grid and a frame showing
  the game's screen. Scroll to zoom, right-drag to pan, click to select. Drag to move,
  and use the handles to rotate and scale. Snap is optional.
- **Inspector:** the selected entity's components, with a widget for every field. It is
  generated from component descriptions, so any described component is editable with
  no editor code.
- **Panels:** drag the edge between two panels to resize them. The arrow in a panel's
  corner collapses it to a thin strip (click the strip to bring it back); dragging an edge
  nearly shut or double-clicking it does the same. The layout is remembered.
- **Play / Stop:** runs the scene's systems in place. Stop puts everything back exactly
  as it was.
- **Assets:** a file browser for the project folder, with image thumbnails and a
  preview (image size, sound length with Play, scene entity count). Drag images, sounds
  and scenes into the view: an image becomes a sprite, a sound a Sound entity, and a
  scene is placed as a prefab copy. Drop onto Sprite.image or Sound.file in the
  inspector to fill them in. New folder, rename, delete (to a `.trash` folder),
  double-click to open a folder or scene or to hear a sound, and **To prefab** for the
  selected entity. Scripts (`.bs`) show their first lines and any mistake; drag one onto an
  entity to attach it, double-click to edit it in your text editor, or **+ Script** for a
  new one.
- **Build menu:** a game for this computer (or others, given their players) in one file,
  after checking every script; **Project Settings** for the game's name, first scene, screen
  size and strict typing. See [Building games](#building-games). The same build runs
  without a window: `editor --build <target> <project> [--out folder]` (or `make game`).
- **Physics and scripts** run when you press Play. **Colliders** outlines every collider,
  and script errors appear in the status bar and the console.
- **Right-click menus** everywhere: hierarchy rows and empty space, objects and empty
  space in the view, files and folders, inspector sections and fields, console lines.
  They hold the actions for that thing (rename, add child, cut / copy / paste, add
  component, reset, open, show in the file manager, ...).
- **Console:** a tab next to the file browser. Everything the editor writes to stdout and
  stderr is captured there (and still reaches the terminal), along with the engine's
  log and `print()` from scripts, coloured by level with filters, search and repeat
  counts. Double-click a script error to open the script at that line.
- **Clipboard:** text fields take Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+A (Cmd on a Mac) with the
  system clipboard. Copied entities go on it as scene text, so they paste into another
  editor window; menus copy console lines and file paths.
- **Undo / redo**, and a prompt to save before unsaved changes are lost.

Scenes are saved as readable text files that games load with one call; see
[tools/editor/README.md](tools/editor/README.md).

## The scene tree

Entities form a tree, like Godot's nodes or Unity's GameObjects. A **Parent** component makes
an entity a child of another, and its Transform is then relative to that parent. Moving,
turning or scaling a parent carries its whole subtree, and destroying a parent destroys its
children. Drawing, picking and physics all use the world transform
(`br_world_transform`). Scene files save the tree, prefabs keep their subtrees, and scripts
see it as `parent`, `children()` and `global_position`. In the editor, drag rows in the
hierarchy to reparent and reorder, and fold rows with their arrows (see `hierarchy.h`).

## Physics and scripts

Add a **Collider** (a box or a circle) to make something solid, and a **RigidBody** to
make it move: gravity, bouncing, friction, tumbling and stacking. A collider marked
`trigger` reports what enters it instead of blocking it. A **Script** component runs a
BrontoScript file, a small language built into the engine:

```
var speed: number = 90                # types are optional, and checked before it runs
func update(dt: number) -> void {
    if key_down("right") { self.velocity.x = speed }
    if key_pressed("space") and raycast(self.position, self.position + vec2(0, 12), self) != nil {
        self.velocity.y = -280
    }
}
func on_trigger(other) { if other.name == "Coin" { destroy(other) } }
```

`examples/platformer` is a complete level made this way, with no C code: open it with
**Load Project** and press Play. `examples/pantryquest` is a whole adventure game made the
same way: a top-down quest across a land of 128 screens built from Tilemaps, with caves,
shops, secrets, cellars with keys, maps, compasses and bosses, and its own art, sounds and music. The language is
described in [docs/scripting.md](docs/scripting.md). When speed matters, a script can
instead be a behaviour written in C (`br_script_register_behaviour`).

For editing scripts, **Settings** in the editor chooses the text editor that opens them,
with presets for VS Code, Sublime Text, Notepad++ and Zed. `tools/vscode` is a VS Code
extension for `.bs` files with highlighting, completion, hover docs, signature help, go to
definition and checking as you type (`make vscode` packages it). `bscheck` (`make tools`)
checks scripts from the command line.

Scripts can be typed, like GDScript: `var hp: int = 3`, `var spot := vec2(1, 2)`,
`func hit(amount: int) -> bool`, `list[vec2]`. Types are checked before a script runs and
again when values arrive from untyped code. `@strict` at the top of a file, or **Strict
typing** in the project's settings, makes them required everywhere.

## Building games

The editor's **Build** menu turns a project into a game that runs without the editor:

- **Build for <this computer>** (Ctrl+B) checks every script (strictly, if the project asks
  for it; mistakes go to the console), then writes the game to `builds/<system>/` in the
  project: on Linux and Windows one program file, on macOS a `.app`. **Build and Run**
  (Ctrl+Shift+B) starts it too.
- A game is `bronto_player` (`tools/player`, built by `make tools` next to the editor) with
  the project packed into it (`pack.h`: every scene, script, image and sound in one file),
  so there is nothing else to install.
- **Build for > Windows / macOS / Linux**: a player for another system is built on that
  system and copied into a `players/` folder beside the editor, named
  `bronto_player-windows.exe`, `bronto_player-macos` or `bronto_player-linux`.
- **Build for > Web** writes `builds/web/index.html`: one file that holds the web player,
  the project and the page. Open it in a browser, or put it on any web site. **Build and
  Open in Browser** does both. It uses the web player that `make web` puts beside the
  editor (see [docs/web.md](docs/web.md)).
- **Project Settings** (saved in `project.bronto`): the game's name, the scene it starts
  with, its screen size and window scale, strict typing, an icon, the builds folder, and what to leave out of builds
  (`exclude = tools, notes.txt`: folders and files the pack skips).

- **Open Save Folder** shows where the game keeps its [saved data](#saved-data). The editor's
  Play uses the same folder as the built game.

`bronto_player <project folder>` also runs a project straight from its files, and
`--headless --frames N` runs it with no window (for tests). Scripts move between scenes
with `load_scene("levels/2.scene")` and end the game with `quit()`; in the editor's Play,
those swap the scene until Stop and stop playing.

## Saved data

Games keep save games, high scores and options in one place per game, the place each
system has for a user's data. `save.h` gives every game the same calls, on every system:

| System | Where |
|---|---|
| Windows | `%APPDATA%\<game>` (`C:\Users\you\AppData\Roaming\<game>`) |
| macOS | `~/Library/Application Support/<game>` |
| Linux / BSD | `$XDG_DATA_HOME/<game>`, or `~/.local/share/<game>` |
| Web | the browser's storage for the game's page (localStorage) |

`<game>` is the game's name: `br_engine_config.save_name`, or else its title. For a project
from the editor it is the project's name, so the editor's Play and the built game share
it. **Build › Open Save Folder** in the editor opens the folder.

```c
br_save_write("slot1.sav", &state, sizeof(state));     /* replaces the whole file, safely */
size_t size;
void *data = br_save_read("slot1.sav", &size);           /* NULL if there's none; br_free it */
br_save_exists("slot1.sav");  br_save_delete("slot1.sav");  br_save_list(&files);
```

- **Safe:** a save is written beside the old one and then swapped in, in one step (and on
  the disk before the swap). A crash or power cut part-way through leaves the old save,
  never half of the new one. On the web, the browser's storage always holds one whole
  copy.
- **Names, not paths:** names are file names of up to 64 letters, digits, spaces and
  `_-.()`. Anything else is refused, so a name can't reach outside the game's folder.
- **Scripts** use the same folder with `save_data("slot1", value)` and `load_data("slot1")`.
  The value can be a number, string, true/false, vec2 or list, and is kept as readable text
  ([docs/scripting.md](docs/scripting.md)).
- **Elsewhere:** `br_save_set_folder()`, or the `BRONTO_SAVE_DIR` environment variable, keeps
  saved data in another folder: for tests, or a copy of a game that runs from a USB stick.

## Platforms

| Target | Window | Sound | Gamepads | How it is built | Verified |
|---|---|---|---|---|---|
| Linux / BSD | X11 | ALSA (works with PulseAudio / PipeWire) | evdev, with hot-plug and rumble | X11 and ALSA are loaded at run time with `dlopen`, so there are no X11 or ALSA headers; only `-ldl` | Integration tests against a real X server and through ALSA (the captured output is checked for pitch, level and dropouts); gamepad mapping tested by feeding kernel events for Xbox, PlayStation and generic pads |
| Windows | Win32 + GDI, per-monitor DPI aware | waveOut (winmm, loaded at run time) | XInput, with rumble | Links user32 + gdi32 | Compiles and links against mingw-w64 headers |
| macOS | Cocoa from plain C through the Objective-C runtime, presented with Core Animation | Core Audio (AudioQueue) | GameController framework (Xbox, PlayStation, Switch Pro, MFi); no rumble yet | `-framework Cocoa -framework QuartzCore -framework AudioToolbox -framework GameController` | Compiles and links for arm64 and x86_64 against the macOS 14.5 SDK; constants, key codes and GameController selectors checked against the SDK |
| Any | Headless | Captured in memory | Simulated (`br_headless_set_gamepad`) | Built in | Unit tests and CI |
| Web | `<canvas>` (WebAssembly, built with plain clang, with Bronto's own small C library) | WebAudio | Gamepad API, with rumble where the browser has it | `make web` / `build.bat web`: clang `--target=wasm32` and wasm-ld; no Emscripten ([docs/web.md](docs/web.md)) | The whole test suite runs as WebAssembly in Node; the C library's output is compared with glibc's; the examples give picture- and sample-identical results to the desktop builds; the pages are played in Chromium with keyboard, mouse and gamepad |

On Windows, games built as above are console-subsystem programs, so log output shows up
in a console window. Link with `-mwindows` (or `/SUBSYSTEM:WINDOWS`) to hide it.

## Window management

The game draws into a fixed framebuffer (320x180 by default). The window can be any
size, and the engine places the framebuffer inside it using one of these scale modes:

| Mode | Behaviour |
|---|---|
| `BR_SCALE_FIT` (default) | Largest size that keeps the aspect ratio, with black bars filling the rest |
| `BR_SCALE_INTEGER` | Largest whole-number multiple that fits, so every pixel is the same size |
| `BR_SCALE_STRETCH` | Fills the whole window, ignoring aspect ratio |
| `BR_SCALE_RESIZE` | The framebuffer itself follows the window size, so a bigger window shows more of the world |

What the engine handles:

- **Mouse coordinates:** the platform reports positions in window pixels, and the
  engine converts them to framebuffer pixels before your code sees them. The raw
  window position is kept in `wx`/`wy`.
- **Window events:** close, resize, minimise, maximise, restore, move, focus,
  mouse enter/leave, fullscreen and HiDPI scale changes all arrive as events.
- **Closing:** a close request quits the game unless a scene consumes the event, so a
  game can ask "are you sure?" first.
- **Window control:** `br_window_set_title`, `set_size` (in points), `set_fullscreen`,
  `toggle_fullscreen`, `set_cursor_visible` and `set_scale_mode`. The live state is in
  `engine->window`.
- **Frame pacing:** a frame limiter (`max_fps`, default 60). While the window is
  minimised, updates pause and the loop slows to 20 frames per second (still fast
  enough to keep the sound buffer full).
- **HiDPI:** window sizes are given in points. On a Retina display or at 150% Windows
  scaling, the engine works in physical pixels.

All of the scaling and state logic lives in portable code (`src/core/window.c`) and is
unit-tested on the headless backend's simulated window. Each OS backend only reports its
pixel size and copies the framebuffer into the rectangle the engine gives it.

## Layout

```
include/bronto/      public headers (bronto.h includes everything)
src/core/            platform-independent engine
src/platform/        headless + native backends (x11, win32, macos, web)
src/platform/web/    the web build's C library (no Emscripten)
src/bronto.c         unity build
tests/               unit tests; tests/platform/ holds backend checks
examples/bounce/     every system in a small toy
examples/snake/      sample game: Snake
examples/towerdefense/  sample game: Pop Defense (tower defense)
examples/platformer/    a level built only from the editor: physics + scripts, no C
examples/pantryquest/   an adventure game made of scenes, Tilemaps, prefabs and scripts, no C
tools/editor/        the visual scene editor
tools/bscheck/       checks .bs scripts from the command line
tools/player/        bronto_player: runs games (the editor's Build packs projects into it)
tools/vscode/        the BrontoScript extension for VS Code
tools/web/           the web runtime (bronto.js), the page template, bronto_web (makes pages)
docs/scripting.md    the BrontoScript reference
docs/web.md          web builds
```

## Architecture

| System | Header | What it does |
|---|---|---|
| Memory | `memory.h` | Tracked heap (`br_alloc`, leak stats, pluggable allocator), linear arena, fixed-size pool, file helpers |
| Math | `bmath.h` | vec2/3/4, column-major mat4 (ortho, perspective, look-at, inverse), quaternions, rects, deterministic RNG |
| Events | `event.h` | FIFO queue plus pub/sub bus. Handlers run by priority and can consume an event to stop it. Custom message types are registered at runtime and carry up to 48 bytes of payload |
| Input | `input.h` | Keyboard, mouse, text and 4 gamepads, built purely from events. Pressed/released edges are frame-accurate. Named actions bind to keys, buttons and stick directions. Gamepad snapshot diffing is shared by all backends; rumble goes through `br_gamepad_rumble` |
| ECS | `ecs.h` | Sparse-set storage with generation-checked entity handles. Components are registered at runtime. Queries take include and exclude lists. Supports deferred destruction, add/remove hooks, and systems ordered within 4 phases |
| Scenes | `scene.h` | Scene stack where each scene has its own ECS world. `transparent` and `update_below` flags handle overlays and modals. Push/pop/switch requests are deferred to a safe point |
| Render | `render.h` | Software rasterizer: shapes, triangles (including per-vertex colour; rasterised in fixed point so shared edges have no cracks or double pixels), affine sprites, 4 blend modes, 2D camera, 8x8 font, BMP/TGA |
| Audio | `audio.h` | 32-voice mixer with volume, pan, pitch, looping, ramps, buses and resampling; WAV loader; tone generator; lock-free ring for audio threads |
| Window | `window.h` | Viewport and scale modes, mouse mapping, window state and control, the app icon (`br_window_set_icon`: window, taskbar and Dock), and the system clipboard (`br_clipboard_set` / `br_clipboard_get`: Win32 `CF_UNICODETEXT`, macOS `NSPasteboard`, X11 `CLIPBOARD` selection; a private one in headless and web builds) |
| Platform | `platform.h` | Table of function pointers that each OS backend implements |
| Scene files | `serialize.h` | Saves and loads worlds as readable text. Every *described* component (a list of its fields with names and types, `ecs.h`) is saved field by field. Loading skips unknown components and fields with a warning, and is all-or-nothing on errors |
| Built-in components | `components.h` | Name, Transform, Shape, Sprite, Label, Motion, Bounds, Sound and Tilemap (a grid of tiles from one tileset image, one character per cell, with solid characters for scripts to test), with motion, audio and layered draw systems: enough to make and play simple scenes |
| Scene tree | `hierarchy.h` | Parent component, local and world transforms, reparenting that keeps the on-screen position, children, subtrees and cascading destruction. Scene files store entity references (`BR_FIELD_ENTITY`) as positions in the file, reconnected on load |
| Physics | `physics.h` | RigidBody (dynamic, static, kinematic), Collider (box or circle; friction, bounce, trigger, layers) and PhysicsWorld (gravity). Sort-and-sweep broad phase; circle and oriented-box contacts with clipping; a sequential-impulse solver, warm-started so stacks settle. Contact and trigger events, raycasts and point queries |
| Scripts | `script.h` | The Script component. BrontoScript is interpreted from `.bs` files, re-read when they change, with a garbage-collected heap. Errors give a file and line and stop only that script. Scripts read the keyboard, the mouse and gamepads (buttons by name, sticks, triggers, rumble). C behaviours can be registered by name |
| Assets | `assets.h` | Image and sound cache: each file is loaded once, at a stable address, with relative paths resolved against the project folder (the nearest folder holding `project.bronto`, else the scene's own). Forgetting a sound silences it first. Project settings (`br_project`: name, main scene, screen size, strict typing, what built games leave out) live in `project.bronto` |
| Packs | `pack.h` | A project folder in one file, or added to the end of a program (a built game). Once mounted, `br_file_read_all` finds files in it first, so the asset cache, scenes and scripts need no changes |
| File system | `fs.h` | List folders (folders first, sorted), stat, make folders, rename without overwriting, replace in one step, remove; the user's folders for settings and saved data; path string helpers. POSIX and Win32 backends in the platform layer |
| Saved data | `save.h` | A game's save files, by name, in the system's place for them (`%APPDATA%`, `~/Library/Application Support`, `~/.local/share`, the browser's storage), written safely; `save_data` / `load_data` for scripts |
| Output capture | `capture.h` | Redirects the program's own stdout and stderr into a buffer it reads back each frame (the editor's console), optionally still echoing to the terminal. Pipes plus a reader thread, in POSIX and Win32 backends in the platform layer; not available on the web (output goes to the browser's console) |
| GUI | `ui.h` | Immediate-mode widgets: buttons, checkboxes, number fields you drag or type into, vec2 and colour fields, text fields, drop-downs, list rows, collapsible sections, scrolling panels, clickable custom-drawn areas with double-click, drag and drop, and right-click context menus (submenus, ticks, shortcuts, keyboard navigation). The editor uses it, and so can games |
| Engine | `engine.h` | Fixed-timestep loop, frame limiter, per-frame arena; `br_engine_frame()` runs one frame, `br_engine_loop()` runs them until the game ends (on the web it hands them to the browser) |

## Gamepads

Up to 4 controllers. Every pad is presented with the same Xbox-style layout, whatever the
hardware. On a PlayStation pad, A = cross, B = circle, X = square and Y = triangle.
Sticks report -1..1 with +Y pointing down, matching screen coordinates. Triggers report
0..1.

```c
if (br_pad_pressed(&e->input, 0, BR_PAD_A)) jump();
float move = br_pad_axis(&e->input, 0, BR_PAD_AXIS_LEFT_X);          /* deadzone applied */
br_input_bind(&e->input, left, BR_BIND_PAD_AXIS_NEG, BR_PAD_AXIS_LEFT_X, -1);  /* any pad */
br_gamepad_rumble(e, 0, 0.8f, 0.3f, 0.25f);   /* heavy motor, light motor, seconds */
```

How it works:

- **One code path:** each backend reads controllers as snapshots, and the shared
  `br_gamepads_sync` (`src/core/input.c`) turns them into `GAMEPAD_*` events. Every
  platform therefore behaves the same, including connect and disconnect, releasing held
  buttons on unplug, and filtering out sensor noise.
- **Hot-plug:** pads can be connected or removed at any time. `br_pad_name()` gives
  the controller's name.
- **Rumble:** the engine switches the motors off when the duration ends, and on
  shutdown.
- **Linux:** access to `/dev/input` is normally granted automatically to the logged-in
  user (systemd "uaccess"). Otherwise, add the user to the `input` group.
- **Windows:** XInput covers Xbox pads, and others through Steam Input. Plain
  DirectInput pads are not supported.
- **macOS:** rumble needs Core Haptics and is not implemented yet.

## Sound output

The mixer runs on the game thread. Each frame, the platform reports how many frames the
device needs to get back up to the target latency, and the engine mixes exactly that
much and hands it over. `audio_latency_ms` sets the target (default 75 ms). Lower
values make sounds respond faster; higher values make the game more tolerant of frame
hitches.

- **Linux (ALSA):** non-blocking writes straight into the device buffer, with automatic
  recovery from underruns. Set `BRONTO_AUDIO_DEVICE` to choose a device (`null`
  discards sound, `file:'/tmp/out.raw',raw` captures it).
- **Windows (waveOut):** a ring of 5.8 ms blocks, filled from the game thread.
- **macOS (Core Audio):** AudioQueue pulls from its own real-time thread. The game
  thread feeds it through `br_audio_ring`, a lock-free single-producer /
  single-consumer queue (stress-tested with two threads and ThreadSanitizer). The
  callback never blocks; if the queue runs dry it plays silence.

If there is no sound device, the game logs a warning and keeps running silently.

Known limitation: on Windows, while the user drags the window border, the game is
frozen by the OS's modal sizing loop, so the sound pauses until the drag ends.

### Why software rendering and pushed audio?

These choices keep the "no external dependencies" rule while staying portable:

- **Video:** each backend only copies a finished framebuffer to the screen. That
  means `StretchDIBits` on Windows, a `CALayer` on macOS, `XPutImage` on Linux and a
  canvas on the web. None of these needs a GPU API.
- **Audio:** the mixer runs on the game thread and pushes PCM to the device. No locks
  are needed; where the OS insists on its own audio thread (Core Audio), a lock-free
  ring sits in between.

### Determinism

On the headless platform a run is fully reproducible, and the examples produce
bit-identical frames and audio on x86-64 and ARM64. This makes replays, lockstep
networking and golden-image tests practical. Two rules keep it that way:

- Build with `-ffp-contract=off` (the Makefile already does).
- Never pass two RNG calls as arguments to the same function, because C does not
  specify the order in which arguments are evaluated.

### Frame order

1. poll OS events
2. clock, window sync, map mouse positions to the framebuffer
3. dispatch events: window state and input, then scenes from top down, then the engine (quit/close)
4. fixed updates
5. update and late update
6. render, then present into the viewport
7. mix and submit audio
8. apply scene transitions
9. frame limiter

## Roadmap

1. PNG decoder, sprite sheets and animation (tilemaps are in)
2. Joints, one-way platforms and continuous collision for fast bodies; a script debugger
3. Editor: multi-select, copy/paste between scenes, nested prefabs that update, game-specific components
4. Web: touch controls beyond the mouse, and a smaller page (compressing the pack)
5. 3D path: depth buffer and perspective-correct triangles on the existing mat4/quat math
