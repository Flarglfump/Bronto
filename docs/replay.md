# Recording and replaying a run

A Bronto run is deterministic. The clock is virtual, the random numbers are seeded and
saved data starts empty, so the same keys on the same frames always give the same game.
A recording therefore only needs the input: which keys went down or up, and on which
frame. Playing those back reproduces the run exactly, at any speed, in a window or a
browser tab. `include/bronto/replay.h` does both.

Pantry Quest comes with a recording of the whole game, from the title to the ending,
made by a bot (`tools/pantrybot`):

```sh
make replay                  # watch it in a window (make tools builds the player)
make replay SEGMENT=11       # starting at segment 11 (Cellar 5)
make replay-check            # play it headless and check every recorded frame
make replay-web              # one web page that plays it: build/web/pantryquest-replay/index.html
make playthrough             # let the bot play it again and record a new one
```

Pantry Quest II has one too, made by its own bot (`examples/pantryquest2/tools/bot`):

```sh
make pq2-replay              # watch it in a window (SEGMENT=N starts at segment N)
make pq2-replay-check        # play it headless and check every recorded frame
make pq2-replay-web          # one web page that plays it: build/web/pantryquest2-replay/index.html
make pq2-playthrough         # let the bot play it again and record a new one
```

## Watching

`bronto_player <game> --replay run.brec` plays a recording in a window.
`--segment N` starts at segment N, and `--headless` plays it as fast as it goes and
checks it. The keys control the playback. The game never sees them, or the mouse or a
gamepad:

| Key | |
|---|---|
| Space | pause / carry on |
| . (period) | one frame on, while paused |
| 1 2 3 4 | speed x1, x2, x4, x8 |
| 0 | as fast as it goes |
| N or ] | on to the next segment |
| P or [ | back to the start of this segment (or the one before, near its start) |
| H | hide / show the status bar |

The bar at the bottom shows the segment, the time into the run, the speed, and the
segment's number. Going forward skims the frames in between unseen, which takes a few
seconds for the far end of a long run. Going back starts the game again and skims up to
the segment.

A web page made with `bronto_web <project> --replay run.brec` does the same in a
browser. Its **Watch** button starts it, and `#seg=N` at the end of its address starts at
segment N. The page is one file, like any other Bronto web build (see [web.md](web.md)).

## Checking

Every 30th frame (by default), the recording keeps a hash of the picture (FNV-1a over
the framebuffer). A replay compares its own pictures with those hashes. A difference
turns the status bar red, logs the first frame that differs, and makes `--headless` exit
with status 1. The engine gives the same results on every system, so a recording made on
one machine plays back exactly on another: the Pantry Quest recording was made on Linux
(x86-64) and checks out frame for frame as WebAssembly too.

## The file

A recording is text (`.brec`), one line each:

```
brec 1                       the format
game Pantry Quest            for people; not checked
seg 889 Cellar 1: ...        a segment starts on frame 889
k 892 +265                   key 265 (up, input.h) goes down on frame 892; - is up
h 900 3fa04c11               the picture's hash at the end of frame 900
end 120256                   the run is 120256 frames long
```

## How it works

`br_replay_platform(inner, live)` wraps the real platform (or the headless one) and
replaces three things:

- **The clock** is virtual: exactly 1/60 s a frame, so every frame's `dt` and fixed steps
  are the same as when it was recorded.
- **The input**: everything the real devices report for the game (keys, text, mouse,
  gamepads, focus) is taken out of the event queue. The recorded keys for the frame go in
  instead (or, when recording, the keys the program asked for with `br_replay_key`).
- **Pacing**: in a window, frames are held to real time at the chosen speed. In a browser,
  which gives one frame per screen refresh, the extra frames of a faster speed are run
  after each one.

Pausing uses `br_engine.suspended`: the frame runs no update and draws nothing. When
nobody is watching (headless, or skimming), `br_engine.skip_render` leaves out the
drawing of frames whose pictures aren't checked, which makes skimming several times
faster. Saved data goes to a folder of its own (`<game> (replay)`), emptied first, since a
run must start from what the recording started from.

```c
/* recording */
br_replay_record("My Game", 30);
cfg.platform = br_replay_platform(br_platform_headless(), false);
br_engine_init(e, &cfg);
br_replay_attach(e);
br_replay_segment("Level 1");
br_replay_key(BR_KEY_RIGHT, true);           /* held from the next frame on */
br_engine_frame(e);
...
br_replay_finish("run.brec");

/* playing back */
br_replay_load("run.brec");
cfg.platform = br_replay_platform(br_platform_native(), true);
br_engine_init(e, &cfg);
br_replay_attach(e);
br_engine_loop(e, after_frame);             /* after_frame calls br_replay_after_frame */
```

## The Pantry Quest bot

`tools/pantrybot` plays Pantry Quest from the title to the ending and records it, in 20
segments: the title, then each stretch of the land followed by the cellar it leads to,
and the ending. It only presses keys. It reads the game to decide which ones (Pip's place,
the room's tiles, the enemies, the Game script's variables), but nothing reaches the game
except the keyboard, so the recording replays exactly.

- **world.c** loads every room file and plans routes across the land and the cellars:
  edges, doorways, cave mouths and stairs, locked doors (spending keys), cracked walls
  (bombs), jam the wafer plank crosses, and doors still sealed by a puzzle. It keeps to
  the parts of the land Pip is ready for.
- **act.c** walks a path over a 2-pixel grid of Pip's positions, using his own collision
  rules, and keeps clear of enemies. When crates are in the way it finds the pushes with
  a small Sokoban search.
- **fight.c** chooses an enemy that can be hurt now, stands where the spoon reaches it
  without touching it, and pokes. It blocks shots with the pot lid or steps out of their
  way, picks up drops, and tries again after a game over.
- **cellar.c** clears a cellar: the nearest thing not yet taken, then puzzles, then things
  that need a key, then things that need a bomb. It has tactics for the guardians that
  need them: the Big Dill, the Spice Wraith (skewers from a distance) and Baron Gorge's
  three courses.
- **puzzle.c** lights braziers with the fire chili, covers pressure plates with crates and
  rings the gong with a skewer.
- **route.c** is the order of the whole game: the spoon, the cellars one to nine, the
  upgrades on the way (heart pies, the copper spoon, the chef's jacket, the royal coat,
  the golden ladle, the Big Bomb Bag), and a stop at a healing spring when Pip is low.

The recorded run finishes the game in about 33 minutes of game time, with no game overs.
The bot plays it in a minute or two, headless, drawing only the frames it keeps hashes
for.

```sh
build/pantrybot --out run.brec                     # the whole game
build/pantrybot --test 'start_room = "d9_2_0", start_pos = vec2(128, 196), weapon = 4, ...' --route boss9
                                                   # one guardian, for working on its tactics
```

The bot needs POSIX threads (Linux and macOS, or MinGW's winpthreads).

## The Pantry Quest II bot

`examples/pantryquest2/tools/bot` plays Pantry Quest II from the title to the ending, in
16 segments: the title, each part of the world and its knight, the castle, the Count, the
escape and the ending. It is written in Python (standard library only) and drives
`tools/botrunner`, which runs the game headless and takes keys and questions on stdin.
Like the Pantry Quest bot it only presses keys, so what it records replays exactly.

Basil's movement is copied exactly into the bot (in Python, and again in C for speed), so
it plans inside a room by trying moves (walks, jumps held for so many frames, ball
bounces, whisk spins) and seeing where they land, and across the world from the game's
own map. The knights and the Count each have a model that predicts where they and their
shots will be, and the bot picks its moves frame by frame to keep clear of them while it
shoots. Its [README](../examples/pantryquest2/tools/bot/README.md) describes the parts.

The recorded run finishes the game in about 34 minutes of game time, taking every weapon,
the four seals and the diary pages on the way; it has two game overs, after which the bot
tries that stretch again. A run takes the bot about 40 minutes.

Making it found three bugs in the game, now fixed: the crawlspace door in C4 couldn't be
opened (a shot couldn't reach its cap), turning a page in the pause menu also changed a
setting on the new page, and a shot from Basil standing on the throne room floor passed
under Count Fromage.
