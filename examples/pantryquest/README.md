# Pantry Quest

A top-down adventure in the Pantry Kingdom, made the way an editor project is made: scenes,
prefabs and BrontoScript, with no C code. Greedy Baron Gorge has torn the Grand Recipe
into eight pages and hidden them all over the land. Pip, a young cook, picks up a wooden
spoon and sets out to find them.

This is the first part of a bigger game. The whole land is there to explore now, with
the first three of its nine cellars:

- **The Pantry Plains:** 128 screens in ten regions: the start plains, the Lettuce Woods,
  Blueberry Lake, the Frosty Fields, the Fortune Cookie Field, the Chocolate Mountains,
  the Pickle Marsh, the Sugar Desert, the Candy Meadows and the Juice Coast.
  - **26 caves:** the old chef with the spoon, five shops, nine chefs with hints, eight
    secret bean stashes behind cracked cliffs and suspicious bushes (a popcorn bomb
    finds them), and two heart-pie caves.
  - **Two healing springs**, and three heart pies hidden out in the land.
  - **The doors of all nine cellars.** Two are hidden behind cracked cliffs, and the
    last, Baron Gorge's Banquet Hall, waits at the top of the Chocolate Mountains.
    Cellars 2 to 9 are still being dug: for now each door leads to an entrance room
    where a cook says so.
- **The Cold Cellar** (in the Frosty Fields): nine rooms. It has locked doors and keys,
  shutters that close until the enemies are beaten, and a bombable wall with a stash
  behind it. Its treasure is the **pizza cutter**, and its guardian is the **Gelatin
  Giant**, who wobbles and spits sprinkles.
- **The Pickle Pantry** (in the Pickle Marsh): ten rooms, with sour olives that spit
  pits. Its treasure is the **skewer bow**: each skewer costs a bean and hits twice as
  hard as the spoon. Its guardian, the **Big Dill**, charges at Pip and only feels a hit
  while it sits dizzy after slamming into a wall.
- **The Spice Rack** (deep in the Lettuce Woods): fourteen rooms, with chili imps. Its
  treasure is the **fire chili**: its flame scorches enemies and burns away a bush that
  hides a cave. Its guardian, the **Spice Wraith**, drifts from spot to spot bursting
  out rings of spice, and can only be hurt while it is solid.
- Every cellar has a **map**, which shows all its rooms on the HUD, and a **compass**,
  which marks the guardian's room until it is beaten. Behind each guardian is a heart
  pie and a page of the recipe, and the page takes Pip back out of the cellar's door.

| | Keys | |
|---|---|---|
| Walk | arrows or WASD | |
| Poke with the spoon | X (or K, or Space) | At full health the spoon also throws a spark |
| Use the B item | Z (or J) | Popcorn bombs, the pizza cutter, the skewer bow, the fire chili |
| Inventory | Enter | Choose the B item there |

Pip's pot lid blocks small things thrown at him (seeds, toothpicks) while he faces them.

The HUD is a walnut kitchen counter with a gingham trim: the map is pinned on a recipe
card, the beans, keys and bombs are on paper tags, the items on B and A sit on plates,
and Pip's hearts are jam cookies. Coming into a new part of the land, or a cellar, its
name comes in on a red ribbon. The inventory is a recipe page with Pip's things on
plates; the arrows or the mouse choose one, and its name and what it does show
underneath. A cellar's guardian makes an entrance: bars close in, it swells into view
with a roar, and its name comes in on a card (Enter skips that); while it fights, its
name and strength stay at the bottom of the screen.

## Playing it

```sh
build/bronto_player examples/pantryquest                     # from its files (make tools)
build/editor examples/pantryquest                            # in the editor: open main.scene and press Play
build/bronto_web examples/pantryquest -o pantry/index.html   # a web page (make web)
```

`make web` also builds `build/web/pantryquest/index.html`.

## How it's made

- `title.scene` is the title screen.
- `main.scene` is the game: the HUD, Pip, and the **Game** entity (`scripts/game.bs`).
  The Game entity keeps track of everything Pip carries and everything he has done. It
  also brings in rooms as he moves.
- **Rooms** (`rooms/*.scene`) are ordinary scenes. Each has a root entity
  (`scripts/room.bs`) with a **Tilemap** child (the floor and walls, one character per
  tile) and a child for every enemy, item and doorway. The Game entity spawns a room at
  the top of the play area. Walking off an edge slides the next room in; caves and
  cellar doors fade instead. Open any room in the editor to look at it.
- **Prefabs** (`prefabs/*.scene`) hold the enemies, shots, pickups, effects, the
  inventory and the music.
- **Scripts:**
  - `hero.bs` handles Pip: grid-aligned walking, the spoon, items, and being hurt.
  - `enemies/walker.bs` covers tomatoes, plums, burger knights and breadstick soldiers.
  - `jelly.bs`, `bat.bs` and `meatball.bs` are cellar creatures; `gelatin.bs`,
    `big_dill.bs` and `spice_wraith.bs` are the guardians. Sour olives and chili imps are
    walkers with their own vars (`prefabs/olive.scene`, `prefabs/imp.scene`).
  - `skewer.bs` and `fire.bs` are the skewer bow's skewers and the fire chili's flame.
  - `pickup.bs` covers anything to pick up or buy.
  - `warp.bs` is for cave mouths and doors.
  - `hud.bs` and `menu.bs` run the HUD and the inventory; `banner.bs` shows the name of
    where Pip comes in (a room's `region` var, or its cellar's `name`), and `boss_ui.bs`
    a guardian's entrance and strength (the game times the entrance: `boss_arrives`).
- To test a room straight away, set the Game entity's Script vars in the editor, for
  example `start_room = "d1_2_1", start_pos = vec2(128, 200), weapon = 1, start_items = "bombs"`.
  Any of the Game's variables can be set there, lists too: `maps = ["d2"], compasses = ["d2"]`
  shows the Pickle Pantry's whole map with its guardian marked.

### The tools

Everything in the game is original and made by the scripts in `tools/`, using Python's
standard library only:

- `make_art.py` draws every sprite and tileset from pixel text, and the HUD and menu
  textures (wood, gingham, parchment, plates, ribbons) from code (`--preview sheet.png`
  writes a contact sheet).
- `make_sound.py` synthesises the sound effects and the four music tracks with square,
  triangle and noise voices.
- `make_rooms.py` turns the maps in `tools/world/*.txt` into `rooms/*.scene`. The maps
  are easier to edit there than tile by tile. Each map is 16 x 11 characters, or a
  cellar room's 12 x 7 inside with its doors named. `overworld.txt` holds the land,
  `caves.txt` the caves, `cellar1.txt` to `cellar3.txt` the cellars built so far (each
  with its own `name` and `tint`), and `cellars_unbuilt.txt` the entrance rooms of the cellars to
  come. It also writes `scripts/cellars.bs`, which tells the game each cellar's rooms,
  its guardian's room and its way out (for the HUD's map and compass).
- `design_overworld.py --force` lays the whole land out again from its plan (the
  regions, where every cellar and cave goes, what is in each cave), keeping the
  hand-made screens around the start. It checks that every screen's edges meet its
  neighbours' and that every way in and out can be reached. It replaces the world
  files, so small changes are better made in `overworld.txt` by hand.

`project.bronto` leaves `tools/` out of built games (`exclude = tools`).
