# Bronto Editor

A visual scene editor, built with the engine itself: one engine scene, the `br_ui`
GUI module, and the ECS world it edits.

```sh
make editor                          # start with a sample scene
make editor SCENE=levels/one.scene   # open a scene file
make editor SCENE=examples/platformer   # open a project folder
build/editor --headless              # scripted self-test, writes screenshots
build/editor --write-icon icon.png 256   # save the app icon as a PNG (make vscode-icon uses it)
```

## Using it

| | |
|---|---|
| **Load Project** | Pick a folder in the system's folder dialog (Ctrl+Shift+O). It becomes the project: the assets strip shows it, and its `main.scene` opens if there is one. On Linux the dialog is zenity or kdialog when installed, and otherwise a folder picker inside the editor. |
| **Select** | Click an entity in the scene or in the list on the left. Esc deselects. |
| **Move** | Drag it. With **Snap** on it lands on the grid. Arrow keys nudge it by 1 unit, or by one grid step with Shift. |
| **Rotate / scale** | Drag the round handle above the selection, or the square handle at its corner. |
| **View** | Scroll to zoom around the cursor. Right-drag, middle-drag or Space+drag pans. **F** centres the selection. **G** toggles the grid. |
| **Edit values** | In the inspector, drag a number left or right, or click it and type. Text fields commit on Enter. Colours are R G B A. |
| **Components** | Add them from **+ Add component** at the bottom of the inspector, and remove one with the x on its header. |
| **Entities** | **+ New** adds a root, **+ Child** a child of the selection. **Copy** (Ctrl+D) and **Delete** (Del) take the whole subtree. Ctrl+C, Ctrl+X and Ctrl+V copy, cut and paste an entity with its subtree (pasted next to the original, as a sibling of the selection). They use the system clipboard, as scene text, so entities copied in one editor window paste into another (and into a text file). F2 renames the selection. |
| **Clipboard** | In any text field, Ctrl+C / Ctrl+X copy or cut its text, Ctrl+V pastes (the first line) and Ctrl+A selects it all. The console's menu has **Copy Line** and **Copy All** (Ctrl+C copies the selected line while the mouse is over the console), and a file's menu **Copy Path** and **Copy Full Path**. On Linux, text copied from the editor stays on the clipboard while the editor is open (X11 keeps no copy itself unless a clipboard manager runs). |
| **Right-click** | Every panel has a menu for what is under the mouse. **Hierarchy row or object in the view:** Rename, Add Child, Duplicate, Cut / Copy / Paste, Paste as Child, Add Component (a submenu), Unparent, Expand / Collapse, Frame in View, Save as Prefab, Delete. **Empty space in the hierarchy:** New Entity, Paste, Expand All, Collapse All. **Empty space in the view:** New Entity Here, New Label Here, Paste Here, Frame Selection, Reset View, Grid, Snap to Grid, Show Colliders, Panels (show or hide each, Reset Layout), Play. **A file:** what double-clicking does (Open, Play, Edit Script...), Place in Scene / Attach to Selected, Open with System App, Rename, Delete, Show in File Manager. **Empty space among the files:** New Folder, New Script, Save Selected as Prefab, Up, Refresh, Show in File Manager. **An inspector section or field:** Reset the field or the whole component to its defaults, Copy Values, Paste Values (onto the same kind of component), Edit Script, Remove. **Empty space in the inspector:** Add Component, Paste a copied component. Menus work with the mouse or the arrow keys and Enter; Esc closes them. |
| **Console** | The **Console** tab beside **Files** shows everything the editor writes to stdout and stderr (it is captured; the terminal still gets it too), the engine's log and `print()` from scripts. Errors are red and warnings yellow; **Messages**, **Warnings** and **Errors** filter them, the box on the right searches, and a line repeated in a row shows a count instead. While the tab is hidden, new errors show on it. Double-click a line naming a script (`player.bs:12`) to open the script at that line. **Clear on Play** empties it each time Play starts. |
| **The tree** | The hierarchy is a tree. Drag a row onto the middle of another row to make it a child (it stays where it is on screen); drag it between rows to reorder it or move it to another parent; drag it below everything to make it a root. A line or box shows where it will land. The arrows fold subtrees, and double-clicking a row centres the view on it. The **filter** box above the tree shows only the entities that match: words are parts of names (`bat` finds Bat and Acrobat), `#enemy` finds entities tagged enemy, and several words must all match (`#enemy #flying`, `#pickup heart`); the matching rows show their tags. Children move, turn and scale with their parent, and their Transform is relative to it. **Unparent** in the inspector makes the selection a root. |
| **Play** | **Play** (F5) runs the scene's systems: Motion and Bounds, physics (RigidBody, Collider), sounds and scripts. While playing, the keyboard belongs to the game; F5 or Esc stops. **Stop** puts every entity back exactly as it was, and edits made while playing are undone too. A script error shows in the status bar and the console, with its file and line. |
| **Physics** | Add a **Collider** to make an entity solid and a **RigidBody** to make it fall and collide. **Colliders** in the toolbar outlines them: green for solid, yellow for triggers. |
| **Build** | The toolbar's **Build** menu. **Build for <this computer>** (Ctrl+B) checks every script and writes the game into `builds/<system>/` in the project: one program on Linux and Windows, a `.app` on macOS. **Build and Run** (Ctrl+Shift+B) starts it. **Build for** lists the other systems: each works once its player is in `players/` beside the editor (`bronto_player-windows.exe`, `bronto_player-macos`, `bronto_player-linux`, built on that system with `make tools`). **Web** writes `builds/web/index.html`, one page holding the game, to open in a browser or put on a web site; **Build and Open in Browser** does both. It needs the web player from `make web` in `web/` beside the editor (or a copy in `players/web/`); see [docs/web.md](../../docs/web.md). **Check Scripts** checks without building; script mistakes go to the console, where a double-click opens them. **Open Save Folder** opens the folder where the game keeps its saved data (`save_data()` in scripts); Play uses the same one as the built game. From the command line (no window): `editor --build linux|macos|windows|web|host [project folder] [--out folder]` does the same build, into `builds/<system>/` or the `--out` folder; script mistakes go to stderr and it exits 1. `make game GAME=<project> [TARGET=...]` runs it into `build/games/<project>/<system>/`. |
| **Project Settings** | In the Build menu: the game's name (its window title and program name), the main scene it starts with, its screen size and window scale, **Strict typing** (every script must declare types, as with `@strict`), an icon image, the builds folder, and **Leave out**: files and folders of the project that built games don't need (`tools, notes.txt`). They are saved in the project's `project.bronto`. |
| **Settings** | **Script editor**: the program that opens `.bs` files, a command line with `{file}` and `{line}`, with presets for VS Code, Sublime Text, Notepad++ and Zed (empty = the system's default). Saved in your settings folder, so it applies to every project. |
| **Scripts** | **+ Script** makes a new `.bs` file from a template. Drag a script onto an entity to attach it (it gets a Script component), or into empty space for a new entity. Double-click to edit it in your text editor; Play reads it again. The preview shows the first lines and any mistake. See [docs/scripting.md](../../docs/scripting.md). |
| **Undo** | Ctrl+Z and Ctrl+Y (or Ctrl+Shift+Z). Every finished drag or edit is one step. |
| **Panels** | Drag the edge between two panels to resize them: the scene list, the inspector, Files / Console under the view, and the file preview. The edge lights up under the mouse. The arrow in a panel's top corner collapses it: the side panels and the preview become a thin strip with their name down it (click it to bring the panel back as it was), and Files / Console shrink to just their tabs (click a tab to open them). Dragging an edge most of the way shut, or double-clicking it, collapses or opens the panel too. The scene view's right-click menu has **Panels**, to show or hide each one and **Reset Layout**. Sizes are saved with the editor's settings. |
| **Assets** | The strip under the view shows the project folder. Click a tile to preview it on the right; double-click to open a folder or scene, hear a sound, or add an image. **Up** and the path buttons move between folders. |
| **Drag & drop** | Drag an image, sound or scene from the assets into the view: images become sprites, sounds become Sound entities (they play when the scene starts), scenes are placed as copies (prefabs). Drop an image onto **Sprite › image** or a sound onto **Sound › file** to swap it. Esc cancels a drag. |
| **Prefabs** | **To prefab** writes the selected entity to a `.scene` file in the folder shown. Drag it into any scene to place a copy, centred where you drop it. |
| **File operations** | **+ Folder**, rename (edit the name in the preview and press Enter), **Delete** (moves the file to `.trash` in the project, so nothing is lost) and **Refresh** (reloads files changed outside the editor; the list also updates by itself). |
| **Files** | Type a name in the toolbar box, then **Save** (Ctrl+S) or **Open** (Ctrl+O). `.scene` is added if you leave it off. The editor asks before New, Open or Quit would lose changes. |

On a Mac, Cmd works wherever Ctrl is listed.

**The project folder** is the one chosen with Load Project, or a folder given on the
command line, or the folder of the scene given on the command line (or the nearest
folder above it holding a `project.bronto` file), or else the folder the editor was
started in. Asset paths in scenes are relative to it, such as `art/hero.tga`. When a
scene or prefab is saved in a subfolder, the editor writes `project.bronto` into the
project folder so that games (`br_assets_find_root`) resolve the same paths.

The white frame in the view is a 320x180 game screen (the engine's default resolution),
with (0, 0) at its top-left corner.

## How it is organised

```
editor.h      shared declarations
doc.c         the DOCUMENT: entities in order, selection, undo/redo, play/stop,
              load/save. No drawing, no input; tests/test_editor_doc.c tests it.
commands.c    every user action (New, Save, Delete, Play...). Buttons and
              shortcuts both call these.
viewport.c    the scene view: camera, grid, picking, move/rotate/scale handles
panels.c      toolbar, hierarchy, inspector, status bar, console, the unsaved-changes dialog
assets.c      the asset browser and preview, file operations, drops into the scene
settings.c    preferences (the script editor) and the Settings dialog
console.c     the console's lines: captured output cut into lines, log levels,
              repeats; tested by tests/test_editor_doc.c
menus.c       the right-click menus, component copy / paste / reset
build.c       the Build menu (the player with the project packed in; for the web, a page from
              tools/web/webpage.c), Project Settings
icon.c        the app icon: a little brontosaurus drawn with the renderer (also the VS Code extension's icon)
main.c        engine setup, window layout, keyboard shortcuts, headless self-test
```

- **Undo, redo and play/stop** all use the engine's scene format. Before each change
  the document saves the scene to text, and undoing loads it back. That makes undo
  exact for every field of every component, with no per-command undo code.
- **The inspector** is generated from component descriptions (`br_component_desc` in
  `ecs.h`). It shows a widget for each field by type: int, float, bool, vec2, colour,
  string or enum.
- **Drag and drop** is part of `br_ui`: a tile calls `br_ui_drag_source` with the
  asset's path, and the view and inspector fields call `br_ui_drop_target`.
- **Right-click menus** are part of `br_ui` too. A panel that sees a right-click
  (`BR_UI_RIGHT_CLICKED`, or `br_ui_context_area` for empty space) calls `ed_menu_*`,
  which remembers what was clicked and opens a menu by name; `ed_menus()` lists the
  open menu's items once a frame and runs the chosen one through the same commands
  as the buttons and shortcuts.
- **The console** reads what `br_capture` (the engine's `capture.h`) caught each frame.
  Lines in the log's format (`[WARN ] file.c:12: ...`) become entries with a level.
- **Prefabs** are ordinary scene files; placing one loads its entities into the open
  scene (`ed_doc_instantiate`), so they can be edited and saved like anything else.
- **The viewport** draws the scene with `br_builtin_draw`, the same call a game's render
  system makes, so what you see is what the game shows.

## Using scenes in a game

```c
br_builtin_register(&scene->world, NULL);   /* the components and their systems */
br_world_load_file(&scene->world, "level.scene", NULL, 0, err, sizeof(err));
```

That's all. The built-in systems move, collide, run the scripts and draw everything.
Set the asset root first with `br_assets_find_root(path, dir, size)` and
`br_assets_set_root(dir)` so sprites, sounds and scripts are found.

**A game's own components** work the same way, once they have a description (see the
example in `ecs.h`). To edit them in the editor, register them on the document's world
in `ed_doc_init` (doc.c). The inspector picks them up automatically.
