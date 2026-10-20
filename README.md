# Bronto

A small 2D game engine in C99 with **no dependencies beyond the C standard library and
the operating system itself**. OS-specific code is kept in a separate platform layer,
and the math and render APIs are designed so that 3D can be added later.

Status (v0.1):
- **Done:** the core systems; native windows, sound output and gamepads on Windows,
  macOS and Linux; and two sample games, Snake and Pop Defense.
- **Not yet:** the web build. See the roadmap below.

## Quick start

```sh
make snake         # build and play Snake
make towerdefense  # build and play Pop Defense (tower defense)
make test          # build libbronto.a and run the test suite
make examples      # build examples/ into build/examples/
make sanitize      # tests under AddressSanitizer + UndefinedBehaviorSanitizer
make x11-test      # Linux: drives a real X11 window (falls back to Xvfb)
make audio-test    # Linux: plays a tone through ALSA and checks what reached the device
make evdev-test    # Linux: feeds kernel gamepad events through the evdev backend
```

On Windows with MSVC, run `build.bat` from a Developer Command Prompt; it builds
`build\snake.exe` and `build\towerdefense.exe`. With MinGW, use `make`.

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
bonus and on death (Windows and Linux). `snake --autopilot` lets the AI play. `snake --headless` plays a game with no
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

## Platforms

| Target | Window | Sound | Gamepads | How it is built | Verified |
|---|---|---|---|---|---|
| Linux / BSD | X11 | ALSA (works with PulseAudio / PipeWire) | evdev, with hot-plug and rumble | X11 and ALSA are loaded at run time with `dlopen`, so there are no X11 or ALSA headers; only `-ldl` | Integration tests against a real X server and through ALSA (the captured output is checked for pitch, level and dropouts); gamepad mapping tested by feeding kernel events for Xbox, PlayStation and generic pads |
| Windows | Win32 + GDI, per-monitor DPI aware | waveOut (winmm, loaded at run time) | XInput, with rumble | Links user32 + gdi32 | Compiles and links against mingw-w64 headers |
| macOS | Cocoa from plain C through the Objective-C runtime, presented with Core Animation | Core Audio (AudioQueue) | GameController framework (Xbox, PlayStation, Switch Pro, MFi); no rumble yet | `-framework Cocoa -framework QuartzCore -framework AudioToolbox -framework GameController` | Compiles and links for arm64 and x86_64 against the macOS 14.5 SDK; constants, key codes and GameController selectors checked against the SDK |
| Any | Headless | Captured in memory | Simulated (`br_headless_set_gamepad`) | Built in | Unit tests and CI |
| Web | Emscripten + canvas | WebAudio | Gamepad API | Planned | |

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
src/platform/        headless + native backends (x11, win32, macos)
src/bronto.c         unity build
tests/               unit tests; tests/platform/ holds backend checks
examples/bounce/     every system in a small toy
examples/snake/      sample game: Snake
examples/towerdefense/  sample game: Pop Defense (tower defense)
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
| Render | `render.h` | Software rasterizer: shapes, triangles (including per-vertex colour), affine sprites, 4 blend modes, 2D camera, 8x8 font, BMP/TGA |
| Audio | `audio.h` | 32-voice mixer with volume, pan, pitch, looping, ramps, buses and resampling; WAV loader; tone generator; lock-free ring for audio threads |
| Window | `window.h` | Viewport and scale modes, mouse mapping, window state and control |
| Platform | `platform.h` | Table of function pointers that each OS backend implements |
| Engine | `engine.h` | Fixed-timestep loop, frame limiter, per-frame arena; `br_engine_frame()` runs one frame at a time so a browser can drive the loop |

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

1. Web backend (Emscripten, canvas, WebAudio, Gamepad API)
2. PNG decoder, sprite sheets and animation, tilemaps
3. Collision and physics helpers, asset manager, scene serialisation
4. 3D path: depth buffer and perspective-correct triangles on the existing mat4/quat math
5. GUI editor
