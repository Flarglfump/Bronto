# Pantry Quest

A top-down adventure in the Pantry Kingdom, made the way an editor project is made: scenes,
prefabs and BrontoScript, with no C code. Greedy Baron Gorge has torn the Grand Recipe
into eight pages and hidden them all over the land. Pip, a young cook, picks up a wooden
spoon and sets out to find them.

This is the first part of a bigger game (eight cellars, a much larger land). It's playable
from start to finish now:

- **The Pantry Plains:** seven screens to explore, with three caves. One cave has the
  spoon, one is a shop, and one is a secret that a bomb opens.
- **The Cold Cellar:** nine rooms. It has locked doors and keys, shutters that close
  until the enemies are beaten, and a bombable wall with a stash behind it. Its treasure
  is the **pizza cutter**, and its guardian is the **Gelatin Giant**. Behind the guardian
  is the first recipe page.

| | Keys | |
|---|---|---|
| Walk | arrows or WASD | |
| Poke with the spoon | X (or K, or Space) | At full health the spoon also throws a spark |
| Use the B item | Z (or J) | Popcorn bombs, the pizza cutter |
| Inventory | Enter | Choose the B item there |

Pip's pot lid blocks small things thrown at him (seeds, toothpicks) while he faces them.

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
  - `jelly.bs`, `bat.bs`, `meatball.bs` and `gelatin.bs` are the cellar's creatures.
  - `pickup.bs` covers anything to pick up or buy.
  - `warp.bs` is for cave mouths and doors.
  - `hud.bs` and `menu.bs` run the HUD and the inventory.
- To test a room straight away, set the Game entity's Script vars in the editor, for
  example `start_room = "d1_2_1", start_pos = vec2(128, 200), weapon = 1, start_items = "bombs"`.

### The tools

Everything in the game is original and made by the scripts in `tools/`, using Python's
standard library only:

- `make_art.py` draws every sprite and tileset from pixel text (`--preview sheet.png`
  writes a contact sheet).
- `make_sound.py` synthesises the sound effects and the four music tracks with square,
  triangle and noise voices.
- `make_rooms.py` turns the maps in `tools/world/*.txt` into `rooms/*.scene`. The maps
  are easier to edit there than tile by tile. Each map is 16 x 11 characters, or a
  cellar room's 12 x 7 inside with its doors named.

`project.bronto` leaves `tools/` out of built games (`exclude = tools`).
