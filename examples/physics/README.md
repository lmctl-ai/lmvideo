# Physics examples

These examples are motion-first art studies for the physics book in the v0.1.38 release. They
use a fixed Jolt trajectory baked forward once, then replay its poses with the
smooth `sin²(pi*f/N)` phase. That is explicitly time-replayed physics; the
examples do not reverse-integrate a simulation or splice in a last-frame reset.

The native set now contains fifteen runnable scenes:

- `boxes-collapse.lms` — a pastel stack caught mid-collapse.
- `ballpit-impact.lms` — spheres arriving in a shallow pit; the PNG is a
  mid-flight impact frame.
- `domino-wave.lms` — one real impulse traveling through a domino wave.
- `plinko.lms` — seeded balls descending a peg field.
- `pyramid-collapse.lms` — an impulse-driven pyramid collapse.
- `rolling-ramp.lms` — a sphere rolling from a physical ramp.
- `impact-rings.lms` — a collision impact witness.
- `kinetic-sculpture.lms` — a seeded sphere sculpture settling on a floor.
- `title-drop.lms` — the real words IDEAS MOVE as physical letter boxes.
- `bar-stack.lms` — BUILD and SHIP as stacked presentation units.
- `bullet-container.lms` — labeled bullet balls tumbling into the frame.
- `kpi-42.lms` — a real 42-grain KPI heap in motion.
- `slide-tumble.lms` — a presentation slide tumble and explicit reassembly.
- `letter-drop.lms` — MAKE IT MOVE as a readable moving title.
- `contact-sweep.lms` — eight colliding spheres with soft floor contact.

Each script uses `run_lmcode`, writes under `build/physics/`, and keeps the
camera/depth renderer in the loop. Plinko and ballpit draw alpha-blended,
depth-tested ghosts sampled from wrapped playback history. These are visual
motion cues, not extra simulated bodies. Each published loop measures cyclic
adjacent pixel deltas and checks the join against its neighbors.

Poster PNGs select explicit interior action frames rather than the loop start.
The artifact gate compares each still with that MP4 frame (allowing encoding
noise and the reserved caption band), and the catalog records the frame index,
movie hash and match error. Choosing a new poster does not re-time the movie.

Run a chapter with compiler 0.2.456 and the v0.1.38 release host:

```sh
lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmvideo/main/examples/physics/plinko.lms
```

Each source pins v0.1.38 for reproducible runs. The [book catalog](../books.json)
records source hashes, measured loop evidence, media and rights; the
[physics API](../../docs/physics.md) explains units and limits.

All loops replay a baked forward simulation through a smooth periodic phase; they do not reverse the solver or splice a reset frame. Each script explicitly paints rendered Images into the video Canvas. Frame count alone is not the artifact gate.
