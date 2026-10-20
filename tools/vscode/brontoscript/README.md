# BrontoScript for VS Code

Editing support for Bronto engine scripts (`.bs` files):

- **Highlighting** for keywords, strings, numbers, built-in functions, the engine's
  callbacks (`start`, `update`, `on_collision`...), components (`self.Shape`) and
  entity properties.
- **Completion:**
  - built-in functions (the arguments they need are filled in as placeholders),
  - your variables and functions, and the local variables in scope,
  - after a `.`: entity properties (`position`, `global_position`, `velocity`, `parent`...),
    components, and other scripts' variables and functions,
  - after `self.Collider.`: that component's fields,
  - at the top of a file: the callbacks the script doesn't define yet (typed:
    `func update(dt: number) -> void`),
  - after `:` and `->`: the types (`number`, `int`, `string`, `bool`, `vec2`, `list`,
    `list[T]`, `entity`, `any`, `void`), and `@strict`.
- **Hover** documentation for built-ins (with their parameter and result types), keywords,
  types, properties and your own functions and variables, with their declared types (the `#`
  comment above a declaration is its documentation).
- **Signature help** while you type a function's arguments.
- **Go to definition** for functions and variables, including functions in other scripts
  in the workspace, plus an **outline** of the file.
- **Error checking as you type.** The checker is `bscheck`, built with the engine, so it
  finds exactly what Bronto finds before running a script: syntax errors, unknown names and
  functions, wrong argument counts and type mistakes (`speed is a number, so it can't hold
  a string`). In a project whose `project.bronto` has `strict_typing = true`, every script is
  checked as strict, like the engine does. Without it, quick checks still catch unknown
  functions, unbalanced brackets and code outside functions.
- **Snippets:** `func`, `var`, `if`, `ifelse`, `for`, `forn`, `while`, each callback,
  `move` and `onground`.

## Install

Either:

- Build the package with `make vscode` (from the engine's folder; it needs Python 3), then
  in VS Code run **Extensions: Install from VSIX...** and pick `build/brontoscript-0.1.0.vsix`.
  From a terminal, `code --install-extension build/brontoscript-0.1.0.vsix` does the same.
- Or copy this `brontoscript` folder into your extensions folder as
  `bronto.brontoscript-0.1.0`: `~/.vscode/extensions/` on macOS and Linux,
  `%USERPROFILE%\.vscode\extensions\` on Windows. Then restart VS Code.

For full error checking, build the checker with `make tools` (it lands in `build/bscheck`).
The extension looks for it in the workspace's `build/` folders (and up to three folders
above), then on your `PATH`. You can also set **brontoscript.checker.path** in your
settings.

## Make the Bronto editor open scripts in VS Code

In the Bronto editor, open **Settings** and choose the **VS Code** preset (command
`code -g "{file}:{line}"`). Double-clicking a script, or pressing **Edit** in the preview,
then opens it in VS Code. When the script has a mistake, it opens at that line.
