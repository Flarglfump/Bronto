# BrontoScript reference

BrontoScript is the small language Bronto runs for `Script` components. A script is a
`.bs` text file in your project. You attach it to an entity by setting **Script › file**
to its path, or by dragging the file onto the entity in the editor. Every entity that
runs a script gets its own copy of the script's variables.

```
# coin.bs - spins, and is collected when the player touches it
var t = 0

func update(dt) {
    t += dt * 240
    self.scale = vec2(abs(cos(t)), 1)
}

func on_trigger(other) {
    if other.name == "Player" {
        find("HUD").collect()          # call a function in another entity's script
        destroy(self)
    }
}
```

## The shape of a file

Only these can appear at the top level of a file:

- `var name = value` declares a variable. Each entity running the script has its own copy,
  and the value is kept between calls. **Script › vars** in the inspector overrides the
  starting values for one entity, for example `speed = 120, name = "Rex", wrap = true`
  (only literal values: numbers, strings, `true`/`false`/`nil`, `vec2(x, y)`, `vec3(x, y, z)`
  and lists of these, such as `path = [vec2(0, 0), vec2(64, 0)]`).
- `const name = value` declares a variable that can't be changed (see [const](#const)).
  **Script › vars** can still give it its starting value.
- `func name(a, b) { ... }` declares a function.
- `struct Name { ... }` and `class Name { ... }` declare your own types (see
  [Structs and classes](#structs-and-classes)).
- `@strict`, on the first line only, makes types required (see [Types](#types)).
- `@3d`, also at the top, says the script drives a 3D entity: `self.position` and the other
  transform properties are `vec3`s (see [3D](#3d)). `@strict @3d` (or the two on separate
  lines) gives both.

`#` starts a comment. Statements end at the end of the line, or at a `;`. Inside `( )` or
`[ ]` a statement can carry on over several lines:

```
var names = ["UP", "DOWN",
             "LEFT", "RIGHT"]
```

## Callbacks

The engine calls these functions when a script defines them:

| Function | When |
|---|---|
| `start()` | once, before the entity's first update |
| `update(dt)` | every fixed step (60 per second); `dt` is the step in seconds |
| `on_collision(other, normal, speed)` | this entity's collider started touching `other`'s. `normal` points away from this entity (a `vec3` for 3D colliders), and `speed` is how fast they hit |
| `on_collision_end(other)` | they stopped touching |
| `on_trigger(other)` | `other` entered a trigger collider (either this entity's or `other`'s) |
| `on_trigger_exit(other)` | ... and left it |
| `draw()` | when the scene is drawn: see [Drawing](#drawing) |
| `on_pause()` | another scene was pushed over this one (`push_scene`) |
| `on_resume()` | this scene is the top one again (the one over it was popped) |

Every callback can declare fewer parameters than the engine passes.

## Values

| Type | Examples | Notes |
|---|---|---|
| number | `3`, `-0.5`, `1e3` | always floating point |
| string | `"hi"`, `'hi'`, `"a\nb"` | `+` joins strings, and turns anything else into text: `"score: " + 5`. `\n` is a new line (a Label shows it as one), `\t` a tab, and `\"`, `\'` and `\\` the characters themselves. `text[i]` is one character, `len(text)` the length |
| true / false | `true`, `false` | |
| nil | `nil` | "nothing"; variables start as nil |
| vec2 | `vec2(3, 4)` | `.x`, `.y`, `.length`. `+ -` with another vec2; `* /` by a number |
| vec3 | `vec3(1, 2, 3)` | `.x`, `.y`, `.z`, `.length`. The same operators as vec2 (for 3D: see [3D](#3d)) |
| list | `[1, 2, 3]` | `list[0]`, `list[-1]` (the last item), `len(list)`, `push`, `pop`, `remove` |
| entity | `self`, `find("Player")` | see below |
| struct / class object | `Point(1, 2)`, `Dog("Rex")` | your own types: see [Structs and classes](#structs-and-classes) |

`nil`, `false` and `0` count as false in `if` and `while`. Everything else counts as true,
including `""` and `[]`. `and` and `or` stop early and return one of their operands:
`name or "nobody"`.

## Statements

```
var x = 1                     # a local variable (inside a function)
const limit = 10              # a local that can't be changed
x = 2;  x += 1;  x -= 1;  x *= 2;  x /= 2
if a { ... } elif b { ... } else { ... }
while cond { ... }
for item in [1, 2, 3] { ... }
for i in 10 { ... }           # 0, 1, ... 9
for i in range(2, 8, 2) { ... }
break;  continue;  return value
```

Operators, loosest first: `or`, `and`, `not`, comparisons (`== != < <= > >=`), `+ -`,
`* / %`, unary `-`. `%` always gives a positive result for a positive divisor:
`-7 % 3 == 2`.

## Entities

`self` is the entity running the script. `other` in a callback is another entity.

| Property | Meaning |
|---|---|
| `.name` | Name.text |
| `.position`, `.x`, `.y` | Transform.position (pixels; +y is down), relative to the parent |
| `.rotation` | Transform.rotation (degrees, clockwise), relative to the parent |
| `.scale` | Transform.scale (set it to a number or a vec2) |
| `.global_position`, `.global_rotation` | where it is in the world, after its parents; setting them moves it there |
| `.parent` | the parent entity, or nil for a root. Setting it reparents (nil makes a root), and the entity stays where it is on screen |
| `.velocity` | RigidBody.velocity, or Motion.velocity if there is no RigidBody |
| `.angular_velocity` | RigidBody.angular_velocity (degrees / second) |
| `.alive` | false once the entity has been destroyed |
| `.id` | a number unique to the entity |
| `.Shape`, `.Sprite`, ... | a component (any capitalised name), or nil if the entity doesn't have it. Read and write its fields: `self.Shape.color = rgb(255, 0, 0)`, `self.Label.text = "Hi"`, `self.Collider.trigger = true`, `self.Tags.list = "enemy, boss"` |
| `.health` (any other name) | that entity's script variable |

Nested values write back: `self.velocity.x = 50` and `self.Shape.size.y += 2` both work.

`other.hit(3)` calls the function `hit` in `other`'s script and returns its result.
Field types map as follows: numbers to numbers, true/false to true/false, vec2 to vec2,
strings to strings, enums to their names (`"ellipse"`), and colours to `"#rrggbbaa"`
strings. You can also set a colour from a list `[r, g, b]` or `[r, g, b, a]`. Entity
fields (such as a game component's `target`) are entities or nil.

## Functions

**Maths:** `abs floor ceil round sqrt pow sign min max clamp(v, lo, hi)
lerp(a, b, t)` (numbers or vec2s), `sin cos` (degrees), `atan2(y, x)` (degrees),
`random()` (0..1), `random(n)` (0..n), `random(a, b)`.

**Vectors:** `vec2(x, y) length(v) normalize(v) dot(a, b) distance(a, b) angle(v)`
(degrees), `from_angle(degrees, length)`. `vec3(x, y, z)` and `cross(a, b)` are for 3D;
`length normalize dot distance lerp` take vec3s as well.

**Text:** `str(v)`, `num("12")` (nil if it isn't a number), `print(...)` (to the log, and the
editor's console and status bar), `rgb(r, g, b[, a])` (a colour string).

**Lists:** `len(x) push(list, v) pop(list) remove(list, index) contains(list, v)
range(n) range(a, b[, step])`.

**Input:** `key_down(name) key_pressed(name) key_released(name)`. Key names, ignoring case:
the letters `"a"` to `"z"` and digits `"0"` to `"9"`; `"space" "enter" "escape" "tab"
"backspace" "insert" "delete" "home" "end" "pageup" "pagedown"`; the arrows `"left" "right"
"up" "down"`; `"f1"` to `"f12"`; `"shift" "ctrl" "alt"` (either side) or `"leftshift"
"rightctrl"`... (one side); `"esc"` and `"return"` work too, and so do punctuation keys by
their character (`","`, `"/"`).
`mouse_down(button) mouse_pressed(button)` (0 left, 1 right, 2 middle),
`mouse_position()` (in game pixels), and `time()` (seconds since the game started).
`last_key()` gives the name of a key pressed this step (`""` if none) in the form
`key_down()` takes, for letting players choose their own keys.

**Gamepads:** every controller has the same Xbox-style layout, whatever it is (on a
PlayStation pad, A is cross; on a Steam Deck, the Deck's own buttons). Up to four are
numbered 0 to 3; without a pad number, these look at every pad.
- `button_down(name[, pad]) button_pressed(name[, pad]) button_released(name[, pad])`.
  Buttons: `"a" "b" "x" "y"`, `"lb" "rb"` (bumpers), `"lt" "rt"` (the triggers, as
  buttons: down past half way), `"back" "start" "guide"`, `"ls" "rs"` (the sticks
  pressed in), `"up" "down" "left" "right"` (the d-pad). The usual other names work
  too: `"cross" "circle" "square" "triangle"`, `"l1" "r1" "l2" "r2" "l3" "r3"`,
  `"select" "view" "menu" "options" "home"`, `"dpad_up"`...
- `stick("left")` / `stick("right")` is a stick as a `vec2` (-1 to 1, +y down), and
  `axis(name[, pad])` one axis: `"left_x" "left_y" "right_x" "right_y"`, or `"lt" "rt"`
  (0 to 1). Both take off the dead zone, so a stick at rest is exactly 0.
- `gamepad_connected([pad])`, `gamepad_count()`, `gamepad_name([pad])`.
- `last_button()` gives the name of a button pressed this step (`""` if none), as
  `button_down()` takes it, for letting players choose their own buttons.
- `rumble(strength, seconds[, pad])` shakes the pad (0 to 1; 0 stops it).
- `last_input()` says what the player touched last: `"keyboard"`, `"mouse"`,
  `"gamepad"` or `"touch"` (`""` before anything). It carries over from scene to scene, so a
  prompt can name the right button straight away.

**Touch screens:** for playing on a phone or tablet (a game built for the web), list the
on-screen controls in the project's settings: `touch_controls = stick, a, b, start` in
`project.bronto` (**Build > Project Settings > Touch controls** in the editor). They show
up when the screen is first touched, and they are a gamepad as far as scripts can tell: the
stick is `stick()`, the A button `button_pressed("a")`, and so on, so a game that can be
played with a gamepad needs nothing more. Each control can press a key instead, with a
label of its own: `stick=arrows, a=space:JUMP`. The controls sit in the black bars around
the game where there is room (on a screen taller than wide, the game moves to the top and
they go below it); a tap that misses them is the left mouse button. While they are in use,
`last_input()` is `"touch"`; a key or a real gamepad hides them again.
- `touch_controls_shown()`: are they on screen?
- `show_touch_controls(on)`: hide them for now (`false`: a cutscene, a menu taken with
  taps), or show them again.
- `set_touch_controls(list)`: different controls from now on (`""` removes them).
- The controls: `stick` (the left stick), the buttons by their names above (`a b x y`,
  `lb rb lt rt`, `back start`, `up down left right` as a d-pad); after `=` a key
  (`space`, `z`, `enter`; for the stick `arrows` or `wasd`), after `:` a label of up to six
  characters. `bronto_player --touch <project>` tries them on a desktop, with the mouse as a
  finger.

Every press is seen once: `key_pressed()`, `button_pressed()` and the rest are true in
exactly one step of `update()`, even when the game runs two steps in one frame (or none).

```
var move := stick()                          # the left stick, any pad
if key_down("left") or button_down("left") { move.x = -1 }
if key_pressed("space") or button_pressed("a") { jump() }
```

**The world:**
- `find(name)` returns the first entity with that name, or nil. `find_all(name)` returns a list.
- `children(e)` returns its direct children; `child(e, "Gun")` the one with that name, or nil.
- `spawn("prefabs/bullet.scene", position[, parent])` adds a copy of a scene file's entities
  and returns the first one (position is a vec2, or a vec3 for 3D prefabs). With a parent,
  they become its children, and position is in its space. The new entities' scripts are ready at once. You can set their variables and
  call their functions straight away (`var b = spawn(...)` then `b.speed = 200`). Their
  `start()` runs before their first update, on the next step.
- `destroy(e)` removes an entity, and its children, at the end of the step.
- `has(e, "Shape")`, `add(e, "Motion")` (returns the component) and `remove_component(e, "Motion")`.
- `send(e, "function", args...)` calls a function on another entity's script if it has one,
  and quietly does nothing if not.
- `play_sound("sfx/jump.wav"[, volume[, pitch[, pan]]])` plays on the `sfx` bus and
  returns the voice (see [Sounds and music](#sounds-and-music)). A Sound component's
  `bus` field (`sfx`, `music`, `ui` or `ambient`) says where it plays.
  `set_volume("music", 0.5)` and `volume("music")` set and read a bus's volume (0 to 1,
  up to 2), or the whole mix's with `"master"`: for a game's volume settings.
- `load_scene("levels/2.scene")` ends this scene (and any pushed over it) and starts that
  one (after this step). `quit()` ends the game. In the editor's Play, load_scene shows the
  other scene until Stop, and quit stops playing. To put a scene *on top* of this one, see
  [Scenes on a stack](#scenes-on-a-stack-and-globals).
- `broadcast("boss_beaten", 3)` calls `boss_beaten(3)` in every script of the scene that
  has that function, and returns how many did: a message to whoever cares, without
  finding them. (`send(e, ...)` is the same for one entity.) C code hears it on the
  engine's message bus as the type `"script.boss_beaten"`, with the number arguments as
  its payload (up to six `double`s), once it has registered that type.

**Saved data** (kept between runs of the game, in its own folder for saved data: see
[Saved data](../README.md#saved-data)):
- `save_data("progress", value)` keeps a value under a name, and returns false if it
  couldn't be written. The value can be a number, a string, `true`/`false`, `nil`, a
  vec2, or a list of these (lists inside lists too). Saving an entity is an error. Names
  are up to 60 letters, digits, spaces and `_-.()`.
- `load_data("progress"[, default])` returns the saved value, or the default (nil if
  none is given) when there isn't one. A file that has been damaged also gives the
  default, with a warning in the log.
- `has_data(name)`, `delete_data(name)`, and `data_names()` (every name, in order).

```
func save_game() {
    save_data("slot1", [hearts, beans, room_name, flags])
}
func load_game() {
    var s = load_data("slot1")
    if s == nil { return false }
    hearts = s[0]; beans = s[1]; room_name = s[2]; flags = s[3]
    return true
}
```

Each name is a text file (`slot1.txt`) holding the value written as BrontoScript, such
as `[3, 12, "ow_7_7", ["spoon", "d1:key1"]]`.

**Tags** (a `Tags` component: words that say what an entity is, so you can find entities by
kind rather than by name). In the inspector, add a **Tags** component and type its list:
`enemy, flying` (commas or spaces). A tag is up to 31 letters, digits and `_-.:/`, and
case counts (`enemy` isn't `Enemy`). The editor's hierarchy filter finds them with `#enemy`.
- `has_tag(e, "enemy")` tells whether an entity has a tag.
- `add_tag(e, "stunned")` gives it one (adding the Tags component if needed), and
  `remove_tag(e, "stunned")` takes it off again (true if it had it).
- `tags(e)` lists an entity's tags.
- `find_tagged("enemy")` returns every entity with the tag, and `find_tagged("enemy",
  "flying")` every one with all of them. `first_tagged(...)` returns the first, or nil.

```
func explode() {                               # hurts every enemy nearby
    for e in find_tagged("enemy") {
        if distance(e.position, self.position) < 40 { e.hit(2) }
    }
}
```

**Tilemaps** (a `Tilemap` component: a grid of tiles from one image, one character per cell):
- `tile_at(map, point)` returns the character in the cell under a world point, or `""`
  outside the map.
- `set_tile(map, point, "#")` changes that cell and returns false outside the map.
- `tile_solid(map, point)` tells whether that cell's character is one of the map's `solid`
  ones (outside the map: false). Walls for scripted movement work this way.
- `tile_center(map, point)` returns the world position of the centre of that cell.

**Text:** `substr(text, start[, count])` returns part of a string; a negative start counts
from the end. `text[i]` is one character.

- `tile_cell(map, point)` gives the column and row (a vec2) of the cell under a world point;
  `tile_get(map, column, row)` and `tile_put(map, column, row, "#")` read and change a cell by
  its column and row, and `tile_cell_center(map, column, row)` gives its middle.
- `tile_box_solid(map, centre, size)` tells whether a box (its world centre and full size)
  overlaps any solid cell; edges that only touch don't count. It is the quick way to move
  something through a level by script: try the move, and take it back if the box hits.

```
func move_x(dx: number) -> void {            # one pixel at a time, stopping at walls
    for i in abs(dx) {
        var next := self.position + vec2(sign(dx), 0)
        if tile_box_solid(map, next, vec2(10, 14)) { return }
        self.position = next
    }
}
```

**Physics:**
- `apply_force(e, force[, point])` pushes over the next step. `apply_impulse(e, impulse[, point])`
  changes the velocity now.
- `raycast(from, to[, ignore[, mask]])` returns `[entity, point, normal]` for the first solid
  collider on the line, or nil. `mask` picks the collision layers to look at (bits: `1 + 4`
  is layers 0 and 2; -1, the default, is every layer).
- `entity_at(point[, mask])` returns the collider at a point, or nil.
- `gravity()` and `set_gravity(vec2(0, 900))`: the PhysicsWorld's gravity (set_gravity adds
  a PhysicsWorld to this entity if the scene has none).

## Sprite sheets, animation and cameras

These are components (edit them in the inspector, or from scripts as `self.Sprite.frame`),
with a few functions to help.

**Sprite sheets.** A Sprite's `frame_size` (say `16 16`) makes its image a sheet of frames
that size, numbered from 0 left to right, then top to bottom; `frame` is the one shown.
`flip_x` and `flip_y` mirror it, and `blend` draws it as `alpha` (the default), `add`
(glows), `multiply` (shadows) or `none`. Shapes have `blend` too.

**Animation.** An Animation component plays a list of its Sprite's frames: `frames` like
`"0-3"`, `"4,5,6,5"` or `"7-4"` (backwards), `fps` frames a second, round and round if
`loop`, else once - after which `playing` turns false and the last frame stays. Setting
`frames` to a new list starts it from its first frame.
- `play_animation(e, "4-7"[, fps[, loop]])` does that (adding an Animation if it has none)
  unless those frames are already playing, so it can be called every step:
  `if moving { play_animation(self, "1-4", 10) } else { play_animation(self, "0") }`.
- `stop_animation(e)` holds it where it is.

**3D model animations.** On a 3D entity whose Mesh is a glTF model (`.gltf` / `.glb` with a
skeleton), `play_animation(e, "walk"[, speed[, loop]])` plays that animation (by its name
in the file) through an Animator (added if it has none), crossfading from the last one
over the Animator's `blend` seconds; calling it every step with the same name does
nothing more. `animation_clips(e)` lists the names. See [3D](3d.md#animated-models).

**Cameras.** A Camera on an entity makes the game show the world around that entity's
Transform: its position at the middle of the screen, `zoom`ed, turned with its rotation.
The first Camera with `current` set is the one used; with none, the screen shows the world
from (0, 0) as it always has.
- `follow`: an entity to keep in view (with `follow_offset`), `deadzone`: how far (each way)
  that entity can move from the middle before the camera moves, `smoothing`: 0 to keep up
  at once, or about how many seconds it takes to catch up.
- `limit` with `limit_min` and `limit_max`: the camera never shows anything outside that
  box (world coordinates), so it stops at a room's edges. A room smaller than the screen
  is centred.
- `ui_layer` (50 at first): drawing layers from this one up are drawn on the screen
  itself, not in the world, so a HUD stays put while the world scrolls. Place HUD entities
  in screen pixels, (0, 0) at the top left, as you would without a camera.
- `pixel_snap` draws from whole pixels so pixel art doesn't shimmer.
- `camera()` returns the current camera entity (or nil): `camera().Camera.limit_max = vec2(512, 240)`.
- `snap_camera()` puts it straight where it is heading (after moving the player to a new
  room). `shake_camera(4, 0.3)` shakes it by up to 4 pixels, fading out over 0.3 seconds.
- `screen_to_world(mouse_position())` and `world_to_screen(p)` convert through it.

In the editor, each camera shows what it sees as a blue frame (and its limits in orange).
While playing, the view follows the game's camera inside the game-screen frame.

## The game's own components

A script can declare components of the game's own, beside the engine's:

```
component Health {
    var hp: int = 3            # fields need types: scenes save them
    var regen: number = 0.5
    var shield: bool
    var tint: color = "#ff4040"
    var team: enum(red, blue) = blue
    var target: entity         # starts as none
}
```

Field types: `int`, `number`, `bool`, `string` (up to 63 bytes), `vec2`, `vec3`, `color`,
`entity` and `enum(...)` (a choice of names; scripts see the name as a string). The editor
reads every `.bs` file of the project for them when it opens it (and again when a script
changes), so they appear in **Add component** and the inspector edits them; scene files
save them like any other; built games read them from their scripts too. Scripts use them
like the engine's: `self.Health.hp -= 1`, `has(other, "Health")`, `add(e, "Health")`,
`remove_component(e, "Health")`, and `find_with("Health")` lists every entity with one.

A component is data: what it does is up to the scripts that read it (a `damage.bs` on the
enemies that takes `hp` off whatever `Health` it touches, say). `bscheck` checks the
declarations too.

## Particles

A **Particles** component gives off sparks, smoke, fire, rain and the like, on an entity with
a Transform (2D) or a Transform3D (3D). Right-click in the editor's view, **New Particles
Here**, for ready-made ones (sparks, fire, smoke, explosion, rain, snow, magic). Its fields:
`emitting`, `rate` (a second) and `burst` (at once, when it starts), `max`, `lifetime`,
`speed` (each with a `_random` fraction), `angle` and `spread` (degrees: 2D -90 is up; 3D
they leave along the entity's up within a cone), `area`, `gravity`, `drag`, `size_start` /
`size_end`, `color_start` / `color_end`, `shape` (circle, square, image), `image`, `blend`
(add makes things glow), `layer`, `local` (they move with the entity) and `auto_destroy`
(the entity goes once its particles have).

- `emit(e[, count])` gives off `count` at once (without it: the burst), for a hit or a
  pickup: `emit(find("Sparks"), 12)`.
- `particle_count(e)` says how many are alive.
- Change the fields as with any component: `self.Particles.emitting = key_down("space")`.

A one-off effect is a prefab with `rate` 0, a `burst` and `auto_destroy`:
`spawn("prefabs/boom.scene", other.position)`.

## Tweens

`tween(e, property, target, seconds[, ease[, delay]])` moves a property smoothly from where
it is to `target`, a step at a time, with no code in `update()`:

```
tween(self, "position", vec2(160, 40), 0.6, "ease_out")      # slide in
tween(self, "Shape.color", "#ffffff00", 0.4)                  # fade out (a component's field)
tween(self, "scale", 1.5, 0.15, "back_out")                   # pop (a number: both ways)
tween(find("HUD"), "shown", 1, 0.3)                           # another script's variable
tween(self, "rotation", 90, 0.5, "ease_in_out", 1)            # after a second's wait
```

- The property is one an entity has (`position`, `x`, `y`, `z`, `rotation`, `scale`,
  `velocity`, `global_position`...), `"Component.field"`, or a variable of the entity's
  script. Numbers, vec2s, vec3s and colours can be tweened.
- Eases: `linear` (the default), `ease_in`, `ease_out`, `ease_in_out`, `cubic_in`,
  `cubic_out`, `cubic_in_out`, `sine_in`, `sine_out`, `sine_in_out`, `back_in`, `back_out`
  (overshoots a little), `elastic_out` (springs) and `bounce_out`.
- A delayed tween starts from wherever the property is when its wait ends, so delays make
  sequences: move there, then (with a delay as long as the first) fade.
- A new tween of the same property replaces the one running (unless either has a delay).
- `tween()` returns an id: `tween_repeat(id, times[, yoyo])` plays it again (`-1`: for ever;
  `yoyo` goes back and forth), `tween_then(id, "func")` calls one of this script's
  functions when it ends, `stop_tween(id)` stops it where it is (`stop_tween(e)`: all of an
  entity's), and `is_tweening(id)` or `is_tweening(e[, property])` says whether it runs.
- `ease(name, t)` is the curve itself, for your own animation: `ease("ease_out", 0.5)`.

Tweens move in the fixed step, after the scripts' `update()`s, so they replay exactly. One
whose entity is destroyed, or loses the component, just stops.

## Drawing

A script can draw for itself: give it a `draw()` function. It runs whenever the scene is
drawn, among the sprites, in the layer named by the script's `draw_layer` variable (0 if
it has none), and in the same place: in the world, or on the screen for layers from the
camera's `ui_layer` up.

```
var draw_layer: int = 60                     # above the HUD's sprites, on the screen

func draw() -> void {
    draw_rect(vec2(8, 8), vec2(52, 6), rgb(40, 20, 20))           # a health bar
    draw_rect(vec2(9, 9), vec2(hp * 5, 4), "#e04040")
    draw_text("HP", vec2(64, 7), rgb(255, 255, 255))
    for p in path { draw_circle(p, 2, rgb(255, 220, 80)) }
}
```

- `draw_rect(position, size, colour[, filled])` from its top-left corner (filled false: the
  outline), `draw_line(from, to, colour)`, `draw_circle(centre, radius, colour[, filled])`.
- `draw_text(text, position, colour[, size])` in the built-in 8x8 font, from its top-left.
  `text_size(text[, size])` says how much room it takes (this one works anywhere).
- `draw_image(path, centre[, frame[, frame_size[, flip_x]]])` an image, or one frame of a
  sheet.
- Colours are strings (`"#e04040"`, `rgb(224, 64, 64)`) or lists (`[224, 64, 64]`).

These work only while `draw()` runs. Use `draw()` only for drawing: a replay that skips
ahead doesn't draw the frames it skips, so anything `draw()` changed would differ
(`random()` inside it has numbers of its own, for that reason).

## Sounds and music

`play_sound(path[, volume[, pitch[, pan]]])` returns a **voice**: a number for the sound
while it plays (0 if every voice was busy). `pitch` is the playback rate (2 is an octave
up, 0.5 an octave down); `pan` goes from -1 (left) to 1 (right).
- `stop_sound(voice)`, `pause_sound(voice, true)`, `sound_playing(voice)`,
  `set_sound_volume(voice, v)`, `set_sound_pitch(voice, p)`, `set_sound_pan(voice, p)`.
- `stop_sounds()` stops everything (`stop_sounds("sfx")`: one bus).
- `sound_length(path)`: a file's length in seconds.
- `play_tone(wave, frequency, seconds[, volume])` plays a tone made on the spot, for
  blips with no file: `wave` is `"sine"`, `"square"`, `"triangle"`, `"saw"` or `"noise"`.

**Music.** `play_music("music/castle.wav"[, volume[, fade]])` loops a track on the music
bus, fading in over `fade` seconds while the one before fades out; asking for the track
that is already playing only changes its volume, so every room can ask for its own. The
music keeps playing through `load_scene()` and `push_scene()`. `stop_music([fade])` stops
it, and `music()` names what is playing (`""` for nothing).

## Scenes on a stack, and globals

`load_scene()` replaces the game's scene. `push_scene("menus/pause.scene")` puts a scene
*on top* of it instead - a pause menu, a map, a dialogue box - and `pop_scene()` takes the
top one off again, so the game underneath carries on just as it was left.
- `push_scene(path[, transparent[, update_below]])`: `transparent` (true unless you say
  otherwise) keeps drawing the scenes underneath; `update_below` keeps them running too
  (false: they wait, frozen).
- When a scene is covered, its scripts' `on_pause()` runs; when it is the top one again,
  `on_resume()` does.
- Each scene has its own entities: `find()` and the rest look only in the script's own
  scene. To pass things between scenes, use globals.

**Globals** are values kept while the game runs, whatever scene is showing:
`set_global("coins", 12)`, `global("coins", 0)` (the second value is the default),
`has_global(name)` and `delete_global(name)`. They take what `save_data()` takes: numbers,
strings, true/false, nil, vec2s and lists of these. (They last until the game ends; use
saved data for what must outlive it. In the editor, Play starts with none.)

## The window

`set_fullscreen(true)` / `fullscreen()`, `set_window_title(text)`, `show_cursor(false)`,
and `screen_size()` (the game's screen in game pixels).

## Actions

Instead of asking for keys and buttons by name everywhere, a game can name its actions
once and bind inputs to them:

```
func start() -> void {
    bind("jump", "space", "z", "pad:a")
    bind("left", "left", "a", "pad:left", "pad:left_x-")
    bind("right", "right", "d", "pad:right", "pad:left_x+")
}
func update(dt: number) -> void {
    self.velocity.x = action_axis("left", "right") * 90
    if action_pressed("jump") { jump() }
}
```

- `bind(action, input, ...)` adds up to six inputs: a key's name, `"mouse:0"` (1 right,
  2 middle), `"pad:a"` (any button `button_down()` names), or `"pad:left_x-"` /
  `"pad:right_y+"` / `"pad:lt+"` (a stick or trigger pushed that way past half way).
  `"shift"`, `"ctrl"` and `"alt"` mean either side, as they do for `key_down()`;
  `"leftshift"` or `"rightshift"` is one side only.
  `unbind(action)` takes them all off. Actions are kept for the whole game.
- `action_down(name)`, `action_pressed(name)` (once per press, like `key_pressed()`; a key
  pressed and let go within one frame still counts),
  `action_released(name)`, and `action_axis(negative, positive)`: -1, 0 or 1.

**More input:** `mouse_released(button)`, `mouse_wheel()` (how far the wheel turned since
the last frame), `text_typed()` (the characters typed since the last frame, for name
entry) and `touches()` (where each finger is, leaving out those on the on-screen
controls).

**Mouse-look:** `lock_mouse(true)` captures the mouse: the pointer is hidden and held in the
window, and `mouse_motion()` (how far the mouse moved since the last frame, in game pixels)
keeps reporting movement however far it goes. `lock_mouse(false)` lets it go, and
`mouse_locked()` says whether a lock was asked for. The lock lapses while the window is in
the background and comes back with it; on the web, browsers allow it only from a click, so
it takes hold on the next click on the game.

## Odds and ends

`random_seed(n)` (the same seed gives the same numbers from then on), `clipboard()` and
`set_clipboard(text)`, `fps()`, `frame_count()`, `log_warning(...)` and `log_error(...)`
(like `print()`, as a warning or an error), `open_url(url)` and `image_size(path)`.

## Structs and classes

A `struct` or `class` declares a type of your own: named fields with starting values, and
functions that work on them. Both go at the top level of a file, and can be written before
or after the code that uses them.

```
struct Point {
    var x: number = 0
    var y: number = 0

    func moved(dx: number, dy: number) -> Point { return Point(this.x + dx, this.y + dy) }
    func nudge() -> void { this.x += 1 }
}

class Animal {
    var name: string = "?"
    protected var legs: uint = 4
    private var mood: int = 0
    static var count: uint = 0                 # one for the class, not one per animal
    static const KINGDOM: string = "animalia"

    func init(name: string) -> void {          # runs when one is made: Animal("Blob")
        this.name = name
        Animal.count += 1
    }
    func speak() -> string { return "..." }
    func describe() -> string { return this.name + " says " + this.speak() }
    static func total() -> uint { return Animal.count }
}

class Dog : Animal {                           # a Dog is an Animal, with more
    var tricks: list[string] = []
    func init(name: string, trick: string) -> void {
        super.init(name)                       # the parent's init()
        push(this.tricks, trick)
    }
    override func speak() -> string { return "woof" }
}

func start() {
    var pets: list[Animal] = [Dog("Rex", "sit"), Animal("Blob")]
    for pet in pets { print(pet.describe()) }  # "Rex says woof", "Blob says ..."
    print(Animal.total(), Animal.KINGDOM)      # 2 animalia
}
```

**Making one.** Call the type like a function: `Dog("Rex", "sit")`. Every field gets its
starting value (the parent's fields first), then `init()` runs with the arguments, if the
type has one. A class without `init()` takes no arguments. A struct without `init()` takes
its fields in order, any number of them: `Point(1, 2)`, `Point(5)`, `Point()`. A field
without a value starts like a typed variable does (`0`, `""`, `[]`, nil for a class, and
a fresh struct with its starting values for a struct). A class's `init()` is inherited when
the child has none.

**this.** Inside a struct's or class's functions, `this` is the object the function runs
on. Fields and functions are always reached through it: `this.x`, `this.speak()`. A
function marked `static` has no `this`.

**Structs are values, classes are shared.** Storing a struct (in a variable, a field, a
list item, or as an argument or a result) stores a copy, so changing one copy never changes
another, and `==` compares their fields. Storing a class object stores the same object:
every name for it sees the same fields, and `==` is true only for the same object.

```
var a := Point(1, 2)
var b := a          # a copy
b.x = 10            # a.x is still 1
pts[0].nudge()      # changes the point in the list itself
```

A class field or variable can be nil; a struct can't. A struct can't hold itself (use a
class, or a list, for chains and trees).

**Access.** Each member is `public` (the default: any code can use it), `private` (only the
type's own functions can use it) or `protected` (the class's own functions, and those of the
classes that inherit from it). Structs have no children, so nothing in them is protected.

**Inheritance.** A class can have one parent: `class Dog : Animal`. It gets the parent's
fields and functions, and a `Dog` fits wherever an `Animal` is wanted (`var a: Animal =
Dog("Rex", "sit")`). A child can't declare a field the parent already has. Structs can't
inherit or be inherited from.

**Overriding.** To replace a function the parent has, the child writes `override` before
it, with the same parameters and result type: `override func speak() -> string`. Calls
always run the object's own version, even through a parent's function or an `Animal`
variable (above, `describe()` gets `"woof"` from a dog). `super.speak()` runs the parent's
version from inside the child. An override can make a function more visible (protected to
public), not less. Static functions can't be overridden. Every class has its own `init()`;
it needs no `override`.

**static.** A `static var` or `static func` belongs to the type itself, not to each object.
Use it through the type's name: `Animal.count`, `Animal.total()`. Static fields are shared
by every entity running the file, so they suit counters, caches and settings; a file's own
`var`s are still one set per entity. They get their starting values once, before the first
entity's variables.

**const.** A `const` field can't be changed after the object is made, except by its own
type's `init()` (`this.born = ...`). A `static const` is a constant of the type, and needs
its value where it is declared: `static const MAX: int = 8`. (`const` keeps a name from
changing; a const list's items can still change.)

**What a type can see.** Its functions and starting values see their parameters, `this`,
the file's structs and classes (and their static members), and the built-in functions. They
can't use the file's `var`s or `func`s, or `self`: an object can be handed to other
entities, so pass in what it needs (or keep it in a static field).

**Other things to know.** `print()` and `str()` show an object with its fields:
`Point(x: 1, y: 2)`. Objects can't be saved with `save_data()` (save their fields instead),
or set from **Script › vars**. Under `@strict`, fields need types (or `:=`), and functions
need parameter and result types, like everywhere else. Objects are found by the garbage
collector like lists, so nothing needs to be freed.

## Types

Types are optional. Write them, and the script is checked before it runs:

```
var speed: number = 90        # a number
var lives: int = 3            # a whole number
var name: string              # no value: it starts as "" (see below)
var spot := vec2(1, 2)        # := takes the value's type: spot is a vec2
var path: list[vec2]          # a list of vec2s

func hit(amount: int, from: entity) -> bool { ... }
func update(dt: number) -> void { ... }     # -> void: returns nothing
for i: int in 10 { ... }
```

| Type | Holds | Starts as |
|---|---|---|
| `number` (or `float`) | any number | `0` |
| `int` | a whole number | `0` |
| `unsigned int` (or `uint`), `unsigned number` | the same, but never below 0 | `0` |
| `string` | text | `""` |
| `bool` | `true` or `false` | `false` |
| `vec2` | a vector | `vec2(0, 0)` |
| `vec3` | a 3D vector | `vec3(0, 0, 0)` |
| `list`, `list[T]` | a list (of T) | `[]` |
| `entity` | an entity, or nil | `nil` |
| a struct's name | that struct | one with its fields' starting values |
| a class's name | that class or one of its children, or nil | `nil` |
| `any` | anything (checked only when it runs) | `nil` |
| `void` | only after `->`: the function returns nothing | |

`3` is an int and `3.0` or `1.5` a number. `+ - * %` of ints give an int, `/` always gives a
number, and `floor ceil round len sign` give ints. An int fits where a number is wanted, but
not the other way round (`floor()` or `round()` make a number whole). `nil` fits only an
entity, a class (or `any`).

<a id="const"></a>**const and unsigned.** `const` declares a name that keeps its first value:
`const MAX_HP: int = 5` at the top of a file (one per entity, like `var`), `const step := 2`
inside a function, or `const` / `static const` in a struct or class (see above). Changing
one is an error before the script runs, or, from another entity's script, when it runs.
`unsigned` before `int` or `number` (`uint` is short for `unsigned int`) means the value is
never below 0: `var coins: uint = 0`, `list[unsigned int]`. Storing a negative number in one
is an error - written in the source (`var u: uint = -1`), it is found before the script runs;
otherwise when it is stored (`coins -= 1` at 0: `coins is unsigned, and -1 is below 0`).
Unsigned values mix freely with the others in arithmetic.

Before the script runs, the checker finds: values that don't fit a variable, parameter or
result (`speed is a number, so it can't hold a string`), functions that don't always return,
`return` with a value from a `-> void` function, operators on the wrong types (`"a" - 1`),
wrong argument types for the built-in functions (`key_down(3)`) and the engine's callbacks
(`update(dt: string)`), properties that don't exist (`v.z` on a vec2), members a struct or
class doesn't have or doesn't let this code use, and changes to `const` names. It knows the types
of literals, of typed names, of the built-in functions' results and of entity properties
(`self.position` is a vec2, `self.x` a number).

What isn't known until the game runs - an untyped variable, another script's variable, a
component's field - is checked when it is stored into a typed variable, parameter or
result: `hp is an int, and 2.5 isn't a whole number`. Untyped code works as it always did.

**Strict typing:** `@strict` on the first line of a file makes types required: every `var`
needs a type or `:=` (and so does every `const` and struct or class field), every parameter
a type, and every function a `->` result (`-> void` when it returns nothing). Loop variables take their type from what they loop over. To make
every script in a project strict, set **Strict typing** in the editor's **Build > Project
Settings** (`strict_typing = true` in `project.bronto`); `bscheck --strict` does the same
from the command line.

## 3D

Entities with a **Transform3D** live in 3D: x to the right, **y up**, and the camera looking
down **-z** (so -z is "forward"). Distances are in units (think metres); rotation is three
angles in degrees: `x` pitch (nose up), `y` yaw (turn left), `z` roll. See
[3d.md](3d.md) for the components (Mesh, Camera3D, Light, RigidBody3D, Collider3D).

A script for a 3D entity starts with `@3d`. Then the transform properties are vec3s:

| Property | Meaning |
|---|---|
| `.position`, `.x`, `.y`, `.z` | Transform3D.position, relative to the parent |
| `.rotation` | Transform3D.rotation: `vec3(pitch, yaw, roll)` in degrees |
| `.scale` | Transform3D.scale (a vec3, or a number for all three) |
| `.global_position`, `.global_rotation` | in the world, after its parents; setting them moves it there |
| `.forward`, `.right`, `.up` | which way it faces (read only): its -z, +x and +y in the world |
| `.velocity`, `.angular_velocity` | RigidBody3D's (units / second; degrees / second about x, y, z) |

`on_collision(other, normal, speed)` gets a vec3 normal. Without `@3d` a file is 2D: its
`position` is a vec2. The difference is only in the checker's types, so `@3d` scripts can
still `find()` and message 2D entities, and the other way round. `.z`, `.forward`, `.right`
and `.up` on an entity without a Transform3D are errors when the script runs.

3D functions:
- `vec3(x, y, z)`, `cross(a, b)`; `length normalize dot distance lerp` work on vec3s.
- `look_at(e, point)` turns an entity to face a point (its -z towards it, +y kept up).
- `move_and_slide(e, velocity)` moves a character (an entity with a capsule Collider3D, say)
  by `velocity * dt`, sliding along what it meets, and returns the velocity with the parts
  that went into things taken out: `vel = move_and_slide(self, vel)`. Then
  `is_on_floor(e)`, `is_on_wall(e)` and `is_on_ceiling(e)` say what that move touched.
  See [3D characters](3d.md#characters).
- `apply_force(e, force[, point])`, `apply_impulse(e, impulse[, point])` take vec3s for
  3D bodies, and `apply_torque(e, torque)` spins one (a vec3, about x, y and z).
- `raycast(from, to[, ignore])` with vec3s returns `[entity, point, normal]` for the first
  solid 3D collider on the line, or nil. `entity_at(point)` with a vec3 returns a 3D collider
  there, and `overlap_sphere(centre, radius)` every 3D collider (triggers too) touching a
  sphere, as a list.
- `world_to_screen(point)` with a vec3 returns where a 3D point appears on the game's screen
  (a vec2 in game pixels, as `mouse_position()`), through the scene's Camera3D. A point behind the camera
  (or with no camera) gives `vec2(-100000, -100000)`.
- `screen_ray(point)` returns `[origin, direction]` (two vec3s): the line through a screen
  point into the scene, for clicking on 3D things:
  `var r := screen_ray(mouse_position())` then `raycast(r[0], r[0] + r[1] * 100)`.

```
@3d
# roller.bs - rolls a ball with the arrow keys, and follows it with the camera
var push: number = 12
var cam: entity

func start() -> void { cam = find("Camera") }

func update(dt: number) -> void {
    var dir := vec3(0, 0, 0)
    if key_down("left") { dir.x -= 1 }
    if key_down("right") { dir.x += 1 }
    if key_down("up") { dir.z -= 1 }
    if key_down("down") { dir.z += 1 }
    apply_force(self, dir * push)
    cam.position = self.position + vec3(0, 4, 7)
    look_at(cam, self.position)
}
```

## Debugging

The editor's **Scripts** tab (next to Files and Console) is a debugger for scripts as well
as their editor:

- Open a script (the scene's scripts are listed in the tab's bar) and click a line's
  number to put a **breakpoint** on it (a red dot); click again to take it away.
  Breakpoints move with their lines as you edit.
- Press **Play**. When a script reaches that line, the game stops right there, in the
  middle of its step: the line is marked, and the side shows the **call stack** (click a
  function to see where it was called from) and the **variables**: the function's own and
  the script's.
- **Continue** (F5) runs on to the next breakpoint, **Over** (F10) to the next line,
  **Into** (F11) into a function the line calls, **Out** (Shift+F11) back to whoever called
  this one, and **Stop** (Esc) stops playing.
- **Pause**, while playing, stops at the next line any script runs.

While stopped, the scene view and the inspector show the game as it is at that moment, but
can't be changed. With no breakpoints set, scripts run at full speed. From C, the same
debugger is `br_script_debug_attach` and friends in `script.h`.

## Errors

A mistake stops only the script that made it. The rest of the game keeps running, and
the error is reported once with the file and line, for example
`player.bs:12: unknown name 'speeed' (declare it with var)`. The editor shows it in the
status bar and the console while playing (double-click it there to open the script at that line). Mistakes the parser can find (unknown names and functions,
wrong argument counts, missing braces) are reported before the script runs, and the
editor's asset preview shows them as soon as you save the file. A loop that never ends
is stopped after a few million steps.

Scripts are read again when their file changes, and every time Play starts in the editor.

## Editing scripts

- **In the Bronto editor:** double-click a script in Files (or a mistake in the console)
  and it opens in the **Scripts** tab under the scene: BrontoScript in colour, undo,
  indenting, Ctrl+/ to comment lines out, and the script checked as you type, with the
  first mistake marked at its line. Ctrl+S saves; Play saves every changed script first.
  See [the editor's README](../tools/editor/README.md).
- **Another editor:** **Settings › Script editor** can open `.bs` files elsewhere instead:
  a command line where `{file}` is the script and `{line}` the line to show (presets for
  VS Code, Sublime Text, Notepad++ and Zed), or `system` for your system's default program.
  When a script has a mistake, it opens at that line.
- **VS Code:** `tools/vscode` has an extension with highlighting, completion (including the
  names that go inside strings: keys, gamepad buttons, axes, sound buses, components, and
  the project's sounds and scenes for `play_sound`, `spawn` and `load_scene`), hover
  documentation, signature help, go to definition and checking as you type. See
  [its README](../tools/vscode/brontoscript/README.md).
- **Any editor:** `bscheck file.bs ...` (built by `make tools`) prints each mistake as
  `file:line: message`, the format most editors and CI systems understand.

## C behaviours

The engine can't run a `.bs` script as fast as compiled code. When speed matters, write
the behaviour in C and register it by name:

```c
static void spin(br_world *w, br_entity self, void *state, float dt) {
    br_transform *t = br_get(w, self, br_component_find(w, "Transform"));
    t->rotation += 90 * dt;
}
static const br_behaviour k_spinner = { 0, NULL, spin, NULL, NULL, NULL };

br_script_register_behaviour("spinner", &k_spinner);   /* Script.file = "spinner" */
```

A C program can also call into scripts: `br_script_call`, `br_script_get_number`,
`br_script_set_number` and `br_script_get_string`.
