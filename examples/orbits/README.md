# Orbits

A project with C code. Its `src/orbits.c` adds an **Orbit** component (circle another
entity) with the system that moves it, and a **Twinkle** behaviour (a Script whose file
is `Twinkle`); `main.scene` uses them like any other component.

- In the editor: `make editor SCENE=examples/orbits` (or Load Project). The editor
  compiles the code as it opens the project, so Orbit is in the inspector's Add Component
  and Play runs it. Change `src/orbits.c` (in the editor's code tab or anywhere) and it's
  compiled and loaded again; F7 does it at once.
- A game: Build (Ctrl+B), or `make orbits`, compiles the code into the player for this
  computer. `make orbits TARGET=web` makes a web page (with clang's WebAssembly target).

See [docs/c-code.md](../../docs/c-code.md).
