# Physics in motion

lmvideo uses Jolt Physics on the CPU. Boxes and spheres share a fixed-step,
right-handed Y-up world with the existing Scene3D renderer. They can fall,
collide, stack, bounce and receive impulses. Capsules, convex hulls and joints
are not exposed by this first API.

```text
let material = physics_material(0.55, 0.4);
let world = physics_world(1.0 / 120.0, 42);
world.box(8.0, 0.2, 8.0, 0.0, -0.1, 0.0, 0.0, material);
let ball = world.sphere(0.4, 0.0, 3.0, 0.0, 1.0, material);
let node = scene3d().add(mesh_sphere(0.4, 16, 24), "#ef8f8f");
world.step(120);
ball.sync(node);
```

Box arguments are full width/height/depth, position x/y/z, mass and material;
sphere arguments are radius, position x/y/z, mass and material. Units are
metres, kilograms and seconds. Mass zero is static. Friction and restitution
are 0..1. Choose `world.planar(true)` before adding bodies to constrain motion
to X/Y translation and Z rotation. Gravity changes wake resting dynamic bodies.
`body(id)` reuses a stable handle. Position/quaternion/velocity getters read the
actual simulation; synchronization changes node position and rotation without
squashing its mesh. Parented, keyframed or detached nodes refuse synchronization.
Match render geometry to collider dimensions, including floors and ramps.

## Baking a loop

`world.bake(frames, stride)` records the current poses, then advances the world
forward by `(frames-1)*stride` fixed updates. The immutable `PhysicsTake` retains
those samples even if the live world changes. `take.pose(t, body_id)` interpolates
position and a normalized quaternion for t in 0..1; `.sync(node)` displays it.
Use `sin(pi*frame/frame_count)^2` to replay a take periodically. This is explicitly
time-replayed physics, with forward and backward playback; the solver never
integrates backwards. A duplicated first frame after a discontinuous last frame
is not a seamless loop. The artifact gate measures the actual decoded seam.

One bake allows 2..512 samples, stride 1..1000, at most 10,000 updates and
262,144 stored body poses. All retained takes together allow 524,288 poses.
The recording refuses an empty world or an exhausted update/memory budget.
The base world permits 2048 bodies, four simultaneous worlds, and one million
fixed updates per world; per-call stepping is capped at 10,000 updates.

## Physics-led presentations

`physics_presentation(width, height, seed)` creates physical letter tiles,
stacked bar units, bullet balls and exact bounded KPI heaps. Its authoring
coordinates are top-left pixels; it uses the same planar physics world and
existing Canvas/Font/Image pipeline.

- `title(text,x,y,color)` creates one rigid tile per printable ASCII character.
- `bar(label,value,maximum,x,y,color)` uses twenty normalized bins, rounded nearest.
- `bullet(text,x,y,color)` creates a labeled ball.
- `kpi(label,count,x,y,color)` creates exactly 0..64 grains.
- `step(count)` advances fixed time; `render()` returns an immutable Image.
- `tumble()` removes the floor and launches the content; `assemble()` restores
  authored poses and clears motion traces. An assemble/reset must be masked or
  replayed continuously before calling its output a seamless loop.

Use the existing `Timeline` to retain presentation Images and replay them through
a periodic phase. Explicitly paint each Image with `canvas.image(image,0,0)` when
encoding video. Rendering an Image alone does not paint the active video frame.

The [physics examples](../examples/physics/README.md) and
[book catalog](../examples/books.json) target the v0.1.38 prerelease, with
compiler 0.2.456. The scripts explicitly pin the host release; the owner
promotes it after the five-OS integration gate. No implementation sources
are needed to run these chapters.
