# Playing in the terminal

A Bronto game can draw in the terminal it is started from instead of opening a window. The
game itself doesn't change: the same scenes, scripts, framebuffer and sound. Only the
platform backend is different (`src/platform/platform_terminal.c`). It works on Windows,
macOS and Linux. Web builds always draw in the page.

There are two styles:

| Style | Setting | What a character cell shows |
|---|---|---|
| Colour blocks | `terminal_blocks` | Two pixels, one above the other: the upper half block `▀` in the top pixel's colour, over a background in the bottom pixel's colour. This is the most detail a terminal can show. |
| Text | `terminal_text` | One character from `" .:-=+*#%@"`, picked by the brightness of the cell's two pixels and drawn in their hue on black: live ASCII art. Very dark cells are left blank. |

## Choosing it

**For a project:** in the editor, open **Build > Build Settings** and set **Renderer**.
This writes `renderer` to `project.bronto`:

```
renderer = terminal_blocks      # or terminal_text; window is the default
```

A built game (from **Build for ...**) then draws in the terminal when it is run from one.
**Build and Run** starts it in a new terminal window: a console window on Windows,
Terminal on macOS, and on Linux the first terminal it finds (`x-terminal-emulator`,
GNOME Terminal, Konsole, xfce4-terminal, kitty, Alacritty, WezTerm, foot or xterm).

If a terminal game is started without a terminal (from a desktop icon, or as a Windows
program linked with `-mwindows`), it opens a window instead.

**For one run:** `bronto_player` takes `--terminal` (colour blocks), `--terminal=text` or
`--window`. Any of these overrides the project's setting:

```sh
build/bronto_player examples/platformer --terminal
build/bronto_player examples/pantryquest --terminal=text
build/bronto_player examples/pantryquest --terminal --replay examples/pantryquest/tools/playthrough.brec
```

**In C:** pass the backend to the engine.

```c
cfg.platform = br_platform_terminal(BR_TERMINAL_BLOCKS);   /* NULL when there's no terminal */
if (!cfg.platform) cfg.platform = br_platform_native();
```

## How it fits the terminal

- **The window is the terminal.** It is one pixel wide and two pixels high per cell.
  Cells are about twice as tall as they are wide, so the pixels come out square. The
  engine's scale modes place the framebuffer in the terminal just as they do in a window,
  with black bars around it. Resizing the terminal resizes the window.
- **Smaller than the game's screen.** A 120x40 terminal is 120x80 pixels, so a 320x180
  game is drawn at about a third of its size. Each terminal pixel is the average of the
  game pixels it covers. Make the terminal's font smaller to see more detail.
- **Only what changed is sent.** Unchanged cells are skipped. Each frame goes out in one
  write, wrapped in *synchronized output*, so terminals that support it never show half
  a frame.
- **Colour.** 24-bit colour is used when the terminal reports it (`COLORTERM=truecolor`),
  in Windows' console, and in terminals known to support it. Otherwise colours are mapped
  to the 256-colour xterm palette. To choose one yourself, set
  `BRONTO_TERMINAL_COLORS=truecolor` or `BRONTO_TERMINAL_COLORS=256`.
- **Sound and gamepads** use the system's backends, as a windowed game would: ALSA and
  evdev, waveOut and XInput, or Core Audio and the GameController framework.
- **The game's output.** While the game is running, the engine's log and `print()` from
  scripts are held in a temporary file so they don't draw over the picture. They are
  written to the terminal when the game ends.
- **Leaving.** The terminal is put back as it was when the game quits or is killed (and,
  on macOS and Linux, when it crashes). **Ctrl+C** quits.

## Keys

The Windows console reports each key press and release, so input works exactly as it does
in a window.

Other terminals send characters when a key is typed and send nothing when it is let go.
Bronto handles that in two ways:

- **Terminals that support the kitty keyboard protocol** (kitty, WezTerm, foot, Ghostty,
  Alacritty, iTerm2, Rio and others) are asked to use it. They then report every press,
  repeat and release, modifier keys included, so holding keys works as in a window.
- **Other terminals** (GNOME Terminal and other VTE-based terminals, macOS Terminal,
  xterm, and anything inside tmux or screen): a key counts as held from when it is pressed until
  its autorepeat stops arriving. That is 0.42 s after the press, then 0.15 s after each
  repeat. Set `BRONTO_TERMINAL_KEY_HOLD` (in seconds) to change the first wait to match
  your keyboard's repeat delay. These terminals only repeat the last key pressed, so if
  you hold one key and tap another, the first one is released. Games that need two keys
  held at once (run and jump) should be played in a kitty-protocol terminal or on Windows.

The mouse works too: clicks, the wheel and movement, at one position per character cell.
So do focus changes: when the terminal loses focus, held keys are released.

## Testing

`tests/test_terminal.c` drives the backend through pipes in place of a terminal (the
`br_terminal_attach_fds` hook, POSIX only). It checks the encoding of blocks and text,
skipping unchanged cells, the box filter, the 256-colour mapping, plain and
kitty-protocol keys, the mouse, focus, Ctrl+C, and that the terminal is restored.
