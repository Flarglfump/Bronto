# The Pantry Quest II bot

A bot that plays Pantry Quest II from the title to the ending and records the run as a
`.brec` (see [docs/replay.md](../../../../docs/replay.md)). Like Pantry Quest's bot it
only presses keys: it reads the game (Basil's state, the room's tiles, the enemies, the
Game script's variables) to decide which ones, but nothing reaches the game except the
keyboard, so the recording replays exactly in the player and on the web.

```sh
make pq2-playthrough                 # the whole game: examples/pantryquest2/tools/playthrough.brec
make pq2-replay-check                # play the recording back headless and check every frame
make pq2-replay SEGMENT=12           # watch it, from segment 12
```

It is Python 3 with no packages beyond the standard library. It drives
`build/botrunner` (`tools/botrunner`, built by `make botrunner`), which runs the game
headless over stdin and stdout: keys, frames, and questions about the world. The movement
simulation is also in C (`simc.c`), built on first use with `cc` and loaded with ctypes;
without a compiler the bot falls back to the same simulation in Python, slowly.

## The parts

- **runner.py** talks to the botrunner: keys, frames (with how many fixed steps the next
  frames will take), segments, and reading entities and script variables.
- **game.py** reads the game: the Game script's state, Basil, and each room's Tilemaps.
- **sim.py** is an exact copy of Basil's movement (`scripts/basil.bs`): walking, jumping,
  the dough ball, popcorn bounces, the whisk spin, the Leaven Boots, ladders, water and
  molten cheese. **simc.c** and **simfast.py** are the same in C, checked against it
  (`testsim.py`).
- **nav.py** plans inside a room. Its nodes are the places Basil can stand (or roll, as
  the ball); its edges are moves (a walk, a jump with the button held for so many frames,
  a ball bounce, a whisk spin chain) played through the simulation to see where they land.
  Paths are found with A*, through blocks a shot can break, and never through spikes or
  molten cheese except to get out of them.
- **worldnav.py** plans across rooms from `tools/world.py`'s map: doors, caps that need a
  shot or a weapon, and the one-way drops.
- **bot.py** follows a plan frame by frame, starting each move from exactly where the
  simulation says it works, clearing enemies that keep knocking Basil off, and planning
  again when something goes wrong.
- **fight.py** shoots enemies: which aim and stance reach them, shields and armour, and
  rockets where pepper won't do.
- **travel.py** goes from room to room and fast-travels between Hearths when that's
  quicker, or when the way back is one-way.
- **blocks.py** breaks crumb, wax and cracked blocks, and shoots switches.
- **freeze.py** crosses gaps on frozen enemies (Frost Sugar).
- **boss.py** fights the knights and the Count: a model of each one predicts where it and
  its shots will be over the next 45 frames, and the bot picks, every frame, the plan of
  moves that keeps clear of them, shooting whenever a shot can land.
- **route.py** is the order of the whole game, one step after another (the weapons and
  boots, the four seals, the diary pages, cakes and packets on the way), and tries a step
  again after a game over.
- **viewrec.py** makes a contact sheet of a stretch of a recording, for seeing what went
  wrong.

## Working on it

```sh
cd examples/pantryquest2/tools/bot
python3 -u route.py --out run.brec                         # the whole game
python3 -u route.py --out run.brec --stop-after stale      # up to Sir Stale
python3 -u route.py --resume run.brec --at castle --out run2.brec
                                   # replay run.brec up to the castle step, carry on from there
python3 viewrec.py run.brec 100000 101000 50 sheet.png     # a picture every 50 frames
```

Each run also writes `run.brec.steps.json`, the frame each step started on, which is what
`--resume` uses. A recording made by resuming is a whole run from the title: the keys
before the step come from the earlier recording.
