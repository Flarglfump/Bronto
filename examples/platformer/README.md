# Platformer (an example project)

A small level made with the editor, using physics and scripts and no C code:

- `main.scene`: the level. The ground, the platforms and the walls are static colliders.
  The crates and the ball are dynamic rigid bodies. The coins are trigger colliders.
- `scripts/player.bs`: running and jumping, with a raycast to check for ground.
- `scripts/coin.bs`: spins, and when the player touches it, tells the HUD and removes itself.
- `scripts/hud.bs`: counts the coins.

Open it in the editor with **Load Project** (or `build/editor examples/platformer`)
and press Play. A game plays it the same way: register the built-in components, load
`main.scene` and let the engine's systems run (see "Using scenes in a game" in
tools/editor/README.md).

Arrows (or A/D) run; space (or up/W) jumps. On a phone or tablet (the web build), touching the
screen brings up ◀ ▶ and JUMP buttons, which press those keys (`touch_controls` in
`project.bronto`), and the help in the corner names them.
