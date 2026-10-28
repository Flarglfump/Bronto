# Marble (a 3D example project)

A small 3D game made with the editor and scripts, no C code: roll a marble up a course
of stone walkways, a ramp and a lift, collect the eight gems, and reach the flag.

- `main.scene`: the course. Every block is a scaled cube **Mesh** with a **Collider3D**
  (static scenery); the marble is a textured sphere with a **RigidBody3D**; the lift is a
  kinematic body; gems and the goal are trigger colliders. A **Camera3D**, a directional
  **Light** (the sun) and an ambient one light it. The HUD is ordinary 2D **Labels**, drawn
  over the 3D view.
- `scripts/marble.bs`: rolling (forces), jumping (an impulse, with a raycast to check for
  ground), and falling back to the last checkpoint (a tagged trigger).
- `scripts/camera.bs`: follows the marble and looks at it (`look_at`).
- `scripts/gem.bs`, `scripts/goal.bs`: spinning, bobbing gems and the flag (`on_trigger`).
- `scripts/lift.bs`: a kinematic platform moved by setting its velocity.
- `scripts/hud.bs`: the gem count, the clock and messages (a 2D script).
- `models/gem.obj`: the gem, a Wavefront model. `art/`: the textures.
- `tools/make_marble.py`: how the first version of the course was laid out (it rewrites
  `main.scene`; after that, the editor is the place to change it).

The 3D scripts start with `@strict @3d` (see docs/3d.md and docs/scripting.md).

Open it in the editor with **Load Project** (or `build/editor examples/marble`): it opens
in the 3D view. Play it with `make marble`, or press Play in the editor.

Arrows, WASD or the left stick roll; space or A jumps. At the flag, R or Start plays again.
