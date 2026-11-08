# The editor from the command line

The editor (`build/editor`, or `build\editor.exe` from `build.bat`) is mostly used with its
window, but it takes a few command-line arguments too. They open a project or a scene
straight away, build a game with no window at all (the **Build** menu, for scripts and CI),
run the editor's own self-test, and save its icon.

```sh
editor                                   # a sample scene
editor examples/platformer               # open a project folder
editor levels/one.scene                  # open one scene (and the project it belongs to)
editor --build host examples/platformer  # build the game for this computer, no window
editor --build web my_game --out site    # build a web page into site/
editor --headless                        # the editor's self-test
editor --write-icon icon.png 256         # save the app icon as a PNG
editor --help                            # list the options (or -h)
```

From the Makefile, `make editor SCENE=<path>` starts it with a path, and
`make game GAME=<project> TARGET=<target>` runs `editor --build` (see
[Building from make](#building-from-make)).

## Opening a project or a scene

```
editor [path]
```

| `path` is | The editor |
|---|---|
| left out | starts with a small sample scene, in the folder it was started from |
| a folder | opens it as a project, as **Load Project** does: its `main.scene` if it has one, else an empty scene. A folder without `project.bronto` gets one written into it |
| a `.scene` file | opens that scene. The project is the nearest folder above it holding `project.bronto`, else the scene's own folder |

If the scene can't be loaded, the editor says why in the status bar and starts with the
sample scene instead. If more than one path is given, the last one is used.

An argument that starts with `-` but isn't one of the options below (a typo such as
`--biuld`) is not taken as a path: the editor says it doesn't know it, prints the list
of options and exits with status 2.

## Building a game: `--build`

```
editor --build <target> [project folder] [--out folder]
```

This does what the **Build** menu does, without opening a window: it saves nothing, asks
nothing, and exits when it is done. It reads the project's settings (`project.bronto`),
checks every script (strictly, if the project asks for strict typing), and packs the
project into a game.

| Argument | Meaning |
|---|---|
| `<target>` | `host` (or `native`) for this computer; `linux`, `macos` (or `mac`, `osx`), `windows` or `web` |
| `project folder` | the project to build. Left out, the folder the command runs in |
| `--out folder` | where the game goes (made if it isn't there). Left out, `builds/<target>/` in the project (the builds folder from Project Settings) |

The arguments can come in any order. What it writes:

| Target | The game |
|---|---|
| Linux | one program file, named after the project |
| Windows | one `.exe`, named after the project |
| macOS | a `.app` (the program in `Contents/MacOS`, the pack in `Contents/Resources/game.brpak`) |
| Web | `index.html`: one page holding the web player, the project and the page ([web.md](web.md)) |

### The players it needs

A game is `bronto_player` with the project packed into it, so `--build` needs the player
for its target beside the editor's program:

| Target | Where the editor looks | How to get it |
|---|---|---|
| this computer | `bronto_player` (`bronto_player.exe` on Windows) beside the editor | `make tools` |
| another computer | `players/bronto_player-linux`, `players/bronto_player-macos` or `players/bronto_player-windows.exe` beside the editor | build `bronto_player` on that system (`make tools`) and copy it there under that name |
| web | `web/` beside the editor (where `make web` puts it), else `players/web/` | `make web` (needs LLVM's clang), or a copy of that folder from a computer that has it |

### What it prints

Progress goes to stdout, problems to stderr. A build that works:

```
$ build/editor --build host examples/platformer --out build/games/platformer
Build: Platformer for Linux
Build: packed 7 files (3.1 KB, from 8.6 KB, 3 scripts checked) into /home/me/bronto/build/games/platformer/Platformer
Built Platformer for Linux: /home/me/bronto/build/games/platformer/Platformer
```

A script with a mistake (every mistake is listed, with its file and line, before the
summary):

```
Build: Platformer for Linux
[ERROR] build: scripts/coin.bs:21: the line ends too early: a value is missing
editor --build: Build failed: 1 script with mistakes
```

A missing player:

```
Build: Platformer for Windows
editor --build: no Windows player - build bronto_player on Windows and copy it to players/ beside the editor
```

The engine's own start-up line (`[INFO ] engine.c: Bronto 0.1.0 initialised ...`) comes
first in each case.

### Exit status

| Status | Meaning |
|---|---|
| 0 | the game was built |
| 1 | the build failed: a script mistake, no main scene, no player for the target, or a folder or file that couldn't be written |
| 2 | the command was wrong: no target or one it doesn't know (it prints the usage line), an option it doesn't know, or the project folder isn't a folder |

So a script or CI job can stop on a broken project:

```sh
build/editor --build web my_game --out public || exit 1
```

```powershell
build\editor.exe --build web my_game --out public
if ($LASTEXITCODE -ne 0) { exit 1 }
```

### Building from make

```sh
make game GAME=examples/pantryquest               # for this computer
make game GAME=examples/pantryquest TARGET=web     # a web page
```

`make game` builds the editor and the player first (and the web player, for `TARGET=web`),
then runs `editor --build <target> <GAME> --out build/games/<name>/<target>`, where `<name>`
is the last part of the project's path. `TARGET` takes the same names as `--build`, and is
`host` when left out. `make pantryquest` is a shortcut for it.

## The self-test: `--headless`

```
editor --headless
```

Runs the editor with no window and plays through a scripted test of it: selecting and
dragging, the hierarchy, Play and Stop, undo, saving and opening, the menus, the files,
the panels, the 3D view, prefabs, the debugger, the code editor and building. It prints
each failure, then a summary, and exits with status 1 if anything failed. It writes
screenshots (`editor*.bmp`), `editor_demo.scene` and an `editor_project/` folder into the
folder it runs in, so run it somewhere you don't mind those landing. It is a test of the
editor itself (for working on the editor), not a way to check a project: use `--build` or
`bscheck` for that.

## The icon: `--write-icon`

```
editor --write-icon <file.png> [size]
```

Draws the editor's app icon at `size` pixels square (16 to 1024; 256 if left out or out of
range), saves it as a PNG and exits: status 0 if it was written, 1 if not. This has to be
the first argument. `make vscode-icon` uses it to make the VS Code extension's icon.

## Help: `--help`, `-h`

```
editor --help
```

Prints the forms the command takes and what each option does, then exits with status 0
without starting the editor. It works anywhere on the command line, so
`editor --build web --help` shows the help rather than building.

## See also

- [tools/editor/README.md](../tools/editor/README.md): the editor itself, panel by panel.
- [web.md](web.md): web builds, and `bronto_web`, which makes web pages from the command
  line too.
- `bscheck` (`make tools`): checks `.bs` scripts from the command line without building.
- `bronto_player <project folder>` runs a project straight from its files, without
  packing it.
