# Pop Defense

A balloon-popping tower defense game for the Bronto engine, in the style of
Bloons TD 5 and 6. It has one map, *Meadow Lanes*, and 30 rounds.

```sh
make towerdefense                               # build and play
build/examples/towerdefense --autopilot         # watch the scripted player
build/examples/towerdefense --headless          # play all 30 rounds with no window; exit code 0 = victory
```

## How to play

Balloons float along the dirt path. Build towers on the grass beside it. Every layer
you pop earns $1, and clearing a round pays a bonus. Each balloon that reaches the end
costs lives equal to the layers left inside it. Survive round 30 to win.

| Input | Mouse / keyboard | Gamepad |
|---|---|---|
| Pick a tower to build | click the shop, or `1`-`5` | `LB` / `RB` |
| Build it | click the grass (Shift+click keeps building) | `A` |
| Select a tower | click it | `A` |
| Upgrade path 1 / path 2 | click the panel, or `,` / `.` | `A` on the panel |
| Change targeting | click the panel, or `Tab` | `Y` |
| Sell for 70% | panel, or `Backspace` / `Delete` | `A` on the panel |
| Start round, then change speed (x1, x2, x3) | `PLAY` button, or `Space` | `X` |
| Cancel / deselect | right click, or `Esc` | `B` |
| Pause | `P`, or `Esc` when nothing is selected | `Start` |
| Mute music / fullscreen | `M` / `F11` | |

On a phone or tablet (the web build), a tap is a click, and touching the screen brings up two
on-screen buttons: `CANCEL` (the right click: cancel / deselect) and `PAUSE`
(`cfg.touch_controls` in `main.c`).

The cursor moves with the left stick or the d-pad. The `AUTO` button starts each round
as soon as the previous one is cleared.

## How the code is organised

The game is split into **rules** and **presentation**, and the rules are split again into
one file per part of the game. Every file is short enough to read in one sitting.

```
td.h              the rules' public API: every type, plus the commands a player can issue
td_internal.h     how the rule modules call each other

td_data.c         WHAT things are     balloon, tower, upgrade and round tables
td_map.c          WHERE things are    the path curve and the buildable ground
td_balloon.c      balloons            spawn, move, pop into children, leak
td_tower.c        towers              choose a target, attack
td_projectile.c   projectiles         fly, hit, pierce, explode
td_round.c        rounds              send each group of balloons on schedule
td_game.c         THE GAME            state, td_game_step(), player commands
td_autopilot.c    a scripted player   attract mode, headless demo, balance tests

view.h            the presentation's shared declarations
view_world.c      draw the map, towers, balloons and projectiles
view_ui.c         the side panel: shop, upgrades, play button
view_fx.c         pops, explosions and floating text (ECS entities)
sfx.c             synthesised sounds and music
main.c            engine setup, scenes, input -> commands, events -> message bus
```

**Read in this order:** `td.h`, then `td_game.c`, then whichever system you are curious
about.

### Data flows one way

```
 mouse / keys / gamepad
          |
          v
 main.c  ---- commands ---->  rules (td_*.c)        td_place_tower, td_upgrade_tower,
                                   |                 td_sell_tower, td_start_round ...
                                   | td_game_step() at 60 Hz
                                   v
                              td_event list          POP, LEAK, EXPLOSION, ROUND_END ...
                                   |
 main.c forwards them to the engine's message bus as "td.event"
                                   |
                  +----------------+----------------+
                  v                                 v
        view_fx.c (ECS effects)              sfx.c (sound, rumble)

 view_world.c / view_ui.c only READ the game state to draw it.
```

The rules never call the engine, so they:

- are **deterministic**: there are no clocks, no random numbers and no `sin`/`cos`.
  The same commands always give the same game, on every CPU.
- are **unit tested** without a window (`tests/test_towerdefense.c`). That includes a
  full 30-round game played by the autopilot.
- run identically at **x1, x2 and x3 speed**, because fast-forward just runs more rule
  steps per frame.

### One step of the game (`td_game_step`)

1. **Rounds** release this round's balloons on schedule.
2. **Balloons** move along the path. A balloon that reaches the end leaks and costs lives.
3. **Towers** cool down, pick a target by their priority and attack it.
4. **Projectiles** fly. Each hit pops layers until its pierce runs out, and bombs explode.
5. Popped balloons and spent projectiles are removed.
6. When lives reach 0, the game is lost.
7. When the round is empty, the bonus is paid. After round 30, the game is won.

## The rules

**Balloons** are stacks of layers. Popping one releases its children: a pink becomes a
yellow, a black becomes two pinks, and so on down to red. A hit that does more damage
than a layer needs carries the extra damage through to the children. "RBE" is the
number of pops a balloon contains, and it is what a leak costs you.

| Balloon | Speed | Children | RBE | Special |
|---|---|---|---|---|
| Red | 1.0 | - | 1 | |
| Blue / Green | 1.4 / 1.8 | one smaller | 2 / 3 | |
| Yellow / Pink | 2.8 / 3.0 | one smaller | 4 / 5 | fast |
| Black / White | 1.8 / 2.0 | 2 pink | 11 | black is bomb-proof, white is ice-proof |
| Lead | 1.0 | 2 black | 23 | sharp-proof: darts and tacks bounce off |
| Zebra | 1.8 | black + white | 23 | bomb-proof and ice-proof |
| Rainbow | 2.2 | 2 zebra | 47 | |
| Ceramic | 2.5 | 2 rainbow | 104 | takes 10 hits |
| Blimp | 1.0 | 4 ceramic | 616 | takes 200 hits, can't be frozen |

Any balloon can be **camo**: only towers with detection target it. Towers that hit an
area can still pop it.

**Towers**

| Tower | Cost | Path 1 | Path 2 |
|---|---|---|---|
| Dart | $200 | Sharp Darts, Razor Darts, Spike Ball | Quick Shots, Eagle Eye (camo), Triple Shot |
| Tack | $260 | Faster Tacks, Even Faster, Blade Storm | Long Tacks, More Tacks, Ring of Fire |
| Bomb | $450 | Bigger Bombs, Heavy Bombs, Concussion | Fast Reload, Missiles, Cluster Bombs |
| Ice | $400 | Permafrost, Deep Freeze, Ice Shards | Wide Freeze, Quick Freeze, Arctic Wind |
| Sniper | $350 | Armor Piercing, Heavy Rounds, Deadeye | Faster Firing, Semi-Auto, Bounty Drop |

Each path has 3 tiers, and only one of the two paths can go to tier 3. A tower sells for
70% of everything spent on it. Aimed towers (Dart, Bomb, Sniper) target **First**,
**Last**, **Strong** or **Close**.

## Extending it

- **A new upgrade:** write a three-line function that changes `td_stats`, then put it
  in a tower's `paths` in `td_data.c`.
- **A new tower:** add a `td_tower_type`, then a row in `k_towers` with its base stats and
  upgrades. If it needs a new kind of attack, add a `td_attack` and handle it in
  `td_towers_update` (`td_tower.c`). Then draw it in `view_draw_tower`.
- **A new balloon:** add a `td_balloon_type`, a row in `k_balloons` and a colour in
  `view_world.c`.
- **Different rounds:** edit `k_groups` in `td_data.c`. Each row is
  `{ round, balloon, count, seconds apart, start delay, camo }`.
- **Another map:** `td_map_build` turns a list of control points into the path and adds
  the obstacles, so a second map is another list of points and obstacles.

After changing balance, run `make test`. Its full-game test checks that the autopilot
can still win and that a player who builds nothing still loses.
