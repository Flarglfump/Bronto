# pantrybot

Plays Pantry Quest from the title to the ending and records the run (`replay.h`), in
segments: the title, each stretch of the land and the cellar it leads to, and the ending.
The bot only presses keys, so the recording plays back exactly.

```sh
make playthrough                   # examples/pantryquest/tools/playthrough.brec
make replay                        # watch it
build/pantrybot --out run.brec     # the same, by hand
```

| File | |
|---|---|
| `main.c` | The engine and the bot's thread, which takes turns with it a frame at a time |
| `game.c` | Reading the game: entities by name, the Game's variables, the room's tiles |
| `world.c` | Every room from its file, and planning routes across the land and the cellars |
| `act.c` | Walking (a path over Pip's own collision), facing, the spoon, the B item, crates |
| `fight.c` | Enemies: what can be hurt now, where to stand, shots, drops, game overs |
| `cellar.c` | Clearing a cellar, opening hidden ways in, and the guardians' tactics |
| `puzzle.c` | Braziers, pressure plates and the gong |
| `route.c` | The whole game, in order |

`--test 'Game vars' --route boss3` starts in a room with what the Game's vars give and
runs one part, for working on it. See `docs/replay.md`.
