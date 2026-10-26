# Pantry Quest II: The Castle of Count Fromage

A side-scrolling exploration adventure in the manner of the old 8-bit castle and cave games,
and the sequel to [Pantry Quest](../pantryquest/README.md). Like the first game it is an
editor project with no C code: scenes, prefabs, Tilemaps and BrontoScript. Open this folder
with **Load Project** and press Play, or pack it with `make pantryquest2` (add
`TARGET=web` for a web page).

> One night, under a full cheese moon, a castle rose over Crumb Village where no castle had
> ever been. By morning Chef Pip was gone. His apprentice, **Basil**, puts on a pot for a
> helmet, takes up the old pepper mill, and goes to bring him home.

## The story

- **Count Fromage**, a blue cheese left to age in a cellar for a hundred years and forgotten,
  has come back hungry. He has taken Chef Pip to cook his Eternal Fondue, which will spoil
  the whole world.
- **Old Mozz**, once the Count's butler, meets Basil in the ruins of the bakery and turns up
  again in each part of the world with advice. He tells Basil that the castle gate is shut
  with **four seals** (Salt, Sugar, Vinegar and Pepper), each held by one of the Count's
  **Spoiled Knights**.
- **The Count's diary:** eight pages are hidden around the world. Read in the pause menu,
  they tell how a patient young cheese became a monster. They also explain the ending.
- **The knights**, each a food left too long: **Sir Stale** (a loaf gone hard as stone), the
  **Abominable Sundae**, **Doctor Brine** and his pickled monster **Frankenfurter**, and the
  **Flambé Wyrm**, which has burned for years waiting for someone to taste it.
- **The end:** the Count's two-phase fight (the second phase, the Big Cheese, can only be
  hurt with garlic), Pip's rescue, a three-minute escape as the castle melts, and an ending
  with the journey's time, the treasure found, and the cast.

## The world

54 rooms (133 screens of 256 × 240) in six areas, each with its own tiles, enemies and music.

| Area | Rooms | What's there |
|---|---|---|
| Crumb Village | 7 | the surface: the bakery, the market square and its well, the rooftops, Chapel Road, the castle gate, the bridge |
| Rye Catacombs | 11 | the Dough Wrap, the map room, Sir Stale (the Seal of Salt and the popcorn) |
| Frostbite Cellar | 10 | Hot Sauce, Frost Sugar in the Sugar Vault (behind wax), the Abominable Sundae (the Seal of Sugar) |
| Molten Kitchens | 10 | the Leaven Boots, the Whisk Spin, the garlic switch, the Flambé Wyrm (the Seal of Pepper); the hottest rooms burn without the Oven Mitts |
| Brine Works | 8 | the Oven Mitts, the Garlic Wave, Doctor Brine (the Seal of Vinegar) |
| Castle Fromage | 8 | the foyer, gallery, wine cellar, library, fondue kitchen, the clock tower, the throne room, Pip's cell |

Each area has a **map table** that fills in its part of the map. There are **seven hearths**:
stand at one and press UP to save and restore energy and hot sauce.

### Abilities, in roughly the order they're found

| | Found | What it does | What it opens |
|---|---|---|---|
| Dough Wrap | Rye Catacombs | DOWN curls Basil up in dough | tunnels one tile high |
| Popcorn Kernels | Sir Stale | FIRE while rolled drops popcorn | cracked blocks; a popcorn bounce up a shaft |
| Hot Sauce | Frostbite Cellar | ROCKET fires a hot-sauce rocket | red wax seals, wax doors, tough enemies |
| Frost Sugar | Frostbite Cellar | peppercorns freeze enemies into platforms | high ledges, the fire-bean pool |
| Leaven Boots | Molten Kitchens | a far higher jump | the climbs to the Brine Works and to high shelves |
| Oven Mitts | Brine Works | heat no longer hurts; damage is halved | the hot kitchens and the Wyrm's furnace |
| Garlic Wave | Brine Works | peppercorns go further, through walls, and harder | the switch behind a wall; the Count |
| Whisk Spin | Molten Kitchens | spin-jump, and jump again in the air | the clock tower and anywhere high |

Along with those: **4 cake slices** (+100 energy each), **5 sauce packets** (+5 rockets
each) and the **8 diary pages**: 24 items in the world, plus the four seals.

## Controls

The defaults:

| | Keyboard | Gamepad |
|---|---|---|
| Run, aim, crouch | arrows / WASD | d-pad (the left stick always works too) |
| Jump | Z, Space | A |
| Pepper | X, J | X |
| Hot sauce | C, L | B |
| Aim (hold) | Shift, Q | LB |
| Roll up | E | Y |
| Pause (map, items, diary, settings) | Enter, P (Esc always works) | Start |
| Map | M, Tab | Back |

**Aiming:** UP aims straight up, and UP while running aims up at an angle. In the air,
DOWN aims straight down, and DOWN with LEFT or RIGHT aims down at an angle. Holding AIM
plants Basil's feet and turns the direction keys into aiming alone: LEFT or RIGHT aims
level, UP or DOWN straight up or down, and both together at an angle, eight ways.

**Crouching and rolling up:** DOWN on the ground crouches; AIM works crouched too.
DOWN again (not while aiming) rolls Basil up in the Dough Wrap, and so does ROLL UP at
any time on the ground. UP, JUMP or LEFT/RIGHT stand him up again; if there's no room to
stand, he unrolls into a crouch.

**Changing the controls:** every action has two keys and a gamepad button, and all of
them can be changed under **CONTROLS**, in OPTIONS on the title or SYSTEM in the pause
menu. Choose one and press the new key or button. They're saved for every journey, and
the game's signs and item descriptions show your keys. The arrow keys, the d-pad, Enter,
Escape, A and B always work on the controls screen, so it can't be made unusable.

**The mouse** works every menu: pointing at something chooses it, clicking does it, a
click on a volume bar sets it there, and a BACK button closes the pause menu.

## Finding the way

- **The map** (pause, or MAP) marks each hearth with an **S**, rooms with **items still
  to find** in gold, ways on that **need gear Basil hasn't got yet** in red (a wax door,
  cracked blocks, a crawlspace, a ledge only the Leaven Boots reach, the heat of the
  hottest kitchens...), and **where to go next** with a blinking blue frame. The minimap
  in the corner shows the same. JUMP (or a click) on the map's NEXT line tells how to get
  there.
- **After each guardian**, a word on where to go next.
- **Hearths**: UP at one saves the journey and restores energy and sauce, then offers to
  **travel to any other hearth** Basil has found.
- **RETURN TO LAST HEARTH**, in the pause menu's SYSTEM page, takes him straight back to
  the hearth he last rested at (not during a guardian's fight or the escape).

On a phone or tablet, the web build shows an on-screen stick and buttons.

## How it's made

The game uses the engine features that BrontoScript and the editor can reach:

- **Scenes on a stack.** The pause menu (`pause.scene`) and the dialogue box
  (`dialogue.scene`) are scenes pushed over the game with `push_scene` and closed with
  `pop_scene`. The game waits underneath, and its `on_resume` hears when they close.
  Globals (`set_global`/`global`) carry data between the scenes.
- **Classes.** `Inventory` in `game.bs` is saved and loaded as one thing. Enemies are a
  `Body` plus a `Brain` class hierarchy in `enemy.bs`: Crawler, Flyer, Swooper, Hopper,
  Walker (Knight extends it), Slider, Leaper, Ghost and Turret. The Wyrm's body is a `Trail`;
  the Count picks a `Spot` to appear at.
- **Tags.** `enemy`, `boss`, `freezable` (Frost Sugar works only on these), `platform` (a
  frozen enemy, which Basil can stand on), `door`, `item`, `hearth`, and more. Every prefab
  is tagged `transient`, and the Game clears all of them (`find_tagged`) when Basil changes
  rooms.
- **The Camera component** follows Basil with a deadzone and smoothing, stays inside the
  room's limits, and shakes for explosions and the escape. **Animation** components play
  the sprite sheets.
- **Drawing in script.** The HUD (energy, tanks, rockets, the minimap, the boss bar, the
  escape timer), the pause menu, the dialogue box and the title, intro and ending screens
  are drawn in `draw()` callbacks on their own `draw_layer`.
- **Actions** (`bind`, `action_pressed`) give every screen the same keyboard and gamepad
  controls. The player chooses their own: the controls screen reads the new key or button
  with `last_key()` / `last_button()`, and `controls.bs` rebinds the actions (`unbind`,
  `bind`) and saves them. **Music** crossfades between areas with `play_music` and carries on across
  scene changes (the title music plays on under the story). `save_data` keeps three save
  slots and the shared settings.
- **Tilemaps** per room (back, solid and front layers), with `tile_put`/`tile_get` for
  breakable blocks, doors that close their cells, melting wax and frozen platforms.

### Files

```
project.bronto        the project (main scene: title.scene; 256 x 240; strict typing)
title.scene           the title: three journeys, options                 scripts/title.bs
intro.scene           the story so far, before a new journey               scripts/intro.bs
main.scene            the game: World, Story, Game, Basil, Camera, HUD
pause.scene           the pause menu: map, items, diary, settings          scripts/pause.bs
hearth.scene          the hearth's menu: stay, or travel to another         scripts/hearthmenu.bs
controls.scene        the controls screen                                 scripts/rebind.bs
                      (each screen's Controls entity, scripts/controls.bs, keeps the keys)
dialogue.scene        the dialogue box                                    scripts/dialogue.bs
ending.scene          the ending                                          scripts/ending.bs
rooms/*.scene         the 54 rooms (made by tools/make_rooms.py)
prefabs/*.scene       shots, rockets, popcorn, spit, effects, drops, boss parts
scripts/              game.bs (rooms, saving, story), basil.bs, enemy.bs, door.bs, story.bs
                      (all the words), hud.bs, bosses/*.bs, world_data.bs (generated) ...
art/ sfx/ music/      made by the tools below
tools/                the Python tools that make everything above (not packed into the game)
```

### The tools

Everything in `art/`, `sfx/`, `music/`, `rooms/` and `prefabs/` is made by Python scripts in
`tools/` that use only the standard library: `make pantryquest2-assets` runs them all.

- `make_art.py` draws every picture from pixel text and code (`art_*.py`, `pixels.py`).
- `make_sound.py` and `make_music.py` synthesise the sound effects and the eleven music
  tracks on a small chip-style synthesiser (`synth.py`: two pulse voices, a triangle, noise
  drums). The music is original and written in the style of the old castle games: minor
  keys, a pulse lead with an echo a sixteenth behind it, galloping triangle bass. Each
  bar is checked for length as it's read.
- `world.py` describes every room. `make_rooms.py` turns it into scenes and
  `scripts/world_data.bs`. `make_locks.py` works out, for the map, which parts of each
  room need which gear (`tools/locks.json`, which `make_rooms.py` reads): it plays each
  room with the abilities in the order they're found and notes what each one opens up.
- `check_world.py` plays the world through on paper. It models Basil's jumps, rolls and
  bounces with the same numbers the game uses, picks up items as he reaches them, and beats
  each guardian once he has what it needs. It checks that every room, item and guardian can
  be reached from the start. It plays every jump 8 pixels lower than it really goes, so
  nothing the game needs is a pixel-perfect jump (`--exact` plays them at full height).
- `check_softlocks.py` makes sure Basil can't get stuck. At each stage of the game, from
  every spot he can reach, he must be able to get back to the start, perhaps by picking up
  what's there to find; otherwise the spot is reported. A guardian's room or a hazard
  counts as a way out, since a game over sends him back to a hearth.
- `check_scenes.py` makes sure the hand-written scenes' references between entities
  still point where they should (the camera following Basil).
- `make pantryquest2-check` runs `bscheck` and all three.

To try a part of the game in the editor, set the Game entity's Script vars in `main.scene`:
`start_room`, `start_pos` (or `start_door`, like `"W0"`) and `start_items`
(`["dough", "popcorn", "sauce"]`).
