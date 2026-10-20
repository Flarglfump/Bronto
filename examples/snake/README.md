# Snake

The classic: eat apples to grow, and don't hit the walls or yourself. Every fifth
apple a gold bonus appears for a few seconds, and the sooner you reach it, the more
it is worth. The snake speeds up as it grows.

```sh
make snake                               # build and play
build/examples/snake --autopilot         # watch the computer play
build/examples/snake --headless          # no window: writes screenshots and a WAV
```

Controls: arrows / WASD / d-pad / left stick to move, P / Esc / Start to pause,
Enter / A to start, M to mute the music, F11 or Alt+Enter for fullscreen.

## How the code is organised

The game is split into **rules** and **presentation**, the same way as
[Pop Defense](../towerdefense/README.md).

```
snake.h              the rules' public API: board size and tuning, types, events, commands
snake_internal.h     how the rule modules call each other

snake_body.c         the snake itself     its cells on the grid: move, grow, collide
snake_food.c         apples and bonus     where they appear, what the bonus is worth
snake_game.c         THE GAME             state, snake_step(), steering with snake_turn()
snake_autopilot.c    a computer player    attract mode, headless demo, tests

view.h               the presentation's shared declarations
view_board.c         draw the board, the snake, the apples and the HUD
view_fx.c            particles, score pop-ups and screen shake (ECS entities)
sfx.c                synthesised sounds and music
main.c               engine setup, scenes, high score, input -> snake_turn()
```

**Read in this order:** `snake.h`, then `snake_game.c`.

### Data flows one way

```
 keys / gamepad --(named actions)--> main.c --snake_turn()--> rules (snake_*.c)
                                                                  |
                                                     snake_step() on the fixed timestep
                                                                  v
                                                         snake_event list   ATE, ATE_BONUS, DIED, WON
                                                                  |
                        main.c forwards them to the engine's message bus as "snake.event"
                                                                  |
                                          +-----------------------+-----------------------+
                                          v                       v                       v
                                  view_fx.c (effects)     sfx.c (sound, rumble)   main.c (game over, high score)

 view_board.c only READS the game state to draw it.
```

The rules never call the engine, so they are deterministic: the same seed and the
same turns always replay the same game. They are also unit-tested without a window
(`tests/test_snake.c`).

### One tick of the game (`snake_step`)

1. Apply the next queued turn. Up to 3 turns can be queued, so quick double-taps work.
2. Look at the cell ahead of the head.
3. **Crash?** Walls and the body end the game. Moving into the tail's cell is fine
   when the tail moves away this tick.
4. **Eat** the apple (+10, grow 1) or the bonus (+50 plus 1 per tick left, grow 3).
5. Move: the head advances, and the tail follows unless the snake is growing.
6. Count the bonus timer down.
7. **Won** if the snake fills the board.
8. After an apple: place a new one, and every fifth apple a bonus.

## Extending it

- **Tuning:** points, growth, speed and bonus timing are all `#define`s at the top of
  `snake.h`.
- **A new item** (say, a shrinking mushroom): put it on the board in `snake_food.c`,
  handle eating it in `snake_step`, add a `snake_event` type, then draw it in
  `view_board.c` and give it an effect and a sound in `view_fx.c` and `sfx.c`.
- **Obstacles or wrap-around walls:** `snake_body_blocked` in `snake_body.c` is the one
  place that decides what the head may move into.
