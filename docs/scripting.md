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

Only two things can appear at the top level of a file:

- `var name = value` declares a variable. Each entity running the script has its own copy,
  and the value is kept between calls. **Script › vars** in the inspector overrides the
  starting values for one entity, for example `speed = 120, name = "Rex", wrap = true`
  (only literal values: numbers, strings, `true`/`false`/`nil`, `vec2(x, y)`).
- `func name(a, b) { ... }` declares a function.
- `@strict`, on the first line only, makes types required (see [Types](#types)).

`#` starts a comment. Statements end at the end of the line, or at a `;`.

## Callbacks

The engine calls these functions when a script defines them:

| Function | When |
|---|---|
| `start()` | once, before the entity's first update |
| `update(dt)` | every fixed step (60 per second); `dt` is the step in seconds |
| `on_collision(other, normal, speed)` | this entity's collider started touching `other`'s. `normal` points away from this entity, and `speed` is how fast they hit |
| `on_collision_end(other)` | they stopped touching |
| `on_trigger(other)` | `other` entered a trigger collider (either this entity's or `other`'s) |
| `on_trigger_exit(other)` | ... and left it |

Every callback can declare fewer parameters than the engine passes.

## Values

| Type | Examples | Notes |
|---|---|---|
| number | `3`, `-0.5`, `1e3` | always floating point |
| string | `"hi"`, `'hi'`, `"a\nb"` | `+` joins strings, and turns anything else into text: `"score: " + 5` |
| true / false | `true`, `false` | |
| nil | `nil` | "nothing"; variables start as nil |
| vec2 | `vec2(3, 4)` | `.x`, `.y`, `.length`. `+ -` with another vec2; `* /` by a number |
| list | `[1, 2, 3]` | `list[0]`, `list[-1]` (the last item), `len(list)`, `push`, `pop`, `remove` |
| entity | `self`, `find("Player")` | see below |

`nil`, `false` and `0` count as false in `if` and `while`. Everything else counts as true,
including `""` and `[]`. `and` and `or` stop early and return one of their operands:
`name or "nobody"`.

## Statements

```
var x = 1                     # a local variable (inside a function)
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
| `.Shape`, `.Sprite`, ... | a component (any capitalised name), or nil if the entity doesn't have it. Read and write its fields: `self.Shape.color = rgb(255, 0, 0)`, `self.Label.text = "Hi"`, `self.Collider.trigger = true` |
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
(degrees), `from_angle(degrees, length)`.

**Text:** `str(v)`, `num("12")` (nil if it isn't a number), `print(...)` (to the log, and the
editor's console and status bar), `rgb(r, g, b[, a])` (a colour string).

**Lists:** `len(x) push(list, v) pop(list) remove(list, index) contains(list, v)
range(n) range(a, b[, step])`.

**Input:** `key_down(name) key_pressed(name) key_released(name)`. Key names include
`"left"`, `"a"`, `"space"`, `"enter"`, `"shift"` and `"F1"`, ignoring case.
`mouse_down(button) mouse_pressed(button)` (0 left, 1 right, 2 middle),
`mouse_position()` (in game pixels), and `time()` (seconds since the game started).

**The world:**
- `find(name)` returns the first entity with that name, or nil. `find_all(name)` returns a list.
- `children(e)` returns its direct children; `child(e, "Gun")` the one with that name, or nil.
- `spawn("prefabs/bullet.scene", position[, parent])` adds a copy of a scene file's entities
  and returns the first one. With a parent, they become its children, and position is in
  its space.
- `destroy(e)` removes an entity, and its children, at the end of the step.
- `has(e, "Shape")`, `add(e, "Motion")` (returns the component) and `remove_component(e, "Motion")`.
- `send(e, "function", args...)` calls a function on another entity's script if it has one,
  and quietly does nothing if not.
- `play_sound("sfx/jump.wav"[, volume])`.
- `load_scene("levels/2.scene")` ends this scene and starts that one (after this step).
  `quit()` ends the game. In the editor's Play, load_scene shows the other scene until Stop,
  and quit stops playing.

**Physics:**
- `apply_force(e, force[, point])` pushes over the next step. `apply_impulse(e, impulse[, point])`
  changes the velocity now.
- `raycast(from, to[, ignore])` returns `[entity, point, normal]` for the first solid collider
  on the line, or nil.
- `entity_at(point)` returns the collider at a point, or nil.

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
| `string` | text | `""` |
| `bool` | `true` or `false` | `false` |
| `vec2` | a vector | `vec2(0, 0)` |
| `list`, `list[T]` | a list (of T) | `[]` |
| `entity` | an entity, or nil | `nil` |
| `any` | anything (checked only when it runs) | `nil` |
| `void` | only after `->`: the function returns nothing | |

`3` is an int and `3.0` or `1.5` a number. `+ - * %` of ints give an int, `/` always gives a
number, and `floor ceil round len sign` give ints. An int fits where a number is wanted, but
not the other way round (`floor()` or `round()` make a number whole). `nil` fits only an
entity (or `any`).

Before the script runs, the checker finds: values that don't fit a variable, parameter or
result (`speed is a number, so it can't hold a string`), functions that don't always return,
`return` with a value from a `-> void` function, operators on the wrong types (`"a" - 1`),
wrong argument types for the built-in functions (`key_down(3)`) and the engine's callbacks
(`update(dt: string)`), and properties that don't exist (`v.z` on a vec2). It knows the types
of literals, of typed names, of the built-in functions' results and of entity properties
(`self.position` is a vec2, `self.x` a number).

What isn't known until the game runs - an untyped variable, another script's variable, a
component's field - is checked when it is stored into a typed variable, parameter or
result: `hp is an int, and 2.5 isn't a whole number`. Untyped code works as it always did.

**Strict typing:** `@strict` on the first line of a file makes types required: every `var`
needs a type or `:=`, every parameter a type, and every function a `->` result (`-> void`
when it returns nothing). Loop variables take their type from what they loop over. To make
every script in a project strict, set **Strict typing** in the editor's **Build > Project
Settings** (`strict_typing = true` in `project.bronto`); `bscheck --strict` does the same
from the command line.

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

- **Which editor opens them:** in the Bronto editor, **Settings › Script editor** sets the
  program that opens `.bs` files: a command line where `{file}` is the script and `{line}`
  the line to show. There are presets for VS Code, Sublime Text, Notepad++ and Zed, and
  empty means your system's default program. When a script has a mistake, it opens at
  that line.
- **VS Code:** `tools/vscode` has an extension with highlighting, completion, hover
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
