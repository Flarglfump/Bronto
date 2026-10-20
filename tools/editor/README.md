# Bronto Editor

A visual scene editor, built with the engine itself: one engine scene, the `br_ui`
GUI module, and the ECS world it edits.

```sh
make editor                          # start with a sample scene
make editor SCENE=levels/one.scene   # open a scene file
make editor SCENE=examples/platformer   # open a project folder
build/editor --headless              # scripted self-test, writes screenshots
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
| **Entities** | **+ New**, **Copy** (Ctrl+D), **Delete** (Del), and drag rows up or down in the list to reorder them (a line shows where the row will go; the order is the save order). |
| **Play** | **Play** (F5) runs the scene's systems: Motion and Bounds, physics (RigidBody, Collider), sounds and scripts. While playing, the keyboard belongs to the game; F5 or Esc stops. **Stop** puts every entity back exactly as it was, and edits made while playing are undone too. A script error shows in the status bar, with its file and line. |
| **Physics** | Add a **Collider** to make an entity solid and a **RigidBody** to make it fall and collide. **Colliders** in the toolbar outlines them: green for solid, yellow for triggers. |
| **Scripts** | **+ Script** makes a new `.bs` file from a template. Drag a script onto an entity to attach it (it gets a Script component), or into empty space for a new entity. Double-click to edit it in your text editor; Play reads it again. The preview shows the first lines and any mistake. See [docs/scripting.md](../../docs/scripting.md). |
| **Undo** | Ctrl+Z and Ctrl+Y (or Ctrl+Shift+Z). Every finished drag or edit is one step. |
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
panels.c      toolbar, hierarchy, inspector, status bar, the unsaved-changes dialog
assets.c      the asset browser and preview, file operations, drops into the scene
icon.c        the app icon: a little brontosaurus drawn with the renderer
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
- **Prefabs** are ordinary scene files; placing one loads its entities into the open
  scene (`ed_doc_instantiate`), so they can be edited and saved like anything else.
- **The viewport** draws the scene with `br_builtin_draw`, the same call a game's render
  system makes, so what you see is what the game shows.

## Using scenes in a game

```c
br_builtin_register(&scene->world, NULL);   /* the components and their systems */
br_world_load_file(&scene->world, "level.scene", NULL, 0, err, sizeof(err));
```

That's all. The built-in systems move and draw everything. `examples/sceneplayer` is
a complete example: `sceneplayer level.scene`.

**A game's own components** work the same way, once they have a description (see the
example in `ecs.h`). To edit them in the editor, register them on the document's world
in `ed_doc_init` (doc.c). The inspector picks them up automatically.
