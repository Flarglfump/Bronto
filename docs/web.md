# Bronto on the web

A Bronto game can run in a browser tab. A web build is **one `index.html` file**. The
engine (compiled to WebAssembly), the game's files and the small JavaScript runtime that
connects them to the browser are all inside it. You can open it straight from the disk,
put it on any web server, or upload it to a site like itch.io (as a zip holding
`index.html`).

No Emscripten or other SDK is involved. Plain `clang` can compile C to WebAssembly
(`--target=wasm32`). What a browser lacks is a C library, so Bronto brings a small one
of its own in `src/platform/web`. That keeps the engine's rule of no dependencies beyond
the compiler.

## Making a web build

**From the editor:** *Build > Build for > Web*, or *Build > Build and Open in Browser*.
The page is written to `builds/web/index.html` in the project. As with the desktop
builds, the editor checks every script first. The web player has to be beside the
editor (see below).

**From the command line:**

```sh
make web                          # the web player, plus a page for every example in build/web/
build/bronto_web my_project       # a project's page -> my_project/builds/web/index.html
build/bronto_web --wasm game.wasm --title "My Game" -o page.html   # a game written in C
build/bronto_web my_project --replay run.brec -o watch.html        # a page that plays a recorded run
```

A page made with `--replay` plays the recording back instead of the game (see
[replay.md](replay.md)). Its button says **Watch**, the playback keys are listed on it,
and `#seg=N` at the end of the page's address starts at segment N.

`make web` writes the following to `build/web/`:

| File | What it is |
|---|---|
| `bronto_player.wasm` | the web player: the engine plus `tools/player`, which runs projects made in the editor |
| `bronto.js` | the browser side of the runtime (`tools/web/bronto.js`) |
| `shell.html` | the page template (`tools/web/shell.html`) |
| `snake/index.html`, ... | each example as a page: open one in a browser |

The editor finds the web player in `web/` beside its own program, which is where
`make web` leaves it when the editor is `build/editor`. It also looks in `players/web/`,
so a computer that can't build WebAssembly can use a copy of that folder made on one
that can.

### What you need

Building the web player needs `clang` with its WebAssembly target and `wasm-ld` (LLVM's
linker):

- **Linux:** the `clang` and `lld` packages.
- **macOS:** `brew install llvm lld`. Apple's own clang has no WebAssembly target.
  The Makefile finds Homebrew's clang by itself, and `make web WASM_CC=/path/to/clang`
  uses another one.
- **Windows:** LLVM from llvm.org. Then run `build.bat web` from a Developer Command
  Prompt.

Opening a page needs nothing beyond a current browser (Chrome, Edge, Firefox or Safari).

## Games written in C

Any game that runs with Bronto on the desktop also builds for the web. The only thing to
change is how the main loop is written. In a browser, the page owns the loop: the game
can't sit in a `while` loop; it has to hand each frame back to the browser. So use
`br_engine_loop` instead of looping yourself:

```c
int main(int argc, char **argv) {
    br_engine *e = BR_NEW(br_engine);          /* on the heap, not main()'s stack */
    br_engine_config cfg = br_engine_default_config();
    cfg.initial_scene = &k_title;
    if (!br_engine_init(e, &cfg)) return 1;
    br_engine_loop(e, NULL);                   /* runs until the game quits, then shuts down */
    br_free(e);                                /* (desktop only: on the web nothing after the loop runs) */
    return 0;
}
```

On the desktop, `br_engine_loop` runs frames until the game quits and then returns. On
the web, `main()` ends at that call and the browser asks for a frame on every screen
refresh. When the game quits, the engine shuts down (`on_shutdown` still runs) and the
page shows that the game has ended. `br_engine_run(&cfg)` does all of this in one call.
The optional second argument of `br_engine_loop` is a function to run after each frame.

To compile a game, put its files together with the engine's WebAssembly objects. The
Makefile's `$(WEB)/%.wasm` rule shows the flags. Then make its page with
`bronto_web --wasm`.

## What works how

| | On the web |
|---|---|
| Picture | The framebuffer is drawn into a `<canvas>` that fills the page (or the frame it's embedded in), with the window's scale modes and HiDPI as on the desktop. |
| Keyboard, mouse, wheel | As on the desktop, with the same key codes. The browser keeps a few of its own shortcuts (F5, F11, F12, Ctrl+R, Ctrl+W...). |
| Touch | Real multi-touch (`BR_EVENT_TOUCH_*`). A project's `touch_controls` (see below) puts an on-screen stick and buttons in the bars around the game; a touch that misses them is the left mouse button. The page doesn't scroll, zoom or pop up menus under a finger. |
| Gamepads | Through the browser's Gamepad API, in the usual Xbox-style layout, with rumble where the browser supports it. |
| Sound | WebAudio. Browsers only start sound after a click or key press, which is why the page opens with a **Play** button. |
| Saved files | Saved data (`save.h`, and `save_data` in scripts) and any other files the game writes are kept in the browser's local storage for that page, and are there on the next visit. Saved data lives in `/saves/<game>`. The game's own files come from its pack and are read-only. |
| Clipboard | Copying puts text on the system clipboard. Pasting gives the text of the last paste into the page (browsers don't let pages read the clipboard freely). |
| Window title, icon, fullscreen, cursor | Set the tab's title and icon, the page's fullscreen mode, and the cursor over the canvas. |
| Folder dialogs, starting programs | Not available on the web (`br_pick_folder` reports no dialog; `br_launch` fails). `br_open_path` opens web addresses in a new tab. |
| Output capture (`capture.h`) | Not available. stdout and stderr go to the browser's console. |
| `max_fps` | The browser calls once per screen refresh; on faster screens, frames are skipped to keep to `max_fps`. |

Runs are as deterministic as on the desktop. `make web-test` plays the examples headless
as WebAssembly and checks that every picture and every sound sample matches the desktop
build exactly.

## Phones and tablets

A game made in the editor can be played on a phone or tablet, in its browser, with
on-screen controls. List them in **Build > Project Settings > Touch controls**
(`touch_controls` in `project.bronto`):

```
touch_controls = stick, a, b, x, y, lb, rb, back, start
```

- **They are a gamepad.** The stick is the left stick and the buttons are the gamepad's
  buttons, so whatever a game does with a gamepad, it does with these: scripts read
  them with `stick()` and `button_pressed("a")`. For a game that only reads the keyboard,
  each control can press a key instead, with its own label: `stick=arrows, a=space:JUMP`.
- **They appear when the screen is first touched** (that touch is also a click), and
  go again when a key or a real gamepad is used. While they are in use, `last_input()`
  is `"touch"`, so prompts can name their buttons.
- **They go where there is room.** Held sideways, the stick is at the bottom left, A B X Y
  are a diamond at the bottom right and the shoulder and menu buttons are at the top
  corners, all in the black bars beside the game (on a tablet, where the bars are narrow,
  the game shrinks a little, to 60% at most). Held upright, the game moves to the top and
  the controls go below it. They are drawn over the whole page at the screen's own
  resolution, so they stay sharp however small the game's pixels are.
- **Fingers:** any number at once (the stick and a button together); a finger can slide
  from one button onto the next. A touch that misses every control is the left mouse
  button, so menus that take clicks take taps.
- **The page** goes fullscreen when **Play** is tapped on a touch screen (where the browser
  allows: Android does; an iPhone's Safari keeps its bars), keeps clear of the notch, and
  doesn't scroll, zoom or select text. Added to the home screen, it opens as an app.
- **Scripts** can hide them for a moment (`show_touch_controls(false)`) or change them
  (`set_touch_controls("stick, a")`); see [scripting.md](scripting.md).
- **On a desktop**, `bronto_player --touch <project>` shows them with the mouse as a
  finger, for trying a layout. In C, the controls are `engine->touch` (`touch.h`), set up
  with `br_engine_config.touch_controls`.

## How it fits together

```
src/platform/web/include/    C library headers (stdio.h, stdlib.h, string.h, math.h, setjmp.h...)
src/platform/web/libc.c      strings, conversions, sorting, time, exit
src/platform/web/stdio.c     printf/scanf, FILE, the console
src/platform/web/memfs.c     the in-memory file system behind fopen and fs.h
src/platform/web/malloc.c    the memory allocator
src/platform/web/math.c      maths (the transcendental functions come from the browser)
src/platform/web/sjlj.c      what setjmp/longjmp need (clang turns them into WebAssembly exceptions)
src/platform/web/tag.s       the exception tag longjmp throws with (LLVM 20 and later expect the C library to define it)
src/platform/web/start.c     main(), and leaving it when the browser takes over the loop
src/platform/web/bw.h        everything the C side imports from bronto.js, in one list
src/platform/platform_web.c  the platform backend: canvas, input, audio, gamepads
src/platform/fs_web.c        fs.h over the in-memory file system
tools/web/bronto.js          the browser side: loads the program, supplies its imports, sends it events
tools/web/shell.html         the page template
tools/web/webpage.c          makes the one-file page (used by the editor and bronto_web)
tools/web/run_node.js        runs a Bronto WebAssembly program in Node (tests, tools)
```

`printf` and `strtod` give exactly the same results as glibc, including rounding.
`make web-test` builds one program both ways and compares the output line by line.

## Testing

```sh
make web-test
```

This runs the following:

- the whole test suite, compiled to WebAssembly, in Node;
- the C library check above;
- `tools/web/test/web.test.js`, which covers:
  - the web player drawing the same frames as the desktop player;
  - the C examples matching the desktop picture for picture and sound for sound;
  - the pages holding what they should;
  - a game starting, drawing, taking key presses and ending in a stand-in browser;
  - saved files surviving to the next run.
