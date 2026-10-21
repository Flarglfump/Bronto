# Pantry Quest

A top-down adventure in the Pantry Kingdom, made the way an editor project is made: scenes,
prefabs and BrontoScript, with no C code. Greedy Baron Gorge has torn the Grand Recipe
into eight pages and hidden them all over the land. Pip, a young cook, picks up a wooden
spoon and sets out to find them.

The whole game is here, beginning to end: the land to explore, nine cellars, and an
ending.

- **The Pantry Plains:** 128 screens in ten regions: the start plains, the Lettuce Woods,
  Blueberry Lake, the Frosty Fields, the Fortune Cookie Field, the Chocolate Mountains,
  the Pickle Marsh, the Sugar Desert, the Candy Meadows and the Juice Coast.
  - **The Sugar Road** runs along the desert's north edge, from the Frosty Fields east
    to the Pickle Marsh: the safe way to the second cellar, and on down the Juice Coast
    to the Candy Meadows, without crossing the desert's or the mountains' later dangers.
  - **30 caves:** the old chef with the spoon (and hints), five shops, nine chefs with
    hints, eight secret bean stashes, two heart-pie caves, and four upgrades.
  - **Secrets show if you look:** a cliff a bomb can open has a crack down it, a bush
    that hides a cave is dry and brown in patches (the fire chili burns it away, and so
    does a bomb), and a cellar wall a bomb can open is cracked too.
  - **Two healing springs**, and three heart pies hidden out in the land.
  - **Upgrades**, each in a cave of its own:
    - spoons and ladles, each hitting harder than the last: the **copper spoon** (a cave
      in the Frosty Fields, once Pip has five hearts), the **silver ladle** (the Soup
      Cauldron's treasure) and the **golden ladle** (behind a cracked cliff in the
      mountains, for ten hearts). A poke does 1, 1.5, 2 and 3;
    - outfits: the **chef's jacket** (behind a dry bush in the Lettuce Woods) halves every
      hurt, and the **royal coat** (behind a cracked cliff on the Juice Coast, for eight
      hearts) quarters it. Pip wears it: blue, then red.
  - **Hints:** once Pip has the spoon, the old chef in the first cave puts out two cards.
    Step on **WHERE NEXT?** and he names the next cellar to clear (the first whose page is
    still missing, or the Banquet Hall), or says to get bombs first if it is hidden. Step
    on **UPGRADES?** and he tells of an upgrade out in the land Pip can reach now (a heart
    pie, a spoon or ladle, a jacket or coat), a different one each time.
  - **The doors of all nine cellars.** Two are hidden behind cracked cliffs, and the
    last, Baron Gorge's Banquet Hall, waits at the top of the Chocolate Mountains.
- **Stages:** each cellar is a stage of the game (its number), and each region of the land
  takes the stage of the cellar in it. Later stages bring tougher kinds of enemy (the
  plains' tomatoes near home; chili imps in the woods, sugar ants in the desert, carrot
  soldiers and toast knights in the mountains), more of them to a screen, and stronger
  versions of the same ones. An enemy's health grows with the stage (up to x3.5, keeping
  pace with the spoons and ladles) and so does the damage it and its shots do (up to x3,
  keeping pace with the heart pies and the chef's jacket). The guardians are tuned one by
  one for the spoon or ladle Pip is likely to have by then. Bombs, skewers and the fire
  chili hit harder as his spoon or ladle does.
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
- **The Jam Jar** (on the island in Blueberry Lake, over the long bridge): fourteen rooms,
  with toast knights whose buttered fronts turn a poke (hit them from the side or
  behind). Its treasure is the **wafer plank**, which Pip just carries: with it he
  walks over jam or water one tile wide. A key on a jam-ringed island and the guardian's
  room both need it. The guardian, the **Jammerwock**, rises out of the jam pool nearest
  Pip, spits jam and sinks again; it can only be hit while it is up.
- **The Candy Works** (in the Candy Meadows): fifteen rooms, with candy-corn soldiers
  that fire kernels when lined up with Pip. A syrup channel needs the plank. Its treasure
  is the **whisk**: out on the land, it whirls Pip to the door of a cellar whose recipe
  page he has, the next one each time. The guardian, the **Gumdrop King**, bounces off
  the walls, and when beaten bursts into two gumdrops, which burst into two more each.
- **The Fortune Hall** (in the Fortune Cookie Field): fourteen rooms, with dumplings. Its
  treasure is the **oven mitt**, carried: walk into a crate for a moment and it slides a
  tile. Crates plug the gaps on the way to the guardian (leaving a room puts its crates
  back). The guardian, the **Lucky Cookie**, floats after Pip inside a ring of crumbs;
  only once every crumb is knocked away can it be hurt, and then it flings fortunes.
- **The Sugar Mill** (behind a cracked cliff in the Sugar Desert): sixteen rooms, with
  quick sugar ants, and keys behind syrup and crates. Its treasure is the **baking
  tray**, carried: Pip's pot lid stops every shot, even spice and jam. The guardian, the
  **Sugar Golem**, leaps at Pip (watch for its shadow) and lands in a burst of sugar;
  only while it is stuck in the floor after landing can it be hurt.
- **The Soup Cauldron** (behind a cracked cliff in the Chocolate Mountains): sixteen
  rooms, with carrot soldiers that throw carrot coins. Its treasure is the **silver
  ladle**: it replaces the spoon, and every poke hits twice as hard. The guardian, the
  **Noodle Hydra**, is an iron pot with four noodle heads that spit broth. Three hits
  break a head off, and it flies loose round the room until the pot goes down with
  its last head.
- **Baron Gorge's Banquet Hall** (atop the Chocolate Mountains): the last cellar,
  seventeen rooms. Its door only opens for a cook carrying all eight recipe pages.
  - **Puzzles** that need everything Pip has found, each keeping a door shut (or a key
    hidden) until it is solved:
    - two crates-and-plates rooms: push crates onto every pressure plate (the oven mitt);
    - four braziers to light with the fire chili, which burn out again after nine
      seconds, so all four must be burning at once;
    - a gong across the jam that only a skewer can ring, once a crate is pushed out of
      the way;
    - a key on an island whose jam ring is too wide at the corners for the plank.
  - **Gauntlets:** rooms that shut Pip in with wave after wave of the kingdom's worst,
    each wave coming in as soon as the last is beaten.
  - **Baron Gorge**, in three courses, his bar counting all of them down:
    1. *The Feast:* behind his banquet table he flings dishes and rings for waiters,
       and can only be hit when he leans over the table to slam it.
    2. *Gorge:* he smashes the table and chases Pip, breathing in so hard he drags
       him closer. His belly turns every hit, but a popcorn bomb pulled into his mouth
       goes down whole and stuns him. Bombs drop in while Pip is short of them.
    3. *The Last Course:* he hides under one of three dish covers that shuffle, faster
       as he weakens. Under his: him, to be hit. Under the others: dishes, or a
       meatball.
  - Behind him, the **Grand Recipe**, whole again, and the ending. The file is saved,
    and Enter goes back to the title.
- **Keys belong to their cellar:** a key opens any locked door in the cellar it was
  found in, and nowhere else (the HUD shows that cellar's keys; out on the land, none).
  Shops don't sell keys. Every cellar has enough keys for all its locked doors, so
  whichever door a key goes on, Pip can always get to the end (`tools/check_cellars.py`
  checks this).
- Every cellar has a **map**, which shows all its rooms on the HUD, and a **compass**,
  which marks the guardian's room until it is beaten. Behind each guardian is a heart
  pie and a page of the recipe (in the Banquet Hall, the Grand Recipe), and the page
  takes Pip back out of the cellar's door.

**Maps.** On the land, the HUD shows a little colour map of the whole kingdom (each region in
its own colour), with Pip blinking where he is and the cellar doors he has found: red, or
gold once cleared. In a cellar it shows that cellar's rooms instead, with its map and
compass. The Pocket Pantry's **MAP** page (the M key, straight from play) shows the land
a pixel a tile, with the areas' names, the cellar doors found (numbered), where Pip is,
and fog over the screens he hasn't been to yet.

**The Pocket Pantry** has two pages, ITEMS and MAP: Tab (or a click on the other tab)
turns between them. On ITEMS, Pip's things for B sit on plates (the arrows or the mouse
choose one), his equipment is down the left (his spoon or ladle, his apron, jacket or
coat, and the **Big Bomb Bag**, sold in a cave shop, which holds 16 bombs), the
treasures he carries down the right, and the recipe pages along the bottom. Whatever
the mouse is over - a plate, equipment, a treasure, the pages - shows its name and what
it does.

| | Keys (at first: SETTINGS changes them) | Gamepad | |
|---|---|---|---|
| Walk | arrows or WASD | left stick or d-pad | Eight directions; Pip slides along walls and round corners |
| Poke with the spoon | X (or Space) | A | At full health it also throws a spark (as strong as a poke) |
| Use the B item | Z (or J) | B | Popcorn bombs, the pizza cutter, the skewer bow, the fire chili, the whisk |
| Pocket Pantry | Enter (or I) | X | Choose the B item, see equipment and treasures; Tab (LB/RB) for the map |
| Map | M | Y | The Pocket Pantry's map page |
| Next / previous item | E / Q | RB / LB | Changes the B item without opening anything |
| Quick save | F5 | Back (View) | Saves to the file straight away |
| Pause | Escape (or P) | Start (Menu) | Resume, save, settings, quit to the title |

Every menu works with the mouse too: over an option chooses it, a click does it. With a
gamepad, the stick or d-pad chooses, A is OK and B goes back, and the menus' hints name
the gamepad's buttons while it is the one being used. SETTINGS shows the gamepad's buttons
when opened with the gamepad (one per action: A, then the new button; X clears it), and
the keys when opened with the keys. Any controller works, in the same Xbox-style layout
(on a Steam Deck, its own controls); getting hurt makes it rumble. On a Steam Deck, add the
game to Steam as a non-Steam game and play it from there, so that Steam passes its controls
on as a gamepad.

**The title** offers PLAY, SETTINGS and QUIT. PLAY shows three files: an empty one starts
a new game (saved straight away), one with a game in it carries on from where it was
saved - the screen Pip was on, outside the cave he was in, or the door of the cellar -
showing where that is, his hearts, his recipe pages and the time played. ERASE wipes a
file. **The pause menu** saves the game to its file, opens the settings, or quits to the
title (saving first, or not). **The settings** set the music's and the sounds' volume and
two keys and a gamepad button for each action (Enter, then the new key; a key does one thing at a time), and
are kept for every file. Everything is kept with Bronto's saved data (`save_data`): the
files are `slot1` to `slot3`, the settings `settings`, in the game's folder for saved
data (on a Mac, `~/Library/Application Support/Pantry Quest`).

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
make pantryquest                                             # a game for this computer: build/games/pantryquest/<system>/
make pantryquest TARGET=web                                  # one web page: build/games/pantryquest/web/index.html
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
  - `hero.bs` handles Pip: walking in eight directions, the spoon, items, and being hurt.
  - `enemies/walker.bs` covers tomatoes, plums, burger knights, breadstick soldiers,
    toast knights (`front_guard`), candy-corn soldiers, dumplings, sugar ants and carrot
    soldiers.
  - `jelly.bs`, `bat.bs` and `meatball.bs` are cellar creatures; `gelatin.bs`,
    `big_dill.bs`, `spice_wraith.bs`, `jammerwock.bs`, `gumdrop.bs` (the Gumdrop King
    and the gumdrops he splits into), `lucky_cookie.bs` (with `crumb.bs`),
    `sugar_golem.bs` and `noodle_hydra.bs` (with `noodle_head.bs`) are the guardians. Sour olives and chili imps are
    walkers with their own vars (`prefabs/olive.scene`, `prefabs/imp.scene`).
  - `skewer.bs` and `fire.bs` are the skewer bow's skewers and the fire chili's flame.
  - `pickup.bs` covers anything to pick up or buy.
  - `room.bs` also runs a room's waves (`spawner.bs` holds an enemy back for a later
    wave) and its puzzle: pressure plates (tile `B`), braziers (`brazier.bs`, over tile
    `Q`), a gong (`gong.bs`), or carrying enough pages. A room's `sealed` sides stay shut
    until it is solved; pickups with `on_solve` appear then.
  - `menu.bs` and `map_view.bs` are the Pocket Pantry's two pages (items, and the map of
    the land); `hud.bs` draws the minimap.
  - `hint.bs` is the old chef's hint cards; `game.bs` works the hints out (`hint_next`,
    `hint_upgrade`) from `hint_data.bs`, which `tools/design_overworld.py` writes.
  - `enemies/baron.bs` is Baron Gorge, all three courses (`cloche.bs` is a dish cover),
    and `ending.bs` the ending card.
  - `warp.bs` is for cave mouths and doors.
  - `title.bs`, `files_menu.bs`, `pause_menu.bs` and `settings_menu.bs` are the title and
    its menus; `settings.bs` keeps the player's keys and volumes, and everything that
    reads a key asks it (`find("Settings").down("left")`). `game.bs` saves and loads a
    file (`save_game`, `load_game`).
  - `hud.bs` runs the HUD; `banner.bs` shows the name of
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
  `caves.txt` the caves, `cellar1.txt` to `cellar9.txt` the cellars (each
  with its own `name` and `tint`), and `cellars_unbuilt.txt` a stand-in entrance room for any
  cellar without its own file (none, now that all nine are built). It also writes `scripts/cellars.bs`, which tells the game each cellar's rooms,
  its guardian's room and its way out (for the HUD's map and compass).
- `check_cellars.py` tries every order of spending keys on locked doors in every cellar
  (with the items Pip has by then) and reports any order that leaves him stuck, doors
  that disagree, or a cellar missing its map, compass or heart pie. `make_rooms.py`
  runs it at the end.
- `make_map.py` draws the maps from the land: `art/world_map.bmp` (a pixel a tile, the
  regions' open ground in their colours), `art/map_names.bmp` (the areas' names in a tiny
  pixel font), the cellar numbers, the HUD's `art/minimap.bmp`, and `scripts/map_data.bs`
  (where each cellar's door is). `design_overworld.py` runs it; run it yourself after
  `make_art.py` changes a tile.
- `design_overworld.py --force` lays the whole land out again from its plan (the
  regions, where every cellar and cave goes, what is in each cave), keeping the
  hand-made screens around the start. It checks that every screen's edges meet its
  neighbours' and that every way in and out can be reached. It replaces the world
  files, so small changes are better made in `overworld.txt` by hand.

`project.bronto` leaves `tools/` out of built games (`exclude = tools`).
